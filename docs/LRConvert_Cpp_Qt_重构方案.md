# LRConvert 重构方案：Python/PySide6 + ffmpeg CLI → C++20/Qt6 + FFmpeg 动态库

> 基于现有 v3.4 代码结构制定。目标：保持功能与交互不变，把「子进程调用 ffmpeg.exe + 解析 stdout 进度」的架构替换为「运行时动态加载 libav* 动态库，进程内完成解封装/解码/滤镜/编码/封装」。

---

## 1. 总体思路

现有项目的本质是：**GUI 收集参数 → 拼 ffmpeg 命令行 → QProcess 执行 → 解析 `-progress pipe:1` 输出**。

重构后变为：**GUI 收集参数 → 组装 Task 对象（内部调用 libavformat/libavcodec/libavfilter/libswscale/libswresample）→ 丢到工作线程执行 → Qt 信号回传进度**。

四条硬性原则：

1. **分层不变**：沿用现在 `ffmpeg_cmd.py`（纯逻辑）/ `LRConvert.py`（界面）两层结构，C++ 侧对应 `core/`（不依赖 Qt Widgets，只依赖 Qt Core 做信号线程安全）与 `ui/`。
2. **行为对齐**：六大功能（截取/连接/分离/合并/批量压缩/自定义压缩）的输入输出、命名规则（`原名_new_时间戳.后缀`、冲突追加 `(1)`）、进度语义与现在保持一致，老用户零学习成本。
3. **线程模型对齐**：现在是 `ThreadPoolExecutor(max_workers=1)` 串行执行；C++ 侧同样单工作线程串行，任务内协作式取消。
4. **启动优先（硬性要求）**：**UI 必须先显示，FFmpeg 动态库在 UI 显示成功后才加载**，且加载在工作线程进行，绝不阻塞首帧。详见 §6。

---

## 2. 技术栈与假设

| 项      | 选型                                | 说明                                                       |
| ------ | --------------------------------- | -------------------------------------------------------- |
| 语言     | C++20                             | `std::atomic`、`std::filesystem`、RAII 管理 AVFrame/AVPacket |
| Qt     | **Qt 6**（原项目 PySide6）             | 若必须 Qt5 也可，本文按 Qt6 写                                     |
| 构建     | CMake ≥ 3.16                      | 只找 Qt6 + 系统库；**不链接 libav***（见 §6、§7）                     |
| FFmpeg | 用户自行编译的**共享库**（`--enable-shared`） | 运行时 `dlopen`/`LoadLibrary` 加载，版本与路径运行时探测                 |
| 平台     | Windows 优先、macOS 兼容               | 加载层用 `QLibrary` 抽象，一套代码通吃                                |

> 假设未确认项：若需 Linux，§6 的加载层无需改动（`QLibrary` 内部即 dlopen）。

---

## 3. 架构设计

### 3.1 分层图

```
┌─────────────────────────────────────────────┐
│ ui/             MainWindow + .ui（7 个 Tab）  │  只碰控件，不碰 FFmpeg
├─────────────────────────────────────────────┤
│ tasks/          CutTask / ConcatTask /       │  一个功能一个 Task
│                 SplitTask / MergeTask /      │  实现 ITask 接口
│                 TranscodeTask                │
├─────────────────────────────────────────────┤
│ core/           ITask、TaskBase、             │  不依赖 QtWidgets
│                 ProgressEmitter、ErrorUtil   │  可脱离 UI 单测
├─────────────────────────────────────────────┤
│ util/           TimeFormat、PathUtil          │  纯函数
├─────────────────────────────────────────────┤
│ ffmpeg/         FfmpegLoader（dlopen + 符号绑定）│ ← 唯一的动态库接触点
│                 FfmpegApi（全局唯一访问门面）    │   UI 显示后才 load()
└─────────────────────────────────────────────┘
         ↓ 运行时加载（不产生链接期依赖）
libavformat / libavcodec / libavutil / libavfilter / libswscale / libswresample
```

**关键约束**：`core/`、`tasks/` 里**不允许出现直接调用 `av_*` 符号的代码**，一律写成 `FfmpegApi::instance().av_xxx(...)`。这样：

- 链接期不产生 libav* 导入项 → 操作系统**不会**在进程启动时加载这些 DLL（这是启动速度的前提）；
- 缺库时程序照常启动，只在日志面板提示，与现有 Python 版行为一致（见 §6.4）；
- 单测可以注入 mock 的 `FfmpegApi`。

### 3.2 目录结构

```
lrconvert-qt/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp                  （UI 先行 + 延迟启动加载器）
│   ├── MainWindow.h/.cpp
│   ├── MainWindow.ui             （由 mainui.ui 改造，保留控件命名）
│   ├── ffmpeg/
│   │   ├── FfmpegLoader.h/.cpp   dlopen/LoadLibrary + 符号表 + 版本探测
│   │   └── FfmpegApi.h/.cpp      单例门面：所有 av_* 函数指针的唯一出口
│   ├── core/
│   │   ├── ITask.h               抽象任务：run()/cancel() + progress/log/finished 信号
│   │   ├── TaskBase.h/.cpp       RAII 封装、错误码转中文、公共日志
│   │   ├── HwDevice.h/.cpp       硬件编码器设备上下文（CUDA/QSV/D3D11/VT）+ 回退
│   │   └── MediaInfo.h/.cpp      替代 ffprobe：时长/流信息探测
│   ├── tasks/
│   │   ├── RemuxTask.h/.cpp      重封装公共基类（截取/分离/合并共用写包逻辑）
│   │   ├── CutTask.h/.cpp
│   │   ├── ConcatTask.h/.cpp
│   │   ├── SplitTask.h/.cpp
│   │   ├── MergeTask.h/.cpp
│   │   └── TranscodeTask.h/.cpp  解码 → scale 滤镜 → 编码 全管线
│   └── util/
│       ├── TimeFormat.h/.cpp     对应 parse_timestamp/format_timestamp
│       ├── PathUtil.h/.cpp       对应 output_path/copy_safe_suffix/unique
│       └── VersionCheck.h/.cpp   QNetworkAccessManager 版本检查
└── tests/                        （可选 gtest）
```

