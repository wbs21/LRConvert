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
#include <string>
#include <thread>
#include <vector>

#include "audio/AudioOutput.h"
#include "ffmpeg/AudioDecoder.h"
#include "ffmpeg/FfmpegApi.h"
#include "ffmpeg/FrameGrabber.h"

namespace lr {

class FramePlayer {
public:
    struct FrameData {
        std::vector<uint8_t> rgb;  // RGB24 紧密排列
        int width = 0;
        int height = 0;
        double sec = 0.0;  // 这一帧在片子里的时间
    };

    using FrameCb = std::function<void(FrameData &&)>;
    using EndCb = std::function<void()>;

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

    void setFrameCallback(FrameCb cb) { frameCb_ = std::move(cb); }
    void setEndCallback(EndCb cb) { endCb_ = std::move(cb); }

private:
    void threadMain(double from, double to);
    void pumpAudio(double untilSec);
    void startAudio(double fromSec);
    void stopAudio();

    FrameGrabber video_;
    AudioDecoder audio_;
    AudioOutput audioOut_;

    std::thread thread_;
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

    // 播放线程内部状态
    double playFrom_ = 0.0;     // 本次播放/跳转的起点：早于它的帧要丢掉
    // 音频侧状态（只在播放线程里用）
    double audioSec_ = 0.0;     // 已经解码并送出的音频时间
    double audioCutSec_ = 0.0;  // 播放起点：这之前的采样要裁掉（seek 会往回退一点）

    FrameCb frameCb_;
    EndCb endCb_;
};

}  // namespace lr
