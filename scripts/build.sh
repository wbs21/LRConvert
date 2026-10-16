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
#   ./scripts/build.sh asan         # ASan + UBSan 构建（P1-7 模糊测试用）
#   BUILD_DIR=xxx JOBS=8 ./scripts/build.sh
# ---------------------------------------------------------------------------
set -euo pipefail

QT_KIT="${QT_KIT:-C:/Qt/6.12.0/mingw_64}"
QT_MINGW="${QT_MINGW:-C:/Qt/Tools/mingw1310_64}"
JOBS="${JOBS:-4}"

MODE="${1:-release}"
case "$MODE" in
    release) BUILD_DIR="${BUILD_DIR:-build}"; EXTRA=() ;;
    clean)   BUILD_DIR="${BUILD_DIR:-build}"; EXTRA=() ;;
    asan)
        # 单独建一个目录：Sanitizer 版的运行库要求不一样，不能污染发布构建。
        # 产物专供 scripts/fuzz_corpus.sh（ASAN_BUILD=build-asan）。
        BUILD_DIR="${BUILD_DIR:-build-asan}"
        EXTRA=(-DLRCONVERT_SANITIZE=ON)
        echo "构建 ASan + UBSan 版本 → $BUILD_DIR（仅供测试，不要打包分发）"
        ;;
    *)
        echo "未知参数：$MODE（可选：release | clean | asan）"
        exit 2
        ;;
esac

cd "$(dirname "$0")/.."

if [[ "$MODE" == "clean" ]]; then
    echo "清理 ${BUILD_DIR}/"
    rm -rf "$BUILD_DIR"
fi

if ! command -v cmake >/dev/null 2>&1; then
    # Qt Creator 自带的 CMake 常常不在 PATH 上（本机的实际情况）
    CMAKE_BIN="${CMAKE_BIN:-C:/Qt/Tools/CMake_64/bin/cmake.exe}"
    [[ -x "$CMAKE_BIN" ]] || { echo "找不到 cmake，请装 CMake 或设 CMAKE_BIN="; exit 1; }
else
    CMAKE_BIN="cmake"
fi

"$CMAKE_BIN" -B "$BUILD_DIR" -G "MinGW Makefiles" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_KIT" \
    -DCMAKE_CXX_COMPILER="$QT_MINGW/bin/g++.exe" \
    -DCMAKE_C_COMPILER="$QT_MINGW/bin/gcc.exe" \
    "${EXTRA[@]}"

"$CMAKE_BIN" --build "$BUILD_DIR" -j"$JOBS"

echo ""
echo "构建完成（$MODE）："
echo "  $(pwd)/$BUILD_DIR/LRConvert.exe           主程序"
echo "  $(pwd)/$BUILD_DIR/capability_check.exe    能力清单工具（不依赖 Qt）"
echo "  $(pwd)/$BUILD_DIR/remux_check.exe         流拷贝管线验证工具（无界面）"
echo "  $(pwd)/$BUILD_DIR/transcode_check.exe     转码管线验证工具（无界面）"
echo ""
echo "运行：./scripts/run.sh"
echo "打包分发：./scripts/package.sh"
echo "一键回归（与 ffmpeg CLI 对照）：./scripts/regression.sh"
echo "损坏文件模糊测试：./scripts/fuzz_corpus.sh"
echo ""
echo "流拷贝功能自检："
echo "  ./$BUILD_DIR/remux_check.exe cut    <输入> <输出> <起始秒> <结束秒>"
echo "  ./$BUILD_DIR/remux_check.exe video  <输入> <输出>"
echo "  ./$BUILD_DIR/remux_check.exe audio  <输入> <输出>"
echo "  ./$BUILD_DIR/remux_check.exe merge  <视频> <音频> <输出>"
echo "  ./$BUILD_DIR/remux_check.exe concat <输出> <输入1> <输入2> ..."
echo ""
echo "转码功能自检："
echo "  ./$BUILD_DIR/transcode_check.exe <输入> <输出> [编码器] [视频kbps] [缩放] [音频kbps]"
echo ""
echo "崩溃转储自检（故意向地址 0 写一次，用来验证「写 .dmp + 落日志」这条链路）："
echo "  LRCONVERT_CRASH_TEST=1 ./scripts/run.sh"
echo "崩溃后安全模式自检（跑一次上面的自检，再正常启动一次，应看到进入安全模式）："
echo "  ./scripts/run.sh   # 日志里出现 [P1-8] 即为正常"
echo "gui 卡死看门狗自检：长时间无响应 15 秒后自动写 dump（见 docs/稳定性排查与加固.md）"
