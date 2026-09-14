# T015 — Passive Replay Expansion（被动回放诊断扩展）

- **Goal**          解决“哪些**真实历史事务**根本进不了现有分析模型”——把 Replay 从 active-master 的 trusted-request 契约迁移为 passive observer 契约：FC06 / 0x10 被动语义、generic exception 识别、invalid-request 可观察性、broadcast expected-no-response；同时不破坏六状态、Statistics、Active Serial read-only 边界。
- **Background**    M8.1 审计（demo_v2 14 场景）确认当前是 FC03-centric transaction-level analyzer：S2/S3 写功能被 `InvalidRequestFunction` 拒之门外、S5 合法 Exception 01 被请求门挡住、S6 invalid request + 合法 Exception 03 整笔丢弃、S4 broadcast 语义缺失、坏 request 毒死整个 batch。T014 已解决“已接受事务的 **detail** 丢失”（TransactionIssue，六状态不变）；T015 处理的是它们的**上游**：进入模型之前。verified LKGC = `cc8393a`。
- **状态**           **T015 = IN PROGRESS（整体）**。Phase A（Learning + Test Design）与 **Phase B（Passive Core + FC06 + Broadcast）= DONE / REVIEW PASS**（用户 Manual UI Smoke = PASS；verified LKGC = `02ce302`）；**Part C（Function 0x10 normal semantics）= NOT STARTED**。
- **Phase 状态**     Phase A = DONE / REVIEW PASS（Gate A~F 全批）；**Phase B = DONE / REVIEW PASS**；**T015 整体不得标 DONE**（Part C 未启动）。

## 1. Evidence Inspection

- 文档：AGENTS、README、01/02/03/04/07/08/09、PROJECT_STATUS、BACKLOG、INTERVIEW_NOTES；重点任务 T004/T007/T009/T010/T014，参考 T011/T012。
- 协议事实（repo 内权威证据，**非记忆**）：`03_MODBUS_LEARNING.md:38` —— “地址 0 = 广播；广播只允许写类功能码，无响应”；:51-56 —— 功能码表（0x06 Write Single Register“单寄存器”、0x10 Write Multiple Registers“多寄存器 **1–123**”）；:59 —— “异常响应：功能码 | 0x80 + 异常码”（generic exception 结构）；:61-71 —— 标准异常码 0x01~0x0B 表。
- **repo 未记载的协议事实**（按用户 §15/16 纪律标注 External Protocol Reference Required，Phase B 须先对官方 Modbus Application Protocol 规范取证再写常量）：FC06 request/response 字段布局与“normal response 是否 echo request”契约；0x10 request 字段布局与 byteCount=2×quantity 约束、normal response 字段。Phase A 只设计**结构与接口**，不发明常量。
- 代码核验（全部来自当前最终代码，与 T014 同批）：
  - `ReplayAnalysis.cpp:18-36` 请求可信链（decodeRtuFrame → fc==0x03 → decodeReadHoldingRegistersRequest）与三错误码；`ReplayAnalysis.cpp:38-61` 响应侧观察与 T007 复用；`ReplayLog.cpp` parser 八错误码、fail-fast、仅 `NO_RESPONSE` token；`TransactionAnalysis.{h,cpp}`（T014 后含 issue）；`SerialTransactionSession.cpp`（active 路径直呼 analyzeFunction03Transaction）；`RuleBasedDiagnosis/DiagnosisContext`；`DiagnosisPromptBuilder`；`AgentTools`；`AnalysisController`（三条 entry 构造 + loadReplayFile 原子发布）；`Main.qml` 五行行模板（T014 后 secondary detail）。
- 测试现状：REPLAY-A01~A08（parser）、REPLAY-I01~I05/03B/03C（分析）、UI-R01~R08（含 **R04 断言 InvalidRequestWire→人读文案的 load 失败语义**）、SERIAL-A01~A16、TX a01~a18、STAT-B01~B09、DIAG、AI-B、AGENT-A、UI-T01。

## 2. Current Replay Contract Reconstructed（从最终代码逐环节重建）

```text
.mlog text ──parseReplayLog──► ReplayLog{timeoutThreshold, records[elapsed, requestWire, responseWire?]}
                                                  │ （语法层：八错误码、fail-fast、仅 NO_RESPONSE token）
                  requestWire ──decodeRtuFrame──► 失败 ⇒ ReplayExecutionError::InvalidRequestWire（整批失败）
                                                  │ 成功 ⇒ ModbusRtuFrame
                  fc != 0x03 ──────────────────► ReplayExecutionError::InvalidRequestFunction（整批失败）
                  decodeReadHoldingRegistersRequest 失败 ⇒ ReplayExecutionError::InvalidRequestData（整批失败）
                  responseWire 缺省 ⇒ NoResponse；否则 decodeRtuFrame ⇒ Frame | RtuDecodeError
                  analyzeFunction03Transaction(request, observation, elapsed, threshold)
                  （T014 后内部产出 TransactionIssue）
                  ⇒ ReplayTransactionOutcome{deviceAddress, functionCode, analysis}
                  ⇒ ReplayBatchAnalysis{transactions, statistics=summarizeTransactions(analyses)}
```

**当前三种结局（锁定事实）**：
- **A. 整个文件 parse fail**：任一语法错误（InvalidHex/InvalidRecord/…）即 `ReplayParseError`，后续记录全部不可达（fail-fast，M8.1 实测 demo_v2 死于 L15）。
- **B. `ReplayAnalysisResult = ReplayExecutionError`**：任一记录请求侧三错误之一 ⇒ **整个 batch** 失败（`loadReplayFile` 只设 replay error，旧 batch 全保留，UI-R03/R04/R05 锁定）。
- **C. 正常 `TransactionAnalysis`**：仅当每一条记录的请求都过 FC03 可信链。

## 3. Three Kinds of “Bad Input”（T015 核心分类）

| 类 | 例子 | 含义 | 建议归宿 |
| --- | --- | --- | --- |
| A. Replay File **Syntax Error** | 非法 hex token、字段数≠4、header 错误 | 日志格式本身无法解释 | **继续 load/parse failure**（§22 不动 parser） |
| B. Captured Request **Protocol-invalid** | hex 合法但 CRC 错；FC03 quantity=126（CRC 合法） | “历史总线上真的观察到一个坏 Request”——passive 视角下是**诊断事实** | 未来 → **per-record 诊断观察**（requestIssue），**不再整批毒死** |
| C. Analyzer **Unsupported Function Semantics** | FC06/0x10 尚未支持时、FC08 normal | **不等于该 Modbus Request 非法** | 未来 → **per-record Unsupported 观察**（或 T015 落地 FC06/0x10 后进入正常分析）；**禁止**混称为 invalid |

**关键区分口号**：`unsupported by ModbusLens ≠ invalid Modbus request`。

## 4. ReplayExecutionError Audit（逐错误码重评估）

| 错误码 | 真实语义 | 属于 | T015 后建议 |
| --- | --- | --- | --- |
| `InvalidRequestWire`（decodeRtuFrame：CRC 错 / <4B） | 记录是合法 hex，但请求 wire 自身损坏 | **B（captured anomaly）**，非 infrastructure | Gate E 决策：Scope A 下**暂保留整批失败**（明确记录为 scope-limitation）；不建议本轮展开 |
| `InvalidRequestFunction`（fc≠0x03） | 请求本身合法 Modbus，只是 ModbusLens 不识 | **C（unsupported）** | FC06/0x10 落地后走新 passive 路径；其余 fc → per-record Unsupported 观察 |
| `InvalidRequestData`（quantity 越界等） | 请求**确实违反协议约束**（观察事实） | **B（captured anomaly）** | → per-record `RequestProblem` 观察：requestIssue + 可选合法 Exception 响应（§10） |

**因此**：三错误码没有一个是真正的“replay infrastructure failure”——它们全部是历史流量事实。当前把 B/C 当整批 execution error 处理，是 active 契约错配到 passive 流量的直接后果。

## 5. Active Master vs Passive Observer（两种契约，正式建档）

| | Active Serial（及 Simulator） | Passive Replay |
| --- | --- | --- |
| Request 作者 | ModbusLens 自己构造 | 历史上不知名主站 |
| Trusted-request 契约 | **合理**（发坏请求是自家 bug，编码器已校验） | **错配**（坏请求是待诊断事实） |
| T007 现状 | 直呼 analyzeFunction03Transaction | 复用同一 T007（借用了 active 契约） |

**结论**：T007 的 trusted-request contract 对 Active 模式是**对的**，不应被“推翻”；真正的问题是 Replay 把 active 契约直接套在了 passive historical traffic 上。T015 的修复点是 **Replay 层之上的 passive 分析面**，而不是改 T007。

## 6. Active Serial Safety Red Line（永不突破）

T015 之后 Active Serial：继续 FC03 read-only；**继续禁止** FC06/0x10 send、write register、broadcast send、Agent actuation、automatic retry。**Passive understanding ≠ Active capability**——Replay 能理解历史写事务，绝不授予 UI/Serial/Agent 任何写权限。此边界写入本档案与验收标准（测试 P18 纠察：源码 grep 级证明无 encoder/无 send API）。

## 7. Analyzer Architecture Options（Gate A）

| 方案 | 形态 | 评价 |
| --- | --- | --- |
| A. 扩大 `analyzeFunction03Transaction` | 让 0x03 函数容纳所有 fc 逻辑 | ❌ 函数职责失真；active/passive 契约混于一炉；名字与行为背离；T014 已有大量测试锁定其 FC03 语义，扩大=回归风暴 |
| B. 新增 generic/passive analyzer（**推荐**） | `analyzeObservedTransaction(...)`：按 request fc dispatch；内部对 FC03 **复用** analyzer（不复制）；generic exception 路径只写一处；最终仍产出共享 `TransactionAnalysis` | ✅ active 路径零触碰；协议/事务 Core 仍是唯一事实 authority；passive 语义集中一处；未来 passive tool 可复用 |
| C. Replay 层自己判断各功能码 | 回放层私有协议规则 | ❌ 协议规则复制；Replay 与未来其他 passive source 出现第二事实源——违反共享核心纪律 |

**Gate A 推荐 = B**。位置 `src/core/analysis/PassiveTransactionAnalysis.{h,cpp}`（Zero Qt）。T007 `analyzeFunction03Transaction` 一字不改；FC03 的 passive 路径就是 dispatch 到它。

## 8. TransactionAnalysis Shared / Not Shared（核心裁决）

**推荐：继续共享 `TransactionAnalysis` 作为 passive 多功能的 high-level outcome。**

理由：
1. FC06/0x10 的正常/异常事务天然落在同一个 outcome 轴上（Success/Exception/CrcError/Timeout/ProtocolError/Pending 全部语义适用：Success=匹配 normal response 观察到、Exception=匹配 exception 观察到、CrcError/Timeout 与 fc 无关）。
2. Statistics、DiagnosisContext、Baseline、Prompt、Agent、UI 全部已经消费 `TransactionAnalysis` —— 共享即零下游重构。
3. 逐步成为 **function-agnostic high-level transaction outcome**：函数相关的语义只活在 dispatch 前的 semantic 校验层，outcome 保持“事务层语言”。

**拒绝** `PassiveTransactionAnalysis` 分离：两套 status/statistics/diagnosis 长期漂移的风险（M8.1 的“两套世界”警告）。唯一例外：**“无法形成分析”的记录**（§12 Unsupported/RequestProblem 观察）不进六状态池——它们不是 transaction outcome（见 §23），但**已分析**的每条记录必产出共享 outcome。

