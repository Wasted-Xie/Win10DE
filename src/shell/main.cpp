// w10shell —— Win10DE Shell 客户端入口（M4：桌面 + 任务栏 + 开始菜单）
//
// 通过 layer-shell 协议挂载：
//   桌面（background 层，全屏壁纸 + 图标）
//   任务栏（bottom 层）+ 开始菜单（overlay 层）
// 运行前提：Wayland 合成器（w10compositor）支持 wlr-layer-shell 协议。

#include <QApplication>
#include <QClipboard>
#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDir>
#include <QMargins>
#include <QShortcut>
#include <QTimer>
#include <QVariant>
#include <QWindow>

#include <cstdio>  // setvbuf（日志无缓冲）

#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>

#include "desktop/desktopwindow.h"
#include "ipc/clipboardservice.h"
#include "ipc/config.h"
#include "ipc/lockservice.h"
#include "ipc/notificationservice.h"
#include "clipboard/clipboardhistory.h"
#include "clipboard/clipboardpanel.h"
#include "notification/notificationcenter.h"
#include "notification/notificationpopup.h"
#include "startmenu/startmenu.h"
#include "taskbar/startbutton.h"
#include "taskbar/taskbarwindow.h"
#include "taskbar/monthcalendar.h"  // G6：月历（--calendar-selftest/--calendar-render）
#include "theme/colors.h"

namespace {

// 配置 layer-shell 窗口（layer-shell-qt 绑定）。
// Qt 6.11 验证：shell integration 必须在窗口 surface 创建（show）前设置，
// show 后再 Window::get 会失败（"already has a shell integration"，真实
// 运行验证）。正确时序：winId() 创建 QWindow（不创建 surface）→ get 绑定
// layer-shell 并配置 → 最后 show()。
void configureLayerWindow(QWidget* widget, const QString& scope,
                          LayerShellQt::Window::Layer layer,
                          LayerShellQt::Window::Anchors anchors,
                          int exclusiveZone,
                          const QMargins& margins,
                          LayerShellQt::Window::KeyboardInteractivity keyboard =
                              LayerShellQt::Window::KeyboardInteractivityOnDemand) {
    widget->winId();
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
    widget->show();
}

}  // namespace

