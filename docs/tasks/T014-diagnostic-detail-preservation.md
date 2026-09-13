# T014 — Diagnostic Detail Preservation（诊断细节保留）

- **Goal**          把 Transaction Analyzer 在**判定过程中已经确定性知道**的 protocol/transaction reason（而非导致的物理原因）结构化保存下来，一路传递到 DiagnosisContext / Baseline / AI Prompt / Agent Tools /（可选）UI——修复 M8.1 审计 §13.1 确认的 “ProtocolError Detail Loss”，而不改变六状态、统计口径或任何现有结论。
- **Background**    M8.1 Diagnostic Coverage Audit（`dab9b5f` + `5f21911`，用户 Final Review PASS）确认：六种 `TransactionStatus` 作为 high-level normalized outcome 是合理设计；但 `analyzeFunction03Transaction` 的 5 个真实 ProtocolError 分支 + 2 个防御路径在返回 `ProtocolError` 时把 “原因” 全部丢弃，导致 Baseline/AI/Agent 只能说 “ProtocolError”，无法说出它已知的 AddressMismatch / QuantityMismatch 等事实。T014 = 只保存确定性事实，不猜物理根因。
- **状态**           **T014 = IN PROGRESS**。**Phase A（Learning + Test Design）= 本档案，docs-only，零代码改动。**
- **Phase 状态**     Phase A = DONE / AWAITING REVIEW；Phase B（Test First + Implementation）= WAITING FOR USER APPROVAL。

## 1. Evidence Inspection（证据清单）

- 文档：AGENTS.md、README.md、docs/02_ARCHITECTURE.md、04_TEST_STRATEGY.md、07_FINAL_PROJECT_REVIEW.md、08_KNOWLEDGE_OWNERSHIP.md、09_DIAGNOSTIC_COVERAGE_AUDIT.md、PROJECT_STATUS、BACKLOG、INTERVIEW_NOTES；任务档案 T004/T007/T009/T010/T011/T012/T013。
- 真实代码（全部逐行核验，非摘要推断）：
  - `src/core/analysis/TransactionAnalysis.{h,cpp}` —— 六状态、`ResponseObservation` variant、makeAnalysis 漏斗、全部返回分支（**本次设计的直接对象**）。
  - `src/core/protocol/ModbusRtuCodec.{h,cpp}`、`Function03.{h,cpp}` —— `RtuDecodeErrorCode={FrameTooShort, CrcMismatch}`（恰两值）；`Function03DecodeErrorCode={WrongFunctionCode, InvalidRequestLength, InvalidQuantity, InvalidByteCount, InvalidExceptionLength}`；quantity 1..125 常量。
  - `src/core/replay/ReplayAnalysis.cpp`、`src/core/serial/SerialTransactionSession.cpp` —— 两条生产路径都经同一个 `analyzeFunction03Transaction`（一致性由结构保证；session 侧 FrameTooShort/地址不符/0x84/byteCount 异常等场景已存在测试）。
  - `src/core/analysis/TransactionStatistics.h` —— snapshot 七计数 + 四不变量（**T014 不改**）。
  - `src/core/diagnosis/DiagnosisContext.h`、`RuleBasedDiagnosis.{h,cpp}` —— context 直接持有 `DiagnosisTransaction`（含 analysis）；Baseline 只按 status/exceptionCode 出 finding。
  - `src/ui/ai/DiagnosisPromptBuilder.{h,cpp}` —— `transactionLine()` 是每事务事实行的唯一出口（device/function/status/elapsed/exception_code；bounded 20、失败优先原序）。
  - `src/ui/agent/AgentTools.{h,cpp}`、`AgentToolContext.h` —— `TransactionDetailResult`/`AnomalyEntry` 是 Agent 事实出口；三工具白名单不变。
  - `src/ui/AnalysisController.h`、`TransactionListModel.h`；`tests/` 全量扫描：`TransactionAnalysis{` 聚合初始化站点 = 7 个测试文件 + Core 内部漏斗（见 §26 回归清单）。
- 测试现状（ProtocolError 相关断言）：`test_transaction_analysis.cpp` a06/a07/a08/a09/a10/a11 目前只断言 `status==ProtocolError`（a10 另断言无 exceptionCode）——没有任何 reason 断言；`test_replay_analysis.cpp` i05_protocolError、`test_serial_session.cpp` a07/a11/a15/a16 同理。

## 2. Six-Status Confirmation（从最终代码重新核验）

`TransactionAnalysis.h:14-21`：`enum class TransactionStatus { Pending, Success, Exception, CrcError, Timeout, ProtocolError }`，恰六值、语义与历史档案一致。**T014 红线：此枚举一字不改**（§9 理由）。

## 3. Every Current TransactionStatus Production Branch（逐分支重建，来自 `TransactionAnalysis.cpp`）

