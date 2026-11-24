#pragma once
// Külső hangstream letöltése (Icecast / Shoutcast, http vagy https) a hangmotornak. Saját szálon, saját
// eseményhurokkal fut; a .pls / .m3u lejátszási listát feloldja, az ICY metaadatot (StreamTitle) kiszedi a
// hangadatból. Megszakadáskor újrakapcsolódik; ha a megadott ideig nem jön adat, a read() 0-t ad, és az elem
// véget ér (a playlist továbblép).
#include "audio/Stream.h"

#include <QThread>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>

class QtStreamInput : public QThread, public eter::StreamInput {
public:
    explicit QtStreamInput(const QString &url, int giveUpMs = 15000);
    ~QtStreamInput() override;

    int read(uint8_t *buf, int size) override;
    void abort() override;
    void setPlaying(bool on) override;
    std::string streamTitle() const override;
    std::string stationName() const override;
    std::string state() const override;

    // Beállítások → Általános: ennyi ideig próbál újrakapcsolódni, mielőtt az elem véget ér.
    static void setGiveUpMs(int ms);
    static int giveUpMs();
    // A gyár, amit a hangmotor használ (main.cpp állítja be).
    static void install();

protected:
    void run() override;

private:
    friend class StreamWorker;
    void push(const char *data, qint64 n);
    void setState(const QString &s);
    void setMeta(const QString &title, const QString &name);

    QString url_;
    int giveUpMs_;
    std::atomic<bool> aborted_{false};
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<uint8_t> buf_;
    std::chrono::steady_clock::time_point lastData_;
    std::string title_, name_, state_;
    bool gaveUp_ = false;
    bool playing_ = false;
    bool startedOnce_ = false;
};
