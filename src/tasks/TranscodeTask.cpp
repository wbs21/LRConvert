// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "TranscodeTask.h"

#include "core/PathUtil.h"
#include "ffmpeg/FfObjects.h"
#include "ffmpeg/IoTimeout.h"
#include "ffmpeg/FfmpegApi.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/opt.h>
#include <libavutil/rational.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <QCoreApplication>
#include <QDateTime>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace lr {
namespace {

constexpr int kUsRate = 1000000;

FfRational rat(AVRational r) { return FfRational{r.num, r.den}; }

int64_t toUs(FfmpegApi &api, AVRational tb, int64_t value) {
    return api.av_rescale_q(value, rat(tb), FfRational{1, kUsRate});
}

/// 偶数对齐：yuv420p 之类的色度子采样格式要求宽高都是偶数，
/// 奇数会让 avcodec_open2 直接失败（"width not divisible by 2"）。
/// 这也是 Python 版 SCALE_FILTERS 里那串 trunc(iw/2)*2 的全部用意。
int evenDown(int v) { return v <= 0 ? 0 : (v / 2) * 2; }

/// 节流器：避免每帧都往 GUI 发信号
class Throttle {
public:
    bool ready(int permille) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - last_ < kIntervalMs) return false;
        if (std::llabs(permille - lastPermille_) < 4) return false;
        last_ = now;
        lastPermille_ = permille;
        return true;
    }

private:
    static constexpr qint64 kIntervalMs = 250;
    qint64 last_ = 0;
    int lastPermille_ = -10000;
};

// ---------------------------------------------------------------------------
// 视频缩放器：swscale 的薄封装（对应原来的 -vf scale=... + -pix_fmt）
// ---------------------------------------------------------------------------
class Scaler {
public:
    Scaler(FfmpegApi &api, AVPixelFormat target) : api_(api), target_(target) {}

    /// 转换到目标像素格式 / 尺寸。返回内部持有的帧，下次调用即失效。
    AVFrame *convert(AVFrame *src, int dstW, int dstH) {
        if (!dst_) {
            dst_ = api_.av_frame_alloc();
            if (!dst_) return nullptr;
        } else {
            api_.av_frame_unref(dst_);
        }
        dst_->format = target_;
        dst_->width = dstW;
        dst_->height = dstH;
        if (api_.av_frame_get_buffer(dst_, 0) < 0) return nullptr;

        ctx_ = api_.sws_getCachedContext(
            ctx_, src->width, src->height,
            static_cast<AVPixelFormat>(src->format), dstW, dstH, target_,
            SWS_BICUBIC, nullptr, nullptr, nullptr);
        if (!ctx_) return nullptr;

        if (api_.sws_scale(ctx_, src->data, src->linesize, 0, src->height,
                           dst_->data, dst_->linesize) != dstH) {
            return nullptr;
        }
        return dst_;
    }

    ~Scaler() {
        if (ctx_) api_.sws_freeContext(ctx_);
        if (dst_) api_.av_frame_free(&dst_);
    }

private:
    FfmpegApi &api_;
    AVPixelFormat target_;
    SwsContext *ctx_ = nullptr;
    AVFrame *dst_ = nullptr;
};

// ---------------------------------------------------------------------------
// 音频重采样器：swresample + 输出整形缓冲
//
// swr_convert 出来的样本数是不确定的（内部有插值和延迟），而 AAC 编码器
// 一次只接受固定 frame_size（1024）个样本，所以中间必须垫一层缓冲：
// 攒够一帧再送出去 —— 等价于原来 ffmpeg 内部 AVAudioFifo 的角色。
// ---------------------------------------------------------------------------
class Resampler {
public:
    explicit Resampler(FfmpegApi &api) : api_(api) {}

    bool init(const AVChannelLayout &inLayout, AVSampleFormat inFormat, int inRate,
              std::string &error) {
        outRate_ = inRate;
        outFormat_ = AV_SAMPLE_FMT_FLTP;  // ffmpeg 内置 aac 编码器只认这一种
        channels_ = inLayout.nb_channels;
        if (channels_ <= 0) {
            error = "音轨声道数为 0";
            return false;
        }

        // 输入布局可能是多层表达（如 5.0(side)），先规整成标准布局，
        // 否则 swr 初始化会失败
        AVChannelLayout def{};
        api_.av_channel_layout_default(&def, channels_);
        outLayout_ = def.nb_channels == channels_ ? def : inLayout;

        const int ret = api_.swr_alloc_set_opts2(&ctx_, &outLayout_, outFormat_,
                                                 outRate_, &inLayout, inFormat,
                                                 inRate, 0, nullptr);
        if (ret < 0 || !ctx_) {
            error = "无法初始化音频重采样器：" + api_.errorText(ret);
            return false;
        }
        if (api_.swr_init(ctx_) < 0) {
            error = "swr_init 失败";
            return false;
        }

        tmpSamples_ = 8192;
        uint8_t *buf = nullptr;
        const int bytes = api_.av_samples_alloc(&buf, nullptr, channels_,
                                                tmpSamples_, outFormat_, 0);
        if (bytes < 0 || !buf) {
            error = "无法分配重采样输出缓冲";
            return false;
        }
        tmpBuf_ = buf;
        const int planeBytes = bytes / channels_;  // planar：各声道等分
        for (int i = 0; i < channels_; ++i) tmpPtrs_[i] = buf + planeBytes * i;
        bytesPerSample_ = planeBytes / tmpSamples_;
        return true;
    }

