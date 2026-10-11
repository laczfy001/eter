#include "i18n.h"
#include "MixEditorPage.h"
#include "Library.h"
#include "SchedulerPage.h"
#include "Theme.h"
#include "audio/Decoder.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

static QString clockText(qint64 ms)
{
    if (ms < 0) ms = 0;
    return QStringLiteral("%1:%2.%3").arg(ms / 60000).arg(ms / 1000 % 60, 2, 10, QLatin1Char('0')).arg(ms / 100 % 10);
}

static double dbToGain(double db) { return std::pow(10.0, db / 20.0); }
static qint64 round100(qint64 v) { return qint64(std::llround(v / 100.0)) * 100; }

static void repolish(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
}

// Mono, 16 bites WAV írása.
static bool writeWav(const QString &path, const std::vector<float> &samples, int rate)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const quint32 dataBytes = quint32(samples.size() * 2);
    QByteArray head;
    auto u32 = [&head](quint32 v) { head.append(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&head](quint16 v) { head.append(reinterpret_cast<const char *>(&v), 2); };
    head.append("RIFF");
    u32(36 + dataBytes);
    head.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(1);
    u32(quint32(rate));
    u32(quint32(rate) * 2);
    u16(2);
    u16(16);
    head.append("data");
    u32(dataBytes);
    f.write(head);
    QByteArray pcm;
    pcm.resize(int(dataBytes));
    auto *out = reinterpret_cast<qint16 *>(pcm.data());
    for (size_t i = 0; i < samples.size(); ++i)
        out[i] = qint16(std::lround(std::clamp(samples[i], -1.f, 1.f) * 32767.f));
    return f.write(pcm) == pcm.size();
}

// ============================================================ idővonal

static const int kRuler = 24;
static const QColor kBright[3] = {QColor("#b07a1e"), QColor("#2f6ea8"), QColor("#3f9a6b")};
static const QColor kDim[3] = {QColor("#3d3220"), QColor("#1d3046"), QColor("#1f3d2e")};
static const QColor kLine[3] = {QColor("#ffd58a"), QColor("#bfe0ff"), QColor("#b4f0cf")};
static const QColor kTag[3] = {QColor("#f5a524"), QColor("#5cb0ff"), QColor("#45d483")};

MixTimeline::MixTimeline(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(300);
    setMinimumWidth(380);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setClips({Clip(), Clip()});
}

void MixTimeline::setClips(const QVector<Clip> &clips)
{
    clips_ = clips;
    while (clips_.size() < 2) clips_.push_back(Clip());
    if (clips_.size() > 3) clips_.resize(3);
    tr_.resize(clips_.size() - 1);
    vt_.resize(clips_.size() - 1);
    for (Clip &v : vt_) v = Clip();
    active_ = std::min<int>(active_, tr_.size() - 1);
    update();
}

void MixTimeline::setClipWave(int k, const QVector<float> &peaks, qint64 durationMs)
{
    if (k < 0 || k >= clips_.size()) return;
    clips_[k].peaks = peaks;
    clips_[k].durMs = durationMs;
    update();
}

void MixTimeline::setVtWave(int j, const QVector<float> &peaks, qint64 durationMs, const QString &title)
{
    if (j < 0 || j >= vt_.size()) return;
    vt_[j].peaks = peaks;
    vt_[j].durMs = durationMs;
    vt_[j].title = title;
    update();
}

void MixTimeline::setParams(int j, const MixParams &p, bool center)
{
    if (j < 0 || j >= tr_.size()) return;
    tr_[j] = p;
    if (center) recenter();
    update();
}

void MixTimeline::setActive(int j)
{
    j = std::clamp<int>(j, 0, tr_.size() - 1);
    if (j == active_) return;
    active_ = j;
    update();
    emit activeChanged(j);
}

qint64 MixTimeline::cue(int k) const { return k <= 0 ? 0 : tr_[k - 1].cueInMs; }

qint64 MixTimeline::clipStart(int k) const
{
    qint64 t = 0;
    for (int i = 1; i <= k && i < clips_.size(); ++i) t += tr_[i - 1].mixOutMs - cue(i - 1);
    return t;
}

qint64 MixTimeline::naturalEnd(int k) const
{
    return clips_[k].durMs > 0 ? clipStart(k) + clips_[k].durMs - cue(k) : -1;
}

qint64 MixTimeline::clipEnd(int k) const
{
    const qint64 nat = naturalEnd(k);
    if (k >= tr_.size()) return nat;
    const qint64 end = clipStart(k) + outEndTrack(k) - cue(k);
    return nat >= 0 ? std::min(nat, end) : end;
}

// A kimenő elem vége a saját idejében. Ez a mix ponttól független: az elemek egymásba
// csúsztatása nem változtat rajta, csak az elem jobb szélének húzása.
qint64 MixTimeline::outEndTrack(int j) const
{
    const MixParams &p = tr_[j];
    qint64 end = p.outEndMs > 0 ? p.outEndMs : p.mixOutMs + p.fadeOutMs;
    if (clips_[j].durMs > 0) end = std::min(end, clips_[j].durMs);
    return std::max(end, p.mixOutMs);
}

void MixTimeline::recenter()
{
    // Két elemnél a mix pont a szélesség 45%-ánál; háromnál a két átmenet közepe kerül középre.
    if (tr_.size() == 1) viewStart_ = clipStart(1) - window_ * 45 / 100;
    else viewStart_ = (clipStart(1) + clipStart(2)) / 2 - window_ / 2;
    update();
    emit viewChanged();
}

void MixTimeline::setViewStart(qint64 ms)
{
    if (ms == viewStart_) return;
    viewStart_ = ms;
    update();
    emit viewChanged();
}

// A görgethető tartomány: az első elem elejétől az utolsó elem végéig, a voice trackekkel együtt.
qint64 MixTimeline::contentStart() const
{
    qint64 t = 0;
    for (int j = 0; j < tr_.size(); ++j)
        if (tr_[j].vtItemId) t = std::min(t, clipStart(j + 1) + tr_[j].vtOffsetMs);
    return t - 2000;
}

qint64 MixTimeline::contentEnd() const
{
    const int last = clips_.size() - 1;
    qint64 t = naturalEnd(last) >= 0 ? naturalEnd(last) : clipStart(last) + 60000;
    for (int k = 0; k < last; ++k)
        if (naturalEnd(k) >= 0) t = std::max(t, naturalEnd(k));
    for (int j = 0; j < tr_.size(); ++j)
        if (tr_[j].vtItemId) t = std::max(t, clipStart(j + 1) + tr_[j].vtOffsetMs + vtLen(j));
    return t + 2000;
}

void MixTimeline::setWindowMs(qint64 ms)
{
    window_ = std::clamp<qint64>(ms, 6000, 384000);
    recenter();
}

// Nagyítás úgy, hogy az x képpontnál lévő időpont a helyén maradjon (pl. az egérkurzor alatt).
void MixTimeline::zoomAt(qint64 ms, double x)
{
    const qint64 w = std::clamp<qint64>(ms, 6000, 384000);
    if (w == window_) return;
    const double frac = std::clamp(x / std::max(1, width()), 0.0, 1.0);
    const double anchor = double(viewStart_) + frac * double(window_);
    window_ = w;
    viewStart_ = qint64(std::llround(anchor - frac * double(window_)));
    update();
    emit viewChanged();
}

void MixTimeline::setPlayhead(qint64 ms)
{
    if (ms == playhead_) return;
    playhead_ = ms;
    update();
}

double MixTimeline::xOf(qint64 ms) const { return double(ms - viewStart_) / double(window_) * width(); }
qint64 MixTimeline::msAt(double x) const { return viewStart_ + qint64(x / std::max(1, width()) * double(window_)); }

QRectF MixTimeline::laneRect(int lane) const
{
    const int n = clips_.size();
    const double h = height() - kRuler;
    const double clipH = h * (n == 2 ? 0.38 : 0.275);
    if (lane < n) return QRectF(0, kRuler + lane * clipH, width(), clipH);
    return QRectF(0, kRuler + n * clipH, width(), h - n * clipH);
}

// A szintgörbe függőleges helye a sávon belül: 1,0 felül, 0 alul.
static double gainY(const QRectF &lane, double gain)
{
    const double top = lane.top() + 30, bottom = lane.bottom() - 8;
    return bottom - std::clamp(gain, 0.0, 1.0) * (bottom - top);
}

QPointF MixTimeline::handlePos(Handle h, int j) const
{
    if (j < 0 || j >= tr_.size()) return {};
    const MixParams &p = tr_[j];
    const QRectF out = laneRect(j), in = laneRect(j + 1);
    const qint64 mix = clipStart(j + 1);
    switch (h) {
    case MixPoint: return {xOf(mix), in.top()};
    case FadeOutEnd: return {xOf(clipEnd(j) - p.fadeOutMs), gainY(out, 1.0)};     // a kiúsztatás kezdete
    case FadeInEnd: return {xOf(mix + p.fadeInMs), gainY(in, 1.0)};
    case StartLevel: return {xOf(mix), gainY(in, p.fadeInMs > 0 ? dbToGain(p.startLevelDb) : 1.0)};
    case Intro: return {xOf(mix + p.introMs - p.cueInMs), in.bottom() - 8};
    case TrimLeft: return {xOf(mix), in.center().y() + 10};
    case TrimRight: return {xOf(clipEnd(j)), out.center().y() + 10};
    case Block: return {xOf(mix) + 60, in.center().y() + 10};
    case VtBlock: return {xOf(mix + p.vtOffsetMs) + 10, laneRect(clips_.size()).center().y() + 10};
    default: return {};
    }
}

