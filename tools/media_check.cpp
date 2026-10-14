// ---------------------------------------------------------------------------
// Tab1 时间轴的无界面验证工具（不依赖 Qt）
//
// 一次性验证这条链路：
//   [1] 媒体信息（时长 / 分辨率 / 帧率 / 编码 / 采样率…）
//   [2] 关键帧表（起点终点吸附的依据）
//   [3] 抓帧（缩略图 3 张 + 预览帧）
//   [4] 整轨音频峰值（兜底路径）
//   [5] 区间切片波形（界面对可见区间实际走的路径：seek + 切 600 片）
//
// 用法：
//   ./build/media_check.exe <视频文件>
// ---------------------------------------------------------------------------

#include <cstdio>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include "ffmpeg/AudioPeaks.h"
#include "ffmpeg/FfmpegApi.h"
#include "ffmpeg/FrameGrabber.h"
#include "ffmpeg/MediaProbe.h"

namespace {

/// 阶段计时：用来对比"导入（快）"与"波形（慢）"的耗时，
/// 这也是 Tab1 改成"先出画面、后算波形"的依据
struct Timer {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    long long elapsedMs() const {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start)
            .count();
    }
};

/// 秒 → 00:00:02.500
std::string formatTime(double sec) {
    if (sec < 0) sec = 0;
    const int totalMs = int(std::llround(sec * 1000.0));
    const int ms = totalMs % 1000;
    const int totalSec = totalMs / 1000;
    const int s = totalSec % 60;
    const int m = (totalSec / 60) % 60;
    const int h = totalSec / 3600;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d", h, m, s, ms);
    return buf;
}

void printInfo(const lr::MediaInfo &info) {
    if (!info.valid) {
        std::printf("  探测失败：%s\n", info.error.c_str());
        return;
    }
    std::printf("  时长      %s（%.3f 秒）\n", formatTime(info.durationSec).c_str(),
                info.durationSec);
    if (info.hasVideo) {
        std::printf("  视频      %dx%d  %.2f fps  %s\n", info.width, info.height, info.fps,
                    info.videoCodec.c_str());
    } else {
        std::printf("  视频      无\n");
    }
    if (info.hasAudio) {
        std::printf("  音频      %s  %dHz/%dch\n", info.audioCodec.c_str(), info.sampleRate,
                    info.channels);
    } else {
        std::printf("  音频      无\n");
    }
}

/// 把峰值画成 ASCII 波形，肉眼确认"形状对不对"
void printAsciiWave(const lr::AudioPeaks &peaks, int cols = 72, int halfRows = 4) {
    const std::size_t n = peaks.size();
    if (n == 0) return;
    for (int row = halfRows; row >= 1; --row) {
        std::string line;
        for (int c = 0; c < cols; ++c) {
            const std::size_t from = static_cast<std::size_t>(c) * n / cols;
            const std::size_t to = std::max(from + 1, static_cast<std::size_t>(c + 1) * n / cols);
            int peak = 0;
            for (std::size_t i = from; i < to && i < n; ++i) {
                peak = std::max(peak, std::max(std::abs(int(peaks.min[i])),
                                               std::abs(int(peaks.max[i]))));
            }
            const double level = double(peak) / 32767.0 * halfRows;
            line += (level >= row - 0.5) ? '#' : ' ';
        }
        std::printf("  |%s|\n", line.c_str());
    }
    for (int row = 1; row <= halfRows; ++row) {
        std::string line;
        for (int c = 0; c < cols; ++c) {
            const std::size_t from = static_cast<std::size_t>(c) * n / cols;
            const std::size_t to = std::max(from + 1, static_cast<std::size_t>(c + 1) * n / cols);
            int peak = 0;
            for (std::size_t i = from; i < to && i < n; ++i) {
                peak = std::max(peak, std::max(std::abs(int(peaks.min[i])),
                                               std::abs(int(peaks.max[i]))));
            }
            const double level = double(peak) / 32767.0 * halfRows;
            line += (level >= row - 0.5) ? '#' : ' ';
        }
        std::printf("  |%s|\n", line.c_str());
    }
}

}  // namespace

