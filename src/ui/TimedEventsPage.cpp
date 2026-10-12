#include "TimedEventsPage.h"
#include "i18n.h"
#include "Library.h"
#include "SchedulerPage.h"
#include "Theme.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTimeEdit>
#include <QVBoxLayout>

static const char *kDayShort[7] = {"H", "K", "Sze", "Cs", "P", "Szo", "V"};

static QFrame *panel()
{
    auto *f = new QFrame;
    f->setObjectName("panel");
    return f;
}

QString TimedEventsPage::actionText(const QString &action)
{
    if (action == QLatin1String("start")) return TR("Azonnal indul");
    if (action == QLatin1String("end")) return TR("A playlist végére");
    return TR("Következőként");
}

QString TimedEventsPage::daysText(const TimedEvent &e)
{
    if (e.date.isValid()) return i18n::locale().toString(e.date, TR("yyyy. MMM d."));
    if ((e.weekdays & 0x7f) == 0x7f) return TR("minden nap");
    if ((e.weekdays & 0x7f) == 0x1f) return TR("hétköznap");
    if ((e.weekdays & 0x7f) == 0x60) return TR("hétvégén");
    QStringList parts;
    for (int d = 0; d < 7; ++d)
        if (e.weekdays & (1 << d)) parts << i18n::text(kDayShort[d]);
    return parts.isEmpty() ? TR("(nincs nap kijelölve)") : parts.join(QStringLiteral(", "));
}

QDateTime TimedEventsPage::nextOccurrence(const TimedEvent &e, const QDateTime &after)
{
    const QTime t = QTime(0, 0).addSecs(e.timeS);
    if (e.date.isValid()) {
        const QDateTime at(e.date, t);
        return at > after ? at : QDateTime();
    }
    for (int i = 0; i < 8; ++i) {
        const QDate d = after.date().addDays(i);
        if (!(e.weekdays & (1 << (d.dayOfWeek() - 1)))) continue;
        const QDateTime at(d, t);
        if (at > after) return at;
    }
    return {};
}