    bool push(AVFrame *frame, std::string &error) {
        uint8_t *in[AV_NUM_DATA_POINTERS]{};
        for (int i = 0; i < AV_NUM_DATA_POINTERS; ++i) in[i] = frame->data[i];
        const int got = api_.swr_convert(ctx_, tmpPtrs_, tmpSamples_,
                                         const_cast<const uint8_t **>(in),
                                         frame->nb_samples);
        if (got < 0) {
            error = "音频重采样失败：" + api_.errorText(got);
            return false;
        }
        return append(got);
    }

    /// 冲刷 swr 内部缓冲（输入结束时调用一次）
    bool flush(std::string &error) {
        const int got = api_.swr_convert(ctx_, tmpPtrs_, tmpSamples_, nullptr, 0);
        if (got < 0) {
            error = "冲刷重采样器失败：" + api_.errorText(got);
            return false;
        }
        return append(got);
    }

    int pendingSamples() const { return pendingSamples_; }
    int channels() const { return channels_; }
    int sampleRate() const { return outRate_; }
    const AVChannelLayout &layout() const { return outLayout_; }
    AVSampleFormat format() const { return outFormat_; }

    /// 取出 frameSize 个样本组成一帧；不足则返回 nullptr
    AVFrame *take(int frameSize) {
        if (pendingSamples_ < frameSize) return nullptr;
        if (!frame_) {
            frame_ = api_.av_frame_alloc();
            if (!frame_) return nullptr;
        } else {
            api_.av_frame_unref(frame_);
        }
        frame_->format = outFormat_;
        frame_->sample_rate = outRate_;
        frame_->ch_layout = outLayout_;
        frame_->nb_samples = frameSize;
        if (api_.av_frame_get_buffer(frame_, 0) < 0) return nullptr;

        const size_t chunk = static_cast<size_t>(frameSize) * bytesPerSample_;
        for (int c = 0; c < channels_; ++c) {
            const uint8_t *src = pending_.data() + static_cast<size_t>(c) *
                                                       pendingSamples_ *
                                                       bytesPerSample_;
            std::memcpy(frame_->data[c], src, chunk);
        }
        return frame_;
    }

    /// 丢弃队列头部已消费的 frameSize 个样本
    void consume(int frameSize) {
        if (pendingSamples_ <= frameSize) {
            pending_.clear();
            pendingSamples_ = 0;
            return;
        }
        const int remain = pendingSamples_ - frameSize;
        const size_t chunk = static_cast<size_t>(frameSize) * bytesPerSample_;
        std::vector<uint8_t> next;
        next.reserve(static_cast<size_t>(remain) * bytesPerSample_ * channels_);
        for (int c = 0; c < channels_; ++c) {
            const uint8_t *p = pending_.data() + static_cast<size_t>(c) *
                                                     pendingSamples_ *
                                                     bytesPerSample_ + chunk;
            next.insert(next.end(), p,
                        p + static_cast<size_t>(remain) * bytesPerSample_);
        }
        pending_.swap(next);
        pendingSamples_ = remain;
    }

    ~Resampler() {
        // av_samples_free 这份构建没有导出，它内部也就是 av_freep 而已
        if (tmpBuf_) api_.av_freep(&tmpBuf_);
        if (ctx_) api_.swr_free(&ctx_);
        if (frame_) api_.av_frame_free(&frame_);
    }

private:
    bool append(int got) {
        if (got > 0) {
            for (int c = 0; c < channels_; ++c) {
                pending_.insert(pending_.end(), tmpPtrs_[c],
                                tmpPtrs_[c] + got * bytesPerSample_);
            }
            pendingSamples_ += got;
        }
        return true;
    }

    FfmpegApi &api_;
    SwrContext *ctx_ = nullptr;
    AVChannelLayout outLayout_{};
    AVSampleFormat outFormat_ = AV_SAMPLE_FMT_FLTP;
    int outRate_ = 0;
    int channels_ = 0;
    int tmpSamples_ = 0;
    int bytesPerSample_ = 4;
    uint8_t *tmpBuf_ = nullptr;
    uint8_t *tmpPtrs_[AV_NUM_DATA_POINTERS]{};

