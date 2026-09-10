# Devlog 2026-09-10 — T012 Part B Phase 2: Controller + QML Integration（Learning / Integration Plan）

## 今日工作（严格 docs-only）

- **Phase 2 Learning / Integration Plan 完成并归档**（零 src/tests/CMake/QML 改动；零真实 ModelScope）：
  - **Controller 状态图**（真实成员核验）：deterministic batch = `activeDiagnosisTransactions_` + `statistics_` + `transactionModel_` + `activeBatchRevision_`；Baseline = `hasBaselineDiagnosis_` / `baselineDiagnosisText_`；T011 AI = `aiConfigured_ / aiDiagnosisBusy_ / hasAiDiagnosis_ / aiDiagnosisText_ / aiDiagnosisErrorMessage_ / aiRequestGeneration_ / activeAiRequestId_ / requestBatchRevision_ / aiClient_ / aiModelName_`。
  - **批次发布路径全面核验（5 处全部真批变）**：connectSerial 成功清批 / publishSerialResult / runDemoBatch / clearResults / loadReplayFile 成功 —— 每一处经 `invalidateAiForBatchChange()` 做 `++activeBatchRevision_`；failed Replay / failed Serial connect 不触碰批与 revision（原子语义保持）。
  - **AgentRuntime seam 行为实测**：`setCurrentBatchRevision` 仅更新 live seam——busy run 不被立即终止，到下一交付时经 `isStale` 静默回 Idle（T011 备答）。定案 Phase 2 最小 seam：新增 `AgentRuntime::invalidateForBatchChange()`（busy → ++currentAgentGeneration_ + client.cancel(BatchInvalidated) + Idle；零用户可见信号），使"batch 切换 → 立即失效 + UI busy promptly clear"成立。
  - **ST-A 实证**：Phase 1 preflight 位于 supersede 之前——stale start 直接 return：零 HTTP、不 cancel/supersede Run A、不动 generation。Phase 2 以 UI-AG12 集成锁定。
  - **定案清单**（详见 T012 档案 Phase 2 段）：Agent ownership=Controller parents `ModelScopeAgentClient agentClient_` + `AgentRuntime agentRuntime_`；snapshot 唯一合法链（copy → makeAgentToolContext → summarizeTransactions 同源自洽）；revision 同步点=invalidateAiForBatchChange 尾部（++ 后 set seam + invalidate agent）；single-flight = derived `cloudAiBusy = aiDiagnosisBusy_ || agentBusy_`（UI guard + backend guard 双层，不引入第三 bool）；Ask Agent 不绑 Baseline（T011 BaselineRequired 不变）；NoData=本地拒绝零云端请求；answer/error 规则与 T011 同构（accepted 清 error 保 old answer；invalid question 仅覆盖 error；provider error 保 old answer；Cancel 清 busy 不写红 error；batch change 清 answer+error+busy）；generation 由 Controller `agentRequestGeneration_` 单调唯一来源（Runtime 内部 current 同步保留）；cancelAgent 只取消 Agent（UserCancel），Cancel AI 只取消 AI，BatchInvalidated 静默；UI=Diagnosis pane 内「Agent 问答」TextArea(2~4 行中文 placeholder)+询问/取消+PlainText answer（ISSUE-004 布局零推翻、长答案走 pane 内 Flickable）；tool timeline v1 不做；产品定案=Agent busy 时按钮 disabled + backend Busy 拒绝（Runtime supersede 保留为 defensive，Controller 不主动利用）。
  - **UI-AG01~AG18 测试矩阵**定稿（P0×13 / P1×5，覆盖 wiring/snapshot 自洽/revision/success/failure/single-flight 双向/cancel/batch 变更×2/supersede 定案/ST-A/NoData/输入校验/事实零改动/旧 answer 清除/source-error 不误伤/QML smoke）。
  - **能力边界记录**：「哪个寄存器有问题」——当前 detail facts 无 FC03 startAddress/quantity，Agent 只可答 0x02=Illegal Data Address 与事务编号，**不可**回答具体寄存器地址；记 T013/T015 candidate，本阶段不扩数据模型。
  - Manual UI Smoke（A~J 十项）与未来 Live Agent Smoke（1 个 scenario，预算另行授权）范围入档；T013 polish note（模型偶尔吐出 evidence_scope / multiple anomaly types / shared root cause 英文短语）记录。

