#include "BackupWindow.h"
#include "db/Database.h"
#include "i18n.h"
#include "RemoteEditor.h"
#include "Theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSpinBox>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cstdio>

// ------------------------------------------------------------- beállítások

BackupConfig BackupWindow::loadConfig()
{
    QSettings s;
    BackupConfig c;
    c.host = s.value("backup/host").toString();
    c.port = quint16(s.value("backup/port", remote::kDefaultPort).toUInt());
    c.user = s.value("backup/user").toString();
    c.salt = QByteArray::fromBase64(s.value("backup/salt").toByteArray());
    c.key = QByteArray::fromBase64(s.value("backup/key").toByteArray());
    c.pathFrom = s.value("backup/pathFrom").toString();
    c.pathTo = s.value("backup/pathTo").toString();
    c.autoTakeover = s.value("backup/auto", true).toBool();
    c.timeoutS = std::clamp(s.value("backup/timeout", 10).toInt(), 3, 300);
    // Fejlesztéshez: környezeti változókkal felülbírálható.
    if (qEnvironmentVariableIsSet("ETER_BACKUP_HOST")) c.host = qEnvironmentVariable("ETER_BACKUP_HOST");
    if (qEnvironmentVariableIsSet("ETER_BACKUP_PORT")) c.port = quint16(qEnvironmentVariableIntValue("ETER_BACKUP_PORT"));
    if (qEnvironmentVariableIsSet("ETER_BACKUP_USER")) c.user = qEnvironmentVariable("ETER_BACKUP_USER");
    if (qEnvironmentVariableIsSet("ETER_BACKUP_TIMEOUT")) c.timeoutS = qEnvironmentVariableIntValue("ETER_BACKUP_TIMEOUT");
    if (qEnvironmentVariableIsSet("ETER_BACKUP_MANUAL")) c.autoTakeover = false;
    return c;
}

void BackupWindow::saveConfig(const BackupConfig &c)
{
    QSettings s;
    s.setValue("backup/host", c.host);
    s.setValue("backup/port", c.port);
    s.setValue("backup/user", c.user);
    s.setValue("backup/salt", c.salt.toBase64());
    s.setValue("backup/key", c.key.toBase64());
    s.setValue("backup/pathFrom", c.pathFrom);
    s.setValue("backup/pathTo", c.pathTo);
    s.setValue("backup/auto", c.autoTakeover);
    s.setValue("backup/timeout", c.timeoutS);
}

// ------------------------------------------------------------------ ablak

BackupWindow::BackupWindow(const QString &replicaPath, QWidget *parent)
    : QMainWindow(parent), replica_(replicaPath), cfg_(loadConfig()), client_(new remote::Client(this))
{
    setWindowTitle(TR("Éter – tartalék playout"));
    setWindowIcon(theme::appIcon());
    resize(900, 600);
    auto *central = new QWidget;
    auto *v = new QVBoxLayout(central);
    v->setContentsMargins(12, 12, 12, 12);
    v->setSpacing(10);
    banner_ = new QLabel;
    banner_->setWordWrap(true);
    banner_->setTextFormat(Qt::PlainText);
    banner_->setMinimumHeight(54);
    v->addWidget(banner_);
    now_ = new QLabel;
    now_->setTextFormat(Qt::PlainText);
    now_->setWordWrap(true);
    QFont big = now_->font();
    big.setPixelSize(17);
    big.setWeight(QFont::DemiBold);
    now_->setFont(big);
    v->addWidget(now_);
    auto *cap = new QLabel(TR("A FŐ PLAYOUT PLAYLISTJE"));
    cap->setObjectName("caption");
    v->addWidget(cap);
    next_ = new QTableWidget(0, 3);
    next_->setHorizontalHeaderLabels({TR("Kezdés"), TR("Cím"), TR("Állapot")});
    next_->verticalHeader()->hide();
    next_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    next_->setSelectionMode(QAbstractItemView::NoSelection);
    next_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    next_->setColumnWidth(0, 80);
    next_->setColumnWidth(2, 130);
    v->addWidget(next_, 1);
    info_ = new QLabel;
    info_->setObjectName("small");
    info_->setWordWrap(true);
    v->addWidget(info_);
    auto *buttons = new QHBoxLayout;
    auto *settings = new QPushButton(TR("Beállítások…"));
    takeBtn_ = new QPushButton(TR("Átvétel most"));
    takeBtn_->setProperty("accent", true);
    takeBtn_->setToolTip(TR("A tartalék azonnal átveszi az adást a fő playouttól"));
    buttons->addWidget(settings);
    buttons->addStretch(1);
    buttons->addWidget(takeBtn_);
    v->addLayout(buttons);
    setCentralWidget(central);

    retry_ = new QTimer(this);
    retry_->setInterval(3000);
    connect(retry_, &QTimer::timeout, this, [this] {
        if (!client_->ready() && !cfg_.host.isEmpty()) client_->connectTo(cfg_.host, cfg_.port, cfg_.user, QString());
    });
    checker_ = new QTimer(this);
    checker_->setInterval(250);
    connect(checker_, &QTimer::timeout, this, &BackupWindow::check);

    client_->setRole(QStringLiteral("backup"));
    connect(client_, &remote::Client::loggedIn, this, [this] {
        everConnected_ = true;
        online_ = true;
        bye_ = false;
        lastError_.clear();
        stateClock_.restart();
        // A sikeres belépés kulcsa megmarad: újraindítás után jelszó nélkül csatlakozik.
        if (client_->lastKey() != cfg_.key || client_->lastSalt() != cfg_.salt) {
            cfg_.salt = client_->lastSalt();
            cfg_.key = client_->lastKey();
            saveConfig(cfg_);
        }
        refresh();
    });
    connect(client_, &remote::Client::failed, this, [this](const QString &why) {
        online_ = false;
        lastError_ = why;
        refresh();
    });
    connect(client_, &remote::Client::lost, this, [this](const QString &why) {
        online_ = false;
        lastError_ = why;
        lostClock_.restart();
        refresh();
    });
    connect(client_, &remote::Client::message, this, &BackupWindow::onMessage);
    connect(settings, &QPushButton::clicked, this, &BackupWindow::openSettings);
    connect(takeBtn_, &QPushButton::clicked, this, [this] { takeOverNow(true); });
    loadTitles();
    refresh();
}

