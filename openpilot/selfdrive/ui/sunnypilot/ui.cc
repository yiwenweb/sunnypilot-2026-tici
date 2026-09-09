/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */

#include "openpilot/selfdrive/ui/sunnypilot/ui.h"

#include "openpilot/common/watchdog.h"
#include "cereal/messaging/messaging.h"

void UIStateSP::updateStatus() {
  UIState::updateStatus();

  if (scene.started && scene.onroadScreenOffBrightness != 0) {
    auto selfdriveState = (*sm)["selfdriveState"].getSelfdriveState();
    if (selfdriveState.getAlertSize() != cereal::SelfdriveState::AlertSize::NONE) {
      reset_onroad_sleep_timer();
    } else if (scene.onroadScreenOffTimer > 0) {
      scene.onroadScreenOffTimer--;
    }
  }
}

UIStateSP::UIStateSP(QObject *parent) : UIState(parent) {
  sm = std::make_unique<SubMaster>(std::vector<const char*>{
    "modelV2", "controlsState", "extrinsicsCalibration", "radarState", "deviceState",
    "pandaStates", "carParams", "driverMonitoringState", "carState", "driverStateV2",
    "wideRoadCameraState", "managerState", "selfdriveState", "longitudinalPlan",
    "modelManagerSP", "selfdriveStateSP", "longitudinalPlanSP", "backupManagerSP",
    "carControl", "gpsLocationExternal", "gpsLocation", "lateralTorqueParameters",
    "carStateSP", "vehicleParameters", "liveMapDataSP", "carParamsSP", "carOutput"
  });

  // update timer
  timer = new QTimer(this);
  QObject::connect(timer, &QTimer::timeout, this, &UIStateSP::update);
  timer->start(1000 / UI_FREQ);

  // Param watcher for UIScene param updates
  param_watcher = new ParamWatcher(this);
  connect(param_watcher, &ParamWatcher::paramChanged, [=](const QString &param_name, const QString &param_value) {
    ui_update_params_sp(this);
  });
  param_watcher->addParam("DevUIInfo");
  param_watcher->addParam("StandstillTimer");
  param_watcher->addParam("TorqueBar");
  param_watcher->addParam("RocketFuel");
}

// This method overrides completely the update method from the parent class intentionally.
void UIStateSP::update() {
  update_sockets(this);
  update_state(this);
  updateStatus();

  if (sm->frame % UI_FREQ == 0) {
    watchdog_kick(nanos_since_boot());
    // sunnypilot: periodic param constraint enforcement (1 Hz)
    enforceConstraints();
  }
  emit uiUpdate(*this);
}

// Port of UIStateSP._enforce_constraints from the raylib UI
// (selfdrive/ui/sunnypilot/ui_state.py): keeps car-dependent params
// consistent with the current car and control availability.
void UIStateSP::enforceConstraints() {
  auto params = Params();

  bool has_cp = false;
  bool is_angle_steering = false;
  bool alpha_long_available = false;
  bool enable_bsm = false;
  bool has_long = false;

  auto cp_bytes = params.get("CarParamsPersistent");
  if (!cp_bytes.empty()) {
    AlignedBuffer aligned_buf;
    capnp::FlatArrayMessageReader cmsg(aligned_buf.align(cp_bytes.data(), cp_bytes.size()));
    cereal::CarParams::Reader CP = cmsg.getRoot<cereal::CarParams>();

    has_cp = true;
    is_angle_steering = CP.getSteerControlType() == cereal::CarParams::SteerControlType::ANGLE;
    alpha_long_available = CP.getAlphaLongitudinalAvailable();
    enable_bsm = CP.getEnableBsm();
    has_long = hasLongitudinalControl(CP);
  }

  bool icbm_available = false;
  bool has_icbm = false;
  auto cp_sp_bytes = params.get("CarParamsSPPersistent");
  if (!cp_sp_bytes.empty()) {
    AlignedBuffer aligned_buf_sp;
    capnp::FlatArrayMessageReader cmsg_sp(aligned_buf_sp.align(cp_sp_bytes.data(), cp_sp_bytes.size()));
    cereal::CarParamsSP::Reader CP_SP = cmsg_sp.getRoot<cereal::CarParamsSP>();

    icbm_available = CP_SP.getIntelligentCruiseButtonManagementAvailable();
    has_icbm = icbm_available && params.getBool("IntelligentCruiseButtonManagement");
  }

  if (has_cp) {
    if (params.getBool("EnforceTorqueControl") && params.getBool("NeuralNetworkLateralControl")) {
      params.putBool("EnforceTorqueControl", false);
      params.putBool("NeuralNetworkLateralControl", false);
    }

    if (params.getBool("LateralJerkTorqueController") && params.getBool("NeuralNetworkLateralControl")) {
      params.putBool("LateralJerkTorqueController", false);
      params.putBool("NeuralNetworkLateralControl", false);
    }

    // Angle steering: no torque-based lateral controls
    if (is_angle_steering) {
      params.remove("EnforceTorqueControl");
      params.remove("NeuralNetworkLateralControl");
      params.remove("LateralJerkTorqueController");
    }

    // Alpha longitudinal: clear if not available
    if (!alpha_long_available) {
      params.remove("AlphaLongitudinalEnabled");
    }

    // BSM not available: clear BSM-dependent settings
    if (!enable_bsm) {
      params.remove("AutoLaneChangeBsmDelay");
    }
  } else {
    // No CarParams: clear all car-dependent params as safety default
    params.remove("EnforceTorqueControl");
    params.remove("NeuralNetworkLateralControl");
    params.remove("LateralJerkTorqueController");
    params.remove("AlphaLongitudinalEnabled");
  }

  // No longitudinal control: no experimental mode or DEC
  if (!has_long) {
    params.remove("ExperimentalMode");
    params.remove("DynamicExperimentalControl");
  }

  // ICBM: clear if not available or if full longitudinal control is active
  if (!icbm_available || has_long) {
    params.remove("IntelligentCruiseButtonManagement");
    has_icbm = false;
  }

  // Cruise features requiring longitudinal or ICBM
  if (!(has_long || has_icbm)) {
    params.remove("CustomAccIncrementsEnabled");
    params.remove("SmartCruiseControlVision");
    params.remove("SmartCruiseControlMap");
  }
}

