// w10control 类别对话框实现（传统控制面板风格；共享后端见头文件注释）。
#include "systemapps/control/categorydialogs.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextStream>
#include <QTimeEdit>
#include <QTimeZone>
#include <QVBoxLayout>

#include "ipc/config.h"
#include "ipc/inputsettings.h"
#include "ipc/nightlight.h"
#include "ipc/shortcuts.h"
#include "systemapps/common/ruleeditdialog.h"
#include "systemapps/settings/audioinfo.h"
#include "systemapps/settings/bluetoothinfo.h"
#include "systemapps/settings/defaultapps.h"
#include "systemapps/settings/networkinfo.h"
#include "systemapps/settings/powerinfo.h"
#include "theme/colors.h"

namespace w10de::control {

namespace {

QString configPath() {
    return QDir::homePath() + QStringLiteral("/.config/w10de/config.ini");
}

// compositor D-Bus 接口访问器（org.w10de.Compositor /Outputs）。
QDBusInterface compositorIface(QObject* parent = nullptr) {
    return QDBusInterface(QStringLiteral("org.w10de.Compositor"),
                          QStringLiteral("/Outputs"),
                          QStringLiteral("org.w10de.Compositor"),
                          QDBusConnection::sessionBus(), parent);
}

// 次要文字色样式。
QString secondaryStyle() {
    return QStringLiteral("color: %1;").arg(theme::kTextSecondary().name());
}

// 粗体小标题样式。
QString sectionTitleStyle() {
    return QStringLiteral("font-size: 15px; font-weight: bold;");
}

// 对话框内容区标题。
QLabel* makeSectionTitle(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(sectionTitleStyle());
    return l;
}

QLabel* makeHint(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(secondaryStyle());
    l->setWordWrap(true);
    return l;
}

}  // namespace

void attachDialogButtons(QDialog* dlg, std::function<bool()> save) {
    auto* box = new QDialogButtonBox(
        QDialogButtonBox::Apply | QDialogButtonBox::Ok
            | QDialogButtonBox::Cancel,
        dlg);
    // Apply：保存并保持打开。
    QPushButton* applyBtn = box->button(QDialogButtonBox::Apply);
    if (applyBtn != nullptr) {
        applyBtn->setText(QStringLiteral("应用"));
        QObject::connect(applyBtn, &QPushButton::clicked, dlg, [save] {
            save();
        });
    }
    // Ok：保存成功后关闭。
    QPushButton* okBtn = box->button(QDialogButtonBox::Ok);
    if (okBtn != nullptr) {
        okBtn->setText(QStringLiteral("确定"));
        QObject::connect(okBtn, &QPushButton::clicked, dlg,
                         [dlg, save] {
            if (save()) {
                dlg->accept();
            }
        });
    }
    QPushButton* cancelBtn = box->button(QDialogButtonBox::Cancel);
    if (cancelBtn != nullptr) {
        cancelBtn->setText(QStringLiteral("取消"));
    }
    QObject::connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
    dlg->layout()->addWidget(box);
}

// ---- 系统和安全 ----

SystemSecurityDialog::SystemSecurityDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("系统和安全"));
    setMinimumWidth(480);
    auto* lay = new QVBoxLayout(this);

    lay->addWidget(makeSectionTitle(QStringLiteral("关于 Win10DE"), this));
    const w10de::Config config = w10de::Config::load(configPath().toStdString());
    const QString mode = QString::fromStdString(
        config.get("theme", "mode", "dark"));
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->addWidget(new QLabel(QStringLiteral("版本"), this), 0, 0);
    grid->addWidget(new QLabel(QStringLiteral("Win10DE 0.1.0"), this), 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("平台"), this), 1, 0);
    grid->addWidget(new QLabel(QStringLiteral("wlroots 0.19 · Qt %1")
        .arg(QString::fromLatin1(qVersion())), this), 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("当前主题"), this), 2, 0);
    grid->addWidget(new QLabel(mode == "light" ? QStringLiteral("浅色")
                                               : QStringLiteral("深色"),
                               this), 2, 1);
    lay->addLayout(grid);

    lay->addWidget(makeSectionTitle(QStringLiteral("会话"), this));
    autostartCheck_ = new QCheckBox(
        QStringLiteral("登录时自动启动 Win10DE 会话"), this);
    const QString autostartFile = QDir::homePath()
        + QStringLiteral("/.config/autostart/w10de.desktop");
    autostartCheck_->setChecked(QFileInfo::exists(autostartFile));
    lay->addWidget(autostartCheck_);
    lay->addWidget(makeHint(QStringLiteral(
        "写入 ~/.config/autostart/w10de.desktop"), this));

    lay->addWidget(makeSectionTitle(QStringLiteral("电源"), this));
    batteryValue_ = new QLabel(this);
    batteryValue_->setWordWrap(true);
    lay->addWidget(batteryValue_);
    backlightValue_ = new QLabel(this);
    backlightValue_->setWordWrap(true);
    lay->addWidget(backlightValue_);
    brightnessSlider_ = new QSlider(Qt::Horizontal, this);
    brightnessSlider_->setRange(0, 100);
    brightnessSlider_->setEnabled(false);
    lay->addWidget(brightnessSlider_);
    connect(brightnessSlider_, &QSlider::valueChanged, this,
            [this](int value) {
        // 拖动中不写；释放瞬间写一次（键盘调节即时生效）。
        if (backlightDevice_.isEmpty() || backlightMax_ <= 0) {
            return;
        }
        if (!brightnessSlider_->isSliderDown()) {
            const int abs = value * backlightMax_ / 100;
            w10de::settings::PowerInfo::setBrightness(backlightDevice_, abs);
        }
    });
    connect(brightnessSlider_, &QSlider::sliderReleased, this, [this] {
        if (backlightDevice_.isEmpty() || backlightMax_ <= 0) {
            return;
        }
        const int abs = brightnessSlider_->value() * backlightMax_ / 100;
        w10de::settings::PowerInfo::setBrightness(backlightDevice_, abs);
    });
    refreshPower();

    auto* linkRow = new QHBoxLayout;
    auto* monitorBtn = new QPushButton(
        QStringLiteral("打开任务管理器（w10monitor）"), this);
    linkRow->addWidget(monitorBtn);
    linkRow->addStretch(1);
    lay->addLayout(linkRow);
    connect(monitorBtn, &QPushButton::clicked, this, [this] {
        QProcess::startDetached(QStringLiteral("w10monitor"), {});
        accept();
    });

    // G1 双入口全功能：快捷键/窗口规则入口（w10settings 对应页共享后端）。
    auto* extraRow = new QHBoxLayout;
    auto* shortcutsBtn = new QPushButton(QStringLiteral("快捷键…"), this);
    auto* rulesBtn = new QPushButton(QStringLiteral("窗口规则…"), this);
    extraRow->addWidget(shortcutsBtn);
    extraRow->addWidget(rulesBtn);
    extraRow->addStretch(1);
    lay->addLayout(extraRow);
    connect(shortcutsBtn, &QPushButton::clicked, this, [this] {
        ShortcutsDialog dlg(this);
        dlg.exec();
    });
    connect(rulesBtn, &QPushButton::clicked, this, [this] {
        WindowRulesDialog dlg(this);
        dlg.exec();
    });

    lay->addStretch(1);
    attachDialogButtons(this, [this] { return save(); });
}