void BackupWindow::closeEvent(QCloseEvent *e)
{
    stop();
    QMainWindow::closeEvent(e);
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

void BackupWindow::start(const QString &password)
{
    if (cfg_.host.isEmpty()) {
        refresh();
        QTimer::singleShot(0, this, &BackupWindow::openSettings);
        return;
    }
    client_->setKeyCache(cfg_.salt, cfg_.key);
    client_->connectTo(cfg_.host, cfg_.port, cfg_.user, password);
    retry_->start();
    checker_->start();
}

void BackupWindow::stop()
{
    retry_->stop();
    checker_->stop();
    client_->disconnectFromHost();
}

bool BackupWindow::hasReplica() const { return QFileInfo::exists(replica_) && QFileInfo(replica_).size() > 0; }

void BackupWindow::onMessage(const QJsonObject &m)
{
    const QString t = m.value("t").toString();
    stateClock_.restart();                         // minden üzenet életjel
    if (t == QLatin1String("snapshot")) storeSnapshot(m);
    else if (t == QLatin1String("state")) {
        lastState_ = m;
        refresh();
    } else if (t == QLatin1String("bye")) {
        bye_ = true;                               // szabályos leállás: nem vesszük át
        refresh();
    }
}

void BackupWindow::storeSnapshot(const QJsonObject &m)
{
    const QByteArray data = QByteArray::fromBase64(m.value("db").toString().toLatin1());
    if (data.size() < 512) return;
    QDir().mkpath(QFileInfo(replica_).absolutePath());
    const QString tmp = replica_ + QStringLiteral(".new");
    {
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
            lastError_ = TR("A másolat nem írható ide: %1").arg(tmp);
            refresh();
            return;
        }
    }
    // A hangfájlok útvonala erre a gépre fordítva (a közös hálózati mappa itteni helye).
    if (!cfg_.pathFrom.isEmpty()) {
        const QString root = RemoteEditorWindow::localFolder(cfg_.pathTo);
        const QString conn = QStringLiteral("eter-backup-map");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
            db.setDatabaseName(tmp);
            if (db.open()) {
                QSqlQuery sel(db), upd(db);
                db.transaction();
                sel.exec(QStringLiteral("SELECT id, file_path FROM items"));
                upd.prepare(QStringLiteral("UPDATE items SET file_path = ? WHERE id = ?"));
                QSet<QString> used;
                QVector<QPair<int, QString>> changes;
                while (sel.next()) {
                    QString p = RemoteEditorWindow::mapWith(sel.value(1).toString(), cfg_.pathFrom, root);
                    if (used.contains(p)) p += QStringLiteral("#%1").arg(sel.value(0).toInt());
                    used.insert(p);
                    changes.push_back({sel.value(0).toInt(), p});
                }
                // Két lépésben, hogy az egyedi útvonal-megkötés a csere közben se akadjon el.
                for (const auto &c : changes) {
                    upd.addBindValue(QStringLiteral("~tmp~%1").arg(c.first));
                    upd.addBindValue(c.first);
                    upd.exec();
                }
                for (const auto &c : changes) {
                    upd.addBindValue(c.second);
                    upd.addBindValue(c.first);
                    upd.exec();
                }
                db.commit();
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(conn);
    }
    QFile::remove(replica_ + QStringLiteral("-wal"));
    QFile::remove(replica_ + QStringLiteral("-shm"));
    QFile::remove(replica_);
    if (!QFile::rename(tmp, replica_)) {
        lastError_ = TR("A másolat nem cserélhető: %1").arg(replica_);
        refresh();
        return;
    }
    snapshotAt_ = QDateTime::fromString(m.value("at").toString(), Qt::ISODate).toString(QStringLiteral("HH:mm:ss"));
    loadTitles();
    refresh();
}

void BackupWindow::loadTitles()
{
    titles_.clear();
    snapshotItems_ = 0;
    if (!hasReplica()) return;
    const QString conn = QStringLiteral("eter-backup-read");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), conn);
        db.setDatabaseName(replica_);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery q(db);
            q.exec(QStringLiteral("SELECT id, title, artist FROM items"));
            while (q.next()) titles_.insert(q.value(0).toInt(), {q.value(1).toString(), q.value(2).toString()});
            snapshotItems_ = int(titles_.size());
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(conn);
    if (snapshotAt_.isEmpty()) snapshotAt_ = QFileInfo(replica_).lastModified().toString(QStringLiteral("HH:mm:ss"));
}