## 关联档案

- 任务：[T012-agent-tools](../../tasks/T012-agent-tools.md)（Phase 2 Learning / Integration Plan 段）；ADR：[ADR002](../../adr/ADR002-readonly-tool-agent-architecture.md)（Phase 2 integration seam 细化）；状态：[PROJECT_STATUS](../../PROJECT_STATUS.md)。

## 提交

- docs-only：随本 devlog 的 Phase 2 Learning 归档提交（LKGC 保持 `b322cc3`，不推进；未 push）。


## 追加（晚些）— Phase 2 Implementation ✅（`d781ab0`）

- Controller+QML Agent 集成落地（owns client+runtime/同源双配置/全 derived 状态/四级前置/批切换 ordering seam/invalidateForBatchChange 新最小 seam/single-flight 双向/answer-error 语义）；左 pane Agent 问答 UI；UI-AG01~AG20 全绿（RED=未接线 Controller 编译失败，AG11 时序断言修正一次）；ctest 23/23、clean 0 警告、deploy+minimal-PATH PASS。
- **Phase 2 candidate = `d781ab0`（未推进 verified LKGC）**；Manual UI Review（A~J）待用户。


## 追加（Live Smoke）— Final Real Live Agent Smoke = FAIL（2026-09-10）

- 唯一授权 run：Qwen/Qwen3.5-27B + 正式 endpoint；约 12s 内 UI 报「工具调用次数已达上限。」（ToolCallLimitExceeded），无 final answer；无第二 run、无 retry、无 Ask AI；请求数不可直接观测但 Runtime 累计上限保证 ≤3（<4 授权上限）。
- 判定：Live E2E = FAIL（模型工具调用行为超出 v1 TOTAL_TOOL_CALLS=3 上限），防护按契约工作；facts 零变化。候选改进（仅记录待用户决策）：指令约束每轮单工具 / 调上限 / 保持 v1。
- d781ab0 未推进 LKGC（仍 b322cc3）。


## 追加（ISSUE-007）— 修复完成 `e922c19`

- Live FAIL → ISSUE-007 建档（RCA 可证/不可证分离；budget 3→6 + planning discipline；否决 one-per-round）；B05 新边界（6 过/7 拒零执行）+ B23（5 calls 多步计划）RED→GREEN；clean 147 零警告、ctest 23/23。
- **新 Phase 2 candidate = `e922c19`**；verified LKGC 仍 `b322cc3`；Live Re-Smoke 待用户授权。


## 追加（Live Re-Validation PASS）— ISSUE-007 修复被真实证明 ✅

- 经授权唯一 run、同一问题原文：约 60s 产出 usable final answer；零 ToolCall/ToolRound 超限；facts/rows/baseline 不变、无 crash；0x02=Illegal Data Address（编号/耗时对）、CRC/Timeout possible 语气、混合独立、明确"仅限本批次样本"、无 false action、无具体地址编造（逐项验收在 ISSUE-007 §7）。
- exact tool sequence / request count = not externally observable（政策如实）。ISSUE-007 = LIVE RE-VALIDATION PASS / AWAITING USER FINAL CLOSURE；candidate `e922c19` 未推进（LKGC `b322cc3`）。


## 追加（Final Closure）— T012 / M6 DONE ✅

- 用户 Final Review = PASS：ISSUE-007 RESOLVED；Phase 2 DONE；T012 DONE；M6 DONE；verified LKGC 推进至 **`e922c19`**；T013 polish notes 与 register-address limitation 入档，NOT STARTED。