void SystemSecurityDialog::refreshPower() {
    const w10de::settings::BatteryInfo battery =
        w10de::settings::PowerInfo::battery();
    if (battery.present) {
        const QString statusText =
            battery.status == QStringLiteral("Charging") ? QStringLiteral("充电中")
            : battery.status == QStringLiteral("Discharging") ? QStringLiteral("放电中")
            : battery.status == QStringLiteral("Full") ? QStringLiteral("已充满")
            : battery.status;
        const QString pctText = battery.percent >= 0
            ? QString::number(battery.percent) + QStringLiteral("%")
            : QStringLiteral("--");
        batteryValue_->setText(QStringLiteral("电池：%1，状态：%2%3")
            .arg(battery.device).arg(pctText).arg(statusText));
    } else {
        batteryValue_->setText(QStringLiteral("电池：未检测到（桌面/无电池设备）。"));
    }
    const w10de::settings::BacklightInfo backlight =
        w10de::settings::PowerInfo::backlight();
    if (backlight.present && backlight.maxBrightness > 0) {
        backlightDevice_ = backlight.device;
        backlightMax_ = backlight.maxBrightness;
        const bool writable =
            w10de::settings::PowerInfo::backlightWritable(backlight.device);
        backlightValue_->setText(QStringLiteral("屏幕亮度：%1 / %2%3")
            .arg(backlight.device).arg(backlight.brightness)
            .arg(backlight.maxBrightness)
            .arg(writable ? QString() : QStringLiteral("（无写权限）")));
        brightnessSlider_->setEnabled(writable);
        const QSignalBlocker blocker(brightnessSlider_);
        brightnessSlider_->setValue(
            backlight.brightness * 100 / backlight.maxBrightness);
    } else {
        backlightDevice_.clear();
        backlightMax_ = 0;
        backlightValue_->setText(
            QStringLiteral("屏幕亮度：无背光设备（headless/外接显示器）。"));
        brightnessSlider_->setEnabled(false);
    }
}

bool SystemSecurityDialog::save() {
    const QString autostartDir =
        QDir::homePath() + QStringLiteral("/.config/autostart");
    const QString autostartFile = autostartDir + QStringLiteral("/w10de.desktop");
    const bool on = autostartCheck_->isChecked();
    const bool exists = QFileInfo::exists(autostartFile);
    if (on && !exists) {
        if (!QDir().mkpath(autostartDir)) {
            QMessageBox::warning(this, QStringLiteral("控制面板"),
                QStringLiteral("创建 autostart 目录失败。"));
            return false;
        }
        QFile f(autostartFile);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::warning(this, QStringLiteral("控制面板"),
                QStringLiteral("写入 autostart 失败。"));
            return false;
        }
        QTextStream ts(&f);
        ts << "[Desktop Entry]\n"
           << "Type=Application\n"
           << "Name=Win10DE Session\n"
           << "Exec=w10-session\n"
           << "X-GNOME-Autostart-enabled=true\n";
        f.close();
    } else if (!on && exists) {
        QFile::remove(autostartFile);
    }
    return true;
}

// ---- 外观和个性化 ----

AppearanceDialog::AppearanceDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("外观和个性化"));
    setMinimumWidth(480);
    auto* lay = new QVBoxLayout(this);

    lay->addWidget(makeSectionTitle(QStringLiteral("主题"), this));
    themeCombo_ = new QComboBox(this);
    themeCombo_->addItem(QStringLiteral("深色（Win10 深色任务栏/标题栏）"));
    themeCombo_->addItem(QStringLiteral("浅色（Win10 浅色 + 深色文字）"));
    const w10de::Config config = w10de::Config::load(configPath().toStdString());
    themeCombo_->setCurrentIndex(
        config.get("theme", "mode", "dark") == "light" ? 1 : 0);
    lay->addWidget(themeCombo_);
    lay->addWidget(makeHint(
        QStringLiteral("主题修改需重启会话生效。"), this));

    lay->addWidget(makeSectionTitle(QStringLiteral("壁纸"), this));
    auto* wpRow = new QHBoxLayout;
    wallpaperEdit_ = new QLineEdit(this);
    wallpaperEdit_->setPlaceholderText(
        QStringLiteral("图片路径（留空 = 内置渐变）"));
    wallpaperEdit_->setText(
        QString::fromStdString(config.get("wallpaper", "path")));
    auto* browseBtn = new QPushButton(QStringLiteral("浏览…"), this);
    wpRow->addWidget(wallpaperEdit_, 1);
    wpRow->addWidget(browseBtn);
    lay->addLayout(wpRow);
    connect(browseBtn, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("选择壁纸"), QDir::homePath(),
            QStringLiteral("图片 (*.png *.jpg *.jpeg *.svg)"));
        if (!file.isEmpty()) {
            wallpaperEdit_->setText(file);
        }
    });
    lay->addWidget(makeHint(
        QStringLiteral("壁纸修改需重启会话生效。"), this));

    lay->addWidget(makeSectionTitle(QStringLiteral("Night Light（夜间色温）"), this));
    const w10de::ipc::NightLightConfig nightCfg =
        w10de::ipc::loadNightLightConfig(configPath().toStdString());
    nightEnabledCheck_ = new QCheckBox(QStringLiteral("启用 Night Light"), this);
    nightEnabledCheck_->setChecked(nightCfg.enabled);
    lay->addWidget(nightEnabledCheck_);
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    grid->addWidget(new QLabel(QStringLiteral("夜间色温"), this), 0, 0);
    auto* tempRow = new QHBoxLayout;
    nightTempSlider_ = new QSlider(Qt::Horizontal, this);
    nightTempSlider_->setRange(1000, 8000);
    nightTempSlider_->setSingleStep(100);
    nightTempSlider_->setValue(nightCfg.temperature);
    tempRow->addWidget(nightTempSlider_, 1);
    nightTempValue_ = new QLabel(this);
    tempRow->addWidget(nightTempValue_);
    grid->addLayout(tempRow, 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("开始时间"), this), 1, 0);
    nightStartEdit_ = new QTimeEdit(this);
    nightStartEdit_->setDisplayFormat(QStringLiteral("HH:mm"));
    nightStartEdit_->setTime(QTime(nightCfg.startMinutes / 60,
                                   nightCfg.startMinutes % 60));
    grid->addWidget(nightStartEdit_, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("结束时间"), this), 2, 0);
    nightEndEdit_ = new QTimeEdit(this);
    nightEndEdit_->setDisplayFormat(QStringLiteral("HH:mm"));
    nightEndEdit_->setTime(QTime(nightCfg.endMinutes / 60,
                                 nightCfg.endMinutes % 60));
    grid->addWidget(nightEndEdit_, 2, 1);
    lay->addLayout(grid);
    auto updateTempLabel = [this]() {
        nightTempValue_->setText(QStringLiteral("%1 K")
            .arg(nightTempSlider_->value()));
    };
    connect(nightTempSlider_, &QSlider::valueChanged, this,
            [updateTempLabel](int) { updateTempLabel(); });
    updateTempLabel();
    lay->addWidget(makeHint(QStringLiteral(
        "Night Light 经 org.w10de.Compositor 热应用；主题/壁纸需重启会话。"),
        this));

    lay->addStretch(1);
    attachDialogButtons(this, [this] { return save(); });
}

QString AppearanceDialog::configPath() const {
    return w10de::control::configPath();
}

bool AppearanceDialog::save() {
    w10de::Config config = w10de::Config::load(configPath().toStdString());
    config.set("theme", "mode",
               themeCombo_->currentIndex() == 1 ? "light" : "dark");
    config.set("wallpaper", "path", wallpaperEdit_->text().trimmed().toStdString());
    const QTime start = nightStartEdit_->time();
    const QTime end = nightEndEdit_->time();
    // 审查 M2（G1）：开始=结束会与启动加载回退默认的语义漂移——拒绝。
    if (start == end) {
        QMessageBox::warning(this, QStringLiteral("控制面板"),
            QStringLiteral("Night Light 开始与结束时间不能相同。"));
        return false;
    }
    const auto hhmm = [](const QTime& t) {
        return QStringLiteral("%1:%2")
            .arg(t.hour(), 2, 10, QLatin1Char('0'))
            .arg(t.minute(), 2, 10, QLatin1Char('0')).toStdString();
    };
    config.set("night_light", "enabled",
               nightEnabledCheck_->isChecked() ? "1" : "0");
    config.set("night_light", "temperature",
               std::to_string(nightTempSlider_->value()));
    config.set("night_light", "start_time", hhmm(start));
    config.set("night_light", "end_time", hhmm(end));
    if (!config.save(configPath().toStdString())) {
        QMessageBox::warning(this, QStringLiteral("控制面板"),
            QStringLiteral("保存配置失败（%1）。").arg(configPath()));
        return false;
    }
    // Night Light 热应用；审查 M4（G1）：检查返回，失败时提示（与
    // w10settings 的降级提示一致），不再 fire-and-forget。
    QDBusInterface iface = compositorIface(this);
    if (iface.isValid()) {
        const QDBusMessage reply = iface.call(QStringLiteral("SetNightLight"),
            QVariant(nightEnabledCheck_->isChecked()),
            QVariant(nightTempSlider_->value()),
            QVariant(start.hour() * 60 + start.minute()),
            QVariant(end.hour() * 60 + end.minute()));
        if (reply.type() == QDBusMessage::ErrorMessage) {
            QMessageBox::warning(this, QStringLiteral("控制面板"),
                QStringLiteral("配置已保存；热应用失败（%1），重启会话后生效。")
                    .arg(reply.errorMessage()));
        }
    }
    return true;
}

