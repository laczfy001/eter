#include "i18n.h"
#include "PlayerWidget.h"
#include "Theme.h"
#include "Widgets.h"

#include <QComboBox>
#include <QPainter>
#include <QPainterPath>
#include <QTextBrowser>
#include <QToolButton>
#include "net/ArtistInfo.h"
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
    index_ = ch_ == Engine::Player1 ? 0 : ch_ == Engine::Player2 ? 1 : 2;

    auto *badge = new QLabel(letter());
    badge->setObjectName(index_ == 0 ? "badge1" : index_ == 1 ? "badge2" : "badge3");
    caption_ = new QLabel(TR("%1 LEJÁTSZÓ").arg(letter()));
    caption_->setObjectName("caption");
    pill_ = new QLabel;
    pill_->setObjectName("pill");
    vtChip_ = new QLabel;
    vtChip_->setObjectName("vtChip");
    vtChip_->setToolTip(TR("Voice track szól az átmenet fölött"));
    vtChip_->hide();
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
    head->addWidget(pill_);
    head->addWidget(caption_);
    head->addWidget(vtChip_);
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
    wave_->setPlayedColor(theme::playerColor(index_));

    // Előadói adatok: fotó a cím mellett, a tények az előadó után, az életrajz egy gombnyomásra.
    artistPhoto_ = new QLabel;
    artistPhoto_->setObjectName("artistPhoto");
    artistPhoto_->setFixedSize(48, 48);
    artistPhoto_->setScaledContents(false);
    artistPhoto_->hide();
    artistFacts_ = new QLabel;
    artistFacts_->setObjectName("small");
    artistFacts_->setTextFormat(Qt::PlainText);
    artistFacts_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    artistFacts_->hide();
    bioBtn_ = new QToolButton;
    bioBtn_->setObjectName("bioButton");
    bioBtn_->setText(TR("Életrajz"));
    bioBtn_->setToolTip(TR("Az előadó életrajza és adatai"));
    bioBtn_->setFocusPolicy(Qt::NoFocus);
    bioBtn_->setAutoRaise(true);
    bioBtn_->hide();
    connect(bioBtn_, &QToolButton::clicked, this, &PlayerWidget::showBio);
    auto *artistRow = new QHBoxLayout;
    artistRow->setSpacing(8);
    artistRow->addWidget(artist_, 0);
    artistRow->addWidget(artistFacts_, 1);
    artistRow->addWidget(bioBtn_);
    auto *names = new QVBoxLayout;
    names->setSpacing(2);
    names->addWidget(title_);
    names->addLayout(artistRow);
    auto *top = new QHBoxLayout;
    top->setSpacing(10);
    top->addWidget(artistPhoto_, 0, Qt::AlignVCenter);
    top->addLayout(names, 1);
    auto *left = new QVBoxLayout;
    left->setSpacing(2);
    left->addLayout(top);
    left->addSpacing(4);
    left->addWidget(wave_);
    if (ArtistInfoService *info = ArtistInfoService::instance())
        connect(info, &ArtistInfoService::artistUpdated, this, [this](const QString &key) {
            if (key == artistKey_) updateArtistInfo();
        });

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
        std::vector<float> peaks, env;
        int64_t total = 0;
        const bool ok = eter::scanPeaks(p, 1200, peaks, total, *cancel, &env);
        if (!ok || cancel->load() || peaks.empty() || total <= 0) return;
        QVector<float> v(peaks.begin(), peaks.end());
        auto profile = std::make_shared<eter::MixProfile>(eter::analyzeMixProfile(env, total * 1000 / eter::kRate));
        QMetaObject::invokeMethod(this, [this, v, total, generation, profile] {
            if (generation != scanGeneration_ || !src_->loaded()) return;
            profile_ = *profile;
            wave_->setPeaks(v);
            src_->setDuration(total);     // a teljes végigolvasás pontos hosszt ad
            // Automatikus mix pont: az utolsó hely, ahol a jel még a küszöb fölött van.
            mixOut_ = eter::autoMixOut(std::vector<float>(v.begin(), v.end()), total, autoMixDb);
            // A szám eleji csend: az első pont, ahol a jel a kezdőpont küszöbe (alapból −45 dB) fölé ér.
            const float quiet = static_cast<float>(std::pow(10.0, autoCueDb / 20.0));
            int first = 0;
            while (first < v.size() - 1 && v[first] < quiet) ++first;
            leadIn_ = first > 0 ? std::min<qint64>(total / 4, static_cast<qint64>(first) * total / v.size()) : 0;
            emit scanned(total);
        }, Qt::QueuedConnection);
    });
}

