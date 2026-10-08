# 03 — ARCHITECTURE DECISIONS（真实架构决策与演化）

> 用途：面试"为什么这么设计 / 备选是什么 / 代价是什么"的可辩护版本。
> 证据分级：`[V-nnn]` = VERIFIED；`[C-nn]` = CONFLICT；`[U-nn]` = UNVERIFIED。
>
> **重要纪律（本轮强制）**：**不把后期修正写成 Day-1 的完美设计**。
> 每个决策都给出「演化历史」——包括当时的错误假设、被审计推翻的结论、以及同一决策在同一天内的反转。

---

## D1 — Core Zero Qt（`modbuslens_core` 纯 C++20 静态库）

- **Decision**：协议解析、事务分析、统计、诊断规则全部落在 `src/core/`，编译为静态库
  `modbuslens_core`，**不链接任何 Qt**；Qt 类型只允许出现在 App/Adapter 层与 UI 测试。
- **Problem**：三种数据源必须产出**口径一致**的结论；且核心必须能在无显示器环境被完整测试。
- **Alternatives**：把逻辑放在 `QObject` 里（与 UI 同层）；引入线程抽象混在 UI 层。
- **Why chosen**：口径一致性（`docs/00` G1/SC2）＋ 无头可测（NFR-02）＋ 复用（FR-UI-03）。
- **Trade-off**：边界纪律负担（`optional` 在边界要显式拆成 `hasX + value`；不能顺手用 `QString`）。
- **Evolution / 证据强度**：**Day 1 就定为纪律（`docs/02` D3），但不是一开始就有证据来自证**；
  其"可被无 Qt 的 g++ 直接链接"的能力是**在建档时实测过**的（`docs/07` §4.1、`docs/12` §2.1）。`[V-195]`
- **Current result（构建系统级证据）**：
  ```text
  grep -rn "#include <Q\|#include <Qt" src/core/                → 0 命中
  grep -n "target_link_libraries(modbuslens_core" CMakeLists.txt → 无此语句
  ```
  ⇒ 核心的零 Qt 不是注释承诺，而是构建图里的事实。`[V-196]`
- **Evidence**：`docs/02_ARCHITECTURE.md` §2 D3；`CMakeLists.txt:16/304-310`。

---

## D2 — 最终 UI 采用 Qt Quick / QML（ADR001）

- **Decision**：最终 UI 用 Qt 6 + Qt Quick + QML + Qt Quick Controls；**不使用 QWidget 作为最终 UI 方案**。
- **Problem**：T001 为快速验证工具链用了最小 `QMainWindow` scaffold——它只证明"可构建/可测试/可演示"，
  不代表 UI 选型。
- **Alternatives**：QWidget 体系（Qt Widgets）。
- **Why chosen**：开发者已有 QML 能力，显著降低 UI 学习/试错成本；声明式绑定更适合诊断 Dashboard；
  C++ Core 独立于 UI 技术演进。
- **Trade-off**：需要设计清晰的 C++/QML 数据边界（哪些对象暴露、如何避免跨线程绑定陷阱）；
  Transaction/统计需经 `QObject` / `QAbstractListModel` 暴露，多一层适配代码。
- **Evolution**：决策在 **T003 开始前**由用户明确确认，T003 期间**补录** ADR（AGENTS 纪律 7）；
  Widgets scaffold 与最终 QML **并存至 T008**，期间 UI 演示以核心能力为主。`[V-197]`
- **Current result**：M9 UI 现代化后为 6 工作区 + 设计系统；QML 门禁族覆盖 smoke/nav/geometry/focus/业务 oracle。
- **Evidence**：`docs/adr/ADR001-use-qt-quick-qml.md`；`docs/02_ARCHITECTURE.md` 决策索引。

---

## D3 — 三模式共享"核心函数"，而**统一数据源接口（`IFrameSource`）被有意推迟且从未提取**

- **Decision（实际成立的部分）**：Simulator / Replay / Serial 三种数据源复用同一套协议解析、事务分析、
  统计与诊断**核心函数**——它们只是三种字节来源，拿到字节后进同一个核心。
- **Problem**：如果每个模式各自实现一套分析，结论口径必然漂移。
- **Alternatives**：为三模式提取统一接口类型（`IFrameSource`：`open()/start()/stop()/close()` +
  帧回调，产出统一 `RawFrame{timestamp, direction, bytes, sourceName}`）。
- **Why chosen（当时的判断）**：**单数据源阶段不过早设计**——等 Replay/Serial 出现真实共性再提取
  （`docs/02` §4 明确写入"推迟"理由）。
- **Evolution / 诚实结论**：`IFrameSource` 被设计文档列为 "D1 统一数据源抽象：三模式复用的关键"，
  但**代码里从未存在**：`grep -rn "IFrameSource" src/ tests/` → **0 命中**，
  而 `docs/02_ARCHITECTURE.md` 中出现 4 次；`src/io/` 与 `src/app/` 两个规划目录
  **也从未创建**（`find src -type d` 实测只有 `core` / `platform` / `ui`）。`[C-02]`
- **Trade-off（真实代价）**：模式接入没有统一接口约束 ⇒ 新增数据源时要手工对齐调用点；
  反过来说也避免了为不存在的共性付抽象税。
- **Current result（可验证的共享形式）**：`SerialTransactionSession.cpp` 经 `analyzeActiveResponse`
  调用 `analyzeFunction03Transaction`；`ReplayAnalysis.cpp` 调用 `analyzeObservedTransaction`；
  两者与 Simulator 演示批次最终都汇入 `summarizeTransactions`（`src/core/analysis/TransactionStatistics.*`）。
  口径一致性由**可执行断言**锁定：`demo_v1.mlog` Golden Replay 与 Simulator Demo 的统计快照严格相等
  （4/4/0、1/1/1/1/0、0.25、25.0 ms），见 `test_replay_analysis.cpp::i01_goldenReplay`。`[V-198]`