void MixTimeline::drawWave(QPainter &p, const QRectF &lane, const QVector<float> &peaks, qint64 durMs,
                           qint64 trackAtLeftMs, const QColor &color) const
{
    if (peaks.isEmpty() || durMs <= 0) return;
    const double mid = lane.center().y() + 10;
    const double maxAmp = lane.height() / 2.0 - 22;
    const int w = width();
    for (int x = 0; x < w; x += 2) {
        const qint64 t0 = trackAtLeftMs + qint64(double(x) / w * window_);
        const qint64 t1 = trackAtLeftMs + qint64(double(x + 2) / w * window_);
        if (t1 < 0 || t0 >= durMs) continue;
        const int from = int(std::clamp<qint64>(t0 * peaks.size() / durMs, 0, peaks.size() - 1));
        const int to = int(std::clamp<qint64>(t1 * peaks.size() / durMs, from, peaks.size() - 1));
        float v = 0.f;
        for (int i = from; i <= to; ++i) v = std::max(v, peaks[i]);
        const double amp = std::max(0.5, std::sqrt(std::min(1.f, v)) * maxAmp);
        p.fillRect(QRectF(x, mid - amp, 2, amp * 2), color);
    }
}

void MixTimeline::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const int w = width();
    const int n = clips_.size();
    const QRectF vtLane = laneRect(n);
    p.fillRect(rect(), theme::panel);

    // Az átfedések sávja
    for (int j = 0; j < tr_.size(); ++j) {
        const double x0 = xOf(clipStart(j + 1)), x1 = xOf(clipEnd(j));
        if (x1 > x0) p.fillRect(QRectF(x0, laneRect(j).top(), x1 - x0, laneRect(j + 1).bottom() - laneRect(j).top()), QColor("#20242c"));
    }

    // Vonalzó: a kijelölt átmenet mix pontjához viszonyított másodpercek
    QFont mono("IBM Plex Mono");
    mono.setPixelSize(11);
    p.setFont(mono);
    const qint64 zero = clipStart(active_ + 1);
    qint64 step = 120000;
    for (qint64 s : {1000, 2000, 5000, 10000, 20000, 30000, 60000, 120000})
        if (double(s) / window_ * w >= 70) { step = s; break; }
    const qint64 first = (viewStart_ - zero) / step - 1;
    for (qint64 k = first; ; ++k) {
        const double x = xOf(zero + k * step);
        if (x > w) break;
        if (x < -80) continue;
        p.setPen(QColor("#30353e"));
        p.drawLine(QPointF(x, 0), QPointF(x, kRuler));
        p.setPen(k == 0 ? theme::amberSoft : theme::muted);
        p.drawText(QPointF(x + 4, 16), k == 0 ? TR("mix pont")
                                             : TR("%1%2 mp").arg(k > 0 ? "+" : "−").arg(std::llabs(k * step) / 1000));
    }
    p.setPen(QColor("#30353e"));
    p.drawLine(0, kRuler, w, kRuler);
    for (int lane = 1; lane <= n; ++lane) p.drawLine(QPointF(0, laneRect(lane).top()), QPointF(w, laneRect(lane).top()));

    // Hullámformák: halványan a teljes fájl, erősen az, ami ténylegesen szól
    for (int k = 0; k < n; ++k) {
        const QRectF lane = laneRect(k);
        const qint64 trackAtLeft = cue(k) + (viewStart_ - clipStart(k));
        drawWave(p, lane, clips_[k].peaks, clips_[k].durMs, trackAtLeft, kDim[k]);
        const double x0 = k == 0 ? 0 : std::max(0.0, xOf(clipStart(k)));
        const double x1 = k < tr_.size() ? std::min<double>(w, xOf(clipEnd(k))) : w;
        if (x1 > x0) {
            p.save();
            p.setClipRect(QRectF(x0, lane.top(), x1 - x0, lane.height()));
            drawWave(p, lane, clips_[k].peaks, clips_[k].durMs, trackAtLeft, kBright[k]);
            p.restore();
        }
        // A fájl vége
        const qint64 nat = naturalEnd(k);
        if (nat >= 0) {
            const double xe = xOf(nat);
            if (xe >= 0 && xe <= w) {
                p.setPen(QPen(QColor("#6b7380"), 1, Qt::DashLine));
                p.drawLine(QPointF(xe, lane.top()), QPointF(xe, lane.bottom()));
            }
        }
    }
    // Voice trackek
    for (int j = 0; j < tr_.size(); ++j) {
        if (!tr_[j].vtItemId) continue;
        const qint64 at = clipStart(j + 1) + tr_[j].vtOffsetMs;
        const double x0 = xOf(at), x1 = xOf(at + vtLen(j));
        p.fillRect(QRectF(x0, vtLane.top() + 1, x1 - x0, vtLane.height() - 1), QColor("#16303a"));
        drawWave(p, vtLane, vt_[j].peaks, vt_[j].durMs, viewStart_ - at, QColor("#4fb6c9"));
    }

    p.setRenderHint(QPainter::Antialiasing);
    // Mix pontok: függőleges vonal a két érintett sávon és a voice track sávon át
    for (int j = 0; j < tr_.size(); ++j) {
        const double x = xOf(clipStart(j + 1));
        p.setPen(QPen(j == active_ ? theme::amber : QColor("#8a6a2a"), 1.5, Qt::DashLine));
        p.drawLine(QPointF(x, laneRect(j).top()), QPointF(x, laneRect(j + 1).bottom()));
        p.drawLine(QPointF(x, vtLane.top()), QPointF(x, vtLane.bottom()));
    }

    // Intro vége
    for (int j = 0; j < tr_.size(); ++j) {
        if (tr_[j].introMs <= 0) continue;
        const QRectF lane = laneRect(j + 1);
        const double xi = xOf(clipStart(j + 1) + tr_[j].introMs - tr_[j].cueInMs);
        p.setPen(QPen(theme::blue, 1.5, Qt::DashLine));
        p.drawLine(QPointF(xi, lane.top() + 30), QPointF(xi, lane.bottom()));
        p.setFont(mono);
        p.setPen(theme::blueSoft);
        p.drawText(QPointF(xi + 10, lane.bottom() - 4), TR("intro vége ") + clockText(tr_[j].introMs));
    }

    // Szintgörbék elemenként: beúsztatás, teljes szint, kiúsztatás
    for (int k = 0; k < n; ++k) {
        const QRectF lane = laneRect(k);
        QPainterPath path;
        if (k == 0) {
            path.moveTo(-2, gainY(lane, 1.0));
        } else {
            const MixParams &in = tr_[k - 1];
            path.moveTo(xOf(clipStart(k)), gainY(lane, in.fadeInMs > 0 ? dbToGain(in.startLevelDb) : 1.0));
            path.lineTo(xOf(clipStart(k) + in.fadeInMs), gainY(lane, 1.0));
        }
        if (k < tr_.size()) {
            const MixParams &out = tr_[k];
            // Teljes szint a kiúsztatás kezdetéig, onnan a görbe az elem végéig.
            const qint64 end = clipEnd(k);
            const qint64 fadeStart = end - out.fadeOutMs;
            path.lineTo(xOf(fadeStart), gainY(lane, 1.0));
            if (out.fadeOutMs > 0) {
                const eter::FadeCurve curve = curveFromName(out.curve);
                for (int i = 1; i <= 40; ++i) {
                    const float f = i / 40.f;
                    path.lineTo(xOf(fadeStart + qint64(out.fadeOutMs * f)), gainY(lane, eter::fadeGain(curve, 1.f, 0.f, f)));
                }
            }
        } else {
            path.lineTo(w + 2, gainY(lane, 1.0));
        }
        p.setPen(QPen(kLine[k], 2));
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    // Az elemek széle: itt fogható meg az elem eleje és vége
    auto edge = [&](double x, const QRectF &lane, const QColor &c, bool left) {
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRect(QRectF(x - 1.5, lane.top() + 30, 3, lane.height() - 34));
        const double cy = lane.center().y() + 10;
        p.drawRoundedRect(QRectF(left ? x - 1.5 : x - 6.5, cy - 12, 8, 24), 2, 2);
    };
    for (int j = 0; j < tr_.size(); ++j) {
        edge(xOf(clipEnd(j)), laneRect(j), kTag[j], false);
        edge(xOf(clipStart(j + 1)), laneRect(j + 1), kTag[j + 1], true);
    }

    // Fogópontok
    auto square = [&](Handle h, int j, const QColor &c) {
        const QPointF pos = handlePos(h, j);
        p.setPen(QPen(theme::bg, 2));
        p.setBrush(c);
        p.drawRoundedRect(QRectF(pos.x() - 6, pos.y() - 6, 12, 12), 2, 2);
    };
    for (int j = 0; j < tr_.size(); ++j) {
        square(MixPoint, j, theme::amber);
        square(FadeOutEnd, j, kLine[j]);
        square(FadeInEnd, j, kLine[j + 1]);
        if (tr_[j].fadeInMs > 0) square(StartLevel, j, kLine[j + 1]);
        if (tr_[j].introMs > 0) square(Intro, j, theme::blue);
    }

    // Lejátszófej
    if (playhead_ >= 0) {
        const double xp = xOf(playhead_);
        p.setPen(QPen(Qt::white, 2));
        p.drawLine(QPointF(xp, kRuler), QPointF(xp, height()));
    }

    // Sávcímkék
    QFont label = font();
    label.setPixelSize(12);
    p.setFont(label);
    auto tag = [&](const QRectF &lane, const QString &num, const QColor &c, const QString &text) {
        const int numW = p.fontMetrics().horizontalAdvance(num);
        const int tw = p.fontMetrics().horizontalAdvance(text) + numW + 34;
        p.setPen(Qt::NoPen);
        p.setBrush(theme::bg);
        p.drawRoundedRect(QRectF(8, lane.top() + 6, std::min<double>(tw, w - 16), 20), 3, 3);
        p.setPen(c);
        p.drawText(QPointF(16, lane.top() + 20), num);
        p.setPen(theme::text);
        p.drawText(QRectF(26 + numW, lane.top() + 6, w - 60, 20), Qt::AlignVCenter | Qt::AlignLeft,
                   p.fontMetrics().elidedText(text, Qt::ElideRight, w - 60));
    };
    for (int k = 0; k < n; ++k) {
        QString text = clips_[k].title;
        // Ennyi szól belőle ténylegesen (ha mindkét vége ismert)
        const qint64 end = clipEnd(k);
        if (k > 0 && end >= 0) text += TR("   ·   adásban %1").arg(clockText(end - clipStart(k)));
        tag(laneRect(k), QString::number(k + 1), kTag[k], text);
    }
    QStringList vtNames;
    for (int j = 0; j < tr_.size(); ++j)
        if (tr_[j].vtItemId) vtNames << vt_[j].title;
    tag(vtLane, TR("VT"), QColor("#7fd6e8"),
        TR("VOICE TRACK   ") + (vtNames.isEmpty() ? TR("nincs") : vtNames.join(QStringLiteral(" · "))));
}

