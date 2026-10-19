#include "Analysis.h"
#include "Decoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace eter {

double estimateBpm(const std::string &path, const std::atomic<bool> &cancel)
{
    Decoder dec;
    if (!dec.open(path)) return 0;
    // A szám eleje (felvezetés) és vége (lecsengés) gyakran ütem nélküli: a közepét nézzük.
    const int64_t dur = dec.durationFrames();
    const int64_t want = int64_t(kRate) * 60;
    if (dur > want + kRate * 20) dec.seek(std::max<int64_t>(kRate * 10, (dur - want) / 2));

    // Ütésjel: rövid ablakonként a jel változásának (első differencia) energiája, logaritmikusan, és ennek
    // a növekedése. Ez a dobütéseknél és a hangindításoknál ugrik meg.
    constexpr int hop = 256;                         // 187,5 ablak másodpercenként
    constexpr double fr = double(kRate) / hop;
    std::vector<float> buf(size_t(hop) * 2 * 32);
    // Két sáv: a mély (lábdob, basszus: egypólusú aluláteresztő ~150 Hz) és a magas (első differencia: pergő, cin).
    std::vector<double> lowE, highE;
    lowE.reserve(size_t(want / hop) + 8);
    highE.reserve(size_t(want / hop) + 8);
    float prevSample = 0.f, low = 0.f;
    const float alpha = float(1.0 - std::exp(-2.0 * 3.14159265358979 * 150.0 / kRate));
    double accLow = 0, accHigh = 0;
    int inHop = 0;
    int64_t done = 0;
    while (done < want) {
        if (cancel.load()) return 0;
        const int n = dec.read(buf.data(), int(std::min<int64_t>(hop * 32, want - done)));
        if (n <= 0) break;
        for (int i = 0; i < n; ++i) {
            const float m = 0.5f * (buf[size_t(i) * 2] + buf[size_t(i) * 2 + 1]);
            const float d = m - prevSample;
            prevSample = m;
            low += alpha * (m - low);
            accLow += double(low) * low;
            accHigh += double(d) * d;
            if (++inHop == hop) {
                lowE.push_back(std::log(1e-9 + accLow / hop));
                highE.push_back(std::log(1e-9 + accHigh / hop));
                accLow = accHigh = 0;
                inHop = 0;
            }
        }
        done += n;
    }
    if (lowE.size() < size_t(fr * 15)) return 0;     // legalább 15 másodperc kell
    // Az ütésjel a két sáv növekedésének összege, sávonként a saját szórásával mérve (egyik se nyomja el a másikat).
    std::vector<double> onset(lowE.size(), 0.0);
    for (const std::vector<double> *band : {&lowE, &highE}) {
        std::vector<double> f(band->size(), 0.0);
        double sum = 0, sq = 0;
        for (size_t i = 1; i < band->size(); ++i) {
            f[i] = std::max(0.0, (*band)[i] - (*band)[i - 1]);
            sum += f[i];
            sq += f[i] * f[i];
        }
        const double cnt = double(f.size()), mean = sum / cnt, sd = std::sqrt(std::max(1e-12, sq / cnt - mean * mean));
        for (size_t i = 0; i < f.size(); ++i) onset[i] += f[i] / sd;
    }
    // A lassú változás (hangerő-hullámzás) kivonása: mozgó átlag 0,5 másodpercre.
    {
        const int w = int(fr / 2);
        std::vector<double> avg(onset.size());
        double sum = 0;
        for (size_t i = 0; i < onset.size(); ++i) {
            sum += onset[i];
            if (i >= size_t(w)) sum -= onset[i - w];
            avg[i] = sum / std::min<size_t>(i + 1, size_t(w));
        }
        for (size_t i = 0; i < onset.size(); ++i) onset[i] = std::max(0.0, onset[i] - avg[i]);
    }
    // Enyhe simítás: az ütések egy-két ablaknyi eltérése se vesszen el.
    {
        std::vector<double> sm(onset.size(), 0.0);
        for (size_t i = 1; i + 1 < onset.size(); ++i) sm[i] = 0.25 * onset[i - 1] + 0.5 * onset[i] + 0.25 * onset[i + 1];
        onset.swap(sm);
    }
    // Autokorreláció a 60–200 BPM-nek megfelelő késleltetésekre (és többszöröseikre).
    const int minLag = int(std::floor(60.0 * fr / 200.0)), maxLag = int(std::ceil(60.0 * fr / 60.0));
    std::vector<double> ac(size_t(maxLag) * 4 + 8, 0.0);
    const size_t n = onset.size();
    double zero = 0;
    for (size_t i = 0; i < n; ++i) zero += onset[i] * onset[i];
    if (zero <= 0) return 0;
    for (int lag = 1; size_t(lag) < ac.size() && size_t(lag) < n; ++lag) {
        double s = 0;
        for (size_t i = size_t(lag); i < n; ++i) s += onset[i] * onset[i - lag];
        ac[size_t(lag)] = s / double(n - size_t(lag));
    }
    // Pontszám: a késleltetés és többszörösei (az ütem ismétlődik: 1, 2, 3, 4 ütés múlva is egyezik), enyhe előnnyel
    // a 110 BPM körüli tempóknak a fél- vagy dupla tempó összetévesztése ellen.
    const double zeroNorm = zero / double(n);
    // A késleltetés nem egész szám: a többszöröseknél a hiba nő, ezért ott kis környezetben a legnagyobb érték számít.
    auto at = [&](double l) {
        const int c = int(std::lround(l)), w = 1 + int(l / 150.0);
        double m = 0;
        for (int i = c - w; i <= c + w; ++i)
            if (i > 0 && size_t(i) < ac.size()) m = std::max(m, ac[size_t(i)]);
        return m / zeroNorm;
    };
    double best = 0;
    int bestLag = 0;
    for (int l = minLag; l <= maxLag; ++l) {
        const double bpm = 60.0 * fr / l;
        const double prior = std::exp(-0.5 * std::pow(std::log2(bpm / 110.0) / 0.7, 2));
        const double comb = at(l) + 0.6 * at(2.0 * l) + 0.4 * at(3.0 * l) + 0.3 * at(4.0 * l);
        const double score = comb * prior;
        if (score > best) {
            best = score;
            bestLag = l;
        }
    }
    if (!bestLag) return 0;
    // Határozott ütem kell: a késleltetésnél a jel önmagához hasonlítva is erősen egyezzen.
    const double strength = std::max({at(bestLag), at(bestLag - 1), at(bestLag + 1)});
    if (std::getenv("ETER_BPM_DEBUG")) std::fprintf(stderr, "  lag %d erősség %.3f pont %.3f\n", bestLag, strength, best);
    if (strength < 0.18) return 0;
    // Finomítás a szomszédos késleltetésekkel (parabola).
    double lag = bestLag;
    if (bestLag > minLag && bestLag < maxLag) {
        const double a = ac[size_t(bestLag) - 1], b = ac[size_t(bestLag)], c = ac[size_t(bestLag) + 1];
        const double den = a - 2 * b + c;
        if (std::abs(den) > 1e-12) lag += std::clamp(0.5 * (a - c) / den, -0.5, 0.5);
    }
    const double bpm = 60.0 * fr / lag;
    return std::round(bpm * 10.0) / 10.0;
}

} // namespace eter
