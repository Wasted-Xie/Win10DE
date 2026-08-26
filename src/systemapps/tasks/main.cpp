// w10tasks —— 任务计划程序（WIN10-GAP G5：cron 等价 GUI + 调度守护）。
//
// 双模式：
//   w10tasks                GUI（任务列表：新建/编辑/删除/启用禁用/立即运行）
//   w10tasks --daemon       调度守护（无 GUI：每分钟检查任务配置，匹配则
//                           执行命令；D-Bus org.w10de.Tasks /Tasks Reload
//                           通知重载）。会话启动（w10-session）拉起。
//   w10tasks --selftest     headless 单测（配置读写/调度匹配表驱动）。
//
// 调度字段（cron 风格）：minute/hour/day_of_month/month/day_of_week，
// -1 = 通配/忽略；day_of_month 与 day_of_week 同时指定时任一匹配即触发。

#include <QApplication>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QDir>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>

#include <cstdio>  // setvbuf（日志无缓冲）

#include <set>  // ranKeys_（同分钟去重）

#include "systemapps/appipc.h"
#include "systemapps/tasks/taskstore.h"
#include "systemapps/tasks/taskwindow.h"
#include "ipc/config.h"
#include "ipc/theme.h"
#include "theme/colors.h"

namespace {

// ---- 调度守护（--daemon）----

class TaskDaemon : public QObject {
    Q_OBJECT
public:
    explicit TaskDaemon(QObject* parent = nullptr) : QObject(parent) {
        timer_ = new QTimer(this);
        connect(timer_, &QTimer::timeout, this, &TaskDaemon::tick);
        timer_->start(60 * 1000);  // 每分钟
    }

    // D-Bus Reload：重读配置。审查 L2：立即单发一次 tick（否则语义空洞——
    // 配置已由 tick 每轮惰性重读，Reload 仅加速生效）。
    // 方法名与 slot 名一致（Qt D-Bus ExportAllSlots 仅导出 slots——public
    // 区普通方法不导出，实测 Introspect 无此方法）。
public slots:
    void Reload() {
        std::printf("taskd: reload requested\n");
        tick();
    }

private slots:
    void tick() {
        const QDateTime now = QDateTime::currentDateTime();
        const QString minuteKey = now.toString(QStringLiteral("yyyyMMddHHmm"));
        const QList<w10task::Task> tasks = w10task::loadTasks();
        bool changed = false;
        QList<w10task::Task> mutableTasks = tasks;
        for (w10task::Task& t : mutableTasks) {
            if (!t.enabled || !w10task::taskMatches(t, now)) {
                continue;
            }
            // 同一分钟不重复执行（守护重启/配置重载后已跑过的分钟不补跑）。
            const QString key = minuteKey + QStringLiteral(":") + QString::number(t.id);
            if (ranKeys_.contains(key)) {
                continue;
            }
            ranKeys_.insert(key);
            // 上限防无限增长（保留最近 1024 个键）。
            while (ranKeys_.size() > 1024) {
                ranKeys_.erase(ranKeys_.begin());
            }
            // 经 shell 执行（命令可含参数/管道；QProcess::startDetached
            // 单参版本把整串当程序名，含空格必失败——实测）。
            const bool ok = QProcess::startDetached(
                QStringLiteral("/bin/sh"),
                {QStringLiteral("-c"), t.command});
            t.lastRun = now.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            t.lastResult = ok ? QStringLiteral("OK")
                              : QStringLiteral("启动失败");
            std::printf("taskd: run task %d '%s' -> %s\n", t.id,
                        t.name.toUtf8().constData(),
                        ok ? "OK" : "FAIL");
            changed = true;
        }
        if (changed) {
            // 审查 L6：写回失败记日志（磁盘满/权限）。
            if (!w10task::saveTasks(mutableTasks)) {
                std::fprintf(stderr, "taskd: failed to save tasks state\n");
            }
        }
    }

private:
    QTimer* timer_ = nullptr;
    // 已执行标记（分钟键 + 任务 id；防同分钟重复）。
    std::set<QString> ranKeys_;
};

// ---- selftest（headless）----

int runSelfTest() {
    // 审查 M5：任务配置经环境变量隔离到临时目录（不碰用户真实配置）。
    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        std::fprintf(stderr, "SELFTEST FAIL: 临时目录创建失败\n");
        return 1;
    }
    qputenv("W10DE_TASKS_CONFIG",
            (tmpDir.path() + QStringLiteral("/tasks.ini")).toUtf8());
    QTextStream out(stdout);
    auto fail = [&out](const QString& m) {
        out << "SELFTEST FAIL: " << m << "\n";
        return 1;
    };
    using w10task::Task;
    using w10task::taskMatches;
    using w10task::triggerText;

