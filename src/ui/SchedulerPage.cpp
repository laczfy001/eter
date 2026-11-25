#include "i18n.h"
#include "Widgets.h"
#include "SchedulerPage.h"

#include <QMessageBox>
#include "Library.h"
#include "core/ArtistSplit.h"
#include "Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateEdit>
#include <QDoubleSpinBox>
#include <QDialog>
#include <QInputDialog>
#include <functional>
#include <QDropEvent>
#include <QMimeData>
#include <QTimer>
#include <algorithm>
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
#include <QToolButton>
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
    const QColor base = theme::c("#1b1e24");
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
    connect(db_, &Database::weekGridsChanged, this, [this] {
        if (gridSel_) reloadGridList();
    });
    editGrid_ = db_->gridFor(eter::now().date());
    reloadGridList();
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

    // Heti rácsok (előbeállítások): kézzel választott aktív rács, és időszakok, amikor egy másik érvényes.
    auto *gridBar = new QFrame;
    gridBar->setObjectName("panelFooter");
    auto *gb = new QHBoxLayout(gridBar);
    gb->setContentsMargins(10, 6, 10, 6);
    gb->setSpacing(6);
    auto *gridLabel = new QLabel(TR("Rács"));
    gridSel_ = new QComboBox;
    gridSel_->setObjectName("weekGridSelect");
    gridSel_->setMinimumWidth(230);
    gridSel_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    gridLabel->setBuddy(gridSel_);
    auto *gridNew = new QToolButton;
    gridNew->setText(TR("Új"));
    gridNew->setToolTip(TR("Új, üres heti rács"));
    auto *gridCopy = new QToolButton;
    gridCopy->setText(TR("Másolat"));
    gridCopy->setToolTip(TR("Új heti rács ennek a másolataként"));
    auto *gridRename = new QToolButton;
    gridRename->setText(TR("Átnevezés"));
    gridDelete_ = new QPushButton(TR("Törlés"));
    gridDelete_->setProperty("danger", true);
    gridActive_ = new QPushButton(TR("Legyen ez az aktív"));
    gridActive_->setToolTip(TR("Kézi váltás: ez a rács érvényes, amikor egyik időszak sem tart"));
    auto *periods = new QPushButton(TR("Időszakok…"));
    periods->setObjectName("weekGridPeriods");
    periods->setToolTip(TR("Automatikus váltás: dátumtól dátumig (akár minden évben) egy másik rács érvényes"));
    gridState_ = new QLabel;
    gridState_->setObjectName("small");
    gb->addWidget(gridLabel);
    gb->addWidget(gridSel_);
    gb->addWidget(gridNew);
    gb->addWidget(gridCopy);
    gb->addWidget(gridRename);
    gb->addWidget(gridDelete_);
    gb->addStretch(1);
    gb->addWidget(gridState_);
    gb->addWidget(gridActive_);
    gb->addWidget(periods);
    v->addWidget(gridBar);
    connect(gridSel_, &QComboBox::activated, this, [this] {
        editGrid_ = gridSel_->currentData().toInt();
        reloadGridList();
        reloadGrid();
    });
    auto askName = [this](const QString &title, const QString &def) {
        bool ok = false;
        const QString name = QInputDialog::getText(this, title, TR("A rács neve:"), QLineEdit::Normal, def, &ok).trimmed();
        return ok ? (name.isEmpty() ? QStringLiteral(" ") : name) : QString();
    };
    connect(gridNew, &QToolButton::clicked, this, [this, askName] {
        const QString name = askName(TR("Új heti rács"), QString());
        if (name.isEmpty()) return;
        editGrid_ = db_->addWeekGrid(name.trimmed());
        reloadGridList();
        reloadGrid();
    });
    connect(gridCopy, &QToolButton::clicked, this, [this, askName] {
        const QString name = askName(TR("Rács másolása"), TR("%1 (másolat)").arg(gridSel_->currentData(Qt::UserRole + 1).toString()));
        if (name.isEmpty()) return;
        editGrid_ = db_->addWeekGrid(name.trimmed(), editGrid_);
        reloadGridList();
        reloadGrid();
    });
    connect(gridRename, &QToolButton::clicked, this, [this, askName] {
        const QString name = askName(TR("Rács átnevezése"), gridSel_->currentData(Qt::UserRole + 1).toString());
        if (name.isEmpty()) return;
        db_->renameWeekGrid(editGrid_, name.trimmed());
        reloadGridList();
    });
    connect(gridDelete_, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, TR("Rács törlése"),
                                  TR("Törlöd a(z) „%1” heti rácsot és az időszakait?").arg(gridSel_->currentData(Qt::UserRole + 1).toString()))
            != QMessageBox::Yes)
            return;
        if (db_->deleteWeekGrid(editGrid_)) editGrid_ = db_->activeWeekGrid();
        reloadGridList();
        reloadGrid();
    });
    connect(gridActive_, &QPushButton::clicked, this, [this] {
        db_->setActiveWeekGrid(editGrid_);
        reloadGridList();
        emit message(TR("Aktív heti rács: %1. A már elkészült playlistekhez generálj újra.").arg(gridSel_->currentData(Qt::UserRole + 1).toString()));
    });
    connect(periods, &QPushButton::clicked, this, &SchedulerPage::editPeriods);

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
    gen->setToolTip(TR("Megadhatod, mely napok mely óráira készüljön (újra) playlist. "
                       "A kézzel betett és az éppen szóló elemek megmaradnak."));
    f->addWidget(genInfo_, 1);
    f->addWidget(gen);
    v->addWidget(foot);

    connect(gen, &QPushButton::clicked, this, &SchedulerPage::generateDialog);
    connect(clear, &QPushButton::clicked, this, [this] { assignSelection(0); });
    connect(grid_, &QTableWidget::cellClicked, this, [this](int row, int col) {
        // Egy cellára kattintva a hozzá tartozó séma jelenik meg a szerkesztőben.
        const int id = db_->weekGrid(editGrid_).value(col * 100 + row, 0);
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

void SchedulerPage::reloadGridList()
{
    const QVector<WeekGrid> grids = db_->weekGrids();
    const int active = db_->activeWeekGrid();
    const int today = db_->gridFor(eter::now().date());
    bool found = false;
    for (const WeekGrid &g : grids) found = found || g.id == editGrid_;
    if (!found) editGrid_ = today;
    QSignalBlocker block(gridSel_);
    gridSel_->clear();
    QString todayName;
    for (const WeekGrid &g : grids) {
        const QString name = Database::gridDisplayName(g);
        QStringList marks;
        if (g.id == active) marks << TR("aktív");
        if (g.id == today) marks << TR("ma érvényes");
        gridSel_->addItem(marks.isEmpty() ? name : QStringLiteral("%1  ·  %2").arg(name, marks.join(QStringLiteral(", "))), g.id);
        gridSel_->setItemData(gridSel_->count() - 1, name, Qt::UserRole + 1);
        if (g.id == today) todayName = name;
    }
    gridSel_->setCurrentIndex(std::max(0, gridSel_->findData(editGrid_)));
    gridDelete_->setEnabled(grids.size() > 1);
    gridActive_->setEnabled(editGrid_ != active);
    gridState_->setText(today == active ? QString() : TR("Most időszak szerint: %1").arg(todayName));
}

void SchedulerPage::editPeriods()
{
    QDialog dlg(this);
    dlg.setWindowTitle(TR("Heti rácsok időszakai"));
    dlg.resize(720, 380);
    auto *v = new QVBoxLayout(&dlg);
    auto *about = new QLabel(TR("Az időszak alatt a megadott rács érvényes; ha egyik időszak sem tart, az aktív rács. Ha két "
                                "időszak fedi egymást, a lista lejjebb lévő tagja számít. „Minden évben”: csak a hónap és a "
                                "nap számít, és átnyúlhat az év végén (például dec. 20 – jan. 6)."));
    about->setWordWrap(true);
    about->setObjectName("small");
    v->addWidget(about);
    auto *table = new QTableWidget(0, 4);
    table->setObjectName("weekGridPeriodTable");
    table->setHorizontalHeaderLabels({TR("Rács"), TR("Kezdete"), TR("Vége"), TR("Minden évben")});
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->setColumnWidth(1, 160);
    table->setColumnWidth(2, 160);
    table->setColumnWidth(3, 110);
    table->verticalHeader()->setDefaultSectionSize(34);
    makeColumnsResizable(table, QStringLiteral("weekGridPeriods"));
    v->addWidget(table, 1);
    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(TR("+ Időszak"));
    auto *del = new QPushButton(TR("Törlés"));
    auto *close = new QPushButton(TR("Bezárás"));
    close->setProperty("accent", true);
    row->addWidget(add);
    row->addWidget(del);
    row->addStretch(1);
    row->addWidget(close);
    v->addLayout(row);
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);

    std::function<void()> fill;
    fill = [this, table, &fill] {
        table->setRowCount(0);
        const QVector<WeekGrid> grids = db_->weekGrids();
        for (const WeekGridPeriod &p : db_->weekGridPeriods()) {
            const int r = table->rowCount();
            table->insertRow(r);
            auto *g = new QComboBox;
            for (const WeekGrid &w : grids) g->addItem(Database::gridDisplayName(w), w.id);
            g->setCurrentIndex(std::max(0, g->findData(p.gridId)));
            auto *from = new QDateEdit(p.from);
            auto *to = new QDateEdit(p.to);
            for (QDateEdit *e : {from, to}) {
                e->setCalendarPopup(true);
                e->setDisplayFormat(p.yearly ? QStringLiteral("MM.dd.") : QStringLiteral("yyyy.MM.dd."));
            }
            auto *yearly = new QCheckBox;
            yearly->setChecked(p.yearly);
            auto *yw = new QWidget;
            auto *yl = new QHBoxLayout(yw);
            yl->setContentsMargins(8, 0, 0, 0);
            yl->addWidget(yearly);
            table->setCellWidget(r, 0, g);
            table->setCellWidget(r, 1, from);
            table->setCellWidget(r, 2, to);
            table->setCellWidget(r, 3, yw);
            auto *idItem = new QTableWidgetItem;
            idItem->setData(Qt::UserRole, p.id);
            table->setItem(r, 0, idItem);
            const int id = p.id;
            auto save = [this, id, g, from, to, yearly] {
                WeekGridPeriod np;
                np.id = id;
                np.gridId = g->currentData().toInt();
                np.from = from->date();
                np.to = to->date();
                np.yearly = yearly->isChecked();
                db_->updateWeekGridPeriod(np);
            };
            connect(g, &QComboBox::activated, table, save);
            connect(from, &QDateEdit::dateChanged, table, save);
            connect(to, &QDateEdit::dateChanged, table, save);
            connect(yearly, &QCheckBox::toggled, table, [save, from, to](bool on) {
                for (QDateEdit *e : {from, to}) e->setDisplayFormat(on ? QStringLiteral("MM.dd.") : QStringLiteral("yyyy.MM.dd."));
                save();
            });
        }
    };
    fill();
    connect(add, &QPushButton::clicked, &dlg, [this, table, &fill] {
        WeekGridPeriod p;
        const QVector<WeekGrid> grids = db_->weekGrids();
        p.gridId = grids.size() > 1 && grids[0].id == db_->activeWeekGrid() ? grids[1].id : grids[0].id;
        p.from = eter::now().date();
        p.to = p.from.addDays(6);
        db_->addWeekGridPeriod(p);
        fill();
        table->selectRow(table->rowCount() - 1);
    });
    connect(del, &QPushButton::clicked, &dlg, [this, table, &fill] {
        const int r = table->currentRow();
        if (r < 0 || !table->item(r, 0)) return;
        db_->deleteWeekGridPeriod(table->item(r, 0)->data(Qt::UserRole).toInt());
        fill();
    });
    if (qEnvironmentVariableIsSet("ETER_PERIODS_SHOT")) {
        // Fejlesztéshez: a párbeszéd képe.
        QTimer::singleShot(400, &dlg, [&dlg] {
            dlg.grab().save(qEnvironmentVariable("ETER_PERIODS_SHOT"));
            dlg.accept();
        });
    }
    dlg.exec();
    reloadGridList();
    reloadGrid();
}

void SchedulerPage::reloadGrid()
{
    const QHash<int, int> grid = db_->weekGrid(editGrid_);
    const bool validToday = editGrid_ == db_->gridFor(eter::now().date());
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
                item->setForeground(theme::c("#e8eaed"));
            } else {
                item->setText({});
                item->setBackground(theme::c("#16181d"));
            }
            QFont f = item->font();
            f.setBold(validToday && d == today && h == now.time().hour());
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
    for (const QTableWidgetItem *item : selected) db_->setGrid(editGrid_, item->column(), item->row(), clockId);
    db_->endBatch();
    reloadGrid();
    emit message(clockId ? TR("%1 óra beállítva.").arg(selected.size())
                         : TR("%1 óra kiürítve.").arg(selected.size()));
}

