// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "player/FramePlayer.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace lr {

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
    thread_ = std::thread([this, fromSec, toSec]() {
        // ⚠ 这层 try 是保命的：解码、sws 缩放、vector 分配都可能抛异常
        // （损坏文件、异常分辨率、内存不足），一旦让异常逃出线程函数，
        // C++ 运行时会直接 std::terminate() —— 整个进程瞬间消失，
        // 没有日志、没有崩溃对话框。宁可安静地当成"播完了"。
        try {
            threadMain(fromSec, toSec);
        } catch (...) {
            playing_ = false;
            stopAudio();
            if (endCb_ && !silentStop_) endCb_();
        }
    });
}

void FramePlayer::stop() {
    stopFlag_ = true;
    silentStop_ = true;  // 是我让人家停的，不要再回调"播完了"
    if (thread_.joinable()) thread_.join();
    playing_ = false;
    stopAudio();
}

// ---------------- 播放线程 ----------------

void FramePlayer::threadMain(double from, double to) {
    std::string error;

    if (!video_.seekTo(from, error)) {
        playing_ = false;
        if (endCb_) endCb_();
        return;
    }
    if (audio_.hasAudio()) startAudio(from);

    const double wallStepMs = 1000.0 / fps_;
    auto nextWall = std::chrono::steady_clock::now();
    playFrom_ = from;

    while (!stopFlag_) {
        lr::FrameGrabber::Frame frame;
        if (!video_.grabNext(outW_, outH_, frame, error)) break;  // 读完了
        if (frame.actualSec > to) break;                          // 播过终点了
        // seek 只能落在关键帧上，开头几帧会早于起点（本片关键帧间隔 1 秒，
        // 从 5.9s 起播时第一帧其实是 5.0s 的关键帧）。这些帧必须丢掉，
        // 否则画面会先往回退一大截再往下播。
        if (frame.actualSec < playFrom_ - 1e-6) continue;

        FrameData data;
        data.rgb = std::move(frame.rgb);
        data.width = frame.width;
        data.height = frame.height;
        data.sec = frame.actualSec;
        pos_ = frame.actualSec;

        // 回调出去（UI 层负责切回 GUI 线程再画）
        if (frameCb_) frameCb_(std::move(data));

        // 音频补齐到"当前帧再往后 0.25 秒"，留一点余量吸收抖动
        if (audioActive_) pumpAudio(frame.actualSec + 0.25);

        // 按帧率睡到下一帧该出现的时刻；如果解码本身就慢（掉帧），
        // 不要把 nextWall 越推越远，直接以当前时间为基准重新起算。
        nextWall += std::chrono::microseconds(static_cast<long long>(wallStepMs * 1000.0));
        const auto now = std::chrono::steady_clock::now();
        if (nextWall > now) {
            std::this_thread::sleep_until(nextWall);
        } else {
            nextWall = now;
        }
    }

    playing_ = false;
    stopAudio();
    // 主动 stop() 的那次不汇报：UI 自己发起的停止它自己知道，
    // 这条消息晚到一步会把紧接着的"重播"状态误清掉
    if (endCb_ && !silentStop_) endCb_();
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
        if (!audio_.decodeNext(pcm, startSec, error)) break;
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

        if (frames <= 0) continue;
        if (!audioOut_.write(pcm.data() + static_cast<std::size_t>(offset) *
                                              audio_.channels(),
                             frames, 40)) {
            // 声卡堵了：丢掉这段，绝不让音频把播放循环卡住
            break;
        }
    }
}

}  // namespace lr
