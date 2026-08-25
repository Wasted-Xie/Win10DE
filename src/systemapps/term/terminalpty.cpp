#include "systemapps/term/terminalpty.h"

#include <QSocketNotifier>
#include <QtDebug>

#include <cerrno>
#include <cstring>

#include <pty.h>       // forkpty（util-linux）
#include <fcntl.h>     // O_NONBLOCK / F_SETFL（pty master 非阻塞）
#include <csignal>     // kill / SIGHUP / SIGKILL
#include <sys/wait.h>  // waitpid
#include <unistd.h>

namespace w10de::term {

TerminalPty::TerminalPty(QObject* parent) : QObject(parent) {}

TerminalPty::~TerminalPty() {
    stop();
}

bool TerminalPty::start(const QString& shell) {
    if (masterFd_ >= 0) {
        return false;  // 已在运行
    }
    const QString sh = shell.isEmpty()
        ? QString::fromLocal8Bit(qgetenv("SHELL").isEmpty()
                                     ? "/bin/bash"
                                     : qgetenv("SHELL"))
        : shell;
    const QByteArray shellUtf8 = sh.toLocal8Bit();

    // forkpty：子进程在 slave 上挂会话并重定向 stdio；父进程返回 master。
    winsize ws{};
    ws.ws_col = 120;
    ws.ws_row = 32;
    const pid_t pid = forkpty(&masterFd_, nullptr, nullptr, &ws);
    if (pid < 0) {
        qWarning("TerminalPty: forkpty failed: %s", std::strerror(errno));
        masterFd_ = -1;
        return false;
    }
    if (pid == 0) {
        // 子进程：TERM 必须设置（dumb 下 readline/TUI 退化，审查 M2）；
        // exec 交互 shell。
        ::setenv("TERM", "xterm-256color", 1);
        ::setenv("COLORTERM", "truecolor", 1);
        execlp(shellUtf8.constData(), shellUtf8.constData(), "-i", nullptr);
        _exit(127);  // exec 失败
    }
    childPid_ = pid;
    // master fd 必须非阻塞（读写两方向）：阻塞 read 会卡死事件循环
    // （gdb attach 实测）；写方向 EAGAIN 由 pendingOut_ 缓冲（审查 S1）。
    // fcntl 失败必须中止——否则 O_NONBLOCK 未生效，灾难性卡死（审查 M1）。
    const int flags = ::fcntl(masterFd_, F_GETFL, 0);
    if (flags < 0 ||
            ::fcntl(masterFd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        qWarning("TerminalPty: failed to set O_NONBLOCK on master: %s",
                 std::strerror(errno));
        ::close(masterFd_);
        masterFd_ = -1;
        childPid_ = -1;
        return false;
    }
    readNotifier_ = new QSocketNotifier(masterFd_, QSocketNotifier::Read, this);
    connect(readNotifier_, &QSocketNotifier::activated,
            this, &TerminalPty::onReadable);
    qInfo("TerminalPty: started shell '%s' (pid %d)", shellUtf8.constData(), pid);
    return true;
}

void TerminalPty::stop() {
    if (readNotifier_ != nullptr) {
        // 不能在此调用 setEnabled(false)：stop() 可能从 activated 槽栈内
        // 触发（Qt 明确禁止在 activated 处理器中禁用 notifier——崩溃源，
        // gdb 实测）。deleteLater 由事件循环处理，从自身槽调用安全。
        readNotifier_->deleteLater();
        readNotifier_ = nullptr;
    }
    if (writeNotifier_ != nullptr) {
        writeNotifier_->deleteLater();
        writeNotifier_ = nullptr;
    }
    pendingOut_.clear();
    if (masterFd_ >= 0) {
        ::close(masterFd_);
        masterFd_ = -1;
    }
    // 关 master 触发 slave 端 SIGHUP 是异步的；shell 可能仍存活（用户
    // 直接关窗口）。SIGHUP + 超时 waitpid，超时 SIGKILL 兜底（审查 S3）。
    if (childPid_ > 0) {
        ::kill(childPid_, SIGHUP);
        reapChild(true);
    }
}

void TerminalPty::onReadable() {
    const int fd = masterFd_;
    if (fd < 0) {
        return;
    }
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            emit outputReady(QByteArray(buf, static_cast<int>(n)));
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;  // 无更多数据
        }
        // EOF（shell 关闭 slave）或错误：shell 已退出。必须删除 notifier
        // 并关 fd——EOF 的 fd 对电平触发 notifier 永远"可读"，留着会
        // 事件循环忙循环 CPU 100%（审查 S4；deleteLater 避开 activated
        // 栈内 setEnabled 限制）。
        readNotifier_->deleteLater();
        readNotifier_ = nullptr;
        ::close(fd);
        masterFd_ = -1;
        reapChild(false);
        return;
    }
}

