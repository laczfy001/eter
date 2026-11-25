#include "ServerSync.h"
#include "i18n.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostInfo>
#include <QAbstractButton>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVariant>
#include <algorithm>
#include <atomic>
#include <utility>
#include <cstdio>

namespace sqlsrv {

// ---- Beállítások ---------------------------------------------------------------------------------------------------

Config Config::load()
{
    QSettings s;
    Config c;
    c.enabled = s.value(QStringLiteral("sql/enabled"), false).toBool();
    c.host = s.value(QStringLiteral("sql/host")).toString();
    c.port = s.value(QStringLiteral("sql/port"), 3306).toInt();
    c.user = s.value(QStringLiteral("sql/user")).toString();
    c.password = s.value(QStringLiteral("sql/password")).toString();
    c.database = s.value(QStringLiteral("sql/database")).toString();
    return c;
}

void Config::save() const
{
    QSettings s;
    s.setValue(QStringLiteral("sql/enabled"), enabled);
    s.setValue(QStringLiteral("sql/host"), host.trimmed());
    s.setValue(QStringLiteral("sql/port"), port);
    s.setValue(QStringLiteral("sql/user"), user);
    s.setValue(QStringLiteral("sql/password"), password);
    s.setValue(QStringLiteral("sql/database"), database.trimmed());
}

QString clientId()
{
    QSettings s;
    QString id = s.value(QStringLiteral("sql/clientId")).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        s.setValue(QStringLiteral("sql/clientId"), id);
    }
    return id;
}

QString clientName()
{
    const QString h = QHostInfo::localHostName();
    return h.isEmpty() ? QStringLiteral("?") : h;
}

static std::atomic<bool> gForceUpload{false};
bool forceUploadRequested() { return gForceUpload.load(); }

// ---- Segédek -------------------------------------------------------------------------------------------------------

static QString bq(const QString &name) { return QLatin1Char('`') + QString(name).remove(QLatin1Char('`')) + QLatin1Char('`'); }
static QString srvTable(const QString &t) { return bq(QStringLiteral("eter_") + t); }
static QString sqlErr(const QSqlQuery &q) { return q.lastError().text().trimmed(); }

struct Col {
    QString name, type;
    int pk = 0;
};

struct Table {
    QString name;
    QVector<Col> cols;
    QStringList pk;                 // a kulcs oszlopai sorrendben (legfeljebb 3)
};

// A szinkronizált helyi táblák: minden, kivéve az SQLite belső tábláit, a szinkron saját tábláit és a hullámforma-
// gyorsítótárat (az a hangfájlokból újra elkészül).
static QVector<Table> localTables(QSqlDatabase &db)
{
    QVector<Table> out;
    QSqlQuery q(db);
    q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' "
                          "AND substr(name, 1, 1) <> '_' AND name <> 'waveforms' ORDER BY name"));
    QStringList names;
    while (q.next()) names << q.value(0).toString();
    for (const QString &n : names) {
        Table t;
        t.name = n;
        QSqlQuery c(db);
        c.exec(QStringLiteral("PRAGMA table_info(%1)").arg(n));
        QVector<QPair<int, QString>> pk;
        while (c.next()) {
            Col col;
            col.name = c.value(1).toString();
            col.type = c.value(2).toString().toUpper();
            col.pk = c.value(5).toInt();
            if (col.pk > 0) pk.push_back({col.pk, col.name});
            t.cols.push_back(col);
        }
        std::sort(pk.begin(), pk.end());
        for (const auto &p : pk) t.pk << p.second;
        if (t.pk.isEmpty() || t.pk.size() > 3 || t.cols.isEmpty()) continue;   // ilyen nincs; biztonság kedvéért
        out.push_back(t);
    }
    return out;
}

static QString mysqlType(const Col &c)
{
    const QString &t = c.type;
    if (t.contains(QLatin1String("INT"))) return QStringLiteral("BIGINT");
    if (t.contains(QLatin1String("REAL")) || t.contains(QLatin1String("FLOA")) || t.contains(QLatin1String("DOUB")))
        return QStringLiteral("DOUBLE");
    if (t.contains(QLatin1String("BLOB"))) return QStringLiteral("LONGBLOB");
    return c.pk > 0 ? QStringLiteral("VARCHAR(191) COLLATE utf8mb4_bin") : QStringLiteral("LONGTEXT");
}

static QSqlDatabase openLocal(const QString &path, const QString &name, QString *err)
{
    if (QSqlDatabase::contains(name)) QSqlDatabase::removeDatabase(name);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
    db.setDatabaseName(path);
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=8000"));
    if (!db.open() && err) *err = db.lastError().text();
    return db;
}

static void closeConnection(const QString &name)
{
    {
        QSqlDatabase db = QSqlDatabase::database(name, false);
        if (db.isValid()) db.close();
    }
    QSqlDatabase::removeDatabase(name);
}

QString openServer(const Config &cfg, const QString &name)
{
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QMYSQL")))
        return TR("A MySQL/MariaDB illesztő (Qt QMYSQL) nincs telepítve ezen a gépen.");
    if (QSqlDatabase::contains(name)) QSqlDatabase::removeDatabase(name);
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QMYSQL"), name);
    db.setHostName(cfg.host.trimmed());
    db.setPort(cfg.port);
    db.setUserName(cfg.user);
    db.setPassword(cfg.password);
    db.setDatabaseName(cfg.database.trimmed());
    db.setConnectOptions(QStringLiteral("MYSQL_OPT_CONNECT_TIMEOUT=5;MYSQL_OPT_READ_TIMEOUT=30;MYSQL_OPT_WRITE_TIMEOUT=30"));
    if (!db.open()) {
        const QString e = db.lastError().text().trimmed();
        return e.isEmpty() ? TR("A kapcsolat nem jött létre.") : e;
    }
    QSqlQuery q(db);
    q.exec(QStringLiteral("SET NAMES utf8mb4"));
    q.exec(QStringLiteral("SET SESSION sql_mode = 'STRICT_ALL_TABLES,NO_ENGINE_SUBSTITUTION'"));
    return {};
}

