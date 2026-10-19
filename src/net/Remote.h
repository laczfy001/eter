// Távoli szerkesztés: a playout egy kis TCP-szolgáltatást futtat, a szerkesztő mód ehhez csatlakozik.
// Üzenetek: soronként egy tömör JSON-objektum. Belépés: a szerver véletlen kihívást (nonce) küld, a kliens a
// jelszóból képzett kulccsal HMAC-SHA256 választ ad – a jelszó maga nem megy át a hálózaton.
//
//   szerver → kliens  {"t":"hello", "app":"Éter", "version":…, "proto":2, "nonce":…}
//   kliens → szerver  {"t":"login", "user":…}         (üres név: az alap jelszó, teljes joggal)
//   szerver → kliens  {"t":"challenge", "salt":…}     a felhasználó kulcsának sója
//   kliens → szerver  {"t":"auth", "mac":…}
//   szerver → kliens  {"t":"auth", "ok":true, "user":…, "perms":[…]} | {"t":"auth", "ok":false, "error":…}
//   szerver → kliens  {"t":"library", …}   a hangtár (kategóriák, elemek, beállítások)
//   szerver → kliens  {"t":"state", …}     a playlist élő állapota és az átmenetek
//   kliens → szerver  {"t":"op", "id":n, "op":…, …}   módosítás
//   szerver → kliens  {"t":"ack", "id":n, "ok":…, "error":…}
#pragma once
#include "db/Database.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVector>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace remote {

constexpr int kProtocol = 2;

// Jogosultságok: sorrend, törlés, beszúrás a hangtárból, mix és ráfektetés, feltöltés (fájl és voice track).
inline const QStringList &allPerms()
{
    static const QStringList p{QStringLiteral("order"), QStringLiteral("delete"), QStringLiteral("insert"), QStringLiteral("mix"),
                               QStringLiteral("upload"), QStringLiteral("backup")};
    return p;
}
QString permName(const QString &perm);

// Egy belépésre jogosult felhasználó a playout gépen.
struct User {
    QString name;                      // üres: az alap jelszó
    QByteArray salt, key;
    QStringList perms;
};
QVector<User> loadUsers();                              // a beállításokból (az alap jelszóval együtt)
void saveUsers(const QVector<User> &users);              // a névvel rendelkezőket (az alap jelszó külön marad)
constexpr quint16 kDefaultPort = 7341;

QJsonObject itemToJson(const Item &it);
Item itemFromJson(const QJsonObject &o);
QJsonObject categoryToJson(const Category &c);
Category categoryFromJson(const QJsonObject &o);
QJsonObject transitionToJson(const Transition &t);
Transition transitionFromJson(const QJsonObject &o);

QByteArray newSalt();
QByteArray deriveKey(const QByteArray &salt, const QString &password);
QByteArray answer(const QByteArray &key, const QByteArray &nonce);

// Egy kapcsolat: soronkénti JSON küldése és fogadása.
class Connection : public QObject {
    Q_OBJECT
public:
    explicit Connection(QTcpSocket *socket, QObject *parent = nullptr);
    ~Connection() override;
    void send(const QJsonObject &msg);
    void close();
    void flushNow(int ms);             // a kimenő adat elküldése most (kilépés előtt)
    QString peer() const;
    bool authenticated = false;
    QByteArray nonce;
    QString user;                      // belépés után: a felhasználó neve
    QStringList perms;                 // és a jogai
    QString role;                      // "editor" (szerkesztő), "backup" (tartalék, készenlétben), "onair" (tartalék, adásban)
    QByteArray pendingKey;             // a bejelentkezés alatt: a választott felhasználó kulcsa

signals:
    void message(const QJsonObject &msg);
    void closed();

private:
    void readSome();
    QTcpSocket *socket_;
    QByteArray buffer_;
    bool closedSent_ = false;
};

// A playout oldala.
class Server : public QObject {
    Q_OBJECT
public:
    explicit Server(QObject *parent = nullptr);
    ~Server() override;
    bool start(quint16 port, const QVector<User> &users, QString *err);
    void setUsers(const QVector<User> &users) { users_ = users; }
    QStringList connectedUsers() const;                // „név (cím)” a belépett kliensekről
    void stop();
    bool running() const;
    quint16 port() const;
    int clientCount() const;
    enum Audience { Everyone, Editors, Backups };
    void broadcast(const QJsonObject &msg, Audience to = Everyone);   // a belépett klienseknek
    int count(Audience to) const;
    QStringList onAirBackups() const;                  // az adásban lévő tartalékok címe
    void goodbye();                                    // szabályos leállás: a tartalékok ne vegyék át az adást

signals:
    void authenticated(remote::Connection *c);
    void request(remote::Connection *c, const QJsonObject &msg);
    void clientsChanged(int count);

private:
    void accept();
    void onMessage(Connection *c, const QJsonObject &msg);

    QTcpServer *tcp_;
    QVector<QPointer<Connection>> clients_;
    QVector<User> users_;
};

// A szerkesztő oldala.
class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(QObject *parent = nullptr);
    void connectTo(const QString &host, quint16 port, const QString &user, const QString &password);
    // A tartalék szerepe a belépéskor ("backup" vagy "onair"); üres: szerkesztő.
    void setRole(const QString &role) { role_ = role; }
    // A jelszó helyett a korábban kapott só és kulcs is használható (a tartalék így nem tárolja a jelszót).
    void setKeyCache(const QByteArray &salt, const QByteArray &key) { cacheSalt_ = salt; cacheKey_ = key; }
    QByteArray lastSalt() const { return lastSalt_; }
    QByteArray lastKey() const { return lastKey_; }
    const QStringList &perms() const { return perms_; }
    bool can(const QString &perm) const { return perms_.contains(perm); }
    void disconnectFromHost();
    bool ready() const { return conn_ && conn_->authenticated; }
    int sendOp(const QString &op, QJsonObject args);    // a kérés azonosítója; 0, ha nincs kapcsolat
    QString serverVersion() const { return version_; }

signals:
    void loggedIn(const QString &version);              // sikeres belépés
    void failed(const QString &reason);                 // nem sikerült csatlakozni vagy belépni
    void message(const QJsonObject &msg);               // library, state, ack
    void lost(const QString &reason);                   // a belépés után megszakadt

private:
    QPointer<Connection> conn_;
    QString user_, password_, version_, role_;
    QByteArray cacheSalt_, cacheKey_, lastSalt_, lastKey_;
    QStringList perms_;
    int nextId_ = 1;
    bool wasReady_ = false;
};

} // namespace remote
