// Mix editor: két vagy három egymást követő elem átmeneteinek beállítása idővonalon, előhallgatással.
#pragma once
#include "audio/Engine.h"
#include "db/Database.h"

#include <QHash>
#include <QPointer>
#include <QWidget>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

class QCheckBox;
class QComboBox;
class QDialog;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QScrollBar;
class QTableWidget;
class QTimer;

struct MixPair {
    int fromEntry = 0, toEntry = 0;
    Item from, to;
    QString startText;               // a bejövő elem becsült kezdése
    bool fromOnAir = false;          // a kimenő elem éppen adásban van
    bool toOnAir = false;
};

struct MixParams {
    qint64 mixOutMs = 0;             // a kimenő elem ezen pontján indul a bejövő
    qint64 outEndMs = 0;             // a kimenő elem itt hallgat el (a saját idejében); 0 = mix pont + kiúsztatás
    qint64 fadeOutMs = 2500;         // a vég előtti kiúsztatás hossza
    QString curve = "scurve";
    qint64 cueInMs = 0;
    qint64 fadeInMs = 0;
    double startLevelDb = 0;
    qint64 introMs = 0;              // a bejövő elem introjának vége (a szám saját idejében); 0 = nincs
    int vtItemId = 0;                // voice track; 0 = nincs
    qint64 vtOffsetMs = 0;           // kezdése a mix ponthoz képest
    double vtDuckDb = -9;
};

inline eter::FadeCurve curveFromName(const QString &name)
{
    if (name == "linear") return eter::FadeCurve::Linear;
    if (name == "log") return eter::FadeCurve::Log;
    if (name == "scurve") return eter::FadeCurve::SCurve;
    return eter::FadeCurve::Quadratic;
}

// Idővonal 2 vagy 3 elemmel (1 vagy 2 átmenettel) és a voice track sávval.
// Az időtengely az első elem saját ideje; a többi elem az előző mix pontjánál kezdődik.
class MixTimeline : public QWidget {
    Q_OBJECT
public:
    enum Handle { None, MixPoint, FadeOutEnd, FadeInEnd, StartLevel, Block, Intro, VtBlock, TrimLeft, TrimRight };
    struct Clip {
        QString title;
        QVector<float> peaks;
        qint64 durMs = 0;
    };

    explicit MixTimeline(QWidget *parent = nullptr);
    void setClips(const QVector<Clip> &clips);              // 2 vagy 3 elem
    void setClipWave(int k, const QVector<float> &peaks, qint64 durationMs);
    void setVtWave(int j, const QVector<float> &peaks, qint64 durationMs, const QString &title);
    void setParams(int j, const MixParams &p, bool recenter);
    const MixParams &params(int j) const { return tr_[j]; }
    int transitions() const { return tr_.size(); }
    int active() const { return active_; }
    void setActive(int j);
    void setWindowMs(qint64 ms);
    void zoomAt(qint64 ms, double x);   // nagyítás az x képpontnál lévő időpont körül
    qint64 windowMs() const { return window_; }
    void setPlayhead(qint64 ms);     // az idővonal idejében; −1 = nincs
    void recenter();
    // Vízszintes görgetés: a nézet bal széle és a görgethető tartomány az idővonal idejében.
    qint64 viewStart() const { return viewStart_; }
    void setViewStart(qint64 ms);
    qint64 contentStart() const;
    qint64 contentEnd() const;

    qint64 cue(int k) const;         // a k. elem kezdőpontja a saját idejében
    qint64 clipStart(int k) const;   // a k. elem kezdete az idővonalon
    qint64 clipEnd(int k) const;     // a k. elem hallható vége az idővonalon
    qint64 outEndTrack(int j) const; // a j. átmenet kimenő elemének vége a saját idejében
    QPointF handlePos(Handle h, int j) const;

signals:
    void paramsEdited(int j);
    void activeChanged(int j);
    void viewChanged();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    struct Hit {
        Handle h = None;
        int j = 0;
    };
    double xOf(qint64 ms) const;
public:
    qint64 msAt(double x) const;
private:
    QRectF laneRect(int lane) const;                        // 0..n−1: elemek, n: voice track
    qint64 naturalEnd(int k) const;
    qint64 vtLen(int j) const { return std::max<qint64>(vt_[j].durMs, 500); }
    Hit hitTest(const QPointF &pos) const;
    void drawWave(QPainter &p, const QRectF &lane, const QVector<float> &peaks, qint64 durMs,
                  qint64 trackAtLeftMs, const QColor &color) const;