## 9. TransactionIssue Evolution（Gate B）

问题：Scenario 6 将是 `status=Exception + exceptionCode=0x03` 且 request 同时 `quantity invalid`。T014 的不变量“非 ProtocolError ⇒ issue absent”（I1）与之冲突。

| 方案 | 形态 | 评价 |
| --- | --- | --- |
| A. 扩展现有 `TransactionIssue` 允许非 ProtocolError 携带 | 打破 T014 I1 + per-code 载荷表重写 | ❌ I1 被 TX a14 锁定；“ResponseFrameTooShort/AddressMismatch…”全部是 response-side 事实，与 request-side 事实混在一个 enum 里语义混乱；T014 回归成本大 |
| B. Outcome 级新增正交字段（**推荐**） | `ReplayTransactionOutcome` / `DiagnosisTransaction` 增 `std::optional<TransactionRequestIssue>` | ✅ T014 契约一字不动；request-side 事实有自己的 invariants；active 路径永远 nullopt；下游 additive |
| C. 新 request-analysis 子结构 | request 侧单独一层对象 | ⚠️ 结构最整齐，但会让“一笔事务”裂成两个对象，UI/Agent 拼接成本上升；B 已够用 |

**Gate B 推荐 = B**。`TransactionRequestIssue`（Core，Phase B 定义）候选 code（以最终实现 scope 为准）：`InvalidRequestQuantity`（且可带 observed 值载荷）、`InvalidRequestLength`。**禁止** root-cause 类 code：`WrongDeviceConfiguration / BadPLCProgram / OperatorError`。
高状态语义：**Scenario 6 的 high-level status = `Exception`**（设备确实返回了匹配且合法的 Modbus Exception——响应侧事实），requestIssue 是正交的请求侧事实。**不得**把 requestIssue 塞成 ProtocolError。

## 10. Scenario 6 — Invalid FC03 + Legal Exception（设计定案）

```text
request: FC03, quantity=126（wire CRC 更正合法）
response: 0x83/0x03（wire CRC 更正合法，地址/异常函数匹配）
⇒ 不在请求语义校验处“停止”
⇒ status = Exception, exceptionCode = 0x03
⇒ requestIssue = InvalidRequestQuantity（载荷：observedQuantity=126，上限 125）
⇒ 同一条记录进 Statistics（exceptionCount+1）、UI 一行双事实、
   prompt/agent 拿到 status + exception_code 0x03 + request_issue_code
```

证据边界：requestIssue 说“请求数量不符合 0x03 协议约束（实测 126）”，**不说**“主站软件写错了”。与 M8.1 §19 的候选完全一致（RequestIssue::InvalidQuantity + Response::Exception(0x03) 并存，而非丢弃）。

## 11. Generic Exception Handling（只写一处）

协议证据：`03_MODBUS_LEARNING.md:59` —— “异常响应：功能码 | 0x80 + 异常码”。判据（不需要完整功能语义）：

```text
response.address == request.address
response.functionCode == request.functionCode | 0x80
response.data.size() == 1（异常码字节）
⇒ status = Exception, exceptionCode = data[0]
```

**只需要**：request address、request fc、response address、response exception fc、exception payload。因此 FC08 请求 + 0x88/0x01（S5）**无需 FC08 normal decoder** 即可被正确诊断为合法 Exception。generic matcher 在 `PassiveTransactionAnalysis` 中**只写一处**；FC03 自己的异常判据仍走既有 analyzer（双路径终态一致，以既有 TX 测试为准）。另行明确：**“能诊断一个合法 Exception” ≠ “支持 FC08 全部正常语义”**——normal FC08 response = per-record Unsupported 观察（§16 P04 政策）。

## 12. Scenario 5 — FC08 + Exception 0x01（设计）

推荐：**支持 generic exception-only analysis for otherwise unsupported request functions**。FC08 request（CRC 合法）+ `01 88 01`（CRC 修正）→ `status=Exception, exceptionCode=0x01`，行 functionCode 显示 0x08。external 声明：ModbusLens **不声称支持 FC08**（normal FC08 semantics 属 Unsupported，§16）；当前 Baseline 的 0x01→CheckFunctionSupport 建议自动适用（“设备说非法功能码”的诊断完全成立）。

## 13. FC06 Passive Semantics（Phase B 取证后落地）

- **repo 权威证据**：仅功能码名称（03:51-52 “Write Single Register｜单寄存器”）+ 广播允许类（写类）＋异常结构；**request 字段布局与 echo 契约 repo 未记载** ⇒ **External Protocol Reference Required**：Phase B 实现前须以官方 Modbus Application Protocol Specification（Fc06 章）取证并记录于 03_MODBUS_LEARNING（新增条目），禁止凭记忆写常量。
- Phase A 只定**结构**（取证后填字段）：`Function06.{h,cpp}`（`src/core/protocol/`）提供 `decodeWriteSingleRegisterRequest/Response`（Pure C++，variant 错误，风格同 Function03）+ `matchWriteSingleRegisterTransaction`-式语义仅当规格确认 echo 契约后定型；exception 走 generic path（§11）。
- 提供：**只有 decoder/matcher**。**禁止 encoder、Serial send API、QML write control**（§6 红线）。

## 14. Function 0x10 Passive Semantics（命名即证据）

- 名称：**Function 0x10 — Write Multiple Registers** 全称（文档与档案内禁止仅写“FC10”造成十进制/十六进制歧义）；文件命名讨论见 §17。
- **repo 权威证据**：03:54 “多寄存器 **1–123**”（quantity 合法域）；异常结构与广播类同上。request 字段布局、**byteCount=2×quantity 关系**、normal response 字段与合法范围细节 repo 未记载 ⇒ **External Protocol Reference Required**（同上流程，Phase B/Part C 取证）。
- Phase A 只定结构：`Function16`-style decoder + matcher + generic exception；**禁止 encoder/active write**。

## 15. FC06 / 0x10 Implementation Split（Gate D）

**推荐：分阶段 —— Phase B 只做 FC06；0x10 拆独立 Part C。**

理由：①FC06 定长极小（request 4 数据字节 + normal 定长），测试量与取证量远小于变长的 0x10（byteCount/quantity registers 关系 + 4 项字段核对）；②0x10 的 quantity 域（1–123）与 byteCount 校验是独立一轮 RED 矩阵；③demo_v2 两个场景分别兑现（S2 先行，S3 Part C）；④秋招 scope：FC06 已证通用性（generic exception + dispatcher 复用），0x10 是“再来一个同样的”边际收益，分阶段展示更真实。**不因“功能更多”一次全塞。**

## 16. Broadcast Protocol Evidence（Gate C 依据）

- repo 权威证据：`03_MODBUS_LEARNING.md:38` —— “地址 0 = 广播；广播只允许写类功能码，无响应”。
- **适用性判定规则（禁止过度泛化）**：`request.address == 0` **且** `request.functionCode ∈ 写类功能码集` ⇒ broadcast；当前写类集在 T015 语境 = {0x06, 0x10}（本任务支持域），其他写类 fc（0x05/0x0F/0x16…）按其支持状态另议。**禁止**“address=0 一律 expected no response”（FC03 read 以地址 0 出现 = 协议不允许 → 记为 requestIssue 观察而非 broadcast）。

## 17. Broadcast Outcome Options（Gate C 三案 + 数学）

先锁定事实：broadcast 不期待响应 → 不能 Pending/Timeout（会误导为失败）；无响应也**不能证明**“所有从站写成功” → 不能 Success（且 §20 明确 Success 语义不得被偷偷重定义）。**六状态集合中没有诚实的成员** ⇒ 任何诚实方案都要动 status 轴或统计口径 ⇒ **USER ARCHITECTURE DECISION REQUIRED（不得 Phase B 自动实施）**。

| 案 | 形态 | Statistics 数学 | 评价 |
| --- | --- | --- | --- |
| A. 第七状态 `ExpectedNoResponse` | 新增 TransactionStatus | 变体1：invariant B 扩为六分类+broadcast；rate=suc/completed（合法广播**会稀释**成功率——违反 §26 目标）；变体2：rate=suc/(completed−broadcast)（“已应答事务”口径），invariant C 条件改；(**) | 需要用户拍板；影响 Statistics/Dashboard/Baseline/AI/Agent/UI 六层 |
| B. 六状态 + 正交 `ResponseExpectation/TransactionKind` | 状态仍六值，outcome 增 kind | 依旧无状态可给 broadcast……（要么回退到 A，要么 kind=Broadcast 时 status 留“未定”=两套世界） | ⚠️ 概念对、落点难 |
| C. Broadcast 用独立 passive outcome 不进六状态统计 | 与 §8 共享裁决冲突：两套世界 | 广播行另立 counter；observed/completed 是否含？含则 invariant B 裂，不含则 row 计数≠observed | ❌ 两套世界，长期漂移风险 |

**Phase A 推荐（待用户裁决）**：**案 A + 变体1** 为最简（保留“completed=各状态之和”哲学，一条新计数列 + UI 一行），**但**因 §26 明确“不能让合法 Broadcast 无意义拉低 successRate”，把 **案 A 变体2** 作为备选主推并给出精确公式：
```
observed      = pending + completed                 （不变）
completed     = success+exception+crcError+timeout+protocolError+expectedNoResponse  （B′）
successRate   = success / (completed − expectedNoResponse)，当 completed−expectedNoResponse>0；否则 nullopt （C′）
avgSuccessLatencyMs 定义不变（仅 Success 的 elapsed）
```
证据边界文案（status 语义层）：`ExpectedNoResponse` 只说“未观察到响应，且协议上该广播请求不期待响应”——**绝不**渲染为“写入已成功应用到所有从站”。由用户在 Gate C 三案中裁决；采用新状态/新口径前 Phase B 不启动相关实现。

**→ 2026-09-13 Phase A Review 最终批准口径（Gate C = APPROVED）**：采用上列公式且明确——
- `completed` **不得包含 Pending**；`ExpectedNoResponse` 属于 completed observation。
- `rateEligibleCompleted = completed − expectedNoResponse`；**`successRate = success / rateEligibleCompleted`；`rateEligibleCompleted == 0 ⇒ successRate = nullopt`**。
- `averageSuccessLatencyMs` 仍只由 Success 记录计算。
- 另批准：FC06 exact-echo mismatch 必须使用独立 deterministic issue `WriteSingleRegisterEchoMismatch`（**不得**误写成 `MalformedNormalResponse`）——此为 Phase A Review 新增要求。

## 18. BROADCAST_NO_RX Token Decision（§19/§21 结论）

**推荐：不新增 mlog token。** 广播期望可由 `request.address==0 ∧ request.fc∈写类集`（§16）**推导**，日志继续记 `NO_RESPONSE`，Analyzer 判 expected vs unexpected。理由：observation token 只应表达“观察到什么”（no bytes/a wire），“这该怎么解释”是 analyzer 的业务结论，塞进 wire token 会让格式承载业务判断、且 demo_v2 的 `BROADCAST_NO_RX` 正是这种硬编码例子。`.mlog v1` 语法**保持不动**（§22）。Scenario 4 流程：
```text
request decode OK → FC06 语义（取证后）→ address==0 ∧ 写类 ⇒ expectation=no-response
→ 观察到 NO_RESPONSE ⇒ ● Gate C 选定的 outcome
⇒ 系统能证明：未观察到 Response，且该请求协议上本不期待 Response
⇒ 系统不能证明：任何设备执行成功（UI 措辞必须尊重此边界）
```
若日后出现“broadcast 却有响应”（P10）：确定性事实=期待无响应却收到 wire ⇒ 提案 `ProtocolError + TransactionIssueCode::UnexpectedResponseForBroadcast`（T014 枚举 additive 扩展，属 Gate C 附注）。

