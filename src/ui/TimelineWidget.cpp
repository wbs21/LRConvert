// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "ui/TimelineWidget.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace lr::ui {
namespace {

constexpr int kThumbHeight = 64;
constexpr int kWaveHeight = 56;
constexpr int kRulerHeight = 22;
constexpr int kSliderHeight = 12;
constexpr int kSliderGap = 2;
constexpr int kWaveSlices = 600;    // 需求：可见区间切 600 片，片内用该片高度填满
constexpr int kMarkerHitPx = 10;    // 标尺手柄的命中宽度（太窄会抓不住）
constexpr int kThumbMinPx = 16;     // 深蓝滑块的最小宽度（放大很深时才抓得住）（太窄会抓不住）

// 配色（多条蓝色同屏，靠明度分层避免打架）
const QColor kWaveColor(0x8A, 0x8F, 0x98);        // 波形：灰
const QColor kSelectionColor(0x42, 0xA5, 0xF5);   // 选中区间蒙层：淡蓝
const QColor kTrackColor(0xBB, 0xDE, 0xFB);       // 底条：浅蓝
const QColor kThumbColor(0x15, 0x65, 0xC0);       // 视窗滑块：深蓝
const QColor kCursorColor(0x2F, 0x80, 0xED);      // 播放头：亮蓝
const QColor kStartColor(0x2E, 0x7D, 0x32);       // 起点：绿
const QColor kEndColor(0xD3, 0x2F, 0x2F);         // 终点：红

/// 刻度步长候选：选出"主刻度间距 >= 70px"的那一档
const double kTickSteps[] = {0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60,
                             120,  300,  600,  900, 1800, 3600};

QString formatTick(double t, double step) {
    const int totalMs = static_cast<int>(std::llround(t * 1000.0));
    const int ms = totalMs % 1000;
    const int totalSec = totalMs / 1000;
    const int s = totalSec % 60;
    const int m = (totalSec / 60) % 60;
    const int h = totalSec / 3600;
    const bool needMs = step < 1.0;
    if (h > 0) {
        return needMs ? QString::asprintf("%d:%02d:%02d.%03d", h, m, s, ms)
                      : QString::asprintf("%d:%02d:%02d", h, m, s);
    }
    return needMs ? QString::asprintf("%02d:%02d.%03d", m, s, ms)
                  : QString::asprintf("%02d:%02d", m, s);
}

}  // namespace

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent) {
    setMinimumHeight(kThumbHeight + kWaveHeight + kRulerHeight + kSliderGap + kSliderHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(false);
    setCursor(Qt::ArrowCursor);
}

QSize TimelineWidget::sizeHint() const { return QSize(640, minimumHeight()); }

// ---------------- 布局与坐标换算 ----------------

TimelineWidget::Layout TimelineWidget::layout() const {
    Layout l;
    l.thumbTop = 0;
    l.thumbH = kThumbHeight;
    l.waveTop = l.thumbTop + l.thumbH;
    l.waveH = kWaveHeight;
    l.rulerTop = l.waveTop + l.waveH;
    l.rulerH = kRulerHeight;
    l.sliderTop = l.rulerTop + l.rulerH + kSliderGap;
    l.sliderH = kSliderHeight;
    return l;
}

int TimelineWidget::plotTop() const {
    const Layout l = layout();
    return l.thumbTop;
}

int TimelineWidget::plotBottom() const {
    const Layout l = layout();
    return l.rulerTop + l.rulerH;
}

double TimelineWidget::timeToX(double t) const {
    if (span() <= 0.0) return 0.0;
    return (t - viewFrom_) / span() * double(width());
}

double TimelineWidget::xToTime(int x) const {
    if (width() <= 0) return viewFrom_;
    return viewFrom_ + double(x) / double(width()) * span();
}

double TimelineWidget::xToFullTime(int x) const {
    if (width() <= 0 || duration_ <= 0.0) return 0.0;
    return double(x) / double(width()) * duration_;
}

void TimelineWidget::clampView() {
    if (duration_ <= 0.0) return;
    double s = span();
    if (s <= 0.0) s = duration_;
    if (s >= duration_) {
        viewFrom_ = 0.0;
        viewTo_ = duration_;
        return;
    }
    if (viewFrom_ < 0.0) {
        viewFrom_ = 0.0;
        viewTo_ = s;
    }
    if (viewTo_ > duration_) {
        viewTo_ = duration_;
        viewFrom_ = duration_ - s;
    }
}

// ---------------- 数据注入 ----------------

void TimelineWidget::setMedia(double durationSec, double fps) {
    duration_ = std::max(0.0, durationSec);
    fps_ = (fps > 0.0) ? fps : 25.0;
    hasMedia_ = duration_ > 0.0;
    hasAudio_ = false;
    keyframes_.clear();
    peaks_.reset();
    thumb_.fill(QImage());
    viewFrom_ = 0.0;
    viewTo_ = duration_;
    start_ = 0.0;
    end_ = duration_;
    cursor_ = 0.0;
    update();
    if (hasMedia_) emit viewChanged(viewFrom_, viewTo_);
}

int TimelineWidget::thumbnailCount() const {
    int n = 0;
    for (const QImage &img : thumb_) {
        if (!img.isNull()) ++n;
    }
    return n;
}

void TimelineWidget::setKeyframes(std::vector<double> times) {
    keyframes_ = std::move(times);
    update();
}

void TimelineWidget::setPeaks(std::shared_ptr<const AudioPeaks> peaks) {
    peaks_ = std::move(peaks);
    update();
}

void TimelineWidget::setThumbnails(const std::array<QImage, kThumbCount> &images) {
    thumb_ = images;
    update();
}

void TimelineWidget::setAudioAvailable(bool hasAudio) { hasAudio_ = hasAudio; }

void TimelineWidget::clearMedia() {
    hasMedia_ = false;
    hasAudio_ = false;
    duration_ = 0.0;
    keyframes_.clear();
    peaks_.reset();
    thumb_.fill(QImage());
    update();
}

void TimelineWidget::setStartSec(double t) {
    start_ = std::clamp(t, 0.0, std::max(0.0, duration_));
    update();
}

void TimelineWidget::setEndSec(double t) {
    end_ = std::clamp(t, 0.0, std::max(0.0, duration_));
    update();
}

void TimelineWidget::setCursorSec(double t) {
    cursor_ = std::clamp(t, 0.0, std::max(0.0, duration_));
    update();
}

bool TimelineWidget::peaksCoverView() const {
    return peaks_ != nullptr && peaks_->covers(viewFrom_, viewTo_);
}

void TimelineWidget::setViewRange(double from, double to) {
    if (duration_ <= 0.0 || to <= from) return;
    viewFrom_ = std::clamp(from, 0.0, duration_);
    viewTo_ = std::clamp(to, 0.0, duration_);
    clampView();
    update();
    emit viewChanged(viewFrom_, viewTo_);
}

void TimelineWidget::pageViewForCursor() {
    if (!hasMedia_ || duration_ <= 0.0) return;
    const double s = std::min(span(), duration_);

    // 翻页的时机：播放头走到（或被挪到）当前页外面。
    // 页内一律不动视窗 —— 这正是"翻页"和"滚动"的区别：
    // 滚动是每帧把播放头摆回中间，翻页是等它走到头了才整页跳一下。
    const bool inside = cursor_ >= viewFrom_ && cursor_ < viewTo_ - 1e-6;
    if (inside) return;

    // 新页的左端就是当前帧：翻完之后播放头出现在这一页的最左端。
    // 片尾处钳住（再往右就超出片尾了），此时播放头在最后一页里继续往右走。
    const double maxFrom = std::max(0.0, duration_ - s);
    const double from = std::clamp(cursor_, 0.0, maxFrom);
    // 已经在这一页就别重画、更别发 viewChanged —— 播放时每帧都发的话
    // 上层会不停地重算波形和缩略图
    if (std::abs(from - viewFrom_) < 1e-6) return;
    viewFrom_ = from;
    viewTo_ = from + s;
    clampView();
    update();
    emit viewChanged(viewFrom_, viewTo_);
}

void TimelineWidget::zoomToFit() {
    viewFrom_ = 0.0;
    viewTo_ = duration_;
    update();
    emit viewChanged(viewFrom_, viewTo_);
}

// ---------------- 绘制 ----------------

void TimelineWidget::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), palette().color(QPalette::Base));

    if (!hasMedia_) {
        p.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        // 绘制时现场 tr()：切语言后重绘（update()）即跟上，不需要另存字符串
        p.drawText(rect(), Qt::AlignCenter, QCoreApplication::translate("lr::ui::TimelineWidget", "导入视频后显示时间轴"));
        return;
    }

    const Layout l = layout();
    drawSelection(p, l);
    drawThumbnails(p, l);
    drawWaveform(p, l);
    drawRuler(p, l);
    drawMarkers(p, l);
    drawSlider(p, l);
}