// ---- 硬件和声音 ----

HardwareSoundDialog::HardwareSoundDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("硬件和声音"));
    setMinimumSize(640, 480);
    auto* outer = new QVBoxLayout(this);

    auto* tabs = new QTabWidget(this);
    outer->addWidget(tabs);

    // ---- 显示 tab ----
    {
        auto* page = new QWidget(tabs);
        auto* lay = new QVBoxLayout(page);
        lay->addWidget(makeHint(QStringLiteral(
            "经 org.w10de.Compositor 热应用（无需重启会话）。"), page));
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(8);
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
        lay->addWidget(makeSectionTitle(QStringLiteral("排列"), page));
        arrangement_ = new w10de::common::MonitorArrangementWidget(page);
        lay->addWidget(arrangement_);
        auto* arrBtn = new QPushButton(QStringLiteral("应用排列"), page);
        lay->addWidget(arrBtn, 0, Qt::AlignLeft);
        displayStatus_ = new QLabel(QStringLiteral(
            "未连接合成器（org.w10de.Compositor 不可用）。"), page);
        displayStatus_->setStyleSheet(secondaryStyle());
        displayStatus_->setWordWrap(true);
        lay->addWidget(displayStatus_);
        lay->addStretch(1);
        connect(refreshBtn, &QPushButton::clicked, this,
                &HardwareSoundDialog::refreshOutputs);
        connect(applyBtn, &QPushButton::clicked, this,
                &HardwareSoundDialog::applyDisplaySettings);
        connect(arrBtn, &QPushButton::clicked, this,
                &HardwareSoundDialog::applyArrangement);
        connect(outputCombo_, &QComboBox::currentIndexChanged, this,
                &HardwareSoundDialog::loadOutputDetails);
        tabs->addTab(page, QStringLiteral("显示"));
    }

    // ---- 音频 tab ----
    {
        auto* page = new QWidget(tabs);
        auto* lay = new QVBoxLayout(page);
        lay->addWidget(makeHint(QStringLiteral(
            "输出设备与音量（libpulse 客户端；真机 PipeWire 提供服务）。"), page));
        audioStatus_ = new QLabel(QStringLiteral("（检测中…）"), page);
        audioStatus_->setWordWrap(true);
        lay->addWidget(audioStatus_);
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(8);
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
        lay->addWidget(makeSectionTitle(QStringLiteral("应用音量"), page));
        appStreamTable_ = new QTableWidget(page);
        appStreamTable_->setColumnCount(3);
        appStreamTable_->setHorizontalHeaderLabels(
            {QStringLiteral("应用"), QStringLiteral("音量"), QStringLiteral("静音")});
        appStreamTable_->horizontalHeader()->setStretchLastSection(false);
        appStreamTable_->horizontalHeader()->setSectionResizeMode(
            0, QHeaderView::Stretch);
        appStreamTable_->horizontalHeader()->setSectionResizeMode(
            1, QHeaderView::ResizeToContents);
        appStreamTable_->horizontalHeader()->setSectionResizeMode(
            2, QHeaderView::ResizeToContents);
        appStreamTable_->verticalHeader()->setVisible(false);
        appStreamTable_->setSelectionMode(QAbstractItemView::NoSelection);
        appStreamTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        appStreamTable_->setFixedHeight(140);
        lay->addWidget(appStreamTable_);
        appStreamStatus_ = new QLabel(QStringLiteral("（无活动应用）"), page);
        appStreamStatus_->setStyleSheet(secondaryStyle());
        lay->addWidget(appStreamStatus_);
        lay->addStretch(1);

        audio_ = new w10de::settings::AudioInfo(this);
        connect(refreshBtn, &QPushButton::clicked, this,
                &HardwareSoundDialog::refreshAudio);
        connect(audio_, &w10de::settings::AudioInfo::sinksReady, this,
                &HardwareSoundDialog::onAudioSinksReady);
        connect(audio_, &w10de::settings::AudioInfo::appStreamsReady, this,
                &HardwareSoundDialog::onAudioAppStreamsReady);
        connect(audio_, &w10de::settings::AudioInfo::connectionFailed, this,
                [this](const QString& reason) {
            audioStatus_->setText(reason);
            sinkCombo_->setEnabled(false);
            volumeSlider_->setEnabled(false);
            muteCheck_->setEnabled(false);
            appStreamTable_->setRowCount(0);
            appStreamStatus_->setText(reason);
        });
        connect(volumeSlider_, &QSlider::valueChanged, this, [this](int value) {
            if (!volumeSlider_->isSliderDown() && sinkCombo_->currentIndex() >= 0) {
                applyAudioVolume(value);
            }
        });
        connect(volumeSlider_, &QSlider::sliderReleased, this, [this] {
            if (sinkCombo_->currentIndex() >= 0) {
                applyAudioVolume(volumeSlider_->value());
            }
        });
        connect(muteCheck_, &QCheckBox::toggled, this,
                &HardwareSoundDialog::toggleAudioMute);
        connect(sinkCombo_, &QComboBox::currentIndexChanged, this,
                [this](int index) {
            if (index < 0 || index >= sinkCache_.size()) {
                return;
            }
            const w10de::settings::SinkInfo& s = sinkCache_.at(index);
            const QSignalBlocker vb(volumeSlider_);
            volumeSlider_->setValue(s.volumePercent);
            const QSignalBlocker mb(muteCheck_);
            muteCheck_->setChecked(s.muted);
        });
        tabs->addTab(page, QStringLiteral("音频"));
    }

    // ---- 蓝牙 tab ----
    {
        auto* page = new QWidget(tabs);
        auto* lay = new QVBoxLayout(page);
        lay->addWidget(makeHint(QStringLiteral(
            "经 Bluez D-Bus 查询适配器与设备（服务缺失时显示不可用）。"), page));
        auto* switchRow = new QHBoxLayout;
        bluetoothSwitch_ = new QCheckBox(QStringLiteral("蓝牙开关"), page);
        switchRow->addWidget(bluetoothSwitch_);
        switchRow->addStretch(1);
        lay->addLayout(switchRow);
        connect(bluetoothSwitch_, &QCheckBox::toggled, this,
                &HardwareSoundDialog::toggleBluetooth);
        bluetoothStatus_ = new QLabel(page);
        bluetoothStatus_->setWordWrap(true);
        lay->addWidget(bluetoothStatus_);
        bluetoothDevices_ = new QLabel(page);
        bluetoothDevices_->setStyleSheet(secondaryStyle());
        bluetoothDevices_->setWordWrap(true);
        lay->addWidget(bluetoothDevices_);
        auto* btnRow = new QHBoxLayout;
        auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), page);
        btnRow->addWidget(refreshBtn);
        btnRow->addStretch(1);
        lay->addLayout(btnRow);
        connect(refreshBtn, &QPushButton::clicked, this,
                &HardwareSoundDialog::refreshBluetooth);
        lay->addStretch(1);
        refreshBluetooth();
        tabs->addTab(page, QStringLiteral("蓝牙"));
    }

    // ---- 输入设备 tab ----
    {
        auto* page = new QWidget(tabs);
        auto* lay = new QVBoxLayout(page);
        lay->addWidget(makeHint(QStringLiteral(
            "鼠标/触摸板/键盘设置。经 org.w10de.Compositor 热应用；"
            "headless 自动降级为仅保存配置。"), page));
        const w10de::ipc::InputSettings current =
            w10de::ipc::InputSettings::load(configPath().toStdString());
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(8);
        const auto makeLabel = [page](const QString& text) {
            auto* l = new QLabel(text, page);
            l->setStyleSheet(secondaryStyle());
            return l;
        };
        grid->addWidget(makeLabel(QStringLiteral("指针速度")), 0, 0);
        auto* speedRow = new QHBoxLayout;
        pointerSpeedSlider_ = new QSlider(Qt::Horizontal, page);
        pointerSpeedSlider_->setRange(-100, 100);
        pointerSpeedSlider_->setValue(
            qBound(-100, qRound(current.pointerSpeed * 100.0), 100));
        speedRow->addWidget(pointerSpeedSlider_, 1);
        pointerSpeedValue_ = new QLabel(page);
        speedRow->addWidget(pointerSpeedValue_);
        grid->addLayout(speedRow, 0, 1);
        naturalScrollCheck_ = new QCheckBox(
            QStringLiteral("自然滚动（触摸板/滚轮反向）"), page);
        naturalScrollCheck_->setChecked(current.naturalScroll);
        grid->addWidget(naturalScrollCheck_, 1, 1);
        grid->addWidget(makeLabel(QStringLiteral("滚动")), 1, 0);
        leftHandedCheck_ = new QCheckBox(
            QStringLiteral("左手模式（交换主键）"), page);
        leftHandedCheck_->setChecked(current.leftHanded);
        grid->addWidget(leftHandedCheck_, 2, 1);
        grid->addWidget(makeLabel(QStringLiteral("按键")), 2, 0);
        tapToClickCheck_ = new QCheckBox(
            QStringLiteral("触摸板点击（Tap 单击）"), page);
        tapToClickCheck_->setChecked(current.tapToClick);
        grid->addWidget(tapToClickCheck_, 3, 1);
        grid->addWidget(makeLabel(QStringLiteral("触摸板")), 3, 0);
        grid->addWidget(makeLabel(QStringLiteral("重复速率")), 4, 0);
        auto* rateRow = new QHBoxLayout;
        repeatRateSlider_ = new QSlider(Qt::Horizontal, page);
        repeatRateSlider_->setRange(1, 100);
        repeatRateSlider_->setValue(qBound(1, current.repeatRate, 100));
        rateRow->addWidget(repeatRateSlider_, 1);
        repeatRateValue_ = new QLabel(page);
        rateRow->addWidget(repeatRateValue_);
        grid->addLayout(rateRow, 4, 1);
        grid->addWidget(makeLabel(QStringLiteral("重复延迟")), 5, 0);
        auto* delayRow = new QHBoxLayout;
        repeatDelaySlider_ = new QSlider(Qt::Horizontal, page);
        repeatDelaySlider_->setRange(100, 5000);
        repeatDelaySlider_->setSingleStep(100);
        repeatDelaySlider_->setValue(qBound(100, current.repeatDelay, 5000));
        delayRow->addWidget(repeatDelaySlider_, 1);
        repeatDelayValue_ = new QLabel(page);
        delayRow->addWidget(repeatDelayValue_);
        grid->addLayout(delayRow, 5, 1);
        lay->addLayout(grid);
        auto* btnRow = new QHBoxLayout;
        auto* applyBtn = new QPushButton(QStringLiteral("应用"), page);
        btnRow->addWidget(applyBtn);
        btnRow->addStretch(1);
        lay->addLayout(btnRow);
        connect(applyBtn, &QPushButton::clicked, this,
                &HardwareSoundDialog::saveInputSettings);
        auto updateSpeedLabel = [this]() {
            const int v = pointerSpeedSlider_->value();
            pointerSpeedValue_->setText(QStringLiteral("%1%%").arg(
                (v > 0 ? QStringLiteral("+") : QString()) + QString::number(v)));
        };
        connect(pointerSpeedSlider_, &QSlider::valueChanged, this,
                [updateSpeedLabel](int) { updateSpeedLabel(); });
        updateSpeedLabel();
        auto updateRateLabel = [this]() {
            repeatRateValue_->setText(QStringLiteral("%1 字符/秒")
                .arg(repeatRateSlider_->value()));
        };
        connect(repeatRateSlider_, &QSlider::valueChanged, this,
                [updateRateLabel](int) { updateRateLabel(); });
        updateRateLabel();
        auto updateDelayLabel = [this]() {
            repeatDelayValue_->setText(QStringLiteral("%1 ms")
                .arg(repeatDelaySlider_->value()));
        };
        connect(repeatDelaySlider_, &QSlider::valueChanged, this,
                [updateDelayLabel](int) { updateDelayLabel(); });
        updateDelayLabel();
        lay->addStretch(1);
        tabs->addTab(page, QStringLiteral("输入设备"));
    }

    outer->addStretch(1);
    attachDialogButtons(this, [this] { return save(); });
}

