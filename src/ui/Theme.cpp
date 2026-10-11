#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
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
#tabRow QToolButton:checked { color: #e8eaed; font-weight: 600; border-bottom: 2px solid #f5a524; }
#tabRow QToolButton:hover:enabled { color: #e8eaed; }
#tabRow QToolButton:disabled { color: #6b7380; }

#modeSwitch { background: #121418; border: 1px solid #30353e; border-radius: 5px; }
#viewSwitch { background: #121418; border: 1px solid #30353e; border-radius: 5px; }
#viewSwitch QToolButton { border: 0; border-radius: 3px; padding: 3px 14px; color: #c9ced6; font-size: 13px; font-weight: 600; background: transparent; }
#viewSwitch QToolButton:checked { background: #3a4150; color: #ffffff; }
#viewSwitch QToolButton:hover:!checked { color: #ffffff; }
#tabRow #modeSwitch QToolButton { border: 0; border-radius: 3px; padding: 3px 12px; color: #c9ced6; font-size: 12px; font-weight: 400; }
#tabRow #modeSwitch QToolButton:checked { background: #f5a524; color: #121418; font-weight: 600; border: 0; }
QLineEdit { background: #121418; border: 1px solid #30353e; border-radius: 4px; padding: 0 8px; min-height: 26px; selection-background-color: #3a4150; }
QLineEdit:focus { border-color: #5cb0ff; }
QListWidget { background: #121418; border: 1px solid #30353e; border-radius: 4px; outline: 0; }
QListWidget::item { padding: 6px 8px; }
QListWidget::item:selected { background: #2e333c; color: #e8eaed; }
QPushButton::menu-indicator { subcontrol-position: right center; subcontrol-origin: padding; right: 8px; }
QCheckBox { spacing: 8px; }
QCheckBox::indicator { width: 16px; height: 16px; border: 1px solid #6b7380; border-radius: 3px; background: #121418; }
QCheckBox::indicator:checked { background: #f5a524; border-color: #f5a524; }
QTableView#weekGrid { gridline-color: #121418; font-size: 11px; }
QTableView#weekGrid QHeaderView::section:vertical { padding: 0 6px; border: 0; font-family: "IBM Plex Mono"; }
QSpinBox, QTimeEdit, QDoubleSpinBox { min-height: 26px; }
QRadioButton { spacing: 8px; }
QRadioButton::indicator { width: 14px; height: 14px; border: 1px solid #6b7380; border-radius: 8px; background: #121418; }
QRadioButton::indicator:checked { width: 6px; height: 6px; border: 5px solid #f5a524; border-radius: 8px; background: #121418; }
QScrollBar:horizontal { background: #121418; height: 14px; margin: 0; border-top: 1px solid #30353e; }
QScrollBar::handle:horizontal { background: #4a5160; min-width: 36px; border-radius: 4px; margin: 2px; }
QScrollBar::handle:horizontal:hover { background: #6b7380; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }
QFrame#panel { background: #1b1e24; border: 1px solid #30353e; border-radius: 6px; }
QFrame#panel[onair="true"] { border: 1px solid #f5a524; }
QFrame#panelHeader { border: 0; border-bottom: 1px solid #30353e; }
QFrame#panelFooter { border: 0; border-top: 1px solid #30353e; }

QLabel#caption { color: #9aa3af; font-size: 11px; letter-spacing: 1px; }
QLabel#muted { color: #9aa3af; }
QLabel#small { color: #9aa3af; font-size: 12px; }
QLabel#panelTitle { font-size: 14px; font-weight: 600; }
QLabel#trackTitle { font-size: 17px; font-weight: 600; }
QLabel#bigTime { font-family: "IBM Plex Mono"; font-size: 34px; font-weight: 500; }
QLabel#bigTime[onair="true"] { color: #f5a524; }
QLabel#clock { font-family: "IBM Plex Mono"; font-size: 32px; font-weight: 500; }
QLabel#intro { font-family: "IBM Plex Mono"; font-size: 13px; font-weight: 500; color: #8cc8ff; min-height: 18px; }
QLabel#intro[live="true"] { font-size: 15px; font-weight: 600; color: #121418; background: #5cb0ff; border-radius: 3px; padding: 0 6px; }
QLabel#mono { font-family: "IBM Plex Mono"; color: #c9ced6; }
QLabel#badge1 { font-family: "IBM Plex Mono"; font-weight: 600; background: #f5a524; color: #121418; border-radius: 3px; padding: 1px 7px; }
QLabel#badge2 { font-family: "IBM Plex Mono"; font-weight: 600; background: #5cb0ff; color: #121418; border-radius: 3px; padding: 1px 7px; }
QLabel#badgeP { font-family: "IBM Plex Mono"; font-weight: 600; background: #3a4150; color: #e8eaed; border-radius: 3px; padding: 1px 7px; }

QLabel#pill { font-size: 11px; font-weight: 600; letter-spacing: 1px; border-radius: 9px; padding: 1px 9px; border: 1px solid #454b57; color: #9aa3af; }
QLabel#pill[state="ready"] { border: 1px solid #5cb0ff; color: #bfe0ff; }
QLabel#pill[state="paused"] { border: 1px solid #f5a524; color: #ffd58a; }
QLabel#pill[state="onair"] { border: 1px solid #f5a524; background: #f5a524; color: #121418; }

QPushButton { background: #23272e; border: 1px solid #30353e; border-radius: 4px; padding: 0 12px; min-height: 28px; color: #e8eaed; }
QPushButton:hover:enabled { background: #2b3038; border-color: #454b57; }
QPushButton:pressed { background: #1f232a; }
QPushButton:disabled { color: #6b7380; }
QPushButton[transport="true"] { min-width: 44px; max-width: 44px; min-height: 40px; padding: 0; }
QPushButton[tall="true"] { min-height: 40px; }
QPushButton[accent="true"] { background: #f5a524; border: 0; color: #121418; font-weight: 600; }
QPushButton[accent="true"]:hover:enabled { background: #ffb53d; }
QPushButton[accent="true"]:disabled { background: #5a4313; color: #121418; }
QPushButton[lit="true"], QPushButton[lit="true"]:disabled { background: #f5a524; border: 0; }
QPushButton[danger="true"] { color: #ff8f85; font-weight: 600; }
QPushButton[active="true"] { background: #14283d; border: 1px solid #5cb0ff; color: #bfe0ff; }
QPushButton#nextButton { font-size: 16px; min-height: 56px; }

QHeaderView::section { background: #1b1e24; color: #9aa3af; border: 0; border-bottom: 1px solid #30353e; padding: 6px 8px; font-size: 11px; font-weight: 500; }
QTableView { background: #1b1e24; border: 0; gridline-color: transparent; selection-background-color: #2e333c; selection-color: #e8eaed; outline: 0; }
QTableCornerButton::section { background: #1b1e24; border: 0; }

QProgressBar { background: #121418; border: 0; border-radius: 2px; max-height: 4px; min-height: 4px; }
QProgressBar::chunk { background: #5cb0ff; border-radius: 2px; }
)";

void apply(QApplication &app)
{
    for (const char *f : {":/fonts/IBMPlexSans-Regular.ttf", ":/fonts/IBMPlexSans-Medium.ttf",
                          ":/fonts/IBMPlexSans-SemiBold.ttf", ":/fonts/IBMPlexMono-Regular.ttf",
                          ":/fonts/IBMPlexMono-Medium.ttf", ":/fonts/IBMPlexMono-SemiBold.ttf"})
        QFontDatabase::addApplicationFont(f);

    QFont font("IBM Plex Sans");
    font.setPixelSize(13);
    app.setFont(font);

    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, panel);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::AlternateBase, QColor("#16181d"));
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, panel2);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::ToolTipBase, panel2);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, QColor("#6b7380"));
    p.setColor(QPalette::Highlight, QColor("#3a4150"));
    p.setColor(QPalette::HighlightedText, text);
    p.setColor(QPalette::Link, blue);
    p.setColor(QPalette::Light, QColor("#3a4150"));
    p.setColor(QPalette::Midlight, QColor("#30353e"));
    p.setColor(QPalette::Mid, QColor("#30353e"));
    p.setColor(QPalette::Dark, QColor("#16181d"));
    p.setColor(QPalette::Shadow, QColor("#0c0d10"));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor("#6b7380"));
    app.setPalette(p);

    // Jelölőnégyzet: a pipa képét futás közben rajzoljuk meg, hogy sötét háttéren is jól látsszon.
    QPixmap check(32, 32);
    check.fill(Qt::transparent);
    {
        QPainter cp(&check);
        cp.setRenderHint(QPainter::Antialiasing);
        cp.setPen(QPen(bg, 4.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        QPainterPath path;
        path.moveTo(7, 17);
        path.lineTo(13.5, 23);
        path.lineTo(25, 9.5);
        cp.drawPath(path);
    }
    const QString checkPath = QDir::temp().filePath(QStringLiteral("eter-check-%1.png").arg(QCoreApplication::applicationPid()));
    QString qss = QString::fromUtf8(kQss);
    if (check.save(checkPath))
        qss += QStringLiteral("QCheckBox::indicator:checked { image: url(%1); }").arg(checkPath);
    app.setStyleSheet(qss);
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
