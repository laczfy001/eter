#include "i18n.h"
#include "ItemEditor.h"
#include "Library.h"
#include "Theme.h"
#include "audio/Decoder.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <cmath>
#include <QDateTime>
#include <QCheckBox>
#include "audio/Analysis.h"
#include "sched/Scheduler.h"
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>

static const QColor kMarkerColor[3] = {theme::green, theme::blue, theme::amber};
static const char *kMarkerShort[3] = {"KEZDŐPONT", "INTRO VÉGE", "OUTRO"};

static QString clock(qint64 ms)
{
    ms = std::max<qint64>(0, ms);
    return QStringLiteral("%1:%2.%3").arg(ms / 60000).arg(ms / 1000 % 60, 2, 10, QLatin1Char('0')).arg(ms / 100 % 10);
}

// ================================================================ hullámforma

MarkerWave::MarkerWave(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(170);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void MarkerWave::setWave(const QVector<float> &peaks, qint64 durMs)
{
    peaks_ = peaks;
    durMs_ = durMs;
    update();
}

void MarkerWave::setDuration(qint64 durMs) { durMs_ = durMs; update(); }
void MarkerWave::setMarker(Marker m, qint64 ms) { ms_[m] = ms; update(); }
void MarkerWave::setAutoOutro(qint64 ms) { autoOutro_ = ms; update(); }

void MarkerWave::setPlayhead(qint64 ms)
{
    if (ms == playhead_) return;
    playhead_ = ms;
    update();
}

double MarkerWave::xOf(qint64 ms) const { return durMs_ > 0 ? double(ms) / double(durMs_) * width() : 0.0; }
qint64 MarkerWave::msAt(double x) const
{
    return durMs_ > 0 ? std::clamp<qint64>(qint64(x / std::max(1, width()) * double(durMs_)), 0, durMs_) : 0;
}

// A jelölők fogantyúja felül van, egymás alatt három sorban, hogy közel állva se takarják egymást.
QPointF MarkerWave::markerPos(Marker m) const
{
    const qint64 at = (m == Outro && ms_[m] <= 0) ? (autoOutro_ > 0 ? autoOutro_ : durMs_) : ms_[m];
    return {xOf(at), 9.0 + 16.0 * int(m)};
}

MarkerWave::Marker MarkerWave::hit(const QPointF &pos) const
{
    Marker best = None;
    double bestDist = 9.0;
    for (int m = 0; m < 3; ++m) {
        const QPointF p = markerPos(Marker(m));
        // A fogantyú címkéje a vonaltól jobbra (az outróé balra) nyúlik.
        const QRectF label = m == Outro ? QRectF(p.x() - 78, p.y() - 8, 80, 16) : QRectF(p.x() - 2, p.y() - 8, 80, 16);
        if (label.contains(pos)) return Marker(m);
        const double d = std::abs(pos.x() - p.x());
        if (d < bestDist && pos.y() > 52) { bestDist = d; best = Marker(m); }
    }
    return best;
}

void MarkerWave::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRectF r = rect();
    p.fillRect(r, QColor("#121418"));
    const double top = 54, bottom = r.height() - 18;
    const double mid = (top + bottom) / 2, half = (bottom - top) / 2;

    const qint64 cue = ms_[CueIn], intro = ms_[Intro];
    const qint64 outro = ms_[Outro] > 0 ? ms_[Outro] : autoOutro_;
    // Tartományok: a kezdőpont előtti rész sötét, az intro kék, az outro borostyán árnyalatú.
    if (durMs_ > 0) {
        if (cue > 0) p.fillRect(QRectF(0, top, xOf(cue), bottom - top), QColor(0, 0, 0, 110));
        if (intro > cue) p.fillRect(QRectF(xOf(cue), top, xOf(intro) - xOf(cue), bottom - top), QColor(92, 176, 255, 34));
        if (outro > 0 && outro < durMs_)
            p.fillRect(QRectF(xOf(outro), top, r.width() - xOf(outro), bottom - top), QColor(245, 165, 36, ms_[Outro] > 0 ? 40 : 18));
    }

    if (peaks_.isEmpty()) {
        p.setPen(QColor("#6b7380"));
        p.drawText(QRectF(0, top, r.width(), bottom - top), Qt::AlignCenter,
                   durMs_ > 0 ? TR("Hullámforma készül…") : TR("A fájl nem olvasható"));
    } else {
        const int w = int(r.width());
        for (int x = 0; x < w; ++x) {
            const int a = int(qint64(x) * peaks_.size() / w), b = std::max(a + 1, int(qint64(x + 1) * peaks_.size() / w));
            float v = 0.f;
            for (int i = a; i < b && i < peaks_.size(); ++i) v = std::max(v, peaks_[i]);
            const qint64 t = msAt(x);
            QColor c("#8a93a0");
            if (t < cue) c = QColor("#4a5160");
            else if (intro > cue && t < intro) c = QColor("#7fb8ee");
            else if (outro > 0 && t >= outro) c = ms_[Outro] > 0 ? QColor("#e0a84a") : QColor("#a8946e");
            p.setPen(c);
            const double h = std::max(0.5, double(v) * half);
            p.drawLine(QPointF(x + 0.5, mid - h), QPointF(x + 0.5, mid + h));
        }
    }

    // Időskála
    QFont mono("IBM Plex Mono");
    mono.setPixelSize(10);
    p.setFont(mono);
    if (durMs_ > 0) {
        static const qint64 steps[] = {1000, 2000, 5000, 10000, 15000, 30000, 60000, 120000, 300000, 600000};
        qint64 step = steps[9];
        for (qint64 s : steps)
            if (double(s) / durMs_ * r.width() >= 70) { step = s; break; }
        p.setPen(QColor("#6b7380"));
        for (qint64 t = 0; t <= durMs_; t += step) {
            const double x = xOf(t);
            p.drawLine(QPointF(x, bottom), QPointF(x, bottom + 4));
            if (x < r.width() - 34)
                p.drawText(QPointF(x + 3, r.height() - 3), QStringLiteral("%1:%2").arg(t / 60000).arg(t / 1000 % 60, 2, 10, QLatin1Char('0')));
        }
    }

    // Jelölők
    QFont label = font();
    label.setPixelSize(10);
    label.setWeight(QFont::DemiBold);
    p.setFont(label);
    for (int m = 0; m < 3; ++m) {
        const bool unset = m != CueIn && ms_[m] <= 0;
        if (m == Intro && unset) continue;                       // intro nélkül nincs mit rajzolni
        const QPointF pos = markerPos(Marker(m));
        QColor c = kMarkerColor[m];
        if (unset) c.setAlpha(120);
        p.setPen(QPen(c, drag_ == m ? 2 : 1, unset ? Qt::DashLine : Qt::SolidLine));
        p.drawLine(QPointF(pos.x(), pos.y()), QPointF(pos.x(), bottom));
        const QString text = unset ? TR("OUTRO (auto)") : i18n::text(kMarkerShort[m]);
        const double tw = QFontMetrics(label).horizontalAdvance(text) + 10;
        // A címke a vonaltól jobbra áll; az outróé (és ami a jobb szélen kilógna) balra.
        const bool leftSide = m == Outro || pos.x() + tw > r.width();
        const QRectF box(leftSide ? pos.x() - tw : pos.x(), pos.y() - 7, tw, 14);
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(box, 2, 2);
        p.setPen(QColor("#101215"));
        p.drawText(box, Qt::AlignCenter, text);
    }

    if (playhead_ >= 0 && durMs_ > 0) {
        p.setPen(QPen(QColor("#ffffff"), 1));
        p.drawLine(QPointF(xOf(playhead_), top - 4), QPointF(xOf(playhead_), bottom));
    }
}

