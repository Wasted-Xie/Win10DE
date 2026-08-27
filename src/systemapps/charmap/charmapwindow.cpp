// w10charmap 字符映射表实现。
#include "systemapps/charmap/charmapwindow.h"

#include <QClipboard>
#include <QComboBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QVBoxLayout>

#include "theme/colors.h"

namespace w10charmap {

const QList<Block>& blocks() {
    static const QList<Block> kBlocks = {
        {0x0000, 0x00FF, "基本拉丁 / 拉丁补充"},
        {0x0100, 0x024F, "拉丁扩展"},
        {0x0370, 0x03FF, "希腊文"},
        {0x0400, 0x04FF, "西里尔文"},
        {0x2000, 0x206F, "常用标点"},
        {0x2190, 0x21FF, "箭头"},
        {0x2500, 0x257F, "制表符"},
        {0x25A0, 0x25FF, "几何图形"},
        {0x2600, 0x26FF, "杂项符号"},
        {0x3000, 0x303F, "中日韩符号和标点"},
        {0x4E00, 0x9FFF, "中日韩统一表意文字（CJK）"},
        {0xFF00, 0xFFEF, "半角及全角字符"},
    };
    return kBlocks;
}

QString codepointToChar(int cp) {
    // 代理区（0xD800-0xDFFF）与越界：返回空。
    if (cp < 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        return QString();
    }
    const char32_t u = static_cast<char32_t>(cp);
    return QString::fromUcs4(&u, 1);
}

int charToCodepoint(const QString& ch) {
    if (ch.isEmpty()) {
        return -1;
    }
    // 审查（E4）：toUcs4 首项返回完整码点（非 BMP 字符如 😀 返回
    // U+1F600，而非 ch.at(0).unicode() 的高位代理单元）。
    const QList<uint> ucs4 = ch.toUcs4();
    return ucs4.isEmpty() ? -1 : static_cast<int>(ucs4.first());
}

CharmapWindow::CharmapWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("字符映射表"));
    resize(720, 460);
    buildUi();
    loadBlock(0);
}

void CharmapWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* lay = new QVBoxLayout(central);

    auto* topRow = new QHBoxLayout;
    topRow->addWidget(new QLabel(QStringLiteral("Unicode 区块："), central));
    blockCombo_ = new QComboBox(central);
    const QList<Block>& blks = blocks();
    for (const Block& b : blks) {
        blockCombo_->addItem(QStringLiteral("%1（U+%2-U+%3）")
            .arg(QString::fromUtf8(b.name))
            .arg(b.start, 4, 16, QLatin1Char('0'))
            .arg(b.end, 4, 16, QLatin1Char('0')));
    }
    topRow->addWidget(blockCombo_, 1);
    lay->addLayout(topRow);
    connect(blockCombo_, &QComboBox::currentIndexChanged, this,
            &CharmapWindow::loadBlock);

    table_ = new QTableWidget(central);
    table_->setColumnCount(16);
    QStringList headers;
    for (int i = 0; i < 16; ++i) {
        headers << QString::number(i, 16).toUpper();
    }
    table_->setHorizontalHeaderLabels(headers);
    table_->verticalHeader()->setDefaultSectionSize(22);
    table_->horizontalHeader()->setDefaultSectionSize(34);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    lay->addWidget(table_, 1);
    connect(table_, &QTableWidget::cellClicked, this,
            &CharmapWindow::onCellSelected);

    auto* bottomRow = new QHBoxLayout;
    detailLabel_ = new QLabel(central);
    detailLabel_->setStyleSheet(QStringLiteral("color: %1;")
        .arg(w10de::theme::kTextSecondary().name()));
    bottomRow->addWidget(detailLabel_, 1);
    auto* copyBtn = new QPushButton(QStringLiteral("复制"), central);
    bottomRow->addWidget(copyBtn);
    lay->addLayout(bottomRow);
    setCentralWidget(central);
    connect(copyBtn, &QPushButton::clicked, this, [this] {
        const QList<QTableWidgetItem*> sel = table_->selectedItems();
        // 审查（E4）：复制排除占位字符"·"。
        if (!sel.isEmpty() && !sel.first()->text().isEmpty()
                && sel.first()->text() != QStringLiteral("·")) {
            QGuiApplication::clipboard()->setText(sel.first()->text());
            statusBar()->showMessage(
                QStringLiteral("已复制：%1").arg(sel.first()->text()), 2000);
        }
    });
}

void CharmapWindow::loadBlock(int index) {
    const QList<Block>& blks = blocks();
    if (index < 0 || index >= blks.size()) {
        return;
    }
    blockStart_ = blks.at(index).start;
    const int count = blks.at(index).end - blks.at(index).start + 1;
    const int rows = (count + 15) / 16;
    table_->setRowCount(rows);
    for (int row = 0; row < rows; ++row) {
        table_->setVerticalHeaderItem(row, new QTableWidgetItem(
            QStringLiteral("%1").arg(blockStart_ + row * 16, 4, 16,
                                    QLatin1Char('0')).toUpper()));
        for (int col = 0; col < 16; ++col) {
            const int cp = blockStart_ + row * 16 + col;
            QString text;
            if (cp <= blks.at(index).end) {
                text = codepointToChar(cp);
                if (text.isEmpty()) {
                    text = QStringLiteral("·");  // 代理区等占位
                }
            }
            auto* item = new QTableWidgetItem(text);
            item->setTextAlignment(Qt::AlignCenter);
            table_->setItem(row, col, item);
        }
    }
    table_->clearSelection();
    detailLabel_->setText(QStringLiteral("点击字符查看码点，再点【复制】"));
}

void CharmapWindow::onCellSelected(int row, int col) {
    const QTableWidgetItem* item = table_->item(row, col);
    if (item == nullptr) {
        return;
    }
    const QString text = item->text();
    if (text.isEmpty() || text == QStringLiteral("·")) {
        detailLabel_->setText(QStringLiteral("（空）"));
        return;
    }
    const int cp = blockStart_ + row * 16 + col;
    const QString hex = QStringLiteral("U+%1").arg(cp, 4, 16,
                                                  QLatin1Char('0')).toUpper();
    QByteArray utf8 = text.toUtf8();
    QStringList bytes;
    for (int i = 0; i < utf8.size(); ++i) {
        bytes << QStringLiteral("%1")
                     .arg(static_cast<unsigned char>(utf8.at(i)), 2, 16,
                          QLatin1Char('0')).toUpper();
    }
    detailLabel_->setText(QStringLiteral("%1  UTF-8: %2")
        .arg(hex, bytes.join(QStringLiteral(" "))));
}

}  // namespace w10charmap
