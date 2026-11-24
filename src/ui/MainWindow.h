#pragma once
#include "net/WebSync.h"
#include "audio/SmartMix.h"
#include "audio/Engine.h"
#include "db/Database.h"
#include "sched/Scheduler.h"

#include <QColor>
#include <QDateTime>
#include <QMainWindow>
#include <QPointer>
#include <QHash>
#include <QSet>
#include <QVector>
#include <memory>

class QAction;
class QComboBox;
class QDialog;
class QFrame;
class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class QToolButton;
class PlayerWidget;
class MeterWidget;
class SettingsDialog;
class LibraryPanel;
class PlaylistTable;
class SchedulerPage;
class MixEditorPage;
class PlaylistEditorPage;
class CartwallPanel;
class ItemEditorDialog;
class TimedEventsPage;
class MiniMix;
class CountdownOverlay;
class FileCheck;
class ProgramLogger;
namespace remote { class Server; class Connection; class Client; }
class QJsonObject;
class QPainter;
class QStyleOptionViewItem;
class QStackedWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    enum Mode { Assist, Auto };

    MainWindow(Database *db, const QString &backendOverride = {}, QWidget *parent = nullptr);
    ~MainWindow() override;

    // A Beállítások ablak is ezeket használja.
    eter::Engine *engine() { return &engine_; }
    QStringList deviceNames();
    // ch: az Engine csatornái (Player1, Player2, Player3, Pfl), illetve kCartRoute a cartwall.
    // A cartwallnál a "@player1" azt jelenti, hogy az A lejátszó kimenetét követi.
    static constexpr int kCartRoute = 3;
    static constexpr int kPlayers = 3;
    static eter::Engine::Channel playerChannel(int i)
    {
        return i == 0 ? eter::Engine::Player1 : i == 1 ? eter::Engine::Player2 : eter::Engine::Player3;
    }
    static QString playerLetter(int i) { return QString(QChar('A' + i)); }
    static QColor playerColor(int i);
    // Átmenetek: automatikus mix (zenék között) vagy a Mix editorban mentett beállítások.
    bool autoMix() const { return autoMix_; }
    void reloadMixSettings();   // az automatikus mix beállításai megváltoztak (Beállítások)
    class ChannelMixer *mixer() const { return mixer_; }
    class MixdownPage *mixdownPage() const { return mixdownPage_; }
    void applyMixerGains();
    void setAutoMix(bool on);
    QString routeOf(int ch) const { return ch == kCartRoute ? cartRoute_ : routes_[ch]; }
    QString backendSetting() const { return backend_; }
    bool channelBusy(int ch);
    bool anythingPlaying();
    bool changeRoute(int ch, const QString &deviceName);
    bool changeBackend(const QString &backend);
    void refreshDeviceLists();

    // Elemek hozzáadása a playlisthez; at < 0: a végére.
    void appendItems(const QVector<int> &itemIds, int at = -1, int groupId = 0, const QString &groupName = {},
                     const QString &source = QStringLiteral("manual"));
    // Mentett playlist beillesztése blokként; at < 0: a végére.
    void insertSavedPlaylist(int savedPlaylistId, int at = -1);
    void toggleGroup(int groupId);
    PlaylistEditorPage *playlistPage() { return plPage_; }
    CartwallPanel *cartwall() { return cartPanel_; }
    // A hangtárelem szerkesztője; itemId <= 0: a kijelölt elem.
    ItemEditorDialog *openItemEditor(int itemId = 0);
    void showLeft(int view);          // 0: playlist, 1: cartwall
    PlaylistTable *playlistTable() { return table_; }
    TimedEventsPage *eventsPage() { return eventsPage_; }
    bool following() const { return follow_; }
    void expandStandbyTransition();       // fejlesztői ellenőrzéshez: a készenlétben álló sor átmenetének lenyitása
    bool miniOpen() const;
    class MiniMixCanvas *miniCanvas();
    void setMode(Mode mode);
    SchedulerPage *schedulerPage() { return schedPage_; }
    MixEditorPage *mixPage() { return mixPage_; }
    void runScheduleRules() { applySchedule(); }
    void moveRow(int from, int to, bool outsideBlock = false);   // outsideBlock: a blokk fejléce elé ejtve
    LibraryPanel *library() { return library_; }
    QString debugState() const;         // fejlesztői ellenőrzéshez
    QString playerViewDebug() const;    // a látható lejátszó, a sáv és a Következő gomb (fejlesztéshez)
    QString rowStatusDebug() const;     // a sorok állapotfelirata (fejlesztéshez)