int BackupWindow::playingEntry(qint64 *positionMs) const
{
    for (const QJsonValue &v : lastState_.value("rows").toArray()) {
        const QJsonObject o = v.toObject();
        if (o.value("state").toString() != QLatin1String("playing") || o.contains("ovl")) continue;
        if (!o.contains("pos")) continue;
        if (positionMs) *positionMs = o.value("pos").toInteger() + std::max<qint64>(0, stateAgeMs());
        return o.value("id").toInt();
    }
    return 0;
}

void BackupWindow::refresh()
{
    QString text, style;
    const qint64 age = stateAgeMs();
    if (cfg_.host.isEmpty()) {
        text = TR("Nincs beállítva a fő playout. Kattints a Beállítások… gombra.");
        style = QStringLiteral("#3a2c10");
    } else if (online_) {
        text = TR("KÉSZENLÉT – a fő playout (%1) rendben, a tartalék tükrözi. Utolsó jel: %2 mp.")
                   .arg(cfg_.host, i18n::locale().toString(std::max<qint64>(0, age) / 1000.0, 'f', 1));
        style = QStringLiteral("#163a26");
    } else if (bye_) {
        text = TR("A fő playout (%1) szabályosan leállt – a tartalék nem veszi át magától az adást.").arg(cfg_.host);
        style = QStringLiteral("#2a2f38");
    } else if (everConnected_) {
        const int left = cfg_.timeoutS - int(std::max<qint64>(0, age) / 1000);
        text = cfg_.autoTakeover ? TR("NINCS JEL a fő playouttól (%1) %2 mp óta – átvétel %3 mp múlva.")
                                       .arg(cfg_.host).arg(std::max<qint64>(0, age) / 1000).arg(std::max(0, left))
                                 : TR("NINCS JEL a fő playouttól (%1) %2 mp óta – kézi átvétel: Átvétel most.")
                                       .arg(cfg_.host).arg(std::max<qint64>(0, age) / 1000);
        style = QStringLiteral("#3a1d1f");
    } else {
        text = TR("Csatlakozás a fő playouthoz: %1:%2…").arg(cfg_.host).arg(cfg_.port);
        if (!lastError_.isEmpty()) text += QStringLiteral("  ") + lastError_;
        style = QStringLiteral("#3a2c10");
    }
    banner_->setText(text);
    banner_->setStyleSheet(QStringLiteral("background: %1; color: #f3f4f6; font-weight: 600; border-radius: 6px; padding: 10px 14px;").arg(style));

    // Mi szól és mi következik a fő gépen
    qint64 pos = 0;
    const int playing = playingEntry(&pos);
    QString nowText = TR("A fő playouton most nem szól semmi.");
    next_->setRowCount(0);
    for (const QJsonValue &v : lastState_.value("rows").toArray()) {
        const QJsonObject o = v.toObject();
        const QString st = o.value("state").toString();
        const auto t = titles_.value(o.value("item").toInt());
        const QString title = t.second.isEmpty() ? t.first : t.first + QStringLiteral(" – ") + t.second;
        if (o.value("id").toInt() == playing) {
            const qint64 dur = o.value("dur").toInteger();
            nowText = TR("Most a fő playouton: %1  (%2 / %3)").arg(title, theme::formatTime(pos * 48), theme::formatTime(dur * 48));
        }
        if (st != QLatin1String("playing") && st != QLatin1String("cued") && st != QLatin1String("pending")) continue;
        if (next_->rowCount() >= 30) continue;
        const int r = next_->rowCount();
        next_->insertRow(r);
        next_->setItem(r, 0, new QTableWidgetItem(o.value("start").toString()));
        next_->setItem(r, 1, new QTableWidgetItem(title.isEmpty() ? TR("(ismeretlen elem)") : title));
        next_->setItem(r, 2, new QTableWidgetItem(st == QLatin1String("playing") ? TR("ADÁSBAN") : st == QLatin1String("cued") ? TR("KÉSZENLÉT") : QString()));
    }
    now_->setText(nowText);
    info_->setText(hasReplica() ? TR("Az adatbázis másolata: %1 · %2 elem · %3").arg(snapshotAt_).arg(snapshotItems_).arg(QDir::toNativeSeparators(replica_))
                                : TR("Még nincs másolat a fő playout adatbázisáról – az első csatlakozáskor érkezik."));
    takeBtn_->setEnabled(hasReplica() && !takenOver_);
}

