// Távoli szerkesztés: a playout egy kis TCP-szolgáltatást futtat, a szerkesztő mód ehhez csatlakozik.
// Üzenetek: soronként egy tömör JSON-objektum. Belépés: a szerver véletlen kihívást (nonce) küld, a kliens a
// jelszóból képzett kulccsal HMAC-SHA256 választ ad – a jelszó maga nem megy át a hálózaton.
//
//   szerver → kliens  {"t":"hello", "app":"Éter", "version":…, "proto":1, "salt":…, "nonce":…}
//   kliens → szerver  {"t":"auth", "mac":…}
//   szerver → kliens  {"t":"auth", "ok":true} | {"t":"auth", "ok":false, "error":…}
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
#include <QVector>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace remote {

constexpr int kProtocol = 1;
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
    QString peer() const;
    bool authenticated = false;
    QByteArray nonce;

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
    bool start(quint16 port, const QByteArray &salt, const QByteArray &key, QString *err);
    void stop();
    bool running() const;
    quint16 port() const;
    int clientCount() const;
    void broadcast(const QJsonObject &msg);            // a belépett kliensek mindegyikének

signals:
    void authenticated(remote::Connection *c);
    void request(remote::Connection *c, const QJsonObject &msg);
    void clientsChanged(int count);

private:
    void accept();
    void onMessage(Connection *c, const QJsonObject &msg);

    QTcpServer *tcp_;
    QVector<QPointer<Connection>> clients_;
    QByteArray salt_, key_;
};

// A szerkesztő oldala.
class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(QObject *parent = nullptr);
    void connectTo(const QString &host, quint16 port, const QString &password);
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
    QString password_, version_;
    int nextId_ = 1;
    bool wasReady_ = false;
};

} // namespace remote
