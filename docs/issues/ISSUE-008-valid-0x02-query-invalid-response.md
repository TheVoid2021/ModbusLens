# ISSUE-008: Valid 0x02 Agent query can end in InvalidResponse

- **状态**：OPEN（2026-09-10 发现；RCA 完成；修复与 Live Diagnostic 待授权）
- **发现**：用户在正常 Simulator Demo batch 连续测试约 15 类自然语言问题（其余 14 类正常），其中合法问题——「异常码 0x02 在这个批次里代表什么？我下一步应该优先检查什么？请区分'已知事实'和'建议检查项'。」——最终 UI 显示「模型响应格式无效」。
- **关联**：T012（Post-Closure Stabilization）；定案：**不是 Provider bug、不是 Prompt bug；根因形态 = final round 无可用 content，被正确 fail closed 为 InvalidResponse（具体触发原因未知，见 §2）**。

## 1. InvalidResponse 真实产生路径（代码核验）

**A. ModelScopeAgentClient::handleFinished（HTTP 2xx 后）产生 InvalidResponse 的三处**：

1. `choices` 缺失或非 array（body 非预期 Chat Completions shape）；
2. `choices` 为空数组，或 `choices[0]` 非 object；
3. `choices[0].message` 非 object。
   ——注意：HTTP 非 2xx 走独立 mapping（mapHttpStatus），不产生 InvalidResponse。

**B. AgentRuntime::handleRoundSucceeded 产生 InvalidResponse 的一处**：

4. 助理消息无 tool_calls（或为空数组）**且** `content` 缺失/null/空串/纯空白 → `failProvider(InvalidResponse, "响应中无可用最终回答")` —— 这正是用户可见「模型响应格式无效。」的来源（Controller 映射见 §3）。tool_calls 非空时内容被忽略、工具优先（B21 锁定）。

**C. Controller 映射**：`agentFailureText(AgentRunFailure)`：`providerError=true && providerCode==InvalidResponse` → 「模型响应格式无效。」→ 写入 `agentErrorText` → QML `visible: agentErrorText !== ""`（第 522 行）→ 用户可见。**该链路工作正确**。

## 2. 本案例最可能进入哪条路径（RCA 结论）

- 用户看到了「模型响应格式无效」= **B 路径（4）**：provider 已返回 HTTP 200 且 choices/message 结构合法，但 `message.content` 为空/纯空白且无 tool_calls。
- 为什么该问题的最终轮会空 content：**unknown**。可行假设（标注 hypothesis only / not proven）：模型在最终轮把输出预算消耗于 reasoning_content（`max_tokens=768` 统一适用所有轮，见 ISSUE-008 §4），导致 `content` 为空；或 provider 对这一问题形态返回了消费异常。
- **不得写成事实**：reasoning 耗尽预算、finish_reason=length、模型行为异常——均未经 sanitized 响应证据证明。

## 3. Final-content parser audit（真实实现逐项）

| 情况 | 实际行为 |
| --- | --- |
| choices 缺失/非 array | client InvalidResponse（A1） |
| choices 空 / [0] 非 object | client InvalidResponse（A2） |
| message 缺失/非 object | client InvalidResponse（A3） |
| content missing / null / "" | runtime：tool_calls 空 → InvalidResponse（B4） |
| content 纯空白 | runtime：trimmed().isEmpty() → InvalidResponse（B4） |
| reasoning_content only（content 空） | runtime：**不升级 reasoning**；走 B4 → InvalidResponse |
| tool_calls 非空 | 工具路径优先；content 被忽略（B21 锁定语义） |
| content + tool_calls 同时非空 | 工具路径优先（B21）；content 不提前 Complete |
| finish_reason = stop / length / 任意 | **运行时从不读取 finish_reason**（api 契约扭曲无影响） |
| content 为数字等意外类型 | `.toString()` 会非空（如 "123"）→ 会被当作 final answer —— 极边角缺陷，仅记录，单独测试列入未来矩阵 |

## 4. Output/token-budget audit（事实）

- 所有轮（tool round 与 final round）使用**同一个输出预算** `max_tokens=768`（`ModelScopeAgentClient.cpp` 第 113 行；T011 客户端同为 768）。
- 是否可能"reasoning 消耗输出预算导致 final content 为空"：**hypothesis only / not proven**（本阶段不改 token 上限、不关 thinking、不加 retry）。

## 5. Existing test gap

- B20 已锁定「无 tool_calls + 空内容 → provider InvalidResponse（不 emit 空 runCompleted）」，但只用了 `content=""`，未覆盖纯空白与 reasoning-only。
- **未来回归设计（不重复编号）**：
  - 扩展 B20：`content="   "`（纯空白）→ InvalidResponse；
  - 新增 **AGENT-B24**：`reasoning_content` 非空、`content` 空、无 tool_calls → InvalidResponse（reasoning 永不升级为 answer 的回归锁）；
  - 拿到真实 sanitized evidence 后，把真实响应 shape（定长的 message keys/content 长度/reasoning presence）作为 fake regression fixture 加入 agent_runtime tests。