- **Evidence**：`docs/02_ARCHITECTURE.md` §2 D1 / §4；`docs/04_TEST_STRATEGY.md` §3；
  `src/core/replay/ReplayAnalysis.*`、`src/core/serial/SerialTransactionSession.cpp:301-338`。

---

## D4 — Replay 采用批处理 / 分析式回放（而非实时时间轴播放）

- **Decision**：`.mlog` v1 是 **transaction-oriented text log**；Replay 是"历史记录瞬间重新分析"，
  不做时间轴播放、不做调速、不 sleep。
- **Problem**：诊断场景要的是"同一批历史通信得到相同结论"，不是"重演一遍过程"。
- **Alternatives**：实时播放 + 调速 + 时间轴跳转（`docs/01_REQUIREMENTS.md` FR-REP-03 曾如此规划）。
- **Why chosen**：可重复 + 可测试 —— 批式分析让"Replay 与 Simulator 统计严格相等"成为可自动化断言的事实。
- **Trade-off**：无实时播放/调速；`.mlog` v1 语法保持稳定（**无 t1.5/t3.5 时序、无通用抓包导入**）。
- **Evolution**：`FR-REP-03` 的"开始/暂停/调速/时间轴跳转"是**规划条目的残留**，
  与已交付能力不一致——`docs/09` §26 已把它列入 **Claim Risk**（描述的是规划而非已交付）。`[V-199]`
- **Current result**：`parseReplayLog`（纯文本 → `ReplayLog`，八错误码 + 1-based 物理行号）
  → `analyzeReplayLog`（per-record 被动分析，内部含 `summarizeTransactions`）；
  性能口径 ≈1.35~1.4 M records/s（10k~1M 近线性），见 `docs/10`。`[V-200]`
- **Evidence**：`docs/02_ARCHITECTURE.md` §3；`docs/10_REPLAY_PERFORMANCE_BENCHMARK.md`；T009 档案。

---

## D5 — 确定性诊断先于 LLM；**AI is interpreter, not detector**

- **Decision**：`DiagnosisContext` + `RuleBasedDiagnosis` 产生**纯 enum 的**确定性 finding 与建议检查项；
  LLM 只解释既有结构化事实。**核心通信分析功能不得依赖 LLM**（AGENTS 纪律 11 / charter G4）。
- **Problem**：如果让 LLM 参与协议事实判定，幻觉会污染诊断口径；且断网/未配置时产品会残废。
- **Alternatives**：直接让 LLM 做诊断（跳过规则引擎）。
- **Why chosen**：诊断软件的可信度来自"事实与解释分离"；无 Key 时产品仍完整可用。
- **Trade-off**：工程量大（两套层都要做）；LLM 输出质量仍依赖 provider 与小样本（ISSUE-006 家族）。
- **Evolution（关键：不是 Day-1 就完美的）**：
  1. 初版 system instruction 只有**通用否定式**规则（"do not claim root cause"），
     挡不住"往往源于物理层信号干扰"这类**过度归因**——ISSUE-006 用真实 4 笔批次实测暴露；`[V-201]`
  2. 修正方案最初设计为 `total_transactions <= 10 → small_sample=true` 阈值，
     **在批准实现时被用户明确推翻**："count 本身不能论证长期代表性（30/100 笔若仍是一次 session 依旧不可外推）"；
     最终实施为**无条件** `evidence_scope=current_observed_batch` + 短语禁令；`[V-202]`
  3. 教训沉淀为"**否定式规则不足以塑造先验**——必须给出每种状态唯一允许的**正例语义**"。`[V-203]`
- **Current result**：`README.md` 核心原则行："**AI is interpreter, not detector**"；
  三层宪法（Deterministic Core / AI Explanation / Agent Tool Calling）。`[V-204]`
- **Evidence**：`docs/issues/ISSUE-006-ai-explanation-overattribution.md` §5/§6.2/§10；
  `src/ui/ai/DiagnosisPromptBuilder.cpp`；`docs/07_FINAL_PROJECT_REVIEW.md` 第一章。

---

## D6 — Agent 只读：**写能力在类型层面不存在**（ADR002）

- **Decision**：Agent v1 只允许三个白名单工具，全部只读；产品代码中**不存在**任何写操作 tool 的
  类型、名字、schema 或调度分支。
- **Problem**：LLM 首次获得"间接读取产品状态"的能力，越权面增大；工具返回值类型与序列化边界未定；
  多轮异步交互使 T011 的单请求 stale guard 不够用。
- **Alternatives**：动态 tool registry（可运行时注册工具）；约定式"我们不会加写工具"。
- **Why chosen**：D-A1 能力边界 / D-A2 数据保真 / D-A3 时间有效性——三者缺一不可。
  **"写能力在类型层面不存在"** 从"没有写 API"升级为"写能力连名字都不在 contract 里"。
- **Trade-off**：若未来要加第 4 个 tool，必须显式改 contract + schema + dispatcher
  （**这恰是设计意图**）；batch-scoped `transaction_number` 在 batch 切换后失效，**跨 batch 查询天然不支持**。
- **Evolution**：
  1. Part A 期间 Review 修正：「Pending is not an anomaly」——`get_recent_anomalies` 加白名单过滤；`[V-205]`
  2. 同一 ADR 追加三次细化：`transaction_id` → **`transaction_number`**（"不存在真正稳定 ID，措辞不得暗示持久性"）、
     immutable `AgentToolContext` 快照、`recent` 语义锁定 latest-20；`[V-206]`
  3. Phase 1 收口时在**类型层删除** request 级重复 revision（"重复 identity 元数据使不一致态可表达"）；`[V-207]`
  4. ADR 明确定位修正：早期草案 D5（"Agent 通过 HTTP 与只读 Service 通信"）被解释为
     **实际落地形态是本地 Browser 内 tool 调用**，ADR 显式记录了这处修正。`[V-208]`
