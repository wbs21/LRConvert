// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// 整轨波形：分段 + 多线程 + 边算边画 + 可见区优先。
//
// 用法
// ----
//   1) 上层先把 AudioPeaks 用 allocate(0, duration, pointsPerSec) 开好一整条
//      （**开好之后不再改大小**：这样多个线程写互不重叠的下标区间是安全的，
//      也才有"边算边画"——画的时候直接读同一份数据，不用等全部算完）；
//   2) start() 把整轨切成 N 段（默认 16，多于线程数，便于优先级调度），
//      起 workers（默认 4）个线程各领一段；
//   3) 每算完一段 → progress 回调 → 上层 update() 重绘，波形一段一段填出来；
//   4) 可见区间变了 → setPriorityRange()，剩下没派发的段按"离可见区最近"重排；
//      已经派发在跑的那几个不打断（打断要重 seek，不划算）。
//
// 每个 worker **各开一份自己的文件句柄和解码器**：av_read_frame 是"读走一个包"，
// 两个消费者共用一个 AVFormatContext 会互相偷对方的包。
// ---------------------------------------------------------------------------

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "ffmpeg/AudioPeaks.h"
#include "ffmpeg/FfmpegApi.h"
#include "platform/ThreadUtil.h"

namespace lr {

class WaveformBuilder {
public:
    /// 每算完一段回调一次：index 是段号（0 起、按时间顺序），t0/t1 是这一段的区间，
    /// done/total 是完成进度。上层在这里 update() 重绘即可（边算边画）。
    using ProgressCb =
        std::function<void(int index, double t0, double t1, int done, int total)>;
    using ErrorCb = std::function<void(const std::string &error)>;

    ~WaveformBuilder();

    /// target 必须是已经 allocate() 好的整轨峰值（大小固定，不再变）
    bool start(FfmpegApi &api, const std::string &path, double duration,
               double pointsPerSec, int segments, int workers,
               const std::shared_ptr<AudioPeaks> &target);
    void stop();
    bool running() const { return running_; }
    int totalSegments() const { return total_; }
    int doneSegments() const { return doneCount_; }

    /// 可见区间变了：剩下的段按"离可见区最近"重排
    void setPriorityRange(double from, double to);

    void setProgressCallback(ProgressCb cb) { progressCb_ = std::move(cb); }
    void setErrorCallback(ErrorCb cb) { errorCb_ = std::move(cb); }

private:
    struct Segment {
        int index = 0;      // 段号（按时间顺序，0 起）
        double t0 = 0.0;
        double t1 = 0.0;
    };

    void workerMain(FfmpegApi *api, std::string path);
    bool takeNext(Segment &out);

    std::mutex mutex_;
    std::vector<Segment> pending_;
    std::shared_ptr<AudioPeaks> target_;
    double priFrom_ = 0.0;
    double priTo_ = 0.0;

    std::atomic<bool> stopFlag_{false};
    std::atomic<bool> running_{false};
    std::atomic<int> doneCount_{0};
    int total_ = 0;
    /// 大栈线程（P2-12：解码链深，默认 1MB 栈不够）＋ 线程名（P1-6）
    std::vector<lr::platform::BigStackThread> threads_;

    ProgressCb progressCb_;
    ErrorCb errorCb_;
};

}  // namespace lr
