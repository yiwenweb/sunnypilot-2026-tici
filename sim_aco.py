# -*- coding: utf-8 -*-
"""Simulate runtime AutoCameraOffset calibration loop.
True mount+crown bias b=0.08m (car sits right of center).
Observed lane_center_offset = b - CameraOffset + segment crown noise.
Auto-cal writes CameraOffset every 60s-check from >=15min median, step 2cm, clamp 15cm.
Verify: convergence to b, obs median -> 0, no oscillation, safety under sign error.
"""
import random

def median(xs):
    s = sorted(xs)
    return s[len(s) // 2]

def clamp(v, lo, hi):
    return max(min(v, hi), lo)

def run(sign, hours=6.0, seed=1, b=0.08, win_min=30.0, dead=0.05, step=0.03, maxlen=72000,
        lr=0.15, persist=True):
    """Accumulating learner: every check, learned = (1-lr)*learned + lr*med (persisted across
    restarts in the real impl). cam steps toward -learned only when |learned| > dead.
    Robust to crown noise because the EWMA averages many windows (cross-day)."""
    random.seed(seed)
    fs = 20.0
    n = int(hours * 3600 * fs)
    cam = 0.0
    learned = 0.0                  # persisted learning state
    samples = []
    last_check = 0.0
    log = []
    seg_bias = 0.0
    seg_start = 0
    for i in range(n):
        t = i / fs
        if i - seg_start >= 5 * 60 * fs:
            seg_start = i
            seg_bias = random.uniform(-0.15, 0.15)
        obs = b + cam + seg_bias + random.gauss(0, 0.02)
        samples.append(obs)
        if len(samples) > maxlen:
            samples.pop(0)
        if t - last_check >= 60.0 and len(samples) >= win_min * 60 * fs:
            last_check = t
            med = median(samples)
            residual = med - cam          # bias NOT yet compensated by CameraOffset
            learned = (1.0 - lr) * learned + lr * residual
            if abs(learned) > dead:
                target = clamp(sign * -learned, cam - step, cam + step)
                target = clamp(target, -0.15, 0.15)
                cam = target
            samples.clear()
        if int(t) % 1200 == 0 and (not log or abs(t - log[-1][0]) > 60):
            log.append((t / 3600, cam, learned))
    return log, cam, learned

for name, sign in [("正常符号", 1), ("符号错误(反)", -1)]:
    log, final, learned = run(sign)
    print(f"== {name} ==  最终 cam={final:+.3f} learned={learned:+.3f} (目标 cam=-0.080)")
    for t, cam, lrnd in log[::8]:
        print(f"  t={t:.1f}h  cam={cam:+.3f}  learned={lrnd:+.3f}")

# statistical check
finals = [run(1, hours=6.0, seed=s)[1] for s in range(20)]
import statistics
print(f"\n20 次仿真(6h, 30min窗口+累积学习) 最终 CameraOffset: mean={statistics.mean(finals):+.3f} "
      f"min={min(finals):+.3f} max={max(finals):+.3f}  (目标 -0.080)")
ok = sum(1 for f in finals if abs(f - (-0.08)) < 0.03)
print(f"收敛到 ±3cm 内: {ok}/20")
