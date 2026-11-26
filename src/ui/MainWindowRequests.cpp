// Weboldal: a „Most szól” és a „Következő számok” küldése a WordPress-oldalra, és a látogatók számkérései.
// A kérés a most szóló elem után kerül a playlistbe; ha már vannak kérések a sorban, az utolsó alá. Ha nincs kérés,
// a playlist a szokásos módon megy tovább. Jóváhagyásos módban a kérés először a Kérések ablakba kerül.
#include "MainWindow.h"
#include "Widgets.h"

#include "audio/Engine.h"
#include "i18n.h"
#include "net/WebSync.h"
#include "PlayerWidget.h"
#include "Theme.h"

#include <QStyle>

#include <QDateTime>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace {
bool musicKind(const QString &kind) { return kind.isEmpty() || kind == QLatin1String("music"); }
QString statusText(const QString &s)
{
    if (s == QLatin1String("pending")) return TR("jóváhagyásra vár");
    if (s == QLatin1String("queued")) return TR("a playlistben");
    if (s == QLatin1String("played")) return TR("lejátszva");
    if (s == QLatin1String("rejected")) return TR("elutasítva");
    if (s == QLatin1String("invalid")) return TR("nem kérhető");
    if (s == QLatin1String("removed")) return TR("kivéve a playlistből");
    return s;
}
}

void MainWindow::initWeb()
{
    web_ = new WebSync(db_, this);
    connect(web_, &WebSync::requestsArrived, this, [this](const QVector<WebSync::Incoming> &list) {
        QVector<std::pair<int, int>> v;
        for (const WebSync::Incoming &i : list) v.push_back({i.remoteId, i.itemId});
        onWebRequests(v);
    });
    for (const WebRequest &r : db_->webRequests({QStringLiteral("queued")}))
        if (r.entryId) requestEntries_.insert(r.entryId);
    requestTimer_ = new QTimer(this);
    requestTimer_->setInterval(1000);
    connect(requestTimer_, &QTimer::timeout, this, [this] {
        checkRequests();
        updateWeb();
    });
    requestTimer_->start();
    configureWeb();
}

void MainWindow::configureWeb()
{
    if (!web_) return;
    web_->configure(WebSync::loadConfig());
    updateWeb();
    updateRequestsButton();
}

// Az adásban lévő és a következő elemek a weboldalnak (csak ha változtak, akkor megy ki soron kívül).
void MainWindow::updateWeb()
{
    if (!web_ || !web_->config().enabled) return;
    const WebSync::Config &c = web_->config();
    auto track = [this](const Row &row, qint64 startMs) {
        WebSync::Track t;
        t.title = row.item.title;
        t.artist = row.item.artist;
        t.startMs = startMs;
        t.durationMs = row.frames > 0 ? row.frames * 1000 / eter::kRate : row.item.durationMs;
        t.request = requestEntries_.contains(row.id);
        return t;
    };
    int from = firstMovable();
    const int onAir = onAirPlayer();
    if (onAir >= 0) {
        if (const Row *row = rowById(players_[onAir]->rowId)) {
            from = std::max(from, indexOfRow(row->id) + 1);
            if (!c.musicOnly || musicKind(row->item.kind)) {
                const qint64 start = row->startedAt.isValid()
                                         ? row->startedAt.toMSecsSinceEpoch()
                                         : QDateTime::currentMSecsSinceEpoch() - players_[onAir]->position() * 1000 / eter::kRate;
                webNow_ = track(*row, start);
                webHasNow_ = true;
            }
            // nem zenei elem (jingle, hírek): az előző zene címe marad
        }
    } else if (std::none_of(std::begin(players_), std::end(players_), [](PlayerWidget *p) { return p->isPlaying(); })) {
        webHasNow_ = false;
    }
    QVector<WebSync::Track> next;
    for (int i = std::max(0, from); i < rows_.size() && next.size() < c.nextCount; ++i) {
        const Row &row = rows_[i];
        if (row.overlay || (row.state != Row::Pending && row.state != Row::Cued)) continue;
        if (c.musicOnly && !musicKind(row.item.kind)) continue;
        const QDateTime at = plannedStart_.value(row.id);
        next << track(row, at.isValid() ? at.toMSecsSinceEpoch() : 0);
    }
    web_->setOnAir(std::any_of(std::begin(players_), std::end(players_), [](PlayerWidget *p) { return p->isPlaying(); }));
    web_->setTracks(webNow_, webHasNow_, next);
}

