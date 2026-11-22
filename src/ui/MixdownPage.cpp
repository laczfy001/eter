#include "i18n.h"
#include "MixdownPage.h"
#include "Library.h"

#include "audio/Engine.h"
#include "core/SmartMixConfig.h"
#include "core/VolumeEnvelope.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <limits>
#include <cmath>

using eter::kRate;

static QString mmss(qint64 ms, bool tenths = false)
{
    if (ms < 0) ms = 0;
    QString s = QStringLiteral("%1:%2").arg(ms / 60000).arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'));
    if (tenths) s += QStringLiteral(".%1").arg(ms / 100 % 10);
    return s;
}

static bool isMusic(const Item &it) { return it.kind.isEmpty() || it.kind == QLatin1String("music"); }

// ------------------------------------------------------------------ a sorrend listája

class MdSeqList : public QListWidget {
public:
    std::function<void(const QVector<int> &, int)> dropped;   // hangtárból: elemek, beszúrási hely
    std::function<void(const QVector<int> &)> reordered;      // belső áthúzás: az új sorrend (régi indexek)

    explicit MdSeqList(QWidget *parent = nullptr) : QListWidget(parent)
    {
        setDragDropMode(QAbstractItemView::DragDrop);
        setDefaultDropAction(Qt::MoveAction);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setWordWrap(true);
    }

protected:
    void dragEnterEvent(QDragEnterEvent *e) override
    {
        if (e->mimeData()->hasFormat(QString::fromLatin1(kItemsMime))) e->acceptProposedAction();
        else QListWidget::dragEnterEvent(e);
    }
    void dragMoveEvent(QDragMoveEvent *e) override
    {
        if (e->mimeData()->hasFormat(QString::fromLatin1(kItemsMime))) e->acceptProposedAction();
        else QListWidget::dragMoveEvent(e);
    }
    void dropEvent(QDropEvent *e) override
    {
        if (e->mimeData()->hasFormat(QString::fromLatin1(kItemsMime)) && e->source() != this) {
            const QListWidgetItem *at = itemAt(e->position().toPoint());
            const int row = at ? row_(at) : count();
            if (dropped) dropped(decodeIds(e->mimeData()->data(QString::fromLatin1(kItemsMime))), row);
            e->acceptProposedAction();
            return;
        }
        QListWidget::dropEvent(e);
        QVector<int> order;
        for (int i = 0; i < count(); ++i) order << item(i)->data(Qt::UserRole).toInt();
        if (reordered) QTimer::singleShot(0, this, [this, order] { reordered(order); });
    }

private:
    int row_(const QListWidgetItem *it) const { return row(it); }
};

// A jobb oldali hangtár-lista: húzáskor a hangtár saját formátumában adja az elemeket.
class MdLibList : public QListWidget {
public:
    using QListWidget::QListWidget;

protected:
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override
    {
        QVector<int> ids;
        for (const QListWidgetItem *i : items) ids << i->data(Qt::UserRole).toInt();
        auto *m = new QMimeData;
        m->setData(QString::fromLatin1(kItemsMime), encodeIds(ids));
        return m;
    }
    QStringList mimeTypes() const override { return {QString::fromLatin1(kItemsMime)}; }
};

// ------------------------------------------------------------------ az idővonal

class MdTimeline : public QWidget {
public:
    explicit MdTimeline(MixdownPage *page) : QWidget(page), page_(page)
    {
        setMouseTracking(true);
        setFocusPolicy(Qt::ClickFocus);
        setMinimumHeight(260);
        setAcceptDrops(true);
    }
    double pps = 4.0;          // képpont / másodperc
    qint64 offset = 0;         // a bal szél ideje (ms)
    bool envMode = false;      // hangerőgörbe szerkesztése
    std::function<void()> viewChanged;

    double xOf(qint64 ms) const { return (ms - offset) / 1000.0 * pps; }
    qint64 msAt(double x) const { return offset + qint64(x / pps * 1000.0); }

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        // Egy elemre kattintás (húzás nélkül): a lejátszási pozíció is oda kerül, onnan indul a voice track felvétele.
        if ((drag_ == Body || drag_ == Left || drag_ == Right) && std::abs(e->position().x() - pressX_) < 3)
            page_->setPlayhead(std::max<qint64>(0, msAt(e->position().x())));
        drag_ = None;
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) page_->deleteSelection();
        else QWidget::keyPressEvent(e);
    }
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragMoveEvent(QDragMoveEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *) override { dropX_ = -1; update(); }
    void dropEvent(QDropEvent *e) override;

private:
    enum Drag { None, Body, Left, Right, Vt, Head, EnvPoint };
    MixdownPage *page_;
    Drag drag_ = None;
    int dragIdx_ = -1, dragPoint_ = -1;
    qint64 dragOrigin_ = 0, dragValue_ = 0;
    double dragX_ = 0, pressX_ = -1000;
    double dropX_ = -1;
    int dropLane_ = 0;         // 0, 1: zenesáv; 2: voice track

    static constexpr int kRuler = 22, kVtLane = 54;
    double laneH() const { return (height() - kRuler - kVtLane) / 2.0; }
    int laneAt(double y) const { return y >= height() - kVtLane ? 2 : y >= kRuler + laneH() ? 1 : 0; }
    QRectF clipRect(int i) const
    {
        const auto &e = page_->entries()[i];
        const double y = kRuler + page_->laneOf(i) * laneH() + 4;
        return QRectF(xOf(page_->startOf(i)), y, (e.end - e.cueIn) / 1000.0 * pps, laneH() - 8);
    }
    // A görbe a sáv belsejében: felül 0 dB, alul −40 dB.
    QRectF envArea(int i) const { return clipRect(i).adjusted(2, 18, -2, -4); }
    double envY(int i, double db) const
    {
        const QRectF a = envArea(i);
        return a.top() + (-db / -env::kMinDb) * a.height();
    }
    QPointF envPoint(int i, int k) const
    {
        const auto &e = page_->entries()[i];
        const env::Point &p = e.env[k];
        return {clipRect(i).left() + (p.ms - e.cueIn) / 1000.0 * pps, envY(i, p.db)};
    }
    int envPointAt(int i, const QPointF &pt) const
    {
        const auto &e = page_->entries()[i];
        for (int k = 0; k < e.env.size(); ++k)
            if (QLineF(envPoint(i, k), pt).length() <= 7) return k;
        return -1;
    }
    qint64 envMsAt(int i, double x) const
    {
        const auto &e = page_->entries()[i];
        return std::clamp<qint64>(e.cueIn + qint64((x - clipRect(i).left()) / pps * 1000.0), e.cueIn, e.end);
    }
    double envDbAt(int i, double y) const
    {
        const QRectF a = envArea(i);
        return std::clamp((y - a.top()) / std::max(1.0, a.height()) * env::kMinDb, env::kMinDb, 0.0);
    }
    QRectF vtRect(int i) const
    {
        const auto &e = page_->entries()[i];
        const qint64 at = (i + 1 < page_->entries().size() ? page_->startOf(i + 1) : page_->startOf(i) + e.end - e.cueIn) + e.vtOffsetMs;
        return QRectF(xOf(at), height() - kVtLane + 6, std::max(4.0, e.vtDurMs / 1000.0 * pps), kVtLane - 12);
    }
};

