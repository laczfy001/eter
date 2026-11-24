#include "i18n.h"
#include "ChannelMixer.h"

#include <QContextMenuEvent>
#include <QHelpEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

static constexpr double kLabelH = 16, kDbH = 14, kMuteH = 16, kGap = 3;

ChannelMixer::ChannelMixer(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    QSettings s;
    for (int i = 0; i < Count; ++i)
        strips_[i].db = std::clamp(s.value(QStringLiteral("mixer/db%1").arg(i), 0.0).toDouble(), kMinDb - 1, kMaxDb);
}

QString ChannelMixer::label(int i)
{
    static const char *l[] = {"A", "B", "C", "VT", "CART", "KI"};
    return i == Out ? TR("KI") : QString::fromLatin1(l[i]);
}

QString ChannelMixer::name(int i)
{
    switch (i) {
    case A:
    case B:
    case C: return TR("%1 lejátszó").arg(label(i));
    case Vt: return TR("Voice track");
    case Cart: return TR("Cartwall");
    default: return TR("Fő kimenet");
    }
}

QColor ChannelMixer::color(int i)
{
    static const QColor c[] = {QColor("#f5a524"), QColor("#5cb0ff"), QColor("#3fd0b5"), QColor("#c79bff"), QColor("#ff8f85"),
                               QColor("#e8eaed")};
    return c[std::clamp(i, 0, int(Count) - 1)];
}

double ChannelMixer::posOf(double db)
{
    if (db < kMinDb) return 0.0;
    if (db < -40.0) return (db - kMinDb) / 20.0 * 0.15;
    return 0.15 + (std::min(db, kMaxDb) + 40.0) / (kMaxDb + 40.0) * 0.85;
}

double ChannelMixer::dbOfPos(double p)
{
    if (p <= 0.005) return kMinDb - 1;   // −∞
    if (p < 0.15) return kMinDb + p / 0.15 * 20.0;
    return -40.0 + (std::min(p, 1.0) - 0.15) / 0.85 * (kMaxDb + 40.0);
}

float ChannelMixer::gain(int strip) const
{
    const S &s = strips_[strip];
    if (s.muted || s.db < kMinDb) return 0.f;
    return float(std::pow(10.0, s.db / 20.0));
}

void ChannelMixer::store(int strip)
{
    QSettings().setValue(QStringLiteral("mixer/db%1").arg(strip), strips_[strip].db);
}

void ChannelMixer::setDb(int strip, double db)
{
    db = std::clamp(db, kMinDb - 1, kMaxDb);
    if (db < kMinDb) db = kMinDb - 1;
    if (std::abs(strips_[strip].db - db) < 1e-6) return;
    strips_[strip].db = db;
    store(strip);
    update();
    emit changed();
}

void ChannelMixer::setMuted(int strip, bool on)
{
    if (strips_[strip].muted == on) return;
    strips_[strip].muted = on;
    update();
    emit changed();
}

void ChannelMixer::toggleMute(int strip)
{
    const bool on = !strips_[strip].muted;
    if (on && confirmMute && !confirmMute(strip)) return;
    setMuted(strip, on);
}

void ChannelMixer::setOnAir(int strip, bool on)
{
    if (strips_[strip].onAir == on) return;
    strips_[strip].onAir = on;
    update();
}

void ChannelMixer::resetAll()
{
    for (int i = 0; i < Count; ++i) {
        strips_[i].db = 0.0;
        strips_[i].muted = false;
        store(i);
    }
    update();
    emit changed();
}

void ChannelMixer::feed(int strip, float peakL, float peakR)
{
    S &s = strips_[strip];
    auto toDb = [](float v) { return v > 1e-5f ? 20.0 * std::log10(double(v)) : -100.0; };
    const double l = toDb(peakL), r = toDb(peakR);
    if (peakL > 1.0f || peakR > 1.0f) s.clip = true;
    // Gyors felfutás, kb. 25 dB/mp esés (30 ms-onként 0,75 dB).
    s.levelL = std::max(l, s.levelL - 0.75);
    s.levelR = std::max(r, s.levelR - 0.75);
    const double top = std::max(l, r);
    if (top >= s.holdDb) {
        s.holdDb = top;
        s.holdTicks = 66;   // kb. 2 mp
    } else if (--s.holdTicks <= 0) {
        s.holdDb = std::max(-100.0, s.holdDb - 0.75);
    }
    update(stripRect(strip).toAlignedRect());
}

QRectF ChannelMixer::stripRect(int i) const
{
    const double w = double(width()) / Count;
    return QRectF(i * w, 0, w, height());
}

QRectF ChannelMixer::meterRect(int i) const
{
    const QRectF s = stripRect(i);
    return QRectF(s.left() + 3, kLabelH + kGap + 4, s.width() - 6, height() - kLabelH - kDbH - kMuteH - 3 * kGap - 4);
}

QRectF ChannelMixer::clipRect(int i) const
{
    const QRectF m = meterRect(i);
    return QRectF(m.center().x() - 7, m.top() - 5, 14, 4);
}

