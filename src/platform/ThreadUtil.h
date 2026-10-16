// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// 线程小工具 —— 线程命名 + 大栈线程。
//
// 1) setThreadName（稳定性改进方案 P1-6）：
//    dump 里只有线程 ID 时，排查"崩在哪个业务线程"全靠猜。给线程起名之后，
//    VS / WinDbg 打开 .dmp 一眼就能看出来，排查效率差距很大。
//    每个线程创建处加一行即可。
//
// 2) BigStackThread（P2-12 栈 hardening）：
//    std::thread 不支持指定栈大小（Windows 上默认 1MB）。SWR / 解码库调用链
//    很深，递归解析路径上栈溢出是真风险。解析/播放/波形这些重线程显式给 8MB，
//    配合看门狗（CrashDump）兜底。
//    接口刻意与 std::thread 保持最小一致（joinable / join），方便原地替换。
// ---------------------------------------------------------------------------

#include <chrono>
#include <cstddef>
#include <functional>

namespace lr::platform {

/// 给**当前线程**起名。尽快调用（线程入口第一行）。
/// 名字只影响调试器/dump 的显示，不影响任何行为。
void setThreadName(const char *name);

/// 大栈线程。析构时不 join 会 detach（与 std::thread 相同语义）。
class BigStackThread {
public:
    /// 默认 8MB。播放/波形/解码线程用默认值即可。
    static constexpr std::size_t kDefaultStack = 8u << 20;

    /// f 为线程入口；name 可空（不起名）。栈默认 8MB。
    explicit BigStackThread(std::function<void()> f, const char *name = nullptr,
                            std::size_t stackBytes = kDefaultStack);

    /// 空线程（还没有起任何东西）。用于「先占个位、稍后再赋值」的成员变量
    /// —— FramePlayer 就是这种用法。joinable() 为 false，join() 直接返回。
    BigStackThread() = default;

    ~BigStackThread();

    BigStackThread(const BigStackThread &) = delete;
    BigStackThread &operator=(const BigStackThread &) = delete;
    /// 可移动（放进 vector 用）：移动后源对象不再 joinable
    BigStackThread(BigStackThread &&other) noexcept;
    BigStackThread &operator=(BigStackThread &&other) noexcept;

    bool joinable() const;
    void join();

private:
    void *handle_ = nullptr;  // Windows：线程句柄；其他平台：std::thread 堆对象
};

/// 在大栈线程上跑 f，最多等 timeout。
///
/// 超时返回 false —— **f 可能仍在后台运行**，因此 f 必须自持所需状态
/// （不得按引用捕获调用方栈上的对象，不得在超时后再去碰 f 的中间结果）。
/// 用于"调用可能卡住但又不能让它拖死整个流程"的场景（如硬件编码器探测）。
bool runWithTimeout(const std::function<void()> &f, std::chrono::milliseconds timeout,
                    const char *name = nullptr,
                    std::size_t stackBytes = BigStackThread::kDefaultStack);

}  // namespace lr::platform
