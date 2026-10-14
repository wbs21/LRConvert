#pragma once
// ---------------------------------------------------------------------------
// 取帧器 —— 从视频里取出"某个时间点"的画面，输出紧密排列的 RGB24。
//
// 为什么自己写而不用 Qt Multimedia：
//   · Qt Multimedia 会带一份自己的 FFmpeg 后端，发布包凭空多出几十 MB；
//   · 本项目已经运行时加载了 libav*，解码走同一套符号表即可，零额外部署。
//
// 用法：open() 一次，之后反复 grabAt() 取不同时间点的帧（缩略图 3 张、
// 拖动预览都靠它）。内部保持文件与解码器打开，避免每次重新开文件。
//
// 不依赖 Qt（输出裸 RGB 缓冲，由 UI 层转成 QImage）。
// ---------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

#include "ffmpeg/FfmpegApi.h"

namespace lr {

class FrameGrabber {
public:
    FrameGrabber() = default;
    ~FrameGrabber();
    FrameGrabber(const FrameGrabber &) = delete;
    FrameGrabber &operator=(const FrameGrabber &) = delete;

    struct Frame {
        std::vector<uint8_t> rgb;  // 紧密排列的 RGB24（无行对齐填充）
        int width = 0;
        int height = 0;
        double actualSec = 0.0;  // 实际取到的帧所在时间（可能与请求时间略有差别）
    };

    bool open(FfmpegApi &api, const std::string &path, std::string &error);
    void close();
    bool isOpen() const { return fmt_ != nullptr; }

    /// 取 tSec 处的一帧。outW/outH > 0 时缩放到该尺寸（缩略图用），
    /// 否则保持原始分辨率（预览窗用）。
    /// 实现上就是 seekTo() + 连续 grabNext() 直到跨过目标时间。
    bool grabAt(double tSec, int outW, int outH, Frame &out, std::string &error);

    /// 只做 seek + 清解码器，不解任何帧。连续播放前调一次即可。
    bool seekTo(double tSec, std::string &error);

    /// 顺序解出**下一帧**（不 seek）。文件读完返回 false。
    /// 播放就靠它：seekTo 一次，然后反复 grabNext，比每帧都 grabAt 快得多
    /// （grabAt 每帧都要从关键帧重新解码一遍整段 GOP）。
    bool grabNext(int outW, int outH, Frame &out, std::string &error);

    // ---- 播放用的两段式接口（跳帧必需）----
    //
    // 16 倍速时每显示 1 帧要解码 16 帧，如果每帧都做缩放 + RGB 拷贝，
    // 光转换就把 CPU 吃光了。所以拆成两步：先只解码拿到时间戳，
    // 调用方判断这帧要不要显示，要就 takeFrame()（此时才转换），
    // 不要就 dropFrame()（直接丢，零转换开销）。

    /// 解码下一帧但**不做转换**，帧暂存在内部，secOut 返回它的时间。
    /// 取用前必须调 takeFrame() 或 dropFrame()，否则下一帧会把它顶掉。
    bool decodeNext(double &secOut, std::string &error);

    /// 把 decodeNext() 暂存的那一帧转换输出（outW/outH 同 grabAt）。
    bool takeFrame(int outW, int outH, Frame &out, std::string &error);

    /// 丢弃暂存的那一帧（跳帧时用）。
    void dropFrame();

    double fps() const { return fps_; }

private:
    void release();
    bool convertFrame(AVFrame *src, int outW, int outH, double actualSec, Frame &out,
                      std::string &error);
    void flushDecoder();

    FfmpegApi *api_ = nullptr;
    AVFormatContext *fmt_ = nullptr;
    AVCodecContext *codec_ = nullptr;
    AVFrame *frame_ = nullptr;   // 解码输出
    AVFrame *rgb_ = nullptr;     // 缩放/格式转换输出
    AVPacket *packet_ = nullptr;
    SwsContext *sws_ = nullptr;
    int videoIdx_ = -1;
    bool frameHeld_ = false;  // decodeNext() 之后有没有一帧等着被取走
    double heldSec_ = 0.0;    // 那一帧的时间
    double timeBase_ = 0.0;  // 秒 / 时间戳单位
    double fps_ = 25.0;
    int srcW_ = 0;
    int srcH_ = 0;
    int srcPixFmt_ = -1;
    int rgbW_ = 0;
    int rgbH_ = 0;
};

}  // namespace lr
