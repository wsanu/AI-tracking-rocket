# 从零复现 HKUST NXT Dual TRACK 固件

本文面向第一次接触本仓库的人，目标是在一台全新电脑上，从 GitHub 源码构建出可刷入 `HKUST_NXT_DUAL` 飞控的固件，并在拆桨状态下复现 UART 目标数据和四电机差动输出。

## 1. 复现范围

已验证组合：

| 项目 | 已验证值 |
| --- | --- |
| 飞控 | HKUST NXT Dual / NxtPX4v2，`HW arch: HKUST_NXT_DUAL` |
| PX4 基线 | 1.15.4，提交 `99c40407ffd7ac184e2d7b4b293f36f10fe561ef` |
| 编译目标 | `hkust_nxt-dual_default` |
| 主机环境 | Ubuntu 22.04 或 Windows 10/11 + WSL2 Ubuntu 22.04 |
| 已验证编译器 | GNU Arm Embedded 10.3.1 20210621 |
| 视觉模块 | Viztra LE071 / 慧眼 V3.1 UART 协议 |
| 视觉串口 | UART3（USART3），PX4 `/dev/ttyS2`，115200 baud |
| 电调 | HK38203 V2.1，四路电机输出 |
| 遥控链路 | MicoAir LR24-F-mini V1.0，ELRS/CRSF |

其他飞控板型没有经过验证。不要仅修改编译目标就直接上桨测试，因为启动脚本、UART设备名、RC串口和输出映射可能不同。

## 2. 安全前提

- 编译和协议测试可以不连接飞机。
- 固件刷写时断开动力电池，只用USB直连电脑。
- 接收机、电机和TRACK首次联调必须拆下所有螺旋桨并固定机体。
- 台架输出通过不代表具备飞行条件。
- 不要导入其他飞机的传感器校准、RC端点、电池比例、执行器或PID参数。

## 3. 获取本项目

在 WSL/Ubuntu 中执行：

```sh
git clone https://github.com/wsanu/AI-tracking-rocket.git
cd AI-tracking-rocket
git checkout main
```

确认项目自检通过：

```sh
python3 scripts/check_repository.py
python3 -m unittest discover -s tests
```

JavaScript仪表板测试需要 Node.js 18 或更高版本：

```sh
node --test tests/test_dashboard_protocol.mjs
node --test tests/test_dashboard_mavlink.mjs
```

## 4. 创建固定版本的 PX4 工作树

不要使用任意最新 PX4。仓库的 `reproducibility/px4-base.json` 固定了经过编译验证的上游提交。

在项目根目录执行：

```sh
python3 scripts/reproduce_px4.py \
  --px4-root ../PX4-Autopilot \
  --clone
```

此命令将：

1. 克隆官方 `PX4/PX4-Autopilot`。
2. 检出固定提交 `99c40407…`。
3. 初始化对应子模块。
4. 安装 `uart_tracker`、`track_control` 和两条 uORB 消息。
5. 以可重复、幂等方式加入 TRACK 模式接入点。
6. 为 `hkust_nxt-dual_default` 和 SITL 启用模块。

已有工作树可省略 `--clone`。脚本发现PX4版本不一致且存在已修改文件时会停止，不会覆盖这些修改。

验证覆盖层而不写文件：

```sh
python3 scripts/reproduce_px4.py \
  --px4-root ../PX4-Autopilot \
  --verify-only
```

Windows PowerShell也可使用现有脚本：

```powershell
.\scripts\install_px4_overlay.ps1 -Px4Root E:\path\to\PX4-Autopilot
```

该PowerShell脚本会自动调用TRACK覆盖脚本，不需要再执行第二次。

## 5. 安装 PX4 工具链

在固定版本PX4工作树中运行其自带安装脚本：

```sh
cd ../PX4-Autopilot
bash Tools/setup/ubuntu.sh --no-sim-tools
```

安装完成后关闭并重新打开WSL终端，检查：

```sh
arm-none-eabi-gcc --version
python3 --version
git --version
```

PX4 v1.15官方推荐Ubuntu 22.04/20.04/18.04；Windows使用WSL2。若使用不同Ubuntu或不同工具链，固件散列可能不同，应以成功编译、容量、板型和功能验收为准。

## 6. 编译

```sh
cd ../PX4-Autopilot
make hkust_nxt-dual_default -j4
```

预期输出文件：

```text
build/hkust_nxt-dual_default/hkust_nxt-dual_default.px4
```

记录证据：

```sh
ls -lh build/hkust_nxt-dual_default/hkust_nxt-dual_default.px4
sha256sum build/hkust_nxt-dual_default/hkust_nxt-dual_default.px4
```

已验证参考固件为1711988字节，SHA-256为：

```text
ae7803cf7aa9c9dd6cf24f4757d237746d0691559259f6ab06bbcd33b7677432
```

PX4会写入构建时间、工具链和构建URI，因此换电脑后散列不同不一定表示功能不同。必须确认编译目标正确、没有FLASH溢出，并继续完成上板验收。

## 7. 刷写固件

1. 拆桨并断开动力电池。
2. 打开桌面版QGroundControl，进入“车辆设置 → 固件”。
3. 用USB将飞控直接连接电脑，不使用USB Hub。
4. 勾选高级设置，选择“自定义固件文件”。
5. 选择上一步生成的 `hkust_nxt-dual_default.px4`。
6. 等待擦除、写入、校验和重启完成。

刷写后在MAVLink Console执行：

```sh
ver all
```

必须看到：

```text
HW arch: HKUST_NXT_DUAL
PX4 version: 1.15.4
```

## 8. 接线

视觉模块使用3.3 V TTL UART：

