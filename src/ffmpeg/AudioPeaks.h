// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// 音频峰值 —— Tab1 时间轴波形层的数据源。
//
// 每片得出一个波形高度（该片内的峰值，0..32767），片内用这个高度填满。
//
// 两套用法并存：
//   ① 整轨全量（当前 UI 用的）：allocate() 先按固定密度把数组开好，
//      再由 WaveformBuilder 分 4 个线程各写互不重叠的一段，边算边画。
//   ② 只算某一区间（兜底 / media_check 工具）：buildRange()。
//
// 「没算」和「算出来是静音」必须分得开，所以多了 ready[]：
// 没算到的片画留白，不能画成 0 高度，否则看着像静音。
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "ffmpeg/FfmpegApi.h"

namespace lr {

/// 峰值计算用的重采样率：峰值只要低频包络，8k 足够且快。
/// 分段全量算的时候，段边界换算也依赖它，所以放在头文件里。
constexpr int kPeakSampleRate = 8000;

/// 解码出来的一段 s16 采样（单声道 / 8kHz）交给调用方投片。
/// 参数 startSec 是 samples[0] 对应的时间；返回 true 表示"够了，别再解了"。
using SampleSink = std::function<bool(const int16_t *samples, int count, double startSec)>;

struct AudioPeaks {
    double fromSec = 0.0;       // 覆盖区间起点（秒）
    double toSec = 0.0;         // 覆盖区间终点（秒）
    double pointsPerSec = 0.0;  // 每秒点数（= 切片数 / 区间时长）
    std::vector<int16_t> min;   // 每片最小振幅
    std::vector<int16_t> max;   // 每片最大振幅
    std::vector<uint8_t> ready;  // 每片是否已算出来（区分"没算"和"算出来是静音"）

    bool empty() const { return min.empty(); }
    std::size_t size() const { return min.size(); }
    /// 该峰值数据覆盖的时长
    double span() const { return toSec - fromSec; }

    /// 预分配一整条（整轨全量用）。开好之后**不再改大小**，
    /// 这样多个线程写互不重叠的下标区间是安全的，也才能边算边画。
    void allocate(double from, double to, double pps) {
        fromSec = from;
        toSec = to;
        pointsPerSec = pps;
        const std::size_t n = static_cast<std::size_t>(
            std::max(1.0, std::ceil(std::max(0.0, to - from) * pps)));
        min.assign(n, 0);
        max.assign(n, 0);
        ready.assign(n, 0);
    }
    /// 时间 → 片下标（越界返回 size()，调用方自己判）
    std::size_t indexAt(double t) const {
        if (pointsPerSec <= 0.0 || t <= fromSec) return 0;
        const auto i = static_cast<std::size_t>((t - fromSec) * pointsPerSec);
        return (i < min.size()) ? i : min.size();
    }
    /// 是否完整覆盖 [from, to) —— 没覆盖就说明这份数据是旧的，需要重算
    bool covers(double from, double to) const {
        return !empty() && fromSec <= from + 1e-6 && toSec >= to - 1e-6;
    }
    /// 第 i 片的波形高度（0..32767）；**-1 表示这一片还没算出来**（绘制时留白）
    int level(std::size_t i) const {
        if (i >= min.size()) return -1;
        if (i < ready.size() && ready[i] == 0) return -1;
        return std::max(std::abs(static_cast<int>(min[i])),
                        std::abs(static_cast<int>(max[i])));
    }
};

class AudioPeakBuilder {
public:
    /// 整条音轨建峰值（pointsPerSec 为每秒点数；media_check 工具与兜底用）
    static bool build(FfmpegApi &api, const std::string &path, double pointsPerSec,
                      AudioPeaks &out, std::string &error);

    /// 只算 [fromSec, toSec) 这一段的峰值，切成 slices 片
    static bool buildRange(FfmpegApi &api, const std::string &path, double fromSec,
                           double toSec, int slices, AudioPeaks &out,
                           std::string &error);

    /// 把 [fromSec, toSec) 解码成 单声道/s16/8kHz，一段一段交给 sink。
    /// 这是上面两个函数与"分段全量"共用的解码骨架：
    /// 8k 重采样、seek 回退 1 秒、avcodec_flush_buffers、冲 swr 残留
    /// 全在这里，别在别处重写一遍（这几处都是踩过坑的）。
    static bool decodeRange(FfmpegApi &api, const std::string &path, double fromSec,
                            double toSec, bool seekToStart, const SampleSink &sink,
                            std::string &error);
};

}  // namespace lr
