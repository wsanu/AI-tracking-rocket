# 自动追踪小火箭

这是一个从空目录开始搭建的 PX4 自动追踪小火箭工程仓库。目标是把闭源视觉识别/追踪模块通过 UART 输出的目标信息，接入 PX4 固件，并转换成飞控内可消费的 uORB 消息。

当前仓库不直接包含完整 PX4 源码，而是提供一个可合入 PX4-Autopilot 的 overlay：

- `px4_tracker_integration/src/modules/uart_tracker`: PX4 模块源码
- `px4_tracker_integration/msg/tracker_target.msg`: 新增 uORB 消息
- `docs`: 架构和 UART 协议说明
- `scripts`: 把 overlay 安装到 PX4 源码树的脚本
- `tools`: 生成/检查 UART 测试帧的小工具
- `tests`: 无飞控硬件时的协议级单元测试

官方 PX4 固件仓库：

https://github.com/PX4/PX4-Autopilot

## 数据流

```text
闭源视觉模块
  -> UART
  -> PX4 uart_tracker 模块
  -> tracker_target uORB
  -> 后续制导/云台/任务控制模块
```

## 默认 UART 协议

在闭源模块真实协议未给出前，仓库先定义一个工程可调试协议。后续适配真实协议时，主要修改：

```text
px4_tracker_integration/src/modules/uart_tracker/UartTracker.cpp
decode_tracker_payload()
```

默认帧格式：

```text
AA 55 LEN MSG_ID PAYLOAD CRC16_LE
```

详见 [docs/uart_protocol.md](docs/uart_protocol.md)。

## 合入 PX4 固件

假设 PX4-Autopilot 在 `E:\PX4-Autopilot`：

```powershell
.\scripts\install_px4_overlay.ps1 -Px4Root E:\PX4-Autopilot
```

脚本会复制 `tracker_target.msg` 和 `uart_tracker` 模块，并默认尝试为 `px4/fmu-v6x` 与 `px4/sitl` 启用 `CONFIG_MODULES_UART_TRACKER=y`。

如果使用其他目标板，需要在对应板配置里启用模块，例如 `boards/<vendor>/<board>/default.px4board` 加入：

```text
CONFIG_MODULES_UART_TRACKER=y
```

编译示例：

```powershell
make px4_fmu-v6x_default
```

## 飞控上启动

```sh
uart_tracker start -d /dev/ttyS2 -b 115200 --width 1280 --height 720 --hfov 62 --vfov 48
listener tracker_target
```

## 本地验证

生成一帧测试数据：

```powershell
python .\tools\make_tracker_frame.py --x 640 --y 360 --w 120 --h 80 --confidence 90 --valid
```

运行协议测试：

```powershell
python -m unittest discover -s tests
```

## 当前限制

- 本机网络无法连接 GitHub，尚未把 PX4-Autopilot 拉到本目录编译验证。
- 闭源识别模块真实 UART 协议未知，当前实现使用占位协议。
- 当前只完成“识别目标信息 -> PX4 uORB 可执行数据”的第一段；闭环控制律、发射安全逻辑和执行机构控制需要在拿到硬件约束后单独实现。
