# ISSUE-009: Provider quota/request failure is not surfaced to the user

- **状态**：OPEN — **Disposition：MONITORING / NON-BLOCKING**（2026-09-10 Final Review；不阻塞 closure；历史 silent breakpoint 未证明，保持监控）
- **发现**：当 ModelScope API 没有可用额度时，点击 Agent → 「分析中...」约 1 秒 → busy 消失 → **没有持久可见错误提示**。用户无法判断是额度不足 / Provider 拒绝 / 网络问题 / 模型问题。
- **关联**：T012（Post-Closure Stabilization）；shared ModelScope provider failure UX（T011 同类检查见 §6）。

## 1. Error-path audit（真实传播链，逐层核验）

`ModelScopeAgentClient`（非 2xx）：mapHttpStatus → 401/403=Unauthorized、429=RateLimited、400/404/422=ProviderRequestError、≥500=ServerError；sanitizeProviderMessage 提取 error.message（无 message 时用兜底文案「模型服务错误（无可用错误信息）」）→ `roundFailed(gen, code, msg)`。
`AgentRuntime::handleRoundFailed`：stale 则静默；否则 `failProvider(code, msg)` → `runFailed(gen, AgentRunFailure{providerError=true,…})`。
`AnalysisController::handleAgentFailed`：`agentErrorText_ = agentFailureText(failure)` → `agentStateChanged`。
`QML`：Agent error label `visible: agentErrorText !== ""`（红色，位于左 pane Flickable 内）。

**各类终态现状**：

| code | emit | 映射文案（Agent 路径） | 写入 agentErrorText | QML 可见 | 何时被清 |
| --- | --- | --- | --- | --- | --- |
| NotConfigured | 客户端 preflight fail | 「ModelScope 未配置。」 | ✓ | ✓ | 下一次 accepted run |
| NetworkError | ✓ | 「网络错误，Agent 请求失败。」 | ✓ | ✓ | 同上 |
| Timeout | ✓ | 「AI 请求超时。」 | ✓ | ✓ | 同上 |
| Unauthorized | ✓ | 「ModelScope 未授权（Unauthorized）。」 | ✓ | ✓ | 同上 |
| RateLimited | ✓ | 「ModelScope 请求受限（RateLimited）。」 | ✓ | ✓ | 同上 |
| ProviderRequestError | ✓ | 「模型服务请求错误。」 | ✓ | ✓ | 同上 |
| ServerError | ✓ | 「模型服务端错误。」 | ✓ | ✓ | 同上 |
| InvalidResponse | ✓ | 「模型响应格式无效。」 | ✓ | ✓ | 同上 |

## 2. Silent UX 的精确断点（RCA）

- 在**既有代码路径**里，任何 provider/network failure 都应产生可见 agentErrorText —— 没有任何一条路径会"busy 消失且不写错误"。因此用户观察到的静默无法由已核验代码解释。
- **最可能断点（hypothesis，未经响应证据证明）**：额度不足场景 provider 返回 `HTTP 200` + 非标准/异常 body，且形态不在 audit 表内（例如 content 为错误说明文本被当 final answer 展示、或 body 结构与 A1~A3 之外的形状）。**精确断点 = unknown**，需一次 user-authorized sanitized Live Diagnostic（最多 1 次）捕捉：HTTP status / finish_reason / message keys / content 类型与长度 / tool_calls / reasoning / usage 字段（绝不含 key/Authorization/完整 reasoning_content）。
- **已证**：`configured != healthy`、`configured != quota available` —— 顶部「模型服务：ModelScope — 模型：%1」只表示 config 存在（API key + endpoint + model 齐全），不代表 Provider 当前健康或有额度。

## 3. Quota vs Rate Limit

- 当前代码**无法区分** quota exhausted 与 rate limited（429 一律 RateLimited；无 QuotaExceeded 枚举）。
- 项目历史真实证据（T011 Live attempt#1 insufficient balance）**未保存 response shape**——无法凭现有证据建立稳定 machine-readable 映射。
- **定案**：不新增 QuotaExceeded enum；采用合并 UX 文案（§4），待未来真实响应可可靠区分后再考虑专门映射。

## 4. Provider UX Final Design（文案建议，遵 06 语言政策）

- Unauthorized → 「ModelScope API Key 无效或没有访问权限。」
- RateLimited / quota-like → 「ModelScope 请求受限或额度不足，请检查账户状态后重试。」
- Timeout → 「模型请求超时，请稍后重试。」
- NetworkError → 「无法连接到 ModelScope，请检查网络连接。」
- ServerError → 「ModelScope 服务暂时不可用，请稍后重试。」
- InvalidResponse → 「模型返回了无法处理的响应格式。」
- Cancel / BatchInvalidated：继续静默（语义不变）。
- 所有真实 provider/network/auth/rate/error 必须**持久可见**——绝不允许"分析中..."后无下文。

