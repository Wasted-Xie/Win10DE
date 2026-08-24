// Win10DE 系统应用通用接口（单实例 + 激活既有窗口，D-Bus）。
//
// 约定见 docs/SYSTEMAPPS.md：每个系统应用是独立二进制，D-Bus 服务
// org.w10de.Apps.<AppName>（对象 /App，方法 Activate(s path)）。
// 本封装供全部系统应用复用：tryActivateExisting 探测既有实例并激活；
// registerService 注册本实例为服务。
#pragma once

#include <QString>
#include <functional>

class QObject;

namespace w10de::app {

// 探测并激活既有实例：服务已在运行 → 调 Activate(path) 并返回 true
//（当前进程应退出）；服务不在 → 返回 false（当前进程成为实例）。
bool tryActivateExisting(const QString& appName, const QString& path = QString());

// 以 org.w10de.Apps.<appName> 注册本实例为 D-Bus 服务。
// onActivate(path)：既有实例被再次启动时回调（置前主窗口并导航）。
// 返回 false 表示服务名已被占用（本进程应退出）。
// parent：服务对象生命周期宿主（通常传 qApp）。
bool registerService(const QString& appName,
                     std::function<void(const QString& path)> onActivate,
                     QObject* parent);

}  // namespace w10de::app
