#pragma once

#include <string>

#include <QApplication>
#include <QScreen>
#include <QWidget>

#ifdef QCOM2
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client-protocol.h>
#include <QPlatformSurfaceEvent>
#endif

#include "system/hardware/hw.h"

class QDialog;

const QString ASSET_PATH = ":/";
const QSize DEVICE_SCREEN_SIZE = {2160, 1080};

void setMainWindow(QWidget *w);

// PC: overlay dialog on the main ScaleHost canvas and block until closed.
int execPcDialog(QDialog *dialog);
