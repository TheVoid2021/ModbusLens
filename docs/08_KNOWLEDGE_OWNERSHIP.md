# 08 — Knowledge Ownership（架构知识所有权）

> 定位：M8 Phase B1。把最终 ModbusLens 架构转换成"开发者本人必须能解释"的知识地图（Part 1 — System Architecture）。不是背诵稿、不是 README；每条结论都与 `docs/07_FINAL_PROJECT_REVIEW.md` 和真实代码互核。

---

## 1. One-Sentence Project Explanation（三档）

**A. 15 秒版**
ModbusLens 是一个 C++20 + Qt Quick/QML 写的 Modbus RTU 诊断工具：把 Simulator / Replay / Serial 三种来源统一成同一条确定性分析链（CRC→事务→统计→诊断），再用可选的 AI 解释与只读 Agent 做自然语言输出——但 AI/Agent 从来不是 Modbus 事实的来源。

**B. 45 秒版**
现场 Modbus RTU 出了 CRC 错误、超时或 0x02 异常时，靠原始字节很难解释"发生了什么"。ModbusLens 把三种运行模式（内置模拟器、.mlog 历史回放、真实串口）都接到同一个 Zero-Qt 的确定性核心上：CRC 校验、帧解码、FC03 语义、请求-响应配对、超时判定、统计、基线诊断全部由 C++ 核心决定。之上是两层可选增强：AI 解释（一键把已确定的事实转成自然语言）和只读 Agent（用 Qwen 的 native tool calling 调三个只读工具按需取数回答）。没有 API Key、断网、额度不足时，核心诊断完全照常工作。

**C. 2 分钟版**
（见 07 第一章 + 本文 §2~§5：先说确定性链，再说 AI/Agent 是解释层，最后说权限边界——"写能力在类型层面不存在，模型连一个写工具的名字都拿不到"。）

---

## 2. Final Architecture（白板级 ASCII，依赖方向向下）

```text
┌─────────────────────────────────────────────────────────────┐
│ Presentation            Main.qml（Qt Quick/QML + Fusion）      │
│                         统计卡｜Diagnosis 三 Tab｜Transactions   │
└─────────────▲───────────────────────────────────────────────┘
              │ Q_PROPERTY / QAbstractListModel / signals
┌─────────────┴───────────────────────────────────────────────┐
│ App / Adapter   AnalysisController · TransactionListModel     │
│                 SerialPortAdapter（薄 Qt 适配）                │
│                 ModelScopeDiagnosisClient（one-shot AI）      │
│                 AgentRuntime · ModelScopeAgentClient          │
│                 AgentTools dispatcher · AgentPromptBuilder    │
└─────────────▲───────────────────────────────────────────────┘
              │ modbuslens_core（纯 C++20，Zero Qt）
┌─────────────┴───────────────────────────────────────────────┐
│ Deterministic Core                                           │
│  protocol: ModbusCrc · ModbusRtuFrame · ModbusRtuCodec       │
│            · Function03（FC03 decoder）                       │
│  simulator: SimulatedSlave · SimulationFault                 │
│  analysis:  TransactionAnalysis · TransactionStatistics      │
│  replay:    ReplayLog · ReplayAnalysis                       │
│  serial:    SerialTransactionSession（Zero Qt 状态机）        │
│  diagnosis: DiagnosisContext · RuleBasedDiagnosis            │
└─────────────▲───────────────▲───────────────▲───────────────┘
              │               │               │
        Simulator 数据    Replay .mlog      Serial bytes
        （内存端点+注入） （解析+批量分析）  （QSerialPort → Adapter → Session）
```

**依赖方向与禁令**
- `modbuslens_core → Qt` 反向依赖：**禁止**（构建系统级隔离：core 静态库不链 Qt）。
- QML 不直接操作 `QSerialPort`、`ModbusCrc/ModbusRtuCodec`、`AgentRuntime`——一切经 Controller 与 Model。
- AI / Agent = Integration，位于 App 层之上、只消费 Core 的确定性产物。

