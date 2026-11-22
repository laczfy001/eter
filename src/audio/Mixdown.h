#pragma once
// Mixdown: elemek sorozatának (és a fölöttük szóló voice trackeknek) összekeverése egyetlen hangfájlba, mintapontosan.
// A hangmotortól független: a fájlokat saját dekóderrel olvassa, a háttérben, a valós időnél gyorsabban.
#include "Decoder.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace eter {

struct MixdownClip {
    std::string path;
    int64_t start = 0;             // az eredményben itt kezdődik (képkocka, 48 kHz)
    int64_t in = 0;                // a fájlban innen szól
    int64_t end = 0;               // a fájlban eddig szól (kizárólagos)
    int64_t fadeIn = 0, fadeOut = 0;   // úsztatás az elején / a vége előtt (képkocka)
    std::vector<std::pair<int64_t, float>> env;   // az elem hangerőgörbéje (fájlbeli képkocka, lineáris erősítés)
    bool voice = false;            // voice track: a zene halkul alatta, ő maga nem
    float duck = 1.f;              // voice tracknél: ennyire halkul alatta a zene (lineáris)
    int64_t length() const { return std::max<int64_t>(0, end - in); }
};

struct MixdownJob {
    std::vector<MixdownClip> clips;
    int64_t total() const;
};

// Egy szakasz kikeverése a memóriába (sztereó, 48 kHz, float). Előhallgatáshoz és a fájlba íráshoz.
class MixdownRenderer {
public:
    explicit MixdownRenderer(const MixdownJob &job);
    ~MixdownRenderer();
    // A [from, from+frames) szakasz; egymás utáni hívásoknál folyamatosan olvas (nem keres újra).
    void render(int64_t from, int frames, float *out);

private:
    struct Open;
    MixdownJob job_;
    std::vector<std::unique_ptr<Open>> open_;
    std::vector<float> tmp_;
    float duckAt(int64_t t) const;
};

struct MixdownOutput {
    std::string path;
    bool mp3 = true;
    int kbps = 320;
    int bits = 16;                 // WAV: 16 vagy 24
    int rate = 48000;              // 48000 vagy 44100
    bool limitPeak = true;         // a csúcs −1 dBFS-re (csak lehalkít)
};

// A teljes keverés fájlba; progress 0..1 (a korlátozásnál két menetben). Hamis és err: hiba vagy megszakítás.
bool renderMixdown(const MixdownJob &job, const MixdownOutput &o, const std::function<void(double)> &progress,
                   const std::atomic<bool> &cancel, std::string *err);

// Rövid szakasz WAV-ba (48 kHz, 16 bit) az előhallgatáshoz.
bool renderPreviewWav(const MixdownJob &job, int64_t from, int64_t frames, const std::string &path);

} // namespace eter
