# Devlog 2026-09-09 — T012 Part A: Read-only Tool Layer（Implementation）

## 今日工作

- **T012 Part A Implementation 完成**（真实 RED→GREEN；code/test commit `9921efd`，LKGC **不推进**——candidate 待用户审核）：
  - **Refinement 落档**（不改历史，Implementation Review Refinements R1~R4 追加于任务档案与 ADR002）：R1 `transaction_number`（1-based batch ordinal，不称 ID）；R2 recent=latest-20（原序、total/returned/truncated）；R3 immutable `AgentToolContext` snapshot（dispatcher 只读传入快照）；R4 Live Probe 预算改为最多 2 个真实请求。
  - **`src/ui/agent/`（新目录，App/Adapter 层，不属 protocol Core）**：
    - `AgentToolContext.h`：run 级不可变快照 = `vector<DiagnosisTransaction>` + `TransactionStatisticsSnapshot` + `capturedBatchRevision`。
    - `AgentTools.h/.cpp`：enum 白名单（恰三个工具）+ explicit dispatcher（无 registry）；typed results（SessionSummary/RecentAnomalies/TransactionDetail）+ 最小错误 enum（UnknownTool/InvalidArguments/TransactionNotFound）；模型 arguments 全量校验（精确名映射/required/type/整数/未知字段拒绝/范围→NotFound）；provider-independent JSON DTO（QJsonObject，evidence_scope 定值标注）；0x01~0x04 标准异常名 structured 输出、未知码不猜。
  - **测试**：`tests/test_agent_tools.cpp` AGENT-A01~A09（RED=10 处 undefined reference；GREEN 9 函数全过）——含 latest-20 原序/截断、空 batch optional 缺席（不伪造 0）、0/5/999→NotFound、未知字段/类型错误矩阵、只读白名单（11 个写类名全拒）、字节级确定性重放、snapshot isolation（同一快照跨"外部批切换"仍自洽）。
  - **CMake**：新 target `modbuslens_agent_tools_tests` + `add_test(agent_tools)` + offscreen 属性。
- **自动验证**：clean 全量重建 **131 targets 零警告**；ctest **21/21**（新增 agent_tools）；零公网/零 token/零 quota。
- **T011 零 diff**：client/prompt/QML pipeline 一字未动（verified fallback 保持）。

## 状态

- T012 overall = IN PROGRESS；**Part A = IMPLEMENTED / AWAITING REVIEW**；**Part B = NOT STARTED**（Live Tool-Calling Probe 未执行、未授权）；M6 = IN PROGRESS。
- LKGC 保持 `01841b1`；`9921efd` 仅为自动验证 candidate。

## 关联档案

- 任务：[T012-agent-tools](../../tasks/T012-agent-tools.md)（R1~R4 + Part A Verification 已附）；ADR：[ADR002](../../adr/ADR002-readonly-tool-agent-architecture.md)（refinement 段已附）。