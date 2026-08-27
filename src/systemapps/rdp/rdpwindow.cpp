// RdpWindow 实现（可选拓展 E9 远程桌面）。

#include "systemapps/rdp/rdpwindow.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace w10de::rdp {

namespace {
const char* kFieldStyle =
    "QLineEdit,QSpinBox,QComboBox{background:#3D3D3D;color:#E0E0E0;"
    "border:1px solid #555;border-radius:4px;padding:6px;}"
    "QLineEdit:focus,QSpinBox:focus,QComboBox:focus{border-color:#3D6FB4;}";
const char* kBtnStyle =
    "QPushButton{background:#3D3D3D;color:#E0E0E0;border:none;"
    "border-radius:4px;padding:7px 14px;}"
    "QPushButton:hover{background:#4A4A4A;}"
    "QPushButton:disabled{color:#777;}";
}  // namespace

RdpWindow::RdpWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("远程桌面连接"));
    setFixedSize(460, 420);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 18);
    root->setSpacing(10);

    auto* title = new QLabel(QStringLiteral("远程桌面连接"), this);
    title->setStyleSheet(QStringLiteral(
        "color:#FFFFFF; font-size:20px; font-weight:bold;"));
    root->addWidget(title);

    // 表单。
    auto* form = new QFormLayout;
    form->setSpacing(8);
    auto makeLabel = [](const QString& t) {
        auto* l = new QLabel(t);
        l->setStyleSheet(QStringLiteral("color:#C8CDD3; font-size:13px;"));
        return l;
    };
    hostEdit_ = new QLineEdit(this);
    hostEdit_->setPlaceholderText(QStringLiteral("例如 192.168.1.100 或 host.example.com"));
    hostEdit_->setStyleSheet(kFieldStyle);
    portSpin_ = new QSpinBox(this);
    portSpin_->setRange(1, 65535);
    portSpin_->setValue(3389);
    portSpin_->setStyleSheet(kFieldStyle);
    userEdit_ = new QLineEdit(this);
    userEdit_->setPlaceholderText(QStringLiteral("可选"));
    userEdit_->setStyleSheet(kFieldStyle);
    passEdit_ = new QLineEdit(this);
    passEdit_->setEchoMode(QLineEdit::Password);
    passEdit_->setPlaceholderText(QStringLiteral("可选"));
    passEdit_->setStyleSheet(kFieldStyle);
    sizeCombo_ = new QComboBox(this);
    sizeCombo_->addItems({QStringLiteral("客户端默认"), QStringLiteral("1280x720"),
                          QStringLiteral("1920x1080"), QStringLiteral("2560x1440")});
    sizeCombo_->setStyleSheet(kFieldStyle);
    fullscreenCheck_ = new QCheckBox(QStringLiteral("全屏显示"), this);
    fullscreenCheck_->setStyleSheet(QStringLiteral("color:#C8CDD3;"));
    // 审查 L3：勾选全屏时分辨率不可选（参数被忽略）。
    connect(fullscreenCheck_, &QCheckBox::toggled, this, [this](bool on) {
        sizeCombo_->setEnabled(!on);
    });
    savePassCheck_ = new QCheckBox(QStringLiteral("记住密码"), this);
    savePassCheck_->setStyleSheet(QStringLiteral("color:#C8CDD3;"));

    form->addRow(makeLabel(QStringLiteral("计算机：")), hostEdit_);
    form->addRow(makeLabel(QStringLiteral("端口：")), portSpin_);
    form->addRow(makeLabel(QStringLiteral("用户名：")), userEdit_);
    form->addRow(makeLabel(QStringLiteral("密码：")), passEdit_);
    form->addRow(makeLabel(QStringLiteral("分辨率：")), sizeCombo_);
    form->addRow(QString(), fullscreenCheck_);
    form->addRow(QString(), savePassCheck_);
    root->addLayout(form);

    // 状态区。
    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);
    statusLabel_->setStyleSheet(
        QStringLiteral("color:#9AA0A6; font-size:12px;"));
    statusLabel_->setText(clientAvailable()
        ? QStringLiteral("已找到 RDP 客户端（%1）")
              .arg(QFileInfo(findClient()).fileName())
        : QStringLiteral("未找到 RDP 客户端，请安装 freerdp 包"));
    root->addWidget(statusLabel_);

    // 按钮行。
    connectButton_ = new QPushButton(QStringLiteral("连接"), this);
    disconnectButton_ = new QPushButton(QStringLiteral("断开"), this);
    auto* saveButton = new QPushButton(QStringLiteral("保存"), this);
    auto* cancelButton = new QPushButton(QStringLiteral("取消"), this);
    for (QPushButton* b : {connectButton_, disconnectButton_, saveButton,
                           cancelButton}) {
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet(kBtnStyle);
    }
    connectButton_->setStyleSheet(QStringLiteral(
        "QPushButton{background:#C42B1C;color:#FFF;border:none;"
        "border-radius:4px;padding:8px 18px;}"
        "QPushButton:hover{background:#D0382A;}"));
    disconnectButton_->setEnabled(false);
    connect(connectButton_, &QPushButton::clicked,
            this, &RdpWindow::onConnect);
    connect(disconnectButton_, &QPushButton::clicked,
            this, &RdpWindow::onDisconnect);
    connect(saveButton, &QPushButton::clicked, this, &RdpWindow::onSave);
    connect(cancelButton, &QPushButton::clicked, this, &QWidget::close);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(connectButton_);
    btnRow->addWidget(disconnectButton_);
    btnRow->addStretch();
    btnRow->addWidget(saveButton);
    btnRow->addWidget(cancelButton);
    root->addLayout(btnRow);

    // 客户端退出 → 复位 UI（审查 L1：正常退出码 0 用中性色，非 0 才警示）。
    connect(&client_, &RdpClient::stopped, this, [this](int code) {
        if (code == 0) {
            setStatus(QStringLiteral("连接已结束"), true);
        } else {
            setStatus(QStringLiteral("连接已断开（退出码 %1）").arg(code), false);
        }
        updateUi();
    });
    connect(&client_, &RdpClient::error, this,
            [this](const QString& m) { setStatus(m, false); });

    setStyleSheet(QStringLiteral("QWidget{background:#2D2D2D;}"));
    // 加载上次保存的配置。
    const RdpConfig saved = loadConfig();
    if (!saved.host.isEmpty()) {
        hostEdit_->setText(saved.host);
        portSpin_->setValue(saved.port);
        userEdit_->setText(saved.username);
        passEdit_->setText(saved.password);
        // 审查 L4：自定义分辨率不在下拉列表时先添加再选中。
        const QString sizeText = saved.size.isEmpty()
            ? QStringLiteral("客户端默认") : saved.size;
        const int idx = sizeCombo_->findText(sizeText);
        if (idx >= 0) {
            sizeCombo_->setCurrentIndex(idx);
        } else {
            sizeCombo_->addItem(sizeText);
            sizeCombo_->setCurrentIndex(sizeCombo_->count() - 1);
        }
        fullscreenCheck_->setChecked(saved.fullscreen);
        savePassCheck_->setChecked(saved.savePassword);
        sizeCombo_->setEnabled(!saved.fullscreen);
    }
}

