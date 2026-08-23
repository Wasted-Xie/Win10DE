// Win10 风格磁贴按钮：支持四种尺寸（小/中/大/宽），右键菜单自由设置。
#pragma once

#include <QToolButton>

namespace w10de {

class TileButton : public QToolButton {
    Q_OBJECT
public:
    enum class TileSize {
        Small,   // 48×48  （1×1 按钮宽）
        Medium,  // 96×96  （2×2）
        Large,   // 192×192（4×4）
        Wide,    // 192×96 （4×2：宽 4×按钮宽，高 2×按钮宽）
    };

    explicit TileButton(const QString& name, const QString& iconName,
                        const QString& exec, QWidget* parent = nullptr);

    // 自由设置磁贴尺寸（右键菜单/外部调用），触发重排。
    void setTileSize(TileSize size);
    TileSize tileSize() const { return size_; }
    QSize tileSizeHint() const;  // 依当前尺寸
    // 布局使用固定磁贴尺寸（QToolButton 默认 sizeHint 按内容计算，
    // 会撑破流式布局的网格）。
    QSize sizeHint() const override { return tileSizeHint(); }
    QString exec() const { return exec_; }

signals:
    void launchRequested(const QString& exec);
    void sizeChanged();  // 尺寸变更后请宿主重排

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void applySize();

    QString name_;
    QString icon_;
    QString exec_;
    TileSize size_ = TileSize::Small;
};

}  // namespace w10de
