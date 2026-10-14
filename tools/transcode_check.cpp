// ---------------------------------------------------------------------------
// transcode_check —— 无界面的转码验证工具
//
// 走的是与 GUI 完全相同的 TranscodeTask 代码路径，方便在命令行 / CI 里
// 回归验证，也便于和 ffmpeg CLI 的输出做逐项对照。
//
//   transcode_check <输入> <输出> [编码器] [视频kbps] [缩放预设] [音频kbps]
//
// 例：
//   transcode_check in.mp4 out.mp4 libx264 4000 1280*720 128
//   transcode_check in.mp4 out.mp4 h264_nvenc 8000 原始大小 160
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QObject>
#include <cstdio>

#include "ffmpeg/FfmpegApi.h"
#include "tasks/TranscodeTask.h"

using namespace lr;

namespace {

void printHelp() {
    std::printf(
        "用法：transcode_check <输入> <输出> [编码器] [视频kbps] [缩放] [音频kbps]\n"
        " 编码器   默认 libx264（h264_nvenc / h264_qsv / h264_amf 等亦可）\n"
        " 视频kbps 默认 4000\n"
        " 缩放     原始大小 | 一半 | 四分之一 | 1920*1080 | 1280*720\n"
        " 音频kbps 默认 128；传 0 表示不要音轨\n");
}

}  // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    if (argc < 3) {
        printHelp();
        return 2;
    }

    // 命令行参数用的是系统本地编码（Windows 中文环境下是 GBK），
    // 而 TranscodeSpec::scale 里存的是 UTF-8（与 GUI 的 QString 一致），
    // 所以必须显式转换，否则中文的"一半 / 原始大小"会匹配不上。
    auto utf8 = [](const char *s) { return QString::fromLocal8Bit(s).toStdString(); };

    const std::string input = utf8(argv[1]);
    const std::string output = utf8(argv[2]);
    const std::string encoder = argc > 3 ? utf8(argv[3]) : "libx264";
    const int videoKbps = argc > 4 ? std::atoi(argv[4]) : 4000;
    const std::string scale = argc > 5 ? utf8(argv[5]) : "原始大小";
    const int audioKbps = argc > 6 ? std::atoi(argv[6]) : 128;

    std::printf("[1] 加载 FFmpeg 动态库\n");
    auto &api = FfmpegApi::instance();
    std::string error;
    if (!api.load(error)) {
        std::printf("    加载失败：%s\n", error.c_str());
        return 1;
    }
    std::printf("    %s\n", api.summary().c_str());
    std::printf("    加载耗时 %d ms\n\n", api.loadElapsedMs());

    TranscodeSpec spec;
    spec.title = "headless transcode";
    spec.input = input;
    spec.output = output;
    spec.videoEncoder = encoder;
    spec.videoKbps = videoKbps > 0 ? videoKbps : 4000;
    spec.audioKbps = audioKbps;
    spec.scale = scale;
    spec.preset = "fast";
    spec.level = "4.1";
    spec.muxerOptions = {{"movflags", "+faststart"}};

    std::printf("[2] 参数：编码器=%s  视频=%d kbps  缩放=%s  音频=%s\n",
                spec.videoEncoder.c_str(), spec.videoKbps, spec.scale.c_str(),
                spec.audioKbps > 0
                    ? (std::to_string(spec.audioKbps) + " kbps").c_str()
                    : "无音轨");

    TaskSignals sig;
    QObject::connect(&sig, &TaskSignals::log,
                     [](const QString &text, const QString &) {
                         std::printf("    %s\n", text.toUtf8().constData());
                     });
    QObject::connect(&sig, &TaskSignals::progress,
                     [](int percent, const QString &detail) {
                         if (percent >= 0) {
                             std::printf("    [进度] %3d%%  %s\n", percent,
                                         detail.toUtf8().constData());
                         }
                     });

    TranscodeTask task(spec);
    const bool ok = task.run(sig);

    std::printf("\n[3] 结果：%s\n", ok ? "成功" : "失败");
    return ok ? 0 : 1;
}
