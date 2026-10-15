// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "FfmpegApi.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <unistd.h>
#endif

namespace {

// ---------------- FFmpeg 内部日志的桥接 ----------------

void (*g_logSink)(int, const char *) = nullptr;
int g_logLevel = 24;  // AV_LOG_WARNING，与 load() 里 av_log_set_level 保持一致
int g_logCount = 0;   // 已产生的 FFmpeg 消息条数（限流用）

void ffmpegLogBridge(void *, int level, const char *fmt, va_list vl) {
    // ⚠ 这一行不能省：FFmpeg 的 av_vlog() 拿到回调就**直接调用**，
    // 级别过滤只存在于它那个默认回调里。换成自定义回调后如果不自己判断，
    // 硬件探测那堆 AV_LOG_INFO 的 "Loaded lib: nvcuda.dll" 会全涌进来，
    // 一次启动就能刷出几万行（实测 56083 条），把日志文件撑爆。
    if (level > g_logLevel) return;

    char buf[1024];
    std::vsnprintf(buf, sizeof(buf), fmt, vl);
    buf[sizeof(buf) - 1] = '\0';
    // FFmpeg 的消息自带结尾换行，去掉它（交给日志那层统一加）
    std::size_t n = std::strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';

    // 限流：一个坏文件会源源不断地报同一个错，几万条同样的日志既没参考价值
    // 又拖慢程序。前 100 条照实记，之后每 100 条汇总一次。
    const int seq = ++g_logCount;
    char out[1152];
    if (seq <= 100) {
        std::snprintf(out, sizeof(out), "%s", buf);
    } else if (seq % 100 == 0) {
        std::snprintf(out, sizeof(out), "%s  …（此后已抑制 %d 条同类 FFmpeg 消息）",
                      buf, seq - 100);
    } else {
        return;
    }

    if (g_logSink != nullptr) {
        g_logSink(level, out);
    } else {
        std::fprintf(stderr, "[FFmpeg] %s\n", out);
        std::fflush(stderr);
    }
}

// ---------------- 平台无关的动态库装载封装 ----------------

#ifdef _WIN32
std::wstring utf8ToWide(const std::string &utf8) {
    int wide = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (wide <= 0) return {};
    std::wstring out(static_cast<size_t>(wide), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &out[0], wide);
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

std::string wideToUtf8(const wchar_t *wide) {
    int need = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, &out[0], need, nullptr, nullptr);
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}
#endif

#ifdef _WIN32
/// 把目录注册进进程的 DLL 搜索路径（配合 LOAD_LIBRARY_SEARCH_USER_DIRS 使用）。
///
/// 这些 API 在 Windows 8 / Server 2012 才出现（Win7 需 KB2533623），且 MinGW 的
/// 头文件受 _WIN32_WINNT 控制，因此运行时解析函数指针，编译期不强依赖 SDK 版本。
using FnAddDllDirectory = void *(WINAPI *)(const wchar_t *);
using FnRemoveDllDirectory = int(WINAPI *)(void *);

FnAddDllDirectory addDllDirectoryFn() {
    static FnAddDllDirectory fn = [] {
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        return k32 ? reinterpret_cast<FnAddDllDirectory>(
                         reinterpret_cast<void *>(
                             GetProcAddress(k32, "AddDllDirectory")))
                   : nullptr;
    }();
    return fn;
}

FnRemoveDllDirectory removeDllDirectoryFn() {
    static FnRemoveDllDirectory fn = [] {
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        return k32 ? reinterpret_cast<FnRemoveDllDirectory>(
                         reinterpret_cast<void *>(
                             GetProcAddress(k32, "RemoveDllDirectory")))
                   : nullptr;
    }();
    return fn;
}

/// 已注册的搜索目录句柄，卸载时清理。
std::vector<void *> &registeredDirs() {
    static std::vector<void *> cookies;
    return cookies;
}

/// 让「FFmpeg 动态库所在目录」也参与其依赖的搜索。
///
/// 背景（这里踩过坑）：FFmpeg 若动态链接了第三方库，avcodec-63.dll 的导入表里会
/// 写着 libx264-165.dll / libx265-217.dll / libaom.dll … 一大串。用绝对路径调用
/// LoadLibrary 时，Windows 在解析**导入表里的依赖**走的是标准搜索顺序
///（应用程序目录 → 系统目录 → 当前目录 → PATH），**不会**自动去「被加载 DLL
/// 所在目录」找。结果就是第三方依赖全都找不到，LoadLibrary 直接失败并报
/// error 126（ERROR_MOD_NOT_FOUND）——哪怕这些 DLL 就在同一个文件夹里。
///
/// 两道保险：
///   1. LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR —— 把被加载 DLL 的目录临时插入搜索列表，
///      专用于解析它的依赖链（含递归依赖）；
///   2. AddDllDirectory + LOAD_LIBRARY_SEARCH_USER_DIRS —— 把 FFmpeg 目录注册为
///      进程级搜索目录，防止某些间接依赖没有继承到上面的临时目录。
void registerSearchDir(const std::string &dir) {
    FnAddDllDirectory add = addDllDirectoryFn();
    if (!add) return;
    std::wstring wdir = utf8ToWide(dir);
    if (wdir.empty()) return;
    if (void *cookie = add(wdir.c_str())) registeredDirs().push_back(cookie);
}

void unregisterSearchDirs() {
    FnRemoveDllDirectory remove = removeDllDirectoryFn();
    if (!remove) return;
    for (void *cookie : registeredDirs()) remove(cookie);
    registeredDirs().clear();
}
#endif  // _WIN32

void *openLibrary(const std::string &path) {
#ifdef _WIN32
    // 路径可能含中文，统一按 UTF-8 → UTF-16 转换后调用 LoadLibraryW
    std::wstring wpath = utf8ToWide(path);
    if (wpath.empty()) return nullptr;

    // 首选：带目录搜索标志的扩展加载，见 registerSearchDir 的注释
    HMODULE h = LoadLibraryExW(wpath.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR
                                   | LOAD_LIBRARY_SEARCH_USER_DIRS
                                   | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (h) return static_cast<void *>(h);
    // 回退：Windows 7 且未装 KB2533623 时不支持上述标志，退回普通加载
    return static_cast<void *>(LoadLibraryW(wpath.c_str()));
#else
    return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
#endif
}

void *findSymbol(void *handle, const char *name) {
    if (!handle) return nullptr;
#ifdef _WIN32
    return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return dlsym(handle, name);
#endif
}

void closeLibrary(void *handle) {
    if (!handle) return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

// ---------------- 路径与候选文件名 ----------------

std::string executableDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, buf, MAX_PATH) == 0) return {};
    std::string path = wideToUtf8(buf);
    auto slash = path.find_last_of("\\/");
    return slash == std::string::npos ? std::string{} : path.substr(0, slash);
#else
    char buf[4096]{};
    ssize_t n = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return {};
    std::string path(buf, static_cast<size_t>(n));
    auto slash = path.find_last_of('/');
    return slash == std::string::npos ? std::string{} : path.substr(0, slash);
#endif
}

