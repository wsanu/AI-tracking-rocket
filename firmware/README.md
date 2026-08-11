# 固件归档清单

固件文件按验证阶段保留，禁止用同名新文件静默覆盖。烧录前应核对文件名、大小和 SHA-256。

| 文件 | 字节数 | SHA-256 | 说明 |
| --- | ---: | --- | --- |
| `nxtpx4v2-v1.15.4-baseline.px4` | 1691900 | `dcca7839f56fa83747687d0efbacf05cd69d9eb046f81528c35c4ef34ebdb543` | 基线 |
| `nxtpx4v2-v1.15.4-uart-tracker-20260803.px4` | 1698084 | `924b5ff1da0f5e2328a5aeaa7bd632fcaec0c2cedad491eee0f3e0a7049b784c` | UART Tracker |
| `nxtpx4v2-v1.15.4-uart-tracker-startupfix-20260803.px4` | 1698428 | `99c51fbbd2a3136080ff3dd375d165e906d4180b180a8872eca87acba81812c1` | UART 启动修正 |
| `nxtpx4v2-v1.15.4-track-20260803.px4` | 1711988 | `ae7803cf7aa9c9dd6cf24f4757d237746d0691559259f6ab06bbcd33b7677432` | TRACK 台架验证版本 |
| `nxtpx4v2-release-v1.1.px4` | 1602652 | `5f8d1256de8803ae6119938ac95f0c49ca78903227fbd7c62510a1bd6fe349c7` | Release V1.1: UART3 bidirectional control |
| `nxtpx4v2-release-v1.1.1.px4` | 1603608 | `9d6a762e7bd7a68b896ae73aa101594b6d205f675598fe58fb376b49c65fbbcd` | Release V1.1.1: UART send task-context fix |
| `nxtpx4v2-release-v1.1.2.px4` | 1604704 | `e917be398df765476c5f40de3e138b44698f7a73a8b7608c9f3d5eb6088dc430` | Release V1.1.2: RC AUX switch tracker control |

`nxtpx4v2-v1.15.4-track-20260803.px4` 已于 2026-08-04 完成真实 UART、模式切换和拆桨四电机差动输出验证，尚未完成实飞验证。V1.1.1 已完成自动测试和完整固件编译。V1.1.2 已于 2026-08-07 完成拆桨台架 RC 三段开关 ↔ 慧视双向 UART 验证，sent 5 errors 0 timeouts 0 responses 5。
