#include "i18n.h"
#include "SchedulerPage.h"

#include <QMessageBox>
#include "Library.h"
#include "Theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSpinBox>
#include "sched/Scheduler.h"
#include <QSplitter>
#include <QTableView>
#include <QTableWidget>
#include <QTimeEdit>
#include <QVBoxLayout>

static const char *kDays[] = {"Hétfő", "Kedd", "Szerda", "Csütörtök", "Péntek", "Szombat", "Vasárnap"};

static QFrame *panel()
{
    auto *f = new QFrame;
    f->setObjectName("panel");
    return f;
}

static QLabel *caption(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName("caption");
    return l;
}

// A séma színéből sötét cellaháttér, hogy a világos szöveg olvasható maradjon rajta.
static QColor cellColor(const QString &hex)
{
    const QColor c(hex.isEmpty() ? QStringLiteral("#3a4150") : hex);
    const QColor base("#1b1e24");
    auto mix = [](int a, int b) { return (a * 38 + b * 62) / 100; };
    return QColor(mix(c.red(), base.red()), mix(c.green(), base.green()), mix(c.blue(), base.blue()));
}

SchedulerPage::SchedulerPage(Database *db, Scheduler *scheduler, QWidget *parent)
    : QWidget(parent), db_(db), scheduler_(scheduler)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setHandleWidth(8);
    split->setChildrenCollapsible(false);
    split->addWidget(buildGridPanel());
    split->addWidget(buildClockPanel());
    split->setStretchFactor(0, 12);
    split->setStretchFactor(1, 10);
    split->setSizes({760, 660});
    root->addWidget(split);

    connect(db_, &Database::clocksChanged, this, [this] {
        reloadClocks(currentClockId());
        reloadGrid();
    });
    connect(db_, &Database::categoriesChanged, this, [this] { reloadSlots(slots_.value(currentSlotRow()).id); });
    connect(db_, &Database::itemsChanged, this, [this] { categoryAvgMs_.clear(); });
    reloadClocks();
    reloadGrid();
}

// ------------------------------------------------------------ heti rács

QWidget *SchedulerPage::buildGridPanel()
{
    auto *p = panel();
    auto *v = new QVBoxLayout(p);
    v->setContentsMargins(1, 1, 1, 1);
    v->setSpacing(0);

    auto *head = new QFrame;
    head->setObjectName("panelHeader");
    auto *h = new QHBoxLayout(head);
    h->setContentsMargins(10, 8, 10, 8);
    h->setSpacing(6);
    auto *title = new QLabel(TR("Heti rács"));
    title->setObjectName("panelTitle");
    auto *hint = new QLabel(TR("Jelölj ki órákat, majd rendelj hozzájuk sémát"));
    hint->setObjectName("small");
    assign_ = new QPushButton(TR("Séma hozzárendelése"));
    assign_->setMenu(new QMenu(assign_));
    auto *clear = new QPushButton(TR("Kiürítés"));
    clear->setToolTip(TR("A kijelölt órákból kiveszi a sémát"));
    h->addWidget(title);
    h->addSpacing(8);
    h->addWidget(hint);
    h->addStretch(1);
    h->addWidget(assign_);
    h->addWidget(clear);
    v->addWidget(head);

    grid_ = new QTableWidget(24, 7);
    QStringList days, hours;
    for (const char *d : kDays) days << i18n::text(d);
    for (int i = 0; i < 24; ++i) hours << QStringLiteral("%1:00").arg(i, 2, 10, QLatin1Char('0'));
    grid_->setHorizontalHeaderLabels(days);
    grid_->setVerticalHeaderLabels(hours);
    grid_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    grid_->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    grid_->horizontalHeader()->setHighlightSections(false);
    grid_->verticalHeader()->setDefaultSectionSize(22);
    grid_->verticalHeader()->setMinimumSectionSize(18);
    grid_->verticalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    grid_->verticalHeader()->setHighlightSections(false);
    grid_->verticalHeader()->setDefaultAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    grid_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    grid_->setFrameShape(QFrame::NoFrame);
    grid_->setObjectName("weekGrid");
    grid_->setContextMenuPolicy(Qt::CustomContextMenu);
    v->addWidget(grid_, 1);

    auto *foot = new QFrame;
    foot->setObjectName("panelFooter");
    auto *f = new QHBoxLayout(foot);
    f->setContentsMargins(10, 7, 10, 7);
    f->setSpacing(10);
    genInfo_ = new QLabel(TR("A sémák és a rács módosítása a már elkészült playlisteket nem írja át; ahhoz generálj újra."));
    genInfo_->setObjectName("small");
    genInfo_->setWordWrap(true);
    auto *gen = new QPushButton(TR("Playlistek generálása"));
    gen->setProperty("accent", true);
    gen->setToolTip(TR("Mostantól a beállított óraszámra előre újra elkészíti az ütemezett sorokat. "
                                   "A kézzel betett és az éppen szóló elemek megmaradnak."));
    f->addWidget(genInfo_, 1);
    f->addWidget(gen);
    v->addWidget(foot);

    connect(gen, &QPushButton::clicked, this, &SchedulerPage::generateNow);
    connect(clear, &QPushButton::clicked, this, [this] { assignSelection(0); });
    connect(grid_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        // Egy cellára kattintva a hozzá tartozó séma jelenik meg a szerkesztőben.
        const int id = db_->weekGrid().value(col * 100 + row, 0);
        if (id && id != currentClockId()) reloadClocks(id);
    });
    connect(grid_, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu(this);
        for (const Clock &c : db_->clocks())
            menu.addAction(swatchIcon(QColor(c.color)), c.name, this, [this, c] { assignSelection(c.id); });
        if (!menu.isEmpty()) menu.addSeparator();
        menu.addAction(TR("Kiürítés"), this, [this] { assignSelection(0); });
        menu.exec(grid_->viewport()->mapToGlobal(pos));
    });
    return p;
}