- **Current result（最终链路）**：QML Question → Controller → active structured batch → `makeAgentToolContext`
  → immutable snapshot → `AgentRuntime` → ModelScope/Qwen native tool_calls → strict C++ validation
  （白名单/参数/预算，**零部分执行**）→ 三只读工具 → `role=tool` → final answer → Controller → PlainText QML。
  i.e. read → reason → explain，无任何写能力。`[V-209]`
- **Evidence**：`docs/adr/ADR002-readonly-tool-agent-architecture.md`；
  `grep -rniE "write_coil|write_register|set_register|device_write" src/ui/agent/` → **0 命中**；`[V-210]`
  `src/ui/agent/AgentTools.h`；AGENT-A01~A13 / AGENT-B01~B23。

---

## D7 — 事务模型：**状态轴与 Issue 轴正交**（后来才形成的第二维）

- **Decision**：`TransactionStatus`（归一化结局）与 `TransactionIssue` / `TransactionRequestIssue`
  （判定时刻的具体违规事实）是**两个正交维度**；issue 不产生统计维度。
- **Problem**：ProtocolError 有五个真实分支（地址/功能码/数量/形状/帧过短），
  全部归一为同一 status ⇒ **信息在归一化瞬间丢失**，Baseline 只能说"协议错误"一句。
- **Alternatives**：①把细节折进状态（把 6 个状态扩成 20 个）；②让 downstream 重新推导原因；
  ③在状态里加"reason"字符串。
- **Why chosen**：状态轴被统计 / UI / Agent 白名单共同消费，扩状态会**连锁破坏所有穷举分支与既有数据**；
  issue 是**旁挂的一维**，老数据集行为逐位不变（additive）。
- **Trade-off**：模型更复杂（枚举码 + 稀疏 optional 载荷 + 稳定机器 token）；下游必须"只读不重判"。
- **Evolution（**典型的两段式演化，绝不可写成 Day-1**）**：
  1. **T007（2026-09-06）**：六状态，`analyzeFunction03Transaction` + statistics；
     当时的定案是「**不新增状态**」；`[V-211]`
  2. **`docs/09` 覆盖审计（2026-09-13）**：逐场景审计后把 "ProtocolError detail preservation"
     列为 **P0** 建议——证据就是"五个真实分支归一丢信息"；此时 `docs/09` §1 仍写着
     "`TransactionStatus` 恰为上述六类，**无第七个状态**"；`[V-212]`
  3. **T014（2026-09-13）**：新增 `TransactionIssue`（响应侧）+ 生产不变量
     `ProtocolError ⇒ issue.has_value()`（**只在 `analyzeFunction03Transaction()` 生产输出中成立**，
     防御分支用 `UnknownProtocolError` sentinel）；RED = `tx 11 passed / 9 failed` → GREEN `20/20`；`[V-213]`
  4. **T015 Part C（2026-09-14）**：新增**请求侧** `TransactionRequestIssue` 有序 collection
     （requestIssues 是 ordered collection，**reporting order ≠ discard priority**，防 cascade 依赖规则冻结）；
     最终表达能力 = 例如 `Success + InvalidRequestByteCount + InvalidRequestLength`、
     `ExpectedNoResponse + InvalidBroadcastFunction`。`[V-214]`
- **Current result**：
  ```text
  TransactionStatus        = 7 个枚举成员
  TransactionIssueCode     = 10 个枚举成员（含 1 个防御哨兵 UnknownProtocolError ⇒ 业务值 9 个）
  TransactionRequestIssueCode = 4 个枚举成员
  ⇒ 枚举口径 14；业务口径 13
  ```
  `[V-215]`
- **Evidence**：`src/core/analysis/TransactionAnalysis.h`（本轮逐行实测枚举体）；
  `docs/04_TEST_STRATEGY.md` §3；T014 / T015 档案；`docs/09` §13.1/§24。

---

## D8 — 广播：新增第七状态 `ExpectedNoResponse` + 成功率排除广播（ADR-003）

- **Decision**：地址 0 的广播请求在无响应时归一为**第七状态** `ExpectedNoResponse`
  （response expectation / observed outcome）；统计公式
  `successRate = success / (completed − expectedNoResponse)`，分母 0 ⇒ `nullopt`。
- **Problem**：六状态是围绕"请求期待响应"的 active-master 世界观建立的。广播**协议上不期待响应**：
  判 `Success` = 假称写入成功；判 `Timeout` = 把合法沉默当故障；判 `Pending` = 语义错误。
- **Alternatives（ADR-003 的穷尽论证）**：
  - **Option A**：新增第七状态。变体 1（`rate = success/completed`，简单但**合法广播会稀释成功率**）vs
    **变体 2（隔离口径 `completed − expectedNoResponse`）**；
  - **Option B**：六状态不动 + 正交 `ResponseExpectation/TransactionKind`——
    **不能独立成立**（broadcast 行仍需一个诚实落点，否则事实上是两套世界），只能作 A 的补充标注；
  - **Option C**：广播独立 passive outcome 不进统计——**长期漂移风险最高**
    （不计入则行计数 ≠ observed，两套世界）。
- **Why chosen**：A + 变体 2（Option B 的 kind 字段作解释性补充为可选）。
- **Trade-off**：不变量 A/B/C 家族重写与全层适配（Statistics / Baseline / Prompt / Agent / UI / 表格）。
- **Evolution（同一天内的三次状态变化）**：
  1. **T015 Phase A（2026-09-13）**：ADR-003 建为 **Draft**，明确说明本决策**显式覆盖**
     T014 的"六状态不动"红线，**而非静默突破**；`[V-216]`
  2. **同日 用户 Gate C 批准 → Accepted（implemented-candidate）**，给出最终公式；`[V-217]`
  3. **同日 T015 Phase B 实现完成**（`IMPLEMENTED / AWAITING REVIEW`），Final Review PASS 后背书；
     `[V-218]`
  4. **T015 Part C（2026-09-14）**：广播集 `{0x06, 0x10}` 且**不与语义有效性 gate**——
     `invalid Function16 broadcast + NO_RESPONSE = ExpectedNoResponse + requestIssues`；
     任何 response bytes ⇒ `UnexpectedResponseForBroadcast + requestIssues`；
     ExpectedNoResponse 的"valid broadcast-capable"旧表述在 `TransactionAnalysis.h` /
     `DiagnosisPromptBuilder` / ADR 三处**同步修正为正交语义**。`[V-219]`
