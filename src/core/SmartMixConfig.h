#pragma once
// Az okos automatikus mix beállításai az adatbázisban (így a távoli szerkesztő is ugyanazt látja).
#include "audio/SmartMix.h"
#include "db/Database.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVector>
#include <algorithm>

// ---- Mix sablonok -----------------------------------------------------------------------------------------------------
// Az automatikus mix beállításainak elmentett változatai (Beállítások → Automatikus mix → Sablon). Egy sablon a
// hangtár kategóriáihoz rendelhető (Hangtár → Kategóriák…): az ilyen kategóriájú elem *kimenő* átmeneténél a sablon
// értékei érvényesek (a kezdőpont küszöbénél a bejövő elem sablonja), a többinél az alapbeállítás. A sablon nem
// tartalmazott kulcsa az alapbeállításból jön. Tárolás: a beállítások között ("mix_templates" JSON, kategóriánként
// "mix_template_cat_<id>").

struct MixTemplate {
    int id = 0;
    QString name;
    QJsonObject values;         // beállításkulcs → szöveges érték (ugyanazok a kulcsok, mint az alapbeállításé)
};

// A sablonba kerülő kulcsok és alapértékeik (a csendvédelem minden elemre közös, nincs benne).
inline const QVector<QPair<const char *, const char *>> &mixTemplateKeys()
{
    static const QVector<QPair<const char *, const char *>> k{
        {"smart_mix", "1"},           {"smart_end_depth_db", "24"},   {"smart_fade_depth_db", "10"},
        {"smart_intro_max_ms", "6000"}, {"smart_tight_ms", "150"},    {"smart_max_overlap_ms", "8000"},
        {"smart_flow", "1"},          {"smart_tempo_tol_pct", "6"},   {"smart_clash_overlap_ms", "2000"},
        {"smart_all_items", "1"},     {"smart_saved_wins", "1"},      {"smart_hidden_track", "1"},
        {"smart_segue_gap_ms", "0"},  {"smart_hidden_silence_ms", "4000"}, {"auto_mix_threshold_db", "-24"},
        {"auto_cue_threshold_db", "-45"}, {"default_fade_out_ms", "2500"}};
    return k;
}

inline QVector<MixTemplate> loadMixTemplates(Database *db)
{
    QVector<MixTemplate> out;
    for (const QJsonValue &v : QJsonDocument::fromJson(db->setting("mix_templates").toUtf8()).array()) {
        const QJsonObject o = v.toObject();
        MixTemplate t;
        t.id = o.value(QStringLiteral("id")).toInt();
        t.name = o.value(QStringLiteral("name")).toString();
        t.values = o.value(QStringLiteral("values")).toObject();
        if (t.id > 0) out.push_back(t);
    }
    return out;
}

inline void saveMixTemplates(Database *db, const QVector<MixTemplate> &list)
{
    QJsonArray a;
    for (const MixTemplate &t : list) a.append(QJsonObject{{"id", t.id}, {"name", t.name}, {"values", t.values}});
    db->setSetting("mix_templates", QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact)));
}

inline int categoryMixTemplate(Database *db, int categoryId)
{
    return categoryId > 0 ? db->setting(QStringLiteral("mix_template_cat_%1").arg(categoryId), "0").toInt() : 0;
}

inline void setCategoryMixTemplate(Database *db, int categoryId, int templateId)
{
    db->setSetting(QStringLiteral("mix_template_cat_%1").arg(categoryId), QString::number(std::max(0, templateId)));
}

// A beállítások értéke: a sablonból (ha van benne), különben az alapbeállításból.
struct MixValues {
    Database *db;
    const QJsonObject *tpl = nullptr;
    QString get(const char *key, const char *def) const
    {
        if (tpl && tpl->contains(QLatin1String(key))) return tpl->value(QLatin1String(key)).toString();
        return db->setting(QLatin1String(key), QLatin1String(def));
    }
};