public slots:
    void next();

signals:
    void devicesChanged();

protected:
    void closeEvent(QCloseEvent *) override;

private:
    struct Row {
        int id = 0;                       // playlist_entries.id
        int playlistId = 0, playlistKey = 0;
        Item item;
        qint64 frames = 0;
        enum State { Pending, Cued, Playing, Played, Skipped, Failed } state = Pending;
        int player = -1;
        QDateTime startedAt;              // mikor került ténylegesen adásba
        int logId = 0;
        bool scheduled = false;           // az ütemező tette be
        QDateTime fixedAt;                // fix kezdési időpont, ha van
        int groupId = 0;                  // kézzel készített playlist blokkja; 0 = nincs
        QString groupName;
        // Ráfektetés: az elem nem a lejátszókon szól, hanem az előtte és utána álló elem átmenete fölött,
        // egy külön csatornán; a kezdése a két elem közti mix ponthoz képest.
        bool overlay = false;
        qint64 overlayOffsetMs = 0;
        double overlayDuckDb = 0;
        int overlayVoice = -1;            // szóló ráfektetett elem csatornája (0 vagy 1), különben −1
    };
    // A táblázat egy sora: playlist-sor, vagy egy blokk fejléce (row < 0; first/count: a blokk sorai).
    // Item: playlist-sor (row); Header: blokk fejléce (first, count); Hour: óraelválasztó (first: az óra első sora);
    // Wave: a fölötte lévő sor (row) lenyitott mini mix editora.
    struct ViewRow {
        enum Kind { Item, Header, Hour, Wave };
        int row = -1, first = 0, count = 0;
        Kind kind = Item;
        int hourKey = 0;
    };
    QVector<ViewRow> view_;
    QSet<int> expanded_;                  // a kinyitott blokkok
    QVector<QString> startText_;          // a sorok „Kezdés" szövege
    void rebuildView();
    int insertIndexForView(int viewRow) const;
    int selectedHeaderRowId() const;      // a kijelölt blokkfejléc első sorának azonosítója, vagy −1
    void selectRowId(int rowId);
    void onViewRowMoved(int from, int to);
    void moveBlock(int first, int count, int to);
    void ungroup(int first, int count);   // a sorok kikerülnek a blokkból (a playlistben maradnak)
    void playlistContextMenu(const QPoint &pos);
    qint64 lastToggleMs_ = 0;
    Row makeRow(const Entry &e) const;
    void syncRowsFromDb();
    void applySchedule();
    void autoGenerate();
    void trimHistory();
    void showPage(int page);

    // Egy átmenet érvényes beállításai adáshoz: egyedi mix, a számok alapértelmezése vagy automatikus.
    struct MixPlan {
        qint64 mixOutFrames = -1;         // −1: még nem ismert
        int fadeOutMs = -1;               // −1: az általános kiúsztatás
        qint64 outEndFrames = -1;         // a kimenő elem beállított vége; −1: a mix ponttól úszik ki
        eter::FadeCurve curve = eter::FadeCurve::Quadratic;
        qint64 cueInFrames = -1;          // −1: a szám saját kezdőpontja
        int fadeInMs = 0;
        double startLevelDb = 0;
        int vtItemId = 0;                 // voice track az átmenet fölött
        qint64 vtOffsetMs = 0;
        double vtDuckDb = -9;
        bool nonMusic = false;            // nem zenei elem saját beállítás nélkül: a mix pontnál nem halkul
    };
    // A kimenő elem időzített vége: a mix pont után tovább szól, és a beállított végpontján úszik ki.
    struct OutFade { int rowId = -1; qint64 startFrame = 0; qint64 endFrame = 0; int fadeMs = 0;
                     eter::FadeCurve curve = eter::FadeCurve::Quadratic; };
    OutFade outFade_[kPlayers];
    int handedOver_[kPlayers] = {-1, -1, -1};   // ennek a sornak már elindult a következője (tovább szólhat)
    bool leaving(int player) const;
    bool soundingOn(int player) const;   // átadva, de teljes hangerővel szól tovább       // kiúszik, vagy már elindult utána a következő
    void runOutFades();
    void runVoiceTrack();
    void startVoiceTrack(int itemId, double duckDb);
    void endVoiceTrack(const QString &reason);
    MixPlan planFor(int outPlayer, int inPlayer);
    QString rowPlayerStatus(const Row &row) const;   // „ADÁSBAN · B”, „KIÚSZIK · A”, „KÉSZENLÉT · C”
    int autoMixMode(const Row *from, const Row *to, bool hasSaved) const;
    MixPlan planForBase(int outPlayer, int inPlayer);
    void guardDeadAir(const float *playerPeak, float otherPeak);
    bool deadAirOn_ = true;
    int deadAirMs_ = 3000;
    qint64 deadSince_ = 0;
    bool autoMixApplies(const Row *from, const Row *to) const;   // automatikus mix érvényes erre a párra
    QSet<int> mixOff_;                    // ezekből a sorokból a mentett mix most nem érvényes (automatikus mix)
    void updateMixPairs();
    void onMixSaved(const QVector<int> &itemIds);

    void buildUi();
    void buildMenus();
    QWidget *buildDeck();
    QWidget *buildPlaylistPanel();
    QWidget *buildPflBar();

    void startAudio();
    void tick();
    void updateClock();
    void stopAll();
    void openSettings();
    void openLog();
    void about();

    Row *rowById(int id);
    int indexOfRow(int id) const;
    int selectedRowId() const;
    int firstMovable() const;
    void refreshTable();
    void updateFlowIssues();
    QHash<int, QString> flowIssue_;       // playlist-sor → miért nem illik az előző zenéhez (tempó, hangnem)
    void updateStartTimes();
    void persistOrder();
    void reconcile();
    bool loadIntoPlayer(Row &row, int player);
    void onPlayerFinished(int player);
    void onPlayerEjected(int player);
    void syncRowStates();
    void removeSelected();
    void clearList();
    void startStandby(int fadeOutPlayer, int fadeMs);
    int standbyPlayer() const;
    int onAirPlayer() const;              // az adásban lévő (nem kiúszó) lejátszó; több közül a későbbi elem
    bool standbyIsEarlyFixed();
    void togglePfl();
    void stopPfl();
    void say(const QString &msg, int ms = 6000);

    Database *db_;
    eter::Engine engine_;
    QString backend_;
    QString routes_[eter::Engine::ChannelCount];
    QString cartRoute_ = QStringLiteral("@player1");
    bool applyCartRoute(std::string *err = nullptr);
    Mode mode_ = Assist;
    int fadeMs_ = 2500;

    PlayerWidget *players_[kPlayers] = {nullptr, nullptr, nullptr};
    QStackedWidget *playerStack_ = nullptr;   // a főablakban egyszerre egy lejátszó látszik
    class PlayerStrip *strip_ = nullptr;      // az A/B/C állapota és a voice track
    int shownPlayer_ = 0;
    int pinnedPlayer_ = -1, pinnedOnAir_ = -2;               // a sávon kiválasztott lejátszó (egy ideig az látszik)
    qint64 pinnedAt_ = 0;
    void updatePlayerView();
    QPushButton *toPlayer_[kPlayers] = {nullptr, nullptr, nullptr};
    bool autoMix_ = true;
    eter::SmartMixSettings smart_;
    class ChannelMixer *mixer_ = nullptr;
    class MixdownPage *mixdownPage_ = nullptr;
    float vtPeak_[2] = {0.f, 0.f}, ovlPeak_[2] = {0.f, 0.f};   // a voice track és a ráfektetett elemek csúcsa (a mixerhez)
    QToolButton *mixAuto_ = nullptr, *mixEditor_ = nullptr;
    int vtItemShown_ = 0;
    QString vtTitle_;
    double vtDuckDbShown_ = 0;
    QVector<Row> rows_;

    PlaylistTable *table_ = nullptr;
    LibraryPanel *library_ = nullptr;
    QLabel *listInfo_ = nullptr, *listHint_ = nullptr;
    QLabel *clock_ = nullptr, *date_ = nullptr, *toHour_ = nullptr, *masterDb_ = nullptr;
    MeterWidget *master_ = nullptr;
    QPushButton *nextBtn_ = nullptr, *pflBtn_ = nullptr;
    QToolButton *modeAuto_ = nullptr, *modeAssist_ = nullptr;
    QAction *autoAction_ = nullptr;
    QLabel *pflTitle_ = nullptr;
    QProgressBar *pflProgress_ = nullptr;
    QComboBox *pflOutput_ = nullptr;
    QLabel *audioStatus_ = nullptr, *dbStatus_ = nullptr;
    QTimer *timer_ = nullptr;
    bool pflActive_ = false;
    bool lastSelectionInLibrary_ = false;
    int clockDivider_ = 0;
    QDateTime chainBase_;                 // a várakozó elemek becsült kezdésének alapja
    QPointer<SettingsDialog> settings_;
    QPointer<QDialog> logDialog_;
    QPointer<ItemEditorDialog> itemEditor_;
    Scheduler scheduler_;
    SchedulerPage *schedPage_ = nullptr;
    QStackedWidget *stack_ = nullptr;
    QWidget *schedWindow_ = nullptr, *mixWindow_ = nullptr;   // az Ütemező és a Mix editor külön ablaka
    QWidget *schedHost_ = nullptr, *mixHost_ = nullptr;       // a helyük a főablakban (nagy képernyőn)
    bool separatePageWindows() const;
    QWidget *makePageWindow(QWidget *page, const QString &title, const QString &key);
    QToolButton *tabLive_ = nullptr, *tabSched_ = nullptr, *tabMix_ = nullptr, *tabEvents_ = nullptr, *tabMixdown_ = nullptr;
    TimedEventsPage *eventsPage_ = nullptr;
    QVector<TimedEvent> events_;
    void runTimedEvents();
    void fireTimedEvent(const TimedEvent &e);
    int pendingStartRowId_ = -1;          // időzítve indítandó sor, amint készenlétbe kerül
    QDateTime pendingStartUntil_;
    // A playlist követi az adásban lévő elemet, amíg kézzel el nem görgetik róla.
    bool follow_ = true, programmaticScroll_ = false;
    int followedRowId_ = -1;
    int followTargetView() const;          // az adásban lévő (ennek híján a készenlétben álló) elem sora a táblázatban
    bool followTargetVisible() const;
    void followPlaying();
    // Lenyitható mini mix editor: a sor és a következő elem átmenete.
    int expandedRowId_ = -1;
    QPointer<MiniMix> mini_;
    int miniView_ = -1;
    int nextPlayableAfter(int index) const;
    void toggleMini(int rowId);
    void updateMini();
    void placeMini();                                      // a mini szerkesztő a lenyitott sor helyére
    QSet<int> customFrom_, vtFrom_;       // azok a sorok, amelyekből egyedi mix (illetve voice track) visz a következőbe
