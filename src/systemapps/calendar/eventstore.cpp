// EventStore 实现（可选拓展 E11 日历完整版）。

#include "systemapps/calendar/eventstore.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSettings>

#include <algorithm>

namespace w10de::calendar {

// ---- 月历网格 ----

int daysInMonth(int year, int month) {
    // month: 1-12。
    return QDate(year, month, 1).daysInMonth();
}

QList<Cell> monthCells(int year, int month) {
    QList<Cell> cells;
    const QDate first(year, month, 1);
    // 周日起始：offset = dayOfWeek % 7（Qt：周一=1→偏移1；周日=7→0；
    // 周六=6→偏移 6 即首格为 7/26）。
    const int offset = first.dayOfWeek() % 7;
    const QDate gridStart = first.addDays(-offset);
    for (int i = 0; i < 42; ++i) {
        const QDate d = gridStart.addDays(i);
        Cell c;
        c.date = d;
        c.inMonth = d.month() == month && d.year() == year;
        cells.append(c);
    }
    return cells;
}

// ---- 事件存储 ----

namespace {
constexpr int kMaxEventId = 999999;
}

EventStore::EventStore(const QString& configPathOverride) {
    if (!configPathOverride.isEmpty()) {
        configPath_ = configPathOverride;
    } else {
        // 审查 S2：环境变量覆盖（测试/部署隔离）。
        const QByteArray env = qgetenv("W10DE_CALENDAR_CONFIG");
        if (!env.isEmpty()) {
            configPath_ = QString::fromUtf8(env);
        } else {
            configPath_ = QDir::homePath()
                + QStringLiteral("/.config/w10de/calendar.ini");
        }
    }
}

QList<CalendarEvent> EventStore::eventsForDate(const QString& date) const {
    QList<CalendarEvent> result;
    QSettings s(configPath_, QSettings::IniFormat);
    const int count = s.beginReadArray(QStringLiteral("events"));
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        const QString d = s.value(QStringLiteral("date")).toString();
        if (d != date) continue;
        CalendarEvent e;
        e.id = s.value(QStringLiteral("id")).toInt();
        e.date = d;
        e.time = s.value(QStringLiteral("time")).toString();
        e.title = s.value(QStringLiteral("title")).toString();
        e.detail = s.value(QStringLiteral("detail")).toString();
        result.append(e);
    }
    s.endArray();
    // 排序：全天（time 空）在前，其余按时间。
    std::sort(result.begin(), result.end(), [](const CalendarEvent& a,
                                               const CalendarEvent& b) {
        if (a.time.isEmpty() != b.time.isEmpty()) {
            return a.time.isEmpty();  // 全天优先
        }
        return a.time < b.time;
    });
    return result;
}

QStringList EventStore::monthDates(int year, int month) const {
    QSet<QString> dates;
    QSettings s(configPath_, QSettings::IniFormat);
    const int count = s.beginReadArray(QStringLiteral("events"));
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        const QDate d = QDate::fromString(
            s.value(QStringLiteral("date")).toString(),
            QStringLiteral("yyyy-MM-dd"));
        if (d.isValid() && d.year() == year && d.month() == month) {
            dates.insert(d.toString(QStringLiteral("yyyy-MM-dd")));
        }
    }
    s.endArray();
    QStringList list = dates.values();
    std::sort(list.begin(), list.end());
    return list;
}

