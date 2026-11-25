#include "ArtistInfo.h"

#include "core/ArtistSplit.h"
#include "core/Vocal.h"
#include "db/Database.h"
#include "i18n.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QSqlQuery>
#include <QUrl>
#include <QUrlQuery>

static ArtistInfoService *gInstance = nullptr;

// Percenként legfeljebb 30 kérés (az ingyenes kulcs korlátja); egy kis tartalékkal.
static constexpr int kIntervalMs = 2100;

// A szolgáltatások címei (fejlesztéshez helyi utánzat is megadható: ETER_MB_URL, ETER_WD_URL, ETER_WP_URL).
static QString mbUrl() { return qEnvironmentVariable("ETER_MB_URL", QStringLiteral("https://musicbrainz.org/ws/2/")); }
static QString wdUrl() { return qEnvironmentVariable("ETER_WD_URL", QStringLiteral("https://www.wikidata.org/w/api.php")); }
static QString wpUrl(const QString &lang)
{
    return qEnvironmentVariable("ETER_WP_URL", QStringLiteral("https://%1.wikipedia.org/api/rest_v1/page/summary/")).arg(lang);
}
static constexpr int kMbIntervalMs = 1100;     // MusicBrainz: legfeljebb egy kérés másodpercenként

static QString userAgent()
{
    // A MusicBrainz és a Wikimedia is kéri, hogy a program azonosítsa magát egy elérhetőséggel (a projekt címe).
    return QStringLiteral("Eter/%1 ( https://github.com/laczfy001/eter )").arg(QCoreApplication::applicationVersion());
}

static QString baseUrl()
{
    // Fejlesztéshez helyi utánzat is megadható (ETER_ADB_URL).
    const QString env = qEnvironmentVariable("ETER_ADB_URL");
    return env.isEmpty() ? QStringLiteral("https://www.theaudiodb.com/api/v1/json/") : env;
}

// Összehasonlításhoz: kisbetű, ékezet és írásjelek nélkül, a „the ” előtag nélkül.
static QString fold(const QString &s)
{
    QString n = s.normalized(QString::NormalizationForm_KD).toLower();
    QString out;
    for (const QChar c : std::as_const(n))
        if (c.isLetterOrNumber()) out += c;
    if (out.startsWith(QLatin1String("the")) && n.trimmed().startsWith(QLatin1String("the "))) out = out.mid(3);
    return out;
}

QString ArtistData::facts() const
{
    QStringList p;
    if (!country.isEmpty()) p << country;
    if (formed > 0) p << QString::number(formed);
    else if (born > 0) p << TR("szül. %1").arg(born);
    for (const QString &s : {genre, style, mood})
        if (!s.isEmpty() && !p.contains(s)) p << s;
    return p.join(QStringLiteral(" · "));
}

ArtistInfoService::ArtistInfoService(Database *db, QObject *parent) : QObject(parent), db_(db)
{
    gInstance = this;
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &ArtistInfoService::next);
    // Automatikus módban, ha minden megvan, tízpercenként (és új elemeknél) újra körbenéz.
    idle_.setInterval(10 * 60 * 1000);
    connect(&idle_, &QTimer::timeout, this, [this] {
        autoExhausted_ = false;
        mbExhausted_ = false;
        if (settings_.enabled && !busy_ && !timer_.isActive()) next();
        if (settings_.mbEnabled && !mbBusy_ && !mbTimer_.isActive()) mbNext();
    });
    connect(db_, &Database::itemsChanged, this, [this] {
        if (settings_.mode != Auto) return;
        if (settings_.enabled && autoExhausted_) {
            autoExhausted_ = false;
            if (!busy_ && !timer_.isActive()) timer_.start(kIntervalMs);
        }
        if (settings_.mbEnabled && mbExhausted_) {
            mbExhausted_ = false;
            if (!mbBusy_ && !mbTimer_.isActive()) mbTimer_.start(1100);
        }
    });
    mbTimer_.setSingleShot(true);
    connect(&mbTimer_, &QTimer::timeout, this, &ArtistInfoService::mbNext);
    reload();
}

ArtistInfoService::~ArtistInfoService()
{
    if (gInstance == this) gInstance = nullptr;
    if (busy_) busy_->abort();
    if (mbBusy_) mbBusy_->abort();
}

ArtistInfoService *ArtistInfoService::instance() { return gInstance; }

void ArtistInfoService::reload()
{
    Settings s;
    s.enabled = db_->setting(QStringLiteral("adb_enabled"), QStringLiteral("0")) == QLatin1String("1");
    s.key = db_->setting(QStringLiteral("adb_key"), QStringLiteral("123")).trimmed();
    if (s.key.isEmpty()) s.key = QStringLiteral("123");
    s.mode = std::clamp(db_->setting(QStringLiteral("adb_mode"), QStringLiteral("0")).toInt(), 0, 2);
    s.mbEnabled = db_->setting(QStringLiteral("mb_enabled"), QStringLiteral("0")) == QLatin1String("1");
    s.wikiEnabled = db_->setting(QStringLiteral("wiki_enabled"), QStringLiteral("1")) == QLatin1String("1");
    settings_ = s;
    // 0.35: az előadói adatlap több forrásból áll össze. A korábbi „nincs ilyen” bejegyzések (az ingyenes TheAudioDB-
    // kulcs mintaválaszai miatt) nem akadályozhatják az új forrásokat.
    if (db_->setting(QStringLiteral("online_meta_ver")) != QLatin1String("2")) {
        QSqlQuery q;
        q.exec(QStringLiteral("DELETE FROM online_meta WHERE found = 0"));
        db_->setSetting(QStringLiteral("online_meta_ver"), QStringLiteral("2"));
    }
    if (!s.enabled) {
        queue_.clear();
        queued_.clear();
        timer_.stop();
        if (busy_) busy_->abort();
    } else {
        autoExhausted_ = false;
        if (!busy_ && !timer_.isActive()) timer_.start(200);
    }
    if (!s.mbEnabled) {
        mbQueue_.clear();
        mbQueued_.clear();
        mbTimer_.stop();
        if (mbBusy_) mbBusy_->abort();
    } else {
        mbExhausted_ = false;
        if (!mbBusy_ && !mbTimer_.isActive()) mbTimer_.start(300);
    }
    if (anyEnabled() && s.mode == Auto) idle_.start();
    else idle_.stop();
    emit statusChanged();
}

