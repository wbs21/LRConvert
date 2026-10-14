// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// Tab1 的影片显示区（时间轴）—— 一个自绘 QWidget，纵向 4 层：
//
    //   ① 缩略图层   5 张：可视区间 左 / 1/4 / 中 / 3/4 / 右（高度贴合条高，左右留空隙）
    //   ② 波形层     灰色，可见区间切 600 片，片内用该片高度填满
//   ③ 时间码层   大刻度（带时间文字）+ 小刻度，密度随缩放自适应
//   ④ 滑块层     浅蓝底条（全片，不可拖）+ 深蓝滑块（可视区间，拖动平移）
//
//   ①②③ 之上叠三条竖向标尺：绿=起点、红=终点（带手柄可拖）、蓝=当前帧
//
// 控件只管"显示与交互"，不做解码、不做关键帧吸附——取帧与吸附交给上层
// （MainWindow），通过信号汇报"用户改了什么"。
// ---------------------------------------------------------------------------

#include <array>
#include <vector>

#include <QImage>
#include <QWidget>

#include "ffmpeg/AudioPeaks.h"

namespace lr::ui {

class TimelineWidget : public QWidget {
    Q_OBJECT
public:
    /// 缩略图张数：可视区间 左 / 1/4 / 中 / 3/4 / 右
    static constexpr int kThumbCount = 5;

    explicit TimelineWidget(QWidget *parent = nullptr);

    // ---- 数据注入（全部来自上层后台线程） ----
    void setMedia(double durationSec, double fps);
    /// 是否有音轨（决定波形区显示"无音轨"还是"计算中"）
    void setAudioAvailable(bool hasAudio);
    void setKeyframes(std::vector<double> times);
    /// 整轨全量波形是**共享**的（后台线程还在往里写，UI 边算边画），
    /// 所以这里持有 shared_ptr 而不是拷贝一份 —— 一小时片子的整轨峰值有
    /// 好几 MB，每算完一段拷一次太贵。
    void setPeaks(std::shared_ptr<const AudioPeaks> peaks);
    void setThumbnails(const std::array<QImage, kThumbCount> &images);
    void clearMedia();

    // ---- 状态读写 ----
    bool hasMedia() const { return hasMedia_; }
    /// 冒烟测试用：模拟"按住深蓝滑块再拖到某处"（坐标用控件宽度比例 0..1），
    /// 用来验证平移时窗口长度不变
    void smokeDragView(double pressFrac, double moveFrac);
    /// 冒烟测试用：模拟"在影片条上点一下"—— 走和真实鼠标完全一样的
    /// cursorChanged → cursorSettled 两步，用来验证播放中被点击时的重播链路
    void smokeClickCursor(double sec);
    /// 已设置的缩略图张数（0..5）——冒烟测试用
    int thumbnailCount() const;
    bool hasPeaks() const { return peaks_ != nullptr && !peaks_->empty(); }
    std::size_t peakCount() const { return peaks_ ? peaks_->size() : 0; }
    double durationSec() const { return duration_; }
    double startSec() const { return start_; }
    double endSec() const { return end_; }
    double cursorSec() const { return cursor_; }
    double viewFrom() const { return viewFrom_; }
    double viewTo() const { return viewTo_; }
    const std::vector<double> &keyframes() const { return keyframes_; }

    void setStartSec(double t);
    void setEndSec(double t);
    void setCursorSec(double t);
    /// 直接设定可见区间（冒烟测试用）
    void setViewRange(double from, double to);
    void zoomToFit();

    /// 播放时每帧调一次：**翻页** —— 页内不动视窗，播放头自己从左往右走；
    /// 走到右端（或被挪到页外）才整页向左跳一下，播放头随即出现在新页最左端。
    /// 片尾处钳住，播放头在最后一页里继续往右走到片子结束。
    void pageViewForCursor();

signals:
    void cursorChanged(double sec);               // 蓝标尺移动中（上层刷新预览，需节流）
    void cursorSettled(double sec);               // 蓝标尺松手（精确 seek）
    void rangeChanged(double start, double end);  // 起终点拖动中
    void rangeSettled(double start, double end);  // 起终点松手（上层做关键帧吸附）
    void viewChanged(double from, double to);     // 缩放/平移后需要重取缩略图与波形
    /// 用户亲手在**底部滑块层**上操作了（拖动深蓝滑块 / 点底条跳转 / 滚轮缩放）。
    /// 与 viewChanged 的区别：播放翻页时控件自己也会挪视窗并发 viewChanged，
    /// 那种是"程序改的"，不能当成"用户在干预"，所以另开一个信号让上层据此停播。
    void viewDraggedByUser();

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    QSize sizeHint() const override;

private:
    struct Layout {
        int thumbTop = 0;
        int thumbH = 0;
        int waveTop = 0;
        int waveH = 0;
        int rulerTop = 0;
        int rulerH = 0;
        int sliderTop = 0;
        int sliderH = 0;
    };

    enum class Drag { None, Cursor, Start, End, View };

    Layout layout() const;
    int plotTop() const;
    int plotBottom() const;
    double span() const { return viewTo_ - viewFrom_; }
    double timeToX(double t) const;
    double xToTime(int x) const;
    double xToFullTime(int x) const;  // 底部滑块用的"全片"映射
    void clampView();
    /// 深蓝滑块实际画出来的矩形（逻辑区间太窄时给一个最小宽度，保证抓得住）
    QRect sliderThumbRect() const;

    void drawSelection(QPainter &p, const Layout &l);
    void drawThumbnails(QPainter &p, const Layout &l);
    void drawWaveform(QPainter &p, const Layout &l);
    void drawRuler(QPainter &p, const Layout &l);
    void drawMarkers(QPainter &p, const Layout &l);
    void drawSlider(QPainter &p, const Layout &l);
    /// 等比缩放居中画进 target：不裁切，装不满的地方留空隙
    void drawImageFitted(QPainter &p, const QImage &img, const QRect &target);

    /// [t0, t1) 区间内的最大绝对振幅（0..32767）
    int peakLevel(double t0, double t1) const;
    /// 当前峰值数据是否已覆盖可见区间（没覆盖说明要重算，先画占位）
    bool peaksCoverView() const;
    void setCursorFromX(int x, bool settled);
    void beginViewDrag(int x);   ///< 记住抓点相对窗口左端的偏移
    void panViewTo(int x);       ///< 整体平移到新位置（窗口长度不变）

    double duration_ = 0.0;
    double fps_ = 25.0;
    bool hasMedia_ = false;
    bool hasAudio_ = false;
    std::vector<double> keyframes_;
    std::shared_ptr<const AudioPeaks> peaks_;  ///< 与后台线程共享的整轨峰值
    std::array<QImage, kThumbCount> thumb_;

    double viewFrom_ = 0.0;
    double viewTo_ = 0.0;
    double start_ = 0.0;
    double end_ = 0.0;
    double cursor_ = 0.0;

    Drag drag_ = Drag::None;
    double dragOffset_ = 0.0;
};

}  // namespace lr::ui