void MarkerWave::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || durMs_ <= 0) return;
    drag_ = hit(e->position());
    if (drag_ == None) emit seekRequested(msAt(e->position().x()));
    update();
}

void MarkerWave::mouseMoveEvent(QMouseEvent *e)
{
    if (drag_ == None) {
        setCursor(hit(e->position()) != None ? Qt::SizeHorCursor : Qt::ArrowCursor);
        return;
    }
    const qint64 t = msAt(e->position().x()) / 100 * 100;
    emit markerMoved(drag_, t);
}

void MarkerWave::mouseReleaseEvent(QMouseEvent *)
{
    drag_ = None;
    update();
}

// ================================================================ ablak

ItemEditorDialog::ItemEditorDialog(Database *db, eter::Engine *engine, int itemId, QWidget *parent)
    : QDialog(parent), db_(db), engine_(engine), cancel_(std::make_shared<std::atomic<bool>>(false))
{
    item_ = db_->item(itemId);
    durMs_ = item_.durationMs;
    setWindowTitle(TR("Elem szerkesztése – %1").arg(item_.title));
    resize(900, 600);
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 14);
    root->setSpacing(10);

    // ---- adatok
    auto *dataCaption = new QLabel(TR("ADATOK"));
    dataCaption->setObjectName("caption");
    root->addWidget(dataCaption);
    auto *top = new QHBoxLayout;
    top->setSpacing(18);
    auto *form1 = new QFormLayout;
    form1->setHorizontalSpacing(10);
    form1->setVerticalSpacing(8);
    title_ = new QLineEdit(item_.title);
    title_->setMaxLength(200);
    artist_ = new QLineEdit(item_.artist);
    artist_->setMaxLength(200);
    form1->addRow(TR("Cím"), title_);
    form1->addRow(TR("Előadó"), artist_);
    auto *form2 = new QFormLayout;
    form2->setHorizontalSpacing(10);
    form2->setVerticalSpacing(8);
    QString album;
    int year = 0;
    db_->itemExtra(itemId, &album, &year);
    album_ = new QLineEdit(album);
    album_->setMaxLength(200);
    year_ = new QSpinBox;
    year_->setRange(0, 2100);
    year_->setSpecialValueText(TR("nincs"));
    year_->setValue(year);
    year_->setFixedWidth(90);
    category_ = new QComboBox;
    category_->setIconSize(QSize(12, 12));
    for (const Category &c : db_->itemCategories())
        category_->addItem(swatchIcon(QColor(c.color)),
                           c.name.compare(kindLabel(c.kind), Qt::CaseInsensitive) == 0 ? c.name   // „Zene · Zene” helyett csak „Zene”
                                                                                      : QStringLiteral("%1 · %2").arg(c.name, kindLabel(c.kind)),
                           c.id);
    category_->setCurrentIndex(std::max(0, category_->findData(item_.categoryId)));
    auto *yearRow = new QHBoxLayout;
    yearRow->addWidget(year_);
    yearRow->addSpacing(10);
    yearRow->addWidget(new QLabel(TR("Kategória")));
    yearRow->addWidget(category_, 1);
    form2->addRow(TR("Album"), album_);
    form2->addRow(TR("Év"), yearRow);
    top->addLayout(form1, 1);
    top->addLayout(form2, 1);
    root->addLayout(top);

    // Az ütemező szabályaihoz: energia, tempó, napszakok.
    auto *flow = new QHBoxLayout;
    flow->setSpacing(8);
    energy_ = new QComboBox;
    for (int e = 0; e <= 5; ++e) energy_->addItem(Scheduler::energyName(e), e);
    energy_->setCurrentIndex(std::clamp(item_.energy, 0, 5));
    energy_->setToolTip(TR("Az ütemező szabályaihoz: például ne jöjjön két lassú szám egymás után"));
    bpm_ = new QDoubleSpinBox;
    bpm_->setRange(0, 250);
    bpm_->setDecimals(1);
    bpm_->setSpecialValueText(TR("nem ismert"));
    bpm_->setSuffix(TR(" BPM"));
    bpm_->setValue(item_.bpm);
    bpm_->setFixedWidth(120);
    bpmEstimate_ = new QPushButton(TR("Becslés"));
    bpmEstimate_->setToolTip(TR("A tempó becslése a hangfájlból"));
    bpmTap_ = new QPushButton(TR("Ütem"));
    bpmTap_->setToolTip(TR("Kattints az ütemre (legalább négyszer): a tempó a kattintások üteméből jön"));
    flow->addWidget(new QLabel(TR("Energia")));
    flow->addWidget(energy_);
    flow->addSpacing(10);
    flow->addWidget(new QLabel(TR("Tempó")));
    flow->addWidget(bpm_);
    flow->addWidget(bpmEstimate_);
    flow->addWidget(bpmTap_);
    flow->addSpacing(10);
    flow->addWidget(new QLabel(TR("Napszak")));
    for (int p = 0; p < Scheduler::kDayparts; ++p) {
        dayparts_[p] = new QCheckBox(Scheduler::daypartName(p));
        dayparts_[p]->setChecked(item_.dayparts & (1 << p));
        dayparts_[p]->setToolTip(TR("Az ütemező csak a bejelölt napszakokban teszi be; ha egyik sincs bejelölve, bármikor"));
        flow->addWidget(dayparts_[p]);
    }
    flow->addStretch(1);
    root->addLayout(flow);
    info_ = new QLabel;
    info_->setObjectName("small");
    info_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(info_);

    // ---- jelölőpontok
    auto *markCaption = new QLabel(TR("JELÖLŐPONTOK"));
    markCaption->setObjectName("caption");
    root->addSpacing(4);
    root->addWidget(markCaption);
    wave_ = new MarkerWave;
    wave_->setDuration(durMs_);
    wave_->setAccessibleName(TR("Hullámforma a jelölőpontokkal"));
    root->addWidget(wave_, 1);

    auto *transport = new QHBoxLayout;
    play_ = new QPushButton(TR("▶  Előhallgatás"));
    play_->setMinimumWidth(140);
    play_->setToolTip(TR("Lejátszás az előhallgatás kimenetén; a hullámformára kattintva onnan szól"));
    pos_ = new QLabel;
    QFont mono("IBM Plex Mono");
    mono.setPixelSize(13);
    pos_->setFont(mono);
    auto *hint = new QLabel(TR("A jelölők a címkéjüknél vagy a vonaluknál fogva húzhatók · kattintás a hullámformán: lejátszás onnan"));
    hint->setObjectName("small");
    transport->addWidget(play_);
    transport->addWidget(pos_);
    transport->addStretch(1);
    transport->addWidget(hint);
    root->addLayout(transport);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    const QString names[3] = {TR("Kezdőpont"), TR("Intro vége"), TR("Outro kezdete")};
    const QString tips[3] = {
        TR("Innen indul az elem lejátszása (az eleji csend vagy felvezetés átugrása)"),
        TR("Az intro vége, vagyis az ének kezdete; a lejátszó eddig visszaszámlál"),
        TR("Az outro kezdete: itt indul a következő elem (mix pont). Üresen hagyva a program találja meg.")};
    const QString specials[3] = {QString(), TR("nincs"), TR("automatikus")};
    const qint64 values[3] = {item_.cueInMs, item_.introMs, item_.userMixOutMs};
    for (int m = 0; m < 3; ++m) {
        auto *dot = new QLabel;
        dot->setFixedSize(12, 12);
        dot->setStyleSheet(QStringLiteral("background: %1; border-radius: 3px;").arg(kMarkerColor[m].name()));
        auto *name = new QLabel(names[m]);
        name->setToolTip(tips[m]);
        spin_[m] = new QDoubleSpinBox;
        spin_[m]->setDecimals(1);
        spin_[m]->setSingleStep(0.1);
        spin_[m]->setRange(0, std::max(0.1, durMs_ / 1000.0));
        spin_[m]->setSuffix(TR(" mp"));
        spin_[m]->setMinimumWidth(130);
        spin_[m]->setToolTip(tips[m]);
        spin_[m]->setAccessibleName(names[m]);
        if (!specials[m].isEmpty()) spin_[m]->setSpecialValueText(specials[m]);
        spin_[m]->setValue(values[m] / 1000.0);
        name->setBuddy(spin_[m]);
        auto *here = new QPushButton(TR("Ide: lejátszási pozíció"));
        here->setToolTip(TR("A jelölő arra a pontra kerül, ahol az előhallgatás most tart"));
        auto *listen = new QPushButton(m == MarkerWave::Outro ? TR("▶ 3 mp-cel előtte") : TR("▶ Innen"));
        listen->setToolTip(TR("Előhallgatás a jelölőtől"));
        auto *clear = new QPushButton(m == MarkerWave::CueIn ? TR("Az elejére") : m == MarkerWave::Intro ? TR("Törlés") : TR("Automatikus"));
        grid->addWidget(dot, m, 0);
        grid->addWidget(name, m, 1);
        grid->addWidget(spin_[m], m, 2);
        grid->addWidget(here, m, 3);
        grid->addWidget(listen, m, 4);
        grid->addWidget(clear, m, 5);
        wave_->setMarker(MarkerWave::Marker(m), values[m]);

        connect(spin_[m], &QDoubleSpinBox::valueChanged, this, [this, m](double v) {
            if (loading_) return;
            wave_->setMarker(MarkerWave::Marker(m), qint64(std::llround(v * 1000)));
            markDirty();
        });
        connect(here, &QPushButton::clicked, this, [this, m] {
            if (!playing_) {
                status_->setText(TR("Előbb indítsd el az előhallgatást, és ott nyomd meg, ahová a jelölő kerüljön."));
                return;
            }
            spin_[m]->setValue(engine_->source(eter::Engine::Pfl).position() * 1000 / eter::kRate / 100 / 10.0);
        });
        connect(listen, &QPushButton::clicked, this, [this, m] {
            qint64 from = markerMs(m);
            if (m == MarkerWave::Outro) from = std::max<qint64>(0, (from > 0 ? from : item_.mixOutMs) - 3000);
            playFrom(from);
        });
        connect(clear, &QPushButton::clicked, this, [this, m] { spin_[m]->setValue(0); });
    }
    grid->setColumnStretch(6, 1);
    wave_->setAutoOutro(item_.userMixOutMs > 0 ? 0 : item_.mixOutMs);
    root->addLayout(grid);

    auto *bottom = new QHBoxLayout;
    status_ = new QLabel;
    status_->setObjectName("small");
    status_->setWordWrap(true);
    auto *cancel = new QPushButton(TR("Bezárás"));
    save_ = new QPushButton(TR("Mentés"));
    save_->setProperty("accent", true);
    save_->setMinimumWidth(96);
    save_->setDefault(true);
    bottom->addWidget(status_, 1);
    bottom->addWidget(cancel);
    bottom->addWidget(save_);
    root->addLayout(bottom);

    for (QLineEdit *e : {title_, artist_, album_}) connect(e, &QLineEdit::textEdited, this, [this] { markDirty(); });
    connect(year_, &QSpinBox::valueChanged, this, [this] { if (!loading_) markDirty(); });
    connect(category_, &QComboBox::activated, this, [this] { markDirty(); });
    connect(energy_, &QComboBox::activated, this, [this] { markDirty(); });
    connect(bpm_, &QDoubleSpinBox::valueChanged, this, [this] { if (!loading_) markDirty(); });
    for (QCheckBox *b : dayparts_) connect(b, &QCheckBox::toggled, this, [this] { if (!loading_) markDirty(); });
    connect(bpmTap_, &QPushButton::clicked, this, [this] {
        // A legutóbbi kattintások átlagos távolsága; 2 mp szünet után újrakezdődik.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (!taps_.isEmpty() && now - taps_.last() > 2000) taps_.clear();
        taps_ << now;
        if (taps_.size() > 12) taps_.remove(0);
        if (taps_.size() >= 4) bpm_->setValue(std::round(60000.0 * (taps_.size() - 1) / double(taps_.last() - taps_.first()) * 10) / 10);
        status_->setText(taps_.size() < 4 ? TR("Ütem: még %1 kattintás…").arg(4 - taps_.size()) : TR("Ütem: %1 BPM").arg(bpm_->value()));
    });
    connect(bpmEstimate_, &QPushButton::clicked, this, [this] {
        if (bpmThread_.joinable()) return;
        bpmEstimate_->setEnabled(false);
        status_->setText(TR("Tempó becslése…"));
        QPointer<ItemEditorDialog> self(this);
        const std::string path = item_.path.toStdString();
        auto flag = cancel_;
        bpmThread_ = std::thread([self, path, flag] {
            const double bpm = eter::estimateBpm(path, *flag);
            if (flag->load()) return;
            QMetaObject::invokeMethod(qApp, [self, bpm] {
                if (!self) return;
                if (self->bpmThread_.joinable()) self->bpmThread_.join();
                self->bpmEstimate_->setEnabled(true);
                if (bpm > 0) {
                    self->bpm_->setValue(bpm);
                    self->status_->setText(TR("Becsült tempó: %1 BPM (a fél- vagy dupla tempó is lehet: ellenőrizd az Ütem gombbal).").arg(bpm));
                } else {
                    self->status_->setText(TR("A tempó nem állapítható meg (nincs határozott ütem)."));
                }
            }, Qt::QueuedConnection);
        });
    });
    connect(cancel, &QPushButton::clicked, this, &ItemEditorDialog::reject);
    connect(save_, &QPushButton::clicked, this, [this] { save(); });
    connect(play_, &QPushButton::clicked, this, [this] {
        if (playing_) stopPreview();
        else playFrom(markerMs(MarkerWave::CueIn));
    });
    connect(wave_, &MarkerWave::seekRequested, this, &ItemEditorDialog::playFrom);
    connect(wave_, &MarkerWave::markerMoved, this, [this](int m, qint64 ms) { spin_[m]->setValue(ms / 1000.0); });

    timer_ = new QTimer(this);
    timer_->setInterval(30);
    connect(timer_, &QTimer::timeout, this, &ItemEditorDialog::tick);
    loading_ = false;
    save_->setEnabled(false);
    updateInfo();

    // A hullámforma a háttérben készül; a pontos hossz és az automatikus mix pont is ebből jön.
    const std::string path = item_.path.toStdString();
    const int bins = int(std::clamp<qint64>(durMs_ / 25, 2000, 40000));
    const double threshold = db_->setting("auto_mix_threshold_db", "-24").toDouble();
    auto flag = cancel_;
    QPointer<ItemEditorDialog> self(this);
    thread_ = std::thread([self, path, bins, threshold, flag] {
        std::vector<float> peaks;
        int64_t total = 0;
        const bool ok = eter::scanPeaks(path, bins, peaks, total, *flag);
        if (flag->load()) return;
        const QVector<float> q(peaks.begin(), peaks.end());
        const qint64 dur = total * 1000 / eter::kRate;
        const qint64 autoMix = ok ? eter::autoMixOut(peaks, total, threshold) * 1000 / eter::kRate : 0;
        QMetaObject::invokeMethod(qApp, [self, ok, q, dur, autoMix] {
            if (!self) return;
            if (!ok) {
                self->wave_->setDuration(0);
                self->status_->setText(TR("A hangfájl nem olvasható, a jelölők nem állíthatók: %1").arg(self->item_.path));
                return;
            }
            self->durMs_ = dur;
            self->loading_ = true;
            for (QDoubleSpinBox *s : self->spin_) s->setMaximum(std::max(0.1, dur / 1000.0));
            self->loading_ = false;
            self->wave_->setWave(q, dur);
            if (self->item_.userMixOutMs <= 0 && autoMix > 0) self->item_.mixOutMs = autoMix;
            self->wave_->setAutoOutro(self->item_.mixOutMs);
            self->ready_ = true;
            self->updateInfo();
        }, Qt::QueuedConnection);
    });
}

