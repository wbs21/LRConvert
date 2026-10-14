// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "MainWindow.h"
#include "platform/WindowFrame.h"

#include <QAbstractButton>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProgressBar>
#include <QRegularExpression>
#include <QScreen>
#include <QResizeEvent>
#include <QTextCursor>
#include <QTextEdit>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QTimer>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

#include "core/PathUtil.h"
#include "core/TimeUtil.h"
#include "core/Version.h"
#include "ffmpeg/FfmpegApi.h"
#include "tasks/TranscodeTask.h"
#include "ffmpeg/FfmpegCapability.h"
#include "tasks/RemuxTask.h"

namespace {

/// 缩略图取样位置（占可见区间的比例）：左 / 1/4 / 中 / 3/4 / 右。
/// 末点取 0.999 而不是 1.0，避免 seek 到正好等于片长的位置取不到帧
constexpr double kThumbFrac[5] = {0.0, 0.25, 0.5, 0.75, 0.999};

// ---- 整轨波形的三个参数 ----
// 密度：峰值只画个包络，400 点/秒足够细（放大到 3 秒窗口仍有 1200 点）；
//       1 小时片 ≈ 5.8MB。超过 2 小时自动降档，免得内存爆。
constexpr double kWavePointsPerSec = 400.0;
constexpr double kWavePointsPerSecLong = 100.0;
// 分段数：多于线程数，这样"可见区优先"才有得挑
constexpr int kWaveSegments = 16;
// worker 数（用户指定 4）
constexpr int kWaveWorkers = 4;

constexpr const char *kAppName = "LRconvert";
constexpr const char *kVersionUrl =
    "https://gitee.com/wbs21/lrconvert/raw/main/version.md";
constexpr const char *kHomeUrl = "https://gitee.com/wbs21/lrconvert/";

// 「提示」标签页正文（Tab7 的 T7about）。按用户 2026-10-15 给的文案简化：
// 只留"它是什么"和"哪些功能会重编码"这两件事，技术细节一律不写。
const char *kAboutText =
    "LRconvert是一个FFmpeg命令行UI，所有功能都是基于FFmpeg完成。\n"
    "\n"
    "· 前四项功能（截取 / 连接 / 分离 / 合并）为流拷贝，不重新编码，\n"
    "  因此速度极快，且不会带来任何画质损失。\n"
    "· 批量压缩与自定义压缩会重新编码，画质与体积由码率决定。\n";

/// 版本号比较用：'3.10' -> {3, 10}
QList<int> parseVersion(const QString &text) {
    QList<int> parts;
    int current = -1;
    for (const QChar ch : text) {
        if (ch.isDigit()) {
            current = current < 0 ? ch.digitValue() : current * 10 + ch.digitValue();
        } else if (current >= 0) {
            parts.append(current);
            current = -1;
        }
    }
    if (current >= 0) parts.append(current);
    if (parts.isEmpty()) parts.append(0);
    return parts;
}

bool versionGreater(const QList<int> &a, const QList<int> &b) {
    const int count = std::max(a.size(), b.size());
    for (int i = 0; i < count; ++i) {
        const int x = i < a.size() ? a.at(i) : 0;
        const int y = i < b.size() ? b.at(i) : 0;
        if (x != y) return x > y;
    }
    return false;
}

bool isMovFamily(const QString &path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QStringLiteral("mp4") || ext == QStringLiteral("mov") ||
           ext == QStringLiteral("m4a") || ext == QStringLiteral("3gp");
}

/// 对应原命令里的 -movflags +faststart（让 mp4 可以边下边播）
std::vector<std::pair<std::string, std::string>> muxerOptionsFor(
    const QString &output) {
    if (!isMovFamily(output)) return {};
    return {{"movflags", "+faststart"}};
}

}  // namespace

MainWindow::~MainWindow() {
    // 空格快捷键的过滤器装在 qApp 上，必须在窗口销毁前摘掉，
    // 否则应用退出时 qApp 会回调到一个已经析构的对象
    qApp->removeEventFilter(this);
}

MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
    setupUi(this);
    qApp->installEventFilter(this);
    setWindowTitle(QString::fromUtf8(kAppName) + QStringLiteral(" v") +
                   QString::fromUtf8(lr::kAppVersion));
    setWindowIcon(QIcon(QStringLiteral(":/LRconvert.ico")));

    // 底部补一个进度条（不需要改动 mainui.ui）
    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    progressBar_->setFormat(QStringLiteral("空闲"));
    progressBar_->setVisible(false);
    progressBar_->setFixedHeight(20);
    verticalLayout->addWidget(progressBar_);

    setupTimeline();
    initWidgets();
    connectActions();

    connect(this, &MainWindow::ffmpegLoaded, this, &MainWindow::onFfmpegLoaded);
    connect(this, &MainWindow::hwEncodersProbed, this,
            &MainWindow::applyHwEncoders);
    connect(&signals_, &lr::TaskSignals::log, this, &MainWindow::appendLog);
    connect(&signals_, &lr::TaskSignals::progress, this, &MainWindow::setProgress);
    connect(&signals_, &lr::TaskSignals::finished, this,
            &MainWindow::onBatchFinished);

    infobox->document()->setMaximumBlockCount(800);

    appendLog(QStringLiteral("界面已就绪。此刻尚未加载任何 FFmpeg 动态库 —— "
                             "这是刻意的。"),
              lr::logcolor::kStep);
    qInfo().noquote() << "[M1] 界面就绪，标签页数：" << tabWidget->count();
    checkVersion();
}

// ---------------- 初始化 ----------------

QString MainWindow::smokeEncoderItems() const {
    auto items = [](QComboBox *box) {
        QStringList out;
        for (int i = 0; i < box->count(); ++i) out << box->itemText(i);
        return out.join(QLatin1Char(','));
    };
    // Tab5（批量压缩）与 Tab6（自定义压缩）两个下拉框都必须被过滤
    return QStringLiteral("Tab5=[%1] Tab6=[%2]")
        .arg(items(T5hwacc), items(T6quality));
}

void MainWindow::smokeLoadCutFile(const QString &path) {
    T1file->setText(path);
    loadCutSource();  // 引擎还没好时只会记下路径，等 onFfmpegLoaded 自动补导入
}

QString MainWindow::smokeCutState() const {
    if (!timeline_) return QStringLiteral("no-timeline");
    if (!cutLoaded_) return QStringLiteral("未载入");
    return QStringLiteral("已载入 时长=%1 缩略图=%2/5 波形点=%3 波形段=%4 关键帧=%5 起点=%6 终点=%7"
                          " 视窗=%8~%9 按钮=%10 目前时长=%11")
        .arg(formatTimecode(cutInfo_.durationSec))
        .arg(timeline_->thumbnailCount())
        .arg(timeline_->peakCount())
        .arg(QStringLiteral("%1/%2%3")
                 .arg(waveBuilder_.doneSegments())
                 .arg(waveBuilder_.totalSegments())
                 .arg(waveBuilder_.running() ? QStringLiteral("(算中)") : QStringLiteral("")))
        .arg(cutKeyframes_.size())
        .arg(formatTimecode(timeline_->startSec()))
        .arg(formatTimecode(timeline_->endSec()))
        .arg(formatTimecode(timeline_->viewFrom()))
        .arg(formatTimecode(timeline_->viewTo()))
        .arg(T1start->text())
        .arg(T1duration->text());
}

void MainWindow::smokeApplyRange(double start, double end) {
    if (!timeline_ || !cutLoaded_) return;
    timeline_->setStartSec(start);
    timeline_->setEndSec(end);
    applySnappedRange();
}

void MainWindow::smokeTogglePlay() { togglePlayback(); }

void MainWindow::smokeSetCursor(double sec) {
    if (timeline_) timeline_->setCursorSec(sec);
}

void MainWindow::smokeApplyView(double from, double to) {
    if (!timeline_) return;
    timeline_->setViewRange(from, to);
}

void MainWindow::smokeClickCursor(double sec) {
    // 走真实鼠标那条路（cursorChanged → cursorSettled），
    // 这样"播放中点击 = 停 → 定位 → 立刻重播"整条链路都进了冒烟
    if (timeline_) timeline_->smokeClickCursor(sec);
}

