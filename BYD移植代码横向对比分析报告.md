# BYD 移植代码横向对比分析报告

> 分析对象：`sunnypilot-2026` 中移植的 BYD 唐 DM 2018 车型代码
> 对比基准：丰田（Toyota）、现代起亚（Hyundai/Kia）、本田（Honda）、特斯拉（Tesla）、Rivian 等已实现车型
> 生成日期：2026-08-16

---

## 目录

1. [总体结论](#一总体结论)
2. [代码结构全景](#二代码结构全景)
3. [关键发现：死代码问题](#三关键发现-longcontrol_bydpy-是死代码)
4. [六大对比维度详解](#四六大对比维度详解)
5. [优先级建议清单](#五优先级建议清单)
6. [附录：关键文件对照表](#附录关键文件对照表)

---

## 一、总体结论

BYD 唐 DM 的移植在**底层逆向深度**上是整个仓库最高水平的——`carcontroller.py`（29KB）和 `values.py`（28KB）里那套 LOCK1~LOCK7 防锁死状态机、门总逐帧逆向的 EPS 握手序列、CAN 双源切换、checksum 算法、panda fail-safe 门控，其精细程度远超任何其他车型。

但与之形成强烈反差的，是你**完全没用上 sunnypilot 的通用抽象层和横向控制基础设施**，导致：

1. 一套 347 行的 MPC+PID 纵向控制器是**死代码**（从未被调用）
2. **MADS（横向单独控制）缺失**——这是 sunnypilot 的招牌功能，其他所有品牌都已接入
3. 纵向控制**无坡度补偿**（唐 DM 是 2.25 吨 SUV，坡道工况敏感）
4. sunnypilot 沉淀的通用组件（LeadData、ICBM、jerk-limited 纵向）零利用

**一句话总结**：逆向和控制安全做到了 120 分，但工程集成和功能完整性只做到了 60 分。

---

## 二、代码结构全景

### 2.1 BYD 车型实现文件清单

BYD 的实现全部位于 `opendbc_repo/opendbc/car/byd/`（`openpilot/selfdrive/car/byd/` 是空目录，sunnypilot 已把车型代码迁移到 opendbc_repo）。

| 文件 | 大小 | 作用 | 完成度 |
|---|---|---|---|
| `carcontroller.py` | 29.2 KB | 转向 + 纵向控制，含 LOCK1~7 防锁死状态机 | ★★★★★ |
| `values.py` | 28.2 KB | 平台参数 + 大量实测标定注释 | ★★★★★ |
| `carstate.py` | 14.69 KB | 状态解析（EPS 握手、ACC 状态、按钮） | ★★★★★ |
| `bydcan.py` | 8.89 KB | CAN 报文打包 + checksum 算法 | ★★★★★ |
| `interface.py` | 5.28 KB | 接口定义 + 驾驶风格绑定 | ★★★★☆ |
| `radar_interface.py` | 1.49 KB | 雷达接口（`BYD_RADAR` 环境变量开关） | ★★★☆☆ |
| `fingerprints.py` | 824 B | 指纹（仅 `BYD_TANG_DM`） | ★★★☆☆ |
| `__init__.py` | 0 B | — | — |

配套文件：
- `opendbc_repo/opendbc/dbc/byd_tang_dm_2018.dbc` — DBC 定义
- `opendbc_repo/opendbc/safety/modes/byd.h` — panda 安全模型（287 行，含 fail-safe 门控）
- `opendbc_repo/opendbc/safety/tests/test_byd.py` — 安全层单元测试
- `openpilot/selfdrive/controls/lib/longcontrol_byd.py` — **死代码**（见第三章）

### 2.2 仓库中已实现车型清单（17 个）

| 品牌 | 目录 | 说明 |
|---|---|---|
| **byd** | `byd/` | **唯一中国品牌**，仅 `BYD_TANG_DM` |
| toyota | `toyota/` | 12 文件，含 SecOC、坡度补偿 |
| hyundai | `hyundai/` | 13 文件 + 27 文件 sunnypilot 扩展，最完整 |
| honda | `honda/` | 11 文件 |
| gm / ford / subaru / nissan / mazda / psa / chrysler | 各自目录 | 完整实现 |
| rivian / tesla | 各自目录 | 新能源，含 CoopSteering（Tesla） |
| body / mock | 各自目录 | 测试用 |

> 搜索确认：除 BYD 外，**没有其他任何国产车品牌**（吉利/奇瑞/长城/蔚来/小鹏/理想等均无实现）。

### 2.3 sunnypilot 通用抽象层（BYD 未利用）

`opendbc_repo/opendbc/sunnypilot/car/` 下有各品牌的 sunnypilot 扩展：

```
opendbc/sunnypilot/car/
├── hyundai/     (27 文件：mads.py、lead_data_ext.py、longitudinal/、escc.py、icbm.py ...)
├── honda/       (8 文件)
├── toyota/      (gas_interceptor.py 等)
├── rivian/      (mads.py)
├── tesla/       (coop_steering.py)
├── chrysler/    (mads.py)
├── ford/ gm/ mazda/ nissan/ subaru/
├── mads_base.py            ← 通用 MADS 基类
├── intelligent_cruise_button_management_interface_base.py  ← ICBM 基类
└── (无 byd/)     ← ★ 缺失
```

**关键结论**：`sunnypilot/car/` 下没有 `byd/` 目录，说明 BYD 移植完全没有接入 sunnypilot 的通用扩展框架。

---

## 三、关键发现：`longcontrol_byd.py` 是死代码

这是本次分析最重要的发现。

### 3.1 事实

`openpilot/selfdrive/controls/lib/longcontrol_byd.py` 定义了 347 行的 `LongitudinalController` 类，包含：

- MPC+PID 混合控制（`compute_mpc_accel` / `compute_pid_accel`）
- 4 档跟车距离（`FollowDistance`：CLOSE/MEDIUM/FAR/EXTRA_FAR，TTC 2.5~6.0s）
- 3 档舒适模式（`ComfortMode`：ECO/COMFORT/SPORT）
- TTC 碰撞预警 + 紧急制动（`TTC_WARNING=1.5s`、`TTC_EMERGENCY=0.8s`）
- 刹停逻辑（creep + hold）

但通过全仓库搜索确认：

```python
# 唯一匹配点就是类定义本身
sunnypilot-2026/openpilot/selfdrive/controls/lib/longcontrol_byd.py:42:class LongitudinalController:
```

**没有任何 `from ... import LongitudinalController` 引用它**。

### 3.2 更严重的问题

它引用了两个**根本不存在的参数**：

```python
self.params.get("BydFollowDistance", ...)   # params_keys.h 中不存在
self.params.get("BydComfortMode", ...)      # params_keys.h 中不存在
```

`params_keys.h` 中搜索 `Byd`/`BYD` 均为 0 匹配。也就是说，即使有人 import 了这个类，运行时也会因参数不存在而静默回退到默认值（`except: pass`）。

### 3.3 实际生效的纵向控制

真正运行的纵向控制是 `carcontroller.py` 的 `acc_cmd`（433~490 行），逻辑非常薄：

1. `accel = clip(CC.actuators.accel, -3.5, 2.0)`
2. 0.5 m/s 速度滞环（`speed_hyst_upper`，防点刹震荡）
3. `rfss`/`sss` 刹停/起步状态位

**结论**：实际纵向控制是「sunnypilot 上层通用 MPC 输出目标加速度 → 直接透传给 CAN」，你写的 MPC+PID 控制器从未运行过。

> 注：这不是坏事——sunnypilot 上层的通用纵向 MPC（`longitudinal_mpc_lib/long_mpc.py`，支持 `LongitudinalPersonality`）已经很成熟，你的 `interface.py` 里也已经通过 `LongitudinalPersonality` 参数绑定了激进/标准/舒适三档调参。问题在于**死代码误导了后续维护者**（包括你自己），且暴露了"跟车距离档位"这一功能实际上从未真正实现——`values.py` 里的 `GAP_TIME_TABLE` 同样是死配置。

---

## 四、六大对比维度详解

### 4.1 横向控制基础设施

| 对比项 | BYD（你的实现） | 丰田/现代/本田 |
|---|---|---|
| 驾驶员对抗限幅 | 手写 `apply_meas_steer_torque_limits` 调用 | 用 `apply_meas_steer_torque_limits` / `apply_driver_steer_torque_limits` |
| EPS 故障预防 | 手写 LOCK1~7 状态机（~400 行） | 通用 `common_fault_avoidance()`（`lateral.py` 174 行） |
| 速率限制 | 手写 `steerRateLim` | `rate_limit()` 或内置 |
| 转向软启动 | 手写 `steer_softstart_limit` | 无（或简单处理） |

**分析**：

丰田和现代都用一个通用函数 `common_fault_avoidance(fault_condition, request, above_limit_frames, max_above_limit_frames)`：

```python
# lateral.py:174
def common_fault_avoidance(fault_condition, request, above_limit_frames,
                           max_above_limit_frames, max_mismatching_frames=1):
  """Several cars have the ability to work around their EPS limits by cutting the
  request bit of their LKAS message after a certain number of frames above the limit."""
```

丰田用它防转向角速度 >100 deg/s 的 EPS 故障，现代用它防转向角 >85° 的故障。你的 LOCK6/LOCK7 本质在做同样的事（超过包络→限幅→恢复），但手写成了几百行的状态机。

**但这里要客观评价**：你的 LOCK 机制**更贴合 BYD EPS 的特性**——BYD 的 `Prepared`/`CruiseActivated` 握手（`0xF9`/`0xFA`/`0xFB` 状态机）是 BYD 独有的，通用的 `common_fault_avoidance` 只处理「切断请求位」，无法处理「EPS 主动请求重握手」。所以**不建议简单替换**，而是建议**抽取到独立模块**（见 5.2）。

### 4.2 MADS（横向单独控制）——最大功能缺失

这是 sunnypilot 的核心卖点：**不开 ACC 也能车道保持，横向/纵向独立启停**。

#### 现状

- **其他所有品牌**都实现了 MADS：
  - 现代：`sunnypilot/car/hyundai/mads.py`（`MadsCarController` + `MadsCarState`）
  - 本田：`sunnypilot/car/honda/mads.py`
  - Rivian：`sunnypilot/car/rivian/mads.py`
  - 克莱斯勒：`sunnypilot/car/chrysler/mads.py`
  - 特斯拉：`sunnypilot/car/tesla/coop_steering.py`（合作转向）
- **BYD 完全没有**：`car/byd/` 目录里搜 `MadsCarController`/`MadsCarState`/`mads_base` 均为 0 匹配。

#### 关键细节：panda 层已经预留了 MADS 支持，但 Python 层没接

在 `safety/modes/byd.h` 中：

```c
// 103 行
if (msg->addr == BYD_PCM_BUTTONS) {
  acc_main_on = GET_BIT(msg, 8U);
  // MADS support - still use the button press event
  mads_button_press = acc_main_on ? MADS_BUTTON_PRESSED : MADS_BUTTON_NOT_PRESSED;
}

// 112 行
if (msg->addr == BYD_DRIVE_STATE) {
  if ((alternative_experience & ALT_EXP_ENABLE_MADS) && !m_mads_state.system_enabled) {
    m_mads_state.system_enabled = true;
  }
  mads_state_update(vehicle_moving, acc_main_on, controls_allowed, ...);
}
```

panda 侧已经写好了 MADS 按钮检测和状态机更新逻辑，但 Python 侧的 `carstate.py` / `carcontroller.py` 完全没有对应的 `MadsCarState` / `MadsCarController` 来消费这些状态。**这是「底层已就绪、上层未接线」的典型半成品状态。**

#### 影响

BYD 唐 DM 用户无法享受 sunnypilot 的横向单独控制。这是 sunnypilot 用户最看重的功能之一，缺失会显著降低移植版本的可用性评价。

#### 参考实现（很薄）

现代起亚的 `MadsCarController` 只有约 80 行，核心逻辑：

```python
# hyundai/mads.py:37
def mads_status_update(self, CC, CC_SP, frame):
  enable_mads = CC_SP.mads.available
  if CC.latActive:
    self.lat_disengage_init = False
  elif self.prev_lat_active:
    self.lat_disengage_init = True
  # ...
  return MadsDataSP(enable_mads, CC.latActive, disengaging, paused)
```

接入 MADS 对 BYD 来说工作量不大，重点是：
1. 建 `sunnypilot/car/byd/mads.py`，`MadsCarState` 继承 `MadsCarStateBase`（`mads_base.py`）
2. `carstate.py` 的 `CarState` 增加 `MadsCarState` 继承，实现 `update_mads` 消费 panda 的 `mads_button_press`
3. `carcontroller.py` 增加 `MadsCarController`，把 `CC_SP.mads.available/enabled` 接到 `lkas_active` 握手逻辑

### 4.3 sunnypilot 通用抽象层——零利用

现代起亚的 `CarController` 继承了 6 个 mixin：

```python
# hyundai/carcontroller.py:57
class CarController(CarControllerBase, EsccCarController, LeadDataCarController,
                    LongitudinalController, MadsCarController,
                    IntelligentCruiseButtonManagementInterface):
```

这些是 sunnypilot 沉淀的通用组件，BYD 全都没用：

| 组件 | 作用 | BYD 现状 |
|---|---|---|
| `LeadDataCarController` | 前车数据扩展（把雷达/模型 lead 统一喂给 CAN 的 HasLead/LeadingDistance） | 未用，`carcontroller.py` 里 `create_acc_hud_adas` 直接透传摄像头原始 `HasLead`/`LeadingDistance` |
| `IntelligentCruiseButtonManagementInterface`（ICBM） | 智能巡航按键管理（距离档位自动化） | 未用，`carstate.py` 里距离按钮是裸的 `gapAdjustCruise` 事件 |
| `LongitudinalController` | jerk 受限纵向调校（ISO 15622） | 未用（死代码 `longcontrol_byd.py` 之外，`bydcan.py` 手写 jerk 查表） |
| `MadsCarController` | 横向单独控制 | 未用（见 4.2） |

#### 值得借鉴的具体点

**① LeadData 规范化**（`hyundai/lead_data_ext.py`）：

现代起亚用 `LeadDataCarController` 把前车数据做**迟滞滤波**（`LEAD_HYSTERESIS_FRAMES=50`）后再喂给 CAN，避免前车时有时无导致仪表盘前车图标闪烁。你的 BYD 直接透传摄像头原始值（`bydcan.py` 的 `create_acc_hud_adas` 里 `HasLead`/`LeadingDistance` 原样透传），这在 OP 接管纵向时可能出现「OP 模型和摄像头雷达判定不一致」导致仪表盘显示与实际控制不符。

**② jerk-limited 纵向**（`hyundai/longitudinal/controller.py` + `helpers.py`）：

现代起亚把纵向加速度的 jerk 限制做成了按速度分段的完整框架（`_calculate_speed_based_jerk_limits`，遵循 ISO 15622），且有 `jerk_limited_integrator` 做平滑。你的 `bydcan.py` 里手写了 `K_jerk_xp`/`K_jerk_base_upper_fp` 查表，功能类似但更粗糙、且没有遵循 ISO 标准。

### 4.4 纵向控制架构对比

#### 你的实际做法（`carcontroller.py`）

```
sunnypilot 通用 MPC 输出 CC.actuators.accel
  → clip 到 [-3.5, 2.0]
  → 0.5 m/s 速度滞环
  → acc_cmd 透传 CAN
```

**缺失坡度补偿**。

#### 丰田的做法（`toyota/carcontroller.py`，最值得参考）

丰田有完整的纵向补偿链：

```python
# 221 行
accel_due_to_pitch = math.sin(min(self.pitch.x, 0.0)) * ACCELERATION_DUE_TO_GRAVITY
net_acceleration_request = pcm_accel_cmd + accel_due_to_pitch

# 234 行
a_ego_blended = float(np.interp(CS.out.vEgo, [1.0, 2.0], [CS.gvc, CS.out.aEgo]))
prev_aego = self.aego.x
self.aego.update(a_ego_blended)
j_ego = (self.aego.x - prev_aego) / (DT_CTRL * 3)

# 238 行
future_t = float(np.interp(CS.out.vEgo, [2., 5.], [0.25, 0.5]))
a_ego_future = a_ego_blended + j_ego * future_t

# 245 行
error_future = pcm_accel_cmd - a_ego_future

# 250 行（坡度补偿放大）
pitch_compensation = float(np.clip(math.sin(self.pitch_hp.x) * ACCELERATION_DUE_TO_GRAVITY,
                                   -MAX_PITCH_COMPENSATION, MAX_PITCH_COMPENSATION))
pcm_accel_cmd += pitch_compensation

# 243 行（积分器缓慢 unwind，防饱和）
self.long_pid.i -= ACCEL_PID_UNWIND * float(np.sign(self.long_pid.i))
```

核心要点：
1. **坡度补偿**：用 `orientationNED[1]` 计算俯仰角，补偿上下坡的加速度偏差
2. **未来误差预测**：用 jerk 外推未来加速度，提前补偿执行器延迟
3. **积分器 unwind**：缓慢释放积分，防止长时间误差累积

#### 影响分析

你的 BYD 纵向在**上下坡时会有明显偏差**（唐 DM 是 2.25 吨 SUV，坡道工况常见，且 `ret.transmissionType = TransmissionType.direct` 意味着电机直接驱动，坡度对加速度影响更直接）。这是目前纵向最实际的短板。

> 注：`longcontrol_byd.py` 死代码里的 MPC 思路**不必复活**——sunnypilot 上层通用 MPC 已经够好，且你已经通过 `LongitudinalPersonality` 绑定了三档调参。真正缺的是**坡度补偿**这一物理修正。

### 4.5 前车数据喂入对比

BYD 的 `bydcan.py` 中 `create_acc_hud_adas` 直接透传摄像头原始值：

```python
values = {s: cam_msg[s] for s in [
    "SetSpeed", "HasLead", "SetDistance", "LeadingDistance", "AEB", "FCW", ...]}
```

这意味着当 OP 接管纵向时，仪表盘显示的「前车距离/前车有无」仍然是摄像头雷达的原始判定，**没有和 OP 自己的模型 lead 数据融合**。

现代起亚用 `LeadDataCarController` 专门解决这个问题：从 `CC_SP.leadOne`（OP 模型的前车数据）提取 `dRel`/`vRel`/`status`，经迟滞滤波后喂给 CAN，保证仪表盘显示和 OP 实际控制一致。

### 4.6 代码组织 / 工程性

| 方面 | 其他车型 | BYD（你的实现） |
|---|---|---|
| `CarControllerParams` | 构造函数（按 fingerprint 动态生成） | 纯静态类属性 |
| 报文打包 | 独立 `xxxcan.py` | ✅ 已做对（`bydcan.py` 结构清晰） |
| sunnypilot 扩展分离 | `sunnypilot/car/<brand>/` | ❌ 无 |
| 注释规模 | 几行内 | 数千行中文推导史 |
| 多车型扩展性 | 平台集合 + flags 机制 | 单一 `BYD_TANG_DM`，硬编码 |

**关于注释**：你的 `values.py` 里塞了数千行的 LOCK1~LOCK7 完整推导史（每个参数的「为什么是这个值」都写了实证过程），这对你回看极有价值，但对代码可维护性不利。丰田/现代的参数注释都控制在几行内，详细推导放在 docs 或笔记里。

---

## 五、优先级建议清单

| 优先级 | 事项 | 借鉴来源 | 工作量 | 价值 |
|---|---|---|---|---|
| ⭐⭐⭐ | **接入 MADS**（横向单独控制） | `hyundai/mads.py` + `mads_base.py` | 小（~80 行 + 握手接线） | 恢复 sunnypilot 招牌功能 |
| ⭐⭐⭐ | **处理死代码**：删除或复活 `longcontrol_byd.py` + 补齐 `BydFollowDistance` 参数 | — | 小 | 消除误导，理清真实控制路径 |
| ⭐⭐ | **纵向坡度补偿** | `toyota/carcontroller.py` | 中 | 坡道工况纵向准确性 |
| ⭐⭐ | **前车数据规范化**（LeadData 迟滞滤波） | `hyundai/lead_data_ext.py` | 中 | 仪表盘显示与控制一致性 |
| ⭐⭐ | **抽取 LOCK1~7 到独立模块**，瘦身 `carcontroller.py` | — | 中 | 可维护性 |
| ⭐ | 参考 `LongitudinalController` 对齐 jerk 限制 | `hyundai/longitudinal/` | 中 | 纵向平顺性 |
| ⭐ | 建立 `sunnypilot/car/byd/` 目录结构，对齐扩展框架 | — | 小 | 工程规范 |

### 具体落地建议（按优先级排序）

#### 1. MADS 接入（最高优先）

```
1. 新建 opendbc/sunnypilot/car/byd/mads.py
   - MadsCarState(MadsCarStateBase): 实现 update_mads
   - MadsCarController: 复用 hyundai/mads.py 的 mads_status_update 模式
2. carstate.py 的 CarState 增加 MadsCarState 继承
3. carcontroller.py 增加 MadsCarController 继承，接入 CC_SP.mads
4. panda 层已就绪（byd.h 的 mads_button_press / mads_state_update），无需改
```

#### 2. 死代码清理

```
- 删除 openpilot/selfdrive/controls/lib/longcontrol_byd.py
- 或在 params_keys.h 补齐 BydFollowDistance/BydComfortMode 并真正接线
- 推荐前者（sunnypilot 通用 MPC 已够用，保留死代码只会误导）
```

#### 3. 纵向坡度补偿

```
在 carcontroller.py 的 acc_cmd 前，参考丰田：
- 用 CC.orientationNED[1] 计算俯仰角
- 补偿量 = sin(pitch) * g，clip 到 ±1.5 m/s²
- 叠加到 accel 命令上
```

---

## 附录：关键文件对照表

| 功能 | BYD 文件 | 参考实现 | 参考文件 |
|---|---|---|---|
| 横向控制器 | `car/byd/carcontroller.py` | 丰田 | `car/toyota/carcontroller.py` |
| 故障预防 | 手写 LOCK1~7 | 通用函数 | `car/lateral.py:common_fault_avoidance` |
| MADS | ❌ 无 | 现代 | `sunnypilot/car/hyundai/mads.py` |
| MADS 基类 | ❌ 未继承 | — | `sunnypilot/mads_base.py` |
| 前车数据 | `bydcan.py:create_acc_hud_adas` 透传 | 现代 | `sunnypilot/car/hyundai/lead_data_ext.py` |
| 纵向 jerk | `bydcan.py` 手写查表 | 现代 | `sunnypilot/car/hyundai/longitudinal/` |
| 坡度补偿 | ❌ 无 | 丰田 | `car/toyota/carcontroller.py:221` |
| 通用纵向 MPC | 上层自动调用 | — | `controls/lib/longitudinal_mpc_lib/long_mpc.py` |
| 死代码纵向 | `controls/lib/longcontrol_byd.py` | — | 建议删除 |
| panda 安全 | `safety/modes/byd.h`（287 行，完整） | — | 已含 MADS 预留 |

---

## 结论

**你的 BYD 移植是「逆向的天才，工程的短板」。**

- ✅ 横向控制安全（LOCK1~7）达到甚至超过原厂门总水准，这是全场无人能及的
- ✅ CAN 报文逆向、checksum、双源切换、panda fail-safe 门控全部到位
- ❌ 但没有接入 sunnypilot 的通用抽象层，导致 MADS 缺失、死代码存在、纵向无坡度补偿

建议优先补齐 **MADS** 和**死代码清理**两项（投入小、收益大），再考虑**坡度补偿**和**前车数据规范化**。补齐后，BYD 移植将从「能跑」提升到「好用」。
