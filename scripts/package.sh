#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 打包 LRConvert 为可分发目录 dist/LRConvert-<版本>/ 及 zip
#
# 版本号从 src/core/Version.h 解析（单一来源，与 version.md 一致），
# 产物：dist/LRConvert-<版本>/、dist/LRConvert-<版本>-win64.zip（完整程序），
#       以及 dist/LRConvert-<版本>-ffmpeg-dll-win64.zip（只有 FFmpeg DLL）。
#
# 布局（与构建目录一致，FFmpeg DLL 放 ffmpeg/ 子目录，原因见 CMakeLists.txt
# 里的注释：不能与 exe 同级，否则 zlib1.dll 等会劫持 Qt 的依赖解析）：
#
#   dist/LRConvert-4.0/
#   ├── LRConvert.exe          ← 主程序
#   ├── Qt6*.dll 等            ← windeployqt 部署的 Qt 运行时
#   ├── plugins/ styles/ ...   ← windeployqt 部署的 Qt 插件
#   ├── ffmpeg/                ← vendor/ffmpeg/bin 的 29 个 DLL（74.6 MB）
#   ├── README.md
#   └── LICENSE
#
# 用法：
#   ./scripts/package.sh           # 先构建再打包
#   SKIP_BUILD=1 ./scripts/package.sh   # 用当前 build/ 直接打包
# ---------------------------------------------------------------------------
set -euo pipefail

QT_KIT="${QT_KIT:-C:/Qt/6.12.0/mingw_64}"
QT_MINGW="${QT_MINGW:-C:/Qt/Tools/mingw1310_64}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-build}"

# 版本号：单一来源 src/core/Version.h（CMakeLists.txt 与打包脚本都读它）
VERSION="$(sed -n 's/.*kAppVersion *= *"\([0-9][0-9.]*\)".*/\1/p' \
    "$ROOT/src/core/Version.h")"
if [[ -z "$VERSION" ]]; then
    echo "无法从 src/core/Version.h 解析出版本号（kAppVersion）" >&2
    exit 1
fi

DIST="$ROOT/dist/LRConvert-$VERSION"
ZIP="$ROOT/dist/LRConvert-$VERSION-win64.zip"

cd "$ROOT"

# windeployqt 与 exe 都要求 PATH 里的 Qt 路径是 MSYS 可转换的形式（/c/...），
# 直接写 C:/... 会原样透传给 Windows 进程导致加载失败。
export PATH="$(cygpath -u "$QT_KIT")/bin:$(cygpath -u "$QT_MINGW")/bin:$PATH"

if [[ "${SKIP_BUILD:-0}" != "1" ]]; then
    echo "[1/6] 构建…"
    ./scripts/build.sh >/dev/null
else
    echo "[1/6] 跳过构建（SKIP_BUILD=1）"
fi

[[ -f "$BUILD_DIR/LRConvert.exe" ]] || { echo "找不到 $BUILD_DIR/LRConvert.exe"; exit 1; }

echo "[2/6] 准备 dist/LRConvert-$VERSION/"
rm -rf "$ROOT/dist"
mkdir -p "$DIST"
cp "$BUILD_DIR/LRConvert.exe" "$DIST/"

echo "[3/6] windeployqt 部署 Qt 运行时…"
windeployqt.exe --release --no-translations --no-system-d3d-compiler \
    --no-opengl-sw "$DIST/LRConvert.exe" >/dev/null

