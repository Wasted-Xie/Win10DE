// Win10DE 设置中心（参考 KDE System Settings 组织：顶部搜索 + 左侧分类
// 导航 + 右侧模块页面）。系统应用，通用接口见 docs/SYSTEMAPPS.md。
#pragma once

#include <QMainWindow>
#include <QStringList>

#include "systemapps/settings/audioinfo.h"  // SinkInfo（音频页信号参数）

class QCheckBox;
class QComboBox;
class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QStackedWidget;
class QLabel;
class QSlider;
class QTimer;

namespace w10de::settings {

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
    // 操作
    void saveTheme();
    void browseWallpaper();
    void applyWallpaper();
    void toggleAutostart(bool on);
    // 显示页（第二批：compositor D-Bus 输出管理 IPC）
    void refreshOutputs();
    void loadOutputDetails(int index);
    void applyDisplaySettings();
    // 电源页（第二批：UPower 语义 sysfs 电池/背光）
    void refreshPower();
    void applyBrightness(int value);
    // 音频页（第二批：PipeWire 音量/设备，libpulse 客户端）
    void refreshAudio();
    void onAudioSinksReady(const QList<w10de::settings::SinkInfo>& sinks);
    void applyAudioVolume(int value);
    void toggleAudioMute(bool muted);
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
};

}  // namespace w10de::settings
