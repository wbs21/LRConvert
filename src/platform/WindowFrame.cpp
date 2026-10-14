#include "platform/WindowFrame.h"

#include <cmath>

#include <QWidget>

#ifdef Q_OS_WIN
// windows.h 会定义 min/max 宏，跟 std::min/std::max 撞车，必须先按下
#define NOMINMAX
#include <windows.h>
#endif

namespace lr::platform {
namespace {

#ifdef Q_OS_WIN
/// 物理像素 → 逻辑像素
int toLogical(int physical, qreal dpr) {
    if (dpr <= 0.0) return physical;
    return static_cast<int>(std::lround(static_cast<qreal>(physical) / dpr));
}

bool queryWin32(const QWidget *w, int *topOffset, int *extraW, int *extraH) {
    if (!w) return false;
    const HWND hwnd = reinterpret_cast<HWND>(w->winId());
    if (!hwnd) return false;

    RECT frame{};
    if (!GetWindowRect(hwnd, &frame)) return false;

    // 客户区左上角在屏幕上的物理坐标
    POINT client{0, 0};
    if (!ClientToScreen(hwnd, &client)) return false;

    const qreal dpr = w->devicePixelRatioF();
    // 客户区右下角的物理坐标（POINT 只有 x / y，得自己加上宽高）
    const int physW = static_cast<int>(std::lround(w->width() * dpr));
    const int physH = static_cast<int>(std::lround(w->height() * dpr));
    const int top = toLogical(frame.top - client.y, dpr);
    const int left = toLogical(frame.left - client.x, dpr);
    const int right = toLogical(frame.right - (client.x + physW), dpr);
    const int bottom = toLogical(frame.bottom - (client.y + physH), dpr);

    if (topOffset) *topOffset = top;
    if (extraW) *extraW = -left + right;
    if (extraH) *extraH = -top + bottom;
    return true;
}
#endif

}  // namespace

bool frameTopOffset(const QWidget *w, int *offset) {
#ifdef Q_OS_WIN
    if (queryWin32(w, offset, nullptr, nullptr)) return true;
#else
    Q_UNUSED(w);
    Q_UNUSED(offset);
#endif
    return false;
}

bool frameExtraSize(const QWidget *w, int *extraW, int *extraH) {
#ifdef Q_OS_WIN
    if (queryWin32(w, nullptr, extraW, extraH)) return true;
#else
    Q_UNUSED(w);
    Q_UNUSED(extraW);
    Q_UNUSED(extraH);
#endif
    return false;
}

}  // namespace lr::platform