## 19. Request Wire CRC Scope（Gate E）

| | A. 只解决 semantic-invalid valid-CRC request（**推荐**） | B. 同时吞并 request wire corruption |
| --- | --- | --- |
| 内容 | quantity/length 类（request 可解码、CRC 正确） | 另建 captured-request-corruption 观察（CRC 错/<4B） |
| 风险 | — | 损坏请求的解释空间爆炸（对齐/噪声/捕获损毁多层假设）；T015 语义扩展被拖慢；Scope 爆炸 |
| 决定 | **T015 = Scope A**：`InvalidRequestWire` 语义暂时保留（整批 execution error 维持或另议，Phase B 不碰），档案记录 scope-limitation | 未来独立任务（与 UART/timing 同族） |

## 20. Parser Boundary（§22/§23 结论）

- **`ReplayLog` text parser 一字不改**：InvalidHex/InvalidRecord/InvalidHeader… 继续 = file syntax failure（load 失败可接受）。
- `FRAME_FRAGMENT` **不属于 T015**；t1.5/t3.5 仍属未来 Replay event/timing 工作。
- 因此 **demo_v2 as-is 在 T015 完成后也不要求整文件成功加载**（L15 `BROADCAST_NO_RX` 语法错 + L36 五字段）——T015 acceptance 全部使用 **purpose-built valid fixtures**（含“修正 CRC 后的 S2/S4/S5/S6 形态”为设计输入，落地 fixture 时才物化）。demo_v2 维持 untracked evidence。

## 21. Per-record Failure vs Whole-batch Failure（Gate F）

**推荐：parse success 之后，每条 record 独立形成 outcome；parser 语法失败仍整体 load fail（不再“毒死”）。**

- `analyzeReplayLog` 返回 `ReplayBatchAnalysis`，`transactions` 每项成为 `ReplayTransactionOutcome = std::variant<AnalyzedTransaction(TransactionAnalysis), UnsupportedObservation(functionCode 等), ...>`——更保守的最小形态：outcome 保持 `{deviceAddress, functionCode, analysis}` + 新增 optional 正交事实列（requestIssue / unsupported 标志），避免 variant 大改（Phase B 定，Phase A 给两种形状的比较与倾向：**倾向“保留 Analysis 值 + 正交 optional 列”**，因为 UI/统计/诊断轮子全在 analysis 上）。
- 统计口径：**只有 Analyzed 结果进入 `summarizeTransactions`**（不变式族 A~D 一字不改）；Unsupported/RequestProblem 记录不计入六状态统计（“可分析子集”口径，文档明示；未来如需计数另行设计 counter，用户 Gate 决定）。
- 影响面（Phase B 成本，先在档案声明）：UI-R04（InvalidRequestWire 整批失败文案）**语义将被替换**为 per-record 形态——R04 测试随 Phase B 重写（列入回归清单）；demo_v1 四条全部 Analyzed ⇒ 结果与统计逐位不变。

## 22. Six-Status Semantics Review（§24 逐一确认）

| 状态 | passive multi-function 世界下 | 裁决 |
| --- | --- | --- |
| Success | **必须继续=“观察到 matching normal response”** | ✅ 不重定义（重定义=successRate/用户认知/回归连环爆炸；broadcast 已用 Gate C 单独解决） |
| Exception | generic exception 匹配（§11） | ✅ 语义不变，覆盖面扩大 |
| CrcError / Timeout | 与 fc 无关 | ✅ 不变 |
| ProtocolError | response 侧失配文件族 | ✅ 不变（+可能 additive issue code） |
| Pending | 阈值内无响应观察 | ✅ 不变（**broadcast 除外**：由 Gate C 决定其归处，Session/Runtime 层的普通等待语义不动） |

## 23. Statistics Compatibility（§25 证明）

- FC06/0x10 的 Success/Exception 记录 → 现有 snapshot 五计数 + rate/latency **自然复用**（elapsed 即事务时延；证明方法 = PASSIVE-P14 混合批统计断言 + demo_v1 golden 回归）。
- Broadcast → 见 §17 Gate C 数学（recommendation 已给，最终由用户裁决）。
- 四不变量（A~D）在非 broadcast 世界**一字不改**；broadcast 落地时的 B′/C′ 变更随 Gate C 批准一并写测试。

## 24. New Deterministic Request-side Facts（§26 候选，最终以实现 scope 为准）

候选 `TransactionRequestIssue` code：`InvalidRequestQuantity`（载荷 observed/limit）、`InvalidRequestLength`（载荷 observed/expected）。**绝不加入**：WrongDeviceConfiguration / BadPLCProgram / OperatorError / any root-cause guess。传播：Core → `DiagnosisTransaction`（active 路径恒 nullopt）→（additive）Prompt `request_issue_code=` 行 + Agent detail 字段 + UI 第二行 adapter 文案——“请求参数不符合 0x03 约束（数量 126）”。哪些只留在 replay 内部：无不透出的 field（全量事实对上层可见更安全）。

## 25. Diagnosis / AI / Agent Boundary（§27/§28/§29）

- **Baseline**：不新增“从 invalid request 推出 PLC 软件 bug”类规则。可为 requestIssue 增加一条确定性 finding（`RequestIssueObserved` 候选，按 code 分组计数，action=CheckRequestParameters/CheckDeviceDocumentation）——**裁决：required 范围不含新 finding，列为 recommended/P1 候选**（理由：invalid request 常伴随 Exception 响应、Exception finding 已在；先让事实可见，finding 后置，避免 finding 轴churn）。广播Finding 语义可说“此事务为无响应预期的广播观察”，不说“写入已成功”。
- **AI**：prompt facts additive（`request_issue_code`、broadcast kind 语义句族——“广播不响应≠失败≠成功”，全部 Core 产生，模型只读）；禁止模型自己判断“广播是否应该 response”或重验 FC06 语义。
- **Agent**：**不新增 Tool（仍 3 个）**；detail/anomaly DTO additive（`request_issue_code` 仅在存在时输出）；读与不读的边界不变。

## 26. UI Scope（§30 设计）

复用 Recent Transactions / Statistics / Diagnosis 面板，**不建第二 Replay 页面/抓包 UI**。行表达：
- fc 显示已支持任意 0xNN（06/0x10 直接可用）。
- invalid-request + Exception 行：primary = `异常`（status），exception code 次要，T014 secondary line 增 requestIssue 文案（第二行合并或两条 secondary，布局沿用 58px 契约）；primary 绝不因 requestIssue 变“协议错误”。
- broadcast 行：Gate C 选定 status 后的确定性文案（“广播无响应预期”…），**绝不写“成功”**；Unsupported 行：`不受支持（功能码 0x08）` 类保守文案（该行不进六状态统计）。
- 术语修复（设备→设备地址）仍不并入（独立 polish）。

## 27. Core File/API Organization（§31/§32、含命名定案）

- 新协议目录文件（命名定案）：`src/core/protocol/Function06.{h,cpp}`（注释：function code 0x06）；`src/core/protocol/Function16.{h,cpp}`（**头注释明确“function code 0x10 = decimal 16”**，与 Modbus.org 十进制规范名一致、与 Function03 命名风格同构，面试不混）；现行 `Function03` 不动。
- 新分析文件：`src/core/analysis/PassiveTransactionAnalysis.{h,cpp}` —— generic dispatcher（Zero Qt）：
```text
decoded request frame
  ↓ classify request function
  ↓ function-specific request semantic analysis（03=复用；06/16=新 decoder；其他=generic-unknown 政策）
  ↓ response observation（Frame|RtuDecodeError|NoResponse）
  ↓ 有响应：先 generic exception matcher（只写一处）→ 命中 ⇒ Exception(+code)
         未见 ⇒ function-specific normal matcher（03 复用 analyzer；06/16 新 matcher）
         全不中 ⇒ per-record Unsupported 观察（或 ProtocolError 按其失配事实）
  ↓ 无响应：broadcast 期望（§16）⇒ Gate C outcome；否则复用 NoResponse→Pending/Timeout 判定
  ⇒ TransactionAnalysis（analyzed 情形）
```
- `ReplayAnalysis.cpp`：request 可信链改为 dispatch 到 passive analyzer（FC03 golden 路径保证等义）；per-record outcome 化（§21）。
- **禁止**：在 Replay 层写任何功能码语义；exception matcher **只存在于 Core 一处**（不复制到 Function03/06/16 各一份）。

## 28. Backward Compatibility Locks（§33）

demo_v1.mlog 四 outcome 与统计逐位不变；T014 ProtocolError detail（含 issue 载荷）完全保留；Active Serial FC03 不变；Simulator FC03 不变；Baseline 既有 finding 不回退；AI/Agent 3-tool 架构与 read-only 不回退；`.mlog v1` parser 不变。仅 UI-R04 语义随 per-record 化**有意替换**（§21，测试重写与理由一并入 Phase B 档案）。

## 29. Phase B Test Matrix（RED-first；编号按 repo 风格落地）

| ID（概念名） | 场景 | 关键断言 |
| --- | --- | --- |
| PASSIVE-P01 | FC03 existing golden unchanged | demo_v1 golden 四 outcome + 统计逐位不变（回归锁） |
| PASSIVE-P02 | FC06 normal unicast | Success；echo 契约按取证结果断言 |
| PASSIVE-P03 | 0x10 normal unicast（Part C） | Success；byteCount/quantity 校验按取证结果 |
| PASSIVE-P04 | unsupported-function normal response policy | per-record Unsupported 观察（不进统计池），且**不混称 invalid** |
| PASSIVE-P05 | generic exception: FC08 request + 0x88/0x01 | Exception + code 0x01；不需 FC08 normal decoder |
| PASSIVE-P06 | invalid FC03 semantic + matching Exception 0x03 | status=Exception + exceptionCode 0x03 + requestIssue=InvalidRequestQuantity(126)；整批不失败 |
| PASSIVE-P07 | invalid request 不毒死后续记录 | TX#2 invalid 后 TX#3 正常分析；batch 统计来自 analyzed 子集 |
| PASSIVE-P08 | ordinary unicast NO_RESPONSE | 仍 Timeout（P14 不变；broadcast 除外） |
| PASSIVE-P09 | broadcast write + NO_RESPONSE | Gate C 批准后的 outcome + 统计口径（阶段 GATE） |
| PASSIVE-P10 | broadcast 却收到响应 | 按 Gate C 附注：ProtocolError + UnexpectedResponseForBroadcast（阶段 GATE） |
| PASSIVE-P11 | wrong response address | 既有 ProtocolError+AddressMismatch 遗传（跨 fc 亦成立） |
| PASSIVE-P12 | wrong response function | UnexpectedResponseFunction 遗传 |
| PASSIVE-P13 | bad response CRC | CrcError 遗传 |
| PASSIVE-P14 | mixed multi-function batch statistics | Success(03)+Success(06)+Exception(08/01)+Timeout 混合批的七计数/rate/latency 精确断言 |
| PASSIVE-P15 | deterministic repeat | 同 log 双跑 outcome/统计/issue 全等 |

Phase A **不写任何 tests**（writing 属 Phase B）。

## 30. Required / Recommended / Deferred（§35）

