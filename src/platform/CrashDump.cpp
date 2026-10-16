// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "platform/CrashDump.h"

#include "platform/ThreadUtil.h"

// 用 _WIN32 而不是 Q_OS_WIN：崩溃时 Qt 的状态不可信，这个模块连 QtGlobal 都不该依赖
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <string>

namespace lr::platform {
namespace {

/// 正常退出的标记文件名（P1-8 用）：放在 dump 目录里，
/// 时间戳比最新那份 .dmp 新 = "那次崩溃已经看过了"。
constexpr const wchar_t *kCleanExitMarker = L"clean-exit.marker";

#ifdef _WIN32

// ---- dbghelp 运行时加载（不进导入表，与 FfmpegApi 的做法一致）----

/// MINIDUMP_EXCEPTION_INFORMATION：自己声明一份，省得依赖 dbghelp.h
struct MiniDumpExceptionInfo {
    DWORD ThreadId;
    EXCEPTION_POINTERS *ExceptionPointers;
    BOOL ClientPointers;
};

using MiniDumpWriteDumpFn = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, DWORD,
                                           MiniDumpExceptionInfo *, void *, void *);

// MINIDUMP_TYPE（只用到这两个）
constexpr DWORD kMiniDumpNormal = 0x00000000;
constexpr DWORD kMiniDumpWithThreadInfo = 0x00001000;

/// dbghelp 在**安装时就**加载好：崩溃发生时进程处于异常状态，
/// 那时再 LoadLibrary 能不能成全看运气（加载器锁、异常态下的限制）。
/// 提前拿到函数指针，写 dump 时就只剩一次调用了。
MiniDumpWriteDumpFn g_miniDumpWriteDump = nullptr;

std::wstring g_dumpDir;
std::wstring g_lastDump;
std::function<void(const char *)> g_logSink;

/// 转储进行中：崩溃路径、terminate/signal 路径、看门狗的 forceWriteDump
/// 三方共用。exchange 失败的一方直接放弃（上一份还没写完，别叠上去）。
std::atomic<bool> g_dumpInProgress{false};

// ---- 预建 dump 线程（P0-2）----
//
// sehFilter 跑在崩溃线程的异常帧上，进程仍处在"异常未处理"的状态。
// 实测在这个状态下 CreateThread 可能失败（甚至死在加载器锁上），失败后退回
// 在崩溃线程上直接写 dump —— 那条路产出 0 字节文件。所以线程在安装时就建好，
// 挂在 manual-reset 事件上；崩溃路径只做"填静态请求 + SetEvent"。
HANDLE g_dumpThread = nullptr;
HANDLE g_dumpEvent = nullptr;    ///< 请求转储（manual-reset）
HANDLE g_dumpDoneEvent = nullptr;  ///< 转储完成（manual-reset）

// ---- 看门狗（P0-1）----
constexpr int64_t kWatchdogIntervalMs = 5000;   ///< 每 5 秒查一次
constexpr int64_t kStaleThresholdMs = 15000;    ///< 15 秒无心跳算卡死
constexpr int kHeartbeatCount = 3;
const char *const kHeartbeatNames[kHeartbeatCount] = {"GUI", "播放", "波形"};
/// 各子系统的最近心跳（steady_clock 毫秒；0 = 从未心跳/不再期望）
std::atomic<int64_t> g_heartbeatMs[kHeartbeatCount]{{0}, {0}, {0}};
/// 各子系统"现在是否应该活着"
std::atomic<bool> g_expected[kHeartbeatCount]{{false}, {false}, {false}};
/// 这一段卡死**已经报过**了。一次卡死只留一份 dump：
/// 卡死 30 秒写 6 份同内容的 dmp 只会把真正有用的那一份淹掉
///（实测一次 25 秒的冻结写了两份）。心跳恢复（staleMs 回到阈值内）时清掉。
std::atomic<bool> g_hangReported[kHeartbeatCount]{{false}, {false}, {false}};
std::atomic<bool> g_shuttingDown{false};

void emit(const char *text) {
    // 两条路都走：stderr 给冒烟/回归脚本看，sink 给日志文件看。
    // 崩溃现场里 Qt 不一定还活着，所以 stderr 用裸 fwrite，不碰任何框架。
    std::fprintf(stderr, "[崩溃] %s\n", text);
    std::fflush(stderr);
    if (g_logSink) {
        try {
            g_logSink(text);
        } catch (...) {
            // 记日志失败也不能把崩溃处理拖住
        }
    }
}

const char *exceptionText(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:      return "访问违规（0xC0000005：读/写了不该碰的地址）";
    case EXCEPTION_STACK_OVERFLOW:        return "栈溢出（0xC00000FD：递归太深或局部数组过大）";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "除零（0xC0000094）";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "浮点除零（0xC000008E）";
    case 0xC0000374:                      return "堆损坏（0xC0000374：多半是越界写或双重释放）";
    case 0xC0000409:                      return "栈缓冲区溢出（0xC0000409）";
    case 0xE06D7363:                      return "未捕获的 C++ 异常（0xE06D7363）";
    case EXCEPTION_BREAKPOINT:            return "断点（0x80000003）";
    case EXCEPTION_ILLEGAL_INSTRUCTION:   return "非法指令（0xC000001D）";
    default:                              return "未分类的结构化异常";
    }
}

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

