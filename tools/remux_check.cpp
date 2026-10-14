// ---------------------------------------------------------------------------
// remux_check —— RemuxTask 的无界面功能验证工具。
//
//   remux_check <模式> <输入...> <输出> [startSec] [endSec]
//
//   模式：cut | video | audio | merge | concat
//     cut    : <输入> <输出> <start> <end>
//     video  : <输入> <输出>              （只留视频轨）
//     audio  : <输入> <输出>              （只留音频轨）
//     merge  : <视频> <音频> <输出>
//     concat : <输出> <输入1> <输入2> ... （第一个参数是输出）
//
// 它走的是和 GUI 完全相同的代码路径，只是把 TaskSignals 打到 stdout，
// 因此可以在没有界面的情况下验证整条 libav* 管线。
// ---------------------------------------------------------------------------

#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QObject>
#include <QTextStream>

#include <string>
#include <vector>

#include "core/ITask.h"
#include "ffmpeg/FfmpegApi.h"
#include "tasks/RemuxTask.h"

static QTextStream out(stdout);

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    const QStringList args = app.arguments();
    if (args.size() < 4) {
        out << "用法: remux_check <cut|video|audio|merge|concat> ...\n";
        return 2;
    }

    const QString mode = args.at(1);

    auto &api = FfmpegApi::instance();
    std::string error;
    if (!api.load(error)) {
        out << "动态库加载失败：" << QString::fromStdString(error) << "\n";
        return 1;
    }
    out << "引擎：" << QString::fromStdString(api.summary()) << "\n";

    lr::RemuxSpec spec;
    if (mode == QStringLiteral("cut")) {
        if (args.size() < 6) { out << "cut 需要: <输入> <输出> <start> <end>\n"; return 2; }
        spec.kind = lr::RemuxSpec::Kind::CopyAll;
        spec.inputs = {args.at(2).toStdString()};
        spec.output = args.at(3).toStdString();
        spec.startSec = args.at(4).toDouble();
        spec.endSec = args.at(5).toDouble();
        spec.title = "截取 " + spec.inputs[0];
    } else if (mode == QStringLiteral("video") || mode == QStringLiteral("audio")) {
        spec.kind = mode == QStringLiteral("video")
                        ? lr::RemuxSpec::Kind::VideoOnly
                        : lr::RemuxSpec::Kind::AudioOnly;
        spec.inputs = {args.at(2).toStdString()};
        spec.output = args.at(3).toStdString();
        spec.title = std::string(mode == QStringLiteral("video") ? "抽取视频轨 "
                                                                : "抽取音频轨 ") +
                     spec.inputs[0];
    } else if (mode == QStringLiteral("merge")) {
        if (args.size() < 5) { out << "merge 需要: <视频> <音频> <输出>\n"; return 2; }
        spec.kind = lr::RemuxSpec::Kind::Merge;
        spec.inputs = {args.at(2).toStdString(), args.at(3).toStdString()};
        spec.output = args.at(4).toStdString();
        spec.shortest = true;
        spec.title = "合并 " + spec.inputs[0];
    } else if (mode == QStringLiteral("concat")) {
        if (args.size() < 5) { out << "concat 需要: <输出> <输入1> <输入2> ...\n"; return 2; }
        spec.kind = lr::RemuxSpec::Kind::Concat;
        spec.output = args.at(2).toStdString();
        for (int i = 3; i < args.size(); ++i) {
            spec.inputs.push_back(args.at(i).toStdString());
        }
        spec.title = "连接 " + std::to_string(spec.inputs.size()) + " 个视频";
    } else {
        out << "未知模式：" << mode << "\n";
        return 2;
    }

    const QString ext =
        QFileInfo(QString::fromStdString(spec.output)).suffix().toLower();
    if (ext == QStringLiteral("mp4") || ext == QStringLiteral("mov") ||
        ext == QStringLiteral("m4a") || ext == QStringLiteral("3gp")) {
        spec.muxerOptions = {{"movflags", "+faststart"}};
    }

    lr::TaskSignals sig;
    QObject::connect(&sig, &lr::TaskSignals::log,
                     [](const QString &text, const QString &) {
                         QTextStream(stdout) << text << "\n";
                     });
    QObject::connect(&sig, &lr::TaskSignals::progress,
                     [](int percent, const QString &detail) {
                         QTextStream(stdout) << "  [进度 " << percent << "%] "
                                             << detail << "\n";
                     });

    lr::RemuxTask task(spec);
    const bool ok = task.run(sig);
    out << (ok ? "结果：成功\n" : "结果：失败\n");
    return ok ? 0 : 1;
}
