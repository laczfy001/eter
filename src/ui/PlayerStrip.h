#pragma once
#include <QColor>
#include <QWidget>

// A főablak lejátszója alatti sáv: az A, B és C lejátszó állapota, és amíg szól, a voice track.
// Egy lapkára kattintva az a lejátszó látszik fölötte.
class PlayerStrip : public QWidget {
    Q_OBJECT
public:
    enum State { Empty, Loaded, Ready, OnAir, Leaving, Sounding, Paused };
    struct Tile {
        QString letter;
        QColor color;
        State state = Empty;
        QString title;
        QString time;
        QString output;
        bool shown = false;               // ez látszik fölötte
    };
    explicit PlayerStrip(QWidget *parent = nullptr);
    void setTiles(const QVector<Tile> &tiles);
    // Voice track: üres cím = nem szól.
    void setVoiceTrack(const QString &title, const QString &state, const QString &time);
    QSize sizeHint() const override { return {600, 50}; }
    QString debugText() const;            // fejlesztői ellenőrzéshez

signals:
    void tileClicked(int index);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    QRect tileRect(int i) const;
    int count() const { return tiles_.size() + (vtTitle_.isEmpty() ? 0 : 1); }
    QVector<Tile> tiles_;
    QString vtTitle_, vtState_, vtTime_;
};
