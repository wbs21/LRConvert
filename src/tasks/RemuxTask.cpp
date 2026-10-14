#include "RemuxTask.h"

#include "core/TimeUtil.h"
#include "ffmpeg/FfmpegApi.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/rational.h>
}

#include <QDateTime>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>

namespace lr {
namespace {

constexpr int kUsRate = 1000000;

/// 一个输入文件及其「输入流 → 输出流」的映射
struct InCtx {
    AVFormatContext *ic = nullptr;
    std::vector<int> map;   ///< 下标 = 输入流序号，值 = 输出流序号（-1 跳过）
    int64_t offsetUs = 0;   ///< 叠加到本输入所有包上的时间偏移（微秒）
    int64_t endUs = 0;      ///< 本输入的结束时间（微秒，= 最后一个包的 pts+duration）
    bool eof = false;
};

FfRational rat(AVRational r) { return FfRational{r.num, r.den}; }

int64_t toUs(FfmpegApi &api, AVRational tb, int64_t value) {
    return api.av_rescale_q(value, rat(tb), FfRational{1, kUsRate});
}

int64_t fromUs(FfmpegApi &api, AVRational tb, int64_t us) {
    return api.av_rescale_q(us, FfRational{1, kUsRate}, rat(tb));
}

/// 节流器：避免每包都往 GUI 发信号（阈值与 Python 版一致：0.25 秒 / 0.4%）
class Throttle {
public:
    bool ready(int64_t permille) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - last_ < kIntervalMs) return false;
        if (permille >= 0 && std::llabs(permille - lastPermille_) < 4) {
            return false;
        }
        last_ = now;
        lastPermille_ = permille;
        return true;
    }

private:
    static constexpr qint64 kIntervalMs = 250;
    qint64 last_ = 0;
    int64_t lastPermille_ = -10000;
};

bool isTargetType(AVCodecParameters *par, RemuxSpec::Kind kind) {
    switch (kind) {
        case RemuxSpec::Kind::VideoOnly:
            return par->codec_type == AVMEDIA_TYPE_VIDEO;
        case RemuxSpec::Kind::AudioOnly:
            return par->codec_type == AVMEDIA_TYPE_AUDIO;
        default:
            // CopyAll / Concat：-map 0，跳过附件与数据轨
            return par->codec_type == AVMEDIA_TYPE_VIDEO ||
                   par->codec_type == AVMEDIA_TYPE_AUDIO ||
                   par->codec_type == AVMEDIA_TYPE_SUBTITLE;
    }
}

}  // namespace

QString RemuxTask::title() const {
    return QString::fromStdString(spec_.title);
}

