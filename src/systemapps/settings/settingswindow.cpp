#include "systemapps/settings/settingswindow.h"
#include "systemapps/settings/audioinfo.h"
#include "systemapps/settings/bluetoothinfo.h"
#include "systemapps/settings/defaultapps.h"
#include "systemapps/settings/networkinfo.h"
#include "systemapps/settings/powerinfo.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>
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
    buildDisplayPage();
    buildPowerPage();
    buildAudioPage();
    buildDefaultsPage();
    buildNetworkPage();
    buildBluetoothPage();
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

// ---- 显示页（第二批：compositor D-Bus 输出管理 IPC）----

namespace {

// compositor D-Bus 接口访问器（org.w10de.Compositor /Outputs）。
QDBusInterface compositorOutputsIface(QObject* parent = nullptr) {
    return QDBusInterface(QStringLiteral("org.w10de.Compositor"),
                          QStringLiteral("/Outputs"),
                          QStringLiteral("org.w10de.Compositor"),
                          QDBusConnection::sessionBus(), parent);
}

}  // namespace

// （结构 OutputInfo/ModeInfo 定义于外层 w10de::settings 命名空间内；
// Q_DECLARE_METATYPE 在文件末尾全局。）

// GetOutputs 返回 a(siiiii)：(name, width, height, scalePercent, x, y)。
// 用 Qt 标准 qdbus_cast 方式解析（直接迭代 QDBusArgument 在 Qt6 有
// read-only 限制——gdb 定位 operator>> 断言崩溃）。
struct OutputInfo {
    QString name;
    int w = 0;
    int h = 0;
    int scale = 100;
    int x = 0;
    int y = 0;
};
QDBusArgument& operator<<(QDBusArgument& arg, const OutputInfo& o) {
    arg.beginStructure();
    arg << o.name << o.w << o.h << o.scale << o.x << o.y;
    arg.endStructure();
    return arg;
}
const QDBusArgument& operator>>(const QDBusArgument& arg, OutputInfo& o) {
    arg.beginStructure();
    arg >> o.name >> o.w >> o.h >> o.scale >> o.x >> o.y;
    arg.endStructure();
    return arg;
}

// GetModes 返回 a(ii)：(width, height)。
struct ModeInfo {
    int w = 0;
    int h = 0;
};
QDBusArgument& operator<<(QDBusArgument& arg, const ModeInfo& m) {
    arg.beginStructure();
    arg << m.w << m.h;
    arg.endStructure();
    return arg;
}
const QDBusArgument& operator>>(const QDBusArgument& arg, ModeInfo& m) {
    arg.beginStructure();
    arg >> m.w >> m.h;
    arg.endStructure();
    return arg;
}

void SettingsWindow::buildDisplayPage() {
    auto* page = new QWidget(pages_);
    auto* lay = new QVBoxLayout(page);

    auto* title = new QLabel(QStringLiteral("显示"), page);
    title->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold;"));
    lay->addWidget(title);
    auto* hint = new QLabel(QStringLiteral("经 org.w10de.Compositor 热应用（无需重启会话）。"), page);
    hint->setStyleSheet(QStringLiteral("color: %1;")
        .arg(theme::kTextSecondary().name()));
    lay->addWidget(hint);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    grid->addWidget(new QLabel(QStringLiteral("输出"), page), 0, 0);
    outputCombo_ = new QComboBox(page);
    grid->addWidget(outputCombo_, 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("分辨率"), page), 1, 0);
    modeCombo_ = new QComboBox(page);
    grid->addWidget(modeCombo_, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("缩放"), page), 2, 0);
    scaleCombo_ = new QComboBox(page);
    scaleCombo_->addItem(QStringLiteral("100%"), 100);
    scaleCombo_->addItem(QStringLiteral("125%"), 125);
    scaleCombo_->addItem(QStringLiteral("150%"), 150);
    scaleCombo_->addItem(QStringLiteral("200%"), 200);
    grid->addWidget(scaleCombo_, 2, 1);
    lay->addLayout(grid);

    auto* btnRow = new QHBoxLayout;
    auto* applyBtn = new QPushButton(QStringLiteral("应用"), page);
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), page);
    btnRow->addWidget(applyBtn);
    btnRow->addWidget(refreshBtn);
    btnRow->addStretch(1);
    lay->addLayout(btnRow);

    displayStatus_ = new QLabel(QStringLiteral("未连接合成器（org.w10de.Compositor 不可用）。"), page);
    displayStatus_->setStyleSheet(QStringLiteral("color: %1;")
        .arg(theme::kTextSecondary().name()));
    displayStatus_->setWordWrap(true);
    lay->addWidget(displayStatus_);
    lay->addStretch(1);

    connect(refreshBtn, &QPushButton::clicked, this, &SettingsWindow::refreshOutputs);
    connect(applyBtn, &QPushButton::clicked, this, &SettingsWindow::applyDisplaySettings);
    // 审查 M3：切换输出下拉时重新加载该输出的模式/缩放。
    connect(outputCombo_, &QComboBox::currentIndexChanged,
            this, &SettingsWindow::loadOutputDetails);
    pages_->addWidget(page);
    categoryList_->addItem(QStringLiteral("显示"));
}