void TimelineWidget::drawSelection(QPainter &p, const Layout &l) {
    const int x0 = static_cast<int>(timeToX(start_));
    const int x1 = static_cast<int>(timeToX(end_));
    if (x1 <= x0) return;
    const QRect area(x0, l.thumbTop, x1 - x0, l.waveH + l.thumbH);
    p.fillRect(area, QColor(kSelectionColor.red(), kSelectionColor.green(),
                            kSelectionColor.blue(), 55));
}

void TimelineWidget::drawImageFitted(QPainter &p, const QImage &img, const QRect &target) {
    if (img.isNull()) {
        p.fillRect(target, QColor(0xF0, 0xF0, 0xF0));
        return;
    }
    // 高度贴合影片条，宽度按原比例走；装不满的地方留空隙，不裁切、不拉伸
    const double scale = std::min(double(target.height()) / img.height(),
                                  double(target.width()) / img.width());
    const int w = std::max(1, static_cast<int>(std::floor(img.width() * scale)));
    const int h = std::max(1, static_cast<int>(std::floor(img.height() * scale)));
    const int x = target.left() + (target.width() - w) / 2;
    const int y = target.top() + (target.height() - h) / 2;
    p.drawImage(QRect(x, y, w, h), img);
}

void TimelineWidget::drawThumbnails(QPainter &p, const Layout &l) {
    const double slot = double(width()) / double(kThumbCount);
    for (int i = 0; i < kThumbCount; ++i) {
        const int x = static_cast<int>(std::lround(i * slot));
        const int x1 = static_cast<int>(std::lround((i + 1) * slot));
        drawImageFitted(p, thumb_[i], QRect(x, l.thumbTop, std::max(1, x1 - x), l.thumbH));
    }
    // 缩略图与波形之间一条分隔线
    p.setPen(QColor(0xDD, 0xDD, 0xDD));
    p.drawLine(0, l.waveTop, width(), l.waveTop);
}

