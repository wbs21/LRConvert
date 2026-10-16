// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "ffmpeg/FfObjects.h"

// 只有这个文件需要 libav* 的完整类型（释放函数要访问结构体字段）
#include <libavformat/avformat.h>

namespace lr {
namespace {

void freeFrame(void *obj, FfmpegApi *api) {
    auto *p = static_cast<AVFrame *>(obj);
    if (api != nullptr) api->av_frame_free(&p);
}

void freePacket(void *obj, FfmpegApi *api) {
    auto *p = static_cast<AVPacket *>(obj);
    if (api != nullptr) api->av_packet_free(&p);
}

void freeCodecContext(void *obj, FfmpegApi *api) {
    auto *p = static_cast<AVCodecContext *>(obj);
    if (api != nullptr) api->avcodec_free_context(&p);
}

void freeInputContext(void *obj, FfmpegApi *api) {
    auto *p = static_cast<AVFormatContext *>(obj);
    if (api != nullptr) api->avformat_close_input(&p);
}

/// 输出容器：先关自定义 I/O（mp4 的 moov 在 trailer 里，不关的话数据可能
/// 还在缓冲里），再释放上下文。与 TranscodeTask/RemuxTask 原来的收尾一致。
void freeOutputContext(void *obj, FfmpegApi *api) {
    auto *p = static_cast<AVFormatContext *>(obj);
    if (api == nullptr || p == nullptr) return;
    if (!(p->oformat->flags & AVFMT_NOFILE) && p->pb != nullptr) {
        api->avio_closep(&p->pb);
    }
    api->avformat_free_context(p);
}

void freeSws(void *obj, FfmpegApi *api) {
    auto *p = static_cast<SwsContext *>(obj);
    if (api != nullptr) api->sws_freeContext(p);
}

void freeSwr(void *obj, FfmpegApi *api) {
    auto *p = static_cast<SwrContext *>(obj);
    if (api != nullptr) api->swr_free(&p);
}

}  // namespace

FfFrame wrapFrame(FfmpegApi &api, AVFrame *raw) { return FfFrame(raw, api, &freeFrame); }
FfPacket wrapPacket(FfmpegApi &api, AVPacket *raw) {
    return FfPacket(raw, api, &freePacket);
}
FfCodec wrapCodec(FfmpegApi &api, AVCodecContext *raw) {
    return FfCodec(raw, api, &freeCodecContext);
}
FfInput wrapInput(FfmpegApi &api, AVFormatContext *raw) {
    return FfInput(raw, api, &freeInputContext);
}
FfOutput wrapOutput(FfmpegApi &api, AVFormatContext *raw) {
    return FfOutput(raw, api, &freeOutputContext);
}
FfSws wrapSws(FfmpegApi &api, SwsContext *raw) { return FfSws(raw, api, &freeSws); }
FfSwr wrapSwr(FfmpegApi &api, SwrContext *raw) { return FfSwr(raw, api, &freeSwr); }

void closeFormatFile(AVFormatContext *oc, FfmpegApi *api) {
    if (oc == nullptr || api == nullptr) return;
    if (!(oc->oformat->flags & AVFMT_NOFILE) && oc->pb != nullptr) {
        api->avio_closep(&oc->pb);
    }
}

}  // namespace lr