void SettingsWindow::refreshOutputs() {
    QDBusInterface iface = compositorOutputsIface(this);
    if (!iface.isValid()) {
        displayStatus_->setText(QStringLiteral("合成器 D-Bus 服务不可用（org.w10de.Compositor）。"));
        outputCombo_->clear();
        outputNames_.clear();
        return;
    }
    const QDBusMessage msg = iface.call(QStringLiteral("GetOutputs"));
    if (msg.type() == QDBusMessage::ErrorMessage) {
        displayStatus_->setText(QStringLiteral("GetOutputs 失败：%1").arg(msg.errorMessage()));
        return;
    }
    if (msg.arguments().isEmpty()) {
        displayStatus_->setText(QStringLiteral("GetOutputs 返回空。"));
        return;
    }
    static const bool reg = [] { qDBusRegisterMetaType<OutputInfo>(); return true; }();
    Q_UNUSED(reg);
    const QList<OutputInfo> outputs =
        qdbus_cast<QList<OutputInfo>>(msg.arguments().at(0));

    outputCombo_->clear();
    outputNames_.clear();
    for (const OutputInfo& o : outputs) {
        outputCombo_->addItem(QStringLiteral("%1（%2×%3，缩放 %4%%）")
            .arg(o.name).arg(o.w).arg(o.h).arg(o.scale));
        outputNames_.append(o.name);
    }
    if (outputs.isEmpty()) {
        displayStatus_->setText(QStringLiteral("合成器无输出。"));
        return;
    }
    // 选中第一个输出并加载其模式/缩放（审查 M3：切换下拉时按索引重新
    // 加载，应用时也按当前索引操作对应输出）。
    if (outputs.first().scale != curScalePercent_) {
        curScalePercent_ = outputs.first().scale;
    }
    curWidth_ = outputs.first().w;
    curHeight_ = outputs.first().h;
    loadOutputDetails(0);
    displayStatus_->setText(QStringLiteral("已连接合成器。"));
    qInfo("settings: display page loaded %d output(s), first=%s (%dx%d scale %d%%)",
          outputs.size(), qPrintable(outputNames_.value(0)),
          curWidth_, curHeight_, curScalePercent_);
}

void SettingsWindow::loadOutputDetails(int index) {
    if (index < 0 || index >= outputNames_.size()) {
        return;
    }
    const QString name = outputNames_.at(index);
    QDBusInterface iface = compositorOutputsIface(this);
    if (!iface.isValid()) {
        return;
    }
    // 分辨率列表：优先 GetModes；headless 无 modes 时用常用列表。
    modeCombo_->clear();
    modeMap_.clear();
    QSet<QString> seen;
    const QDBusMessage modesMsg = iface.call(QStringLiteral("GetModes"), name);
    bool haveModes = false;
    if (modesMsg.type() == QDBusMessage::ReplyMessage && !modesMsg.arguments().isEmpty()) {
        static const bool reg = [] { qDBusRegisterMetaType<ModeInfo>(); return true; }();
        Q_UNUSED(reg);
        const QList<ModeInfo> modes =
            qdbus_cast<QList<ModeInfo>>(modesMsg.arguments().at(0));
        for (const ModeInfo& m : modes) {
            if (m.w > 0 && m.h > 0 &&
                    !seen.contains(QStringLiteral("%1x%2").arg(m.w).arg(m.h))) {
                modeCombo_->addItem(QStringLiteral("%1 × %2").arg(m.w).arg(m.h));
                modeMap_.append({QStringLiteral("%1x%2").arg(m.w).arg(m.h), m.h});
                seen.insert(QStringLiteral("%1x%2").arg(m.w).arg(m.h));
                haveModes = true;
            }
        }
    }
    if (!haveModes) {
        // headless/未知输出：常用分辨率。
        const QList<QPair<int, int>> common = {
            {1920, 1080}, {1366, 768}, {1280, 720}, {1024, 768}, {800, 600}};
        for (const auto& [w, h] : common) {
            modeCombo_->addItem(QStringLiteral("%1 × %2").arg(w).arg(h));
            modeMap_.append({QStringLiteral("%1x%2").arg(w).arg(h), h});
        }
    }
    // 恢复当前模式选择；不在列表时追加"当前模式"项（审查 L5：scale≠100
    // 时有效分辨率不在常用列表）。
    const QString cur = QStringLiteral("%1x%2").arg(curWidth_).arg(curHeight_);
    int curIdx = -1;
    for (int i = 0; i < modeMap_.size(); ++i) {
        if (modeMap_.at(i).first == cur) {
            curIdx = i;
            break;
        }
    }
    if (curIdx < 0) {
        modeCombo_->addItem(QStringLiteral("%1 × %2（当前）").arg(curWidth_).arg(curHeight_));
        modeMap_.append({cur, curHeight_});
        curIdx = modeMap_.size() - 1;
    }
    modeCombo_->setCurrentIndex(curIdx);
    // 缩放恢复；非预设值（如 110）时追加当前值项（审查 L6）。
    int scaleIdx = scaleCombo_->findData(curScalePercent_);
    if (scaleIdx < 0) {
        scaleCombo_->addItem(QStringLiteral("%1%%（当前）").arg(curScalePercent_),
                             curScalePercent_);
        scaleIdx = scaleCombo_->count() - 1;
    }
    scaleCombo_->setCurrentIndex(scaleIdx);
}