public:
    void paintPlaylistRow(QPainter *p, const QStyleOptionViewItem &opt, int v) const;
private:
    MixEditorPage *mixPage_ = nullptr;
    PlaylistEditorPage *plPage_ = nullptr;
    CartwallPanel *cartPanel_ = nullptr;
    QStackedWidget *leftStack_ = nullptr;
    QVector<QToolButton *> viewButtons_[2];
    QWidget *makeViewSwitch();
    QToolButton *tabPl_ = nullptr;
    int mixVersion_ = 0, planVersion_ = -1, planFrom_ = -2, planTo_ = -2;
    Transition planTransition_;
    bool vtPlaying_ = false;
    int vtLogId_ = 0;
    int vtDoneTo_ = -1;                   // ehhez a bejövő sorhoz a voice track már elindult
    struct { int toRowId = -1; int player = -1; qint64 startFrame = 0; int itemId = 0; double duckDb = -9; } vtPending_;
    double vtDuckGain_ = 1.0;
    // Ráfektetett elemek
    struct OverlayVoice { int rowId = -1; int logId = 0; double duckGain = 1.0; };
    OverlayVoice ovl_[eter::Engine::kOverlayVoices];
    bool overlayTargets(int index, int *a, int *c) const;   // aktív ráfektetés: az előtte (a) és utána (c) álló elem
    bool overlayActive(int index) const { return overlayTargets(index, nullptr, nullptr); }
    void runOverlays();
    bool startOverlay(Row &row);
    void endOverlay(int voice, const QString &reason);
    void applyDuck();
