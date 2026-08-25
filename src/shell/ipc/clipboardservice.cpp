#include "shell/ipc/clipboardservice.h"

#include <QDebug>

namespace w10de {

ClipboardService::ClipboardService(QObject* parent) : QObject(parent) {}

void ClipboardService::ToggleClipboardHistory() {
    qInfo("clipboard service: Toggle invoked");
    emit toggleRequested();
}

}  // namespace w10de
