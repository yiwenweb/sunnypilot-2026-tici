/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "openpilot/selfdrive/ui/sunnypilot/qt/onroad/buttons.h"

#include <QPainter>

ExperimentalButtonSP::ExperimentalButtonSP(QWidget *parent) : ExperimentalButton(parent) {
  QObject::disconnect(uiState(), &UIState::uiUpdate, this, &ExperimentalButton::updateState);
  QObject::connect(uiState(), &UIState::uiUpdate, this, &ExperimentalButtonSP::updateState);
}

void ExperimentalButtonSP::updateState(const UIState &s) {
  ExperimentalButton::updateState(s);
  const auto long_plan_sp = (*s.sm)["longitudinalPlanSP"].getLongitudinalPlanSP();

  int mode = int(long_plan_sp.getDec().getState());
  if ((long_plan_sp.getDec().getActive() != dynamic_experimental_control) || (mode != dec_mpc_mode)) {
    dynamic_experimental_control = long_plan_sp.getDec().getActive();
    dec_mpc_mode = mode;
    update();
  }
}

void ExperimentalButtonSP::drawButton(QPainter &p) {
  if (dynamic_experimental_control) {
    // Show the full experimental icon instead of splitting it in half
    // with one half dimmed, which looked like a half-shadowed icon
    drawIcon(p, QPoint(btn_size / 2, btn_size / 2), experimental_img, QColor(0, 0, 0, 166), (isDown() || !engageable) ? 0.6 : 1.0);
  } else {
    ExperimentalButton::drawButton(p);
  }
}