QRectF ChannelMixer::muteRect(int i) const
{
    const QRectF s = stripRect(i);
    const double w = std::min(30.0, s.width() - 8);
    return QRectF(s.center().x() - w / 2, height() - kMuteH, w, kMuteH);
}

double ChannelMixer::yOf(const QRectF &m, double db) const
{
    return m.bottom() - posOf(db) * m.height();
}

double ChannelMixer::dbAt(const QRectF &m, double y) const
{
    return dbOfPos((m.bottom() - y) / m.height());
}

int ChannelMixer::stripAt(const QPoint &p) const
{
    if (width() <= 0) return -1;
    return std::clamp(int(p.x() * Count / width()), 0, int(Count) - 1);
}

static QString dbText(double db)
{
    if (db < ChannelMixer::kMinDb) return QStringLiteral("−∞");
    QString t = QString::number(db, 'f', 1);
    if (t == QLatin1String("-0.0")) t = QStringLiteral("0.0");
    return t.replace(QLatin1Char('-'), QStringLiteral("−"));
}

void ChannelMixer::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont f = font();
    for (int i = 0; i < Count; ++i) {
        const S &s = strips_[i];
        const QRectF sr = stripRect(i);
        if (i == Out) p.fillRect(sr.adjusted(1, 0, -1, 0), QColor("#22262e"));
        // Címke (az adásban lévő lejátszóé kiemelve; némítva áthúzva)
        f.setPixelSize(11);
        f.setBold(true);
        f.setStrikeOut(s.muted);
        p.setFont(f);
        const QRectF lr(sr.left(), 0, sr.width(), kLabelH);
        // Van jel a csatornán (−50 dB fölött): a betűjel zöld kitöltéssel, sötét betűvel.
        const bool signal = std::max(s.levelL, s.levelR) > -50.0;
        if (signal) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#4cc38a"));
            p.drawRoundedRect(lr.adjusted(4, 1, -4, -1), 3, 3);
        }
        p.setPen(signal ? QColor("#101215") : s.muted ? QColor("#6b727c") : color(i));
        p.drawText(lr, Qt::AlignCenter, label(i));
        f.setStrikeOut(false);
        // Jelszint: két sáv, a 0 dB fölött sárga, −6 fölött borostyán
        const QRectF m = meterRect(i);
        const double bw = std::max(3.0, std::min(6.0, m.width() / 6));
        const double cx = m.center().x();
        for (int ch = 0; ch < 2; ++ch) {
            const QRectF bar(cx - bw - 1.5 + ch * (bw + 3), m.top(), bw, m.height());
            p.setPen(Qt::NoPen);
            p.setBrush(QColor("#0b0c0f"));
            p.drawRoundedRect(bar, 1.5, 1.5);
            const double lv = ch == 0 ? s.levelL : s.levelR;
            const double y = yOf(m, lv);
            if (y < bar.bottom() - 0.5) {
                QLinearGradient g(bar.bottomLeft(), bar.topLeft());
                g.setColorAt(0.0, QColor("#3fd0b5"));
                g.setColorAt(posOf(-12), QColor("#3fd0b5"));
                g.setColorAt(posOf(-6), QColor("#f5a524"));
                g.setColorAt(posOf(0), QColor("#ff5a4f"));
                g.setColorAt(1.0, QColor("#ff5a4f"));
                p.setBrush(g);
                p.drawRect(QRectF(bar.left(), y, bar.width(), bar.bottom() - y));
            }
            if (s.holdDb > -90) {
                const double hy = yOf(m, s.holdDb);
                p.fillRect(QRectF(bar.left(), hy - 1, bar.width(), 2), QColor("#ffd58a"));
            }
        }
        // Túlvezérlés-jelző (kattintásra törlődik)
        p.setPen(Qt::NoPen);
        p.setBrush(s.clip ? QColor("#ff5a4f") : QColor("#2a2d33"));
        p.drawRoundedRect(clipRect(i), 1.5, 1.5);
        // 0 dB jel és a fader
        const double y0 = yOf(m, 0.0);
        p.setPen(QPen(QColor("#6b727c"), 1));
        p.drawLine(QPointF(m.left(), y0), QPointF(m.left() + 3, y0));
        p.drawLine(QPointF(m.right() - 3, y0), QPointF(m.right(), y0));
        const double fy = yOf(m, s.db);
        const QRectF thumb(m.left() + 1, fy - 4, m.width() - 2, 8);
        p.setPen(QPen(QColor(0, 0, 0, 140), 1));
        p.setBrush(s.muted ? QColor("#8a919b") : QColor("#e8eaed"));
        p.drawRoundedRect(thumb, 2, 2);
        p.setPen(QPen(QColor("#111317"), 1));
        p.drawLine(QPointF(thumb.left() + thumb.width() * 0.25, fy), QPointF(thumb.right() - thumb.width() * 0.25, fy));
        // dB érték
        f.setBold(false);
        f.setPixelSize(10);
        p.setFont(f);
        p.setPen(std::abs(s.db) < 0.05 ? QColor("#9aa1ab") : QColor("#ffd58a"));
        p.drawText(QRectF(sr.left(), m.bottom() + kGap, sr.width(), kDbH), Qt::AlignCenter, dbText(s.db));
        // Némítás gomb
        const QRectF mr = muteRect(i);
        p.setPen(QPen(s.muted ? QColor("#ff8f85") : QColor("#3a404b"), 1));
        p.setBrush(s.muted ? QColor("#5a1f1b") : QColor("#22262e"));
        p.drawRoundedRect(mr, 3, 3);
        f.setBold(true);
        f.setPixelSize(9);
        p.setFont(f);
        p.setPen(s.muted ? QColor("#ffb4ae") : QColor("#9aa1ab"));
        p.drawText(mr, Qt::AlignCenter, QStringLiteral("M"));
    }
}

