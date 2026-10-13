#include "i18n.h"
#include "MainWindow.h"
#include "net/Remote.h"
#include "Library.h"
#include "PlayerWidget.h"
#include "MixEditorPage.h"
#include "PlaylistEditorPage.h"
#include "CartwallPanel.h"
#include "ItemEditor.h"
#include "TimedEventsPage.h"
#include "MiniMix.h"
#include <QScrollBar>
#include "PlaylistTable.h"
#include "SchedulerPage.h"
#include "SettingsDialog.h"
#include "Theme.h"
#include "Widgets.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QSysInfo>
#include <QNetworkInterface>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

using eter::Engine;

static const int kChipRole = Qt::UserRole + 1;
static const int kGroupRole = Qt::UserRole + 2;     // 1: blokk tagja, 2: blokk fejléce
// A sor azonosítója a táblázat első oszlopában: elemnél a sor, fejlécnél a blokk első sora (−1: más).
// A kijelölés ebből olvasható akkor is, ha a sorok listája már változott, de a táblázat még nem frissült.
static const int kRowIdRole = Qt::UserRole + 3;
static const int kHeadIdRole = Qt::UserRole + 4;

// A playlist sorai: a főablak rajzolja őket, a sor fajtája (elem, blokkfejléc, óra, mini mix) szerint.
class RowDelegate : public QStyledItemDelegate {
public:
    RowDelegate(MainWindow *w, QObject *parent) : QStyledItemDelegate(parent), w_(w) {}
    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &idx) const override
    {
        w_->paintPlaylistRow(p, opt, idx.row());
    }

private:
    MainWindow *w_;
};

static QFrame *makePanel()
{
    auto *f = new QFrame;
    f->setObjectName("panel");
    return f;
}

static void repolish(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
}

MainWindow::MainWindow(Database *db, const QString &backendOverride, QWidget *parent)
    : QMainWindow(parent), db_(db), scheduler_(db)
{
    setWindowTitle(TR("Éter"));
    setWindowIcon(theme::appIcon());
    resize(1440, 900);
    setMinimumSize(1100, 640);

    QSettings s;
    backend_ = backendOverride.isEmpty() ? s.value("audio/backend").toString() : backendOverride;
    routes_[Engine::Player1] = s.value("audio/player1").toString();
    routes_[Engine::Player2] = s.value("audio/player2").toString();
    routes_[Engine::Pfl] = s.value("audio/pfl").toString();
    cartRoute_ = s.value("audio/cartwall", QStringLiteral("@player1")).toString();
    fadeMs_ = std::clamp(db_->setting("default_fade_out_ms", "2500").toInt(), 100, 15000);

    buildUi();
    buildMenus();
    startAudio();
    const double mixDb = db_->setting("auto_mix_threshold_db", "-24").toDouble();
    for (auto *p : players_) p->autoMixDb = mixDb;

    // Az előző futásból megmaradt, még le nem játszott sorok
    // Ami az ütemezőtől egy már elmúlt órára maradt, az nem kerül adásba.
    db_->purgeSchedulerEntriesBefore(Scheduler::keyOf(eter::now()));
    for (const Entry &e : db_->pendingEntries()) rows_.push_back(makeRow(e));

    timer_ = new QTimer(this);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &MainWindow::tick);
    timer_->start(30);
    updateClock();
    reconcile();
    refreshTable();
    setMode(s.value("ui/mode").toString() == "auto" ? Auto : Assist);

    const QByteArray geo = s.value("window/geometry").toByteArray();
    if (!geo.isEmpty()) restoreGeometry(geo);

    // Távoli szerkesztés: az állapot és a hangtár változásait késleltetve, összevonva küldjük.
    remoteStateTimer_ = new QTimer(this);
    remoteStateTimer_->setSingleShot(true);
    remoteStateTimer_->setInterval(250);
    connect(remoteStateTimer_, &QTimer::timeout, this, [this] { if (remote_) remote_->broadcast(remoteState()); });
    remoteLibTimer_ = new QTimer(this);
    remoteLibTimer_->setSingleShot(true);
    remoteLibTimer_->setInterval(800);
    connect(remoteLibTimer_, &QTimer::timeout, this, [this] {
        if (!remote_ || !remote_->clientCount()) return;
        remote_->broadcast(remoteLibrary());
        remote_->broadcast(remoteState());                // a hangtár után a playlist is újra kell (a tükör így épül)
    });
    connect(db_, &Database::itemsChanged, this, [this] { if (remote_ && remote_->clientCount()) remoteLibTimer_->start(); });
    connect(db_, &Database::categoriesChanged, this, [this] { if (remote_ && remote_->clientCount()) remoteLibTimer_->start(); });
    configureRemote();
}

MainWindow::~MainWindow()
{
    timer_->stop();
    if (remote_) {
        // Elsőként és csendben: a leállítás már nem értesítheti a bomló ablakot.
        remote_->disconnect(this);
        delete remote_;
        remote_ = nullptr;
    }
    // A nyitva maradt párbeszédablakok (pl. az elemszerkesztő előhallgatása) a hangmotor forrásait használják:
    // még a motor leállítása előtt zárjuk be őket, különben a bontásuk már felszabadított memóriához nyúlna.
    for (const QPointer<QDialog> &d : [this] {
             QList<QPointer<QDialog>> l;
             for (QDialog *x : findChildren<QDialog *>()) l << x;
             return l;
         }())
        delete d.data();
    engine_.shutdown();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (anythingPlaying()) {
        const auto answer = QMessageBox::question(
            this, TR("Kilépés"),
            TR("Éppen szól valamelyik lejátszó. Biztosan kilépsz? Az adás megszakad."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            e->ignore();
            return;
        }
    }
    for (Row &row : rows_)
        if (row.logId) db_->logEnd(row.logId, "stopped");
    QSettings().setValue("window/geometry", saveGeometry());
    e->accept();
}

// ---------------------------------------------------------------- felület

void MainWindow::buildUi()
{
    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Munkaterület fülek
    auto *tabRow = new QWidget;
    tabRow->setObjectName("tabRow");
    tabRow->setAttribute(Qt::WA_StyledBackground);
    auto *tabs = new QHBoxLayout(tabRow);
    tabs->setContentsMargins(8, 0, 8, 0);
    tabs->setSpacing(2);
    auto addTab = [&](const QString &text, bool enabled, const QString &tip) {
        auto *b = new QToolButton;
        b->setText(text);
        b->setCheckable(true);
        b->setEnabled(enabled);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        tabs->addWidget(b);
        return b;
    };
    tabLive_ = addTab(TR("Adás"), true, TR("Playlist és hangtár"));
    tabSched_ = addTab(TR("Ütemező"), true, TR("Heti rács és órasémák"));
    connect(tabLive_, &QToolButton::clicked, this, [this] { showPage(0); });
    connect(tabSched_, &QToolButton::clicked, this, [this] { showPage(1); });
    tabMix_ = addTab(TR("Mix editor"), true, TR("Az átmenetek beállítása"));
    connect(tabMix_, &QToolButton::clicked, this, [this] { showPage(2); });
    tabPl_ = addTab(TR("Playlist szerkesztő"), true, TR("Egyéni playlistek összeállítása és mentése"));
    connect(tabPl_, &QToolButton::clicked, this, [this] { showPage(3); });
    tabEvents_ = addTab(TR("Időzítések"), true, TR("Elem vagy playlist időzített beküldése a playlistbe vagy adásba"));
    connect(tabEvents_, &QToolButton::clicked, this, [this] { showPage(4); });
    auto *settingsTab = addTab(TR("Beállítások"), true, TR("Hangkimenetek beállítása"));
    settingsTab->setCheckable(false);
    connect(settingsTab, &QToolButton::clicked, this, &MainWindow::openSettings);
    tabs->addStretch(1);

    // Üzemmód
    auto *modeLabel = new QLabel(TR("Üzemmód"));
    modeLabel->setObjectName("small");
    auto *modeBox = new QWidget;
    modeBox->setObjectName("modeSwitch");
    modeBox->setAttribute(Qt::WA_StyledBackground);
    auto *ml = new QHBoxLayout(modeBox);
    ml->setContentsMargins(2, 2, 2, 2);
    ml->setSpacing(2);
    auto modeButton = [&](const QString &text, const QString &tip) {
        auto *b = new QToolButton;
        b->setText(text);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        ml->addWidget(b);
        return b;
    };
    modeAuto_ = modeButton(TR("Automata"),
                           TR("A szám végén a következő elem magától indul (F9)"));
    modeAssist_ = modeButton(TR("Asszisztált"),
                             TR("A következő elem betöltődik, de te indítod a szóközzel (F9)"));
    connect(modeAuto_, &QToolButton::clicked, this, [this] { setMode(Auto); });
    connect(modeAssist_, &QToolButton::clicked, this, [this] { setMode(Assist); });
    tabs->addWidget(modeLabel);
    tabs->addSpacing(6);
    tabs->addWidget(modeBox, 0, Qt::AlignVCenter);
    root->addWidget(tabRow);

    auto *body = new QWidget;
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(8, 8, 8, 8);
    bodyLayout->setSpacing(8);
    bodyLayout->addWidget(buildDeck());

    library_ = new LibraryPanel(db_);
    library_->setFooter(buildPflBar());
    connect(library_, &LibraryPanel::addToPlaylist, this, [this](const QVector<int> &ids) { appendItems(ids); });
    connect(library_, &LibraryPanel::message, this, [this](const QString &m) { say(m, 9000); });
    connect(library_, &LibraryPanel::editRequested, this, [this](int id) { openItemEditor(id); });
    connect(library_, &LibraryPanel::scheduleRequested, this, [this](int id) {
        showPage(4);
        eventsPage_->newForItem(id);
    });
    connect(library_, &LibraryPanel::selectionTouched, this, [this] { lastSelectionInLibrary_ = true; });
    connect(library_, &LibraryPanel::previewRequested, this, [this] {
        lastSelectionInLibrary_ = true;
        if (pflActive_) stopPfl();
        togglePfl();
    });

    auto *split = new QSplitter(Qt::Horizontal);
    split->setHandleWidth(8);
    split->setChildrenCollapsible(false);
    // A bal oldalon a playlist vagy a cartwall látszik; a fejlécükben lévő váltóval cserélhetők.
    leftStack_ = new QStackedWidget;
    leftStack_->addWidget(buildPlaylistPanel());
    cartPanel_ = new CartwallPanel(db_, &engine_);
    cartPanel_->setHeaderWidget(makeViewSwitch());
    connect(cartPanel_, &CartwallPanel::message, this, [this](const QString &m) { say(m, 8000); });
    leftStack_->addWidget(cartPanel_);
    split->addWidget(leftStack_);
    split->addWidget(library_);
    split->setStretchFactor(0, 11);
    split->setStretchFactor(1, 10);
    split->setSizes({740, 680});
    schedPage_ = new SchedulerPage(db_, &scheduler_);
    connect(schedPage_, &SchedulerPage::message, this, [this](const QString &m) { say(m, 8000); });
    connect(schedPage_, &SchedulerPage::generated, this, [this](const GenerateResult &) {
        syncRowsFromDb();
        reconcile();
        refreshTable();
    });
    stack_ = new QStackedWidget;
    stack_->addWidget(split);
    stack_->addWidget(schedPage_);
    mixPage_ = new MixEditorPage(db_, &engine_);
    connect(mixPage_, &MixEditorPage::message, this, [this](const QString &m) { say(m, 8000); });
    connect(mixPage_, &MixEditorPage::mixSaved, this, &MainWindow::onMixSaved);
    connect(mixPage_, &MixEditorPage::overlayEdited, this, [this](int entryId, bool on, qint64 offsetMs, double duckDb) {
        setRowOverlay(entryId, on, offsetMs, duckDb);
    });
    connect(mixPage_, &MixEditorPage::waveLoaded, this, [this] { if (mini_) updateMini(); });
    connect(mixPage_, &MixEditorPage::previewStarting, this, [this] {
        if (itemEditor_) itemEditor_->releasePreview();
        if (pflActive_) stopPfl();        // egyszerre csak egy előhallgatás szólhat
    });
    stack_->addWidget(mixPage_);
    plPage_ = new PlaylistEditorPage(db_);
    connect(plPage_, &PlaylistEditorPage::message, this, [this](const QString &m) { say(m, 8000); });
    connect(plPage_, &PlaylistEditorPage::editRequested, this, [this](int id) { openItemEditor(id); });
    connect(plPage_, &PlaylistEditorPage::insertRequested, this, [this](int id) { insertSavedPlaylist(id); });
    stack_->addWidget(plPage_);
    eventsPage_ = new TimedEventsPage(db_);
    connect(eventsPage_, &TimedEventsPage::message, this, [this](const QString &m) { say(m, 8000); });
    stack_->addWidget(eventsPage_);
    events_ = db_->timedEvents();
    connect(db_, &Database::timedEventsChanged, this, [this] { events_ = db_->timedEvents(); });
    bodyLayout->addWidget(stack_, 1);
    root->addWidget(body, 1);
    showPage(0);
    showLeft(db_->setting("left_view", "0").toInt());
    setCentralWidget(central);

    dbStatus_ = new QLabel;
    statusBar()->addPermanentWidget(dbStatus_);
    schedStatus_ = new QLabel;
    statusBar()->addPermanentWidget(schedStatus_);
    audioStatus_ = new QLabel;
    statusBar()->addPermanentWidget(audioStatus_);
    statusBar()->setSizeGripEnabled(false);
    auto updateDbStatus = [this] {
        dbStatus_->setText(TR("Adatbázis: %1 · %2 elem").arg(db_->path()).arg(db_->itemCount()));
    };
    connect(db_, &Database::itemsChanged, this, updateDbStatus);
    updateDbStatus();
}

QWidget *MainWindow::buildDeck()
{
    auto *deck = new QWidget;
    auto *row = new QHBoxLayout(deck);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    for (int i = 0; i < 2; ++i) {
        auto *p = new PlayerWidget(&engine_, i == 0 ? Engine::Player1 : Engine::Player2);
        players_[i] = p;
        row->addWidget(p, 10);
        connect(p, &PlayerWidget::finished, this, [this, i] { onPlayerFinished(i); });
        connect(p, &PlayerWidget::ejected, this, [this, i] { onPlayerEjected(i); });
        connect(p, &PlayerWidget::stateChanged, this, [this] { syncRowStates(); });
        connect(p, &PlayerWidget::scanned, this, [this, i](qint64 total) {
            if (Row *r = rowById(players_[i]->rowId)) {
                r->frames = total;
                r->item.durationMs = total * 1000 / eter::kRate;
                db_->setItemDuration(r->item.id, r->item.durationMs);
                if (players_[i]->mixOutFrame() >= 0) {
                    r->item.mixOutMs = players_[i]->mixOutFrame() * 1000 / eter::kRate;
                    db_->setItemAutoMix(r->item.id, r->item.mixOutMs);
                }
                refreshTable();
            }
        });
        connect(p, &PlayerWidget::itemsDropped, this, [this, i](const QVector<int> &ids) {
            if (ids.isEmpty()) return;
            const int before = rows_.size();
            appendItems(ids);
            if (rows_.size() > before) loadRowInto(rows_[before].id, i);
        });
        connect(p, &PlayerWidget::filesDropped, this, [this, i](const QStringList &paths) {
            library_->importPaths(paths, [this, i](const QVector<int> &ids) {
                if (ids.isEmpty()) return;
                const int before = rows_.size();
                appendItems(ids);
                if (rows_.size() > before) loadRowInto(rows_[before].id, i);
            });
        });
    }

    // Adásvezérlés
    auto *ctl = makePanel();
    ctl->setMinimumWidth(180);
    auto *cl = new QVBoxLayout(ctl);
    cl->setContentsMargins(12, 10, 12, 10);
    cl->setSpacing(8);
    auto *ctlCaption = new QLabel(TR("ADÁSVEZÉRLÉS"));
    ctlCaption->setObjectName("caption");
    nextBtn_ = new QPushButton(TR("Következő\nSzóköz"));
    nextBtn_->setObjectName("nextButton");
    nextBtn_->setProperty("accent", true);
    nextBtn_->setFocusPolicy(Qt::NoFocus);
    nextBtn_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    nextBtn_->setToolTip(TR("Elindítja a készenlétben álló lejátszót, a szólót kiúsztatja"));
    auto *stopBtn = new QPushButton(TR("Stop"));
    stopBtn->setProperty("danger", true);
    stopBtn->setProperty("tall", true);
    stopBtn->setFocusPolicy(Qt::NoFocus);
    stopBtn->setToolTip(TR("Mindkét lejátszót azonnal megállítja"));
    cl->addWidget(ctlCaption);
    cl->addWidget(nextBtn_, 1);
    cl->addWidget(stopBtn);
    connect(nextBtn_, &QPushButton::clicked, this, &MainWindow::next);
    connect(stopBtn, &QPushButton::clicked, this, &MainWindow::stopAll);
    row->addWidget(ctl, 3);

    // Óra és szint
    auto *clk = makePanel();
    clk->setMinimumWidth(200);
    auto *kl = new QVBoxLayout(clk);
    kl->setContentsMargins(12, 10, 12, 10);
    kl->setSpacing(4);
    clock_ = new QLabel;
    clock_->setObjectName("clock");
    date_ = new QLabel;
    date_->setObjectName("small");
    auto *hourRow = new QHBoxLayout;
    auto *hourLabel = new QLabel(TR("Óra végéig"));
    hourLabel->setObjectName("muted");
    toHour_ = new QLabel;
    toHour_->setObjectName("mono");
    hourRow->addWidget(hourLabel);
    hourRow->addStretch(1);
    hourRow->addWidget(toHour_);
    auto *masterRow = new QHBoxLayout;
    auto *masterLabel = new QLabel(TR("LEJÁTSZÓK"));
    masterLabel->setObjectName("caption");
    masterDb_ = new QLabel;
    masterDb_->setObjectName("mono");
    masterRow->addWidget(masterLabel);
    masterRow->addStretch(1);
    masterRow->addWidget(masterDb_);
    master_ = new MeterWidget(8);
    kl->addWidget(clock_);
    kl->addWidget(date_);
    kl->addSpacing(4);
    kl->addLayout(hourRow);
    kl->addStretch(1);
    kl->addLayout(masterRow);
    kl->addWidget(master_);
    row->addWidget(clk, 3);
    return deck;
}

QWidget *MainWindow::makeViewSwitch()
{
    auto *box = new QWidget;
    box->setObjectName("viewSwitch");
    box->setAttribute(Qt::WA_StyledBackground);
    auto *l = new QHBoxLayout(box);
    l->setContentsMargins(2, 2, 2, 2);
    l->setSpacing(2);
    const QString texts[2] = {TR("Playlist"), TR("Cartwall")};
    const QString tips[2] = {TR("A playlist megjelenítése"), TR("A cartwall megjelenítése a playlist helyén")};
    for (int i = 0; i < 2; ++i) {
        auto *b = new QToolButton;
        b->setText(texts[i]);
        b->setToolTip(tips[i]);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QToolButton::clicked, this, [this, i] { showLeft(i); });
        l->addWidget(b);
        viewButtons_[i] << b;
    }
    return box;
}