QString HardwareSoundDialog::configPath() const {
    return w10de::control::configPath();
}

void HardwareSoundDialog::refreshOutputs() {
    QDBusInterface iface = compositorIface(this);
    if (!iface.isValid()) {
        displayStatus_->setText(QStringLiteral(
            "合成器 D-Bus 服务不可用（org.w10de.Compositor）。"));
        outputCombo_->clear();
        outputNames_.clear();
        return;
    }
    const QDBusMessage msg = iface.call(QStringLiteral("GetOutputs"));
    if (msg.type() == QDBusMessage::ErrorMessage) {
        displayStatus_->setText(
            QStringLiteral("GetOutputs 失败：%1").arg(msg.errorMessage()));
        return;
    }
    if (msg.arguments().isEmpty()) {
        displayStatus_->setText(QStringLiteral("GetOutputs 返回空。"));
        return;
    }
    static const bool reg = [] {
        qDBusRegisterMetaType<w10de::common::OutputInfo>();
        return true;
    }();
    Q_UNUSED(reg);
    const QList<w10de::common::OutputInfo> outputs =
        qdbus_cast<QList<w10de::common::OutputInfo>>(msg.arguments().at(0));
    outputCombo_->clear();
    outputNames_.clear();
    for (const w10de::common::OutputInfo& o : outputs) {
        outputCombo_->addItem(QStringLiteral("%1（%2×%3，缩放 %4%%）")
            .arg(o.name).arg(o.w).arg(o.h).arg(o.scale));
        outputNames_.append(o.name);
    }
    arrangement_->setOutputs(outputs);
    if (outputs.isEmpty()) {
        displayStatus_->setText(QStringLiteral("合成器无输出。"));
        return;
    }
    curWidth_ = outputs.first().w;
    curHeight_ = outputs.first().h;
    curScalePercent_ = outputs.first().scale;
    loadOutputDetails(0);
    displayStatus_->setText(QStringLiteral("已连接合成器。"));
}

void HardwareSoundDialog::loadOutputDetails(int index) {
    if (index < 0 || index >= outputNames_.size()) {
        return;
    }
    const QString name = outputNames_.at(index);
    QDBusInterface iface = compositorIface(this);
    if (!iface.isValid()) {
        return;
    }
    modeCombo_->clear();
    modeMap_.clear();
    QSet<QString> seen;
    const QDBusMessage modesMsg = iface.call(QStringLiteral("GetModes"), name);
    bool haveModes = false;
    if (modesMsg.type() == QDBusMessage::ReplyMessage
            && !modesMsg.arguments().isEmpty()) {
        static const bool reg = [] {
            qDBusRegisterMetaType<w10de::common::ModeInfo>();
            return true;
        }();
        Q_UNUSED(reg);
        const QList<w10de::common::ModeInfo> modes =
            qdbus_cast<QList<w10de::common::ModeInfo>>(
                modesMsg.arguments().at(0));
        for (const w10de::common::ModeInfo& m : modes) {
            if (m.w > 0 && m.h > 0
                    && !seen.contains(QStringLiteral("%1x%2").arg(m.w).arg(m.h))) {
                modeCombo_->addItem(QStringLiteral("%1 × %2").arg(m.w).arg(m.h));
                modeMap_.append({QStringLiteral("%1x%2").arg(m.w).arg(m.h), m.h});
                seen.insert(QStringLiteral("%1x%2").arg(m.w).arg(m.h));
                haveModes = true;
            }
        }
    }
    if (!haveModes) {
        const QList<QPair<int, int>> common = {
            {1920, 1080}, {1366, 768}, {1280, 720}, {1024, 768}, {800, 600}};
        for (const auto& [w, h] : common) {
            modeCombo_->addItem(QStringLiteral("%1 × %2").arg(w).arg(h));
            modeMap_.append({QStringLiteral("%1x%2").arg(w).arg(h), h});
        }
    }
    const QString cur = QStringLiteral("%1x%2").arg(curWidth_).arg(curHeight_);
    int curIdx = -1;
    for (int i = 0; i < modeMap_.size(); ++i) {
        if (modeMap_.at(i).first == cur) {
            curIdx = i;
            break;
        }
    }
    if (curIdx < 0) {
        modeCombo_->addItem(QStringLiteral("%1 × %2（当前）")
            .arg(curWidth_).arg(curHeight_));
        modeMap_.append({cur, curHeight_});
        curIdx = modeMap_.size() - 1;
    }
    modeCombo_->setCurrentIndex(curIdx);
    int scaleIdx = scaleCombo_->findData(curScalePercent_);
    if (scaleIdx < 0) {
        scaleCombo_->addItem(
            QStringLiteral("%1%%（当前）").arg(curScalePercent_),
            curScalePercent_);
        scaleIdx = scaleCombo_->count() - 1;
    }
    scaleCombo_->setCurrentIndex(scaleIdx);
}

