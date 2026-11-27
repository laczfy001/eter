#include "sched/ExternalPlaylist.h"

#include "audio/Decoder.h"
#include "core/ErrorLog.h"
#include "core/MusicKey.h"
#include "db/Database.h"
#include "i18n.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringDecoder>

#include <algorithm>
#include <climits>
#include <cmath>

namespace {

const char16_t kCp1250[128] = {
    0x20AC, 0xFFFD, 0x201A, 0xFFFD, 0x201E, 0x2026, 0x2020, 0x2021, 0xFFFD, 0x2030, 0x0160, 0x2039, 0x015A, 0x0164, 0x017D, 0x0179,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122, 0x0161, 0x203A, 0x015B, 0x0165, 0x017E, 0x017A,
    0x00A0, 0x02C7, 0x02D8, 0x0141, 0x00A4, 0x0104, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x015E, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x017B,
    0x00B0, 0x00B1, 0x02DB, 0x0142, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x0105, 0x015F, 0x00BB, 0x013D, 0x02DD, 0x013E, 0x017C,
    0x0154, 0x00C1, 0x00C2, 0x0102, 0x00C4, 0x0139, 0x0106, 0x00C7, 0x010C, 0x00C9, 0x0118, 0x00CB, 0x011A, 0x00CD, 0x00CE, 0x010E,
    0x0110, 0x0143, 0x0147, 0x00D3, 0x00D4, 0x0150, 0x00D6, 0x00D7, 0x0158, 0x016E, 0x00DA, 0x0170, 0x00DC, 0x00DD, 0x0162, 0x00DF,
    0x0155, 0x00E1, 0x00E2, 0x0103, 0x00E4, 0x013A, 0x0107, 0x00E7, 0x010D, 0x00E9, 0x0119, 0x00EB, 0x011B, 0x00ED, 0x00EE, 0x010F,
    0x0111, 0x0144, 0x0148, 0x00F3, 0x00F4, 0x0151, 0x00F6, 0x00F7, 0x0159, 0x016F, 0x00FA, 0x0171, 0x00FC, 0x00FD, 0x0163, 0x02D9};
const char16_t kCp1252[128] = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
    0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7, 0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7, 0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7, 0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
    0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7, 0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
    0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7, 0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
    0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7, 0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF};

QString fromTable(const QByteArray &data, const char16_t *table)
{
    QString out;
    out.reserve(data.size());
    for (const char ch : data) {
        const auto b = static_cast<unsigned char>(ch);
        out += b < 0x80 ? QChar(b) : QChar(table[b - 0x80]);
    }
    return out;
}

// A hónap neve (első három betű, kisbetűvel): angol, magyar és néhány gyakori más nyelvű rövidítés.
int monthOf(const QString &name)
{
    static const QHash<QString, int> months = [] {
        QHash<QString, int> m;
        const char *en[] = {"jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec"};
        const char *hu[] = {"jan", "feb", "már", "ápr", "máj", "jún", "júl", "aug", "sze", "okt", "nov", "dec"};
        for (int i = 0; i < 12; ++i) {
            m.insert(QString::fromUtf8(en[i]), i + 1);
            m.insert(QString::fromUtf8(hu[i]), i + 1);
        }
        for (auto [k, v] : std::initializer_list<std::pair<const char *, int>>{
                 {"mrt", 3}, {"mär", 3}, {"mei", 5}, {"mai", 5}, {"okt", 10}, {"dez", 12}, {"maa", 3}, {"mag", 5}, {"giu", 6},
                 {"lug", 7}, {"ago", 8}, {"set", 9}, {"ott", 10}, {"dic", 12}, {"ene", 1}, {"abr", 4}})
            m.insert(QString::fromUtf8(k), v);
        return m;
    }();
    return months.value(name.left(3).toLower(), 0);
}

QString sig(const ExtFile &f) { return QStringLiteral("%1|%2|%3").arg(f.name).arg(f.mtime).arg(f.size); }