## 6. 是否需 Live Diagnostic

违反仅靠代码+已有证据无法知道该次 provider 返回的确切 shape（§6=B 情形）。**需要最多 1 次 user-authorized diagnostic reproduction**：同一问题重发并捕获 sanitized evidence（HTTP status / finish_reason / message keys / content type 与长度 / tool_calls presence / reasoning_content presence / usage token 字段）；绝不含 key/Authorization/完整 reasoning_content。**本阶段不执行。**

## 7. 状态

- 修复未开始（Budget 调整/重试集群 在用户批准前一律不实施）。T012 = REOPENED / STABILIZATION；M6 = IN PROGRESS；verified LKGC `e922c19` 不回退。


## 9. Correction（2026-09-10，append-only，不 rewrite 原 RCA）

- 实际 InvalidResponse producer 数量 = **4**：
  A. ModelScopeAgentClient：① choices 缺失/非 array；② choices 空或 choices[0] 非 object；③ choices[0].message 非 object。
  B. AgentRuntime：④ 无 usable tool_calls 且 final content 缺失/null/空/纯 whitespace → InvalidResponse。
  - tool_calls 非空时 content 被忽略、Tool Calling 优先——这是 **precedence rule**，不是第 5 个 InvalidResponse producer。
- 原措辞"本次 0x02 最可能进入路径④"修正为：**本次真实 InvalidResponse 的精确 producer 目前 unknown；可证明范围为上述 ①~④ 之一**。Controller 的中文映射只能证明最终 error class，不能区分 ①~④。


## 10. Live Diagnostic Evidence（2026-09-10，经授权唯一 reproduction）

- 条件：build/debug 生产同基线（仅临时 sanitized 插桩，三文件，Live 后 `git checkout` 完全恢复，src 对 HEAD 零 diff）；与原失败**完全相同的问句**、相同 Simulator Demo batch、仅一次点击。
- **结果：NOT REPRODUCED** —— 同一问题本次成功产出 usable final answer（约 60s；含「已知事实/建议检查项」区分、0x02=Illegal Data Address、transaction_number 2、18ms、25%、独立异常与单点观测 disclaimer）。
- **sanitized per-round metadata（临时观测日志，仅元数据；无正文/无 reasoning 正文/无 key）**：
  - round1：HTTP=200，body=1180B，keys=choices/created/id/model/object/system_fingerprint/usage，choices=1，finish_reason=tool_calls，message keys=content/function_calls/reasoning_content/role/tool_calls，content present/rlen=0/tlen=0，tool_calls=2，reasoning present/194，usage 对象存在。
  - round2：HTTP=200，body=1142B，同 keys，finish_reason=tool_calls，content rlen=0，tool_calls=1，reasoning present/306，usage 对象存在。
  - round3（final）：HTTP=200，body=3023B，finish_reason=stop，content present/rlen=714/tlen=714（usable），tool_calls 空，reasoning present/786，usage 对象存在（completion/prompt/total_tokens）。
- **精确 producer 判定**：本次未触发 InvalidResponse（Client ①/②/③ 与 Runtime ④ 都未被命中）——用户此前真实失败的精确 producer 仍为 **unknown（范围 ①~④）**。
- **Output-budget hypothesis 状态**：未被支持、未被排除。本次 final 轮 reasoning present/786 与 usable content/714 共存，说明"reasoning 存在"本身不必然导致空 content。
- **结论**：成功一次不能否定此前真实失败；ISSUE-008 保持 OPEN，"/等待用户 Review 后走 Implementation（届时 B20 扩展 + AGENT-B24 照计划实施）。


## 11. Correction（2026-09-10 Phase 3，append-only）

- 表述校正：「本次证据使某 producer 更可能」不成立且已被撤回。正确表述：**同一问题在同一 Demo 场景下一次成功完成，因此该历史 failure 不是已证明的 prompt-deterministic / always-reproducible failure；历史 InvalidResponse 的 exact producer 仍为 unknown，范围保持 Client ①~③ 或 Runtime ④。**
- 单位澄清：§10 中 response body 长度 = **bytes**；content/reasoning_content 长度 = 代码实际读取的 QString 字符数（UTF-16 code units 口径，非 tokens）；**只有 usage 对象中的真实 token 字段才称为 tokens**；字符串长度绝不与 max_tokens 比较。


## 12. Phase 3 Hardening（2026-09-10）

- Desired final-answer contract = **AUTOMATED / HARDENED**：B20 扩展 whitespace 全族、新 B24（reasoning-only 不升级）、新 B25（非 string content fails closed）。其中 B20/B24/B25 首次即 PASS——如实记录为 **coverage gap only**（既有契约已正确），未声称 TDD RED。ag22 集成锁：HTTP 200 + 无可用 content/tool_calls → InvalidResponse 必见。
- Historical exact producer 仍 **UNKNOWN**（Client ①~③ / Runtime ④）；Same-scenario rerun 此前已 NOT REPRODUCED。
- **ISSUE-008 保持 OPEN**。
