#include "systemapps/settings/settingswindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextStream>
#include <QVBoxLayout>

#include "ipc/config.h"
#include "theme/colors.h"

namespace w10de::settings {

namespace {

// 形如 "外观 → 主题" 的导航提示（KDE 风格路径）。
QString breadcrumb(const QString& category, const QString& page) {
    return QStringLiteral("%1 → %2").arg(category, page);
}

}  // namespace

SettingsWindow::SettingsWindow(QWidget* parent) : QMainWindow(parent) {
    buildUi();
    applyTheme();
    setWindowTitle(QStringLiteral("设置"));
    resize(860, 560);
}

QString SettingsWindow::configPath() const {
    return QDir::homePath() + QStringLiteral("/.config/w10de/config.ini");
}

void SettingsWindow::buildUi() {
    // ---- 顶部搜索（KDE：按模块/关键词过滤左侧分类）----
    auto* searchBar = new QWidget(this);
    auto* searchLayout = new QHBoxLayout(searchBar);
    searchLayout->setContentsMargins(8, 6, 8, 6);
    searchBox_ = new QLineEdit(searchBar);
    searchBox_->setPlaceholderText(QStringLiteral("搜索设置…"));
    searchLayout->addWidget(searchBox_);
    setMenuWidget(searchBar);
    connect(searchBox_, &QLineEdit::textChanged,
            this, &SettingsWindow::onSearchChanged);

    // ---- 主体：左侧分类 + 右侧页面 ----
    auto* splitter = new QSplitter(this);
    categoryList_ = new QListWidget(splitter);
    categoryList_->setFixedWidth(200);
    categoryList_->setWordWrap(true);

    pages_ = new QStackedWidget(splitter);
    buildAppearancePage();
    buildSystemPage();
    splitter->addWidget(categoryList_);
    splitter->addWidget(pages_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    setCentralWidget(splitter);

    pathLabel_ = new QLabel(statusBar());
    statusBar()->addWidget(pathLabel_);

    connect(categoryList_, &QListWidget::currentRowChanged,
            this, &SettingsWindow::onCategoryChanged);
    if (categoryList_->count() > 0) {
        categoryList_->setCurrentRow(0);
    }
}

void SettingsWindow::applyTheme() {
    const QColor bg = theme::kStartMenuBackground();
    const QColor fg = theme::kTextPrimary();
    setStyleSheet(QStringLiteral(
        "QMainWindow, QSplitter, QWidget { background: %1; color: %2; }"
        "QLineEdit, QListWidget { background: %3; color: %2;"
        "  border: 1px solid %4; }"
        "QPushButton { background: %4; color: %2; border: none;"
        "  padding: 4px 12px; border-radius: 2px; }"
        "QPushButton:hover { background: %5; }"
        "QListWidget::item { padding: 6px 8px; }"
        "QListWidget::item:selected { background: %6; color: %7; }")
        .arg(bg.name(), fg.name(),
             theme::kTaskbarBackground().name(),
             theme::kHoverBackground().name(),
             theme::kPressedBackground().name(),
             theme::kAccentBlue().name(),
             theme::kAccentText().name()));
}

// ---- 外观页 ----

void SettingsWindow::buildAppearancePage() {
    auto* page = new QWidget(pages_);
    auto* lay = new QVBoxLayout(page);

    // 主题模式
    auto* themeTitle = new QLabel(QStringLiteral("主题模式"), page);
    themeTitle->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold;"));
    lay->addWidget(themeTitle);
    themeList_ = new QListWidget(page);
    themeList_->addItem(QStringLiteral("深色（Win10 深色任务栏/标题栏）"));
    themeList_->addItem(QStringLiteral("浅色（Win10 浅色 + 深色文字）"));
    themeList_->setFixedHeight(72);
    lay->addWidget(themeList_);
    const w10de::Config config = w10de::Config::load(configPath().toStdString());
    const std::string mode = config.get("theme", "mode", "dark");
    themeList_->setCurrentRow(mode == "light" ? 1 : 0);

    auto* themeHint = new QLabel(QStringLiteral("提示：主题修改需重启会话生效。"),
                                 page);
    themeHint->setStyleSheet(QStringLiteral("color: %1;")
        .arg(theme::kTextSecondary().name()));
    lay->addWidget(themeHint);

    // 壁纸
    auto* wallpaperTitle = new QLabel(QStringLiteral("壁纸"), page);
    wallpaperTitle->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold; margin-top: 16px;"));
    lay->addWidget(wallpaperTitle);
    auto* wpRow = new QHBoxLayout;
    wallpaperEdit_ = new QLineEdit(page);
    wallpaperEdit_->setPlaceholderText(QStringLiteral("图片路径（留空 = 内置渐变）"));
    wallpaperEdit_->setText(QString::fromStdString(
        config.get("wallpaper", "path")));
    auto* browseBtn = new QPushButton(QStringLiteral("浏览…"), page);
    auto* applyBtn = new QPushButton(QStringLiteral("应用"), page);
    wpRow->addWidget(wallpaperEdit_, 1);
    wpRow->addWidget(browseBtn);
    wpRow->addWidget(applyBtn);
    lay->addLayout(wpRow);
    connect(browseBtn, &QPushButton::clicked, this, &SettingsWindow::browseWallpaper);
    connect(applyBtn, &QPushButton::clicked, this, &SettingsWindow::applyWallpaper);

    // 保存主题
    auto* saveBtn = new QPushButton(QStringLiteral("保存主题"), page);
    connect(saveBtn, &QPushButton::clicked, this, &SettingsWindow::saveTheme);
    lay->addWidget(saveBtn);
    lay->addStretch(1);
    pages_->addWidget(page);
    categoryList_->addItem(QStringLiteral("外观"));
}

void SettingsWindow::saveTheme() {
    w10de::Config config = w10de::Config::load(configPath().toStdString());
    const bool light = themeList_->currentRow() == 1;
    config.set("theme", "mode", light ? "light" : "dark");
    if (config.save(configPath().toStdString())) {
        QMessageBox::information(this, QStringLiteral("设置"),
            QStringLiteral("已保存主题为 %1。\n重启会话（w10-session）后生效。")
                .arg(light ? QStringLiteral("浅色") : QStringLiteral("深色")));
    } else {
        QMessageBox::warning(this, QStringLiteral("设置"),
            QStringLiteral("保存配置失败（%1）。").arg(configPath()));
    }
}

void SettingsWindow::browseWallpaper() {
    const QString file = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择壁纸"), QDir::homePath(),
        QStringLiteral("图片 (*.png *.jpg *.jpeg *.svg)"));
    if (!file.isEmpty()) {
        wallpaperEdit_->setText(file);
    }
}

void SettingsWindow::applyWallpaper() {
    const QString path = wallpaperEdit_->text().trimmed();
    w10de::Config config = w10de::Config::load(configPath().toStdString());
    if (path.isEmpty()) {
        // 清空 = 恢复内置渐变（置空值，读取方视为未配置）。
        config.set("wallpaper", "path", std::string());
    } else {
        config.set("wallpaper", "path", path.toStdString());
    }
    if (config.save(configPath().toStdString())) {
        QMessageBox::information(this, QStringLiteral("设置"),
            QStringLiteral("壁纸已保存（%1）。\n重启会话后生效。")
                .arg(path.isEmpty() ? QStringLiteral("内置渐变") : path));
    } else {
        QMessageBox::warning(this, QStringLiteral("设置"),
            QStringLiteral("保存配置失败（%1）。").arg(configPath()));
    }
}

// ---- 系统页 ----

void SettingsWindow::buildSystemPage() {
    auto* page = new QWidget(pages_);
    auto* lay = new QVBoxLayout(page);

    auto* aboutTitle = new QLabel(QStringLiteral("关于"), page);
    aboutTitle->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold;"));
    lay->addWidget(aboutTitle);

    const w10de::Config config = w10de::Config::load(configPath().toStdString());
    const QString mode = QString::fromStdString(config.get("theme", "mode", "dark"));

    auto* grid = new QGridLayout;
    versionValue_ = new QLabel(QStringLiteral("Win10DE 0.1.0"), page);
    platformValue_ = new QLabel(QStringLiteral("wlroots 0.19 · Qt %1").arg(QString::fromLatin1(qVersion())), page);
    themeValue_ = new QLabel(mode == "light" ? QStringLiteral("浅色") : QStringLiteral("深色"), page);
    grid->addWidget(new QLabel(QStringLiteral("版本"), page), 0, 0);
    grid->addWidget(versionValue_, 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("平台"), page), 1, 0);
    grid->addWidget(platformValue_, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("当前主题"), page), 2, 0);
    grid->addWidget(themeValue_, 2, 1);
    lay->addLayout(grid);

    // 会话：开机自启
    auto* sessionTitle = new QLabel(QStringLiteral("会话"), page);
    sessionTitle->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold; margin-top: 16px;"));
    lay->addWidget(sessionTitle);
    auto* autostart = new QCheckBox(QStringLiteral("登录时自动启动 Win10DE 会话"), page);
    const QString autostartFile = QDir::homePath()
        + QStringLiteral("/.config/autostart/w10de.desktop");
    autostart->setChecked(QFileInfo::exists(autostartFile));
    connect(autostart, &QCheckBox::toggled, this, &SettingsWindow::toggleAutostart);
    lay->addWidget(autostart);
    auto* sessionHint = new QLabel(QStringLiteral("写入 ~/.config/autostart/w10de.desktop"), page);
    sessionHint->setStyleSheet(QStringLiteral("color: %1;")
        .arg(theme::kTextSecondary().name()));
    lay->addWidget(sessionHint);
    lay->addStretch(1);
    pages_->addWidget(page);
    categoryList_->addItem(QStringLiteral("系统"));
}