void ArtistInfoService::setSettings(const Settings &s)
{
    db_->setSetting(QStringLiteral("adb_enabled"), s.enabled ? QStringLiteral("1") : QStringLiteral("0"));
    db_->setSetting(QStringLiteral("adb_key"), s.key.trimmed().isEmpty() ? QStringLiteral("123") : s.key.trimmed());
    db_->setSetting(QStringLiteral("adb_mode"), QString::number(s.mode));
    const bool sourcesChanged = s.mbEnabled != settings_.mbEnabled || s.wikiEnabled != settings_.wikiEnabled || s.enabled != settings_.enabled
                                || s.key.trimmed() != settings_.key;
    db_->setSetting(QStringLiteral("mb_enabled"), s.mbEnabled ? QStringLiteral("1") : QStringLiteral("0"));
    db_->setSetting(QStringLiteral("wiki_enabled"), s.wikiEnabled ? QStringLiteral("1") : QStringLiteral("0"));
    if (sourcesChanged) {
        // Más forrással újra érdemes keresni, ami eddig nem volt meg.
        QSqlQuery q;
        q.exec(QStringLiteral("DELETE FROM online_meta WHERE found = 0"));
    }
    reload();
}

QString ArtistInfoService::primaryName(const QString &artistField) const
{
    const QString seps = db_->setting(QStringLiteral("artist_separators"), artists::defaultSeparators());
    const QString whole = artistField.trimmed();
    if (whole.isEmpty()) return {};
    // A szétválasztás kisbetűsít; az eredeti írásmódot az első rész hosszáig visszük tovább.
    const QStringList parts = artists::split(whole, seps);
    if (parts.size() <= 1) return whole;
    const int at = whole.toLower().indexOf(parts.first());
    return at >= 0 ? whole.mid(at, parts.first().size()).trimmed() : parts.first();
}

QString ArtistInfoService::keyFor(const QString &name) { return QStringLiteral("a:") + name.trimmed().toLower(); }

ArtistData ArtistInfoService::artist(const QString &artistField)
{
    ArtistData d;
    const QString name = primaryName(artistField);
    if (name.isEmpty()) return d;
    const Database::OnlineMeta m = db_->onlineMeta(keyFor(name));
    d.known = m.known;
    d.found = m.found;
    d.fetchedAt = m.fetchedAt;
    if (!m.found) return d;
    const QJsonObject o = QJsonDocument::fromJson(m.data.toUtf8()).object();
    d.name = o.value(QStringLiteral("name")).toString();
    d.id = o.value(QStringLiteral("id")).toString();
    d.mbid = o.value(QStringLiteral("mbid")).toString();
    d.country = o.value(QStringLiteral("country")).toString();
    d.genre = o.value(QStringLiteral("genre")).toString();
    d.style = o.value(QStringLiteral("style")).toString();
    d.mood = o.value(QStringLiteral("mood")).toString();
    d.gender = o.value(QStringLiteral("gender")).toString();
    d.label = o.value(QStringLiteral("label")).toString();
    d.website = o.value(QStringLiteral("website")).toString();
    d.disbanded = o.value(QStringLiteral("disbanded")).toString();
    d.formed = o.value(QStringLiteral("formed")).toInt();
    d.born = o.value(QStringLiteral("born")).toInt();
    d.died = o.value(QStringLiteral("died")).toInt();
    d.members = o.value(QStringLiteral("members")).toInt();
    d.thumbUrl = o.value(QStringLiteral("thumb")).toString();
    d.logoUrl = o.value(QStringLiteral("logo")).toString();
    d.type = o.value(QStringLiteral("type")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("sources")).toArray()) d.sources << v.toString();
    const QString lang = i18n::language();
    d.bio = o.value(QStringLiteral("bio_") + lang).toString();
    d.wikiUrl = o.value(QStringLiteral("wiki_") + lang).toString();
    if (d.bio.trimmed().isEmpty()) {
        d.bio = o.value(QStringLiteral("bio_en")).toString();
        d.wikiUrl = o.value(QStringLiteral("wiki_en")).toString();
    }
    const QString img = QDir(imageDir()).filePath(d.id + QStringLiteral(".jpg"));
    if (!d.id.isEmpty() && QFile::exists(img)) d.imageFile = img;
    return d;
}

void ArtistInfoService::clearCache()
{
    queue_.clear();
    queued_.clear();
    mbQueue_.clear();
    mbQueued_.clear();
    db_->clearOnlineMeta();
    QDir(imageDir()).removeRecursively();
    reload();
}

QString ArtistInfoService::imageDir() const
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty()) dir = QDir::homePath() + QStringLiteral("/.eter");
    return dir + QStringLiteral("/artist-images");
}

QString ArtistInfoService::jobKey(const Job &j) const
{
    switch (j.kind) {
    case Job::Artist: return keyFor(j.name);
    case Job::Track: return QStringLiteral("t:%1").arg(j.itemId);
    case Job::Album: return QStringLiteral("al:%1:%2").arg(j.albumId).arg(j.itemId);
    case Job::ArtistMb: return QStringLiteral("am:") + j.albumId;
    }
    return {};
}

void ArtistInfoService::enqueue(const Job &j, bool front)
{
    const QString k = jobKey(j);
    if (queued_.contains(k)) {
        if (front)
            for (int i = 0; i < queue_.size(); ++i)
                if (jobKey(queue_[i]) == k) {
                    queue_.move(i, 0);
                    break;
                }
        return;
    }
    queued_.insert(k);
    if (front) queue_.prepend(j);
    else queue_.append(j);
    if (!busy_ && !timer_.isActive()) timer_.start(50);
    emit statusChanged();
}

void ArtistInfoService::request(const QString &artistField, bool front, bool force)
{
    if (!settings_.enabled) return;
    const QString name = primaryName(artistField);
    if (name.isEmpty()) return;
    if (!force && db_->onlineMeta(QStringLiteral("adb:") + keyFor(name).mid(2)).known) {
        // Megvan; ha a fotó még hiányzik, azt pótolja.
        const ArtistData d = artist(artistField);
        if (d.found && d.imageFile.isEmpty() && !d.thumbUrl.isEmpty()) fetchImage(keyFor(name), d.id, d.thumbUrl);
        return;
    }
    Job j;
    j.kind = Job::Artist;
    j.name = name;
    j.force = force;
    enqueue(j, front);
}

void ArtistInfoService::requestItems(const QVector<int> &itemIds, bool force)
{
    if (settings_.mbEnabled) {
        const QSet<int> want(itemIds.begin(), itemIds.end());
        for (const Item &it : db_->items())
            if (want.contains(it.id)) mbEnqueue(it.id, it.artist, it.title, it.durationMs, false, force);
    }
    if (!settings_.enabled) return;
    const QSet<int> want(itemIds.begin(), itemIds.end());
    const QHash<int, QPair<QString, int>> ay = settings_.premium() ? db_->albumYears() : QHash<int, QPair<QString, int>>();
    for (const Item &it : db_->items()) {
        if (!want.contains(it.id)) continue;
        request(it.artist, false, force);
        // Saját kulccsal a szám albuma és éve is, ha valamelyik még üres.
        if (settings_.premium() && !it.artist.trimmed().isEmpty()) {
            const auto v = ay.value(it.id);
            if ((v.first.isEmpty() || v.second == 0) && (force || !db_->onlineMeta(QStringLiteral("t:%1").arg(it.id)).known)) {
                Job j;
                j.kind = Job::Track;
                j.name = primaryName(it.artist);
                j.title = it.title;
                j.itemId = it.id;
                j.force = force;
                enqueue(j, false);
            }
        }
    }
}

