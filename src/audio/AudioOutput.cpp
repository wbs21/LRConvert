#include "audio/AudioOutput.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

#include <cstring>

namespace lr {

#ifdef _WIN32

// ---------------------------------------------------------------------------
// Windows waveOut 实现
//
// 8 个 40ms 的缓冲轮着用：够吸收抖动，又不会让"停止"等待太久
// （停止时最多等 8×40 = 320ms 的已排队音频播完，或者 drain() 显式等）。
// ---------------------------------------------------------------------------
namespace {
constexpr int kBufferCount = 8;
constexpr int kBufferMs = 40;
}  // namespace

struct AudioOutput::Impl {
    HWAVEOUT handle = nullptr;
    HANDLE event = nullptr;
    WAVEHDR headers[kBufferCount] = {};
    std::vector<int16_t> bufs[kBufferCount];
    int sampleRate = 0;
    int channels = 0;
    int framesPerBuffer = 0;
    int next = 0;  // 下一个要用的缓冲
    bool opened = false;
};

AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}
AudioOutput::~AudioOutput() { stop(); }

bool AudioOutput::start(int sampleRate, int channels) {
    if (impl_->opened) return true;
    if (sampleRate <= 0 || channels <= 0) return false;

    impl_->sampleRate = sampleRate;
    impl_->channels = channels;
    impl_->framesPerBuffer = sampleRate * kBufferMs / 1000;
    if (impl_->framesPerBuffer < 128) impl_->framesPerBuffer = 128;

    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = static_cast<WORD>(channels);
    fmt.nSamplesPerSec = static_cast<DWORD>(sampleRate);
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = static_cast<WORD>(channels * 2);
    fmt.nAvgBytesPerSec = static_cast<DWORD>(sampleRate * channels * 2);
    fmt.cbSize = 0;

    impl_->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (impl_->event == nullptr) return false;

    const MMRESULT ret = waveOutOpen(&impl_->handle, WAVE_MAPPER, &fmt,
                                     reinterpret_cast<DWORD_PTR>(impl_->event), 0,
                                     CALLBACK_EVENT);
    if (ret != MMSYSERR_NOERROR) {
        CloseHandle(impl_->event);
        impl_->event = nullptr;
        impl_->handle = nullptr;
        return false;
    }

    for (int i = 0; i < kBufferCount; ++i) {
        impl_->bufs[i].assign(static_cast<std::size_t>(impl_->framesPerBuffer) *
                                  static_cast<std::size_t>(channels),
                              0);
        std::memset(&impl_->headers[i], 0, sizeof(WAVEHDR));
        impl_->headers[i].lpData = reinterpret_cast<LPSTR>(impl_->bufs[i].data());
        impl_->headers[i].dwBufferLength =
            static_cast<DWORD>(impl_->framesPerBuffer * channels * 2);
        impl_->headers[i].dwFlags = WHDR_DONE;  // 初始即可用
    }

    impl_->opened = true;
    started_ = true;
    setVolume(volume_);  // 沿用上次设过的音量，别每次 start 都跳回满音量
    return true;
}

void AudioOutput::stop() {
    if (!impl_->opened) return;
    impl_->opened = false;
    started_ = false;

    waveOutReset(impl_->handle);  // 立刻停下并归还所有缓冲
    for (int i = 0; i < kBufferCount; ++i) {
        if (impl_->headers[i].dwFlags & WHDR_PREPARED) {
            waveOutUnprepareHeader(impl_->handle, &impl_->headers[i],
                                   sizeof(WAVEHDR));
        }
    }
    waveOutClose(impl_->handle);
    impl_->handle = nullptr;
    if (impl_->event != nullptr) {
        CloseHandle(impl_->event);
        impl_->event = nullptr;
    }
    impl_->next = 0;
}

bool AudioOutput::isStarted() const { return impl_->opened; }

bool AudioOutput::write(const int16_t *data, int frames, int timeoutMs) {
    if (!impl_->opened || data == nullptr || frames <= 0) return false;

    const int framesPerBuffer = impl_->framesPerBuffer;
    int written = 0;

    while (written < frames) {
        WAVEHDR &hdr = impl_->headers[impl_->next];
        if (!(hdr.dwFlags & WHDR_DONE)) {
            // 缓冲都占着：等一块播完。等不到就放弃剩下的（宁可丢一点声音，
            // 也不能让播放循环卡死在音频上）
            WaitForSingleObject(impl_->event, static_cast<DWORD>(timeoutMs));
            if (!(hdr.dwFlags & WHDR_DONE)) return written > 0;
        }

        if (hdr.dwFlags & WHDR_PREPARED) {
            waveOutUnprepareHeader(impl_->handle, &hdr, sizeof(WAVEHDR));
        }

        const int room = framesPerBuffer;
        const int todo = (frames - written < room) ? (frames - written) : room;
        const int samples = todo * impl_->channels;
        std::memcpy(impl_->bufs[impl_->next].data(), data + static_cast<std::size_t>(written) *
                                                              impl_->channels,
                    static_cast<std::size_t>(samples) * sizeof(int16_t));
        hdr.dwBufferLength = static_cast<DWORD>(samples * 2);
        hdr.dwFlags = 0;

        if (waveOutPrepareHeader(impl_->handle, &hdr, sizeof(WAVEHDR)) !=
            MMSYSERR_NOERROR) {
            return written > 0;
        }
        if (waveOutWrite(impl_->handle, &hdr, sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(impl_->handle, &hdr, sizeof(WAVEHDR));
            hdr.dwFlags = WHDR_DONE;
            return written > 0;
        }

        written += todo;
        impl_->next = (impl_->next + 1) % kBufferCount;
    }
    return true;
}

void AudioOutput::drain() {
    if (!impl_->opened) return;
    // 等所有缓冲都播完（WHDR_DONE 置位）
    for (int guard = 0; guard < 200; ++guard) {
        bool anyBusy = false;
        for (int i = 0; i < kBufferCount; ++i) {
            if (!(impl_->headers[i].dwFlags & WHDR_DONE)) anyBusy = true;
        }
        if (!anyBusy) break;
        WaitForSingleObject(impl_->event, 20);
    }
}

void AudioOutput::setVolume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    volume_ = percent;
    if (!impl_->opened) return;
    // waveOut 的音量是左右声道各 16 位；这里整体设成同一个百分比
    const DWORD v = static_cast<DWORD>((percent * 0xFFFF) / 100);
    waveOutSetVolume(impl_->handle, (v << 16) | v);
}

#else  // !_WIN32 —— 非 Windows：空实现，保证代码能编过

struct AudioOutput::Impl {};

AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}
AudioOutput::~AudioOutput() = default;
bool AudioOutput::start(int, int) { return false; }
void AudioOutput::stop() {}
bool AudioOutput::isStarted() const { return false; }
bool AudioOutput::write(const int16_t *, int, int) { return false; }
void AudioOutput::drain() {}
void AudioOutput::setVolume(int percent) {
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    volume_ = percent;
}

#endif

}  // namespace lr