int TimelineWidget::peakLevel(double t0, double t1) const {
    if (!peaks_ || peaks_->empty() || peaks_->pointsPerSec <= 0.0) return 0;
    const double pps = peaks_->pointsPerSec;
    std::size_t i0 = static_cast<std::size_t>(std::max(0.0, (t0 - peaks_->fromSec) * pps));
    std::size_t i1 = static_cast<std::size_t>(std::max(0.0, (t1 - peaks_->fromSec) * pps));
    if (i1 <= i0) i1 = i0 + 1;
    i0 = std::min(i0, peaks_->size() - 1);
    i1 = std::min(i1, peaks_->size());
    // -1 表示"这一片还没算出来"（后台还在算），不能当成静音画成 0 高度，
    // 否则整段波形看起来像没声音。返回 -1 由 drawWaveform 留白处理。
    int level = -1;
    for (std::size_t i = i0; i < i1; ++i) {
        const int v = peaks_->level(i);
        if (v < 0) continue;   // 空洞：跳过，别把这一片拉成 0
        level = std::max(level, v);
    }
    return level;
}

void TimelineWidget::drawWaveform(QPainter &p, const Layout &l) {
    const int midY = l.waveTop + l.waveH / 2;
    const int halfH = l.waveH / 2 - 1;

    // 峰值数据还没覆盖当前可见区间（刚导入 / 刚缩放平移）：先给占位，
    // 后台算完再刷新 —— 这样长视频不用等波形算完就有画面
    if (!peaksCoverView()) {
        p.setPen(QPen(QColor(0xB0, 0xB0, 0xB0), 1, Qt::DashLine));
        p.drawLine(0, midY, width(), midY);
        p.setPen(palette().color(QPalette::Disabled, QPalette::Text));
        p.drawText(QRect(0, l.waveTop, width(), l.waveH), Qt::AlignCenter,
                   hasAudio_ ? QCoreApplication::translate("lr::ui::TimelineWidget", "波形计算中…") : QCoreApplication::translate("lr::ui::TimelineWidget", "无音轨"));
        return;
    }

    // 屏幕片数按控件宽度定（每 2 像素一片，最多 600 片）：
    // 数据源是整轨固定密度的峰值，屏幕只要按自己的宽度切片再取片内最大值即可
    const int slices = std::clamp(width() / 2, 60, kWaveSlices);
    const double slice = span() / double(slices);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < slices; ++i) {
        const double t0 = viewFrom_ + double(i) * slice;
        const int level = peakLevel(t0, t0 + slice);
        // level < 0 = 这一段还没算出来（后台还在算别的段）：留白，
        // 不能画成 0 高度，否则看着像静音
        if (level <= 0) continue;
        const int h = std::max(1, static_cast<int>(std::lround(double(level) / 32767.0 * halfH)));
        const int x0 = static_cast<int>(std::lround(double(i) * width() / slices));
        const int x1 = static_cast<int>(std::lround(double(i + 1) * width() / slices));
        p.fillRect(x0, midY - h, std::max(1, x1 - x0), 2 * h, kWaveColor);
    }
    p.setPen(QColor(0xCC, 0xCC, 0xCC));
    p.drawLine(0, midY, width(), midY);
}

