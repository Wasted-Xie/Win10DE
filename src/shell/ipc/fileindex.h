// fileindex —— 文件索引搜索（KDE-GAP 高优先 #5，Baloo 等价 MVP）。
//
// 后台线程索引主目录文件：
//   - 名称索引：路径 → 小写文件名（搜索子串匹配）
//   - 内容索引：小文本文件（常见文本扩展名 + <64KB）前 8KB 分词 → 路径
// 搜索时名称 + 内容合并（限 30 结果）。排除隐藏目录/常见噪声目录
// （.git/.cache/node_modules 等）。索引在独立线程构建，完成信号通知。
//
// 线程模型：索引线程只写独立快照；完成后原子替换 current_（搜索读
// current_——无锁读取安全：QStringList/QHash 拷贝替换，旧快照由 Qt
// 隐式共享管理）。

#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QThread;
class QTimer;

namespace w10de::ipc {

class FileIndex : public QObject {
    Q_OBJECT
public:
    explicit FileIndex(QObject* parent = nullptr);
    ~FileIndex() override;

    // 启动后台索引（幂等；已完成则 no-op）。
    void startIndexing(const QString& rootDir = QString());

    // 搜索：返回匹配路径（名称子串 + 内容分词；合并去重，限 maxResults）。
    // 索引未完成时返回空（调用方显示"索引中"）。
    QStringList search(const QString& term, int maxResults = 30) const;

    // 索引是否完成（供 UI 显示状态）。
    bool ready() const { return ready_; }
    int indexedCount() const { return current_.count; }
    bool truncated() const { return current_.truncated; }

signals:
    // 索引完成（新快照可用）。
    void indexingFinished();

private slots:
    // 审查 F1（增量更新）：监听目录/文件变化，单条目增量入库。
    void onDirectoryChanged(const QString& path);
    void onFileChanged(const QString& path);
    // 事件归并（审查 M：批量目录变化合并为一次处理，避免每事件
    // 全快照深拷贝卡 UI）。
    void flushPendingDirs();

private:
    // 当前快照（原子替换；审查 L1：count/truncated 随快照，消除跨线程
    // 竞争；L2：删除只写不读的 paths）。定义须先于引用它的方法声明
    //（GCC：嵌套类型在声明点后可见）。
    struct Snapshot {
        QHash<QString, QString> nameLower;  // 路径 → 小写文件名
        QHash<QString, QSet<QString>> contentWords;  // 词 → 路径集合
        int count = 0;              // 索引文件数
        bool truncated = false;     // 达到上限被截断
    };

    // 在工作线程执行全量扫描（静态：线程入口）。
    void runIndexing(const QString& rootDir);
    // 单文件索引（名称 + 内容；增量更新与全量扫描共用，审查 F1）。
    void indexSingleFile(const QString& path, Snapshot& snap);
    // 从快照移除单路径（名称 + 内容倒排）。
    static void removeFromSnapshot(Snapshot& snap, const QString& path);
    // 批量移除（审查 M：contentWords 一次全表扫移除全部路径，
    // 避免每文件全扫一遍 O(k·|V|)）。
    static void removePathsFromSnapshot(Snapshot& snap,
                                        const QStringList& paths);
    // 索引完成后注册 watcher（rootDir + 顶层子目录；更深层靠手动重建）。
    void setupWatcher(const QString& rootDir);

    Snapshot current_;
    bool ready_ = false;
    QThread* thread_ = nullptr;
    QFileSystemWatcher* watcher_ = nullptr;
    // 事件归并状态（审查 M）：待处理目录 + 单次 timer。
    QSet<QString> pendingDirs_;
    QTimer* pendingTimer_ = nullptr;
};

}  // namespace w10de::ipc
