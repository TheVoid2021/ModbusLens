# Devlog — 2026-09-06（T008 Part B Learning: Analysis Dashboard + Deterministic Demo 测试设计）

- Demo Batch 定案：四条固定事务（DEMO-1 Success 25ms / DEMO-2 Exception 0x02 18ms / DEMO-3 CrcError 17ms / DEMO-4 Timeout 1000ms），全部真实调用 T005/T006/T007 链路产生，禁止伪造 TransactionStatus。
- Expected statistics 独立复核：observed=4, completed=4, success=1, exception=1, crcError=1, timeout=1, successRate=0.25, avgSuccessLatencyMs=25.0（仅 Success elapsed 进平均）。
- Controller Part B API 定案：`runDemoBatch()` / `clearDemo()` Q_INVOKABLE——首次允许 QML command API；业务编排全在 Controller（QML 不直接调 Simulator/Fault/Analyzer/Statistics）。
- Replace 语义定案：重复 Run rowCount 恒=4（不追加），clearDemo 恢复全 0 + nullopt（复用 summarizeTransactions(空)）。
- 测试矩阵 UI-B01~B06 落库（P0×5/P1×1）；Core Integration Guard 要求 CRC row 必须真实走 CorruptCrc→decode→analyze 链路。
- QML Dashboard 新增：5 个状态计数卡 + Run/Clear 按钮 + transaction delegate 增强（不引入 chart library）。
- 14 题问答与 22 步实施计划落库。
- 本阶段 docs-only，src/、tests/、CMakeLists.txt、scripts/ 未改动，LKGC 维持 `28f38b0`。
- 任务档案：[T008](tasks/T008-qt-quick-qml-analysis-ui.md)