QString mmss(qint64 ms)
{
    const qint64 s = (ms + 500) / 1000;
    return s >= 3600 ? QStringLiteral("%1:%2:%3").arg(s / 3600).arg(s / 60 % 60, 2, 10, QLatin1Char('0')).arg(s % 60, 2, 10, QLatin1Char('0'))
                     : QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

constexpr qint64 kHourMs = 3600 * 1000;

} // namespace

// ---------------------------------------------------------------- fájlformátum

QString ext::decode(const QByteArray &raw, const QString &encoding, QString *used)
{
    QByteArray data = raw;
    if (data.startsWith("\xEF\xBB\xBF")) {
        if (used) *used = QStringLiteral("UTF-8");
        return QString::fromUtf8(data.mid(3));
    }
    if (encoding == QLatin1String("cp1250")) {
        if (used) *used = QStringLiteral("Windows-1250");
        return fromTable(data, kCp1250);
    }
    if (encoding == QLatin1String("cp1252")) {
        if (used) *used = QStringLiteral("Windows-1252");
        return fromTable(data, kCp1252);
    }
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8.decode(data);
    if (encoding == QLatin1String("utf8") || !utf8.hasError()) {
        if (used) *used = QStringLiteral("UTF-8");
        return text;
    }
    // A rendszer ANSI kódlapja: a Creator és a többi Windows-os ütemező ezzel ír sima M3U-t.
#ifdef Q_OS_WIN
    if (used) *used = QStringLiteral("ANSI");
    return QString::fromLocal8Bit(data);
#else
    if (used) *used = QStringLiteral("Windows-1252");
    return fromTable(data, kCp1252);
#endif
}

ext::Parsed ext::parse(const QByteArray &data, const QString &encoding)
{
    Parsed out;
    const QString text = decode(data, encoding, &out.encoding);
    Line pending;
    bool hasInfo = false;
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QLatin1Char('#'))) {
            if (line.startsWith(QLatin1String("#EXTM3U"), Qt::CaseInsensitive)) {
                out.extended = true;
            } else if (line.startsWith(QLatin1String("#EXTINF:"), Qt::CaseInsensitive)) {
                pending = Line{};
                hasInfo = true;
                const QString rest = line.mid(8);
                const int comma = rest.indexOf(QLatin1Char(','));
                bool ok = false;
                const double secs = rest.left(comma < 0 ? rest.size() : comma).trimmed().toDouble(&ok);
                if (ok && secs > 0) pending.durationMs = qint64(std::llround(secs * 1000));
                const QString info = comma < 0 ? QString() : rest.mid(comma + 1);
                if (info.startsWith(QLatin1Char('|'))) {
                    // StationPlaylist Creator: |előadó|cím|kategória|…|…|…|típus|… (típus 2 = jingle)
                    const QStringList f = info.split(QLatin1Char('|'));
                    out.creator = true;
                    pending.artist = f.value(1).trimmed();
                    pending.title = f.value(2).trimmed();
                    pending.category = f.value(3).trimmed();
                    pending.jingle = f.value(7).trimmed() == QLatin1String("2");
                } else if (!info.trimmed().isEmpty()) {
                    const int dash = info.indexOf(QStringLiteral(" - "));
                    if (dash > 0) {
                        pending.artist = info.left(dash).trimmed();
                        pending.title = info.mid(dash + 3).trimmed();
                    } else {
                        pending.title = info.trimmed();
                    }
                }
            }
            continue;
        }
        Line l = hasInfo ? pending : Line{};
        l.path = line;
        out.lines << l;
        hasInfo = false;
    }
    return out;
}

QString ext::defaultPattern() { return TR("{hónap}") + QStringLiteral(".") + TR("{nap}") + QStringLiteral("-") + TR("{óra}") + QStringLiteral(".M3U"); }