| # | 输入 observation | 检查条件（代码位置） | 最终 status | 当时 Analyzer 已知的 deterministic 事实 | 现保存 | 现丢弃 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | `NoResponse` | `elapsed < threshold`（:31） | Pending | 无响应字节 + elapsed | status+elapsed | 无重要丢弃 |
| 2 | `NoResponse` | `elapsed >= threshold`（:34） | Timeout | 阈值内未观察到任何字节 | status+elapsed | 无（Timeout 语义=NoResponse+阈值，已在状态命名内） |
| 3 | `RtuDecodeError{CrcMismatch}` | switch :43 | CrcError | 收到 wire 但 CRC 校验失败（**RtuDecodeErrorCode 恰两值——CrcError 只有这一个确定性来源**） | status | 无（单原因，状态已命名） |
| 4 | `RtuDecodeError{FrameTooShort}` | switch :45 | ProtocolError | 响应 wire < 4 字节 | status | **reason=FrameTooShort** |
| 5 | `ModbusRtuFrame` | `response.address != request.address`（:53-56） | ProtocolError | 请求地址 vs 响应地址两字节实测不符 | status | **expected/actual 地址** |
| 6 | `ModbusRtuFrame`（fc=0x83） | 异常响应解码失败（:60-64；唯一可达失败=`InvalidExceptionLength`，data≠1 字节） | ProtocolError | 0x83 但形状非法 | status | **reason=MalformedExceptionResponse** |
| 7 | `ModbusRtuFrame`（fc=0x03） | 防御性 request 重解码失败（:75-82，契约上不可达） | ProtocolError | （请求违反契约——不该发生） | status | **无可言事实 → 防御 sentinel** |
| 8 | `ModbusRtuFrame`（fc=0x03） | response 解码失败（:77-82；唯一可达失败=`InvalidByteCount`） | ProtocolError | 0x03、形状/长度非法（byteCount 不符） | status | **reason=MalformedNormalResponse** |
| 9 | `ModbusRtuFrame`（fc=0x03） | `request.quantity != values.size()`（:88-91） | ProtocolError | 期望数量 vs 实际值数（**跨帧事实，非单帧 wire 错误**） | status | **expected/actual quantity** |
| 10 | `ModbusRtuFrame`（任何其他 fc） | 尾部兜底（:95-97，含 0x84 等异常形状非 0x83） | ProtocolError | 实际 response 功能码；期望 ∈ {0x03, 0x83} | status | **actual functionCode** |
| 11 | `RtuDecodeError` 未来第三值/switch 漏更新时的类型级兜底 | switch 后 return（:47，-Wswitch 哨兵路径） | ProtocolError | 无 | status | **无 → Unknown sentinel** |

> 说明：分支 5–10 为 production 可达；分支 7、11 为防御路径（7=请求契约被违反，11=未来 RtuDecodeErrorCode 增值时为编译器哨兵）。Branch 4 的 wire 事实来自已存在的 `RtuDecodeErrorCode::FrameTooShort`（decode 层已经命名）。**本表是 §4 设计矩阵的唯一事实来源，未依据任何旧文档猜测。**

## 4. ProtocolError Detail Loss Matrix（核心矩阵）

| 当前高层结果 | Analyzer 时刻已确定的 reason（全部来自代码证据） | 现存储 detail | 丢失的事实 | 是否值得保存 |
| --- | --- | --- | --- | --- |
| ProtocolError | ResponseFrameTooShort（分支 4 / :44-46） | 无 | “收到了不足最小帧长的字节”这一 decode 层已命名的事实 | ✅ 已命名、稳定、跨层有用 |
| ProtocolError | ResponseAddressMismatch（分支 5 / :53-56） | 无 | expected address=request.address；actual address=response.address | ✅ expected/actual 均可瞬时取得 |
| ProtocolError | UnexpectedResponseFunction（分支 10 / :95-97） | 无 | actual functionCode（期望 0x03/0x83 可推导：request.fc=0x03） | ✅ actual 值；expected 免存 |
| ProtocolError | MalformedExceptionResponse（分支 6 / :60-64） | 无 | “0x83 但 data≠1 字节” | ✅ 仅 code；低层 InvalidExceptionLength 不外泄（§15） |
| ProtocolError | MalformedNormalResponse（分支 8 / :77-82） | 无 | “0x03 但 byteCount/长度非法” | ✅ 仅 code；低层 InvalidByteCount 不外泄（§15） |
| ProtocolError | QuantityMismatch（分支 9 / :88-91） | 无 | expected=request.quantity；actual=values.size()（**两者下游当前全都不可见**） | ✅ 两者都必须保存——否则该事实永久消失 |
| ProtocolError | UnknownProtocolError（防御分支 7 / :75-82 与 11 / :47） | 无 | 无（防御路径本无确定性事实） | ⚠️ 需要 sentinel 而非 nullopt（§8 决策） |

M8.1 提出的 6 个候选（FrameTooShort/AddressMismatch/UnexpectedFunction/MalformedException/MalformedNormal/QuantityMismatch）经代码核验**全部真实存在**，另确认 **1 个防御 sentinel**（Unknown）。不硬凑数量：上述 7 值即最终代码的完整枚举。

## 5. “Reason” vs “Root Cause”（T014 的保存边界）