    // 1) 调度匹配表驱动（cron 语义）。
    {
        Task daily;   // 每天 10:30
        daily.minute = 30;
        daily.hour = 10;
        const QDateTime at1(QDate(2026, 8, 5), QTime(10, 30));
        const QDateTime at2(QDate(2026, 8, 5), QTime(10, 31));
        const QDateTime at3(QDate(2026, 8, 6), QTime(10, 30));
        if (!taskMatches(daily, at1) || taskMatches(daily, at2)
                || !taskMatches(daily, at3)) {
            return fail(QStringLiteral("每天 10:30 匹配错误"));
        }
        // 每分钟：全 -1。
        Task everyMinute;
        if (!taskMatches(everyMinute, at1)
                || !taskMatches(everyMinute, at2)) {
            return fail(QStringLiteral("每分钟匹配错误"));
        }
        // 每周一 08:00（Qt dayOfWeek: Mon=1; 0=Sun 映射）。
        Task weekly;
        weekly.minute = 0;
        weekly.hour = 8;
        weekly.dayOfWeek = 1;  // 周一
        const QDateTime mon(QDate(2026, 8, 3), QTime(8, 0));   // 2026-08-03 是周一
        const QDateTime tue(QDate(2026, 8, 4), QTime(8, 0));
        if (!taskMatches(weekly, mon) || taskMatches(weekly, tue)) {
            return fail(QStringLiteral("每周一 08:00 匹配错误"));
        }
        // 每月 1 日 00:00。
        Task monthly;
        monthly.minute = 0;
        monthly.hour = 0;
        monthly.dayOfMonth = 1;
        if (!taskMatches(monthly, QDateTime(QDate(2026, 9, 1), QTime(0, 0)))
                || taskMatches(monthly, QDateTime(QDate(2026, 9, 2), QTime(0, 0)))) {
            return fail(QStringLiteral("每月 1 日匹配错误"));
        }
        // day_of_month 与 day_of_week 同时指定：任一匹配（cron 语义）。
        Task orRule;
        orRule.minute = 0;
        orRule.hour = 0;
        orRule.dayOfMonth = 1;
        orRule.dayOfWeek = 0;  // 周日
        // 2026-08-02 是周日（非 1 日）→ 应匹配（dow 命中）。
        if (!taskMatches(orRule, QDateTime(QDate(2026, 8, 2), QTime(0, 0)))) {
            return fail(QStringLiteral("dow 与 dom 任一匹配错误"));
        }
        // 2026-08-04 是周二且非 1 日 → 不匹配。
        if (taskMatches(orRule, QDateTime(QDate(2026, 8, 4), QTime(0, 0)))) {
            return fail(QStringLiteral("dow/dom 都不命中却匹配"));
        }
        out << "OK task-matching\n";
    }

    // 2) 配置读写（临时目录隔离——审查 M5，不再动真实 HOME 配置）。
    {
        QList<Task> tasks;
        Task t;
        t.id = 1;
        t.name = QStringLiteral("测试任务");
        t.command = QStringLiteral("/bin/true");
        t.minute = 15;
        t.hour = 3;
        t.enabled = true;
        tasks.append(t);
        const bool saved = w10task::saveTasks(tasks);
        const QList<Task> loaded = saved ? w10task::loadTasks() : QList<Task>();
        if (!saved) {
            return fail(QStringLiteral("saveTasks 失败"));
        }
        if (loaded.size() != 1 || loaded.first().name != QStringLiteral("测试任务")
                || loaded.first().minute != 15 || loaded.first().hour != 3) {
            return fail(QStringLiteral("任务读回不一致"));
        }
        if (triggerText(loaded.first()) != QStringLiteral("每天 03:15")) {
            return fail(QStringLiteral("触发器文本不符：%1")
                            .arg(triggerText(loaded.first())));
        }
        // 审查 L4：特殊字符回读一致性（name/command 含 = [ 换行）。
        {
            QList<Task> special = tasks;
            special[0].name = QStringLiteral("带=号[任务");
            special[0].command = QStringLiteral("echo a=b && echo c\nd");
            w10task::saveTasks(special);
            const QList<Task> back = w10task::loadTasks();
            if (back.size() != 1
                    || back.first().name != QStringLiteral("带=号[任务")
                    || back.first().command.contains(QLatin1Char('\n'))) {
                return fail(QStringLiteral("特殊字符回读不一致（换行未转义）"));
            }
        }
        out << "OK task-config (" << triggerText(loaded.first()) << ")\n";
    }

