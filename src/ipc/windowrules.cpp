// windowrules.cpp —— 窗口规则解析实现。

#include "ipc/windowrules.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>   // fprintf（ipc 层避免 wlroots 头依赖）
#include <cstdlib>  // strtol
#include <fstream>
#include <sstream>

#include "ipc/config.h"

namespace w10de::ipc {

namespace {

// 去掉首尾空白。
std::string trim(const std::string& s) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    const auto b = std::find_if(s.begin(), s.end(), [&](char c) {
                       return !isSpace(static_cast<unsigned char>(c));
                   });
    const auto e = std::find_if(s.rbegin(), s.rend(), [&](char c) {
                       return !isSpace(static_cast<unsigned char>(c));
                   }).base();
    return b < e ? std::string(b, e) : std::string();
}

// 拆分 match/action 段（分号分隔，段内逗号分隔 action）。
// 返回 false 表示格式非法。
bool parseRuleLine(const std::string& line, WindowRule& rule) {
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#') {
        return true;  // 空/注释：跳过（不报错）
    }
    const std::size_t semi = t.find(';');
    if (semi == std::string::npos) {
        return false;  // 缺分号
    }
    const std::string matchPart = trim(t.substr(0, semi));
    const std::string actionPart = trim(t.substr(semi + 1));

    // match：app_id=<v> 或 title=<v>；支持 & 组合 AND
    // （如 app_id=foo&title=Bar，KWin 多条件对标）。
    {
        std::string match = matchPart;
        bool firstIsApp = false;
        if (match.rfind("app_id=", 0) == 0) {
            match = match.substr(7);
            firstIsApp = true;
        } else if (match.rfind("title=", 0) == 0) {
            match = match.substr(6);
        } else {
            return false;  // 未知 match 类型
        }
        const std::size_t amp = match.find('&');
        if (amp != std::string::npos) {
            const std::string first = trim(match.substr(0, amp));
            const std::string secondRaw = trim(match.substr(amp + 1));
            if (firstIsApp) {
                rule.matchAppId = first;
                if (secondRaw.rfind("title=", 0) != 0) {
                    return false;  // 第二条件必须 title=
                }
                rule.matchTitle = trim(secondRaw.substr(6));
            } else {
                rule.matchTitle = first;
                if (secondRaw.rfind("app_id=", 0) != 0) {
                    return false;  // 第二条件必须 app_id=
                }
                rule.matchAppId = trim(secondRaw.substr(7));
            }
        } else {
            if (firstIsApp) {
                rule.matchAppId = match;
            } else {
                rule.matchTitle = match;
            }
        }
    }

    // action：| 分隔（不用逗号——geometry 参数含逗号，避免冲突）。
    std::istringstream iss(actionPart);
    std::string action;
    while (std::getline(iss, action, '|')) {
        const std::string a = trim(action);
        if (a.empty()) {
            continue;
        }
        if (a == "always_on_top") {
            rule.alwaysOnTop = true;
        } else if (a == "borderless") {
            rule.borderless = true;
        } else if (a.rfind("workspace=", 0) == 0) {
            // 审查 M1：strtol 严格校验（atoi 会把 "abc"/"2x" 静默变 0/2）。
            const std::string wsStr = trim(a.substr(10));
            char* end = nullptr;
            errno = 0;
            const long ws = std::strtol(wsStr.c_str(), &end, 10);
            if (end == wsStr.c_str() || *end != '\0' ||
                    errno == ERANGE || ws < 0 || ws > 3) {
                return false;  // 非法/越界：拒绝该规则
            }
            rule.workspace = static_cast<int>(ws);
        } else if (a.rfind("geometry=", 0) == 0) {
            // geometry=x,y,w,h
            // 审查 M3（G1）：严格解析——分隔符必须恰为 ','，读完 4 值后
            // 不允许尾随内容（原实现任意单字符当逗号、尾随垃圾静默忽略）。
            int v[4] = {0, 0, 0, 0};
            std::istringstream g(trim(a.substr(9)));
            for (int n = 0; n < 4; ++n) {
                if (!(g >> v[n])) {
                    return false;
                }
                if (n < 3) {
                    char c = '\0';
                    g >> c;
                    if (c != ',') {
                        return false;
                    }
                }
            }
            std::string rest;
            if (g >> rest) {
                return false;  // 尾随非空白内容
            }
            if (v[2] <= 0 || v[3] <= 0) {
                return false;  // 几何不完整/非法
            }
            rule.geomX = v[0];
            rule.geomY = v[1];
            rule.geomW = v[2];
            rule.geomH = v[3];
            rule.hasGeometry = true;
        } else {
            return false;  // 未知 action
        }
    }
    // match 与至少一个 action 必须有效。
    return !rule.matchAppId.empty() || !rule.matchTitle.empty();
}

}  // namespace

