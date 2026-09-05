/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "openpilot/selfdrive/ui/sunnypilot/qt/offroad/settings/vehicle/toyota_settings.h"

#include "openpilot/selfdrive/ui/qt/util.h"

ToyotaSettings::ToyotaSettings(QWidget *parent) : BrandSettingsInterface(parent) {
  const QString enforce_stock_desc = tr("sunnypilot will not take over control of gas and brakes. Factory Toyota longitudinal control will be used.");
  const QString stop_and_go_hack_desc = tr("sunnypilot will allow some Toyota/Lexus cars to auto resume during stop and go traffic. "
                                           "This feature is only applicable to certain models that are able to use longitudinal control. "
                                           "This is an alpha feature. Use at your own risk.");

  enforceStockLongitudinalToggle = new ParamControlSP(
    "ToyotaEnforceStockLongitudinal",
    tr("Enforce Factory Longitudinal Control"),
    enforce_stock_desc,
    "",
    this
  );
  enforceStockLongitudinalToggle->setConfirmation(true, false);

  QObject::connect(enforceStockLongitudinalToggle, &ParamControlSP::toggleFlipped, [=](bool state) {
    if (state) {
      // Matches raylib _on_enable_enforce_stock_longitudinal: on enable, disable
      // AlphaLongitudinalEnabled and StopAndGoHack, then request an onroad cycle.
      if (params.getBool("AlphaLongitudinalEnabled")) {
        params.putBool("AlphaLongitudinalEnabled", false);
      }
      params.putBool("ToyotaStopAndGoHack", false);
      stopAndGoHackToggle->refresh();
    }
    params.putBool("OnroadCycleRequested", true);
  });
  list->addItem(enforceStockLongitudinalToggle);

  stopAndGoHackToggle = new ParamControlSP(
    "ToyotaStopAndGoHack",
    tr("Stop and Go Hack (Alpha)"),
    stop_and_go_hack_desc,
    "",
    this
  );
  stopAndGoHackToggle->setConfirmation(true, false);

  QObject::connect(stopAndGoHackToggle, &ParamControlSP::toggleFlipped, [=](bool state) {
    params.putBool("OnroadCycleRequested", true);
  });
  list->addItem(stopAndGoHackToggle);

  stopAndGoHackToggle->showDescription();
}

void ToyotaSettings::updateSettings() {
  const std::string cp_bytes = params.get("CarParamsPersistent");

  bool has_longitudinal_control = false;
  if (!cp_bytes.empty()) {
    AlignedBuffer aligned_buf;
    capnp::FlatArrayMessageReader cmsg(aligned_buf.align(cp_bytes.data(), cp_bytes.size()));
    cereal::CarParams::Reader CP = cmsg.getRoot<cereal::CarParams>();
    has_longitudinal_control = hasLongitudinalControl(CP);
  }

  const bool enforce_stock = params.getBool("ToyotaEnforceStockLongitudinal");

  enforceStockLongitudinalToggle->setEnabled(offroad);

  // Matches raylib update_settings: StopAndGoHack only available when
  // longitudinal control is active AND enforce_stock is off.
  const bool sng_available = has_longitudinal_control && !enforce_stock;
  stopAndGoHackToggle->setEnabled(offroad && sng_available);

  if (offroad && has_longitudinal_control) {
    if (sng_available) {
      stopAndGoHackToggle->setDescription(tr("sunnypilot will allow some Toyota/Lexus cars to auto resume during stop and go traffic. "
                                             "This feature is only applicable to certain models that are able to use longitudinal control. "
                                             "This is an alpha feature. Use at your own risk."));
    } else {
      stopAndGoHackToggle->setDescription("<b>" + tr("sunnypilot Longitudinal Control must be available and enabled for your vehicle to use this feature.") + "</b>");
    }
  } else if (!offroad) {
    stopAndGoHackToggle->setDescription("<b>" + tr("Start the vehicle to check vehicle compatibility.") + "</b>");
  }

  stopAndGoHackToggle->showDescription();
}