ext::Target ext::match(const QString &fileName, const QString &pattern, const QDate &today)
{
    // A minta helyőrzői csoportokká, a többi betű szerint (kis- és nagybetű mindegy).
    static const QRegularExpression token(QStringLiteral("\\{([^}]+)\\}"));
    QString re = QStringLiteral("^");
    QStringList order;
    int pos = 0;
    for (auto it = token.globalMatch(pattern); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        re += QRegularExpression::escape(pattern.mid(pos, m.capturedStart() - pos));
        const QString t = m.captured(1).toLower();
        if (t == QStringLiteral("év") || t == QLatin1String("year")) { re += QStringLiteral("(\\d{4}|\\d{2})"); order << "y"; }
        else if (t == QStringLiteral("hónap") || t == QLatin1String("mon")) { re += QStringLiteral("(\\p{L}{3,})\\.?"); order << "mn"; }
        else if (t == QStringLiteral("hó") || t == QLatin1String("month")) { re += QStringLiteral("(\\d{1,2})"); order << "m"; }
        else if (t == QLatin1String("nap") || t == QLatin1String("day")) { re += QStringLiteral("(\\d{1,2})"); order << "d"; }
        else if (t == QStringLiteral("óra") || t == QLatin1String("hour")) { re += QStringLiteral("(\\d{1,2})"); order << "h"; }
        else { re += QRegularExpression::escape(m.captured(0)); }
        pos = m.capturedEnd();
    }
    re += QRegularExpression::escape(pattern.mid(pos)) + QStringLiteral("$");
    // „{hónap}.” a mintában: a pont a hónap után opcionális (nov. / nov), ezért a minta pontja is az.
    re.replace(QStringLiteral("\\.?\\."), QStringLiteral("\\.?"));
    const QRegularExpression rx(re, QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    const QRegularExpressionMatch m = rx.match(fileName);
    Target t;
    if (!m.hasMatch()) return t;
    int y = 0, mo = 0, d = 0, h = -1;
    for (int i = 0; i < order.size(); ++i) {
        const QString v = m.captured(i + 1);
        if (order[i] == "y") y = v.size() == 2 ? 2000 + v.toInt() : v.toInt();
        else if (order[i] == "mn") mo = monthOf(v);
        else if (order[i] == "m") mo = v.toInt();
        else if (order[i] == "d") d = v.toInt();
        else if (order[i] == "h") h = v.toInt();
    }
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23) return t;
    if (!y) {
        // Év nélkül: a mához legközelebbi (az év végén a januári fájl a jövő évé).
        int best = today.year();
        qint64 bestDist = LLONG_MAX;
        for (int cand : {today.year() - 1, today.year(), today.year() + 1}) {
            const QDate dt(cand, mo, d);
            if (dt.isValid() && std::abs(today.daysTo(dt)) < bestDist) {
                bestDist = std::abs(today.daysTo(dt));
                best = cand;
            }
        }
        y = best;
    }
    t.date = QDate(y, mo, d);
    t.hour = h;
    return t;
}

// ---------------------------------------------------------------- beállítások

bool ExternalPlaylists::enabled() const
{
    return db_->setting("ext_enabled", "0") == "1" && !db_->setting("ext_dir").trimmed().isEmpty();
}

int ExternalPlaylists::clockId(Database *db)
{
    const int id = db->setting("ext_clock_id", "0").toInt();
    if (!id) return 0;
    for (const Clock &c : db->clocks())
        if (c.id == id) return id;
    return 0;
}

int ExternalPlaylists::clockId(bool create) const
{
    if (const int id = clockId(db_)) return id;
    if (!create) return 0;
    const QString name = TR("Külső playlist");
    int id = db_->addClock(name, QStringLiteral("#5cb0ff"));
    if (!id)
        for (const Clock &c : db_->clocks())
            if (c.name == name) id = c.id;
    if (id) db_->setSetting("ext_clock_id", QString::number(id));
    return id;
}

