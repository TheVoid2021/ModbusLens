# Devlog — 2026-09-06（T007 Part B Learning: 统计快照学习与测试设计）

- Part B 定案：`summarizeTransactions(batch) -> TransactionStatisticsSnapshot` 纯函数——三计数（observed/pending/completed）+ 五分类计数 + optional successRate/averageSuccessLatencyMs + 四不变量（A/B 计数恒等式、C/D optional 语义）。
- 语义落库：successRate 分母 = completedCount（Pending 不进分母，例 8/10=80% 而非 8/100）；completed=0 → nullopt（"没有数据"≠"0%"）；B06 与 B05 的关键对比（有 completed 且 0 success 才是 0%）；latency 只统计 Success elapsed（Timeout 的 elapsed 是阈值时刻，混入会虚高）。
- 浮点测试规则：精确值直断言，2/6 用容差比较；Core 返回数学值，"33.33%" 格式归 Presentation。
- 设计取舍：不做 mutable accumulator（reset/rollback/线程安全无消费者），batch→snapshot 对 Replay/QML 随时重算友好；不做 rolling/per-device/per-function/p95。
- 矩阵 STAT-B01~B08（P0×7/P1×1）+ STAT-I01（SimulatedSlave→analyzer 四条真实结果聚合，P0）落库；14 题问答齐备。
- 本阶段 docs-only，src/、tests/、CMakeLists.txt 未改动，LKGC 保持 `14982f6`。
- 任务档案：[T007](tasks/T007-transaction-analysis.md)