#include "WebSync.h"

#include "db/Database.h"
#include "i18n.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSettings>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>
#include <QLocale>
#include <QTimeZone>

#include <algorithm>
#include <cstdlib>

namespace {
constexpr int kIntervalMs = 5000;        // rendes szinkron
constexpr int kTimeoutMs = 15000;

QList<int> intList(const QVariant &v)
{
    QList<int> out;
    for (const QString &s : v.toString().split(QLatin1Char(','), Qt::SkipEmptyParts))
        if (const int i = s.trimmed().toInt(); i > 0) out << i;
    return out;
}
QString joinInts(const QList<int> &l)
{
    QStringList s;
    for (int i : l) s << QString::number(i);
    return s.join(QLatin1Char(','));
}
bool isMusic(const QString &kind) { return kind.isEmpty() || kind == QLatin1String("music"); }
}

WebSync::Config WebSync::loadConfig()
{
    QSettings s;
    Config c;
    c.enabled = s.value("web/enabled", false).toBool();
    c.url = s.value("web/url").toString();
    c.key = s.value("web/key").toString();
    c.nextCount = std::clamp(s.value("web/nextCount", 10).toInt(), 1, 20);
    c.musicOnly = s.value("web/musicOnly", true).toBool();
    c.requests = s.value("web/requests", false).toBool();
    c.approve = s.value("web/approve", false).toBool();
    c.allMusic = s.value("web/allMusic", true).toBool();
    c.categories = intList(s.value("web/categories"));
    c.always = s.value("web/always", true).toBool();
    c.clocks = intList(s.value("web/clocks"));
    return c;
}

void WebSync::saveConfig(const Config &c)
{
    QSettings s;
    s.setValue("web/enabled", c.enabled);
    s.setValue("web/url", c.url);
    s.setValue("web/key", c.key);
    s.setValue("web/nextCount", c.nextCount);
    s.setValue("web/musicOnly", c.musicOnly);
    s.setValue("web/requests", c.requests);
    s.setValue("web/approve", c.approve);
    s.setValue("web/allMusic", c.allMusic);
    s.setValue("web/categories", joinInts(c.categories));
    s.setValue("web/always", c.always);
    s.setValue("web/clocks", joinInts(c.clocks));
}

WebSync::WebSync(Database *db, QObject *parent)
    : QObject(parent), db_(db), net_(new QNetworkAccessManager(this)), timer_(new QTimer(this)), soon_(new QTimer(this))
{
    timer_->setInterval(kIntervalMs);
    connect(timer_, &QTimer::timeout, this, &WebSync::tick);
    soon_->setSingleShot(true);
    soon_->setInterval(400);
    connect(soon_, &QTimer::timeout, this, &WebSync::tick);
    connect(db_, &Database::itemsChanged, this, [this] { catalogDirty_ = true; });
    status_ = TR("kikapcsolva");
}

void WebSync::configure(const Config &c)
{
    const bool catalogChange = c.allMusic != cfg_.allMusic || c.categories != cfg_.categories || c.url != cfg_.url;
    cfg_ = c;
    if (catalogChange) catalogDirty_ = true;
    topSentHash_.clear();          // az (esetleg új) oldal is megkapja
    redirectBase_.clear();
    redirects_ = 0;
    clockOffset_ = 0;
    topSentAt_ = 0;
    failures_ = 0;
    if (!cfg_.enabled) {
        timer_->stop();
        soon_->stop();
        setStatus(false, TR("kikapcsolva"));
        return;
    }
    if (cfg_.url.trimmed().isEmpty() || cfg_.key.trimmed().isEmpty()) {
        timer_->stop();
        setStatus(false, TR("add meg a weboldal címét és a kulcsot"));
        return;
    }
    timer_->start(kIntervalMs);
    setStatus(false, TR("kapcsolódás…"));
    syncNow();
}

