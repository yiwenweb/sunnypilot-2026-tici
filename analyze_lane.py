"""Analyze lane center offset from rlog modelV2: does the car sit at lane center?"""
import sys
import os
import numpy as np

sys.path.insert(0, r"D:\1\sunnypilot-2026-02")
import importlib.util
spec = importlib.util.spec_from_file_location("al", r"D:\1\sunnypilot-2026-02\analyze_lat.py")
al = importlib.util.module_from_spec(spec)
spec.loader.exec_module(al)

import capnp
capnp_log = capnp.load(os.path.join(al.CEREAL_DIR, "log.capnp"), imports=[al.CEREAL_DIR, al.OPENDBC_CAR])

def collect_mv(seg_path):
    try:
        with open(seg_path, "rb") as f:
            z = __import__("zstandard").ZstdDecompressor()
            reader = z.stream_reader(f)
            data = reader.read()
    except Exception as e:
        print(f"open fail {seg_path}: {e}")
        return None
    t0 = None
    mv_t, mv_lc, mv_py0, mv_px, mv_pathy0 = [], [], [], [], []
    try:
        events = capnp_log.Event.read_multiple_bytes(bytes(data))
        for ev in events:
            which = ev.which
            if which == "modelV2":
                mv = ev.modelV2
                if t0 is None:
                    t0 = ev.logMonoTime
                tt = (ev.logMonoTime - t0) / 1e9
                mv_t.append(tt)
                try:
                    ll = mv.laneLines
                    probs = list(mv.laneLineProbs) if len(mv.laneLineProbs) else []
                    if len(ll) >= 3 and len(probs) >= 3:
                        lly = np.array(ll[1].y)
                        rly = np.array(ll[2].y)
                        if len(lly) > 0 and len(rly) > 0:
                            lc = (lly[0] + rly[0]) / 2.0
                        else:
                            lc = float("nan")
                        conf = min(probs[1], probs[2])
                    else:
                        lc, conf = float("nan"), 0.0
                    mv_lc.append((lc, conf))
                except Exception:
                    mv_lc.append((float("nan"), 0.0))
                try:
                    py = np.array(mv.position.y)
                    px = np.array(mv.position.x)
                    if len(py) > 0 and len(px) > 0:
                        mv_py0.append(float(py[0]))
                        mv_px.append(float(px[0]))
                    else:
                        mv_py0.append(float("nan"))
                        mv_px.append(float("nan"))
                    mv_pathy0.append(float(py[0]) if len(py) > 0 else float("nan"))
                except Exception:
                    mv_py0.append(float("nan"))
                    mv_px.append(float("nan"))
                    mv_pathy0.append(float("nan"))
    except Exception as e:
        pass
    return {"t": np.array(mv_t), "lc": mv_lc, "py0": np.array(mv_py0), "pathy0": np.array(mv_pathy0)}

routes = sorted([d for d in os.listdir(al.REALDATA) if os.path.isdir(os.path.join(al.REALDATA, d)) and os.path.exists(os.path.join(al.REALDATA, d, "rlog.zst"))])
all_lc, all_conf, all_py0 = [], [], []
seg_stat = []
for r in routes:
    res = al.collect(os.path.join(al.REALDATA, r, "rlog.zst"))
    if res is None or len(res["car_t"]) == 0:
        continue
    mv = collect_mv(os.path.join(al.REALDATA, r, "rlog.zst"))
    if mv is None or len(mv["t"]) == 0:
        continue
    t = res["cs_t"]
    lat = np.interp(t, res["cc_t"], res["cc_lat"].astype(float)) > 0.5
    # modelV2 runs at 20Hz -> interp onto controls timeline
    lc_vals = np.array([v[0] for v in mv["lc"]])
    conf_vals = np.array([v[1] for v in mv["lc"]])
    lc_i = np.interp(t, mv["t"], np.nan_to_num(lc_vals, nan=0.0))
    conf_i = np.interp(t, mv["t"], conf_vals)
    py_i = np.interp(t, mv["t"], np.nan_to_num(mv["py0"], nan=0.0))
    m = lat & (conf_i > 0.5)
    if m.sum() < 50:
        continue
    lc_m = lc_i[m]
    seg_stat.append((r, lc_m.mean(), np.median(lc_m), (lc_m > 0.2).mean(), (lc_m < -0.2).mean(), m.sum()))
    all_lc.extend(lc_m.tolist())
    all_conf.extend(conf_i[m].tolist())
    all_py0.extend(py_i[m].tolist())

lc = np.array(all_lc)
py0 = np.array(all_py0)
print(f"segments with data: {len(seg_stat)}")
print(f"frames: {len(lc)}")
print("lane_center_offset (ll+rl)/2 @ y0  mean/median:", round(lc.mean(),3), round(np.median(lc),3))
print("  p10/p90:", np.percentile(lc,10).round(3), np.percentile(lc,90).round(3))
print("  |offset|>0.2m frac:", round((np.abs(lc)>0.2).mean(),3), " >0.3m:", round((np.abs(lc)>0.3).mean(),3))
print("  offset>+0.2 (car right of center) frac:", round((lc>0.2).mean(),3))
print("  offset<-0.2 (car left of center) frac:", round((lc<-0.2).mean(),3))
print("model path y[0] mean/median:", round(py0.mean(),3), round(np.median(py0),3), " |py0|>0.2:", round((np.abs(py0)>0.2).mean(),3))
print()
print("=== worst segments (mean |offset|) ===")
for r, mn, md, rgt, lft, n in sorted(seg_stat, key=lambda x: abs(x[1]), reverse=True)[:12]:
    print(f"{r}: mean={mn:+.3f} med={md:+.3f} right>0.2={rgt:.2f} left<-0.2={lft:.2f} frames={n}")
