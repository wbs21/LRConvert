// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// 媒体探测 —— Tab1 时间轴需要的两样东西：
//   1. 文件编码信息（时长 / 分辨率 / 帧率 / 编码 / 采样率…）
//   2. 视频关键帧时间戳表（起点终点吸附要用）
//
// 两者都只走 avformat 的解封装层：probe 只读头部，关键帧表只读包头不解包内容，
// 因此即使是很长的视频也能在几十毫秒内跑完，可以放心放在后台线程。
//
// 不依赖 Qt（与 capability_check 一样，只链接 ffmpeg_runtime）。
// ---------------------------------------------------------------------------

#include <string>
#include <vector>

#include "ffmpeg/FfmpegApi.h"

namespace lr {

struct MediaInfo {
    bool valid = false;
    std::string path;

    double durationSec = 0.0;  // 容器时长（秒，精确到毫秒级）

    bool hasVideo = false;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    std::string videoCodec;  // 如 "h264"

    bool hasAudio = false;
    std::string audioCodec;  // 如 "aac"
    int sampleRate = 0;      // 如 48000
    int channels = 0;        // 如 2

    std::string error;  // 失败原因（valid == false 时有效）
};

class MediaProbe {
public:
    /// 读容器/流信息。失败时返回 valid=false 且 error 有内容。
    static MediaInfo probe(FfmpegApi &api, const std::string &path);

    /// 视频流的关键帧时间戳（秒，升序去重）。
    /// 无视频流时返回空表。
    static std::vector<double> keyframeTimes(FfmpegApi &api, const std::string &path,
                                             std::string *error = nullptr);
};

}  // namespace lr