CalendarEvent EventStore::add(const QString& date, const QString& time,
                              const QString& title, const QString& detail) {
    CalendarEvent e;
    e.date = date;
    e.time = time;
    e.title = title;
    e.detail = detail;
    QSettings s(configPath_, QSettings::IniFormat);
    // 分配 id：现有最大 id + 1（首条为 1）。
    int maxId = 0;
    const int count = s.beginReadArray(QStringLiteral("events"));
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        maxId = qMax(maxId, s.value(QStringLiteral("id")).toInt());
    }
    s.endArray();
    e.id = maxId + 1;
    if (e.id > kMaxEventId) {
        lastError_ = QStringLiteral("事件数量超出限制");
        return CalendarEvent();
    }
    s.beginWriteArray(QStringLiteral("events"));
    // 重写全部（QSettings 数组追加语义简单可靠）。
    // 先读旧事件。
    QList<CalendarEvent> all;
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        CalendarEvent old;
        old.id = s.value(QStringLiteral("id")).toInt();
        old.date = s.value(QStringLiteral("date")).toString();
        old.time = s.value(QStringLiteral("time")).toString();
        old.title = s.value(QStringLiteral("title")).toString();
        old.detail = s.value(QStringLiteral("detail")).toString();
        all.append(old);
    }
    all.append(e);
    for (int i = 0; i < all.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("id"), all[i].id);
        s.setValue(QStringLiteral("date"), all[i].date);
        s.setValue(QStringLiteral("time"), all[i].time);
        s.setValue(QStringLiteral("title"), all[i].title);
        s.setValue(QStringLiteral("detail"), all[i].detail);
    }
    s.endArray();
    s.sync();
    if (s.status() != QSettings::NoError) {
        lastError_ = QStringLiteral("写入日历失败：%1").arg(configPath_);
        return CalendarEvent();
    }
    lastError_.clear();
    return e;
}

bool EventStore::update(const CalendarEvent& e) {
    QSettings s(configPath_, QSettings::IniFormat);
    const int count = s.beginReadArray(QStringLiteral("events"));
    QList<CalendarEvent> all;
    bool found = false;
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        CalendarEvent old;
        old.id = s.value(QStringLiteral("id")).toInt();
        old.date = s.value(QStringLiteral("date")).toString();
        old.time = s.value(QStringLiteral("time")).toString();
        old.title = s.value(QStringLiteral("title")).toString();
        old.detail = s.value(QStringLiteral("detail")).toString();
        if (old.id == e.id) {
            // 审查 S3：空标题在存储层拒绝（UI 之外的第二道防线）。
            if (e.title.trimmed().isEmpty()) {
                lastError_ = QStringLiteral("事件标题不能为空");
                return false;
            }
            found = true;
            all.append(e);
        } else {
            all.append(old);
        }
    }
    s.endArray();
    // 审查 M2：不存在 id 与 remove 对称地返回 false（而非静默成功）。
    if (!found) {
        lastError_ = QStringLiteral("事件不存在（id=%1）").arg(e.id);
        return false;
    }
    s.beginWriteArray(QStringLiteral("events"));
    for (int i = 0; i < all.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("id"), all[i].id);
        s.setValue(QStringLiteral("date"), all[i].date);
        s.setValue(QStringLiteral("time"), all[i].time);
        s.setValue(QStringLiteral("title"), all[i].title);
        s.setValue(QStringLiteral("detail"), all[i].detail);
    }
    s.endArray();
    s.sync();
    if (s.status() != QSettings::NoError) {
        lastError_ = QStringLiteral("更新日历失败：%1").arg(configPath_);
        return false;
    }
    lastError_.clear();
    return true;
}

bool EventStore::remove(int id) {
    QSettings s(configPath_, QSettings::IniFormat);
    const int count = s.beginReadArray(QStringLiteral("events"));
    QList<CalendarEvent> all;
    bool found = false;
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        const int eid = s.value(QStringLiteral("id")).toInt();
        if (eid == id) {
            found = true;
            continue;  // 跳过删除项
        }
        CalendarEvent old;
        old.id = eid;
        old.date = s.value(QStringLiteral("date")).toString();
        old.time = s.value(QStringLiteral("time")).toString();
        old.title = s.value(QStringLiteral("title")).toString();
        old.detail = s.value(QStringLiteral("detail")).toString();
        all.append(old);
    }
    s.endArray();
    if (!found) {
        lastError_ = QStringLiteral("事件不存在（id=%1）").arg(id);
        return false;
    }
    s.beginWriteArray(QStringLiteral("events"));
    for (int i = 0; i < all.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue(QStringLiteral("id"), all[i].id);
        s.setValue(QStringLiteral("date"), all[i].date);
        s.setValue(QStringLiteral("time"), all[i].time);
        s.setValue(QStringLiteral("title"), all[i].title);
        s.setValue(QStringLiteral("detail"), all[i].detail);
    }
    s.endArray();
    s.sync();
    if (s.status() != QSettings::NoError) {
        lastError_ = QStringLiteral("删除失败：%1").arg(configPath_);
        return false;
    }
    lastError_.clear();
    return true;
}

}  // namespace w10de::calendar
