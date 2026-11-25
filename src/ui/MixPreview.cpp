#include "MixPreview.h"

#include "Theme.h"
#include "i18n.h"
#include "core/MusicKey.h"

#include <QLocale>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kStep = 100;      // ms, a hangerő-kép lépése

double dbAt(const std::vector<float> &env, qint64 ms)
{
    if (env.empty() || ms < 0) return -90;
    const size_t i = size_t(ms / kStep);
    if (i >= env.size()) return -90;
    return 20.0 * std::log10(std::max(1e-5f, env[i]));
}

// Egy elem hangerő-képe lépésenként megadott dB-értékekből (determinisztikus „zenei” ingadozással).
struct Builder {
    std::vector<float> env;
    unsigned seed;
    explicit Builder(unsigned s) : seed(s) {}
    double wobble()
    {
        seed = seed * 1103515245u + 12345u;
        const double r = double((seed >> 16) & 0x7fff) / 32767.0;   // 0–1
        const size_t i = env.size();
        return 1.2 * std::sin(double(i) * 0.9) + 0.6 * std::sin(double(i) * 0.23) + (r - 0.5) * 1.6;
    }
    void add(double ms, double fromDb, double toDb, bool music = true)
    {
        const int n = std::max(1, int(ms / kStep));
        for (int k = 0; k < n; ++k) {
            const double t = n > 1 ? double(k) / (n - 1) : 1.0;
            double db = fromDb + (toDb - fromDb) * t;
            if (db > -80 && music) db += wobble();
            env.push_back(float(std::pow(10.0, std::min(0.0, db) / 20.0)));
        }
    }
    void silence(double ms) { add(ms, -90, -90, false); }
};

QString sec(qint64 ms) { return QLocale().toString(ms / 1000.0, 'f', 1); }

} // namespace

MixPreview::MixPreview(QWidget *parent) : QWidget(parent)
{
    setObjectName("mixPreview");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(236);
    makeScenario(0, &out_, &in_);
    recompute();
}

QStringList MixPreview::scenarioNames()
{
    return {TR("Lecsengő vég → lágy intró"),
            TR("Lecsengő vég → erős kezdés"),
            TR("Hirtelen vég → erős kezdés"),
            TR("Hirtelen vég → lágy intró"),
            TR("Eltérő tempó (128 → 96 BPM), lecsengő vég → lágy intró"),
            TR("Zene → jingle"),
            TR("Jingle → zene"),
            TR("Rejtett szám (a dal után csend, majd még valami) → zene")};
}

void MixPreview::makeScenario(int index, Item *out, Item *in)
{
    const double body = -9;
    auto song = [&](bool fadeEnd, unsigned seed) {
        Builder b(seed);
        b.silence(300);
        b.add(60000 - (fadeEnd ? 14000 : 600) - 300, body, body);
        if (fadeEnd) b.add(14000, body, -60);
        else b.silence(600);
        return b.env;
    };
    auto start = [&](bool soft, unsigned seed) {
        Builder b(seed);
        b.silence(500);
        if (soft) b.add(6000, -38, body);
        b.add(60000 - 500 - (soft ? 6000 : 0), body, body);
        return b.env;
    };
    auto jingle = [&](unsigned seed) {
        Builder b(seed);
        b.silence(200);
        b.add(8200, -6, -6);
        b.add(300, -6, -40);
        b.silence(500);
        return b.env;
    };
    *out = Item();
    *in = Item();
    out->name = TR("Kimenő");
    in->name = TR("Bejövő");
    switch (index) {
    default:
    case 0: out->env = song(true, 7); in->env = start(true, 11); break;
    case 1: out->env = song(true, 7); in->env = start(false, 11); break;
    case 2: out->env = song(false, 7); in->env = start(false, 11); break;
    case 3: out->env = song(false, 7); in->env = start(true, 11); break;
    case 4:
        out->env = song(true, 7);
        in->env = start(true, 11);
        out->bpm = 128;
        in->bpm = 96;
        out->key = mkey::make(9, true);     // A-moll
        in->key = mkey::make(6, false);     // Fisz-dúr: nem illik
        break;
    case 5:
        out->env = song(true, 7);
        in->env = jingle(5);
        in->music = false;
        in->name = TR("Bejövő (jingle)");
        break;
    case 6:
        out->env = jingle(5);
        out->music = false;
        out->name = TR("Kimenő (jingle)");
        in->env = start(false, 11);
        break;
    case 7: {
        Builder b(7);
        b.silence(300);
        b.add(50000 - 300 - 600, body, body);
        b.add(600, body, -60);
        b.silence(8000);
        b.add(11000, -15, -15);
        b.silence(1000);
        out->env = b.env;
        in->env = start(false, 11);
        break;
    }
    }
    out->durMs = qint64(out->env.size()) * kStep;
    in->durMs = qint64(in->env.size()) * kStep;
}

