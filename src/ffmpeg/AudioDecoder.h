// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// 音频解码器 —— 把音轨解成可以直接喂给声卡的 PCM。
//
// 输出格式固定为 48kHz / 立体声 / s16 交错（waveOut 与绝大多数声卡都吃这个），
// 源是什么采样率/声道数由 swr 重采样统一过来，上层不用管。
//
// 与 FrameGrabber 一样：不依赖 Qt，符号全部走 FfmpegApi 的函数指针表。
//
// 用法：open() → seekTo() → 反复 decodeNext()（顺序读，不回退）。
// 播放时它与 FrameGrabber 各开各的文件句柄 —— av_read_frame 是"读走一个包"，
// 两个消费者共用同一个 AVFormatContext 会互相偷包。
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

#include "ffmpeg/FfObjects.h"
#include "ffmpeg/FfmpegApi.h"

namespace lr {

class AudioDecoder {
public:
    static constexpr int kOutRate = 48000;      // 输出采样率
    static constexpr int kOutChannels = 2;      // 输出声道（立体声）

    AudioDecoder() = default;
    ~AudioDecoder();
    AudioDecoder(const AudioDecoder &) = delete;
    AudioDecoder &operator=(const AudioDecoder &) = delete;

    /// 打开文件。文件没有音轨时**也算成功**，只是 hasAudio() 为 false
    /// （无音轨的视频照样能播画面）。
    bool open(FfmpegApi &api, const std::string &path, std::string &error);
    void close();
    bool isOpen() const { return fmt_ != nullptr; }
    bool hasAudio() const { return audioIdx_ >= 0 && codec_ != nullptr && swr_ != nullptr; }

    int sampleRate() const { return kOutRate; }
    int channels() const { return kOutChannels; }

    /// seek 到 tSec（音频只能落到关键位置，实际解出来的起点可能略早于 tSec，
    /// 由调用方按 startSec 裁掉前面多出来的样本）。
    bool seekTo(double tSec, std::string &error);

    /// 与 FrameGrabber::DecodeResult 同义：EndOfStream 是正常读完，
    /// Error 是"这一个音频包坏了，后面可能还有好的" —— 播放时遇到 Error 要补一段
    /// 静音（或做丢包隐藏）继续往下走，绝不能当成播完而停掉。
    enum class DecodeResult { Ok, EndOfStream, Error };

    /// 顺序解出下一个包里的全部 PCM。startSec = 这段 PCM 对应的起始时间。
    DecodeResult decodeNext(std::vector<int16_t> &pcm, double &startSec, std::string &error);

    /// 累计跳过的坏包数（open() 之后一直累加）
    int badBlocks() const { return badBlocks_; }
    void resetBadBlocks() { badBlocks_ = 0; }

private:
    void release();

    FfmpegApi *api_ = nullptr;
    /// libav* 对象 RAII 化（P2-11）
    FfInput fmt_;
    FfCodec codec_;
    FfFrame frame_;
    FfPacket packet_;
    FfSwr swr_;
    uint8_t *buffer_ = nullptr;   // swr 的输出缓冲（s16 交错）
    int bufferSamples_ = 0;       // 缓冲能装多少个"帧"（一帧 = kOutChannels 个 s16）
    int audioIdx_ = -1;
    double timeBase_ = 0.0;       // 秒 / 时间戳单位

    // 与视频侧同款：坏包计数放在成员里才是"连续"的
    int badBlocks_ = 0;
    int badRun_ = 0;
};

}  // namespace lr
