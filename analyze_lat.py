"""Analyze BYD straight-line lane departure from rlogs. Scan all segments, find
candidate episodes (high sustained error / frozen integrator / LOCK7-style cmd>150 & MainTq<10)."""
import os
import sys
import warnings
import numpy as np
import capnp
import zstandard as zstd

warnings.filterwarnings("ignore")

REALDATA = r"C:\Users\69265\Desktop\realdata"
CEREAL_DIR = r"D:\1\sunnypilot-2026-02\openpilot\cereal"
OPENDBC_CAR = r"D:\1\sunnypilot-2026-02\opendbc_repo\opendbc\car"

capnp_log = capnp.load(os.path.join(CEREAL_DIR, "log.capnp"), imports=[CEREAL_DIR, OPENDBC_CAR])

def iter_events(dat):
    try:
        for e in capnp_log.Event.read_multiple_bytes(dat):
            try:
                yield e
            except Exception:
                continue
    except Exception:
        pass

def load_segment(seg_path):
    with open(seg_path, "rb") as f:
        dat = zstd.ZstdDecompressor().stream_reader(f).read()
    return dat

def collect(seg_path):
    """Collect signals from one rlog segment. Returns dict of arrays (t in sec from seg start)."""
    dat = load_segment(seg_path)
    cs_t, cs_sat, cs_err, cs_p, cs_i, cs_d, cs_f, cs_out, cs_dcurv = [], [], [], [], [], [], [], [], []
    car_t, car_vego, car_angle, car_pressed, car_steer_tq, car_steer_tq_eps = [], [], [], [], [], []
    cc_t, cc_torque, cc_accel, cc_lat = [], [], [], []
    can_list = []

    for e in iter_events(dat):
        try:
            t = e.which()
        except Exception:
            continue
        try:
            if t == "controlsState":
                cs = e.controlsState
                try:
                    ts_ = cs.lateralControlState.torqueState
                    sat = bool(ts_.saturated)
                    err = float(ts_.error)
                    p = float(ts_.p)
                    i = float(ts_.i)
                    d = float(ts_.d)
                    f = float(ts_.f)
                    out = float(ts_.output)
                except Exception:
                    sat = err = p = i = d = f = out = np.nan
                try:
                    dcurv = float(cs.desiredCurvature)
                except Exception:
                    dcurv = np.nan
                cs_t.append(e.logMonoTime)
                cs_sat.append(sat); cs_err.append(err)
                cs_p.append(p); cs_i.append(i); cs_d.append(d); cs_f.append(f); cs_out.append(out)
                cs_dcurv.append(dcurv)
            elif t == "carState":
                cs = e.carState
                car_t.append(e.logMonoTime)
                car_vego.append(float(cs.vEgo))
                car_angle.append(float(cs.steeringAngleDeg))
                car_pressed.append(bool(cs.steeringPressed))
                car_steer_tq.append(float(cs.steeringTorque))
                try:
                    car_steer_tq_eps.append(float(cs.steeringTorqueEps))
                except Exception:
                    car_steer_tq_eps.append(np.nan)
            elif t == "carControl":
                cc = e.carControl
                cc_t.append(e.logMonoTime)
                cc_torque.append(float(cc.actuators.torque))
                cc_accel.append(float(cc.actuators.accel))
                try:
                    cc_lat.append(bool(cc.latActive))
                except Exception:
                    cc_lat.append(False)
            elif t == "can":
                for c in e.can:
                    can_list.append((e.logMonoTime, c.address, c.src, bytes(c.dat)))
        except Exception:
            continue

    if not cs_t:
        return None

    t0 = cs_t[0]
    def norm(arr): return [(x - t0) / 1e9 for x in arr]
    return {
        "cs_t": np.array(norm(cs_t)), "cs_sat": np.array(cs_sat),
        "cs_err": np.array(cs_err), "cs_p": np.array(cs_p), "cs_i": np.array(cs_i),
        "cs_d": np.array(cs_d), "cs_f": np.array(cs_f), "cs_out": np.array(cs_out),
        "cs_dcurv": np.array(cs_dcurv),
        "car_t": np.array(norm(car_t)), "car_vego": np.array(car_vego), "car_angle": np.array(car_angle),
        "car_pressed": np.array(car_pressed), "car_steer_tq": np.array(car_steer_tq),
        "car_steer_tq_eps": np.array(car_steer_tq_eps),
        "cc_t": np.array(norm(cc_t)), "cc_torque": np.array(cc_torque), "cc_accel": np.array(cc_accel),
        "cc_lat": np.array(cc_lat),
        "can": can_list, "t0": t0,
    }