// Hová kerül a következő kérés: a most szóló elem után (a megkezdett blokk és a ráfektetett elemek után), és a már
// sorban álló kérések alá.
int MainWindow::requestInsertIndex() const
{
    int i = firstMovable();
    // A most szóló blokk végéig (egy reklámblokk ne szakadjon ketté).
    if (i > 0)
        while (i < rows_.size() && rows_[i - 1].groupId && rows_[i].groupId == rows_[i - 1].groupId) ++i;
    // A korábbi kérések alá (és a köztük lévő ráfektetett elemek alá).
    while (i < rows_.size() && (requestEntries_.contains(rows_[i].id) || rows_[i].overlay)) {
        if (rows_[i].overlay && !(i > 0 && requestEntries_.contains(rows_[i - 1].id))) break;
        ++i;
    }
    return i;
}

bool MainWindow::queueRequest(const WebRequest &r)
{
    const int at = requestInsertIndex();
    const int before = rows_.size();
    appendItems({r.itemId}, at, 0, {}, QStringLiteral("manual"));
    if (rows_.size() == before) return false;
    int idx = std::clamp(at, firstMovable(), int(rows_.size()) - 1);
    if (rows_[idx].item.id != r.itemId) {
        // biztos, ami biztos: az új sor a legnagyobb azonosítójú ilyen elem
        idx = -1;
        for (int i = 0; i < rows_.size(); ++i)
            if (rows_[i].item.id == r.itemId && (idx < 0 || rows_[i].id > rows_[idx].id)) idx = i;
        if (idx < 0) return false;
    }
    const int entry = rows_[idx].id;
    requestEntries_.insert(entry);
    db_->setWebRequestStatus(r.id, QStringLiteral("queued"), entry);
    web_->ack(r.remoteId, QStringLiteral("queued"));
    refreshTable();
    updateWeb();
    return true;
}

void MainWindow::onWebRequests(const QVector<std::pair<int, int>> &remoteAndItem)
{
    if (!web_) return;
    const bool approve = web_->config().approve;
    for (const auto &[remote, itemId] : remoteAndItem) {
        const WebRequest known = db_->webRequestByRemote(remote);
        if (known.id) {                                    // a weboldal nem kapta meg a nyugtát: újra
            web_->ack(remote, known.status);
            continue;
        }
        const Item it = db_->item(itemId);
        WebRequest r;
        r.remoteId = remote;
        r.itemId = itemId;
        r.title = it.title;
        r.artist = it.artist;
        r.requestedAt = QDateTime::currentSecsSinceEpoch();
        if (!it.id || !web_->requestable(itemId) || missingItems_.contains(itemId)) r.status = QStringLiteral("invalid");
        else if (!web_->requestsOpen()) r.status = QStringLiteral("rejected");
        else r.status = QStringLiteral("pending");
        r.id = db_->addWebRequest(r);
        if (!r.id) continue;
        if (r.status == QLatin1String("pending") && !approve) {
            if (!queueRequest(r)) {
                db_->setWebRequestStatus(r.id, QStringLiteral("invalid"));
                web_->ack(remote, QStringLiteral("invalid"));
                continue;
            }
            say(TR("Számkérés a weboldalról: %1").arg(it.artist.isEmpty() ? it.title : it.artist + QStringLiteral(" – ") + it.title), 9000);
        } else {
            web_->ack(remote, r.status);
            if (r.status == QLatin1String("pending"))
                say(TR("Számkérés jóváhagyásra vár: %1").arg(it.artist.isEmpty() ? it.title : it.artist + QStringLiteral(" – ") + it.title), 9000);
        }
    }
    updateRequestsButton();
    refreshRequestsDialog();
}

