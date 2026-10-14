// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 wbs21

#include "TimeUtil.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace lr::timeutil {
namespace {

constexpr long long kAvTimeBase = 1000000LL;

/// 解析 "123" / "123.456" 这类纯数字（含可选前导 '-'）
bool parsePlainNumber(const std::string &s, double &out) {
    if (s.empty()) return false;
    size_t i = 0;
    bool negative = false;
    if (s[i] == '-') {
        negative = true;
        ++i;
    }
    bool anyDigit = false;
    double value = 0.0;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
        value = value * 10 + (s[i] - '0');
        anyDigit = true;
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        double scale = 0.1;
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
            value += (s[i] - '0') * scale;
            scale *= 0.1;
        }
    }
    if (!anyDigit || i != s.size()) return false;
    out = negative ? -value : value;
    return true;
}

}  // namespace

std::optional<double> parseTimestamp(const std::string &text) {
    // 去掉首尾空白与负号（负数一律视为非法，与 Python 版一致）
    size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return std::nullopt;
    size_t end = text.find_last_not_of(" \t\r\n");
    std::string token = text.substr(begin, end - begin + 1);
    if (!token.empty() && token.front() == '-') token.erase(token.begin());

    double value = 0.0;
    if (parsePlainNumber(token, value)) {
        return value >= 0 ? std::optional<double>(value) : std::nullopt;
    }

    // 冒号分隔：HH:MM:SS[.mmm] 或 MM:SS[.mmm]
    int colons = 0;
    for (char c : token) {
        if (c == ':') ++colons;
    }
    if (colons == 0 || colons > 2) return std::nullopt;

    std::string rest = token;
    int hours = 0;
    if (colons == 2) {
        auto pos = rest.find(':');
        int h = 0;
        for (char c : rest.substr(0, pos)) {
            if (c < '0' || c > '9') return std::nullopt;
            h = h * 10 + (c - '0');
        }
        hours = h;
        rest = rest.substr(pos + 1);
    }

    auto pos = rest.find(':');
    if (pos == std::string::npos) return std::nullopt;
    int minutes = 0;
    for (char c : rest.substr(0, pos)) {
        if (c < '0' || c > '9') return std::nullopt;
        minutes = minutes * 10 + (c - '0');
    }
    if (minutes > 59) return std::nullopt;
    rest = rest.substr(pos + 1);

    // 秒部分允许小数点（. 或 ,）
    double seconds = 0.0;
    auto dot = rest.find_first_of(".,");
    if (dot == std::string::npos) {
        int s = 0;
        if (rest.empty()) return std::nullopt;
        for (char c : rest) {
            if (c < '0' || c > '9') return std::nullopt;
            s = s * 10 + (c - '0');
        }
        if (s > 59) return std::nullopt;
        seconds = s;
    } else {
        int s = 0;
        std::string intPart = rest.substr(0, dot);
        if (intPart.empty()) return std::nullopt;
        for (char c : intPart) {
            if (c < '0' || c > '9') return std::nullopt;
            s = s * 10 + (c - '0');
        }
        if (s > 59) return std::nullopt;
        seconds = s;
        std::string frac = rest.substr(dot + 1);
        if (frac.length() > 6) frac = frac.substr(0, 6);
        double scale = 0.1;
        for (char c : frac) {
            if (c < '0' || c > '9') return std::nullopt;
            seconds += (c - '0') * scale;
            scale *= 0.1;
        }
    }

    return hours * 3600.0 + minutes * 60.0 + seconds;
}

std::string formatTimestamp(double seconds) {
    if (!(seconds >= 0) || !std::isfinite(seconds)) return "--:--:--";
    long long total = static_cast<long long>(seconds);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld",
                  total / 3600, (total % 3600) / 60, total % 60);
    return buf;
}

std::string formatTimestamp(std::optional<double> seconds) {
    return seconds ? formatTimestamp(*seconds) : std::string("--:--:--");
}

long long toAvTime(double seconds) {
    return static_cast<long long>(seconds * kAvTimeBase);
}

double fromAvTime(long long value) {
    return static_cast<double>(value) / static_cast<double>(kAvTimeBase);
}

}  // namespace lr::timeutil