- **Current result**：Evidence 措辞冻结——状态只表达"未观察到响应，且协议上该请求不期待响应"；
  **绝不**渲染为"写入已成功应用到所有从站"，也**不证明设备健康**；
  Baseline 中为 Info（排在 Pending 之后）且**不得仅凭广播宣布 Healthy**；Agent anomaly 白名单不含它。`[V-220]`
- **Evidence**：`docs/adr/ADR-003-broadcast-outcome-semantics.md`；STAT-B10、PASSIVE-P09/P14、
  DIAG-A12/A13、AGENT-A12、UI-T02。

---

## D9 — Per-record 化：**一条坏记录不再毒死整批**；`Unsupported ≠ ProtocolError`

- **Decision**：`.mlog` 解析的**语法失败仍整文件 fail**；但有效 RTU 请求之后的
  semantic-invalid / supported / unsupported **全部按记录保留**。工具不支持的正常语义产出**显式**
  `UnsupportedObservedTransaction`，绝不伪装成 `TransactionStatus`。
- **Problem**：旧实现把"自己构造的请求一定可信"这个**主动模式契约**套在了历史流量上——
  日志里一出现 FC06 / FC16 / 通用异常响应 / 非法请求 / 广播，**整批分析直接失败**。
  而"客户端发错请求 + 设备合法返回 Exception"**正是真实诊断对象**。
- **Alternatives**：保持整批失败；或把 unsupported 静默丢弃；或放松 active 路径的校验来兼容。
- **Why chosen**：被动诊断平台的可信度要求"工具能力边界不得伪装成设备故障"。
  **必须区分两个能力**：A. Passive/Replay 理解历史写事务（不涉及下发）vs B. Active Serial 写能力（产生新写操作）。
- **Trade-off**：`request-wire corruption`（CRC / FrameTooShort）仍属旧失败契约（Gate E Scope A，deferred）；
  Replay v1 无法观察帧内 gap/断帧（即使 bytes 拼起来合法）。
- **Evolution**：
  1. **T009**：Replay 三错误码信任链（任一失败即整条记录/整文件失败）；`[V-221]`
  2. **`docs/09` §19（2026-09-13）**：把 "invalid request 作为可观察历史事实" 列为 **P1**，
     并标注为「**本次最重要的架构审计点之一**」——S6（quantity=126）与 S5（FC=0x08）
     证明合法的设备 Exception 被整笔丢弃；`[V-222]`
  3. **T015 Phase B**：实现 per-record + `unsupportedRecords`；RED = 13 条断言在旧整批失败链路上**真实 FAIL**
     （8 passed / 13 failed）→ GREEN；`[V-223]`
  4. **T015 Part C**：Function 0x10 纳入被动语义；`requestIssues` 升级为有序 collection。`[V-224]`
- **Current result**：统计只描述 analyzed 子集且 unsupported **显式披露**（UI 非致命提示）；
  `p06_invalidRecordDoesNotPoisonBatch` / `c11_invalidDoesNotPoison` 锁定；且**两个桶的行数加起来等于有效记录数**（不静默丢记录）。`[V-225]`
- **Evidence**：`src/core/replay/ReplayAnalysis.h`；T015 档案 §Gate F；`docs/09` §19/§21；PASSIVE-P01~P16。

---

## D10 — Active / Passive 契约分离；主动写 = **用户确认式**（Confirmation vs Dispatch）

- **Decision**：
  1. **Active（Serial/Simulator）**：ModbusLens 自己构造请求 ⇒ T007 的 trusted-request 契约成立且**不改**；
  2. **Passive（Replay）**：分析他人历史流量 ⇒ 新增 `PassiveTransactionAnalysis`，请求自身违规作为事实保留；
  3. **M10 起新增用户确认式主动写路径**（FC06/FC16）：参数校验 + 写前快照 → **确认对话框** →
     用户显式确认后才经同一 Serial 会话派发；
  4. **写能力仅存在于该用户确认路径**；**Agent / 工具层仍无写 API**。
- **Problem**：理解历史写事务（被动）**不等于**获得写权限（主动）——两者必须是两件事。
  一旦产品具备写能力，它就从"旁路观察者"变成"协议参与者"，风险面完全不同。
- **Alternatives**：单 open API（连接即挂起事务）；写操作"点一下就直接下发"；让 Agent 也能写。
- **Why chosen**：连接 ≠ 事务挂起（transport / transaction 生命周期分离）；写操作必须是
  **用户显式动作 + 明确参数 + 明确确认**，失败不得呈现为成功。
- **Trade-off**：状态面稍大（UI-S 矩阵）；写路径不可用时需明确禁用（未连接串口 / 未通过校验 ⇒ 预览与写入按钮保持禁用）。
- **Evolution（**关键：V1 的"无写能力"曾是架构锚，后被 M10 有意取代**）**：
  1. **T015 时点（2026-09-14）**：回归锚 = 源码 grep「**无 FC06 encoder / 无 Function16 / 无 write tool**」；
     `docs/11_PROJECT_FINAL_RETROSPECTIVE.md` §1 当时明写"**不是完整 Modbus Master**：主动路径只有 FC03 只读"；
     `[V-226]`
  2. **M10（2026-09-20→09-23）**：主动写落地 ⇒ **该 grep 锚被取代**；
     取而代之的锚是 **FR-AG-02 的 Agent 工具层约束**（Agent 仍无写 API）；
     `docs/04_TEST_STRATEGY.md` 文首 CURRENT STATE 与 `docs/02_ARCHITECTURE.md` D7 已就地标注这处取代；
     `[V-227]`
  3. **T031-A（2026-10-07）**：`docs/02_ARCHITECTURE.md` D5/D7 精确化，把"产品为用户提供的确认式
     Serial 写路径（用户操作面）"与"Agent 能力"**明确区分**。`[V-228]`
