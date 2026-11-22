#pragma once
// Weboldal: a „Most szól” és a „Következő számok” küldése a WordPress-oldalra (az Éter Radio bővítménynek), és a
// látogatók számkéréseinek begyűjtése. A playout kezdeményez minden kapcsolatot (néhány másodpercenként egy aláírt
// HTTP(S) kérés), így a stúdió gépét nem kell az internet felől elérhetővé tenni.
//
//   POST <oldal>/?rest_route=/eter/v1/sync     állapot + a kérések új állapotai → az új kérések, kell-e a katalógus
//   POST <oldal>/?rest_route=/eter/v1/catalog  a kérhető számok [[id, cím, előadó], …]
//   Aláírás: X-Eter-Time (unix mp) és X-Eter-Signature = hex HMAC-SHA256(kulcs, "<idő>\n<törzs>")
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

class Database;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class WebSync : public QObject {
    Q_OBJECT
public:
    struct Config {
        bool enabled = false;
        QString url;                      // a WordPress-oldal címe
        QString key;                      // a bővítmény kulcsa
        int nextCount = 10;               // ennyi következő szám megy ki
        bool musicOnly = true;            // a jingle, reklám és hasonló elemek nem látszanak
        bool requests = false;            // számkérés
        bool approve = false;             // a kéréseket a stúdióban jóvá kell hagyni
        bool allMusic = true;             // minden zene kérhető (különben: categories)
        QList<int> categories;
        bool always = true;               // mindig lehet kérni (különben: a clocks sémák óráiban)
        QList<int> clocks;
    };
    static Config loadConfig();
    static void saveConfig(const Config &c);

    struct Track {
        QString title, artist;
        qint64 startMs = 0;               // unix ms
        qint64 durationMs = 0;
        bool request = false;
    };
    struct Incoming {
        int remoteId = 0;
        int itemId = 0;
        qint64 time = 0;                  // unix mp
    };

    WebSync(Database *db, QObject *parent = nullptr);
    void configure(const Config &c);
    const Config &config() const { return cfg_; }

    void setTracks(const Track &now, bool hasNow, const QVector<Track> &next);
    // A kérés állapota a weboldal felé (a következő szinkronnal megy ki).
    void ack(int remoteId, const QString &status);
    bool requestsOpen() const;            // most lehet-e kérni (be van kapcsolva, és a mostani óra engedi)
    bool requestable(int itemId) const;   // a szám a katalógusban van-e
    void syncNow();                       // soron kívüli szinkron (például számváltáskor)
    QString status() const { return status_; }
    bool ok() const { return ok_; }
    QDateTime lastSync() const { return lastOk_; }
    void invalidateCatalog() { catalogDirty_ = true; }

signals:
    void requestsArrived(const QVector<WebSync::Incoming> &list);
    void statusChanged();

private:
    Database *db_;
    Config cfg_;
    QNetworkAccessManager *net_;
    QTimer *timer_, *soon_;
    QNetworkReply *busy_ = nullptr;
    bool ok_ = false;
    QString status_;
    QDateTime lastOk_;
    Track now_;
    bool hasNow_ = false;
    QVector<Track> next_;
    QHash<int, QString> acks_;           // távoli azonosító → állapot, amíg a weboldal nem nyugtázza
    QHash<int, QString> sentAcks_;
    QByteArray catalog_;                 // a katalógus JSON-ja
    QString catalogRev_;
    QSet<int> catalogIds_;
    bool catalogDirty_ = true;
    bool sendCatalog_ = false;
    int failures_ = 0;

    void tick();
    void post(const QString &path, const QByteArray &body);
    void finished(QNetworkReply *reply, const QString &path);
    void buildCatalog();
    void setStatus(bool ok, const QString &text);
};