static bool ensureMeta(QSqlDatabase &srv, QString *err)
{
    QSqlQuery q(srv);
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS `eter_meta` (`k` VARCHAR(64) NOT NULL PRIMARY KEY, `v` LONGTEXT) "
                               "ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci"))) {
        if (err) *err = sqlErr(q);
        return false;
    }
    return true;
}

static QHash<QString, QString> readMeta(QSqlDatabase &srv)
{
    QHash<QString, QString> m;
    QSqlQuery q(srv);
    if (q.exec(QStringLiteral("SELECT `k`, `v` FROM `eter_meta`")))
        while (q.next()) m.insert(q.value(0).toString(), q.value(1).toString());
    return m;
}

static bool setMeta(QSqlDatabase &srv, const QString &k, const QString &v)
{
    QSqlQuery q(srv);
    q.prepare(QStringLiteral("INSERT INTO `eter_meta` (`k`, `v`) VALUES (?, ?) ON DUPLICATE KEY UPDATE `v` = VALUES(`v`)"));
    q.addBindValue(k);
    q.addBindValue(v);
    return q.exec();
}

// A szerver táblája a helyi tábla oszlopaival (hiányzó oszlop: hozzáadja). Idegen kulcs és egyediség nincs rajta:
// a szerver a helyi adatbázis tükre, a kulcs szerint.
static bool ensureServerTable(QSqlDatabase &srv, const Table &t, QString *err)
{
    QSqlQuery q(srv);
    QStringList defs;
    for (const Col &c : t.cols) defs << bq(c.name) + QLatin1Char(' ') + mysqlType(c) + (c.pk > 0 ? QStringLiteral(" NOT NULL") : QString());
    QStringList pk;
    for (const QString &p : t.pk) pk << bq(p);
    defs << QStringLiteral("PRIMARY KEY (%1)").arg(pk.join(QStringLiteral(", ")));
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS %1 (%2) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci")
                    .arg(srvTable(t.name), defs.join(QStringLiteral(", "))))) {
        if (err) *err = QStringLiteral("%1: %2").arg(t.name, sqlErr(q));
        return false;
    }
    QSet<QString> have;
    if (q.exec(QStringLiteral("SHOW COLUMNS FROM %1").arg(srvTable(t.name))))
        while (q.next()) have.insert(q.value(0).toString());
    for (const Col &c : t.cols) {
        if (have.contains(c.name)) continue;
        if (!q.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3").arg(srvTable(t.name), bq(c.name), mysqlType(c)))) {
            if (err) *err = QStringLiteral("%1.%2: %3").arg(t.name, c.name, sqlErr(q));
            return false;
        }
    }
    return true;
}

static int userVersion(QSqlDatabase &db)
{
    QSqlQuery q(db);
    return q.exec(QStringLiteral("PRAGMA user_version")) && q.next() ? q.value(0).toInt() : 0;
}

static void setLocalState(QSqlDatabase &db, const QString &k, const QVariant &v)
{
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO _sync_state (k, v) VALUES (?, ?) ON CONFLICT(k) DO UPDATE SET v = excluded.v"));
    q.addBindValue(k);
    q.addBindValue(v);
    q.exec();
}

static QString serverKey(const Config &cfg)
{
    return QStringLiteral("%1:%2/%3").arg(cfg.host.trimmed().toLower()).arg(cfg.port).arg(cfg.database.trimmed());
}

// ---- Triggerek ------------------------------------------------------------------------------------------------------

static bool ensureLocalTables(QSqlDatabase &db, QString *err)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS _sync_log (id INTEGER PRIMARY KEY, tbl TEXT NOT NULL, k1, k2, k3)"))
        || !q.exec(QStringLiteral("CREATE TABLE IF NOT EXISTS _sync_state (k TEXT PRIMARY KEY, v)"))) {
        if (err) *err = sqlErr(q);
        return false;
    }
    return true;
}

bool installTriggers(QSqlDatabase &db, QString *err)
{
    if (!ensureLocalTables(db, err)) return false;
    QSqlQuery q(db);
    for (const Table &t : localTables(db)) {
        auto keys = [&](const char *row) {
            QStringList k;
            for (int i = 0; i < 3; ++i) k << (i < t.pk.size() ? QStringLiteral("%1.\"%2\"").arg(QLatin1String(row), t.pk[i]) : QStringLiteral("NULL"));
            return k.join(QStringLiteral(", "));
        };
        QStringList same;
        for (const QString &p : t.pk) same << QStringLiteral("OLD.\"%1\" IS NEW.\"%1\"").arg(p);
        const QString ins = QStringLiteral("INSERT INTO _sync_log (tbl, k1, k2, k3) VALUES ('%1', %2);").arg(t.name);
        const QStringList sql{
            QStringLiteral("DROP TRIGGER IF EXISTS _sync_%1_ai").arg(t.name),
            QStringLiteral("DROP TRIGGER IF EXISTS _sync_%1_au").arg(t.name),
            QStringLiteral("DROP TRIGGER IF EXISTS _sync_%1_ad").arg(t.name),
            QStringLiteral("CREATE TRIGGER _sync_%1_ai AFTER INSERT ON \"%1\" BEGIN %2 END").arg(t.name, ins.arg(keys("NEW"))),
            // Módosításkor az új kulcs; ha a kulcs is változott, a régi is (az onnan törlődik a szerveren).
            QStringLiteral("CREATE TRIGGER _sync_%1_au AFTER UPDATE ON \"%1\" BEGIN %2 "
                           "INSERT INTO _sync_log (tbl, k1, k2, k3) SELECT '%1', %3 WHERE NOT (%4); END")
                .arg(t.name, ins.arg(keys("NEW")), keys("OLD"), same.join(QStringLiteral(" AND "))),
            QStringLiteral("CREATE TRIGGER _sync_%1_ad AFTER DELETE ON \"%1\" BEGIN %2 END").arg(t.name, ins.arg(keys("OLD"))),
        };
        for (const QString &s : sql)
            if (!q.exec(s)) {
                if (err) *err = QStringLiteral("%1: %2").arg(t.name, sqlErr(q));
                return false;
            }
    }
    return true;
}

