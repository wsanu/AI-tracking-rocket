# 2026-08-07 RC AUX 三段开关 UART 指令控制测试计划

## 功能说明

通过遥控器 Channel 9（三段拨档开关，映射为 AUX1）控制慧视 V3.1 视觉跟踪板的检测和自动锁定模式，无需 NSH 终端。

## 修改文件

| 文件 | 变更 |
| --- | --- |
| `px4_tracker_integration/src/modules/uart_tracker/UartTracker.hpp` | +2 枚举、+3 方法、+7 成员变量、+1 Subscription |
| `px4_tracker_integration/src/modules/uart_tracker/UartTracker.cpp` | +116 行：状态机 + 主循环集成 + status 显示 |

## 原理

### 状态机

RC 开关处于**任何飞行模式**下都生效。`UartTracker::run()` 每 100ms 检查 `manual_control_setpoint.aux1`：

```
aux > 0.5   →  3档 (ON)   →  autolock 2 0 (循环+自动锁定)
-0.5~0.5   →  2档 (MID)   →  ① autolock 0 0 → ② 等待2秒冷却 → ③ detect 1
aux < -0.5  →  1档 (OFF)  →  detect 0 (关闭所有目标检测)
```

### 2档冷却序列

2档需要先关自动锁定、再开目标检测，中间间隔 2 秒（`kAuxCooldownUs = 2000000`）。序列分三步：

1. `AutolockOff` — 发送 `03 05` (autolock 0 0)，等待 `_command_busy` 释放
2. `Cooldown` — 等待 2 秒
3. `DetectOn` — 发送 `03 01` (detect 1)

序列进行中如果切换到其他档位，立即打断。

### 复用 V1.1.1 命令队列

RC 状态机通过 `send_frame()` 入队，和 NSH `uart_tracker send` 命令走同一个队列和同一个 mutex 保护，不会并发写入。

### 首次上电不触发

`_aux_position` 初始为 `UNKNOWN`，第一次读到开关值只记录位置不发送指令，避免上电误触发。

## 测试配置

- 遥控器：Channel 9 = 三段拨档开关
- PX4 参数：`RC_MAP_AUX1=9`
- 飞控：HKUST NXT Dual
- 慧视模块：Viztra LE071 / 慧眼 V3.1
- 接线：慧眼 RX 必须接飞控 UART3 TX（V1.1.1 要求）

## 测试步骤

### 0. 参数配置

NSH 或 QGC 中执行：

```
param set RC_MAP_AUX1 9
param save
reboot
```

在 QGC Channel Monitor 中确认 Channel 9 三段开关对应 aux1 值为 +1 / 0 / -1。

### 1. 启动模块

```
uart_tracker start
uart_tracker status
```

确认 frames 在增长、parse errors 为 0。

### 2. 逐档测试

每次拨档后等待 2 秒再执行 `uart_tracker status`。

**拨到 3 档（ON，上段）：**

预期 status 显示 `commands: sent N+1`，`last command: 03 05`，`last response: 03 85`。跟踪板进入循环自动锁定。

**拨到 2 档（MID，中段）：**

立即看到 `last command: 03 05`（autolock 0 0），2 秒后 `commands` +1 为 `03 01`（detect 1）。`sequence` 从 1→2→3→0。

**拨到 1 档（OFF，下段）：**

`commands` +1，`last command: 03 01`（detect 0）。跟踪板停止检测。

**快速切换测试：** 2 档冷却期间立刻拨到 3 档，序列被中断，直接执行 `autolock 2 0`。

### 3. status 输出验证

```
uart_tracker status
```

最后一行显示：

```
rc aux switch: position <0|1|2>, sequence <0-3>
```

position: 0=OFF, 1=MID, 2=ON。sequence: 0=Idle, 1=AutolockOff, 2=Cooldown, 3=DetectOn。

### 4. 离板测试（不接慧视）

即使慧视没连接，NSH 日志也能看到状态机工作：

```
INFO  RC aux switch init: position 1
INFO  RC aux switch: 1 -> 2
INFO  queued command 03 05 (4 payload bytes)
INFO  sent command 03 05 ...
```

注意 `send_frame` 返回 `PX4_ERROR` 是正常的（慧视没接），但状态机逻辑不受影响。

### 5. 并发保护验证

NSH 同时发 `uart_tracker send info` 时拨档——两个命令走同一个队列，后到的会被 `a UART command is already pending` 拒绝。

## 通过标准

- `python -m unittest discover -s tests` 6/6 通过（含 `test_uart_write_runs_in_module_task`）
- 模块编译无警告
- 三档分别对应三条 UART 指令，指令帧字段正确
- 2 档冷却序列完整执行（autolock 0 → 2 秒 → detect 1）
- 快速切换时序列被打断
- 首次上电不触发
- `uart_tracker status` 显示 RC 开关状态

## 已知限制

- 开关位置判定阈值为 ±0.5，如果遥控器通道行程不够可能需要调整 `kAuxThreshold`
- Module 停止 (`uart_tracker stop`) 时 `_aux_position` 不重置为 UNKNOWN，下次 start 后会恢复上次位置
- 不读取 `RC_MAP_AUX1` 参数值，需手动配置
