// 任务栏托盘区：SNI watcher 宿主 + 图标集合。
#pragma once

#include <QMap>
#include <QWidget>

class QHBoxLayout;

namespace w10de {

class SniWatcher;
class TrayIcon;

// 通知区域（时钟左侧）：收集全部注册的 StatusNotifierItem 并显示图标。
class TrayArea : public QWidget {
    Q_OBJECT
public:
    explicit TrayArea(QWidget* parent = nullptr);

private:
    void onItemRegistered(const QString& service);
    void onItemUnregistered(const QString& service);

    SniWatcher* watcher_ = nullptr;
    QHBoxLayout* layout_ = nullptr;
    QMap<QString, TrayIcon*> icons_;
};

}  // namespace w10de