ItemEditorDialog::~ItemEditorDialog()
{
    cancel_->store(true);
    if (thread_.joinable()) thread_.join();
    if (bpmThread_.joinable()) bpmThread_.join();
    if (playing_) stopPreview();
}

qint64 ItemEditorDialog::markerMs(int m) const { return qint64(std::llround(spin_[m]->value() * 1000)); }

void ItemEditorDialog::updateInfo()
{
    const QString last = item_.lastPlayedAt > 0
        ? i18n::locale().toString(QDateTime::fromSecsSinceEpoch(item_.lastPlayedAt), TR("yyyy. MMM d. HH:mm"))
        : TR("még nem szólt");
    info_->setText(TR("Hossz: %1   ·   Utoljára adásban: %2   ·   Fájl: %3").arg(clock(durMs_), last, item_.path));
    if (!playing_) pos_->setText(QStringLiteral("  –  / %1").arg(clock(durMs_)));
}

void ItemEditorDialog::markDirty()
{
    dirty_ = true;
    save_->setEnabled(true);
    status_->setText(TR("Nem mentett módosítások."));
}

void ItemEditorDialog::setMarkerSeconds(int marker, double seconds) { spin_[marker]->setValue(seconds); }

void ItemEditorDialog::setTitleText(const QString &title)
{
    title_->setText(title);
    markDirty();
}