void SettingsWindow::applyDisplaySettings() {
    if (outputNames_.isEmpty()) {
        return;
    }
    // 审查 M3：操作下拉当前选中的输出（不再固定第一个）。
    const int idx = outputCombo_->currentIndex();
    if (idx < 0 || idx >= outputNames_.size()) {
        return;
    }
    const QString name = outputNames_.at(idx);
    QDBusInterface iface = compositorOutputsIface(this);
    if (!iface.isValid()) {
        displayStatus_->setText(QStringLiteral("合成器 D-Bus 服务不可用。"));
        return;
    }
    // 分辨率（modeCombo_ 文本 "W × H" 或映射）。
    int w = 0, h = 0;
    const int modeIdx = modeCombo_->currentIndex();
    if (modeIdx >= 0 && modeIdx < modeMap_.size()) {
        const QString key = modeMap_.at(modeIdx).first;
        const int x = key.indexOf(QLatin1Char('x'));
        w = key.left(x).toInt();
        h = key.mid(x + 1).toInt();
    }
    const int scale = scaleCombo_->currentData().toInt();

    bool ok = true;
    QString errMsg;
    if (w > 0 && h > 0) {
        const QDBusReply<void> r = iface.call(QStringLiteral("SetMode"), name, w, h);
        if (!r.isValid()) {
            ok = false;
            errMsg = r.error().message();
        }
    }
    if (ok) {
        const QDBusReply<void> r = iface.call(QStringLiteral("SetScale"), name, scale);
        if (!r.isValid()) {
            ok = false;
            errMsg = r.error().message();
        }
    }
    displayStatus_->setText(ok
        ? QStringLiteral("已应用：%1 × %2，缩放 %3%%。").arg(w).arg(h).arg(scale)
        : QStringLiteral("应用失败：%1").arg(errMsg));
    if (ok) {
        curWidth_ = w;
        curHeight_ = h;
        curScalePercent_ = scale;
        refreshOutputs();  // 回读确认
    }
}

// ---- 电源页（第二批：UPower 语义，sysfs 电池/背光直读）----

void SettingsWindow::buildPowerPage() {
    auto* page = new QWidget(pages_);
    auto* lay = new QVBoxLayout(page);

    auto* title = new QLabel(QStringLiteral("电源"), page);
    title->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold;"));
    lay->addWidget(title);
    auto* hint = new QLabel(QStringLiteral("电池/背光信息（sysfs 读取，等价 UPower 数据源）。"), page);
    hint->setStyleSheet(QStringLiteral("color: %1;")
        .arg(theme::kTextSecondary().name()));
    lay->addWidget(hint);

    auto* batteryTitle = new QLabel(QStringLiteral("电池"), page);
    batteryTitle->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold; margin-top: 8px;"));
    lay->addWidget(batteryTitle);
    batteryValue_ = new QLabel(QStringLiteral("（检测中…）"), page);
    batteryValue_->setWordWrap(true);
    lay->addWidget(batteryValue_);

    auto* backlightTitle = new QLabel(QStringLiteral("屏幕亮度"), page);
    backlightTitle->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold; margin-top: 16px;"));
    lay->addWidget(backlightTitle);
    backlightValue_ = new QLabel(QStringLiteral("（检测中…）"), page);
    backlightValue_->setWordWrap(true);
    lay->addWidget(backlightValue_);
    brightnessSlider_ = new QSlider(Qt::Horizontal, page);
    brightnessSlider_->setRange(0, 100);
    brightnessSlider_->setEnabled(false);
    lay->addWidget(brightnessSlider_);

    auto* btnRow = new QHBoxLayout;
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), page);
    btnRow->addWidget(refreshBtn);
    btnRow->addStretch(1);
    lay->addLayout(btnRow);
    lay->addStretch(1);

    connect(refreshBtn, &QPushButton::clicked, this, &SettingsWindow::refreshPower);
    // 键盘/鼠标调节都生效（审查 M2）：sliderReleased 不覆盖键盘；用
    // valueChanged + !isSliderDown()（拖动中不写，释放瞬间写一次）。
    connect(brightnessSlider_, &QSlider::valueChanged,
            this, [this](int value) {
        if (!brightnessSlider_->isSliderDown()) {
            applyBrightness(value);
        }
    });
    pages_->addWidget(page);
    categoryList_->addItem(QStringLiteral("电源"));

    refreshPower();
}

