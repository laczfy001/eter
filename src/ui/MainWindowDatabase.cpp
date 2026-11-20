// A főablak: az adatbázis exportálása és importálása (Beállítások → Adatbázis).
// Export: SQLite VACUUM INTO, így futó adás közben is konzisztens másolat készül.
// Import: a kiválasztott fájl ellenőrzése, a jelenlegi adatbázis biztonsági mentése, majd újraindulás; az új
// adatbázis az induláskor lép a régi helyére (applyPendingImport), a program a sémát magától frissíti.
#include "MainWindow.h"
#include "i18n.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocale>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

namespace {

QString sqlQuoted(QString s)
{
    s.replace(QLatin1Char('\''), QStringLiteral("''"));
    return QLatin1Char('\'') + s + QLatin1Char('\'');
}

QString sizeText(qint64 bytes) { return i18n::locale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat); }

// Az élő adatbázis konzisztens másolata a célfájlba (előbb ideiglenes névre, aztán átnevezés).
bool vacuumInto(const QString &target, QString *err)
{
    const QString tmp = target + QStringLiteral(".part");
    QFile::remove(tmp);
    QSqlQuery q;
    if (!q.exec(QStringLiteral("VACUUM INTO %1").arg(sqlQuoted(tmp)))) {
        if (err) *err = q.lastError().text();
        QFile::remove(tmp);
        return false;
    }
    QFile::remove(target);
    if (!QFile::rename(tmp, target)) {
        if (err) *err = TR("a fájl nem nevezhető át: %1").arg(QDir::toNativeSeparators(target));
        QFile::remove(tmp);
        return false;
    }
    return true;
}

struct Inspect {
    bool ok = false;
    QString error;
    int version = 0;
    qint64 items = 0, categories = 0, playlists = 0, entries = 0, transitions = 0, carts = 0;
};

qint64 countOf(QSqlDatabase &db, const QString &table)
{
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(table)) || !q.next()) return -1;
    return q.value(0).toLongLong();
}

// Éter-adatbázis-e, sértetlen-e, és nem újabb-e a sémája, mint amit ez a verzió ismer.
Inspect inspect(const QString &path)
{
    Inspect r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        r.error = f.errorString();
        return r;
    }
    if (f.read(16) != QByteArray("SQLite format 3\0", 16)) {
        r.error = TR("ez nem SQLite adatbázisfájl");
        return r;
    }
    f.close();
    const QString name = QStringLiteral("eter_import_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!db.open()) {
            r.error = db.lastError().text();
        } else {
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("PRAGMA quick_check")) && q.next() && q.value(0).toString() != QLatin1String("ok")) {
                r.error = TR("a fájl sérült (%1)").arg(q.value(0).toString());
            } else {
                q.exec(QStringLiteral("PRAGMA user_version"));
                r.version = q.next() ? q.value(0).toInt() : 0;
                QStringList missing;
                for (const char *t : {"items", "categories", "playlist_entries"}) {
                    QSqlQuery c(db);
                    c.prepare(QStringLiteral("SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = ?"));
                    c.addBindValue(QString::fromLatin1(t));
                    if (!c.exec() || !c.next()) missing << QString::fromLatin1(t);
                }
                if (!missing.isEmpty() || r.version <= 0)
                    r.error = TR("ez nem az Éter adatbázisa (hiányzó táblák: %1)").arg(missing.isEmpty() ? QStringLiteral("–") : missing.join(", "));
                else if (r.version > Database::kSchemaVersion)
                    r.error = TR("az adatbázist az Éter egy újabb verziója készítette (séma: %1; ez a verzió a %2. sémáig ismeri). "
                                 "Frissítsd a programot, és utána importáld.").arg(r.version).arg(Database::kSchemaVersion);
                else {
                    r.items = countOf(db, QStringLiteral("items"));
                    r.categories = countOf(db, QStringLiteral("categories"));
                    r.playlists = countOf(db, QStringLiteral("playlists"));
                    r.entries = countOf(db, QStringLiteral("playlist_entries"));
                    r.transitions = countOf(db, QStringLiteral("transitions"));
                    r.carts = countOf(db, QStringLiteral("carts"));
                    r.ok = true;
                }
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(name);
    return r;
}

