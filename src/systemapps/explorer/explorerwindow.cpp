#include "systemapps/explorer/explorerwindow.h"

#include "systemapps/explorer/fileops.h"

#include <QAction>
#include <QCloseEvent>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QSplitter>
#include <QStatusBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>

#include "theme/colors.h"

namespace w10de::explorer {

ExplorerWindow::ExplorerWindow(QWidget* parent) : QMainWindow(parent) {
    buildUi();
    applyTheme();

    // 初始位置：主目录（Win10 快速访问默认行为）。
    const QString home =
        QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    navigateTo(home.isEmpty() ? QStringLiteral("/") : home);

    setWindowTitle(QStringLiteral("文件资源管理器"));
    resize(900, 600);
}

void ExplorerWindow::buildUi() {
    // ---- 顶部导航条：后退/前进/上级 | 地址栏 | 刷新/新建文件夹 ----
    auto* navBar = new QWidget(this);
    auto* navLayout = new QHBoxLayout(navBar);
    navLayout->setContentsMargins(6, 4, 6, 4);
    navLayout->setSpacing(4);

    backBtn_ = new QPushButton(QStringLiteral("←"), navBar);
    backBtn_->setToolTip(QStringLiteral("后退 (Alt+←)"));
    backBtn_->setFixedSize(30, 26);
    fwdBtn_ = new QPushButton(QStringLiteral("→"), navBar);
    fwdBtn_->setToolTip(QStringLiteral("前进 (Alt+→)"));
    fwdBtn_->setFixedSize(30, 26);
    auto* upBtn = new QPushButton(QStringLiteral("↑"), navBar);
    upBtn->setToolTip(QStringLiteral("上级目录 (Alt+↑)"));
    upBtn->setFixedSize(30, 26);
    auto* refreshBtn = new QPushButton(QStringLiteral("⟳"), navBar);
    refreshBtn->setToolTip(QStringLiteral("刷新 (F5)"));
    refreshBtn->setFixedSize(30, 26);
    auto* newFolderBtn = new QPushButton(QStringLiteral("新建文件夹"), navBar);
    newFolderBtn->setToolTip(QStringLiteral("新建文件夹 (Ctrl+Shift+N)"));
    navLayout->addWidget(backBtn_);
    navLayout->addWidget(fwdBtn_);
    navLayout->addWidget(upBtn);
    navLayout->addSpacing(4);
    addressBar_ = new QLineEdit(navBar);
    addressBar_->setPlaceholderText(QStringLiteral("路径"));
    navLayout->addWidget(addressBar_, 1);
    navLayout->addWidget(refreshBtn);
    navLayout->addWidget(newFolderBtn);

    connect(backBtn_, &QPushButton::clicked, this, &ExplorerWindow::goBack);
    connect(fwdBtn_, &QPushButton::clicked, this, &ExplorerWindow::goForward);
    connect(upBtn, &QPushButton::clicked, this, &ExplorerWindow::goUp);
    connect(refreshBtn, &QPushButton::clicked, this, &ExplorerWindow::refresh);
    connect(newFolderBtn, &QPushButton::clicked, this, &ExplorerWindow::newFolder);
    connect(addressBar_, &QLineEdit::returnPressed, this, &ExplorerWindow::addressEntered);
    setMenuWidget(navBar);

    // ---- 主体：左侧（快速访问 + 目录树）| 右侧文件区 ----
    model_ = new QFileSystemModel(this);
    model_->setRootPath(QStringLiteral("/"));
    model_->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden);

    auto* splitter = new QSplitter(this);
    auto* leftPanel = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    // 快速访问（Win10 左侧导航窗格顶层）。
    auto* quickLabel = new QLabel(QStringLiteral("快速访问"), leftPanel);
    leftLayout->addWidget(quickLabel);

    treeView_ = new QTreeView(leftPanel);
    treeView_->setModel(model_);
    treeView_->setRootIndex(model_->index(QStringLiteral("/")));
    treeView_->hideColumn(1);
    treeView_->hideColumn(2);
    treeView_->hideColumn(3);
    treeView_->setHeaderHidden(true);
    treeView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(treeView_, &QTreeView::clicked, this,
            [this](const QModelIndex& idx) {
                navigateTo(model_->filePath(idx));
            });
    leftLayout->addWidget(treeView_, 1);

