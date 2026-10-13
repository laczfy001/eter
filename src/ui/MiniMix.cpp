#include "MiniMix.h"
#include "i18n.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

static QString clockMs(qint64 ms)
{
    const QString sign = ms < 0 ? QStringLiteral("−") : QString();
    ms = std::llabs(ms);
    return sign + QStringLiteral("%1:%2.%3").arg(ms / 60000).arg(ms / 1000 % 60, 2, 10, QLatin1Char('0')).arg(ms / 100 % 10);
}

static const QColor kOutWave("#8fb8e6"), kInWave("#9fe0a8"), kMix("#ff8f85");

// ================================================================ rajzlap

MiniMixCanvas::MiniMixCanvas(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(92);
    setMouseTracking(true);
    setAccessibleName(TR("Átmenet: a következő elem a kimenő végére húzható"));
}

void MiniMixCanvas::setPair(const QString &outTitle, const QString &inTitle, const MixParams &p)
{
    outTitle_ = outTitle;
    inTitle_ = inTitle;
    p_ = p;
    if (!drag_) {
        mix_ = p.mixOutMs;
        // A nézet: a mix pont előtt 14, utána 10 másodperc.
        viewStart_ = mix_ - window_ * 14 / 24;
    }
    update();
}

void MiniMixCanvas::setWaves(const QVector<float> &outPeaks, qint64 outDur, const QVector<float> &inPeaks, qint64 inDur)
{
    outPeaks_ = outPeaks;
    inPeaks_ = inPeaks;
    if (outDur > 0) outDur_ = outDur;
    if (inDur > 0) inDur_ = inDur;
    update();
}

void MiniMixCanvas::setPlayhead(qint64 outMs)
{
    if (outMs == playhead_) return;
    playhead_ = outMs;
    update();
}

double MiniMixCanvas::xOf(qint64 outMs) const { return double(outMs - viewStart_) / double(window_) * width(); }

QRectF MiniMixCanvas::inRect() const
{
    const double mid = height() / 2.0;
    const qint64 len = inDur_ > 0 ? inDur_ - p_.cueInMs : window_;
    return QRectF(xOf(mix_), mid + 2, double(len) / window_ * width(), height() - mid - 16);
}

// Egy elem hullámformája a sávjában: `startX` képponttól, `fromMs`-től kezdve a saját idejében.
static void drawWave(QPainter &p, const QRectF &lane, const QVector<float> &peaks, qint64 dur, double startX, qint64 fromMs,
                     double pxPerMs, const QColor &c, double clipLeft, double clipRight)
{
    if (peaks.isEmpty() || dur <= 0) return;
    p.setPen(c);
    const double mid = lane.center().y(), half = lane.height() / 2.0 - 1;
    for (int x = int(std::max(clipLeft, startX)); x < int(clipRight); ++x) {
        const qint64 ms = fromMs + qint64((x - startX) / pxPerMs);
        if (ms < 0 || ms >= dur) continue;
        const qint64 ms2 = fromMs + qint64((x + 1 - startX) / pxPerMs);
        int a = int(ms * peaks.size() / dur), b = int(std::min<qint64>(dur, ms2) * peaks.size() / dur);
        b = std::max(a + 1, b);
        float v = 0.f;
        for (int i = a; i < b && i < peaks.size(); ++i) v = std::max(v, peaks[i]);
        const double h = std::max(0.5, double(v) * half);
        p.drawLine(QPointF(x + 0.5, mid - h), QPointF(x + 0.5, mid + h));
    }
}

void MiniMixCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    const QRectF r = rect();
    p.fillRect(r, QColor("#0b0c0f"));
    const double pxPerMs = double(width()) / double(window_);
    const double mid = r.height() / 2.0;
    const QRectF outLane(0, 14, r.width(), mid - 16);
    const QRectF inLane = inRect();

    // Időskála a kimenő elem idejében
    QFont mono("IBM Plex Mono");
    mono.setPixelSize(10);
    p.setFont(mono);
    p.setPen(QColor("#4a5160"));
    const qint64 first = (viewStart_ / 5000) * 5000;
    for (qint64 t = first; t < viewStart_ + window_; t += 5000) {
        if (t < 0) continue;
        const double x = xOf(t);
        p.drawLine(QPointF(x, 0), QPointF(x, 6));
        p.drawText(QPointF(x + 3, 10), QStringLiteral("%1:%2").arg(t / 60000).arg(t / 1000 % 60, 2, 10, QLatin1Char('0')));
    }

    // Kimenő elem: hullámforma, a vége utáni rész sötét, a kiúsztatás görbéje
    const double outEndX = xOf(p_.outEndMs > 0 ? p_.outEndMs : outDur_);
    drawWave(p, outLane, outPeaks_, outDur_, xOf(0), 0, pxPerMs, kOutWave, 0, std::min(r.width(), outEndX));
    drawWave(p, outLane, outPeaks_, outDur_, xOf(0), 0, pxPerMs, QColor("#39424f"), std::max(0.0, outEndX), r.width());
    if (p_.fadeOutMs > 0) {
        p.setRenderHint(QPainter::Antialiasing, true);
        const double x0 = xOf(p_.outEndMs - p_.fadeOutMs), x1 = outEndX;
        QPainterPath env;
        env.moveTo(x0, outLane.top() + 2);
        env.cubicTo((x0 + x1) / 2, outLane.top() + 2, (x0 + x1) / 2, outLane.bottom(), x1, outLane.bottom());
        p.setPen(QPen(QColor("#ffd58a"), 1.5));
        p.drawPath(env);
        p.setRenderHint(QPainter::Antialiasing, false);
    }
    p.setPen(QColor("#c9ced6"));
    QFont small = font();
    small.setPixelSize(11);
    p.setFont(small);
    p.drawText(QRectF(6, outLane.top(), std::max(0.0, xOf(mix_) - 12), 14), Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetrics(small).elidedText(outTitle_, Qt::ElideRight, int(std::max(0.0, xOf(mix_) - 12))));

    // Bejövő elem: a mix ponttól, a saját kezdőpontjától; húzható
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(drag_ ? QColor("#ffffff") : QColor("#3d6b46"), 1));
    p.setBrush(QColor(69, 212, 131, drag_ ? 46 : 26));
    p.drawRoundedRect(inLane, 3, 3);
    p.setRenderHint(QPainter::Antialiasing, false);
    drawWave(p, inLane.adjusted(0, 2, 0, -2), inPeaks_, inDur_, inLane.left(), p_.cueInMs, pxPerMs, kInWave, std::max(0.0, inLane.left()),
             std::min(r.width(), inLane.right()));
    p.setPen(QColor("#e8eaed"));
    p.drawText(QRectF(inLane.left() + 6, inLane.top(), 260, 14), Qt::AlignLeft | Qt::AlignVCenter, inTitle_);

    // Ráfektetett elem: a két sáv határán, a mix ponttal együtt mozog
    if (p_.ovlItemId) {
        const qint64 len = std::max<qint64>(p_.ovlDurMs, 500);
        const double x0 = xOf(mix_ + p_.ovlOffsetMs), x1 = xOf(mix_ + p_.ovlOffsetMs + len);
        const QRectF bar(x0, mid - 17, std::max(2.0, x1 - x0), 12);       // a kimenő sáv alján
        p.fillRect(bar, QColor(185, 156, 255, 170));
        p.fillRect(QRectF(x0 - 1, mid - 19, 2, 16), QColor("#b99cff"));
        QFont tiny = font();
        tiny.setPixelSize(10);
        tiny.setBold(true);
        p.setFont(tiny);
        p.setPen(QColor("#15121f"));
        p.drawText(bar.adjusted(4, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(tiny).elidedText(TR("RÁF  %1").arg(p_.ovlTitle), Qt::ElideRight, int(std::max(0.0, bar.width() - 6))));
        p.setFont(small);
    }

    // Mix pont
    const double mx = xOf(mix_);
    p.fillRect(QRectF(mx - 1, 12, 2, r.height() - 12), kMix);
    p.setFont(mono);
    const QString label = TR("MIX %1").arg(clockMs(mix_));
    const double lw = QFontMetrics(mono).horizontalAdvance(label) + 8;
    const QRectF box(mx + 4 + lw > r.width() ? mx - lw - 4 : mx + 4, r.height() - 14, lw, 13);
    p.fillRect(box, QColor("#3a1d1b"));
    p.setPen(kMix);
    p.drawText(box, Qt::AlignCenter, label);

    if (playhead_ >= 0) {
        const double x = xOf(playhead_);
        if (x >= 0 && x <= r.width()) p.fillRect(QRectF(x, 12, 1, mid), QColor("#ffffff"));
    }
}

void MiniMixCanvas::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    if (!inRect().adjusted(-4, -4, 4, 4).contains(e->position())) return;
    drag_ = true;
    dragX_ = e->position().x();
    dragMix_ = mix_;
    update();
}