void MainWindow::smokePressSpace() {
    // 走真实按键路径（会经过装在 qApp 上的 eventFilter），
    // 这样"空格 = 播放/停止"这条链路也进了冒烟
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(this, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(this, &release);
}

QString MainWindow::smokePlayState() const {
    const QPixmap pixmap = T1preview->pixmap();
    return QStringLiteral("播放中=%1 按钮=%2 标尺=%3 播放位置=%4 有音轨=%5 声卡已开=%6"
                          " 视窗=%7~%8 画面=%9x%10")
        .arg(playRunning_ ? QStringLiteral("是") : QStringLiteral("否"))
        .arg(T1play->text())
        .arg(formatTimecode(timeline_ ? timeline_->cursorSec() : 0.0))
        .arg(formatTimecode(player_.position()))
        .arg(player_.hasAudioTrack() ? QStringLiteral("有") : QStringLiteral("无"))
        .arg(player_.audioActive() ? QStringLiteral("是") : QStringLiteral("否"))
        .arg(formatTimecode(timeline_ ? timeline_->viewFrom() : 0.0))
        .arg(formatTimecode(timeline_ ? timeline_->viewTo() : 0.0))
        .arg(pixmap.isNull() ? 0 : pixmap.width())
        .arg(pixmap.isNull() ? 0 : pixmap.height());
}

QString MainWindow::smokePreviewInfo() const {
    const QPixmap pixmap = T1preview->pixmap();
    return QStringLiteral("窗口=%1x%2 预览窗=%3x%4 画面=%5x%6 日志区高=%7")
        .arg(width())
        .arg(height())
        .arg(T1preview->width())
        .arg(T1preview->height())
        .arg(pixmap.isNull() ? 0 : pixmap.width())
        .arg(pixmap.isNull() ? 0 : pixmap.height())
        .arg(infobox->height());
}

void MainWindow::smokeNudgeOffScreen() {
    // 人为把标题栏顶出屏幕（y 取 -40），造出 BUG-001 的现场，
    // 再看 keepWindowOnScreen() 能不能把它拉回可见区。
    // 本机上真实的最大化/还原不复现这个 bug，只能这样验证修法本身。
    move(x(), -40);
    qInfo().noquote() << "[SMOKE] 几何·已顶出（修正前）：" << smokeWindowGeom();
    // 与 changeEvent 里一样延后一拍：等 frameGeometry() 同步到真实外框
    QTimer::singleShot(100, this, &MainWindow::keepWindowOnScreen);
}

void MainWindow::smokeSetMaximized(bool on) {
    if (on) {
        showMaximized();
    } else {
        showNormal();
    }
}

QString MainWindow::smokeWindowGeom() const {
    const QRect g = geometry();
    const QRect fr = frameGeometry();
    const QRect ng = normalGeometry();
    const QScreen *s = screen();
    const QRect avail = s ? s->availableGeometry() : QRect();
    const QRect scr = s ? s->geometry() : QRect();
    return QStringLiteral(
               "状态=%1 客户区=%2,%3 %4x%5 外框=%6,%7 %8x%9 常规=%10,%11 %12x%13 "
               "最小高=%14 屏幕=%15,%16 %17x%18 可用=%19,%20 %21x%22")
        .arg(isMaximized() ? QStringLiteral("最大化")
                           : (isMinimized() ? QStringLiteral("最小化")
                                            : QStringLiteral("正常")))
        .arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height())
        .arg(fr.x()).arg(fr.y()).arg(fr.width()).arg(fr.height())
        .arg(ng.x()).arg(ng.y()).arg(ng.width()).arg(ng.height())
        .arg(minimumSizeHint().height())
        .arg(scr.x()).arg(scr.y()).arg(scr.width()).arg(scr.height())
        .arg(avail.x()).arg(avail.y()).arg(avail.width()).arg(avail.height());
}

QString MainWindow::smokeTipText() const {
    const QStringList lines =
        T7about->toPlainText().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    return lines.mid(0, 2).join(QLatin1String(" / "));
}

void MainWindow::smokePanSlider(double from, double to, double moveFrac) {
    if (!timeline_ || !cutLoaded_) return;
    timeline_->setViewRange(from, to);
    timeline_->smokeDragView(0.0, moveFrac);
}

void MainWindow::initWidgets() {
    T1time1->setText(QStringLiteral("00:00:00.000"));
    T1time2->setText(QStringLiteral("00:00:00.000"));

    T2listbox->setDragDropMode(QAbstractItemView::InternalMove);
    T2listbox->setDefaultDropAction(Qt::MoveAction);
    T2listbox->setDragEnabled(true);
    T2listbox->setAcceptDrops(true);
    T2listbox->setDropIndicatorShown(true);
    T2listbox->setSelectionMode(QAbstractItemView::ExtendedSelection);

    auto fill = [](QComboBox *box, const lr::pathutil::NameValues &values) {
        box->clear();
        for (const auto &kv : values) {
            box->addItem(QString::fromStdString(kv.first));
        }
        box->setCurrentIndex(0);
    };

    T5bit->clear();
    for (const char *v : {"1", "2", "4", "8", "12", "16", "20", "30"}) {
        T5bit->addItem(QString::fromUtf8(v));
    }
    T5bit->setCurrentIndex(2);
    fill(T5size, lr::pathutil::scaleFilters());
    // 编码器下拉框启动时只放软件编码器；硬件项要等后台探测确认本机可用后
    // 再由 applyHwEncoders() 追加——不支持的项从头到尾不会出现。
    fill(T5hwacc, lr::pathutil::softwareEncoders());

    fill(T6quality, lr::pathutil::softwareEncoders());
    T6speed->clear();
    for (const char *v : {"fast", "medium", "slow"}) {
        T6speed->addItem(QString::fromUtf8(v));
    }
    T6speed->setCurrentIndex(0);
    T6grade->clear();
    for (const char *v : {"5.1", "4.2", "4.1"}) {
        T6grade->addItem(QString::fromUtf8(v));
    }
    T6grade->setCurrentIndex(2);
    fill(T6type, lr::pathutil::scaleFilters());
    T6level->clear();
    for (const char *v : {"1", "2", "4", "8", "12", "16", "20", "30"}) {
        T6level->addItem(QString::fromUtf8(v));
    }
    T6level->setCurrentIndex(2);
    T6audio->clear();
    for (const char *v : {"96", "128", "160", "196"}) {
        T6audio->addItem(QString::fromUtf8(v));
    }
    T6audio->setCurrentIndex(1);

    T7this->setText(QStringLiteral("当前版本 %1").arg(QString::fromUtf8(lr::kAppVersion)));
    T7about->setPlainText(QString::fromUtf8(kAboutText));

    setBusy(false);
}

void MainWindow::connectActions() {
    connect(T1add, &QPushButton::clicked, this, [this]() {
        pickVideoFile(T1file);
        loadCutSource();
    });
    connect(T1file, &QLineEdit::editingFinished, this, &MainWindow::loadCutSource);
    connect(T1start, &QPushButton::clicked, this, &MainWindow::cutFile);
    connect(T1setStart, &QPushButton::clicked, this, &MainWindow::setCutStart);
    connect(T1setEnd, &QPushButton::clicked, this, &MainWindow::setCutEnd);
    connect(T1time1, &QLineEdit::editingFinished, this, &MainWindow::onCutStartEdited);
    connect(T1time2, &QLineEdit::editingFinished, this, &MainWindow::onCutEndEdited);

    connect(T2add, &QPushButton::clicked, this, &MainWindow::addToList);
    connect(T2del, &QPushButton::clicked, this, &MainWindow::removeFromList);
    connect(T2start, &QPushButton::clicked, this, &MainWindow::joinFiles);
    connect(T2stop, &QPushButton::clicked, this, &MainWindow::stopTasks);

    connect(T3add, &QPushButton::clicked, this,
            [this]() { pickVideoFile(T3file); });
    connect(T3start, &QPushButton::clicked, this, &MainWindow::splitFile);
    connect(T3stop, &QPushButton::clicked, this, &MainWindow::stopTasks);

    connect(T4addvideo, &QPushButton::clicked, this,
            [this]() { pickVideoFile(T4file); });
    connect(T4addaudio, &QPushButton::clicked, this, &MainWindow::pickAudioFile);
    connect(T4start, &QPushButton::clicked, this, &MainWindow::mergeFile);
    connect(T4stop, &QPushButton::clicked, this, &MainWindow::stopTasks);

    connect(T5add, &QPushButton::clicked, this, &MainWindow::browseDir);
    connect(T5start, &QPushButton::clicked, this, &MainWindow::batchCode);
    connect(T5stop, &QPushButton::clicked, this, &MainWindow::stopTasks);

    connect(T6add, &QPushButton::clicked, this,
            [this]() { pickVideoFile(T6file); });
    connect(T6start, &QPushButton::clicked, this, &MainWindow::customCode);
    connect(T6stop, &QPushButton::clicked, this, &MainWindow::stopTasks);

    connect(T7check, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl(QString::fromUtf8(kHomeUrl)));
    });
}

// ---------------- FFmpeg 延迟加载 ----------------