void MainWindow::showLeft(int view)
{
    view = view == 1 ? 1 : 0;
    if (leftStack_) leftStack_->setCurrentIndex(view);
    for (int i = 0; i < 2; ++i)
        for (QToolButton *b : viewButtons_[i]) b->setChecked(i == view);
    if (db_->setting("left_view", "0").toInt() != view) db_->setSetting("left_view", QString::number(view));
}

QWidget *MainWindow::buildPlaylistPanel()
{
    auto *panel = makePanel();
    auto *v = new QVBoxLayout(panel);
    v->setContentsMargins(1, 1, 1, 1);
    v->setSpacing(0);

    auto *head = new QFrame;
    head->setObjectName("panelHeader");
    auto *h = new QHBoxLayout(head);
    h->setContentsMargins(10, 8, 10, 8);
    h->setSpacing(6);
    auto *title = new QLabel(TR("Playlist"));
    title->setObjectName("panelTitle");
    auto mk = [&](const QString &text, const QString &tip) {
        auto *b = new QPushButton(text);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        return b;
    };
    auto *to1 = mk(QStringLiteral("→ 1"), TR("A kijelölt sor betöltése az 1-es lejátszóba"));
    auto *to2 = mk(QStringLiteral("→ 2"), TR("A kijelölt sor betöltése a 2-es lejátszóba"));
    auto *del = mk(TR("Törlés"), TR("A kijelölt sor törlése a playlistből (Delete)"));
    auto *clear = mk(TR("Lista ürítése"), TR("Minden, ami nem szól éppen, kikerül a playlistből"));
    // Mentett playlist beillesztése blokként
    auto *plBtn = mk(TR("Playlist beillesztése"),
                     TR("Mentett playlist beillesztése blokként (a Playlist szerkesztőben készíthető)"));
    auto *plMenu = new QMenu(plBtn);
    plBtn->setMenu(plMenu);
    connect(plMenu, &QMenu::aboutToShow, this, [this, plMenu] {
        plMenu->clear();
        const QVector<SavedPlaylist> lists = db_->savedPlaylists();
        QString category;
        for (const SavedPlaylist &p : lists) {
            if (p.categoryName != category) {
                category = p.categoryName;
                plMenu->addSection(category);
            }
            plMenu->addAction(swatchIcon(QColor(p.color)),
                              TR("%1   (%2 elem · %3)").arg(p.name).arg(p.count).arg(formatMs(p.durationMs)), this,
                              [this, id = p.id] { insertSavedPlaylist(id); });
        }
        if (lists.isEmpty()) plMenu->addAction(TR("Még nincs mentett playlist"))->setEnabled(false);
        plMenu->addSeparator();
        plMenu->addAction(TR("Playlist szerkesztő megnyitása…"), this, [this] { showPage(3); });
    });
    delete title;
    h->addWidget(makeViewSwitch());
    h->addStretch(1);
    h->addWidget(plBtn);
    h->addWidget(to1);
    h->addWidget(to2);
    h->addWidget(del);
    h->addWidget(clear);
    v->addWidget(head);

    table_ = new PlaylistTable;
    table_->setObjectName("playlistTable");
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({TR("Kezdés"), TR("Cím"), TR("Előadó"),
                                       TR("Kategória"), TR("Hossz"), TR("Állapot")});
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(30);
    table_->setShowGrid(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setFrameShape(QFrame::NoFrame);
    table_->setItemDelegate(new RowDelegate(this, table_));
    table_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    auto *hh = table_->horizontalHeader();
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hh->setHighlightSections(false);
    // Egy látható oszlop: a sort a RowDelegate rajzolja egészben. A többi oszlop rejtett, a szövegeit
    // a hozzáférhetőség és a fejlesztői ellenőrzés használja.
    hh->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c < 6; ++c) table_->setColumnHidden(c, true);
    hh->hide();
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->setMouseTracking(true);
    v->addWidget(table_, 1);

    auto *foot = new QFrame;
    foot->setObjectName("panelFooter");
    auto *f = new QHBoxLayout(foot);
    f->setContentsMargins(10, 7, 10, 7);
    listInfo_ = new QLabel;
    listInfo_->setObjectName("small");
    auto *hint = new QLabel(TR("Elem hozzáadása: húzd ide a hangtárból · a sorok és a blokkok húzással átrendezhetők"));
    hint->setObjectName("small");
    f->addWidget(listInfo_);
    f->addStretch(1);
    f->addWidget(hint);
    v->addWidget(foot);

    connect(del, &QPushButton::clicked, this, &MainWindow::removeSelected);
    connect(clear, &QPushButton::clicked, this, &MainWindow::clearList);
    connect(to1, &QPushButton::clicked, this, [this] { loadRowInto(selectedRowId(), 0); });
    connect(to2, &QPushButton::clicked, this, [this] { loadRowInto(selectedRowId(), 1); });
    connect(table_, &PlaylistTable::deletePressed, this, &MainWindow::removeSelected);
    connect(table_, &PlaylistTable::spacePressed, this, &MainWindow::next);
    connect(table_, &PlaylistTable::rowMoved, this, &MainWindow::onViewRowMoved);
    connect(table_, &PlaylistTable::viewGeometryChanged, this, &MainWindow::placeMini);
    connect(table_->verticalHeader(), &QHeaderView::sectionResized, this, &MainWindow::placeMini);
    table_->setContextMenuPolicy(Qt::CustomContextMenu);
    // Kézi görgetés: ha az adásban lévő elem kikerül a látható részből, a lista nem ugrik vissza rá,
    // amíg vissza nem görgetnek hozzá.
    connect(table_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (programmaticScroll_) return;
        const bool visible = followTargetVisible();
        if (visible != follow_) {
            follow_ = visible;
            if (follow_) followedRowId_ = -1;
        }
    });
    connect(table_, &QWidget::customContextMenuRequested, this, &MainWindow::playlistContextMenu);
    connect(table_, &PlaylistTable::itemsDropped, this, [this](const QVector<int> &ids, int at) {
        appendItems(ids, insertIndexForView(at));
    });
    connect(table_, &PlaylistTable::pathsDropped, this, [this](const QStringList &paths, int at) {
        // Előbb a hangtárba kerülnek (kategóriával), utána a playlistbe.
        const int index = insertIndexForView(at);
        const int anchor = index < rows_.size() ? rows_[index].id : -1;
        library_->importPaths(paths, [this, anchor](const QVector<int> &ids) {
            appendItems(ids, anchor >= 0 ? indexOfRow(anchor) : -1);
        });
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { lastSelectionInLibrary_ = false; });
    connect(table_, &QTableWidget::cellPressed, this, [this] { lastSelectionInLibrary_ = false; });
    // Blokkfejléc: kattintásra kinyílik vagy becsukódik (a dupla kattintás egynek számít).
    connect(table_, &QTableWidget::cellClicked, this, [this](int v, int) {
        if (v < 0 || v >= view_.size()) return;
        if (view_[v].kind == ViewRow::Item) {
            // A sor jobb szélén lévő nyíl: a mini mix editor lenyitása.
            const QPoint pos = table_->viewport()->mapFromGlobal(QCursor::pos());
            if (pos.x() >= table_->viewport()->width() - 34 && nextPlayableAfter(view_[v].row) >= 0) {
                // A lista újraépítése ne a táblázat saját egérkezelője alatt történjen.
                const int id = rows_[view_[v].row].id;
                QTimer::singleShot(0, this, [this, id] { if (rowById(id)) toggleMini(id); });
            }
            return;
        }
        if (view_[v].kind != ViewRow::Header) return;
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastToggleMs_ < QApplication::doubleClickInterval()) return;
        lastToggleMs_ = now;
        toggleGroup(rows_[view_[v].first].groupId);
    });
    connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int v, int) {
        if (v < 0 || v >= view_.size() || view_[v].kind != ViewRow::Item) return;
        const QPoint pos = table_->viewport()->mapFromGlobal(QCursor::pos());
        if (pos.x() >= table_->viewport()->width() - 34) return;           // a nyílra kattintás nem betöltés
        const int r = view_[v].row;
        int target = -1;
        for (int i = 0; i < 2; ++i)
            if (!players_[i]->isLoaded()) { target = i; break; }
        if (target < 0)
            for (int i = 0; i < 2; ++i)
                if (!players_[i]->isPlaying()) { target = i; break; }
        if (target < 0) {
            say(TR("Mindkét lejátszó szól, most nem tölthető be új elem."));
            return;
        }
        loadRowInto(rows_[r].id, target);
    });
    return panel;
}

QWidget *MainWindow::buildPflBar()
{
    auto *foot = new QFrame;
    foot->setObjectName("panelFooter");
    auto *f = new QHBoxLayout(foot);
    f->setContentsMargins(10, 7, 10, 7);
    f->setSpacing(10);
    auto *badge = new QLabel(QStringLiteral("P"));
    badge->setObjectName("badgeP");
    pflBtn_ = new QPushButton(TR("Előhallgatás"));
    pflBtn_->setFocusPolicy(Qt::NoFocus);
    pflBtn_->setIcon(theme::icon(theme::Icon::Play, theme::text));
    pflBtn_->setToolTip(TR("A hangtárban vagy a playlistben kijelölt elem meghallgatása az előhallgatás kimenetén (Ctrl+P)"));
    auto *mid = new QVBoxLayout;
    mid->setSpacing(4);
    pflTitle_ = new QLabel;
    pflTitle_->setObjectName("small");
    pflTitle_->setTextFormat(Qt::PlainText);
    pflTitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    pflProgress_ = new QProgressBar;
    pflProgress_->setRange(0, 1000);
    pflProgress_->setTextVisible(false);
    mid->addWidget(pflTitle_);
    mid->addWidget(pflProgress_);
    auto *outLabel = new QLabel(TR("Kimenet"));
    outLabel->setObjectName("small");
    pflOutput_ = new QComboBox;
    pflOutput_->setFocusPolicy(Qt::NoFocus);
    pflOutput_->setMinimumWidth(170);
    pflOutput_->setMaximumWidth(240);
    f->addWidget(badge);
    f->addWidget(pflBtn_);
    f->addLayout(mid, 1);
    f->addWidget(outLabel);
    f->addWidget(pflOutput_);
    connect(pflBtn_, &QPushButton::clicked, this, &MainWindow::togglePfl);
    connect(pflOutput_, &QComboBox::activated, this, [this](int i) {
        changeRoute(Engine::Pfl, pflOutput_->itemData(i).toString());
    });
    stopPfl();
    return foot;
}

