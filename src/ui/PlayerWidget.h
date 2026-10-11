#pragma once
#include "audio/Engine.h"

#include <QFrame>
#include <atomic>
#include <memory>
#include <thread>

class QComboBox;
class QLabel;
class QPushButton;
class WaveformWidget;
class MeterWidget;

class PlayerWidget : public QFrame {
    Q_OBJECT
public:
    PlayerWidget(eter::Engine *engine, eter::Engine::Channel channel, QWidget *parent = nullptr);
    ~PlayerWidget() override;

    bool loadFile(const QString &path, QString *err = nullptr);
    void eject();
    bool isLoaded() const { return src_->loaded(); }
    bool isPlaying() const { return src_->playing(); }
    bool isFading() const { return src_->fading(); }
    QString path() const { return QString::fromStdString(src_->path()); }

    int rowId = -1;                       // a betöltött playlist-sor azonosítója
    double autoMixDb = -24.0;             // automatikus mix pont küszöbe

    qint64 position() const { return src_->position(); }
    qint64 duration() const { return src_->duration(); }
    // Az a pont, ahol a következő elemnek indulnia kell; −1, amíg a hullámforma nincs kész.
    qint64 mixOutFrame() const { return mixOut_; }
    // „finished" = végigment, „mixed" = kiúsztatva ért véget
    QString finishReason() const { return fadeRequested_ ? QStringLiteral("mixed") : QStringLiteral("finished"); }

    void play();
    void pause();
    void stop();
    void fadeOut(int ms = 2500, eter::FadeCurve curve = eter::FadeCurve::Quadratic);
    // A kezdőpont beállítása (ha a lejátszó áll, oda is ugrik); a Stop és a CUE ide tér vissza.
    void cueTo(qint64 frame);
    qint64 cueFrame() const { return cue_; }
    // Az intro vége (az ének kezdete): a hullámformán jelölő, lejátszás közben visszaszámláló.
    void setIntro(qint64 frame) { intro_ = frame; }
    // Indítás beúsztatással (a Mix editorban beállított átmenethez).
    void playMixed(int fadeInMs, double startLevelDb);

    // A főablak időzítője hívja ~30 ms-onként.
    void tick(float *peakL = nullptr, float *peakR = nullptr);
    void setDevices(const QStringList &names, const QString &current);

signals:
    void outputChosen(const QString &deviceName);
    void stateChanged();
    void finished();                      // végére ért vagy kiúszott
    void ejected();                       // a felhasználó kiadta
    void filesDropped(const QStringList &paths);
    void itemsDropped(const QVector<int> &itemIds);
    void scanned(qint64 totalFrames);     // elkészült a hullámforma, ismert a pontos hossz

protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

private:
    void refreshState();
    void cancelScan();
    void startScan(const QString &path);

    eter::Engine *engine_;
    eter::Engine::Channel ch_;
    eter::Source *src_;

    QLabel *pill_, *title_, *artist_, *remainCaption_, *remain_, *elapsed_;
    QComboBox *output_;
    WaveformWidget *wave_;
    MeterWidget *meter_;
    QPushButton *playBtn_, *pauseBtn_, *stopBtn_, *cueBtn_, *fadeBtn_, *ejectBtn_;

    std::thread scanThread_;
    std::shared_ptr<std::atomic<bool>> scanCancel_;
    int scanGeneration_ = 0;
    qint64 mixOut_ = -1;
    bool fadeRequested_ = false;
    qint64 cue_ = 0;
    qint64 intro_ = 0;
    QLabel *introLabel_ = nullptr;
    QString lastState_;
};
