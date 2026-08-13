/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "openpilot/selfdrive/ui/sunnypilot/qt/onroad/annotated_camera.h"

#include <QPainter>

#include "openpilot/selfdrive/ui/qt/util.h"

AnnotatedCameraWidgetSP::AnnotatedCameraWidgetSP(VisionStreamType type, QWidget *parent)
    : AnnotatedCameraWidget(type, parent) {
  // Bottom fade gradient used by the 2026 raylib UI (icons_mici/onroad/onroad_fade.png).
  // Loaded unscaled; it is stretched to the camera rect at draw time.
  fade_img = loadPixmap("../assets/icons_mici/onroad/onroad_fade.png");
}

void AnnotatedCameraWidgetSP::drawFadeOverlay(QPainter &p, const QRect &surface_rect) {
  // Port of AugmentedRoadViewSP.update_fade_out_bottom_overlay(): fades out the
  // bottom of the overlays whenever we're not disengaged, so the steering arc and
  // bottom HUD stay readable. Only shown when the steering arc is enabled.
  const float target = (uiState()->status != STATUS_DISENGAGED) ? 1.0f : 0.0f;
  const float fade_alpha = fade_alpha_filter.update(target);

  if (!uiStateSP()->scene.torque_bar || fade_alpha <= 1e-2f || fade_img.isNull()) {
    return;
  }

  p.save();
  p.setOpacity(fade_alpha);
  p.setRenderHint(QPainter::SmoothPixmapTransform);
  p.drawPixmap(surface_rect, fade_img);
  p.restore();
}

void AnnotatedCameraWidgetSP::updateState(const UIState &s) {
  AnnotatedCameraWidget::updateState(s);
}

void AnnotatedCameraWidgetSP::showEvent(QShowEvent *event) {
  AnnotatedCameraWidget::showEvent(event);
  ui_update_params_sp(uiState());
  uiStateSP()->reset_onroad_sleep_timer(OnroadTimerStatusToggle::RESUME);
}

void AnnotatedCameraWidgetSP::hideEvent(QHideEvent *event) {
  AnnotatedCameraWidget::hideEvent(event);
  uiStateSP()->reset_onroad_sleep_timer(OnroadTimerStatusToggle::PAUSE);
}
