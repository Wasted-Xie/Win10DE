// 任务计划程序数据层（G5：cron 等价调度）。
//
// 任务配置：~/.config/w10de/tasks.ini
//   [task:1]
//   name = 备份
//   command = /usr/bin/rsync -a ~/data /mnt/backup
//   minute = 30          # -1 = 每分钟；0-59
//   hour = 2             # -1 = 每小时；0-23
//   day_of_month = -1    # -1 = 每天；1-31
//   month = -1           # -1 = 每月；1-12
//   day_of_week = -1     # -1 = 忽略；0-6（0=周日）
//   enabled = 1
//   last_run = 2026-08-05 02:30:00   # 守护更新（最近一次执行）
//   last_result = OK                 # OK / 退出码 / 错误描述
//
// 匹配语义（cron 风格）：所有指定字段匹配才触发（AND）。minute/hour 为
// -1 时视为"每分/每时"通配；day_of_month/month/day_of_week 为 -1 时忽略。
// day_of_week 与 day_of_month 同时指定时任一匹配即触发（cron 语义）。
#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace w10task {

struct Task {
    int id = 0;
    QString name;
    QString command;
    int minute = -1;      // -1 = 每分
    int hour = -1;        // -1 = 每时
    int dayOfMonth = -1;  // -1 = 每天
    int month = -1;       // -1 = 每月
    int dayOfWeek = -1;   // -1 = 忽略
    bool enabled = true;
    QString lastRun;      // 上次执行时间（"yyyy-MM-dd HH:mm:ss"；空=未执行）
    QString lastResult;   // OK / 退出码 / 错误
};

// 任务配置文件路径（~/.config/w10de/tasks.ini；环境变量
// W10DE_TASKS_CONFIG 可覆盖——selftest 隔离用）。
QString tasksConfigPath();

// 加载任务（文件缺失 → 空列表；非法/越界字段跳过并记 stderr）。
QList<Task> loadTasks();

// 保存任务列表（全量重写；name/command 换行转义；未识别键与手写注释
// 不保留）。返回是否成功。
bool saveTasks(const QList<Task>& tasks);

// cron 风格调度匹配（taskMatches 的字段语义见头注释）。
bool taskMatches(const Task& task, const QDateTime& at);

// 人类可读触发器描述（UI 显示："每天 02:30" / "每周一 08:00" 等）。
QString triggerText(const Task& task);

}  // namespace w10task
