#include "i18n.h"
#include "Library.h"
#include "Theme.h"
#include "audio/Decoder.h"

#include <QCheckBox>
#include <QCollator>
#include <QComboBox>
#include <QDataStream>
#include <QDateTime>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QTableView>
#include <QTableWidget>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QElapsedTimer>

// ------------------------------------------------------------ segédek

QStringList audioExtensions()
{
    return {"mp3", "flac", "ogg", "oga", "opus", "wav", "m4a", "aac", "aiff", "aif", "wma", "mp2", "mka"};
}

QByteArray encodeIds(const QVector<int> &ids)
{
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out << ids;
    return data;
}

QVector<int> decodeIds(const QByteArray &data)
{
    QVector<int> ids;
    QDataStream in(data);
    in >> ids;
    return ids;
}

QString formatMs(qint64 ms)
{
    const qint64 s = (ms + 500) / 1000;
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

QColor readableOn(const QColor &bg)
{
    const double lum = 0.299 * bg.redF() + 0.587 * bg.greenF() + 0.114 * bg.blueF();
    return lum > 0.5 ? QColor("#121418") : QColor("#e8eaed");
}

static QString formatLastPlayed(qint64 secs)
{
    if (secs <= 0) return QStringLiteral("–");
    const QDateTime t = QDateTime::fromSecsSinceEpoch(secs);
    const QDate today = QDate::currentDate();
    if (t.date() == today) return TR("ma ") + t.toString("HH:mm");
    if (t.date() == today.addDays(-1)) return TR("tegnap ") + t.toString("HH:mm");
    return i18n::locale().toString(t, t.date().year() == today.year() ? TR("MMM d. HH:mm") : TR("yyyy. MMM d."));
}

static QIcon swatch(const QColor &c)
{
    QPixmap pm(14, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QColor("#6b7380"));
    p.setBrush(c);
    p.drawRoundedRect(QRectF(0.5, 0.5, 13, 13), 3, 3);
    return QIcon(pm);
}

QIcon swatchIcon(const QColor &color) { return swatch(color); }

static QStringList collectFiles(const QStringList &paths, bool recursive)
{
    const QStringList exts = audioExtensions();
    QStringList out;
    auto consider = [&](const QString &f) {
        if (exts.contains(QFileInfo(f).suffix().toLower())) out << f;
    };
    for (const QString &p : paths) {
        const QFileInfo fi(p);
        if (fi.isDir()) {
            QDirIterator it(p, QDir::Files | QDir::Readable,
                            recursive ? QDirIterator::Subdirectories | QDirIterator::FollowSymlinks
                                      : QDirIterator::NoIteratorFlags);
            while (it.hasNext()) consider(it.next());
        } else if (fi.isFile()) {
            consider(fi.absoluteFilePath());
        }
    }
    out.removeDuplicates();
    QCollator col;
    col.setNumericMode(true);
    std::sort(out.begin(), out.end(), [&](const QString &a, const QString &b) { return col.compare(a, b) < 0; });
    return out;
}

static void showNonModal(QDialog *d)
{
    d->setModal(false);              // önállóan mozgatható, az adás közben is kezelhető
    d->show();
    d->raise();
    d->activateWindow();
}

// ------------------------------------------------------------- modell

LibraryModel::LibraryModel(Database *db, QObject *parent) : QAbstractTableModel(parent), db_(db) { reload(); }

void LibraryModel::reload()
{
    beginResetModel();
    all_ = db_->items();
    apply();
    endResetModel();
}

void LibraryModel::setFilter(int categoryId, const QString &text)
{
    beginResetModel();
    categoryId_ = categoryId;
    text_ = text.trimmed();
    apply();
    endResetModel();
}

void LibraryModel::apply()
{
    rows_.clear();
    const QStringList words = text_.split(' ', Qt::SkipEmptyParts);
    for (int i = 0; i < all_.size(); ++i) {
        const Item &it = all_[i];
        if (categoryId_ && it.categoryId != categoryId_) continue;
        bool match = true;
        for (const QString &w : words) {
            if (!it.title.contains(w, Qt::CaseInsensitive) && !it.artist.contains(w, Qt::CaseInsensitive)) {
                match = false;
                break;
            }
        }
        if (match) rows_.push_back(i);
    }
    QCollator col(i18n::locale());
    col.setCaseSensitivity(Qt::CaseInsensitive);
    col.setNumericMode(true);
    const int c = sortColumn_;
    auto less = [&](int a, int b) {
        const Item &x = all_[a], &y = all_[b];
        int r = 0;
        switch (c) {
        case 0: r = col.compare(x.title, y.title); break;
        case 1: r = col.compare(x.artist, y.artist); break;
        case 2: r = col.compare(x.categoryName, y.categoryName); break;
        case 3: r = x.durationMs < y.durationMs ? -1 : x.durationMs > y.durationMs; break;
        default: r = x.lastPlayedAt < y.lastPlayedAt ? -1 : x.lastPlayedAt > y.lastPlayedAt; break;
        }
        if (r == 0) r = col.compare(x.artist, y.artist);
        if (r == 0) r = col.compare(x.title, y.title);
        return r < 0;
    };
    std::stable_sort(rows_.begin(), rows_.end(), less);
    if (sortOrder_ == Qt::DescendingOrder) std::reverse(rows_.begin(), rows_.end());
}

void LibraryModel::sort(int column, Qt::SortOrder order)
{
    beginResetModel();
    sortColumn_ = column;
    sortOrder_ = order;
    apply();
    endResetModel();
}

const Item *LibraryModel::itemAt(int row) const
{
    return row >= 0 && row < rows_.size() ? &all_[rows_[row]] : nullptr;
}

QVariant LibraryModel::data(const QModelIndex &index, int role) const
{
    const Item *it = itemAt(index.row());
    if (!it) return {};
    const int c = index.column();
    if (role == Qt::DisplayRole) {
        switch (c) {
        case 0: return it->title;
        case 1: return it->artist;
        case 2: return it->categoryName;
        case 3: return formatMs(it->durationMs);
        default: return formatLastPlayed(it->lastPlayedAt);
        }
    }
    if (role == Qt::TextAlignmentRole && c >= 3) return int(Qt::AlignRight | Qt::AlignVCenter);
    if (role == Qt::FontRole && c >= 3) {
        QFont mono("IBM Plex Mono");
        mono.setPixelSize(12);
        return mono;
    }
    if (role == Qt::ForegroundRole && (c == 2 || c == 4)) return QColor("#c9ced6");
    if (role == Qt::DecorationRole && c == 2 && !it->color.isEmpty()) return swatch(QColor(it->color));
    if (role == Qt::ToolTipRole) return it->path;
    return {};
}

QVariant LibraryModel::headerData(int section, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const QString names[] = {TR("Cím"), TR("Előadó"), TR("Kategória"),
                                    TR("Hossz"), TR("Utoljára adásban")};
    return names[section];
}

Qt::ItemFlags LibraryModel::flags(const QModelIndex &index) const
{
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled : Qt::NoItemFlags;
}

QMimeData *LibraryModel::mimeData(const QModelIndexList &indexes) const
{
    QVector<int> ids;
    for (const QModelIndex &i : indexes)
        if (i.column() == 0)
            if (const Item *it = itemAt(i.row())) ids << it->id;
    auto *mime = new QMimeData;
    mime->setData(kItemsMime, encodeIds(ids));
    return mime;
}

// -------------------------------------------------------------- panel

LibraryPanel::LibraryPanel(Database *db, QWidget *parent) : QFrame(parent), db_(db)
{
    setObjectName("panel");
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(1, 1, 1, 1);
    layout_->setSpacing(0);

    auto *head = new QFrame;
    head->setObjectName("panelHeader");
    auto *h = new QHBoxLayout(head);
    h->setContentsMargins(10, 8, 10, 8);
    h->setSpacing(6);
    auto *title = new QLabel(TR("Adatbázis"));
    title->setObjectName("panelTitle");
    category_ = new QComboBox;
    category_->setMinimumWidth(150);
    category_->setAccessibleName(TR("Kategória szűrő"));
    search_ = new QLineEdit;
    search_->setPlaceholderText(TR("Keresés cím, előadó szerint…"));
    search_->setClearButtonEnabled(true);
    search_->setMinimumWidth(140);
    search_->setAccessibleName(TR("Keresés az adatbázisban"));
    auto *importBtn = new QPushButton(TR("Importálás"));
    importBtn->setFocusPolicy(Qt::NoFocus);
    auto *menu = new QMenu(importBtn);
    menu->addAction(TR("Fájlok…"), this, &LibraryPanel::chooseFiles);
    menu->addAction(TR("Mappa…"), this, &LibraryPanel::chooseFolder);
    importBtn->setMenu(menu);
    auto *catBtn = new QPushButton(TR("Kategóriák…"));
    catBtn->setFocusPolicy(Qt::NoFocus);
    h->addWidget(title);
    h->addSpacing(6);
    h->addWidget(category_);
    h->addWidget(search_, 1);
    h->addWidget(importBtn);
    h->addWidget(catBtn);
    layout_->addWidget(head);

    model_ = new LibraryModel(db_, this);
    view_ = new QTableView;
    view_->setModel(model_);
    view_->verticalHeader()->hide();
    view_->verticalHeader()->setDefaultSectionSize(30);
    view_->setShowGrid(false);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setFrameShape(QFrame::NoFrame);
    view_->setSortingEnabled(true);
    view_->sortByColumn(1, Qt::AscendingOrder);
    view_->setDragEnabled(true);
    view_->setDragDropMode(QAbstractItemView::DragOnly);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    view_->setIconSize(QSize(14, 14));
    view_->setAcceptDrops(false);
    auto *hh = view_->horizontalHeader();
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hh->setHighlightSections(false);
    hh->setSectionResizeMode(0, QHeaderView::Stretch);
    hh->setSectionResizeMode(1, QHeaderView::Stretch);
    hh->setSectionResizeMode(2, QHeaderView::Fixed);
    hh->setSectionResizeMode(3, QHeaderView::Fixed);
    hh->setSectionResizeMode(4, QHeaderView::Fixed);
    view_->setColumnWidth(2, 150);
    view_->setColumnWidth(3, 64);
    view_->setColumnWidth(4, 130);
    view_->installEventFilter(this);
    setAcceptDrops(true);
    installEventFilter(this);

    empty_ = new QLabel;
    empty_->setObjectName("muted");
    empty_->setAlignment(Qt::AlignCenter);
    empty_->setWordWrap(true);
    layout_->addWidget(view_, 1);
    layout_->addWidget(empty_, 1);

    auto refilter = [this] {
        model_->setFilter(category_->currentData().toInt(), search_->text());
        const bool none = model_->totalCount() == 0;
        view_->setVisible(!none);
        empty_->setVisible(none);
        if (none)
            empty_->setText(db_->itemCategories().isEmpty()
                ? TR("A hangtár még üres.\n\nElőször hozz létre legalább egy kategóriát a „Kategóriák…” gombbal,\n"
                                 "utána az „Importálás” gombbal olvashatsz be fájlokat vagy egy egész mappát.")
                : TR("A hangtár még üres.\n\nAz „Importálás” gombbal olvashatsz be fájlokat vagy egy egész mappát,\n"
                                 "vagy húzd ide őket a fájlkezelőből."));
    };
    connect(category_, &QComboBox::activated, this, refilter);
    connect(search_, &QLineEdit::textChanged, this, refilter);
    connect(db_, &Database::itemsChanged, this, [this, refilter] {
        model_->reload();
        refilter();
    });
    connect(db_, &Database::categoriesChanged, this, [this, refilter] {
        reloadCategories();
        refilter();
    });
    connect(catBtn, &QPushButton::clicked, this, &LibraryPanel::openCategories);
    connect(view_, &QTableView::doubleClicked, this, [this](const QModelIndex &i) {
        if (const Item *it = model_->itemAt(i.row())) emit addToPlaylist({it->id});
    });
    connect(view_, &QTableView::customContextMenuRequested, this, &LibraryPanel::contextMenu);
    connect(view_, &QTableView::pressed, this, [this] { emit selectionTouched(); });
    connect(view_->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this] { emit selectionTouched(); });

    reloadCategories();
    refilter();
}

