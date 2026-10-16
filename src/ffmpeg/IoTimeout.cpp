// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "ffmpeg/IoTimeout.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include <libavformat/avformat.h>
#include <libavutil/error.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace lr::io {
namespace {

/// 当前线程"下一步 I/O"的截止时刻（steady_clock 毫秒，0 = 不设限）。
///
/// 为什么可以用 thread_local：FFmpeg 的 interrupt_callback 是在**执行阻塞
/// I/O 的那个线程上同步调用**的，与我们的读循环同线程。所以截止时间存在
/// 线程本地即可，不存在"context 比状态活得久"的悬垂指针问题 —— 状态根本
/// 不挂在 context 上。
thread_local int64_t t_deadlineMs = 0;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int interruptCallback(void * /*opaque*/) {
    const int64_t deadline = t_deadlineMs;
    if (deadline == 0) return 0;  // 没上刹车：永远放行
    return (nowMs() > deadline) ? 1 : 0;
}

#ifdef _WIN32

/// 路径是不是"可能慢"的网络位置：UNC（\\server\share\...）或网络驱动器。
/// 本地路径不做探测（CreateFile+读 1 字节虽然也快，但没必要改变现有行为）。
bool isRemotePath(const std::string &path) {
    if (path.size() >= 2 && (path[0] == '\\' || path[0] == '/') &&
        (path[1] == '\\' || path[1] == '/')) {
        return true;  // UNC
    }
    if (path.size() >= 2 && path[1] == ':') {
        wchar_t root[4] = {static_cast<wchar_t>(path[0]), L':', L'\\', L'\0'};
        return GetDriveTypeW(root) == DRIVE_REMOTE;
    }
    return false;
}

/// 打开并读 1 字节，最多等 timeoutMs。timedOut 区分"网络无响应"和
/// "文件根本不在/没权限"——后者交给 avformat_open_input 报更准的错，
/// 只有前者才提前失败。
bool quickReadable(const std::string &path, int timeoutMs, bool &timedOut) {
    timedOut = false;
    const int wideLen =
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    if (wideLen <= 1) return true;  // 转换失败：不拦，让 ffmpeg 自己报
    std::wstring wide(static_cast<std::size_t>(wideLen - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wide.data(), wideLen);

    HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return true;  // 打不开的原因很多（不存在/权限/占锁），交给 ffmpeg 报
    }

    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool readable = true;
    if (ov.hEvent != nullptr) {
        BYTE byte = 0;
        const BOOL ok = ReadFile(file, &byte, 1, nullptr, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            const DWORD w = WaitForSingleObject(ov.hEvent, static_cast<DWORD>(timeoutMs));
            if (w == WAIT_TIMEOUT) {
                readable = false;
                timedOut = true;
                CancelIoEx(file, &ov);  // 取消可能也慢，但比干等着强
                WaitForSingleObject(ov.hEvent, 2000);  // 给它最多 2 秒收尾
            }
        }
        CloseHandle(ov.hEvent);
    }
    CloseHandle(file);
    return readable;
}

#endif  // _WIN32

}  // namespace

void armTimeout(int timeoutMs) {
    t_deadlineMs = (timeoutMs > 0) ? (nowMs() + timeoutMs) : 0;
}

void disarmTimeout() { t_deadlineMs = 0; }

AVFormatContext *openInputWithTimeout(FfmpegApi &api, const std::string &path,
                                      int timeoutSec, std::string &error) {
#ifdef _WIN32
    // 网络路径先探可达性：离线时把"导入卡死几十秒"变成"立即明确报错"
    if (isRemotePath(path)) {
        bool timedOut = false;
        if (!quickReadable(path, timeoutSec * 1000, timedOut) && timedOut) {
            error = "网络文件无响应（" + std::to_string(timeoutSec) +
                    " 秒内读不出数据，目标机可能不在线）";
            return nullptr;
        }
    }
#endif

    // 必须自己 alloc 再挂回调：传 nullptr 的话 avformat_open_input 内部才分配，
    // 没有机会设 interrupt_callback
    AVFormatContext *ic = api.avformat_alloc_context();
    if (ic == nullptr) {
        error = "分配解封装上下文失败";
        return nullptr;
    }
    ic->interrupt_callback.callback = &interruptCallback;
    ic->interrupt_callback.opaque = nullptr;

    armTimeout(timeoutSec * 1000);
    const int ret = api.avformat_open_input(&ic, path.c_str(), nullptr, nullptr);
    disarmTimeout();
    if (ret < 0) {
        // avformat_open_input 失败时已释放 ic 并置空，不用我们管
        if (ret == AVERROR_EXIT) {
            error = "打开输入超时（" + std::to_string(timeoutSec) +
                    " 秒无进展，可能是网络协议对方无响应）";
        } else {
            error = api.errorText(ret);
        }
        return nullptr;
    }
    return ic;
}

}  // namespace lr::io
