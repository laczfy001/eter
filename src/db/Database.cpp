#include "i18n.h"
#include "Database.h"
#include <QSet>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

QStringList kindKeys() { return {"music", "jingle", "ad", "news", "voicetrack", "playlist", "other"}; }

QString kindLabel(const QString &kind)
{
    if (kind == "music") return TR("Zene");
    if (kind == "jingle") return TR("Jingle");
    if (kind == "ad") return TR("Reklám");
    if (kind == "news") return TR("Hírek");
    if (kind == "voicetrack") return TR("Voice track");
    if (kind == "playlist") return TR("Playlist");
    return TR("Egyéb");
}

static bool run(QSqlQuery &q)
{
    if (q.exec()) return true;
    qWarning("SQL hiba: %s\n  %s", qPrintable(q.lastError().text()), qPrintable(q.lastQuery()));
    return false;
}

#define ETER_ITEM_COLS \
    "i.id, i.category_id, c.name, c.kind, c.color, i.title, i.artist, i.file_path, i.duration_ms, " \
    "COALESCE(i.last_played_at, 0), COALESCE(i.mix_out_ms, i.auto_mix_out_ms, 0), COALESCE(i.mix_out_ms, 0), " \
    "i.cue_in_ms, COALESCE(i.fade_out_ms, -1), COALESCE(i.fade_curve, ''), COALESCE(i.intro_ms, 0), " \
    "COALESCE(i.cue_out_ms, 0)"
static const int kItemColumns = 17;
static const char *kItemSelect = "SELECT " ETER_ITEM_COLS " FROM items i JOIN categories c ON c.id = i.category_id ";

static Item readItem(const QSqlQuery &q, int o = 0)
{
    Item it;
    it.id = q.value(o).toInt();
    it.categoryId = q.value(o + 1).toInt();
    it.categoryName = q.value(o + 2).toString();
    it.kind = q.value(o + 3).toString();
    it.color = q.value(o + 4).toString();
    it.title = q.value(o + 5).toString();
    it.artist = q.value(o + 6).toString();
    it.path = q.value(o + 7).toString();
    it.durationMs = q.value(o + 8).toLongLong();
    it.lastPlayedAt = q.value(o + 9).toLongLong();
    it.mixOutMs = q.value(o + 10).toLongLong();
    it.userMixOutMs = q.value(o + 11).toLongLong();
    it.cueInMs = q.value(o + 12).toLongLong();
    it.fadeOutMs = q.value(o + 13).toLongLong();
    it.fadeCurve = q.value(o + 14).toString();
    it.introMs = q.value(o + 15).toLongLong();
    it.cueOutMs = q.value(o + 16).toLongLong();
    return it;
}

Database::Database(QObject *parent) : QObject(parent) {}

Database::~Database()
{
    if (QSqlDatabase::contains(QSqlDatabase::defaultConnection)) {
        QSqlDatabase::database().close();
    }
}

bool Database::open(const QString &path, QString *err)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(path);
    if (!db.open()) {
        if (err) *err = db.lastError().text();
        return false;
    }
    path_ = path;
    QSqlQuery q;
    q.exec("PRAGMA foreign_keys = ON");
    q.exec("PRAGMA journal_mode = WAL");
    q.exec("PRAGMA synchronous = NORMAL");
    q.exec("PRAGMA user_version");
    const int version = q.next() ? q.value(0).toInt() : 0;
    q.finish();
    int v = version;
    if (v == 0) {
        if (!createSchema(err)) return false;
        v = 8;
    }
    if (v == 1) {
        // 2. séma: az automatikusan megtalált mix pont eltárolása az ütemező pontosabb tervezéséhez.
        q.exec("ALTER TABLE items ADD COLUMN auto_mix_out_ms INTEGER");
        q.exec("PRAGMA user_version = 2");
        v = 2;
    }
    if (v == 2) {
        // 3. séma: voice track az átmenetekhez.
        q.exec("ALTER TABLE transitions ADD COLUMN vt_item_id INTEGER REFERENCES items(id) ON DELETE SET NULL");
        q.exec("ALTER TABLE transitions ADD COLUMN vt_offset_ms INTEGER NOT NULL DEFAULT 0");
        q.exec("ALTER TABLE transitions ADD COLUMN vt_duck_db REAL NOT NULL DEFAULT -9");
        q.exec("PRAGMA user_version = 3");
        v = 3;
    }
    if (v == 3) {
        // 4. séma: a kimenő elem vége a mix ponttól függetlenül állítható.
        q.exec("ALTER TABLE transitions ADD COLUMN out_end_ms INTEGER");
        q.exec("PRAGMA user_version = 4");
        v = 4;
    }
    if (v == 4) {
        // 5. séma: kézzel készített playlistek (playlist típusú kategória, mentett listák, blokkok a fő playlistben).
        // A kategória típusának ellenőrzése csak a tábla újraépítésével bővíthető.
        q.exec("PRAGMA foreign_keys = OFF");
        bool ok = q.exec("BEGIN");
        ok = ok && q.exec("CREATE TABLE categories_new ("
                          "id INTEGER PRIMARY KEY, "
                          "kind TEXT NOT NULL CHECK (kind IN ('music','jingle','ad','news','voicetrack','playlist','other')), "
                          "name TEXT NOT NULL, color TEXT, UNIQUE (kind, name)) STRICT");
        ok = ok && q.exec("INSERT INTO categories_new (id, kind, name, color) SELECT id, kind, name, color FROM categories");
        ok = ok && q.exec("DROP TABLE categories");
        ok = ok && q.exec("ALTER TABLE categories_new RENAME TO categories");
        ok = ok && q.exec("ALTER TABLE playlist_entries ADD COLUMN group_id INTEGER");
        ok = ok && q.exec("ALTER TABLE playlist_entries ADD COLUMN group_name TEXT");
        ok = ok && q.exec("CREATE TABLE saved_playlists ("
                          "id INTEGER PRIMARY KEY, "
                          "category_id INTEGER NOT NULL REFERENCES categories(id) ON DELETE RESTRICT, "
                          "name TEXT NOT NULL, "
                          "created_at INTEGER NOT NULL DEFAULT (unixepoch()), "
                          "updated_at INTEGER NOT NULL DEFAULT (unixepoch()), "
                          "UNIQUE (category_id, name)) STRICT");
        ok = ok && q.exec("CREATE TABLE saved_playlist_items ("
                          "playlist_id INTEGER NOT NULL REFERENCES saved_playlists(id) ON DELETE CASCADE, "
                          "position INTEGER NOT NULL, "
                          "item_id INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE, "
                          "PRIMARY KEY (playlist_id, position)) STRICT, WITHOUT ROWID");
        ok = ok && q.exec("PRAGMA user_version = 5");
        if (ok) ok = q.exec("COMMIT");
        if (!ok) {
            const QString text = q.lastError().text();
            q.exec("ROLLBACK");
            q.exec("PRAGMA foreign_keys = ON");
            if (err) *err = TR("Az adatbázis frissítése nem sikerült: %1").arg(text);
            return false;
        }
        q.exec("PRAGMA foreign_keys = ON");
        v = 5;
    }
    if (v == 5) {
        // 6. séma: cartwall.
        q.exec("CREATE TABLE cartwalls (id INTEGER PRIMARY KEY, name TEXT NOT NULL, color TEXT, "
               "position INTEGER NOT NULL DEFAULT 0) STRICT");
        q.exec("CREATE TABLE carts ("
               "wall_id INTEGER NOT NULL REFERENCES cartwalls(id) ON DELETE CASCADE, "
               "slot INTEGER NOT NULL, "
               "item_id INTEGER REFERENCES items(id) ON DELETE CASCADE, "
               "file_path TEXT, label TEXT NOT NULL DEFAULT '', duration_ms INTEGER NOT NULL DEFAULT 0, "
               "PRIMARY KEY (wall_id, slot)) STRICT, WITHOUT ROWID");
        q.exec("PRAGMA user_version = 6");
        v = 6;
    }
    if (v == 6) {
        // 7. séma: időzített események.
        q.exec("CREATE TABLE timed_events ("
               "id INTEGER PRIMARY KEY, "
               "enabled INTEGER NOT NULL DEFAULT 1 CHECK (enabled IN (0,1)), "
               "item_id INTEGER REFERENCES items(id) ON DELETE CASCADE, "
               "saved_playlist_id INTEGER REFERENCES saved_playlists(id) ON DELETE CASCADE, "
               "action TEXT NOT NULL DEFAULT 'next' CHECK (action IN ('next','end','start')), "
               "time_s INTEGER NOT NULL CHECK (time_s BETWEEN 0 AND 86399), "
               "on_date TEXT, "
               "weekdays INTEGER NOT NULL DEFAULT 0, "
               "last_fired_at INTEGER, "
               "CHECK ((item_id IS NULL) <> (saved_playlist_id IS NULL))) STRICT");
        q.exec("PRAGMA user_version = 7");
        v = 7;
    }
    if (v == 7) {
        // 8. séma: az átmenetre ráfektetett playlist-elemek.
        q.exec("ALTER TABLE playlist_entries ADD COLUMN overlay_offset_ms INTEGER");
        q.exec("ALTER TABLE playlist_entries ADD COLUMN overlay_duck_db REAL NOT NULL DEFAULT 0");
        q.exec("PRAGMA user_version = 8");
        v = 8;
    }
    housekeeping();
    return true;
}

