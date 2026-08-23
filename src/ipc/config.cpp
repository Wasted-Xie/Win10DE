#include "ipc/config.h"

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace w10de {

namespace {

// 去除首尾空白。
std::string trim(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

}  // namespace

Config Config::load(const std::string& path) {
    Config config;
    std::ifstream file(path);
    if (!file.is_open()) {
        return config;  // 空配置（文件不存在是常见情况）。
    }
    std::string section;
    std::string line;
    while (std::getline(file, line)) {
        // 去注释（# 或 ; 开头；行内注释不支持，避免路径含 # 出错）。
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        if (trimmed.front() == '[') {
            const size_t close = trimmed.find(']');
            if (close != std::string::npos) {
                section = trim(trimmed.substr(1, close - 1));
            }
            continue;
        }
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = trim(trimmed.substr(0, eq));
        const std::string value = trim(trimmed.substr(eq + 1));
        if (key.empty()) {
            continue;
        }
        config.values_[section + "." + key] = value;
    }
    return config;
}

std::string Config::get(const std::string& section, const std::string& key,
                        const std::string& fallback) const {
    const auto it = values_.find(section + "." + key);
    return it != values_.end() ? it->second : fallback;
}

int Config::getInt(const std::string& section, const std::string& key,
                   int fallback) const {
    const std::string value = get(section, key);
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    errno = 0;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || errno == ERANGE ||
            parsed > INT_MAX || parsed < INT_MIN) {
        return fallback;
    }
    return static_cast<int>(parsed);
}

}  // namespace w10de