void WebSync::setTracks(const Track &now, bool hasNow, const QVector<Track> &next)
{
    auto same = [](const Track &a, const Track &b) {
        return a.title == b.title && a.artist == b.artist && a.startMs == b.startMs && a.request == b.request;
    };
    bool changed = hasNow != hasNow_ || (hasNow && !same(now, now_)) || next.size() != next_.size();
    for (int i = 0; !changed && i < next.size(); ++i)
        changed = !same(next[i], next_[i]) && std::llabs(next[i].startMs - next_[i].startMs) > 5000;
    // Számváltáskor a legtöbbet játszott lista is frissül (legfeljebb negyedpercenként).
    if (hasNow && (!hasNow_ || now.title != now_.title || now.startMs != now_.startMs)
        && QDateTime::currentSecsSinceEpoch() - topBuiltAt_ >= 15)
        topBuiltAt_ = 0;
    now_ = now;
    hasNow_ = hasNow;
    next_ = next;
    if (changed && cfg_.enabled && timer_->isActive()) soon_->start();
}

void WebSync::ack(int remoteId, const QString &status)
{
    if (remoteId <= 0) return;
    acks_[remoteId] = status;
    if (cfg_.enabled && timer_->isActive()) soon_->start();
}

bool WebSync::requestsOpen() const
{
    if (!cfg_.enabled || !cfg_.requests) return false;
    if (cfg_.always) return true;
    const QDateTime t = QDateTime::currentDateTime();
    const int clock = db_->clockAt(t.date(), t.time().hour());
    return clock && cfg_.clocks.contains(clock);
}

bool WebSync::requestable(int itemId) const { return catalogIds_.contains(itemId); }

void WebSync::syncNow()
{
    if (cfg_.enabled && timer_->isActive()) soon_->start(0);
}

void WebSync::setStatus(bool ok, const QString &text)
{
    ok_ = ok;
    status_ = text;
    emit statusChanged();
}

void WebSync::buildCatalog()
{
    catalogDirty_ = false;
    QSet<int> cats;
    if (!cfg_.allMusic)
        for (int c : cfg_.categories) cats.insert(c);
    QJsonArray items;
    catalogIds_.clear();
    for (const Item &it : db_->items()) {
        if (cfg_.allMusic ? !isMusic(it.kind) : !cats.contains(it.categoryId)) continue;
        if (it.title.isEmpty() && it.artist.isEmpty()) continue;
        items.append(QJsonArray{it.id, it.title, it.artist});
        catalogIds_.insert(it.id);
    }
    catalog_ = QJsonDocument(items).toJson(QJsonDocument::Compact);
    catalogRev_ = QString::fromLatin1(QCryptographicHash::hash(catalog_, QCryptographicHash::Sha1).toHex().left(16));
}

static QJsonObject trackJson(const WebSync::Track &t)
{
    QJsonObject o{{"title", t.title}, {"artist", t.artist}, {"start", double(t.startMs)}, {"duration", double(t.durationMs)}};
    if (t.request) o["request"] = true;
    return o;
}

void WebSync::buildTop()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    topBuiltAt_ = now;
    QJsonObject top;
    const std::pair<const char *, qint64> windows[] = {{"day", 86400}, {"week", 7 * 86400}, {"month", 30 * 86400}};
    for (const auto &[name, secs] : windows) {
        QJsonArray list;
        for (const Database::TopRow &r : db_->topPlayed(now - secs, 20))
            list.append(QJsonObject{{"title", r.title}, {"artist", r.artist}, {"plays", r.plays}});
        top.insert(QLatin1String(name), list);
    }
    top_ = top;
    topHash_ = QCryptographicHash::hash(QJsonDocument(top_).toJson(QJsonDocument::Compact), QCryptographicHash::Sha1);
}

void WebSync::tick()
{
    if (!cfg_.enabled || busy_) return;
    if (catalogDirty_) buildCatalog();
    const qint64 nowS = QDateTime::currentSecsSinceEpoch();
    if (nowS - topBuiltAt_ >= 300) buildTop();
    if (sendCatalog_) {
        const QByteArray body = "{\"rev\":\"" + catalogRev_.toLatin1() + "\",\"items\":" + catalog_ + "}";
        post(QStringLiteral("catalog"), body);
        return;
    }
    QJsonArray next;
    for (const Track &t : next_) next.append(trackJson(t));
    QJsonArray acks;
    sentAcks_ = acks_;
    for (auto it = acks_.cbegin(); it != acks_.cend(); ++it) acks.append(QJsonObject{{"id", it.key()}, {"status", it.value()}});
    QJsonObject o{
        {"time", double(QDateTime::currentMSecsSinceEpoch())},
        {"version", QStringLiteral(ETER_VERSION)},
        {"now", hasNow_ ? QJsonValue(trackJson(now_)) : QJsonValue()},
        {"next", next},
        {"requests", QJsonObject{{"enabled", cfg_.requests}, {"open", requestsOpen()}}},
        {"catalog_rev", cfg_.requests ? catalogRev_ : QString()},
        {"acks", acks},
    };
    if (topHash_ != topSentHash_ || nowS - topSentAt_ >= 600) o.insert("top", top_);
    post(QStringLiteral("sync"), QJsonDocument(o).toJson(QJsonDocument::Compact));
    if (busy_ && o.contains("top")) {
        busy_->setProperty("withTop", true);
        busy_->setProperty("topHash", topHash_);
    }
}

