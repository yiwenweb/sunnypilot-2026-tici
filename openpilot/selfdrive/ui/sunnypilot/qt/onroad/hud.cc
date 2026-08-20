/**
 * Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
 *
 * This file is part of sunnypilot and is licensed under the MIT License.
 * See the LICENSE.md file in the root directory for more details.
 */
#include <algorithm>
#include <cmath>
#include <QPainterPath>

#include "openpilot/selfdrive/ui/sunnypilot/qt/onroad/hud.h"

#include "openpilot/selfdrive/ui/qt/util.h"


// HSV-space color blend, mirroring raylib's blend_colors (shortest hue delta).
static QColor hsvBlend(const QColor &a, const QColor &b, float f) {
  QColor ha = a.toHsv();
  QColor hb = b.toHsv();
  float dh = std::fmod(hb.hueF() - ha.hueF() + 0.5f, 1.0f) - 0.5f;  // shortest hue delta in [0,1)
  float h = std::fmod(ha.hueF() + f * dh + 1.0f, 1.0f);
  float s = ha.saturationF() + f * (hb.saturationF() - ha.saturationF());
  float v = ha.valueF() + f * (hb.valueF() - ha.valueF());
  float alphaF = a.alphaF() + f * (b.alphaF() - a.alphaF());
  return QColor::fromHsvF(h, std::clamp(s, 0.0f, 1.0f), std::clamp(v, 0.0f, 1.0f), std::clamp(alphaF, 0.0f, 1.0f));
}


HudRendererSP::HudRendererSP() {
  plus_arrow_up_img = loadPixmap("../../sunnypilot/selfdrive/assets/img_plus_arrow_up", {90, 90});
  minus_arrow_down_img = loadPixmap("../../sunnypilot/selfdrive/assets/img_minus_arrow_down", {90, 90});

  int size = e2e_alert_size * 2 - 40;
  green_light_alert_img = loadPixmap("../../sunnypilot/selfdrive/assets/images/green_light.png", {size, size});
  lead_depart_alert_img = loadPixmap("../../sunnypilot/selfdrive/assets/images/lead_depart.png", {size, size});
}

