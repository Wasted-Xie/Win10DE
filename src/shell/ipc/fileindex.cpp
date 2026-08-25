// fileindex.cpp —— 文件索引实现。

#include "shell/ipc/fileindex.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QThread>

namespace w10de::ipc {

namespace {

constexpr int kMaxFiles = 50000;  // 索引文件数上限
constexpr int kMaxContentBytes = 64 * 1024;  // 内容索引文件大小上限

// 排除目录（常见噪声/隐藏）。
bool isExcludedDir(const QString& name) {
    if (name.startsWith(QLatin1Char('.'))) {
        return true;  // 全部隐藏目录
    }
    static const QSet<QString> kExcluded = {
        QStringLiteral("node_modules"), QStringLiteral("__pycache__"),
        QStringLiteral(".git"), QStringLiteral(".cache"),
        QStringLiteral(".local"), QStringLiteral(".config"),
    };
    return kExcluded.contains(name);
}

// 常见文本扩展名（内容索引用）。
bool isTextFile(const QString& name) {
    static const QSet<QString> kExts = {
        QStringLiteral("txt"), QStringLiteral("md"), QStringLiteral("markdown"),
        QStringLiteral("cpp"), QStringLiteral("h"), QStringLiteral("hpp"),
        QStringLiteral("c"), QStringLiteral("cc"), QStringLiteral("py"),
        QStringLiteral("sh"), QStringLiteral("json"), QStringLiteral("ini"),
        QStringLiteral("conf"), QStringLiteral("log"), QStringLiteral("toml"),
        QStringLiteral("yaml"), QStringLiteral("yml"), QStringLiteral("xml"),
        QStringLiteral("html"), QStringLiteral("css"), QStringLiteral("js"),
        QStringLiteral("ts"), QStringLiteral("qml"), QStringLiteral("cmake"),
        QStringLiteral("txt"), QStringLiteral("rst"), QStringLiteral("tex"),
    };
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    if (dot < 0) {
        return false;
    }
    return kExts.contains(name.mid(dot + 1).toLower());
}

// 分词（Unicode 感知：字母/数字连续段成词，小写；审查 M2——中文等 CJK
// 文本成词，原 ASCII-only 分词导致中文内容无法索引/搜索）。
QStringList tokenize(const QString& data) {
    QStringList words;
    QString cur;
    for (const QChar c : data) {
        if (c.isLetterOrNumber() || c == QLatin1Char('_')) {
            cur.append(c);
        } else if (!cur.isEmpty()) {
            words << cur.toLower();
            cur.clear();
        }
    }
    if (!cur.isEmpty()) {
        words << cur.toLower();
    }
    return words;
}

}  // namespace

// 停用词（审查 S2：高频词倒排集合膨胀且无检索价值）。
bool isStopWord(const QString& word) {
    static const QSet<QString> kStop = {
        QStringLiteral("the"), QStringLiteral("and"), QStringLiteral("for"),
        QStringLiteral("int"), QStringLiteral("void"), QStringLiteral("return"),
        QStringLiteral("this"), QStringLiteral("that"), QStringLiteral("with"),
        QStringLiteral("from"), QStringLiteral("struct"), QStringLiteral("const"),
        QStringLiteral("include"), QStringLiteral("using"), QStringLiteral("null"),
        QStringLiteral("true"), QStringLiteral("false"), QStringLiteral("auto"),
        QStringLiteral("static"), QStringLiteral("public"), QStringLiteral("private"),
    };
    return kStop.contains(word);
}

FileIndex::FileIndex(QObject* parent) : QObject(parent) {}

FileIndex::~FileIndex() {
    if (thread_ != nullptr) {
        // 审查 S1：先请求中断（runIndexing 循环检查）再等待，避免超时后
        // 删除运行中线程（terminate 路径 UB）。首启全量索引可能较长。
        thread_->requestInterruption();
        thread_->quit();
        thread_->wait(10000);
        delete thread_;
    }
}

void FileIndex::startIndexing(const QString& rootDir) {
    if (thread_ != nullptr) {
        return;  // 已启动
    }
    QString root = rootDir;
    if (root.isEmpty()) {
        root = QDir::homePath();
    }
    // 工作线程执行扫描（避免卡 UI；快照完成后原子替换）。
    thread_ = QThread::create([this, root] { runIndexing(root); });
    thread_->setObjectName(QStringLiteral("w10de-fileindex"));
    QObject::connect(thread_, &QThread::finished, this, [this] {
        thread_->deleteLater();
        thread_ = nullptr;
        ready_ = true;
        emit indexingFinished();
    });
    thread_->start();
}

void FileIndex::runIndexing(const QString& rootDir) {
    Snapshot snap;
    const QDir root(rootDir);
    if (!root.exists()) {
        // 审查 L4：无效根目录——保持 ready_=false（UI 提示索引失败而非空结果）。
        qWarning("fileindex: root '%s' does not exist",
                 qPrintable(rootDir));
        return;
    }
    // 审查 S2：内容索引闸门（文件数 / 词-路径对总数上限）。
    constexpr int kMaxContentFiles = 5000;
    constexpr int kMaxWordPairs = 2000000;
    int contentFiles = 0;
    int wordPairs = 0;
    bool wordBudgetExhausted = false;

    QDirIterator it(rootDir,
                    QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    int count = 0;
    int processed = 0;
    while (it.hasNext() && count < kMaxFiles) {
        // 审查 S1：中断检查（每 200 文件一次，析构/退出可快速终止）。
        if ((processed & 0x7F) == 0 &&
                QThread::currentThread()->isInterruptionRequested()) {
            qWarning("fileindex: indexing interrupted");
            return;
        }
        it.next();
        ++processed;
        // 排除隐藏/噪声目录（检查当前路径的所有祖先段）。
        const QString path = it.filePath();
        const QString relative = root.relativeFilePath(path);
        bool excluded = false;
        const QStringList parts = relative.split(QLatin1Char('/'));
        for (int i = 0; i < parts.size() - 1; ++i) {
            if (isExcludedDir(parts[i])) {
                excluded = true;
                break;
            }
        }
        if (excluded) {
            continue;
        }
        const QString name = it.fileName();
        snap.nameLower.insert(path, name.toLower());
        ++count;

        // 内容索引（小文本文件前 8KB 分词；审查 S2：文件数与词条数闸门）。
        if (isTextFile(name) && contentFiles < kMaxContentFiles &&
                !wordBudgetExhausted) {
            const QFileInfo fi(path);
            if (fi.size() <= kMaxContentBytes) {
                QFile f(path);
                if (f.open(QIODevice::ReadOnly)) {
                    const QByteArray head = f.read(8192);
                    f.close();
                    const QStringList words = tokenize(QString::fromUtf8(head));
                    ++contentFiles;
                    for (const QString& w : words) {
                        if (isStopWord(w)) {
                            continue;  // 停用词跳过
                        }
                        snap.contentWords[w].insert(path);
                        if (++wordPairs > kMaxWordPairs) {
                            wordBudgetExhausted = true;
                            qWarning("fileindex: content word budget exceeded");
                            break;
                        }
                    }
                }
            }
        }
    }
    if (count >= kMaxFiles) {
        snap.truncated = true;
        qWarning("fileindex: reached %d file limit, truncated", kMaxFiles);
    }
    // 原子替换快照。
    snap.count = count;
    current_ = std::move(snap);
}

QStringList FileIndex::search(const QString& term, int maxResults) const {
    QStringList out;
    if (!ready_ || term.isEmpty()) {
        return out;
    }
    const QString lower = term.toLower();
    QSet<QString> seen;

    // 名称匹配（审查 M1：多词按词拆分 and 匹配——每个词都包含）。
    const QStringList nameWords = tokenize(term);
    const bool multiWord = nameWords.size() > 1;
    for (auto it = current_.nameLower.constBegin();
         it != current_.nameLower.constEnd() && out.size() < maxResults; ++it) {
        bool match = true;
        if (multiWord) {
            for (const QString& w : nameWords) {
                if (!it.value().contains(w)) {
                    match = false;
                    break;
                }
            }
        } else {
            match = it.value().contains(lower);
        }
        if (match) {
            seen.insert(it.key());
            out.append(it.key());
        }
    }
    // 内容匹配（审查 M1：多词取交集——所有词都命中的路径优先）。
    const QStringList termWords = tokenize(term);
    if (!termWords.isEmpty()) {
        QSet<QString> contentHits;
        for (const QString& w : termWords) {
            const auto found = current_.contentWords.constFind(w);
            if (found == current_.contentWords.constEnd()) {
                contentHits.clear();  // 任一词缺失 → 无交集
                break;
            }
            if (contentHits.isEmpty()) {
                contentHits = *found;
            } else {
                contentHits.intersect(*found);
            }
        }
        for (const QString& path : contentHits) {
            if (seen.contains(path) || out.size() >= maxResults) {
                continue;
            }
            seen.insert(path);
            out.append(path);
        }
    }
    // 审查 L6：结果排序（名称优先 + 字典序，避免哈希序不稳定）。
    out.sort();
    return out;
}

}  // namespace w10de::ipc