/// 真正写 dump。info 可以为 nullptr（std::terminate / abort / 看门狗这类
/// 没有 SEH 现场的）
///
/// threadId 必须与 info 属于同一个线程：**发生异常的那个线程**。
/// 早先在这里填 GetCurrentThreadId()，结果 dump 线程拿到的自己的 ID 配上
/// 崩溃线程的 ExceptionPointers，MiniDumpWriteDump 直接报 ERROR_NOACCESS(998)
/// —— 写出来的还是个空文件。所以 ID 由调用方（崩溃发生的现场）带进来。
bool writeMinidump(EXCEPTION_POINTERS *info, const char *reason, DWORD threadId = 0,
                   bool clientPointers = false) {
    if (threadId == 0) threadId = GetCurrentThreadId();
    if (g_dumpDir.empty()) return false;

    // 文件名带上时间戳和进程号：多次崩溃不会被互相覆盖
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char name[256];
    std::snprintf(name, sizeof(name),
                  "LRConvert-crash-%04d%02d%02d-%02d%02d%02d-%lu.dmp", st.wYear, st.wMonth,
                  st.wDay, st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId());
    std::wstring path = g_dumpDir + L"\\";
    path.append(name, name + std::strlen(name));

    // 目录可能还没建（首次崩溃），这里兜一下
    CreateDirectoryW(g_dumpDir.c_str(), nullptr);

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    bool dumped = false;
    DWORD lastErr = 0;
    int level = 0;  // 最终用的是哪一档（1=完整现场 2=基础现场 3=仅线程栈）

    // 预加载没成功的话这里再兜一次（正常路径下 g_miniDumpWriteDump 已经有值了）
    MiniDumpWriteDumpFn fn = g_miniDumpWriteDump;
    HMODULE lateLoaded = nullptr;
    if (fn == nullptr) {
        lateLoaded = LoadLibraryW(L"dbghelp.dll");
        if (lateLoaded != nullptr) {
            fn = reinterpret_cast<MiniDumpWriteDumpFn>(
                GetProcAddress(lateLoaded, "MiniDumpWriteDump"));
        }
    }

    if (fn != nullptr) {
        const HANDLE proc = GetCurrentProcess();
        const DWORD pid = GetCurrentProcessId();

        MiniDumpExceptionInfo mei{};
        mei.ThreadId = threadId;
        mei.ExceptionPointers = info;
        // TRUE = ExceptionPointers 指向的内存就在本进程（我们拷出来的那份副本）。
        // 传 FALSE 时 dbghelp 会去崩溃线程的栈上读原始结构，而它此时正卡在
        // 异常分发里 —— 读不进来，直接失败。
        mei.ClientPointers = clientPointers ? TRUE : FALSE;

        // 三级降级，写出一份就比没有强：
        //   1) 带异常现场 + 线程信息 —— 最理想，VS 打开直接停在崩溃那一行
        //   2) 带异常现场，只取基础信息 —— 老系统的 dbghelp 不认扩展标志
        //   3) 不带异常现场 —— 异常态下读崩溃线程的上下文会失败（实测
        //      ERROR_NOACCESS=998），退到这一档至少能拿到所有线程的调用栈
        if (fn(proc, pid, file, kMiniDumpNormal | kMiniDumpWithThreadInfo, &mei, nullptr,
               nullptr)) {
            dumped = true;
            level = 1;
        } else if (fn(proc, pid, file, kMiniDumpNormal, &mei, nullptr, nullptr)) {
            dumped = true;
            level = 2;
        } else {
            lastErr = GetLastError();
            if (fn(proc, pid, file, kMiniDumpNormal, nullptr, nullptr, nullptr)) {
                dumped = true;
                level = 3;  // 没有异常现场，只有线程栈 —— 够用但不理想
            } else if (lastErr == 0) {
                lastErr = GetLastError();
            }
        }
    }
    if (lateLoaded != nullptr) FreeLibrary(lateLoaded);
    CloseHandle(file);

    if (dumped) {
        g_lastDump = path;
        std::string narrow(path.begin(), path.end());  // 文件名是自己生成的 ASCII，够用
        char buf[512];
        std::snprintf(buf, sizeof(buf), "%s —— 转储已写入：%s", reason, narrow.c_str());
        emit(buf);
        // 第 3 档说明没抓到异常现场：事后排查时要知道这一点，
        // 否则会对着一份"看不出崩在哪"的 dump 白费功夫
        if (level >= 3) {
            emit("注意：这份转储没能写入异常现场（只有各线程的调用栈），"
                 "打开后不会自动停在崩溃那一行。");
        }
        emit("用 Visual Studio 打开这个 .dmp（并指定对应的 .pdb）就能看到崩溃时的调用栈。");
    } else {
        emit(reason);
        char buf[256];
        // 空文件比没文件更误导人，所以失败原因一定要说明白
        std::snprintf(buf, sizeof(buf),
                      "转储失败：dbghelp.dll 不可用或 MiniDumpWriteDump 失败（GetLastError=%lu）。"
                      " 只保留了上面的日志。",
                      static_cast<unsigned long>(lastErr));
        emit(buf);
        // 删掉那个 0 字节的空壳，免得事后误以为有 dump 可看
        DeleteFileW(path.c_str());
    }
    return dumped;
}