void HudRendererSP::updateState(const UIState &s) {
  HudRenderer::updateState(s);

  float speedConv = is_metric ? MS_TO_KPH : MS_TO_MPH;
  devUiInfo = s.scene.dev_ui_info;
  roadName = s.scene.road_name;
  showTurnSignals = s.scene.turn_signals;
  speedLimitMode = static_cast<SpeedLimitMode>(s.scene.speed_limit_mode);
  speedUnit = is_metric ? tr("km/h") : tr("mph");
  standstillTimer = s.scene.standstill_timer;

  const SubMaster &sm = *(s.sm);
  const auto cs = sm["controlsState"].getControlsState();
  const auto car_state = sm["carState"].getCarState();
  const auto car_control = sm["carControl"].getCarControl();
  const auto radar_state = sm["radarState"].getRadarState();
  const auto is_gps_location_external = sm.rcv_frame("gpsLocationExternal") > 1;
  const char *gps_source = is_gps_location_external ? "gpsLocationExternal" : "gpsLocation";
  const auto gpsLocation = is_gps_location_external ? sm[gps_source].getGpsLocationExternal() : sm[gps_source].getGpsLocation();
  const auto ltp = sm["liveTorqueParameters"].getLiveTorqueParameters();
  const auto car_params = sm["carParams"].getCarParams();
  const auto car_params_sp = sm["carParamsSP"].getCarParamsSP();
  const auto lp_sp = sm["longitudinalPlanSP"].getLongitudinalPlanSP();
  const auto lmd = sm["liveMapDataSP"].getLiveMapDataSP();

  if (sm.updated("carParams")) {
    steerControlType = car_params.getSteerControlType();
  }

  if (sm.updated("carParamsSP")) {
    pcmCruiseSpeed = car_params_sp.getPcmCruiseSpeed();
  }

  if (sm.updated("longitudinalPlanSP")) {
    speedLimit = lp_sp.getSpeedLimit().getResolver().getSpeedLimit() * speedConv;
    speedLimitLast = lp_sp.getSpeedLimit().getResolver().getSpeedLimitLast() * speedConv;
    speedLimitOffset = lp_sp.getSpeedLimit().getResolver().getSpeedLimitOffset() * speedConv;
    speedLimitValid = lp_sp.getSpeedLimit().getResolver().getSpeedLimitValid();
    speedLimitLastValid = lp_sp.getSpeedLimit().getResolver().getSpeedLimitLastValid();
    speedLimitFinalLast = lp_sp.getSpeedLimit().getResolver().getSpeedLimitFinalLast() * speedConv;
    speedLimitSource = lp_sp.getSpeedLimit().getResolver().getSource();
    speedLimitAssistState = lp_sp.getSpeedLimit().getAssist().getState();
    speedLimitAssistActive = lp_sp.getSpeedLimit().getAssist().getActive();
    smartCruiseControlVisionEnabled = lp_sp.getSmartCruiseControl().getVision().getEnabled();
    smartCruiseControlVisionActive = lp_sp.getSmartCruiseControl().getVision().getActive();
    smartCruiseControlMapEnabled = lp_sp.getSmartCruiseControl().getMap().getEnabled();
    smartCruiseControlMapActive = lp_sp.getSmartCruiseControl().getMap().getActive();
  }
  greenLightAlert = lp_sp.getE2eAlerts().getGreenLightAlert();
  leadDepartAlert = lp_sp.getE2eAlerts().getLeadDepartAlert();

  if (sm.updated("liveMapDataSP")) {
    roadNameStr = QString::fromStdString(lmd.getRoadName());
    speedLimitAheadValid = lmd.getSpeedLimitAheadValid();
    speedLimitAhead = lmd.getSpeedLimitAhead() * speedConv;
    speedLimitAheadDistance = lmd.getSpeedLimitAheadDistance();
    if (speedLimitAheadDistance < speedLimitAheadDistancePrev && speedLimitAheadValidFrame < SPEED_LIMIT_AHEAD_VALID_FRAME_THRESHOLD) {
      speedLimitAheadValidFrame++;
    } else if (speedLimitAheadDistance > speedLimitAheadDistancePrev && speedLimitAheadValidFrame > 0) {
      speedLimitAheadValidFrame--;
    }
  }
  speedLimitAheadDistancePrev = speedLimitAheadDistance;

  static int reverse_delay = 0;
  bool reverse_allowed = false;
  if (car_state.getGearShifter() != cereal::CarState::GearShifter::REVERSE) {
    reverse_delay = 0;
    reverse_allowed = false;
  } else {
    reverse_delay += 50;
    if (reverse_delay >= 1000) {
      reverse_allowed = true;
    }
  }

  reversing = reverse_allowed;

  if (sm.updated("liveParameters")) {
    roll = sm["liveParameters"].getLiveParameters().getRoll();
  }

  if (sm.updated("deviceState")) {
    memoryUsagePercent = sm["deviceState"].getDeviceState().getMemoryUsagePercent();
  }

  if (sm.updated(gps_source)) {
    gpsAccuracy = is_gps_location_external ? gpsLocation.getHorizontalAccuracy() : 1.0;  // External reports accuracy, internal does not.
    altitude = gpsLocation.getAltitude();
    bearingAccuracyDeg = gpsLocation.getBearingAccuracyDeg();
    bearingDeg = gpsLocation.getBearingDeg();
  }

  if (sm.updated("liveTorqueParameters")) {
    torquedUseParams = ltp.getUseParams();
    latAccelFactorFiltered = ltp.getLatAccelFactorFiltered();
    frictionCoefficientFiltered = ltp.getFrictionCoefficientFiltered();
    liveValid = ltp.getLiveValid();
  }

  latActive = car_control.getLatActive();
  actuators = car_control.getActuators();
  longOverride = car_control.getCruiseControl().getOverride();
  carControlEnabled = car_control.getEnabled();

  steerOverride = car_state.getSteeringPressed();
  lead_d_rel = radar_state.getLeadOne().getDRel();
  lead_v_rel = radar_state.getLeadOne().getVRel();
  lead_status = radar_state.getLeadOne().getPresent();
  torqueLateral = steerControlType == cereal::CarParams::SteerControlType::TORQUE;
  angleSteers = car_state.getSteeringAngleDeg();
  desiredCurvature = cs.getDesiredCurvature();
  curvature = cs.getCurvature();
  vEgo = car_state.getVEgo();
  aEgo = car_state.getAEgo();
  steeringTorqueEps = car_state.getSteeringTorqueEps();

  isStandstill = car_state.getStandstill();
  if (!s.scene.started) standstillElapsedTime = 0.0;

  // override stock current speed values
  float v_ego = (v_ego_cluster_seen && !s.scene.trueVEgoUI) ? car_state.getVEgoCluster() : car_state.getVEgo();
  speed = std::max<float>(0.0f, v_ego * (is_metric ? MS_TO_KPH : MS_TO_MPH));
  hideVEgoUI = s.scene.hideVEgoUI;

  leftBlinkerOn = car_state.getLeftBlinker();
  rightBlinkerOn = car_state.getRightBlinker();
  leftBlindspot = car_state.getLeftBlindspot();
  rightBlindspot = car_state.getRightBlindspot();

  // TorqueBar & RocketFuel
  torqueBar = s.scene.torque_bar;
  rocketFuel = s.scene.rocket_fuel;
  if (sm.updated("controlsState") || sm.updated("carState") || sm.updated("liveParameters")) {
    // TorqueBar: update lateral torque utilization filter
    const auto controls_state = sm["controlsState"].getControlsState();
    const auto live_parameters = sm["liveParameters"].getLiveParameters();
    const float max_lat_accel = car_params.getMaxLateralAccel();

    if (controls_state.getLateralControlState().which() == cereal::ControlsState::LateralControlState::ANGLE_STATE ||
        controls_state.getLateralControlState().which() == cereal::ControlsState::LateralControlState::CURVATURE_STATE) {
      float actual_lateral_accel = controls_state.getCurvature() * car_state.getVEgo() * car_state.getVEgo();
      float desired_lateral_accel = controls_state.getDesiredCurvature() * car_state.getVEgo() * car_state.getVEgo();
      float accel_diff = desired_lateral_accel - actual_lateral_accel;
      float roll_comp = live_parameters.getRoll() * 9.81f * std::min(1.0f, std::max(0.0f, (car_state.getVEgo() - 5.0f) / 10.0f));
      float lateral_accel = actual_lateral_accel - roll_comp;

      if (!car_control.getLatActive()) {
        torqueFilterX += (0.0f - torqueFilterX) * 0.1f;
      } else {
        float target = std::clamp((lateral_accel + accel_diff) / max_lat_accel, -1.0f, 1.0f);
        torqueFilterX += (target - torqueFilterX) * 0.1f;
      }
    } else {
      float target = -sm["carOutput"].getCarOutput().getActuatorsOutput().getTorque();
      torqueFilterX += (target - torqueFilterX) * 0.1f;
    }

    // TorqueBar: alpha filter (animated based on engagement)
    float alpha_target = (status != STATUS_DISENGAGED && status != STATUS_LONG_ONLY) ? 1.0f : 0.0f;
    torqueLineAlphaFilter += (alpha_target - torqueLineAlphaFilter) * 0.1f;

    // RocketFuel: smooth acceleration
    vcAccel += (car_state.getAEgo() - vcAccel) / 5.0f;
  }

  // ConfidenceBall: read disengagePredictions and smooth the confidence value.
  // Mirrors mici/onroad/confidence_ball.py:
  //   1 - max(steerOverrideProbs or [1])       (LAT_ONLY)
  //   1 - max(brakeDisengageProbs or [1])      (LONG_ONLY)
  //   (1 - max(brake)) * (1 - max(steer))      (ENGAGED)
  // Empty list -> fallback to 1.0 so `1 - max` = 0 (low confidence).
  if (sm.updated("modelV2")) {
    const auto model = sm["modelV2"].getModelV2();
    float confidence_target = -0.5f;
    auto dp = model.getMeta().getDisengagePredictions();
    // capnp List Reader access is via operator[] (not .c_data()).
    auto steer_reader = dp.getSteerOverrideProbs();
    auto brake_reader = dp.getBrakeDisengageProbs();

    float max_steer = steer_reader.size() > 0 ? steer_reader[0] : 1.0f;
    for (size_t i = 1; i < steer_reader.size(); ++i) {
      max_steer = std::max(max_steer, steer_reader[i]);
    }
    float max_brake = brake_reader.size() > 0 ? brake_reader[0] : 1.0f;
    for (size_t i = 1; i < brake_reader.size(); ++i) {
      max_brake = std::max(max_brake, brake_reader[i]);
    }

    if (status == STATUS_DISENGAGED) {
      confidence_target = -0.5f;
    } else if (status == STATUS_LAT_ONLY) {
      // LAT_ONLY: use steerOverrideProbs
      confidence_target = 1.0f - max_steer;
    } else if (status == STATUS_LONG_ONLY) {
      // LONG_ONLY: use brakeDisengageProbs
      confidence_target = 1.0f - max_brake;
    } else if (status == STATUS_ENGAGED) {
      // ENGAGED: combine both probabilities
      confidence_target = (1.0f - max_steer) * (1.0f - max_brake);
    } else {
      // OVERRIDE
      confidence_target = 0.5f;
    }

    // First-order low-pass filter (alpha = 0.2, ~5 frames smoothing at 20Hz)
    confidenceFilterX += (confidence_target - confidenceFilterX) * 0.2f;
    confidenceFilterX = std::clamp(confidenceFilterX, -0.5f, 1.0f);
  }

  speedCluster = car_state.getCruiseState().getSpeedCluster() * speedConv;

  allow_e2e_alerts = sm["selfdriveState"].getSelfdriveState().getAlertSize() == cereal::SelfdriveState::AlertSize::NONE &&
                     sm.rcv_frame("driverStateV2") > s.scene.started_frame && !reversing;
}