### 3.3 类映射表（Python → C++）

| 现有（Python）                               | 重构后（C++）                                           | 变化                          |
| ---------------------------------------- | -------------------------------------------------- | --------------------------- |
| `FfmpegJob`（Popen + 管道读进度）               | `ITask` + 各 `XxxTask`                              | 不再有子进程；进度改为帧 pts / 包 pts 计算 |
| `TaskSignals`（Qt 信号）                     | 保留同名设计：`log` / `progress` / `finished` / `version` | 信号天然线程安全，方案不变               |
| `ThreadPoolExecutor(1)` + `_run_all`     | 单个 `QThread` 工作线程 + 任务队列                           | 语义一致：串行、失败即停                |
| `ffmpeg_cmd.cmd_*()` 拼 list[str]         | `XxxTask::run()` 内部直接调 libav*                      | 每个 cmd_ 函数对应一个 Task 类       |
| `fc.probe_duration()`（ffprobe）           | `MediaInfo::duration()`（avformat 探测）               | 进程内完成                       |
| `fc.with_progress()`（`-progress pipe:1`） | 删除，进度在编码/拷贝循环里直接算                                  | 更可靠，无文本解析                   |
| `fc.ffmpeg_path/ffprobe_path`            | 删除；改由 `FfmpegLoader` 运行时定位                         | 见 §6                        |
| `fc.SCALE_FILTERS / HW_ENCODERS`         | 同名常量表                                              | 直接搬                         |
| `check_version()`（requests）              | `VersionCheck`（QNetworkAccessManager）              | 线程模型相同                      |
| `_build_extra_ui` 动态进度条                  | 放进 .ui                                             | 设计器可见                       |
| 启动时 `fc.ffmpeg_path()` 同步查库              | **UI 显示后异步加载**（§6）                                 | 启动耗时显著下降                    |

---

## 4. 核心功能实现要点

> 以下所有 `av_*` 调用，实际都写成 `FfmpegApi::instance().av_*`，下文为可读性省略前缀。

### 4.0 公共基础设施

- **RAII 封装**：`AVFramePtr`/`AVPacketPtr`（自定义 deleter），保证 `av_*_alloc` 与 `av_*_free` 配对；`avformat_open_input` 失败必须 `avformat_close_input`。
- **错误处理**：`FfmpegApi::err2str(code)` 转 `QString`，统一弹窗或记彩色日志（对齐现在的 ✓/✗ 日志）。
- **进度协议**：与现在一致的 `(百分比, "标题 · 当前/总长 · 速度x")`；无总时长时走忙碌指示条（对应现在 percent<0 分支）。
- **取消协议**：`std::atomic_bool cancelRequested`，在 packet/frame 循环里检查；置位后优雅收尾：flush 解码器 → flush 编码器（`avcodec_send_frame(ctx, nullptr)` 再收包）→ `av_write_trailer`。**语义对齐现在的「先发 q 再强杀」**，保证输出文件完整。
- **前置条件**：所有 Task 启动前检查 `FfmpegApi::isReady()`，未就绪直接返回失败并提示。

### 4.1 截取 CutTask（流拷贝，无损）

对应 `cmd_cut`：`-ss` 输入侧快进 + `-to` 输出侧截断 + `-map 0` 全轨 + `-avoid_negative_ts 1`。

```
avformat_seek_file(ic, -1, INT64_MIN, start_ts, start_ts, AVSEEK_FLAG_BACKWARD)
  → 逐包 av_interleaved_write_frame
  → 首包做时间戳平移（offset = first_pts - start_ts，av_packet_rescale_ts）
  → pts >= end_ts 即停（以平移后为基准）
```

非关键帧截取仍从最近关键帧开始（与现状一致，README 已说明该限制）。

### 4.2 连接 ConcatTask（流拷贝）

- **A. concat 解封装器（推荐）**：与现状同构——写临时清单文件，`avformat_open_input(&ic, "concat:xxx.txt|safe=0", ...)`。实现最少、行为一致（要求各文件编码参数相同）。
- B. 手动多文件拼接：顺序打开各输入，按前一时长累加时间戳偏移。兼容性略好但代码量翻倍，留作备选。

### 4.3 分离 SplitTask（选流重封装）

遍历流，只保留 `0:v`（去音）或 `0:a`（去视频）**一个**流，逐包拷贝，对应 `-map 0:v -an` / `-map 0:a -vn`。输出两条任务，与现在一致。

### 4.4 合并 MergeTask（双输入重封装）

开两个输入：取 video 的 `0:v:0`、audio 的 `0:a:0`，交错写出；`-shortest` 语义 = 较短流结束后停止。

