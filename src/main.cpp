// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

// ---------------------------------------------------------------------------
// 入口：UI 先行，FFmpeg 动态库延后加载。
//
// 启动顺序（方案 §6.2）：
//   1. QApplication 构造
//   2. MainWindow 构造 + show()      ← 首帧出现，用户已经能看到界面
//   3. 事件循环跑起来之后（singleShot(0)）才在工作线程里加载 5 个 DLL
//
// 这样 FFmpeg 的加载耗时（冷启动可达秒级，热启动约 5ms）完全不落在
// "双击图标到看到窗口"这段时间里。
// ---------------------------------------------------------------------------

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <cstdio>
#include <exception>
#include <thread>

#include "MainWindow.h"
#include "core/I18n.h"
#include "core/Version.h"
#include "ffmpeg/FfmpegApi.h"
#include "platform/CrashDump.h"
#include "platform/ThreadUtil.h"

namespace {

// ---------------------------------------------------------------------------
// 把 Qt 日志同时落一份到文件
//
// 目的很实际：程序在用户机器上"突然没了"的时候，光凭一句"刚才退出了"
// 是查不出原因的。有了这份日志，至少能看到退出前最后几行在做什么
// （在解哪个文件、波形算到第几段、播放到哪一秒）。
// ---------------------------------------------------------------------------
std::FILE *g_logFile = nullptr;

QString logFilePath() {
    // 优先写在程序旁边：发布包是解压即用的，日志就在 exe 同级的 logs/ 里，
    // 找起来最直观。写不进去（比如放在 Program Files 下）就退回临时目录。
    const QString beside = QCoreApplication::applicationDirPath() + QStringLiteral("/logs");
    if (QDir().mkpath(beside) && QFileInfo(beside).isWritable()) {
        return beside + QStringLiteral("/LRConvert.log");
    }
    const QString fallback =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
        QStringLiteral("/LRConvert");
    QDir().mkpath(fallback);
    return fallback + QStringLiteral("/LRConvert.log");
}

void messageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    const char *tag = "";
    switch (type) {
    case QtDebugMsg:    tag = "[D] "; break;
    case QtInfoMsg:     tag = "[I] "; break;
    case QtWarningMsg:  tag = "[W] "; break;
    case QtCriticalMsg: tag = "[C] "; break;
    case QtFatalMsg:    tag = "[F] "; break;
    }
    const QString line =
        QStringLiteral("%1%2  %3\n")
            .arg(QString::fromUtf8(tag),
                 QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),
                 msg);

    if (g_logFile != nullptr) {
        const QByteArray bytes = line.toLocal8Bit();
        std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), g_logFile);
        // 必须立刻 flush：进程要是崩了，还留在缓冲区里的那几行就永远看不到了，
        // 而那往往正是最接近崩溃现场的那几行
        std::fflush(g_logFile);
    }
    // 控制台照常输出（冒烟与回归脚本就是靠读 stderr 拿 [SMOKE] 行的）
    const QByteArray out = line.toLocal8Bit();
    std::fwrite(out.constData(), 1, static_cast<std::size_t>(out.size()), stderr);
    std::fflush(stderr);
}

}  // namespace