- **Required for T015**：passive analyzer（dispatch + generic exception + per-record 化）；FC06（Part B）；Scenario 5/6 语义；broadcast 语义（**Gate C 批准后**）；§6/§35 边界与非目标清单。
- **Recommended if low-cost**：RequestIssueObserved 基线 finding（P1 候选）；UnexpectedResponseForBroadcast issue code（随 Gate C）。
- **Deferred（绝不偷偷进入）**：FRAME_FRAGMENT；t1.5/t3.5；UART errors；physical bus diagnosis；register datatype / business semantics；per-device time window；request-wire corruption 观察（Gate E=A）；0x10（Part C 独立任务）；0x05/0x0F/0x16 等其他写功能。

## 31. demo_v2 Mapping（After T014 → After proposed T015）

| Scenario | After T014（现状） | After T015（预期） | 说明 |
| --- | --- | --- | --- |
| S1 float | 修正 CRC 后 Success（值不保留） | 不变 | 业务语义仍 Deferred |
| **S2** FC06 | InvalidRequestFunction 整批失败 | **Success（Part B，需修正 CRC）** | ✅ 改善 |
| **S3** 0x10 | InvalidRequestFunction | **Part C 后 Success** | ✅（分期） |
| **S4** broadcast | InvalidHex（token 非法）→ L15 整文件失败 | **NO_RESPONSE+地址0 推导 → Gate C outcome**（仍需修 fixture CRC/token） | ✅ 改善（语义层） |
| **S5** FC08+Exc01 | InvalidRequestFunction | **Exception 0x01（generic path；normal FC08 仍 Unsupported）** | ✅ 改善 |
| **S6** qty126+Exc03 | InvalidRequestData | **Exception 0x03 + requestIssue InvalidQuantity(126)** | ✅ 改善（本任务核心） |
| S7/S8 exc04/06 | Exception+detail | 不变（**Exception 处理早于 T014 已存在**——T004B/T007 数值存储+Baseline 映射） | T014 新增的只是 **ProtocolError deterministic detail**，不是 Exception 覆盖 |
| S9/S10/S12/S13 | CrcError | 不变（CrcError；物因不可证） | 不改善（预期内） |
| S11 timing | InvalidRecord | 不变（≠T015） | 仍不改善 |
| S14 | Timeout | 不变 | 不改善（覆盖充分） |

**明确声明**：T015 后 **不宣称 “demo_v2 全支持”**——S11 时序、S13/9/10 物理根因、业务语义根因仍然不改善；且 demo_v2 as-is 仍因 L15/L36 语法问题整文件不可加载（purpose-built fixtures 才是 T015 验收物）。

## 32. Architecture Decision Gates（Phase B 前需用户 Review）

- **Gate A** — Passive analyzer architecture：推荐 Option B（Core 内 `PassiveTransactionAnalysis`，FC03 复用、generic exception 单点）。
- **Gate B** — TransactionIssue vs RequestIssue：推荐 B（outcome/DiagnosisTransaction 级正交 `TransactionRequestIssue`，T014 契约零改动）。
- **Gate C** — Broadcast outcome semantics：**USER ARCHITECTURE DECISION REQUIRED**（涉及第七状态/Statistics 口径；三案与数学见 §17；配套 ADR-003 Draft）。批准前 Phase B 不实现、不自动实施。
- **Gate D** — FC06 + 0x10 分期：推荐 Phase B 只 FC06，0x10 独立 Part C。
- **Gate E** — request-wire corruption 范围：推荐 Scope A（T015 不吞 wire corruption）。
- **Gate F** — per-record vs whole-batch：推荐 per-record（parser 保持 all-or-nothing，analyzer 不再整批毒死）。
任何 Gate 若影响 TransactionStatus / Statistics 语义 / public data model，一律先批后做。

### Phase A Review 批示结果（2026-09-13，Implementation 准入）

- **Gate A = APPROVED**（Core generic passive analyzer）。**Gate B = APPROVED**（独立 `TransactionRequestIssue`；不得修改 T014 `TransactionIssue` 的语义职责）。**Gate C = APPROVED**（新增 `TransactionStatus::ExpectedNoResponse` —— T015 **有意的** high-level outcome expansion；统计公式以 §17 批准口径为准）。**Gate D = APPROVED**（Phase B = FC06；Part C = Function 0x10 normal semantics，**本 Phase 禁止实现**）。**Gate E = APPROVED Scope A**（bad request CRC / FrameTooShort 本 Phase 保持旧契约，记录 deferred）。**Gate F = APPROVED WITH SCOPE**（valid RTU request 之后的 semantic-invalid / supported / unsupported 全部 per-record 化；parser syntax failure 继续整文件 fail；bad request wire 继续旧行为）。
- **Phase A 文档订正（本 Phase 实施前）**：① demo_v2 mapping 的“S7/S8 = T014 覆盖”改为“Exception 处理早于 T014 已存在（T004B/T007），T014 新增的是 ProtocolError deterministic detail”（§31 已改）；② Broadcast 统计最终采用批准公式（§17 已改）；③ 新增要求：FC06 exact-echo mismatch → 独立 issue `WriteSingleRegisterEchoMismatch`，不得混用 `MalformedNormalResponse`。
- **0x10 边界**：Phase B 只允许 03_MODBUS_LEARNING 知识回填与未来类型/API 规划思考；**禁止** Function16 production decoder / normal matcher / 相关 tests 实现（全部留 T015 Part C）。

## 33. Documentation / Files Changed（本阶段 docs-only）

- 新增：`docs/tasks/T015-passive-replay-expansion.md`（本档案）；`docs/adr/ADR-003-broadcast-outcome-semantics.md`（**Draft/Proposal，非 FINAL**——Gate C 待裁）。
- 更新：`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`（仅登记 T015 IN PROGRESS / Phase A，不写成已实现）。
- 不修改：09_DIAGNOSTIC_COVERAGE_AUDIT（历史结论不动）；src/tests/CMake/scripts/QML = 零修改；`samples/demo_v2.mlog` 只读。

## Phase B Implementation Record（Test First + Implementation 实录）

- **提交结构**：① `c5cfbf7`（docs：Phase A 口径订正 + 官方协议回填 03 §4.5 + ADR-003 获批）→ ② `477ed44`（**RED**：模型表面 + 21 个 passive 测试，13 条断言在旧行为下真实失败）→ ③ `2ba719f`（GREEN core）→ ④ `6944fd5`（GREEN downstream）→ ⑤ 本归档提交。未 amend/squash/push。
- **RED 实证**（提交②后）：`ctest -R passive` → FAILED；`modbuslens_passive_tests.exe` → **`Totals: 8 passed, 13 failed`**，失败全部是断言级——FC06/0x10 时代旧链路对 p02/p03/p04/p05/p06/p07/p09/p10/p12/p13/p14/p15 返回整批 `ReplayExecutionError`（`batch.has_value()` FALSE），p11 缺 `requestIssue`。通过者 = F06 单元解码器 ×4 + p01 FC03 golden 回归锚 + p08 unicast Timeout 锚。
- **GREEN**：core 提交后 p14 一处期望值修正（rateEligible=4−1=3 ⇒ rate 2/3，非 1.0——**测试先写错、以公式为准修正**）；全量 **ctest 24/24**（新增 `passive` 目标 = 第 24 个）。
- **实现映射（全部落在既有分层）**：
  - `src/core/protocol/Function06.{h,cpp}`：request/response 双 decoder（**无 encoder**）+ 广播知识锚点（03 §4.5）。
  - `src/core/protocol/Function03.{h,cpp}`：新增 `readHoldingRegistersRequestQuantity`（**复用/扩展既有解析事实**，不在 Controller/Replay 手拼协议语义）。
  - `src/core/analysis/PassiveTransactionAnalysis.{h,cpp}`：Gate A 唯一 passive analyzer——request 分类只此一处（quantity/length/InvalidBroadcastFunction 优先级）、generic exception matcher 只写一处、FC03 有效请求**逐字复用 T007**、broadcast 判定 = `address==0 ∧ fc==0x06`（不建“写类猜测表”）、unsupported = 显式 per-record 事实（**不伪装成 TransactionStatus**）。
  - `TransactionStatus::ExpectedNoResponse`（Gate C）：七状态（有意扩展，ADR-003）；`TransactionIssue` additive 两枚（`WriteSingleRegisterEchoMismatch` 四寄存器载荷、`UnexpectedResponseForBroadcast`）；`TransactionRequestIssue`（Gate B，独立于 T014 issue，T014 契约零改动）。
  - `ReplayAnalysis`：per-record 化（Gate F）——`transactions`（analyzed）+ `unsupportedRecords`（显式事实）+ 统计仅由 analyzed 子集计算；错误枚举收缩为 `InvalidRequestWire` 一种（Gate E Scope A 保留旧契约）。
  - Statistics：`expectedNoResponseCount` + 批准公式（completed 含 Broadcast；rate = success/(completed−expectedNoResponse)；分母 0 ⇒ nullopt）。
  - Baseline：`ExpectedNoResponseObserved`（Info、无 action、确定性顺序 Pending 之后）+ Healthy 四条件（含 `expectedNoResponseCount==0`）。
  - 下游 additive：Controller `expectedNoResponseCount`/replay notice/`composeIssueText`（response issue + request issue 单行）；模型 `ExpectedNoResponse` 文案 `预期无响应` + `requestIssueDetailText`；Prompt stats `expected_no_response` + `request_issue`/`observed_quantity`/`max_allowed_quantity` + system 语义句族（“不证明写入成功/不得推断程序或操作员错误”）；Agent summary `expected_no_response`、detail `request_issue_code`/载荷/`response_expected=false`、anomalies **排除 ExpectedNoResponse**（whitelist 不变）；QML 新增统计卡与非致命提示条。
- **测试增量**：`test_passive_analysis.cpp`（F06×4 + P01~P15）；STAT-B10（批准公式）；REPLAY-i03b/i03c 重写为 per-record 语义；SERIAL 等既有链路零改动即通过；DIAG-A12/A13；AI-B20；AGENT-A12；UI-T02/T03（含 notice 与 request-issue 行）；UI-R04 按 Phase A 预声明替换为 per-record 期望。
- **边界取证（自动）**：`grep -rn "encodeWriteSingleRegister|Function16|sendWrite|writeRegister(" src` = **零命中**；Serial 仅有 `beginReadHoldingRegisters`/`encodeReadHoldingRegistersRequest`；`src/ui/agent` 无任何 write 工具名。**Passive understanding ≠ Active capability** 由源码事实保证。
- **0x10 边界**：仅 03 知识回填；`Function16`/normal matcher/tests **零实现**（Part C）。

## Final Acceptance（用户 Manual UI Review = PASS，2026-09-13）

