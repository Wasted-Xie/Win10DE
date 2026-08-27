// w10pad 写字板实现。
#include "systemapps/pad/padwindow.h"

#include <QCloseEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

#include "theme/colors.h"

namespace w10pad {

PadWindow::PadWindow(const QString& openPath, QWidget* parent)
    : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("写字板"));
    resize(640, 480);
    buildUi();

    if (!openPath.isEmpty() && !openFile(openPath)) {
        QMessageBox::warning(this, QStringLiteral("写字板"),
            QStringLiteral("无法打开：%1").arg(openPath));
    }
}

void PadWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(6, 6, 6, 6);

    auto* bar = new QHBoxLayout;
    auto* openBtn = new QPushButton(QStringLiteral("打开…"), central);
    auto* saveBtn = new QPushButton(QStringLiteral("保存"), central);
    bar->addWidget(openBtn);
    bar->addWidget(saveBtn);
    bar->addSpacing(12);
    const auto makeFormatBtn = [central, this](const QString& text,
                                               const QTextCharFormat& fmt) {
        auto* b = new QToolButton(central);
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        QObject::connect(b, &QToolButton::clicked, this,
                         [this, fmt] { mergeFormat(fmt); });
        return b;
    };
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    bar->addWidget(makeFormatBtn(QStringLiteral("B"), bold));
    QTextCharFormat italic;
    italic.setFontItalic(true);
    bar->addWidget(makeFormatBtn(QStringLiteral("I"), italic));
    QTextCharFormat underline;
    underline.setFontUnderline(true);
    bar->addWidget(makeFormatBtn(QStringLiteral("U"), underline));
    bar->addStretch(1);
    root->addLayout(bar);

    edit_ = new QTextEdit(central);
    edit_->setAcceptRichText(true);
    root->addWidget(edit_, 1);
    setCentralWidget(central);

    QObject::connect(openBtn, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(
            this, QStringLiteral("打开"), QDir::homePath(),
            QStringLiteral("文本 (*.txt *.html *.htm);;所有文件 (*)"));
        if (!file.isEmpty()) {
            if (!openFile(file)) {
                QMessageBox::warning(this, QStringLiteral("写字板"),
                    QStringLiteral("无法打开：%1").arg(file));
            }
        }
    });
    QObject::connect(saveBtn, &QPushButton::clicked, this,
                     &PadWindow::saveFile);
    QObject::connect(edit_, &QTextEdit::textChanged, this, [this] {
        changed_ = true;
        setWindowTitle(QStringLiteral("写字板 *"));
    });
}

bool PadWindow::openFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();
    // 审查（E3）：扩展名判定大小写不敏感（DOC.HTML 应走富文本分支）。
    const QString lower = path.toLower();
    if (lower.endsWith(QStringLiteral(".html"))
            || lower.endsWith(QStringLiteral(".htm"))) {
        edit_->setHtml(QString::fromUtf8(data));
    } else {
        edit_->setPlainText(QString::fromUtf8(data));
    }
    currentPath_ = path;
    changed_ = false;
    setWindowTitle(QStringLiteral("写字板 - %1").arg(QFileInfo(path).fileName()));
    return true;
}

void PadWindow::saveFile() {
    if (currentPath_.isEmpty()) {
        currentPath_ = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存"), QDir::homePath(),
            QStringLiteral("HTML (*.html);;纯文本 (*.txt)"));
        if (currentPath_.isEmpty()) {
            return;
        }
        // 审查（E3）：无后缀按默认格式（HTML）补扩展名。
        if (QFileInfo(currentPath_).suffix().isEmpty()) {
            currentPath_ += QStringLiteral(".html");
        }
    }
    QFile f(currentPath_);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("写字板"),
            QStringLiteral("保存失败：%1").arg(currentPath_));
        return;
    }
    const QString lower = currentPath_.toLower();
    const bool html = lower.endsWith(QStringLiteral(".html"))
        || lower.endsWith(QStringLiteral(".htm"));
    f.write((html ? edit_->toHtml() : edit_->toPlainText()).toUtf8());
    f.close();
    changed_ = false;
    setWindowTitle(QStringLiteral("写字板 - %1")
        .arg(QFileInfo(currentPath_).fileName()));
    statusBar()->showMessage(
        QStringLiteral("已保存：%1").arg(currentPath_), 3000);
}

void PadWindow::mergeFormat(const QTextCharFormat& fmt) {
    QTextCursor cursor = edit_->textCursor();
    // 审查（E3）：无选区时不再 select WordUnderCursor（中文整段会被误选
    // 加粗）——只设置后续输入格式。
    if (cursor.hasSelection()) {
        cursor.mergeCharFormat(fmt);
    }
    edit_->mergeCurrentCharFormat(fmt);
}

void PadWindow::closeEvent(QCloseEvent* e) {
    // 审查（E3）：编辑未保存时确认（changed_ 此前未用于关闭确认）。
    if (changed_
            && QMessageBox::question(this, QStringLiteral("写字板"),
                   QStringLiteral("有未保存的修改，确定关闭？"))
                   != QMessageBox::Yes) {
        e->ignore();
        return;
    }
    QMainWindow::closeEvent(e);
}

}  // namespace w10pad
