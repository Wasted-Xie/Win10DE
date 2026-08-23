#include "tray/trayarea.h"

#include <QHBoxLayout>

#include "tray/sniwatcher.h"
#include "tray/trayicon.h"

namespace w10de {

TrayArea::TrayArea(QWidget* parent) : QWidget(parent) {
    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(4, 0, 4, 0);
    layout_->setSpacing(2);

    // 注册 SNI watcher（宿主）。
    watcher_ = new SniWatcher(this);
    if (watcher_->isRegistered()) {
        connect(watcher_, &SniWatcher::StatusNotifierItemRegistered,
                this, &TrayArea::onItemRegistered);
        connect(watcher_, &SniWatcher::StatusNotifierItemUnregistered,
                this, &TrayArea::onItemUnregistered);
        // 已注册项（watcher 启动前注册的，正常无——本进程是新 watcher）。
        for (const QString& service : watcher_->RegisteredStatusNotifierItems()) {
            onItemRegistered(service);
        }
    }
}

void TrayArea::onItemRegistered(const QString& service) {
    if (icons_.contains(service)) {
        return;
    }
    auto* icon = new TrayIcon(service, this);
    icons_.insert(service, icon);
    layout_->addWidget(icon);
}

void TrayArea::onItemUnregistered(const QString& service) {
    TrayIcon* icon = icons_.take(service);
    if (icon != nullptr) {
        layout_->removeWidget(icon);
        icon->deleteLater();
    }
}

}  // namespace w10de