MixTimeline::Hit MixTimeline::hitTest(const QPointF &pos) const
{
    Hit hit;
    // 1. A szintgörbe négyzetei
    double best = 11.0;
    for (int j = 0; j < tr_.size(); ++j) {
        for (Handle h : {FadeOutEnd, FadeInEnd, StartLevel, Intro, MixPoint}) {
            if (h == StartLevel && tr_[j].fadeInMs <= 0) continue;
            if (h == Intro && tr_[j].introMs <= 0) continue;
            const QPointF hp = handlePos(h, j);
            const double d = std::hypot(hp.x() - pos.x(), hp.y() - pos.y());
            if (d < best) {
                best = d;
                hit = {h, j};
            }
        }
    }
    if (hit.h != None) return hit;
    // 2. Az elemek széle
    for (int j = 0; j < tr_.size(); ++j) {
        const QRectF out = laneRect(j), in = laneRect(j + 1);
        if (pos.y() > in.top() + 28 && pos.y() < in.bottom() && std::abs(pos.x() - xOf(clipStart(j + 1))) < 7) return {TrimLeft, j};
        if (pos.y() > out.top() + 28 && pos.y() < out.bottom() && std::abs(pos.x() - xOf(clipEnd(j))) < 7) return {TrimRight, j};
    }
    // 3. Voice track
    const QRectF vtLane = laneRect(clips_.size());
    if (vtLane.contains(pos)) {
        for (int j = 0; j < tr_.size(); ++j) {
            if (!tr_[j].vtItemId) continue;
            const qint64 at = clipStart(j + 1) + tr_[j].vtOffsetMs;
            if (pos.x() >= xOf(at) && pos.x() <= xOf(at + vtLen(j))) return {VtBlock, j};
        }
        return hit;
    }
    // 4. Az elem teste: a bejövő elem (és vele a mix pont) mozgatása
    for (int j = tr_.size() - 1; j >= 0; --j) {
        const QRectF in = laneRect(j + 1);
        if (in.contains(pos) && pos.x() > xOf(clipStart(j + 1)) &&
            (j + 1 >= tr_.size() || pos.x() < xOf(clipEnd(j + 1))))
            return {Block, j};
    }
    return hit;
}

void MixTimeline::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    const QPointF pos = e->position();
    drag_ = hitTest(pos);
    if (drag_.h == None) return;
    const int j = drag_.j;
    setActive(j);
    // Innentől a kimenő elem vége rögzített érték: a csúsztatás nem viszi magával.
    if (tr_[j].outEndMs <= 0) tr_[j].outEndMs = outEndTrack(j);
    const qint64 t = msAt(pos.x());
    switch (drag_.h) {
    case Block:
    case MixPoint:
    case TrimLeft: dragOffsetMs_ = double(t - clipStart(j + 1)); break;
    case TrimRight: dragOffsetMs_ = double(t - clipEnd(j)); break;
    case VtBlock: dragOffsetMs_ = double(t - (clipStart(j + 1) + tr_[j].vtOffsetMs)); break;
    default: dragOffsetMs_ = 0; break;
    }
}

void MixTimeline::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pos = e->position();
    if (drag_.h == None) {
        // Kurzor: jelzi, mi fogható meg.
        const Hit h = hitTest(pos);
        setCursor(h.h == None ? Qt::ArrowCursor
                  : (h.h == Block || h.h == VtBlock) ? Qt::SizeHorCursor
                  : (h.h == TrimLeft || h.h == TrimRight) ? Qt::SplitHCursor : Qt::PointingHandCursor);
        return;
    }
    const int j = drag_.j;
    MixParams &p = tr_[j];
    const qint64 t = msAt(pos.x()) - qint64(dragOffsetMs_);
    const qint64 outDur = clips_[j].durMs;
    const qint64 minMix = cue(j) + 200;
    const qint64 maxMix = outDur > 0 ? outDur : std::max<qint64>(p.mixOutMs, minMix) + 600000;
    auto setNextStart = [&](qint64 timelineMs) {
        // A következő elem kezdete az idővonalon → a kimenő elem mix pontja a saját idejében.
        p.mixOutMs = round100(std::clamp<qint64>(cue(j) + (timelineMs - clipStart(j)), minMix, maxMix));
    };
    switch (drag_.h) {
    case MixPoint:
    case Block:
        // Csúsztatás: csak a következő elem kezdete változik, a kimenő elem hossza nem.
        setNextStart(t);
        break;
    case TrimRight: {
        // Az elem vége: itt hallgat el. A következő elem a helyén marad.
        const qint64 end = cue(j) + (t - clipStart(j));
        p.outEndMs = round100(std::clamp<qint64>(end, p.mixOutMs, outDur > 0 ? outDur : end));
        break;
    }
    case TrimLeft: {
        // Az elem eleje: a tartalom a helyén marad, csak később kezdődik (vagy korábban).
        qint64 d = round100(t - clipStart(j + 1));
        d = std::max(d, -p.cueInMs);
        d = std::clamp<qint64>(d, minMix - p.mixOutMs, maxMix - p.mixOutMs);
        const qint64 inDur = clips_[j + 1].durMs;
        if (inDur > 0) d = std::min(d, inDur - 500 - p.cueInMs);
        p.cueInMs += d;
        p.mixOutMs += d;
        break;
    }
    case FadeOutEnd:
        // A kiúsztatás kezdete: innen halkul az elem a végéig.
        p.fadeOutMs = round100(std::clamp<qint64>(clipEnd(j) - msAt(pos.x()), 0, 60000));
        break;
    case FadeInEnd:
        p.fadeInMs = round100(std::clamp<qint64>(msAt(pos.x()) - clipStart(j + 1), 0, 15000));
        break;
    case StartLevel: {
        const QRectF lane = laneRect(j + 1);
        const double top = lane.top() + 30, bottom = lane.bottom() - 8;
        const double gain = std::clamp((bottom - pos.y()) / (bottom - top), 0.01, 1.0);
        p.startLevelDb = std::round(std::clamp(20.0 * std::log10(gain), -40.0, 0.0) * 2) / 2;
        break;
    }
    case Intro:
        p.introMs = round100(std::max<qint64>(100, p.cueInMs + (msAt(pos.x()) - clipStart(j + 1))));
        break;
    case VtBlock:
        p.vtOffsetMs = round100(std::clamp<qint64>(t - clipStart(j + 1), -120000, 120000));
        break;
    default: break;
    }
    // A vég nem lehet a mix pont előtt (nem maradhat csend), a kiúsztatás nem lehet hosszabb az elemnél.
    p.outEndMs = std::max(p.outEndMs, p.mixOutMs);
    p.fadeOutMs = std::clamp<qint64>(p.fadeOutMs, 0, std::max<qint64>(0, p.outEndMs - cue(j)));
    update();
    emit paramsEdited(j);
}

void MixTimeline::mouseReleaseEvent(QMouseEvent *) { drag_ = Hit(); }

void MixTimeline::wheelEvent(QWheelEvent *e)
{
    // Vízszintes görgő vagy Shift + görgő: lapozás az időben; sima görgő: nagyítás.
    const QPoint d = e->angleDelta();
    if (d.x() != 0 || (e->modifiers() & Qt::ShiftModifier)) {
        const int delta = d.x() != 0 ? d.x() : d.y();
        setViewStart(viewStart_ - window_ * delta / 1200);
        return;
    }
    if (d.y() != 0) zoomAt(d.y() > 0 ? window_ / 2 : window_ * 2, e->position().x());   // a kurzor alatti pont marad a helyén
}

// ================================================================ oldal

static QFrame *panel()
{
    auto *f = new QFrame;
    f->setObjectName("panel");
    return f;
}

static const eter::Engine::Channel kClipCh[3] = {eter::Engine::Pfl, eter::Engine::Pfl2, eter::Engine::Pfl4};
static const eter::Engine::Channel kVtCh[2] = {eter::Engine::Pfl3, eter::Engine::Pfl5};

