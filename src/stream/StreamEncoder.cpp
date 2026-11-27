#include "stream/StreamEncoder.h"

#include "core/ErrorLog.h"
#include "i18n.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QLocale>
#include <QTime>
#include <QJsonDocument>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSslSocket>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <lame/lame.h>
#include <vorbis/vorbisenc.h>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>
#include <vector>

namespace stream {

static constexpr int kInRate = 48000;

// ---------------------------------------------------------------------------------------------- beállítások

static QString serverKey(Server s)
{
    switch (s) {
    case Server::Shoutcast1: return QStringLiteral("shoutcast1");
    case Server::Shoutcast2: return QStringLiteral("shoutcast2");
    default: return QStringLiteral("icecast");
    }
}

QJsonObject Config::toJson() const
{
    QJsonObject o;
    o["id"] = id;
    o["name"] = name;
    o["autoStart"] = autoStart;
    o["server"] = serverKey(server);
    o["host"] = host;
    o["port"] = port;
    o["mount"] = mount;
    o["user"] = user;
    o["password"] = password;
    o["tls"] = tls;
    o["sid"] = sid;
    o["format"] = format == Format::Vorbis ? QStringLiteral("vorbis") : QStringLiteral("mp3");
    o["kbps"] = kbps;
    o["abr"] = abr;
    o["sampleRate"] = sampleRate;
    o["mono"] = mono;
    o["source"] = source;
    o["gainDb"] = gainDb;
    {
        QJsonObject pr;
        pr["preset"] = proc.preset;
        pr["agc"] = proc.agc;
        pr["targetLufs"] = proc.targetLufs;
        pr["agcMaxGainDb"] = proc.agcMaxGainDb;
        pr["agcMaxCutDb"] = proc.agcMaxCutDb;
        pr["agcSpeedSec"] = proc.agcSpeedSec;
        pr["comp"] = proc.comp;
        pr["compThresholdDb"] = proc.compThresholdDb;
        pr["compRatio"] = proc.compRatio;
        pr["compAttackMs"] = proc.compAttackMs;
        pr["compReleaseMs"] = proc.compReleaseMs;
        pr["compMakeupDb"] = proc.compMakeupDb;
        pr["limiter"] = proc.limiter;
        pr["ceilingDbTp"] = proc.ceilingDbTp;
        o["proc"] = pr;
    }
    o["sendTitle"] = sendTitle;
    o["titleLikeNowPlaying"] = titleLikeNowPlaying;
    o["titleFormat"] = titleFormat;
    o["stationName"] = stationName;
    o["description"] = description;
    o["genre"] = genre;
    o["url"] = url;
    o["public"] = isPublic;
    o["reconnect"] = reconnect;
    o["retrySec"] = retrySec;
    return o;
}

Config Config::fromJson(const QJsonObject &o)
{
    Config c;
    c.id = o.value("id").toString();
    c.name = o.value("name").toString();
    c.autoStart = o.value("autoStart").toBool();
    const QString s = o.value("server").toString();
    c.server = s == QLatin1String("shoutcast1") ? Server::Shoutcast1 : s == QLatin1String("shoutcast2") ? Server::Shoutcast2 : Server::Icecast;
    c.host = o.value("host").toString();
    c.port = std::clamp(o.value("port").toInt(8000), 1, 65534);
    c.mount = o.value("mount").toString(QStringLiteral("/stream"));
    c.user = o.value("user").toString(QStringLiteral("source"));
    c.password = o.value("password").toString();
    c.tls = o.value("tls").toBool();
    c.sid = std::clamp(o.value("sid").toInt(1), 1, 999);
    c.format = o.value("format").toString() == QLatin1String("vorbis") ? Format::Vorbis : Format::Mp3;
    c.kbps = std::clamp(o.value("kbps").toInt(128), 32, 320);
    c.abr = o.value("abr").toBool();
    c.sampleRate = o.value("sampleRate").toInt(44100);
    if (c.sampleRate != 22050 && c.sampleRate != 32000 && c.sampleRate != 44100 && c.sampleRate != 48000) c.sampleRate = 44100;
    c.mono = o.value("mono").toBool();
    c.source = o.value("source").toString();
    c.gainDb = std::clamp(o.value("gainDb").toDouble(0), -12.0, 12.0);
    if (o.contains("proc")) {
        const QJsonObject pr = o.value("proc").toObject();
        const int preset = std::clamp(pr.value("preset").toInt(ProcConfig::Light), 0, int(ProcConfig::Custom));
        ProcConfig d = ProcConfig::fromPreset(preset == ProcConfig::Custom ? ProcConfig::Light : preset);
        if (preset == ProcConfig::Custom) {
            d.preset = ProcConfig::Custom;
            d.agc = pr.value("agc").toBool(d.agc);
            d.agcMaxGainDb = std::clamp(pr.value("agcMaxGainDb").toDouble(d.agcMaxGainDb), 0.0, 24.0);
            d.agcMaxCutDb = std::clamp(pr.value("agcMaxCutDb").toDouble(d.agcMaxCutDb), 0.0, 24.0);
            d.agcSpeedSec = std::clamp(pr.value("agcSpeedSec").toDouble(d.agcSpeedSec), 1.0, 30.0);
            d.comp = pr.value("comp").toBool(d.comp);
            d.compThresholdDb = std::clamp(pr.value("compThresholdDb").toDouble(d.compThresholdDb), -40.0, 0.0);
            d.compRatio = std::clamp(pr.value("compRatio").toDouble(d.compRatio), 1.0, 20.0);
            d.compAttackMs = std::clamp(pr.value("compAttackMs").toDouble(d.compAttackMs), 0.5, 200.0);
            d.compReleaseMs = std::clamp(pr.value("compReleaseMs").toDouble(d.compReleaseMs), 20.0, 2000.0);
            d.compMakeupDb = std::clamp(pr.value("compMakeupDb").toDouble(d.compMakeupDb), 0.0, 12.0);
            d.limiter = pr.value("limiter").toBool(d.limiter);
        }
        d.targetLufs = std::clamp(pr.value("targetLufs").toDouble(-16), -30.0, -8.0);
        d.ceilingDbTp = std::clamp(pr.value("ceilingDbTp").toDouble(-1), -6.0, 0.0);
        c.proc = d;
    } else {
        // 0.37.0 előtti beállítás: csak a csúcskorlátozó volt; a kikapcsolt korlátozó „Ki”, egyébként „Enyhe”.
        c.proc = ProcConfig::fromPreset(o.value("limiter").toBool(true) ? ProcConfig::Light : ProcConfig::Off);
    }
    c.sendTitle = o.value("sendTitle").toBool(true);
    c.titleLikeNowPlaying = o.value("titleLikeNowPlaying").toBool(true);
    c.titleFormat = o.value("titleFormat").toString(QStringLiteral("%artist% - %title%"));
    c.stationName = o.value("stationName").toString();
    c.description = o.value("description").toString();
    c.genre = o.value("genre").toString();
    c.url = o.value("url").toString();
    c.isPublic = o.value("public").toBool();
    c.reconnect = o.value("reconnect").toBool(true);
    c.retrySec = std::clamp(o.value("retrySec").toInt(5), 1, 300);
    return c;
}

bool Config::connectionDiffers(const Config &o) const
{
    return server != o.server || host != o.host || port != o.port || mount != o.mount || user != o.user
           || password != o.password || tls != o.tls || sid != o.sid || format != o.format || kbps != o.kbps || abr != o.abr
           || sampleRate != o.sampleRate || mono != o.mono || stationName != o.stationName || description != o.description
           || genre != o.genre || url != o.url || isPublic != o.isPublic;
}

QString Config::contentType() const
{
    return format == Format::Vorbis ? QStringLiteral("application/ogg") : QStringLiteral("audio/mpeg");
}

QString Config::endpoint() const
{
    const QString h = host.isEmpty() ? TR("[nincs szerver]") : host;
    switch (server) {
    case Server::Shoutcast1: return QStringLiteral("Shoutcast v1 · %1:%2").arg(h).arg(port);
    case Server::Shoutcast2: return QStringLiteral("Shoutcast v2 · %1:%2 · SID %3").arg(h).arg(port).arg(sid);
    default: return QStringLiteral("Icecast · %1:%2%3").arg(h).arg(port).arg(mount);
    }
}

QString Config::formatText() const
{
    return format == Format::Vorbis ? QStringLiteral("Ogg Vorbis") : QStringLiteral("MP3");
}

QString Config::audioText() const
{
    const QString rate = QLocale().toString(sampleRate / 1000.0, 'g', 3) + QStringLiteral(" kHz");
    return rate + QStringLiteral(" · ") + (mono ? TR("mono") : TR("sztereó"));
}

QString stateLabel(State s)
{
    switch (s) {
    case State::Connecting: return TR("KAPCSOLÓDIK");
    case State::OnAir: return TR("AD");
    case State::Retrying: return TR("ÚJRAPRÓBÁL");
    case State::Error: return TR("HIBA");
    default:
        // A „KI” a keverőben a kimenetet jelenti (angolul OUT); itt a kikapcsolt állapotot.
        return i18n::language() == QLatin1String("en") ? QStringLiteral("OFF") : QStringLiteral("KI");
    }
}

// ---------------------------------------------------------------------------------------------- közös adatok

struct Shared {
    std::mutex m;
    std::deque<float> queue;          // sztereó, 48 kHz, átlapolt
    Status st;
    QStringList events;
    float gain = 1;                   // élő: a szint a streamre (lineáris)
    ProcConfig proc;
    bool procDirty = true;
    std::function<void(const float *, int)> monitor;
};

static constexpr size_t kMaxQueue = size_t(kInRate) * 2 * 10;   // 10 mp

// ---------------------------------------------------------------------------------------------- a munkaszál

class Worker : public QObject {
    Q_OBJECT
public:
    Worker(std::shared_ptr<Shared> sh, const Config &c) : sh_(std::move(sh)), cfg_(c) {}
    ~Worker() override { closeCodec(); }

public slots:
    void init()
    {
        nam_ = new QNetworkAccessManager(this);
        tick_ = new QTimer(this);
        tick_->setInterval(50);
        connect(tick_, &QTimer::timeout, this, &Worker::onTick);
        tick_->start();
        retry_ = new QTimer(this);
        retry_->setSingleShot(true);
        connect(retry_, &QTimer::timeout, this, [this] { connectNow(); });
        handshake_ = new QTimer(this);
        handshake_->setSingleShot(true);
        connect(handshake_, &QTimer::timeout, this, [this] { fail(TR("A szerver nem válaszol."), false); });
        stats_ = new QTimer(this);
        stats_->setInterval(15000);
        connect(stats_, &QTimer::timeout, this, &Worker::fetchStats);
    }