void MainWindow::startAsyncInit() {
    if (ffmpegLoadStarted_) return;
    ffmpegLoadStarted_ = true;

    appendLog(QStringLiteral("开始在后台加载 FFmpeg 动态库…（加载期间功能按钮不可用）"));
    qInfo().noquote() << "[M1] 界面已显示，开始后台加载 FFmpeg 动态库";

    auto guard = QPointer<MainWindow>(this);
    (void)QtConcurrent::run([this, guard]() {
        auto &api = FfmpegApi::instance();
        std::string error;
        const bool ok = api.load(error);

        QString detail;
        if (ok) {
            const FfmpegCapability cap = collectCapability(api);
            detail = QString::fromStdString(api.summary()) +
                     QStringLiteral("\n视频解码 %1 · 视频编码 %2 ｜ 音频解码 %3 · 音频编码 %4"
                                    "\n解封装器 %5 个 · 封装器 %6 个")
                         .arg(cap.decoders.video.size())
                         .arg(cap.encoders.video.size())
                         .arg(cap.decoders.audio.size())
                         .arg(cap.encoders.audio.size())
                         .arg(cap.demuxers.size())
                         .arg(cap.muxers.size());
        } else {
            detail = QString::fromStdString(error);
        }
        if (guard) emit ffmpegLoaded(ok, detail);

        // 硬件编码器识别：探测路径与 TranscodeTask 的打开路径一致
        // （find_encoder + 最小配置 open2），本机打不开的编码器不进下拉框，
        // 避免"选完 nvenc 一点开始才回退 libx264"的体验。
        // 探测在后台线程完成，期间下拉框仍是全量列表，结果到了再收敛。
        if (ok && guard) {
            std::vector<std::string> names;
            for (const auto &kv : lr::pathutil::hwEncoders()) {
                if (kv.second != "libx264") names.push_back(kv.second);
            }
            QElapsedTimer probeClock;
            probeClock.start();
            const auto probes = probeHardwareEncoders(api, names);
            const int probeMs = static_cast<int>(probeClock.elapsed());

            QStringList available;
            QStringList hidden;
            for (const auto &p : probes) {
                if (p.available) {
                    available << QString::fromStdString(p.name);
                    qInfo().noquote()
                        << "[M6] 硬件编码器可用：" << p.name.c_str();
                } else {
                    const std::string req =
                        lr::pathutil::hwEncoderRequirement(p.name);
                    const QString reason =
                        req.empty()
                            ? QString::fromStdString(p.error)
                            : QStringLiteral("%1；%2")
                                  .arg(QString::fromStdString(req),
                                       QString::fromStdString(p.error));
                    hidden << QStringLiteral("%1（%2)")
                                  .arg(QString::fromStdString(p.name), reason);
                    qInfo().noquote()
                        << "[M6] 硬件编码器不可用（将隐藏）：" << p.name.c_str()
                        << "·" << p.error.c_str();
                }
            }
            emit hwEncodersProbed(
                available.join(QLatin1Char(' ')),
                hidden.isEmpty()
                    ? QString()
                    : QStringLiteral("（识别耗时 %1 ms）").arg(probeMs) +
                          QLatin1Char('\n') + hidden.join(QLatin1Char('\n')));
        }
    });
}

void MainWindow::onFfmpegLoaded(bool ok, const QString &detail) {
    engineReady_ = ok;

    if (!ok) {
        qWarning().noquote() << "[M1] 动态库加载失败：" << detail;
        appendLog(QStringLiteral("✗ FFmpeg 动态库加载失败：") + detail,
                  lr::logcolor::kError);
        appendLog(QStringLiteral("  请把 avutil / avcodec / avformat / swscale / "
                                 "swresample 五个 DLL 放到程序目录，"
                                 "或用环境变量 LRCONVERT_FFMPEG_DIR 指定。"),
                  lr::logcolor::kWarn);
        appendLog(QStringLiteral("  在动态库就绪之前，各功能按钮不可用。"),
                  lr::logcolor::kWarn);
        return;
    }

    qInfo().noquote() << "[M1] 动态库加载成功：" << detail;
    appendLog(QStringLiteral("✓ ") + detail, lr::logcolor::kOk);
    appendLog(QStringLiteral("引擎就绪，可以开始处理任务了。"),
              lr::logcolor::kOk);
    setBusy(false);  // Loading → Ready：放开六个开始按钮

    // 引擎就绪之前如果已经选过文件，这里补上导入（此前只是记下了路径）
    if (!T1file->text().trimmed().isEmpty()) loadCutSource();

    const FfmpegCapability cap = collectCapability(FfmpegApi::instance());
    auto join = [](const std::vector<std::string> &v) {
        QStringList out;
        for (const auto &s : v) out << QString::fromStdString(s);
        return out.join(QStringLiteral(" "));
    };
    appendLog(QStringLiteral("  视频解码：") + join(cap.decoders.video));
    appendLog(QStringLiteral("  视频编码：") + join(cap.encoders.video));
    appendLog(QStringLiteral("  音频解码：") + join(cap.decoders.audio));
    appendLog(QStringLiteral("  音频编码：") + join(cap.encoders.audio));
    appendLog(QStringLiteral("  解封装器：") + join(cap.demuxers));
    appendLog(QStringLiteral("  封装器：") + join(cap.muxers));

    if (cap.decoders.video.empty()) {
        appendLog(QStringLiteral("  ⚠ 这份构建没有编译任何视频解码器，"
                                 "压缩类功能不可用。"),
                  lr::logcolor::kWarn);
    }
}

void MainWindow::applyHwEncoders(const QString &availableCsv,
                                 const QString &hiddenReport) {
    const QStringList available =
        availableCsv.split(QLatin1Char(' '), Qt::SkipEmptyParts);

    // 重建下拉框：libx264（软件）永远保留，硬件编码器只留探测可用的。
    // 保留当前选中项（若它没被隐藏），否则回落到第一项。
    auto refill = [&available](QComboBox *box) {
        const QString keep = box->currentText();
        box->clear();
        for (const auto &kv : lr::pathutil::hwEncoders()) {
            const bool software = kv.second == "libx264";
            if (software ||
                available.contains(QString::fromStdString(kv.second))) {
                box->addItem(QString::fromStdString(kv.first));
            }
        }
        const int idx = box->findText(keep);
        box->setCurrentIndex(idx >= 0 ? idx : 0);
    };
    refill(T5hwacc);
    refill(T6quality);

    if (hiddenReport.isEmpty()) {
        if (available.isEmpty()) {
            appendLog(QStringLiteral("硬件编码器识别：本机没有可用的硬件编码器，"
                                     "压缩将使用 libx264。"),
                      lr::logcolor::kStep);
        } else {
            appendLog(QStringLiteral("硬件编码器识别：本机可用 %1，"
                                     "已加入编码器下拉框。")
                          .arg(available.join(QStringLiteral("、"))),
                      lr::logcolor::kOk);
        }
    } else {
        appendLog(QStringLiteral("硬件编码器识别完成，已隐藏本机不支持的选项："),
                  lr::logcolor::kStep);
        if (!available.isEmpty()) {
            appendLog(QStringLiteral("  · 可用并已加入：") +
                          available.join(QStringLiteral("、")),
                      lr::logcolor::kOk);
        }
        const QStringList lines =
            hiddenReport.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            appendLog(QStringLiteral("  · ") + line, lr::logcolor::kMuted);
        }
    }
}

// ---------------- 任务反馈 ----------------

void MainWindow::appendLog(const QString &text, const QString &color) {
    infobox->moveCursor(QTextCursor::End);
    infobox->insertHtml(
        QStringLiteral("<span style=\"color:%1\">%2</span><br>")
            .arg(color, text.toHtmlEscaped()));
    infobox->ensureCursorVisible();
}

void MainWindow::setProgress(int percent, const QString &detail) {
    if (percent < 0) {
        progressBar_->setRange(0, 0);
        progressBar_->setFormat(detail.isEmpty() ? QStringLiteral("处理中…")
                                                 : detail);
    } else {
        progressBar_->setRange(0, 100);
        progressBar_->setValue(percent);
        progressBar_->setFormat(detail + QStringLiteral(" · %p%"));
    }
}

void MainWindow::onBatchFinished(bool ok) {
    busy_ = false;
    stopAll_.store(false);
    setBusy(false);
    progressBar_->setVisible(false);
    progressBar_->setFormat(QStringLiteral("空闲"));
    if (ok) {
        appendLog(QStringLiteral("全部任务已完成。"), lr::logcolor::kOk);
    } else {
        appendLog(QStringLiteral("任务结束（失败或已被停止）。"),
                  lr::logcolor::kWarn);
    }
}

void MainWindow::setBusy(bool busy) {
    // 引擎没就绪（正在加载 / 加载失败）时，六个「开始」按钮一律置灰；
    // 就绪后由 busy 决定：忙 → 停止按钮可用，闲 → 开始按钮可用。
    // 对应重构方案 §5.4 的引擎三态：Loading / Ready / Missing。
    // 两个编码器下拉框同样跟随：加载中/失败时置灰（此时只有软件编码器，
    // 选了也用不了），任务执行期间锁定防止中途改参数。
    const bool usable = !busy && engineReady_;
    for (QPushButton *button : {T2start, T3start, T4start, T5start, T6start}) {
        button->setEnabled(usable);
    }
    for (QPushButton *button : {T2stop, T3stop, T4stop, T5stop, T6stop}) {
        button->setEnabled(busy);
    }
    for (QComboBox *box : {T5hwacc, T6quality}) {
        box->setEnabled(usable);
    }

    // Tab1 的「开始剪辑 ⇄ 停止编码」是单一切换按钮：
    // 执行中变红可点（点了就是停止），空闲时可点（需要已载入视频）
    const bool cutRunnable = !busy && engineReady_ && cutLoaded_;
    T1start->setEnabled(busy || cutRunnable);
    if (busy) {
        T1start->setText(QStringLiteral("停止编码"));
        T1start->setStyleSheet(QStringLiteral(
            "QPushButton{background:#D32F2F;color:#FFFFFF;border:1px solid "
            "#B71C1C;border-radius:3px;padding:4px 8px;}"
            "QPushButton:hover{background:#E53935;}"
            "QPushButton:pressed{background:#B71C1C;}"));
    } else {
        T1start->setText(QStringLiteral("开始剪辑"));
        T1start->setStyleSheet(cutStartStyle_);
    }
    // 执行期间锁住时间轴与输入，避免中途改参数
    T1file->setEnabled(!busy);
    T1add->setEnabled(!busy);
    if (timeline_) timeline_->setEnabled(!busy && engineReady_ && cutLoaded_);
    T1time1->setEnabled(!busy && cutLoaded_);
    T1time2->setEnabled(!busy && cutLoaded_);
    T1setStart->setEnabled(!busy && cutLoaded_);
    T1setEnd->setEnabled(!busy && cutLoaded_);
    // 播放按钮：编码执行期间不给播（会跟编码抢 CPU，画面也没意义）
    T1play->setEnabled(!busy && engineReady_ && cutLoaded_);
    if (busy && playRunning_) stopPlayback();
}