- **Current result**：
  ```text
  prepare / validate（从站与寄存器地址、数量、值域校验）
    → 读/快照当前值回显（写前快照）
    → 确认对话框（目标从站 / 地址 / 数量 / 值清单 / PDU / RTU 帧）
    → 用户点击「确认写入」
    → 经已打开的 Serial 会话派发（一次一笔事务）
  ```
  写前提 = Serial 会话已打开；**确认对话框出现前不产生任何下发**。`[V-229]`
- **Evidence**：`docs/02_ARCHITECTURE.md` D5/D7；`README.md` §6；提交 `db2aea9`（确认与派发分离）、
  `c50dbfe`（证明 rapid Enter 无法逃逸确认）；`test_write_prepare` / `test_write_dispatch` / `test_write_encoder`。

---

## D11 — **PREVIEW == WIRE**（请求预览的单一事实源）

- **Decision**：请求预览字节必须来自 dispatch 使用的**同一个** production encoder（`encodeActiveRequest`）；
  dispatch 对 prepared snapshot **只编码一次**，transport 原样写 `descriptor.wire`。
  控制器 preview 入口（`previewReadRequest` / `previewWrite06Draft` / `previewWrite10Draft` / `previewPreparedWrite`）
  **绝不 dispatch、绝不建 snapshot、绝不改 write store**。
- **Problem**：如果预览单独实现一套编码，UI 显示的字节与实际下发的字节可能不同——
  这在"写寄存器"场景是**安全性缺陷**（用户确认的是 A，实际发的是 B）。
- **Alternatives**：预览与 dispatch 各写一套编码；预览只显示语义字段不显示字节。
- **Why chosen**：用户确认的对象必须**就是**将要下发的字节；拒绝时返回与 prepare 相同的 field/message。
- **Trade-off**：预览入口必须无副作用（不能借 dispatch 的编码路径顺带改状态）。
- **Evolution**：M10-F correction 轮（`cc3c6f8`）与"M10-F correction"同期确立
  （`docs/04_TEST_STRATEGY.md` §3 请求预览 / 事务透明）；此前写区的 PDU/RTU 预览在"尚未 prepare"这一
  **正常初始状态**下会打印 `Unable to assign [undefined] to QString`（map 形状不对称），
  该 pre-existing 缺陷由 ISSUE-016 follow-up 一并修掉。`[V-230]`
- **Current result**：契约测试 = `test_ui_bridge` **pv1–pv4**（预览 == encoder 字节，**逐字节**）。`[V-231]`
- **Evidence**：`docs/04_TEST_STRATEGY.md` §3（请求预览 / 事务透明）；ISSUE-016 follow-up ①。

---

## D12 — 验证门禁的两条铁律：**可观测性** 与 **平台一致性**

- **Decision（教训驱动的两条硬规则）**：
  1. **要断言某类缺陷，必须先保证该缺陷可观测**：WIN32 子系统 exe 的诊断默认写到 OutputDebugString，
     门禁必须显式设 `QT_ASSUME_STDERR_HAS_CONSOLE=1` 才能捕获，并加
     `FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;Unable to assign"`；
  2. **门禁的平台必须与被验收的平台一致**：所有 QML 门禁都用 `offscreen`（headless-safe），
     而 offscreen 行高 12px vs 真实平台 16px —— 逐行累计足以把一个"刚好放得下"的页面推过窗口边缘。
- **Problem（实证）**：ISSUE-016 发现**六个** QML 门禁**全部**含 `referenceerror`——缺陷一直都在，
  只是**不可观测**；ISSUE-018 发现 offscreen 下仍有 41px 余量而真实 windows QPA 下 panel 底边 714 > 700。
- **Alternatives**：只查 exit code；把 CI 全部改成 windows QPA（过度）。
- **Why chosen**：最小且定向——扩面到全部六个 QML 门禁 + 新增**一个**真实平台条目
  （`qml_write_foundation_check_windows`，复用既有 oracle，**不建第二套体系**）。
- **Trade-off**：测试数 36 → 37（如实披露）；该条目**需要交互式 Windows 会话**。
- **Evolution**：断言面从「打包门禁的三个模式」→「全部六个 QML 门禁」（ISSUE-016 follow-up ④）。
- **Current result**：已知缺口如实登记（ISSUE-016 曾因 pre-existing TypeError 只覆盖部分模式；
  ISSUE-017 修复后应扩到全部六个——该扩面已在 follow-up ④ 落地）。`[V-232]`
- **Evidence**：`docs/issues/ISSUE-016-*.md` §E4/§Fix/§Regression Protection；
  `docs/issues/ISSUE-018-*.md` §E2/§Fix/§Regression Protection；`CMakeLists.txt`。

---

## D13 — 发布身份 = **内容哈希 + 净化环境**，不是"文件在不在"

- **Decision**：Windows Human 验收与打包只消费 **candidate tree**（`<build-config>/candidate/ModbusLens/`，
  每次**从零重建**）；打包用逐文件 SHA-256 manifest + fail-fast；启动门用 **sanitized environment**
  （PATH 只含 candidate root 与 Windows 系统目录，清除 `QT_PLUGIN_PATH` / `QT_QPA_PLATFORM_PLUGIN_PATH` /
  `QML_IMPORT_PATH` / `QML2_IMPORT_PATH`）；验收身份 = **exe SHA-256**（ZIP 非 identity criterion）。
