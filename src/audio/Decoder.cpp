#include "i18n.h"
#include "Decoder.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace eter {

static std::string avErr(int code)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(code, buf, sizeof buf);
    return buf;
}

static std::string tag(AVDictionary *a, AVDictionary *b, const char *key)
{
    for (AVDictionary *d : {a, b}) {
        if (AVDictionaryEntry *e = av_dict_get(d, key, nullptr, 0))
            if (e->value && *e->value)
                return e->value;
    }
    return {};
}

Decoder::Decoder()
{
    static const int quiet = (av_log_set_level(AV_LOG_FATAL), 0);   // az FFmpeg ne írjon a terminálra
    (void)quiet;
}
Decoder::~Decoder() { close(); }

void Decoder::close()
{
    if (swr_) swr_free(&swr_);
    if (ctx_) avcodec_free_context(&ctx_);
    if (fmt_) avformat_close_input(&fmt_);
    if (frame_) av_frame_free(&frame_);
    if (pkt_) av_packet_free(&pkt_);
    stream_ = -1;
    duration_ = 0;
    skipTo_ = -1;
    eof_ = drainSent_ = false;
    errors_ = 0;
    fifo_.clear();
    fifoPos_ = 0;
    title_.clear();
    artist_.clear();
}

bool Decoder::open(const std::string &path, std::string *err)
{
    close();
    auto fail = [&](const std::string &msg) {
        if (err) *err = msg;
        close();
        return false;
    };

    int r = avformat_open_input(&fmt_, path.c_str(), nullptr, nullptr);
    if (r < 0) return fail(i18n::textStd("Nem nyitható meg: ") + avErr(r));
    r = avformat_find_stream_info(fmt_, nullptr);
    if (r < 0) return fail(i18n::textStd("Ismeretlen formátum: ") + avErr(r));

    const AVCodec *codec = nullptr;
    stream_ = av_find_best_stream(fmt_, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (stream_ < 0 || !codec) return fail(i18n::textStd("Nincs benne dekódolható hangsáv"));

    AVStream *st = fmt_->streams[stream_];
    ctx_ = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx_, st->codecpar);
    ctx_->pkt_timebase = st->time_base;
    r = avcodec_open2(ctx_, codec, nullptr);
    if (r < 0) return fail(i18n::textStd("A dekóder nem indítható: ") + avErr(r));

    frame_ = av_frame_alloc();
    pkt_ = av_packet_alloc();
    startPts_ = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;

    if (st->duration != AV_NOPTS_VALUE && st->duration > 0)
        duration_ = av_rescale_q(st->duration, st->time_base, AVRational{1, kRate});
    else if (fmt_->duration != AV_NOPTS_VALUE && fmt_->duration > 0)
        duration_ = av_rescale(fmt_->duration, kRate, AV_TIME_BASE);

    title_ = tag(fmt_->metadata, st->metadata, "title");
    artist_ = tag(fmt_->metadata, st->metadata, "artist");
    return true;
}

void Decoder::convert()
{
    if (!swr_) {
        AVChannelLayout in;
        if (frame_->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || frame_->ch_layout.nb_channels == 0)
            av_channel_layout_default(&in, std::max(1, frame_->ch_layout.nb_channels));
        else
            av_channel_layout_copy(&in, &frame_->ch_layout);
        AVChannelLayout out = AV_CHANNEL_LAYOUT_STEREO;
        int r = swr_alloc_set_opts2(&swr_, &out, AV_SAMPLE_FMT_FLT, kRate, &in,
                                    static_cast<AVSampleFormat>(frame_->format),
                                    frame_->sample_rate, 0, nullptr);
        av_channel_layout_uninit(&in);
        if (r < 0 || swr_init(swr_) < 0) {
            if (swr_) swr_free(&swr_);
            ++errors_;
            return;
        }
    }

    const int cap = swr_get_out_samples(swr_, frame_->nb_samples);
    if (cap <= 0) return;
    const size_t old = fifo_.size();
    fifo_.resize(old + static_cast<size_t>(cap) * kChannels);
    uint8_t *outPtr = reinterpret_cast<uint8_t *>(fifo_.data() + old);
    int n = swr_convert(swr_, &outPtr, cap, const_cast<const uint8_t **>(frame_->extended_data),
                        frame_->nb_samples);
    if (n < 0) n = 0;
    fifo_.resize(old + static_cast<size_t>(n) * kChannels);

    // Pontos pozicionálás: a kulcskockától a kért pontig eldobjuk a mintákat.
    if (skipTo_ >= 0 && n > 0) {
        int64_t drop = 0;
        if (frame_->pts != AV_NOPTS_VALUE) {
            AVRational tb = fmt_->streams[stream_]->time_base;
            int64_t pos = av_rescale_q(frame_->pts - startPts_, tb, AVRational{1, kRate});
            drop = std::clamp<int64_t>(skipTo_ - pos, 0, n);
        }
        if (drop >= n) {
            fifo_.resize(old);
        } else {
            if (drop > 0)
                fifo_.erase(fifo_.begin() + old, fifo_.begin() + old + drop * kChannels);
            skipTo_ = -1;
        }
    }
}