// A playlistbe tett kérések sorsa: lejátszva, vagy kivették a playlistből.
void MainWindow::checkRequests()
{
    if (requestEntries_.isEmpty()) return;
    bool changed = false;
    for (const WebRequest &r : db_->webRequests({QStringLiteral("queued")})) {
        const Row *row = r.entryId ? rowById(r.entryId) : nullptr;
        QString status;
        if (!row) status = QStringLiteral("removed");
        else if (row->state == Row::Playing || row->state == Row::Played) status = QStringLiteral("played");
        else if (row->state == Row::Skipped || row->state == Row::Failed) status = QStringLiteral("removed");
        if (status.isEmpty()) continue;
        db_->setWebRequestStatus(r.id, status);
        if (web_) web_->ack(r.remoteId, status);
        if (!row) requestEntries_.remove(r.entryId);
        changed = true;
    }
    // A lejátszott sorok jelölése marad, amíg a playlistben vannak.
    for (auto it = requestEntries_.begin(); it != requestEntries_.end();)
        it = rowById(*it) ? std::next(it) : requestEntries_.erase(it);
    if (changed) refreshRequestsDialog();
}

int MainWindow::pendingRequestCount() const
{
    return db_->webRequests({QStringLiteral("pending")}).size();
}

void MainWindow::approveRequest(int requestId)
{
    for (const WebRequest &r : db_->webRequests({QStringLiteral("pending")})) {
        if (r.id != requestId) continue;
        if (!queueRequest(r)) {
            db_->setWebRequestStatus(r.id, QStringLiteral("invalid"));
            if (web_) web_->ack(r.remoteId, QStringLiteral("invalid"));
        }
    }
    updateRequestsButton();
    refreshRequestsDialog();
}

void MainWindow::rejectRequest(int requestId)
{
    for (const WebRequest &r : db_->webRequests({QStringLiteral("pending")})) {
        if (r.id != requestId) continue;
        db_->setWebRequestStatus(r.id, QStringLiteral("rejected"));
        if (web_) web_->ack(r.remoteId, QStringLiteral("rejected"));
    }
    updateRequestsButton();
    refreshRequestsDialog();
}

void MainWindow::updateRequestsButton()
{
    if (!requestsBtn_ || !web_) return;
    const WebSync::Config &c = web_->config();
    const int pending = pendingRequestCount();
    requestsBtn_->setVisible((c.enabled && c.requests) || pending > 0);
    requestsBtn_->setText(pending > 0 ? TR("Kérések (%1)").arg(pending) : TR("Kérések"));
    requestsBtn_->setProperty("pending", pending > 0);
    requestsBtn_->style()->unpolish(requestsBtn_);
    requestsBtn_->style()->polish(requestsBtn_);
    requestsBtn_->setToolTip(c.approve ? TR("A weboldalról érkezett számkérések; jóváhagyás után kerülnek a playlistbe")
                                       : TR("A weboldalról érkezett számkérések (magától a playlistbe kerülnek)"));
}