void MainWindow::runTasks(std::vector<std::shared_ptr<lr::ITask>> jobs) {
    if (jobs.empty()) return;
    if (busy_) {
        appendLog(QStringLiteral("已有任务在执行，请先等待完成或点击停止。"),
                  lr::logcolor::kWarn);
        return;
    }
    if (!FfmpegApi::instance().isLoaded()) {
        QMessageBox::critical(
            this, QStringLiteral("FFmpeg 未就绪"),
            QStringLiteral("FFmpeg 动态库还没有加载完成（或加载失败），"
                           "暂时无法执行任务。\n"
                           "详情请看下方日志区。"));
        return;
    }

    jobs_ = std::move(jobs);
    stopAll_.store(false);
    busy_ = true;
    setBusy(true);
    progressBar_->setVisible(true);
    progressBar_->setValue(0);

    auto guard = QPointer<MainWindow>(this);
    (void)QtConcurrent::run([this, guard, jobs = jobs_]() {
        bool ok = true;
        const int total = static_cast<int>(jobs.size());
        for (int i = 0; i < total; ++i) {
            if (!guard) return;
            if (stopAll_.load()) break;
            if (total > 1) {
                emit signals_.log(
                    QStringLiteral("[%1/%2] %3").arg(i + 1).arg(total).arg(
                        jobs[i]->title()),
                    lr::logcolor::kMuted);
            }
            const bool succeeded = jobs[i]->run(signals_);
            jobs[i]->cleanup();
            if (!succeeded) {
                ok = false;
                break;
            }
        }
        if (guard) emit signals_.finished(ok);
    });
}

void MainWindow::stopTasks() {
    if (!busy_) return;
    stopAll_.store(true);
    appendLog(QStringLiteral("正在停止，等待当前任务正常收尾…"),
              lr::logcolor::kWarn);
    for (auto &job : jobs_) {
        job->requestStop();
    }
}

// ---------------- 通用辅助 ----------------

void MainWindow::pickVideoFile(QLineEdit *target) {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择视频文件"), QString(),
        QString::fromStdString(lr::pathutil::videoFileFilter()));
    if (!path.isEmpty()) target->setText(path);
}

void MainWindow::pickAudioFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择音频文件"), QString(),
        QString::fromStdString(lr::pathutil::audioFileFilter()));
    if (!path.isEmpty()) T4audio->setText(path);
}

bool MainWindow::checkSource(const QString &path) {
    if (path.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("缺少输入"),
                             QStringLiteral("请先选择一个输入文件。"));
        return false;
    }
    if (!QFileInfo::exists(path)) {
        QMessageBox::warning(this, QStringLiteral("文件不存在"),
                             QStringLiteral("找不到文件：\n%1").arg(path));
        return false;
    }
    return true;
}

// ---------------- Tab 1：截取（可视化时间轴） ----------------

void MainWindow::setupTimeline() {
    timeline_ = new lr::ui::TimelineWidget(this);
    T1timelineLayout->addWidget(timeline_);
    timeline_->setEnabled(false);

    // 「开始剪辑」的蓝色样式写在 mainui.ui 里，这里先记下来：
    // 执行任务时按钮要变成红色的「停止编码」，任务结束后恢复成这个蓝色。
    cutStartStyle_ = T1start->styleSheet();

    // 播放按钮同理：常态是绿色「▶ 播放」，播放中换成红色「■ 停止」。
    playStyle_ = T1play->styleSheet();
    T1play->setEnabled(false);
    connect(T1play, &QPushButton::clicked, this, &MainWindow::togglePlayback);

    // 播放引擎的帧回调：在播放线程里触发，这里切回 GUI 线程再画。
    // 注意用 cutToken_ 挡住"换了文件之后旧帧才送到"的情况。
    player_.setFrameCallback([this](lr::FramePlayer::FrameData &&data) {
        QImage image = toQImage(data.rgb, data.width, data.height);
        const double sec = data.sec;
        const int token = cutToken_;
        auto guard = QPointer<MainWindow>(this);
        QMetaObject::invokeMethod(
            this,
            [this, guard, token, image, sec]() {
                if (!guard || token != cutToken_ || !playRunning_) return;
                setPreviewImage(image);
                // 下面两步是播放替用户改的界面状态，先立起标志：
                // 否则它们触发的 cursorChanged 会被当成"用户点了别处"又去 seek，
                // 播放就会自己追自己、抖成一团
                playUpdatingCursor_ = true;
                timeline_->setCursorSec(sec);   // 蓝标尺跟着播放位置走
                // 翻页：页内不动，播放头走到右端才把影片整页向左跳
                timeline_->pageViewForCursor();
                playUpdatingCursor_ = false;
            },
            Qt::QueuedConnection);
    });
    player_.setEndCallback([this]() {
        const int epoch = playEpoch_;   // 认这次播放；晚到的旧消息会被丢掉
        auto guard = QPointer<MainWindow>(this);
        QMetaObject::invokeMethod(this, [this, guard, epoch]() {
            if (!guard || epoch != playEpoch_) return;
            onPlaybackEnded();
        }, Qt::QueuedConnection);
    });

    // 预览窗：最小高度沿用 .ui 里的 203px，界面拉高时吃掉剩余空间跟着变大
    T1preview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    T1preview->setMinimumHeight(203);
    // 索引 3 = 预览窗那一行（文件行 / 起终点行 / 时间轴 / 预览行 / 信息 / 提示）
    verticalLayout_2->setStretch(3, 1);

    // 底部日志区给个高度上限：否则窗口拉高时它先把自己的 sizeHint（约 250px）
    // 吃满，多出来的空间轮不到上面的预览窗。窗口小时它照样能被压缩，不受影响。
    groupBox->setMaximumHeight(170);

    // 拖动蓝标尺时的取帧节流（50ms）：保证跟手，又不会一帧一个解码任务
    previewThrottle_ = new QTimer(this);
    previewThrottle_->setSingleShot(true);
    previewThrottle_->setInterval(50);
    connect(previewThrottle_, &QTimer::timeout, this, &MainWindow::refreshCutPreview);

    // 缩放/平移停止后的缩略图与波形刷新（150ms）
    assetThrottle_ = new QTimer(this);
    assetThrottle_->setSingleShot(true);
    assetThrottle_->setInterval(150);
    connect(assetThrottle_, &QTimer::timeout, this,
            &MainWindow::refreshCutTimelineAssets);

    connect(timeline_, &lr::ui::TimelineWidget::cursorChanged, this,
            &MainWindow::onCutCursorChanged);
    connect(timeline_, &lr::ui::TimelineWidget::cursorSettled, this,
            &MainWindow::onCutCursorSettled);
    connect(timeline_, &lr::ui::TimelineWidget::rangeChanged, this,
            &MainWindow::onCutRangeChanged);
    connect(timeline_, &lr::ui::TimelineWidget::rangeSettled, this,
            &MainWindow::onCutRangeSettled);
    connect(timeline_, &lr::ui::TimelineWidget::viewChanged, this,
            &MainWindow::onCutViewChanged);
    // 用户亲手拖了底部滑块（不是播放翻页自己挪视窗）：正在播就停掉。
    // 理由：播放每帧都在翻页重设视窗，不停的话用户刚拖到的位置下一帧就被覆盖回去了。
    connect(timeline_, &lr::ui::TimelineWidget::viewDraggedByUser, this, [this]() {
        if (playRunning_) stopPlayback();
    });

    // 整轨波形的两个回调：
    // · 每算完一段就重绘一次（边算边画）—— 注意只 update()，不重算任何东西；
    // · 某一段算失败：记一行，并退回"只算可见区间"的兜底方案
    waveBuilder_.setProgressCallback([this](int index, double t0, double t1, int done,
                                            int total) {
        // 边算边画：每算完一段就重绘一次（只 update()，不重算任何东西）
        if (timeline_) timeline_->update();
        if (qEnvironmentVariableIsSet("LRCONVERT_SMOKE_TEST")) {
            // 冒烟才打：段完成的**顺序**能看出"可见区优先"是否生效
            qInfo().noquote() << "[SMOKE] 波形段" << index
                              << formatTimecode(t0) << "~" << formatTimecode(t1)
                              << done << "/" << total;
        }
        if (done >= total && total > 0) {
            appendLog(QStringLiteral("波形计算完成（%1 段）").arg(total), lr::logcolor::kMuted);
        }
    });
    waveBuilder_.setErrorCallback([this](const std::string &error) {
        appendLog(QStringLiteral("波形分段失败：") + QString::fromStdString(error),
                  lr::logcolor::kMuted);
        rebuildCutWaveform(cutToken_);
    });
}

QString MainWindow::formatTimecode(double sec) const {
    if (sec < 0.0) sec = 0.0;
    const int totalMs = static_cast<int>(std::llround(sec * 1000.0));
    const int ms = totalMs % 1000;
    const int totalSec = totalMs / 1000;
    const int s = totalSec % 60;
    const int m = (totalSec / 60) % 60;
    const int h = totalSec / 3600;
    return QString::asprintf("%02d:%02d:%02d.%03d", h, m, s, ms);
}

