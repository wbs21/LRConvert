#pragma once
// ---------------------------------------------------------------------------
// 时间工具 —— 对应 Python 版 ffmpeg_cmd.py 的 parse_timestamp / format_timestamp
//
// 刻意不依赖 Qt，也不依赖 FFmpeg：纯字符串 ↔ 秒的换算，方便单测。
// ---------------------------------------------------------------------------

#include <optional>
#include <string>

namespace lr::timeutil {

/// 把 "HH:MM:SS.mmm" / "MM:SS" / "SS" / 纯秒数解析为秒；失败返回 nullopt。
///
/// 原实现用固定下标切片（time[3:5]），遇到 "0:05.50" 或 "N/A" 会抛异常，
/// C++ 版按字符解析，兼容各种写法。
std::optional<double> parseTimestamp(const std::string &text);

/// 秒 -> "HH:MM:SS"；非法值返回 "--:--:--"
std::string formatTimestamp(double seconds);
std::string formatTimestamp(std::optional<double> seconds);

/// 秒 -> FFmpeg 内部时间基（AV_TIME_BASE = 1000000）的整数
long long toAvTime(double seconds);

/// FFmpeg 内部时间 -> 秒
double fromAvTime(long long value);

}  // namespace lr::timeutil