void ItemEditorDialog::playFrom(qint64 ms)
{
    emit previewStarting();                    // a főablak és a Mix editor előhallgatása leáll
    auto &pfl = engine_->source(eter::Engine::Pfl);
    std::string err;
    pfl.takeFinished();
    if (!pfl.load(item_.path.toStdString(), &err)) {
        status_->setText(TR("Nem játszható le: %1").arg(QString::fromStdString(err)));
        return;
    }
    if (ms > 0) pfl.seek(ms * eter::kRate / 1000);
    pfl.play();
    playing_ = true;
    play_->setText(TR("■  Leállítás"));
    timer_->start();
}

void ItemEditorDialog::stopPreview()
{
    if (!playing_) return;
    auto &pfl = engine_->source(eter::Engine::Pfl);
    pfl.stop();
    pfl.unload();
    releasePreview();
}

void ItemEditorDialog::releasePreview()
{
    if (!playing_) return;
    playing_ = false;
    timer_->stop();
    play_->setText(TR("▶  Előhallgatás"));
    wave_->setPlayhead(-1);
    updateInfo();
}

void ItemEditorDialog::tick()
{
    if (!playing_) return;
    auto &pfl = engine_->source(eter::Engine::Pfl);
    if (pfl.takeFinished() || !pfl.playing()) {
        stopPreview();
        return;
    }
    const qint64 ms = pfl.position() * 1000 / eter::kRate;
    wave_->setPlayhead(ms);
    pos_->setText(QStringLiteral("%1 / %2").arg(clock(ms), clock(durMs_)));
}

