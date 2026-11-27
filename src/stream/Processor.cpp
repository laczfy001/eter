#include "stream/Processor.h"

#include <algorithm>
#include <cmath>

namespace stream {

static constexpr int kRate = 48000;
static constexpr int kBlock = kRate / 10;       // 100 ms
static constexpr int kBlocks = 30;              // 3 mp rövid távú hangosság
static constexpr int kTpDelay = 7;              // a true-peak szűrő késése (+1)

static inline double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
static inline double linToDb(double v) { return v > 1e-9 ? 20.0 * std::log10(v) : -180.0; }

ProcConfig ProcConfig::fromPreset(int p)
{
    ProcConfig c;
    c.preset = p;
    switch (p) {
    case Off:
        c.agc = c.comp = c.limiter = false;
        break;
    case Medium:
        c.agc = true;
        c.agcMaxGainDb = 12;
        c.agcMaxCutDb = 18;
        c.agcSpeedSec = 5;
        c.comp = true;
        c.compThresholdDb = -20;
        c.compRatio = 2.5;
        c.compAttackMs = 10;
        c.compReleaseMs = 250;
        break;
    case Strong:
        c.agc = true;
        c.agcMaxGainDb = 15;
        c.agcMaxCutDb = 20;
        c.agcSpeedSec = 3;
        c.comp = true;
        c.compThresholdDb = -24;
        c.compRatio = 4;
        c.compAttackMs = 5;
        c.compReleaseMs = 150;
        break;
    default:            // Light (és az egyéni alapja)
        c.agc = true;
        c.agcMaxGainDb = 9;
        c.agcMaxCutDb = 15;         // a túl hangos jelből többet vehet le, mint amennyit a halkba hozzáad
        c.agcSpeedSec = 8;
        c.comp = false;
        break;
    }
    return c;
}

Processor::Processor()
{
    // ITU-R BS.1770 K-súlyozás 48 kHz-en: magas polc + felüláteresztő.
    for (int ch = 0; ch < 2; ++ch) {
        kShelf_[ch].b0 = 1.53512485958697;
        kShelf_[ch].b1 = -2.69169618940638;
        kShelf_[ch].b2 = 1.19839281085285;
        kShelf_[ch].a1 = -1.69065929318241;
        kShelf_[ch].a2 = 0.73248077421585;
        kHigh_[ch].b0 = 1.0;
        kHigh_[ch].b1 = -2.0;
        kHigh_[ch].b2 = 1.0;
        kHigh_[ch].a1 = -1.99004745483398;
        kHigh_[ch].a2 = 0.99007225036621;
    }
    reset();
}

void Processor::configure(const ProcConfig &c)
{
    const bool limChanged = c.limiter != cfg_.limiter;
    cfg_ = c;
    if (cfg_.agc) {
        agcDb_ = std::clamp(agcDb_, -cfg_.agcMaxCutDb, cfg_.agcMaxGainDb);
    } else {
        agcDb_ = 0;
    }
    agcTargetDb_ = agcDb_;
    if (limChanged) {
        std::fill(delay_.begin(), delay_.end(), 0.f);
        minQ_.clear();
        std::fill(avgBuf_.begin(), avgBuf_.end(), 1.f);
        avgSum_ = look_;
        holdGain_ = 1;
    }
}

void Processor::reset()
{
    for (int ch = 0; ch < 2; ++ch) {
        kShelf_[ch].z1 = kShelf_[ch].z2 = 0;
        kHigh_[ch].z1 = kHigh_[ch].z2 = 0;
    }
    blockSum_ = 0;
    blockFrames_ = 0;
    blocks_.clear();
    shortTerm_ = -70;
    agcDb_ = agcTargetDb_ = 0;
    agcGain_ = 1;
    compEnvDb_ = -120;
    delay_.assign(size_t(look_ + kTpDelay) * 2, 0.f);
    delayPos_ = 0;
    minQ_.clear();
    idx_ = 0;
    avgBuf_.assign(size_t(look_), 1.f);
    avgSum_ = look_;
    avgPos_ = 0;
    holdGain_ = 1;
    for (auto &h : hist_)
        for (float &v : h) v = 0;
    m_ = ProcMeters();
}

// A valódi csúcs becslése: 4× túlmintavételezés az ITU-R BS.1770 (4. melléklet) 48 tapos szűrőjével, fázisonként
// 12 együtthatóval. Az eredmény kb. 6 mintával késik; a limiter késleltetése ezt is fedezi (kTpDelay).
static const float kTpCoef[4][12] = {
    {0.0017089843750f, 0.0109863281250f, -0.0196533203125f, 0.0332031250000f, -0.0594482421875f, 0.1373291015625f,
     0.9721679687500f, -0.1022949218750f, 0.0476074218750f, -0.0266113281250f, 0.0148925781250f, -0.0083007812500f},
    {-0.0291748046875f, 0.0292968750000f, -0.0517578125000f, 0.0891113281250f, -0.1665039062500f, 0.4650878906250f,
     0.7797851562500f, -0.2003173828125f, 0.1015625000000f, -0.0582275390625f, 0.0330810546875f, -0.0189208984375f},
    {-0.0189208984375f, 0.0330810546875f, -0.0582275390625f, 0.1015625000000f, -0.2003173828125f, 0.7797851562500f,
     0.4650878906250f, -0.1665039062500f, 0.0891113281250f, -0.0517578125000f, 0.0292968750000f, -0.0291748046875f},
    {-0.0083007812500f, 0.0148925781250f, -0.0266113281250f, 0.0476074218750f, -0.1022949218750f, 0.9721679687500f,
     0.1373291015625f, -0.0594482421875f, 0.0332031250000f, -0.0196533203125f, 0.0109863281250f, 0.0017089843750f}};

float Processor::truePeak(int ch, float x)
{
    float *h = hist_[ch];
    for (int k = 0; k < 11; ++k) h[k] = h[k + 1];
    h[11] = x;
    float peak = 0;
    for (const auto &c : kTpCoef) {
        float v = 0;
        for (int k = 0; k < 12; ++k) v += c[k] * h[11 - k];
        peak = std::max(peak, std::fabs(v));
    }
    return peak;
}

void Processor::loudnessBlock()
{
    blocks_.push_back(blockSum_ / std::max(1, blockFrames_));
    while (int(blocks_.size()) > kBlocks) blocks_.pop_front();
    double sum = 0;
    for (double b : blocks_) sum += b;
    const double ms = sum / std::max<size_t>(1, blocks_.size());
    shortTerm_ = float(ms > 1e-12 ? -0.691 + 10.0 * std::log10(ms) : -70.0);
    m_.shortTermLufs = shortTerm_;
    blockSum_ = 0;
    blockFrames_ = 0;
    // AGC: csak ha szól valami (a kapu fölött), és csak a teljes 3 mp-es ablak után.
    if (cfg_.agc && shortTerm_ > cfg_.gateLufs && int(blocks_.size()) >= kBlocks / 3) {
        const double err = std::clamp(double(cfg_.targetLufs - shortTerm_), -6.0, 6.0);
        agcTargetDb_ += err * (0.1 / std::max(0.5, cfg_.agcSpeedSec));
        agcTargetDb_ = std::clamp(agcTargetDb_, -cfg_.agcMaxCutDb, cfg_.agcMaxGainDb);
    }
}

void Processor::process(float *buf, int frames)
{
    const double atk = cfg_.compAttackMs > 0 ? 1.0 - std::exp(-1.0 / (cfg_.compAttackMs * 0.001 * kRate)) : 1.0;
    const double rel = 1.0 - std::exp(-1.0 / (std::max(1.0, cfg_.compReleaseMs) * 0.001 * kRate));
    const double limRel = 1.0 - std::exp(-1.0 / (std::max(1.0, cfg_.limiterReleaseMs) * 0.001 * kRate));
    const float ceil = float(dbToLin(cfg_.ceilingDbTp));
    const float detCeil = float(dbToLin(cfg_.ceilingDbTp - 0.5));
    const double makeup = dbToLin(cfg_.compMakeupDb);
    const double smooth = 1.0 - std::exp(-1.0 / (0.05 * kRate));     // az AGC erősítésének simítása (50 ms)
    float compGrMax = m_.compGrDb, limGrMax = m_.limGrDb, tpMax = 0;

    for (int i = 0; i < frames; ++i) {
        double l = buf[i * 2], r = buf[i * 2 + 1];

        // 1. AGC
        if (cfg_.agc) {
            agcDb_ += (agcTargetDb_ - agcDb_) * smooth;
            const double g = dbToLin(agcDb_);
            l *= g;
            r *= g;
        }

        // 2. Kompresszor (sztereó csatolt, lágy térd)
        if (cfg_.comp) {
            const double lvl = linToDb(std::max(std::fabs(l), std::fabs(r)));
            compEnvDb_ += (lvl - compEnvDb_) * (lvl > compEnvDb_ ? atk : rel);
            const double over = compEnvDb_ - cfg_.compThresholdDb, knee = std::max(0.0, cfg_.compKneeDb);
            const double slope = 1.0 - 1.0 / std::max(1.0, cfg_.compRatio);
            double gr = 0;
            if (knee > 0 && std::fabs(over) <= knee / 2) gr = slope * (over + knee / 2) * (over + knee / 2) / (2 * knee);
            else if (over > 0) gr = slope * over;
            const double g = dbToLin(-gr) * makeup;
            l *= g;
            r *= g;
            compGrMax = std::max(compGrMax, float(gr));
        }

        // Hangosság mérése (a limiter előtt): K-súlyozott négyzetösszeg
        {
            const double kl = kHigh_[0].run(kShelf_[0].run(l)), kr = kHigh_[1].run(kShelf_[1].run(r));
            blockSum_ += kl * kl + kr * kr;
            if (++blockFrames_ >= kBlock) loudnessBlock();
        }

        // 3. Limiter (előretekintő, true peak)
        if (cfg_.limiter) {
            const float tp = std::max(truePeak(0, float(l)), truePeak(1, float(r)));
            // A 4× szűrő a legmagasabb hangoknál kicsit alábecsül: 0,5 dB tartalékkal (a kimenet így a plafonon marad).
            const float req = tp > detCeil ? detCeil / tp : 1.f;
            // csúszó minimum az utolsó look+2 mintára
            while (!minQ_.empty() && minQ_.back().second >= req) minQ_.pop_back();
            minQ_.emplace_back(idx_, req);
            while (minQ_.front().first <= idx_ - (look_ + kTpDelay + 1)) minQ_.pop_front();
            const float hold = minQ_.front().second;
            holdGain_ = hold < holdGain_ ? hold : float(holdGain_ + (hold - holdGain_) * limRel);
            // mozgóátlag a look hosszán: a csillapítás simán ér le a csúcsig
            avgSum_ += holdGain_ - avgBuf_[size_t(avgPos_)];
            avgBuf_[size_t(avgPos_)] = holdGain_;
            avgPos_ = (avgPos_ + 1) % look_;
            const float g = float(std::min(1.0, avgSum_ / look_));
            // késleltetett minta ki, az új be
            const float dl = delay_[size_t(delayPos_) * 2], dr = delay_[size_t(delayPos_) * 2 + 1];
            delay_[size_t(delayPos_) * 2] = float(l);
            delay_[size_t(delayPos_) * 2 + 1] = float(r);
            delayPos_ = (delayPos_ + 1) % (look_ + kTpDelay);
            ++idx_;
            l = std::clamp(dl * g, -ceil, ceil);              // végső biztosíték
            r = std::clamp(dr * g, -ceil, ceil);
            limGrMax = std::max(limGrMax, float(-linToDb(g)));
            tpMax = std::max(tpMax, float(std::max(std::fabs(l), std::fabs(r))));
        } else {
            l = std::clamp(l, -1.0, 1.0);
            r = std::clamp(r, -1.0, 1.0);
            tpMax = std::max(tpMax, float(std::max(std::fabs(l), std::fabs(r))));
        }
        buf[i * 2] = float(l);
        buf[i * 2 + 1] = float(r);
    }
    m_.compGrDb = compGrMax;
    m_.limGrDb = limGrMax;
    m_.agcGainDb = float(agcDb_);
    m_.truePeakDb = std::max(m_.truePeakDb, float(linToDb(tpMax)));
}

ProcMeters Processor::takeMeters()
{
    ProcMeters out = m_;
    m_.compGrDb = 0;
    m_.limGrDb = 0;
    m_.truePeakDb = -70;
    return out;
}

} // namespace stream
