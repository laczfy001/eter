// A főablak: tartalék playout. A fő gép a csatlakozott tartalékoknak elküldi a teljes adatbázis másolatát (és a
// távoli szerkesztés élő állapotát, benne azzal, hol tart a szóló elem). Ha a tartalék átveszi az adást, ez a főablak
// fut rajta a másolattal; a fő gép visszatérésekor a Visszaadás pontosan onnan adja vissza neki az adást, ahol tart.
#include "MainWindow.h"
#include "i18n.h"
#include "net/Remote.h"
#include "PlayerWidget.h"
#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSqlQuery>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>

QJsonObject MainWindow::remoteSnapshot()
{
    // Az élő adatbázis konzisztens másolata (SQLite VACUUM INTO), egy ideiglenes fájlon át.
    QTemporaryDir dir;
    if (!dir.isValid()) return {};
    const QString path = dir.filePath(QStringLiteral("snapshot.sqlite"));
    QSqlQuery q;
    QString quoted = path;
    quoted.replace(QLatin1Char('\''), QStringLiteral("''"));
    if (!q.exec(QStringLiteral("VACUUM INTO '%1'").arg(quoted))) return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray data = f.readAll();
    return QJsonObject{{"t", "snapshot"}, {"at", eter::now().toString(Qt::ISODate)}, {"size", qint64(data.size())},
                       {"db", QString::fromLatin1(data.toBase64())}};
}

void MainWindow::sendSnapshot()
{
    if (!remote_ || !remote_->count(remote::Server::Backups)) return;
    const QJsonObject snap = remoteSnapshot();
    if (!snap.isEmpty()) remote_->broadcast(snap, remote::Server::Backups);
}

QString MainWindow::applyHandback(const QJsonObject &m)
{
    // A tartalék visszaadja az adást: ami ott már lement, itt is lement; a szóló elem innen folytatódik.
    if (players_[0]->isPlaying() || players_[1]->isPlaying())
        return TR("A fő playouton már szól valami – előbb állítsd le ott, vagy vedd át kézzel.");
    QSet<int> played;
    for (const QJsonValue &v : m.value("played").toArray()) played.insert(v.toInt());
    const int entry = m.value("entry").toInt();
    const qint64 pos = std::max<qint64>(0, m.value("pos").toInteger());
    for (Row &r : rows_) {
        if (!played.contains(r.id) || (r.state != Row::Pending && r.state != Row::Cued)) continue;
        if (r.state == Row::Cued && r.player >= 0) players_[r.player]->eject();
        r.state = Row::Played;
        r.player = -1;
        db_->setEntryStatus(r.id, "played");
    }
    trimHistory();
    reconcile();
    refreshTable();
    setMode(Auto);
    // Ha a szóló elem sora itt már nincs meg (például a fő gép újraindult), ugyanaz az elem új sorként kerül előre.
    int target = entry;
    const int itemId = m.value("item").toInt();
    if (!rowById(entry) && itemId && db_->item(itemId).id) {
        appendItems({itemId}, firstMovable());
        const int at = firstMovable();
        for (int i = at; i < rows_.size(); ++i)
            if (rows_[i].item.id == itemId && (rows_[i].state == Row::Pending || rows_[i].state == Row::Cued)) {
                target = rows_[i].id;
                break;
            }
    }
    takeOver(target, pos);
    say(TR("A tartalék visszaadta az adást; innen folytatódik."), 12000);
    return {};
}

void MainWindow::takeOver(int entryId, qint64 positionMs)
{
    setMode(Auto);
    Row *row = rowById(entryId);
    if (row && (row->state == Row::Pending || row->state == Row::Cued) && positionMs > 0) {
        const qint64 durMs = row->frames * 1000 / eter::kRate;
        if (durMs <= 0 || positionMs < durMs - 1500) {
            int player = row->state == Row::Cued && row->player >= 0 ? row->player : 0;
            if (row->state != Row::Cued) loadRowInto(entryId, player);
            row = rowById(entryId);
            if (row && row->state == Row::Cued && row->player >= 0) {
                player = row->player;
                players_[player]->cueTo(positionMs * eter::kRate / 1000);
                players_[player]->playMixed(400, -24);           // rövid beúsztatás: nem kattan a folytatás
                say(TR("Adás átvéve: „%1” innen folytatódik: %2.").arg(row->item.title, theme::formatTime(positionMs * eter::kRate / 1000)), 15000);
                return;
            }
        } else {
            // Az elem már a végén járt: kimarad, a következő indul.
            row->state = Row::Played;
            db_->setEntryStatus(row->id, "played");
            reconcile();
            refreshTable();
        }
    }
    next();
    say(TR("Adás átvéve: a következő elem indult."), 15000);
}

