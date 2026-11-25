#include "i18n.h"
#include "Widgets.h"
#include <QEvent>
#include "Theme.h"

#include <QMouseEvent>
#include <QHeaderView>
#include <QPainter>
#include <QSet>
#include <QSettings>
#include <QTableView>
#include <QTimer>
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
    message_.clear();
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
    if (peaks_.isEmpty() && !message_.isEmpty()) {
        QColor fill = played_;
        fill.setAlphaF(0.35);
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(0, 0, progress_ * w, h), 4, 4);
        p.setPen(theme::text);
        p.drawText(rect().adjusted(8, 0, -8, 0), Qt::AlignCenter | Qt::TextSingleLine,
                   fontMetrics().elidedText(message_, Qt::ElideRight, w - 16));
        return;
    }
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
    if (mixMarker_ > 0.0 && mixMarker_ < 1.0) {
        const int mx = static_cast<int>(mixMarker_ * w);
        p.fillRect(QRect(mx, 0, 2, h), mixColor_);
        if (!mixLabel_.isEmpty()) {
            QFont f = font();
            f.setPixelSize(10);
            f.setWeight(QFont::Bold);
            p.setFont(f);
            const int tw = QFontMetrics(f).horizontalAdvance(mixLabel_) + 8;
            // A jel bal oldalára; ha ott nem fér el, a jobb oldalára.
            const QRect box(mx - tw - 2 >= 0 ? mx - tw - 2 : std::min(mx + 4, w - tw), 1, tw, 14);
            p.fillRect(box, theme::bg);
            p.setPen(mixColor_);
            p.drawText(box, Qt::AlignCenter, mixLabel_);
        }
    }
    p.fillRect(QRect(std::clamp(playedX - 1, 0, w - 2), 0, 2, h), theme::c(QColor(Qt::white)));
}

void WaveformWidget::setMixMarker(double fraction, const QString &label, const QColor &color)
{
    if (qFuzzyCompare(fraction + 2.0, mixMarker_ + 2.0) && label == mixLabel_ && color == mixColor_) return;
    mixMarker_ = fraction;
    mixLabel_ = label;
    mixColor_ = color;
    update();
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

// ------------------------------------------------------- visszaszámláló

CountdownOverlay::CountdownOverlay(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setFocusPolicy(Qt::NoFocus);
    setAccessibleName(QStringLiteral("countdown"));
    hide();
    parent->installEventFilter(this);
}

bool CountdownOverlay::eventFilter(QObject *o, QEvent *e)
{
    if (o == parentWidget() && e->type() == QEvent::Resize) place();
    return false;
}

void CountdownOverlay::place()
{
    QWidget *p = parentWidget();
    const int w = std::min(560, std::max(260, p->width() - 32));
    const int h = 92;
    setGeometry((p->width() - w) / 2, p->height() - h - 18, w, h);
}

void CountdownOverlay::showCount(int seconds, double fraction, const QString &label)
{
    if (seconds <= 0) {
        if (isVisible()) hide();
        seconds_ = 0;
        return;
    }
    const bool changed = seconds != seconds_ || label != label_ || std::abs(fraction - fraction_) > 0.04;
    seconds_ = seconds;
    fraction_ = fraction;
    label_ = label;
    if (!isVisible()) {
        place();
        show();
        raise();
    }
    setProperty("debugText", QStringLiteral("%1 | %2").arg(seconds).arg(label));
    if (changed) update();
}

void CountdownOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);
    // Minden másodperc elején erősebb, utána halványul: a szám „üt”, de nem villog zavaróan.
    const double pulse = 0.72 + 0.28 * std::clamp(fraction_, 0.0, 1.0);
    QColor bg(150, 18, 24);
    bg.setAlphaF(0.82 * pulse + 0.1);
    p.setPen(QPen(QColor(255, 120, 110, 220), 2));
    p.setBrush(bg);
    p.drawRoundedRect(r, 14, 14);

    QFont big(QStringLiteral("IBM Plex Mono"));
    big.setPixelSize(62);
    big.setWeight(QFont::Bold);
    p.setFont(big);
    p.setPen(Qt::white);
    const QRectF numBox(r.left() + 18, r.top(), 96, r.height());
    p.drawText(numBox, Qt::AlignCenter, QString::number(seconds_));

    QFont small = font();
    small.setPixelSize(15);
    small.setWeight(QFont::DemiBold);
    p.setFont(small);
    p.setPen(QColor(255, 225, 222));
    const QRectF textBox(numBox.right() + 12, r.top() + 10, r.right() - numBox.right() - 28, r.height() - 20);
    p.drawText(textBox, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap,
               QFontMetrics(small).elidedText(label_, Qt::ElideRight, int(textBox.width() * 2 - 20)));
}