void PlayerWidget::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::StyleChange && wave_) wave_->setPlayedColor(theme::playerColor(index_));
    QFrame::changeEvent(e);
}

bool PlayerWidget::loadFile(const QString &path, QString *err)
{
    cancelScan();
    mixOut_ = -1;
    leadIn_ = -1;
    profile_ = eter::MixProfile();
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
    updateArtistInfo();
    if (src_->isStream()) {
        // Élő stream: nincs hullámforma; a hosszát a hangtárban megadott tervezett hossz adja (setStreamLength).
        wave_->clear();
        wave_->setMessage(TR("● ÉLŐ STREAM · kapcsolódás…"));
        leadIn_ = 0;
    } else {
        startScan(path);
    }
    refreshState();
    emit stateChanged();
    return true;
}

void PlayerWidget::setStreamLength(qint64 ms)
{
    if (!src_->isStream()) return;
    const qint64 frames = std::max<qint64>(1, ms) * eter::kRate / 1000;
    src_->setDuration(frames);
    mixOut_ = frames;           // a tervezett hossz végén jön a következő (a stream kiúszik)
}

void PlayerWidget::eject()
{
    cancelScan();
    src_->unload();
    rowId = -1;
    mixOut_ = -1;
    leadIn_ = -1;
    profile_ = eter::MixProfile();
    wave_->clear();
    wave_->setMixMarker(-1, {}, {});
    setVtChip({});
    title_->clear();
    title_->setToolTip({});
    artist_->clear();
    updateArtistInfo();
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
    QString state = !loaded ? "empty" : playing ? (leaving_ ? (sounding_ ? "sounding" : "leaving") : "onair") : atStart ? "ready" : "paused";
    if (state != lastState_) {
        lastState_ = state;
        pill_->setText(state == "empty" ? TR("ÜRES")
                       : state == "onair" ? TR("ADÁSBAN")
                       : state == "leaving" ? TR("KIÚSZIK")
                       : state == "sounding" ? TR("SZÓL")
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
    if (src_->isStream()) {
        const QString title = QString::fromStdString(src_->streamTitle());
        const QString state = QString::fromStdString(src_->streamState());
        wave_->setMessage(TR("● ÉLŐ STREAM") + (title.isEmpty() ? QString() : QStringLiteral(" · ") + title)
                          + (state.isEmpty() ? QString() : QStringLiteral(" · ") + state));
    }
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

void PlayerWidget::setMeta(const QString &title, const QString &artist)
{
    if (!title.isEmpty()) title_->setText(title);
    artist_->setText(artist);
    updateArtistInfo();
}

void PlayerWidget::updateArtistInfo()
{
    ArtistInfoService *info = ArtistInfoService::instance();
    const QString artist = artist_->text();
    ArtistData d;
    if (info && info->anyEnabled() && !artist.isEmpty() && !src_->isStream()) {
        artistKey_ = ArtistInfoService::keyFor(info->primaryName(artist));
        d = info->artist(artist);
    } else {
        artistKey_.clear();
    }
    // A szöveg a rendelkezésre álló helyhez igazodik; a teljes szöveg a buborékban.
    artist_->setSizePolicy(d.found ? QSizePolicy::Preferred : QSizePolicy::Ignored, QSizePolicy::Preferred);
    artistFacts_->setText(d.found ? QStringLiteral("·  ") + d.facts() : QString());
    artistFacts_->setToolTip(d.facts());
    artistFacts_->setVisible(d.found && !d.facts().isEmpty());
    bioBtn_->setVisible(d.found);
    QPixmap pm;
    if (d.found && !d.imageFile.isEmpty() && pm.load(d.imageFile)) {
        // Négyzetesre vágva, lekerekítve.
        const int side = std::min(pm.width(), pm.height());
        pm = pm.copy((pm.width() - side) / 2, (pm.height() - side) / 2, side, side)
                 .scaled(96, 96, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QPixmap round(96, 96);
        round.fill(Qt::transparent);
        QPainter p(&round);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(0, 0, 96, 96), 12, 12);
        p.setClipPath(path);
        p.drawPixmap(0, 0, pm);
        p.end();
        round.setDevicePixelRatio(2.0);
        artistPhoto_->setPixmap(round);
        artistPhoto_->show();
    } else {
        artistPhoto_->clear();
        artistPhoto_->hide();
    }
}

void PlayerWidget::showBio()
{
    ArtistInfoService *info = ArtistInfoService::instance();
    if (!info) return;
    const ArtistData d = info->artist(artist_->text());
    if (!d.found) return;
    auto *pop = new QFrame(this, Qt::Popup);
    pop->setObjectName("bioPopup");
    pop->setAttribute(Qt::WA_DeleteOnClose);
    pop->setStyleSheet(QStringLiteral("QFrame#bioPopup { background: #1b1e24; border: 1px solid #454b57; border-radius: 6px; }"));
    auto *v = new QVBoxLayout(pop);
    v->setContentsMargins(16, 14, 16, 12);
    v->setSpacing(8);
    auto *head = new QHBoxLayout;
    head->setSpacing(14);
    QPixmap pm;
    if (!d.imageFile.isEmpty() && pm.load(d.imageFile)) {
        auto *img = new QLabel;
        img->setPixmap(pm.scaled(120, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        head->addWidget(img, 0, Qt::AlignTop);
    }
    auto *facts = new QVBoxLayout;
    facts->setSpacing(4);
    auto *name = new QLabel(d.name);
    name->setObjectName("trackTitle");
    facts->addWidget(name);
    QStringList lines;
    if (!d.country.isEmpty()) lines << TR("Ország: %1").arg(d.country);
    if (d.formed > 0) lines << TR("Alakult: %1").arg(d.formed) + (d.disbanded.isEmpty() ? QString() : TR(" (feloszlott)"));
    if (d.born > 0) lines << TR("Született: %1").arg(d.born) + (d.died > 0 ? TR(", meghalt: %1").arg(d.died) : QString());
    if (d.members > 2 || (d.members == 2 && !d.sources.contains(QStringLiteral("MusicBrainz")))) lines << TR("Tagok: %1").arg(d.members);
    if (!d.website.isEmpty()) lines << TR("Weboldal: %1").arg(d.website);
    QStringList style;
    for (const QString &x : {d.genre, d.style})
        if (!x.isEmpty() && !style.contains(x)) style << x;
    if (!style.isEmpty()) lines << TR("Műfaj: %1").arg(style.join(QStringLiteral(", ")));
    if (!d.mood.isEmpty()) lines << TR("Hangulat: %1").arg(d.mood);
    if (!d.label.isEmpty()) lines << TR("Kiadó: %1").arg(d.label);
    for (const QString &l : std::as_const(lines)) {
        auto *lab = new QLabel(l);
        lab->setObjectName("small");
        facts->addWidget(lab);
    }
    facts->addStretch(1);
    head->addLayout(facts, 1);
    v->addLayout(head);
    auto *bio = new QTextBrowser;
    bio->setObjectName("bioText");
    bio->setPlainText(d.bio.isEmpty() ? TR("Ehhez az előadóhoz nincs életrajz.") : d.bio);
    bio->setFrameShape(QFrame::NoFrame);
    bio->setStyleSheet(QStringLiteral("QTextBrowser { background: transparent; color: #d4d8de; font-size: 13px; }"));
    v->addWidget(bio, 1);
    auto *src = new QLabel(TR("Forrás: %1").arg(d.sources.isEmpty() ? QStringLiteral("TheAudioDB") : d.sources.join(QStringLiteral(", "))));
    src->setObjectName("small");
    v->addWidget(src);
    pop->resize(560, 440);
    QPoint at = bioBtn_->mapToGlobal(QPoint(0, bioBtn_->height() + 4));
    pop->move(at);
    pop->show();
}

void PlayerWidget::setOutputName(const QString &name)
{
    if (name == outputName_) return;
    outputName_ = name;
    caption_->setText(name.isEmpty() ? TR("%1 LEJÁTSZÓ").arg(letter()) : TR("%1 LEJÁTSZÓ · %2").arg(letter(), name));
}

void PlayerWidget::setVtChip(const QString &text)
{
    if (text.isEmpty()) {
        vtChip_->hide();
        return;
    }
    vtChip_->setText(text);
    vtChip_->show();
}

void PlayerWidget::setMixPoint(qint64 frame, const QString &label, const QColor &color)
{
    const qint64 dur = src_->duration();
    wave_->setMixMarker(frame > 0 && dur > 0 ? double(frame) / double(dur) : -1.0, label, color);
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
