// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "FfmpegCapability.h"

#include <algorithm>

// 只有这里需要访问 AVCodec / AVInputFormat 的字段，因此只在这个 .cpp 里
// 引入 FFmpeg 头文件（其余工程代码保持与 av*.h 无关）。
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavformat/avformat.h>

namespace {

std::vector<std::string> sorted(std::vector<std::string> v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

}  // namespace

FfmpegCapability collectCapability(FfmpegApi &api) {
    FfmpegCapability cap;
    if (!api.isLoaded()) return cap;

    void *opaque = nullptr;
    const AVCodec *codec = nullptr;
    while ((codec = api.av_codec_iterate(&opaque)) != nullptr) {
        // codec->type 是 AVCodec 结构体的公共 ABI 字段，本 .cpp 已 include
        // libavcodec/codec.h，可以直接读——不需要为它单独绑定一个函数。
        CodecSet *dst = nullptr;
        if (api.av_codec_is_decoder(codec)) dst = &cap.decoders;
        else if (api.av_codec_is_encoder(codec)) dst = &cap.encoders;
        if (!dst) continue;

        switch (codec->type) {
            case AVMEDIA_TYPE_VIDEO:    dst->video.push_back(codec->name); break;
            case AVMEDIA_TYPE_AUDIO:    dst->audio.push_back(codec->name); break;
            case AVMEDIA_TYPE_SUBTITLE: dst->subtitle.push_back(codec->name); break;
            default:                    dst->other.push_back(codec->name); break;
        }
    }

    opaque = nullptr;
    const AVInputFormat *demuxer = nullptr;
    while ((demuxer = api.av_demuxer_iterate(&opaque)) != nullptr)
        cap.demuxers.emplace_back(demuxer->name);

    opaque = nullptr;
    const AVOutputFormat *muxer = nullptr;
    while ((muxer = api.av_muxer_iterate(&opaque)) != nullptr)
        cap.muxers.emplace_back(muxer->name);

    auto sortSet = [](CodecSet &s) {
        s.video = sorted(std::move(s.video));
        s.audio = sorted(std::move(s.audio));
        s.subtitle = sorted(std::move(s.subtitle));
        s.other = sorted(std::move(s.other));
    };
    sortSet(cap.decoders);
    sortSet(cap.encoders);
    cap.demuxers = sorted(std::move(cap.demuxers));
    cap.muxers = sorted(std::move(cap.muxers));
    return cap;
}

bool hasMuxer(const FfmpegCapability &cap, const std::string &name) {
    return std::find(cap.muxers.begin(), cap.muxers.end(), name) != cap.muxers.end();
}

bool hasDemuxer(const FfmpegCapability &cap, const std::string &name) {
    return std::find(cap.demuxers.begin(), cap.demuxers.end(), name) != cap.demuxers.end();
}

std::vector<HwEncoderProbe> probeHardwareEncoders(
    FfmpegApi &api, const std::vector<std::string> &encoderNames) {
    std::vector<HwEncoderProbe> out;
    if (!api.isLoaded()) return out;

    for (const std::string &name : encoderNames) {
        HwEncoderProbe probe;
        probe.name = name;

        const AVCodec *enc = api.avcodec_find_encoder_by_name(name.c_str());
        if (!enc) {
            probe.error = "这份 FFmpeg 构建没有编译该编码器";
            out.push_back(std::move(probe));
            continue;
        }

        // 最小配置：只为验证"这台机器能不能打开它"，不追求画质/速度。
        // 字段与 TranscodeTask::tryOpen 保持一致（见该文件视频编码器一节），
        // 尤其是 pix_fmt / time_base / framerate —— 硬件编码器对这三项挑剔。
        AVCodecContext *ctx = api.avcodec_alloc_context3(enc);
        if (!ctx) {
            probe.error = "内存不足";
            out.push_back(std::move(probe));
            continue;
        }
        ctx->width = 320;
        ctx->height = 240;
        ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        ctx->bit_rate = 1000000;
        ctx->time_base = AVRational{1, 25};
        ctx->framerate = AVRational{25, 1};
        ctx->thread_count = 0;  // 与任务里一致：0 = 自动

        const int r = api.avcodec_open2(ctx, enc, nullptr);
        if (r < 0) {
            probe.error = api.errorText(r);
        } else {
            probe.available = true;
        }
        api.avcodec_free_context(&ctx);  // 立即释放：探测不驻留任何设备会话
        out.push_back(std::move(probe));
    }
    return out;
}