bool Database::createSchema(QString *err)
{
    QFile f(":/schema.sql");
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = TR("A séma nem olvasható");
        return false;
    }
    // Megjegyzések ki, majd utasításonként futtatjuk.
    QString sql;
    for (const QString &line : QString::fromUtf8(f.readAll()).split('\n')) {
        const int c = line.indexOf("--");
        sql += (c >= 0 ? line.left(c) : line) + '\n';
    }
    QSqlQuery q;
    for (const QString &stmt : sql.split(';', Qt::SkipEmptyParts)) {
        const QString s = stmt.trimmed();
        if (s.isEmpty()) continue;
        if (!q.exec(s)) {
            if (err) *err = q.lastError().text() + "\n" + s.left(80);
            q.exec("ROLLBACK");
            return false;
        }
    }
    return true;
}

void Database::housekeeping()
{
    // Előző futásból megmaradt, már nem várakozó sorok törlése; ami szólt, azt a napló őrzi.
    QSqlQuery q;
    q.exec("DELETE FROM playlist_entries WHERE status <> 'pending'");
    q.exec("DELETE FROM playlists WHERE id NOT IN (SELECT DISTINCT playlist_id FROM playlist_entries) "
           "AND generated_at IS NULL");
    q.exec("UPDATE play_log SET ended_at = started_at, end_reason = 'error' WHERE ended_at IS NULL");
}

