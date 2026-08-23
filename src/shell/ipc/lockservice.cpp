#include "ipc/lockservice.h"

#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QDebug>

namespace w10de {

LockService::LockService(QObject* parent) : QObject(parent) {}

void LockService::Lock() {
    // 定位 w10lock：PATH 优先（会话安装后位于 /usr/local/bin），
    // 常见安装路径兜底。
    QString w10lock = QStandardPaths::findExecutable(QStringLiteral("w10lock"));
    if (w10lock.isEmpty() &&
            QFile::exists(QStringLiteral("/usr/local/bin/w10lock"))) {
        w10lock = QStringLiteral("/usr/local/bin/w10lock");
    }
    if (w10lock.isEmpty()) {
        qWarning() << "LockService: w10lock not found in PATH";
        return;
    }
    // 分离式启动锁屏进程（通过 PATH 查找 w10lock）。
    if (!QProcess::startDetached(w10lock, QStringList())) {
        qWarning() << "LockService: failed to start" << w10lock;
    }
}

}  // namespace w10de