- **保存**：deterministic protocol/transaction reason —— “response address != request address”“数量 3 ≠ 请求 2”“响应功能码 0x04”等**被观测到的事实**。
- **不保存**：“从站地址配置错了”“PLC 程序配置错误”“线路被 EMI 干扰”“DE/RE 切换过早”——这些是 possible cause / 物理叙事，无法由当前 observation 唯一证明（M8.1 §27 Evidence Classification 纪律的延续；T011 “不宣称 root cause” 原则在事实链上的夯实）。
- TransactionIssue/Detail 的定义因此是：**what was deterministically observed**，不是 why the physical system caused it。

## 6. Minimal Data Model Decision（候选比较 + 最终推荐）

| 候选 | 形态 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- | --- | --- |
| A | `std::optional<TransactionIssueCode>`（裸枚举） | 最小 | **expected/actual 值永久丢失**（尤其 QuantityMismatch：期望数量 downstream 无处可查） | 不采用（信息仍丢） |
| B | `std::variant<ResponseAddressMismatch{...}, ...>`（per-reason 结构体） | 类型安全最强、payload 逐类型 | 7 个变体 + 7 组 payload；每个消费层都要 visit；未来加 reason 是 breaking；对 6~7 个 code 属过度设计 | 不采用（复杂度不成比例） |
| C | `std::optional<ProtocolErrorReason>`（窄枚举） | 结构最简 | 名字绑死 “Protocol” 域：T015 的 request-side issue / 未来 timing issue 要么塞不进去要么改名迁移 | 概念对、名字窄 |
| A′（**推荐**） | `std::optional<TransactionIssue>`，其中 `TransactionIssue { TransactionIssueCode code; optional expected/actual 数值载荷 }` | 一档增量换整域可扩展；nullopt=“无 issue”不扰动六状态；`==` 默认可用；加 code 或加 optional 列都是 add-on | payload 需 per-code invariant 表约束（§8） | **采用** |

**最终推荐 = A′**：`TransactionAnalysis` 增加**末尾成员** `std::optional<TransactionIssue> issue{};`（默认 nullopt）。

- `TransactionIssueCode`（当前 7 值，即 §4 的 reason 枚举）：`UnknownProtocolError, ResponseFrameTooShort, ResponseAddressMismatch, UnexpectedResponseFunction, MalformedExceptionResponse, MalformedNormalResponse, QuantityMismatch`。
- payload 字段（皆为 optional）：`expectedAddress, actualAddress, actualFunctionCode, expectedQuantity, actualQuantity`（`std::uint8_t/uint16_t`，Pure C++ protocol facts，**绝无 QString**）。
- 该枚举命名 `TransactionIssue`（而非 `ProtocolErrorReason`）：T015 未来 request-side issue 可挂同一域；**但 Phase A 只定义上述 7 值，不预建 ontology**（“不要为未来所有可能故障现在建立巨大通用错误本体”）。
- 回落考虑过的备选：在 DiagnosisContext 旁挂 index-keyed 侧信道 map —— **否决**：列表复制/重排会断裂身份；三种模式的消费者都要各自传 map；issue 属于 analysis 值本身，必须随值走。

## 7. Append-Last + 默认值（编译兼容性决策）

- 新成员放在 `TransactionAnalysis` **结构体末尾**且带默认初始化：现有 7 个测试文件的聚合初始化（`TransactionAnalysis{` 站点）与 `makeAnalysis` 漏斗调用**全部无需改动即可编译**；`operator==` 默认比较会把“旧期望（issue 缺省 nullopt）”与“新结果”正确比较——只有 ProtocolError 测试的期望需要补 issue。
- 四个不变量（§8）经 `makeAnalysis` 唯一漏斗保证（T007 既有风格：单一漏斗，每条 return 路径过同一点）。

## 8. Core Invariants（新增，逐步锁定）

| # | 不变量 | 说明 |
| --- | --- | --- |
| I1 | `status ∈ {Pending, Success, Exception, CrcError, Timeout}` ⇒ `issue == nullopt` | 非 ProtocolError 一律无 issue |
| I2 | `status == ProtocolError` ⇒ `issue.has_value()` | **不允许 nullopt**：defensive 路径用 `UnknownProtocolError` sentinel（分支 7/11 的诚实表达；下游 switch 无 null 歧义） |
| I3 | `status == Exception` ⇒ `exceptionCode.has_value()`（既有）；issue 一律 absent | Exception 的 detail 仍只有 exceptionCode（§13 决策） |
| I4 | `status == CrcError` ⇒ issue absent | CrcError 当前只有 CrcMismatch 一个确定性来源（RtuDecodeErrorCode 恰两值），状态名已承载原因，**不重复保存**；若未来 RtuDecodeErrorCode 增值，届时再议（-Wswitch 会提醒） |
| I5 | per-code payload：`ResponseAddressMismatch ⇒ expectedAddress∧actualAddress`；`UnexpectedResponseFunction ⇒ actualFunctionCode 且无其他载荷`；`QuantityMismatch ⇒ expectedQuantity∧actualQuantity`；其余四 code ⇒ 全部载荷 absent | 载荷字段只在该 code 语义需要时出现（§12–§16 逐条决策） |
| I6 | `elapsed` 始终保留（既有）；determinism：相同输入 → 相同 `TransactionAnalysis`（含 issue 与载荷） | 幂等纯函数回归 |