void ArtistInfoService::requestForPlayer(int itemId)
{
    if (!anyEnabled() || settings_.mode == Manual) return;
    const Item it = db_->item(itemId);
    if (!it.id) return;
    if (settings_.mbEnabled) mbEnqueue(it.id, it.artist, it.title, it.durationMs, true, false);
    if (settings_.enabled) request(it.artist, true, false);
}

void ArtistInfoService::requestForInfo(int itemId, bool force)
{
    if (!anyEnabled()) return;
    const Item it = db_->item(itemId);
    if (!it.id) return;
    if (settings_.mbEnabled) mbEnqueue(it.id, it.artist, it.title, it.durationMs, true, force);
    if (settings_.enabled) request(it.artist, true, force);
}

bool ArtistInfoService::pendingFor(const QString &artistField, int itemId) const
{
    const QString name = primaryName(artistField);
    for (const Job &j : queue_)
        if ((!name.isEmpty() && j.name == name) || (itemId && j.itemId == itemId)) return true;
    for (const MbJob &j : mbQueue_)
        if ((!name.isEmpty() && j.artist == name) || (itemId && j.itemId == itemId)) return true;
    auto same = [&name](const QString &cur) { return !name.isEmpty() && (cur == name || cur.startsWith(name + QStringLiteral(" – "))); };
    if ((busy_ && same(current_)) || (mbBusy_ && same(mbCurrent_))) return true;
    return false;
}

// Automatikus mód: a hangtár még nem kérdezett előadói (a „nincs ilyen” 30 nap, a megtalált 180 nap után újra),
// saját kulccsal a számok is, ahol az album vagy az év üres.
void ArtistInfoService::refillAuto()
{
    if (autoExhausted_) return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QHash<QString, qint64> found, missing;      // a TheAudioDB saját nyilvántartása ("adb:" → "a:" kulcs)
    for (auto [src, dst] : {std::pair{true, &found}, std::pair{false, &missing}}) {
        const QHash<QString, qint64> t = db_->onlineMetaTimes(QStringLiteral("adb:"), src);
        for (auto it = t.begin(); it != t.end(); ++it) dst->insert(QStringLiteral("a:") + it.key().mid(4), it.value());
    }
    QSet<QString> seen;
    int added = 0;
    const QVector<Item> items = db_->items();
    for (const Item &it : items) {
        if (it.kind != QLatin1String("music")) continue;
        const QString name = primaryName(it.artist);
        if (name.isEmpty()) continue;
        const QString k = keyFor(name);
        if (seen.contains(k)) continue;
        seen.insert(k);
        const bool due = found.contains(k) ? now - found.value(k) > 180 * 86400
                         : missing.contains(k) ? now - missing.value(k) > 30 * 86400
                                               : true;
        if (!due) continue;
        Job j;
        j.kind = Job::Artist;
        j.name = name;
        enqueue(j, false);
        if (++added >= 40) return;
    }
    if (settings_.premium()) {
        const QHash<QString, qint64> tf = db_->onlineMetaTimes(QStringLiteral("t:"), true);
        const QHash<QString, qint64> tm = db_->onlineMetaTimes(QStringLiteral("t:"), false);
        const QHash<int, QPair<QString, int>> ay = db_->albumYears();
        for (const Item &it : items) {
            if (it.kind != QLatin1String("music") || it.artist.trimmed().isEmpty()) continue;
            const auto v = ay.value(it.id);
            if (!v.first.isEmpty() && v.second > 0) continue;
            if (missing.contains(keyFor(primaryName(it.artist)))) continue;   // az előadót sem ismeri
            const QString k = QStringLiteral("t:%1").arg(it.id);
            if (tf.contains(k) || (tm.contains(k) && now - tm.value(k) < 30 * 86400)) continue;
            Job j;
            j.kind = Job::Track;
            j.name = primaryName(it.artist);
            j.title = it.title;
            j.itemId = it.id;
            enqueue(j, false);
            if (++added >= 40) return;
        }
    }
    if (!added) autoExhausted_ = true;
}

void ArtistInfoService::next()
{
    if (!settings_.enabled || busy_) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (pauseUntil_ > now) {
        timer_.start(int(std::min<qint64>(pauseUntil_ - now, 60000)));
        return;
    }
    if (queue_.isEmpty() && settings_.mode == Auto) refillAuto();
    if (queue_.isEmpty()) {
        current_.clear();
        emit statusChanged();
        return;
    }
    const Job job = queue_.takeFirst();
    queued_.remove(jobKey(job));
    if (job.kind == Job::Track) {
        // Ha az előadót sem ismeri a TheAudioDB, a számát sem keressük.
        const Database::OnlineMeta a = db_->onlineMeta(QStringLiteral("adb:") + keyFor(job.name).mid(2));
        if (a.known && !a.found) {
            db_->setOnlineMeta(QStringLiteral("t:%1").arg(job.itemId), false, {});
            timer_.start(0);
            return;
        }
    }
    // A lekérdezés kézzel kódolva: az „&” és a „+” az előadónevekben is előfordul (Simon & Garfunkel).
    auto enc = [](const QString &v) { return QUrl::toPercentEncoding(v); };
    QByteArray path = baseUrl().toUtf8() + enc(settings_.key) + '/';
    switch (job.kind) {
    case Job::Artist:
        path += "search.php?s=" + enc(job.name);
        current_ = job.name;
        break;
    case Job::Track:
        path += "searchtrack.php?s=" + enc(job.name) + "&t=" + enc(job.title);
        current_ = QStringLiteral("%1 – %2").arg(job.name, job.title);
        break;
    case Job::Album:
        path += "album.php?m=" + enc(job.albumId);
        current_ = job.name;
        break;
    case Job::ArtistMb:
        path += "artist-mb.php?i=" + enc(job.albumId);
        current_ = job.name;
        break;
    }
    const QUrl url = QUrl::fromEncoded(path);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setTransferTimeout(20000);
    QNetworkReply *reply = nam_.get(req);
    busy_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] { finished(reply, job); });
    emit statusChanged();
}

