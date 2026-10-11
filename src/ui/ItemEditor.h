// Hangtárelem szerkesztője: az adatok (cím, előadó, album, év, kategória) és a jelölőpontok
// (kezdőpont, intro vége, outro kezdete) beállítása hullámformán, előhallgatással.
#pragma once
#include "audio/Engine.h"
#include "db/Database.h"

#include <QDialog>
#include <QVector>
#include <atomic>
#include <memory>
#include <thread>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTimer;

class MarkerWave : public QWidget {
    Q_OBJECT
public:
    enum Marker { CueIn = 0, Intro = 1, Outro = 2, None = -1 };
    explicit MarkerWave(QWidget *parent = nullptr);
    void setWave(const QVector<float> &peaks, qint64 durMs);
    void setDuration(qint64 durMs);
    void setMarker(Marker m, qint64 ms);       // 0: nincs megadva (Intro, Outro)
    void setAutoOutro(qint64 ms);              // a program által talált mix pont, tájékoztatásul
    void setPlayhead(qint64 ms);               // −1: nem látszik
    qint64 marker(Marker m) const { return ms_[m]; }
    QPointF markerPos(Marker m) const;         // fejlesztői ellenőrzéshez
    double xOf(qint64 ms) const;

signals:
    void markerMoved(int marker, qint64 ms);
    void seekRequested(qint64 ms);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

private:
    qint64 msAt(double x) const;
    Marker hit(const QPointF &pos) const;
    QVector<float> peaks_;
    qint64 durMs_ = 0, autoOutro_ = 0, playhead_ = -1;
    qint64 ms_[3] = {0, 0, 0};
    Marker drag_ = None;
};

class ItemEditorDialog : public QDialog {
    Q_OBJECT
public:
    ItemEditorDialog(Database *db, eter::Engine *engine, int itemId, QWidget *parent);
    ~ItemEditorDialog() override;

    int itemId() const { return item_.id; }
    void releasePreview();                     // más kezdett előhallgatást a közös csatornán
    // Fejlesztői ellenőrzéshez
    MarkerWave *wave() { return wave_; }
    bool waveReady() const { return ready_; }
    void setMarkerSeconds(int marker, double seconds);
    void setTitleText(const QString &title);
    void playFrom(qint64 ms);
    bool previewing() const { return playing_; }
    bool save();

signals:
    void saved(int itemId);
    void previewStarting();
    void message(const QString &text);

protected:
    void closeEvent(QCloseEvent *) override;
    void reject() override;

private:
    void stopPreview();
    void tick();
    void markDirty();
    void updateInfo();
    qint64 markerMs(int m) const;

    Database *db_;
    eter::Engine *engine_;
    Item item_;
    QLineEdit *title_, *artist_, *album_;
    QSpinBox *year_;
    QComboBox *category_;
    MarkerWave *wave_;
    QDoubleSpinBox *spin_[3];
    QPushButton *play_, *save_;
    QLabel *pos_, *info_, *status_;
    QTimer *timer_;
    qint64 durMs_ = 0;
    bool playing_ = false, dirty_ = false, ready_ = false, loading_ = true;
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::thread thread_;
};