## 9. Preserve Six High-level Statuses（红线及理由）

- 不拆分 `ProtocolError` 为顶层 `AddressError/FunctionError/QuantityError...`。理由：Statistics 七计数与四不变量、Dashboard 六状态文案与卡片、Diagnosis 固定 finding 顺序、Agent anomaly whitelist（{Exception/CrcError/Timeout/ProtocolError}）全部围绕六态构建；拆分会连锁改动所有 exhaustive switch 与金样数字。
- **T014 = 增加 orthogonal diagnostic detail**，不是替换归一化轴。

## 10. Statistics Boundary（回归不变量）

- `summarizeTransactions` 只按 `TransactionStatus` 聚合——**一行不改**。AddressMismatch/FunctionMismatch/QuantityMismatch 全部继续计入 `protocolErrorCount`。
- 不新增 dashboard counter；observed/completed/pending/successRate/averageSuccessLatency 口径不变。
- **回归不变量 R-STAT**：T014 完成后，同一 batch 的 `TransactionStatisticsSnapshot` 与 T014 前**逐字段相等**（含同 batch 内多个不同 reason 的 ProtocolError 记录）。

## 11. Request-side Errors Boundary（T014 ↔ T015 分离）

- M8.1 S6（quantity=126 的 invalid request + 设备合法 Exception）：属于 **T015 Passive Replay Expansion**——“拓宽哪些历史流量可以成为可诊断事务”，涉及 Replay trust contract 变更。**T014 不实现、不改变 trust contract。**
- T014 = 现有**已被接受**的事务分析路径内的确定性细节保存。
- Phase A 兼容性讨论：`TransactionIssueCode` 将来可容纳 request-side issue（因此命名为 TransactionIssue），但那只是命名余量，现在不定义那些值。

## 12. Wire Decode Detail Boundary

- `RtuDecodeErrorCode` 已核验恰两值：`FrameTooShort` / `CrcMismatch`（`ModbusRtuCodec.h:12-15`）。
- 决策：`FrameTooShort` → 保存为 `TransactionIssueCode::ResponseFrameTooShort`（**reason 来自已有 deterministic decode result，不新发明**）；`CrcMismatch` → 已是顶级状态 `CrcError`，不重复挂 issue（I4）。
- **禁止**建立 `BitFlip / EMI / MissingFirstByte` 等无法由当前 observation 唯一证明的 detail（M8.1 §27 纪律）。

## 13. Address Mismatch Detail

保留 **`expectedAddress` + `actualAddress` 都保存**（各 1 字节）。比较：只存 actual 也能在 UI 端与行内 deviceAddress 配对，但 (a) TransactionAnalysis 作为独立值不保留配对上下文，(b) unit test 期望值断言需要自包含 KAT 语义，(c) 成本仅 1 字节。结论：两值都存。命名明确是 protocol fact（数值），不做映射文本、“从站地址配错”等措辞。

## 14. Function Mismatch Detail

FC03 请求下：合法正常响应 = 0x03、合法异常响应 = 0x83、其他（0x04/0x06/0x84/…）→ `UnexpectedResponseFunction`。**只保存 `actualFunctionCode`**；期望值集合 `{request.fc, request.fc|0x80}` 可由请求推导，不重复保存（保持最小，不把全套 Modbus function knowledge 塞进 T014）。0x84 等“异常形状非 0x83”与普通 0x04 同归本 code（分支 10 是同一个 return）。

## 15. Response Shape / Semantic Detail

`Function03DecodeErrorCode` 五值已核验。事务层决策：**不直接泄漏低层枚举**，转换为稳定的事务层 code：

- 0x83 + data≠1（`InvalidExceptionLength`）→ `MalformedExceptionResponse`（仅 code，无 payload）。
- 0x03 + byteCount/长度非法（`InvalidByteCount`）→ `MalformedNormalResponse`（仅 code，无 payload）。
- `WrongFunctionCode` 在 analyzer 的两个调用点均因外层 fc 已检查而不可达；`InvalidRequestLength/InvalidQuantity` 在 response 解码中不出现——它们属于待 T015 处理的 request-side 事实。

理由：低层枚举含编解码器视角的冗余值（WrongFunctionCode 在与事务层网格交叉处语义重复），且其负载未来变化不应动摇跨层契约；事务层 code 面向消费者（Baseline/AI/Agent/UI）。

## 16. Payload Fields Retained / Not Retained（汇总）

| detail | 载荷保留 | 载荷不保留（理由） |
| --- | --- | --- |
| ResponseAddressMismatch | expectedAddress + actualAddress（各 1B） | — |
| UnexpectedResponseFunction | actualFunctionCode（1B） | 期望集合可从请求推导 |
| QuantityMismatch | expectedQuantity + actualQuantity（各 2B） | — |
| ResponseFrameTooShort | — | observedByteCount：v1 无消费者（T007 “错误分类要有消费者”纪律）；未来可加 optional 列 |
| MalformedException/NormalResponse | — | 低层 `Function03DecodeErrorCode` 不稳定契约不外泄 |
| UnknownProtocolError | — | 防御路径本无事实 |