inline eter::SmartMixSettings loadSmartMix(Database *db, const QJsonObject *tpl = nullptr)
{
    const MixValues v{db, tpl};
    eter::SmartMixSettings s;
    s.enabled = v.get("smart_mix", "1") != QLatin1String("0");
    s.endDepthDb = std::clamp(v.get("smart_end_depth_db", "24").toDouble(), 6.0, 50.0);
    s.fadeDepthDb = std::clamp(v.get("smart_fade_depth_db", "10").toDouble(), 2.0, 30.0);
    s.maxOverlapMs = std::clamp(v.get("smart_max_overlap_ms", "8000").toInt(), 0, 30000);
    s.introMaxMs = std::clamp(v.get("smart_intro_max_ms", "6000").toInt(), 0, 30000);
    s.tightMs = std::clamp(v.get("smart_tight_ms", "150").toInt(), 0, 3000);
    s.flowAware = v.get("smart_flow", "1") != QLatin1String("0");
    s.tempoTolPct = std::clamp(v.get("smart_tempo_tol_pct", "6").toInt(), 0, 50);
    s.clashOverlapMs = std::clamp(v.get("smart_clash_overlap_ms", "2000").toInt(), 0, 30000);
    s.allItems = v.get("smart_all_items", "1") != QLatin1String("0");
    s.segueGapMs = std::clamp(v.get("smart_segue_gap_ms", "0").toInt(), 0, 3000);
    s.savedWins = v.get("smart_saved_wins", "1") != QLatin1String("0");
    s.hiddenTrack = v.get("smart_hidden_track", "1") != QLatin1String("0");
    s.hiddenSilenceMs = std::clamp(v.get("smart_hidden_silence_ms", "4000").toInt(), 1000, 30000);
    return s;
}

// A kategóriához rendelt sablon értékei (üres: nincs sablon, az alapbeállítás érvényes).
inline QJsonObject mixTemplateValuesFor(Database *db, int categoryId)
{
    const int id = categoryMixTemplate(db, categoryId);
    if (id <= 0) return {};
    for (const MixTemplate &t : loadMixTemplates(db))
        if (t.id == id) return t.values;
    return {};
}

// A kategória szerinti beállítások (a sablon, ha van; különben az alapbeállítás) – gyorsítótárral, mert az átmenet
// tervezése gyakran kéri. A beállítások változásakor clear().
struct MixSettingsCache {
    struct Entry {
        eter::SmartMixSettings smart;
        double mixDb = -24, cueDb = -45;
        int fadeMs = 2500;
        int templateId = 0;
    };
    QHash<int, Entry> byCategory;
    void clear() { byCategory.clear(); }
    const Entry &get(Database *db, int categoryId)
    {
        auto it = byCategory.find(categoryId);
        if (it != byCategory.end()) return *it;
        Entry e;
        const QJsonObject tpl = mixTemplateValuesFor(db, categoryId);
        const MixValues v{db, tpl.isEmpty() ? nullptr : &tpl};
        e.smart = loadSmartMix(db, v.tpl);
        e.mixDb = v.get("auto_mix_threshold_db", "-24").toDouble();
        e.cueDb = v.get("auto_cue_threshold_db", "-45").toDouble();
        e.fadeMs = std::clamp(v.get("default_fade_out_ms", "2500").toInt(), 100, 15000);
        e.templateId = tpl.isEmpty() ? 0 : categoryMixTemplate(db, categoryId);
        return *byCategory.insert(categoryId, e);
    }
};

// Csendvédelem (automata módban): ha a szóló elem ennyi ideig néma, indul a következő.
inline bool deadAirGuard(Database *db) { return db->setting("dead_air_guard", "1") != QLatin1String("0"); }
inline int deadAirMs(Database *db) { return std::clamp(db->setting("dead_air_ms", "3000").toInt(), 1000, 30000); }
