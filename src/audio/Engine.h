// Hangmotor: kimeneti eszközök kezelése és a három csatorna útválasztása.
// Minden használatban lévő kimenethez egy eszköz nyílik; az ugyanarra a
// kimenetre állított csatornákat annak visszahívása keveri össze.
#pragma once
#include "Source.h"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eter {

struct DeviceInfo {
    std::string name;
    bool isDefault = false;
};

class Engine {
public:
    // Pfl2 … Pfl5: a Mix editor további előhallgató csatornái; mindig a Pfl kimenetén szólnak.
    // Vt: az adásban szóló voice track; mindig az 1-es lejátszó kimenetén szól.
    // Cart1 … Cart8: a cartwall hangjai; közös kimenetük a Cart1 útválasztásával állítható.
    // Ovl1, Ovl2: az átmenetre ráfektetett playlist-elemek; az 1-es lejátszó kimenetén szólnak.
    // Pfl6, Pfl7: a ráfektetett elemek előhallgatása a Mix editorban; a Pfl kimenetén szólnak.
    enum Channel { Player1 = 0, Player2 = 1, Pfl = 2, Pfl2 = 3, Vt = 4, Pfl3 = 5, Pfl4 = 6, Pfl5 = 7,
                   Cart1 = 8, CartLast = 15, Ovl1 = 16, Ovl2 = 17, Pfl6 = 18, Pfl7 = 19, ChannelCount = 20 };
    static constexpr int kOverlayVoices = 2;
    static constexpr int kCartVoices = 8;
    static constexpr int kRoutedChannels = 3;

    Engine();
    ~Engine();

    // backend: "" (automatikus), "pulseaudio", "alsa", "jack", "null"
    bool init(const std::string &backend, std::string *err = nullptr);
    void shutdown();
    bool ready() const;
    std::string backendName() const;

    std::vector<DeviceInfo> devices();

    // deviceName: "" = a rendszer alapértelmezett kimenete
    bool setRoute(Channel ch, const std::string &deviceName, std::string *err = nullptr);
    const std::string &route(Channel ch) const { return routes_[ch]; }
    bool routed(Channel ch) const { return routed_[ch]; }

    Source &source(Channel ch) { return sources_[ch]; }
    void testTone(Channel ch, int ms = 1200);

    // Felvétel az alapértelmezett bemenetről (mikrofon), mono, 48 kHz.
    bool startRecording(std::string *err = nullptr);
    std::vector<float> stopRecording();
    bool recording() const;
    float recordPeak();                // csúcsszint a legutóbbi lekérdezés óta

    // Adásrögzítéshez: az adásba szóló csatornák (lejátszók, voice track, ráfektetés, cartwall) jele, az előhallgatás
    // és a teszthang nélkül, sztereó 48 kHz. A felület szálán hívandó; ha több kimenet visz adást, összeadja őket.
    void setProgramTap(bool on);
    bool programTap() const { return tapOn_; }
    int readProgram(float *dst, int maxFrames);        // ennyi képkocka került dst-be

    struct Impl;
    struct OutDev;
    struct Recorder;

private:
    void releaseDevice(const std::string &key, Channel ch);
    bool setRouteOne(Channel ch, const std::string &deviceName, std::string *err);

    std::unique_ptr<Impl> impl_;
    std::array<Source, ChannelCount> sources_;
    std::array<std::string, ChannelCount> routes_;
    std::array<bool, ChannelCount> routed_{};
    std::map<std::string, std::unique_ptr<OutDev>> outs_;
    std::unique_ptr<Recorder> rec_;
    bool tapOn_ = false;
    std::vector<float> tapMix_;
};

} // namespace eter
