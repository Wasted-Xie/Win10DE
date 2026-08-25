// 桌面窗口（layer-shell background 层，全屏）。
//
// 显示壁纸（--wallpaper 指定或默认 Win10 风格渐变）+ 桌面图标
// （QFileSystemModel 显示桌面目录，双击用系统默认方式打开）。
// 壁纸由 paintEvent 自绘（随窗口尺寸重缩放），图标列表绝对定位浮动左上。
// 幻灯片：setSlideshow(dir, interval) 定时轮换目录内图片（按文件名排序）。
#pragma once

#include <QImage>
#include <QStringList>
#include <QWidget>

#include <memory>

#include "shell/desktop/desktopwidgets.h"  // 桌面小部件（时钟/系统信息）

class QListView;
class QFileSystemModel;
class QModelIndex;  // openItem 参数（const 引用，前向声明足够）
class QTimer;

namespace w10de {

class DesktopWindow : public QWidget {
    Q_OBJECT
public:
    // configPath：配置文件（[widgets] 段控制小部件显示）；空则用默认。
    explicit DesktopWindow(const QString& configPath = QString(),
                           QWidget* parent = nullptr);

    // 设置壁纸；path 为空时使用默认渐变壁纸。设置后停止幻灯片。
    void setWallpaper(const QString& path);

    // 壁纸幻灯片：dir 内图片按文件名排序轮换，intervalSeconds 间隔
    // （<=0 禁用）。dir 为空/无图片时回退 setWallpaper(QString())。
    // 已知限制（审查 L6）：LayerShellQt 增量 paint 调度失效，每次轮换用
    // hide/show 强制重绘——真机上背景层会闪黑一次（interval 越短越明显）。
    void setSlideshow(const QString& dir, int intervalSeconds);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QImage renderDefaultWallpaper(const QSize& size) const;
    void updateWallpaperScaled();
    void openItem(const QModelIndex& index);
    void advanceSlideshow();

    QImage wallpaperSource_;    // 原始壁纸（按窗口尺寸缩放绘制）
    QImage wallpaperScaled_;    // 按窗口尺寸缩放后的绘制结果
    QListView* iconList_ = nullptr;
    QFileSystemModel* iconModel_ = nullptr;
    QTimer* slideshowTimer_ = nullptr;
    std::unique_ptr<DesktopWidgets> widgets_;  // 桌面小部件（中优先 #7）
    QStringList slideshowFiles_;  // 排序后的图片路径
    int slideshowIndex_ = 0;
};

}  // namespace w10de
