// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// 能力清单：运行时枚举这份 FFmpeg 构建里**真正可用**的组件。
//
// 用途：
//   1. M0 验收：把结果打到日志，一眼看出这份构建能干什么；
//   2. UI 层：编码器/封装器下拉框按实际能力动态填充，而不是写死一张表；
//   3. 任务前置校验：缺 demuxer/muxer/codec 时给出中文提示，而不是甩错误码。
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

#include "FfmpegApi.h"

struct CodecSet {
    std::vector<std::string> video;     // 视频编解码器
    std::vector<std::string> audio;     // 音频编解码器
    std::vector<std::string> subtitle;  // 字幕编解码器
    std::vector<std::string> other;     // 数据 / 附件等

    int total() const {
        return static_cast<int>(video.size() + audio.size() + subtitle.size() + other.size());
    }
};

struct FfmpegCapability {
    CodecSet decoders;                  // 按媒体类型分类的解码器
    CodecSet encoders;                  // 按媒体类型分类的编码器
    std::vector<std::string> demuxers;  // 可用解封装器名
    std::vector<std::string> muxers;    // 可用封装器名
};

/// api 必须已经 load() 成功，否则返回空清单
FfmpegCapability collectCapability(FfmpegApi &api);

/// 单个硬件编码器的本机探测结果
struct HwEncoderProbe {
    std::string name;       ///< ffmpeg 编码器名，如 "h264_nvenc"
    bool available = false; ///< 本机是否真的能用
    std::string error;      ///< 不可用原因（av_strerror 人话，或"构建未包含"）
};

/// 探测给定编码器列表在本机是否真的可用。
///
/// 判定方式与 TranscodeTask 的打开路径**逐字段一致**：
/// find_encoder_by_name + 最小配置 avcodec_open2。这样"探测可用"与
/// "任务里能打开"是同一件事，不会出现下拉框里有、一点就开始回退的尴尬。
/// 常见不可用原因：这份 FFmpeg 构建没编译它 / 本机没有对应硬件
/// （无 NVIDIA 显卡、无 Intel 核显、非 macOS 等）。
std::vector<HwEncoderProbe> probeHardwareEncoders(
    FfmpegApi &api, const std::vector<std::string> &encoderNames);

/// 判断某个封装器是否可用（如 "mp4"、"matroska"）
bool hasMuxer(const FfmpegCapability &cap, const std::string &name);
bool hasDemuxer(const FfmpegCapability &cap, const std::string &name);
