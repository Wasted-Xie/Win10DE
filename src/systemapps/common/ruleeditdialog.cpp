// 窗口规则编辑对话框实现（从 settingswindow.cpp 提取共享，G1）。
#include "systemapps/common/ruleeditdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QObject>
#include <QSpinBox>
#include <QVBoxLayout>

namespace w10de::common {

QDialog* makeRuleDialog(QWidget* parent,
                        const w10de::ipc::WindowRule& initial,
                        RuleDialogFields* out) {
    auto* dlg = new QDialog(parent);
    dlg->setWindowTitle(initial.name.empty()
        ? QStringLiteral("新增窗口规则")
        : QStringLiteral("编辑窗口规则：%1")
              .arg(QString::fromStdString(initial.name)));
    dlg->setMinimumWidth(460);
    auto* lay = new QVBoxLayout(dlg);
    auto* form = new QFormLayout;

    out->name = new QLineEdit(dlg);
    out->name->setPlaceholderText(QStringLiteral("规则名称（config 键名）"));
    out->name->setText(QString::fromStdString(initial.name));
    form->addRow(QStringLiteral("名称"), out->name);

    out->matchType = new QComboBox(dlg);
    out->matchType->addItem(QStringLiteral("app_id（应用标识）"),
                            QStringLiteral("app_id"));
    out->matchType->addItem(QStringLiteral("title（窗口标题）"),
                            QStringLiteral("title"));
    const bool appFirst = !initial.matchAppId.empty();
    out->matchType->setCurrentIndex(appFirst ? 0 : 1);
    form->addRow(QStringLiteral("匹配类型"), out->matchType);

    out->matchValue = new QLineEdit(dlg);
    out->matchValue->setPlaceholderText(QStringLiteral("匹配值（* 通配子串）"));
    out->matchValue->setText(QString::fromStdString(
        appFirst ? initial.matchAppId : initial.matchTitle));
    form->addRow(QStringLiteral("匹配值"), out->matchValue);

    // S2 修复：AND 双条件（app_id & title 组合）。第二类型 = 主类型相反。
    out->useSecondMatch = new QCheckBox(
        appFirst ? QStringLiteral("同时按 title 匹配")
                 : QStringLiteral("同时按 app_id 匹配"),
        dlg);
    const bool hasSecond = appFirst ? !initial.matchTitle.empty()
                                    : !initial.matchAppId.empty();
    out->useSecondMatch->setChecked(hasSecond);
    form->addRow(QString(), out->useSecondMatch);
    out->secondMatchValue = new QLineEdit(dlg);
    out->secondMatchValue->setPlaceholderText(
        QStringLiteral("第二匹配值（* 通配子串；可选）"));
    out->secondMatchValue->setText(QString::fromStdString(
        appFirst ? initial.matchTitle : initial.matchAppId));
    out->secondMatchValue->setEnabled(hasSecond);
    form->addRow(QString(), out->secondMatchValue);
    QObject::connect(out->useSecondMatch, &QCheckBox::toggled, dlg,
                     [out](bool on) {
        out->secondMatchValue->setEnabled(on);
    });

    out->onTop = new QCheckBox(QStringLiteral("始终置顶"), dlg);
    out->onTop->setChecked(initial.alwaysOnTop);
    form->addRow(QString(), out->onTop);
    out->borderless = new QCheckBox(QStringLiteral("无边框"), dlg);
    out->borderless->setChecked(initial.borderless);
    form->addRow(QString(), out->borderless);

    out->wsSpin = new QSpinBox(dlg);
    out->wsSpin->setRange(-1, 3);
    out->wsSpin->setSpecialValueText(QStringLiteral("（不指定）"));
    out->wsSpin->setValue(initial.workspace >= 0 ? initial.workspace : -1);
    form->addRow(QStringLiteral("初始工作区"), out->wsSpin);

    out->useGeometry = new QCheckBox(QStringLiteral("指定初始几何"), dlg);
    out->useGeometry->setChecked(initial.hasGeometry);
    form->addRow(QString(), out->useGeometry);
    auto* geoRow = new QHBoxLayout;
    const auto makeSpin = [dlg](int value, const QString& prefix) {
        auto* s = new QSpinBox(dlg);
        s->setRange(-99999, 99999);
        s->setPrefix(prefix);
        s->setValue(value);
        return s;
    };
    out->gx = makeSpin(initial.geomX, QStringLiteral("x="));
    out->gy = makeSpin(initial.geomY, QStringLiteral("y="));
    out->gw = makeSpin(initial.geomW, QStringLiteral("w="));
    out->gh = makeSpin(initial.geomH, QStringLiteral("h="));
    geoRow->addWidget(out->gx);
    geoRow->addWidget(out->gy);
    geoRow->addWidget(out->gw);
    geoRow->addWidget(out->gh);
    form->addRow(QStringLiteral("几何"), geoRow);
    lay->addLayout(form);

    auto* box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dlg);
    QObject::connect(box, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
    QObject::connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    lay->addWidget(box);
    return dlg;
}

w10de::ipc::WindowRule ruleFromFields(const RuleDialogFields& f,
                                      const w10de::ipc::WindowRule& base) {
    w10de::ipc::WindowRule rule = base;
    rule.name = f.name->text().trimmed().toStdString();
    rule.matchAppId.clear();
    rule.matchTitle.clear();
    const QString matchValue = f.matchValue->text().trimmed();
    if (f.matchType->currentIndex() == 0) {
        rule.matchAppId = matchValue.toStdString();
    } else {
        rule.matchTitle = matchValue.toStdString();
    }
    // S2 修复：AND 双条件回填（勾选且第二值非空）。
    if (f.useSecondMatch != nullptr && f.useSecondMatch->isChecked()) {
        const QString second = f.secondMatchValue->text().trimmed();
        if (!second.isEmpty()) {
            if (f.matchType->currentIndex() == 0) {
                rule.matchTitle = second.toStdString();
            } else {
                rule.matchAppId = second.toStdString();
            }
        }
    }
    rule.alwaysOnTop = f.onTop->isChecked();
    rule.borderless = f.borderless->isChecked();
    rule.workspace = f.wsSpin->value();
    if (f.useGeometry->isChecked()) {
        rule.hasGeometry = true;
        rule.geomX = f.gx->value();
        rule.geomY = f.gy->value();
        rule.geomW = f.gw->value();
        rule.geomH = f.gh->value();
    } else {
        rule.hasGeometry = false;
    }
    return rule;
}

QString ruleInputError(const QString& name, const QString& matchValue,
                       const QString& secondMatchValue) {
    // 名称是 config 键名（loadWindowRules 用首个 '=' 拆分键值）。
    for (const QChar& c : {QLatin1Char('='), QLatin1Char(';'), QLatin1Char('&')}) {
        if (name.contains(c)) {
            return QStringLiteral("规则名称不能包含字符：= ; &");
        }
        if (matchValue.contains(c) || secondMatchValue.contains(c)) {
            return QStringLiteral("匹配值不能包含字符：= ; &");
        }
    }
    return QString();
}

}  // namespace w10de::common
