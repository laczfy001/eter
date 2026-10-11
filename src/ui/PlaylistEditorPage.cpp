#include "i18n.h"
#include "PlaylistEditorPage.h"
#include "Library.h"
#include "PlaylistTable.h"
#include "Theme.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

static QFrame *panel()
{
    auto *f = new QFrame;
    f->setObjectName("panel");
    return f;
}

static QFrame *bar(const char *name)
{
    auto *f = new QFrame;
    f->setObjectName(name);
    return f;
}

PlaylistEditorPage::PlaylistEditorPage(Database *db, QWidget *parent) : QWidget(parent), db_(db)
{
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto *split = new QSplitter(Qt::Horizontal);
    split->setHandleWidth(8);
    split->setChildrenCollapsible(false);
    root->addWidget(split);

    // ---- bal oldal: a mentett playlistek
    auto *left = panel();
    auto *lv = new QVBoxLayout(left);
    lv->setContentsMargins(1, 1, 1, 1);
    lv->setSpacing(0);
    auto *lhead = bar("panelHeader");
    auto *lh = new QHBoxLayout(lhead);
    lh->setContentsMargins(10, 8, 10, 8);
    auto *ltitle = new QLabel(TR("Mentett playlistek"));
    ltitle->setObjectName("panelTitle");
    auto *newBtn = new QPushButton(TR("Új"));
    newBtn->setToolTip(TR("Új, üres playlist kezdése"));
    lh->addWidget(ltitle);
    lh->addStretch(1);
    lh->addWidget(newBtn);
    lv->addWidget(lhead);
    auto *lbody = new QVBoxLayout;
    lbody->setContentsMargins(10, 10, 10, 10);
    lbody->setSpacing(8);
    filter_ = new QComboBox;
    filter_->setIconSize(QSize(12, 12));
    filter_->setAccessibleName(TR("Playlist kategória szűrő"));
    saved_ = new QListWidget;
    saved_->setIconSize(QSize(12, 12));
    lbody->addWidget(filter_);
    lbody->addWidget(saved_, 1);
    lv->addLayout(lbody, 1);
    auto *lfoot = bar("panelFooter");
    auto *lf = new QHBoxLayout(lfoot);
    lf->setContentsMargins(10, 7, 10, 7);
    delete_ = new QPushButton(TR("Playlist törlése"));
    delete_->setToolTip(TR("A kijelölt mentett playlist törlése (a hangtár elemei megmaradnak)"));
    lf->addWidget(delete_);
    lf->addStretch(1);
    lv->addWidget(lfoot);
    split->addWidget(left);

    // ---- közép: a szerkesztett playlist
    auto *mid = panel();
    auto *mv = new QVBoxLayout(mid);
    mv->setContentsMargins(1, 1, 1, 1);
    mv->setSpacing(0);
    auto *mhead = bar("panelHeader");
    auto *mh = new QHBoxLayout(mhead);
    mh->setContentsMargins(10, 8, 10, 8);
    mh->setSpacing(8);
    title_ = new QLabel;
    title_->setObjectName("panelTitle");
    name_ = new QLineEdit;
    name_->setPlaceholderText(TR("A playlist neve…"));
    name_->setMaxLength(80);
    name_->setAccessibleName(TR("A playlist neve"));
    category_ = new QComboBox;
    category_->setMinimumWidth(170);
    category_->setIconSize(QSize(12, 12));
    category_->setAccessibleName(TR("Playlist kategória"));
    category_->setToolTip(TR("Ebbe a playlist típusú kategóriába kerül mentéskor"));
    auto *cats = new QPushButton(TR("Kategóriák…"));
    cats->setToolTip(TR("Kategóriák kezelése; a playlistekhez „Playlist” típusú kategória kell"));
    mh->addWidget(title_);
    mh->addWidget(name_, 1);
    mh->addWidget(category_);
    mh->addWidget(cats);
    mv->addWidget(mhead);

    table_ = new PlaylistTable;
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels({QStringLiteral("#"), TR("Cím"), TR("Előadó"),
                                       TR("Kategória"), TR("Hossz")});
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(28);
    table_->setShowGrid(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setFrameShape(QFrame::NoFrame);
    table_->setIconSize(QSize(12, 12));
    auto *hh = table_->horizontalHeader();
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hh->setHighlightSections(false);
    for (int c : {0, 3, 4}) hh->setSectionResizeMode(c, QHeaderView::Fixed);
    hh->setSectionResizeMode(1, QHeaderView::Stretch);
    hh->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->setColumnWidth(0, 44);
    table_->setColumnWidth(3, 140);
    table_->setColumnWidth(4, 64);
    hint_ = new QLabel(TR("Üres playlist.\n\nHúzd ide az elemeket a jobb oldali hangtárból,\nvagy kattints rájuk duplán."));
    hint_->setObjectName("small");
    hint_->setAlignment(Qt::AlignCenter);
    mv->addWidget(table_, 1);
    mv->addWidget(hint_, 1);

    auto *mfoot = bar("panelFooter");
    auto *mf = new QHBoxLayout(mfoot);
    mf->setContentsMargins(10, 7, 10, 7);
    mf->setSpacing(6);
    info_ = new QLabel;
    info_->setObjectName("small");
    remove_ = new QPushButton(TR("Eltávolítás"));
    remove_->setToolTip(TR("A kijelölt sor eltávolítása a playlistből (Delete)"));
    insert_ = new QPushButton(TR("Beillesztés a fő playlistbe"));
    insert_->setToolTip(TR("A playlist blokként a fő playlist végére kerül (a nem mentett módosításokat előbb elmenti)"));
    save_ = new QPushButton(TR("Mentés"));
    save_->setProperty("accent", true);
    mf->addWidget(info_);
    mf->addStretch(1);
    mf->addWidget(remove_);
    mf->addWidget(insert_);
    mf->addWidget(save_);
    mv->addWidget(mfoot);
    split->addWidget(mid);

    // ---- jobb oldal: a hangtár
    library_ = new LibraryPanel(db_);
    split->addWidget(library_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 10);
    split->setStretchFactor(2, 10);
    split->setSizes({250, 600, 570});

    connect(newBtn, &QPushButton::clicked, this, [this] {
        if (!confirmDiscard()) return;
        newPlaylist();
    });
    connect(delete_, &QPushButton::clicked, this, &PlaylistEditorPage::deleteCurrent);
    connect(cats, &QPushButton::clicked, this, [this] { library_->openCategories(); });
    connect(filter_, &QComboBox::activated, this, [this] { reloadSaved(); });
    connect(saved_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        const int id = item->data(Qt::UserRole).toInt();
        if (id == id_) return;
        if (!confirmDiscard()) {
            reloadSaved();
            return;
        }
        open(id);
    });
    connect(name_, &QLineEdit::textEdited, this, [this] { dirty_ = true; updateState(); });
    connect(category_, &QComboBox::activated, this, [this] { dirty_ = true; updateState(); });
    connect(save_, &QPushButton::clicked, this, [this] { save(); });
    connect(insert_, &QPushButton::clicked, this, [this] {
        if ((dirty_ || !id_) && !save()) return;
        emit insertRequested(id_);
    });
    connect(remove_, &QPushButton::clicked, this, &PlaylistEditorPage::removeSelected);
    connect(table_, &PlaylistTable::deletePressed, this, &PlaylistEditorPage::removeSelected);
    connect(table_, &PlaylistTable::rowMoved, this, &PlaylistEditorPage::moveRow);
    connect(table_, &PlaylistTable::itemsDropped, this, [this](const QVector<int> &ids, int at) { addItems(ids, at); });
    connect(table_, &PlaylistTable::pathsDropped, this, [this](const QStringList &paths, int at) {
        // A fájlkezelőből húzott fájlok előbb a hangtárba kerülnek (kategóriával), utána ide.
        library_->importPaths(paths, [this, at](const QVector<int> &ids) { addItems(ids, at); });
    });
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { updateState(); });
    connect(library_, &LibraryPanel::addToPlaylist, this, [this](const QVector<int> &ids) { addItems(ids); });
    connect(library_, &LibraryPanel::message, this, &PlaylistEditorPage::message);
    connect(library_, &LibraryPanel::editRequested, this, &PlaylistEditorPage::editRequested);
    connect(db_, &Database::categoriesChanged, this, [this] { reloadCategories(); reloadSaved(); });
    connect(db_, &Database::savedPlaylistsChanged, this, [this] { reloadSaved(); });
    connect(db_, &Database::itemsChanged, this, [this] {
        // Törölt vagy átnevezett hangtárelemek követése.
        QVector<Item> fresh;
        for (const Item &it : items_) {
            const Item now = db_->item(it.id);
            if (now.id) fresh.push_back(now);
        }
        items_ = fresh;
        refreshTable();
    });

    reloadCategories();
    reloadSaved();
    refreshTable();
}

