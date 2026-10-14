#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 打包 LRConvert 为可分发目录 dist/LRConvert-<版本>/ 及 zip
#
# 版本号从 src/core/Version.h 解析（单一来源，与 version.md 一致），
# 产物：dist/LRConvert-4.0/ 与 dist/LRConvert-4.0-win64.zip。
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

echo "[6/6] 分发验证（在无 Qt 开发环境的干净 PATH 下冒烟运行）…"
( export PATH="/usr/bin:/c/Windows/System32"
  cd "$DIST" && LRCONVERT_SMOKE_TEST=1 ./LRConvert.exe ) \
    && echo "  ✓ dist 自包含，冒烟通过" \
    || { echo "  ✗ 冒烟失败"; exit 1; }

# 打成 zip：zip 内保留顶层目录 LRConvert-<版本>/，解压后即是完整文件夹
# （GNU tar 的 -a 按扩展名自动选 zip 格式）
echo ""
echo "压缩 zip…"
tar -a -cf "$ZIP" -C "$ROOT/dist" "LRConvert-$VERSION"
echo "  ✓ $ZIP"
du -sh "$DIST" "$ZIP" 2>/dev/null | sed 's/^/  /'

echo ""
echo "打包完成："
echo "  目录：$DIST"
echo "  zip ：$ZIP"
