"""Deep analysis of a specific episode: dump full timeline of key signals."""
import sys
import os
import numpy as np
sys.path.insert(0, r"D:\1\sunnypilot-2026-02")
import importlib.util
spec = importlib.util.spec_from_file_location("al", r"D:\1\sunnypilot-2026-02\analyze_lat.py")
al = importlib.util.module_from_spec(spec)
spec.loader.exec_module(al)

from opendbc.can.dbc import DBC
from opendbc.can.parser import get_raw_value

SEG = sys.argv[1] if len(sys.argv) > 1 else "00000013--229afaa087--11"
T0 = float(sys.argv[2]) if len(sys.argv) > 2 else 35.0
T1 = float(sys.argv[3]) if len(sys.argv) > 3 else 39.0

res = al.collect(os.path.join(al.REALDATA, SEG, "rlog.zst"))
if res is None:
    print("collect failed"); sys.exit(1)

t = res["cs_t"]
mask = (t >= T0) & (t <= T1)

# interpolate carstate/cc onto controls timeline
ve = np.interp(t, res["car_t"], res["car_vego"])
an = np.interp(t, res["car_t"], res["car_angle"])
pr = np.interp(t, res["car_t"], res["car_pressed"].astype(float))
stq = np.interp(t, res["car_t"], res["car_steer_tq"])
ste = np.interp(t, res["car_t"], res["car_steer_tq_eps"])
cmd = np.interp(t, res["cc_t"], res["cc_torque"])

# CAN 790/792 in window
dbc = DBC("byd_tang_dm_2018")
def sig(addr, name):
    m = dbc.addr_to_msg.get(addr)
    return m.sigs[name] if m is not None and name in m.sigs else None
s_out = sig(0x316, "LKAS_Output"); s_mt = sig(0x318, "MainTorque")
s_dt = sig(0x318, "SteerDriverTorque"); s_cfg = sig(0x316, "LKAS_Config")
s_prep = sig(0x318, "LKAS_Prepared")
t0ns = res["t0"]
can_rows = []
for (tns, addr, src, dat) in res["can"]:
    if len(dat) < 8:
        continue
    tt = (tns - t0ns) / 1e9
    if tt < T0 - 0.5 or tt > T1 + 0.5:
        continue
    if addr == 0x316 and src == 0 and s_out is not None:
        v = get_raw_value(dat, s_out)
        if s_out.is_signed: v -= ((v >> (s_out.size-1)) & 1) * (1 << s_out.size)
        can_rows.append((tt, "LKAS_Out(bus0)", v * s_out.factor))
    elif addr == 0x316 and src == 2 and s_out is not None:
        v = get_raw_value(dat, s_out)
        if s_out.is_signed: v -= ((v >> (s_out.size-1)) & 1) * (1 << s_out.size)
        can_rows.append((tt, "LKAS_Out(bus2cam)", v * s_out.factor))
    elif addr == 0x318 and src == 0 and s_mt is not None:
        mt = get_raw_value(dat, s_mt)
        if s_mt.is_signed: mt -= ((mt >> (s_mt.size-1)) & 1) * (1 << s_mt.size)
        dt = get_raw_value(dat, s_dt) if s_dt else 0
        if s_dt and s_dt.is_signed: dt -= ((dt >> (s_dt.size-1)) & 1) * (1 << s_dt.size)
        pp = get_raw_value(dat, s_prep) if s_prep else -1
        can_rows.append((tt, f"EPS MT={mt:.0f} DT={dt:.0f} Prep={pp}", mt))

print(f"=== {SEG} t=[{T0},{T1}] ===")
print("t  vEgo  ang  dcurv   err    p      i      f    output  cmd  pressed steerTq epsTq")
for i_ in np.where(mask)[0]:
    print(f"{t[i_]:6.2f} {ve[i_]:5.1f} {an[i_]:6.2f} {res['cs_dcurv'][i_]:+.5f} {res['cs_err'][i_]:+.3f} "
          f"{res['cs_p'][i_]:+.3f} {res['cs_i'][i_]:+.3f} {res['cs_f'][i_]:+.3f} {res['cs_out'][i_]:+.3f} "
          f"{cmd[i_]:+.3f} {pr[i_]:.0f} {stq[i_]:+.0f} {ste[i_]:+.0f}")

print("\n=== CAN 790/792 in window ===")
for (tt, desc, val) in sorted(can_rows):
    print(f"{tt:6.2f} {desc}")