void MainWindow::buildMenus()
{
    auto *file = menuBar()->addMenu(TR("&Fájl"));
    file->addAction(TR("Fájlok importálása…"), QKeySequence::Open, library_, &LibraryPanel::chooseFiles);
    file->addAction(TR("Mappa importálása…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), library_,
                    &LibraryPanel::chooseFolder);
    file->addSeparator();
    file->addAction(TR("Távoli szerkesztő…"), this, [this] {
        // Külön folyamat szerkesztő módban: egy másik gépen futó playout playlistjéhez csatlakozik.
        // AppImage-ből a csomag maga indítandó (a belső útvonal a kilépés után eltűnne).
        const QString appImage = qEnvironmentVariable("APPIMAGE");
        const QString exe = appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
        if (!QProcess::startDetached(exe, {QStringLiteral("--editor")}))
            say(TR("A szerkesztő nem indítható: %1").arg(exe));
    });
    file->addSeparator();
    file->addAction(TR("Kilépés"), QKeySequence(Qt::CTRL | Qt::Key_Q), this, &QWidget::close);

    auto *play = menuBar()->addMenu(TR("&Lejátszás"));
    play->addAction(TR("Következő"), QKeySequence(Qt::Key_Space), this, &MainWindow::next);
    play->addAction(TR("Mindent megállít"), QKeySequence(Qt::CTRL | Qt::Key_Period), this,
                    &MainWindow::stopAll);
    play->addSeparator();
    autoAction_ = play->addAction(TR("Automata üzemmód"), QKeySequence(Qt::Key_F9), this,
                                  [this](bool on) { setMode(on ? Auto : Assist); });
    autoAction_->setCheckable(true);
    play->addSeparator();
    play->addAction(TR("Előhallgatás indítása / leállítása"), QKeySequence(Qt::CTRL | Qt::Key_P), this,
                    &MainWindow::togglePfl);

    auto *data = menuBar()->addMenu(TR("&Adatbázis"));
    data->addAction(TR("Elem szerkesztése… (adatok, intro, outro)"), QKeySequence(Qt::CTRL | Qt::Key_E), this,
                    [this] { openItemEditor(); });
    data->addSeparator();
    data->addAction(TR("Kategóriák…"), library_, &LibraryPanel::openCategories);
    data->addAction(TR("Adásnapló…"), QKeySequence(Qt::CTRL | Qt::Key_L), this, &MainWindow::openLog);

    auto *tools = menuBar()->addMenu(TR("&Eszközök"));
    tools->addAction(TR("Beállítások…"), QKeySequence(Qt::CTRL | Qt::Key_Comma), this,
                     &MainWindow::openSettings);

    auto *help = menuBar()->addMenu(TR("&Súgó"));
    help->addAction(TR("Névjegy"), this, &MainWindow::about);
}

void MainWindow::setMode(Mode mode)
{
    const bool changed = mode != mode_;
    mode_ = mode;
    modeAuto_->setChecked(mode == Auto);
    modeAssist_->setChecked(mode == Assist);
    if (autoAction_) autoAction_->setChecked(mode == Auto);
    QSettings().setValue("ui/mode", mode == Auto ? "auto" : "assist");
    if (changed)
        say(mode == Auto ? TR("Automata üzemmód: a szám végén a következő elem magától indul.")
                         : TR("Asszisztált üzemmód: a következő elemet a szóközzel indítod."));
}

// ------------------------------------------------------------------- hang

void MainWindow::say(const QString &msg, int ms) { statusBar()->showMessage(msg, ms); }

void MainWindow::startAudio()
{
    std::string err;
    if (!engine_.init(backend_.toStdString(), &err)) {
        // A kért háttérrendszer nem megy: automatikus választás.
        if (!engine_.init({}, &err)) {
            audioStatus_->setText(TR("Hang: nem indult el"));
            say(QString::fromStdString(err), 0);
            refreshDeviceLists();
            return;
        }
        say(TR("A beállított hangrendszer nem érhető el, automatikus választás történt."), 10000);
    }
    audioStatus_->setText(TR("Hang: %1 · 48 kHz").arg(QString::fromStdString(engine_.backendName())));

    QStringList missing;
    for (int ch = 0; ch < Engine::kRoutedChannels; ++ch) {
        if (!engine_.setRoute(static_cast<Engine::Channel>(ch), routes_[ch].toStdString(), &err)) {
            if (!routes_[ch].isEmpty()) missing << routes_[ch];
            routes_[ch].clear();
            engine_.setRoute(static_cast<Engine::Channel>(ch), {}, &err);
        }
    }
    if (!applyCartRoute(&err)) {
        if (cartRoute_ != QLatin1String("@player1") && !cartRoute_.isEmpty()) missing << cartRoute_;
        cartRoute_ = QStringLiteral("@player1");
        applyCartRoute(&err);
    }
    if (!missing.isEmpty())
        say(TR("Nem található kimenet, az alapértelmezett szól helyette: %1").arg(missing.join(", ")), 0);
    refreshDeviceLists();
}

QStringList MainWindow::deviceNames()
{
    QStringList out;
    for (const auto &d : engine_.devices()) out << QString::fromStdString(d.name);
    return out;
}

void MainWindow::refreshDeviceLists()
{
    const QStringList names = deviceNames();
    pflOutput_->blockSignals(true);
    pflOutput_->clear();
    pflOutput_->addItem(TR("Alapértelmezett kimenet"), QString());
    for (const QString &n : names) pflOutput_->addItem(n, n);
    pflOutput_->setCurrentIndex(std::max(0, pflOutput_->findData(routes_[Engine::Pfl])));
    pflOutput_->blockSignals(false);
    emit devicesChanged();
}

bool MainWindow::channelBusy(int ch)
{
    if (ch == kCartRoute) {
        for (int c = Engine::Cart1; c <= Engine::CartLast; ++c)
            if (engine_.source(static_cast<Engine::Channel>(c)).playing()) return true;
        return false;
    }
    if (ch == 0) {
        // Az 1-es lejátszó kimenetén szól a voice track és a ráfektetett elem is.
        for (Engine::Channel c : {Engine::Player1, Engine::Vt, Engine::Ovl1, Engine::Ovl2})
            if (engine_.source(c).playing()) return true;
        return false;
    }
    return engine_.source(static_cast<Engine::Channel>(ch)).playing();
}

bool MainWindow::anythingPlaying()
{
    return channelBusy(0) || channelBusy(1) || channelBusy(2) || channelBusy(kCartRoute);
}

// A cartwall kimenete: saját eszköz, vagy az 1-es lejátszóé.
bool MainWindow::applyCartRoute(std::string *err)
{
    const QString device = cartRoute_ == QLatin1String("@player1") ? routes_[Engine::Player1] : cartRoute_;
    return engine_.setRoute(Engine::Cart1, device.toStdString(), err);
}

bool MainWindow::changeRoute(int ch, const QString &deviceName)
{
    if (ch == kCartRoute) {
        if (channelBusy(kCartRoute)) {
            say(TR("A cartwall éppen szól, a kimenete most nem váltható."));
            refreshDeviceLists();
            return false;
        }
        const QString previous = cartRoute_;
        cartRoute_ = deviceName;
        std::string err;
        const bool ok = applyCartRoute(&err);
        if (ok) {
            QSettings().setValue("audio/cartwall", cartRoute_);
            say(TR("Cartwall kimenete: %1")
                    .arg(deviceName == QLatin1String("@player1") ? TR("az 1-es lejátszóé")
                         : deviceName.isEmpty() ? TR("alapértelmezett") : deviceName), 4000);
        } else {
            say(QString::fromStdString(err), 10000);
            cartRoute_ = previous;
            if (!applyCartRoute(&err)) {
                cartRoute_ = QStringLiteral("@player1");
                applyCartRoute(&err);
            }
        }
        refreshDeviceLists();
        return ok;
    }
    // Ha a cartwall az 1-es lejátszó kimenetét követi, vele együtt vált; közben ne szóljon.
    if (ch == Engine::Player1 && cartRoute_ == QLatin1String("@player1") && channelBusy(kCartRoute)) {
        say(TR("A cartwall éppen szól az 1-es lejátszó kimenetén, a kimenet most nem váltható."));
        refreshDeviceLists();
        return false;
    }
    static const char *keys[] = {"audio/player1", "audio/player2", "audio/pfl"};
    static const char *labels[] = {"Lejátszó 1", "Lejátszó 2", "Előhallgatás"};
    if (channelBusy(ch)) {
        say(TR("%1 éppen szól, a kimenete most nem váltható.").arg(i18n::text(labels[ch])));
        refreshDeviceLists();
        return false;
    }
    std::string err;
    const bool ok = engine_.setRoute(static_cast<Engine::Channel>(ch), deviceName.toStdString(), &err);
    if (ok) {
        routes_[ch] = deviceName;
        QSettings().setValue(keys[ch], deviceName);
        if (ch == Engine::Player1 && cartRoute_ == QLatin1String("@player1")) applyCartRoute();
        say(TR("%1 kimenete: %2")
                .arg(i18n::text(labels[ch]),
                     deviceName.isEmpty() ? TR("alapértelmezett") : deviceName), 4000);
    } else {
        say(QString::fromStdString(err), 10000);
        // Visszaállunk az előző, működő kimenetre.
        if (!engine_.setRoute(static_cast<Engine::Channel>(ch), routes_[ch].toStdString(), &err)) {
            routes_[ch].clear();
            engine_.setRoute(static_cast<Engine::Channel>(ch), {}, &err);
        }
    }
    refreshDeviceLists();
    return ok;
}

bool MainWindow::changeBackend(const QString &backend)
{
    if (anythingPlaying()) {
        say(TR("Lejátszás közben a hangrendszer nem váltható."));
        return false;
    }
    const QString previous = backend_;
    std::string err;
    if (!engine_.init(backend.toStdString(), &err)) {
        say(QString::fromStdString(err), 10000);
        backend_ = previous;
        startAudio();
        return false;
    }
    backend_ = backend;
    QSettings().setValue("audio/backend", backend_);
    startAudio();
    return true;
}

// ------------------------------------------------------------- lejátszás

void MainWindow::tick()
{
    float peakL = 0.f, peakR = 0.f;
    for (auto *p : players_) {
        float l = 0.f, r = 0.f;
        p->tick(&l, &r);
        peakL = std::max(peakL, l);
        peakR = std::max(peakR, r);
    }
    if (cartPanel_) cartPanel_->tick(&peakL, &peakR);
    master_->feed(peakL, peakR);
    const double db = master_->levelDb();
    masterDb_->setText(db <= -60.0 ? TR("−∞ dB")
                                   : i18n::locale().toString(db, 'f', 1).replace('-', QStringLiteral("−")) +
                                         TR(" dB"));

    // Automata: a szóló elem mix pontjánál indul a következő.
    if (mode_ == Auto) {
        int onAir = -1, playing = 0;
        for (int i = 0; i < 2; ++i) {
            if (!players_[i]->isPlaying()) continue;
            ++playing;
            if (!leaving(i)) onAir = i;
        }
        if (onAir >= 0 && playing == 1) {
            PlayerWidget *p = players_[onAir];
            const int standby = standbyPlayer();
            const qint64 mix = planFor(onAir, standby).mixOutFrames;
            if (mix >= 0 && p->position() >= mix && standby >= 0 && !standbyIsEarlyFixed()) {
                const qint64 leftMs = std::max<qint64>(0, p->duration() - p->position()) * 1000 / eter::kRate;
                startStandby(onAir, static_cast<int>(std::clamp<qint64>(leftMs, 50, fadeMs_)));
            }
        }
    }

    runOutFades();
    runVoiceTrack();
    runOverlays();

    // Előhallgatás
    auto &pfl = engine_.source(Engine::Pfl);
    float l, r;
    pfl.takePeaks(l, r);
    if (!pflActive_) {
        // a Mix editor előhallgatása használja a csatornát, vagy semmi sem szól rajta
    } else if (pfl.takeFinished()) {
        stopPfl();
    } else if (pfl.loaded() && pfl.duration() > 0) {
        pflProgress_->setValue(static_cast<int>(1000.0 * pfl.position() / pfl.duration()));
    }

    nextBtn_->setEnabled(players_[0]->isLoaded() || players_[1]->isLoaded());
    if (++clockDivider_ >= 6) {
        clockDivider_ = 0;
        updateClock();
        applySchedule();
        updateStartTimes();
        autoGenerate();
        runTimedEvents();
        followPlaying();
        table_->viewport()->update();                       // haladási sávok, hátralévő idők
        // A szerkesztők 3 másodpercenként friss időpontokat kapnak (a becsült kezdések telnek).
        if (remote_ && remote_->clientCount() && ++remoteTick_ >= 17) {
            remoteTick_ = 0;
            remoteStateTimer_->start();
        }
        if (mini_) {
            const Row *a = rowById(expandedRowId_);
            mini_->setPlayhead(a && a->state == Row::Playing && a->player >= 0 ? players_[a->player]->position() * 1000 / eter::kRate : -1);
            mini_->setPreviewing(mixPage_->quickPreviewing(mini_->fromEntry(), mini_->toEntry()));
        }
    }
}

// ------------------------------------------------------- mini mix editor

// A sor után következő, még adásba kerülő elem (a Mix editor párjai is így képződnek).
int MainWindow::nextPlayableAfter(int index) const
{
    if (index < 0 || index >= rows_.size()) return -1;
    const Row &a = rows_[index];
    if (a.state != Row::Playing && a.state != Row::Cued && a.state != Row::Pending) return -1;
    if (a.overlay && (a.overlayVoice >= 0 || overlayActive(index))) return -1;     // ráfektetett elem: nincs saját átmenete
    for (int j = index + 1; j < rows_.size(); ++j) {
        const Row &b = rows_[j];
        if (b.state != Row::Playing && b.state != Row::Cued && b.state != Row::Pending) continue;
        if (b.overlay && (b.overlayVoice >= 0 || overlayActive(j))) continue;
        return j;
    }
    return -1;
}

// ------------------------------------------------------- ráfektetett elemek

bool MainWindow::overlayTargets(int index, int *a, int *c) const
{
    if (index < 0 || index >= rows_.size()) return false;
    const Row &r = rows_[index];
    if (!r.overlay) return false;
    const bool sounding = r.state == Row::Playing && r.overlayVoice >= 0;
    if (!sounding && r.state != Row::Pending && r.state != Row::Cued) return false;
    int ai = -1, ci = -1;
    for (int j = index - 1; j >= 0; --j)
        if (!rows_[j].overlay) { ai = j; break; }
    for (int j = index + 1; j < rows_.size(); ++j) {
        if (rows_[j].overlay) continue;
        const Row::State st = rows_[j].state;
        if (st == Row::Pending || st == Row::Cued || st == Row::Playing) ci = j;
        break;
    }
    if (a) *a = ai;
    if (c) *c = ci;
    return sounding || (ai >= 0 && ci >= 0);
}

void MainWindow::applyDuck()
{
    double gain = vtPlaying_ ? vtDuckGain_ : 1.0;
    for (const OverlayVoice &v : ovl_)
        if (v.rowId >= 0) gain = std::min(gain, v.duckGain);
    engine_.source(Engine::Player1).setDuck(static_cast<float>(gain));
    engine_.source(Engine::Player2).setDuck(static_cast<float>(gain));
}

bool MainWindow::startOverlay(Row &row)
{
    int voice = -1;
    for (int v = 0; v < Engine::kOverlayVoices; ++v)
        if (ovl_[v].rowId < 0) { voice = v; break; }
    if (voice < 0) return false;                       // mindkét csatorna foglalt: a következő körben újra
    auto &src = engine_.source(static_cast<Engine::Channel>(Engine::Ovl1 + voice));
    std::string err;
    if (!src.load(row.item.path.toStdString(), &err)) {
        row.state = Row::Failed;
        db_->setEntryStatus(row.id, "skipped");
        say(TR("A ráfektetett elem nem tölthető be: %1").arg(QString::fromStdString(err)), 9000);
        refreshTable();
        return false;
    }
    if (row.item.cueInMs > 0) src.seek(row.item.cueInMs * eter::kRate / 1000);
    src.play();
    OverlayVoice &v = ovl_[voice];
    v.rowId = row.id;
    v.duckGain = std::pow(10.0, std::min(0.0, row.overlayDuckDb) / 20.0);
    v.logId = db_->logStart(row.item, row.id, 1, mode_ == Auto ? "auto" : "assist");
    row.state = Row::Playing;
    row.overlayVoice = voice;
    row.startedAt = eter::now();
    db_->setEntryStatus(row.id, "playing");
    applyDuck();
    refreshTable();
    return true;
}

void MainWindow::endOverlay(int voice, const QString &reason)
{
    OverlayVoice &v = ovl_[voice];
    if (v.rowId < 0) return;
    engine_.source(static_cast<Engine::Channel>(Engine::Ovl1 + voice)).unload();
    db_->logEnd(v.logId, reason);
    if (Row *row = rowById(v.rowId)) {
        row->state = Row::Played;
        row->overlayVoice = -1;
        db_->setEntryStatus(row->id, "played");
    }
    v = OverlayVoice();
    applyDuck();
    trimHistory();
    refreshTable();
}

// A ráfektetett elemek indítása és lezárása; a 30 ms-os időzítő hívja.
void MainWindow::runOverlays()
{
    for (int v = 0; v < Engine::kOverlayVoices; ++v) {
        if (ovl_[v].rowId < 0) continue;
        auto &src = engine_.source(static_cast<Engine::Channel>(Engine::Ovl1 + v));
        float l, r;
        src.takePeaks(l, r);
        if (src.takeFinished() || !src.playing()) endOverlay(v, "finished");
    }
    // A várakozó ráfektetett elemek közül az, amelyiknek eljött az ideje (a lista elejét elég nézni).
    int seen = 0;
    for (int i = 0; i < rows_.size(); ++i) {
        Row &b = rows_[i];
        if (b.state == Row::Played || b.state == Row::Skipped || b.state == Row::Failed) continue;
        if (++seen > 40) break;
        if (!b.overlay || b.state != Row::Pending) continue;
        int ai = -1, ci = -1;
        if (!overlayTargets(i, &ai, &ci) || ci < 0) continue;
        const Row &c = rows_[ci];
        const qint64 offsetFrames = b.overlayOffsetMs * eter::kRate / 1000;
        bool go = false;
        if (c.state == Row::Playing && c.player >= 0) {
            // A bejövő elem már szól: a mix ponthoz (az ő kezdetéhez) képest indul.
            const PlayerWidget *pc = players_[c.player];
            const qint64 elapsed = pc->position() - pc->cueFrame();
            if (elapsed > std::max<qint64>(offsetFrames, 0) + 3 * eter::kRate) {
                // Jóval elmúlt az ideje (például menet közben fektették rá): kimarad, nem szól bele utólag.
                b.state = Row::Skipped;
                db_->setEntryStatus(b.id, "skipped");
                say(TR("A ráfektetett „%1” kimaradt: az ideje már elmúlt.").arg(b.item.title), 9000);
                refreshTable();
                return;
            }
            go = b.overlayOffsetMs <= 0 || elapsed >= offsetFrames;
        } else if (b.overlayOffsetMs < 0 && ai >= 0 && rows_[ai].state == Row::Playing && rows_[ai].player >= 0
                   && c.state == Row::Cued && c.player >= 0) {
            // A mix pont előtt induló elem: a kimenő elem tervezett mix pontjához képest.
            const qint64 mix = planFor(rows_[ai].player, c.player).mixOutFrames;
            go = mix >= 0 && players_[rows_[ai].player]->position() >= mix + offsetFrames;
        }
        if (go) {
            startOverlay(b);
            return;                                    // egy körben egy indítás; a sorok közben frissültek
        }
    }
}

bool MainWindow::setRowOverlay(int rowId, bool on, qint64 offsetMs, double duckDb)
{
    Row *row = rowById(rowId);
    if (!row) return false;
    if (row->state != Row::Pending && row->state != Row::Cued) {
        say(TR("Csak várakozó elem fektethető rá az átmenetre."));
        return false;
    }
    const int index = indexOfRow(rowId);
    offsetMs = std::clamp<qint64>(offsetMs, -120000, 120000);
    duckDb = std::clamp(duckDb, -30.0, 0.0);
    if (on) {
        const bool was = row->overlay;
        row->overlay = true;
        int ai = -1, ci = -1;
        if (!overlayTargets(index, &ai, &ci)) {
            row->overlay = was;
            say(TR("Ráfektetni csak két elem közé lehet: kell előtte és utána is egy elem."));
            return false;
        }
        row->overlayOffsetMs = offsetMs;
        row->overlayDuckDb = duckDb;
        if (row->state == Row::Cued && row->player >= 0) {
            const int p = row->player;
            row->state = Row::Pending;
            row->player = -1;
            players_[p]->eject();
        }
    } else {
        if (!row->overlay) return true;
        // A két szomszéd közti egyedi átmenet a ráfektetéshez tartozott: a feloldással az is megszűnik.
        int ai = -1, ci = -1;
        overlayTargets(index, &ai, &ci);
        if (ai >= 0 && ci >= 0) {
            const Transition t = db_->transition(rows_[ci].id);
            if (t.toEntryId && t.fromEntryId == rows_[ai].id) db_->deleteTransition(rows_[ci].id);
        }
        row = rowById(rowId);
        row->overlay = false;
        row->overlayOffsetMs = 0;
        row->overlayDuckDb = 0;
    }
    db_->setEntryOverlay(rowId, on, offsetMs, duckDb);
    ++mixVersion_;
    reconcile();
    refreshTable();
    return true;
}

void MainWindow::toggleMini(int rowId)
{
    expandedRowId_ = expandedRowId_ == rowId ? -1 : rowId;
    refreshTable();
    if (miniView_ >= 0) {
        programmaticScroll_ = true;
        table_->scrollTo(table_->model()->index(miniView_, 0), QAbstractItemView::EnsureVisible);
        programmaticScroll_ = false;
    }
}

void MainWindow::updateMini()
{
    // A mini szerkesztő egyetlen, a táblázat nézetére helyezett widget (nem a táblázat cellawidgete):
    // a sorok újraépítése így nem törli és nem hozza létre újra, csak a helyére kerül.
    int wv = -1;
    for (int v = 0; v < view_.size(); ++v)
        if (view_[v].kind == ViewRow::Wave) wv = v;
    const int ai = wv >= 0 ? view_[wv].row : -1;
    const int bi = ai >= 0 ? nextPlayableAfter(ai) : -1;
    if (wv < 0 || bi < 0) {
        if (wv < 0 && expandedRowId_ >= 0 && !rowById(expandedRowId_)) expandedRowId_ = -1;
        miniView_ = -1;
        if (mini_) {
            if (mixPage_->quickPreviewing(mini_->fromEntry(), mini_->toEntry())) mixPage_->stopPreview();
            mini_->hide();
        }
        return;
    }
    const Row &a = rows_[ai], &b = rows_[bi];
    if (!mini_) {
        auto *m = new MiniMix(table_->viewport());
        mini_ = m;
        connect(m, &MiniMix::mixChanged, this, [this, m](qint64 ms) {
            const int from = m->fromEntry(), to = m->toEntry();
            // A mentés újraépíti a listát: ne a widget saját eseménykezelője alatt történjen.
            QTimer::singleShot(0, this, [this, from, to, ms] { mixPage_->quickSetMix(from, to, ms); });
        });
        connect(m, &MiniMix::previewRequested, this, [this, m] {
            if (pflActive_) stopPfl();
            mixPage_->quickPreviewToggle(m->fromEntry(), m->toEntry());
            m->setPreviewing(mixPage_->quickPreviewing(m->fromEntry(), m->toEntry()));
        });
        connect(m, &MiniMix::resetRequested, this, [this, m] {
            const int from = m->fromEntry(), to = m->toEntry();
            QTimer::singleShot(0, this, [this, from, to] { mixPage_->quickReset(from, to); });
        });
        connect(m, &MiniMix::editorRequested, this, [this, m] {
            const int from = m->fromEntry(), to = m->toEntry();
            QTimer::singleShot(0, this, [this, from, to] {
                mixPage_->selectEntries(from, to);
                showPage(2);
            });
        });
    }
    miniView_ = wv;
    MixParams p;
    int kind = 0;
    if (mixPage_->quickParams(a.id, b.id, &p, &kind))
        mini_->setPair(a.id, b.id, a.item.title, b.item.title, p, kind);
    qint64 da = 0, dbur = 0;
    const QVector<float> pa = mixPage_->quickWave(a.item, &da), pb = mixPage_->quickWave(b.item, &dbur);
    mini_->setWaves(pa, da, pb, dbur);
    mini_->setPreviewing(mixPage_->quickPreviewing(a.id, b.id));
    placeMini();
}

void MainWindow::placeMini()
{
    if (!mini_) return;
    if (miniView_ < 0 || miniView_ >= table_->rowCount()) {
        mini_->hide();
        return;
    }
    const QRect r = table_->visualRect(table_->model()->index(miniView_, 0));
    if (!r.isValid()) {
        mini_->hide();
        return;
    }
    const QRect g(0, r.top(), table_->viewport()->width(), r.height());
    if (mini_->geometry() != g) mini_->setGeometry(g);
    if (!mini_->isVisible()) mini_->show();
}

void MainWindow::expandStandbyTransition()
{
    // A készenlétben álló sor; ha nincs (mindkét lejátszó szól), az első várakozó.
    for (const Row::State want : {Row::Cued, Row::Pending})
        for (const Row &r : rows_)
            if (r.state == want) {
                if (expandedRowId_ != r.id) toggleMini(r.id);
                return;
            }
}

bool MainWindow::miniOpen() const { return mini_ && miniView_ >= 0; }

MiniMixCanvas *MainWindow::miniCanvas() { return mini_ ? mini_->canvas() : nullptr; }

// ------------------------------------------------------- a sorok rajza

static QColor mixColor(const QColor &a, const QColor &b, double t)
{
    return QColor(int(a.red() + (b.red() - a.red()) * t), int(a.green() + (b.green() - a.green()) * t),
                  int(a.blue() + (b.blue() - a.blue()) * t));
}

static QString mmss(qint64 ms)
{
    ms = std::max<qint64>(0, ms);
    return QStringLiteral("%1:%2").arg(ms / 60000).arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'));
}

void MainWindow::paintPlaylistRow(QPainter *p, const QStyleOptionViewItem &opt, int v) const
{
    if (v < 0 || v >= view_.size()) return;
    const ViewRow &vr = view_[v];
    // A sorok listája és a nézet egy pillanatra eltérhet (a frissítés előtt): ilyenkor nincs mit rajzolni.
    if ((vr.kind == ViewRow::Item || vr.kind == ViewRow::Wave) && (vr.row < 0 || vr.row >= rows_.size())) return;
    if (vr.kind == ViewRow::Header && (vr.first < 0 || vr.first + vr.count > rows_.size() || vr.count <= 0)) return;
    const QRect r = opt.rect;
    const bool selected = opt.state & QStyle::State_Selected;
    static const QColor panel("#1b1e24"), line("#0f1114");
    QFont sans = opt.font, sansBold = opt.font, small = opt.font, mono("IBM Plex Mono"), monoBig("IBM Plex Mono"), monoSmall("IBM Plex Mono");
    sans.setPixelSize(12);
    sansBold.setPixelSize(14);
    sansBold.setWeight(QFont::DemiBold);
    small.setPixelSize(12);
    mono.setPixelSize(13);
    mono.setWeight(QFont::DemiBold);
    monoBig.setPixelSize(17);
    monoBig.setWeight(QFont::DemiBold);
    monoSmall.setPixelSize(10);
    p->save();
    p->setClipRect(r);

    if (vr.kind == ViewRow::Hour) {
        p->fillRect(r, QColor("#121418"));
        p->fillRect(QRect(r.left(), r.bottom(), r.width(), 1), QColor("#30353e"));
        const int key = vr.hourKey;
        const QDate d = QDate::fromString(QString::number(key / 100), "yyyyMMdd");
        const int hr = key % 100;
        QString text = QStringLiteral("%1:00 – %2:00").arg(hr, 2, 10, QLatin1Char('0')).arg((hr + 1) % 24, 2, 10, QLatin1Char('0'));
        if (d.isValid() && d != eter::now().date()) text += QStringLiteral("  ·  ") + i18n::locale().toString(d, TR("MMM d., dddd"));
        p->setFont(monoSmall);
        p->setPen(QColor("#c9ced6"));
        p->drawText(r.adjusted(12, 0, -12, 0), Qt::AlignLeft | Qt::AlignVCenter, text);
        p->restore();
        return;
    }
    if (vr.kind == ViewRow::Wave) {
        p->fillRect(r, QColor("#101215"));
        p->restore();
        return;
    }

    if (vr.kind == ViewRow::Header) {
        const Row &head = rows_[vr.first];
        const bool open = expanded_.contains(head.groupId);
        qint64 frames = 0;
        int done = 0, playing = -1, cued = -1, firstLive = vr.first;
        QString nowTitle;
        bool foundLive = false;
        for (int i = vr.first; i < vr.first + vr.count; ++i) {
            const Row &m = rows_[i];
            frames += m.frames;
            if (m.state == Row::Playing) { playing = m.player; nowTitle = m.item.title; }
            else if (m.state == Row::Cued) { if (cued < 0) cued = m.player; }
            else if (m.state != Row::Pending) ++done;
            if (!foundLive && (m.state == Row::Playing || m.state == Row::Cued || m.state == Row::Pending)) { firstLive = i; foundLive = true; }
        }
        QColor bg("#262b34"), stripe("#8a93a0"), statusColor("#c9ced6");
        QString status;
        if (playing >= 0) { status = TR("ADÁSBAN · %1/%2").arg(done + 1).arg(vr.count); bg = mixColor(panel, theme::amber, 0.42); stripe = theme::amber; statusColor = Qt::white; }
        else if (done == vr.count) { status = TR("lejátszva"); p->setOpacity(0.55); }
        else if (cued >= 0) { status = TR("KÉSZENLÉT · %1").arg(cued + 1); bg = mixColor(panel, theme::blue, 0.30); stripe = theme::blue; statusColor = Qt::white; }
        else if (done > 0) status = TR("%1/%2 lejátszva").arg(done).arg(vr.count);
        p->fillRect(r, bg);
        p->fillRect(QRect(r.left(), r.top(), 5, r.height()), stripe);
        p->fillRect(QRect(r.left(), r.bottom(), r.width(), 1), line);
        int x = r.left() + 13;
        p->setFont(mono);
        p->setPen(QColor("#e8eaed"));
        p->drawText(QRect(x, r.top(), 74, r.height()), Qt::AlignVCenter, firstLive < startText_.size() ? startText_[firstLive] : QString());
        x += 82;
        const int right = r.right() - 10;
        p->setFont(sans);
        p->setPen(statusColor);
        p->drawText(QRect(right - 118, r.top(), 118, r.height()), Qt::AlignVCenter, status);
        p->setFont(monoBig);
        p->setPen(QColor("#e8eaed"));
        p->drawText(QRect(right - 118 - 70, r.top(), 64, r.height()), Qt::AlignVCenter | Qt::AlignRight, theme::formatTime(frames));
        p->setFont(sans);
        p->setPen(QColor("#9aa3af"));
        p->drawText(QRect(right - 118 - 70 - 80, r.top(), 74, r.height()), Qt::AlignVCenter | Qt::AlignRight, TR("%1 elem").arg(vr.count));
        QString title = QStringLiteral("%1  %2").arg(open ? QStringLiteral("▾") : QStringLiteral("▸"), head.groupName);
        if (!open && !nowTitle.isEmpty()) title += TR("  –  most: %1").arg(nowTitle);
        p->setFont(sansBold);
        p->setPen(QColor("#f3f4f6"));
        const int tw = right - 118 - 70 - 80 - x - 8;
        p->drawText(QRect(x, r.top(), tw, r.height()), Qt::AlignVCenter, QFontMetrics(sansBold).elidedText(title, Qt::ElideRight, tw));
        if (selected) {
            p->setOpacity(1.0);
            p->fillRect(r, QColor(255, 255, 255, 26));
            p->setPen(QColor(255, 255, 255, 170));
            p->setBrush(Qt::NoBrush);                      // a típusikon ecsetje ne töltse ki a sort
        p->drawRect(r.adjusted(0, 0, -1, -1));
        }
        p->restore();
        return;
    }

    // Elem
    const int i = vr.row;
    const Row &row = rows_[i];
    const bool member = row.groupId && vr.count > 0;
    QColor cat(row.item.color);
    if (!cat.isValid()) cat = QColor("#8a93a0");
    const bool strong = row.item.kind != QLatin1String("music");
    QColor bg = mixColor(panel, cat, strong ? 0.34 : 0.16), stripe = cat, statusColor("#c9ced6");
    QString status;
    bool dim = false;
    switch (row.state) {
    case Row::Playing:
        bg = mixColor(panel, theme::amber, 0.42);
        stripe = theme::amber;
        status = TR("ADÁSBAN · %1").arg(row.player + 1);
        statusColor = Qt::white;
        break;
    case Row::Cued:
        bg = mixColor(panel, theme::blue, 0.30);
        stripe = theme::blue;
        status = TR("KÉSZENLÉT · %1").arg(row.player + 1);
        statusColor = Qt::white;
        break;
    case Row::Pending:
        if (row.fixedAt.isValid()) { status = TR("FIX %1").arg(row.fixedAt.toString("HH:mm:ss")); statusColor = theme::amberSoft; }
        break;
    case Row::Played: status = TR("lejátszva"); dim = true; break;
    case Row::Skipped: status = TR("kihagyva"); dim = true; break;
    case Row::Failed:
        bg = mixColor(panel, QColor("#e5484d"), 0.22);
        stripe = QColor("#e5484d");
        status = TR("hibás fájl");
        statusColor = QColor("#ffb4ae");
        break;
    }
    static const QColor purple("#b99cff");
    const bool ovlActive = row.overlay && (row.overlayVoice >= 0 || overlayActive(i));
    if (ovlActive && row.state == Row::Playing) {
        bg = mixColor(panel, purple, 0.45);
        stripe = purple;
        status = TR("RÁFEKTETVE · SZÓL");
        statusColor = Qt::white;
    } else if (ovlActive) {
        bg = mixColor(panel, purple, 0.20);
        stripe = purple;
        status = TR("RÁFEKTETVE");
        statusColor = QColor("#ddd2ff");
    }
    if (dim) p->setOpacity(0.55);
    p->fillRect(r, bg);
    int x = r.left();
    if (member) {                                       // blokk tagja: szürke sáv és behúzás
        p->fillRect(QRect(x, r.top(), 3, r.height()), QColor("#8a93a0"));
        x += 8;
    }
    if (row.overlay && (ovlActive || dim)) {                // ráfektetett elem: behúzva, a két szomszédja közé kötve
        p->fillRect(QRect(x + 3, r.top(), 2, r.height()), mixColor(panel, purple, 0.7));
        x += 12;
    }
    p->fillRect(QRect(x, r.top(), 5, r.height()), stripe);
    p->fillRect(QRect(r.left(), r.bottom(), r.width(), 1), line);
    x += 13;
    const QColor sub("#c3c8d0");
    // 1. kezdés és jelölő
    p->setFont(mono);
    p->setPen(QColor("#e8eaed"));
    p->drawText(QRect(x, r.top() + 7, 80, 18), Qt::AlignLeft | Qt::AlignVCenter, i < startText_.size() ? startText_[i] : QString());
    p->setFont(monoSmall);
    p->setPen(sub);
    QString note = row.item.introMs > 0 ? TR("intro %1").arg(mmss(row.item.introMs)) : row.item.categoryName;
    if (row.overlay)
        note = TR("mix %1 mp").arg((row.overlayOffsetMs > 0 ? QStringLiteral("+") : row.overlayOffsetMs < 0 ? QStringLiteral("−") : QString())
                                   + i18n::locale().toString(std::llabs(row.overlayOffsetMs) / 1000.0, 'f', 1));
    p->drawText(QRect(x, r.top() + 26, 80, 14), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(monoSmall).elidedText(note, Qt::ElideRight, 76));
    x += 82;
    // 2. típusikon
    {
        p->setRenderHint(QPainter::Antialiasing, true);
        const QRectF box(x + 2, r.center().y() - 13, 26, 26);
        p->setPen(Qt::NoPen);
        p->setBrush(cat);
        p->drawRoundedRect(box, 6, 6);
        const QString k = row.item.kind;
        const QString glyph = k == QLatin1String("music") ? QStringLiteral("♪") : k == QLatin1String("jingle") ? QStringLiteral("✦")
                            : k == QLatin1String("news") ? QStringLiteral("≡") : k == QLatin1String("ad") ? QStringLiteral("◆")
                            : k == QLatin1String("voicetrack") ? QStringLiteral("●") : QStringLiteral("•");
        QFont g = sansBold;
        g.setPixelSize(15);
        p->setFont(g);
        p->setPen(QColor("#101215"));
        p->drawText(box, Qt::AlignCenter, glyph);
        p->setBrush(Qt::NoBrush);
        p->setRenderHint(QPainter::Antialiasing, false);
    }
    x += 36;
    // 3. hossz és hátralévő idő
    p->setFont(monoBig);
    p->setPen(Qt::white);
    p->drawText(QRect(x, r.top() + 5, 64, 22), Qt::AlignLeft | Qt::AlignVCenter, row.frames > 0 ? theme::formatTime(row.frames) : QStringLiteral("?"));
    double progress = -1;
    if (row.state == Row::Playing && row.overlayVoice >= 0) {
        auto &src = const_cast<eter::Engine &>(engine_).source(static_cast<eter::Engine::Channel>(eter::Engine::Ovl1 + row.overlayVoice));
        const qint64 dur = src.duration(), pos = src.position();
        if (dur > 0) {
            progress = std::clamp(double(pos) / double(dur), 0.0, 1.0);
            p->setFont(monoSmall);
            p->setPen(QColor("#ece6ff"));
            p->drawText(QRect(x, r.top() + 27, 64, 14), Qt::AlignLeft | Qt::AlignVCenter,
                        QStringLiteral("−") + mmss((dur - pos) * 1000 / eter::kRate));
        }
    } else if (row.state == Row::Playing && row.player >= 0) {
        const PlayerWidget *pw = players_[row.player];
        const qint64 dur = pw->duration(), pos = pw->position();
        if (dur > 0) {
            progress = std::clamp(double(pos) / double(dur), 0.0, 1.0);
            p->setFont(monoSmall);
            p->setPen(QColor("#ffe7b8"));
            p->drawText(QRect(x, r.top() + 27, 64, 14), Qt::AlignLeft | Qt::AlignVCenter,
                        QStringLiteral("−") + mmss((dur - pos) * 1000 / eter::kRate));
        }
    }
    x += 66;
    // a jobb oldal: nyíl, állapot, jelzések
    int right = r.right() - 4;
    const bool canExpand = nextPlayableAfter(i) >= 0;
    if (canExpand) {
        const bool open = row.id == expandedRowId_;
        p->setFont(sansBold);
        p->setPen(open ? Qt::white : QColor("#9aa3af"));
        p->drawText(QRect(right - 26, r.top(), 26, r.height()), Qt::AlignCenter, open ? QStringLiteral("▾") : QStringLiteral("▸"));
    }
    right -= 30;
    p->setFont(small);
    p->setPen(statusColor);
    QFont st = small;
    st.setWeight(QFont::DemiBold);
    p->setFont(st);
    p->drawText(QRect(right - 112, r.top(), 112, r.height()), Qt::AlignVCenter | Qt::AlignLeft, status);
    right -= 118;
    QStringList chips;
    QVector<QColor> chipColors;
    if (row.fixedAt.isValid()) { chips << QStringLiteral("FIX"); chipColors << theme::amber; }
    if (vtFrom_.contains(row.id)) { chips << QStringLiteral("VT"); chipColors << QColor("#7fd6e8"); }
    if (customFrom_.contains(row.id)) { chips << QStringLiteral("MIX"); chipColors << QColor("#9aa3af"); }
    if (row.overlay && !ovlActive && !dim) { chips << TR("RÁF"); chipColors << purple; }   // jelölve, de nincs mire ráfeküdnie
    p->setFont(monoSmall);
    for (int c = 0; c < chips.size(); ++c) {
        const int w = QFontMetrics(monoSmall).horizontalAdvance(chips[c]) + 12;
        const QRect box(right - w, r.center().y() - 8, w, 16);
        p->fillRect(box, mixColor(panel, chipColors[c], 0.55));
        p->setPen(Qt::white);
        p->drawText(box, Qt::AlignCenter, chips[c]);
        right -= w + 4;
    }
    // 4. cím és előadó
    const int tw = std::max(40, right - 8 - x);
    p->setFont(sansBold);
    p->setPen(QColor("#f3f4f6"));
    p->drawText(QRect(x, r.top() + 6, tw, 20), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(sansBold).elidedText(row.item.title, Qt::ElideRight, tw));
    p->setFont(sans);
    p->setPen(sub);
    p->drawText(QRect(x, r.top() + 26, tw, 16), Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(sans).elidedText(row.item.artist, Qt::ElideRight, tw));
    // haladás
    if (progress >= 0) {
        p->fillRect(QRect(r.left(), r.bottom() - 3, r.width(), 3), QColor(0, 0, 0, 90));
        p->fillRect(QRect(r.left(), r.bottom() - 3, int(r.width() * progress), 3), Qt::white);
    }
    if (selected) {
        p->setOpacity(1.0);
        p->fillRect(r, QColor(255, 255, 255, 26));
        p->setPen(QColor(255, 255, 255, 170));
        p->setBrush(Qt::NoBrush);                      // a típusikon ecsetje ne töltse ki a sort
        p->drawRect(r.adjusted(0, 0, -1, -1));
    }
    p->restore();
}

// ------------------------------------------------------------ időzítések

// Az esedékes időzített események végrehajtása. Egy alkalom csak egyszer fut le, és csak akkor,
// ha az időpontja legfeljebb egy perce múlt el (ha a program akkor nem futott, kimarad).
void MainWindow::runTimedEvents()
{
    const QDateTime now = eter::now();
    for (const TimedEvent &e : QVector<TimedEvent>(events_)) {
        if (!e.enabled) continue;
        const QDateTime at = TimedEventsPage::nextOccurrence(e, now.addSecs(-60));
        if (!at.isValid() || at > now) continue;
        const qint64 occ = at.toSecsSinceEpoch();
        if (e.lastFiredAt >= occ) continue;
        db_->markEventFired(e.id, occ, e.date.isValid());     // egyszeri esemény után kikapcsol
        events_ = db_->timedEvents();
        fireTimedEvent(e);
    }
    // Az időzítve indítandó elem indítása, amint a készenlétben álló lejátszóba került.
    if (pendingStartRowId_ >= 0) {
        const int standby = standbyPlayer();
        if (standby >= 0 && players_[standby]->rowId == pendingStartRowId_) {
            pendingStartRowId_ = -1;
            int onAir = -1;
            for (int i = 0; i < 2; ++i)
                if (players_[i]->isPlaying() && !leaving(i)) onAir = i;
            startStandby(onAir, fadeMs_);
        } else if (!rowById(pendingStartRowId_) || now > pendingStartUntil_) {
            pendingStartRowId_ = -1;
            say(TR("Az időzített elem nem indulhatott el: egyik lejátszó sem szabadult fel időben."), 10000);
        }
    }
}

void MainWindow::fireTimedEvent(const TimedEvent &e)
{
    QVector<int> ids;
    QString name = e.label;
    int groupId = 0;
    if (e.itemId) {
        ids << e.itemId;
    } else {
        for (const Item &it : db_->savedPlaylistItems(e.playlistId)) ids << it.id;
        groupId = db_->nextGroupId();
    }
    if (ids.isEmpty()) {
        say(TR("Időzítés: „%1” üres vagy már nincs meg, kimaradt.").arg(name), 10000);
        return;
    }
    const int before = rows_.size();
    // „Következőként” és „Azonnal indul”: a szóló elem mögé, a várakozók elé.
    const int at = e.action == QLatin1String("end") ? -1 : firstMovable();
    appendItems(ids, at, groupId, name);
    if (rows_.size() == before) return;
    const int first = at;                                   // a beszúrt első elem helye (a végére kerülőnél nem kell)
    if (e.action == QLatin1String("start") && first >= 0) {
        pendingStartRowId_ = rows_[first].id;
        pendingStartUntil_ = eter::now().addSecs(30);
        say(TR("Időzítés: „%1” indul.").arg(name), 8000);
        runTimedEvents();                                   // ha a lejátszó szabad, azonnal indul
    } else {
        say(e.action == QLatin1String("end") ? TR("Időzítés: „%1” a playlist végére került.").arg(name)
                                             : TR("Időzítés: „%1” következőként a playlistbe került.").arg(name), 8000);
    }
}

// ------------------------------------------------------------ követés

int MainWindow::followTargetView() const
{
    // Az adásban lévő elem; ha kettő szól (átúsztatás), az újabb. Ha semmi sem szól, a készenlétben álló.
    int index = -1;
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_[i].state == Row::Playing) index = i;
    if (index < 0)
        for (int i = 0; i < rows_.size() && index < 0; ++i)
            if (rows_[i].state == Row::Cued) index = i;
    if (index < 0) return -1;
    for (int v = 0; v < view_.size(); ++v) {
        if (view_[v].kind == ViewRow::Item && view_[v].row == index) return v;
        if (view_[v].kind == ViewRow::Header && index >= view_[v].first && index < view_[v].first + view_[v].count
            && !expanded_.contains(rows_[index].groupId))
            return v;                                       // becsukott blokkban: a fejléce
    }
    return -1;
}

