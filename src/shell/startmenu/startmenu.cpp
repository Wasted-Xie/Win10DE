#include "startmenu/startmenu.h"

#include <QIcon>
#include <QKeyEvent>
#include <QListWidget>
#include <QProcess>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "startmenu/appmodel.h"
#include "theme/colors.h"

namespace w10de {

namespace {

// 清理 Exec 中的字段码（%f/%F/%u/%U/%i/%c/%k 等）：M3 不传文件参数。
// 单次正则回调替换：%%（字面 %）与字段码互不干扰（先整体还原再删除会
// 误删 %%f 这类组合）。
QString sanitizeExec(const QString& exec) {
    QString cmd = exec;
    cmd.replace(QRegularExpression(QStringLiteral("%%|%[fFuUdDnNickvm]")),
                [](const QRegularExpressionMatch& match) {
                    return match.captured(0) == QStringLiteral("%%")
                        ? QStringLiteral("%")
                        : QString();
                });
    return cmd.trimmed();
}

}  // namespace

StartMenu::StartMenu(QWidget* parent) : QWidget(parent) {
    setMinimumSize(480, 600);
    setStyleSheet(QStringLiteral("QWidget { background: %1; }")
                      .arg(theme::kStartMenuBackground.name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(8);

    // 应用磁贴网格（Win10 风格：大图标 + 名称）。
    appGrid_ = new QListWidget(this);
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
    layout->addWidget(appGrid_, 1);

    rebuildAppList();

    // 磁贴单击启动（Win10 交互）；不连 itemActivated 避免双击重复启动。
    connect(appGrid_, &QListWidget::itemClicked,
            this, &StartMenu::launchApplication);
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
