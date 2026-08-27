// w10rdp —— 远程桌面连接入口（可选拓展 E9：xfreerdp 封装）。
//
// 用法：w10rdp [--selftest] [--render <png>] [--dry-run <host> [port] [user] [pass] [size]]。
// --selftest：参数构建/校验/配置存储自测（不启动客户端）。
// --dry-run：打印将执行的 RDP 客户端与参数（验证命令构建，不真正连接）。
// --render：offscreen 渲染表单窗口到 PNG。

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QTemporaryDir>
#include <QTextStream>
#include <QWidget>

#include <cstdio>

#include "systemapps/appipc.h"
#include "systemapps/rdp/rdpclient.h"
#include "systemapps/rdp/rdpwindow.h"
#include "ipc/config.h"
#include "ipc/theme.h"
#include "theme/colors.h"

namespace {

int runSelfTest() {
    QTextStream out(stdout);
    auto fail = [&out](const QString& m) {
        out << "SELFTEST FAIL: " << m << "\n";
        return 1;
    };
    using w10de::rdp::buildArgs;
    using w10de::rdp::loadConfig;
    using w10de::rdp::saveConfig;
    using w10de::rdp::validateConfig;

    // 1) 校验边界。
    {
        w10de::rdp::RdpConfig c;
        if (validateConfig(c).isEmpty()) {
            return fail(QStringLiteral("空主机应报错"));
        }
        c.host = QStringLiteral("192.168.1.1");
        c.port = 0;
        if (validateConfig(c).isEmpty()) {
            return fail(QStringLiteral("端口 0 应报错"));
        }
        c.port = 65536;
        if (validateConfig(c).isEmpty()) {
            return fail(QStringLiteral("端口 65536 应报错"));
        }
        c.port = 3389;
        c.size = QStringLiteral("abc");
        if (validateConfig(c).isEmpty()) {
            return fail(QStringLiteral("非法分辨率应报错"));
        }
        c.size = QStringLiteral("1920x1080");
        if (!validateConfig(c).isEmpty()) {
            return fail(QStringLiteral("合法配置不应报错"));
        }
        out << "OK validate\n";
    }
    // 2) 参数构建（参数数组，无 shell 注入）。
    {
        w10de::rdp::RdpConfig c;
        c.host = QStringLiteral("host.example.com");
        c.username = QStringLiteral("user name");
        c.password = QStringLiteral("pa ss word");
        auto args = buildArgs(c);
        // /v:host（默认端口不拼接）。
        if (!args.contains(QStringLiteral("/v:host.example.com"))) {
            return fail(QStringLiteral("默认端口应不拼 :3389"));
        }
        // 空格值引号包裹（FreeRDP 二次解析兼容）。
        if (!args.contains(QStringLiteral("/u:\"user name\""))
                || !args.contains(QStringLiteral("/p:\"pa ss word\""))) {
            return fail(QStringLiteral("用户名/密码参数错误（空格应引号包裹）"));
        }
        if (args.contains(QStringLiteral("/f"))) {
            return fail(QStringLiteral("未选全屏不应带 /f"));
        }
        // 端口非默认 + 全屏优先于 size。
        c.port = 3390;
        c.size = QStringLiteral("1280x720");
        c.fullscreen = true;
        args = buildArgs(c);
        if (!args.contains(QStringLiteral("/v:host.example.com:3390"))) {
            return fail(QStringLiteral("非默认端口拼接错误"));
        }
        if (!args.contains(QStringLiteral("/f"))
                || args.contains(QStringLiteral("/size:"))) {
            return fail(QStringLiteral("全屏应优先于分辨率"));
        }
        c.fullscreen = false;
        args = buildArgs(c);
        if (!args.contains(QStringLiteral("/size:1280x720"))) {
            return fail(QStringLiteral("分辨率参数错误"));
        }
        out << "OK build-args\n";
    }
    // 3) 配置存储往返（注入临时路径）。
    {
        QTemporaryDir tmp;
        if (!tmp.isValid()) return fail(QStringLiteral("临时目录创建失败"));
        const QByteArray cfgPath =
            (tmp.path() + QStringLiteral("/rdp.ini")).toUtf8();
        qputenv("W10DE_RDP_CONFIG", cfgPath);
        w10de::rdp::RdpConfig c;
        c.host = QStringLiteral("10.0.0.5");
        c.port = 3390;
        c.username = QStringLiteral("admin");
        c.password = QStringLiteral("secret");
        c.size = QStringLiteral("1920x1080");
        c.fullscreen = false;
        c.savePassword = true;
        saveConfig(c);
        const auto loaded = loadConfig();
        if (loaded.host != c.host || loaded.port != c.port
                || loaded.username != c.username
                || loaded.password != c.password
                || loaded.size != c.size) {
            return fail(QStringLiteral("配置往返不一致"));
        }
        // 审查 M4：savePassword 标志持久化往返。
        if (!loaded.savePassword) {
            return fail(QStringLiteral("savePassword 未持久化"));
        }
        // savePassword=false 时密码不持久化（旧密码被清除）。
        w10de::rdp::RdpConfig c2 = c;
        c2.savePassword = false;
        c2.password = QString();
        saveConfig(c2);
        if (!loadConfig().password.isEmpty()) {
            return fail(QStringLiteral("密码未被清除"));
        }
        if (loadConfig().savePassword) {
            return fail(QStringLiteral("savePassword 未更新为 false"));
        }
        // 审查 M8：损坏 INI 容错（垃圾内容回落默认值不崩溃）。
        {
            QFile bad(cfgPath);
            if (bad.open(QIODevice::WriteOnly)) {
                bad.write("\xff\xfe garbage \x00\x01");
                bad.close();
            }
            const auto fallback = loadConfig();
            if (!fallback.host.isEmpty()) {
                return fail(QStringLiteral("损坏配置未回落默认"));
            }
        }
        qunsetenv("W10DE_RDP_CONFIG");
        out << "OK config-roundtrip\n";
    }
    // 5) 审查 M1/M2/M8：IPv6 主机括号 + 特殊字符拒绝 + 引号包裹空格。
    {
        w10de::rdp::RdpConfig c;
        // IPv6 无端口：自动加方括号。
        c.host = QStringLiteral("2001:db8::1");
        auto args = buildArgs(c);
        if (!args.contains(QStringLiteral("/v:[2001:db8::1]"))) {
            return fail(QStringLiteral("IPv6 未加方括号"));
        }
        // IPv6 + 自定义端口。
        c.port = 3391;
        args = buildArgs(c);
        if (!args.contains(QStringLiteral("/v:[2001:db8::1]:3391"))) {
            return fail(QStringLiteral("IPv6 端口拼接错误"));
        }
        // 已带括号的 IPv6 不重复包裹。
        c.host = QStringLiteral("[fe80::1]");
        c.port = 3389;
        args = buildArgs(c);
        if (!args.contains(QStringLiteral("/v:[fe80::1]"))) {
            return fail(QStringLiteral("已括号 IPv6 重复包裹"));
        }
        // 主机含空格拒绝。
        c.host = QStringLiteral("bad host");
        if (validateConfig(c).isEmpty()) {
            return fail(QStringLiteral("主机空格未拒绝"));
        }
        // 用户名/密码含 / 拒绝。
        c.host = QStringLiteral("ok.example.com");
        c.username = QStringLiteral("a/b");
        if (validateConfig(c).isEmpty()) {
            return fail(QStringLiteral("用户名含 / 未拒绝"));
        }
        c.username = QStringLiteral("user");
        c.password = QStringLiteral("p:w");
        if (validateConfig(c).isEmpty()) {
            return fail(QStringLiteral("密码含 : 未拒绝"));
        }
        // 空格值用引号包裹（FreeRDP 二次解析兼容）。
        c.password = QStringLiteral("pa ss word");
        args = buildArgs(c);
        if (!args.contains(QStringLiteral("/p:\"pa ss word\""))) {
            return fail(QStringLiteral("密码空格未引号包裹"));
        }
        // 全屏 + 空 size：不产出 /size。
        c.fullscreen = true;
        c.size = QString();
        args = buildArgs(c);
        if (args.contains(QStringLiteral("/size:"))) {
            return fail(QStringLiteral("全屏时不应产出 /size"));
        }
        out << "OK ipv6-quoting\n";
    }
    // 4) RDP 客户端探测（wlfreerdp3/xfreerdp3 任一存在）。
    {
        const QString client = w10de::rdp::findClient();
        if (client.isEmpty()) {
            out << "WARN no-rdp-client（未安装 freerdp；UI 降级提示）\n";
        } else {
            out << "OK client=" << client.toUtf8() << "\n";
        }
    }
    out << "SELFTEST PASS\n";
    return 0;
}

int runDryRun(const QStringList& parts) {
    using w10de::rdp::RdpConfig;
    RdpConfig c;
    c.host = parts.value(0);
    c.port = parts.size() > 1 ? parts[1].toInt() : 3389;
    c.username = parts.size() > 2 ? parts[2] : QString();
    c.password = parts.size() > 3 ? parts[3] : QString();
    c.size = parts.size() > 4 ? parts[4] : QString();
    const QString err = w10de::rdp::validateConfig(c);
    if (!err.isEmpty()) {
        std::fprintf(stderr, "DRY-RUN FAIL: %s\n", qPrintable(err));
        return 1;
    }
    const QString client = w10de::rdp::findClient();
    if (client.isEmpty()) {
        std::fprintf(stderr, "DRY-RUN FAIL: 未找到 RDP 客户端（安装 freerdp）\n");
        return 1;
    }
    std::printf("DRY-RUN client=%s\n", qPrintable(client));
    const QStringList args = w10de::rdp::buildArgs(c);
    for (const QString& a : args) {
        // 审查 M7：密码参数打码（避免明文进终端/CI 日志）。
        if (a.startsWith(QStringLiteral("/p:"))) {
            std::printf("  arg: /p:***\n");
            continue;
        }
        std::printf("  arg: %s\n", qPrintable(a));
    }
    std::printf("DRY-RUN NOTE: 实际连接时密码仍可见于 ps aux（FreeRDP 固有）\n");
    return 0;
}

int runRender(const QString& pngPath) {
    using w10de::rdp::RdpWindow;
    RdpWindow window;
    window.show();
    QApplication::processEvents();
    const QPixmap pm = window.grab();
    if (!pm.save(pngPath)) {
        std::fprintf(stderr, "render save failed\n");
        return 1;
    }
    std::printf("RENDER OK %s %dx%d client_available=%d\n",
                qPrintable(pngPath), pm.width(), pm.height(),
                window.clientAvailable() ? 1 : 0);
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QString renderPath;
    QStringList dryRun;
    bool selftest = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--selftest") == 0) {
            selftest = true;
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (qstrcmp(argv[i], "--render") == 0 && i + 1 < argc) {
            renderPath = QString::fromLocal8Bit(argv[++i]);
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (qstrcmp(argv[i], "--dry-run") == 0) {
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                dryRun.append(QString::fromLocal8Bit(argv[++i]));
            }
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (qstrcmp(argv[i], "--help") == 0) {
            std::printf("Usage: w10rdp [--selftest] [--render <png>]"
                        " [--dry-run <host> [port] [user] [pass] [size]]\n");
            return 0;
        } else {
            std::fprintf(stderr, "w10rdp: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (selftest) {
        QApplication app(argc, argv);
        return runSelfTest();
    }
    if (!dryRun.isEmpty()) {
        QCoreApplication app(argc, argv);
        return runDryRun(dryRun);
    }
    if (!renderPath.isEmpty()) {
        QApplication app(argc, argv);
        return runRender(renderPath);
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10rdp"));
    QApplication::setApplicationDisplayName(QStringLiteral("远程桌面连接"));
    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());

    if (w10de::app::tryActivateExisting(QStringLiteral("Rdp"))) {
        return 0;
    }
    w10de::app::registerService(
        QStringLiteral("Rdp"),
        [](const QString&) {
            for (QWidget* w : QApplication::topLevelWidgets()) {
                if (auto* rw = qobject_cast<w10de::rdp::RdpWindow*>(w)) {
                    rw->show();
                    rw->raise();
                    rw->activateWindow();
                    break;
                }
            }
        },
        &app);

    w10de::rdp::RdpWindow window;
    window.show();
    return QApplication::exec();
}