void WebSync::post(const QString &path, const QByteArray &body)
{
    QString base = redirectBase_.isEmpty() ? cfg_.url.trimmed() : redirectBase_;
    if (!base.contains(QStringLiteral("://"))) base.prepend(QStringLiteral("https://"));
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    // Az aláírás fejlécben és a címben is megy: van olyan tárhely (proxy, tűzfal, gyorsítótár), amely a saját
    // fejléceket elnyeli. Az időt a weboldal órájához igazítja (a válaszok Date fejléce alapján), így a két gép órájának
    // eltérése sem okoz hibát.
    const QByteArray time = QByteArray::number(QDateTime::currentSecsSinceEpoch() + clockOffset_);
    const QByteArray sig = QMessageAuthenticationCode::hash(time + '\n' + body, cfg_.key.trimmed().toUtf8(),
                                                            QCryptographicHash::Sha256).toHex();
    const QUrl url(base + QStringLiteral("/?rest_route=/eter/v1/") + path + QStringLiteral("&eter_time=")
                   + QString::fromLatin1(time) + QStringLiteral("&eter_sig=") + QString::fromLatin1(sig));
    if (url.scheme() == QLatin1String("https") && !QSslSocket::supportsSsl()) {
        setStatus(false, TR("HTTPS nem érhető el ezen a gépen (hiányzik az OpenSSL)"));
        return;
    }
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Eter/" ETER_VERSION));
    req.setTransferTimeout(kTimeoutMs);
    // Átirányítást (http → https, www nélkül → www-vel) maga kezel: az automatikus követésnél a POST kérésből GET
    // lehetne, és elveszne a törzs.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setRawHeader("X-Eter-Time", time);
    req.setRawHeader("X-Eter-Signature", sig);
    busy_ = net_->post(req, body);
    connect(busy_, &QNetworkReply::finished, this, [this, reply = busy_, path] { finished(reply, path); });
}

