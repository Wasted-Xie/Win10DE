// w10screenshot 交互模式（G2：Win10 截图工具风格遮罩 + 拖选/窗口/延时）。
//
// 无边框半透明遮罩窗口 + 顶部工具条（全屏/区域/窗口/延时 5 秒/取消）。
// 区域 = 拖选矩形（自绘虚线 + 尺寸）；窗口 = compositor GetViews 列表选择；
// 延时 = 倒计时后自动捕获。选区经 mapToGlobal 换算为输出坐标（单输出 +
// scale=100；多输出/缩放场景取首个输出，已知简化记录 WIN10-GAP）。
// 捕获走 w10shot::captureOutput（同步阻塞 ~1s）。
#pragma once

#include <QDBusArgument>
#include <QString>
#include <QWidget>

#include <vector>

class QLabel;
class QPushButton;
class QTimer;

namespace w10shot {

// GetViews 返回 a(ssiiii)：(app_id, title, x, y, w, h)。CLI（--window）
// 与交互模式（窗口选择）共用（qdbus_cast 解析）。
struct ViewInfo {
    QString appId;
    QString title;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};
QDBusArgument& operator<<(QDBusArgument& arg, const ViewInfo& v);
const QDBusArgument& operator>>(const QDBusArgument& arg, ViewInfo& v);

class ScreenshotWindow : public QWidget {
    Q_OBJECT
public:
    explicit ScreenshotWindow(QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    enum class Mode { Fullscreen, Region, Window, Countdown };

    void setMode(Mode mode);
    // 按当前模式/选区捕获并保存；成功返回 true。
    bool captureAndSave();
    // 生成保存路径（~/Pictures/Screenshots/w10shot-时间戳.png）。
    QString defaultPath() const;
    // 倒计时显示（延时模式）。
    void startCountdown(int seconds);
    void onCountdownTick();

    Mode mode_ = Mode::Fullscreen;
    QPushButton* fullscreenBtn_ = nullptr;
    QPushButton* regionBtn_ = nullptr;
    QPushButton* windowBtn_ = nullptr;
    QPushButton* delayBtn_ = nullptr;
    QPushButton* cancelBtn_ = nullptr;
    QLabel* hintLabel_ = nullptr;

    bool dragging_ = false;
    QPoint dragStart_;
    QRect selection_;

    int countdownSeconds_ = 0;
    QTimer* countdownTimer_ = nullptr;
};

}  // namespace w10shot
