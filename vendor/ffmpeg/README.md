# FFmpeg 依赖副本

本目录是项目**实际使用**的 FFmpeg 依赖，已经过裁剪。
构建时由 CMake（`vendor/ffmpeg`）引用，产物里的 DLL 会被自动复制到可执行文件同目录。

> **不要直接引用外部构建目录。** 原始完整构建（`ffmpeg-gpl-full/`）留在仓库外，
> 只作为本目录内容的来源；每次更换 FFmpeg 构建时，重新挑选并覆盖本目录。

---

## 1. 基本信息

| 项 | 值 |
|---|---|
| 来源 | `ffmpeg-gpl-full/`（完整构建，95 MB） |
| 版本 | **FFmpeg 9.0.2-gplfull** |
| 库版本 | avcodec **63.1.102** ／ avformat **63.1.102** ／ avutil **61.1.102** ／ swscale 10 ／ swresample 7 |
| 许可 | **GPL version 3 or later**（与项目根 `LICENSE` 一致） |
| 工具链 | MinGW-w64（x86_64），与 Qt 6.12.0 MinGW 版配套 |
| 本目录体积 | `bin/` 29 个 DLL ＝ 74.6 MB；`include/` 145 个头文件 ＝ 1.3 MB |

**头文件版本与旧精简构建完全一致**（同为 63.1.102 / 61.1.102），因此 ABI 兼容，
切换运行时库无需修改任何代码。

---

## 2. 挑选方法

不是"凭感觉删"，而是**按 PE 导入表求依赖闭包**：

1. 本工程运行时只加载 5 个核心库（见 `src/ffmpeg/FfmpegApi.cpp` 的 `kLibs`）：
   `avutil` ／ `avcodec` ／ `avformat` ／ `swscale` ／ `swresample`
2. 用 `objdump -p` 读出每个 DLL 的导入表，从这 5 个根出发递归遍历；
3. 凡在闭包内的一律保留，闭包外的一律剔除。

**为什么必须按闭包而不是"看起来有用"来挑**：`avcodec-63.dll` 的导入表里写死了
`libx264-165.dll` / `libx265-217.dll` / `libaom.dll` 等第三方库。只要 `avcodec` 被加载，
Windows 加载器就会强制加载它们——**少一个文件，`LoadLibrary` 直接失败（error 126）**。
所以闭包内的文件一个都不能删。

| | 文件数 | 体积 |
|---|---|---|
| 完整构建 `bin/` | 50 | 89.7 MB |
| **选用（闭包）** | **29** | **74.6 MB** |
| 剔除 | 21 | 15.1 MB |

---

## 3. 剔除清单及原因

共 21 个，主要是 `avfilter` 及其级联的字体／字幕库：

| 剔除项 | 大小 | 原因 |
|---|---:|---|
| `avfilter-12.dll` | 5.54 MB | 本工程未加载。缩放走 `swscale`，不用滤镜链 |
| `libharfbuzz-0.dll` | 1.54 MB | 文字排版，仅 avfilter 的 subtitles/drawtext 需要 |
| `libglib-2.0-0.dll` | 1.50 MB | libass 依赖 |
| `libiconv-2.dll` | 1.09 MB | 字符编码转换，同上 |
| `libfreetype-6.dll` | 0.75 MB | 字体渲染，同上 |
| `xvidcore.dll` | 0.75 MB | Xvid 编码器，本项目不用 |
| `libpcre2-8-0.dll` | 0.67 MB | 正则，glib 依赖 |
| `libzimg-2.dll` | 0.61 MB | zscale 滤镜，仅 avfilter 需要 |
| `libfontconfig-1.dll` | 0.48 MB | 字体配置，libass 依赖 |
| `libass-9.dll` | 0.34 MB | 字幕渲染，仅 avfilter 需要 |
| `libintl-8.dll` | 0.28 MB | i18n，glib 依赖 |
| `libpng16-16.dll` | 0.25 MB | PNG，libass/freetype 依赖 |
| `libtheoraenc-2.dll` | 0.24 MB | Theora 编码器，本项目不用 |
| `libexpat-1.dll` | 0.23 MB | XML 解析，fontconfig 依赖 |
| `libgraphite2.dll` | 0.16 MB | 复杂文字排版，harfbuzz 依赖 |
| `libfribidi-0.dll` | 0.15 MB | 双向文字，libass 依赖 |
| `avdevice-63.dll` | 0.14 MB | **设备采集**（摄像头／屏幕／音频输入），本项目不需要 |
| `libbrotlicommon.dll` | 0.14 MB | Brotli 压缩，freetype 依赖 |
| `libunibreak-7.dll` | 0.13 MB | 断行，libass 依赖 |
| `libtheoradec-2.dll` | 0.09 MB | Theora 解码器（**解码器保留会更稳妥，见下**） |
| `libbrotlidec.dll` | 0.06 MB | Brotli 解压，freetype 依赖 |