void MdTimeline::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor("#14161b"));
    const auto &es = page_->entries();
    // Vonalzó
    p.fillRect(QRect(0, 0, width(), kRuler), QColor("#1a1d23"));
    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    double stepS = 10;
    for (double s : {1.0, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0, 600.0})
        if (s * pps >= 70) {
            stepS = s;
            break;
        }
    const qint64 first = (offset / 1000 / qint64(stepS)) * qint64(stepS);
    for (qint64 s = first; xOf(s * 1000) < width(); s += qint64(stepS)) {
        const double x = xOf(s * 1000);
        p.setPen(QColor("#3a404b"));
        p.drawLine(QPointF(x, kRuler - 8), QPointF(x, height()));
        p.setPen(QColor("#8a919b"));
        p.drawText(QPointF(x + 3, 14), mmss(s * 1000));
    }
    const double lh = laneH();
    p.setPen(QColor("#1f232a"));
    p.drawLine(QPointF(0, kRuler + lh), QPointF(width(), kRuler + lh));
    p.drawLine(QPointF(0, height() - kVtLane), QPointF(width(), height() - kVtLane));
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor("#c79bff"));
    p.drawText(QPointF(6, height() - kVtLane + 14), TR("VOICE TRACK"));
    // Elemek
    static const QColor fills[2] = {QColor("#3a2c10"), QColor("#1d2f47")};
    static const QColor lines[2] = {QColor("#7a5418"), QColor("#2f5a8a")};
    static const QColor fgs[2] = {QColor("#ffd58a"), QColor("#9fd0ff")};
    for (int i = 0; i < es.size(); ++i) {
        const auto &e = es[i];
        const QRectF r = clipRect(i);
        if (r.right() < 0 || r.left() > width()) continue;
        const int c = page_->laneOf(i);
        { const bool csel = i == page_->selected() && !page_->vtSelected();
        p.setPen(QPen(csel ? QColor("#f5a524") : lines[c], csel ? 2 : 1)); }
        p.setBrush(fills[c]);
        p.drawRoundedRect(r, 4, 4);
        // Hullámforma
        if (const MixdownPage::Wave *w = page_->wave(e.item.path); w && w->ready && !w->peaks.isEmpty() && w->durMs > 0) {
            p.setPen(QPen(fgs[c].darker(140), 1));
            const double mid = r.center().y();
            const double half = r.height() / 2 - 14;
            for (double x = std::max(0.0, r.left()); x < std::min<double>(width(), r.right()); x += 1.0) {
                const qint64 ms = e.cueIn + qint64((x - r.left()) / pps * 1000.0);
                const int bin = int(std::clamp<qint64>(ms * w->peaks.size() / w->durMs, 0, w->peaks.size() - 1));
                const double a = w->peaks[bin] * half;
                p.drawLine(QPointF(x, mid - a + 8), QPointF(x, mid + a + 8));
            }
        }
        // Kiúsztatás
        if (e.fadeOut > 0) {
            const double fx = r.right() - e.fadeOut / 1000.0 * pps;
            p.setPen(QPen(QColor("#ffffff"), 1.5));
            p.drawLine(QPointF(fx, r.top() + 16), QPointF(r.right(), r.bottom() - 2));
        }
        p.setPen(fgs[c]);
        p.drawText(r.adjusted(6, 2, -4, 0), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("%1 · %2").arg(i + 1).arg(e.item.title));
        // Hangerőgörbe: szerkesztéskor mindig, egyébként ha van
        if (envMode || !env::isFlat(e.env)) {
            p.setRenderHint(QPainter::Antialiasing, true);
            QPolygonF poly;
            poly << QPointF(r.left(), envY(i, env::dbAt(e.env, e.cueIn)));
            for (int k = 0; k < e.env.size(); ++k)
                if (e.env[k].ms >= e.cueIn && e.env[k].ms <= e.end) poly << envPoint(i, k);
            poly << QPointF(r.right(), envY(i, env::dbAt(e.env, e.end)));
            p.setPen(QPen(QColor("#ffffff"), envMode ? 2 : 1.2));
            p.drawPolyline(poly);
            if (envMode) {
                p.setBrush(QColor("#ffffff"));
                for (int k = 0; k < e.env.size(); ++k)
                    if (e.env[k].ms >= e.cueIn && e.env[k].ms <= e.end) p.drawRect(QRectF(envPoint(i, k) - QPointF(4, 4), QSizeF(8, 8)));
            }
            p.setRenderHint(QPainter::Antialiasing, false);
        }
        // Voice track
        if (!e.vtPath.isEmpty()) {
            const QRectF v = vtRect(i);
            const bool vsel = page_->vtSelected() && i == page_->selected();
            p.setPen(QPen(vsel ? QColor("#f5a524") : QColor("#6a4fa0"), vsel ? 2 : 1));
            p.setBrush(QColor("#2a1f40"));
            p.drawRoundedRect(v, 3, 3);
            p.setPen(QColor("#d7b8ff"));
            p.drawText(v.adjusted(4, 2, -2, 0), Qt::AlignLeft | Qt::AlignTop, e.vtTitle);
        }
    }
    // Behúzás közben: hová kerül
    if (dropX_ >= 0) {
        const double y0 = dropLane_ == 2 ? height() - kVtLane : kRuler + dropLane_ * lh;
        const double h = dropLane_ == 2 ? kVtLane : lh;
        p.fillRect(QRectF(dropX_ - 1.5, y0, 3, h), QColor("#f5a524"));
        p.fillRect(QRectF(dropX_, y0 + 2, 60, h - 4), QColor(245, 165, 36, 40));
    }
    // Lejátszási pozíció
    const double hx = xOf(page_->playheadMs());
    p.fillRect(QRectF(hx - 1, 0, 2, height()), QColor("#ff5a4f"));
    if (es.isEmpty()) {
        p.setPen(QColor("#8a919b"));
        p.drawText(rect(), Qt::AlignCenter, TR("Húzz ide elemeket a hangtárból (jobb oldalt) vagy a főablak hangtárából."));
    }
}

void MdTimeline::mousePressEvent(QMouseEvent *e)
{
    const QPointF pt = e->position();
    const auto &es = page_->entries();
    drag_ = None;
    if (e->button() != Qt::LeftButton) return;
    if (envMode) {
        // Hangerőgörbe: meglévő pont megfogása, vagy új pont a sávon.
        for (int i = es.size() - 1; i >= 0; --i) {
            if (!clipRect(i).adjusted(-6, -6, 6, 6).contains(pt)) continue;
            int k = envPointAt(i, pt);
            if (k < 0) {
                if (!clipRect(i).contains(pt)) continue;
                k = page_->envAdd(i, envMsAt(i, pt.x()), envDbAt(i, pt.y()));
            }
            drag_ = EnvPoint;
            dragIdx_ = i;
            dragPoint_ = k;
            page_->select(i);
            return;
        }
    }
    for (int i = es.size() - 1; i >= 0; --i) {
        if (!es[i].vtPath.isEmpty() && vtRect(i).contains(pt)) {
            drag_ = Vt;
            dragIdx_ = i;
            dragOrigin_ = es[i].vtOffsetMs;
            dragX_ = pt.x();
            page_->selectVt(i);
            return;
        }
    }
    for (int i = es.size() - 1; i >= 0; --i) {
        const QRectF r = clipRect(i);
        if (!r.contains(pt)) continue;
        page_->select(i);   // a kattintott elem (a törlés is ezt törli)
        dragIdx_ = i;
        dragX_ = pt.x();
        pressX_ = pt.x();
        if (i == 0 && pt.x() >= r.left() + 6 && pt.x() <= r.right() - 6) {
            drag_ = Body;   // az első elem: csak sávot válthat
            dragOrigin_ = 0;
            return;
        }
        if (pt.x() > r.right() - 6) {
            drag_ = Right;
            dragOrigin_ = es[i].end;
        } else if (pt.x() < r.left() + 6) {
            drag_ = Left;
            dragOrigin_ = es[i].cueIn;
        } else if (i > 0) {
            drag_ = Body;
            dragOrigin_ = page_->startOf(i);
        }
        return;
    }
    drag_ = Head;
    page_->setPlayhead(std::max<qint64>(0, msAt(pt.x())));
}

void MdTimeline::mouseMoveEvent(QMouseEvent *e)
{
    const QPointF pt = e->position();
    if (drag_ == None) {
        Qt::CursorShape c = Qt::ArrowCursor;
        const auto &es = page_->entries();
        for (int i = 0; i < es.size(); ++i) {
            const QRectF r = clipRect(i);
            if (r.contains(pt)) c = (pt.x() > r.right() - 6 || pt.x() < r.left() + 6) ? Qt::SizeHorCursor : i > 0 ? Qt::OpenHandCursor : Qt::ArrowCursor;
            if (!es[i].vtPath.isEmpty() && vtRect(i).contains(pt)) c = Qt::OpenHandCursor;
        }
        setCursor(c);
        return;
    }
    if (drag_ == EnvPoint) {
        dragPoint_ = page_->envMove(dragIdx_, dragPoint_, envMsAt(dragIdx_, pt.x()), envDbAt(dragIdx_, pt.y()));
        return;
    }
    if (drag_ == Body) {
        // Függőlegesen: a másik zenesávra
        const int lane = laneAt(pt.y());
        if (lane < 2 && lane != page_->laneOf(dragIdx_)) page_->setLane(dragIdx_, lane);
    }
    const qint64 delta = qint64((pt.x() - dragX_) / pps * 1000.0);
    const qint64 snap = 10;   // 10 ms-os lépés
    const qint64 v = (dragOrigin_ + delta) / snap * snap;
    switch (drag_) {
    case Body: page_->moveClip(dragIdx_, v); break;
    case Left: page_->trimStart(dragIdx_, v); break;
    case Right: page_->trimEnd(dragIdx_, v); break;
    case Vt: page_->moveVt(dragIdx_, v); break;
    case Head: page_->setPlayhead(std::max<qint64>(0, msAt(pt.x()))); break;
    default: break;
    }
}

void MdTimeline::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (!envMode) return;
    const auto &es = page_->entries();
    for (int i = es.size() - 1; i >= 0; --i) {
        const int k = envPointAt(i, e->position());
        if (k >= 0) {
            page_->envRemove(i, k);
            drag_ = None;
            return;
        }
    }
}

void MdTimeline::contextMenuEvent(QContextMenuEvent *e)
{
    if (!envMode) return;
    const auto &es = page_->entries();
    for (int i = es.size() - 1; i >= 0; --i) {
        const int k = envPointAt(i, e->pos());
        if (k >= 0) {
            page_->envRemove(i, k);
            return;
        }
    }
}

void MdTimeline::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasFormat(QString::fromLatin1(kItemsMime))) e->acceptProposedAction();
}

void MdTimeline::dragMoveEvent(QDragMoveEvent *e)
{
    if (!e->mimeData()->hasFormat(QString::fromLatin1(kItemsMime))) return;
    dropX_ = e->position().x();
    dropLane_ = laneAt(e->position().y());
    e->acceptProposedAction();
    update();
}

void MdTimeline::dropEvent(QDropEvent *e)
{
    const QVector<int> ids = decodeIds(e->mimeData()->data(QString::fromLatin1(kItemsMime)));
    const qint64 ms = std::max<qint64>(0, msAt(e->position().x()));
    const int lane = laneAt(e->position().y());
    dropX_ = -1;
    if (lane == 2 && !ids.isEmpty()) page_->dropVoice(ids.first(), ms);
    else page_->dropItems(ids, ms, lane);
    e->acceptProposedAction();
    update();
}

void MdTimeline::wheelEvent(QWheelEvent *e)
{
    const int d = e->angleDelta().y() != 0 ? e->angleDelta().y() : e->angleDelta().x();
    if (d == 0) return;
    if (e->modifiers() & Qt::ShiftModifier || e->angleDelta().x() != 0) {
        offset = std::max<qint64>(0, offset - qint64(d / 120.0 * 80.0 / pps * 1000.0));
    } else {
        const qint64 at = msAt(e->position().x());
        pps = std::clamp(pps * (d > 0 ? 1.25 : 0.8), 0.2, 400.0);
        offset = std::max<qint64>(0, at - qint64(e->position().x() / pps * 1000.0));
    }
    if (viewChanged) viewChanged();
    update();
    e->accept();
}