void HudRendererSP::draw(QPainter &p, const QRect &surface_rect) {
  HudRenderer::draw(p, surface_rect);

  e2eAlertDisplayTimer = std::max(0, e2eAlertDisplayTimer - 1);

  p.save();

  if (is_cruise_available) {
    drawSetSpeedSP(p, surface_rect);
  }

  if (!hideVEgoUI) {
    drawCurrentSpeedSP(p, surface_rect);
  }

  if (!reversing) {
    // Smart Cruise Control
    int x_offset = -260;
    int y1_offset = -60;
    int y2_offset = -120;

    int y_scc_v = 0, y_scc_m = 0;
    const int orders[2] = {y1_offset, y2_offset};
    int i = 0;
    // SCC-V takes first order
    if (smartCruiseControlVisionEnabled) y_scc_v = orders[i++];
    if (smartCruiseControlMapEnabled) y_scc_m = orders[i++];

    // Smart Cruise Control - Vision
    bool scc_vision_active_pulse = pulseElement(smartCruiseControlVisionFrame);
    if ((smartCruiseControlVisionEnabled && !smartCruiseControlVisionActive) || (smartCruiseControlVisionActive && scc_vision_active_pulse)) {
      drawSmartCruiseControlOnroadIcon(p, surface_rect, x_offset, y_scc_v, "SCC-V");
    }
    smartCruiseControlVisionFrame = smartCruiseControlVisionActive ? (smartCruiseControlVisionFrame + 1) : 0;

    // Smart Cruise Control - Map
    bool scc_map_active_pulse = pulseElement(smartCruiseControlMapFrame);
    if ((smartCruiseControlMapEnabled && !smartCruiseControlMapActive) || (smartCruiseControlMapActive && scc_map_active_pulse)) {
      drawSmartCruiseControlOnroadIcon(p, surface_rect, x_offset, y_scc_m, "SCC-M");
    }
    smartCruiseControlMapFrame = smartCruiseControlMapActive ? (smartCruiseControlMapFrame + 1) : 0;

    // Dev UI — matches raylib DeveloperUiState: OFF=0, BOTTOM=1, RIGHT=2, BOTH=3
    const bool dev_ui_bottom = (devUiInfo == 1 || devUiInfo == 3);
    const bool dev_ui_right = (devUiInfo == 2 || devUiInfo == 3);

    // Bottom Dev UI
    if (dev_ui_bottom) {
      QRect rect_bottom(surface_rect.left(), surface_rect.bottom() - 60, surface_rect.width(), 61);
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(0, 0, 0, 100));
      p.drawRect(rect_bottom);
      drawBottomDevUI(p, rect_bottom.left(), rect_bottom.center().y());
    }

    // Right Dev UI
    if (dev_ui_right) {
      QRect rect_right(surface_rect.right() - (UI_BORDER_SIZE * 2), UI_BORDER_SIZE * 1.5, 184, 170);
      drawRightDevUI(p, surface_rect.right() - 184 - UI_BORDER_SIZE * 2, UI_BORDER_SIZE * 2 + rect_right.height());
    }

    // Speed Limit
    bool showSpeedLimit;
    bool speed_limit_assist_pre_active_pulse = pulseElement(speedLimitAssistFrame);

    // Position speed limit sign next to set speed box.
    // Matches raylib speed_limit.py _render():
    //   x = rect.x + 60 + width + 30 - 6  (screen coords, minus UI_BORDER_SIZE=30 -> local)
    //   y = rect.y + 45 - 6
    //   height = set_speed_height + 6*2 = 216
    const int sign_width = is_metric ? 200 : 172;
    const int sign_x = is_metric ? 284 : 256;
    const int sign_y = 39;
    const int sign_height = 216;
    QRect sign_rect(sign_x, sign_y, sign_width, sign_height);

    if (speedLimitAssistState == cereal::LongitudinalPlanSP::SpeedLimit::AssistState::PRE_ACTIVE) {
      speedLimitAssistFrame++;
      showSpeedLimit = speed_limit_assist_pre_active_pulse;
      drawSpeedLimitPreActiveArrow(p, sign_rect);
    } else {
      speedLimitAssistFrame = 0;
      showSpeedLimit = speedLimitMode != SpeedLimitMode::OFF;
    }

    if (showSpeedLimit) {
      drawSpeedLimitSigns(p, sign_rect);

      // do not show during SLA's preActive state
      if (speedLimitAssistState != cereal::LongitudinalPlanSP::SpeedLimit::AssistState::PRE_ACTIVE) {
        drawUpcomingSpeedLimit(p);
      }
    }

    // Road Name
    drawRoadName(p, surface_rect);

    // Green Light & Lead Depart Alerts
    if (greenLightAlert || leadDepartAlert) {
      e2eAlertDisplayTimer = 3 * UI_FREQ;
      // reset onroad sleep timer for e2e alerts
      uiStateSP()->reset_onroad_sleep_timer();
    }

    if (e2eAlertDisplayTimer > 0) {
      e2eAlertFrame++;
      if (greenLightAlert) {
        alert_text = tr("GREEN\nLIGHT");
        alert_img = green_light_alert_img;
      }
      else if (leadDepartAlert) {
        alert_text = tr("LEAD VEHICLE\nDEPARTING");
        alert_img = lead_depart_alert_img;
      }
      drawE2eAlert(p, surface_rect);
    }
    // Standstill Timer
    else if (standstillTimer && isStandstill) {
      alert_img = QPixmap();

      standstillElapsedTime += 1.0 / UI_FREQ;
      int minute = static_cast<int>(standstillElapsedTime / 60);
      int second = static_cast<int>(standstillElapsedTime - (minute * 60));
      alert_text = QString("%1:%2").arg(minute, 1, 10, QChar('0')).arg(second, 2, 10, QChar('0'));
      drawE2eAlert(p, surface_rect, tr("STOPPED"));
      e2eAlertFrame++;
    }
    // No Alerts displayed
    else {
      e2eAlertFrame = 0;
      if (!isStandstill) standstillElapsedTime = 0.0;
    }

    // Blinker
    if (showTurnSignals) {
      drawBlinker(p, surface_rect);
    }

    // TorqueBar
    if (torqueBar) {
      drawTorqueBar(p, surface_rect);
    }

    // RocketFuel
    if (rocketFuel) {
      drawRocketFuel(p, surface_rect);
    }

    // ConfidenceBall
    drawConfidenceBall(p, surface_rect);
  }

  p.restore();
}

void HudRendererSP::drawText(QPainter &p, int x, int y, const QString &text, QColor color) {
  QRect real_rect = p.fontMetrics().boundingRect(text);
  real_rect.moveCenter({x, y - real_rect.height() / 2});
  p.setPen(color);
  p.drawText(real_rect.x(), real_rect.bottom(), text);
}

bool HudRendererSP::pulseElement(int frame) {
  if (frame % UI_FREQ < (UI_FREQ / 2.5)) {
    return false;
  }

  return true;
}

void HudRendererSP::drawSmartCruiseControlOnroadIcon(QPainter &p, const QRect &surface_rect, int x_offset, int y_offset, std::string name) {
  int x = surface_rect.center().x();
  int y = surface_rect.height() / 4;

  QString text = QString::fromStdString(name);
  QFont font = InterFont(36, QFont::Bold);
  p.setFont(font);

  QFontMetrics fm(font);

  int padding_v = 5;
  int box_width = 160;
  int box_height = fm.height() + padding_v * 2;

  QRectF bg_rect(x - (box_width / 2) + x_offset,
                 y - (box_height / 2) + y_offset,
                 box_width, box_height);

  QPainterPath boxPath;
  boxPath.addRoundedRect(bg_rect, 10, 10);

  int text_w = fm.horizontalAdvance(text);
  qreal baseline_y = bg_rect.top() + padding_v + fm.ascent();
  qreal text_x = bg_rect.center().x() - (text_w / 2.0);

  QPainterPath textPath;
  textPath.addText(QPointF(text_x, baseline_y), font, text);
  boxPath = boxPath.subtracted(textPath);

  p.setPen(Qt::NoPen);
  p.setBrush(longOverride ? QColor(0x91, 0x9b, 0x95, 0xf1) : QColor(0, 0xff, 0, 0xff));
  p.drawPath(boxPath);
}

int HudRendererSP::drawRightDevUIElement(QPainter &p, int x, int y, const QString &value, const QString &label, const QString &units, QColor &color) {

  p.setFont(InterFont(28, QFont::Bold));
  x += 92;
  y += 80;
  drawText(p, x, y, label);

  p.setFont(InterFont(30 * 2, QFont::Bold));
  y += 65;
  drawText(p, x, y, value, color);

  p.setFont(InterFont(28, QFont::Bold));

  if (units.length() > 0) {
    p.save();
    x += 120;
    y -= 25;
    p.translate(x, y);
    p.rotate(-90);
    drawText(p, 0, 0, units);
    p.restore();
  }

  return 130;
}

