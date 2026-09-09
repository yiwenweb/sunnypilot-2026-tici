/**
 * Copyright (c) 2026, yiwenweb, sunnypilot BYD adaptation.
 *
 * Lane-center correction + auto camera-offset status HUD (Qt port of the
 * raylib LaneCenterStatusRenderer in selfdrive/ui/sunnypilot/onroad/lane_center_status.py).
 */

#pragma once

#include <QPainter>

#include "openpilot/common/params.h"
#include "openpilot/selfdrive/ui/ui.h"

class LaneCenterStatus {
public:
  LaneCenterStatus() = default;
  void updateState(const UIState &s);
  void draw(QPainter &painter, const QRect &surface_rect);

private:
  Params params;
  double params_t = 0.0;

  bool lc_enabled = false;
  bool aco_enabled = false;
  float cam_offset = 0.0f;
  float learned = 0.0f;
  float offset = 0.0f;
  bool lines_ok = false;
  bool correcting = false;
  bool learning = false;
  bool is_rhd = false;
};