int ExternalPlaylists::fallbackClock() const
{
    if (db_->setting("ext_fallback", "1") != "1") return 0;
    const int ext = clockId(db_);
    const int chosen = db_->setting("ext_fallback_clock", "0").toInt();
    int first = 0;
    for (const Clock &c : db_->clocks()) {
        if (c.id == ext) continue;
        if (c.id == chosen) return chosen;
        if (!first && !db_->clockSlots(c.id).isEmpty()) first = c.id;
    }
    return first;
}

QString ExternalPlaylists::mapPath(const QString &path) const
{
    QString p = path.trimmed();
    p.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const QJsonArray map = QJsonDocument::fromJson(db_->setting("ext_path_map", "[]").toUtf8()).array();
    for (const QJsonValue &v : map) {
        QString from = v.toArray().at(0).toString().trimmed(), to = v.toArray().at(1).toString().trimmed();
        from.replace(QLatin1Char('\\'), QLatin1Char('/'));
        to.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (from.isEmpty()) continue;
        if (p.startsWith(from, Qt::CaseInsensitive)) {
            p = to + p.mid(from.size());
            break;
        }
    }
    // Relatív útvonal: a playlist mappájához képest.
    if (QDir::isRelativePath(p) && !(p.size() > 1 && p[1] == QLatin1Char(':')))
        p = QDir(db_->setting("ext_dir")).filePath(p);
    return QDir::cleanPath(p);
}

// ---------------------------------------------------------------- a mappa

const ext::Parsed &ExternalPlaylists::parsed(const ExtFile &f) const
{
    const qint64 key = f.mtime * 1000003 + f.size;
    auto it = cache_.find(f.path);
    if (it != cache_.end() && it->first == key) return it->second;
    QFile file(f.path);
    ext::Parsed p;
    if (file.open(QIODevice::ReadOnly)) p = ext::parse(file.readAll(), db_->setting("ext_encoding", "auto"));
    cache_.insert(f.path, {key, p});
    return cache_[f.path].second;
}

QVector<ExtFile> ExternalPlaylists::scan() const
{
    QVector<ExtFile> out;
    const QString dirPath = db_->setting("ext_dir").trimmed();
    if (dirPath.isEmpty()) return out;
    const QString pattern = db_->setting("ext_pattern", ext::defaultPattern());
    const QDate today = eter::now().date();
    const QFileInfoList entries = QDir(dirPath).entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo &fi : entries) {
        ExtFile f;
        f.target = ext::match(fi.fileName(), pattern, today);
        if (!f.target.valid()) continue;
        f.path = fi.absoluteFilePath();
        f.name = fi.fileName();
        f.mtime = fi.lastModified().toSecsSinceEpoch();
        f.size = fi.size();
        if (f.target.hour == 0 && db_->setting("ext_split_day", "1") == "1") {
            // A 00 órás fájl lehet egész napos is (a Creator sima M3U-ja): ha jóval hosszabb egy óránál.
            const ext::Parsed &p = parsed(f);
            qint64 total = 0;
            bool known = true;
            for (const ext::Line &l : p.lines) {
                if (l.durationMs < 0) known = false;
                else total += l.durationMs;
            }
            if (known ? total > kHourMs * 3 / 2 : p.lines.size() > 40) f.target.hour = -1;
        }
        out << f;
    }
    std::sort(out.begin(), out.end(), [](const ExtFile &a, const ExtFile &b) {
        return a.target.date != b.target.date ? a.target.date < b.target.date : a.target.hour < b.target.hour;
    });
    return out;
}

const ExtFile *ExternalPlaylists::fileFor(const QDateTime &hour, const QVector<ExtFile> &files) const
{
    const ExtFile *day = nullptr;
    for (const ExtFile &f : files) {
        if (f.target.date != hour.date()) continue;
        if (f.target.hour == hour.time().hour()) return &f;
        if (f.target.hour < 0) day = &f;
    }
    return day;
}