QImage MainWindow::toQImage(const std::vector<uint8_t> &rgb, int width, int height) {
    if (rgb.empty() || width <= 0 || height <= 0) return {};
    QImage image(width, height, QImage::Format_RGB888);
    const int rowBytes = width * 3;
    for (int y = 0; y < height; ++y) {
        std::memcpy(image.scanLine(y), rgb.data() + static_cast<size_t>(y) * rowBytes,
                    rowBytes);
    }
    return image;
}

void MainWindow::setPreviewImage(const QImage &image) {
    if (image.isNull()) return;
    cutPreviewImage_ = image;   // 存原图：窗口缩放时按新尺寸重缩放
    scalePreviewToLabel();
}

void MainWindow::scalePreviewToLabel() {
    if (cutPreviewImage_.isNull() || T1preview->width() <= 0) return;
    T1preview->setPixmap(QPixmap::fromImage(cutPreviewImage_)
                             .scaled(T1preview->size(), Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation));
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    // 预览窗高度跟着界面走，所以尺寸一变就要按新尺寸重缩放已缓存的那一帧
    scalePreviewToLabel();
}

void MainWindow::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange) return;
    // 只管"从最大化/全屏回到正常态"这一下：
    // Windows 在最大化态会把窗口外框顶到屏幕外 8px（把可拖拽的边框藏起来），
    // 这个 -8 的偏移有概率被当成正常几何带回来，于是还原后标题栏跑到屏幕
    // 上方——看不见、也抓不住拖动，只能靠拖边框把窗口拽回来（BUG-001）。
    if (isMaximized() || isFullScreen() || isMinimized()) return;
    // 不能立刻动手：还原这一刻 Windows 还在发 WM_MOVE / WM_SIZE，布局也还在
    // 跟着重排，此刻读到的几何是中间态，按它修正完会被随后的更新又顶回去。
    // 延后 100ms 等这些都落定再修（实测这一步是必要的）。
    QTimer::singleShot(100, this, &MainWindow::keepWindowOnScreen);
}

void MainWindow::keepWindowOnScreen() {
    if (isMaximized() || isFullScreen() || isMinimized()) return;
    const QScreen *s = screen();
    if (!s) return;
    const QRect avail = s->availableGeometry();

    // 外框比客户区多出来的那一圈（标题栏 + 边框）：按外框判断是否越界、
    // 但 setGeometry 用的是客户区，中间靠这几个偏移换算。
    // 先问系统要（Qt 的 frameGeometry() 在窗口被挪出屏幕时会退化成客户区，
    // 边框偏移算成 0，修正就会做歪），拿不到再回退到 Qt 的。
    int topOff = 0;
    int extraW = 0;
    int extraH = 0;
    if (!lr::platform::frameTopOffset(this, &topOff) ||
        !lr::platform::frameExtraSize(this, &extraW, &extraH)) {
        const QRect f = frameGeometry();
        topOff = f.y() - y();
        extraW = f.width() - width();
        extraH = f.height() - height();
    }
    const int leftOff = extraW / 2;   // 左右边框等宽

    int newX = x();
    int newY = y();
    int newW = width();
    int newH = height();
    if (y() + topOff < avail.top()) newY = avail.top() - topOff;
    if (x() + leftOff < avail.left()) newX = avail.left() - leftOff;
    if (width() + extraW > avail.width())
        newW = std::max(minimumWidth(), avail.width() - extraW);
    if (height() + extraH > avail.height())
        newH = std::max(minimumHeight(), avail.height() - extraH);
    if (newX == x() && newY == y() && newW == width() && newH == height()) return;
    setGeometry(newX, newY, newW, newH);
}

void MainWindow::updateCutInfoBar() {
    if (!cutInfo_.valid) {
        T1info->setText(QStringLiteral("尚未选择文件"));
        return;
    }
    QStringList parts;
    parts << QFileInfo(cutSource_).fileName();
    parts << formatTimecode(cutInfo_.durationSec);
    if (cutInfo_.hasVideo) {
        parts << QStringLiteral("%1*%2").arg(cutInfo_.width).arg(cutInfo_.height);
        parts << QStringLiteral("%1 fps").arg(cutInfo_.fps, 0, 'f', 2);
        parts << QString::fromStdString(cutInfo_.videoCodec);
    }
    if (cutInfo_.hasAudio) {
        parts << QString::fromStdString(cutInfo_.audioCodec);
        parts << QStringLiteral("%1Hz/%2ch").arg(cutInfo_.sampleRate).arg(cutInfo_.channels);
    }
    T1info->setText(parts.join(QStringLiteral("  ")));
}

double MainWindow::snapStartSec(double t) const {
    // 起点：吸附"选取点右侧"最近关键帧
    for (double key : cutKeyframes_) {
        if (key >= t - 1e-6) return key;
    }
    return t;  // 右侧没有关键帧就保持原样
}

double MainWindow::snapEndSec(double t) const {
    // 终点：吸附"选取点左侧"最近关键帧
    double best = -1.0;
    for (double key : cutKeyframes_) {
        if (key <= t + 1e-6) {
            best = key;
        } else {
            break;
        }
    }
    return best >= 0.0 ? best : t;
}

void MainWindow::syncCutTimeEdits() {
    if (!timeline_) return;
    T1time1->setText(formatTimecode(timeline_->startSec()));
    T1time2->setText(formatTimecode(timeline_->endSec()));
    // 「目前时长」= 终点 - 起点，跟着起终点实时变
    const double span = std::max(0.0, timeline_->endSec() - timeline_->startSec());
    T1duration->setText(formatTimecode(span));
}

void MainWindow::loadCutSource() {
    const QString path = T1file->text().trimmed();
    if (path.isEmpty()) return;
    if (!QFileInfo::exists(path)) {
        appendLog(QStringLiteral("文件不存在：") + path, lr::logcolor::kWarn);
        return;
    }
    if (!FfmpegApi::instance().isLoaded()) {
        cutLoaded_ = false;
        setBusy(busy_);
        appendLog(QStringLiteral("FFmpeg 尚未就绪，等加载完成后会自动导入该视频。"),
                  lr::logcolor::kMuted);
        return;
    }
    if (cutLoading_) return;

    cutLoading_ = true;
    cutLoaded_ = false;
    cutSource_ = path;
    const int token = ++cutToken_;
    // 换文件了：正在播就停掉，播放引擎里那份旧文件句柄也关掉，
    // 上一份文件的波形任务也要立刻停（否则它还会往旧的大数组里写）
    stopPlayback();
    player_.close();
    waveBuilder_.stop();
    cutPeaks_.reset();
    timeline_->clearMedia();
    T1duration->setText(formatTimecode(0.0));
    appendLog(QStringLiteral("正在导入 %1 …").arg(QFileInfo(path).fileName()),
              lr::logcolor::kStep);

    auto guard = QPointer<MainWindow>(this);
    (void)QtConcurrent::run([this, guard, path, token]() {
        auto &api = FfmpegApi::instance();
        const std::string src = path.toStdString();

        // 只做"快"的两件事：探测信息 + 三张缩略图 / 首帧预览。
        // 关键帧表和波形放到画面出来之后再补 —— 长视频这才不会卡在导入上。
        lr::MediaInfo info = lr::MediaProbe::probe(api, src);

        std::array<QImage, lr::ui::TimelineWidget::kThumbCount> thumbs;
        QImage preview;
        std::string openError;
        {
            std::lock_guard<std::mutex> lock(cutGrabberMutex_);
            std::string error;
            if (cutGrabber_.open(api, src, error)) {
                for (int i = 0; i < int(thumbs.size()); ++i) {
                    const double t = info.durationSec * kThumbFrac[i];
                    lr::FrameGrabber::Frame frame;
                    if (cutGrabber_.grabAt(t, 320, 180, frame, error)) {
                        thumbs[i] = toQImage(frame.rgb, frame.width, frame.height);
                    }
                }
                lr::FrameGrabber::Frame first;
                if (cutGrabber_.grabAt(0.0, 0, 0, first, error)) {
                    preview = toQImage(first.rgb, first.width, first.height);
                }
            } else {
                openError = error;
            }
        }

        QMetaObject::invokeMethod(
            this, [this, guard, token, info, thumbs, preview, openError]() {
                if (!guard || token != cutToken_) return;
                cutLoading_ = false;
                if (!openError.empty()) {
                    appendLog(QStringLiteral("取帧器打开失败：") +
                                  QString::fromStdString(openError),
                              lr::logcolor::kWarn);
                }
                if (!info.valid) {
                    cutLoaded_ = false;
                    appendLog(QStringLiteral("导入失败：") + QString::fromStdString(info.error),
                              lr::logcolor::kError);
                    setBusy(busy_);
                    return;
                }
                cutInfo_ = info;
                cutKeyframes_.clear();
                cutPeaks_.reset();
                cutLoaded_ = true;

                timeline_->setMedia(info.durationSec, info.fps);
                timeline_->setAudioAvailable(info.hasAudio);
                timeline_->setThumbnails(thumbs);
                // 默认：起点 = 0，终点 = 片长
                timeline_->setStartSec(0.0);
                timeline_->setEndSec(info.durationSec);
                timeline_->setCursorSec(0.0);
                syncCutTimeEdits();
                updateCutInfoBar();
                setPreviewImage(preview);
                setBusy(busy_);

                appendLog(QStringLiteral("已导入：%1 %2")
                              .arg(QFileInfo(cutSource_).fileName(),
                                   formatTimecode(info.durationSec)),
                          lr::logcolor::kOk);

                // 画面已经出来了，剩下这两样慢慢算
                startCutBackgroundAssets(token);
            },
            Qt::QueuedConnection);
    });
}

