#include "Remote.h"
#include "i18n.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

namespace remote {

static constexpr qsizetype kMaxLine = 64 * 1024 * 1024;   // egy üzenet legfeljebb ekkora lehet

// --------------------------------------------------------------- JSON

QJsonObject itemToJson(const Item &it)
{
    return QJsonObject{{"id", it.id}, {"cat", it.categoryId}, {"title", it.title}, {"artist", it.artist}, {"path", it.path},
                       {"dur", it.durationMs}, {"last", it.lastPlayedAt}, {"mix", it.mixOutMs}, {"userMix", it.userMixOutMs},
                       {"cueIn", it.cueInMs}, {"fadeOut", it.fadeOutMs}, {"curve", it.fadeCurve}, {"intro", it.introMs},
                       {"cueOut", it.cueOutMs}};
}

Item itemFromJson(const QJsonObject &o)
{
    Item it;
    it.id = o.value("id").toInt();
    it.categoryId = o.value("cat").toInt();
    it.title = o.value("title").toString();
    it.artist = o.value("artist").toString();
    it.path = o.value("path").toString();
    it.durationMs = o.value("dur").toInteger();
    it.lastPlayedAt = o.value("last").toInteger();
    it.mixOutMs = o.value("mix").toInteger();
    it.userMixOutMs = o.value("userMix").toInteger();
    it.cueInMs = o.value("cueIn").toInteger();
    it.fadeOutMs = o.value("fadeOut").toInteger(-1);
    it.fadeCurve = o.value("curve").toString();
    it.introMs = o.value("intro").toInteger();
    it.cueOutMs = o.value("cueOut").toInteger();
    return it;
}

QJsonObject categoryToJson(const Category &c)
{
    return QJsonObject{{"id", c.id}, {"kind", c.kind}, {"name", c.name}, {"color", c.color}};
}

Category categoryFromJson(const QJsonObject &o)
{
    Category c;
    c.id = o.value("id").toInt();
    c.kind = o.value("kind").toString();
    c.name = o.value("name").toString();
    c.color = o.value("color").toString();
    return c;
}

QJsonObject transitionToJson(const Transition &t)
{
    return QJsonObject{{"to", t.toEntryId}, {"from", t.fromEntryId}, {"mix", t.mixOutMs}, {"outEnd", t.outEndMs},
                       {"fadeOut", t.fadeOutMs}, {"curve", t.fadeOutCurve}, {"cueIn", t.cueInMs}, {"fadeIn", t.fadeInMs},
                       {"startLevel", t.startLevelDb}, {"vt", t.vtItemId}, {"vtOffset", t.vtOffsetMs}, {"vtDuck", t.vtDuckDb}};
}

Transition transitionFromJson(const QJsonObject &o)
{
    Transition t;
    t.toEntryId = o.value("to").toInt();
    t.fromEntryId = o.value("from").toInt();
    t.mixOutMs = o.value("mix").toInteger();
    t.outEndMs = o.value("outEnd").toInteger();
    t.fadeOutMs = o.value("fadeOut").toInteger();
    t.fadeOutCurve = o.value("curve").toString(QStringLiteral("scurve"));
    if (t.fadeOutCurve != "linear" && t.fadeOutCurve != "log" && t.fadeOutCurve != "scurve") t.fadeOutCurve = QStringLiteral("scurve");
    t.cueInMs = o.value("cueIn").toInteger();
    t.fadeInMs = o.value("fadeIn").toInteger();
    t.startLevelDb = o.value("startLevel").toDouble();
    t.vtItemId = o.value("vt").toInt();
    t.vtOffsetMs = o.value("vtOffset").toInteger();
    t.vtDuckDb = o.value("vtDuck").toDouble(-9);
    return t;
}

// ------------------------------------------------------------ belépés

QByteArray newSalt()
{
    QByteArray b(16, Qt::Uninitialized);
    for (char &c : b) c = char(QRandomGenerator::system()->bounded(256));
    return b;
}

QByteArray deriveKey(const QByteArray &salt, const QString &password)
{
    // Lassított származtatás: 20 000 kör SHA-256, hogy a tárolt kulcsból a jelszó ne legyen könnyen kitalálható.
    QByteArray k = salt + password.toUtf8();
    for (int i = 0; i < 20000; ++i) k = QCryptographicHash::hash(k + salt, QCryptographicHash::Sha256);
    return k;
}

QByteArray answer(const QByteArray &key, const QByteArray &nonce)
{
    return QMessageAuthenticationCode::hash(nonce, key, QCryptographicHash::Sha256);
}

static bool sameBytes(const QByteArray &a, const QByteArray &b)
{
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    return diff == 0;
}

// ----------------------------------------------------------- kapcsolat

Connection::Connection(QTcpSocket *socket, QObject *parent) : QObject(parent), socket_(socket)
{
    socket_->setParent(this);
    socket_->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(socket_, &QTcpSocket::readyRead, this, &Connection::readSome);
    // A bontás egyszer jelez, akár szabályos, akár hiba miatt történt.
    auto once = [this] {
        if (closedSent_) return;
        closedSent_ = true;
        emit closed();
    };
    connect(socket_, &QTcpSocket::disconnected, this, once);
    connect(socket_, &QTcpSocket::errorOccurred, this, once);
}

Connection::~Connection() = default;

QString Connection::peer() const { return socket_->peerAddress().toString(); }

void Connection::send(const QJsonObject &msg)
{
    if (socket_->state() != QAbstractSocket::ConnectedState) return;
    socket_->write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
}

void Connection::close()
{
    socket_->flush();
    socket_->disconnectFromHost();
}

void Connection::readSome()
{
    buffer_ += socket_->readAll();
    for (;;) {
        const qsizetype nl = buffer_.indexOf('\n');
        if (nl < 0) break;
        const QByteArray line = buffer_.left(nl);
        buffer_.remove(0, nl + 1);
        QJsonParseError err;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error == QJsonParseError::NoError && doc.isObject()) emit message(doc.object());
        if (!socket_ || socket_->state() != QAbstractSocket::ConnectedState) return;
    }
    // Belépés előtt csak rövid üzenet jöhet; utána is van felső határ (hibás vagy rosszindulatú partner).
    if (buffer_.size() > (authenticated ? kMaxLine : qsizetype(64 * 1024))) {
        buffer_.clear();
        socket_->abort();
    }
}

