// Ütemező: az órasémákból és a heti rácsból óránkénti playlisteket készít.
#pragma once
#include "db/Database.h"

#include <QSet>

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

    // Ezek az elemek nem kerülnek be (például mert a fájljuk nem érhető el).
    void setUnavailable(const QSet<int> &itemIds) { unavailable_ = itemIds; }

    // Napszakok: 0 éjjel, 1 reggel, 2 napközben, 3 délután, 4 este. A kezdő órák az adatbázis beállításában
    // (daypart_starts, alapból „0,6,10,15,19”).
    static constexpr int kDayparts = 5;
    static QVector<int> daypartStarts(Database *db);
    static int daypartOf(int hour, const QVector<int> &starts);
    static QString daypartName(int part);
    static QString energyName(int energy);             // 0: „nincs megadva”, 1 … 5

    static int keyOf(const QDateTime &t) { return t.date().toString("yyyyMMdd").toInt() * 100 + t.time().hour(); }

private:
    Database *db_;
    QSet<int> unavailable_;
};