std::vector<std::string> searchDirs() {
    std::vector<std::string> dirs;
    if (const char *env = std::getenv("LRCONVERT_FFMPEG_DIR")) dirs.emplace_back(env);
    std::string exe = executableDir();
    if (!exe.empty()) {
        dirs.push_back(exe);
        dirs.push_back(exe + "/ffmpeg");
    }
    dirs.emplace_back(".");  // 当前工作目录（便于从构建目录直接跑）
    return dirs;
}

/// 按 stem（如 "avcodec"）生成候选文件名。
/// 关键点：**不写死版本号** —— 先通配枚举 avcodec-*.dll，再回退版本区间，
/// 这样 FFmpeg 大版本升级（63 → 64）时代码不用改。
std::vector<std::string> candidateNames(const std::string &stem) {
    std::vector<std::string> names;

#ifdef _WIN32
    // 1) 通配枚举：avcodec-*.dll
    for (const std::string &dir : searchDirs()) {
        WIN32_FIND_DATAW data{};
        std::wstring pattern = utf8ToWide(dir + "/" + stem + "-*.dll");
        HANDLE h = FindFirstFileW(pattern.c_str(), &data);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            std::string file = wideToUtf8(data.cFileName);
            if (!file.empty()) names.push_back(dir + "/" + file);
        } while (FindNextFileW(h, &data));
        FindClose(h);
        if (!names.empty()) break;
    }
    // 2) 版本号回退（从高到低）
    for (int v = 99; v >= 50; --v) {
        for (const std::string &dir : searchDirs())
            names.push_back(dir + "/" + stem + "-" + std::to_string(v) + ".dll");
    }
    for (const std::string &dir : searchDirs()) names.push_back(dir + "/" + stem + ".dll");
#else
    for (int v = 99; v >= 50; --v) {
        for (const std::string &dir : searchDirs())
            names.push_back(dir + "/lib" + stem + ".so." + std::to_string(v));
    }
    for (const std::string &dir : searchDirs()) {
        names.push_back(dir + "/lib" + stem + ".so");
        names.push_back(dir + "/lib" + stem + ".dylib");
    }
#endif
    return names;
}

struct LibDesc {
    FfmpegLib id;
    const char *stem;
};

constexpr LibDesc kLibs[] = {
    {FfmpegLib::AVUTIL, "avutil"},     {FfmpegLib::AVCODEC, "avcodec"},
    {FfmpegLib::AVFORMAT, "avformat"}, {FfmpegLib::SWSCALE, "swscale"},
    {FfmpegLib::SWRESAMPLE, "swresample"},
};

std::string decodeVersion(unsigned v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u", (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
    return buf;
}

}  // namespace

// ---------------------------------------------------------------------------

