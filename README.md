# PX4 慧眼目标跟踪与 TRACK 飞行模式

本仓库用于把慧眼 V3.1 视觉模块的 UART 目标数据接入 PX4，并在自定义 `Track` 飞行模式中生成姿态设定值。当前版本已完成软件回归、固件编译、真实 UART 数据接收、模式切换，以及拆桨状态下四路电机差动输出验证；尚未完成带桨悬停、目标丢失、RC 丢失和实际跟踪飞行验证。

## 数据链路

```text
慧眼 UART
  -> uart_tracker
  -> TrackerTarget uORB
  -> track_control
  -> vehicle_attitude_setpoint
  -> mc_att_control / mc_rate_control / control_allocator
  -> 四路电机
```

## 目录职责

| 目录 | 用途 | 维护规则 |
| --- | --- | --- |
| `design_sourse/` | 用户说明、通讯协议、TRACK 设计报告 | 设计输入，只读保留，不改名 |
| `px4_tracker_integration/` | PX4 消息、`uart_tracker`、`track_control` 覆盖层 | 本项目功能源码的唯一维护入口 |
| `scripts/` | 覆盖层安装和目录校验脚本 | 修改后必须执行校验 |
| `PX4-Autopilot/` | 本机 PX4 工作树 | 由覆盖层同步生成，不作为独立源码维护 |
| `tools/` | 浏览器仪表板和测试辅助工具 | 与协议测试同步维护 |
| `tests/` | Python 与 JavaScript 回归测试 | 修改协议或消息映射后运行 |
| `docs/` | 架构、协议、TRACK 模式、维护说明和测试记录 | 与实现和实测结果同步更新 |
| `firmware/` | 已验证固件归档 | 用 SHA-256 标识，不覆盖旧版本 |
| `artifacts/` | 原始测试数据和非源码证据 | 不放入源码目录 |
| `uart_example/` | 厂商示例 | 上游参考，只读保留 |

详细维护规则见 [工程维护说明](docs/maintenance.md)，本次验证结果见 [2026-08-04 TRACK 拆桨台架测试](docs/test_records/2026-08-04-track-bench.md)。

## 硬件接口

慧眼模块接入 `TELEM4 / UART8`，PX4 设备为 `/dev/ttyS7`：

```text
模块 TX  -> UART8 RX
模块 RX  -> UART8 TX（仅接收目标数据时可不接）
模块 GND -> GND
```

只能接 3.3 V TTL UART，不能将 RS-232 电平直接接入飞控。

## 同步与编译

在 Windows PowerShell 仓库根目录执行：

```powershell
.\scripts\install_px4_overlay.ps1
.\scripts\install_track_mode_overlay.ps1
.\scripts\verify_layout.ps1
```

在 WSL 中编译：

```sh
cd /home/wsanu/rocket_tracker/PX4-Autopilot
make hkust_nxt-dual_default -j4
```

当前已验证固件 SHA-256 为 `ae7803cf7aa9c9dd6cf24f4757d237746d0691559259f6ab06bbcd33b7677432`。

## 运行与检查

当前板级默认参数可直接启动串口解析器：

```sh
uart_tracker start
uart_tracker status
track_control status
listener tracker_target -n 5
listener vehicle_status -n 1
listener track_status -n 5
```

已知限制：当前命令行长选项 `--width`、`--height`、`--hfov`、`--vfov` 会导致实例化失败，应先使用板级默认参数启动。退出 TRACK 后，`track_status` 可能保留最后一帧；判断当前模式应以 `vehicle_status.nav_state` 和消息时间戳为准。

`Track` 对应飞行模式编号 `16`。台架验证中使用 `COM_FLTMODE6=16`，实际映射以 QGroundControl 的通道监视器为准。不能在 TRACK 中直接解锁，应先在 Stabilized 或 Position 中完成检查和解锁，再按测试计划切换。

## 自动测试

```powershell
python -m unittest discover -s tests
node --test tests\test_dashboard_protocol.mjs
node --test tests\test_dashboard_mavlink.mjs
.\scripts\verify_layout.ps1
```

如果系统没有安装 Node.js，可使用 Codex 工作区依赖中附带的 Node 运行 JavaScript 测试。

## 安全边界

- 所有首次电机和控制链路测试必须拆桨并固定机体。
- 台架差动输出通过只证明控制链路接通，不等同于飞行安全验证。
- 上桨前必须完成传感器、机架、电源、遥控器、失控保护、电机顺序和旋向检查。
- `COM_DISARM_PRFLT` 和 `COM_DISARM_LAND` 只能改变自动上锁等待时间，不能替代安全检查。