void BackupWindow::check()
{
    refresh();
    // Átvétel: ha a fő géptől a megadott ideje nem jött jel (kapcsolat nélkül, vagy él a kapcsolat, de lefagyott).
    if (takenOver_ || !cfg_.autoTakeover || !everConnected_ || bye_ || !hasReplica()) return;
    if (stateAgeMs() < qint64(cfg_.timeoutS) * 1000) return;
    // Csak akkor, ha a fő gépen szólt valami: ha ott épp csend volt (leállított adás), itt sem indul semmi.
    if (!playingEntry(nullptr)) {
        if (!silentNoted_) statusBar()->showMessage(TR("A fő playout nem érhető el, de nem is szólt rajta semmi – nincs automatikus átvétel."), 0);
        silentNoted_ = true;
        return;
    }
    takeOverNow(false);
}

void BackupWindow::takeOverNow(bool ask)
{
    if (takenOver_ || !hasReplica()) return;
    if (ask) {
        const QString q = online_ ? TR("A fő playout még elérhető, és ott is szólhat az adás. Biztosan átveszed?")
                                  : TR("Átveszed az adást a fő playouttól?");
        if (QMessageBox::question(this, TR("Átvétel"), q, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    }
    takenOver_ = true;
    std::printf("tartalék: átvétel (utolsó jel %lld ms)\n", static_cast<long long>(stateAgeMs()));
    std::fflush(stdout);
    emit takeoverRequested();
}

QString BackupWindow::debugText() const { return banner_->text(); }

void BackupWindow::openSettings()
{
    QDialog dlg(this);
    dlg.setWindowTitle(TR("Tartalék playout beállításai"));
    dlg.setMinimumWidth(520);
    auto *v = new QVBoxLayout(&dlg);
    auto *intro = new QLabel(TR("A tartalék a fő playout gépre csatlakozik (ott a Beállítások → Hálózat részben kell bekapcsolni "
                                "a távoli szerkesztést, és a felhasználónak „tartalék playout” jog kell). A fő gép teljes "
                                "adatbázisáról másolatot tart, és ha a fő gép eltűnik, átveszi az adást."));
    intro->setWordWrap(true);
    intro->setObjectName("small");
    v->addWidget(intro);
    auto *f = new QFormLayout;
    auto *host = new QLineEdit(cfg_.host);
    host->setPlaceholderText(TR("például 192.168.1.20"));
    auto *port = new QSpinBox;
    port->setRange(1, 65535);
    port->setValue(cfg_.port);
    auto *user = new QLineEdit(cfg_.user);
    user->setPlaceholderText(TR("üresen: az alap jelszó"));
    auto *pass = new QLineEdit;
    pass->setEchoMode(QLineEdit::Password);
    pass->setPlaceholderText(cfg_.key.isEmpty() ? QString() : TR("üresen: a mentett belépés marad"));
    auto *from = new QLineEdit(cfg_.pathFrom);
    from->setPlaceholderText(TR("például D:\\Zene vagy /srv/zene"));
    auto *to = new QLineEdit(cfg_.pathTo);
    to->setPlaceholderText(TR("például smb://radio/Zene, \\\\radio\\Zene vagy /mnt/zene"));
    auto *mode = new QComboBox;
    mode->addItem(TR("automatikusan, ha a fő gép eltűnik"), true);
    mode->addItem(TR("csak gombnyomásra"), false);
    mode->setCurrentIndex(cfg_.autoTakeover ? 0 : 1);
    auto *timeout = new QSpinBox;
    timeout->setRange(3, 300);
    timeout->setSuffix(TR(" mp"));
    timeout->setValue(cfg_.timeoutS);
    f->addRow(TR("Fő playout címe (IP)"), host);
    f->addRow(TR("Port"), port);
    f->addRow(TR("Felhasználó"), user);
    f->addRow(TR("Jelszó"), pass);
    f->addRow(TR("Hangfájlok a fő gépen"), from);
    f->addRow(TR("Ugyanez ezen a gépen"), to);
    f->addRow(TR("Átvétel"), mode);
    f->addRow(TR("Ennyi csend után"), timeout);
    v->addLayout(f);
    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText(TR("Mentés és csatlakozás"));
    bb->button(QDialogButtonBox::Cancel)->setText(TR("Mégse"));
    v->addWidget(bb);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(bb, &QDialogButtonBox::accepted, &dlg, [&] {
        if (host->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, TR("Tartalék playout"), TR("Add meg a fő playout gép címét."));
            return;
        }
        if (pass->text().isEmpty() && (cfg_.key.isEmpty() || user->text().trimmed() != cfg_.user || host->text().trimmed() != cfg_.host)) {
            QMessageBox::warning(&dlg, TR("Tartalék playout"), TR("Add meg a jelszót (csak a belépés kulcsa marad meg, a jelszó nem)."));
            return;
        }
        dlg.accept();
    });
    if (dlg.exec() != QDialog::Accepted) return;
    const bool newLogin = !pass->text().isEmpty();
    cfg_.host = host->text().trimmed();
    cfg_.port = quint16(port->value());
    cfg_.user = user->text().trimmed();
    cfg_.pathFrom = from->text().trimmed();
    cfg_.pathTo = to->text().trimmed();
    cfg_.autoTakeover = mode->currentData().toBool();
    cfg_.timeoutS = timeout->value();
    if (newLogin) {
        cfg_.salt.clear();
        cfg_.key.clear();
    }
    saveConfig(cfg_);
    everConnected_ = false;
    online_ = false;
    stop();
    start(pass->text());
}

bool BackupWindow::applyLiveState(Database &db, const QJsonObject &state)
{
    // Ugyanaz az átalakítás, mint a távoli szerkesztőben: a fő gép élő playlistje a másolat playlistje lesz.
    QSet<int> items;
    for (const Item &it : db.items()) items.insert(it.id);
    QVector<Database::MirrorRow> rows;
    QSet<int> kept;
    for (const QJsonValue &v : state.value("rows").toArray()) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.id = o.value("id").toInt();
        e.playlistId = o.value("pl").toInt();
        e.playlistKey = o.value("key").toInt();
        e.source = o.value("src").toString();
        e.item.id = o.value("item").toInt();
        if (!items.contains(e.item.id)) continue;       // a másolat óta került a hangtárba: ez az elem kimarad
        if (o.contains("fixed")) {
            const QDateTime at = QDateTime::fromString(o.value("fixed").toString(), Qt::ISODate);
            if (at.isValid()) e.fixedStartS = at.time().minute() * 60 + at.time().second();
        }
        e.groupId = o.value("group").toInt();
        e.groupName = o.value("groupName").toString();
        if (o.contains("ovl")) {
            const QJsonObject ov = o.value("ovl").toObject();
            e.overlay = true;
            e.overlayOffsetMs = ov.value("offset").toInteger();
            e.overlayDuckDb = ov.value("duck").toDouble();
        }
        const QString st = o.value("state").toString();
        const QString status = (st == QLatin1String("pending") || st == QLatin1String("cued") || st == QLatin1String("playing"))
                                   ? QStringLiteral("pending") : st == QLatin1String("played") ? QStringLiteral("played") : QStringLiteral("skipped");
        rows.push_back({e, status});
        kept.insert(e.id);
    }
    QVector<Transition> trs;
    for (const QJsonValue &v : state.value("transitions").toArray()) {
        const Transition t = remote::transitionFromJson(v.toObject());
        if (kept.contains(t.toEntryId) && kept.contains(t.fromEntryId)) trs.push_back(t);
    }
    return db.mirrorState(rows, trs);
}