---

## 3. 三条主数据链（真实类/函数名）

**A. Simulator（Run Demo）**
`AnalysisController::runDemoBatch()` → `SimulatedSlave.handleRequest(makeFc03Read(...))` → DEMO-3/4 经 `SimulationFault`（CorruptCrc / DropResponse）构造 wire → `decodeRtuFrame` / `ResponseObservation` → `analyzeFunction03Transaction(request, observation, elapsed, threshold)` × 4 → `TransactionListModel::setEntries` + `summarizeTransactions` → `applySnapshot` + `emit statisticsChanged` → QML 统计卡 / 交易列表。

**B. Replay（Load Replay）**
`AnalysisController::loadReplayFile(QUrl)` → `QFile::readAll` → `parseReplayLog(text)`（纯文本语法层→`ReplayLog`；坏 header/hex/字段 → `ReplayParseError{code,line}`）→ `analyzeReplayLog(log)`（逐条→ FC03/RTU 分析→`ReplayBatchAnalysis{transactions,statistics}`；坏 request 三错误码→`ReplayExecutionError`）→ `setEntries`（outcome→`TransactionListEntry`）+ `activeDiagnosisTransactions_` + `statistics_ = batch.statistics` → emit → QML。失败路径：仅 `setReplayError`，旧 batch/mode/source 原样（原子切换）。

**C. Serial（Connect + Read Once）**
`AnalysisController::connectSerial(portName, baud)` → `SerialTransactionAdapter::openPort`（`QSerialPort` 8N1 固定+用户波特率）→ 成功才清批切 source → `readHoldingRegistersOnce(slave,start,qty,timeout)` → `SerialTransactionAdapter::startTransaction` → `encodeReadHoldingRegistersRequest` + `port_.write` → `readyRead` → `feedResponseBytes`（`SerialTransactionSession`：framing→收齐→`TransactionAnalysis`，partial→CrcError 而非 Timeout）→ `transactionCompleted` → `publishSerialResult`（单行 replace + 单元素统计）→ UI。Transport error（PE-4 有界报告）≠ Modbus 诊断。

---

## 4. Diagnosis 数据链

```text
activeDiagnosisTransactions_（Controller 持有，与 rows/statistics 同源）
   → buildDiagnosisContext（自洽快照：statistics=summarizeTransactions(同一批事务)）
   → diagnoseTransactions（RuleBasedDiagnosis：finding code/severity/actions，绝不判 wire）
        ├── Baseline（确定性文本 formatter）→ QML「基线诊断」Tab
        └── 同一 context+report → buildDiagnosisPrompt → ModelScope -> 解释文本 → QML「AI 解释」Tab
```

- **AI failure 为什么不能清掉 Baseline**：两者是独立产物——Baseline 由 Core 规则引擎同步生成，AI 解释是其后的一跳网络调用；`handleAiFailed` 只写 `aiDiagnosisErrorMessage_`，不触碰 `hasBaselineDiagnosis_`/`statistics_`/rows。
- **AI 返回错误事实为什么不能改 Dashboard**：Dashboard 事实全部来自 Core 快照；`aiDiagnosisText_` 只是展示字符串（Text.PlainText），AI-B12/UI-AG15 用测试锁死"AI 内容再怎么错，statistics/rows/baseline 一字不变"。

---

## 5. Agent 数据链（标注所有 guard 点）

