// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "platform/ThreadUtil.h"

#include <exception>
#include <memory>
#include <new>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>  // _beginthreadex
#endif

namespace lr::platform {

void setThreadName(const char *name) {
#ifdef _WIN32
    if (name == nullptr) return;

    // Win10 1607+ 的正道：SetThreadDescription。它写进线程的元数据，
    // dump（MiniDumpWriteDump 默认带线程信息）里直接能看见。
    using SetThreadDescriptionFn = HRESULT(WINAPI *)(HANDLE, PCWSTR);
    static SetThreadDescriptionFn setDescription = reinterpret_cast<SetThreadDescriptionFn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription"));
    // SetThreadDescription 取不到（Win10 1607 以前）就算了 —— 名字只服务于
    // 调试可读性，拿不到比写一个不可移植的 hack 划算。
    //
    // 这里原本还留了一条 0x406D1388 的路子：那个约定必须包在 SEH 的
    // __try/__except 里才安全（没有调试器接住时它是个货真价实的异常），
    // 而 __try/__except 是 MSVC 语法，MinGW 只有 __try1/__except1 的变体，
    // 写法不同且同样不跨编译器 —— 为一个调试便利引入方言不值得。
    if (setDescription == nullptr) return;

    wchar_t wide[64] = {};
    MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, 63);
    setDescription(GetCurrentThread(), wide);
#else
    (void)name;
#endif
}

#ifdef _WIN32

namespace {

/// 大栈线程的上下文：函数 + 线程名，堆上存活到线程退出。
struct ThreadCtx {
    std::function<void()> fn;
    const char *name = nullptr;
};

unsigned __stdcall bigStackTrampoline(void *raw) {
    std::unique_ptr<ThreadCtx> ctx(static_cast<ThreadCtx *>(raw));
    if (ctx->name != nullptr) setThreadName(ctx->name);
    try {
        ctx->fn();
    } catch (...) {
        // 与 FramePlayer / WaveformBuilder 的线程入口同款原则：
        // 异常逃出线程函数 = std::terminate = 整个进程消失。
        // 这里兜住，至少让调用方的收尾逻辑有机会跑。
    }
    return 0;
}

/// runWithTimeout 专用：跑完置事件；超时没人等，自己 delete。
struct TimeoutCtx {
    std::function<void()> fn;
    const char *name = nullptr;
    HANDLE done = nullptr;
};

unsigned __stdcall timeoutTrampoline(void *raw) {
    auto *ctx = static_cast<TimeoutCtx *>(raw);
    try {
        ctx->fn();
    } catch (...) {
    }
    if (ctx->done != nullptr) SetEvent(ctx->done);
    delete ctx;
    return 0;
}

}  // namespace

BigStackThread::BigStackThread(std::function<void()> f, const char *name,
                               std::size_t stackBytes) {
    auto *ctx = new (std::nothrow) ThreadCtx{std::move(f), name};
    if (ctx == nullptr) return;  // handle_ 保持 null：joinable() 为 false
    // stackBytes 为 0 时用系统默认（与 std::thread 一致）
    const unsigned stack = (stackBytes == 0) ? 0u : static_cast<unsigned>(stackBytes);
    const uintptr_t handle =
        _beginthreadex(nullptr, stack, &bigStackTrampoline, ctx, 0, nullptr);
    if (handle == 0) {
        // 起不来：就地跑完再交还（保底，别把调用方挂死）
        std::unique_ptr<ThreadCtx> guard(ctx);
        if (name != nullptr) setThreadName(name);
        guard->fn();
        return;
    }
    handle_ = reinterpret_cast<void *>(handle);
}