    std::vector<uint8_t> pending_;
    int pendingSamples_ = 0;
    AVFrame *frame_ = nullptr;
};

/// 分辨率规范键（pathutil::scaleFilters 的键，与语言无关）-> 目标尺寸。
/// 与 Python 版 SCALE_FILTERS 一一对应。未知键按"原始大小"处理
/// （只做偶数对齐）——宁可保守，也不要因为一个陌生值就把画面缩没。
void resolveScale(const std::string &key, int srcW, int srcH, int &outW,
                  int &outH) {
    if (key == "1920*1080") {
        outW = 1920;
        outH = 1080;
    } else if (key == "1280*720") {
        outW = 1280;
        outH = 720;
    } else if (key == "quarter") {
        outW = evenDown(srcW / 4);
        outH = evenDown(srcH / 4);
    } else if (key == "half") {
        outW = evenDown(srcW / 2);
        outH = evenDown(srcH / 2);
    } else {  // "original" 及未知项：仅做偶数对齐
        outW = evenDown(srcW);
        outH = evenDown(srcH);
    }
}

/// ffmpeg 的 "-level 4.1" 的真实取值是 41 —— 直接把 "4.1" 丢进
/// av_opt_set_int 会因为不是整数而失败，所以这里自己换算。
int levelValue(const std::string &level) {
    try {
        return static_cast<int>(std::lround(std::stod(level) * 10));
    } catch (...) {
        return 0;
    }
}

}  // namespace

QString TranscodeTask::title() const { return QString::fromStdString(spec_.title); }

