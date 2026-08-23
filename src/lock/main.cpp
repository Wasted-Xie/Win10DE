// w10lock —— 锁屏进程（ext-session-lock-v1，M6）
//
// QGuiApplication 提供 Wayland display；锁屏画面用 QPainter 离屏渲染到
// QImage，再提交到 session-lock surface（wl_shm buffer）。
// MVP：任意键解锁（不验证密码，PAM 集成后续里程碑）。

#include <QDateTime>
#include <QFont>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTimer>

#include <QtGui/qpa/qplatformnativeinterface.h>

#include <cstdio>

#include "lock/lockclient.h"

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("w10lock"));

    // 获取 Wayland display（Qt 平台集成提供；锁定期间无普通窗口）。
    auto* waylandApp =
        QGuiApplication::nativeInterface<QNativeInterface::QWaylandApplication>();
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

    // 渲染一帧锁屏画面（Win10 风格：蓝底 + 大时钟 + 日期）。
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

    // MVP：任意键解锁（后续里程碑接入密码验证）。
    client.setKeyCallback([&]() {
        client.unlock();
        // 协议要求：解锁后退出前必须 sync，确保合成器收到 unlock_and_destroy。
        wl_display_roundtrip(display);
        app.quit();
    });

    return app.exec();
}
