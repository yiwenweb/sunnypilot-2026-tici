/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "openpilot/selfdrive/ui/sunnypilot/qt/onroad/annotated_camera.h"

AnnotatedCameraWidgetSP::AnnotatedCameraWidgetSP(VisionStreamType type, QWidget *parent)
    : AnnotatedCameraWidget(type, parent) {
}

void AnnotatedCameraWidgetSP::updateState(const UIState &s) {
  AnnotatedCameraWidget::updateState(s);

  // MADS border: visually distinguish LAT_ONLY / LONG_ONLY / ENGAGED / DISENGAGED
  const int border_width = 6;
  const UIStateSP *ss = uiStateSP();
  QColor bg, border;

  if (ss->status == STATUS_LAT_ONLY) {
    bg = QColor(0x00, 0x2a, 0x2a, 0xc8);
    border = bg_colors[STATUS_LAT_ONLY];
  } else if (ss->status == STATUS_LONG_ONLY) {
    bg = QColor(0x20, 0x08, 0x24, 0xc8);
    border = bg_colors[STATUS_LONG_ONLY];
  } else {
    bg = bg_colors[ss->status];
    border = (ss->status == STATUS_ENGAGED) ? bg_colors[STATUS_ENGAGED] : bg_colors[STATUS_DISENGAGED];
  }

  setBackgroundColor(bg);
  setStyleSheet(QString("border: %1px solid %2; background-color: %3")
    .arg(border_width)
    .arg(border.name())
    .arg(bg.name()));
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
