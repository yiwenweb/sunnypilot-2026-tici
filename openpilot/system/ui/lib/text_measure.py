import pyray as rl
from openpilot.system.ui.lib.application import get_font_scale, font_fallback

_cache: dict[int, rl.Vector2] = {}


def measure_text_cached(font: rl.Font, text: str, font_size: int, spacing: float = 0) -> rl.Vector2:
  """Caches text measurements to avoid redundant calculations."""
  font = font_fallback(font)
  spacing = round(spacing, 4)
  scale = get_font_scale()
  # Include the effective font scale in the cache key so measurements invalidate
  # automatically whenever the user font size changes.
  key = hash((font.texture.id, text, font_size, spacing, scale))
  if key in _cache:
    return _cache[key]

  result = rl.measure_text_ex(font, text, font_size * scale, spacing)  # noqa: TID251

  _cache[key] = result
  return result