void MixPreview::setScenario(int index)
{
    scenario_ = std::clamp(index, 0, int(scenarioNames().size()) - 1);
    makeScenario(scenario_, &out_, &in_);
    recompute();
}

void MixPreview::setValues(const eter::SmartMixSettings &s, double mixThresholdDb, double cueThresholdDb, int defaultFadeMs)
{
    s_ = s;
    mixDb_ = mixThresholdDb;
    cueDb_ = cueThresholdDb;
    fadeMs_ = std::max(100, defaultFadeMs);
    recompute();
}

void MixPreview::recompute()
{
    Result r;
    const bool useSmart = s_.enabled && ((out_.music && in_.music) || s_.allItems);
    if (useSmart) {
        const eter::MixProfile po = eter::analyzeMixProfile(out_.env, out_.durMs);
        const eter::MixProfile pi = eter::analyzeMixProfile(in_.env, in_.durMs);
        r.plan = eter::planAutoMix(po, pi, out_.music, in_.music, out_.bpm, in_.bpm, out_.key, in_.key, s_);
        if (r.plan.valid) {
            r.smart = true;
            r.mixOutMs = r.plan.mixOutMs;
            r.outEndMs = r.plan.outEndMs;
            r.fadeOutMs = r.plan.fadeOutMs;
            r.cueInMs = r.plan.cueInMs;
            r.hiddenAtMs = r.plan.hiddenAtMs;
        }
    }
    if (!r.smart) {
        // Egyszerű mód: a mix pont az utolsó hely a küszöb fölött, onnan kiúszik; a bejövő a kezdőpont küszöbénél indul.
        qint64 last = 0;
        for (size_t i = 0; i < out_.env.size(); ++i)
            if (dbAt(out_.env, qint64(i) * kStep) >= mixDb_) last = qint64(i + 1) * kStep;
        r.mixOutMs = last > 0 ? last : out_.durMs;
        r.fadeOutMs = fadeMs_;
        r.outEndMs = std::min(out_.durMs, r.mixOutMs + fadeMs_);
        r.cueInMs = 0;
        for (size_t i = 0; i < in_.env.size(); ++i)
            if (dbAt(in_.env, qint64(i) * kStep) >= cueDb_) {
                r.cueInMs = qint64(i) * kStep;
                break;
            }
    }
    r_ = r;

    QStringList p;
    const qint64 overlap = std::max<qint64>(0, r.outEndMs - r.mixOutMs);
    if (r.smart) {
        const eter::SmartMixPlan &pl = r.plan;
        if (pl.segue) {
            p << (s_.segueGapMs > 0 ? TR("Nem zenei elem: a hallható vége után %1 mp szünettel jön a következő.").arg(sec(s_.segueGapMs))
                                    : TR("Nem zenei elem: a hallható vége után szorosan jön a következő."));
        } else {
            QStringList why;
            why << (pl.fadeEnding ? TR("lecsengő vég (%1 mp)").arg(sec(pl.fadeLenMs)) : TR("hirtelen vég"));
            if (in_.music) why << (pl.softIntro ? TR("lágy intró (%1 mp)").arg(sec(pl.introRampMs)) : TR("erős kezdés"));
            p << TR("Okos mix: %1.").arg(why.join(QStringLiteral(", ")));
            if (pl.flowLimited)
                p << TR("Eltérő tempó (%1 %) vagy nem illő hangnem: az átfedés legfeljebb %2 mp, a kimenő végig halkul.")
                         .arg(pl.tempoJumpPct)
                         .arg(sec(s_.clashOverlapMs));
        }
        if (r.hiddenAtMs > 0) p << TR("Rejtett szám: a kimenő a %1. mp-nél, a csend elején véget ér.").arg(sec(r.hiddenAtMs));
    } else {
        p << TR("Egyszerű mód: a mix pont a %1 dB-es küszöb utolsó átlépése, onnan %2 mp kiúsztatás.")
                 .arg(QLocale().toString(mixDb_, 'f', 0), sec(r.fadeOutMs));
    }
    QStringList nums;
    nums << TR("Átfedés: %1 mp").arg(sec(overlap));
    if (overlap > 0) nums << TR("kiúsztatás: %1 mp").arg(sec(std::min<qint64>(r.fadeOutMs, overlap)));
    if (r.cueInMs > 0) nums << TR("a bejövő elejéből %1 mp kimarad").arg(sec(r.cueInMs));
    p << nums.join(QStringLiteral(" · ")) + QLatin1Char('.');
    summary_ = p.join(QLatin1Char(' '));
    emit summaryChanged(summary_);
    update();
}