void removeTriggers(QSqlDatabase &db)
{
    QSqlQuery q(db);
    QStringList names;
    if (q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'trigger' AND name LIKE '\\_sync\\_%' ESCAPE '\\'")))
        while (q.next()) names << q.value(0).toString();
    for (const QString &n : names) q.exec(QStringLiteral("DROP TRIGGER IF EXISTS \"%1\"").arg(n));
    q.exec(QStringLiteral("DROP TABLE IF EXISTS _sync_log"));
}

// ---- Állapot --------------------------------------------------------------------------------------------------------

ServerInfo probe(const Config &cfg)
{
    ServerInfo info;
    const QString name = QStringLiteral("eter_sql_probe");
    {
        const QString e = openServer(cfg, name);
        if (!e.isEmpty()) {
            info.error = e;
            closeConnection(name);
            return info;
        }
        QSqlDatabase srv = QSqlDatabase::database(name);
        info.ok = true;
        QSqlQuery q(srv);
        if (q.exec(QStringLiteral("SELECT VERSION()")) && q.next()) info.version = q.value(0).toString();
        if (q.exec(QStringLiteral("SHOW TABLES LIKE 'eter\\_meta'")) && q.next()) {
            const QHash<QString, QString> m = readMeta(srv);
            info.hasData = m.contains(QStringLiteral("generation"));
            info.schema = m.value(QStringLiteral("schema")).toInt();
            info.generation = m.value(QStringLiteral("generation")).toLongLong();
            info.writerId = m.value(QStringLiteral("writer_id"));
            info.writerName = m.value(QStringLiteral("writer_name"));
            info.heartbeat = m.value(QStringLiteral("heartbeat")).toLongLong();
            if (q.exec(QStringLiteral("SELECT COUNT(*) FROM `eter_items`")) && q.next()) info.items = q.value(0).toLongLong();
        }
    }
    closeConnection(name);
    return info;
}

LocalState localState(const QString &dbPath)
{
    LocalState st;
    if (!QFile::exists(dbPath)) return st;
    const QString name = QStringLiteral("eter_sql_state");
    {
        QSqlDatabase db = openLocal(dbPath, name, nullptr);
        if (db.isOpen()) {
            st.schema = userVersion(db);
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("SELECT v FROM _sync_state WHERE k = 'generation'")) && q.next()) st.generation = q.value(0).toLongLong();
            if (q.exec(QStringLiteral("SELECT COUNT(*) FROM _sync_log")) && q.next()) st.pending = q.value(0).toLongLong();
            // Ha ez az adatbázis egy másik szerverhez tartozott, a generáció nem összevethető.
            if (q.exec(QStringLiteral("SELECT v FROM _sync_state WHERE k = 'server'")) && q.next()
                && q.value(0).toString() != serverKey(Config::load()))
                st.generation = -1;
        }
    }
    closeConnection(name);
    return st;
}

// ---- Teljes feltöltés -----------------------------------------------------------------------------------------------

