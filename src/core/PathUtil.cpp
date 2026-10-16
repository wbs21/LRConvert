// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "PathUtil.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>

namespace lr::pathutil {
namespace fs = std::filesystem;

const std::vector<std::string> &videoExtensions() {
    static const std::vector<std::string> kExts = {
        ".mp4", ".m4v", ".mkv", ".mts", ".avi", ".mov",  ".mpg",  ".flv",
        ".dat", ".wmv", ".rm",  ".rmvb", ".mpeg", ".3gp", ".mxf", ".m4s",
    };
    return kExts;
}

std::string videoFilterPattern() {
    return "*.mp4 *.m4v *.mkv *.mts *.avi *.mov *.mpg *.flv *.dat *.wmv "
           "*.rm *.rmvb *.mpeg *.3gp *.mxf *.m4s";
}

std::string audioFilterPattern() {
    return "*.aac *.wav *.mp3 *.ac3 *.m4a *.mov *.m4s";
}

const NameValues &scaleFilters() {
    // 键是规范名（与语言无关）；显示名的本地化在界面层做
    static const NameValues kFilters = {
        {"original", "scale=trunc(iw/2)*2:trunc(ih/2)*2"},
        {"half", "scale=trunc(iw/4)*2:trunc(ih/4)*2"},
        {"quarter", "scale=trunc(iw/8)*2:trunc(ih/8)*2"},
        {"1920*1080", "scale=1920:1080"},
        {"1280*720", "scale=1280:720"},
    };
    return kFilters;
}

const NameValues &hwEncoders() {
    static const NameValues kEncoders = {
        {"libx264", "libx264"},
        {"nvenc", "h264_nvenc"},
        {"qsv", "h264_qsv"},
        {"amf", "h264_amf"},
        {"videotoolbox", "h264_videotoolbox"},
    };
    return kEncoders;
}

const NameValues &softwareEncoders() {
    // 从 hwEncoders() 过滤，保证"哪些是软件编码器"只有一个信息来源
    static const NameValues kSoftware = [] {
        NameValues v;
        for (const auto &kv : hwEncoders()) {
            if (kv.second == "libx264") v.push_back(kv);
        }
        return v;
    }();
    return kSoftware;
}

std::string lookup(const NameValues &table, const std::string &displayName,
                   const std::string &fallback) {
    for (const auto &kv : table) {
        if (kv.first == displayName) return kv.second;
    }
    return fallback;
}

std::string extensionOf(const std::string &path) {
    fs::path p(path);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext;
}

std::string stemOf(const std::string &path) {
    return fs::path(path).stem().string();
}

std::string atomicTempPath(const std::string &output) {
    if (output.empty()) return {};
    const fs::path p(output);
    const std::string ext = p.extension().string();
    fs::path stem = p.stem();
    stem += L".LRConvert-tmp";

    fs::path result = p.parent_path() / stem;
    result += ext;  // 后缀原样保留，理由见头文件注释（movflags +faststart）
    return result.string();
}

std::string parentOf(const std::string &path) {
    std::error_code ec;
    fs::path dir = fs::path(path).parent_path();
    if (dir.empty()) return {};
    return dir.string();
}

std::string stamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d%02d%02d%02d%02d",
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

std::string uniquePath(const std::string &path) {
    std::error_code ec;
    if (!fs::exists(fs::path(path), ec)) return path;

    fs::path p(path);
    std::string stem = p.stem().string();
    std::string ext = p.extension().string();
    for (int index = 1; index < 1000; ++index) {
        fs::path candidate =
            p.parent_path() / (stem + "(" + std::to_string(index) + ")" + ext);
        if (!fs::exists(candidate, ec)) return candidate.string();
    }
    return path;
}

std::string copySafeSuffix(const std::string &source) {
    std::string suffix = extensionOf(source);
    if (suffix == ".m4s" || suffix.empty()) return ".mp4";
    return suffix;
}

std::string outputPath(const std::string &source, const std::string &suffix,
                       const std::string &tag, const std::string &stampText) {
    fs::path p(source);
    const std::string st = stampText.empty() ? stamp() : stampText;

    std::string ext = suffix;
    if (!ext.empty() && ext.front() != '.') ext = "." + ext;

    const std::string name = p.stem().string() + tag + "_new_" + st + ext;
    return uniquePath((p.parent_path() / name).string());
}

std::string concatListContent(const std::vector<std::string> &files) {
    std::string out;
    for (const auto &file : files) {
        out += "file '";
        for (char c : file) {
            if (c == '\'') {
                out += "'\\''";
            } else {
                out += c;
            }
        }
        out += "'\n";
    }
    return out;
}

bool sameFilePath(const std::string &a, const std::string &b) {
    if (a.empty() || b.empty()) return false;

    // 真实判定：两个文件都存在时，equivalent() 能识破硬链接/不同写法
    std::error_code ec;
    if (fs::exists(a, ec) && fs::exists(b, ec) && fs::equivalent(a, b, ec)) {
        return true;
    }

    // 退回字符串比较：规范化（统一分隔符、去掉 ./ ../）后大小写不敏感
    // （Windows 文件系统不区分大小写）
    auto normalize = [](const std::string &p) {
        std::string s = p;
        std::replace(s.begin(), s.end(), '/', '\\');
        const fs::path path = fs::path(s).lexically_normal();
        std::wstring w = path.wstring();
        for (auto &c : w) c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
        return w;
    };
    return normalize(a) == normalize(b);
}

}  // namespace lr::pathutil
