#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 构建 LRConvert（Qt6 MinGW 版）
#
# 关键点：必须用 **Qt 自带的 MinGW 编译器**（Qt/Tools/mingw1310_64），
# 而不是系统里更新的 g++ —— Qt 官方 MinGW 包是用 GCC 13.1 编的，
# 混用不同 GCC 大版本的 libstdc++ 容易在运行时炸。
#
# 用法：
#   ./scripts/build.sh              # Release 构建
#   ./scripts/build.sh clean        # 清掉 build 目录重新配置
# ---------------------------------------------------------------------------
set -euo pipefail

QT_KIT="${QT_KIT:-C:/Qt/6.12.0/mingw_64}"
QT_MINGW="${QT_MINGW:-C:/Qt/Tools/mingw1310_64}"
BUILD_DIR="${BUILD_DIR:-build}"
JOBS="${JOBS:-4}"

cd "$(dirname "$0")/.."

if [[ "${1:-}" == "clean" ]]; then
    echo "清理 ${BUILD_DIR}/"
    rm -rf "$BUILD_DIR"
fi

cmake -B "$BUILD_DIR" -G "MinGW Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_KIT" \
    -DCMAKE_CXX_COMPILER="$QT_MINGW/bin/g++.exe" \
    -DCMAKE_C_COMPILER="$QT_MINGW/bin/gcc.exe"

cmake --build "$BUILD_DIR" -j"$JOBS"

echo ""
echo "构建完成："
echo "  $(pwd)/$BUILD_DIR/LRConvert.exe           主程序"
echo "  $(pwd)/$BUILD_DIR/capability_check.exe    能力清单工具（不依赖 Qt）"
echo "  $(pwd)/$BUILD_DIR/remux_check.exe         流拷贝管线验证工具（无界面）"
echo "  $(pwd)/$BUILD_DIR/transcode_check.exe     转码管线验证工具（无界面）"
echo ""
echo "运行：./scripts/run.sh"
echo "打包分发：./scripts/package.sh"
echo "一键回归（与 ffmpeg CLI 对照）：./scripts/regression.sh"
echo ""
echo "流拷贝功能自检："
echo "  ./build/remux_check.exe cut    <输入> <输出> <起始秒> <结束秒>"
echo "  ./build/remux_check.exe video  <输入> <输出>"
echo "  ./build/remux_check.exe audio  <输入> <输出>"
echo "  ./build/remux_check.exe merge  <视频> <音频> <输出>"
echo "  ./build/remux_check.exe concat <输出> <输入1> <输入2> ..."
echo ""
echo "转码功能自检："
echo "  ./build/transcode_check.exe <输入> <输出> [编码器] [视频kbps] [缩放] [音频kbps]"