void HardwareSoundDialog::applyDisplaySettings() {
    if (outputNames_.isEmpty()) {
        return;
    }
    const int idx = outputCombo_->currentIndex();
    if (idx < 0 || idx >= outputNames_.size()) {
        return;
    }
    const QString name = outputNames_.at(idx);
    QDBusInterface iface = compositorIface(this);
    if (!iface.isValid()) {
        displayStatus_->setText(QStringLiteral("合成器 D-Bus 服务不可用。"));
        return;
    }
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
        const QDBusReply<void> r =
            iface.call(QStringLiteral("SetMode"), name, w, h);
        if (!r.isValid()) {
            ok = false;
            errMsg = r.error().message();
        }
    }
    if (ok) {
        const QDBusReply<void> r =
            iface.call(QStringLiteral("SetScale"), name, scale);
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
        refreshOutputs();
    }
}

void HardwareSoundDialog::applyArrangement() {
    if (arrangement_ == nullptr || !arrangement_->hasChanges()) {
        displayStatus_->setText(QStringLiteral("未检测到排列变化。"));
        return;
    }
    QDBusInterface iface = compositorIface(this);
    if (!iface.isValid()) {
        displayStatus_->setText(QStringLiteral("合成器 D-Bus 服务不可用。"));
        return;
    }
    int applied = 0;
    int total = 0;
    QString errMsg;
    for (const w10de::common::OutputInfo& o : arrangement_->positions()) {
        ++total;
        const QDBusReply<void> r =
            iface.call(QStringLiteral("SetPosition"), o.name, o.x, o.y);
        if (!r.isValid()) {
            errMsg = r.error().message();
            break;
        }
        ++applied;
    }
    displayStatus_->setText(applied == total
        ? QStringLiteral("已应用排列：%1 个输出。").arg(applied)
        : QStringLiteral("已应用排列 %1/%2 个输出，失败：%3")
              .arg(applied).arg(total).arg(errMsg));
    if (applied > 0) {
        refreshOutputs();
    }
}

void HardwareSoundDialog::refreshAudio() {
    if (audio_ == nullptr) {
        return;
    }
    audioStatus_->setText(QStringLiteral("（检测中…）"));
    audio_->refreshSinks();
    audio_->refreshAppStreams();
}

void HardwareSoundDialog::onAudioSinksReady(
        const QList<w10de::settings::SinkInfo>& sinks) {
    sinkCache_ = sinks;
    sinkCombo_->clear();
    sinkNames_.clear();
    for (const w10de::settings::SinkInfo& s : sinks) {
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
    const w10de::settings::SinkInfo& first = sinks.first();
    const QSignalBlocker vb(volumeSlider_);
    volumeSlider_->setValue(first.volumePercent);
    const QSignalBlocker mb(muteCheck_);
    muteCheck_->setChecked(first.muted);
    audioStatus_->setText(QStringLiteral("已连接 PulseAudio（%1 个输出设备）。")
        .arg(sinks.size()));
}

void HardwareSoundDialog::onAudioAppStreamsReady(
        const QList<w10de::settings::AppStreamInfo>& streams) {
    appRowIndex_.clear();
    appStreamTable_->setRowCount(streams.size());
    for (int row = 0; row < streams.size(); ++row) {
        const w10de::settings::AppStreamInfo& s = streams.at(row);
        appRowIndex_.insert(row, s.index);
        appStreamTable_->setItem(row, 0, new QTableWidgetItem(
            s.application.isEmpty() ? s.name
                                    : QStringLiteral("%1（%2）")
                                          .arg(s.application, s.name)));
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 100);
        slider->setValue(s.volumePercent);
        appStreamTable_->setCellWidget(row, 1, slider);
        connect(slider, &QSlider::valueChanged, this,
                [this, row, slider](int value) {
            if (!slider->isSliderDown()) {
                applyAppVolume(row, value);
            }
        });
        connect(slider, &QSlider::sliderReleased, this,
                [this, row, slider] {
            applyAppVolume(row, slider->value());
        });
        auto* mute = new QCheckBox(QStringLiteral("静音"));
        mute->setChecked(s.muted);
        appStreamTable_->setCellWidget(row, 2, mute);
        connect(mute, &QCheckBox::toggled, this,
                [this, row](bool on) { toggleAppMute(row, on); });
    }
    appStreamStatus_->setText(streams.isEmpty()
        ? QStringLiteral("（无活动应用）")
        : QStringLiteral("%1 个应用正在播放").arg(streams.size()));
}

void HardwareSoundDialog::applyAppVolume(int row, int value) {
    const auto it = appRowIndex_.constFind(row);
    if (it == appRowIndex_.constEnd() || audio_ == nullptr) {
        return;
    }
    audio_->setAppVolume(it.value(), value);
}

void HardwareSoundDialog::toggleAppMute(int row, bool muted) {
    const auto it = appRowIndex_.constFind(row);
    if (it == appRowIndex_.constEnd() || audio_ == nullptr) {
        return;
    }
    audio_->setAppMuted(it.value(), muted);
}

void HardwareSoundDialog::applyAudioVolume(int value) {
    const int idx = sinkCombo_->currentIndex();
    if (idx < 0 || idx >= sinkNames_.size() || audio_ == nullptr) {
        return;
    }
    audio_->setVolume(sinkNames_.at(idx), value);
}

void HardwareSoundDialog::toggleAudioMute(bool muted) {
    const int idx = sinkCombo_->currentIndex();
    if (idx < 0 || idx >= sinkNames_.size() || audio_ == nullptr) {
        return;
    }
    audio_->setMuted(sinkNames_.at(idx), muted);
}

void HardwareSoundDialog::refreshBluetooth() {
    const w10de::settings::BluetoothStatus st =
        w10de::settings::BluetoothInfo::query();
    if (!st.available) {
        bluetoothSwitch_->setEnabled(false);
        bluetoothStatus_->setText(st.errorText.isEmpty()
            ? QStringLiteral("Bluez 服务不可用（未安装或未运行）。")
            : st.errorText);
        bluetoothDevices_->setText(QString());
        return;
    }
    bluetoothSwitch_->setEnabled(true);
    const QSignalBlocker blocker(bluetoothSwitch_);
    bluetoothSwitch_->setChecked(st.powered);
    bluetoothStatus_->setText(QStringLiteral("适配器：%1（%2）  %3")
        .arg(st.adapterAlias.isEmpty() ? QStringLiteral("hci0") : st.adapterAlias,
             st.adapterAddress,
             st.powered ? QStringLiteral("已开启") : QStringLiteral("已关闭")));
    if (st.devices.isEmpty()) {
        bluetoothDevices_->setText(QStringLiteral("无已发现设备。"));
    } else {
        QStringList lines;
        for (const w10de::settings::BluetoothDevice& dev : st.devices) {
            const QString state = dev.connected ? QStringLiteral("已连接")
                : (dev.paired ? QStringLiteral("已配对") : QStringLiteral("未配对"));
            lines << QStringLiteral("· %1（%2） %3")
                         .arg(dev.name, dev.address, state);
        }
        bluetoothDevices_->setText(lines.join(QLatin1Char('\n')));
    }
}

