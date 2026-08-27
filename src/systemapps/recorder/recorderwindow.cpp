// RecorderWindow 实现（可选拓展 E6 录音机）。
//
// 布局（垂直）：
//   状态文字（大）/计时（更大）
//   大圆录音按钮（空闲=白底红点 → 点击开始；录音中=红底白方块 → 停止）
//   电平条（仅录音中可见）
//   提示文字
//   历史录音列表 + 底部按钮行（播放/停止/删除/刷新）

#include "systemapps/recorder/recorderwindow.h"

#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace w10de::recorder {

namespace {
constexpr int kBigButton = 110;  // 大圆按钮直径（px）
}  // namespace

RecorderWindow::RecorderWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("录音机"));
    setFixedSize(420, 600);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 28, 24, 20);
    root->setSpacing(10);

    // 状态 + 计时。
    statusLabel_ = new QLabel(QStringLiteral("点击录音按钮开始录音"), this);
    statusLabel_->setAlignment(Qt::AlignCenter);
    statusLabel_->setStyleSheet(QStringLiteral(
        "color:#9AA0A6; font-size:16px;"));
    root->addWidget(statusLabel_);

    timerLabel_ = new QLabel(QStringLiteral("00:00"), this);
    timerLabel_->setAlignment(Qt::AlignCenter);
    timerLabel_->setStyleSheet(QStringLiteral(
        "color:#FFFFFF; font-size:44px; font-weight:bold;"));
    root->addWidget(timerLabel_);

    // 大圆录音按钮。
    recordButton_ = new QPushButton(this);
    recordButton_->setFixedSize(kBigButton, kBigButton);
    recordButton_->setCursor(Qt::PointingHandCursor);
    recordButton_->setFocusPolicy(Qt::NoFocus);
    connect(recordButton_, &QPushButton::clicked,
            this, &RecorderWindow::onRecordButton);
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(recordButton_);
    btnRow->addStretch();
    root->addLayout(btnRow);

    // 电平条（默认隐藏）。
    levelBar_ = new QProgressBar(this);
    levelBar_->setRange(0, 100);
    levelBar_->setTextVisible(false);
    levelBar_->setFixedHeight(6);
    levelBar_->setStyleSheet(QStringLiteral(
        "QProgressBar{background:#444;border-radius:3px;}"
        "QProgressBar::chunk{background:#3DDC84;border-radius:3px;}"));
    levelBar_->hide();
    root->addWidget(levelBar_);

    hintLabel_ = new QLabel(this);
    hintLabel_->setAlignment(Qt::AlignCenter);
    hintLabel_->setWordWrap(true);
    root->addWidget(hintLabel_);

    // 历史录音列表。
    list_ = new QListWidget(this);
    list_->setStyleSheet(QStringLiteral(
        "QListWidget{background:#262626;color:#E0E0E0;border:1px solid #3A3A3A;"
        "border-radius:6px;font-size:13px;}"
        "QListWidget::item{padding:8px;}"
        "QListWidget::item:selected{background:#3D6FB4;}"));
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] {
        onPlaySelected();
    });
    root->addWidget(list_, 1);

    // 底部按钮行。
    playButton_ = new QPushButton(QStringLiteral("播放"), this);
    stopButton_ = new QPushButton(QStringLiteral("停止播放"), this);
    deleteButton_ = new QPushButton(QStringLiteral("删除"), this);
    auto* refreshButton = new QPushButton(QStringLiteral("刷新"), this);
    for (QPushButton* b : {playButton_, stopButton_, deleteButton_,
                           refreshButton}) {
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet(QStringLiteral(
            "QPushButton{background:#3D3D3D;color:#E0E0E0;border:none;"
            "border-radius:4px;padding:7px 14px;}"
            "QPushButton:hover{background:#4A4A4A;}"
            "QPushButton:disabled{color:#777;}"));
    }
    connect(playButton_, &QPushButton::clicked,
            this, &RecorderWindow::onPlaySelected);
    connect(stopButton_, &QPushButton::clicked,
            this, &RecorderWindow::onStopPlayback);
    connect(deleteButton_, &QPushButton::clicked,
            this, &RecorderWindow::onDeleteSelected);
    connect(refreshButton, &QPushButton::clicked,
            this, &RecorderWindow::onRefreshList);
    auto* btnRow2 = new QHBoxLayout;
    btnRow2->addWidget(playButton_);
    btnRow2->addWidget(stopButton_);
    btnRow2->addWidget(deleteButton_);
    btnRow2->addStretch();
    btnRow2->addWidget(refreshButton);
    root->addLayout(btnRow2);

    // 引擎信号。
    connect(&engine_, &RecorderEngine::availableChanged, this, [this](bool ok) {
        if (!ok) setStatus(QStringLiteral("音频服务不可用"), false);
        updateUiState();
    });
    connect(&engine_, &RecorderEngine::recordingStarted, this, [this] {
        recordStartMs_ = QDateTime::currentMSecsSinceEpoch();
        timerLabel_->setText(QStringLiteral("00:00"));
        timerTick_->start();
        levelBar_->setValue(0);
        levelBar_->show();
        setStatus(QStringLiteral("正在录音…"), true);
        updateUiState();
    });
    connect(&engine_, &RecorderEngine::recordingSaved, this,
            [this](const QString& path, qint64 durationMs, int) {
        timerTick_->stop();
        levelBar_->hide();
        timerLabel_->setText(formatDuration(durationMs));
        setStatus(QStringLiteral("已保存录音"), true);
        hintLabel_->setText(QStringLiteral("已保存：%1")
            .arg(QFileInfo(path).fileName()));
        refreshList();
        updateUiState();
    });
    connect(&engine_, &RecorderEngine::levelChanged, this, [this](int pct) {
        lastLevel_ = pct;
        levelBar_->setValue(pct);
    });
    connect(&engine_, &RecorderEngine::playbackStarted, this, [this] {
        setStatus(QStringLiteral("正在播放…"), true);
        updateUiState();
    });
    connect(&engine_, &RecorderEngine::playbackFinished, this, [this] {
        setStatus(QStringLiteral("点击录音按钮开始录音"), false);
        updateUiState();
    });
    connect(&engine_, &RecorderEngine::error, this,
            [this](const QString& msg) {
        setStatus(msg, false);
        hintLabel_->setText(msg);
    });

    // 录音计时刷新。
    timerTick_ = new QTimer(this);
    timerTick_->setInterval(1000);
    connect(timerTick_, &QTimer::timeout, this, [this] {
        const qint64 elapsed =
            QDateTime::currentMSecsSinceEpoch() - recordStartMs_;
        timerLabel_->setText(formatDuration(elapsed));
    });

    setStyleSheet(QStringLiteral("QWidget{background:#2D2D2D;}"));
    refreshList();
    updateUiState();
    engine_.ensureContext();
}

