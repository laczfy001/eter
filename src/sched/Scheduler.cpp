#include "Scheduler.h"
#include "i18n.h"

#include <QRandomGenerator>
#include <algorithm>

namespace {

struct Picker {
    QVector<Item> all;
    QHash<int, QVector<int>> byCategory;        // kategória → indexek
    QHash<int, qint64> itemLast;                // elem → utolsó (tényleges vagy tervezett) kezdés
    QHash<QString, qint64> artistLast;
    qint64 artistGap = 0, titleGap = 0;
    GenerateResult *result = nullptr;
    // A zenei folyam szabályai (0 = nincs ilyen szabály)
    int slowMax = 0;                            // ennyi lassú (energia 1–2) szám jöhet egymás után
    int energyJump = 0;                         // két egymást követő szám energiája legfeljebb ennyivel tér el
    int bpmJumpPct = 0;                         // két egymást követő szám tempója legfeljebb ennyi százalékkal tér el
    int daypart = -1;                           // az épp tervezett óra napszaka
    // Az utoljára betett zenei elem, és hány lassú szám áll a végén egymás után
    const Item *prev = nullptr;
    int slowRun = 0;

    void note(const Item &it, qint64 at)
    {
        itemLast[it.id] = std::max(itemLast.value(it.id, 0), at);
        const QString a = it.artist.trimmed().toLower();
        if (!a.isEmpty()) artistLast[a] = std::max(artistLast.value(a, 0), at);
    }

    // A sorrendben következő zenei elem: a folyam szabályaihoz ehhez képest nézzük a következőt.
    void follow(const Item &it)
    {
        if (it.kind != QLatin1String("music")) return;
        prev = &it;
        slowRun = (it.energy > 0 && it.energy <= 2) ? slowRun + 1 : 0;
    }

    qint64 lastOf(const Item &it) const { return itemLast.value(it.id, 0); }

    bool separated(const Item &it, qint64 at) const
    {
        const qint64 li = itemLast.value(it.id, 0);
        if (li && std::llabs(at - li) < titleGap) return false;
        const QString a = it.artist.trimmed().toLower();
        if (!a.isEmpty()) {
            const qint64 la = artistLast.value(a, 0);
            if (la && std::llabs(at - la) < artistGap) return false;
        }
        return true;
    }

    bool flows(const Item &it) const
    {
        if (it.kind != QLatin1String("music")) return true;
        if (slowMax > 0 && it.energy > 0 && it.energy <= 2 && slowRun >= slowMax) return false;
        if (!prev) return true;
        if (energyJump > 0 && it.energy > 0 && prev->energy > 0 && std::abs(it.energy - prev->energy) > energyJump) return false;
        if (bpmJumpPct > 0 && it.bpm > 0 && prev->bpm > 0) {
            // A fél- és kétszeres tempó is illeszkedik (ugyanaz az ütem).
            double ratio = it.bpm / prev->bpm;
            if (ratio < 0.75) ratio *= 2;
            if (ratio > 1.5) ratio /= 2;
            if (std::abs(ratio - 1.0) * 100.0 > bpmJumpPct) return false;
        }
        return true;
    }

    static bool inBand(const Item &it, int lo, int hi)
    {
        // Ismeretlen energiájú elem bármelyik sávba belefér.
        if (it.energy <= 0) return true;
        return (lo <= 0 || it.energy >= lo) && (hi <= 0 || it.energy <= hi);
    }

    bool inDaypart(const Item &it) const { return it.dayparts == 0 || daypart < 0 || (it.dayparts & (1 << daypart)); }