void MainWindow::watchOrigin(const QString &host, quint16 port, const QString &user, const QByteArray &salt, const QByteArray &key,
                             const QStringList &relaunchArgs)
{
    originHost_ = host;
    relaunchArgs_ = relaunchArgs;
    // Felső sáv: a tartalék van adásban; ha a fő gép újra elérhető, itt adható vissza.
    handbackBar_ = new QFrame;
    handbackBar_->setObjectName("handbackBar");
    handbackBar_->setStyleSheet(QStringLiteral("#handbackBar { background: #3a1d1f; border-bottom: 1px solid #e5484d; }"));
    auto *h = new QHBoxLayout(handbackBar_);
    h->setContentsMargins(14, 6, 10, 6);
    handbackText_ = new QLabel;
    handbackText_->setStyleSheet(QStringLiteral("color: #ffd0cc; font-weight: 600;"));
    auto *btn = new QPushButton(TR("Visszaadás a fő playoutnak"));
    btn->setObjectName("handbackButton");
    btn->setEnabled(false);
    h->addWidget(handbackText_, 1);
    h->addWidget(btn);
    if (auto *central = centralWidget())
        if (auto *v = qobject_cast<QVBoxLayout *>(central->layout())) v->insertWidget(0, handbackBar_);
    handbackText_->setText(TR("TARTALÉK ADÁSBAN – a fő playout (%1) nem érhető el.").arg(host));
    connect(btn, &QPushButton::clicked, this, &MainWindow::handBack);

    origin_ = new remote::Client(this);
    origin_->setRole(QStringLiteral("onair"));
    origin_->setKeyCache(salt, key);
    auto retry = new QTimer(this);
    retry->setInterval(5000);
    connect(retry, &QTimer::timeout, this, [this, host, port, user] {
        if (!origin_->ready()) origin_->connectTo(host, port, user, QString());
    });
    connect(origin_, &remote::Client::loggedIn, this, [this, btn, host] {
        handbackText_->setText(TR("TARTALÉK ADÁSBAN – a fő playout (%1) újra elérhető. Ha ott minden rendben, add vissza az adást.").arg(host));
        btn->setEnabled(true);
    });
    connect(origin_, &remote::Client::lost, this, [this, btn, host](const QString &) {
        handbackText_->setText(TR("TARTALÉK ADÁSBAN – a fő playout (%1) nem érhető el.").arg(host));
        btn->setEnabled(false);
    });
    retry->start();
    origin_->connectTo(host, port, user, QString());
}

void MainWindow::handBack()
{
    if (!origin_ || !origin_->ready()) return;
    // Ami a tartalékon már lement (vagy kimaradt), és ami most szól, és hol tart.
    QJsonArray played;
    int entry = 0, item = 0;
    qint64 pos = 0;
    for (const Row &r : rows_) {
        if (r.state == Row::Played || r.state == Row::Skipped || r.state == Row::Failed) played.push_back(r.id);
        if (r.state == Row::Playing && r.player >= 0 && !r.overlay && !leaving(r.player)) {
            entry = r.id;
            item = r.item.id;
            pos = players_[r.player]->position() * 1000 / eter::kRate;
        }
    }
    if (QMessageBox::question(this, TR("Visszaadás"), TR("Visszaadod az adást a fő playoutnak (%1)? Ott ugyanonnan folytatódik, "
                                                        "itt pedig elhalkul, és ez a gép újra tartalékként áll készenlétben.").arg(originHost_),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    // Az eltelt idő a válaszig: a fő gépen pár tized másodperccel később indul, ezért kicsit előre számolunk.
    const int id = origin_->sendOp(QStringLiteral("handback"), QJsonObject{{"entry", entry}, {"item", item}, {"pos", pos + 300}, {"played", played}});
    if (!id) return;
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(origin_, &remote::Client::message, this, [this, id, conn](const QJsonObject &m) {
        if (m.value("t").toString() != QLatin1String("ack") || m.value("id").toInt() != id) return;
        disconnect(*conn);
        if (!m.value("ok").toBool()) {
            QMessageBox::warning(this, TR("Visszaadás"), TR("A fő playout nem vette át: %1").arg(m.value("error").toString()));
            return;
        }
        // Átvette: itt elhalkul, majd a program tartalék módban újraindul.
        for (PlayerWidget *p : players_)
            if (p->isPlaying()) p->fadeOut(1200);
        setMode(Assist);
        handbackText_->setText(TR("Visszaadva a fő playoutnak. Újraindulás tartalékként…"));
        QTimer::singleShot(2000, this, [this] {
            const QString appImage = qEnvironmentVariable("APPIMAGE");
            const QString exe = appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
            QProcess::startDetached(exe, relaunchArgs_);
            QApplication::exit(0);
        });
    });
}
