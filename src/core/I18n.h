// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#pragma once
// ---------------------------------------------------------------------------
// 多语言支持（i18n）
//
// 方案（源码语言 = 中文，默认语言 = 英文）：
//   * 代码里的 tr("中文") 即 source；英文译文放在 translations/LRconvert_en.qm。
//   * .qm 由 CMake 的 qt_add_lrelease 生成，并打进 exe 的 qrc
//     （:/translations/）——默认英文不依赖任何外部文件，qrc 漏不掉。
//   * 同时允许 exe 同级 translations/ 目录里的 .qm **覆盖**内置版本：
//     以后加新语言，把 .qm 扔进那个目录即可（连同登记 languages() 一行），
//     详见 translations/README.md。
//   * 中文是源码语言，不产生 .qm：切到中文 = 卸载程序翻译器
//     （另加载 Qt 自带的 qtbase_zh_CN.qm，让标准对话框按钮也是中文）。
//   * 选择记忆在 QSettings（wbs21/LRConvert → language）。
//
// 线程：只在 GUI 线程调用（translator 的装卸不是线程安全的）。
// ---------------------------------------------------------------------------

#include <QString>
#include <QVector>

class QApplication;

namespace lr::i18n {

/// 一种可切换的语言。id 同时是 .qm 文件名的一段（LRconvert_<id>.qm）。
struct LanguageInfo {
    QString id;          ///< "en" / "zh_CN"
    QString nativeName;  ///< 下拉框里显示的名字（语言的原生名称，不随界面翻译）
};

/// 内置语言表（顺序即下拉框顺序）。加语言时在此登记 + 提供 .qm。
const QVector<LanguageInfo> &languages();

/// 当前生效的语言 id；从未设置过时返回默认语言（英文）。
QString current();

/// 默认语言 id（英文）
inline constexpr const char *kDefaultLanguage = "en";

/// 源码语言 id（中文）：没有自己的 .qm，卸载翻译器即是中文
inline constexpr const char *kSourceLanguage = "zh_CN";

/// 启动时调用：按 QSettings 记忆应用语言（默认英文）。
/// 必须在构造任何窗口之前调用——setupUi 那一刻就要拿到翻译。
bool applySaved(QString *error = nullptr);

/// 切换语言（装卸 translator + 写 QSettings）。
/// 调用方负责随后刷新已显示的界面（MainWindow::retranslate()）。
bool apply(const QString &id, QString *error = nullptr);

/// exe 同级 translations/ 目录（磁盘 .qm 优先于内置 qrc）
QString translationsDir();

}  // namespace lr::i18n
