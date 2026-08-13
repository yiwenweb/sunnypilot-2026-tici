/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once

#include <QPixmap>

#include "openpilot/common/util.h"
#include "openpilot/selfdrive/ui/qt/onroad/annotated_camera.h"

class AnnotatedCameraWidgetSP : public AnnotatedCameraWidget {
  Q_OBJECT

public:
  explicit AnnotatedCameraWidgetSP(VisionStreamType type, QWidget *parent = nullptr);
  void updateState(const UIState &s) override;

protected:
  void showEvent(QShowEvent *event) override;
  void hideEvent(QHideEvent* event) override;
  void drawFadeOverlay(QPainter &p, const QRect &surface_rect) override;

private:
  QPixmap fade_img;
  // Fade in/out when engaged. ts=0.1, dt=1/UI_FREQ matches
  // AugmentedRoadViewSP._fade_alpha_filter in the raylib UI.
  FirstOrderFilter fade_alpha_filter{0.0f, 0.1f, 1.0f / UI_FREQ};
};
