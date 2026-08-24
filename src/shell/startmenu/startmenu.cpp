#include "startmenu/startmenu.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "startmenu/appmodel.h"
#include "startmenu/flowlayout.h"
#include "theme/colors.h"

namespace w10de {

namespace {

// 清理 Exec 中的字段码（%f/%F/%u/%U/%i/%c/%k 等）：M3 不传文件参数。
// Qt 的 QString::replace 不支持回调替换，用 QRegularExpression::globalMatch
// 手动拼接：%%（字面 %）与字段码互不干扰（先整体还原再删除会误删 %%f
// 这类组合）。
QString sanitizeExec(const QString& exec) {
    const QRegularExpression re(QStringLiteral("%%|%[fFuUdDnNickvm]"));
    QString out;
    int last = 0;
    QRegularExpressionMatchIterator it = re.globalMatch(exec);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        out += exec.mid(last, match.capturedStart() - last);
        out += match.captured(0) == QStringLiteral("%%") ? QStringLiteral("%")
                                                         : QString();
        last = match.capturedEnd();
    }
    out += exec.mid(last);
    return out.trimmed();
}

}  // namespace

StartMenu::StartMenu(QWidget* parent) : QWidget(parent) {
    setMinimumSize(kSidebarWidth + kAppListWidth + kTilesWidth, 600);
    setStyleSheet(QStringLiteral("QWidget { background: %1; }")
                      .arg(theme::kStartMenuBackground().name()));

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左侧窄栏（Win10：与开始按钮等宽 48px）----
    sidebar_ = new QWidget(this);
    sidebar_->setFixedWidth(kSidebarWidth);
    sidebar_->setStyleSheet(QStringLiteral(
        "QWidget { background: %1; }")  // 主题化（深色略深 / 浅色浅灰）
        .arg(w10de::theme::kMenuSidebar().name()));
    auto* sb = new QVBoxLayout(sidebar_);
    sb->setContentsMargins(0, 0, 0, 0);
    sb->setSpacing(0);

    // 顶部：三条横杠汉堡按钮（展开/折叠左侧栏）。
    hamburgerBtn_ = makeSideButton(QString(), QStringLiteral("☰"));
    hamburgerBtn_->setToolTip(QStringLiteral("展开/折叠侧栏"));
    connect(hamburgerBtn_, &QToolButton::clicked,
            this, &StartMenu::toggleSidebar);
    sb->addWidget(hamburgerBtn_);
    sb->addStretch(1);

    // 底部功能区：账户按钮（最上方）→ 功能按钮 → 电源按钮（最底）。
    accountBtn_ = makeSideButton(QStringLiteral("user-identity"),
                                 QStringLiteral("账户"));
    connect(accountBtn_, &QToolButton::clicked, this, [this]() {
        // 账户按钮 → 锁屏联动：D-Bus 调 org.w10de.Shell.Lock()
        // （与外部 dbus-send 触发同路径；LockService 启动 w10lock）。
        if (QDBusConnection::sessionBus().isConnected()) {
            QDBusInterface iface(QStringLiteral("org.w10de.Shell"),
                                 QStringLiteral("/Shell"),
                                 QStringLiteral("org.w10de.Shell"),
                                 QDBusConnection::sessionBus());
            if (iface.isValid()) {
                iface.call(QStringLiteral("Lock"));
            } else {
                qWarning() << "startmenu: org.w10de.Shell not available";
            }
        }
    });
    sb->addWidget(accountBtn_);

    auto* settingsBtn = makeSideButton(QStringLiteral("preferences-system"),
                                       QStringLiteral("设置"));
    connect(settingsBtn, &QToolButton::clicked, this, [this]() {
        qInfo() << "startmenu: settings clicked (TODO: systemsettings)";
    });
    sb->addWidget(settingsBtn);

    auto* docsBtn = makeSideButton(QStringLiteral("folder-documents"),
                                   QStringLiteral("文档"));
    connect(docsBtn, &QToolButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::homePath()));
    });
    sb->addWidget(docsBtn);

    auto* picsBtn = makeSideButton(QStringLiteral("folder-pictures"),
                                   QStringLiteral("图片"));
    connect(picsBtn, &QToolButton::clicked, this, []() {
        const QString pics = QDir::homePath() + QStringLiteral("/Pictures");
        QDesktopServices::openUrl(QUrl::fromLocalFile(
            QDir(pics).exists() ? pics : QDir::homePath()));
    });
    sb->addWidget(picsBtn);

    powerBtn_ = makeSideButton(QStringLiteral("system-shutdown"),
                               QStringLiteral("电源"));
    powerBtn_->setToolTip(QStringLiteral("关机 / 重启 / 睡眠"));
    connect(powerBtn_, &QToolButton::clicked,
            this, &StartMenu::showPowerMenu);
    sb->addWidget(powerBtn_);

    root->addWidget(sidebar_);

    // ---- 应用列表列（Win10：5×开始按钮宽，全部应用文本列表）----
    auto* listHost = new QWidget(this);
    listHost->setFixedWidth(kAppListWidth);
    auto* ll = new QVBoxLayout(listHost);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(0);

    appList_ = new QListWidget(listHost);
    appList_->setStyleSheet(QStringLiteral(
        "QListWidget {"
        "  background: transparent;"
        "  border: none;"
        "  color: %1;"
        "  font-size: 13px;"
        "}"
        "QListWidget::item { padding: 6px 8px; }"
        "QListWidget::item:hover { background: %2; }"
        "QListWidget::item:selected { background: %3; }")
        .arg(theme::kTextPrimary().name(),
             theme::kHoverBackground().name(),
             theme::kPressedBackground().name()));
    ll->addWidget(appList_, 1);
    root->addWidget(listHost);

    // ---- 磁贴区（Win10：6 小磁贴宽 + 7×4px 间隙 = 316px，可滚动磁贴流）----
    auto* tileHost = new QWidget(this);
    tileHost->setFixedWidth(kTilesWidth);
    auto* tl = new QVBoxLayout(tileHost);
    // 左右边距 4px（磁贴与磁贴区边缘间隔，小磁贴网格标准）。
    tl->setContentsMargins(4, 12, 4, 12);
    tl->setSpacing(8);

    auto* scrollTitle = new QLabel(QStringLiteral("磁贴"), tileHost);
    scrollTitle->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 13px; font-weight: bold; }")
        .arg(theme::kTextSecondary().name()));
    tl->addWidget(scrollTitle);

    // 磁贴流直接挂在 288px 固定宽宿主上（不用 QScrollArea：其 viewport 初始
    // 100px 宽会在显示时序中反复覆盖布局，导致磁贴垂直堆叠——真实运行验证）。
    tilesHost_ = new QWidget(tileHost);
    tilesHost_->setStyleSheet(QStringLiteral("QWidget { background: transparent; }"));
    // 小磁贴网格标准：磁贴间隔 4px（水平），行间距 4px（垂直）。
    auto* flow = new FlowLayout(tilesHost_, 4, 4);
    flow->setContentsMargins(0, 0, 0, 0);
    tl->addWidget(tilesHost_, 1);
    root->addWidget(tileHost);

    rebuildAppList();

    // 磁贴点击启动；应用列表点击启动。
    connect(appList_, &QListWidget::itemClicked,
            this, &StartMenu::launchApplication);
}

