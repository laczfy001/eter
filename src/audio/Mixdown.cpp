#include "i18n.h"
#include "Mixdown.h"
#include "Source.h"

#include <QFile>
#include <QString>
#include <lame/lame.h>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cmath>
#include <cstring>

namespace eter {

int64_t MixdownJob::total() const
{
    int64_t t = 0;
    for (const MixdownClip &c : clips) t = std::max(t, c.start + c.length());
    return t;
}

struct MixdownRenderer::Open {
    Decoder dec;
    bool ok = false;
    int64_t next = -1;   // a dekóder következő olvasása ezt a fájlbeli képkockát adja
};

MixdownRenderer::MixdownRenderer(const MixdownJob &job) : job_(job)
{
    open_.resize(job_.clips.size());
}

MixdownRenderer::~MixdownRenderer() = default;

// A zene halkítása a voice trackek alatt: 0,3 mp-es rámpával be és ki.
float MixdownRenderer::duckAt(int64_t t) const
{
    static constexpr int64_t kRamp = kRate * 3 / 10;
    float g = 1.f;
    for (const MixdownClip &c : job_.clips) {
        if (!c.voice || c.duck >= 0.999f) continue;
        const int64_t a = c.start, b = c.start + c.length();
        if (t < a - kRamp || t > b + kRamp) continue;
        float w = 1.f;   // mennyire van „bent” (0..1)
        if (t < a) w = 1.f - float(a - t) / float(kRamp);
        else if (t > b) w = 1.f - float(t - b) / float(kRamp);
        g = std::min(g, 1.f + (c.duck - 1.f) * std::clamp(w, 0.f, 1.f));
    }
    return g;
}

static float envGain(const std::vector<std::pair<int64_t, float>> &env, int64_t f)
{
    if (env.empty()) return 1.f;
    if (f <= env.front().first) return env.front().second;
    if (f >= env.back().first) return env.back().second;
    auto it = std::upper_bound(env.begin(), env.end(), f, [](int64_t v, const std::pair<int64_t, float> &p) { return v < p.first; });
    const auto &b = *it;
    const auto &a = *(it - 1);
    const float p = float(f - a.first) / float(std::max<int64_t>(1, b.first - a.first));
    // Mint a lejátszásnál: a pontok között dB-ben egyenletesen (a −40 dB alatti, azaz néma pont felé lineárisan).
    if (a.second <= 0.f || b.second <= 0.f) return a.second + (b.second - a.second) * p;
    const float da = std::log10(a.second), db = std::log10(b.second);
    return std::pow(10.f, da + (db - da) * p);
}

void MixdownRenderer::render(int64_t from, int frames, float *out)
{
    std::fill(out, out + size_t(frames) * kChannels, 0.f);
    const int64_t to = from + frames;
    for (size_t k = 0; k < job_.clips.size(); ++k) {
        const MixdownClip &c = job_.clips[k];
        const int64_t cs = c.start, ce = c.start + c.length();
        const int64_t a = std::max(from, cs), b = std::min(to, ce);
        if (a >= b) {
            if (open_[k] && from >= ce) open_[k].reset();   // már elmúlt: a dekóder felszabadítható
            continue;
        }
        if (!open_[k]) {
            open_[k] = std::make_unique<Open>();
            open_[k]->ok = open_[k]->dec.open(c.path);
        }
        Open &o = *open_[k];
        if (!o.ok) continue;
        const int64_t fileAt = c.in + (a - cs);
        if (o.next != fileAt) {
            o.dec.seek(fileAt);
            o.next = fileAt;
        }
        const int n = int(b - a);
        tmp_.assign(size_t(n) * kChannels, 0.f);
        int got = 0;
        while (got < n) {
            const int r = o.dec.read(tmp_.data() + size_t(got) * kChannels, n - got);
            if (r <= 0) break;
            got += r;
        }
        o.next += n;
        float *dst = out + size_t(a - from) * kChannels;
        for (int i = 0; i < n; ++i) {
            const int64_t f = fileAt + i;   // fájlbeli képkocka
            float g = envGain(c.env, f);
            const int64_t fromIn = f - c.in, toEnd = c.end - f;
            if (c.fadeIn > 0 && fromIn < c.fadeIn) g *= fadeGain(FadeCurve::SCurve, 0.f, 1.f, float(fromIn) / float(c.fadeIn));
            if (c.fadeOut > 0 && toEnd <= c.fadeOut) g *= fadeGain(FadeCurve::SCurve, 1.f, 0.f, 1.f - float(toEnd) / float(c.fadeOut));
            if (!c.voice) g *= duckAt(a + i);
            dst[i * 2] += tmp_[size_t(i) * 2] * g;
            dst[i * 2 + 1] += tmp_[size_t(i) * 2 + 1] * g;
        }
    }
}

// ------------------------------------------------------------------ fájlba írás

namespace {

struct Writer {
    QFile file;
    bool mp3 = false;
    int bits = 16, rate = kRate;
    lame_t lame = nullptr;
    std::vector<unsigned char> buf;
    qint64 dataBytes = 0;