- **Problem**：ISSUE-002 证明"exe 能链接通过 ≠ 能启动"——runtime DLL 解析依赖启动环境；
  Raw build output **不是**可部署工件。
- **Alternatives**：直接把 `build/<cfg>/modbuslens.exe` 当交付物；复用历史 deploy/package tree
  （**明令禁止**：`build/**/deploy`、`_superseded*`、历史 portable package 一律禁止 copy）。
- **Why chosen**：部署正确性是**内容身份**问题；手工拼装部署树会破坏
  "部署树 provenance = windeployqt + 编译器 bin 三件套"这一被冻结契约。
- **Trade-off**：流程长（retention → freshness → make_package → identity → portable gates）；
  环境阻塞（ISSUE-015）会直接阻断整条链。
- **Evolution（**多轮收紧**）**：
  1. ISSUE-002（T008.1）确立 minimal-PATH 纪律；`[V-233]`
  2. ISSUE-015 暴露 **freshness 覆盖不足**：`deploy_is_current()` 仅比 exe、不含部署树完整性
     ⇒ 实测触发 `FAIL: required package file missing: platforms/qwindows.dll`；
     并暴露**缺失前置探测**（无 `windeployqt --version` 自检）；`[V-234]`
  3. "candidate 不可重生成"暴露 **fixture 未覆盖真实候选内容** + **worktree 不携带 git-ignored distfiles**；
     教训：**fixture 必须来自真实制品；"ignored ≠ 无关"**；`[V-235]`
  4. M12 起 candidate 架构写入 AGENTS.md 作为**长期强制规则**（Repository 最高规约层）。`[V-236]`
- **Current result**：v2.0.0 公开发布 + **R5G 独立验证**（公开下载 35,814,735 B 并算 SHA-256，
  **不解压、不重打包**）；H1–H8 人工验收 PASS。`[V-237]`
- **Evidence**：AGENTS.md「Windows Deployment / Candidate Architecture」；`docs/ENVIRONMENT.md` §4b；
  `cmake/modbuslens_generate_candidate.cmake`；`scripts/make_package.py`；ISSUE-002 / ISSUE-015。

---

## D14 — UI 语言政策：简中为主 + 专业实体保留英文 + **判定永不依赖文案**

- **Decision**：全部用户可见文案简体中文；协议/工业标准术语与实体保留英文原文；
  Core **只产生 enum / 结构化事实，绝不出用户文案**；所有中文文案在 UI/App 适配层产生；
  任何控制流判断必须用 enum/结构，**严禁 `if (statusText == "超时")`**。
- **Problem**：目标用户为中文工业调试人员；同时要避免"展示文案变化导致业务语义与测试漂移"。
- **Alternatives**：Qt Linguist / `.ts` / `.qm` / 运行时语言选择器。
- **Why chosen**：产品**无多语言需求**（目标用户单语种），Linguist 属过度设计，增加构建与部署面；
  单语言直写 `qsTr("中文")` 保留未来迁移余地（文案已集中在 QML 与适配层，届时无需触碰 Core）。
- **Trade-off**：无多语扩展；若未来需要第二种语言，**先立 ADR 再迁移**。
- **Evolution / 真实教训**：
  1. Localization Pass（`9e79558`，2026-09-09）一次性中文化；`[V-238]`
  2. **中文严禁经 `const char*` + `QLatin1String` 转换**——UTF-8 字节会按 Latin-1 逐字节映射产生乱码；
     该 pass 已把 replay 错误短语从该组合**结构性修正**（中文一律 `QStringLiteral`）；`[V-239]`
  3. 测试更新原则：**仅 presentation 断言随文案同步更新，语义不变**；Core 测试从不因文案改动而变；
     `[V-240]`
  4. AI 输出语言政策：发给 LLM 的 user prompt 保持英文结构化标识（**它是机器通道，不是用户可见 UI**，
     不参与本地化）；system prompt 要求简中输出 + 术语保留 + **PlainText only**，
     且原有英文 authority 规则**一字未改**。`[V-241]`
- **Current result**：`docs/06_UI_LANGUAGE_POLICY.md` 生效；AI 解释一律 `Text.PlainText`
  （**不启用 RichText/Markdown/HTML 渲染**，PlainText 安全边界保持）。`[V-242]`
- **Evidence**：`docs/06_UI_LANGUAGE_POLICY.md` §2/§5/§6/§7/§8。

---

## D15 — 版本权威单一来源

- **Decision**：**CMake `project(ModbusLens VERSION 2.0.0)` 是唯一公开产品版本权威**；
  应用与 Windows VERSIONINFO（PE 身份）同源派生；打包 stem 由 Release 构建的 version authority 自动派生，**勿手改名称**。
- **Problem**：应用内版本、标签/Release、打包文件名三方不一致是发布事故常见形态。
- **Alternatives**：各处手写版本号；版本号只在打包脚本里。
- **Why chosen**：单一来源 + 校验跟随之（`641b1db` "make version validation follow CMake authority"）。
- **Trade-off**：版本号改动必须经 CMake 单一入口。
- **Evolution**：M9-E 期间先记录 **2.0.0 产品版本决策**（`6e248b0`），随后
  `0463207` 统一应用与 Windows 版本身份，`641b1db` 使版本校验跟随 CMake 权威——**三步而非一步到位**。`[V-243]`
- **Current result**：T028 §4 C7 审计结论：`CMake 2.0.0 = tag v2.0.0 = Release 页` → **无冲突**。`[V-244]`
- **Evidence**：`CMakeLists.txt:3-4`、`:177-180`；`docs/tasks/T020`（M9-E E1）；`docs/tasks/T028` §4。

---

## D16 — 档案不可变 + LKGC 单步推进

