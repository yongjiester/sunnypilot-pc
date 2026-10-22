#!/usr/bin/env python3
"""Unit checks for PC UI auto-fit math (mirrors selfdrive/ui/qt/util.cc)."""
import math


DEVICE = (2160, 1080)


def compute_ui_fit_scale(avail_w: float, avail_h: float, dpr: float = 1.0) -> float:
  # availableGeometry is in points; multiply by DPR when HiDPI will be cancelled
  phys_w = avail_w * dpr
  phys_h = avail_h * dpr
  return min(phys_w / DEVICE[0], phys_h / DEVICE[1], 1.0)


def resolve_user_scale_or_fit(auto_fit: float, scale_env=None) -> float:
  if scale_env is None or scale_env == "auto":
    return auto_fit
  return float(scale_env)


def qt_scale_factor(dpr_inv: float) -> float:
  """QT_SCALE_FACTOR is Mac DPR cancel only; screen fit is ScaleHost / UI_FIT_SCALE."""
  return dpr_inv


def initial_window_size(fit: float):
  return (DEVICE[0] * fit, DEVICE[1] * fit)


def test_1080p_non_retina():
  fit = compute_ui_fit_scale(1920, 1080, dpr=1.0)
  assert math.isclose(fit, 1920 / 2160, rel_tol=1e-5)
  w, h = initial_window_size(fit)
  assert math.isclose(w, 1920, rel_tol=1e-5)
  assert math.isclose(h, 960, rel_tol=1e-5)
  # Global Qt scale stays 1 on non-Retina; ScaleHost fitInView does the shrink
  assert qt_scale_factor(1.0) == 1.0


def test_1440p():
  fit = compute_ui_fit_scale(2560, 1440, dpr=1.0)
  assert fit == 1.0  # larger than design -> do not upscale past 1x


def test_retina_cancel_then_fit():
  # Logical points 1512x982 at DPR 2 -> physical 3024x1964 -> fit capped at 1
  fit = compute_ui_fit_scale(1512, 982, dpr=2.0)
  assert fit == 1.0
  assert math.isclose(qt_scale_factor(0.5), 0.5)


def test_retina_small_external_1080p_points():
  # Some Mac setups report 1080p external as 1920x1080 @ dpr 1
  fit = compute_ui_fit_scale(1920, 1000, dpr=1.0)  # minus menu/dock
  assert fit < 1.0
  _, h = initial_window_size(fit)
  assert h <= 1000 + 1e-3


def test_scale_env_override():
  auto = compute_ui_fit_scale(1920, 1080)
  assert resolve_user_scale_or_fit(auto, None) == auto
  assert resolve_user_scale_or_fit(auto, "auto") == auto
  assert resolve_user_scale_or_fit(auto, "1") == 1.0
  assert resolve_user_scale_or_fit(auto, "0.75") == 0.75


def test_no_double_scale():
  # Fit must not be multiplied into QT_SCALE_FACTOR (that clipped settings UI).
  fit = compute_ui_fit_scale(1920, 1080, dpr=1.0)
  assert qt_scale_factor(1.0) * fit == fit
  assert qt_scale_factor(1.0) == 1.0


if __name__ == "__main__":
  test_1080p_non_retina()
  test_1440p()
  test_retina_cancel_then_fit()
  test_retina_small_external_1080p_points()
  test_scale_env_override()
  test_no_double_scale()
  print("test_ui_fit_scale: ok")
