// w10lock —— 锁屏进程（ext-session-lock-v1，M6 + KDE-GAP #4）
//
// QGuiApplication 提供 Wayland display；锁屏画面用 QPainter 离屏渲染到
// QImage，再提交到 session-lock surface（wl_shm buffer）。
// KDE-GAP #4：密码输入 + PAM 验证（服务 "login"）；验证不可用（非 root）
// 时 UI 提示并回退任意键解锁（安全限制见 README/HANDOFF）。

#include <QDateTime>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QThread>
#include <QTimer>

#include <QtGui/qpa/qplatformnativeinterface.h>

#include <xkbcommon/xkbcommon-keysyms.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "lock/lockclient.h"
#include "lock/pamcheck.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("w10lock"));

    // 获取 Wayland display（Qt 平台集成提供；锁定期间无普通窗口）。
    auto* waylandApp =
        app.nativeInterface<QNativeInterface::QWaylandApplication>();
    wl_display* display = waylandApp != nullptr ? waylandApp->display() : nullptr;
    if (display == nullptr) {
        std::fprintf(stderr, "w10lock: no Wayland display (not running on Wayland?)\n");
        return 1;
    }

    w10de::LockClient client(display);
    if (!client.isValid()) {
        std::fprintf(stderr, "w10lock: failed to bind required globals "
                             "(compositor lacks ext-session-lock?)\n");
        return 1;
    }

    // ---- 密码状态（KDE-GAP #4）----
    std::string password;
    QString errorText;
    // PAM 可用性（审查 M4：只探测一次并缓存——避免每次按键调用 PAM 触发
    // faillock preauth 计数）。
    bool pamProbed = false;
    bool pamAvailable = false;

    auto doUnlock = [&]() {
        // 审查 M1：解锁前擦除密码明文。
        std::fill(password.begin(), password.end(), '\0');
        client.unlock();
        // 协议要求：解锁后退出前必须 sync，确保合成器收到 unlock_and_destroy。
        wl_display_roundtrip(display);
        app.quit();
    };

    // 渲染一帧锁屏画面（Win10 风格：蓝底 + 大时钟 + 日期 + 密码框）。
    QImage frame;
    auto render = [&]() {
        const int w = client.width();
        const int h = client.height();
        if (w <= 0 || h <= 0) {
            return;
        }
        if (frame.size() != QSize(w, h)) {
            // 预乘 ARGB：与 WL_SHM_FORMAT_ARGB8888 的预乘语义一致，
            // 避免抗锯齿边缘色偏。
            frame = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
        }
        frame.fill(QColor(0x00, 0x5A, 0x9E));  // Win10 锁屏蓝

        const QDateTime now = QDateTime::currentDateTime();
        QPainter painter(&frame);
        painter.setPen(Qt::white);
        QFont font = painter.font();
        font.setPointSize(72);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(frame.rect(), Qt::AlignCenter,
                         now.toString(QStringLiteral("HH:mm")));

        font.setPointSize(20);
        font.setBold(false);
        painter.setFont(font);
        QRect dateRect = frame.rect();
        dateRect.adjust(0, 110, 0, 0);
        painter.drawText(dateRect, Qt::AlignHCenter | Qt::AlignTop,
                         now.toString(QStringLiteral("yyyy年M月d日 dddd")));

        // ---- 密码输入区（KDE-GAP #4）----
        font.setPointSize(16);
        painter.setFont(font);
        QRect promptRect = frame.rect();
        promptRect.adjust(0, -120, 0, -40);
        painter.setPen(QColor(0xFF, 0xFF, 0xFF, 220));
        if (pamAvailable || !pamProbed) {
            painter.drawText(promptRect, Qt::AlignHCenter | Qt::AlignBottom,
                             QStringLiteral("输入密码解锁（%1）")
                                 .arg(QString::fromStdString(
                                     w10de::lock::currentUsername())));
        } else {
            painter.drawText(promptRect, Qt::AlignHCenter | Qt::AlignBottom,
                             QStringLiteral("验证服务不可用"));
        }

        // 密码圆点（最多显示 24 个）。
        QRect dotsRect = frame.rect();
        dotsRect.adjust(0, -60, 0, 0);
        const int maxDots = 24;
        const int dots = static_cast<int>(password.size()) > maxDots
            ? maxDots : static_cast<int>(password.size());
        const int dotSpacing = 22;
        const int totalW = (dots > 0 ? (dots - 1) * dotSpacing : 0);
        const int startX = (w - totalW) / 2;
        painter.setBrush(Qt::white);
        painter.setPen(Qt::NoPen);
        for (int i = 0; i < dots; ++i) {
            painter.drawEllipse(QPointF(startX + i * dotSpacing, dotsRect.center().y()), 6, 6);
        }

        // 错误/提示文字。
        if (!errorText.isEmpty()) {
            painter.setPen(QColor(0xFF, 0x6B, 0x6B));  // 浅红
            QRect errRect = frame.rect();
            errRect.adjust(0, 40, 0, 80);
            painter.drawText(errRect, Qt::AlignHCenter | Qt::AlignTop, errorText);
        }
        painter.end();

        client.present(frame.constBits(), w, h);
    };

    // 请求锁定。首帧由 configure 回调（ack 后）提交；locked 后启动时钟刷新。
    client.setConfiguredCallback(render);
    client.lock([&]() {
        auto* timer = new QTimer(&app);
        QObject::connect(timer, &QTimer::timeout, &app, render);
        timer->start(1000);
    });

    // 合成器结束锁定（外部解锁/会话结束）：退出锁屏进程。
    client.setFinishedCallback([&]() {
        app.quit();
    });

    // 键盘处理（KDE-GAP #4：密码输入 + PAM 验证）。
    client.setKeySymCallback([&](xkb_keysym_t sym, bool pressed) {
        if (!pressed) {
            return;
        }
        // 控制键：退格/回车单独处理；其余控制键（Tab/Escape/方向键等）忽略。
        if (sym == XKB_KEY_BackSpace) {
            if (!password.empty()) {
                password.pop_back();
            }
            render();
            return;
        }
        if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
            // 审查 L3：空密码也走一次 PAM（nullok 账户可解锁；无密码账户
            // 文档注明）。
            const auto res = w10de::lock::pamAuthenticate(
                w10de::lock::currentUsername(), password);
            if (res == w10de::lock::PamResult::Ok) {
                doUnlock();
                return;
            }
            // 审查 M1：失败后擦除密码。
            std::fill(password.begin(), password.end(), '\0');
            password.clear();
            if (res == w10de::lock::PamResult::NoPermission) {
                errorText = QStringLiteral("验证服务不可用（w10lock 需 root 权限）");
                pamAvailable = false;
                pamProbed = true;
            } else {
                errorText = QStringLiteral("密码错误，请重试");
                // 审查 M2：失败延迟（防脚本暴力尝试）。
                QThread::msleep(500);
            }
            render();
            return;
        }
        // PAM 可用性探测（审查 M4：仅一次并缓存）。
        if (!pamProbed) {
            pamProbed = true;
            const auto res = w10de::lock::pamAuthenticate(
                w10de::lock::currentUsername(), std::string());
            // 审查 S1：fail-closed——PAM 不可用（非 root）时不提供任意键
            // 解锁（安全）；唯一出口为系统控制台/会话重启。
            pamAvailable = (res != w10de::lock::PamResult::NoPermission);
            if (!pamAvailable) {
                errorText = QStringLiteral(
                    "验证服务不可用（w10lock 需 root 权限），无法从锁屏解锁——"
                    "请通过系统控制台（Ctrl+Alt+F1）或重启会话");
                render();
                return;
            }
            errorText.clear();
        }
        // 审查 S2：用 xkb_keysym_to_utf8 收集可打印字符（覆盖特殊符号与
        // 小键盘数字；原白名单漏掉 - _ ! @ 等与 KP_0..9）。
        if (password.size() < 64) {
            char buf[8] = {};
            const int n = xkb_keysym_to_utf8(sym, buf, sizeof(buf));
            if (n > 0) {
                // 只接受可打印字符（跳过控制字符）。
                bool printable = true;
                for (int i = 0; i < n; ++i) {
                    const unsigned char c = static_cast<unsigned char>(buf[i]);
                    if (c < 0x20) {
                        printable = false;
                        break;
                    }
                }
                if (printable) {
                    password.append(buf, static_cast<size_t>(n));
                }
            }
        }
        render();
    });

    return app.exec();
}
