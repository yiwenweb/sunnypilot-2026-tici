"""
BYD MADS (Modular Autonomous Driving System) Implementation
横向单独控制 - 允许不开ACC也能车道保持

Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.
Adapted for BYD Tang DM 2018 by the BYD port maintainer.

This file is part of sunnypilot and is licensed under the MIT License.
See the LICENSE.md file in the root directory for more details.
"""

from enum import StrEnum
from collections import namedtuple

from opendbc.car import Bus, DT_CTRL, structs
from opendbc.sunnypilot.mads_base import MadsCarStateBase
from opendbc.can.parser import CANParser

ButtonType = structs.CarState.ButtonEvent.Type

MadsDataSP = namedtuple("MadsDataSP",
                        ["enable_mads", "lat_active", "disengaging", "paused"])


class MadsCarController:
  """
  BYD MADS 控制器 - 管理横向单独控制的状态和显示
  """
  def __init__(self):
    self.mads = MadsDataSP(False, False, False, False)

    self.lat_disengage_blink = 0
    self.lat_disengage_init = False
    self.prev_lat_active = False

  def mads_status_update(self, CC: structs.CarControl, CC_SP: structs.CarControlSP, frame: int) -> MadsDataSP:
    """
    更新 MADS 状态
    
    状态说明：
    - enable_mads: MADS功能是否可用（用户开启 + 车辆支持）
    - lat_active: 横向是否正在接管（绿色方向盘）
    - disengaging: 正在脱离接管（闪烁1秒提示）
    - paused: MADS已启用但横向未接管（白色方向盘，等待接管）
    """
    enable_mads = CC_SP.mads.available

    # 检测横向脱离沿（lat_active: 1->0）
    if CC.latActive:
      self.lat_disengage_init = False
    elif self.prev_lat_active:
      self.lat_disengage_init = True

    if not self.lat_disengage_init:
      self.lat_disengage_blink = frame

    # 计算状态
    paused = CC_SP.mads.enabled and not CC.latActive
    disengaging = (frame - self.lat_disengage_blink) * DT_CTRL < 1.0 if self.lat_disengage_init else False

    self.prev_lat_active = CC.latActive

    return MadsDataSP(enable_mads, CC.latActive, disengaging, paused)

  def create_lkas_icon(self, enabled: bool) -> int:
    """
    创建 LKAS 图标状态（BYD仪表盘显示）
    
    返回值：
    1 = 白色（MADS启用但未接管）
    2 = 绿色（正在接管）
    3 = 黄色闪烁（正在脱离）
    """
    if self.mads.enable_mads:
      # MADS模式：根据横向状态显示
      if self.mads.lat_active:
        return 2  # 绿色：横向接管中
      elif self.mads.disengaging:
        return 3  # 黄色闪烁：正在脱离
      else:
        return 1  # 白色：已启用但未接管
    else:
      # 传统模式：跟随enabled状态
      return 2 if enabled else 1

  def update(self, CC: structs.CarControl, CC_SP: structs.CarControlSP, frame: int) -> None:
    """每帧更新 MADS 状态"""
    self.mads = self.mads_status_update(CC, CC_SP, frame)


class MadsCarState(MadsCarStateBase):
  """
  BYD MADS 状态解析器 - 从 CAN 消息解析 MADS 相关状态
  
  注意：panda 层（safety/modes/byd.h）已经实现了：
  - acc_main_on 按钮检测（0x300:PCM_BUTTONS 的 bit8）
  - mads_button_press 状态更新
  - mads_state_update 调用
  
  这里只需消费 panda 传上来的状态即可。
  """
  def __init__(self, CP: structs.CarParams, CP_SP: structs.CarParams):
    super().__init__(CP, CP_SP)
    self.main_cruise_enabled: bool = False

  @staticmethod
  def get_parser(CP, CP_SP, pt_messages) -> None:
    """BYD 的 MADS 状态已在 panda 层处理，无需额外 CAN parser"""
    pass

  def get_main_cruise(self, ret: structs.CarState) -> bool:
    """
    获取主巡航开关状态（ACC ON/OFF 按钮）
    
    BYD 唐 DM 的 ACC 主开关是切换式（toggle），不是保持式。
    用户按一次开启，再按一次关闭。
    """
    # 检测 mainCruise 按钮按下事件
    if any(be.type == ButtonType.mainCruise and be.pressed for be in ret.buttonEvents):
      self.main_cruise_enabled = not self.main_cruise_enabled
    
    # 巡航不可用时强制关闭
    return self.main_cruise_enabled if ret.cruiseState.available else False

  def update_mads(self, ret: structs.CarState, can_parsers: dict[StrEnum, CANParser]) -> None:
    """
    更新 MADS 状态（每帧调用）
    
    BYD 的 MADS 实现依赖 panda 层的 mads_button_press 和 mads_state_update。
    panda 通过 alternative_experience 的 ALT_EXP_ENABLE_MADS 标志位启用 MADS，
    并自动处理 acc_main_on 按钮逻辑。
    
    Python 层只需确保 ret.cruiseState.available 正确即可。
    """
    # BYD 的 cruiseState.available 已在 carstate.py 的 update() 中设置
    # 这里无需额外处理
    pass
