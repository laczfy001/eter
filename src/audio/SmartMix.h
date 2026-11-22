#pragma once
// Okos automatikus mix: a kimenő szám végének és a bejövő szám elejének elemzése (lecsengő vagy hirtelen vég,
// lágy vagy erős kezdés), és ebből a mix pont, a kimenő szám vége és kiúsztatása. A zenei folyam (tempó, hangnem)
// figyelésekor ütköző pároknál rövidebb az átfedés, hogy ne csússzon szét az ütem és ne szóljon össze két nem
// illő hangnem.
#include <cstdint>
#include <vector>

namespace eter {

// A szám hangerő-képe 100 ms-os lépésekben (a scanPeaks() `env` kimenetéből).
struct MixProfile {
    bool valid = false;
    double bodyDb = -99;          // a szám „törzsének” szintje (a simított szint 70. percentilise a 10–90 % között)
    std::vector<float> rawDb;     // 100 ms-onként a csúcsszint, dB
    std::vector<float> smoothDb;  // 1 mp-es ablakkal simítva (teljesítmény-átlag), dB
    int64_t durMs = 0;
};

MixProfile analyzeMixProfile(const std::vector<float> &env100ms, int64_t durMs);
// A Mix editor sűrűbb csúcs-sorából (tetszőleges felbontás) 100 ms-os lépésekre.
std::vector<float> envFromPeaks(const std::vector<float> &peaks, int64_t durMs);

struct SmartMixSettings {
    bool enabled = true;
    double endDepthDb = 24;       // a hallható vég: ennyivel a szám szintje alatt
    double fadeDepthDb = 10;      // lecsengő végnél ennyit halkul a kimenő szám, mire a következő indul
    int maxOverlapMs = 8000;      // a leghosszabb átfedés
    int introMaxMs = 6000;        // lágy intrónál legfeljebb ennyivel indul korábban a következő
    int tightMs = 150;            // hirtelen vég + erős kezdés: ennyi az átfedés (szoros illesztés)
    bool flowAware = true;        // tempó és hangnem figyelése
    int tempoTolPct = 6;          // ekkora tempóeltérés még egyezőnek számít (a fél/dupla tempó is)
    int clashOverlapMs = 2000;    // eltérő tempónál vagy nem illő hangnemnél legfeljebb ennyi az átfedés
};

struct SmartMixPlan {
    bool valid = false;
    int64_t mixOutMs = -1;        // a kimenő számban: itt indul a következő
    int64_t outEndMs = -1;        // a kimenő szám itt ér véget (eddig szól, a végén kiúszik)
    int fadeOutMs = 0;            // a kimenő szám kiúsztatása a vége előtt
    int64_t cueInMs = 0;          // a bejövő szám kezdőpontja (az eleji csend kimarad)
    int overlapMs = 0;
    // A döntés okai (a felületen kiírható):
    bool fadeEnding = false;      // lecsengő vég (különben hirtelen)
    int fadeLenMs = 0;
    bool softIntro = false;       // lágy, felfutó intró (különben erős kezdés)
    int introRampMs = 0;
    int tempoJumpPct = -1;        // −1: valamelyik tempó ismeretlen
    bool keyClash = false;
    bool flowLimited = false;     // a tempó vagy a hangnem miatt rövidült az átfedés
    bool flowMatch = false;       // egyező tempó és illő hangnem (mindkettő ismert)
};

int tempoJumpPercent(double fromBpm, double toBpm);   // −1, ha valamelyik ismeretlen; a fél/dupla tempó egyezik

SmartMixPlan planSmartMix(const MixProfile &out, const MixProfile &in, double outBpm, double inBpm, int outKey,
                          int inKey, const SmartMixSettings &s);

} // namespace eter