void HudRendererSP::drawRightDevUI(QPainter &p, int x, int y) {
  int rh = 5;
  int ry = y;

  UiElement dRelElement = DeveloperUi::getDRel(lead_status, lead_d_rel);
  rh += drawRightDevUIElement(p, x, ry, dRelElement.value, dRelElement.label, dRelElement.units, dRelElement.color);
  ry = y + rh;

  UiElement vRelElement = DeveloperUi::getVRel(lead_status, lead_v_rel, is_metric, speedUnit);
  rh += drawRightDevUIElement(p, x, ry, vRelElement.value, vRelElement.label, vRelElement.units, vRelElement.color);
  ry = y + rh;

  UiElement steeringAngleDegElement = DeveloperUi::getSteeringAngleDeg(angleSteers, latActive, steerOverride);
  rh += drawRightDevUIElement(p, x, ry, steeringAngleDegElement.value, steeringAngleDegElement.label, steeringAngleDegElement.units, steeringAngleDegElement.color);
  ry = y + rh;

  UiElement actuatorsOutputLateralElement = DeveloperUi::getActuatorsOutputLateral(steerControlType, actuators, desiredCurvature, vEgo, roll, latActive, steerOverride);
  rh += drawRightDevUIElement(p, x, ry, actuatorsOutputLateralElement.value, actuatorsOutputLateralElement.label, actuatorsOutputLateralElement.units, actuatorsOutputLateralElement.color);
  ry = y + rh;

  UiElement actualLateralAccelElement = DeveloperUi::getActualLateralAccel(curvature, vEgo, roll, latActive, steerOverride);
  rh += drawRightDevUIElement(p, x, ry, actualLateralAccelElement.value, actualLateralAccelElement.label, actualLateralAccelElement.units, actualLateralAccelElement.color);
}

int HudRendererSP::drawBottomDevUIElement(QPainter &p, int x, int y, const QString &value, const QString &label, const QString &units, QColor &color) {
  p.setFont(InterFont(38, QFont::Bold));
  QFontMetrics fm(p.font());
  QRect init_rect = fm.boundingRect(label + " ");
  QRect real_rect = fm.boundingRect(init_rect, 0, label + " ");
  real_rect.moveCenter({x, y});

  QRect init_rect2 = fm.boundingRect(value);
  QRect real_rect2 = fm.boundingRect(init_rect2, 0, value);
  real_rect2.moveTop(real_rect.top());
  real_rect2.moveLeft(real_rect.right() + 10);

  QRect init_rect3 = fm.boundingRect(units);
  QRect real_rect3 = fm.boundingRect(init_rect3, 0, units);
  real_rect3.moveTop(real_rect.top());
  real_rect3.moveLeft(real_rect2.right() + 10);

  p.setPen(Qt::white);
  p.drawText(real_rect, Qt::AlignLeft | Qt::AlignVCenter, label);

  p.setPen(color);
  p.drawText(real_rect2, Qt::AlignRight | Qt::AlignVCenter, value);
  p.drawText(real_rect3, Qt::AlignLeft | Qt::AlignVCenter, units);
  return 430;
}

void HudRendererSP::drawBottomDevUI(QPainter &p, int x, int y) {
  int rw = 90;

  UiElement aEgoElement = DeveloperUi::getAEgo(aEgo);
  rw += drawBottomDevUIElement(p, rw, y, aEgoElement.value, aEgoElement.label, aEgoElement.units, aEgoElement.color);

  UiElement vEgoLeadElement = DeveloperUi::getVEgoLead(lead_status, lead_v_rel, vEgo, is_metric, speedUnit);
  rw += drawBottomDevUIElement(p, rw, y, vEgoLeadElement.value, vEgoLeadElement.label, vEgoLeadElement.units, vEgoLeadElement.color);

  if (torqueLateral && torquedUseParams) {
    UiElement frictionCoefficientFilteredElement = DeveloperUi::getFrictionCoefficientFiltered(frictionCoefficientFiltered, liveValid);
    rw += drawBottomDevUIElement(p, rw, y, frictionCoefficientFilteredElement.value, frictionCoefficientFilteredElement.label, frictionCoefficientFilteredElement.units, frictionCoefficientFilteredElement.color);

    UiElement latAccelFactorFilteredElement = DeveloperUi::getLatAccelFactorFiltered(latAccelFactorFiltered, liveValid);
    rw += drawBottomDevUIElement(p, rw, y, latAccelFactorFilteredElement.value, latAccelFactorFilteredElement.label, latAccelFactorFilteredElement.units, latAccelFactorFilteredElement.color);
  } else {
    UiElement steeringTorqueEpsElement = DeveloperUi::getSteeringTorqueEps(steeringTorqueEps);
    rw += drawBottomDevUIElement(p, rw, y, steeringTorqueEpsElement.value, steeringTorqueEpsElement.label, steeringTorqueEpsElement.units, steeringTorqueEpsElement.color);

    UiElement bearingDegElement = DeveloperUi::getBearingDeg(bearingAccuracyDeg, bearingDeg);
    rw += drawBottomDevUIElement(p, rw, y, bearingDegElement.value, bearingDegElement.label, bearingDegElement.units, bearingDegElement.color);
  }

  UiElement altitudeElement = DeveloperUi::getAltitude(gpsAccuracy, altitude);
  rw += drawBottomDevUIElement(p, rw, y, altitudeElement.value, altitudeElement.label, altitudeElement.units, altitudeElement.color);
}