QString describe(const Inspect &r)
{
    QStringList parts;
    parts << TR("%1 elem").arg(r.items);
    if (r.categories >= 0) parts << TR("%1 kategória").arg(r.categories);
    if (r.playlists >= 0) parts << TR("%1 playlist").arg(r.playlists);
    if (r.entries >= 0) parts << TR("%1 playlistsor").arg(r.entries);
    if (r.transitions >= 0) parts << TR("%1 mentett átmenet").arg(r.transitions);
    return parts.join(QStringLiteral(" · "));
}

QString backupFolder(const QString &dbPath) { return QFileInfo(dbPath).absolutePath() + QStringLiteral("/biztonsagi-mentesek"); }

} // namespace

QString MainWindow::databaseSummary() const
{
    const QString path = db_->path();
    QSqlQuery q;
    int version = 0;
    if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next()) version = q.value(0).toInt();
    qint64 items = 0;
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM items")) && q.next()) items = q.value(0).toLongLong();
    qint64 size = QFileInfo(path).size() + QFileInfo(path + QStringLiteral("-wal")).size();
    return TR("%1\n%2 · %3 · séma: %4").arg(QDir::toNativeSeparators(path), sizeText(size), TR("%1 elem").arg(items)).arg(version);
}

bool MainWindow::exportDatabase(QWidget *parent)
{
    const QString last = QSettings().value("database/exportDir", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    const QString suggested = QDir(last).filePath(QStringLiteral("eter-adatbazis-%1.sqlite")
                                                     .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmm"))));
    QString target = QFileDialog::getSaveFileName(parent, TR("Adatbázis exportálása"), suggested,
                                                  TR("Éter adatbázis (*.sqlite *.db);;Minden fájl (*)"));
    if (target.isEmpty()) return false;
    if (QFileInfo(target).suffix().isEmpty()) target += QStringLiteral(".sqlite");
    if (QFileInfo(target).absoluteFilePath() == QFileInfo(db_->path()).absoluteFilePath()) {
        QMessageBox::warning(parent, TR("Adatbázis exportálása"), TR("Az export nem írhatja felül a használatban lévő adatbázist."));
        return false;
    }
    QSettings().setValue("database/exportDir", QFileInfo(target).absolutePath());
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = exportDatabaseTo(target, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::critical(parent, TR("Adatbázis exportálása"), TR("Az exportálás nem sikerült:\n%1").arg(err));
        return false;
    }
    const Inspect r = inspect(target);
    say(TR("Adatbázis exportálva: %1").arg(QDir::toNativeSeparators(target)), 10000);
    QMessageBox::information(parent, TR("Adatbázis exportálása"),
                             TR("Kész: %1\n\n%2 · %3\n\nA hangfájlok nincsenek benne, csak a hivatkozásuk: másik gépen ugyanott "
                                "kell lenniük (vagy a Hiányzó fájlok ellenőrzésével újra összeköthetők).")
                                 .arg(QDir::toNativeSeparators(target), sizeText(QFileInfo(target).size()), describe(r)));
    return true;
}

void MainWindow::importDatabase(QWidget *parent)
{
    if (anythingPlaying()) {
        QMessageBox::warning(parent, TR("Adatbázis importálása"),
                             TR("Most szól valami. Az importálás után a program újraindul, ezért előbb állítsd le az adást."));
        return;
    }
    const QString last = QSettings().value("database/exportDir", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    const QString source = QFileDialog::getOpenFileName(parent, TR("Adatbázis importálása"), last,
                                                        TR("Éter adatbázis (*.sqlite *.db);;Minden fájl (*)"));
    if (source.isEmpty()) return;
    if (QFileInfo(source).absoluteFilePath() == QFileInfo(db_->path()).absoluteFilePath()) {
        QMessageBox::warning(parent, TR("Adatbázis importálása"), TR("Ez a most használt adatbázis."));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const Inspect r = inspect(source);
    QApplication::restoreOverrideCursor();
    if (!r.ok) {
        QMessageBox::critical(parent, TR("Adatbázis importálása"), TR("Ez a fájl nem importálható:\n%1").arg(r.error));
        return;
    }
    const QString backupDir = backupFolder(db_->path());
    if (QMessageBox::question(parent, TR("Adatbázis importálása"),
                              TR("Importálod ezt az adatbázist?\n\n%1\n%2\n\nA mostani adatbázis teljes egészében lecserélődik "
                                 "(hangtár, playlistek, ütemező, mixek, beállítások). Előtte biztonsági mentés készül róla ide:\n%3\n\n"
                                 "A program ezután újraindul.")
                                  .arg(QDir::toNativeSeparators(source), describe(r), QDir::toNativeSeparators(backupDir)),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    QString err;
    if (!stageDatabaseImport(source, &err)) {
        QMessageBox::critical(parent, TR("Adatbázis importálása"), err);
        return;
    }
    say(TR("Adatbázis importálása: újraindulás…"));
    QTimer::singleShot(300, this, [this] { relaunch(QCoreApplication::arguments().mid(1)); });
}

bool MainWindow::exportDatabaseTo(const QString &target, QString *err)
{
    return vacuumInto(target, err);
}

bool MainWindow::stageDatabaseImport(const QString &source, QString *err, QString *backupPath)
{
    const Inspect r = inspect(source);
    if (!r.ok) {
        if (err) *err = TR("Ez a fájl nem importálható:\n%1").arg(r.error);
        return false;
    }
    const QString backupDir = backupFolder(db_->path());
    QDir().mkpath(backupDir);
    const QString backup = QDir(backupDir).filePath(QStringLiteral("library-import-elott-%1.sqlite")
                                                        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd-HHmmss"))));
    QString e;
    if (!vacuumInto(backup, &e)) {
        if (err) *err = TR("A mostani adatbázisról nem sikerült biztonsági mentést készíteni, ezért az importálás elmarad:\n%1").arg(e);
        return false;
    }
    if (backupPath) *backupPath = backup;
    const QString pending = db_->path() + QStringLiteral(".import");
    QFile::remove(pending + QStringLiteral(".part"));
    QFile::remove(pending);
    if (!QFile::copy(source, pending + QStringLiteral(".part")) || !QFile::rename(pending + QStringLiteral(".part"), pending)) {
        QFile::remove(pending + QStringLiteral(".part"));
        if (err) *err = TR("Az importálandó fájl nem másolható ide:\n%1").arg(QDir::toNativeSeparators(pending));
        return false;
    }
    return true;
}

// Újraindítás; az új példány megvárja, amíg ez kilép (a zár felszabadul), és induláskor átveszi az importált adatbázist.
void MainWindow::relaunch(const QStringList &args)
{
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const QString exe = appImage.isEmpty() ? QCoreApplication::applicationFilePath() : appImage;
    QProcess p;
    p.setProgram(exe);
    p.setArguments(args);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("ETER_RELAUNCH"), QStringLiteral("1"));
    p.setProcessEnvironment(env);
    p.startDetached();
    QApplication::exit(0);
}

QString MainWindow::applyPendingImport(const QString &dbPath)
{
    const QString pending = dbPath + QStringLiteral(".import");
    if (!QFile::exists(pending)) return {};
    // A régi adatbázisról az importáláskor már készült biztonsági mentés; a WAL-fájlok a régihez tartoznak.
    const QString old = dbPath + QStringLiteral(".replaced");
    QFile::remove(old);
    if (QFile::exists(dbPath) && !QFile::rename(dbPath, old)) return TR("a régi adatbázis nem nevezhető át");
    QFile::remove(dbPath + QStringLiteral("-wal"));
    QFile::remove(dbPath + QStringLiteral("-shm"));
    if (!QFile::rename(pending, dbPath)) {
        QFile::rename(old, dbPath);                      // vissza a régi
        return TR("az importált adatbázis nem tehető a helyére");
    }
    QFile::remove(old);
    return {};
}
