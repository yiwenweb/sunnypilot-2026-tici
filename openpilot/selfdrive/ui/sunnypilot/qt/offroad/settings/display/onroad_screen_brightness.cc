/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/display/onroad_screen_brightness.h"

OnroadScreenBrightnessControl::OnroadScreenBrightnessControl(const QString &param, const QString &title,
                                                             const QString &description, const QString &icon,
                                                             QWidget *parent)
  : ExpandableToggleRow(param, title, description, icon, parent) {
  auto *mainFrame = new QFrame(this);
  auto *mainFrameLayout = new QVBoxLayout();
  mainFrame->setLayout(mainFrameLayout);
  mainFrameLayout->setSpacing(30);
  mainFrameLayout->setContentsMargins(0, 0, 0, 0);

  onroadScreenOffTimer = new OptionControlSP(
    "OnroadScreenOffTimer",
    tr("Onroad Brightness Delay"),
    "",
    "",
    {0, 15}, 1, true, &onroadScreenOffTimerOptions);

  onroadScreenBrightness = new OptionControlSP(
    "OnroadScreenOffBrightness",
    tr("Onroad Brightness"),
    "",
    "",
    {0, 22}, 1, true);

  connect(onroadScreenOffTimer, &OptionControlSP::updateLabels, this, &OnroadScreenBrightnessControl::refresh);
  connect(onroadScreenBrightness, &OptionControlSP::updateLabels, this, &OnroadScreenBrightnessControl::refresh);
  mainFrameLayout->addWidget(onroadScreenBrightness);
  mainFrameLayout->addWidget(onroadScreenOffTimer);

  addItem(mainFrame);

  refresh();
}

void OnroadScreenBrightnessControl::refresh() {
  // Driving Screen Off Timer — label uses mapped seconds (match raylib)
  const QString timer_key = QString::fromStdString(params.get("OnroadScreenOffTimer"));
  const int valTimer = onroadScreenOffTimerOptions.value(timer_key, "0").toInt();
  const QString labelTimer = (valTimer < 60) ? QString::number(valTimer) + "s"
                                             : QString::number(valTimer / 60) + "m";
  onroadScreenOffTimer->setLabel(labelTimer);

  // Driving Screen Off Brightness — match raylib OnroadBrightness enum (0=Auto,1=AutoDark,2=Off,3..22=(val-2)*5%)
  const int valBrightness = std::atoi(params.get("OnroadScreenOffBrightness").c_str());
  QString labelBrightness;
  if (valBrightness == 0) labelBrightness = tr("Auto (Default)");
  else if (valBrightness == 1) labelBrightness = tr("Auto (Dark)");
  else if (valBrightness == 2) labelBrightness = tr("Screen Off");
  else labelBrightness = QString::number((valBrightness - 2) * 5) + "%";
  onroadScreenBrightness->setLabel(labelBrightness);
}