void PlaylistEditorPage::reloadCategories()
{
    const QVector<Category> cats = db_->playlistCategories();
    const int keepFilter = filter_->currentData().toInt();
    filter_->clear();
    filter_->addItem(TR("Minden playlist kategória"), 0);
    for (const Category &c : cats) filter_->addItem(swatchIcon(QColor(c.color)), c.name, c.id);
    filter_->setCurrentIndex(std::max(0, filter_->findData(keepFilter)));

    const int keep = category_->currentData().toInt();
    category_->clear();
    if (cats.isEmpty()) category_->addItem(TR("Nincs playlist kategória"), 0);
    for (const Category &c : cats) category_->addItem(swatchIcon(QColor(c.color)), c.name, c.id);
    int index = category_->findData(keep);
    if (index < 0) index = category_->findData(db_->setting("playlist_editor_category", "0").toInt());
    category_->setCurrentIndex(std::max(0, index));
    updateState();
}

void PlaylistEditorPage::reloadSaved()
{
    const int only = filter_->currentData().toInt();
    saved_->blockSignals(true);
    saved_->clear();
    int row = -1;
    for (const SavedPlaylist &p : db_->savedPlaylists()) {
        if (only && p.categoryId != only) continue;
        auto *item = new QListWidgetItem(swatchIcon(QColor(p.color)),
                                         TR("%1\n%2 · %3 elem · %4").arg(p.name, p.categoryName).arg(p.count).arg(formatMs(p.durationMs)));
        item->setData(Qt::UserRole, p.id);
        saved_->addItem(item);
        if (p.id == id_) row = saved_->count() - 1;
    }
    saved_->setCurrentRow(row);
    saved_->blockSignals(false);
    updateState();
}

