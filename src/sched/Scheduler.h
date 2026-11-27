// Ütemező: az órasémákból és a heti rácsból óránkénti playlisteket készít.
#pragma once
#include "db/Database.h"

#include <QSet>

#include <algorithm>

struct GenerateResult {
    int hours = 0;          // ennyi órához készült playlist
    int entries = 0;        // ennyi elem került be
    int relaxed = 0;        // ennyi helyen kellett lazítani valamelyik szabályon
    // Melyik szabályon (a legszigorúbb lazítás szerint): 1 a zenei folyam (lassú számok, energiaugrás, tempó,
    // hangnem, ének), 2 a hely energiasávja, 3 az ismétlés (előadó, szám), 4 a napszak.
    int relaxedBy[5] = {0, 0, 0, 0, 0};
    int empty = 0;          // ennyi helyre nem jutott elem (üres kategória)
    int noClock = 0;        // ennyi órához nincs séma a heti rácsban
    int alreadyDone = 0;    // ennyi óra már elkészült (marad)
    int refilled = 0;       // ennyi már generált, de azóta kiürített jövőbeli óra készült el újra
    int external = 0;       // ennyi óra készült külső playlistből (figyelt mappa)
    int fallback = 0;       // ennyi „Külső” óra készült a tartalék sémából (nem volt fájl)
    void noteRelaxed(int level)
    {
        ++relaxed;
        ++relaxedBy[std::clamp(level, 0, 4)];
    }
};

// A generált sorrend ellenőrzése: hány egymást követő zene közti átmenet illik a tempó- és a hangnem-szabályhoz.
struct FlowStats {
    int transitions = 0;
    int tempoKnown = 0, tempoOk = 0;   // ahol mindkét tempó ismert / és a szabályon belül van
    int keyKnown = 0, keyOk = 0;       // ahol mindkét hangnem ismert / és illik (Camelot)
    int vocalKnown = 0, vocalOk = 0;   // számító (férfi, női, együttes, vegyes) énekű szám / és nem lépi túl a sorozatot
};

class Scheduler {
public:
    explicit Scheduler(Database *db) : db_(db) {}

    // `from` órájától `hours` órányit generál. replace: a már meglévő ütemezett sorokat újra elkészíti.
    GenerateResult generate(const QDateTime &from, int hours, bool replace);
    // A megadott órákat generálja (a már elmúltakat kihagyja). replace: ezeknek az óráknak a meglévő ütemezett sorait
    // újra elkészíti; különben a már elkészült órák maradnak.
    GenerateResult generateHours(QVector<QDateTime> hours, const QDateTime &now, bool replace);

    // Ezek az elemek nem kerülnek be (például mert a fájljuk nem érhető el).
    void setUnavailable(const QSet<int> &itemIds) { unavailable_ = itemIds; }
    // Külső playlistek (figyelt mappa): a „Külső playlist” sémájú és a séma nélküli órák ebből készülnek.
    void setExternal(class ExternalPlaylists *ext) { ext_ = ext; }

    // Napszakok: 0 éjjel, 1 reggel, 2 napközben, 3 délután, 4 este. A kezdő órák az adatbázis beállításában
    // (daypart_starts, alapból „0,6,10,15,19”).
    static constexpr int kDayparts = 5;
    static QVector<int> daypartStarts(Database *db);
    static int daypartOf(int hour, const QVector<int> &starts);
    static QString daypartName(int part);
    static QString energyName(int energy);             // 0: „nincs megadva”, 1 … 5

    // Két egymást követő szám tempóeltérése százalékban (a fél- és dupla tempó egyezik); −1, ha valamelyik ismeretlen.
    static int tempoJumpPct(double fromBpm, double toBpm);
    static FlowStats flowStats(const QVector<Entry> &entries, int bpmPct, int keyRule, int vocalMax = 0);

    static int keyOf(const QDateTime &t) { return t.date().toString("yyyyMMdd").toInt() * 100 + t.time().hour(); }

private:
    Database *db_;
    class ExternalPlaylists *ext_ = nullptr;
    QSet<int> unavailable_;
};
