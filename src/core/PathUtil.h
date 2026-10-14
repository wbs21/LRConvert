#pragma once
// ---------------------------------------------------------------------------
// 路径 / 常量工具 —— 对应 Python 版 ffmpeg_cmd.py 的常量与 output_path /
// copy_safe_suffix / write_concat_list。
//
// 不依赖 Qt（方便单测），也不依赖 FFmpeg。
// ---------------------------------------------------------------------------

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lr::pathutil {

// ---- 常量（与 Python 版保持一致） ----

/// 批量压缩时扫描目录用的视频后缀集合
const std::vector<std::string> &videoExtensions();

/// 文件选择框过滤器：视频
std::string videoFileFilter();
/// 文件选择框过滤器：音频
std::string audioFileFilter();

/// 有序名值对（下拉框要按这个顺序显示，所以用 vector 而不是 map）
using NameValues = std::vector<std::pair<std::string, std::string>>;

/// 分辨率预设 -> scale 滤镜表达式。
/// 注意：除以 2/4/8 之后必须再对齐到偶数，否则 yuv420p 编码会直接失败
///（"width not divisible by 2"）——这是原实现里最隐蔽的一个坑。
const NameValues &scaleFilters();

/// 界面下拉框显示名 -> ffmpeg 实际的 -c:v 值
const NameValues &hwEncoders();

/// 启动时编码器下拉框的初始内容：**只含软件编码器**。
/// 硬件编码器要等 probeHardwareEncoders() 探测完再由界面层追加，
/// 保证本机不支持的项从不出现（包括动态库加载中的那两秒）。
const NameValues &softwareEncoders();

/// 硬件编码器的本机要求说明（探测失败时展示，帮助用户理解为什么被隐藏）。
/// 软件编码器或未知编码器返回空串。
std::string hwEncoderRequirement(const std::string &encoderName);

/// 在 NameValues 里按显示名查值；找不到返回 fallback
std::string lookup(const NameValues &table, const std::string &displayName,
                   const std::string &fallback = std::string());

// ---- 路径构造 ----

/// 若文件已存在，追加 "(1)"、"(2)"... 避免静默覆盖
std::string uniquePath(const std::string &path);

/// 根据源文件生成输出路径：原名[tag]_new_时间戳.后缀
std::string outputPath(const std::string &source,
                       const std::string &suffix,
                       const std::string &tag = std::string(),
                       const std::string &stamp = std::string());

/// 流拷贝类操作使用的后缀：沿用原封装，B 站 m4s 分片改成 mp4
std::string copySafeSuffix(const std::string &source);

/// "MMDDHHMMSS" 形式的时间戳
std::string stamp();

/// 取文件后缀（小写）
std::string extensionOf(const std::string &path);

/// 取不含路径、不含后缀的主名
std::string stemOf(const std::string &path);

/// 取所在目录（不含末尾分隔符）；失败返回空串
std::string parentOf(const std::string &path);

/// ffmpeg concat 清单文件的内容（已对单引号转义）
std::string concatListContent(const std::vector<std::string> &files);

}  // namespace lr::pathutil