    bool open(const MixdownOutput &o, std::string *err)
    {
        mp3 = o.mp3;
        bits = o.bits == 24 ? 24 : 16;
        rate = o.rate;
        file.setFileName(QString::fromStdString(o.path));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (err) *err = i18n::textStd("A fájl nem írható: ") + o.path;
            return false;
        }
        if (mp3) {
            lame = lame_init();
            lame_set_in_samplerate(lame, rate);
            lame_set_out_samplerate(lame, rate);
            lame_set_num_channels(lame, 2);
            lame_set_mode(lame, JOINT_STEREO);
            lame_set_brate(lame, o.kbps);
            lame_set_quality(lame, 2);
            if (lame_init_params(lame) < 0) {
                if (err) *err = i18n::textStd("Az MP3 kódoló nem indítható.");
                return false;
            }
        } else {
            file.write(QByteArray(44, '\0'));   // a fejléc a végén kerül a helyére
        }
        return true;
    }

    bool write(const float *pcm, int frames)
    {
        if (frames <= 0) return true;
        if (mp3) {
            const size_t need = size_t(1.25 * frames + 7200);
            if (buf.size() < need) buf.resize(need);
            const int n = lame_encode_buffer_interleaved_ieee_float(lame, pcm, frames, buf.data(), int(buf.size()));
            return n <= 0 || file.write(reinterpret_cast<const char *>(buf.data()), n) == n;
        }
        const int bytes = bits / 8;
        buf.resize(size_t(frames) * 2 * bytes);
        unsigned char *p = buf.data();
        for (int i = 0; i < frames * 2; ++i) {
            const float v = std::clamp(pcm[i], -1.f, 1.f);
            if (bits == 16) {
                const int16_t s = int16_t(std::lround(v * 32767.f));
                std::memcpy(p, &s, 2);
                p += 2;
            } else {
                const int32_t s = int32_t(std::lround(double(v) * 8388607.0));
                p[0] = uint8_t(s & 0xff);
                p[1] = uint8_t((s >> 8) & 0xff);
                p[2] = uint8_t((s >> 16) & 0xff);
                p += 3;
            }
        }
        dataBytes += qint64(buf.size());
        return file.write(reinterpret_cast<const char *>(buf.data()), qint64(buf.size())) == qint64(buf.size());
    }

    bool finish()
    {
        bool ok = true;
        if (mp3) {
            buf.resize(7200);
            const int n = lame_encode_flush(lame, buf.data(), int(buf.size()));
            if (n > 0) ok = file.write(reinterpret_cast<const char *>(buf.data()), n) == n;
            lame_close(lame);
            lame = nullptr;
        } else {
            QByteArray h;
            auto u32 = [&h](quint32 v) { h.append(reinterpret_cast<const char *>(&v), 4); };
            auto u16 = [&h](quint16 v) { h.append(reinterpret_cast<const char *>(&v), 2); };
            const int bytes = bits / 8;
            h.append("RIFF");
            u32(quint32(36 + dataBytes));
            h.append("WAVEfmt ");
            u32(16);
            u16(1);
            u16(2);
            u32(quint32(rate));
            u32(quint32(rate * 2 * bytes));
            u16(quint16(2 * bytes));
            u16(quint16(bits));
            h.append("data");
            u32(quint32(dataBytes));
            ok = file.seek(0) && file.write(h) == h.size();
        }
        file.close();
        return ok;
    }

    ~Writer()
    {
        if (lame) lame_close(lame);
    }
};

} // namespace