bool upload(const Config &cfg, const QString &dbPath, const Progress &progress, QString *err)
{
    const QString sName = QStringLiteral("eter_sql_up_srv"), lName = QStringLiteral("eter_sql_up_loc");
    bool ok = false;
    {
        QString e = openServer(cfg, sName);
        QSqlDatabase local = openLocal(dbPath, lName, &e);
        QSqlDatabase srv = QSqlDatabase::database(sName, false);
        auto fail = [&](const QString &m) {
            if (err) *err = m;
            return false;
        };
        ok = [&]() -> bool {
            if (!e.isEmpty()) return fail(e);
            if (!installTriggers(local, &e)) return fail(e);
            QSqlQuery lq(local);
            qint64 maxLog = 0;
            if (lq.exec(QStringLiteral("SELECT COALESCE(MAX(id), 0) FROM _sync_log")) && lq.next()) maxLog = lq.value(0).toLongLong();
            const QVector<Table> tables = localTables(local);
            if (!ensureMeta(srv, &e)) return fail(e);
            for (const Table &t : tables)
                if (!ensureServerTable(srv, t, &e)) return fail(e);
            // Egységes pillanatkép a helyi adatbázisról, egy tranzakció a szerveren.
            lq.exec(QStringLiteral("BEGIN"));
            qint64 total = 0, done = 0;
            QHash<QString, qint64> counts;
            for (const Table &t : tables)
                if (lq.exec(QStringLiteral("SELECT COUNT(*) FROM \"%1\"").arg(t.name)) && lq.next()) {
                    counts[t.name] = lq.value(0).toLongLong();
                    total += counts[t.name];
                }
            if (!srv.transaction()) {
                lq.exec(QStringLiteral("ROLLBACK"));
                return fail(srv.lastError().text());
            }
            QSqlQuery sq(srv);
            auto abort = [&](const QString &m) {
                srv.rollback();
                lq.exec(QStringLiteral("ROLLBACK"));
                return fail(m);
            };
            for (const Table &t : tables) {
                if (!sq.exec(QStringLiteral("DELETE FROM %1").arg(srvTable(t.name)))) return abort(sqlErr(sq));
                QStringList cols, ph;
                for (const Col &c : t.cols) {
                    cols << bq(c.name);
                    ph << QStringLiteral("?");
                }
                const QString rowPh = QStringLiteral("(%1)").arg(ph.join(QLatin1Char(',')));
                const int per = std::clamp(20000 / int(t.cols.size()), 1, 300);
                QStringList sel;
                for (const Col &c : t.cols) sel << QStringLiteral("\"%1\"").arg(c.name);
                QSqlQuery rq(local);
                rq.setForwardOnly(true);
                if (!rq.exec(QStringLiteral("SELECT %1 FROM \"%2\"").arg(sel.join(QLatin1Char(',')), t.name))) return abort(sqlErr(rq));
                QVector<QVariantList> batch;
                auto flush = [&]() -> bool {
                    if (batch.isEmpty()) return true;
                    QStringList rows;
                    for (int i = 0; i < batch.size(); ++i) rows << rowPh;
                    QSqlQuery ins(srv);
                    ins.prepare(QStringLiteral("INSERT INTO %1 (%2) VALUES %3").arg(srvTable(t.name), cols.join(QLatin1Char(',')), rows.join(QLatin1Char(','))));
                    for (const QVariantList &r : std::as_const(batch))
                        for (const QVariant &v : r) ins.addBindValue(v);
                    if (!ins.exec()) {
                        e = QStringLiteral("%1: %2").arg(t.name, sqlErr(ins));
                        return false;
                    }
                    done += batch.size();
                    batch.clear();
                    return !progress || progress(total ? int(done * 100 / total) : 100, t.name);
                };
                while (rq.next()) {
                    QVariantList r;
                    r.reserve(t.cols.size());
                    for (int i = 0; i < t.cols.size(); ++i) r << rq.value(i);
                    batch << r;
                    if (batch.size() >= per && !flush()) return abort(e.isEmpty() ? TR("Megszakítva.") : e);
                }
                if (!flush()) return abort(e.isEmpty() ? TR("Megszakítva.") : e);
            }
            const QHash<QString, QString> m = readMeta(srv);
            const qint64 gen = m.value(QStringLiteral("generation")).toLongLong() + 1;
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            bool mok = setMeta(srv, QStringLiteral("schema"), QString::number(userVersion(local)))
                       && setMeta(srv, QStringLiteral("generation"), QString::number(gen))
                       && setMeta(srv, QStringLiteral("writer_id"), clientId())
                       && setMeta(srv, QStringLiteral("writer_name"), clientName())
                       && setMeta(srv, QStringLiteral("heartbeat"), QString::number(now))
                       && setMeta(srv, QStringLiteral("updated_at"), QString::number(now));
            if (!mok || !srv.commit()) return abort(srv.lastError().text());
            lq.exec(QStringLiteral("COMMIT"));
            lq.prepare(QStringLiteral("DELETE FROM _sync_log WHERE id <= ?"));
            lq.addBindValue(maxLog);
            lq.exec();
            setLocalState(local, QStringLiteral("generation"), gen);
            setLocalState(local, QStringLiteral("server"), serverKey(cfg));
            if (progress) progress(100, QString());
            return true;
        }();
    }
    closeConnection(sName);
    closeConnection(lName);
    return ok;
}

// ---- Letöltés -------------------------------------------------------------------------------------------------------