void LibraryPanel::setFooter(QWidget *footer) { layout_->addWidget(footer); }

void LibraryPanel::reloadCategories()
{
    const int current = category_->currentData().toInt();
    category_->clear();
    category_->addItem(TR("Minden kategória"), 0);
    for (const Category &c : db_->itemCategories())
        category_->addItem(swatch(QColor(c.color)), QStringLiteral("%1 (%2)").arg(c.name).arg(c.itemCount), c.id);
    category_->setCurrentIndex(std::max(0, category_->findData(current)));
}

QVector<int> LibraryPanel::selectedItemIds() const
{
    QVector<int> ids;
    QModelIndexList rows = view_->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(), [](const QModelIndex &a, const QModelIndex &b) { return a.row() < b.row(); });
    for (const QModelIndex &i : rows)
        if (const Item *it = model_->itemAt(i.row())) ids << it->id;
    return ids;
}

Item LibraryPanel::currentItem() const
{
    const Item *it = model_->itemAt(view_->currentIndex().row());
    return it ? *it : Item{};
}

bool LibraryPanel::hasFocusInTable() const { return view_->hasFocus(); }

bool LibraryPanel::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == view_ && ev->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent *>(ev)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter) {
            const QVector<int> ids = selectedItemIds();
            if (!ids.isEmpty()) emit addToPlaylist(ids);
            return true;
        }
        if (key == Qt::Key_Delete) {
            deleteSelected();
            return true;
        }
    }
    if (obj == this) {
        if (ev->type() == QEvent::DragEnter) {
            auto *e = static_cast<QDragEnterEvent *>(ev);
            if (e->mimeData()->hasUrls()) {
                e->acceptProposedAction();
                return true;
            }
        } else if (ev->type() == QEvent::Drop) {
            auto *e = static_cast<QDropEvent *>(ev);
            QStringList paths;
            for (const QUrl &u : e->mimeData()->urls())
                if (u.isLocalFile()) paths << u.toLocalFile();
            if (!paths.isEmpty()) {
                e->acceptProposedAction();
                importPaths(paths);
                return true;
            }
        }
    }
    return QFrame::eventFilter(obj, ev);
}