    void setConfig(const stream::Config &c) { cfg_ = c; }

    void start(bool testOnly)
    {
        closeConnection();
        retry_->stop();
        testOnly_ = testOnly;
        attempt_ = 0;
        sourceMethod_ = false;
        everConnected_ = false;
        if (cfg_.host.trimmed().isEmpty()) {
            setState(State::Error, TR("Nincs megadva a szerver címe."));
            return;
        }
        if (cfg_.format == Format::Vorbis && cfg_.server != Server::Icecast) {
            setState(State::Error, TR("Ogg Vorbis csak Icecast szerverre küldhető; Shoutcasthoz válaszd az MP3-at."));
            return;
        }
        connectNow();
    }

    void stop()
    {
        retry_->stop();
        const bool was = phase_ != Phase::Idle;
        closeConnection();
        if (was) log(TR("Leállítva."));
        setState(State::Off, TR("Nincs elindítva."));
    }

    void shutdown()
    {
        retry_->stop();
        if (tick_) tick_->stop();
        closeConnection();
    }

    void setTitle(const QString &t)
    {
        if (t == title_) return;
        title_ = t;
        titleDirty_ = true;
    }

signals:
    void event(const QString &line);
    void stateChanged();

private:
    enum class Phase { Idle, Connecting, Handshake, Streaming };