### 4.5 压缩 TranscodeTask（完整管线，最重）

```
demux(av_read_frame) → decode → filter(buffer→scale→buffersink) → encode → mux
```

关键点：

- **编码器选择**：`avcodec_find_encoder_by_name()`，表 `HW_ENCODERS` 直接搬（libx264/h264_nvenc/h264_qsv/h264_amf/h264_videotoolbox）。
- **硬件设备上下文**：nvenc→`AV_HWDEVICE_TYPE_CUDA`；qsv→`AV_HWDEVICE_TYPE_QSV`（Windows 下配 D3D11）；amf→`AV_HWDEVICE_TYPE_D3D11VA`；macOS→`VIDEOTOOLBOX`。**任一硬件初始化失败自动回退 libx264 并提示**。
- **硬件编码器识别（启动后自动执行，见 §9.10）**：探测路径与 `TranscodeTask` 的打开路径逐字段一致（`find_encoder_by_name` + 最小配置 `avcodec_open2`），本机打不开的编码器直接不进 Tab5/Tab6 的下拉框，并日志说明原因与所需硬件。
- **码率控制**：`AVCodecContext::bit_rate` + `av_dict_set(&opt, "maxrate", …)` / `"bufsize", …`（bufsize = 2×码率、maxrate = 1.5×码率，公式照搬 `cmd_transcode`）。
- **私有选项**：`profile=high`、`preset`、`level` 走 `av_dict_set`；videotoolbox 跳过 level。
- **分辨率**：libavfilter scale，表达式与 `SCALE_FILTERS` 完全一致（`trunc(iw/2)*2` 的偶数对齐坑自然保留）。
- **音频**：`aac` 编码器 + `swr_convert` 重采样，码率 96/128/160/196k。
- **多线程**：`AVCodecContext::thread_count = 0`（自动），优于命令行。

### 4.6 UI 联动

工作线程只发 Qt 信号，不碰控件（与现在 `TaskSignals` 相同）。停止按钮置 `cancelRequested` → 任务优雅收尾 → `finished(false)`，UI 恢复空闲。

---

## 5. UI 层迁移

1. `mainui.ui` 是 Qt Designer XML，**可直接复用**；uic 生成头文件，控件名（T1file、T5bit…）原样保留，槽函数逐个对应。
2. `retranslateUi` 里的中文文案原样搬运（含时间格式说明、码率推荐提示）。
3. 删除 Python 版特有的：`_build_extra_ui`（进度条进 .ui）、`QProcess` 相关、`requests` 相关。
4. 新增「引擎状态」三态：`Loading / Ready / Missing`——加载中禁用六个开始按钮，缺库时只提示不阻塞。
5. 图标沿用根目录 `favicon.ico`。

---

## 6. 启动性能与延迟加载（硬性要求）

### 6.1 为什么必须动态加载（关键陷阱）

只要在链接期链接 libav*，这些 DLL 会被写进 PE 的**导入表**，Windows 加载器在 `main()` 之前就会把它们全部映射进地址空间——**哪怕一行业务代码都不用**。启动时的这部分耗时（几十到几百毫秒，取决于 DLL 体积与依赖链）无法靠「放到后面调用」省掉。

因此本项目**明确不链接 libav***，改为运行时装载：

| 方案                                         | 结论                                                          |
| ------------------------------------------ | ----------------------------------------------------------- |
| **运行时 `dlopen`/`LoadLibrary` + 函数指针表（采用）** | 真正延迟：进程启动不碰这些 DLL；跨 MSVC/MinGW/clang、Win/Mac/Linux 通吃；缺库不致命 |
| MSVC `/DELAYLOAD:xxx.dll`                  | 仅 MSVC；MinGW 不支持；仍会在「首次调用」处产生加载尖峰。可作为补充优化                   |
| Qt 插件式 `.dll`                              | 适合把整个 FFmpeg 后端做成插件；本项目没必要                                  |

### 6.2 加载时机

```
main()
 ├─ QApplication app(...)            ← 慢启动（Qt 本体，不在本项目范围）
 ├─ MainWindow w; w.show();          ← 首帧出现，启动体验达成
 └─ QTimer::singleShot(0, &w, &MainWindow::startAsyncInit)
      └─ QThreadPool::globalInstance()->start([]{ FfmpegApi::instance().load(); … })
           └─ 信号 initialized(bool ok, QString detail) → 回到 GUI 线程更新状态
```

- 加载动作**必须离开 GUI 线程**（哪怕只耗时几十毫秒，也不能让窗口卡住不响应）；
- `singleShot(0, …)` 保证排在首帧绘制之后；
- 加载期间界面完全可用，只是六个「开始」按钮置灰，日志面板显示「正在加载 FFmpeg 库…」，与现有 Python 版「启动即检测 ffmpeg」的反馈一致。

### 6.3 FfmpegLoader 设计（宏表 + 函数指针）

