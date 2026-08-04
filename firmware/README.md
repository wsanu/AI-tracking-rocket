# 固件归档清单

固件文件按验证阶段保留，禁止用同名新文件静默覆盖。烧录前应核对文件名、大小和 SHA-256。

| 文件 | 字节数 | SHA-256 | 说明 |
| --- | ---: | --- | --- |
| `nxtpx4v2-v1.15.4-baseline.px4` | 1691900 | `dcca7839f56fa83747687d0efbacf05cd69d9eb046f81528c35c4ef34ebdb543` | 基线 |
| `nxtpx4v2-v1.15.4-uart-tracker-20260803.px4` | 1698084 | `924b5ff1da0f5e2328a5aeaa7bd632fcaec0c2cedad491eee0f3e0a7049b784c` | UART Tracker |
| `nxtpx4v2-v1.15.4-uart-tracker-startupfix-20260803.px4` | 1698428 | `99c51fbbd2a3136080ff3dd375d165e906d4180b180a8872eca87acba81812c1` | UART 启动修正 |
| `nxtpx4v2-v1.15.4-track-20260803.px4` | 1711988 | `ae7803cf7aa9c9dd6cf24f4757d237746d0691559259f6ab06bbcd33b7677432` | TRACK 台架验证版本 |

最后一项已于 2026-08-04 完成真实 UART、模式切换和拆桨四电机差动输出验证，尚未完成实飞验证。
