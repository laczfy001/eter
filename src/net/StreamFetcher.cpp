#include "core/ErrorLog.h"
#include "StreamFetcher.h"

#include "i18n.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace {
constexpr size_t kMaxBuffer = 512 * 1024;      // kb. 30 mp 128 kbit/s-nál; ha nem olvassák, a legrégebbi esik ki
std::atomic<int> gGiveUpMs{15000};

QString metaText(const QByteArray &b)
{
    const QString u = QString::fromUtf8(b);
    return u.contains(QChar::ReplacementCharacter) ? QString::fromLatin1(b) : u;
}
}

// A szál saját objektuma: a hálózati kérés, az újrakapcsolódás, a lejátszási lista és az ICY metaadat.
class StreamWorker : public QObject {
public:
    explicit StreamWorker(QtStreamInput *in) : in_(in)
    {
        retry_.setSingleShot(true);
        connect(&retry_, &QTimer::timeout, this, [this] { connectTo(url_); });
    }

    void start(const QUrl &u)
    {
        original_ = u;
        connectTo(u);
    }

private:
    QtStreamInput *in_;
    QNetworkAccessManager nam_;
    QPointer<QNetworkReply> reply_;
    QTimer retry_;
    QUrl url_, original_;
    bool headersSeen_ = false, playlist_ = false;
    QByteArray body_;
    int metaint_ = 0, audioLeft_ = 0, metaRemaining_ = 0;
    QByteArray meta_;
    int hops_ = 0, failures_ = 0;

    void connectTo(const QUrl &u)
    {
        if (in_->aborted_.load()) return;
        url_ = u;
        headersSeen_ = playlist_ = false;
        body_.clear();
        metaint_ = audioLeft_ = metaRemaining_ = 0;
        QNetworkRequest rq(u);
        rq.setRawHeader("Icy-MetaData", "1");
        rq.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Eter/" ETER_VERSION));
        rq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        rq.setTransferTimeout(10000);   // 10 mp adat nélkül: megszakadt
        in_->setState(failures_ ? TR("újrakapcsolódás…") : TR("kapcsolódás…"));
        reply_ = nam_.get(rq);
        connect(reply_, &QNetworkReply::readyRead, this, &StreamWorker::onData);
        connect(reply_, &QNetworkReply::finished, this, &StreamWorker::onFinished);
    }

    void headers()
    {
        headersSeen_ = true;
        const QString type = reply_->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
        const QString path = reply_->url().path().toLower();
        playlist_ = type.contains(QLatin1String("scpls")) || type.contains(QLatin1String("mpegurl"))
                    || ((path.endsWith(QLatin1String(".pls")) || path.endsWith(QLatin1String(".m3u"))
                         || path.endsWith(QLatin1String(".m3u8")))
                        && !type.startsWith(QLatin1String("audio/mpeg")) && !type.startsWith(QLatin1String("audio/aac")));
        metaint_ = reply_->rawHeader("icy-metaint").toInt();
        audioLeft_ = metaint_;
        const QString name = metaText(reply_->rawHeader("icy-name"));
        if (!name.isEmpty()) in_->setMeta(QString(), name);
    }

    void onData()
    {
        if (!reply_) return;
        if (!headersSeen_) headers();
        const QByteArray d = reply_->readAll();
        if (playlist_) {
            body_ += d.left(65536 - body_.size());
            return;
        }
        if (!d.isEmpty() && failures_) failures_ = 0;
        in_->setState(TR("szól"));
        if (metaint_ <= 0) {
            in_->push(d.constData(), d.size());
            return;
        }
        // ICY: metaint bájt hang, utána egy hosszbájt (×16) és annyi metaadat.
        int pos = 0;
        while (pos < d.size()) {
            if (metaRemaining_ > 0) {
                const int n = std::min<int>(metaRemaining_, d.size() - pos);
                meta_.append(d.constData() + pos, n);
                metaRemaining_ -= n;
                pos += n;
                if (metaRemaining_ == 0) parseMeta();
                continue;
            }
            if (audioLeft_ == 0) {
                const int len = static_cast<unsigned char>(d[pos++]) * 16;
                meta_.clear();
                metaRemaining_ = len;
                audioLeft_ = metaint_;
                continue;
            }
            const int n = std::min<int>(audioLeft_, d.size() - pos);
            in_->push(d.constData() + pos, n);
            audioLeft_ -= n;
            pos += n;
        }
    }

    void parseMeta()
    {
        const QString m = metaText(meta_);
        const int a = m.indexOf(QLatin1String("StreamTitle='"));
        if (a < 0) return;
        const int b = m.indexOf(QLatin1String("';"), a + 13);
        in_->setMeta(m.mid(a + 13, b < 0 ? -1 : b - a - 13).trimmed(), QString());
    }

