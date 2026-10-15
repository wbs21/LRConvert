// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// 动态符号表 —— 新增/减少一个 av_* 函数，只改这一个文件。
//
// 设计要点：
//   1. 本头文件**故意不 include 任何 FFmpeg 头文件**，只做前向声明。
//      这样整个工程除了少数需要访问结构体字段的 .cpp 外，都不依赖 av*.h，
//      也就杜绝了"顺手直接调用 av_*"导致链接期重新引入 libav* 依赖的风险。
//   2. 每一行的格式：X(库枚举名, 返回类型, 函数名, (参数列表))
//   3. 参数列表里遇到 FFmpeg 的结构体/枚举，一律用前向声明的指针或 int 代替。
//   4. 唯一例外是 AVRational：它按值传参，无法用不完整类型声明。这里定义
//      一个布局完全一致的 FfRational（int num; int den;）——ABI 相同、名字不同，
//      因此即使 .cpp 同时 include 了真正的 avutil.h 也不会冲突。
//   5. **一条一行，不要为了排版折行**。宏续行一旦漏写反斜杠，错误信息会指到
//      毫不相关的地方（"expected constructor before '('"），极难排查。
// ---------------------------------------------------------------------------

#include <cstdarg>
#include <cstddef>
#include <cstdint>

// ---- 前向声明（避免引入 FFmpeg 头文件） ----
struct AVCodec;              // libavcodec
struct AVCodecContext;       // libavcodec
struct AVCodecParameters;    // libavcodec
struct AVFrame;              // libavutil
struct AVPacket;             // libavcodec
struct AVInputFormat;        // libavformat
struct AVOutputFormat;       // libavformat
struct AVFormatContext;      // libavformat
struct AVStream;             // libavformat
struct AVIOContext;          // libavformat
struct AVDictionary;         // libavutil
struct AVChannelLayout;      // libavutil（FFmpeg 7+ 新声道布局）
struct SwsContext;           // libswscale
struct SwsFilter;            // libswscale
struct SwrContext;           // libswresample

/// 与 AVRational 二进制布局一致（见文件头第 4 点）
struct FfRational {
    int num;
    int den;
};

/// av_log_set_callback 的回调签名（对应 FFmpeg 的 void (*)(void*, int,
/// const char*, va_list)）。这里必须用真正的 va_list —— 它在 x86_64 MinGW 上
/// 是数组类型，用别的类型代替再 cast 回来是不成立的。
/// <cstdarg> 是标准头文件，不影响"本文件不引入 FFmpeg 头文件"的原则。
using FfLogCallback = void (*)(void *, int, const char *, va_list);

