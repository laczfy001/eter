// Hibanapló: minden hiba és figyelmeztetés szöveges fájlba, a program mappájában (logs/eter-ÉÉÉÉ-HH-NN.txt).
//   - Windows: az eter.exe mellett (logs\); ha oda nem írható (pl. Program Files), a felhasználó AppData mappájába;
//   - Linux AppImage: az AppImage fájl mellett (eter-logs/); fejlesztői fordításnál a program mellett (logs/);
//   - ETER_LOG_DIR: tetszőleges mappa (fejlesztéshez).
// Kiterjed: váratlan kilépés (a következő indításkor derül ki, az életjelből), összeomlás (a hívási lánccal), elkapatlan
// kivétel, a Qt figyelmeztetései (felület, hálózat, SQL-illesztő), a figyelmeztető/hiba párbeszédablakok, az adatbázis,
// a hang (kimenet, betöltés, stream), a weboldal, az SQL szerver, az online adatok, a MIDI és a távoli kapcsolat hibái.
// Ugyanaz az üzenet egy percen belül csak egyszer kerül be (az ismétlések számával). A 30 napnál régebbi naplók törlődnek.
#pragma once

#include <QString>

namespace elog {

enum Level { Info, Warning, Error };

// Indításkor, a QApplication után: a napló megnyitása, az előző futás ellenőrzése, a Qt üzenetkezelő beállítása.
// role: "playout", "editor", "backup", … (a váratlan kilépés figyelése szerepenként külön).
void init(const QString &role);
// Rendes kilépés (az életjel törlése, „Kilépés” sor).
void shutdown();

void write(Level level, const QString &area, const QString &text);
inline void error(const QString &area, const QString &text) { write(Error, area, text); }
inline void warning(const QString &area, const QString &text) { write(Warning, area, text); }
inline void info(const QString &area, const QString &text) { write(Info, area, text); }
// Változó szövegű, de ugyanarról szóló üzenet (pl. percenként frissülő becslés): a kulcs szerint legfeljebb
// windowMs-onként egyszer kerül be.
void writeKeyed(Level level, const QString &area, const QString &text, const QString &key, int windowMs);

QString directory();        // a naplók mappája
QString currentFile();      // a mai napló
bool active();

} // namespace elog