void MainWindow::openRequestsDialog()
{
    if (requestsDlg_) {
        requestsDlg_->raise();
        requestsDlg_->activateWindow();
        return;
    }
    auto *dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(TR("Számkérések"));
    dlg->resize(720, 420);
    auto *v = new QVBoxLayout(dlg);
    auto *info = new QLabel;
    info->setObjectName("requestsInfo");
    info->setWordWrap(true);
    v->addWidget(info);
    auto *table = new QTableWidget(0, 4);
    table->setObjectName("requestsTable");
    table->setHorizontalHeaderLabels({TR("Idő"), TR("Előadó"), TR("Cím"), TR("Állapot")});
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setShowGrid(false);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->setColumnWidth(0, 70);
    table->setColumnWidth(1, 200);
    table->setColumnWidth(3, 150);
    makeColumnsResizable(table, QStringLiteral("webRequests"));
    v->addWidget(table, 1);
    auto *row = new QHBoxLayout;
    auto *ok = new QPushButton(TR("Jóváhagyás"));
    ok->setProperty("accent", true);
    auto *all = new QPushButton(TR("Mind jóváhagyása"));
    auto *no = new QPushButton(TR("Elutasítás"));
    no->setProperty("danger", true);
    auto *close = new QPushButton(TR("Bezárás"));
    row->addWidget(ok);
    row->addWidget(all);
    row->addWidget(no);
    row->addStretch(1);
    row->addWidget(close);
    v->addLayout(row);
    auto selectedPending = [table] {
        QVector<int> ids;
        for (const QModelIndex &i : table->selectionModel()->selectedRows())
            if (table->item(i.row(), 0)->data(Qt::UserRole + 1).toBool()) ids << table->item(i.row(), 0)->data(Qt::UserRole).toInt();
        return ids;
    };
    connect(ok, &QPushButton::clicked, dlg, [this, selectedPending] {
        for (int id : selectedPending()) approveRequest(id);
    });
    connect(no, &QPushButton::clicked, dlg, [this, selectedPending] {
        for (int id : selectedPending()) rejectRequest(id);
    });
    connect(all, &QPushButton::clicked, dlg, [this] {
        for (const WebRequest &r : db_->webRequests({QStringLiteral("pending")})) approveRequest(r.id);
    });
    connect(close, &QPushButton::clicked, dlg, &QDialog::close);
    requestsDlg_ = dlg;
    refreshRequestsDialog();
    dlg->show();
}

void MainWindow::refreshRequestsDialog()
{
    if (!requestsDlg_) return;
    auto *table = requestsDlg_->findChild<QTableWidget *>("requestsTable");
    auto *info = requestsDlg_->findChild<QLabel *>("requestsInfo");
    if (!table) return;
    QVector<WebRequest> all = db_->webRequests();
    // Elöl a jóváhagyásra várók, utánuk a legutóbbiak (legfeljebb 50).
    QVector<WebRequest> list;
    for (const WebRequest &r : all)
        if (r.status == QLatin1String("pending")) list << r;
    const int pending = list.size();
    for (int i = all.size() - 1; i >= 0 && list.size() < pending + 50; --i)
        if (all[i].status != QLatin1String("pending")) list << all[i];
    table->setRowCount(list.size());
    for (int i = 0; i < list.size(); ++i) {
        const WebRequest &r = list[i];
        const bool p = r.status == QLatin1String("pending");
        auto *t = new QTableWidgetItem(QDateTime::fromSecsSinceEpoch(r.requestedAt).toString("HH:mm"));
        t->setData(Qt::UserRole, r.id);
        t->setData(Qt::UserRole + 1, p);
        table->setItem(i, 0, t);
        table->setItem(i, 1, new QTableWidgetItem(r.artist));
        table->setItem(i, 2, new QTableWidgetItem(r.title));
        auto *s = new QTableWidgetItem(statusText(r.status));
        if (p) s->setForeground(theme::amber);
        table->setItem(i, 3, s);
    }
    if (info) {
        const WebSync::Config &c = web_ ? web_->config() : WebSync::Config();
        QString text = !c.enabled || !c.requests ? TR("A számkérés ki van kapcsolva (Beállítások → Weboldal).")
                     : c.approve ? TR("A kérések jóváhagyás után kerülnek a playlistbe: a most szóló elem után, a korábbi kérések alá.")
                                 : TR("A kérések magától a playlistbe kerülnek: a most szóló elem után, a korábbi kérések alá.");
        if (web_ && c.enabled && c.requests) text += QLatin1Char(' ') + (web_->requestsOpen() ? TR("Most lehet kérni.") : TR("Most nem lehet kérni."));
        info->setText(text);
    }
}

void MainWindow::injectRequestsForTest(const QVector<std::pair<int, int>> &remoteAndItem)
{
    onWebRequests(remoteAndItem);
}
