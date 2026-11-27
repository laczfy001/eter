// A stream hangfeldolgozása a kódolás előtt (enkóderenként külön): AGC → kompresszor → true-peak limiter.
//   - AGC: lassan a célhangosságra (−16 LUFS) húzza a jelet; a hangosságot a kompresszor utáni jelen méri (ITU-R
//     BS.1770 K-súlyozás, 3 mp-es rövid távú hangosság), így a kimenet a célon marad. Csendben (a kapu alatt) az
//     erősítés nem változik, hogy a zajt ne húzza fel.
//   - Kompresszor: sztereó csatolt, lágy térdű, a felfutás és az elengedés dB-ben.
//   - Limiter: előretekintő (5 ms), a valódi csúcsot (4× túlmintavételezés) a plafon (−1 dBTP) alatt tartja; a lossy
//     kódolás okozta túllövéshez marad tartalék.
// Mindez 48 kHz-en, sztereóban, a hangszál terhelése nélkül (az enkóder saját szálán fut).
#pragma once

#include <deque>
#include <vector>

namespace stream {

struct ProcConfig {
    enum Preset { Off = 0, Light = 1, Medium = 2, Strong = 3, Custom = 4 };
    int preset = Light;
    bool agc = true;
    double targetLufs = -16;
    double agcMaxGainDb = 9, agcMaxCutDb = 15;
    double agcSpeedSec = 8;          // az AGC időállandója másodpercben (minél nagyobb, annál lassabban követ)
    double gateLufs = -45;           // e alatt az AGC nem mozdul (csend, szünet)
    bool comp = false;
    double compThresholdDb = -20, compRatio = 2.5, compAttackMs = 10, compReleaseMs = 250, compMakeupDb = 0, compKneeDb = 6;
    bool limiter = true;
    double ceilingDbTp = -1;
    double limiterReleaseMs = 120;

    static ProcConfig fromPreset(int p);  // a kész beállítások (Off, Light, Medium, Strong)
    bool active() const { return agc || comp || limiter; }
};

struct ProcMeters {
    float shortTermLufs = -70;       // a kimenet rövid távú hangossága (3 mp)
    float agcGainDb = 0;
    float compGrDb = 0;              // a legnagyobb csillapítás a lekérdezés óta (pozitív szám)
    float limGrDb = 0;
    float truePeakDb = -70;          // a kimenet legnagyobb valódi csúcsa a lekérdezés óta
};

class Processor {
public:
    Processor();
    void configure(const ProcConfig &c);
    void reset();
    void process(float *stereo, int frames);      // helyben, 48 kHz
    ProcMeters takeMeters();                       // a csúcsértékeket nullázza

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1 = 0, z2 = 0;
        double run(double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    void loudnessBlock();
    float truePeak(int ch, float x);

    ProcConfig cfg_;
    // hangosság (K-súlyozás csatornánként két szűrővel; 100 ms-os blokkok, 30 blokk = 3 mp)
    Biquad kShelf_[2], kHigh_[2];
    double blockSum_ = 0;
    int blockFrames_ = 0;
    std::deque<double> blocks_;
    float shortTerm_ = -70;
    // AGC
    double agcDb_ = 0, agcTargetDb_ = 0;
    double agcGain_ = 1;
    // kompresszor
    double compEnvDb_ = -120;
    // limiter
    int look_ = 240;
    std::vector<float> delay_;          // sztereó késleltető (előretekintés)
    int delayPos_ = 0;
    std::deque<std::pair<long long, float>> minQ_;   // csúszó minimum: (index, szükséges erősítés)
    long long idx_ = 0;
    std::vector<float> avgBuf_;
    double avgSum_ = 0;
    int avgPos_ = 0;
    float holdGain_ = 1;
    float hist_[2][12] = {};            // a túlmintavételezés előzményei
    // mérők
    ProcMeters m_;
};

} // namespace stream
