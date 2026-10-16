// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "core/I18n.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QSettings>
#include <QTranslator>

namespace lr::i18n {
namespace {

// 与 main.cpp 里的 setOrganizationName / setApplicationName 保持一致
const QString kSettingsOrg = QStringLiteral("wbs21");
const QString kSettingsApp = QStringLiteral("LRConvert");
const QString kSettingsKey = QStringLiteral("language");

QTranslator *g_appTr = nullptr;  ///< 程序自己的字符串（LRconvert_<id>.qm）
QTranslator *g_qtTr = nullptr;   ///< Qt 标准对话框（qtbase_zh_CN.qm，仅中文加载）

bool load(QTranslator *&slot, const QString &path) {
    auto *translator = new QTranslator();
    if (!translator->load(path)) {
        delete translator;
        return false;
    }
    qApp->installTranslator(translator);
    slot = translator;
    return true;
}

void unloadAll() {
    if (g_appTr != nullptr) {
        qApp->removeTranslator(g_appTr);
        delete g_appTr;
        g_appTr = nullptr;
    }
    if (g_qtTr != nullptr) {
        qApp->removeTranslator(g_qtTr);
        delete g_qtTr;
        g_qtTr = nullptr;
    }
}

/// 磁盘优先、内置 qrc 兜底：前者让"以后加语言"可以不重新编译，
/// 后者保证默认英文在任何情况下都拿得到译文。
bool loadAppTranslation(const QString &id) {
    const QString disk =
        translationsDir() + QStringLiteral("/LRconvert_%1.qm").arg(id);
    const QString builtin =
        QStringLiteral(":/translations/LRconvert_%1.qm").arg(id);
    if (QFileInfo::exists(disk) && load(g_appTr, disk)) return true;
    if (QFileInfo::exists(builtin) && load(g_appTr, builtin)) return true;
    return false;
}

/// Qt 自己的文案（QMessageBox 按钮、QFileDialog 文字等）。
/// 只在中文模式下需要——英文就是 Qt 的源码语言。
bool loadQtChineseTranslation() {
    const QString disk =
        translationsDir() + QStringLiteral("/qtbase_zh_CN.qm");
    const QString installed = QLibraryInfo::path(QLibraryInfo::TranslationsPath) +
                              QStringLiteral("/qtbase_zh_CN.qm");
    if (QFileInfo::exists(disk)) return load(g_qtTr, disk);
    if (QFileInfo::exists(installed)) return load(g_qtTr, installed);
    return false;
}

}  // namespace

const QVector<LanguageInfo> &languages() {
    static const QVector<LanguageInfo> kLanguages = {
        {QStringLiteral("en"), QStringLiteral("English")},
        {QStringLiteral("zh_CN"), QStringLiteral("中文")},
    };
    return kLanguages;
}

QString current() {
    const QSettings settings(kSettingsOrg, kSettingsApp);
    return settings.value(kSettingsKey, QString::fromUtf8(kDefaultLanguage))
        .toString();
}

QString translationsDir() {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
}

bool apply(const QString &id, QString *error) {
    // 合法身份：内置表里有，或者磁盘 translations/ 里有对应的 .qm
    // （后者让新语言可以不改代码，见 translations/README.md）
    bool known = false;
    for (const LanguageInfo &lang : languages()) {
        if (lang.id == id) {
            known = true;
            break;
        }
    }
    if (!known) {
        const QString disk =
            translationsDir() + QStringLiteral("/LRconvert_%1.qm").arg(id);
        if (!QFileInfo::exists(disk)) {
            if (error != nullptr) {
                *error = QCoreApplication::translate(
                    "lr::i18n", "未知的语言：%1").arg(id);
            }
            return false;
        }
    }

    unloadAll();

    // 1) 程序自身翻译。中文是源码语言，没有 .qm 也不算错（卸载即中文）；
    //    其它语言找不到 .qm 是实打实的故障，必须报出来。
    if (!loadAppTranslation(id) && id != QLatin1String(kSourceLanguage)) {
        if (error != nullptr) {
            *error = QCoreApplication::translate(
                         "lr::i18n",
                         "找不到语言文件 LRconvert_%1.qm。"
                         "它应随程序发布，或放在 exe 同级的 translations/ 目录。")
                         .arg(id);
        }
        return false;
    }

    // 2) Qt 自身文案：仅中文模式加载；找不到就跳过（不影响程序自己的翻译）
    if (id == QLatin1String(kSourceLanguage)) loadQtChineseTranslation();

    QSettings settings(kSettingsOrg, kSettingsApp);
    settings.setValue(kSettingsKey, id);
    return true;
}

bool applySaved(QString *error) {
    return apply(current(), error);
}

}  // namespace lr::i18n
