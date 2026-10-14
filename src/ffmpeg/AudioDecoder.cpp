#include "ffmpeg/AudioDecoder.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

#include <cmath>
#include <cstring>

namespace lr {
namespace {

/// swr 单次输出的样本帧数上限（8192 帧 @48k ≈ 0.17 秒，够装下一个音频包）
constexpr int kMaxOutFrames = 8192;

int firstAudioStream(AVFormatContext *fmt) {
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

AudioDecoder::~AudioDecoder() { release(); }

void AudioDecoder::release() {
    if (buffer_ != nullptr && api_ != nullptr) {
        int linesize = 0;
        api_->av_freep(&buffer_);
        (void)linesize;
        buffer_ = nullptr;
    } else if (buffer_ != nullptr) {
        buffer_ = nullptr;
    }
    if (swr_ != nullptr && api_ != nullptr) api_->swr_free(&swr_);
    if (frame_ != nullptr && api_ != nullptr) api_->av_frame_free(&frame_);
    if (packet_ != nullptr && api_ != nullptr) api_->av_packet_free(&packet_);
    if (codec_ != nullptr && api_ != nullptr) api_->avcodec_free_context(&codec_);
    if (fmt_ != nullptr && api_ != nullptr) api_->avformat_close_input(&fmt_);
    api_ = nullptr;
    audioIdx_ = -1;
    bufferSamples_ = 0;
}

void AudioDecoder::close() { release(); }

bool AudioDecoder::open(FfmpegApi &api, const std::string &path, std::string &error) {
    release();
    api_ = &api;

    AVFormatContext *fmt = nullptr;
    if (api.avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) < 0) {
        error = "打开音频文件失败";
        release();
        return false;
    }
    fmt_ = fmt;
    api.avformat_find_stream_info(fmt_, nullptr);

    audioIdx_ = firstAudioStream(fmt_);
    if (audioIdx_ < 0) {
        // 没有音轨：不算失败，上层按"只能播画面"处理
        return true;
    }

    AVStream *stream = fmt_->streams[audioIdx_];
    timeBase_ = av_q2d(stream->time_base);
    if (timeBase_ <= 0.0) timeBase_ = 1.0 / 48000.0;

    const AVCodec *codec = api.avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        error = "找不到对应的音频解码器";
        audioIdx_ = -1;
        return true;  // 解不了就当没音轨，画面照播
    }

    codec_ = api.avcodec_alloc_context3(codec);
    if (codec_ == nullptr) {
        error = "分配音频解码上下文失败";
        audioIdx_ = -1;
        return true;
    }
    if (api.avcodec_parameters_to_context(codec_, stream->codecpar) < 0) {
        error = "音频解码参数拷贝失败";
        api.avcodec_free_context(&codec_);
        codec_ = nullptr;
        audioIdx_ = -1;
        return true;
    }
    if (api.avcodec_open2(codec_, codec, nullptr) < 0) {
        error = "打开音频解码器失败";
        api.avcodec_free_context(&codec_);
        codec_ = nullptr;
        audioIdx_ = -1;
        return true;
    }

    // 重采样到 48kHz / 立体声 / s16 —— 声卡通用格式，上层不用再转换
    AVChannelLayout stereo;
    api.av_channel_layout_default(&stereo, kOutChannels);
    swr_ = api.swr_alloc();
    if (api.swr_alloc_set_opts2(&swr_, &stereo, AV_SAMPLE_FMT_S16, kOutRate,
                                &codec_->ch_layout, codec_->sample_fmt,
                                codec_->sample_rate, 0, nullptr) < 0 ||
        swr_ == nullptr) {
        error = "创建音频重采样上下文失败";
        api.swr_free(&swr_);
        swr_ = nullptr;
        audioIdx_ = -1;
        return true;
    }
    if (api.swr_init(swr_) < 0) {
        error = "初始化音频重采样失败";
        api.swr_free(&swr_);
        swr_ = nullptr;
        audioIdx_ = -1;
        return true;
    }

