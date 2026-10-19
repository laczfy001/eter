// Hangelemzés az ütemezőhöz: tempó (BPM) becslése a fájlból.
#pragma once
#include <atomic>
#include <string>

namespace eter {

// A szám tempója ütésszám/percben (60–200), a közepe körüli legfeljebb egy perc alapján. 0, ha nem állapítható meg
// (például beszéd, vagy nincs határozott ütem). `cancel` igazra állítva megszakítható.
double estimateBpm(const std::string &path, const std::atomic<bool> &cancel);

} // namespace eter