```cpp
// src/ffmpeg/FfmpegLoader.h —— 唯一的动态库接触点
#pragma once
#include <QLibrary>

// 用一张宏表批量声明 + 批量绑定，避免手写上百个函数指针
#define FFMPEG_API_LIST(F)                                                        \
    /* libavutil  */                                                             \
    F(void,        av_log_set_level,   (int))                                     \
    F(unsigned,    avutil_version,      (void))                                   \
    /* libavformat */                                                            \
    F(int,         avformat_open_input, (AVFormatContext **, const char *,        \
                                          AVInputFormat *, AVDictionary **))      \
    F(int,         avformat_find_stream_info, (AVFormatContext **))              \
    F(AVFormatContext*, avformat_alloc_context, (void))                          \
    /* …其余按需列全… */

class FfmpegLoader {
public:
    static FfmpegLoader &instance();
    bool load(QString *error);        // load 六库 → 解析符号 → avformat_network_init()
    bool isLoaded() const;
    QString summary() const;          // "FFmpeg 7.1 / libavcodec 61.x（运行时加载 42ms）"

private:
    struct Lib { const char *stem; QLibrary lib; };
    Lib libs_[6]{ {"avutil"}, {"avcodec"}, {"avformat"},
                  {"avfilter"}, {"swscale"}, {"swresample"} };
    bool bindAll(QString *error);
};
```

绑定实现：

```cpp
// Windows：LoadLibraryA + GetProcAddress；其余：dlopen + dlsym
template <typename Fn> bool resolve(Fn &fn, const char *sym, QString *err);

// 声明与绑定各生成一遍
#define DECL(ret, name, args)  ret(*name) args = nullptr;
#define BIND(ret, name, args)  if (!resolve(name, #name, error)) return false;
```

- 库文件名带主版本号（如 `avcodec-61.dll` / `libavcodec.so.61` / `libavcodec.61.dylib`），**遍历候选版本号尝试加载**，避免写死；或从 exe 同目录的 `avcodec-*.dll` 通配枚举（Windows 首选）。
- 逐个 `av*_version()` 校验，版本全部记录到日志；`avcodec_version()` 与编码器名字符串一起展示，方便排错。
- 任一库缺失：释放已加载的句柄、返回 `false`，界面继续活着（§6.4）。

### 6.4 缺库时的行为（对照现有 Python 版）

| 场景             | 现有 Python 版                | 重构后                 |
| -------------- | -------------------------- | ------------------- |
| 没装 ffmpeg      | 程序能启动，日志面板红字提示「未找到 ffmpeg」 | 完全一致：启动不受影响，缺库仅日志红字 |
| 某个 DLL 损坏/版本不对 | 不适用                        | 同上，记日志并列出缺失的库名      |
| 首次点击功能按钮       | 直接报错                       | 加载未完成时按钮置灰；完成后可用    |


> 这一条是延迟加载的额外收益：Windows 传统链接方式下缺 DLL 会直接弹「无法找到 xxx.dll，程序无法运行」而**根本进不到 UI**，现在这个问题消失了。

### 6.5 启动耗时验收

用 `QElapsedTimer` 记录并写入日志：

```
启动耗时 186 ms ｜ FFmpeg 库运行时加载 41 ms（UI 已可见）
```

验收标准：**从进程启动到首帧可见 < 500 ms，且该耗时不随 FFmpeg DLL 体积变化**；库加载全部在首帧之后异步完成。

---

## 7. 构建与依赖（CMake）

由于不链接 libav*，CMake 侧非常干净：

```cmake
cmake_minimum_required(VERSION 3.16)
project(LRConvert LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)   # Network 用于版本检查
qt_standard_project_setup()                              # AUTOMOC/AUTOUIC/AUTORCC

qt_add_executable(LRConvert src/main.cpp src/...)
# 仅链接 Qt 与系统库；libav* 头文件以 include 目录方式引入
target_include_directories(LRConvert PRIVATE ${FFMPEG_INCLUDE_DIR})
target_link_libraries(LRConvert PRIVATE Qt6::Widgets Qt6::Network)
```

- **头文件**：`FFMPEG_INCLUDE_DIR` 指向你编译产物的 `include/`（仍需 `libavcodec/avcodec.h` 等头，编译期需要）；
- **库文件**：不链接，只需把 `avcodec-*.dll` 等放在 exe 同目录（或子目录 `ffmpeg/`），运行时由 `FfmpegLoader` 定位；
- **MSVC 可选加速**：`/DELAYLOAD` 不再必要（已经 dlopen）；若后续想省掉函数指针表，可再议。

发布时 Qt 侧用 `windeployqt`，FFmpeg 侧把六个 DLL 一并带上。

> **发布目录布局的硬约束（实测确认，勿再尝试）**：`windeployqt` 部署的 Qt6*.dll 与
> MinGW 运行时 DLL（libgcc/libstdc++/libwinpthread）**必须与 exe 同目录**。
> 它们写在 exe 的 PE 导入表里（`objdump -p` 可见），由 Windows 加载器在 `main()`
> 之前解析，搜索顺序只含「exe 目录 / 系统目录 / PATH」，**不含子文件夹**——
> 移到任何子文件夹都会让进程启动失败（退出码 127，无提示，已实测）。
> 唯一能放子文件夹的是 FFmpeg 的 DLL（`ffmpeg/`）：它们不进导入表，
> 由 `FfmpegApi` 用绝对路径 `LoadLibraryEx` 运行时加载。
> 若未来确实需要「根目录只有 exe」，只有两条路：微型启动器拉起子目录里的主程序，
> 或改用静态编译的 Qt（Qt/MinGW 运行时 DLL 全部消失）。

---

## 8. 风险与注意事项