void SettingsWindow::refreshPower() {
    const BatteryInfo battery = PowerInfo::battery();
    if (battery.present) {
        const QString statusText =
            battery.status == QStringLiteral("Charging") ? QStringLiteral("充电中")
            : battery.status == QStringLiteral("Discharging") ? QStringLiteral("放电中")
            : battery.status == QStringLiteral("Full") ? QStringLiteral("已充满")
            : battery.status;
        // percent -1（无 capacity 设备）显示 "--"（审查 L1/L9）。
        const QString pctText = battery.percent >= 0
            ? QString::number(battery.percent) + QStringLiteral("%")
            : QStringLiteral("--");
        QString energyText;
        if (battery.energyNowUwh > 0) {
            // energy_now（µWh）→ mWh；charge_now（µAh）→ mAh（审查 M1
            // 单位不强行换算，按来源标注）。
            energyText = battery.energyIsCharge
                ? QStringLiteral("，剩余 %1 mAh").arg(battery.energyNowUwh / 1000)
                : QStringLiteral("，剩余 %1 mWh").arg(battery.energyNowUwh / 1000);
        }
        batteryValue_->setText(QStringLiteral("%1（主电池）：%2，状态：%3%4")
            .arg(battery.device).arg(pctText).arg(statusText).arg(energyText));
    } else {
        batteryValue_->setText(QStringLiteral("未检测到电池（桌面/无电池设备）。"));
    }

    const BacklightInfo backlight = PowerInfo::backlight();
    if (backlight.present && backlight.maxBrightness > 0) {
        backlightDevice_ = backlight.device;
        backlightMax_ = backlight.maxBrightness;
        backlightValue_->setText(QStringLiteral("%1：%2 / %3")
            .arg(backlight.device).arg(backlight.brightness)
            .arg(backlight.maxBrightness));
        // 无写权限禁用滑块（审查 M4：可拖必失败不合格）。
        const bool writable = PowerInfo::backlightWritable(backlight.device);
        brightnessSlider_->setEnabled(writable);
        const int pct = backlight.brightness * 100 / backlight.maxBrightness;
        // 刷新时 setValue 不应触发写回（QSignalBlocker）。
        const QSignalBlocker blocker(brightnessSlider_);
        brightnessSlider_->setValue(pct);
        if (!writable) {
            backlightValue_->setText(backlightValue_->text() +
                QStringLiteral("（无写权限，需 root/backlight 组）"));
        }
    } else {
        backlightDevice_.clear();
        backlightMax_ = 0;
        backlightValue_->setText(QStringLiteral("无背光设备（headless/外接显示器）。"));
        brightnessSlider_->setEnabled(false);
    }
}

void SettingsWindow::applyBrightness(int value) {
    if (backlightDevice_.isEmpty() || backlightMax_ <= 0) {
        return;
    }
    // 滑块 0-100 → 实际亮度值（0..maxBrightness；用缓存的 max——审查 M3
    // 避免重查背光时设备错配）。
    const int abs = value * backlightMax_ / 100;
    if (PowerInfo::setBrightness(backlightDevice_, abs)) {
        backlightValue_->setText(QStringLiteral("%1：%2 / %3（已应用）")
            .arg(backlightDevice_).arg(abs).arg(backlightMax_));
    } else {
        backlightValue_->setText(QStringLiteral("%1：写入失败（需要写权限）。")
            .arg(backlightDevice_));
        brightnessSlider_->setEnabled(false);
    }
}

// ---- 音频页（第二批：PipeWire 音量/设备，libpulse 客户端）----