QToolButton* StartMenu::makeSideButton(const QString& iconName,
                                       const QString& text) {
    auto* btn = new QToolButton(sidebar_);
    const QIcon icon = QIcon::fromTheme(iconName);
    if (!icon.isNull()) {
        btn->setIcon(icon);
        btn->setIconSize(QSize(22, 22));
    }
    // 无主题图标时按钮文字本身作为图标（如 ☰/回退）。
    btn->setText(text);
    btn->setFixedSize(kSidebarWidth, kSidebarButtonHeight);
    btn->setAutoRaise(true);
    btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    btn->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  color: %1;"
        "  background: transparent;"
        "  border: none;"
        "  padding: 0;"
        "  font-size: 13px;"
        "}"
        "QToolButton:hover { background: %2; }"
        "QToolButton:pressed { background: %3; }")
        .arg(theme::kTextPrimary().name(),
             theme::kHoverBackground().name(),
             theme::kPressedBackground().name()));
    sideButtons_.append(btn);
    return btn;
}

void StartMenu::toggle() {
    setVisible(!isVisible());
    if (isVisible()) {
        // 显示后请求键盘焦点（overlay 层已配置 keyboard-interactivity）。
        appList_->setFocus();
        raise();
        activateWindow();
    }
}

void StartMenu::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        return;
    }
    QWidget::keyPressEvent(event);
}