```text
askAgent(question)                        ← 前置：AI busy?/自 busy?/空批?/未配置?（零网络拒绝）
  → copy activeDiagnosisTransactions_
  → makeAgentToolContext(copy, activeBatchRevision_)     ★ immutable snapshot
  → AgentRunRequest{question, context, ++agentRequestGeneration_}
  → AgentRuntime::start
       ├─ preflight: context.capturedBatchRevision != currentBatchRevision_ → 静默 no-op（ST-A）
       ├─ round 1 request (system+user+tools[3])
       ├─ handleRoundSucceeded:
       │    ★ stale check(captured==current && gen==currentAgentGeneration)
       │    tool_calls? → toolRounds++ / totalToolCalls+=N
       │    ★ budget: rounds>3 → ToolRoundLimitExceeded；total>6 → ToolCallLimitExceeded（整批零执行）
       │    ★ 顺序：parse 全部 → 结构验证 → 白名单映射 → validate-only 全部 → 全部通过才 dispatch
       │    dispatchAgentTool(context_, name, args) → 三只读工具（typed JSON DTO）
       │    → role=tool 逐条(原 tool_call_id) → 下一轮
       │    无 tool_calls: content 必须为 string 且非空白 → 否则 provider InvalidResponse
       └─ final: runCompleted(gen, answer) → Controller: hasAgentAnswer/answerText + busy=false
```

- **为什么不允许 partial execution**：同一 assistant 批内先整体验证再执行——只读工具虽无副作用，但"半执行=半回答"会让工具结果与轮次账错乱，不利于将来任何带状态工具扩展（transaction-like semantics）。
- batch invalidation（活动批次变换）：`++revision → setCurrentBatchRevision(new) → AgentRuntime::invalidateForBatchChange()`（静默终止，无用户可见信号）。

---

## 6. Ownership Matrix

| Fact / Decision | Owner | 禁止越界 |
| --- | --- | --- |
| CRC correctness | Core ModbusCrc | QML/AI/Agent |
| RTU frame validity | Core ModbusRtuCodec | 同上 |
| FC03 semantic validity | Core Function03 | 同上 |
| Request/Response matching | Core TransactionAnalysis | 同上 |
| Timeout | Core（NoResponse+阈值） | AI 不许改判 |
| Exception code | Core（帧内 0x02 事实） | AI 不许重释 |
| Statistics | Core TransactionStatistics | AI/QML 不重算 |
| Displayed string | App 层 formatter/statusText | Core 不出用户文案 |
| Baseline recommendation | Core RuleBasedDiagnosis（结构）＋App formatter（中文） | LLM 不产生新 finding |
| AI explanation wording | LLM（受 ISSUE-006/术语政策约束） | 不写回事实 |
| Agent question interpretation | LLM 选择工具，C++ 白名单裁决 | 模型不能发明工具 |
| Tool result facts | Core 事实经 App DTO | 模型不能改写 |
| Serial connection state | Controller + SerialPortAdapter | QML 直接读写禁止 |
| Replay source state | Controller（loadReplayFile 原子语义） | UI/adapter 不得绕过 |

---

## 7. 15 个"为什么"（项目内证据版）

