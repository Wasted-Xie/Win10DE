// storewindow.cpp —— 软件中心 UI 实现。

#include "systemapps/software/storewindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include "theme/colors.h"

namespace w10de::software {

namespace {

QString sourceLabel(AppSource src) {
    switch (src) {
    case AppSource::Flatpak:
        return QStringLiteral("Flatpak");
    case AppSource::User:
        return QStringLiteral("用户");
    default:
        return QStringLiteral("系统");
    }
}

}  // namespace

StoreWindow::StoreWindow(QWidget* parent) : QMainWindow(parent) {
    apps_ = SoftwareStore::listInstalled();
    flatpakCli_ = SoftwareStore::flatpakAvailable();  // 轻微 L4：构造时检测
    setWindowTitle(QStringLiteral("软件中心"));
    resize(860, 560);
    buildUi();
    rebuildList();
}

void StoreWindow::buildUi() {
    auto* central = new QWidget(this);
    // 审查 M5：统一深色主题样式（与系统其他应用一致；Qt 默认浅色 palette
    // 会造成"整窗浅色 + 个别主题灰字"混搭）。
    central->setStyleSheet(QStringLiteral(
        "QWidget { background: %1; color: %2; }"
        "QLineEdit, QListWidget { background: %3; color: %2;"
        "  border: 1px solid %4; }"
        "QPushButton { background: %4; color: %2; border: none;"
        "  padding: 4px 14px; border-radius: 2px; }"
        "QPushButton:hover { background: %5; }"
        "QPushButton:disabled { color: %6; }"
        "QListWidget::item { padding: 6px; }"
        "QListWidget::item:selected { background: %7; }")
        .arg(theme::kStartMenuBackground().name(),
             theme::kTextPrimary().name(),
             theme::kTaskbarBackground().name(),
             theme::kHoverBackground().name(),
             theme::kPressedBackground().name(),
             theme::kTextSecondary().name(),
             theme::kAccentBlue().name()));
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    // 顶部：搜索 + 计数。
    auto* topRow = new QHBoxLayout;
    search_ = new QLineEdit(central);
    search_->setPlaceholderText(QStringLiteral("搜索应用…"));
    search_->setClearButtonEnabled(true);
    topRow->addWidget(search_, 1);
    countLabel_ = new QLabel(central);
    topRow->addWidget(countLabel_);
    root->addLayout(topRow);
    connect(search_, &QLineEdit::textChanged,
            this, &StoreWindow::onSearchChanged);

    // 主体：左网格 + 右详情。
    auto* splitter = new QSplitter(central);
    grid_ = new QListWidget(splitter);
    grid_->setViewMode(QListView::IconMode);
    grid_->setIconSize(QSize(48, 48));
    grid_->setGridSize(QSize(120, 96));
    grid_->setWordWrap(true);
    grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static);
    splitter->addWidget(grid_);
    connect(grid_, &QListWidget::currentItemChanged,
            this, &StoreWindow::onAppSelected);

    auto* detail = new QWidget(splitter);
    auto* dl = new QVBoxLayout(detail);
    dl->setContentsMargins(12, 12, 12, 12);
    detailIcon_ = new QLabel(detail);
    detailIcon_->setFixedSize(64, 64);
    detailIcon_->setAlignment(Qt::AlignCenter);
    dl->addWidget(detailIcon_);
    detailName_ = new QLabel(detail);
    detailName_->setStyleSheet(QStringLiteral("font-size: 17px; font-weight: bold;"));
    detailName_->setWordWrap(true);
    dl->addWidget(detailName_);
    detailComment_ = new QLabel(detail);
    detailComment_->setWordWrap(true);
    dl->addWidget(detailComment_);
    detailMeta_ = new QLabel(detail);
    detailMeta_->setStyleSheet(QStringLiteral("color: %1;")
        .arg(theme::kTextSecondary().name()));
    detailMeta_->setWordWrap(true);
    dl->addWidget(detailMeta_);

    launchBtn_ = new QPushButton(QStringLiteral("启动"), detail);
    uninstallBtn_ = new QPushButton(QStringLiteral("卸载"), detail);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(launchBtn_);
    btnRow->addWidget(uninstallBtn_);
    btnRow->addStretch(1);
    dl->addLayout(btnRow);
    connect(launchBtn_, &QPushButton::clicked, this, &StoreWindow::launchSelected);
    connect(uninstallBtn_, &QPushButton::clicked, this, &StoreWindow::uninstallSelected);

    status_ = new QLabel(detail);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color: %1;")
        .arg(theme::kTextSecondary().name()));
    dl->addWidget(status_);
    dl->addStretch(1);
    splitter->addWidget(detail);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);
    setCentralWidget(central);
}

