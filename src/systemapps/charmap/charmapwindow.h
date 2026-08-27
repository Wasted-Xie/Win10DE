// w10charmap —— 字符映射表（可选拓展 E4：charmap，特殊字符插入）。
//
// 按 Unicode 区块浏览：表格每行 16 字符（列头 0-F）+ 区块下拉 + 点击字符
// 显示详情（码点/UTF-8/UTF-16）+ 复制到剪贴板。
// 纯逻辑：codepointToChar / charToCodepoint / blockRange（selftest 可测）。
#pragma once

#include <QMainWindow>

class QLabel;
class QTableWidget;
class QComboBox;

namespace w10charmap {

// Unicode 区块（起点/终点/中文名）。
struct Block {
    int start;
    int end;
    const char* name;
};
const QList<Block>& blocks();
// 码点 → 字符（非法/代理区返回空）。
QString codepointToChar(int cp);
// 字符 → 码点（非 BMP 返回 > 0xFFFF）。
int charToCodepoint(const QString& ch);

class CharmapWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit CharmapWindow(QWidget* parent = nullptr);

private:
    void buildUi();
    void loadBlock(int index);
    void onCellSelected(int row, int col);

    QComboBox* blockCombo_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* detailLabel_ = nullptr;
    int blockStart_ = 0;
};

}  // namespace w10charmap