int main(int argc, char* argv[]) {
    // 日志即时可见（重定向到文件时 stderr 全缓冲，kill 会丢日志——壁纸
    // 幻灯片调试实测：advance 后 paint 日志丢失导致误判未重绘）。
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    // G6：--calendar-selftest 需 offscreen（QApplication 构造前设置）。
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--calendar-selftest") == 0) {
            qputenv("QT_QPA_PLATFORM", "offscreen");
            break;
        }
    }
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
    // --config：与 compositor 同款参数，保证自定义路径时两进程读取同一
    // 配置（主题/壁纸不因路径分叉）——审查 t2/t3。缺省 ~/.config/w10de/config.ini。
    QCommandLineOption configOption(QStringLiteral("config"),
                                    QStringLiteral("配置文件路径（缺省 ~/.config/w10de/config.ini）"),
                                    QStringLiteral("path"));
    parser.addOption(configOption);
    // 测试辅助：
    // --clipboard-seed：启动时预置 2 条剪贴板历史种子（headless 渲染验证
    //   面板"有条目"状态；真实复制内容来自系统剪贴板）。
    QCommandLineOption clipboardSeedOption(QStringLiteral("clipboard-seed"),
                                           QStringLiteral("测试：预置剪贴板历史种子"));
    parser.addOption(clipboardSeedOption);
    // --clipboard-selftest：剪贴板历史逻辑自测（去重/上限/类型），
    // 通过退出码 0，失败 1（headless 验证用）。
    QCommandLineOption clipboardSelftestOption(QStringLiteral("clipboard-selftest"),
                                               QStringLiteral("测试：剪贴板历史逻辑自测"));
    parser.addOption(clipboardSelftestOption);
    // G6：月历逻辑自测（offscreen）与独立渲染（--calendar-render 显示
    // MonthCalendar 普通窗口，供 headless 截图校验）。
    QCommandLineOption calendarSelftestOption(
        QStringLiteral("calendar-selftest"),
        QStringLiteral("测试：月历网格逻辑自测"));
    parser.addOption(calendarSelftestOption);
    QCommandLineOption calendarRenderOption(
        QStringLiteral("calendar-render"),
        QStringLiteral("测试：独立显示月历窗口（渲染验证）"));
    parser.addOption(calendarRenderOption);
    parser.process(app);

    // 启用 layer-shell 支持（必须在使用任何 layer-shell 窗口前调用）。
    LayerShellQt::Shell::useLayerShell();

    // ---- 主题（[theme] 段：mode=dark/light 预设 + 颜色键覆盖）----
    // 与 compositor 读取同一配置，视觉一致；未配置时为深色预设。
    QString configPath = parser.value(configOption);
    if (configPath.isEmpty()) {
        configPath = QDir::homePath() + QStringLiteral("/.config/w10de/config.ini");
    }
    w10de::theme::loadFromConfig(configPath.toStdString());

    // ---- 剪贴板历史逻辑自测（headless 验证：--clipboard-selftest）----
    if (parser.isSet(clipboardSelftestOption)) {
        bool ok = true;
        auto check = [&ok](bool cond, const char* what) {
            if (!cond) {
                ok = false;
                qWarning("clipboard selftest FAIL: %s", what);
            }
        };
        w10de::ClipboardHistory h;
        check(h.isEmpty(), "initial empty");
        h.addText(QStringLiteral("alpha"));
        check(h.count() == 1, "one text entry");
        h.addText(QStringLiteral("alpha"));
        check(h.count() == 1, "dedupe identical text");
        h.addText(QStringLiteral("beta"));
        check(h.count() == 2, "second distinct text");
        check(!h.entries().first().isImage, "text entry flagged text");
        h.addImage(QImage(8, 8, QImage::Format_ARGB32));
        check(h.count() == 3, "image entry appended");
        check(h.entries().first().isImage, "newest is image");
        // QVariant 往返（面板 QListWidgetItem 数据通路；缺 metatype 时
        // fromValue 产生无效 QVariant，canConvert 恒 false——S1 回归防护）。
        const QVariant roundTrip = QVariant::fromValue(h.entries().first());
        check(roundTrip.canConvert<w10de::ClipboardEntry>(), "variant round-trip");
        check(roundTrip.value<w10de::ClipboardEntry>().isImage, "variant value intact");
        // 写回非最近条目：全表去重应移到顶部而不产生重复（M1）。
        h.addText(QStringLiteral("alpha"));
        check(h.count() == 3, "re-add older entry deduped");
        check(h.entries().first().text == QStringLiteral("alpha"), "older entry moved to top");
        for (int i = 0; i < 30; ++i) {
            h.addText(QStringLiteral("bulk-%1").arg(i));
        }
        check(h.count() == h.maxEntries(), "capped at max entries");
        check(!h.entries().first().text.isEmpty(), "newest survives trim");
        // 30 次追加后保留 bulk-29..bulk-10（20 条）；起始 3 条（img/beta/alpha）
        // 在第 18/19/20 次追加时被逐出，最旧剩余为 bulk-10。
        check(h.entries().last().text == QStringLiteral("bulk-10"),
              "oldest beyond cap evicted");
        qInfo("clipboard selftest: %s", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }

    // ---- G6：月历逻辑自测 / 独立渲染（headless 验证）----
    if (parser.isSet(calendarSelftestOption)) {
        bool ok = true;
        auto check = [&ok](bool cond, const char* what) {
            if (!cond) {
                ok = false;
                qWarning("calendar selftest FAIL: %s", what);
            }
        };
        // 月天数（含闰年）。
        check(w10de::daysInMonth(2026, 2) == 28, "2026-02 = 28");
        check(w10de::daysInMonth(2024, 2) == 29, "2024-02 leap = 29");
        check(w10de::daysInMonth(2000, 2) == 29, "2000-02 %400 leap = 29");
        check(w10de::daysInMonth(2026, 8) == 31, "2026-08 = 31");
        check(w10de::daysInMonth(2026, 9) == 30, "2026-09 = 30");
        check(w10de::daysInMonth(2026, 13) == 0, "invalid month -> 0");
        // 2026-06-01 恰为周一 → offset=0，首格即当月 1 日。
        const QList<QDate> june = w10de::calendarCells(2026, 6);
        check(june.first() == QDate(2026, 6, 1), "2026-06 first cell = 6/1 (offset 0)");
        check(w10de::calendarCells(2026, 13).isEmpty(), "invalid month -> empty");
        // 网格：2026-08-01 是周六（2026-01-01 周四 + 212 天）→ 周一始偏移
        // 5 → 第一格 7/27、第 5 格 8/1、第 36 格 9/1、末格 9/6。
        const QList<QDate> cells = w10de::calendarCells(2026, 8);
        check(cells.size() == 42, "42 cells");
        check(cells.first() == QDate(2026, 7, 27), "first cell = 7/27");
        check(cells.at(5) == QDate(2026, 8, 1), "8/1 at index 5");
        check(cells.at(36) == QDate(2026, 9, 1), "9/1 at index 36");
        check(cells.last() == QDate(2026, 9, 6), "last cell = 9/6");
        // 连续 42 天。
        bool contiguous = true;
        for (int i = 1; i < cells.size(); ++i) {
            if (cells.at(i) != cells.at(i - 1).addDays(1)) {
                contiguous = false;
                break;
            }
        }
        check(contiguous, "cells contiguous");
        // 每周一起始：第一列全部是周一。
        bool firstColMonday = true;
        for (int row = 0; row < 6; ++row) {
            if (cells.at(row * 7).dayOfWeek() != Qt::Monday) {
                firstColMonday = false;
                break;
            }
        }
        check(firstColMonday, "first column is Monday");
        qInfo("calendar selftest: %s", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
    if (parser.isSet(calendarRenderOption)) {
        // 独立显示月历（普通窗口，供 headless 截图校验；非 layer-shell）。
        w10de::MonthCalendar calendar;
        calendar.setDate(QDate::currentDate());
        calendar.show();
        return QApplication::exec();
    }

    // ---- 桌面（background 层，全屏；不接收键盘输入）----
    // configPath 传入：桌面小部件（[widgets] 段）读取同一配置（中优先 #7）。
    w10de::DesktopWindow desktop(configPath);
    configureLayerWindow(&desktop, QStringLiteral("w10de-desktop"),
                         LayerShellQt::Window::LayerBackground,
                         LayerShellQt::Window::Anchors(
                             LayerShellQt::Window::AnchorLeft |
                                 LayerShellQt::Window::AnchorRight |
                                 LayerShellQt::Window::AnchorTop |
                                 LayerShellQt::Window::AnchorBottom),
                         0, QMargins(),
                         LayerShellQt::Window::KeyboardInteractivityNone);
    // 壁纸优先级（审查 S1：--wallpaper / slideshow_dir / path 独立判断，
    // 不可合并短路——否则 path 存在时幻灯片永不启动）：
    //   1) --wallpaper CLI 显式指定
    //   2) [wallpaper] slideshow_dir（幻灯片，按 slideshow_interval 秒轮换，默认 60）
    //   3) [wallpaper] path 单张
    //   4) 都没有 → 内置渐变
    const w10de::Config config = w10de::Config::load(configPath.toStdString());
    const QString cliWallpaper = parser.value(wallpaperOption);
    const QString cfgPath = QString::fromStdString(config.get("wallpaper", "path"));
    const QString slideshowDir =
        QString::fromStdString(config.get("wallpaper", "slideshow_dir"));
    const int slideshowInterval =
        config.getInt("wallpaper", "slideshow_interval", 60);
    if (!cliWallpaper.isEmpty()) {
        desktop.setWallpaper(cliWallpaper);
    } else if (!slideshowDir.isEmpty()) {
        desktop.setSlideshow(slideshowDir, slideshowInterval);
    } else if (!cfgPath.isEmpty()) {
        desktop.setWallpaper(cfgPath);
    } else {
        desktop.setWallpaper(QString());
    }

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
                         QMargins());
    // 底边距必须为 0：overlay 层 surface 的 bounds 是可用区（已排除任务栏
    // 独占的 kTaskbarHeight），再设底边距会双重避让、菜单与任务栏间留出
    // 空隙（渲染验证实测：菜单底比任务栏顶高 49px）。
    startMenu.hide();

    // 开始按钮切换开始菜单。
    QObject::connect(taskbar.startButton(), &w10de::StartButton::startMenuRequested,
                     &startMenu, &w10de::StartMenu::toggle);

    // ---- 通知（org.freedesktop.Notifications 标准服务 + 弹窗 + 历史）----
    w10de::NotificationService notificationService;
    w10de::NotificationPopup popup;
    w10de::NotificationCenter center;
    // 弹窗：右下角 overlay；历史：右下角 overlay（比弹窗稍靠上，宽 380）。
    configureLayerWindow(&popup, QStringLiteral("w10de-notification"),
                         LayerShellQt::Window::LayerOverlay,
                         LayerShellQt::Window::Anchors(
                             LayerShellQt::Window::AnchorRight |
                                 LayerShellQt::Window::AnchorBottom),
                         0, QMargins(0, 0, 8, w10de::theme::kTaskbarHeight + 8),
                         LayerShellQt::Window::KeyboardInteractivityNone);
    configureLayerWindow(&center, QStringLiteral("w10de-notification-center"),
                         LayerShellQt::Window::LayerOverlay,
                         LayerShellQt::Window::Anchors(
                             LayerShellQt::Window::AnchorRight |
                                 LayerShellQt::Window::AnchorBottom),
                         0, QMargins(0, 0, 8, w10de::theme::kTaskbarHeight + 8),
                         LayerShellQt::Window::KeyboardInteractivityOnDemand);
    popup.hide();
    center.hide();
    // 新通知 → 弹窗显示 + 定时隐藏；点击弹窗 → 打开历史中心。
    QObject::connect(&notificationService,
                     &w10de::NotificationService::notificationReceived,
                     &popup, &w10de::NotificationPopup::showNotification);
    QObject::connect(&popup, &w10de::NotificationPopup::clicked, [&]() {
        popup.hide();
        center.refresh(notificationService.history());
        center.show();
        center.raise();
    });
    // 弹窗 5 秒后自动消失（Win10 语义）。
    auto* popupTimer = new QTimer(&popup);
    popupTimer->setSingleShot(true);
    popupTimer->setInterval(5000);
    QObject::connect(&notificationService,
                     &w10de::NotificationService::notificationReceived,
                     popupTimer, qOverload<>(&QTimer::start));
    QObject::connect(popupTimer, &QTimer::timeout, &popup, &QWidget::hide);
    // 点击历史外部（Esc）关闭中心：给中心设关闭快捷键。
    auto* closeCenter = new QShortcut(QKeySequence(QStringLiteral("Escape")), &center);
    QObject::connect(closeCenter, &QShortcut::activated, &center, &QWidget::hide);

    // ---- 剪贴板历史（Win10 Win+V：监听系统剪贴板 + 历史面板）----
    w10de::ClipboardHistory clipboardHistory;
    w10de::ClipboardPanel clipboardPanel;
    configureLayerWindow(&clipboardPanel, QStringLiteral("w10de-clipboard"),
                         LayerShellQt::Window::LayerOverlay,
                         LayerShellQt::Window::Anchors(
                             LayerShellQt::Window::AnchorRight |
                                 LayerShellQt::Window::AnchorBottom),
                         0, QMargins(0, 0, 8, w10de::theme::kTaskbarHeight + 8),
                         LayerShellQt::Window::KeyboardInteractivityOnDemand);
    clipboardPanel.hide();
    // 测试种子：--clipboard-seed 预置条目（验证面板"有条目"渲染）。
    if (parser.isSet(clipboardSeedOption)) {
        clipboardHistory.addText(QStringLiteral("seeded clipboard text A"));
        clipboardHistory.addText(QStringLiteral("seeded clipboard text B"));
    }
    // Win+V（compositor → D-Bus）→ 切换面板；显示时刷新为最新历史。
    w10de::ClipboardService clipboardService;
    QObject::connect(&clipboardService, &w10de::ClipboardService::toggleRequested,
                     [&]() {
        if (clipboardPanel.isVisible()) {
            clipboardPanel.hide();
        } else {
            clipboardPanel.setHistory(clipboardHistory.entries());
            clipboardPanel.showPanel();
        }
    });
    // 选中条目 → 写回系统剪贴板（Win10 语义：成为当前剪贴板，供 Ctrl+V
    // 粘贴；写回触发 dataChanged 由历史去重吸收，不产生新条目）。
    QObject::connect(&clipboardPanel, &w10de::ClipboardPanel::entryPicked,
                     [&](const w10de::ClipboardEntry& e) {
        if (e.isImage) {
            QApplication::clipboard()->setImage(e.image);
        } else {
            QApplication::clipboard()->setText(e.text);
        }
    });
    // 面板显示期间历史变化 → 实时刷新。
    QObject::connect(&clipboardHistory, &w10de::ClipboardHistory::historyChanged,
                     [&]() {
        if (clipboardPanel.isVisible()) {
            clipboardPanel.setHistory(clipboardHistory.entries());
        }
    });

    // ---- D-Bus 会话服务（org.w10de.Shell：Lock / Clipboard 等）----
    // 外部触发锁屏：dbus-send --session --dest=org.w10de.Shell /Shell org.w10de.Shell.Lock
    // 外部触发剪贴板面板：dbus-send --session --dest=org.w10de.Shell /Clipboard org.w10de.Clipboard.ToggleClipboardHistory
    // LockService/ClipboardService 须在 app.exec() 期间存活（栈上声明于本作用域）。
    w10de::LockService lockService;
    if (QDBusConnection::sessionBus().registerService(
            QStringLiteral("org.w10de.Shell"))) {
        QDBusConnection::sessionBus().registerObject(
            QStringLiteral("/Shell"), &lockService,
            QDBusConnection::ExportAllSlots);
        QDBusConnection::sessionBus().registerObject(
            QStringLiteral("/Clipboard"), &clipboardService,
            QDBusConnection::ExportAllSlots);
    } else {
        qWarning("w10shell: failed to register D-Bus service org.w10de.Shell");
    }

    return app.exec();
}
