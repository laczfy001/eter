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
    enum Channel { Player1 = 0, Player2 = 1, Pfl = 2, Pfl2 = 3, Vt = 4, Pfl3 = 5, Pfl4 = 6, Pfl5 = 7,
                   Cart1 = 8, CartLast = 15, ChannelCount = 16 };
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
};

} // namespace eter