TimedEventsPage::TimedEventsPage(Database *db, QWidget *parent) : QWidget(parent), db_(db)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setHandleWidth(8);
    split->setChildrenCollapsible(false);
    root->addWidget(split);

    // ---- bal oldal: az események listája
    auto *left = panel();
    auto *lv = new QVBoxLayout(left);
    lv->setContentsMargins(1, 1, 1, 1);
    lv->setSpacing(0);
    auto *head = new QFrame;
    head->setObjectName("panelHeader");
    auto *hh = new QHBoxLayout(head);
    hh->setContentsMargins(10, 8, 10, 8);
    auto *title = new QLabel(TR("Időzítések"));
    title->setObjectName("panelTitle");
    auto *sub = new QLabel(TR("Elem vagy playlist a megadott időben a playlistbe vagy adásba"));
    sub->setObjectName("small");
    new_ = new QPushButton(TR("Új időzítés"));
    hh->addWidget(title);
    hh->addSpacing(10);
    hh->addWidget(sub);
    hh->addStretch(1);
    hh->addWidget(new_);
    lv->addWidget(head);

    table_ = new QTableWidget;
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels({TR("Aktív"), TR("Időpont"), TR("Mikor"), TR("Mit"), TR("Művelet"), TR("Következő")});
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(30);
    table_->setShowGrid(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setFrameShape(QFrame::NoFrame);
    auto *h = table_->horizontalHeader();
    h->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    h->setHighlightSections(false);
    for (int c : {0, 1, 4, 5}) h->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    h->setSectionResizeMode(2, QHeaderView::Interactive);
    h->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->setColumnWidth(2, 150);
    lv->addWidget(table_, 1);
    hint_ = new QLabel(TR("Még nincs időzítés. Az „Új időzítés” gombbal hozz létre egyet, vagy a hangtárban "
                          "egy elemen jobb gombbal válaszd az „Időzítés…” pontot."));
    hint_->setObjectName("small");
    hint_->setWordWrap(true);
    hint_->setAlignment(Qt::AlignCenter);
    hint_->setContentsMargins(30, 30, 30, 30);
    lv->addWidget(hint_, 1);
    split->addWidget(left);

    // ---- jobb oldal: a kijelölt esemény
    auto *right = panel();
    right->setMinimumWidth(380);
    right->setMaximumWidth(460);
    auto *rv = new QVBoxLayout(right);
    rv->setContentsMargins(16, 14, 16, 14);
    rv->setSpacing(10);
    formTitle_ = new QLabel;
    formTitle_->setObjectName("caption");
    rv->addWidget(formTitle_);

    auto *what = new QLabel(TR("MIT"));
    what->setObjectName("caption");
    rv->addWidget(what);
    srcItem_ = new QRadioButton(TR("Elem a hangtárból"));
    srcPlaylist_ = new QRadioButton(TR("Mentett playlist"));
    auto *srcGroup = new QButtonGroup(this);
    srcGroup->addButton(srcItem_);
    srcGroup->addButton(srcPlaylist_);
    auto *itemRow = new QHBoxLayout;
    sourceLabel_ = new QLabel;
    sourceLabel_->setWordWrap(true);
    chooseItem_ = new QPushButton(TR("Választás…"));
    itemRow->addWidget(srcItem_);
    itemRow->addWidget(sourceLabel_, 1);
    itemRow->addWidget(chooseItem_);
    rv->addLayout(itemRow);
    auto *plRow = new QHBoxLayout;
    playlist_ = new QComboBox;
    playlist_->setIconSize(QSize(12, 12));
    playlist_->setAccessibleName(TR("Mentett playlist"));
    plRow->addWidget(srcPlaylist_);
    plRow->addWidget(playlist_, 1);
    rv->addLayout(plRow);

    auto *when = new QLabel(TR("MIKOR"));
    when->setObjectName("caption");
    rv->addSpacing(6);
    rv->addWidget(when);
    auto *form = new QFormLayout;
    form->setHorizontalSpacing(10);
    form->setVerticalSpacing(8);
    time_ = new QTimeEdit;
    time_->setDisplayFormat("HH:mm:ss");
    time_->setAccessibleName(TR("Időpont"));
    form->addRow(TR("Időpont"), time_);
    rv->addLayout(form);
    once_ = new QRadioButton(TR("Egyszer, ezen a napon:"));
    weekly_ = new QRadioButton(TR("Hetente, ezeken a napokon:"));
    auto *repGroup = new QButtonGroup(this);
    repGroup->addButton(once_);
    repGroup->addButton(weekly_);
    date_ = new QDateEdit;
    date_->setCalendarPopup(true);
    date_->setDisplayFormat(TR("yyyy. MM. dd."));
    auto *onceRow = new QHBoxLayout;
    onceRow->addWidget(once_);
    onceRow->addWidget(date_, 1);
    rv->addLayout(onceRow);
    rv->addWidget(weekly_);
    auto *dayRow = new QHBoxLayout;
    dayRow->setContentsMargins(22, 0, 0, 0);
    for (int d = 0; d < 7; ++d) {
        days_[d] = new QCheckBox(i18n::text(kDayShort[d]));
        dayRow->addWidget(days_[d]);
    }
    dayRow->addStretch(1);
    rv->addLayout(dayRow);

    auto *how = new QLabel(TR("MŰVELET"));
    how->setObjectName("caption");
    rv->addSpacing(6);
    rv->addWidget(how);
    action_ = new QComboBox;
    action_->addItem(actionText("next"), QStringLiteral("next"));
    action_->addItem(actionText("end"), QStringLiteral("end"));
    action_->addItem(actionText("start"), QStringLiteral("start"));
    action_->setItemData(0, TR("A szóló elem után következik; a többi várakozó elem utána jön"), Qt::ToolTipRole);
    action_->setItemData(1, TR("A kézzel betett elemek után, a playlist végére kerül"), Qt::ToolTipRole);
    action_->setItemData(2, TR("Az időpontban elindul, a szóló elemet kiúsztatja – asszisztált módban is"), Qt::ToolTipRole);
    rv->addWidget(action_);
    enabled_ = new QCheckBox(TR("Aktív"));
    rv->addWidget(enabled_);
    nextRun_ = new QLabel;
    nextRun_->setObjectName("small");
    nextRun_->setWordWrap(true);
    rv->addWidget(nextRun_);
    auto *note = new QLabel(TR("Az időzítés csak akkor fut le, ha a program az adott időpontban fut. "
                               "Ha a program akkor nem futott, az alkalom kimarad."));
    note->setObjectName("small");
    note->setWordWrap(true);
    rv->addWidget(note);
    rv->addStretch(1);
    auto *buttons = new QHBoxLayout;
    delete_ = new QPushButton(TR("Törlés"));
    save_ = new QPushButton(TR("Mentés"));
    save_->setProperty("accent", true);
    buttons->addWidget(delete_);
    buttons->addStretch(1);
    buttons->addWidget(save_);
    rv->addLayout(buttons);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);

    connect(new_, &QPushButton::clicked, this, [this] { table_->clearSelection(); clearForm(); });
    connect(table_, &QTableWidget::itemSelectionChanged, this, &TimedEventsPage::showCurrent);
    connect(table_, &QTableWidget::cellClicked, this, [this](int r, int c) {
        // Az „Aktív” oszlopra kattintva be- és kikapcsol.
        if (c != 0 || r < 0 || r >= events_.size()) return;
        TimedEvent e = events_[r];
        e.enabled = !e.enabled;
        db_->saveTimedEvent(e);
    });
    connect(chooseItem_, &QPushButton::clicked, this, [this] {
        if (picker_) picker_->close();
        auto *dlg = new ItemPickerDialog(db_, window(), QStringLiteral("picker_category_event"));
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        picker_ = dlg;
        connect(dlg, &ItemPickerDialog::picked, this, [this](int id) {
            const Item it = db_->item(id);
            itemId_ = it.id;
            itemTitle_ = it.artist.isEmpty() ? it.title : QStringLiteral("%1 – %2").arg(it.artist, it.title);
            srcItem_->setChecked(true);
            updateSource();
            markDirty();
        });
        dlg->setModal(false);
        dlg->show();
    });
    for (QRadioButton *b : {srcItem_, srcPlaylist_, once_, weekly_})
        connect(b, &QRadioButton::toggled, this, [this] { updateSource(); markDirty(); });
    connect(playlist_, &QComboBox::activated, this, [this] { srcPlaylist_->setChecked(true); markDirty(); });
    connect(time_, &QTimeEdit::timeChanged, this, &TimedEventsPage::markDirty);
    connect(date_, &QDateEdit::dateChanged, this, [this] { once_->setChecked(true); markDirty(); });
    for (QCheckBox *c : days_)
        connect(c, &QCheckBox::toggled, this, [this](bool on) { if (on) weekly_->setChecked(true); markDirty(); });
    connect(action_, &QComboBox::activated, this, &TimedEventsPage::markDirty);
    connect(enabled_, &QCheckBox::toggled, this, &TimedEventsPage::markDirty);
    connect(save_, &QPushButton::clicked, this, &TimedEventsPage::save);
    connect(delete_, &QPushButton::clicked, this, &TimedEventsPage::remove);
    connect(db_, &Database::timedEventsChanged, this, [this] { reload(editingId_); });
    connect(db_, &Database::savedPlaylistsChanged, this, [this] { reloadPlaylists(); reload(editingId_); });
    connect(db_, &Database::itemsChanged, this, [this] { reload(editingId_); });

    reloadPlaylists();
    reload();
    clearForm();
}

