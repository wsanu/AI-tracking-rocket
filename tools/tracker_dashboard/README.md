# 慧眼 UART 监视器

这是面向 NxtPX4v2 与慧眼 V3.1 串口协议的只读调试界面，支持：

- 慧眼原始反馈帧 `78 07 00 81 0E ... CHK 79`
- PX4 USB MAVLink `DEBUG_FLOAT_ARRAY` 跟踪遥测
- 无硬件模拟器
- 动态准星、目标框、偏差、目标尺寸、帧率和错误计数
- 原始帧查看与 CSV 导出

## 启动

在 WSL 中执行：

```bash
cd '/mnt/e/project/rocket_tracker'
python3 -m http.server 8765 --directory tools/tracker_dashboard
```

然后使用 Windows Chrome 或 Edge 打开：

```text
http://localhost:8765
```

先选择“模拟器”验证界面。直接验证闭源模块时，使用 3.3 V USB-TTL 转接器连接模块 UART，再在界面中选择“USB 串口”。

## NxtPX4v2 USB

PX4 固件中的 `uart_tracker` 会把跟踪结果发布为名为 `TRK_V31`、ID 为 `0x8100` 的只读 `DEBUG_FLOAT_ARRAY`。浏览器连接 NxtPX4v2 USB 后会自动筛选该遥测。

PX4 默认可能只以较低频率发送调试流。需要提高刷新率时，在 PX4 NSH 中执行：

```sh
mavlink stream -d /dev/ttyACM0 -s DEBUG_FLOAT_ARRAY -r 20
```

浏览器和 QGroundControl 不应同时占用同一个 USB 串口。

## 接线建议

推荐将闭源模块连接到 NxtPX4v2 的 `TELEM4 / UART8`：

```text
模块 TX  -> NxtPX4v2 UART8 RX
模块 RX  -> NxtPX4v2 UART8 TX（当前只读解析可不接）
模块 GND -> NxtPX4v2 GND
```

串口信号必须是 3.3 V TTL。不要把 RS-232 电平直接接入飞控。