void SchedulerPage::reloadGrid()
{
    const QHash<int, int> grid = db_->weekGrid();
    QHash<int, Clock> clocks;
    for (const Clock &c : db_->clocks()) clocks.insert(c.id, c);
    const QDateTime now = eter::now();
    const int today = now.date().dayOfWeek() - 1;
    for (int d = 0; d < 7; ++d) {
        auto *header = grid_->horizontalHeaderItem(d);
        header->setText(i18n::text(kDays[d]) + (d == today ? TR(" · ma") : QString()));
        for (int h = 0; h < 24; ++h) {
            auto *item = grid_->item(h, d);
            if (!item) {
                item = new QTableWidgetItem;
                item->setTextAlignment(Qt::AlignCenter);
                grid_->setItem(h, d, item);
            }
            const int id = grid.value(d * 100 + h, 0);
            if (id && clocks.contains(id)) {
                item->setText(clocks[id].name);
                item->setBackground(cellColor(clocks[id].color));
                item->setForeground(QColor("#e8eaed"));
            } else {
                item->setText({});
                item->setBackground(QColor("#16181d"));
            }
            QFont f = item->font();
            f.setBold(d == today && h == now.time().hour());
            item->setFont(f);
        }
    }
    QMenu *menu = assign_->menu();
    menu->clear();
    for (const Clock &c : clocks)
        menu->addAction(swatchIcon(QColor(c.color)), c.name, this, [this, c] { assignSelection(c.id); });
    assign_->setEnabled(!clocks.isEmpty());
}

void SchedulerPage::assignSelection(int clockId)
{
    const auto selected = grid_->selectedItems();
    if (selected.isEmpty()) {
        emit message(TR("Előbb jelölj ki órákat a rácsban."));
        return;
    }
    db_->beginBatch();
    for (const QTableWidgetItem *item : selected) db_->setGrid(item->column(), item->row(), clockId);
    db_->endBatch();
    reloadGrid();
    emit message(clockId ? TR("%1 óra beállítva.").arg(selected.size())
                         : TR("%1 óra kiürítve.").arg(selected.size()));
}

void SchedulerPage::generateNow()
{
    if (db_->weekGrid().isEmpty()) {
        emit message(TR("A heti rács üres: előbb rendelj sémát legalább egy órához."));
        return;
    }
    const int hours = std::clamp(db_->setting("generate_ahead_hours", "24").toInt(), 1, 168);
    const GenerateResult r = scheduler_->generate(eter::now(), hours, true);
    QStringList parts{TR("%1 órára %2 elem készült").arg(r.hours).arg(r.entries)};
    if (r.relaxed) parts << TR("%1 helyen nem volt az ismétlési szabálynak megfelelő elem").arg(r.relaxed);
    if (r.empty) parts << TR("%1 hely üresen maradt (üres kategória)").arg(r.empty);
    genInfo_->setText(parts.join(QStringLiteral(" · ")));
    emit generated(r);
}

// ---------------------------------------------------------- órasémák