/// 交给 dump 线程的请求（静态对象：崩溃时堆分配不一定靠得住）
struct DumpRequest {
    EXCEPTION_POINTERS *info = nullptr;
    std::string reason;
    DWORD threadId = 0;  // 崩溃线程（不是写 dump 的那个线程）
    bool localCopy = false;  // info 是否指向下面的本地副本
};
DumpRequest g_request;

// EXCEPTION_POINTERS 原本在崩溃线程的栈上。dump 线程去读那块内存会被拒绝
// （就是那个 ERROR_NOACCESS=998），所以先在崩溃线程里把它整份拷到这里，
// 再让 MiniDumpWriteDump 以 ClientPointers=TRUE 的方式读本地副本。
EXCEPTION_RECORD g_record{};
CONTEXT g_context{};
EXCEPTION_POINTERS g_pointers{};

/// 唤醒预建的 dump 线程并等它写完。崩溃路径专用：一返回系统就要杀进程。
void requestDumpAndWait(DWORD waitMs) {
    if (g_dumpThread != nullptr && g_dumpEvent != nullptr && g_dumpDoneEvent != nullptr) {
        ResetEvent(g_dumpDoneEvent);
        SetEvent(g_dumpEvent);
        // 必须等它写完再返回：一返回系统就要杀进程了
        WaitForSingleObject(g_dumpDoneEvent, waitMs);
        return;
    }
    // 预建线程没起来（install 阶段就失败的极端情况）：退回老路 ——
    // 现场建一个；还不行就在当前线程上直写（0 字节风险的那条路，保底）。
    HANDLE worker = CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                                     writeMinidump(g_request.info, g_request.reason.c_str(),
                                                   g_request.threadId, g_request.localCopy);
                                     return 0;
                                 },
                                 nullptr, 0, nullptr);
    if (worker != nullptr) {
        WaitForSingleObject(worker, waitMs);
        CloseHandle(worker);
    } else {
        writeMinidump(g_request.info, g_request.reason.c_str(), g_request.threadId,
                      g_request.localCopy);
    }
}

