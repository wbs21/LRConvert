// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// 主窗口 —— 对应 Python 版 LRConvert.py 的 MainWindow。
//
// 界面完全来自 src/ui/mainui.ui（Qt Designer 产物，uic 生成 Ui::Form），
// 控件名与 Python 版一一对应，因此业务逻辑可以逐条照搬。
//
// 启动顺序上的硬要求（见重构方案 §6）：
//   show() 出首帧 → QTimer::singleShot(0) → 工作线程加载 FFmpeg 动态库。
// ---------------------------------------------------------------------------

#include <QImage>
#include <QWidget>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

#include "core/ITask.h"
#include "core/PathUtil.h"
#include "platform/CrashDump.h"
#include "ffmpeg/AudioPeaks.h"
#include "ffmpeg/FrameGrabber.h"
#include "ffmpeg/MediaProbe.h"
#include "ffmpeg/WaveformBuilder.h"
#include "player/FramePlayer.h"
#include "ui/TimelineWidget.h"
#include "ui_mainui.h"

class QCloseEvent;
class QComboBox;
class QKeyEvent;
class QLineEdit;
class QNetworkAccessManager;
class QProgressBar;
class QResizeEvent;
class QTimer;

class MainWindow : public QWidget, private Ui::Form {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    /// 崩溃后安全模式（P1-8）：上一次运行留下了 dump 且没走到正常退出。
    /// 置位后跳过那些"最可能再次把它弄崩"的可选动作（硬件编码器探测），
    /// 让这一跑至少能正常起来、把用户的活干完。
    void setSafeMode(bool on) { safeMode_ = on; }
    bool safeMode() const { return safeMode_; }

    /// 冒烟测试用：编码器下拉框当前的可选项（Tab5 与 Tab6 同源，取 Tab5）
    QString smokeEncoderItems() const;
    /// 冒烟用：切换界面语言并刷新全部动态文本（LRCONVERT_LANG_TEST 驱动，
    /// 验证"装卸 translator + retranslate + QSettings 记忆"整条链路）
    bool smokeSwitchLanguage(const QString &id);
    /// 冒烟测试用：把视频导入 Tab1（配合 LRCONVERT_CUT_FILE）
    void smokeLoadCutFile(const QString &path);
    /// 冒烟测试用：Tab1 时间轴当前状态的一行摘要
    QString smokeCutState() const;
    /// 冒烟测试用：模拟"拖到 start/end 后松手"，用于验证关键帧吸附方向
    void smokeApplyRange(double start, double end);
    /// 冒烟测试用：预览窗尺寸 + 当前画面尺寸（验证"跟着界面放大"）
    QString smokePreviewInfo() const;
    /// 冒烟用：按一下「播放 ⇄ 停止」
    void smokeTogglePlay();
    /// 冒烟用：模拟按下空格键（走 eventFilter，验证快捷键链路）
    void smokePressSpace();
    /// 冒烟用：直接把播放头放到指定位置
    void smokeSetCursor(double sec);
    /// 冒烟用：设定可见区间
    void smokeApplyView(double from, double to);
    /// 冒烟用：播放中在影片条上点一下（验证"停 → 定位 → 立刻重播"）
    void smokeClickCursor(double sec);
    QString smokePlayState() const;
    /// 冒烟用：「提示」标签页（Tab7）正文的前两行，核对文案有没有被改回旧版
    QString smokeTipText() const;
    /// 冒烟用：Tab7 的「当前版本」一行（retranslate 会重设它，用于核对语言切换）
    QString smokeVersionLine() const;
    /// 冒烟用：最大化 / 还原（BUG-001 复现用，无需人工点标题栏）
    void smokeSetMaximized(bool on);
    /// 冒烟用：人为把窗口顶出屏幕，验证 keepWindowOnScreen() 能把它拉回来
    void smokeNudgeOffScreen();
    /// 冒烟用：窗口几何体检（定位 BUG-001 用，看还原后 y 是不是负的）
    QString smokeWindowGeom() const;
    /// 冒烟测试用：设定可见区间，再把深蓝滑块拖到 moveFrac（控件宽度比例）处，
    /// 用来验证"拖动=整体平移、窗口长度不变"
    void smokePanSlider(double from, double to, double moveFrac);
    /// 冒烟用：最近一次播放的损坏统计 —— 验证"坏块被跳过且播放没中断"，
    /// 以及导入取帧时发现的坏包数
    QString smokeDamageState() const;

signals:
    /// 由工作线程发出（Qt 会自动排队投递回 GUI 线程）
    void ffmpegLoaded(bool ok, QString detail);
    /// 硬件编码器识别完成：availableCsv 是可用的编码器名（空格分隔），
    /// hiddenReport 是被隐藏的编码器及原因（多行文本，可为空）
    void hwEncodersProbed(const QString &availableCsv, const QString &hiddenReport);

public slots:
    /// UI 首帧显示后调用；幂等
    void startAsyncInit();

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    /// 从最大化/全屏还原回来之后，把窗口拉回屏幕可见区（BUG-001）
    void changeEvent(QEvent *event) override;
    /// 空格 = 播放/停止。装在 qApp 上而不是只重写 keyPressEvent：
    /// 点过「开始剪辑」后焦点会留在那个按钮上，光靠 keyPressEvent 的话
    /// 空格会被按钮当成"按下我"，一按空格就开始编码了。
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    // ---- Tab1 时间轴 ----
    void loadCutSource();                       ///< 载入文件（快）：探测 + 缩略图 + 首帧预览
    void onCutCursorChanged(double sec);         ///< 蓝标尺移动中（节流刷新预览）
    void onCutCursorSettled(double sec);         ///< 蓝标尺松手（精确取帧）
    void onCutRangeChanged(double start, double end);
    void onCutRangeSettled(double start, double end);  ///< 松手后按关键帧吸附
    void onCutViewChanged(double from, double to);     ///< 缩放/平移后重取缩略图与波形
    void onCutStartEdited();                    ///< 时间码输入框手动改时间
    void onCutEndEdited();
    // ---- Tab1 预览播放 ----
    void togglePlayback();                      ///< 「播放 ⇄ 停止」同一个按钮，空格键同效
    void onPlaybackEnded();                     ///< 播到片尾（或被 stop）后收尾
    void setCutStart();                         ///< 「设为起点」= 取蓝标尺位置
    void setCutEnd();                           ///< 「设为终点」
    void refreshCutPreview();                   ///< 节流后的实际取帧
    void refreshCutTimelineAssets();            ///< 节流后的缩略图/波形刷新