void HudRendererSP::drawSpeedLimitSigns(QPainter &p, QRect &sign_rect) {
  bool speedLimitWarningEnabled = speedLimitMode >= SpeedLimitMode::WARNING;  // TODO-SP: update to include SpeedLimitMode::ASSIST
  bool hasSpeedLimit = speedLimitValid || speedLimitLastValid;
  bool overspeed = hasSpeedLimit && std::nearbyint(speedLimitFinalLast) < std::nearbyint(speed);
  QString speedLimitStr = hasSpeedLimit ? QString::number(std::nearbyint(speedLimitLast)) : "---";

  // Offset display text
  QString speedLimitSubText = "";
  if (speedLimitOffset != 0) {
    speedLimitSubText = (speedLimitOffset > 0 ? "" : "-") + QString::number(std::nearbyint(speedLimitOffset));
  }

  int alpha = 255;
  // Matches raylib Colors.RED = (235, 32, 32)
  QColor red_color = QColor(235, 32, 32, alpha);
  QColor speed_color = (speedLimitWarningEnabled && overspeed) ? red_color :
                       (!speedLimitValid && speedLimitLastValid ? QColor(0x91, 0x9b, 0x95, 0xf1) : QColor(0, 0, 0, alpha));

  if (is_metric) {
    // EU Vienna Convention style circular sign.
    // Matches raylib _render_vienna(): radius = (width + 18) / 2,
    // red ring inner radius = radius * 0.75, outer = radius.
    qreal radius = (sign_rect.width() + 18) / 2.0;
    QPointF center(sign_rect.x() + sign_rect.width() / 2.0,
                   sign_rect.y() + sign_rect.height() / 2.0);

    // White background circle (diameter = width + 18)
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, alpha));
    p.drawEllipse(center, radius, radius);

    // Red ring: outer radius = radius, inner radius = radius * 0.75
    p.setBrush(red_color);
    p.drawEllipse(center, radius, radius);
    p.setBrush(QColor(255, 255, 255, alpha));
    p.drawEllipse(center, radius * 0.75, radius * 0.75);

    // Speed value, smaller font for 3+ digits
    int font_size = (speedLimitStr.size() >= 3) ? 70 : 85;
    p.setFont(InterFont(font_size, QFont::Bold));
    p.setPen(speed_color);
    QRectF text_rect(center.x() - radius, center.y() - radius, radius * 2, radius * 2);
    p.drawText(text_rect, Qt::AlignCenter, speedLimitStr);

    // Offset value in small circular box.
    // Matches raylib: s_radius = radius * 0.4, s_center = (rect.x + width - s_radius/2, rect.y + s_radius/2)
    if (!speedLimitSubText.isEmpty() && hasSpeedLimit) {
      qreal s_radius = radius * 0.4;
      QPointF s_center(sign_rect.x() + sign_rect.width() - s_radius / 2.0,
                       sign_rect.y() + s_radius / 2.0);

      // Dark grey ring (width 3) over black circle
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(77, 77, 77, 255));
      p.drawEllipse(s_center, s_radius, s_radius);
      p.setBrush(QColor(0, 0, 0, alpha));
      p.drawEllipse(s_center, s_radius - 3, s_radius - 3);

      qreal font_scale = (speedLimitSubText.size() < 3) ? 0.5 : 0.45;
      int sub_font_size = int(s_radius * 2 * font_scale);
      p.setFont(InterFont(sub_font_size, QFont::Bold));
      p.setPen(QColor(255, 255, 255, alpha));
      QRectF sub_rect(s_center.x() - s_radius, s_center.y() - s_radius, s_radius * 2, s_radius * 2);
      p.drawText(sub_rect, Qt::AlignCenter, speedLimitSubText);
    }
  } else {
    // US/Canada MUTCD style sign.
    // Matches raylib _render_mutcd(): outer roundness 0.35, inner radius = outer - 10.
    qreal outer_radius = 0.35 * sign_rect.width() / 2.0;
    qreal inner_radius = outer_radius - 10.0;

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, alpha));
    p.drawRoundedRect(sign_rect, outer_radius, outer_radius);

    // Inner border (4px black line)
    QRectF inner_rect = QRectF(sign_rect).adjusted(10, 10, -10, -10);
    p.setPen(QPen(QColor(0, 0, 0, alpha), 4));
    p.setBrush(QColor(255, 255, 255, alpha));
    p.drawRoundedRect(inner_rect, inner_radius, inner_radius);

    // Text centered at rect.y + 40 / 80 / 150 (matches _draw_text_centered)
    auto drawCentered = [&](const QString &text, int font_size, QFont::Weight weight, qreal center_y, const QColor &color) {
      p.setFont(InterFont(font_size, weight));
      QFontMetrics fm(p.font());
      int line_height = fm.height();
      QRectF line_rect(sign_rect.x(), center_y - line_height / 2.0, sign_rect.width(), line_height);
      p.setPen(color);
      p.drawText(line_rect, Qt::AlignCenter, text);
    };

    drawCentered(tr("SPEED"), 40, QFont::DemiBold, sign_rect.y() + 40, QColor(0, 0, 0, alpha));
    drawCentered(tr("LIMIT"), 40, QFont::DemiBold, sign_rect.y() + 80, QColor(0, 0, 0, alpha));
    drawCentered(speedLimitStr, 90, QFont::Bold, sign_rect.y() + 150, speed_color);

    // Offset value in small box.
    // Matches raylib: box_sz = width * 0.3, overlap = box_sz * 0.2,
    // s_rect = (x + width - box_sz/1.5 + overlap, y - box_sz/1.25 + overlap, box_sz, box_sz)
    if (!speedLimitSubText.isEmpty() && hasSpeedLimit) {
      qreal box_sz = sign_rect.width() * 0.3;
      qreal overlap = box_sz * 0.2;
      QRectF offset_box_rect(
        sign_rect.x() + sign_rect.width() - box_sz / 1.5 + overlap,
        sign_rect.y() - box_sz / 1.25 + overlap,
        box_sz,
        box_sz
      );

      qreal box_radius = 0.35 * box_sz / 2.0;
      p.setPen(QPen(QColor(77, 77, 77, 255), 6));
      p.setBrush(QColor(0, 0, 0, alpha));
      p.drawRoundedRect(offset_box_rect, box_radius, box_radius);

      qreal f_scale = (speedLimitSubText.size() < 3) ? 0.6 : 0.475;
      int sub_font_size = int(box_sz * f_scale);
      p.setFont(InterFont(sub_font_size, QFont::Bold));
      p.setPen(QColor(255, 255, 255, alpha));
      p.drawText(offset_box_rect, Qt::AlignCenter, speedLimitSubText);
    }
  }
}

void HudRendererSP::drawUpcomingSpeedLimit(QPainter &p) {
  bool speed_limit_ahead = speedLimitAheadValid && speedLimitAhead > 0 && speedLimitAhead != speedLimit && speedLimitAheadValidFrame > 0 &&
                           speedLimitSource == cereal::LongitudinalPlanSP::SpeedLimit::Source::MAP;
  if (!speed_limit_ahead) {
    return;
  }

  auto roundToInterval = [&](float distance, int interval, int threshold) {
    int base = static_cast<int>(distance / interval) * interval;
    return (distance - base >= threshold) ? base + interval : base;
  };

  auto outputDistance = [&] {
    if (is_metric) {
      if (speedLimitAheadDistance < 50) return tr("Near");
      if (speedLimitAheadDistance >= 1000) return QString::number(speedLimitAheadDistance * METER_TO_KM, 'f', 1) + tr("km");

      int rounded = (speedLimitAheadDistance < 200) ? std::max(10, roundToInterval(speedLimitAheadDistance, 10, 5)) : roundToInterval(speedLimitAheadDistance, 100, 50);
      return QString::number(rounded) + tr("m");
    } else {
      float distance_ft = speedLimitAheadDistance * METER_TO_FOOT;
      if (distance_ft < 100) return tr("Near");
      if (distance_ft >= 900) return QString::number(speedLimitAheadDistance * METER_TO_MILE, 'f', 1) + tr("mi");

      int rounded = (distance_ft < 500) ? std::max(50, roundToInterval(distance_ft, 50, 25)) : roundToInterval(distance_ft, 100, 50);
      return QString::number(rounded) + tr("ft");
    }
  };

  QString speedStr = QString::number(std::nearbyint(speedLimitAhead));
  QString distanceStr = outputDistance();

  // Position below current speed limit sign.
  // Must match the sign_rect computed in drawSpeedLimitSigns().
  const int sign_width = is_metric ? 200 : 172;
  const int sign_x = is_metric ? 284 : 256;
  const int sign_y = 39;
  const int sign_height = 216;

  const int ahead_width = 170;
  const int ahead_height = 160;
  const int ahead_x = sign_x + (sign_width - ahead_width) / 2;
  const int ahead_y = sign_y + sign_height + 10;

  QRect ahead_rect(ahead_x, ahead_y, ahead_width, ahead_height);
  // Raylib draw_rectangle_rounded(rect, 0.35) uses roundness=0.35 relative to
  // half the shorter side: 0.35 * 160 / 2 = 28px radius.
  const int ahead_radius = 28;
  p.setPen(QPen(QColor(255, 255, 255, 100), 3));
  p.setBrush(QColor(0, 0, 0, 180));
  p.drawRoundedRect(ahead_rect, ahead_radius, ahead_radius);

  // Text colors match raylib: AHEAD and distance use GREY (145,155,149),
  // speed value uses WHITE.
  const QColor grey_color(145, 155, 149, 255);
  const QColor white_color(255, 255, 255, 255);

  // Vertical centering matches raylib's _draw_text_centered: each text block's
  // center is at rect.y + offset (28 / 82 / 134), so build a per-line rect whose
  // vertical center lands on that exact pixel.
  auto drawCentered = [&](const QString &text, int font_size, QFont::Weight weight, int center_y, const QColor &color) {
    p.setFont(InterFont(font_size, weight));
    QFontMetrics fm(p.font());
    int line_height = fm.height();
    QRect line_rect(ahead_rect.x(), ahead_rect.y() + center_y - line_height / 2, ahead_rect.width(), line_height);
    p.setPen(color);
    p.drawText(line_rect, Qt::AlignCenter, text);
  };

  // "AHEAD" label (font_demi 40, center y+28)
  drawCentered(tr("AHEAD"), 40, QFont::DemiBold, 28, grey_color);

  // Speed value (font_bold 70, center y+82)
  drawCentered(speedStr, 70, QFont::Bold, 82, white_color);

  // Distance (font_norm 36, center y+134)
  drawCentered(distanceStr, 36, QFont::Normal, 134, grey_color);
}

