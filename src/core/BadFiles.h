// Összeomlást okozó hangfájlok kihagyása. Ha a program egy fájl dekódolása közben omlik össze (sérült vagy szokatlan
// fájl), az összeomlás-kezelő feljegyzi az útvonalát (crash::setFile); a következő indításkor ez a fájl a kihagyandók
// közé kerül, és a dekóder nem nyitja meg többé – így egy hibás fájl nem okoz újra és újra összeomlást. Ha a fájl
// megváltozik (más a mérete vagy a módosítás ideje, pl. kicserélték), újra próbálkozik vele. A lista a Súgó menüből
// nézhető meg és üríthető.
#pragma once

#include <QString>
#include <QVector>

namespace badfiles {

struct Entry {
    QString path;
    qint64 size = -1;
    qint64 mtime = 0;                // ms (epoch)
    QString at;                      // mikor került a listára
};

// Indításkor, a hibanapló után: az előző összeomlás fájlja a listára kerül; a dekóder ettől kezdve kihagyja őket.
void init();
QString takeNewlyAdded();            // az ebben az indításban felvett fájl (a felület egyszer kiírja), utána üres
QVector<Entry> list();
void clear();

} // namespace badfiles