- **A. Existing Demo regression PASS**：原四行（Success/Exception/CRC/Timeout）正常；新「预期无响应」统计计数为 **0**；旧布局无回归。
- **B. demo_v1 Replay regression PASS**：Success/Exception/CrcError/Timeout 四行正常；**无 unsupported notice**；Statistics 逐字段不变。
- **C. Broadcast PASS**：FC06 broadcast + NO_RESPONSE → 状态显示「**预期无响应**」；`expectedNoResponse` 计数正确；**success rate 未被广播稀释**；**不显示 Success、不显示 Timeout**；无任何“写入成功”类措辞。
- **D. Unsupported normal function PASS**：unsupported record **不导致 Replay load failure**、**不伪装成 ProtocolError**；UI 有明确非致命提示；未支持记录**不计入 TransactionStatistics**；用户能区分 analyzed vs unsupported。
- **E. Invalid FC03 + legal Exception PASS**：主状态 **Exception**、exception code **0x03**，同时显示 request-side 确定性事实 **quantity=126 / max=125**；两个事实同时保留；无 PLC/operator root-cause 推断。
- **F. FC06 echo mismatch**：用户清单列明该项（ProtocolError + `WriteSingleRegisterEchoMismatch`、expected/actual 可读、未误归 MalformedNormalResponse）；本档案按用户整体 PASS 归档，该确定性行为另由 `PASSIVE-P03` 自动锁定、UI 文案由 `issueDetailText` 提供。
- **G. 整体布局**：无裁剪、无重叠、行高正常、Dashboard 正常、unsupported notice 正常、无可见回归。

**结论**：Phase A = DONE；**Phase B = DONE / REVIEW PASS**；**T015 整体 = IN PROGRESS（Part C 未启动）**。verified LKGC 推进 `cc8393a` → **`02ce302`**（semantic audit 修复后的最终 code/test commit；`6944fd5` 与一切 docs-only 提交均不作为 LKGC）。

### Bookkeeping correction（按 Git 实际输出，替代手工汇总数字）

- Phase B code/test 提交 = `477ed44` / `2ba719f` / `6944fd5` / `02ce302`；`git show --name-only` 并集：
  - **src 生产文件（23 个）**：`src/core/analysis/` 6 个（PassiveTransactionAnalysis.h/.cpp、TransactionAnalysis.h/.cpp、TransactionStatistics.h/.cpp）、`src/core/diagnosis/` 3 个（DiagnosisContext.h、RuleBasedDiagnosis.h/.cpp）、`src/core/protocol/` 4 个（Function03.h/.cpp、Function06.h/.cpp）、`src/core/replay/` 2 个（ReplayAnalysis.h/.cpp）、`src/ui/` 8 个（AnalysisController.h/.cpp、TransactionListModel.h/.cpp、AnalysisController 同目录、agent/AgentTools.h/.cpp、ai/DiagnosisPromptBuilder.cpp、qml/Main.qml）。
  - **构建配置（1 个）**：`CMakeLists.txt` ⇒ **24 tracked production/build paths**。
  - **测试文件（8 个）**：`tests/` 的 test_passive_analysis.cpp、test_replay_analysis.cpp、test_transaction_statistics.cpp、test_diagnosis.cpp、test_ai_client.cpp、test_agent_tools.cpp、test_agent_runtime.cpp、test_ui_bridge.cpp。
- 此前汇报的 “13 production files / 9 tests changed” 属 **component 与 file 计数混用**，作废；本档案与后续文档统一以 **exact changed paths / N 生产组件 + M tracked paths** 口径为准。

### Semantic audit story（完整归档，Review 前专项）

- 规则：generic exception（fn 匹配）= `response.function == (request.function | 0x80)`，且**必须先满足 `(request.function & 0x80) == 0`**。
- 初始实现遗漏前置约束 ⇒ request=0x88 / response=0x88 时 `0x88|0x80 == 0x88` **自我匹配**，1 字节载荷被误判为 **Exception 0x01**。
- **RED**：`PASSIVE-P16` 旧逻辑 FAIL（**21 passed / 1 failed**，误接受 Exception）。
- **Fix**：`PassiveTransactionAnalysis.cpp` §5 增加 request MSB guard；修复后 `0x88/0x88` 落入**已有** `UnsupportedObservedTransaction`（零新状态、零新 issue code），合法 `0x08 → 0x88/0x01` 仍为 **Exception 0x01**（对照断言保留）。
- **GREEN**：passive **22/22**、full ctest **24/24**、clean build **zero warnings**。
- **问题定性（不得写错）**：**generic exception matcher 的 protocol-semantic edge-case bug**——不是 Provider bug、不是 Replay parser bug、不是 Modbus device bug。

## Problems Encountered / Solutions

- **P1（六状态容不下的 broadcast）**：逐一试放 Pending/Timeout/Success 都不诚实（§17 锁定事实），得出“任何诚实方案都必须动状态轴或统计口径”的结论 → 不硬编方案，升为 Gate C + ADR-003 Draft。
- **P2（repo 知识库缺 FC06/0x10 字段布局）**：按纪律不凭记忆写常量 → 标注 External Protocol Reference Required 并把“Phase B 先取证再写常量”立为前置步骤（取证结果回填 03_MODBUS_LEARNING 新条目）。
- **P3（T014 I1 与 Scenario 6 冲突）**：不做“为了新事实推翻旧不变量”或“因为名字叫 TransactionIssue 就硬塞”二选一 → Gate B 推荐独立正交字段（B 案），T014 契约与测试零回归。
- **P4（R04 测试语义将被替换）**：per-record 化会改变 InvalidRequestWire 的 UI 语义 → 提前在档案声明 UI-R04 随 Phase B 重写及理由，避免实现期才暴露。

Phase B：

- **PB1（RED 必须可编译）**：新行为测试引用新类型 → 与 T014 同法：提交②先落“模型表面”（类型/枚举/统计公式/文案 switch），passive analyzer 行为**未接线** → RED 表现 13 条断言失败而非编译错误；用户要求的 5 类行为（FC06 / generic FC08 Exception / S6 invalid+Exception / broadcast ExpectedNoResponse / per-record continuation）全部在旧代码上真实 FAIL。
- **PB2（-Wmissing-field-initializers 第三次来袭）**：`TransactionIssue{.code=…}` 指定初始化在 core 触发告警 → `makeIssue` value-init 工厂 + 载荷逐项赋值（与 T014 同一纪律），零告警收口。
- **PB3（-Wswitch 连锁）**：新增状态/issue code 后，`TransactionListModel`/`DiagnosisPromptBuilder`/`AgentTools` 三处 switch 与 Controller 的 finding/error 两个 switch 必须同步穷举——编译器强制，无静默通道。
- **PB4（聚合初始化复查）**：`DiagnosisTransaction` 增成员 → 11 处聚合站点逐一补 `.requestIssue`（Python 批量 + 手工补嵌套/单行形态）；提交②后 `Function03.h` 缺 `<optional>` 一次编译失败，即时修复。
- **PB5（测试期望与批准公式冲突）**：p14 初稿把 rate 写成 1.0；按 ADR-003 公式（分母=completed−expectedNoResponse=3，success=2）应为 2/3 ——**以公式与实现为准修正测试**，并在档案记录（不掩盖）。
- **PB6（per-record 化对既有 golden 的影响面）**：唯一需要重写的既有 UI 断言是 R04（Phase A 已预声明）；demo_v1 四条与全部 T014/T007/T009 断言自动保持（统计池仅含 analyzed）。
- **PB7（semantic audit：generic exception 的 request-function MSB 边界，用户 Review 前专项）**：核验发现 matcher 缺前置守卫——`response.functionCode == (request.functionCode | 0x80)` 对 `request.functionCode` 已带 0x80 的捕获请求（如 0x88）会**自我匹配**：`0x88|0x80 == 0x88`，1 字节载荷被误判为合法 Exception 0x01。修复=显式守卫 `(request.functionCode & 0x80) == 0` 后才允许 generic exception 判断；修复后 0x88/0x88 落入既有 `response.fc == request.fc` 的 **Unsupported** 路径（不新增状态/issue code，符合“invalid/unsupported/non-exception 路径”要求）。RED 实证：新增 `PASSIVE-P16` 在旧逻辑下 FAIL（21 passed/1 failed，误接受 Exception）；GREEN：passive **22/22**、clean 152 零警告、ctest 24/24。合法路径未受影响（P04 对照断言保留）。**新 LKGC candidate = `02ce302`（`6944fd5` 作废）**。

## Verification（Phase A docs-only + Phase B 全链）

Phase A（docs-only）：`git diff --check`=0；diff 仅 T015 档案 + ADR-003(Draft) + PROJECT_STATUS + BACKLOG；`samples/demo_v2.mlog` 未改未加；src/tests/CMake/scripts/QML 零修改；零构建（无代码改动，无构建必要）；零真实 ModelScope 调用。

Phase B（真实命令与输出）：

- **RED**：`modbuslens_passive_tests.exe` → `Totals: 8 passed, 13 failed`（13 条断言失败，模式见 Phase B Record）。
- **GREEN**：`ctest --preset debug-local` → **`100% tests passed, 0 tests failed out of 24`**（含新增 `passive` 目标）。
- **clean 全量重建**：`--target clean`（Cleaning **152 files**）→ 全量 rebuild **零 warning/error**（grep 计数 0）。
- **QML smoke**：`modbuslens.exe --qml-smoke-test` → exit=0、零输出；ctest #24 qml_smoke Passed。
- **deploy + minimal-PATH**：`scripts/deploy_windows.bat` → `[OK] Deployment directory ready`（dxcompiler 提示为历史已知、非阻塞）；minimal-PATH（仅 System32+deploy 目录）`ModbusLens.exe --qml-smoke-test` → exit=0。
- **边界取证**：§ Phase B Record 的三条 grep（无 FC06 encoder / 无 Function16 / 无 write tool）零命中；`samples/demo_v2.mlog` 未进入任何测试、未跟踪、未修改。
- **Manual UI Smoke = WAITING FOR USER**（政策：Agent 不自报视觉 PASS）。检查清单：① Run Demo 四行与新增「预期无响应」统计卡（值 0）外观正常；② Replay demo_v1 四行与统计不变、无提示条；③ 含广播的 purpose-built 日志 → 行显示「预期无响应」、统计卡计数 1、无成功率（—）；④ 含 FC08 正常响应的日志 → 出现非致命「未支持分析」提示且不进入统计；⑤ 含 quantity=126+Exception 0x03 的日志 → 行为「异常」+ 第二行「请求数量不符合 0x03 约束（126，上限 125）」；⑥ 布局无回归。

## Result

- **Phase A = DONE；Phase B = DONE / REVIEW PASS；T015 整体 = IN PROGRESS（Part C = NOT STARTED）**。
- 交付：七状态（Gate C 批准的 `ExpectedNoResponse`）+ 批准统计公式；Gate A passive analyzer（FC03 复用 / generic exception 单点 / broadcast 仅 FC06）；Gate B 独立 `TransactionRequestIssue`（T014 契约零改动）；Gate D 仅 FC06；Gate E Scope A；Gate F per-record 化 + unsupported 显式披露；Baseline/Agent/Prompt/UI additive 全链；demo_v1/T014/Active Serial 零回归；semantic audit（request MSB guard）修复并留痕。
- **verified LKGC = `02ce302`**（用户 Final Review 推进；`6944fd5` superseded；docs-only 提交不作 LKGC）。

## Knowledge Learned

- active/passive 契约分离是“trusted request”问题的正解：不改 T007，而在其上加 passive 分析面。
- 状态轴满员时的诚实处理：先证明“没有诚实的成员”，再把决策升级为 USER GATE + ADR Draft，而不是硬塞或偷改语义。
- generic exception 识别所需事实极少（地址+fc|0x80+单字节）——协议层“支持诊断异常”与“支持正常语义”是两个可分离的能力。
- 文档知识库的权威边界：库里没有的协议常数，必须标 External Protocol Reference Required，让取证成为实现前置门。

## Potential Interview Questions

