# V4_R1_GIT_EXE_BASELINE.md — 仓库与可执行文件基线（Agent 实测，2026-10-17 16:42）

## Git

| 检查 | 结果 |
|---|---|
| `git rev-parse HEAD` | **3adaea4282a2282a3d340f3811b433d334031800** ✅ MATCH |
| `git status --porcelain` staged | **0** ✅ |
| `git diff --check` | **PASS**（exit 0） ✅ |
| `git tag --list` | `v1.0.0`、`v2.0.0`（历史 tag，本轮未创建） |
| 产品源码未授权变动 | **无**（工作树改动仅为前几轮 docs/acceptance 归档，均属既有授权范围） |

## 冻结可执行文件

| 目标 | 路径 | SHA-256 | 大小 | 判定 |
|---|---|---|---|---|
| 冻结 Release | `build/release/modbuslens.exe` | `4044edae6933ed98acd6424483399dbcdd5185b26840ebfd6dc5008f14a93fe5` | — | **MATCH** ✅ |
| C2.1 Debug（本轮验收对象） | `build/debug-h2/modbuslens.exe` | `9cadc2c53d80e7b74e9a4bf65dd042068d58b78a2e4e128f7b573c9445876915` | 83,084,566 B | **MATCH** ✅ |

## 验收程序与启动方式（从仓库实际确认，非猜测）

- 验收对象 = **C2.1 Debug EXE**（`build/debug-h2/modbuslens.exe`），与 R3/C2.2 生产路径契约一致。
- 正常 GUI 启动方式 = 直接运行该 EXE（自包含 debug 构建，无 QML 测试参数、无 demo/replay/simulation 参数）。
- 隔离启动器存在：`build/debug-h2/start_modbuslens_debug.ps1`（仅设置 PATH 前置与工作目录，不注入测试参数）——本轮 GUI 启动将使用它或直接启动 EXE。
- **未重新编译**；**未更换 EXE**；无 canonical 冲突。

## R3 证据归属（索引，不修改旧文件）

- `V3_R2_V2_RETEST.log`（2,220 B，文件时间 2026-10-17 16:14）——**内容为 R3 于 16:12 的 V2 复验实测日志**（A 8/8 EXACT、B 7/7、C 分段 7/7、D roundtrip，含 `16:12:14` 时间戳）。文件名沿用 R2 占位名属于命名问题；**R3 原始证据归属成立**（可由时间戳与内容独立确认），R3 报告 §C 已如实说明。**本文件不改写、不覆盖**。
- `V3_R3_FULL_MATRIX.log`（19,830 B，16:14）——R3 矩阵汇总；权威原始场景证据为 `slave_log_*.log` / `probe_log_*.log`（UTF-8）。
- 结论：**R3 证据门禁满足**（V2 4/4、V3 8/8、原始日志属 R3 本轮、测试后 Pair 正常）。
