// 极简 INI 配置解析（无外部依赖，compositor/shell 共用）。
//
// 格式：`[section]` + `key=value`，`#`/`;` 行注释。
// 访问：get("section", "key")。文件不存在时为空配置。
#pragma once

#include <map>
#include <string>

namespace w10de {

class Config {
public:
    // 加载文件（不存在/不可读时返回空配置）。
    static Config load(const std::string& path);

    // section.key 的值；不存在返回 fallback。
    std::string get(const std::string& section, const std::string& key,
                    const std::string& fallback = std::string()) const;
    // section.key 的整数值；不存在或非法返回 fallback。
    int getInt(const std::string& section, const std::string& key, int fallback) const;

private:
    // 键："section.key" → 值。
    std::map<std::string, std::string> values_;
};

}  // namespace w10de
