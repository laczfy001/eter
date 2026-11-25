// MIDI bemenet a rendszer saját felületén (külső könyvtár nélkül):
//   - Windows: winmm (midiIn…): minden MIDI-eszköz, amely a Windows eszközei között megjelenik;
//   - Linux: az ALSA nyers MIDI-eszközei (/dev/snd/midiC*D*), a nevük a /proc/asound/cards-ból.
// A beérkező üzenetek a callbacken jönnek, a rendszer saját szálán (nem a felület szálán).
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace midi {

struct Port {
    std::string id;         // tartós azonosító (Windows: a név; Linux: az eszközfájl)
    std::string name;       // a felületen
};

// Egy MIDI-üzenet (a rendszerüzenetek, a SysEx és az órajel nélkül).
struct Message {
    uint8_t status = 0;     // 0x80…0xEF (a csatornával együtt)
    uint8_t data1 = 0, data2 = 0;
    int type() const { return status & 0xF0; }
    int channel() const { return (status & 0x0F) + 1; }    // 1–16
};

// A nyers bájtfolyam üzenetekre bontása (futó státusszal; a valós idejű és a SysEx bájtok kimaradnak).
class Parser {
public:
    template <typename F> void feed(const uint8_t *data, size_t n, F &&emit)
    {
        for (size_t i = 0; i < n; ++i) {
            const uint8_t b = data[i];
            if (b >= 0xF8) continue;                    // órajel, start/stop, aktív érzékelés
            if (b >= 0xF0) {                            // rendszerüzenet: SysEx eleje/vége, időkód, …
                sysex_ = b == 0xF0;
                status_ = 0;
                have_ = 0;
                continue;
            }
            if (b & 0x80) {
                status_ = b;
                sysex_ = false;
                have_ = 0;
                continue;
            }
            if (sysex_ || !status_) continue;
            buf_[have_++] = b;
            const int need = (status_ & 0xF0) == 0xC0 || (status_ & 0xF0) == 0xD0 ? 1 : 2;
            if (have_ >= need) {
                emit(Message{status_, buf_[0], need == 2 ? buf_[1] : uint8_t(0)});
                have_ = 0;                              // futó státusz: a következő adatbájtok ugyanehhez
            }
        }
    }

private:
    uint8_t status_ = 0, buf_[2] = {0, 0};
    int have_ = 0;
    bool sysex_ = false;
};

class Input {
public:
    using Callback = std::function<void(const Message &)>;
    using ErrorCallback = std::function<void(const std::string &)>;   // az eszköz eltűnt (kihúzták)

    Input();
    ~Input();
    Input(const Input &) = delete;
    Input &operator=(const Input &) = delete;

    static std::vector<Port> ports();
    // Megnyitás azonosító (vagy név) szerint; hibánál false és err.
    bool open(const std::string &id, Callback cb, ErrorCallback onError, std::string *err);
    void close();
    bool isOpen() const;
    std::string openId() const { return openId_; }

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    std::string openId_;
};

} // namespace midi