QWidget *SchedulerPage::buildClockPanel()
{
    auto *p = panel();
    auto *v = new QVBoxLayout(p);
    v->setContentsMargins(1, 1, 1, 1);
    v->setSpacing(0);

    auto *head = new QFrame;
    head->setObjectName("panelHeader");
    auto *h = new QHBoxLayout(head);
    h->setContentsMargins(10, 8, 10, 8);
    h->setSpacing(6);
    auto *title = new QLabel(TR("Óraséma"));
    title->setObjectName("panelTitle");
    clock_ = new QComboBox;
    clock_->setMinimumWidth(150);
    clock_->setAccessibleName(TR("Óraséma kiválasztása"));
    clockName_ = new QLineEdit;
    clockName_->setPlaceholderText(TR("A séma neve"));
    clockName_->setMaxLength(40);
    clockName_->setAccessibleName(TR("A séma neve"));
    clockColor_ = new QComboBox;
    clockColor_->setAccessibleName(TR("A séma színe"));
    fillColorCombo(clockColor_);
    auto *add = new QPushButton(TR("Új"));
    auto *copy = new QPushButton(TR("Másolat"));
    auto *del = new QPushButton(TR("Törlés"));
    del->setProperty("danger", true);
    h->addWidget(title);
    h->addWidget(clock_);
    h->addWidget(clockName_, 1);
    h->addWidget(clockColor_);
    h->addWidget(add);
    h->addWidget(copy);
    h->addWidget(del);
    v->addWidget(head);

    slotTable_ = new QTableWidget(0, 5);
    slotTable_->setHorizontalHeaderLabels({QStringLiteral("#"), TR("Időzítés"), TR("Típus"),
                                           TR("Forrás"), TR("Becsült")});
    slotTable_->verticalHeader()->hide();
    slotTable_->verticalHeader()->setDefaultSectionSize(28);
    slotTable_->setShowGrid(false);
    slotTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    slotTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    slotTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    slotTable_->setFrameShape(QFrame::NoFrame);
    slotTable_->setIconSize(QSize(14, 14));
    auto *hh = slotTable_->horizontalHeader();
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hh->setHighlightSections(false);
    hh->setSectionResizeMode(3, QHeaderView::Stretch);
    slotTable_->setColumnWidth(0, 36);
    slotTable_->setColumnWidth(1, 110);
    slotTable_->setColumnWidth(2, 100);
    slotTable_->setColumnWidth(4, 76);
    v->addWidget(slotTable_, 1);

    auto *bar = new QFrame;
    bar->setObjectName("panelFooter");
    auto *b = new QHBoxLayout(bar);
    b->setContentsMargins(10, 7, 10, 7);
    b->setSpacing(6);
    auto *addSlot = new QPushButton(TR("+ Elem"));
    auto *delSlot = new QPushButton(TR("Törlés"));
    auto *up = new QPushButton(QStringLiteral("↑"));
    up->setAccessibleName(TR("Feljebb"));
    auto *down = new QPushButton(QStringLiteral("↓"));
    down->setAccessibleName(TR("Lejjebb"));
    total_ = new QLabel;
    total_->setObjectName("small");
    b->addWidget(addSlot);
    b->addWidget(delSlot);
    b->addWidget(up);
    b->addWidget(down);
    b->addStretch(1);
    b->addWidget(total_);
    v->addWidget(bar);

    // A kijelölt elem beállításai
    slotForm_ = new QFrame;
    slotForm_->setObjectName("panelFooter");
    auto *fl = new QVBoxLayout(slotForm_);
    fl->setContentsMargins(10, 10, 10, 10);
    fl->setSpacing(8);
    fl->addWidget(caption(TR("A KIJELÖLT ELEM")));
    auto *row1 = new QHBoxLayout;
    row1->setSpacing(8);
    slotType_ = new QComboBox;
    slotType_->addItem(TR("Kategóriából, véletlenszerűen"), "category");
    slotType_->addItem(TR("Konkrét elem"), "item");
    slotType_->addItem(TR("Kitöltés a következő fix pontig"), "fill");
    slotType_->setAccessibleName(TR("Az elem típusa"));
    slotCategoryLabel_ = new QLabel(TR("Kategória"));
    slotCategory_ = new QComboBox;
    slotCategory_->setMinimumWidth(150);
    slotCategoryLabel_->setBuddy(slotCategory_);
    slotCountLabel_ = new QLabel(TR("Darab"));
    slotCount_ = new QSpinBox;
    slotCount_->setRange(1, 20);
    slotCountLabel_->setBuddy(slotCount_);
    slotItemLabel_ = new QLabel;
    slotItemLabel_->setTextFormat(Qt::PlainText);
    slotItemLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    slotPick_ = new QPushButton(TR("Választás…"));
    row1->addWidget(slotType_);
    row1->addWidget(slotCategoryLabel_);
    row1->addWidget(slotCategory_, 1);
    row1->addWidget(slotCountLabel_);
    row1->addWidget(slotCount_);
    row1->addWidget(slotItemLabel_, 1);
    row1->addWidget(slotPick_);
    fl->addLayout(row1);
    auto *row2 = new QHBoxLayout;
    row2->setSpacing(8);
    slotFixed_ = new QCheckBox(TR("Fix időpont az órán belül (perc:másodperc)"));
    slotTime_ = new QTimeEdit;
    slotTime_->setDisplayFormat("mm:ss");
    slotTime_->setAccessibleName(TR("Fix időpont"));
    row2->addWidget(slotFixed_);
    row2->addWidget(slotTime_);
    row2->addSpacing(16);
    // Energiasáv: csak ilyen energiájú elem kerülhet ide (az ismeretlen energiájú is).
    slotEnergyLabel_ = new QLabel(TR("Energia"));
    slotEnergyMin_ = new QComboBox;
    slotEnergyMax_ = new QComboBox;
    slotEnergyMin_->addItem(TR("bármi"), 0);
    slotEnergyMax_->addItem(TR("bármi"), 0);
    for (int e = 1; e <= 5; ++e) {
        slotEnergyMin_->addItem(QString::number(e), e);
        slotEnergyMax_->addItem(QString::number(e), e);
    }
    slotEnergyMin_->setToolTip(TR("A legkisebb energia, ami ide kerülhet (1 nagyon nyugodt … 5 nagyon pörgős)"));
    slotEnergyMax_->setToolTip(TR("A legnagyobb energia, ami ide kerülhet"));
    slotEnergyLabel_->setBuddy(slotEnergyMin_);
    slotEnergyDash_ = new QLabel(QStringLiteral("–"));
    row2->addWidget(slotEnergyLabel_);
    row2->addWidget(slotEnergyMin_);
    row2->addWidget(slotEnergyDash_);
    row2->addWidget(slotEnergyMax_);
    row2->addStretch(1);
    fl->addLayout(row2);
    slotHint_ = new QLabel;
    slotHint_->setObjectName("small");
    slotHint_->setWordWrap(true);
    fl->addWidget(slotHint_);
    v->addWidget(slotForm_);

    // Szabályok
    auto *rules = new QFrame;
    rules->setObjectName("panelFooter");
    auto *rl = new QVBoxLayout(rules);
    rl->setContentsMargins(10, 10, 10, 10);
    rl->setSpacing(8);
    rl->addWidget(caption(TR("SZABÁLYOK")));
    auto *rr = new QHBoxLayout;
    rr->setSpacing(8);
    auto spin = [&](const QString &label, const QString &suffix, int max, const QString &key, const QString &def) {
        auto *l = new QLabel(label);
        auto *s = new QSpinBox;
        s->setRange(0, max);
        s->setSuffix(suffix);
        s->setValue(db_->setting(key, def).toInt());
        l->setBuddy(s);
        rr->addWidget(l);
        rr->addWidget(s);
        connect(s, &QSpinBox::valueChanged, this, [this, key](int v) { db_->setSetting(key, QString::number(v)); });
        return s;
    };
    artistGap_ = spin(TR("Előadó újra"), TR(" perc"), 1440, "artist_separation_min", "90");
    titleGap_ = spin(TR("Szám újra"), TR(" perc"), 10080, "title_separation_min", "240");
    ahead_ = spin(TR("Előre"), TR(" óra"), 168, "generate_ahead_hours", "24");
    ahead_->setMinimum(1);
    rr->addStretch(1);
    rl->addLayout(rr);
    auto *rr2 = new QHBoxLayout;
    rr2->setSpacing(8);
    auto *policyLabel = new QLabel(TR("Fix időpontkor"));
    policy_ = new QComboBox;
    policy_->addItem(TR("a szóló elemet kiúsztatja"), "fade");
    policy_->addItem(TR("megvárja a szóló elem végét"), "wait");
    policy_->setCurrentIndex(std::max(0, policy_->findData(db_->setting("fixed_time_policy", "fade"))));
    policyLabel->setBuddy(policy_);
    autoGen_ = new QCheckBox(TR("Automatikus generálás"));
    autoGen_->setToolTip(TR("Percenként ellenőrzi, és előre elkészíti a hiányzó órák playlistjét"));
    autoGen_->setChecked(db_->setting("auto_generate", "1") == "1");
    rr2->addWidget(policyLabel);
    rr2->addWidget(policy_);
    rr2->addSpacing(12);
    rr2->addWidget(autoGen_);
    rr2->addStretch(1);
    rl->addLayout(rr2);
    // Pontos időre igazítás: automata módban a fix pont előtt 20 perccel kitölti vagy rövidíti a playlistet.
    // A zenei folyam szabályai (energia, tempó) és a napszakok határai.
    auto *rr4 = new QHBoxLayout;
    rr4->setSpacing(8);
    rr = rr4;                                     // a spin() innentől ebbe a sorba tesz
    auto *slowMax = spin(TR("Lassú egymás után"), QString(), 5, "rule_slow_max", "0");
    slowMax->setSpecialValueText(TR("nincs szabály"));
    slowMax->setToolTip(TR("Legfeljebb ennyi lassú (1-es vagy 2-es energiájú) szám jöhet egymás után. 1: két lassú szám nem jöhet egymás után."));
    auto *jump = spin(TR("Energiaugrás"), QString(), 4, "rule_energy_jump", "0");
    jump->setSpecialValueText(TR("nincs szabály"));
    jump->setToolTip(TR("Két egymást követő szám energiája legfeljebb ennyivel térhet el (1: csak fokozatosan változik)"));
    auto *bpmJump = spin(TR("Tempóváltás"), TR(" %"), 60, "rule_bpm_jump_pct", "0");
    bpmJump->setSpecialValueText(TR("nincs szabály"));
    bpmJump->setToolTip(TR("Két egymást követő szám tempója legfeljebb ennyi százalékkal térhet el (a fél- és dupla tempó egyezőnek számít)"));
    rr4->addStretch(1);
    rl->addLayout(rr4);
    auto *rr5 = new QHBoxLayout;
    rr5->setSpacing(8);
    rr5->addWidget(new QLabel(TR("Napszakok kezdete")));
    {
        const QVector<int> starts = Scheduler::daypartStarts(db_);
        QVector<QSpinBox *> hours;
        for (int p = 1; p < Scheduler::kDayparts; ++p) {
            auto *h = new QSpinBox;
            h->setRange(0, 24);
            h->setSuffix(TR(" óra"));
            h->setValue(starts[p]);
            rr5->addWidget(new QLabel(Scheduler::daypartName(p)));
            rr5->addWidget(h);
            hours << h;
        }
        for (QSpinBox *h : hours)
            connect(h, &QSpinBox::valueChanged, this, [this, hours] {
                QStringList parts{QStringLiteral("0")};
                for (QSpinBox *x : hours) parts << QString::number(x->value());
                db_->setSetting("daypart_starts", parts.join(QLatin1Char(',')));
            });
        for (QSpinBox *h : hours) h->setToolTip(TR("Az Éjjel 0 órakor kezdődik. Az elemek napszakai a hangtárban (jobb gomb → Napszakok…) "
                                                    "vagy az elemszerkesztőben állíthatók."));
    }
    rr5->addStretch(1);
    rl->addLayout(rr5);

    auto *rr3 = new QHBoxLayout;
    rr3->setSpacing(8);
    auto *adjustLabel = new QLabel(TR("Fix pontok igazítása"));
    fixAdjust_ = new QComboBox;
    fixAdjust_->addItem(TR("kézzel (csak jelzi)"), "manual");
    fixAdjust_->addItem(TR("automatikus kitöltés"), "fill");
    fixAdjust_->addItem(TR("automatikus kitöltés és rövidítés"), "both");
    fixAdjust_->setToolTip(TR("Automata üzemmódban a fix idejű elem előtt 20 perccel: ha a playlist nem érne oda pontosan, "
                              "elemet tesz be (kitöltés) vagy vesz ki (rövidítés)"));
    fixAdjust_->setCurrentIndex(std::max(0, fixAdjust_->findData(db_->setting("fix_adjust", "manual"))));
    adjustLabel->setBuddy(fixAdjust_);
    auto *fillLabel = new QLabel(TR("Kitöltés ebből"));
    fixCategory_ = new QComboBox;
    fillLabel->setBuddy(fixCategory_);
    reloadFixCategories();
    rr3->addWidget(adjustLabel);
    rr3->addWidget(fixAdjust_);
    rr3->addSpacing(12);
    rr3->addWidget(fillLabel);
    rr3->addWidget(fixCategory_);
    rr3->addStretch(1);
    rl->addLayout(rr3);
    connect(fixAdjust_, &QComboBox::activated, this, [this] { db_->setSetting("fix_adjust", fixAdjust_->currentData().toString()); });
    connect(fixCategory_, &QComboBox::activated, this, [this] { db_->setSetting("fix_fill_category", fixCategory_->currentData().toString()); });
    connect(db_, &Database::categoriesChanged, this, &SchedulerPage::reloadFixCategories);
    v->addWidget(rules);

    connect(policy_, &QComboBox::activated, this, [this] { db_->setSetting("fixed_time_policy", policy_->currentData().toString()); });
    connect(autoGen_, &QCheckBox::toggled, this, [this](bool on) { db_->setSetting("auto_generate", on ? "1" : "0"); });

    connect(clock_, &QComboBox::activated, this, [this] { reloadClocks(clock_->currentData().toInt()); });
    connect(add, &QPushButton::clicked, this, [this] {
        QString name = TR("Új séma");
        for (int n = 2; !db_->addClock(name, "#5cb0ff"); ++n) {
            name = TR("Új séma %1").arg(n);
            if (n > 50) return;
        }
        for (const Clock &c : db_->clocks())
            if (c.name == name) reloadClocks(c.id);
        clockName_->setFocus();
        clockName_->selectAll();
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        const int id = currentClockId();
        if (!id) return;
        const int made = db_->duplicateClock(id, clockName_->text() + TR(" másolata"));
        if (made) reloadClocks(made);
        else emit message(TR("Már van ilyen nevű másolat; nevezd át előbb."));
    });
    connect(del, &QPushButton::clicked, this, [this] {
        const int id = currentClockId();
        if (!id) return;
        if (QMessageBox::question(this, TR("Óraséma törlése"),
                                  TR("Törlöd a(z) „%1” órasémát? A heti rácsból is kikerül.").arg(clockName_->text()),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        db_->deleteClock(id);        // a rácsból is kikerül
        emit message(TR("A séma törölve, a heti rácsból is kikerült."));
    });
    auto saveClock = [this] {
        const int id = currentClockId();
        if (!id || loading_) return;
        const QString name = clockName_->text().trimmed();
        if (name.isEmpty()) return;
        QString err;
        if (!db_->updateClock(id, name, clockColor_->currentData().toString(), &err)) emit message(err);
    };
    connect(clockName_, &QLineEdit::editingFinished, this, saveClock);
    connect(clockColor_, &QComboBox::activated, this, saveClock);

    connect(slotTable_, &QTableWidget::currentCellChanged, this, [this] { showSlot(); });
    connect(addSlot, &QPushButton::clicked, this, [this] {
        const int id = currentClockId();
        if (!id) {
            emit message(TR("Előbb hozz létre egy órasémát az „Új” gombbal."));
            return;
        }
        const QVector<Category> cats = db_->itemCategories();
        if (cats.isEmpty()) {
            emit message(TR("Előbb hozz létre kategóriát a hangtárban."));
            return;
        }
        ClockSlot s;
        s.clockId = id;
        s.type = "category";
        // Az új elem alapból az előző elem kategóriáját kapja.
        s.categoryId = slots_.isEmpty() || !slots_.last().categoryId ? cats.first().id : slots_.last().categoryId;
        reloadSlots(db_->addSlot(s));
    });
    connect(delSlot, &QPushButton::clicked, this, [this] {
        const int row = currentSlotRow();
        if (row < 0) return;
        db_->deleteSlot(slots_[row].id);
        const int nextId = row + 1 < slots_.size() ? slots_[row + 1].id : (row > 0 ? slots_[row - 1].id : 0);
        reloadSlots(nextId);
    });
    connect(up, &QPushButton::clicked, this, [this] { moveSlot(-1); });
    connect(down, &QPushButton::clicked, this, [this] { moveSlot(1); });

    connect(slotType_, &QComboBox::activated, this, &SchedulerPage::applySlot);
    connect(slotCategory_, &QComboBox::activated, this, &SchedulerPage::applySlot);
    connect(slotCount_, &QSpinBox::valueChanged, this, &SchedulerPage::applySlot);
    connect(slotFixed_, &QCheckBox::toggled, this, &SchedulerPage::applySlot);
    connect(slotEnergyMin_, &QComboBox::activated, this, &SchedulerPage::applySlot);
    connect(slotEnergyMax_, &QComboBox::activated, this, &SchedulerPage::applySlot);
    connect(slotTime_, &QTimeEdit::timeChanged, this, &SchedulerPage::applySlot);
    connect(slotPick_, &QPushButton::clicked, this, &SchedulerPage::pickItem);
    return p;
}

int SchedulerPage::currentClockId() const { return clock_->currentData().toInt(); }

int SchedulerPage::currentSlotRow() const
{
    const int r = slotTable_->currentRow();
    return r >= 0 && r < slots_.size() ? r : -1;
}

void SchedulerPage::reloadClocks(int selectId)
{
    loading_ = true;
    const QVector<Clock> clocks = db_->clocks();
    clock_->clear();
    for (const Clock &c : clocks) clock_->addItem(swatchIcon(QColor(c.color)), c.name, c.id);
    int idx = clock_->findData(selectId);
    if (idx < 0 && !clocks.isEmpty()) idx = 0;
    clock_->setCurrentIndex(idx);
    const bool has = idx >= 0;
    clockName_->setEnabled(has);
    clockColor_->setEnabled(has);
    if (has) {
        clockName_->setText(clocks[idx].name);
        clockColor_->setCurrentIndex(std::max(0, clockColor_->findData(clocks[idx].color)));
    } else {
        clockName_->clear();
    }
    loading_ = false;
    reloadSlots();
}

void SchedulerPage::reloadSlots(int selectSlotId)
{
    if (categoryAvgMs_.isEmpty()) {
        QHash<int, QPair<qint64, int>> sum;
        for (const Item &it : db_->items()) {
            sum[it.categoryId].first += it.durationMs;
            sum[it.categoryId].second += 1;
        }
        for (auto it = sum.begin(); it != sum.end(); ++it)
            categoryAvgMs_[it.key()] = it.value().second ? it.value().first / it.value().second : 0;
        categoryAvgMs_[-1] = 0;      // jelzi, hogy a gyorsítótár fel van töltve
    }
    loading_ = true;
    const int clockId = currentClockId();
    slots_ = clockId ? db_->clockSlots(clockId) : QVector<ClockSlot>();
    slotTable_->setRowCount(slots_.size());
    qint64 total = 0;
    bool hasFill = false;
    int selectRow = -1;
    QFont mono("IBM Plex Mono");
    mono.setPixelSize(12);
    for (int r = 0; r < slots_.size(); ++r) {
        const ClockSlot &s = slots_[r];
        QString timing = TR("folytatólagos");
        if (s.fixedOffsetS >= 0)
            timing = TR("fix :%1:%2").arg(s.fixedOffsetS / 60, 2, 10, QLatin1Char('0')).arg(s.fixedOffsetS % 60, 2, 10, QLatin1Char('0'));
        QString source, estimate;
        qint64 ms = 0;
        if (s.type == "item") {
            source = s.label.isEmpty() ? TR("(nincs kiválasztva)") : s.label;
            ms = s.itemId ? db_->item(s.itemId).durationMs : 0;
            estimate = formatMs(ms);
        } else if (s.type == "fill") {
            source = TR("Kitöltés: %1").arg(s.label);
            estimate = TR("változó");
            hasFill = true;
        } else {
            source = s.count > 1 ? QStringLiteral("%1 × %2").arg(s.count).arg(s.label) : s.label;
            ms = categoryAvgMs_.value(s.categoryId, 0) * s.count;
        }
        if (s.type != "item" && (s.energyMin || s.energyMax)) {
            const QString lo = s.energyMin ? QString::number(s.energyMin) : QStringLiteral("1"),
                          hi = s.energyMax ? QString::number(s.energyMax) : QStringLiteral("5");
            source += TR("  · energia %1").arg(lo == hi ? lo : lo + QStringLiteral("–") + hi);
        }
        if (s.type == "category") {
            estimate = QStringLiteral("~") + formatMs(ms);
        }
        total += ms;
        const QString cells[5] = {QString::number(r + 1), timing, kindLabel(s.kind), source, estimate};
        for (int c = 0; c < 5; ++c) {
            auto *item = new QTableWidgetItem(cells[c]);
            if (c == 0 || c == 1 || c == 4) item->setFont(mono);
            if (c == 1 && s.fixedOffsetS >= 0) item->setForeground(theme::amberSoft);
            if (c == 3 && !s.color.isEmpty()) item->setIcon(swatchIcon(QColor(s.color)));
            if (c == 4) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            slotTable_->setItem(r, c, item);
        }
        if (s.id == selectSlotId) selectRow = r;
    }
    if (selectRow < 0 && !slots_.isEmpty()) selectRow = 0;
    if (selectRow >= 0) slotTable_->setCurrentCell(selectRow, 0);
    total_->setText(slots_.isEmpty() ? TR("Üres séma")
                    : hasFill ? TR("Fix hosszú rész: ~%1 · a többit a kitöltés adja").arg(formatMs(total))
                              : TR("Becsült hossz: ~%1 / 60:00").arg(formatMs(total)));
    loading_ = false;
    showSlot();
}

void SchedulerPage::showSlot()
{
    if (loading_) return;
    const int row = currentSlotRow();
    slotForm_->setEnabled(row >= 0);
    if (row < 0) {
        slotHint_->setText(currentClockId() ? TR("A „+ Elem” gombbal adj elemeket a sémához, felülről lefelé abban a sorrendben, ahogy az órában következnek.")
                                            : TR("Még nincs óraséma. Az „Új” gombbal hozz létre egyet."));
        return;
    }
    loading_ = true;
    const ClockSlot &s = slots_[row];
    slotType_->setCurrentIndex(std::max(0, slotType_->findData(s.type)));
    slotCategory_->clear();
    for (const Category &c : db_->itemCategories())
        slotCategory_->addItem(swatchIcon(QColor(c.color)), QStringLiteral("%1 (%2)").arg(c.name).arg(c.itemCount), c.id);
    slotCategory_->setCurrentIndex(std::max(0, slotCategory_->findData(s.categoryId)));
    slotCount_->setValue(s.count);
    slotFixed_->setChecked(s.fixedOffsetS >= 0);
    const int off = std::max(0, s.fixedOffsetS);
    slotTime_->setTime(QTime(0, off / 60, off % 60));
    slotItemLabel_->setText(s.label.isEmpty() ? TR("(nincs kiválasztva)") : s.label);
    slotEnergyMin_->setCurrentIndex(std::max(0, slotEnergyMin_->findData(s.energyMin)));
    slotEnergyMax_->setCurrentIndex(std::max(0, slotEnergyMax_->findData(s.energyMax)));

    const bool isItem = s.type == "item", isFill = s.type == "fill";
    slotCategoryLabel_->setVisible(!isItem);
    slotCategory_->setVisible(!isItem);
    slotCountLabel_->setVisible(!isItem && !isFill);
    slotCount_->setVisible(!isItem && !isFill);
    slotItemLabel_->setVisible(isItem);
    slotPick_->setVisible(isItem);
    slotFixed_->setVisible(!isFill);
    slotTime_->setVisible(!isFill);
    for (QWidget *w : std::initializer_list<QWidget *>{slotEnergyLabel_, slotEnergyMin_, slotEnergyDash_, slotEnergyMax_}) w->setVisible(!isItem);
    slotTime_->setEnabled(s.fixedOffsetS >= 0);
    slotHint_->setText(isFill ? TR("Annyi elemet tesz be ebből a kategóriából, amennyi a következő fix időpontig (vagy az óra végéig) kell.")
                       : s.fixedOffsetS >= 0 ? TR("Automata üzemmódban pontosan ekkor indul. Ha addig nem telne ki az idő, az ütemező az előző zenei kategóriából pótol.")
                                             : TR("Az előző elem után következik."));
    loading_ = false;
}

void SchedulerPage::applySlot()
{
    if (loading_) return;
    const int row = currentSlotRow();
    if (row < 0) return;
    ClockSlot s = slots_[row];
    s.type = slotType_->currentData().toString();
    if (s.type != "item") {
        s.categoryId = slotCategory_->currentData().toInt();
        if (!s.categoryId) {
            const QVector<Category> cats = db_->itemCategories();
            if (cats.isEmpty()) return;
            s.categoryId = cats.first().id;
        }
    } else if (!s.itemId) {
        // Elem nélkül nem menthető „konkrét elem": előbb választani kell.
        pickItem();
        return;
    }
    s.count = s.type == "category" ? slotCount_->value() : 1;
    s.energyMin = s.type != "item" ? slotEnergyMin_->currentData().toInt() : 0;
    s.energyMax = s.type != "item" ? slotEnergyMax_->currentData().toInt() : 0;
    if (s.energyMin && s.energyMax && s.energyMin > s.energyMax) std::swap(s.energyMin, s.energyMax);
    const QTime t = slotTime_->time();
    s.fixedOffsetS = s.type != "fill" && slotFixed_->isChecked() ? t.minute() * 60 + t.second() : -1;
    db_->updateSlot(s);
    reloadSlots(s.id);
}

void SchedulerPage::moveSlot(int delta)
{
    const int row = currentSlotRow();
    const int to = row + delta;
    if (row < 0 || to < 0 || to >= slots_.size()) return;
    QVector<int> ids;
    for (const ClockSlot &s : slots_) ids << s.id;
    ids.swapItemsAt(row, to);
    db_->reorderSlots(currentClockId(), ids);
    reloadSlots(ids[to]);
}

void SchedulerPage::pickItem()
{
    const int row = currentSlotRow();
    if (row < 0) return;
    const int slotId = slots_[row].id;
    if (picker_) picker_->close();
    auto *dlg = new ItemPickerDialog(db_, window(), QStringLiteral("picker_category_clock"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    picker_ = dlg;
    connect(dlg, &ItemPickerDialog::picked, this, [this, slotId](int itemId) {
        for (ClockSlot s : slots_) {
            if (s.id != slotId) continue;
            s.type = "item";
            s.itemId = itemId;
            s.categoryId = 0;
            s.count = 1;
            db_->updateSlot(s);
        }
        reloadSlots(slotId);
    });
    connect(dlg, &QDialog::rejected, this, [this, slotId] { reloadSlots(slotId); });
    dlg->setModal(false);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

// --------------------------------------------------------- elemválasztó

ItemPickerDialog::ItemPickerDialog(Database *db, QWidget *parent, const QString &rememberKey) : QDialog(parent)
{
    setWindowTitle(TR("Elem kiválasztása a hangtárból"));
    resize(680, 460);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 14);
    root->setSpacing(10);
    auto *search = new QLineEdit;
    search->setPlaceholderText(TR("Keresés cím, előadó szerint…"));
    search->setClearButtonEnabled(true);
    search->setAccessibleName(TR("Keresés"));
    // Kategória szűrő; a legutóbbi választást megjegyzi.
    auto *category = new QComboBox;
    category->setObjectName("pickerCategory");
    category->setMinimumWidth(190);
    category->setIconSize(QSize(12, 12));
    category->setAccessibleName(TR("Kategória szűrő"));
    category->addItem(TR("Minden kategória"), 0);
    for (const Category &c : db->itemCategories())
        category->addItem(swatchIcon(QColor(c.color)), QStringLiteral("%1 (%2)").arg(c.name).arg(c.itemCount), c.id);
    if (!rememberKey.isEmpty())
        category->setCurrentIndex(std::max(0, category->findData(db->setting(rememberKey, "0").toInt())));
    auto *filters = new QHBoxLayout;
    filters->setSpacing(8);
    filters->addWidget(category);
    filters->addWidget(search, 1);
    root->addLayout(filters);
    model_ = new LibraryModel(db, this);
    view_ = new QTableView;
    view_->setModel(model_);
    view_->verticalHeader()->hide();
    view_->verticalHeader()->setDefaultSectionSize(28);
    view_->setShowGrid(false);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setSortingEnabled(true);
    view_->sortByColumn(0, Qt::AscendingOrder);
    view_->setIconSize(QSize(14, 14));
    view_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    view_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    view_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    view_->setColumnWidth(2, 140);
    view_->setColumnWidth(3, 60);
    view_->hideColumn(4);
    root->addWidget(view_, 1);
    auto *buttons = new QHBoxLayout;
    auto *cancel = new QPushButton(TR("Mégse"));
    auto *ok = new QPushButton(TR("Kiválasztás"));
    ok->setProperty("accent", true);
    ok->setDefault(true);
    buttons->addStretch(1);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    root->addLayout(buttons);

    auto choose = [this] {
        if (const Item *it = model_->itemAt(view_->currentIndex().row())) {
            emit picked(it->id);
            accept();
        }
    };
    auto applyFilter = [this, category, search] { model_->setFilter(category->currentData().toInt(), search->text()); };
    connect(search, &QLineEdit::textChanged, this, applyFilter);
    connect(category, &QComboBox::currentIndexChanged, this, [applyFilter, category, db, rememberKey] {
        applyFilter();
        if (!rememberKey.isEmpty()) db->setSetting(rememberKey, QString::number(category->currentData().toInt()));
    });
    applyFilter();
    search->setFocus();
    connect(ok, &QPushButton::clicked, this, choose);
    connect(view_, &QTableView::doubleClicked, this, choose);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
}

void SchedulerPage::reloadFixCategories()
{
    const QString current = db_->setting("fix_fill_category", "0");
    fixCategory_->clear();
    fixCategory_->addItem(TR("az előtte szóló zene kategóriája"), QStringLiteral("0"));
    for (const Category &c : db_->itemCategories()) fixCategory_->addItem(c.name, QString::number(c.id));
    fixCategory_->setCurrentIndex(std::max(0, fixCategory_->findData(current)));
}
