// Külső playlistek (Beállítások → Külső playlistek): más ütemező programok (például a StationPlaylist Creator)
// által kiírt M3U playlistek beolvasása egy figyelt mappából.
//   - Fájlonként egy óra (nov.30-14.M3U) vagy egy egész nap (nov.29-00.M3U, csak útvonalakkal): a fájlnév mintából
//     derül ki, melyik nap melyik órájára szól. Az egész napos fájl a hosszak alapján bomlik órákra.
//   - Formátum: sima M3U (soronként egy útvonal) és #EXTM3U (#EXTINF: hossz, cím; a Creator „Studio” formátuma
//     „|előadó|cím|kategória|…|típus|…” mezőkkel). Kódolás: UTF-8, ha érvényes, különben a rendszer ANSI kódlapja.
//   - Az elemeket útvonal szerint keresi a hangtárban (útvonal-megfeleltetéssel); ami nincs benne, beolvassa a
//     kategória-megfeleltetés szerinti kategóriába (a háttérelemzés elkészíti a hullámformát és a mix pontot).
//   - Az ütemező a heti rács „Külső playlist” sémájú (és a séma nélküli) óráihoz kéri: ha van fájl, abból készül az
//     óra, ha nincs, a tartalék sémából. Az óra első eleme fix időponttal indul (óra:00:00).
#pragma once

#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

class Database;

namespace ext {

struct Line {
    QString path;              // ahogy a fájlban áll
    qint64 durationMs = -1;    // #EXTINF; −1 = nincs
    QString artist, title, category;
    bool jingle = false;       // Creator: típus 2
};

struct Parsed {
    QVector<Line> lines;
    bool extended = false;     // #EXTM3U
    bool creator = false;      // a Creator „|…|” mezői
    QString encoding;          // a felismert kódolás (felirathoz)
};

QString decode(const QByteArray &data, const QString &encoding, QString *used = nullptr);   // auto|utf8|cp1250|cp1252
Parsed parse(const QByteArray &data, const QString &encoding);

struct Target {
    QDate date;
    int hour = -1;             // −1: egész napos fájl
    bool valid() const { return date.isValid(); }
};
// A fájlnév és a minta ({év} {hónap} {hó} {nap} {óra}, angolul {year} {mon} {month} {day} {hour}) alapján.
Target match(const QString &fileName, const QString &pattern, const QDate &today);
QString defaultPattern();

} // namespace ext

struct ExtFile {
    QString path, name;
    ext::Target target;
    qint64 mtime = 0, size = 0;
};

struct ExtReport {
    QString file;
    int key = 0;                          // ÉÉÉÉHHNNÓÓ
    int total = 0, fromLibrary = 0, added = 0, missing = 0;
    qint64 lengthMs = 0;
    QStringList missingPaths;
    QString error;
    bool fallback = false;                // nem volt fájl: a tartalék sémából
};

class ExternalPlaylists {
public:
    explicit ExternalPlaylists(Database *db) : db_(db) {}

    bool enabled() const;
    // A heti rács „Külső playlist” sémája (külön séma, elemek nélkül); create: ha még nincs, létrehozza.
    int clockId(bool create = false) const;
    static int clockId(Database *db);
    int fallbackClock() const;            // a tartalék séma (0 = nincs: a hiányzó óra üres marad)

    QVector<ExtFile> scan() const;        // a mappa fájljai, amelyek illeszkednek a mintára (időrendben)
    const ExtFile *fileFor(const QDateTime &hour, const QVector<ExtFile> &files) const;
    bool covers(const QDateTime &hour) const;

    // Az óra elkészítése a fájlból. −1: nincs fájl (vagy egyetlen elemet sem adott): a hívó a tartalékból generál.
    int importHour(const QDateTime &hour, const QDateTime &now);
    int importFrom(const ExtFile &f, const QDateTime &hour, const QDateTime &now);
    // Egy tetszőleges fájl kézi beolvasása (a neve adja az időpontot): a még hátralévő órái újra elkészülnek.
    int importFile(const QString &path, const QDateTime &now, QString *error);
    // Egy „Külső” órához nem volt fájl: a tartalék sémából készül (napló, jelentés, állapotsor).
    void noteMissing(const QDateTime &hour, bool fallback);
    // A már beolvasott (vagy tartalékból készült) jövőbeli órák frissítése, ha a fájl megváltozott vagy megérkezett.
    int refresh(const QDateTime &now, int aheadHours);

    QString sourceOf(int key) const;      // a beolvasott óra fájlja ("" = nem külső)
    QVector<ExtReport> takeReports();     // a legutóbbi beolvasások (a jelentéshez és az állapotsorhoz)
    const QVector<ExtReport> &recent() const { return recent_; }

    // Az elem útvonala ezen a gépen (útvonal-megfeleltetés, elválasztók).
    QString mapPath(const QString &path) const;
    // A fájlban szereplő Creator-kategóriák (a beállítások táblázatához).
    QStringList categoriesIn(const QVector<ExtFile> &files) const;

private:
    struct Resolved {
        int itemId = 0;
        qint64 airMs = 0;
        QString path;                     // a megfeleltetett útvonal
        int state = 0;                    // 0: a hangtárból, 1: most beolvasva, 2: hiányzik
    };
    const ext::Parsed &parsed(const ExtFile &f) const;
    Resolved resolve(const ext::Line &line, ExtReport &rep);
    int categoryFor(const ext::Line &line);
    QVector<int> hourSlice(const ExtFile &f, int hour, QVector<Resolved> &all, ExtReport &rep);
    void remember(int key, const ExtFile &f);
    void forget(int key);
    QHash<QString, QString> records() const;

    Database *db_;
    mutable QHash<QString, std::pair<qint64, ext::Parsed>> cache_;   // útvonal → (mtime+méret, beolvasott)
    QHash<QString, QVector<Resolved>> resolved_;                      // a fájlok feloldott elemei (aláírás szerint)
    QVector<ExtReport> reports_, recent_;
    QSet<int> notedMissing_;
};