MixEditorPage::MixEditorPage(Database *db, eter::Engine *engine, QWidget *parent)
    : QWidget(parent), db_(db), engine_(engine), cancel_(std::make_shared<std::atomic<bool>>(false))
{
    autoMixDb_ = db_->setting("auto_mix_threshold_db", "-24").toDouble();
    defaultFadeMs_ = std::clamp(db_->setting("default_fade_out_ms", "2500").toInt(), 100, 15000);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    // Idővonal
    auto *tl = panel();
    auto *tv = new QVBoxLayout(tl);
    tv->setContentsMargins(1, 1, 1, 1);
    tv->setSpacing(0);
    auto *head = new QFrame;
    head->setObjectName("panelHeader");
    auto *h = new QHBoxLayout(head);
    h->setContentsMargins(10, 8, 10, 8);
    h->setSpacing(8);
    auto *prev = new QPushButton(QStringLiteral("‹"));
    prev->setAccessibleName(TR("Előző átmenet"));
    prev->setToolTip(TR("Előző átmenet"));
    auto *nextBtn = new QPushButton(QStringLiteral("›"));
    nextBtn->setAccessibleName(TR("Következő átmenet"));
    nextBtn->setToolTip(TR("Következő átmenet"));
    title_ = new QLabel;
    title_->setObjectName("panelTitle");
    title_->setTextFormat(Qt::PlainText);
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    triple_ = new QPushButton(TR("3 elem"));
    triple_->setCheckable(true);
    triple_->setToolTip(TR("Három egymást követő elem és a köztük lévő két átmenet szerkesztése egyszerre"));
    triple_->setChecked(QSettings().value("mix/triple", false).toBool());
    triple_->setProperty("active", triple_->isChecked());
    previewBtn_ = new QPushButton(TR("Előhallgatás"));
    previewBtn_->setIcon(theme::icon(theme::Icon::Play, theme::text));
    previewBtn_->setToolTip(TR("Az átmenet meghallgatása az előhallgatás kimenetén, a mix pont előtt 5 másodperccel indítva. Az adást nem érinti."));
    loop_ = new QCheckBox(TR("Ismétlés"));
    loop_->setToolTip(TR("Az előhallgatás a végén újraindul, a közben módosított beállításokkal"));
    loop_->setChecked(QSettings().value("mix/loop", false).toBool());
    auto *zoomOut = new QPushButton(QStringLiteral("−"));
    zoomOut->setAccessibleName(TR("Kicsinyítés"));
    auto *zoomIn = new QPushButton(QStringLiteral("+"));
    zoomIn->setAccessibleName(TR("Nagyítás"));
    auto *zoomLabel = new QLabel;
    zoomLabel->setObjectName("mono");
    h->addWidget(prev);
    h->addWidget(nextBtn);
    h->addWidget(title_, 1);
    h->addWidget(triple_);
    h->addWidget(previewBtn_);
    h->addWidget(loop_);
    h->addSpacing(4);
    h->addWidget(zoomOut);
    h->addWidget(zoomLabel);
    h->addWidget(zoomIn);
    tv->addWidget(head);
    timeline_ = new MixTimeline;
    tv->addWidget(timeline_, 1);
    hscroll_ = new QScrollBar(Qt::Horizontal);
    hscroll_->setObjectName("mixScroll");
    hscroll_->setAccessibleName(TR("Idővonal görgetése"));
    hscroll_->setToolTip(TR("Az idővonal görgetése (Shift + görgő is)"));
    tv->addWidget(hscroll_);
    auto *foot = new QFrame;
    foot->setObjectName("panelFooter");
    auto *fh = new QHBoxLayout(foot);
    fh->setContentsMargins(10, 7, 10, 7);
    state_ = new QLabel;
    state_->setObjectName("small");
    auto *hint = new QLabel(TR("Húzható: az elemek teste és két széle, a négyzetek, a voice track · görgő: nagyítás, Shift + görgő: lapozás"));
    hint->setObjectName("small");
    hint->setToolTip(TR("Az elem teste: az elem (és a mix pont) elmozdul; az előtte lévő elem hossza nem változik.\n"
        "Az elem jobb széle: itt hallgat el az elem.\n"
        "Az elem bal széle: az eleje levágódik, a tartalom a helyén marad.\n"
        "Négyzetek: a kiúsztatás kezdete, a beúsztatás hossza, a kezdő szint, az intro vége."));
    fh->addWidget(state_);
    fh->addStretch(1);
    fh->addWidget(hint);
    tv->addWidget(foot);

    // Az átmenetek listája az idővonal mellett
    auto *lp = panel();
    auto *lv = new QVBoxLayout(lp);
    lv->setContentsMargins(1, 1, 1, 1);
    lv->setSpacing(0);
    auto *lhead = new QFrame;
    lhead->setObjectName("panelHeader");
    auto *lh = new QHBoxLayout(lhead);
    lh->setContentsMargins(10, 8, 10, 8);
    auto *ltitle = new QLabel(TR("Átmenetek"));
    ltitle->setObjectName("panelTitle");
    lh->addWidget(ltitle);
    lhead->setMinimumHeight(46);
    lv->addWidget(lhead);
    table_ = new QTableWidget(0, 3);
    table_->setHorizontalHeaderLabels({TR("Időpont"), TR("Kimenő → bejövő"), TR("Mix")});
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setDefaultSectionSize(44);
    table_->setShowGrid(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setFrameShape(QFrame::NoFrame);
    table_->setWordWrap(false);              // a két sort mi törjük; a hosszú cím a sor végén rövidül
    table_->setTextElideMode(Qt::ElideRight);
    table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *hh = table_->horizontalHeader();
    hh->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    hh->setHighlightSections(false);
    hh->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->setColumnWidth(0, 76);
    table_->setColumnWidth(2, 84);
    lv->addWidget(table_, 1);

    auto *split = new QSplitter(Qt::Horizontal);
    split->setHandleWidth(8);
    split->setChildrenCollapsible(false);
    split->addWidget(tl);
    split->addWidget(lp);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({770, 290});

    // Jobb oldal: számszerű beállítások (görgethető, ha nem fér ki)
    auto *right = panel();
    right->setFixedWidth(346);
    auto *rightOuter = new QVBoxLayout(right);
    rightOuter->setContentsMargins(1, 1, 1, 1);
    rightOuter->setSpacing(0);

    // Hármas nézetben: melyik átmenetet mutatják a mezők
    actRow_ = new QFrame;
    actRow_->setObjectName("panelHeader");
    auto *al = new QHBoxLayout(actRow_);
    al->setContentsMargins(10, 8, 10, 8);
    al->setSpacing(6);
    for (int j = 0; j < 2; ++j) {
        act_[j] = new QPushButton(j == 0 ? TR("1 → 2 átmenet") : TR("2 → 3 átmenet"));
        act_[j]->setCheckable(true);
        act_[j]->setToolTip(TR("Az alábbi mezők ezt az átmenetet mutatják"));
        al->addWidget(act_[j], 1);
        connect(act_[j], &QPushButton::clicked, this, [this, j] { timeline_->setActive(j); showParams(); });
    }
    rightOuter->addWidget(actRow_);

    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *inner = new QWidget;
    auto *rv = new QVBoxLayout(inner);
    rv->setContentsMargins(13, 10, 13, 10);
    rv->setSpacing(7);
    scroll->setWidget(inner);
    rightOuter->addWidget(scroll, 1);

    auto spin = [](double min, double max, double step, const QString &suffix) {
        auto *s = new QDoubleSpinBox;
        s->setRange(min, max);
        s->setSingleStep(step);
        s->setDecimals(1);
        s->setSuffix(suffix);
        s->setLocale(i18n::locale());
        return s;
    };
    auto cap = [&](const QString &text) {
        auto *c = new QLabel(text);
        c->setObjectName("caption");
        c->setTextFormat(Qt::PlainText);
        rv->addWidget(c);
        return c;
    };
    outCaption_ = cap(TR("KIMENŐ ELEM"));
    auto *f1 = new QFormLayout;
    f1->setVerticalSpacing(6);
    mixOut_ = spin(0, 36000, 0.1, TR(" mp"));
    mixOut_->setToolTip(TR("A kimenő elem ezen pontján indul a bejövő"));
    mixClock_ = new QLabel;
    mixClock_->setObjectName("mono");
    auto *mixRow = new QHBoxLayout;
    mixRow->addWidget(mixOut_, 1);
    mixRow->addWidget(mixClock_);
    fadeOut_ = spin(0, 60, 0.1, TR(" mp"));
    fadeOut_->setToolTip(TR("A kimenő elem a vége előtt ennyi idő alatt halkul el; 0: nincs kiúsztatás"));
    outEnd_ = spin(0, 36000, 0.1, TR(" mp"));
    outEnd_->setToolTip(TR("A kimenő elem itt hallgat el (a saját idejében). A csúsztatás nem változtat rajta, csak te."));
    curve_ = new QComboBox;
    curve_->addItem(TR("S-görbe"), "scurve");
    curve_->addItem(TR("Lineáris"), "linear");
    curve_->addItem(TR("Logaritmikus"), "log");
    f1->addRow(TR("Mix pont"), mixRow);
    f1->addRow(TR("Elem vége"), outEnd_);
    f1->addRow(TR("Kiúsztatás"), fadeOut_);
    f1->addRow(TR("Görbe"), curve_);
    rv->addLayout(f1);

    rv->addSpacing(2);
    inCaption_ = cap(TR("BEJÖVŐ ELEM"));
    auto *f2 = new QFormLayout;
    f2->setVerticalSpacing(6);
    cueIn_ = spin(0, 36000, 0.1, TR(" mp"));
    cueIn_->setToolTip(TR("A bejövő elem innen indul; ami előtte van, nem szól"));
    fadeIn_ = spin(0, 15, 0.1, TR(" mp"));
    startLevel_ = spin(-40, 0, 0.5, TR(" dB"));
    intro_ = spin(0, 600, 0.1, TR(" mp"));
    intro_->setSpecialValueText(TR("nincs"));
    intro_->setToolTip(TR("Az ének kezdete a számban. A lejátszó eddig visszaszámlál; mindig a számhoz mentődik."));
    f2->addRow(TR("Kezdőpont"), cueIn_);
    f2->addRow(TR("Beúsztatás"), fadeIn_);
    f2->addRow(TR("Kezdő szint"), startLevel_);
    f2->addRow(TR("Intro vége"), intro_);
    rv->addLayout(f2);

    rv->addSpacing(2);
    cap(TR("VOICE TRACK"));
    vtName_ = new QLabel;
    vtName_->setObjectName("small");
    vtName_->setTextFormat(Qt::PlainText);
    vtName_->setWordWrap(true);
    rv->addWidget(vtName_);
    auto *vtButtons = new QHBoxLayout;
    vtButtons->setSpacing(6);
    recordBtn_ = new QPushButton(TR("Felvétel"));
    recordBtn_->setToolTip(TR("Mikrofonfelvétel: az átmenet közben szól az előhallgatáson, te pedig rábeszélsz. Fejhallgatóval használd."));
    vtFile_ = new QPushButton(TR("Fájl…"));
    vtLibrary_ = new QPushButton(TR("Hangtár…"));
    vtRemove_ = new QPushButton(TR("Törlés"));
    vtRemove_->setToolTip(TR("A voice track levétele erről az átmenetről (a hangtárban megmarad)"));
    vtButtons->addWidget(recordBtn_);
    vtButtons->addWidget(vtFile_);
    vtButtons->addWidget(vtLibrary_);
    vtButtons->addWidget(vtRemove_);
    rv->addLayout(vtButtons);
    auto *f3 = new QFormLayout;
    f3->setVerticalSpacing(6);
    vtOffset_ = spin(-120, 120, 0.1, TR(" mp"));
    vtOffset_->setToolTip(TR("A voice track kezdése a mix ponthoz képest; negatív érték: előtte"));
    vtDuck_ = spin(-30, 0, 0.5, TR(" dB"));
    vtDuck_->setToolTip(TR("Ennyivel halkul a zene, amíg a voice track szól"));
    f3->addRow(TR("Kezdés"), vtOffset_);
    f3->addRow(TR("Zene alatta"), vtDuck_);
    rv->addLayout(f3);

    rv->addSpacing(2);
    cap(TR("MENTÉS HELYE"));
    scopePair_ = new QRadioButton(TR("Csak ez az átmenet"));
    scopePair_->setChecked(true);
    scopePair_->setToolTip(TR("Az adott playlist-sorokhoz kötve; ha a sorrend megváltozik, érvényét veszti."));
    scopeItems_ = new QRadioButton(TR("Alapértelmezés a számokhoz"));
    scopeItems_->setToolTip(TR("A kimenő számhoz a mix pont és a kiúsztatás, a bejövőhöz a kezdőpont kerül; ezután mindig így szólnak. Voice track így nem menthető."));
    rv->addWidget(scopePair_);
    rv->addWidget(scopeItems_);
    rv->addStretch(1);
    save_ = new QPushButton(TR("Mentés"));
    save_->setProperty("accent", true);
    save_->setProperty("tall", true);
    reset_ = new QPushButton(TR("Visszaállítás automatikusra"));
    reset_->setToolTip(TR("Törli a kijelölt átmenethez mentett egyedi mixet; ha a számokhoz mentés van kijelölve, azok alapértelmezését is"));
    // A mentés gombjai a görgethető rész alatt, mindig láthatóan.
    auto *actions = new QVBoxLayout;
    actions->setContentsMargins(13, 8, 13, 12);
    actions->setSpacing(7);
    actions->addWidget(save_);
    actions->addWidget(reset_);
    rightOuter->addLayout(actions);

    root->addWidget(split, 1);
    root->addWidget(right);

    timer_ = new QTimer(this);
    timer_->setInterval(25);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &MixEditorPage::previewTick);

    auto updateZoom = [this, zoomLabel] { zoomLabel->setText(TR("%1 mp").arg(timeline_->windowMs() / 1000)); };
    connect(zoomOut, &QPushButton::clicked, this, [this, updateZoom] { timeline_->zoomAt(timeline_->windowMs() * 2, timeline_->width() / 2.0); });
    connect(zoomIn, &QPushButton::clicked, this, [this, updateZoom] { timeline_->zoomAt(timeline_->windowMs() / 2, timeline_->width() / 2.0); });
    updateZoom();
    connect(prev, &QPushButton::clicked, this, [this] { selectPair(currentIndex() - 1); });
    connect(nextBtn, &QPushButton::clicked, this, [this] { selectPair(currentIndex() + 1); });
    connect(previewBtn_, &QPushButton::clicked, this, &MixEditorPage::togglePreview);
    connect(loop_, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("mix/loop", on); });
    connect(triple_, &QPushButton::toggled, this, [this, updateZoom](bool on) {
        QSettings().setValue("mix/triple", on);
        triple_->setProperty("active", on);
        repolish(triple_);
        if (recordMode_) return;
        if (anyDirty()) emit message(TR("A nem mentett módosítások elvesztek a nézetváltáskor."));
        stopPreview();
        dirty_[0] = dirty_[1] = false;
        loadCurrent();
        updateZoom();
    });
    connect(table_, &QTableWidget::cellClicked, this, [this](int row, int) { selectPair(row); });
    connect(timeline_, &MixTimeline::paramsEdited, this, [this](int j) {
        dirty_[j] = true;
        showParams();
        syncScrollBar();
    });
    connect(timeline_, &MixTimeline::activeChanged, this, [this](int) { showParams(); });
    connect(timeline_, &MixTimeline::viewChanged, this, &MixEditorPage::syncScrollBar);
    connect(timeline_, &MixTimeline::viewChanged, this, updateZoom);
    connect(hscroll_, &QScrollBar::valueChanged, this, [this](int v) {
        if (!loading_) timeline_->setViewStart(qint64(v) * 10);
    });
    for (QDoubleSpinBox *s : {mixOut_, outEnd_, fadeOut_, cueIn_, fadeIn_, startLevel_, intro_, vtOffset_, vtDuck_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &MixEditorPage::fieldsEdited);
    connect(curve_, &QComboBox::activated, this, &MixEditorPage::fieldsEdited);
    connect(save_, &QPushButton::clicked, this, &MixEditorPage::save);
    connect(reset_, &QPushButton::clicked, this, &MixEditorPage::resetToAuto);
    connect(recordBtn_, &QPushButton::clicked, this, &MixEditorPage::toggleRecord);
    connect(vtFile_, &QPushButton::clicked, this, &MixEditorPage::chooseVtFile);
    connect(vtLibrary_, &QPushButton::clicked, this, &MixEditorPage::chooseVtFromLibrary);
    connect(vtRemove_, &QPushButton::clicked, this, [this] { setVtItem(0); });

    loadCurrent();
}

