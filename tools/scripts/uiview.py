#!/usr/bin/env python3
import time

from openpilot.cereal import log, custom, messaging
from opendbc.car.structs import car
from openpilot.common.params import Params
from openpilot.system.manager.process_config import managed_processes, is_tinygrad_model, is_stock_model
from openpilot.common.hardware import HARDWARE

if __name__ == "__main__":
  # Keep the test UI on the normal onroad camera view. With notCar=True,
  # HomeWindow intentionally switches to BodyWindow instead of OnroadWindow.
  CP = car.CarParams(notCar=False, wheelbase=1, steerRatio=10)
  params = Params()
  params.put("CarParams", CP.to_bytes(), block=True)

  # Enable speed limit display so the onroad HUD renders the sign.
  # SpeedLimitMode: off=0, information=1, warning=2, assist=3
  params.put("SpeedLimitMode", 2, block=True)
  params.put_bool("IsMetric", True, block=True)

  if use_tinygrad_modeld := is_tinygrad_model(False, params, CP):
    print("Using TinyGrad modeld")
  if use_stock_modeld := is_stock_model(False, params, CP):
    print("Using stock modeld")

  HARDWARE.set_power_save(False)

  procs = ['camerad', 'ui', 'calibrationd', 'plannerd', 'dmonitoringmodeld', 'dmonitoringd']
  procs += ["modeld_tinygrad" if use_tinygrad_modeld else "modeld"]
  for p in procs:
    managed_processes[p].start()

  pm = messaging.PubMaster(['controlsState', 'deviceState', 'pandaStates', 'carParams', 'longitudinalPlanSP', 'liveMapDataSP'])

  msgs = {s: messaging.new_message(s) for s in ['controlsState', 'deviceState', 'carParams', 'longitudinalPlanSP', 'liveMapDataSP']}
  for s in msgs:
    msgs[s].valid = True
  msgs['deviceState'].deviceState.started = True
  msgs['deviceState'].deviceState.deviceType = HARDWARE.get_device_type()
  msgs['carParams'].carParams.openpilotLongitudinalControl = True

  # cereal speed limit fields are in m/s; the UI multiplies by MS_TO_KPH (3.6).
  KPH_TO_MS = 1.0 / 3.6

  # Fake a valid speed limit so the onroad HUD renders the sign (style check
  # without a real car). 60 km/h with a +5 km/h offset -> shows "60" with a
  # "+5" offset bubble, exercising both the Vienna circle and the offset box.
  lp = msgs['longitudinalPlanSP'].longitudinalPlanSP
  lp.speedLimit.resolver.speedLimit = 60.0 * KPH_TO_MS
  lp.speedLimit.resolver.speedLimitLast = 60.0 * KPH_TO_MS
  lp.speedLimit.resolver.speedLimitFinal = 60.0 * KPH_TO_MS
  lp.speedLimit.resolver.speedLimitFinalLast = 60.0 * KPH_TO_MS
  lp.speedLimit.resolver.speedLimitOffset = 5.0 * KPH_TO_MS
  lp.speedLimit.resolver.speedLimitValid = True
  lp.speedLimit.resolver.speedLimitLastValid = True
  lp.speedLimit.resolver.source = custom.LongitudinalPlanSP.SpeedLimit.Source.map
  lp.speedLimit.assist.state = custom.LongitudinalPlanSP.SpeedLimit.AssistState.inactive

  # Fake upcoming speed limit (80 km/h ahead) so the "AHEAD" box below the sign
  # renders. The distance is decremented every frame to satisfy the
  # speedLimitAheadValidFrame counter (must be strictly decreasing for >5 frames).
  lmd = msgs['liveMapDataSP'].liveMapDataSP
  lmd.speedLimitValid = True
  lmd.speedLimit = 60.0 * KPH_TO_MS
  lmd.speedLimitAheadValid = True
  lmd.speedLimitAhead = 80.0 * KPH_TO_MS
  lmd.roadName = "UIVIEW TEST"

  msgs['pandaStates'] = messaging.new_message('pandaStates', 1)
  msgs['pandaStates'].pandaStates[0].ignitionLine = True
  msgs['pandaStates'].pandaStates[0].pandaType = log.PandaState.PandaType.uno

  distance_ahead = 500.0  # meters, decremented each frame then wrapped
  try:
    while True:
      time.sleep(1 / 100)  # continually send, rate doesn't matter
      # Decrement distance so speedLimitAheadValidFrame increments past threshold;
      # wrap back to 500m so the "AHEAD" box keeps showing.
      distance_ahead -= 2.0
      if distance_ahead < 50.0:
        distance_ahead = 500.0
      lmd.speedLimitAheadDistance = distance_ahead
      for s in msgs:
        pm.send(s, msgs[s])
  except KeyboardInterrupt:
    for p in procs:
      managed_processes[p].stop()