// ------------------------------------------------------------------ az oldal

MixdownPage::MixdownPage(Database *db, eter::Engine *engine, QWidget *parent)
    : QWidget(parent), db_(db), engine_(engine), cancel_(std::make_shared<std::atomic<bool>>(false))
{
    smart_ = loadSmartMix(db_);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Felső sáv
    auto *bar = new QFrame;
    bar->setObjectName("panel");
    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(10, 6, 10, 6);
    bl->setSpacing(8);
    bl->addWidget(new QLabel(TR("Név")));
    name_ = new QLineEdit(TR("Mixdown %1").arg(QDate::currentDate().toString(Qt::ISODate)));
    name_->setMinimumWidth(240);
    bl->addWidget(name_);
    auto *newBtn = new QPushButton(TR("Új"));
    auto *openBtn = new QPushButton(TR("Megnyitás…"));
    auto *saveBtn = new QPushButton(TR("Projekt mentése"));
    bl->addWidget(newBtn);
    bl->addWidget(openBtn);
    bl->addWidget(saveBtn);
    bl->addSpacing(12);
    playBtn_ = new QPushButton(TR("▶ Előhallgatás innen"));
    playBtn_->setToolTip(TR("A piros vonaltól másfél percet kever ki és játszik le az előhallgatáson"));
    bl->addWidget(playBtn_);
    envBtn_ = new QPushButton(TR("Hangerőgörbe"));
    envBtn_->setCheckable(true);
    envBtn_->setToolTip(TR("A sávok hangerejének rajzolása: kattints egy sávra, és új pont jelenik meg; húzd a pontot (fel-le a "
                           "szint, oldalra az idő); jobb gomb vagy dupla kattintás: a pont törlése. Csak a mixdownra hat."));
    bl->addWidget(envBtn_);
    bl->addStretch(1);
    auto *lenLabel = new QLabel(TR("Hossz"));
    lenLabel->setObjectName("muted");
    bl->addWidget(lenLabel);
    total_label_ = new QLabel(QStringLiteral("0:00.0"));
    total_label_->setObjectName("mono");
    total_label_->setStyleSheet(QStringLiteral("font-size: 18px; font-weight: 600;"));
    bl->addWidget(total_label_);
    bl->addSpacing(8);
    auto *exportBtn = new QPushButton(TR("Mixdown készítése…"));
    exportBtn->setProperty("accent", true);
    bl->addWidget(exportBtn);
    root->addWidget(bar);

    auto *split = new QSplitter(Qt::Horizontal);
    // Bal: a sorrend
    auto *left = new QWidget;
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(8, 8, 8, 8);
    auto *cap = new QLabel(TR("SORREND"));
    cap->setObjectName("caption");
    ll->addWidget(cap);
    seq_ = new MdSeqList;
    seq_->dropped = [this](const QVector<int> &ids, int row) { addItems(ids, row); };
    seq_->reordered = [this](const QVector<int> &order) { reorder(order); };
    ll->addWidget(seq_, 1);
    auto *hint = new QLabel(TR("Húzd ide az elemeket a hangtárból (jobb oldalt, vagy a főablakból) · dupla kattintás a hangtárban: hozzáadás a végére"));
    hint->setObjectName("small");
    hint->setWordWrap(true);
    ll->addWidget(hint);
    auto *lb = new QHBoxLayout;
    vtBtn_ = new QToolButton;
    vtBtn_->setText(TR("● Voice track"));
    vtBtn_->setPopupMode(QToolButton::InstantPopup);
    auto *vtMenu = new QMenu(vtBtn_);
    vtMenu->addAction(TR("Felvétel…"), this, &MixdownPage::recordVoiceTrack);
    vtMenu->addAction(TR("Fájlból…"), this, &MixdownPage::pickVoiceFile);
    vtMenu->addAction(TR("A hangtárból…"), this, &MixdownPage::pickVoiceLibrary);
    vtMenu->addSeparator();
    vtMenu->addAction(TR("Voice track törlése"), this, [this] { if (selected_ >= 0) { vtSelected_ = false; setVoiceTrack(selected_, {}, {}); } });
    vtBtn_->setMenu(vtMenu);
    vtBtn_->setToolTip(TR("Voice track a kijelölés helyére: kattints az idővonalon az elemre ott, ahol a voice tracknek kezdődnie kell"));
    auto *delBtn = new QPushButton(TR("Törlés"));
    auto *upBtn = new QPushButton(QStringLiteral("↑"));
    auto *downBtn = new QPushButton(QStringLiteral("↓"));
    upBtn->setAccessibleName(TR("Fel"));
    downBtn->setAccessibleName(TR("Le"));
    lb->addWidget(vtBtn_);
    lb->addWidget(delBtn);
    lb->addWidget(upBtn);
    lb->addWidget(downBtn);
    lb->addStretch(1);
    ll->addLayout(lb);
    split->addWidget(left);

    // Közép: idővonal és az átmenet adatai
    auto *mid = new QWidget;
    auto *ml = new QVBoxLayout(mid);
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(0);
    envHint_ = new QLabel(TR("Hangerőgörbe: kattints egy sávra, és új pont jelenik meg · húzd a pontot: fel-le a szint (0 … −40 dB), "
                             "oldalra az idő · jobb gomb vagy dupla kattintás: a pont törlése · a görbe csak a mixdownra hat, az elemet nem módosítja."));
    envHint_->setWordWrap(true);
    envHint_->setStyleSheet(QStringLiteral("QLabel { background: #262626; color: #f2f2f2; border: 1px solid #4a4a4a; border-radius: 4px; padding: 6px 10px; }"));
    envHint_->hide();
    ml->addWidget(envHint_);
    timeline_ = new MdTimeline(this);
    timeline_->setObjectName("mdTimeline");
    ml->addWidget(timeline_, 1);
    scroll_ = new QScrollBar(Qt::Horizontal);
    ml->addWidget(scroll_);
    auto *tr = new QFrame;
    tr->setObjectName("panel");
    auto *tl = new QVBoxLayout(tr);
    tl->setContentsMargins(10, 8, 10, 8);
    auto *trHead = new QHBoxLayout;
    trTitle_ = new QLabel;
    trTitle_->setStyleSheet(QStringLiteral("font-weight: 600;"));
    trKind_ = new QLabel;
    trKind_->setObjectName("muted");
    trPlay_ = new QPushButton(TR("▶ Átmenet meghallgatása"));
    trAuto_ = new QPushButton(TR("Vissza automatikusra"));
    trHead->addWidget(trTitle_);
    trHead->addWidget(trKind_);
    trHead->addStretch(1);
    trHead->addWidget(trAuto_);
    trHead->addWidget(trPlay_);
    tl->addLayout(trHead);
    auto *trRow = new QHBoxLayout;
    auto spin = [&](const QString &label, double lo, double hi, const QString &suffix, int dec = 1) {
        auto *l = new QLabel(label);
        l->setObjectName("muted");
        auto *s = new QDoubleSpinBox;
        s->setRange(lo, hi);
        s->setDecimals(dec);
        s->setSingleStep(dec ? 0.1 : 1);
        s->setSuffix(suffix);
        s->setKeyboardTracking(false);
        trRow->addWidget(l);
        trRow->addWidget(s);
        return s;
    };
    mixOut_ = spin(TR("Mix pont"), 0, 36000, TR(" mp"));
    fadeOut_ = spin(TR("Kiúsztatás"), 0, 60, TR(" mp"));
    endAt_ = spin(TR("Vége"), 0, 36000, TR(" mp"));
    cueIn_ = spin(TR("Következő kezdőpontja"), 0, 600, TR(" mp"));
    vtStart_ = spin(TR("VT kezdés"), -600, 600, TR(" mp"));
    vtDuck_ = spin(TR("Zene alatta"), -40, 0, TR(" dB"), 0);
    trRow->addStretch(1);
    tl->addLayout(trRow);
    ml->addWidget(tr);
    split->addWidget(mid);

    // Jobb: hangtár
    auto *right = new QWidget;
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(8, 8, 8, 8);
    auto *rs = new QHBoxLayout;
    libSearch_ = new QLineEdit;
    libSearch_->setPlaceholderText(TR("Keresés…"));
    libSearch_->setClearButtonEnabled(true);
    libCat_ = new QComboBox;
    libCat_->setMaximumWidth(140);
    libCat_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    libCat_->setMinimumContentsLength(8);
    libSearch_->setMinimumWidth(110);
    rs->addWidget(libSearch_, 1);
    rs->addWidget(libCat_);
    rl->addLayout(rs);
    lib_ = new MdLibList;
    lib_->setDragEnabled(true);
    lib_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    rl->addWidget(lib_, 1);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 7);
    split->setStretchFactor(2, 3);
    split->setSizes({300, 820, 300});
    root->addWidget(split, 1);

    timer_ = new QTimer(this);
    timer_->setInterval(50);
    connect(timer_, &QTimer::timeout, this, &MixdownPage::tick);

    timeline_->viewChanged = [this] {
        scroll_->blockSignals(true);
        scroll_->setRange(0, int(std::max<qint64>(0, total_ - qint64(timeline_->width() / timeline_->pps * 1000.0) + 10000) / 100));
        scroll_->setPageStep(int(timeline_->width() / timeline_->pps * 10.0));
        scroll_->setValue(int(timeline_->offset / 100));
        scroll_->blockSignals(false);
    };
    connect(scroll_, &QScrollBar::valueChanged, this, [this](int v) {
        timeline_->offset = qint64(v) * 100;
        timeline_->update();
    });
    connect(seq_, &QListWidget::currentRowChanged, this, [this](int r) {
        if (loading_ || r < 0 || r >= entries_.size()) return;
        select(r);
        // A listában kijelölt elemre ugrik a lejátszási pozíció (ha nem azon állt): oda kerül a voice track felvétele.
        const qint64 from = startOf(r), to = from + entries_[r].end - entries_[r].cueIn;
        if (playhead_ < from || playhead_ > to) setPlayhead(from);
    });
    connect(envBtn_, &QPushButton::toggled, this, [this](bool on) {
        timeline_->envMode = on;
        envHint_->setVisible(on);
        timeline_->update();
    });
    connect(delBtn, &QPushButton::clicked, this, &MixdownPage::deleteSelection);
    connect(upBtn, &QPushButton::clicked, this, [this] { moveSelected(-1); });
    connect(downBtn, &QPushButton::clicked, this, [this] { moveSelected(1); });
    connect(newBtn, &QPushButton::clicked, this, &MixdownPage::newProject);
    connect(saveBtn, &QPushButton::clicked, this, &MixdownPage::saveProject);
    connect(openBtn, &QPushButton::clicked, this, &MixdownPage::openProjectMenu);
    connect(exportBtn, &QPushButton::clicked, this, &MixdownPage::openExportDialog);
    connect(playBtn_, &QPushButton::clicked, this, [this] {
        if (previewing_) stopPreview();
        else startPreview(playhead_, 90000);
    });
    connect(trPlay_, &QPushButton::clicked, this, [this] {
        if (selected_ < 0 || selected_ + 1 >= entries_.size()) return;
        const qint64 at = starts_[selected_ + 1];
        const auto &e = entries_[selected_];
        const qint64 extra = e.vtPath.isEmpty() ? 0 : std::max<qint64>(0, e.vtOffsetMs + e.vtDurMs);
        startPreview(std::max<qint64>(0, at - 8000), 16000 + extra);
    });
    connect(trAuto_, &QPushButton::clicked, this, [this] {
        if (selected_ < 0) return;
        entries_[selected_].manualMix = false;
        if (selected_ + 1 < entries_.size()) entries_[selected_ + 1].manualCue = false;
        recompute();
    });
    auto edited = [this](auto fn) {
        return [this, fn](double v) {
            if (loading_ || selected_ < 0) return;
            fn(v);
            recompute();
        };
    };
    connect(mixOut_, &QDoubleSpinBox::valueChanged, this, edited([this](double v) {
        Entry &e = entries_[selected_];
        e.mixOut = std::max<qint64>(qint64(v * 1000), e.cueIn + 100);
        e.manualMix = true;
    }));
    connect(fadeOut_, &QDoubleSpinBox::valueChanged, this, edited([this](double v) {
        Entry &e = entries_[selected_];
        e.fadeOut = int(std::clamp<qint64>(qint64(v * 1000), 0, e.end - e.cueIn));
        e.manualMix = true;
    }));
    connect(endAt_, &QDoubleSpinBox::valueChanged, this, edited([this](double v) { trimEnd(selected_, qint64(v * 1000)); }));
    connect(cueIn_, &QDoubleSpinBox::valueChanged, this, edited([this](double v) {
        if (selected_ + 1 < entries_.size()) trimStart(selected_ + 1, qint64(v * 1000));
    }));
    connect(vtStart_, &QDoubleSpinBox::valueChanged, this, edited([this](double v) { entries_[selected_].vtOffsetMs = qint64(v * 1000); }));
    connect(vtDuck_, &QDoubleSpinBox::valueChanged, this, edited([this](double v) { entries_[selected_].vtDuckDb = v; }));

    connect(libSearch_, &QLineEdit::textChanged, this, &MixdownPage::refreshLibrary);
    connect(libCat_, &QComboBox::activated, this, &MixdownPage::refreshLibrary);
    connect(lib_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) { addItems({it->data(Qt::UserRole).toInt()}); });
    connect(db_, &Database::itemsChanged, this, &MixdownPage::refreshLibrary);
    connect(db_, &Database::categoriesChanged, this, &MixdownPage::refreshLibrary);
    refreshLibrary();
    recompute();
}