bool MainWindow::followTargetVisible() const
{
    const int v = followTargetView();
    if (v < 0) return true;
    const QRect r = table_->visualRect(table_->model()->index(v, 0));
    return r.isValid() && r.bottom() > 0 && r.top() < table_->viewport()->height();
}

// Ha a követés be van kapcsolva, az adásban lévő elem a lista tetejére kerül (előtte egy sorral),
// amikor új elem indul, vagy amikor kikerülne a látható részből.
void MainWindow::followPlaying()
{
    if (!follow_ || !table_) return;
    const int v = followTargetView();
    if (v < 0) return;
    const int rowIndex = view_[v].kind == ViewRow::Item ? view_[v].row : view_[v].first;
    const int id = rowIndex < rows_.size() ? rows_[rowIndex].id : -1;
    if (id == followedRowId_ && followTargetVisible()) return;
    followedRowId_ = id;
    programmaticScroll_ = true;
    table_->scrollTo(table_->model()->index(std::max(0, v - 1), 0), QAbstractItemView::PositionAtTop);
    programmaticScroll_ = false;
}

void MainWindow::updateClock()
{
    const QDateTime now = eter::now();
    clock_->setText(now.toString("HH:mm:ss"));
    date_->setText(i18n::locale().toString(now.date(), TR("yyyy. MMM d., dddd")));
    const int left = 3600 - (now.time().minute() * 60 + now.time().second());
    toHour_->setText(QStringLiteral("%1:%2").arg(left / 60).arg(left % 60, 2, 10, QLatin1Char('0')));
}

