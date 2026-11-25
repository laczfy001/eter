// SQLite adatbázis: hangtár, kategóriák, playlist, adásnapló.
#pragma once
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <functional>
#include <QString>
#include <algorithm>
#include <QVector>

struct Category {
    int id = 0;
    QString kind, name, color;
    int itemCount = 0;
};

struct Item {
    int id = 0;
    int categoryId = 0;
    QString categoryName, kind, color;
    QString title, artist, path;
    qint64 durationMs = 0;
    qint64 lastPlayedAt = 0;        // Unix idő, 0 = még nem szólt
    qint64 mixOutMs = 0;            // ahol a következő elem indul; 0 = még nem ismert
    qint64 userMixOutMs = 0;        // a Mix editorban a számhoz mentett mix pont; 0 = nincs
    qint64 cueInMs = 0;             // innen indul a lejátszás
    qint64 fadeOutMs = -1;          // a számhoz mentett kiúsztatás; −1 = alapértelmezett
    QString fadeCurve;              // "linear", "log", "scurve" vagy üres
    qint64 introMs = 0;             // az intro vége (az ének kezdete); 0 = nincs megadva
    qint64 cueOutMs = 0;            // a számhoz mentett vég (itt hallgat el); 0 = nincs megadva
    int energy = 0;                 // 1 nagyon nyugodt … 5 nagyon pörgős; 0 = nincs megadva
    double bpm = 0;                 // tempó; 0 = nem ismert
    int musicKey = 0;               // hangnem 1..24 (core/MusicKey.h); 0 = nem ismert
    int analysisVer = 0;            // a háttérelemzés verziója, amellyel már megnézte
    int vocal = 0;                  // ének (core/Vocal.h); 0 = nem ismert
    int dayparts = 0;               // napszakok bitmaszkja (lásd daypartOf); 0 = bármikor szólhat
    QString volumeEnv;              // hangerőgörbe (core/VolumeEnvelope.h: "ms:dB;…"); üres = változatlan hangerő
    // Ennyi ideig tölti ki az adást: a mix pontig, ennek híján a teljes hossz egy becsült átfedéssel.
    qint64 airMs() const { return mixOutMs > 0 ? mixOutMs : std::max<qint64>(0, durationMs - (durationMs > 30000 ? 3000 : 0)); }
};

struct AnalysisJob {
    int id = 0;
    QString path;
    bool needBpm = false, needKey = false;
};

// Számkérés a weboldalról (Beállítások → Weboldal).
struct WebRequest {
    int id = 0;
    int remoteId = 0;               // az azonosítója a weboldalon
    int itemId = 0;
    QString title, artist;
    qint64 requestedAt = 0;         // unix mp
    QString status;                 // pending, queued, played, rejected, invalid, removed
    int entryId = 0;                // a playlist sora, amíg benne van
};

struct Entry {
    int id = 0;
    int playlistId = 0;
    int playlistKey = 0;            // ÉÉÉÉHHNNÓÓ – a sorrend ellenőrzéséhez
    QString source = "manual";      // "manual" vagy "scheduler"
    int fixedStartS = -1;           // fix kezdés az órán belül (mp), −1 = nincs
    int groupId = 0;                // kézzel készített playlist blokkja; 0 = nem tartozik blokkhoz
    QString groupName;
    bool overlay = false;           // az előtte és utána szóló elem átmenetére van ráfektetve
    qint64 overlayOffsetMs = 0;     // a kezdése a két elem közti mix ponthoz képest (negatív: előtte)
    double overlayDuckDb = 0;       // ennyivel halkul alatta a zene
    QString note;                   // felirat a sor előtt (az órasémák „Jegyzet” eleme); üres = nincs
    Item item;
};

// Heti rács (előbeállítás). Az 1-es mindig megvan; az üres név a kijelzésben „Alap”.
struct WeekGrid {
    int id = 0;
    QString name;
    int position = 0;
};

// Időszak, amíg egy heti rács érvényes (yearly: minden évben, csak a hónap és a nap számít; átnyúlhat az év végén).
struct WeekGridPeriod {
    int id = 0;
    int gridId = 0;
    QDate from, to;
    bool yearly = false;
    bool contains(const QDate &d) const
    {
        if (!yearly) return d >= from && d <= to;
        const int md = d.month() * 100 + d.day(), a = from.month() * 100 + from.day(), b = to.month() * 100 + to.day();
        return a <= b ? (md >= a && md <= b) : (md >= a || md <= b);
    }
};

// Kézzel készített, elmentett playlist (a Playlist szerkesztőből).
struct SavedPlaylist {
    int id = 0;
    int categoryId = 0;
    QString name, categoryName, color;
    int count = 0;
    qint64 durationMs = 0;
};

