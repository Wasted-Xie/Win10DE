// 单个 SNI 托盘图标（任务栏托盘区的一个按钮）。
#pragma once

#include <QIcon>
#include <QToolButton>

class QDBusInterface;

namespace w10de {

// 监听一个 StatusNotifierItem（service/path），显示其图标，
// 左键 Activate、右键 ContextMenu。
class TrayIcon : public QToolButton {
    Q_OBJECT
public:
    TrayIcon(const QString& service, QWidget* parent = nullptr);

    QString service() const { return service_; }

private slots:
    // PropertiesChanged 信号连接的槽（必须为 slot，否则 SLOT() 宏无效）。
    void refreshFromItem();

private:
    QIcon iconFromItem() const;
    void onActivate();
    void onContextMenu();

    QString service_;
    QString path_;
    QDBusInterface* item_ = nullptr;
};

}  // namespace w10de
