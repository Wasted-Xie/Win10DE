// Win10DE 设置中心（参考 KDE System Settings 组织：顶部搜索 + 左侧分类
// 导航 + 右侧模块页面）。系统应用，通用接口见 docs/SYSTEMAPPS.md。
#pragma once

#include <QMainWindow>
#include <QHash>
#include <QStringList>

#include <vector>  // 窗口规则列表（G1）

#include "ipc/windowrules.h"  // WindowRule（窗口规则页，G1）
#include "systemapps/settings/audioinfo.h"  // SinkInfo（音频页信号参数）

class QCheckBox;
class QComboBox;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QStackedWidget;
class QLabel;
class QSlider;
class QTableWidget;
class QTimeEdit;
class QTimer;

namespace w10de {
namespace common {
class MonitorArrangementWidget;  // 共享排列控件（common/monitorarrangement.h）
}
namespace settings {

class SettingsWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget* parent = nullptr);
    // 按名称切换到分类页（--page display 测试辅助；触发 onCategoryChanged）。
    void selectCategory(const QString& name);

private slots:
    void onSearchChanged(const QString& text);
    void onCategoryChanged();

private:
    void buildUi();
    void applyTheme();
    // 各模块页
    void buildAppearancePage();
    void buildSystemPage();
    void buildDisplayPage();
    void buildPowerPage();
    void buildAudioPage();
    void buildDefaultsPage();
    void buildNetworkPage();
    void buildBluetoothPage();
    // 输入设备页（第三批：鼠标/键盘/触摸板，[input] 配置 + D-Bus 热应用）
    void buildInputPage();
    void saveInputSettings();
    // Night Light 页（G1 补全：[night_light] 配置 + D-Bus 热应用）
    void buildNightLightPage();
    void saveNightLight();
    // 快捷键页（G1 补全：[shortcuts] 配置编辑）
    void buildShortcutsPage();
    void saveShortcuts();
    void editShortcut(int row);
    // 窗口规则页（G1 补全：[window_rules] 增删改）
    void buildWindowRulesPage();
    void refreshRulesTable();
    void addRuleDialog();
    void editRuleDialog(int row);
    void removeRule(int row);
    void saveRules();
    // 操作
    void saveTheme();
    void browseWallpaper();
    void applyWallpaper();
    void toggleAutostart(bool on);
    // 显示页（第二批：compositor D-Bus 输出管理 IPC）
    void refreshOutputs();
    void loadOutputDetails(int index);
    void applyDisplaySettings();
    // 显示器排列（中优先 #5：图形化排列 GUI）
    void applyArrangement();
    // 电源页（第二批：UPower 语义 sysfs 电池/背光）
    void refreshPower();
    void applyBrightness(int value);
    // 音频页（第二批：PipeWire 音量/设备，libpulse 客户端）
    void refreshAudio();
    void onAudioSinksReady(const QList<w10de::settings::SinkInfo>& sinks);
    void applyAudioVolume(int value);
    void toggleAudioMute(bool muted);
    // 每应用音量（中优先 #4：sink-input 应用流）
    void onAudioAppStreamsReady(
        const QList<w10de::settings::AppStreamInfo>& streams);
    void applyAppVolume(int row, int value);
    void toggleAppMute(int row, bool muted);
    // 默认应用页（第二批收官：mimeapps.list）
    bool applyDefault(int kindIndex);  // 返回是否成功（失败中断循环 L7）
    // 网络页（第三批：NetworkManager 状态）
    void refreshNetwork();
    // 蓝牙页（第三批：Bluez 状态/开关）
    void refreshBluetooth();
    void toggleBluetooth(bool on);

    // 配置文件路径（与 compositor/w10shell 一致）。
    QString configPath() const;

    QLineEdit* searchBox_ = nullptr;
    QListWidget* categoryList_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QLabel* pathLabel_ = nullptr;