BigStackThread::~BigStackThread() {
    if (handle_ != nullptr) {
        // 不 join 就 detach：线程继续跑，我们不再等它
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
}

BigStackThread::BigStackThread(BigStackThread &&other) noexcept
    : handle_(other.handle_) {
    other.handle_ = nullptr;
}

BigStackThread &BigStackThread::operator=(BigStackThread &&other) noexcept {
    if (this != &other) {
        if (handle_ != nullptr) CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

bool BigStackThread::joinable() const { return handle_ != nullptr; }

void BigStackThread::join() {
    if (handle_ == nullptr) return;
    WaitForSingleObject(static_cast<HANDLE>(handle_), INFINITE);
    CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
}

bool runWithTimeout(const std::function<void()> &f, std::chrono::milliseconds timeout,
                    const char *name, std::size_t stackBytes) {
    auto *ctx = new (std::nothrow) TimeoutCtx{f, name, CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (ctx == nullptr) return false;
    if (ctx->done == nullptr) {
        // 连事件都建不出来：就地跑（至少不丢功能）
        std::unique_ptr<TimeoutCtx> guard(ctx);
        try {
            guard->fn();
        } catch (...) {
        }
        return true;
    }
    const unsigned stack = (stackBytes == 0) ? 0u : static_cast<unsigned>(stackBytes);
    const uintptr_t handle =
        _beginthreadex(nullptr, stack, &timeoutTrampoline, ctx, 0, nullptr);
    if (handle == 0) {
        std::unique_ptr<TimeoutCtx> guard(ctx);
        CloseHandle(guard->done);
        try {
            guard->fn();
        } catch (...) {
        }
        return true;
    }
    HANDLE thread = reinterpret_cast<HANDLE>(handle);
    const DWORD wait = WaitForSingleObject(ctx->done, static_cast<DWORD>(timeout.count()));
    CloseHandle(ctx->done);
    if (wait == WAIT_OBJECT_0) {
        // 正常跑完：线程即将退出，收掉句柄
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
        return true;
    }
    // 超时：线程还在跑，交给它自己收尾（ctx 由线程 delete）。
    // 关掉句柄不影响线程继续运行；之后不再碰它。
    CloseHandle(thread);
    return false;
}

#else  // !_WIN32 —— 便携兜底：std::thread 没有栈大小参数，退化处理

namespace {
struct StdThreadBox {
    std::thread thread;
    explicit StdThreadBox(std::thread t) : thread(std::move(t)) {}
    ~StdThreadBox() {
        if (thread.joinable()) thread.detach();
    }
};
}  // namespace

BigStackThread::BigStackThread(std::function<void()> f, const char *name,
                               std::size_t /*stackBytes*/) {
    if (name != nullptr) setThreadName(name);
    auto *box = new (std::nothrow) StdThreadBox(std::thread([fn = std::move(f)]() {
        try {
            fn();
        } catch (...) {
        }
    }));
    handle_ = box;
}

BigStackThread::~BigStackThread() { delete static_cast<StdThreadBox *>(handle_); }

BigStackThread::BigStackThread(BigStackThread &&other) noexcept
    : handle_(other.handle_) {
    other.handle_ = nullptr;
}

BigStackThread &BigStackThread::operator=(BigStackThread &&other) noexcept {
    if (this != &other) {
        delete static_cast<StdThreadBox *>(handle_);
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

bool BigStackThread::joinable() const {
    auto *box = static_cast<StdThreadBox *>(handle_);
    return box != nullptr && box->thread.joinable();
}

void BigStackThread::join() {
    auto *box = static_cast<StdThreadBox *>(handle_);
    if (box != nullptr && box->thread.joinable()) box->thread.join();
}

bool runWithTimeout(const std::function<void()> &f, std::chrono::milliseconds timeout,
                    const char *name, std::size_t /*stackBytes*/) {
    // 非 Windows 没有 _beginthreadex：用 async + future 等，
    // 超时只能返回 false（future 析构会等着，聊胜于无）。
    auto fut = std::async(std::launch::async, [&f, name]() {
        setThreadName(name);
        try {
            f();
        } catch (...) {
        }
    });
    return fut.wait_for(timeout) == std::future_status::ready;
}

#endif  // _WIN32

}  // namespace lr::platform
