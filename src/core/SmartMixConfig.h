#pragma once
// Az okos automatikus mix beállításai az adatbázisban (így a távoli szerkesztő is ugyanazt látja).
#include "audio/SmartMix.h"
#include "db/Database.h"

#include <algorithm>

inline eter::SmartMixSettings loadSmartMix(Database *db)
{
    eter::SmartMixSettings s;
    s.enabled = db->setting("smart_mix", "1") != QLatin1String("0");
    s.endDepthDb = std::clamp(db->setting("smart_end_depth_db", "24").toDouble(), 6.0, 50.0);
    s.fadeDepthDb = std::clamp(db->setting("smart_fade_depth_db", "10").toDouble(), 2.0, 30.0);
    s.maxOverlapMs = std::clamp(db->setting("smart_max_overlap_ms", "8000").toInt(), 0, 30000);
    s.introMaxMs = std::clamp(db->setting("smart_intro_max_ms", "6000").toInt(), 0, 30000);
    s.tightMs = std::clamp(db->setting("smart_tight_ms", "150").toInt(), 0, 3000);
    s.flowAware = db->setting("smart_flow", "1") != QLatin1String("0");
    s.tempoTolPct = std::clamp(db->setting("smart_tempo_tol_pct", "6").toInt(), 0, 50);
    s.clashOverlapMs = std::clamp(db->setting("smart_clash_overlap_ms", "2000").toInt(), 0, 30000);
    s.allItems = db->setting("smart_all_items", "1") != QLatin1String("0");
    s.segueGapMs = std::clamp(db->setting("smart_segue_gap_ms", "0").toInt(), 0, 3000);
    s.savedWins = db->setting("smart_saved_wins", "1") != QLatin1String("0");
    s.hiddenTrack = db->setting("smart_hidden_track", "1") != QLatin1String("0");
    s.hiddenSilenceMs = std::clamp(db->setting("smart_hidden_silence_ms", "4000").toInt(), 1000, 30000);
    return s;
}

// Csendvédelem (automata módban): ha a szóló elem ennyi ideig néma, indul a következő.
inline bool deadAirGuard(Database *db) { return db->setting("dead_air_guard", "1") != QLatin1String("0"); }
inline int deadAirMs(Database *db) { return std::clamp(db->setting("dead_air_ms", "3000").toInt(), 1000, 30000); }
