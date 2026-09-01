# BYD 唐 DM 跟车距离策略

> 本文档保留了原 `longcontrol_byd.py` 中经过门总实测标定的跟车距离策略。
> 该文件已删除（死代码），但策略仍有参考价值。

## 跟车距离档位（基于门总段25实测）

| 档位 | TTC 基准 | 用途 | 参数值 |
|---|---|---|---|
| **CLOSE（近）** | 2.5s | 运动/激进驾驶 | `BydFollowDistance=1` |
| **MEDIUM（中）** | 3.5s | 舒适驾驶 | `BydFollowDistance=2` |
| **FAR（远）** | 4.5s | **默认/门总实测** | `BydFollowDistance=3` ⭐ |
| **EXTRA_FAR（超远）** | 6.0s | 保守/新手 | `BydFollowDistance=4` |

## TTC 随速度分段调整（门总实测策略）

基于门总实测数据，跟车距离不是固定TTC，而是随速度动态调整：

```python
# 速度分段倍数（相对基准TTC）
低速 (0-2 m/s):   TTC × 1.5   # 超保守，防追尾
中低速 (2-5 m/s):  TTC × 0.9   # 略紧凑
中速 (5-9 m/s):   TTC × 0.75  # 紧凑跟车
中高速 (9-15 m/s): TTC × 1.0   # 基准
高速 (>15 m/s):   TTC × 1.0   # 稳定
```

**实际跟车距离计算**：
```
距离 = (TTC基准 × 速度修正倍数 × 自车速度) + 静止安全距离(3m)
```

## 门总实测标定数据（段25）

- **TTC 基准**: 4.0s（中位值）
- **TTC 范围**: 2.9s ~ 6.2s
- **加速度**: +1.38 m/s² / -2.03 m/s²
- **速度范围**: 0-18 m/s (0-65 km/h)

## 实现状态

### ✅ 已实现
- 参数定义：`BydFollowDistance` 已添加到 `params_keys.h`（默认值3）
- 4档调节：1=近/2=中/3=远/4=超远

### ⚠️ 待实现（可选）
1. **界面设置项**：在 sunnypilot UI 添加跟车距离调节选项
2. **实时切换**：按距离按钮时读取 `BydFollowDistance` 参数
3. **TTC分段**：在 `carcontroller.py` 或 `interface.py` 实现速度分段逻辑

## 为什么删除原代码？

1. **死代码**：`longcontrol_byd.py` 从未被调用（全仓库0引用）
2. **重复造轮子**：MPC+PID控制器与 sunnypilot 上层的通用 longitudinal_mpc 重复
3. **参数未接线**：`BydFollowDistance`/`BydComfortMode` 原本不存在，功能未生效
4. **上层已足够**：sunnypilot 的 `LongitudinalPersonality` 已实现三档调节（激进/标准/舒适）

## 参考：如何接入跟车距离调节

如需恢复跟车距离功能，建议在 `interface.py` 的 `get_params()` 中读取参数：

```python
from openpilot.common.params import Params

params = Params()
byd_distance = int(params.get("BydFollowDistance", encoding='utf-8') or "3")

# 映射到 TTC（秒）
ttc_map = {1: 2.5, 2: 3.5, 3: 4.5, 4: 6.0}
target_ttc = ttc_map.get(byd_distance, 4.5)

# 在 ret.longitudinalTuning 或其他地方使用 target_ttc
```

## 历史记录

- **2026-08-16**: 从 `longcontrol_byd.py` 提取策略，文件已删除
- **原作者**: BYD 唐 DM 移植团队
- **数据来源**: 门总段25实测（探针 probe_menmen_seg25）
