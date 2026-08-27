// RdpClient 实现（可选拓展 E9 远程桌面）。
//
// 参数兼容 FreeRDP 3.x（wlfreerdp3 / xfreerdp3 共用 /v:/u:/p:/size:/f）。
// 配置用 QSettings INI 存 ~/.config/w10de/rdp.ini（密码默认不持久化）。

#include "systemapps/rdp/rdpclient.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

namespace w10de::rdp {

QString validateConfig(const RdpConfig& c) {
    const QString host = c.host.trimmed();
    if (host.isEmpty()) {
        return QStringLiteral("请输入主机名或 IP 地址");
    }
    // 审查 M2：主机含空格/斜杠/引号会破坏 FreeRDP 二次解析。
    if (host.contains(QLatin1Char(' ')) || host.contains(QLatin1Char('/'))
            || host.contains(QLatin1Char('"')) || host.contains(QLatin1Char('\''))) {
        return QStringLiteral("主机名含不允许的字符（空格 / 斜杠 引号）");
    }
    if (c.port < 1 || c.port > 65535) {
        return QStringLiteral("端口必须在 1-65535 之间");
    }
    // 审查 M2：用户名/密码含 / 或 : 会被 FreeRDP 当参数分隔符二次拆分。
    const QString user = c.username;
    const QString pass = c.password;
    if (user.contains(QLatin1Char('/')) || user.contains(QLatin1Char(':'))
            || user.contains(QLatin1Char('"'))) {
        return QStringLiteral("用户名含不允许的字符（/ 冒号 引号）");
    }
    if (pass.contains(QLatin1Char('/')) || pass.contains(QLatin1Char(':'))
            || pass.contains(QLatin1Char('"'))) {
        return QStringLiteral("密码含不允许的字符（/ 冒号 引号）");
    }
    if (!c.size.isEmpty()) {
        // 分辨率格式 WxH（数字 × 数字；大小写 x 均可——审查 L3）。
        const QStringList parts = c.size.toLower().split(QLatin1Char('x'));
        bool okW = false, okH = false;
        if (parts.size() == 2) {
            const int w = parts[0].toInt(&okW);
            const int h = parts[1].toInt(&okH);
            if (!okW || !okH || w < 320 || h < 200 || w > 16384 || h > 16384) {
                return QStringLiteral("分辨率格式错误（应为 宽x高，如 1920x1080）");
            }
        } else {
            return QStringLiteral("分辨率格式错误（应为 宽x高，如 1920x1080）");
        }
    }
    return QString();
}

QStringList buildArgs(const RdpConfig& c) {
    QStringList args;
    QString target = c.host.trimmed();
    // 审查 M1：IPv6 主机加方括号（FreeRDP 按最后一个 : 切分 host/port）。
    if (target.contains(QLatin1Char(':')) && !target.startsWith(QLatin1Char('['))) {
        target = QLatin1Char('[') + target + QLatin1Char(']');
    }
    if (c.port != 3389) {
        target += QStringLiteral(":%1").arg(c.port);
    }
    args << QStringLiteral("/v:") + target;
    // 审查 M2：值含空格用引号包裹（FreeRDP Windows 风格二次解析支持；
    // 实测 wlfreerdp3 接受）；/ : " 已在 validateConfig 拒绝。
    const auto quoted = [](const QString& v) {
        return v.contains(QLatin1Char(' '))
            ? QStringLiteral("\"") + v + QStringLiteral("\"") : v;
    };
    if (!c.username.isEmpty()) {
        args << QStringLiteral("/u:") + quoted(c.username);
    }
    if (!c.password.isEmpty()) {
        args << QStringLiteral("/p:") + quoted(c.password);
    }
    if (c.fullscreen) {
        args << QStringLiteral("/f");
    } else if (!c.size.isEmpty()) {
        args << QStringLiteral("/size:") + c.size.toLower();
    }
    // MVP：忽略证书校验（内网/自签场景可用；生产建议去掉）。
    args << QStringLiteral("/cert:ignore");
    return args;
}

QString findClient() {
    // Wayland 原生优先（Win10DE 合成器内可运行）；X11 客户端次之。
    const QString wl = QStandardPaths::findExecutable(QStringLiteral("wlfreerdp3"));
    if (!wl.isEmpty()) return wl;
    const QString x11 = QStandardPaths::findExecutable(QStringLiteral("xfreerdp3"));
    if (!x11.isEmpty()) return x11;
    // 兼容旧名（FreeRDP 2.x）。
    const QString legacy = QStandardPaths::findExecutable(QStringLiteral("xfreerdp"));
    if (!legacy.isEmpty()) return legacy;
    return QString();
}

QString configPath() {
    const QByteArray env = qgetenv("W10DE_RDP_CONFIG");
    if (!env.isEmpty()) {
        return QString::fromUtf8(env);
    }
    return QDir::homePath() + QStringLiteral("/.config/w10de/rdp.ini");
}

QString saveConfig(const RdpConfig& c) {
    // 审查 M6：目录可能不存在（其他组件未运行过），先创建。
    const QString path = configPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSettings s(path, QSettings::IniFormat);
    s.setValue(QStringLiteral("host"), c.host);
    s.setValue(QStringLiteral("port"), c.port);
    s.setValue(QStringLiteral("username"), c.username);
    s.setValue(QStringLiteral("size"), c.size);
    s.setValue(QStringLiteral("fullscreen"), c.fullscreen);
    s.setValue(QStringLiteral("savePassword"), c.savePassword);  // 审查 M4
    if (c.savePassword) {
        s.setValue(QStringLiteral("password"), c.password);
    } else {
        s.remove(QStringLiteral("password"));  // 不持久化时清掉旧密码
    }
    s.sync();
    if (s.status() != QSettings::NoError) {
        return QStringLiteral("写入配置失败：%1").arg(path);
    }
    // 审查 M3：配置文件含明文密码时收紧权限为 0600。
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    return QString();
}

RdpConfig loadConfig() {
    RdpConfig c;
    QSettings s(configPath(), QSettings::IniFormat);
    c.host = s.value(QStringLiteral("host")).toString();
    c.port = s.value(QStringLiteral("port"), 3389).toInt();
    c.username = s.value(QStringLiteral("username")).toString();
    c.password = s.value(QStringLiteral("password")).toString();
    c.size = s.value(QStringLiteral("size")).toString();
    c.fullscreen = s.value(QStringLiteral("fullscreen"), false).toBool();
    c.savePassword = s.value(QStringLiteral("savePassword"), false).toBool();
    return c;
}

RdpClient::RdpClient(QObject* parent) : QObject(parent) {
    proc_ = new QProcess(this);
    // 审查 M5：转发子进程 stdout/stderr（FreeRDP 日志量大，默认缓存会
    // 无限增长）。
    proc_->setProcessChannelMode(QProcess::ForwardedChannels);
    connect(proc_, &QProcess::started, this, [this] {
        emit started();
    });
    connect(proc_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        emit stopped(code);
    });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            lastError_ = QStringLiteral("无法启动 RDP 客户端");
            emit error(lastError_);
        }
    });
}