    // ------------------------------------------------------------------ állapot és napló

    void log(const QString &text)
    {
        const QString line = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")) + QStringLiteral("  ") + text;
        {
            std::lock_guard<std::mutex> lk(sh_->m);
            sh_->events << line;
            while (sh_->events.size() > 200) sh_->events.removeFirst();
        }
        emit event(line);
    }

    void setState(State s, const QString &msg)
    {
        {
            std::lock_guard<std::mutex> lk(sh_->m);
            sh_->st.state = s;
            sh_->st.message = msg;
            sh_->st.attempt = attempt_;
            sh_->st.retryAt = s == State::Retrying ? retryAt_ : 0;
            if (s != State::OnAir) sh_->st.since = 0;
            if (s == State::OnAir && sh_->st.since == 0) sh_->st.since = QDateTime::currentMSecsSinceEpoch();
            if (s != State::OnAir) sh_->st.listeners = -1;
        }
        emit stateChanged();
    }

    QString who() const { return cfg_.name.isEmpty() ? cfg_.endpoint() : cfg_.name; }

    // ------------------------------------------------------------------ kapcsolat

    void connectNow()
    {
        closeConnection();
        phase_ = Phase::Connecting;
        respBuf_.clear();
        setState(State::Connecting, TR("Kapcsolódás a szerverhez…"));
        const bool shout = cfg_.server != Server::Icecast;
        const int port = shout ? cfg_.port + 1 : cfg_.port;      // Shoutcast: a forrás a következő porton
        if (cfg_.tls && !shout) {
            auto *s = new QSslSocket(this);
            sock_ = s;
            connect(s, &QSslSocket::encrypted, this, &Worker::sendHandshake);
            wire();
            s->connectToHostEncrypted(cfg_.host.trimmed(), quint16(port));
        } else {
            sock_ = new QTcpSocket(this);
            connect(sock_, &QTcpSocket::connected, this, &Worker::sendHandshake);
            wire();
            sock_->connectToHost(cfg_.host.trimmed(), quint16(port));
        }
        handshake_->start(15000);
    }

    void wire()
    {
        connect(sock_, &QTcpSocket::readyRead, this, &Worker::onRead);
        connect(sock_, &QAbstractSocket::errorOccurred, this, [this](QAbstractSocket::SocketError e) {
            if (phase_ == Phase::Idle || !sock_) return;
            if (e == QAbstractSocket::RemoteHostClosedError && phase_ == Phase::Streaming) {
                fail(TR("A szerver bontotta a kapcsolatot."), false);
                return;
            }
            QString why;
            switch (e) {
            case QAbstractSocket::ConnectionRefusedError: why = TR("A szerver nem fogad kapcsolatot ezen a porton (nem fut, vagy rossz a port)."); break;
            case QAbstractSocket::HostNotFoundError: why = TR("A szerver címe nem található (%1).").arg(cfg_.host); break;
            case QAbstractSocket::SocketTimeoutError: why = TR("A szerver nem válaszol."); break;
            case QAbstractSocket::NetworkError: why = TR("Hálózati hiba (nincs internetkapcsolat?)."); break;
            case QAbstractSocket::SslHandshakeFailedError: why = TR("A titkosított (HTTPS) kapcsolat nem jött létre: %1").arg(sock_->errorString()); break;
            case QAbstractSocket::RemoteHostClosedError: why = TR("A szerver bontotta a kapcsolatot."); break;
            default: why = sock_->errorString(); break;
            }
            fail(why, false);
        });
        connect(sock_, &QTcpSocket::disconnected, this, [this] {
            if (phase_ == Phase::Streaming) fail(TR("A szerver bontotta a kapcsolatot."), false);
            else if (phase_ == Phase::Handshake) fail(TR("A szerver válasz nélkül bontotta a kapcsolatot."), false);
        });
    }

    // A fejlécek szövege. Az MP3 streamek állomásadatait az Icecast és a Shoutcast Latin-1-ként olvassa: ott Latin-1
    // megy (a magyar ő/ű helyett ö/ü, más nem ábrázolható betű helyett „?”); Ogg streamnél UTF-8.
    QByteArray hdr(const QByteArray &name, const QString &value, const char *sep = ": ") const
    {
        QString v = value;
        v.remove(QLatin1Char('\r'));
        v.remove(QLatin1Char('\n'));
        QByteArray enc;
        if (cfg_.format == Format::Vorbis) {
            enc = v.toUtf8();
        } else {
            for (QChar ch : v) {
                ushort u = ch.unicode();
                if (u == 0x0151) u = 0xF6;          // ő → ö
                else if (u == 0x0150) u = 0xD6;     // Ő → Ö
                else if (u == 0x0171) u = 0xFC;     // ű → ü
                else if (u == 0x0170) u = 0xDC;     // Ű → Ü
                enc += u < 256 ? char(u) : '?';
            }
        }
        return name + sep + enc + "\r\n";
    }