FfmpegApi &FfmpegApi::instance() {
    static FfmpegApi api;
    return api;
}

template <typename F>
bool FfmpegApi::bindOne(F &fn, void *handle, const char *symbol, std::string &error) {
    void *addr = findSymbol(handle, symbol);
    if (!addr) {
        error = std::string("动态库加载成功但缺少符号：") + symbol
              + "（FFmpeg 大版本可能已变更，请检查 FfmpegSymbols.h）";
        return false;
    }
    fn = reinterpret_cast<F>(addr);
    return true;
}

bool FfmpegApi::load(std::string &error) {
    if (loaded_) return true;

    const auto t0 = std::chrono::steady_clock::now();

#ifdef _WIN32
    // 把候选目录注册为进程级 DLL 搜索目录。FFmpeg 动态链接的第三方库
    //（libx264-165.dll / libx265-217.dll / libaom.dll …）必须能被加载器找到，
    // 否则 LoadLibrary 会以 error 126 失败。详见 registerSearchDir 的注释。
    for (const std::string &dir : searchDirs()) registerSearchDir(dir);
#endif

    // 1) 逐个装载 5 个动态库
    for (const LibDesc &desc : kLibs) {
        bool ok = false;
#ifdef _WIN32
        DWORD lastError = 0;
#endif
        for (const std::string &path : candidateNames(desc.stem)) {
            void *h = openLibrary(path);
            if (h) {
                handles_[static_cast<int>(desc.id)] = h;
                // 记录所在目录，便于日志排错
                auto slash = path.find_last_of("\\/");
                dir_ = slash == std::string::npos ? std::string{"."} : path.substr(0, slash);
                ok = true;
                break;
            }
#ifdef _WIN32
            lastError = GetLastError();
#endif
        }
        if (!ok) {
            error = std::string("找不到动态库 ") + desc.stem
                  + "（已搜索：程序目录、程序目录/ffmpeg、LRCONVERT_FFMPEG_DIR、当前目录）";
#ifdef _WIN32
            // 126 = ERROR_MOD_NOT_FOUND：库本身在，但它依赖的某个 DLL 找不到。
            // 这是部署时最常见的问题（第三方依赖没跟着一起复制），单独提示一句。
            if (lastError == 126) {
                error += "。库文件已找到，但它依赖的其他 DLL 缺失或不在同一目录"
                         "（Windows 错误 126）";
            } else if (lastError != 0) {
                error += "。Windows 错误码：" + std::to_string(lastError);
            }
#endif
            unload();
            return false;
        }
    }

    // 2) 绑定全部符号
#define LR_BIND_FN(lib, ret, name, args)                                              \
    if (!bindOne(this->name, handles_[static_cast<int>(FfmpegLib::lib)], #name, error)) { \
        unload();                                                                     \
        return false;                                                                 \
    }
    FFMPEG_FN_LIST(LR_BIND_FN)
#undef LR_BIND_FN

    // 3) 初始化（日志级别压到 warning，避免 stderr 刷屏；网络组件可选）
    av_log_set_level(g_logLevel = 24);  // AV_LOG_WARNING（桥接里也要用，故存一份）
    // 把 FFmpeg 的报错接到我们自己的日志上：解码器抱怨了什么、
    // 哪个流有问题，都会进程序日志文件，而不是飘在看不见的 stderr 里
    av_log_set_callback(ffmpegLogBridge);
    avformat_network_init();

    loaded_ = true;
    loadMs_ = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count());
    return true;
}

void FfmpegApi::unload() {
    for (void *&h : handles_) {
        closeLibrary(h);
        h = nullptr;
    }
#define LR_CLEAR_FN(lib, ret, name, args) this->name = nullptr;
    FFMPEG_FN_LIST(LR_CLEAR_FN)
#undef LR_CLEAR_FN
#ifdef _WIN32
    unregisterSearchDirs();
#endif
    loaded_ = false;
    loadMs_ = -1;
}

std::string FfmpegApi::summary() const {
    if (!loaded_) return "FFmpeg 未加载";
    std::string text = "FFmpeg ";
    if (av_version_info) text += av_version_info();
    text += "（avcodec " + decodeVersion(avcodec_version())
          + " / avformat " + decodeVersion(avformat_version())
          + " / avutil " + decodeVersion(avutil_version()) + "）";
    if (loadMs_ >= 0) text += " · 运行时加载 " + std::to_string(loadMs_) + " ms";
    return text;
}

std::string FfmpegApi::errorText(int code) const {
    if (!loaded_ || !av_strerror) return "FFmpeg 未加载，无法解析错误码 " + std::to_string(code);
    char buf[256]{};
    if (av_strerror(code, buf, sizeof(buf)) < 0) return "libav 错误码 " + std::to_string(code);
    return std::string(buf);
}

void FfmpegApi::setLogSink(void (*sink)(int, const char *)) {
    g_logSink = sink;  // 匿名 namespace 里的全局，dlopen 前后设置都行
}