void MainWindow::startCutBackgroundAssets(int token) {
    if (!cutLoaded_) return;
    auto guard = QPointer<MainWindow>(this);
    const QString path = cutSource_;

    // 1) 关键帧表（只扫包头）—— 吸附要用
    (void)QtConcurrent::run([this, guard, path, token]() {
        const std::vector<double> keys =
            lr::MediaProbe::keyframeTimes(FfmpegApi::instance(), path.toStdString());
        QMetaObject::invokeMethod(this, [this, guard, token, keys]() {
            if (!guard || token != cutToken_ || !timeline_) return;
            cutKeyframes_ = keys;
            timeline_->setKeyframes(keys);
            appendLog(QStringLiteral("关键帧 %1 个").arg(keys.size()), lr::logcolor::kMuted);
        }, Qt::QueuedConnection);
    });

    // 2) 波形：整轨全量，4 线程分段算（可见区优先、边算边画）
    startFullWaveform(token);
}

void MainWindow::startFullWaveform(int token) {
    if (!cutLoaded_ || !timeline_) return;
    if (!cutInfo_.hasAudio) {
        appendLog(QStringLiteral("该视频没有音轨，不绘制波形。"), lr::logcolor::kMuted);
        return;
    }
    const double duration = cutInfo_.durationSec;
    if (duration <= 0.0) return;

    auto guard = QPointer<MainWindow>(this);
    const QString path = cutSource_;

    (void)QtConcurrent::run([this, guard, path, token, duration]() {
        if (!guard) return;
        // 整轨一条：密度固定（超长片自动降档，免得内存爆），
        // 开好之后大小不再变 —— 4 个线程各写互不重叠的一段
        const double pps = (duration > 7200.0) ? kWavePointsPerSecLong
                                               : kWavePointsPerSec;
        auto peaks = std::make_shared<lr::AudioPeaks>();
        peaks->allocate(0.0, duration, pps);

        QMetaObject::invokeMethod(this, [this, guard, token, peaks, duration, pps]() {
            if (!guard || token != cutToken_) return;
            cutPeaks_ = peaks;                 // 共享同一份，不是拷贝
            timeline_->setPeaks(peaks);
            // 先按当前可见区排一次优先级（导入时就是整片可见）
            waveBuilder_.setPriorityRange(timeline_->viewFrom(), timeline_->viewTo());
            const bool started = waveBuilder_.start(
                FfmpegApi::instance(), cutSource_.toStdString(), duration, pps,
                kWaveSegments, kWaveWorkers, peaks);
            if (!started) {
                appendLog(QStringLiteral("波形计算未能启动。"), lr::logcolor::kMuted);
            }
        }, Qt::QueuedConnection);
    });
}