void LibraryPanel::contextMenu(const QPoint &pos)
{
    const QVector<int> ids = selectedItemIds();
    if (ids.isEmpty()) return;
    QMenu menu(this);
    menu.addAction(TR("Hozzáadás a playlisthez"), this, [this, ids] { emit addToPlaylist(ids); });
    menu.addAction(TR("Előhallgatás"), this, [this] { emit previewRequested(); });
    menu.addAction(TR("Szerkesztés… (adatok, intro, outro)"), this, [this, ids] { emit editRequested(ids.first()); });
    menu.addAction(TR("Időzítés…"), this, [this, ids] { emit scheduleRequested(ids.first()); });
    menu.addSeparator();
    QMenu *move = menu.addMenu(TR("Áthelyezés másik kategóriába"));
    for (const Category &c : db_->itemCategories()) {
        move->addAction(swatch(QColor(c.color)), c.name, this, [this, ids, c] {
            db_->setItemsCategory(ids, c.id);
            emit message(TR("%1 elem áthelyezve ide: %2").arg(ids.size()).arg(c.name));
        });
    }
    menu.addSeparator();
    menu.addAction(TR("Törlés a hangtárból"), this, &LibraryPanel::deleteSelected);
    menu.exec(view_->viewport()->mapToGlobal(pos));
}

