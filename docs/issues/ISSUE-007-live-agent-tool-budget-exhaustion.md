# ISSUE-007: Live Agent tool budget exhaustion on valid multi-step diagnosis

- **状态**：**RESOLVED**（2026-09-10；用户 Final Review = PASS。同题 Live Re-Validation PASS；完整 Live FAIL → RCA → fix → re-validate 历史保留，不改写）
- **发现**：T012 Part B Phase 2 Final Real Live Agent Smoke（Qwen/Qwen3.5-27B，正式 endpoint，唯一授权 run）
- **关联**：T012 Phase 2（candidate `d781ab0`→fix `?`）；ISSUE-006（Attribution discipline 保持不弱化）

## 1. 现象

唯一授权的真实 Agent run 在获得 final answer 之前，UI 显示本地错误「工具调用次数已达上限。」（`ToolCallLimitExceeded`），run 终止。约 12 秒、无崩溃、facts 零变化。

## 2. RCA —— 已证明事实 vs 不可证明事实（严格分离）

**已证明**：
- 真实 run 在 final answer 前触发 `ToolCallLimitExceeded`；
- 当时 hard limits = `MAX_TOOL_ROUNDS=3` / `MAX_TOTAL_TOOL_CALLS=3`；
- 因此 provider 在该 run 中请求的工具调用总数超过了当时 total budget（累计 >3）；
- bounded Runtime 防护按契约工作：整批拒绝、zero partial execution、agentBusy 回 false、deterministic statistics/rows/baseline 未变、无 crash、无 QML 损坏、无 false action。

**不可证明（禁止写成事实）**：
- 具体调用顺序（exact tool-call sequence 在 deployed UI 不可观察；按禁令未加任何调试设施）；例如"模型重复调用 summary"或"一次调用四个 detail"均为推测，不得记载为事实。

**RCA 定案**：一个合法、自然的 multi-step 只读诊断问题（先摘要+异常概览，再针对少量异常事务取详情）暴露出 `MAX_TOTAL_TOOL_CALLS=3` 对该类 bounded read-only diagnosis **过于严格**。这是 **Agent orchestration / planning budget issue**——不是 Modbus Core、deterministic data、provider outage 或 QML 问题。

## 3. 解决方案（经用户批准）

1. **上限调整**：`MAX_TOOL_ROUNDS = 3` 保持（严格限制 provider-loop 深度）；`MAX_TOTAL_TOOL_CALLS = 3 → 6`（允许 aggregates + 少量 targeted details；全部 read-only、单 immutable snapshot、bounded result——无权限扩张）。禁止提高 rounds、取消 limit、无限、自动 retry。
2. **Prompt planning discipline**（只改独立 Agent system instruction，不动 T011）：最小化工具调用、不得重复请求已获得的聚合信息、summary/anomalies 通常各至多一次、detail 只在直接相关时调用、足够事实后立即给 final、运行时预算 3 rounds/6 calls。通用 policy，不硬编码 demo 四事务 / transaction #2 / 0x02 / smoke 问题。
3. **否决 one-tool-per-round**：Phase 1 的 multiple tool calls 是完整支持且已测试的能力；只读 immutable snapshot 上并行/批量调用合法；强制一轮一个会人为增加 round trips 与延迟并废弃已有能力。
4. 既有 authority 规则（deterministic 工具结果权威 / 不得重释 CRC·status·异常码·统计 / evidence_scope / 混合异常独立 / 只读边界 / no false action claims / ISSUE-006 全部约束）**一字未弱化**。

## 4. 自动验证（离线回归，零真实请求）

