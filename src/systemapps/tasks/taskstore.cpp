// 任务计划程序数据层实现（G5）。
#include "systemapps/tasks/taskstore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>

#include <cstdio>

namespace w10task {

namespace {

// 读取整数配置项；非法/缺失返回 fallback。
// 审查 M3：越界（minute>59 等）也视为非法回退并记 stderr（防静默反转调度）。
int intValue(const QString& value, int fallback, int min, int max) {
    bool ok = false;
    const int v = value.trimmed().toInt(&ok);
    if (!ok || v < min || v > max) {
        std::fprintf(stderr, "tasks: 非法调度字段 '%s'（回退 %d）\n",
                     value.toUtf8().constData(), fallback);
        return fallback;
    }
    return v;
}

}  // namespace

QString tasksConfigPath() {
    // 审查 M5：支持环境变量注入（selftest 隔离到临时目录；正常用
    // ~/.config/w10de/tasks.ini）。
    const QByteArray env = qgetenv("W10DE_TASKS_CONFIG");
    if (!env.isEmpty()) {
        return QString::fromUtf8(env);
    }
    return QDir::homePath() + QStringLiteral("/.config/w10de/tasks.ini");
}

QList<Task> loadTasks() {
    QList<Task> tasks;
    QFile f(tasksConfigPath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return tasks;  // 无配置：空列表
    }
    QTextStream ts(&f);
    QString line;
    Task* current = nullptr;
    int id = 0;
    while (ts.readLineInto(&line)) {
        const QString t = line.trimmed();
        if (t.isEmpty() || t.startsWith(QLatin1Char('#'))
                || t.startsWith(QLatin1Char(';'))) {
            continue;
        }
        if (t.startsWith(QLatin1Char('['))) {
            const QRegularExpressionMatch m = QRegularExpression(
                QStringLiteral("^\\[task:(\\d+)\\]$")).match(t);
            if (m.hasMatch()) {
                tasks.append(Task());
                current = &tasks.last();
                current->id = m.captured(1).toInt();
                id = qMax(id, current->id);
            } else {
                current = nullptr;  // 未知段：跳过
            }
            continue;
        }
        if (current == nullptr) {
            continue;
        }
        const int eq = t.indexOf(QLatin1Char('='));
        if (eq < 0) {
            continue;
        }
        const QString key = t.left(eq).trimmed();
        const QString value = t.mid(eq + 1).trimmed();
        if (key == QLatin1String("name")) {
            current->name = value;
        } else if (key == QLatin1String("command")) {
            current->command = value;
        } else if (key == QLatin1String("minute")) {
            current->minute = intValue(value, -1, -1, 59);
        } else if (key == QLatin1String("hour")) {
            current->hour = intValue(value, -1, -1, 23);
        } else if (key == QLatin1String("day_of_month")) {
            current->dayOfMonth = intValue(value, -1, -1, 31);
        } else if (key == QLatin1String("month")) {
            current->month = intValue(value, -1, -1, 12);
        } else if (key == QLatin1String("day_of_week")) {
            current->dayOfWeek = intValue(value, -1, -1, 6);
        } else if (key == QLatin1String("enabled")) {
            current->enabled = intValue(value, 1, 0, 1) != 0;
        } else if (key == QLatin1String("last_run")) {
            current->lastRun = value;
        } else if (key == QLatin1String("last_result")) {
            current->lastResult = value;
        }
        // 未识别键忽略（保留在文件中）。
    }
    f.close();
    // 过滤空任务（无 command）。
    QList<Task> filtered;
    for (const Task& t : tasks) {
        if (!t.command.isEmpty()) {
            filtered.append(t);
        } else {
            std::fprintf(stderr, "tasks: 跳过空命令任务 id=%d\n", t.id);
        }
    }
    Q_UNUSED(id);
    return filtered;
}

bool saveTasks(const QList<Task>& tasks) {
    const QString path = tasksConfigPath();
    // 用 QFileInfo 取目录部分（QDir(path) 会把文件路径当目录——mkpath
    // 可能创建同名目录导致 QFile 打开失败，selftest 实测）。
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    // 审查 M1：name/command 换行转义（裸换行破坏 INI 行结构——回读截断）。
    const auto sanitize = [](const QString& s) {
        QString out = s;
        out.replace(QLatin1Char('\n'), QLatin1Char(' '));
        out.replace(QLatin1Char('\r'), QLatin1Char(' '));
        return out;
    };
    QTextStream ts(&f);
    ts << "# Win10DE 任务计划（w10tasks）\n"
       << "# minute=-1 每分 / hour=-1 每时 / day_of_month=-1 每天 /\n"
       << "# month=-1 每月 / day_of_week=-1 忽略（0=周日）\n"
       << "# 注意：保存为全量重写（未识别键与手写注释不保留）\n";
    for (const Task& t : tasks) {
        ts << "\n[task:" << t.id << "]\n"
           << "name = " << sanitize(t.name) << "\n"
           << "command = " << sanitize(t.command) << "\n"
           << "minute = " << t.minute << "\n"
           << "hour = " << t.hour << "\n"
           << "day_of_month = " << t.dayOfMonth << "\n"
           << "month = " << t.month << "\n"
           << "day_of_week = " << t.dayOfWeek << "\n"
           << "enabled = " << (t.enabled ? 1 : 0) << "\n";
        if (!t.lastRun.isEmpty()) {
            ts << "last_run = " << t.lastRun << "\n";
        }
        if (!t.lastResult.isEmpty()) {
            ts << "last_result = " << t.lastResult << "\n";
        }
    }
    f.close();
    return true;
}

bool taskMatches(const Task& task, const QDateTime& at) {
    // minute/hour：-1 = 通配（每分/每时）；否则精确匹配。
    if (task.minute >= 0 && at.time().minute() != task.minute) {
        return false;
    }
    if (task.hour >= 0 && at.time().hour() != task.hour) {
        return false;
    }
    // month：-1 = 每月；否则精确。
    if (task.month >= 0 && at.date().month() != task.month) {
        return false;
    }
    // day_of_month / day_of_week：-1 = 忽略；两者同时指定时任一匹配即触发
    // （cron 语义）。
    const bool domOk = task.dayOfMonth < 0
        || at.date().day() == task.dayOfMonth;
    const int dow = at.date().dayOfWeek() % 7;  // Qt: Mon=1..Sun=7 → 0=Sun
    const bool dowOk = task.dayOfWeek < 0 || dow == task.dayOfWeek;
    if (task.dayOfMonth >= 0 && task.dayOfWeek >= 0) {
        return domOk || dowOk;
    }
    return domOk && dowOk;
}

QString triggerText(const Task& task) {
    QStringList parts;
    const QStringList kDays = {QStringLiteral("周日"), QStringLiteral("周一"),
                               QStringLiteral("周二"), QStringLiteral("周三"),
                               QStringLiteral("周四"), QStringLiteral("周五"),
                               QStringLiteral("周六")};
    const QStringList kShortDays = {QStringLiteral("日"), QStringLiteral("一"),
                                    QStringLiteral("二"), QStringLiteral("三"),
                                    QStringLiteral("四"), QStringLiteral("五"),
                                    QStringLiteral("六")};
    // 日期/月份条件（审查 M2：month 条件与 dom/dow 分支合并——"每月 15 日"
    // 若指定了月份应为"M 月 15 日"，不丢信息）。
    const bool hasDomDow = task.dayOfMonth >= 0 || task.dayOfWeek >= 0;
    if (task.dayOfMonth >= 0 && task.dayOfWeek < 0) {
        parts << (task.month >= 0
            ? QStringLiteral("%1 月 %2 日").arg(task.month).arg(task.dayOfMonth)
            : QStringLiteral("每月 %1 日").arg(task.dayOfMonth));
    } else if (task.dayOfWeek >= 0 && task.dayOfMonth < 0) {
        parts << (task.month >= 0
            ? QStringLiteral("%1 月每周%2")
                  .arg(task.month)
                  .arg(task.dayOfWeek >= 0 && task.dayOfWeek <= 6
                      ? kShortDays.at(task.dayOfWeek) : QStringLiteral("?"))
            : (task.dayOfWeek >= 0 && task.dayOfWeek <= 6
                ? kDays.at(task.dayOfWeek) : QStringLiteral("周?")));
    } else if (task.dayOfMonth >= 0 && task.dayOfWeek >= 0) {
        parts << QStringLiteral("每月 %1 日或每周%2")
                     .arg(task.dayOfMonth)
                     .arg(task.dayOfWeek >= 0 && task.dayOfWeek <= 6
                         ? kShortDays.at(task.dayOfWeek) : QStringLiteral("?"));
    } else if (task.month >= 0) {
        parts << QStringLiteral("%1 月").arg(task.month);
    }
    // 无日期/月份条件：仅"每天 HH:MM"加前缀（其余组合的时间部分自带
    // 描述（"每小时 30 分"/"每小时 7 点内每分钟"），重复加前缀会成
    // "每小时 每小时 30 分"）。
    if (!hasDomDow && task.month < 0) {
        if (task.hour >= 0 && task.minute >= 0) {
            parts << QStringLiteral("每天");
        }
    }
    // 时间条件（审查 M2b：hour>=0 且 minute<0 = "每小时第 X 时内每分钟"，
    // 不再把 hour 当分钟显示）。
    if (task.hour >= 0 && task.minute >= 0) {
        parts << QStringLiteral("%1:%2")
                     .arg(task.hour, 2, 10, QLatin1Char('0'))
                     .arg(task.minute, 2, 10, QLatin1Char('0'));
    } else if (task.hour >= 0) {
        parts << QStringLiteral("每小时 %1 点内每分钟").arg(task.hour);
    } else if (task.minute >= 0) {
        parts << QStringLiteral("每小时 %1 分").arg(task.minute);
    } else if (parts.isEmpty()) {
        parts << QStringLiteral("每分钟");
    }
    return parts.join(QStringLiteral(" "));
}

}  // namespace w10task