    fileView_ = new QListView(splitter);
    fileView_->setModel(model_);
    fileView_->setViewMode(QListView::IconMode);
    fileView_->setResizeMode(QListView::Adjust);
    fileView_->setMovement(QListView::Static);
    fileView_->setUniformItemSizes(false);
    fileView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    fileView_->setDragDropMode(QAbstractItemView::DragDrop);
    fileView_->setDefaultDropAction(Qt::MoveAction);
    fileView_->setEditTriggers(QAbstractItemView::SelectedClicked |
                               QAbstractItemView::EditKeyPressed);
    fileView_->setWordWrap(true);
    connect(fileView_, &QListView::doubleClicked, this, &ExplorerWindow::openItem);
    connect(fileView_, &QListView::customContextMenuRequested,
            this, &ExplorerWindow::showContextMenu);
    fileView_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(fileView_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ExplorerWindow::updateStatus);

    splitter->addWidget(leftPanel);
    splitter->addWidget(fileView_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 680});
    setCentralWidget(splitter);

    // 状态栏：先经 statusBar() 获取（首次调用自动创建），label 挂其上；
    // 不得先 addWidget 再 setStatusBar 替换（旧栏销毁 → label 悬垂崩溃）。
    statusLabel_ = new QLabel(statusBar());
    statusBar()->addWidget(statusLabel_);

    // 快捷键（Windows 语义）。全部用 lambda 连接，避免 SLOT 字符串与
    // QWidget 内置槽/自定义枚举（PasteMode）的 moc 解析问题。
    auto act = [this](QKeySequence seq, auto&& fn) {
        auto* a = new QAction(this);
        a->setShortcut(seq);
        connect(a, &QAction::triggered, this, std::forward<decltype(fn)>(fn));
        addAction(a);
    };
    act(QKeySequence(QStringLiteral("Ctrl+C")),
        [this]() { copySelected(PasteMode::Copy); });
    act(QKeySequence(QStringLiteral("Ctrl+X")),
        [this]() { copySelected(PasteMode::Move); });
    act(QKeySequence(QStringLiteral("Ctrl+V")), [this]() { paste(); });
    act(QKeySequence(QStringLiteral("Delete")), [this]() { deleteSelected(); });
    act(QKeySequence(QStringLiteral("F2")), [this]() { renameSelected(); });
    act(QKeySequence(QStringLiteral("Ctrl+Shift+N")), [this]() { newFolder(); });
    act(QKeySequence(QStringLiteral("F5")), [this]() { refresh(); });
    act(QKeySequence(QStringLiteral("Alt+Return")), [this]() { showProperties(); });
    act(QKeySequence(QStringLiteral("Backspace")), [this]() { goUp(); });
    act(QKeySequence(QStringLiteral("Alt+Left")), [this]() { goBack(); });
    act(QKeySequence(QStringLiteral("Alt+Right")), [this]() { goForward(); });
}

void ExplorerWindow::applyTheme() {
    const QColor bg = theme::kTaskbarBackground();
    const QColor fg = theme::kTextPrimary();
    setStyleSheet(QStringLiteral(
        "QMainWindow, QSplitter, QWidget { background: %1; color: %2; }"
        "QLineEdit, QListView, QTreeView { background: %3; color: %2;"
        "  border: 1px solid %4; }"
        "QPushButton, QToolButton { background: %4; color: %2; border: none;"
        "  padding: 3px 10px; border-radius: 2px; }"
        "QPushButton:hover, QToolButton:hover { background: %5; }"
        "QListView::item:selected, QTreeView::item:selected { background: %6;"
        "  color: %7; }")
        .arg(bg.name())
        .arg(fg.name())
        .arg(theme::kStartMenuBackground().name())
        .arg(theme::kHoverBackground().name())
        .arg(theme::kPressedBackground().name())
        .arg(theme::kAccentBlue().name())
        .arg(theme::kAccentText().name()));
    statusBar()->setStyleSheet(QStringLiteral("background: %1; color: %2;")
        .arg(theme::kHoverBackground().name(), theme::kTextSecondary().name()));
}

void ExplorerWindow::navigateTo(const QString& path) {
    const QFileInfo info(path);
    if (!info.isDir()) {
        return;
    }
    const QString canonical = QDir(path).canonicalPath();
    if (canonical.isEmpty()) {
        return;
    }
    // 导航历史（后退/前进）。
    if (!navigating_ && !currentDir_.isEmpty() && currentDir_ != canonical) {
        backStack_.push_back(currentDir_);
        fwdStack_.clear();
    }
    currentDir_ = canonical;
    model_->setRootPath(canonical);
    fileView_->setRootIndex(model_->index(canonical));
    treeView_->setCurrentIndex(model_->index(canonical));
    if (!navigating_) {
        addressBar_->setText(canonical);
    }
    setWindowTitle(QStringLiteral("%1 - 文件资源管理器").arg(QFileInfo(canonical).fileName()));
    updateNavState();
    updateStatus();
}