- **Decision**：`docs/tasks/`、`docs/issues/`、`docs/adr/`、`docs/devlog/` 是**只增不改**的档案区；
  修正事实用**追加批注**，不覆盖原文。**HEAD ≠ verified LKGC**；**docs-only commit 永不作 LKGC**；
  存在自动测试覆盖不了的 UI/hardware/live 交互时，**人工验收前不得推进 LKGC**。
- **Problem**：面试与移交的可信度取决于"当时的判断是否被如实保留"；覆盖历史会让失败教训消失。
- **Alternatives**：清理式文档（把旧结论直接改对）。
- **Why chosen**：历史与现状分离归档 ⇒ 能区分"当时为真"与"现在为真"。
- **Trade-off**：文档里会长期并存互相过时的表述（**必须靠 superseding note 维护，否则产生 Claim Risk**）——
  T028/T031 的 GAP-03/04/10 就是该代价的真实体现。
- **Evolution**：
  1. Git Policy：只允许 amend 当前任务**最新、尚未 push** 的提交；**禁止 rewrite 更早历史**；`[V-245]`
  2. M10-E5 曾出现**过早收口**（`2bf049b`）→ 同任务**撤回**（`a29e508` "retract the premature closure"）
     → 重新执行 canonical package 并验证（`d8d0ff2`）——**档案保留了这处自我纠正**；`[V-246]`
  3. M10-F 因环境阻塞置 HOLD 后，**未**用陈旧/伪造产物冒充 fresh，而是如实挂起（ISSUE-015）；`[V-247]`
  4. T031-A 为 `docs/07` / `docs/11` 加**历史快照声明**（不改正文）。`[V-248]`
- **Current result**：498 个提交、30 份任务档案、20 份 Issue、3 份 ADR、逐日 devlog 全程留档。
- **Evidence**：AGENTS.md（工作纪律 8、Git Policy、Cross-Agent Anti-Drift §8）；`docs/11_V2_UPGRADE_PLAN.md` §LKGC 纪律。

---

## D17 — QML 模块单例改为 **engine root context property**（ISSUE-010 之后的务实选择）

- **Decision**：设计系统 `DS` 不用 QML module singleton，而用
  `rootContext()->setContextProperty("DS", ds)` 注入一个普通 `QtObject`。
- **Problem**：`pragma Singleton` + `QT_QML_SINGLETON_TYPE TRUE` + qmldir `singleton` 行**三者齐备**，
  `QML_IMPORT_TRACE` 证明类型层已按 SINGLETON 解析——但运行时每一处 `DS.*` 仍抛
  `ReferenceError: DS is not defined`（约 80 条），窗口仍能创建（视觉为全空 token 值）。
- **Alternatives**：继续深挖 Qt 内部实现（受里程碑边界约束未做）；退化为每个组件重复样式（被否）。
- **Why chosen**：engine root context property 是 **Qt 官方同样认可的稳定形态**；
  token 值一字不改（`DesignSystem.qml` 仅移除 `pragma Singleton`）。
- **Trade-off**：失去 QML 类型系统的静态解析（`DS` 变成 context property）；
  它是**单例**，与类型化 import 相比可发现性略低。
- **Evolution / 诚实声明（关键）**：
  1. 诊断过程本身有两次误导：Git Bash 下 exit 127 是 **MSYS 把 Windows 负退出码 -1 映射的结果**，
     掩盖了真实信号；需用 PowerShell `Start-Process -PassThru` 拿原始 `ExitCode=-1` +
     `QT_ASSUME_STDERR_HAS_CONSOLE=1` 才拿到真实错误文本；`[V-249]`
  2. 排除 qmlcache 陈旧假设（强制全量重生成后错误不变）；`[V-250]`
  3. **机理级根因未完全隔离**——指向 exe-attached 内嵌 qrc 模块 + composite singleton + AOT qml cache
     组合下的问题族（社区同族报告），**受里程碑边界约束未继续深挖 Qt internals**；
     Issue 明确记录"未隔离"而不编造根因。`[V-251]`
- **Current result**：Debug exe smoke `EXITCODE=0`、0 条 ReferenceError；ctest 24/24；deploy 候选 smoke `EXITCODE=0`。
- **Evidence**：`docs/issues/ISSUE-010-qml-module-singleton-runtime-unresolved.md`；`src/main.cpp`；`CMakeLists.txt`。

---

## D18 — 持久化在 Adapter 层（Qt JSON），Core 保持零 Qt

- **Decision**：设备档案 / 手册库的 JSON 持久化由 `src/ui/profile/ProfileStore.cpp` 承担
  （`QFile` / `QDir` / `QJsonDocument` / `QSaveFile` / `QStandardPaths` / `QCryptographicHash`）；
  **Core 的领域模型保持零 Qt**。
- **Problem**：领域模型需要被持久化，但不能因此把 Qt 拖进 `modbuslens_core`（破坏 D1）。
- **Alternatives**：`std::filesystem` + 手写文本序列化（**未采用**，src/tests 命中 0）；
  SQLite（`docs/INTERVIEW_NOTES` F9 反思"应更早做持久化层设计"）。
- **Why chosen**：领域模型（纯 C++）与 I/O（Qt）分层，layer 边界与 D1 一致；
  `QSaveFile` 提供原子写 + 路径安全硬化。
- **Trade-off**：**ManualStore 现为文件型 JSON**（无事务、无并发控制）；
  捆绑了 `Reader/Writer` 语义到 Qt 层。
- **Evolution**：M12-A first slice 先冻结 **JSON v1 shape**（T027 §25）+ **validation rules v1**（§26）
  + 路径安全硬化（§29.2），**再**实现——不是先写代码后补契约。`[V-252]`
- **Current result**：`src/core/profile/DeviceProfile.h` 只 `#include <cstdint> <string> <string_view> <vector>`
  （本轮实测）；schema version = 1。