## 17. DiagnosisContext Propagation

核验：`DiagnosisContext.transactions` 是 `vector<DiagnosisTransaction>`，而 `DiagnosisTransaction` 按值持有 `TransactionAnalysis`（`DiagnosisContext.h:17-23`）→ **新字段随值自动到达 context，无需任何新结构**。红线：**Baseline 不重新推断 issue**——issue 只在 Analyzer 产生，之后所有层只读取（Analyzer → structured detail → DiagnosisContext → Baseline/Prompt/Agent 单向流动）。

## 18. Baseline Diagnosis Design（Phase A 只设计）

- 现行为：ProtocolError → 单条 `ProtocolErrorObserved(Error)` + `{InspectProtocolConsistency, CheckDeviceDocumentation}`（`RuleBasedDiagnosis.cpp:46-55`）。
- 方案 A（**推荐**）：finding 与 action **保持不动**；新增纯函数 `summarizeProtocolIssues(context) → map<TransactionIssueCode, size_t>`（Core、Zero Qt）仅做 reason **计数** breakdown（如 `response_address_mismatch=2, quantity_mismatch=1`），供 Prompt/Agent/UI 读取。**零新增 finding code、零因果排序、零 root-cause 措辞。**
- 方案 B（否决）：按 reason 拆 sub-findings —— 增加 finding 种类、扰乱固定呈现顺序、向“按原因给建议”的专家系统滑移，复杂化不成比例。
- 理由：计数是纯确定性推导且不改任何既有 finding/顺序/金样（R-BASE 回归不变量）。

## 19. AI Prompt Propagation

- 现状：`transactionLine()` 是事实行唯一出口（`DiagnosisPromptBuilder.cpp:59-74`），bounded 20、失败优先原序；system instructions 已有 ISSUE-006 attribution discipline。
- 设计：ProtocolError 事务行追加 machine-channel 字段 `issue=<code>` + 存在时 `expected_address/actual_address`、`actual_function_code`、`expected_quantity/actual_quantity`（确定性数值，非文案、非推测）。`kMaxDetailTransactions=20` 与选样算法不变。
- system 增加该事实族的确定性语义句族（与 ISSUE-006 同风格），例如：`response_address_mismatch` 只表示“响应地址字节与请求不符”，**不等于**“从站地址配置错误”；AI 可以解释“收到的响应地址与请求地址不一致”，禁止断言“主站/从站地址设置错了”，除非另有证据。
- 禁止进 Prompt：QML 文本、presentation label、任何 speculative cause。

## 20. Agent Tool Propagation

- `get_transaction_detail`（推荐承载完整 detail）：`TransactionDetailResult` 增补 `std::optional<TransactionIssueCode>` + payload optional 字段；JSON 只输出存在的字段（hasX 语义，与既有 exception_code 风格一致）；新增 `transactionIssueName()`（与 `standardExceptionName` 同族，machine/stable 名）。
- `get_recent_anomalies`：`AnomalyEntry` 增补简化 `issue_code`（仅 code，不带 payload），保持 latest-20 原始顺序与 whitelist 语义不变。
- **不新增 Agent Tool；tool 数量继续 3；read-only boundary 不变。** contract 不提供任何写能力（与 T012 一致）。

## 21. UI Presentation Boundary（只设计，不改 QML）

候选比较：A. 新增“详情”大列（否决——当前 5 列稳定表格/比例列宽是 T013 人工验收成果，加列即回归风险）；B. 仅 ProtocolError 行显示 reason（**推荐基础**）；C. 点击行展开（交互成本高，不做）；D. tooltip/secondary text（可与 B 组合，P2 可选）。
- **最小推荐 = B**：`TransactionListModel` 新增 role(s)（如 `IssuedDetailTextRole`，由 C++ adapter 预组确定性中文文案：“响应地址不匹配（请求 0x01 / 响应 0x02）”；仅有 ProtocolError 行非空），渲染为行内 secondary text，不加列。
- M8.1 的“设备→设备地址 / 状态→事务结果”术语修复**不并入 T014**：若 Phase B 成本极小可顺手改 label，否则留独立 polish（不把它与 T014 混成大 UI redesign）。

## 22. Simulator / Replay / Serial Consistency

- 结构性保证：三条生产路径（Demo 批次、`analyzeReplayLog`、`SerialTransactionSession::analyzeAndReset`）**全部经同一个 `analyzeFunction03Transaction`** → issue 一次性产生、三模式自动同口径。**禁止**任何模式私有复制 reason 判定（AGENTS.md 纪律 10 的延伸）。
- 集成断言（§25）：同一 wire 经 Replay 与 Serial session 产出的 `TransactionAnalysis`（含 issue 与 payload）严格相等。

## 23. Backward Compatibility（设计回归原则）