/// 崩溃处理的公共入口：terminate / signal / CRT 处理器都走这里。
/// info 为 nullptr 时只有线程栈那一档。
void handleCrash(EXCEPTION_POINTERS *info, const char *reason) {
    if (g_dumpInProgress.exchange(true)) return;  // 处理崩溃的过程中又崩了：直接让它死
    g_request.reason = reason;
    g_request.threadId = GetCurrentThreadId();  // 此刻还在出事的线程上

    // 趁还在崩溃线程上，把现场整份拷到静态变量里（见 g_pointers 的说明）
    if (info != nullptr && info->ExceptionRecord != nullptr &&
        info->ContextRecord != nullptr) {
        g_record = *info->ExceptionRecord;
        g_context = *info->ContextRecord;
        g_pointers.ExceptionRecord = &g_record;
        g_pointers.ContextRecord = &g_context;
        g_request.info = &g_pointers;
        g_request.localCopy = true;
    } else {
        g_request.info = nullptr;
        g_request.localCopy = false;
    }
    requestDumpAndWait(30000);
}

DWORD WINAPI dumpThreadProc(LPVOID) {
    setThreadName("LRConvert-Dump");
    for (;;) {
        WaitForSingleObject(g_dumpEvent, INFINITE);
        ResetEvent(g_dumpEvent);
        writeMinidump(g_request.info, g_request.reason.c_str(), g_request.threadId,
                      g_request.localCopy);
        SetEvent(g_dumpDoneEvent);
    }
    return 0;
}

DWORD WINAPI watchdogProc(LPVOID) {
    setThreadName("LRConvert-Watchdog");
    for (;;) {
        Sleep(static_cast<DWORD>(kWatchdogIntervalMs));
        if (g_shuttingDown.load(std::memory_order_relaxed)) break;

        const int64_t now = nowMs();
        for (int i = 0; i < kHeartbeatCount; ++i) {
            if (!g_expected[i].load(std::memory_order_relaxed)) continue;
            const int64_t last = g_heartbeatMs[i].load(std::memory_order_relaxed);
            if (last == 0) continue;  // 还没心跳过：刚启动，别误报
            const int64_t staleMs = now - last;
            if (staleMs <= kStaleThresholdMs) {
                // 心跳回来了（可能是卡了一下缓过来的，也可能是又开了一轮新任务）：
                // 把"已报过"清掉，下次真卡死还能再报。
                g_hangReported[i].store(false, std::memory_order_relaxed);
                continue;
            }
            if (g_hangReported[i].exchange(true)) continue;  // 这一轮已经报过了

            char reason[160];
            std::snprintf(reason, sizeof(reason),
                          "疑似卡死：%s 线程 %lld 秒无心跳（进程仍活着，界面无响应）",
                          kHeartbeatNames[i], static_cast<long long>(staleMs / 1000));
            forceWriteDump(reason);
        }
    }
    return 0;
}

LONG WINAPI sehFilter(EXCEPTION_POINTERS *info) {
    if (g_dumpInProgress.exchange(true)) {
        // 处理崩溃的过程中又崩了：别再套一层，直接让它死
        return EXCEPTION_EXECUTE_HANDLER;
    }
    DWORD code = (info != nullptr && info->ExceptionRecord != nullptr)
                     ? info->ExceptionRecord->ExceptionCode
                     : 0;
    char buf[512];
    std::snprintf(buf, sizeof(buf), "进程遇到无法处理的错误：%s（代码 0x%08lX）",
                  exceptionText(code), static_cast<unsigned long>(code));
    g_request.reason = buf;
    g_request.threadId = GetCurrentThreadId();

    // ⚠ 关键：**不能在异常分发回调里直接写 dump**（也不能在这时 CreateThread，
    // 实测可能失败）。标准做法是唤醒预建的 dump 线程：崩溃线程在等待期间被
    // 挂起不动，它的寄存器和栈正好能被 dump 线程原样抓进 .dmp 里。
    if (info != nullptr && info->ExceptionRecord != nullptr &&
        info->ContextRecord != nullptr) {
        g_record = *info->ExceptionRecord;
        g_context = *info->ContextRecord;
        g_pointers.ExceptionRecord = &g_record;
        g_pointers.ContextRecord = &g_context;
        g_request.info = &g_pointers;
        g_request.localCopy = true;
    } else {
        g_request.info = nullptr;
        g_request.localCopy = false;
    }
    requestDumpAndWait(30000);

    // 返回"执行处理器"= 交给系统结束进程。
    // 千万不能返回 CONTINUE_EXECUTION：现场已经坏了，继续跑只会二次崩溃。
    return EXCEPTION_EXECUTE_HANDLER;
}

