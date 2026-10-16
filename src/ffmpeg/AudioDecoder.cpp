// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "ffmpeg/AudioDecoder.h"

#include "ffmpeg/IoTimeout.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace lr {
namespace {

/// swr 单次输出的样本帧数上限（8192 帧 @48k ≈ 0.17 秒，够装下一个音频包）
constexpr int kMaxOutFrames = 8192;

/// 连续坏包到这个数就判定音轨整体解不动（交给上层放弃音频、只播画面）
constexpr int kConsecutiveBadLimit = 300;

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
        api_->av_freep(&buffer_);
        buffer_ = nullptr;
    } else if (buffer_ != nullptr) {
        buffer_ = nullptr;
    }
    // 其余 libav* 对象随 RAII 成员自动释放（P2-11）
    swr_.reset();
    frame_.reset();
    packet_.reset();
    codec_.reset();
    fmt_.reset();
    api_ = nullptr;
    audioIdx_ = -1;
    bufferSamples_ = 0;
}

void AudioDecoder::close() { release(); }

bool AudioDecoder::open(FfmpegApi &api, const std::string &path, std::string &error) {
    release();
    api_ = &api;

    // 统一入口：带 I/O 超时（P0-4）
    std::string openError;
    AVFormatContext *raw =
        lr::io::openInputWithTimeout(api, path, lr::io::kOpenTimeoutSec, openError);
    if (raw == nullptr) {
        error = openError.empty() ? "打开音频文件失败" : openError;
        release();
        return false;
    }
    fmt_ = lr::wrapInput(api, raw);
    api.avformat_find_stream_info(fmt_.get(), nullptr);

    audioIdx_ = firstAudioStream(fmt_.get());
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

    codec_.reset(api.avcodec_alloc_context3(codec));
    if (!codec_) {
        error = "分配音频解码上下文失败";
        audioIdx_ = -1;
        return true;
    }
    if (api.avcodec_parameters_to_context(codec_.get(), stream->codecpar) < 0) {
        error = "音频解码参数拷贝失败";
        codec_.reset();
        audioIdx_ = -1;
        return true;
    }
    if (api.avcodec_open2(codec_.get(), codec, nullptr) < 0) {
        error = "打开音频解码器失败";
        codec_.reset();
        audioIdx_ = -1;
        return true;
    }

    // 重采样到 48kHz / 立体声 / s16 —— 声卡通用格式，上层不用再转换
    AVChannelLayout stereo;
    api.av_channel_layout_default(&stereo, kOutChannels);
    // swr_alloc_set_opts2 是通过出参写新指针的（不像 reset 那样接管），
    // 所以先拿裸指针再交给 RAII
    SwrContext *rawSwr = nullptr;
    const int swrRet = api.swr_alloc_set_opts2(&rawSwr, &stereo, AV_SAMPLE_FMT_S16,
                                               kOutRate, &codec_->ch_layout,
                                               codec_->sample_fmt, codec_->sample_rate,
                                               0, nullptr);
    swr_.reset(rawSwr);
    if (swrRet < 0 || !swr_) {
        error = "创建音频重采样上下文失败";
        swr_.reset();
        audioIdx_ = -1;
        return true;
    }
    if (api.swr_init(swr_.get()) < 0) {
        error = "初始化音频重采样失败";
        swr_.reset();
        audioIdx_ = -1;
        return true;
    }

    int linesize = 0;
    if (api.av_samples_alloc(&buffer_, &linesize, kOutChannels, kMaxOutFrames,
                             AV_SAMPLE_FMT_S16, 0) < 0) {
        error = "分配音频输出缓冲失败";
        swr_.reset();
        audioIdx_ = -1;
        return true;
    }
    bufferSamples_ = kMaxOutFrames;

    frame_.reset(api.av_frame_alloc());
    packet_.reset(api.av_packet_alloc());
    if (!frame_ || !packet_) {
        error = "分配音频帧/包失败";
        audioIdx_ = -1;
        return true;
    }

    badBlocks_ = 0;
    badRun_ = 0;
    return true;
}