void LibraryPanel::deleteSelected()
{
    const QVector<int> ids = selectedItemIds();
    int removed = 0, blocked = 0;
    db_->beginBatch();
    for (int id : ids) {
        if (db_->deleteItem(id)) ++removed;
        else ++blocked;
    }
    db_->endBatch();
    db_->notifyItemsChanged();
    emit db_->categoriesChanged();
    QString msg = TR("%1 elem törölve a hangtárból (a fájlok a lemezen megmaradnak).").arg(removed);
    if (blocked) msg += TR(" %1 elem a playlistben szerepel, ezért megmaradt.").arg(blocked);
    emit message(msg);
}

void LibraryPanel::chooseFiles()
{
    auto *dlg = new QFileDialog(this, TR("Fájlok importálása"), QSettings().value("ui/lastDir").toString(),
                                TR("Hangfájlok (*.%1);;Minden fájl (*)").arg(audioExtensions().join(" *.")));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setFileMode(QFileDialog::ExistingFiles);
    connect(dlg, &QFileDialog::filesSelected, this, [this](const QStringList &files) {
        if (files.isEmpty()) return;
        QSettings().setValue("ui/lastDir", QFileInfo(files.first()).absolutePath());
        importPaths(files);
    });
    showNonModal(dlg);
}

void LibraryPanel::chooseFolder()
{
    auto *dlg = new QFileDialog(this, TR("Mappa importálása"), QSettings().value("ui/lastDir").toString());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setFileMode(QFileDialog::Directory);
    dlg->setOption(QFileDialog::ShowDirsOnly, true);
    connect(dlg, &QFileDialog::filesSelected, this, [this](const QStringList &dirs) {
        if (dirs.isEmpty()) return;
        QSettings().setValue("ui/lastDir", dirs.first());
        importPaths(dirs);
    });
    showNonModal(dlg);
}

void LibraryPanel::importPaths(const QStringList &paths, Done done)
{
    if (importDialog_) {
        emit message(TR("Már folyamatban van egy importálás, előbb azt fejezd be."));
        importDialog_->raise();
        importDialog_->activateWindow();
        return;
    }
    auto *dlg = new ImportDialog(db_, paths, window());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    importDialog_ = dlg;
    connect(dlg, &ImportDialog::categoriesRequested, this, &LibraryPanel::openCategories);
    connect(dlg, &ImportDialog::imported, this, [this, done](const QVector<int> &ids, int added, int existing, int failed) {
        QStringList parts{TR("%1 új elem importálva").arg(added)};
        if (existing) parts << TR("%1 már szerepelt a hangtárban").arg(existing);
        if (failed) parts << TR("%1 fájl nem olvasható hangként").arg(failed);
        emit message(parts.join(QStringLiteral(" · ")));
        if (done) done(ids);
    });
    showNonModal(dlg);
}

