#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include "i18n.h"
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QSettings>
#include <QWidget>
#include <algorithm>
#include <QStyleFactory>

namespace theme {

static const char *kQss = R"(
QMainWindow, QDialog { background: #121418; }
QToolTip { background: #23272e; color: #e8eaed; border: 1px solid #30353e; padding: 4px 6px; }

QMenuBar { background: #1b1e24; border-bottom: 1px solid #30353e; padding: 2px 6px; }
QMenuBar::item { padding: 6px 10px; border-radius: 4px; color: #c9ced6; background: transparent; }
QMenuBar::item:selected { background: #2e333c; color: #e8eaed; }
QMenu { background: #1b1e24; border: 1px solid #30353e; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 12px; border-radius: 3px; }
QMenu::item:selected { background: #2e333c; }
QMenu::separator { height: 1px; background: #30353e; margin: 4px 6px; }

QStatusBar { background: #1b1e24; border-top: 1px solid #30353e; color: #9aa3af; font-size: 12px; }
QStatusBar::item { border: 0; }
QStatusBar QLabel { color: #9aa3af; font-size: 12px; padding: 0 6px; }

#tabRow { background: #16181d; border-bottom: 1px solid #30353e; }
#tabRow QToolButton { border: 0; border-bottom: 2px solid transparent; padding: 8px 16px; color: #9aa3af; background: transparent; }
#tabRow QToolButton:checked { color: #e8eaed; font-weight: 600; border-bottom: 2px solid @ACC@; }
#tabRow QToolButton:hover:enabled { color: #e8eaed; }
#tabRow QToolButton:disabled { color: #6b7380; }

#modeSwitch { background: #121418; border: 1px solid #30353e; border-radius: 5px; }
#segment { background: #121418; border: 1px solid #30353e; border-radius: 5px; }
#segment QToolButton { border: 0; border-radius: 3px; padding: 4px 12px; color: #c9ced6; background: transparent; }
#segment QToolButton:checked { background: #3a4150; color: #ffffff; font-weight: 600; }
#segment QToolButton:disabled { color: #6b7380; }
QToolButton#rateBtn { min-width: 42px; border: 1px solid #30353e; border-radius: 4px; background: #121418; color: #c9ced6; padding: 4px 0; }
QToolButton#rateBtn:checked { background: @ACC@; border-color: @ACC@; color: @ONACC@; font-weight: 600; }
QToolButton#rateBtn:disabled { color: #4a5160; }
#viewSwitch { background: #121418; border: 1px solid #30353e; border-radius: 5px; }
#viewSwitch QToolButton { border: 0; border-radius: 3px; padding: 3px 14px; color: #c9ced6; font-size: 13px; font-weight: 600; background: transparent; }
#viewSwitch QToolButton:checked { background: #3a4150; color: #ffffff; }
#viewSwitch QToolButton:hover:!checked { color: #ffffff; }
#tabRow #modeSwitch QToolButton { border: 0; border-radius: 3px; padding: 3px 12px; color: #c9ced6; font-size: 12px; font-weight: 400; }
#tabRow #modeSwitch QToolButton:checked { background: @ACC@; color: @ONACC@; font-weight: 600; border: 0; }
QLineEdit { background: #121418; border: 1px solid #30353e; border-radius: 4px; padding: 0 8px; min-height: 26px; selection-background-color: #3a4150; }
QLineEdit:focus { border-color: #5cb0ff; }
QListWidget { background: #121418; border: 1px solid #30353e; border-radius: 4px; outline: 0; }
QListWidget::item { padding: 6px 8px; }
QListWidget::item:selected { background: #2e333c; color: #e8eaed; }
QPushButton::menu-indicator { subcontrol-position: right center; subcontrol-origin: padding; right: 8px; }
QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #6b7380; border-radius: 3px; background: #121418; }
QCheckBox::indicator:checked { background: @ACC@; border-color: @ACC@; }
QTableView#weekGrid { gridline-color: #121418; font-size: 11px; }
QTableView#weekGrid QHeaderView::section:vertical { padding: 0 6px; border: 0; font-family: "IBM Plex Mono"; }
QSpinBox, QTimeEdit, QDoubleSpinBox { min-height: 26px; }
QRadioButton { spacing: 8px; }
QRadioButton::indicator { width: 14px; height: 14px; border: 1px solid #6b7380; border-radius: 8px; background: #121418; }
QRadioButton::indicator:checked { width: 6px; height: 6px; border: 5px solid @ACC@; border-radius: 8px; background: #121418; }
QScrollBar:horizontal { background: #121418; height: 14px; margin: 0; border-top: 1px solid #30353e; }
QScrollBar::handle:horizontal { background: #4a5160; min-width: 36px; border-radius: 4px; margin: 2px; }
QScrollBar::handle:horizontal:hover { background: #6b7380; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
QFrame#panel { background: #1b1e24; border: 1px solid #30353e; border-radius: 0; }
QFrame#panel[onair="true"] { border: 1px solid @ACC@; }
QFrame#panelHeader { border: 0; border-bottom: 1px solid #30353e; }
QFrame#panelFooter { border: 0; border-top: 1px solid #30353e; }

QLabel#caption { color: #9aa3af; font-size: 11px; letter-spacing: 1px; }
QToolButton#rulesToggle { color: #9aa3af; font-size: 11px; letter-spacing: 1px; border: 0; background: transparent; padding: 2px 0; }
QToolButton#rulesToggle:hover { color: #e6e9ee; }
QToolButton#requestsBtn { color: #c3c8d0; border: 1px solid #3a414b; border-radius: 6px; padding: 4px 10px; background: #1b1f24; }
QToolButton#requestsBtn:hover { color: #ffffff; border-color: #59616c; }
QToolButton#requestsBtn[pending="true"] { color: @ONACC@; background: @ACC@; border-color: @ACC@; font-weight: 600; }
QLabel#muted { color: #9aa3af; }
QLabel#small { color: #9aa3af; font-size: 12px; }
QLabel#panelTitle { font-size: 14px; font-weight: 600; }
QLabel#trackTitle { font-size: 17px; font-weight: 600; }
QLabel#bigTime { font-family: "IBM Plex Mono"; font-size: 34px; font-weight: 500; }
QLabel#bigTime[onair="true"] { color: @ACC@; }
QLabel#clock { font-family: "IBM Plex Mono"; font-size: 32px; font-weight: 500; }
QLabel#intro { font-family: "IBM Plex Mono"; font-size: 13px; font-weight: 500; color: #8cc8ff; min-height: 18px; }
QLabel#intro[live="true"] { font-size: 15px; font-weight: 600; color: #121418; background: #5cb0ff; border-radius: 3px; padding: 0 6px; }
QLabel#mono { font-family: "IBM Plex Mono"; color: #c9ced6; }
QLabel#badge1 { font-family: "IBM Plex Mono"; font-weight: 600; background: @PA@; color: @ONPA@; border-radius: 3px; padding: 1px 7px; }
QLabel#badge2 { font-family: "IBM Plex Mono"; font-weight: 600; background: @PB@; color: @ONPB@; border-radius: 3px; padding: 1px 7px; }
QLabel#badge3 { font-family: "IBM Plex Mono"; font-weight: 600; background: @PC@; color: @ONPC@; border-radius: 3px; padding: 1px 7px; }
QLabel#vtChip { font-family: "IBM Plex Mono"; font-size: 11px; font-weight: 700; background: #f472b6; color: #1a0b14; border-radius: 9px; padding: 1px 9px; }
QLabel#badgeP { font-family: "IBM Plex Mono"; font-weight: 600; background: #3a4150; color: #e8eaed; border-radius: 3px; padding: 1px 7px; }

QLabel#pill { font-size: 11px; font-weight: 600; letter-spacing: 1px; border-radius: 9px; padding: 1px 9px; border: 1px solid #454b57; color: #9aa3af; }
QLabel#pill[state="ready"] { border: 1px solid #5cb0ff; color: #bfe0ff; }
QLabel#pill[state="paused"] { border: 1px solid @ACC@; color: @ACCSOFT@; }
QLabel#pill[state="leaving"] { border: 1px solid @ACC@; color: @ACCSOFT@; }
QLabel#pill[state="sounding"] { border: 1px solid #8ec8ff; color: #8ec8ff; }
QLabel#pill[state="onair"] { border: 1px solid @ACC@; background: @ACC@; color: @ONACC@; }

QPushButton { background: #23272e; border: 1px solid #30353e; border-radius: 4px; padding: 0 12px; min-height: 28px; color: #e8eaed; }
QPushButton:hover:enabled { background: #2b3038; border-color: #454b57; }
QPushButton:pressed { background: #1f232a; }
QPushButton:disabled { color: #6b7380; }
QPushButton[transport="true"] { min-width: 44px; max-width: 44px; min-height: 40px; padding: 0; }
QPushButton[tall="true"] { min-height: 40px; }
QPushButton[accent="true"] { background: @ACC@; border: 0; color: @ONACC@; font-weight: 600; }
QPushButton[accent="true"]:hover:enabled { background: @ACCHOVER@; }
QPushButton[accent="true"]:disabled { background: @ACCDIS@; color: #121418; }
QPushButton[lit="true"], QPushButton[lit="true"]:disabled { background: @ACC@; border: 0; }
QPushButton[danger="true"] { color: #ff8f85; font-weight: 600; }
QPushButton[active="true"] { background: #14283d; border: 1px solid #5cb0ff; color: #bfe0ff; }
QPushButton#nextButton { font-size: 16px; min-height: 56px; }

QHeaderView::section { background: #1b1e24; color: #9aa3af; border: 0; border-bottom: 1px solid #30353e; padding: 6px 8px; font-size: 11px; font-weight: 500; }
QTableView { background: #1b1e24; border: 0; gridline-color: transparent; selection-background-color: #2e333c; selection-color: #e8eaed; outline: 0; }
QTableCornerButton::section { background: #1b1e24; border: 0; }

QProgressBar { background: #121418; border: 0; border-radius: 2px; max-height: 4px; min-height: 4px; }
QProgressBar::chunk { background: #5cb0ff; border-radius: 2px; }
)";

static void applyPalette(QApplication &app)
{
    QPalette p;
    p.setColor(QPalette::Window, panel);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::AlternateBase, c("#16181d"));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, panel2);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, c("#ffffff"));
    p.setColor(QPalette::ToolTipBase, panel2);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, c("#6b7380"));
    p.setColor(QPalette::Highlight, c("#3a4150"));
    p.setColor(QPalette::HighlightedText, text);
    p.setColor(QPalette::Link, blue);
    p.setColor(QPalette::Light, c("#3a4150"));
    p.setColor(QPalette::Midlight, c("#30353e"));
    p.setColor(QPalette::Mid, c("#30353e"));
    p.setColor(QPalette::Dark, c("#16181d"));
    p.setColor(QPalette::Shadow, c("#0c0d10"));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, c("#6b7380"));
    app.setPalette(p);
}

static void installCssMapper(QApplication &app);
static void drawCheck();

void apply(QApplication &app)
{
    loadColors();
    for (const char *f : {":/fonts/IBMPlexSans-Regular.ttf", ":/fonts/IBMPlexSans-Medium.ttf",
                          ":/fonts/IBMPlexSans-SemiBold.ttf", ":/fonts/IBMPlexMono-Regular.ttf",
                          ":/fonts/IBMPlexMono-Medium.ttf", ":/fonts/IBMPlexMono-SemiBold.ttf"})
        QFontDatabase::addApplicationFont(f);

    QFont font("IBM Plex Sans");
    font.setPixelSize(13);
    app.setFont(font);

    app.setStyle(QStyleFactory::create("Fusion"));
    applyPalette(app);
    installCssMapper(app);

    drawCheck();
    app.setStyleSheet(styleSheet());
}

QString &checkImage()
{
    static QString path;
    return path;
}

// ---- Színsémák (Beállítások → Megjelenés) ------------------------------------------------------------------------

const QList<Role> &roles()
{
    static const QList<Role> r{
        {QStringLiteral("bg"), QT_TR_NOOP("Háttér"), QT_TR_NOOP("Az ablakok alapja, a beviteli mezők és listák háttere."), QColor("#121418")},
        {QStringLiteral("panel"), QT_TR_NOOP("Panelek"), QT_TR_NOOP("A lejátszók, a playlist, a hangtár és a menüsor felülete."), QColor("#1b1e24")},
        {QStringLiteral("raised"), QT_TR_NOOP("Gombok"), QT_TR_NOOP("A gombok és a kiemelt felületek."), QColor("#23272e")},
        {QStringLiteral("border"), QT_TR_NOOP("Keretek"), QT_TR_NOOP("A keretek és az elválasztó vonalak."), QColor("#30353e")},
        {QStringLiteral("text"), QT_TR_NOOP("Szöveg"), QT_TR_NOOP("A feliratok és a listák szövege."), QColor("#e8eaed")},
        {QStringLiteral("muted"), QT_TR_NOOP("Halvány szöveg"), QT_TR_NOOP("A másodlagos feliratok, oszlopfejlécek, megjegyzések."), QColor("#9aa3af")},
        {QStringLiteral("accent"), QT_TR_NOOP("Kiemelő szín"), QT_TR_NOOP("A kijelölt fül, a fő gombok, az adásban lévő elem és a jelölőnégyzetek."), QColor("#f5a524")},
        {QStringLiteral("info"), QT_TR_NOOP("Információ"), QT_TR_NOOP("A készenlét, a fókusz, a hivatkozások és a folyamatjelzők."), QColor("#5cb0ff")},
        {QStringLiteral("ok"), QT_TR_NOOP("Rendben"), QT_TR_NOOP("A sikeres műveletek és a működő kapcsolatok."), QColor("#45d483")},
        {QStringLiteral("warn"), QT_TR_NOOP("Hiba"), QT_TR_NOOP("A hibák, figyelmeztetések és a hiányzó fájlok."), QColor("#ff8f85")},
        {QStringLiteral("player0"), QT_TR_NOOP("A lejátszó"), QT_TR_NOOP("Az A lejátszó betűjele, a mini mixer és a playlist jelölése."), QColor("#f5a524")},
        {QStringLiteral("player1"), QT_TR_NOOP("B lejátszó"), QT_TR_NOOP("A B lejátszó betűjele, a mini mixer és a playlist jelölése."), QColor("#5cb0ff")},
        {QStringLiteral("player2"), QT_TR_NOOP("C lejátszó"), QT_TR_NOOP("A C lejátszó betűjele, a mini mixer és a playlist jelölése."), QColor("#3fd0b5")},
    };
    return r;
}

Colors defaultColors()
{
    Colors c;
    for (const Role &r : roles()) c[r.key] = r.def;
    return c;
}

static Colors fill(Colors c)
{
    // A hiányzó szerepek a gyári színt kapják.
    for (const Role &r : roles())
        if (!c.value(r.key).isValid()) c[r.key] = r.def;
    return c;
}

namespace {
struct BuiltIn {
    const char *id;
    const char *name;
    const char *colors[13];     // a roles() sorrendjében
};
const BuiltIn kBuiltIns[] = {
    {"eter", QT_TR_NOOP("Éter (sötét)"),
     {"#121418", "#1b1e24", "#23272e", "#30353e", "#e8eaed", "#9aa3af", "#f5a524", "#5cb0ff", "#45d483", "#ff8f85", "#f5a524", "#5cb0ff", "#3fd0b5"}},
    {"graphite", QT_TR_NOOP("Grafit"),
     {"#161616", "#1f1f1f", "#2a2a2a", "#3a3a3a", "#ececec", "#a3a3a3", "#7aa7ff", "#7aa7ff", "#4cc38a", "#ff7b72", "#f5a524", "#7aa7ff", "#3fd0b5"}},
    {"midnight", QT_TR_NOOP("Éjkék"),
     {"#0b1322", "#111d33", "#182943", "#253a5c", "#e6edf7", "#8fa3c0", "#ffb020", "#64b5ff", "#45d483", "#ff8f85", "#ffb020", "#64b5ff", "#3fd0b5"}},
    {"contrast", QT_TR_NOOP("Nagy kontraszt"),
     {"#000000", "#0b0b0b", "#1c1c1c", "#7a7a7a", "#ffffff", "#d0d0d0", "#ffd400", "#4fc3ff", "#3ce37a", "#ff6b6b", "#ffd400", "#4fc3ff", "#3ce3c0"}},
    {"light", QT_TR_NOOP("Világos"),
     {"#e9ecf0", "#ffffff", "#f3f4f6", "#cfd4dc", "#1c1f24", "#5f6773", "#d97a0b", "#1f6fd1", "#1e8f4e", "#cc3b3b", "#d97a0b", "#1f6fd1", "#11897a"}},
};

Colors builtInColors(const BuiltIn &b)
{
    Colors c;
    const auto &rs = roles();
    for (int i = 0; i < rs.size() && i < 13; ++i) c[rs[i].key] = QColor(b.colors[i]);
    return c;
}

QJsonObject colorsToJson(const Colors &c)
{
    QJsonObject o;
    for (const Role &r : roles()) o[r.key] = c.value(r.key, r.def).name();
    return o;
}

Colors colorsFromJson(const QJsonObject &o)
{
    Colors c;
    for (const Role &r : roles()) {
        const QColor v(o.value(r.key).toString());
        if (v.isValid()) c[r.key] = v;
    }
    return fill(c);
}

QJsonObject savedObject()
{
    return QJsonDocument::fromJson(QSettings().value(QStringLiteral("ui/schemes")).toByteArray()).object();
}

void saveObject(const QJsonObject &o)
{
    QSettings().setValue(QStringLiteral("ui/schemes"), QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// ---- A leképezés ----
Colors g_cur = Colors();        // az érvényes színek (loadColors)
bool g_identity = true;
QHash<QRgb, QRgb> g_cache;

double lum(const QColor &c)
{
    auto ch = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch(c.redF()) + 0.7152 * ch(c.greenF()) + 0.0722 * ch(c.blueF());
}

struct V3 {
    double r, g, b;
};
V3 v(const QColor &c) { return {double(c.red()), double(c.green()), double(c.blue())}; }
V3 mixv(const V3 &a, const V3 &b, double t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }

// A c szín helye az a→b szakaszon (vetítés), és a szakasztól való eltérése.
double project(const V3 &a, const V3 &b, const V3 &c)
{
    const V3 d{b.r - a.r, b.g - a.g, b.b - a.b};
    const double dd = d.r * d.r + d.g * d.g + d.b * d.b;
    if (dd < 1e-9) return 0;
    return ((c.r - a.r) * d.r + (c.g - a.g) * d.g + (c.b - a.b) * d.b) / dd;
}

QRgb mapSegment(const QColor &a0, const QColor &b0, const QColor &a1, const QColor &b1, const QColor &col, double t)
{
    // Az eredeti szakasz t pontjától való eltérés megmarad, a szakasz végpontjai az új színekre cserélődnek.
    const V3 base0 = mixv(v(a0), v(b0), t), base1 = mixv(v(a1), v(b1), t), cc = v(col);
    auto cl = [](double x) { return std::clamp(int(std::lround(x)), 0, 255); };
    return qRgb(cl(base1.r + cc.r - base0.r), cl(base1.g + cc.g - base0.g), cl(base1.b + cc.b - base0.b));
}

const char *familyOf(QRgb rgb)
{
    // A szerepszínek gyári árnyalatai (a halványabb és a sötétebb változatok is).
    static const QHash<QRgb, const char *> fam = [] {
        QHash<QRgb, const char *> h;
        for (const char *x : {"#f5a524", "#ffd58a", "#ffd27a", "#ffb020"}) h[QColor(x).rgb()] = "accent";
        for (const char *x : {"#5cb0ff", "#bfe0ff", "#8ec8ff", "#8cc8ff", "#9fd0ff", "#8fb8e6"}) h[QColor(x).rgb()] = "info";
        for (const char *x : {"#45d483", "#9be8b8", "#4cc38a", "#9fe0a8"}) h[QColor(x).rgb()] = "ok";
        for (const char *x : {"#ff8f85", "#ffb4ae", "#ffd0cc", "#e5484d", "#ff5a4f", "#ff6b6b"}) h[QColor(x).rgb()] = "warn";
        return h;
    }();
    return fam.value(rgb, nullptr);
}

QRgb mapRgb(QRgb rgb)
{
    if (g_identity) return rgb;
    if (auto it = g_cache.constFind(rgb); it != g_cache.cend()) return *it;
    const QColor col = QColor::fromRgb(rgb);
    const Colors def = defaultColors();
    QRgb out = rgb;
    if (const char *f = familyOf(rgb)) {
        // Egy szerepszín árnyalata: a háttér felé sötétebb, a szöveg felé világosabb változat.
        const QString k = QLatin1String(f);
        const QColor r0 = def[k], r1 = g_cur.value(k, r0);
        if (lum(col) <= lum(r0)) {
            const double t = project(v(def["bg"]), v(r0), v(col));
            out = mapSegment(def["bg"], r0, g_cur["bg"], r1, col, t);
        } else {
            const double t = project(v(r0), v(def["text"]), v(col));
            out = mapSegment(r0, def["text"], r1, g_cur["text"], col, t);
        }
    } else {
        const int chroma = std::max({col.red(), col.green(), col.blue()}) - std::min({col.red(), col.green(), col.blue()});
        const double l = lum(col);
        // Szürkék, a sötét színezett hátterek és a halvány, színezett szövegszínek: a gyári szürkeskála (háttér →
        // szöveg) megfelelő helyére, az árnyalatuk megmarad. Az élénk középtónusok (pl. a kategóriák színei) maradnak.
        if (chroma < 32 || l < lum(def["border"]) * 1.15 || l > lum(def["muted"])) {
            static const char *ramp[] = {"bg", "panel", "raised", "border", "muted", "text"};
            constexpr int n = 6;
            int i = 0;
            while (i < n - 2 && l > lum(def[ramp[i + 1]])) ++i;
            const QColor a0 = def[ramp[i]], b0 = def[ramp[i + 1]];
            const double la = lum(a0), lb = lum(b0);
            double t = (l - la) / (lb - la);
            t = std::clamp(t, -1.5, 2.0);
            out = mapSegment(a0, b0, g_cur[ramp[i]], g_cur[ramp[i + 1]], col, t);
        }
    }
    g_cache.insert(rgb, out);
    return out;
}
} // namespace

QColor c(const char *hexv) { return c(QColor(hexv)); }

QColor c(const QColor &f)
{
    if (g_identity || !f.isValid()) return f;
    QColor r = QColor::fromRgb(mapRgb(f.rgb()));
    r.setAlpha(f.alpha());
    return r;
}

QString hex(const char *hexv) { return c(hexv).name(); }

void rawCss(QWidget *w) { w->setProperty("_eterCssRaw", true); }

QString css(const QString &sheet)
{
    if (g_identity) return sheet;
    static const QRegularExpression re(QStringLiteral("#([0-9a-fA-F]{6})\\b"));
    QString out;
    out.reserve(sheet.size());
    qsizetype last = 0;
    auto it = re.globalMatch(sheet);
    while (it.hasNext()) {
        const auto m = it.next();
        // Az objektumnév-választók (#panel, #badge1) nem színek; a hat hexa jegyű név ritka, de a „#c0ffee” is az.
        out += sheet.mid(last, m.capturedStart() - last);
        out += QColor::fromRgb(mapRgb(QColor(m.captured(0)).rgb())).name();
        last = m.capturedEnd();
    }
    out += sheet.mid(last);
    return out;
}

double contrast(const QColor &a, const QColor &b)
{
    const double la = lum(a), lb = lum(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

QList<Scheme> schemes()
{
    QList<Scheme> out;
    for (const BuiltIn &b : kBuiltIns) out << Scheme{QStringLiteral("builtin:") + QLatin1String(b.id), TR(b.name), builtInColors(b), true};
    const QJsonObject o = savedObject();
    QStringList names = o.keys();
    std::sort(names.begin(), names.end(), [](const QString &x, const QString &y) { return x.localeAwareCompare(y) < 0; });
    for (const QString &n : names) out << Scheme{QStringLiteral("user:") + n, n, colorsFromJson(o.value(n).toObject()), false};
    return out;
}

Scheme scheme(const QString &id)
{
    for (const Scheme &s : schemes())
        if (s.id == id) return s;
    return {};
}

bool isBuiltInName(const QString &name)
{
    for (const BuiltIn &b : kBuiltIns)
        if (name.compare(TR(b.name), Qt::CaseInsensitive) == 0 || name.compare(QLatin1String(b.name), Qt::CaseInsensitive) == 0) return true;
    return false;
}

QString uniqueSchemeName(const QString &base)
{
    const QString b = base.trimmed().isEmpty() ? TR("Saját séma") : base.trimmed();
    const QJsonObject o = savedObject();
    auto taken = [&](const QString &n) { return o.contains(n) || isBuiltInName(n); };
    if (!taken(b)) return b;
    for (int i = 2;; ++i) {
        const QString n = QStringLiteral("%1 (%2)").arg(b).arg(i);
        if (!taken(n)) return n;
    }
}

QString saveScheme(const QString &name, const Colors &c)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || isBuiltInName(n)) return {};
    QJsonObject o = savedObject();
    o[n] = colorsToJson(fill(c));
    saveObject(o);
    return QStringLiteral("user:") + n;
}

bool removeScheme(const QString &id)
{
    if (!id.startsWith(QLatin1String("user:"))) return false;
    QJsonObject o = savedObject();
    if (!o.contains(id.mid(5))) return false;
    o.remove(id.mid(5));
    saveObject(o);
    if (currentSchemeId() == id) QSettings().setValue(QStringLiteral("ui/scheme"), QString());
    return true;
}

QString renameScheme(const QString &id, const QString &newName)
{
    const QString n = newName.trimmed();
    if (!id.startsWith(QLatin1String("user:")) || n.isEmpty() || isBuiltInName(n)) return {};
    QJsonObject o = savedObject();
    const QString old = id.mid(5);
    if (!o.contains(old) || (o.contains(n) && n != old)) return {};
    const QJsonValue val = o.value(old);
    o.remove(old);
    o[n] = val;
    saveObject(o);
    const QString nid = QStringLiteral("user:") + n;
    if (currentSchemeId() == id) QSettings().setValue(QStringLiteral("ui/scheme"), nid);
    return nid;
}

QJsonObject schemeToJson(const QString &name, const Colors &c)
{
    QJsonObject o;
    o[QStringLiteral("format")] = QStringLiteral("eter-color-scheme");
    o[QStringLiteral("version")] = 1;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("colors")] = colorsToJson(fill(c));
    return o;
}

bool schemeFromJson(const QByteArray &data, QString *name, Colors *out, QString *err)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (!doc.isObject()) {
        if (err) *err = TR("A fájl nem érvényes JSON (%1).").arg(pe.errorString());
        return false;
    }
    const QJsonObject o = doc.object();
    if (o.value(QStringLiteral("format")).toString() != QLatin1String("eter-color-scheme")) {
        if (err) *err = TR("A fájl nem Éter színséma.");
        return false;
    }
    if (o.value(QStringLiteral("version")).toInt() > 1) {
        if (err) *err = TR("A színsémát a program újabb változata készítette.");
        return false;
    }
    const QJsonObject cols = o.value(QStringLiteral("colors")).toObject();
    int valid = 0;
    for (const Role &r : roles())
        if (QColor(cols.value(r.key).toString()).isValid()) ++valid;
    if (valid == 0) {
        if (err) *err = TR("A fájlban nincs egyetlen érvényes szín sem.");
        return false;
    }
    if (name) *name = o.value(QStringLiteral("name")).toString().trimmed().left(60);
    if (out) *out = colorsFromJson(cols);
    return true;
}

// ---- Az érvényes színek ----

QColor defaultPlayerColor(int i)
{
    static const QColor c[3] = {QColor("#f5a524"), QColor("#5cb0ff"), QColor("#3fd0b5")};
    return c[std::clamp(i, 0, 2)];
}

static QColor players[3] = {QColor("#f5a524"), QColor("#5cb0ff"), QColor("#3fd0b5")};

QColor playerColor(int i) { return players[std::clamp(i, 0, 2)]; }

QColor onColor(const QColor &col)
{
    // Sötét szöveg világos háttéren, fehér a sötéten (relatív fényesség).
    const double l = 0.2126 * col.redF() + 0.7152 * col.greenF() + 0.0722 * col.blueF();
    return l > 0.45 ? QColor("#121418") : QColor("#ffffff");
}

// A szerepek tárolása (gépenként): a kiemelő szín és a lejátszók a korábbi kulcsokon maradnak.
static QString roleKey(const QString &role)
{
    if (role == QLatin1String("accent")) return QStringLiteral("ui/accent");
    if (role.startsWith(QLatin1String("player"))) return QStringLiteral("ui/") + role;
    return QStringLiteral("ui/color/") + role;
}

Colors currentColors()
{
    QSettings s;
    Colors out;
    for (const Role &r : roles()) {
        const QColor v(s.value(roleKey(r.key), r.def.name()).toString());
        out[r.key] = v.isValid() ? v : r.def;
    }
    return out;
}

QString currentSchemeId()
{
    return QSettings().value(QStringLiteral("ui/scheme"), QStringLiteral("builtin:eter")).toString();
}

bool currentModified()
{
    const Scheme s = scheme(currentSchemeId());
    if (s.id.isEmpty()) return true;
    const Colors cur = currentColors();
    for (const Role &r : roles())
        if (cur.value(r.key).rgb() != s.colors.value(r.key).rgb()) return true;
    return false;
}

void setCurrentColors(const Colors &col, const QString &schemeId)
{
    QSettings s;
    const Colors f = fill(col);
    for (const Role &r : roles()) s.setValue(roleKey(r.key), f.value(r.key).name());
    if (!schemeId.isNull()) s.setValue(QStringLiteral("ui/scheme"), schemeId);
    reapply();
}

void loadColors()
{
    g_cur = currentColors();
    g_cache.clear();
    const Colors def = defaultColors();
    g_identity = true;
    for (const Role &r : roles())
        if (!r.key.startsWith(QLatin1String("player")) && g_cur[r.key].rgb() != def[r.key].rgb())
            g_identity = false;
    bg = g_cur["bg"];
    panel = g_cur["panel"];
    panel2 = g_cur["raised"];
    border = g_cur["border"];
    text = g_cur["text"];
    muted = g_cur["muted"];
    amber = g_cur["accent"];
    blue = g_cur["info"];
    green = g_cur["ok"];
    red = g_cur["warn"];
    blueSoft = c("#bfe0ff");
    waveIdle = c("#4a5160");
    // A halványabb változat: a kiemelő szín világosabb, kevésbé telített árnyalata (világos témán sötétebb).
    if (lum(text) > lum(bg)) {
        QColor soft = amber.toHsl();
        soft.setHsl(soft.hslHue(), std::min(255, int(soft.hslSaturation())), std::min(235, soft.lightness() + 70));
        amberSoft = soft.toRgb();
    } else {
        amberSoft = amber.darker(135);
    }
    for (int i = 0; i < 3; ++i) players[i] = g_cur[QStringLiteral("player%1").arg(i)];
}

QString styleSheet()
{
    auto mix = [](const QColor &a, const QColor &b, double t) {
        return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                                a.blueF() + (b.blueF() - a.blueF()) * t);
    };
    QString qss = css(QString::fromUtf8(kQss));
    qss.replace(QLatin1String("@ACCHOVER@"), (lum(text) > lum(bg) ? amber.lighter(112) : amber.darker(110)).name());
    qss.replace(QLatin1String("@ACCSOFT@"), amberSoft.name());
    qss.replace(QLatin1String("@ACCDIS@"), mix(bg, amber, 0.35).name());
    qss.replace(QLatin1String("@ONACC@"), onColor(amber).name());
    qss.replace(QLatin1String("@ACC@"), amber.name());
    qss.replace(QLatin1String("@ONPA@"), onColor(players[0]).name());
    qss.replace(QLatin1String("@ONPB@"), onColor(players[1]).name());
    qss.replace(QLatin1String("@ONPC@"), onColor(players[2]).name());
    qss.replace(QLatin1String("@PA@"), players[0].name());
    qss.replace(QLatin1String("@PB@"), players[1].name());
    qss.replace(QLatin1String("@PC@"), players[2].name());
    if (!checkImage().isEmpty()) qss += QStringLiteral("QCheckBox::indicator:checked { image: url(%1); }").arg(checkImage());
    return qss;
}

// A soron belüli stíluslapok (setStyleSheet a widgeteken) a gyári színekkel készülnek: az eredetit a widget
// megjegyzi, és minden sémaváltáskor (és beállításkor) a leképezett változatot kapja.
namespace {
class CssMapper : public QObject {
public:
    using QObject::QObject;
    static void remap(QWidget *w)
    {
        const QString cur = w->styleSheet();
        if (cur.isEmpty() || w->property("_eterCssRaw").toBool()) return;
        const QVariant mapped = w->property("_eterCssMapped");
        QString orig = cur;
        if (mapped.isValid() && mapped.toString() == cur) orig = w->property("_eterCss").toString();
        const QString m = css(orig);
        w->setProperty("_eterCss", orig);
        w->setProperty("_eterCssMapped", m);
        if (m != cur) w->setStyleSheet(m);
    }
protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if ((e->type() == QEvent::StyleChange || e->type() == QEvent::Polish) && o->isWidgetType()) {
            auto *w = static_cast<QWidget *>(o);
            const QString cur = w->styleSheet();
            if (!cur.isEmpty() && !w->property("_eterCssRaw").toBool() && w->property("_eterCssMapped").toString() != cur && !w->property("_eterCssBusy").toBool()) {
                w->setProperty("_eterCssBusy", true);
                w->setProperty("_eterCssMapped", QVariant());
                remap(w);
                w->setProperty("_eterCssBusy", false);
            }
        }
        return false;
    }
};
} // namespace

static void installCssMapper(QApplication &app)
{
    static CssMapper *m = nullptr;
    // Hibakereséshez: ETER_NO_CSSMAP=1 esetén a soron belüli stíluslapok leképezése ki van kapcsolva.
    if (qEnvironmentVariableIsSet("ETER_NO_CSSMAP")) return;
    if (!m) {
        m = new CssMapper(&app);
        app.installEventFilter(m);
    }
}

static void drawCheck()
{
    // Jelölőnégyzet: a pipa képét futás közben rajzoljuk meg, hogy a kiemelő színen jól látsszon.
    QPixmap check(32, 32);
    check.fill(Qt::transparent);
    {
        QPainter cp(&check);
        cp.setRenderHint(QPainter::Antialiasing);
        cp.setPen(QPen(onColor(amber), 4.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath path;
        path.moveTo(7, 17);
        path.lineTo(13.5, 23);
        path.lineTo(25, 9.5);
        cp.drawPath(path);
    }
    static int gen = 0;
    const QString checkPath = QDir::temp().filePath(QStringLiteral("eter-check-%1-%2.png").arg(QCoreApplication::applicationPid()).arg(++gen % 2));
    checkImage() = check.save(checkPath) ? checkPath : QString();
}

void reapply()
{
    loadColors();
    if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        drawCheck();
        applyPalette(*app);
        app->setStyleSheet(styleSheet());
        for (QWidget *w : QApplication::allWidgets()) {
            if (!w->styleSheet().isEmpty()) {
                w->setProperty("_eterCssBusy", true);
                CssMapper::remap(w);
                w->setProperty("_eterCssBusy", false);
            }
            w->update();
        }
    }
}

int uiScalePercent()
{
    // 100 % alatt a Qt hibásan rajzol (1-nél kisebb méretezési szorzó): a régi 80 és 90 % értéke 100 %.
    return std::clamp(QSettings().value(QStringLiteral("ui/scale"), 100).toInt(), 100, 200);
}

static QPixmap drawIcon(Icon type, const QColor &color);

QIcon icon(Icon type, const QColor &color, bool keepWhenDisabled)
{
    QIcon ic(drawIcon(type, color));
    QColor dim = color;
    if (!keepWhenDisabled) dim.setAlphaF(0.35);
    ic.addPixmap(drawIcon(type, dim), QIcon::Disabled);
    return ic;
}

static QPixmap drawIcon(Icon type, const QColor &color)
{
    const qreal dpr = 2.0;
    QPixmap pm(QSize(16, 16) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (type) {
    case Icon::Play: {
        QPainterPath path;
        path.moveTo(4, 2.5);
        path.lineTo(4, 13.5);
        path.lineTo(13, 8);
        path.closeSubpath();
        p.setBrush(color);
        p.drawPath(path);
        break;
    }
    case Icon::Pause:
        pen.setWidthF(2.2);
        p.setPen(pen);
        p.drawLine(QPointF(5, 3), QPointF(5, 13));
        p.drawLine(QPointF(11, 3), QPointF(11, 13));
        break;
    case Icon::Stop:
        p.setBrush(color);
        p.drawRoundedRect(QRectF(3.5, 3.5, 9, 9), 1, 1);
        break;
    case Icon::Eject: {
        QPainterPath path;
        path.moveTo(3, 9.5);
        path.lineTo(8, 3.5);
        path.lineTo(13, 9.5);
        path.closeSubpath();
        p.drawPath(path);
        p.drawLine(QPointF(3, 12.5), QPointF(13, 12.5));
        break;
    }
    }
    return pm;
}

QIcon appIcon()
{
    QPixmap pm(128, 128);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(4, 4, 120, 120), 24, 24);
    p.setBrush(amber);
    p.drawEllipse(QPointF(64, 64), 11, 11);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(amber, 8, Qt::SolidLine, Qt::RoundCap));
    for (int r : {28, 46}) {
        p.drawArc(QRectF(64 - r, 64 - r, 2 * r, 2 * r), -50 * 16, 100 * 16);
        p.drawArc(QRectF(64 - r, 64 - r, 2 * r, 2 * r), 130 * 16, 100 * 16);
    }
    return QIcon(pm);
}

QString formatTime(qint64 frames)
{
    if (frames < 0) frames = 0;
    const qint64 s = frames / 48000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

} // namespace theme
