"""
Copyright (c) 2026, yiwenweb, sunnypilot BYD adaptation.

Lane-center correction + auto camera-offset status HUD widget.

Shows, at the top-center of the onroad UI (below the road name):
  - LaneCenterCorrection state: green dot + "居中修正 · 偏右 8cm" while the
    correction loop is actively pulling the car back to lane center; grey
    "居中修正 · 居中" when armed but centered; "无车道线" when no confident
    lines (mirrors controlsd gating: |offset| > 3cm dead zone).
  - CameraOffset state: current value + learning indicator, e.g.
    "相机偏移 +0.08m · 学习中" (blue) vs "相机偏移 +0.08m" (green, settled);
    manual (AutoCameraOffset off) shows grey "相机偏移 +0.08m（手动）".

Data: lane geometry from modelV2 (same lane_center_offset convention as
controlsd: y positive = left, negative = right), params read throttled at 2 Hz.
"""
import time

import pyray as rl

from openpilot.selfdrive.ui import UI_BORDER_SIZE
from openpilot.selfdrive.ui.onroad.driver_state import BTN_SIZE
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.ui.lib.application import gui_app, FontWeight
from openpilot.system.ui.lib.text_measure import measure_text_cached
from openpilot.system.ui.widgets import Widget

# ---- palette ----
COLOR_ACTIVE = rl.Color(0x2e, 0xcc, 0x71, 0xff)      # green: correcting / settled
COLOR_IDLE = rl.Color(0x91, 0x9b, 0x95, 0xff)         # grey: armed, centered
COLOR_LEARN = rl.Color(0x4a, 0xa8, 0xea, 0xff)        # blue: learning
COLOR_BG = rl.Color(0, 0, 0, 140)
COLOR_TEXT = rl.Color(255, 255, 255, 220)

DEAD_ZONE = 0.03   # same as controlsd lane center correction dead zone (m)
FONT_SIZE = 30


class LaneCenterStatusRenderer(Widget):
  def __init__(self):
    super().__init__()
    self._font_semi = gui_app.font(FontWeight.SEMI_BOLD)
    self._font_regular = gui_app.font(FontWeight.NORMAL)
    self._params_t = 0.0
    self._lc_enabled = False
    self._aco_enabled = False
    self._cam_offset = 0.0
    self._learned = 0.0
    self._offset = 0.0
    self._lines_ok = False
    self._correcting = False
    self._learning = False
    self._is_rhd = False

  def _read_params(self):
    now = time.monotonic()
    if now - self._params_t < 0.5:
      return
    self._params_t = now
    params = ui_state.params
    self._lc_enabled = params.get_bool("LaneCenterCorrection")
    self._aco_enabled = params.get_bool("AutoCameraOffset")
    try:
      self._cam_offset = float(params.get("CameraOffset", return_default=True) or 0.0)
      self._learned = float(params.get("AutoCamOffsetLearned", return_default=True) or 0.0)
    except (TypeError, ValueError):
      pass

  def update(self):
    sm = ui_state.sm
    if sm.recv_frame["carState"] < ui_state.started_frame:
      return

    self._read_params()

    if sm.recv_frame["modelV2"] < ui_state.started_frame:
      self._lines_ok = False
      self._correcting = False
      return

    dm = sm["driverMonitoringState"]
    self._is_rhd = dm.isRHD

    mv = sm["modelV2"]
    ll = mv.laneLines
    probs = list(mv.laneLineProbs) if len(mv.laneLineProbs) else []
    if len(ll) >= 3 and len(probs) >= 3 and min(probs[1], probs[2]) > 0.5 and \
       len(ll[1].y) > 0 and len(ll[2].y) > 0:
      self._offset = (ll[1].y[0] + ll[2].y[0]) / 2.0
      self._lines_ok = True
    else:
      self._lines_ok = False

    self._correcting = self._lc_enabled and self._lines_ok and abs(self._offset) > DEAD_ZONE
    # "学习中" = 还有未补偿完的残差 (learned 未收敛到 0)。收敛后 learned≈0 而
    # CameraOffset≈-learned 非零, 旧条件 abs(learned - cam_offset) 恒真 → 永远显示学习中。
    self._learning = self._aco_enabled and abs(self._learned) > 0.005

  def _render(self, rect: rl.Rectangle):
    if not (self._lc_enabled or self._aco_enabled):
      return

    rows = []
    if self._lc_enabled:
      if self._correcting:
        direction = "偏右" if self._offset < 0 else "偏左"
        rows.append((COLOR_ACTIVE, "居中修正", f"{direction} {abs(self._offset) * 100:.0f}cm", COLOR_ACTIVE))
      elif self._lines_ok:
        rows.append((COLOR_IDLE, "居中修正", "居中", COLOR_IDLE))
      else:
        rows.append((COLOR_IDLE, "居中修正", "无车道线", COLOR_IDLE))

    if self._aco_enabled:
      if abs(self._cam_offset) > 0.001:
        color = COLOR_LEARN if self._learning else COLOR_ACTIVE
        suffix = " · 学习中" if self._learning else ""
        rows.append((color, "相机偏移", f"{self._cam_offset:+.2f}m{suffix}", color))
      else:
        rows.append((COLOR_LEARN, "相机偏移", "收集中…", COLOR_LEARN))
    elif abs(self._cam_offset) > 0.001:
      rows.append((COLOR_IDLE, "相机偏移", f"{self._cam_offset:+.2f}m（手动）", COLOR_IDLE))

    if not rows:
      return

    row_h = FONT_SIZE + 12
    box_w = 0.0
    for dot_c, title, value, value_c in rows:
      dot_r = 6.0
      title_sz = measure_text_cached(self._font_semi, title, FONT_SIZE)
      value_sz = measure_text_cached(self._font_regular, value, FONT_SIZE)
      w = dot_r * 2 + 12 + title_sz.x + 12 + value_sz.x
      box_w = max(box_w, w)

    box_h = row_h * len(rows) + 16

    # Position: anchored to the driver-monitoring circle (bottom-left for LHD,
    # bottom-right for RHD), placed on its outside edge and vertically centered.
    offset = UI_BORDER_SIZE + BTN_SIZE // 2
    btn_cx = rect.x + (rect.width - offset if self._is_rhd else offset)
    btn_cy = rect.y + rect.height - offset
    gap = 16.0
    box_y = btn_cy - box_h / 2.0
    if self._is_rhd:
      box_x = btn_cx - BTN_SIZE / 2.0 - gap - box_w
    else:
      box_x = btn_cx + BTN_SIZE / 2.0 + gap
    box_x = max(box_x, rect.x + 10.0)

    rl.draw_rectangle_rounded(rl.Rectangle(box_x, box_y, box_w, box_h), 0.25, 10, COLOR_BG)

    y = box_y + 8
    for dot_c, title, value, value_c in rows:
      dot_r = 6.0
      title_sz = measure_text_cached(self._font_semi, title, FONT_SIZE)
      tx = box_x + dot_r * 2 + 12
      # DrawCircle takes (int centerX, int centerY, float radius, Color): box_x/y are
      # floats, so cast or pyray raises "TypeError: an integer is required"
      rl.draw_circle(int(box_x + dot_r), int(y + FONT_SIZE // 2 - 3), dot_r, dot_c)
      rl.draw_text_ex(self._font_semi, title, rl.Vector2(tx, y), FONT_SIZE, 0, COLOR_TEXT)
      rl.draw_text_ex(self._font_regular, value, rl.Vector2(tx + title_sz.x + 12, y), FONT_SIZE, 0, value_c)
      y += row_h