void Database::setSetting(const QString &key, const QString &value)
{
    QSqlQuery q;
    q.prepare("INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    q.addBindValue(key);
    q.addBindValue(value);
    run(q);
}

QString Database::setting(const QString &key, const QString &fallback)
{
    QSqlQuery q;
    q.prepare("SELECT value FROM settings WHERE key = ?");
    q.addBindValue(key);
    return run(q) && q.next() ? q.value(0).toString() : fallback;
}

// ------------------------------------------------------------- kategóriák

QVector<Category> Database::categories()
{
    QVector<Category> out;
    QSqlQuery q("SELECT c.id, c.kind, c.name, COALESCE(c.color, ''), "
                "(SELECT COUNT(*) FROM items i WHERE i.category_id = c.id) + "
                "(SELECT COUNT(*) FROM saved_playlists s WHERE s.category_id = c.id) FROM categories c");
    while (q.next())
        out.push_back({q.value(0).toInt(), q.value(1).toString(), q.value(2).toString(), q.value(3).toString(),
                       q.value(4).toInt()});
    std::sort(out.begin(), out.end(), [](const Category &a, const Category &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return out;
}

QVector<Category> Database::itemCategories()
{
    QVector<Category> out;
    for (const Category &c : categories())
        if (c.kind != QLatin1String("playlist")) out.push_back(c);
    return out;
}

QVector<Category> Database::playlistCategories()
{
    QVector<Category> out;
    for (const Category &c : categories())
        if (c.kind == QLatin1String("playlist")) out.push_back(c);
    return out;
}

int Database::addCategory(const QString &name, const QString &kind, const QString &color, QString *err)
{
    QSqlQuery q;
    q.prepare("INSERT INTO categories (kind, name, color) VALUES (?, ?, ?)");
    q.addBindValue(kind);
    q.addBindValue(name.trimmed());
    q.addBindValue(color);
    if (!q.exec()) {
        if (err) *err = TR("Ilyen nevű kategória már van ebben a típusban.");
        return 0;
    }
    const int id = q.lastInsertId().toInt();
    emit categoriesChanged();
    return id;
}

bool Database::updateCategory(int id, const QString &name, const QString &kind, const QString &color, QString *err)
{
    QSqlQuery q;
    q.prepare("UPDATE categories SET kind = ?, name = ?, color = ? WHERE id = ?");
    q.addBindValue(kind);
    q.addBindValue(name.trimmed());
    q.addBindValue(color);
    q.addBindValue(id);
    if (!q.exec()) {
        if (err) *err = TR("Ilyen nevű kategória már van ebben a típusban.");
        return false;
    }
    emit categoriesChanged();
    emit itemsChanged();
    return true;
}

bool Database::deleteCategory(int id, QString *err)
{
    QSqlQuery q;
    q.prepare("DELETE FROM categories WHERE id = ?");
    q.addBindValue(id);
    if (!q.exec()) {
        if (err) *err = TR("A kategória nem törölhető, amíg vannak benne elemek vagy playlistek.");
        return false;
    }
    emit categoriesChanged();
    return true;
}

// ---------------------------------------------------------------- hangtár

int Database::itemIdByPath(const QString &path)
{
    QSqlQuery q;
    q.prepare("SELECT id FROM items WHERE file_path = ?");
    q.addBindValue(path);
    return run(q) && q.next() ? q.value(0).toInt() : 0;
}

int Database::addItem(const Item &item, QString *err)
{
    const QFileInfo fi(item.path);
    QSqlQuery q;
    q.prepare("INSERT INTO items (category_id, title, artist, file_path, file_size, file_mtime, duration_ms) "
              "VALUES (?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(item.categoryId);
    q.addBindValue(item.title);
    q.addBindValue(item.artist.isNull() ? QStringLiteral("") : item.artist);   // üres, de nem NULL
    q.addBindValue(item.path);
    q.addBindValue(fi.size());
    q.addBindValue(fi.lastModified().toSecsSinceEpoch());
    q.addBindValue(item.durationMs);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return 0;
    }
    return q.lastInsertId().toInt();
}

QVector<Item> Database::items()
{
    QVector<Item> out;
    QSqlQuery q(kItemSelect);
    while (q.next()) out.push_back(readItem(q));
    return out;
}

Item Database::item(int id)
{
    QSqlQuery q;
    q.prepare(QString::fromLatin1(kItemSelect) + "WHERE i.id = ?");
    q.addBindValue(id);
    return run(q) && q.next() ? readItem(q) : Item{};
}

int Database::itemCount()
{
    QSqlQuery q("SELECT COUNT(*) FROM items");
    return q.next() ? q.value(0).toInt() : 0;
}

void Database::setItemsCategory(const QVector<int> &ids, int categoryId)
{
    beginBatch();
    QSqlQuery q;
    q.prepare("UPDATE items SET category_id = ? WHERE id = ?");
    for (int id : ids) {
        q.bindValue(0, categoryId);
        q.bindValue(1, id);
        run(q);
    }
    endBatch();
    emit categoriesChanged();
    emit itemsChanged();
}

bool Database::itemInPlaylist(int itemId)
{
    QSqlQuery q;
    q.prepare("SELECT 1 FROM playlist_entries WHERE item_id = ? LIMIT 1");
    q.addBindValue(itemId);
    return run(q) && q.next();
}

void Database::itemExtra(int id, QString *album, int *year)
{
    QSqlQuery q;
    q.prepare("SELECT album, COALESCE(year, 0) FROM items WHERE id = ?");
    q.addBindValue(id);
    if (run(q) && q.next()) {
        if (album) *album = q.value(0).toString();
        if (year) *year = q.value(1).toInt();
    }
}

void Database::updateItemDetails(int id, int categoryId, const QString &title, const QString &artist, const QString &album, int year)
{
    QSqlQuery q;
    q.prepare("UPDATE items SET category_id = ?, title = ?, artist = ?, album = ?, year = ? WHERE id = ?");
    q.addBindValue(categoryId);
    q.addBindValue(title.trimmed());
    q.addBindValue(artist.trimmed().isEmpty() ? QStringLiteral("") : artist.trimmed());
    q.addBindValue(album.trimmed().isEmpty() ? QStringLiteral("") : album.trimmed());
    q.addBindValue(year > 0 ? QVariant(year) : QVariant());
    q.addBindValue(id);
    run(q);
}

void Database::setItemMarkers(int id, qint64 cueInMs, qint64 introMs, qint64 outroMs)
{
    QSqlQuery q;
    q.prepare("UPDATE items SET cue_in_ms = ?, intro_ms = ?, mix_out_ms = ? WHERE id = ?");
    q.addBindValue(std::max<qint64>(0, cueInMs));
    q.addBindValue(introMs > 0 ? QVariant(introMs) : QVariant());
    q.addBindValue(outroMs > 0 ? QVariant(outroMs) : QVariant());
    q.addBindValue(id);
    run(q);
}

bool Database::deleteItem(int id, QString *err)
{
    QSqlQuery q;
    q.prepare("DELETE FROM items WHERE id = ?");
    q.addBindValue(id);
    if (!q.exec()) {
        if (err) *err = TR("Az elem a playlistben szerepel, előbb onnan vedd ki.");
        return false;
    }
    return true;
}

void Database::setItemDuration(int id, qint64 ms)
{
    QSqlQuery q;
    q.prepare("UPDATE items SET duration_ms = ? WHERE id = ? AND duration_ms <> ?");
    q.addBindValue(ms);
    q.addBindValue(id);
    q.addBindValue(ms);
    run(q);
}

void Database::setItemAutoMix(int id, qint64 mixOutMs)
{
    QSqlQuery q;
    q.prepare("UPDATE items SET auto_mix_out_ms = ? WHERE id = ?");
    q.addBindValue(mixOutMs);
    q.addBindValue(id);
    run(q);
}

void Database::beginBatch() { QSqlDatabase::database().transaction(); }
void Database::endBatch() { QSqlDatabase::database().commit(); }

// --------------------------------------------------------------- playlist

int Database::playlistFor(const QDate &date, int hour, int *key)
{
    if (key) *key = date.toString("yyyyMMdd").toInt() * 100 + hour;
    const QString d = date.toString(Qt::ISODate);
    QSqlQuery q;
    q.prepare("SELECT id FROM playlists WHERE air_date = ? AND hour = ?");
    q.addBindValue(d);
    q.addBindValue(hour);
    if (run(q) && q.next()) return q.value(0).toInt();
    q.prepare("INSERT INTO playlists (air_date, hour) VALUES (?, ?)");
    q.addBindValue(d);
    q.addBindValue(hour);
    run(q);
    return q.lastInsertId().toInt();
}

Entry Database::addEntry(int playlistId, int itemId, const QString &source, int fixedStartS, int groupId, const QString &groupName)
{
    Entry e;
    QSqlQuery q;
    q.prepare("INSERT INTO playlist_entries (playlist_id, position, item_id, source, fixed_start_s, group_id, group_name) VALUES "
              "(?, (SELECT COALESCE(MAX(position), 0) + 1 FROM playlist_entries WHERE playlist_id = ?), ?, ?, ?, ?, ?)");
    q.addBindValue(playlistId);
    q.addBindValue(playlistId);
    q.addBindValue(itemId);
    q.addBindValue(source);
    q.addBindValue(fixedStartS >= 0 ? QVariant(fixedStartS) : QVariant());
    q.addBindValue(groupId > 0 ? QVariant(groupId) : QVariant());
    q.addBindValue(groupId > 0 ? QVariant(groupName) : QVariant());
    if (!run(q)) return e;
    e.id = q.lastInsertId().toInt();
    e.groupId = groupId;
    e.groupName = groupName;
    e.playlistId = playlistId;
    e.source = source;
    e.fixedStartS = fixedStartS;
    q.prepare("SELECT CAST(REPLACE(air_date, '-', '') AS INTEGER) * 100 + hour FROM playlists WHERE id = ?");
    q.addBindValue(playlistId);
    if (run(q) && q.next()) e.playlistKey = q.value(0).toInt();
    e.item = item(itemId);
    return e;
}

int Database::nextGroupId()
{
    // A blokk azonosítója csak a futó playlisten belül kell, hogy egyedi legyen; a beállítások között számoljuk.
    const int id = setting("last_group_id", "0").toInt() + 1;
    setSetting("last_group_id", QString::number(id));
    return id;
}

void Database::setEntryOverlay(int entryId, bool on, qint64 offsetMs, double duckDb)
{
    QSqlQuery q;
    q.prepare("UPDATE playlist_entries SET overlay_offset_ms = ?, overlay_duck_db = ? WHERE id = ?");
    q.addBindValue(on ? QVariant(offsetMs) : QVariant());
    q.addBindValue(on ? duckDb : 0.0);
    q.addBindValue(entryId);
    run(q);
}

void Database::setEntryGroup(int entryId, int groupId, const QString &groupName)
{
    QSqlQuery q;
    q.prepare("UPDATE playlist_entries SET group_id = ?, group_name = ? WHERE id = ?");
    q.addBindValue(groupId > 0 ? QVariant(groupId) : QVariant());
    q.addBindValue(groupId > 0 ? QVariant(groupName) : QVariant());
    q.addBindValue(entryId);
    run(q);
}

// -------------------------------------------------------- időzített események

QVector<TimedEvent> Database::timedEvents()
{
    QVector<TimedEvent> out;
    QSqlQuery q("SELECT e.id, e.enabled, COALESCE(e.item_id, 0), COALESCE(e.saved_playlist_id, 0), e.action, e.time_s, "
                "COALESCE(e.on_date, ''), e.weekdays, COALESCE(e.last_fired_at, 0), COALESCE(i.title, p.name, '') "
                "FROM timed_events e LEFT JOIN items i ON i.id = e.item_id "
                "LEFT JOIN saved_playlists p ON p.id = e.saved_playlist_id ORDER BY e.time_s, e.id");
    while (q.next()) {
        TimedEvent e;
        e.id = q.value(0).toInt();
        e.enabled = q.value(1).toBool();
        e.itemId = q.value(2).toInt();
        e.playlistId = q.value(3).toInt();
        e.action = q.value(4).toString();
        e.timeS = q.value(5).toInt();
        e.date = QDate::fromString(q.value(6).toString(), Qt::ISODate);
        e.weekdays = q.value(7).toInt();
        e.lastFiredAt = q.value(8).toLongLong();
        e.label = q.value(9).toString();
        out.push_back(e);
    }
    return out;
}

int Database::saveTimedEvent(const TimedEvent &e)
{
    QSqlQuery q;
    if (e.id > 0)
        q.prepare("UPDATE timed_events SET enabled = ?, item_id = ?, saved_playlist_id = ?, action = ?, time_s = ?, "
                  "on_date = ?, weekdays = ?, last_fired_at = ? WHERE id = ?");
    else
        q.prepare("INSERT INTO timed_events (enabled, item_id, saved_playlist_id, action, time_s, on_date, weekdays, last_fired_at) "
                  "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(e.enabled ? 1 : 0);
    q.addBindValue(e.itemId > 0 ? QVariant(e.itemId) : QVariant());
    q.addBindValue(e.itemId > 0 || e.playlistId <= 0 ? QVariant() : QVariant(e.playlistId));
    q.addBindValue(e.action);
    q.addBindValue(std::clamp(e.timeS, 0, 86399));
    q.addBindValue(e.date.isValid() ? QVariant(e.date.toString(Qt::ISODate)) : QVariant());
    q.addBindValue(e.weekdays);
    q.addBindValue(e.lastFiredAt > 0 ? QVariant(e.lastFiredAt) : QVariant());
    if (e.id > 0) q.addBindValue(e.id);
    if (!run(q)) return 0;
    const int id = e.id > 0 ? e.id : q.lastInsertId().toInt();
    emit timedEventsChanged();
    return id;
}

void Database::deleteTimedEvent(int id)
{
    QSqlQuery q;
    q.prepare("DELETE FROM timed_events WHERE id = ?");
    q.addBindValue(id);
    run(q);
    emit timedEventsChanged();
}

void Database::markEventFired(int id, qint64 occurrence, bool disable)
{
    QSqlQuery q;
    q.prepare(disable ? "UPDATE timed_events SET last_fired_at = ?, enabled = 0 WHERE id = ?"
                      : "UPDATE timed_events SET last_fired_at = ? WHERE id = ?");
    q.addBindValue(occurrence);
    q.addBindValue(id);
    run(q);
    emit timedEventsChanged();
}

// ---------------------------------------------------------------- cartwall

QVector<Cartwall> Database::cartwalls()
{
    QVector<Cartwall> out;
    QSqlQuery q("SELECT id, name, COALESCE(color, '') FROM cartwalls ORDER BY position, id");
    while (q.next()) out.push_back({q.value(0).toInt(), q.value(1).toString(), q.value(2).toString()});
    return out;
}

int Database::addCartwall(const QString &name, const QString &color)
{
    QSqlQuery q;
    q.prepare("INSERT INTO cartwalls (name, color, position) VALUES (?, ?, (SELECT COALESCE(MAX(position), 0) + 1 FROM cartwalls))");
    q.addBindValue(name.trimmed());
    q.addBindValue(color);
    return run(q) ? q.lastInsertId().toInt() : 0;
}

void Database::updateCartwall(int id, const QString &name, const QString &color)
{
    QSqlQuery q;
    q.prepare("UPDATE cartwalls SET name = ?, color = ? WHERE id = ?");
    q.addBindValue(name.trimmed());
    q.addBindValue(color);
    q.addBindValue(id);
    run(q);
}

void Database::deleteCartwall(int id)
{
    QSqlQuery q;
    q.prepare("DELETE FROM cartwalls WHERE id = ?");
    q.addBindValue(id);
    run(q);
}

QVector<Cart> Database::carts(int wallId)
{
    QVector<Cart> out;
    QSqlQuery q;
    // Hangtárelemnél az útvonal és a hossz mindig a hangtárból jön; a felirat felülírható.
    q.prepare("SELECT c.slot, COALESCE(c.item_id, 0), COALESCE(i.file_path, c.file_path, ''), "
              "CASE WHEN c.label <> '' THEN c.label ELSE COALESCE(i.title, '') END, "
              "COALESCE(i.duration_ms, c.duration_ms) "
              "FROM carts c LEFT JOIN items i ON i.id = c.item_id WHERE c.wall_id = ? ORDER BY c.slot");
    q.addBindValue(wallId);
    if (run(q))
        while (q.next()) {
            Cart c;
            c.slot = q.value(0).toInt();
            c.itemId = q.value(1).toInt();
            c.path = q.value(2).toString();
            c.label = q.value(3).toString();
            c.durationMs = q.value(4).toLongLong();
            out.push_back(c);
        }
    return out;
}

void Database::setCart(int wallId, const Cart &cart)
{
    QSqlQuery q;
    q.prepare("INSERT INTO carts (wall_id, slot, item_id, file_path, label, duration_ms) VALUES (?, ?, ?, ?, ?, ?) "
              "ON CONFLICT(wall_id, slot) DO UPDATE SET item_id = excluded.item_id, file_path = excluded.file_path, "
              "label = excluded.label, duration_ms = excluded.duration_ms");
    q.addBindValue(wallId);
    q.addBindValue(cart.slot);
    q.addBindValue(cart.itemId > 0 ? QVariant(cart.itemId) : QVariant());
    q.addBindValue(cart.itemId > 0 ? QVariant() : QVariant(cart.path));
    q.addBindValue(cart.label.isEmpty() ? QStringLiteral("") : cart.label);   // üres, de nem NULL
    q.addBindValue(cart.durationMs);
    run(q);
}

void Database::clearCart(int wallId, int slot)
{
    QSqlQuery q;
    q.prepare("DELETE FROM carts WHERE wall_id = ? AND slot = ?");
    q.addBindValue(wallId);
    q.addBindValue(slot);
    run(q);
}

// ------------------------------------------------------ mentett playlistek

QVector<SavedPlaylist> Database::savedPlaylists()
{
    QVector<SavedPlaylist> out;
    QSqlQuery q("SELECT s.id, s.category_id, s.name, c.name, COALESCE(c.color, ''), "
                "(SELECT COUNT(*) FROM saved_playlist_items x WHERE x.playlist_id = s.id), "
                "(SELECT COALESCE(SUM(i.duration_ms), 0) FROM saved_playlist_items x JOIN items i ON i.id = x.item_id "
                " WHERE x.playlist_id = s.id) "
                "FROM saved_playlists s JOIN categories c ON c.id = s.category_id");
    while (q.next()) {
        SavedPlaylist p;
        p.id = q.value(0).toInt();
        p.categoryId = q.value(1).toInt();
        p.name = q.value(2).toString();
        p.categoryName = q.value(3).toString();
        p.color = q.value(4).toString();
        p.count = q.value(5).toInt();
        p.durationMs = q.value(6).toLongLong();
        out.push_back(p);
    }
    std::sort(out.begin(), out.end(), [](const SavedPlaylist &a, const SavedPlaylist &b) {
        const int c = QString::localeAwareCompare(a.categoryName, b.categoryName);
        return c != 0 ? c < 0 : QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return out;
}

QVector<Item> Database::savedPlaylistItems(int playlistId)
{
    QVector<Item> out;
    QSqlQuery q;
    q.prepare(QStringLiteral("SELECT " ETER_ITEM_COLS " FROM saved_playlist_items x JOIN items i ON i.id = x.item_id "
                             "JOIN categories c ON c.id = i.category_id WHERE x.playlist_id = ? ORDER BY x.position"));
    q.addBindValue(playlistId);
    if (run(q))
        while (q.next()) out.push_back(readItem(q, 0));
    return out;
}

int Database::saveSavedPlaylist(int id, int categoryId, const QString &name, const QVector<int> &itemIds, QString *err)
{
    QSqlQuery q;
    q.exec("BEGIN");
    if (id > 0) {
        q.prepare("UPDATE saved_playlists SET category_id = ?, name = ?, updated_at = unixepoch() WHERE id = ?");
        q.addBindValue(categoryId);
        q.addBindValue(name.trimmed());
        q.addBindValue(id);
    } else {
        q.prepare("INSERT INTO saved_playlists (category_id, name) VALUES (?, ?)");
        q.addBindValue(categoryId);
        q.addBindValue(name.trimmed());
    }
    if (!q.exec()) {
        q.exec("ROLLBACK");
        if (err) *err = TR("Ilyen nevű playlist már van ebben a kategóriában.");
        return 0;
    }
    if (id <= 0) id = q.lastInsertId().toInt();
    q.prepare("DELETE FROM saved_playlist_items WHERE playlist_id = ?");
    q.addBindValue(id);
    run(q);
    int pos = 0;
    for (int itemId : itemIds) {
        q.prepare("INSERT INTO saved_playlist_items (playlist_id, position, item_id) VALUES (?, ?, ?)");
        q.addBindValue(id);
        q.addBindValue(++pos);
        q.addBindValue(itemId);
        run(q);
    }
    q.exec("COMMIT");
    emit savedPlaylistsChanged();
    emit categoriesChanged();
    return id;
}

void Database::deleteSavedPlaylist(int id)
{
    QSqlQuery q;
    q.prepare("DELETE FROM saved_playlists WHERE id = ?");
    q.addBindValue(id);
    run(q);
    emit savedPlaylistsChanged();
    emit categoriesChanged();
}

bool Database::playlistGenerated(int playlistId)
{
    QSqlQuery q;
    q.prepare("SELECT generated_at IS NOT NULL FROM playlists WHERE id = ?");
    q.addBindValue(playlistId);
    return run(q) && q.next() && q.value(0).toBool();
}

void Database::markGenerated(int playlistId, int clockId)
{
    QSqlQuery q;
    q.prepare("UPDATE playlists SET generated_at = unixepoch(), clock_id = ? WHERE id = ?");
    q.addBindValue(clockId);
    q.addBindValue(playlistId);
    run(q);
}

static const char *kKeyExpr = "(CAST(REPLACE(air_date, '-', '') AS INTEGER) * 100 + hour)";

void Database::deleteSchedulerEntriesFrom(int fromKey)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM playlist_entries WHERE source = 'scheduler' AND status = 'pending' "
                             "AND playlist_id IN (SELECT id FROM playlists WHERE %1 >= ?)").arg(kKeyExpr));
    q.addBindValue(fromKey);
    run(q);
    q.prepare(QStringLiteral("UPDATE playlists SET generated_at = NULL WHERE %1 >= ?").arg(kKeyExpr));
    q.addBindValue(fromKey);
    run(q);
}

void Database::purgeSchedulerEntriesBefore(int beforeKey)
{
    QSqlQuery q;
    q.prepare(QStringLiteral("DELETE FROM playlist_entries WHERE source = 'scheduler' AND status = 'pending' "
                             "AND playlist_id IN (SELECT id FROM playlists WHERE %1 < ?)").arg(kKeyExpr));
    q.addBindValue(beforeKey);
    run(q);
}

void Database::setEntryStatus(int entryId, const QString &status)
{
    QSqlQuery q;
    q.prepare("UPDATE playlist_entries SET status = ? WHERE id = ?");
    q.addBindValue(status);
    q.addBindValue(entryId);
    run(q);
}

void Database::deleteEntry(int entryId)
{
    QSqlQuery q;
    q.prepare("DELETE FROM playlist_entries WHERE id = ?");
    q.addBindValue(entryId);
    run(q);
}

void Database::saveOrder(const QVector<QPair<int, int>> &rows)
{
    // Két menetben, hogy az (óra, pozíció) egyediség közben se sérüljön.
    beginBatch();
    QSqlQuery q;
    q.prepare("UPDATE playlist_entries SET playlist_id = ?, position = ? WHERE id = ?");
    for (int pass = 0; pass < 2; ++pass) {
        QHash<int, int> counter;
        for (const auto &r : rows) {
            const int pos = ++counter[r.second];
            q.bindValue(0, r.second);
            q.bindValue(1, pass == 0 ? -r.first : pos);   // átmenetileg −azonosító: biztosan egyedi
            q.bindValue(2, r.first);
            run(q);
        }
    }
    endBatch();
}

QVector<Entry> Database::pendingEntries()
{
    QVector<Entry> out;
    QSqlQuery q(QStringLiteral(
        "SELECT e.id, e.playlist_id, CAST(REPLACE(p.air_date, '-', '') AS INTEGER) * 100 + p.hour, "
        ETER_ITEM_COLS ", e.source, COALESCE(e.fixed_start_s, -1), COALESCE(e.group_id, 0), COALESCE(e.group_name, ''), "
        "e.overlay_offset_ms, e.overlay_duck_db "
        "FROM playlist_entries e JOIN playlists p ON p.id = e.playlist_id "
        "JOIN items i ON i.id = e.item_id JOIN categories c ON c.id = i.category_id "
        "WHERE e.status = 'pending' ORDER BY p.air_date, p.hour, e.position"));
    while (q.next()) {
        Entry e;
        e.id = q.value(0).toInt();
        e.playlistId = q.value(1).toInt();
        e.playlistKey = q.value(2).toInt();
        e.item = readItem(q, 3);
        e.source = q.value(3 + kItemColumns).toString();
        e.fixedStartS = q.value(4 + kItemColumns).toInt();
        e.groupId = q.value(5 + kItemColumns).toInt();
        e.groupName = q.value(6 + kItemColumns).toString();
        e.overlay = !q.value(7 + kItemColumns).isNull();
        e.overlayOffsetMs = q.value(7 + kItemColumns).toLongLong();
        e.overlayDuckDb = q.value(8 + kItemColumns).toDouble();
        out.push_back(e);
    }
    return out;
}

// ------------------------------------------------------------- adásnapló

int Database::logStart(const Item &item, int entryId, int player, const QString &mode)
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    QSqlQuery q;
    q.prepare("INSERT INTO play_log (started_at, item_id, entry_id, kind, title, artist, player, mode) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
    q.addBindValue(now);
    q.addBindValue(item.id);
    q.addBindValue(entryId > 0 ? QVariant(entryId) : QVariant());
    q.addBindValue(item.kind);
    q.addBindValue(item.title);
    q.addBindValue(item.artist);
    q.addBindValue(player);
    q.addBindValue(mode);
    if (!run(q)) return 0;
    const int id = q.lastInsertId().toInt();
    q.prepare("UPDATE items SET last_played_at = ?, play_count = play_count + 1 WHERE id = ?");
    q.addBindValue(now);
    q.addBindValue(item.id);
    run(q);
    emit itemsChanged();
    return id;
}

void Database::logEnd(int logId, const QString &reason)
{
    if (logId <= 0) return;
    QSqlQuery q;
    q.prepare("UPDATE play_log SET ended_at = ?, end_reason = ? WHERE id = ?");
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.addBindValue(reason);
    q.addBindValue(logId);
    run(q);
}

QVector<LogRow> Database::log(int limit)
{
    QVector<LogRow> out;
    QSqlQuery q;
    q.prepare("SELECT started_at, COALESCE(ended_at, 0), kind, title, artist, mode, COALESCE(end_reason, ''), player "
              "FROM play_log ORDER BY started_at DESC, id DESC LIMIT ?");
    q.addBindValue(limit);
    if (!run(q)) return out;
    while (q.next()) {
        LogRow r;
        r.startedAt = q.value(0).toLongLong();
        r.endedAt = q.value(1).toLongLong();
        r.kind = q.value(2).toString();
        r.title = q.value(3).toString();
        r.artist = q.value(4).toString();
        r.mode = q.value(5).toString();
        r.endReason = q.value(6).toString();
        r.player = q.value(7).toInt();
        out.push_back(r);
    }
    return out;
}

// ---------------------------------------------------------------- ütemező

static qint64 gClockOffsetMs = 0;
QDateTime eter::now() { return QDateTime::currentDateTime().addMSecs(gClockOffsetMs); }
void eter::setClockOffsetMs(qint64 ms) { gClockOffsetMs = ms; }

QVector<Clock> Database::clocks()
{
    QVector<Clock> out;
    QSqlQuery q("SELECT id, name, COALESCE(color, '') FROM clocks");
    while (q.next()) out.push_back({q.value(0).toInt(), q.value(1).toString(), q.value(2).toString()});
    std::sort(out.begin(), out.end(), [](const Clock &a, const Clock &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    return out;
}

int Database::addClock(const QString &name, const QString &color, QString *err)
{
    QSqlQuery q;
    q.prepare("INSERT INTO clocks (name, color) VALUES (?, ?)");
    q.addBindValue(name.trimmed());
    q.addBindValue(color);
    if (!q.exec()) {
        if (err) *err = TR("Ilyen nevű óraséma már van.");
        return 0;
    }
    const int id = q.lastInsertId().toInt();
    emit clocksChanged();
    return id;
}

bool Database::updateClock(int id, const QString &name, const QString &color, QString *err)
{
    QSqlQuery q;
    q.prepare("UPDATE clocks SET name = ?, color = ? WHERE id = ?");
    q.addBindValue(name.trimmed());
    q.addBindValue(color);
    q.addBindValue(id);
    if (!q.exec()) {
        if (err) *err = TR("Ilyen nevű óraséma már van.");
        return false;
    }
    emit clocksChanged();
    return true;
}

void Database::deleteClock(int id)
{
    QSqlQuery q;
    for (const char *sql : {"DELETE FROM week_grid WHERE clock_id = ?", "DELETE FROM grid_overrides WHERE clock_id = ?",
                            "DELETE FROM clocks WHERE id = ?"}) {
        q.prepare(sql);
        q.addBindValue(id);
        run(q);
    }
    emit clocksChanged();
}

int Database::duplicateClock(int id, const QString &newName)
{
    QString color;
    for (const Clock &c : clocks())
        if (c.id == id) color = c.color;
    const int copy = addClock(newName, color);
    if (!copy) return 0;
    for (ClockSlot s : clockSlots(id)) {
        s.clockId = copy;
        addSlot(s);
    }
    emit clocksChanged();
    return copy;
}

QVector<ClockSlot> Database::clockSlots(int clockId)
{
    QVector<ClockSlot> out;
    QSqlQuery q;
    q.prepare("SELECT s.id, s.position, s.slot_type, COALESCE(s.category_id, 0), COALESCE(s.item_id, 0), s.item_count, "
              "COALESCE(s.fixed_offset_s, -1), c.name, c.kind, c.color, i.title, i.artist, ic.kind, ic.color "
              "FROM clock_slots s LEFT JOIN categories c ON c.id = s.category_id "
              "LEFT JOIN items i ON i.id = s.item_id LEFT JOIN categories ic ON ic.id = i.category_id "
              "WHERE s.clock_id = ? ORDER BY s.position");
    q.addBindValue(clockId);
    if (!run(q)) return out;
    while (q.next()) {
        ClockSlot s;
        s.id = q.value(0).toInt();
        s.clockId = clockId;
        s.position = q.value(1).toInt();
        s.type = q.value(2).toString();
        s.categoryId = q.value(3).toInt();
        s.itemId = q.value(4).toInt();
        s.count = q.value(5).toInt();
        s.fixedOffsetS = q.value(6).toInt();
        if (s.type == "item") {
            const QString artist = q.value(11).toString();
            s.label = q.value(10).toString() + (artist.isEmpty() ? QString() : QStringLiteral(" – ") + artist);
            s.kind = q.value(12).toString();
            s.color = q.value(13).toString();
        } else {
            s.label = q.value(7).toString();
            s.kind = q.value(8).toString();
            s.color = q.value(9).toString();
        }
        out.push_back(s);
    }
    return out;
}

int Database::addSlot(const ClockSlot &slot)
{
    QSqlQuery q;
    q.prepare("INSERT INTO clock_slots (clock_id, position, slot_type, category_id, item_id, item_count, fixed_offset_s) "
              "VALUES (?, (SELECT COALESCE(MAX(position), 0) + 1 FROM clock_slots WHERE clock_id = ?), ?, ?, ?, ?, ?)");
    q.addBindValue(slot.clockId);
    q.addBindValue(slot.clockId);
    q.addBindValue(slot.type);
    q.addBindValue(slot.type != "item" && slot.categoryId ? QVariant(slot.categoryId) : QVariant());
    q.addBindValue(slot.type == "item" && slot.itemId ? QVariant(slot.itemId) : QVariant());
    q.addBindValue(std::max(1, slot.count));
    q.addBindValue(slot.fixedOffsetS >= 0 ? QVariant(slot.fixedOffsetS) : QVariant());
    if (!run(q)) return 0;
    return q.lastInsertId().toInt();
}

void Database::updateSlot(const ClockSlot &slot)
{
    QSqlQuery q;
    q.prepare("UPDATE clock_slots SET slot_type = ?, category_id = ?, item_id = ?, item_count = ?, fixed_offset_s = ? "
              "WHERE id = ?");
    q.addBindValue(slot.type);
    q.addBindValue(slot.type != "item" && slot.categoryId ? QVariant(slot.categoryId) : QVariant());
    q.addBindValue(slot.type == "item" && slot.itemId ? QVariant(slot.itemId) : QVariant());
    q.addBindValue(std::max(1, slot.count));
    q.addBindValue(slot.fixedOffsetS >= 0 ? QVariant(slot.fixedOffsetS) : QVariant());
    q.addBindValue(slot.id);
    run(q);
}

void Database::deleteSlot(int slotId)
{
    QSqlQuery q;
    q.prepare("DELETE FROM clock_slots WHERE id = ?");
    q.addBindValue(slotId);
    run(q);
}

void Database::reorderSlots(int clockId, const QVector<int> &slotIds)
{
    Q_UNUSED(clockId);
    beginBatch();
    QSqlQuery q;
    q.prepare("UPDATE clock_slots SET position = ? WHERE id = ?");
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < slotIds.size(); ++i) {
            q.bindValue(0, pass == 0 ? -slotIds[i] : i + 1);
            q.bindValue(1, slotIds[i]);
            run(q);
        }
    }
    endBatch();
}

QHash<int, int> Database::weekGrid()
{
    QHash<int, int> out;
    QSqlQuery q("SELECT weekday, hour, clock_id FROM week_grid");
    while (q.next()) out.insert(q.value(0).toInt() * 100 + q.value(1).toInt(), q.value(2).toInt());
    return out;
}

void Database::setGrid(int weekday, int hour, int clockId)
{
    QSqlQuery q;
    if (clockId > 0) {
        q.prepare("INSERT INTO week_grid (weekday, hour, clock_id) VALUES (?, ?, ?) "
                  "ON CONFLICT(weekday, hour) DO UPDATE SET clock_id = excluded.clock_id");
        q.addBindValue(weekday);
        q.addBindValue(hour);
        q.addBindValue(clockId);
    } else {
        q.prepare("DELETE FROM week_grid WHERE weekday = ? AND hour = ?");
        q.addBindValue(weekday);
        q.addBindValue(hour);
    }
    run(q);
}

// -------------------------------------------------------------------- mix

Transition Database::transition(int toEntryId)
{
    Transition t;
    QSqlQuery q;
    q.prepare("SELECT from_entry_id, mix_out_ms, fade_out_ms, fade_out_curve, cue_in_ms, fade_in_ms, start_level_db, "
              "COALESCE(vt_item_id, 0), vt_offset_ms, vt_duck_db, COALESCE(out_end_ms, 0) FROM transitions WHERE to_entry_id = ?");
    q.addBindValue(toEntryId);
    if (!run(q) || !q.next()) return t;
    t.toEntryId = toEntryId;
    t.fromEntryId = q.value(0).toInt();
    t.mixOutMs = q.value(1).toLongLong();
    t.fadeOutMs = q.value(2).toLongLong();
    t.fadeOutCurve = q.value(3).toString();
    t.cueInMs = q.value(4).toLongLong();
    t.fadeInMs = q.value(5).toLongLong();
    t.startLevelDb = q.value(6).toDouble();
    t.vtItemId = q.value(7).toInt();
    t.vtOffsetMs = q.value(8).toLongLong();
    t.vtDuckDb = q.value(9).toDouble();
    t.outEndMs = q.value(10).toLongLong();
    return t;
}

void Database::saveTransition(const Transition &t)
{
    if (remoteHook)
        remoteHook("saveTransition", QJsonObject{{"to", t.toEntryId}, {"from", t.fromEntryId}, {"mix", t.mixOutMs}, {"outEnd", t.outEndMs},
                                                 {"fadeOut", t.fadeOutMs}, {"curve", t.fadeOutCurve}, {"cueIn", t.cueInMs},
                                                 {"fadeIn", t.fadeInMs}, {"startLevel", t.startLevelDb}, {"vt", t.vtItemId},
                                                 {"vtOffset", t.vtOffsetMs}, {"vtDuck", t.vtDuckDb}});
    QSqlQuery q;
    q.prepare("INSERT INTO transitions (to_entry_id, from_entry_id, mix_out_ms, fade_out_ms, fade_out_curve, "
              "cue_in_ms, fade_in_ms, start_level_db, vt_item_id, vt_offset_ms, vt_duck_db, out_end_ms) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) "
              "ON CONFLICT(to_entry_id) DO UPDATE SET from_entry_id = excluded.from_entry_id, "
              "mix_out_ms = excluded.mix_out_ms, fade_out_ms = excluded.fade_out_ms, "
              "fade_out_curve = excluded.fade_out_curve, cue_in_ms = excluded.cue_in_ms, "
              "fade_in_ms = excluded.fade_in_ms, start_level_db = excluded.start_level_db, "
              "vt_item_id = excluded.vt_item_id, vt_offset_ms = excluded.vt_offset_ms, vt_duck_db = excluded.vt_duck_db, "
              "out_end_ms = excluded.out_end_ms, "
              "updated_at = unixepoch()");
    q.addBindValue(t.toEntryId);
    q.addBindValue(t.fromEntryId);
    q.addBindValue(t.mixOutMs);
    q.addBindValue(t.fadeOutMs);
    q.addBindValue(t.fadeOutCurve);
    q.addBindValue(t.cueInMs);
    q.addBindValue(t.fadeInMs);
    q.addBindValue(t.startLevelDb);
    q.addBindValue(t.vtItemId > 0 ? QVariant(t.vtItemId) : QVariant());
    q.addBindValue(t.vtOffsetMs);
    q.addBindValue(t.vtDuckDb);
    q.addBindValue(t.outEndMs > 0 ? QVariant(t.outEndMs) : QVariant());
    run(q);
}

void Database::deleteTransition(int toEntryId)
{
    if (remoteHook) remoteHook("deleteTransition", QJsonObject{{"to", toEntryId}});
    QSqlQuery q;
    q.prepare("DELETE FROM transitions WHERE to_entry_id = ?");
    q.addBindValue(toEntryId);
    run(q);
}

QHash<int, int> Database::transitionPairs()
{
    QHash<int, int> out;
    QSqlQuery q("SELECT to_entry_id, from_entry_id FROM transitions");
    while (q.next()) out.insert(q.value(0).toInt(), q.value(1).toInt());
    return out;
}

void Database::setItemMixDefaults(int itemId, qint64 mixOutMs, qint64 fadeOutMs, const QString &curve, qint64 cueOutMs)
{
    if (remoteHook)
        remoteHook("setItemMixDefaults", QJsonObject{{"item", itemId}, {"mix", mixOutMs}, {"fadeOut", fadeOutMs}, {"curve", curve},
                                                     {"cueOut", cueOutMs}});
    QSqlQuery q;
    q.prepare("UPDATE items SET mix_out_ms = ?, fade_out_ms = ?, fade_curve = ?, cue_out_ms = ? WHERE id = ?");
    q.addBindValue(mixOutMs);
    q.addBindValue(fadeOutMs);
    q.addBindValue(curve);
    q.addBindValue(cueOutMs > 0 ? QVariant(cueOutMs) : QVariant());
    q.addBindValue(itemId);
    run(q);
}

void Database::clearItemMixDefaults(int itemId)
{
    if (remoteHook) remoteHook("clearItemMixDefaults", QJsonObject{{"item", itemId}});
    QSqlQuery q;
    q.prepare("UPDATE items SET mix_out_ms = NULL, fade_out_ms = NULL, fade_curve = NULL, cue_out_ms = NULL WHERE id = ?");
    q.addBindValue(itemId);
    run(q);
}

void Database::setItemCueIn(int itemId, qint64 cueInMs)
{
    if (remoteHook) remoteHook("setItemCueIn", QJsonObject{{"item", itemId}, {"ms", cueInMs}});
    QSqlQuery q;
    q.prepare("UPDATE items SET cue_in_ms = ? WHERE id = ?");
    q.addBindValue(std::max<qint64>(0, cueInMs));
    q.addBindValue(itemId);
    run(q);
}

void Database::setItemIntro(int itemId, qint64 introMs)
{
    if (remoteHook) remoteHook("setItemIntro", QJsonObject{{"item", itemId}, {"ms", introMs}});
    QSqlQuery q;
    q.prepare("UPDATE items SET intro_ms = ? WHERE id = ?");
    q.addBindValue(introMs > 0 ? QVariant(introMs) : QVariant());
    q.addBindValue(itemId);
    run(q);
}

int Database::voiceTrackCategory()
{
    for (const Category &c : categories())
        if (c.kind == "voicetrack") return c.id;
    return addCategory(TR("Voice track"), "voicetrack", "#7fd6e8");
}

// ------------------------------------------------------- távoli szerkesztő

QVector<Transition> Database::transitionsFor(const QVector<int> &toEntryIds)
{
    QVector<Transition> out;
    const QHash<int, int> pairs = transitionPairs();
    for (int id : toEntryIds)
        if (pairs.contains(id)) out.push_back(transition(id));
    return out;
}

// A hangtár tükre: a kategóriák és az elemek a playout azonosítóival. A playlist tükre ezzel törlődik,
// utána újra kell tölteni (mirrorState).
bool Database::mirrorLibrary(const QVector<Category> &categories, const QVector<Item> &items)
{
    QSqlQuery q;
    bool ok = q.exec("BEGIN");
    ok = ok && q.exec("DELETE FROM transitions") && q.exec("DELETE FROM playlist_entries") && q.exec("DELETE FROM playlists");
    ok = ok && q.exec("DELETE FROM cartwalls") && q.exec("DELETE FROM timed_events") && q.exec("DELETE FROM saved_playlist_items")
         && q.exec("DELETE FROM saved_playlists") && q.exec("DELETE FROM clock_slots") && q.exec("DELETE FROM items")
         && q.exec("DELETE FROM categories");
    QSqlQuery ins;
    ok = ok && ins.prepare("INSERT INTO categories (id, kind, name, color) VALUES (?, ?, ?, ?)");
    for (const Category &c : categories) {
        if (!ok) break;
        ins.addBindValue(c.id);
        ins.addBindValue(c.kind);
        ins.addBindValue(c.name);
        ins.addBindValue(c.color);
        ok = ins.exec();
    }
    ok = ok && ins.prepare("INSERT INTO items (id, category_id, title, artist, file_path, duration_ms, last_played_at, cue_in_ms, "
                           "intro_ms, mix_out_ms, cue_out_ms, fade_out_ms, fade_curve, auto_mix_out_ms) "
                           "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    QSet<QString> paths;
    for (const Item &it : items) {
        if (!ok) break;
        QString path = it.path;
        if (paths.contains(path)) path += QStringLiteral("#%1").arg(it.id);   // két playout-útvonal ugyanoda képződött le
        paths.insert(path);
        ins.addBindValue(it.id);
        ins.addBindValue(it.categoryId);
        ins.addBindValue(it.title);
        ins.addBindValue(it.artist);
        ins.addBindValue(path);
        ins.addBindValue(std::max<qint64>(0, it.durationMs));
        ins.addBindValue(it.lastPlayedAt > 0 ? QVariant(it.lastPlayedAt) : QVariant());
        ins.addBindValue(it.cueInMs);
        ins.addBindValue(it.introMs > 0 ? QVariant(it.introMs) : QVariant());
        ins.addBindValue(it.userMixOutMs > 0 ? QVariant(it.userMixOutMs) : QVariant());
        ins.addBindValue(it.cueOutMs > 0 ? QVariant(it.cueOutMs) : QVariant());
        ins.addBindValue(it.fadeOutMs >= 0 ? QVariant(it.fadeOutMs) : QVariant());
        ins.addBindValue(it.fadeCurve.isEmpty() ? QVariant() : QVariant(it.fadeCurve));
        ins.addBindValue(it.userMixOutMs <= 0 && it.mixOutMs > 0 ? QVariant(it.mixOutMs) : QVariant());
        ok = ins.exec();
    }
    if (!ok) {
        qWarning("A hangtár tükrözése nem sikerült: %s %s", qPrintable(q.lastError().text()), qPrintable(ins.lastError().text()));
        q.exec("ROLLBACK");
        return false;
    }
    q.exec("COMMIT");
    emit categoriesChanged();
    emit itemsChanged();
    return true;
}

bool Database::mirrorState(const QVector<MirrorRow> &rows, const QVector<Transition> &transitions)
{
    QSqlQuery q;
    bool ok = q.exec("BEGIN");
    ok = ok && q.exec("DELETE FROM transitions") && q.exec("DELETE FROM playlist_entries") && q.exec("DELETE FROM playlists");
    QSqlQuery pl, en;
    ok = ok && pl.prepare("INSERT OR IGNORE INTO playlists (id, air_date, hour) VALUES (?, ?, ?)");
    ok = ok && en.prepare("INSERT INTO playlist_entries (id, playlist_id, position, item_id, fixed_start_s, source, status, "
                          "group_id, group_name, overlay_offset_ms, overlay_duck_db) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    QHash<int, int> position;
    QSet<int> playlists;
    for (const MirrorRow &r : rows) {
        if (!ok) break;
        const Entry &e = r.entry;
        if (!playlists.contains(e.playlistId)) {
            playlists.insert(e.playlistId);
            const QDate d = QDate::fromString(QString::number(e.playlistKey / 100), "yyyyMMdd");
            pl.addBindValue(e.playlistId);
            pl.addBindValue(d.isValid() ? d.toString(Qt::ISODate) : QStringLiteral("2000-01-01"));
            pl.addBindValue(std::clamp(e.playlistKey % 100, 0, 23));
            ok = pl.exec();
            if (!ok) break;
        }
        en.addBindValue(e.id);
        en.addBindValue(e.playlistId);
        en.addBindValue(++position[e.playlistId]);
        en.addBindValue(e.item.id);
        en.addBindValue(e.fixedStartS >= 0 ? QVariant(e.fixedStartS) : QVariant());
        en.addBindValue(e.source == QLatin1String("scheduler") ? QStringLiteral("scheduler") : QStringLiteral("manual"));
        en.addBindValue(r.status);
        en.addBindValue(e.groupId > 0 ? QVariant(e.groupId) : QVariant());
        en.addBindValue(e.groupId > 0 ? QVariant(e.groupName) : QVariant());
        en.addBindValue(e.overlay ? QVariant(e.overlayOffsetMs) : QVariant());
        en.addBindValue(e.overlay ? e.overlayDuckDb : 0.0);
        ok = en.exec();
    }
    if (!ok) {
        qWarning("A playlist tükrözése nem sikerült: %s %s %s", qPrintable(q.lastError().text()), qPrintable(pl.lastError().text()), qPrintable(en.lastError().text()));
        q.exec("ROLLBACK");
        return false;
    }
    q.exec("COMMIT");
    // Az átmenetek a mentéssel azonos úton, de a továbbítás nélkül.
    auto hook = std::move(remoteHook);
    remoteHook = nullptr;
    for (const Transition &t : transitions) saveTransition(t);
    remoteHook = std::move(hook);
    return true;
}