bool TranscodeTask::run(TaskSignals &sig) {
    FfmpegApi &api = FfmpegApi::instance();

    auto say = [&sig](const QString &text,
                      const QString &color = lr::logcolor::kInfo) {
        emit sig.log(text, color);
    };

    // libav* 对象全部 RAII 化（P2-11）：出了作用域自动释放，
    // 异常路径（下面的 try/catch 收敛的那种）不用再逐个补清理
    lr::FfInput ic;
    lr::FfOutput oc;
    lr::FfCodec vDec;
    lr::FfCodec vEnc;
    lr::FfCodec aDec;
    lr::FfCodec aEnc;
    lr::FfPacket pkt;
    lr::FfPacket outPkt;
    lr::FfFrame frame;
    Scaler *scaler = nullptr;    // 这两个是自带析构的 C++ 包装，保持原样
    Resampler *resampler = nullptr;
    AVStream *vOut = nullptr;
    AVStream *aOut = nullptr;
    int vIdx = -1;
    int aIdx = -1;
    int64_t totalUs = 0;
    int64_t doneUs = 0;
    int64_t vAutoPts = 0;
    int64_t aOutSamples = 0;
    int audioFrameSize = 1024;
    int dstW = 0;
    int dstH = 0;
    bool ok = false;
    /// 「文件要不要留下」与「任务算不算成功」是两件事：
    /// 手动停止时两者分叉（任务失败，但已经写好的那半截文件要保留）。
    bool keepOutput = false;
    bool inputEof = false;
    std::string stopReason;
    /// 原子落盘（P2-9）：实际写到"输出路径 + .LRConvert-tmp"，完成并关闭后
    /// 才 rename 到最终路径。崩溃/断电最多留下一个临时文件，最终路径要么
    /// 完整、要么不存在 —— 不会出现半个坏文件盖掉旧文件的情况。
    std::string tempPath;

    // 下面四个 lambda 必须定义在第一个 goto 之前：它们捕获的变量此刻还是
    // 空值，但因为是按引用捕获，真正用到时（运行时）已经赋好了。

    /// 把编码好的包取出来写进容器
    auto pumpVideo = [&]() -> bool {
        while (true) {
            const int r = api.avcodec_receive_packet(vEnc.get(), outPkt.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
            if (r < 0) return false;
            outPkt->stream_index = vOut->index;
            api.av_packet_rescale_ts(outPkt.get(), rat(vEnc->time_base),
                                     rat(vOut->time_base));
            const int w = api.av_interleaved_write_frame(oc.get(), outPkt.get());
            api.av_packet_unref(outPkt.get());
            if (w < 0) return false;
        }
    };

    auto pumpAudio = [&]() -> bool {
        while (true) {
            const int r = api.avcodec_receive_packet(aEnc.get(), outPkt.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
            if (r < 0) return false;
            outPkt->stream_index = aOut->index;
            api.av_packet_rescale_ts(outPkt.get(), rat(aEnc->time_base),
                                     rat(aOut->time_base));
            const int w = api.av_interleaved_write_frame(oc.get(), outPkt.get());
            api.av_packet_unref(outPkt.get());
            if (w < 0) return false;
        }
    };

    /// 把视频解码器里已经就绪的帧全部取出、缩放、送去编码
    auto drainVideo = [&]() -> bool {
        while (true) {
            const int r = api.avcodec_receive_frame(vDec.get(), frame.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
            if (r < 0) {
                stopReason = "视频解码失败：" + api.errorText(r);
                return false;
            }
            const AVRational inTb = ic->streams[vIdx]->time_base;
            const int64_t srcPts = frame->best_effort_timestamp;
            if (srcPts != AV_NOPTS_VALUE) {
                const int64_t us = toUs(api, inTb, srcPts);
                if (us > doneUs) doneUs = us;
            }
            AVFrame *dst = scaler->convert(frame.get(), dstW, dstH);
            if (!dst) {
                stopReason = "画面缩放失败";
                return false;
            }
            // pts 换算到编码器时间基准；源时间戳缺失时退化为顺序计数，
            // 保证输出仍然单调。
            dst->pts = srcPts != AV_NOPTS_VALUE
                           ? api.av_rescale_q(srcPts, rat(inTb),
                                              rat(vEnc->time_base))
                           : vAutoPts++;
            const int s = api.avcodec_send_frame(vEnc.get(), dst);
            if (s < 0 || !pumpVideo()) {
                stopReason = "视频编码失败";
                return false;
            }
            api.av_frame_unref(frame.get());
        }
    };

    /// 把音频解码器里已经就绪的帧全部取出、重采样、按帧喂给 AAC 编码器
    auto drainAudio = [&]() -> bool {
        while (true) {
            const int r = api.avcodec_receive_frame(aDec.get(), frame.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
            if (r < 0) {
                stopReason = "音频解码失败：" + api.errorText(r);
                return false;
            }
            std::string err;
            if (!resampler->push(frame.get(), err)) {
                stopReason = err;
                return false;
            }
            api.av_frame_unref(frame.get());

            while (AVFrame *af = resampler->take(audioFrameSize)) {
                af->pts = api.av_rescale_q(
                    aOutSamples, FfRational{1, resampler->sampleRate()},
                    rat(aEnc->time_base));
                aOutSamples += audioFrameSize;
                const int s = api.avcodec_send_frame(aEnc.get(), af);
                if (s < 0 || !pumpAudio()) {
                    stopReason = "音频编码失败";
                    return false;
                }
                resampler->consume(audioFrameSize);
            }
        }
    };

    // ---- 以下主体整体包在 try 里（P2-9）----
    // 任务线程是 std::thread 语义：异常逃出 = std::terminate = 进程秒死。
    // 收敛成"该任务失败"，让批处理决定继续还是中止。goto done 会跳出本
    // try 块（合法：向外跳，块内对象的析构照常执行），最终收尾在 done:。
    try {

    if (!api.isLoaded()) {
        say(QCoreApplication::translate("lr::TranscodeTask", "✗ FFmpeg 动态库尚未加载完成，无法编码。"),
            lr::logcolor::kError);
        return false;
    }

    say(QCoreApplication::translate("lr::TranscodeTask", "▶ 开始编码：%1").arg(QString::fromStdString(spec_.title)),
        lr::logcolor::kStep);

    // 输入与输出同一个文件直接拒绝：原子落盘也救不回来（rename 目标正被
    // 输入句柄占着），而且会把用户的源文件覆盖掉
    if (lr::pathutil::sameFilePath(spec_.input, spec_.output)) {
        say(QCoreApplication::translate("lr::TranscodeTask", "✗ 输出文件不能与输入文件相同。"),
            lr::logcolor::kError);
        return false;
    }

    int ret = 0;

    // ---------- 1. 打开输入、找出视频/音频轨 ----------
    // 统一入口：带 I/O 超时（P0-4）
    {
        std::string openError;
        AVFormatContext *raw =
            lr::io::openInputWithTimeout(api, spec_.input, lr::io::kOpenTimeoutSec, openError);
        if (raw == nullptr) {
            say(QCoreApplication::translate("lr::TranscodeTask", "✗ 无法打开输入文件：%1")
                    .arg(QString::fromStdString(openError)),
                lr::logcolor::kError);
            return false;
        }
        ic = lr::wrapInput(api, raw);
    }
    ret = api.avformat_find_stream_info(ic.get(), nullptr);
    if (ret < 0) {
        say(QCoreApplication::translate("lr::TranscodeTask", "✗ 无法读取媒体信息：%1").arg(api.errorText(ret)),
            lr::logcolor::kError);
        goto done;
    }
    for (unsigned i = 0; i < ic->nb_streams; ++i) {
        AVCodecParameters *par = ic->streams[i]->codecpar;
        if (par->codec_type == AVMEDIA_TYPE_VIDEO && vIdx < 0) {
            vIdx = static_cast<int>(i);
        } else if (par->codec_type == AVMEDIA_TYPE_AUDIO && aIdx < 0 &&
                   spec_.audioKbps > 0) {
            aIdx = static_cast<int>(i);
        }
    }
    if (vIdx < 0) {
        say(QCoreApplication::translate("lr::TranscodeTask", "✗ 输入文件里没有视频轨，无法进行编码。"),
            lr::logcolor::kError);
        goto done;
    }
    totalUs = toUs(api, AV_TIME_BASE_Q, ic->duration);

    // ---------- 2. 打开输出容器 ----------
    // 临时路径一开始就定下来：后缀必须保留原样（pathutil::atomicTempPath 有说明），
    // 所以这里拿它去创建容器 —— 之后 movflags +faststart 在写尾时重新打开的
    // 也是这个临时文件，而不是还没影儿的最终文件。
    {
        tempPath = lr::pathutil::atomicTempPath(spec_.output);
        AVFormatContext *rawOc = nullptr;
        ret = api.avformat_alloc_output_context2(&rawOc, nullptr, nullptr,
                                                 tempPath.c_str());
        oc = lr::wrapOutput(api, rawOc);
    }
    if (!oc) {
        say(QCoreApplication::translate("lr::TranscodeTask", "✗ 无法根据扩展名创建输出文件（建议使用 .mp4）：%1")
                .arg(api.errorText(ret)),
            lr::logcolor::kError);
        goto done;
    }

    pkt.reset(api.av_packet_alloc());
    outPkt.reset(api.av_packet_alloc());
    frame.reset(api.av_frame_alloc());
    if (!pkt || !outPkt || !frame) {
        say(QCoreApplication::translate("lr::TranscodeTask", "✗ 内存分配失败"), lr::logcolor::kError);
        goto done;
    }

    // ---------- 3. 视频解码器 ----------
    {
        AVStream *in = ic->streams[vIdx];
        const AVCodec *dec = api.avcodec_find_decoder(in->codecpar->codec_id);
        if (!dec) {
            say(QCoreApplication::translate("lr::TranscodeTask", "✗ 找不到视频解码器（这份 FFmpeg 的解码能力不足）"),
                lr::logcolor::kError);
            goto done;
        }
        vDec.reset(api.avcodec_alloc_context3(dec));
        if (!vDec || api.avcodec_parameters_to_context(vDec.get(), in->codecpar) < 0 ||
            api.avcodec_open2(vDec.get(), dec, nullptr) < 0) {
            say(QCoreApplication::translate("lr::TranscodeTask", "✗ 无法打开视频解码器"), lr::logcolor::kError);
            goto done;
        }

        resolveScale(spec_.scale, vDec->width, vDec->height, dstW, dstH);
        if (dstW <= 0 || dstH <= 0) {
            say(QCoreApplication::translate("lr::TranscodeTask", "✗ 无法从源文件取得有效分辨率"),
                lr::logcolor::kError);
            goto done;
        }
    }

    // ---------- 4. 视频编码器（硬件不可用时自动回退 libx264） ----------
    {
        AVRational fr = vDec->framerate;
        if (fr.num <= 0 || fr.den <= 0) fr = ic->streams[vIdx]->avg_frame_rate;
        if (fr.num <= 0 || fr.den <= 0) fr = AVRational{25, 1};

        // 与原 cmd_transcode 完全一致的联动公式：
        // bufsize 原来写死 2000k，高码率下会把码率压死、画质很差。
        const int64_t bufsize =
            std::max<int64_t>(static_cast<int64_t>(spec_.videoKbps) * 2, 1000) *
            1000;
        const int64_t maxrate =
            std::max<int64_t>(static_cast<int64_t>(spec_.videoKbps * 1.5),
                              spec_.videoKbps + 500) *
            1000;
        const int lv = levelValue(spec_.level);

        auto tryOpen = [&](const std::string &name, std::string &err) -> int {
            const AVCodec *enc = api.avcodec_find_encoder_by_name(name.c_str());
            if (!enc) return AVERROR_ENCODER_NOT_FOUND;
            vEnc.reset(api.avcodec_alloc_context3(enc));  // reset 先释放旧的
            if (!vEnc) return AVERROR(ENOMEM);

            vEnc->width = dstW;
            vEnc->height = dstH;
            vEnc->pix_fmt = AV_PIX_FMT_YUV420P;
            vEnc->bit_rate = static_cast<int64_t>(spec_.videoKbps) * 1000;
            vEnc->rc_max_rate = maxrate;
            vEnc->rc_buffer_size = static_cast<int>(bufsize);
            vEnc->thread_count = 0;  // 0 = 自动，比命令行默认更好
            vEnc->time_base = av_inv_q(fr);
            vEnc->framerate = fr;
            if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
                vEnc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            }
            if (lv > 0) vEnc->level = lv;

            // 私有选项：硬件编码器不认 preset/profile 时会失败，这里不检查
            // 返回值（videotoolbox 连 level 都不接受）。
            if (vEnc->priv_data) {
                api.av_opt_set(vEnc->priv_data, "preset", spec_.preset.c_str(), 0);
                api.av_opt_set(vEnc->priv_data, "profile", "high", 0);
            }
            // 新版推荐走的 AVOption 通道；失败也不影响（已同时设了 rc_* 字段）
            api.av_opt_set_int(vEnc.get(), "maxrate", maxrate, 0);
            api.av_opt_set_int(vEnc.get(), "bufsize", bufsize, 0);

            AVDictionary *opts = nullptr;
            const int r = api.avcodec_open2(vEnc.get(), enc, &opts);
            api.av_dict_free(&opts);
            if (r < 0) err = api.errorText(r);
            return r;
        };

        std::string err;
        ret = tryOpen(spec_.videoEncoder, err);
        if (ret < 0) {
            const QString first = QString::fromStdString(err);
            if (!spec_.fallbackToX264 || spec_.videoEncoder == "libx264") {
                say(QCoreApplication::translate("lr::TranscodeTask", "✗ 无法打开视频编码器 %1：%2")
                        .arg(QString::fromStdString(spec_.videoEncoder), first),
                    lr::logcolor::kError);
                goto done;
            }
            say(QCoreApplication::translate("lr::TranscodeTask", "⚠ 编码器 %1 不可用（%2），自动回退 libx264。")
                    .arg(QString::fromStdString(spec_.videoEncoder), first),
                lr::logcolor::kWarn);
            err.clear();
            ret = tryOpen("libx264", err);
            if (ret < 0) {
                say(QCoreApplication::translate("lr::TranscodeTask", "✗ 回退到 libx264 也失败了：%1")
                        .arg(QString::fromStdString(err)),
                    lr::logcolor::kError);
                goto done;
            }
        }

        vOut = api.avformat_new_stream(oc.get(), nullptr);
        if (!vOut) {
            say(QCoreApplication::translate("lr::TranscodeTask", "✗ 无法创建视频输出流"), lr::logcolor::kError);
            goto done;
        }
        if (api.avcodec_parameters_from_context(vOut->codecpar, vEnc.get()) < 0) {
            say(QCoreApplication::translate("lr::TranscodeTask", "✗ 无法回填视频流参数"), lr::logcolor::kError);
            goto done;
        }
        vOut->time_base = vEnc->time_base;
        say(QCoreApplication::translate("lr::TranscodeTask", "  视频：%1×%2 → %3×%4，编码器 %5，%6 kbps")
                .arg(vDec->width)
                .arg(vDec->height)
                .arg(dstW)
                .arg(dstH)
                .arg(QString::fromUtf8(vEnc->codec->name))
                .arg(spec_.videoKbps));
    }

    // ---------- 5. 音频：解码 → 重采样 → AAC 编码 ----------
    if (aIdx >= 0) {
        AVStream *ain = ic->streams[aIdx];
        const AVCodec *adec = api.avcodec_find_decoder(ain->codecpar->codec_id);
        if (adec) {
            aDec.reset(api.avcodec_alloc_context3(adec));
            if (aDec &&
                api.avcodec_parameters_to_context(aDec.get(), ain->codecpar) >= 0 &&
                api.avcodec_open2(aDec.get(), adec, nullptr) >= 0) {
                resampler = new Resampler(api);
                std::string err;
                if (!resampler->init(aDec->ch_layout,
                                     static_cast<AVSampleFormat>(aDec->sample_fmt),
                                     aDec->sample_rate, err)) {
                    say(QCoreApplication::translate("lr::TranscodeTask", "⚠ 跳过音频轨：%1")
                            .arg(QString::fromStdString(err)),
                        lr::logcolor::kWarn);
                    delete resampler;
                    resampler = nullptr;
                }
            } else {
                say(QCoreApplication::translate("lr::TranscodeTask", "⚠ 无法打开音频解码器，输出将无音轨。"),
                    lr::logcolor::kWarn);
            }
        } else {
            say(QCoreApplication::translate("lr::TranscodeTask", "⚠ 找不到音频解码器，输出将无音轨。"),
                lr::logcolor::kWarn);
        }

        if (resampler) {
            const AVCodec *aenc = api.avcodec_find_encoder_by_name("aac");
            if (!aenc) {
                say(QCoreApplication::translate("lr::TranscodeTask", "⚠ 这份 FFmpeg 没有 aac 编码器，输出无音轨。"),
                    lr::logcolor::kWarn);
            } else {
                aEnc.reset(api.avcodec_alloc_context3(aenc));
                if (!aEnc) {
                    say(QCoreApplication::translate("lr::TranscodeTask", "⚠ 无法创建 aac 编码器，输出无音轨。"),
                        lr::logcolor::kWarn);
                } else {
                aEnc->sample_rate = resampler->sampleRate();
                aEnc->ch_layout = resampler->layout();
                aEnc->sample_fmt = resampler->format();
                aEnc->bit_rate = static_cast<int64_t>(spec_.audioKbps) * 1000;
                aEnc->time_base = AVRational{1, aEnc->sample_rate};
                aEnc->thread_count = 0;
                if (oc->oformat->flags & AVFMT_GLOBALHEADER) {
                    aEnc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
                }
                if (api.avcodec_open2(aEnc.get(), aenc, nullptr) < 0) {
                    say(QCoreApplication::translate("lr::TranscodeTask", "⚠ 无法打开 aac 编码器，输出无音轨。"),
                        lr::logcolor::kWarn);
                    aEnc.reset();
                } else {
                    audioFrameSize = aEnc->frame_size > 0 ? aEnc->frame_size : 1024;
                    aOut = api.avformat_new_stream(oc.get(), nullptr);
                    if (!aOut ||
                        api.avcodec_parameters_from_context(aOut->codecpar, aEnc.get()) < 0) {
                        say(QCoreApplication::translate("lr::TranscodeTask", "⚠ 无法创建音频输出流，输出无音轨。"),
                            lr::logcolor::kWarn);
                        aEnc.reset();
                    } else {
                        aOut->time_base = aEnc->time_base;
                        say(QCoreApplication::translate("lr::TranscodeTask", "  音频：%1 Hz · %2 声道 · %3 kbps")
                                .arg(aEnc->sample_rate)
                                .arg(resampler->channels())
                                .arg(spec_.audioKbps));
                    }
                }
                }
            }
            if (!aEnc) {
                delete resampler;
                resampler = nullptr;
            }
        }
    }

    // ---------- 6. 写封装头部 ----------
    scaler = new Scaler(api, AV_PIX_FMT_YUV420P);

    if (oc->oformat->flags & AVFMT_NOFILE) {
        tempPath.clear();  // 不落盘的封装器（如直接推流）：谈不上"原子落盘"
    } else {
        // 原子落盘（P2-9 的核心）：容器按临时路径建好、数据也写到那里，
        // 一切成功并关闭句柄后才 rename 到最终路径 —— 崩溃/断电最多留下
        // 一个 .LRConvert-tmp，最终路径要么完整、要么不存在。
        std::error_code rmEc;
        std::filesystem::remove(tempPath, rmEc);  // 上次异常退出留下的残骸
        ret = api.avio_open(&oc->pb, tempPath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            say(QCoreApplication::translate("lr::TranscodeTask", "✗ 无法写入输出文件：%1").arg(api.errorText(ret)),
                lr::logcolor::kError);
            goto done;
        }
    }
    {
        AVDictionary *opts = nullptr;
        for (const auto &kv : spec_.muxerOptions) {
            api.av_dict_set(&opts, kv.first.c_str(), kv.second.c_str(), 0);
        }
        ret = api.avformat_write_header(oc.get(), &opts);
        api.av_dict_free(&opts);
        if (ret < 0) {
            say(QCoreApplication::translate("lr::TranscodeTask", "✗ 写封装头部失败：%1").arg(api.errorText(ret)),
                lr::logcolor::kError);
            goto done;
        }
    }

    // ---------- 7. 主循环：读包 → 解码 → 转换 → 编码 → 写包 ----------
    {
        Throttle th;
        while (!cancel_.load()) {
            // 读包刹车（P0-4）：本地文件不走协议轮询；网络协议下 30 秒
            // 无进展就中断，把"永久挂起"变回"正常报错"
            lr::io::armTimeout(lr::io::kReadTimeoutMs);
            ret = api.av_read_frame(ic.get(), pkt.get());
            lr::io::disarmTimeout();
            if (ret < 0) {
                inputEof = true;
                break;
            }
            if (pkt->stream_index == vIdx) {
                const int s = api.avcodec_send_packet(vDec.get(), pkt.get());
                if (s < 0 && s != AVERROR(EAGAIN)) {
                    stopReason = "视频解码器不接受数据";
                    api.av_packet_unref(pkt.get());
                    break;
                }
                if (!drainVideo()) break;
            } else if (pkt->stream_index == aIdx && resampler) {
                const int s = api.avcodec_send_packet(aDec.get(), pkt.get());
                if (s < 0 && s != AVERROR(EAGAIN)) {
                    stopReason = "音频解码器不接受数据";
                    api.av_packet_unref(pkt.get());
                    break;
                }
                if (!drainAudio()) break;
            }
            api.av_packet_unref(pkt.get());

            if (totalUs > 0) {
                const int permille = static_cast<int>(doneUs * 1000 / totalUs);
                if (th.ready(permille)) {
                    emit sig.progress(permille / 10,
                                      QCoreApplication::translate("lr::TranscodeTask", "已处理 %1 / %2 秒")
                                          .arg(doneUs / kUsRate)
                                          .arg(totalUs / kUsRate));
                }
            } else {
                emit sig.progress(-1, QCoreApplication::translate("lr::TranscodeTask", "正在编码…"));
            }
        }
        if (pkt) api.av_packet_unref(pkt.get());
    }

    // ---------- 8. 收尾：冲刷解码器 / 重采样器 / 编码器 ----------
    // 即使被"停止"也要走完这一步 —— 对应原来"先发 q 让 ffmpeg 自己收尾"，
    // 保证输出文件头部/索引完整、可以直接播放。
    if (inputEof) {
        api.avcodec_send_packet(vDec.get(), nullptr);
        drainVideo();
        if (resampler) {
            api.avcodec_send_packet(aDec.get(), nullptr);
            drainAudio();
            std::string err;
            if (resampler->flush(err)) {
                // 注意：冲刷阶段残余样本**不足一帧也要送出去**（编码器接受
                // 最后一帧短一些）。如果这里仍然只按整帧取，尾部会凭空丢掉
                // 一个 AAC 帧 —— 表现就是输出比源短 21ms、音频包数少一个。
                while (resampler->pendingSamples() > 0) {
                    const int n = std::min(audioFrameSize,
                                           resampler->pendingSamples());
                    AVFrame *af = resampler->take(n);
                    if (!af) break;
                    af->pts = api.av_rescale_q(
                        aOutSamples, FfRational{1, resampler->sampleRate()},
                        rat(aEnc->time_base));
                    aOutSamples += n;
                    if (api.avcodec_send_frame(aEnc.get(), af) < 0 || !pumpAudio()) break;
                    resampler->consume(n);
                }
            }
        }
    }
    api.avcodec_send_frame(vEnc.get(), nullptr);
    pumpVideo();
    if (aEnc) {
        api.avcodec_send_frame(aEnc.get(), nullptr);
        pumpAudio();
    }
    if (api.av_write_trailer(oc.get()) < 0 && stopReason.empty()) {
        stopReason = "写封装尾部失败";
    }

    if (cancel_.load()) {
        // 被手动停止：已经写进去的那部分仍然是可播放的完整文件（trailer 已写），
        // 按原行为保留 —— 所以这里也算"晋升"（下面 done: 里 rename）。
        keepOutput = true;
        say(QCoreApplication::translate("lr::TranscodeTask", "■ 已手动停止，输出文件已保留到这里为止的内容。"),
            lr::logcolor::kWarn);
    } else if (!stopReason.empty()) {
        say(QCoreApplication::translate("lr::TranscodeTask", "✗ 编码中断：%1").arg(QString::fromStdString(stopReason)),
            lr::logcolor::kError);
    } else {
        emit sig.progress(1000 / 10, QCoreApplication::translate("lr::TranscodeTask", "编码完成"));
        say(QCoreApplication::translate("lr::TranscodeTask", "✓ 编码完成：%1").arg(QString::fromStdString(spec_.output)),
            lr::logcolor::kOk);
        ok = true;
        keepOutput = true;
    }

    } catch (const std::exception &e) {
        // P2-9：任务线程同样是 std::thread 语义，异常逃出即 std::terminate，
        // 整个进程秒死。这里收敛成"该任务失败"，批处理继续/中止由上层决定。
        // （RAII 保证上面的 libav* 对象在异常路径上一样被释放。）
        say(QCoreApplication::translate("lr::TranscodeTask", "✗ 编码过程发生异常：%1")
                .arg(QString::fromUtf8(e.what())),
            lr::logcolor::kError);
    } catch (...) {
        say(QCoreApplication::translate("lr::TranscodeTask", "✗ 编码过程发生未知异常"),
            lr::logcolor::kError);
    }

done:
    // 输出容器：先关文件句柄（不关的话 rename 时数据可能还在缓冲里），
    // 再按成败决定"晋升"还是"删掉"。上下文本身由 RAII 在此之后释放。
    if (oc && !tempPath.empty()) {
        lr::closeFormatFile(oc.get(), &api);
        std::error_code ec;
        if (keepOutput) {
            std::filesystem::rename(tempPath, spec_.output, ec);
            if (ec) {
                say(QCoreApplication::translate("lr::TranscodeTask", "✗ 输出已写完但未能就位（%1）：%2")
                        .arg(QString::fromStdString(spec_.output),
                             QString::fromLocal8Bit(ec.message())),
                    lr::logcolor::kError);
                ok = false;
                std::filesystem::remove(tempPath, ec);
            }
        } else {
            // 失败：最终路径保持原状（上一版会把半个坏文件写在这里），
            // 临时文件清掉 —— 宁可没有输出，也不给一个打不开的文件。
            std::filesystem::remove(tempPath, ec);
        }
    }
    delete scaler;
    delete resampler;
    return ok;
    // pkt / outPkt / frame / vDec / vEnc / aDec / aEnc / oc / ic 在 return
    // 之后随 RAII 析构（P2-11）
}

}  // namespace lr
