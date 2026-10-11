// Saját rajzolású kis elemek: hullámforma és szintmérő.
#pragma once
#include <QVector>
#include <QWidget>

class WaveformWidget : public QWidget {
    Q_OBJECT
public:
    explicit WaveformWidget(QWidget *parent = nullptr);
    void setPeaks(const QVector<float> &peaks);
    void clear();
    void setProgress(double fraction);          // 0..1
    void setPlayedColor(const QColor &c) { played_ = c; update(); }
    void setLoading(bool on) { loading_ = on; update(); }
    void setMarker(double fraction);            // intro vége; negatív = nincs
    QSize sizeHint() const override { return {300, 44}; }

signals:
    void seekRequested(double fraction);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;

private:
    QVector<float> peaks_;
    double progress_ = 0.0;
    bool loading_ = false;
    double marker_ = -1.0;
    QColor played_;
};

class MeterWidget : public QWidget {
    Q_OBJECT
public:
    explicit MeterWidget(int barHeight = 6, QWidget *parent = nullptr);
    // Lineáris csúcsértékek (0..1+); a visszaesést a widget maga kezeli.
    void feed(float left, float right);
    double levelDb() const;
    QSize sizeHint() const override { return {160, bar_ * 2 + 3}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    int bar_;
    float l_ = 0.f, r_ = 0.f;
};