void HardwareSoundDialog::toggleBluetooth(bool on) {
    const bool ok = w10de::settings::BluetoothInfo::setPowered(on);
    refreshBluetooth();
    if (!ok) {
        bluetoothStatus_->setText(QStringLiteral(
            "%1（切换失败：Bluez 不可用或权限不足。）")
            .arg(bluetoothStatus_->text()));
    }
}

void HardwareSoundDialog::saveInputSettings() {
    w10de::ipc::InputSettings s;
    s.pointerSpeed = pointerSpeedSlider_->value() / 100.0;
    s.naturalScroll = naturalScrollCheck_->isChecked();
    s.leftHanded = leftHandedCheck_->isChecked();
    s.tapToClick = tapToClickCheck_->isChecked();
    s.repeatRate = repeatRateSlider_->value();
    s.repeatDelay = repeatDelaySlider_->value();
    if (!w10de::ipc::InputSettings::save(configPath().toStdString(), s)) {
        QMessageBox::warning(this, QStringLiteral("控制面板"),
            QStringLiteral("保存配置失败（%1）。").arg(configPath()));
        return;
    }
    QDBusInterface iface = compositorIface(this);
    if (!iface.isValid()) {
        return;  // headless：配置已保存，重启会话生效
    }
    iface.call(QStringLiteral("SetInputSettings"),
        QVariant(s.pointerSpeed), QVariant(s.naturalScroll),
        QVariant(s.leftHanded), QVariant(s.tapToClick),
        QVariant(s.repeatRate), QVariant(s.repeatDelay));
}

bool HardwareSoundDialog::save() {
    // 蓝牙开关即时生效（toggled 已处理）；显示/音频/输入均为即时应用。
    return true;
}

// ---- 网络和 Internet ----

NetworkDialog::NetworkDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("网络和 Internet"));
    setMinimumWidth(480);
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(makeSectionTitle(QStringLiteral("网络"), this));
    lay->addWidget(makeHint(QStringLiteral(
        "经 NetworkManager D-Bus 查询连接状态（服务缺失时显示不可用）。"), this));
    networkStatus_ = new QLabel(this);
    networkStatus_->setWordWrap(true);
    lay->addWidget(networkStatus_);
    networkDetails_ = new QLabel(this);
    networkDetails_->setStyleSheet(secondaryStyle());
    networkDetails_->setWordWrap(true);
    lay->addWidget(networkDetails_);
    auto* btnRow = new QHBoxLayout;
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"), this);
    btnRow->addWidget(refreshBtn);
    btnRow->addStretch(1);
    lay->addLayout(btnRow);
    connect(refreshBtn, &QPushButton::clicked, this,
            &NetworkDialog::refreshNetwork);
    lay->addStretch(1);
    refreshNetwork();
    attachDialogButtons(this, [this] { return save(); });
}

void NetworkDialog::refreshNetwork() {
    const w10de::settings::NetworkStatus st =
        w10de::settings::NetworkInfo::query();
    if (!st.available) {
        networkStatus_->setText(QStringLiteral(
            "NetworkManager 服务不可用（未安装或未运行）。"));
        networkDetails_->setText(QString());
        return;
    }
    networkStatus_->setText(QStringLiteral("状态：%1").arg(st.stateText));
    if (st.connections.isEmpty()) {
        networkDetails_->setText(QStringLiteral("无活动连接。"));
        return;
    }
    QStringList lines;
    for (const w10de::settings::NetworkConnection& conn : st.connections) {
        const QString typeName = conn.type == QStringLiteral("802-11-wireless")
            ? QStringLiteral("无线") : QStringLiteral("有线");
        QString line = QStringLiteral("· %1（%2）").arg(conn.id, typeName);
        if (!conn.ip.isEmpty()) {
            line += QStringLiteral("  IP: %1").arg(conn.ip);
        }
        lines << line;
    }
    networkDetails_->setText(lines.join(QLatin1Char('\n')));
}

// ---- 程序 ----

ProgramsDialog::ProgramsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("程序"));
    setMinimumWidth(480);
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(makeSectionTitle(QStringLiteral("默认应用"), this));
    lay->addWidget(makeHint(QStringLiteral(
        "写入 ~/.config/mimeapps.list（xdg 标准，xdg-open 生效）。"), this));

    const QList<w10de::settings::DesktopApp> apps =
        w10de::settings::DefaultApps::listApplications();
    const QString mimeapps = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation) + QStringLiteral("/mimeapps.list");
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(8);
    for (int k = 0; k < static_cast<int>(w10de::settings::DefaultKind::Count); ++k) {
        const auto kind = static_cast<w10de::settings::DefaultKind>(k);
        auto* label = new QLabel(w10de::settings::defaultKindLabel(kind), this);
        auto* combo = new QComboBox(this);
        combo->addItem(QStringLiteral("（未设置）"), QString());
        for (const w10de::settings::DesktopApp& app : apps) {
            combo->addItem(app.name.isEmpty() ? app.id : app.name, app.id);
        }
        const QString current =
            w10de::settings::DefaultApps::currentDefault(mimeapps, kind);
        const int idx = combo->findData(current);
        if (idx >= 0) {
            combo->setCurrentIndex(idx);
        }
        grid->addWidget(label, k, 0);
        grid->addWidget(combo, k, 1);
        defaultCombos_.append(combo);
    }
    lay->addLayout(grid);
    defaultsStatus_ = new QLabel(
        QStringLiteral("已扫描 %1 个应用。").arg(apps.size()), this);
    defaultsStatus_->setStyleSheet(secondaryStyle());
    defaultsStatus_->setWordWrap(true);
    lay->addWidget(defaultsStatus_);

    lay->addWidget(makeSectionTitle(QStringLiteral("软件"), this));
    auto* linkRow = new QHBoxLayout;
    auto* softwareBtn = new QPushButton(
        QStringLiteral("打开软件中心（w10software）"), this);
    linkRow->addWidget(softwareBtn);
    linkRow->addStretch(1);
    lay->addLayout(linkRow);
    connect(softwareBtn, &QPushButton::clicked, this, [this] {
        QProcess::startDetached(QStringLiteral("w10software"), {});
        accept();
    });

    lay->addStretch(1);
    attachDialogButtons(this, [this] { return save(); });
}

bool ProgramsDialog::save() {
    const QString mimeapps = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation) + QStringLiteral("/mimeapps.list");
    for (int k = 0; k < defaultCombos_.size(); ++k) {
        const auto kind = static_cast<w10de::settings::DefaultKind>(k);
        const QString desktopId = defaultCombos_.at(k)->currentData().toString();
        if (desktopId.isEmpty()) {
            auto defaults =
                w10de::settings::DefaultApps::loadMimeDefaults(mimeapps);
            for (const QString& mime : w10de::settings::defaultKindMimes(kind)) {
                defaults.remove(mime);
            }
            if (!w10de::settings::DefaultApps::saveMimeDefaults(mimeapps,
                                                                defaults)) {
                defaultsStatus_->setText(QStringLiteral("写入 mimeapps.list 失败。"));
                return false;
            }
        } else if (!w10de::settings::DefaultApps::setDefault(mimeapps, kind,
                                                             desktopId)) {
            defaultsStatus_->setText(QStringLiteral("写入 mimeapps.list 失败。"));
            return false;
        }
    }
    defaultsStatus_->setText(QStringLiteral("已应用（写入 %1）。").arg(mimeapps));
    return true;
}

// ---- 时钟和区域 ----

ClockRegionDialog::ClockRegionDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("时钟和区域"));
    setMinimumWidth(480);
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(makeSectionTitle(QStringLiteral("日期和时间"), this));
    const QDateTime now = QDateTime::currentDateTime();
    lay->addWidget(new QLabel(QStringLiteral("当前时间：%1")
        .arg(now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))), this));
    const QString tzId = QString::fromUtf8(QTimeZone::systemTimeZoneId());
    const QTimeZone tz = QTimeZone::systemTimeZone();
    lay->addWidget(new QLabel(QStringLiteral("时区：%1（%2）")
        .arg(tzId,
             tz.displayName(QTimeZone::StandardTime, QTimeZone::OffsetName)),
        this));
    lay->addWidget(makeHint(QStringLiteral(
        "日历（任务栏时钟弹月历）见 G6 立项；系统时间设置依赖 timedatectl。"), this));
    lay->addStretch(1);
    attachDialogButtons(this, [this] { return save(); });
}

