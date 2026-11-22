#pragma once
// Mini mixer a főablak óra-paneljén: a három lejátszó, a voice track, a cartwall és a fő kimenet jelszintje
// (a fader után) és fadere, némítással. A faderek a mix és a hangerőgörbe fölött hatnak: az egész csatorna lesz
// hangosabb vagy halkabb. A beállítás (dB) újraindítás után is megmarad; a némítás nem (biztonsági okból).
#include <QColor>
#include <QString>
#include <QWidget>

#include <array>
#include <functional>

class ChannelMixer : public QWidget {
    Q_OBJECT
public:
    enum Strip { A = 0, B = 1, C = 2, Vt = 3, Cart = 4, Out = 5, Count = 6 };
    static constexpr double kMinDb = -60.0, kMaxDb = 6.0;

    explicit ChannelMixer(QWidget *parent = nullptr);

    void feed(int strip, float peakL, float peakR);   // a 30 ms-os időzítőből: a csatorna csúcsszintje (lineáris)
    double db(int strip) const { return strips_[strip].db; }   // a fader állása; kMinDb alatt: −∞
    bool muted(int strip) const { return strips_[strip].muted; }
    float gain(int strip) const;                      // lineáris erősítés (némítva 0)
    void setDb(int strip, double db);
    void setMuted(int strip, bool on);
    void setOnAir(int strip, bool on);
    void resetAll();                                  // minden csatorna 0 dB, némítás nélkül
    QString debugText() const;                        // fejlesztéshez

    // Némítás előtt megkérdezhető (pl. az adásban lévő lejátszónál); igaz: mehet.
    std::function<bool(int strip)> confirmMute;

    QSize sizeHint() const override { return {240, 150}; }
    QSize minimumSizeHint() const override { return {180, 120}; }

signals:
    void changed();                                   // fader vagy némítás változott

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    bool event(QEvent *e) override;                   // súgó a csatorna nevével és értékével

private:
    struct S {
        double db = 0.0;
        bool muted = false, onAir = false, clip = false;
        double levelL = -100, levelR = -100;          // a kijelzett szint (lassan esik)
        double holdDb = -100;
        int holdTicks = 0;
    };
    std::array<S, Count> strips_;
    int drag_ = -1;
    double dragOffset_ = 0;

    int stripAt(const QPoint &p) const;
    QRectF stripRect(int i) const;
    QRectF meterRect(int i) const;                    // a jelszint és a fader sávja
    QRectF muteRect(int i) const;
    QRectF clipRect(int i) const;
    double yOf(const QRectF &m, double db) const;
    double dbAt(const QRectF &m, double y) const;
    static double posOf(double db);                   // 0..1 (alulról); kMinDb alatt 0
    static double dbOfPos(double p);
    static QString label(int i);
    static QColor color(int i);
    static QString name(int i);
    void store(int strip);
    void toggleMute(int strip);
};