    // Elem választása a kategóriából a tervezett `at` időpontra. bestFitMs > 0: a hátralévő időhöz
    // legjobban illő hosszúságút keresi (óra végi kitöltéshez). energyMin/Max: a hely energiasávja (0 = nincs).
    // Előbb minden szabály szerint; ha így nincs jelölt, sorban lazít: a folyam szabályai, az energiasáv, az ismétlés,
    // végül a napszak. nullptr, ha a kategória üres.
    const Item *pick(int categoryId, qint64 at, qint64 bestFitMs = 0, int energyMin = 0, int energyMax = 0)
    {
        const QVector<int> &pool = byCategory[categoryId];
        if (pool.isEmpty()) return nullptr;
        QVector<int> ok;
        bool relaxed = false;
        for (int level = 0; level < 5 && ok.isEmpty(); ++level) {
            for (int i : pool) {
                const Item &it = all[i];
                if (level < 4 && !inDaypart(it)) continue;
                if (level < 3 && !separated(it, at)) continue;
                if (level < 2 && !inBand(it, energyMin, energyMax)) continue;
                if (level < 1 && !flows(it)) continue;
                ok << i;
            }
            if (ok.isEmpty()) relaxed = true;
        }
        if (relaxed && result) ++result->relaxed;
        // Legrégebben használt elöl; az azonosak között véletlen sorrend.
        std::shuffle(ok.begin(), ok.end(), *QRandomGenerator::global());
        std::stable_sort(ok.begin(), ok.end(), [&](int a, int b) { return lastOf(all[a]) < lastOf(all[b]); });
        int take = relaxed ? 1 : std::max<int>(1, (ok.size() + 2) / 3);
        ok.resize(take);
        int chosen = ok[QRandomGenerator::global()->bounded(take)];
        if (bestFitMs > 0) {
            for (int i : ok)
                if (std::llabs(all[i].airMs() - bestFitMs) < std::llabs(all[chosen].airMs() - bestFitMs)) chosen = i;
        }
        return &all[chosen];
    }
};

} // namespace

GenerateResult Scheduler::generate(const QDateTime &from, int hours, bool replace)
{
    GenerateResult result;
    const QHash<int, int> grid = db_->weekGrid();
    if (grid.isEmpty()) return result;

    QDateTime start(from.date(), QTime(from.time().hour(), 0));
    if (replace) db_->deleteSchedulerEntriesFrom(keyOf(start));

    Picker picker;
    picker.result = &result;
    picker.all = db_->items();
    picker.artistGap = db_->setting("artist_separation_min", "90").toLongLong() * 60;
    picker.titleGap = db_->setting("title_separation_min", "240").toLongLong() * 60;
    picker.slowMax = db_->setting("rule_slow_max", "0").toInt();
    picker.energyJump = db_->setting("rule_energy_jump", "0").toInt();
    picker.bpmJumpPct = db_->setting("rule_bpm_jump_pct", "0").toInt();
    const QVector<int> starts = daypartStarts(db_);
    for (int i = 0; i < picker.all.size(); ++i) {
        const Item &it = picker.all[i];
        if (!unavailable_.contains(it.id)) picker.byCategory[it.categoryId] << i;
        if (it.lastPlayedAt > 0) picker.note(it, it.lastPlayedAt);
    }
    // A már sorban álló elemek is számítanak az ismétlési szabálynál.
    {
        QHash<int, qint64> offset;               // óra → eddigi hossz
        for (const Entry &e : db_->pendingEntries()) {
            const QDate d = QDate::fromString(QString::number(e.playlistKey / 100), "yyyyMMdd");
            const qint64 base = QDateTime(d, QTime(e.playlistKey % 100, 0)).toSecsSinceEpoch();
            picker.note(e.item, base + offset[e.playlistKey] / 1000);
            offset[e.playlistKey] += e.item.airMs();
        }
    }
    // A folyam szabályai a legutolsó, már sorban álló zenei elemtől folytatódnak.
    QHash<int, int> indexOf;
    for (int i = 0; i < picker.all.size(); ++i) indexOf.insert(picker.all[i].id, i);
    for (const Entry &e : db_->pendingEntries())
        if (indexOf.contains(e.item.id)) picker.follow(picker.all[indexOf.value(e.item.id)]);

    const qint64 nowSecs = from.toSecsSinceEpoch();
    db_->beginBatch();
    for (int h = 0; h < hours; ++h) {
        const QDateTime hour = start.addSecs(qint64(h) * 3600);
        const int clockId = grid.value((hour.date().dayOfWeek() - 1) * 100 + hour.time().hour(), 0);
        if (!clockId) continue;
        const int playlist = db_->playlistFor(hour.date(), hour.time().hour());
        if (db_->playlistGenerated(playlist)) continue;
        const QVector<ClockSlot> slots_ = db_->clockSlots(clockId);
        if (slots_.isEmpty()) continue;

        const qint64 base = hour.toSecsSinceEpoch();
        picker.daypart = daypartOf(hour.time().hour(), starts);
        qint64 t = 0;                            // ms az óra elejétől
        int fillCategory = 0;                    // a legutóbbi kategória: ebből pótol a fix pont előtt
        int fillMin = 0, fillMax = 0;            // és annak energiasávja
        auto add = [&](const Item &it, int fixed) {
            // A már elmúlt időre (a folyó óra eleje) nem kerül be elem, csak az idő halad.
            if (base + (t + it.airMs()) / 1000 <= nowSecs) {
                t += it.airMs();
                return;
            }
            db_->addEntry(playlist, it.id, "scheduler", fixed);
            picker.note(it, base + t / 1000);
            if (indexOf.contains(it.id)) picker.follow(picker.all[indexOf.value(it.id)]);
            t += it.airMs();
            ++result.entries;
        };
        auto fillUntil = [&](int category, qint64 untilMs, int eMin, int eMax) {
            // Addig tesz be elemeket, amíg túl nem ér a határon (a fix időpontnál a szóló elem kiúszik,
            // így nem marad csend); az utolsó elem hossza a hátralévő időhöz igazodik.
            for (int guard = 0; guard < 5000 && t < untilMs; ++guard) {
                const qint64 left = untilMs - t;
                const Item *it = picker.pick(category, base + t / 1000, left < 480000 ? left : 0, eMin, eMax);
                if (!it || it->durationMs <= 0) break;
                add(*it, -1);
            }
        };

        for (int s = 0; s < slots_.size(); ++s) {
            const ClockSlot &slot = slots_[s];
            qint64 nextFixed = 3600000;          // a következő fix pont (vagy az óra vége)
            for (int n = s + 1; n < slots_.size(); ++n)
                if (slots_[n].fixedOffsetS >= 0) { nextFixed = qint64(slots_[n].fixedOffsetS) * 1000; break; }

            int fixed = slot.fixedOffsetS;
            if (fixed >= 0) {
                // Ha a fix pontig még maradna üres idő, az előző kategóriából pótoljuk.
                if (fillCategory) fillUntil(fillCategory, qint64(fixed) * 1000, fillMin, fillMax);
                t = std::max(t, qint64(fixed) * 1000);
            }
            if (slot.type == "item") {
                const Item it = db_->item(slot.itemId);
                if (it.id) add(it, fixed);
                else ++result.empty;
            } else if (slot.type == "fill") {
                const qint64 before = t;
                fillUntil(slot.categoryId, nextFixed, slot.energyMin, slot.energyMax);
                if (t == before && picker.byCategory[slot.categoryId].isEmpty()) ++result.empty;
                fillCategory = slot.categoryId;
                fillMin = slot.energyMin;
                fillMax = slot.energyMax;
            } else {
                for (int n = 0; n < std::max(1, slot.count); ++n) {
                    const Item *it = picker.pick(slot.categoryId, base + t / 1000, 0, slot.energyMin, slot.energyMax);
                    if (!it) { ++result.empty; break; }
                    add(*it, n == 0 ? fixed : -1);
                }
                if (slot.kind == "music") {
                    fillCategory = slot.categoryId;
                    fillMin = slot.energyMin;
                    fillMax = slot.energyMax;
                }
            }
        }
        db_->markGenerated(playlist, clockId);
        ++result.hours;
    }
    db_->endBatch();
    return result;
}