void onTerminate() {
    handleCrash(nullptr, "未捕获的 C++ 异常导致 std::terminate() —— 常见于异常逃出了线程函数");
    // 默认 terminate 会调 abort()，这里补一次，保证进程真的退出
    std::abort();
}

void onSignal(int sig) {
    const char *name = (sig == SIGABRT)  ? "abort()/qFatal()（SIGABRT）"
                       : (sig == SIGSEGV) ? "段错误（SIGSEGV）"
                       : (sig == SIGFPE)  ? "浮点错误（SIGFPE）"
                       : (sig == SIGILL)  ? "非法指令（SIGILL）"
                                          : "致命信号";
    char buf[512];
    std::snprintf(buf, sizeof(buf), "进程收到%s，即将退出。", name);
    handleCrash(nullptr, buf);
    // 用 _Exit 而不是 abort：避免再触发一轮 signal → 又进这里
    std::_Exit(2);
}

// ---- CRT 层的两条 abort 路径（P0-3）----
//
// Windows 上 CRT 导致进程 abort 的常见两条路没接的话，默认行为都是直接 abort，
// 只能落到 SIGABRT 那条（没有 C++ 异常现场，dump 只有线程栈），而且日志里
// 只有干巴巴的"abort 了"。这里补上原因，再走现有 abort 链。

/// 把宽字符转成窄字符写进 buf（截断也无所谓，诊断用）
void narrowFrom(const wchar_t *src, char *buf, std::size_t size) {
    if (src == nullptr) {
        std::snprintf(buf, size, "(null)");
        return;
    }
    std::size_t i = 0;
    for (; i + 1 < size && src[i] != L'\0'; ++i) {
        buf[i] = (src[i] < 0x80) ? static_cast<char>(src[i]) : '?';
    }
    buf[i] = '\0';
}

void onInvalidParameter(const wchar_t *expression, const wchar_t *function,
                        const wchar_t *file, unsigned int line, uintptr_t /*reserved*/) {
    char expr[128] = {};
    char func[128] = {};
    char fileBuf[128] = {};
    narrowFrom(expression, expr, sizeof(expr));
    narrowFrom(function, func, sizeof(func));
    narrowFrom(file, fileBuf, sizeof(fileBuf));
    char reason[512];
    std::snprintf(reason, sizeof(reason),
                  "CRT 参数校验失败（invalid parameter）：%s"
                  "（函数 %s · %s:%u）—— 常见于错误的 printf 格式串或空指针入参",
                  expr, func, fileBuf, line);
    handleCrash(nullptr, reason);
    // 默认行为就是 abort；我们只是先把原因写进日志和 dump
    std::abort();
}

void onPureCall() {
    // 纯虚函数调用：对象已析构还在用，或跨 DLL 的虚表被踩坏
    handleCrash(nullptr, "纯虚函数调用（pure virtual call）—— 对象已析构后仍被使用，或虚表损坏");
    std::abort();
}

