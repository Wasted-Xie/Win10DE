// 窗口规则编辑对话框（w10settings 与 w10control 共享，G1）。
//
// 对话框字段（名称/匹配类型/匹配值/置顶/无边框/工作区/几何）+ 预填
// initial 规则；exec 后调用方从字段读取并自行组装 WindowRule。
#pragma once

#include <QString>

#include "ipc/windowrules.h"

class QCheckBox;
class QComboBox;
class QDialog;
class QLineEdit;
class QSpinBox;
class QWidget;

namespace w10de::common {

// 规则编辑对话框字段（makeRuleDialog 填充）。
struct RuleDialogFields {
    QLineEdit* name = nullptr;
    QComboBox* matchType = nullptr;  // 0=app_id 1=title
    QLineEdit* matchValue = nullptr;
    // S2 修复：AND 双条件（app_id & title 组合）编辑支持。
    QCheckBox* useSecondMatch = nullptr;
    QLineEdit* secondMatchValue = nullptr;
    QCheckBox* onTop = nullptr;
    QCheckBox* borderless = nullptr;
    QSpinBox* wsSpin = nullptr;
    QCheckBox* useGeometry = nullptr;
    QSpinBox* gx = nullptr, *gy = nullptr, *gw = nullptr, *gh = nullptr;
};

// 构建窗口规则编辑对话框（新增/编辑共用；initial 预填，编辑后字段经 out）。
// 调用方负责 exec 后 delete。返回的对话框有 Ok/Cancel 按钮。
QDialog* makeRuleDialog(QWidget* parent,
                        const w10de::ipc::WindowRule& initial,
                        RuleDialogFields* out);

// 从对话框字段组装 WindowRule（名称/匹配校验由调用方做）。
// matchType==0 → matchAppId；否则 matchTitle；useSecondMatch 勾选且
// 第二匹配值非空时补上另一条件（AND）。
w10de::ipc::WindowRule ruleFromFields(const RuleDialogFields& f,
                                      const w10de::ipc::WindowRule& base);

// S1 修复：规则输入校验（名称/匹配值禁止破坏 config 行解析的字符
// `;` `&` `=`——parseRuleLine 用 `;` 切分 match/action、`&` 引入 AND、
// 键名用首个 `=` 拆分）。返回错误描述；空字符串 = 合法。
QString ruleInputError(const QString& name, const QString& matchValue,
                       const QString& secondMatchValue);

}  // namespace w10de::common
