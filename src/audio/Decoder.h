// FFmpeg-alapú dekóder: bármely támogatott fájlt 48 kHz sztereó float mintákra alakít.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct SwrContext;
struct AVFrame;
struct AVPacket;
struct AVIOContext;

namespace eter {

class StreamInput;
constexpr int kRate = 48000;
constexpr int kChannels = 2;

class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder &) = delete;
    Decoder &operator=(const Decoder &) = delete;

    bool open(const std::string &path, std::string *err = nullptr);   // stream címet nem nyit meg (lásd openStream)
    // Kihagyandó fájlok (amelyek korábban összeomlást okoztak): a program indításkor állítja be. true: ne nyissa meg.
    static void setOpenGuard(bool (*guard)(const std::string &path));
    // Külső stream: a bájtokat az `input` adja; blokkolhat, amíg az első adatok megérkeznek (a dekódoló szálon hívandó).
    bool openStream(std::shared_ptr<StreamInput> input, std::string *err = nullptr);
    void close();
    bool isStream() const { return input_ != nullptr; }
    const std::string &codecName() const { return codec_; }
    bool isOpen() const { return fmt_ != nullptr; }

    // A konténer szerinti hossz képkockában (48 kHz); becslés lehet, 0 = ismeretlen.
    int64_t durationFrames() const { return duration_; }
    const std::string &title() const { return title_; }
    const std::string &artist() const { return artist_; }
    const std::string &keyTag() const { return key_; }   // a fájlban tárolt hangnem-címke (TKEY / INITIALKEY)
    const std::string &bpmTag() const { return bpm_; }

    // Legfeljebb `frames` sztereó képkockát ír `out`-ba; 0 = fájl vége.
    int read(float *out, int frames);
    bool seek(int64_t frame);

private:
    bool decodeMore();
    void convert();

    AVFormatContext *fmt_ = nullptr;
    AVCodecContext *ctx_ = nullptr;
    SwrContext *swr_ = nullptr;
    int swrRate_ = 0, swrFormat_ = -1, swrChannels_ = 0;   // amire az átalakító készült
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
    std::string title_, artist_, key_, bpm_, codec_;
    std::string path_;                      // a megnyitott fájl (az összeomlás-jelentéshez)
    std::shared_ptr<StreamInput> input_;
    AVIOContext *avio_ = nullptr;
    bool finishOpen(std::string *err);
};

// Teljes fájl végigolvasása a hullámformához: `bins` darab csúcsérték (0..1)
// és a pontos hossz. `cancel` igazra állítva megszakítható.
bool scanPeaks(const std::string &path, int bins, std::vector<float> &peaks,
               int64_t &totalFrames, const std::atomic<bool> &cancel,
               std::vector<float> *env100ms = nullptr);

// Automatikus mix pont: az utolsó hely, ahol a jel még a küszöb (dB) fölött van.
int64_t autoMixOut(const std::vector<float> &peaks, int64_t totalFrames, double thresholdDb);

} // namespace eter
