// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

// ---------------------------------------------------------------------------
// capability_check —— M0 验收用的小工具（**不依赖 Qt**，Qt6 没装也能构建运行）。
//
// 作用：
//   1. 证明"运行时 dlopen 加载 5 个 DLL"这条路在当前产物上真的可行；
//   2. 打印这份 FFmpeg 构建里实际可用的编解码器 / 封装器清单（方案 §2.1 的依据）；
//   3. 测出加载耗时，为"启动 < 500ms"的验收标准提供实测数据。
//
// 注意：本程序**没有链接**任何 libav*（见 CMakeLists.txt），
// 全部符号都是运行时解析的 —— 这正是方案 §6 的核心机制。
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ffmpeg/FfmpegApi.h"
#include "ffmpeg/FfmpegCapability.h"

namespace {

void printList(const char *title, const std::vector<std::string> &items) {
    std::printf("%s（%zu）：", title, items.size());
    if (items.empty()) {
        std::printf(" 无\n");
        return;
    }
    std::printf("\n");
    for (size_t i = 0; i < items.size(); ++i) {
        std::printf("   %-16s", items[i].c_str());
        if ((i + 1) % 5 == 0) std::printf("\n");
    }
    if (items.size() % 5 != 0) std::printf("\n");
}

}  // namespace

int main() {
    const auto t0 = std::chrono::steady_clock::now();

    // ---- 1. 先模拟"UI 首帧已显示"：这里刻意不做任何 FFmpeg 相关动作 ----
    std::printf("[1] 程序启动完成，尚未加载任何 FFmpeg 动态库\n");
    const auto tStart = std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - t0).count() / 1000.0;
    std::printf("    启动到此处：%.1f ms（此阶段与 FFmpeg 完全无关）\n\n", tStart);

    // ---- 2. UI 就绪后才加载动态库 ----
    auto &api = FfmpegApi::instance();
    std::string error;
    const auto t1 = std::chrono::steady_clock::now();
    const bool ok = api.load(error);
    const auto tLoad = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now() - t1).count() / 1000.0;

    std::printf("[2] 动态库加载：%s\n", ok ? "成功" : "失败");
    if (!ok) {
        std::printf("    原因：%s\n", error.c_str());
        return 1;
    }
    std::printf("    耗时：%.1f ms\n", tLoad);
    std::printf("    目录：%s\n\n", api.loadedDir().c_str());

    // ---- 3. 版本与许可 ----
    std::printf("[3] %s\n", api.summary().c_str());
    if (api.avcodec_license) std::printf("    许可：%s\n", api.avcodec_license());
    if (api.avcodec_configuration) {
        std::string cfg = api.avcodec_configuration();
        std::printf("    编译参数：%s\n",
                    cfg.size() > 160 ? (cfg.substr(0, 160) + " …").c_str() : cfg.c_str());
    }
    std::printf("\n");

    // ---- 4. 能力清单（按媒体类型分类）----
    FfmpegCapability cap = collectCapability(api);
    std::printf("[4] 本机 FFmpeg 实际能力\n");
    std::printf("    —— 解码器 共 %d 个 ——\n", cap.decoders.total());
    printList("      视频解码", cap.decoders.video);
    printList("      音频解码", cap.decoders.audio);
    printList("      字幕解码", cap.decoders.subtitle);
    if (!cap.decoders.other.empty()) printList("      其他解码", cap.decoders.other);

    std::printf("    —— 编码器 共 %d 个 ——\n", cap.encoders.total());
    printList("      视频编码", cap.encoders.video);
    printList("      音频编码", cap.encoders.audio);
    printList("      字幕编码", cap.encoders.subtitle);
    if (!cap.encoders.other.empty()) printList("      其他编码", cap.encoders.other);

    printList("    解封装器", cap.demuxers);
    printList("    封装器", cap.muxers);

    // ---- 5. 对六大功能的可行性快速判定 ----
    std::printf("\n[5] 六大功能可行性（基于上面这份能力）\n");
    auto has = [](const std::vector<std::string> &v, const char *n) {
        for (const auto &s : v) if (s == n) return true;
        return false;
    };
    const auto &vdec = cap.decoders.video;
    const auto &venc = cap.encoders.video;
    const auto &adec = cap.decoders.audio;
    const bool canH264Out = has(venc, "libx264");
    const bool canAudioIn = has(adec, "aac") || has(adec, "mp3");
    std::printf("    视频重编码（libx264）      ：%s\n", canH264Out ? "可用" : "不可用");
    std::printf("    音频解码（aac / mp3 任一） ：%s\n", canAudioIn ? "可用" : "不可用 → 压缩功能跑不通");
    std::printf("    输出 mp4                   ：%s\n", has(cap.muxers, "mp4") ? "可用" : "不可用");
    std::printf("    读取 mkv（matroska）       ：%s\n", has(cap.demuxers, "matroska") ? "可用" : "不可用");
    std::printf("    输出 mkv（matroska）       ：%s\n", has(cap.muxers, "matroska") ? "可用" : "不可用");

    // 硬件编码器：按前缀扫描，避免写死清单
    std::printf("\n[6] 硬件编码器（按后缀扫描）\n");
    const char *hwSuffixes[] = {"_nvenc", "_qsv", "_amf", "_vaapi", "_d3d12va", "_mf", "_videotoolbox"};
    for (const char *suf : hwSuffixes) {
        std::vector<std::string> hit;
        for (const auto &e : venc)
            if (e.size() > strlen(suf) && e.compare(e.size() - strlen(suf), strlen(suf), suf) == 0)
                hit.push_back(e);
        if (!hit.empty()) {
            std::string line;
            for (const auto &h : hit) line += h + " ";
            std::printf("    %-14s %s\n", suf, line.c_str());
        }
    }
    std::printf("    （软件编码器：libx264 / libx265 / libaom-av1 / libsvtav1 / libvpx-vp9）\n");

    // ---- 7. 硬件编码器本机探测 ----
    // 与 GUI 启动后的识别、TranscodeTask 的打开路径是同一套判定：
    // find_encoder + 最小配置 open2。上面 [6] 只说明"构建里有"，
    // 这一节才说明"这台机器真的能用"——GUI 下拉框按本节结果隐藏。
    std::printf("\n[7] 硬件编码器本机探测（open2 实测）\n");
    const std::vector<std::string> hwNames = {"h264_nvenc", "h264_qsv",
                                              "h264_amf", "h264_videotoolbox"};
    for (const auto &p : probeHardwareEncoders(api, hwNames)) {
        std::printf("    %-18s %s\n", p.name.c_str(),
                    p.available ? "可用"
                                : ("不可用（" + p.error + "）").c_str());
    }
    std::printf("    libx264（软件）不受影响；不可用项在 GUI 编码器下拉框中隐藏\n");

    return 0;
}
