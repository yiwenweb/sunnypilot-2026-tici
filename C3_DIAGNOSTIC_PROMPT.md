# C3 Panda 诊断任务 - 只读检查

## 背景
C3 设备（comma three）运行 sunnypilot，当前有两套环境：
- `/data/openpilot2025`: sp2025-gf 版本，运行正常
- `/data/openpilot`: sunnypilot-2026 版本，需要验证 F4 Panda USB 连接是否正常

已知事实：
1. 当前 C3 的 Panda 固件版本：`DEV-e838e944`（2025 版固件）
2. Panda 类型：DOS (F4, internal panda)
3. 传输方式：USB（serial: `2d001f000651383332353431`）
4. SPI **不可用**（`SPI=[]` 是正常状态）
5. 固件状态：`bootstub=False` 表示应用固件正常运行

历史问题：
- 2026 版 pandad 报告 `health packet version mismatch`
- 混用了 2025 固件和 2026 Python 库导致协议版本不匹配

## 任务目标
**只读检查**以下内容（禁止修改任何代码或刷写固件）：

### 1. 验证 2026 环境的构建状态
```bash
# 检查 pandad 是否已编译
ls -lh /data/openpilot/selfdrive/pandad/pandad
file /data/openpilot/selfdrive/pandad/pandad
ldd /data/openpilot/selfdrive/pandad/pandad 2>&1 | head -20

# 检查固件是否已编译
ls -lh /data/openpilot/panda/board/obj/panda.bin.signed
ls -lh /data/openpilot/panda/board/obj/bootstub.panda.bin
file /data/openpilot/panda/board/obj/panda.bin.signed 2>/dev/null || echo "固件未构建"
```

### 2. 对比两个版本的协议定义
```bash
# 对比 health packet 定义
echo "=== 2025 health.h ==="
md5sum /data/openpilot2025/panda/board/health.h
grep -E "HEALTH_PACKET_VERSION|health_t" /data/openpilot2025/panda/board/health.h | head -5

echo "=== 2026 health.h ==="
md5sum /data/openpilot/panda/board/health.h
grep -E "HEALTH_PACKET_VERSION|health_t" /data/openpilot/panda/board/health.h | head -5

# 对比 SConscript 中的版本计算
grep -A2 "HEALTH_PACKET_VERSION" /data/openpilot2025/panda/SConscript
grep -A2 "HEALTH_PACKET_VERSION" /data/openpilot/panda/SConscript
```

### 3. 验证 2026 Python 库的协议版本
```bash
cd /data/openpilot
export PYTHONPATH=/data/openpilot
python3 << 'EOF'
from panda import Panda
import panda
print("2026 Python module:", panda.__file__)
print("2026 HEALTH_PACKET_VERSION:", Panda.HEALTH_PACKET_VERSION)
print("2026 CAN_PACKET_VERSION:", Panda.CAN_PACKET_VERSION)
print("2026 USB_PIDS:", Panda.USB_PIDS)
print("2026 USB_VIDS:", Panda.USB_VIDS)
print("2026 INTERNAL_DEVICES:", Panda.INTERNAL_DEVICES)
EOF
```

### 4. 验证 2025 环境的协议版本（对照组）
```bash
cd /data/openpilot2025
export PYTHONPATH=/data/openpilot2025
python3 << 'EOF'
from panda import Panda
import panda
print("2025 Python module:", panda.__file__)
print("2025 HEALTH_PACKET_VERSION:", Panda.HEALTH_PACKET_VERSION)
print("2025 CAN_PACKET_VERSION:", Panda.CAN_PACKET_VERSION)

# 检查当前 Panda 状态
print("\n=== Current Panda Status ===")
print("USB list:", Panda.usb_list())
print("SPI list:", Panda.spi_list())

usb = Panda.usb_list()
if usb:
    p = Panda(usb[0], claim=False)
    print("Serial:", usb[0])
    print("Bootstub:", p.bootstub)
    print("Type:", p.get_type())
    print("Firmware:", p.get_version())
    print("Panda health_version:", p.health_version)
    print("Library HEALTH_PACKET_VERSION:", Panda.HEALTH_PACKET_VERSION)
    print("Match:", p.health_version == Panda.HEALTH_PACKET_VERSION)
    p.close()
EOF
```

### 5. 检查 2026 pandad 源码中的 USB/SPI 选择逻辑
```bash
echo "=== 2026 pandad USB/SPI 选择 ==="
grep -A5 "try USB first" /data/openpilot/selfdrive/pandad/panda.cc || \
grep -A10 "Panda::Panda" /data/openpilot/selfdrive/pandad/panda.cc | head -15

echo "=== 2026 PandaUsbHandle 构造 ==="
grep -A10 "PandaUsbHandle::PandaUsbHandle" /data/openpilot/selfdrive/pandad/panda_comms.cc | head -15
```

### 6. 检查 2026 Git 状态和最近提交
```bash
cd /data/openpilot
git status --short
git branch --show-current
git log --oneline -10 --grep="panda\|USB\|SPI\|F4" --all
git log --oneline -10 -- selfdrive/pandad/ panda/
```

### 7. 检查当前运行的 pandad 进程
```bash
ps aux | grep pandad | grep -v grep
pgrep -a pandad || echo "pandad 未运行"

# 如果 pandad 在 tmux 中运行，捕获最后 50 行日志
tmux list-sessions 2>/dev/null | grep -q pandad && \
  tmux capture-pane -pt pandad -S -50 | tail -50 || \
  echo "未找到 pandad tmux session"
```

### 8. 验证 libusb 和依赖库
```bash
echo "=== libusb 检查 ==="
ldconfig -p | grep libusb
python3 -c "import usb1; print('usb1 module:', usb1.__file__); print('version:', usb1.getVersion())"

echo "=== 2026 Panda Python 依赖 ==="
python3 -c "import sys; sys.path.insert(0, '/data/openpilot'); from panda import usb; print(usb.__file__)"
```

## 输出要求
将所有命令输出汇总为一个文本文件，格式：
```
=== Section 1: 构建状态 ===
[输出]

=== Section 2: 协议定义对比 ===
[输出]

...
```

## 安全约束（重要！）
1. **禁止运行任何写入操作**：不得使用 `scons`、`make`、`flash`、`recover`、`dfu` 等命令
2. **禁止修改代码**：只读检查，不修改任何 `.py`、`.cc`、`.h` 文件
3. **禁止重启服务**：不杀进程、不启动新 pandad
4. **禁止刷写固件**：不调用任何 `Panda.flash()`、`PandaDFU.recover()` 方法
5. **如遇错误立即停止**：任何命令失败都记录错误继续，不尝试修复

## 预期结果
完成后，应能回答：
1. `/data/openpilot` 的 pandad 和固件是否已正确编译？
2. 2025 和 2026 的 `HEALTH_PACKET_VERSION` 是否一致？
3. 2026 pandad 源码是否包含 USB 支持？
4. 当前 Panda 固件（2025 版）是否与 2025 Python 库匹配？
5. 下一步应该做什么（构建 2026？统一协议版本？还是其他）？