void HudRendererSP::drawRoadName(QPainter &p, const QRect &surface_rect) {
  if (!roadName || roadNameStr.isEmpty()) return;

  // Measure text to size container (font enlarged 20% from 46 -> 55)
  p.setFont(InterFont(55, QFont::DemiBold));
  QFontMetrics fm(p.font());

  int text_width = fm.horizontalAdvance(roadNameStr);
  int padding = 48;
  int rect_width = text_width + padding;

  // Constrain to reasonable bounds
  int min_width = 240;
  int max_width = surface_rect.width() - 40;
  rect_width = std::max(min_width, std::min(rect_width, max_width));

  // Center at top of screen (match raylib: rect.x + width/2 - rect_width/2, rect.y - 4)
  // Height enlarged 20% from 60 -> 72
  QRect road_rect(surface_rect.x() + surface_rect.width() / 2 - rect_width / 2, surface_rect.y() + 16, rect_width, 72);

  p.setPen(Qt::NoPen);
  p.setBrush(QColor(0, 0, 0, 120));
  p.drawRoundedRect(road_rect, 12, 12);

  p.setPen(QColor(255, 255, 255, 200));

  // Truncate if still too long
  QString truncated = fm.elidedText(roadNameStr, Qt::ElideRight, road_rect.width() - 20);
  p.drawText(road_rect, Qt::AlignCenter, truncated);
}

void HudRendererSP::drawSpeedLimitPreActiveArrow(QPainter &p, QRect &sign_rect) {
  const int sign_margin = 12;
  const int arrow_spacing = sign_margin * 1.4;
  int arrow_x = sign_rect.right() + arrow_spacing;

  int _set_speed = std::nearbyint(set_speed);
  int _speed_limit_final_last = std::nearbyint(speedLimitFinalLast);

  // Calculate the vertical offset using a sinusoidal function for smooth bouncing
  double bounce_frequency = 2.0 * M_PI / UI_FREQ;  // 20 frames for one full oscillation
  int bounce_offset = 20 * sin(speedLimitAssistFrame * bounce_frequency);  // Adjust the amplitude (20 pixels) as needed

  if (_set_speed < _speed_limit_final_last) {
    QPoint iconPosition(arrow_x, sign_rect.center().y() - plus_arrow_up_img.height() / 2 + bounce_offset);
    p.drawPixmap(iconPosition, plus_arrow_up_img);
  } else if (_set_speed > _speed_limit_final_last) {
    QPoint iconPosition(arrow_x, sign_rect.center().y() - minus_arrow_down_img.height() / 2 - bounce_offset);
    p.drawPixmap(iconPosition, minus_arrow_down_img);
  }
}

void HudRendererSP::drawSetSpeedSP(QPainter &p, const QRect &surface_rect) {
  // Draw outer box + border to contain set speed
  const QSize default_size = {172, 204};
  QSize set_speed_size = is_metric ? QSize(200, 204) : default_size;
  QRect set_speed_rect(QPoint(60 + (default_size.width() - set_speed_size.width()) / 2, 45), set_speed_size);

  // Draw set speed box
  p.setPen(QPen(QColor(255, 255, 255, 75), 6));
  p.setBrush(QColor(0, 0, 0, 166));
  p.drawRoundedRect(set_speed_rect, 32, 32);

  // Colors based on status
  QColor max_color = QColor(0xa6, 0xa6, 0xa6, 0xff);
  QColor set_speed_color = QColor(0x72, 0x72, 0x72, 0xff);
  if (is_cruise_set) {
    set_speed_color = QColor(255, 255, 255);
    if (speedLimitAssistActive) {
      set_speed_color = longOverride ? QColor(0x91, 0x9b, 0x95, 0xff) : QColor(0, 0xff, 0, 0xff);
      max_color = longOverride ? QColor(0x91, 0x9b, 0x95, 0xff) : QColor(0x80, 0xd8, 0xa6, 0xff);
    } else {
      if (status == STATUS_DISENGAGED) {
        max_color = QColor(255, 255, 255);
      } else if (status == STATUS_OVERRIDE) {
        max_color = QColor(0x91, 0x9b, 0x95, 0xff);
      } else {
        max_color = QColor(0x80, 0xd8, 0xa6, 0xff);
      }
    }
  }

  // Draw "MAX" or carState.cruiseState.speedCluster (when ICBM is active) text
  if (!pcmCruiseSpeed && carControlEnabled) {
    if (std::nearbyint(set_speed) != std::nearbyint(speedCluster)) {
      icbm_active_counter = 3 * UI_FREQ;
    } else if (icbm_active_counter > 0) {
      icbm_active_counter--;
    }
  } else {
    icbm_active_counter = 0;
  }
  int max_str_size = (icbm_active_counter != 0) ? 60 : 40;
  int max_str_y = (icbm_active_counter != 0) ? 15 : 27;
  QString max_str = (icbm_active_counter != 0) ? QString::number(std::nearbyint(speedCluster)) : tr("MAX");

  p.setFont(InterFont(max_str_size, QFont::DemiBold));
  p.setPen(max_color);
  p.drawText(set_speed_rect.adjusted(0, max_str_y, 0, 0), Qt::AlignTop | Qt::AlignHCenter, max_str);

  // Draw set speed
  QString setSpeedStr = is_cruise_set ? QString::number(std::nearbyint(set_speed)) : "–";
  p.setFont(InterFont(90, QFont::Bold));
  p.setPen(set_speed_color);
  p.drawText(set_speed_rect.adjusted(0, 77, 0, 0), Qt::AlignTop | Qt::AlignHCenter, setSpeedStr);
}

void HudRendererSP::drawE2eAlert(QPainter &p, const QRect &surface_rect, const QString &alert_alt_text) {
  if (!allow_e2e_alerts) return;

  // Matches raylib: width adjustment 180 when RIGHT(2)/BOTH(3), else 100
  int x = surface_rect.right() - e2e_alert_size - ((devUiInfo == 2 || devUiInfo == 3) ? 180 : 100) - (UI_BORDER_SIZE * 3);
  int y = surface_rect.center().y() + 20;
  QRect alertRect(x - e2e_alert_size, y - e2e_alert_size, e2e_alert_size * 2, e2e_alert_size * 2);

  // Alert Circle
  QPoint center = alertRect.center();
  QColor frameColor;
  if (!alert_alt_text.isEmpty()) frameColor = QColor(255, 255, 255, 75);
  else frameColor = pulseElement(e2eAlertFrame) ? QColor(255, 255, 255, 75) : QColor(0, 255, 0, 75);
  p.setPen(QPen(frameColor, 15));
  p.setBrush(QColor(0, 0, 0, 190));
  p.drawEllipse(center, e2e_alert_size, e2e_alert_size);

  // Alert Text
  QColor txtColor;
  QFont font;
  int alert_bottom_adjustment;
  if (!alert_alt_text.isEmpty()) {
    font = InterFont(100, QFont::Bold);
    alert_bottom_adjustment = 5;
    txtColor = QColor(255, 255, 255, 255);
  } else {
    font = InterFont(48, QFont::Bold);
    alert_bottom_adjustment = 7;
    txtColor = pulseElement(e2eAlertFrame) ? QColor(255, 255, 255, 255) : QColor(0, 255, 0, 190);
  }
  p.setPen(txtColor);
  p.setFont(font);
  QFontMetrics fm(p.font());
  QRect textRect = fm.boundingRect(alertRect, Qt::TextWordWrap, alert_text);
  textRect.moveCenter({alertRect.center().x(), alertRect.center().y()});
  textRect.moveBottom(alertRect.bottom() - alertRect.height() / alert_bottom_adjustment);
  p.drawText(textRect, Qt::AlignCenter, alert_text);

  if (!alert_alt_text.isEmpty()) {
    // Alert Alternate Text
    p.setFont(InterFont(80, QFont::Bold));
    p.setPen(QColor(255, 175, 3, 240));
    QFontMetrics fmt(p.font());
    QRect topTextRect = fmt.boundingRect(alertRect, Qt::TextWordWrap, alert_alt_text);
    topTextRect.moveCenter({alertRect.center().x(), alertRect.center().y()});
    topTextRect.moveTop(alertRect.top() + alertRect.height() / 3.5);
    p.drawText(topTextRect, Qt::AlignCenter, alert_alt_text);
  } else {
    // Alert Image instead of Top Text
    QPointF pixmapCenterOffset = QPointF(alert_img.width() / 2.0, alert_img.height() / 2.0);
    QPointF drawPoint = center - pixmapCenterOffset;
    p.drawPixmap(drawPoint, alert_img);
  }
}