void SettingsWindow::toggleAutostart(bool on) {
    const QString autostartDir = QDir::homePath() + QStringLiteral("/.config/autostart");
    const QString autostartFile = autostartDir + QStringLiteral("/w10de.desktop");
    if (on) {
        if (!QDir().mkpath(autostartDir)) {
            QMessageBox::warning(this, QStringLiteral("设置"),
                                 QStringLiteral("创建 autostart 目录失败。"));
            return;
        }
        QFile f(autostartFile);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << "[Desktop Entry]\n"
               << "Type=Application\n"
               << "Name=Win10DE Session\n"
               << "Exec=w10-session\n"
               << "X-GNOME-Autostart-enabled=true\n";
            f.close();
        }
    } else {
        QFile::remove(autostartFile);
    }
    statusBar()->showMessage(on ? QStringLiteral("已启用开机自启")
                                : QStringLiteral("已禁用开机自启"), 3000);
}

// ---- 导航/搜索 ----

void SettingsWindow::onSearchChanged(const QString& text) {
    // 按关键词过滤分类项（KDE 搜索语义简化版）。
    const QString t = text.trimmed();
    for (int i = 0; i < categoryList_->count(); ++i) {
        auto* item = categoryList_->item(i);
        item->setHidden(!t.isEmpty() && !item->text().contains(t, Qt::CaseInsensitive));
    }
    // 选中项被隐藏时移到第一个可见项。
    const int row = categoryList_->currentRow();
    if (row >= 0 && categoryList_->item(row)->isHidden()) {
        for (int i = 0; i < categoryList_->count(); ++i) {
            if (!categoryList_->item(i)->isHidden()) {
                categoryList_->setCurrentRow(i);
                break;
            }
        }
    }
}

void SettingsWindow::onCategoryChanged() {
    const int row = categoryList_->currentRow();
    if (row < 0 || row >= pages_->count()) {
        return;
    }
    pages_->setCurrentIndex(row);
    const QString name = categoryList_->item(row)->text();
    pathLabel_->setText(breadcrumb(QStringLiteral("设置"), name));
}

}  // namespace w10de::settings