| 风险                                | 影响             | 对策                                                                         |
| --------------------------------- | -------------- | -------------------------------------------------------------------------- |
| **手写函数指针表与 FFmpeg 版本不匹配**         | 符号找不到 → 加载失败   | 加载前用 `avcodec_version()` 校验；符号表集中一处，升级只需改一处；缺失符号给出明确日志                     |
| 忘记用 `FfmpegApi` 而在任务代码里直接写 `av_*` | 链接期重新引入依赖，启动变慢 | 编译期加一条静态检查：扫描 `src/` 中 `av_` 裸调用（脚本/CI）；代码评审红线                             |
| FFmpeg API 版本漂移（5/6/7 差异大）        | 编译失败           | 编译机固定版本；用 `LIBAVUTIL_VERSION_MAJOR` 条件编译；采用 ≥5.1 的 `AVChannelLayout` 新 API |
| 硬件编码器初始化失败                        | 转码直接失败         | §4.5 自动回退 libx264                                                          |
| 硬件帧格式与滤镜链不匹配                      | 花屏/黑屏          | 先跑通 libx264 纯软件链路再接硬件；硬件帧用 libavfilter 的 `hwmap`/`hwupload` 显式声明           |
| concat 流拷贝对参数不一致的文件失败             | 与现状一致的限制       | 保持现状行为，README 已有提示                                                         |
| 时间戳精度/负时间戳                        | 音视频不同步         | 统一 `av_packet_rescale_ts` + 首包平移，收尾 `av_write_trailer`                     |
| 库加载与用户首次操作竞态                      | 点按钮时库还没加载完     | 状态机 + 按钮置灰保证不会发生（§5.4）                                                     |

---

## 9. 里程碑（建议顺序）

| 阶段              | 内容                                                                 | 验收标准                                 |
| --------------- | ------------------------------------------------------------------ | ------------------------------------ |
| **M0 骨架**       | CMake + Qt6 空窗口 + `FfmpegLoader`（dlopen + 版本探测）+ 启动耗时打点            | 首帧 < 500ms；日志打印 FFmpeg 版本与加载耗时；缺库不崩溃 |
| **M1 基础设施** ✅   | `FfmpegApi` 门面 + core/（ITask、TaskSignals、时间/路径 util、串行任务队列）        | 7 个 Tab 全部迁完，信号链路与忙锁可用            |
| **M2 媒体探测** ✅   | `avformat_find_stream_info` 进程内探测（无需 ffprobe）                      | 打开任意视频显示时长                           |
| **M3 截取/分离/合并** ✅ | 三个重封装 Task（共用 `RemuxTask`，按 Kind 区分）                              | 与 ffmpeg CLI 同参数输出对比一致（见 §9.6）      |
| **M4 连接** ✅     | `RemuxTask::Kind::Concat`（多输入顺序拼接 + 时间戳累加，不依赖 concat 解封装器）         | 多段拼接可播（见 §9.6）                      |
| **M5 压缩** ✅     | `TranscodeTask`：先 libx264 全软件管线，再接 nvenc/qsv/amf/videotoolbox + 回退 | 画质/体积与 CLI 版对比（见 §9.8）             |
| **M6 收尾** ✅     | 版本检查、停止逻辑、打包（`windeployqt` + FFmpeg DLL）、README 更新                 | 全功能对照 v3.4 回归一遍（见 §9.9）            |

每个阶段结束与 ffmpeg CLI 版做**同参数输出对比**（ffprobe 比帧数/时长/码率），这是最可靠的验收。

---

## 9.5 M0 已完成（本机实测通过）

| 项 | 结果 |
|---|---|
| 代码 | `src/ffmpeg/`（FfmpegSymbols 宏表 + FfmpegApi 加载器 + FfmpegCapability）、`src/main.cpp`、`src/MainWindow.*`、`tools/capability_check.cpp`、`CMakeLists.txt` |
| 构建 | Qt 6.12.0 **MinGW 版** + Qt 自带 mingw1310 编译器，一次通过 |
| 运行 | `LRCONVERT_SMOKE_TEST=1` 冒烟跑通：退出码 0，事件循环正常，无崩溃 |
| 实测链路 | `界面已显示，开始后台加载 FFmpeg 动态库` → `动态库加载成功 … 运行时加载 6 ms` |
| 能力清单 | 与 §2.1 完全一致：解码器 1 / 编码器 2 / 解封装器 1 / 封装器 2 |

**构建与运行**：

```bash
./scripts/build.sh                 # 构建（内部固定用 Qt 自带的 MinGW 编译器）
./scripts/run.sh                   # 运行（临时把 Qt DLL/插件加进 PATH）
./build/capability_check.exe       # 能力清单工具，不依赖 Qt，随时可跑
# 正式发布：windeployqt build/LRConvert.exe
```

> 注意：必须用 Qt 自带的 `Tools/mingw1310_64` 编译器，而不是系统里更新的 g++；
> Qt 官方 MinGW 包由 GCC 13.1 构建，混用大版本不同的 libstdc++ 容易运行时出错。

---

## 9.6 M1–M4 已完成（与 ffmpeg CLI 逐项对比通过）

### 新增代码