bool renderMixdown(const MixdownJob &job, const MixdownOutput &o, const std::function<void(double)> &progress,
                   const std::atomic<bool> &cancel, std::string *err)
{
    const int64_t total = job.total();
    if (total <= 0) {
        if (err) *err = i18n::textStd("Nincs mit kikeverni.");
        return false;
    }
    constexpr int kBlock = kRate / 2;
    std::vector<float> block(size_t(kBlock) * kChannels);
    const double passes = o.limitPeak ? 2.0 : 1.0;

    // 1. menet (ha kell): a csúcsszint, hogy a végeredmény ne lépje túl a −1 dBFS-t.
    float gain = 1.f;
    if (o.limitPeak) {
        MixdownRenderer r(job);
        float peak = 0.f;
        for (int64_t t = 0; t < total; t += kBlock) {
            if (cancel.load()) {
                if (err) *err = i18n::textStd("Megszakítva.");
                return false;
            }
            const int n = int(std::min<int64_t>(kBlock, total - t));
            r.render(t, n, block.data());
            for (int i = 0; i < n * kChannels; ++i) peak = std::max(peak, std::fabs(block[size_t(i)]));
            if (progress) progress(double(t + n) / double(total) / passes);
        }
        const float limit = std::pow(10.f, -1.f / 20.f);
        if (peak > limit) gain = limit / peak;
    }

    // 2. menet: kikeverés, szükség esetén 44,1 kHz-re alakítva, és írás.
    Writer w;
    if (!w.open(o, err)) return false;
    SwrContext *swr = nullptr;
    if (o.rate != kRate) {
        AVChannelLayout st = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&swr, &st, AV_SAMPLE_FMT_FLT, o.rate, &st, AV_SAMPLE_FMT_FLT, kRate, 0, nullptr) < 0 || swr_init(swr) < 0) {
            if (swr) swr_free(&swr);
            if (err) *err = i18n::textStd("A mintavétel átalakítása nem indítható.");
            return false;
        }
    }
    std::vector<float> conv;
    MixdownRenderer r(job);
    bool ok = true;
    for (int64_t t = 0; t < total && ok; t += kBlock) {
        if (cancel.load()) {
            ok = false;
            if (err) *err = i18n::textStd("Megszakítva.");
            break;
        }
        const int n = int(std::min<int64_t>(kBlock, total - t));
        r.render(t, n, block.data());
        if (gain != 1.f)
            for (int i = 0; i < n * kChannels; ++i) block[size_t(i)] *= gain;
        if (swr) {
            const int cap = swr_get_out_samples(swr, n);
            conv.resize(size_t(cap) * kChannels);
            uint8_t *outp = reinterpret_cast<uint8_t *>(conv.data());
            const uint8_t *inp = reinterpret_cast<const uint8_t *>(block.data());
            const int m = swr_convert(swr, &outp, cap, &inp, n);
            ok = m < 0 || w.write(conv.data(), m);
        } else {
            ok = w.write(block.data(), n);
        }
        if (!ok && err && err->empty()) *err = i18n::textStd("Írási hiba (megtelt a lemez?)");
        if (progress) progress(((passes - 1.0) + double(t + n) / double(total)) / passes);
    }
    if (swr && ok) {
        const int cap = swr_get_out_samples(swr, 0);
        if (cap > 0) {
            conv.resize(size_t(cap) * kChannels);
            uint8_t *outp = reinterpret_cast<uint8_t *>(conv.data());
            const int m = swr_convert(swr, &outp, cap, nullptr, 0);
            if (m > 0) ok = w.write(conv.data(), m);
        }
    }
    if (swr) swr_free(&swr);
    const bool fin = w.finish();
    if (!ok || !fin) {
        QFile::remove(QString::fromStdString(o.path));
        if (err && err->empty()) *err = i18n::textStd("Írási hiba (megtelt a lemez?)");
        return false;
    }
    return true;
}

bool renderPreviewWav(const MixdownJob &job, int64_t from, int64_t frames, const std::string &path)
{
    MixdownOutput o;
    o.path = path;
    o.mp3 = false;
    o.bits = 16;
    o.rate = kRate;
    Writer w;
    if (!w.open(o, nullptr)) return false;
    MixdownRenderer r(job);
    constexpr int kBlock = kRate / 2;
    std::vector<float> block(size_t(kBlock) * kChannels);
    for (int64_t t = 0; t < frames; t += kBlock) {
        const int n = int(std::min<int64_t>(kBlock, frames - t));
        r.render(from + t, n, block.data());
        if (!w.write(block.data(), n)) return false;
    }
    return w.finish();
}

} // namespace eter
