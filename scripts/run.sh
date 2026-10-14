#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# 从构建目录运行 LRConvert（Qt 的 DLL 与插件尚未部署到 build/，
# 所以这里临时把它们加进 PATH / QT_PLUGIN_PATH）。
#
# 正式发布时改用：windeployqt build/LRConvert.exe
#
# 环境变量：
#   LRCONVERT_SMOKE_TEST=1   跑 6 秒后自动退出（脚本/CI 冒烟验证用）
#   QT_FORCE_STDERR_LOGGING=1 把 qInfo 诊断输出到 stderr（Windows 默认不输出）
# ---------------------------------------------------------------------------
set -euo pipefail

QT_KIT="${QT_KIT:-C:/Qt/6.12.0/mingw_64}"
QT_MINGW="${QT_MINGW:-C:/Qt/Tools/mingw1310_64}"
BUILD_DIR="${BUILD_DIR:-build}"

cd "$(dirname "$0")/.."

# Windows 控制台默认 GBK 代码页，Qt 日志是 UTF-8，直接看会乱码
cmd //c "chcp 65001" >/dev/null 2>&1 || true

# 注意：PATH 必须用 MSYS 能转换成 Windows 形式的路径（/c/... 风格）。
# 直接写 C:/Qt/... 的话 Git Bash 会原样透传给 Windows 进程，
# LoadLibrary 解析 PATH 条目时失败（退出码 127，提示找不到 Qt6Core.dll）。
export PATH="$(cygpath -u "$QT_KIT")/bin:$(cygpath -u "$QT_MINGW")/bin:$PATH"
export QT_PLUGIN_PATH="$QT_KIT/plugins"
export QT_FORCE_STDERR_LOGGING=1

exec "$BUILD_DIR/LRConvert.exe"