1. 为什么 T007 的 trusted-request 契约不用改？——active 模式自己构造请求，契约成立；错配发生在 Replay 套用契约，修复点是 passive 分析面而非 T007（两契约并存，各自正确）。
2. Scenario 6 为什么 status=Exception 而不是 ProtocolError？——响应侧事实（合法匹配的 Modbus Exception）归一为 Exception；请求侧 invalid 是正交事实（requestIssue），不污染状态轴（与 T014 同哲学：两轴分离）。
3. 广播为什么必须 USER DECISION？——六状态没有诚实成员、任何方案都动状态/统计口径；先证明穷尽，再给三案与精确数学，不替用户拍板。
4. generic exception matcher 为什么只需五个事实？——协议结构保证：fn|0x80 + 单字节异常码 + 地址匹配；不需要被拒函数的正常语义。
5. 为什么 FC06 先行、0x10 独立 Part C？——定长 vs 变长（byteCount/quantity 校验）、取证量与 RED 矩阵体量差、演示叙事真实性；“功能更多”不是并包的依据。
6. 广播统计为什么把 Broadcast 排除出成功率分母？——广播既非成功也非失败（协议不期待响应），计入分母会“合法地稀释”成功率、计入分子则需要重定义 Success；因此 completed 含广播、rateEligible 不含（ADR-003 公式，STAT-B10/P14 锁定）。
7. per-record 化如何避免“隐形丢记录”？——`unsupportedRecords` 是显式一等事实 + UI 非致命提示 + 统计只声明“可分析子集”，并有测试证明日志行不会被静默吞掉（Gate F）。
8. FC06 echo mismatch 为什么不能归 MalformedNormalResponse？——帧格式完全合法（长度/字段都对），错的是**语义回显**；把“格式非法”和“语义不匹配”分开，AI/Agent 才不会给出错误解释（Phase A Review 新增要求）。
9. 一次 RED 里 13 条失败说明了什么？——旧链路把“历史事实”当“执行错误”：FC06/generic Exception/invalid request/broadcast 全部整批毒死；RED 精确量化了 active 契约错配的成本，GREEN 后逐条转为 per-record 结果。

## Git Commit

- A（docs）`c5cfbf7` T015: Phase A acceptance corrections and official protocol evidence backfill
- B（RED）`477ed44` T015: add passive expansion model surface and RED tests（passive 8 passed/13 failed）
- C（GREEN core）`2ba719f` T015: implement passive analyzer core (FC06, generic exception, broadcast status)（ctest 24/24）
- D（GREEN downstream）`6944fd5` T015: propagate passive facts to dashboard, prompt and agent tools（+481/−7，11 文件）
- F（semantic audit fix）`02ce302` T015: guard generic exception matcher against request function MSB（+59/−2；passive 22/22、ctest 24/24）——**verified LKGC（用户 Final Review 推进；`6944fd5` superseded）**
- G（docs）`fdefb0e` 记录 semantic audit 并刷新 candidate（不作 LKGC）
- H 本 Final Acceptance 提交（docs-only；哈希见 git log；不作 LKGC）
---

# Part C — Function 0x10 Learning + Test Design（STRICT DOCS-ONLY）

- **状态**（2026-09-14 建稿）：**T015 = IN PROGRESS；Part C = IN PROGRESS（Learning + Test Design，DONE / AWAITING REVIEW）；Function 0x10 Implementation = NOT STARTED。**
- 本部分只设计，零代码/测试/QML 修改；verified LKGC 维持 `02ce302`。

## C0. 冻结基线与证据现状

- HEAD=`4de0237`、verified LKGC=`02ce302`；Phase B 已交付：generic passive analyzer、FC03/FC06 passive 语义、MSB-guarded generic exception、requestIssue（单 issue）、`ExpectedNoResponse`、per-record continuation、Unsupported 显式事实。Active Serial 仍 FC03 read-only。
- 协议依据与现有文档核对：`03_MODBUS_LEARNING.md §4.5` Phase B 回填与官方 V1.1b3 无冲突——request PDU 字段顺序（function=0x10、starting address 2B、quantity 2B、byte count 1B、values N×2B，quantity 1..123、byteCount=2×quantity、actual value payload 与 byteCount 一致）、normal response = (starting address, quantity written) 且**不回显 values**、exception function **0x90**。仅需补记一句“request PDU 字段顺序 + 0x90”（见 §C1 后的 03 增补计划），**不重复堆内容**。

## C1. Official Function 0x10 Contract（本设计的事实底座）

| 项 | 官方事实（V1.1b3） | 备注 |
| --- | --- | --- |
| function | 0x10（decimal 16） | 命名见 §C2 |
| request PDU | startingAddress(2B) + quantity(2B) + byteCount(1B) + values(2×N B) | data 层总长 = 5 + byteCount 字节 |
| quantity 合法域 | **1..123** | 0 与 124 均非法（§C4 边界矩阵） |
| byteCount | **2 × quantity** | quantity=2/byteCount=2 = **semantic mismatch**（不是 CRC、不是 response 错——是 request-side fact） |
| values 长度 | 必须与 byteCount 一致 | 缺（truncated）与多（excess）都成立（§C5） |
| normal response | function=0x10 + startingAddress(2B) + quantityWritten(2B) | 固定 4 字节 data；**不回显 values** |
| exception | request fn | 0x80 ⇒ **0x90** | 走 generic exception matcher，**不写 Function16 私有 exception 解析** |

## C2. Naming Decision（定案）

- C++ 文件/API：**`Function16.{h,cpp}`**（function code 0x10 = decimal 16；与 `Function03`/`Function06` 风格统一）。
- 文档首次出现必须写全称：**Function 0x10 (Write Multiple Registers, decimal 16)**，之后允许 `Function16`；**禁止**使用“FC10”（易误读为十进制 10）。本档案自此处起用 Function16。

## C3. Existing Passive Dispatcher Insertion Point（从最终代码重建）

```text
valid RTU request
  ↓ analyzeObservedTransaction
  ├─ classifyRequest 的 switch —— 【插入点①】新增 case kWriteMultipleRegistersFunction(0x10)：
  │     decode 失败（无法读取基本字段）⇒ requestIssue=InvalidRequestLength
  │     quantity ∉ 1..123 ⇒ requestIssue=InvalidRequestQuantity
  │     byteCount ≠ 2×quantity ⇒ requestIssue=InvalidRequestByteCount（NEW code）
  │     data.size() ≠ 5+byteCount ⇒ requestIssue=InvalidRequestLength（payload 单位见 §C5）
  ├─ broadcast 判定 —— 【插入点②】`address==0 && fc∈{0x06, 0x10}`；且（Gate C7）仅语义有效才谓 broadcast
  ├─ generic exception path —— 【零改动】0x10|0x80==0x90 自动命中；继续继承 request-function MSB guard
  ├─ function-specific normal semantics —— 【插入点③】`request.fc==0x10 && response.fc==0x10` 分支（FC06 分支之后、Unsupported fallback 之前）
  └─ Analyzed / Unsupported
```

**不得设计第二套 Replay 私有 matcher**；Function16 的一切语义只在 `Function16.*`（passive decoder/matcher）与 `PassiveTransactionAnalysis` 的 dispatch 内。

## C4. Request Semantic Layers（不许统称 InvalidRequestData）

| 层 | 事实 | issue 归属 |
| --- | --- | --- |
| A 基本长度/字段可读 | data.size() ≥ 5 才可读出三项字段 | `InvalidRequestLength`（无法读字段） |
| B quantity 范围 | 1..123 | `InvalidRequestQuantity` |
| C byteCount↔quantity | byteCount == 2×quantity | **`InvalidRequestByteCount`（NEW）** |
| D 实际 payload↔byteCount | data.size() == 5 + byteCount | `InvalidRequestLength`（稳定单位，§C5） |

**quantity 边界矩阵**（Phase B 仅测 124 的教训）：0 / 124 / 123 / 1 四点必须全测——0 与 124 非法、1 与 123 合法（F16-U02/U03/U04/U05）。

## C5. Quantity / ByteCount / Payload-Length 设计

- **C3 Gate（minAllowedQuantity）**：当前 `InvalidRequestQuantity` 载荷只有 `observedQuantity/maxAllowedQuantity`。推荐**新增 optional `minAllowedQuantity`**（append-last，additive；FC03 与 Function16 都填：1..125 / 1..123），使“合法区间”可在双边界精确表达，quantity=0 不再被误读为“只超上限”。UI 文案形如“合法范围 1–123”。
- **C4 Gate（InvalidRequestByteCount）**：推荐**新增 code** `InvalidRequestByteCount` + payload `observedByteCount/expectedByteCount`（uint8；expected=2×quantity ≤ 246）。与 `InvalidRequestLength` 严格区分：byteCount **声明值**错误 vs 实际**data 长度**错误。
- **C5 Gate（InvalidRequestLength payload）**：推荐新增 optional `observedLength/expectedLength`，**稳定单位定案 = Function16 request data 字节数（frame.data.size()）**，expected = 5 + byteCount（D 层）或 ≥5 不可计算（A 层，expectedLength 缺省）。不采用“values-only bytes”口径（避免两种单位混用）。FC06 的 InvalidRequestLength 暂不加 payload（Part C 范围纪律，记 future）。
- **excess payload（§12）**：quantity=1/byteCount=2 但带 4 字节 values ⇒ data.size()=7 ≠ 5+2 ⇒ 同样 `InvalidRequestLength`（observed=7/expected=7？——注意 expected 以 declared byteCount 计=5+2=7 → data.size()=7 时 D 层通过但 C 层已失败……重述：excess 场景中的 declared 语义冲突在 C 层先行暴露，D 层仅在 C 通过后比较 declared vs actual）。设计以 §C7 优先级版为准。

## C6. Request Parsing Strategy（Gate C1：structural parser vs strict decoder）

- **推荐：structural parser（B 案，轻量版）**。理由：Passive Replay 必须保留“非法 Request 是现场事实”——strict decoder 会在第一次失败时丢掉 quantity/byteCount/payload facts；Function16 的字段全在固定前 5 字节，**一次 structural read 即可取出全部字段**（无需复杂 parser framework）。形态：`readWriteMultipleRegistersFields(frame) → optional<WriteMultipleRegistersFields{startingAddress, quantity, byteCount, valueCount}>`（data.size()≥5 即可返回），语义校验在 passive analyzer 内逐层产生 requestIssue——与 FC03 的 `readHoldingRegistersRequestQuantity` 同族、比 Function06 的 strict decoder 更进一步（因 0x10 有字节数与 payload 两层）。
- strict 与 structural 并存：valid 请求的 `WriteMultipleRegistersRequest` 完整模型由同一读取路径构造，**一个字段来源、两种消费**。

## C7. Register Values Storage Decision（Gate C8）

- 推荐 **B 案（最小）**：decoder DTO 只保存 `startingAddress/quantity/byteCount/valueCount`——不下发 values。
  - 理由 ①当前无 register-map/business 语义 ⇒ **零消费者**（“错误分类要有消费者”纪律与 T007 `returnedRegisterCount` 先例）；②values 不脱离 `requestWire` 原文（Replay record 保留原始字节），未来 inspection 可再取——信息未永久丢失，只是不进 analysis 结果；③避免“以后可能用”式无限保存。
  - A 案（保存全部 values）否决：本阶段 downstream 几乎不消费具体值，扩容 DTO 无意义；C 案（decoder 内保存但不传播）为中间态，B 已覆盖其收益。
- normal response matcher 需要的只是 startingAddress/quantity 比较（§C9），与 values 无关。

## C8. Normal Response Contract + Response Mismatch（Gate C6）