void SettingsWindow::buildAudioPage() {
    auto* page = new QWidget(pages_);
    auto* lay = new QVBoxLayout(page);

    auto* title = new QLabel(QStringLiteral("音频"), page);
    title->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold;"));
    lay->addWidget(title);
    auto* hint = new QLabel(QStringLiteral("输出设备与音量（libpulse 客户端；真机 PipeWire 提供 Pulse 兼容服务）。"), page);
    hint->setStyleSheet(QStringLiteral("color: %1;")
        .arg(theme::kTextSecondary().name()));
    lay->addWidget(hint);

    audioStatus_ = new QLabel(QStringLiteral("（检测中…）"), page);
    audioStatus_->setWordWrap(true);
    lay->addWidget(audioStatus_);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    grid->addWidget(new QLabel(QStringLiteral("输出设备"), page), 0, 0);
    sinkCombo_ = new QComboBox(page);
    sinkCombo_->setEnabled(false);
    grid->addWidget(sinkCombo_, 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("音量"), page), 1, 0);
    volumeSlider_ = new QSlider(Qt::Horizontal, page);
    volumeSlider_->setRange(0, 100);
    volumeSlider_->setValue(100);
    volumeSlider_->setEnabled(false);
    grid->addWidget(volumeSlider_, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("静音"), page), 2, 0);
    muteCheck_ = new QCheckBox(QStringLiteral("静音输出"), page);
    muteCheck_->setEnabled(false);
    grid->addWidget(muteCheck_, 2, 1);
    lay->addLayout(grid);

    auto* btnRow = new QHBoxLayout;
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), page);
    btnRow->addWidget(refreshBtn);
    btnRow->addStretch(1);
    lay->addLayout(btnRow);
    lay->addStretch(1);

    audio_ = new AudioInfo(this);
    connect(refreshBtn, &QPushButton::clicked, this, &SettingsWindow::refreshAudio);
    connect(audio_, &AudioInfo::sinksReady, this, &SettingsWindow::onAudioSinksReady);
    connect(audio_, &AudioInfo::connectionFailed, this, [this](const QString& reason) {
        qInfo("settings: audio unavailable: %s", qPrintable(reason));
        audioStatus_->setText(reason);
        sinkCombo_->setEnabled(false);
        volumeSlider_->setEnabled(false);
        muteCheck_->setEnabled(false);
    });
    // 音量滑块：拖动中不写，释放/键盘改后写（与亮度滑块同语义）。
    connect(volumeSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (!volumeSlider_->isSliderDown() && sinkCombo_->currentIndex() >= 0) {
            applyAudioVolume(value);
        }
    });
    connect(muteCheck_, &QCheckBox::toggled, this, &SettingsWindow::toggleAudioMute);
    // 审查 L7：切换输出设备时更新滑块/静音显示（用缓存 sinks）。
    connect(sinkCombo_, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0 || index >= sinkCache_.size()) {
            return;
        }
        const SinkInfo& s = sinkCache_.at(index);
        const QSignalBlocker vb(volumeSlider_);
        volumeSlider_->setValue(s.volumePercent);
        const QSignalBlocker mb(muteCheck_);
        muteCheck_->setChecked(s.muted);
    });
    pages_->addWidget(page);
    categoryList_->addItem(QStringLiteral("音频"));
}

void SettingsWindow::refreshAudio() {
    if (audio_ == nullptr) {
        return;
    }
    audioStatus_->setText(QStringLiteral("（检测中…）"));
    // 连接超时兜底（审查 L1：可取消的成员 timer，避免多次刷新累积回调）。
    if (audioTimeoutTimer_ == nullptr) {
        audioTimeoutTimer_ = new QTimer(this);
        audioTimeoutTimer_->setSingleShot(true);
        audioTimeoutTimer_->setInterval(3000);
        connect(audioTimeoutTimer_, &QTimer::timeout, this, [this] {
            if (audioStatus_ != nullptr &&
                    audioStatus_->text() == QStringLiteral("（检测中…）")) {
                audioStatus_->setText(
                    QStringLiteral("PulseAudio 服务不可用（连接超时；真机 PipeWire 提供该服务）。"));
                sinkCombo_->setEnabled(false);
                volumeSlider_->setEnabled(false);
                muteCheck_->setEnabled(false);
            }
        });
    }
    audioTimeoutTimer_->start();
    audio_->refreshSinks();
}

