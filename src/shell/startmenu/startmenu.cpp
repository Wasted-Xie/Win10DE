#include "startmenu/startmenu.h"

#include <QDesktopServices>
#include <QDir>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QListWidget>
#include <QMenu>
#include <QProcess>
#include <QRegularExpression>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "startmenu/appmodel.h"
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
    setMinimumSize(480, 600);
    setStyleSheet(QStringLiteral("QWidget { background: %1; }")
                      .arg(theme::kStartMenuBackground.name()));

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左侧窄栏（Win10：与开始按钮等宽 48px）----
    sidebar_ = new QWidget(this);
    sidebar_->setFixedWidth(kSidebarWidth);
    sidebar_->setStyleSheet(QStringLiteral(
        "QWidget { background: #171717; }"));  // 比主区略深（Win10 左侧栏）
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
        // MVP 占位：账户/锁定/注销菜单为后续里程碑（PAM）。
        qInfo() << "startmenu: account clicked (TODO)";
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

    // ---- 主区域：应用磁贴网格（Win10 风格：大图标 + 名称）----
    auto* content = new QWidget(this);
    auto* cl = new QVBoxLayout(content);
    cl->setContentsMargins(12, 12, 12, 12);
    cl->setSpacing(8);

    appGrid_ = new QListWidget(content);
    appGrid_->setViewMode(QListView::IconMode);
    appGrid_->setIconSize(QSize(48, 48));
    appGrid_->setGridSize(QSize(110, 110));
    appGrid_->setResizeMode(QListView::Adjust);
    appGrid_->setMovement(QListView::Static);
    appGrid_->setWordWrap(true);
    appGrid_->setStyleSheet(QStringLiteral(
        "QListWidget {"
        "  background: transparent;"
        "  border: none;"
        "  color: %1;"
        "  font-size: 12px;"
        "}"
        "QListWidget::item { padding: 4px; }"
        "QListWidget::item:hover { background: %2; }"
        "QListWidget::item:selected { background: %3; }")
        .arg(theme::kTextPrimary.name(),
             theme::kHoverBackground.name(),
             theme::kPressedBackground.name()));
    cl->addWidget(appGrid_, 1);
    root->addWidget(content, 1);

    rebuildAppList();

    // 磁贴单击启动（Win10 交互）；不连 itemActivated 避免双击重复启动。
    connect(appGrid_, &QListWidget::itemClicked,
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
        .arg(theme::kTextPrimary.name(),
             theme::kHoverBackground.name(),
             theme::kPressedBackground.name()));
    sideButtons_.append(btn);
    return btn;
}

void StartMenu::toggle() {
    setVisible(!isVisible());
    if (isVisible()) {
        // 显示后请求键盘焦点（overlay 层已配置 keyboard-interactivity）。
        appGrid_->setFocus();
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
        .arg(theme::kStartMenuBackground.name(),
             theme::kTextPrimary.name(),
             theme::kHoverBackground.name(),
             theme::kPressedBackground.name()));
    QAction* shutdown = menu.addAction(QStringLiteral("关机"));
    QAction* reboot = menu.addAction(QStringLiteral("重启"));
    QAction* sleep = menu.addAction(QStringLiteral("睡眠"));
    // 在电源按钮上方弹出。
    QAction* chosen = menu.exec(
        powerBtn_->mapToGlobal(QPoint(0, -menu.sizeHint().height())));
    // MVP：动作占位（真机接入 systemctl/PAM 为后续里程碑）。
    if (chosen == shutdown) {
        qInfo() << "startmenu: shutdown requested (TODO)";
    } else if (chosen == reboot) {
        qInfo() << "startmenu: reboot requested (TODO)";
    } else if (chosen == sleep) {
        qInfo() << "startmenu: sleep requested (TODO)";
    }
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
    appGrid_->clear();
    const QList<AppEntry> apps = scanDesktopApplications();
    for (const AppEntry& app : apps) {
        auto* item = new QListWidgetItem(QIcon::fromTheme(app.icon), app.name);
        item->setData(Qt::UserRole, app.exec);
        item->setToolTip(app.name);
        appGrid_->addItem(item);
    }
}

}  // namespace w10de
