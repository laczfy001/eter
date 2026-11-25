#pragma once
#include <QColor>
#include <QIcon>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QString>

class QApplication;

namespace theme {

// Az aktuális színséma alapszínei (Beállítások → Megjelenés → Színséma). A loadColors() frissíti őket.
inline QColor bg("#121418");
inline QColor panel("#1b1e24");
inline QColor panel2("#23272e");
inline QColor border("#30353e");
inline QColor text("#e8eaed");
inline QColor muted("#9aa3af");
inline QColor amber("#f5a524");          // kiemelő szín
inline QColor amberSoft("#ffd58a");
inline QColor blue("#5cb0ff");
inline QColor blueSoft("#bfe0ff");
inline QColor green("#45d483");
inline QColor red("#ff8f85");
inline QColor waveIdle("#4a5160");

enum class Icon { Play, Pause, Stop, Eject };

void apply(QApplication &app);

// ---- Színsémák ----------------------------------------------------------------------------------------------------
// A felület minden színe az alapszínekből jön: a program kódjában és stíluslapjaiban a gyári sötét téma színei állnak,
// ezeket a c() / css() képezi le az aktuális sémára (a köztes árnyalatok a két szomszédos alapszín közé esnek, az
// eltérő árnyalatuk megmarad). A gyári sémánál a leképezés semmit nem változtat.
struct Role {
    QString key;            // "bg", "panel", …, "player0"
    const char *label;      // magyar felirat (TR)
    const char *hint;       // rövid magyarázat (TR)
    QColor def;             // a gyári sötét téma színe
};
const QList<Role> &roles();
using Colors = QMap<QString, QColor>;      // szerep → szín
struct Scheme {
    QString id;             // "builtin:eter" vagy "user:<név>"
    QString name;           // a megjelenített név
    Colors colors;
    bool builtIn = false;
};
QList<Scheme> schemes();                    // a beépítettek, majd a mentettek név szerint
Scheme scheme(const QString &id);           // üres id: nincs ilyen
Colors defaultColors();
Colors currentColors();                     // a most érvényes színek
void setCurrentColors(const Colors &c, const QString &schemeId);   // menti és alkalmazza (reapply)
QString currentSchemeId();
bool currentModified();                     // a színek eltérnek a kiválasztott séma mentett színeitől
QString saveScheme(const QString &name, const Colors &c);          // a mentett séma azonosítója
bool removeScheme(const QString &id);
QString renameScheme(const QString &id, const QString &newName);   // az új azonosító (üres: nem sikerült)
QString uniqueSchemeName(const QString &base);                      // „Név”, „Név (2)”, …
bool isBuiltInName(const QString &name);
QJsonObject schemeToJson(const QString &name, const Colors &c);
// Import: a fájl tartalmából a név és a színek; hiba esetén false és err.
bool schemeFromJson(const QByteArray &data, QString *name, Colors *c, QString *err);
double contrast(const QColor &a, const QColor &b);                  // WCAG kontrasztarány (1–21)

// A gyári színből az aktuális séma színe (a festéshez és a soron belüli stíluslapokhoz).
QColor c(const char *hex);
QColor c(const QColor &factory);
QString hex(const char *hex);               // c(hex).name()
QString css(const QString &styleSheet);     // minden #rrggbb leképezve
// A widget stíluslapja már a séma színeivel készül (pl. a lejátszó vagy a kategória színéből): nem kell leképezni.
void rawCss(class QWidget *w);

// A fájl szintű színtáblákhoz: a gyári színeket tárolja, olvasáskor a séma szerint adja vissza.
template <int N> struct ColorTable {
    const char *hexes[N];
    QColor operator[](int i) const { return c(hexes[i]); }
};
struct ColorRef {
    const char *hexv;
    operator QColor() const { return c(hexv); }
    QColor get() const { return c(hexv); }
};
// Saját színek és méret (Beállítások → Megjelenés; gépenként, QSettings ui/…).
QColor playerColor(int i);                  // az A, B, C lejátszó színe
QColor defaultPlayerColor(int i);
QColor onColor(const QColor &background);   // olvasható szövegszín ezen a háttéren
void loadColors();
QString styleSheet();
void reapply();                             // a megváltozott színek alkalmazása (a stíluslap újra)
int uiScalePercent();                       // a felület mérete (100–200 %); újraindítás után érvényes
QString &checkImage();
QIcon icon(Icon type, const QColor &color, bool keepWhenDisabled = false);
QIcon appIcon();

// 48 kHz-es képkockaszámból „p:mm" alakú idő
QString formatTime(qint64 frames);

} // namespace theme
