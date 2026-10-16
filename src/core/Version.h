// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

// ---------------------------------------------------------------------------
// 版本号单一来源（single source of truth）。
//
// 以下位置都从这里取值，避免多处漂移：
//   · CMakeLists.txt       —— project(LRConvert VERSION ...)，用正则解析本文件
//   · scripts/package.sh   —— 发布目录 dist/LRConvert-<版本>/ 与 zip 命名
//   · src/MainWindow.cpp   —— 窗口标题、Tab7「当前版本」
//   · src/main.cpp         —— QApplication::applicationVersion
//
// 注意与仓库根的 version.md 保持一致：应用内的「检查更新」拉取的
// 就是这个文件（格式见 python/version.md：version: 3.4）。
// ---------------------------------------------------------------------------
#ifndef LR_CORE_VERSION_H
#define LR_CORE_VERSION_H

namespace lr {

/// 应用版本号（形如 "4.0"）
inline constexpr const char *kAppVersion = "4.0.2";

}  // namespace lr

#endif  // LR_CORE_VERSION_H
