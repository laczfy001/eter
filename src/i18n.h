// Nyelvkezelés. A felület szövegei magyarul vannak a forrásban; angol nyelvnél a TR() a
// fordítótáblából (i18n_en.cpp, az i18n/en.json alapján generálva) adja vissza a megfelelőjüket.
// A nyelv a program indulásakor dől el; váltás után újra kell indítani.
#pragma once
#include <QLocale>
#include <QString>
#include <string>

namespace i18n {
void setLanguage(const QString &code);      // "en" vagy "hu"
QString language();
QString text(const char *hungarian);        // a szöveg a beállított nyelven
std::string textStd(const char *hungarian); // ugyanaz, a Qt nélküli hangmotor hibaüzeneteihez
QLocale locale();                           // számok és dátumok formátuma
}

#define TR(s) i18n::text(s)