QString SchedulerPage::generateInfo() const { return genInfo_->text(); }

void SchedulerPage::generateNow()
{
    if (!db_->hasWeekGrid()) {
        emit message(TR("A heti rács üres: előbb rendelj sémát legalább egy órához."));
        return;
    }
    const int hours = std::clamp(db_->setting("generate_ahead_hours", "24").toInt(), 1, 168);
    const QDateTime from = eter::now();
    const GenerateResult r = scheduler_->generate(from, hours, true);
    reportGenerated(r, Scheduler::keyOf(QDateTime(from.date(), QTime(from.time().hour(), 0))));
}

void SchedulerPage::generateDialog()
{
    if (!db_->hasWeekGrid()) {
        emit message(TR("A heti rács üres: előbb rendelj sémát legalább egy órához."));
        return;
    }
    const QDateTime now = eter::now();
    QDialog dlg(this);
    dlg.setObjectName("generateDialog");
    dlg.setWindowTitle(TR("Playlistek generálása"));
    dlg.setMinimumWidth(640);
    auto *v = new QVBoxLayout(&dlg);
    v->setSpacing(10);
    auto *about = new QLabel(TR("Válaszd ki, mely napok mely óráira készüljön playlist. A már elmúlt órák kimaradnak; a "
                                "kézzel betett és az éppen szóló elemek megmaradnak."));
    about->setObjectName("small");
    about->setWordWrap(true);
    v->addWidget(about);

    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    auto *fromDay = new QDateEdit(now.date());
    auto *toDay = new QDateEdit(now.date());
    for (QDateEdit *e : {fromDay, toDay}) {
        e->setCalendarPopup(true);
        e->setDisplayFormat(QStringLiteral("yyyy.MM.dd., dddd"));
        e->setMinimumDate(now.date());
        e->setMaximumDate(now.date().addDays(90));
        e->setMinimumWidth(240);
    }
    fromDay->setObjectName("genFrom");
    toDay->setObjectName("genTo");
    auto *dayRow = new QHBoxLayout;
    dayRow->setSpacing(8);
    dayRow->addWidget(fromDay);
    dayRow->addWidget(new QLabel(QStringLiteral("–")));
    dayRow->addWidget(toDay);
    form->addRow(TR("Napok"), dayRow);

    auto *wdRow = new QHBoxLayout;
    wdRow->setSpacing(10);
    static const char *kShort[] = {"H", "K", "Sze", "Cs", "P", "Szo", "V"};
    QVector<QCheckBox *> weekdays;
    for (int d = 0; d < 7; ++d) {
        auto *c = new QCheckBox(i18n::text(kShort[d]));
        c->setToolTip(i18n::text(kDays[d]));
        c->setChecked(true);
        weekdays << c;
        wdRow->addWidget(c);
    }
    form->addRow(TR("A hét napjai"), wdRow);

    auto *fromHour = new QSpinBox;
    auto *toHour = new QSpinBox;
    fromHour->setObjectName("genFromHour");
    toHour->setObjectName("genToHour");
    for (QSpinBox *sp : {fromHour, toHour}) {
        sp->setRange(0, 23);
        sp->setMinimumWidth(190);
    }
    fromHour->setValue(0);
    toHour->setValue(23);
    fromHour->setSuffix(TR(":00 órától"));
    toHour->setSuffix(TR(":00 órával bezárólag"));
    auto *hourRow = new QHBoxLayout;
    hourRow->addWidget(fromHour);
    hourRow->addWidget(toHour);
    form->addRow(TR("Órák"), hourRow);
    v->addLayout(form);

    auto *presets = new QHBoxLayout;
    presets->setSpacing(6);
    auto preset = [&](const QString &label, const QDate &a, const QDate &b) {
        auto *t = new QPushButton(label);
        presets->addWidget(t);
        connect(t, &QPushButton::clicked, &dlg, [=] {
            fromDay->setDate(a);
            toDay->setDate(b);
            fromHour->setValue(0);
            toHour->setValue(23);
            for (QCheckBox *c : weekdays) c->setChecked(true);
        });
    };
    const QDate today = now.date();
    preset(TR("Ma"), today, today);
    preset(TR("Holnap"), today.addDays(1), today.addDays(1));
    preset(TR("A hét hátralévő része"), today, today.addDays(7 - today.dayOfWeek()));
    preset(TR("Jövő hét"), today.addDays(8 - today.dayOfWeek()), today.addDays(14 - today.dayOfWeek()));
    presets->addStretch(1);
    v->addLayout(presets);

    auto *replace = new QCheckBox(TR("A már elkészült órák újragenerálása"));
    replace->setObjectName("genReplace");
    replace->setChecked(true);
    replace->setToolTip(TR("Kikapcsolva csak a még üres órák készülnek el, a meglévők maradnak."));
    v->addWidget(replace);
    auto *summary = new QLabel;
    summary->setObjectName("genSummary");
    summary->setWordWrap(true);
    v->addWidget(summary);

    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancel = new QPushButton(TR("Mégse"));
    auto *ok = new QPushButton(TR("Generálás"));
    ok->setProperty("accent", true);
    ok->setDefault(true);
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    v->addLayout(buttons);
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);

    // A kiválasztott órák (a már elmúltak nélkül).
    auto selected = [=] {
        QVector<QDateTime> out;
        const QDateTime thisHour(now.date(), QTime(now.time().hour(), 0));
        int a = fromHour->value(), b = toHour->value();
        for (QDate d = fromDay->date(); d <= toDay->date(); d = d.addDays(1)) {
            if (!weekdays[d.dayOfWeek() - 1]->isChecked()) continue;
            // Ha a „-tól” nagyobb, mint a „-ig”, éjfélen átnyúló sáv (például 22–02).
            for (int k = 0; k < 24; ++k) {
                const bool in = a <= b ? (k >= a && k <= b) : (k >= a || k <= b);
                const QDateTime t(d, QTime(k, 0));
                if (in && t >= thisHour) out << t;
            }
        }
        return out;
    };
    auto update = [=] {
        if (toDay->date() < fromDay->date()) toDay->setDate(fromDay->date());
        const QVector<QDateTime> hrs = selected();
        int done = 0, noClock = 0;
        for (const QDateTime &t : hrs) {
            if (!db_->clockAt(t.date(), t.time().hour())) ++noClock;
            else if (db_->playlistGenerated(db_->playlistFor(t.date(), t.time().hour()))) ++done;
        }
        QStringList parts{TR("%1 óra").arg(hrs.size())};
        if (done) parts << (replace->isChecked() ? TR("ebből %1 újragenerálva").arg(done) : TR("ebből %1 már kész, marad").arg(done));
        if (noClock) parts << TR("%1 órához nincs séma a heti rácsban").arg(noClock);
        summary->setText(parts.join(QStringLiteral(" · ")));
        ok->setEnabled(!hrs.isEmpty());
    };
    for (QDateEdit *e : {fromDay, toDay}) connect(e, &QDateEdit::dateChanged, &dlg, update);
    for (QSpinBox *sp : {fromHour, toHour}) connect(sp, &QSpinBox::valueChanged, &dlg, update);
    for (QCheckBox *c : weekdays) connect(c, &QCheckBox::toggled, &dlg, update);
    connect(replace, &QCheckBox::toggled, &dlg, update);
    update();

    if (qEnvironmentVariableIsSet("ETER_GENDLG_SHOT")) {
        // Fejlesztéshez: holnap 6–9 óra, kép a párbeszédről, majd generálás.
        QTimer::singleShot(300, &dlg, [&dlg, fromDay, toDay, fromHour, toHour, today] {
            fromDay->setDate(today.addDays(1));
            toDay->setDate(today.addDays(1));
            fromHour->setValue(6);
            toHour->setValue(9);
            QApplication::processEvents();
            dlg.grab().save(qEnvironmentVariable("ETER_GENDLG_SHOT"));
            dlg.accept();
        });
    }
    if (dlg.exec() != QDialog::Accepted) return;
    const QVector<QDateTime> hrs = selected();
    if (hrs.isEmpty()) return;
    const GenerateResult r = scheduler_->generateHours(hrs, eter::now(), replace->isChecked());
    reportGenerated(r, Scheduler::keyOf(hrs.first()));
}