| 面 | 承诺 |
| --- | --- |
| demo_v1 golden 四 outcome（Success/Exception/CrcError/Timeout） | high-level status **完全不变**；四笔全无 issue |
| Statistics | 逐字段相等（R-STAT） |
| Baseline | 既有 finding/顺序/action 不变（R-BASE） |
| AI Prompt | 无 ProtocolError 的批次（含 golden）prompt 字节不变；有 ProtocolError 的批次仅**附加** issue 事实 |
| Agent | 三工具架构、whitelist、read-only 边界不回退；DTO 仅新增可选字段 |
| UI | 非 ProtocolError 行渲染不变 |

T014 是 **additive deterministic information**。

## 24. T014 Unit-Test Matrix（Phase B 执行；RED→GREEN）

> 命名沿用 test_transaction_analysis 的 a 序列；标注 [update]=改现有断言、[new]=新增。每条必须证明 A 高层状态仍 ProtocolError、B detail/reason 正确、C 统计口仅 protocolErrorCount、D 重复 analyze 确定性。

| ID | 场景（输入） | 期望 |
| --- | --- | --- |
| a06 [update] | 地址不符（request=1, response=2，现有金样） | ProtocolError + `ResponseAddressMismatch` + expected=0x01/actual=0x02 |
| a07 [update] | 响应 fc=0x04 | ProtocolError + `UnexpectedResponseFunction` + actual=0x04（无其他载荷） |
| a08 [update] | byteCount 诈称的 0x03 响应（现有金样） | ProtocolError + `MalformedNormalResponse`（无载荷） |
| a09 [update] | 数量不一致（resp 3 值 ≠ qty 2，现有金样） | ProtocolError + `QuantityMismatch` + expected=2/actual=3 |
| a10 [update] | 0x83 但 data 空（现有金样） | ProtocolError + `MalformedExceptionResponse`；仍断言 `!exceptionCode.has_value()` |
| a11 [update] | response wire 2 字节 | ProtocolError + `ResponseFrameTooShort` |
| a13 [new] | 响应 fc=0x84（异常形状非 0x83，Serial A15 同族） | ProtocolError + `UnexpectedResponseFunction` + actual=0x84 |
| a14 [new] | 防御路径：非法 request（如 quantity=0 的构造 Frame）直调 analyzer | ProtocolError + `UnknownProtocolError`（sentinel，满足 I2） |
| a15 [new] | 非 ProtocolError 全族（Success/Exception/CrcError/Timeout/Pending） | `issue == nullopt`（I1/I3/I4） |
| a16 [new] | 不变量扫捕：固定输入集上 `status==ProtocolError ⟺ issue.has_value()` | 双向成立（I2） |
| a17 [new] | 同输入两次 analyze | 返回完全相等（含 payload；I6） |
| a18 [new] | per-code 载荷约束（I5 表格逐行） | 每个 code → 要求字段存在、其余 absent |

`test_transaction_statistics.cpp`：R-STAT 回归 [new]——多 reason 三笔 ProtocolError batch 的 snapshot 与“同 status 无 issue”的等价 batch **逐字段相等**，且 `protocolErrorCount==3`、其余计数不变。

## 25. Integration-Test Matrix（Phase B）

| ID | 目标 | 断言 |
| --- | --- | --- |
| TX-I* [new] | 混合 batch：Success+Exception+CrcError+Timeout+2 个不同 reason 的 ProtocolError | 统计与 T014 前口径一致（protocolErrorCount=2 合并计数）；`summarizeProtocolIssues` breakdown 各=1；不依赖任何 QML 文案 |
| REPLAY-I05 [update] | replay 地址不符（encodeRtuFrame 生成） | 补 issue/payload 断言（与 a06 同款） |
| REPLAY-I06 [new, P1] | 同一 wire 走 `analyzeReplayLog` 与 `test_serial_session` 会话 | 两路 `TransactionAnalysis`（含 issue）严格相等（§22 一致性） |
| SERIAL-A07 [update] | 2 字节 partial → FrameTooShort（PE-5 历史金样） | ProtocolError + `ResponseFrameTooShort` |
| SERIAL-A11 [update] | 会话收到地址不符响应 | ＋`ResponseAddressMismatch` payload |
| SERIAL-A15 [update] | 0x84 5 字节收口（不硬编码 0x83 的既有金样） | ＋`UnexpectedResponseFunction` actual=0x84 |
| SERIAL-A16 [update] | byteCount=2 的 7 字节响应 | ＋`MalformedNormalResponse` |
| UI-B* [new, 若 Phase B 落地 UI role] | ProtocolError 行 role 文案确定性 | 非 ProtocolError 行 role 为空；文案固定不随 QML 变化 |
| AGENT-A11 [new] | detail/anomaly DTO 序列化 | `issue_code` 与 payload 仅在存在时输出；三工具数量与白名单不变 |

## 26. Regression-Test Scope（三级，不机械全改）

