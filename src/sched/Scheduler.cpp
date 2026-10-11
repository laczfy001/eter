#include "Scheduler.h"

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

    void note(const Item &it, qint64 at)
    {
        itemLast[it.id] = std::max(itemLast.value(it.id, 0), at);
        const QString a = it.artist.trimmed().toLower();
        if (!a.isEmpty()) artistLast[a] = std::max(artistLast.value(a, 0), at);
    }

    qint64 lastOf(const Item &it) const { return itemLast.value(it.id, 0); }

    bool allowed(const Item &it, qint64 at) const
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

    // Elem választása a kategóriából a tervezett `at` időpontra. bestFitMs > 0: a hátralévő időhöz
    // legjobban illő hosszúságút keresi (óra végi kitöltéshez). nullptr, ha a kategória üres.
    const Item *pick(int categoryId, qint64 at, qint64 bestFitMs = 0)
    {
        const QVector<int> &pool = byCategory[categoryId];
        if (pool.isEmpty()) return nullptr;
        QVector<int> ok;
        for (int i : pool)
            if (allowed(all[i], at)) ok << i;
        const bool relaxed = ok.isEmpty();
        if (relaxed) {
            ok = pool;                           // nincs szabályos jelölt: a legrégebben szólt jön
            if (result) ++result->relaxed;
        }
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
    for (int i = 0; i < picker.all.size(); ++i) {
        const Item &it = picker.all[i];
        picker.byCategory[it.categoryId] << i;
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
        qint64 t = 0;                            // ms az óra elejétől
        int fillCategory = 0;                    // a legutóbbi kategória: ebből pótol a fix pont előtt
        auto add = [&](const Item &it, int fixed) {
            // A már elmúlt időre (a folyó óra eleje) nem kerül be elem, csak az idő halad.
            if (base + (t + it.airMs()) / 1000 <= nowSecs) {
                t += it.airMs();
                return;
            }
            db_->addEntry(playlist, it.id, "scheduler", fixed);
            picker.note(it, base + t / 1000);
            t += it.airMs();
            ++result.entries;
        };
        auto fillUntil = [&](int category, qint64 untilMs) {
            // Addig tesz be elemeket, amíg túl nem ér a határon (a fix időpontnál a szóló elem kiúszik,
            // így nem marad csend); az utolsó elem hossza a hátralévő időhöz igazodik.
            for (int guard = 0; guard < 5000 && t < untilMs; ++guard) {
                const qint64 left = untilMs - t;
                const Item *it = picker.pick(category, base + t / 1000, left < 480000 ? left : 0);
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
                if (fillCategory) fillUntil(fillCategory, qint64(fixed) * 1000);
                t = std::max(t, qint64(fixed) * 1000);
            }
            if (slot.type == "item") {
                const Item it = db_->item(slot.itemId);
                if (it.id) add(it, fixed);
                else ++result.empty;
            } else if (slot.type == "fill") {
                const qint64 before = t;
                fillUntil(slot.categoryId, nextFixed);
                if (t == before && picker.byCategory[slot.categoryId].isEmpty()) ++result.empty;
                fillCategory = slot.categoryId;
            } else {
                for (int n = 0; n < std::max(1, slot.count); ++n) {
                    const Item *it = picker.pick(slot.categoryId, base + t / 1000);
                    if (!it) { ++result.empty; break; }
                    add(*it, n == 0 ? fixed : -1);
                }
                if (slot.kind == "music") fillCategory = slot.categoryId;
            }
        }
        db_->markGenerated(playlist, clockId);
        ++result.hours;
    }
    db_->endBatch();
    return result;
}
