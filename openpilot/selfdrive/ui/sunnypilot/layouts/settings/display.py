"""
Copyright (c) 2021-, Haibin Wen, sunnypilot, and a number of other contributors.

This file is part of sunnypilot and is licensed under the MIT License.
See the LICENSE.md file in the root directory for more details.
"""
import time
from enum import IntEnum

from openpilot.system.ui.widgets import Widget
from openpilot.system.ui.lib.multilang import tr
from openpilot.system.ui.widgets.scroller_tici import Scroller
from openpilot.system.ui.sunnypilot.widgets.list_view import toggle_item_sp, option_item_sp, button_item_sp
from openpilot.system.ui.lib.application import get_user_font_scale, set_user_font_scale, set_user_font_scale_preview
from openpilot.sunnypilot.system.params_migration import ONROAD_BRIGHTNESS_TIMER_VALUES


class OnroadBrightness(IntEnum):
  AUTO = 0
  AUTO_DARK = 1
  SCREEN_OFF = 2


class DisplayLayout(Widget):
  # (font size %, label) - cycled by the "Font Size" setting
  FONT_SIZE_LEVELS = (
    (80, "Small (80%)"),
    (100, "Standard (100%)"),
    (120, "Large (120%)"),
    (150, "X-Large (150%)"),
  )
  FONT_SIZE_CONFIRM_TIMEOUT = 5.0  # seconds to confirm a pending font size change

  def __init__(self):
    super().__init__()
    self._saved_font_percent: int | None = None
    self._pending_font_percent: int | None = None
    self._font_deadline = 0.0

    items = self._initialize_items()
    self._scroller = Scroller(items, line_separator=True, spacing=0)

  def _initialize_items(self):
    self._onroad_brightness = option_item_sp(
      param="OnroadScreenOffBrightness",
      title=lambda: tr("Onroad Brightness"),
      description="",
      min_value=0,
      max_value=22,
      value_change_step=1,
      label_callback=lambda value: self.update_onroad_brightness(value),
      inline=True
    )
    self._onroad_brightness_timer = option_item_sp(
      param="OnroadScreenOffTimer",
      title=lambda: tr("Onroad Brightness Delay"),
      description="",
      min_value=0,
      max_value=15,
      value_change_step=1,
      value_map=ONROAD_BRIGHTNESS_TIMER_VALUES,
      label_callback=lambda value: f"{value} s" if value < 60 else f"{int(value/60)} m",
      inline=True
    )
    self._interactivity_timeout = option_item_sp(
      param="InteractivityTimeout",
      title=lambda: tr("Interactivity Timeout"),
      description=lambda: tr("Apply a custom timeout for settings UI." +
                             "<br>This is the time after which settings UI closes automatically " +
                             "if user is not interacting with the screen."),
      min_value=0,
      max_value=120,
      value_change_step=10,
      label_callback=lambda value: (tr("Default") if not value or value == 0 else
                                    f"{value} s" if value < 60 else f"{int(value/60)} m"),
      inline=True
    )
    self._screensaver_toggle = toggle_item_sp(
      param="ScreenSaverEnabled",
      title=lambda: tr("Screen Saver"),
      description=lambda: tr("Show a screen saver when the device is offroad and idle, instead of turning the screen off."),
    )
    self._screensaver_timeout = option_item_sp(
      param="ScreenSaverTimeout",
      title=lambda: tr("Screen Saver Duration"),
      description=lambda: tr("How long the screen saver runs before the screen turns off."),
      min_value=60,
      max_value=600,
      value_change_step=60,
      label_callback=lambda value: f"{int(value/60)} m"
    )
    self._font_size_item = button_item_sp(
      title=lambda: tr("Font Size"),
      description=lambda: tr("Adjust the size of the text shown across the UI. Changing it applies an instant " +
                             "preview and shows a confirm button for 5 seconds; the change is kept only if " +
                             "confirmed, otherwise the previous size is restored."),
      button_text=self._font_size_button_text,
      callback=self._cycle_font_size,
    )
    self._font_size_confirm_item = button_item_sp(
      title="",
      description="",
      button_text=self._font_size_confirm_text,
      callback=self._confirm_font_size,
    )
    self._font_size_confirm_item.set_visible(False)

    items = [
      self._onroad_brightness,
      self._onroad_brightness_timer,
      self._interactivity_timeout,
      self._screensaver_toggle,
      self._screensaver_timeout,
      self._font_size_item,
      self._font_size_confirm_item,
    ]
    return items

  @staticmethod
  def update_onroad_brightness(val):
    if val == OnroadBrightness.AUTO:
      return tr("Auto (Default)")

    if val == OnroadBrightness.AUTO_DARK:
      return tr("Auto (Dark)")

    if val == OnroadBrightness.SCREEN_OFF:
      return tr("Screen Off")

    return f"{(val - 2) * 5} %"

  # ---------- Font size helpers ----------

  def _current_font_percent(self) -> int:
    if self._pending_font_percent is not None:
      return self._pending_font_percent
    if self._saved_font_percent is None:
      self._saved_font_percent = round(get_user_font_scale() * 100.0)
    return self._saved_font_percent

  def _font_size_label(self, percent: int) -> str:
    for value, label in self.FONT_SIZE_LEVELS:
      if value == percent:
        return tr(label)
    return f"{percent}%"

  def _font_size_button_text(self) -> str:
    return self._font_size_label(self._current_font_percent())

  def _font_size_confirm_text(self) -> str:
    remaining = max(0, int(self._font_deadline - time.monotonic()) + 1)
    return tr(f"Confirm ({remaining}s)")

  def _cycle_font_size(self):
    levels = self.FONT_SIZE_LEVELS
    current = self._current_font_percent()
    index = next((i for i, (value, _) in enumerate(levels) if value == current), 0)
    next_percent = levels[(index + 1) % len(levels)][0]
    self._begin_font_size_confirm(next_percent)

  def _begin_font_size_confirm(self, percent: int):
    if self._pending_font_percent is None:
      # Remember the persisted size the first time we enter the confirm window
      self._saved_font_percent = round(get_user_font_scale() * 100.0)
    self._pending_font_percent = percent
    self._font_deadline = time.monotonic() + self.FONT_SIZE_CONFIRM_TIMEOUT
    set_user_font_scale_preview(percent)  # instant global preview
    self._font_size_confirm_item.set_visible(True)

  def _confirm_font_size(self):
    if self._pending_font_percent is None:
      return
    set_user_font_scale(self._pending_font_percent)  # persist + apply immediately
    self._saved_font_percent = self._pending_font_percent
    self._pending_font_percent = None
    self._font_size_confirm_item.set_visible(False)

  def _abort_font_size(self):
    if self._pending_font_percent is None:
      return
    set_user_font_scale_preview(self._saved_font_percent)  # restore previous size
    self._pending_font_percent = None
    self._font_size_confirm_item.set_visible(False)

  def _update_state(self):
    super()._update_state()

    brightness_val = self._onroad_brightness.action_item.current_value
    self._onroad_brightness_timer.action_item.set_enabled(brightness_val not in (OnroadBrightness.AUTO, OnroadBrightness.AUTO_DARK))

    self._screensaver_timeout.set_visible(self._screensaver_toggle.action_item.get_state())

    # Non-blocking 5s confirm window for the font size change (single-threaded event loop)
    if self._pending_font_percent is not None and time.monotonic() >= self._font_deadline:
      self._abort_font_size()

  def _render(self, rect):
    self._scroller.render(rect)

  def show_event(self):
    self._scroller.show_event()
    # Drop any unconfirmed pending change and restore the persisted size when re-entering settings
    self._abort_font_size()

  def hide_event(self):
    super().hide_event()
    # Leaving the Display settings: roll back any unconfirmed font size preview
    self._abort_font_size()