bool ExternalPlaylists::covers(const QDateTime &hour) const
{
    if (!enabled()) return false;
    const QVector<ExtFile> files = scan();
    return fileFor(hour, files) != nullptr;
}

QStringList ExternalPlaylists::categoriesIn(const QVector<ExtFile> &files) const
{
    QStringList out;
    for (const ExtFile &f : files)
        for (const ext::Line &l : parsed(f).lines) {
            const QString c = l.category.isEmpty() ? QString() : l.category;
            if (!out.contains(c, Qt::CaseInsensitive)) out << c;
        }
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    return out;
}

// ---------------------------------------------------------------- beolvasás

int ExternalPlaylists::categoryFor(const ext::Line &line)
{
    const QString name = line.category.isEmpty() ? (line.jingle ? QStringLiteral("Jingle") : TR("Külső playlist")) : line.category;
    QJsonObject map = QJsonDocument::fromJson(db_->setting("ext_cat_map", "{}").toUtf8()).object();
    const QVector<Category> cats = db_->itemCategories();
    const int mapped = map.value(line.category).toInt();
    for (const Category &c : cats)
        if (c.id == mapped && mapped) return mapped;
    int id = 0;
    for (const Category &c : cats)
        if (c.name.compare(name, Qt::CaseInsensitive) == 0) id = c.id;
    if (!id) {
        static const char *palette[] = {"#5cb0ff", "#a371f7", "#3fb950", "#f778ba", "#56d4dd", "#e3b341", "#ff7b72"};
        id = db_->addCategory(name, line.jingle ? QStringLiteral("jingle") : QStringLiteral("music"),
                              QString::fromLatin1(palette[cats.size() % 7]));
        if (id) emit db_->categoriesChanged();
    }
    if (id) {
        map.insert(line.category, id);
        db_->setSetting("ext_cat_map", QString::fromUtf8(QJsonDocument(map).toJson(QJsonDocument::Compact)));
    }
    return id;
}

ExternalPlaylists::Resolved ExternalPlaylists::resolve(const ext::Line &line, ExtReport &rep)
{
    Resolved r;
    r.path = mapPath(line.path);
    if (const int id = db_->itemIdByPath(r.path)) {
        const Item it = db_->item(id);
        r.itemId = id;
        r.airMs = it.airMs();
        r.state = 0;
        return r;
    }
    if (!QFileInfo::exists(r.path)) {
        r.state = 2;
        r.airMs = std::max<qint64>(0, line.durationMs);
        return r;
    }
    eter::Decoder dec;
    std::string err;
    if (!dec.open(r.path.toStdString(), &err)) {
        r.state = 2;
        r.airMs = std::max<qint64>(0, line.durationMs);
        rep.error = QString::fromStdString(err);
        return r;
    }
    Item it;
    it.categoryId = categoryFor(line);
    it.path = r.path;
    const bool fromFile = db_->setting("ext_tags", "1") == "1" && !line.title.isEmpty();
    it.title = fromFile ? line.title : QString::fromStdString(dec.title()).trimmed();
    it.artist = fromFile ? line.artist : QString::fromStdString(dec.artist()).trimmed();
    if (it.title.isEmpty()) {
        const QString base = QFileInfo(r.path).completeBaseName();
        const int dash = base.indexOf(QStringLiteral(" - "));
        if (dash > 0 && it.artist.isEmpty()) {
            it.artist = base.left(dash).trimmed();
            it.title = base.mid(dash + 3).trimmed();
        } else {
            it.title = base;
        }
    }
    it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
    it.musicKey = mkey::parse(QString::fromStdString(dec.keyTag()));
    it.bpm = mkey::parseBpm(QString::fromStdString(dec.bpmTag()));
    r.itemId = db_->addItem(it);
    r.state = r.itemId ? 1 : 2;
    r.airMs = r.itemId ? it.airMs() : std::max<qint64>(0, line.durationMs);
    return r;
}

