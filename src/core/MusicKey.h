#pragma once
// Zenei hangnem: kód 0 = ismeretlen, 1–24 = 1 + hangosztály·2 + (moll ? 1 : 0), ahol a hangosztály 0 = C … 11 = H (B).
// Megjelenítés Camelot-jelöléssel (8A, 11B – a DJ-k harmonikus keveréséhez) vagy hagyományosan (Am, F#).
// Két hangnem harmonikusan illik egymáshoz, ha a Camelot-kódjuk azonos, szomszédos (±1 ugyanazzal a betűvel),
// vagy ugyanaz a szám dúr és moll között (párhuzamos hangnem).
#include <QString>

namespace mkey {

inline int make(int pitchClass, bool minor) { return 1 + ((pitchClass % 12 + 12) % 12) * 2 + (minor ? 1 : 0); }
inline bool valid(int key) { return key >= 1 && key <= 24; }
inline int pitchClass(int key) { return (key - 1) / 2; }
inline bool isMinor(int key) { return (key - 1) % 2 == 1; }

// Camelot-szám (1–12) és betű (A = moll, B = dúr).
inline int camelotNumber(int key)
{
    if (!valid(key)) return 0;
    const int major = isMinor(key) ? (pitchClass(key) + 3) % 12 : pitchClass(key);   // a párhuzamos dúr
    return (7 + (major * 7) % 12) % 12 + 1;
}
inline QString camelot(int key)
{
    if (!valid(key)) return {};
    return QString::number(camelotNumber(key)) + (isMinor(key) ? QLatin1Char('A') : QLatin1Char('B'));
}
inline QString traditional(int key)
{
    if (!valid(key)) return {};
    static const char *major[12] = {"C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
    static const char *minor[12] = {"Cm", "C#m", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "Bbm", "Bm"};
    return QString::fromLatin1((isMinor(key) ? minor : major)[pitchClass(key)]);
}
inline QString name(int key, bool useCamelot) { return useCamelot ? camelot(key) : traditional(key); }
// A megjelenítés jelölése (Beállítások → Elemzés); alapból Camelot.
inline bool &camelotNotation()
{
    static bool v = true;
    return v;
}
inline QString display(int key) { return valid(key) ? name(key, camelotNotation()) : QString(); }
// Rendezési kulcs a Camelot-kerék szerint: 1A, 1B, 2A, …; az ismeretlen a végére kerül.
inline int sortKey(int key) { return valid(key) ? camelotNumber(key) * 2 + (isMinor(key) ? 0 : 1) : 1000; }
inline QString both(int key) { return valid(key) ? camelot(key) + QStringLiteral(" · ") + traditional(key) : QString(); }

// Harmonikus illeszkedés (Camelot-kerék). Ismeretlen hangnem mindenhez illik.
inline bool compatible(int a, int b)
{
    if (!valid(a) || !valid(b)) return true;
    const int na = camelotNumber(a), nb = camelotNumber(b);
    if (isMinor(a) == isMinor(b)) {
        const int d = (na - nb + 12) % 12;
        return d == 0 || d == 1 || d == 11;
    }
    return na == nb;
}

// Címkéből vagy kézi bevitelből: „8A”, „08a”, „Am”, „A minor”, „F#m”, „Gb”, „Bbmin”, „1m”/„1d” (Open Key) stb.
inline int parse(QString s)
{
    s = s.trimmed();
    if (s.isEmpty()) return 0;
    // Camelot vagy Open Key: szám + betű
    {
        int i = 0;
        while (i < s.size() && s[i].isDigit()) ++i;
        if (i > 0 && i < s.size() && i + 1 == s.size()) {
            const int n = s.left(i).toInt();
            const QChar c = s[i].toLower();
            if (n >= 1 && n <= 12) {
                int camelotN = n;
                bool minor;
                if (c == QLatin1Char('a') || c == QLatin1Char('b')) minor = c == QLatin1Char('a');
                else if (c == QLatin1Char('m') || c == QLatin1Char('d')) {       // Open Key: 1d = C-dúr = 8B
                    minor = c == QLatin1Char('m');
                    camelotN = (n + 6) % 12 + 1;
                } else return 0;
                for (int k = 1; k <= 24; ++k)
                    if (isMinor(k) == minor && camelotNumber(k) == camelotN) return k;
            }
            return 0;
        }
    }
    // Hagyományos: hangnév (+ # vagy b) és dúr/moll jelölés
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7};   // A B C D E F G
    const QChar l = s[0].toUpper();
    if (l < QLatin1Char('A') || l > QLatin1Char('G')) return 0;
    int pc = base[l.unicode() - 'A'];
    int i = 1;
    if (i < s.size() && (s[i] == QLatin1Char('#') || s[i] == QChar(0x266F))) { ++pc; ++i; }
    else if (i < s.size() && (s[i] == QLatin1Char('b') || s[i] == QChar(0x266D))) {
        // „Bb” = B-moll? Nem: a második b módosító, ha utána nincs „…b” mint betű folytatás.
        --pc;
        ++i;
    }
    QString rest = s.mid(i).trimmed().toLower();
    rest.remove(QLatin1Char(' '));
    bool minor = false;
    if (rest.isEmpty() || rest == QLatin1String("maj") || rest == QLatin1String("major") || rest == QLatin1String("dúr")
        || rest == QLatin1String("dur"))
        minor = false;
    else if (rest == QLatin1String("m") || rest == QLatin1String("min") || rest == QLatin1String("minor")
             || rest == QLatin1String("moll") || rest == QLatin1String("-"))
        minor = true;
    else
        return 0;
    return make(pc, minor);
}

// A fájl BPM-címkéje („128”, „127.9”, „128,0”); a valószerűtlen értéket elveti.
inline double parseBpm(QString s)
{
    s = s.trimmed().replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double v = s.section(QLatin1Char(' '), 0, 0).toDouble(&ok);
    return ok && v >= 40 && v <= 250 ? v : 0.0;
}

} // namespace mkey
