/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once

#include "openpilot/selfdrive/ui/sunnypilot/qt/widgets/controls.h"

// Read-only row that shows a title + a value (with optional color), used to
// display device calibration/mounting info. Reuses AbstractControlSP's `value`
// label which supports setValue(value, color).
class ValueRowSP : public AbstractControlSP {
  Q_OBJECT

public:
  explicit ValueRowSP(const QString &title, const QString &desc = "", QWidget *parent = nullptr)
      : AbstractControlSP(title, desc, "", parent, false) {}
};

class ServicePanelSP : public QFrame {
  Q_OBJECT

public:
  explicit ServicePanelSP(QWidget *parent = nullptr);
  void showEvent(QShowEvent *event) override;

private:
  void refresh();

  Params params;

  ValueRowSP *mountingPitch = nullptr;
  ValueRowSP *mountingYaw = nullptr;
  ValueRowSP *mountingHeight = nullptr;
  ValueRowSP *calibrationStatus = nullptr;
  ValueRowSP *calibrationProgress = nullptr;
};
