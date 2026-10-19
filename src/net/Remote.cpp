#include "Remote.h"
#include "i18n.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSettings>
#include <QJsonArray>
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
                       {"cueOut", it.cueOutMs}, {"energy", it.energy}, {"bpm", it.bpm}, {"parts", it.dayparts}};
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
    it.energy = std::clamp(o.value("energy").toInt(), 0, 5);
    it.bpm = o.value("bpm").toDouble();
    it.dayparts = o.value("parts").toInt();
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

QString permName(const QString &perm)
{
    if (perm == QLatin1String("order")) return TR("sorrend");
    if (perm == QLatin1String("delete")) return TR("törlés");
    if (perm == QLatin1String("insert")) return TR("beszúrás a hangtárból");
    if (perm == QLatin1String("mix")) return TR("mix és ráfektetés");
    if (perm == QLatin1String("upload")) return TR("feltöltés és voice track");
    if (perm == QLatin1String("backup")) return TR("tartalék playout");
    return perm;
}

QVector<User> loadUsers()
{
    QSettings s;
    QVector<User> out;
    const QByteArray key = QByteArray::fromBase64(s.value("remote/key").toByteArray());
    if (!key.isEmpty()) out.push_back({QString(), QByteArray::fromBase64(s.value("remote/salt").toByteArray()), key, allPerms()});
    for (const QJsonValue &v : QJsonDocument::fromJson(s.value("remote/users").toByteArray()).array()) {
        const QJsonObject o = v.toObject();
        User u;
        u.name = o.value("name").toString().trimmed();
        u.salt = QByteArray::fromBase64(o.value("salt").toString().toLatin1());
        u.key = QByteArray::fromBase64(o.value("key").toString().toLatin1());
        for (const QJsonValue &p : o.value("perms").toArray())
            if (allPerms().contains(p.toString())) u.perms << p.toString();
        if (!u.name.isEmpty() && !u.key.isEmpty()) out.push_back(u);
    }
    return out;
}

void saveUsers(const QVector<User> &users)
{
    QJsonArray a;
    for (const User &u : users) {
        if (u.name.isEmpty()) continue;
        a.push_back(QJsonObject{{"name", u.name}, {"salt", QString::fromLatin1(u.salt.toBase64())},
                                {"key", QString::fromLatin1(u.key.toBase64())}, {"perms", QJsonArray::fromStringList(u.perms)}});
    }
    QSettings().setValue("remote/users", QJsonDocument(a).toJson(QJsonDocument::Compact));
}

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

void Connection::flushNow(int ms)
{
    if (socket_->state() != QAbstractSocket::ConnectedState) return;
    socket_->flush();
    if (socket_->bytesToWrite() > 0) socket_->waitForBytesWritten(ms);
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

bool Server::start(quint16 port, const QVector<User> &users, QString *err)
{
    stop();
    users_ = users;
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

static bool inAudience(const Connection *c, Server::Audience to)
{
    const bool backup = c->role == QLatin1String("backup") || c->role == QLatin1String("onair");
    return to == Server::Everyone || (to == Server::Backups) == backup;
}

void Server::broadcast(const QJsonObject &msg, Audience to)
{
    for (const QPointer<Connection> &c : clients_)
        if (c && c->authenticated && inAudience(c, to)) c->send(msg);
}

int Server::count(Audience to) const
{
    int n = 0;
    for (const QPointer<Connection> &c : clients_)
        if (c && c->authenticated && inAudience(c, to)) ++n;
    return n;
}

void Server::goodbye()
{
    for (const QPointer<Connection> &c : clients_)
        if (c && c->authenticated) {
            c->send(QJsonObject{{"t", "bye"}});
            c->flushNow(300);
        }
}

QStringList Server::onAirBackups() const
{
    QStringList out;
    for (const QPointer<Connection> &c : clients_)
        if (c && c->authenticated && c->role == QLatin1String("onair")) out << c->peer();
    return out;
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
                            {"nonce", QString::fromLatin1(c->nonce.toBase64())}});
    }
}

