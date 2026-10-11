// FFmpeg-alapú dekóder: bármely támogatott fájlt 48 kHz sztereó float mintákra alakít.
#pragma once
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct SwrContext;
struct AVFrame;
struct AVPacket;

namespace eter {

constexpr int kRate = 48000;
constexpr int kChannels = 2;

class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;

    bool open(const std::string &path, std::string *err = nullptr);
    void close();
    bool isOpen() const { return fmt_ != nullptr; }

    // A konténer szerinti hossz képkockában (48 kHz); becslés lehet, 0 = ismeretlen.
    int64_t durationFrames() const { return duration_; }
    const std::string &title() const { return title_; }
    const std::string &artist() const { return artist_; }

    // Legfeljebb `frames` sztereó képkockát ír `out`-ba; 0 = fájl vége.
    int read(float *out, int frames);
    bool seek(int64_t frame);

private:
    bool decodeMore();
    void convert();

    AVFormatContext *fmt_ = nullptr;
    AVCodecContext *ctx_ = nullptr;
    SwrContext *swr_ = nullptr;
    AVFrame *frame_ = nullptr;
    AVPacket *pkt_ = nullptr;
    int stream_ = -1;
    int64_t startPts_ = 0;
    int64_t duration_ = 0;
    int64_t skipTo_ = -1;
    bool eof_ = false;
    bool drainSent_ = false;
    int errors_ = 0;
    std::vector<float> fifo_;
    size_t fifoPos_ = 0;
    std::string title_, artist_;
};

// Teljes fájl végigolvasása a hullámformához: `bins` darab csúcsérték (0..1)
// és a pontos hossz. `cancel` igazra állítva megszakítható.
bool scanPeaks(const std::string &path, int bins, std::vector<float> &peaks,
               int64_t &totalFrames, const std::atomic<bool> &cancel);

// Automatikus mix pont: az utolsó hely, ahol a jel még a küszöb (dB) fölött van.
int64_t autoMixOut(const std::vector<float> &peaks, int64_t totalFrames, double thresholdDb);

} // namespace eter
