#pragma once
// Külső hangstream (Icecast / Shoutcast, http vagy https) mint lejátszható elem. A hangmotor nem függ a hálózattól:
// a bájtokat egy StreamInput adja (a programban a hálózati réteg tölti), a Decoder ebből demuxol és dekódol.
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace eter {

// Igaz, ha az útvonal stream címe (http:// vagy https://).
bool isStreamUrl(const std::string &path);

class StreamInput {
public:
    virtual ~StreamInput() = default;
    // Blokkol, amíg van adat: >0 bájt, 0 = vége (megszakítva, vagy az újrakapcsolódás sem sikerült).
    virtual int read(uint8_t *buf, int size) = 0;
    virtual void abort() = 0;               // bármely szálról; a várakozó read() azonnal visszatér
    // Adásban van-e: adás előtt a read() a végtelenségig vár, adásban csak a feladási időig.
    virtual void setPlaying(bool) {}
    virtual std::string streamTitle() const { return {}; }   // az ICY „StreamTitle” (ha a stream küld ilyet)
    virtual std::string stationName() const { return {}; }   // icy-name
    virtual std::string state() const { return {}; }         // a kapcsolat állapota szövegesen
};

using StreamFactory = std::function<std::shared_ptr<StreamInput>(const std::string &url)>;
void setStreamFactory(StreamFactory f);
std::shared_ptr<StreamInput> openStreamInput(const std::string &url);   // nullptr, ha nincs gyár

} // namespace eter
