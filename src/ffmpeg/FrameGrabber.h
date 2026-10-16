// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

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

#include "ffmpeg/FfObjects.h"
#include "ffmpeg/FfmpegApi.h"

namespace lr {

class FrameGrabber {
public:
    FrameGrabber() = default;
    ~FrameGrabber();
    FrameGrabber(const FrameGrabber &) = delete;
    FrameGrabber &operator=(const FrameGrabber &) = delete;
    /// 可移动：导入新文件时在本地对象上开文件 + 取帧，就绪后整体换进
    /// 共享对象（锁内只做指针交换，见 MainWindow 导入路径与 P2-10）
    FrameGrabber(FrameGrabber &&other) noexcept;
    FrameGrabber &operator=(FrameGrabber &&other) noexcept;

    struct Frame {
        std::vector<uint8_t> rgb;  // 紧密排列的 RGB24（无行对齐填充）
        int width = 0;
        int height = 0;
        double actualSec = 0.0;  // 实际取到的帧所在时间（可能与请求时间略有差别）
    };

    /// 解码动作的三种结局 —— 必须区分开，不能一律当成"读完了"。
    ///
    /// 损坏文件里最常见的是 EndOfStream 与 Error 被混为一谈：
    /// 一帧解不出就当播完，表现是"播放莫名停住"。播放循环要靠这个
    /// 决定是继续（沿用上一帧）还是真的结束。
    enum class DecodeResult {
        Ok,           ///< 解出一帧
        EndOfStream,  ///< 文件正常读完，后面没有数据了
        Error,        ///< 数据损坏 / 参数异常，这一帧拿不到，但后面可能还有
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

    /// 顺序解出**下一帧**（不 seek）。播放就靠它：seekTo 一次，然后反复
    /// grabNext，比每帧都 grabAt 快得多（grabAt 每帧都要从关键帧重新解码一遍
    /// 整段 GOP）。
    /// 返回 DecodeResult：EndOfStream 才是真的播完，Error 是"这一帧坏了"。
    DecodeResult grabNext(int outW, int outH, Frame &out, std::string &error);

    // ---- 播放用的两段式接口（跳帧必需）----
    //
    // 16 倍速时每显示 1 帧要解码 16 帧，如果每帧都做缩放 + RGB 拷贝，
    // 光转换就把 CPU 吃光了。所以拆成两步：先只解码拿到时间戳，
    // 调用方判断这帧要不要显示，要就 takeFrame()（此时才转换），
    // 不要就 dropFrame()（直接丢，零转换开销）。

    /// 解码下一帧但**不做转换**，帧暂存在内部，secOut 返回它的时间。
    /// 取用前必须调 takeFrame() 或 dropFrame()，否则下一帧会把它顶掉。
    DecodeResult decodeNext(double &secOut, std::string &error);

    /// 把 decodeNext() 暂存的那一帧转换输出（outW/outH 同 grabAt）。
    bool takeFrame(int outW, int outH, Frame &out, std::string &error);

    /// 丢弃暂存的那一帧（跳帧时用）。
    void dropFrame();

    double fps() const { return fps_; }

    /// 累计跳过的坏包数（open() 之后一直累加，不清零）。
    /// 给上层做"播放结束时的诚实汇报"用：画面是补出来的，得让用户知道。
    int badBlocks() const { return badBlocks_; }
    void resetBadBlocks() { badBlocks_ = 0; }

private:
    void release();
    bool convertFrame(AVFrame *src, int outW, int outH, double actualSec, Frame &out,
                      std::string &error);
    void flushDecoder();
    /// 连续坏区里的"向前跳"：整段坏了就没必要一个一个包读下去，直接 seek 越过
    bool skipBrokenRegion();

    FfmpegApi *api_ = nullptr;
    /// libav* 对象全部 RAII 化（P2-11）：出了作用域自动释放，
    /// 错误分支/异常路径不用再逐个保证调到 release
    FfInput fmt_;
    FfCodec codec_;
    FfFrame frame_;   ///< 解码输出
    FfFrame rgb_;     ///< 缩放/格式转换输出
    FfPacket packet_;
    FfSws sws_;
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

    // ---- 损坏数据的容错状态（会话级，跨 decodeNext 调用累加）----
    //
    // 原来 badPackets 是 decodeNext() 里的局部变量，每次调用都从 0 开始数。
    // 碰上"中间一大段全坏"的文件，外层 1500 帧循环会一遍遍进来、每遍读几十个
    // 坏包再退出 —— 不会死循环，但白白烧 CPU，界面看着像卡住。
    // 改成成员之后：连续坏包在**整个播放过程**里累计，攒够就直接跳过去。
    int badBlocks_ = 0;        // 跳过的坏包总数（只增，供上层汇报）
    int badRun_ = 0;           // 当前这串连续坏包的长度
    int regionSkips_ = 0;      // 已经做过几次"向前跳"（有上限，防止来回横跳）
};

}  // namespace lr
