// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// libav* 对象的 RAII 包装（稳定性改进方案 P2-11）
//
// 由来：FrameGrabber::release() 那种"手工逐项释放"的模式，要求每个错误分支
// 都保证调到释放 —— 每次新增一种资源都是新的泄漏/UAF 机会；任务线程里再抛出
// 个异常（P2-9 收敛的那种），goto/return 路径外的清理干脆整段跳过。
// 这里统一成"出了作用域自动释放"，异常路径也不用再逐个补清理。
//
// 设计约束：
//   · 头文件只做前向声明 —— 不 include avformat.h 等，维持"FFmpeg ABI 隔离"
//     的既有架构（只有个别 .cpp 见完整类型）。释放逻辑全部在 FfObjects.cpp。
//   · 释放走 FfmpegApi 的函数指针表，与"业务代码只能通过 FfmpegApi 调用
//     libav*"的硬约束一致。
//   · FfPtr<T> 本身不区分类型语义，靠工厂函数绑定对应的释放函数：
//     wrapInput 用 avformat_close_input，wrapOutput 用 avio_closep+free。
//
// 用法：
//   lr::FfFrame frame = lr::wrapFrame(api, nullptr);   // 空壳
//   frame.reset(api.av_frame_alloc());                 // 接管裸指针
//   frame->width                                       // operator->
//   if (frame) { ... }                                 // operator bool
//   api.avcodec_send_packet(codec.get(), pkt.get());   // get() 取裸指针
// ---------------------------------------------------------------------------

#include <cstddef>

#include "ffmpeg/FfmpegApi.h"

struct AVFrame;
struct AVPacket;
struct AVCodecContext;
struct AVFormatContext;
struct SwsContext;
struct SwrContext;

namespace lr {

/// 释放函数签名：(对象指针, FfmpegApi*)。由工厂函数按类型绑定。
using FfFreeFn = void (*)(void *obj, FfmpegApi *api);

template <typename T>
class FfPtr {
public:
    FfPtr() = default;

    /// 接管裸指针。freeFn 为 null 表示"空壳"（不释放任何东西）——
    /// 用于先默认构造、open() 成功后再接管的成员变量模式。
    FfPtr(T *raw, FfmpegApi &api, FfFreeFn freeFn)
        : api_(&api), freeFn_(freeFn), ptr_(raw) {}

    ~FfPtr() { reset(); }

    FfPtr(const FfPtr &) = delete;
    FfPtr &operator=(const FfPtr &) = delete;

    FfPtr(FfPtr &&other) noexcept
        : api_(other.api_), freeFn_(other.freeFn_), ptr_(other.ptr_) {
        other.ptr_ = nullptr;
        other.freeFn_ = nullptr;
    }

    FfPtr &operator=(FfPtr &&other) noexcept {
        if (this != &other) {
            reset();
            api_ = other.api_;
            freeFn_ = other.freeFn_;
            ptr_ = other.ptr_;
            other.ptr_ = nullptr;
            other.freeFn_ = nullptr;
        }
        return *this;
    }

    T *get() const { return ptr_; }
    T *operator->() const { return ptr_; }
    T &operator*() const { return *ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    /// 与 nullptr 的比较：让 `if (fmt_ != nullptr)` 这种老写法继续可用。
    /// 只提供 nullptr 一侧，不提供 FfPtr↔FfPtr —— 同一份资源不该有两个所有者。
    friend bool operator==(const FfPtr &a, std::nullptr_t) noexcept {
        return a.ptr_ == nullptr;
    }
    friend bool operator!=(const FfPtr &a, std::nullptr_t) noexcept {
        return a.ptr_ != nullptr;
    }

    /// 释放当前对象并接管 p（p 为 nullptr 时只释放）
    void reset(T *p = nullptr) {
        T *old = ptr_;
        ptr_ = p;
        if (old != nullptr && freeFn_ != nullptr) freeFn_(old, api_);
    }

    /// 交出所有权（不释放）。用于"把指针交给另一个也懂释放的所有者"
    /// （如 sws_getCachedContext 会接管旧上下文）。
    T *release() {
        T *p = ptr_;
        ptr_ = nullptr;
        return p;
    }

private:
    FfmpegApi *api_ = nullptr;
    FfFreeFn freeFn_ = nullptr;
    T *ptr_ = nullptr;
};

using FfFrame = FfPtr<AVFrame>;
using FfPacket = FfPtr<AVPacket>;
using FfCodec = FfPtr<AVCodecContext>;
using FfInput = FfPtr<AVFormatContext>;   ///< avformat_close_input 语义
using FfOutput = FfPtr<AVFormatContext>;  ///< avio_closep + avformat_free_context 语义
using FfSws = FfPtr<SwsContext>;
using FfSwr = FfPtr<SwrContext>;

// ---- 工厂函数（实现在 FfObjects.cpp，那里才见 FFmpeg 完整类型）----
FfFrame wrapFrame(FfmpegApi &api, AVFrame *raw);
FfPacket wrapPacket(FfmpegApi &api, AVPacket *raw);
FfCodec wrapCodec(FfmpegApi &api, AVCodecContext *raw);
FfInput wrapInput(FfmpegApi &api, AVFormatContext *raw);
FfOutput wrapOutput(FfmpegApi &api, AVFormatContext *raw);
FfSws wrapSws(FfmpegApi &api, SwsContext *raw);
FfSwr wrapSwr(FfmpegApi &api, SwrContext *raw);

/// 输出容器专用：提前关闭文件句柄（数据落盘、缓冲刷盘）。
///
/// 原子落盘（临时文件 + rename）要求"先关文件、再 rename"，而上下文的释放
/// 要等 rename 结果出来之后 —— 所以关闭文件这一下必须能单独触发。
/// 重复调用安全（avio_closep 会把 pb 置空）。
void closeFormatFile(AVFormatContext *oc, FfmpegApi *api);

}  // namespace lr
