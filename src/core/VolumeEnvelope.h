#pragma once
// Az elem hangerőgörbéje: pontok (az elem saját ideje ms-ban, szint dB-ben). A pontok között a szint dB-ben
// egyenletesen változik; az első pont előtt az első, az utolsó után az utolsó pont szintje érvényes.
// Üres görbe: végig 0 dB (változatlan hangerő).
#include <QString>
#include <QStringList>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace env {

struct Point {
    qint64 ms = 0;
    double db = 0;
};
using Envelope = QVector<Point>;

constexpr double kMinDb = -40.0;      // ennél halkabbra nem húzható (a görbe alja)
constexpr double kMaxDb = 0.0;        // hangosítani nem lehet, csak halkítani
constexpr int kMaxPoints = 64;

inline void normalize(Envelope &e)
{
    std::sort(e.begin(), e.end(), [](const Point &a, const Point &b) { return a.ms < b.ms; });
    for (Point &p : e) {
        p.ms = std::max<qint64>(0, p.ms);
        p.db = std::clamp(std::round(p.db * 2) / 2, kMinDb, kMaxDb);
    }
    if (e.size() > kMaxPoints) e.resize(kMaxPoints);
}

// Csupa 0 dB-es pont: nincs hatása.
inline bool isFlat(const Envelope &e)
{
    return std::all_of(e.begin(), e.end(), [](const Point &p) { return std::abs(p.db) < 0.01; });
}

// "ms:dB;ms:dB" – az adatbázisban és a távoli szerkesztésben így utazik.
inline QString toString(Envelope e)
{
    normalize(e);
    if (isFlat(e)) return {};
    QStringList parts;
    for (const Point &p : e) parts << QStringLiteral("%1:%2").arg(p.ms).arg(p.db, 0, 'f', 1);
    return parts.join(QLatin1Char(';'));
}

inline Envelope fromString(const QString &s)
{
    Envelope e;
    for (const QString &part : s.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const int c = part.indexOf(QLatin1Char(':'));
        if (c <= 0) continue;
        bool ok1 = false, ok2 = false;
        Point p;
        p.ms = part.left(c).toLongLong(&ok1);
        p.db = part.mid(c + 1).toDouble(&ok2);
        if (ok1 && ok2 && std::isfinite(p.db)) e << p;
    }
    normalize(e);
    return e;
}

inline double dbAt(const Envelope &e, qint64 ms)
{
    if (e.isEmpty()) return 0;
    if (ms <= e.first().ms) return e.first().db;
    if (ms >= e.last().ms) return e.last().db;
    for (int i = 1; i < e.size(); ++i)
        if (ms <= e[i].ms) {
            const Point &a = e[i - 1], &b = e[i];
            const double t = b.ms > a.ms ? double(ms - a.ms) / double(b.ms - a.ms) : 1.0;
            return a.db + (b.db - a.db) * t;
        }
    return e.last().db;
}

// A hangmotornak: (frame, lineáris erősítés) párok.
inline std::vector<std::pair<int64_t, float>> toSource(const Envelope &e, int rate)
{
    std::vector<std::pair<int64_t, float>> out;
    if (isFlat(e)) return out;
    out.reserve(size_t(e.size()));
    for (const Point &p : e) out.emplace_back(int64_t(p.ms) * rate / 1000, float(p.db <= kMinDb - 0.01 ? 0.0 : std::pow(10.0, p.db / 20.0)));
    return out;
}

inline double toGain(double db) { return db <= kMinDb - 0.01 ? 0.0 : std::pow(10.0, db / 20.0); }

} // namespace env