void ArtistInfoService::finished(QNetworkReply *reply, const Job &job)
{
    reply->deleteLater();
    busy_ = nullptr;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::OperationCanceledError && !settings_.enabled) return;
    if (status == 429) {
        // Túl sok kérés: egy perc szünet, és újra.
        lastError_ = TR("túl sok kérés, egy perc szünet");
        pauseUntil_ = QDateTime::currentMSecsSinceEpoch() + 61000;
        enqueue(job, true);
        timer_.start(61000);
        emit statusChanged();
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        lastError_ = status == 401 || status == 403 ? TR("a kulcs nem érvényes") : reply->errorString();
        pauseUntil_ = QDateTime::currentMSecsSinceEpoch() + 60000;
        enqueue(job, true);
        timer_.start(60000);
        emit statusChanged();
        return;
    }
    lastError_.clear();
    const QByteArray body = reply->readAll();
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    ++done_;
    if (job.kind == Job::Artist) {
        QJsonObject best;
        for (const QJsonValue &v : root.value(QStringLiteral("artists")).toArray()) {
            const QJsonObject a = v.toObject();
            if (fold(a.value(QStringLiteral("strArtist")).toString()) == fold(job.name)
                || fold(a.value(QStringLiteral("strArtistAlternate")).toString()) == fold(job.name)) {
                best = a;
                break;
            }
        }
        db_->setOnlineMeta(QStringLiteral("adb:") + keyFor(job.name).mid(2), !best.isEmpty(), {});
        if (best.isEmpty()) {
            // Az összevont adatlapot csak akkor jelöli „nincs ilyen”-nek, ha más forrásból sincs meg.
            if (!db_->onlineMeta(keyFor(job.name)).found) db_->setOnlineMeta(keyFor(job.name), false, {});
            // Név szerint nincs meg; ha a MusicBrainz már azonosította az előadót, annak azonosítójával még egyszer.
            const Database::OnlineMeta mb = db_->onlineMeta(QStringLiteral("mbid:") + keyFor(job.name).mid(2));
            if (mb.found && !mb.data.isEmpty() && settings_.premium()) {   // az ingyenes kulcs erre is mintaadatot adna
                Job j;
                j.kind = Job::ArtistMb;
                j.name = job.name;
                j.albumId = mb.data;
                enqueue(j, true);
            }
        } else {
            storeArtist(job, best);
        }
        emit artistUpdated(keyFor(job.name));
    } else if (job.kind == Job::ArtistMb) {
        // Csak az a válasz jó, amelyik tényleg ehhez az azonosítóhoz tartozik (az ingyenes kulcs mintaadatot ad).
        for (const QJsonValue &v : root.value(QStringLiteral("artists")).toArray()) {
            const QJsonObject a = v.toObject();
            if (a.value(QStringLiteral("strMusicBrainzID")).toString().compare(job.albumId, Qt::CaseInsensitive) == 0) {
                storeArtist(job, a);
                emit artistUpdated(keyFor(job.name));
                break;
            }
        }
    } else if (job.kind == Job::Track) {
        QJsonObject best;
        for (const QJsonValue &v : root.value(QStringLiteral("track")).toArray()) {
            const QJsonObject t = v.toObject();
            if (fold(t.value(QStringLiteral("strTrack")).toString()) == fold(job.title)) {
                best = t;
                break;
            }
        }
        const QString k = QStringLiteral("t:%1").arg(job.itemId);
        if (best.isEmpty()) {
            db_->setOnlineMeta(k, false, {});
        } else {
            const QString album = best.value(QStringLiteral("strAlbum")).toString();
            const QString albumId = best.value(QStringLiteral("idAlbum")).toString();
            db_->setOnlineMeta(k, true, QString::fromUtf8(QJsonDocument(QJsonObject{{"album", album}, {"albumId", albumId}}).toJson(QJsonDocument::Compact)));
            db_->fillAlbumYear(job.itemId, album, 0);
            if (!albumId.isEmpty()) {
                // Az év az album adatlapján van.
                Job a;
                a.kind = Job::Album;
                a.albumId = albumId;
                a.itemId = job.itemId;
                a.name = album;
                enqueue(a, true);
            }
            db_->notifyItemsChanged();
        }
    } else {
        const QJsonArray albums = root.value(QStringLiteral("album")).toArray();
        if (!albums.isEmpty()) {
            const QJsonObject a = albums.first().toObject();
            db_->fillAlbumYear(job.itemId, a.value(QStringLiteral("strAlbum")).toString(),
                               a.value(QStringLiteral("intYearReleased")).toString().toInt());
            db_->notifyItemsChanged();
        }
    }
    timer_.start(kIntervalMs);
    emit statusChanged();
}

void ArtistInfoService::storeArtist(const Job &job, const QJsonObject &a)
{
    auto s = [&a](const char *k) { return a.value(QLatin1String(k)).toString().trimmed(); };
    auto n = [&a](const char *k) { return a.value(QLatin1String(k)).toVariant().toString().toInt(); };
    QJsonObject o{{"name", s("strArtist")},       {"id", s("idArtist")},          {"mbid", s("strMusicBrainzID")},
                  {"country", s("strCountry")},   {"genre", s("strGenre")},       {"style", s("strStyle")},
                  {"mood", s("strMood")},         {"gender", s("strGender")},     {"label", s("strLabel")},
                  {"website", s("strWebsite")},   {"disbanded", s("strDisbanded")}, {"formed", n("intFormedYear")},
                  {"born", n("intBornYear")},     {"died", n("intDiedYear")},     {"members", n("intMembers")},
                  {"thumb", s("strArtistThumb")}, {"logo", s("strArtistLogo")},   {"bio_en", s("strBiographyEN")},
                  {"bio_hu", s("strBiographyHU")}, {"bio_nl", s("strBiographyNL")}, {"bio_de", s("strBiographyDE")}};
    if (o.value(QStringLiteral("bio_en")).toString().isEmpty()) o.insert(QStringLiteral("bio_en"), s("strBiography"));
    const QString key = keyFor(job.name);
    mergeArtist(job.name, o, QStringLiteral("TheAudioDB"));
    const ArtistData d = artist(job.name);
    fillVocal(key, d);
    if (!d.thumbUrl.isEmpty() && (d.imageFile.isEmpty() || job.force)) fetchImage(key, d.id, d.thumbUrl);
}

// Az ének előtöltése, csak ahol még nem ismert: együttes (több tag), különben a megadott nem.
void ArtistInfoService::fillVocal(const QString &key, const ArtistData &d)
{
    int v = vocal::Unknown;
    const QString g = d.gender.toLower();
    if (d.members > 1) v = g.contains(QLatin1String("mixed")) ? vocal::Mixed : vocal::Group;
    else if (g == QLatin1String("male")) v = vocal::Male;
    else if (g == QLatin1String("female")) v = vocal::Female;
    if (v == vocal::Unknown) return;
    QVector<int> ids;
    for (const Item &it : db_->items())
        if (it.vocal == vocal::Unknown && it.kind == QLatin1String("music") && keyFor(primaryName(it.artist)) == key) ids << it.id;
    if (!ids.isEmpty()) db_->setItemVocal(ids, v);
}

