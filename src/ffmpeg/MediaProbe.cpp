// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "ffmpeg/MediaProbe.h"

#include "ffmpeg/FfObjects.h"
#include "ffmpeg/IoTimeout.h"

// 只有本文件会访问 libav* 结构体字段，因此在这里 include 真正的头文件
// （其余工程代码只看到 FfmpegSymbols.h 里的前向声明，不会顺手直接调用 av_*）。
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>

namespace lr {
namespace {

/// codec_id → 编码器/解码器名（"h264" / "aac" …）
std::string codecName(FfmpegApi &api, AVCodecID id) {
    const AVCodec *codec = api.avcodec_find_decoder(id);
    if (codec == nullptr || codec->name == nullptr) return {};
    return codec->name;
}

/// 打开并读流信息；失败时把错误写进 error 并返回空 FfInput。
/// 走统一入口：带 I/O 超时（P0-4）+ RAII 接管（P2-11）。
FfInput openInput(FfmpegApi &api, const std::string &path, std::string &error) {
    std::string openError;
    AVFormatContext *raw =
        lr::io::openInputWithTimeout(api, path, lr::io::kOpenTimeoutSec, openError);
    if (raw == nullptr) {
        error = openError;
        return {};
    }
    FfInput fmt = lr::wrapInput(api, raw);
    if (api.avformat_find_stream_info(fmt.get(), nullptr) < 0) {
        // 探测流信息失败不算致命（少数容器需要更多数据），继续用已有信息
    }
    return fmt;
}

/// 第一个指定类型的流下标，没有则 -1
int findStream(AVFormatContext *fmt, AVMediaType type) {
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == type) return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

MediaInfo MediaProbe::probe(FfmpegApi &api, const std::string &path) {
    MediaInfo info;
    info.path = path;

    std::string error;
    FfInput fmt = openInput(api, path, error);
    if (!fmt) {
        info.error = error;
        return info;
    }

    if (fmt->duration > 0) info.durationSec = double(fmt->duration) / double(AV_TIME_BASE);

    const int videoIdx = findStream(fmt.get(), AVMEDIA_TYPE_VIDEO);
    if (videoIdx >= 0) {
        AVStream *stream = fmt->streams[videoIdx];
        const AVCodecParameters *par = stream->codecpar;
        info.hasVideo = true;
        info.width = par->width;
        info.height = par->height;
        info.videoCodec = codecName(api, par->codec_id);
        if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
            info.fps = double(stream->avg_frame_rate.num) / double(stream->avg_frame_rate.den);
        }
        // 容器没给时长时退回视频流时长
        if (info.durationSec <= 0.0 && stream->duration > 0) {
            info.durationSec = double(stream->duration) * av_q2d(stream->time_base);
        }
    }

    const int audioIdx = findStream(fmt.get(), AVMEDIA_TYPE_AUDIO);
    if (audioIdx >= 0) {
        const AVCodecParameters *par = fmt->streams[audioIdx]->codecpar;
        info.hasAudio = true;
        info.audioCodec = codecName(api, par->codec_id);
        info.sampleRate = par->sample_rate;
        info.channels = par->ch_layout.nb_channels;
    }

    info.valid = info.hasVideo || info.hasAudio;
    return info;  // fmt 在此随 RAII 关闭
}

std::vector<double> MediaProbe::keyframeTimes(FfmpegApi &api, const std::string &path,
                                              std::string *error) {
    std::vector<double> times;

    std::string openError;
    FfInput fmt = openInput(api, path, openError);
    if (!fmt) {
        if (error) *error = openError;
        return times;
    }

    const int videoIdx = findStream(fmt.get(), AVMEDIA_TYPE_VIDEO);
    if (videoIdx < 0) {
        if (error) *error = "没有视频流";
        return times;
    }

    const double toSec = av_q2d(fmt->streams[videoIdx]->time_base);
    FfPacket packet = lr::wrapPacket(api, api.av_packet_alloc());
    double last = -1e100;  // 关键帧时间戳理论上递增，这里顺手去重/防倒退

    for (;;) {
        // 读包刹车（P0-4）：见 FrameGrabber::decodeNext 的说明
        lr::io::armTimeout(lr::io::kReadTimeoutMs);
        const int ret = api.av_read_frame(fmt.get(), packet.get());
        lr::io::disarmTimeout();
        if (ret < 0) break;

        if (packet->stream_index == videoIdx && (packet->flags & AV_PKT_FLAG_KEY) != 0) {
            const int64_t ts = (packet->pts != AV_NOPTS_VALUE) ? packet->pts : packet->dts;
            if (ts != AV_NOPTS_VALUE) {
                const double t = double(ts) * toSec;
                if (t > last + 1e-6) {
                    times.push_back(t);
                    last = t;
                }
            }
        }
        api.av_packet_unref(packet.get());
    }
    return times;
}

}  // namespace lr