    // 外观页控件
    QListWidget* themeList_ = nullptr;
    QLineEdit* wallpaperEdit_ = nullptr;
    // 系统页控件
    QLabel* versionValue_ = nullptr;
    QLabel* platformValue_ = nullptr;
    QLabel* themeValue_ = nullptr;
    // 显示页控件（org.w10de.Compositor /Outputs）
    QComboBox* outputCombo_ = nullptr;
    QComboBox* modeCombo_ = nullptr;
    QComboBox* scaleCombo_ = nullptr;
    QLabel* displayStatus_ = nullptr;
    QStringList outputNames_;   // 与 outputCombo_ 行号对应
    QList<QPair<QString, int>> modeMap_;  // 分辨率下拉 "WxH" → 高度值
    // 当前输出的当前模式（下拉恢复用）。
    int curWidth_ = 0;
    int curHeight_ = 0;
    int curScalePercent_ = 100;
    // 显示器排列控件（中优先 #5；数据由控件自身持有）。
    w10de::common::MonitorArrangementWidget* arrangement_ = nullptr;
    // 电源页控件
    QLabel* batteryValue_ = nullptr;
    QLabel* backlightValue_ = nullptr;
    QSlider* brightnessSlider_ = nullptr;
    QString backlightDevice_;
    int backlightMax_ = 0;  // 缓存 maxBrightness（审查 M3：避免重查错配）
    // 音频页控件（libpulse）
    QLabel* audioStatus_ = nullptr;
    QComboBox* sinkCombo_ = nullptr;
    QSlider* volumeSlider_ = nullptr;
    QCheckBox* muteCheck_ = nullptr;
    QStringList sinkNames_;  // 与 sinkCombo_ 行号对应
    class AudioInfo* audio_ = nullptr;
    QList<w10de::settings::SinkInfo> sinkCache_;  // 切换设备时更新滑块显示（L7）
    QTimer* audioTimeoutTimer_ = nullptr;  // 连接超时兜底（可取消，L1）
    // 每应用音量控件（中优先 #4）
    QTableWidget* appStreamTable_ = nullptr;
    QLabel* appStreamStatus_ = nullptr;
    QList<w10de::settings::AppStreamInfo> appStreamCache_;
    // 应用行号 → 控件映射（滑块/静音回调定位行）。
    QHash<int, uint32_t> appRowIndex_;
    // 默认应用页控件
    QList<QComboBox*> defaultCombos_;  // 每类别一个下拉（0=浏览器/1=邮件/2=文件管理器）
    QLabel* defaultsStatus_ = nullptr;
    // 网络页控件（第三批：NetworkManager）
    QLabel* networkStatus_ = nullptr;
    QLabel* networkDetails_ = nullptr;
    // 蓝牙页控件（第三批：Bluez）
    QCheckBox* bluetoothSwitch_ = nullptr;
    QLabel* bluetoothStatus_ = nullptr;
    QLabel* bluetoothDevices_ = nullptr;
    // 输入设备页控件（第三批：鼠标/键盘/触摸板）
    QSlider* pointerSpeedSlider_ = nullptr;
    QLabel* pointerSpeedValue_ = nullptr;
    QCheckBox* naturalScrollCheck_ = nullptr;
    QCheckBox* leftHandedCheck_ = nullptr;
    QCheckBox* tapToClickCheck_ = nullptr;
    QSlider* repeatRateSlider_ = nullptr;
    QLabel* repeatRateValue_ = nullptr;
    QSlider* repeatDelaySlider_ = nullptr;
    QLabel* repeatDelayValue_ = nullptr;
    QLabel* inputStatus_ = nullptr;
    // Night Light 页控件（G1）
    QCheckBox* nightEnabledCheck_ = nullptr;
    QSlider* nightTempSlider_ = nullptr;
    QLabel* nightTempValue_ = nullptr;
    QTimeEdit* nightStartEdit_ = nullptr;
    QTimeEdit* nightEndEdit_ = nullptr;
    QLabel* nightStatus_ = nullptr;
    // 快捷键页控件（G1）
    QTableWidget* shortcutTable_ = nullptr;
    QLabel* shortcutsStatus_ = nullptr;
    // 窗口规则页控件（G1；规则列表为值类型，编辑时副本修改）
    QTableWidget* rulesTable_ = nullptr;
    QLabel* rulesStatus_ = nullptr;
    std::vector<w10de::ipc::WindowRule> rules_;
};

}  // namespace settings
}  // namespace w10de
