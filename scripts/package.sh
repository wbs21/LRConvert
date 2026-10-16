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
#   ├── translations/          ← 语言文件（LRconvert_en.qm + qtbase_zh_CN.qm）
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
DLL_NAME="LRConvert-$VERSION-ffmpeg-dll"
DLL_DIST="$ROOT/dist/$DLL_NAME"
DLL_ZIP="$ROOT/dist/$DLL_NAME-win64.zip"

cd "$ROOT"

# ---- 旧产物"退休"而不是删除 ---------------------------------------------
#
# 这里原来是 `rm -rf "$ROOT/dist"`，一巴掌把 dist/ 下**所有**历史版本全清掉 ——
# 包括上一次已经发出去的那个包。重打包一次就等于销毁一次回滚能力，
# 出问题只能从头再来一遍（windeployqt + 75MB DLL 复制，几分钟）。
#
# 改成：同名的旧目录挪到 dist_<名字>-<时间戳>/ 留一版（.gitignore 里的
# dist_*/ 规则本来就是为这个准备的），一次也不删。磁盘不够了自己清。
STAMP="$(date +%Y%m%d-%H%M%S)"
retire() {
    local dir="$1"
    [[ -d "$dir" ]] || return 0
    local dest="$ROOT/dist_$(basename "$dir")-$STAMP"
    mv "$dir" "$dest"
    echo "  旧产物已退休 → $(basename "$dest")/（需要时可直接换回来）"
}

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
mkdir -p "$ROOT/dist"
retire "$DIST"
retire "$DLL_DIST"
mkdir -p "$DIST"
cp "$BUILD_DIR/LRConvert.exe" "$DIST/"

echo "[3/6] windeployqt 部署 Qt 运行时…"
windeployqt.exe --release --no-translations --no-system-d3d-compiler \
    --no-opengl-sw "$DIST/LRConvert.exe" >/dev/null

