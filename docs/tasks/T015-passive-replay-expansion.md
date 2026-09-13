# T015 — Passive Replay Expansion（被动回放诊断扩展）

- **Goal**          解决“哪些**真实历史事务**根本进不了现有分析模型”——把 Replay 从 active-master 的 trusted-request 契约迁移为 passive observer 契约：FC06 / 0x10 被动语义、generic exception 识别、invalid-request 可观察性、broadcast expected-no-response；同时不破坏六状态、Statistics、Active Serial read-only 边界。
- **Background**    M8.1 审计（demo_v2 14 场景）确认当前是 FC03-centric transaction-level analyzer：S2/S3 写功能被 `InvalidRequestFunction` 拒之门外、S5 合法 Exception 01 被请求门挡住、S6 invalid request + 合法 Exception 03 整笔丢弃、S4 broadcast 语义缺失、坏 request 毒死整个 batch。T014 已解决“已接受事务的 **detail** 丢失”（TransactionIssue，六状态不变）；T015 处理的是它们的**上游**：进入模型之前。verified LKGC = `cc8393a`。
- **状态**           **T015 = IN PROGRESS**。**Phase A（Learning + Test Design）= 本档案，STRICT DOCS-ONLY，零代码改动。**
- **Phase 状态**     Phase A = DONE / AWAITING REVIEW；Phase B（Test First + Implementation）= WAITING FOR USER APPROVAL（且 Gate C/Gate A~F 需用户先批）。

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
| S7/S8 exc04/06 | Exception+detail | 不变 | T014 已覆盖 |
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

## 33. Documentation / Files Changed（本阶段 docs-only）

- 新增：`docs/tasks/T015-passive-replay-expansion.md`（本档案）；`docs/adr/ADR-003-broadcast-outcome-semantics.md`（**Draft/Proposal，非 FINAL**——Gate C 待裁）。
- 更新：`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`（仅登记 T015 IN PROGRESS / Phase A，不写成已实现）。
- 不修改：09_DIAGNOSTIC_COVERAGE_AUDIT（历史结论不动）；src/tests/CMake/scripts/QML = 零修改；`samples/demo_v2.mlog` 只读。

## Problems Encountered / Solutions

- **P1（六状态容不下的 broadcast）**：逐一试放 Pending/Timeout/Success 都不诚实（§17 锁定事实），得出“任何诚实方案都必须动状态轴或统计口径”的结论 → 不硬编方案，升为 Gate C + ADR-003 Draft。
- **P2（repo 知识库缺 FC06/0x10 字段布局）**：按纪律不凭记忆写常量 → 标注 External Protocol Reference Required 并把“Phase B 先取证再写常量”立为前置步骤（取证结果回填 03_MODBUS_LEARNING 新条目）。
- **P3（T014 I1 与 Scenario 6 冲突）**：不做“为了新事实推翻旧不变量”或“因为名字叫 TransactionIssue 就硬塞”二选一 → Gate B 推荐独立正交字段（B 案），T014 契约与测试零回归。
- **P4（R04 测试语义将被替换）**：per-record 化会改变 InvalidRequestWire 的 UI 语义 → 提前在档案声明 UI-R04 随 Phase B 重写及理由，避免实现期才暴露。

## Verification（Phase A docs-only）

`git diff --check`=0；diff 仅 T015 档案 + ADR-003(Draft) + PROJECT_STATUS + BACKLOG；`samples/demo_v2.mlog` 未改未加；src/tests/CMake/scripts/QML 零修改；零构建（无代码改动，无构建必要）；零真实 ModelScope 调用。

## Result

T015 = IN PROGRESS；**Phase A = DONE / AWAITING REVIEW**（含 6 个 Gate 与 ADR-003 Draft；Gate C 为用户架构决策点）。verified LKGC 维持 `cc8393a`。

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

## Git Commit

（docs-only；提交哈希与信息见 git log 与 PROJECT_STATUS/BACKLOG 变更记录）