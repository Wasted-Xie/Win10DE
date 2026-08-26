// w10control 主窗口实现（Win10 控制面板按类别视图）。
#include "systemapps/control/controlwindow.h"
#include "systemapps/control/categorydialogs.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

#include "theme/colors.h"

namespace w10de::control {

namespace {

// 类别元数据（id/中文名/副标题/图标绘制）。
enum class Category {
    SystemSecurity,
    Appearance,
    HardwareSound,
    Network,
    Programs,
    ClockRegion,
};

struct CategoryEntry {
    const char* id;
    const char* title;
    const char* subtitle;
    Category category;
};

const CategoryEntry kCategories[] = {
    {"system", "系统和安全", "查看 Win10DE 信息、电源与开机自启", Category::SystemSecurity},
    {"appearance", "外观和个性化", "更改主题、壁纸与 Night Light", Category::Appearance},
    {"hardware", "硬件和声音", "显示、音频、蓝牙与输入设备", Category::HardwareSound},
    {"network", "网络和 Internet", "查看网络连接状态", Category::Network},
    {"programs", "程序", "默认应用与软件中心", Category::Programs},
    {"clock", "时钟和区域", "查看日期、时间与时区", Category::ClockRegion},
};

// 类别图标（48×48 自绘，Win10 控制面板几何风格）。
void paintCategoryIcon(Category category, QPainter* p, const QRect& r) {
    p->setRenderHint(QPainter::Antialiasing, true);
    const QColor stroke(200, 205, 215);
    const QColor fill(90, 130, 190);
    p->setPen(QPen(stroke, 2));
    p->setBrush(fill);
    switch (category) {
    case Category::SystemSecurity: {
        // 盾牌。
        QPainterPath path;
        path.moveTo(r.center().x(), r.top() + 4);
        path.lineTo(r.right() - 6, r.top() + 10);
        path.lineTo(r.right() - 6, r.center().y());
        path.cubicTo(r.right() - 6, r.bottom() - 12,
                     r.center().x(), r.bottom() - 4,
                     r.center().x(), r.bottom() - 4);
        path.cubicTo(r.center().x(), r.bottom() - 4,
                     r.left() + 6, r.bottom() - 12,
                     r.left() + 6, r.center().y());
        path.lineTo(r.left() + 6, r.top() + 10);
        path.closeSubpath();
        p->drawPath(path);
        break;
    }
    case Category::Appearance: {
        // 调色板：圆 + 色点。
        p->setBrush(QColor(235, 180, 90));
        p->drawEllipse(r.adjusted(6, 6, -14, -14));
        p->drawEllipse(r.adjusted(16, 6, -6, -14));
        p->setBrush(QColor(150, 190, 120));
        p->drawEllipse(r.adjusted(6, 16, -14, -6));
        p->setBrush(QColor(230, 120, 120));
        p->drawEllipse(r.adjusted(16, 16, -6, -6));
        p->setBrush(fill);
        p->drawEllipse(QPoint(r.center().x(), r.center().y()), 4, 4);
        break;
    }
    case Category::HardwareSound: {
        // 显示器。
        p->drawRoundedRect(r.adjusted(6, 6, -6, -16), 3, 3);
        p->drawLine(r.center().x() - 8, r.bottom() - 10,
                    r.center().x() + 8, r.bottom() - 10);
        p->drawLine(r.center().x(), r.bottom() - 10,
                    r.center().x(), r.bottom() - 4);
        break;
    }
    case Category::Network: {
        // 地球。
        p->setBrush(QColor(120, 170, 220));
        p->drawEllipse(r.adjusted(4, 4, -4, -4));
        p->setPen(QPen(stroke, 2));
        p->setBrush(Qt::NoBrush);
        p->drawEllipse(r.adjusted(10, 8, -10, -8));
        p->drawEllipse(r.adjusted(8, 4, -8, -4));
        p->drawLine(r.center().x(), r.top() + 4, r.center().x(), r.bottom() - 4);
        p->drawLine(r.left() + 4, r.center().y(), r.right() - 4, r.center().y());
        break;
    }
    case Category::Programs: {
        // 四宫格。
        const int cw = (r.width() - 8) / 2;
        const int ch = (r.height() - 8) / 2;
        for (int i = 0; i < 4; ++i) {
            const QRect cell(r.left() + 4 + (i % 2) * (cw + 4),
                             r.top() + 4 + (i / 2) * (ch + 4),
                             cw, ch);
            p->setBrush(i == 0 ? fill : QColor(230, 180, 90));
            p->drawRoundedRect(cell, 3, 3);
        }
        break;
    }
    case Category::ClockRegion: {
        // 时钟。
        p->drawEllipse(r.adjusted(4, 4, -4, -4));
        p->drawLine(r.center(), QPoint(r.center().x(), r.center().y() - 12));
        p->drawLine(r.center(), QPoint(r.center().x() + 9, r.center().y() + 4));
        p->setBrush(Qt::NoBrush);
        break;
    }
    }
}

QIcon makeCategoryIcon(Category category) {
    QPixmap pm(48, 48);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paintCategoryIcon(category, &p, QRect(0, 0, 48, 48));
    p.end();
    return QIcon(pm);
}

}  // namespace

ControlWindow::ControlWindow(QWidget* parent) : QMainWindow(parent) {
    buildUi();
    applyTheme();
    setWindowTitle(QStringLiteral("控制面板"));
    resize(760, 460);
}

void ControlWindow::buildUi() {
    auto* central = new QWidget(this);
    auto* lay = new QVBoxLayout(central);

    // 顶部：标题 + 搜索框（Win10 控制面板样式）。
    auto* headRow = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("控制面板"), central);
    title->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: bold;"));
    headRow->addWidget(title);
    headRow->addStretch(1);
    searchBox_ = new QLineEdit(central);
    searchBox_->setPlaceholderText(QStringLiteral("搜索控制面板…"));
    searchBox_->setFixedWidth(220);
    headRow->addWidget(searchBox_);
    lay->addLayout(headRow);
    connect(searchBox_, &QLineEdit::textChanged,
            this, &ControlWindow::onSearchChanged);

