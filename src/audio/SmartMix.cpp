#include "SmartMix.h"

#include "core/MusicKey.h"

#include <algorithm>
#include <cmath>

namespace eter {

static constexpr int kStepMs = 100;

MixProfile analyzeMixProfile(const std::vector<float> &env, int64_t durMs)
{
    MixProfile p;
    const int n = int(env.size());
    if (n < 50) return p;   // 5 mp alatt nincs értelme (jingle-szerű)
    p.durMs = durMs > 0 ? durMs : int64_t(n) * kStepMs;
    p.rawDb.resize(n);
    std::vector<double> pw(n);
    for (int i = 0; i < n; ++i) {
        const double v = std::max(1e-6f, env[i]);
        p.rawDb[i] = float(20.0 * std::log10(v));
        pw[i] = v * v;
    }
    // 1 mp-es (±5 lépés) középre igazított teljesítmény-átlag.
    p.smoothDb.resize(n);
    std::vector<double> pre(n + 1, 0.0);
    for (int i = 0; i < n; ++i) pre[i + 1] = pre[i] + pw[i];
    for (int i = 0; i < n; ++i) {
        const int a = std::max(0, i - 5), b = std::min(n, i + 6);
        p.smoothDb[i] = float(10.0 * std::log10(std::max(1e-12, (pre[b] - pre[a]) / (b - a))));
    }
    std::vector<float> body;
    for (int i = n / 10; i < n * 9 / 10; ++i)
        if (p.smoothDb[i] > -60) body.push_back(p.smoothDb[i]);
    if (body.size() < 20) return p;
    std::nth_element(body.begin(), body.begin() + body.size() * 7 / 10, body.end());
    p.bodyDb = body[body.size() * 7 / 10];
    p.valid = true;
    return p;
}

std::vector<float> envFromPeaks(const std::vector<float> &peaks, int64_t durMs)
{
    std::vector<float> env;
    if (peaks.empty() || durMs <= 0) return env;
    const int64_t n = std::max<int64_t>(1, (durMs + kStepMs - 1) / kStepMs);
    env.assign(size_t(n), 0.f);
    const size_t m = peaks.size();
    for (size_t i = 0; i < m; ++i) {
        const int64_t t = int64_t(i) * durMs / int64_t(m);
        const size_t k = size_t(std::min<int64_t>(n - 1, t / kStepMs));
        env[k] = std::max(env[k], peaks[i]);
    }
    // A ritkább csúcs-sornál maradt lyukak: az előző értékkel.
    for (size_t k = 1; k < env.size(); ++k)
        if (env[k] == 0.f && m < size_t(n)) env[k] = env[k - 1];
    return env;
}

int tempoJumpPercent(double a, double b)
{
    if (a <= 0 || b <= 0) return -1;
    double ratio = b / a;
    if (ratio < 0.75) ratio *= 2;
    if (ratio > 1.5) ratio /= 2;
    return int(std::lround(std::abs(ratio - 1.0) * 100.0));
}

SmartMixPlan planSmartMix(const MixProfile &out, const MixProfile &in, double outBpm, double inBpm, int outKey,
                          int inKey, const SmartMixSettings &s)
{
    SmartMixPlan r;
    if (!out.valid) return r;
    const int n = int(out.rawDb.size());

    // A kimenő szám hallható vége: az utolsó pont, ahol a jel a szám szintje alatt legfeljebb endDepthDb-lel van.
    const double endThr = std::max(-60.0, out.bodyDb - s.endDepthDb);
    int e = n - 1;
    while (e > 0 && out.rawDb[e] < endThr) --e;
    const int64_t endMs = std::min<int64_t>(out.durMs, int64_t(e + 1) * kStepMs);
    // A lecsengés kezdete: visszafelé az utolsó pont, ahol a simított szint még a szám szintje közelében (−3 dB) van.
    int f = e;
    while (f > 0 && out.smoothDb[f] < out.bodyDb - 3) --f;
    r.fadeLenMs = int(std::max<int64_t>(0, endMs - int64_t(f) * kStepMs - 500));   // a simítás fél ablaka
    r.fadeEnding = r.fadeLenMs >= 3000;
    int overlapFade = 0;
    if (r.fadeEnding) {
        int i = f;
        while (i < e && out.smoothDb[i] > out.bodyDb - s.fadeDepthDb) ++i;
        overlapFade = int(endMs - int64_t(i) * kStepMs);
    }

    // A bejövő szám eleje: az eleji csend vége, és mennyi idő alatt ér fel a szám szintjéhez (−6 dB).
    if (in.valid) {
        const int m = int(in.rawDb.size());
        const double startThr = std::max(-45.0, in.bodyDb - 40);
        int a = 0;
        while (a < m - 1 && in.rawDb[a] < startThr) ++a;
        int b = a;
        while (b < m - 1 && in.smoothDb[b] < in.bodyDb - 3) ++b;
        r.cueInMs = std::max<int64_t>(0, int64_t(a) * kStepMs - 50);
        r.introRampMs = int(std::max<int64_t>(0, int64_t(b - a) * kStepMs - 300));
        r.softIntro = r.introRampMs >= 2000;
    }

    int overlap = 0;
    if (r.fadeEnding) overlap = overlapFade;
    if (r.softIntro) overlap = std::max(overlap, std::min(r.introRampMs, s.introMaxMs));
    if (!r.fadeEnding && !r.softIntro) overlap = s.tightMs;

    r.tempoJumpPct = tempoJumpPercent(outBpm, inBpm);
    r.keyClash = mkey::valid(outKey) && mkey::valid(inKey) && !mkey::compatible(outKey, inKey);
    const bool tempoClash = r.tempoJumpPct > s.tempoTolPct;
    r.flowMatch = r.tempoJumpPct >= 0 && !tempoClash && mkey::valid(outKey) && mkey::valid(inKey) && !r.keyClash;
    if (s.flowAware && (tempoClash || r.keyClash) && overlap > s.clashOverlapMs) {
        overlap = s.clashOverlapMs;
        r.flowLimited = true;
    }
    overlap = std::clamp(overlap, 0, s.maxOverlapMs);

    r.mixOutMs = std::max<int64_t>(out.durMs / 2, endMs - overlap);
    r.overlapMs = int(endMs - r.mixOutMs);
    r.outEndMs = endMs;
    // A kimenő szám kiúsztatása: lecsengő végnél csak a legvégén (a szám maga halkul), korlátozott átfedésnél az egész
    // átfedés alatt, egyébként legfeljebb másfél másodperc a végén.
    r.fadeOutMs = r.fadeEnding ? std::min(1000, r.overlapMs) : r.flowLimited ? r.overlapMs : std::min(1500, r.overlapMs);
    r.fadeOutMs = std::max(r.fadeOutMs, 100);
    r.valid = true;
    return r;
}

} // namespace eter