void TimelineWidget::drawRuler(QPainter &p, const Layout &l) {
    const int bottom = l.rulerTop + l.rulerH;
    p.fillRect(QRect(0, l.rulerTop, width(), l.rulerH), palette().color(QPalette::Window));

    const double visible = span();
    double step = kTickSteps[std::size(kTickSteps) - 1];
    for (double s : kTickSteps) {
        if (s / visible * double(width()) >= 70.0) {
            step = s;
            break;
        }
    }
    const double minorStep = step / 5.0;

    p.setPen(QColor(0xB0, 0xB0, 0xB0));
    if (minorStep / visible * double(width()) >= 4.0) {
        for (double t = std::ceil(viewFrom_ / minorStep) * minorStep; t < viewTo_;
             t += minorStep) {
            const int x = static_cast<int>(timeToX(t));
            p.drawLine(x, bottom - 5, x, bottom);
        }
    }

    p.setPen(QColor(0x66, 0x66, 0x66));
    QFontMetrics fm = p.fontMetrics();
    for (double t = std::ceil(viewFrom_ / step) * step; t < viewTo_; t += step) {
        const int x = static_cast<int>(timeToX(t));
        p.drawLine(x, l.rulerTop, x, bottom);
        const QString label = formatTick(t, step);
        p.drawText(x + 3, l.rulerTop + 13, label);
    }
    p.setPen(QColor(0xDD, 0xDD, 0xDD));
    p.drawLine(0, l.rulerTop, width(), l.rulerTop);
}

