#include "Analysis.h"
#include "Decoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <complex>
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

// Iteratív radix-2 FFT (helyben).
static void fft(std::vector<std::complex<float>> &a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2 * 3.14159265358979323846 / double(len);
        const std::complex<float> wl(float(std::cos(ang)), float(std::sin(ang)));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (size_t k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

int estimateKey(const std::string &path, const std::atomic<bool> &cancel, double *confidence)
{
    if (confidence) *confidence = 0;
    Decoder dec;
    if (!dec.open(path)) return 0;
    const int64_t dur = dec.durationFrames();
    const int64_t want = int64_t(kRate) * 120;
    if (dur > want + kRate * 20) dec.seek(std::max<int64_t>(kRate * 10, (dur - want) / 2));

    // 12 kHz-re ritkítva (4 minta átlaga), egycsatornásan: a hangnemhez elég a 2 kHz alatti tartomány.
    constexpr int dec4 = 4;
    constexpr double rate = double(kRate) / dec4;
    constexpr int N = 8192, hop = 4096;                  // ~1,5 Hz felbontás, ~0,34 s lépés
    std::vector<float> mono;
    mono.reserve(size_t(want / dec4) + 16);
    std::vector<float> buf(size_t(4096) * 2);
    int64_t done = 0;
    float acc = 0.f;
    int accN = 0;
    while (done < want) {
        if (cancel.load()) return 0;
        const int n = dec.read(buf.data(), int(std::min<int64_t>(4096, want - done)));
        if (n <= 0) break;
        for (int i = 0; i < n; ++i) {
            acc += 0.5f * (buf[size_t(i) * 2] + buf[size_t(i) * 2 + 1]);
            if (++accN == dec4) {
                mono.push_back(acc / dec4);
                acc = 0.f;
                accN = 0;
            }
        }
        done += n;
    }
    if (mono.size() < size_t(rate * 15)) return 0;       // legalább 15 másodperc kell

    // Bin → hangosztály súlyok (háromszög: a félhang közepén 1, a félhangok határán 0), 65 Hz … 2 kHz.
    struct Bin { int k; int pc; float w; };
    std::vector<Bin> bins;
    for (int k = 1; k < N / 2; ++k) {
        const double f = k * rate / N;
        if (f < 65 || f > 2000) continue;
        const double midi = 69 + 12 * std::log2(f / 440.0);
        const double nearest = std::round(midi);
        const float w = float(std::max(0.0, 1.0 - 2.0 * std::abs(midi - nearest)));
        if (w > 0) bins.push_back({k, int(((long long)nearest % 12 + 12) % 12), w});
    }
    std::vector<float> window(N);
    for (int i = 0; i < N; ++i) window[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * 3.14159265358979323846 * i / (N - 1)));

    double chroma[12] = {0};
    std::vector<std::complex<float>> spec(N);
    std::vector<double> frameEnergy;
    struct Frame { double c[12]; double e; };
    std::vector<Frame> frames;
    for (size_t start = 0; start + N <= mono.size(); start += hop) {
        if (cancel.load()) return 0;
        for (int i = 0; i < N; ++i) spec[size_t(i)] = {mono[start + size_t(i)] * window[size_t(i)], 0.f};
        fft(spec);
        Frame fr{};
        for (const Bin &b : bins) {
            const double m = std::abs(spec[size_t(b.k)]);
            fr.c[b.pc] += b.w * m;
            fr.e += b.w * m;
        }
        frames.push_back(fr);
    }
    if (frames.empty()) return 0;
    // A csendes részek ne számítsanak; a hangos keretek egyenként normálva (egy hangos rész se nyomja el a többit).
    std::vector<double> es;
    for (const Frame &f : frames) es.push_back(f.e);
    std::vector<double> sorted = es;
    std::nth_element(sorted.begin(), sorted.begin() + long(sorted.size() / 2), sorted.end());
    const double median = sorted[sorted.size() / 2];
    if (median <= 1e-6) return 0;
    for (const Frame &f : frames) {
        if (f.e < median * 0.25) continue;
        double mx = 0;
        for (double v : f.c) mx = std::max(mx, v);
        if (mx <= 0) continue;
        for (int p = 0; p < 12; ++p) chroma[p] += f.c[p] / mx;
    }

    // Krumhansl–Kessler hangnemprofilok; Pearson-korreláció mind a 24 hangnemmel.
    static const double majorP[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static const double minorP[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    auto corr = [&](const double *prof, int tonic) {
        double mx = 0, my = 0;
        for (int i = 0; i < 12; ++i) { mx += chroma[i]; my += prof[i]; }
        mx /= 12;
        my /= 12;
        double sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < 12; ++i) {
            const double x = chroma[(i + tonic) % 12] - mx, y = prof[i] - my;
            sxy += x * y;
            sxx += x * x;
            syy += y * y;
        }
        return sxx > 0 && syy > 0 ? sxy / std::sqrt(sxx * syy) : 0.0;
    };
    double best = -2, second = -2;
    int bestKey = 0;
    for (int tonic = 0; tonic < 12; ++tonic)
        for (int minor = 0; minor < 2; ++minor) {
            const double r = corr(minor ? minorP : majorP, tonic);
            if (r > best) {
                second = best;
                best = r;
                bestKey = 1 + tonic * 2 + minor;
            } else if (r > second) {
                second = r;
            }
        }
    if (std::getenv("ETER_KEY_DEBUG")) std::fprintf(stderr, "  hangnem: legjobb %.3f, második %.3f\n", best, second);
    if (confidence) *confidence = std::clamp(best - second, 0.0, 1.0);
    if (best < 0.5) return 0;                             // nincs határozott hangnem (beszéd, zaj, ütemek)
    return bestKey;
}

} // namespace eter