int main(int argc, char **argv) {
    if (argc < 2) {
        std::printf("用法：media_check <视频文件>\n");
        return 1;
    }
    const std::string path = argv[1];

    auto &api = FfmpegApi::instance();
    std::string error;
    if (!api.load(error)) {
        std::printf("加载 FFmpeg 失败：%s\n", error.c_str());
        return 1;
    }
    std::printf("[0] %s\n", api.summary().c_str());
    std::printf("文件：%s\n\n", path.c_str());

    // ---- [1] 媒体信息 ----
    std::printf("[1] 媒体信息\n");
    Timer t1;
    const lr::MediaInfo info = lr::MediaProbe::probe(api, path);
    std::printf("  耗时 %lld ms\n", t1.elapsedMs());
    printInfo(info);
    if (!info.valid) return 1;

    // ---- [2] 关键帧表 ----
    std::printf("\n[2] 关键帧（起点/终点吸附依据）\n");
    Timer t2;
    std::string kfError;
    const std::vector<double> keys = lr::MediaProbe::keyframeTimes(api, path, &kfError);
    std::printf("  耗时 %lld ms\n", t2.elapsedMs());
    if (keys.empty()) {
        std::printf("  无（%s）\n", kfError.empty() ? "空表" : kfError.c_str());
    } else {
        std::printf("  共 %zu 个，间隔约 %.3f 秒\n", keys.size(),
                    keys.size() > 1 ? (keys.back() - keys.front()) / (keys.size() - 1) : 0.0);
        for (std::size_t i = 0; i < keys.size() && i < 8; ++i) {
            std::printf("    #%zu  %s\n", i, formatTime(keys[i]).c_str());
        }
        if (keys.size() > 8) std::printf("    ...（其余 %zu 个略）\n", keys.size() - 8);
    }

    // ---- [3] 抓帧（缩略图 5 张的位置：左 / 1/4 / 中 / 3/4 / 右）----
    std::printf("\n[3] 抓帧（0%% / 25%% / 50%% / 75%% / 99.9%% 五处）\n");
    lr::FrameGrabber grabber;
    if (!grabber.open(api, path, error)) {
        std::printf("  取帧器打开失败：%s\n", error.c_str());
    } else {
        const double duration = info.durationSec;
        const double targets[5] = {0.0, duration * 0.25, duration * 0.5, duration * 0.75,
                                   duration * 0.999};
        for (double t : targets) {
            lr::FrameGrabber::Frame frame;
            if (!grabber.grabAt(t, 160, 90, frame, error)) {
                std::printf("  t=%s  失败：%s\n", formatTime(t).c_str(), error.c_str());
                continue;
            }
            // 用平均亮度做个简单校验（全黑往往意味着解码有问题）
            long sum = 0;
            for (unsigned char v : frame.rgb) sum += v;
            const double mean = frame.rgb.empty() ? 0.0 : double(sum) / frame.rgb.size();
            std::printf("  t=%s -> 实际 %s  %dx%d  平均亮度 %.1f\n", formatTime(t).c_str(),
                        formatTime(frame.actualSec).c_str(), frame.width, frame.height, mean);
        }
        grabber.close();
    }

    // ---- [4] 音频峰值 ----
    std::printf("\n[4] 音频峰值（200 点/秒）\n");
    Timer t4;
    lr::AudioPeaks peaks;
    if (!lr::AudioPeakBuilder::build(api, path, 200.0, peaks, error)) {
        std::printf("  无波形：%s\n", error.c_str());
    } else {
        int maxAbs = 0;
        for (std::size_t i = 0; i < peaks.size(); ++i) {
            maxAbs = std::max(maxAbs, std::max(std::abs(int(peaks.min[i])),
                                               std::abs(int(peaks.max[i]))));
        }
        std::printf("  %zu 点，覆盖 %s ~ %s，峰值 %d，耗时 %lld ms\n", peaks.size(),
                    formatTime(peaks.fromSec).c_str(), formatTime(peaks.toSec).c_str(), maxAbs,
                    t4.elapsedMs());
        printAsciiWave(peaks);
    }

    // ---- [5] 区间切片波形（界面实际用的路径）----
    // 取中间 1/3 区间切 600 片；seek + avcodec_flush_buffers 没写对的话
    // 这里会整段都是 0（旧代码就是用"发空包"排空，结果一帧都解不出来）
    std::printf("\n[5] 区间切片波形（可见区间切 600 片）\n");
    const double from = info.durationSec / 3.0;
    const double to = std::min(info.durationSec, from + info.durationSec / 3.0);
    Timer t5;
    lr::AudioPeaks slice;
    if (!lr::AudioPeakBuilder::buildRange(api, path, from, to, 600, slice, error)) {
        std::printf("  失败：%s\n", error.c_str());
    } else {
        int maxAbs = 0;
        int nonZero = 0;
        for (std::size_t i = 0; i < slice.size(); ++i) {
            const int level = slice.level(i);
            maxAbs = std::max(maxAbs, level);
            if (level > 0) ++nonZero;
        }
        std::printf("  %zu 片，覆盖 %s ~ %s，峰值 %d，非静音片 %d，耗时 %lld ms\n", slice.size(),
                    formatTime(slice.fromSec).c_str(), formatTime(slice.toSec).c_str(), maxAbs,
                    nonZero, t5.elapsedMs());
        printAsciiWave(slice);
        if (nonZero == 0) {
            std::printf("  !! 全为静音：seek 后的解码器排空多半写错了\n");
        }
    }

    return 0;
}