    void sendHandshake()
    {
        if (!sock_) return;
        phase_ = Phase::Handshake;
        const QByteArray ua = QStringLiteral("Eter/%1 ( https://github.com/laczfy001/eter )").arg(QCoreApplication::applicationVersion()).toUtf8();
        if (cfg_.server == Server::Icecast) {
            QString mount = cfg_.mount.trimmed();
            if (!mount.startsWith(QLatin1Char('/'))) mount.prepend(QLatin1Char('/'));
            const QByteArray auth = (cfg_.user + QLatin1Char(':') + cfg_.password).toUtf8().toBase64();
            QByteArray r;
            // Icecast 2.4-től PUT; a régebbiek csak a SOURCE módot ismerik (arra vált, ha a PUT-ot nem fogadja el).
            if (sourceMethod_) r = "SOURCE " + QUrl::toPercentEncoding(mount, "/") + " HTTP/1.0\r\n";
            else r = "PUT " + QUrl::toPercentEncoding(mount, "/") + " HTTP/1.1\r\nHost: " + cfg_.host.trimmed().toUtf8() + ":" + QByteArray::number(cfg_.port) + "\r\n";
            r += "Authorization: Basic " + auth + "\r\n";
            r += "User-Agent: " + ua + "\r\n";
            r += "Content-Type: " + cfg_.contentType().toUtf8() + "\r\n";
            r += hdr("Ice-Public", cfg_.isPublic ? QStringLiteral("1") : QStringLiteral("0"));
            if (!cfg_.stationName.isEmpty()) r += hdr("Ice-Name", cfg_.stationName);
            if (!cfg_.description.isEmpty()) r += hdr("Ice-Description", cfg_.description);
            if (!cfg_.url.isEmpty()) r += hdr("Ice-Url", cfg_.url);
            if (!cfg_.genre.isEmpty()) r += hdr("Ice-Genre", cfg_.genre);
            r += "Ice-Audio-Info: ice-samplerate=" + QByteArray::number(cfg_.sampleRate) + ";ice-bitrate=" + QByteArray::number(cfg_.kbps)
                 + ";ice-channels=" + QByteArray::number(cfg_.mono ? 1 : 2) + "\r\n";
            r += "\r\n";
            sock_->write(r);
        } else {
            // Shoutcast: előbb a jelszó (v2: „jelszó:#SID”), a válasz „OK2”; utána az állomás adatai.
            QByteArray pw = cfg_.password.toUtf8();
            if (cfg_.server == Server::Shoutcast2) pw += ":#" + QByteArray::number(cfg_.sid);
            sock_->write(pw + "\r\n");
        }
    }

    void onRead()
    {
        if (!sock_) return;
        if (phase_ != Phase::Handshake) {
            sock_->readAll();          // a szerver mást nem küld; ami jön, eldobható
            return;
        }
        respBuf_ += sock_->readAll();
        if (cfg_.server == Server::Icecast) {
            int end = respBuf_.indexOf("\r\n\r\n");
            if (end < 0) end = respBuf_.indexOf("\n\n");
            if (end < 0) {
                if (respBuf_.size() > 16384) fail(TR("Érthetetlen válasz a szervertől."), false);
                return;
            }
            const QByteArray first = respBuf_.left(respBuf_.indexOf('\n')).trimmed();
            const QList<QByteArray> parts = first.split(' ');
            const int code = parts.size() > 1 ? parts.at(1).toInt() : 0;
            const QString line = QString::fromUtf8(first);
            if (code == 200 || code == 100) {
                startStreaming();
            } else if (code == 401) {
                fail(TR("A szerver elutasította a felhasználót vagy a jelszót (401). Javítsd, majd indítsd újra."), true);
            } else if (code == 403) {
                fail(TR("A mountpoint foglalt vagy nem engedélyezett (403): már ad rá egy másik forrás, vagy a szerver nem fogadja ezt a formátumot. Adj meg másik mountpointot."), true);
            } else if ((code == 400 || code == 405 || code == 501) && !sourceMethod_) {
                // Régi Icecast: SOURCE módban újra.
                sourceMethod_ = true;
                log(TR("A szerver nem fogadja a PUT kérést, újra SOURCE módban."));
                QTimer::singleShot(0, this, [this] { connectNow(); });
            } else {
                fail(TR("A szerver hibát adott: %1").arg(line), false);
            }
            return;
        }
        const int nl = respBuf_.indexOf('\n');
        if (nl < 0) {
            if (respBuf_.size() > 4096) fail(TR("Érthetetlen válasz a szervertől."), false);
            return;
        }
        const QString line = QString::fromUtf8(respBuf_.left(nl)).trimmed();
        if (line.startsWith(QLatin1String("OK"), Qt::CaseInsensitive)) {
            QByteArray h;
            h += hdr("icy-name", cfg_.stationName, ":");
            h += hdr("icy-genre", cfg_.genre, ":");
            h += hdr("icy-url", cfg_.url, ":");
            h += hdr("icy-pub", cfg_.isPublic ? QStringLiteral("1") : QStringLiteral("0"), ":");
            h += "icy-br:" + QByteArray::number(cfg_.kbps) + "\r\n";
            h += "content-type:" + cfg_.contentType().toUtf8() + "\r\n\r\n";
            sock_->write(h);
            startStreaming();
        } else if (line.contains(QLatin1String("password"), Qt::CaseInsensitive)) {
            fail(TR("A szerver elutasította a jelszót. Javítsd, majd indítsd újra."), true);
        } else {
            fail(TR("A szerver hibát adott: %1").arg(line), false);
        }
    }

    void startStreaming()
    {
        handshake_->stop();
        if (testOnly_) {
            log(TR("A kapcsolat rendben: a szerver elfogadta a bejelentkezést (%1).").arg(cfg_.endpoint()));
            closeConnection();
            setState(State::Off, TR("A kapcsolat rendben (teszt)."));
            return;
        }
        QString err;
        if (!openCodec(&err)) {
            closeConnection();
            setState(State::Error, err);
            log(err);
            elog::error(QStringLiteral("Stream"), who() + QStringLiteral(": ") + err);
            return;
        }
        phase_ = Phase::Streaming;
        {
            // A kapcsolódás alatt összegyűlt hangból csak az utolsó fél másodperc marad (ne induljon késéssel).
            std::lock_guard<std::mutex> lk(sh_->m);
            const size_t keep = size_t(kInRate);
            while (sh_->queue.size() > keep) sh_->queue.pop_front();
            sh_->st.bytes = 0;
            sh_->st.since = 0;
        }
        if (everConnected_ || attempt_ > 0) elog::info(QStringLiteral("Stream"), TR("%1: a kapcsolat helyreállt").arg(who()));
        everConnected_ = true;
        attempt_ = 0;
        log(TR("Kapcsolódva: %1 (%2, %3 kbit/s)").arg(cfg_.endpoint(), cfg_.formatText()).arg(cfg_.kbps));
        setState(State::OnAir, QString());
        titleDirty_ = true;
        stats_->start();
        QTimer::singleShot(4000, this, &Worker::fetchStats);
    }