void LibraryPanel::openCategories()
{
    if (!categoriesDialog_) {
        categoriesDialog_ = new CategoriesDialog(db_, window());
        categoriesDialog_->setAttribute(Qt::WA_DeleteOnClose);
    }
    showNonModal(categoriesDialog_);
}

// ---------------------------------------------------------- importálás

ImportDialog::ImportDialog(Database *db, const QStringList &paths, QWidget *parent)
    : QDialog(parent), db_(db), paths_(paths)
{
    setWindowTitle(TR("Importálás a hangtárba"));
    setMinimumWidth(520);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 14);
    root->setSpacing(12);

    summary_ = new QLabel;
    summary_->setObjectName("panelTitle");
    summary_->setWordWrap(true);
    root->addWidget(summary_);

    bool hasDir = false;
    for (const QString &p : paths_) hasDir = hasDir || QFileInfo(p).isDir();
    recursive_ = new QCheckBox(TR("Az almappákkal együtt"));
    recursive_->setChecked(true);
    recursive_->setVisible(hasDir);
    root->addWidget(recursive_);

    auto *row = new QHBoxLayout;
    auto *label = new QLabel(TR("Kategória"));
    category_ = new QComboBox;
    category_->setMinimumWidth(240);
    label->setBuddy(category_);
    manage_ = new QPushButton(TR("Kategóriák…"));
    row->addWidget(label);
    row->addWidget(category_, 1);
    row->addWidget(manage_);
    root->addLayout(row);

    auto *hint = new QLabel(TR("Minden beolvasott elem ebbe a kategóriába kerül; később áthelyezhetők. "
                                           "A hangtárban már szereplő fájlok nem kerülnek be még egyszer."));
    hint->setObjectName("small");
    hint->setWordWrap(true);
    root->addWidget(hint);

    progress_ = new QProgressBar;
    progress_->setTextVisible(false);
    progress_->setVisible(false);
    root->addWidget(progress_);
    root->addStretch(1);

    auto *buttons = new QHBoxLayout;
    cancel_ = new QPushButton(TR("Mégse"));
    go_ = new QPushButton(TR("Importálás"));
    go_->setProperty("accent", true);
    go_->setDefault(true);
    go_->setMinimumWidth(110);
    buttons->addStretch(1);
    buttons->addWidget(cancel_);
    buttons->addWidget(go_);
    root->addLayout(buttons);

    timer_ = new QTimer(this);
    timer_->setInterval(0);
    connect(timer_, &QTimer::timeout, this, &ImportDialog::step);
    connect(recursive_, &QCheckBox::toggled, this, &ImportDialog::rescan);
    connect(manage_, &QPushButton::clicked, this, &ImportDialog::categoriesRequested);
    connect(cancel_, &QPushButton::clicked, this, &QDialog::reject);
    connect(go_, &QPushButton::clicked, this, &ImportDialog::start);
    connect(category_, &QComboBox::currentIndexChanged, this, [this] {
        go_->setEnabled(!running_ && !files_.isEmpty() && category_->currentData().toInt() > 0);
    });
    connect(db_, &Database::categoriesChanged, this, [this] {
        if (!running_) reloadCategories(category_->currentData().toInt());
    });
    connect(this, &QDialog::finished, this, [this] {
        if (!running_) return;
        // Megszakítás: ami eddig bekerült, megmarad.
        timer_->stop();
        running_ = false;
        db_->endBatch();
        db_->notifyItemsChanged();
        emit db_->categoriesChanged();
        emit imported(ids_, added_, existing_, failed_);
    });

    reloadCategories();
    rescan();
}

void ImportDialog::rescan()
{
    files_ = collectFiles(paths_, recursive_->isChecked());
    summary_->setText(files_.isEmpty() ? TR("Nem található hangfájl a kijelölésben.")
                                       : TR("%1 hangfájl beolvasása").arg(files_.size()));
    go_->setEnabled(!files_.isEmpty() && category_->currentData().toInt() > 0);
}

