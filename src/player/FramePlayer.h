// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// 播放引擎 —— Tab1 预览窗的「播放 / 停止」。
//
// 设计要点
// --------
// 1) 常驻线程：play() 起一个 std::thread 跑循环，stop() 让它退出。
//    解码是重活，放 GUI 线程会把界面拖死。
//
// 2) 顺序解码：seek 一次，之后一帧一帧往下解。
//    不能每帧都 seek —— 那就是"从关键帧重新解一整个 GOP"，会卡成幻灯片。
//
// 3) 出声：1 倍速播放时同步把音轨解成 PCM 送给声卡。
//    音画同步只认视频时钟，音频是"尽力喂"，声卡堵了就丢，绝不卡住播放循环。
//
// 4) 不依赖 Qt：帧通过回调往外送，由 UI 层切回 GUI 线程。
//
// 注：倍速快进/快退已按需求取消，所以这里没有 speed、也没有倒放用的段缓冲。
// ---------------------------------------------------------------------------

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "audio/AudioOutput.h"
#include "ffmpeg/AudioDecoder.h"
#include "ffmpeg/FfmpegApi.h"
#include "ffmpeg/FrameGrabber.h"
#include "platform/ThreadUtil.h"

namespace lr {

class FramePlayer {
public:
    struct FrameData {
        // 用共享指针而不是值语义：坏块处要"沿用上一帧"把同一份像素再送一次，
        // 1080p 一帧 6MB，每帧拷一份纯属白烧 CPU。共享之后补救帧是零拷贝的。
        std::shared_ptr<const std::vector<uint8_t>> rgb;  // RGB24 紧密排列
        int width = 0;
        int height = 0;
        double sec = 0.0;  // 这一帧在片子里的时间
    };

    /// 本次播放的"诚实汇报"：片子坏了多少、我们补了多少。
    /// 画面是靠补救撑下来的这件事，必须让用户知道，不能装作一切正常。
    struct PlayStats {
        int videoBadBlocks = 0;   // 视频侧跳过的坏包数
        int videoRecovered = 0;   // 用上一帧顶替过去的时间段数
        int audioBadBlocks = 0;   // 音频侧跳过的坏包数
        int audioConcealed = 0;   // 做了丢包隐藏（补衰减尾音）的段数
        bool audioDropped = false;  // 音轨彻底放弃，这次播放是静音的

        bool anyDamage() const {
            return videoBadBlocks > 0 || videoRecovered > 0 || audioBadBlocks > 0 ||
                   audioConcealed > 0 || audioDropped;
        }
    };

    using FrameCb = std::function<void(FrameData &&)>;
    using EndCb = std::function<void(PlayStats)>;

    FramePlayer();
    ~FramePlayer();
    FramePlayer(const FramePlayer &) = delete;
    FramePlayer &operator=(const FramePlayer &) = delete;

    /// fps 传 <= 0 时自动取视频自己的帧率；outW/outH 传 0 表示保持原始分辨率
    bool open(FfmpegApi &api, const std::string &path, double fps, int outW, int outH,
              std::string &error);
    void close();
    bool isOpen() const { return opened_; }

    /// 从 fromSec 播到 toSec（通常 toSec 就是片长）。已在播就先停再重开。
    ///
    /// 播放中改位置不在这里做：上层是"先 stop 再 play"的（用户在影片条上点了
    /// 别处 → 立刻停 → 标尺挪过去 → 立刻从新位置重播）。播放线程每帧都在改标尺，
    /// 在同一条循环里塞 seek 会和"播放推进"打架，看着像点了没反应。
    void play(double fromSec, double toSec);
    void stop();
    bool isPlaying() const { return playing_; }

    double position() const { return pos_; }  ///< 当前播到哪儿（秒）

    bool hasAudioTrack() const { return audio_.hasAudio(); }
    bool audioActive() const { return audioActive_; }

    /// 本次播放累积的损坏统计（播放结束后由 endCb 带出去；播放中也可查）
    const PlayStats &stats() const { return stats_; }

    void setFrameCallback(FrameCb cb) { frameCb_ = std::move(cb); }
    void setEndCallback(EndCb cb) { endCb_ = std::move(cb); }

private:
    void threadMain(double from, double to);
    void pumpAudio(double untilSec);
    void startAudio(double fromSec);
    void stopAudio();
    /// 音频丢包隐藏：坏包处用上一段的尾音衰减补上，比硬切静音好听得多
    void concealAudioDrop(double durationSec);

    FrameGrabber video_;
    AudioDecoder audio_;
    AudioOutput audioOut_;

    /// 播放线程：大栈（P2-12）+ 名字（P1-6）。接口与 std::thread 最小一致
    lr::platform::BigStackThread thread_;
    std::atomic<bool> stopFlag_{false};
    std::atomic<bool> playing_{false};
    std::atomic<double> pos_{0.0};
    std::atomic<bool> audioActive_{false};

    /// 主动 stop() 时为 true：这种情况下不回调 endCb。
    /// UI 是自己按下停止的，它已经知道停了；再送一个"播完了"过去，
    /// 这个消息是排队投递的，可能晚到一步，把紧接着的"重播"状态误清掉。
    std::atomic<bool> silentStop_{false};

    bool opened_ = false;
    double fps_ = 25.0;
    int outW_ = 0;
    int outH_ = 0;

    PlayStats stats_;

    // 播放线程内部状态
    double playFrom_ = 0.0;     // 本次播放/跳转的起点：早于它的帧要丢掉
    // 音频侧状态（只在播放线程里用）
    double audioSec_ = 0.0;     // 已经解码并送出的音频时间
    double audioCutSec_ = 0.0;  // 播放起点：这之前的采样要裁掉（seek 会往回退一点）
    double lastAudioDur_ = 0.02;  // 上一个音频包的时长（坏包按它估算该补多久）
    int audioBadRun_ = 0;   // 音频连续坏包计数（跨 pumpAudio 调用保留）

    // 坏块统计是"从 open 起累计"的，这两个是本次播放开始时的基线
    int videoBadBase_ = 0;
    int audioBadBase_ = 0;

    // ---- 坏块补救用的状态 ----
    std::shared_ptr<const std::vector<uint8_t>> lastGood_;  // 上一张好画面（补救时复用）
    int lastGoodW_ = 0;
    int lastGoodH_ = 0;
    double lastGoodSec_ = 0.0;   // 上一张好画面的时间
    std::vector<int16_t> plcTail_;  // 上一段 PCM 的尾巴（音频丢包隐藏用）

    FrameCb frameCb_;
    EndCb endCb_;
};

}  // namespace lr
