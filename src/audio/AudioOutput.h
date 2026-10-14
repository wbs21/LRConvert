#pragma once
// ---------------------------------------------------------------------------
// 音频输出 —— 把 PCM 丢给声卡。
//
// 为什么不用 Qt Multimedia 的 QAudioSink：
//   · 本项目明确"不引入 Qt Multimedia"（见 Tab1 方案 §8）—— 它会在发布包里
//     带一份 Qt 自己的 FFmpeg 后端（几十 MB），而我们要的只是"把采样播出来"；
//   · 我们只需要 Windows，waveOut 属于 winmm.dll，系统自带，零部署成本。
//
// 接口是"推模式"：write() 把采样推进内部环形缓冲，缓冲满时**阻塞等待**，
// 这天然形成背压，让播放循环跟着声卡走，音画不会越走越偏。
//
// 不依赖 Qt（pimpl 隔开 windows.h，免得污染调用方的命名空间）。
// 非 Windows 平台编译成空实现：能编过，但没有声音。
// ---------------------------------------------------------------------------

#include <cstdint>
#include <memory>
#include <vector>

namespace lr {

class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();
    AudioOutput(const AudioOutput &) = delete;
    AudioOutput &operator=(const AudioOutput &) = delete;

    /// 打开声卡。sampleRate/channels 必须与送进来的 PCM 一致（本项目固定 48k/2ch）
    bool start(int sampleRate, int channels);
    void stop();
    bool isStarted() const;

    /// 写入交错 PCM。缓冲满时最多等 timeoutMs 毫秒；超时返回 false
    /// （调用方直接丢弃这段采样即可，不要因此卡住播放循环）。
    bool write(const int16_t *data, int frames, int timeoutMs = 100);

    /// 等已写入的采样播完（停止前调一次，避免最后半秒被掐掉）
    void drain();

    /// 音量 0~100
    void setVolume(int percent);
    int volume() const { return volume_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool started_ = false;
    int volume_ = 100;
};

}  // namespace lr