void ExplorerWindow::goBack() {
    if (backStack_.isEmpty()) {
        return;
    }
    const QString target = backStack_.takeLast();
    if (!currentDir_.isEmpty()) {
        fwdStack_.push_back(currentDir_);
    }
    navigating_ = true;
    navigateTo(target);
    navigating_ = false;
}

void ExplorerWindow::goForward() {
    if (fwdStack_.isEmpty()) {
        return;
    }
    const QString target = fwdStack_.takeLast();
    if (!currentDir_.isEmpty()) {
        backStack_.push_back(currentDir_);
    }
    navigating_ = true;
    navigateTo(target);
    navigating_ = false;
}

void ExplorerWindow::goUp() {
    const QDir dir(currentDir_);
    if (dir.isRoot()) {
        return;
    }
    navigateTo(dir.filePath(QStringLiteral("..")));
}

void ExplorerWindow::refresh() {
    model_->setRootPath(currentDir_);
    navigateTo(currentDir_);
}

void ExplorerWindow::addressEntered() {
    const QString p = addressBar_->text().trimmed();
    if (p.isEmpty()) {
        return;
    }
    const QFileInfo info(p);
    if (info.isDir()) {
        navigateTo(p);
    } else if (info.exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    } else {
        statusLabel_->setText(QStringLiteral("找不到 \"%1\"").arg(p));
    }
}

void ExplorerWindow::quickAccessClicked(const QModelIndex& /*index*/) {
    // 预留：快速访问列表（MVP 由左侧树 + 主目录入口覆盖）。
}

