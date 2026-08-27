// RdpWindow —— 远程桌面连接窗口（可选拓展 E9，Win10 远程桌面连接风格）。
//
// 表单：主机/端口/用户名/密码/分辨率/全屏；底部 [连接] [保存] [取消]；
// 连接启动外部客户端（wlfreerdp3/xfreerdp3）；状态区显示运行/错误。

#pragma once

#include <QWidget>

#include "systemapps/rdp/rdpclient.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace w10de::rdp {

class RdpWindow : public QWidget {
    Q_OBJECT
public:
    explicit RdpWindow(QWidget* parent = nullptr);

    // 供验证：当前表单配置。
    RdpConfig currentConfig() const;
    // 供验证：RDP 客户端是否可用。
    bool clientAvailable() const { return !findClient().isEmpty(); }

protected:
    // 审查 L7：连接中关闭询问（防误杀会话）。
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onConnect();
    void onSave();
    void onDisconnect();

private:
    void setStatus(const QString& text, bool ok);
    void updateUi();

    RdpClient client_;
    QLineEdit* hostEdit_ = nullptr;
    QSpinBox* portSpin_ = nullptr;
    QLineEdit* userEdit_ = nullptr;
    QLineEdit* passEdit_ = nullptr;
    QComboBox* sizeCombo_ = nullptr;
    QCheckBox* fullscreenCheck_ = nullptr;
    QCheckBox* savePassCheck_ = nullptr;
    QPushButton* connectButton_ = nullptr;
    QPushButton* disconnectButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
};

}  // namespace w10de::rdp