void StartMenu::toggleSidebar() {
    sidebarExpanded_ = !sidebarExpanded_;
    const int w = sidebarExpanded_ ? kSidebarExpandedWidth : kSidebarWidth;
    sidebar_->setFixedWidth(w);
    for (QToolButton* b : sideButtons_) {
        b->setFixedSize(w, kSidebarButtonHeight);
        b->setToolButtonStyle(sidebarExpanded_
                                  ? Qt::ToolButtonTextBesideIcon
                                  : Qt::ToolButtonIconOnly);
    }
}

void StartMenu::showPowerMenu() {
    QMenu menu(this);
    // 菜单宽与左侧栏（展开区域）等宽。
    menu.setMinimumWidth(sidebar_->width());
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: %1; color: %2; border: 1px solid %3; }"
        "QMenu::item { padding: 6px 16px; }"
        "QMenu::item:selected { background: %4; }")
        .arg(theme::kStartMenuBackground().name(),
             theme::kTextPrimary().name(),
             theme::kHoverBackground().name(),
             theme::kPressedBackground().name()));
    QAction* shutdown = menu.addAction(QStringLiteral("关机"));
    QAction* reboot = menu.addAction(QStringLiteral("重启"));
    QAction* sleep = menu.addAction(QStringLiteral("睡眠"));
    // 在电源按钮上方弹出。
    QAction* chosen = menu.exec(
        powerBtn_->mapToGlobal(QPoint(0, -menu.sizeHint().height())));

    // 电源动作：systemd（systemctl）。无 systemd 环境（如 WSL）仅告警。
    const QString systemctl = QStandardPaths::findExecutable(QStringLiteral("systemctl"));
    if (systemctl.isEmpty()) {
        qWarning() << "startmenu: systemctl not found (systemd unavailable?)";
        return;
    }
    if (chosen == shutdown) {
        QProcess::startDetached(systemctl, {QStringLiteral("poweroff")});
    } else if (chosen == reboot) {
        QProcess::startDetached(systemctl, {QStringLiteral("reboot")});
    } else if (chosen == sleep) {
        QProcess::startDetached(systemctl, {QStringLiteral("suspend")});
    }
}

void StartMenu::launchTile(const QString& exec) {
    if (exec.isEmpty()) {
        return;
    }
    // 分离式启动，不阻塞 shell。
    const QStringList parts = QProcess::splitCommand(sanitizeExec(exec));
    if (parts.isEmpty()) {
        return;
    }
    QProcess::startDetached(parts.first(), parts.mid(1));
    hide();  // 启动后收起开始菜单
}

void StartMenu::launchApplication(QListWidgetItem* item) {
    const QString exec = item->data(Qt::UserRole).toString();
    if (exec.isEmpty()) {
        return;
    }
    // 分离式启动，不阻塞 shell。
    const QStringList parts = QProcess::splitCommand(sanitizeExec(exec));
    if (parts.isEmpty()) {
        return;
    }
    QProcess::startDetached(parts.first(), parts.mid(1));
    hide();  // 启动后收起开始菜单
}

void StartMenu::rebuildAppList() {
    appList_->clear();
    // 清空磁贴流（FlowLayout takeAt 删除 item 与 widget）。
    if (auto* flow = qobject_cast<FlowLayout*>(tilesHost_->layout())) {
        while (QLayoutItem* item = flow->takeAt(0)) {
            delete item->widget();
            delete item;
        }
    }
    tiles_.clear();
    const QList<AppEntry> apps = scanDesktopApplications();
    for (const AppEntry& app : apps) {
        // 应用列表列：文本行（Win10 全部应用）。
        auto* row = new QListWidgetItem(app.name);
        row->setData(Qt::UserRole, app.exec);
        row->setToolTip(app.name);
        appList_->addItem(row);

        // 磁贴：默认中尺寸（右键可自由设置为小/大/宽）。
        auto* tile = new TileButton(app.name, app.icon, app.exec, tilesHost_);
        tile->setTileSize(TileButton::TileSize::Medium);
        connect(tile, &TileButton::launchRequested,
                this, &StartMenu::launchTile);
        connect(tile, &TileButton::sizeChanged, tilesHost_, [this]() {
            // 磁贴尺寸变化后重排（FlowLayout 依赖 sizeHint 变化）。
            tilesHost_->layout()->invalidate();
            tilesHost_->adjustSize();
        });
        qobject_cast<FlowLayout*>(tilesHost_->layout())->addWidget(tile);
        tiles_.append(tile);
    }
    tilesHost_->adjustSize();
}

}  // namespace w10de