    // 类别图标网格（2 列）。
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(16);
    const int count = static_cast<int>(sizeof(kCategories) / sizeof(kCategories[0]));
    for (int i = 0; i < count; ++i) {
        auto* btn = new QToolButton(central);
        btn->setIcon(makeCategoryIcon(kCategories[i].category));
        btn->setIconSize(QSize(48, 48));
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setText(QStringLiteral("%1\n%2")
            .arg(QLatin1String(kCategories[i].title),
                 QLatin1String(kCategories[i].subtitle)));
        btn->setFixedSize(300, 96);
        btn->setAutoRaise(false);
        btn->setObjectName(QLatin1String(kCategories[i].id));
        btn->setCursor(Qt::PointingHandCursor);
        grid->addWidget(btn, i / 2, i % 2);
        categoryButtons_[i] = btn;
        connect(btn, &QToolButton::clicked, this, [this, id = QLatin1String(kCategories[i].id)] {
            openCategory(id);
        });
    }
    lay->addLayout(grid);
    lay->addStretch(1);
    setCentralWidget(central);
    statusBar()->showMessage(QStringLiteral("所有类别（%1 项）").arg(count));
}

void ControlWindow::applyTheme() {
    const QColor bg = theme::kStartMenuBackground();
    const QColor fg = theme::kTextPrimary();
    setStyleSheet(QStringLiteral(
        "QMainWindow, QWidget { background: %1; color: %2; }"
        "QToolButton { background: transparent; color: %2; border: 1px solid %3;"
        "  border-radius: 4px; padding: 8px; text-align: center; }"
        "QToolButton:hover { background: %4; }"
        "QToolButton:pressed { background: %5; }"
        "QLineEdit { background: %6; color: %2; border: 1px solid %3;"
        "  padding: 4px 8px; }"
        "QStatusBar { color: %2; }")
        .arg(bg.name(), fg.name(),
             theme::kHoverBackground().name(),
             theme::kPressedBackground().name(),
             theme::kTaskbarBackground().name(),
             theme::kTextSecondary().name()));
}

void ControlWindow::openCategory(const QString& id) {
    QDialog* dlg = nullptr;
    if (id == QLatin1String("system")) {
        dlg = new SystemSecurityDialog(this);
    } else if (id == QLatin1String("appearance")) {
        dlg = new AppearanceDialog(this);
    } else if (id == QLatin1String("hardware")) {
        dlg = new HardwareSoundDialog(this);
    } else if (id == QLatin1String("network")) {
        dlg = new NetworkDialog(this);
    } else if (id == QLatin1String("programs")) {
        dlg = new ProgramsDialog(this);
    } else if (id == QLatin1String("clock")) {
        dlg = new ClockRegionDialog(this);
    }
    if (dlg == nullptr) {
        return;
    }
    // 审查 M2（G1）：不用 new+WA_DeleteOnClose+exec() 组合（Qt 的 done()
    // 会同步 delete this，与 DeleteOnClose 叠加易悬垂/双删）——
    // 与 ruleeditdialog 同款模式：exec 后显式 delete。
    dlg->exec();  // 模态（传统控制面板对话框语义）
    delete dlg;
    statusBar()->showMessage(QStringLiteral("类别已关闭：%1").arg(id), 3000);
}

void ControlWindow::onSearchChanged(const QString& text) {
    const QString t = text.trimmed();
    const int count = static_cast<int>(sizeof(kCategories) / sizeof(kCategories[0]));
    for (int i = 0; i < count; ++i) {
        const bool visible = t.isEmpty()
            || QLatin1String(kCategories[i].title).contains(t, Qt::CaseInsensitive)
            || QLatin1String(kCategories[i].subtitle).contains(t, Qt::CaseInsensitive);
        categoryButtons_[i]->setVisible(visible);
    }
}

}  // namespace w10de::control