- **回应契约（与 FC06 exact echo 不同）**：合法 normal response data = 4 字节 `startingAddress(2B) + quantityWritten(2B)`，必须与 request 的对应字段一致；**不回显 values**。
- **新 issue（Gate C6 推荐）**：`WriteMultipleRegistersEchoMismatch`，语义=“response 格式合法、但起始地址或写入数量与请求不匹配”。
- **payload 复用审计（已核验 T014/T015 现有列）**：`expectedRegisterAddress/actualRegisterAddress`（uint16）语义完全契合“起始地址 请求/响应”；`expectedQuantity/actualQuantity`（uint16）契合“数量 请求/响应”。⇒ **零新增字段**，直接复用这两对列；per-code 不变量表扩一行（四载荷全 present）。machine token：`write_multiple_registers_echo_mismatch`。
- **与 MalformedNormalResponse 的分界（§17 红线）**：response data ≠ 4 字节 ⇒ `MalformedNormalResponse`（**format invalid**）；data 4 字节但值不匹配 ⇒ `WriteMultipleRegistersEchoMismatch`（**format valid but mismatched**）。两者绝不可混（Phase A Review 同类教训：FC06 echo mismatch ≠ MalformedNormalResponse）。

## C9. Wrong Function / Address / CRC 复用（零新增 code）

bad response CRC → CrcError；wrong device address → ProtocolError + `ResponseAddressMismatch`；wrong normal response function → ProtocolError + `UnexpectedResponseFunction`；malformed normal response → ProtocolError + `MalformedNormalResponse`。**禁止**臆造 `Function16AddressMismatch/Function16CrcError` 等重复 code。

## C10. Generic Exception Reuse

request 0x10 → response **0x90** 完全由既有 generic exception matcher 命中（`0x10|0x80==0x90`），并继续继承 request-function MSB guard。`Function16.cpp` **只负责 normal semantics**，不得再写 `response.function==0x90` 的私有 exception parser/matcher。

## C11. Invalid Request + Legal Exception（双事实）

| 案例 | 高状态 | requestIssue |
| --- | --- | --- |
| A quantity=124 + Exception 0x03 | Exception(0x03) | InvalidRequestQuantity（observed 124/min 1/max 123） |
| B quantity=2/byteCount=2 + Exception 0x03 | Exception(0x03) | InvalidRequestByteCount（observed 2/expected 4） |
| C quantity=2/byteCount=4 但 payload truncated + Exception 0x03 | Exception(0x03)（†前提：request RTU frame 可 decode） | InvalidRequestLength（observed/expected，单位=request data 字节） |

† C 案严守 Gate E：**只有 valid RTU wire 内的 semantic invalid 才 per-record**；CRC 错/<4B 的 request 仍旧契约（deferred）。**任何案例都不得退回 whole-batch failure。**

## C12. Multi-request-issue Problem（Gate C2，REVISED：保留多个 issues）

**Review correction（2026-09-14，P0）**：否决“单 issue + priority + 丢弃其余已知 issue”。T014/T015 核心原则是 **Core 已经确定知道的 deterministic facts 不应要求下游重新解析 raw wire 才能恢复**——raw wire 是 evidence source，不是 downstream structured fact；Drop 已知 issue = 制造与 T014 同类的 detail loss。Function 0x10 已证明一个 Request 可同时存在多个独立 semantic issues。

**定案（design-only）**：`std::vector<TransactionRequestIssue>`（仓库风格等价的有序 collection）取代单 optional。

- **顺序 deterministic（reporting order，≠ discard priority）**：
  1. structural/readability　2. quantity　3. byteCount↔quantity　4. actual payload↔declared byteCount
- **不得因为前面有 issue 就无条件丢弃后面所有仍可独立证明的 issue。**
- **防 cascade（派生依赖规则）**：
  - quantity invalid → 记录 quantity issue；
  - byteCount vs quantity → **仅当 quantity 合法时**才计算 expectedByteCount（不基于非法 quantity 派生伪 byteCount issue）；
  - actual payload length vs declared byteCount → **只要 byteCount 字段可读取即可独立判断**（比较 data.size() 与 5+declaredByteCount），与 quantity 合法性无关。
- **Multi-issue example（必测）**：`quantity=2, byteCount=2, actual value payload=4 bytes` ⇒ Expected = **[InvalidRequestByteCount(observed 2/expected 4), InvalidRequestLength(observed 9/expected 7)]**——两项均保存、顺序稳定，**绝不只留第一项**。
- 单 issue 的旧下游（outcome/DiagnosisContext/prompt/agent DTO/UI/Phase B 测试）改为 collection 时的触达面列入 §C23 清单。

## C13. Broadcast Integration（REVISED：两个正交维度）

**Review correction（2026-09-14，P0）**：否决“semantic-validity-gated broadcast → invalid request + NO_RESPONSE = Timeout”。重新设计为**两个正交维度**：

- **A. broadcast response expectation**（address==0 ∧ fc ∈ 明确 broadcast-capable set {0x06, 0x10}）
- **B. request semantic validity**（由 `requestIssues` collection 正交表达）

**定案**：

- `address == 0 ∧ function == 0x10` ⇒ **已知 broadcast-capable**。即使 request semantic invalid（如 quantity=124），只要 request RTU wire 仍在 Gate E 支持范围（可 decode），**NO_RESPONSE ⇒ status = `ExpectedNoResponse`，同时 requestIssues = [InvalidRequestQuantity…]** —— **不得 Timeout**（系统本来就不等待 broadcast response）；这**不证明写入成功**。
- **该语义与 Phase B 生产行为一致**（真实代码证据：`PassiveTransactionAnalysis.cpp:116-134`——broadcast 判定与 NoResponse 分支是 expectation-driven，返回 `ExpectedNoResponse` 并携带 requestIssue；invalid FC06 broadcast + NO_RESPONSE 今天即产出 `ExpectedNoResponse + requestIssue`，而非 Timeout。Part C 初稿 §C13 的“Pending/Timeout”推荐是**对既有生产事实的错误描述**，已废除。）
- 合法/非法 Function16 broadcast 观察到**任何** response bytes ⇒ **status = ProtocolError + issue = `UnexpectedResponseForBroadcast`**，**同时保留 requestIssues**；不得让 normal matcher 抢先判 Success；**不得用 `UnknownProtocolError` 掩盖已有确定性 broadcast fact**。
- broadcast-capable set 显式维护：Phase B = {0x06}，Part C 加入 0x10 = {0x06, 0x10}；**不重开** address=0 + FC03 ⇒ `InvalidBroadcastFunction` 的既有边界（Phase B p11 锁定）；禁止“address=0 → 所有 function 自动 ExpectedNoResponse”的过度泛化。

**ExpectedNoResponse 语义澄清 proposal（待 Review；本次不改 Accepted ADR 为 FINAL 新语义）**：
提议将语义表述为“**response expectation / transaction outcome**：观察到请求、未观察到响应、且协议上该请求属不期待响应的 broadcast；request semantic validity 由 requestIssues 正交表达”。当前把 “valid / 合法 broadcast-capable request” 写死的**既有位置清单**（Implementation 经批准后需同步更新）：`TransactionAnalysis.h:22-24`（状态枚举注释）、`docs/adr/ADR-003-broadcast-outcome-semantics.md`（第 44/50 行两处语义句）、`docs/02_ARCHITECTURE.md` D6、`src/ui/ai/DiagnosisPromptBuilder.cpp:205`（system 语义句 “a valid broadcast-capable write request…”）、T015 Phase B Final Acceptance 语义句、`RuleBasedDiagnosis` 注释、`TransactionListModel.cpp` 文案（文案本身无“valid”，仅复核）。

## C14. Unsupported Transition（Before→After 测试）

- 现状（Phase B）：Function16 的 normal 形态记录 → `UnsupportedObservedTransaction`。Part C 实现后**同一 purpose-built record** 应变为 Analyzed——写 transition 断言（Before：unsupported 1 条；After：transactions 1 条）锁定语义迁移。
- **FC08 normal 仍继续 Unsupported**——generic unsupported contract 不因 Function16 加入而破坏（PASSIVE-C15）。

## C15. Parser / Request-wire / Statistics / Baseline / AI / Agent / UI 边界

- `.mlog v1` **零语法变化**：0x10 不需要、也不得新增 `WRITE_MULTIPLE/BROADCAST_NO_RX/FUNCTION16` 等 token；NO_RESPONSE 继续复用，协议语义必须来自 wire。
- request-wire corruption（bad CRC/FrameTooShort）继续 deferred（Gate E），Part C 不顺手扩大。
- Statistics：Function16 的 Success/Exception/CrcError/Timeout/ProtocolError 自然进入现有计数；**零新增** `function16SuccessCount`；broadcast 继续 `ExpectedNoResponseCount`；**successRate 公式不得再改**。回归矩阵证明 status-based 统计与 function code 无关。
- Baseline：不新增 Function16-specific root-cause finding；requestIssue 无专门 finding 的状态保持不变（不强制新增）；禁止输出“PLC 写请求程序有 bug / 写寄存器失败 / 设备寄存器非法”——除非 deterministic facts 真正支持。
- AI：新增 issue 仅以 machine facts 入 prompt（`request_issue=invalid_request_byte_count expected_byte_count=… observed_byte_count=…`；response mismatch 用 `issue=write_multiple_registers_echo_mismatch` + 既有 expected/actual 列）；禁止 raw speculative cause；**不真实调用 ModelScope**。
- Agent：仍 3 tools、whitelist 不变、ExpectedNoResponse 仍非 anomaly；detail 未来照常输出 Function16 的 requestIssue/issue；**不得出现** write_register/retry/send_function16。
- UI：布局不变；Function 列天然显示 `0x10`；requestIssue 沿用 secondary deterministic text（示例：“请求寄存器数量不符合 0x10 约束（124，合法范围 1–123）”、“请求字节数不匹配（实际 2 / 期望 4）”；response mismatch 文案“写多个寄存器响应不匹配（起始地址 请求/响应…；数量 请求/响应…）”）；**不加大列**。Part C Learning 只设计不改 QML。

## C16. Active Serial Safety（红线继续）

**Function16 passive support ≠ Function16 active write support。** Part C Implementation 未来也禁止：encoder / Serial send / write API / QML write button / Agent write tool。`Function16.{h,cpp}` 只含 passive decoder 与 semantic matcher 所需能力。

## C17. demo_v2 S3 Audit（不修改 sample；结论沿用 M8.1 独立 CRC 审计）

| 问 | 结论（证据：M8.1 §6 独立计算 + 本会话 Phase B 代码） |
| --- | --- |
| A 原 sample wire CRC-valid？ | **否**——request provided `62 10`、正确应为 `22 A2`；response provided `41 CD`、正确应为 `40 0D`（两条均 **fixture defect**，如实保留，不偷偷修 evidence 文件） |
| B 若修正 CRC，request semantics 是否合法？ | **是**——fc=0x10、start=0x0010、quantity=0x0002、byteCount=0x04、values `00 01 00 02`（4 字节 ≡ byteCount）全部合规（A~D 层全过） |
| C response 是否符合 normal response contract？ | **是（修正 CRC 后）**——`01 10 00 10 00 02` = start 0x0010 + quantityWritten 0x0002，与 request 一致、不回显 values |
| D Part C 完成后理论结果 | **Success**（unicast、语义与回应全匹配）；demo_v2 as-is 仍因 S4/L15 语法问题整文件不可加载；T015 验收不用 demo_v2 |

