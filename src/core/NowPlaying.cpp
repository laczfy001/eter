#include "core/NowPlaying.h"
#include "i18n.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTime>

NowPlaying::NowPlaying(QObject *parent) : QObject(parent) {}

QString NowPlaying::defaultPath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty()) base = QDir::homePath();
    return QDir(base).filePath(QStringLiteral("nowplaying.txt"));
}

NowPlaying::Config NowPlaying::loadConfig()
{
    QSettings s;
    Config c;
    c.fileEnabled = s.value("nowplaying/enabled", false).toBool();
    c.path = s.value("nowplaying/path").toString();
    c.format = s.value("nowplaying/format", c.format).toString();
    c.bom = s.value("nowplaying/bom", false).toBool();
    const QString nm = s.value("nowplaying/nonMusic", "keep").toString();
    c.nonMusic = nm == QLatin1String("own") ? OwnTitle : nm == QLatin1String("text") ? FixedText : KeepLast;
    c.nonMusicText = s.value("nowplaying/nonMusicText").toString();
    c.idleText = s.value("nowplaying/idleText").toString();
    c.icecastEnabled = s.value("nowplaying/icecast", false).toBool();
    c.icecastUrl = s.value("nowplaying/icecastUrl", c.icecastUrl).toString();
    c.icecastMount = s.value("nowplaying/icecastMount", c.icecastMount).toString();
    c.icecastUser = s.value("nowplaying/icecastUser", c.icecastUser).toString();
    c.icecastPassword = s.value("nowplaying/icecastPassword").toString();
    return c;
}

void NowPlaying::saveConfig(const Config &c)
{
    QSettings s;
    s.setValue("nowplaying/enabled", c.fileEnabled);
    s.setValue("nowplaying/path", c.path);
    s.setValue("nowplaying/format", c.format);
    s.setValue("nowplaying/bom", c.bom);
    s.setValue("nowplaying/nonMusic", c.nonMusic == OwnTitle ? "own" : c.nonMusic == FixedText ? "text" : "keep");
    s.setValue("nowplaying/nonMusicText", c.nonMusicText);
    s.setValue("nowplaying/idleText", c.idleText);
    s.setValue("nowplaying/icecast", c.icecastEnabled);
    s.setValue("nowplaying/icecastUrl", c.icecastUrl);
    s.setValue("nowplaying/icecastMount", c.icecastMount);
    s.setValue("nowplaying/icecastUser", c.icecastUser);
    s.setValue("nowplaying/icecastPassword", c.icecastPassword);
}

void NowPlaying::configure(const Config &c)
{
    cfg_ = c;
    fileError_.clear();
    iceError_.clear();
    publish(true);
}

static QString mmss(qint64 ms)
{
    const qint64 s = std::max<qint64>(0, ms / 1000);
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QString NowPlaying::render(const QString &format, const Track &t, const Track &next)
{
    QString out = format;
    out.replace(QLatin1String("%title%"), t.title);
    out.replace(QLatin1String("%artist%"), t.artist);
    out.replace(QLatin1String("%category%"), t.category);
    out.replace(QLatin1String("%duration%"), t.durationMs > 0 ? mmss(t.durationMs) : QString());
    out.replace(QLatin1String("%next_title%"), next.title);
    out.replace(QLatin1String("%next_artist%"), next.artist);
    out.replace(QLatin1String("%time%"), QTime::currentTime().toString(QStringLiteral("HH:mm")));
    out.replace(QLatin1String("\\n"), QLatin1String("\n"));
    // Hiányzó mező után ne maradjon árva elválasztó („ - Cím”, „Előadó - ”, „Most:  - Cím”). Csak a szóközökkel
    // körülvett elválasztókat érinti, így a címekben lévő kötőjel („Jay-Z”, „AC/DC”) megmarad.
    static const QRegularExpression lead(QStringLiteral("^(\\s*[\\-–—|·/:,]\\s+|\\s+)+")),
        trail(QStringLiteral("(\\s+[\\-–—|·/:,]\\s*|\\s+)+$")),
        dashes(QStringLiteral("\\s+([\\-–—|·/])\\s+[\\-–—|·/](?=\\s)")),
        afterColon(QStringLiteral("([:,])\\s+[\\-–—|·/](?=\\s)"));
    QStringList lines = out.split(QLatin1Char('\n'));
    for (QString &l : lines) {
        for (int i = 0; i < 4; ++i) {
            const QString before = l;
            l.replace(dashes, QStringLiteral(" \\1"));
            l.replace(afterColon, QStringLiteral("\\1"));
            if (l == before) break;
        }
        l.remove(lead);
        l.remove(trail);
        l.replace(QRegularExpression(QStringLiteral(" {2,}")), QStringLiteral(" "));
    }
    return lines.join(QLatin1Char('\n'));
}

void NowPlaying::setTracks(const Track &onAir, const Track &next)
{
    if (onAir == onAir_ && next == next_) return;
    onAir_ = onAir;
    next_ = next;
    if (onAir.valid()) {
        everPlayed_ = true;
        if (onAir.music) lastMusic_ = onAir;
    }
    publish();
}

void NowPlaying::writeIdle()
{
    if (!everPlayed_) return;
    onAir_ = Track();
    publish(true);
}

void NowPlaying::publish(bool force)
{
    if (!cfg_.fileEnabled && !cfg_.icecastEnabled) return;
    QString text;
    if (!onAir_.valid()) {
        if (!everPlayed_) return;                    // induláskor nem írja felül a fájlt, amíg nem szólt semmi
        text = cfg_.idleText;
    } else if (onAir_.music) {
        text = render(cfg_.format, onAir_, next_);
    } else {
        switch (cfg_.nonMusic) {
        case OwnTitle: text = render(cfg_.format, onAir_, next_); break;
        case FixedText: text = cfg_.nonMusicText; break;
        case KeepLast:
            if (!lastMusic_.valid()) return;          // még nem volt zene: marad, ami volt
            text = render(cfg_.format, lastMusic_, next_);
            break;
        }
    }
    if (hasText_ && text == text_ && !force) return;
    text_ = text;
    hasText_ = true;
    if (cfg_.fileEnabled) writeFile(text);
    if (cfg_.icecastEnabled) sendIcecast(text);
    emit changed(text);
}

bool NowPlaying::writeFile(const QString &text)
{
    const QString path = cfg_.path.isEmpty() ? defaultPath() : cfg_.path;
    QDir().mkpath(QFileInfo(path).absolutePath());
    QByteArray data = text.toUtf8();
    if (cfg_.bom) data.prepend("\xEF\xBB\xBF");
    // Előbb ideiglenes fájlba, aztán csere: az olvasó program soha nem lát félig írt sort.
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly) && f.write(data) == data.size() && f.commit()) {
        fileError_.clear();
        fileAt_ = QDateTime::currentDateTime();
        return true;
    }
    // Ha az olvasó program zárolja a fájlt (Windows), a csere nem megy: közvetlen felülírás.
    QFile direct(path);
    if (direct.open(QIODevice::WriteOnly | QIODevice::Truncate) && direct.write(data) == data.size()) {
        fileError_.clear();
        fileAt_ = QDateTime::currentDateTime();
        return true;
    }
    fileError_ = direct.errorString().isEmpty() ? f.errorString() : direct.errorString();
    return false;
}