public:
    // A sor ráfektetése az előtte és utána álló elem átmenetére (vagy a ráfektetés megszüntetése).
    bool setRowOverlay(int rowId, bool on, qint64 offsetMs = 0, double duckDb = 0);
    void loadRowInto(int rowId, int player);           // kézi betöltés egy lejátszóba
    // Távoli szerkesztés: a beállítások szerint elindítja vagy leállítja a szolgáltatást.
    void configureRemote();
    QString remoteStatus() const;
    QJsonObject remoteState() const;                   // a playlist élő állapota a szerkesztőknek
    QJsonObject remoteLibrary();                       // a hangtár a szerkesztőknek
    // Üres: sikerült; különben a hiba szövege. c: a kérő kapcsolat (a jogai miatt); reply: a válasz további mezői.
    QString applyRemoteOp(const QJsonObject &m, remote::Connection *c = nullptr, QJsonObject *reply = nullptr);
    QString applyUpload(const QJsonObject &m, remote::Connection *c, QJsonObject *reply);
public:
    static QString uploadFolder();
private:
private:
    CountdownOverlay *countdown_ = nullptr;
    void updateCountdown();
public:
    QString countdownText() const;                     // fejlesztéshez: a visszaszámláló állapota
private:
    // Hiányzó fájlok: a várakozó elemek (és voice trackjeik) fájljainak ellenőrzése háttérszálon.
    FileCheck *fileCheck_ = nullptr;
    QSet<QString> missing_;                            // a playlist elérhetetlen fájljai
    QSet<int> missingItems_;                           // hangtárelemek elérhetetlen fájllal (az ütemező kihagyja őket)
    QTimer *fileCheckTimer_ = nullptr, *fileRecheckTimer_ = nullptr;
    QPushButton *missingBtn_ = nullptr;
    QPointer<QDialog> missingDialog_;
    QSet<int> warnedMissing_;                          // ezekre a sorokra már szólt
    void requestFileCheck();
    void onFilesChecked(const QStringList &checked, const QSet<QString> &missing);
    void updateMissingUi();
    bool rowMissing(const Row &r) const;
    void removeRows(const QVector<int> &rowIds);
