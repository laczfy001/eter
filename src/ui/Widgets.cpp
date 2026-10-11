#include "i18n.h"
#include "Widgets.h"
#include "Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

WaveformWidget::WaveformWidget(QWidget *parent) : QWidget(parent), played_(theme::amber)
{
    setMinimumHeight(44);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setCursor(Qt::PointingHandCursor);
}

void WaveformWidget::setPeaks(const QVector<float> &peaks)
{
    peaks_ = peaks;
    loading_ = false;
    update();
}

void WaveformWidget::clear()
{
    peaks_.clear();
    progress_ = 0.0;
    loading_ = false;
    update();
}

void WaveformWidget::setProgress(double fraction)
{
    fraction = std::clamp(fraction, 0.0, 1.0);
    if (std::abs(fraction - progress_) * width() < 0.5) return;
    progress_ = fraction;
    update();
}

void WaveformWidget::setMarker(double fraction)
{
    if (std::abs(fraction - marker_) < 1e-4) return;
    marker_ = fraction;
    update();
}

void WaveformWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(theme::bg);
    p.drawRoundedRect(rect(), 4, 4);

    const int w = width(), h = height();
    if (peaks_.isEmpty()) {
        if (loading_) {
            p.setPen(theme::muted);
            p.drawText(rect(), Qt::AlignCenter, TR("Hullámforma készül…"));
        }
        return;
    }

    p.setRenderHint(QPainter::Antialiasing, false);
    const int step = 3;                       // 2 px oszlop, 1 px köz
    const int cols = std::max(1, w / step);
    const int playedX = static_cast<int>(progress_ * w);
    const double mid = h / 2.0;
    for (int c = 0; c < cols; ++c) {
        const int from = static_cast<int>(static_cast<qint64>(c) * peaks_.size() / cols);
        const int to = std::max(from + 1, static_cast<int>(static_cast<qint64>(c + 1) * peaks_.size() / cols));
        float v = 0.f;
        for (int i = from; i < to && i < peaks_.size(); ++i) v = std::max(v, peaks_[i]);
        // Enyhe gyökös skála, hogy a halk részek is látszódjanak.
        const double amp = std::sqrt(std::min(1.f, v)) * (mid - 3);
        const double half = std::max(1.0, amp);
        const int x = c * step;
        p.fillRect(QRectF(x, mid - half, 2, half * 2), x < playedX ? played_ : theme::waveIdle);
    }
    if (marker_ > 0.0 && marker_ < 1.0) {
        // Intro vége: kék vonal kis zászlóval
        const int mx = static_cast<int>(marker_ * w);
        p.fillRect(QRect(mx, 0, 1, h), theme::blue);
        p.fillRect(QRect(mx, 0, 5, 5), theme::blue);
    }
    p.fillRect(QRect(std::clamp(playedX - 1, 0, w - 2), 0, 2, h), Qt::white);
}

void WaveformWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && !peaks_.isEmpty() && width() > 0)
        emit seekRequested(std::clamp(e->position().x() / width(), 0.0, 1.0));
}

void WaveformWidget::mouseMoveEvent(QMouseEvent *e)
{
    if ((e->buttons() & Qt::LeftButton) && !peaks_.isEmpty() && width() > 0)
        emit seekRequested(std::clamp(e->position().x() / width(), 0.0, 1.0));
}

MeterWidget::MeterWidget(int barHeight, QWidget *parent) : QWidget(parent), bar_(barHeight)
{
    setFixedHeight(bar_ * 2 + 3);
    setMinimumWidth(80);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void MeterWidget::feed(float left, float right)
{
    // Kb. 30 ms-onként hívódik: gyors felfutás, lassú visszaesés.
    const float decay = 0.86f;
    const float nl = std::max(left, l_ * decay);
    const float nr = std::max(right, r_ * decay);
    if (std::abs(nl - l_) < 1e-4f && std::abs(nr - r_) < 1e-4f) return;
    l_ = nl < 1e-4f ? 0.f : nl;
    r_ = nr < 1e-4f ? 0.f : nr;
    update();
}

double MeterWidget::levelDb() const
{
    const float v = std::max(l_, r_);
    return v > 1e-4f ? 20.0 * std::log10(v) : -100.0;
}

void MeterWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const int w = width();
    auto frac = [](float v) {
        if (v <= 1e-4f) return 0.0;
        const double db = 20.0 * std::log10(v);
        return std::clamp((db + 60.0) / 60.0, 0.0, 1.0);   // −60 … 0 dB
    };
    const double hot = (60.0 - 3.0) / 60.0;               // −3 dB fölött piros
    int y = 0;
    for (float v : {l_, r_}) {
        p.setBrush(theme::bg);
        p.drawRoundedRect(QRectF(0, y, w, bar_), 2, 2);
        const double f = frac(v);
        if (f > 0) {
            p.setBrush(theme::green);
            p.drawRoundedRect(QRectF(0, y, w * std::min(f, hot), bar_), 2, 2);
            if (f > hot) {
                p.setBrush(theme::red);
                p.drawRect(QRectF(w * hot, y, w * (f - hot), bar_));
            }
        }
        y += bar_ + 3;
    }
}