    QVector<Clip> clips_;
    QVector<MixParams> tr_;
    QVector<Clip> vt_;
    int active_ = 0;
    qint64 viewStart_ = 0, window_ = 24000;
    qint64 playhead_ = -1;
    Hit drag_;
    double dragOffsetMs_ = 0;
};

class MixEditorPage : public QWidget {
    Q_OBJECT
public:
    MixEditorPage(Database *db, eter::Engine *engine, QWidget *parent = nullptr);
    ~MixEditorPage() override;

    void setPairs(const QVector<MixPair> &pairs);
    void stopPreview();
    bool previewing() const { return phase_ != 0; }
    MixTimeline *timeline() { return timeline_; }

signals:
    void mixSaved(const QVector<int> &itemIds);
    void previewStarting();
    void message(const QString &text);

private:
    struct Wave {
        QVector<float> peaks;
        qint64 durMs = 0, autoMixMs = 0;
        bool ready = false;
    };
public:
    void selectPair(int index);
private:
    void loadCurrent();
    MixParams resolve(const MixPair &pair, int *kind) const;
    void requestWave(const Item &item);
    void waveReady(int itemId);
    void showParams();
    void fieldsEdited();
    void save();
    bool saveOne(int j, QVector<int> &changedItems);
    void resetToAuto();
    void togglePreview();
    void startPreview(bool forRecording);
    void previewTick();
    void applyVt(int j);
    void setVtItem(int itemId);
    void chooseVtFile();
    void chooseVtFromLibrary();
    void toggleRecord();
    void finishRecording();
    void refreshTable();
    int currentIndex() const;
    int shown() const { return timeline_->transitions(); }      // a szerkesztett átmenetek száma
    const MixPair &pairOf(int j) const { return pairs_[currentIndex() + j]; }
    bool anyDirty() const { return dirty_[0] || dirty_[1]; }

    Database *db_;
    eter::Engine *engine_;
    QVector<MixPair> pairs_;
    int fromEntry_ = 0, toEntry_ = 0;      // a szerkesztett (első) pár
    int shownTo2_ = 0;                      // hármas nézetben a második átmenet bejövő sora
    bool dirty_[2] = {false, false};
    int kind_[2] = {0, 0};                  // 0 automatikus, 1 a számok alapértelmezése, 2 egyedi
    bool loading_ = false;

    MixTimeline *timeline_;
    QScrollBar *hscroll_;
    void syncScrollBar();
    QLabel *title_, *state_, *mixClock_;
    QPushButton *previewBtn_, *triple_, *act_[2];
    QWidget *actRow_;
    QTableWidget *table_;
    QDoubleSpinBox *outEnd_;
    QDoubleSpinBox *mixOut_, *fadeOut_, *cueIn_, *fadeIn_, *startLevel_, *intro_, *vtOffset_, *vtDuck_;
    QLabel *vtName_, *outCaption_, *inCaption_;
    QPushButton *recordBtn_, *vtFile_, *vtLibrary_, *vtRemove_;
    QCheckBox *loop_;
    QComboBox *curve_;
    QRadioButton *scopePair_, *scopeItems_;
    QPushButton *save_, *reset_;

    QHash<int, Wave> waves_;
    std::vector<std::thread> threads_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    double autoMixDb_ = -24.0;
    qint64 defaultFadeMs_ = 2500;

    // Előhallgatás
    QTimer *timer_;
    int phase_ = 0;                         // 0 áll, 1 szól
    struct {
        int n = 2;                          // elemek száma
        MixParams tr[2];
        qint64 cue[3] = {0, 0, 0}, start[3] = {0, 0, 0};
        int started = 0;                    // a legutóbb elindított elem
        bool vtStarted[2] = {false, false};
        bool faded[3] = {false, false, false};  // az elem kiúsztatása már elindult
        qint64 dur[3] = {0, 0, 0};
        qint64 vtDur[2] = {0, 0};
        int vtPlaying = 0;
    } pv_;
    bool recordMode_ = false;
    int recordFor_ = 0;                     // melyik átmenethez készül a felvétel
    qint64 previewStartMs_ = 0;             // az idővonalon, ahonnan az előhallgatás indult
    QPointer<QDialog> vtDialog_;
};