QString RecorderWindow::stateText() const {
    if (engine_.isRecording()) return QStringLiteral("recording");
    if (engine_.isPlaying()) return QStringLiteral("playing");
    return QStringLiteral("idle");
}

void RecorderWindow::onRecordButton() {
    if (engine_.isRecording()) {
        engine_.stopRecording();
        return;
    }
    const QString dir = recordingsDir();
    QDir().mkpath(dir);
    engine_.startRecording(
        QDir(dir).absoluteFilePath(newRecordingName(QDateTime::currentDateTime())));
}

void RecorderWindow::onPlaySelected() {
    // 审查 M4：录音中禁止播放（双击列表路径可能绕过按钮禁用）。
    if (engine_.isRecording()) return;
    const QString path = selectedPath();
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatus(QStringLiteral("无法读取录音文件"), false);
        return;
    }
    const QByteArray wav = f.readAll();
    f.close();
    engine_.playWav(wav);
}

void RecorderWindow::onStopPlayback() {
    engine_.stopPlayback();
}

void RecorderWindow::onDeleteSelected() {
    const QString path = selectedPath();
    if (path.isEmpty()) return;
    const auto ret = QMessageBox::question(
        this, QStringLiteral("删除录音"),
        QStringLiteral("确定删除「%1」吗？").arg(QFileInfo(path).fileName()));
    if (ret != QMessageBox::Yes) return;
    if (engine_.isPlaying()) engine_.stopPlayback();
    QFile::remove(path);
    refreshList();
}

void RecorderWindow::onRefreshList() {
    refreshList();
}

void RecorderWindow::refreshList() {
    list_->clear();
    const QList<RecordingItem> items = scanRecordings(recordingsDir());
    for (const RecordingItem& it : items) {
        const QString text = QStringLiteral("%1    %2    %3")
            .arg(it.name,
                 formatDuration(it.durationMs),
                 it.sizeBytes >= 1024 * 1024
                     ? QStringLiteral("%1 MB")
                           .arg(it.sizeBytes / 1024.0 / 1024.0, 0, 'f', 1)
                     : QStringLiteral("%1 KB")
                           .arg(qMax<qint64>(1, it.sizeBytes / 1024)));
        auto* item = new QListWidgetItem(text, list_);
        item->setData(Qt::UserRole, it.path);
        item->setToolTip(it.modified.toString(
            QStringLiteral("yyyy-MM-dd HH:mm")));
    }
    if (list_->count() == 0) {
        hintLabel_->setText(QStringLiteral(
            "暂无录音。点击上方按钮开始录音。\n录音保存在 %1")
            .arg(recordingsDir()));
    } else {
        hintLabel_->clear();
    }
    updateUiState();
}

void RecorderWindow::updateUiState() {
    const bool recording = engine_.isRecording();
    const bool playing = engine_.isPlaying();
    const bool available = engine_.isAvailable();
    // 大圆按钮样式：空闲=白底红点；录音中=红底白方块。
    // 按钮恒可用（无音频服务时点击会提示，服务恢复后自动重连）。
    recordButton_->setStyleSheet(recording
        ? QStringLiteral(
              "QPushButton{background:#C42B1C;color:#FFFFFF;"
              "border-radius:%1px;}"
              "QPushButton:hover{background:#D0382A;}")
              .arg(kBigButton / 2)
        : QStringLiteral(
              "QPushButton{background:#F5F5F5;color:#C42B1C;"
              "border-radius:%1px;}"
              "QPushButton:hover{background:#FFFFFF;}"
              "QPushButton:pressed{background:#E8E8E8;}")
              .arg(kBigButton / 2));
    recordButton_->setText(recording ? QStringLiteral("■")
                                     : QStringLiteral("●"));
    recordButton_->setEnabled(true);
    const bool hasSelection = list_->currentItem() != nullptr;
    playButton_->setEnabled(available && !recording && hasSelection
                            && !playing);
    stopButton_->setEnabled(playing);
    deleteButton_->setEnabled(hasSelection && !recording);
    if (recording) {
        timerLabel_->show();
        levelBar_->show();
    } else if (!playing) {
        levelBar_->hide();
        timerLabel_->show();
    }
}

void RecorderWindow::setStatus(const QString& text, bool bright) {
    statusLabel_->setText(text);
    statusLabel_->setStyleSheet(bright
        ? QStringLiteral("color:#FFFFFF; font-size:16px;")
        : QStringLiteral("color:#9AA0A6; font-size:16px;"));
}

QString RecorderWindow::selectedPath() const {
    const auto* item = list_->currentItem();
    return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
}

}  // namespace w10de::recorder