void SchedulerPage::reportGenerated(const GenerateResult &r, int firstKey)
{
    QStringList parts{TR("%1 órára %2 elem készült").arg(r.hours).arg(r.entries)};
    // A szabályok ellenőrzése a most generált órákon: az egymást követő zenék közti átmenetek.
    QVector<Entry> made;
    for (const Entry &e : db_->pendingEntries())
        if (e.playlistKey >= firstKey) made << e;
    const int bpmPct = db_->setting("rule_bpm_jump_pct", "0").toInt();
    const int keyRule = db_->setting("rule_key", "0").toInt();
    const int vocalMax = db_->setting("rule_vocal_max", "0").toInt();
    const FlowStats st = Scheduler::flowStats(made, bpmPct, keyRule, vocalMax);
    if (bpmPct > 0)
        parts << (st.tempoKnown ? TR("tempó: %1 / %2 átmenet a szabályon belül").arg(st.tempoOk).arg(st.tempoKnown)
                                : TR("tempó: nincs ismert tempójú számpár"));
    if (keyRule > 0)
        parts << (st.keyKnown ? TR("hangnem: %1 / %2 átmenet illik").arg(st.keyOk).arg(st.keyKnown)
                              : TR("hangnem: nincs ismert hangnemű számpár"));
    if (vocalMax > 0)
        parts << (st.vocalKnown ? TR("ének: %1 / %2 szám a szabályon belül").arg(st.vocalOk).arg(st.vocalKnown)
                                : TR("ének: nincs megadott énekű szám"));
    if (r.relaxed) {
        QStringList why;
        if (r.relaxedBy[1]) why << TR("zenei folyam (tempó, hangnem, energia, ének) %1×").arg(r.relaxedBy[1]);
        if (r.relaxedBy[2]) why << TR("energiasáv %1×").arg(r.relaxedBy[2]);
        if (r.relaxedBy[3]) why << TR("ismétlés %1×").arg(r.relaxedBy[3]);
        if (r.relaxedBy[4]) why << TR("napszak %1×").arg(r.relaxedBy[4]);
        parts << TR("lazítás %1 helyen: %2").arg(r.relaxed).arg(why.join(QStringLiteral(", ")));
    } else {
        parts << TR("lazítás nem kellett");
    }
    if (r.empty) parts << TR("%1 hely üresen maradt (üres kategória)").arg(r.empty);
    genInfo_->setText(parts.join(QStringLiteral(" · ")));
    genInfo_->setToolTip(TR("Lazítás: ha egy helyre nem volt minden szabálynak megfelelő elem, az ütemező sorban enged: "
                            "előbb a zenei folyam szabályaiból, aztán az energiasávból, az ismétlésből, végül a napszakból. "
                            "A playlistben a szabályt sértő átmenet sora figyelmeztető jelet kap."));
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
    {
        // A séma saját kitöltési kategóriája az élő igazításhoz (a fix pont előtti hiány kitöltése, csere).
        auto *fillRow = new QFrame;
        fillRow->setObjectName("panelHeader");
        auto *fr = new QHBoxLayout(fillRow);
        fr->setContentsMargins(10, 5, 10, 5);
        fr->setSpacing(6);
        auto *l = new QLabel(TR("Kitöltés a fix pontok előtt ebből"));
        l->setObjectName("small");
        clockFill_ = new QComboBox;
        clockFill_->setObjectName("clockFill");
        l->setBuddy(clockFill_);
        clockFill_->setToolTip(TR("Ebben az órasémában a fix idejű elem előtti automatikus kitöltés és csere ebből a "
                                  "kategóriából választ. „Az általános beállítás szerint”: a Szabályok → „Kitöltés ebből” "
                                  "érvényes. A séma az az óra, amelyben a fix pont előtti zene szól (a 13:00-s hírek előtt "
                                  "a 12 órás séma)."));
        fr->addWidget(l);
        fr->addWidget(clockFill_);
        fr->addStretch(1);
        v->addWidget(fillRow);
        connect(clockFill_, &QComboBox::activated, this, [this] {
            const int id = currentClockId();
            if (!id) return;
            const QString key = QStringLiteral("fix_fill_category_clock_%1").arg(id);
            const QString val = clockFill_->currentData().toString();
            db_->setSetting(key, val);
        });
    }

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
    makeColumnsResizable(slotTable_, QStringLiteral("clockSlots"));
    // A sorok húzással is átrendezhetők; az ejtést az eventFilter kezeli (a sorrend az adatbázisba kerül).
    slotTable_->setDragEnabled(true);
    slotTable_->setAcceptDrops(true);
    slotTable_->viewport()->setAcceptDrops(true);
    slotTable_->setDragDropMode(QAbstractItemView::DragDrop);
    slotTable_->setDefaultDropAction(Qt::MoveAction);
    slotTable_->setDropIndicatorShown(true);
    slotTable_->setDragDropOverwriteMode(false);
    slotTable_->viewport()->installEventFilter(this);
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
    up->setToolTip(TR("Feljebb (a sorok húzással is átrendezhetők)"));
    down->setToolTip(TR("Lejjebb (a sorok húzással is átrendezhetők)"));
    total_ = new QLabel;
    total_->setObjectName("small");
    b->addWidget(addSlot);
    b->addWidget(delSlot);
    b->addWidget(up);
    b->addWidget(down);
    b->addStretch(1);
    b->addWidget(total_);
    v->addWidget(bar);
    // Túlcsordulás: a darabszámos elemek becsült hossza nem fér el két fix pont között.
    auto *warn = new QFrame;
    warn->setObjectName("panelFooter");
    auto *wl = new QHBoxLayout(warn);
    wl->setContentsMargins(10, 6, 10, 6);
    wl->setSpacing(8);
    overflow_ = new QLabel;
    overflow_->setWordWrap(true);
    overflow_->setStyleSheet(QStringLiteral("color: #ff8f85;"));
    overflowFix_ = new QPushButton(TR("Javítás: kitöltés a fix pontig"));
    overflowFix_->setToolTip(TR("A túlcsorduló zenei sorok „Kitöltés a következő fix pontig” típusúak lesznek, a többi "
                                "darabszámos sor pedig „legfeljebb ennyi, amennyi elfér”"));
    wl->addWidget(overflow_, 1);
    wl->addWidget(overflowFix_);
    warn->hide();
    v->addWidget(warn);
    connect(overflowFix_, &QPushButton::clicked, this, &SchedulerPage::fixOverflow);
    overflow_->setProperty("frame", QVariant::fromValue<QObject *>(warn));

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
    slotType_->addItem(TR("Jegyzet (csak felirat)"), "note");
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
    slotUpToFit_ = new QCheckBox(TR("legfeljebb, amennyi elfér"));
    slotUpToFit_->setToolTip(TR("A darabszám csak felső határ: ha a következő fix pontig (vagy az óra végéig) nem fér el "
                                "ennyi, kevesebb kerül be, és az utolsó hossza a hátralévő időhöz igazodik."));
    row1->addWidget(slotUpToFit_);
    row1->addWidget(slotItemLabel_, 1);
    row1->addWidget(slotPick_);
    slotNote_ = new QLineEdit;
    slotNote_->setObjectName("slotNote");
    slotNote_->setPlaceholderText(TR("A playlistben megjelenő felirat, például: Időjárás élőben a stúdióból"));
    slotNote_->setAccessibleName(TR("Jegyzet"));
    slotNote_->setMaxLength(200);
    row1->addWidget(slotNote_, 1);
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
    // Sweeper: a hely első eleme előtti átmenetre egy elem ebből a kategóriából, ráfektetve (a mix pontnál indul).
    auto *row3 = new QHBoxLayout;
    row3->setSpacing(8);
    slotSweeperLabel_ = new QLabel(TR("Sweeper az átmenetre"));
    slotSweeper_ = new QComboBox;
    slotSweeper_->setObjectName("slotSweeper");
    slotSweeper_->setMinimumWidth(150);
    slotSweeper_->setToolTip(TR("Az ide vezető átmenetre (az előző elem és ennek a helynek az első eleme közé) egy elem ebből "
                                "a kategóriából kerül, ráfektetve: a mix pontnál indul, és nem foglal időt."));
    slotSweeperLabel_->setBuddy(slotSweeper_);
    slotSweeperDuckLabel_ = new QLabel(TR("alatta a zene"));
    slotSweeperDuck_ = new QDoubleSpinBox;
    slotSweeperDuck_->setRange(-30, 0);
    slotSweeperDuck_->setDecimals(0);
    slotSweeperDuck_->setSuffix(TR(" dB"));
    slotSweeperDuck_->setToolTip(TR("Ennyivel halkul a zene a sweeper alatt (0: nem halkul)"));
    slotSweeperDuckLabel_->setBuddy(slotSweeperDuck_);
    row3->addWidget(slotSweeperLabel_);
    row3->addWidget(slotSweeper_);
    row3->addWidget(slotSweeperDuckLabel_);
    row3->addWidget(slotSweeperDuck_);
    row3->addStretch(1);
    fl->addLayout(row3);
    slotHint_ = new QLabel;
    slotHint_->setObjectName("small");
    slotHint_->setWordWrap(true);
    fl->addWidget(slotHint_);
    v->addWidget(slotForm_);

    // Szabályok
    auto *rules = new QFrame;
    rules->setObjectName("panelFooter");
    auto *ro = new QVBoxLayout(rules);
    ro->setContentsMargins(10, 6, 10, 6);
    ro->setSpacing(4);
    // Lecsukható: alapból csukva, a fejlécre kattintva nyílik.
    auto *rulesToggle = new QToolButton;
    rulesToggle->setObjectName("rulesToggle");
    rulesToggle->setText(TR("SZABÁLYOK"));
    rulesToggle->setCheckable(true);
    rulesToggle->setChecked(false);
    rulesToggle->setArrowType(Qt::RightArrow);
    rulesToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    rulesToggle->setAutoRaise(true);
    rulesToggle->setToolTip(TR("A szabályok megjelenítése vagy elrejtése"));
    ro->addWidget(rulesToggle);
    auto *rulesBody = new QWidget;
    rulesBody->setObjectName("rulesBody");
    rulesBody->setVisible(false);
    ro->addWidget(rulesBody);
    connect(rulesToggle, &QToolButton::toggled, this, [rulesToggle, rulesBody](bool on) {
        rulesToggle->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        rulesBody->setVisible(on);
    });
    auto *rl = new QVBoxLayout(rulesBody);
    rl->setContentsMargins(0, 4, 0, 4);
    rl->setSpacing(8);
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
    {
        // Előadók szétválasztása: „A and B” két előadó, mindkettőre külön számít az elválasztás; „&” egy előadó marad.
        auto *sepRow = new QHBoxLayout;
        sepRow->setSpacing(8);
        auto *l = new QLabel(TR("Előadók elválasztói"));
        auto *e = new QLineEdit(db_->setting("artist_separators", artists::defaultSeparators()));
        e->setObjectName("artistSeparators");
        e->setPlaceholderText(TR("nincs: az előadó mező egy előadó"));
        e->setToolTip(TR("Szóközzel elválasztva. „A and B” vagy „A feat. B” két előadó, és az „Előadó újra” mindkettőre külön "
                         "érvényes. Ami egy előadó marad, azt írd „&”-tel (például Simon & Garfunkel); a „&” csak akkor "
                         "választ el, ha ide felveszed."));
        l->setBuddy(e);
        auto *reset = new QToolButton;
        reset->setText(TR("Alapérték"));
        sepRow->addWidget(l);
        sepRow->addWidget(e, 1);
        sepRow->addWidget(reset);
        rl->addLayout(sepRow);
        connect(e, &QLineEdit::editingFinished, this, [this, e] {
            db_->setSetting("artist_separators", e->text().simplified());
        });
        connect(reset, &QToolButton::clicked, this, [this, e] {
            e->setText(artists::defaultSeparators());
            db_->setSetting("artist_separators", e->text());
        });
    }
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
    {
        auto *l = new QLabel(TR("Hangnem"));
        auto *keyRule = new QComboBox;
        keyRule->setObjectName("keyRule");
        keyRule->addItem(TR("nincs szabály"), 0);
        keyRule->addItem(TR("előnyben az illő"), 1);
        keyRule->addItem(TR("csak illő"), 2);
        keyRule->setCurrentIndex(std::clamp(db_->setting("rule_key", "0").toInt(), 0, 2));
        keyRule->setToolTip(TR("Harmonikus keverés a Camelot-kerék szerint: a következő szám hangneme az előzővel azonos, "
                               "szomszédos (8A → 7A, 9A) vagy a párhuzamos dúr/moll (8A → 8B) legyen.\n"
                               "Előnyben az illő: ha a soron következő számok között van illő, abból választ.\n"
                               "Csak illő: kötelező, és csak akkor lazít, ha egyáltalán nincs illő jelölt.\n"
                               "Az ismeretlen hangnemű szám mindenhez illik (Beállítások → Elemzés)."));
        l->setBuddy(keyRule);
        rr4->addWidget(l);
        rr4->addWidget(keyRule);
        connect(keyRule, &QComboBox::activated, this, [this, keyRule] { db_->setSetting("rule_key", keyRule->currentData().toString()); });
    }
    rr4->addStretch(1);
    rl->addLayout(rr4);
    auto *rrv = new QHBoxLayout;
    rrv->setSpacing(8);
    {
        auto *l = new QLabel(TR("Azonos ének egymás után"));
        auto *vocalMax = new QComboBox;
        vocalMax->setObjectName("vocalRule");
        vocalMax->addItem(TR("nincs szabály"), 0);
        for (int n = 1; n <= 6; ++n) vocalMax->addItem(TR("legfeljebb %1").arg(n), n);
        vocalMax->setCurrentIndex(std::max(0, vocalMax->findData(db_->setting("rule_vocal_max", "0").toInt())));
        vocalMax->setToolTip(TR("Férfi, női, együttes és vegyes ének egyaránt: legfeljebb ennyi azonos jön egymás után. A nem "
                                "ismert és az instrumentális szám nem számít bele, és nem is szakítja meg a sorozatot. Az ének "
                                "az elemszerkesztőben vagy a hangtár jobb klikkes menüjében állítható."));
        l->setBuddy(vocalMax);
        rrv->addWidget(l);
        rrv->addWidget(vocalMax);
        connect(vocalMax, &QComboBox::activated, this, [this, vocalMax] { db_->setSetting("rule_vocal_max", vocalMax->currentData().toString()); });
    }
    rrv->addStretch(1);
    rl->addLayout(rrv);
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
    fixAdjust_->addItem(TR("automatikus kitöltés, rövidítés és csere"), "both");
    fixAdjust_->setToolTip(TR("Automata üzemmódban a fix idejű elem előtt legfeljebb 30 perccel: ha a playlist nem érne oda "
                              "pontosan (a „Kitöltés pontossága” tűrésén belül), elemet tesz be (kitöltés), vagy az ütemező "
                              "által betett, még el nem indult zenék közül kivesz egyet-kettőt, és jobban illőket tesz a "
                              "helyükre (rövidítés és csere). Kézzel betett elemhez nem nyúl."));
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
    rr3->addSpacing(12);
    auto *tolLabel = new QLabel(TR("Kitöltés pontossága"));
    auto *tol = new QSpinBox;
    tol->setRange(0, 120);
    tol->setPrefix(QStringLiteral("± "));
    tol->setSuffix(TR(" mp"));
    tol->setValue(db_->setting("fill_tolerance_s", "15").toInt());
    tol->setToolTip(TR("Az ütemező a kitöltés utolsó 1–3 elemét együtt választja ki úgy, hogy a fix pontig hátralévő időtől "
                       "legfeljebb ennyivel térjenek el (ha van ilyen összeállítás)."));
    tolLabel->setBuddy(tol);
    rr3->addWidget(tolLabel);
    rr3->addWidget(tol);
    connect(tol, &QSpinBox::valueChanged, this, [this](int v) { db_->setSetting("fill_tolerance_s", QString::number(v)); });
    rr3->addStretch(1);
    rl->addLayout(rr3);
    {
        // Hiány a fix pont előtt: egy dallal kitölti, kis túllógással.
        auto *rr6 = new QHBoxLayout;
        rr6->setSpacing(8);
        auto *gap = new QCheckBox(TR("Hiány esetén egy dallal kitölti"));
        gap->setObjectName("gapFill");
        gap->setChecked(db_->setting("fix_gap_fill", "0") == QLatin1String("1"));
        gap->setToolTip(TR("Automata üzemmódban: ha a fix idejű elem előtt csend lenne, a „Kitöltés ebből” kategóriából "
                           "betesz egy számot, amely kitölti a hiányt. Túllóghat (a beállított értéken belül "
                           "választ); a fix időpontban a szóló szám kiúszik („Fix időpontkor: a szóló "
                           "elemet kiúsztatja”). Ha nincs ilyen szám, a legkisebb túllógásút teszi be, mert az még mindig "
                           "jobb a csendnél. Az „igazítás” beállítástól függetlenül is működik."));
        auto *overLabel = new QLabel(TR("túllóghat legfeljebb"));
        auto *over = new QSpinBox;
        over->setRange(0, 300);
        over->setSuffix(TR(" mp"));
        over->setValue(db_->setting("fix_gap_over_s", "20").toInt());
        overLabel->setBuddy(over);
        over->setEnabled(gap->isChecked());
        rr6->addWidget(gap);
        rr6->addWidget(overLabel);
        rr6->addWidget(over);
        rr6->addStretch(1);
        rl->addLayout(rr6);
        connect(gap, &QCheckBox::toggled, this, [this, over](bool on) {
            db_->setSetting("fix_gap_fill", on ? "1" : "0");
            over->setEnabled(on);
        });
        connect(over, &QSpinBox::valueChanged, this, [this](int v) { db_->setSetting("fix_gap_over_s", QString::number(v)); });
    }
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
    connect(slotUpToFit_, &QCheckBox::toggled, this, &SchedulerPage::applySlot);
    connect(slotEnergyMin_, &QComboBox::activated, this, &SchedulerPage::applySlot);
    connect(slotEnergyMax_, &QComboBox::activated, this, &SchedulerPage::applySlot);
    connect(slotTime_, &QTimeEdit::timeChanged, this, &SchedulerPage::applySlot);
    connect(slotPick_, &QPushButton::clicked, this, &SchedulerPage::pickItem);
    connect(slotNote_, &QLineEdit::editingFinished, this, &SchedulerPage::applySlot);
    connect(slotSweeper_, &QComboBox::activated, this, &SchedulerPage::applySlot);
    connect(slotSweeperDuck_, &QDoubleSpinBox::valueChanged, this, &SchedulerPage::applySlot);
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
    reloadClockFill();
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
        if (s.type == "note") {
            timing = QStringLiteral("—");
            source = s.note.isEmpty() ? TR("(üres jegyzet)") : QStringLiteral("„%1”").arg(s.note);
        } else if (s.type == "item") {
            source = s.label.isEmpty() ? TR("(nincs kiválasztva)") : s.label;
            ms = s.itemId ? db_->item(s.itemId).durationMs : 0;
            estimate = formatMs(ms);
        } else if (s.type == "fill") {
            source = TR("Kitöltés: %1").arg(s.label);
            estimate = TR("változó");
            hasFill = true;
        } else {
            source = s.count > 1 ? (s.upToFit ? TR("legfeljebb %1 × %2").arg(s.count).arg(s.label)
                                              : QStringLiteral("%1 × %2").arg(s.count).arg(s.label))
                                 : s.label;
            ms = categoryAvgMs_.value(s.categoryId, 0) * s.count;
        }
        if (s.sweeperCategoryId > 0) {
            QString sw;
            for (const Category &c : db_->itemCategories())
                if (c.id == s.sweeperCategoryId) sw = c.name;
            if (!sw.isEmpty()) source = TR("↝ sweeper: %1 · ").arg(sw) + source;
        }
        if (s.type != "item" && s.type != "note" && (s.energyMin || s.energyMax)) {
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
            if (s.type == "note" && c >= 2) {
                QFont it = item->font();
                it.setItalic(true);
                item->setFont(it);
                item->setForeground(theme::c("#c9b27a"));
            }
            if (c == 4) item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            slotTable_->setItem(r, c, item);
        }
        if (s.id == selectSlotId) selectRow = r;
    }
    if (selectRow < 0 && !slots_.isEmpty()) selectRow = 0;
    if (selectRow >= 0) slotTable_->setCurrentCell(selectRow, 0);

    // Túlcsordulás: fix pontonként (és az óra végéig) összeadja a darabszámos és konkrét elemek becsült hosszát.
    // Ahol nincs kitöltés vagy „legfeljebb” sor, és a becslés több mint fél perccel hosszabb a rendelkezésre álló
    // időnél, ott a sorok pirosak, és a javítás felajánlható.
    overflowSlots_.clear();
    QStringList problems;
    {
        int segStart = 0;
        int r = 0;
        while (r < slots_.size()) {
            int end = r + 1;
            while (end < slots_.size() && slots_[end].fixedOffsetS < 0) ++end;
            const int fromS = slots_[r].fixedOffsetS >= 0 ? slots_[r].fixedOffsetS : segStart;
            const int toS = end < slots_.size() ? slots_[end].fixedOffsetS : 3600;
            qint64 est = 0;
            bool flexible = false;
            QVector<int> rows;
            for (int k = r; k < end; ++k) {
                const ClockSlot &s = slots_[k];
                if (s.type == "note") continue;
                if (s.type == "fill" || s.upToFit) { flexible = true; continue; }
                if (s.type == "item") est += s.itemId ? db_->item(s.itemId).durationMs : 0;
                else { est += categoryAvgMs_.value(s.categoryId, 0) * s.count; rows << k; }
            }
            const qint64 room = qint64(std::max(0, toS - fromS)) * 1000;
            if (!flexible && !rows.isEmpty() && est > room + 30000) {
                // A zenei sorok a hibásak (a jingle, a reklám marad); ha nincs zenei sor, mind.
                QVector<int> music;
                for (int k : rows)
                    if (slots_[k].kind == QLatin1String("music")) music << k;
                overflowSlots_ << (music.isEmpty() ? rows : music);
                problems << TR("%1–%2: ~%3 a %4 helyett").arg(QStringLiteral(":%1").arg(fromS / 60, 2, 10, QLatin1Char('0')),
                                                               toS >= 3600 ? TR("óra vége") : QStringLiteral(":%1").arg(toS / 60, 2, 10, QLatin1Char('0')),
                                                               formatMs(est), formatMs(room));
            }
            segStart = toS;
            r = end;
        }
    }
    for (int k : overflowSlots_)
        for (int c = 0; c < 5; ++c)
            if (QTableWidgetItem *it = slotTable_->item(k, c)) {
                it->setForeground(theme::c("#ff8f85"));
                it->setToolTip(TR("Ez a sor a becsült hossza alapján nem fér el a következő fix pontig."));
            }
    if (auto *frame = qobject_cast<QWidget *>(overflow_->property("frame").value<QObject *>())) {
        frame->setVisible(!problems.isEmpty());
        overflow_->setText(TR("Túlcsordul: %1. Állítsd a zenei sorokat „Kitöltés a következő fix pontig” típusúra, vagy jelöld be "
                              "a „legfeljebb, amennyi elfér” opciót.").arg(problems.join(QStringLiteral(" · "))));
    }
    total_->setText(slots_.isEmpty() ? TR("Üres séma")
                    : hasFill ? TR("Fix hosszú rész: ~%1 · a többit a kitöltés adja").arg(formatMs(total))
                              : TR("Becsült hossz: ~%1 / 60:00").arg(formatMs(total)));
    loading_ = false;
    showSlot();
}

