// windowrules —— 窗口规则（KDE-GAP 中优先 #6：对标 KWin Window Rules）。
//
// [window_rules] 配置段（compositor 启动读取，map 时应用）：
//   <name> = <match>;<action[|action...]>
//     match:  app_id=<值> 或 title=<值>（* 通配子串）
//     action: always_on_top 置顶 / borderless 无边框 /
//             workspace=<0-3> 初始工作区 / geometry=x,y,w,h 初始几何
//             （多个 action 用 | 分隔；geometry 参数含逗号故不用逗号分隔）
//   # 注释行忽略；非法行跳过并记日志（不阻断启动）。
//
// 示例：
//   [window_rules]
//   term_ontop = app_id=w10term;always_on_top
//   calc_ws2   = app_id=w10calc;workspace=1
//   chrome_geo = title=Chrome;geometry=200,100,1280,800

#pragma once

#include <string>
#include <vector>

namespace w10de::ipc {

struct WindowRule {
    // 规则名（config 中的键名；loadWindowRules 解析时填入）。
    // 供设置/控制面板 UI 显示与增删改定位。
    std::string name;
    // match：任一非空即参与匹配（* 通配子串）。
    std::string matchAppId;   // 空 = 不按 app_id 匹配
    std::string matchTitle;   // 空 = 不按 title 匹配
    // action。
    bool alwaysOnTop = false;
    bool borderless = false;
    int workspace = -1;       // -1 = 不指定
    int geomX = 0, geomY = 0, geomW = 0, geomH = 0;
    bool hasGeometry = false;

    // 通配子串匹配（pattern 含 * 时按通配，否则子串包含）。
    static bool matchPattern(const std::string& pattern,
                             const std::string& value);
    // 判定给定 app_id/title 是否命中本规则。
    bool matches(const std::string& appId,
                 const std::string& title) const;
};

// 从 config.ini 的 [window_rules] 段加载规则（非法行跳过）。
std::vector<WindowRule> loadWindowRules(const std::string& configPath);

}  // namespace w10de::ipc
