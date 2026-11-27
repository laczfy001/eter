#include "core/BadFiles.h"

#include "audio/Decoder.h"
#include "core/CrashReport.h"
#include "core/ErrorLog.h"
#include "i18n.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QSettings>

namespace badfiles {
namespace {

QMutex g_mutex;
QHash<QString, Entry> g_entries;
QString g_new;

const QString kKey = QStringLiteral("decoder/badFiles");

void load()
{
    g_entries.clear();
    const QJsonArray arr = QJsonDocument::fromJson(QSettings().value(kKey).toByteArray()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.path = o.value("path").toString();
        e.size = qint64(o.value("size").toDouble(-1));
        e.mtime = qint64(o.value("mtime").toDouble(0));
        e.at = o.value("at").toString();
        if (!e.path.isEmpty()) g_entries.insert(e.path, e);
    }
}

void save()
{
    QJsonArray arr;
    for (const Entry &e : std::as_const(g_entries)) {
        QJsonObject o;
        o["path"] = e.path;
        o["size"] = double(e.size);
        o["mtime"] = double(e.mtime);
        o["at"] = e.at;
        arr << o;
    }
    QSettings().setValue(kKey, QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

// A dekóder hívja (bármely szálról): ki kell-e hagyni ezt a fájlt.
bool guard(const std::string &path)
{
    const QString p = QString::fromStdString(path);
    Entry e;
    {
        QMutexLocker lk(&g_mutex);
        auto it = g_entries.constFind(p);
        if (it == g_entries.cend()) return false;
        e = it.value();
    }
    // Ha a fájl azóta megváltozott (kicserélték, javították), újra megpróbálja.
    const QFileInfo fi(p);
    if (!fi.exists()) return false;
    return fi.size() == e.size && fi.lastModified().toMSecsSinceEpoch() == e.mtime;
}

} // namespace

void init()
{
    {
        QMutexLocker lk(&g_mutex);
        load();
    }
    const QString suspect = crash::suspectFilePath();
    if (!suspect.isEmpty() && QFile::exists(suspect)) {
        QFile f(suspect);
        QString path;
        if (f.open(QIODevice::ReadOnly)) path = QString::fromUtf8(f.readAll()).trimmed();
        f.close();
        QFile::remove(suspect);
        if (!path.isEmpty()) {
            const QFileInfo fi(path);
            Entry e;
            e.path = path;
            e.size = fi.exists() ? fi.size() : -1;
            e.mtime = fi.exists() ? fi.lastModified().toMSecsSinceEpoch() : 0;
            e.at = QDateTime::currentDateTime().toString(Qt::ISODate);
            {
                QMutexLocker lk(&g_mutex);
                g_entries.insert(path, e);
                save();
                g_new = path;
            }
            elog::error(QStringLiteral("Hangfájl"),
                        TR("Az előző futás ennek a fájlnak a dekódolása közben omlott össze: %1. A program mostantól kihagyja "
                           "(nem játssza le és nem elemzi). Ha a fájlt kicserélted vagy javítottad, magától újra próbálja. "
                           "A lista: Súgó → Kihagyott fájlok.").arg(path));
        }
    }
    eter::Decoder::setOpenGuard(&guard);
}

QString takeNewlyAdded()
{
    QMutexLocker lk(&g_mutex);
    QString s = g_new;
    g_new.clear();
    return s;
}

QVector<Entry> list()
{
    QMutexLocker lk(&g_mutex);
    QVector<Entry> out;
    for (const Entry &e : std::as_const(g_entries)) out << e;
    std::sort(out.begin(), out.end(), [](const Entry &a, const Entry &b) { return a.at > b.at; });
    return out;
}

void clear()
{
    QMutexLocker lk(&g_mutex);
    g_entries.clear();
    save();
}

} // namespace badfiles
