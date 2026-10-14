# LRconvert（小兔子转换器）· C++ 版

FFmpeg 的图形界面：**截取 / 连接 / 分离 / 合并 / 批量压缩 / 自定义压缩**。

本目录是 **C++20 + Qt6 重写版**。原 Python/PySide6 版本（v3.4）保存在
[`python/`](python/) 目录，含完整的历史代码与 MIT 许可证。

## 与 Python 版的区别

| | Python 版（python/） | C++ 版（本目录） |
|---|---|---|
| FFmpeg 调用方式 | 子进程跑 `ffmpeg.exe`，解析 `-progress pipe:1` 输出 | **进程内运行时加载 libav\* 动态库**，直接调 API |
| ffmpeg 安装 | 需要用户自行安装并加入 PATH | 不需要， DLL 随程序分发 |
| 进度 | 解析命令行文本输出 | 编码/拷贝循环内按 pts 直接计算 |
| 停止 | 先发 `q` 再强杀进程 | 协作式取消：flush 解码器/编码器后正常写文件尾 |
| 启动速度 | 启动即检测 ffmpeg | **UI 先出首帧，动态库在后台加载**，缺库不阻塞启动 |
| 界面 | PySide6 + mainui.ui | Qt6 + 同一份 mainui.ui（控件名、交互完全一致） |

六大功能的行为（输入输出、命名规则 `原名_new_时间戳.后缀`、冲突追加 `(1)`）
与原版保持一致，老用户零学习成本。

## 构建

依赖：

- **Qt 6**（MinGW 版，≥ 6.5；MSVC 版与 FFmpeg 的 MinGW-w64 构建 ABI 不兼容，不能混用）
- **CMake** ≥ 3.20
- 编译器：**Qt 自带的 MinGW**（`Qt/Tools/mingw1310_64`，GCC 13.1）。
  不要用系统里更新的 g++——Qt 官方包由 GCC 13.1 构建，混用大版本不同的
  libstdc++ 容易运行时出错。

```bash
./scripts/build.sh          # Release 构建
./scripts/build.sh clean    # 清掉 build/ 重新配置
```

FFmpeg 头文件在仓库内的 [`vendor/ffmpeg/include/`](vendor/ffmpeg/include/)，构建直接用它。
29 个运行时 DLL（74.6 MB，挑选依据见 [`vendor/ffmpeg/README.md`](vendor/ffmpeg/README.md)）
**不在仓库里**（体积原因，已 gitignore），两种获取方式：

- 从 [GitHub Release](../../releases) 的发布包里取（解压后 `ffmpeg/` 子目录即是）；
- 或者设环境变量 `LRCONVERT_FFMPEG_DIR` 指向你自己的 FFmpeg DLL 目录。

构建本身只需要 `include/`，DLL 缺失不影响编译；`scripts/package.sh` 会在打包时
自动把 `vendor/ffmpeg/bin/` 的内容复制进发布目录。

> 工程**不链接** libav\*——符号在运行时由 `src/ffmpeg/FfmpegApi` 解析，
> 这样 DLL 不会写进 PE 导入表，进程启动时不会预加载它们。详见
> [`docs/LRConvert_Cpp_Qt_重构方案.md`](docs/LRConvert_Cpp_Qt_重构方案.md) §6。

## 运行

```bash
./scripts/run.sh
```

FFmpeg 动态库按以下顺序查找（`src/ffmpeg/FfmpegApi.cpp`）：

1. 环境变量 `LRCONVERT_FFMPEG_DIR` 指定的目录；
2. exe 同级的 `ffmpeg/` 子目录（构建与打包脚本的默认布局）；
3. exe 所在目录本身。

> ⚠️ **不要把 FFmpeg 的 DLL 放到 exe 同级目录**。完整构建会附带
> `zlib1.dll` / `libstdc++-6.dll` / `libbz2-1.dll` 等通用运行时库，
> 它们会优先命中 Qt 自身的同名依赖（exe 目录搜索优先级最高），
> 版本对不上会让进程直接启动失败（退出码 127，无任何提示）。
> 放 `ffmpeg/` 子目录即可物理隔离。

## 无界面验证工具

不走 GUI、直接复用任务层代码路径，便于回归和 CI：

```bash
./build/capability_check.exe                        # 引擎能力清单（不依赖 Qt）
./build/remux_check.exe cut    <输入> <输出> <起始秒> <结束秒>
./build/remux_check.exe video  <输入> <输出>        # 只留视频轨
./build/remux_check.exe audio  <输入> <输出>        # 只留音频轨
./build/remux_check.exe merge  <视频> <音频> <输出>
./build/remux_check.exe concat <输出> <输入1> <输入2> ...
./build/transcode_check.exe  <输入> <输出> [编码器] [视频kbps] [缩放] [音频kbps]
```

一键全功能回归（与 ffmpeg CLI 同参数输出逐项对比）：

```bash
./scripts/regression.sh
```

## 打包分发

```bash
./scripts/package.sh        # 构建 + windeployqt + FFmpeg DLL
```

产物（版本号从 `src/core/Version.h` 读取，单一来源）：

- `dist/LRConvert-<版本>/` —— 完整发布目录（exe、Qt 运行时、`ffmpeg/` 子目录、README、LICENSE）
- `dist/LRConvert-<版本>-win64.zip` —— 上一项的压缩包，解压即用

打包末尾会在无 Qt 开发环境的干净 PATH 下自动冒烟验证自包含性。

> **发布目录里的 Qt DLL 不能移到子文件夹。** 它们写在 exe 的 PE 导入表里，
> 由 Windows 加载器在 `main()` 之前按「exe 目录 → 系统目录 → PATH」解析，
> 子文件夹不在搜索范围——移走会让程序直接无法启动（退出码 127，无提示）。
> FFmpeg 的 DLL 是个例外：它们放在 `ffmpeg/` 子目录，因为程序是用绝对路径
> 运行时加载它们的，不进导入表。

## 已知限制

- 截取/连接是流拷贝，从最近关键帧开始，起点可能略早于设定时间（与原版一致）。
- 连接要求各段编码参数一致（与原版一致，README 原有提示）。
- **硬件编码器启动后自动识别**：批量压缩（Tab5）与自定义压缩（Tab6）的编码器
  下拉框启动时只含软件编码器 libx264，后台探测确认本机可用（有对应显卡）的
  硬件项才会加入；不支持的项从不出现，日志区有原因说明。万一选中后仍打开
  失败，任务会自动回退 libx264。
- 截取边界与 ffmpeg CLI 存在约 2 帧（66ms）差异：CLI 按 dts 收尾，
  B 帧内容下会多带少量帧；本实现按 pts 收尾，输出是精确的 `[起始, 结束)` 区间。
  详见 docs/ 回归方案 §9.8。

## 许可证

- C++ 版：GPL-3.0（见 [LICENSE](LICENSE)，因运行时链接的 FFmpeg 为 GPL 构建）
- Python 原版：MIT（见 [python/LICENSE](python/LICENSE)）
