#pragma once
#include <QString>

// Összeomláskor (hibás memóriakezelés, érvénytelen hivatkozás stb.) a hívási lánc a terminálra és egy naplófájlba
// kerül, hogy a hiba visszakereshető legyen. Linuxon a crash.log a program adatmappájában, Windowson ugyanott.
namespace crash {
void install(const QString &logPath);
QString logPath();
// Mit csinál éppen ez a szál (pl. melyik fájlt dekódolja): összeomláskor a jelentésbe kerül. Gyors, zár nélküli.
void setActivity(const char *what, const char *detail);
}