RdpClient::~RdpClient() {
    stop();
}

bool RdpClient::isRunning() const {
    return proc_ != nullptr
        && proc_->state() != QProcess::NotRunning;
}

bool RdpClient::start(const RdpConfig& c) {
    const QString err = validateConfig(c);
    if (!err.isEmpty()) {
        lastError_ = err;
        return false;
    }
    const QString client = findClient();
    if (client.isEmpty()) {
        lastError_ = QStringLiteral(
            "未找到 RDP 客户端（wlfreerdp3/xfreerdp3）。\n"
            "请安装 freerdp 包后重试。");
        return false;
    }
    if (isRunning()) {
        stop();
    }
    // 审查 H1：stop 后确认进程已完全停止（kill 是异步的——直接 start
    // 会静默失败，UI 谎报"正在连接"）。
    if (proc_->state() != QProcess::NotRunning) {
        lastError_ = QStringLiteral("前一个连接进程尚未退出，请稍后重试");
        return false;
    }
    proc_->start(client, buildArgs(c));
    return true;
}

void RdpClient::stop() {
    if (proc_ == nullptr) {
        return;
    }
    if (proc_->state() != QProcess::NotRunning) {
        proc_->terminate();
        if (!proc_->waitForFinished(3000)) {
            proc_->kill();
            // 审查 H1：kill 后等待回收（waitForFinished 内部处理事件）。
            proc_->waitForFinished(1000);
        }
    }
}

}  // namespace w10de::rdp