void MixPreview::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF all = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(theme::border, 1));
    p.setBrush(theme::bg);
    p.drawRoundedRect(all, 6, 6);

    const double left = 86, right = width() - 14;
    const double rulerY = 24;
    const double laneH = 78;
    const QRectF laneA(left, 34, right - left, laneH), laneB(left, 34 + laneH + 14, right - left, laneH);

    // Az időablak: a mix pont előtt 10 mp, utána legalább 14 mp (a kimenő vége mindig látszik).
    const qint64 t0 = r_.mixOutMs - 10000;
    const qint64 span = std::max<qint64>(24000, r_.outEndMs - t0 + 4000);
    auto x = [&](qint64 t) { return left + double(t - t0) * (right - left) / double(span); };

    QFont small = font();
    small.setPixelSize(11);
    QFont mono(QStringLiteral("IBM Plex Mono"));
    mono.setPixelSize(10);

    // Idővonal: 2 mp-enként, a mix ponthoz képest.
    p.setFont(mono);
    for (qint64 k = -10; k * 1000 <= span - 10000; k += 2) {
        const double xx = x(r_.mixOutMs + k * 1000);
        p.setPen(QPen(theme::border, 1));
        p.drawLine(QPointF(xx, rulerY + 2), QPointF(xx, laneB.bottom()));
        p.setPen(theme::muted);
        const QString t = k == 0 ? TR("mix pont") : TR("%1 mp").arg((k > 0 ? QStringLiteral("+") : QString()) + QString::number(k));
        if (xx + 34 <= width()) p.drawText(QRectF(xx - 40, 4, 80, 16), Qt::AlignCenter, t);
    }

    // Az átfedés sávja.
    if (r_.outEndMs > r_.mixOutMs) {
        QColor ov = theme::amber;
        ov.setAlphaF(0.12);
        p.fillRect(QRectF(x(r_.mixOutMs), laneA.top(), x(r_.outEndMs) - x(r_.mixOutMs), laneB.bottom() - laneA.top()), ov);
    }

    // Hullámforma: a hallható rész a lejátszó színével, a nem szóló rész halványan.
    auto drawWave = [&](const QRectF &lane, const std::vector<float> &env, qint64 offset, auto gainAt, const QColor &col) {
        const double mid = lane.center().y(), half = lane.height() / 2 - 3;
        const int px0 = int(std::floor(lane.left())), px1 = int(std::ceil(lane.right()));
        for (int px = px0; px < px1; ++px) {
            const qint64 t = t0 + qint64(double(px - left) * span / (right - left));   // a kimenő idejében
            const qint64 local = t - offset;
            if (local < 0 || local >= qint64(env.size()) * kStep) continue;
            const double db = dbAt(env, local);
            const double h = std::clamp((db + 60.0) / 60.0, 0.0, 1.0) * half;
            if (h < 0.5) continue;
            const double g = gainAt(local);
            QColor c = g > 0.001 ? col : theme::muted;
            c.setAlphaF(g > 0.001 ? 0.35 + 0.55 * g : 0.22);
            p.setPen(QPen(c, 1));
            p.drawLine(QPointF(px + 0.5, mid - h), QPointF(px + 0.5, mid + h));
        }
    };
    const qint64 fadeStart = r_.outEndMs - r_.fadeOutMs;
    auto outGain = [&](qint64 t) {
        if (t >= r_.outEndMs) return 0.0;
        if (t <= fadeStart) return 1.0;
        return double(r_.outEndMs - t) / double(std::max(1, r_.fadeOutMs));
    };
    const qint64 inOffset = r_.mixOutMs - r_.cueInMs;     // a bejövő fájl 0. ms-a a kimenő idejében
    auto inGain = [&](qint64 t) { return t >= r_.cueInMs ? 1.0 : 0.0; };
    drawWave(laneA, out_.env, 0, outGain, theme::playerColor(0));
    drawWave(laneB, in_.env, inOffset, inGain, theme::playerColor(1));

    // A kimenő hangerőgörbéje (1 = a sáv teteje).
    {
        QPainterPath path;
        const double top = laneA.top() + 2, bottom = laneA.bottom() - 2;
        bool first = true;
        for (int px = int(laneA.left()); px <= int(laneA.right()); px += 2) {
            const qint64 t = t0 + qint64(double(px - left) * span / (right - left));
            const double y = bottom - (bottom - top) * outGain(t);
            if (first) path.moveTo(px, y);
            else path.lineTo(px, y);
            first = false;
        }
        p.setPen(QPen(theme::text, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    // A bejövő indulása (a hangerő a kezdőponttól teljes).
    {
        const double xs = x(r_.mixOutMs);
        p.setPen(QPen(theme::text, 1.4));
        p.drawLine(QPointF(std::max(laneB.left(), xs), laneB.top() + 2), QPointF(laneB.right(), laneB.top() + 2));
    }

    // Jelölők: mix pont és a kimenő vége.
    auto marker = [&](qint64 t, const QColor &c, const QString &label, bool below) {
        const double xx = x(t);
        if (xx < left || xx > right) return;
        QPen pen(c, 1.4, Qt::DashLine);
        p.setPen(pen);
        p.drawLine(QPointF(xx, laneA.top() - 2), QPointF(xx, laneB.bottom() + 2));
        if (label.isEmpty()) return;
        p.setFont(small);
        const QRectF box(xx + 4, below ? laneB.bottom() - 16 : laneA.top() + 2, 120, 14);
        p.setPen(c);
        p.drawText(box, Qt::AlignLeft | Qt::AlignVCenter, label);
    };
    marker(r_.mixOutMs, theme::amber, TR("a bejövő indul"), false);
    if (r_.outEndMs != r_.mixOutMs) marker(r_.outEndMs, theme::muted, TR("a kimenő vége"), true);

    // A sávok neve.
    p.setFont(small);
    auto laneLabel = [&](const QRectF &lane, const QString &name, const QColor &c) {
        p.setPen(c);
        p.drawText(QRectF(10, lane.top(), left - 16, lane.height()), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, name);
    };
    laneLabel(laneA, out_.name, theme::playerColor(0));
    laneLabel(laneB, in_.name, theme::playerColor(1));

    // Lábléc: jelmagyarázat.
    p.setFont(small);
    p.setPen(theme::muted);
    p.drawText(QRectF(left, laneB.bottom() + 6, right - left, 16), Qt::AlignLeft | Qt::AlignVCenter,
               TR("Vonal: a hangerő · kiemelt sáv: a két elem együtt szól · halvány: nem hallatszik"));
}