QVector<int> ExternalPlaylists::hourSlice(const ExtFile &f, int hour, QVector<Resolved> &all, ExtReport &rep)
{
    const QString key = sig(f) + f.path;
    auto it = resolved_.find(key);
    if (it == resolved_.end()) {
        QVector<Resolved> rs;
        for (const ext::Line &l : parsed(f).lines) rs << resolve(l, rep);
        it = resolved_.insert(key, rs);
        if (resolved_.size() > 64) resolved_.clear(), it = resolved_.insert(key, rs);
    }
    all = *it;
    QVector<int> out;
    if (f.target.hour >= 0) {
        for (int i = 0; i < all.size(); ++i) out << i;
        return out;
    }
    // Egész napos fájl: minden óra fixen indul, az óra azokat kapja, amelyek még az órán belül kezdődnek.
    int h = 0;
    qint64 t = 0;
    for (int i = 0; i < all.size(); ++i) {
        if (t >= kHourMs && h < 23) {
            ++h;
            t = 0;
        }
        if (h == hour) out << i;
        t += all[i].airMs;
    }
    return out;
}

int ExternalPlaylists::importHour(const QDateTime &hourIn, const QDateTime &now)
{
    if (!enabled()) return -1;
    const QDateTime hour(hourIn.date(), QTime(hourIn.time().hour(), 0));
    const QVector<ExtFile> files = scan();
    const ExtFile *f = fileFor(hour, files);
    if (!f) return -1;
    return importFrom(*f, hour, now);
}

int ExternalPlaylists::importFrom(const ExtFile &file, const QDateTime &hourIn, const QDateTime &now)
{
    const ExtFile *f = &file;
    const QDateTime hour(hourIn.date(), QTime(hourIn.time().hour(), 0));
    const int key = hour.date().toString("yyyyMMdd").toInt() * 100 + hour.time().hour();
    ExtReport rep;
    rep.file = f->name;
    rep.key = key;
    QVector<Resolved> all;
    const QVector<int> slice = hourSlice(*f, hour.time().hour(), all, rep);
    const QVector<ext::Line> &lines = parsed(*f).lines;
    for (int i : slice) {
        ++rep.total;
        const Resolved &r = all[i];
        if (r.state == 0) ++rep.fromLibrary;
        else if (r.state == 1) ++rep.added;
        else {
            ++rep.missing;
            rep.missingPaths << (i < lines.size() ? lines[i].path : r.path);
        }
        if (r.itemId) rep.lengthMs += r.airMs;
    }
    if (rep.total - rep.missing <= 0) {
        if (rep.error.isEmpty()) rep.error = TR("nincs lejátszható elem");
        elog::warning(QStringLiteral("Külső playlist"),
                      QStringLiteral("%1 → %2:00: %3, tartalék séma").arg(f->name).arg(hour.time().hour(), 2, 10, QLatin1Char('0')).arg(rep.error));
        reports_ << rep;
        return -1;
    }

    const int playlist = db_->playlistFor(hour.date(), hour.time().hour());
    const bool fixedHour = db_->setting("ext_fixed_hour", "1") == "1";
    const qint64 base = hour.toSecsSinceEpoch(), nowSecs = now.toSecsSinceEpoch();
    qint64 t = 0;
    bool first = true;
    int added = 0, firstEntry = 0;
    for (int i : slice) {
        const Resolved &r = all[i];
        if (!r.itemId) continue;
        const bool isFirst = first;
        first = false;
        // Az óra már elmúlt része kimarad (ha az óra közben derül ki, hogy kell).
        if (base + (t + r.airMs) / 1000 <= nowSecs) {
            t += r.airMs;
            continue;
        }
        const Entry e = db_->addEntry(playlist, r.itemId, QStringLiteral("scheduler"), isFirst && fixedHour ? 0 : -1);
        if (e.id && !firstEntry) firstEntry = e.id;
        if (e.id) ++added;
        t += r.airMs;
    }
    if (firstEntry) {
        QString note = TR("KÜLSŐ · %1 · %2 elem · %3").arg(f->name).arg(rep.total - rep.missing).arg(mmss(rep.lengthMs));
        if (rep.lengthMs > kHourMs + 15000) note += TR(" · túlfut %1").arg(mmss(rep.lengthMs - kHourMs));
        if (rep.missing) note += TR(" · %1 hiányzik").arg(rep.missing);
        db_->setEntryNote(firstEntry, note);
    }
    db_->markGenerated(playlist, clockId(db_));
    remember(key, *f);
    notedMissing_.remove(key);
    elog::info(QStringLiteral("Külső playlist"),
               QStringLiteral("%1 → %2 %3:00: %4 elem (%5 a hangtárból, %6 új, %7 hiányzik), %8")
                   .arg(f->name, hour.date().toString(Qt::ISODate)).arg(hour.time().hour(), 2, 10, QLatin1Char('0'))
                   .arg(added).arg(rep.fromLibrary).arg(rep.added).arg(rep.missing).arg(mmss(rep.lengthMs)));
    for (const QString &p : std::as_const(rep.missingPaths))
        elog::warning(QStringLiteral("Külső playlist"), QStringLiteral("%1: nem található: %2").arg(f->name, p));
    if (rep.added) db_->notifyItemsChanged();
    reports_ << rep;
    return added;
}