    int linesize = 0;
    if (api.av_samples_alloc(&buffer_, &linesize, kOutChannels, kMaxOutFrames,
                             AV_SAMPLE_FMT_S16, 0) < 0) {
        error = "分配音频输出缓冲失败";
        api.swr_free(&swr_);
        swr_ = nullptr;
        audioIdx_ = -1;
        return true;
    }
    bufferSamples_ = kMaxOutFrames;

    frame_ = api.av_frame_alloc();
    packet_ = api.av_packet_alloc();
    if (frame_ == nullptr || packet_ == nullptr) {
        error = "分配音频帧/包失败";
        audioIdx_ = -1;
        return true;
    }
    return true;
}

bool AudioDecoder::seekTo(double tSec, std::string &error) {
    if (!hasAudio()) return false;
    if (tSec < 0.0) tSec = 0.0;

    // 音频 seek 只能落到关键位置，往前多退 0.5 秒，避免丢掉 tSec 开头那一段
    // （AudioPeaks 里踩过同样的坑：直接定位会缺一块）。
    const double back = std::max(0.0, tSec - 0.5);
    const int64_t ts = static_cast<int64_t>(std::llround(back / timeBase_));
    api_->avformat_seek_file(fmt_, audioIdx_, INT64_MIN, ts, ts, 0);
    // 必须是 avcodec_flush_buffers；发空包会把解码器推进 draining 状态，
    // 之后 send_packet 一律返回 AVERROR_EOF（两个模块踩过两次了）。
    api_->avcodec_flush_buffers(codec_);
    (void)error;
    return true;
}

bool AudioDecoder::decodeNext(std::vector<int16_t> &pcm, double &startSec,
                              std::string &error) {
    pcm.clear();
    if (!hasAudio()) return false;

    while (api_->av_read_frame(fmt_, packet_) >= 0) {
        if (packet_->stream_index != audioIdx_) {
            api_->av_packet_unref(packet_);
            continue;
        }
        const int64_t pts = (packet_->pts != AV_NOPTS_VALUE) ? packet_->pts : packet_->dts;
        const double pktSec = (pts != AV_NOPTS_VALUE) ? double(pts) * timeBase_ : -1.0;

        const int sendRet = api_->avcodec_send_packet(codec_, packet_);
        api_->av_packet_unref(packet_);
        if (sendRet < 0) continue;

        bool got = false;
        auto *out = reinterpret_cast<int16_t *>(buffer_);
        int used = 0;  // 已写入的样本帧数

        while (true) {
            const int recvRet = api_->avcodec_receive_frame(codec_, frame_);
            if (recvRet < 0) break;  // EAGAIN（要更多输入）或 EOF

            int space = bufferSamples_ - used;
            if (space > 0) {
                uint8_t *dst = reinterpret_cast<uint8_t *>(out + used * kOutChannels);
                int produced = api_->swr_convert(
                    swr_, &dst, space, const_cast<const uint8_t **>(frame_->data),
                    frame_->nb_samples);
                if (produced > 0) {
                    used += produced;
                    got = true;
                }
            }
            api_->av_frame_unref(frame_);
        }

        if (got) {
            // 把 swr 里还没吐干净的残留再排一次，避免样本积压导致音画越走越偏
            int space = bufferSamples_ - used;
            if (space > 0) {
                uint8_t *dst = reinterpret_cast<uint8_t *>(out + used * kOutChannels);
                int produced = api_->swr_convert(swr_, &dst, space, nullptr, 0);
                if (produced > 0) used += produced;
            }
            pcm.assign(out, out + static_cast<std::size_t>(used) * kOutChannels);
            startSec = pktSec;
            return true;
        }
        // 这个包没解出采样（常见：刚 seek 完的第一个包），继续读下一个
    }
    (void)error;
    return false;  // 读完了
}

}  // namespace lr