- **Evidence**：`src/ui/profile/ProfileStore.cpp` 头部 include 清单；`src/core/profile/DeviceProfile.h`；
  `docs/tasks/T027` §25/§26/§29.2。

---

## D19 — Raw 与 Derived Presentation **分离**（M11 的核心原则）

- **Decision**：`raw deterministic register data`（原始十六进制 / 原始十进制，**恒显**）与
  `device-specific presentation`（解析结果，随配置变化）是**两个分离的呈现层**；
  解码**只读** raw、绝不回写。
- **Problem**：如果解码结果覆盖原值，一旦字节序/字序配置错误，**用户就再也看不到真实字节**——
  这在诊断场景是致命的信息损失。
- **Alternatives**：解码结果就地替换原值（更"干净"但不可回溯）。
- **Why chosen**：必须区分 **raw deterministic register data** 与 **device-specific physical semantics**
  （`docs/11_V2_UPGRADE_PLAN.md` §4 M11 定义）；解码资格按"成功 register-read-compatible 事务"判定，
  FC03/FC04/custom **一视同仁**（非 0x03 硬编码）。
- **Trade-off**：UI 多一列；配置错误时用户仍需自行比对。
- **Evolution（**provenance 的真实形态**）**：
  1. **T025 first slice**：Hex/Binary/UInt16/Int16 + 寄存器内字节序；`register_decode 19` / `ui_bridge 88`；`[V-253]`
  2. **T026 second slice**：加 UInt32/Int32/Float32 + word order（HighWordFirst AB CD 默认 / LowWordFirst CD AB）
     + **滑动窗口 alignment**（行 i 解码 `words[i..i+1]`，末寄存器 `insufficient_words`，成功行带 `decodeSpan`）；`[V-254]`
  3. **alignment 的 provenance 是三层**：**实现时决策 → `2f767d1` 成文 → 审计后由 Human 追认为 canonical（T024 §27）**——
     **不是 pre-frozen 设计**；档案明确如此记录。`[V-255]`
- **Current result**：raw DEC/HEX 恒不变由**负向对照**证明（raw 改写 3 条断言 FAIL，恢复后复绿）；
  Float32 特殊值（NaN/±Inf）文案按 T027 §22 C3 **冻结**；明确**不做** scaling/offset/units/40001/Float64/String/register map（**M11 边界**，这些属 M12）。`[V-256]`
- **Evidence**：`docs/tasks/T024` §9/§22/§27；`docs/BACKLOG.md` T025/T026 行；`src/core/analysis/RegisterDecode.h`。

---

## D20 — LLM **只能做选择，不能做权威数据**（app-resolved citations）

- **Decision**：手册问答的引用由 **app 侧**解析——provider 只能在 app 提供的上下文块 **id** 中"选择"；
  `offset` / `hash` 由 app 侧回填；**未知 id fail-closed**。
- **Problem**：让 provider 自己产出 citation offset **不可靠**——M12-D R2E 本地 round-trip 校验直接失败。
- **Alternatives**：信任 provider 输出的 offset/hash 并展示（会误导用户到错误位置）。
- **Why chosen**：这是 D5（AI 不产生协议事实）在**文档智能域**的同构延伸：
  LLM 输出是不可信输入，"选择可以，权威数据不行"。
- **Trade-off**：模型不能引用上下文之外的任何位置（**故意的能力收缩**）；
  需要 app 侧维护 citationId 与 offset/hash 的映射。
- **Evolution**：
  1. M12-D 契约先冻结**六字段 citation** + 本地验证（**D2 决策**）；`[V-257]`
  2. 真实 Live 使用暴露 round-trip 失败（R2E）⇒ 改为 app-resolved id；`[V-258]`
  3. 同期 M12-D R2B 的"无法解析"双根因（抽取层 + **thinking 模式未关**）一并修复，
     补 `enable_thinking=false` 显式契约 + `qa36` 契约测试 + 15 类解析失败可观测。`[V-259]`
- **Current result**：引用答案必须附带**可本地校验**的引用块（`README.md` §2）；
  `qa36` 锁定结构化输出契约。
- **Evidence**：`docs/tasks/T027` §119.6 item 6/7；`docs/INTERVIEW_NOTES.md` F5/F7；`README.md` §2。

---

## 附 — 决策索引与 ADR 覆盖度

| ADR | 决策 | 状态 | 备注 |
| --- | --- | --- | --- |
| `ADR001-use-qt-quick-qml.md` | 最终 UI = Qt Quick/QML | Accepted（2026-09-05，T003 期间补录） | 本文 D2 | `[V-260]` |
| `ADR002-readonly-tool-agent-architecture.md` | Agent 只读工具架构（D-A1/D-A2/D-A3） | Accepted（2026-09-09） | 本文 D6 | `[V-261]` |
| `ADR-003-broadcast-outcome-semantics.md` | 广播 Outcome 语义（第七状态 + 统计公式） | Accepted / Implemented（2026-09-13） | 本文 D8 | `[V-262]` |

**未立 ADR 但属重大架构决策**：D1（Core Zero Qt）、D3（不提取 `IFrameSource`）、D7（状态/Issue 正交）、
D9（per-record 化）、D10（确认式写路径）、D11（PREVIEW == WIRE）、D12（门禁两铁律）、
D13（发布身份）、D16（档案不可变 / LKGC）、D18（持久化分层）、D19（raw/derived 分离）、
D20（app-resolved citations）。

> **架构文档自身的规则**：`docs/02_ARCHITECTURE.md` §6 写明"任何对 D1–D7 的变动 → 新建 ADR，
> **禁止静默改架构**"。按该规则，D3（`IFrameSource` 未提取）与 D10（M10 主动写路径）在
> `docs/02` 中是以**就地更新 + 引用取代关系**的方式维护的，而非各立一份 ADR——
> 这是可讨论的治理松紧度，属事实记录，不在此评判。`[U-02]`