## C18. Purpose-built Fixture 计划（CRC 纪律）

最小 Function16 fixture 集（全部由项目 `encodeRtuFrame` 构造 ⇒ CRC 由项目已 KAT 验证的 codec 产生；不手抄未经验证的 CRC 进 golden）：valid unicast Success；invalid quantity(124) + Exception 0x03；byteCount mismatch + Exception 0x03；normal response 起始地址不符；normal response 数量不符；broadcast + NO_RESPONSE；unsupported FC08 normal 对照。测试不得直接依赖 `samples/demo_v2.mlog`（及其余 untracked samples——一律只读）。

## C19. Test Matrices（本阶段只写文档，不写 tests）

**Function16 单元（F16-U 系列）**：U01 valid request decoder（字段全读对）；U02 quantity=1 合法；U03 quantity=123 合法；U04 quantity=0 非法；U05 quantity=124 非法；U06 byteCount==2N 合法；U07 byteCount≠2N → InvalidRequestByteCount（observed/expected）；U08 payload truncated → InvalidRequestLength；U09 payload excess → 按 priority 先出 byteCount（若 C 层已违）或 InvalidRequestLength；U10 normal response decoder（4 字节 data）。

**Passive 集成（PASSIVE-C 系列）**：C01 Function16 normal unicast Success；C02 地址不符 → ResponseAddressMismatch；C03 响应功能码不符 → UnexpectedResponseFunction；C04 bad response CRC → CrcError；C05 malformed normal response → MalformedNormalResponse（≠EchoMismatch）；C06 起始地址不匹配 → `WriteMultipleRegistersEchoMismatch`（四载荷）；C07 写入数量不匹配 → 同上；C08 generic Exception（0x10→0x90）→ Exception；C09 quantity=124 + Exception 0x03 → Exception + requestIssues=[InvalidRequestQuantity]；C10 byteCount mismatch + Exception 0x03 → Exception + requestIssues=[InvalidRequestByteCount]；C11 semantic-invalid 记录不毒死后续记录；C12 valid broadcast + NO_RESPONSE → ExpectedNoResponse；C13 broadcast + response → UnexpectedResponseForBroadcast；C14 invalid broadcast policy（Gate C7 REVISED 口径）；C15 FC08 normal 仍 Unsupported；C16 mixed FC03/FC06/F16 batch statistics；C17 deterministic repeat。

**Review 新增（2026-09-14）**：

- **MULTI-C01（P0）**：`quantity=2, byteCount=2, actual payload=4 bytes` ⇒ requestIssues = **[InvalidRequestByteCount(2/4), InvalidRequestLength(observed 9 / expected 7)]**——两项均存、顺序稳定、不得只留第一项。
- **MULTI-C02（P0）**：`quantity=124, byteCount 与 payload 自洽` ⇒ requestIssues = **[InvalidRequestQuantity]** only——**不得基于非法 quantity 派生伪 byteCount issue**。
- **BCAST-C01（P0）**：Function16 broadcast semantic-invalid + NO_RESPONSE ⇒ **`ExpectedNoResponse` + requestIssues 全保留**（不得 Timeout）。
- **BCAST-C02（P0）**：Function16 broadcast semantic-invalid + response bytes ⇒ **ProtocolError + `UnexpectedResponseForBroadcast` + requestIssues 全保留**（不得 Success、不得以 UnknownProtocolError 掩盖 broadcast fact）。
- valid broadcast tests 全部保留（PASSIVE-C12/C13）。

**Statistics 回归**：FC03 Success + FC06 Success + F16 Success + F16 Exception + F16 ProtocolError + F16 ExpectedNoResponse 混合批——证明 status-based 统计与 function code 无关、successRate 继续排除 ExpectedNoResponse、无 function-specific statistics branch。

**T014/T015 回归锁**：T014 全部 issue 行为不回退；Phase B 的 FC06/generic Exception/MSB guard/requestIssue/ExpectedNoResponse/Unsupported/per-record 全不回退；特别锁定 `0x08→0x88/0x01` 仍 Exception、`0x88→0x88` 仍 Unsupported——**绝不因 Function16 修改而复发**。

## C20. Architecture Gates（Implementation 前必须 Review）

| Gate | 问题 | 推荐（本设计） |
| --- | --- | --- |
| C1 | structural parser vs strict decoder | structural（轻量字段读取 + 分层校验） |
| C2 | requestIssue 单个 vs 多个 | **REVISED：multi-request-issue collection（`std::vector<TransactionRequestIssue>` 有序 collection；ordering=reporting order，≠discard priority；防 cascade 依赖规则；MULTI-C01/02 锁定）** |
| C3 | InvalidRequestQuantity 加 minAllowed | **新增 optional `minAllowedQuantity`**（FC03/Function16 双填） |
| C4 | InvalidRequestByteCount 新增 | 新增（payload observed/expected byteCount） |
| C5 | InvalidRequestLength payload 定义 | 新增 observedLength/expectedLength；**单位=request data 字节**（稳定、单一；不可计算则缺省） |
| C6 | WriteMultipleRegistersEchoMismatch 字段 | 复用既有 `expectedRegisterAddress/actualRegisterAddress` + `expectedQuantity/actualQuantity`，零新列 |
| C7 | invalid broadcast + NO_RESPONSE 的高状态 | **REVISED：正交两维——broadcast response expectation 与 request semantic validity 分离；address==0 ∧ fc∈{0x06,0x10} 即 broadcast-capable ⇒ NO_RESPONSE = `ExpectedNoResponse` + requestIssues（不得 Timeout）；任何 response bytes = UnexpectedResponseForBroadcast + requestIssues（BCAST-C01/02 锁定）** |
| C8 | values 完整保存？ | 不传播 values（DTO 存 starting/quantity/byteCount/valueCount；value bytes 仍是记录内 evidence，不是 downstream structured fact 的替代） |

## C21. Required / Deferred

- **Required for Part C**：Function16 passive normal semantics（decoder + fields reader + 分层校验）、generic exception 复用（含 MSB guard）、normal response matcher、broadcast 复用（**REVISED 正交口径**）、Replay per-record 集成、**multi-request-issue collection（C23 触达面清单）**、新 issue codes（C3/C4/C5/C6 载荷）、tests（含 MULTI-C/BCAST-C）与 downstream collection 传播。
- **Deferred**：active write / encoder / 0x10 Serial 命令 / Agent action、request-wire corruption per-record 化、Replay timing（t1.5/t3.5）、UART、register-map/business 语义、per-device health、其他功能码。

## C22. 文档计划（本阶段）

- `docs/tasks/T015-passive-replay-expansion.md`：本 Part C 段（本提交）。
- `docs/PROJECT_STATUS.md` / `docs/BACKLOG.md`：仅登记 Part C = IN PROGRESS（Learning + Test Design）、不得写成已实现。
- `docs/03_MODBUS_LEARNING.md`：仅补记一条（§4.5 后）：Function 0x10 request PDU 字段顺序 = startingAddress(2B)+quantity(2B)+byteCount(1B)+values；exception function = **0x90**（经 generic matcher）。**不重复堆 Phase B 已回填的契约**。
- `02/04` 本阶段不动（设计待 Review；提案通过后 Implementation 归档再锁 FINAL）。ADR-003 不改（未发现 Broadcast 复用设计缺陷）。
## C23. Part C Architecture Review Corrections（2026-09-14，P0×2，docs-only）

### 1. Context-decay reinspection evidence（真实代码/tests 重新核验，非会话记忆）

- `src/core/analysis/PassiveTransactionAnalysis.cpp:116-134`（现文）：`isBroadcast = address==0 && fc==0x06`；NoResponse 分支对 broadcast 返回 `ExpectedNoResponse` **并携带 requestIssue**；任何 response bytes（覆盖 broadcast）→ `UnexpectedResponseForBroadcast` + requestIssue。⇒ **Phase B 生产本来就是 expectation-driven 正交口径**，本 Part C 初稿 §C13 的“invalid broadcast + NO_RESPONSE = Pending/Timeout”是对既有生产事实的错误描述，已废除。
- `src/core/analysis/TransactionAnalysis.h`（现文）：`TransactionRequestIssueCode` 三值 {InvalidRequestQuantity, InvalidRequestLength, InvalidBroadcastFunction}；`TransactionRequestIssue` 载荷仅 `observedQuantity/maxAllowedQuantity`；单 optional 挂载于 `ReplayTransactionOutcome.requestIssue` / `DiagnosisTransaction.requestIssue`。⇒ collection 化的触达面（Impl 时）：
  - Core：`TransactionRequestIssue`（+minAllowed/byteCount/length 载荷）与挂载点（Outcome/DiagnosisTransaction）改为有序 collection。
  - 消费点：`DiagnosisPromptBuilder::transactionLine`（现单 issue 输出）、`AgentTools` detail DTO（现单 issue + JSON 字段）、`AnalysisController::composeIssueText`（现单 issue 拼接）、Phase B 测试（p05/i03b/p11/a12/AGENT-A12/UI-T03 等现断言单 issue）。
  - 兼容策略候选（Implementation 时再定）：`requestIssue` 更名/加 `requestIssues` 并列过渡，或直接以 collection 替换并同步全部断言——**本阶段只登记触达面，不编码**。
- `docs/adr/ADR-003-broadcast-outcome-semantics.md:44,50`、`src/core/analysis/TransactionAnalysis.h:22-24`、`src/ui/ai/DiagnosisPromptBuilder.cpp:205`：现文把 ExpectedNoResponse 写成 “合法 / valid broadcast-capable request”。⇒ 按 §C13 clarification proposal 处理：**本次不把 Accepted ADR 改成 FINAL 新语义**，仅登记 Implementation 时需同步更新的位置清单（§C13 已列）。
- Phase B 测试现状：p11（addr0+FC03 ⇒ InvalidBroadcastFunction）与 p16（MSB guard）继续作为不回退回归锚。

### 2. Raw wire 边界声明（写入本档案，替代“原文 wire 可弥补”理由）

**Raw wire 是 evidence source，不是 downstream structured fact。** Diagnosis / Prompt / Agent / UI 不得为恢复被丢弃的 issue 而重新解析协议；Passive Core 是唯一 semantic authority。被删除/失效的旧理由（初稿 §C12 “被降权事实仍字面存在于 requestWire 原文”）废止。

### 3. Preflight bookkeeping wording（evidence-based）

前一轮汇报把 `samples/` 新增文件写作“用户放置的 5 个 fixture”——**更正为可证据化表述**：**“5 个额外 untracked files，名称与此前 manual-smoke artifacts 一致；按用户 evidence 对待并保持 untouched。”** Git 无法证明创建者来源，文档与汇报一律不得断言。

### 4. Downstream multi-issue design（只设计）

- DiagnosisContext：保存完整 collection（DiagnosisTransaction 携带全量 ordered issues）。
- AI Prompt：输出**全部** deterministic request issues（按 deterministic order 逐条 `request_issue=` 行）。
- Agent detail：输出数组/等价 structured collection（不得新增 Tool；anomalies 不变）。
- UI：secondary text 按 deterministic order 用「；」连接（或等价最小可读 presentation）；**不得**由 UI 重新决定 issue 优先级、PromptBuilder 不得重解析 wire、AgentTools 不得重判协议。
- 其余 Gates（C1/C3~C6/C8、generic Exception 复用 + MSB guard、parser 不变、request-wire corruption deferred、Active Serial FC03 read-only）维持批准。