void TimedEventsPage::reloadPlaylists()
{
    const int keep = playlist_->currentData().toInt();
    playlist_->clear();
    for (const SavedPlaylist &p : db_->savedPlaylists())
        playlist_->addItem(swatchIcon(QColor(p.color)), QStringLiteral("%1  (%2 · %3)").arg(p.name, p.categoryName).arg(formatMs(p.durationMs)), p.id);
    if (playlist_->count() == 0) playlist_->addItem(TR("Még nincs mentett playlist"), 0);
    playlist_->setCurrentIndex(std::max(0, playlist_->findData(keep)));
}

void TimedEventsPage::reload(int selectId)
{
    events_ = db_->timedEvents();
    const QDateTime now = eter::now();
    table_->blockSignals(true);
    table_->setRowCount(events_.size());
    QFont mono("IBM Plex Mono");
    mono.setPixelSize(12);
    int select = -1;
    for (int r = 0; r < events_.size(); ++r) {
        const TimedEvent &e = events_[r];
        const QDateTime next = e.enabled ? nextOccurrence(e, now) : QDateTime();
        const QString what = (e.playlistId ? TR("Playlist: ") : QString()) + e.label;
        const QString cells[6] = {e.enabled ? QStringLiteral("●") : QStringLiteral("○"),
                                  QTime(0, 0).addSecs(e.timeS).toString("HH:mm:ss"), daysText(e), what, actionText(e.action),
                                  next.isValid() ? i18n::locale().toString(next, TR("MMM d. HH:mm")) : QStringLiteral("–")};
        for (int c = 0; c < 6; ++c) {
            auto *item = table_->item(r, c);
            if (!item) {
                item = new QTableWidgetItem;
                table_->setItem(r, c, item);
            }
            item->setText(cells[c]);
            item->setForeground(e.enabled ? (c == 0 ? theme::green : theme::text) : QColor("#6b7380"));
            if (c == 1 || c == 5) item->setFont(mono);
            if (c == 0) {
                item->setTextAlignment(Qt::AlignCenter);
                item->setToolTip(e.enabled ? TR("Aktív – kattints a kikapcsoláshoz") : TR("Kikapcsolva – kattints a bekapcsoláshoz"));
            }
        }
        if (e.id == selectId) select = r;
    }
    table_->blockSignals(false);
    table_->setVisible(!events_.isEmpty());
    hint_->setVisible(events_.isEmpty());
    if (select >= 0) {
        table_->selectRow(select);
    } else if (selectId) {
        clearForm();
    }
}

