// SNI（StatusNotifierItem）Watcher 服务：系统托盘的 D-Bus 宿主。
//
// 实现 org.kde.StatusNotifierWatcher：Qt/KDE 应用调用 QSystemTrayIcon 时
// 会自动向本服务注册托盘图标；宿主跟踪全部注册项并通知任务栏显示。
// 纯 Qt D-Bus 实现，无 KF 依赖。
#pragma once

#include <QDBusContext>
#include <QObject>
#include <QStringList>

namespace w10de {

class SniWatcher : public QObject, protected QDBusContext {
    Q_OBJECT
public:
    explicit SniWatcher(QObject* parent = nullptr);

    // watcher 服务是否注册成功（session bus）。
    bool isRegistered() const { return registered_; }

signals:
    // D-Bus 导出信号（StatusNotifierItemRegistered/Unregistered）。
    void StatusNotifierItemRegistered(const QString& service);
    void StatusNotifierItemUnregistered(const QString& service);
    void StatusNotifierHostRegistered();

public slots:
    // ---- org.kde.StatusNotifierWatcher 接口 ----
    void RegisterStatusNotifierItem(const QString& service);
    void RegisterStatusNotifierHost(const QString& service);
    QStringList RegisteredStatusNotifierItems() const;
    bool IsStatusNotifierHostRegistered() const;
    int ProtocolVersion() const;

private slots:
    // 监听 NameOwnerChanged：item 进程退出时清理残留并通知移除。
    void onNameOwnerChanged(const QString& name, const QString& oldOwner,
                            const QString& newOwner);

private:
    // 归一化客户端传入的 service 参数：
    //   "/StatusNotifierItem" → (调用者 bus 名, /StatusNotifierItem)
    //   "org.foo.Item/123/StatusNotifierItem" → (org.foo.Item/123, /StatusNotifierItem)
    QString normalizeItemService(const QString& service) const;

    QStringList items_;
    bool registered_ = false;
};

}  // namespace w10de