MixdownPage::~MixdownPage()
{
    cancel_->store(true);
    stopPreview();
    for (auto &t : threads_)
        if (t.joinable()) t.join();
}

void MixdownPage::reapThreads()
{
    // A befejezett szálakat a destruktor várja meg; itt csak a lista ne nőjön a végtelenségig.
    if (threads_.size() < 64) return;
    for (auto &t : threads_)
        if (t.joinable()) t.join();
    threads_.clear();
}

const MixdownPage::Wave *MixdownPage::wave(const QString &path) const
{
    auto it = waves_.constFind(path);
    return it == waves_.constEnd() ? nullptr : &it.value();
}

void MixdownPage::requestWave(const Item &item)
{
    if (waves_.contains(item.path)) return;
    waves_.insert(item.path, Wave());
    const QString path = item.path;
    const int bins = int(std::clamp<qint64>(item.durationMs / 50, 400, 20000));
    auto cancel = cancel_;
    QPointer<MixdownPage> self(this);
    reapThreads();
    threads_.emplace_back([self, path, bins, cancel] {
        std::vector<float> peaks, env;
        int64_t total = 0;
        if (!eter::scanPeaks(path.toStdString(), bins, peaks, total, *cancel, &env) || cancel->load()) return;
        Wave w;
        w.peaks = QVector<float>(peaks.begin(), peaks.end());
        w.durMs = total * 1000 / kRate;
        w.profile = eter::analyzeMixProfile(env, w.durMs);
        w.ready = true;
        QMetaObject::invokeMethod(qApp, [self, path, w] {
            if (!self) return;
            self->waves_[path] = w;
            for (Entry &e : self->entries_)
                if (e.item.path == path) e.durMs = w.durMs;
            self->recompute();
        }, Qt::QueuedConnection);
    });
}

void MixdownPage::addItems(const QVector<int> &ids, int at)
{
    if (at < 0 || at > entries_.size()) at = int(entries_.size());
    // A meglévők a sávjukon maradnak; az újak az előttük lévőtől eltérő sávra kerülnek.
    for (int i = 0; i < entries_.size(); ++i) entries_[i].lane = laneOf(i);
    int lane = at > 0 ? 1 - entries_[at - 1].lane : 0;
    int added = 0;
    for (int id : ids) {
        const Item it = db_->item(id);
        if (!it.id || it.path.isEmpty()) continue;
        Entry e;
        e.item = it;
        e.durMs = it.durationMs;
        e.end = it.cueOutMs > 0 ? it.cueOutMs : it.durationMs;
        e.mixOut = e.end;
        e.cueIn = std::max<qint64>(0, it.cueInMs);
        e.env = env::fromString(it.volumeEnv);   // az elem saját görbéjéből indul; innentől csak a mixdowné
        e.lane = lane;
        lane = 1 - lane;
        entries_.insert(at + added, e);
        ++added;
        requestWave(it);
    }
    if (!added) return;
    // A szomszédos átmenetek is újraszámolódnak (ha nem kézzel állítottak).
    selected_ = at + added - 1;
    recompute();
}

// Egy átmenet automatikus értékei (i → i+1), az okos mix szerint; a Mix editortól független.
void MixdownPage::autoTransition(int i)
{
    Entry &e = entries_[i];
    const Wave *w = wave(e.item.path);
    const qint64 dur = w && w->ready ? w->durMs : (e.durMs > 0 ? e.durMs : e.item.durationMs);
    e.durMs = dur;
    const bool hasNext = i + 1 < entries_.size();
    const Entry *n = hasNext ? &entries_[i + 1] : nullptr;
    const Wave *wn = n ? wave(n->item.path) : nullptr;
    const eter::MixProfile none;
    if (!e.manualCue) {
        e.cueIn = e.item.cueInMs > 0 ? e.item.cueInMs : 0;
        if (e.item.cueInMs <= 0 && w && w->ready && w->profile.valid && isMusic(e.item)) {
            const eter::SmartMixPlan sp = eter::planSmartMix(w->profile, w->profile, 0, 0, 0, 0, smart_);
            if (sp.valid) e.cueIn = sp.cueInMs;
        }
    }
    if (e.manualMix) return;
    if (isMusic(e.item) && w && w->ready && w->profile.valid && e.item.cueOutMs <= 0) {
        const bool nextMusic = n && isMusic(n->item);
        const eter::SmartMixPlan sp = eter::planSmartMix(w->profile, nextMusic && wn && wn->ready ? wn->profile : none, e.item.bpm,
                                                         n ? n->item.bpm : 0, e.item.musicKey, n ? n->item.musicKey : 0, smart_);
        if (sp.valid) {
            e.end = sp.outEndMs;
            e.mixOut = hasNext && nextMusic ? sp.mixOutMs : sp.outEndMs;
            e.fadeOut = hasNext && nextMusic ? sp.fadeOutMs : std::min<int>(300, int(sp.outEndMs - e.cueIn));
            return;
        }
    }
    // Nem zenei elem (vagy még nincs elemzés): a végéig szól, a következő utána indul.
    e.end = e.item.cueOutMs > 0 ? std::min(e.item.cueOutMs, dur) : dur;
    e.mixOut = e.end;
    e.fadeOut = 0;
}

void MixdownPage::recompute()
{
    for (int i = 0; i < entries_.size(); ++i) autoTransition(i);
    starts_.resize(entries_.size());
    qint64 t = 0;
    total_ = 0;
    for (int i = 0; i < entries_.size(); ++i) {
        Entry &e = entries_[i];
        e.end = std::max(e.end, e.cueIn + 100);
        // A mix pont a vége után is lehet: a következő később indul (közben egy másik sávon szólhat valami).
        e.mixOut = std::max(e.mixOut, e.cueIn + 100);
        starts_[i] = t;
        total_ = std::max(total_, t + e.end - e.cueIn);
        if (!e.vtPath.isEmpty()) {
            const qint64 at = (i + 1 < entries_.size() ? t + e.mixOut - e.cueIn : t + e.end - e.cueIn) + e.vtOffsetMs;
            total_ = std::max(total_, at + e.vtDurMs);
        }
        t += e.mixOut - e.cueIn;
    }
    if (selected_ >= entries_.size()) selected_ = int(entries_.size()) - 1;
    total_label_->setText(mmss(total_, true));
    refreshList();
    showTransition();
    if (timeline_->viewChanged) timeline_->viewChanged();
    timeline_->update();
}