void TimedEventsPage::clearForm()
{
    loading_ = true;
    editingId_ = 0;
    itemId_ = 0;
    itemTitle_.clear();
    formTitle_->setText(TR("ÚJ IDŐZÍTÉS"));
    srcItem_->setChecked(true);
    const QDateTime soon = eter::now().addSecs(3600);
    time_->setTime(QTime(soon.time().hour(), 0));
    date_->setDate(soon.date());
    once_->setChecked(true);
    for (QCheckBox *c : days_) c->setChecked(false);
    action_->setCurrentIndex(0);
    enabled_->setChecked(true);
    delete_->setEnabled(false);
    loading_ = false;
    updateSource();
    save_->setEnabled(false);
    nextRun_->clear();
}

void TimedEventsPage::newForItem(int itemId)
{
    table_->clearSelection();
    clearForm();
    const Item it = db_->item(itemId);
    if (!it.id) return;
    itemId_ = it.id;
    itemTitle_ = it.artist.isEmpty() ? it.title : QStringLiteral("%1 – %2").arg(it.artist, it.title);
    updateSource();
    markDirty();
    time_->setFocus();
}

void TimedEventsPage::showCurrent()
{
    const int r = table_->currentRow();
    if (r < 0 || r >= events_.size() || table_->selectedItems().isEmpty()) return;
    const TimedEvent &e = events_[r];
    loading_ = true;
    editingId_ = e.id;
    formTitle_->setText(TR("A KIJELÖLT IDŐZÍTÉS"));
    itemId_ = e.itemId;
    itemTitle_.clear();
    if (e.itemId) {
        const Item it = db_->item(e.itemId);
        itemTitle_ = it.artist.isEmpty() ? it.title : QStringLiteral("%1 – %2").arg(it.artist, it.title);
        srcItem_->setChecked(true);
    } else {
        srcPlaylist_->setChecked(true);
        playlist_->setCurrentIndex(std::max(0, playlist_->findData(e.playlistId)));
    }
    time_->setTime(QTime(0, 0).addSecs(e.timeS));
    if (e.date.isValid()) {
        once_->setChecked(true);
        date_->setDate(e.date);
    } else {
        weekly_->setChecked(true);
    }
    for (int d = 0; d < 7; ++d) days_[d]->setChecked(e.weekdays & (1 << d));
    action_->setCurrentIndex(std::max(0, action_->findData(e.action)));
    enabled_->setChecked(e.enabled);
    delete_->setEnabled(true);
    loading_ = false;
    updateSource();
    save_->setEnabled(false);
    const QDateTime next = e.enabled ? nextOccurrence(e, eter::now()) : QDateTime();
    nextRun_->setText(next.isValid() ? TR("Legközelebb: %1").arg(i18n::locale().toString(next, TR("yyyy. MMM d., dddd HH:mm:ss")))
                                     : (e.enabled ? TR("Nincs több alkalom.") : TR("Kikapcsolva.")));
}