def find_episodes(res):
    """Find candidate straight-line lane-departure episodes."""
    t = res["cs_t"]
    err = res["cs_err"]
    i = res["cs_i"]
    sat = res["cs_sat"]
    dcurv = res["cs_dcurv"]
    ve = np.interp(t, res["car_t"], res["car_vego"]) if len(res["car_t"]) else np.zeros(len(t))
    an = np.interp(t, res["car_t"], res["car_angle"]) if len(res["car_t"]) else np.zeros(len(t))
    lat = np.interp(t, res["cc_t"], res["cc_lat"].astype(float)) > 0.5 if len(res["cc_t"]) else np.zeros(len(t), dtype=bool)

    # active driving, speed > 8 m/s
    active = lat & (ve > 8)
    # Episodes: active & |error| > 0.15 sustained >= 2s (regardless of road shape)
    cond = active & (np.abs(err) > 0.15)
    eps = []
    start = None
    for i_ in range(len(t)):
        if cond[i_] and start is None:
            start = i_
        elif not cond[i_] and start is not None:
            if t[i_] - t[start] >= 2.0:
                eps.append((start, i_))
            start = None
    if start is not None and t[-1] - t[start] >= 2.0:
        eps.append((start, len(t)))

    out = []
    for (a, b) in eps:
        seg_i = i[a:b]
        i_move = float(seg_i[-1] - seg_i[0])
        i_frozen = np.max(np.abs(np.diff(seg_i))) < 0.005
        out.append({
            "t0": float(t[a]), "t1": float(t[b]),
            "mean_err": float(np.mean(np.abs(err[a:b]))),
            "sign_err": float(np.sign(np.mean(err[a:b]))),
            "max_err": float(np.max(np.abs(err[a:b]))),
            "i_move": i_move, "i_frozen": i_frozen,
            "sat_frac": float(np.mean(sat[a:b])),
            "mean_vego": float(np.mean(ve[a:b])),
            "mean_angle": float(np.mean(an[a:b])),
            "mean_dcurv": float(np.mean(dcurv[a:b])),
            "mean_cmd": float(np.mean(np.interp(t[a:b], res["cc_t"], res["cc_torque"]))) if len(res["cc_t"]) else 0.0,
        })
    return out, (t, active)

def lock7_candidates(res):
    """From CAN layer: cmd(790 bus0) > 150 & MainTq(792) < 10 sustained >= 5 frames (0.1s)."""
    # decode CAN with opendbc
    from opendbc.can.dbc import DBC
    from opendbc.can.parser import get_raw_value
    dbc = DBC("byd_tang_dm_2018")
    def sig(addr, name):
        m = dbc.addr_to_msg.get(addr)
        if m is None or name not in m.sigs:
            return None
        return m.sigs[name]
    s_out = sig(0x316, "LKAS_Output")
    s_mt = sig(0x318, "MainTorque")
    t0 = res["t0"]
    c_times, cmds = [], []
    m_times, mts = [], []
    for (tns, addr, src, dat) in res["can"]:
        if len(dat) < 8:
            continue
        tt = (tns - t0) / 1e9
        if addr == 0x316 and src == 0 and s_out is not None:
            v = get_raw_value(dat, s_out)
            if s_out.is_signed:
                v -= ((v >> (s_out.size - 1)) & 1) * (1 << s_out.size)
            c_times.append(tt); cmds.append(v * s_out.factor)
        elif addr == 0x318 and src == 0 and s_mt is not None:
            v = get_raw_value(dat, s_mt)
            if s_mt.is_signed:
                v -= ((v >> (s_mt.size - 1)) & 1) * (1 << s_mt.size)
            m_times.append(tt); mts.append(v * s_mt.factor)
    if not c_times or not m_times:
        return []
    # interpolate MainTq onto cmd timeline (both 50Hz-ish, close enough)
    times = np.array(c_times); cmds = np.array(cmds)
    mt_arr = np.interp(times, np.array(m_times), np.array(mts))
    cond = (np.abs(cmds) > 150) & (np.abs(mt_arr) < 10)
    eps = []
    start = None
    for i_ in range(len(times)):
        if cond[i_] and start is None:
            start = i_
        elif not cond[i_] and start is not None:
            if times[i_] - times[start] >= 0.1:
                eps.append((float(times[start]), float(times[i_]), float(np.mean(np.abs(cmds[start:i_]))), float(np.mean(np.abs(mt_arr[start:i_])))))
            start = None
    if start is not None and times[-1] - times[start] >= 0.1:
        eps.append((float(times[start]), float(times[-1]), float(np.mean(np.abs(cmds[start:]))), float(np.mean(np.abs(mt_arr[start:])))))
    return eps

if __name__ == "__main__":
    routes = sorted([d for d in os.listdir(REALDATA) if os.path.isdir(os.path.join(REALDATA, d))])
    ep_summary = []
    lk7_summary = []
    for r in routes:
        rlog = os.path.join(REALDATA, r, "rlog.zst")
        if not os.path.exists(rlog):
            continue
        try:
            res = collect(rlog)
        except Exception as ex:
            print(f"{r}: collect ERR {ex}", file=sys.stderr)
            continue
        if res is None:
            continue
        eps, _ = find_episodes(res)
        lk7 = lock7_candidates(res)
        dur = float(res["cs_t"][-1] - res["cs_t"][0])
        for ep in eps:
            ep_summary.append((r, dur, ep))
            print(f"[EP] {r} dur={dur:.0f}s t=[{ep['t0']:.1f},{ep['t1']:.1f}] |err|={ep['mean_err']:.3f} max={ep['max_err']:.3f} sign={ep['sign_err']:+.0f} i_move={ep['i_move']:+.4f} i_frozen={ep['i_frozen']} sat={ep['sat_frac']:.2f} v={ep['mean_vego']:.1f} ang={ep['mean_angle']:+.2f} dcurv={ep['mean_dcurv']:+.5f} cmd={ep['mean_cmd']:+.0f}")
        for (a, b, mc, mm) in lk7:
            lk7_summary.append((r, a, b, mc, mm))
            print(f"[LK7] {r} t=[{a:.1f},{b:.1f}] mean|cmd|={mc:.0f} mean|maintq|={mm:.1f}")
    print(f"\nTotal segments: {len(routes)}, EP episodes: {len(ep_summary)}, LK7 candidates: {len(lk7_summary)}")