public:
    void showMissingFiles();
    void scanLibraryFiles();
    QStringList missingTitles() const;                 // fejlesztéshez: a hiányzó fájlú várakozó sorok
    void checkFilesNow() { requestFileCheck(); }
private:
    // Pontos időre igazítás: a fix idejű elemekhez képest mennyit késik vagy siet a playlist.
    struct FixInfo {
        int rowId = 0;
        QDateTime fixedAt, arrive;                     // a fix időpont, és amikor a sor magától odaérne
        qint64 diffMs = 0;                             // > 0: ennyi hiányzik (csend lenne); < 0: ennyivel túllóg
    };
    QVector<FixInfo> fixes_;
    QFrame *fixBar_ = nullptr;
    QLabel *fixText_ = nullptr;
    QPushButton *fixBtn_ = nullptr;
    // Fix pontonként az automatikus igazítás állapota: hányszor igazított, mikor próbálta utoljára, és a fix pont
    // előtti sorok „lenyomata” – ha a playlist megváltozik (pl. kézzel törölnek belőle), azonnal újra megnézi.
    QHash<int, qint64> removedByHand_;   // elem → mikor vették ki kézzel a playlistből (a kitöltés ne tegye vissza)
    bool autoRemoving_ = false;          // az automatikus igazítás töröl (nem kézi)
    struct AutoFixState { int applied = 0; qint64 lastTry = 0, lastGapTry = 0; size_t sig = 0; qint64 sigAt = 0; };
    QHash<int, AutoFixState> autoFixed_;
    QPointer<QDialog> fixDialog_;
    void updateFixBar();
    void autoAdjustFixes();
    const FixInfo *nextFix() const;
    static qint64 rowAirMs(const Row &r);