// A készenlétben álló lejátszó; ha mindkettő áll, a playlistben előrébb lévő.
int MainWindow::standbyPlayer() const
{
    QVector<int> idle;
    for (int i = 0; i < 2; ++i)
        if (players_[i]->isLoaded() && !players_[i]->isPlaying()) idle << i;
    if (idle.size() == 2) return indexOfRow(players_[0]->rowId) <= indexOfRow(players_[1]->rowId) ? 0 : 1;
    return idle.size() == 1 ? idle[0] : -1;
}

void MainWindow::startStandby(int fadeOutPlayer, int fadeMs)
{
    const int standby = standbyPlayer();
    if (standby < 0) return;
    waitingForFixed_ = false;
    const bool hasOut = fadeOutPlayer >= 0 && fadeOutPlayer != standby;
    const MixPlan plan = planFor(hasOut ? fadeOutPlayer : -1, standby);
    PlayerWidget *in = players_[standby];
    if (plan.cueInFrames >= 0 && plan.cueInFrames != in->cueFrame()) in->cueTo(plan.cueInFrames);
    if (plan.fadeInMs > 0) in->playMixed(plan.fadeInMs, plan.startLevelDb);
    else in->play();
    if (!hasOut) return;
    // Voice track, ha az átmenethez tartozik, és még nem indult el a mix pont előtt.
    if (plan.vtItemId && in->rowId != vtDoneTo_ && !vtPlaying_) {
        vtDoneTo_ = in->rowId;
        if (plan.vtOffsetMs <= 0) {
            startVoiceTrack(plan.vtItemId, plan.vtDuckDb);
        } else {
            vtPending_.toRowId = in->rowId;
            vtPending_.player = standby;
            vtPending_.startFrame = in->cueFrame() + plan.vtOffsetMs * eter::kRate / 1000;
            vtPending_.itemId = plan.vtItemId;
            vtPending_.duckDb = plan.vtDuckDb;
        }
    }
    PlayerWidget *out = players_[fadeOutPlayer];
    const qint64 leftMs = std::max<qint64>(0, out->duration() - out->position()) * 1000 / eter::kRate;
    // Beállított végpont: a kimenő elem addig szól tovább, és ott úszik ki. Ha a váltás jóval a mix pont
    // előtt történik (kézi indítás, fix időpont), a kimenő elem azonnal kiúszik, mint eddig.
    const bool atMixPoint = plan.mixOutFrames >= 0 && out->position() >= plan.mixOutFrames - eter::kRate;
    if (plan.outEndFrames >= 0 && plan.fadeOutMs >= 0 && atMixPoint) {
        OutFade &f = outFade_[fadeOutPlayer];
        f.rowId = out->rowId;
        f.endFrame = std::min(plan.outEndFrames, out->duration());
        f.fadeMs = plan.fadeOutMs;
        f.startFrame = f.endFrame - qint64(plan.fadeOutMs) * eter::kRate / 1000;
        f.curve = plan.curve;
        runOutFades();
        return;
    }
    if (plan.fadeOutMs >= 0) {
        // Beállított kiúsztatás; 0 esetén a kimenő elem a saját végéig szól.
        const int ms = static_cast<int>(std::min<qint64>(plan.fadeOutMs, leftMs));
        if (ms > 0) out->fadeOut(ms, plan.curve);
    } else {
        out->fadeOut(static_cast<int>(std::clamp<qint64>(std::min<qint64>(fadeMs, leftMs), 50, 60000)), plan.curve);
    }
}

bool MainWindow::leaving(int player) const
{
    const PlayerWidget *p = players_[player];
    return p->isFading() || (outFade_[player].rowId >= 0 && outFade_[player].rowId == p->rowId);
}

// A beállított végpontjukig szóló kimenő elemek kiúsztatása; a 30 ms-os időzítő hívja.
void MainWindow::runOutFades()
{
    for (int i = 0; i < 2; ++i) {
        OutFade &f = outFade_[i];
        if (f.rowId < 0) continue;
        PlayerWidget *p = players_[i];
        if (p->rowId != f.rowId || !p->isPlaying() || p->isFading()) {
            f.rowId = -1;
            continue;
        }
        const qint64 pos = p->position();
        const qint64 leftMs = std::max<qint64>(0, f.endFrame - pos) * 1000 / eter::kRate;
        if (f.fadeMs > 0 && pos >= f.startFrame) {
            f.rowId = -1;
            p->fadeOut(static_cast<int>(std::max<qint64>(30, leftMs)), f.curve);
        } else if (f.fadeMs <= 0 && leftMs <= 30) {
            // Kiúsztatás nélküli vég: ha az elem a saját vége előtt áll meg, rövid lecsengés, hogy ne kattanjon.
            f.rowId = -1;
            if (f.endFrame < p->duration() - eter::kRate / 20) p->fadeOut(30, f.curve);
        }
    }
}

// Az adásban szóló voice track indítása és lezárása; a 30 ms-os időzítő hívja.
void MainWindow::runVoiceTrack()
{
    auto &vt = engine_.source(Engine::Vt);
    if (vtPlaying_) {
        float l, r;
        vt.takePeaks(l, r);
        if (vt.takeFinished() || !vt.playing()) endVoiceTrack("finished");
        return;
    }
    // A mix pont után induló voice track: a bejövő elem megfelelő pontján.
    if (vtPending_.toRowId >= 0) {
        PlayerWidget *p = vtPending_.player >= 0 ? players_[vtPending_.player] : nullptr;
        if (!p || p->rowId != vtPending_.toRowId || !p->isPlaying()) {
            vtPending_.toRowId = -1;
        } else if (p->position() >= vtPending_.startFrame) {
            vtPending_.toRowId = -1;
            startVoiceTrack(vtPending_.itemId, vtPending_.duckDb);
        }
        return;
    }
    // A mix pont előtt induló voice track: csak automata üzemmódban tudjuk előre, mikor jön a váltás.
    if (mode_ != Auto) return;
    int onAir = -1, playing = 0;
    for (int i = 0; i < 2; ++i) {
        if (!players_[i]->isPlaying()) continue;
        ++playing;
        if (!players_[i]->isFading()) onAir = i;
    }
    const int standby = standbyPlayer();
    if (onAir < 0 || playing != 1 || standby < 0) return;
    const MixPlan plan = planFor(onAir, standby);
    const int toId = players_[standby]->rowId;
    if (!plan.vtItemId || plan.vtOffsetMs >= 0 || plan.mixOutFrames < 0 || toId == vtDoneTo_) return;
    if (players_[onAir]->position() >= plan.mixOutFrames + plan.vtOffsetMs * eter::kRate / 1000) {
        vtDoneTo_ = toId;
        startVoiceTrack(plan.vtItemId, plan.vtDuckDb);
    }
}

void MainWindow::startVoiceTrack(int itemId, double duckDb)
{
    const Item item = db_->item(itemId);
    if (!item.id) return;
    auto &vt = engine_.source(Engine::Vt);
    std::string err;
    if (!vt.load(item.path.toStdString(), &err)) {
        say(TR("A voice track nem tölthető be: %1").arg(QString::fromStdString(err)), 9000);
        return;
    }
    vt.play();
    vtDuckGain_ = std::pow(10.0, duckDb / 20.0);
    vtPlaying_ = true;
    applyDuck();
    vtLogId_ = db_->logStart(item, 0, 1, mode_ == Auto ? "auto" : "assist");
    say(TR("Voice track szól: %1").arg(item.title), 0);
}

void MainWindow::endVoiceTrack(const QString &reason)
{
    engine_.source(Engine::Vt).unload();
    db_->logEnd(vtLogId_, reason);
    vtLogId_ = 0;
    vtPlaying_ = false;
    applyDuck();
    statusBar()->clearMessage();
}

MainWindow::MixPlan MainWindow::planFor(int outPlayer, int inPlayer)
{
    MixPlan plan;
    const Row *outRow = outPlayer >= 0 ? rowById(players_[outPlayer]->rowId) : nullptr;
    const Row *inRow = inPlayer >= 0 ? rowById(players_[inPlayer]->rowId) : nullptr;
    const int from = outRow ? outRow->id : -1, to = inRow ? inRow->id : -1;
    // Az adatbázist csak akkor kérdezzük, ha a pár vagy a mentett mixek megváltoztak.
    if (from != planFrom_ || to != planTo_ || planVersion_ != mixVersion_) {
        planFrom_ = from;
        planTo_ = to;
        planVersion_ = mixVersion_;
        planTransition_ = (from > 0 && to > 0) ? db_->transition(to) : Transition();
        if (planTransition_.fromEntryId != from) planTransition_ = Transition();
    }
    if (planTransition_.toEntryId) {
        const Transition &t = planTransition_;
        plan.mixOutFrames = t.mixOutMs * eter::kRate / 1000;
        plan.fadeOutMs = static_cast<int>(t.fadeOutMs);
        if (t.outEndMs > 0) plan.outEndFrames = t.outEndMs * eter::kRate / 1000;
        plan.curve = curveFromName(t.fadeOutCurve);
        plan.cueInFrames = t.cueInMs * eter::kRate / 1000;
        plan.fadeInMs = static_cast<int>(t.fadeInMs);
        plan.startLevelDb = t.startLevelDb;
        plan.vtItemId = t.vtItemId;
        plan.vtOffsetMs = t.vtOffsetMs;
        plan.vtDuckDb = t.vtDuckDb;
        return plan;
    }
    if (outRow) {
        const Item &it = outRow->item;
        plan.mixOutFrames = it.userMixOutMs > 0 ? it.userMixOutMs * eter::kRate / 1000 : players_[outPlayer]->mixOutFrame();
        if (it.fadeOutMs >= 0) plan.fadeOutMs = static_cast<int>(it.fadeOutMs);
        if (!it.fadeCurve.isEmpty()) plan.curve = curveFromName(it.fadeCurve);
        if (it.cueOutMs > 0 && it.userMixOutMs > 0) plan.outEndFrames = it.cueOutMs * eter::kRate / 1000;
    }
    return plan;
}

void MainWindow::updateMixPairs()
{
    if (!mixPage_) return;
    QVector<MixPair> pairs;
    int prev = -1, ovl = -1;
    for (int i = 0; i < rows_.size() && pairs.size() < 60; ++i) {
        const Row &r = rows_[i];
        if (r.state != Row::Playing && r.state != Row::Cued && r.state != Row::Pending) continue;
        // A ráfektetett elem nem alkot párt: a két szomszédja közti átmenethez tartozik.
        if (r.overlay && (r.overlayVoice >= 0 || overlayActive(i))) {
            if (r.state == Row::Pending && ovl < 0) ovl = i;
            continue;
        }
        if (prev >= 0) {
            MixPair p;
            if (ovl >= 0) {
                p.ovlEntry = rows_[ovl].id;
                p.ovlItem = rows_[ovl].item;
                p.ovlOffsetMs = rows_[ovl].overlayOffsetMs;
                p.ovlDuckDb = rows_[ovl].overlayDuckDb;
            }
            p.fromEntry = rows_[prev].id;
            p.toEntry = r.id;
            p.from = rows_[prev].item;
            p.to = r.item;
            p.fromOnAir = rows_[prev].state == Row::Playing;
            p.toOnAir = r.state == Row::Playing;
            if (i < startText_.size()) p.startText = startText_[i];
            pairs.push_back(p);
        }
        prev = i;
        ovl = -1;
    }
    mixPage_->setPairs(pairs);
}

void MainWindow::onMixSaved(const QVector<int> &itemIds)
{
    ++mixVersion_;
    for (Row &row : rows_)
        if (itemIds.contains(row.item.id)) row.item = db_->item(row.item.id);
    // A készenlétben álló lejátszó az új kezdőpontra áll.
    for (int p = 0; p < 2; ++p) {
        if (!players_[p]->isLoaded()) continue;
        const Row *row = rowById(players_[p]->rowId);
        if (!row || !itemIds.contains(row->item.id)) continue;
        players_[p]->setIntro(row->item.introMs * eter::kRate / 1000);
        if (!players_[p]->isPlaying()) players_[p]->cueTo(row->item.cueInMs * eter::kRate / 1000);
    }
    refreshTable();
}

void MainWindow::next()
{
    int onAir = -1;
    for (int i = 0; i < 2; ++i)
        if (players_[i]->isPlaying() && !leaving(i)) onAir = i;
    if (standbyPlayer() < 0) {
        say(TR("Nincs készenlétben álló lejátszó."));
        return;
    }
    startStandby(onAir, fadeMs_);
}

// Igaz, ha a soron következő elem fix idejű, és még nem jött el az ideje.
bool MainWindow::standbyIsEarlyFixed()
{
    const int p = standbyPlayer();
    if (p < 0) return false;
    const Row *row = rowById(players_[p]->rowId);
    return row && row->fixedAt.isValid() && row->fixedAt > eter::now();
}

void MainWindow::stopAll()
{
    waitingForFixed_ = false;
    vtPending_.toRowId = -1;
    if (vtPlaying_) endVoiceTrack("stopped");
    for (int v = 0; v < Engine::kOverlayVoices; ++v) endOverlay(v, "stopped");
    for (auto *p : players_) p->stop();
    if (cartPanel_) cartPanel_->stopAll();
}

void MainWindow::onPlayerFinished(int player)
{
    if (Row *row = rowById(players_[player]->rowId)) {
        db_->logEnd(row->logId, players_[player]->finishReason());
        row->logId = 0;
        row->state = Row::Played;
        row->player = -1;
        db_->setEntryStatus(row->id, "played");
    }
    players_[player]->eject();
    trimHistory();
    reconcile();
    refreshTable();

    // Automata: ha a mix pontnál nem volt mit indítani, vagy a hullámforma még nem készült el.
    if (mode_ == Auto && !players_[0]->isPlaying() && !players_[1]->isPlaying()) {
        if (standbyIsEarlyFixed()) {
            waitingForFixed_ = true;         // a fix idejű elem nem indul a saját időpontja előtt
            say(TR("Várakozás a fix idejű elemre."), 0);
        } else if (standbyPlayer() >= 0) startStandby(-1, 0);
        else say(TR("A playlist a végére ért."), 0);
    }
}

void MainWindow::onPlayerEjected(int player)
{
    for (Row &row : rows_) {
        if (row.player == player && (row.state == Row::Cued || row.state == Row::Playing)) {
            db_->logEnd(row.logId, "stopped");
            row.logId = 0;
            row.state = Row::Skipped;
            row.player = -1;
            db_->setEntryStatus(row.id, "skipped");
        }
    }
    reconcile();
    refreshTable();
}

void MainWindow::syncRowStates()
{
    bool changed = false;
    for (Row &row : rows_) {
        if (row.player < 0 || (row.state != Row::Cued && row.state != Row::Playing)) continue;
        PlayerWidget *p = players_[row.player];
        const Row::State s = p->isPlaying() ? Row::Playing : Row::Cued;
        if (s == row.state) continue;
        row.state = s;
        changed = true;
        db_->setEntryStatus(row.id, s == Row::Playing ? "playing" : "pending");
        if (s == Row::Playing) {
            row.startedAt = eter::now().addMSecs(-p->position() * 1000 / eter::kRate);
            if (!row.logId)
                row.logId = db_->logStart(row.item, row.id, row.player + 1, mode_ == Auto ? "auto" : "assist");
        } else if (p->position() == p->cueFrame()) {
            // Megállították és az elejére állt: ez a lejátszás lezárult.
            db_->logEnd(row.logId, "stopped");
            row.logId = 0;
            row.startedAt = {};
        }
    }
    if (changed) refreshTable();
}

// --------------------------------------------------------------- playlist

MainWindow::Row *MainWindow::rowById(int id)
{
    for (Row &r : rows_)
        if (r.id == id) return &r;
    return nullptr;
}

int MainWindow::indexOfRow(int id) const
{
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_[i].id == id) return i;
    return rows_.size();
}