void ExplorerWindow::openItem(const QModelIndex& index) {
    const QString path = model_->filePath(index);
    const QFileInfo info(path);
    if (info.isDir()) {
        navigateTo(path);
    } else {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void ExplorerWindow::openSelected() {
    const QModelIndexList sel = fileView_->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) {
        return;
    }
    for (const QModelIndex& idx : sel) {
        const QFileInfo info(model_->filePath(idx));
        if (info.isDir()) {
            navigateTo(info.absoluteFilePath());
            return;  // Windows：双击目录只进入第一个
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(info.absoluteFilePath()));
    }
}

void ExplorerWindow::copySelected(PasteMode mode) {
    const QModelIndexList sel = fileView_->selectionModel()->selectedIndexes();
    QStringList paths;
    for (const QModelIndex& idx : sel) {
        paths.append(model_->filePath(idx));
    }
    if (!paths.isEmpty()) {
        FileOps::setClipboard(paths, mode);
        updateStatus();
    }
}

void ExplorerWindow::paste() {
    const QStringList srcs = FileOps::clipboardPaths();
    if (srcs.isEmpty()) {
        return;
    }
    const PasteResult r = FileOps::pasteTo(currentDir_);
    statusLabel_->setText(QStringLiteral("已完成 %1 项，跳过 %2 项")
        .arg(r.ok).arg(r.skipped));
    refresh();
}

void ExplorerWindow::deleteSelected() {
    const QModelIndexList sel = fileView_->selectionModel()->selectedIndexes();
    QStringList paths;
    for (const QModelIndex& idx : sel) {
        paths.append(model_->filePath(idx));
    }
    if (paths.isEmpty() || !confirmDelete(paths)) {
        return;
    }
    int ok = 0;
    for (const QString& p : paths) {
        if (FileOps::moveToTrash(p)) {
            ++ok;
        }
    }
    statusLabel_->setText(QStringLiteral("已将 %1 项移到回收站").arg(ok));
    refresh();
}

bool ExplorerWindow::confirmDelete(const QStringList& paths) {
    const QString msg = paths.size() == 1
        ? QStringLiteral("确实要将 \"%1\" 移到回收站吗？").arg(QFileInfo(paths.first()).fileName())
        : QStringLiteral("确实要将选中的 %1 项移到回收站吗？").arg(paths.size());
    return QMessageBox::question(this, QStringLiteral("确认删除"), msg,
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::Yes) == QMessageBox::Yes;
}

void ExplorerWindow::renameSelected() {
    const QModelIndexList sel = fileView_->selectionModel()->selectedIndexes();
    if (sel.size() != 1) {
        return;
    }
    fileView_->edit(sel.first());
}

void ExplorerWindow::newFolder() {
    const QString created = FileOps::makeDir(currentDir_);
    if (created.isEmpty()) {
        statusLabel_->setText(QStringLiteral("新建文件夹失败"));
        return;
    }
    refresh();
    // 定位到新文件夹并进入编辑态（Windows 行为）。
    const QModelIndex idx = model_->index(created);
    fileView_->setCurrentIndex(idx);
    fileView_->edit(idx);
}

void ExplorerWindow::showProperties() {
    const QModelIndexList sel = fileView_->selectionModel()->selectedIndexes();
    if (sel.size() != 1) {
        return;
    }
    const QFileInfo info(model_->filePath(sel.first()));
    const qint64 size = info.isDir()
        ? FileOps::totalSize({info.absoluteFilePath()})
        : info.size();
    const QString text = QStringLiteral(
        "名称: %1\n类型: %2\n位置: %3\n大小: %4\n修改时间: %5\n")
        .arg(info.fileName())
        .arg(info.isDir() ? QStringLiteral("文件夹") : info.suffix())
        .arg(info.absolutePath())
        .arg(size > 0 ? QStringLiteral("%1 字节").arg(size) : QStringLiteral("—"))
        .arg(info.lastModified().toString(Qt::ISODate));
    QMessageBox::information(this, QStringLiteral("属性"), text);
}

void ExplorerWindow::showContextMenu(const QPoint& pos) {
    const QModelIndexList sel = fileView_->selectionModel()->selectedIndexes();
    const bool hasSel = !sel.isEmpty();
    QMenu menu(this);
    QAction* openAct = menu.addAction(QStringLiteral("打开"));
    menu.addSeparator();
    QAction* cutAct = menu.addAction(QStringLiteral("剪切"));
    QAction* copyAct = menu.addAction(QStringLiteral("复制"));
    QAction* pasteAct = menu.addAction(QStringLiteral("粘贴"));
    pasteAct->setEnabled(!FileOps::clipboardPaths().isEmpty());
    QAction* renameAct = menu.addAction(QStringLiteral("重命名"));
    QAction* delAct = menu.addAction(QStringLiteral("删除"));
    menu.addSeparator();
    QAction* newFolderAct = menu.addAction(QStringLiteral("新建文件夹"));
    QAction* propsAct = menu.addAction(QStringLiteral("属性"));
    openAct->setEnabled(hasSel);
    cutAct->setEnabled(hasSel);
    copyAct->setEnabled(hasSel);
    renameAct->setEnabled(sel.size() == 1);
    delAct->setEnabled(hasSel);
    propsAct->setEnabled(sel.size() == 1);

    QAction* chosen = menu.exec(fileView_->viewport()->mapToGlobal(pos));
    if (chosen == openAct) {
        openSelected();
    } else if (chosen == cutAct) {
        copySelected(PasteMode::Move);
    } else if (chosen == copyAct) {
        copySelected(PasteMode::Copy);
    } else if (chosen == pasteAct) {
        paste();
    } else if (chosen == renameAct) {
        renameSelected();
    } else if (chosen == delAct) {
        deleteSelected();
    } else if (chosen == newFolderAct) {
        newFolder();
    } else if (chosen == propsAct) {
        showProperties();
    }
}

void ExplorerWindow::updateStatus() {
    const QModelIndexList sel = fileView_->selectionModel()->selectedIndexes();
    if (sel.isEmpty()) {
        const int n = model_->rowCount(fileView_->rootIndex());
        statusLabel_->setText(QStringLiteral("%1 个项目").arg(n));
        return;
    }
    QStringList paths;
    for (const QModelIndex& idx : sel) {
        paths.append(model_->filePath(idx));
    }
    const qint64 size = FileOps::totalSize(paths);
    statusLabel_->setText(QStringLiteral("已选择 %1 项（%2 字节）")
        .arg(paths.size()).arg(size));
}

void ExplorerWindow::updateNavState() {
    backBtn_->setEnabled(!backStack_.isEmpty());
    fwdBtn_->setEnabled(!fwdStack_.isEmpty());
}

void ExplorerWindow::closeEvent(QCloseEvent* e) {
    QMainWindow::closeEvent(e);
}

bool ExplorerWindow::eventFilter(QObject* /*obj*/, QEvent* /*ev*/) {
    return false;
}

}  // namespace w10de::explorer