QVector<int> Scheduler::daypartStarts(Database *db)
{
    QVector<int> out;
    for (const QString &p : db->setting("daypart_starts", "0,6,10,15,19").split(QLatin1Char(','))) out << p.trimmed().toInt();
    if (out.size() != kDayparts) out = {0, 6, 10, 15, 19};
    out[0] = 0;
    for (int i = 1; i < kDayparts; ++i) out[i] = std::clamp(out[i], out[i - 1], 24);
    return out;
}

int Scheduler::daypartOf(int hour, const QVector<int> &starts)
{
    int part = 0;
    for (int i = 0; i < starts.size(); ++i)
        if (hour >= starts[i]) part = i;
    return part;
}

QString Scheduler::daypartName(int part)
{
    switch (part) {
    case 0: return TR("Éjjel");
    case 1: return TR("Reggel");
    case 2: return TR("Napközben");
    case 3: return TR("Délután");
    default: return TR("Este");
    }
}

QString Scheduler::energyName(int energy)
{
    switch (energy) {
    case 1: return TR("1 – nagyon nyugodt");
    case 2: return TR("2 – nyugodt");
    case 3: return TR("3 – közepes");
    case 4: return TR("4 – lendületes");
    case 5: return TR("5 – nagyon pörgős");
    default: return TR("nincs megadva");
    }
}
