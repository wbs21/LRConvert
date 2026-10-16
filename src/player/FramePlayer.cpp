// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "player/FramePlayer.h"

#include "platform/CrashDump.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace lr {
namespace {

/// 音频坏包连续到这个数就彻底放弃音轨：画面照播，别让坏音频拖着整个播放
constexpr int kMaxAudioBadRun = 20;
/// 视频连续补救超过这么久（秒）就判定救不回来了 —— 再拖下去只是空转
constexpr double kMaxRecoverySec = 1.5;
/// 丢包隐藏补多长：短了没意义，长了会听出"嗡嗡"的重复
constexpr double kConcealMinSec = 0.005;
constexpr double kConcealMaxSec = 0.100;
/// 保留多长的上一包尾音用于隐藏（20ms 足够盖住一般的数据缺失）
constexpr double kPlcTailSec = 0.020;

int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(v, hi)); }

}  // namespace

FramePlayer::FramePlayer() = default;

FramePlayer::~FramePlayer() {
    stop();
    close();
}

bool FramePlayer::open(FfmpegApi &api, const std::string &path, double fps, int outW,
                       int outH, std::string &error) {
    stop();
    close();

    if (!video_.open(api, path, error)) return false;

    fps_ = (fps > 0.1) ? fps : video_.fps();
    if (!(fps_ > 0.1)) fps_ = 25.0;  // 兜底：拿不到帧率就按 25 走
    outW_ = outW;
    outH_ = outH;

    // 音频再开一次同一份文件：av_read_frame 是"读走一个包"，
    // 视频和音频共用同一个 AVFormatContext 会互相偷对方的包。
    std::string audioError;
    audio_.open(api, path, audioError);  // 无音轨也算成功，只是不出声

    opened_ = true;
    return true;
}

void FramePlayer::close() {
    // 必须先收线程：video_ / audio_ 就在播放线程里被用着，
    // 直接释放等于让它在已析构的对象上继续解码。
    // （open() 里 stop + close 是成对写的，但 close() 单独被调用时也要安全。）
    stop();
    stopAudio();
    audio_.close();
    video_.close();
    opened_ = false;
}

void FramePlayer::play(double fromSec, double toSec) {
    stop();
    if (!opened_) return;

    stopFlag_ = false;
    silentStop_ = false;
    pos_ = fromSec;
    playing_ = true;

    // 本次播放重新开始计数：坏块统计是"从 open 起累计"的，
    // 这里记下基线，结束时用差值才是"这次播放跳过了多少"。
    stats_ = PlayStats{};
    videoBadBase_ = video_.badBlocks();
    audioBadBase_ = audio_.badBlocks();
    lastGood_.reset();  // 换起点了，不能沿用上一次播放留下的画面
    plcTail_.clear();
    audioBadRun_ = 0;

    thread_ = lr::platform::BigStackThread(
        [this, fromSec, toSec]() {
            // 看门狗开始盯这条线程（P0-1）：播起来之后每帧打点，
            // 15 秒不打点就判定卡死并留一份 dump；线程退出（播完/出错/
            // 异常）时取消盯梢 —— 放在这里而非 stop() 里，覆盖"自己播完"
            // 那条路径，不会留下"没人跑了还被盯着"的误报窗口
            lr::platform::setExpected(lr::platform::Heartbeat::Playback, true);
            lr::platform::heartbeat(lr::platform::Heartbeat::Playback);
            // ⚠ 这层 try 是保命的：解码、sws 缩放、vector 分配都可能抛异常
            // （损坏文件、异常分辨率、内存不足），一旦让异常逃出线程函数，
            // C++ 运行时会直接 std::terminate() —— 整个进程瞬间消失，
            // 没有日志、没有崩溃对话框。宁可安静地当成"播完了"。
            try {
                threadMain(fromSec, toSec);
            } catch (...) {
                playing_ = false;
                stopAudio();
                if (endCb_ && !silentStop_) endCb_(stats_);
            }
            lr::platform::setExpected(lr::platform::Heartbeat::Playback, false);
        },
        "LRDecode-Play");  // 线程名（P1-6）：dump 里能一眼看出是播放线程
}

void FramePlayer::stop() {
    stopFlag_ = true;
    silentStop_ = true;  // 是我让人家停的，不要再回调"播完了"
    if (thread_.joinable()) thread_.join();
    playing_ = false;
    stopAudio();
    // 双保险：线程入口退出时也会取消盯梢，这里覆盖"线程还没起来就 stop"的路径
    lr::platform::setExpected(lr::platform::Heartbeat::Playback, false);
}

// ---------------- 播放线程 ----------------