void HudRendererSP::drawCurrentSpeedSP(QPainter &p, const QRect &surface_rect) {
  QString speedStr = QString::number(std::nearbyint(speed));

  p.setFont(InterFont(176, QFont::Bold));
  HudRenderer::drawText(p, surface_rect.center().x(), 235, speedStr);

  p.setFont(InterFont(66));
  HudRenderer::drawText(p, surface_rect.center().x(), 315, is_metric ? tr("km/h") : tr("mph"), 200);
}

void HudRendererSP::drawBlinker(QPainter &p, const QRect &surface_rect) {
  const bool hazard = leftBlinkerOn && rightBlinkerOn;
  int blinkerStatus = hazard ? 2 : (leftBlinkerOn || rightBlinkerOn) ? 1 : 0;

  if (!leftBlinkerOn && !rightBlinkerOn) {
    blinkerFrameCounter = 0;
    lastBlinkerStatus = 0;
    return;
  }

  if (blinkerStatus != lastBlinkerStatus) {
    blinkerFrameCounter = 0;
    lastBlinkerStatus = blinkerStatus;
  }

  ++blinkerFrameCounter;

  const int BLINKER_COOLDOWN_FRAMES = UI_FREQ / 10;
  if (blinkerFrameCounter < BLINKER_COOLDOWN_FRAMES) {
    return;
  }

  const int circleRadius = 60;
  const int arrowLength = 60;
  const int x_gap = 160;
  const int y_offset = 272;

  const int centerX = surface_rect.center().x();

  const QPen bgBorder(Qt::white, 5);
  const QPen arrowPen(Qt::NoPen);

  p.save();

  auto drawArrow = [&](int cx, int cy, int dir, const QBrush &arrowBrush) {
    const int bodyLength = arrowLength / 2;
    const int bodyWidth = arrowLength / 2;
    const int headLength = arrowLength / 2;
    const int headWidth = arrowLength;

    QPolygon arrow;
    arrow.reserve(7);
    arrow << QPoint(cx - dir * bodyLength, cy - bodyWidth / 2)
        << QPoint(cx, cy - bodyWidth / 2)
        << QPoint(cx, cy - headWidth / 2)
        << QPoint(cx + dir * headLength, cy)
        << QPoint(cx, cy + headWidth / 2)
        << QPoint(cx, cy + bodyWidth / 2)
        << QPoint(cx - dir * bodyLength, cy + bodyWidth / 2);

    p.setPen(arrowPen);
    p.setBrush(arrowBrush);
    p.drawPolygon(arrow);
  };

  auto drawCircle = [&](int cx, int cy, const QBrush &bgBrush) {
    p.setPen(bgBorder);
    p.setBrush(bgBrush);
    p.drawEllipse(QPoint(cx, cy), circleRadius, circleRadius);
  };

  struct BlinkerSide { bool on; int dir; bool blocked; int cx; };
  const std::array<BlinkerSide, 2> sides = {{
    {leftBlinkerOn, -1, hazard ? true : (leftBlinkerOn  && leftBlindspot), centerX - x_gap},
    {rightBlinkerOn, 1, hazard ? true : (rightBlinkerOn && rightBlindspot), centerX + x_gap},
  }};

  for (const auto &s: sides) {
    if (!s.on) continue;

    QColor bgColor = s.blocked ? QColor(135, 23, 23) : QColor(23, 134, 68);
    QColor arrowColor = s.blocked ? QColor(66, 12, 12) : QColor(12, 67, 34);
    if (pulseElement(blinkerFrameCounter)) arrowColor = Qt::white;

    const QBrush bgBrush(bgColor);
    const QBrush arrowBrush(arrowColor);

    drawCircle(s.cx, y_offset, bgBrush);
    drawArrow(s.cx, y_offset, s.dir, arrowBrush);
  }

  p.restore();
}

