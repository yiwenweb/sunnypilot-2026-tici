"""Analyze lane_center_offset per segment/route to detect camera-mount bias drift.
For each rlog.zst: sample lane_center_offset (gated: lane probs>0.5, latActive),
report median/IQR per segment, grouped by route + file mtime (mount-batch proxy).
"""
import os, sys, glob, numpy as np, zstandard
sys.path.insert(0, r'D:\1\sunnypilot-2026-02')
import importlib.util
spec = importlib.util.spec_from_file_location('al', r'D:\1\sunnypilot-2026-02\analyze_lat.py')
al = importlib.util.module_from_spec(spec)
spec.loader.exec_module(al)
import capnp
capnp_log = capnp.load(os.path.join(al.CEREAL_DIR, 'log.capnp'), imports=[al.CEREAL_DIR, al.OPENDBC_CAR])

ROWS = []
segs = sorted(glob.glob(os.path.join(al.REALDATA, '*', 'rlog.zst')))
for si, seg in enumerate(segs):
    mtime = os.path.getmtime(seg)
    offs = []
    try:
        with open(seg, 'rb') as f:
            data = zstandard.ZstdDecompressor().stream_reader(f).read()
        for ev in capnp_log.Event.read_multiple_bytes(bytes(data)):
            if ev.which == 'modelV2':
                mv = ev.modelV2
                ll = mv.laneLines
                probs = list(mv.laneLineProbs) if len(mv.laneLineProbs) else []
                if len(ll) < 3 or len(probs) < 3:
                    continue
                l, r = ll[1], ll[2]
                if len(l.y) == 0 or len(r.y) == 0:
                    continue
                if min(probs[1], probs[2]) < 0.5:
                    continue
                offs.append((l.y[0] + r.y[0]) / 2.0)
    except Exception as e:
        print(f'ERR {seg}: {e}', file=sys.stderr)
        continue
    if len(offs) < 50:
        continue
    a = np.array(offs)
    ROWS.append((os.path.basename(os.path.dirname(seg)), mtime, np.median(a), a.std(), np.percentile(a, 25), np.percentile(a, 75), len(a)))

print(f"{'segment':<22}{'mtime':<20}{'median':>8}{'IQR':>8}{'p25':>7}{'p75':>7}{'n':>8}")
print('-' * 80)
for r in sorted(ROWS, key=lambda x: x[1]):
    seg, mt, med, sd, p25, p75, n = r
    ts = __import__('datetime').datetime.fromtimestamp(mt).strftime('%m-%d %H:%M')
    print(f"{seg:<22}{ts:<20}{med:>8.3f}{p75-p25:>8.3f}{p25:>7.3f}{p75:>7.3f}{n:>8}")

# route-level grouping (same route = same mount session)
from collections import OrderedDict
routes = OrderedDict()
for seg, mt, med, sd, p25, p75, n in ROWS:
    route = seg.rsplit('--', 1)[0]
    routes.setdefault(route, []).append((seg, mt, med, sd, p25, p75, n))
print()
print(f"routes: {len(routes)}, segments: {len(ROWS)}")
print(f"{'route':<20}{'nseg':>5}{'median range':>18}{'span':>8}")
print('-' * 56)
for route, rs in routes.items():
    meds = [r[2] for r in rs]
    span = max(meds) - min(meds)
    first = __import__('datetime').datetime.fromtimestamp(min(r[1] for r in rs)).strftime('%m-%d')
    last = __import__('datetime').datetime.fromtimestamp(max(r[1] for r in rs)).strftime('%m-%d')
    print(f"{route:<20}{len(rs):>5}{min(meds):>8.3f}~{max(meds):>8.3f}{span:>8.3f}  ({first}~{last})")

# overall distribution of per-segment medians -> mount bias variability
meds = np.array([r[2] for r in ROWS])
print()
print(f"per-segment median: min={meds.min():.3f} p25={np.percentile(meds,25):.3f} med={np.median(meds):.3f} p75={np.percentile(meds,75):.3f} max={meds.max():.3f}")
print(f"median spread (max-min): {meds.max()-meds.min():.3f} m  <- mount-batch variability evidence")

# ── Recommendation ──
print()
print("=" * 64)
print("CameraOffset 建议值（自动校准结果）")
print("=" * 64)
overall = float(np.median(meds))
suggest = round(-overall, 2)
print(f"1. 全部 {len(meds)} 段的中位偏移: {overall:+.3f} m   (负 = 车偏右)")
print(f"2. 建议 CameraOffset = {suggest:+.2f} m  ->  设置页填 {int(abs(suggest) * 100)} (正值=左移, 负值=右移)")
print(f"3. 数据跨度 {meds.max()-meds.min():.2f} m（横坡噪声），若跨度 > 0.3m 建议多收集几天数据再校准")
print()
print("使用方法：")
print("  a) 拆装 C3 后正常开几天（覆盖不同方向/路段），拷贝 rlog 到日志目录重跑本脚本")
print("  b) 设置页 设置→模型 里把 'Adjust Camera Offset' 填成上面建议值")
print("  c) 首次建议做方向标定：先填 +0.10 跑一段，若中位偏移变正（更偏左）说明方向正确；")
print("     若更偏右则把正负号反过来再试")
print("  d) 校正后再跑本脚本，中位偏移应趋近 0")
print("=" * 64)