void TerminalPty::onWritable() {
    if (pendingOut_.isEmpty()) {
        // 数据已写完：停用写 notifier。不能 setEnabled(false)（activated
        // 栈内禁止——与读方向同款约束），deleteLater 安全。
        if (writeNotifier_ != nullptr) {
            writeNotifier_->deleteLater();
            writeNotifier_ = nullptr;
        }
        return;
    }
    const int fd = masterFd_;
    if (fd < 0) {
        pendingOut_.clear();
        return;
    }
    const ssize_t n = ::write(fd, pendingOut_.constData(),
                              static_cast<size_t>(pendingOut_.size()));
    if (n > 0) {
        pendingOut_.remove(0, static_cast<int>(n));
    } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return;  // 仍满：等下一次 Write 事件。
    } else if (n < 0) {
        // 真错误（EPIPE 等）：放弃剩余。
        pendingOut_.clear();
    }
    if (pendingOut_.isEmpty() && writeNotifier_ != nullptr) {
        writeNotifier_->deleteLater();
        writeNotifier_ = nullptr;
    }
}

void TerminalPty::reapChild(bool blocking) {
    if (childPid_ <= 0) {
        return;
    }
    auto tryWait = [this](bool block) -> bool {
        int status = 0;
        const pid_t r = block
            ? ::waitpid(childPid_, &status, 0)
            : ::waitpid(childPid_, &status, WNOHANG);
        if (r == childPid_) {
            const int code = WIFEXITED(status) ? WEXITSTATUS(status)
                             : (WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1);
            qInfo("TerminalPty: shell exited (pid %d, code %d)", childPid_, code);
            childPid_ = -1;
            emit shellExited(code);
            return true;
        }
        return false;
    };
    if (tryWait(false)) {
        return;
    }
    if (blocking) {
        // 阻塞回收：SIGHUP 后给 shell 短暂收尾时间，超时 SIGKILL。
        for (int i = 0; i < 50; ++i) {
            if (tryWait(false)) {
                return;
            }
            ::usleep(10 * 1000);  // 10ms × 50 = 500ms
        }
        qWarning("TerminalPty: shell (pid %d) did not exit after SIGHUP; SIGKILL",
                 childPid_);
        ::kill(childPid_, SIGKILL);
        tryWait(true);
    }
    // 非阻塞且未退出：保留 childPid_（EOF 不代表 shell 已死；stop() 再回收）。
}

void TerminalPty::write(const QByteArray& data) {
    if (masterFd_ < 0 || data.isEmpty()) {
        return;
    }
    if (!pendingOut_.isEmpty()) {
        // 已有待写数据：追加（先到先写）。
        pendingOut_.append(data);
        ensureWriteNotifier();
        return;
    }
    const ssize_t n = ::write(masterFd_, data.constData(),
                              static_cast<size_t>(data.size()));
    if (n == static_cast<ssize_t>(data.size())) {
        return;  // 全部写入
    }
    if (n > 0) {
        pendingOut_ = data.mid(static_cast<int>(n));  // 部分写：剩余缓冲
    } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        pendingOut_ = data;  // 缓冲满：整段缓冲
    } else if (n < 0) {
        return;  // 真错误：丢弃
    }
    ensureWriteNotifier();
}

void TerminalPty::ensureWriteNotifier() {
    if (writeNotifier_ == nullptr && masterFd_ >= 0) {
        writeNotifier_ = new QSocketNotifier(masterFd_, QSocketNotifier::Write, this);
        connect(writeNotifier_, &QSocketNotifier::activated,
                this, &TerminalPty::onWritable);
    }
    if (writeNotifier_ != nullptr) {
        writeNotifier_->setEnabled(true);
    }
}

}  // namespace w10de::term