void SettingsWindow::onAudioSinksReady(const QList<SinkInfo>& sinks) {
    sinkCache_ = sinks;
    sinkCombo_->clear();
    sinkNames_.clear();
    for (const SinkInfo& s : sinks) {
        sinkCombo_->addItem(QStringLiteral("%1（%2%3）")
            .arg(s.description.isEmpty() ? s.name : s.description)
            .arg(s.volumePercent)
            .arg(s.muted ? QStringLiteral("，静音") : QString()));
        sinkNames_.append(s.name);
    }
    if (sinks.isEmpty()) {
        audioStatus_->setText(QStringLiteral("PulseAudio 已连接，但无输出设备。"));
        return;
    }
    sinkCombo_->setEnabled(true);
    volumeSlider_->setEnabled(true);
    muteCheck_->setEnabled(true);
    const SinkInfo& first = sinks.first();
    const QSignalBlocker vb(volumeSlider_);
    volumeSlider_->setValue(first.volumePercent);
    const QSignalBlocker mb(muteCheck_);
    muteCheck_->setChecked(first.muted);
    audioStatus_->setText(QStringLiteral("已连接 PulseAudio（%1 个输出设备）。")
        .arg(sinks.size()));
}

void SettingsWindow::applyAudioVolume(int value) {
    const int idx = sinkCombo_->currentIndex();
    if (idx < 0 || idx >= sinkNames_.size() || audio_ == nullptr) {
        return;
    }
    audio_->setVolume(sinkNames_.at(idx), value);
}

void SettingsWindow::toggleAudioMute(bool muted) {
    const int idx = sinkCombo_->currentIndex();
    if (idx < 0 || idx >= sinkNames_.size() || audio_ == nullptr) {
        return;
    }
    audio_->setMuted(sinkNames_.at(idx), muted);
}

// ---- 默认应用页（第二批收官：xdg mimeapps.list）----

void SettingsWindow::buildDefaultsPage() {
    auto* page = new QWidget(pages_);
    auto* lay = new QVBoxLayout(page);

    auto* title = new QLabel(QStringLiteral("默认应用"), page);
    title->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold;"));
    lay->addWidget(title);
    auto* hint = new QLabel(QStringLiteral("写入 ~/.config/mimeapps.list（xdg 标准，xdg-open 生效）。"), page);
    hint->setStyleSheet(QStringLiteral("color: %1;")
        .arg(theme::kTextSecondary().name()));
    lay->addWidget(hint);

    const QList<DesktopApp> apps = DefaultApps::listApplications();
    // 审查 M4：尊重 XDG_CONFIG_HOME（QStandardPaths）。
    const QString mimeapps = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation) + QStringLiteral("/mimeapps.list");
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(10);
    for (int k = 0; k < static_cast<int>(DefaultKind::Count); ++k) {
        const auto kind = static_cast<DefaultKind>(k);
        auto* label = new QLabel(defaultKindLabel(kind), page);
        auto* combo = new QComboBox(page);
        // 空项 = 未设置。
        combo->addItem(QStringLiteral("（未设置）"), QString());
        for (const DesktopApp& app : apps) {
            combo->addItem(app.name.isEmpty() ? app.id : app.name, app.id);
        }
        // 恢复当前默认。
        const QString current = DefaultApps::currentDefault(mimeapps, kind);
        const int idx = combo->findData(current);
        if (idx >= 0) {
            combo->setCurrentIndex(idx);
        }
        grid->addWidget(label, k, 0);
        grid->addWidget(combo, k, 1);
        defaultCombos_.append(combo);
    }
    lay->addLayout(grid);

    auto* applyBtn = new QPushButton(QStringLiteral("应用"), page);
    lay->addWidget(applyBtn, 0, Qt::AlignLeft);
    defaultsStatus_ = new QLabel(QStringLiteral("已扫描 %1 个应用。").arg(apps.size()), page);
    defaultsStatus_->setStyleSheet(QStringLiteral("color: %1;")
        .arg(theme::kTextSecondary().name()));
    defaultsStatus_->setWordWrap(true);
    lay->addWidget(defaultsStatus_);
    lay->addStretch(1);

    connect(applyBtn, &QPushButton::clicked, this, [this] {
        for (int k = 0; k < defaultCombos_.size(); ++k) {
            // 审查 L7：某类别写入失败即中断，保留错误信息。
            if (!applyDefault(k)) {
                return;
            }
        }
    });
    pages_->addWidget(page);
    categoryList_->addItem(QStringLiteral("默认应用"));
}

