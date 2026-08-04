# 2026-08-04 TRACK 拆桨台架测试记录

## 结论

测试证明以下链路已接通：慧眼目标数据经 UART 进入 `uart_tracker`，发布 `TrackerTarget`，`track_control` 在 TRACK 模式生成姿态设定值，姿态/角速度控制器和控制分配器最终产生四路不同的电机输出。

本结论仅适用于拆桨台架，不代表已经具备安全飞行条件。

## 测试配置

- 飞控：HKUST NXT Dual，硬件标识 `HKUST_NXT_DUAL`
- PX4：1.15.4，Git `99c40407ffd7ac184e2d7b4b293f36f10fe561ef`
- 固件：`hkust_nxt-dual_default.px4`
- 固件大小：约 1.7 MiB
- SHA-256：`ae7803cf7aa9c9dd6cf24f4757d237746d0691559259f6ab06bbcd33b7677432`
- 电调：HK38203 V2.1
- 接收机：MicoAir LR24-F-mini V1.0（ELRS）
- 目标串口：`/dev/ttyS7`，115200 baud
- 飞行模式：`COM_FLTMODE6=16`（Track）
- 安全条件：已拆桨，机体固定

## 软件与编译结果

- `python -m unittest discover -s tests`：3 项通过。
- JavaScript 协议和 MAVLink 测试：使用工作区附带 Node 执行通过。
- `scripts/verify_layout.ps1`：`Repository layout OK`。
- WSL：`make hkust_nxt-dual_default -j4`，`ninja: no work to do`，现有构建有效。

## 串口与目标数据

`uart_tracker status` 实测：

- frames：97720
- parse errors：0
- AI detection frames：9444
- targets：1/1
- heartbeats：499

有效目标时 `tracker_target` 的 `confidence=1.0`、`valid=True`、`tracking_state=2`，方向与像素位置随目标移动。样例消息的新鲜度约 1–8 ms。

## TRACK 控制状态

进入 Track 后 `vehicle_status.nav_state=9`；离开后回到 `nav_state=15`。有效目标下：

- `track_status.active=True`
- `target_valid=True`
- `failsafe_active=False`
- `failure_reason=0`
- `attitude_setpoint_q` 随目标方向变化

`thrust_setpoint=0` 是当前记录中的控制状态字段，不能单独据此判断电机是否有输出；实际输出应观察 `actuator_motors`。

## 四路电机输出

20 个样本中四个已分配电机通道均出现有效、非一致输出，其余通道为 `NaN`，符合四旋翼控制分配的预期形式。

| 通道 | 最小值 | 最大值 | 平均值 |
| --- | ---: | ---: | ---: |
| Motor 1 | 0.04134 | 0.15074 | 0.07755 |
| Motor 2 | 0.01346 | 0.14540 | 0.08809 |
| Motor 3 | 0.00000 | 0.11157 | 0.02433 |
| Motor 4 | 0.00000 | 0.11157 | 0.03318 |

原始数值已整理到 `artifacts/test_records/2026-08-04-actuator-motors.csv`。

## 已知现象

- `uart_tracker start` 使用长选项时实例化失败，使用板级默认参数启动正常。
- 离开 TRACK 后 `track_status` 可能保留最后一帧并因无新消息而超时，因此模式判断使用 `vehicle_status.nav_state`。
- 测试过程中预飞检查状态随配置和解锁条件变化；拆桨电机验证通过不等于完整预飞检查通过。

## 下一阶段

完成传感器校准、机架方向、电机顺序和旋向、失控保护后，依次进行带桨低空悬停、空中进入/退出 Track、目标跳变与丢失、RC 丢失、twist 方向、倾角限制及推力连续性测试。
