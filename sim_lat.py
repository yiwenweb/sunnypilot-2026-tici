"""Simulate lane-center correction loop (Plan A variants) with bicycle kinematics.
y: lateral offset from lane center (m, + = car right of center)
psi: heading (rad, + = heading right)
kappa: curvature (rad/m, + = turn right)
y'' ~ v^2 * kappa (lateral accel), psi' = v * kappa, y' = v * psi
Steering actuator lag: first-order tau=0.15s
"""
import numpy as np

DT = 0.01          # 100 Hz control
TAU = 0.15         # steering actuator lag
TAU_YAW = 0.4      # vehicle yaw relaxation (tire cornering damping)
V = 18.6           # m/s (67 km/h)
LOOK_F = 3.5
LOOK_MIN, LOOK_MAX = 25.0, 120.0
DEAD = 0.03
CLIP = 0.002
KP_COEFF = 0.36    # 0.3 weight * 1.2 gain

def lookahead(v):
    return max(min(v * LOOK_F, LOOK_MAX), LOOK_MIN)

def sim(v, y0, kappa_model_fn, ctrl, noise=0.0, T=90.0, seed=0):
    rng = np.random.default_rng(seed)
    la = lookahead(v)
    y, psi, ka = y0, 0.0, 0.0
    yf, yf_prev, ycmd = y0, y0, y0
    t, ys = 0.0, []
    while t < T:
        # measurement with noise
        y_meas = y + (rng.normal(0, noise) if noise > 0 else 0.0)
        # controller
        if ctrl == "V1":   # plain proportional (original plan A)
            off = y_meas if abs(y_meas) > DEAD else 0.0
            kfix = -KP_COEFF * off / (la ** 2)
        elif ctrl == "V2": # rate-limited reference (sp2025 style)
            target = 0.0 if abs(y_meas) > DEAD else ycmd
            rate = 0.05
            ycmd += np.clip(target - ycmd, -rate * DT, rate * DT)
            kfix = -KP_COEFF * ycmd / (la ** 2)
        elif ctrl == "V3": # filtered P + damping
            alpha = np.exp(-DT / 0.4)
            yf = alpha * yf + (1 - alpha) * y_meas
            ydot = (yf - yf_prev) / DT
            kp = KP_COEFF / (la ** 2)
            kd = 2 * np.sqrt(kp) / v   # critical damping approx
            off = yf if abs(yf) > DEAD else 0.0
            kfix = -kp * off - kd * ydot
        elif ctrl == "V4": # strong proportional (gain 1.2)
            off = y_meas if abs(y_meas) > DEAD else 0.0
            kfix = -1.2 * off / (la ** 2)
        elif ctrl == "V5": # strong proportional + damping (gain 1.2, critical damp)
            alpha = np.exp(-DT / 0.3)
            yf = alpha * yf + (1 - alpha) * y_meas
            ydot = (yf - yf_prev) / DT
            kp = 1.2 / (la ** 2)
            kd = 2 * np.sqrt(kp) / v
            off = yf if abs(yf) > DEAD else 0.0
            kfix = -kp * off - kd * ydot
        elif ctrl == "V6": # sp2025 exact replica: curvature_fix uses UNSMOOTHED target
            # target_right_offset = base*0.7 - lane_center_offset*0.3 (base~0 for self-centering model)
            # dead zone on |target|>0.03, K=1.2, clip by speed-dependent limit
            target = -0.3 * y_meas
            kfix = 0.0
            if abs(target) > 0.03:
                kfix = 1.2 * target / (la ** 2)
                kfix = np.clip(kfix, -np.interp(v, [0, 10, 20], [0.008, 0.006, 0.004]),
                               np.interp(v, [0, 10, 20], [0.008, 0.006, 0.004]))
        kfix = np.clip(kfix, -CLIP, CLIP)
        kcmd = kappa_model_fn(t) + kfix
        # actuator + kinematics (y relative to road center, road curvature = kappa_model)
        ka += (kcmd - ka) * DT / TAU
        # vehicle yaw relaxation (tire cornering damping, real car has this)
        psi += (v * (ka - kappa_model_fn(t)) - psi / TAU_YAW) * DT   # heading error vs road tangent
        y += v * psi * DT
        yf_prev = yf
        t += DT
        ys.append((t, y, ycmd if ctrl == "V2" else yf, kfix))
    return np.array(ys)