void ArtistInfoService::fetchImage(const QString &key, const QString &id, const QString &url)
{
    if (id.isEmpty() || url.isEmpty()) return;
    QDir().mkpath(imageDir());
    // A kép a kis (250 px-es) változat; a TheAudioDB képszervere, nem az API (nem számít a korlátba).
    // A Wikipédia képe már kicsinyített (az összefoglaló bélyegképe).
    const bool adb = url.contains(QLatin1String("theaudiodb.com"));
    QUrl u(adb && (url.endsWith(QLatin1String(".jpg")) || url.endsWith(QLatin1String(".png"))) ? url + QStringLiteral("/small") : url);
    QNetworkRequest req(u);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setTransferTimeout(30000);
    QNetworkReply *r = nam_.get(req);
    const QString file = QDir(imageDir()).filePath(id + QStringLiteral(".jpg"));
    connect(r, &QNetworkReply::finished, this, [this, r, file, key] {
        r->deleteLater();
        if (r->error() != QNetworkReply::NoError) return;
        const QByteArray data = r->readAll();
        if (data.size() < 100) return;
        QFile f(file);
        if (f.open(QIODevice::WriteOnly)) {
            f.write(data);
            f.close();
            emit artistUpdated(key);
        }
    });
}

QString ArtistInfoService::statusText() const
{
    if (!settings_.enabled) return TR("kikapcsolva");
    const int found = db_->onlineMetaTimes(QStringLiteral("a:"), true).size();
    const int missing = db_->onlineMetaTimes(QStringLiteral("a:"), false).size();
    QStringList p{TR("%1 előadó adatai megvannak").arg(found)};
    if (missing) p << TR("%1 nem található").arg(missing);
    if (pending()) p << TR("%1 lekérés vár").arg(pending());
    if (busy_ && !current_.isEmpty()) p << TR("most: %1").arg(current_);
    if (!lastError_.isEmpty()) p << TR("hiba: %1").arg(lastError_);
    return p.join(QStringLiteral(" · "));
}

// ---- Összevont adatlap ------------------------------------------------------------------------------------------------

