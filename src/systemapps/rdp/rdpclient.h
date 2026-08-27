// RdpClient —— 远程桌面客户端封装（可选拓展 E9：xfreerdp 封装）。
//
// 依赖外部二进制（FreeRDP 3.x）：优先 wlfreerdp3（Wayland 原生，可在
// Win10DE 合成器内运行），其次 xfreerdp3（X11/XWayland）。无客户端时
// 降级为提示（构建不依赖 freerdp 头）。启动用 QProcess 参数数组，
// 无 shell 注入风险。
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

namespace w10de::rdp {

struct RdpConfig {
    QString host;           // 主机/IP（必填）
    int port = 3389;        // 端口 1-65535
    QString username;
    QString password;
    QString size;           // "1280x720" 等（空 = 客户端默认；全屏时忽略）
    bool fullscreen = false;
    bool savePassword = false;  // 是否持久化密码（默认否）
};

// 校验配置；返回错误信息（空串 = 合法）。
QString validateConfig(const RdpConfig& c);

// 构建客户端参数数组（/v:/u:/p:/size:/f 等；参数列表传递，无注入）。
QStringList buildArgs(const RdpConfig& c);

// 可用 RDP 客户端二进制（wlfreerdp3 优先，xfreerdp3 次之）；均无返回空。
QString findClient();

// 配置持久化（~/.config/w10de/rdp.ini；W10DE_RDP_CONFIG 覆盖——测试隔离）。
// 返回错误信息（空串 = 保存成功；含目录创建/写入失败）。
QString configPath();
QString saveConfig(const RdpConfig& c);
RdpConfig loadConfig();

class RdpClient : public QObject {
    Q_OBJECT
public:
    explicit RdpClient(QObject* parent = nullptr);
    ~RdpClient() override;

    bool isRunning() const;
    // 启动客户端连接；返回是否成功发起（无客户端/参数非法返回 false）。
    bool start(const RdpConfig& c);
    // 终止客户端进程。
    void stop();

    QString lastError() const { return lastError_; }

signals:
    void started();
    void stopped(int exitCode);
    void error(const QString& message);

private:
    QProcess* proc_ = nullptr;
    QString lastError_;
};

}  // namespace w10de::rdp
