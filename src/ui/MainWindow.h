#pragma once
#include "audio/Engine.h"
#include "db/Database.h"
#include "sched/Scheduler.h"

#include <QDateTime>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QVector>

class QAction;
class QComboBox;
class QDialog;
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
    // ch: 0–2 a lejátszók és az előhallgatás, 3 a cartwall. A cartwallnál a "@player1" azt jelenti,
    // hogy az 1-es lejátszó kimenetét követi.
    static constexpr int kCartRoute = 3;
    QString routeOf(int ch) const { return ch == kCartRoute ? cartRoute_ : routes_[ch]; }
    QString backendSetting() const { return backend_; }
    bool channelBusy(int ch);
    bool anythingPlaying();
    bool changeRoute(int ch, const QString &deviceName);
    bool changeBackend(const QString &backend);
    void refreshDeviceLists();

    // Elemek hozzáadása a playlisthez; at < 0: a végére.
    void appendItems(const QVector<int> &itemIds, int at = -1, int groupId = 0, const QString &groupName = {});
    // Mentett playlist beillesztése blokként; at < 0: a végére.
    void insertSavedPlaylist(int savedPlaylistId, int at = -1);
    void toggleGroup(int groupId);
    PlaylistEditorPage *playlistPage() { return plPage_; }
    CartwallPanel *cartwall() { return cartPanel_; }
    // A hangtárelem szerkesztője; itemId <= 0: a kijelölt elem.
    ItemEditorDialog *openItemEditor(int itemId = 0);
    void showLeft(int view);          // 0: playlist, 1: cartwall
    PlaylistTable *playlistTable() { return table_; }
    void setMode(Mode mode);
    SchedulerPage *schedulerPage() { return schedPage_; }
    MixEditorPage *mixPage() { return mixPage_; }
    void runScheduleRules() { applySchedule(); }
    void moveRow(int from, int to, bool outsideBlock = false);   // outsideBlock: a blokk fejléce elé ejtve
    LibraryPanel *library() { return library_; }
    QString debugState() const;         // fejlesztői ellenőrzéshez

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
    };
    // A táblázat egy sora: playlist-sor, vagy egy blokk fejléce (row < 0; first/count: a blokk sorai).
    struct ViewRow { int row = -1; int first = 0; int count = 0; };
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
    };
    // A kimenő elem időzített vége: a mix pont után tovább szól, és a beállított végpontján úszik ki.
    struct OutFade { int rowId = -1; qint64 startFrame = 0; qint64 endFrame = 0; int fadeMs = 0;
                     eter::FadeCurve curve = eter::FadeCurve::Quadratic; };
    OutFade outFade_[2];
    bool leaving(int player) const;       // kiúszik, vagy már elindult utána a következő
    void runOutFades();
    void runVoiceTrack();
    void startVoiceTrack(int itemId, double duckDb);
    void endVoiceTrack(const QString &reason);
    MixPlan planFor(int outPlayer, int inPlayer);
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
    void updateStartTimes();
    void persistOrder();
    void reconcile();
    bool loadIntoPlayer(Row &row, int player);
    void loadRowInto(int rowId, int player);
    void onPlayerFinished(int player);
    void onPlayerEjected(int player);
    void syncRowStates();
    void removeSelected();
    void clearList();
    void startStandby(int fadeOutPlayer, int fadeMs);
    int standbyPlayer() const;
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

    PlayerWidget *players_[2] = {nullptr, nullptr};
    QVector<Row> rows_;

    PlaylistTable *table_ = nullptr;
    LibraryPanel *library_ = nullptr;
    QLabel *listInfo_ = nullptr;
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
    QToolButton *tabLive_ = nullptr, *tabSched_ = nullptr, *tabMix_ = nullptr;
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
    QLabel *schedStatus_ = nullptr;
    int lastGenerateMinute_ = -1;
    bool waitingForFixed_ = false;
};