bool Decoder::decodeMore()
{
    for (;;) {
        int r = avcodec_receive_frame(ctx_, frame_);
        if (r == 0) {
            convert();
            av_frame_unref(frame_);
            return true;
        }
        if (r == AVERROR_EOF) {
            eof_ = true;
            return false;
        }
        if (r != AVERROR(EAGAIN)) {
            if (++errors_ > 200) {
                eof_ = true;
                return false;
            }
            continue;
        }
        if (drainSent_) {
            eof_ = true;
            return false;
        }
        r = av_read_frame(fmt_, pkt_);
        if (r < 0) {
            avcodec_send_packet(ctx_, nullptr);
            drainSent_ = true;
            continue;
        }
        if (pkt_->stream_index == stream_) {
            if (avcodec_send_packet(ctx_, pkt_) < 0) ++errors_;
        }
        av_packet_unref(pkt_);
    }
}

int Decoder::read(float *out, int frames)
{
    if (!fmt_) return 0;
    const size_t need = static_cast<size_t>(frames) * kChannels;
    while (fifo_.size() - fifoPos_ < need && !eof_)
        decodeMore();
    const size_t have = std::min(need, fifo_.size() - fifoPos_);
    std::copy_n(fifo_.begin() + fifoPos_, have, out);
    fifoPos_ += have;
    if (fifoPos_ > 65536 || fifoPos_ == fifo_.size()) {
        fifo_.erase(fifo_.begin(), fifo_.begin() + fifoPos_);
        fifoPos_ = 0;
    }
    return static_cast<int>(have / kChannels);
}

bool Decoder::seek(int64_t frame)
{
    if (!fmt_) return false;
    frame = std::max<int64_t>(0, frame);
    AVRational tb = fmt_->streams[stream_]->time_base;
    int64_t ts = av_rescale_q(frame, AVRational{1, kRate}, tb) + startPts_;
    int r = av_seek_frame(fmt_, stream_, ts, AVSEEK_FLAG_BACKWARD);
    if (r < 0) r = av_seek_frame(fmt_, stream_, ts, AVSEEK_FLAG_ANY);
    avcodec_flush_buffers(ctx_);
    if (swr_) swr_free(&swr_);
    fifo_.clear();
    fifoPos_ = 0;
    eof_ = drainSent_ = false;
    errors_ = 0;
    skipTo_ = frame;
    return r >= 0;
}

bool scanPeaks(const std::string &path, int bins, std::vector<float> &peaks,
               int64_t &totalFrames, const std::atomic<bool> &cancel)
{
    Decoder dec;
    if (!dec.open(path)) return false;

    constexpr int kBlock = 1024;
    std::vector<float> buf(kBlock * kChannels);
    std::vector<float> blocks;
    blocks.reserve(16384);
    totalFrames = 0;
    for (;;) {
        if (cancel.load(std::memory_order_relaxed)) return false;
        int n = dec.read(buf.data(), kBlock);
        if (n <= 0) break;
        float peak = 0.f;
        for (int i = 0; i < n * kChannels; ++i)
            peak = std::max(peak, std::fabs(buf[i]));
        blocks.push_back(peak);
        totalFrames += n;
    }
    if (blocks.empty()) return false;

    peaks.assign(bins, 0.f);
    for (size_t i = 0; i < blocks.size(); ++i) {
        size_t b = std::min<size_t>(bins - 1, i * bins / blocks.size());
        peaks[b] = std::max(peaks[b], blocks[i]);
    }
    // Ha kevesebb blokk van, mint oszlop, a lyukakat az előző értékkel töltjük ki.
    if (blocks.size() < static_cast<size_t>(bins)) {
        for (int b = 0; b < bins; ++b)
            peaks[b] = blocks[std::min(blocks.size() - 1, static_cast<size_t>(b) * blocks.size() / bins)];
    }
    return true;
}

int64_t autoMixOut(const std::vector<float> &peaks, int64_t totalFrames, double thresholdDb)
{
    if (peaks.empty()) return totalFrames;
    const float threshold = static_cast<float>(std::pow(10.0, thresholdDb / 20.0));
    int last = static_cast<int>(peaks.size()) - 1;
    while (last > 0 && peaks[last] < threshold) --last;
    return std::min<int64_t>(totalFrames, static_cast<int64_t>(last + 1) * totalFrames / static_cast<int64_t>(peaks.size()));
}

} // namespace eter