// --------------------------------------------------------------- szerver

Server::Server(QObject *parent) : QObject(parent), tcp_(new QTcpServer(this))
{
    connect(tcp_, &QTcpServer::newConnection, this, &Server::accept);
}

Server::~Server()
{
    blockSignals(true);
    stop();
}

bool Server::start(quint16 port, const QByteArray &salt, const QByteArray &key, QString *err)
{
    stop();
    salt_ = salt;
    key_ = key;
    if (!tcp_->listen(QHostAddress::Any, port)) {
        if (err) *err = tcp_->errorString();
        return false;
    }
    return true;
}

void Server::stop()
{
    if (tcp_->isListening()) tcp_->close();
    for (const QPointer<Connection> &c : clients_)
        if (c) c->deleteLater();
    clients_.clear();
    emit clientsChanged(0);
}

bool Server::running() const { return tcp_->isListening(); }
quint16 Server::port() const { return tcp_->serverPort(); }

int Server::clientCount() const
{
    int n = 0;
    for (const QPointer<Connection> &c : clients_)
        if (c && c->authenticated) ++n;
    return n;
}

void Server::broadcast(const QJsonObject &msg)
{
    for (const QPointer<Connection> &c : clients_)
        if (c && c->authenticated) c->send(msg);
}

void Server::accept()
{
    while (QTcpSocket *s = tcp_->nextPendingConnection()) {
        auto *c = new Connection(s, this);
        c->nonce = newSalt() + newSalt();
        clients_.push_back(c);
        connect(c, &Connection::message, this, [this, c](const QJsonObject &m) { onMessage(c, m); });
        connect(c, &Connection::closed, this, [this, c] {
            clients_.removeAll(QPointer<Connection>(c));
            c->deleteLater();
            emit clientsChanged(clientCount());
        });
        // Aki 15 másodpercen belül nem lép be, azt bontjuk.
        QTimer::singleShot(15000, c, [c] { if (!c->authenticated) c->close(); });
        c->send(QJsonObject{{"t", "hello"}, {"app", "Éter"}, {"version", QStringLiteral(ETER_VERSION)}, {"proto", kProtocol},
                            {"salt", QString::fromLatin1(salt_.toBase64())}, {"nonce", QString::fromLatin1(c->nonce.toBase64())}});
    }
}