**required（必须更新/新增）**
- `test_transaction_analysis.cpp`（a06~a11 update + a13~a18 new）、`test_transaction_statistics.cpp`（R-STAT）、`test_replay_analysis.cpp`（i05 update）、`test_serial_session.cpp`（a07/a11/a15/a16 update 或新断言）、`test_diagnosis.cpp`（R-BASE：断言既有 finding/顺序不变 + 若采纳方案 A 断言 `summarizeProtocolIssues`）、`test_agent_tools.cpp`（A03 扩充 + A11 new）、AI prompt 相关测试（`test_ai_client.cpp` 内 prompt 事实行断言：有 issue 行含 `issue=` 字段；无 issue 批次 golden 字节不变）。
- Phase B 步骤 0 = 编译面普查：对 `TransactionAnalysis{` 全部站点（`TransactionAnalysis.cpp`、`test_transaction_analysis.cpp`、`test_transaction_statistics.cpp`、`test_diagnosis.cpp`、`test_ui_bridge.cpp`、`test_ai_client.cpp`、`test_agent_tools.cpp`、`test_agent_runtime.cpp`）逐一确认 append-last 后源兼容。

**recommended（建议做）**
- 独立不变量扫描小工具化测试（§24 a16/a18 的批量形态）；fuzz-lite：随机/截断 response wire → 必出 CrcError/协议错 + issue 且无崩溃（沿 T003 起的 fuzz-lite 精神）。

**unnecessary（不改）**
- QML 文案测试、`test_replay_log.cpp`（parser 零相关）、`test_modbus_*`/`test_function03`/`test_simulator*`/`test_fault*`（协议/模拟层零相关）、`test_statistics_integration.cpp` 内部实现（聚合层不变的直接结果）、QA/LLM 相关测试。

## 27. Explicit Non-Goals（明确不是）

不是 root cause engine；不是 expert system；不是 fault correlation engine；不是 physical diagnosis；不是 device-health score。**只做 deterministic information preservation。** 亦不是：T015（invalid-request 可观察性）、FC06/FC10、Broadcast、Replay v2、UART timing。

## 28. API / Data Model Review Answers（八问）

1. **为什么不用增加新的 TransactionStatus？** 六状态是归一化结果轴，统计/仪表盘/Agent whitelist/固定 finding 顺序全部消费它；把 reason 抬成状态会连锁破坏 exhaustive switch、七计数不变式与金样数字，且把 outcome 与 reason 两个正交轴混为一谈。
2. **为什么不是 QString reason？** Core 是 Zero Qt（Qt 类型只允许在 app 层）；字符串不可穷举开关（丢失 -Wswitch 保护）、允许拼写漂移、类型不安全。enum 在 Core 判定、在 app 层确定性地映射为人类语言。
3. **为什么 reason 属于 Core？** reason 诞生于 Core 的判定瞬间（分支 4–10），只有随 analysis 值保存才是“同一事实”；任何下游重推都会再实现一遍协议判断，违反单一事实权威，且三模式一致性（§22）会失去结构性保证。
4. **为什么 AddressMismatch 是事实，“设备地址配置错误”却不是事实？** 前者是两个实测字节的不等（request.address vs response.address 在同一等待窗口内被看见）；后者是配置世界的推断，需要设备清单/意图拓扑等外部证据。
5. **为什么 QuantityMismatch 属于 transaction layer？** 单帧 decode 各自合法；数量一致性是跨帧（请求要几个 vs 响应给几个），只有事务窗口知道“问了什么”（T007 定案：该检查不能放 T004B）。
6. **为什么 FrameTooShort 可以保留，BitFlip 却不能？** FrameTooShort 是 decode 层已命名的确定性结果（bytes<4）；BitFlip/EMI 是物理叙事，CRC 失败存在多种候选原因，无法唯一证明。
7. **为什么 Statistics 不按 reason 拆？** 计数是高层监控轴（六族、锁定不变量与金样）；reason 是逐事务上下文，价值在解释与查询而非聚合度量。
8. **为什么 AI/Agent 不能重新猜 reason？** “AI is interpreter, not detector”（T011）与 ISSUE-006 纪律：模型不得建立协议事实；工具只读 Core 事实。LLM 重猜等于引入第二个未审计分类器。

## 29. Documentation / Files Changed（本阶段 docs-only）

- 新增：`docs/tasks/T014-diagnostic-detail-preservation.md`（本档案）。
- 更新：`docs/PROJECT_STATUS.md`（T014 立项行 + §2/§3 状态 + 变更记录）；`docs/BACKLOG.md`（M8 里程碑行 + T014/T015 任务行 + 建议路线 + 变更记录）。
- **不修改**：`docs/02_ARCHITECTURE.md`、`docs/04_TEST_STRATEGY.md`（Phase A 设计尚待用户批准；经批准后 Phase B 实现落地时再锁长期契约，避免把提案当定案写入架构文档）；`docs/09_DIAGNOSTIC_COVERAGE_AUDIT.md`（未发现真实错误，历史结论不动）；`samples/demo_v2.mlog`（untracked evidence fixture，只读）；src/tests/CMake/scripts/QML = 零修改。

## 30. Phase B Preview（Test First + Implementation 顺序，待批准后执行）

