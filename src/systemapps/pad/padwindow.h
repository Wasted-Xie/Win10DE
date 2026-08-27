// w10pad —— 写字板（可选拓展 E3：QTextEdit 富文本打开/保存）。
//
// 打开 .txt（纯文本）/ .html（富文本）；保存为 .html（富文本）/ .txt
// （纯文本）。工具栏：粗体/斜体/下划线 + 字号 + 对齐。
#pragma once

#include <QMainWindow>

class QComboBox;
class QCloseEvent;
class QTextCharFormat;
class QTextEdit;

namespace w10pad {

class PadWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit PadWindow(const QString& openPath = QString(),
                       QWidget* parent = nullptr);

protected:
    // 审查（E3）：编辑未保存时关闭确认。
    void closeEvent(QCloseEvent* e) override;

private:
    void buildUi();
    bool openFile(const QString& path);
    void saveFile();
    void mergeFormat(const QTextCharFormat& fmt);

    QTextEdit* edit_ = nullptr;
    QString currentPath_;
    bool changed_ = false;
};

}  // namespace w10pad
