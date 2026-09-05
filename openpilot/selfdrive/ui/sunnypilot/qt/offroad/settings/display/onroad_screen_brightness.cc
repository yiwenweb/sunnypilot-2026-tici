/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/display/onroad_screen_brightness.h"

OnroadScreenBrightnessControl::OnroadScreenBrightnessControl(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setSpacing(30);
  layout->setContentsMargins(0, 0, 0, 0);

  // Matches raylib display.py: two independent options, no toggle.
  onroadScreenOffBrightness = new OptionControlSP(
    "OnroadScreenOffBrightness",
    tr("Onroad Brightness"),
    "",
    "",
    {0, 22}, 1, true);

  onroadScreenOffTimer = new OptionControlSP(
    "OnroadScreenOffTimer",
    tr("Onroad Brightness Delay"),
    "",
    "",
    {0, 15}, 1, true, &onroadScreenOffTimerOptions);

  connect(onroadScreenOffBrightness, &OptionControlSP::updateLabels, this, &OnroadScreenBrightnessControl::refresh);
  connect(onroadScreenOffTimer, &OptionControlSP::updateLabels, this, &OnroadScreenBrightnessControl::refresh);

  layout->addWidget(onroadScreenOffBrightness);
  layout->addWidget(onroadScreenOffTimer);

  refresh();
}

void OnroadScreenBrightnessControl::refresh() {
  // Driving Screen Off Brightness — match raylib OnroadBrightness enum (0=Auto,1=AutoDark,2=Off,3..22=(val-2)*5%)
  const int valBrightness = std::atoi(params.get("OnroadScreenOffBrightness").c_str());
  QString labelBrightness;
  if (valBrightness == 0) labelBrightness = tr("Auto (Default)");
  else if (valBrightness == 1) labelBrightness = tr("Auto (Dark)");
  else if (valBrightness == 2) labelBrightness = tr("Screen Off");
  else labelBrightness = QString::number((valBrightness - 2) * 5) + "%";
  onroadScreenOffBrightness->setLabel(labelBrightness);

  // Driving Screen Off Timer — label uses mapped seconds (match raylib)
  const QString timer_key = QString::fromStdString(params.get("OnroadScreenOffTimer"));
  const int valTimer = onroadScreenOffTimerOptions.value(timer_key, "0").toInt();
  const QString labelTimer = (valTimer < 60) ? QString::number(valTimer) + "s"
                                             : QString::number(valTimer / 60) + "m";
  onroadScreenOffTimer->setLabel(labelTimer);

  // Timer is only relevant for non-AUTO brightness (raylib disables it for AUTO/AUTO_DARK)
  const bool timer_enabled = (valBrightness != 0 && valBrightness != 1);
  onroadScreenOffTimer->setEnabled(timer_enabled);
}
