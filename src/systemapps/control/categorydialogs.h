// w10control —— 控制面板类别对话框（Win10 传统对话框风格）。
//
// 与 w10settings 覆盖同一功能全集（G1 要求"设置和控制面板都可更改全部
// 功能"）：共享后端 = config.ini（~/.config/w10de/config.ini）+
// org.w10de.Compositor D-Bus + 各 info 查询类（sysfs/NetworkManager/
// Bluez/libpulse）。对话框为模态传统样式：底部 应用/确定/取消。
#pragma once

#include <QDialog>
#include <QStringList>

#include <functional>  // std::function（attachDialogButtons）
#include <vector>      // 窗口规则列表（WindowRulesDialog）

#include "ipc/windowrules.h"  // WindowRule（窗口规则对话框）
#include "systemapps/common/monitorarrangement.h"  // OutputInfo/ModeInfo

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QTableWidget;
class QTabWidget;
class QTimeEdit;

namespace w10de::settings {
class AudioInfo;
struct SinkInfo;
struct AppStreamInfo;
}

namespace w10de::control {

// ---- 系统和安全：关于 / 开机自启 / 电源（电池 + 亮度）----
class SystemSecurityDialog : public QDialog {
    Q_OBJECT
public:
    explicit SystemSecurityDialog(QWidget* parent = nullptr);
    // 保存（应用/确定共用）；返回是否成功。
    bool save();

private:
    void refreshPower();

    QLabel* batteryValue_ = nullptr;
    QLabel* backlightValue_ = nullptr;
    QSlider* brightnessSlider_ = nullptr;
    QCheckBox* autostartCheck_ = nullptr;
    QString backlightDevice_;
    int backlightMax_ = 0;
};

// ---- 外观和个性化：主题 / 壁纸 / Night Light ----
class AppearanceDialog : public QDialog {
    Q_OBJECT
public:
    explicit AppearanceDialog(QWidget* parent = nullptr);
    bool save();

private:
    QString configPath() const;

    QComboBox* themeCombo_ = nullptr;
    QLineEdit* wallpaperEdit_ = nullptr;
    QCheckBox* nightEnabledCheck_ = nullptr;
    QSlider* nightTempSlider_ = nullptr;
    QLabel* nightTempValue_ = nullptr;
    QTimeEdit* nightStartEdit_ = nullptr;
    QTimeEdit* nightEndEdit_ = nullptr;
};

// ---- 硬件和声音：显示 / 音频 / 蓝牙 / 输入设备（QTabWidget）----
class HardwareSoundDialog : public QDialog {
    Q_OBJECT
public:
    explicit HardwareSoundDialog(QWidget* parent = nullptr);
    bool save();
    // 刷新显示 tab 的输出列表（selftest/外部触发用；compositor 未运行
    // 时显示"服务不可用"）。
    void refreshOutputs();

private:
    QString configPath() const;
    // 显示 tab
    void loadOutputDetails(int index);
    void applyDisplaySettings();
    void applyArrangement();
    // 音频 tab
    void refreshAudio();
    void onAudioSinksReady(const QList<w10de::settings::SinkInfo>& sinks);
    void onAudioAppStreamsReady(
        const QList<w10de::settings::AppStreamInfo>& streams);
    void applyAudioVolume(int value);
    void toggleAudioMute(bool muted);
    void applyAppVolume(int row, int value);
    void toggleAppMute(int row, bool muted);
    // 蓝牙 tab
    void refreshBluetooth();
    void toggleBluetooth(bool on);
    // 输入 tab
    void saveInputSettings();

    // 显示
    QLabel* displayStatus_ = nullptr;
    QComboBox* outputCombo_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QComboBox* scaleCombo_ = nullptr;
    QStringList outputNames_;
    QList<QPair<QString, int>> modeMap_;
    int curWidth_ = 0, curHeight_ = 0, curScalePercent_ = 100;
    w10de::common::MonitorArrangementWidget* arrangement_ = nullptr;
    // 音频
    QLabel* audioStatus_ = nullptr;
    QComboBox* sinkCombo_ = nullptr;
    QSlider* volumeSlider_ = nullptr;
    QCheckBox* muteCheck_ = nullptr;
    QStringList sinkNames_;
    w10de::settings::AudioInfo* audio_ = nullptr;
    QList<w10de::settings::SinkInfo> sinkCache_;
    QTableWidget* appStreamTable_ = nullptr;
    QLabel* appStreamStatus_ = nullptr;
    QHash<int, uint32_t> appRowIndex_;
    // 蓝牙
    QCheckBox* bluetoothSwitch_ = nullptr;
    QLabel* bluetoothStatus_ = nullptr;
    QLabel* bluetoothDevices_ = nullptr;
    // 输入
    QSlider* pointerSpeedSlider_ = nullptr;
    QLabel* pointerSpeedValue_ = nullptr;
    QCheckBox* naturalScrollCheck_ = nullptr;
    QCheckBox* leftHandedCheck_ = nullptr;
    QCheckBox* tapToClickCheck_ = nullptr;
    QSlider* repeatRateSlider_ = nullptr;
    QLabel* repeatRateValue_ = nullptr;
    QSlider* repeatDelaySlider_ = nullptr;
    QLabel* repeatDelayValue_ = nullptr;
};

// ---- 网络和 Internet：连接状态（只读）----
class NetworkDialog : public QDialog {
    Q_OBJECT
public:
    explicit NetworkDialog(QWidget* parent = nullptr);
    bool save() { return true; }  // 只读

private:
    void refreshNetwork();
    QLabel* networkStatus_ = nullptr;
    QLabel* networkDetails_ = nullptr;
};

// ---- 程序：默认应用 + 软件中心入口 ----
class ProgramsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProgramsDialog(QWidget* parent = nullptr);
    bool save();

private:
    QList<QComboBox*> defaultCombos_;
    QLabel* defaultsStatus_ = nullptr;
};

// ---- 时钟和区域：当前日期/时间/时区（只读）----
class ClockRegionDialog : public QDialog {
    Q_OBJECT
public:
    explicit ClockRegionDialog(QWidget* parent = nullptr);
    bool save() { return true; }  // 只读
};

// ---- 快捷键（[shortcuts] 编辑；G1 双入口全功能，控制面板入口）----
class ShortcutsDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShortcutsDialog(QWidget* parent = nullptr);

private:
    void editShortcut(int row);
    bool save();
    QTableWidget* table_ = nullptr;
};

// ---- 窗口规则（[window_rules] 增删改；G1 双入口全功能，控制面板入口）----
class WindowRulesDialog : public QDialog {
    Q_OBJECT
public:
    explicit WindowRulesDialog(QWidget* parent = nullptr);

private:
    QString configPath() const;
    void refreshTable();
    void addRule();
    void editRule(int row);
    void removeRule(int row);
    bool save();
    QTableWidget* table_ = nullptr;
    std::vector<w10de::ipc::WindowRule> rules_;
};

// 构建对话框底部按钮行（应用/确定/取消）；"应用"与"确定"调 save()。
// parent：对话框；save：保存回调（返回 false 时"确定"不关闭）。
void attachDialogButtons(QDialog* dlg, std::function<bool()> save);

}  // namespace w10de::control
