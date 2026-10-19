// A főablak: hangfájlok feltöltése a távoli szerkesztőkből (új hangtárelem vagy voice track). A fájl darabokban érkezik,
// minden darabot nyugtázunk; a végén a playout ellenőrzi, hogy hangfájl-e, és felveszi a hangtárba.
#include "MainWindow.h"
#include "audio/Decoder.h"
#include "i18n.h"
#include "Library.h"
#include "net/Remote.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace {
constexpr qint64 kMaxUpload = qint64(1024) * 1024 * 1024;     // 1 GB

QString safeName(QString name)
{
    // Csak a fájlnév (útvonal nélkül), a fájlrendszerekben tiltott jelek nélkül.
    name = QFileInfo(name.replace(QLatin1Char('\\'), QLatin1Char('/'))).fileName();
    static const QString bad = QStringLiteral("<>:\"/\\|?*");
    QString out;
    for (const QChar ch : name) out += (ch.unicode() < 32 || bad.contains(ch)) ? QChar('_') : ch;
    out = out.trimmed();
    while (out.startsWith(QLatin1Char('.'))) out.remove(0, 1);
    return out.left(180);
}
}

QString MainWindow::uploadFolder()
{
    QString dir = QSettings().value("remote/uploadDir").toString();
    if (dir.isEmpty()) {
        QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
        if (music.isEmpty()) music = QDir::homePath();
        dir = QDir(music).filePath(QStringLiteral("Eter-feltoltesek"));
    }
    return dir;
}

QString MainWindow::applyUpload(const QJsonObject &m, remote::Connection *c, QJsonObject *reply)
{
    const QString op = m.value("op").toString();
    // A megszakadt kapcsolatok félkész feltöltései törlődnek.
    for (auto it = uploads_.begin(); it != uploads_.end();) {
        if (!it->conn) {
            if (it->file) it->file->remove();
            it = uploads_.erase(it);
        } else {
            ++it;
        }
    }
    if (op == QLatin1String("uploadBegin")) {
        const QString name = safeName(m.value("name").toString());
        const qint64 size = m.value("size").toInteger();
        const bool vt = m.value("vt").toBool();
        if (name.isEmpty()) return TR("Hiányzik a fájlnév.");
        if (!audioExtensions().contains(QFileInfo(name).suffix().toLower())) return TR("Ez nem hangfájl: %1").arg(name);
        if (size <= 0 || size > kMaxUpload) return TR("A fájl túl nagy (legfeljebb 1 GB).");
        int categoryId = m.value("category").toInt();
        if (vt) categoryId = db_->voiceTrackCategory();
        bool okCategory = false;
        for (const Category &cat : db_->itemCategories())
            if (cat.id == categoryId) okCategory = true;
        if (!okCategory) return TR("Nincs ilyen kategória a playouton.");
        QDir dir(uploadFolder());
        if (vt) dir.setPath(dir.filePath(QStringLiteral("voicetracks")));
        if (!dir.mkpath(QStringLiteral("."))) return TR("A feltöltési mappa nem hozható létre: %1").arg(dir.path());
        // Ha már van ilyen nevű fájl, sorszámot kap.
        QString path = dir.filePath(name);
        for (int n = 2; QFileInfo::exists(path) && n < 1000; ++n)
            path = dir.filePath(QStringLiteral("%1 (%2).%3").arg(QFileInfo(name).completeBaseName()).arg(n).arg(QFileInfo(name).suffix()));
        auto file = std::make_shared<QFile>(path);
        if (!file->open(QIODevice::WriteOnly)) return TR("A fájl nem írható a playouton: %1").arg(path);
        Upload u;
        u.conn = c;
        u.file = file;
        u.path = path;
        u.name = name;
        u.title = m.value("title").toString().trimmed().left(200);
        u.size = size;
        u.categoryId = categoryId;
        u.voiceTrack = vt;
        const int token = nextUpload_++;
        uploads_.insert(token, u);
        if (reply) reply->insert("upload", token);
        return {};
    }
    const int token = m.value("upload").toInt();
    auto it = uploads_.find(token);
    if (it == uploads_.end() || it->conn != c) return TR("Ismeretlen feltöltés.");
    if (op == QLatin1String("uploadCancel")) {
        it->file->remove();
        uploads_.erase(it);
        return {};
    }
    if (op == QLatin1String("uploadChunk")) {
        const QByteArray data = QByteArray::fromBase64(m.value("data").toString().toLatin1());
        if (m.value("offset").toInteger() != it->received) return TR("A feltöltés darabjai összekeveredtek.");
        if (it->received + data.size() > it->size || it->file->write(data) != data.size()) {
            it->file->remove();
            uploads_.erase(it);
            return TR("A fájl írása nem sikerült a playouton (megtelt a lemez?).");
        }
        it->received += data.size();
        if (reply) reply->insert("received", it->received);
        return {};
    }
    if (op == QLatin1String("uploadEnd")) {
        Upload u = *it;
        uploads_.erase(it);
        u.file->close();
        if (u.received != u.size) {
            u.file->remove();
            return TR("A feltöltés nem teljes.");
        }
        eter::Decoder dec;
        if (!dec.open(u.path.toStdString()) || dec.durationFrames() <= 0) {
            u.file->remove();
            return TR("A feltöltött fájl nem olvasható hangként.");
        }
        int id = db_->itemIdByPath(u.path);
        if (!id) {
            Item item;
            item.categoryId = u.categoryId;
            item.path = u.path;
            item.title = !u.title.isEmpty() ? u.title : QString::fromStdString(dec.title()).trimmed();
            if (item.title.isEmpty()) item.title = QFileInfo(u.name).completeBaseName();
            item.artist = QString::fromStdString(dec.artist()).trimmed();
            item.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            QString err;
            id = db_->addItem(item, &err);
            if (!id) {
                u.file->remove();
                return TR("Az elem nem vehető fel a hangtárba: %1").arg(err);
            }
        }
        emit db_->categoriesChanged();
        db_->notifyItemsChanged();
        if (reply) {
            const Item made = db_->item(id);
            reply->insert("item", id);
            reply->insert("path", u.path);
            reply->insert("itemData", remote::itemToJson(made));
            for (const Category &cat : db_->categories())
                if (cat.id == made.categoryId) reply->insert("category", remote::categoryToJson(cat));
        }
        say(TR("Távoli feltöltés%1: %2").arg(c && !c->user.isEmpty() ? QStringLiteral(" (%1)").arg(c->user) : QString(), u.name), 8000);
        return {};
    }
    return TR("Ismeretlen művelet: %1").arg(op);
}
