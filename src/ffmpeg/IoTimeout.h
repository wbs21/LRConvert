// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// FFmpeg I/O 超时（稳定性改进方案 P0-4）
//
// 现状问题：AVFormatContext 没设中断回调。用户从网络盘/共享盘打开文件而目标机
// 离线或链路慢时，av_read_frame 会无限期阻塞 —— 表现是"导入卡死、界面无响应"，
// 且所有既有机制（含看门狗）只能事后 dump，不能防止。
//
// 做法（FFmpeg 官方对网络输入的标准用法）：
//   1) openInputWithTimeout()：所有 avformat_open_input 的统一入口，
//      自建 context 并挂上 interrupt_callback；
//   2) armTimeout()/disarmTimeout()：读包循环在每次 av_read_frame 前后调用，
//      给"这一步 I/O"上刹车。回调与调用在同一线程（阻塞读就跑在这个线程上），
//      所以截止时间用 thread_local 存即可 —— 没有跨线程生命周期问题。
//
// 边界（如实记录）：
//   · 本地文件读不走协议层的轮询，回调管不到；UNC/网络盘的真实阻塞发生在
//     操作系统 SMB 重定向器里，也管不到。那种情况由两件事兜：
//       a) open 前对网络路径先做一次"带超时的可达性探测"（本文件实现），
//         读不出来就直接快速失败，不等 SMB 自己的几十秒超时；
//       b) 播放/导入中途卡死由看门狗（CrashDump）事后留 dump + 日志。
//   · 本程序的界面只能选本地文件（文件对话框），网络协议路径目前只是纵深防御。
// ---------------------------------------------------------------------------

#include <string>

#include "ffmpeg/FfmpegApi.h"

struct AVFormatContext;

namespace lr::io {

/// 打开输入（统一入口）。失败时 error 有原因、返回 nullptr。
/// 成功时 context 已可读；调用方负责 find_stream_info 与关闭
/// （关闭建议用 lr::wrapInput 的 RAII 包装）。
AVFormatContext *openInputWithTimeout(FfmpegApi &api, const std::string &path,
                                      int timeoutSec, std::string &error);

/// 给当前线程的**下一步** FFmpeg I/O 上刹车：timeoutMs 内无进展就中断
/// （调用返回错误码，而不是永久挂住）。0 = 不设限。
/// 用法：arm → 调用 → disarm，必须成对。
void armTimeout(int timeoutMs);
void disarmTimeout();

/// 默认超时：打开 15 秒（冷启动的网络盘也够了，本地是毫秒级）、
/// 单次读包 30 秒（正常读一个包不可能这么久）。
constexpr int kOpenTimeoutSec = 15;
constexpr int kReadTimeoutMs = 30 * 1000;

}  // namespace lr::io
