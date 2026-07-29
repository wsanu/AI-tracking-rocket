# UART 协议

这是当前仓库使用的默认工程协议。闭源模块真实协议确认后，应以真实协议为准。

## 帧格式

```text
AA 55 LEN MSG_ID PAYLOAD CRC16_LE
```

字段：

| 字段 | 长度 | 说明 |
| --- | ---: | --- |
| `AA 55` | 2 | 帧头 |
| `LEN` | 1 | `MSG_ID + PAYLOAD` 字节数 |
| `MSG_ID` | 1 | 消息类型 |
| `PAYLOAD` | N | 消息数据 |
| `CRC16_LE` | 2 | CRC-16/CCITT-FALSE，小端 |

CRC 计算范围：

```text
LEN MSG_ID PAYLOAD
```

## 目标追踪消息

`MSG_ID = 0x01`

Payload，小端：

| 偏移 | 类型 | 名称 | 说明 |
| ---: | --- | --- | --- |
| 0 | `uint16` | `image_x` | 目标中心 x 像素 |
| 2 | `uint16` | `image_y` | 目标中心 y 像素 |
| 4 | `uint16` | `box_w` | 目标框宽度 |
| 6 | `uint16` | `box_h` | 目标框高度 |
| 8 | `uint8` | `confidence` | 0..100 |
| 9 | `uint8` | `flags` | bit0 表示 valid |
| 10 | `uint32` | `source_age_ms` | 视觉模块输出延迟 |

Payload 长度为 14 字节。

## 示例

```powershell
python .\tools\make_tracker_frame.py --x 640 --y 360 --w 120 --h 80 --confidence 90 --valid
```