// ---- WER LocalDumps 兜底注册（P1-5）----
//
// SetUnhandledExceptionFilter 理论上覆盖全部 SEH，但实践中存在它够不到的死法
// （处理过程自身栈溢出、堆损坏到连写文件都失败）。让系统级 WER 也留一份：
// 两道转储互不依赖，任一失效另一份还在。用 HKCU —— 不需要管理员权限，
// 对分发到海量用户机器上的软件是很划算的保险。
void installWerLocalDumps(const std::wstring &dumpDir) {
    HKEY key = nullptr;
    const LPCWSTR subKey =
        L"SOFTWARE\\Microsoft\\Windows\\Windows Error Reporting\\LocalDumps\\LRConvert.exe";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subKey, 0, nullptr, 0, KEY_WRITE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS) {
        emit("WER LocalDumps 注册失败（注册表不可写），系统级兜底转储不可用。");
        return;
    }

    const std::wstring folder = dumpDir + L"\\wer";
    CreateDirectoryW(folder.c_str(), nullptr);

    // DumpFolder 用 REG_EXPAND_SZ：WER 会展开 %LOCALAPPDATA% 之类的变量
    RegSetValueExW(key, L"DumpFolder", 0, REG_EXPAND_SZ,
                   reinterpret_cast<const BYTE *>(folder.c_str()),
                   static_cast<DWORD>((folder.size() + 1) * sizeof(wchar_t)));
    const DWORD dumpCount = 5;  // 最多留 5 份，别把用户磁盘塞满
    RegSetValueExW(key, L"DumpCount", 0, REG_DWORD,
                   reinterpret_cast<const BYTE *>(&dumpCount), sizeof(dumpCount));
    const DWORD dumpType = 1;  // 1 = minidump（WER 那份是兜底，不必全量）
    RegSetValueExW(key, L"DumpType", 0, REG_DWORD,
                   reinterpret_cast<const BYTE *>(&dumpType), sizeof(dumpType));

    RegCloseKey(key);
    emit("已注册 WER LocalDumps 兜底转储（logs/crashdumps/wer/，最多 5 份）。");
}

#else

void emit(const char *text) {
    std::fprintf(stderr, "[崩溃] %s\n", text);
    std::fflush(stderr);
}

#endif  // _WIN32

}  // namespace

void installCrashHandler(const std::wstring &dumpDir,
                         std::function<void(const char *)> logSink) {
#ifdef _WIN32
    g_dumpDir = dumpDir;
    g_logSink = std::move(logSink);

    // 0) 先把 dbghelp 挂上：崩溃时进程处于异常状态，那时候再 LoadLibrary
    //    能不能成看运气，提前拿到函数指针最稳。
    //    这里加载失败也没关系，writeMinidump 里还会再兜一次。
    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    if (dbghelp != nullptr) {
        g_miniDumpWriteDump = reinterpret_cast<MiniDumpWriteDumpFn>(
            GetProcAddress(dbghelp, "MiniDumpWriteDump"));
        // 故意不 FreeLibrary：这个句柄要一直留到进程结束
    }

    // 1) 预建 dump 线程 + 看门狗线程（P0-2 / P0-1）。
    //    都建在"安装时"而非"崩溃时"：崩溃路径上只剩填请求 + 发事件。
    if (g_dumpThread == nullptr) {
        g_dumpEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);      // manual-reset
        g_dumpDoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // manual-reset
        if (g_dumpEvent != nullptr && g_dumpDoneEvent != nullptr) {
            g_dumpThread =
                CreateThread(nullptr, 0, &dumpThreadProc, nullptr, 0, nullptr);
        }
        // 看门狗：即使 dump 线程没建成也要起（它还能直接调 forceWriteDump 的兜底路径）
        HANDLE watchdog = CreateThread(nullptr, 0, &watchdogProc, nullptr, 0, nullptr);
        if (watchdog != nullptr) CloseHandle(watchdog);  // 没人 join 它，句柄可以关
    }

    // 2) 主战场：结构化异常（访问违规 / 栈溢出 / 堆损坏……）
    SetUnhandledExceptionFilter(&sehFilter);

    // 3) 未捕获的 C++ 异常：线程函数里漏出来的异常最终都会走到 std::terminate，
    //    它不经过 SEH 过滤器，所以要单独挂一个
    std::set_terminate(&onTerminate);

    // 4) abort() / qFatal() / 断言失败：这些发的是信号，走不到 SEH。
    //
    //    注意这里**只挂 SIGABRT**：访问违规、除零这类在 Windows 上本质是 SEH，
    //    挂了 SIGSEGV/SIGFPE/SIGILL 的 signal handler 反而会被 CRT 抢先接走 ——
    //    那条路拿不到 EXCEPTION_POINTERS，写出来的 dump 没有异常现场，
    //    调用栈指不到崩溃的那一行。交给上面的 sehFilter 才是完整的。
    std::signal(SIGABRT, &onSignal);

    // 5) CRT 层的两条 abort 路径（P0-3）：默认行为都是直接 abort，
    //    不接的话日志和 dump 里只有"abort 了"，没有为什么。
    _set_invalid_parameter_handler(&onInvalidParameter);
    _set_purecall_handler(&onPureCall);

    // 6) WER LocalDumps 兜底（P1-5）：系统级第二道转储
    installWerLocalDumps(dumpDir);
