#pragma once
// Mixdown: elemek sorozatából (a fölöttük szóló voice trackekkel) egyetlen hangfájl, mintapontosan. A Mix editortól
// és a playlisttől teljesen független: az átmenetek behúzáskor az okos mix szerint állnak be, utána szabadon
// állíthatók, és csak a mixdownban (és a mentett mixdown projektben) élnek. Az eredmény MP3 vagy WAV, és a hangtár
// „Mixdown” kategóriájába kerül.
#include "audio/Mixdown.h"
#include "audio/SmartMix.h"
#include "db/Database.h"
#include "core/VolumeEnvelope.h"

#include <QHash>
#include <QVector>
#include <QWidget>

#include <atomic>
#include <memory>
#include <thread>

namespace eter { class Engine; }
class QLabel;
class QLineEdit;
class QListWidget;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QToolButton;
class QScrollBar;
class QTimer;
class MdTimeline;
class MdSeqList;

class MixdownPage : public QWidget {
    Q_OBJECT
public:
    MixdownPage(Database *db, eter::Engine *engine, QWidget *parent = nullptr);
    ~MixdownPage() override;

    void stopPreview();
    void addItems(const QVector<int> &ids, int at = -1);   // a hangtárból (behúzás, dupla kattintás)
    QString debugState() const;                           // fejlesztéshez: elemek, átmenetek, hossz
    bool exportTo(const QString &path, bool mp3, int kbps, int bits, int rate, bool limit, bool toLibrary,
                  QString *err);                          // fejlesztéshez: kikeverés várakozással (a felület nélkül)
    void loadProjectFile(const QString &path);
    void setVoiceTrack(int i, const QString &path, const QString &title);   // üres útvonal: törlés

    // Egy elem a mixdownban; a mezők ms-ban, a fájlbeli időben.
    struct Entry {
        Item item;
        qint64 durMs = 0;
        qint64 cueIn = 0;            // innen szól
        qint64 mixOut = 0;           // itt indul a következő
        qint64 end = 0;              // eddig szól
        int fadeOut = 0;             // kiúsztatás a vége előtt
        bool manualMix = false, manualCue = false;
        // Voice track az átmenet fölött (a következő elem kezdéséhez képest)
        QString vtPath, vtTitle;
        qint64 vtDurMs = 0, vtOffsetMs = 0;
        double vtDuckDb = -9;
        int lane = -1;               // a sáv az idővonalon (0 vagy 1); −1: váltakozva
        env::Envelope env;           // hangerőgörbe (csak a mixdownban; az elem saját görbéjéből indul)
    };
    struct Wave {
        QVector<float> peaks;
        qint64 durMs = 0;
        eter::MixProfile profile;
        bool ready = false;
    };
    const QVector<Entry> &entries() const { return entries_; }
    qint64 startOf(int i) const { return i >= 0 && i < starts_.size() ? starts_[i] : 0; }
    qint64 totalMs() const { return total_; }
    const Wave *wave(const QString &path) const;
    int selected() const { return selected_; }
    qint64 playheadMs() const;
    bool previewing() const { return previewing_; }

    // Az idővonal szerkesztései
    void moveClip(int i, qint64 startMs);       // az i. elem kezdése (az előző mix pontja)
    void trimStart(int i, qint64 cueInMs);
    void trimEnd(int i, qint64 endMs);
    void moveVt(int i, qint64 offsetMs);
    void select(int i);
    void selectVt(int i);                       // az i. elem voice trackje van kijelölve (a törlés csak azt törli)
    bool vtSelected() const { return vtSelected_; }
    void deleteSelection();                     // a kijelölt voice track, vagy ha az nincs kijelölve, a kijelölt elem
    void placeVoice(const QString &path, const QString &title);   // a kijelölés helyére (lásd voiceTargetMs)
    qint64 voiceTargetMs() const;
    void setPlayhead(qint64 ms);
    int laneOf(int i) const;
    void setLane(int i, int lane);
    // Hangerőgörbe (a pont indexét adja vissza a rendezés után)
    int envAdd(int i, qint64 ms, double db);
    int envMove(int i, int point, qint64 ms, double db);
    void envRemove(int i, int point);
    // Behúzás közvetlenül az idővonalra: a megadott időpontra és sávra (a voice track sávra: voice track)
    void dropItems(const QVector<int> &ids, qint64 ms, int lane);
    void dropVoice(int itemId, qint64 ms);

signals:
    void message(const QString &text);
    void previewStarting();                     // a többi előhallgatás álljon le
    void exported(int itemId);

private:
    Database *db_;
    eter::Engine *engine_;
    QVector<Entry> entries_;
    QVector<qint64> starts_;
    qint64 total_ = 0;
    int selected_ = -1;
    qint64 playhead_ = 0;
    QHash<QString, Wave> waves_;
    eter::SmartMixSettings smart_;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::vector<std::thread> threads_;
    bool previewing_ = false;
    qint64 previewFrom_ = 0;
    QString previewPath_;
    bool loading_ = false;
    bool vtSelected_ = false;
    void attachVoiceAt(const QString &path, const QString &title, qint64 ms);

    QLineEdit *name_;
    QLabel *total_label_;
    MdSeqList *seq_;
    MdTimeline *timeline_;
    QScrollBar *scroll_;
    QLabel *trTitle_, *trKind_;
    QDoubleSpinBox *mixOut_, *fadeOut_, *endAt_, *cueIn_, *vtStart_, *vtDuck_;
    QPushButton *trPlay_, *trAuto_, *playBtn_, *envBtn_;
    QLabel *envHint_;
    QToolButton *vtBtn_;
    QLineEdit *libSearch_;
    QComboBox *libCat_;
    QListWidget *lib_;
    QTimer *timer_;

    void requestWave(const Item &item);
    void recompute();                           // automatikus átmenetek, kezdések, hossz
    void autoTransition(int i);
    void refreshList();
    void showTransition();
    void refreshLibrary();
    void remove();
    void moveSelected(int delta);
    void reorder(const QVector<int> &order);
    eter::MixdownJob buildJob() const;
    void startPreview(qint64 fromMs, qint64 lengthMs);
    void tick();
    void openExportDialog();
    void newProject();
    void saveProject();
    void openProjectMenu();
    QString projectDir() const;
    void recordVoiceTrack();
    void pickVoiceFile();
    void pickVoiceLibrary();
    void reapThreads();
};