// ---- 快捷键（[shortcuts] 编辑；与 w10settings 快捷键页共享后端）----

namespace {

// 动作 → 中文名 / 默认绑定文本（与 settings 页一致，见 settingswindow.cpp）。
QString shortcutActionLabel(w10de::ShortcutAction action) {
    switch (action) {
    case w10de::ShortcutAction::Close: return QStringLiteral("关闭窗口");
    case w10de::ShortcutAction::Maximize: return QStringLiteral("最大化/还原");
    case w10de::ShortcutAction::Minimize: return QStringLiteral("最小化");
    case w10de::ShortcutAction::SnapLeft: return QStringLiteral("左半屏");
    case w10de::ShortcutAction::SnapRight: return QStringLiteral("右半屏");
    case w10de::ShortcutAction::SnapUp: return QStringLiteral("最大化（Win+↑）");
    case w10de::ShortcutAction::SnapDown: return QStringLiteral("还原（Win+↓）");
    case w10de::ShortcutAction::SnapLayout: return QStringLiteral("Snap 布局选择器");
    case w10de::ShortcutAction::Lock: return QStringLiteral("锁屏");
    case w10de::ShortcutAction::Quit: return QStringLiteral("退出合成器");
    case w10de::ShortcutAction::Clipboard: return QStringLiteral("剪贴板历史");
    case w10de::ShortcutAction::Workspace1: return QStringLiteral("工作区 1");
    case w10de::ShortcutAction::Workspace2: return QStringLiteral("工作区 2");
    case w10de::ShortcutAction::Workspace3: return QStringLiteral("工作区 3");
    case w10de::ShortcutAction::Workspace4: return QStringLiteral("工作区 4");
    default: return QString();
    }
}

QString defaultShortcutText(w10de::ShortcutAction action) {
    switch (action) {
    case w10de::ShortcutAction::Close: return QStringLiteral("win+q");
    case w10de::ShortcutAction::Maximize: return QStringLiteral("win+f");
    case w10de::ShortcutAction::Minimize: return QStringLiteral("win+m");
    case w10de::ShortcutAction::SnapLeft: return QStringLiteral("win+left");
    case w10de::ShortcutAction::SnapRight: return QStringLiteral("win+right");
    case w10de::ShortcutAction::SnapUp: return QStringLiteral("win+up");
    case w10de::ShortcutAction::SnapDown: return QStringLiteral("win+down");
    case w10de::ShortcutAction::SnapLayout: return QStringLiteral("win+z");
    case w10de::ShortcutAction::Lock: return QStringLiteral("win+l");
    case w10de::ShortcutAction::Quit: return QStringLiteral("win+escape");
    case w10de::ShortcutAction::Clipboard: return QStringLiteral("win+v");
    case w10de::ShortcutAction::Workspace1: return QStringLiteral("win+1");
    case w10de::ShortcutAction::Workspace2: return QStringLiteral("win+2");
    case w10de::ShortcutAction::Workspace3: return QStringLiteral("win+3");
    case w10de::ShortcutAction::Workspace4: return QStringLiteral("win+4");
    default: return QString();
    }
}

}  // namespace

ShortcutsDialog::ShortcutsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("快捷键"));
    setMinimumSize(460, 420);
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(makeHint(QStringLiteral(
        "修改 [shortcuts] 配置；格式如 win+q / ctrl+shift+a。"
        "保存后重启会话生效。"), this));

    table_ = new QTableWidget(this);
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("动作"), QStringLiteral("绑定")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(table_);

    const w10de::Config config =
        w10de::Config::load(configPath().toStdString());
    const int actionCount = static_cast<int>(w10de::ShortcutAction::Count);
    table_->setRowCount(actionCount);
    for (int a = 0; a < actionCount; ++a) {
        const auto action = static_cast<w10de::ShortcutAction>(a);
        auto* nameItem = new QTableWidgetItem(shortcutActionLabel(action));
        nameItem->setData(Qt::UserRole, a);
        table_->setItem(a, 0, nameItem);
        const std::string key = w10de::shortcutActionName(action);
        const QString configured =
            QString::fromStdString(config.get("shortcuts", key));
        table_->setItem(a, 1, new QTableWidgetItem(
            configured.isEmpty()
                ? QStringLiteral("（默认）%1").arg(defaultShortcutText(action))
                : configured));
    }
    connect(table_, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) { editShortcut(row); });

    auto* btnRow = new QHBoxLayout;
    auto* editBtn = new QPushButton(QStringLiteral("修改选中…"), this);
    auto* saveBtn = new QPushButton(QStringLiteral("保存"), this);
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    btnRow->addWidget(editBtn);
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(closeBtn);
    btnRow->addStretch(1);
    lay->addLayout(btnRow);
    connect(editBtn, &QPushButton::clicked, this, [this] {
        editShortcut(table_->currentRow());
    });
    connect(saveBtn, &QPushButton::clicked, this, [this] {
        if (save()) {
            accept();
        }
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void ShortcutsDialog::editShortcut(int row) {
    if (row < 0 || row >= table_->rowCount()) {
        return;
    }
    QString current = table_->item(row, 1)->text();
    if (current.startsWith(QStringLiteral("（默认）"))) {
        current.remove(0, QStringLiteral("（默认）").size());
    }
    bool ok = false;
    const QString text = QInputDialog::getText(this,
        QStringLiteral("修改快捷键"),
        QStringLiteral("输入绑定（如 win+q；留空 = 恢复默认）："),
        QLineEdit::Normal, current, &ok);
    if (!ok) {
        return;
    }
    const std::string trimmed = text.trimmed().toStdString();
    if (!trimmed.empty()) {
        const w10de::ShortcutBinding parsed = w10de::parseShortcut(trimmed);
        if (!parsed.valid()) {
            QMessageBox::warning(this, QStringLiteral("快捷键"),
                QStringLiteral("无效绑定：%1。格式如 win+q / ctrl+shift+a。")
                    .arg(text));
            return;
        }
    }
    const int action = table_->item(row, 0)->data(Qt::UserRole).toInt();
    table_->item(row, 1)->setText(trimmed.empty()
        ? QStringLiteral("（默认）%1").arg(defaultShortcutText(
              static_cast<w10de::ShortcutAction>(action)))
        : QString::fromStdString(trimmed));
    table_->setCurrentCell(row, 0);
}

bool ShortcutsDialog::save() {
    w10de::Config config = w10de::Config::load(configPath().toStdString());
    for (const std::string& key : config.sectionKeys("shortcuts")) {
        config.remove("shortcuts", key);
    }
    for (int row = 0; row < table_->rowCount(); ++row) {
        const int action = table_->item(row, 0)->data(Qt::UserRole).toInt();
        const std::string key = w10de::shortcutActionName(
            static_cast<w10de::ShortcutAction>(action));
        const QString text = table_->item(row, 1)->text();
        if (text.startsWith(QStringLiteral("（默认）"))) {
            config.remove("shortcuts", key);
            continue;
        }
        config.set("shortcuts", key, text.trimmed().toStdString());
    }
    if (!config.save(configPath().toStdString())) {
        QMessageBox::warning(this, QStringLiteral("快捷键"),
            QStringLiteral("保存配置失败（%1）。").arg(configPath()));
        return false;
    }
    return true;
}

// ---- 窗口规则（[window_rules] 增删改；与 w10settings 窗口规则页共享后端）----

namespace {

QString ruleMatchText(const w10de::ipc::WindowRule& r) {
    QStringList parts;
    if (!r.matchAppId.empty()) {
        parts << QStringLiteral("app_id=%1")
                     .arg(QString::fromStdString(r.matchAppId));
    }
    if (!r.matchTitle.empty()) {
        parts << QStringLiteral("title=%1")
                     .arg(QString::fromStdString(r.matchTitle));
    }
    return parts.isEmpty() ? QStringLiteral("（空匹配）")
                           : parts.join(QStringLiteral(" & "));
}

QString ruleActionText(const w10de::ipc::WindowRule& r) {
    QStringList parts;
    if (r.alwaysOnTop) {
        parts << QStringLiteral("置顶");
    }
    if (r.borderless) {
        parts << QStringLiteral("无边框");
    }
    if (r.workspace >= 0) {
        parts << QStringLiteral("工作区 %1").arg(r.workspace + 1);
    }
    if (r.hasGeometry) {
        parts << QStringLiteral("几何 %1,%2,%3,%4")
                     .arg(r.geomX).arg(r.geomY).arg(r.geomW).arg(r.geomH);
    }
    return parts.isEmpty() ? QStringLiteral("（无动作）")
                           : parts.join(QStringLiteral("，"));
}

}  // namespace

WindowRulesDialog::WindowRulesDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("窗口规则"));
    setMinimumSize(520, 400);
    auto* lay = new QVBoxLayout(this);
    lay->addWidget(makeHint(QStringLiteral(
        "按应用/标题匹配窗口并应用动作（对标 KWin Window Rules）。"
        "保存后重启会话生效。"), this));

    rules_ = w10de::ipc::loadWindowRules(configPath().toStdString());
    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("名称"), QStringLiteral("匹配"), QStringLiteral("动作")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(table_);
    refreshTable();
    connect(table_, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) { editRule(row); });

    auto* btnRow = new QHBoxLayout;
    auto* addBtn = new QPushButton(QStringLiteral("新增…"), this);
    auto* editBtn = new QPushButton(QStringLiteral("编辑…"), this);
    auto* delBtn = new QPushButton(QStringLiteral("删除"), this);
    auto* saveBtn = new QPushButton(QStringLiteral("保存"), this);
    auto* closeBtn = new QPushButton(QStringLiteral("关闭"), this);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(editBtn);
    btnRow->addWidget(delBtn);
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(closeBtn);
    btnRow->addStretch(1);
    lay->addLayout(btnRow);
    connect(addBtn, &QPushButton::clicked, this, &WindowRulesDialog::addRule);
    connect(editBtn, &QPushButton::clicked, this, [this] {
        editRule(table_->currentRow());
    });
    connect(delBtn, &QPushButton::clicked, this, [this] {
        removeRule(table_->currentRow());
    });
    connect(saveBtn, &QPushButton::clicked, this, [this] {
        if (save()) {
            accept();
        }
    });
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
}

