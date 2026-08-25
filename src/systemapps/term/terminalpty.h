// TerminalPty —— 伪终端封装（w10term 终端核心）。
//
// - forkpty 创建 PTY，子进程 exec shell（stdin/stdout/stderr 挂 slave）。
// - 父进程持有 master fd（O_NONBLOCK，读写两个方向都非阻塞）：
//   QSocketNotifier(Read) 读输出 → outputReady 信号；
//   write() 循环写输入，EAGAIN 时剩余数据进 pendingOut_ + Write notifier
//   续写（审查 S1：部分写/满缓冲不丢数据）。
// - 生命周期：start() 创建；stop() 关 master + SIGHUP + 超时回收子进程
//   （审查 S3：直接关窗口时 shell 可能仍存活，须杀后 waitpid）。
// - EOF 时删除 notifier 并关 fd（审查 S4：电平触发 notifier 在 EOF fd 上
//   永远可读 → 事件循环忙循环 CPU 100%）。
#pragma once

#include <QObject>

class QSocketNotifier;

namespace w10de::term {

class TerminalPty : public QObject {
    Q_OBJECT
public:
    explicit TerminalPty(QObject* parent = nullptr);
    ~TerminalPty() override;

    // 创建 PTY 并启动 shell；失败返回 false。
    bool start(const QString& shell = QString());
    // 关闭 PTY 并回收子进程（幂等；shell 存活时 SIGHUP + SIGKILL 兜底）。
    void stop();

    bool isRunning() const { return masterFd_ >= 0; }
    int masterFd() const { return masterFd_; }

    // 向 shell 写入（键盘输入/粘贴内容）。不丢数据（部分写/EAGAIN 缓冲）。
    void write(const QByteArray& data);

signals:
    // PTY 输出（原始字节，含 ANSI 转义；由 TermWindow 解析渲染）。
    void outputReady(const QByteArray& data);
    // shell 退出（正常/信号）。
    void shellExited(int status);

private:
    void onReadable();
    void onWritable();
    void ensureWriteNotifier();
    void reapChild(bool blocking);

    int masterFd_ = -1;
    pid_t childPid_ = -1;
    QSocketNotifier* readNotifier_ = nullptr;
    QSocketNotifier* writeNotifier_ = nullptr;
    QByteArray pendingOut_;  // EAGAIN 时暂存未写入数据（审查 S1）。
};

}  // namespace w10de::term