int MainWindow::selectedRowId() const
{
    const QTableWidgetItem *item = table_->item(table_->currentRow(), 0);
    return item && item->data(kRowIdRole).isValid() ? item->data(kRowIdRole).toInt() : -1;
}

int MainWindow::selectedHeaderRowId() const
{
    const QTableWidgetItem *item = table_->item(table_->currentRow(), 0);
    return item && item->data(kHeadIdRole).isValid() ? item->data(kHeadIdRole).toInt() : -1;
}

// A táblázat sorai: az egymást követő, azonos blokkba tartozó sorok fölé fejléc kerül;
// a becsukott blokk tagjai nem látszanak.
void MainWindow::rebuildView()
{
    view_.clear();
    int lastKey = -1;
    auto item = [](int i, int first = 0, int count = 0) {
        ViewRow v;
        v.kind = ViewRow::Item;
        v.row = i;
        v.first = first;
        v.count = count;
        return v;
    };
    auto hour = [&](int i) {
        if (rows_[i].playlistKey == lastKey) return;
        lastKey = rows_[i].playlistKey;
        ViewRow h;
        h.kind = ViewRow::Hour;
        h.first = i;
        h.hourKey = lastKey;
        view_.push_back(h);
    };
    auto wave = [&](int i) {
        if (rows_[i].id != expandedRowId_ || nextPlayableAfter(i) < 0) return;
        ViewRow w;
        w.kind = ViewRow::Wave;
        w.row = i;
        view_.push_back(w);
    };
    for (int i = 0; i < rows_.size();) {
        const int g = rows_[i].groupId;
        if (!g) {
            hour(i);
            view_.push_back(item(i));
            wave(i);
            ++i;
            continue;
        }
        int n = 1;
        while (i + n < rows_.size() && rows_[i + n].groupId == g) ++n;
        hour(i);
        ViewRow h;
        h.kind = ViewRow::Header;
        h.first = i;
        h.count = n;
        view_.push_back(h);
        if (expanded_.contains(g)) {
            for (int k = 0; k < n; ++k) {
                view_.push_back(item(i + k, i, n));
                wave(i + k);
            }
        }
        lastKey = rows_[i + n - 1].playlistKey;              // a blokk belsejében nincs óraelválasztó
        i += n;
    }
}

// A táblázat egy beszúrási helyéhez tartozó index a sorok között.
int MainWindow::insertIndexForView(int viewRow) const
{
    if (viewRow < 0 || viewRow >= view_.size()) return rows_.size();
    const ViewRow &vr = view_[viewRow];
    if (vr.kind == ViewRow::Item) return vr.row;
    if (vr.kind == ViewRow::Wave) return vr.row + 1;
    return vr.first;
}

void MainWindow::selectRowId(int rowId)
{
    const int index = indexOfRow(rowId);
    int header = -1;
    for (int v = 0; v < view_.size(); ++v) {
        if (view_[v].kind == ViewRow::Item && view_[v].row == index) { table_->setCurrentCell(v, 0); return; }
        if (view_[v].kind == ViewRow::Header && index >= view_[v].first && index < view_[v].first + view_[v].count) header = v;
    }
    if (header >= 0) table_->setCurrentCell(header, 0);
}

void MainWindow::toggleGroup(int groupId)
{
    if (!groupId) return;
    if (expanded_.contains(groupId)) expanded_.remove(groupId);
    else expanded_.insert(groupId);
    refreshTable();
}

void MainWindow::insertSavedPlaylist(int savedPlaylistId, int at)
{
    QString name;
    for (const SavedPlaylist &p : db_->savedPlaylists())
        if (p.id == savedPlaylistId) name = p.name;
    QVector<int> ids;
    for (const Item &it : db_->savedPlaylistItems(savedPlaylistId)) ids << it.id;
    if (ids.isEmpty()) {
        say(TR("Ez a playlist üres, nincs mit beilleszteni."));
        return;
    }
    appendItems(ids, at, db_->nextGroupId(), name);
    say(TR("„%1” beillesztve a playlistbe blokként (%2 elem). A fejlécére kattintva kinyitható.").arg(name).arg(ids.size()));
}

// A táblázatban áthúzott sor: sima sor vagy egy egész blokk (a fejlécénél fogva).
void MainWindow::onViewRowMoved(int from, int to)
{
    if (from < 0 || from >= view_.size()) return;
    const ViewRow vr = view_[from];
    const int index = insertIndexForView(to);
    const bool ontoHeader = to >= 0 && to < view_.size() && (view_[to].kind == ViewRow::Header || view_[to].kind == ViewRow::Hour);
    if (vr.kind == ViewRow::Item) moveRow(vr.row, index, ontoHeader);
    else if (vr.kind == ViewRow::Header) moveBlock(vr.first, vr.count, index);
}

void MainWindow::ungroup(int first, int count)
{
    for (int i = first; i < first + count && i < rows_.size(); ++i) {
        rows_[i].groupId = 0;
        rows_[i].groupName.clear();
        db_->setEntryGroup(rows_[i].id, 0, {});
    }
    refreshTable();
}

void MainWindow::playlistContextMenu(const QPoint &pos)
{
    const int v = table_->rowAt(pos.y());
    if (v < 0 || v >= view_.size()) return;
    table_->setCurrentCell(v, 0);
    const ViewRow vr = view_[v];
    if (vr.kind == ViewRow::Hour || vr.kind == ViewRow::Wave) return;
    QMenu menu(this);
    if (vr.kind == ViewRow::Header) {
        const int g = rows_[vr.first].groupId;
        menu.addAction(expanded_.contains(g) ? TR("Blokk becsukása") : TR("Blokk kinyitása"), this, [this, g] { toggleGroup(g); });
        menu.addAction(TR("Blokk felbontása (az elemek maradnak)"), this, [this, vr] { ungroup(vr.first, vr.count); });
        menu.addAction(TR("Blokk törlése a playlistből"), this, &MainWindow::removeSelected);
    } else {
        menu.addAction(TR("Betöltés az 1-es lejátszóba"), this, [this] { loadRowInto(selectedRowId(), 0); });
        menu.addAction(TR("Betöltés a 2-es lejátszóba"), this, [this] { loadRowInto(selectedRowId(), 1); });
        menu.addAction(TR("Elem szerkesztése…"), this, [this, id = rows_[vr.row].item.id] { openItemEditor(id); });
        if (nextPlayableAfter(vr.row) >= 0)
            menu.addAction(rows_[vr.row].id == expandedRowId_ ? TR("Átmenet bezárása") : TR("Átmenet a következővel (lenyitás)"), this,
                           [this, id = rows_[vr.row].id] { toggleMini(id); });
        {
            // Ráfektetés: az elem az előtte és utána álló elem átmenete fölött szól, a saját csatornáján.
            const Row &row = rows_[vr.row];
            const int id = row.id;
            if (row.overlay && (row.state == Row::Pending || row.state == Row::Cued)) {
                menu.addAction(TR("Ráfektetés megszüntetése (önálló elem)"), this, [this, id] {
                    if (setRowOverlay(id, false)) say(TR("A ráfektetés megszűnt; az elem újra önállóan szól."));
                });
            } else if (row.state == Row::Pending || row.state == Row::Cued) {
                int ai = -1, ci = -1;
                for (int j = vr.row - 1; j >= 0; --j)
                    if (!rows_[j].overlay) { ai = j; break; }
                for (int j = vr.row + 1; j < rows_.size(); ++j) {
                    if (rows_[j].overlay) continue;
                    if (rows_[j].state == Row::Pending || rows_[j].state == Row::Cued || rows_[j].state == Row::Playing) ci = j;
                    break;
                }
                if (ai >= 0 && ci >= 0) {
                    QAction *act = menu.addAction(TR("Ráfektetés az átmenetre (%1 → %2)").arg(rows_[ai].item.title, rows_[ci].item.title), this, [this, id] {
                        if (setRowOverlay(id, true, 0, 0))
                            say(TR("Ráfektetve: a mix pontnál indul. Az időpontját a Mix editorban vagy a lenyitott átmenetnél állíthatod."));
                    });
                    act->setEnabled(rows_[ci].state != Row::Playing);
                }
            }
        }
        if (rows_[vr.row].groupId)
            menu.addAction(TR("Kivétel a blokkból"), this, [this, vr] {
                // A sor a blokkja mögé kerül, hogy a blokk ne szakadjon ketté.
                const int id = rows_[vr.row].id;
                const Row row = rows_[vr.row];
                const bool movable = row.state == Row::Pending || row.state == Row::Cued;
                if (movable && vr.row != vr.first + vr.count - 1) {
                    rows_.remove(vr.row);
                    rows_.insert(vr.first + vr.count - 1, row);
                    persistOrder();
                    reconcile();
                }
                ungroup(indexOfRow(id), 1);
                selectRowId(id);
            });
        menu.addSeparator();
        menu.addAction(TR("Törlés a playlistből"), this, &MainWindow::removeSelected);
    }
    menu.exec(table_->viewport()->mapToGlobal(pos));
}

void MainWindow::moveBlock(int first, int count, int to)
{
    for (int i = first; i < first + count; ++i) {
        if (rows_[i].state != Row::Pending && rows_[i].state != Row::Cued) {
            say(TR("Az a blokk, amelyből már szólt vagy szól elem, egészben nem helyezhető át; nyisd ki, és a sorait húzd."));
            return;
        }
    }
    to = std::clamp(to, firstMovable(), int(rows_.size()));
    if (to >= first && to <= first + count) return;
    const QVector<Row> block = rows_.mid(first, count);
    rows_.remove(first, count);
    if (to > first) to -= count;
    for (int k = 0; k < count; ++k) {
        Row row = block[k];
        if (!rows_.isEmpty()) {
            const Row &neighbour = to + k > 0 ? rows_[to + k - 1] : rows_[0];
            row.playlistId = neighbour.playlistId;
            row.playlistKey = neighbour.playlistKey;
        }
        rows_.insert(to + k, row);
    }
    persistOrder();
    reconcile();
    refreshTable();
    selectRowId(block.first().id);
}

// Az első hely, ahová még beszúrható vagy áthúzható elem: a már lejátszott és a szóló sorok után.
int MainWindow::firstMovable() const
{
    for (int i = rows_.size() - 1; i >= 0; --i)
        if (rows_[i].state != Row::Pending && rows_[i].state != Row::Cued) return i + 1;
    return 0;
}

void MainWindow::persistOrder()
{
    // A sorok az órák szerint is sorban maradnak: egy sor nem tartozhat korábbi órához, mint az előtte álló.
    QVector<QPair<int, int>> order;
    int key = 0, playlist = 0;
    for (Row &row : rows_) {
        if (row.playlistKey < key) {
            row.playlistKey = key;
            row.playlistId = playlist;
        } else {
            key = row.playlistKey;
            playlist = row.playlistId;
        }
        order.push_back({row.id, row.playlistId});
    }
    db_->saveOrder(order);
}

bool MainWindow::loadIntoPlayer(Row &row, int player)
{
    QString err;
    if (!players_[player]->loadFile(row.item.path, &err)) {
        row.state = Row::Failed;
        row.player = -1;
        db_->setEntryStatus(row.id, "skipped");
        say(TR("Nem tölthető be: %1 – %2").arg(row.item.title, err), 9000);
        return false;
    }
    players_[player]->rowId = row.id;
    if (row.item.cueInMs > 0) players_[player]->cueTo(row.item.cueInMs * eter::kRate / 1000);
    players_[player]->setIntro(row.item.introMs * eter::kRate / 1000);
    row.state = Row::Cued;
    row.player = player;
    return true;
}

// A készenlétben álló lejátszókban mindig a sorban következő elemek várnak.
void MainWindow::reconcile()
{
    QVector<int> idle;
    for (int i = 0; i < 2; ++i)
        if (!players_[i]->isPlaying()) idle << i;

    auto desired = [&] {
        QVector<int> ids;
        for (int i = 0; i < rows_.size(); ++i) {
            if (ids.size() >= idle.size()) break;
            const Row &r = rows_[i];
            // A ráfektetett elem nem kerül lejátszóba: a saját csatornáján szól az átmenet fölött.
            if ((r.state == Row::Pending || r.state == Row::Cued) && !overlayActive(i)) ids << r.id;
        }
        return ids;
    };

    QVector<int> want = desired();
    for (int p : idle) {
        if (!players_[p]->isLoaded()) continue;
        Row *row = rowById(players_[p]->rowId);
        if (row && want.contains(row->id)) continue;
        if (row && row->state == Row::Cued) {
            row->state = Row::Pending;
            row->player = -1;
        }
        players_[p]->eject();
    }
    for (int guard = 0; guard < 1000; ++guard) {
        want = desired();
        Row *candidate = nullptr;
        for (int id : want) {
            Row *r = rowById(id);
            if (r && r->state == Row::Pending) { candidate = r; break; }
        }
        int empty = -1;
        for (int p : idle)
            if (!players_[p]->isLoaded()) { empty = p; break; }
        if (!candidate || empty < 0) break;
        loadIntoPlayer(*candidate, empty);      // hiba esetén a sor „hibás" lesz, és a következő jön
    }
}

void MainWindow::appendItems(const QVector<int> &itemIds, int at, int groupId, const QString &groupName)
{
    if (itemIds.isEmpty()) return;
    int index;
    if (at >= 0) {
        index = std::clamp(at, firstMovable(), int(rows_.size()));
    } else {
        // A végére – de ha az ütemező sorai is ott vannak, akkor azok elé: a legutóbbi kézzel betett
        // elem után, ennek híján a készenlétben álló elemek után.
        index = firstMovable();
        while (index < rows_.size() && rows_[index].state == Row::Cued) ++index;
        for (int i = rows_.size() - 1; i >= index; --i)
            if (!rows_[i].scheduled) { index = i + 1; break; }
    }
    const QDateTime now = eter::now();
    int nowKey = 0;
    const int nowPlaylist = db_->playlistFor(now.date(), now.time().hour(), &nowKey);
    // Blokk belsejébe ejtett elem a blokk tagja lesz, hogy a blokk ne szakadjon ketté.
    QString group = groupName;
    if (!groupId && index > 0 && index < rows_.size() && rows_[index - 1].groupId
        && rows_[index - 1].groupId == rows_[index].groupId) {
        groupId = rows_[index].groupId;
        group = rows_[index].groupName;
    }
    int added = 0;
    for (int itemId : itemIds) {
        int playlist = nowPlaylist;
        if (index < rows_.size()) playlist = index > 0 ? rows_[index - 1].playlistId : rows_[index].playlistId;
        else if (index > 0 && rows_[index - 1].playlistKey > nowKey) playlist = rows_[index - 1].playlistId;
        const Entry e = db_->addEntry(playlist, itemId, "manual", -1, groupId, group);
        if (!e.id) continue;
        rows_.insert(index++, makeRow(e));
        ++added;
    }
    if (!added) return;
    persistOrder();
    reconcile();
    refreshTable();
}

void MainWindow::moveRow(int from, int to, bool outsideBlock)
{
    if (from < 0 || from >= rows_.size()) return;
    if (rows_[from].state != Row::Pending && rows_[from].state != Row::Cued) {
        say(TR("Csak a még várakozó sorok rendezhetők át."));
        return;
    }
    to = std::clamp(to, firstMovable(), int(rows_.size()));
    if (to == from || to == from + 1) return;
    Row row = rows_[from];
    rows_.remove(from);
    if (to > from) --to;
    if (!rows_.isEmpty()) {
        const Row &neighbour = to > 0 ? rows_[to - 1] : rows_[0];
        row.playlistId = neighbour.playlistId;
        row.playlistKey = neighbour.playlistKey;
    }
    // Blokk: két azonos blokkbeli sor közé ejtve a sor a blokk tagja lesz; a blokkjából kihúzva kikerül belőle.
    const int prev = to > 0 ? rows_[to - 1].groupId : 0;
    const int next = to < rows_.size() && !outsideBlock ? rows_[to].groupId : 0;
    const int before = row.groupId;
    if (prev && prev == next) {
        row.groupId = prev;
        row.groupName = rows_[to].groupName;
    } else if (!row.groupId || (prev != row.groupId && next != row.groupId)) {
        row.groupId = 0;
        row.groupName.clear();
    }
    if (row.groupId != before) db_->setEntryGroup(row.id, row.groupId, row.groupName);
    rows_.insert(to, row);
    persistOrder();
    reconcile();
    refreshTable();
    selectRowId(row.id);
}

// Kézi betöltés: a sor a várakozók élére kerül, és a kért lejátszóba töltődik.
void MainWindow::loadRowInto(int rowId, int player)
{
    Row *found = rowById(rowId);
    if (!found) return;
    if (players_[player]->isPlaying()) {
        say(TR("A %1-es lejátszó szól, most nem tölthető bele új elem.").arg(player + 1));
        return;
    }
    if (found->state == Row::Playing) return;
    if (found->overlay) {
        // Kézi betöltés: az elem a lejátszón szól, a ráfektetés megszűnik.
        found->overlay = false;
        found->overlayOffsetMs = 0;
        found->overlayDuckDb = 0;
        db_->setEntryOverlay(rowId, false);
        ++mixVersion_;
        say(TR("A ráfektetés megszűnt: „%1” a lejátszóba került.").arg(found->item.title));
    }
    if (found->state == Row::Cued && found->player == player) return;
    if (found->state == Row::Cued && found->player >= 0) players_[found->player]->eject();

    // Ami eddig ebben a lejátszóban várt, visszakerül a várakozók közé.
    for (Row &other : rows_) {
        if (other.player == player && other.state == Row::Cued) {
            other.state = Row::Pending;
            other.player = -1;
        }
    }
    players_[player]->eject();

    Row row = *found;
    row.state = Row::Pending;
    row.player = -1;
    row.startedAt = {};
    rows_.remove(indexOfRow(rowId));
    const int to = firstMovable();
    if (!rows_.isEmpty()) {
        const Row &neighbour = to > 0 ? rows_[to - 1] : rows_[0];
        row.playlistId = neighbour.playlistId;
        row.playlistKey = neighbour.playlistKey;
    }
    rows_.insert(to, row);
    db_->setEntryStatus(rowId, "pending");
    loadIntoPlayer(rows_[to], player);
    persistOrder();
    reconcile();
    refreshTable();
}

