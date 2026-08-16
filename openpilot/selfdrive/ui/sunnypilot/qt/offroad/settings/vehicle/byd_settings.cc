/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/vehicle/byd_settings.h"

BydSettings::BydSettings(QWidget *parent) : BrandSettingsInterface(parent) {
  // 跟车距离档位：近/中/远/超远（对应门总实测的4档TTC策略）
  std::vector<QString> distance_texts{ tr("Close"), tr("Medium"), tr("Far"), tr("Extra Far") };
  followDistanceToggle = new ButtonParamControl(
    "BydFollowDistance",
    tr("Follow Distance"),
    "",
    "",
    distance_texts,
    500
  );
  QObject::connect(followDistanceToggle, &ButtonParamControlSP::buttonClicked, this, &BydSettings::updateSettings);
  list->addItem(followDistanceToggle);
  followDistanceToggle->showDescription();
}

void BydSettings::updateSettings() {
  // 读取当前跟车距离参数（1=近，2=中，3=远，4=超远）
  auto follow_distance_param = std::atoi(params.get("BydFollowDistance").c_str());

  BydFollowDistanceOption follow_distance_option;
  if (follow_distance_param == int(BydFollowDistanceOption::CLOSE)) {
    follow_distance_option = BydFollowDistanceOption::CLOSE;
  } else if (follow_distance_param == int(BydFollowDistanceOption::MEDIUM)) {
    follow_distance_option = BydFollowDistanceOption::MEDIUM;
  } else if (follow_distance_param == int(BydFollowDistanceOption::EXTRA_FAR)) {
    follow_distance_option = BydFollowDistanceOption::EXTRA_FAR;
  } else {
    follow_distance_option = BydFollowDistanceOption::FAR;  // 默认远档
  }

  // 更新描述文本
  QString distance_description = followDistanceDescription(follow_distance_option);
  
  // 只在offroad时允许修改
  followDistanceToggle->setEnabled(offroad);
  followDistanceToggle->setDescription(distance_description);
  followDistanceToggle->showDescription();
}
