# 2026-09-14 — 简历性能声明核验 + benchmark 证据落库（maintenance，docs/scripts-only）

## 背景

用户提供简历文本要求核验其中所有项目相关数据。核验发现两件事：

1. **时间范围错误**：简历写"2026.03 ~ 2025.05"（且倒序）；Git 实证项目实际为 2026-09-05（T001）~ 2026-09-14（T015 最终验收）。已如实反馈用户。
2. **性能数字无仓库痕迹**：70.9 ms / 141 万条每秒 / 686.7 ms 等在任何文档中都搜不到（违反 AGENTS.md 纪律 13 的精神——重要数据必须落仓库）。其余声称项（7 状态、14 Issue、三模式共享核心、ExpectedNoResponse、3 只读工具）均有代码/文档实证。

## 做法

- 同机同工具链独立复测：配置 `release-local` preset → 构建 `libmodbuslens_core.a` → 生成确定性混合样本（100k/1M）→ 手写 harness 测**生产 Replay 链路**（读文件 → parse → analyze+统计，单线程 -O3），多轮取中位数。
- 核验结论：数量级一致；1M 档去 IO 同口径偏差 ≈4%、100k 档 ≈19%（噪声带内 + 原测构成未记录）；简历三数字内部换算自洽且"近线性"成立 → 性能声明可信。
- 落库：`scripts/bench_replay/`（gen_samples.py / bench.cpp / README）+ `docs/10_REPLAY_PERFORMANCE_BENCHMARK.md`（口径、样本构成、逐轮原始数据、对照结论、诚实边界）+ INTERVIEW_NOTES 方法论条目 + devlog 本条。
- 顺带修复 PROJECT_STATUS/BACKLOG 的 T015 收盘一致性（面板/§2/§3/changelog 补齐 Part C DONE 状态与 LKGC `ae067ab`；K7 行粘贴损坏清理）。

## 关键链接

- 基准文档：[docs/10_REPLAY_PERFORMANCE_BENCHMARK.md](../10_REPLAY_PERFORMANCE_BENCHMARK.md)
- 复现工具：[scripts/bench_replay/](../../scripts/bench_replay/README.md)
- 状态修复：PROJECT_STATUS、BACKLOG（同步本提交）

## 备忘

- 实测数字是主机相对值、非跨机常数；复现用于同机退化检测。
- 简历原测的当时运行记录已不可追（当时未落库）——本文档从今起是性能声明的事实源。
- 提交：本 maintenance 提交（docs + scripts only；未触碰任何 src/tests/CMake；ctest 无需重跑，构建产物零改动）。