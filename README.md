# PX4 慧眼目标跟踪与 TRACK 飞行模式

本仓库提供一套可从源码复现的 PX4 1.15.4 目标指向飞行模式：慧眼/Viztra LE071通过UART发布目标方向，自定义 `track_control` 生成姿态设定值，继续使用PX4原生姿态、角速度和控制分配链路驱动四旋翼电机。

当前完成状态：

- Python与JavaScript协议回归测试通过。
- `hkust_nxt-dual_default` 编译、链接和上板启动通过。
- 真实慧眼UART数据解析通过，实测解析错误为0。
- QGC可选择Track模式，`vehicle_status.nav_state=9`。
- 拆桨状态下，目标移动能产生四路电机差动输出。
- 尚未完成带桨Track飞行、目标长期丢失和RC丢失实飞验证。

## 新用户从这里开始

完整教程：[从零复现固件、接线和拆桨验收](docs/reproduction.md)

```sh
git clone https://github.com/wsanu/AI-tracking-rocket.git
cd AI-tracking-rocket

# 检查仓库
python3 scripts/check_repository.py
python3 -m unittest discover -s tests

# 创建固定PX4工作树并安装覆盖层
python3 scripts/reproduce_px4.py \
  --px4-root ../PX4-Autopilot \
  --clone

# 安装PX4 NuttX工具链（完成后重开终端）
cd ../PX4-Autopilot
bash Tools/setup/ubuntu.sh --no-sim-tools

# 编译HKUST NXT Dual固件
make hkust_nxt-dual_default -j4
```

复现脚本固定PX4上游提交为：

```text
99c40407ffd7ac184e2d7b4b293f36f10fe561ef
```

固定信息保存在 [px4-base.json](reproducibility/px4-base.json)。不要使用任意最新PX4替代该提交。

## 支持的已验证硬件

| 组件 | 已验证型号/接口 |
| --- | --- |
| 飞控 | HKUST NXT Dual / NxtPX4v2，`HKUST_NXT_DUAL` |
| PX4目标 | `hkust_nxt-dual_default` |
| 视觉模块 | 慧眼 V3.1 / Viztra LE071 |
| 视觉串口 | TELEM4 / UART8，`/dev/ttyS7`，115200 baud，3.3 V TTL |
| 电调 | HK38203 V2.1，PWM1–PWM4 |
| 接收机 | MicoAir LR24-F-mini V1.0，ELRS/CRSF |

其他飞控可以进行SITL或移植研究，但不能视为已经过硬件验证。

## 数据链路

```text
慧眼 UART
  -> uart_tracker
  -> TrackerTarget uORB
  -> track_control
  -> vehicle_attitude_setpoint
  -> mc_att_control / mc_rate_control / control_allocator
  -> Motor 1..4
```

TRACK不直接写电机输出，也不替换PX4原生PID和Control Allocation。

## 目录职责

| 目录 | 用途 |
| --- | --- |
| `reproducibility/` | PX4版本锁和TRACK项目参数 |
| `scripts/reproduce_px4.py` | 跨平台克隆、固定版本和幂等覆盖安装 |
| `px4_tracker_integration/` | UART、TRACK模块和uORB消息的维护源 |
| `design_sourse/` | 用户说明、通信协议和TRACK设计报告，只读设计输入 |
| `docs/` | 架构、协议、复现、硬件检查与测试记录 |
| `tests/` | Python和JavaScript回归测试 |
| `tools/` | 数据帧生成器和Web Serial仪表板 |
| `uart_example/` | 厂商示例代码，只读参考 |
| `firmware/` | 已验证固件文件名、大小和SHA-256清单 |
| `artifacts/` | 结构化测试数据；本地日志默认不提交 |

## Windows PowerShell覆盖安装

已经自行准备好固定版本PX4工作树时，可以使用：

```powershell
.\scripts\install_px4_overlay.ps1 -Px4Root E:\path\to\PX4-Autopilot
.\scripts\verify_layout.ps1
```

`install_px4_overlay.ps1` 会自动安装UART层和TRACK层，不需要再次调用 `install_track_mode_overlay.ps1`。

## 上板最小检查

拆桨、固定机体后，在MAVLink Console执行：

```sh
ver all
track_control status
uart_tracker start
uart_tracker status
listener tracker_target -n 5
listener vehicle_status -n 1
listener track_status -n 5
listener actuator_motors -n 20 -r 5
```

当前已知限制：

- `uart_tracker start` 的长选项存在实例化问题，先使用板级默认参数启动。
- 离开Track后 `track_status` 可能保留最后一帧；当前模式以 `vehicle_status.nav_state` 为准。
- 当前固件FLASH使用率约98.39%，不要随意增加板级模块。
- HK38203 V2.1电流采样比例需要针对实际硬件标定。

## 安全文档

- [硬件接线与首次上电检查表](docs/hardware_checklist.md)
- [TRACK模式设计与参数](docs/track_mode.md)
- [2026-08-04拆桨台架测试记录](docs/test_records/2026-08-04-track-bench.md)
- [工程维护说明](docs/maintenance.md)

所有首次电机和控制链测试必须拆桨。台架输出通过只证明链路接通，不代表具备安全飞行条件。