// A görgetősáv tartománya és helyzete az idővonalhoz igazodik (egység: 10 ms).
void MixEditorPage::syncScrollBar()
{
    const qint64 view = timeline_->viewStart(), window = timeline_->windowMs();
    // A tartomány mindig tartalmazza az éppen látható részt is.
    const qint64 lo = std::min(timeline_->contentStart(), view);
    const qint64 hi = std::max(timeline_->contentEnd(), view + window);
    const bool was = loading_;
    loading_ = true;
    hscroll_->setRange(int(lo / 10), int(std::max(lo, hi - window) / 10));
    hscroll_->setPageStep(int(window / 10));
    hscroll_->setSingleStep(int(std::max<qint64>(1, window / 200)));
    hscroll_->setValue(int(view / 10));
    loading_ = was;
}

MixEditorPage::~MixEditorPage()
{
    cancel_->store(true);
    for (auto &t : threads_)
        if (t.joinable()) t.join();
}

int MixEditorPage::currentIndex() const
{
    for (int i = 0; i < pairs_.size(); ++i)
        if (pairs_[i].fromEntry == fromEntry_ && pairs_[i].toEntry == toEntry_) return i;
    return -1;
}

void MixEditorPage::setPairs(const QVector<MixPair> &pairs)
{
    pairs_ = pairs;
    refreshTable();
    const int i = currentIndex();
    // Újratöltés kell, ha a szerkesztett pár eltűnt (lejátszották vagy átrendezték), vagy ha
    // megváltozott, hogy mi a harmadik elem (illetve hogy van-e egyáltalán).
    const bool wantThree = triple_->isChecked() && i >= 0 && i + 1 < pairs_.size();
    const int wantTo2 = wantThree ? pairs_[i + 1].toEntry : 0;
    const bool gone = i < 0 || (shown() == 2) != wantThree || wantTo2 != shownTo2_;
    if (gone) {
        if (engine_->recording()) engine_->stopRecording();
        recordMode_ = false;
        if (previewing()) stopPreview();
        dirty_[0] = dirty_[1] = false;
        if (i < 0) {
            fromEntry_ = pairs_.isEmpty() ? 0 : pairs_.first().fromEntry;
            toEntry_ = pairs_.isEmpty() ? 0 : pairs_.first().toEntry;
        }
        loadCurrent();
    }
}

void MixEditorPage::selectPair(int index)
{
    if (index < 0 || index >= pairs_.size()) return;
    if (recordMode_) return;
    if (anyDirty()) emit message(TR("Az előző átmenet nem mentett módosításai elvesztek."));
    stopPreview();
    dirty_[0] = dirty_[1] = false;
    fromEntry_ = pairs_[index].fromEntry;
    toEntry_ = pairs_[index].toEntry;
    loadCurrent();
    refreshTable();
}

// Az átmenet érvényes beállításai: egyedi mix, a számok alapértelmezése, vagy automatikus.
MixParams MixEditorPage::resolve(const MixPair &pair, int *kind) const
{
    MixParams p;
    p.introMs = pair.to.introMs;
    const Transition t = db_->transition(pair.toEntry);
    if (t.toEntryId && t.fromEntryId == pair.fromEntry) {
        p.mixOutMs = t.mixOutMs;
        p.outEndMs = t.outEndMs > 0 ? t.outEndMs : t.mixOutMs + t.fadeOutMs;
        p.fadeOutMs = t.fadeOutMs;
        p.curve = t.fadeOutCurve;
        p.cueInMs = t.cueInMs;
        p.fadeInMs = t.fadeInMs;
        p.startLevelDb = t.startLevelDb;
        p.vtItemId = t.vtItemId;
        p.vtOffsetMs = t.vtOffsetMs;
        p.vtDuckDb = t.vtDuckDb;
        if (kind) *kind = 2;
        return p;
    }
    const bool defaults = pair.from.userMixOutMs > 0 || pair.to.cueInMs > 0;
    if (kind) *kind = defaults ? 1 : 0;
    const Wave w = waves_.value(pair.from.id);
    if (pair.from.userMixOutMs > 0) p.mixOutMs = pair.from.userMixOutMs;
    else if (w.ready) p.mixOutMs = w.autoMixMs;
    else if (pair.from.mixOutMs > 0) p.mixOutMs = pair.from.mixOutMs;
    else p.mixOutMs = std::max<qint64>(0, pair.from.durationMs - defaultFadeMs_);
    p.fadeOutMs = pair.from.fadeOutMs >= 0 ? pair.from.fadeOutMs : defaultFadeMs_;
    p.curve = pair.from.fadeCurve.isEmpty() ? QStringLiteral("scurve") : pair.from.fadeCurve;
    p.cueInMs = pair.to.cueInMs;
    p.outEndMs = pair.from.cueOutMs > 0 ? pair.from.cueOutMs : p.mixOutMs + p.fadeOutMs;
    const qint64 dur = w.ready ? w.durMs : pair.from.durationMs;
    if (dur > 0) p.outEndMs = std::min(p.outEndMs, dur);
    return p;
}