void WebSync::finished(QNetworkReply *reply, const QString &path)
{
    reply->deleteLater();
    busy_ = nullptr;
    const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    const QJsonObject o = QJsonDocument::fromJson(data).object();
    // A weboldal órája (Date fejléc): ha a két gép órája eltér, az aláírás ideje ehhez igazodik.
    {
        // HTTP dátum: „Tue, 24 Nov 2026 14:40:40 GMT” (a Qt RFC 2822 olvasója a „GMT”-t nem mindig fogadja el)
        const QString raw = QString::fromLatin1(reply->rawHeader("Date")).trimmed();
        QDateTime server = QLocale::c().toDateTime(raw, QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'"));
        if (server.isValid()) server.setTimeZone(QTimeZone::utc());
        else server = QDateTime::fromString(raw, Qt::RFC2822Date);
        if (server.isValid()) {
            const qint64 off = server.toSecsSinceEpoch() - QDateTime::currentSecsSinceEpoch();
            clockOffset_ = std::llabs(off) > 2 ? off : 0;
        }
    }
    // Átirányítás: ugyanaz a kérés az új címre (a cím többi része marad), és a felhasználó is látja.
    if (http >= 301 && http <= 308 && http != 304) {
        QUrl to = reply->url().resolved(QUrl(QString::fromLatin1(reply->rawHeader("Location"))));
        to.setQuery(QString());
        QString nb = to.toString();
        while (nb.endsWith(QLatin1Char('/'))) nb.chop(1);
        if (!nb.isEmpty() && nb != redirectBase_ && redirects_ < 3) {
            ++redirects_;
            redirectBase_ = nb;
            setStatus(false, TR("a weboldal átirányít ide: %1 (érdemes ezt beírni címként)").arg(nb));
            soon_->start(0);
            return;
        }
    }
    if (reply->error() != QNetworkReply::NoError || !o.value("ok").toBool()) {
        ++failures_;
        QString why;
        const QString server = o.value("message").toString();
        if (http == 401) {
            // A weboldal pontosan megmondja, mi a baj (kulcs, óra, elnyelt fejléc); ha nem, az általános szöveg.
            why = server.isEmpty() ? TR("a kulcs nem egyezik a weboldalon beállítottal (vagy a gép órája pontatlan)")
                                   : TR("a weboldal elutasította: %1").arg(server);
            // Nem az Éter bővítménye utasította el, hanem egy biztonsági beállítás (a REST API csak bejelentkezve).
            const QString code = o.value("code").toString();
            if (!server.isEmpty() && !code.startsWith(QLatin1String("eter_")))
                why += TR(" – a weboldal egy biztonsági beállítása csak bejelentkezve engedi a REST API-t; az Éter Radio "
                          "bővítmény 1.2.2-es változata kivételt kér magának, frissítsd a bővítményt");
        } else if (http == 404 || (o.isEmpty() && http >= 200 && http < 300))
            why = TR("nem található az Éter Radio bővítmény ezen a címen (be van kapcsolva a WordPressben?)");
        else if (!server.isEmpty()) why = server;
        else why = reply->errorString();
        if (http > 0) why += TR(" (HTTP %1)").arg(http);
        setStatus(false, TR("hiba: %1").arg(why));
        // Hiba után ritkábban próbálkozik (legfeljebb negyedpercenként).
        timer_->setInterval(std::min(60000, kIntervalMs * (1 << std::min(failures_, 4))));
        return;
    }
    failures_ = 0;
    redirects_ = 0;
    timer_->setInterval(kIntervalMs);
    lastOk_ = QDateTime::currentDateTime();
    if (path == QLatin1String("catalog")) {
        sendCatalog_ = false;
        setStatus(true, TR("kapcsolódva · %1 kérhető szám elküldve").arg(o.value("count").toInt()));
        soon_->start();
        return;
    }
    if (path == QLatin1String("sync") && reply->property("withTop").toBool()) {
        topSentHash_ = reply->property("topHash").toByteArray();
        topSentAt_ = QDateTime::currentSecsSinceEpoch();
    }
    // A nyugtázott állapotok törlése (ami közben változott, az a következővel megy).
    for (auto it = sentAcks_.cbegin(); it != sentAcks_.cend(); ++it)
        if (acks_.value(it.key()) == it.value()) acks_.remove(it.key());
    sentAcks_.clear();
    if (cfg_.requests && o.value("need_catalog").toBool()) {
        sendCatalog_ = true;
        soon_->start();
    }
    QVector<Incoming> in;
    for (const QJsonValue &v : o.value("requests").toArray()) {
        const QJsonObject r = v.toObject();
        Incoming i;
        i.remoteId = r.value("id").toInt();
        i.itemId = r.value("item").toInt();
        i.time = r.value("time").toVariant().toLongLong();
        if (i.remoteId <= 0) continue;
        // Ami fél óránál régebben várakozik (például mert a program közben nem futott), az már nem kerül adásba.
        if (i.time > 0 && QDateTime::currentSecsSinceEpoch() - i.time > 1800) {
            acks_[i.remoteId] = QStringLiteral("removed");
            continue;
        }
        in << i;
    }
    QString text = TR("kapcsolódva · utolsó frissítés %1").arg(lastOk_.toString("HH:mm:ss"));
    if (cfg_.requests) text += requestsOpen() ? TR(" · most lehet kérni") : TR(" · most nem lehet kérni");
    if (!redirectBase_.isEmpty()) text += TR(" · a weboldal átirányít ide: %1 (érdemes ezt beírni címként)").arg(redirectBase_);
    if (std::llabs(clockOffset_) >= 60)
        text += TR(" · a gép órája %1 mp-cel eltér a weboldalétól (a program kiegyenlíti)").arg(-clockOffset_);
    setStatus(true, text);
    if (!in.isEmpty()) emit requestsArrived(in);
}