void MainWindow::removeSelected()
{
    const int v = table_->currentRow();
    if (v < 0 || v >= view_.size()) return;
    if (view_[v].kind == ViewRow::Hour || view_[v].kind == ViewRow::Wave) return;
    if (view_[v].kind == ViewRow::Header) {
        // Blokkfejléc: a blokk minden sora törlődik, kivéve azt, ami éppen szól.
        QVector<Row> keep;
        int removed = 0;
        for (int i = 0; i < rows_.size(); ++i) {
            const Row &row = rows_[i];
            const bool inBlock = i >= view_[v].first && i < view_[v].first + view_[v].count;
            if (!inBlock || row.state == Row::Playing) {
                keep.push_back(row);
                continue;
            }
            if (row.state == Row::Cued && row.player >= 0) players_[row.player]->eject();
            db_->deleteEntry(row.id);
            ++removed;
        }
        rows_ = keep;
        reconcile();
        refreshTable();
        say(TR("A blokk %1 eleme törölve a playlistből.").arg(removed));
        return;
    }
    const int r = view_[v].row;
    Row &row = rows_[r];
    if (row.state == Row::Playing) {
        say(TR("Az adásban lévő elem nem törölhető."));
        return;
    }
    if (row.state == Row::Cued && row.player >= 0) players_[row.player]->eject();
    db_->deleteEntry(row.id);
    rows_.remove(r);
    reconcile();
    refreshTable();
}