void NowPlaying::sendIcecast(const QString &text)
{
    if (!net_) net_ = new QNetworkAccessManager(this);
    QUrl url(cfg_.icecastUrl.trimmed());
    if (!url.isValid() || url.host().isEmpty()) {
        iceError_ = TR("hibás szervercím");
        return;
    }
    QString mount = cfg_.icecastMount.trimmed();
    if (!mount.startsWith(QLatin1Char('/'))) mount.prepend(QLatin1Char('/'));
    url.setPath(QStringLiteral("/admin/metadata"));
    // Minden érték százalékkódolva (a „+” is, különben a szerver szóköznek olvasná).
    const QByteArray song = QUrl::toPercentEncoding(QString(text).replace(QLatin1Char('\n'), QStringLiteral(" · ")));
    url.setQuery(QStringLiteral("mount=%1&mode=updinfo&song=%2&charset=UTF-8")
                     .arg(QString::fromLatin1(QUrl::toPercentEncoding(mount, "/")), QString::fromLatin1(song)),
                 QUrl::StrictMode);
    QNetworkRequest req(url);
    req.setRawHeader("Authorization",
                     "Basic " + (cfg_.icecastUser + QLatin1Char(':') + cfg_.icecastPassword).toUtf8().toBase64());
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Eter/%1").arg(QStringLiteral(ETER_VERSION)));
    req.setTransferTimeout(8000);
    const int serial = ++iceSerial_;
    QNetworkReply *r = net_->get(req);
    connect(r, &QNetworkReply::finished, this, [this, r, serial] {
        r->deleteLater();
        if (serial != iceSerial_) return;                 // közben újabb ment ki
        const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (r->error() == QNetworkReply::NoError && code == 200) {
            iceError_.clear();
            iceAt_ = QDateTime::currentDateTime();
        } else if (code == 401) {
            iceError_ = TR("a szerver nem fogadta el a felhasználónevet vagy a jelszót");
        } else if (code == 400 || code == 404) {
            iceError_ = TR("a szerver nem ismeri ezt a mount pointot (%1), vagy most nem kap rá adást").arg(cfg_.icecastMount);
        } else {
            iceError_ = r->errorString();
        }
    });
}

QString NowPlaying::status() const
{
    QStringList parts;
    if (cfg_.fileEnabled) {
        const QString path = QDir::toNativeSeparators(cfg_.path.isEmpty() ? defaultPath() : cfg_.path);
        parts << (!fileError_.isEmpty() ? TR("fájl: hiba – %1").arg(fileError_)
                  : fileAt_.isValid()   ? TR("fájl: %1 (%2)").arg(path, fileAt_.toString(QStringLiteral("HH:mm:ss")))
                                        : TR("fájl: %1 (az első elem indulásakor íródik)").arg(path));
    }
    if (cfg_.icecastEnabled)
        parts << (!iceError_.isEmpty() ? TR("Icecast: hiba – %1").arg(iceError_)
                  : iceAt_.isValid()   ? TR("Icecast: frissítve %1").arg(iceAt_.toString(QStringLiteral("HH:mm:ss")))
                                       : TR("Icecast: az első elem indulásakor küldi"));
    if (parts.isEmpty()) return TR("kikapcsolva");
    if (hasText_) parts << TR("most: „%1”").arg(QString(text_).replace(QLatin1Char('\n'), QStringLiteral(" ⏎ ")));
    return parts.join(QStringLiteral(" · "));
}