// A túlcsorduló sorok javítása: a zenei sor kitöltés lesz (fix pontig), a többi darabszámos sor „legfeljebb”.
// Egy szakaszban csak egy zenei sor lesz kitöltés (az utolsó); a többi zenei sor „legfeljebb” marad.
void SchedulerPage::fixOverflow()
{
    if (overflowSlots_.isEmpty()) return;
    QSet<int> filled;                             // szakaszonként már van-e kitöltés: a szakasz első sorának indexe
    int changed = 0;
    for (int i = overflowSlots_.size() - 1; i >= 0; --i) {
        const int k = overflowSlots_[i];
        int segFirst = k;
        while (segFirst > 0 && slots_[segFirst].fixedOffsetS < 0) --segFirst;
        ClockSlot s = slots_[k];
        if (s.kind == QLatin1String("music") && !filled.contains(segFirst) && s.fixedOffsetS < 0) {
            s.type = "fill";
            s.count = 1;
            s.upToFit = false;
            filled.insert(segFirst);
        } else {
            s.upToFit = true;
        }
        db_->updateSlot(s);
        ++changed;
    }
    reloadSlots(slots_.value(currentSlotRow()).id);
    emit message(TR("%1 sor javítva: a zene a következő fix pontig tölt, a többi sor legfeljebb annyi, amennyi elfér.").arg(changed));
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
    slotUpToFit_->setChecked(s.upToFit);
    slotFixed_->setChecked(s.fixedOffsetS >= 0);
    const int off = std::max(0, s.fixedOffsetS);
    slotTime_->setTime(QTime(0, off / 60, off % 60));
    slotItemLabel_->setText(s.label.isEmpty() ? TR("(nincs kiválasztva)") : s.label);
    slotEnergyMin_->setCurrentIndex(std::max(0, slotEnergyMin_->findData(s.energyMin)));
    slotEnergyMax_->setCurrentIndex(std::max(0, slotEnergyMax_->findData(s.energyMax)));
    slotNote_->setText(s.note);
    slotSweeper_->clear();
    slotSweeper_->addItem(TR("nincs"), 0);
    for (const Category &c : db_->itemCategories())
        slotSweeper_->addItem(swatchIcon(QColor(c.color)), QStringLiteral("%1 (%2)").arg(c.name).arg(c.itemCount), c.id);
    slotSweeper_->setCurrentIndex(std::max(0, slotSweeper_->findData(s.sweeperCategoryId)));
    slotSweeperDuck_->setValue(s.sweeperDuckDb);

    const bool isItem = s.type == "item", isFill = s.type == "fill", isNote = s.type == "note";
    slotCategoryLabel_->setVisible(!isItem && !isNote);
    slotCategory_->setVisible(!isItem && !isNote);
    slotCountLabel_->setVisible(!isItem && !isFill && !isNote);
    slotCount_->setVisible(!isItem && !isFill && !isNote);
    slotUpToFit_->setVisible(!isItem && !isFill && !isNote);
    slotItemLabel_->setVisible(isItem);
    slotPick_->setVisible(isItem);
    slotNote_->setVisible(isNote);
    slotFixed_->setVisible(!isFill && !isNote);
    slotTime_->setVisible(!isFill && !isNote);
    for (QWidget *w : std::initializer_list<QWidget *>{slotEnergyLabel_, slotEnergyMin_, slotEnergyDash_, slotEnergyMax_})
        w->setVisible(!isItem && !isNote);
    for (QWidget *w : std::initializer_list<QWidget *>{slotSweeperLabel_, slotSweeper_}) w->setVisible(!isNote);
    for (QWidget *w : std::initializer_list<QWidget *>{slotSweeperDuckLabel_, slotSweeperDuck_})
        w->setVisible(!isNote && s.sweeperCategoryId > 0);
    slotTime_->setEnabled(s.fixedOffsetS >= 0);
    slotHint_->setText(isNote ? TR("Csak felirat: a playlistben a következő elem fölött jelenik meg. Nem szól, és nem foglal időt.")
                       : isFill ? TR("Annyi elemet tesz be ebből a kategóriából, amennyi a következő fix időpontig (vagy az óra végéig) kell.")
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
    s.sweeperCategoryId = s.type == "note" ? 0 : slotSweeper_->currentData().toInt();
    s.sweeperDuckDb = slotSweeperDuck_->value();
    if (s.type == "note") {
        s.note = slotNote_->text().trimmed();
        s.categoryId = 0;
        s.count = 1;
        s.upToFit = false;
        s.energyMin = s.energyMax = 0;
        s.fixedOffsetS = -1;
        db_->updateSlot(s);
        reloadSlots(s.id);
        if (s.note.isEmpty()) slotNote_->setFocus();
        return;
    }
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
    s.upToFit = s.type == "category" && slotUpToFit_->isChecked();
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

void SchedulerPage::moveSlotTo(int from, int to)
{
    if (from < 0 || from >= slots_.size()) return;
    to = std::clamp(to, 0, int(slots_.size()));
    if (to > from) --to;          // a kivett sor után eggyel előrébb csúszik minden
    if (to == from) return;
    QVector<int> ids;
    for (const ClockSlot &s : slots_) ids << s.id;
    ids.move(from, to);
    db_->reorderSlots(currentClockId(), ids);
    reloadSlots(ids[to]);
}

bool SchedulerPage::eventFilter(QObject *o, QEvent *e)
{
    const QEvent::Type t = e->type();
    if (slotTable_ && o == slotTable_->viewport() && (t == QEvent::DragEnter || t == QEvent::DragMove || t == QEvent::Drop)) {
        auto *d = static_cast<QDropEvent *>(e);
        // Csak a saját sorai: más táblából (vagy fájlt) nem fogad.
        const bool ours = d->mimeData() && d->mimeData()->hasFormat(QStringLiteral("application/x-qabstractitemmodeldatalist"))
                          && (d->source() == slotTable_ || !d->source());
        if (!ours) {
            d->ignore();
            return true;
        }
        if (t != QEvent::Drop) return QWidget::eventFilter(o, e);
        const int from = currentSlotRow();
        const QPoint pos = d->position().toPoint();
        const QModelIndex at = slotTable_->indexAt(pos);
        int to = int(slots_.size());
        if (at.isValid()) {
            const QRect r = slotTable_->visualRect(at);
            to = at.row() + (pos.y() > r.center().y() ? 1 : 0);
        }
        // Másolásként nyugtázva, hogy a tábla maga ne töröljön sort; az újratöltés a húzás vége után jön.
        d->setDropAction(Qt::CopyAction);
        d->accept();
        QTimer::singleShot(0, this, [this, from, to] { moveSlotTo(from, to); });
        return true;
    }
    return QWidget::eventFilter(o, e);
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
    makeColumnsResizable(view_, QStringLiteral("schedulerItems"));
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
    fixCategory_->addItem(TR("az óra sémájának kitöltő kategóriája"), QStringLiteral("-1"));
    for (const Category &c : db_->itemCategories()) fixCategory_->addItem(c.name, QString::number(c.id));
    fixCategory_->setCurrentIndex(std::max(0, fixCategory_->findData(current)));
    reloadClockFill();
}

void SchedulerPage::reloadClockFill()
{
    if (!clockFill_) return;
    const int id = currentClockId();
    clockFill_->clear();
    clockFill_->setEnabled(id != 0);
    clockFill_->addItem(TR("az általános beállítás szerint"), QString());
    clockFill_->addItem(TR("az előtte szóló zene kategóriája"), QStringLiteral("0"));
    clockFill_->addItem(TR("a séma kitöltő kategóriája"), QStringLiteral("-1"));
    for (const Category &c : db_->itemCategories()) clockFill_->addItem(c.name, QString::number(c.id));
    const QString cur = id ? db_->setting(QStringLiteral("fix_fill_category_clock_%1").arg(id), QString()) : QString();
    clockFill_->setCurrentIndex(std::max(0, clockFill_->findData(cur)));
}