    void closeConnection()
    {
        handshake_->stop();
        if (stats_) stats_->stop();
        if (sock_) {
            QTcpSocket *s = sock_;
            sock_ = nullptr;
            s->disconnect(this);
            if (phase_ == Phase::Streaming) s->flush();
            s->abort();
            s->deleteLater();
        }
        phase_ = Phase::Idle;
        closeCodec();
    }

    // fatal: nincs értelme magától újrapróbálni (hibás jelszó, foglalt mountpoint).
    void fail(const QString &why, bool fatal)
    {
        if (phase_ == Phase::Idle) return;
        const bool wasStreaming = phase_ == Phase::Streaming;
        closeConnection();
        log(why);
        const QString key = QStringLiteral("stream:") + cfg_.id;
        if (testOnly_) {
            setState(State::Error, why);
            return;
        }
        if (fatal || !cfg_.reconnect) {
            elog::error(QStringLiteral("Stream"), who() + QStringLiteral(": ") + why);
            setState(State::Error, why);
            return;
        }
        elog::writeKeyed(elog::Warning, QStringLiteral("Stream"), who() + QStringLiteral(": ") + why, key, 10 * 60 * 1000);
        ++attempt_;
        retryAt_ = QDateTime::currentMSecsSinceEpoch() + qint64(cfg_.retrySec) * 1000;
        const QString msg = wasStreaming
                                ? TR("A kapcsolat megszakadt %1-kor (%2). Újracsatlakozás %3 másodperc múlva (%4. próbálkozás).")
                                      .arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss")), why)
                                      .arg(cfg_.retrySec)
                                      .arg(attempt_)
                                : TR("Nem sikerült kapcsolódni: %1 Újra %2 másodperc múlva (%3. próbálkozás).").arg(why).arg(cfg_.retrySec).arg(attempt_);
        setState(State::Retrying, msg);
        retry_->start(cfg_.retrySec * 1000);
    }

    // ------------------------------------------------------------------ kódolás

    bool openCodec(QString *err)
    {
        closeCodec();
        const int ch = cfg_.mono ? 1 : 2;
        if (cfg_.sampleRate != kInRate) {
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            if (swr_alloc_set_opts2(&swr_, &stereo, AV_SAMPLE_FMT_FLT, cfg_.sampleRate, &stereo, AV_SAMPLE_FMT_FLT, kInRate, 0, nullptr) < 0
                || swr_init(swr_) < 0) {
                *err = TR("A mintavétel átalakítása nem indítható.");
                return false;
            }
        }
        if (cfg_.format == Format::Mp3) {
            lame_ = lame_init();
            lame_set_in_samplerate(lame_, cfg_.sampleRate);
            lame_set_out_samplerate(lame_, cfg_.sampleRate);
            lame_set_num_channels(lame_, 2);
            lame_set_mode(lame_, cfg_.mono ? MONO : JOINT_STEREO);
            if (cfg_.abr) {
                lame_set_VBR(lame_, vbr_abr);
                lame_set_VBR_mean_bitrate_kbps(lame_, cfg_.kbps);
            } else {
                lame_set_VBR(lame_, vbr_off);
                lame_set_brate(lame_, cfg_.kbps);
            }
            lame_set_quality(lame_, 3);
            lame_set_bWriteVbrTag(lame_, 0);
            if (lame_init_params(lame_) < 0) {
                *err = TR("Az MP3 kódoló nem indítható ezekkel a beállításokkal (%1 kbit/s, %2 Hz).").arg(cfg_.kbps).arg(cfg_.sampleRate);
                return false;
            }
            return true;
        }
        return openVorbis(ch, err);
    }

    bool openVorbis(int ch, QString *err)
    {
        vorbis_info_init(&vi_);
        if (vorbis_encode_init(&vi_, ch, cfg_.sampleRate, -1, long(cfg_.kbps) * 1000, -1) != 0) {
            vorbis_info_clear(&vi_);
            *err = TR("Az Ogg Vorbis ezzel a bitrátával nem kódol (%1 kbit/s, %2 Hz, %3). Válassz nagyobb bitrátát vagy más mintavételt.")
                       .arg(cfg_.kbps)
                       .arg(cfg_.sampleRate)
                       .arg(ch == 1 ? TR("mono") : TR("sztereó"));
            return false;
        }
        vorbisOpen_ = true;
        vorbis_comment_init(&vc_);
        vorbis_comment_add_tag(&vc_, "ENCODER", "Eter");
        if (!title_.isEmpty() && cfg_.sendTitle) vorbis_comment_add_tag(&vc_, "TITLE", title_.toUtf8().constData());
        vorbis_analysis_init(&vd_, &vi_);
        vorbis_block_init(&vd_, &vb_);
        ogg_stream_init(&os_, int(QRandomGenerator::global()->generate() & 0x7fffffff));
        ogg_packet h, hc, hcode;
        vorbis_analysis_headerout(&vd_, &vc_, &h, &hc, &hcode);
        ogg_stream_packetin(&os_, &h);
        ogg_stream_packetin(&os_, &hc);
        ogg_stream_packetin(&os_, &hcode);
        ogg_page og;
        while (ogg_stream_flush(&os_, &og) != 0) sendPage(og);
        return true;
    }