echo "[4/6] 复制 FFmpeg 动态库（放 ffmpeg/ 子目录）…"
mkdir -p "$DIST/ffmpeg"
cp "$ROOT"/vendor/ffmpeg/bin/*.dll "$DIST/ffmpeg/"

echo "[5/6] 附带文档…"
[[ -f "$ROOT/README.md" ]] && cp "$ROOT/README.md" "$DIST/"
cp "$ROOT/LICENSE" "$DIST/"

echo "[6/7] 分发验证（在无 Qt 开发环境的干净 PATH 下冒烟运行）…"
( export PATH="/usr/bin:/c/Windows/System32"
  cd "$DIST" && LRCONVERT_SMOKE_TEST=1 ./LRConvert.exe ) \
    && echo "  ✓ dist 自包含，冒烟通过" \
    || { echo "  ✗ 冒烟失败"; exit 1; }

# 顺带出一份「只有 FFmpeg DLL」的包：给从源码构建的人用
# （仓库里 vendor/ffmpeg 默认不放 DLL，完整包已经自带，这包是补缺口用的）
echo "[7/7] 生成 FFmpeg 纯 DLL 包…"
DLL_NAME="LRConvert-$VERSION-ffmpeg-dll"
DLL_DIST="$ROOT/dist/$DLL_NAME"
DLL_ZIP="$ROOT/dist/$DLL_NAME-win64.zip"
mkdir -p "$DLL_DIST/ffmpeg"
cp "$ROOT"/vendor/ffmpeg/bin/*.dll "$DLL_DIST/ffmpeg/"
cat > "$DLL_DIST/README.md" <<README_EOF
# LRConvert $VERSION —— FFmpeg 运行时 DLL（仅 DLL）

本压缩包只包含 LRConvert v$VERSION 运行所需的 FFmpeg 动态库，
供**从源码构建**的开发者使用（完整程序请下载 LRConvert-$VERSION-win64.zip）。

来源：FFmpeg 9.0.2-gplfull（MinGW-w64 x86_64 构建，GPL-3）。

## 用法

把 ffmpeg/ 整个目录放到 **exe 的同级目录**（不是放到源码根目录）：

    LRConvert-$VERSION/
      LRConvert.exe
      ffmpeg/            ← 放这里
        avcodec-63.dll
        ...

或者不改文件位置，设环境变量指向本目录：

    set LRCONVERT_FFMPEG_DIR=D:/path/to/ffmpeg

## 注意（重要）

- **必须放在 ffmpeg/ 子目录，不要直接放到 exe 同级。**
  这里面有 zlib1.dll / libstdc++-6.dll 等通用运行时库，
  它们会劫持 Qt 的依赖解析，导致进程启动失败（无提示，退出码 127）。
- 若你用 scripts/package.sh 打包，脚本会自动从 vendor/ffmpeg/bin/
  复制 DLL 到发布目录，无需手动操作；本包是给**源码目录里没有这些 DLL**
  的情况准备的。

## 许可

这些 DLL 为 GPL-3（含 libx264 / libx265 等 GPL 组件），
因此链接它们的 LRConvert 同样以 GPL-3 发布。
README_EOF

# 打成 zip：zip 内保留顶层目录 LRConvert-<版本>/，解压后即是完整文件夹
# （GNU tar 的 -a 按扩展名自动选 zip 格式）
echo ""
echo "压缩 zip…"
tar -a -cf "$ZIP" -C "$ROOT/dist" "LRConvert-$VERSION"
echo "  ✓ $ZIP"

# DLL 包单独压缩：GNU tar 的 -a 只打包不压缩（115M 的目录出来 114M），
# 而 DLL 全是二进制、压缩率很高（75M → 25M），能省一半下载体积。
# 完整包里的 Qt DLL + FFmpeg DLL 同理，但它已经发布过两版、体积稳定，
# 先不动，免得改了压缩方式导致校验值全变。
DLL_PY=""
for c in python python3 py; do command -v "$c" >/dev/null 2>&1 && { DLL_PY="$c"; break; }; done
if [[ -n "$DLL_PY" ]]; then
    ( cd "$ROOT/dist" && "$DLL_PY" -c "
import os, sys, zipfile
name = sys.argv[1]
out = name + '-win64.zip'
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, _dirs, files in os.walk(name):
        for f in files:
            p = os.path.join(root, f)
            z.write(p, os.path.relpath(p, '.'))
print('  ✓ ' + os.path.abspath(out))
" "$DLL_NAME" )
else
    tar -a -cf "$DLL_ZIP" -C "$ROOT/dist" "$DLL_NAME"
    echo "  ✓ $DLL_ZIP（未压缩：本机没有 python）"
fi
du -sh "$DIST" "$ZIP" "$DLL_ZIP" 2>/dev/null | sed 's/^/  /'

echo ""
echo "打包完成："
echo "  目录  ：$DIST"
echo "  完整包：$ZIP"
echo "  DLL 包：$DLL_ZIP"
