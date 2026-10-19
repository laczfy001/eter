// A főablak: hiányzó fájlok ellenőrzése (a playlist várakozó elemei és a teljes hangtár).
#include "MainWindow.h"
#include "core/FileCheck.h"
#include "i18n.h"
#include "net/Remote.h"
#include "PlayerWidget.h"
#include "PlaylistTable.h"
#include "Theme.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

bool MainWindow::rowMissing(const Row &r) const
{
    return (r.state == Row::Pending || r.state == Row::Cued) && missing_.contains(r.item.path);
}

void MainWindow::requestFileCheck()
{
    if (!fileCheck_) {
        fileCheck_ = new FileCheck(this);
        connect(fileCheck_, &FileCheck::finished, this, &MainWindow::onFilesChecked);
    }
    QStringList paths;
    QSet<QString> seen;
    QVector<int> ids;
    auto add = [&](const QString &p) {
        if (!p.isEmpty() && !seen.contains(p)) {
            seen.insert(p);
            paths << p;
        }
    };
    for (const Row &r : rows_) {
        if (r.state != Row::Pending && r.state != Row::Cued) continue;
        add(r.item.path);
        ids << r.id;
    }
    // A voice trackek is: az átmenetükkel együtt szólnának.
    for (const Transition &t : db_->transitionsFor(ids))
        if (t.vtItemId) add(db_->item(t.vtItemId).path);
    fileCheck_->check(paths);
}

