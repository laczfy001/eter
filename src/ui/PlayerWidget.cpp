#include "i18n.h"
#include "PlayerWidget.h"
#include "Theme.h"
#include "Widgets.h"

#include <QComboBox>
#include <QDataStream>
#include <cmath>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

using eter::Engine;

static void repolish(QWidget *w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

PlayerWidget::PlayerWidget(Engine *engine, Engine::Channel channel, QWidget *parent)
    : QFrame(parent), engine_(engine), ch_(channel), src_(&engine->source(channel))
{
    setObjectName("panel");
    setAcceptDrops(true);
    const int number = ch_ == Engine::Player1 ? 1 : 2;

    auto *badge = new QLabel(QString::number(number));
    badge->setObjectName(number == 1 ? "badge1" : "badge2");
    auto *caption = new QLabel(TR("LEJÁTSZÓ %1").arg(number));
    caption->setObjectName("caption");
    pill_ = new QLabel;
    pill_->setObjectName("pill");
    auto *outLabel = new QLabel(TR("Kimenet"));
    outLabel->setObjectName("small");
    output_ = new QComboBox;
    output_->setMinimumWidth(170);
    output_->setMaximumWidth(240);
    output_->setFocusPolicy(Qt::NoFocus);
    output_->setToolTip(TR("Melyik hangkimeneten szóljon ez a lejátszó"));
    outLabel->setBuddy(output_);

    auto *head = new QHBoxLayout;
    head->setSpacing(8);
    head->addWidget(badge);
    head->addWidget(caption);
    head->addWidget(pill_);
    head->addStretch(1);
    // A kimenet a Beállításokban választható; itt nem jelenik meg.
    outLabel->setParent(this);
    outLabel->hide();
    output_->setParent(this);
    output_->hide();

    title_ = new QLabel;
    title_->setObjectName("trackTitle");
    title_->setTextFormat(Qt::PlainText);
    title_->setMinimumWidth(60);
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    artist_ = new QLabel;
    artist_->setObjectName("muted");
    artist_->setTextFormat(Qt::PlainText);
    artist_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    wave_ = new WaveformWidget;
    wave_->setPlayedColor(number == 1 ? theme::amber : theme::blue);

    auto *left = new QVBoxLayout;
    left->setSpacing(2);
    left->addWidget(title_);
    left->addWidget(artist_);
    left->addSpacing(4);
    left->addWidget(wave_);

    remainCaption_ = new QLabel(TR("HÁTRA"));
    remainCaption_->setObjectName("caption");
    remainCaption_->setAlignment(Qt::AlignRight);
    remain_ = new QLabel;
    remain_->setObjectName("bigTime");
    remain_->setAlignment(Qt::AlignRight);
    remain_->setMinimumWidth(130);
    elapsed_ = new QLabel;
    elapsed_->setObjectName("mono");
    elapsed_->setAlignment(Qt::AlignRight);

    auto *right = new QVBoxLayout;
    right->setSpacing(2);
    right->addWidget(remainCaption_);
    right->addWidget(remain_);
    introLabel_ = new QLabel;
    introLabel_->setObjectName("intro");
    introLabel_->setAlignment(Qt::AlignRight);
    right->addWidget(elapsed_);
    right->addWidget(introLabel_);
    right->addStretch(1);

    auto *mid = new QHBoxLayout;
    mid->setSpacing(14);
    mid->addLayout(left, 1);
    mid->addLayout(right);

    auto transport = [](QPushButton *b, const QString &tip) {
        b->setProperty("transport", true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setIconSize(QSize(16, 16));
    };
    playBtn_ = new QPushButton;
    transport(playBtn_, TR("Lejátszás"));
    pauseBtn_ = new QPushButton;
    pauseBtn_->setIcon(theme::icon(theme::Icon::Pause, theme::text));
    transport(pauseBtn_, TR("Szünet"));
    stopBtn_ = new QPushButton;
    stopBtn_->setIcon(theme::icon(theme::Icon::Stop, theme::text));
    transport(stopBtn_, TR("Stop – megáll és az elejére áll"));
    ejectBtn_ = new QPushButton;
    ejectBtn_->setIcon(theme::icon(theme::Icon::Eject, theme::text));
    transport(ejectBtn_, TR("Kiadás"));
    cueBtn_ = new QPushButton(TR("CUE"));
    cueBtn_->setProperty("tall", true);
    cueBtn_->setFocusPolicy(Qt::NoFocus);
    cueBtn_->setToolTip(TR("Vissza a kezdőpontra, készenlétbe"));
    fadeBtn_ = new QPushButton(TR("Kiúsztatás"));
    fadeBtn_->setProperty("tall", true);
    fadeBtn_->setFocusPolicy(Qt::NoFocus);
    fadeBtn_->setToolTip(TR("2,5 másodperc alatt lehalkít, majd megáll"));
    meter_ = new MeterWidget(6);
    meter_->setMaximumWidth(220);

    auto *foot = new QHBoxLayout;
    foot->setSpacing(6);
    foot->addWidget(playBtn_);
    foot->addWidget(pauseBtn_);
    foot->addWidget(stopBtn_);
    foot->addWidget(cueBtn_);
    foot->addWidget(fadeBtn_);
    foot->addWidget(ejectBtn_);
    foot->addStretch(1);
    foot->addWidget(meter_, 2);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(8);
    root->addLayout(head);
    root->addLayout(mid);
    root->addLayout(foot);

    connect(playBtn_, &QPushButton::clicked, this, &PlayerWidget::play);
    connect(pauseBtn_, &QPushButton::clicked, this, &PlayerWidget::pause);
    connect(stopBtn_, &QPushButton::clicked, this, &PlayerWidget::stop);
    connect(cueBtn_, &QPushButton::clicked, this, &PlayerWidget::stop);
    connect(fadeBtn_, &QPushButton::clicked, this, [this] { fadeOut(); });
    connect(ejectBtn_, &QPushButton::clicked, this, [this] {
        eject();
        emit ejected();
    });
    connect(wave_, &WaveformWidget::seekRequested, this, [this](double f) {
        if (src_->loaded() && src_->duration() > 0)
            src_->seek(static_cast<qint64>(f * src_->duration()));
    });
    connect(output_, &QComboBox::activated, this, [this](int index) {
        emit outputChosen(output_->itemData(index).toString());
    });

    refreshState();
}

PlayerWidget::~PlayerWidget() { cancelScan(); }

void PlayerWidget::cancelScan()
{
    ++scanGeneration_;
    if (scanCancel_) scanCancel_->store(true);
    if (scanThread_.joinable()) scanThread_.join();
    scanCancel_.reset();
}

void PlayerWidget::startScan(const QString &path)
{
    cancelScan();
    wave_->clear();
    wave_->setLoading(true);
    const int generation = scanGeneration_;
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    scanCancel_ = cancel;
    const std::string p = path.toStdString();
    scanThread_ = std::thread([this, p, cancel, generation] {
        std::vector<float> peaks;
        int64_t total = 0;
        const bool ok = eter::scanPeaks(p, 1200, peaks, total, *cancel);
        if (!ok || cancel->load()) return;
        QVector<float> v(peaks.begin(), peaks.end());
        QMetaObject::invokeMethod(this, [this, v, total, generation] {
            if (generation != scanGeneration_ || !src_->loaded()) return;
            wave_->setPeaks(v);
            src_->setDuration(total);     // a teljes végigolvasás pontos hosszt ad
            // Automatikus mix pont: az utolsó hely, ahol a jel még a küszöb fölött van.
            const float threshold = static_cast<float>(std::pow(10.0, autoMixDb / 20.0));
            int last = v.size() - 1;
            while (last > 0 && v[last] < threshold) --last;
            mixOut_ = std::min<qint64>(total, static_cast<qint64>(last + 1) * total / v.size());
            emit scanned(total);
        }, Qt::QueuedConnection);
    });
}

bool PlayerWidget::loadFile(const QString &path, QString *err)
{
    cancelScan();
    mixOut_ = -1;
    cue_ = 0;
    intro_ = 0;
    fadeRequested_ = false;
    std::string e;
    if (!src_->load(path.toStdString(), &e)) {
        if (err) *err = QString::fromStdString(e);
        wave_->clear();
        refreshState();
        return false;
    }
    QString t = QString::fromStdString(src_->title());
    if (t.isEmpty()) t = QFileInfo(path).completeBaseName();
    title_->setText(t);
    title_->setToolTip(path);
    artist_->setText(QString::fromStdString(src_->artist()));
    startScan(path);
    refreshState();
    emit stateChanged();
    return true;
}

void PlayerWidget::eject()
{
    cancelScan();
    src_->unload();
    rowId = -1;
    mixOut_ = -1;
    wave_->clear();
    title_->clear();
    title_->setToolTip({});
    artist_->clear();
    refreshState();
    emit stateChanged();
}

void PlayerWidget::play()
{
    if (!src_->loaded()) return;
    // Ha a szám a végén áll, elölről indul.
    if (src_->duration() > 0 && src_->position() >= src_->duration() - 2400) src_->seek(cue_);
    fadeRequested_ = false;
    src_->play();
    refreshState();
    emit stateChanged();
}

void PlayerWidget::pause()
{
    src_->pause();
    refreshState();
    emit stateChanged();
}

void PlayerWidget::stop()
{
    src_->pause();
    src_->seek(cue_);                     // a kezdőpontra áll vissza
    refreshState();
    emit stateChanged();
}

void PlayerWidget::cueTo(qint64 frame)
{
    cue_ = std::max<qint64>(0, frame);
    if (src_->loaded() && !src_->playing()) src_->seek(cue_);
    refreshState();
}

void PlayerWidget::playMixed(int fadeInMs, double startLevelDb)
{
    if (!src_->loaded()) return;
    fadeRequested_ = false;
    src_->play(fadeInMs, static_cast<float>(std::pow(10.0, startLevelDb / 20.0)));
    refreshState();
    emit stateChanged();
}

void PlayerWidget::fadeOut(int ms, eter::FadeCurve curve)
{
    fadeRequested_ = true;
    src_->fadeOutAndStop(ms, curve);
}

void PlayerWidget::refreshState()
{
    const bool loaded = src_->loaded();
    const bool playing = src_->playing();
    const bool atStart = src_->position() == cue_;
    QString state = !loaded ? "empty" : playing ? "onair" : atStart ? "ready" : "paused";
    if (state != lastState_) {
        lastState_ = state;
        pill_->setText(state == "empty" ? TR("ÜRES")
                       : state == "onair" ? TR("ADÁSBAN")
                       : state == "ready" ? TR("KÉSZENLÉT") : TR("SZÜNET"));
        pill_->setProperty("state", state);
        repolish(pill_);
        setProperty("onair", playing);
        repolish(this);
        remain_->setProperty("onair", playing);
        repolish(remain_);
        playBtn_->setProperty("lit", playing);
        playBtn_->setIcon(theme::icon(theme::Icon::Play, playing ? theme::bg : theme::text, playing));
        repolish(playBtn_);
        remainCaption_->setText(playing || state == "paused" ? TR("HÁTRA") : TR("HOSSZ"));
    }
    playBtn_->setEnabled(loaded && !playing);
    pauseBtn_->setEnabled(playing);
    stopBtn_->setEnabled(loaded && (playing || !atStart));
    cueBtn_->setEnabled(loaded && !atStart);
    fadeBtn_->setEnabled(playing);
    ejectBtn_->setEnabled(loaded && !playing);
    // Adásban lévő lejátszó kimenete nem váltható, mert megszakítaná a hangot.
    output_->setEnabled(!playing);
    if (!loaded) {
        remain_->setText(QStringLiteral("–:––"));
        elapsed_->setText(TR("üres"));
    }
}

void PlayerWidget::tick(float *peakL, float *peakR)
{
    float l = 0.f, r = 0.f;
    src_->takePeaks(l, r);
    meter_->feed(l, r);
    if (peakL) *peakL = l;
    if (peakR) *peakR = r;

    if (src_->takeFinished()) {
        refreshState();
        emit finished();
        return;
    }
    refreshState();
    if (!src_->loaded()) {
        introLabel_->clear();
        wave_->setMarker(-1);
        return;
    }

    const qint64 dur = src_->duration();
    // Intro: amíg tart, visszaszámlál; előtte a hosszát mutatja.
    {
        const qint64 p = src_->position();
        if (intro_ > cue_ && p < intro_) {
            const qint64 left = intro_ - p;
            const bool live = src_->playing();
            introLabel_->setText(TR("INTRO %1:%2%3")
                                     .arg(left / eter::kRate / 60)
                                     .arg(left / eter::kRate % 60, 2, 10, QLatin1Char('0'))
                                     .arg(live && left < 10 * eter::kRate ? QStringLiteral(".%1").arg(left * 10 / eter::kRate % 10) : QString()));
            if (introLabel_->property("live").toBool() != live) {
                introLabel_->setProperty("live", live);
                introLabel_->style()->unpolish(introLabel_);
                introLabel_->style()->polish(introLabel_);
            }
        } else {
            introLabel_->clear();
        }
        wave_->setMarker(intro_ > 0 && dur > 0 ? static_cast<double>(intro_) / dur : -1.0);
    }
    const qint64 rawPos = src_->position();
    const qint64 pos = dur > 0 ? std::min(rawPos, dur) : rawPos;
    const bool running = src_->playing() || pos != cue_;
    if (dur > 0) {
        remain_->setText(running ? QStringLiteral("−") + theme::formatTime(dur - pos + 47999)
                                 : theme::formatTime(dur));
        elapsed_->setText(theme::formatTime(pos) + QStringLiteral(" / ") + theme::formatTime(dur));
        wave_->setProgress(static_cast<double>(pos) / dur);
    } else {
        remain_->setText(theme::formatTime(pos));
        elapsed_->setText(TR("ismeretlen hossz"));
    }
}

void PlayerWidget::setDevices(const QStringList &names, const QString &current)
{
    output_->blockSignals(true);
    output_->clear();
    output_->addItem(TR("Alapértelmezett kimenet"), QString());
    for (const QString &n : names) output_->addItem(n, n);
    int idx = output_->findData(current);
    if (idx < 0) {
        output_->addItem(TR("(nem található) ") + current, current);
        idx = output_->count() - 1;
    }
    output_->setCurrentIndex(idx);
    output_->blockSignals(false);
}

void PlayerWidget::dragEnterEvent(QDragEnterEvent *e)
{
    if ((e->mimeData()->hasUrls() || e->mimeData()->hasFormat("application/x-eter-items")) && !src_->playing())
        e->acceptProposedAction();
}

void PlayerWidget::dropEvent(QDropEvent *e)
{
    if (e->mimeData()->hasFormat("application/x-eter-items")) {
        QVector<int> ids;
        QDataStream in(e->mimeData()->data("application/x-eter-items"));
        in >> ids;
        e->acceptProposedAction();
        if (!ids.isEmpty()) emit itemsDropped(ids);
        return;
    }
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    if (!paths.isEmpty()) {
        e->acceptProposedAction();
        emit filesDropped(paths);
    }
}