void TimelineWidget::drawMarkers(QPainter &p, const Layout &l) {
    const int top = plotTop();
    const int bottom = plotBottom();

    auto marker = [&](double t, const QColor &color, bool withHandle) {
        const int x = static_cast<int>(timeToX(t));
        p.setPen(QPen(color, 2));
        p.drawLine(x, top, x, bottom);
        if (withHandle) {
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawRoundedRect(x - 7, top + 1, 14, 12, 3, 3);
        }
    };

    marker(start_, kStartColor, true);
    marker(end_, kEndColor, true);

    // 播放头画在最上层
    const int cx = static_cast<int>(timeToX(cursor_));
    p.setPen(QPen(kCursorColor, 1));
    p.drawLine(cx, top, cx, bottom);
    p.setPen(Qt::NoPen);
    p.setBrush(kCursorColor);
    QPolygon tri;
    tri << QPoint(cx, top + 14) << QPoint(cx - 5, top + 24) << QPoint(cx + 5, top + 24);
    p.drawPolygon(tri);
}

QRect TimelineWidget::sliderThumbRect() const {
    const Layout l = layout();
    if (duration_ <= 0.0) return {};
    const int x0 = static_cast<int>(viewFrom_ / duration_ * width());
    const int x1 = static_cast<int>(viewTo_ / duration_ * width());
    const int w = std::max(kThumbMinPx, x1 - x0);
    const int left = std::clamp(x0, 0, std::max(0, width() - w));
    return QRect(left, l.sliderTop, w, l.sliderH);
}

void TimelineWidget::drawSlider(QPainter &p, const Layout &l) {
    if (duration_ <= 0.0) return;
    p.setPen(Qt::NoPen);

    // 底条：全片
    p.setBrush(kTrackColor);
    p.drawRoundedRect(0, l.sliderTop, width(), l.sliderH, 4, 4);

    // 深蓝滑块：可视区间在全片中的位置与占比（拖动=整体平移，长度不变）
    p.setBrush(kThumbColor);
    p.drawRoundedRect(sliderThumbRect(), 4, 4);

    // 起点/终点在整片中的位置（放大后也能知道剪辑段处于何处）
    const int px = static_cast<int>(start_ / duration_ * width());
    const int qx = static_cast<int>(end_ / duration_ * width());
    p.fillRect(px - 1, l.sliderTop, 2, l.sliderH, kStartColor);
    p.fillRect(qx - 1, l.sliderTop, 2, l.sliderH, kEndColor);
}

// ---------------- 交互 ----------------

void TimelineWidget::setCursorFromX(int x, bool settled) {
    const double t = std::clamp(xToTime(x), 0.0, duration_);
    if (std::abs(t - cursor_) < 1e-9 && !settled) return;
    cursor_ = t;
    update();
    if (settled) {
        emit cursorSettled(cursor_);
    } else {
        emit cursorChanged(cursor_);
    }
}

void TimelineWidget::smokeClickCursor(double sec) {
    if (!hasMedia_ || duration_ <= 0.0) return;
    // 和真实鼠标一致：按下先发 cursorChanged（拖动中），松手再发 cursorSettled
    cursor_ = std::clamp(sec, 0.0, duration_);
    update();
    emit cursorChanged(cursor_);
    emit cursorSettled(cursor_);
}

void TimelineWidget::beginViewDrag(int x) {
    dragOffset_ = xToFullTime(x) - viewFrom_;   // 抓点相对窗口左端的偏移
}

void TimelineWidget::panViewTo(int x) {
    // 平移：窗口长度保持不变，整体在底条上挪位置。
    // 注意必须先取 span() 再改 viewFrom_ —— span() = viewTo_ - viewFrom_，
    // 改完左端再算就成了"右端钉住、长度随拖动变"，看着像在缩放。
    const double s = span();
    viewFrom_ = xToFullTime(x) - dragOffset_;
    viewTo_ = viewFrom_ + s;
    clampView();
    update();
    emit viewChanged(viewFrom_, viewTo_);
    emit viewDraggedByUser();
}