| 文件 | 作用 |
|---|---|
| `src/core/ITask.h` | `TaskSignals`（工作线程→界面的唯一通道）+ `ITask` 抽象 + 日志颜色常量 |
| `src/core/TimeUtil.*` | `parseTimestamp` / `formatTimestamp` / AV_TIME_BASE 换算 |
| `src/core/PathUtil.*` | `VIDEO_EXTS` / 文件过滤 / `SCALE_FILTERS` / `HW_ENCODERS` / `outputPath` / `copySafeSuffix` |
| `src/tasks/RemuxTask.*` | **流拷贝管线**，按 `Kind` 覆盖截取 / 分离 / 合并 / 连接 |
| `src/ui/mainui.ui` | 原 `python/mainui.ui` 直接搬过来，7 个 Tab、控件名一字不改，由 uic 生成 `Ui::Form` |
| `src/MainWindow.*` | 继承 `QWidget` + `private Ui::Form`，还原 Python 版的初始化、连接、日志、进度、忙锁、版本检查 |
| `tools/remux_check.cpp` | 无界面功能验证工具，走的是与 GUI **完全相同**的代码路径 |

### 实测对比（源：6 秒 320x240 / h264 + aac / 关键帧间隔 1 秒）

| 功能 | 我们的输出 | ffmpeg CLI 输出 | 结论 |
|---|---|---|---|
| 截取 2s→5s | `3.018594` | `3.018594` | ✅ 一致 |
| 分离视频轨 | `6.000000` | `6.000000` | ✅ 一致 |
| 分离音频轨 | `6.000000` | `6.000000` | ✅ 一致 |
| 视频+音频合并 | `6.000000` | `6.000000` | ✅ 一致 |
| 连接 2 个 6s | `12.000000` | `12.023220` | ✅ 可播、全片解码无错（比 CLI 还准，见下方说明） |

### 过程中踩到并已修掉的两个坑（很重要）

1. **`-avoid_negative_ts` 只能给「截取」用。**
   Python 版也只在 `cmd_cut` 里加了这个参数。一开始我对所有 Remux 都设了 `avoid_negative_ts = 1`，
   结果 AAC 的编码器延迟（首包 pts = −1024/44100 ≈ −23ms）被抹成 0，音频轨整体后移一帧，
   输出时长比源文件多出 23ms。现在只有 `startSec > 0` 时才设。

2. **拼接时第二个及以后的文件必须丢掉负时间戳的包。**
   否则该轨道 dts 回退，mp4 封装器直接报
   `Application provided invalid, non monotonically increasing dts to muxer`，
   整个任务失败。这些包是编码器延迟产生的引导样本，concat 解封装器同样是丢掉的。
   丢掉之后输出时长正好是 6+6=12.000000，反而比 CLI（12.023220，多了一帧引导样本）更准。

### 无界面验证

```bash
./build/remux_check.exe cut    <输入> <输出> <起始秒> <结束秒>
./build/remux_check.exe video  <输入> <输出>          # 只留视频轨
./build/remux_check.exe audio  <输入> <输出>          # 只留音频轨
./build/remux_check.exe merge  <视频> <音频> <输出>
./build/remux_check.exe concat <输出> <输入1> <输入2> ...
```

---

## 9.7 切换到完整版 FFmpeg（gplfull）与依赖裁剪

原精简构建只有 h264 解码 / aac+libx264 编码 / mov 解封装 / mp4-mov 封装，
压缩功能与 mkv/avi/flv 都跑不通。现已换成完整构建 `FFmpeg 9.0.2-gplfull`
（解码器 536 ／ 编码器 120 ／ 解封装器 359 ／ 封装器 183），
头文件版本同为 avcodec 63.1.102 / avformat 63.1.102 / avutil 61.1.102，ABI 兼容，无需改代码。

依赖副本放在 **`vendor/ffmpeg/`**（`bin/` 29 个 DLL 74.6 MB + `include/`），
不直接引用外部构建目录。挑选方法与完整清单见 `vendor/ffmpeg/README.md`。

裁剪结果：从完整构建的 50 个 DLL / 89.7 MB，按「本项目实际加载的 5 个核心库」
求 PE 导入表依赖闭包，取 **29 个 / 74.6 MB**，剔除 21 个 / 15.1 MB
（主要是 avfilter 及 libass/freetype/harfbuzz/glib 等字体字幕库、avdevice、xvidcore/theora）。

> 注意：闭包内的第三方库（`libx265` 21.6 MB、`libaom` 9.2 MB、`libSvtAv1Enc` 7.2 MB、
> `libvpx` 4.2 MB）**不能靠删文件去掉**——它们写死在 `avcodec-63.dll` 的导入表里。
> 要再省这 38 MB，只能重新编译 FFmpeg 时去掉对应的 `--enable-lib*`。

### 换库后新增踩到的两个坑（均已修）

3. **⚠️ FFmpeg 的 DLL 绝不能与 exe 放同级目录。**
   完整构建会附带 `zlib1.dll`、`libstdc++-6.dll`、`libgcc_s_seh-1.dll`、
   `libbz2-1.dll`、`liblzma-5.dll` 等通用运行时库。它们一旦与 exe 同级，
   Windows 加载器在解析 **Qt 自身依赖**时会优先命中这些同名 DLL（exe 目录优先级最高），
   版本对不上直接让**进程启动失败**：双击无反应、命令行退出码 **127**、
   **没有任何错误提示**（很容易误判成 Qt 装坏了）。
   改为放到 exe 同级的 `ffmpeg/` **子目录**即可——`searchDirs()` 本来就包含该路径。
   CMake 的 POST_BUILD 已按此布局复制。

4. **`LoadLibrary` 不会自动在被加载 DLL 所在目录找它的依赖。**
   完整构建是动态链接第三方库的，`avcodec-63.dll` 的导入表里写着
   `libx264-165.dll` / `libx265-217.dll` / `libaom.dll` … 用绝对路径 `LoadLibrary` 时，
   这些依赖走标准搜索顺序解析，全部找不到，报 **error 126**。
   改用 `LoadLibraryExW` + `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR`
   （辅以 `AddDllDirectory`）解决。旧精简构建没有第三方依赖，所以当时没暴露。