QString WindowRulesDialog::configPath() const {
    return w10de::control::configPath();
}

void WindowRulesDialog::refreshTable() {
    table_->setRowCount(static_cast<int>(rules_.size()));
    for (int i = 0; i < static_cast<int>(rules_.size()); ++i) {
        const w10de::ipc::WindowRule& r = rules_.at(static_cast<size_t>(i));
        table_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(r.name)));
        table_->setItem(i, 1, new QTableWidgetItem(ruleMatchText(r)));
        table_->setItem(i, 2, new QTableWidgetItem(ruleActionText(r)));
    }
}

void WindowRulesDialog::addRule() {
    w10de::ipc::WindowRule empty;
    w10de::common::RuleDialogFields f;
    QDialog* dlg = w10de::common::makeRuleDialog(this, empty, &f);
    if (dlg->exec() != QDialog::Accepted) {
        delete dlg;
        return;
    }
    const QString name = f.name->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("窗口规则"),
            QStringLiteral("规则名称不能为空。"));
        delete dlg;
        return;
    }
    // S1 修复：禁止破坏 config 行解析的字符（= ; &）。
    const QString inputErr = w10de::common::ruleInputError(
        name, f.matchValue->text(),
        f.secondMatchValue != nullptr ? f.secondMatchValue->text()
                                      : QString());
    if (!inputErr.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("窗口规则"), inputErr);
        delete dlg;
        return;
    }
    for (const w10de::ipc::WindowRule& r : rules_) {
        if (r.name == name.toStdString()) {
            QMessageBox::warning(this, QStringLiteral("窗口规则"),
                QStringLiteral("规则名称已存在：%1").arg(name));
            delete dlg;
            return;
        }
    }
    w10de::ipc::WindowRule rule = w10de::common::ruleFromFields(f, empty);
    if (rule.matchAppId.empty() && rule.matchTitle.empty()) {
        QMessageBox::warning(this, QStringLiteral("窗口规则"),
            QStringLiteral("匹配值不能为空。"));
        delete dlg;
        return;
    }
    if (rule.hasGeometry && (rule.geomW <= 0 || rule.geomH <= 0)) {
        QMessageBox::warning(this, QStringLiteral("窗口规则"),
            QStringLiteral("几何宽高必须为正。"));
        delete dlg;
        return;
    }
    rules_.push_back(rule);
    refreshTable();
    delete dlg;
}

void WindowRulesDialog::editRule(int row) {
    if (row < 0 || row >= static_cast<int>(rules_.size())) {
        return;
    }
    const w10de::ipc::WindowRule original = rules_.at(static_cast<size_t>(row));
    w10de::common::RuleDialogFields f;
    QDialog* dlg = w10de::common::makeRuleDialog(this, original, &f);
    if (dlg->exec() != QDialog::Accepted) {
        delete dlg;
        return;
    }
    const QString name = f.name->text().trimmed();
    if (name.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("窗口规则"),
            QStringLiteral("规则名称不能为空。"));
        delete dlg;
        return;
    }
    // S1 修复：禁止破坏 config 行解析的字符（= ; &）。
    const QString inputErr = w10de::common::ruleInputError(
        name, f.matchValue->text(),
        f.secondMatchValue != nullptr ? f.secondMatchValue->text()
                                      : QString());
    if (!inputErr.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("窗口规则"), inputErr);
        delete dlg;
        return;
    }
    for (size_t i = 0; i < rules_.size(); ++i) {
        if (i != static_cast<size_t>(row)
                && rules_.at(i).name == name.toStdString()) {
            QMessageBox::warning(this, QStringLiteral("窗口规则"),
                QStringLiteral("规则名称已存在：%1").arg(name));
            delete dlg;
            return;
        }
    }
    w10de::ipc::WindowRule rule = w10de::common::ruleFromFields(f, original);
    if (rule.matchAppId.empty() && rule.matchTitle.empty()) {
        QMessageBox::warning(this, QStringLiteral("窗口规则"),
            QStringLiteral("匹配值不能为空。"));
        delete dlg;
        return;
    }
    if (rule.hasGeometry && (rule.geomW <= 0 || rule.geomH <= 0)) {
        QMessageBox::warning(this, QStringLiteral("窗口规则"),
            QStringLiteral("几何宽高必须为正。"));
        delete dlg;
        return;
    }
    rules_[static_cast<size_t>(row)] = rule;
    refreshTable();
    delete dlg;
}

void WindowRulesDialog::removeRule(int row) {
    if (row < 0 || row >= static_cast<int>(rules_.size())) {
        return;
    }
    rules_.erase(rules_.begin() + row);
    refreshTable();
}

bool WindowRulesDialog::save() {
    w10de::Config config = w10de::Config::load(configPath().toStdString());
    for (const std::string& key : config.sectionKeys("window_rules")) {
        config.remove("window_rules", key);
    }
    for (const w10de::ipc::WindowRule& r : rules_) {
        QStringList matchParts;
        if (!r.matchAppId.empty()) {
            matchParts << QStringLiteral("app_id=%1")
                              .arg(QString::fromStdString(r.matchAppId));
        }
        if (!r.matchTitle.empty()) {
            matchParts << QStringLiteral("title=%1")
                              .arg(QString::fromStdString(r.matchTitle));
        }
        const QString match = matchParts.join(QStringLiteral("&"));
        QStringList actParts;
        if (r.alwaysOnTop) {
            actParts << QStringLiteral("always_on_top");
        }
        if (r.borderless) {
            actParts << QStringLiteral("borderless");
        }
        if (r.workspace >= 0) {
            actParts << QStringLiteral("workspace=%1").arg(r.workspace);
        }
        if (r.hasGeometry) {
            actParts << QStringLiteral("geometry=%1,%2,%3,%4")
                            .arg(r.geomX).arg(r.geomY).arg(r.geomW)
                            .arg(r.geomH);
        }
        const QString line = match + QLatin1Char(';')
            + actParts.join(QLatin1Char('|'));
        config.set("window_rules", r.name, line.toStdString());
    }
    if (!config.save(configPath().toStdString())) {
        QMessageBox::warning(this, QStringLiteral("窗口规则"),
            QStringLiteral("保存配置失败（%1）。").arg(configPath()));
        return false;
    }
    return true;
}

}  // namespace w10de::control
