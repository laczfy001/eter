#pragma once
// Az ének (vokál) jellege: az ütemező „azonos ének egymás után legfeljebb” szabályához.
#include "i18n.h"

#include <QColor>
#include <QString>

namespace vocal {

enum Type { Unknown = 0, Male = 1, Female = 2, Group = 3, Mixed = 4, Instrumental = 5 };

inline bool valid(int v) { return v >= Unknown && v <= Instrumental; }
// Beleszámít-e az azonos ének sorozatába: a nem ismert és az instrumentális nem (és nem is szakítja meg).
inline bool counts(int v) { return v >= Male && v <= Mixed; }

inline QString name(int v)
{
    switch (v) {
    case Male: return TR("Férfi");
    case Female: return TR("Női");
    case Group: return TR("Együttes");
    case Mixed: return TR("Vegyes");
    case Instrumental: return TR("Instrumentális");
    default: return TR("nem ismert");
    }
}
// Melléknévi alak a figyelmeztetéshez: „3. női ének egymás után”.
inline QString adjective(int v)
{
    switch (v) {
    case Male: return TR("férfi");
    case Female: return TR("női");
    case Group: return TR("együttes");
    case Mixed: return TR("vegyes");
    default: return QString();
    }
}
// Egybetűs jel a playlistben (magyarul F N E V I, angolul M F G X I – ott az F a „female”).
inline QString letter(int v)
{
    static const char *hu[] = {"", "F", "N", "E", "V", "I"}, *en[] = {"", "M", "F", "G", "X", "I"};
    if (!valid(v)) return QString();
    return QString::fromLatin1(i18n::language() == QLatin1String("en") ? en[v] : hu[v]);
}
inline QColor color(int v)
{
    switch (v) {
    case Male: return QColor("#9fd0ff");
    case Female: return QColor("#ffd58a");
    case Group: return QColor("#9be8b8");
    case Mixed: return QColor("#d7b8ff");
    case Instrumental: return QColor("#c9ced6");
    default: return QColor("#6b727c");
    }
}

} // namespace vocal
