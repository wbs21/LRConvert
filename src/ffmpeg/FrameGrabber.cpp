// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "ffmpeg/FrameGrabber.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace lr {
namespace {

// ---- 损坏数据的容错阈值 ----
//
// 单个坏包就跳过，但连续坏到一定程度要换策略：
//   · kBadRunBeforeSkip：这一串坏包攒到这么长，说明是成片坏的 —— 直接 seek 越过，
//     不再一个一个读。实测"中间被填成 0xFF"的文件会连续几千个包全废，
//     逐包读的话界面要空转好几秒。
//   · kConsecutiveBadLimit：seek 也跳不动时才认命，判定整条流解不动。
//   · kMaxRegionSkips：一次 open 里最多跳这么多次，防止在坏区里来回横跳。
constexpr int kBadRunBeforeSkip = 64;
constexpr int kConsecutiveBadLimit = 300;
constexpr double kSkipSeconds = 1.0;
constexpr int kMaxRegionSkips = 20;

int firstVideoStream(AVFormatContext *fmt) {
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

FrameGrabber::~FrameGrabber() { release(); }

void FrameGrabber::release() {
    if (sws_ != nullptr && api_ != nullptr) {
        api_->sws_freeContext(sws_);
        sws_ = nullptr;
    }
    if (rgb_ != nullptr && api_ != nullptr) api_->av_frame_free(&rgb_);
    if (frame_ != nullptr && api_ != nullptr) api_->av_frame_free(&frame_);
    if (packet_ != nullptr && api_ != nullptr) api_->av_packet_free(&packet_);
    if (codec_ != nullptr && api_ != nullptr) api_->avcodec_free_context(&codec_);
    if (fmt_ != nullptr && api_ != nullptr) api_->avformat_close_input(&fmt_);
    api_ = nullptr;
    videoIdx_ = -1;
    frameHeld_ = false;
    rgbW_ = rgbH_ = 0;
}

void FrameGrabber::close() { release(); }

bool FrameGrabber::open(FfmpegApi &api, const std::string &path, std::string &error) {
    release();
    api_ = &api;

    AVFormatContext *fmt = nullptr;
    int ret = api.avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        error = api.errorText(ret);
        release();
        return false;
    }
    fmt_ = fmt;

    if (api.avformat_find_stream_info(fmt_, nullptr) < 0) {
        // 探测流信息失败不致命，继续
    }

    videoIdx_ = firstVideoStream(fmt_);
    if (videoIdx_ < 0) {
        error = "文件里没有视频流";
        release();
        return false;
    }

    AVStream *stream = fmt_->streams[videoIdx_];
    timeBase_ = av_q2d(stream->time_base);
    if (timeBase_ <= 0.0) timeBase_ = 1.0 / 1000.0;  // 兜底，正常不会走到
    if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
        fps_ = double(stream->avg_frame_rate.num) / double(stream->avg_frame_rate.den);
    }

    const AVCodec *codec = api.avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        error = "找不到对应的视频解码器";
        release();
        return false;
    }

    codec_ = api.avcodec_alloc_context3(codec);
    if (codec_ == nullptr) {
        error = "分配解码上下文失败";
        release();
        return false;
    }
    ret = api.avcodec_parameters_to_context(codec_, stream->codecpar);
    if (ret < 0) {
        error = api.errorText(ret);
        release();
        return false;
    }
    codec_->pkt_timebase = stream->time_base;

    // 错误隐藏：让解码器主动去补残缺的宏块，而不是把整帧判死。
    //
    // GUESS_MVS = 用周边的运动矢量外推补洞，DEBLOCK = 对补出来的区域做去块滤波。
    // 开完之后坏块处常见的是"局部糊一块"，而不是整帧变绿或者干脆整帧丢失。
    //
    // 反面教材：千万不要设 AV_EF_EXPLODE —— 它会让解码器把轻微错误当成致命错误，
    // 本来还能出画面的帧也一并返回失败，画面反而烂得更快。err_recognition 保持
    // 默认的 AV_EF_DEFAULT 就好。
    codec_->error_concealment = FF_EC_GUESS_MVS | FF_EC_DEBLOCK;

    ret = api.avcodec_open2(codec_, codec, nullptr);
    if (ret < 0) {
        error = api.errorText(ret);
        release();
        return false;
    }

    frame_ = api.av_frame_alloc();
    packet_ = api.av_packet_alloc();
    if (frame_ == nullptr || packet_ == nullptr) {
        error = "分配帧/包失败";
        release();
        return false;
    }

    srcW_ = codec_->width;
    srcH_ = codec_->height;
    srcPixFmt_ = codec_->pix_fmt;

    badBlocks_ = 0;
    badRun_ = 0;
    regionSkips_ = 0;
    return true;
}

