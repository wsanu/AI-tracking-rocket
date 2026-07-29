# 架构说明

## 目标

闭源视觉模块负责识别和追踪目标，PX4 负责接收追踪结果并转换成飞控内部可执行的数据接口。

第一阶段只做信息接入：

```text
UART bytes -> parser -> validated target -> bearing angles -> tracker_target uORB
```

这避免在协议还没稳定时直接改动 PX4 控制主链路。

## PX4 接入点

新增 PX4 模块：

```text
src/modules/uart_tracker
```

新增 uORB 消息：

```text
msg/tracker_target.msg
```

`uart_tracker` 作为独立任务运行，使用 PX4 的模块框架支持：

- `uart_tracker start`
- `uart_tracker stop`
- `uart_tracker status`

## 坐标转换

视觉模块输出像素坐标：

```text
image_x, image_y
```

PX4 模块转换为视线角：

```text
bearing_x_rad = normalized_x * hfov_rad / 2
bearing_y_rad = -normalized_y * vfov_rad / 2
```

其中：

```text
normalized_x = (image_x - width / 2) / (width / 2)
normalized_y = (image_y - height / 2) / (height / 2)
```

## 后续控制模块建议

建议不要让 UART 解析模块直接驱动舵机、电机或姿态控制。更稳妥的分层是：

```text
uart_tracker -> tracker_target -> guidance module -> actuator/control setpoint
```

这样可以单独加入目标丢失保护、置信度阈值、发射安全条件、速度限制、手动接管和 failsafe。