#define FFMPEG_FN_LIST(X) \
    X(AVUTIL, const char *, av_version_info, (void)) \
    X(AVUTIL, unsigned, avutil_version, (void)) \
    X(AVUTIL, void, av_log_set_level, (int)) \
    X(AVUTIL, void, av_log_set_callback, (FfLogCallback)) \
    X(AVUTIL, int, av_strerror, (int, char *, size_t)) \
    X(AVUTIL, int, av_dict_set, (AVDictionary **, const char *, const char *, int)) \
    X(AVUTIL, void, av_dict_free, (AVDictionary **)) \
    X(AVUTIL, int64_t, av_rescale_q, (int64_t, FfRational, FfRational)) \
    X(AVUTIL, AVFrame *, av_frame_alloc, (void)) \
    X(AVUTIL, void, av_frame_free, (AVFrame **)) \
    X(AVUTIL, void, av_frame_unref, (AVFrame *)) \
    X(AVUTIL, int, av_frame_get_buffer, (AVFrame *, int)) \
    X(AVUTIL, int, av_frame_make_writable, (AVFrame *)) \
    X(AVUTIL, int, av_opt_set, (void *, const char *, const char *, int)) \
    X(AVUTIL, int, av_opt_set_int, (void *, const char *, int64_t, int)) \
    X(AVUTIL, void, av_channel_layout_default, (AVChannelLayout *, int)) \
    X(AVUTIL, int, av_samples_alloc, (uint8_t **, int *, int, int, int, int)) \
    X(AVUTIL, void, av_freep, (void *)) \
    X(AVCODEC, unsigned, avcodec_version, (void)) \
    X(AVCODEC, const char *, avcodec_configuration, (void)) \
    X(AVCODEC, const char *, avcodec_license, (void)) \
    X(AVCODEC, const AVCodec *, av_codec_iterate, (void **)) \
    X(AVCODEC, int, av_codec_is_encoder, (const AVCodec *)) \
    X(AVCODEC, int, av_codec_is_decoder, (const AVCodec *)) \
    X(AVCODEC, int, avcodec_parameters_copy, (AVCodecParameters *, const AVCodecParameters *)) \
    X(AVCODEC, AVPacket *, av_packet_alloc, (void)) \
    X(AVCODEC, void, av_packet_free, (AVPacket **)) \
    X(AVCODEC, void, av_packet_unref, (AVPacket *)) \
    X(AVCODEC, void, av_packet_rescale_ts, (AVPacket *, FfRational, FfRational)) \
    X(AVCODEC, const AVCodec *, avcodec_find_decoder, (int)) \
    X(AVCODEC, const AVCodec *, avcodec_find_encoder_by_name, (const char *)) \
    X(AVCODEC, AVCodecContext *, avcodec_alloc_context3, (const AVCodec *)) \
    X(AVCODEC, void, avcodec_free_context, (AVCodecContext **)) \
    X(AVCODEC, int, avcodec_parameters_to_context, (AVCodecContext *, const AVCodecParameters *)) \
    X(AVCODEC, int, avcodec_parameters_from_context, (AVCodecParameters *, const AVCodecContext *)) \
    X(AVCODEC, int, avcodec_open2, (AVCodecContext *, const AVCodec *, AVDictionary **)) \
    X(AVCODEC, void, avcodec_flush_buffers, (AVCodecContext *)) \
    X(AVCODEC, int, avcodec_send_packet, (AVCodecContext *, const AVPacket *)) \
    X(AVCODEC, int, avcodec_receive_frame, (AVCodecContext *, AVFrame *)) \
    X(AVCODEC, int, avcodec_send_frame, (AVCodecContext *, const AVFrame *)) \
    X(AVCODEC, int, avcodec_receive_packet, (AVCodecContext *, AVPacket *)) \
    X(AVFORMAT, unsigned, avformat_version, (void)) \
    X(AVFORMAT, const AVInputFormat *, av_demuxer_iterate, (void **)) \
    X(AVFORMAT, const AVOutputFormat *, av_muxer_iterate, (void **)) \
    X(AVFORMAT, void, avformat_network_init, (void)) \
    X(AVFORMAT, AVFormatContext *, avformat_alloc_context, (void)) \
    X(AVFORMAT, int, avformat_open_input, (AVFormatContext **, const char *, const AVInputFormat *, AVDictionary **)) \
    X(AVFORMAT, int, avformat_find_stream_info, (AVFormatContext *, AVDictionary **)) \
    X(AVFORMAT, void, avformat_close_input, (AVFormatContext **)) \
    X(AVFORMAT, void, avformat_free_context, (AVFormatContext *)) \
    X(AVFORMAT, int, avformat_alloc_output_context2, (AVFormatContext **, AVOutputFormat *, const char *, const char *)) \
    X(AVFORMAT, AVStream *, avformat_new_stream, (AVFormatContext *, const AVCodec *)) \
    X(AVFORMAT, int, avformat_write_header, (AVFormatContext *, AVDictionary **)) \
    X(AVFORMAT, int, av_interleaved_write_frame, (AVFormatContext *, AVPacket *)) \
    X(AVFORMAT, int, av_write_trailer, (AVFormatContext *)) \
    X(AVFORMAT, int, av_read_frame, (AVFormatContext *, AVPacket *)) \
    X(AVFORMAT, int, avformat_seek_file, (AVFormatContext *, int, int64_t, int64_t, int64_t, int)) \
    X(AVFORMAT, const AVInputFormat *, av_find_input_format, (const char *)) \
    X(AVFORMAT, int, avio_open, (AVIOContext **, const char *, int)) \
    X(AVFORMAT, int, avio_closep, (AVIOContext **)) \
    X(SWSCALE, unsigned, swscale_version, (void)) \
    X(SWSCALE, SwsContext *, sws_getContext, (int, int, int, int, int, int, int, SwsFilter *, SwsFilter *, const double *)) \
    X(SWSCALE, void, sws_freeContext, (SwsContext *)) \
    X(SWSCALE, int, sws_scale, (SwsContext *, const uint8_t *const *, const int *, int, int, uint8_t *const *, const int *)) \
    X(SWSCALE, SwsContext *, sws_getCachedContext, (SwsContext *, int, int, int, int, int, int, int, SwsFilter *, SwsFilter *, const double *)) \
    X(SWRESAMPLE, unsigned, swresample_version, (void)) \
    X(SWRESAMPLE, SwrContext *, swr_alloc, (void)) \
    X(SWRESAMPLE, int, swr_alloc_set_opts2, (SwrContext **, const AVChannelLayout *, int, int, const AVChannelLayout *, int, int, int, void *)) \
    X(SWRESAMPLE, int, swr_init, (SwrContext *)) \
    X(SWRESAMPLE, void, swr_free, (SwrContext **)) \
    X(SWRESAMPLE, int, swr_convert, (SwrContext *, uint8_t **, int, const uint8_t **, int))

// 注：FFmpeg 6 起 swr_alloc_set_opts（旧 uint64 channel_layout API）已废弃，
// 这份构建也没有导出它 —— 音频重采样只能用接受 AVChannelLayout 的 opts2 版本。