void MixdownPage::refreshList()
{
    loading_ = true;
    seq_->clear();
    for (int i = 0; i < entries_.size(); ++i) {
        const Entry &e = entries_[i];
        QString how;
        if (i + 1 < entries_.size()) {
            if (e.mixOut > e.end + 50) how = TR("a következő %1 mp-cel a vége után").arg(QString::number((e.mixOut - e.end) / 1000.0, 'f', 1));
            else if (e.mixOut >= e.end) how = TR("a végéig szól");
            else how = e.manualMix ? TR("kézzel") : TR("automatikus");
        }
        if (!e.vtPath.isEmpty()) how += (how.isEmpty() ? QString() : QStringLiteral(" · ")) + TR("VT ●");
        auto *it = new QListWidgetItem(QStringLiteral("%1.  %2   %3\n      %4%5").arg(i + 1).arg(e.item.title, mmss(e.end - e.cueIn), e.item.artist,
                                                                                    how.isEmpty() ? QString() : QStringLiteral("  ·  ") + how));
        it->setData(Qt::UserRole, i);
        seq_->addItem(it);
    }
    if (selected_ >= 0) seq_->setCurrentRow(selected_);
    loading_ = false;
}

void MixdownPage::showTransition()
{
    loading_ = true;
    const bool has = selected_ >= 0 && selected_ < entries_.size();
    const bool hasNext = has && selected_ + 1 < entries_.size();
    for (QWidget *w : std::initializer_list<QWidget *>{mixOut_, fadeOut_, endAt_, vtStart_, vtDuck_, trAuto_}) w->setEnabled(has);
    cueIn_->setEnabled(hasNext);
    trPlay_->setEnabled(hasNext);
    if (!has) {
        trTitle_->setText(TR("Nincs kijelölt átmenet"));
        trKind_->clear();
        loading_ = false;
        return;
    }
    const Entry &e = entries_[selected_];
    trTitle_->setText(hasNext ? TR("Átmenet %1 → %2").arg(selected_ + 1).arg(selected_ + 2) : TR("Az utolsó elem vége"));
    trKind_->setText(hasNext ? QStringLiteral("%1 → %2 · %3").arg(e.item.title, entries_[selected_ + 1].item.title,
                                                                 e.manualMix ? TR("kézzel állítva") : TR("automatikus (okos mix)"))
                             : e.item.title);
    mixOut_->setValue(e.mixOut / 1000.0);
    fadeOut_->setValue(e.fadeOut / 1000.0);
    endAt_->setValue(e.end / 1000.0);
    cueIn_->setValue(hasNext ? entries_[selected_ + 1].cueIn / 1000.0 : 0.0);
    vtStart_->setValue(e.vtOffsetMs / 1000.0);
    vtDuck_->setValue(e.vtDuckDb);
    vtStart_->setEnabled(!e.vtPath.isEmpty());
    vtDuck_->setEnabled(!e.vtPath.isEmpty());
    loading_ = false;
}

void MixdownPage::select(int i)
{
    if (i < 0 || i >= entries_.size()) return;
    selected_ = i;
    vtSelected_ = false;
    if (seq_->currentRow() != i) {
        loading_ = true;
        seq_->setCurrentRow(i);
        loading_ = false;
    }
    showTransition();
    timeline_->update();
}

void MixdownPage::moveClip(int i, qint64 startMs)
{
    if (i <= 0 || i >= entries_.size()) return;
    Entry &p = entries_[i - 1];
    p.mixOut = std::max<qint64>(p.cueIn + (startMs - starts_[i - 1]), p.cueIn + 100);
    p.manualMix = true;
    recompute();
}

void MixdownPage::trimStart(int i, qint64 cueInMs)
{
    if (i < 0 || i >= entries_.size()) return;
    Entry &e = entries_[i];
    e.cueIn = std::clamp<qint64>(cueInMs, 0, e.end - 500);
    e.manualCue = true;
    e.manualMix = true;   // a vége és a mix pontja is maradjon
    recompute();
}

void MixdownPage::trimEnd(int i, qint64 endMs)
{
    if (i < 0 || i >= entries_.size()) return;
    Entry &e = entries_[i];
    const qint64 dur = e.durMs > 0 ? e.durMs : e.item.durationMs;
    e.end = std::clamp<qint64>(endMs, e.cueIn + 500, dur);
    e.fadeOut = int(std::min<qint64>(e.fadeOut, e.end - e.cueIn));
    e.manualMix = true;
    recompute();
}

void MixdownPage::moveVt(int i, qint64 offsetMs)
{
    if (i < 0 || i >= entries_.size()) return;
    entries_[i].vtOffsetMs = std::clamp<qint64>(offsetMs, -600000, 600000);
    recompute();
}

int MixdownPage::laneOf(int i) const
{
    if (i < 0 || i >= entries_.size()) return 0;
    return entries_[i].lane >= 0 ? entries_[i].lane : i % 2;
}

void MixdownPage::setLane(int i, int lane)
{
    if (i < 0 || i >= entries_.size()) return;
    entries_[i].lane = std::clamp(lane, 0, 1);
    timeline_->update();
}

int MixdownPage::envAdd(int i, qint64 ms, double db)
{
    if (i < 0 || i >= entries_.size()) return -1;
    env::Envelope &e = entries_[i].env;
    if (e.size() >= env::kMaxPoints) return -1;
    // Az első pont: a görbe addig a 0 dB-en marad, utána ide megy.
    e << env::Point{ms, db};
    env::normalize(e);
    timeline_->update();
    for (int k = 0; k < e.size(); ++k)
        if (e[k].ms == ms) return k;
    return -1;
}

int MixdownPage::envMove(int i, int point, qint64 ms, double db)
{
    if (i < 0 || i >= entries_.size()) return -1;
    env::Envelope &e = entries_[i].env;
    if (point < 0 || point >= e.size()) return -1;
    e[point].ms = ms;
    e[point].db = db;
    const env::Point moved = e[point];
    env::normalize(e);
    timeline_->update();
    // A rendezés után ugyanaz a pont (az idő és a szint alapján)
    int best = -1;
    for (int k = 0; k < e.size(); ++k)
        if (e[k].ms == moved.ms && (best < 0 || std::abs(e[k].db - moved.db) < std::abs(e[best].db - moved.db))) best = k;
    return best;
}

void MixdownPage::envRemove(int i, int point)
{
    if (i < 0 || i >= entries_.size()) return;
    env::Envelope &e = entries_[i].env;
    if (point < 0 || point >= e.size()) return;
    e.removeAt(point);
    timeline_->update();
}

void MixdownPage::dropItems(const QVector<int> &ids, qint64 ms, int lane)
{
    // Ahová az egeret engedték: az ott kezdődő elemek elé kerül, és pontosan ott kezdődik (az előző mix pontja
    // kézzel ide áll).
    // Az utána következők a helyükön maradnak (ha a behúzott elem odáig elér; egy másik sávra húzva így rétegezhető,
    // például szignál a zene fölé).
    int k = 0;
    while (k < entries_.size() && starts_.value(k) < ms) ++k;
    const qint64 nextStart = k < entries_.size() ? starts_[k] : -1;
    const int before = int(entries_.size());
    addItems(ids, k);
    const int added = int(entries_.size()) - before;
    if (added <= 0) return;
    for (int j = k; j < k + added; ++j) entries_[j].lane = std::clamp(lane, 0, 1);
    if (k > 0) {
        Entry &p = entries_[k - 1];
        p.mixOut = std::max<qint64>(p.cueIn + (ms - starts_[k - 1]), p.cueIn + 100);
        p.manualMix = true;
    }
    if (nextStart >= 0) {
        // A behúzottak egymás után; az utolsó után a régi következő az eredeti idejében indul, ha az utána van.
        qint64 t = ms;
        for (int j = k; j < k + added - 1; ++j) t += entries_[j].end - entries_[j].cueIn;
        Entry &last = entries_[k + added - 1];
        if (nextStart > t + 100) {
            last.mixOut = last.cueIn + (nextStart - t);
            last.manualMix = true;
        }
    }
    selected_ = k;
    recompute();
}

void MixdownPage::dropVoice(int itemId, qint64 ms)
{
    if (entries_.isEmpty()) return;
    const Item it = db_->item(itemId);
    if (!it.id) return;
    attachVoiceAt(it.path, it.title, ms);
}

// A voice track a legközelebbi átmenethez (a következő elem kezdéséhez, az utolsónál az elem végéhez) tartozik,
// és pontosan a megadott időpontban kezdődik.
void MixdownPage::attachVoiceAt(const QString &path, const QString &title, qint64 ms)
{
    if (entries_.isEmpty()) return;
    int best = 0;
    qint64 bestD = std::numeric_limits<qint64>::max(), anchor = 0;
    for (int i = 0; i < entries_.size(); ++i) {
        const qint64 a = i + 1 < entries_.size() ? starts_[i + 1] : starts_[i] + entries_[i].end - entries_[i].cueIn;
        if (std::llabs(a - ms) < bestD) {
            bestD = std::llabs(a - ms);
            best = i;
            anchor = a;
        }
    }
    setVoiceTrack(best, path, title);
    if (!entries_[best].vtPath.isEmpty()) {
        moveVt(best, ms - anchor);
        selectVt(best);
    }
}

