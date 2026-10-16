// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "ffmpeg/AudioPeaks.h"

#include "ffmpeg/FfObjects.h"
#include "ffmpeg/IoTimeout.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>

#include <algorithm>
#include <cmath>

namespace lr {
namespace {

constexpr int kOutSampleRate = kPeakSampleRate;  // 见头文件：8kHz
constexpr int kBufferSamples = 8192;   // 每次 swr_convert 的输出块

int firstAudioStream(AVFormatContext *fmt) {
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// 解码骨架：把 [fromSec, toSec) 解成 单声道/s16/8kHz，逐块交给 sink。
/// 建峰值的两种方式（整轨 build / 区间 buildRange / 分段全量 WaveformBuilder）
/// 都复用这一份，避免把下面这几个坑各写一遍。
///
/// libav* 对象全部 RAII 化（P2-11）：原来的版本每个错误分支都要手工配平
/// 四五次释放，新增资源就是新的泄漏机会。
bool decodeRangeImpl(FfmpegApi &api, const std::string &path, double fromSec, double toSec,
                     bool seekToStart, const SampleSink &sink, std::string &error) {
    std::string openError;
    FfInput fmt = lr::wrapInput(
        api, lr::io::openInputWithTimeout(api, path, lr::io::kOpenTimeoutSec, openError));
    if (!fmt) {
        error = openError.empty() ? "打开音频文件失败" : openError;
        return false;
    }

    if (api.avformat_find_stream_info(fmt.get(), nullptr) < 0) {
        // 不致命
    }

    const int audioIdx = firstAudioStream(fmt.get());
    if (audioIdx < 0) {
        error = "没有音轨";
        return false;
    }

    AVStream *stream = fmt->streams[audioIdx];
    const AVCodec *codec = api.avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        error = "找不到对应的音频解码器";
        return false;
    }

    FfCodec codecCtx = lr::wrapCodec(api, api.avcodec_alloc_context3(codec));
    if (!codecCtx) {
        error = "分配解码上下文失败";
        return false;
    }
    int ret = api.avcodec_parameters_to_context(codecCtx.get(), stream->codecpar);
    if (ret < 0) {
        error = api.errorText(ret);
        return false;
    }
    ret = api.avcodec_open2(codecCtx.get(), codec, nullptr);
    if (ret < 0) {
        error = api.errorText(ret);
        return false;
    }

    // 重采样到 单声道 / s16 / 8kHz：后续分桶只读一路 s16，最快
    AVChannelLayout mono;
    api.av_channel_layout_default(&mono, 1);
    SwrContext *rawSwr = api.swr_alloc();
    ret = api.swr_alloc_set_opts2(&rawSwr, &mono, AV_SAMPLE_FMT_S16, kOutSampleRate,
                                  &codecCtx->ch_layout, codecCtx->sample_fmt,
                                  codecCtx->sample_rate, 0, nullptr);
    FfSwr swr = lr::wrapSwr(api, rawSwr);
    if (ret < 0 || !swr) {
        error = "创建音频重采样上下文失败";
        return false;
    }
    if (api.swr_init(swr.get()) < 0) {
        error = "初始化音频重采样失败";
        return false;
    }

    const double timeBase = av_q2d(stream->time_base);
    const double streamSec = (timeBase > 0) ? timeBase : 1.0 / 1000.0;
    if (seekToStart) {
        // 往前多退 1 秒再 seek：音频 seek 只能落到关键位置，直接定位到 fromSec
        // 很容易丢掉开头那一段，波形左端就会缺一块
        const double back = std::max(0.0, fromSec - 1.0);
        const int64_t ts = static_cast<int64_t>(std::llround(back / streamSec));
        api.avformat_seek_file(fmt.get(), audioIdx, INT64_MIN, ts, ts, 0);
        // 注意：seek 后必须用 avcodec_flush_buffers 清掉解码器里积压的旧帧。
        // 用"发一个空包"代替会把解码器推进 draining 状态，之后 send_packet
        // 一律返回 AVERROR_EOF，表现就是"怎么都解不出采样"。
        api.avcodec_flush_buffers(codecCtx.get());
    }

    uint8_t *buffer = nullptr;
    int linesize = 0;
    if (api.av_samples_alloc(&buffer, &linesize, 1, kBufferSamples, AV_SAMPLE_FMT_S16, 0) < 0) {
        error = "分配采样缓冲失败";
        return false;
    }

    const double sampleStep = 1.0 / double(kOutSampleRate);

    FfPacket packet = lr::wrapPacket(api, api.av_packet_alloc());
    FfFrame frame = lr::wrapFrame(api, api.av_frame_alloc());
    bool stop = false;
    double writeSec = fromSec;   // 下一段待写采样的起始时间

    for (;;) {
        if (stop) break;
        // 读包刹车（P0-4）：见 FrameGrabber::decodeNext 的说明
        lr::io::armTimeout(lr::io::kReadTimeoutMs);
        const int readRet = api.av_read_frame(fmt.get(), packet.get());
        lr::io::disarmTimeout();
        if (readRet < 0) break;
        if (packet->stream_index != audioIdx) {
            api.av_packet_unref(packet.get());
            continue;
        }
        if (api.avcodec_send_packet(codecCtx.get(), packet.get()) < 0) {
            api.av_packet_unref(packet.get());
            continue;
        }
        api.av_packet_unref(packet.get());

        while (api.avcodec_receive_frame(codecCtx.get(), frame.get()) >= 0) {
            // 这一帧的起始时间（重采样后输出样本的时间基准）
            const int64_t pts = (frame->pts != AV_NOPTS_VALUE) ? frame->pts : frame->pkt_dts;
            const double frameSec = (pts != AV_NOPTS_VALUE) ? double(pts) * timeBase : fromSec;
            double posSec = frameSec;

            int produced = api.swr_convert(swr.get(), &buffer, kBufferSamples,
                                           const_cast<const uint8_t **>(frame->data),
                                           frame->nb_samples);
            while (produced > 0) {
                const int16_t *samples = reinterpret_cast<int16_t *>(buffer);
                stop = sink(samples, produced, posSec);
                posSec += double(produced) * sampleStep;
                if (stop) break;
                produced = api.swr_convert(swr.get(), &buffer, kBufferSamples, nullptr, 0);
            }
            writeSec = posSec;
            api.av_frame_unref(frame.get());
            if (stop) break;
        }
    }

    // 冲掉 swr 里剩余的样本（不这么做末尾几片会偏小）
    if (!stop) {
        int produced = api.swr_convert(swr.get(), &buffer, kBufferSamples, nullptr, 0);
        while (produced > 0) {
            const int16_t *samples = reinterpret_cast<int16_t *>(buffer);
            stop = sink(samples, produced, writeSec);
            writeSec += double(produced) * sampleStep;
            if (stop) break;
            produced = api.swr_convert(swr.get(), &buffer, kBufferSamples, nullptr, 0);
        }
    }

    api.av_freep(&buffer);
    return true;  // fmt / codecCtx / swr / packet / frame 随 RAII 释放
}

/// 把解出来的采样投进 out 自己的片里（build / buildRange 用）。
/// 整轨全量的分段写法在 WaveformBuilder 里 —— 那里只写自己那一段的下标区间。
bool buildWhole(FfmpegApi &api, const std::string &path, double fromSec, double toSec,
                bool seekToStart, AudioPeaks &out, std::string &error) {
    const double spanSec = std::max(1e-6, toSec - fromSec);
    const std::size_t bucketCount = out.size();
    const double sampleStep = 1.0 / double(kOutSampleRate);

    SampleSink sink = [&](const int16_t *samples, int count, double startSec) -> bool {
        double t = startSec;
        for (int i = 0; i < count; ++i, t += sampleStep) {
            if (t >= toSec) return true;
            if (t < fromSec) continue;   // seek 往回退多解的那部分，丢掉
            const auto idx = static_cast<std::size_t>((t - fromSec) / spanSec *
                                                      double(bucketCount));
            if (idx >= bucketCount) continue;
            const int16_t v = samples[i];
            if (v < out.min[idx]) out.min[idx] = v;
            if (v > out.max[idx]) out.max[idx] = v;
        }
        return false;
    };

    const bool ok = decodeRangeImpl(api, path, fromSec, toSec, seekToStart, sink, error);
    // 整段算完了：整段标记为已算（区分"算完是静音"和"没算"）
    out.ready.assign(out.ready.size(), 1);
    return ok;
}

}  // namespace

bool AudioPeakBuilder::build(FfmpegApi &api, const std::string &path, double pointsPerSec,
                             AudioPeaks &out, std::string &error) {
    // 先拿时长，决定片数（必须先 find_stream_info，否则很多容器 duration 还是 0）
    double duration = 0.0;
    {
        std::string openError;
        FfInput fmt = lr::wrapInput(
            api, lr::io::openInputWithTimeout(api, path, lr::io::kOpenTimeoutSec, openError));
        if (fmt) {
            api.avformat_find_stream_info(fmt.get(), nullptr);
            if (fmt->duration > 0) duration = double(fmt->duration) / double(AV_TIME_BASE);
        }
    }
    if (duration <= 0.0) {
        error = "读不到时长";
        return false;
    }
    out.allocate(0.0, duration, pointsPerSec);
    return buildWhole(api, path, 0.0, duration, false, out, error);
}

bool AudioPeakBuilder::buildRange(FfmpegApi &api, const std::string &path, double fromSec,
                                  double toSec, int slices, AudioPeaks &out,
                                  std::string &error) {
    if (toSec <= fromSec) return false;
    out.allocate(fromSec, toSec, double(std::max(1, slices)) / (toSec - fromSec));
    return buildWhole(api, path, fromSec, toSec, true, out, error);
}

bool AudioPeakBuilder::decodeRange(FfmpegApi &api, const std::string &path, double fromSec,
                                   double toSec, bool seekToStart, const SampleSink &sink,
                                   std::string &error) {
    return decodeRangeImpl(api, path, fromSec, toSec, seekToStart, sink, error);
}

}  // namespace lr
