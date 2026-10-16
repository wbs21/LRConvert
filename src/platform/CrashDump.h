// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// 崩溃转储 + 卡死看门狗 + WER 兜底（稳定性改进方案 P0-1 / P0-2 / P0-3 / P1-5）
//
// 覆盖范围：
//   · SEH 结构化异常（访问违规 / 栈溢出 / 堆损坏）← 主战场
//   · std::terminate（有未捕获的 C++ 异常）
//   · abort() / qFatal() / SIGABRT，含 CRT 层的两条 abort 路径
//     （_set_invalid_parameter_handler / _set_purecall_handler，P0-3）
//   · **进程活着但界面无响应**（卡死）：看门狗线程盯各业务线程的心跳，
//     15 秒无心跳就 forceWriteDump —— "卡死"和"死了"用同一条链路排查
//   · 系统级 WER LocalDumps 兜底注册（P1-5）：我们的过滤器够不到的死法
//     （处理过程自身栈溢出、堆损坏到写文件都失败），系统再留一份
//
// 设计取舍（沿用既有，新增的保持同风格）：
//   · dbghelp.dll 运行时 LoadLibrary，不进导入表（与 FfmpegApi 一致）
//   · 不依赖 Qt：崩溃时 Qt 状态不可信，只用 Win32 + CRT
//   · dump 线程**安装时就建好**、挂起等待（P0-2）：崩溃路径上只剩
//     "填静态请求 + SetEvent" 两个不会失败的动作，把 CreateThread 这种
//     重操作完全移出异常态 —— 实测异常态里建线程可能失败，失败后退回在
//     崩溃线程上直写，正是产出 0 字节 dump 的那条路
// ---------------------------------------------------------------------------

#include <functional>
#include <string>

namespace lr::platform {

/// logSink：崩溃前最后几句话交给上层去记（会写进日志文件）。
/// 传 nullptr 也可以，那就只往 stderr 打。
/// 幂等：重复调用只会覆盖 logSink，不会重复建线程。
void installCrashHandler(const std::wstring &dumpDir,
                         std::function<void(const char *)> logSink = nullptr);

/// 最近一次写出的 dmp 路径（没崩溃过就是空串）
std::wstring lastDumpPath();

// ---- 心跳（P0-1 看门狗的数据源）----

enum class Heartbeat { Gui, Playback, Waveform };

/// 各线程在自己的循环里调用（一次 relaxed store，开销可忽略）。
/// GUI 线程由界面层用定时器驱动；播放/波形线程在各自循环里驱动。
void heartbeat(Heartbeat which);

/// 标记某个子系统"现在应该活着"。false 时看门狗不再盯它 ——
/// 播放没在跑、波形没在算的时候，没有心跳是正常的，不能误报成卡死。
void setExpected(Heartbeat which, bool expected);

/// 不依赖 SEH 现场的强制转储：进程活着但要留一份现场时用
/// （看门狗发现卡死；也开放给任何"我想 dump"的场景）。
/// 只写各线程调用栈那一档（没有异常现场可言）。
void forceWriteDump(const char *reason);

/// 正常退出前调用：停掉看门狗，避免退出流程被误判成卡死。
/// 崩溃路径（进程要死了）不需要调用。
void shutdownDiagnostics();

// ---- 崩溃后安全模式（P1-8）的判定依据 ----

/// 写/刷新"干净退出"标记。正常退出时调用一次。
///
/// 原理：崩溃从来不及更新这个标记，所以拿它的时间戳与 dump 目录里最新
/// .dmp 的时间戳比，就能判断"上一次是不是没走到正常退出就结束了"。
/// 比去解析系统事件日志、比在崩溃路径里做额外 I/O 都稳妥得多 ——
/// 崩溃那一刻唯一可信的动作越少越好。
bool markCleanExit(const std::wstring &dumpDir);

/// 上一次运行是否异常结束（留下 .dmp 且之后没再干净退出过）。
/// lastDump 可空；非空则返回最近那份 dmp 的完整路径。
/// 从没崩过（目录里一份 dmp 都没有）时返回 false —— 首次运行不算异常。
bool hadAbnormalExit(const std::wstring &dumpDir, std::wstring *lastDump = nullptr);

}  // namespace lr::platform
