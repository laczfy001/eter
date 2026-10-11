// Egy lejátszható csatorna (Lejátszó 1, Lejátszó 2 vagy előhallgatás).
// A dekódolás saját szálon tölt egy körpuffert; a hangkártya visszahívása
// (mixInto) ebből olvas, zárolás és memóriafoglalás nélkül.
#pragma once
#include "Decoder.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace eter {

enum class FadeCurve { Quadratic = 0, Linear = 1, Log = 2, SCurve = 3 };

// Az úsztatás alakja: p = 0..1 előrehaladásnál a `from` és `to` erősítés közötti érték.
float fadeGain(FadeCurve curve, float from, float to, float p);

class Source {
public:
    Source();
    ~Source();

    // Felületi szálról hívható
    bool load(const std::string &path, std::string *err = nullptr);
    void unload();
    bool loaded() const { return loaded_.load(); }
    const std::string &path() const { return path_; }
    const std::string &title() const { return title_; }
    const std::string &artist() const { return artist_; }

    // fadeInMs > 0: a megadott szintről (lineáris erősítés) úszik be.
    void play(int fadeInMs = 0, float fromGain = 0.f, FadeCurve curve = FadeCurve::Linear);
    void pause();
    void stop();                      // megáll és a kezdőpontra áll
    void seek(int64_t frame);
    void fadeOutAndStop(int ms, FadeCurve curve = FadeCurve::Quadratic);
    void setGain(float linear) { gain_.store(linear); }
    // Halkítás beszéd alatt (voice track): 1,0 = nincs; a váltás kb. 0,2 mp alatt, kattanás nélkül megy végbe.
    void setDuck(float linear) { duck_.store(linear); }

    bool playing() const { return playing_.load(); }
    bool fading() const { return fadeTotal_.load() > 0 && fadeStops_.load(); }
    int64_t position() const;
    int64_t duration() const { return duration_.load(); }
    void setDuration(int64_t frames) { duration_.store(frames); }

    // Igaz, ha a legutóbbi hívás óta a lejátszás a végére ért vagy kiúszott.
    bool takeFinished() { return finished_.exchange(false); }
    void takePeaks(float &l, float &r);

    // Hangszálról hívandó: hozzákeveri a saját jelét `out`-hoz.
    void mixInto(float *out, int frames);

private:
    void decodeLoop();
    void stopThread();

    static constexpr int64_t kRingFrames = kRate * 2;

    Decoder dec_;
    std::string path_, title_, artist_;
    std::thread thread_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> loaded_{false};

    std::vector<float> ring_;
    std::atomic<int64_t> wr_{0}, rd_{0};
    std::mutex ringMtx_;              // a hangszál csak try_lock-kal nyúl hozzá
    std::atomic<bool> decodedEof_{false};
    std::atomic<bool> flushPending_{false};
    std::atomic<int64_t> seekReq_{-1};

    std::atomic<bool> playing_{false};
    std::atomic<bool> finished_{false};
    std::atomic<int64_t> pos_{0};
    std::atomic<int64_t> duration_{0};
    std::atomic<float> gain_{1.f};
    std::atomic<float> duck_{1.f};
    float duckNow_ = 1.f;               // csak a hangszál használja
    std::atomic<int64_t> fadeTotal_{0}, fadeLeft_{0};
    std::atomic<float> fadeFrom_{1.f}, fadeTo_{0.f};
    std::atomic<int> fadeCurve_{0};
    std::atomic<bool> fadeStops_{true};
    std::atomic<float> peakL_{0.f}, peakR_{0.f};
};

} // namespace eter
