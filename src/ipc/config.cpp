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
        config.rows_.push_back(line);
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

void Config::set(const std::string& section, const std::string& key,
                 const std::string& value) {
    values_[section + "." + key] = value;
}

bool Config::save(const std::string& path) const {
    // 写回：保留原始行（注释/空行/顺序），替换已存在键的值，末尾追加
    // 缺失的 (section,key)。
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    // 需追加的键：原文件未出现的（按 set 顺序）。
    std::map<std::string, std::string> pending(values_);
    std::string section;
    for (const std::string& line : rows_) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == '#' || trimmed[0] == ';') {
            file << line << '\n';
            continue;
        }
        if (trimmed.front() == '[') {
            const size_t close = trimmed.find(']');
            if (close != std::string::npos) {
                section = trim(trimmed.substr(1, close - 1));
            }
            file << line << '\n';
            continue;
        }
        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            file << line << '\n';
            continue;
        }
        const std::string key = trim(trimmed.substr(0, eq));
        const std::string mapKey = section + "." + key;
        const auto it = pending.find(mapKey);
        if (it != pending.end()) {
            file << key << " = " << it->second << '\n';
            pending.erase(it);
        } else {
            file << line << '\n';
        }
    }
    // 追加缺失的键：按 section 分组（保持首见顺序）。
    std::vector<std::pair<std::string, std::string>> ordered(pending.begin(),
                                                             pending.end());
    std::string lastSection;
    bool wroteSection = false;
    for (const auto& [mapKey, value] : ordered) {
        const size_t dot = mapKey.find('.');
        if (dot == std::string::npos) {
            continue;
        }
        const std::string sec = mapKey.substr(0, dot);
        const std::string key = mapKey.substr(dot + 1);
        if (sec != lastSection) {
            file << (file.tellp() > 0 ? "\n" : "") << "[" << sec << "]\n";
            lastSection = sec;
            wroteSection = true;
        }
        file << key << " = " << value << '\n';
        (void)wroteSection;
    }
    file.close();
    return file.good();
}

}  // namespace w10de
