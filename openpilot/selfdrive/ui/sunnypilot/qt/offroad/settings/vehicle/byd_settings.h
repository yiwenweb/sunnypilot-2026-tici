/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once

#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/vehicle/brand_settings_interface.h"

#include "openpilot/selfdrive/ui/qt/util.h"
#include "openpilot/selfdrive/ui/sunnypilot/ui.h"
#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/settings.h"
#include "openpilot/selfdrive/ui/sunnypilot/qt/widgets/controls.h"

enum class BydFollowDistanceOption {
  CLOSE = 1,      // 近档 2.5s TTC
  MEDIUM = 2,     // 中档 3.5s TTC
  FAR = 3,        // 远档 4.5s TTC（默认，门总实测）
  EXTRA_FAR = 4,  // 超远档 6.0s TTC
};

class BydSettings : public BrandSettingsInterface {
  Q_OBJECT

public:
  explicit BydSettings(QWidget *parent = nullptr);
  void updateSettings() override;

private:
  ButtonParamControl *followDistanceToggle = nullptr;

  static QString followDistanceDescription(BydFollowDistanceOption option = BydFollowDistanceOption::FAR) {
    QString close_str = tr("Close (2.5s): Sporty/Aggressive following");
    QString medium_str = tr("Medium (3.5s): Comfortable following");
    QString far_str = tr("Far (4.5s): Default, validated by real-world testing");
    QString extra_far_str = tr("Extra Far (6.0s): Conservative/Beginner-friendly");

    // 高亮当前选项
    if (option == BydFollowDistanceOption::CLOSE) {
      close_str = "<font color='white'><b>" + close_str + "</b></font>";
    } else if (option == BydFollowDistanceOption::MEDIUM) {
      medium_str = "<font color='white'><b>" + medium_str + "</b></font>";
    } else if (option == BydFollowDistanceOption::FAR) {
      far_str = "<font color='white'><b>" + far_str + "</b></font>";
    } else {
      extra_far_str = "<font color='white'><b>" + extra_far_str + "</b></font>";
    }

    return QString("%1<br><br>%2<br>%3<br>%4<br>%5")
             .arg(tr("Adjust the time gap between your vehicle and the lead vehicle during ACC operation."))
             .arg(close_str)
             .arg(medium_str)
             .arg(far_str)
             .arg(extra_far_str);
  }
};