bool SettingsWindow::applyDefault(int kindIndex) {
    if (kindIndex < 0 || kindIndex >= defaultCombos_.size()) {
        return false;
    }
    const auto kind = static_cast<DefaultKind>(kindIndex);
    const QString desktopId = defaultCombos_.at(kindIndex)->currentData().toString();
    const QString mimeapps = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation) + QStringLiteral("/mimeapps.list");
    if (desktopId.isEmpty()) {
        // 清空该类别的关联（保留其他类别）。
        auto defaults = DefaultApps::loadMimeDefaults(mimeapps);
        for (const QString& mime : defaultKindMimes(kind)) {
            defaults.remove(mime);
        }
        if (!DefaultApps::saveMimeDefaults(mimeapps, defaults)) {
            defaultsStatus_->setText(QStringLiteral("写入 mimeapps.list 失败。"));
            return false;
        }
    } else if (!DefaultApps::setDefault(mimeapps, kind, desktopId)) {
        defaultsStatus_->setText(QStringLiteral("写入 mimeapps.list 失败。"));
        return false;
    }
    defaultsStatus_->setText(QStringLiteral("已应用：%1 → %2（写入 %3）。")
        .arg(defaultKindLabel(kind),
             defaultCombos_.at(kindIndex)->currentText(),
             mimeapps));
    return true;
}

// ---- 网络页（第三批：NetworkManager 状态）----

void SettingsWindow::buildNetworkPage() {
    auto* page = new QWidget(pages_);
    auto* lay = new QVBoxLayout(page);

    auto* title = new QLabel(QStringLiteral("网络"), page);
    title->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold;"));
    lay->addWidget(title);
    auto* hint = new QLabel(QStringLiteral("经 NetworkManager D-Bus 查询连接状态（服务缺失时显示不可用）。"), page);
    hint->setStyleSheet(QStringLiteral("color: %1;").arg(theme::kTextSecondary().name()));
    lay->addWidget(hint);

    networkStatus_ = new QLabel(page);
    networkStatus_->setStyleSheet(QStringLiteral("color: %1;")
        .arg(theme::kTextPrimary().name()));
    networkStatus_->setWordWrap(true);
    lay->addWidget(networkStatus_);

    networkDetails_ = new QLabel(page);
    networkDetails_->setStyleSheet(QStringLiteral("color: %1;")
        .arg(theme::kTextSecondary().name()));
    networkDetails_->setWordWrap(true);
    lay->addWidget(networkDetails_);

    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), page);
    lay->addWidget(refreshBtn, 0, Qt::AlignLeft);
    connect(refreshBtn, &QPushButton::clicked, this, &SettingsWindow::refreshNetwork);
    lay->addStretch(1);

    refreshNetwork();
}

void SettingsWindow::refreshNetwork() {
    const NetworkStatus st = NetworkInfo::query();
    if (!st.available) {
        networkStatus_->setText(QStringLiteral("NetworkManager 服务不可用（未安装或未运行）。"));
        networkDetails_->setText(QString());
        return;
    }
    networkStatus_->setText(QStringLiteral("状态：%1").arg(st.stateText));
    if (st.connections.isEmpty()) {
        networkDetails_->setText(QStringLiteral("无活动连接。"));
        return;
    }
    QStringList lines;
    for (const NetworkConnection& conn : st.connections) {
        QString typeName = conn.type == QStringLiteral("802-11-wireless")
            ? QStringLiteral("无线") : QStringLiteral("有线");
        QString line = QStringLiteral("· %1（%2）").arg(conn.id, typeName);
        if (!conn.ip.isEmpty()) {
            line += QStringLiteral("  IP: %1").arg(conn.ip);
        }
        lines << line;
    }
    networkDetails_->setText(lines.join(QLatin1Char('\n')));
}

// ---- 蓝牙页（第三批：Bluez 状态/开关）----

void SettingsWindow::buildBluetoothPage() {
    auto* page = new QWidget(pages_);
    auto* lay = new QVBoxLayout(page);

    auto* title = new QLabel(QStringLiteral("蓝牙"), page);
    title->setStyleSheet(QStringLiteral("font-size: 15px; font-weight: bold;"));
    lay->addWidget(title);
    auto* hint = new QLabel(QStringLiteral("经 Bluez D-Bus 查询适配器与设备（服务缺失时显示不可用）。"), page);
    hint->setStyleSheet(QStringLiteral("color: %1;").arg(theme::kTextSecondary().name()));
    lay->addWidget(hint);

    auto* switchRow = new QHBoxLayout;
    bluetoothSwitch_ = new QCheckBox(QStringLiteral("蓝牙开关"), page);
    switchRow->addWidget(bluetoothSwitch_);
    switchRow->addStretch(1);
    lay->addLayout(switchRow);
    connect(bluetoothSwitch_, &QCheckBox::toggled,
            this, &SettingsWindow::toggleBluetooth);

    bluetoothStatus_ = new QLabel(page);
    bluetoothStatus_->setStyleSheet(QStringLiteral("color: %1;")
        .arg(theme::kTextPrimary().name()));
    bluetoothStatus_->setWordWrap(true);
    lay->addWidget(bluetoothStatus_);

    bluetoothDevices_ = new QLabel(page);
    bluetoothDevices_->setStyleSheet(QStringLiteral("color: %1;")
        .arg(theme::kTextSecondary().name()));
    bluetoothDevices_->setWordWrap(true);
    lay->addWidget(bluetoothDevices_);

    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), page);
    lay->addWidget(refreshBtn, 0, Qt::AlignLeft);
    connect(refreshBtn, &QPushButton::clicked, this, &SettingsWindow::refreshBluetooth);
    lay->addStretch(1);

    refreshBluetooth();
}

