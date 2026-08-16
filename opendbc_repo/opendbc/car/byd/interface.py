from opendbc.car import get_safety_config, structs
from opendbc.car.common.conversions import Conversions as CV
from opendbc.car.interfaces import CarInterfaceBase
from opendbc.car.byd.values import CAR, CanBus, BydSafetyFlags, MPC_ACC_CAR, TORQUE_LAT_CAR, EXP_LONG_CAR, \
                                PLATFORM_HANTANG_DMEV
from opendbc.car.byd.carcontroller import CarController
from opendbc.car.byd.carstate import CarState
from opendbc.car.byd.radar_interface import RadarInterface

ButtonType = structs.CarState.ButtonEvent.Type
GearShifter = structs.CarState.GearShifter
TransmissionType = structs.CarParams.TransmissionType
NetworkLocation = structs.CarParams.NetworkLocation

import os
BYD_RADAR = os.getenv("BYD_RADAR") is not None


class CarInterface(CarInterfaceBase):
    CarState = CarState
    CarController = CarController
    RadarInterface = RadarInterface

    @staticmethod
    def _get_params(ret: structs.CarParams, candidate, fingerprint, car_fw,
                    alpha_long, is_release, docs) -> structs.CarParams:
        ret.brand = "byd"
        ret.safetyConfigs = [get_safety_config(structs.CarParams.SafetyModel.byd)]

        ret.dashcamOnly = False
        if BYD_RADAR:
            ret.radarUnavailable = False
        else:
            ret.radarUnavailable = True

        ret.enableBsm = 0x418 in fingerprint[CanBus.ESC]
        ret.transmissionType = TransmissionType.direct

        ret.minEnableSpeed = -1.
        ret.minSteerSpeed = 0.1 * CV.KPH_TO_MS

        ret.steerActuatorDelay = 0.3   # 门总实测 0.30 (BYD EPS has ~0.3s lag);
                                       # 0.05 assumed near-instant response → no lead steering → sluggish turn-in
        ret.steerLimitTimer = 0.5      # 门总实测 0.5 (原 0.4)

        if candidate in PLATFORM_HANTANG_DMEV:
            ret.safetyConfigs[0].safetyParam |= BydSafetyFlags.HAN_TANG_DMEV.value

        if candidate in MPC_ACC_CAR:
            ret.networkLocation = NetworkLocation.fwdCamera

        use_torque_lat = candidate in TORQUE_LAT_CAR

        if use_torque_lat:
            CarInterfaceBase.configure_torque_tune(candidate, ret.lateralTuning)
            # 2026版torque控制器不再使用PID参数(ki/kp/kf已废弃),只需latAccelFactor/friction(由override.toml提供)
            # BYD实测值: latAccelFactor=2.0, friction=0.05 (见override.toml)
        else:
            ret.lateralTuning.init('pid')
            ret.lateralTuning.pid.kpBP, ret.lateralTuning.pid.kiBP = [[8.3, 27.8], [8.3, 27.8]]
            ret.lateralTuning.pid.kpV, ret.lateralTuning.pid.kiV = [[0.6, 0.3], [0.2, 0.1]]
            ret.lateralTuning.pid.kf = 0.000072

        use_experimental_long = candidate in EXP_LONG_CAR

        ret.alphaLongitudinalAvailable = use_experimental_long
        ret.openpilotLongitudinalControl = alpha_long and ret.alphaLongitudinalAvailable
        ret.pcmCruise = True  # Always follow PCM cruise state for activation/speed display, even when OP handles longitudinal accel

        ret.longitudinalTuning.kpBP, ret.longitudinalTuning.kiBP = [[0.], [0.]]
        ret.longitudinalTuning.kpV, ret.longitudinalTuning.kiV = [[1.5], [0.3]]

        if candidate == CAR.BYD_TANG_DM:
            # ret.steerRatio = 19.0 已在values.py的CarSpecs中定义（门总实测: liveParameters学习锁定19.0）
            # 2026版框架会自动从CarSpecs填充 mass/wheelbase/steerRatio/centerToFront，无需手动设置
            ret.minSteerSpeed = 0
            ret.autoResumeSng = True
            ret.startingState = True
            
            # 驾驶风格绑定（读取sunnypilot的LongitudinalPersonality参数）
            # 0=激进, 1=标准, 2=舒适
            try:
                from opendbc.car.common.params import Params
                personality = Params().get_int("LongitudinalPersonality")
            except:
                personality = 1  # 默认标准
            
            if personality == 0:  # 激进
                ret.startAccel = 1.0
                ret.stopAccel = -0.7
                ret.longitudinalActuatorDelay = 0.4
                ret.longitudinalTuning.kpV = [1.8]
                ret.longitudinalTuning.kiV = [0.4]
            elif personality == 2:  # 舒适
                ret.startAccel = 0.6
                ret.stopAccel = -0.4
                ret.longitudinalActuatorDelay = 0.6
                ret.longitudinalTuning.kpV = [1.2]
                ret.longitudinalTuning.kiV = [0.25]
            else:  # 标准（默认）
                ret.startAccel = 0.8
                ret.stopAccel = -0.5
                ret.longitudinalActuatorDelay = 0.5
                ret.longitudinalTuning.kpV = [1.5]
                ret.longitudinalTuning.kiV = [0.3]
            
            # 跟车距离档位（读取UI设置，影响stoppingDecelRate）
            # 1=近档2.5s, 2=中档3.5s, 3=远档4.5s(默认), 4=超远档6.0s
            # 通过调整stoppingDecelRate影响跟车距离（值越小距离越远）
            try:
                from opendbc.car.common.params import Params
                follow_distance = Params().get_int("BydFollowDistance")
                if follow_distance == 1:
                    ret.stoppingDecelRate = 0.8  # 近档：更激进的减速
                elif follow_distance == 2:
                    ret.stoppingDecelRate = 0.6  # 中档
                elif follow_distance == 4:
                    ret.stoppingDecelRate = 0.3  # 超远档：更保守
                else:  # 3=远档（默认）
                    ret.stoppingDecelRate = 0.5
            except:
                ret.stoppingDecelRate = 0.5  # 默认值
            
            ret.vEgoStarting = 0.2 * CV.KPH_TO_MS
            ret.vEgoStopping = 0.1 * CV.KPH_TO_MS
        else:
            ret.dashcamOnly = True

        return ret

    @staticmethod
    def _get_params_sp(stock_cp: structs.CarParams, ret: structs.CarParamsSP, candidate,
                       fingerprint: dict[int, dict[int, int]], car_fw: list[structs.CarParams.CarFw],
                       alpha_long: bool, is_release_sp: bool, docs: bool) -> structs.CarParamsSP:
        return ret

    @staticmethod
    def init(CP, CP_SP, can_recv, can_send):
        pass

    @staticmethod
    def deinit(CP, can_recv, can_send):
        pass