void MixEditorPage::loadCurrent()
{
    const int i = currentIndex();
    const bool has = i >= 0;
    for (QWidget *w : std::initializer_list<QWidget *>{outEnd_, mixOut_, fadeOut_, curve_, cueIn_, fadeIn_, startLevel_, intro_, save_, reset_,
                                                       previewBtn_, scopePair_, scopeItems_, recordBtn_, vtFile_, vtLibrary_,
                                                       vtRemove_, vtOffset_, vtDuck_})
        w->setEnabled(has);
    shownTo2_ = 0;
    if (!has) {
        title_->setText(TR("Nincs szerkeszthető átmenet"));
        state_->setText(TR("Legalább két elem kell a playlistbe"));
        timeline_->setClips({MixTimeline::Clip(), MixTimeline::Clip()});
        vtName_->setText(TR("Nincs voice track."));
        actRow_->setVisible(false);
        return;
    }
    // Hármas nézet, ha van harmadik elem is.
    const bool three = triple_->isChecked() && i + 1 < pairs_.size();
    QVector<Item> items{pairs_[i].from, pairs_[i].to};
    if (three) {
        items << pairs_[i + 1].to;
        shownTo2_ = pairs_[i + 1].toEntry;
    }
    auto name = [](const Item &it) { return it.artist.isEmpty() ? it.title : it.title + QStringLiteral(" – ") + it.artist; };
    QVector<MixTimeline::Clip> clips;
    QStringList titles;
    for (const Item &it : items) {
        requestWave(it);
        const Wave w = waves_.value(it.id);
        clips.push_back({name(it), w.peaks, w.ready ? w.durMs : it.durationMs});
        titles << it.title;
    }
    title_->setText(titles.join(QStringLiteral("  →  ")));
    timeline_->setClips(clips);
    for (int j = 0; j < shown(); ++j) timeline_->setParams(j, resolve(pairs_[i + j], &kind_[j]), false);
    // Hármas nézetben akkora ablak kell, hogy mindkét átmenet látsszon.
    if (three) {
        const qint64 span = timeline_->clipStart(2) - timeline_->clipStart(1);
        qint64 w = 24000;
        while (w < span * 2 && w < 384000) w *= 2;
        timeline_->setWindowMs(w);
    } else {
        timeline_->recenter();
    }
    for (int j = 0; j < shown(); ++j) applyVt(j);
    actRow_->setVisible(three);
    showParams();
    syncScrollBar();
}

void MixEditorPage::requestWave(const Item &item)
{
    if (!item.id || waves_.contains(item.id)) return;
    waves_.insert(item.id, Wave());
    const int id = item.id;
    const std::string path = item.path.toStdString();
    const int bins = int(std::clamp<qint64>(item.durationMs / 25, 2000, 40000));     // kb. 40 érték másodpercenként
    const double db = autoMixDb_;
    auto cancel = cancel_;
    threads_.emplace_back([this, id, path, bins, db, cancel] {
        std::vector<float> peaks;
        int64_t total = 0;
        if (!eter::scanPeaks(path, bins, peaks, total, *cancel) || cancel->load()) return;
        Wave w;
        w.peaks = QVector<float>(peaks.begin(), peaks.end());
        w.durMs = total * 1000 / eter::kRate;
        w.autoMixMs = eter::autoMixOut(peaks, total, db) * 1000 / eter::kRate;
        w.ready = true;
        QMetaObject::invokeMethod(this, [this, id, w] {
            waves_[id] = w;
            waveReady(id);
        }, Qt::QueuedConnection);
    });
}

void MixEditorPage::waveReady(int itemId)
{
    const int i = currentIndex();
    if (i < 0) return;
    const Wave &w = waves_[itemId];
    bool changed = false;
    for (int k = 0; k <= shown(); ++k) {
        const Item &it = k == 0 ? pairs_[i].from : pairs_[i + k - 1].to;
        if (it.id == itemId) timeline_->setClipWave(k, w.peaks, w.durMs);
    }
    for (int j = 0; j < shown(); ++j) {
        // Ha még nincs saját beállítás, a most megtalált automatikus mix pont érvényes.
        if (pairs_[i + j].from.id == itemId && !dirty_[j] && kind_[j] == 0) {
            timeline_->setParams(j, resolve(pairs_[i + j], &kind_[j]), false);
            changed = true;
        }
        if (timeline_->params(j).vtItemId == itemId) applyVt(j);
    }
    if (changed) {
        if (!previewing()) timeline_->recenter();
        showParams();
    }
    syncScrollBar();
}

// A voice track hullámformája az idővonalon.
void MixEditorPage::applyVt(int j)
{
    const int id = timeline_->params(j).vtItemId;
    if (!id) {
        timeline_->setVtWave(j, {}, 0, {});
        return;
    }
    const Item it = db_->item(id);
    if (!it.id) {
        timeline_->setVtWave(j, {}, 0, TR("(törölt elem)"));
        return;
    }
    requestWave(it);
    const Wave w = waves_.value(id);
    timeline_->setVtWave(j, w.peaks, w.ready ? w.durMs : it.durationMs, it.title);
}

void MixEditorPage::showParams()
{
    const int i = currentIndex();
    const bool has = i >= 0;
    const int a = timeline_->active();
    loading_ = true;
    const MixParams &p = timeline_->params(a);
    mixOut_->setValue(p.mixOutMs / 1000.0);
    mixClock_->setText(clockText(p.mixOutMs));
    fadeOut_->setValue(p.fadeOutMs / 1000.0);
    outEnd_->setValue(timeline_->outEndTrack(a) / 1000.0);
    curve_->setCurrentIndex(std::max(0, curve_->findData(p.curve)));
    cueIn_->setValue(p.cueInMs / 1000.0);
    fadeIn_->setValue(p.fadeInMs / 1000.0);
    startLevel_->setValue(p.startLevelDb);
    startLevel_->setEnabled(p.fadeInMs > 0 && has);
    intro_->setValue(p.introMs / 1000.0);
    vtOffset_->setValue(p.vtOffsetMs / 1000.0);
    vtDuck_->setValue(p.vtDuckDb);
    vtOffset_->setEnabled(p.vtItemId && has);
    vtDuck_->setEnabled(p.vtItemId && has);
    vtRemove_->setEnabled(p.vtItemId && has && !recordMode_);
    for (int j = 0; j < 2; ++j) {
        act_[j]->setChecked(j == a);
        act_[j]->setProperty("active", j == a);
        repolish(act_[j]);
    }
    if (has) {
        const MixPair &pair = pairs_[i + a];
        outCaption_->setText(TR("KIMENŐ: ") + pair.from.title.toUpper());
        inCaption_->setText(TR("BEJÖVŐ: ") + pair.to.title.toUpper());
        if (p.vtItemId) {
            const Item it = db_->item(p.vtItemId);
            vtName_->setText(it.id ? QStringLiteral("%1 · %2").arg(it.title, formatMs(it.durationMs))
                                   : TR("A voice track már nincs a hangtárban."));
        } else {
            vtName_->setText(TR("Nincs voice track ezen az átmeneten."));
        }
    }
    loading_ = false;
    auto kindText = [this](int j) {
        return dirty_[j] ? TR("nem mentett módosítás")
               : kind_[j] == 2 ? TR("egyedi mix")
               : kind_[j] == 1 ? TR("a számok alapértelmezése") : TR("automatikus");
    };
    if (recordMode_) state_->setText(TR("felvétel…"));
    else if (!has) state_->setText(TR("Legalább két elem kell a playlistbe"));
    else if (shown() == 2) state_->setText(QStringLiteral("1 → 2: %1  ·  2 → 3: %2").arg(kindText(0), kindText(1)));
    else state_->setText(kindText(0));
}

void MixEditorPage::fieldsEdited()
{
    if (loading_) return;
    auto ms = [](QDoubleSpinBox *s) { return qint64(std::llround(s->value() * 10)) * 100; };
    const int a = timeline_->active();
    MixParams p = timeline_->params(a);
    p.mixOutMs = ms(mixOut_);
    p.fadeOutMs = ms(fadeOut_);
    // A mix pont átírása nem viszi magával az elem végét; a vég legfeljebb a mix pontig húzható vissza.
    p.outEndMs = std::max(ms(outEnd_), p.mixOutMs);
    p.curve = curve_->currentData().toString();
    p.cueInMs = ms(cueIn_);
    p.fadeInMs = ms(fadeIn_);
    p.startLevelDb = startLevel_->value();
    p.introMs = ms(intro_);
    p.vtOffsetMs = ms(vtOffset_);
    p.vtDuckDb = vtDuck_->value();
    dirty_[a] = true;
    timeline_->setParams(a, p, false);
    showParams();
}

bool MixEditorPage::saveOne(int j, QVector<int> &changedItems)
{
    const MixPair pair = pairOf(j);
    const MixParams p = timeline_->params(j);
    db_->setItemIntro(pair.to.id, p.introMs);         // az intro mindig a szám tulajdonsága
    if (scopeItems_->isChecked()) {
        db_->setItemMixDefaults(pair.from.id, p.mixOutMs, p.fadeOutMs, p.curve, timeline_->outEndTrack(j));
        db_->setItemCueIn(pair.to.id, p.cueInMs);
        db_->deleteTransition(pair.toEntry);          // az alapértelmezés érvényesüljön, ne egy régi egyedi mix
    } else {
        Transition t;
        t.toEntryId = pair.toEntry;
        t.fromEntryId = pair.fromEntry;
        t.mixOutMs = p.mixOutMs;
        t.outEndMs = timeline_->outEndTrack(j);
        t.fadeOutMs = p.fadeOutMs;
        t.fadeOutCurve = p.curve;
        t.cueInMs = p.cueInMs;
        t.fadeInMs = p.fadeInMs;
        t.startLevelDb = p.fadeInMs > 0 ? p.startLevelDb : 0;
        t.vtItemId = p.vtItemId;
        t.vtOffsetMs = p.vtOffsetMs;
        t.vtDuckDb = p.vtDuckDb;
        db_->saveTransition(t);
    }
    changedItems << pair.from.id << pair.to.id;
    return true;
}

