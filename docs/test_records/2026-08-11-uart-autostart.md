# 2026-08-11 UART Tracker上电自启动验收记录

## 基线与范围

- 唯一功能源码：Windows `AI-tracking-rocket`。
- 主仓库基线提交：`c72ea08ec19bc70db2954963496624215a30d716`（当前main，v1.1.2基线）。
- PX4固定提交：`99c40407ffd7ac184e2d7b4b293f36f10fe561ef`（PX4 v1.15.4）。
- 构建目标：`hkust_nxt-dual_default`。
- 构建位置：WSL Linux文件系统 `/home/cokayear81/work/project_4/PX4-Autopilot`，不是Windows或 `/mnt/d` PX4工作树。
- 工具链：`arm-none-eabi-gcc 10.3.1 20210621`、Ninja 1.10.1、Python 3.10.12。
- 本任务未修改UART协议、uORB、TRACK模式、COM_FLTMODE/RC_MAP_FLTMODE或v1.1.2 RC AUX三档状态机。

## 同步前发现的旧文件

Windows和WSL PX4工作树均存在同一批旧覆盖：

- `UartTracker.cpp` SHA-256为 `e3975c9bfdefb36ee18008cc07b715c3f57bb413953b278edbc058bc0b7df8b1`，与主仓库不一致。
- `UartTracker.hpp` SHA-256为 `600b973cda680c4cdece2ae471c597c63cd802dfa0ad90a006c60c1b075fbf42`，与主仓库不一致。
- 两边均残留主仓库已删除的 `uart_tracker_params.c`，SHA-256为 `4e1e3ba80dfb707ac56584480c8f047e9cd144c172aac222da41b83cf24401a`。
- 旧WSL固件生成于2026-08-07，未作为本次验收结果。

清理前验证了四个模块目录的绝对路径、父目录、非符号链接/重解析点和PX4 HEAD。仅删除Windows/WSL两边的 `src/modules/uart_tracker` 与 `src/modules/track_control`，再从Windows主仓库重新安装。同步后旧参数文件消失，模块与消息的文件集合和SHA-256均一致。

## 自动化与覆盖验收

| 检查 | 结果 |
| --- | --- |
| `scripts/check_repository.py` | 通过 |
| Python unittest discovery | 13项通过 |
| `tests/test_tracker_protocol.py` 独立运行 | 6项通过 |
| Node dashboard测试 | 5项通过 |
| `scripts/verify_layout.ps1` | 通过 |
| PowerShell覆盖专项测试 | 通过 |
| `git diff --check` | 通过 |

启动规范化测试覆盖：空文件、只有 `track_control start`、正确顺序、反向顺序、重复命令、保留其他板级内容、连续执行两次输出不变、旧模块文件清除、非目录目标和越界模块名拒绝。

- Windows Python覆盖连续执行两次：通过；第二次未重复改写启动脚本。
- WSL Python覆盖连续执行两次：通过；第二次未重复改写启动脚本。
- Windows PowerShell实际覆盖连续执行两次：通过。
- Windows与WSL源 `rc.board_extras` SHA-256均为 `049c4f2a7d24cb5057a9515ba88ad0c3ee81736dd4c4e0426cc662109d78895e`。
- 两边 `uart_tracker start`、`track_control start` 各出现一次，前者位于后者之前，未使用 `&&`。
- 两边PX4 HEAD最终仍为 `99c40407ffd7ac184e2d7b4b293f36f10fe561ef`。

## 完整PX4编译与ROMFS

先精确删除 `/home/cokayear81/work/project_4/PX4-Autopilot/build/hkust_nxt-dual_default`，确保CMake、参数表、ROMFS和模块全部重新生成。

第一次构建推进到1146/1156后，Micro-XRCE-DDS-Client的外部依赖从GitHub下载Micro-CDR时发生三次SSL超时；此时 `uart_tracker` 和 `track_control` 静态库已经成功生成。随后在Windows临时下载Micro-CDR `v2.0.1`（提交 `3d1b17703c7cf4f22def2910bc845bdb5152d7b5`），通过单次进程级Git URL重写提供给WSL构建，未修改PX4源码、配置或全局Git设置。继续构建后成功完成剩余11步，临时Windows缓存已删除。

最终结果：

- `libmodules__uart_tracker.a`：生成成功。
- `libmodules__track_control.a`：生成成功。
- `hkust_nxt-dual_default.elf`：链接成功，36,729,256字节。
- `hkust_nxt-dual_default.bin`：生成成功，1,704,020字节。
- `hkust_nxt-dual_default.px4`：生成成功，1,603,948字节。
- FLASH：1,704,020 / 1,835,008字节，92.86%，剩余130,988字节，未溢出。
- 生成时间：2026-08-11 14:09:27 +08:00。
- PX4 SHA-256：`60fa2763523dcf28c082702a53ff4055c396a666c290e8f07fd92de50e9021e7`。
- 固件路径：`/home/cokayear81/work/project_4/PX4-Autopilot/build/hkust_nxt-dual_default/hkust_nxt-dual_default.px4`。

生成的ROMFS文件 `/home/cokayear81/work/project_4/PX4-Autopilot/build/hkust_nxt-dual_default/etc/init.d/rc.board_extras` 内容为：

```sh
uart_tracker start
track_control start
```

两条命令各出现一次且顺序正确；该ROMFS文件SHA-256为 `931c3024ba490213b1200eba567ba7f1a425271f5f81cc826667dbb02e67e4dd`。生成参数元数据和最终ELF中均未发现旧 `TRK_RC_AUX`。

新固件保留在WSL构建目录中，没有覆盖主仓库 `firmware/` 下的既有发布文件，也没有把PX4生成工作树改动作为主仓库源码。

## 真实硬件验收

本次执行会话没有可控的真实飞控、遥控器和慧眼视觉模块连接。以下项目均为**未执行**，不能由自动化、ROMFS检查或编译成功替代：

- 拆桨、完全断电、不连接MAVLink Console、不输入启动命令后重新上电。
- `uart_tracker status` 上电自动运行检查，以及 `/dev/ttyS2`、115200波特率确认。
- 真实慧眼UART数据接收。
- RC三档关闭检测、普通检测、多目标检测与自动锁定。
- 长时间保持档位不重复发送命令。
- 多次断电上电的自动启动重复性。
- 视觉设备未接入时其他PX4模块继续启动。

上板时必须按照 `docs/hardware_checklist.md` 的“自动启动断电验收”逐项补充真实结果。