- `AGENT-B05`（重写新边界）：**6 calls 一个响应 → 全部执行**（tool_call_id 逐一回传）；**7 calls → ToolCallLimitExceeded 且整批零部分执行**（requestCount==1）。
- `AGENT-B23`（新增，Live-failure regression shape）：round1=summary+anomalies（2 calls）→ round2=detail(2)/detail(3)/detail(4)（累计 5 ≤6）→ round3=final。无超限、ids 正确、final 发布、rounds=3 ≤ 上限。
- `AGENT-B04` 保持：第 4 个 tool round → ToolRoundLimitExceeded 不变（round guard 未被 total 调整破坏）。
- 全量：agent_runtime B01~B23、agent_tools A01~A10、agent_integration UI-AG01~AG20、ai_client、ui_bridge、qml_smoke；clean 0 警告；ctest 23/23。
- **RED 证据**：新 B05/B23 在旧上限 3 下运行 → B05 FAIL（6 calls 被拒）、B23 FAIL（累计 5 被拒）——精确命中旧失败形状；改上限+prompt 后 GREEN。

## 5. 复验要求

Issued 状态在**下一次经用户明确授权的 Live Agent Re-Smoke** PASS 后才 RESOLVED。本轮零真实 ModelScope 调用。

## 6. 教训

- Hard budget 必须在"模型可用计划空间"与"资源上界"之间试调：真实模型在多步探查指令下自然使用 ≥4 次只读调用,3 的 total 上界把合法计划排除在外。
- 只读 + immutable snapshot + bounded result 从根本上改变了"提高本地调用上限"的风险面——它不增加写权限与状态面。
- 上线前无法观察 exact sequence 属已知盲区(按设计无工具日志);RCA 只依赖可证事实(budget exceeded + guards intact)完成修复,不靠猜 sequence。


## 7. Live Re-Validation Evidence（2026-09-10，经用户授权，唯一 run）

- 与上次**完全相同**的 scenario 与问题原文（一字未改）；唯一一次「询问 Agent」点击；无 retry、无 Ask AI、无换题/换模型/换 endpoint。
- **结果：PASS**——约 60 秒内得到 usable final answer；**未触发 ToolCallLimitExceeded**；**未触发 ToolRoundLimitExceeded**；agentBusy 最终 false；deterministic statistics/rows/baseline 未变。
- final answer 一致性摘要：本批次 4 笔事务、成功率 25%（模型主动写出的数字均与事实一致）；Exception 0x02 = Illegal Data Address（事务编号 2、功能码 3、18 ms——编号/耗时与 demo 批次真实顺序一致）；CRC = 17 ms 校验失败描述为链路层完整性问题（possible 语气）；Timeout = 1000 ms 判定超时且"可能因为…"式可能原因；三类异常明示为独立问题场景；"当前观察结果仅限本批次样本，不等同于长期系统稳定性表现"；无 false action claim；**未编造任何具体寄存器地址**（仅建议核对地址范围/规格书/映射表）。
- exact tool sequence / exact Provider request count：**not externally observable**（production 无工具日志，按禁令未加设施）；runtime 不可超过 rounds=3 与 4 次 Provider request 的硬界（本轮受控与授权上限一致）。
- F 长答案：本次 answer 明显超出单屏（约 900+ 字），左 pane 内部滚动机制承载（ISSUE-004 结构）；用户人工视觉复核待记录。


## 8. Final Closure（2026-09-10，RESOLVED）

- 历史不改写保留：第一次 Live = 同一合法 multi-step diagnostic scenario 触发 ToolCallLimitExceeded（Live FAIL）；RCA = MAX_TOTAL_TOOL_CALLS=3 对自然 bounded multi-step diagnosis 过严（**guards 本身工作正确，非 Provider bug、非 core issue**）；修复 = rounds=3 不变、total 3→6 + 通用 Tool Efficiency/Planning Discipline；第二次 Live = **完全相同问题原文** → usable final answer、无 ToolCall/ToolRound 超限。
- exact tool sequence / exact request count 与本档案 §1/§7 一致：production 下 not externally observable；本档案不编造 sequence。
- 验证链：离线 B05 新边界 + B23 多步计划（RED→GREEN）+ clean 147 零警告 + ctest 23/23 + 同题真实 Live PASS + 用户 Final Review。