1. **Core 必须 Zero Qt？** 三种来源与无头测试都依赖同一个分析核心；链入 Qt 会让 Simulator/Replay 的纯逻辑被 GUI 库绑架（`modbuslens_core` 构建级不链 Qt 是这一决定的物理形态）。反过来：项目出现三个事实源则在 Replay/Serial 两个任务中至少要双份逻辑。
2. **QML 不直接调协议 Core？** 需求要求核心可脱离 GUI 测试；QML 直连 core 类型会让 UI 拥有事实所有权，且无 QAbstractListModel 的数据驱动更新。
3. **为什么有 AnalysisController？** 它是 optional→hasX、信号、QObject property 的翻译层，且必须持有 active 结构化 batch 的"同源发布"纪律；没有它 QML 里会散落口径不一致的统计修补。
4. **为什么 Recently Transactions 用 QAbstractListModel？** 行数据由 C++ 批量 replace 产生，用 model（7 roles）使 ListView 数据驱动、与统计同源刷新（UI 不再自己建数组）。
5. **为什么三源共享核心？** 口径一致性护栏（Demo 与 Replay 统计严格相等）是可自动验证的架构承诺；否则维护三套口径。
6. **为什么不统一成过度抽象的 IFrameSource？** 三种源的运行形状完全不一致（内存应答 vs 批式文件 vs 字节流）；T005 曾推迟此抽象，最终共用点落在"分析核心"而非"采集接口"。
7. **为什么 Dashboard 是 current active batch 而非无限历史？** replace 语义显著缩小状态空间与 stale 面：任何时刻只有一份事实与其推导视图，历史回放由 Replay 文件完成而非内存双份历史。
8. **为什么 Connect 成功才切 Serial source？** 失败连接若改 mode/source，会让用户看到串口头但批数据还是旧标的——UI-S02 锁定失败保旧（原子语义）。
9. **为什么 Replay load 失败不破坏旧 batch？** 旧成功数据是用户当前真相，坏文件只是错误态（rule A）；破坏它会让一次误选文件摧毁现场分析。
10. **为什么 Baseline 必须先于 LLM？** Baseline 是确定性引擎的同步产物，LLM 是有网络/配额/权限前提的增强；Base-first 保证产品在无外部依赖时仍然可诊断。
11. **为什么 LLM 是 interpreter 而非 detector？** 模型概率输出不可作为协议事实 authority；项目以 prompt authority 规则+结构化注入+测试锁定三层保证。
12. **为什么 Agent Tools 必须 read-only？** 产品定位诊断而非控制，写能力一旦进 contract 就是授权面爆炸（ISSUE-008/009 的风险已经证明模型侧异常的存在）——类型层面无写 API 是最强边界。
13. **为什么 Agent 用 snapshot？** run 内跨多轮保持一致事实（Tool1 与 Tool2 不能读到不同 batch）；live 状态变化由 revision guard 在 runtime 层另行裁决，两件事职责不同。
14. **为什么异步 AI/Agent 需要 stale guard？** 网络往返窗口内 batch 会变/用户会重问；迟到交付若被接受 = 新 UI 显示旧事实。项目用 capturedRevision×generation 双维处理。
15. **为什么既限制 rounds 又限制 total tool calls？** rounds=3 管 provider 往返深度（成本/延迟），total=6 管本地查询总量（ISSUE-007 实测把合法多步排除在 3 之外）；两者各管一层，不能互相替代。

以上 15 题全部有 T002~T013 档案/测试对应，无 UNKNOWN。

---

## 8. Code Navigation Map（面试快速复习）

| 主题 | 源文件（关键） | 测试（关键） | 入口 API |
| --- | --- | --- | --- |
| CRC | src/core/protocol/ModbusCrc.{h,cpp} | tests/test_modbus_crc.cpp | `crc16Modbus(bytes)` |
| RTU 帧/编解码 | src/core/protocol/ModbusRtuCodec.{h,cpp}、ModbusRtuFrame.h | tests/test_modbus_rtu_codec.cpp、test_modbus_rtu_frame.cpp | `encodeRtuFrame/decodeRtuFrame` |
| FC03 | src/core/protocol/Function03.{h,cpp} | tests/test_function03.cpp | 三个 decoder |
| 模拟与注入 | src/core/simulator/SimulatedSlave.{h,cpp}、SimulationFault | test_simulated_slave / test_simulation_fault | `handleRequest`、`applySimulationFault` |
| 事务/统计 | src/core/analysis/TransactionAnalysis.{h,cpp}、TransactionStatistics | test_transaction_analysis / test_transaction_statistics | 六状态、`summarizeTransactions` |
| Replay | src/core/replay/ReplayLog、ReplayAnalysis | test_replay_log / test_replay_analysis | `parseReplayLog`、`analyzeReplayLog` |
| Serial | src/ui/serial/SerialPortAdapter、src/core/serial/SerialTransactionSession | test_serial_session / test_serial_adapter | openPort/startTransaction/feedResponseBytes |
| 诊断 | src/core/diagnosis/DiagnosisContext、RuleBasedDiagnosis | tests/test_diagnosis.cpp | `buildDiagnosisContext`、`diagnoseTransactions` |
| QML 桥 | src/ui/AnalysisController.{h,cpp}、TransactionListModel | tests/test_ui_bridge.cpp | Q_INVOKABLE + roles |
| AI（one-shot） | src/ui/ai/DiagnosisPromptBuilder、ModelScopeDiagnosisClient | tests/test_ai_client.cpp + fake server | `requestDiagnosis` |
| Agent | src/ui/agent/AgentRuntime、AgentTools、ModelScopeAgentClient、AgentPromptBuilder、AgentToolContext | test_agent_runtime / test_agent_tools / test_agent_integration | `start/dispatchAgentTool/requestRound` |
| 集成/部署 | CMakeLists.txt、scripts/deploy_windows.bat、src/main.cpp | qml_smoke + deploy/minimal-PATH | — |