RdpConfig RdpWindow::currentConfig() const {
    RdpConfig c;
    c.host = hostEdit_->text().trimmed();
    c.port = portSpin_->value();
    c.username = userEdit_->text();
    c.password = passEdit_->text();
    const QString sizeText = sizeCombo_->currentText();
    c.size = sizeText == QStringLiteral("客户端默认") ? QString() : sizeText;
    c.fullscreen = fullscreenCheck_->isChecked();
    c.savePassword = savePassCheck_->isChecked();
    return c;
}

void RdpWindow::onConnect() {
    const RdpConfig c = currentConfig();
    if (!client_.start(c)) {
        setStatus(client_.lastError(), false);
        return;
    }
    setStatus(QStringLiteral("正在连接 %1…").arg(c.host), true);
    updateUi();
}

void RdpWindow::onSave() {
    RdpConfig c = currentConfig();
    const QString err = validateConfig(c);
    if (!err.isEmpty()) {
        setStatus(err, false);
        return;
    }
    const QString saveErr = saveConfig(c);
    if (!saveErr.isEmpty()) {
        setStatus(saveErr, false);
        return;
    }
    setStatus(QStringLiteral("配置已保存"), true);
}

void RdpWindow::onDisconnect() {
    client_.stop();
    setStatus(QStringLiteral("已断开"), true);
    updateUi();
}

// 审查 L7：连接中关闭窗口需确认（避免误杀进行中的远程会话）。
void RdpWindow::closeEvent(QCloseEvent* event) {
    if (client_.isRunning()) {
        const auto ret = QMessageBox::question(
            this, QStringLiteral("断开连接"),
            QStringLiteral("远程连接仍在进行，关闭窗口将断开连接。\n继续吗？"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        client_.stop();
    }
    event->accept();
}

void RdpWindow::setStatus(const QString& text, bool ok) {
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(ok
        ? QStringLiteral("color:#9AA0A6; font-size:12px;")
        : QStringLiteral("color:#E57373; font-size:12px;"));
}

void RdpWindow::updateUi() {
    const bool running = client_.isRunning();
    connectButton_->setEnabled(!running);
    disconnectButton_->setEnabled(running);
}

}  // namespace w10de::rdp