bool ItemEditorDialog::save()
{
    const QString title = title_->text().trimmed();
    if (title.isEmpty()) {
        status_->setText(TR("A cím nem lehet üres."));
        title_->setFocus();
        return false;
    }
    qint64 cue = markerMs(MarkerWave::CueIn), intro = markerMs(MarkerWave::Intro), outro = markerMs(MarkerWave::Outro);
    if (intro > 0 && intro <= cue) {
        status_->setText(TR("Az intro vége a kezdőpont után kell legyen."));
        return false;
    }
    if (outro > 0 && outro <= std::max(cue, intro)) {
        status_->setText(TR("Az outro kezdete a kezdőpont és az intro vége után kell legyen."));
        return false;
    }
    db_->updateItemDetails(item_.id, category_->currentData().toInt(), title, artist_->text(), album_->text(), year_->value());
    db_->setItemMarkers(item_.id, cue, intro, outro);
    int mask = 0;
    for (int p = 0; p < Scheduler::kDayparts; ++p)
        if (dayparts_[p]->isChecked()) mask |= 1 << p;
    if (mask == (1 << Scheduler::kDayparts) - 1) mask = 0;
    db_->setItemFlow({item_.id}, energy_->currentData().toInt(), bpm_->value(), mask);
    item_ = db_->item(item_.id);
    dirty_ = false;
    save_->setEnabled(false);
    setWindowTitle(TR("Elem szerkesztése – %1").arg(item_.title));
    wave_->setAutoOutro(item_.userMixOutMs > 0 ? 0 : item_.mixOutMs);
    status_->setText(TR("Elmentve."));
    emit db_->categoriesChanged();             // a kategóriák elemszáma változhatott
    db_->notifyItemsChanged();
    emit saved(item_.id);
    return true;
}

void ItemEditorDialog::reject()
{
    if (dirty_ && QMessageBox::question(this, TR("Nem mentett módosítások"),
                                        TR("A módosítások nincsenek elmentve. Bezárod mentés nélkül?"),
                                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    stopPreview();
    QDialog::reject();
}

void ItemEditorDialog::closeEvent(QCloseEvent *e)
{
    e->ignore();
    reject();                                  // a bezárás gomb is megkérdezi a nem mentett módosításokat
}