void MixEditorPage::save()
{
    const int i = currentIndex();
    if (i < 0) return;
    const int n = shown();
    // Ami módosult, az mentődik; ha semmi sem módosult, a kijelölt átmenet (például automatikusból egyedivé).
    QVector<int> which;
    for (int j = 0; j < n; ++j)
        if (dirty_[j]) which << j;
    if (which.isEmpty()) which << timeline_->active();
    if (scopeItems_->isChecked()) {
        for (int j : which) {
            if (timeline_->params(j).vtItemId) {
                emit message(TR("Voice track csak egy adott átmenethez menthető: válaszd a „Csak ez az átmenet” lehetőséget."));
                return;
            }
        }
    }
    QVector<int> changed;
    for (int j : which) saveOne(j, changed);
    for (int j : which) dirty_[j] = false;
    emit message(scopeItems_->isChecked() ? TR("Mentve a számokhoz (mix pont, kiúsztatás, kezdőpont).")
                 : which.size() > 1 ? TR("Mindkét átmenet egyedi mixe mentve.")
                                    : TR("Egyedi mix mentve ehhez az átmenethez."));
    emit mixSaved(changed);                           // a főablak frissíti a párokat, az visszahat ide
    const int again = currentIndex();
    if (again >= 0) {
        for (int j = 0; j < shown(); ++j) {
            if (dirty_[j]) continue;
            timeline_->setParams(j, resolve(pairs_[again + j], &kind_[j]), false);
            applyVt(j);
        }
        showParams();
        refreshTable();
    }
}

void MixEditorPage::resetToAuto()
{
    const int i = currentIndex();
    if (i < 0) return;
    const int a = timeline_->active();
    const MixPair pair = pairOf(a);
    db_->deleteTransition(pair.toEntry);
    if (scopeItems_->isChecked()) {
        db_->clearItemMixDefaults(pair.from.id);
        db_->setItemCueIn(pair.to.id, 0);
    }
    dirty_[a] = false;
    emit mixSaved({pair.from.id, pair.to.id});
    emit message(scopeItems_->isChecked() ? TR("Az egyedi mix és a számok alapértelmezése törölve.")
                                          : TR("Az átmenet egyedi mixe törölve."));
    const int again = currentIndex();
    if (again >= 0 && a < shown()) {
        timeline_->setParams(a, resolve(pairs_[again + a], &kind_[a]), false);
        applyVt(a);
        timeline_->recenter();
        showParams();
        refreshTable();
    }
}

void MixEditorPage::refreshTable()
{
    const QHash<int, int> custom = db_->transitionPairs();
    table_->setRowCount(pairs_.size());
    QFont mono("IBM Plex Mono");
    mono.setPixelSize(12);
    const int cur = currentIndex();
    for (int r = 0; r < pairs_.size(); ++r) {
        const MixPair &pair = pairs_[r];
        const bool isCustom = custom.value(pair.toEntry, 0) == pair.fromEntry;
        const bool defaults = pair.from.userMixOutMs > 0 || pair.to.cueInMs > 0;
        QString mix = isCustom ? TR("egyedi") : defaults ? TR("számok") : TR("auto");
        if (isCustom && db_->transition(pair.toEntry).vtItemId) mix += TR("\n+ VT");
        // Ami éppen adásban van, azt jelöljük.
        const QString when = pair.fromOnAir && pair.toOnAir ? TR("ADÁSBAN")
                             : pair.fromOnAir ? TR("%1\nADÁSBAN").arg(pair.startText) : pair.startText;
        const QString names = QStringLiteral("%1\n→ %2").arg(pair.from.title, pair.to.title);
        const QString cells[3] = {when, names, mix};
        const bool shownRow = cur >= 0 && r >= cur && r < cur + shown();
        for (int c = 0; c < 3; ++c) {
            auto *item = new QTableWidgetItem(cells[c]);
            if (c == 0) item->setFont(mono);
            if (pair.fromOnAir || pair.toOnAir) {
                item->setBackground(QColor("#3a2c10"));
                item->setForeground(theme::amberSoft);
            } else if (c == 2 && (isCustom || defaults)) {
                item->setForeground(theme::amberSoft);
            } else if (c == 2) {
                item->setForeground(QColor("#c9ced6"));
            }
            if (c == 1) {
                item->setToolTip(QStringLiteral("%1 → %2").arg(pair.from.title, pair.to.title) +
                                 (pair.fromOnAir ? TR("\nA kimenő elem most adásban van.") : QString()));
                if (shownRow) {
                    QFont f = item->font();
                    f.setBold(true);
                    item->setFont(f);
                }
            }
            table_->setItem(r, c, item);
        }
    }
    if (cur >= 0) table_->setCurrentCell(cur, 0);
}

// ----------------------------------------------------------- voice track

void MixEditorPage::setVtItem(int itemId)
{
    if (currentIndex() < 0) return;
    const int a = timeline_->active();
    MixParams p = timeline_->params(a);
    if (p.vtItemId == itemId) return;
    p.vtItemId = itemId;
    if (!itemId) p.vtOffsetMs = 0;
    dirty_[a] = true;
    scopePair_->setChecked(true);                     // voice track csak átmenethez menthető
    timeline_->setParams(a, p, false);
    applyVt(a);
    showParams();
}