public:
    // Igazítás a fix ponthoz: kimaradó sorok (a fix pont előtt, az ütemező tette be őket) és helyettük vagy
    // mellettük beszúrt elemek, hogy a fix elem a lehető legpontosabban érkezzen.
    struct AdjustPlan {
        QVector<int> remove;                           // playlist-sorok
        QVector<Item> add;                             // a fix elem elé
        qint64 afterMs = 0;                            // az eltérés utána (> 0: hiány, < 0: túllógás)
        qint64 cost = 0;
        bool changes() const { return !remove.isEmpty() || !add.isEmpty(); }
    };
    AdjustPlan planAdjust(int fixRowId, qint64 diffMs, int categoryId, bool allowRemove, const QSet<int> &avoidItems = {}) const;
    qint64 adjustCost(qint64 diffMs) const;            // mennyire rossz egy eltérés (a tervek összevetéséhez)
    bool adjustAcceptable(qint64 diffMs) const;        // a tűrésen belül van-e
    void applyAdjust(int fixRowId, const AdjustPlan &plan);
    QVector<Item> fillPool(int fixRowId, int categoryId, const QSet<int> &avoidItems = {}) const;   // beszúrható számok
    bool gapFillOne(int fixRowId, qint64 gapMs);       // hiány: egy szám, amely kitölti (kicsit túllóghat)
    QHash<int, QDateTime> plannedStart_;               // a várakozó sorok becsült kezdése
    int defaultFillCategory(int fixRowId) const;
    void openFixDialog();
    QString fixDebug() const;                          // fejlesztéshez
    void removeRowsForTest(const QVector<int> &rowIds) { removeRows(rowIds); }   // fejlesztéshez (kézi törlés)
    QString deleteBeforeFixForTest()   // fejlesztéshez: a következő fix pont előtti sor törlése, a címét adja vissza
    {
        const FixInfo *f = nextFix();
        const int at = f ? indexOfRow(f->rowId) : -1;
        if (at < 1) return {};
        const QString title = rows_[at - 1].item.title;
        removeRows({rows_[at - 1].id});
        return title;
    }
private:
    // Adásrögzítés (logger)
    std::unique_ptr<ProgramLogger> logger_;
    class NowPlaying *nowPlaying_ = nullptr;
    class LibraryAnalyzer *analyzer_ = nullptr;
    qint64 nowIdleSince_ = 0;              // mióta nem szól semmi (a rövid szünetek nem számítanak)
    void updateNowPlaying();
    QLabel *loggerStatus_ = nullptr;
    std::vector<float> tapBuf_;
    void pumpLogger();
