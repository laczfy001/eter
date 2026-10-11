// Ütemező: az órasémákból és a heti rácsból óránkénti playlisteket készít.
#pragma once
#include "db/Database.h"

struct GenerateResult {
    int hours = 0;          // ennyi órához készült playlist
    int entries = 0;        // ennyi elem került be
    int relaxed = 0;        // ennyiszer nem volt az ismétlési szabálynak megfelelő elem
    int empty = 0;          // ennyi helyre nem jutott elem (üres kategória)
};

class Scheduler {
public:
    explicit Scheduler(Database *db) : db_(db) {}

    // `from` órájától `hours` órányit generál. replace: a már meglévő ütemezett sorokat újra elkészíti.
    GenerateResult generate(const QDateTime &from, int hours, bool replace);

    static int keyOf(const QDateTime &t) { return t.date().toString("yyyyMMdd").toInt() * 100 + t.time().hour(); }

private:
    Database *db_;
};