```text
慧眼 TX  -> 飞控 UART3（USART3） RX
慧眼 RX  -> 飞控 UART3（USART3） TX（发送控制指令时必须连接）
慧眼 GND -> 飞控 GND
```

禁止把RS-232电平直接接入飞控。视觉模块供电必须满足其说明书，不能因为UART有电平就假设可以由信号口供电。

板级定义中ELRS/CRSF RC串口为 `/dev/ttyS4`。物理插针、电源电压和TX/RX交叉连接必须按HKUST NXT Dual与LR24-F-mini资料复核，不凭软件设备名猜插针。

电调插头按板上标识连接 `GND / VBAT / PWM4 / PWM3 / PWM2 / PWM1 / RX7 / Current`。接入动力电池前再次确认VBAT极性、电机编号和电流采样电压范围。

完整接线检查见 `docs/hardware_checklist.md`。

## 9. QGroundControl基础配置

固件刷写后必须重新完成：

- 机架：四旋翼，确认电机几何布局。
- 传感器：加速度计和陀螺仪，按实际配置处理磁罗盘。
- 遥控器：ELRS/CRSF连接、通道方向、中心点和端点校准。
- Actuators：电机1–4映射、逐个电机测试和旋向。
- 电源：电池节数、电压比例和电流比例。
- 安全：解锁检查、RC丢失行为、电池失效行为和返航条件。

QGC左侧任何红色未完成项都不应忽略。禁止直接导入本项目作者飞机的完整参数备份。

经过台架测试的开关映射为：模式通道选择位置1=Stabilized、位置4=Altitude、位置6=Track；独立Arm开关使用另一个RC通道。实际通道号以QGC Channel Monitor为准。

TRACK项目参数可在NSH中逐行执行 `reproducibility/track-parameters.nsh`。其中 `COM_FLTMODE6=16` 只适用于把Track分配到模式位置6的配置。

## 10. 启动与拆桨验收

`hkust_nxt-dual` 的板级 `rc.board_extras` 会在每次上电时自动执行：

```sh
uart_tracker start
track_control start
```

两条命令独立执行，不使用 `&&`。正常情况下无需连接电脑或手动输入 `uart_tracker start`。每次重启后可检查：

```sh
track_control status
uart_tracker status
```

若UART模块未运行，先用 `dmesg` 检查 `/dev/ttyS2` 打开失败或任务退出，再按需诊断：

```sh
uart_tracker status
uart_tracker stop
uart_tracker start
dmesg
```

当前版本自动启动使用板级默认设备 `/dev/ttyS2`、115200 baud、1280×720、HFOV 62°和VFOV 48°。不要使用已知有问题的 `--width/--height/--hfov/--vfov` 长选项启动。模块已经运行时再次执行 `uart_tracker start`，预期会报告已经运行或拒绝重复实例。

`uart_tracker` 在独立任务中打开UART；视觉设备未接入或暂时无响应不会阻止 `track_control` 和其他PX4模块继续启动。自动启动也不会替代 `RC_MAP_AUX1` 等遥控器映射配置。

按顺序验收：

```sh
uart_tracker status
listener tracker_target -n 5
listener vehicle_status -n 1
listener track_status -n 5
listener actuator_motors -n 20 -r 5
```

通过标准：

| 检查项 | 通过条件 |
| --- | --- |
| UART | frames持续增长，parse errors保持0 |
| 目标 | 有目标时 `valid=True`、`tracking_state=2`、方向随目标移动 |
| 中心方向 | 目标位于画面中心时接近 `[0, 0, -1]` |
| TRACK模式 | `vehicle_status.nav_state=9` |
| TRACK状态 | `active=True`、`target_valid=True`、无failsafe |
| 姿态设定 | `attitude_setpoint_q` 随目标方向平滑变化 |
| 电机 | 拆桨解锁后1–4通道产生合理差动，其余未分配通道为NaN |
| 模式退出 | 拨回Stabilized/Altitude后 `nav_state` 离开9 |

离开TRACK后 `track_status` 可能保留最后一帧并因没有新消息而超时，所以当前模式以 `vehicle_status.nav_state` 为准。

## 11. 上桨前停止条件

以下任一项存在时不得上桨：

- `pre_flight_checks_pass=False`。
- 电机编号、旋向或桨叶方向不确定。
- RC任一通道方向或失控保护未验证。
- 目标中心方向不是 `[0, 0, -1]` 附近。
- UART存在解析错误或目标有效位跳变异常。
- TRACK无法可靠退出到人工模式。
- 电池、电流传感器比例未标定。
- 机体没有先在普通Stabilized/Altitude模式完成稳定悬停。

拆桨复现完成后，按照 `docs/test_records/2026-08-04-track-bench.md` 的下一阶段项目逐项进行低空飞行验证。

## 12. 常见问题

### `failed to instantiate object`

先只执行 `uart_tracker start`。当前长选项解析存在已知问题。

### 找不到 `hkust_nxt-dual_default`

确认PX4 HEAD为锁定提交，并检查：

```sh
git rev-parse HEAD
make list_config_targets | grep hkust_nxt-dual
```

### 覆盖脚本提示anchor不存在

说明PX4版本不匹配或工作树已被其他补丁修改。不要强行替换；重新创建锁定提交的干净工作树。

### FLASH溢出

当前验证版本FLASH使用率约98.39%，没有足够空间继续随意启用模块。确认使用仓库默认板级配置，并清理旧构建后重试。

### 解锁后自动上锁

`COM_DISARM_PRFLT` 是起飞前自动上锁等待时间，`COM_DISARM_LAND` 是落地后自动上锁等待时间。它们不是TRACK功能参数，也不能用于绕过预飞检查。
