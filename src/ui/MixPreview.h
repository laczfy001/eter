// Az automatikus mix beállításainak grafikus előnézete (Beállítások → Automatikus mix): két mintaelem (a választott
// helyzet szerint: lecsengő vagy hirtelen vég, lágy vagy erős kezdés, eltérő tempó, jingle, rejtett szám) átmenete a
// lapon éppen beállított értékekkel, ugyanazzal a tervezővel, amely adás közben dönt (planAutoMix).
#pragma once

#include "audio/SmartMix.h"

#include <QString>
#include <QStringList>
#include <QWidget>

class MixPreview : public QWidget {
    Q_OBJECT
public:
    explicit MixPreview(QWidget *parent = nullptr);

    static QStringList scenarioNames();
    void setScenario(int index);
    int scenario() const { return scenario_; }
    // A lap értékei: az okos mix beállításai és az egyszerű mód küszöbei (dB) és kiúsztatása (ms).
    void setValues(const eter::SmartMixSettings &s, double mixThresholdDb, double cueThresholdDb, int defaultFadeMs);
    QString summary() const { return summary_; }

    QSize sizeHint() const override { return {640, 236}; }
    QSize minimumSizeHint() const override { return {360, 236}; }

signals:
    void summaryChanged(const QString &text);

protected:
    void paintEvent(QPaintEvent *) override;

private:
    struct Item {
        QString name;
        bool music = true;
        double bpm = 0;
        int key = -1;
        std::vector<float> env;     // 100 ms-onként a csúcsszint (lineáris, 0–1)
        qint64 durMs = 0;
    };
    struct Result {
        bool smart = false;         // az okos mix tervezett (különben az egyszerű mód)
        qint64 mixOutMs = 0, outEndMs = 0, cueInMs = 0, hiddenAtMs = -1;
        int fadeOutMs = 0;
        eter::SmartMixPlan plan;
    };
    void recompute();
    static void makeScenario(int index, Item *out, Item *in);

    eter::SmartMixSettings s_;
    double mixDb_ = -24, cueDb_ = -45;
    int fadeMs_ = 2500;
    int scenario_ = 0;
    Item out_, in_;
    Result r_;
    QString summary_;
};
