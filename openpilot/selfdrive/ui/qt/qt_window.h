#pragma once

#include <string>

#include <QApplication>
#include <QScreen>
#include <QWidget>

#if defined(QCOM2) || defined(__COMMA_HARDWARE__)
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client-protocol.h>
#include <QPlatformSurfaceEvent>
#endif

#include "openpilot/common/hardware/hw.h"

const QString ASSET_PATH = ":/";
const QSize DEVICE_SCREEN_SIZE = {2160, 1080};

void setMainWindow(QWidget *w);
