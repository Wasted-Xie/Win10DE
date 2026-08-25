// D-Bus 会话服务：org.w10de.Clipboard —— 剪贴板历史面板开关。
//
// compositor 收到 Win+V（seat 快捷键）后经 dbus-send 调用本服务：
//   dbus-send --session --dest=org.w10de.Shell /Clipboard org.w10de.Clipboard.ToggleClipboardHistory
// 方法与 LockService 同款模式（Q_CLASSINFO 显式接口名 + ExportAllSlots）。
#pragma once

#include <QObject>

namespace w10de {

class ClipboardService : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.w10de.Clipboard")
public:
    explicit ClipboardService(QObject* parent = nullptr);

signals:
    // D-Bus 槽触发 → 切换面板显示/隐藏（main 接线）。
    void toggleRequested();

public slots:
    // D-Bus 导出：Win+V 切换剪贴板历史面板。
    void ToggleClipboardHistory();
};

}  // namespace w10de