// Egyedi mix két szomszédos playlist-sor között.
struct Transition {
    int toEntryId = 0, fromEntryId = 0;   // toEntryId == 0: nincs ilyen
    qint64 mixOutMs = 0;
    qint64 outEndMs = 0;                  // a kimenő elem itt hallgat el (a saját idejében); 0 = a mix pont + kiúsztatás
    qint64 fadeOutMs = 0;                 // a vég előtti kiúsztatás hossza
    QString fadeOutCurve = "scurve";
    qint64 cueInMs = 0;
    qint64 fadeInMs = 0;
    double startLevelDb = 0;
    int vtItemId = 0;                     // voice track az átmenet fölött; 0 = nincs
    qint64 vtOffsetMs = 0;                // a kezdése a mix ponthoz képest (negatív: előtte)
    double vtDuckDb = -9;                 // ennyivel halkul alatta a zene
};

struct Clock {
    int id = 0;
    QString name, color;
};

struct ClockSlot {
    int id = 0;
    int clockId = 0;
    int position = 0;
    QString type = "category";      // "category", "item", "fill"
    int categoryId = 0;
    int itemId = 0;
    int count = 1;
    int fixedOffsetS = -1;          // −1 = folytatólagos
    int energyMin = 0, energyMax = 0;   // a választható elemek energiája; 0 = nincs korlát
    bool upToFit = false;           // „category”: legfeljebb count darab, de csak amennyi a következő fix pontig elfér
    QString note;                   // "note": a playlistben megjelenő felirat (nem szól, nem foglal időt)
    int sweeperCategoryId = 0;      // sweeper az ide vezető átmenetre ebből a kategóriából; 0 = nincs
    double sweeperDuckDb = -6;      // ennyivel halkul alatta a zene
    QString label;                  // megjelenítéshez: kategória vagy elem neve
    QString kind, color;
};

// Cartwall: színes, névvel ellátott gombfal; a gombjai (cartok) azonnal indítható hangok.
struct Cartwall {
    int id = 0;
    QString name, color;
};

struct Cart {
    int slot = -1;                  // a gomb helye a falon, 0-tól
    int itemId = 0;                 // hangtárelem; 0 = tallózott fájl
    QString path, label;
    qint64 durationMs = 0;
};

// Időzített esemény: egy hangtárelem vagy egy mentett playlist adott időpontban bekerül a playlistbe,
// vagy azonnal elindul. Egyszeri (adott napon) vagy hetente ismétlődő (a kijelölt napokon).
struct TimedEvent {
    int id = 0;
    bool enabled = true;
    int itemId = 0;                 // hangtárelem, vagy …
    int playlistId = 0;             // … mentett playlist (a kettő közül pontosan az egyik)
    QString action = "next";        // "next": következőként, "end": a playlist végére, "start": azonnal indul
    int timeS = 0;                  // a nap ennyiedik másodperce
    QDate date;                     // egyszeri esemény napja; érvénytelen: hetente ismétlődik
    int weekdays = 0;               // ismétlődő esemény napjai: 1. bit hétfő … 7. bit vasárnap
    qint64 lastFiredAt = 0;         // a legutóbbi teljesült alkalom (Unix idő), hogy ne fusson kétszer
    QString label;                  // megjelenítéshez: az elem címe vagy a playlist neve
};

// A rendszeróra; fejlesztői teszthez eltolható.
namespace eter {
QDateTime now();
void setClockOffsetMs(qint64 ms);
}

struct LogRow {
    qint64 startedAt = 0, endedAt = 0;
    QString kind, title, artist, mode, endReason;
    int player = 0;
    int itemId = 0;                 // 0: az elem azóta törölve
};

// A típusok (kind) felirata a felületen
QString kindLabel(const QString &kind);
QStringList kindKeys();

class Database : public QObject {
    Q_OBJECT
public:
    explicit Database(QObject *parent = nullptr);
    ~Database() override;

    static constexpr int kSchemaVersion = 17;   // a legújabb séma (PRAGMA user_version)
    bool open(const QString &path, QString *err = nullptr);
    QString path() const { return path_; }

    QString setting(const QString &key, const QString &fallback = {});
    void setSetting(const QString &key, const QString &value);

    // Kategóriák
    QVector<Category> categories();
    QVector<Category> itemCategories();              // a hangtár kategóriái (a playlist típusúak nélkül)
    QVector<Category> playlistCategories();          // csak a playlist típusúak
    int addCategory(const QString &name, const QString &kind, const QString &color, QString *err = nullptr);
    bool updateCategory(int id, const QString &name, const QString &kind, const QString &color, QString *err = nullptr);
    bool deleteCategory(int id, QString *err = nullptr);

