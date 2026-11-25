// Előadói adatok és a számok albuma, éve, választható forrásokból (Beállítások → Előadói adatok):
//   - MusicBrainz (ingyenes, kulcs nélkül, másodpercenként egy kérés): az előadó országa, évei, típusa, műfaja, és a
//     számok albuma és éve; a hivatkozásai alapján a Wikipédiából (Wikidata → Wikipédia összefoglaló) életrajz és fotó;
//   - TheAudioDB (kulccsal, percenként legfeljebb 30 kérés): életrajz, fotó, stílus, hangulat; saját (Premium)
//     kulccsal a számok albuma és éve is. Az ingyenes „123” kulcs jelenleg csak mintaadatot ad (mindig ugyanazt az
//     előadót): a nem egyező választ a program eldobja.
// Minden forrás külön kapcsolható; kikapcsolva egyetlen kérést sem küld. Az adatok az adatbázis online_meta táblájába
// kerülnek ("a:<előadó>": az összevont adatlap; "mbid:", "adb:", "mbr:" a források saját nyilvántartása); a fotók a
// gépen gyűlnek. A meglévő adatot nem írja felül: az ének, az album és az év csak ott töltődik ki, ahol még üres,
// és egy forrás sem írja át, amit egy másik már megadott.
#pragma once

#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>

#include <QNetworkReply>

class Database;
class QJsonObject;

struct ArtistData {
    bool known = false;         // kérdeztük már
    bool found = false;         // a TheAudioDB ismeri
    QString name, id, mbid, country, genre, style, mood, gender, label, website, disbanded;
    int formed = 0, born = 0, died = 0, members = 0;
    QString type;               // MusicBrainz: Group, Person, …
    QString bio;                // a program nyelvén, ennek híján angolul
    QStringList sources;        // honnan jöttek az adatok (MusicBrainz, Wikipédia, TheAudioDB)
    QString thumbUrl, logoUrl;
    QString imageFile;          // a letöltött fotó a gépen (üres: még nincs)
    QString wikiUrl;            // a Wikipédia-cikk (a program nyelvén, ennek híján angolul)
    qint64 fetchedAt = 0;       // mikor frissült (Unix idő)
    QString facts() const;      // „London, England · 1996 · Alternative Rock · Rock/Pop · Happy”
};

class ArtistInfoService : public QObject {
    Q_OBJECT
public:
    enum Mode { Auto = 0, OnLoad = 1, Manual = 2 };
    struct Settings {
        bool enabled = false;
        QString key = QStringLiteral("123");    // az ingyenes kulcs
        int mode = Auto;
        bool mbEnabled = false;                 // MusicBrainz: előadói adatok, a számok albuma és éve (kulcs nélkül)
        bool wikiEnabled = true;                // Wikipédia: életrajz és fotó a MusicBrainz hivatkozása alapján
        bool premium() const { return !key.trimmed().isEmpty() && key.trimmed() != QLatin1String("123") && key.trimmed() != QLatin1String("2"); }
    };

    explicit ArtistInfoService(Database *db, QObject *parent = nullptr);
    ~ArtistInfoService() override;
    static ArtistInfoService *instance();

    Settings settings() const { return settings_; }
    void setSettings(const Settings &s);      // menti és alkalmazza
    void reload();                            // a beállítások újraolvasása az adatbázisból
    void clearCache();                        // a lekért adatok és a fotók törlése

    // Az előadó mezőből a keresett név: több előadónál („A feat. B”) az első.
    QString primaryName(const QString &artistField) const;
    static QString keyFor(const QString &name);
    ArtistData artist(const QString &artistField);          // a gyorsítótárból
    // Lekérés: front = a sor elejére (a lejátszóba töltött elem); force = akkor is, ha már megvan.
    void request(const QString &artistField, bool front = true, bool force = false);
    void requestItems(const QVector<int> &itemIds, bool force);   // kézi indítás a hangtárból
    void requestForPlayer(int itemId);         // egy elem a lejátszóba került (a „betöltéskor” módhoz)
    // Az Infó ablakban megjelenített elem: a sor elejére, a „kézi” módban is (a megnyitás maga a kérés).
    void requestForInfo(int itemId, bool force);
    bool pendingFor(const QString &artistField, int itemId) const;   // van-e még várakozó kérés hozzá

    QString statusText() const;                // TheAudioDB
    QString mbStatusText() const;              // MusicBrainz (és Wikipédia)
    int pending() const { return int(queue_.size()) + (busy_ ? 1 : 0); }
    int mbPending() const { return int(mbQueue_.size()) + (mbBusy_ ? 1 : 0); }
    bool anyEnabled() const { return settings_.enabled || settings_.mbEnabled; }

signals:
    void artistUpdated(const QString &key);
    void statusChanged();

private:
    struct Job {
        enum Kind { Artist, Track, Album, ArtistMb } kind = Artist;
        QString name;           // előadó (Artist, Track)
        QString title;          // cím (Track)
        QString albumId;        // (Album); ArtistMb: a MusicBrainz-azonosító
        int itemId = 0;
        bool force = false;
    };
    void enqueue(const Job &j, bool front);
    void next();
    void finished(QNetworkReply *reply, const Job &job);
    void storeArtist(const Job &job, const QJsonObject &a);
    void fetchImage(const QString &key, const QString &id, const QString &url);
    void fillVocal(const QString &key, const ArtistData &d);
    void refillAuto();
    QString imageDir() const;
    QString jobKey(const Job &j) const;

    // MusicBrainz: külön sor, másodpercenként legfeljebb egy kéréssel (a szolgáltatás szabálya). Ugyanebben a sorban
    // mennek a Wikidata- és Wikipédia-kérések is (az előadó életrajza és fotója).
    struct MbJob {
        enum Kind { Artist, Lookup, Recording, Wikidata, Wiki } kind = Artist;
        int itemId = 0;
        QString artist;         // a keresett előadónév (primaryName)
        QString title;          // Recording: a szám címe; Wikidata: a Q-azonosító; Wiki: a cikk címe
        QString mbid, lang;     // Lookup, Recording: az előadó MusicBrainz-azonosítója; Wiki: a nyelv
        qint64 durationMs = 0;
        bool force = false;
    };
    void mbEnqueue(int itemId, const QString &artist, const QString &title, qint64 durationMs, bool front, bool force);
    void mbPush(const MbJob &j, bool front);
    QString mbKey(const MbJob &j) const;
    void mbNext();
    void mbFinished(QNetworkReply *reply, const MbJob &job);
    void mbRefill();
    void mbArtist(const MbJob &job, const QJsonObject &root);
    void mbLookup(const MbJob &job, const QJsonObject &root);
    void mbRecording(const MbJob &job, const QJsonObject &root);
    // Az összevont adatlap bővítése: csak az üres mezők töltődnek ki; source: a forrás neve.
    void mergeArtist(const QString &name, const QJsonObject &fields, const QString &source);
    QList<MbJob> mbQueue_;
    QSet<QString> mbQueued_;
    int mbAlbums_ = 0;                  // ebben a futásban kitöltött album/év
    QTimer mbTimer_;
    QPointer<QNetworkReply> mbBusy_;
    QString mbCurrent_, mbError_;
    qint64 mbPauseUntil_ = 0;
    bool mbExhausted_ = false;

    Database *db_;
    Settings settings_;
    QNetworkAccessManager nam_;
    QList<Job> queue_;
    QSet<QString> queued_;
    QTimer timer_, idle_;
    QPointer<QNetworkReply> busy_;
    QString current_, lastError_;
    qint64 pauseUntil_ = 0;
    bool autoExhausted_ = false;
    int done_ = 0;
};
