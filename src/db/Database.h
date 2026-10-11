// SQLite adatbázis: hangtár, kategóriák, playlist, adásnapló.
#pragma once
#include <QDate>
#include <QDateTime>
#include <QHash>
#include <QObject>
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
    // Ennyi ideig tölti ki az adást: a mix pontig, ennek híján a teljes hossz egy becsült átfedéssel.
    qint64 airMs() const { return mixOutMs > 0 ? mixOutMs : std::max<qint64>(0, durationMs - (durationMs > 30000 ? 3000 : 0)); }
};

struct Entry {
    int id = 0;
    int playlistId = 0;
    int playlistKey = 0;            // ÉÉÉÉHHNNÓÓ – a sorrend ellenőrzéséhez
    QString source = "manual";      // "manual" vagy "scheduler"
    int fixedStartS = -1;           // fix kezdés az órán belül (mp), −1 = nincs
    int groupId = 0;                // kézzel készített playlist blokkja; 0 = nem tartozik blokkhoz
    QString groupName;
    Item item;
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

// A rendszeróra; fejlesztői teszthez eltolható.
namespace eter {
QDateTime now();
void setClockOffsetMs(qint64 ms);
}

struct LogRow {
    qint64 startedAt = 0, endedAt = 0;
    QString kind, title, artist, mode, endReason;
    int player = 0;
};

// A típusok (kind) felirata a felületen
QString kindLabel(const QString &kind);
QStringList kindKeys();

class Database : public QObject {
    Q_OBJECT
public:
    explicit Database(QObject *parent = nullptr);
    ~Database() override;

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
    // introMs: 0 = nincs; outroMs (az outro kezdete, vagyis a mix pont): 0 = automatikus.
    void setItemMarkers(int id, qint64 cueInMs, qint64 introMs, qint64 outroMs);
    bool deleteItem(int id, QString *err = nullptr);
    void setItemDuration(int id, qint64 ms);
    void setItemAutoMix(int id, qint64 mixOutMs);
    void beginBatch();
    void endBatch();
    void notifyItemsChanged() { emit itemsChanged(); }

    // Playlist
    int playlistFor(const QDate &date, int hour, int *key = nullptr);
    Entry addEntry(int playlistId, int itemId, const QString &source = "manual", int fixedStartS = -1,
                   int groupId = 0, const QString &groupName = {});
    int nextGroupId();
    void setEntryGroup(int entryId, int groupId, const QString &groupName);

    // Cartwall
    QVector<Cartwall> cartwalls();
    int addCartwall(const QString &name, const QString &color);
    void updateCartwall(int id, const QString &name, const QString &color);
    void deleteCartwall(int id);
    QVector<Cart> carts(int wallId);
    void setCart(int wallId, const Cart &cart);
    void clearCart(int wallId, int slot);

    // Mentett playlistek
    QVector<SavedPlaylist> savedPlaylists();
    QVector<Item> savedPlaylistItems(int playlistId);
    // id == 0: új; visszaadja az azonosítót, hiba esetén 0-t.
    int saveSavedPlaylist(int id, int categoryId, const QString &name, const QVector<int> &itemIds, QString *err = nullptr);
    void deleteSavedPlaylist(int id);
    bool playlistGenerated(int playlistId);
    void markGenerated(int playlistId, int clockId);
    // A megadott órától kezdve törli az ütemező még várakozó sorait (újrageneráláshoz).
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
    QHash<int, int> weekGrid();                          // nap*100+óra → séma
    void setGrid(int weekday, int hour, int clockId);    // clockId 0 = törlés
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
    void setItemMixDefaults(int itemId, qint64 mixOutMs, qint64 fadeOutMs, const QString &curve, qint64 cueOutMs);
    void clearItemMixDefaults(int itemId);
    void setItemCueIn(int itemId, qint64 cueInMs);
    void setItemIntro(int itemId, qint64 introMs);
    int voiceTrackCategory();            // az első voice track típusú kategória; ha nincs, létrehozza

    // Adásnapló
    int logStart(const Item &item, int entryId, int player, const QString &mode);
    void logEnd(int logId, const QString &reason);
    QVector<LogRow> log(int limit);

signals:
    void categoriesChanged();
    void itemsChanged();
    void clocksChanged();
    void savedPlaylistsChanged();

private:
    bool createSchema(QString *err);
    void housekeeping();
    QString path_;
};