void TimelineWidget::smokeDragView(double pressFrac, double moveFrac) {
    if (!hasMedia_ || width() <= 0) return;
    const QRect thumb = sliderThumbRect();
    const int pressX = thumb.center().x();   // 抓在滑块中间（不是左端）
    const int moveX = static_cast<int>(std::clamp(moveFrac, 0.0, 1.0) * width());
    (void)pressFrac;
    beginViewDrag(pressX);
    panViewTo(moveX);
    drag_ = Drag::None;
}

void TimelineWidget::mousePressEvent(QMouseEvent *event) {
    if (!hasMedia_ || event->button() != Qt::LeftButton) return;
    const Layout l = layout();
    const int x = event->position().x();
    const int y = event->position().y();

    // 底部滑块层：点中深蓝滑块=整体平移，点底条其余处=跳转
    if (y >= l.sliderTop) {
        const double full = xToFullTime(x);
        const QRect thumb = sliderThumbRect();
        if (thumb.contains(x, y) && span() < duration_) {
            drag_ = Drag::View;
            beginViewDrag(x);
            emit viewDraggedByUser();   // 手已经落在滑块上了，意图明确
        } else {
            // 点击跳转：窗口长度不变，整体挪到点击处居中
            const double s = span();          // 必须先存下来：span() 依赖 viewFrom_
            viewFrom_ = full - s / 2.0;
            viewTo_ = viewFrom_ + s;
            clampView();
            update();
            emit viewChanged(viewFrom_, viewTo_);
            emit viewDraggedByUser();
        }
        return;
    }

    // 影片区：先命中起终点手柄，否则移动播放头
    const int xs = static_cast<int>(timeToX(start_));
    const int xe = static_cast<int>(timeToX(end_));
    if (std::abs(x - xs) <= kMarkerHitPx) {
        drag_ = Drag::Start;
    } else if (std::abs(x - xe) <= kMarkerHitPx) {
        drag_ = Drag::End;
    } else {
        drag_ = Drag::Cursor;
        setCursorFromX(x, false);
    }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event) {
    if (drag_ == Drag::None) return;
    const int x = event->position().x();
    switch (drag_) {
    case Drag::Cursor:
        setCursorFromX(x, false);
        break;
    case Drag::Start: {
        const double t = std::clamp(xToTime(x), 0.0, duration_);
        if (t <= end_ - 0.001) {
            start_ = t;
            update();
            emit rangeChanged(start_, end_);
        }
        break;
    }
    case Drag::End: {
        const double t = std::clamp(xToTime(x), 0.0, duration_);
        if (t >= start_ + 0.001) {
            end_ = t;
            update();
            emit rangeChanged(start_, end_);
        }
        break;
    }
    case Drag::View:
        panViewTo(x);
        break;
    case Drag::None:
        break;
    }
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *) {
    switch (drag_) {
    case Drag::Cursor:
        emit cursorSettled(cursor_);
        break;
    case Drag::Start:
    case Drag::End:
        emit rangeSettled(start_, end_);
        break;
    case Drag::View:
        emit viewChanged(viewFrom_, viewTo_);
        break;
    case Drag::None:
        break;
    }
    drag_ = Drag::None;
}

void TimelineWidget::wheelEvent(QWheelEvent *event) {
    if (!hasMedia_) return;
    const double anchor = xToTime(static_cast<int>(event->position().x()));
    const double frac = span() > 0.0 ? (anchor - viewFrom_) / span() : 0.5;

    // 上限：放大到约 10 帧可见（需求：能看到帧）
    const double minSpan = std::max(0.1, 10.0 / fps_);
    const double factor = event->angleDelta().y() > 0 ? (1.0 / 1.25) : 1.25;
    const double newSpan = std::clamp(span() * factor, minSpan, duration_);

    viewFrom_ = anchor - frac * newSpan;
    viewTo_ = viewFrom_ + newSpan;
    clampView();
    update();
    emit viewChanged(viewFrom_, viewTo_);
    emit viewDraggedByUser();
}

void TimelineWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    update();
}

}  // namespace lr::ui