int ExternalPlaylists::refresh(const QDateTime &now, int aheadHours)
{
    if (!enabled()) return 0;
    const QVector<ExtFile> files = scan();
    QHash<QString, QString> recs = records();
    const QDateTime thisHour(now.date(), QTime(now.time().hour(), 0));
    const int extClock = clockId(db_);
    const bool reload = db_->setting("ext_reload", "1") == "1";
    int changed = 0;
    for (int i = 1; i <= std::max(1, aheadHours); ++i) {
        const QDateTime hour = thisHour.addSecs(qint64(i) * 3600);
        const int key = hour.date().toString("yyyyMMdd").toInt() * 100 + hour.time().hour();
        const ExtFile *f = fileFor(hour, files);
        if (!f || hour.addSecs(-300) <= now) continue;   // az óra küszöbén már nem cseréli (készenlétben lehet)
        const QString rec = recs.value(QString::number(key));
        if (!rec.isEmpty()) {
            if (!reload || rec == sig(*f)) continue;
        } else {
            // Még nem a fájlból készült: ha a tartalékból (vagy kézzel generálva) már elkészült, a fájl felülírja.
            const int clock = db_->clockAt(hour.date(), hour.time().hour());
            if (clock && clock != extClock) continue;
            const int playlist = db_->playlistFor(hour.date(), hour.time().hour());
            if (!db_->playlistGenerated(playlist)) continue;   // az ütemező úgyis a fájlból készíti el
        }
        db_->deleteSchedulerEntriesAt(key);
        if (importHour(hour, now) >= 0) {
            ++changed;
            elog::info(QStringLiteral("Külső playlist"),
                       QStringLiteral("%1 megváltozott vagy megérkezett: %2:00 újraolvasva").arg(f->name).arg(hour.time().hour(), 2, 10, QLatin1Char('0')));
        }
    }
    // A régi bejegyzések törlése (két napnál régebbi órák).
    const int oldest = thisHour.addDays(-2).date().toString("yyyyMMdd").toInt() * 100;
    bool pruned = false;
    for (auto it = recs.begin(); it != recs.end();) {
        if (it.key().toInt() < oldest) {
            it = recs.erase(it);
            pruned = true;
        } else {
            ++it;
        }
    }
    if (pruned) {
        QJsonObject o;
        for (auto it = recs.cbegin(); it != recs.cend(); ++it) o.insert(it.key(), it.value());
        db_->setSetting("ext_imported", QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
    }
    return changed;
}

int ExternalPlaylists::importFile(const QString &path, const QDateTime &now, QString *error)
{
    const QFileInfo fi(path);
    ExtFile f;
    f.target = ext::match(fi.fileName(), db_->setting("ext_pattern", ext::defaultPattern()), now.date());
    if (!f.target.valid()) {
        if (error) *error = TR("A fájlnévből nem derül ki, melyik nap melyik órájára szól (Fájlnév minta).");
        return -1;
    }
    f.path = fi.absoluteFilePath();
    f.name = fi.fileName();
    f.mtime = fi.lastModified().toSecsSinceEpoch();
    f.size = fi.size();
    if (f.target.hour == 0) {
        const ext::Parsed &p = parsed(f);
        qint64 total = 0;
        bool known = true;
        for (const ext::Line &l : p.lines) {
            if (l.durationMs < 0) known = false;
            else total += l.durationMs;
        }
        if (known ? total > kHourMs * 3 / 2 : p.lines.size() > 40) f.target.hour = -1;
    }
    const QDateTime thisHour(now.date(), QTime(now.time().hour(), 0));
    int hours = 0;
    for (int h = 0; h < 24; ++h) {
        if (f.target.hour >= 0 && h != f.target.hour) continue;
        const QDateTime hour(f.target.date, QTime(h, 0));
        if (hour < thisHour) continue;
        const int key = hour.date().toString("yyyyMMdd").toInt() * 100 + h;
        db_->deleteSchedulerEntriesAt(key);
        if (importFrom(f, hour, now) >= 0) ++hours;
    }
    if (!hours && error) *error = TR("A fájl órái már elmúltak, vagy nincs bennük lejátszható elem.");
    return hours;
}

void ExternalPlaylists::noteMissing(const QDateTime &hour, bool fallback)
{
    ExtReport rep;
    rep.key = hour.date().toString("yyyyMMdd").toInt() * 100 + hour.time().hour();
    if (notedMissing_.contains(rep.key)) return;     // óránként egyszer
    notedMissing_.insert(rep.key);
    rep.fallback = true;
    rep.error = fallback ? TR("nincs fájl, tartalék séma") : TR("nincs fájl, az óra üres");
    elog::warning(QStringLiteral("Külső playlist"), QStringLiteral("%1 %2:00: nincs fájl → %3")
                      .arg(hour.date().toString(Qt::ISODate)).arg(hour.time().hour(), 2, 10, QLatin1Char('0'))
                      .arg(fallback ? QStringLiteral("tartalék séma") : QStringLiteral("üres marad")));
    reports_ << rep;
}

QHash<QString, QString> ExternalPlaylists::records() const
{
    QHash<QString, QString> out;
    const QJsonObject o = QJsonDocument::fromJson(db_->setting("ext_imported", "{}").toUtf8()).object();
    for (auto it = o.begin(); it != o.end(); ++it) out.insert(it.key(), it.value().toString());
    return out;
}

void ExternalPlaylists::remember(int key, const ExtFile &f)
{
    QJsonObject o = QJsonDocument::fromJson(db_->setting("ext_imported", "{}").toUtf8()).object();
    o.insert(QString::number(key), sig(f));
    db_->setSetting("ext_imported", QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

void ExternalPlaylists::forget(int key)
{
    QJsonObject o = QJsonDocument::fromJson(db_->setting("ext_imported", "{}").toUtf8()).object();
    o.remove(QString::number(key));
    db_->setSetting("ext_imported", QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}

QString ExternalPlaylists::sourceOf(int key) const { return records().value(QString::number(key)).section(QLatin1Char('|'), 0, 0); }

QVector<ExtReport> ExternalPlaylists::takeReports()
{
    QVector<ExtReport> r = reports_;
    reports_.clear();
    recent_ += r;
    if (recent_.size() > 48) recent_.remove(0, recent_.size() - 48);
    return r;
}
