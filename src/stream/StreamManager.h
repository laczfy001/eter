// A stream enkóderek gyűjteménye (Stream fül): betöltés és mentés (QSettings „stream/…”), a hang szétosztása a
// források szerint, az adásban lévő cím enkóderenként (a Now playing sablonja vagy a sajátja szerint), az
// indításkori automatikus indulás és a kifelé menő sávszél összege.
#pragma once

#include "core/NowPlaying.h"
#include "stream/StreamEncoder.h"

#include <QObject>
#include <QSet>
#include <QVector>

class StreamManager : public QObject {
    Q_OBJECT
public:
    explicit StreamManager(QObject *parent = nullptr);
    ~StreamManager() override;

    const QVector<stream::Encoder *> &encoders() const { return encoders_; }
    stream::Encoder *find(const QString &id) const;
    stream::Encoder *add();                            // új enkóder az alapértékekkel
    void remove(const QString &id);
    void update(const stream::Config &c);              // mentés is
    void startAll();
    void stopAll();
    void autoStart();                                  // a program indulásakor

    // A források, amelyekre most enkóder vár: a program (fő kimenet) és a bemenetek eszközneve.
    bool wantsProgram() const;
    QSet<QString> inputDevices() const;
    void feedProgram(const float *stereo, int frames);
    void feedInput(const QString &device, const float *stereo, int frames);

    void setNowPlayingConfig(const NowPlaying::Config &c);
    void setTracks(const NowPlaying::Track &onAir, const NowPlaying::Track &next);
    bool wantsTitles() const;

    int activeCount() const;                           // bekapcsolt enkóderek
    int onAirCount() const;
    bool anyError() const;
    int totalKbps() const;                             // a bekapcsolt enkóderek bitrátájának összege
    int uploadLimitKbps() const { return limit_; }     // 0: nincs megadva
    void setUploadLimitKbps(int kbps);

signals:
    void listChanged();
    void statusChanged();
    void event(const QString &encoderId, const QString &line);

private:
    void save();
    void attach(stream::Encoder *e);
    void refreshTitles(bool force);

    QVector<stream::Encoder *> encoders_;
    NowPlaying::Config np_;
    NowPlaying::Track onAir_, next_, lastMusic_;
    bool everPlayed_ = false;
    QHash<QString, QString> sentTitle_;
    int limit_ = 0;
};
