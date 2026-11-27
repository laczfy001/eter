#pragma once
#include <QString>

// Összeomláskor (hibás memóriakezelés, érvénytelen hivatkozás stb.) a hívási lánc a terminálra és egy naplófájlba
// kerül, hogy a hiba visszakereshető legyen. Linuxon a crash.log a program adatmappájában, Windowson ugyanott.
namespace crash {
void install(const QString &logPath);
QString logPath();
// Mit csinál éppen ez a szál (pl. melyik fájlt dekódolja): összeomláskor a jelentésbe kerül. Gyors, zár nélküli.
void setActivity(const char *what, const char *detail);
// A szál éppen ezt a hangfájlt dekódolja (a dekóder minden olvasáskor hívja; ugyanarra a szövegre olcsó). Ha a szál
// összeomlik, az útvonal a napló mellé, a „crash-file.txt”-be is kerül: a következő indításkor a program kihagyja.
void setFile(const char *path);
void clearFile(const char *path);   // a dekóder bezárásakor (ha ez a szál ezt a fájlt jelölte)
QString suspectFilePath();          // a crash-file.txt helye
}