    void closeVorbis(bool finish)
    {
        if (!vorbisOpen_) return;
        if (finish && sock_) {
            vorbis_analysis_wrote(&vd_, 0);
            drainVorbis(true);
        }
        ogg_stream_clear(&os_);
        vorbis_block_clear(&vb_);
        vorbis_dsp_clear(&vd_);
        vorbis_comment_clear(&vc_);
        vorbis_info_clear(&vi_);
        vorbisOpen_ = false;
    }

    void closeCodec()
    {
        if (lame_) {
            lame_close(lame_);
            lame_ = nullptr;
        }
        closeVorbis(false);
        if (swr_) swr_free(&swr_);
    }

    void sendBytes(const char *p, qint64 n)
    {
        if (!sock_ || n <= 0) return;
        sock_->write(p, n);
        std::lock_guard<std::mutex> lk(sh_->m);
        sh_->st.bytes += n;
    }

    void sendPage(const ogg_page &og)
    {
        lastPage_ = QDateTime::currentMSecsSinceEpoch();
        sendBytes(reinterpret_cast<const char *>(og.header), og.header_len);
        sendBytes(reinterpret_cast<const char *>(og.body), og.body_len);
    }

    void drainVorbis(bool flushAll)
    {
        ogg_packet op;
        ogg_page og;
        while (vorbis_analysis_blockout(&vd_, &vb_) == 1) {
            vorbis_analysis(&vb_, nullptr);
            vorbis_bitrate_addblock(&vb_);
            while (vorbis_bitrate_flushpacket(&vd_, &op)) {
                ogg_stream_packetin(&os_, &op);
                while (ogg_stream_pageout(&os_, &og) != 0) sendPage(og);
            }
        }
        // Csendben a Vorbis alig termel adatot, és a lap csak 4 kB-nál telne meg: legalább fél másodpercenként kimegy
        // egy lap, különben a szerver forráskimaradásnak venné, és bontana.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (flushAll || now - lastPage_ > 500)
            while (ogg_stream_flush(&os_, &og) != 0) {
                sendPage(og);
                lastPage_ = now;
            }
    }

    // Szint és korlátozó (a beállított szint után 0,98 fölé nem enged; gyors indulás, ~300 ms elengedés).
    void process(std::vector<float> &buf)
    {
        float gain, pl = 0, pr = 0;
        std::function<void(const float *, int)> monitor;
        {
            std::lock_guard<std::mutex> lk(sh_->m);
            gain = sh_->gain;
            if (sh_->procDirty) {
                proc_.configure(sh_->proc);
                procActive_ = sh_->proc.active();
                sh_->procDirty = false;
            }
            monitor = sh_->monitor;
        }
        const size_t frames = buf.size() / 2;
        if (gain != 1.0f)
            for (float &v : buf) v *= gain;
        if (procActive_) proc_.process(buf.data(), int(frames));
        else
            for (float &v : buf) v = std::clamp(v, -1.0f, 1.0f);
        for (size_t i = 0; i < frames; ++i) {
            pl = std::max(pl, std::fabs(buf[i * 2]));
            pr = std::max(pr, std::fabs(buf[i * 2 + 1]));
        }
        if (monitor) monitor(buf.data(), int(frames));
        const ProcMeters pm = proc_.takeMeters();
        // Ha a limiter tartósan sokat dolgozik, a bemenet túl hangos: a naplóba (10 percenként legfeljebb egyszer).
        if (procActive_ && pm.limGrDb > 6) {
            hotFrames_ += qint64(frames);
            if (hotFrames_ > qint64(kInRate) * 10) {
                hotFrames_ = 0;
                const qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (now - hotLogged_ > 10 * 60 * 1000) {
                    hotLogged_ = now;
                    const QString msg = TR("A limiter tartósan sokat dolgozik (%1 dB): túl hangos a bemenet. Csökkentsd a szintet "
                                           "a streamre, vagy válassz erősebb feldolgozást.").arg(QLocale().toString(pm.limGrDb, 'f', 1));
                    log(msg);
                    elog::warning(QStringLiteral("Stream"), who() + QStringLiteral(": ") + msg);
                }
            }
        } else if (pm.limGrDb < 3) {
            hotFrames_ = 0;
        }
        std::lock_guard<std::mutex> lk(sh_->m);
        sh_->st.peakL = std::max(sh_->st.peakL, pl);
        sh_->st.peakR = std::max(sh_->st.peakR, pr);
        ProcMeters &m = sh_->st.proc;
        m.shortTermLufs = pm.shortTermLufs;
        m.agcGainDb = pm.agcGainDb;
        m.compGrDb = std::max(m.compGrDb, pm.compGrDb);
        m.limGrDb = std::max(m.limGrDb, pm.limGrDb);
        m.truePeakDb = std::max(m.truePeakDb, pm.truePeakDb);
    }

    void encode(const std::vector<float> &in)
    {
        const float *data = in.data();
        int frames = int(in.size() / 2);
        if (swr_) {
            const int cap = swr_get_out_samples(swr_, frames);
            res_.resize(size_t(std::max(cap, 0)) * 2);
            uint8_t *out[1] = {reinterpret_cast<uint8_t *>(res_.data())};
            const uint8_t *inp[1] = {reinterpret_cast<const uint8_t *>(data)};
            const int got = swr_convert(swr_, out, cap, inp, frames);
            if (got <= 0) return;
            data = res_.data();
            frames = got;
        }
        if (lame_) {
            mp3_.resize(size_t(1.25 * frames + 7200));
            const int n = lame_encode_buffer_interleaved_ieee_float(lame_, data, frames, mp3_.data(), int(mp3_.size()));
            if (n > 0) sendBytes(reinterpret_cast<const char *>(mp3_.data()), n);
        } else if (vorbisOpen_) {
            float **vb = vorbis_analysis_buffer(&vd_, frames);
            if (cfg_.mono) {
                for (int i = 0; i < frames; ++i) vb[0][i] = 0.5f * (data[i * 2] + data[i * 2 + 1]);
            } else {
                for (int i = 0; i < frames; ++i) {
                    vb[0][i] = data[i * 2];
                    vb[1][i] = data[i * 2 + 1];
                }
            }
            vorbis_analysis_wrote(&vd_, frames);
            drainVorbis(false);
        }
    }

