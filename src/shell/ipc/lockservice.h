// D-Bus 会话服务：org.w10de.Shell —— 外部工具/脚本触发 Shell 动作。
//
// 方法：
//   Lock() —— 启动锁屏进程（w10lock，ext-session-lock 客户端）。
#pragma once

#include <QObject>

namespace w10de {

class LockService : public QObject {
    Q_OBJECT
public:
    explicit LockService(QObject* parent = nullptr);

public slots:
    // D-Bus 导出：触发锁屏。
    void Lock();
};

}  // namespace w10de