void TimedEventsPage::updateSource()
{
    sourceLabel_->setText(itemId_ ? itemTitle_ : TR("(nincs kiválasztva)"));
    sourceLabel_->setEnabled(srcItem_->isChecked());
    playlist_->setEnabled(srcPlaylist_->isChecked());
    date_->setEnabled(once_->isChecked());
    for (QCheckBox *c : days_) c->setEnabled(weekly_->isChecked());
}

void TimedEventsPage::markDirty()
{
    if (loading_) return;
    save_->setEnabled(true);
    nextRun_->setText(TR("Nem mentett módosítás."));
}

void TimedEventsPage::save()
{
    TimedEvent e;
    for (const TimedEvent &old : events_)
        if (old.id == editingId_) e = old;
    e.id = editingId_;
    if (srcItem_->isChecked()) {
        if (!itemId_) {
            emit message(TR("Válassz elemet a hangtárból a „Választás…” gombbal."));
            return;
        }
        e.itemId = itemId_;
        e.playlistId = 0;
    } else {
        if (!playlist_->currentData().toInt()) {
            emit message(TR("Nincs kiválasztható mentett playlist; a Playlist szerkesztőben készíthetsz egyet."));
            return;
        }
        e.itemId = 0;
        e.playlistId = playlist_->currentData().toInt();
    }
    const QTime t = time_->time();
    e.timeS = t.hour() * 3600 + t.minute() * 60 + t.second();
    if (once_->isChecked()) {
        e.date = date_->date();
        e.weekdays = 0;
    } else {
        e.date = QDate();
        e.weekdays = 0;
        for (int d = 0; d < 7; ++d)
            if (days_[d]->isChecked()) e.weekdays |= 1 << d;
        if (!e.weekdays) {
            emit message(TR("Jelölj ki legalább egy napot."));
            return;
        }
    }
    e.action = action_->currentData().toString();
    e.enabled = enabled_->isChecked();
    const QDateTime now = eter::now();
    if (once_->isChecked() && QDateTime(e.date, t) <= now) {
        emit message(TR("Ez az időpont már elmúlt; adj meg egy későbbit."));
        return;
    }
    // Ha az időpont módosult, a mostani napon már lefutott alkalom ne akadályozza az újat.
    if (e.lastFiredAt >= now.toSecsSinceEpoch()) e.lastFiredAt = 0;
    const int id = db_->saveTimedEvent(e);
    if (!id) return;
    editingId_ = id;
    reload(id);
    const QDateTime next = e.enabled ? nextOccurrence(e, now) : QDateTime();
    emit message(next.isValid() ? TR("Időzítés elmentve. Legközelebb: %1").arg(i18n::locale().toString(next, TR("yyyy. MMM d., dddd HH:mm:ss")))
                                : TR("Időzítés elmentve."));
}

void TimedEventsPage::remove()
{
    if (!editingId_) return;
    if (QMessageBox::question(this, TR("Időzítés törlése"), TR("Törlöd a kijelölt időzítést?"),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    db_->deleteTimedEvent(editingId_);
    editingId_ = 0;
    clearForm();
}