void FramePlayer::threadMain(double from, double to) {
    std::string error;

    if (!video_.seekTo(from, error)) {
        playing_ = false;
        if (endCb_) endCb_(stats_);
        return;
    }
    if (audio_.hasAudio()) startAudio(from);

    const double frameDur = 1.0 / fps_;
    const double wallStepMs = 1000.0 / fps_;
    auto nextWall = std::chrono::steady_clock::now();
    playFrom_ = from;

    // 帧坏了最多顶替多少帧：短片坏一两帧能救回来，整段坏掉就别硬撑
    const int maxRecovery = std::max(15, static_cast<int>(kMaxRecoverySec * fps_));
    int recoveryRun = 0;  // 已经连续用上一帧顶了几帧

    // 按帧率睡到下一帧该出现的时刻；解码本身就慢（掉帧）时不要把 nextWall
    // 越推越远，直接以当前时间为基准重新起算。
    const auto sleepToNext = [&]() {
        nextWall += std::chrono::microseconds(static_cast<long long>(wallStepMs * 1000.0));
        const auto now = std::chrono::steady_clock::now();
        if (nextWall > now) {
            std::this_thread::sleep_until(nextWall);
        } else {
            nextWall = now;
        }
    };

    while (!stopFlag_) {
        // 看门狗心跳（P0-1）：播放线程还活着且在推进
        lr::platform::heartbeat(lr::platform::Heartbeat::Playback);
        lr::FrameGrabber::Frame frame;
        const lr::FrameGrabber::DecodeResult r = video_.grabNext(outW_, outH_, frame, error);

        if (r == lr::FrameGrabber::DecodeResult::EndOfStream) break;  // 真的播完了

        if (r == lr::FrameGrabber::DecodeResult::Error) {
            // 这一帧没解出来 —— 但**播放绝不能就此停下**。
            //
            // 损坏文件里这是家常便饭：一个 GOP 坏了，后面全是好的。
            // 以前的写法把 Error 和 EndOfStream 一视同仁地 break，于是播放
            // 到坏块处就"自然结束"，用户只看到播放莫名停住，还以为软件有毛病。
            // 正确做法是沿用上一张好画面、时间继续往前走，等坏区过去自然接上。
            if (lastGood_ == nullptr || ++recoveryRun > maxRecovery) break;  // 实在救不回来

            FrameData data;
            data.rgb = lastGood_;  // 共享，零拷贝
            data.width = lastGoodW_;
            data.height = lastGoodH_;
            data.sec = lastGoodSec_ + frameDur * recoveryRun;
            ++stats_.videoRecovered;
            pos_ = data.sec;
            if (frameCb_) frameCb_(std::move(data));
            if (audioActive_) pumpAudio(pos_ + 0.25);
            sleepToNext();
            continue;
        }

        recoveryRun = 0;
        if (frame.actualSec > to) break;  // 播过终点了
        // seek 只能落在关键帧上，开头几帧会早于起点（本片关键帧间隔 1 秒，
        // 从 5.9s 起播时第一帧其实是 5.0s 的关键帧）。这些帧必须丢掉，
        // 否则画面会先往回退一大截再往下播。
        if (frame.actualSec < playFrom_ - 1e-6) continue;

        // 存一份共享引用：坏块处要靠它顶上去（不拷贝像素，只是多一个引用）
        std::shared_ptr<const std::vector<uint8_t>> buf =
            std::make_shared<std::vector<uint8_t>>(std::move(frame.rgb));
        lastGood_ = buf;
        lastGoodW_ = frame.width;
        lastGoodH_ = frame.height;
        lastGoodSec_ = frame.actualSec;

        FrameData data;
        data.rgb = std::move(buf);
        data.width = frame.width;
        data.height = frame.height;
        data.sec = frame.actualSec;
        pos_ = frame.actualSec;

        // 回调出去（UI 层负责切回 GUI 线程再画）
        if (frameCb_) frameCb_(std::move(data));

        // 音频补齐到"当前帧再往后 0.25 秒"，留一点余量吸收抖动
        if (audioActive_) pumpAudio(frame.actualSec + 0.25);

        sleepToNext();
    }

    playing_ = false;
    stopAudio();
    // 收尾：把累计计数换算成"本次播放"的增量
    stats_.videoBadBlocks = video_.badBlocks() - videoBadBase_;
    stats_.audioBadBlocks = audio_.badBlocks() - audioBadBase_;
    // 主动 stop() 的那次不汇报：UI 自己发起的停止它自己知道，
    // 这条消息晚到一步会把紧接着的"重播"状态误清掉
    if (endCb_ && !silentStop_) endCb_(stats_);
}

// ---------------- 音频 ----------------

void FramePlayer::startAudio(double fromSec) {
    if (!audioOut_.start(audio_.sampleRate(), audio_.channels())) return;
    std::string error;
    audio_.seekTo(fromSec, error);
    audioSec_ = fromSec;
    audioCutSec_ = fromSec;
    audioActive_ = true;
}

