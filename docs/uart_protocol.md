# 慧眼 V3.1 UART 协议摘录

来源：`慧眼_串口通信协议v3.1.docx`。

## 串口设置

- 波特率：默认 115200 bps，可配置
- 起始位：1
- 数据位：8
- 停止位：1
- 校验位：无
- 多字节字段：小端，低字节在前

## 主控板发送帧

```text
58 07 CMD0 CMD1 LEN DATA... CHK 59
```

## 跟踪板反馈帧

```text
78 07 CMD0 CMD1 LEN DATA... CHK 79
```

字段：

| 字段 | 长度 | 说明 |
| --- | ---: | --- |
| 帧头 | 2 | 发送 `58 07`，反馈 `78 07` |
| `CMD0` | 1 | 主命令字 |
| `CMD1` | 1 | 副命令字。反馈方向通常 `CMD1 > 0x80` |
| `LEN` | 1 | `DATA` 字节数 |
| `DATA` | N | 数据区 |
| `CHK` | 1 | 从 `CMD0` 到最后一个 `DATA` 的 8 位累加和 |
| 帧尾 | 1 | 发送 `59`，反馈 `79` |

校验算法：

```c
uint8_t CalcCheckNum(uint8_t *data, uint8_t dataLen)
{
    uint32_t num = 0;
    for (size_t i = 2; i < dataLen - 2; i++) {
        num += data[i];
    }
    return num & 0xff;
}
```

等价地，对 `CMD0 CMD1 LEN DATA...` 求和后取低 8 位。

## 测偏数据报文

这是当前 PX4 `uart_tracker` 使用的主要闭环输入。

```text
78 07 00 81 0E DATA[14] CHK 79
```

`CMD0 = 0x00`：周期性报文  
`CMD1 = 0x81`：测偏数据/脱靶量反馈

Payload：

| 偏移 | 类型 | 名称 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u8` | 状态 | bit2: 0=像素 int32, 1=角度 float；bit1: 0=运行, 1=停止；bit0: 0=无效, 1=有效 |
| 1 | `u8` | 视频通道 ID | 当前跟踪视频通道 ID |
| 2 | `int32/float` | 左右测偏量 | 右正左负。像素模式精度 1 px |
| 6 | `int32/float` | 上下测偏量 | 上正下负。像素模式精度 1 px |
| 10 | `u16` | 目标宽度 | 像素 |
| 12 | `u16` | 目标高度 | 像素 |

说明：

- 测偏量是目标中心相对十字中心的偏移。
- 默认输出像素值。
- 如果 bit2 为 1，则偏移字段按 float 角度解释；PX4 侧当前按“度”转换为弧度。

## AI 目标检测报文

```text
78 07 00 82 LEN DATA... CHK 79
```

文档说明：

- `byte0`: 帧 ID，0~255 循环
- `byte1`: 总目标个数，最大 64
- `byte2`: 当前报文目标个数，最大 22
- `byte3~byteN`: Targets，按当前报文目标个数决定，每个目标 11 字节

当前文档正文未展开每个目标 11 字节的字段布局，所以 PX4 模块暂不把 `00 82` 用于控制闭环。

## 本地测试

```powershell
python .\tools\make_tracker_frame.py --offset-x 25 --offset-y -12 --w 120 --h 80 --valid
```
