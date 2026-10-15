// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// 崩溃转储 —— 进程要死的时候，至少留下一份 .dmp。
//
// 为什么必须有这个：
//   "程序突然没了"是最难查的一类问题。日志只能记到崩溃前的最后一行，
//   但**崩溃在哪一行、哪个线程、调用栈是什么**，日志说不出来。
//   而访问违规、栈溢出、堆损坏这类错误根本不走 C++ 异常 —— 线程里的
//   try-catch 和 main() 里的 catch(...) 全都拦不住，进程直接被系统杀掉。
//
//   装了 SetUnhandledExceptionFilter 之后，Windows 在"要杀掉进程之前"会
//   先回调我们一次，这时把现场写成 minidump，事后用 Visual Studio 直接打开
//   就能看到崩溃时的完整调用栈（配合 .pdb 还能定位到源码行）。
//
// 覆盖范围：
//   · SEH 结构化异常（访问违规 / 栈溢出 / 除零 / 堆损坏）← 最主要的战场
//   · std::terminate（有未捕获的 C++ 异常时，运行时在杀进程前会走这里）
//   · abort() / qFatal() 之类的 SIGABRT
//
// 设计取舍：
//   · dbghelp.dll 用运行时 LoadLibrary 拿，不写进导入表 —— 与本项目加载
//     libav* 的做法一致，免得 MinGW 的 import lib 与系统 dbghelp 版本打架。
//   · 不依赖 Qt：崩溃时 Qt 的状态不能保证，这里只用 Win32 + CRT。
//     日志通过回调交回上层（main.cpp 里接的是 qCritical，能落盘）。
// ---------------------------------------------------------------------------

#include <functional>
#include <string>

namespace lr::platform {

/// logSink：崩溃前最后几句话交给上层去记（会写进日志文件）。
/// 传 nullptr 也可以，那就只往 stderr 打。
void installCrashHandler(const std::wstring &dumpDir,
                         std::function<void(const char *)> logSink = nullptr);

/// 最近一次写出的 dmp 路径（没崩溃过就是空串）
std::wstring lastDumpPath();

}  // namespace lr::platform
