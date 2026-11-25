// Előadók szétválasztása: „A and B” vagy „A feat. B” két előadó, és az előadó-elválasztás mindkettőre külön számít.
// Ami egy előadó marad, azt „&” köti össze (például „Simon & Garfunkel”): az alapból nem elválasztó.
// Az elválasztók listája a beállításokban szóközzel tagolt (artist_separators); a betűvel kezdődőket csak egész
// szóként veszi (szóköz előttük és utánuk), és a „/” is csak szóközök között választ el (az AC/DC egy előadó); a „;” és a
// „,” szóköz nélkül is.
#pragma once
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

namespace artists {

inline QString defaultSeparators() { return QStringLiteral("and feat. feat ft. ft featuring vs. vs / ;"); }

inline QRegularExpression separatorPattern(const QString &separators)
{
    static QHash<QString, QRegularExpression> cache;
    if (auto it = cache.constFind(separators); it != cache.constEnd()) return *it;
    QStringList words, marks;
    for (const QString &s : separators.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const QString e = QRegularExpression::escape(s.toLower());
        (s == QLatin1String(";") || s == QLatin1String(",") ? marks : words) << e;
    }
    // A hosszabb elöl, hogy a „feat.” ne „feat”-ként illeszkedjen.
    auto byLen = [](const QString &a, const QString &b) { return a.size() > b.size(); };
    std::sort(words.begin(), words.end(), byLen);
    std::sort(marks.begin(), marks.end(), byLen);
    QStringList alts;
    if (!words.isEmpty()) alts << QStringLiteral("\\s+(?:%1)\\s+").arg(words.join(QLatin1Char('|')));
    if (!marks.isEmpty()) alts << QStringLiteral("\\s*(?:%1)\\s*").arg(marks.join(QLatin1Char('|')));
    QRegularExpression re(alts.isEmpty() ? QStringLiteral("(?!)") : alts.join(QLatin1Char('|')),
                          QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    if (cache.size() > 16) cache.clear();
    cache.insert(separators, re);
    return re;
}

// A kisbetűs, levágott előadónevek (üres előadó: üres lista).
inline QStringList split(const QString &artist, const QString &separators)
{
    const QString a = artist.trimmed().toLower();
    if (a.isEmpty()) return {};
    QStringList out;
    for (const QString &part : a.split(separatorPattern(separators), Qt::SkipEmptyParts)) {
        const QString p = part.trimmed();
        if (!p.isEmpty() && !out.contains(p)) out << p;
    }
    if (out.isEmpty()) out << a;
    return out;
}

inline bool shareAny(const QStringList &a, const QStringList &b)
{
    for (const QString &x : a)
        if (b.contains(x)) return true;
    return false;
}

} // namespace artists