    void onFfmpegLoaded(bool ok, const QString &detail);
    /// 按探测结果重建 Tab5/Tab6 的编码器下拉框，隐藏本机不支持的硬件编码器
    void applyHwEncoders(const QString &availableCsv, const QString &hiddenReport);

    // 任务反馈（跨线程）
    void appendLog(const QString &text,
                   const QString &color = QStringLiteral("#333333"));
    void setProgress(int percent, const QString &detail);
    void onBatchFinished(bool ok);

    // 通用
    void stopTasks();
    void checkVersion();

    // ---- 语言 ----
    /// 切换语言并刷新所有运行期设置的文本（.ui 静态文本在下次构造时才变，
    /// 语言切换不重建窗口，所以动态文本全靠这里重设）。
    /// 返回 false 时 error 给出原因（界面保持原语言不变）。
    bool switchLanguage(const QString &id, QString *error = nullptr);
    /// 只刷新动态文本（不动 translator）：switchLanguage 内部用，
    /// 也是将来"系统语言变化"之类场景的入口
    void retranslate();
    /// 分辨率规范键 -> 本地化显示名（"1920*1080" 等数字项原样返回）
    static QString scaleDisplayName(const QString &key);
    /// 硬件编码器名 -> 本机要求说明（探测失败时展示）；无关编码器返回空串
    static QString hwRequirementText(const QString &encoderName);
    /// 填充分辨率下拉框：显示名本地化，规范键存 userData（见 fillScaleBox）
    void fillScaleBox(QComboBox *box, const lr::pathutil::NameValues &values);
    void updateAboutText();    ///< Tab7 正文（T7about）
    void updateBatchInfoText(); ///< Tab5 的文件数量提示（T5info）
    void updateVersionLabel();  ///< Tab7 最新版本一行（T7new）

    // Tab 1 截取
    void cutFile();
    // Tab 2 连接
    void addToList();
    void removeFromList();
    void joinFiles();
    // Tab 3 分离
    void splitFile();
    // Tab 4 合并
    void mergeFile();
    // Tab 5 批量压缩 / Tab 6 自定义压缩（M5 实现）
    void browseDir();
    void batchCode();
    void customCode();

private:
    void initWidgets();
    void connectActions();
    /// 把窗口按回屏幕可见区（标题栏必须露出来）；还原态专用，正常时不动手
    void keepWindowOnScreen();
    void setBusy(bool busy);
    void runTasks(std::vector<std::shared_ptr<lr::ITask>> jobs);
    bool checkSource(const QString &path);
    void pickVideoFile(QLineEdit *target);
    void pickAudioFile();