## 5. 顶部「已配置」真实语义与方案

- 现文案：「模型服务：ModelScope — 模型：Qwen/Qwen3.5-27B」（configured）/「模型服务：ModelScope — 未配置」。真实语义 = **config 存在**（configured），不是 healthy/quota/网络可用。
- **推荐方案（最小变更）**：方案 B —— 将前缀改为「模型配置：ModelScope — …」，避免"服务"二字被误读为 Provider 运行状态；不添加主动 quota polling（无谓的云端轮询违背最小配额纪律）；ISSUE-009 的持久错误提示由 §4 达成。方案 A（保持现状+文档澄清）作为备选已记录。

## 6. T011 同类检查

- Ask AI 的 provider failure 走 `setAiError`（前缀「最近一次 AI 请求失败：…」）→ `aiDiagnosisErrorMessage` label 可见（红色、不含 blank 条件）——**T011 无静默问题**，错误会持久显示直至新 Ask 或 batch 变更。
- ISSUE-009 scope 允许覆盖 shared ModelScope provider failure UX（两端共用一致中文语义）；**不得改变 T011 deterministic authority semantics 与其 Validation 规则**。

## 7. 未来 test design（Implementation 时）

- UI-AG21：fake 429（含 error.message）→ agentErrorText == 「ModelScope 请求受限或额度不足，请检查账户状态后重试。」（持久可见、非静默）。
- UI-AG22：sanitized 真实 quota shape 若被 Live Diagnostic 捕捉 → 作为 fake enqueue fixture 回归，断言用户可见错误。
- 回归：B13（cancel 静默）与 UI-AG09/AG10（batch invalidation 静默）不得因新文案误伤。

## 8. 状态

- 修复未开始；诊断不执行（待授权）。T012 = REOPENED / STABILIZATION；M6 = IN PROGRESS；verified LKGC `e922c19` 不回退。


## 9. Live Diagnostic Evidence（2026-09-10）

- **PRECONDITION NOT AVAILABLE / BLOCKED**：本阶段 ISSUE-008 的授权 run 成功完成（3 个真实 Provider 请求全部通过并产出 usable answer）——这直接证明**当前账户额度可用**；"无可用额度的真实状态"在本刻不可获得，且按规则不得伪造 quota 响应冒充 Live evidence、不得用其它错误替代。因此 ISSUE-009 本次未发出额外请求。
- 结果：silent-UX 的精确断点仍为 **unknown**（Case A~E 均未排除）；等待未来真实 quota 状态出现时，再以 1 次授权 run 做 sanitized 诊断，或在 Implementation 阶段先落地§4 文案与 UI-AG21（无论断点如何,此刻 make provider failure persistently visible 的 UX 修复本身不依赖断点证据）。


## 10. Correction（2026-09-10 Phase 3，append-only）

- 明确：**Live quota reproduction count = 0；precondition = unavailable**（ISSUE-008 的成功 run 证明当时额度可用；按纪律未伪造、未替代）。任何"已复现 quota failure"的表述无效。


## 11. Phase 3 Hardening（2026-09-10）

- Known provider-failure visibility contract = **HARDENED / AUTOMATED PASS**：8 类 provider 错误全部非空、持久可见中文文案（§5 六条 + NotConfigured/ProviderRequestError）已落地；ag21 锁定“429 → RateLimited → runFailed → busy false + agentErrorText 持久保留、仅 accepted run 才清”；顶部改「模型配置：」。
- Historical quota silent failure exact breakpoint = **NOT REPRODUCED / UNKNOWN**；Live quota reproduction count = 0（precondition unavailable）。**本阶段不声称根因已修复**。
- **ISSUE-009 保持 OPEN**（等待未来真实 quota 状态自然出现时验证）。


## 12. Enum Terminology Audit（2026-09-10 Final Review）

同一 AiDiagnosisErrorCode 实际承担两类语义，文档必须区分（不重构 enum）：
- **A. provider/request-path failures**（对用户必须可见）：NetworkError / Timeout / Unauthorized / RateLimited / ProviderRequestError / ServerError / InvalidResponse。
- **B. local configuration / precondition / runtime states**：NotConfigured（配置预检）、InvalidConfiguration、NoData、BaselineRequired（T011 专用前置）、Busy（本地单飞）。这些并非"Provider 失败"，不得统称。

## 13. Final Disposition（2026-09-10，MONITORING / NON-BLOCKING）

- Known provider failure visibility = **HARDENED / AUTOMATED PASS**（ag21 429 全链路+lifetime；ag22 malformed-200 可见；8 条中文文案；顶部「模型配置：」）。
- 429 integration = PASS；Malformed HTTP-200 visible failure = PASS。
- Historical quota reproduction count = **0**；Historical silent breakpoint = **UNKNOWN**。
- Future policy：真实账户**自然出现** quota/request-limited 状态时，可作为 Live re-validation opportunity；**不得人为耗尽额度**。
