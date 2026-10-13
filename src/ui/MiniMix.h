// A playlistbe nyíló mini mix editor: a kimenő elem vége és a következő elem eleje egymás alatt.
// Egyetlen dolgot lehet benne állítani: a következő elemet a kimenő végére (vagy beljebb) úsztatni,
// vagyis a mix pontot. Minden mást a nagy Mix editor állít.
#pragma once
#include "ui/MixEditorPage.h"

#include <QWidget>

class QLabel;
class QPushButton;

class MiniMixCanvas : public QWidget {
    Q_OBJECT
public:
    explicit MiniMixCanvas(QWidget *parent = nullptr);
    void setPair(const QString &outTitle, const QString &inTitle, const MixParams &p);
    void setWaves(const QVector<float> &outPeaks, qint64 outDur, const QVector<float> &inPeaks, qint64 inDur);
    void setPlayhead(qint64 outMs);            // −1: nem látszik
    qint64 mixMs() const { return mix_; }

signals:
    void mixChanged(qint64 mixOutMs);          // az egér felengedésekor
    void dragging(qint64 mixOutMs);            // húzás közben

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void wheelEvent(QWheelEvent *) override;

private:
    double xOf(qint64 outMs) const;
    QRectF inRect() const;
    QString outTitle_, inTitle_;
    MixParams p_;
    QVector<float> outPeaks_, inPeaks_;
    qint64 outDur_ = 0, inDur_ = 0;
    qint64 mix_ = 0, viewStart_ = 0, window_ = 24000, playhead_ = -1;
    bool drag_ = false;
    double dragX_ = 0;
    qint64 dragMix_ = 0;
};

class MiniMix : public QWidget {
    Q_OBJECT
public:
    explicit MiniMix(QWidget *parent = nullptr);
    void setPair(int fromEntry, int toEntry, const QString &outTitle, const QString &inTitle, const MixParams &p, int kind);
    void setWaves(const QVector<float> &outPeaks, qint64 outDur, const QVector<float> &inPeaks, qint64 inDur);
    void setPlayhead(qint64 outMs) { canvas_->setPlayhead(outMs); }
    void setPreviewing(bool on);
    int fromEntry() const { return from_; }
    int toEntry() const { return to_; }
    MiniMixCanvas *canvas() { return canvas_; }

signals:
    void mixChanged(qint64 mixOutMs);
    void previewRequested();
    void resetRequested();
    void editorRequested();

private:
    void updateInfo(qint64 mix);
    MiniMixCanvas *canvas_;
    QLabel *info_;
    QPushButton *preview_, *reset_, *editor_;
    MixParams p_;
    int kind_ = 0;
    int from_ = 0, to_ = 0;
};