// Hová kerül az új voice track: a lejátszási pozícióra (az idővonalon kattintás oda teszi; a listában kijelölt elemnél
// az elem elejére ugrik, ha nem azon állt). Kijelölt voice tracknél a helyén cseréli.
qint64 MixdownPage::voiceTargetMs() const
{
    if (vtSelected_ && selected_ >= 0 && selected_ < entries_.size()) {
        const Entry &e = entries_[selected_];
        const qint64 a = selected_ + 1 < entries_.size() ? starts_[selected_ + 1] : starts_[selected_] + e.end - e.cueIn;
        return a + e.vtOffsetMs;
    }
    return playhead_;
}

void MixdownPage::placeVoice(const QString &path, const QString &title)
{
    const qint64 at = voiceTargetMs();
    if (vtSelected_ && selected_ >= 0) {
        const int i = selected_;
        const qint64 off = entries_[i].vtOffsetMs;
        setVoiceTrack(i, path, title);
        if (!entries_[i].vtPath.isEmpty()) {
            moveVt(i, off);
            selectVt(i);
        }
        return;
    }
    attachVoiceAt(path, title, at);
}

void MixdownPage::setPlayhead(qint64 ms)
{
    playhead_ = std::clamp<qint64>(ms, 0, std::max<qint64>(0, total_));
    timeline_->update();
}

qint64 MixdownPage::playheadMs() const
{
    if (previewing_) {
        const auto &src = engine_->source(eter::Engine::Pfl);
        return previewFrom_ + src.position() * 1000 / kRate;
    }
    return playhead_;
}

void MixdownPage::selectVt(int i)
{
    select(i);
    vtSelected_ = i >= 0 && i < entries_.size() && !entries_[i].vtPath.isEmpty();
    timeline_->update();
}

void MixdownPage::deleteSelection()
{
    if (vtSelected_ && selected_ >= 0 && selected_ < entries_.size()) {
        vtSelected_ = false;
        setVoiceTrack(selected_, {}, {});
        emit message(TR("Voice track törölve."));
        return;
    }
    remove();
}

void MixdownPage::remove()
{
    vtSelected_ = false;
    if (selected_ < 0 || selected_ >= entries_.size()) return;
    entries_.removeAt(selected_);
    if (selected_ > 0 && selected_ >= entries_.size()) --selected_;
    recompute();
}

void MixdownPage::moveSelected(int delta)
{
    const int j = selected_ + delta;
    if (selected_ < 0 || j < 0 || j >= entries_.size()) return;
    std::swap(entries_[selected_], entries_[j]);
    for (Entry &e : entries_) e.lane = -1;   // új sorrend: a sávok újra váltakoznak
    // Új szomszédok: az átmenet újra automatikus.
    for (int k : {selected_ - 1, selected_, j - 1, j})
        if (k >= 0 && k < entries_.size()) entries_[k].manualMix = false;
    selected_ = j;
    recompute();
}

void MixdownPage::reorder(const QVector<int> &order)
{
    if (order.size() != entries_.size()) return;
    QVector<Entry> n;
    for (int k : order) n << entries_[k];
    for (int i = 0; i < n.size(); ++i)
        if (i == 0 || order[i] != order[i - 1] + 1) {
            n[i].manualMix = false;
            if (i > 0) n[i - 1].manualMix = false;
        }
    for (Entry &e : n) e.lane = -1;
    entries_ = n;
    recompute();
}

eter::MixdownJob MixdownPage::buildJob() const
{
    eter::MixdownJob job;
    auto frames = [](qint64 ms) { return ms * kRate / 1000; };
    for (int i = 0; i < entries_.size(); ++i) {
        const Entry &e = entries_[i];
        eter::MixdownClip c;
        c.path = e.item.path.toStdString();
        c.start = frames(starts_[i]);
        c.in = frames(e.cueIn);
        c.end = frames(e.end);
        c.fadeOut = frames(e.fadeOut);
        c.env = env::toSource(e.env, kRate);
        job.clips.push_back(c);
        if (!e.vtPath.isEmpty()) {
            eter::MixdownClip v;
            v.path = e.vtPath.toStdString();
            v.voice = true;
            const qint64 at = (i + 1 < entries_.size() ? starts_[i + 1] : starts_[i] + e.end - e.cueIn) + e.vtOffsetMs;
            v.start = frames(std::max<qint64>(0, at));
            v.in = at < 0 ? frames(-at) : 0;
            v.end = frames(e.vtDurMs);
            v.duck = float(env::toGain(e.vtDuckDb));
            job.clips.push_back(v);
        }
    }
    return job;
}

void MixdownPage::startPreview(qint64 fromMs, qint64 lengthMs)
{
    stopPreview();
    if (entries_.isEmpty()) return;
    emit previewStarting();
    const QString path = QDir::temp().filePath(QStringLiteral("eter-mixdown-preview-%1.wav").arg(QCoreApplication::applicationPid()));
    const eter::MixdownJob job = buildJob();
    const qint64 len = std::min(lengthMs, std::max<qint64>(1000, total_ - fromMs));
    playBtn_->setEnabled(false);
    trPlay_->setEnabled(false);
    QPointer<MixdownPage> self(this);
    reapThreads();
    threads_.emplace_back([self, job, path, fromMs, len] {
        const bool ok = eter::renderPreviewWav(job, fromMs * kRate / 1000, len * kRate / 1000, path.toStdString());
        QMetaObject::invokeMethod(qApp, [self, ok, path, fromMs] {
            if (!self) return;
            self->playBtn_->setEnabled(true);
            self->showTransition();
            if (!ok) {
                emit self->message(TR("Az előhallgatás nem készíthető el."));
                return;
            }
            auto &src = self->engine_->source(eter::Engine::Pfl);
            if (!src.load(path.toStdString())) return;
            src.play();
            self->previewing_ = true;
            self->previewFrom_ = fromMs;
            self->previewPath_ = path;
            self->playBtn_->setText(TR("■ Előhallgatás leállítása"));
            self->timer_->start();
        }, Qt::QueuedConnection);
    });
}

void MixdownPage::stopPreview()
{
    if (!previewing_) return;
    previewing_ = false;
    timer_->stop();
    auto &src = engine_->source(eter::Engine::Pfl);
    src.stop();
    src.unload();
    playBtn_->setText(TR("▶ Előhallgatás innen"));
    timeline_->update();
}

void MixdownPage::tick()
{
    auto &src = engine_->source(eter::Engine::Pfl);
    float l, r;
    src.takePeaks(l, r);
    if (!src.playing() || src.takeFinished()) {
        playhead_ = playheadMs();
        stopPreview();
        return;
    }
    timeline_->update();
}

// ------------------------------------------------------------------ voice track

void MixdownPage::setVoiceTrack(int i, const QString &path, const QString &title)
{
    if (i < 0 || i >= entries_.size()) return;
    Entry &e = entries_[i];
    e.vtPath = path;
    e.vtTitle = title;
    e.vtDurMs = 0;
    if (!path.isEmpty()) {
        eter::Decoder dec;
        if (!dec.open(path.toStdString())) {
            emit message(TR("A voice track nem nyitható meg: %1").arg(path));
            e.vtPath.clear();
        } else {
            e.vtDurMs = dec.durationFrames() * 1000 / kRate;
            // Alapból a következő elem kezdése előtt fél másodperccel indul (az intró alatt beszél).
            e.vtOffsetMs = i + 1 < entries_.size() ? -500 : 500;
        }
    }
    recompute();
}

void MixdownPage::pickVoiceFile()
{
    if (entries_.isEmpty()) {
        emit message(TR("Előbb húzz elemeket a mixdownba, és kattints az idővonalon oda, ahol a voice track kezdődjön."));
        return;
    }
    const QString f = QFileDialog::getOpenFileName(this, TR("Voice track választása"), QString(),
                                                   TR("Hangfájlok (*.wav *.mp3 *.flac *.ogg *.m4a *.aac *.opus)"));
    if (!f.isEmpty()) placeVoice(f, QFileInfo(f).completeBaseName());
}

void MixdownPage::pickVoiceLibrary()
{
    if (entries_.isEmpty()) {
        emit message(TR("Előbb húzz elemeket a mixdownba, és kattints az idővonalon oda, ahol a voice track kezdődjön."));
        return;
    }
    QMenu menu(this);
    int n = 0;
    QVector<Item> items = db_->items();
    std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) { return a.id > b.id; });
    for (const Item &it : items) {
        if (it.kind != QLatin1String("voicetrack")) continue;
        menu.addAction(it.title, this, [this, it] { placeVoice(it.path, it.title); });
        if (++n >= 30) break;
    }
    if (!n) {
        emit message(TR("A hangtárban nincs voice track kategóriájú elem."));
        return;
    }
    menu.exec(QCursor::pos());
}