void MixEditorPage::chooseVtFile()
{
    if (vtDialog_) vtDialog_->close();
    auto *dlg = new QFileDialog(window(), TR("Voice track betöltése fájlból"), QSettings().value("ui/lastDir").toString(),
                                TR("Hangfájlok (*.%1);;Minden fájl (*)").arg(audioExtensions().join(" *.")));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setFileMode(QFileDialog::ExistingFile);
    vtDialog_ = dlg;
    connect(dlg, &QFileDialog::fileSelected, this, [this](const QString &path) {
        if (path.isEmpty()) return;
        int id = db_->itemIdByPath(path);
        if (!id) {
            eter::Decoder dec;
            if (!dec.open(path.toStdString())) {
                emit message(TR("Ez a fájl nem olvasható hangként."));
                return;
            }
            Item it;
            it.categoryId = db_->voiceTrackCategory();
            it.path = path;
            it.title = QString::fromStdString(dec.title()).trimmed();
            if (it.title.isEmpty()) it.title = QFileInfo(path).completeBaseName();
            it.artist = QStringLiteral("");
            it.durationMs = dec.durationFrames() * 1000 / eter::kRate;
            id = db_->addItem(it);
            db_->notifyItemsChanged();
        }
        if (id) setVtItem(id);
    });
    dlg->setModal(false);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MixEditorPage::chooseVtFromLibrary()
{
    if (vtDialog_) vtDialog_->close();
    auto *dlg = new ItemPickerDialog(db_, window(), QStringLiteral("picker_category_vt"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    vtDialog_ = dlg;
    connect(dlg, &ItemPickerDialog::picked, this, [this](int itemId) { setVtItem(itemId); });
    dlg->setModal(false);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MixEditorPage::toggleRecord()
{
    if (recordMode_) {
        finishRecording();
        return;
    }
    if (currentIndex() < 0) return;
    stopPreview();
    std::string err;
    if (!engine_->startRecording(&err)) {
        emit message(QString::fromStdString(err));
        return;
    }
    recordMode_ = true;
    recordFor_ = timeline_->active();
    recordBtn_->setText(TR("Felvétel vége"));
    recordBtn_->setProperty("danger", true);
    repolish(recordBtn_);
    startPreview(true);
    if (!previewing()) {                              // nem sikerült elindítani az előhallgatást
        engine_->stopRecording();
        recordMode_ = false;
        recordBtn_->setText(TR("Felvétel"));
        recordBtn_->setProperty("danger", false);
        repolish(recordBtn_);
    }
    showParams();
}

void MixEditorPage::finishRecording()
{
    if (!recordMode_) return;
    std::vector<float> samples = engine_->stopRecording();
    const qint64 startedAt = previewStartMs_;
    const int j = recordFor_;
    recordMode_ = false;
    recordBtn_->setText(TR("Felvétel"));
    recordBtn_->setProperty("danger", false);
    repolish(recordBtn_);
    stopPreview();
    if (currentIndex() < 0 || j >= shown()) return;

    // A csend levágása az elejéről és a végéről (küszöb: −40 dB), kis ráhagyással.
    const float threshold = 0.01f;
    size_t first = 0, last = samples.size();
    while (first < samples.size() && std::fabs(samples[first]) < threshold) ++first;
    while (last > first && std::fabs(samples[last - 1]) < threshold) --last;
    if (last <= first || last - first < size_t(eter::kRate) / 4) {
        showParams();
        emit message(TR("A felvétel üres vagy túl halk – nem jött jel a mikrofonról."));
        return;
    }
    first = first > size_t(eter::kRate) / 10 ? first - eter::kRate / 10 : 0;
    last = std::min(samples.size(), last + size_t(eter::kRate) / 4);
    const std::vector<float> cut(samples.begin() + first, samples.begin() + last);

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/eter/voicetracks");
    QDir().mkpath(dir);
    const QDateTime now = QDateTime::currentDateTime();
    const QString path = dir + QStringLiteral("/vt-%1.wav").arg(now.toString("yyyyMMdd-HHmmss"));
    if (!writeWav(path, cut, eter::kRate)) {
        showParams();
        emit message(TR("A felvétel nem menthető ide: %1").arg(path));
        return;
    }
    Item it;
    it.categoryId = db_->voiceTrackCategory();
    it.path = path;
    it.title = TR("Voice track %1").arg(now.toString("MM.dd. HH:mm:ss"));
    it.artist = QStringLiteral("");
    it.durationMs = qint64(cut.size()) * 1000 / eter::kRate;
    const int id = db_->addItem(it);
    db_->notifyItemsChanged();
    if (!id) {
        showParams();
        return;
    }
    // A felvétel az előhallgatás indulásakor kezdődött; a levágott csenddel későbbre kerül.
    MixParams p = timeline_->params(j);
    p.vtItemId = id;
    p.vtOffsetMs = round100(startedAt + qint64(first) * 1000 / eter::kRate - timeline_->clipStart(j + 1));
    dirty_[j] = true;
    scopePair_->setChecked(true);
    timeline_->setParams(j, p, false);
    applyVt(j);
    showParams();
    emit message(TR("Felvétel kész (%1). Hallgasd vissza, igazítsd a helyét, majd mentsd.").arg(formatMs(it.durationMs)));
}

// ---------------------------------------------------------- előhallgatás

void MixEditorPage::togglePreview()
{
    if (recordMode_) {
        finishRecording();
        return;
    }
    if (previewing()) stopPreview();
    else startPreview(false);
}

void MixEditorPage::startPreview(bool forRecording)
{
    const int i = currentIndex();
    if (i < 0) return;
    emit previewStarting();
    const int nTr = shown();
    pv_ = {};
    pv_.n = nTr + 1;
    std::string err;
    for (int k = 0; k < pv_.n; ++k) {
        const Item &it = k == 0 ? pairs_[i].from : pairs_[i + k - 1].to;
        if (!engine_->source(kClipCh[k]).load(it.path.toStdString(), &err)) {
            for (int c = 0; c < 3; ++c) engine_->source(kClipCh[c]).unload();
            emit message(QString::fromStdString(err));
            return;
        }
    }
    const int a = timeline_->active();
    for (int j = 0; j < nTr; ++j) {
        pv_.tr[j] = timeline_->params(j);
        if (forRecording && j == a) pv_.tr[j].vtItemId = 0;       // felvétel közben a korábbi voice track nem szól
        if (pv_.tr[j].vtItemId) {
            const Item it = db_->item(pv_.tr[j].vtItemId);
            if (it.id && engine_->source(kVtCh[j]).load(it.path.toStdString(), &err)) {
                const Wave w = waves_.value(it.id);
                pv_.vtDur[j] = w.ready ? w.durMs : it.durationMs;
            }
        }
    }
    for (int k = 0; k < pv_.n; ++k) {
        pv_.cue[k] = timeline_->cue(k);
        pv_.start[k] = timeline_->clipStart(k);
        const Item &it = k == 0 ? pairs_[i].from : pairs_[i + k - 1].to;
        const Wave w = waves_.value(it.id);
        pv_.dur[k] = w.ready ? w.durMs : it.durationMs;
    }
    for (int j = 0; j < nTr; ++j) pv_.tr[j].outEndMs = timeline_->outEndTrack(j);
    // Honnan indul: az első átmenet előtt 5 mp-cel. Hármas nézetben, ha a középső elem hosszú,
    // és a második átmenet van kijelölve, akkor közvetlenül az előtt.
    int firstClip = 0;
    if (pv_.n == 3 && a == 1 && pv_.tr[1].mixOutMs - pv_.cue[1] > 30000) {
        firstClip = 1;
        pv_.vtStarted[0] = true;
    }
    const int j0 = firstClip;
    qint64 pos = pv_.tr[j0].mixOutMs - 5000;
    if (pv_.vtDur[j0] > 0) pos = std::min(pos, pv_.tr[j0].mixOutMs + pv_.tr[j0].vtOffsetMs - 2000);
    pos = std::max(pos, pv_.cue[firstClip]);
    previewStartMs_ = pv_.start[firstClip] + (pos - pv_.cue[firstClip]);
    for (int k = 0; k < pv_.n; ++k)
        engine_->source(kClipCh[k]).seek((k == firstClip ? pos : pv_.cue[k]) * eter::kRate / 1000);
    pv_.started = firstClip;
    engine_->source(kClipCh[firstClip]).play();
    phase_ = 1;
    previewBtn_->setText(TR("Leállítás"));
    previewBtn_->setIcon(theme::icon(theme::Icon::Stop, theme::blueSoft));
    previewBtn_->setProperty("active", true);
    repolish(previewBtn_);
    timer_->start();
}

void MixEditorPage::previewTick()
{
    float l, r;
    for (int k = 0; k < 3; ++k) engine_->source(kClipCh[k]).takePeaks(l, r);
    for (int j = 0; j < 2; ++j) engine_->source(kVtCh[j]).takePeaks(l, r);

    const int k = pv_.started;
    auto &cur = engine_->source(kClipCh[k]);
    const qint64 posMs = cur.position() * 1000 / eter::kRate;
    const qint64 now = pv_.start[k] + (posMs - pv_.cue[k]);        // az idővonal ideje
    timeline_->setPlayhead(now);

    // A következő elem indítása a mix pontnál
    if (k < pv_.n - 1) {
        const MixParams &p = pv_.tr[k];
        if (posMs >= p.mixOutMs || !cur.playing()) {
            engine_->source(kClipCh[k + 1]).play(int(p.fadeInMs), float(dbToGain(p.startLevelDb)));
            pv_.started = k + 1;
        }
    }

    // Az elemek vége: a beállított végpont előtt indul a kiúsztatás (a mix ponttól függetlenül).
    for (int c = 0; c < pv_.n - 1; ++c) {
        auto &src = engine_->source(kClipCh[c]);
        if (pv_.faded[c] || !src.playing()) continue;
        const MixParams &p = pv_.tr[c];
        const qint64 pos = src.position() * 1000 / eter::kRate;
        qint64 end = p.outEndMs > 0 ? p.outEndMs : p.mixOutMs + p.fadeOutMs;
        if (pv_.dur[c] > 0) end = std::min(end, pv_.dur[c]);
        const bool cutsEarly = pv_.dur[c] <= 0 || end < pv_.dur[c] - 50;
        if (p.fadeOutMs > 0 && pos >= end - p.fadeOutMs) {
            pv_.faded[c] = true;
            src.fadeOutAndStop(int(std::max<qint64>(30, end - pos)), curveFromName(p.curve));
        } else if (p.fadeOutMs <= 0 && cutsEarly && pos >= end - 30) {
            pv_.faded[c] = true;
            src.fadeOutAndStop(30);               // kiúsztatás nélküli vég: rövid lecsengés, hogy ne kattanjon
        }
    }

    // Voice trackek: a saját időpontjukban indulnak, alattuk a zene halkul.
    for (int j = 0; j < pv_.n - 1; ++j) {
        auto &vt = engine_->source(kVtCh[j]);
        if (pv_.vtDur[j] > 0 && !pv_.vtStarted[j] && now >= pv_.start[j + 1] + pv_.tr[j].vtOffsetMs) {
            pv_.vtStarted[j] = true;
            vt.play();
            ++pv_.vtPlaying;
            for (int c = 0; c < pv_.n; ++c) engine_->source(kClipCh[c]).setDuck(float(dbToGain(pv_.tr[j].vtDuckDb)));
        }
        if (pv_.vtStarted[j] && vt.takeFinished()) {
            if (--pv_.vtPlaying <= 0)
                for (int c = 0; c < pv_.n; ++c) engine_->source(kClipCh[c]).setDuck(1.f);
        }
    }

    bool anyPlaying = false;
    for (int c = 0; c < pv_.n; ++c) anyPlaying = anyPlaying || engine_->source(kClipCh[c]).playing();

    if (recordMode_) {
        const float peak = engine_->recordPeak();
        state_->setText(TR("felvétel… %1  ·  szint: %2").arg(clockText(std::max<qint64>(0, now - previewStartMs_)))
                            .arg(peak > 0.001f ? TR("%1 dB").arg(20.0 * std::log10(peak), 0, 'f', 0) : TR("nincs jel")));
        // Felvétel közben az utolsó elem a végéig szól; legfeljebb 3 perc után leáll.
        if (now - previewStartMs_ > 180000 || (pv_.started == pv_.n - 1 && !anyPlaying)) finishRecording();
        return;
    }

    if (pv_.started == pv_.n - 1) {
        const int j = pv_.n - 2;
        const MixParams &p = pv_.tr[j];
        qint64 tail = std::max((p.outEndMs > 0 ? p.outEndMs : p.mixOutMs + p.fadeOutMs) - p.mixOutMs, p.fadeInMs);
        if (pv_.vtDur[j] > 0) tail = std::max(tail, p.vtOffsetMs + pv_.vtDur[j]);
        if (p.introMs > p.cueInMs) tail = std::max(tail, p.introMs - p.cueInMs);
        tail = std::min<qint64>(tail + 4000, 120000);
        const qint64 lastMs = engine_->source(kClipCh[pv_.n - 1]).position() * 1000 / eter::kRate - pv_.cue[pv_.n - 1];
        if (lastMs >= tail || !anyPlaying) {
            const bool again = loop_->isChecked();
            stopPreview();
            if (again) startPreview(false);           // a közben módosított beállításokkal indul újra
        }
    }
}

void MixEditorPage::stopPreview()
{
    if (phase_ == 0) return;
    phase_ = 0;
    timer_->stop();
    for (int k = 0; k < 3; ++k) engine_->source(kClipCh[k]).unload();
    for (int j = 0; j < 2; ++j) engine_->source(kVtCh[j]).unload();
    timeline_->setPlayhead(-1);
    previewBtn_->setText(TR("Előhallgatás"));
    previewBtn_->setIcon(theme::icon(theme::Icon::Play, theme::text));
    previewBtn_->setProperty("active", false);
    repolish(previewBtn_);
    if (recordMode_) {
        // Felvétel közben állt le (például fülváltás miatt): a felvételt eldobjuk.
        engine_->stopRecording();
        recordMode_ = false;
        recordBtn_->setText(TR("Felvétel"));
        recordBtn_->setProperty("danger", false);
        repolish(recordBtn_);
        showParams();
    }
}