---

## 9. Architecture Red Flags（未来违规信号）

- QML 或 AI 重新计算 success rate / 修正 TransactionStatus → 违反事实所有权。
- AI/Agent 直接修改 TransactionStatus、statistics 或 baseline → 违反 interpreter 宪法。
- Agent Tool 自动重试 Serial 请求或自动重连 → 违反 read-only / 无自动动作。
- Core include QString 或链接 Qt → 违反 Zero Qt。
- Replay 建自己的 Statistics 实现 → 违反共享口径。
- Serial 建第二套 transaction model（不与 `publishSerialResult` 同源发布）→ 破坏 active batch 纪律。
- 新增 flat overlay 淘汰 SplitView/Flickable/ListView 架构 → ISSUE-004 回归。
- 新 Provider/env override 让 endpoint 可变 → 违反 token 防 exfil 边界。
- 把 docs-only commit 用于推进 LKGC/或 rewrite 更早 history → 违反 Git 纪律。

---

## 10. Self-Check Questions（20 题，不附答案）

**基础（5）**
1. Run Demo 的四条事务分别是什么状态、多少毫秒、哪条带 0x02？
2. `decodeRtuFrame` 返回 variant 的三种形态分别是谁？
3. TransactionStatus 的六种状态与 Timeout 的判定条件是什么？
4. Replay 解析与 0x02 判定分别在哪个任务/模块发生？
5. isExceptionResponse 对一个"功能码 0x83"的帧为什么成立？

**中等（10）**
6. 为什么 `dropResponse` 在 T006 产生、但 Timeout 到 T007 才成立？
7. `summarizeTransactions` 的四条不变量分别是什么？哪条用 optional 表达"无意义"？
8. Partial response（4 字节 CRC 错）在 Serial 里为什么是 CrcError 而不是 Timeout？
9. Connect 制造失败串口时 UI-S02 要求保留什么、为什么？
10. `buildDiagnosisContext` 的 statistics 保证什么自洽性、由谁生成？
11. Baseline NoData 与"尚未运行诊断"两个状态在代码里如何区分？
12. AI 请求的 batchRevision 与 requestGeneration 各自防什么？
13. 为什么 OperationCanceledError 必须先写 abort reason 再 abort（ISSUE-005）？
14. `makeAgentToolContext` 为什么被定为 snapshot builder，而不直接拷 presentation statistics？
15. `validateAgentToolCall` 与 `dispatchAgentTool` 的关系是什么、调用顺序为什么不可调换？

**追问级（5）**
16. 同一 assistant 响应带 7 个 tool_calls 时发生什么、会产生几个 provider 请求？
17. `capturedBatchRevision` 与 `currentBatchRevision` 在 start preflight 与 run 中各自扮演什么角色？ST-A 守的是什么？
18. 为什么 Exception 0x02 只是"非法数据地址"+寄存器映射核对，而不能说出具体地址？
19. ISSUE-008 的 InvalidResponse 有哪四个 producer，为什么不能从"是否显示文案"区分它们？
20. T013 三位候选（aea1e64/5a2f60c/a25d63c）各自为什么没有通过人工视觉验收？

---

*Part 1（System Architecture）完。后续 Part 不在本阶段创建。*