void ui_update_params_sp(UIStateSP *s) {
  auto params = Params();
  s->scene.dev_ui_info = std::atoi(params.get("DevUIInfo").c_str());
  s->scene.standstill_timer = params.getBool("StandstillTimer");
  s->scene.speed_limit_mode = std::atoi(params.get("SpeedLimitMode").c_str());
  s->scene.road_name = params.getBool("RoadNameToggle");
  s->scene.trueVEgoUI = params.getBool("TrueVEgoUI");
  s->scene.hideVEgoUI = params.getBool("HideVEgoUI");

  // Onroad Screen Brightness
  s->scene.onroadScreenOffBrightness = std::atoi(params.get("OnroadScreenOffBrightness").c_str());
  s->scene.onroadScreenOffTimerParam = std::atoi(params.get("OnroadScreenOffTimer").c_str());

  s->scene.turn_signals = params.getBool("ShowTurnSignals");
  s->scene.chevron_info = std::atoi(params.get("ChevronInfo").c_str());
  s->scene.blindspot_ui = params.getBool("BlindSpot");
  s->scene.rainbow_mode = params.getBool("RainbowMode");
  s->scene.torque_bar = params.getBool("TorqueBar");
  s->scene.rocket_fuel = params.getBool("RocketFuel");
}

void UIStateSP::reset_onroad_sleep_timer(OnroadTimerStatusToggle toggleTimerStatus) {
  // Toggling from active state to inactive
  if (toggleTimerStatus == OnroadTimerStatusToggle::PAUSE and scene.onroadScreenOffTimer != -1) {
    scene.onroadScreenOffTimer = -1;
  }
  // Toggling from a previously inactive state or resetting an active timer
  else if ((scene.onroadScreenOffTimerParam >= 0 and scene.onroadScreenOffBrightness != 0 and scene.onroadScreenOffTimer != -1) or toggleTimerStatus == OnroadTimerStatusToggle::RESUME) {
    // raylib: AUTO_DARK (1) uses a fixed 15s, otherwise use the mapped seconds
    if (scene.onroadScreenOffBrightness == 1) {
      scene.onroadScreenOffTimer = 15 * UI_FREQ;
    } else {
      scene.onroadScreenOffTimer = scene.onroadScreenOffTimerParam * UI_FREQ;
    }
  }
}

DeviceSP::DeviceSP(QObject *parent) : Device(parent) {
  QObject::connect(uiStateSP(), &UIStateSP::uiUpdate, this, &DeviceSP::update);
  QObject::connect(this, &Device::displayPowerChanged, this, &DeviceSP::handleDisplayPowerChanged);
}

UIStateSP *uiStateSP() {
  static UIStateSP ui_state;
  return &ui_state;
}

void UIStateSP::setSunnylinkRoles(const std::vector<RoleModel>& roles) {
  sunnylinkRoles = roles;
  emit sunnylinkRolesChanged(roles);
}

void UIStateSP::setSunnylinkDeviceUsers(const std::vector<UserModel>& users) {
  sunnylinkUsers = users;
  emit sunnylinkDeviceUsersChanged(users);
}

DeviceSP *deviceSP() {
  static DeviceSP _device;
  return &_device;
}

void DeviceSP::handleDisplayPowerChanged(bool on) {
  // if enabled, trigger offroad mode when device goes to sleep
  if (params.get("DeviceBootMode") == "1" && not on) {
    params.putBool("OffroadMode", true);
  }
}