void MainWindow::clearList()
{
    int n = 0;
    for (const Row &row : rows_)
        if (row.state != Row::Playing) ++n;
    if (!n) return;
    if (QMessageBox::question(this, TR("Lista ürítése"),
                              TR("Törlöd a playlist %1 elemét? Az adásban lévő elem marad.").arg(n),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    QVector<Row> keep;
    for (const Row &row : rows_) {
        if (row.state == Row::Playing) {
            keep.push_back(row);
            continue;
        }
        if (row.state == Row::Cued && row.player >= 0) players_[row.player]->eject();
        db_->deleteEntry(row.id);
    }
    rows_ = keep;
    refreshTable();
}

void MainWindow::refreshTable()
{
    // Az újraépítés közbeni görgetés nem kézi görgetés.
    programmaticScroll_ = true;
    struct Restore { MainWindow *w; ~Restore() { w->programmaticScroll_ = false; w->followPlaying(); } } restore{this};
    const int selected = selectedRowId();
    const int selectedHead = selectedHeaderRowId();
    rebuildView();
    table_->setRowCount(view_.size());
    for (int v = 0; v < view_.size(); ++v) {
        const int h = view_[v].kind == ViewRow::Item ? 48 : view_[v].kind == ViewRow::Header ? 36
                      : view_[v].kind == ViewRow::Hour ? 24 : 150;
        if (table_->rowHeight(v) != h) table_->setRowHeight(v, h);
    }
    // Melyik sorból visz egyedi mix (és voice track) a következőbe.
    customFrom_.clear();
    vtFrom_.clear();
    {
        const QHash<int, int> pairs = db_->transitionPairs();
        for (auto it = pairs.begin(); it != pairs.end(); ++it) {
            customFrom_.insert(it.value());
            if (db_->transition(it.key()).vtItemId) vtFrom_.insert(it.value());
        }
    }
    qint64 pendingFrames = 0;
    int waiting = 0;
    int selectRow = -1;
    QFont mono("IBM Plex Mono");
    mono.setPixelSize(12);
    QFont bold = table_->font();
    bold.setWeight(QFont::DemiBold);
    for (int i = 0; i < rows_.size(); ++i) {
        const Row &row = rows_[i];
        if (row.state != Row::Pending && row.state != Row::Cued) continue;
        ++waiting;
        if (!(row.overlay && overlayActive(i))) pendingFrames += row.frames;     // a ráfektetett elem nem hosszabbít
    }
    for (int v = 0; v < view_.size(); ++v) {
        const ViewRow &vr = view_[v];
        if (vr.kind == ViewRow::Hour || vr.kind == ViewRow::Wave) {
            for (int c = 0; c < 6; ++c) {
                auto *item = table_->item(v, c);
                if (!item) {
                    item = new QTableWidgetItem;
                    table_->setItem(v, c, item);
                }
                item->setText(QString());
                item->setFlags(Qt::ItemIsEnabled);           // nem jelölhető ki
                item->setData(kRowIdRole, QVariant());
                item->setData(kHeadIdRole, QVariant());
            }
            continue;
        }
        if (vr.kind == ViewRow::Header) {
            // Blokk fejléce: név, elemszám, teljes hossz, és hogy hol tart.
            const Row &head = rows_[vr.first];
            const bool open = expanded_.contains(head.groupId);
            qint64 frames = 0;
            int done = 0, playing = -1, cued = -1;
            bool hidesSelected = false;
            QString nowTitle;
            for (int i = vr.first; i < vr.first + vr.count; ++i) {
                const Row &m = rows_[i];
                frames += m.frames;
                if (m.state == Row::Playing) { playing = m.player; nowTitle = m.item.title; }
                else if (m.state == Row::Cued) { if (cued < 0) cued = m.player; }
                else if (m.state != Row::Pending) ++done;
                if (m.id == selected && !open) hidesSelected = true;
            }
            QString status;
            QColor fg = theme::text, bg("#262b34");
            if (playing >= 0) {
                status = TR("ADÁSBAN · %1/%2").arg(done + 1).arg(vr.count);
                fg = theme::amberSoft;
                bg = QColor("#3a2c10");
            } else if (done == vr.count) {
                status = TR("lejátszva");
                fg = QColor("#8a93a0");
                bg = QColor("#1f2329");
            } else if (cued >= 0) {
                status = TR("KÉSZENLÉT · %1").arg(cued + 1);
                fg = theme::blueSoft;
                bg = QColor("#14283d");
            } else if (done > 0) {
                status = TR("%1/%2 lejátszva").arg(done).arg(vr.count);
            }
            QString title = QStringLiteral("%1  %2").arg(open ? QStringLiteral("▾") : QStringLiteral("▸"), head.groupName);
            if (!open && !nowTitle.isEmpty()) title += TR("  –  most: %1").arg(nowTitle);
            const QString cells[6] = {QString(), title, TR("%1 elem").arg(vr.count), TR("Playlist blokk"),
                                      theme::formatTime(frames), status};
            for (int c = 0; c < 6; ++c) {
                auto *item = table_->item(v, c);
                if (!item) {
                    item = new QTableWidgetItem;
                    table_->setItem(v, c, item);
                }
                if (c != 0) item->setText(cells[c]);
                item->setForeground(fg);
                item->setBackground(bg);
                item->setFont(c == 0 || c == 4 ? mono : bold);
                item->setTextAlignment((c == 4 ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
                item->setData(kGroupRole, 2);
                item->setData(kChipRole, QVariant());
                item->setData(kRowIdRole, QVariant());
                item->setData(kHeadIdRole, c == 0 ? QVariant(head.id) : QVariant());
                item->setToolTip(open ? TR("Kattints a becsukáshoz · a fejlécnél fogva az egész blokk áthúzható")
                                      : TR("Kattints a kinyitáshoz · a fejlécnél fogva az egész blokk áthúzható"));
            }
            if (head.id == selectedHead || hidesSelected) selectRow = v;
            continue;
        }
        const int r = vr.row;
        const Row &row = rows_[r];
        QString status;
        QColor fg = theme::text, bg = Qt::transparent;
        switch (row.state) {
        case Row::Pending:
            if (row.fixedAt.isValid()) {
                status = TR("FIX %1").arg(row.fixedAt.toString("HH:mm:ss"));
                fg = theme::amberSoft;
            }
            break;
        case Row::Cued:
            status = TR("KÉSZENLÉT · %1").arg(row.player + 1);
            fg = theme::blueSoft;
            bg = QColor("#14283d");
            break;
        case Row::Playing:
            status = TR("ADÁSBAN · %1").arg(row.player + 1);
            fg = theme::amberSoft;
            bg = QColor("#3a2c10");
            break;
        case Row::Played: status = TR("lejátszva"); fg = QColor("#8a93a0"); break;
        case Row::Skipped: status = TR("kihagyva"); fg = QColor("#8a93a0"); break;
        case Row::Failed: status = TR("hibás fájl"); fg = theme::red; break;
        }
        // A nem zenei elemek (szignál, reklám, hírek…) sorát a kategóriájuk színe tölti ki.
        // Az állapotot ilyenkor az Állapot oszlop színe mutatja; a lejátszott sorok halványabbak.
        QColor statusFg = fg, statusBg = bg;
        const QColor cat(row.item.color);
        if (row.item.kind != QLatin1String("music") && !row.item.color.isEmpty() && cat.isValid()) {
            const bool done = row.state == Row::Played || row.state == Row::Skipped || row.state == Row::Failed;
            const QColor base("#16191e");
            const double a = done ? 0.30 : 1.0;
            bg = QColor(int(base.red() + (cat.red() - base.red()) * a), int(base.green() + (cat.green() - base.green()) * a),
                        int(base.blue() + (cat.blue() - base.blue()) * a));
            const double lum = (0.299 * bg.red() + 0.587 * bg.green() + 0.114 * bg.blue()) / 255.0;
            fg = done ? QColor("#aab1bc") : (lum > 0.55 ? QColor("#101215") : QColor("#ffffff"));
            if (row.state == Row::Pending && !row.fixedAt.isValid()) { statusFg = fg; statusBg = bg; }
            else if (statusBg == Qt::transparent || statusBg.alpha() == 0) statusBg = base;
            if (done) { statusFg = fg; statusBg = bg; }
        }
        const QString cells[6] = {QString(), row.item.title, row.item.artist, row.item.categoryName,
                                  row.frames > 0 ? theme::formatTime(row.frames) : QStringLiteral("?"), status};
        for (int c = 0; c < 6; ++c) {
            auto *item = table_->item(v, c);
            if (!item) {
                item = new QTableWidgetItem;
                table_->setItem(v, c, item);
            }
            if (c != 0) item->setText(cells[c]);
            item->setForeground(c == 5 ? statusFg : fg);
            item->setBackground(c == 5 ? statusBg : bg);
            item->setFont(c == 0 || c == 4 ? mono : table_->font());
            item->setTextAlignment((c == 4 ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
            item->setData(kGroupRole, row.groupId ? 1 : 0);
            item->setData(kRowIdRole, c == 0 ? QVariant(row.id) : QVariant());
            item->setData(kHeadIdRole, QVariant());
            item->setToolTip(c == 1 ? row.item.path : QString());
            if (c == 3) item->setData(kChipRole, row.item.color.isEmpty() ? QVariant() : QVariant(QColor(row.item.color)));
        }
        if (row.id == selected) selectRow = v;
    }
    for (int v = 0; v < view_.size(); ++v)
        if (view_[v].kind == ViewRow::Item || view_[v].kind == ViewRow::Header)
            for (int c = 0; c < 6; ++c)
                if (auto *item = table_->item(v, c)) item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    if (selectRow >= 0) table_->setCurrentCell(selectRow, 0);
    updateStartTimes();
    updateMixPairs();
    updateMini();
    if (remote_ && remoteStateTimer_ && remote_->clientCount()) remoteStateTimer_->start();
    table_->viewport()->update();
    listInfo_->setText(rows_.isEmpty()
                           ? TR("Üres lista")
                           : TR("%1 várakozó elem · hátralévő: %2").arg(waiting).arg(formatMs(pendingFrames * 1000 / eter::kRate)));
}

// A „Kezdés" oszlop: a lejátszott és szóló elemeknél a tényleges kezdés,
// a várakozóknál a becsült időpont, ha az elemek megszakítás nélkül követik egymást.
void MainWindow::updateStartTimes()
{
    const QDateTime now = eter::now();
    QDateTime base;
    for (const Row &row : rows_) {
        if (row.state != Row::Playing || row.player < 0) continue;
        PlayerWidget *p = players_[row.player];
        if (leaving(row.player)) continue;        // a kiúszó elem már nem számít
        // Ahol a következő indulni fog: a mix pont, ha ismert, különben a szám vége.
        const qint64 planned = planFor(row.player, standbyPlayer()).mixOutFrames;
        const qint64 end = planned >= 0 ? planned : p->duration();
        const qint64 left = std::max<qint64>(0, end - p->position());
        const QDateTime t = now.addMSecs(left * 1000 / eter::kRate);
        if (!base.isValid() || t > base) base = t;
    }
    if (!base.isValid()) base = now;              // semmi sem szól: ha most indulna
    // Kis ingadozás (időzítő pontatlansága) ne ugráltassa a másodperceket.
    if (!chainBase_.isValid() || std::llabs(chainBase_.msecsTo(base)) > 400) chainBase_ = base;

    const bool fixedFade = db_->setting("fixed_time_policy", "fade") == "fade";
    QDateTime t = chainBase_;
    startText_.resize(rows_.size());
    QVector<QDateTime> at(rows_.size());
    QVector<int> overlays;
    for (int r = 0; r < rows_.size(); ++r) {
        const Row &row = rows_[r];
        QString text;
        if (row.state == Row::Pending && row.overlay && overlayActive(r)) {
            // A ráfektetett elem nem tolja el a többit: a kezdése a szomszédja kezdetéből jön, lent.
            overlays << r;
            continue;
        }
        switch (row.state) {
        case Row::Playing:
        case Row::Played:
            if (row.startedAt.isValid()) text = row.startedAt.toString("HH:mm:ss");
            break;
        case Row::Cued:
        case Row::Pending:
            // Automata üzemmódban a fix idejű elem a saját időpontjában indul.
            if (mode_ == Auto && row.fixedAt.isValid() && row.fixedAt > now)
                t = fixedFade ? row.fixedAt : std::max(t, row.fixedAt);
            text = t.toString("HH:mm:ss");
            at[r] = t;
            // A sor a mix pontjáig tölti ki az időt, ha az már ismert.
            t = t.addMSecs(row.item.mixOutMs > 0 ? row.item.mixOutMs : row.frames * 1000 / eter::kRate);
            break;
        default:
            text = QStringLiteral("–");
            break;
        }
        startText_[r] = text;
    }
    for (int r : overlays) {
        int ai = -1, ci = -1;
        overlayTargets(r, &ai, &ci);
        QDateTime base;
        if (ci >= 0) base = rows_[ci].state == Row::Playing ? rows_[ci].startedAt : at[ci];
        startText_[r] = base.isValid() ? base.addMSecs(rows_[r].overlayOffsetMs).toString("HH:mm:ss") : QStringLiteral("–");
    }
    // A fejléc a blokk első, még le nem játszott elemének kezdését mutatja.
    for (int v = 0; v < view_.size() && v < table_->rowCount(); ++v) {
        if (view_[v].kind == ViewRow::Hour || view_[v].kind == ViewRow::Wave) continue;
        int r = view_[v].row;
        if (view_[v].kind == ViewRow::Header) {
            r = view_[v].first;
            for (int i = view_[v].first; i < view_[v].first + view_[v].count; ++i)
                if (rows_[i].state == Row::Playing || rows_[i].state == Row::Cued || rows_[i].state == Row::Pending) { r = i; break; }
        }
        if (r >= rows_.size()) continue;
        if (auto *item = table_->item(v, 0))
            if (item->text() != startText_[r]) item->setText(startText_[r]);
    }
}

// ----------------------------------------------------------- előhallgatás

ItemEditorDialog *MainWindow::openItemEditor(int itemId)
{
    if (itemId <= 0) {
        // A kijelölt elem: amelyik listában utoljára kattintottál.
        Item item;
        if (lastSelectionInLibrary_) item = library_->currentItem();
        if (!item.id)
            if (const Row *row = rowById(selectedRowId())) item = row->item;
        if (!item.id) item = library_->currentItem();
        itemId = item.id;
    }
    if (itemId <= 0) {
        say(TR("A szerkesztéshez jelölj ki egy elemet a hangtárban vagy a playlistben."));
        return nullptr;
    }
    if (itemEditor_ && itemEditor_->itemId() == itemId) {
        itemEditor_->raise();
        itemEditor_->activateWindow();
        return itemEditor_;
    }
    if (itemEditor_) {
        itemEditor_->close();                  // nem mentett módosításnál rákérdez; ha marad, nem nyitunk másikat
        if (itemEditor_ && itemEditor_->isVisible()) return itemEditor_;
    }
    auto *dlg = new ItemEditorDialog(db_, &engine_, itemId, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    itemEditor_ = dlg;
    connect(dlg, &ItemEditorDialog::saved, this, [this](int id) {
        onMixSaved({id});
        say(TR("Az elem adatai és jelölőpontjai elmentve."), 5000);
    });
    connect(dlg, &ItemEditorDialog::previewStarting, this, [this] {
        if (pflActive_) stopPfl();             // egyszerre csak egy előhallgatás szólhat
        if (mixPage_) mixPage_->stopPreview();
    });
    dlg->setModal(false);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
    return dlg;
}

void MainWindow::togglePfl()
{
    auto &pfl = engine_.source(Engine::Pfl);
    if (pflActive_) {
        stopPfl();
        return;
    }
    if (mixPage_) mixPage_->stopPreview();
    if (itemEditor_) itemEditor_->releasePreview();
    Item item;
    if (lastSelectionInLibrary_) item = library_->currentItem();
    if (!item.id)
        if (const Row *row = rowById(selectedRowId())) item = row->item;
    if (!item.id) item = library_->currentItem();
    if (!item.id) {
        say(TR("Előhallgatáshoz jelölj ki egy elemet a hangtárban vagy a playlistben."));
        return;
    }
    std::string err;
    if (!pfl.load(item.path.toStdString(), &err)) {
        say(QString::fromStdString(err), 8000);
        return;
    }
    pflActive_ = true;
    pfl.play();
    pflTitle_->setText(item.artist.isEmpty() ? item.title : item.title + QStringLiteral(" – ") + item.artist);
    pflBtn_->setText(TR("Leállítás"));
    pflBtn_->setIcon(theme::icon(theme::Icon::Stop, theme::blueSoft));
    pflBtn_->setProperty("active", true);
    repolish(pflBtn_);
    pflOutput_->setEnabled(false);
}

void MainWindow::stopPfl()
{
    engine_.source(Engine::Pfl).unload();
    pflActive_ = false;
    pflProgress_->setValue(0);
    pflTitle_->setText(TR("Jelölj ki egy elemet az előhallgatáshoz"));
    pflBtn_->setText(TR("Előhallgatás"));
    pflBtn_->setIcon(theme::icon(theme::Icon::Play, theme::text));
    pflBtn_->setProperty("active", false);
    repolish(pflBtn_);
    pflOutput_->setEnabled(true);
}

// ------------------------------------------------------------------ egyéb

void MainWindow::openSettings()
{
    // Nem modális ablak: önállóan mozgatható, és közben az adás is kezelhető marad.
    if (!settings_) {
        settings_ = new SettingsDialog(this);
        settings_->setAttribute(Qt::WA_DeleteOnClose);
    }
    settings_->show();
    settings_->raise();
    settings_->activateWindow();
}

void MainWindow::openLog()
{
    if (!logDialog_) {
        logDialog_ = new LogDialog(db_, this);
        logDialog_->setAttribute(Qt::WA_DeleteOnClose);
    }
    logDialog_->setModal(false);
    logDialog_->show();
    logDialog_->raise();
    logDialog_->activateWindow();
}

void MainWindow::about()
{
    auto *box = new QMessageBox(this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(false);
    box->setWindowTitle(TR("Névjegy"));
    box->setIconPixmap(theme::appIcon().pixmap(64, 64));
    box->setTextFormat(Qt::RichText);
    box->setText(
        TR("<b>Éter</b> – rádiós adáslebonyolító<br>%1 · teljes változat: lejátszók, hangtár, ütemező, Mix editor<br><br>"
                       "Felhasznált könyvtárak: Qt 6 (LGPL), FFmpeg (LGPL), miniaudio (MIT-0), SQLite, "
                       "IBM Plex betűkészlet (OFL).")
            .arg(QStringLiteral(ETER_VERSION)));
    box->show();
}

QString MainWindow::debugState() const
{
    static const char *names[] = {"pending", "cued", "playing", "played", "skipped", "failed"};
    QStringList parts;
    for (const Row &r : rows_)
        parts << QStringLiteral("%1=%2%3").arg(r.item.title, names[r.state], r.player >= 0 ? QString::number(r.player + 1) : QString());
    for (int i = 0; i < 2; ++i)
        parts << QStringLiteral("P%1:%2@%3").arg(i + 1).arg(!players_[i]->isLoaded() ? "empty" : players_[i]->isPlaying() ? "playing" : "ready")
                     .arg(players_[i]->position() / double(eter::kRate), 0, 'f', 1);
    return parts.join("  ");
}

// ---------------------------------------------------------------- ütemező

void MainWindow::showPage(int page)
{
    stack_->setCurrentIndex(page);
    tabLive_->setChecked(page == 0);
    tabSched_->setChecked(page == 1);
    tabMix_->setChecked(page == 2);
    tabPl_->setChecked(page == 3);
    tabEvents_->setChecked(page == 4);
    if (page != 2 && mixPage_) mixPage_->stopPreview();
}

MainWindow::Row MainWindow::makeRow(const Entry &e) const
{
    Row row;
    row.id = e.id;
    row.playlistId = e.playlistId;
    row.playlistKey = e.playlistKey;
    row.item = e.item;
    row.frames = e.item.durationMs * eter::kRate / 1000;
    row.scheduled = e.source == "scheduler";
    row.groupId = e.groupId;
    row.groupName = e.groupName;
    row.overlay = e.overlay;
    row.overlayOffsetMs = e.overlayOffsetMs;
    row.overlayDuckDb = e.overlayDuckDb;
    if (e.fixedStartS >= 0) {
        const QDate d = QDate::fromString(QString::number(e.playlistKey / 100), "yyyyMMdd");
        row.fixedAt = QDateTime(d, QTime(e.playlistKey % 100, 0)).addSecs(e.fixedStartS);
    }
    return row;
}

// Generálás után: a memóriabeli sorok összefésülése az adatbázissal.
void MainWindow::syncRowsFromDb()
{
    QHash<int, Row> old;
    QVector<Row> merged;
    for (const Row &r : rows_) {
        if (r.state == Row::Pending || r.state == Row::Cued) old.insert(r.id, r);
        else merged.push_back(r);            // lejátszott, kihagyott és szóló sorok maradnak elöl
    }
    for (const Entry &e : db_->pendingEntries()) {
        if (old.contains(e.id)) {
            Row r = old[e.id];
            r.playlistId = e.playlistId;
            r.playlistKey = e.playlistKey;
            merged.push_back(r);
        } else {
            merged.push_back(makeRow(e));
        }
    }
    rows_ = merged;                          // a törölt sorok lejátszóit a reconcile() üríti ki
}

// Automata üzemmódban: fix időpontok betartása és az elmúlt óra maradékának elhagyása.
void MainWindow::applySchedule()
{
    if (mode_ != Auto) return;
    const QDateTime now = eter::now();
    const int nowKey = Scheduler::keyOf(now);
    const int first = firstMovable();
    bool changed = false;

    auto skip = [&](Row &row) {
        if (row.state == Row::Cued && row.player >= 0) players_[row.player]->eject();
        row.state = Row::Skipped;
        row.player = -1;
        db_->setEntryStatus(row.id, "skipped");
        changed = true;
    };
    // A [first, end) szakaszból a megadott sorok kihagyva előre kerülnek, `lead` (ha van) közvetlenül utánuk.
    auto regroup = [&](int end, int leadId, auto shouldSkip) {
        QVector<Row> skipped, kept;
        Row lead;
        bool hasLead = false;
        for (int i = first; i < end; ++i) {
            Row row = rows_[i];
            if (row.id == leadId) { lead = row; hasLead = true; }
            else if (shouldSkip(row)) { skip(row); skipped.push_back(row); }
            else kept.push_back(row);
        }
        int at = first;
        for (const Row &r : skipped) rows_[at++] = r;
        if (hasLead) rows_[at++] = lead;
        for (const Row &r : kept) rows_[at++] = r;
    };

    // 1. Esedékes fix idejű elem: a sor elejére kerül, az előtte álló ütemezett elemek kimaradnak.
    int fixedIndex = -1;
    for (int i = first; i < rows_.size(); ++i) {
        const Row &r = rows_[i];
        if (r.fixedAt.isValid() && r.fixedAt <= now && r.fixedAt.secsTo(now) < 120) { fixedIndex = i; break; }
    }
    int fixedId = -1;
    if (fixedIndex >= 0) {
        fixedId = rows_[fixedIndex].id;
        if (fixedIndex > first) {
            regroup(fixedIndex + 1, fixedId, [](const Row &r) { return r.scheduled; });
            changed = true;
        }
    }

    // 2. Óraváltás: ha az új órának már van ütemezett anyaga, az előző órák maradéka kimarad.
    bool currentHourReady = false;
    for (int i = first; i < rows_.size(); ++i)
        if (rows_[i].scheduled && rows_[i].playlistKey == nowKey) { currentHourReady = true; break; }
    if (currentHourReady) {
        int end = firstMovable();
        bool any = false;
        while (end < rows_.size() && rows_[end].playlistKey < nowKey) {
            any = any || (rows_[end].scheduled && rows_[end].id != fixedId);
            ++end;
        }
        if (any) regroup(end, -1, [&](const Row &r) { return r.scheduled && r.id != fixedId; });
    }

    if (changed) {
        trimHistory();
        persistOrder();
        reconcile();
        refreshTable();
    }

    // 3. A fix elem indítása: kiúsztatja, ami szól. („Megvárja" beállításnál a következő váltáskor indul.)
    if (fixedId >= 0 && db_->setting("fixed_time_policy", "fade") == "fade") {
        const Row *fixed = rowById(fixedId);
        int onAir = -1, playing = 0;
        for (int i = 0; i < 2; ++i) {
            if (!players_[i]->isPlaying()) continue;
            ++playing;
            if (!leaving(i)) onAir = i;
        }
        if (fixed && fixed->state == Row::Cued && onAir >= 0 && playing == 1 && standbyPlayer() == fixed->player)
            startStandby(onAir, fadeMs_);
    }
    // Ha a fix elemre vártunk (előtte elfogyott a műsor), most indul.
    if (fixedId >= 0 && waitingForFixed_ && !players_[0]->isPlaying() && !players_[1]->isPlaying()) {
        const Row *fixed = rowById(fixedId);
        if (fixed && fixed->state == Row::Cued && standbyPlayer() == fixed->player) {
            waitingForFixed_ = false;
            statusBar()->clearMessage();
            startStandby(-1, 0);
        }
    }
}

// Percenként: a hiányzó órák playlistjének elkészítése előre.
void MainWindow::autoGenerate()
{
    const QDateTime now = eter::now();
    const int minute = now.time().hour() * 60 + now.time().minute();
    if (minute == lastGenerateMinute_) return;
    lastGenerateMinute_ = minute;
    const bool on = db_->setting("auto_generate", "1") == "1";
    const bool hasGrid = !db_->weekGrid().isEmpty();
    schedStatus_->setText(!hasGrid ? TR("Ütemező: nincs beállítva")
                          : on ? TR("Ütemező: automatikus") : TR("Ütemező: kézi generálás"));
    if (!on || !hasGrid) return;
    const int hours = std::clamp(db_->setting("generate_ahead_hours", "24").toInt(), 1, 168);
    const GenerateResult r = scheduler_.generate(now, hours, false);
    if (r.entries > 0) {
        syncRowsFromDb();
        reconcile();
        refreshTable();
        say(TR("Ütemező: %1 órára %2 új elem került a playlistbe.").arg(r.hours).arg(r.entries));
    }
}

// A lejátszott és kihagyott sorokból csak a legutóbbi néhány marad a listában; a többit a napló őrzi.
void MainWindow::trimHistory()
{
    const int keep = 30;
    int done = 0;
    for (const Row &r : rows_)
        if (r.state == Row::Played || r.state == Row::Skipped || r.state == Row::Failed) ++done;
    for (int i = 0; i < rows_.size() && done > keep;) {
        const Row::State s = rows_[i].state;
        if (s == Row::Played || s == Row::Skipped || s == Row::Failed) {
            db_->deleteEntry(rows_[i].id);
            rows_.remove(i);
            --done;
        } else {
            ++i;
        }
    }
}

// ------------------------------------------------------- távoli szerkesztés

void MainWindow::configureRemote()
{
    QSettings s;
    const bool enabled = s.value("remote/enabled", false).toBool();
    const quint16 port = quint16(s.value("remote/port", remote::kDefaultPort).toUInt());
    const QByteArray salt = QByteArray::fromBase64(s.value("remote/salt").toByteArray());
    const QByteArray key = QByteArray::fromBase64(s.value("remote/key").toByteArray());
    remoteError_.clear();
    if (!enabled || key.isEmpty()) {
        if (remote_) remote_->stop();
        return;
    }
    if (!remote_) {
        remote_ = new remote::Server(this);
        connect(remote_, &remote::Server::authenticated, this, [this](remote::Connection *c) {
            say(TR("Távoli szerkesztő csatlakozott: %1").arg(c->peer()), 8000);
            c->send(remoteLibrary());
            c->send(remoteState());
        });
        connect(remote_, &remote::Server::request, this, [this](remote::Connection *c, const QJsonObject &m) {
            if (m.value("t").toString() != QLatin1String("op")) return;
            const QString err = applyRemoteOp(m);
            c->send(QJsonObject{{"t", "ack"}, {"id", m.value("id").toInt()}, {"ok", err.isEmpty()}, {"error", err}});
            remoteStateTimer_->start();
        });
        connect(remote_, &remote::Server::clientsChanged, this, [this](int n) {
            if (schedStatus_) schedStatus_->setToolTip(n ? TR("Távoli szerkesztők: %1").arg(n) : QString());
        });
    }
    if (remote_->running() && remote_->port() == port) {
        remote_->start(port, salt, key, &remoteError_);     // új jelszóval
        return;
    }
    if (!remote_->start(port, salt, key, &remoteError_))
        say(TR("A távoli szerkesztés nem indult el a(z) %1-es porton: %2").arg(port).arg(remoteError_), 12000);
}

QString MainWindow::remoteStatus() const
{
    if (!remote_ || !remote_->running())
        return remoteError_.isEmpty() ? TR("kikapcsolva") : TR("hiba: %1").arg(remoteError_);
    QStringList ips;
    for (const QHostAddress &a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback()) ips << a.toString();
    return TR("fut · cím: %1 · port: %2 · kapcsolatok: %3")
        .arg(ips.isEmpty() ? QStringLiteral("127.0.0.1") : ips.join(QStringLiteral(", ")))
        .arg(remote_->port())
        .arg(remote_->clientCount());
}

QJsonObject MainWindow::remoteLibrary()
{
    QJsonArray cats, items;
    for (const Category &c : db_->categories()) cats.push_back(remote::categoryToJson(c));
    for (const Item &it : db_->items()) items.push_back(remote::itemToJson(it));
    return QJsonObject{{"t", "library"}, {"station", QSysInfo::machineHostName()}, {"categories", cats}, {"items", items},
                       {"settings", QJsonObject{{"auto_mix_threshold_db", db_->setting("auto_mix_threshold_db", "-24")},
                                                {"default_fade_out_ms", db_->setting("default_fade_out_ms", "2500")}}}};
}

QJsonObject MainWindow::remoteState() const
{
    static const char *names[] = {"pending", "cued", "playing", "played", "skipped", "failed"};
    QJsonArray rows;
    QVector<int> ids;
    // A lejátszottakból csak az utolsó néhány (a szerkesztőnek elég a közelmúlt).
    int firstLive = 0;
    while (firstLive < rows_.size()
           && (rows_[firstLive].state == Row::Played || rows_[firstLive].state == Row::Skipped || rows_[firstLive].state == Row::Failed))
        ++firstLive;
    for (int i = std::max(0, firstLive - 8); i < rows_.size() && rows.size() < 600; ++i) {
        const Row &r = rows_[i];
        QJsonObject o{{"id", r.id}, {"pl", r.playlistId}, {"key", r.playlistKey}, {"item", r.item.id}, {"state", names[r.state]},
                      {"player", r.player}, {"start", i < startText_.size() ? startText_[i] : QString()},
                      {"src", r.scheduled ? "scheduler" : "manual"}};
        if (r.fixedAt.isValid()) o.insert("fixed", r.fixedAt.toString(Qt::ISODate));
        if (r.groupId) {
            o.insert("group", r.groupId);
            o.insert("groupName", r.groupName);
        }
        if (r.overlay) o.insert("ovl", QJsonObject{{"offset", r.overlayOffsetMs}, {"duck", r.overlayDuckDb}, {"voice", r.overlayVoice}});
        rows.push_back(o);
        ids << r.id;
    }
    QJsonArray trs;
    for (const Transition &t : const_cast<Database *>(db_)->transitionsFor(ids)) trs.push_back(remote::transitionToJson(t));
    return QJsonObject{{"t", "state"}, {"now", eter::now().toString(Qt::ISODate)}, {"mode", mode_ == Auto ? "auto" : "assist"},
                       {"rows", rows}, {"transitions", trs}};
}

QString MainWindow::applyRemoteOp(const QJsonObject &m)
{
    const QString op = m.value("op").toString();
    auto waitingRow = [this](int id) -> Row * {
        Row *r = rowById(id);
        return r && (r->state == Row::Pending || r->state == Row::Cued) ? r : nullptr;
    };
    auto knownRow = [this](int id) {
        const Row *r = rowById(id);
        return r && r->state != Row::Played && r->state != Row::Skipped && r->state != Row::Failed;
    };
    QString what;
    if (op == QLatin1String("saveTransition")) {
        const Transition t = remote::transitionFromJson(m);
        if (!knownRow(t.toEntryId) || !knownRow(t.fromEntryId)) return TR("Az átmenet sorai már nincsenek a playlistben.");
        if (rowById(t.toEntryId)->state == Row::Playing) return TR("Az adásban lévő elem átmenete már nem módosítható.");
        db_->saveTransition(t);
        onMixSaved({rowById(t.fromEntryId)->item.id, rowById(t.toEntryId)->item.id});
        what = TR("egyedi mix: %1 → %2").arg(rowById(t.fromEntryId)->item.title, rowById(t.toEntryId)->item.title);
    } else if (op == QLatin1String("deleteTransition")) {
        const int to = m.value("to").toInt();
        if (!knownRow(to)) return TR("A sor már nincs a playlistben.");
        db_->deleteTransition(to);
        onMixSaved({rowById(to)->item.id});
        what = TR("átmenet automatikusra");
    } else if (op == QLatin1String("setItemIntro") || op == QLatin1String("setItemCueIn") || op == QLatin1String("setItemMixDefaults")
               || op == QLatin1String("clearItemMixDefaults")) {
        const int itemId = m.value("item").toInt();
        const Item it = db_->item(itemId);
        if (!it.id) return TR("Az elem már nincs a hangtárban.");
        if (op == QLatin1String("setItemIntro")) db_->setItemIntro(itemId, m.value("ms").toInteger());
        else if (op == QLatin1String("setItemCueIn")) db_->setItemCueIn(itemId, m.value("ms").toInteger());
        else if (op == QLatin1String("clearItemMixDefaults")) db_->clearItemMixDefaults(itemId);
        else db_->setItemMixDefaults(itemId, m.value("mix").toInteger(), m.value("fadeOut").toInteger(), m.value("curve").toString(),
                                     m.value("cueOut").toInteger());
        onMixSaved({itemId});
        db_->notifyItemsChanged();
        what = TR("a szám beállításai: %1").arg(it.title);
    } else if (op == QLatin1String("overlay")) {
        const int id = m.value("entry").toInt();
        if (!rowById(id)) return TR("A sor már nincs a playlistben.");
        if (!setRowOverlay(id, m.value("on").toBool(), m.value("offset").toInteger(), m.value("duck").toDouble()))
            return TR("Ez az elem most nem fektethető rá (csak várakozó elem, két elem között).");
        what = m.value("on").toBool() ? TR("ráfektetés: %1").arg(rowById(id)->item.title) : TR("ráfektetés megszüntetve");
    } else if (op == QLatin1String("move")) {
        const int id = m.value("entry").toInt(), before = m.value("before").toInt();
        if (!waitingRow(id)) return TR("Csak a még várakozó sorok rendezhetők át.");
        const int from = indexOfRow(id);
        const int to = before > 0 && rowById(before) ? indexOfRow(before) : rows_.size();
        if (to < firstMovable()) return TR("Az adásban lévő elem elé nem tehető sor.");
        moveRow(from, to, true);
        what = TR("sorrend: %1").arg(rowById(id) ? rowById(id)->item.title : QString());
    } else if (op == QLatin1String("remove")) {
        Row *r = waitingRow(m.value("entry").toInt());
        if (!r) return TR("Csak a még várakozó sor törölhető.");
        what = TR("törölve: %1").arg(r->item.title);
        const int id = r->id;
        if (r->state == Row::Cued && r->player >= 0) {
            const int p = r->player;
            r->state = Row::Pending;
            r->player = -1;
            players_[p]->eject();
        }
        db_->deleteEntry(id);
        rows_.remove(indexOfRow(id));
        reconcile();
        refreshTable();
    } else {
        return TR("Ismeretlen művelet: %1").arg(op);
    }
    say(TR("Távoli szerkesztés – %1").arg(what), 6000);
    return {};
}
