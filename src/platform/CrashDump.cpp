// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "platform/CrashDump.h"

// 用 _WIN32 而不是 Q_OS_WIN：崩溃时 Qt 的状态不可信，这个模块连 QtGlobal 都不该依赖
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>

namespace lr::platform {
namespace {

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
bool g_inHandler = false;  // 防重入：写出 dump 的过程本身也可能再崩一次

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

/// 真正写 dump。info 可以为 nullptr（std::terminate / abort 这类没有 SEH 现场的）
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

void handleCrash(EXCEPTION_POINTERS *info, const char *reason) {
    if (g_inHandler) return;  // 处理崩溃的过程中又崩了：别再套一层，直接让它死
    g_inHandler = true;
    writeMinidump(info, reason);
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

DWORD WINAPI dumpThreadProc(LPVOID) {
    writeMinidump(g_request.info, g_request.reason.c_str(), g_request.threadId,
                  g_request.localCopy);
    return 0;
}

LONG WINAPI sehFilter(EXCEPTION_POINTERS *info) {
    DWORD code = (info != nullptr && info->ExceptionRecord != nullptr)
                     ? info->ExceptionRecord->ExceptionCode
                     : 0;
    char buf[512];
    std::snprintf(buf, sizeof(buf), "进程遇到无法处理的错误：%s（代码 0x%08lX）",
                  exceptionText(code), static_cast<unsigned long>(code));

    if (!g_inHandler) {
        g_inHandler = true;

        // ⚠ 关键：**不能在异常分发回调里直接写 dump**。
        //
        // 这个回调跑在崩溃线程的异常帧上，进程仍处在"异常未处理"的状态。
        // 实测在这个状态下 LoadLibrary(dbghelp) + MiniDumpWriteDump 会失败 ——
        // 文件被创建出来却是 0 字节，等于白忙一场。
        //
        // 标准做法是另起一个线程去写：崩溃线程在这里被挂起不动，
        // 它的寄存器和栈正好能被 dump 线程原样抓进 .dmp 里。
        g_request.reason = buf;
        g_request.threadId = GetCurrentThreadId();  // 此刻还在崩溃线程上

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
        HANDLE worker = CreateThread(nullptr, 0, &dumpThreadProc, nullptr, 0, nullptr);
        if (worker != nullptr) {
            // 必须等它写完再返回：一返回系统就要杀进程了
            WaitForSingleObject(worker, 30000);
            CloseHandle(worker);
        } else {
            // 连线程都起不来，至少再试一次（这时就在崩溃线程上，读原结构可以）
            writeMinidump(info, buf, GetCurrentThreadId(), false);
        }
    }

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

    // 1) 主战场：结构化异常（访问违规 / 栈溢出 / 堆损坏……）
    SetUnhandledExceptionFilter(&sehFilter);

    // 2) 未捕获的 C++ 异常：线程函数里漏出来的异常最终都会走到 std::terminate，
    //    它不经过 SEH 过滤器，所以要单独挂一个
    std::set_terminate(&onTerminate);

    // 3) abort() / qFatal() / 断言失败：这些发的是信号，走不到 SEH
    //
    // 注意这里**只挂 SIGABRT**：访问违规、除零这类在 Windows 上本质是 SEH，
    // 挂了 SIGSEGV/SIGFPE/SIGILL 的 signal handler 反而会被 CRT 抢先接走 ——
    // 那条路拿不到 EXCEPTION_POINTERS，写出来的 dump 没有异常现场，
    // 调用栈指不到崩溃的那一行。交给上面的 sehFilter 才是完整的。
    std::signal(SIGABRT, &onSignal);
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

}  // namespace lr::platform
