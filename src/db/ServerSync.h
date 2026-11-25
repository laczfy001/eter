// Külső MySQL / MariaDB szerver: fő tároló, helyi tartalékkal.
//
// Az Éter továbbra is a helyi SQLite adatbázisból dolgozik (így a hálózat vagy a szerver kiesése az adást nem
// állítja meg), de minden változás percen belül felkerül a szerverre, és a szerveren lévő adat az irányadó:
//   - a helyi adatbázis triggerei feljegyzik, melyik sor változott (_sync_log); a háttérszál ezeket a sorokat
//     néhány másodpercenként átmásolja a szerver azonos nevű, „eter_” előtagú tábláiba;
//   - egyszerre egy gép ír a szerverre (eter_meta: writer_id, heartbeat). Ha egy másik gép átveszi, az előző
//     észreveszi, és nem ír tovább;
//   - induláskor, ha a szerveren egy másik gép újabb adatot hagyott, az Éter letölti, és azzal indul;
//   - ha a szerver nem érhető el, a változások helyben gyűlnek, és a kapcsolat visszatértével felkerülnek.
// A hangfájlok nem kerülnek a szerverre: a hangtár az elérési útjukat tárolja (közös hálózati meghajtón érdemes).
#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class QSqlDatabase;
class QThread;
class QTimer;

namespace sqlsrv {

struct Config {
    bool enabled = false;
    QString host;
    int port = 3306;
    QString user, password, database;
    static Config load();       // QSettings sql/…
    void save() const;
    bool complete() const { return !host.trimmed().isEmpty() && !user.isEmpty() && !database.trimmed().isEmpty(); }
};

QString clientId();             // ennek a gépnek (telepítésnek) az állandó azonosítója
QString clientName();           // a gép neve, a többi gépnek kiírva

// A szerver állapota (kapcsolat tesztelése, indulási ellenőrzés).
struct ServerInfo {
    bool ok = false;
    QString error;
    QString version;            // a szerver verziója
    bool hasData = false;       // van már Éter-adat a szerveren
    int schema = 0;             // az adatbázis sémája, amivel feltöltötték
    qint64 generation = 0;      // minden feltöltéssel nő
    QString writerId, writerName;
    qint64 heartbeat = 0;       // az író gép utolsó jelentkezése (unix mp)
    qint64 items = 0;           // a hangtár elemei a szerveren
};

// A kapcsolat megnyitása a megadott néven (a hívó szálában használható). Üres: rendben, különben a hiba szövege.
QString openServer(const Config &cfg, const QString &connectionName);
ServerInfo probe(const Config &cfg);

// A helyi adatbázis szinkronállapota (a fájlból, induláskor; a táblák hiányozhatnak).
struct LocalState {
    qint64 generation = -1;     // −1: ez az adatbázis még nem volt a szerverrel összekötve
    qint64 pending = 0;         // a még fel nem töltött változások
    int schema = 0;
};
LocalState localState(const QString &dbPath);

using Progress = std::function<bool(int percent, const QString &what)>;   // false: megszakítás

// Teljes feltöltés: a szerver Éter-táblái a helyi adatbázis másolatai lesznek; ez a gép lesz az író.
bool upload(const Config &cfg, const QString &dbPath, const Progress &progress, QString *err);
// Letöltés: a helyi adatbázis másolata (target) a szerver adataival. A hívó teszi a helyére (importként).
bool download(const Config &cfg, const QString &dbPath, const QString &target, const Progress &progress, QString *err);
// A változásnapló triggerei a helyi adatbázisban (a megadott kapcsolaton); remove: törlés.
bool installTriggers(QSqlDatabase &db, QString *err);
void removeTriggers(QSqlDatabase &db);

// Indulás előtt (a Database megnyitása előtt): a szerver újabb adatának letöltése, az író gép kérdése.
// A választ párbeszédablakban kérdezi. Visszaad: true, ha ebben a futásban szinkronizálni kell.
bool startupCheck(const QString &dbPath, bool interactive);
bool forceUploadRequested();    // induláskor a „helyi adat a szerverre” választás született

} // namespace sqlsrv

// A háttérszál: néhány másodpercenként feltölti a változásokat, félpercenként jelentkezik (író zár).
class ServerSync : public QObject {
    Q_OBJECT
public:
    enum State { Off, Connecting, Online, Offline, LostLock, Failed };
    explicit ServerSync(const QString &dbPath, QObject *parent = nullptr);
    ~ServerSync() override;
    void start();               // a beállítások szerint (sql/enabled)
    void stop();
    bool running() const { return thread_ != nullptr; }
    State state() const { return state_; }
    QString statusText() const;
    qint64 pending() const { return pending_; }
    QDateTime lastSync() const { return lastSync_; }

signals:
    void statusChanged();

private:
    friend class SyncWorker;
    void setStatus(State s, qint64 pending, const QDateTime &last, const QString &detail);
    QString dbPath_;
    QThread *thread_ = nullptr;
    class SyncWorker *worker_ = nullptr;
    State state_ = Off;
    qint64 pending_ = 0;
    QDateTime lastSync_;
    QString detail_;
};