void PlaylistEditorPage::newPlaylist()
{
    id_ = 0;
    items_.clear();
    name_->clear();
    dirty_ = false;
    refreshTable();
    reloadSaved();
    name_->setFocus();
}

void PlaylistEditorPage::open(int id)
{
    for (const SavedPlaylist &p : db_->savedPlaylists()) {
        if (p.id != id) continue;
        id_ = id;
        items_ = db_->savedPlaylistItems(id);
        name_->setText(p.name);
        category_->setCurrentIndex(std::max(0, category_->findData(p.categoryId)));
        dirty_ = false;
        refreshTable();
        reloadSaved();
        return;
    }
}

void PlaylistEditorPage::setName(const QString &name)
{
    name_->setText(name);
    dirty_ = true;
    updateState();
}

void PlaylistEditorPage::addItems(const QVector<int> &itemIds, int at)
{
    int index = at < 0 ? items_.size() : std::clamp(at, 0, int(items_.size()));
    int added = 0;
    for (int itemId : itemIds) {
        const Item it = db_->item(itemId);
        if (!it.id) continue;
        items_.insert(index++, it);
        ++added;
    }
    if (!added) return;
    dirty_ = true;
    refreshTable();
    table_->setCurrentCell(index - 1, 0);
}

void PlaylistEditorPage::removeSelected()
{
    const int r = table_->currentRow();
    if (r < 0 || r >= items_.size()) return;
    items_.remove(r);
    dirty_ = true;
    refreshTable();
    if (!items_.isEmpty()) table_->setCurrentCell(std::min(r, int(items_.size()) - 1), 0);
}

void PlaylistEditorPage::moveRow(int from, int to)
{
    if (from < 0 || from >= items_.size()) return;
    to = std::clamp(to, 0, int(items_.size()));
    if (to == from || to == from + 1) return;
    const Item it = items_[from];
    items_.remove(from);
    if (to > from) --to;
    items_.insert(to, it);
    dirty_ = true;
    refreshTable();
    table_->setCurrentCell(to, 0);
}

