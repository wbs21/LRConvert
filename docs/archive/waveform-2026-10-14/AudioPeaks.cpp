#include "ffmpeg/AudioPeaks.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>

#include <algorithm>
#include <cmath>

namespace lr {
namespace {

constexpr int kOutSampleRate = 8000;   // 峰值只需要低频信息，8kHz 足够且快
constexpr int kBufferSamples = 8192;   // 每次 swr_convert 的输出块

int firstAudioStream(AVFormatContext *fmt) {
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// 峰值构建的核心循环：把 [fromSec, toSec) 切成 buckets 片，
/// 每片记下片内的最小/最大振幅（绘制时取该片高度填满整片）
bool buildCore(FfmpegApi &api, const std::string &path, double fromSec, double toSec,
               int buckets, bool seekToStart, AudioPeaks &out, std::string &error) {
    AVFormatContext *fmt = nullptr;
    int ret = api.avformat_open_input(&fmt, path.c_str(), nullptr, nullptr);
    if (ret < 0) {
        error = api.errorText(ret);
        return false;
    }

    if (api.avformat_find_stream_info(fmt, nullptr) < 0) {
        // 不致命
    }

    const int audioIdx = firstAudioStream(fmt);
    if (audioIdx < 0) {
        api.avformat_close_input(&fmt);
        error = "没有音轨";
        return false;
    }

    AVStream *stream = fmt->streams[audioIdx];
    const AVCodec *codec = api.avcodec_find_decoder(stream->codecpar->codec_id);
    if (codec == nullptr) {
        api.avformat_close_input(&fmt);
        error = "找不到对应的音频解码器";
        return false;
    }

    AVCodecContext *codecCtx = api.avcodec_alloc_context3(codec);
    if (codecCtx == nullptr) {
        api.avformat_close_input(&fmt);
        error = "分配解码上下文失败";
        return false;
    }
    ret = api.avcodec_parameters_to_context(codecCtx, stream->codecpar);
    if (ret < 0) {
        error = api.errorText(ret);
        api.avcodec_free_context(&codecCtx);
        api.avformat_close_input(&fmt);
        return false;
    }
    ret = api.avcodec_open2(codecCtx, codec, nullptr);
    if (ret < 0) {
        error = api.errorText(ret);
        api.avcodec_free_context(&codecCtx);
        api.avformat_close_input(&fmt);
        return false;
    }

    // 重采样到 单声道 / s16 / 8kHz：后续分桶只读一路 s16，最快
    AVChannelLayout mono;
    api.av_channel_layout_default(&mono, 1);
    SwrContext *swr = api.swr_alloc();
    ret = api.swr_alloc_set_opts2(&swr, &mono, AV_SAMPLE_FMT_S16, kOutSampleRate,
                                  &codecCtx->ch_layout, codecCtx->sample_fmt,
                                  codecCtx->sample_rate, 0, nullptr);
    if (ret < 0 || swr == nullptr) {
        error = "创建音频重采样上下文失败";
        api.swr_free(&swr);
        api.avcodec_free_context(&codecCtx);
        api.avformat_close_input(&fmt);
        return false;
    }
    if (api.swr_init(swr) < 0) {
        error = "初始化音频重采样失败";
        api.swr_free(&swr);
        api.avcodec_free_context(&codecCtx);
        api.avformat_close_input(&fmt);
        return false;
    }

    const double timeBase = av_q2d(stream->time_base);
    const double streamSec = (timeBase > 0) ? timeBase : 1.0 / 1000.0;
    if (seekToStart) {
        // 往前多退 1 秒再 seek：音频 seek 只能落到关键位置，直接定位到 fromSec
        // 很容易丢掉开头那一段，波形左端就会缺一块
        const double back = std::max(0.0, fromSec - 1.0);
        const int64_t ts = static_cast<int64_t>(std::llround(back / streamSec));
        api.avformat_seek_file(fmt, audioIdx, INT64_MIN, ts, ts, 0);
        // 注意：seek 后必须用 avcodec_flush_buffers 清掉解码器里积压的旧帧。
        // 用"发一个空包"代替会把解码器推进 draining 状态，之后 send_packet
        // 一律返回 AVERROR_EOF，表现就是"怎么都解不出采样"。
        api.avcodec_flush_buffers(codecCtx);
    }

    const double spanSec = std::max(1e-6, toSec - fromSec);
    const auto bucketCount = static_cast<std::size_t>(std::max(1, buckets));
    out.fromSec = fromSec;
    out.toSec = toSec;
    out.pointsPerSec = double(bucketCount) / spanSec;
    out.min.assign(bucketCount, 0);
    out.max.assign(bucketCount, 0);

    uint8_t *buffer = nullptr;
    int linesize = 0;
    if (api.av_samples_alloc(&buffer, &linesize, 1, kBufferSamples, AV_SAMPLE_FMT_S16, 0) < 0) {
        error = "分配采样缓冲失败";
        api.swr_free(&swr);
        api.avcodec_free_context(&codecCtx);
        api.avformat_close_input(&fmt);
        return false;
    }

    // 把一段 s16 采样投进对应的片里；返回是否越过了区间末尾
    const double sampleStep = 1.0 / double(kOutSampleRate);
    auto feed = [&](const int16_t *samples, int count, double startSec) -> bool {
        double t = startSec;
        for (int i = 0; i < count; ++i, t += sampleStep) {
            if (t >= toSec) return true;
            if (t < fromSec) continue;
            const std::size_t idx =
                static_cast<std::size_t>((t - fromSec) / spanSec * double(bucketCount));
            if (idx >= bucketCount) continue;
            const int16_t v = samples[i];
            if (v < out.min[idx]) out.min[idx] = v;
            if (v > out.max[idx]) out.max[idx] = v;
        }
        return false;
    };

    AVPacket *packet = api.av_packet_alloc();
    AVFrame *frame = api.av_frame_alloc();
    bool stop = false;
    double writeSec = fromSec;   // 下一段待写采样的起始时间

    while (!stop && api.av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index != audioIdx) {
            api.av_packet_unref(packet);
            continue;
        }
        if (api.avcodec_send_packet(codecCtx, packet) < 0) {
            api.av_packet_unref(packet);
            continue;
        }
        api.av_packet_unref(packet);

        while (api.avcodec_receive_frame(codecCtx, frame) >= 0) {
            // 这一帧的起始时间（重采样后输出样本的时间基准）
            const int64_t pts = (frame->pts != AV_NOPTS_VALUE) ? frame->pts : frame->pkt_dts;
            const double frameSec = (pts != AV_NOPTS_VALUE) ? double(pts) * timeBase : fromSec;
            double posSec = frameSec;

            int produced = api.swr_convert(swr, &buffer, kBufferSamples,
                                           const_cast<const uint8_t **>(frame->data),
                                           frame->nb_samples);
            while (produced > 0) {
                const int16_t *samples = reinterpret_cast<int16_t *>(buffer);
                stop = feed(samples, produced, posSec);
                posSec += double(produced) * sampleStep;
                if (stop) break;
                produced = api.swr_convert(swr, &buffer, kBufferSamples, nullptr, 0);
            }
            writeSec = posSec;
            api.av_frame_unref(frame);
            if (stop) break;
        }
    }

    // 冲掉 swr 里剩余的样本并投进对应的片（不这么做末尾几片会偏小）
    if (!stop) {
        int produced = api.swr_convert(swr, &buffer, kBufferSamples, nullptr, 0);
        while (produced > 0) {
            const int16_t *samples = reinterpret_cast<int16_t *>(buffer);
            stop = feed(samples, produced, writeSec);
            writeSec += double(produced) * sampleStep;
            if (stop) break;
            produced = api.swr_convert(swr, &buffer, kBufferSamples, nullptr, 0);
        }
    }

    api.av_freep(&buffer);
    api.av_frame_free(&frame);
    api.av_packet_free(&packet);
    api.swr_free(&swr);
    api.avcodec_free_context(&codecCtx);
    api.avformat_close_input(&fmt);
    return true;
}

}  // namespace

bool AudioPeakBuilder::build(FfmpegApi &api, const std::string &path, double pointsPerSec,
                             AudioPeaks &out, std::string &error) {
    // 先拿时长，决定片数（必须先 find_stream_info，否则很多容器 duration 还是 0）
    AVFormatContext *fmt = nullptr;
    double duration = 0.0;
    if (api.avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) >= 0) {
        api.avformat_find_stream_info(fmt, nullptr);
        if (fmt->duration > 0) duration = double(fmt->duration) / double(AV_TIME_BASE);
        api.avformat_close_input(&fmt);
    }
    if (duration <= 0.0) {
        error = "读不到时长";
        return false;
    }
    const int slices = static_cast<int>(std::max(1.0, std::ceil(duration * pointsPerSec)));
    return buildCore(api, path, 0.0, duration, slices, false, out, error);
}

bool AudioPeakBuilder::buildRange(FfmpegApi &api, const std::string &path, double fromSec,
                                  double toSec, int slices, AudioPeaks &out,
                                  std::string &error) {
    if (toSec <= fromSec) return false;
    return buildCore(api, path, fromSec, toSec, slices, true, out, error);
}

}  // namespace lr