void ArtistInfoService::mergeArtist(const QString &name, const QJsonObject &fields, const QString &source)
{
    const QString key = keyFor(name);
    const Database::OnlineMeta m = db_->onlineMeta(key);
    QJsonObject o = m.found ? QJsonDocument::fromJson(m.data.toUtf8()).object() : QJsonObject();
    bool added = false;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const QJsonValue cur = o.value(it.key());
        const bool empty = cur.isUndefined() || cur.isNull() || (cur.isString() && cur.toString().trimmed().isEmpty())
                           || (cur.isDouble() && cur.toDouble() == 0);
        const bool has = !(it.value().isNull() || (it.value().isString() && it.value().toString().trimmed().isEmpty())
                           || (it.value().isDouble() && it.value().toDouble() == 0));
        if (empty && has) {
            o.insert(it.key(), it.value());
            added = true;
        }
    }
    QJsonArray src = o.value(QStringLiteral("sources")).toArray();
    if (added && !src.contains(source)) src.append(source);
    o.insert(QStringLiteral("sources"), src);
    db_->setOnlineMeta(key, true, QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

// ---- MusicBrainz (és Wikipédia) ---------------------------------------------------------------------------------------

QString ArtistInfoService::mbKey(const MbJob &j) const
{
    switch (j.kind) {
    case MbJob::Artist: return QStringLiteral("A:") + keyFor(j.artist);
    case MbJob::Lookup: return QStringLiteral("L:") + j.mbid;
    case MbJob::Recording: return QStringLiteral("R:%1").arg(j.itemId);
    case MbJob::Wikidata: return QStringLiteral("D:") + j.title;
    case MbJob::Wiki: return QStringLiteral("W:%1:%2").arg(j.lang, j.title);
    }
    return {};
}

void ArtistInfoService::mbPush(const MbJob &j, bool front)
{
    const QString k = mbKey(j);
    if (mbQueued_.contains(k)) {
        if (front)
            for (int i = 0; i < mbQueue_.size(); ++i)
                if (mbKey(mbQueue_[i]) == k) {
                    mbQueue_.move(i, 0);
                    break;
                }
        return;
    }
    mbQueued_.insert(k);
    if (front) mbQueue_.prepend(j);
    else mbQueue_.append(j);
    if (!mbBusy_ && !mbTimer_.isActive()) mbTimer_.start(50);
    emit statusChanged();
}

// Egy elem: az előadója (ha még nem kérdeztük), és a száma, ha az album vagy az év üres.
void ArtistInfoService::mbEnqueue(int itemId, const QString &artistField, const QString &title, qint64 durationMs, bool front,
                                  bool force)
{
    if (!settings_.mbEnabled) return;
    const QString name = primaryName(artistField);
    if (name.isEmpty()) return;
    const QString mk = QStringLiteral("mbid:") + keyFor(name).mid(2);
    const Database::OnlineMeta known = db_->onlineMeta(mk);
    // A szám előbb kerül a sor elejére, hogy utána az előadó álljon legelöl (az előadó kell a szám kereséséhez).
    const auto ay = db_->albumYears().value(itemId);
    if (!title.trimmed().isEmpty() && (ay.first.isEmpty() || ay.second == 0)
        && (force || !db_->onlineMeta(QStringLiteral("mbr:%1").arg(itemId)).known)) {
        MbJob r;
        r.kind = MbJob::Recording;
        r.itemId = itemId;
        r.artist = name;
        r.title = title.trimmed();
        r.durationMs = durationMs;
        r.force = force;
        mbPush(r, front);
    }
    if (force || !known.known) {
        MbJob a;
        a.kind = MbJob::Artist;
        a.artist = name;
        a.force = force;
        mbPush(a, front);
    } else if (known.found) {
        // Megvan; ha a fotó még nincs a gépen, azt pótolja.
        const ArtistData d = artist(artistField);
        if (d.found && d.imageFile.isEmpty() && !d.thumbUrl.isEmpty()) fetchImage(keyFor(name), d.id, d.thumbUrl);
    }
}

void ArtistInfoService::mbRefill()
{
    if (mbExhausted_) return;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const QHash<QString, qint64> aFound = db_->onlineMetaTimes(QStringLiteral("mbid:"), true);
    const QHash<QString, qint64> aMiss = db_->onlineMetaTimes(QStringLiteral("mbid:"), false);
    const QHash<QString, qint64> rFound = db_->onlineMetaTimes(QStringLiteral("mbr:"), true);
    const QHash<QString, qint64> rMiss = db_->onlineMetaTimes(QStringLiteral("mbr:"), false);
    const QHash<int, QPair<QString, int>> ay = db_->albumYears();
    QSet<QString> seen;
    int added = 0;
    for (const Item &it : db_->items()) {
        if (it.kind != QLatin1String("music")) continue;
        const QString name = primaryName(it.artist);
        if (name.isEmpty()) continue;
        const QString mk = QStringLiteral("mbid:") + keyFor(name).mid(2);
        if (!seen.contains(mk)) {
            seen.insert(mk);
            const bool due = aFound.contains(mk) ? now - aFound.value(mk) > 180 * 86400
                             : aMiss.contains(mk) ? now - aMiss.value(mk) > 30 * 86400
                                                  : true;
            if (due) {
                MbJob a;
                a.kind = MbJob::Artist;
                a.artist = name;
                mbPush(a, false);
                ++added;
            }
        }
        const auto v = ay.value(it.id);
        const QString rk = QStringLiteral("mbr:%1").arg(it.id);
        if ((v.first.isEmpty() || v.second == 0) && !rFound.contains(rk) && !aMiss.contains(mk)
            && !(rMiss.contains(rk) && now - rMiss.value(rk) < 30 * 86400) && !it.title.trimmed().isEmpty()) {
            MbJob r;
            r.kind = MbJob::Recording;
            r.itemId = it.id;
            r.artist = name;
            r.title = it.title.trimmed();
            r.durationMs = it.durationMs;
            mbPush(r, false);
            ++added;
        }
        if (added >= 40) return;
    }
    if (!added) mbExhausted_ = true;
}

// Lucene-kifejezés idézőjelben: csak az idézőjel és a fordított perjel kell elé jel.
static QString phrase(const QString &s)
{
    QString t = s;
    t.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    t.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + t + QLatin1Char('"');
}

void ArtistInfoService::mbNext()
{
    if (!settings_.mbEnabled || mbBusy_) return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (mbPauseUntil_ > now) {
        mbTimer_.start(int(std::min<qint64>(mbPauseUntil_ - now, 60000)));
        return;
    }
    if (mbQueue_.isEmpty() && settings_.mode == Auto) mbRefill();
    if (mbQueue_.isEmpty()) {
        mbCurrent_.clear();
        emit statusChanged();
        return;
    }
    const MbJob job = mbQueue_.takeFirst();
    mbQueued_.remove(mbKey(job));
    QUrl url;
    QUrlQuery q;
    switch (job.kind) {
    case MbJob::Artist:
        url = QUrl(mbUrl() + QStringLiteral("artist/"));
        q.addQueryItem(QStringLiteral("query"), QStringLiteral("artist:") + phrase(job.artist));
        q.addQueryItem(QStringLiteral("limit"), QStringLiteral("5"));
        mbCurrent_ = job.artist;
        break;
    case MbJob::Lookup:
        url = QUrl(mbUrl() + QStringLiteral("artist/") + job.mbid);
        q.addQueryItem(QStringLiteral("inc"), QStringLiteral("url-rels genres"));
        mbCurrent_ = job.artist;
        break;
    case MbJob::Recording: {
        // Az előadó azonosítójával pontosabb; ha még nincs meg, a nevével.
        const Database::OnlineMeta m = db_->onlineMeta(QStringLiteral("mbid:") + keyFor(job.artist).mid(2));
        if (m.known && !m.found) {
            db_->setOnlineMeta(QStringLiteral("mbr:%1").arg(job.itemId), false, {});
            mbTimer_.start(0);
            return;
        }
        url = QUrl(mbUrl() + QStringLiteral("recording/"));
        q.addQueryItem(QStringLiteral("query"), QStringLiteral("recording:") + phrase(job.title) + QStringLiteral(" AND ")
                                                    + (m.found && !m.data.isEmpty() ? QStringLiteral("arid:") + m.data
                                                                                     : QStringLiteral("artist:") + phrase(job.artist)));
        q.addQueryItem(QStringLiteral("limit"), QStringLiteral("25"));
        mbCurrent_ = QStringLiteral("%1 – %2").arg(job.artist, job.title);
        break;
    }
    case MbJob::Wikidata:
        url = QUrl(wdUrl());
        q.addQueryItem(QStringLiteral("action"), QStringLiteral("wbgetentities"));
        q.addQueryItem(QStringLiteral("ids"), job.title);
        q.addQueryItem(QStringLiteral("props"), QStringLiteral("sitelinks"));
        q.addQueryItem(QStringLiteral("sitefilter"), QStringLiteral("huwiki|enwiki|nlwiki|dewiki"));
        q.addQueryItem(QStringLiteral("format"), QStringLiteral("json"));
        mbCurrent_ = job.artist;
        break;
    case MbJob::Wiki:
        url = QUrl(wpUrl(job.lang) + QString::fromUtf8(QUrl::toPercentEncoding(QString(job.title).replace(QLatin1Char(' '), QLatin1Char('_')))));
        mbCurrent_ = job.artist;
        break;
    }
    if (job.kind <= MbJob::Recording) q.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
    if (!q.isEmpty()) url.setQuery(q.query(QUrl::FullyEncoded).replace(QLatin1Char('+'), QStringLiteral("%2B")));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(20000);
    QNetworkReply *reply = nam_.get(req);
    mbBusy_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] { mbFinished(reply, job); });
    emit statusChanged();
}