    // Hangtár
    int itemIdByPath(const QString &path);              // 0 = nincs
    int addItem(const Item &item, QString *err = nullptr);
    QVector<Item> items();
    Item item(int id);
    int itemCount();
    void setItemsCategory(const QVector<int> &ids, int categoryId);
    // Az elem szerkesztőjéhez: album és év (ezek nincsenek az Item-ben), az adatok és a jelölőpontok mentése.
    void itemExtra(int id, QString *album, int *year);
    void updateItemDetails(int id, int categoryId, const QString &title, const QString &artist, const QString &album, int year);
    bool updateStream(int id, const QString &url, qint64 durationMs, QString *err = nullptr);   // élő stream elem
    // Energia, tempó és napszakok (az ütemező szabályaihoz); negatív érték: az a mező marad.
    void setItemFlow(const QVector<int> &ids, int energy, double bpm, int dayparts);
    void setItemKey(const QVector<int> &ids, int key);   // 0 = nincs megadva
    void setItemVocal(const QVector<int> &ids, int vocal);   // core/Vocal.h; 0 = nem ismert
    // Online adatok gyorsítótára (TheAudioDB). found: 1 megvan, 0 nincs ilyen; fetchedAt: unix mp.
    struct OnlineMeta {
        bool known = false, found = false;
        QString data;
        qint64 fetchedAt = 0;
    };
    OnlineMeta onlineMeta(const QString &key);
    void setOnlineMeta(const QString &key, bool found, const QString &data);
    QHash<QString, qint64> onlineMetaTimes(const QString &prefix, bool found);   // kulcs → mikor kérdeztük
    void clearOnlineMeta();
    // Az album és az év, csak ha még üres (a kézzel vagy a címkéből kapott adatot nem írja felül).
    void fillAlbumYear(int itemId, const QString &album, int year);
    QHash<int, QPair<QString, int>> albumYears();             // elem → (album, év); üres/0 = nincs
    // Háttérelemzés (tempó, hangnem): a még meg nem nézett zenék, a számlálók és az eredmény mentése.
    QVector<AnalysisJob> analysisJobs(int version, int limit);
    void analysisCounts(int version, int *pending, int *total);
    void setItemAnalysis(int itemId, int version, double bpm, int key);
    void resetAnalysis();
    // introMs: 0 = nincs; outroMs (az outro kezdete, vagyis a mix pont): 0 = automatikus.
    void setItemMarkers(int id, qint64 cueInMs, qint64 introMs, qint64 outroMs);
    bool deleteItem(int id, QString *err = nullptr);
    void setItemDuration(int id, qint64 ms);
    void setItemAutoMix(int id, qint64 mixOutMs);
    void beginBatch();
    void endBatch();
    void notifyItemsChanged() { emit itemsChanged(); }

    // Számkérések a weboldalról
    int addWebRequest(const WebRequest &r);     // 0: már megvan (ugyanaz a távoli azonosító)
    WebRequest webRequestByRemote(int remoteId);
    QVector<WebRequest> webRequests(const QStringList &statuses = {});
    void setWebRequestStatus(int id, const QString &status, int entryId = -1);

    // Playlist
    int playlistFor(const QDate &date, int hour, int *key = nullptr);
    Entry addEntry(int playlistId, int itemId, const QString &source = "manual", int fixedStartS = -1,
                   int groupId = 0, const QString &groupName = {});
    int nextGroupId();
    void setEntryGroup(int entryId, int groupId, const QString &groupName);
    void setEntryOverlay(int entryId, bool on, qint64 offsetMs = 0, double duckDb = 0);
    void setEntryNote(int entryId, const QString &note);

    // Cartwall
    QVector<Cartwall> cartwalls();
    int addCartwall(const QString &name, const QString &color);
    void updateCartwall(int id, const QString &name, const QString &color);
    void deleteCartwall(int id);
    QVector<Cart> carts(int wallId);
    void setCart(int wallId, const Cart &cart);
    void clearCart(int wallId, int slot);

    // Időzített események
    QVector<TimedEvent> timedEvents();
    int saveTimedEvent(const TimedEvent &e);            // id == 0: új; visszaadja az azonosítót
    void deleteTimedEvent(int id);
    void markEventFired(int id, qint64 occurrence, bool disable);

    // Mentett playlistek
    QVector<SavedPlaylist> savedPlaylists();
    QVector<Item> savedPlaylistItems(int playlistId);
    // id == 0: új; visszaadja az azonosítót, hiba esetén 0-t.
    int saveSavedPlaylist(int id, int categoryId, const QString &name, const QVector<int> &itemIds, QString *err = nullptr);
    void deleteSavedPlaylist(int id);
    bool playlistGenerated(int playlistId);
    void markGenerated(int playlistId, int clockId);
    // A megadott órától kezdve törli az ütemező még várakozó sorait (újrageneráláshoz).
    void deleteSchedulerEntriesAt(int key);       // egy óra (ÉÉÉÉHHNNÓÓ) várakozó ütemezett sorai
    void deleteSchedulerEntriesFrom(int fromKey);
    // A megadott óra előtti, le nem játszott ütemezett sorok törlése.
    void purgeSchedulerEntriesBefore(int beforeKey);