bool FrameGrabber::skipBrokenRegion() {
    if (fmt_ == nullptr || videoIdx_ < 0 || regionSkips_ >= kMaxRegionSkips) return false;

    // 当前读到哪儿？用解封装层的下一个包的时间戳当基准（拿不到就按上次的位置推）
    const int64_t curTs = (packet_->pts != AV_NOPTS_VALUE)   ? packet_->pts
                          : (packet_->dts != AV_NOPTS_VALUE) ? packet_->dts
                                                             : -1;
    double curSec = -1.0;
    if (curTs != AV_NOPTS_VALUE) curSec = double(curTs) * timeBase_;
    if (curSec < 0.0) return false;  // 时间戳也没有，没法跳，只能老实往下读

    const double target = curSec + kSkipSeconds;
    const int64_t targetTs = static_cast<int64_t>(std::llround(target / timeBase_));
    if (api_->avformat_seek_file(fmt_, videoIdx_, INT64_MIN, targetTs, targetTs, 0) < 0) {
        return false;
    }
    ++regionSkips_;
    flushDecoder();
    return true;
}

/// 把解码器里积压的帧排空（seek 之后必须做，否则会拿到旧位置的画面）
///
/// 注意：这里必须用 avcodec_flush_buffers，不能用"发一个空包"来代替 ——
/// 空包是给**流结束**用的，它会把解码器推进 draining 状态，之后
/// avcodec_send_packet 一律返回 AVERROR_EOF（实测踩过）。
void FrameGrabber::flushDecoder() {
    if (codec_ == nullptr) return;
    api_->avcodec_flush_buffers(codec_);
}

