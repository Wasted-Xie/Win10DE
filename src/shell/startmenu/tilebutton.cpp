#include "startmenu/tilebutton.h"

#include <QContextMenuEvent>
#include <QIcon>
#include <QMenu>

#include "theme/colors.h"

namespace w10de {

namespace {

constexpr int kIconSmall = 24;
constexpr int kIconMedium = 36;
constexpr int kIconLarge = 48;

}  // namespace

TileButton::TileButton(const QString& name, const QString& iconName,
                       const QString& exec, QWidget* parent)
    : QToolButton(parent), name_(name), icon_(iconName), exec_(exec) {
    setText(name_);
    setToolTip(name_);
    setAutoRaise(true);
    setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  color: %1;"
        "  background: %2;"
        "  border: none;"
        "  border-radius: 2px;"
        "  font-size: 11px;"
        "}"
        "QToolButton:hover { background: %3; }"
        "QToolButton:pressed { background: %4; }")
        .arg(theme::kTextPrimary.name(),
             theme::kHoverBackground.name(),
             theme::kPressedBackground.name(),
             theme::kPressedBackground.name()));
    connect(this, &QToolButton::clicked,
            this, [this]() { emit launchRequested(exec_); });
    applySize();
}

QSize TileButton::tileSizeHint() const {
    // 网格基准：小磁贴 48px + 4px 间隔。
    // 中 = 2 小 + 1 间隔（100×100）；大 = 4 小 + 3 间隔（204×204）；
    // 宽 = 4 小 + 3 间隔 × 2 小 + 1 间隔（204×100）。
    switch (size_) {
    case TileSize::Small: return QSize(48, 48);
    case TileSize::Medium: return QSize(100, 100);
    case TileSize::Large: return QSize(204, 204);
    case TileSize::Wide: return QSize(204, 100);
    }
    return QSize(48, 48);
}

void TileButton::setTileSize(TileSize size) {
    if (size_ == size) {
        return;
    }
    size_ = size;
    applySize();
    emit sizeChanged();
}

void TileButton::applySize() {
    const QSize s = tileSizeHint();
    setFixedSize(s);
    const QIcon icon = QIcon::fromTheme(icon_);
    switch (size_) {
    case TileSize::Small:
        // 小磁贴仅图标（Win10 小磁贴）。
        setToolButtonStyle(Qt::ToolButtonIconOnly);
        setIcon(icon.pixmap(QSize(kIconSmall, kIconSmall)));
        setIconSize(QSize(kIconSmall, kIconSmall));
        break;
    case TileSize::Medium:
        setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        setIcon(icon.pixmap(QSize(kIconMedium, kIconMedium)));
        setIconSize(QSize(kIconMedium, kIconMedium));
        break;
    case TileSize::Large:
    case TileSize::Wide:
        setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        setIcon(icon.pixmap(QSize(kIconLarge, kIconLarge)));
        setIconSize(QSize(kIconLarge, kIconLarge));
        break;
    }
}

void TileButton::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: %1; color: %2; border: 1px solid %3; }"
        "QMenu::item { padding: 6px 16px; }"
        "QMenu::item:selected { background: %4; }")
        .arg(theme::kStartMenuBackground.name(),
             theme::kTextPrimary.name(),
             theme::kHoverBackground.name(),
             theme::kPressedBackground.name()));
    QAction* small = menu.addAction(QStringLiteral("小 (48×48)"));
    QAction* medium = menu.addAction(QStringLiteral("中 (100×100)"));
    QAction* large = menu.addAction(QStringLiteral("大 (204×204)"));
    QAction* wide = menu.addAction(QStringLiteral("宽 (204×100)"));
    QAction* chosen = menu.exec(event->globalPos());
    TileSize next = size_;
    if (chosen == small) {
        next = TileSize::Small;
    } else if (chosen == medium) {
        next = TileSize::Medium;
    } else if (chosen == large) {
        next = TileSize::Large;
    } else if (chosen == wide) {
        next = TileSize::Wide;
    }
    if (next != size_) {
        setTileSize(next);
    }
}

}  // namespace w10de