void MixdownPage::recordVoiceTrack()
{
    if (entries_.isEmpty()) {
        emit message(TR("Előbb húzz elemeket a mixdownba, és kattints az idővonalon oda, ahol a voice track kezdődjön."));
        return;
    }
    stopPreview();
    QDialog dlg(this);
    dlg.setWindowTitle(TR("Voice track felvétele"));
    auto *v = new QVBoxLayout(&dlg);
    auto *info = new QLabel(TR("A „Felvétel” gombbal indul, a „Kész” gombbal áll le. Az elejéről és a végéről a csendet levágja."));
    info->setWordWrap(true);
    v->addWidget(info);
    auto *level = new QProgressBar;
    level->setRange(0, 100);
    level->setTextVisible(false);
    v->addWidget(level);
    auto *time = new QLabel(QStringLiteral("0:00"));
    time->setObjectName("mono");
    v->addWidget(time);
    auto *row = new QHBoxLayout;
    auto *rec = new QPushButton(TR("Felvétel"));
    rec->setProperty("danger", true);
    auto *cancel = new QPushButton(TR("Mégse"));
    row->addStretch(1);
    row->addWidget(cancel);
    row->addWidget(rec);
    v->addLayout(row);
    QTimer t;
    QElapsedTimer clock;
    bool recording = false;
    connect(&t, &QTimer::timeout, &dlg, [&] {
        const float pk = engine_->recordPeak();
        level->setValue(int(std::clamp(100.0 + (pk > 1e-5f ? 20.0 * std::log10(pk) : -100.0) * 100.0 / 60.0, 0.0, 100.0)));
        time->setText(mmss(clock.elapsed()));
    });
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(rec, &QPushButton::clicked, &dlg, [&] {
        if (!recording) {
            std::string err;
            if (!engine_->startRecording(&err)) {
                QMessageBox::warning(&dlg, TR("Voice track"), QString::fromStdString(err));
                return;
            }
            recording = true;
            clock.start();
            t.start(50);
            rec->setText(TR("Kész"));
        } else {
            dlg.accept();
        }
    });
    const bool ok = dlg.exec() == QDialog::Accepted;
    t.stop();
    if (!recording) return;
    std::vector<float> samples = engine_->stopRecording();
    if (!ok) return;
    const float threshold = 0.01f;
    size_t first = 0, last = samples.size();
    while (first < samples.size() && std::fabs(samples[first]) < threshold) ++first;
    while (last > first && std::fabs(samples[last - 1]) < threshold) --last;
    if (last <= first || last - first < size_t(kRate) / 4) {
        emit message(TR("A felvétel üres vagy túl halk – nem jött jel a mikrofonról."));
        return;
    }
    first = first > size_t(kRate) / 10 ? first - kRate / 10 : 0;
    last = std::min(samples.size(), last + size_t(kRate) / 4);
    // Mono 16 bites WAV a voice trackek mappájába.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/eter/voicetracks");
    QDir().mkpath(dir);
    const QDateTime now = QDateTime::currentDateTime();
    const QString path = dir + QStringLiteral("/mixdown-vt-%1.wav").arg(now.toString("yyyyMMdd-HHmmss"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        emit message(TR("A felvétel nem menthető ide: %1").arg(path));
        return;
    }
    const quint32 n = quint32(last - first);
    QByteArray h;
    auto u32 = [&h](quint32 x) { h.append(reinterpret_cast<const char *>(&x), 4); };
    auto u16 = [&h](quint16 x) { h.append(reinterpret_cast<const char *>(&x), 2); };
    h.append("RIFF");
    u32(36 + n * 2);
    h.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(1);
    u32(kRate);
    u32(kRate * 2);
    u16(2);
    u16(16);
    h.append("data");
    u32(n * 2);
    f.write(h);
    QByteArray pcm(int(n * 2), '\0');
    auto *o = reinterpret_cast<qint16 *>(pcm.data());
    for (quint32 i = 0; i < n; ++i) o[i] = qint16(std::lround(std::clamp(samples[first + i], -1.f, 1.f) * 32767.f));
    f.write(pcm);
    f.close();
    placeVoice(path, TR("VT %1").arg(now.toString("HH:mm:ss")));
    if (selected_ >= 0 && selected_ < entries_.size())
        emit message(TR("Voice track kész (%1). Húzd a helyére az idővonalon.").arg(mmss(entries_[selected_].vtDurMs, true)));
}

// ------------------------------------------------------------------ hangtár

void MixdownPage::refreshLibrary()
{
    const int cat = libCat_->currentData().toInt();
    libCat_->blockSignals(true);
    libCat_->clear();
    libCat_->addItem(TR("Minden kategória"), 0);
    for (const Category &c : db_->itemCategories()) libCat_->addItem(c.name, c.id);
    libCat_->setCurrentIndex(std::max(0, libCat_->findData(cat)));
    libCat_->blockSignals(false);
    const QString q = libSearch_->text().trimmed();
    lib_->clear();
    int n = 0;
    for (const Item &it : db_->items()) {
        if (cat && it.categoryId != libCat_->currentData().toInt()) continue;
        if (!q.isEmpty() && !it.title.contains(q, Qt::CaseInsensitive) && !it.artist.contains(q, Qt::CaseInsensitive)) continue;
        auto *li = new QListWidgetItem(QStringLiteral("%1\n%2 · %3").arg(it.title, it.artist.isEmpty() ? it.categoryName : it.artist,
                                                                        mmss(it.durationMs)));
        li->setData(Qt::UserRole, it.id);
        li->setToolTip(it.path);
        lib_->addItem(li);
        if (++n >= 2000) break;
    }
}

// ------------------------------------------------------------------ projektek

QString MixdownPage::projectDir() const
{
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/mixdown");
    QDir().mkpath(d);
    return d;
}

void MixdownPage::newProject()
{
    if (!entries_.isEmpty()
        && QMessageBox::question(this, TR("Új mixdown"), TR("Az aktuális mixdown elvész, ha nincs elmentve. Új kezdése?")) != QMessageBox::Yes)
        return;
    stopPreview();
    entries_.clear();
    selected_ = -1;
    playhead_ = 0;
    name_->setText(TR("Mixdown %1").arg(QDate::currentDate().toString(Qt::ISODate)));
    recompute();
}

void MixdownPage::saveProject()
{
    QJsonArray items;
    for (const Entry &e : entries_)
        items.push_back(QJsonObject{{"item", e.item.id}, {"path", e.item.path}, {"title", e.item.title}, {"cueIn", double(e.cueIn)},
                                    {"mixOut", double(e.mixOut)}, {"end", double(e.end)}, {"fadeOut", e.fadeOut},
                                    {"manualMix", e.manualMix}, {"manualCue", e.manualCue}, {"vtPath", e.vtPath},
                                    {"vtTitle", e.vtTitle}, {"vtOffset", double(e.vtOffsetMs)}, {"vtDuck", e.vtDuckDb},
                                    {"lane", e.lane}, {"env", env::toString(e.env)}});
    const QJsonObject o{{"name", name_->text().trimmed()}, {"items", items}, {"saved", QDateTime::currentDateTime().toString(Qt::ISODate)}};
    QString base = name_->text().trimmed();
    base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    if (base.isEmpty()) base = QStringLiteral("mixdown");
    const QString path = projectDir() + QLatin1Char('/') + base + QStringLiteral(".json");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(QJsonDocument(o).toJson()) < 0) {
        emit message(TR("A projekt nem menthető: %1").arg(path));
        return;
    }
    emit message(TR("Mixdown projekt mentve: %1").arg(base));
}

void MixdownPage::loadProjectFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    stopPreview();
    entries_.clear();
    int missing = 0;
    for (const QJsonValue &v : o.value("items").toArray()) {
        const QJsonObject j = v.toObject();
        Item it = db_->item(j.value("item").toInt());
        if (!it.id || it.path != j.value("path").toString()) {
            // A hangtárból azóta törölték vagy cserélték: a mentett fájlútvonallal.
            it = Item();
            it.path = j.value("path").toString();
            it.title = j.value("title").toString();
            eter::Decoder dec;
            if (!dec.open(it.path.toStdString())) {
                ++missing;
                continue;
            }
            it.durationMs = dec.durationFrames() * 1000 / kRate;
            it.kind = QStringLiteral("music");
        }
        Entry e;
        e.item = it;
        e.durMs = it.durationMs;
        e.cueIn = qint64(j.value("cueIn").toDouble());
        e.mixOut = qint64(j.value("mixOut").toDouble());
        e.end = qint64(j.value("end").toDouble());
        e.fadeOut = j.value("fadeOut").toInt();
        e.manualMix = j.value("manualMix").toBool();
        e.manualCue = j.value("manualCue").toBool();
        e.vtPath = j.value("vtPath").toString();
        e.vtTitle = j.value("vtTitle").toString();
        e.vtOffsetMs = qint64(j.value("vtOffset").toDouble());
        e.vtDuckDb = j.value("vtDuck").toDouble(-9);
        e.lane = j.value("lane").toInt(-1);
        e.env = j.contains("env") ? env::fromString(j.value("env").toString()) : env::fromString(it.volumeEnv);
        if (!e.vtPath.isEmpty()) {
            eter::Decoder dec;
            if (dec.open(e.vtPath.toStdString())) e.vtDurMs = dec.durationFrames() * 1000 / kRate;
            else e.vtPath.clear();
        }
        entries_ << e;
        requestWave(it);
    }
    name_->setText(o.value("name").toString());
    selected_ = entries_.isEmpty() ? -1 : 0;
    playhead_ = 0;
    recompute();
    if (missing) emit message(TR("%1 elem fájlja nem található, kimaradt.").arg(missing));
}

void MixdownPage::openProjectMenu()
{
    QMenu menu(this);
    QDir d(projectDir());
    const QFileInfoList files = d.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Time);
    for (const QFileInfo &fi : files)
        menu.addAction(fi.completeBaseName() + QStringLiteral("   ·   ") + fi.lastModified().toString(QStringLiteral("yyyy.MM.dd HH:mm")), this,
                       [this, fi] { loadProjectFile(fi.absoluteFilePath()); });
    if (files.isEmpty()) menu.addAction(TR("Még nincs mentett mixdown projekt"))->setEnabled(false);
    menu.exec(QCursor::pos());
}

// ------------------------------------------------------------------ mentés fájlba