bool FrameGrabber::convertFrame(AVFrame *src, int outW, int outH, double actualSec,
                                Frame &out, std::string &error) {
    const int dstW = (outW > 0) ? outW : src->width;
    const int dstH = (outH > 0) ? outH : src->height;

    // 尺寸兜底：损坏文件或异常分辨率会给出离谱的宽高，
    // 下面 resize(dstW*dstH*3) 要么溢出 size_t、要么直接抛 bad_alloc。
    // 与其让它炸，不如在这里就判掉并返回错误。
    if (dstW <= 0 || dstH <= 0 || dstW > 16384 || dstH > 16384) {
        error = "视频帧尺寸异常：" + std::to_string(dstW) + "x" + std::to_string(dstH);
        return false;
    }
    if (src->width <= 0 || src->height <= 0) {
        error = "解码出的帧为空";
        return false;
    }

    sws_ = api_->sws_getCachedContext(sws_, src->width, src->height,
                                      static_cast<int>(src->format), dstW, dstH,
                                      AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (sws_ == nullptr) {
        error = "创建图像缩放上下文失败";
        return false;
    }

    // RGB 输出帧：尺寸变化时重建（避免每帧都分配）
    if (rgb_ == nullptr || rgbW_ != dstW || rgbH_ != dstH) {
        if (rgb_ != nullptr) api_->av_frame_free(&rgb_);
        rgb_ = api_->av_frame_alloc();
        if (rgb_ == nullptr) {
            error = "分配 RGB 帧失败";
            return false;
        }
        rgb_->format = AV_PIX_FMT_RGB24;
        rgb_->width = dstW;
        rgb_->height = dstH;
        if (api_->av_frame_get_buffer(rgb_, 0) < 0) {
            error = "分配 RGB 缓冲失败";
            return false;
        }
        rgbW_ = dstW;
        rgbH_ = dstH;
    }

    if (api_->sws_scale(sws_, src->data, src->linesize, 0, src->height, rgb_->data,
                        rgb_->linesize) <= 0) {
        error = "图像格式转换失败";
        return false;
    }

    // sws 的输出行可能带对齐填充，这里拷成紧密排列，方便上层直接构造 QImage
    out.rgb.resize(static_cast<size_t>(dstW) * dstH * 3);
    const int rowBytes = dstW * 3;
    for (int y = 0; y < dstH; ++y) {
        std::memcpy(out.rgb.data() + static_cast<size_t>(y) * rowBytes,
                    rgb_->data[0] + static_cast<size_t>(y) * rgb_->linesize[0],
                    rowBytes);
    }
    out.width = dstW;
    out.height = dstH;
    out.actualSec = actualSec;
    return true;
}

bool FrameGrabber::seekTo(double tSec, std::string &error) {
    if (fmt_ == nullptr || codec_ == nullptr) {
        error = "取帧器尚未打开";
        return false;
    }
    if (tSec < 0.0) tSec = 0.0;

    // seek 到目标之前的那个关键帧（avformat_seek_file 的默认行为）
    const int64_t targetTs = static_cast<int64_t>(std::llround(tSec / timeBase_));
    const int ret = api_->avformat_seek_file(fmt_, videoIdx_, INT64_MIN, targetTs, targetTs, 0);
    if (ret < 0) {
        error = api_->errorText(ret);
        return false;
    }
    flushDecoder();
    return true;
}

void FrameGrabber::dropFrame() {
    if (frameHeld_) {
        api_->av_frame_unref(frame_);
        frameHeld_ = false;
    }
}

FrameGrabber::DecodeResult FrameGrabber::decodeNext(double &secOut, std::string &error) {
    dropFrame();  // 上一帧没取走就先释放，别泄漏解码帧
    if (fmt_ == nullptr || codec_ == nullptr) {
        error = "取帧器尚未打开";
        return DecodeResult::Error;
    }

    while (api_->av_read_frame(fmt_, packet_) >= 0) {
        if (packet_->stream_index != videoIdx_) {
            api_->av_packet_unref(packet_);
            continue;
        }
        const int sendRet = api_->avcodec_send_packet(codec_, packet_);
        api_->av_packet_unref(packet_);
        if (sendRet < 0) {
            // 单个坏包很正常（损坏文件里到处都是），跳过它就是了。
            ++badBlocks_;
            ++badRun_;

            // 退让：攒到一定数量说明这一段是成片坏的，一个一个包读下去纯属空转。
            // 直接 seek 越过这一片，比继续读快得多，也不会让 CPU 长时间跑满。
            if (badRun_ == kBadRunBeforeSkip) {
                if (skipBrokenRegion()) {
                    badRun_ = 0;
                    continue;
                }
            }
            // 跳不动（seek 失败 / 超过跳过次数上限 / 没有时间戳）才会走到这里：
            // 连续几百个全失败，说明这条流真的解不动了，那就认了。
            if (badRun_ >= kConsecutiveBadLimit) {
                error = "视频数据损坏，无法继续解码";
                return DecodeResult::Error;
            }
            continue;
        }
        badRun_ = 0;  // 解出一个好包，这串坏包就算过去了

        const int recvRet = api_->avcodec_receive_frame(codec_, frame_);
        if (recvRet < 0) {
            // EAGAIN：这个包还不够解出一帧（B 帧要等后续包），继续读下一个
            continue;
        }
        const int64_t pts = (frame_->pts != AV_NOPTS_VALUE) ? frame_->pts : frame_->pkt_dts;
        const double frameSec = (pts != AV_NOPTS_VALUE) ? double(pts) * timeBase_ : -1.0;
        secOut = frameSec;
        heldSec_ = frameSec;
        frameHeld_ = true;
        return DecodeResult::Ok;
    }
    return DecodeResult::EndOfStream;  // 文件正常读完 —— 不是错误
}

bool FrameGrabber::takeFrame(int outW, int outH, Frame &out, std::string &error) {
    if (!frameHeld_) return false;
    const bool ok = convertFrame(frame_, outW, outH, heldSec_, out, error);
    dropFrame();
    return ok;
}

FrameGrabber::DecodeResult FrameGrabber::grabNext(int outW, int outH, Frame &out,
                                                  std::string &error) {
    double sec = 0.0;
    const DecodeResult r = decodeNext(sec, error);
    if (r != DecodeResult::Ok) return r;
    if (!takeFrame(outW, outH, out, error)) return DecodeResult::Error;
    return DecodeResult::Ok;
}

bool FrameGrabber::grabAt(double tSec, int outW, int outH, Frame &out, std::string &error) {
    if (!seekTo(tSec, error)) return false;

    // 从关键帧开始顺序解码，直到拿到 >= 目标时间的那一帧
    const double tolerance = (fps_ > 0.0) ? (0.5 / fps_) : 0.02;
    bool haveFallback = false;
    Frame fallback;
    Frame frame;

    // 有硬上限：seek 失败或时间戳乱掉时，这个循环会一路解到文件尾
    // （长视频几万帧），界面不卡但 CPU 会长时间跑满、风扇狂转。
    // 超过限额就用已经拿到的最后一帧，好过一直转下去。
    for (int guard = 0; guard < 1500; ++guard) {
        const DecodeResult r = grabNext(outW, outH, frame, error);
        // Error 也要 break：定位取帧没有"接着往下碰运气"的余地，
        // 能拿到 fallback 就用 fallback，拿不到就算失败（上层显示占位图）
        if (r != DecodeResult::Ok) break;
        if (frame.actualSec >= 0.0 && (frame.actualSec + tolerance) >= tSec) {
            out = std::move(frame);
            return true;
        }
        // 还没到目标：记住这一帧，万一后面取不到就退而求其次
        fallback = std::move(frame);
        haveFallback = true;
    }

    if (haveFallback) {
        out = std::move(fallback);
        return true;
    }
    error = "没有解出任何画面";
    return false;
}

}  // namespace lr
