#include "ipc/config.h"

#include <cctype>
#include <cerrno>
#include <climits>
#include <cstdio>   // rename/remove（save 原子替换）
#include <cstdlib>
#include <fstream>
#include <sstream>

#include <unistd.h>  // getpid（tmp 名唯一，审查 L5：多进程并发保存不互踩）

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
    std::ifstream file(path);    if (!file.is_open()) {
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
        config.originalKeys_.insert(section + "." + key);
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

void Config::remove(const std::string& section, const std::string& key) {
    const std::string mapKey = section + "." + key;
    const auto it = values_.find(mapKey);
    if (it == values_.end()) {
        return;  // 键不存在：无操作
    }
    values_.erase(it);
    removedKeys_.insert(mapKey);
}

std::vector<std::string> Config::sectionKeys(
        const std::string& section) const {
    const std::string prefix = section + ".";
    std::vector<std::string> keys;
    for (const auto& kv : values_) {
        const std::string& mapKey = kv.first;
        if (mapKey.size() > prefix.size() &&
                mapKey.compare(0, prefix.size(), prefix) == 0) {
            keys.push_back(mapKey.substr(prefix.size()));
        }
    }
    return keys;
}

bool Config::save(const std::string& path) const {
    // 写回：保留原始行（注释/空行/顺序），替换已存在键的值，末尾追加
    // 缺失的 (section,key)。
    // 审查 L5（G1）：tmp 名带 pid——w10settings/w10control 并发保存同一
    // config 时互不覆盖（固定 .tmp 名会互踩）。
    const std::string tmp = path + ".tmp." + std::to_string(getpid());
    std::ofstream file(tmp, std::ios::trunc);
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
        } else if (removedKeys_.count(mapKey) == 0) {
            // 未修改且未被 remove 的键：保留原行。
            file << line << '\n';
        }
        // removedKeys_ 中的键：跳过原行（删除）。
    }
    // 追加缺失的键：按 section 分组。
    // 审查 M1（G1）：新键**插入到对应段头之后**（非文件末尾）——段头在
    // 原文件已存在时不重复写（否则删除部分键+新增键的 save 会累积重复段头，
    // 且键追加到末尾会归错段）。从未出现的新段才写段头。
    std::map<std::string, std::vector<std::pair<std::string, std::string>>>
        pendingBySection;
    for (const auto& kv : pending) {
        const std::string& mapKey = kv.first;
        if (originalKeys_.count(mapKey) != 0) {
            continue;  // 原文件已有：行循环内原位替换（避免段头处重复输出）
        }
        const size_t dot = mapKey.find('.');
        if (dot == std::string::npos) {
            continue;
        }
        pendingBySection[mapKey.substr(0, dot)].emplace_back(
            mapKey.substr(dot + 1), kv.second);
    }
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
            // 该段的缺失键插入段头之后（保持同段内集中）。
            const auto it = pendingBySection.find(section);
            if (it != pendingBySection.end()) {
                for (const auto& keyValue : it->second) {
                    file << keyValue.first << " = " << keyValue.second << '\n';
                }
                pendingBySection.erase(it);
            }
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
        } else if (removedKeys_.count(mapKey) == 0) {
            // 未修改且未被 remove 的键：保留原行。
            file << line << '\n';
        }
        // removedKeys_ 中的键：跳过原行（删除）。
    }
    // 剩余 pending：原文件从未出现的段，写段头后输出。
    for (const auto& sectionKeys : pendingBySection) {
        file << (file.tellp() > 0 ? "\n" : "")
             << "[" << sectionKeys.first << "]\n";
        for (const auto& keyValue : sectionKeys.second) {
            file << keyValue.first << " = " << keyValue.second << '\n';
        }
    }
    file.close();
    if (!file.good()) {
        std::remove(tmp.c_str());
        return false;
    }
    // 原子替换（POSIX rename 对已存在目标直接覆盖）。
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace w10de
