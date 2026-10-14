#pragma once
// ---------------------------------------------------------------------------
// 音频峰值 —— Tab1 时间轴波形层的数据源。
//
// 按需求"切片"来算：只算**当前可见区间**，把它切成 N 片（600），
// 每片得出一个波形高度（该片内的峰值，0..32767），片内用这个高度填满。
// 不做整轨预计算 —— 长视频导入时先出画面，波形再在后台算。
//
// 缩放/平移后可见区间变了，就重新对新的区间切片计算（区间小的时候很快）。
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "ffmpeg/FfmpegApi.h"

namespace lr {

struct AudioPeaks {
    double fromSec = 0.0;       // 覆盖区间起点（秒）
    double toSec = 0.0;         // 覆盖区间终点（秒）
    double pointsPerSec = 0.0;  // 每秒点数（= 切片数 / 区间时长）
    std::vector<int16_t> min;   // 每片最小振幅
    std::vector<int16_t> max;   // 每片最大振幅

    bool empty() const { return min.empty(); }
    std::size_t size() const { return min.size(); }
    /// 该峰值数据覆盖的时长
    double span() const { return toSec - fromSec; }
    /// 是否完整覆盖 [from, to) —— 没覆盖就说明这份数据是旧的，需要重算
    bool covers(double from, double to) const {
        return !empty() && fromSec <= from + 1e-6 && toSec >= to - 1e-6;
    }
    /// 第 i 片的波形高度（0..32767）
    int level(std::size_t i) const {
        if (i >= min.size()) return 0;
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
    /// （需求：可见区间切 600 片，片内用该片高度填满）
    static bool buildRange(FfmpegApi &api, const std::string &path, double fromSec,
                           double toSec, int slices, AudioPeaks &out,
                           std::string &error);
};

}  // namespace lr
