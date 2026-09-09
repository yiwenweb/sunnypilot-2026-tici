/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/vehicle/byd_settings.h"

BydSettings::BydSettings(QWidget *parent) : BrandSettingsInterface(parent) {
  // No brand-specific controls yet; the Vehicle panel now fits on one screen.
}

void BydSettings::updateSettings() {
  // Nothing to update; kept for BrandSettingsInterface compatibility.
}
