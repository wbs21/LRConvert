#pragma once
// ---------------------------------------------------------------------------
// 任务层抽象
//
//  * TaskSignals：工作线程 → 界面的唯一通道。它活在 GUI 线程里，
//    跨线程 emit 时 Qt 会自动把调用排队投递回 GUI 线程，因此是安全的。
//  * ITask：一个可执行任务。run() **必须在工作线程中调用**。
//
// 对应 Python 版的 TaskSignals / FfmpegJob。
// ---------------------------------------------------------------------------

#include <QObject>
#include <QString>

namespace lr {

// 日志颜色（与 Python 版一致）
namespace logcolor {
inline const QString kInfo = QStringLiteral("#333333");
inline const QString kOk = QStringLiteral("#237804");
inline const QString kError = QStringLiteral("#c0392b");
inline const QString kWarn = QStringLiteral("#a06000");
inline const QString kStep = QStringLiteral("#1a6fc4");
inline const QString kMuted = QStringLiteral("#666666");
}  // namespace logcolor

class TaskSignals : public QObject {
    Q_OBJECT
public:
    explicit TaskSignals(QObject *parent = nullptr) : QObject(parent) {}

signals:
    /// (文本, 颜色)
    void log(const QString &text, const QString &color);
    /// (百分比, 说明)；百分比 < 0 表示进度未知（走忙碌动画）
    void progress(int percent, const QString &detail);
    /// 整批任务是否全部成功
    void finished(bool ok);
    /// 版本检查的结果文本
    void versionInfo(const QString &text);
};

class ITask {
public:
    virtual ~ITask() = default;

    /// 显示在日志和进度条上的名字
    virtual QString title() const = 0;

    /// 执行任务（工作线程）。返回 true 表示成功。
    virtual bool run(TaskSignals &sig) = 0;

    /// 请求优雅停止；run() 应在收尾（flush + 写 trailer）后尽快返回
    virtual void requestStop() = 0;

    /// 删除临时文件（例如 concat 清单）
    virtual void cleanup() {}
};

}  // namespace lr
