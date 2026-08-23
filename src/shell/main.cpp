// w10shell —— Win10DE Shell 客户端入口（M4：桌面 + 任务栏 + 开始菜单）
//
// 通过 layer-shell 协议挂载：
//   桌面（background 层，全屏壁纸 + 图标）
//   任务栏（bottom 层）+ 开始菜单（overlay 层）
// 运行前提：Wayland 合成器（w10compositor）支持 wlr-layer-shell 协议。

#include <QApplication>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDir>
#include <QMargins>
#include <QWindow>

#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>

#include "desktop/desktopwindow.h"
#include "ipc/config.h"
#include "ipc/lockservice.h"
#include "startmenu/startmenu.h"
#include "taskbar/startbutton.h"
#include "taskbar/taskbarwindow.h"
#include "theme/colors.h"

namespace {

// 配置 layer-shell 窗口（layer-shell-qt 绑定）。
// 注：配置时序（show 前/后）在不同版本可能有差异，M3 实现时验证。
void configureLayerWindow(QWidget* widget, const QString& scope,
                          LayerShellQt::Window::Layer layer,
                          LayerShellQt::Window::Anchors anchors,
                          int exclusiveZone,
                          const QMargins& margins,
                          LayerShellQt::Window::KeyboardInteractivity keyboard =
                              LayerShellQt::Window::KeyboardInteractivityOnDemand) {
    widget->show();
    if (QWindow* win = widget->windowHandle()) {
        if (LayerShellQt::Window* layerWindow = LayerShellQt::Window::get(win)) {
            layerWindow->setScope(scope);
            layerWindow->setLayer(layer);
            layerWindow->setAnchors(anchors);
            layerWindow->setExclusiveZone(exclusiveZone);
            layerWindow->setMargins(margins);
            layerWindow->setKeyboardInteractivity(keyboard);
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("w10shell"));
    app.setApplicationDisplayName(QStringLiteral("Win10DE Shell"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Win10DE Shell"));
    parser.addHelpOption();
    QCommandLineOption wallpaperOption(QStringLiteral("wallpaper"),
                                       QStringLiteral("壁纸图片路径（缺省用内置渐变）"),
                                       QStringLiteral("path"));
    parser.addOption(wallpaperOption);
    parser.process(app);

    // 启用 layer-shell 支持（必须在使用任何 layer-shell 窗口前调用）。
    LayerShellQt::Shell::useLayerShell();

    // ---- 桌面（background 层，全屏；不接收键盘输入）----
    w10de::DesktopWindow desktop;
    configureLayerWindow(&desktop, QStringLiteral("w10de-desktop"),
                         LayerShellQt::Window::LayerBackground,
                         LayerShellQt::Window::Anchors(
                             LayerShellQt::Window::AnchorLeft |
                                 LayerShellQt::Window::AnchorRight |
                                 LayerShellQt::Window::AnchorTop |
                                 LayerShellQt::Window::AnchorBottom),
                         0, QMargins(),
                         LayerShellQt::Window::KeyboardInteractivityNone);
    // 壁纸：--wallpaper 优先，否则读配置 ~/.config/w10de/config.ini 的
    // [wallpaper] path；都没有则用内置渐变。
    QString wallpaper = parser.value(wallpaperOption);
    if (wallpaper.isEmpty()) {
        const w10de::Config config = w10de::Config::load(
            (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
                .toStdString());
        wallpaper = QString::fromStdString(config.get("wallpaper", "path"));
    }
    desktop.setWallpaper(wallpaper);

    // ---- 任务栏（bottom 层，底部全宽 + 独占区）----
    w10de::TaskbarWindow taskbar;
    configureLayerWindow(&taskbar, QStringLiteral("w10de-taskbar"),
                         LayerShellQt::Window::LayerBottom,
                         LayerShellQt::Window::Anchors(
                             LayerShellQt::Window::AnchorLeft |
                                 LayerShellQt::Window::AnchorRight |
                                 LayerShellQt::Window::AnchorBottom),
                         w10de::theme::kTaskbarHeight,
                         QMargins());

    // ---- 开始菜单（overlay 层，左下锚定，任务栏之上）----
    w10de::StartMenu startMenu;
    // 初始隐藏：先 show 获取 windowHandle 并配置，再隐藏。
    // 注：layer-shell-qt 的 hide/show 行为实现时验证。
    configureLayerWindow(&startMenu, QStringLiteral("w10de-startmenu"),
                         LayerShellQt::Window::LayerOverlay,
                         LayerShellQt::Window::Anchors(
                             LayerShellQt::Window::AnchorLeft |
                                 LayerShellQt::Window::AnchorBottom),
                         0,  // 弹出式，不占独占区
                         QMargins(0, 0, 0, w10de::theme::kTaskbarHeight));
    startMenu.hide();

    // 开始按钮切换开始菜单。
    QObject::connect(taskbar.startButton(), &w10de::StartButton::startMenuRequested,
                     &startMenu, &w10de::StartMenu::toggle);

    // ---- D-Bus 会话服务（org.w10de.Shell：Lock 等）----
    // 外部触发锁屏：dbus-send --session --dest=org.w10de.Shell /Shell org.w10de.Shell.Lock
    // LockService 须在 app.exec() 期间存活（栈上声明于本作用域）。
    w10de::LockService lockService;
    if (QDBusConnection::sessionBus().registerService(
            QStringLiteral("org.w10de.Shell"))) {
        QDBusConnection::sessionBus().registerObject(
            QStringLiteral("/Shell"), &lockService,
            QDBusConnection::ExportAllSlots);
    } else {
        qWarning("w10shell: failed to register D-Bus service org.w10de.Shell");
    }

    return app.exec();
}