void MixdownPage::openExportDialog()
{
    if (entries_.isEmpty()) {
        emit message(TR("A mixdown üres: húzz bele elemeket a hangtárból."));
        return;
    }
    stopPreview();
    QSettings s;
    QDialog dlg(this);
    dlg.setWindowTitle(TR("Mixdown készítése"));
    dlg.resize(600, 0);
    auto *v = new QVBoxLayout(&dlg);
    auto *form = new QFormLayout;
    auto *fname = new QLineEdit(name_->text().trimmed());
    auto *dirRow = new QHBoxLayout;
    const QString defDir = QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + QStringLiteral("/Eter/Mixdown");
    auto *dir = new QLineEdit(s.value("mixdown/dir", defDir).toString());
    auto *browse = new QPushButton(TR("Tallózás…"));
    dirRow->addWidget(dir, 1);
    dirRow->addWidget(browse);
    auto *fmtRow = new QHBoxLayout;
    auto *mp3 = new QRadioButton(TR("MP3"));
    auto *wav = new QRadioButton(TR("WAV"));
    auto *kbps = new QComboBox;
    for (int k : {320, 256, 192, 128}) kbps->addItem(TR("%1 kbps").arg(k), k);
    kbps->setCurrentIndex(std::max(0, kbps->findData(s.value("mixdown/kbps", 320).toInt())));
    auto *bits = new QComboBox;
    bits->addItem(TR("16 bit"), 16);
    bits->addItem(TR("24 bit"), 24);
    bits->setCurrentIndex(std::max(0, bits->findData(s.value("mixdown/bits", 16).toInt())));
    (s.value("mixdown/format", "mp3").toString() == QLatin1String("wav") ? wav : mp3)->setChecked(true);
    fmtRow->addWidget(mp3);
    fmtRow->addWidget(kbps);
    fmtRow->addSpacing(16);
    fmtRow->addWidget(wav);
    fmtRow->addWidget(bits);
    fmtRow->addStretch(1);
    auto *rate = new QComboBox;
    rate->addItem(TR("48 kHz"), 48000);
    rate->addItem(TR("44,1 kHz"), 44100);
    rate->setCurrentIndex(std::max(0, rate->findData(s.value("mixdown/rate", 48000).toInt())));
    auto *limit = new QCheckBox(TR("a csúcs legfeljebb −1 dBFS (ha a mix ennél hangosabb, lehalkítja; különben változatlan)"));
    limit->setChecked(s.value("mixdown/limit", true).toBool());
    form->addRow(TR("Fájlnév"), fname);
    form->addRow(TR("Mappa"), dirRow);
    form->addRow(TR("Formátum"), fmtRow);
    form->addRow(TR("Mintavétel"), rate);
    form->addRow(TR("Hangerő"), limit);
    v->addLayout(form);
    auto *toLib = new QCheckBox(TR("Felvétel a hangtárba, a „Mixdown” kategóriába (ha nincs, létrehozza)"));
    toLib->setChecked(s.value("mixdown/library", true).toBool());
    auto *overwrite = new QCheckBox(TR("ha már van ilyen nevű fájl, felülírja (különben „(2)” kerül a név végére)"));
    v->addWidget(toLib);
    v->addWidget(overwrite);
    auto *status = new QLabel(TR("Hossz: %1").arg(mmss(total_, true)));
    status->setObjectName("muted");
    auto *bar = new QProgressBar;
    bar->setRange(0, 1000);
    bar->setValue(0);
    v->addWidget(status);
    v->addWidget(bar);
    auto *row = new QHBoxLayout;
    auto *cancelBtn = new QPushButton(TR("Mégse"));
    auto *go = new QPushButton(TR("Mixdown készítése"));
    go->setProperty("accent", true);
    row->addStretch(1);
    row->addWidget(cancelBtn);
    row->addWidget(go);
    v->addLayout(row);
    auto syncFmt = [=] {
        kbps->setEnabled(mp3->isChecked());
        bits->setEnabled(wav->isChecked());
    };
    connect(mp3, &QRadioButton::toggled, &dlg, syncFmt);
    syncFmt();
    connect(browse, &QPushButton::clicked, &dlg, [&dlg, dir] {
        const QString d = QFileDialog::getExistingDirectory(&dlg, TR("Mappa"), dir->text());
        if (!d.isEmpty()) dir->setText(d);
    });
    auto cancelFlag = std::make_shared<std::atomic<bool>>(false);
    bool running = false;
    connect(cancelBtn, &QPushButton::clicked, &dlg, [&] {
        if (running) cancelFlag->store(true);
        else dlg.reject();
    });
    connect(go, &QPushButton::clicked, &dlg, [&] {
        QString base = fname->text().trimmed();
        base.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
        if (base.isEmpty()) return;
        QDir().mkpath(dir->text());
        const QString ext = mp3->isChecked() ? QStringLiteral(".mp3") : QStringLiteral(".wav");
        QString path = QDir(dir->text()).filePath(base + ext);
        QString title = base;
        for (int k = 2; !overwrite->isChecked() && QFileInfo::exists(path); ++k) {
            title = QStringLiteral("%1 (%2)").arg(base).arg(k);
            path = QDir(dir->text()).filePath(title + ext);
        }
        s.setValue("mixdown/dir", dir->text());
        s.setValue("mixdown/format", mp3->isChecked() ? "mp3" : "wav");
        s.setValue("mixdown/kbps", kbps->currentData());
        s.setValue("mixdown/bits", bits->currentData());
        s.setValue("mixdown/rate", rate->currentData());
        s.setValue("mixdown/limit", limit->isChecked());
        s.setValue("mixdown/library", toLib->isChecked());
        running = true;
        go->setEnabled(false);
        status->setText(TR("Keverés…"));
        eter::MixdownOutput o;
        o.path = path.toStdString();
        o.mp3 = mp3->isChecked();
        o.kbps = kbps->currentData().toInt();
        o.bits = bits->currentData().toInt();
        o.rate = rate->currentData().toInt();
        o.limitPeak = limit->isChecked();
        auto progress = std::make_shared<std::atomic<int>>(0);
        auto done = std::make_shared<std::atomic<int>>(0);   // 1: kész, 2: hiba
        auto error = std::make_shared<std::string>();
        const eter::MixdownJob job = buildJob();
        std::thread worker([job, o, progress, done, error, cancelFlag] {
            const bool ok = eter::renderMixdown(job, o, [progress](double p) { progress->store(int(p * 1000)); }, *cancelFlag, error.get());
            done->store(ok ? 1 : 2);
        });
        QElapsedTimer clock;
        clock.start();
        while (!done->load()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            const int p = progress->load();
            bar->setValue(p);
            if (p > 20) {
                const qint64 left = clock.elapsed() * (1000 - p) / p;
                status->setText(TR("Keverés… %1 % · kb. %2 mp van hátra").arg(p / 10).arg((left + 999) / 1000));
            }
        }
        worker.join();
        running = false;
        if (done->load() != 1) {
            status->setText(TR("Nem sikerült: %1").arg(QString::fromStdString(*error)));
            go->setEnabled(true);
            return;
        }
        bar->setValue(1000);
        QString err;
        int itemId = 0;
        if (toLib->isChecked()) {
            int cat = 0;
            for (const Category &c : db_->itemCategories())
                if (c.name == QLatin1String("Mixdown")) cat = c.id;
            if (!cat) cat = db_->addCategory(QStringLiteral("Mixdown"), QStringLiteral("music"), QStringLiteral("#c79bff"), &err);
            if (cat) {
                for (const Item &it : db_->items())
                    if (it.path == path) itemId = it.id;   // felülírt fájl: a meglévő elem marad
                if (!itemId) {
                    Item it;
                    it.categoryId = cat;
                    it.title = title;
                    it.path = path;
                    eter::Decoder dec;
                    if (dec.open(path.toStdString())) it.durationMs = dec.durationFrames() * 1000 / kRate;
                    itemId = db_->addItem(it, &err);
                }
                db_->notifyItemsChanged();
            }
        }
        emit exported(itemId);
        emit message(itemId ? TR("Mixdown kész: %1 – a hangtár „Mixdown” kategóriájában.").arg(QFileInfo(path).fileName())
                            : TR("Mixdown kész: %1").arg(path));
        dlg.accept();
    });
    dlg.exec();
}

bool MixdownPage::exportTo(const QString &path, bool mp3, int kbps, int bits, int rate, bool limit, bool toLibrary, QString *err)
{
    eter::MixdownOutput o;
    o.path = path.toStdString();
    o.mp3 = mp3;
    o.kbps = kbps;
    o.bits = bits;
    o.rate = rate;
    o.limitPeak = limit;
    std::atomic<bool> cancel{false};
    std::string e;
    const bool ok = eter::renderMixdown(buildJob(), o, nullptr, cancel, &e);
    if (!ok && err) *err = QString::fromStdString(e);
    if (ok && toLibrary) {
        int cat = 0;
        for (const Category &c : db_->itemCategories())
            if (c.name == QLatin1String("Mixdown")) cat = c.id;
        if (!cat) cat = db_->addCategory(QStringLiteral("Mixdown"), QStringLiteral("music"), QStringLiteral("#c79bff"));
        Item it;
        it.categoryId = cat;
        it.title = QFileInfo(path).completeBaseName();
        it.path = path;
        eter::Decoder dec;
        if (dec.open(path.toStdString())) it.durationMs = dec.durationFrames() * 1000 / kRate;
        db_->addItem(it);
        db_->notifyItemsChanged();
    }
    return ok;
}

QString MixdownPage::debugState() const
{
    QStringList parts;
    for (int i = 0; i < entries_.size(); ++i) {
        const Entry &e = entries_[i];
        parts << QStringLiteral("%1. %2 kezd %3 · be %4 · mix %5 · vége %6 · úszás %7%8%9")
                     .arg(i + 1).arg(e.item.title, mmss(starts_.value(i), true), mmss(e.cueIn, true), mmss(e.mixOut, true),
                          mmss(e.end, true), QString::number(e.fadeOut / 1000.0, 'f', 1),
                          e.manualMix ? QStringLiteral(" (kézi)") : QString(),
                          e.vtPath.isEmpty() ? QString() : QStringLiteral(" · VT %1 @%2").arg(mmss(e.vtDurMs, true)).arg(e.vtOffsetMs));
    }
    parts << QStringLiteral("hossz %1").arg(mmss(total_, true));
    return parts.join(QLatin1Char('\n'));
}
