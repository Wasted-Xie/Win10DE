// RecorderWindow —— 录音机主窗口（可选拓展 E6，Win10 录音机风格）。
//
// 深色布局：中央大圆录音按钮（空闲=白底红点；录音中=红底白方块）、
// 计时显示、录音电平条、底部历史录音列表（双击播放/播放停止/删除）。
// 引擎不可用（无 PulseAudio）时按钮禁用并显示提示。

#pragma once

#include <QWidget>

#include "systemapps/recorder/recorder.h"

class QLabel;
class QPushButton;
class QListWidget;
class QProgressBar;

namespace w10de::recorder {

class RecorderWindow : public QWidget {
    Q_OBJECT
public:
    explicit RecorderWindow(QWidget* parent = nullptr);

    // 供验证：当前 UI 状态（"idle"/"recording"/"playing"）。
    QString stateText() const;

private slots:
    void onRecordButton();
    void onPlaySelected();
    void onStopPlayback();
    void onDeleteSelected();
    void onRefreshList();

private:
    void refreshList();
    void updateUiState();
    void setStatus(const QString& text, bool bright);
    QString selectedPath() const;

    RecorderEngine engine_;
    QLabel* statusLabel_ = nullptr;
    QLabel* timerLabel_ = nullptr;
    QPushButton* recordButton_ = nullptr;
    QProgressBar* levelBar_ = nullptr;
    QListWidget* list_ = nullptr;
    QPushButton* playButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QPushButton* deleteButton_ = nullptr;
    QLabel* hintLabel_ = nullptr;
    QTimer* timerTick_ = nullptr;
    qint64 recordStartMs_ = 0;
    int lastLevel_ = 0;
};

}  // namespace w10de::recorder