void ChannelMixer::mousePressEvent(QMouseEvent *e)
{
    const int i = stripAt(e->position().toPoint());
    if (i < 0 || e->button() != Qt::LeftButton) return;
    const QPointF pt = e->position();
    if (muteRect(i).adjusted(-2, -2, 2, 2).contains(pt)) {
        toggleMute(i);
        return;
    }
    if (clipRect(i).adjusted(-4, -3, 4, 3).contains(pt)) {
        strips_[i].clip = false;
        strips_[i].holdDb = -100;
        update();
        return;
    }
    const QRectF m = meterRect(i);
    const double fy = yOf(m, strips_[i].db);
    // Csak a fader gombját lehet megfogni (adás közben egy véletlen kattintás ne ugrassa a szintet).
    if (m.adjusted(-2, -6, 2, 6).contains(pt) && std::abs(pt.y() - fy) <= 7) {
        drag_ = i;
        dragOffset_ = pt.y() - fy;
        setCursor(Qt::ClosedHandCursor);
    }
}

void ChannelMixer::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pt = e->position();
    if (drag_ < 0) {
        const int i = stripAt(pt.toPoint());
        const bool overThumb = i >= 0 && std::abs(pt.y() - yOf(meterRect(i), strips_[i].db)) <= 7 && meterRect(i).adjusted(-2, -6, 2, 6).contains(pt);
        setCursor(overThumb ? Qt::OpenHandCursor : Qt::ArrowCursor);
        return;
    }
    double db = dbAt(meterRect(drag_), pt.y() - dragOffset_);
    if (std::abs(db) < 0.75) db = 0.0;   // 0 dB-nél „megakad”
    else if (db >= kMinDb) db = std::round(db * 10.0) / 10.0;
    setDb(drag_, db);
}

void ChannelMixer::mouseReleaseEvent(QMouseEvent *)
{
    if (drag_ >= 0) setCursor(Qt::OpenHandCursor);
    drag_ = -1;
}

void ChannelMixer::mouseDoubleClickEvent(QMouseEvent *e)
{
    const int i = stripAt(e->position().toPoint());
    if (i >= 0 && meterRect(i).adjusted(-2, -6, 2, 6).contains(e->position())) setDb(i, 0.0);
}

void ChannelMixer::wheelEvent(QWheelEvent *e)
{
    const int i = stripAt(e->position().toPoint());
    if (i < 0) return;
    const int delta = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
    if (delta == 0) return;
    const double step = (e->modifiers() & Qt::ShiftModifier) ? 0.1 : 1.0;
    double db = strips_[i].db < kMinDb ? kMinDb : strips_[i].db;
    db += delta > 0 ? step : -step;
    db = std::round(db * 10.0) / 10.0;
    setDb(i, db < kMinDb ? kMinDb - 1 : db);
    e->accept();
}

void ChannelMixer::contextMenuEvent(QContextMenuEvent *e)
{
    const int i = stripAt(e->pos());
    if (i < 0) return;
    QMenu menu(this);
    menu.addSection(name(i));
    menu.addAction(TR("0 dB"), this, [this, i] { setDb(i, 0.0); });
    menu.addAction(strips_[i].muted ? TR("Némítás feloldása") : TR("Némítás"), this, [this, i] { toggleMute(i); });
    menu.addSeparator();
    menu.addAction(TR("Minden csatorna vissza 0 dB-re"), this, [this] { resetAll(); });
    menu.exec(e->globalPos());
}

bool ChannelMixer::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *h = static_cast<QHelpEvent *>(e);
        const int i = stripAt(h->pos());
        if (i >= 0) {
            QToolTip::showText(h->globalPos(),
                               TR("%1: %2 dB%3\nHúzd a fadert · görgő ±1 dB (Shift: ±0,1) · dupla kattintás: 0 dB · jobb klikk: menü")
                                   .arg(name(i), dbText(strips_[i].db), strips_[i].muted ? TR(" · némítva") : QString()),
                               this);
            return true;
        }
    }
    return QWidget::event(e);
}

QString ChannelMixer::debugText() const
{
    QStringList parts;
    for (int i = 0; i < Count; ++i)
        parts << QStringLiteral("%1=%2%3").arg(label(i), dbText(strips_[i].db), strips_[i].muted ? QStringLiteral("(M)") : QString());
    return parts.join(QLatin1Char(' '));
}