bool PlaylistEditorPage::save()
{
    const QString name = name_->text().trimmed();
    const int category = category_->currentData().toInt();
    if (!category) {
        emit message(TR("A mentéshez előbb hozz létre egy „Playlist” típusú kategóriát a „Kategóriák…” gombbal."));
        return false;
    }
    if (name.isEmpty()) {
        emit message(TR("Adj nevet a playlistnek."));
        name_->setFocus();
        return false;
    }
    if (items_.isEmpty()) {
        emit message(TR("Az üres playlist nem menthető: húzz bele elemeket a hangtárból."));
        return false;
    }
    QVector<int> ids;
    for (const Item &it : items_) ids << it.id;
    QString err;
    const int id = db_->saveSavedPlaylist(id_, category, name, ids, &err);
    if (!id) {
        emit message(err);
        return false;
    }
    id_ = id;
    dirty_ = false;
    db_->setSetting("playlist_editor_category", QString::number(category));
    reloadSaved();
    emit message(TR("Playlist elmentve: %1 (%2 elem)").arg(name).arg(ids.size()));
    return true;
}

void PlaylistEditorPage::deleteCurrent()
{
    const QListWidgetItem *item = saved_->currentItem();
    if (!item) return;
    const int id = item->data(Qt::UserRole).toInt();
    QString name;
    for (const SavedPlaylist &p : db_->savedPlaylists())
        if (p.id == id) name = p.name;
    if (QMessageBox::question(this, TR("Playlist törlése"),
                              TR("Törlöd ezt a mentett playlistet?\n\n%1\n\nA hangtár elemei megmaradnak.").arg(name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    const bool wasOpen = id == id_;
    if (wasOpen) id_ = 0;
    db_->deleteSavedPlaylist(id);
    if (wasOpen) {
        dirty_ = !items_.isEmpty();
        updateState();
    }
    emit message(TR("Playlist törölve: %1").arg(name));
}

bool PlaylistEditorPage::confirmDiscard()
{
    if (!dirty_ || items_.isEmpty()) return true;
    return QMessageBox::question(this, TR("Nem mentett módosítások"),
                                 TR("A szerkesztett playlist módosításai nincsenek elmentve. Elveted őket?"),
                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void PlaylistEditorPage::refreshTable()
{
    table_->setRowCount(items_.size());
    QFont mono("IBM Plex Mono");
    mono.setPixelSize(12);
    for (int r = 0; r < items_.size(); ++r) {
        const Item &it = items_[r];
        const QString cells[5] = {QString::number(r + 1), it.title, it.artist, it.categoryName, formatMs(it.durationMs)};
        for (int c = 0; c < 5; ++c) {
            auto *cell = table_->item(r, c);
            if (!cell) {
                cell = new QTableWidgetItem;
                table_->setItem(r, c, cell);
            }
            cell->setText(cells[c]);
            if (c == 0 || c == 4) {
                cell->setFont(mono);
                cell->setTextAlignment((c == 0 ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter);
            }
            if (c == 1) cell->setToolTip(it.path);
            if (c == 3) cell->setIcon(it.color.isEmpty() ? QIcon() : swatchIcon(QColor(it.color)));
        }
    }
    table_->setVisible(!items_.isEmpty());
    hint_->setVisible(items_.isEmpty());
    updateState();
}

void PlaylistEditorPage::updateState()
{
    qint64 total = 0;
    for (const Item &it : items_) total += it.durationMs;
    info_->setText(items_.isEmpty() ? TR("Nincs elem")
                                    : TR("%1 elem · teljes hossz: %2").arg(items_.size()).arg(formatMs(total)));
    title_->setText(id_ ? (dirty_ ? TR("Playlist •") : TR("Playlist")) : TR("Új playlist"));
    title_->setToolTip(dirty_ ? TR("Vannak nem mentett módosítások") : QString());
    save_->setEnabled(!items_.isEmpty() && (dirty_ || !id_));
    insert_->setEnabled(!items_.isEmpty());
    remove_->setEnabled(table_->currentRow() >= 0 && !items_.isEmpty());
    delete_->setEnabled(saved_->currentItem() != nullptr);
}
