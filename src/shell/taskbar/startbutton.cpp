#include "taskbar/startbutton.h"

#include <QFile>
#include <QIcon>

#include "theme/colors.h"

namespace w10de {

namespace {

// 发行版 logo 图标（跨发行版 fallback 链）：
//   1. freedesktop 标准图标名 distributor-logo（多数发行版在图标主题提供）；
//   2. 已知发行版 logo 路径表（按常见发行版排列）；
//   3. 均无则返回空图标（调用方回退文字）。
QIcon distroLogoIcon() {
    const QIcon themed = QIcon::fromTheme(QStringLiteral("distributor-logo"));
    if (!themed.isNull()) {
        return themed;
    }
    static const char* kKnownPaths[] = {
        "/usr/share/pixmaps/archlinux-logo.svg",   // Arch Linux
        "/usr/share/pixmaps/fedora-logo.svg",      // Fedora
        "/usr/share/pixmaps/debian-logo.svg",      // Debian
        "/usr/share/pixmaps/ubuntu-logo.png",      // Ubuntu
        "/usr/share/pixmaps/opensuse-logo.svg",    // openSUSE
        "/usr/share/pixmaps/gentoo-logo.svg",      // Gentoo
        "/usr/share/pixmaps/manjaro-logo.svg",     // Manjaro
        "/usr/share/pixmaps/linuxmint-logo.svg",   // Linux Mint
        "/usr/share/pixmaps/centos-logo.svg",      // CentOS/Rocky/Alma
        "/usr/share/pixmaps/endless-logo.svg",     // Endless
    };
    for (const char* p : kKnownPaths) {
        if (QFile::exists(QLatin1String(p))) {
            return QIcon(QLatin1String(p));
        }
    }
    return QIcon();
}

}  // namespace

StartButton::StartButton(QWidget* parent) : QPushButton(parent) {
    const QIcon distroLogo = distroLogoIcon();
    if (!distroLogo.isNull()) {
        setIcon(distroLogo);
        // 图标保持原本大小不变（26x26）；按钮本体 1:1 与任务栏同高。
        setIconSize(QSize(26, 26));
    } else {
        setText(QStringLiteral("开始"));
    }
    // 正方形按钮（1:1，与任务栏同高），贴任务栏最左（布局无外边距）。
    setFixedSize(theme::kTaskbarHeight, theme::kTaskbarHeight);
    setCursor(Qt::PointingHandCursor);
    // Win10 扁平按钮：默认透明，hover 深灰，按下更深。
    // padding:0 消除 QPushButton 默认内容边距（图标贴屏幕最左，
    // 实测默认 3px 间隙）。
    setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  color: %1;"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0;"
        "  font-size: 13px;"
        "}"
        "QPushButton:hover { background: %2; }"
        "QPushButton:pressed { background: %3; }")
        .arg(theme::kTextPrimary.name(),
             theme::kHoverBackground.name(),
             theme::kPressedBackground.name()));
    connect(this, &QPushButton::clicked, this, &StartButton::startMenuRequested);
}

}  // namespace w10de
