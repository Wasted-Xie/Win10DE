// Win10DE 设置中心（参考 KDE System Settings 组织：顶部搜索 + 左侧分类
// 导航 + 右侧模块页面）。系统应用，通用接口见 docs/SYSTEMAPPS.md。
#pragma once

#include <QMainWindow>

class QListWidget;
class QListWidgetItem;
class QLineEdit;
class QStackedWidget;
class QLabel;

namespace w10de::settings {

class SettingsWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit SettingsWindow(QWidget* parent = nullptr);

private slots:
    void onSearchChanged(const QString& text);
    void onCategoryChanged();

private:
    void buildUi();
    void applyTheme();
    // 各模块页
    void buildAppearancePage();
    void buildSystemPage();
    // 操作
    void saveTheme();
    void browseWallpaper();
    void applyWallpaper();
    void toggleAutostart(bool on);

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
};

}  // namespace w10de::settings