// ---- Resizable, remembered columns ------------------------------------------------------------------------------

namespace {

class ColumnFitter : public QObject {
public:
    ColumnFitter(QTableView *view, const QString &key) : QObject(view->horizontalHeader()), view_(view), key_(key)
    {
        QHeaderView *h = view->horizontalHeader();
        if (h->stretchLastSection() && h->count() > 0) flex_.insert(h->count() - 1);
        h->setStretchLastSection(false);
        for (int c = 0; c < h->count(); ++c) {
            const auto mode = h->sectionResizeMode(c);
            if (mode == QHeaderView::Stretch) flex_.insert(c);
            if (mode == QHeaderView::ResizeToContents) toContents_.insert(c);
            h->setSectionResizeMode(c, QHeaderView::Interactive);
        }
        h->setMinimumSectionSize(28);
        QSettings s;
        const QStringList saved = s.value(QStringLiteral("ui/columns/") + key_).toStringList();
        if (saved.size() == h->count()) {
            restored_ = true;
            fitting_ = true;
            for (int c = 0; c < saved.size(); ++c)
                if (const int w = saved[c].toInt(); w >= 20) h->resizeSection(c, w);
            fitting_ = false;
        }
        saveTimer_.setSingleShot(true);
        saveTimer_.setInterval(400);
        connect(&saveTimer_, &QTimer::timeout, this, [this] { save(); });
        connect(h, &QHeaderView::sectionResized, this, [this](int logical, int, int) {
            if (fitting_) return;
            fit(logical);
            saveTimer_.start();
        });
        connect(h, &QHeaderView::sectionCountChanged, this, [this] { QTimer::singleShot(0, this, [this] { fit(-1); }); });
        viewport_ = view->viewport();
        viewport_->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        // Only the viewport's own pointer is compared: while the table is being destroyed it must not be asked anything.
        if ((e->type() == QEvent::Resize || e->type() == QEvent::Show) && o == viewport_) {
            if (!shown_ && view_->viewport()->width() > 0) {
                shown_ = true;
                // The columns sized to their contents get that width once, unless the user's own width is saved.
                if (!restored_) {
                    fitting_ = true;
                    for (int c : std::as_const(toContents_)) view_->resizeColumnToContents(c);
                    fitting_ = false;
                }
            }
            fit(-1);
        }
        return QObject::eventFilter(o, e);
    }

private:
    // The stretching columns (apart from the one just dragged) share what is left of the visible width.
    void fit(int exclude)
    {
        QHeaderView *h = view_->horizontalHeader();
        const int avail = view_->viewport()->width();
        if (avail <= 0 || fitting_) return;
        QList<int> cols;
        int used = 0;
        for (int c = 0; c < h->count(); ++c) {
            if (h->isSectionHidden(c)) continue;
            if (flex_.contains(c) && c != exclude) cols << c;
            else used += h->sectionSize(c);
        }
        if (cols.isEmpty()) return;
        const int minW = 110;      // a nyúló (cím, előadó…) oszlop ennél keskenyebb nem lesz: inkább görgethető a tábla
        const int target = std::max<int>(minW * cols.size(), avail - used);
        int current = 0;
        for (int c : cols) current += h->sectionSize(c);
        fitting_ = true;
        int left = target;
        for (int i = 0; i < cols.size(); ++i) {
            const int c = cols[i];
            int w = i == cols.size() - 1 ? left
                    : current > 0        ? int(qint64(h->sectionSize(c)) * target / current)
                                         : target / int(cols.size());
            w = std::max(minW, w);
            left -= w;
            if (h->sectionSize(c) != w) h->resizeSection(c, w);
        }
        fitting_ = false;
    }

    void save()
    {
        QHeaderView *h = view_->horizontalHeader();
        QStringList w;
        for (int c = 0; c < h->count(); ++c) w << QString::number(h->sectionSize(c));
        QSettings().setValue(QStringLiteral("ui/columns/") + key_, w);
    }

    QTableView *view_;
    QWidget *viewport_ = nullptr;
    QString key_;
    QSet<int> flex_, toContents_;
    QTimer saveTimer_;
    bool fitting_ = false;
    bool restored_ = false;
    bool shown_ = false;
};

} // namespace

void makeColumnsResizable(QTableView *view, const QString &key)
{
    if (!view || view->horizontalHeader()->property("eterColumns").isValid()) return;
    view->horizontalHeader()->setProperty("eterColumns", key);
    new ColumnFitter(view, key);
}
