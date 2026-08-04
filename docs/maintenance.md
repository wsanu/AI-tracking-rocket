# 工程维护说明

## 维护原则

`px4_tracker_integration/` 是项目功能源码的唯一维护入口。`PX4-Autopilot/` 是用于编译和上板的工作树，通过脚本同步覆盖层；不要在两个位置分别修改同一功能。

`design_sourse/` 和 `uart_example/` 分别保存设计依据与厂商示例，均按只读参考资料管理。目录名 `design_sourse` 虽有历史拼写问题，但为保持既有路径兼容，不改名。

本次整理只调整文档、测试证据和非源码文件，没有改动功能代码。

## 标准维护流程

1. 阅读 `design_sourse/` 中《用户使用说明》《通讯协议》和《PX4 TRACK 目标指向飞行模式设计报告》。已有基础协议行为不得因 TRACK 功能而回退。
2. 在 `px4_tracker_integration/` 修改消息、模块或参数定义。
3. 运行 `scripts/install_px4_overlay.ps1` 和 `scripts/install_track_mode_overlay.ps1`，同步到 Windows PX4 工作树。
4. 将相同工作树同步到 WSL；比较时排除 `build/`、`.git/` 和 NuttX 自动生成文件。
5. 在 WSL 编译 `hkust_nxt-dual_default`。
6. 运行 Python、JavaScript 和目录结构测试。
7. 上板后按“串口 → 目标消息 → TRACK 状态 → 姿态设定 → 拆桨电机输出”的顺序验证。
8. 将固件散列、配置、原始数据和结论写入 `docs/test_records/` 与 `artifacts/test_records/`。

## Windows 与 WSL 一致性

比较的是受维护的源码和配置，不比较编译产物。建议在 WSL 中使用：

```sh
diff -qr --strip-trailing-cr /mnt/e/project/rocket_tracker/PX4-Autopilot \
  /home/wsanu/rocket_tracker/PX4-Autopilot \
  -x .git -x build -x '*.o' -x '*.a' -x '*.elf' -x '*.px4'
```

若有差异，先判断它属于覆盖层源码、PX4 上游文件还是生成文件；不要直接用整个目录覆盖另一侧。

## 发布检查清单

- `python -m unittest discover -s tests` 通过。
- 两个 `node --test` 协议测试通过。
- `scripts/verify_layout.ps1` 通过。
- WSL 编译无错误，并记录 `.px4` 文件大小与 SHA-256。
- 上板 `ver all` 与预期板型、PX4 版本、构建时间一致。
- `uart_tracker status` 无解析错误，心跳与检测帧持续增长。
- `tracker_target` 的坐标符号、中心方向 `[0, 0, -1]`、置信度和有效位符合协议。
- TRACK 进入、退出、目标短时/长期丢失、RC 丢失和姿态限幅均按设计报告验证。
- 首次执行控制链测试时拆桨并固定机体。

## 当前已知问题

- `uart_tracker start` 的长选项当前会导致实例化失败；板级默认参数启动正常。修复前使用默认参数。
- 离开 TRACK 时未发布一帧 `active=false`，所以 `track_status` 可能显示旧状态并在监听时超时；当前模式以 `vehicle_status.nav_state` 为准。
- `uart_tracker` 重启后需要确认是否已自动启动，未启动时手动执行 `uart_tracker start`。
- 当前固件 FLASH 使用率约 98.39%，继续增加功能前必须检查容量。
- HK38203 V2.1 的电流采样比例尚未在本项目记录中标定。

## 尚未完成的验证

- 带桨低空悬停以及 TRACK 进入/退出。
- 目标跳变、短时遮挡和长期丢失。
- RC 丢失、模式开关回退与紧急停机。
- yaw twist 正方向、最大姿态变化率和最大倾角。
- 飞行状态下推力连续性与控制器饱和情况。
