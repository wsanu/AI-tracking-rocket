# PX4 TRACK 目标指向飞行模式

## 实现边界

当前工程实际基线为 PX4 `v1.15.4`、提交 `99c40407ffd7`；设计报告按 v1.16 描述，但本文所有接入点均已按当前基线 API 编译验证。

本实现将《PX4 TRACK 目标指向飞行模式设计报告》作为新增设计输入，同时保留原有慧眼 V3.1 UART 解析、`tracker_target` 旧字段、USB 调试遥测、浏览器仪表盘和可选云台动作路径。TRACK 是独立飞行模式和控制模块，不直接写电机输出，也不替换 PX4 姿态环、角速度环或 Control Allocation。

第一版控制定义：

- 相机沿机体向上安装，中心光轴为机体系 FRD 的 `[0, 0, -1]`。
- 视觉目标方向控制相机光轴的两个倾斜自由度。
- RC Yaw 控制绕目标视线的 twist；RC 油门通过 `MPC_THR_*` 曲线控制总推力。
- RC Roll/Pitch 暂不参与；位置、速度、高度、爬升率、加速度、Auto 和 Offboard 闭环关闭。

## PX4 接入点

- `NAVIGATION_STATE_TRACK = 9`，复用 v1.16 预留状态，不改变后续编号。
- `COM_FLTMODE1`～`COM_FLTMODE6` 参数值 `16` 映射到 Track。
- TRACK 开启 manual、attitude、rates 和 allocation 标志。
- 进入要求包括有效角速度、姿态和 RC；TRACK 下禁止直接解锁。
- `mc_att_control` 在 TRACK 下不生成 Stabilized 手动姿态设定，避免发布源竞争。
- `track_control` 发布 `vehicle_attitude_setpoint`，下游控制链保持原样。

## 消息兼容策略

`TrackerTarget.msg` 原有字段全部保留，追加 `timestamp_sample`、`tracking_state`、`sequence`、FRD 单位向量 `direction_body[3]`、归一化图像坐标和目标尺寸。UART 适配器默认图像右为机体 `+Y`、图像上为机体 `+X`、中心为机体 `-Z`。实机安装方向不同时，应在视觉适配层加入安装外参，不在 `track_control` 内重新解释像素。

`TrackStatus.msg` 只用于日志和调试，记录目标年龄、滤波方向、姿态设定、twist、推力、限制器和失效原因。

## 启动与切换

NxtPX4v2 固件通过板级脚本自动启动 `track_control`。刷入后，在 NSH 启动 UART 适配器并确认控制模块状态：

```sh
uart_tracker start -d /dev/ttyS7 -b 115200 --width 1280 --height 720 --hfov 62 --vfov 48
track_control status
```

其他板或 SITL 需要先手动执行 `track_control start`。将一个 `COM_FLTMODE*` 设置为 `16`。应先在 Stabilized 或 Position 模式解锁并起飞，再用 RC 开关切到 Track；不能直接在 Track 下解锁。

```sh
uart_tracker status
track_control status
listener tracker_target
listener track_status
listener vehicle_attitude_setpoint
```

## 状态机与保护

- 进入或目标恢复按 `TRK_ENTRY_T` 做姿态渐入。
- 目标方向在 NED 中保持并按 `TRK_DIR_TC` 低通滤波。
- `TRK_TGT_TIMEOUT` 内保持最近方向；随后冻结姿态但保留油门映射，不把推力突降为零。
- 超过 `TRK_LOST_DELAY` 请求 Stabilized（默认）或显式配置的 Position。
- 姿态受 `TRK_TILT_MAX` 与 `TRK_ATT_RATE_MAX` 限制，目标还须满足 `TRK_CONF_MIN`、时间戳和单位向量检查。

## 参数初值

| 参数 | 初值 | 说明 |
| --- | ---: | --- |
| `TRK_TGT_TIMEOUT` | 0.30 s | 最近目标保持时间 |
| `TRK_LOST_DELAY` | 0.80 s | 请求安全模式的总丢失延迟 |
| `TRK_CONF_MIN` | 0.50 | 最低置信度 |
| `TRK_TWIST_MAX` | 90 deg/s | 最大 twist 速度 |
| `TRK_TWIST_DZ` | 0.05 | Yaw 杆死区 |
| `TRK_DIR_TC` | 0.08 s | 方向低通时间常数 |
| `TRK_ATT_RATE_MAX` | 120 deg/s | 姿态设定最大变化率 |
| `TRK_TILT_MAX` | 60 deg | 相对 NED 向下最大倾角 |
| `TRK_ENTRY_T` | 0.50 s | 进入/恢复渐入时间 |
| `TRK_LOST_ACT` | 0 | 0=Stabilized，1=Position |

推力继续使用 `MPC_MANTHR_MIN`、`MPC_THR_HOVER`、`MPC_THR_MAX`、`MPC_THR_CURVE` 和 `COM_SPOOLUP_TIME`。

## 当前验证与剩余台架项目

已完成 `hkust_nxt-dual_default` 原生编译，消息、参数、Commander、`mc_att_control`、`track_control` 与 `uart_tracker` 均通过编译链接。固件 FLASH 使用率 98.39%，后续增加功能前必须关注空间。

仍需拆桨台架和实飞验证：相机安装轴及符号、中心目标对应 `[0,0,-1]`、twist 正方向、模式往返、目标跳变、短时/长期丢失、RC 丢失、推力无阶跃、单一姿态设定源、安全回退和大倾角限制。