> **注意**：`libtheoraenc` / `libtheoradec` / `xvidcore` 不在 avcodec 导入表里（动态加载），
> 剔除不影响启动。但 Theora／Xvid 的**编解码功能**会不可用。本项目六个功能不涉及，
> 故剔除；若日后需要，从源构建目录补回即可。

---

## 4. 保留的第三方库（不可删）

这些是 `avcodec` / `avutil` 的直接导入依赖，**删掉任何一个程序都启动不了**：

| 库 | 大小 | 用途 |
|---|---:|---|
| `libx265-217.dll` | 21.62 MB | HEVC 编码 |
| `libaom.dll` | 9.24 MB | AV1 编码 |
| `libSvtAv1Enc-4.dll` | 7.22 MB | AV1 编码（SVT） |
| `libvpx-1.dll` | 4.21 MB | VP8/VP9 编解码 |
| `libx264-165.dll` | 1.93 MB | H.264 编码（**本项目压缩功能的主力**） |
| `libwebp-7.dll` / `libwebpmux-3.dll` / `libsharpyuv-0.dll` | 0.84 MB | WebP 图像 |
| `libvorbis-0.dll` / `libvorbisenc-2.dll` / `libogg-0.dll` | 0.77 MB | Vorbis 音频 |
| `libopus-0.dll` | 0.48 MB | Opus 音频 |
| `libvpl-2.dll` / `libva.dll` / `libva_win32.dll` | 0.75 MB | QSV / VA-API 硬件加速 |
| `libsoxr.dll` | 0.34 MB | 高质量重采样（swresample） |
| `libmp3lame-0.dll` | 0.31 MB | MP3 编码 |
| `liblzma-5.dll` / `libbz2-1.dll` / `zlib1.dll` | 0.40 MB | 压缩（字幕／容器） |
| `libstdc++-6.dll` / `libgcc_s_seh-1.dll` / `libwinpthread-1.dll` / `libgomp-1.dll` | 2.68 MB | MinGW 运行时 |

> **若要进一步瘦身，只能重新编译 FFmpeg**（去掉 `--enable-libx265`、
> `--enable-libaom`、`--enable-libsvtav1` 等），可再省约 38 MB。
> 单纯删文件是无效的——它们被写进了 `avcodec-63.dll` 的导入表。

---

## 5. 本构建的能力

| 类别 | 数量 | 关键项 |
|---|---|---|
| 解码器 | 536 | h264 / hevc / vp9 / av1 / aac / mp3 … |
| 编码器 | 120 | libx264 / libx265 / libaom-av1 / libsvtav1 / libvpx-vp9 / aac / libmp3lame / libopus / libvorbis |
| 解封装器 | 359 | mov（含 mp4/m4a/3gp）／ matroska（含 webm）／ avi ／ flv ／ mpegts ／ image2 ／ concat |
| 封装器 | 183 | mp4 ／ mov ／ matroska ／ webm ／ avi ／ flv ／ mpegts ／ 3gp ／ mp3 ／ gif |

硬件编码器：`h264/hevc` 的 `_nvenc`（NVIDIA）、`_qsv`（Intel）、`_amf`（AMD）、
`_vaapi`、`_d3d12va`、`_mf` 均可用；AV1 的 nvenc / qsv / amf / vaapi 也在。