void SettingsWindow::refreshBluetooth() {
    const BluetoothStatus st = BluetoothInfo::query();
    if (!st.available) {
        bluetoothSwitch_->setEnabled(false);
        // 审查 M3：透传 query 的错误描述（无 bus / 服务缺失 / 方法失败）。
        bluetoothStatus_->setText(st.errorText.isEmpty()
            ? QStringLiteral("Bluez 服务不可用（未安装或未运行）。")
            : st.errorText);
        bluetoothDevices_->setText(QString());
        return;
    }
    bluetoothSwitch_->setEnabled(true);
    // 阻断信号避免 toggleBluetooth 回写（仅刷新显示）。
    QSignalBlocker blocker(bluetoothSwitch_);
    bluetoothSwitch_->setChecked(st.powered);
    bluetoothStatus_->setText(
        QStringLiteral("适配器：%1（%2）  %3")
            .arg(st.adapterAlias.isEmpty() ? QStringLiteral("hci0") : st.adapterAlias,
                 st.adapterAddress,
                 st.powered ? QStringLiteral("已开启") : QStringLiteral("已关闭")));
    if (st.devices.isEmpty()) {
        bluetoothDevices_->setText(QStringLiteral("无已发现设备。"));
    } else {
        QStringList lines;
        for (const BluetoothDevice& dev : st.devices) {
            QString state = dev.connected ? QStringLiteral("已连接")
                : (dev.paired ? QStringLiteral("已配对") : QStringLiteral("未配对"));
            lines << QStringLiteral("· %1（%2） %3").arg(dev.name, dev.address, state);
        }
        bluetoothDevices_->setText(lines.join(QLatin1Char('\n')));
    }
}

void SettingsWindow::toggleBluetooth(bool on) {
    const bool ok = BluetoothInfo::setPowered(on);
    // 审查 M2：先回读实际状态，再追加失败提示（原实现先设错误文本、
    // 后 refreshBluetooth 无条件覆盖，错误永远不可见）。
    refreshBluetooth();
    if (!ok) {
        bluetoothStatus_->setText(
            QStringLiteral("%1（切换失败：Bluez 不可用或权限不足，"
                           "可能需要 root/蓝牙组）。")
                .arg(bluetoothStatus_->text()));
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

void SettingsWindow::selectCategory(const QString& name) {
    // 接受中文分类名或英文别名（--page display/power 等；审查 L7）。
    static const QHash<QString, QString> kAliases = {
        {QStringLiteral("display"), QStringLiteral("显示")},
        {QStringLiteral("appearance"), QStringLiteral("外观")},
        {QStringLiteral("system"), QStringLiteral("系统")},
        {QStringLiteral("power"), QStringLiteral("电源")},
        {QStringLiteral("audio"), QStringLiteral("音频")},
        {QStringLiteral("defaults"), QStringLiteral("默认应用")},
        {QStringLiteral("default"), QStringLiteral("默认应用")},
        {QStringLiteral("network"), QStringLiteral("网络")},
        {QStringLiteral("bluetooth"), QStringLiteral("蓝牙")},
    };
    const QString target = kAliases.value(name, name);
    for (int i = 0; i < categoryList_->count(); ++i) {
        if (categoryList_->item(i)->text() == target) {
            categoryList_->setCurrentRow(i);
            return;
        }
    }
}

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
    // 进入显示页时拉取合成器输出信息（KDE：进入模块即加载）。
    if (name == QStringLiteral("显示")) {
        refreshOutputs();
    } else if (name == QStringLiteral("电源")) {
        refreshPower();
    } else if (name == QStringLiteral("音频")) {
        refreshAudio();
    } else if (name == QStringLiteral("网络")) {  // 审查 L1：切回时刷新
        refreshNetwork();
    } else if (name == QStringLiteral("蓝牙")) {
        refreshBluetooth();
    }
}

}  // namespace w10de::settings

// Q_DECLARE_METATYPE 必须在全局命名空间（qdbus_cast 自定义结构用）。
Q_DECLARE_METATYPE(w10de::settings::OutputInfo)
Q_DECLARE_METATYPE(w10de::settings::ModeInfo)
