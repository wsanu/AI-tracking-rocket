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

根据 `uart_example/serial_monitor-2.c`，Payload 为：

| 偏移 | 类型 | 名称 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u8` | 帧 ID | 0~255 循环 |
| 1 | `u8` | 总目标个数 | 最大 64 |
| 2 | `u8` | 当前报文目标个数 | 最大 22 |
| 3+N*11 | `u8` | 目标 ID | 当前目标 ID |
| 4+N*11 | `u8` | 目标类型 | 目标分类/类型 |
| 5+N*11 | `u8` | 置信度 | 0~100 |
| 6+N*11 | `u16` | x | 目标框 x |
| 8+N*11 | `u16` | y | 目标框 y |
| 10+N*11 | `u16` | w | 目标框宽度 |
| 12+N*11 | `u16` | h | 目标框高度 |

PX4 模块会从当前报文目标中选择置信度最高的目标，按 `x/y/w/h` 转换为框中心并发布 `tracker_target`。如果启用了 `--action gimbal`，AI 检测目标也会参与云台动作输出。

## 心跳报文

```text
78 07 00 83 06 DATA[6] CHK 79
```

根据 `uart_example/serial_monitor-2.c`，Payload 为：

| 偏移 | 类型 | 名称 | 说明 |
| ---: | --- | --- | --- |
| 0 | `u16` | 心跳计数 | 小端 |
| 2 | `u32` | 自检码 | `0` 表示正常，非零表示故障 |

PX4 模块会在 `uart_tracker status` 中显示心跳数量、最后心跳计数和最后自检码。

## PX4发送指令

`uart_tracker` 必须先启动，并以读写模式打开UART3：

```sh
uart_tracker start
uart_tracker send info
```

当前支持的慧眼V3.1命名指令：

| PX4命令 | 功能 |
| --- | --- |
| `uart_tracker send info` | 查询设备信息 |
| `uart_tracker send check` | 设备自检 |
| `uart_tracker send reboot` | 重启设备 |
| `uart_tracker send time <年> <月> <日> <时> <分> <秒> [NTP]` | 设置系统时间 |
| `uart_tracker send switch <0/1>` | 切换可见光/红外通道 |
| `uart_tracker send pip <0/1>` | 控制画中画 |
| `uart_tracker send capture` | 拍照 |
| `uart_tracker send record <0/1>` | 开始/停止录像 |
| `uart_tracker send file <操作>` | 文件操作 |
| `uart_tracker send zoom <0/1> <倍率>` | 电子变倍 |
| `uart_tracker send detect <0/1/2>` | 目标检测控制 |
| `uart_tracker send autolock <模式> <策略>` | 自动锁定控制 |
| `uart_tracker send track <模式> <目标ID> <x> <y> <w> <h>` | 跟踪控制 |
| `uart_tracker send cross <x> <y>` | 设置十字位置 |
| `uart_tracker send color <R> <G> <B>` | 设置OSD颜色 |
| `uart_tracker send text <行号> "<文字>"` | 显示自定义文字 |

发送帧为 `58 07 CMD0 CMD1 LEN DATA CHK 59`。NSH 命令先将完整帧放入单命令队列，实际串口写入由持有 `/dev/ttyS2` 文件描述符的 `uart_tracker` 任务执行，避免跨 NuttX 任务使用文件描述符导致 `EBADF (9)`。模块会继续在同一串口解析反馈帧；`uart_tracker status` 显示发送次数、错误次数、超时次数、响应次数及最后命令字。每次发送最多等待响应 2 秒，等待期间拒绝新的发送命令。发送功能要求慧眼RX连接飞控UART3 TX。

## UART 信息转换为动作

`uart_tracker` 现在支持把有效的 `00 81` 测偏数据转换为 PX4 云台动作输出。默认仍然是只解析和发布 `tracker_target`，不会输出动作；需要显式启用：

```sh
uart_tracker start -d /dev/ttyS2 -b 115200 --width 1280 --height 720 --hfov 62 --vfov 48 --action gimbal
```

动作输出话题：`gimbal_manager_set_manual_control`

转换规则：

- 目标有效且跟踪板处于运行状态时，按目标相对画面中心的水平/垂直角度误差输出 `yaw_rate` 和 `pitch_rate`。
- 目标丢失、数据无效或跟踪停止时，输出零速率，让云台停止继续追。
- `--deadband-deg <deg>` 设置小误差死区，默认 `0.5` 度。
- `--action-gain <value>` 设置动作增益，默认 `1.0`；最终速率会限幅到 `-1..1`，再由 PX4 云台参数 `MNT_RATE_PITCH` / `MNT_RATE_YAW` 转成实际角速度。

示例：

```sh
uart_tracker start -d /dev/ttyS2 -b 115200 --action gimbal --action-gain 0.7 --deadband-deg 1.0
listener gimbal_manager_set_manual_control
```

注意：PX4 的 gimbal manager 会检查 `origin_sysid/origin_compid` 是否是当前云台主控源。模块当前使用 `1/1` 作为板载动作源；如果 `listener gimbal_manager_set_manual_control` 能看到数据但云台不响应，需要把 gimbal manager 的主控源配置为匹配该来源，或后续把模块里的来源 ID 改成你的系统约定。

## 本地测试

```powershell
python .\tools\make_tracker_frame.py --offset-x 25 --offset-y -12 --w 120 --h 80 --valid
```