#else
    (void)dumpDir;
    (void)logSink;
#endif
}

std::wstring lastDumpPath() {
#ifdef _WIN32
    return g_lastDump;
#else
    return {};
#endif
}

void heartbeat(Heartbeat which) {
#ifdef _WIN32
    const int idx = static_cast<int>(which);
    if (idx < 0 || idx >= kHeartbeatCount) return;
    g_heartbeatMs[idx].store(nowMs(), std::memory_order_relaxed);
#else
    (void)which;
#endif
}

void setExpected(Heartbeat which, bool expected) {
#ifdef _WIN32
    const int idx = static_cast<int>(which);
    if (idx < 0 || idx >= kHeartbeatCount) return;
    g_expected[idx].store(expected, std::memory_order_relaxed);
    // 重新开始期望时给一个基准心跳，避免"刚开播还没打到第一帧"被误判
    g_heartbeatMs[idx].store(expected ? nowMs() : 0, std::memory_order_relaxed);
#else
    (void)which;
    (void)expected;
#endif
}

void forceWriteDump(const char *reason) {
#ifdef _WIN32
    if (g_dumpDir.empty()) return;
    if (g_dumpInProgress.exchange(true)) return;  // 上一份还没写完：不叠

    g_request.reason = (reason != nullptr) ? reason : "强制转储";
    g_request.threadId = 0;  // 没有 SEH 现场，谁写都行
    g_request.info = nullptr;
    g_request.localCopy = false;
    requestDumpAndWait(60000);  // 卡死现场可能更大，多给点时间
    g_dumpInProgress.store(false, std::memory_order_relaxed);
#else
    (void)reason;
#endif
}

void shutdownDiagnostics() {
#ifdef _WIN32
    g_shuttingDown.store(true, std::memory_order_relaxed);
    // 退出流程里各线程自然停止，不再视为"应该活着"
    for (int i = 0; i < kHeartbeatCount; ++i) {
        g_expected[i].store(false, std::memory_order_relaxed);
    }
#endif
}

// ---- 崩溃后安全模式（P1-8）的依据 ----

bool markCleanExit(const std::wstring &dumpDir) {
    if (dumpDir.empty()) return false;
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir(dumpDir);
    fs::create_directories(dir, ec);
    const fs::path marker = dir / kCleanExitMarker;
    {  // 写个空文件就够 —— 关键的是它的**时间戳**
        std::FILE *f = std::fopen(marker.string().c_str(), "wb");
        if (f == nullptr) return false;
        std::fclose(f);
    }
    return true;
}

bool hadAbnormalExit(const std::wstring &dumpDir, std::wstring *lastDump) {
    namespace fs = std::filesystem;
    if (lastDump != nullptr) lastDump->clear();
    if (dumpDir.empty()) return false;

    std::error_code ec;
    const fs::path dir(dumpDir);
    const fs::path marker = dir / kCleanExitMarker;

    // 找最新的那份 .dmp，同时记下它的修改时间
    fs::file_time_type newestDumpTime{};
    bool haveDump = false;
    if (fs::exists(dir, ec)) {
        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            if (entry.path().extension() != L".dmp") continue;
            const auto t = entry.last_write_time(ec);
            if (ec) continue;
            if (!haveDump || t > newestDumpTime) {
                newestDumpTime = t;
                haveDump = true;
                if (lastDump != nullptr) *lastDump = entry.path().wstring();
            }
        }
    }
    if (!haveDump) return false;  // 从没崩过：第一次跑也是"干净"的

    // 干净退出的标记比最新那份 dump 还新 —— 说明那次崩溃已经处理过了
    if (fs::exists(marker, ec)) {
        const auto mt = fs::last_write_time(marker, ec);
        if (!ec && mt > newestDumpTime) return false;
    }
    return true;
}

}  // namespace lr::platform
