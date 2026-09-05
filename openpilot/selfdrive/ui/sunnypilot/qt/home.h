/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once

#include <QFrame>
#include <QWidget>

#include "openpilot/common/params.h"
#include "openpilot/selfdrive/ui/qt/body.h"
#include "openpilot/selfdrive/ui/qt/widgets/offroad_alerts.h"
#include "openpilot/selfdrive/ui/sunnypilot/ui.h"
#include "openpilot/selfdrive/ui/qt/home.h"

#ifdef SUNNYPILOT
#include "openpilot/selfdrive/ui/sunnypilot/qt/sidebar.h"
#define OnroadWindow OnroadWindowSP
#else
#include "openpilot/selfdrive/ui/qt/sidebar.h"
#include "openpilot/selfdrive/ui/qt/onroad/onroad_home.h"
#endif

class HomeWindowSP : public HomeWindow {
  Q_OBJECT

public:
  explicit HomeWindowSP(QWidget *parent = 0);

protected:
  void mousePressEvent(QMouseEvent *e) override;

private slots:
  void updateState(const UIState &s) override;
};