void ImportDialog::reloadCategories(int selectId)
{
    const QVector<Category> cats = db_->itemCategories();
    // Ha most jött létre az első/új kategória, azt választjuk ki.
    QSet<int> before;
    for (int i = 0; i < category_->count(); ++i) before << category_->itemData(i).toInt();
    category_->blockSignals(true);
    category_->clear();
    category_->addItem(cats.isEmpty() ? TR("Még nincs kategória – hozz létre egyet")
                                      : TR("Válassz kategóriát…"), 0);
    int fresh = 0;
    for (const Category &c : cats) {
        category_->addItem(swatch(QColor(c.color)), QStringLiteral("%1 · %2").arg(c.name, kindLabel(c.kind)), c.id);
        if (!before.isEmpty() && !before.contains(c.id)) fresh = c.id;
    }
    const int want = fresh ? fresh : selectId;
    category_->setCurrentIndex(std::max(0, category_->findData(want)));
    category_->blockSignals(false);
    go_->setEnabled(!running_ && !files_.isEmpty() && category_->currentData().toInt() > 0);
}

void ImportDialog::start()
{
    categoryId_ = category_->currentData().toInt();
    if (categoryId_ <= 0 || files_.isEmpty()) return;
    running_ = true;
    go_->setEnabled(false);
    category_->setEnabled(false);
    recursive_->setEnabled(false);
    manage_->setEnabled(false);
    cancel_->setText(TR("Megszakítás"));
    progress_->setRange(0, files_.size());
    progress_->setVisible(true);
    db_->beginBatch();
    timer_->start();
}

void ImportDialog::step()
{
    // Rövid szeletekben dolgozik, hogy a felület és az adás kezelhető maradjon.
    QElapsedTimer clock;
    clock.start();
    while (index_ < files_.size() && clock.elapsed() < 25) {
        const QString path = files_[index_++];
        if (const int id = db_->itemIdByPath(path)) {
            ids_ << id;
            ++existing_;
            continue;
        }
        eter::Decoder dec;
        if (!dec.open(path.toStdString())) {
            ++failed_;
            continue;
        }
        Item it;
        it.categoryId = categoryId_;
        it.path = path;
        it.title = QString::fromStdString(dec.title()).trimmed();
        it.artist = QString::fromStdString(dec.artist()).trimmed();
        if (it.title.isEmpty()) {
            // Címke nélkül a fájlnévből: „Előadó - Cím"
            const QString base = QFileInfo(path).completeBaseName();
            const int dash = base.indexOf(QStringLiteral(" - "));
            if (dash > 0 && it.artist.isEmpty()) {
                it.artist = base.left(dash).trimmed();
                it.title = base.mid(dash + 3).trimmed();
            } else {
                it.title = base;
            }
        }
        it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
        if (const int id = db_->addItem(it)) {
            ids_ << id;
            ++added_;
        } else {
            ++failed_;
        }
    }
    progress_->setValue(index_);
    summary_->setText(TR("Importálás: %1 / %2").arg(index_).arg(files_.size()));
    if (index_ >= files_.size()) {
        timer_->stop();
        running_ = false;
        db_->endBatch();
        db_->notifyItemsChanged();
        emit db_->categoriesChanged();
        emit imported(ids_, added_, existing_, failed_);
        accept();
    }
}

// ----------------------------------------------------------- kategóriák

static const struct { const char *name; const char *hex; } kColors[] = {
    {"Szürke", "#3a4150"}, {"Kék", "#5cb0ff"},   {"Borostyán", "#f5a524"}, {"Zöld", "#45d483"},
    {"Lila", "#b99cff"},   {"Rózsaszín", "#ff9fb0"}, {"Türkiz", "#7fd6e8"}, {"Korall", "#ff8f85"},
};

QVector<QPair<QString, QString>> paletteColors()
{
    QVector<QPair<QString, QString>> out;
    for (const auto &c : kColors) out.push_back({i18n::text(c.name), QString::fromLatin1(c.hex)});
    return out;
}

void fillColorCombo(QComboBox *combo)
{
    combo->setIconSize(QSize(14, 14));
    for (const auto &c : kColors) combo->addItem(swatch(QColor(c.hex)), i18n::text(c.name), QString::fromLatin1(c.hex));
}

