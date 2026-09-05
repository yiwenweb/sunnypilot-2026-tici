/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "openpilot/selfdrive/ui/sunnypilot/qt/widgets/toggle.h"

#include <QPainter>

ToggleSP::ToggleSP(QWidget *parent) : Toggle(parent) {
  _height_rect = 100;
}

void ToggleSP::paintEvent(QPaintEvent *e) {
  this->setFixedHeight(120);
  QPainter p(this);
  p.setPen(Qt::NoPen);
  p.setRenderHint(QPainter::Antialiasing, true);

  // Draw toggle background
  // Toggle ON: #1C65BA, OFF enabled: #393939, OFF disabled: #272727, disabled ON: #25466B
  // Knob matches raylib TOGGLE_KNOB_COLOR (white) / TOGGLE_DISABLED_KNOB_COLOR (88,88,88)
  enabled ? green.setRgb(0x1C65BA) : green.setRgb(0x25466B);
  enabled ? circleColor.setRgb(0xFFFFFF) : circleColor.setRgb(0x585858);
  p.setBrush(on ? green : (enabled ? QColor(0x393939) : QColor(0x272727)));
  p.drawRoundedRect(QRect(0, 10, width(), _height_rect), _height_rect / 2, _height_rect / 2);

  // Draw toggle circle
  p.setBrush(circleColor);
  p.drawEllipse(QRectF(_x_circle - _radius + 6, 26, 68, 68));
}
