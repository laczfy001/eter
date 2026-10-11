#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
// Windows: a Qt a főszálon egyszálas (STA) COM-ot használ. A miniaudio alapból többszálas módot kér, ami ott
// nem sikerül, mégis lezárja utána a COM-ot – ezzel a Qt-ét bontja le, és a WASAPI megnyitása összeomlik.
// Egyszálas móddal a nyitás és a zárás párban marad. (2 = COINIT_APARTMENTTHREADED)
#define MA_COINIT_VALUE 2
#define MINIAUDIO_IMPLEMENTATION
#include "i18n.h"
#undef slots   // a Qt kulcsszava ütközne az itteni `slots` taggal
#include "miniaudio.h"

#include "Engine.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

namespace eter {

struct Engine::Impl {
    ma_context ctx;
    bool ok = false;
};

struct Engine::OutDev {
    ma_device dev;
    bool open = false;
    std::atomic<Source *> slots[Engine::ChannelCount];
    std::atomic<int> toneLeft{0};
    double tonePhase = 0.0;

    OutDev()
    {
        for (auto &s : slots) s.store(nullptr);
    }
    bool empty() const
    {
        for (auto &s : slots)
            if (s.load()) return false;
        return true;
    }
};

static void dataCallback(ma_device *dev, void *output, const void *, ma_uint32 frames)
{
    auto *o = static_cast<Engine::OutDev *>(dev->pUserData);
    float *out = static_cast<float *>(output);   // a miniaudio előre kinullázza
    for (auto &slot : o->slots) {
        if (Source *s = slot.load(std::memory_order_acquire))
            s->mixInto(out, static_cast<int>(frames));
    }
    int tone = o->toneLeft.load(std::memory_order_relaxed);
    if (tone > 0) {
        const int n = std::min<int>(tone, static_cast<int>(frames));
        const double step = 2.0 * 3.14159265358979323846 * 440.0 / kRate;
        for (int i = 0; i < n; ++i) {
            const float v = 0.25f * static_cast<float>(std::sin(o->tonePhase));
            o->tonePhase += step;
            out[i * 2] += v;
            out[i * 2 + 1] += v;
        }
        o->toneLeft.store(tone - n, std::memory_order_relaxed);
    }
}

struct Engine::Recorder {
    ma_device dev;
    bool open = false;
    std::mutex mtx;
    std::vector<float> data;
    std::atomic<float> peak{0.f};
};

static void captureCallback(ma_device *dev, void *, const void *input, ma_uint32 frames)
{
    auto *r = static_cast<Engine::Recorder *>(dev->pUserData);
    const float *in = static_cast<const float *>(input);
    if (!in) return;
    float peak = 0.f;
    for (ma_uint32 i = 0; i < frames; ++i) peak = std::max(peak, std::fabs(in[i]));
    if (peak > r->peak.load(std::memory_order_relaxed)) r->peak.store(peak, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lk(r->mtx);
    if (r->data.size() < static_cast<size_t>(kRate) * 600)       // legfeljebb 10 perc
        r->data.insert(r->data.end(), in, in + frames);
}

Engine::Engine() : impl_(new Impl) {}

bool Engine::recording() const { return rec_ && rec_->open; }

bool Engine::startRecording(std::string *err)
{
    if (!impl_->ok) {
        if (err) *err = i18n::textStd("A hangrendszer nem fut");
        return false;
    }
    if (recording()) return true;
    rec_ = std::make_unique<Recorder>();
    rec_->data.reserve(static_cast<size_t>(kRate) * 60);
    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format = ma_format_f32;
    cfg.capture.channels = 1;
    cfg.sampleRate = kRate;
    cfg.periodSizeInMilliseconds = 10;
    cfg.dataCallback = captureCallback;
    cfg.pUserData = rec_.get();
    ma_result r = ma_device_init(&impl_->ctx, &cfg, &rec_->dev);
    if (r == MA_SUCCESS) {
        rec_->open = true;
        r = ma_device_start(&rec_->dev);
    }
    if (r != MA_SUCCESS) {
        if (rec_->open) ma_device_uninit(&rec_->dev);
        rec_.reset();
        if (err) *err = i18n::textStd("A felvétel nem indítható (van mikrofon?): ") + ma_result_description(r);
        return false;
    }
    return true;
}

std::vector<float> Engine::stopRecording()
{
    std::vector<float> out;
    if (!rec_) return out;
    if (rec_->open) ma_device_uninit(&rec_->dev);     // megvárja a futó visszahívást
    out = std::move(rec_->data);
    rec_.reset();
    return out;
}

float Engine::recordPeak() { return rec_ ? rec_->peak.exchange(0.f) : 0.f; }

Engine::~Engine() { shutdown(); }

bool Engine::ready() const { return impl_->ok; }

bool Engine::init(const std::string &backend, std::string *err)
{
    shutdown();
    ma_context_config cfg = ma_context_config_init();
    cfg.pulse.pApplicationName = "Éter";
    cfg.jack.pClientName = "Eter";

    ma_backend one;
    const ma_backend *list = nullptr;
    ma_uint32 count = 0;
    // Automatikus sorrend: PulseAudio (PipeWire alatt is ez szól), ALSA, JACK.
    // Windowson: WASAPI, ennek híján DirectSound. Amelyik rendszer az adott gépen nincs, azt a miniaudio átugorja.
    static const ma_backend autoList[] = {ma_backend_wasapi, ma_backend_dsound, ma_backend_pulseaudio, ma_backend_alsa,
                                          ma_backend_jack, ma_backend_null};
    if (backend == "pulseaudio") one = ma_backend_pulseaudio;
    else if (backend == "wasapi") one = ma_backend_wasapi;
    else if (backend == "dsound") one = ma_backend_dsound;
    else if (backend == "alsa") one = ma_backend_alsa;
    else if (backend == "jack") one = ma_backend_jack;
    else if (backend == "null") one = ma_backend_null;
    else {
        list = autoList;
        count = sizeof(autoList) / sizeof(autoList[0]);
    }
    if (!list) {
        list = &one;
        count = 1;
    }
    ma_result r = ma_context_init(list, count, &cfg, &impl_->ctx);
    if (r != MA_SUCCESS) {
        if (err) *err = i18n::textStd("A hangrendszer nem indítható: ") + ma_result_description(r);
        return false;
    }
    impl_->ok = true;
    return true;
}

void Engine::shutdown()
{
    stopRecording();
    for (auto &kv : outs_) {
        if (kv.second->open) ma_device_uninit(&kv.second->dev);
    }
    outs_.clear();
    routed_.fill(false);
    if (impl_->ok) {
        ma_context_uninit(&impl_->ctx);
        impl_->ok = false;
    }
}

std::string Engine::backendName() const
{
    if (!impl_->ok) return "nincs";
    switch (impl_->ctx.backend) {
    case ma_backend_pulseaudio: return "PulseAudio / PipeWire";
    case ma_backend_alsa: return "ALSA";
    case ma_backend_jack: return "JACK";
    case ma_backend_wasapi: return "WASAPI";
    case ma_backend_dsound: return "DirectSound";
    case ma_backend_null: return i18n::textStd("nincs hangeszköz (néma)");
    default: return ma_get_backend_name(impl_->ctx.backend);
    }
}

std::vector<DeviceInfo> Engine::devices()
{
    std::vector<DeviceInfo> out;
    if (!impl_->ok) return out;
    ma_device_info *infos = nullptr;
    ma_uint32 n = 0;
    if (ma_context_get_devices(&impl_->ctx, &infos, &n, nullptr, nullptr) != MA_SUCCESS) return out;
    for (ma_uint32 i = 0; i < n; ++i)
        out.push_back({infos[i].name, infos[i].isDefault != 0});
    return out;
}

void Engine::releaseDevice(const std::string &key, Channel ch)
{
    auto it = outs_.find(key);
    if (it == outs_.end()) return;
    it->second->slots[ch].store(nullptr, std::memory_order_release);
    if (it->second->empty()) {
        if (it->second->open) ma_device_uninit(&it->second->dev);   // megvárja a futó visszahívást
        outs_.erase(it);
    }
}

bool Engine::setRoute(Channel ch, const std::string &deviceName, std::string *err)
{
    if (ch == Pfl) {
        const bool ok = setRouteOne(Pfl, deviceName, err);
        if (ok) {
            setRouteOne(Pfl2, deviceName, nullptr);
            setRouteOne(Pfl3, deviceName, nullptr);
            setRouteOne(Pfl4, deviceName, nullptr);
            setRouteOne(Pfl5, deviceName, nullptr);
        }
        return ok;
    }
    if (ch == Player1) {
        const bool ok = setRouteOne(Player1, deviceName, err);
        if (ok) setRouteOne(Vt, deviceName, nullptr);
        return ok;
    }
    if (ch == Cart1) {
        const bool ok = setRouteOne(Cart1, deviceName, err);
        if (ok)
            for (int c = Cart1 + 1; c <= CartLast; ++c) setRouteOne(static_cast<Channel>(c), deviceName, nullptr);
        return ok;
    }
    return setRouteOne(ch, deviceName, err);
}

bool Engine::setRouteOne(Channel ch, const std::string &deviceName, std::string *err)
{
    if (!impl_->ok) {
        if (err) *err = i18n::textStd("A hangrendszer nem fut");
        return false;
    }
    if (routed_[ch] && routes_[ch] == deviceName && outs_.count(deviceName)) return true;

    if (routed_[ch]) releaseDevice(routes_[ch], ch);
    routed_[ch] = false;
    routes_[ch] = deviceName;

    auto it = outs_.find(deviceName);
    if (it == outs_.end()) {
        ma_device_id id;
        const ma_device_id *idPtr = nullptr;
        if (!deviceName.empty()) {
            ma_device_info *infos = nullptr;
            ma_uint32 n = 0;
            ma_context_get_devices(&impl_->ctx, &infos, &n, nullptr, nullptr);
            for (ma_uint32 i = 0; i < n; ++i) {
                if (deviceName == infos[i].name) {
                    id = infos[i].id;
                    idPtr = &id;
                    break;
                }
            }
            if (!idPtr) {
                if (err) *err = i18n::textStd("A kimenet nem található: ") + deviceName;
                return false;
            }
        }
        auto dev = std::make_unique<OutDev>();
        ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.pDeviceID = idPtr;
        cfg.playback.format = ma_format_f32;
        cfg.playback.channels = kChannels;
        cfg.sampleRate = kRate;
        cfg.periodSizeInMilliseconds = 10;
        cfg.dataCallback = dataCallback;
        cfg.pUserData = dev.get();
        ma_result r = ma_device_init(&impl_->ctx, &cfg, &dev->dev);
        if (r == MA_SUCCESS) {
            dev->open = true;
            r = ma_device_start(&dev->dev);
        }
        if (r != MA_SUCCESS) {
            if (dev->open) ma_device_uninit(&dev->dev);
            if (err) *err = i18n::textStd("A kimenet nem nyitható meg: ") + ma_result_description(r);
            return false;
        }
        it = outs_.emplace(deviceName, std::move(dev)).first;
    }
    it->second->slots[ch].store(&sources_[ch], std::memory_order_release);
    routed_[ch] = true;
    return true;
}

void Engine::testTone(Channel ch, int ms)
{
    if (!routed_[ch]) return;
    auto it = outs_.find(routes_[ch]);
    if (it != outs_.end()) it->second->toneLeft.store(ms * kRate / 1000);
}

} // namespace eter
