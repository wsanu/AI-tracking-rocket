# PX4 TRACK 目标指向飞行模式

## 实现边界

当前实现基于PX4 1.15.4提交 `99c40407ffd7ac184e2d7b4b293f36f10fe561ef`。设计报告以较新PX4接口描述功能，本项目已把接入点适配并编译验证到固定基线。

TRACK是独立导航状态和控制模块：

```text
uart_tracker -> TrackerTarget(direction_body) -> track_control
             -> vehicle_attitude_setpoint -> mc_att_control
             -> mc_rate_control -> control_allocator -> motors
```

它不直接写电机，不重新实现PID或混控，也不删除原慧眼协议字段、USB调试遥测和仪表板功能。

## 坐标和控制定义

- 相机沿机体向上安装时，画面中心光轴定义为FRD机体系 `[0, 0, -1]`。
- 图像右侧映射机体 `+Y`，图像上方映射机体 `+X`。
- 目标方向控制相机光轴的两个倾斜自由度。
- RC Yaw控制绕目标视线的twist。
- RC Throttle经PX4 `MPC_THR_*` 曲线映射总推力。
- RC Roll/Pitch在TRACK中不参与；位置、速度、高度和自动任务闭环关闭。

相机安装方向不同必须在UART视觉适配层加入外参，不能在 `track_control` 中重新解释像素。

## PX4接入

- `NAVIGATION_STATE_TRACK=9`，复用固定基线中的预留状态。
- `COM_FLTMODE1`–`COM_FLTMODE6` 参数值 `16` 映射到Track。
- TRACK启用manual、attitude、rates和control allocation标志。
- 进入要求有效角速度、姿态和RC，并禁止直接在TRACK中解锁。
- `mc_att_control` 在TRACK下不生成Stabilized手动姿态设定，避免两个发布源竞争。
- `track_control` 是TRACK下唯一 `vehicle_attitude_setpoint` 来源。

## 消息

`TrackerTarget.msg` 保留基础协议字段，并包含采样时间、跟踪状态、序列号、FRD单位方向、归一化图像坐标和目标尺寸。

`TrackStatus.msg` 用于日志和调试，记录目标年龄、滤波方向、姿态设定、twist、推力、限幅和失效原因。

## 启动和模式切换

HKUST NXT Dual板级启动脚本按顺序自动执行：

```sh
uart_tracker start
track_control start
```

两条命令独立执行，不使用 `&&`。`uart_tracker` 在独立任务中打开UART，所以视觉设备暂时无响应或 `/dev/ttyS2` 打开失败不会阻止 `track_control` 和其他PX4模块启动。上电后可用以下命令确认状态：

```sh
uart_tracker status
track_control status
```

板级默认设备是 `/dev/ttyS2`，115200 baud，默认图像1280×720、HFOV 62°、VFOV 48°。正常上电无需连接电脑输入启动命令。当前长选项存在已知实例化问题，不要使用带 `--width/--height/--hfov/--vfov` 的启动命令；`uart_tracker stop/start` 仅用于诊断。

将一个 `COM_FLTMODE*` 设置为16。在Stabilized或Altitude/Position模式完成检查和解锁，再切换到Track；不能直接在Track中解锁。

## 状态机与保护

- 进入或目标恢复时按 `TRK_ENTRY_T` 渐入姿态。
- 目标方向在NED中保持，并按 `TRK_DIR_TC` 低通滤波。
- `TRK_TGT_TIMEOUT` 内保持最近方向。
- 超时后冻结姿态但保留油门映射，避免推力突降。
- 总丢失时间超过 `TRK_LOST_DELAY` 后请求Stabilized或显式配置的Position。
- 姿态受 `TRK_TILT_MAX` 与 `TRK_ATT_RATE_MAX` 限制。
- 目标必须满足置信度、时间戳和单位向量检查。

## 参数默认值

| 参数 | 默认值 | 说明 |
| --- | ---: | --- |
| `TRK_TGT_TIMEOUT` | 0.30 s | 最近目标保持时间 |
| `TRK_LOST_DELAY` | 0.80 s | 请求安全模式的总丢失延迟 |
| `TRK_CONF_MIN` | 0.50 | 最低置信度 |
| `TRK_TWIST_MAX` | 90 deg/s | 最大twist速率 |
| `TRK_TWIST_DZ` | 0.05 | Yaw杆死区 |
| `TRK_DIR_TC` | 0.08 s | 方向低通时间常数 |
| `TRK_ATT_RATE_MAX` | 120 deg/s | 姿态设定最大变化率 |
| `TRK_TILT_MAX` | 60 deg | 相对NED向下最大倾角 |
| `TRK_ENTRY_T` | 0.50 s | 进入/恢复渐入时间 |
| `TRK_LOST_ACT` | 0 | 0=Stabilized，1=Position |

总推力继续使用 `MPC_MANTHR_MIN`、`MPC_THR_HOVER`、`MPC_THR_MAX`、`MPC_THR_CURVE` 和 `COM_SPOOLUP_TIME`。

## 当前验证状态

已完成固定基线编译、真实UART接入、Track模式切换、有效目标姿态设定和拆桨四电机差动输出。原始数据见 `docs/test_records/2026-08-04-track-bench.md`。

尚未完成带桨悬停、目标跳变/长期丢失、RC丢失、twist方向、倾角限制和实际Track飞行验证。离开Track后 `track_status` 可能保留旧帧，当前模式应以 `vehicle_status.nav_state` 判断。
