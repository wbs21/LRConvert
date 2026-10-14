// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// RemuxTask —— 纯流拷贝管线（不解码、不编码），对应原来四个「无损」功能：
//
//   视频截取 (CopyAll + 起止时间)  /  音视频分离 (VideoOnly / AudioOnly)
//   音视频合并 (Merge)             /  多视频连接 (Concat)
//
// 管线：avformat_open_input →（可选 seek）→ av_read_frame
//       → av_packet_rescale_ts → av_interleaved_write_frame → av_write_trailer
//
// 全部通过 FfmpegApi 的函数指针调用，工程本身不链接 libav*。
// ---------------------------------------------------------------------------

#include <atomic>
#include <string>
#include <utility>
#include <vector>

#include "core/ITask.h"

namespace lr {

struct RemuxSpec {
    enum class Kind {
        CopyAll,    ///< -map 0 -c copy（保留全部轨道）
        VideoOnly,  ///< -map 0:v -c copy -an
        AudioOnly,  ///< -map 0:a -c copy -vn
        Merge,      ///< 输入0 取视频、输入1 取音频
        Concat,     ///< N 个输入顺序拼接
    };

    Kind kind = Kind::CopyAll;

    /// 日志/进度条上显示的名字
    std::string title;

    /// 输出文件（封装格式按扩展名推断）
    std::string output;

    /// 输入文件。Merge 时为 [视频, 音频]；Concat 时为 N 个；其余为 1 个。
    std::vector<std::string> inputs;

    /// 截取范围（秒，相对第一个输入的时间轴）。< 0 表示不限。
    double startSec = -1.0;
    double endSec = -1.0;

    /// Merge 专用：以较短的那个输入为准（原 -shortest）
    bool shortest = false;

    /// 输出封装器的私有选项，例如 {"movflags", "+faststart"}
    std::vector<std::pair<std::string, std::string>> muxerOptions;
};

class RemuxTask : public ITask {
public:
    explicit RemuxTask(RemuxSpec spec) : spec_(std::move(spec)) {}

    QString title() const override;
    bool run(TaskSignals &sig) override;
    void requestStop() override { cancel_.store(true); }

private:
    RemuxSpec spec_;
    std::atomic<bool> cancel_{false};
};

}  // namespace lr