> demuxer 名字是逗号分隔的别名组（如 `mov,mp4,m4a,3gp,3g2,mj2`、
> `matroska,webm`），用精确字符串匹配会误判为"缺失"。

---

## 6. 加载机制（重要）

这 5 个核心库**不参与链接**，只由 `FfmpegApi` 在 UI 首帧之后用
`LoadLibraryExW` 运行时加载。三个必须知道的点：

### 6.1 不链接 = 启动不受影响

**不链接 = DLL 不进 PE 导入表** = 进程启动完全不碰这些库。
换来的效果：从双击到窗口出现不受 FFmpeg 体积影响（74.6 MB 也无所谓）。

### 6.2 `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR` 是必需的

用绝对路径 `LoadLibrary` 时，Windows 在解析**导入表里的依赖**（libx264 / libx265 /
libaom 等）走的是标准搜索顺序（exe 目录 → 系统目录 → 当前目录 → PATH），
**不会**自动去"被加载 DLL 所在目录"找，结果就是全部以 **error 126** 失败
（`ERROR_MOD_NOT_FOUND`），哪怕这些 DLL 就在同一个文件夹里。

该标志把被加载 DLL 的目录临时插入搜索列表，专门解决此问题；
另用 `AddDllDirectory` 注册目录作为第二道保险。
两者都实现在 `src/ffmpeg/FfmpegApi.cpp`。

### 6.3 ⚠️ DLL 必须放子目录，绝不能与 exe 同级

这是本项目实际踩到的坑，后果比上面两个严重得多。

`bin/` 里有一批**通用运行时库**——`zlib1.dll`、`libstdc++-6.dll`、
`libgcc_s_seh-1.dll`、`libwinpthread-1.dll`、`libbz2-1.dll`、`liblzma-5.dll`。
如果把它们复制到 exe **同级目录**：Windows 加载器在解析 **Qt 自身的依赖**时，
会优先命中 exe 目录下这些同名 DLL（exe 目录的搜索优先级最高），
版本对不上就直接让**整个进程启动失败**——

- 表现为：双击无任何反应，命令行退出码 **127**，
- **没有任何错误提示**（不是 FFmpeg 的问题，是 Qt 加载不起来）。

因此部署布局必须是**子目录隔离**：

```
发布目录/
├── LRConvert.exe
├── Qt6Core.dll  Qt6Widgets.dll  ...     ← Qt 的 DLL（windeployqt 生成）
├── iconengines/  imageformats/  platforms/
└── ffmpeg/                              ← FFmpeg 及其第三方依赖，全部放这里
    ├── avcodec-63.dll  avformat-63.dll  avutil-61.dll
    ├── swscale-10.dll  swresample-7.dll
    └── libx264-165.dll  zlib1.dll  ...  （共 29 个）
```

`FfmpegApi::searchDirs()` 已经包含 `<exe目录>/ffmpeg`，放到这里就能被自动找到。
CMake 的 POST_BUILD 步骤也已按此布局复制（见 `CMakeLists.txt` 里的注释）。

> 顺带一提：这个问题在旧精简构建上不会出现——它只有 5 个 `av*` DLL，
> 没有附带任何通用运行时库，所以当时放同级目录也没事。

---

## 7. 第三方库许可

整体为 **GPL-3**（因为静态／动态链入了 libx264、libx265 等 GPL 组件）。
其中部分组件为宽松许可（libaom / libvpx / libwebp / libopus / libvorbis 为 BSD 系，
libmp3lame / libsoxr 为 LGPL，zlib / bzip2 / lzma 为各自宽松许可）。

分发本程序时须遵守 GPL-3：提供完整源码或书面要约，并保留本说明。

---

## 8. 更新本目录

更换 FFmpeg 构建时：

1. 从新构建目录求依赖闭包（脚本思路见 §2）；
2. 覆盖 `bin/` 与 `include/`；
3. 核对 `include/libav*/version.h` 的主版本号——若 avcodec 主版本变化
   （63 → 64），需同步更新 `src/ffmpeg/FfmpegSymbols.h` 的符号绑定；
4. 跑 `./build/capability_check.exe` 确认加载与能力清单正常。
