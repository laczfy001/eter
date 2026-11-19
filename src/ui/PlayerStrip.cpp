#include "PlayerStrip.h"
#include "i18n.h"

#include <QMouseEvent>
#include <QPainter>

static QColor mixc(const QColor &a, const QColor &b, double t)
{
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t);
}

static const QColor kVt("#f472b6");

PlayerStrip::PlayerStrip(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(50);
    setMouseTracking(false);
    setAccessibleName(TR("Lejátszók állapota"));
}

void PlayerStrip::setTiles(const QVector<Tile> &tiles)
{
    bool same = tiles.size() == tiles_.size();
    for (int i = 0; same && i < tiles.size(); ++i) {
        const Tile &a = tiles[i], &b = tiles_[i];
        same = a.state == b.state && a.title == b.title && a.time == b.time && a.output == b.output && a.shown == b.shown;
    }
    if (same) return;
    tiles_ = tiles;
    QStringList tip;
    for (const Tile &t : tiles_) tip << QStringLiteral("%1: %2").arg(t.letter, t.title.isEmpty() ? TR("üres") : t.title);
    setToolTip(tip.join('\n') + TR("\nKattints egy lejátszóra, hogy fölötte az látszódjon."));
    update();
}

void PlayerStrip::setVoiceTrack(const QString &title, const QString &state, const QString &time)
{
    if (title == vtTitle_ && state == vtState_ && time == vtTime_) return;
    vtTitle_ = title;
    vtState_ = state;
    vtTime_ = time;
    update();
}

QRect PlayerStrip::tileRect(int i) const
{
    const int n = std::max(1, count()), gap = 8;
    const int w = (width() - gap * (n - 1)) / n;
    return QRect(i * (w + gap), 0, w, height() - 1);
}

static QString stateText(PlayerStrip::State s)
{
    switch (s) {
    case PlayerStrip::OnAir: return TR("ADÁSBAN");
    case PlayerStrip::Leaving: return TR("KIÚSZIK");
    case PlayerStrip::Sounding: return TR("SZÓL");
    case PlayerStrip::Ready: return TR("KÉSZENLÉT");
    case PlayerStrip::Loaded: return TR("BETÖLTVE");
    case PlayerStrip::Paused: return TR("SZÜNET");
    case PlayerStrip::Empty: break;
    }
    return TR("ÜRES");
}

void PlayerStrip::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QColor panel("#1f2227"), border("#2c3038"), text("#e6e8eb"), muted("#9aa3af");
    QFont small = font();
    small.setPixelSize(10);
    small.setWeight(QFont::Bold);
    small.setLetterSpacing(QFont::PercentageSpacing, 108);
    QFont body = font();
    body.setPixelSize(13);
    QFont badge = font();
    badge.setPixelSize(14);
    badge.setWeight(QFont::Bold);
    QFont mono("IBM Plex Mono");
    mono.setPixelSize(13);

    auto drawTile = [&](const QRect &r, const QString &letter, const QColor &c, bool lit, bool shown, const QString &state,
                        const QString &extra, const QString &title, const QString &time, bool dim) {
        p.setPen(QPen(lit ? c : shown ? mixc(border, c, 0.6) : border, 1));
        p.setBrush(lit ? mixc(panel, c, 0.16) : panel);
        p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        const QRect b(r.left() + 10, r.center().y() - 13, 26, 26);
        if (lit) {
            p.setPen(Qt::NoPen);
            p.setBrush(c);
        } else {
            p.setPen(QPen(c, 1));
            p.setBrush(Qt::NoBrush);
        }
        p.drawRoundedRect(QRectF(b).adjusted(0.5, 0.5, -0.5, -0.5), 5, 5);
        p.setFont(badge);
        p.setPen(lit ? QColor("#10131a") : c);
        p.drawText(b, Qt::AlignCenter, letter);
        const int tx = b.right() + 10;
        p.setFont(mono);
        const int timeW = time.isEmpty() ? 0 : QFontMetrics(mono).horizontalAdvance(time) + 10;
        p.setPen(dim ? muted : text);
        p.drawText(QRect(r.right() - timeW - 2, r.top(), timeW, r.height()), Qt::AlignVCenter | Qt::AlignRight, time);
        const int tw = std::max(10, r.right() - timeW - tx - 6);
        p.setFont(small);
        p.setPen(lit ? c : QColor("#c9cdd3"));
        const QString head = extra.isEmpty() ? state : state + QStringLiteral("  ·  ") + extra;
        p.drawText(QRect(tx, r.top() + 6, tw, 14), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(small).elidedText(head, Qt::ElideRight, tw));
        p.setFont(body);
        p.setPen(dim ? muted : text);
        p.drawText(QRect(tx, r.top() + 22, tw, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(body).elidedText(title.isEmpty() ? QStringLiteral("—") : title, Qt::ElideRight, tw));
    };

    for (int i = 0; i < tiles_.size(); ++i) {
        const Tile &t = tiles_[i];
        drawTile(tileRect(i), t.letter, t.color, t.state == OnAir, t.shown, stateText(t.state), t.output, t.title, t.time,
                 t.state == Empty);
    }
    if (!vtTitle_.isEmpty())
        drawTile(tileRect(tiles_.size()), QStringLiteral("VT"), kVt, true, false, vtState_, {}, vtTitle_, vtTime_, false);
}

void PlayerStrip::mousePressEvent(QMouseEvent *e)
{
    for (int i = 0; i < tiles_.size(); ++i)
        if (tileRect(i).contains(e->position().toPoint())) {
            emit tileClicked(i);
            return;
        }
}

QString PlayerStrip::debugText() const
{
    QStringList parts;
    for (const Tile &t : tiles_) parts << QStringLiteral("%1:%2%3").arg(t.letter, stateText(t.state), t.shown ? "*" : "");
    if (!vtTitle_.isEmpty()) parts << QStringLiteral("VT:%1").arg(vtTitle_);
    return parts.join(' ');
}