void MiniMixCanvas::mouseMoveEvent(QMouseEvent *e)
{
    if (!drag_) {
        setCursor(inRect().contains(e->position()) ? Qt::SizeHorCursor : Qt::ArrowCursor);
        return;
    }
    const double dx = e->position().x() - dragX_;
    qint64 m = dragMix_ + qint64(dx / width() * window_);
    m = std::clamp<qint64>(m / 100 * 100, 0, outDur_ > 0 ? outDur_ : m);
    if (m != mix_) {
        mix_ = m;
        emit dragging(mix_);
        update();
    }
}

void MiniMixCanvas::mouseReleaseEvent(QMouseEvent *)
{
    if (!drag_) return;
    drag_ = false;
    update();
    if (mix_ != dragMix_) emit mixChanged(mix_);
}

void MiniMixCanvas::wheelEvent(QWheelEvent *e)
{
    // Görgő: nagyítás a kurzor körül, 6 és 96 másodperc között.
    const int dy = e->angleDelta().y();
    if (!dy) return;
    const qint64 w = std::clamp<qint64>(dy > 0 ? window_ / 2 : window_ * 2, 6000, 96000);
    const double frac = e->position().x() / std::max(1, width());
    const double anchor = viewStart_ + frac * window_;
    window_ = w;
    viewStart_ = qint64(anchor - frac * window_);
    update();
}

// ================================================================ keret

MiniMix::MiniMix(QWidget *parent) : QWidget(parent)
{
    setAttribute(Qt::WA_StyledBackground);
    setObjectName("miniMix");
    setStyleSheet("#miniMix { background: #101215; }");
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(92, 8, 12, 8);
    v->setSpacing(6);
    canvas_ = new MiniMixCanvas;
    v->addWidget(canvas_, 1);
    auto *row = new QHBoxLayout;
    row->setSpacing(6);
    preview_ = new QPushButton(TR("▶ Átmenet"));
    preview_->setToolTip(TR("Az átmenet meghallgatása az előhallgatáson, a mix pont előtt 5 másodperccel indítva"));
    reset_ = new QPushButton(TR("Automatikusra"));
    reset_->setToolTip(TR("Az átmenet egyedi mixének törlése"));
    editor_ = new QPushButton(TR("Mix editor →"));
    editor_->setToolTip(TR("Az átmenet megnyitása a Mix editorban (kiúsztatás, beúsztatás, voice track)"));
    info_ = new QLabel;
    info_->setObjectName("small");
    for (QPushButton *b : {preview_, reset_, editor_}) b->setFocusPolicy(Qt::NoFocus);
    row->addWidget(preview_);
    row->addWidget(info_, 1);
    row->addWidget(reset_);
    row->addWidget(editor_);
    v->addLayout(row);

    connect(canvas_, &MiniMixCanvas::dragging, this, &MiniMix::updateInfo);
    connect(canvas_, &MiniMixCanvas::mixChanged, this, &MiniMix::mixChanged);
    connect(preview_, &QPushButton::clicked, this, &MiniMix::previewRequested);
    connect(reset_, &QPushButton::clicked, this, &MiniMix::resetRequested);
    connect(editor_, &QPushButton::clicked, this, &MiniMix::editorRequested);
}

void MiniMix::setPair(int fromEntry, int toEntry, const QString &outTitle, const QString &inTitle, const MixParams &p, int kind)
{
    from_ = fromEntry;
    to_ = toEntry;
    p_ = p;
    kind_ = kind;
    canvas_->setPair(outTitle, inTitle, p);
    reset_->setEnabled(kind == 2);
    updateInfo(canvas_->mixMs());
}

void MiniMix::setWaves(const QVector<float> &outPeaks, qint64 outDur, const QVector<float> &inPeaks, qint64 inDur)
{
    canvas_->setWaves(outPeaks, outDur, inPeaks, inDur);
}

void MiniMix::setPreviewing(bool on)
{
    preview_->setText(on ? TR("■ Leállítás") : TR("▶ Átmenet"));
}

void MiniMix::updateInfo(qint64 mix)
{
    const QString kind = kind_ == 2 ? TR("egyedi mix") : kind_ == 1 ? TR("a számok alapértelmezése") : TR("automatikus");
    const qint64 overlap = std::max<qint64>(0, p_.outEndMs - mix);
    info_->setText(TR("mix pont %1 · átfedés %2 mp · %3")
                       .arg(clockMs(mix), i18n::locale().toString(overlap / 1000.0, 'f', 1), kind));
    info_->setToolTip(TR("Húzd a következő elemet a kimenő végére vagy beljebb; görgő: nagyítás"));
}