CategoriesDialog::CategoriesDialog(Database *db, QWidget *parent) : QDialog(parent), db_(db)
{
    setWindowTitle(TR("Kategóriák"));
    setMinimumSize(620, 380);
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 14);
    root->setSpacing(16);

    auto *left = new QVBoxLayout;
    list_ = new QListWidget;
    list_->setIconSize(QSize(14, 14));
    list_->setMinimumWidth(250);
    auto *add = new QPushButton(TR("Új kategória"));
    left->addWidget(list_, 1);
    left->addWidget(add);
    root->addLayout(left, 1);

    auto *right = new QVBoxLayout;
    right->setSpacing(10);
    auto *form = new QFormLayout;
    form->setVerticalSpacing(10);
    name_ = new QLineEdit;
    name_->setMaxLength(60);
    kind_ = new QComboBox;
    for (const QString &k : kindKeys()) kind_->addItem(kindLabel(k), k);
    color_ = new QComboBox;
    fillColorCombo(color_);
    form->addRow(TR("Név"), name_);
    form->addRow(TR("Típus"), kind_);
    form->addRow(TR("Szín"), color_);
    right->addLayout(form);
    auto *kindHint = new QLabel(TR("A típus azt mondja meg a programnak, mire való a kategória "
                                               "(az ütemező ez alapján különbözteti meg például a zenét a reklámtól)."));
    kindHint->setObjectName("small");
    kindHint->setWordWrap(true);
    right->addWidget(kindHint);
    info_ = new QLabel;
    info_->setObjectName("small");
    info_->setWordWrap(true);
    right->addWidget(info_);
    right->addStretch(1);
    auto *buttons = new QHBoxLayout;
    delete_ = new QPushButton(TR("Törlés"));
    delete_->setProperty("danger", true);
    save_ = new QPushButton(TR("Mentés"));
    save_->setProperty("accent", true);
    save_->setDefault(true);
    auto *close = new QPushButton(TR("Bezárás"));
    buttons->addWidget(delete_);
    buttons->addStretch(1);
    buttons->addWidget(close);
    buttons->addWidget(save_);
    right->addLayout(buttons);
    root->addLayout(right, 1);

    connect(list_, &QListWidget::currentRowChanged, this, &CategoriesDialog::showCurrent);
    connect(add, &QPushButton::clicked, this, [this] {
        list_->setCurrentRow(-1);
        editingId_ = 0;
        name_->clear();
        name_->setFocus();
        delete_->setEnabled(false);
        save_->setText(TR("Létrehozás"));
        info_->setText(TR("Új kategória: add meg a nevét, majd nyomd meg a Létrehozás gombot."));
    });
    connect(save_, &QPushButton::clicked, this, &CategoriesDialog::save);
    connect(delete_, &QPushButton::clicked, this, &CategoriesDialog::remove);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(name_, &QLineEdit::returnPressed, this, &CategoriesDialog::save);

    reload();
}

void CategoriesDialog::reload(int selectId)
{
    list_->blockSignals(true);
    list_->clear();
    int row = -1;
    for (const Category &c : db_->categories()) {
        auto *item = new QListWidgetItem(swatch(QColor(c.color)),
                                         TR("%1  ·  %2  ·  %3 elem").arg(c.name, kindLabel(c.kind)).arg(c.itemCount));
        item->setData(Qt::UserRole, c.id);
        list_->addItem(item);
        if (c.id == selectId) row = list_->count() - 1;
    }
    list_->blockSignals(false);
    if (row < 0 && list_->count() > 0 && selectId == 0) row = 0;
    list_->setCurrentRow(row);
    showCurrent();
}

void CategoriesDialog::showCurrent()
{
    const QListWidgetItem *item = list_->currentItem();
    if (!item) {
        if (list_->count() == 0 || editingId_ != 0) {
            editingId_ = 0;
            name_->clear();
            save_->setText(TR("Létrehozás"));
            delete_->setEnabled(false);
            info_->setText(list_->count() == 0
                               ? TR("Még nincs kategória. Add meg az első nevét, például „Aktuális slágerek”.")
                               : QString());
        }
        return;
    }
    const int id = item->data(Qt::UserRole).toInt();
    for (const Category &c : db_->categories()) {
        if (c.id != id) continue;
        editingId_ = id;
        name_->setText(c.name);
        kind_->setCurrentIndex(std::max(0, kind_->findData(c.kind)));
        color_->setCurrentIndex(std::max(0, color_->findData(c.color)));
        save_->setText(TR("Mentés"));
        delete_->setEnabled(c.itemCount == 0);
        info_->setText(c.itemCount ? TR("%1 elem van benne; törölni csak üres kategóriát lehet.").arg(c.itemCount)
                                   : TR("Üres kategória, törölhető."));
    }
}

void CategoriesDialog::save()
{
    const QString name = name_->text().trimmed();
    if (name.isEmpty()) {
        info_->setText(TR("Adj nevet a kategóriának."));
        name_->setFocus();
        return;
    }
    const QString kind = kind_->currentData().toString();
    const QString color = color_->currentData().toString();
    QString err;
    if (editingId_) {
        if (!db_->updateCategory(editingId_, name, kind, color, &err)) {
            info_->setText(err);
            return;
        }
        reload(editingId_);
    } else {
        const int id = db_->addCategory(name, kind, color, &err);
        if (!id) {
            info_->setText(err);
            return;
        }
        reload(id);
    }
}

