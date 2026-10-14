// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// TranscodeTask —— 完整编解码管线，对应原来两个「重新编码」功能：
//
//   批量压缩 (Tab5)  /  自定义编码 (Tab6)
//
// 管线（原来是 ffmpeg 一条命令，现在在进程内展开）：
//
//   视频：demux → 解码 → sws_scale(缩放/像素格式转换) → 编码 → mux
//   音频：demux → 解码 → swr_convert(重采样)          → AAC 编码 → mux
//
// 与原来那条命令的对应关系：
//   -vf scale=…      → sws_getContext + sws_scale
//   -c:v <enc>       → avcodec_find_encoder_by_name
//   -b:v / -maxrate  → AV_CODEC 的 bit_rate + av_opt_set("maxrate")
//   -bufsize         → av_opt_set("bufsize")
//   -pix_fmt yuv420p → 编码器 pix_fmt + swscale 输出格式
//   -profile/-preset → av_opt_set（硬件编码器不认的项会被忽略）
//   -c:a aac -b:a    → 找 aac 编码器 + 音频 bit_rate
//
// 全部通过 FfmpegApi 的函数指针调用，工程本身不链接 libav*。
// ---------------------------------------------------------------------------

#include <atomic>
#include <string>
#include <utility>
#include <vector>

#include "core/ITask.h"

namespace lr {

struct TranscodeSpec {
    /// 日志/进度条上显示的名字
    std::string title;

    std::string input;
    std::string output;

    /// 视频编码器名（ffmpeg 的 -c:v 值，如 "libx264" / "h264_nvenc"）
    std::string videoEncoder = "libx264";

    /// 视频目标码率（kbps）
    int videoKbps = 4000;

    /// 音频目标码率（kbps）；<=0 表示不输出音频轨
    int audioKbps = 128;

    /// 分辨率预设的**显示名**，取值来自 pathutil::scaleFilters() 的键
    ///（"原始大小" / "一半" / "四分之一" / "1920*1080" / "1280*720"）。
    /// 之所以传显示名而不是宽高，是因为"一半/四分之一"是相对缩放，
    /// 必须在读到源尺寸后才算得出来。
    std::string scale = "原始大小";

    /// x264 风格 preset（硬件编码器会自动忽略）
    std::string preset = "fast";
    /// H.264 level（"4.1"）；videotoolbox 不接受 level，会自动跳过
    std::string level = "4.1";

    /// 允许回退到 libx264：硬件编码器初始化失败时启用
    bool fallbackToX264 = true;

    /// 输出封装器的私有选项，例如 {"movflags", "+faststart"}
    std::vector<std::pair<std::string, std::string>> muxerOptions;
};

class TranscodeTask : public ITask {
public:
    explicit TranscodeTask(TranscodeSpec spec) : spec_(std::move(spec)) {}

    QString title() const override;
    bool run(TaskSignals &sig) override;
    void requestStop() override { cancel_.store(true); }

private:
    TranscodeSpec spec_;
    std::atomic<bool> cancel_{false};
};

}  // namespace lr