void FramePlayer::stopAudio() {
    if (!audioActive_) return;
    audioActive_ = false;
    audioOut_.stop();  // waveOut 立刻停，剩下的采样不要了（点停止就是想马上停）
}

void FramePlayer::pumpAudio(double untilSec) {
    std::string error;
    const double rate = static_cast<double>(audio_.sampleRate());
    if (rate <= 0.0) return;

    // 有硬上限的循环：正常一次只补几个包（约 0.25 秒的音频）。
    // 但万一上游给的时间戳有问题（比如整轨都没 pts，startSec 恒为负），
    // audioSec_ 就不会往前走 —— 没有这个上限的话，这里会一口气把整条音轨
    // 全部读完，播放线程卡死在这一句上，界面直接假死。
    for (int guard = 0; guard < 512 && audioSec_ < untilSec; ++guard) {
        std::vector<int16_t> pcm;
        double startSec = 0.0;
        const lr::AudioDecoder::DecodeResult r = audio_.decodeNext(pcm, startSec, error);

        if (r == lr::AudioDecoder::DecodeResult::EndOfStream) break;  // 音轨播完了

        if (r == lr::AudioDecoder::DecodeResult::Error) {
            // 音频包坏了：补一段隐藏音（不是干等着），时间继续走。
            // 只有连续坏到一定程度才放弃音轨 —— 画面照播，只是没声音。
            if (++audioBadRun_ >= kMaxAudioBadRun) {
                audioActive_ = false;
                stats_.audioDropped = true;
                break;
            }
            concealAudioDrop(lastAudioDur_);
            continue;
        }
        audioBadRun_ = 0;
        if (pcm.empty()) continue;

        // 少数容器/流的音频包没有时间戳：这时 startSec 是 -1，
        // 不能拿它推进 audioSec_（会变负、且永远追不上 untilSec）。
        // 按"紧接上一段之后"估算一个位置，至少保证循环能往前走。
        if (startSec < 0.0) startSec = audioSec_;

        const int totalFrames = static_cast<int>(pcm.size()) / audio_.channels();
        const double endSec = startSec + double(totalFrames) / rate;

        // seek 时往回退过 0.5 秒，开头会多解出一点，这里裁掉起点之前的部分
        int offset = 0;
        if (startSec < audioCutSec_ - 1e-6) {
            offset = static_cast<int>((audioCutSec_ - startSec) * rate);
        }
        const int frames = totalFrames - offset;
        audioSec_ = endSec;

        lastAudioDur_ = double(totalFrames) / rate;

        // 留一小段尾音：音频包坏掉时用它做丢包隐藏（见 concealAudioDrop）
        const std::size_t wantTail =
            static_cast<std::size_t>(rate * kPlcTailSec) * audio_.channels();
        if (pcm.size() >= wantTail) {
            plcTail_.assign(pcm.end() - static_cast<long>(wantTail), pcm.end());
        } else {
            plcTail_ = pcm;
        }

        if (frames <= 0) continue;
        if (!audioOut_.write(pcm.data() + static_cast<std::size_t>(offset) *
                                              audio_.channels(),
                             frames, 40)) {
            // 声卡堵了：丢掉这段，绝不让音频把播放循环卡住
            break;
        }
    }
}

void FramePlayer::concealAudioDrop(double durationSec) {
    if (!audioActive_) return;
    const double rate = static_cast<double>(audio_.sampleRate());
    if (rate <= 0.0) return;

    const int wanted = static_cast<int>(durationSec * rate);
    const int frames = clampInt(wanted, static_cast<int>(rate * kConcealMinSec),
                                static_cast<int>(rate * kConcealMaxSec));
    std::vector<int16_t> pcm(static_cast<std::size_t>(frames) * audio_.channels(), 0);

    // 丢包隐藏（PLC）：把上一段的尾音搬过来并衰减到 0。
    // 比硬切静音自然得多 —— 静音会在坏块处留下明显的"咔哒"和空洞感，
    // 衰减尾音听着只是声音"闷了一下"，绝大多数人听不出来。
    if (!plcTail_.empty()) {
        const std::size_t tailFrames = plcTail_.size() / audio_.channels();
        const std::size_t n = std::min(static_cast<std::size_t>(frames), tailFrames);
        for (std::size_t i = 0; i < n; ++i) {
            const double g = 1.0 - double(i) / double(n);  // 线性衰减
            for (int c = 0; c < audio_.channels(); ++c) {
                const double s = double(plcTail_[i * audio_.channels() + c]) * g;
                pcm[i * audio_.channels() + c] = static_cast<int16_t>(s);
            }
        }
    }

    audioSec_ += double(frames) / rate;
    ++stats_.audioConcealed;
    audioOut_.write(pcm.data(), frames, 40);
}

}  // namespace lr