bool AudioDecoder::seekTo(double tSec, std::string &error) {
    if (!hasAudio()) return false;
    if (tSec < 0.0) tSec = 0.0;

    // 音频 seek 只能落到关键位置，往前多退 0.5 秒，避免丢掉 tSec 开头那一段
    // （AudioPeaks 里踩过同样的坑：直接定位会缺一块）。
    const double back = std::max(0.0, tSec - 0.5);
    const int64_t ts = static_cast<int64_t>(std::llround(back / timeBase_));
    api_->avformat_seek_file(fmt_.get(), audioIdx_, INT64_MIN, ts, ts, 0);
    // 必须是 avcodec_flush_buffers；发空包会把解码器推进 draining 状态，
    // 之后 send_packet 一律返回 AVERROR_EOF（两个模块踩过两次了）。
    api_->avcodec_flush_buffers(codec_.get());
    (void)error;
    return true;
}

AudioDecoder::DecodeResult AudioDecoder::decodeNext(std::vector<int16_t> &pcm,
                                                    double &startSec, std::string &error) {
    pcm.clear();
    if (!hasAudio()) return DecodeResult::EndOfStream;

    for (;;) {
        // 读包刹车（P0-4）：与 FrameGrabber 同款，见那里的说明
        lr::io::armTimeout(lr::io::kReadTimeoutMs);
        const int readRet = api_->av_read_frame(fmt_.get(), packet_.get());
        lr::io::disarmTimeout();
        if (readRet < 0) break;
        if (packet_->stream_index != audioIdx_) {
            api_->av_packet_unref(packet_.get());
            continue;
        }
        const int64_t pts = (packet_->pts != AV_NOPTS_VALUE) ? packet_->pts : packet_->dts;
        const double pktSec = (pts != AV_NOPTS_VALUE) ? double(pts) * timeBase_ : -1.0;

        const int sendRet = api_->avcodec_send_packet(codec_.get(), packet_.get());
        api_->av_packet_unref(packet_.get());
        if (sendRet < 0) {
            // 坏包：跳过。连续坏到一定程度才认这条音轨救不回来了
            ++badBlocks_;
            if (++badRun_ >= kConsecutiveBadLimit) {
                error = "音频数据损坏，无法继续解码";
                return DecodeResult::Error;
            }
            continue;
        }
        badRun_ = 0;

        bool got = false;
        auto *out = reinterpret_cast<int16_t *>(buffer_);
        int used = 0;  // 已写入的样本帧数

        while (true) {
            const int recvRet = api_->avcodec_receive_frame(codec_.get(), frame_.get());
            if (recvRet < 0) break;  // EAGAIN（要更多输入）或 EOF

            int space = bufferSamples_ - used;
            if (space > 0) {
                uint8_t *dst = reinterpret_cast<uint8_t *>(out + used * kOutChannels);
                int produced = api_->swr_convert(
                    swr_.get(), &dst, space, const_cast<const uint8_t **>(frame_->data),
                    frame_->nb_samples);
                if (produced > 0) {
                    // 必须钳到剩余空间：swr 一旦给出多于请求的样本数，
                    // used 就会越过缓冲区末尾，下一轮的写地址直接飞出去了
                    used += std::min(produced, space);
                    got = true;
                }
            }
            api_->av_frame_unref(frame_.get());
        }

        if (got) {
            // 把 swr 里还没吐干净的残留再排一次，避免样本积压导致音画越走越偏
            int space = bufferSamples_ - used;
            if (space > 0) {
                uint8_t *dst = reinterpret_cast<uint8_t *>(out + used * kOutChannels);
                int produced = api_->swr_convert(swr_.get(), &dst, space, nullptr, 0);
                if (produced > 0) used += std::min(produced, space);
            }
            pcm.assign(out, out + static_cast<std::size_t>(used) * kOutChannels);
            startSec = pktSec;
            return DecodeResult::Ok;
        }
        // 这个包没解出采样（常见：刚 seek 完的第一个包），继续读下一个
    }
    (void)error;
    return DecodeResult::EndOfStream;  // 文件正常读完
}

}  // namespace lr
