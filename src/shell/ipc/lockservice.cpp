#include "ipc/lockservice.h"

#include <QProcess>
#include <QDebug>

namespace w10de {

LockService::LockService(QObject* parent) : QObject(parent) {}

void LockService::Lock() {
    // 分离式启动锁屏进程（通过 PATH 查找 w10lock）。
    if (!QProcess::startDetached(QStringLiteral("w10lock"), QStringList())) {
        qWarning() << "LockService: failed to start w10lock";
    }
}

}  // namespace w10de
