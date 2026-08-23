// 桌面窗口（layer-shell background 层，全屏）。
//
// 显示壁纸（--wallpaper 指定或默认 Win10 风格渐变）+ 桌面图标
// （QFileSystemModel 显示桌面目录，双击用系统默认方式打开）。
// 壁纸由 paintEvent 自绘（随窗口尺寸重缩放），图标列表绝对定位浮动左上。
#pragma once

#include <QImage>
#include <QWidget>

class QListView;
class QFileSystemModel;
class QModelIndex;  // openItem 参数（const 引用，前向声明足够）

namespace w10de {

class DesktopWindow : public QWidget {
    Q_OBJECT
public:
    explicit DesktopWindow(QWidget* parent = nullptr);

    // 设置壁纸；path 为空时使用默认渐变壁纸。
    void setWallpaper(const QString& path);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QImage renderDefaultWallpaper(const QSize& size) const;
    void updateWallpaperScaled();
    void openItem(const QModelIndex& index);

    QImage wallpaperSource_;    // 原始壁纸（按窗口尺寸缩放绘制）
    QImage wallpaperScaled_;    // 按窗口尺寸缩放后的绘制结果
    QListView* iconList_ = nullptr;
    QFileSystemModel* iconModel_ = nullptr;
};

}  // namespace w10de