    void onTick()
    {
        std::vector<float> buf;
        {
            std::lock_guard<std::mutex> lk(sh_->m);
            buf.assign(sh_->queue.begin(), sh_->queue.end());
            sh_->queue.clear();
        }
        if (!buf.empty()) process(buf);           // a szintmérő akkor is mutat, ha az enkóder nem ad
        if (phase_ != Phase::Streaming || !sock_) return;
        if (!buf.empty()) encode(buf);
        // Ha a szerver nem veszi át az adatot (lassú feltöltés), 15 másodpercnyi torlódás után újrakapcsolódik.
        if (sock_ && sock_->bytesToWrite() > qint64(cfg_.kbps) * 125 * 15) {
            fail(TR("A szerver nem veszi át az adatot (túl lassú a feltöltés?)."), false);
            return;
        }
        if (titleDirty_) sendTitle();
    }

    // ------------------------------------------------------------------ cím és hallgatók

    QUrl adminUrl(const QString &path) const
    {
        QUrl u;
        u.setScheme(cfg_.tls && cfg_.server == Server::Icecast ? QStringLiteral("https") : QStringLiteral("http"));
        u.setHost(cfg_.host.trimmed());
        u.setPort(cfg_.port);
        u.setPath(path);
        return u;
    }

    void sendTitle()
    {
        titleDirty_ = false;
        if (!cfg_.sendTitle || title_.isEmpty()) return;
        if (cfg_.format == Format::Vorbis) {
            // Ogg: új logikai folyam az új címmel (a hallgatók lejátszói így látják a változást).
            closeVorbis(true);
            QString err;
            if (!openVorbis(cfg_.mono ? 1 : 2, &err)) {
                fail(err, true);
                return;
            }
            log(TR("Cím: %1").arg(title_));
            return;
        }
        QUrl u;
        QUrlQuery q;
        QNetworkRequest rq;
        if (cfg_.server == Server::Icecast) {
            u = adminUrl(QStringLiteral("/admin/metadata"));
            QString mount = cfg_.mount.trimmed();
            if (!mount.startsWith(QLatin1Char('/'))) mount.prepend(QLatin1Char('/'));
            q.addQueryItem(QStringLiteral("mount"), mount);
            q.addQueryItem(QStringLiteral("mode"), QStringLiteral("updinfo"));
            q.addQueryItem(QStringLiteral("charset"), QStringLiteral("UTF-8"));
            rq.setRawHeader("Authorization", "Basic " + (cfg_.user + QLatin1Char(':') + cfg_.password).toUtf8().toBase64());
        } else {
            u = adminUrl(QStringLiteral("/admin.cgi"));
            if (cfg_.server == Server::Shoutcast2) q.addQueryItem(QStringLiteral("sid"), QString::number(cfg_.sid));
            q.addQueryItem(QStringLiteral("pass"), cfg_.password);
            q.addQueryItem(QStringLiteral("mode"), QStringLiteral("updinfo"));
        }
        q.addQueryItem(QStringLiteral("song"), QString::fromLatin1(QUrl::toPercentEncoding(title_)));
        u.setQuery(q.query(QUrl::FullyEncoded), QUrl::StrictMode);
        rq.setUrl(u);
        // A Shoutcast admin felülete csak böngészőszerű azonosítóra válaszol.
        rq.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 (compatible; Eter/%1)").arg(QCoreApplication::applicationVersion()));
        rq.setTransferTimeout(8000);
        const QString t = title_;
        QNetworkReply *rep = nam_->get(rq);
        connect(rep, &QNetworkReply::finished, this, [this, rep, t] {
            rep->deleteLater();
            const int http = rep->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (rep->error() == QNetworkReply::NoError && (http == 0 || http < 400)) {
                log(TR("Cím: %1").arg(t));
                titleErrorLogged_ = false;
            } else if (!titleErrorLogged_) {
                titleErrorLogged_ = true;
                const QString why = http ? QStringLiteral("HTTP %1").arg(http) : rep->errorString();
                log(TR("A cím nem frissíthető a szerveren (%1). A hang ettől még megy.").arg(why));
                elog::writeKeyed(elog::Warning, QStringLiteral("Stream"), TR("%1: a cím nem frissíthető (%2)").arg(who(), why),
                                 QStringLiteral("streamtitle:") + cfg_.id, 30 * 60 * 1000);
            }
        });
    }

    void fetchStats()
    {
        if (phase_ != Phase::Streaming) return;
        QNetworkRequest rq;
        if (cfg_.server == Server::Icecast) rq.setUrl(adminUrl(QStringLiteral("/status-json.xsl")));
        else if (cfg_.server == Server::Shoutcast2) {
            QUrl u = adminUrl(QStringLiteral("/stats"));
            u.setQuery(QStringLiteral("sid=%1&json=1").arg(cfg_.sid));
            rq.setUrl(u);
        } else rq.setUrl(adminUrl(QStringLiteral("/7.html")));
        rq.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Mozilla/5.0 (compatible; Eter/%1)").arg(QCoreApplication::applicationVersion()));
        rq.setTransferTimeout(8000);
        QNetworkReply *rep = nam_->get(rq);
        connect(rep, &QNetworkReply::finished, this, [this, rep] {
            rep->deleteLater();
            if (rep->error() != QNetworkReply::NoError) return;
            const QByteArray body = rep->readAll();
            int n = -1;
            if (cfg_.server == Server::Icecast) {
                const QJsonObject st = QJsonDocument::fromJson(body).object().value("icestats").toObject();
                QJsonArray srcs;
                const QJsonValue v = st.value("source");
                if (v.isArray()) srcs = v.toArray();
                else if (v.isObject()) srcs.append(v);
                QString mount = cfg_.mount.trimmed();
                if (!mount.startsWith(QLatin1Char('/'))) mount.prepend(QLatin1Char('/'));
                for (const QJsonValue &s : srcs) {
                    const QString lu = s.toObject().value("listenurl").toString();
                    if (lu.endsWith(mount)) n = s.toObject().value("listeners").toInt(-1);
                }
            } else if (cfg_.server == Server::Shoutcast2) {
                n = QJsonDocument::fromJson(body).object().value("currentlisteners").toInt(-1);
            } else {
                QString t = QString::fromLatin1(body);
                t.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
                bool ok = false;
                const int v = t.section(QLatin1Char(','), 0, 0).trimmed().toInt(&ok);
                if (ok) n = v;
            }
            std::lock_guard<std::mutex> lk(sh_->m);
            sh_->st.listeners = n;
        });
    }