bool WindowRule::matchPattern(const std::string& pattern,
                              const std::string& value) {
    if (pattern.empty() || value.empty()) {
        return false;
    }
    // 含 * 的通配（简化：任意位置一个 * 通配串）。
    const std::size_t star = pattern.find('*');
    if (star == std::string::npos) {
        return value.find(pattern) != std::string::npos;
    }
    const std::string prefix = pattern.substr(0, star);
    const std::string suffix = pattern.substr(star + 1);
    if (value.size() < prefix.size() + suffix.size()) {
        return false;
    }
    return value.compare(0, prefix.size(), prefix) == 0
        && value.compare(value.size() - suffix.size(), suffix.size(),
                         suffix) == 0;
}

bool WindowRule::matches(const std::string& appId,
                         const std::string& title) const {
    if (!matchAppId.empty() && !matchPattern(matchAppId, appId)) {
        return false;
    }
    if (!matchTitle.empty() && !matchPattern(matchTitle, title)) {
        return false;
    }
    return !matchAppId.empty() || !matchTitle.empty();
}

std::vector<WindowRule> loadWindowRules(const std::string& configPath) {
    std::vector<WindowRule> rules;
    // Config 无按段遍历接口——按已知键前缀读取：规则键名任意，
    // 使用 getSection? 检查 Config 接口。若无，退化：解析原始 INI。
    // 此处使用 Config::get 不适用（键名不定），直接读文件解析 [window_rules] 段。
    std::ifstream f(configPath);
    if (!f.is_open()) {
        return rules;  // 无配置文件：空规则
    }
    bool inSection = false;
    std::string line;
    while (std::getline(f, line)) {
        const std::string t = trim(line);
        if (!t.empty() && t.front() == '[') {
            // 审查 M2：与 Config::load 的段提取对齐（trim + find(']')），
            // 否则 "[window_rules] "（尾空格）等写法规则静默全丢。
            const std::size_t close = t.find(']');
            inSection = close != std::string::npos
                && trim(t.substr(1, close - 1)) == "window_rules";
            continue;
        }
        if (!inSection) {
            continue;
        }
        const std::size_t eq = t.find('=');
        if (eq == std::string::npos) {
            continue;  // 非法行跳过
        }
        WindowRule rule;
        rule.name = trim(t.substr(0, eq));
        if (parseRuleLine(t.substr(eq + 1), rule)) {
            // 审查 L1（G1）：parseRuleLine 对空/注释值返回 true（"跳过"
            // 语义）——过滤空匹配规则（matches() 恒 false 的垃圾行）。
            if (!rule.matchAppId.empty() || !rule.matchTitle.empty()) {
                rules.push_back(rule);
            } else {
                std::fprintf(stderr,
                             "window_rules: 跳过空匹配规则 '%s'\n",
                             rule.name.c_str());
            }
        } else {
            std::fprintf(stderr,
                         "window_rules: 跳过非法规则 '%s'\n",
                         t.substr(0, eq).c_str());
        }
    }
    return rules;
}

}  // namespace w10de::ipc
