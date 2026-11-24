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
    // Minden elem (automatikus mix módban): a nem zenei elemek (jingle, reklám, hírek, voice track) átmeneteinél is
    // a program dönt – a csend levágása az elején és a végén, és szoros egymásután (átúsztatás nélkül).
    bool allItems = true;
    int segueGapMs = 0;           // ekkora szünet két elem között, ha valamelyik nem zene (a hallható vég után)
    bool savedWins = true;        // a Mix editorban mentett átmenet (és az elem saját mix pontja) érvényes marad
    // Rejtett szám: ha a vége előtt legalább ennyi csend van, és utána még jön valami, a szám a csendnél véget ér.
    bool hiddenTrack = true;
    int hiddenSilenceMs = 4000;
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
    bool segue = false;           // nem zenei elem: szoros egymásután a hallható vég után, átfedés nélkül
    int64_t hiddenAtMs = -1;      // rejtett szám: itt kezdődik a csend, eddig szól a szám
};

int tempoJumpPercent(double fromBpm, double toBpm);

// Rejtett szám: a szám második felében legalább minSilenceMs csend, utána újra hang. A csend kezdete (ms), vagy −1.
int64_t hiddenTrackCut(const MixProfile &p, int minSilenceMs);
// A profil levágva az adott pontnál (mintha ott érne véget a fájl).
MixProfile trimProfile(const MixProfile &p, int64_t ms);
// A szám elején lévő csend hossza (ms); érvénytelen profilnál 0.
int64_t leadSilenceMs(const MixProfile &p);

// Automatikus mix bármely két elem között: két zene között az okos mix (planSmartMix); ha valamelyik nem zene, és
// s.allItems: zenéből nem zenébe az okos mix vége (a bejövő erős kezdésnek számít), nem zenéből szoros egymásután a
// hallható vég után (s.segueGapMs szünettel). Mindkét esetben kimarad a bejövő elem eleji csendje, és (ha
// s.hiddenTrack) a kimenő szám rejtett része. Érvénytelen tervnél a hívó a régi szabály szerint dönt.
SmartMixPlan planAutoMix(const MixProfile &out, const MixProfile &in, bool outMusic, bool inMusic, double outBpm,
                         double inBpm, int outKey, int inKey, const SmartMixSettings &s);   // −1, ha valamelyik ismeretlen; a fél/dupla tempó egyezik

SmartMixPlan planSmartMix(const MixProfile &out, const MixProfile &in, double outBpm, double inBpm, int outKey,
                          int inKey, const SmartMixSettings &s);

} // namespace eter