void Server::onMessage(Connection *c, const QJsonObject &m)
{
    if (!c->authenticated) {
        const QString t = m.value("t").toString();
        if (t == QLatin1String("login")) {
            // A felhasználó sója; ismeretlen névre is válaszolunk (véletlen sóval), hogy a nevek ne legyenek kitalálhatók.
            const QString name = m.value("user").toString().trimmed();
            c->user = name;
            c->role = m.value("role").toString();
            if (c->role != QLatin1String("backup") && c->role != QLatin1String("onair")) c->role = QStringLiteral("editor");
            c->pendingKey.clear();
            c->perms.clear();
            QByteArray salt = newSalt();
            for (const User &u : users_)
                if (u.name.compare(name, Qt::CaseInsensitive) == 0) {
                    salt = u.salt;
                    c->pendingKey = u.key;
                    c->perms = u.perms;
                    c->user = u.name;
                }
            c->send(QJsonObject{{"t", "challenge"}, {"salt", QString::fromLatin1(salt.toBase64())}});
            return;
        }
        if (t != QLatin1String("auth")) return;
        const QByteArray mac = QByteArray::fromBase64(m.value("mac").toString().toLatin1());
        if (c->pendingKey.isEmpty() || !sameBytes(mac, answer(c->pendingKey, c->nonce))) {
            c->send(QJsonObject{{"t", "auth"}, {"ok", false}, {"error", TR("Hibás felhasználónév vagy jelszó.")}});
            // Kis várakozás a bontás előtt: a jelszó próbálgatása így lassú.
            QTimer::singleShot(1500, c, [c] { c->close(); });
            return;
        }
        // A tartalék szerephez külön jog kell (a teljes hangtár másolatát kapja).
        if (c->role != QLatin1String("editor") && !c->perms.contains(QStringLiteral("backup"))) {
            c->send(QJsonObject{{"t", "auth"}, {"ok", false}, {"error", TR("Ennek a felhasználónak nincs joga tartalék playoutként csatlakozni.")}});
            QTimer::singleShot(500, c, [c] { c->close(); });
            return;
        }
        c->authenticated = true;
        c->pendingKey.clear();
        c->send(QJsonObject{{"t", "auth"}, {"ok", true}, {"user", c->user}, {"perms", QJsonArray::fromStringList(c->perms)}});
        emit clientsChanged(clientCount());
        emit authenticated(c);
        return;
    }
    emit request(c, m);
}

QStringList Server::connectedUsers() const
{
    QStringList out;
    for (const QPointer<Connection> &c : clients_)
        if (c && c->authenticated) out << QStringLiteral("%1 (%2)").arg(c->user.isEmpty() ? TR("alap jelszó") : c->user, c->peer());
    return out;
}

// ---------------------------------------------------------------- kliens

Client::Client(QObject *parent) : QObject(parent) {}

void Client::connectTo(const QString &host, quint16 port, const QString &user, const QString &password)
{
    disconnectFromHost();
    user_ = user.trimmed();
    password_ = password;
    perms_.clear();
    wasReady_ = false;
    auto *s = new QTcpSocket;
    auto *c = new Connection(s, this);
    conn_ = c;
    connect(c, &Connection::message, this, [this, c](const QJsonObject &m) {
        const QString t = m.value("t").toString();
        if (t == QLatin1String("hello")) {
            if (m.value("proto").toInt() != kProtocol) {
                // Előbb leválik, hogy a bontás ne írja felül az okot egy általános üzenettel.
                conn_ = nullptr;
                c->close();
                c->deleteLater();
                emit failed(TR("A playout más verziójú (%1) – frissítsd mindkét gépen az Étert.").arg(m.value("version").toString()));
                return;
            }
            version_ = m.value("version").toString();
            c->nonce = QByteArray::fromBase64(m.value("nonce").toString().toLatin1());
            c->send(QJsonObject{{"t", "login"}, {"user", user_}, {"role", role_}});
            return;
        }
        if (t == QLatin1String("challenge")) {
            const QByteArray salt = QByteArray::fromBase64(m.value("salt").toString().toLatin1());
            const QByteArray key = (!cacheKey_.isEmpty() && salt == cacheSalt_ && password_.isEmpty()) ? cacheKey_ : deriveKey(salt, password_);
            lastSalt_ = salt;
            lastKey_ = key;
            c->send(QJsonObject{{"t", "auth"}, {"mac", QString::fromLatin1(answer(key, c->nonce).toBase64())}});
            return;
        }
        if (t == QLatin1String("auth")) {
            if (!m.value("ok").toBool()) {
                const QString error = m.value("error").toString(TR("A belépés nem sikerült."));
                conn_ = nullptr;
                c->close();
                c->deleteLater();
                emit failed(error);
                return;
            }
            c->authenticated = true;
            wasReady_ = true;
            perms_.clear();
            for (const QJsonValue &v : m.value("perms").toArray()) perms_ << v.toString();
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