void ArtistInfoService::mbFinished(QNetworkReply *reply, const MbJob &job)
{
    reply->deleteLater();
    mbBusy_ = nullptr;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::OperationCanceledError && !settings_.mbEnabled) return;
    // A Wikipédia nem létező cikkre 404-et ad: az nem hiba, csak nincs életrajz.
    const bool wiki = job.kind == MbJob::Wikidata || job.kind == MbJob::Wiki;
    if (status == 503 || status == 429 || (reply->error() != QNetworkReply::NoError && !(wiki && status == 404))) {
        // Túlterhelés (a MusicBrainz 503-at ad) vagy hálózati hiba: szünet, majd újra.
        mbError_ = status == 503 || status == 429 ? TR("túl sok kérés, rövid szünet") : reply->errorString();
        const int wait = status == 503 || status == 429 ? 15000 : 60000;
        mbPauseUntil_ = QDateTime::currentMSecsSinceEpoch() + wait;
        mbPush(job, true);
        mbTimer_.start(wait);
        emit statusChanged();
        return;
    }
    mbError_.clear();
    const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
    switch (job.kind) {
    case MbJob::Artist: mbArtist(job, root); break;
    case MbJob::Lookup: mbLookup(job, root); break;
    case MbJob::Recording: mbRecording(job, root); break;
    case MbJob::Wikidata: {
        // A cikkek címei: a program nyelvén és angolul (az életrajz e kettőből jön).
        const QJsonObject links = root.value(QStringLiteral("entities")).toObject().value(job.title).toObject()
                                      .value(QStringLiteral("sitelinks")).toObject();
        for (const QString &lang : {i18n::language(), QStringLiteral("en")}) {
            const QString t = links.value(lang + QStringLiteral("wiki")).toObject().value(QStringLiteral("title")).toString();
            if (t.isEmpty()) continue;
            MbJob w;
            w.kind = MbJob::Wiki;
            w.artist = job.artist;
            w.lang = lang;
            w.title = t;
            w.mbid = job.mbid;
            mbPush(w, true);
        }
        break;
    }
    case MbJob::Wiki: {
        if (status == 404 || root.value(QStringLiteral("type")).toString() == QLatin1String("disambiguation")) break;
        const QString extract = root.value(QStringLiteral("extract")).toString().trimmed();
        const QString thumb = root.value(QStringLiteral("thumbnail")).toObject().value(QStringLiteral("source")).toString();
        QJsonObject f;
        if (!extract.isEmpty()) f.insert(QStringLiteral("bio_") + job.lang, extract);
        // A cikk címe (az Infó ablak „Tovább a Wikipédián” hivatkozásához).
        QString page = root.value(QStringLiteral("content_urls")).toObject().value(QStringLiteral("desktop")).toObject()
                           .value(QStringLiteral("page")).toString();
        if (!page.startsWith(QLatin1String("https://")))
            page = QStringLiteral("https://%1.wikipedia.org/wiki/%2")
                       .arg(job.lang, QString::fromUtf8(QUrl::toPercentEncoding(QString(job.title).replace(QLatin1Char(' '), QLatin1Char('_')))));
        if (!extract.isEmpty()) f.insert(QStringLiteral("wiki_") + job.lang, page);
        if (!thumb.isEmpty()) f.insert(QStringLiteral("thumb"), thumb);
        if (!f.isEmpty()) {
            mergeArtist(job.artist, f, TR("Wikipédia"));
            const ArtistData d = artist(job.artist);
            if (d.imageFile.isEmpty() && !d.thumbUrl.isEmpty()) fetchImage(keyFor(job.artist), d.id, d.thumbUrl);
            emit artistUpdated(keyFor(job.artist));
        }
        break;
    }
    }
    mbTimer_.start(job.kind == MbJob::Wiki || job.kind == MbJob::Wikidata ? 300 : kMbIntervalMs);
    emit statusChanged();
}

static int yearOf(const QString &date) { return date.size() >= 4 ? date.left(4).toInt() : 0; }

void ArtistInfoService::mbArtist(const MbJob &job, const QJsonObject &root)
{
    // A legjobb találat: a név (vagy egy álneve) egyezik; ennek híján a 100 pontos első.
    QJsonObject best;
    int bestScore = -1;
    const QString want = fold(job.artist);
    const QJsonArray arr = root.value(QStringLiteral("artists")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject a = v.toObject();
        const int score = a.value(QStringLiteral("score")).toVariant().toInt();
        bool same = fold(a.value(QStringLiteral("name")).toString()) == want;
        for (const QJsonValue &al : a.value(QStringLiteral("aliases")).toArray())
            same = same || fold(al.toObject().value(QStringLiteral("name")).toString()) == want;
        if (same && score > bestScore) {
            best = a;
            bestScore = score;
        }
    }
    if (best.isEmpty() && !arr.isEmpty() && arr.first().toObject().value(QStringLiteral("score")).toVariant().toInt() >= 100)
        best = arr.first().toObject();
    const QString mk = QStringLiteral("mbid:") + keyFor(job.artist).mid(2);
    if (best.isEmpty()) {
        db_->setOnlineMeta(mk, false, {});
        if (!db_->onlineMeta(keyFor(job.artist)).found) db_->setOnlineMeta(keyFor(job.artist), false, {});
        emit artistUpdated(keyFor(job.artist));
        return;
    }
    const QString mbid = best.value(QStringLiteral("id")).toString();
    db_->setOnlineMeta(mk, true, mbid);
    const QString type = best.value(QStringLiteral("type")).toString();
    const QJsonObject span = best.value(QStringLiteral("life-span")).toObject();
    const QString beginArea = best.value(QStringLiteral("begin-area")).toObject().value(QStringLiteral("name")).toString();
    const QString area = best.value(QStringLiteral("area")).toObject().value(QStringLiteral("name")).toString();
    QString where = beginArea;
    if (!area.isEmpty() && area != beginArea) where = where.isEmpty() ? area : where + QStringLiteral(", ") + area;
    const bool person = type == QLatin1String("Person") || type == QLatin1String("Character");
    const int begin = yearOf(span.value(QStringLiteral("begin")).toString());
    const int end = yearOf(span.value(QStringLiteral("end")).toString());
    QJsonObject f{{"name", best.value(QStringLiteral("name")).toString()},
                  {"mbid", mbid},
                  {"id", mbid},
                  {"type", type},
                  {"country", where},
                  {"gender", best.value(QStringLiteral("gender")).toString()}};
    if (person) {
        f.insert(QStringLiteral("born"), begin);
        f.insert(QStringLiteral("died"), end);
        f.insert(QStringLiteral("members"), 1);
    } else {
        f.insert(QStringLiteral("formed"), begin);
        if (span.value(QStringLiteral("ended")).toBool()) f.insert(QStringLiteral("disbanded"), QString::number(end > 0 ? end : 1));
        if (type == QLatin1String("Group")) f.insert(QStringLiteral("members"), 2);   // együttes (a tagok száma nem ismert)
    }
    mergeArtist(job.artist, f, QStringLiteral("MusicBrainz"));
    fillVocal(keyFor(job.artist), artist(job.artist));
    emit artistUpdated(keyFor(job.artist));
    // A részletek: műfajok és a hivatkozások (Wikidata → Wikipédia); TheAudioDB-kulccsal az azonosító szerinti adatlap.
    MbJob l;
    l.kind = MbJob::Lookup;
    l.artist = job.artist;
    l.mbid = mbid;
    mbPush(l, true);
    if (settings_.enabled && !db_->onlineMeta(QStringLiteral("adb:") + keyFor(job.artist).mid(2)).found) {
        Job j;
        j.kind = Job::ArtistMb;
        j.name = job.artist;
        j.albumId = mbid;
        if (settings_.premium()) enqueue(j, true);    // az ingyenes kulcs erre is csak mintaadatot adna
    }
}

