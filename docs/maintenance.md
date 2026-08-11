# 工程维护说明

## 单一事实来源

- TRACK与UART功能源码：`px4_tracker_integration/`。
- 上游PX4版本：`reproducibility/px4-base.json`。
- 上游设计依据：`design_sourse/`，只读保留。
- 厂商示例：`uart_example/`，只读保留。
- `PX4-Autopilot/`只是生成出来的编译工作树，不在其中单独维护项目功能。

目录名 `design_sourse` 存在历史拼写问题，但为了保持路径兼容不改名。

## 修改流程

1. 阅读三份设计输入，确认基础慧眼协议行为不会因TRACK改动而回退。
2. 在 `px4_tracker_integration/` 修改消息或模块。
3. 若PX4接入点变化，同时更新：
   - `scripts/reproduce_px4.py`（跨平台、主要复现入口）；
   - `scripts/install_track_mode_overlay.ps1`（Windows兼容入口）。
4. 不允许只在本地 `PX4-Autopilot/` 修改后直接编译；所有必要改动必须能由仓库脚本重新生成。
5. 更新架构、协议、复现说明和测试记录。
6. 运行仓库测试、干净PX4覆盖测试和目标固件编译。

## 固定PX4基线

当前基线：

```text
repository: https://github.com/PX4/PX4-Autopilot.git
commit: 99c40407ffd7ac184e2d7b4b293f36f10fe561ef
target: hkust_nxt-dual_default
```

升级PX4时不能只修改JSON。必须在新提交上逐个重新确认补丁锚点、消息API、导航状态编号、Commander要求、QGC模式显示、板级FLASH容量和全部台架验收项目。

## 日常校验

```sh
python3 scripts/check_repository.py
python3 -m unittest discover -s tests
node --test tests/test_dashboard_protocol.mjs
node --test tests/test_dashboard_mavlink.mjs
```

Windows还应运行：

```powershell
.\scripts\verify_layout.ps1
```

验证现有PX4工作树：

```sh
python3 scripts/reproduce_px4.py \
  --px4-root ../PX4-Autopilot \
  --verify-only
```

## 干净环境覆盖测试

发布前必须在固定提交的全新PX4工作树中运行一次覆盖安装，并紧接着再运行第二次。第一次应成功添加全部文件和补丁，第二次应全部显示“already present/enabled”，证明脚本幂等。

随后执行：

```sh
make hkust_nxt-dual_default -j4
```

若出现补丁anchor不存在，不允许使用模糊文本替换绕过。先确定PX4版本是否错误，或者上游API是否发生变化。

## Windows与WSL一致性

推荐在WSL内直接克隆本项目和PX4，并使用跨平台脚本，避免维护Windows、WSL两个PX4副本。

必须比较两个副本时，仅比较源码和配置，排除 `.git/`、`build/`、对象文件、固件、NuttX生成文件和行尾差异。不要把整个构建目录从Windows复制到WSL。

## 发布记录

每个可复现版本至少记录：

- 本项目Git提交。
- PX4完整40位提交。
- 编译目标、主机系统和Arm工具链版本。
- 固件文件大小和SHA-256。
- `ver all`输出。
- UART帧数、解析错误、目标有效性。
- TRACK进入/退出和 `track_status`。
- 拆桨 `actuator_motors` 原始样本。
- 尚未验证的飞行风险。

结构化结果放在 `docs/test_records/` 和 `artifacts/test_records/`。大型 `.ulg`、固件二进制、编译目录和临时日志不提交，只记录外部位置与散列。

## 当前已知问题

- `uart_tracker`长选项会导致实例化失败；先使用板级默认参数。
- 离开TRACK后没有发布新的 `active=false` 状态，旧 `track_status` 可能被误读；当前模式以 `vehicle_status.nav_state` 为准。
- `hkust_nxt-dual` 固件通过 `rc.board_extras` 自动启动 `uart_tracker`，并在其后独立启动 `track_control`；每次发布仍应检查ROMFS中的顺序和唯一性。
- 本次自动启动固件FLASH为1,704,020 / 1,835,008字节（92.86%），继续增加模块前必须重新检查容量。
- HK38203 V2.1电流比例尚需按实际硬件标定。

## 发布检查清单

- [ ] 仓库结构、Python和两个Node测试通过。
- [ ] Windows与WSL PX4工作树首次安装、第二次幂等安装、模块文件集合和SHA-256验证通过。
- [ ] WSL/Ubuntu目标固件重新编译通过。
- [ ] ROMFS中 `uart_tracker start`、`track_control start` 各出现一次且顺序正确。
- [ ] 固件大小、散列、FLASH使用率和构建环境已记录。
- [ ] 上板板型、UART、目标、TRACK和拆桨电机输出通过。
- [ ] README与 `docs/reproduction.md` 命令已由另一环境照抄验证。
- [ ] 未提交密钥、个人参数备份、飞行日志、构建目录或固件二进制。