echo "[4/6] 复制 FFmpeg 动态库（放 ffmpeg/ 子目录）…"
mkdir -p "$DIST/ffmpeg"
cp "$ROOT"/vendor/ffmpeg/bin/*.dll "$DIST/ffmpeg/"

echo "[5/6] 附带文档与语言文件…"
[[ -f "$ROOT/README.md" ]] && cp "$ROOT/README.md" "$DIST/"
[[ -f "$ROOT/CHANGELOG.md" ]] && cp "$ROOT/CHANGELOG.md" "$DIST/"
cp "$ROOT/LICENSE" "$DIST/"
# 语言文件：我们自己的 .qm（lrelease 产物）+ Qt 的 zh_CN 翻译
# （中文模式下加载，让标准对话框按钮也是中文；缺失不影响程序自身翻译）。
# 运行时 exe 同级的 translations/ 优先于 exe 内置的 qrc 版本。
mkdir -p "$DIST/translations"
cp "$BUILD_DIR"/i18n/LRconvert_*.qm "$DIST/translations/" 2>/dev/null || \
    { echo "找不到 $BUILD_DIR/i18n/*.qm（先构建）"; exit 1; }
cp "$QT_KIT/translations/qtbase_zh_CN.qm" "$DIST/translations/" 2>/dev/null || \
    echo "  （未找到 Qt 的 qtbase_zh_CN.qm，跳过；仅影响标准对话框按钮语言）"

echo "[6/6] 分发验证（在无 Qt 开发环境的干净 PATH 下冒烟运行）…"
( export PATH="/usr/bin:/c/Windows/System32"
  cd "$DIST" && LRCONVERT_SMOKE_TEST=1 ./LRConvert.exe ) \
    && echo "  ✓ dist 自包含，冒烟通过" \
    || { echo "  ✗ 冒烟失败"; exit 1; }

# 刚才那次冒烟会在 dist 里留下 logs/（本机运行日志 + crashdumps/clean-exit.marker）。
# 不能跟着发出去：一是带着打包机器的日志，二是那个"干净退出标记"属于本机，
# 用户第一次启动时的安全模式判定应该从零开始。压 zip 之前清掉。
rm -rf "$DIST/logs"

# 顺带出一份「只有 FFmpeg DLL」的包：给从源码构建的人用
# （仓库里 vendor/ffmpeg 默认不放 DLL，完整包已经自带，这包是补缺口用的）
echo "[7/7] 生成 FFmpeg 纯 DLL 包…（附加产物）"
mkdir -p "$DLL_DIST/ffmpeg"
cp "$ROOT"/vendor/ffmpeg/bin/*.dll "$DLL_DIST/ffmpeg/"
cat > "$DLL_DIST/README.md" <<README_EOF
# LRConvert $VERSION — FFmpeg runtime DLLs (DLLs only)

This archive contains only the FFmpeg dynamic libraries required to run
LRConvert v$VERSION. It is meant for developers **building from source**
(for the complete program, download LRConvert-$VERSION-win64.zip).

Source: FFmpeg 9.0.2-gplfull (MinGW-w64 x86_64 build, GPL-3).

## Usage

Put the whole ffmpeg/ directory **next to the exe** (not in the source root):

    LRConvert-$VERSION/
      LRConvert.exe
      ffmpeg/            ← put it here
        avcodec-63.dll
        ...

Or leave the files where they are and point an environment variable here:

    set LRCONVERT_FFMPEG_DIR=D:/path/to/ffmpeg

## Important

- **It must go in the ffmpeg/ subdirectory, never directly next to the exe.**
  The set includes common runtime libraries such as zlib1.dll and
  libstdc++-6.dll. Placed next to the exe they hijack Qt's dependency
  resolution and the process fails to start (silently, exit code 127).
- If you package with scripts/package.sh, the script copies the DLLs from
  vendor/ffmpeg/bin/ into the release directory automatically; this archive
  is only for the case where **your source tree does not have those DLLs**.

## License

These DLLs are GPL-3 (including libx264 / libx265 and other GPL components),
so LRConvert, which links them, is likewise released under GPL-3.
README_EOF

# ---- 压缩 -----------------------------------------------------------------
#
# 这里原来是 `tar -a -cf xxx.zip ...`，指望 GNU tar 的 -a 按扩展名自动选
# zip 格式。实测在 MSYS2/Git-Bash 的 GNU tar 1.35 上它**不生效**：产出的
# 是个纯 tar 存档，文件头是 "LRConvert-4.0.2/" 而不是 "PK" —— 扩展名写着
# .zip，内容却不是，Windows 资源管理器打不开（v4.0 / v4.0.1 两版发布包
# 都中招了）。
#
# 改成统一用 python 的 zipfile 打：真 zip、zlib 压缩（115M → 40M 上下）。
# 两个包共用同一段，也就不存在"谁压谁没压"的差别了。
PY=""
for c in python python3 py; do command -v "$c" >/dev/null 2>&1 && { PY="$c"; break; }; done
if [[ -z "$PY" ]]; then
    echo "打 zip 需要 python（zipfile）；本机 PATH 里没有，中止 ——" >&2
    echo "与其产出一个打不开的假 zip，不如停下来。" >&2
    exit 1
fi
echo ""
echo "压缩 zip…"
make_zip() {
    ( cd "$ROOT/dist" && "$PY" -c "
import os, sys, zipfile
name, out = sys.argv[1], sys.argv[2]
before = 0
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for root, _dirs, files in os.walk(name):
        for f in files:
            p = os.path.join(root, f)
            before += os.path.getsize(p)
            z.write(p, os.path.relpath(p, '.'))
after = os.path.getsize(out)
print('  ✓ %s  (%d MB → %d MB)' % (out, before >> 20, after >> 20))
" "$1" "$2" )
}
make_zip "LRConvert-$VERSION" "$ZIP"
make_zip "$DLL_NAME" "$DLL_ZIP"

# 两个产物的 sha256：发布页要贴，用户下完能自己对
echo ""
echo "sha256（发布页贴这个）："
( cd "$ROOT/dist" && sha256sum "$(basename "$ZIP")" "$(basename "$DLL_ZIP")" | sed 's/^/  /' )

echo ""
echo "打包完成："
echo "  目录  ：$DIST"
echo "  完整包：$ZIP"
echo "  DLL 包：$DLL_ZIP"