def report(name, v, y0, kappa_fn, ctrl, noise=0.0):
    ys = sim(v, y0, kappa_fn, ctrl, noise)
    t, y, ref, kf = ys[:, 0], ys[:, 1], ys[:, 2], ys[:, 3]
    # convergence: first time |y|<0.03 and stays <0.05 for 10s
    conv = None
    for i in range(0, len(y) - 1000, 1):
        if abs(y[i]) < 0.03 and np.max(np.abs(y[i:i + 1000])) < 0.05:
            conv = t[i]
            break
    osc = None
    # detect oscillation: sign changes of y after first crossing
    cross = np.where(np.diff(np.sign(y)) != 0)[0]
    if len(cross) > 3:
        periods = np.diff(t[cross[1:]])
        if len(periods) > 0:
            osc = periods[0]
    settle = np.abs(y[-1000:]).mean()
    peak = np.max(np.abs(y))
    dist = conv * v if conv else None
    cs = f"{conv:5.1f}s" if conv else "never"
    ds = f"{dist:6.0f}m" if dist else "   --"
    print(f"{name:34s} conv={cs} dist={ds} peak={peak:.3f} settle={settle:.3f} osc~{osc if osc else '-'}")
    return t, y

if __name__ == "__main__":
    straight = lambda t: 0.0
    curve_r = lambda t: 0.0015 if t > 2 else 0.0  # right turn 0.0015
    print(f"v={V} m/s lookahead={lookahead(V):.1f}m Kp={KP_COEFF/lookahead(V)**2:.2e}")
    print()
    report("S1 V1 纯比例 偏右10cm", V, 0.10, straight, "V1")
    report("S2 V2 速率限制 偏右10cm", V, 0.10, straight, "V2")
    report("S3 V3 阻尼0.36 偏右10cm", V, 0.10, straight, "V3")
    report("S4 V3 阻尼0.36 偏右30cm", V, 0.30, straight, "V3")
    report("S5 V3 阻尼0.36 右弯κ=0.0015", V, 0.10, curve_r, "V3")
    report("S6 V4 强比例1.2 偏右10cm", V, 0.10, straight, "V4")
    report("S7 V3 阻尼+噪声3cm 偏右10cm", V, 0.10, straight, "V3", noise=0.03)
    report("S8 V1 纯比例 偏右30cm", V, 0.30, straight, "V1")
    report("S9 V5 阻尼1.2 偏右10cm", V, 0.10, straight, "V5")
    report("S10 V5 阻尼1.2 偏右30cm", V, 0.30, straight, "V5")
    report("S11 V5 阻尼1.2 右弯 偏右10cm", V, 0.10, curve_r, "V5")
    report("S12 V5 阻尼1.2+噪声3cm 偏右10cm", V, 0.10, straight, "V5", noise=0.03)
    report("S13 V6 sp2025复刻 偏右10cm", V, 0.10, straight, "V6")
    report("S14 V6 sp2025复刻 偏右30cm", V, 0.30, straight, "V6")
    report("S15 V6 sp2025复刻 右弯 偏右10cm", V, 0.10, curve_r, "V6")
    report("S16 V6 sp2025复刻+噪声3cm 偏右10cm", V, 0.10, straight, "V6", noise=0.03)

    # dump S1 vs S3 for plotting
    for nm, ctrl in [("S1", "V1"), ("S3", "V3"), ("S9", "V5")]:
        ys = sim(V, 0.10, straight, ctrl)
        idx = np.arange(0, len(ys), 50)
        np.savetxt(f"sim_{nm}.csv", ys[idx], delimiter=",", header="t,y,ref,kfix")
    print("\nCSV dumped: sim_S1.csv / sim_S3.csv / sim_S6.csv")