    // Ütemező: órasémák és heti rács
    QVector<Clock> clocks();
    int addClock(const QString &name, const QString &color, QString *err = nullptr);
    bool updateClock(int id, const QString &name, const QString &color, QString *err = nullptr);
    void deleteClock(int id);
    int duplicateClock(int id, const QString &newName);
    QVector<ClockSlot> clockSlots(int clockId);
    int addSlot(const ClockSlot &slot);
    void updateSlot(const ClockSlot &slot);
    void deleteSlot(int slotId);
    void reorderSlots(int clockId, const QVector<int> &slotIds);
    QHash<int, int> weekGrid();                          // az aktív rács (ma érvényes): nap*100+óra → séma
    QHash<int, int> weekGrid(int gridId);
    int clockAt(const QDate &date, int hour);
    bool hasWeekGrid();                                  // van-e bármelyik rácsban beosztott óra            // az adott órára érvényes rács sémája; 0 = nincs
    // Heti rácsok: kézzel választott aktív rács (active_week_grid), amit egy időszak felülírhat.
    QVector<WeekGrid> weekGrids();
    int addWeekGrid(const QString &name, int copyFrom = 0);
    void renameWeekGrid(int id, const QString &name);
    bool deleteWeekGrid(int id);                         // az utolsó rács nem törölhető
    int activeWeekGrid();                                // a kézzel választott (alapból az 1-es)
    void setActiveWeekGrid(int id);
    int gridFor(const QDate &date);                      // az adott napon érvényes rács
    QVector<WeekGridPeriod> weekGridPeriods();
    int addWeekGridPeriod(const WeekGridPeriod &p);
    void updateWeekGridPeriod(const WeekGridPeriod &p);
    void deleteWeekGridPeriod(int id);
    static QString gridDisplayName(const WeekGrid &g);
    void setGrid(int weekday, int hour, int clockId);    // az aktív rácsba; clockId 0 = törlés
    void setGrid(int gridId, int weekday, int hour, int clockId);
    void setEntryStatus(int entryId, const QString &status);
    void deleteEntry(int entryId);
    void saveOrder(const QVector<QPair<int, int>> &entryAndPlaylist);
    QVector<Entry> pendingEntries();
    bool itemInPlaylist(int itemId);

    // Mix
    Transition transition(int toEntryId);
    void saveTransition(const Transition &t);
    void deleteTransition(int toEntryId);
    QHash<int, int> transitionPairs();                   // bejövő sor → kimenő sor
    QVector<Transition> transitionsFor(const QVector<int> &toEntryIds);

    // Távoli szerkesztő: a playout adatainak helyi tükre (a playout azonosítóival).
    struct MirrorRow {
        Entry entry;
        QString status;                                  // pending, playing, played, skipped
    };
    bool mirrorLibrary(const QVector<Category> &categories, const QVector<Item> &items);
    bool mirrorUpsertItem(const Item &item, const Category &category);
    bool mirrorState(const QVector<MirrorRow> &rows, const QVector<Transition> &transitions);
    // Ha be van állítva, a Mix editor mentései (átmenet, a számok jelölői) ide is jelzik magukat:
    // a távoli szerkesztő így küldi tovább őket a playoutnak.
    std::function<void(const QString &op, const QJsonObject &args)> remoteHook;
    void setItemMixDefaults(int itemId, qint64 mixOutMs, qint64 fadeOutMs, const QString &curve, qint64 cueOutMs);
    void clearItemMixDefaults(int itemId);
    void setItemCueIn(int itemId, qint64 cueInMs);
    void setItemIntro(int itemId, qint64 introMs);
    void setItemVolumeEnvelope(int itemId, const QString &envelope);   // üres: törlés
    int voiceTrackCategory();            // az első voice track típusú kategória; ha nincs, létrehozza

    // Adásnapló
    int logStart(const Item &item, int entryId, int player, const QString &mode);
    void logEnd(int logId, const QString &reason);
    QVector<LogRow> log(int limit);
    struct TopRow { int itemId = 0; QString title, artist; int plays = 0; };
    // A legtöbbet játszott zenék a megadott időpont (unix mp) óta, a játszások száma szerint csökkenően.
    QVector<TopRow> topPlayed(qint64 since, int limit);

signals:
    void categoriesChanged();
    void itemsChanged();
    void clocksChanged();
    void savedPlaylistsChanged();
    void timedEventsChanged();
    void weekGridsChanged();

private:
    bool createSchema(QString *err);
    void housekeeping();
    QString path_;
};