int main(int argc, char *argv[]) {
    // 线程命名（P1-6）：dump 里看线程列表时，主线程不能只是一串数字。
    // 越早越好 —— 这条线程后面所有东西都挂在这里跑。
    lr::platform::setThreadName("LRConvert/GUI");

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("LRconvert"));
    // 全局图标：MainWindow 自己也会 setWindowIcon，这里再设一次是为了
    // 覆盖那些没有显式设图标的窗口（QMessageBox 等），也让任务栏回退时有得用
    app.setWindowIcon(QIcon(QStringLiteral(":/LRconvert.ico")));
    app.setApplicationVersion(QString::fromUtf8(lr::kAppVersion));
    app.setOrganizationName(QStringLiteral("wbs21"));

    // 日志落盘：追加写，但超过 2MB 就重来一份，免得无限长大
    const QString logPath = logFilePath();
    {
        const QFileInfo fi(logPath);
        const bool tooBig = fi.exists() && fi.size() > 2 * 1024 * 1024;
        g_logFile = std::fopen(logPath.toLocal8Bit().constData(), tooBig ? "w" : "a");
    }
    qInstallMessageHandler(messageHandler);
    qInfo().noquote() << QStringLiteral("════ LRConvert %1 启动 ════")
                             .arg(QString::fromUtf8(lr::kAppVersion));

    // ---------- 崩溃转储 + 上一次退役方式判定 ----------
    //
    // 崩溃转储：必须尽早装，而且要装在干任何正事之前。
    //
    // 访问违规、栈溢出、堆损坏这类错误不走 C++ 异常 —— 线程里的 try-catch
    // 和下面 app.exec() 的 catch(...) 全都拦不住，进程会被系统直接杀掉，
    // 日志里连最后一行都留不下。装了这个之后，进程死之前会先把现场写成
    // .dmp，事后打开就能看到崩在哪一行。
    QString dumpDirPath;
    bool crashedLastTime = false;
    QString lastDumpPathText;
    {
        const QDir logDir = QFileInfo(logPath).dir();
        const QString dumpDir = logDir.filePath(QStringLiteral("crashdumps"));
        dumpDirPath = dumpDir;
        QDir().mkpath(dumpDir);

        // 先问"上次是不是崩了"（读完答复再装处理器 —— 装完就会开始写
        // 干净退出标记，顺序反了就永远检测不到上一次的崩溃了）
        std::wstring lastDump;
        crashedLastTime = lr::platform::hadAbnormalExit(dumpDir.toStdWString(), &lastDump);
        if (crashedLastTime && !lastDump.empty()) {
            lastDumpPathText = QString::fromWCharArray(lastDump.c_str());
        }

        lr::platform::installCrashHandler(dumpDir.toStdWString(), [](const char *text) {
            // 崩溃时 Qt 可能已经不好用了，但 qCritical 只是走 messageHandler
            // 里的 fwrite + fflush，够朴素、也够可靠
            qCritical().noquote() << QString::fromUtf8(text);
        });
    }

    // FFmpeg 自己的报错也接进日志：解码器抱怨了什么、哪个流坏了、
    // 哪个参数不支持 —— 这些是排查"解码出错"最直接的一手信息。
    // 不接的话它们只会打到 stderr，而 GUI 程序的 stderr 是看不见的。
    // 注意：FfmpegApi 在全局命名空间（它不依赖 Qt，也没跟其它模块一样
    // 放进 lr:: ），所以这里不写 lr::
    FfmpegApi::setLogSink([](int level, const char *msg) {
        // FFmpeg 级别：数字越小越严重，16=错误、24=警告（已在上游压到 24）
        const QString tag = (level <= 16) ? QStringLiteral("[FFmpeg/错误]")
                                          : QStringLiteral("[FFmpeg/警告]");
        qWarning().noquote() << tag << QString::fromUtf8(msg);
    });

    // 语言：默认英文，记忆的选择在 QSettings（wbs21/LRConvert → language）。
    // 必须装在构造任何窗口之前——setupUi 那一刻就要拿到翻译，
    // 否则首帧会是中文、之后也不会自动刷新。
    {
        QString langError;
        if (!lr::i18n::applySaved(&langError)) {
            qWarning().noquote()
                << QStringLiteral("语言加载失败（界面保持源码语言）：") << langError;
        }
    }

    MainWindow window;

    // 崩溃后安全模式（P1-8）：上一跑没走到正常退出就结束，说明崩过一次。
    //
    // 这一跑就把"最可能再崩一次"的那件事省掉（硬件编码器探测 —— 它要去
    // 打开各家的驱动动态库，是整条链路上环境差异最大、最不稳的一段），
    // 让程序至少能顺顺当当起来、把用户的活干完。
    // 干净地退出一次之后，标记刷新，下一次就自动回到完整模式。
    if (crashedLastTime) {
        window.setSafeMode(true);
        qWarning().noquote()
            << QStringLiteral("[P1-8] 检测到上一次运行异常结束，本次进入安全模式。")
            << (lastDumpPathText.isEmpty() ? QStringLiteral("（无转储文件）")
                                           : QStringLiteral("转储：") + lastDumpPathText);

        // 告诉用户一声，别让"下拉框里少了几个硬件编码器"变成一个谜。
        // 冒烟/自检（对环境有要求、无人值守）时不弹窗 —— 那种场合只看日志。
        const bool unattended =
            qEnvironmentVariableIsSet("LRCONVERT_SMOKE_TEST") ||
            qEnvironmentVariableIsSet("LRCONVERT_CRASH_TEST") ||
            qEnvironmentVariableIsSet("LRCONVERT_DAMAGE_TEST") ||
            qEnvironmentVariableIsSet("LRCONVERT_LANG_TEST");
        if (!unattended) {
            QMessageBox box(QMessageBox::Warning,
                            QCoreApplication::translate("main", "LRconvert"),
                            QCoreApplication::translate(
                                "main",
                                "上一次运行似乎没有正常结束（留下了崩溃转储文件）。\n\n"
                                "本次已启用安全模式：跳过硬件编码器识别，"
                                "其余功能不受影响，转码仍可使用 libx264。\n\n"
                                "%1")
                                .arg(lastDumpPathText.isEmpty()
                                         ? QString()
                                         : QCoreApplication::translate("main", "转储文件：%1")
                                               .arg(lastDumpPathText)),
                            QMessageBox::NoButton, &window);
            box.addButton(QCoreApplication::translate("main", "知道了"),
                          QMessageBox::AcceptRole);
            QPushButton *openDir =
                box.addButton(QCoreApplication::translate("main", "打开转储目录"),
                              QMessageBox::ActionRole);
            box.exec();
            if (box.clickedButton() == openDir) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(dumpDirPath));
            }
        }
    }
    window.show();

    // 关键：排在首帧绘制之后，再开始加载动态库
    QTimer::singleShot(0, &window, &MainWindow::startAsyncInit);

    // 损坏容错自检：LRCONVERT_DAMAGE_TEST=<损坏视频> 时，从坏区正前方开始播，
    // 用来验证"播放不会被损坏数据卡住"。
    // 常规冒烟（LRCONVERT_SMOKE_TEST）的播放区间是 0~2s / 4.9~6s，
    // 碰不到测试片中段那片坏区，所以单独开一条时间线。
    // 注意：这个与 LRCONVERT_SMOKE_TEST 二选一，不要同时设。
    if (qEnvironmentVariableIsSet("LRCONVERT_DAMAGE_TEST")) {
        const QString file = QString::fromLocal8Bit(qgetenv("LRCONVERT_DAMAGE_TEST"));
        QTimer::singleShot(600, &window, [&window, file]() { window.smokeLoadCutFile(file); });
        QTimer::singleShot(2500, &window, [&window]() {
            window.smokeSetCursor(2.5);  // 坏区在 45%~51% 处，约合 2.7~3.1 秒
            qInfo().noquote() << "[SMOKE] 坏区播放前：" << window.smokePlayState();
            window.smokeTogglePlay();
        });
        QTimer::singleShot(6000, &window, [&window]() {
            qInfo().noquote() << "[SMOKE] 坏区播放后：" << window.smokePlayState();
            qInfo().noquote() << "[SMOKE] 坏区统计：" << window.smokeDamageState();
        });
        QTimer::singleShot(7000, &app, &QCoreApplication::quit);
    }

    // 崩溃转储自检钩子：LRCONVERT_CRASH_TEST=1 时故意制造一次访问违规，
    // 用来验证「SEH → 写 .dmp → 落日志」这条链路本身是通的。
    // 正常运行时这个环境变量不存在，永远走不到这里 —— 留着它，
    // 是为了以后换了机器/换了编译器还能一键确认转储没失效。
    if (qEnvironmentVariableIsSet("LRCONVERT_CRASH_TEST")) {
        QTimer::singleShot(1500, &app, []() {
            volatile int *bad = reinterpret_cast<volatile int *>(0);
            *bad = 42;  // 向地址 0 写 —— 必崩，就是要崩
        });
    }

    // 冒烟测试钩子：LRCONVERT_SMOKE_TEST=1 时跑 6 秒自动退出，
    // 便于脚本/CI 验证"UI 先出 + 后台加载"这条链路（正常运行时不生效）
    if (qEnvironmentVariableIsSet("LRCONVERT_SMOKE_TEST")) {
        // 带视频导入的检查项更多，退出时间相应后延
        // 不带视频时也要跑完「最大化/还原 + 顶出屏幕拉回」那几步（到 5.6s）
        const int quitMs = qEnvironmentVariableIsSet("LRCONVERT_CUT_FILE") ? 14000 : 7000;
        QTimer::singleShot(quitMs, &app, &QCoreApplication::quit);
        // 第 4 秒打印编码器下拉框内容：验证"硬件探测 → 信号 → 下拉框过滤"
        // 整条链路（此时后台加载与探测应早已完成），顺带打印窗口标题
        // 核对版本号
        QTimer::singleShot(4000, &window, [&window]() {
            qInfo().noquote()
                << "[SMOKE] 窗口标题：" << window.windowTitle();
            // 图标自检：空图标说明 qrc 没挂上或者别名写错了（图标文件本身
            // 在 exe 里由 app.rc 嵌入，这里查的是运行时窗口图标）
            qInfo().noquote()
                << "[SMOKE] 窗口图标：" << (window.windowIcon().isNull()
                                                ? QStringLiteral("空！")
                                                : QStringLiteral("%1 个尺寸")
                                                      .arg(window.windowIcon()
                                                               .availableSizes()
                                                               .size()));
            qInfo().noquote()
                << "[SMOKE] 提示标签页：" << window.smokeTipText();
            qInfo().noquote()
                << "[SMOKE] 编码器下拉框（硬件识别后）："
                << window.smokeEncoderItems();
        });
        // 第 1 秒再打印一次：确认加载期间下拉框也只有软件编码器
        //（不支持的硬件项从不出现，而不是"先全量再收敛"）
        QTimer::singleShot(1000, &window, [&window]() {
            qInfo().noquote()
                << "[SMOKE] 编码器下拉框（加载中）："
                << window.smokeEncoderItems();
        });
        // BUG-001 复现：脚本自己最大化再还原，把几何打出来。
        // 不用人工点标题栏，回归里也能一直跑着防它复发。
        // 排在播放冒烟（7s 起）之前，互不干扰。
        QTimer::singleShot(4500, &window, [&window]() {
            // 先把窗口拖到屏幕顶部（模拟用户把它挪上去的场景）：
            // 最大化时 Windows 会把外框顶到 -8，还原后 y 若沿用这个偏移就是负的
            window.move(window.x(), 0);
            qInfo().noquote() << "[SMOKE] 几何·拖到顶部：" << window.smokeWindowGeom();
            window.smokeSetMaximized(true);
        });
        QTimer::singleShot(4900, &window, [&window]() {
            qInfo().noquote() << "[SMOKE] 几何·最大化中：" << window.smokeWindowGeom();
            window.smokeSetMaximized(false);
        });
        QTimer::singleShot(5400, &window, [&window]() {
            qInfo().noquote() << "[SMOKE] 几何·还原后：" << window.smokeWindowGeom();
            // 本机不复现 BUG-001，所以人为把窗口顶出屏幕验证兜底逻辑本身
            window.smokeNudgeOffScreen();
        });
        QTimer::singleShot(5800, &window, [&window]() {
            qInfo().noquote() << "[SMOKE] 几何·顶出屏幕后拉回：" << window.smokeWindowGeom();
        });
        // LRCONVERT_CUT_FILE=<视频> 时自动导入 Tab1，并打印时间轴状态：
        // 无界面也能验证「探测 + 关键帧 + 波形 + 缩略图 + 吸附」整条链路
        if (qEnvironmentVariableIsSet("LRCONVERT_CUT_FILE")) {
            const QString cutFile =
                QString::fromLocal8Bit(qgetenv("LRCONVERT_CUT_FILE"));
            QTimer::singleShot(600, &window, [&window, cutFile]() {
                window.smokeLoadCutFile(cutFile);
            });
            // 导入后立刻看一次：验证"先出画面"（此时波形应还没算完）
            QTimer::singleShot(1600, &window, [&window]() {
                qInfo().noquote() << "[SMOKE] Tab1（导入后 1s）："
                                  << window.smokeCutState();
            });
            QTimer::singleShot(5200, &window, [&window]() {
                qInfo().noquote() << "[SMOKE] Tab1：" << window.smokeCutState();
                // 选 2.30~4.70，关键帧每秒一个 → 起点应吸附到右侧 3.000、
                // 终点应吸附到左侧 4.000（输出为所选区间的子集）
                window.smokeApplyRange(2.30, 4.70);
            });
            QTimer::singleShot(5600, &window, [&window]() {
                qInfo().noquote() << "[SMOKE] Tab1（选 2.30~4.70 吸附后）："
                                  << window.smokeCutState();
            });
            // 放大到 1~3 秒后把深蓝滑块拖到 75% 处：
            // 正确行为是"窗口长度不变（仍 2 秒）、整体挪位置"，而不是左端钉住变长
            QTimer::singleShot(6000, &window, [&window]() {
                window.smokePanSlider(1.0, 3.0, 0.75);
            });
            QTimer::singleShot(6400, &window, [&window]() {
                qInfo().noquote() << "[SMOKE] Tab1（视窗 1~3s 拖到 75%）："
                                  << window.smokeCutState();
            });
            // 放大窗口：预览窗应吃掉多出来的高度跟着变大，并重新缩放画面
            QTimer::singleShot(6600, &window, [&window]() {
                qInfo().noquote() << "[SMOKE] 放大前：" << window.smokePreviewInfo();
                window.resize(1280, 900);
            });
            QTimer::singleShot(6900, &window, [&window]() {
                qInfo().noquote() << "[SMOKE] 放大后：" << window.smokePreviewInfo();
            });
            // 预览播放：从当前帧（蓝标尺）开始播。
            // 先把可见区间收窄到 0~2s 并把播放头放回 0，这样才能看出
            // "播放头居中 + 影片向左滚"的效果（整片可见时视窗无处可滚）。
            QTimer::singleShot(7000, &window, [&window]() {
                window.smokeApplyView(0.0, 2.0);
                window.smokeSetCursor(0.0);
                qInfo().noquote() << "[SMOKE] 播放前：" << window.smokePlayState();
                window.smokeTogglePlay();
            });
            // 播到约 1s：还在第一页里（0~2），播放头从左往右走到中间，
            // 视窗必须纹丝不动 —— 这是"翻页"和"滚动"的分水岭
            QTimer::singleShot(8000, &window, [&window]() {
                qInfo().noquote() << "[SMOKE] 播放 1s（页内，不该动视窗）："
                                  << window.smokePlayState();
            });
            // 播到约 2s：播放头走到第一页右端（2.0），影片应整页向左跳到 2~4，
            // 播放头回到新页的最左端
            QTimer::singleShot(9200, &window, [&window]() {
                qInfo().noquote() << "[SMOKE] 播放 2.1s（已翻页）："
                                  << window.smokePlayState();
            });
            // 播放中在影片条上点 4.9s（接近 6s 片尾）：
            // 应立刻停 → 标尺挪过去 → 立刻从新位置重播；
            // 视窗钉在 4~6 不动，播放头自己往右端走
            QTimer::singleShot(9400, &window, [&window]() {
                window.smokeClickCursor(4.9);
                qInfo().noquote() << "[SMOKE] 播放中点击 4.9s（刚重播）："
                                  << window.smokePlayState();
            });
            QTimer::singleShot(10400, &window, [&window]() {
                qInfo().noquote() << "[SMOKE] 点击 4.9s 播 1s 后（末页）："
                                  << window.smokePlayState();
                window.smokePressSpace();  // 停止走空格键，顺带验证快捷键
            });
            QTimer::singleShot(10900, &window, [&window]() {
                qInfo().noquote() << "[SMOKE] 空格停止后：" << window.smokePlayState();
            });
            // 再播一次，然后拖底部滑块 —— 拖动滑块应当立刻停止播放。
            // 播放头必须先挪回 0：停在 5.7s 的话只剩 0.3 秒就到片尾，还没来得及
            // 拖滑块它自己就播完停了，测不出东西。
            QTimer::singleShot(11200, &window, [&window]() {
                window.smokeApplyView(0.0, 2.0);
                window.smokeSetCursor(0.0);
                window.smokeTogglePlay();
            });
            QTimer::singleShot(11600, &window, [&window]() {
                qInfo().noquote() << "[SMOKE] 拖滑块前（应在播）："
                                  << window.smokePlayState();
                window.smokePanSlider(0.0, 2.0, 0.5);  // 把深蓝滑块拖到片子中间
            });
            QTimer::singleShot(11900, &window, [&window]() {
                qInfo().noquote() << "[SMOKE] 拖滑块后（应已停止）："
                                  << window.smokePlayState();
            });
            // 损坏容错核对：坏包被跳过多少、画面顶替了几段、声音补了几处。
            // 健康文件应该全 0；损坏文件应当"有跳过但播放照样走完"。
            QTimer::singleShot(12500, &window, [&window]() {
                qInfo().noquote() << "[SMOKE] 损坏统计：" << window.smokeDamageState();
            });
        }
    }

    // 卡死自检钩子（P0-1 看门狗）：LRCONVERT_WATCHDOG_TEST=1 时把 GUI 线程
    // 堵住 25 秒 —— 期间事件循环不转、心跳定时器发不出来，看门狗应当在
    // 15 秒后写一份 dump 下来。"活着但没反应"是崩溃里最难查的一类：进程还在，
    // 用户只能说一句"点了没反应"，没有日志也没有现场，所以留个一键自检。
    if (qEnvironmentVariableIsSet("LRCONVERT_WATCHDOG_TEST")) {
        QTimer::singleShot(1500, &window, []() {
            qWarning().noquote()
                << QStringLiteral("[P0-1] 卡死自检：GUI 线程即将停止响应 25 秒…");
            std::this_thread::sleep_for(std::chrono::milliseconds(25000));
            qWarning().noquote()
                << QStringLiteral("[P0-1] 卡死自检：已恢复响应，应已写出一份 dump");
        });
    }

    // 语言切换自检钩子：LRCONVERT_LANG_TEST=1 时跑一遍
    // 「切中文 → 核对界面文本 → 切回英文 → 再核对」，
    // 验证"装卸 translator + retranslate() + QSettings 记忆"整条链路
    // （无需人工点下拉框；正常运行时这个环境变量不存在）。
    // 可与 LRCONVERT_SMOKE_TEST 同时设（冒烟的退出定时器会一并收场），
    // 也可以单独设——此时 6.5s 自己退出。
    if (qEnvironmentVariableIsSet("LRCONVERT_LANG_TEST")) {
        QTimer::singleShot(3000, &window, [&window]() {
            const bool ok = window.smokeSwitchLanguage(QStringLiteral("zh_CN"));
            qInfo().noquote() << QStringLiteral("[SMOKE] 切中文：")
                              << (ok ? QStringLiteral("成功") : QStringLiteral("失败"))
                              << QStringLiteral(" 提示页=") << window.smokeTipText()
                              << QStringLiteral(" 版本行=") << window.smokeVersionLine()
                              << QStringLiteral(" 静态文本=") << window.smokeStaticSample();
        });
        QTimer::singleShot(4500, &window, [&window]() {
            const bool ok = window.smokeSwitchLanguage(QStringLiteral("en"));
            qInfo().noquote() << QStringLiteral("[SMOKE] 切回英文：")
                              << (ok ? QStringLiteral("成功") : QStringLiteral("失败"))
                              << QStringLiteral(" 提示页=") << window.smokeTipText()
                              << QStringLiteral(" 版本行=") << window.smokeVersionLine()
                              << QStringLiteral(" 静态文本=") << window.smokeStaticSample();
        });
        if (!qEnvironmentVariableIsSet("LRCONVERT_SMOKE_TEST")) {
            QTimer::singleShot(6000, &app, &QCoreApplication::quit);
        }
    }

    // 最后一道兜底：事件循环里要是真漏出异常来（Qt 自己不用异常，但
    // std::vector / std::string 的分配失败、第三方库都可能抛），
    // 默认行为是 std::terminate() —— 窗口直接消失，什么都不说。
    // 这里至少把原因打出来，也让日志留下最后一行。
    int rc = 0;
    try {
        rc = app.exec();
    } catch (const std::exception &e) {
        qCritical().noquote() << QStringLiteral("未捕获异常，程序即将退出：")
                              << QString::fromUtf8(e.what());
        QMessageBox::critical(
            nullptr, QCoreApplication::translate("main", "程序异常"),
            QCoreApplication::translate("main",
                                        "发生了未处理的异常，程序即将退出。\n\n%1\n\n"
                                        "详细信息已写入日志文件：\n%2")
                .arg(QString::fromUtf8(e.what()), logPath));
        rc = 1;
    } catch (...) {
        qCritical().noquote() << QStringLiteral("未捕获的未知异常，程序即将退出");
        QMessageBox::critical(
            nullptr, QCoreApplication::translate("main", "程序异常"),
            QCoreApplication::translate("main",
                                        "发生了未处理的异常，程序即将退出。\n\n"
                                        "详细信息已写入日志文件：\n%1")
                .arg(logPath));
        rc = 1;
    }

    qInfo().noquote() << QStringLiteral("════ 正常退出（%1）════").arg(rc);

    // 退出流程：先让看门狗别再盯（否则收尾期间的反初始化会被当成"卡死"，
    // 莫名其妙多一份 dump），再记录"这一跑是干净退出的"。
    lr::platform::shutdownDiagnostics();
    lr::platform::markCleanExit(dumpDirPath.toStdWString());

    if (g_logFile != nullptr) {
        std::fclose(g_logFile);
        g_logFile = nullptr;
    }
    return rc;
}