    // ---- Tab1 时间轴辅助 ----
    void setupTimeline();
    void schedulePreview();
    void scheduleTimelineAssets();
    void applySnappedRange();
    /// 导入画面出来之后，后台补做：关键帧表 + 整轨波形（4 线程分段全量）
    void startCutBackgroundAssets(int token);
    /// 整轨全量波形的启动入口（分配共享数据 + 起 WaveformBuilder）
    void startFullWaveform(int token);
    /// 全量任务失败时的兜底：退回"只算可见区间"（旧方案）
    void rebuildCutWaveform(int token);
    /// 起点吸附"选取点右侧"最近关键帧
    double snapStartSec(double t) const;
    /// 终点吸附"选取点左侧"最近关键帧
    double snapEndSec(double t) const;
    void syncCutTimeEdits();
    void updateCutInfoBar();
    void setPreviewImage(const QImage &image);
    /// 按预览窗当前尺寸重新缩放已缓存的那一帧
    void scalePreviewToLabel();
    static QImage toQImage(const std::vector<uint8_t> &rgb, int width, int height);
    QString formatTimecode(double sec) const;  ///< 00:00:02.500

    // ---- Tab1 预览播放辅助 ----
    void startPlayback();
    void stopPlayback();
    void updatePlayButton();

    QProgressBar *progressBar_ = nullptr;
    QNetworkAccessManager *net_ = nullptr;
    /// GUI 心跳定时器（P0-1 看门狗）：事件循环转得动它就一直在打点
    QTimer *heartbeatTimer_ = nullptr;
    /// 崩溃后安全模式（P1-8）：上一跑没走到正常退出就结束时由 main() 置位，
    /// 关掉那些"最可能再次把它弄崩"的可选动作（硬件编码器探测）。
    bool safeMode_ = false;
    /// 版本检查结果（版本号；空 = 没查到/查询失败）。文本本地化在
    /// updateVersionLabel() 里做，切换语言不必重新请求网络
    QString latestVersion_;

    lr::TaskSignals signals_;
    std::vector<std::shared_ptr<lr::ITask>> jobs_;
    bool busy_ = false;
    bool engineReady_ = false;
    bool ffmpegLoadStarted_ = false;
    /// 工作线程读取、GUI 线程置位的停止标志
    std::atomic<bool> stopAll_{false};
    std::vector<QString> batchInputs_;   ///< Tab5 扫描到的视频文件

    // ---- Tab1 时间轴状态 ----
    lr::ui::TimelineWidget *timeline_ = nullptr;
    QTimer *previewThrottle_ = nullptr;  ///< 拖动蓝标尺时的取帧节流
    QTimer *assetThrottle_ = nullptr;    ///< 缩放/平移后的缩略图与波形刷新节流
    lr::FrameGrabber cutGrabber_;        ///< 保持文件打开，连续取帧
    /// mutable：冒烟查询是 const 的，也要能加锁。
    /// timed_mutex（P2-10）：带上超时之后，取帧任务可以"抢不到就放弃这一拍"，
    /// 而不是排着队干等 —— 节流性质的任务丢一拍无所谓，界面等锁才要命。
    mutable std::timed_mutex cutGrabberMutex_;
    QString cutSource_;
    lr::MediaInfo cutInfo_;
    std::vector<double> cutKeyframes_;
    /// 整轨峰值（与后台 4 个线程共享的同一份，不是拷贝）
    std::shared_ptr<lr::AudioPeaks> cutPeaks_;
    lr::WaveformBuilder waveBuilder_;   ///< 整轨波形：分段 + 多线程 + 可见区优先
    QImage cutPreviewImage_;             ///< 当前帧原图（预览窗缩放时按新尺寸重画）
    QString cutStartStyle_;              ///< 「开始剪辑」常态样式（蓝色），忙碌时换成红色
    QString playStyle_;                  ///< 「▶ 播放」常态样式（绿色），播放中换成红色「■ 停止」
    lr::FramePlayer player_;             ///< Tab1 预览播放（自己开一份文件，不与取帧器抢）
    /// 最近一次播放的损坏统计（冒烟核对"坏块被跳过、播放没中断"用）
    lr::FramePlayer::PlayStats lastPlayStats_;
    bool playRunning_ = false;           ///< UI 侧的播放状态（线程退出有延迟，以这个为准）
    /// 播放世代：每次 startPlayback 都 +1。播放线程退出的"播完了"是排队投递的，
    /// 可能晚到一步（比如刚 stop 又立刻重播），世代号对不上就直接丢掉，
    /// 否则它会把新一次播放的 playRunning_ 误清成 false。
    int playEpoch_ = 0;
    /// 播放线程正在替用户改标尺/视窗（用于区分"播放推进"和"用户手动拖"）
    bool playUpdatingCursor_ = false;
    /// 播放是被"用户在影片条上点了别处"打断的：松手后要立刻从新位置重播
    bool playInterruptedByUser_ = false;
    /// 已投递给 GUI 线程、还没被执行的帧数（播放背压用，见帧回调处的说明）
    std::atomic<int> pendingFrames_{0};
    bool cutLoaded_ = false;
    bool cutLoading_ = false;
    int cutPeaksToken_ = 0;               ///< 只认最后一次发起的波形计算
    int cutToken_ = 0;                   ///< 防止旧任务的结果覆盖新文件的结果
};