void Server::onMessage(Connection *c, const QJsonObject &m)
{
    if (!c->authenticated) {
        if (m.value("t").toString() != QLatin1String("auth")) return;
        const QByteArray mac = QByteArray::fromBase64(m.value("mac").toString().toLatin1());
        if (key_.isEmpty() || !sameBytes(mac, answer(key_, c->nonce))) {
            c->send(QJsonObject{{"t", "auth"}, {"ok", false}, {"error", TR("Hibás jelszó.")}});
            // Kis várakozás a bontás előtt: a jelszó próbálgatása így lassú.
            QTimer::singleShot(1500, c, [c] { c->close(); });
            return;
        }
        c->authenticated = true;
        c->send(QJsonObject{{"t", "auth"}, {"ok", true}});
        emit clientsChanged(clientCount());
        emit authenticated(c);
        return;
    }
    emit request(c, m);
}

// ---------------------------------------------------------------- kliens

Client::Client(QObject *parent) : QObject(parent) {}

void Client::connectTo(const QString &host, quint16 port, const QString &password)
{
    disconnectFromHost();
    password_ = password;
    wasReady_ = false;
    auto *s = new QTcpSocket;
    auto *c = new Connection(s, this);
    conn_ = c;
    connect(c, &Connection::message, this, [this, c](const QJsonObject &m) {
        const QString t = m.value("t").toString();
        if (t == QLatin1String("hello")) {
            if (m.value("proto").toInt() != kProtocol) {
                emit failed(TR("A playout más verziójú (%1) – frissítsd mindkét gépen az Étert.").arg(m.value("version").toString()));
                c->close();
                return;
            }
            version_ = m.value("version").toString();
            const QByteArray salt = QByteArray::fromBase64(m.value("salt").toString().toLatin1());
            const QByteArray nonce = QByteArray::fromBase64(m.value("nonce").toString().toLatin1());
            const QByteArray key = deriveKey(salt, password_);
            c->send(QJsonObject{{"t", "auth"}, {"mac", QString::fromLatin1(answer(key, nonce).toBase64())}});
            return;
        }
        if (t == QLatin1String("auth")) {
            if (!m.value("ok").toBool()) {
                emit failed(m.value("error").toString(TR("A belépés nem sikerült.")));
                return;
            }
            c->authenticated = true;
            wasReady_ = true;
            emit loggedIn(version_);
            return;
        }
        if (c->authenticated) emit message(m);
    });
    connect(c, &Connection::closed, this, [this, c, s] {
        if (conn_ != c) return;
        const QString reason = s->errorString();
        conn_ = nullptr;
        c->deleteLater();
        if (wasReady_) emit lost(reason);
        else emit failed(TR("Nem sikerült csatlakozni: %1").arg(reason));
    });
    s->connectToHost(host, port);
    // Ha 8 másodperc alatt nincs belépés, feladjuk.
    QTimer::singleShot(8000, c, [this, c, s] {
        if (conn_ == c && !c->authenticated) {
            conn_ = nullptr;
            s->abort();
            c->deleteLater();
            emit failed(TR("Nem sikerült csatlakozni: nem válaszol (rossz cím vagy port, tűzfal, vagy nincs bekapcsolva a távoli szerkesztés)."));
        }
    });
}

void Client::disconnectFromHost()
{
    if (!conn_) return;
    Connection *c = conn_;
    conn_ = nullptr;
    c->close();
    c->deleteLater();
}

int Client::sendOp(const QString &op, QJsonObject args)
{
    if (!ready()) return 0;
    const int id = nextId_++;
    args.insert("t", "op");
    args.insert("op", op);
    args.insert("id", id);
    conn_->send(args);
    return id;
}

} // namespace remote
