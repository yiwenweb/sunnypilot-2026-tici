/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#pragma once

#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/vehicle/brand_settings_interface.h"

// Follow Distance control removed: longitudinal following gap is already owned
// by Driving Personality (LongitudinalPersonality, cycled by the steering-wheel
// distance button and consumed by longitudinal_planner -> get_T_FOLLOW).
// A separate BydFollowDistance param would be a dead value at best and a
// conflicting double-writer at worst. Keep this class as an empty shell so
// BrandSettingsFactory can still instantiate it for future BYD-only settings.
class BydSettings : public BrandSettingsInterface {
  Q_OBJECT

public:
  explicit BydSettings(QWidget *parent = nullptr);
  void updateSettings() override;
};
