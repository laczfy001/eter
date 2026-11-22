// Hangelemzés az ütemezőhöz: tempó (BPM) és hangnem becslése a fájlból.
#pragma once
#include <atomic>
#include <string>

namespace eter {

// A szám tempója ütésszám/percben (60–200), a közepe körüli legfeljebb egy perc alapján. 0, ha nem állapítható meg
// (például beszéd, vagy nincs határozott ütem). `cancel` igazra állítva megszakítható.
double estimateBpm(const std::string &path, const std::atomic<bool> &cancel);

// A szám hangneme (core/MusicKey.h kódolás: 1–24; 0, ha nem állapítható meg, például beszéd vagy zaj).
// Kromagram (a 12 hangosztály energiája) a szám közepe körüli legfeljebb két percből, összevetve a dúr és moll
// hangnemprofilokkal. confidence: a legjobb és a második legjobb egyezés különbsége (0 … 1).
int estimateKey(const std::string &path, const std::atomic<bool> &cancel, double *confidence = nullptr);

} // namespace eter