    std::shared_ptr<Shared> sh_;
    Config cfg_;
    QNetworkAccessManager *nam_ = nullptr;
    QTimer *tick_ = nullptr, *retry_ = nullptr, *handshake_ = nullptr, *stats_ = nullptr;
    QTcpSocket *sock_ = nullptr;
    Phase phase_ = Phase::Idle;
    QByteArray respBuf_;
    bool testOnly_ = false, sourceMethod_ = false, everConnected_ = false;
    int attempt_ = 0;
    qint64 retryAt_ = 0;
    QString title_;
    bool titleDirty_ = false, titleErrorLogged_ = false;
    Processor proc_;
    bool procActive_ = false;
    qint64 hotFrames_ = 0, hotLogged_ = 0;
    qint64 lastPage_ = 0;

    lame_t lame_ = nullptr;
    std::vector<unsigned char> mp3_;
    bool vorbisOpen_ = false;
    vorbis_info vi_{};
    vorbis_comment vc_{};
    vorbis_dsp_state vd_{};
    vorbis_block vb_{};
    ogg_stream_state os_{};
    SwrContext *swr_ = nullptr;
    std::vector<float> res_;
};

// ---------------------------------------------------------------------------------------------- Encoder

Encoder::Encoder(const Config &c, QObject *parent) : QObject(parent), cfg_(c)
{
    auto sh = std::make_shared<Shared>();
    sh->gain = float(std::pow(10.0, c.gainDb / 20.0));
    sh->proc = c.proc;
    sh->st.message = TR("Nincs elindítva.");
    sh_ = sh;
    thread_ = new QThread;
    thread_->setObjectName(QStringLiteral("stream"));
    w_ = new Worker(sh, c);
    w_->moveToThread(thread_);
    connect(thread_, &QThread::started, w_, &Worker::init);
    connect(thread_, &QThread::finished, w_, &QObject::deleteLater);
    connect(w_, &Worker::event, this, &Encoder::event);
    connect(w_, &Worker::stateChanged, this, [this] {
        const State s = state();
        if (s == State::Off || s == State::Error) active_ = false;
        emit stateChanged();
    });
    thread_->start();
}

Encoder::~Encoder()
{
    QMetaObject::invokeMethod(w_, &Worker::shutdown, Qt::BlockingQueuedConnection);
    thread_->quit();
    thread_->wait();
    delete thread_;
}

void Encoder::setConfig(const Config &c)
{
    cfg_ = c;
    {
        std::lock_guard<std::mutex> lk(sh_->m);
        sh_->gain = float(std::pow(10.0, c.gainDb / 20.0));
        sh_->proc = c.proc;
        sh_->procDirty = true;
    }
    QMetaObject::invokeMethod(w_, [w = w_, c] { w->setConfig(c); }, Qt::QueuedConnection);
}

void Encoder::start()
{
    active_ = true;
    started_ = cfg_;
    QMetaObject::invokeMethod(w_, [w = w_] { w->start(false); }, Qt::QueuedConnection);
}

void Encoder::test()
{
    QMetaObject::invokeMethod(w_, [w = w_] { w->start(true); }, Qt::QueuedConnection);
}

void Encoder::stop()
{
    active_ = false;
    QMetaObject::invokeMethod(w_, [w = w_] { w->stop(); }, Qt::QueuedConnection);
}

void Encoder::feed(const float *stereo, int frames)
{
    if (frames <= 0) return;
    std::lock_guard<std::mutex> lk(sh_->m);
    sh_->queue.insert(sh_->queue.end(), stereo, stereo + size_t(frames) * 2);
    if (sh_->queue.size() > kMaxQueue) sh_->queue.erase(sh_->queue.begin(), sh_->queue.begin() + std::ptrdiff_t(sh_->queue.size() - kMaxQueue));
}

void Encoder::setTitle(const QString &text)
{
    QMetaObject::invokeMethod(w_, [w = w_, text] { w->setTitle(text); }, Qt::QueuedConnection);
}

Status Encoder::status() const
{
    std::lock_guard<std::mutex> lk(sh_->m);
    Status s = sh_->st;
    sh_->st.peakL = sh_->st.peakR = 0;
    sh_->st.proc.compGrDb = sh_->st.proc.limGrDb = 0;
    sh_->st.proc.truePeakDb = -70;
    return s;
}

void Encoder::setMonitor(std::function<void(const float *, int)> sink)
{
    std::lock_guard<std::mutex> lk(sh_->m);
    sh_->monitor = std::move(sink);
}

State Encoder::state() const
{
    std::lock_guard<std::mutex> lk(sh_->m);
    return sh_->st.state;
}

QStringList Encoder::events() const
{
    std::lock_guard<std::mutex> lk(sh_->m);
    return sh_->events;
}

} // namespace stream

#include "StreamEncoder.moc"
