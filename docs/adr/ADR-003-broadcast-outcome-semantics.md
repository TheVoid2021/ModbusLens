# ADR-003 — Broadcast Outcome Semantics（广播事务的归一化语义）

> 状态：**Accepted（用户 Gate C 批准，2026-09-13）——implemented-candidate**；Final 状态等待 T015 Phase B Review。
> 关联：M8.1 Diagnostic Coverage Audit §18、T015 Phase A Gate C（docs/tasks/T015-passive-replay-expansion.md）

## 背景

- `03_MODBUS_LEARNING.md:38`：地址 0 = 广播；广播只允许写类功能码，无响应。
- 当前六种 `TransactionStatus`（Pending/Success/Exception/CrcError/Timeout/ProtocolError）是围绕“请求期待响应”的 active-master 世界观建立的。广播事务**协议上不期待响应**：NoResponse 不能判 Pending/Timeout（误导为失败），无响应也不能证明“所有从站执行成功”（不能判 Success）。
- T015 Phase A 逐一论证：六状态集合中没有诚实成员；任何诚实表达都要求扩状态轴或改统计口径 ⇒ 属公共数据契约变更 ⇒ 建此 ADR Draft，留待用户裁决（T014 的“六状态不动”红线由本决策显式覆盖，而非被静默突破）。

## 备选方案

### Option A —— 新增第七状态 `ExpectedNoResponse`

- observed/completed 口径：broadcast 行计入 observed 与 completed。
- 统计变体 1（最简）：invariant B 扩为 `completed = success+exception+crcError+timeout+protocolError+expectedNoResponse`；`successRate = success/completed`（不变式 C 不变）——代价：合法广播会**稀释**成功率（T015 §26 明确不可接受）。
- 统计变体 2（隔离口径）：新增计数列 `expectedNoResponseCount`；B′ 同上；**`successRate = success / (completed − expectedNoResponse)`**，当分母 >0，否则 nullopt（C′）——“成功率=已应答事务中成功的比例”，广播不稀释；代价：不变式 C/A 的表述需重写并新增测试。
- UI/Baseline/AI/Agent 均需认识新状态（additive）。

### Option B —— 六状态不动 + 正交 `ResponseExpectation/TransactionKind`

- outcome 增 kind 字段（ExpectedResponse/ExpectedNoResponse）。
- 但 broadcast 行仍需一个 high-level status：六值内无诚实落点；若不落点则 kind=Broadcast 行游离于状态池 = 事实上的两套世界。⇒ 本方案不能独立成立，只能作为 Option A 的补充标注（kind 携带“为何不期待响应”的解释性事实）。

### Option C —— Broadcast 独立 passive outcome，不进六状态统计

- 广播行独立 counter/行样式；若计入 observed/completed 则不变式 B 裂开，若不计入则行计数 ≠ observed（两套世界）。长期漂移风险最高。

## 推荐（待用户裁决）

- 主推荐：**Option A + 变体 2**（第七状态 `ExpectedNoResponse` + 成功率分母=已应答事务），配合 Option B 的 kind 字段作解释性补充（可选）。
- 证据边界措辞（无论采用哪案）：状态语义只表达“未观察到响应，且协议上该广播请求不期待响应”；**绝不**渲染为“写入已成功应用到所有从站”；broadcast 却收到响应 ⇒ `ProtocolError + UnexpectedResponseForBroadcast`（T014 issue 枚举 additive 扩展，Gate C 附注）。

## 后果

- 正面：广播成为一等可诊断事务；成功率不再被合法广播污染；六状态及全套下游可通过“新增而非修改”适配。
- 负面：概率性成本——不变式 A/B/C 家族重写与全层适配（Statistics/Baseline/Prompt/Agent/UI/表格）；任何既有无广播数据集行为零变化（demo_v1/既有金样），但代码切换点增加。
- 关联任务：T015 Phase B（Gate C 批准前不实施）。

## 状态历史

- 2026-09-13 Draft/Proposal 建立（T015 Phase A）。
- 2026-09-13 **用户 Gate C 批准 → Accepted（implemented-candidate）**。批准口径（最终公式）：`observed = pending + completed`；`completed = success + exception + crcError + timeout + protocolError + expectedNoResponse`（**completed 不含 Pending**）；`rateEligibleCompleted = completed − expectedNoResponse`；`successRate = success / rateEligibleCompleted`；`rateEligibleCompleted == 0 ⇒ successRate = nullopt`；`averageSuccessLatencyMs` 仍只计 Success。新增状态 `TransactionStatus::ExpectedNoResponse` 语义：观察到合法 broadcast-capable 请求、未观察到响应、协议不期待响应——不代表 write success / 设备健康。Final 待 T015 Phase B Review。
- 2026-09-13 **T015 Phase B 实现完成（IMPLEMENTED / AWAITING REVIEW）**：七状态、统计公式（STAT-B10 + PASSIVE-P14 锁定）、Baseline `ExpectedNoResponseObserved`（Info、顺序 Pending 之后）+ Healthy 四条件、Agent anomalies 排除该状态、UI「预期无响应」文案与统计卡、Prompt 语义句族（“不证明写入成功”）。clean 152 零警告、ctest 24/24、qml smoke、deploy+minimal-PATH；LKGC candidate `6944fd5`。Final 仍待用户 Review。