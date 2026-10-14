#pragma once
// ---------------------------------------------------------------------------
// 窗口外框（含标题栏与边框）在屏幕上的真实矩形。
//
// 为什么不能直接用 QWidget::frameGeometry()：
//   窗口被挪到屏幕外（顶部 y 为负）时，Qt 算出来的外框常常**退化成和客户区
//   一样** —— 边框偏移被当成 0。于是"把窗口拉回可见区"这类修正就会做歪：
//   实测把客户区挪到 y=-9 后调用修正，Qt 认为 dy=0，结果窗口只被挪到
//   客户区 y=0，而外框还在 -31，标题栏依旧在屏幕外面。
//
//   所以在 Windows 上直接问系统要（GetWindowRect 拿外框、ClientToScreen
//   拿客户区原点，两者相减就是真实的边框偏移），拿不到再回退到 Qt 的。
//
// 注意 devicePixelRatio：Win32 的坐标是物理像素，Qt 的几何是逻辑像素，
// 系统缩放不是 100% 时两者差一个 dpr，所以这里统一换算回逻辑像素再返回。
// ---------------------------------------------------------------------------

#include <QRect>

class QWidget;

namespace lr::platform {

/// 窗口外框顶部相对客户区顶部的偏移（逻辑像素，通常为负，例如标题栏 31px → -31）
/// 拿不到就返回 false，调用方自行回退到 QWidget::frameGeometry()。
bool frameTopOffset(const QWidget *w, int *offset);

/// 外框比客户区多出来的总宽度 / 总高度（逻辑像素）
bool frameExtraSize(const QWidget *w, int *extraW, int *extraH);

}  // namespace lr::platform