bool download(const Config &cfg, const QString &dbPath, const QString &target, const Progress &progress, QString *err)
{
    const QString sName = QStringLiteral("eter_sql_down_srv"), lName = QStringLiteral("eter_sql_down_loc"),
                  tName = QStringLiteral("eter_sql_down_tgt");
    bool ok = false;
    QFile::remove(target);
    {
        QString e = openServer(cfg, sName);
        QSqlDatabase srv = QSqlDatabase::database(sName, false);
        auto fail = [&](const QString &m) {
            if (err) *err = m;
            return false;
        };
        ok = [&]() -> bool {
            if (!e.isEmpty()) return fail(e);
            QSqlQuery sq(srv);
            if (!sq.exec(QStringLiteral("SHOW TABLES LIKE 'eter\\_meta'")) || !sq.next()) return fail(TR("A szerveren még nincs Éter-adatbázis."));
            const QHash<QString, QString> m = readMeta(srv);
            if (!m.contains(QStringLiteral("generation"))) return fail(TR("A szerveren még nincs Éter-adatbázis."));
            // A helyi adatbázis szerkezete (a séma) a minta: róla készül másolat, és abba kerülnek a szerver sorai.
            {
                QSqlDatabase local = openLocal(dbPath, lName, &e);
                if (!e.isEmpty()) return fail(e);
                const int localSchema = userVersion(local);
                if (m.value(QStringLiteral("schema")).toInt() > localSchema)
                    return fail(TR("A szerveren lévő adatbázist az Éter újabb változata töltötte fel (séma: %1, ez a program: %2). "
                                   "Frissítsd az Étert.").arg(m.value(QStringLiteral("schema")).toInt()).arg(localSchema));
                QSqlQuery lq(local);
                QString quoted = target;
                quoted.replace(QLatin1Char('\''), QStringLiteral("''"));
                if (!lq.exec(QStringLiteral("VACUUM INTO '%1'").arg(quoted))) return fail(sqlErr(lq));
            }
            closeConnection(lName);
            QSqlDatabase tgt = openLocal(target, tName, &e);
            if (!e.isEmpty()) return fail(e);
            removeTriggers(tgt);
            QSqlQuery tq(tgt);
            tq.exec(QStringLiteral("PRAGMA foreign_keys = OFF"));
            const QVector<Table> tables = localTables(tgt);
            qint64 total = 0, done = 0;
            QSet<QString> serverTables;
            if (sq.exec(QStringLiteral("SHOW TABLES LIKE 'eter\\_%'")))
                while (sq.next()) serverTables.insert(sq.value(0).toString());
            for (const Table &t : tables)
                if (serverTables.contains(QStringLiteral("eter_") + t.name) && sq.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(srvTable(t.name))) && sq.next())
                    total += sq.value(0).toLongLong();
            if (!tgt.transaction()) return fail(tgt.lastError().text());
            for (const Table &t : tables) {
                if (!serverTables.contains(QStringLiteral("eter_") + t.name)) continue;   // régebbi szerverséma: a helyi marad
                QSet<QString> srvCols;
                if (sq.exec(QStringLiteral("SHOW COLUMNS FROM %1").arg(srvTable(t.name))))
                    while (sq.next()) srvCols.insert(sq.value(0).toString());
                QStringList cols, sel, ph;
                QVector<bool> textCol;      // a bináris illesztésű szövegoszlop QByteArray-ként jön: szöveggé alakítjuk
                for (const Col &c : t.cols)
                    if (srvCols.contains(c.name)) {
                        textCol << !(c.type.contains(QLatin1String("INT")) || c.type.contains(QLatin1String("REAL"))
                                     || c.type.contains(QLatin1String("FLOA")) || c.type.contains(QLatin1String("DOUB"))
                                     || c.type.contains(QLatin1String("BLOB")));
                        cols << QStringLiteral("\"%1\"").arg(c.name);
                        sel << bq(c.name);
                        ph << QStringLiteral("?");
                    }
                if (!tq.exec(QStringLiteral("DELETE FROM \"%1\"").arg(t.name))) {
                    tgt.rollback();
                    return fail(QStringLiteral("%1: %2").arg(t.name, sqlErr(tq)));
                }
                QSqlQuery rows(srv);
                rows.setForwardOnly(true);
                if (!rows.exec(QStringLiteral("SELECT %1 FROM %2").arg(sel.join(QLatin1Char(',')), srvTable(t.name)))) {
                    tgt.rollback();
                    return fail(QStringLiteral("%1: %2").arg(t.name, sqlErr(rows)));
                }
                QSqlQuery ins(tgt);
                ins.prepare(QStringLiteral("INSERT INTO \"%1\" (%2) VALUES (%3)").arg(t.name, cols.join(QLatin1Char(',')), ph.join(QLatin1Char(','))));
                while (rows.next()) {
                    for (int i = 0; i < cols.size(); ++i) {
                        const QVariant v = rows.value(i);
                        ins.bindValue(i, textCol[i] && !v.isNull() && v.metaType().id() == QMetaType::QByteArray
                                             ? QVariant(QString::fromUtf8(v.toByteArray())) : v);
                    }
                    if (!ins.exec()) {
                        const QString m2 = QStringLiteral("%1: %2").arg(t.name, sqlErr(ins));
                        tgt.rollback();
                        return fail(m2);
                    }
                    if (++done % 2000 == 0 && progress && !progress(total ? int(done * 100 / total) : 100, t.name)) {
                        tgt.rollback();
                        return fail(TR("Megszakítva."));
                    }
                }
            }
            tq.exec(QStringLiteral("DELETE FROM waveforms"));   // az elemazonosítók mások lehetnek: újra elkészül
            if (!ensureLocalTables(tgt, &e)) {
                tgt.rollback();
                return fail(e);
            }
            tq.exec(QStringLiteral("DELETE FROM _sync_log"));
            setLocalState(tgt, QStringLiteral("generation"), m.value(QStringLiteral("generation")).toLongLong());
            setLocalState(tgt, QStringLiteral("server"), serverKey(cfg));
            if (!tgt.commit()) return fail(tgt.lastError().text());
            if (!installTriggers(tgt, &e)) return fail(e);
            if (progress) progress(100, QString());
            return true;
        }();
    }
    closeConnection(sName);
    closeConnection(lName);
    closeConnection(tName);
    if (!ok) QFile::remove(target);
    return ok;
}

// ---- Indulás --------------------------------------------------------------------------------------------------------

// Üres, friss adatbázis a program sémájával (új gépen, a letöltés mintájának).
static bool createFresh(const QString &path, QString *err)
{
    QFile f(QStringLiteral(":/schema.sql"));
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = TR("A séma nem olvasható");
        return false;
    }
    QString sql;
    for (const QString &line : QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'))) {
        const int c = line.indexOf(QLatin1String("--"));
        sql += (c >= 0 ? line.left(c) : line) + QLatin1Char('\n');
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    const QString name = QStringLiteral("eter_sql_fresh");
    bool ok = true;
    {
        QSqlDatabase db = openLocal(path, name, err);
        QSqlQuery q(db);
        for (const QString &stmt : sql.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QString st = stmt.trimmed();
            if (st.isEmpty()) continue;
            if (!q.exec(st)) {
                if (err) *err = sqlErr(q);
                ok = false;
                break;
            }
        }
    }
    closeConnection(name);
    return ok;
}