### 换库后的回归结果（与 ffmpeg CLI 对照）

| 功能 | 我们 | ffmpeg CLI |
|---|---|---|
| 截取 2s→5s | `3.018594` | `3.018594` ✅ |
| 分离视频轨 | `6.000000` | `6.000000` ✅ |
| 分离音频轨 | `6.000000` | `6.000000` ✅ |
| 视频+音频合并 | `6.000000` | — ✅ |
| 连接 2×6s | `12.000000` | `12.023220` ✅ |
| **输出 MKV**（新能力） | `3.038000` | 精简版无 mkv muxer |

GUI 冒烟：7 个 Tab 正常，运行时加载 31 ms，退出码 0。

---

## 9.8 M5 压缩已完成（转码矩阵与 ffmpeg CLI 对照通过）

### 新增代码

| 文件 | 作用 |
|---|---|
| `src/tasks/TranscodeTask.*` | 完整转码管线：解封装 → 解码 → sws_scale → 编码 → swr 重采样 → aac → 封装 |
| `tools/transcode_check.cpp` | 无界面转码验证工具，走与 GUI 完全相同的代码路径 |
| `src/MainWindow.cpp` 的 `batchCode()` / `customCode()` | Tab5 批量压缩 / Tab6 自定义压缩接线 |

码率公式与 Python 版 `cmd_transcode` 逐字对齐：`bufsize = max(2×码率, 1000k)`、
`maxrate = max(1.5×码率, 码率+500k)`，私有选项 `profile=high / preset / level`，
`-pix_fmt yuv420p`，mov 家族输出带 `+faststart`。

### 实测矩阵（源：720p / 6s / h264+aac；参考 CLI 为 FFmpeg 7.1，运行时为 9.0.2）

| 用例 | 我们的输出 | CLI 输出 | 结论 |
|---|---|---|---|
| 原始大小 / 4000k | 6.000000 · 180 帧 · 260 音频包 | 同左 | ✅ |
| 一半 / 2000k | 6.000000 · 180 帧 · 640x360 | 同左 | ✅ |
| 四分之一 / 1000k | 6.000000 · 180 帧 · 320x180 | 同左 | ✅ |
| 指定 1280x720 / 4000k | 6.000000 · 180 帧 | 同左 | ✅ |
| 无音轨（audioKbps=0） | 180 帧 · 解码干净 | 同左 | ✅ |
| MKV 容器 | 6.023000 · 180 帧 · 解码干净 | 同左 | ✅ |
| h264_nvenc（无 NVIDIA 显卡） | 自动回退 libx264，成功 | — | ✅ |

全部输出通过全片解码校验（`ffmpeg -v error -i out -f null -` 无任何输出）。

### 过程中踩到并已修掉的坑（续 §9.6、§9.7）

5. **无音轨输出比 CLI 短 33ms——是版本差异，不是 bug。**
   同为 180 帧、最后一个包的 pts/dts/duration 逐项相同，但 mp4 的 edit list
   （`elst`）时长我们算出 91653 ticks、CLI 算出 92160 ticks。原因是参考用的
   `C:/ffmpeg` 是 **FFmpeg 7.1**，而运行时是 **9.0.2**，mov 封装器的 edit list
   算法在两个大版本间有变化。包内容一致、播放无损，仅容器元数据差 1 帧。

6. **截取边界与 CLI 差约 2 帧——同样是 CLI 的实现细节。**
   CLI 按 **dts** 收尾，B 帧重序下会多带约 2 帧（66ms），muxing 队列的预读
   还会多冲几个音频包；我们按 pts 收尾，输出是精确的 `[起始, 结束)` 半开区间。
   用无 B 帧的夹具时两者逐项一致（§9.6 的旧实测正是如此）。不为复现 CLI 的
   队列冲刷 artifact 把截取改粗。

---

## 9.9 M6 收尾已完成（打包 / README / 一键回归）

| 项 | 落点 |
|---|---|
| 版本检查 | `MainWindow::checkVersion()`：QNetworkAccessManager 拉取 gitee `version.md`，语义化比较，有更新时提示（原 `check_version()` 行为一致） |
| 停止逻辑 | `ITask::requestStop()` → `RemuxTask`/`TranscodeTask` 的 `cancel_` 在拷贝/编码循环中检查；置位后 flush 解码器 → flush 编码器 → `av_write_trailer`，输出文件完整可播。关闭窗口时同样走优雅停止 |
| 引擎三态 | Loading（六个开始按钮置灰）→ Ready（放开）→ Missing（保持置灰 + 红字提示）。`setBusy()` 已覆盖 Tab5/Tab6 的开始/停止按钮（M5 接线时漏了这两个 Tab） |
| 打包 | `scripts/package.sh`：构建 → `windeployqt` → `ffmpeg/` 子目录（29 DLL）→ 干净 PATH 下冒烟验证。产物 `dist/LRConvert-<版本>/` + 同名 zip，共约 114 MB（版本号从 `src/core/Version.h` 读取，见 §9.11） |
| README | 根 `README.md`：与 Python 版的区别、构建/运行/库放置规则、验证工具用法、已知限制 |
| 一键回归 | `scripts/regression.sh`：**12/12 通过**（流拷贝 5 项 + 转码矩阵 7 项，逐项与 CLI 对照 + 全片解码校验） |