void CategoriesDialog::remove()
{
    if (!editingId_) return;
    QString err;
    if (!db_->deleteCategory(editingId_, &err)) {
        info_->setText(err);
        return;
    }
    editingId_ = 0;
    reload();
}

// ------------------------------------------------------------ adásnapló

static QString reasonLabel(const QString &r)
{
    if (r == "finished") return TR("végigment");
    if (r == "mixed") return TR("átúsztatva");
    if (r == "faded") return TR("kiúsztatva");
    if (r == "stopped") return TR("megállítva");
    if (r == "error") return TR("megszakadt");
    return TR("szól");
}

LogDialog::LogDialog(Database *db, QWidget *parent) : QDialog(parent), db_(db)
{
    setWindowTitle(TR("Adásnapló"));
    resize(860, 520);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 14);
    root->setSpacing(10);
    table_ = new QTableWidget(0, 7);
    table_->setHorizontalHeaderLabels({TR("Kezdés"), TR("Hossz"), TR("Cím"),
                                       TR("Előadó"), TR("Típus"), TR("Lejátszó"),
                                       TR("Befejezés")});
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(28);
    table_->setShowGrid(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    auto *hh = table_->horizontalHeader();
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hh->setSectionResizeMode(2, QHeaderView::Stretch);
    hh->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->setColumnWidth(0, 160);
    table_->setColumnWidth(1, 64);
    table_->setColumnWidth(4, 90);
    table_->setColumnWidth(5, 70);
    table_->setColumnWidth(6, 100);
    root->addWidget(table_, 1);

    auto *buttons = new QHBoxLayout;
    auto *note = new QLabel(TR("A legutóbbi 2000 elem, a legfrissebb felül."));
    note->setObjectName("small");
    auto *refresh = new QPushButton(TR("Frissítés"));
    auto *exp = new QPushButton(TR("Exportálás CSV-be…"));
    auto *close = new QPushButton(TR("Bezárás"));
    buttons->addWidget(note);
    buttons->addStretch(1);
    buttons->addWidget(refresh);
    buttons->addWidget(exp);
    buttons->addWidget(close);
    root->addLayout(buttons);
    connect(refresh, &QPushButton::clicked, this, &LogDialog::reload);
    connect(exp, &QPushButton::clicked, this, &LogDialog::exportCsv);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    reload();
}

void LogDialog::reload()
{
    rows_ = db_->log(2000);
    table_->setRowCount(rows_.size());
    for (int r = 0; r < rows_.size(); ++r) {
        const LogRow &l = rows_[r];
        const QString cells[7] = {
            QDateTime::fromSecsSinceEpoch(l.startedAt).toString(TR("yyyy.MM.dd. HH:mm:ss")),
            l.endedAt > 0 ? formatMs((l.endedAt - l.startedAt) * 1000) : QString(),
            l.title, l.artist, kindLabel(l.kind), QString::number(l.player), reasonLabel(l.endReason)};
        for (int c = 0; c < 7; ++c) table_->setItem(r, c, new QTableWidgetItem(cells[c]));
    }
}

void LogDialog::exportCsv()
{
    auto *dlg = new QFileDialog(this, TR("Adásnapló exportálása"),
                                QDir::homePath() + QStringLiteral("/adasnaplo-%1.csv").arg(QDate::currentDate().toString(Qt::ISODate)),
                                TR("CSV (*.csv)"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setAcceptMode(QFileDialog::AcceptSave);
    dlg->setDefaultSuffix("csv");
    connect(dlg, &QFileDialog::fileSelected, this, [this](const QString &path) {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
        QTextStream out(&f);
        out.setEncoding(QStringConverter::Utf8);
        out.setGenerateByteOrderMark(true);       // táblázatkezelők így ismerik fel az ékezeteket
        auto q = [](QString s) { return '"' + s.replace('"', "\"\"") + '"'; };
        out << "kezdes;vege;cim;eloado;tipus;lejatszo;befejezes\n";
        for (const LogRow &l : rows_) {
            out << QDateTime::fromSecsSinceEpoch(l.startedAt).toString(Qt::ISODate) << ';'
                << (l.endedAt > 0 ? QDateTime::fromSecsSinceEpoch(l.endedAt).toString(Qt::ISODate) : QString()) << ';'
                << q(l.title) << ';' << q(l.artist) << ';' << q(kindLabel(l.kind)) << ';' << l.player << ';'
                << q(reasonLabel(l.endReason)) << '\n';
        }
    });
    showNonModal(dlg);
}