bool startupCheck(const QString &dbPath, bool interactive)
{
    const Config cfg = Config::load();
    if (!cfg.enabled || !cfg.complete()) return false;
    const ServerInfo info = probe(cfg);
    if (!info.ok) {
        std::fprintf(stderr, "SQL szerver nem érhető el (%s): a változások helyben gyűlnek.\n", qPrintable(info.error));
        return true;
    }
    if (!info.hasData) return true;              // üres szerver: a háttérszál feltölti
    if (!QFile::exists(dbPath) || localState(dbPath).schema == 0) {
        // Új gép: előbb egy üres adatbázis, az lesz a letöltés mintája.
        QFile::remove(dbPath);
        QString e;
        if (!createFresh(dbPath, &e)) std::fprintf(stderr, "SQL szerver: üres adatbázis nem készíthető: %s\n", qPrintable(e));
    }
    const LocalState ls = localState(dbPath);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (info.writerId != clientId() && info.writerId.size() && now - info.heartbeat < 90 && interactive) {
        if (QMessageBox::question(nullptr, QStringLiteral("Éter"),
                                  TR("Az SQL szervert most is a(z) „%1” gép használja (%2 másodperce jelentkezett).\n\n"
                                     "Ha ez a gép veszi át, a másik gép változásai onnantól csak nála maradnak.\n\nÁtveszed?")
                                      .arg(info.writerName).arg(now - info.heartbeat),
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            std::fprintf(stderr, "SQL szerver: ebben a futásban csak a helyi adatbázis.\n");
            return false;
        }
    }
    if (info.generation == ls.generation) return true;
    if (ls.generation >= 0 && ls.pending > 0) {
        bool useServer = true;
        if (interactive) {
            QMessageBox box(QMessageBox::Question, QStringLiteral("Éter"),
                            TR("A szerveren újabb adat van (feltöltötte: %1), és ezen a gépen is vannak még fel nem töltött "
                               "változások (%2).\n\nMelyik maradjon meg?").arg(info.writerName).arg(ls.pending));
            auto *srvBtn = box.addButton(TR("A szerver adatai"), QMessageBox::AcceptRole);
            box.addButton(TR("Ennek a gépnek az adatai"), QMessageBox::RejectRole);
            box.setDefaultButton(srvBtn);
            box.exec();
            useServer = box.clickedButton() == static_cast<QAbstractButton *>(srvBtn);
        }
        if (!useServer) {
            gForceUpload = true;
            return true;
        }
    }
    // A szerver adatai töltődnek le; a mostani adatbázisról biztonsági mentés készül.
    const QString backupDir = QFileInfo(dbPath).absolutePath() + QStringLiteral("/biztonsagi-mentesek");
    QDir().mkpath(backupDir);
    if (QFile::exists(dbPath)) {
        const QString backup = QDir(backupDir).filePath(QStringLiteral("library-szerver-letoltes-elott-%1.sqlite")
                                                            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmmss"))));
        const QString name = QStringLiteral("eter_sql_backup");
        {
            QSqlDatabase db = openLocal(dbPath, name, nullptr);
            QString quoted = backup;
            quoted.replace(QLatin1Char('\''), QStringLiteral("''"));
            QSqlQuery(db).exec(QStringLiteral("VACUUM INTO '%1'").arg(quoted));
        }
        closeConnection(name);
    }
    QString err;
    const QString tmp = dbPath + QStringLiteral(".sqlsrv");
    if (!download(cfg, dbPath, tmp, nullptr, &err)) {
        std::fprintf(stderr, "SQL szerver: a letöltés nem sikerült: %s\n", qPrintable(err));
        if (interactive)
            QMessageBox::warning(nullptr, QStringLiteral("Éter"),
                                 TR("A szerver adatainak letöltése nem sikerült:\n%1\n\nA program a helyi adatbázissal indul, "
                                    "és ebben a futásban nem ír a szerverre.").arg(err));
        return false;
    }
    const QString pending = dbPath + QStringLiteral(".import");
    QFile::remove(pending);
    if (!QFile::rename(tmp, pending)) {
        QFile::remove(tmp);
        return false;
    }
    std::fprintf(stderr, "SQL szerver: a szerver adatai letöltve (%lld. változat, %s).\n",
                 static_cast<long long>(info.generation), qPrintable(info.writerName));
    return true;
}

} // namespace sqlsrv

// ---- Háttérszál -------------------------------------------------------------------------------------------------------

using namespace sqlsrv;

class SyncWorker : public QObject {
public:
    SyncWorker(ServerSync *owner, const QString &dbPath) : owner_(owner), dbPath_(dbPath) {}
    ~SyncWorker() override { shutdown(); }

    void begin()
    {
        cfg_ = Config::load();
        timer_ = new QTimer(this);
        timer_->setInterval(3000);
        QObject::connect(timer_, &QTimer::timeout, this, [this] { tick(); });
        QString e;
        local_ = openLocal(dbPath_, QStringLiteral("eter_sync_local"), &e);
        if (!e.isEmpty() || !installTriggers(local_, &e)) {
            report(ServerSync::Failed, TR("A helyi adatbázis nem készíthető elő: %1").arg(e));
            return;
        }
        tables_.clear();
        for (const Table &t : localTables(local_)) tables_.insert(t.name, t);
        timer_->start();
        tick();
    }

    void shutdown()
    {
        if (timer_) timer_->stop();
        if (online_ && !lost_) push();         // ami még összegyűlt
        online_ = false;
        local_ = QSqlDatabase();
        srv_ = QSqlDatabase();
        closeConnection(QStringLiteral("eter_sync_local"));
        closeConnection(QStringLiteral("eter_sync_srv"));
    }

private:
    void report(ServerSync::State s, const QString &detail = {})
    {
        qint64 pending = 0;
        if (local_.isOpen()) {
            QSqlQuery q(local_);
            if (q.exec(QStringLiteral("SELECT COUNT(*) FROM _sync_log")) && q.next()) pending = q.value(0).toLongLong();
        }
        const QDateTime last = lastSync_;
        QMetaObject::invokeMethod(owner_, [o = owner_, s, pending, last, detail] { o->setStatus(s, pending, last, detail); },
                                  Qt::QueuedConnection);
    }

    bool connectServer()
    {
        const QString e = openServer(cfg_, QStringLiteral("eter_sync_srv"));
        if (!e.isEmpty()) {
            detail_ = e;
            report(ServerSync::Offline, e);
            return false;
        }
        srv_ = QSqlDatabase::database(QStringLiteral("eter_sync_srv"));
        QString err;
        if (!ensureMeta(srv_, &err)) {
            report(ServerSync::Offline, err);
            return false;
        }
        for (const Table &t : std::as_const(tables_))
            if (!ensureServerTable(srv_, t, &err)) {
                report(ServerSync::Failed, err);
                return false;
            }
        const QHash<QString, QString> m = readMeta(srv_);
        const int localSchema = userVersion(local_);
        QSqlQuery q(local_);
        qint64 localGen = -1;
        if (q.exec(QStringLiteral("SELECT v FROM _sync_state WHERE k = 'generation'")) && q.next()) localGen = q.value(0).toLongLong();
        if (q.exec(QStringLiteral("SELECT v FROM _sync_state WHERE k = 'server'")) && q.next() && q.value(0).toString() != serverKey(cfg_)) localGen = -1;
        const bool empty = !m.contains(QStringLiteral("generation"));
        // Teljes feltöltés: üres szerver, más szerver, régebbi séma a szerveren (programfrissítés után), vagy induláskor
        // a helyi adatok mellett döntött a felhasználó.
        const bool full = empty || localGen < 0 || m.value(QStringLiteral("schema")).toInt() < localSchema
                          || std::exchange(forceUpload_, false);
        if (full) {
            report(ServerSync::Connecting, TR("teljes feltöltés…"));
            srv_ = QSqlDatabase();
            closeConnection(QStringLiteral("eter_sync_srv"));
            QString uerr;
            if (!upload(cfg_, dbPath_, nullptr, &uerr)) {
                report(ServerSync::Failed, uerr);
                return false;
            }
            openServer(cfg_, QStringLiteral("eter_sync_srv"));
            srv_ = QSqlDatabase::database(QStringLiteral("eter_sync_srv"));
        } else {
            setMeta(srv_, QStringLiteral("writer_id"), clientId());
            setMeta(srv_, QStringLiteral("writer_name"), clientName());
            setMeta(srv_, QStringLiteral("heartbeat"), QString::number(QDateTime::currentSecsSinceEpoch()));
        }
        online_ = true;
        lastBeat_ = QDateTime::currentSecsSinceEpoch();
        lastSync_ = QDateTime::currentDateTime();
        return true;
    }

    void tick()
    {
        if (lost_) return;
        if (!online_) {
            const qint64 now = QDateTime::currentSecsSinceEpoch();
            if (now - lastTry_ < 15 && lastTry_) {
                report(ServerSync::Offline, detail_);
                return;
            }
            lastTry_ = now;
            report(ServerSync::Connecting);
            if (!connectServer()) return;
        }
        if (!push()) return;
        // Az író zár ellenőrzése (ha egy másik gép közben átvette, ez a gép nem ír tovább), félpercenként jelentkezés.
        QSqlQuery q(srv_);
        if (!checkLock(q)) return;
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        if (now - lastBeat_ >= 30) {
            if (!setMeta(srv_, QStringLiteral("heartbeat"), QString::number(now))) {
                goOffline(srv_.lastError().text());
                return;
            }
            lastBeat_ = now;
        }
        report(ServerSync::Online);
    }

    // Az író zár: ha közben egy másik gép vette át, ez a gép nem ír tovább.
    bool checkLock(QSqlQuery &q)
    {
        if (!q.exec(QStringLiteral("SELECT `v` FROM `eter_meta` WHERE `k` = 'writer_id'"))) {
            goOffline(sqlErr(q));
            return false;
        }
        if (q.next() && q.value(0).toString() != clientId()) {
            lost_ = true;
            const QHash<QString, QString> m = readMeta(srv_);
            report(ServerSync::LostLock, m.value(QStringLiteral("writer_name")));
            return false;
        }
        return true;
    }

    void goOffline(const QString &why)
    {
        online_ = false;
        detail_ = why;
        srv_ = QSqlDatabase();
        closeConnection(QStringLiteral("eter_sync_srv"));
        lastTry_ = QDateTime::currentSecsSinceEpoch();
        report(ServerSync::Offline, why);
    }

    bool push()
    {
        QSqlQuery lq(local_);
        lq.setForwardOnly(true);
        if (!lq.exec(QStringLiteral("SELECT id, tbl, k1, k2, k3 FROM _sync_log ORDER BY id LIMIT 2000"))) return true;
        struct Key {
            QString tbl;
            QVariantList keys;
        };
        QVector<Key> order;
        QSet<QString> seen;
        qint64 maxId = 0;
        while (lq.next()) {
            maxId = lq.value(0).toLongLong();
            Key k{lq.value(1).toString(), {lq.value(2), lq.value(3), lq.value(4)}};
            const QString sig = k.tbl + QChar(31) + k.keys[0].toString() + QChar(31) + k.keys[1].toString() + QChar(31) + k.keys[2].toString();
            if (seen.contains(sig)) continue;
            seen.insert(sig);
            order.push_back(k);
        }
        lq.finish();
        if (order.isEmpty()) return true;
        if (!srv_.transaction()) {
            goOffline(srv_.lastError().text());
            return false;
        }
        QSqlQuery sq(srv_);
        if (!sq.exec(QStringLiteral("SELECT `v` FROM `eter_meta` WHERE `k` = 'writer_id' FOR UPDATE")) || !sq.next()) {
            srv_.rollback();
            goOffline(sqlErr(sq));
            return false;
        }
        if (sq.value(0).toString() != clientId()) {
            srv_.rollback();
            lost_ = true;
            report(ServerSync::LostLock, readMeta(srv_).value(QStringLiteral("writer_name")));
            return false;
        }
        for (const Key &k : std::as_const(order)) {
            const auto it = tables_.constFind(k.tbl);
            if (it == tables_.constEnd()) continue;     // azóta megszűnt tábla
            const Table &t = *it;
            QStringList where, sel, cols, upd, ph;
            for (int i = 0; i < t.pk.size(); ++i) where << QStringLiteral("\"%1\" IS ?").arg(t.pk[i]);
            for (const Col &c : t.cols) {
                sel << QStringLiteral("\"%1\"").arg(c.name);
                cols << bq(c.name);
                ph << QStringLiteral("?");
                upd << QStringLiteral("%1 = VALUES(%1)").arg(bq(c.name));
            }
            QSqlQuery row(local_);
            row.prepare(QStringLiteral("SELECT %1 FROM \"%2\" WHERE %3").arg(sel.join(QLatin1Char(',')), t.name, where.join(QStringLiteral(" AND "))));
            for (int i = 0; i < t.pk.size(); ++i) row.addBindValue(k.keys[i]);
            if (!row.exec()) continue;
            if (row.next()) {
                sq.prepare(QStringLiteral("INSERT INTO %1 (%2) VALUES (%3) ON DUPLICATE KEY UPDATE %4")
                               .arg(srvTable(t.name), cols.join(QLatin1Char(',')), ph.join(QLatin1Char(',')), upd.join(QLatin1Char(','))));
                for (int i = 0; i < t.cols.size(); ++i) sq.addBindValue(row.value(i));
            } else {
                QStringList sw;
                for (const QString &p : t.pk) sw << QStringLiteral("%1 = ?").arg(bq(p));
                sq.prepare(QStringLiteral("DELETE FROM %1 WHERE %2").arg(srvTable(t.name), sw.join(QStringLiteral(" AND "))));
                for (int i = 0; i < t.pk.size(); ++i) sq.addBindValue(k.keys[i]);
            }
            if (!sq.exec()) {
                const QString e = QStringLiteral("%1: %2").arg(t.name, sqlErr(sq));
                srv_.rollback();
                goOffline(e);
                return false;
            }
        }
        sq.exec(QStringLiteral("UPDATE `eter_meta` SET `v` = CAST(`v` AS SIGNED) + 1 WHERE `k` = 'generation'"));
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        setMeta(srv_, QStringLiteral("updated_at"), QString::number(now));
        setMeta(srv_, QStringLiteral("heartbeat"), QString::number(now));
        qint64 gen = 0;
        if (sq.exec(QStringLiteral("SELECT `v` FROM `eter_meta` WHERE `k` = 'generation'")) && sq.next()) gen = sq.value(0).toLongLong();
        if (!srv_.commit()) {
            goOffline(srv_.lastError().text());
            return false;
        }
        lastBeat_ = now;
        QSqlQuery del(local_);
        del.prepare(QStringLiteral("DELETE FROM _sync_log WHERE id <= ?"));
        del.addBindValue(maxId);
        del.exec();
        setLocalState(local_, QStringLiteral("generation"), gen);
        lastSync_ = QDateTime::currentDateTime();
        return true;
    }

public:
    bool forceUpload_ = false;

private:
    ServerSync *owner_;
    QString dbPath_;
    Config cfg_;
    QSqlDatabase local_, srv_;
    QHash<QString, Table> tables_;
    QTimer *timer_ = nullptr;
    bool online_ = false, lost_ = false;
    qint64 lastTry_ = 0, lastBeat_ = 0;
    QDateTime lastSync_;
    QString detail_;
};

ServerSync::ServerSync(const QString &dbPath, QObject *parent) : QObject(parent), dbPath_(dbPath) {}

ServerSync::~ServerSync() { stop(); }

void ServerSync::start()
{
    if (thread_) return;
    const Config cfg = Config::load();
    if (!cfg.enabled || !cfg.complete()) {
        setStatus(Off, 0, {}, {});
        return;
    }
    thread_ = new QThread;
    thread_->setObjectName(QStringLiteral("eter-sql-sync"));
    worker_ = new SyncWorker(this, dbPath_);
    worker_->forceUpload_ = sqlsrv::forceUploadRequested();
    worker_->moveToThread(thread_);
    QObject::connect(thread_, &QThread::started, worker_, [w = worker_] { w->begin(); });
    thread_->start();
    setStatus(Connecting, 0, {}, {});
}

void ServerSync::stop()
{
    if (!thread_) return;
    // A munkás a saját szálában áll le (az utolsó feltöltéssel), majd a szál kilép.
    QMetaObject::invokeMethod(worker_, [w = worker_] { delete w; }, Qt::BlockingQueuedConnection);
    worker_ = nullptr;
    thread_->quit();
    thread_->wait();
    delete thread_;
    thread_ = nullptr;
    setStatus(Off, 0, lastSync_, {});
}

void ServerSync::setStatus(State s, qint64 pending, const QDateTime &last, const QString &detail)
{
    state_ = s;
    pending_ = pending;
    if (last.isValid()) lastSync_ = last;
    detail_ = detail;
    emit statusChanged();
}

QString ServerSync::statusText() const
{
    const QString when = lastSync_.isValid() ? TR("utolsó szinkron: %1").arg(lastSync_.toString(QStringLiteral("HH:mm:ss"))) : QString();
    switch (state_) {
    case Off: return TR("kikapcsolva");
    case Connecting: return detail_.isEmpty() ? TR("kapcsolódás…") : TR("kapcsolódás: %1").arg(detail_);
    case Online:
        return pending_ ? TR("kapcsolódva · %1 változás feltöltésre vár").arg(pending_)
                        : TR("kapcsolódva · minden feltöltve") + (when.isEmpty() ? QString() : QStringLiteral(" · ") + when);
    case Offline:
        return TR("nem érhető el (%1) · %2 változás helyben vár").arg(detail_).arg(pending_);
    case LostLock:
        return TR("a szervert közben a(z) „%1” gép vette át: ennek a gépnek a változásai csak helyben maradnak").arg(detail_);
    case Failed: return TR("hiba: %1").arg(detail_);
    }
    return {};
}