    // 2b) 边界组合（审查 L4）：hour=-1 且 minute>=0（每小时 X 分）匹配与
    //     triggerText；hour>=0 且 minute=-1（每小时 X 点内每分钟）。
    {
        Task hourly;
        hourly.minute = 30;
        if (!w10task::taskMatches(hourly, QDateTime(QDate(2026, 8, 5), QTime(7, 30)))
                || w10task::taskMatches(hourly,
                                        QDateTime(QDate(2026, 8, 5), QTime(7, 31)))) {
            return fail(QStringLiteral("每小时 30 分匹配错误"));
        }
        if (triggerText(hourly) != QStringLiteral("每小时 30 分")) {
            return fail(QStringLiteral("每小时文本不符：%1")
                            .arg(triggerText(hourly)));
        }
        Task oddHour;
        oddHour.hour = 7;
        if (!triggerText(oddHour).contains(QStringLiteral("每小时 7 点"))) {
            return fail(QStringLiteral("hour-only 文本不符：%1")
                            .arg(triggerText(oddHour)));
        }
        out << "OK edge-combos\n";
    }

    // 3) 触发器文本。
    {
        Task weekly;
        weekly.minute = 0;
        weekly.hour = 8;
        weekly.dayOfWeek = 1;
        if (triggerText(weekly) != QStringLiteral("周一 08:00")) {
            return fail(QStringLiteral("周触发器文本不符：%1")
                            .arg(triggerText(weekly)));
        }
    }
    out << "OK trigger-text\n";
    out << "SELFTEST PASS\n";
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    bool daemon = false;
    bool selftest = false;
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--daemon") == 0) {
            daemon = true;
        } else if (qstrcmp(argv[i], "--selftest") == 0) {
            selftest = true;
            qputenv("QT_QPA_PLATFORM", "offscreen");
        } else if (qstrcmp(argv[i], "--help") == 0) {
            std::printf(
                "Usage: w10tasks [--daemon] [--selftest]\n"
                "  (no args)   GUI（任务计划管理）\n"
                "  --daemon    调度守护（每分钟执行；会话启动拉起）\n"
                "  --selftest  headless 单测\n");
            return 0;
        } else {
            // 审查 L7：未知参数告警（拼错 --dameon 不再静默当 GUI 启动）。
            std::fprintf(stderr, "w10tasks: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (selftest) {
        QCoreApplication app(argc, argv);
        return runSelfTest();
    }
    if (daemon) {
        // 守护：QCoreApplication（无 GUI）+ 每分钟 tick + D-Bus Reload。
        QCoreApplication app(argc, argv);
        QApplication::setApplicationName(QStringLiteral("w10tasks"));
        TaskDaemon daemonObj;
        // D-Bus 服务：org.w10de.Tasks /Tasks Reload()。
        // 审查 L1：先注册服务并检查返回，再注册对象（registerObject 带
        // 接口名重载——否则接口名是自动生成的类名）。
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.registerService(QStringLiteral("org.w10de.Tasks"))) {
            std::fprintf(stderr,
                         "taskd: failed to register org.w10de.Tasks"
                         "（已有实例？）\n");
            return 1;
        }
        bus.registerObject(QStringLiteral("/Tasks"),
                           QStringLiteral("org.w10de.Tasks"), &daemonObj,
                           QDBusConnection::ExportAllSlots);
        std::printf("taskd: running (tasks=%s)\n",
                    w10task::tasksConfigPath().toUtf8().constData());
        return QCoreApplication::exec();
    }

    // GUI。
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("w10tasks"));
    QApplication::setApplicationDisplayName(QStringLiteral("任务计划程序"));
    w10de::theme::loadFromConfig(
        (QDir::homePath() + QStringLiteral("/.config/w10de/config.ini"))
            .toStdString());
    if (w10de::app::tryActivateExisting(QStringLiteral("Tasks"))) {
        return 0;
    }
    if (!w10de::app::registerService(
            QStringLiteral("Tasks"),
            [](const QString&) {
                for (QWidget* w : QApplication::topLevelWidgets()) {
                    if (auto* tw = qobject_cast<w10task::TaskWindow*>(w)) {
                        tw->show();
                        tw->raise();
                        tw->activateWindow();
                        break;
                    }
                }
            },
            &app)) {
        return 0;
    }
    w10task::TaskWindow window;
    window.show();
    return QApplication::exec();
}

// AUTOMOC：main.cpp 内 Q_OBJECT（TaskDaemon）需本文件 include 对应 .moc。
#include "main.moc"