void HudRendererSP::drawTorqueBar(QPainter &p, const QRect &surface_rect) {
  // The raylib reference (torque_bar.py) uses scale=3.0 on tici/mici big_ui
  // (2160x1080 logical space), giving radius = 1200*3 = 3600. The Qt surface_rect
  // (1800x1020, sidebar excluded) shares the same 1080-height logical space, so
  // scale must also be 3.0 to match the reference arc size and curvature.
  const float scale = 3.0f;
  const float TORQUE_ANGLE_SPAN = 12.7f;

  // Adjust Y position and height based on torque magnitude.
  // Mirror raylib's np.interp(x, [0.5, 1], [22, 26]): clamp to 22 for x < 0.5.
  // Raised base offset from 22 to 30 so the arc sits a bit higher (avoids
  // overlapping the very bottom edge / bottom HUD).
  float abs_torque = std::abs(torqueFilterX);
  float torque_line_offset = (abs_torque < 0.5f) ? 30.0f * scale : (30.0f * scale + (abs_torque - 0.5f) / 0.5f * 4.0f * scale);
  float torque_line_height = 14.0f * scale + (abs_torque < 0.5f ? 0.0f : (abs_torque - 0.5f) / 0.5f * 42.0f * scale);

  // Background arc alpha: raylib np.interp(x, [0.5, 1], [0.25, 0.5]) clamps to 0.25 below 0.5
  float bg_alpha = (abs_torque < 0.5f) ? 0.25f : (0.25f + (abs_torque - 0.5f) / 0.5f * 0.25f);
  if (status != STATUS_ENGAGED && status != STATUS_LAT_ONLY) {
    bg_alpha = 0.15f * torqueLineAlphaFilter;
  }

  float torque_line_radius = 1200.0f * scale;
  // raylib uses math coords (x=cos, y=sin with Y down) where -90 maps to the
  // top of the circle. Qt's drawArc is CCW-positive from 3 o'clock, so the
  // top (12 o'clock) is +90 degrees. top_angle must be +90 to match raylib.
  float top_angle = 90.0f;
  float torque_bg_angle_span = torqueLineAlphaFilter * TORQUE_ANGLE_SPAN;
  float torque_start_angle = top_angle - torque_bg_angle_span / 2.0f;

  float mid_r = torque_line_radius + torque_line_height / 2.0f;
  float cx = surface_rect.x() + surface_rect.width() / 2.0f + 8.0f;
  float cy = surface_rect.y() + surface_rect.height() + torque_line_radius - torque_line_offset;

  p.save();
  p.setRenderHint(QPainter::Antialiasing, true);

  // Draw background torque indicator arc
  if (torqueLineAlphaFilter > 0.0f) {
    QPen bg_pen(QColor(255, 255, 255, (int)(255 * bg_alpha * torqueLineAlphaFilter)), torque_line_height);
    bg_pen.setCapStyle(Qt::RoundCap);
    p.setPen(bg_pen);

    QRectF bg_arc_rect(cx - mid_r, cy - mid_r, mid_r * 2, mid_r * 2);
    p.drawArc(bg_arc_rect, (int)(torque_start_angle * 16), (int)(torque_bg_angle_span * 16));
  }

  // Draw torque indicator line (foreground arc)
  // raylib's arc_bar_pts uses math coords (x=cos, y=sin, CCW positive), where
  // torque.x > 0 sweeps from top toward +X (right). Qt's drawArc is CCW-positive
  // from 3 o'clock, so positive torque must map to a negative span (clockwise,
  // from 12 o'clock toward 3 o'clock) to sweep the same direction.
  float a0s = top_angle;
  float fg_span = -torque_bg_angle_span / 2.0f * torqueFilterX;

  if (std::abs(fg_span) > 0.01f && torqueLineAlphaFilter > 0.0f) {
    // Color: raylib blends white -> yellow(start)/orange(end) in HSV space as
    // torque approaches max (factor f = clamp(abs(torque)-0.75, 0, 0.25)*4).
    float f = std::min(1.0f, std::max(0.0f, abs_torque - 0.75f) * 4.0f);

    int a;
    QColor start_color, end_color;
    if (status != STATUS_ENGAGED && status != STATUS_LAT_ONLY) {
      start_color = end_color = QColor(255, 255, 255, (int)(255 * 0.35f * torqueLineAlphaFilter));
    } else {
      a = (int)(255 * 0.9f * torqueLineAlphaFilter);
      // white -> yellow (255,200,0) via HSV blend
      QColor white(255, 255, 255, a);
      QColor yellow(255, 200, 0, (int)(255 * torqueLineAlphaFilter));
      QColor orange(255, 115, 0, (int)(255 * torqueLineAlphaFilter));
      start_color = hsvBlend(white, yellow, f);
      end_color = hsvBlend(white, orange, f);
    }

    // Horizontal gradient from center to 65% of the arc endpoint (mirror raylib)
    float end_x = (torqueFilterX < 0.0f)
      ? (cx * (1.0f - 0.65f) + (cx - mid_r) * 0.65f)
      : (cx * (1.0f - 0.65f) + (cx + mid_r) * 0.65f);
    QLinearGradient gradient(QPointF(cx, cy), QPointF(end_x, cy));
    gradient.setColorAt(0.0f, start_color);
    gradient.setColorAt(1.0f, end_color);

    QPen fg_pen(QBrush(gradient), torque_line_height);
    fg_pen.setCapStyle(Qt::RoundCap);
    p.setPen(fg_pen);

    QRectF fg_arc_rect(cx - mid_r, cy - mid_r, mid_r * 2, mid_r * 2);
    p.drawArc(fg_arc_rect, (int)(a0s * 16), (int)(fg_span * 16));
  }

  // Draw center dot when torque is near zero
  if (abs_torque < 0.5f && torqueLineAlphaFilter > 0.0f) {
    float dot_y = surface_rect.y() + surface_rect.height() - torque_line_offset - torque_line_height / 2.0f;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(182, 182, 182, (int)(255 * 0.9f * torqueLineAlphaFilter)));
    p.drawEllipse(QPointF(cx, dot_y), 5.0f * scale, 5.0f * scale);
  }

  p.restore();
}

void HudRendererSP::drawRocketFuel(QPainter &p, const QRect &surface_rect) {
  float hha = 0.0f;
  QColor color(0, 0, 0, 0);

  if (vcAccel > 0.0f) {
    hha = 0.85f - 0.1f / vcAccel;
    color = QColor(0, 245, 0, 200);
  } else if (vcAccel < 0.0f) {
    hha = 0.85f + 0.1f / vcAccel;
    color = QColor(245, 0, 0, 200);
  }

  if (hha < 0.0f) hha = 0.0f;

  float rect_h = surface_rect.height();
  float hha_px = hha * rect_h;
  float wp = 28.0f;

  float ra_y;
  if (vcAccel > 0.0f) {
    ra_y = rect_h / 2.0f - hha_px / 2.0f;
  } else {
    ra_y = rect_h / 2.0f;
  }

  if (hha_px > 0.0f) {
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    p.drawRect(QRectF(0, surface_rect.y() + ra_y, wp, hha_px / 2.0f));
    p.restore();
  }
}

void HudRendererSP::drawConfidenceBall(QPainter &p, const QRect &surface_rect) {
  // ConfidenceBall: displays model confidence as a vertical gradient dot on the right edge.
  // Mirrors the raylib mici ConfidenceBall logic:
  //   - Dot position: lower = low confidence, higher = high confidence
  //   - Color: green(cyan) = high, yellow/orange = medium, red = low
  //   - MADS states use fixed colors (cyan=purple)
  // Position: right side of the screen, matching the side panel area.
  // raylib uses status_dot_radius=24 and dot center 24px from the right edge
  // (content_rect.width - radius = SIDE_PANEL_WIDTH(60) - 24 = 36). Both Qt and
  // raylib big-screen UI share the 2160x1080 logical space, so no DPI scaling.
  const int dot_radius = 24;
  const int dot_x = surface_rect.right() - dot_radius;
  const int content_h = surface_rect.height();

  // Vertical position: mirror raylib's dot_height = (1 - confidence) * (h - 2r) + r.
  // confidence=1.0 → r (top); confidence=0.0 → h-r (bottom); confidence=-0.5 → off-screen.
  float dot_y = (1.0f - confidenceFilterX) * (content_h - 2 * dot_radius) + dot_radius;

  // Determine colors based on status and confidence level
  QColor top_color(50, 50, 50, 255);   // default: grey (disengaged)
  QColor bottom_color(13, 13, 13, 255);

  if (status == STATUS_ENGAGED) {
    if (confidenceFilterX > 0.5f) {
      top_color = QColor(0, 255, 204, 255);   // cyan-green: high confidence
      bottom_color = QColor(0, 255, 38, 255);  // green: high confidence
    } else if (confidenceFilterX > 0.2f) {
      top_color = QColor(255, 200, 0, 255);   // yellow: medium
      bottom_color = QColor(255, 115, 0, 255); // orange: medium-low
    } else {
      top_color = QColor(255, 0, 21, 255);     // red: low confidence
      bottom_color = QColor(255, 0, 89, 255);
    }
  } else if (status == STATUS_LAT_ONLY) {
    top_color = QColor(0, 200, 200, 255);     // cyan for LAT_ONLY
    bottom_color = QColor(0, 200, 200, 255);
  } else if (status == STATUS_LONG_ONLY) {
    top_color = QColor(150, 28, 168, 255);    // purple for LONG_ONLY
    bottom_color = QColor(150, 28, 168, 255);
  } else if (status == STATUS_OVERRIDE) {
    top_color = QColor(255, 255, 255, 255);   // white
    bottom_color = QColor(82, 82, 82, 255);   // grey
  }

  // Draw gradient circle. raylib uses draw_rectangle_gradient_v (vertical top→bottom
  // gradient) masked by a ring, so use a vertical QLinearGradient here, not radial.
  p.save();
  p.setPen(Qt::NoPen);
  QLinearGradient gradient(QPointF(dot_x, dot_y - dot_radius),
                           QPointF(dot_x, dot_y + dot_radius));
  gradient.setColorAt(0.0, top_color);
  gradient.setColorAt(1.0, bottom_color);
  p.setBrush(gradient);
  p.drawEllipse(QPointF(dot_x, dot_y), dot_radius, dot_radius);
  p.restore();
}