    void onFinished()
    {
        QNetworkReply *r = reply_;
        if (!r) return;
        r->deleteLater();
        reply_ = nullptr;
        if (in_->aborted_.load()) return;
        if (playlist_) {
            // .pls (FileN=…) vagy .m3u: az első stream címe. HLS-t (#EXT-X-) nem tud.
            const QString text = QString::fromUtf8(body_);
            if (text.contains(QLatin1String("#EXT-X-"))) {
                in_->setState(TR("HLS stream: nem támogatott"));
                elog::error(QStringLiteral("Stream"), TR("%1: HLS stream, nem támogatott").arg(in_->url_));
                return;
            }
            for (QString line : text.split(QLatin1Char('\n'))) {
                line = line.trimmed();
                if (line.startsWith(QLatin1String("File"), Qt::CaseInsensitive) && line.contains(QLatin1Char('=')))
                    line = line.section(QLatin1Char('='), 1).trimmed();
                if (line.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)
                    || line.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
                    if (++hops_ > 5) break;
                    connectTo(QUrl(line));
                    return;
                }
            }
            in_->setState(TR("a lejátszási listában nincs stream"));
            elog::error(QStringLiteral("Stream"), TR("%1: a lejátszási listában nincs stream").arg(in_->url_));
            return;
        }
        ++failures_;
        const QString why = r->error() != QNetworkReply::NoError ? r->errorString() : TR("a szerver lezárta a kapcsolatot");
        in_->setState(TR("megszakadt (%1), újrakapcsolódás…").arg(why));
        elog::warning(QStringLiteral("Stream"), TR("%1: megszakadt (%2), újrakapcsolódás").arg(in_->url_, why));
        retry_.start(std::min(5000, 1000 * failures_));
    }
};

QtStreamInput::QtStreamInput(const QString &url, int giveUpMs) : url_(url), giveUpMs_(giveUpMs)
{
    lastData_ = std::chrono::steady_clock::now();
    state_ = i18n::textStd("kapcsolódás…");
    start();
}

QtStreamInput::~QtStreamInput()
{
    abort();
    wait();
}

void QtStreamInput::run()
{
    {
        StreamWorker w(this);
        QTimer::singleShot(0, &w, [&w, this] { w.start(QUrl(url_)); });
        QTimer poll;                 // a leállítás akkor is érvényesüljön, ha az eseményhurok indulása előtt jött
        QObject::connect(&poll, &QTimer::timeout, &poll, [this] { if (aborted_.load()) quit(); });
        poll.start(200);
        if (!aborted_.load()) exec();
    }
}

void QtStreamInput::abort()
{
    aborted_.store(true);
    cv_.notify_all();
    quit();
}

void QtStreamInput::setPlaying(bool on)
{
    std::lock_guard<std::mutex> lk(m_);
    if (on && !playing_) lastData_ = std::max(lastData_, std::chrono::steady_clock::now());
    // Az első indításkor az adás előtt összegyűlt adatból csak a legfrissebb néhány másodperc marad: így az élő
    // adáshoz közel indul, nem a betöltés pillanatától.
    if (on && !startedOnce_) {
        startedOnce_ = true;
        constexpr size_t kKeep = 48 * 1024;
        if (buf_.size() > kKeep) buf_.erase(buf_.begin(), buf_.begin() + (buf_.size() - kKeep));
    }
    playing_ = on;
    cv_.notify_all();
}

int QtStreamInput::read(uint8_t *out, int size)
{
    std::unique_lock<std::mutex> lk(m_);
    while (true) {
        if (aborted_.load() || gaveUp_) return 0;
        if (!buf_.empty()) {
            const int n = std::min<int>(size, int(buf_.size()));
            std::copy(buf_.begin(), buf_.begin() + n, out);
            buf_.erase(buf_.begin(), buf_.begin() + n);
            return n;
        }
        // Adásban: ha a megadott ideig nem jön adat, feladja (az elem véget ér). Adás előtt vár.
        if (playing_ && std::chrono::steady_clock::now() - lastData_ > std::chrono::milliseconds(giveUpMs_)) {
            gaveUp_ = true;
            state_ = i18n::textStd("nem érhető el, továbblép");
            elog::error(QStringLiteral("Stream"), TR("%1: %2 mp óta nem jön adat, az elem véget ért (a playlist továbblép)")
                                                      .arg(url_).arg(giveUpMs_ / 1000));
            return 0;
        }
        cv_.wait_for(lk, std::chrono::milliseconds(100));
    }
}

void QtStreamInput::push(const char *data, qint64 n)
{
    if (n <= 0) return;
    std::lock_guard<std::mutex> lk(m_);
    buf_.insert(buf_.end(), data, data + n);
    if (buf_.size() > kMaxBuffer) buf_.erase(buf_.begin(), buf_.begin() + (buf_.size() - kMaxBuffer));
    lastData_ = std::chrono::steady_clock::now();
    cv_.notify_all();
}

void QtStreamInput::setState(const QString &s)
{
    std::lock_guard<std::mutex> lk(m_);
    if (!gaveUp_) state_ = s.toStdString();
}

void QtStreamInput::setMeta(const QString &title, const QString &name)
{
    std::lock_guard<std::mutex> lk(m_);
    if (!title.isNull()) title_ = title.toStdString();
    if (!name.isNull()) name_ = name.toStdString();
}

std::string QtStreamInput::streamTitle() const
{
    std::lock_guard<std::mutex> lk(m_);
    return title_;
}

std::string QtStreamInput::stationName() const
{
    std::lock_guard<std::mutex> lk(m_);
    return name_;
}

std::string QtStreamInput::state() const
{
    std::lock_guard<std::mutex> lk(m_);
    return state_;
}

void QtStreamInput::setGiveUpMs(int ms) { gGiveUpMs.store(std::clamp(ms, 3000, 120000)); }
int QtStreamInput::giveUpMs() { return gGiveUpMs.load(); }

void QtStreamInput::install()
{
    setGiveUpMs(QSettings().value("stream/giveUpS", 15).toInt() * 1000);
    eter::setStreamFactory([](const std::string &url) -> std::shared_ptr<eter::StreamInput> {
        return std::make_shared<QtStreamInput>(QString::fromStdString(url), gGiveUpMs.load());
    });
}