public:
    void configureLogger();                            // a beállítások szerint indítja vagy leállítja
    QString loggerStatus() const;
    ProgramLogger *logger() const { return logger_.get(); }
    // „Most szól” kiírás (fájl, Icecast): a beállítások mentése után újraolvassa őket.
    void configureNowPlaying();
    // Adatbázis exportálása (konzisztens másolat) és importálása (ellenőrzés, biztonsági mentés, újraindítás).
    bool exportDatabase(QWidget *parent);
    void importDatabase(QWidget *parent);
    QString databaseSummary() const;
    Database *database() const { return db_; }
    // A program indulásakor: ha importálásra váró fájl van, az lép az adatbázis helyére. Üres: rendben (vagy nem volt).
    static QString applyPendingImport(const QString &dbPath);
    // A párbeszédablakok nélküli lépések (a tesztek is ezeket hívják).
    bool exportDatabaseTo(const QString &target, QString *err = nullptr);
    bool stageDatabaseImport(const QString &source, QString *err = nullptr, QString *backupPath = nullptr);
    void relaunch(const QStringList &args);
    QString nowPlayingStatus() const;
    class NowPlaying *nowPlaying() const { return nowPlaying_; }
    class LibraryAnalyzer *analyzer() const { return analyzer_; }

    // Weboldal: „Most szól”, „Következő számok” és számkérés (Beállítások → Weboldal). MainWindowRequests.cpp
    void configureWeb();
    class WebSync *webSync() const { return web_; }
    void openRequestsDialog();
    void approveRequest(int requestId);            // web_requests.id
    void rejectRequest(int requestId);
    int pendingRequestCount() const;
    bool isRequestRow(int entryId) const { return requestEntries_.contains(entryId); }
    // fejlesztéshez: a weboldalról érkezett kérések feldolgozása hálózat nélkül
    void injectRequestsForTest(const QVector<std::pair<int, int>> &remoteAndItem);
private:
    class WebSync *web_ = nullptr;
    WebSync::Track webNow_;
    bool webHasNow_ = false;
    QSet<int> requestEntries_;                     // a kérésként betett playlist-sorok
    QToolButton *requestsBtn_ = nullptr;
    QPointer<QDialog> requestsDlg_;
    QTimer *requestTimer_ = nullptr;
    void initWeb();
    void updateWeb();
    void onWebRequests(const QVector<std::pair<int, int>> &remoteAndItem);
    bool queueRequest(const WebRequest &r);
    int requestInsertIndex() const;
    void checkRequests();
    void updateRequestsButton();
    void refreshRequestsDialog();

    remote::Server *remote_ = nullptr;
    // Feltöltések a szerkesztőkből: azonosító → a készülő fájl
    struct Upload {
        QPointer<remote::Connection> conn;
        std::shared_ptr<class QFile> file;
        QString path, name, title;
        qint64 size = 0, received = 0;
        int categoryId = 0;
        bool voiceTrack = false;
    };
    QHash<int, Upload> uploads_;
    int nextUpload_ = 1;
    // Tartalék playout: a fő gép oldalán az adatbázis-másolat küldése, a tartalék oldalán az átvétel és a visszaadás.
    QTimer *snapshotTimer_ = nullptr;
    QJsonObject remoteSnapshot();
    void sendSnapshot();
    QString applyHandback(const QJsonObject &m);
    remote::Client *origin_ = nullptr;                // tartalékként adásban: a fő playout figyelése
    QFrame *handbackBar_ = nullptr;
    QLabel *handbackText_ = nullptr;
    QString originHost_;
    QStringList relaunchArgs_;
public:
    // A tartalék átveszi az adást: a megadott sor a megadott helyről szól (ha még tart), automata üzemmódban.
    void takeOver(int entryId, qint64 positionMs);
    // A fő playout figyelése az átvétel után; ha újra elérhető, a Visszaadás gomb visszaadja neki az adást.
    void watchOrigin(const QString &host, quint16 port, const QString &user, const QByteArray &salt, const QByteArray &key,
                     const QStringList &relaunchArgs);
    void handBack();
private:
    QTimer *remoteStateTimer_ = nullptr, *remoteLibTimer_ = nullptr;
    int remoteTick_ = 0;
    QString remoteError_;
    QLabel *schedStatus_ = nullptr;
    int lastGenerateMinute_ = -1;
    bool waitingForFixed_ = false;
};