/// 全量任务失败时的兜底：退回"只算可见区间"（旧方案）
void MainWindow::rebuildCutWaveform(int token) {
    if (!cutLoaded_ || !timeline_) return;
    if (!cutInfo_.hasAudio) return;

    const double from = timeline_->viewFrom();
    const double to = timeline_->viewTo();
    if (to - from <= 1e-6) return;
    const int waveToken = ++cutPeaksToken_;

    auto guard = QPointer<MainWindow>(this);
    const QString path = cutSource_;
    (void)QtConcurrent::run([this, guard, path, token, waveToken, from, to]() {
        auto peaks = std::make_shared<lr::AudioPeaks>();
        std::string error;
        const bool ok = lr::AudioPeakBuilder::buildRange(
            FfmpegApi::instance(), path.toStdString(), from, to, 600, *peaks, error);
        QMetaObject::invokeMethod(this, [this, guard, token, waveToken, peaks, ok, error]() {
            if (!guard || token != cutToken_ || waveToken != cutPeaksToken_ || !timeline_) return;
            if (!ok || peaks->empty()) {
                appendLog(QStringLiteral("波形生成失败：") + QString::fromStdString(error),
                          lr::logcolor::kMuted);
                return;
            }
            cutPeaks_ = peaks;
            timeline_->setPeaks(peaks);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::onCutCursorChanged(double sec) {
    // 播放中用户在影片条上按下了：先停（中间会有个短暂停顿，用户已接受），
    // 松手时再从新的当前帧立刻重新播 —— 比"播放中 seek"直观得多，
    // 也避开了"播放线程每帧把标尺改回去、看着像点了没反应"的问题。
    // playUpdatingCursor_ 表示这是播放自己推进的，不是用户操作。
    if (playRunning_ && !playUpdatingCursor_) {
        playInterruptedByUser_ = true;
        stopPlayback();
        return;
    }
    schedulePreview();
}

void MainWindow::onCutCursorSettled(double sec) {
    // 刚才是因为用户点/拖影片条才停的：立刻从新的当前帧接着播
    if (playInterruptedByUser_) {
        playInterruptedByUser_ = false;
        startPlayback();
        return;
    }
    schedulePreview();
}

void MainWindow::schedulePreview() {
    if (previewThrottle_) previewThrottle_->start();
}

void MainWindow::refreshCutPreview() {
    if (!cutLoaded_ || !timeline_) return;
    // 播放中画面由播放线程逐帧送过来，这里再取一次只会抢解码器、白耗 CPU
    if (playRunning_) return;
    const double sec = timeline_->cursorSec();
    const int token = cutToken_;

    auto guard = QPointer<MainWindow>(this);
    (void)QtConcurrent::run([this, guard, sec, token]() {
        QImage image;
        {
            std::lock_guard<std::mutex> lock(cutGrabberMutex_);
            if (!cutGrabber_.isOpen()) return;
            lr::FrameGrabber::Frame frame;
            std::string error;
            if (cutGrabber_.grabAt(sec, 0, 0, frame, error)) {
                image = toQImage(frame.rgb, frame.width, frame.height);
            }
        }
        QMetaObject::invokeMethod(this, [this, guard, token, image]() {
            if (!guard || token != cutToken_) return;
            setPreviewImage(image);
        }, Qt::QueuedConnection);
    });
}

// ---------------- Tab1 预览播放 ----------------

void MainWindow::togglePlayback() {
    if (playRunning_) {
        stopPlayback();
    } else {
        startPlayback();
    }
}

void MainWindow::startPlayback() {
    if (!cutLoaded_ || !timeline_ || playRunning_) return;
    if (!FfmpegApi::instance().isLoaded()) return;

    // 播放引擎自己开一份文件句柄：它要顺序往下读，不能跟"拖标尺取单帧"
    // 的 cutGrabber_ 共用一个 AVFormatContext（会互相偷对方的包）。
    if (!player_.isOpen()) {
        std::string error;
        // 传 0×0 = 保持原始分辨率，画面由预览窗按窗口尺寸缩放显示
        if (!player_.open(FfmpegApi::instance(), cutSource_.toStdString(),
                          cutInfo_.fps, 0, 0, error)) {
            appendLog(QStringLiteral("无法播放：") + QString::fromStdString(error),
                      lr::logcolor::kWarn);
            return;
        }
    }

    // 从当前帧（蓝色标尺位置）开始播；已经在片尾了就从头播，
    // 否则一点播放就立刻结束，看着像"按钮坏了"
    const double to = cutInfo_.durationSec;
    if (timeline_->cursorSec() >= to - 0.05) timeline_->setCursorSec(0.0);
    const double from = timeline_->cursorSec();

    ++playEpoch_;   // 这次播放的编号：旧播放线程迟到的"播完了"会被丢掉
    playRunning_ = true;
    player_.play(from, to);
    updatePlayButton();
    appendLog(QStringLiteral("播放 %1 → %2%3")
                  .arg(formatTimecode(from), formatTimecode(to),
                       player_.hasAudioTrack() ? QStringLiteral("（含声音）")
                                               : QStringLiteral("（无音轨，只有画面）")),
              lr::logcolor::kStep);
}

void MainWindow::stopPlayback() {
    if (!playRunning_) return;
    playRunning_ = false;
    player_.stop();
    updatePlayButton();
    // "用户在影片条上点了别处"导致的这次停止是暂时的，马上就要从新位置重播，
    // 这里再补波形/缩略图/单帧只是白占 CPU，还会让重播卡一下 —— 跳过。
    if (playInterruptedByUser_) return;
    // 播放中波形/缩略图是"跳着"刷新的，停下后按最终视窗补一次精确的
    scheduleTimelineAssets();
    // 播放送出的是原始分辨率的帧，停在这里再取一次当前帧，画面回到最清楚的状态
    schedulePreview();
}

void MainWindow::onPlaybackEnded() {
    // 播到片尾自动结束（也可能是被 stopPlayback 叫停，此时 playRunning_ 已为 false）
    if (!playRunning_) return;
    playRunning_ = false;
    updatePlayButton();
}

void MainWindow::updatePlayButton() {
    if (playRunning_) {
        T1play->setText(QStringLiteral("■ 停止"));
        T1play->setStyleSheet(QStringLiteral(
            "QPushButton{background:#D32F2F;color:#FFFFFF;border:1px solid "
            "#B71C1C;border-radius:3px;padding:4px 8px;}"
            "QPushButton:hover{background:#E53935;}"
            "QPushButton:pressed{background:#B71C1C;}"));
    } else {
        T1play->setText(QStringLiteral("▶ 播放"));
        T1play->setStyleSheet(playStyle_);
    }
    T1play->setEnabled(engineReady_ && cutLoaded_);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Space && !key->isAutoRepeat() &&
            tabWidget->currentIndex() == 0) {
            QWidget *focus = QApplication::focusWidget();
            // 焦点在别的窗口（比如弹出来的 QMessageBox）里就别管，
            // 让那个窗口自己处理空格
            if (focus != nullptr && focus != this && !isAncestorOf(focus)) {
                return QObject::eventFilter(watched, event);
            }
            // 输入框里的空格是打字，必须放行；
            // 除此之外空格一律归"播放/停止"，并且吞掉事件 —— 否则焦点
            // 恰好停在某个按钮上时，空格会被那个按钮当成"按下我"。
            const bool typing = qobject_cast<QLineEdit *>(focus) ||
                                qobject_cast<QTextEdit *>(focus) ||
                                qobject_cast<QComboBox *>(focus);
            if (!typing) {
                togglePlayback();
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}

void MainWindow::onCutRangeChanged(double, double) { syncCutTimeEdits(); }

void MainWindow::onCutRangeSettled(double, double) { applySnappedRange(); }

void MainWindow::applySnappedRange() {
    if (!timeline_) return;
    const double rawStart = timeline_->startSec();
    const double rawEnd = timeline_->endSec();
    const double start = snapStartSec(rawStart);
    const double end = snapEndSec(rawEnd);

    if (std::abs(start - rawStart) > 1e-6) {
        appendLog(QStringLiteral("起点 %1 → 吸附到右侧最近关键帧 %2")
                      .arg(formatTimecode(rawStart), formatTimecode(start)),
                  lr::logcolor::kMuted);
    }
    if (std::abs(end - rawEnd) > 1e-6) {
        appendLog(QStringLiteral("终点 %1 → 吸附到左侧最近关键帧 %2")
                      .arg(formatTimecode(rawEnd), formatTimecode(end)),
                  lr::logcolor::kMuted);
    }
    if (end <= start) {
        appendLog(QStringLiteral("吸附后起点不早于终点（%1 ~ %2），请重新选择区间。")
                      .arg(formatTimecode(start), formatTimecode(end)),
                  lr::logcolor::kWarn);
    }
    timeline_->setStartSec(start);
    timeline_->setEndSec(end);
    syncCutTimeEdits();
}

void MainWindow::onCutStartEdited() {
    if (!cutLoaded_ || !timeline_) return;
    const auto parsed =
        lr::timeutil::parseTimestamp(T1time1->text().trimmed().toStdString());
    if (!parsed) {
        appendLog(QStringLiteral("时间格式错误，请写成 00:00:00 或 00:00:02.500 或 90（秒）。"),
                  lr::logcolor::kWarn);
        syncCutTimeEdits();
        return;
    }
    const double t = snapStartSec(std::clamp(*parsed, 0.0, timeline_->durationSec()));
    if (t >= timeline_->endSec()) {
        appendLog(QStringLiteral("起点不能晚于终点。"), lr::logcolor::kWarn);
        syncCutTimeEdits();
        return;
    }
    timeline_->setStartSec(t);
    syncCutTimeEdits();
}

void MainWindow::onCutEndEdited() {
    if (!cutLoaded_ || !timeline_) return;
    const auto parsed =
        lr::timeutil::parseTimestamp(T1time2->text().trimmed().toStdString());
    if (!parsed) {
        appendLog(QStringLiteral("时间格式错误，请写成 00:00:00 或 00:00:02.500 或 90（秒）。"),
                  lr::logcolor::kWarn);
        syncCutTimeEdits();
        return;
    }
    const double t = snapEndSec(std::clamp(*parsed, 0.0, timeline_->durationSec()));
    if (t <= timeline_->startSec()) {
        appendLog(QStringLiteral("终点不能早于起点。"), lr::logcolor::kWarn);
        syncCutTimeEdits();
        return;
    }
    timeline_->setEndSec(t);
    syncCutTimeEdits();
}

void MainWindow::setCutStart() {
    if (!cutLoaded_ || !timeline_) return;
    const double cursor = timeline_->cursorSec();
    if (cursor >= timeline_->endSec()) {
        appendLog(QStringLiteral("当前帧已晚于终点，不能设为起点。"), lr::logcolor::kWarn);
        return;
    }
    const double snapped = snapStartSec(cursor);
    timeline_->setStartSec(snapped);
    appendLog(QStringLiteral("起点设为 %1").arg(formatTimecode(snapped)),
              lr::logcolor::kStep);
    syncCutTimeEdits();
}

void MainWindow::setCutEnd() {
    if (!cutLoaded_ || !timeline_) return;
    const double cursor = timeline_->cursorSec();
    if (cursor <= timeline_->startSec()) {
        appendLog(QStringLiteral("当前帧已早于起点，不能设为终点。"), lr::logcolor::kWarn);
        return;
    }
    const double snapped = snapEndSec(cursor);
    timeline_->setEndSec(snapped);
    appendLog(QStringLiteral("终点设为 %1").arg(formatTimecode(snapped)),
              lr::logcolor::kStep);
    syncCutTimeEdits();
}

void MainWindow::onCutViewChanged(double from, double to) {
    // 波形是整轨全量算的，缩放/平移**不再重算**，只要告诉后台"现在看的是哪一段"，
    // 让它把还没派发的段按优先级重排即可（可见区优先，边算边画）。
    waveBuilder_.setPriorityRange(from, to);
    // 缩略图还是得重取（它依赖可见区间）。播放时视窗每帧都在翻页，
    // 若还按 150ms 重取会跟解码抢 CPU —— 播放中放宽到 400ms，
    // 停播后再补一次精确的（见 stopPlayback）。
    if (assetThrottle_) assetThrottle_->setInterval(playRunning_ ? 400 : 150);
    scheduleTimelineAssets();
}

void MainWindow::scheduleTimelineAssets() {
    if (assetThrottle_) assetThrottle_->start();
}

void MainWindow::refreshCutTimelineAssets() {
    if (!cutLoaded_ || !timeline_) return;
    const double from = timeline_->viewFrom();
    const double to = timeline_->viewTo();
    const double span = std::max(1e-3, to - from);
    const int token = cutToken_;

    // 缩略图：3 张，取可见区间的左 / 中 / 右
    auto guard = QPointer<MainWindow>(this);
    (void)QtConcurrent::run([this, guard, token, from, span]() {
        std::array<QImage, lr::ui::TimelineWidget::kThumbCount> thumbs;
        {
            std::lock_guard<std::mutex> lock(cutGrabberMutex_);
            if (cutGrabber_.isOpen()) {
                for (int i = 0; i < int(thumbs.size()); ++i) {
                    const double t = from + span * kThumbFrac[i];
                    lr::FrameGrabber::Frame frame;
                    std::string error;
                    if (cutGrabber_.grabAt(t, 320, 180, frame, error)) {
                        thumbs[i] = toQImage(frame.rgb, frame.width, frame.height);
                    }
                }
            }
        }
        QMetaObject::invokeMethod(this, [this, guard, token, thumbs]() {
            if (!guard || token != cutToken_ || !timeline_) return;
            timeline_->setThumbnails(thumbs);
        }, Qt::QueuedConnection);
    });
    // 波形不在这里重算了：它是整轨全量算的，可见区间变了只要重排优先级
    // （见 onCutViewChanged 里的 setPriorityRange）。
}

void MainWindow::cutFile() {
    // 按钮是「停止编码」时，点它等于停止
    if (busy_) {
        stopTasks();
        return;
    }

    const QString source = T1file->text().trimmed();
    if (!checkSource(source)) return;
    if (!cutLoaded_ || !timeline_) {
        QMessageBox::warning(this, QStringLiteral("尚未载入视频"),
                             QStringLiteral("请等视频导入完成（时间轴出现缩略图与波形）"
                                            "后再开始截取。"));
        return;
    }

    const double start = timeline_->startSec();
    const double end = timeline_->endSec();
    if (end <= start + 0.001) {
        QMessageBox::warning(this, QStringLiteral("时间范围错误"),
                             QStringLiteral("结束时间必须晚于开始时间（当前 %1 ~ %2）。")
                                 .arg(formatTimecode(start), formatTimecode(end)));
        return;
    }

    const std::string src = source.toStdString();
    lr::RemuxSpec spec;
    spec.kind = lr::RemuxSpec::Kind::CopyAll;
    spec.title = QStringLiteral("截取 %1").arg(QFileInfo(source).fileName())
                     .toStdString();
    spec.inputs = {src};
    spec.output = lr::pathutil::outputPath(src, lr::pathutil::copySafeSuffix(src));
    spec.startSec = start;
    spec.endSec = end;
    spec.muxerOptions = muxerOptionsFor(QString::fromStdString(spec.output));

    appendLog(QStringLiteral("开始截取 %1 ~ %2（已按关键帧吸附）")
                  .arg(formatTimecode(start), formatTimecode(end)),
              lr::logcolor::kStep);
    runTasks({std::make_shared<lr::RemuxTask>(std::move(spec))});
}

// ---------------- Tab 2：连接 ----------------

void MainWindow::addToList() {
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("添加视频（可多选）"), QString(),
        QString::fromStdString(lr::pathutil::videoFileFilter()));
    for (const QString &path : paths) T2listbox->addItem(path);
}

void MainWindow::removeFromList() {
    QList<int> rows;
    for (const QModelIndex &index : T2listbox->selectionModel()->selectedIndexes()) {
        if (!rows.contains(index.row())) rows.append(index.row());
    }
    if (rows.isEmpty() && T2listbox->currentRow() >= 0) {
        rows.append(T2listbox->currentRow());
    }
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows) delete T2listbox->takeItem(row);
}

void MainWindow::joinFiles() {
    QStringList files;
    for (int i = 0; i < T2listbox->count(); ++i) {
        files << T2listbox->item(i)->text();
    }
    if (files.size() < 2) {
        QMessageBox::warning(this, QStringLiteral("文件不足"),
                             QStringLiteral("请至少添加 2 个视频再连接。"));
        return;
    }
    QStringList missing;
    for (const QString &file : files) {
        if (!QFileInfo::exists(file)) missing << file;
    }
    if (!missing.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("文件不存在"),
                             QStringLiteral("以下文件不存在：\n%1")
                                 .arg(missing.mid(0, 5).join(QStringLiteral("\n"))));
        return;
    }

    lr::RemuxSpec spec;
    spec.kind = lr::RemuxSpec::Kind::Concat;
    spec.title = QStringLiteral("连接 %1 个视频").arg(files.size()).toStdString();
    for (const QString &file : files) spec.inputs.push_back(file.toStdString());
    const std::string first = files.first().toStdString();
    spec.output = lr::pathutil::outputPath(
        first, lr::pathutil::copySafeSuffix(first));
    spec.muxerOptions = muxerOptionsFor(QString::fromStdString(spec.output));

    runTasks({std::make_shared<lr::RemuxTask>(std::move(spec))});
}

// ---------------- Tab 3：分离 ----------------

void MainWindow::splitFile() {
    const QString source = T3file->text().trimmed();
    if (!checkSource(source)) return;

    const std::string src = source.toStdString();
    const std::string suffix = lr::pathutil::copySafeSuffix(src);
    const std::string stamp = lr::pathutil::stamp();
    const QString name = QFileInfo(source).fileName();

    lr::RemuxSpec videoSpec;
    videoSpec.kind = lr::RemuxSpec::Kind::VideoOnly;
    videoSpec.title =
        QStringLiteral("抽取视频轨 %1").arg(name).toStdString();
    videoSpec.inputs = {src};
    videoSpec.output =
        lr::pathutil::outputPath(src, suffix, "-video", stamp);

    lr::RemuxSpec audioSpec;
    audioSpec.kind = lr::RemuxSpec::Kind::AudioOnly;
    audioSpec.title =
        QStringLiteral("抽取音频轨 %1").arg(name).toStdString();
    audioSpec.inputs = {src};
    audioSpec.output =
        lr::pathutil::outputPath(src, suffix, "-audio", stamp);

    runTasks({std::make_shared<lr::RemuxTask>(std::move(videoSpec)),
              std::make_shared<lr::RemuxTask>(std::move(audioSpec))});
}

// ---------------- Tab 4：合并 ----------------

void MainWindow::mergeFile() {
    const QString video = T4file->text().trimmed();
    const QString audio = T4audio->text().trimmed();
    if (!checkSource(video) || !checkSource(audio)) return;

    const std::string src = video.toStdString();
    lr::RemuxSpec spec;
    spec.kind = lr::RemuxSpec::Kind::Merge;
    spec.title = QStringLiteral("合并 %1").arg(QFileInfo(video).fileName())
                     .toStdString();
    spec.inputs = {src, audio.toStdString()};
    spec.shortest = true;  // 原命令里的 -shortest
    spec.output = lr::pathutil::outputPath(src, lr::pathutil::copySafeSuffix(src));
    spec.muxerOptions = muxerOptionsFor(QString::fromStdString(spec.output));

    runTasks({std::make_shared<lr::RemuxTask>(std::move(spec))});
}

// ---------------- Tab 5 / 6：压缩（重新编码） ----------------

void MainWindow::browseDir() {
    const QString directory =
        QFileDialog::getExistingDirectory(this, QStringLiteral("选取文件夹"));
    if (directory.isEmpty()) return;
    T5file->setText(directory);

    batchInputs_.clear();
    const auto &exts = lr::pathutil::videoExtensions();
    const QFileInfoList entries = QDir(directory).entryInfoList(QDir::Files);
    for (const QFileInfo &entry : entries) {
        const QString ext = entry.suffix().toLower();
        for (const std::string &e : exts) {
            if (ext == QString::fromStdString(e)) {
                batchInputs_.push_back(entry.absoluteFilePath());
                break;
            }
        }
    }
    std::sort(batchInputs_.begin(), batchInputs_.end());
    T5info->setText(QStringLiteral("此文件夹中共有 %1 个视频文件，"
                                   "批量转码耗时较长，请利用空闲时间进行转码！")
                        .arg(batchInputs_.size()));
}

void MainWindow::batchCode() {
    if (batchInputs_.empty()) {
        QMessageBox::information(this, QStringLiteral("批量压缩"),
                                 QStringLiteral("请先点击「浏览」选择一个文件夹。"));
        return;
    }

    // T5bit 里的数字单位是 Mbps（界面上写的是 1/2/4/8/12/16/20/30）
    const int videoKbps = T5bit->currentText().toInt() * 1000;
    const std::string scale = T5size->currentText().toStdString();
    const std::string encoder = lr::pathutil::lookup(
        lr::pathutil::hwEncoders(), T5hwacc->currentText().toStdString(),
        "libx264");

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, QStringLiteral("批量压缩"),
        QStringLiteral("即将对 %1 个视频重新编码，耗时较长。\n"
                       "编码器：%2 ｜ 码率：%3 Mbps ｜ 分辨率：%4\n\n"
                       "确定开始吗？")
            .arg(batchInputs_.size())
            .arg(QString::fromStdString(encoder))
            .arg(videoKbps / 1000)
            .arg(T5size->currentText()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    std::vector<std::shared_ptr<lr::ITask>> jobs;
    jobs.reserve(batchInputs_.size());
    for (const QString &src : batchInputs_) {
        lr::TranscodeSpec spec;
        spec.title = lr::pathutil::stemOf(src.toStdString());
        spec.input = src.toStdString();
        // 输出沿用原目录，文件名加 _new_<时间戳> 后缀，不会覆盖原文件
        spec.output = lr::pathutil::outputPath(src.toStdString(), "mp4");
        spec.videoEncoder = encoder;
        spec.videoKbps = videoKbps;
        spec.audioKbps = 128;
        spec.scale = scale;
        spec.muxerOptions = muxerOptionsFor(QString::fromStdString(spec.output));
        jobs.push_back(std::make_shared<lr::TranscodeTask>(std::move(spec)));
    }
    runTasks(std::move(jobs));
}

void MainWindow::customCode() {
    const QString src = T6file->text().trimmed();
    if (src.isEmpty() || !QFileInfo::exists(src)) {
        QMessageBox::information(this, QStringLiteral("自定义编码"),
                                 QStringLiteral("请先选择一个视频文件。"));
        return;
    }

    lr::TranscodeSpec spec;
    spec.title = lr::pathutil::stemOf(src.toStdString());
    spec.input = src.toStdString();
    spec.output = lr::pathutil::outputPath(src.toStdString(), "mp4");
    // Tab6 的控件比 Tab5 更全：编码器/预设/level/分辨率/码率/音频码率
    spec.videoEncoder = lr::pathutil::lookup(
        lr::pathutil::hwEncoders(), T6quality->currentText().toStdString(),
        "libx264");
    spec.preset = T6speed->currentText().toStdString();
    spec.level = T6grade->currentText().toStdString();
    spec.scale = T6type->currentText().toStdString();
    spec.videoKbps = T6level->currentText().toInt() * 1000;
    spec.audioKbps = T6audio->currentText().toInt();
    spec.muxerOptions = muxerOptionsFor(QString::fromStdString(spec.output));

    runTasks({std::make_shared<lr::TranscodeTask>(std::move(spec))});
}

// ---------------- 版本检查 ----------------

void MainWindow::checkVersion() {
    if (!net_) net_ = new QNetworkAccessManager(this);
    QNetworkRequest request(QUrl(QString::fromUtf8(kVersionUrl)));
    QNetworkReply *reply = net_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        QString text = QStringLiteral("无法获取最新版本信息");
        if (reply->error() == QNetworkReply::NoError) {
            const QString body = QString::fromUtf8(reply->readAll());
            const QRegularExpression re(
                QStringLiteral("version:\\s*([0-9]+(?:\\.[0-9]+)*)"));
            const auto match = re.match(body);
            if (match.hasMatch()) {
                const QString latest = match.captured(1);
                text = QStringLiteral("最新版本：%1").arg(latest);
                if (versionGreater(parseVersion(latest),
                                   parseVersion(QString::fromUtf8(lr::kAppVersion)))) {
                    text += QStringLiteral("（有更新）");
                }
            }
        }
        T7new->setText(text);
    });
}

// ---------------- 退出 ----------------

void MainWindow::closeEvent(QCloseEvent *event) {
    if (busy_) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, QStringLiteral("退出"),
            QStringLiteral("任务正在执行，确定要退出吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        stopAll_.store(true);
        for (auto &job : jobs_) job->requestStop();
    }
    // 播放线程是 detach 之外的常驻线程，关窗前必须收掉，否则会拖着文件句柄退出；
    // 整轨波形的 4 个 worker 同理（它们各开着一份文件）
    stopPlayback();
    player_.close();
    waveBuilder_.stop();
    event->accept();
}
