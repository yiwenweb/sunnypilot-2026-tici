#!/usr/bin/env python3
"""
Direct evdev touchscreen backend.

On some devices (e.g. domestic C3 AGNOS builds) the compositor input stack
(weston/libinput) is broken, so the raylib-based UI receives no touch events
at all. This module reads multitouch events straight from /dev/input/eventX
and reports per-slot state in *logical display coordinates*, bypassing
weston entirely (same idea as Qt's evdevtouch generic plugin).

Environment:
  SP_TOUCH_DEVICE   input node to read (default /dev/input/event2)
  SP_TOUCH_ROTATE   rotation applied to raw panel coords: 0/90/180/270 (default 270)
  SP_TOUCH_DISABLE  set to 1 to force-disable this backend
"""

import fcntl
import os
import select
import struct
import threading

from openpilot.common.swaglog import cloudlog

EV_SYN = 0x00
EV_ABS = 0x03
SYN_REPORT = 0x00
ABS_MT_SLOT = 0x2F
ABS_MT_POSITION_X = 0x35
ABS_MT_POSITION_Y = 0x36
ABS_MT_TRACKING_ID = 0x39

EVENT_FMT = "llHHi"
EVENT_SIZE = struct.calcsize(EVENT_FMT)
ABSINFO_FMT = "iiiiii"
ABSINFO_SIZE = struct.calcsize(ABSINFO_FMT)


def _eviocgabs(code: int) -> int:
  return (2 << 30) | (ABSINFO_SIZE << 16) | (ord("E") << 8) | (0x40 + code)


class _Slot:
  __slots__ = ("x", "y", "down", "seen", "dx", "dy")

  def __init__(self):
    self.x = 0.0
    self.y = 0.0
    self.down = False
    self.seen = False
    self.dx = 0.0
    self.dy = 0.0


class EvdevTouch:
  """Reads MT protocol B (with a minimal protocol A fallback) from an evdev node."""

  def __init__(self, device: str, width: int, height: int, rotate: int, max_slots: int):
    self._width = width
    self._height = height
    self._rotate = rotate % 360
    self._max_slots = max_slots
    self._fd = os.open(device, os.O_RDONLY | os.O_NONBLOCK)
    self._x_min, self._x_max = self._abs_range(ABS_MT_POSITION_X)
    self._y_min, self._y_max = self._abs_range(ABS_MT_POSITION_Y)
    self._slots = [_Slot() for _ in range(max_slots)]
    self._cur = 0
    self._mt_in_frame = False
    self._lock = threading.Lock()
    self._exit = threading.Event()
    self._thread = threading.Thread(target=self._run, daemon=True)
    self._thread.start()
    cloudlog.info(f"evdev touch: reading {device} rotate={self._rotate} "
                  f"x=({self._x_min},{self._x_max}) y=({self._y_min},{self._y_max})")

  def _abs_range(self, code: int) -> tuple[int, int]:
    try:
      buf = fcntl.ioctl(self._fd, _eviocgabs(code), b"\x00" * ABSINFO_SIZE)
      _, mn, mx, _, _, _ = struct.unpack(ABSINFO_FMT, buf)
      if mx > mn:
        return mn, mx
    except OSError:
      pass
    cloudlog.warning(f"evdev touch: EVIOCGABS failed for code {code}, assuming 0..4095")
    return 0, 4095

  @staticmethod
  def from_env(width: int, height: int, max_slots: int) -> "EvdevTouch | None":
    if os.getenv("SP_TOUCH_DISABLE") == "1":
      return None
    device = os.getenv("SP_TOUCH_DEVICE", "/dev/input/event2")
    try:
      rotate = int(os.getenv("SP_TOUCH_ROTATE", "270"))
    except ValueError:
      rotate = 270
    if not os.path.exists(device):
      cloudlog.warning(f"evdev touch: {device} not found, falling back to raylib input")
      return None
    try:
      return EvdevTouch(device, width, height, rotate, max_slots)
    except OSError as e:
      cloudlog.warning(f"evdev touch: failed to open {device}: {e}")
      return None

  def stop(self) -> None:
    self._exit.set()
    if self._thread.is_alive():
      self._thread.join(timeout=1.0)
    try:
      os.close(self._fd)
    except OSError:
      pass

  def poll(self) -> list[tuple[int, float, float, bool]]:
    """Current state of all seen slots as (slot, x, y, down) in display coords."""
    with self._lock:
      return [(i, s.dx * self._width, s.dy * self._height, s.down)
              for i, s in enumerate(self._slots) if s.seen]

  def _run(self) -> None:
    while not self._exit.is_set():
      r, _, _ = select.select([self._fd], [], [], 0.2)
      if not r:
        continue
      try:
        data = os.read(self._fd, EVENT_SIZE * 64)
      except OSError:
        continue
      for i in range(0, len(data) - EVENT_SIZE + 1, EVENT_SIZE):
        _, _, typ, code, value = struct.unpack_from(EVENT_FMT, data, i)
        if typ == EV_ABS:
          self._abs(code, value)
        elif typ == EV_SYN and code == SYN_REPORT:
          self._sync()

  def _abs(self, code: int, value: int) -> None:
    s = self._slots[self._cur]
    if code == ABS_MT_SLOT:
      if 0 <= value < self._max_slots:
        self._cur = value
    elif code == ABS_MT_TRACKING_ID:
      s.down = value != -1
      s.seen = True
      self._mt_in_frame = True
    elif code == ABS_MT_POSITION_X:
      s.x = self._norm(value, self._x_min, self._x_max)
      s.seen = True
      self._mt_in_frame = True
      if not s.down:  # protocol A fallback: positions imply contact
        s.down = True
    elif code == ABS_MT_POSITION_Y:
      s.y = self._norm(value, self._y_min, self._y_max)
      s.seen = True
      self._mt_in_frame = True
      if not s.down:
        s.down = True

  @staticmethod
  def _norm(v: int, mn: int, mx: int) -> float:
    return max(0.0, min(1.0, (v - mn) / (mx - mn)))

  def _sync(self) -> None:
    # protocol A lift heuristic: a SYN frame with no MT events ends the contact
    if not self._mt_in_frame:
      for s in self._slots:
        if s.down:
          s.down = False
    self._mt_in_frame = False
    with self._lock:
      for s in self._slots:
        s.dx, s.dy = self._rot(s.x, s.y)

  def _rot(self, nx: float, ny: float) -> tuple[float, float]:
    if self._rotate == 90:
      return 1.0 - ny, nx
    if self._rotate == 180:
      return 1.0 - nx, 1.0 - ny
    if self._rotate == 270:
      return ny, 1.0 - nx
    return nx, ny