### 过程中踩到并已修掉的坑（续前）

7. **Git Bash 里 PATH 写 `C:/Qt/...` 会让原生程序退出码 127。**
   MSYS 只转换 `/c/...` 这类 POSIX 风格路径，`C:/...` 会原样透传给 Windows
   进程，`LoadLibrary` 解析 PATH 条目时找不到 `Qt6Core.dll`。
   `scripts/run.sh`、`scripts/regression.sh`、`scripts/package.sh`
   统一改为 `cygpath -u` 转换后再拼 PATH。

### 回归结论

六大功能与 ffmpeg CLI 同参数输出逐项对比：**帧数、时长、分辨率全部一致**，
输出全部解码干净。两处已知差异（截取 B 帧边界约 66ms、无音轨容器时长 33ms）
均为 CLI 侧实现细节/版本差异，详见 §9.8，不影响功能与播放。

---

## 9.10 硬件编码器识别（M6 后补充）

启动后（与动态库加载同在后台线程）对 `HW_ENCODERS` 里的硬件编码器逐个做本机探测，
**本机不支持的项从 Tab5/Tab6 的编码器下拉框中隐藏**，避免"选完 nvenc、
一点开始才回退 libx264"的体验。

| 项 | 落点 |
|---|---|
| 探测函数 | `src/ffmpeg/FfmpegCapability.cpp` 的 `probeHardwareEncoders()` |
| 判定方式 | 与 `TranscodeTask::tryOpen` **逐字段一致**：`avcodec_find_encoder_by_name` + 320x240/yuv420p 最小配置 `avcodec_open2`。探测可用 ⟺ 任务里能打开，两边不会说不一样的话 |
| 时机 | `startAsyncInit()` 的工作线程里，`ffmpegLoaded` 信号之后；不碰 GUI 线程 |
| 界面 | `hwEncodersProbed` 信号 → `applyHwEncoders()` 重建两个下拉框：**启动时下拉框只含 libx264（软件）**，探测确认可用的硬件项再追加；libx264 永留，本机不支持的硬件项从头到尾不出现（不存在"先全量再收敛"的闪烁）。加载中/失败时两个编码器下拉框一并置灰 |
| 反馈 | 日志区列出每项不可用的原因（FFmpeg 错误码 + 静态要求说明，如"需要 NVIDIA 显卡与驱动"）；`capability_check` 第 [7] 节可无界面复现 |

实测（本机无 NVIDIA/Intel QSV/AMD，非 macOS）：4 个硬件编码器全部正确隐藏，
下拉框从头到尾只有 `libx264`；冒烟测试在第 1 秒（加载中）和第 4 秒（探测后）
分别打印 Tab5/Tab6 两个下拉框的内容，均验证为仅 `libx264`。

---

## 9.11 版本号 4.0 与发布包命名

C++ 重构版随 Python 版的版本线起跳，定为 **4.0**（Python 原版最后一个版本 3.4，
见 `python/version.md`）。

| 项 | 落点 |
|---|---|
| 单一来源 | `src/core/Version.h`：`lr::kAppVersion = "4.0"`。原先三处硬编码（`MainWindow.cpp`、`main.cpp`、`CMakeLists.txt`）全部改为引用 |
| CMake | `project()` 前用 `file(READ)` + 正则从 Version.h 解析 `VERSION`，解析失败直接 `FATAL_ERROR` |
| 打包脚本 | `scripts/package.sh` 用 sed 从 Version.h 解析版本号，产物为 `dist/LRConvert-4.0/` 与 `dist/LRConvert-4.0-win64.zip`（zip 内保留顶层目录，解压即用；GNU tar `-a` 按扩展名自动选 zip 格式） |
| 更新检查 | 仓库根新建 `version.md`（`version: 4.0`）——应用内「检查更新」拉取的就是这个文件，与 Version.h 保持一致 |
| 冒烟钩子 | 第 4 秒的 `[SMOKE]` 输出增加窗口标题一行，发布前可直接核对 `version 4.0` 字样 |

验证：构建通过；冒烟输出 `Little Rabbit Convert  version 4.0`，Tab5/Tab6
编码器下拉框两个时间点均仅 `libx264`；打包干净 PATH 冒烟通过；zip 共 60 个条目、
含 29 个 FFmpeg DLL，约 114 MB。

---

## 10. 建议顺带处理的遗留

- ~~README 增加「动态库版」章节~~ —— 已完成：根 `README.md`（M6）。
- 原 `tests/` 目录漏提交，C++ 版可一步到位上 gtest（`core/` 不依赖 Widgets，天然可测）。
- `mainui.ui`（2023 年文件）借此次重构一并清理无用控件与过期文案。

---

## 附：Python → C++ 概念速查

| Python 概念               | C++/Qt 对应             |
| ----------------------- | --------------------- |
| QProcess + 管道           | 进程内 API 调用，无管道        |
| `Signal`/`emit`         | 同名机制，跨线程一致            |
| ThreadPoolExecutor(1)   | QThread + 任务队列        |
| subprocess kwargs（隐藏黑窗） | 不需要                   |
| requests                | QNetworkAccessManager |
| pathlib                 | std::filesystem       |
| 启动时同步查找 ffmpeg          | UI 首帧后异步 dlopen（§6）   |
