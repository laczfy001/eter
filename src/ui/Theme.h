#pragma once
#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;

namespace theme {

inline const QColor bg("#121418");
inline const QColor panel("#1b1e24");
inline const QColor panel2("#23272e");
inline const QColor border("#30353e");
inline const QColor text("#e8eaed");
inline const QColor muted("#9aa3af");
inline const QColor amber("#f5a524");
inline const QColor amberSoft("#ffd58a");
inline const QColor blue("#5cb0ff");
inline const QColor blueSoft("#bfe0ff");
inline const QColor green("#45d483");
inline const QColor red("#ff8f85");
inline const QColor waveIdle("#4a5160");

enum class Icon { Play, Pause, Stop, Eject };

void apply(QApplication &app);
QIcon icon(Icon type, const QColor &color, bool keepWhenDisabled = false);
QIcon appIcon();

// 48 kHz-es képkockaszámból „p:mm" alakú idő
QString formatTime(qint64 frames);

} // namespace theme