void StoreWindow::rebuildList() {
    grid_->clear();
    const QString filter = search_->text().trimmed();
    visible_ = 0;
    for (const AppInfo& app : apps_) {
        if (!filter.isEmpty() &&
                !app.name.contains(filter, Qt::CaseInsensitive) &&
                !app.comment.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        auto* item = new QListWidgetItem(
            QIcon(app.icon), app.name, grid_);  // 轻微 L5：空路径 QIcon() 即可
        item->setData(Qt::UserRole, app.id);
        item->setToolTip(app.comment);
        ++visible_;
    }
    countLabel_->setText(QStringLiteral("已安装 %1 个应用").arg(visible_));
    if (grid_->count() > 0) {
        grid_->setCurrentRow(0);
    } else {
        detailName_->clear();
        detailComment_->clear();
        detailMeta_->clear();
        detailIcon_->setPixmap(QPixmap());
        launchBtn_->setEnabled(false);
        uninstallBtn_->setEnabled(false);
        selectedId_.clear();
    }
}

void StoreWindow::onSearchChanged(const QString&) {
    rebuildList();
}

void StoreWindow::onAppSelected(QListWidgetItem* item) {
    if (item == nullptr) {
        // 轻微 L6：清空选择状态（防御性；当前重建流程保证不触发）。
        selectedId_.clear();
        detailName_->clear();
        detailComment_->clear();
        detailMeta_->clear();
        detailIcon_->setPixmap(QPixmap());
        launchBtn_->setEnabled(false);
        uninstallBtn_->setEnabled(false);
        return;
    }
    selectedId_ = item->data(Qt::UserRole).toString();
    for (const AppInfo& app : apps_) {
        if (app.id == selectedId_) {
            showDetails(&app);
            return;
        }
    }
}

void StoreWindow::showDetails(const AppInfo* app) {
    detailName_->setText(app->name);
    detailComment_->setText(app->comment);
    if (!app->icon.isEmpty()) {
        detailIcon_->setPixmap(QPixmap(app->icon).scaled(
            64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        detailIcon_->setPixmap(QPixmap());
    }
    detailMeta_->setText(
        QStringLiteral("来源：%1\n类别：%2\n命令：%3")
            .arg(sourceLabel(app->source),
                 app->category.isEmpty() ? QStringLiteral("—") : app->category,
                 app->exec));
    launchBtn_->setEnabled(!app->exec.isEmpty());
    uninstallBtn_->setEnabled(app->source == AppSource::Flatpak &&
                              !app->flatpakId.isEmpty() && flatpakCli_);
    status_->clear();
}

void StoreWindow::launchSelected() {
    for (const AppInfo& app : apps_) {
        if (app.id == selectedId_) {
            if (SoftwareStore::launch(app)) {
                status_->setText(QStringLiteral("已启动 %1。").arg(app.name));
            } else {
                status_->setText(QStringLiteral("启动失败：%1").arg(app.exec));
            }
            return;
        }
    }
}

void StoreWindow::uninstallSelected() {
    for (const AppInfo& app : apps_) {
        if (app.id == selectedId_) {
            if (app.source != AppSource::Flatpak) {
                status_->setText(QStringLiteral("系统/用户应用不可在此卸载（包管理器管理）。"));
                return;
            }
            if (!flatpakCli_) {
                status_->setText(QStringLiteral("flatpak 不可用，无法卸载。"));
                return;
            }
            const auto reply = QMessageBox::question(
                this, QStringLiteral("卸载"),
                QStringLiteral("卸载 %1？").arg(app.name));
            if (reply != QMessageBox::Yes) {
                return;
            }
            // 审查 M4：异步卸载（QProcess 不阻塞 GUI；完成回调刷新列表）。
            status_->setText(QStringLiteral("正在卸载 %1…").arg(app.name));
            uninstallBtn_->setEnabled(false);
            if (uninstallProc_ == nullptr) {
                uninstallProc_ = new QProcess(this);
                connect(uninstallProc_, &QProcess::finished,
                        this, &StoreWindow::onUninstallFinished);
            }
            uninstallProc_->start(QStringLiteral("flatpak"),
                                  {QStringLiteral("uninstall"), QStringLiteral("-y"),
                                   app.flatpakId});
            return;
        }
    }
}

void StoreWindow::onUninstallFinished(int /*exitCode*/, QProcess::ExitStatus status) {
    const bool ok = status == QProcess::NormalExit &&
                    uninstallProc_->exitCode() == 0;
    if (ok) {
        status_->setText(QStringLiteral("已卸载。"));
        apps_ = SoftwareStore::listInstalled();
        rebuildList();
    } else {
        const QString err =
            QString::fromUtf8(uninstallProc_->readAllStandardError()).trimmed();
        status_->setText(err.isEmpty()
            ? QStringLiteral("卸载失败。")
            : QStringLiteral("卸载失败：%1").arg(err));
        uninstallBtn_->setEnabled(true);
    }
}

}  // namespace w10de::software
