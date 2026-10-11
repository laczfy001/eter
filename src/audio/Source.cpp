#include "Source.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace eter {

Source::Source() : ring_(static_cast<size_t>(kRingFrames) * kChannels, 0.f) {}

Source::~Source() { stopThread(); }

void Source::stopThread()
{
    playing_.store(false);
    quit_.store(true);
    if (thread_.joinable()) thread_.join();
    quit_.store(false);
}

bool Source::load(const std::string &path, std::string *err)
{
    unload();
    if (!dec_.open(path, err)) return false;
    path_ = path;
    title_ = dec_.title();
    artist_ = dec_.artist();
    duration_.store(dec_.durationFrames());
    {
        std::lock_guard<std::mutex> lk(ringMtx_);
        wr_.store(0);
        rd_.store(0);
        pos_.store(0);
    }
    decodedEof_.store(false);
    flushPending_.store(false);
    seekReq_.store(-1);
    finished_.store(false);
    fadeTotal_.store(0);
    fadeLeft_.store(0);
    loaded_.store(true);
    thread_ = std::thread(&Source::decodeLoop, this);
    return true;
}

void Source::unload()
{
    stopThread();
    loaded_.store(false);
    dec_.close();
    path_.clear();
    title_.clear();
    artist_.clear();
    duration_.store(0);
    pos_.store(0);
    finished_.store(false);
    fadeTotal_.store(0);
    duck_.store(1.f);
    duckNow_ = 1.f;
    peakL_.store(0.f);
    peakR_.store(0.f);
}

float fadeGain(FadeCurve curve, float from, float to, float p)
{
    p = std::clamp(p, 0.f, 1.f);
    switch (curve) {
    case FadeCurve::Linear: return from + (to - from) * p;
    case FadeCurve::SCurve: return from + (to - from) * (p * p * (3.f - 2.f * p));
    case FadeCurve::Log: {
        // dB-ben egyenletes: a fül számára egyenletes halkulás; −60 dB alatt csendnek számít.
        const float a = 20.f * std::log10(std::max(from, 0.001f));
        const float b = 20.f * std::log10(std::max(to, 0.001f));
        if (p >= 1.f) return to;
        return std::pow(10.f, (a + (b - a) * p) / 20.f);
    }
    default: return from + (to - from) * (p * (2.f - p));
    }
}

void Source::play(int fadeInMs, float fromGain, FadeCurve curve)
{
    if (!loaded_.load()) return;
    fadeTotal_.store(0);
    fadeLeft_.store(0);
    if (fadeInMs > 0) {
        const int64_t frames = std::max<int64_t>(1, static_cast<int64_t>(fadeInMs) * kRate / 1000);
        fadeFrom_.store(fromGain);
        fadeTo_.store(1.f);
        fadeCurve_.store(static_cast<int>(curve));
        fadeStops_.store(false);
        fadeLeft_.store(frames);
        fadeTotal_.store(frames);
    }
    finished_.store(false);
    playing_.store(true);
}

void Source::pause() { playing_.store(false); }

void Source::stop()
{
    playing_.store(false);
    fadeTotal_.store(0);
    seek(0);
}

void Source::seek(int64_t frame)
{
    if (!loaded_.load()) return;
    const int64_t dur = duration_.load();
    if (dur > 0) frame = std::min(frame, dur);
    frame = std::max<int64_t>(0, frame);
    flushPending_.store(true);
    seekReq_.store(frame);
}

void Source::fadeOutAndStop(int ms, FadeCurve curve)
{
    if (!playing_.load()) return;
    const int64_t frames = std::max<int64_t>(1, static_cast<int64_t>(ms) * kRate / 1000);
    fadeTotal_.store(0);                 // egy esetleg még futó beúsztatás leállítása
    fadeFrom_.store(1.f);
    fadeTo_.store(0.f);
    fadeCurve_.store(static_cast<int>(curve));
    fadeStops_.store(true);
    fadeLeft_.store(frames);
    fadeTotal_.store(frames);
}

int64_t Source::position() const
{
    const int64_t s = seekReq_.load();
    return s >= 0 ? s : pos_.load();
}

void Source::takePeaks(float &l, float &r)
{
    l = peakL_.exchange(0.f);
    r = peakR_.exchange(0.f);
}