void ArtistInfoService::mbLookup(const MbJob &job, const QJsonObject &root)
{
    // A legtöbbször megadott műfajok (legfeljebb kettő), a weboldal és a Wikidata-azonosító.
    QVector<QPair<int, QString>> genres;
    for (const QJsonValue &v : root.value(QStringLiteral("genres")).toArray()) {
        const QJsonObject g = v.toObject();
        genres.push_back({g.value(QStringLiteral("count")).toInt(), g.value(QStringLiteral("name")).toString()});
    }
    std::stable_sort(genres.begin(), genres.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    QStringList top;
    for (const auto &g : genres) {
        if (g.second.isEmpty()) continue;
        QString n = g.second;
        n[0] = n[0].toUpper();
        top << n;
        if (top.size() == 2) break;
    }
    QString website, qid;
    for (const QJsonValue &v : root.value(QStringLiteral("relations")).toArray()) {
        const QJsonObject r = v.toObject();
        const QString type = r.value(QStringLiteral("type")).toString();
        const QString res = r.value(QStringLiteral("url")).toObject().value(QStringLiteral("resource")).toString();
        if (type == QLatin1String("official homepage") && website.isEmpty()) website = res;
        if (type == QLatin1String("wikidata") && qid.isEmpty()) qid = res.section(QLatin1Char('/'), -1);
    }
    QJsonObject f;
    if (!top.isEmpty()) f.insert(QStringLiteral("genre"), top.first());
    if (top.size() > 1) f.insert(QStringLiteral("style"), top[1]);
    if (!website.isEmpty()) f.insert(QStringLiteral("website"), website);
    if (!qid.isEmpty()) f.insert(QStringLiteral("wikidata"), qid);
    if (!f.isEmpty()) {
        mergeArtist(job.artist, f, QStringLiteral("MusicBrainz"));
        emit artistUpdated(keyFor(job.artist));
    }
    if (settings_.wikiEnabled && qid.startsWith(QLatin1Char('Q'))) {
        MbJob w;
        w.kind = MbJob::Wikidata;
        w.artist = job.artist;
        w.title = qid;
        w.mbid = job.mbid;
        mbPush(w, true);
    }
}

// A szám: a cím egyezik, az előadó stimmel, a hossza (ha mindkettő ismert) legfeljebb 15 mp-cel tér el. Az év a
// felvétel első megjelenése; az album a legkorábbi hivatalos stúdióalbum (ennek híján kislemez, EP), amelyen szerepel.
void ArtistInfoService::mbRecording(const MbJob &job, const QJsonObject &root)
{
    const QString rk = QStringLiteral("mbr:%1").arg(job.itemId);
    const QString wantTitle = fold(job.title);
    const QString wantArtist = fold(job.artist);
    QJsonObject best;
    int bestYear = 99999;
    for (const QJsonValue &v : root.value(QStringLiteral("recordings")).toArray()) {
        const QJsonObject r = v.toObject();
        if (r.value(QStringLiteral("score")).toVariant().toInt() < 80) continue;
        if (fold(r.value(QStringLiteral("title")).toString()) != wantTitle) continue;
        bool artistOk = false;
        for (const QJsonValue &c : r.value(QStringLiteral("artist-credit")).toArray()) {
            const QJsonObject ac = c.toObject();
            artistOk = artistOk || fold(ac.value(QStringLiteral("name")).toString()) == wantArtist
                       || fold(ac.value(QStringLiteral("artist")).toObject().value(QStringLiteral("name")).toString()) == wantArtist;
        }
        if (!artistOk) continue;
        const qint64 len = r.value(QStringLiteral("length")).toVariant().toLongLong();
        if (len > 0 && job.durationMs > 0 && std::llabs(len - job.durationMs) > 15000) continue;
        const int y = yearOf(r.value(QStringLiteral("first-release-date")).toString());
        const int yy = y > 0 ? y : 99998;
        if (yy < bestYear) {
            best = r;
            bestYear = yy;
        }
    }
    if (best.isEmpty()) {
        db_->setOnlineMeta(rk, false, {});
        return;
    }
    int year = yearOf(best.value(QStringLiteral("first-release-date")).toString());
    // Az album: 0 stúdióalbum, 1 kislemez vagy EP, 2 bármi más; azon belül a legkorábbi.
    QString album;
    int albumRank = 99, albumYear = 99999;
    for (const QJsonValue &v : best.value(QStringLiteral("releases")).toArray()) {
        const QJsonObject rel = v.toObject();
        if (!rel.value(QStringLiteral("status")).toString().isEmpty() && rel.value(QStringLiteral("status")).toString() != QLatin1String("Official"))
            continue;
        const QJsonObject rg = rel.value(QStringLiteral("release-group")).toObject();
        const QString primary = rg.value(QStringLiteral("primary-type")).toString();
        const bool secondary = !rg.value(QStringLiteral("secondary-types")).toArray().isEmpty();   // válogatás, koncert…
        const int rank = primary == QLatin1String("Album") && !secondary ? 0
                         : (primary == QLatin1String("Single") || primary == QLatin1String("EP")) && !secondary ? 1
                                                                                                                  : 2;
        const int ry = yearOf(rel.value(QStringLiteral("date")).toString());
        const int ryy = ry > 0 ? ry : 99998;
        if (rank < albumRank || (rank == albumRank && ryy < albumYear)) {
            album = rel.value(QStringLiteral("title")).toString();
            albumRank = rank;
            albumYear = ryy;
        }
    }
    if (year <= 0 && albumYear < 99998) year = albumYear;
    db_->setOnlineMeta(rk, true, QString::fromUtf8(QJsonDocument(QJsonObject{{"album", album}, {"year", year},
                                                                            {"mbid", best.value(QStringLiteral("id")).toString()}})
                                                       .toJson(QJsonDocument::Compact)));
    db_->fillAlbumYear(job.itemId, album, year);
    ++mbAlbums_;
    db_->notifyItemsChanged();
}

QString ArtistInfoService::mbStatusText() const
{
    if (!settings_.mbEnabled) return TR("kikapcsolva");
    const int found = db_->onlineMetaTimes(QStringLiteral("mbid:"), true).size();
    const int missing = db_->onlineMetaTimes(QStringLiteral("mbid:"), false).size();
    const int tracks = db_->onlineMetaTimes(QStringLiteral("mbr:"), true).size();
    QStringList p{TR("%1 előadó megvan").arg(found)};
    if (missing) p << TR("%1 nem található").arg(missing);
    p << TR("%1 szám albuma és éve").arg(tracks);
    if (mbPending()) p << TR("%1 lekérés vár").arg(mbPending());
    if (mbBusy_ && !mbCurrent_.isEmpty()) p << TR("most: %1").arg(mbCurrent_);
    if (!mbError_.isEmpty()) p << TR("hiba: %1").arg(mbError_);
    return p.join(QStringLiteral(" · "));
}
