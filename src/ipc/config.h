// 极简 INI 配置解析（无外部依赖，compositor/shell 共用）。
//
// 格式：`[section]` + `key=value`，`#`/`;` 行注释。
// 访问：get("section", "key")。文件不存在时为空配置。
// 写入：set() 改内存 + save() 写回（保留原注释/空行/顺序；未出现的键
// 追加到对应 section 末尾）。设置应用（w10settings）用它保存配置。
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

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

    // 设置 section.key（内存中；save 前不落盘）。
    void set(const std::string& section, const std::string& key,
             const std::string& value);
    // 删除 section.key（内存中；save 时该键的原行一并移除）。
    // 键不存在时无操作。供窗口规则页删除规则用。
    void remove(const std::string& section, const std::string& key);
    // 该 section 下的全部键名（不含 section 前缀；按 map 序，即键名字典序）。
    // 供窗口规则页枚举段内规则用。
    std::vector<std::string> sectionKeys(const std::string& section) const;
    // 写回文件：保留原始注释/空行/顺序，替换已存在的键，追加缺失的键
    // 到对应 section（section 缺失则新建）；被 remove 的键原行删除。
    // 返回是否成功。
    bool save(const std::string& path) const;

private:
    // 键："section.key" → 值。
    std::map<std::string, std::string> values_;
    // 原始行（load 时记录，save 时保序输出）。
    std::vector<std::string> rows_;
    // 本次会话中被 remove 的键（save 时跳过对应原行）。
    std::set<std::string> removedKeys_;
    // load 时解析出的全部键（"section.key"）。save 时用于区分"修改既有键"
    // （行内原位替换）与"新增键"（段头后插入，审查 M1）。
    std::set<std::string> originalKeys_;
};

}  // namespace w10de