void Source::decodeLoop()
{
    constexpr int kChunk = 2048;
    std::vector<float> tmp(kChunk * kChannels);
    while (!quit_.load()) {
        const int64_t s = seekReq_.load();
        if (s >= 0) {
            {
                std::lock_guard<std::mutex> lk(ringMtx_);
                wr_.store(0);
                rd_.store(0);
                pos_.store(s);
                decodedEof_.store(false);
            }
            dec_.seek(s);
            // Ha közben újabb pozicionálás érkezett, azt a következő körben kezeljük.
            int64_t expected = s;
            if (seekReq_.compare_exchange_strong(expected, -1))
                flushPending_.store(false);
            continue;
        }

        const int64_t space = kRingFrames - (wr_.load() - rd_.load());
        if (space >= kChunk && !decodedEof_.load()) {
            const int n = dec_.read(tmp.data(), kChunk);
            if (n <= 0) {
                decodedEof_.store(true);
                continue;
            }
            // Egy író, egy olvasó: az írás nem zárol, így nem akasztja meg a hangszálat.
            // (A puffer ürítése ugyanezen a szálon történik, ezért nem ütközhet vele.)
            const int64_t w = wr_.load();
            for (int i = 0; i < n; ++i) {
                const size_t idx = static_cast<size_t>((w + i) % kRingFrames) * kChannels;
                ring_[idx] = tmp[i * 2];
                ring_[idx + 1] = tmp[i * 2 + 1];
            }
            wr_.store(w + n);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    }
}

void Source::mixInto(float *out, int frames)
{
    if (!playing_.load(std::memory_order_relaxed) || flushPending_.load(std::memory_order_relaxed))
        return;
    std::unique_lock<std::mutex> lk(ringMtx_, std::try_to_lock);
    if (!lk.owns_lock()) return;

    const int64_t r = rd_.load();
    const int64_t avail = wr_.load() - r;
    const int n = static_cast<int>(std::min<int64_t>(frames, avail));

    const float gain = gain_.load(std::memory_order_relaxed);
    const int64_t fadeTotal = fadeTotal_.load(std::memory_order_relaxed);
    int64_t fadeLeft = fadeLeft_.load(std::memory_order_relaxed);
    const float fadeFrom = fadeFrom_.load(std::memory_order_relaxed);
    const float fadeTo = fadeTo_.load(std::memory_order_relaxed);
    const FadeCurve curve = static_cast<FadeCurve>(fadeCurve_.load(std::memory_order_relaxed));
    const bool fadeStops = fadeStops_.load(std::memory_order_relaxed);
    const float duckTarget = duck_.load(std::memory_order_relaxed);
    float duck = duckNow_;
    float pl = 0.f, pr = 0.f;

    for (int i = 0; i < n; ++i) {
        duck += (duckTarget - duck) * 0.0004f;
        float g = gain * duck;
        if (fadeTotal > 0) {
            const float p = 1.f - static_cast<float>(fadeLeft) / static_cast<float>(fadeTotal);
            g *= fadeGain(curve, fadeFrom, fadeTo, p);
            if (fadeLeft > 0) --fadeLeft;
        }
        const size_t idx = static_cast<size_t>((r + i) % kRingFrames) * kChannels;
        const float l = ring_[idx] * g;
        const float rr = ring_[idx + 1] * g;
        out[i * 2] += l;
        out[i * 2 + 1] += rr;
        pl = std::max(pl, std::fabs(l));
        pr = std::max(pr, std::fabs(rr));
    }
    duckNow_ = duck;
    rd_.store(r + n);
    pos_.fetch_add(n);

    if (pl > peakL_.load(std::memory_order_relaxed)) peakL_.store(pl, std::memory_order_relaxed);
    if (pr > peakR_.load(std::memory_order_relaxed)) peakR_.store(pr, std::memory_order_relaxed);

    if (fadeTotal > 0 && fadeTotal_.load(std::memory_order_relaxed) == fadeTotal) {
        fadeLeft_.store(fadeLeft);
        if (fadeLeft == 0) {
            fadeTotal_.store(0);
            if (fadeStops) {
                playing_.store(false);
                finished_.store(true);
            }
        }
    }
    if (fadeTotal > 0 && fadeStops && fadeLeft == 0) {
        // kiúszott
    } else if (n < frames && decodedEof_.load() && wr_.load() == r + n) {
        playing_.store(false);
        finished_.store(true);
    }
}

} // namespace eter
