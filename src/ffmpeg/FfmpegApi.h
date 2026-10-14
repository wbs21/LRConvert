// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// FfmpegApi —— 全局唯一的 libav* 访问门面。
//
// 硬约束（见重构方案 §6）：
//   * 工程**不链接** libav*，DLL 在 UI 首帧显示之后才由本类用
//     LoadLibrary / dlopen 装载 —— 否则 DLL 会写进 PE 导入表，
//     在 main() 之前就被操作系统加载，启动速度无从优化。
//   * 所有业务代码只能通过 FfmpegApi::instance().av_xxx(...) 调用，
//     禁止直接写 av_xxx（否则链接期又会引入依赖）。
//
// 本类不依赖 Qt（只依赖标准库），可在任意线程构造/加载，也便于单测。
// ---------------------------------------------------------------------------

#include <cstddef>
#include <string>

#include "FfmpegSymbols.h"

// 枚举名与 FfmpegSymbols.h 里的宏参数保持一致（宏展开需要）
enum class FfmpegLib { AVUTIL, AVCODEC, AVFORMAT, SWSCALE, SWRESAMPLE, COUNT };

class FfmpegApi {
public:
    static FfmpegApi &instance();

    /// 加载 5 个动态库并绑定全部符号。可在任意线程调用；重复调用是幂等的。
    /// @param error 失败原因（中文，已包含缺失的库名或符号名）
    bool load(std::string &error);

    void unload();
    bool isLoaded() const { return loaded_; }

    /// 动态库实际所在的目录（用于排错）
    const std::string &loadedDir() const { return dir_; }
    /// 上一次 load() 的耗时（毫秒），用于启动性能验收
    int loadElapsedMs() const { return loadMs_; }

    /// 一行摘要："FFmpeg 9.0.2（avcodec 63 / avformat 63 / avutil 61）· 加载 41ms"
    std::string summary() const;

    /// av_strerror 封装：把 libav* 的错误码变成人话
    std::string errorText(int code) const;

    // ---- 函数指针表（由宏表展开，勿手写） ----
#define LR_DECLARE_FN(lib, ret, name, args) ret(*name) args = nullptr;
    FFMPEG_FN_LIST(LR_DECLARE_FN)
#undef LR_DECLARE_FN

private:
    FfmpegApi() = default;
    FfmpegApi(const FfmpegApi &) = delete;
    FfmpegApi &operator=(const FfmpegApi &) = delete;

    template <typename F>
    static bool bindOne(F &fn, void *handle, const char *symbol, std::string &error);

    void *handles_[static_cast<int>(FfmpegLib::COUNT)]{};
    bool loaded_ = false;
    std::string dir_;
    int loadMs_ = -1;
};