bool RemuxTask::run(TaskSignals &sig) {
    auto &api = FfmpegApi::instance();
    if (!api.isLoaded()) {
        emit sig.log(QStringLiteral("✗ FFmpeg 动态库尚未加载完成，请稍候再试。"),
                     logcolor::kError);
        return false;
    }

    auto log = [&sig](const QString &text,
                      const QString &color = logcolor::kInfo) {
        emit sig.log(text, color);
    };

    auto closeInputs = [&api](std::vector<InCtx> &ctxs) {
        for (auto &in : ctxs) {
            if (in.ic) api.avformat_close_input(&in.ic);
        }
    };
    auto discardOutput = [&api](AVFormatContext *ctx) {
        if (ctx) {
            if (!(ctx->oformat->flags & AVFMT_NOFILE) && ctx->pb) {
                api.avio_closep(&ctx->pb);
            }
            api.avformat_free_context(ctx);
        }
    };

    // ---------------- 1. 打开输入 ----------------
    std::vector<InCtx> ins(spec_.inputs.size());
    for (size_t i = 0; i < spec_.inputs.size(); ++i) {
        AVFormatContext *ic = nullptr;
        int ret = api.avformat_open_input(&ic, spec_.inputs[i].c_str(), nullptr,
                                          nullptr);
        if (ret < 0) {
            log(QStringLiteral("✗ 无法打开输入文件：%1\n   %2")
                    .arg(QString::fromStdString(spec_.inputs[i]),
                         QString::fromStdString(api.errorText(ret))),
                logcolor::kError);
            closeInputs(ins);
            return false;
        }
        ret = api.avformat_find_stream_info(ic, nullptr);
        if (ret < 0) {
            log(QStringLiteral("✗ 无法读取媒体信息：%1\n   %2")
                    .arg(QString::fromStdString(spec_.inputs[i]),
                         QString::fromStdString(api.errorText(ret))),
                logcolor::kError);
            api.avformat_close_input(&ic);
            closeInputs(ins);
            return false;
        }
        ins[i].ic = ic;
        ins[i].map.assign(ic->nb_streams, -1);
    }

    // ---------------- 2. 截取：先 seek 到起点 ----------------
    int64_t startUs = 0;
    if (spec_.startSec > 0 && !ins.empty()) {
        startUs = timeutil::toAvTime(spec_.startSec);
        const int ret = api.avformat_seek_file(ins[0].ic, -1, INT64_MIN, startUs,
                                               startUs, AVSEEK_FLAG_BACKWARD);
        if (ret < 0) {
            log(QStringLiteral("⚠ 无法定位到 %1，将从文件开头开始（%2）")
                    .arg(QString::fromStdString(
                             timeutil::formatTimestamp(spec_.startSec)),
                         QString::fromStdString(api.errorText(ret))),
                logcolor::kWarn);
            startUs = 0;
        }
    }

    // ---------------- 3. 建立输出 ----------------
    AVFormatContext *oc = nullptr;
    int ret = api.avformat_alloc_output_context2(&oc, nullptr, nullptr,
                                                 spec_.output.c_str());
    if (ret < 0 || oc == nullptr) {
        log(QStringLiteral("✗ 无法创建输出文件（后缀名可能不被支持）：%1")
                .arg(QString::fromStdString(spec_.output)),
            logcolor::kError);
        closeInputs(ins);
        return false;
    }

    auto addOutputStream = [&](AVStream *src) -> int {
        AVStream *os = api.avformat_new_stream(oc, nullptr);
        if (!os) return -1;
        if (api.avcodec_parameters_copy(os->codecpar, src->codecpar) < 0) {
            return -1;
        }
        os->codecpar->codec_tag = 0;
        os->time_base = src->time_base;
        return static_cast<int>(oc->nb_streams) - 1;
    };

    if (spec_.kind == RemuxSpec::Kind::Merge) {
        // 原命令：-map 0:v:0 -map 1:a:0 -c copy -shortest
        if (ins.size() < 2) {
            log(QStringLiteral("✗ 合并需要两个输入（视频 + 音频）。"),
                logcolor::kError);
            discardOutput(oc);
            closeInputs(ins);
            return false;
        }
        int videoIdx = -1;
        for (unsigned i = 0; i < ins[0].ic->nb_streams; ++i) {
            if (ins[0].ic->streams[i]->codecpar->codec_type ==
                AVMEDIA_TYPE_VIDEO) {
                videoIdx = static_cast<int>(i);
                break;
            }
        }
        int audioIdx = -1;
        for (unsigned i = 0; i < ins[1].ic->nb_streams; ++i) {
            if (ins[1].ic->streams[i]->codecpar->codec_type ==
                AVMEDIA_TYPE_AUDIO) {
                audioIdx = static_cast<int>(i);
                break;
            }
        }
        if (videoIdx < 0 || audioIdx < 0) {
            log(QStringLiteral("✗ 合并失败：视频文件里没有视频轨，或音频文件里没有音频轨。"),
                logcolor::kError);
            discardOutput(oc);
            closeInputs(ins);
            return false;
        }
        const int v = addOutputStream(ins[0].ic->streams[videoIdx]);
        const int a = addOutputStream(ins[1].ic->streams[audioIdx]);
        if (v < 0 || a < 0) {
            log(QStringLiteral("✗ 无法创建输出流。"), logcolor::kError);
            discardOutput(oc);
            closeInputs(ins);
            return false;
        }
        ins[0].map[videoIdx] = v;
        ins[1].map[audioIdx] = a;
    } else {
        const bool concat = spec_.kind == RemuxSpec::Kind::Concat;
        for (size_t i = 0; i < ins.size(); ++i) {
            for (unsigned j = 0; j < ins[i].ic->nb_streams; ++j) {
                AVStream *src = ins[i].ic->streams[j];
                if (!isTargetType(src->codecpar, spec_.kind)) continue;

                if (i == 0) {
                    const int out = addOutputStream(src);
                    if (out < 0) {
                        log(QStringLiteral("✗ 无法创建输出流。"), logcolor::kError);
                        discardOutput(oc);
                        closeInputs(ins);
                        return false;
                    }
                    ins[i].map[j] = out;
                } else {
                    // 后续输入按流序号对齐到第一个输入建立的输出流；
                    // 轨道类型不一致就跳过（编码参数不同的文件本来也拼不了）。
                    if (j < ins[0].map.size() && ins[0].map[j] >= 0 &&
                        src->codecpar->codec_type ==
                            ins[0].ic->streams[j]->codecpar->codec_type) {
                        ins[i].map[j] = ins[0].map[j];
                    } else {
                        log(QStringLiteral("⚠ 第 %1 个文件的第 %2 条轨道与第一个文件"
                                           "不匹配，已跳过。")
                                .arg(i + 1)
                                .arg(j),
                            logcolor::kWarn);
                    }
                }
            }
        }
    }

    if (oc->nb_streams == 0) {
        log(QStringLiteral("✗ 没有找到任何可复制的轨道，已放弃。"), logcolor::kError);
        discardOutput(oc);
        closeInputs(ins);
        return false;
    }

    if (!(oc->oformat->flags & AVFMT_NOFILE)) {
        ret = api.avio_open(&oc->pb, spec_.output.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            log(QStringLiteral("✗ 无法写入输出文件（路径不存在或没有权限）：%1\n   %2")
                    .arg(QString::fromStdString(spec_.output),
                         QString::fromStdString(api.errorText(ret))),
                logcolor::kError);
            discardOutput(oc);
            closeInputs(ins);
            return false;
        }
    }

    AVDictionary *opts = nullptr;
    for (const auto &kv : spec_.muxerOptions) {
        api.av_dict_set(&opts, kv.first.c_str(), kv.second.c_str(), 0);
    }

    // 只有「截取」才需要平移时间戳：seek 到关键帧后第一个包通常是负的。
    // Python 版同样只在 cmd_cut 里加了 -avoid_negative_ts 1；
    // 其余功能必须保持原样，否则 AAC 的编码延迟（首包 pts 为负）会被抹掉，
    // 输出时长会比源文件多出一个音频帧。
    if (spec_.startSec > 0) oc->avoid_negative_ts = 1;

    ret = api.avformat_write_header(oc, &opts);
    api.av_dict_free(&opts);
    if (ret < 0) {
        log(QStringLiteral("✗ 无法写入文件头：%1")
                .arg(QString::fromStdString(api.errorText(ret))),
            logcolor::kError);
        discardOutput(oc);
        closeInputs(ins);
        std::error_code ec;
        std::filesystem::remove(spec_.output, ec);
        return false;
    }

    // ---------------- 4. 拷贝数据包 ----------------
    log(QStringLiteral("▶ 开始处理：%1").arg(title()), logcolor::kStep);

    int64_t totalUs = 0;
    if (spec_.kind == RemuxSpec::Kind::Concat) {
        for (auto &in : ins) {
            if (in.ic->duration > 0) totalUs += in.ic->duration;
        }
    } else if (!ins.empty()) {
        totalUs = ins[0].ic->duration;
    }
    if (spec_.startSec >= 0 && spec_.endSec > spec_.startSec) {
        totalUs = timeutil::toAvTime(spec_.endSec - spec_.startSec);
    } else if (totalUs > startUs) {
        totalUs -= startUs;
    }

    const int64_t endUs =
        spec_.endSec > 0 ? timeutil::toAvTime(spec_.endSec) : INT64_MAX;
    const QString totalText = QString::fromStdString(
        timeutil::formatTimestamp(timeutil::fromAvTime(totalUs)));

    AVPacket *pkt = api.av_packet_alloc();
    Throttle throttle;
    bool ok = true;

    /// 读一个包并写出；返回 false 表示这个输入已经结束（或出错）
    auto readAndWrite = [&](InCtx &in, AVPacket *p) -> bool {
        const int rret = api.av_read_frame(in.ic, p);
        if (rret < 0) {
            if (rret != AVERROR_EOF) {
                log(QStringLiteral("✗ 读取数据失败：%1")
                        .arg(QString::fromStdString(api.errorText(rret))),
                    logcolor::kError);
                ok = false;
            }
            return false;
        }

        const int srcIdx = p->stream_index;
        if (srcIdx < 0 || srcIdx >= static_cast<int>(in.map.size()) ||
            in.map[srcIdx] < 0) {
            api.av_packet_unref(p);
            return true;
        }

        AVStream *is = in.ic->streams[srcIdx];
        AVStream *os = oc->streams[in.map[srcIdx]];

        const int64_t rawPts = p->pts != AV_NOPTS_VALUE ? p->pts : p->dts;
        if (rawPts != AV_NOPTS_VALUE) {
            const int64_t localUs = toUs(api, is->time_base, rawPts);
            const int64_t absUs = localUs + in.offsetUs;

            // 拼接时，第二个及以后的文件要丢掉「负时间戳」的包。
            // 这些包是编码器延迟产生的引导样本（AAC 首包 pts 通常为 -1024/44100 秒），
            // 第一个文件保留它是正确的（mp4 会用 edit list 修掉），
            // 但拼到后面就会让该轨道的 dts 回退，muxer 直接报
            // "non monotonically increasing dts"。concat 解封装器同样是丢掉的。
            if (spec_.kind == RemuxSpec::Kind::Concat && in.offsetUs > 0 &&
                localUs < 0) {
                api.av_packet_unref(p);
                return true;
            }

            const int64_t tailUs =
                p->duration > 0 ? toUs(api, is->time_base, p->duration) : 0;
            if (localUs + tailUs > in.endUs) in.endUs = localUs + tailUs;

            if (absUs >= endUs) {  // 到达截取终点
                api.av_packet_unref(p);
                return false;
            }

            const int64_t done = std::max<int64_t>(absUs - startUs, 0);
            const int64_t permille =
                totalUs > 0 ? done * 1000 / totalUs : -1;
            if (throttle.ready(permille)) {
                emit sig.progress(
                    totalUs > 0 ? static_cast<int>(done * 100 / totalUs) : -1,
                    QStringLiteral("%1 · %2 / %3")
                        .arg(title(),
                             QString::fromStdString(timeutil::formatTimestamp(
                                 timeutil::fromAvTime(done))),
                             totalText));
            }
        }

        api.av_packet_rescale_ts(p, rat(is->time_base), rat(os->time_base));
        const int64_t deltaUs = in.offsetUs - startUs;
        if (deltaUs != 0) {
            const int64_t add = fromUs(api, os->time_base, deltaUs);
            if (p->pts != AV_NOPTS_VALUE) p->pts += add;
            if (p->dts != AV_NOPTS_VALUE) p->dts += add;
        }
        p->stream_index = in.map[srcIdx];

        const int wret = api.av_interleaved_write_frame(oc, p);
        api.av_packet_unref(p);
        if (wret < 0) {
            log(QStringLiteral("✗ 写入数据失败：%1")
                    .arg(QString::fromStdString(api.errorText(wret))),
                logcolor::kError);
            ok = false;
            return false;
        }
        return true;
    };

    // Concat 必须顺序读（否则后面文件的大时间戳会被交错缓冲在内存里）；
    // Merge 需要两个输入轮流读，才能把音视频正确地交错在一起。
    const bool sequential = spec_.kind == RemuxSpec::Kind::Concat;
    size_t cur = 0;
    int64_t accUs = 0;

    while (ok && !cancel_.load()) {
        if (sequential) {
            InCtx &in = ins[cur];
            if (readAndWrite(in, pkt)) continue;
            if (!ok) break;
            in.eof = true;
            accUs += in.endUs;
            ++cur;
            if (cur >= ins.size()) break;
            ins[cur].offsetUs = accUs;
        } else {
            int produced = 0;
            bool anyEof = false;
            for (auto &in : ins) {
                if (in.eof) {
                    anyEof = true;
                    continue;
                }
                if (readAndWrite(in, pkt)) {
                    ++produced;
                } else {
                    if (!ok) break;
                    in.eof = true;
                    anyEof = true;
                }
            }
            if (!ok || produced == 0) break;
            if (spec_.shortest && anyEof) break;  // 原 -shortest
        }
    }

    api.av_packet_free(&pkt);

    // ---------------- 5. 收尾 ----------------
    // 即使被取消也要写 trailer：这样输出文件仍然是可正常播放的完整文件，
    // 对应 Python 版「先发 q 让 ffmpeg 正常收尾」的行为。
    const bool cancelled = cancel_.load();
    ret = api.av_write_trailer(oc);
    if (ret < 0) {
        log(QStringLiteral("✗ 写入文件尾失败：%1")
                .arg(QString::fromStdString(api.errorText(ret))),
            logcolor::kError);
        ok = false;
    }

    discardOutput(oc);
    closeInputs(ins);

    if (!ok) {
        std::error_code ec;
        std::filesystem::remove(spec_.output, ec);
        return false;
    }
    if (cancelled) {
        log(QStringLiteral("■ 已停止（输出已正常收尾，可直接播放）：%1")
                .arg(title()),
            logcolor::kWarn);
        return false;
    }

    emit sig.progress(100, QStringLiteral("%1 · 完成").arg(title()));
    log(QStringLiteral("✓ 完成：%1 → %2")
            .arg(title(), QString::fromStdString(spec_.output)),
        logcolor::kOk);
    return true;
}

}  // namespace lr
