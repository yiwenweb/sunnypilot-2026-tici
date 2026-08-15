/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once

#include "openpilot/selfdrive/ui/sunnypilot/ui.h"
#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/settings.h"
#include "openpilot/selfdrive/ui/sunnypilot/qt/widgets/controls.h"
#include "openpilot/selfdrive/ui/sunnypilot/qt/widgets/expandable_row.h"

// Matches raylib ONROAD_BRIGHTNESS_TIMER_VALUES (seconds): {0:3,1:5,2:7,3:10,4:15,5:30,6:60...15:600}
static const QMap<QString, QString> onroadScreenOffTimerOptions = {
  {"0", "3"},
  {"1", "5"},
  {"2", "7"},
  {"3", "10"},
  {"4", "15"},
  {"5", "30"},
  {"6", "60"},
  {"7", "120"},
  {"8", "180"},
  {"9", "240"},
  {"10", "300"},
  {"11", "360"},
  {"12", "420"},
  {"13", "480"},
  {"14", "540"},
  {"15", "600"}
};

class OnroadScreenBrightnessControl : public ExpandableToggleRow {
  Q_OBJECT

public:
  OnroadScreenBrightnessControl(const QString &param, const QString &title, const QString &desc, const QString &icon,
                                QWidget *parent = nullptr);
  void refresh();

private:
  Params params;
  OptionControlSP *onroadScreenOffTimer;
  OptionControlSP *onroadScreenBrightness;
};