void MainWindow::onFilesChecked(const QStringList &checked, const QSet<QString> &missing)
{
    const QSet<QString> before = missing_;
    for (const QString &p : checked) missing_.remove(p);
    missing_ |= missing;
    // Az ütemező ne válasszon olyan elemet, amelynek a fájlja most nem érhető el.
    for (const Row &r : rows_) {
        if (!checked.contains(r.item.path)) continue;
        if (missing_.contains(r.item.path)) missingItems_.insert(r.item.id);
        else missingItems_.remove(r.item.id);
    }
    scheduler_.setUnavailable(missingItems_);
    // Figyelmeztetés: hamarosan adásba kerülne egy elérhetetlen fájl.
    const QDateTime now = eter::now();
    for (int i = 0; i < rows_.size(); ++i) {
        const Row &r = rows_[i];
        if (!rowMissing(r) || warnedMissing_.contains(r.id)) continue;
        const QDateTime at = QDateTime::fromString(QStringLiteral("%1 %2").arg(now.date().toString(Qt::ISODate),
                                                                           i < startText_.size() ? startText_[i] : QString()),
                                                   QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        if (at.isValid() && now.secsTo(at) > 30 * 60) continue;
        warnedMissing_.insert(r.id);
        say(TR("Figyelem: „%1” fájlja nem érhető el, adásba kerülésekor kimarad. (Adatbázis → Hiányzó fájlok)")
                .arg(r.item.title), 20000);
    }
    if (missing_ != before) {
        table_->viewport()->update();
        if (remote_ && remote_->clientCount()) remoteStateTimer_->start();
    }
    updateMissingUi();
}

void MainWindow::updateMissingUi()
{
    int n = 0;
    for (const Row &r : rows_)
        if (rowMissing(r)) ++n;
    if (missingBtn_) {
        missingBtn_->setVisible(n > 0);
        missingBtn_->setText(TR("⚠ %1 hiányzó fájl").arg(n));
    }
    if (!missingDialog_) return;
    auto *table = missingDialog_->findChild<QTableWidget *>();
    auto *info = missingDialog_->findChild<QLabel *>(QStringLiteral("missingInfo"));
    if (!table) return;
    table->setRowCount(0);
    for (int i = 0; i < rows_.size(); ++i) {
        const Row &r = rows_[i];
        if (!rowMissing(r)) continue;
        const int t = table->rowCount();
        table->insertRow(t);
        const QStringList cells{i < startText_.size() ? startText_[i] : QString(), r.item.title, r.item.artist, r.item.path};
        for (int c = 0; c < cells.size(); ++c) {
            auto *it = new QTableWidgetItem(cells[c]);
            it->setData(Qt::UserRole, r.id);
            it->setToolTip(cells[c]);
            table->setItem(t, c, it);
        }
    }
    if (info)
        info->setText(n ? TR("A playlist %1 elemének fájlja nem érhető el ezen a gépen. Ha nem kerül elő, adásba kerülésekor "
                             "kimarad, és a következő elem jön. Ellenőrizd a hálózati meghajtót, vagy vedd ki az elemeket.").arg(n)
                        : TR("A playlist minden fájlja elérhető."));
}

void MainWindow::removeRows(const QVector<int> &rowIds)
{
    QVector<Row> keep;
    for (const Row &row : rows_) {
        if (!rowIds.contains(row.id) || row.state == Row::Playing) {
            keep.push_back(row);
            continue;
        }
        if (row.state == Row::Cued && row.player >= 0) players_[row.player]->eject();
        db_->deleteEntry(row.id);
    }
    rows_ = keep;
    reconcile();
    refreshTable();
}

void MainWindow::showMissingFiles()
{
    if (missingDialog_) {
        missingDialog_->raise();
        missingDialog_->activateWindow();
        requestFileCheck();
        return;
    }
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(TR("Hiányzó fájlok a playlistben"));
    dlg->resize(860, 420);
    auto *v = new QVBoxLayout(dlg);
    auto *info = new QLabel;
    info->setObjectName("missingInfo");
    info->setWordWrap(true);
    v->addWidget(info);
    auto *table = new QTableWidget(0, 4);
    table->setHorizontalHeaderLabels({TR("Kezdés"), TR("Cím"), TR("Előadó"), TR("Fájl")});
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setColumnWidth(0, 80);
    table->setColumnWidth(1, 220);
    table->setColumnWidth(2, 160);
    v->addWidget(table, 1);
    auto *buttons = new QHBoxLayout;
    auto *recheck = new QPushButton(TR("Újraellenőrzés"));
    auto *remove = new QPushButton(TR("Kijelöltek törlése a playlistből"));
    auto *removeAll = new QPushButton(TR("Mind törlése a playlistből"));
    auto *close = new QPushButton(TR("Bezárás"));
    buttons->addWidget(recheck);
    buttons->addStretch(1);
    buttons->addWidget(remove);
    buttons->addWidget(removeAll);
    buttons->addWidget(close);
    v->addLayout(buttons);
    connect(close, &QPushButton::clicked, dlg, &QDialog::close);
    connect(recheck, &QPushButton::clicked, this, [this] { requestFileCheck(); });
    connect(remove, &QPushButton::clicked, this, [this, table] {
        QVector<int> ids;
        for (const QModelIndex &ix : table->selectionModel()->selectedRows()) ids << ix.data(Qt::UserRole).toInt();
        if (!ids.isEmpty()) removeRows(ids);
    });
    connect(removeAll, &QPushButton::clicked, this, [this, dlg] {
        QVector<int> ids;
        for (const Row &r : rows_)
            if (rowMissing(r)) ids << r.id;
        if (ids.isEmpty()) return;
        if (QMessageBox::question(dlg, TR("Hiányzó fájlok"), TR("Kiveszed a playlistből mind a(z) %1 elemet?").arg(ids.size()),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
            removeRows(ids);
    });
    missingDialog_ = dlg;
    updateMissingUi();
    dlg->show();
    requestFileCheck();
}

void MainWindow::scanLibraryFiles()
{
    // A teljes hangtár: minden elem fájlja. Ami nem érhető el, az ütemezőből kimarad, és innen törölhető.
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(TR("Hiányzó fájlok a hangtárban"));
    dlg->resize(860, 460);
    auto *v = new QVBoxLayout(dlg);
    auto *info = new QLabel(TR("Ellenőrzés…"));
    info->setWordWrap(true);
    v->addWidget(info);
    auto *table = new QTableWidget(0, 4);
    table->setHorizontalHeaderLabels({TR("Cím"), TR("Előadó"), TR("Kategória"), TR("Fájl")});
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setColumnWidth(0, 220);
    table->setColumnWidth(1, 160);
    table->setColumnWidth(2, 110);
    v->addWidget(table, 1);
    auto *buttons = new QHBoxLayout;
    auto *remove = new QPushButton(TR("Kijelöltek törlése a hangtárból"));
    remove->setEnabled(false);
    auto *close = new QPushButton(TR("Bezárás"));
    buttons->addStretch(1);
    buttons->addWidget(remove);
    buttons->addWidget(close);
    v->addLayout(buttons);
    connect(close, &QPushButton::clicked, dlg, &QDialog::close);

    const QVector<Item> items = db_->items();
    QStringList paths;
    for (const Item &it : items) paths << it.path;
    auto *check = new FileCheck(dlg);
    connect(check, &FileCheck::progress, dlg, [info](int done, int total) {
        info->setText(TR("Ellenőrzés: %1 / %2 fájl…").arg(done).arg(total));
    });
    connect(check, &FileCheck::finished, dlg, [this, info, table, remove, items](const QStringList &, const QSet<QString> &missing) {
        table->setRowCount(0);
        for (const Item &it : items) {
            if (!missing.contains(it.path)) {
                missingItems_.remove(it.id);
                continue;
            }
            missingItems_.insert(it.id);
            const int t = table->rowCount();
            table->insertRow(t);
            const QStringList cells{it.title, it.artist, it.categoryName, it.path};
            for (int c = 0; c < cells.size(); ++c) {
                auto *cell = new QTableWidgetItem(cells[c]);
                cell->setData(Qt::UserRole, it.id);
                cell->setToolTip(cells[c]);
                table->setItem(t, c, cell);
            }
        }
        scheduler_.setUnavailable(missingItems_);
        info->setText(table->rowCount()
                          ? TR("%1 elem fájlja nem érhető el a(z) %2 közül. Az ütemező ezeket kihagyja, amíg elő nem kerülnek.")
                                .arg(table->rowCount()).arg(items.size())
                          : TR("Mind a(z) %1 elem fájlja elérhető.").arg(items.size()));
        remove->setEnabled(table->rowCount() > 0);
    });
    connect(remove, &QPushButton::clicked, dlg, [this, dlg, table, info] {
        QVector<int> ids;
        for (const QModelIndex &ix : table->selectionModel()->selectedRows()) ids << ix.data(Qt::UserRole).toInt();
        if (ids.isEmpty()) return;
        if (QMessageBox::question(dlg, TR("Törlés a hangtárból"), TR("Törlöd a hangtárból a kijelölt %1 elemet? A fájlokhoz nem nyúl.").arg(ids.size()),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
            return;
        int failed = 0;
        for (int id : ids) {
            if (!db_->deleteItem(id)) { ++failed; continue; }
            missingItems_.remove(id);
            for (int t = table->rowCount() - 1; t >= 0; --t)
                if (table->item(t, 0)->data(Qt::UserRole).toInt() == id) table->removeRow(t);
        }
        db_->notifyItemsChanged();
        if (failed) info->setText(TR("%1 elem nem törölhető, mert a playlistben szerepel; előbb onnan vedd ki.").arg(failed));
    });
    dlg->show();
    check->check(paths);
}

QStringList MainWindow::missingTitles() const
{
    QStringList out;
    for (const Row &r : rows_)
        if (rowMissing(r)) out << r.item.title;
    return out;
}