1. 编译面普查（§26 步骤 0）→ 2. RED：测试矩阵 §24/§25 先写（含 update 断言）→ 3. Core：`TransactionIssue`/`TransactionIssueCode`/`transactionIssueName` + `TransactionAnalysis.issue`（append-last）+ `makeAnalysis` 第 4 参数 + 分支 4–11 填充 → 4. GREEN + 全量 ctest/qml smoke/deploy 回归 → 5. 下游 additive：prompt builder 事实行与 system 语义、AgentTools DTO（三工具不变）、`summarizeProtocolIssues`、UI role（最小 B 方案）→ 6. 文档归档 + LKGC 候选待用户 review（**不得自行推进**）。

## Problems Encountered / Solutions

- **P1（分支 7/11 的归属）**：初稿把“防御性 request 重解码失败”与“response 形状非法”混成一个 reason；核对 `TransactionAnalysis.cpp:72-82` 后确认两分支可分且语义不同 → 拆分：response 侧失败 = `MalformedNormalResponse`，request 侧契约违反与 switch 哨兵 = `UnknownProtocolError` sentinel（I2）。
- **P2（CrcError 是否挂 issue）**：核验 `RtuDecodeErrorCode` 恰两值（`ModbusRtuCodec.h:12-15`）后确认 CrcError 只有 CrcMismatch 一个确定性来源 → 不重复保存（I4），避免冗余双写。
- **P3（载荷存全还是存最小）**：在“只存 actual”与“expected+actual 都存”之间按价值/成本逐字段决策（§13/§14/§16），QuantityMismatch 因下游无处可查而必须存 expected，AddressMismatch 因自包含与 1 字节成本存双值，FunctionMismatch 因可推导只存 actual。
- **P4（档案口径防漂移）**：整个分支矩阵只来自当前 `TransactionAnalysis.cpp` 的行号逐条核验，未引用旧文档的 branch 描述（M8.1 教训）。

## Verification（本阶段）

- `git diff --check` = 0；`git diff --name-only` 仅 `docs/tasks/T014-diagnostic-detail-preservation.md`、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`；`git status`：tracked 无其他改动，`samples/demo_v2.mlog` 仍 untracked/unstaged/unchanged；src/tests/CMakeLists/scripts/QML = 零修改。
- 证据命令性事实：`RtuDecodeErrorCode` 两值、`Function03DecodeErrorCode` 五值、TX 测试 a06~a11 现断言、7 文件 `TransactionAnalysis{` 站点 —— 均为本阶段 grep/Read 实核，非摘抄。
- 本阶段零构建零测试（docs-only；未改任何代码，无构建必要）。

## Result

- Phase A 完成：七分支重构 + 信息损失矩阵 + 最小数据模型定案（A′）+ 六不变量 + 统计/请求侧/UI 边界 + 测试矩阵（unit/integration/regression 三级）。
- **T014 = IN PROGRESS；Phase A = DONE / AWAITING REVIEW；Phase B = WAITING FOR USER APPROVAL。**
- verified LKGC 维持 `99f17d6` 不变（docs-only 不推进）。

## Knowledge Learned

- 归一化状态轴（orthogonal status）与诊断细节轴（reason）分离是信息无损化的最小代价路径；把 reason 提为状态是常见的过度建模陷阱。
- “append-last + 默认成员初始化”是 C++20 聚合结构体做 additive 变更的编译兼容钥匙（`==` 默认比较也随行）。
- -Wswitch 穷举 + 哨兵 sentinel 让“未来枚举增值”的语义安全在类型层面显式化。
- 单一事实漏斗（makeAnalysis）把不变量验证收敛到一点，是 T007 以来连续有效的模式。

## Potential Interview Questions

1. 为什么不把 ProtocolError 拆成几十个状态，而是加 orthogonal detail？——两轴分离：统计/仪表盘消费归一轴，解释层消费细节轴；拆轴会连锁破坏 exhaustive switch 与金样。
2. TransactionIssue 为什么是 `optional<struct>` 而不是 `variant<struct...>`？——7 个 code 时 variant 的 visit 开销与 breaking 演进不值；optional+稀疏载荷兼具类型安全与增量兼容。
3. 你怎么保证三模式 issue 口径一致？——共享 Core 唯一漏斗，Replay/Serial/Simulator 都只是调用 analyzeFunction03Transaction；集成测试直接断言两路结果严格相等。
4. 什么是“确定性事实”与“物理根因”的界限？——能由当前字节/时序观测直接推出的 vs 需要外部证据才能唯一确定的；FrameTooShort 是前者，BitFlip/EMI 是后者。
5. 新增字段为什么不会破坏现有测试与统计？——append-last 默认值保持聚合初始化兼容；summarizeTransactions 不读新字段；R-STAT 回归锁定 snapshot 逐字段相等。
6. AI/Agent 拿到 issue 后可能过度归因，你怎么防？——沿 ISSUE-006：system 语义句族提前定义每个 issue 的确定性含义与禁止推断边界；工具只读 Core 事实。

## Git Commit

（docs-only；提交哈希与信息见 git log 与 PROJECT_STATUS/BACKLOG 变更记录）