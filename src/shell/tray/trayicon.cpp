#include "tray/trayicon.h"

#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QImage>
#include <QMenu>
#include <QPixmap>

namespace w10de {

namespace {

// 解析 SNI 图标属性：IconName（主题名）或 IconPixmap（a(iiay)）。
QIcon parseIconPixmap(const QDBusArgument& arg) {
    // 类型校验：必须是数组。
    if (arg.currentType() != QDBusArgument::ArrayType) {
        return QIcon();
    }
    // 取最大缩放级别（第一个通常即最大，但显式比较更稳）。
    QIcon result;
    int bestArea = 0;
    arg.beginArray();
    while (!arg.atEnd()) {
        arg.beginStructure();
        int width = 0, height = 0;
        QByteArray data;
        arg >> width >> height >> data;
        arg.endStructure();
        if (width > 0 && height > 0 && !data.isEmpty()) {
            // SNI 像素数据为 ARGB32 原始（与 Qt Format_ARGB32 字节序一致）。
            QImage image(reinterpret_cast<const uchar*>(data.constData()),
                         width, height, QImage::Format_ARGB32);
            if (!image.isNull() && width * height > bestArea) {
                result = QIcon(QPixmap::fromImage(image));
                bestArea = width * height;
            }
        }
    }
    arg.endArray();
    return result;
}

}  // namespace

TrayIcon::TrayIcon(const QString& service, QWidget* parent)
    : QToolButton(parent), service_(service) {
    // service 形如 "org.foo.Item/123/StatusNotifierItem" 或 "/StatusNotifierItem"。
    const int slash = service.indexOf(QLatin1Char('/'));
    if (slash > 0) {
        // 含 bus 名的完整形式：拆成 service + path（保留前导 /，D-Bus 路径
        // 必须以 / 开头，否则全部功能失效）。
        path_ = service.mid(slash);
        item_ = new QDBusInterface(service.left(slash), path_,
                                   QStringLiteral("org.kde.StatusNotifierItem"),
                                   QDBusConnection::sessionBus(), this);
    } else {
        // 仅路径：服务名为调用者（由 watcher 已拼接，理论上不会到这）。
        path_ = service;
        item_ = nullptr;
    }

    setIconSize(QSize(20, 20));
    setAutoRaise(true);
    setToolTip(service_);
    refreshFromItem();

    connect(this, &QToolButton::clicked, this, &TrayIcon::onActivate);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested,
            this, &TrayIcon::onContextMenu);

    // 属性变化时刷新（item 的 PropertiesChanged 信号）。
    if (item_ != nullptr) {
        QDBusConnection::sessionBus().connect(
            item_->service(), item_->path(),
            QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("PropertiesChanged"), this,
            SLOT(refreshFromItem()));
    }
}

void TrayIcon::refreshFromItem() {
    if (item_ == nullptr) {
        return;
    }
    const QIcon icon = iconFromItem();
    if (!icon.isNull()) {
        setIcon(icon);
    }
    const QString title = item_->property("Title").toString();
    if (!title.isEmpty()) {
        setToolTip(title);
    }
    // 更新可见性：空图标的无效项隐藏。
    setVisible(!icon.isNull());
}

QIcon TrayIcon::iconFromItem() const {
    if (item_ == nullptr) {
        return QIcon();
    }
    // 优先 IconName（主题查找），其次 IconPixmap。
    const QString iconName = item_->property("IconName").toString();
    if (!iconName.isEmpty()) {
        const QIcon themed = QIcon::fromTheme(iconName);
        if (!themed.isNull()) {
            return themed;
        }
    }
    const QDBusArgument pixmapArg = item_->property("IconPixmap").value<QDBusArgument>();
    if (pixmapArg.currentType() != QDBusArgument::InvalidType) {
        return parseIconPixmap(pixmapArg);
    }
    return QIcon();
}

void TrayIcon::onActivate() {
    if (item_ == nullptr) {
        return;
    }
    // Activate(x, y)：坐标 0 表示由合成器决定。
    item_->asyncCall(QStringLiteral("Activate"), 0, 0);
}

void TrayIcon::onContextMenu() {
    if (item_ == nullptr) {
        return;
    }
    item_->asyncCall(QStringLiteral("ContextMenu"), 0, 0);
}

// SecondaryActivate（中键）未接入：MVP 仅左键 Activate + 右键 ContextMenu。

}  // namespace w10de
