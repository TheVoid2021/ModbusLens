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

*Part 1（System Architecture）完。后续 Part 不在本阶段创建。*---

# Part 2 — Modbus Protocol Core（T002～T004 + T007 边界）

> 与代码互核的符号：`calculateModbusCrc(span<const uint8_t>)→uint16_t`（ModbusCrc）；`ModbusRtuFrame{address,functionCode,data}`（value 语义、不存 CRC）；`RtuDecodeErrorCode{FrameTooShort, CrcMismatch}`；`Function03DecodeErrorCode / Function03EncodeErrorCode`；`TransactionStatus{Pending,Success,Exception,CrcError,Timeout,ProtocolError}`；`ResponseObservation=variant<ModbusRtuFrame, RtuDecodeError, NoResponse>`；`analyzeFunction03Transaction(request, observation, elapsed, threshold)` 纯函数。

---

## 2.1 T002 — CRC16 / MODBUS

**A. API 事实**：`calculateModbusCrc(std::span<const std::uint8_t> data)` 返回数值型 `uint16_t`（按位实现，init=0xFFFF、reflect in/out、XOR out=0x0000）。输入是"待校验的语义字节序列"（不含线上的 CRC 字段本身）。

**B. 数值 vs wire byte order（项目真实 KAT）**
- payload：`01 03 00 00 00 01`
- 数值：`0x0A84`
- wire CRC bytes：`84 0A`（低字节在前）

为什么"明明是 0x0A84、线上却写 84 0A"：Modbus RTU 规范要求 CRC 字段**低字节先发**——`0x0A84` 的低字节 `0x84` 先出现在线上。这不是"小端 CPU"，而是**协议规定的字段传输顺序**。

**C. 为什么不能 `memcpy(uint16_t CRC)` 到 wire**：memcpy 会把本机字节序原样拷出（x86 上会得到 `84 0A`，恰好一致；但这是巧合，不是协议正确性——换架构即错，且把协议语义绑定到了宿主字节序）。正确做法是两个显式字节：`bytes.push_back(crc & 0xFF); bytes.push_back(crc >> 8);`（见 `encodeRtuFrame`）。

**D. 为什么 CPU endian 不能决定协议 endian**：协议字段顺序由 Modbus 规范定义（寄存器语义字段高字节在前、CRC 低字节在前），与运行 CPU 无关；所有序列化必须显式，禁止隐式转换（项目 D2 决策）。

**E. KAT 与 round-trip 的区别**：encode→decode 的往返只证明"编解码器自洽"（用同一个错算法也能自洽闭环）；KAT（已知输入→规范已知输出 `0x4B37` / 帧例 `0x0A84`）才是算法正确的权威证据（04_TEST_STRATEGY 的证据分级：KAT > invariant/round-trip）。

---

## 2.2 T003 — RTU Frame Model

**真实字段**：`{ address, functionCode, data }` + `operator==`；另有 `isExceptionResponse()`（`functionCode & 0x80`）。value 语义、向量拥有数据。

**Frame ≠ wire bytes**。Frame 刻意**不存**：CRC（wire 校验层，避免 stale-CRC bug——头文件注释原文）、elapsed、timeout、transaction status、AI diagnosis。
- **CRC 属于 wire representation 而非 semantic frame state**：CRC 是"线上完整性校验码"，与语义无关；放进 Frame 会让"语义模型"被传输细节污染，并诱使调用方忘记重建/误比较。

---

## 2.3 T004 Part A — RTU Wire Codec

**Encode**：`ModbusRtuFrame` → address → functionCode → data → `calculateModbusCrc` → **先 CRC low byte、再 high byte** → wire。

**Decode**：wire → 最小长度检查（<4 → `FrameTooShort`）→ 取出收到的 CRC 两字节 → 对余下部分重算 → 比较 → 不符 → `CrcMismatch`；通过 → Frame。

**为什么 CRC mismatch 是 decode failure 而不是"Frame + crcValid=false"**：variant 错误模型让下游**不可能忽略**校验失败（系统学上"半好半坏"的对象是静默 bug 温床）；诊断平台尤其需要对"线上确实到达了坏数据"这一事实给出独立状态（CrcError）。这也是 §2.12 的分类原因。

---

## 2.4 T004 Part B — FC03 Request 解析

项目金样：`01 03 00 00 00 01 84 0A`
- `01` = slave address
- `03` = function code（Read Holding Registers）
- `00 00` = startAddress = 0（**16-bit big-endian 语义字段：高字节在前**）
- `00 01` = quantity = 1
- `84 0A` = wire CRC（数值 0x0A84 的低字节在前）

**两条规则不可混淆**：
- register / request 语义字段（startAddress、quantity、register value）：**high byte first**。
- RTU CRC 传输字节：**low byte first**。

---

## 2.5 FC03 Normal Response

金样片段：`01 03 04 00 64 00 C8 ...`
- `01` = address；`03` = function；`04` = byteCount；`00 64`=100；`00 C8`=200。
- **为什么 byteCount=4**：quantity=2 → 2 个 16-bit 寄存器 = 4 字节。
- 寄存器值必须显式 `(high << 8) | low` 组装，禁止依赖主机内存布局（哪怕与传输顺序一致也只是巧合）。

---

## 2.6 Exception Response

`01 83 02 ...`
- `0x83 = 0x03 | 0x80`：异常标志位 + 原功能码。
- `0x02` = exception code（Illegal Data Address，标准语义语义层解释在 T011/T012 作为确定性事实下发）。

**三类区分（项目存在）**：
- 合法 Modbus Exception（0x83 匹配 0x03 请求 + 合法异常码）→ `TransactionStatus::Exception`。
- 帧格式异常/不匹配 → `ProtocolError`。
- wire 校验失败 → `CrcError`。

**为什么 0x84 不能判为 FC03 的匹配 Exception**：0x84 = 0x04|0x80，是对 FC04 请求的异常响应；对 FC03 事务而言，function 不匹配 → `ProtocolError`（而不是把这个异常码的语义套给 FC03）。

---

## 2.7 Codec 与 Transaction 的边界（本阶段重点）

| 问题 | T004 codec 能判 | T007 负责 | 原因 |
| --- | --- | --- | --- |
| wire 长度是否足够 | ✓（FrameTooShort） | — | 纯 frame 形状 |
| CRC 是否正确 | ✓（CrcMismatch） | 分类为 CrcError | wire 校验 |
| FC03 response 自身格式（byteCount/帧内数据量一致等） | ✓（Function03 decode） | 分类为 ProtocolError | 单帧语义 |
| response address 是否 == request | ✗（无 request 上下文） | ✓ | 配对语义 |
| response function 是否匹配 request | ✗ | ✓ | 配对语义 |
| 返回寄存器数 == request quantity | ✗ | ✓（跨帧比较） | 见下例 |
| 是否 Timeout | ✗ | ✓（elapsed≥threshold） | 无时钟概念 |
| elapsed latency | ✗（codec 不进时钟） | ✓（caller 提供 elapsed） | 纯函数 |
| 是否合法 Exception | 部分（帧格式+合法码由 codec 保证） | ✓（异常码归类/文义解释） | 边界在 analyzer |
| Exception code 取值 | 解析出字节 | 记录（optional exceptionCode） | — |

**quantity mismatch 为什么不能属纯 response codec**：response `values={100,200,1500}`（quantity=3）自身是**逐字节完全合法**的 FC03 Normal Response；只有同时看到 request（quantity=2）才能发现不匹配。codec 没有 request 上下文 → 该判断只能发生在 transaction 层。

---

## 2.8 T007 Transaction Boundary（只含协议边界部分）

- `ResponseObservation = variant<ModbusRtuFrame, RtuDecodeError, NoResponse>`（请求方把"到达了什么"抽象成三种语言：好帧 / 解码错误 / 无响应——NoResponse 刻意与 T006 的 DroppedResponse 解耦）。
- 六状态分类流程：
  - observation==NoResponse：`elapsed < threshold` → Pending；`elapsed >= threshold` → Timeout。
  - observation==RtuDecodeError：CrcMismatch → CrcError；FrameTooShort → ProtocolError。
  - observation==Frame：先 address 匹配 → 再 function 匹配（0x03 ↔ 0x83 对偶）→ 再 FC03 语义/quantity 匹配 → Success / Exception / ProtocolError。

---

## 2.9 DropResponse vs Timeout（T006 vs T007）

- T006 `SimulationFault::DropResponse` 只制造**投递事实**："模拟这次没有回答到达"。
- T007 把 `NoResponse` + `elapsed` + `threshold` 合成判定：
  - elapsed=500ms / threshold=1000ms → **Pending**（还没到判定时刻）
  - elapsed=1000ms / threshold=1000ms → **Timeout**
- **DropResponse != Timeout**；Timeout 不能在 Fault Injector 直接生成——注入器无权"断定超时"，超时是 analyzer 对（无响应 + 已等待足够久）的语义裁决。

---

## 2.10 CRC Error vs ProtocolError（真实分类）

| 观察 | 状态 |
| --- | --- |
| decode → CrcMismatch | CrcError（"数据到达但完整性校验失败"是独特诊断事实）
| decode → FrameTooShort | ProtocolError |
| address mismatch | ProtocolError |
| function mismatch（含 0x84 对 FC03 请求） | ProtocolError |
| FC03 响应 malformed / 数量不匹配 | ProtocolError |

CRC Error 单独一档，因为它是"物理线路上最真实的现场故障信号"（干扰/接线/参数）且来自 wire 层而非语义层。

---

## 2.11 RTU Request/Response Pairing

- Modbus RTU **没有 transaction ID**。v1 定案：**one outstanding request**——分析器接收"调用方认为属于同一等待窗口"的 Request+Observation（串行会话/演示/回放都天然满足）。
- 即便有单飞前提，analyzer 仍校验 address / function / 语义一致性——单飞只保证"同一窗口只有一对"，不保证"这对就是同一目标"。
- 当前不存在的机制：并发 request map、transaction-id lookup（记为 limitation，非缺陷——v1 单事务语义足够）。

---

## 2.12 Exact Examples（白板级 6 例）

1. **Success**：req `01 03 00 00 00 02`（CRC 略）→ obs Frame `01 03 04 00 64 00 C8` → address✓/function✓/quantity 2==2✓ → **Success**, elapsed=caller's, exceptionCode=nullopt。
2. **Exception 0x02**：req `01 03 00 64 00 01` → obs `01 83 02`（合法）→ address✓/function(0x83↔0x03)✓/异常码内 0x02 → **Exception**, exceptionCode=0x02。
3. **CRC mismatch**：obs = wire 完整但尾两字节被翻 → decode → CrcMismatch → **CrcError**。
4. **NoResponse before timeout**：obs=NoResponse, elapsed=500, threshold=1000 → **Pending**。
5. **NoResponse at boundary**：obs=NoResponse, elapsed=1000, threshold=1000 → **Timeout**（项目口径 `>=`）。
6. **Quantity mismatch**：req quantity=2 → obs `01 03 06 00 64 00 C8 05 DC`（byteCount=6，合法单帧）→ address✓/function✓ → quantity 不一致 → **ProtocolError**。

---

## 2.13 Code Navigation（协议核快速复习）

| 主题 | 文件 | API | 测试 |
| --- | --- | --- | --- |
| CRC | src/core/protocol/ModbusCrc.{h,cpp} | `calculateModbusCrc(span)` | tests/test_modbus_crc.cpp（KAT/边界/zero-remainder 等） |
| Frame | src/core/protocol/ModbusRtuFrame.h | struct + `isExceptionResponse` | test_modbus_rtu_frame.cpp |
| RTU Codec | src/core/protocol/ModbusRtuCodec.{h,cpp} | `encodeRtuFrame` / `decodeRtuFrame`（variant） | test_modbus_rtu_codec.cpp |
| FC03 | src/core/protocol/Function03.{h,cpp} | request/normal/exception 三个 decoder（+encode） | test_function03.cpp（含 V1.1b3 官方 gold 样例） |
| Transaction | src/core/analysis/TransactionAnalysis.{h,cpp} | `analyzeFunction03Transaction` + `TransactionStatus` | test_transaction_analysis.cpp |

---

## 2.14 Misconception List（至少 10 条，均针对本项目）

1. ❌ "Modbus 所有字段都是 little-endian。" ✅ 寄存器/请求语义字段 high-byte-first；只有 RTU CRC 线上是 low-byte-first。
2. ❌ "没收到 response 就是 Timeout。" ✅ 未到阈值是 Pending。
3. ❌ "0x02 是 CRC 错误。" ✅ 0x02 是 Modbus Exception Code（Illegal Data Address）；CRC 错误来自 wire 校验。
4. ❌ "0x84 也是对 FC03 的异常响应。" ✅ 0x84 对应 FC04 请求，对 FC03 是 function mismatch → ProtocolError。
5. ❌ "Frame 里存了 CRC。" ✅ Frame 只有 address/function/data；CRC 只在 wire 层。
6. ❌ "CPU 是小端所以 84 0A 出现在线上。" ✅ 84 0A 是协议规定（低字节先发），与 CPU 无关，也不能 memcpy。
7. ❌ "encode→decode 能走通就证明 CRC 正确。" ✅ 那只证明自洽；KAT 才是算法正确的权威证据。
8. ❌ "DropResponse 就是 Timeout。" ✅ 注入只制造无响应事实；Timeout 由 elapsed+threshold 在 T007 裁决。
9. ❌ "quantity 不匹配是 response codec 的错。" ✅ 单帧本身完全合法，只有见 request 才知道不匹配 → 属 transaction 层。
10. ❌ "RTU 有 transaction ID 可以用来配对。" ✅ RTU 没有；v1 用 one-outstanding + address/function/语义三重校验。
11. ❌ "异常响应（0x83）与 CRC 校验是同一层的检查。" ✅ 0x83 是语义层匹配；CRC 在 wire decode 层先行。

---

## 2.15 Self-Test（15 题，不附答案）

**基础 5**
1. `calculateModbusCrc` 的输入是什么字节？0x0A84 为什么在线上变成 84 0A？
2. `ModbusRtuFrame` 三个字段之外，为什么不放 CRC？
3. 逐字节解释 `01 03 00 00 00 01 84 0A`。
4. `01 03 04 00 64 00 C8 ...` 中 byteCount 为什么是 4？
5. `0x83` 与 `0x02` 各自表示什么？

**边界 5**
6. 为什么 `0x84` 不能判 FC03 Exception？
7. 单帧 `01 03 06 ...`（byteCount=6）自身合法，为什么在一个 quantity=2 的事务里是 ProtocolError？
8. CrcMismatch 与 FrameTooShort 分别落到什么 TransactionStatus？
9. Pending 与 Timeout 的分界条件是什么（elapsed/threshold）？
10. "到达了 4 字节坏 CRC 的 response"在 Serial 里为什么是 CrcError 而不是 Timeout？

**追问 5**
11. 为什么 decode 失败用 variant 错误而不是 "Frame+crcValid=false"？
12. 为什么 DropResponse 不能在 Fault Injector 里直接生产 Timeout？
13. one-outstanding 前提下，为什么 analyzer 还要校验 address/function？
14. 为什么 round-trip 不能替代 KAT？（04 的证据分级怎么表述？）
15. KAT 数值 0x0A84 反推出"线上 84 0A"的两步推理是什么？

---

*Part 2 完。后续 Part 不在本阶段创建。*---

# Part 3 — Transaction / Statistics（T007 Part A + Part B）

> 与代码互核：`TransactionStatus` 六值；`TransactionAnalysis{status, elapsed, optional exceptionCode}`；`TransactionStatisticsSnapshot` 字段与四不变量（头文件注释 explicit）；`summarizeTransactions(std::span<const TransactionAnalysis>)` 纯函数；STAT-B01~B08 语义注释与 STAT-I01 集成（Success elapsed=15ms → rate 0.25 / avg 15.0）。

---

## 3.1 中文术语表（首次出现的中文解释）

- **Transaction（事务）**：一次 Request→Response 的通信过程（不是单条帧）。
- **TransactionAnalysis**：对单次事务的确定性分析结果（status/elapsed/exceptionCode）。
- **Statistics（统计）**：对一批事务的聚合描述。
- **Snapshot（快照）**：一次根据当前输入计算出的完整结果，不是持续累加器。
- **Observed（已观测）**：进入分析的事务总数。
- **Pending（进行中）**：尚未产生最终结果、仍在等待。**不属于已完成、也不算失败。**
- **Completed（已完成）**：已产生最终结果的事务（Success/Exception/CrcError/Timeout/ProtocolError 五类的并集）。
- **Success Rate（成功率）**：successCount ÷ completedCount。
- **Latency（延迟/耗时）**：一条事务自请求到结果判定的 elapsed。
- **Invariant（不变量）**：任何合法 Snapshot 都必须满足的关系，不是建议。
- **Optional（可选值）**：有值或无值（std::optional）；"无值"与"0"是不同的语义。

---

## 3.2 TransactionAnalysis 最终模型

```cpp
enum class TransactionStatus { Pending, Success, Exception, CrcError, Timeout, ProtocolError };
struct TransactionAnalysis {
    TransactionStatus status{};
    std::chrono::milliseconds elapsed{0};
    std::optional<std::uint8_t> exceptionCode;   // 仅 Exception 有值
};
```

- **elapsed**：六种状态一律保留调用方提供的原值（Timeout 时它同时是"等了多久"的证据，Pending 时是"已等待"）。
- **exceptionCode**：只有 `Exception` 有值；其余状态一律 nullopt（"成功"不可能携带异常码）。
- **定位**：`TransactionAnalysis` 是一条事务的确定性事实——**不是** UI row、不是展示字符串、不是 AI diagnosis、不是 statistics。

---

## 3.3 Statistics Snapshot 的职责与形态

- Part A：一个 Transaction → 一个 TransactionAnalysis。
- Part B：一批 TransactionAnalysis → 一个 `TransactionStatisticsSnapshot`。
- Snapshot 只回答："当前这一批事务呈现什么统计特征？"——它是纯函数输出，输入一批事务、重新计算一次完整快照。
- 它**不是**：database、History Store、StatisticsManager、实时 accumulator、Session Manager、图表、QML Model。

**为什么用纯函数全量重算（把 accumulator 排除在 v1 外）**：
- 输入输出明确、零隐藏状态 → 单测简单（一次输入一次快照）；
- Replay 可对任意历史批次重新计算；
- Controller 可对当前 active batch 重算；
- 消灭 reset/remove/rollback/history-sync/thread-safety 等整套问题；
- 当前产品规模没有消费者需要可变累加器（mutable accumulator 留在无意义侧）。

---

## 3.4 Snapshot 真实字段（逐一中文化）

| 字段 | 中文 | 含义 |
| --- | --- | --- |
| observedCount | 已观测总数 | = 输入事务总数 |
| pendingCount | 进行中数 | status==Pending 的条数 |
| completedCount | 已完成数 | 五类终态之和 |
| successCount | 成功数 | Success 条数 |
| exceptionCount | 异常数 | Exception 条数 |
| crcErrorCount | CRC 错误数 | CrcError 条数 |
| timeoutCount | 超时数 | Timeout 条数 |
| protocolErrorCount | 协议错误数 | ProtocolError 条数 |
| successRate | 成功率（optional） | completed>0 才有值 |
| averageSuccessLatencyMs | 平均成功延迟（optional） | success>0 才有值 |

---

## 3.5 observed / pending / completed

- `observedCount = 输入事务总数`。
- `pendingCount = status==Pending 的数量`。
- `completedCount = Success + Exception + CrcError + Timeout + ProtocolError`（Pending 不属于 completed）。
- 例：Success=1 与 Pending=99 → observed=100、pending=99、completed=1（**不是 100**）。

---

## 3.6 Success Rate 精确定义（v1）

```
successRate = successCount / completedCount     // Pending 绝不进分母
```

- 例：Success=8、Exception=1、Timeout=1、Pending=90 → observed=100、completed=10、successRate=**8/10=80%**（不是 8/100=8%）。
- 为什么 Pending 不能提前算失败：它还没有结果；把它算进分母等于把"还没发生"判成"已经失败"，会系统性低估成功率。

---

## 3.7 completed=0 与 success=0 的边界（nullopt vs 0.0）

- **Case A：completedCount==0**（空输入，或全 Pending）→ `successRate = nullopt`（无值）：没有任何完成事务，"成功率"在数学上无定义。**不得显示为 0%。**
- **Case B：completedCount>0 且 successCount==0**（全是 Exception/Crc/Timeout/Protocol）→ `successRate = 0.0`：事务已完成、但确实无一成功。
- **nullopt 与 0.0 是完全不同的语义**：前者"还没有可计算的样本"，后者"有样本且成功为零"。UI 用 hasSuccessRate 分流（"无值"显示 `—`，"有值 0"显示 `0.0%`）。

---

## 3.8 Average Success Latency（只成功延迟）

- `averageSuccessLatencyMs = 仅 Success 事务 elapsed 的平均值`。
- 例：Success 10ms、Success 30ms、Timeout 1000ms、Exception 200ms → **(10+30)/2 = 20ms**，不是 (10+30+1000+200)/4。
- 为什么 Timeout 的 1000ms 不进入"成功响应平均延迟"：它是"没有响应"的等待时长，度量对象与"成功响应耗时"不是同一事件。
- `successCount==0` → `averageSuccessLatencyMs = nullopt`。

---

## 3.9 四个 Invariants（任何合法快照都必须满足）

- **A.** `observedCount == pendingCount + completedCount`
- **B.** `completedCount == success + exception + crcError + timeout + protocolError`
- **C.** `successRate.has_value()` **iff** `completedCount > 0`
- **D.** `averageSuccessLatencyMs.has_value()` **iff** `successCount > 0`

锁定方式：STAT-B08 在 mixed batch 上断言 A~D（测试注释 documented）。

---

## 3.10 summarizeTransactions（真实 API 与流程）

```cpp
TransactionStatisticsSnapshot summarizeTransactions(
    std::span<const TransactionAnalysis> transactions);
```

```
TransactionAnalysis batch
  ↓ 遍历 status
  ↓ 累加 success/exception/crcError/timeout/protocolError/pending
  ↓ completedCount = 五类之和; observedCount = 总数
  ↓ successRate         = success / completed        (completed>0 才有)
  ↓ averageSuccessLatencyMs = 平均(Success.elapsed)   (success>0 才有)
  ↓ 返回 Snapshot
```

**依赖方向**：Protocol → Transaction Analysis → Statistics（TransactionAnalysis 绝不反向依赖 Statistics）。Statistics 只消费"已经确定的 status"：它不决定 TransactionStatus，只计数。

---

## 3.11 为什么 exhaustive switch 而非 map/字符串

- 固定 enum + 固定字段是最好的 v1 形态：六状态显式分支，每个 case 直接对应一个字段累加。
- map<TransactionStatus,...>/字符串比较/Strategy framework：状态只有六个、消费方只有计数——框架是过度设计。
- 未来新增 TransactionStatus 时，缺 case 直接编译告警，**暴露必须同步修改 Statistics 的位置**（这叫 fail-at-compile,不叫失败运行）。

---

## 3.12 Tests（真实语义核验）

- **STAT-B01 Empty**：全 0 + 两个 nullopt（不是假 0）。
- **STAT-B02 All Success**：rate=1.0 + avg latency。
- **STAT-B03 Mixed Completed**：五类 count 正确；Timeout elapsed 不进入成功平均。
- **STAT-B04 Pending Excluded**：rate 分母不含 Pending（1.0 而非 1/3）。
- **STAT-B05 All Pending**：rate nullopt、avg nullopt。
- **STAT-B06 Completed But No Success**：rate 恰为 0.0（非 nullopt）、avg nullopt。
- **STAT-B07 Success Latency Isolation**：只有 Success elapsed 参与平均。
- **STAT-B08 Invariants**：A~D 在 mixed batch 上断言。
- **STAT-I01 Integration**：真实链路（模拟从站读请求 → analyzer ×4 → summarize）—— real 代码核实：Success elapsed**=15ms**；最终 observed=4/completed=4/success=1/exception=1/crc=1/timeout=1/protocol=0/rate=0.25/avg=**15.0**（test_statistics_integration.cpp 断言）。

---

## 3.13 区分两个 Golden Batch（不要混）

| | A. Statistics Integration Test（STAT-I01） | B. T008 Demo Batch（产品演示） |
| --- | --- | --- |
| Success elapsed | **15 ms** | **25 ms** |
| Exception | 有（集成自造） | 18 ms / 0x02 |
| CRC | 有 | 17 ms |
| Timeout | 有 | 1000 ms |
| 快照 | 4/4/0 · 1/1/1/1/0 · 0.25 · **15.0** | 4/4/0 · 1/1/1/1/0 · **25%** · **25 ms** |

Demo 平均延迟是 **25ms**，因为只有 Success（25ms）参与平均——不是 (25+18+17+1000)/4。

---

## 3.14 从 Transaction 到 Dashboard 的事实链

```
Simulator / Replay / Serial
   ↓ 各自产出 TransactionAnalysis 数组（同一个 core）
transactions → summarizeTransactions
   ↓
TransactionStatisticsSnapshot
   ↓
AnalysisController（hasSuccessRate/successRate/... Q_PROPERTY，emit statisticsChanged）
   ↓
QML Dashboard（只格式化与显示）
```

- QML 不计算成功率、不重数 Timeout、不自算平均延迟。
- Presentation 只做：格式化（0.25→"25.0%"）、显示、颜色/文本。
- Core 才负责统计事实。

---

## 3.15 从 Statistics 到 Diagnosis

```
Transaction facts + Statistics Snapshot
   → DiagnosisContext（buildDiagnosisContext 内自洽重算）
```

- Statistics 是确定性事实。Baseline / AI / Agent 只能消费/解释。
- 禁止：LLM 重新计算 successRate、修改 counts、修改 average latency。
- 若 AI 文本与 Snapshot 冲突：以 Core Snapshot 为准（AI 文本只是展示字符串）。

---

## 3.16 浮点数规则

- `successRate` 与 `averageSuccessLatencyMs` 是 **double**。
- 真实测试用 `qFuzzyCompare`（tolerance/fuzzy）而非精确 `==`（STAT-B02/STAT-I01 断言）。
- Core 返回数值 0.25；Presentation 才决定显示 "25.0%"。
- **Core 绝不返回字符串 "25%"**——字符串是展示格式，不是事实。

---

## 3.17 常见误区（≥10，均指本项目）

1. ❌ "Pending 算失败。" ✅ Pending 未完成，不进入 successRate 分母。
2. ❌ "没有完成事务就是 0% 成功率。" ✅ completed==0 时 successRate 无值。
3. ❌ "完成了但 0 Success 时 successRate 也无值。" ✅ completed>0 时是 0.0。
4. ❌ "平均延迟统计所有事务。" ✅ v1 只统计 Success elapsed。
5. ❌ "Timeout 的 1000ms 应进入平均成功响应耗时。" ✅ 不进入（度量对象不同）。
6. ❌ "Success=1、Pending=99 的 completed=100。" ✅ completed=1。
7. ❌ "成功率 = success/observed。" ✅ v1 是 success/completed。
8. ❌ "QML 自己算统计就够了。" ✅ 统计事实属于 deterministic core。
9. ❌ "Statistics 决定 TransactionStatus。" ✅ Statistics 只消费已定 status。
10. ❌ "可选值没有就给 0 兜底。" ✅ nullopt 与 0.0 不同义；hasX 才是业务含义。
11. ❌ "把 15ms 集成测试与 25ms Demo 混为一谈。" ✅ 两批 golden 事实不同。

---

## 3.18 Code Navigation

| 主题 | 关键文件 | API/符号 | 测试 |
| --- | --- | --- | --- |
| Transaction Analysis | src/core/analysis/TransactionAnalysis.{h,cpp} | `TransactionStatus`、`TransactionAnalysis`、`ResponseObservation`、`analyzeFunction03Transaction` | tests/test_transaction_analysis.cpp、test_transaction_integration.cpp |
| Transaction Statistics | src/core/analysis/TransactionStatistics.{h,cpp} | `TransactionStatisticsSnapshot`、`summarizeTransactions` | tests/test_transaction_statistics.cpp、test_statistics_integration.cpp |
| Demo integration | src/ui/AnalysisController.cpp | `runDemoBatch()`（makeEntry→analyze×4→summarize→applySnapshot） | tests/test_ui_bridge.cpp |
| Replay statistics | src/core/replay/ReplayAnalysis.cpp | `analyzeReplayLog` 内部复用 `summarizeTransactions` | test_replay_analysis / ui_bridge r01 |
| Diagnosis | src/core/diagnosis/DiagnosisContext.cpp | `buildDiagnosisContext`（自洽重算同一批） | tests/test_diagnosis.cpp |

---

## 3.19 Self-Test（15 题，无答案）

**基础 5**
1. 六状态 `TransactionStatus` 各自在什么 observation 下产生？
2. `elapsed` 与 `exceptionCode` 在六种状态下各是什么值域？
3. `observed/pending/completed` 三者关系是什么？Success=2、Pending=3 时各是多少？
4. `summarizeTransactions` 的大致计算顺序？
5. 平均成功延迟为什么只用 Success elapsed？

**边界 5**
6. completed=0 与 completed>0&&success=0 时 successRate 分别为什么值？UI 如何区分？
7. Timeout 1000ms + Success 10/30ms 的平均成功延迟是多少？
8. 四个 Invariant 分别是什么？哪个测试锁定？
9. 为什么 Pending 不进成功率分母？（给一个数字反例）
10. `qFuzzyCompare` 用在哪类断言，为什么不 `==`？

**追问 5**
11. Replay 和 Demo 为什么能得到同一套统计口径？
12. 依赖方向 Protocol→Analysis→Statistics 被颠倒会带来什么架构危害？
13. 为什么 v1 选择纯函数快照而非 mutable accumulator？（列举至少三个真实理由）
14. 新增第七种 TransactionStatus 时，Statistics 哪个位置会先报警、为什么这是好事？
15. STAT-I01 的 avg=15.0 与 Demo 的 25.0 怎么从事实源各自推出？

---

*Part 3 完。后续 Part 不在本阶段创建。*---

# Part 4 — Simulator / Replay / Serial Execution Modes

> 与代码互核：`SimulatedSlave::handleRequest`（可信 Frame→正常/合法异常）；`SimulationFaultMode{None,CorruptCrc,DropResponse,ArtificialDelay}`、`SimulatedDelivery=variant<DeliveredWire,DroppedResponse>`；`ReplayTransactionRecord{elapsed, requestWire, optional responseWire}`（nullopt=明确"未观察到响应"）、`ReplayLog{records, timeoutThreshold}`；`SerialTransactionState{Idle, AwaitingResponse}`、`SerialTransactionErrorCode{Busy, NotActive, ...}`、`beginReadHoldingRegisters→variant<SerialRequestStart,SerialTransactionError>`、`feedResponseBytes`。

---

## 4.1 三种模式的共同目标

Simulator / Replay / Serial 最终都应进入**同一条**判定链路：

```
Modbus Frame / Decode Error / NoResponse
   → TransactionAnalysis（T007）
   → TransactionStatistics（T007）
   → Controller / Diagnosis
```

任何一种模式都**不得**自己重新实现：CRC、FC03 解析、TransactionStatus 分类、Statistics 公式。差别只在"事实从哪里来、怎样到达"，而不是"事实怎样被判断"。

---

## 4.2 Simulator（T005）

- `SimulatedSlave` 模拟的是 **Slave**：接收可信 Request Frame → 查 Holding Register → 返回正常 Response 或合法 Exception（如起始地址越界 → 0x83/0x02）。
- **不负责**：CRC corruption、DropResponse、Timeout、UI、AI。
- Fault Injector 与 SimulatedSlave 分开的理由：从站语义与"故意破坏投递"是两个正交关注点；语义端点保持纯净（const 应答），破坏面单独在 wire 层注入（T006）。

---

## 4.3 Fault Injection（T006）

真实层次：

```
SimulatedSlave → 正确 Response Frame
   → encodeRtuFrame → 正确 wire
   → applySimulationFault（None / CorruptCrc / DropResponse / ArtificialDelay）
```

- **CorruptCrc 必须作用于 wire**：CRC 属于 wire representation（Part 2 结论），语义帧里根本没有 CRC 可"翻"。
- **DropResponse ≠ Timeout**：注入只产生投递事实"没有回答到达"（DroppedResponse）；Timeout 是 T007 依据 NoResponse + elapsed + threshold 的裁决（Part 3/Part 2 双重锁定）。
- **ArtificialDelay 不真实 sleep**：它携带 metadata（延迟量），由上层测试赋值给 elapsed 传入 analyzer——core 无时钟、无等待（纯函数纪律）。

---

## 4.4 Replay（T009）

- Replay = 读取历史记录做**批式离线重分析**，不是实时通信、不是按时间轴播放。
- 真实模型：

```cpp
struct ReplayTransactionRecord {
    std::chrono::milliseconds elapsed{};
    std::vector<std::uint8_t> requestWire;
    std::optional<std::vector<std::uint8_t>> responseWire; // nullopt = NO_RESPONSE（历史事实）
};
struct ReplayLog { std::vector<ReplayTransactionRecord> transactions; std::chrono::milliseconds timeoutThreshold{1000}; };
```

- **Replay 不 sleep**：elapsed 是历史记录的直接值，原样传给 `analyzeFunction03Transaction(request, observation, elapsed, threshold)`——分析结果与当年完全同口径。

---

## 4.5 坏 Request vs 坏 Response（重点面试题）

- **坏 Request**（requestWire decode 失败 / FC03 语义非法）：无法建立可信的事务起点 → **Replay execution error**（InvalidRequestWire/InvalidRequestData/InvalidRequestFunction 三错误码）。**不能**伪造 `TransactionStatus::CrcError`——CRC 错误形容的是"响应数据损坏"，与"请求不可信"是两件事。
- **坏 Response**（responseWire decode 失败）：恰恰是**历史诊断对象本身** → 转成 `ResponseObservation{RtuDecodeError}` → analyzer → CrcError / ProtocolError（真实历史故障被如实重放）。
- 为什么必须区别：一个描述"复盘无法进行（输入不可信）"，一个描述"当时现场确有此故障"。混淆会让坏日志被伪装成 CRC 故障统计。

---

## 4.6 Replay 为什么复用 T007

```
Replay text/file → Parser → ReplayLog
   → requestWire decode（可信链）
   → responseWire decode / NoResponse
   → ResponseObservation
   → analyzeFunction03Transaction
   → TransactionAnalysis
   → summarizeTransactions（batch 级）
```

Replay 自己**不再**实现：CRC 分类、Timeout 分类、quantity mismatch、统计公式——全部是 T007 的既定裁决。

---

## 4.7 Serial（T010）两层职责

- **Qt Serial Adapter（App 层）**：`QSerialPort` 生命周期、port discovery、open/close、字节 I/O、Qt 信号/定时器/事件集成（queued error handling，PE-4）。它是"transport adapter"。
- **SerialTransactionSession（Pure C++ Core）**：begin FC03 事务、保存请求、one outstanding、receive buffer、任意切块累计、candidate framing、调 codec、调 transaction analyzer、timeout 收口、cancel/reset。它是"deterministic session logic"。
- 状态机真实值：`Idle` / `AwaitingResponse`；begin 时已有事务 → `Busy` 错误；无事务时收口 → `NotActive`。

**为什么 Session 必须 Pure C++**：framing/runtime 全塞进 QObject/QSerialPort 会——难单测、协议逻辑与平台 I/O 耦合、Replay/Simulator 无法共享思想、真实 COM 环境成为验证前提。分离后：Qt 负责运输，Core 负责裁决。

---

## 4.8 Serial byte chunks（真实串口特性）

一次 `readyRead` 不保证一帧：可能先 `01 03 04`、再 `00 64`、再 `00 C8 BA 7A`。因此必须有 receive buffer + candidate framing。**一次 read == 一帧是错误假设**。

---

## 4.9 FC03 response framing（真实实现基础）

- Normal：`Address(1) + Function(1) + ByteCount(1) + Data(byteCount) + CRC(2)`，候选完整长度由 byteCount 推出（5+2N 形式）。
- Exception：固定 5 字节（`Address + 0x83 + Code + CRC`）。
- **framing 不判断 quantity mismatch**：候选是否收齐只看字节结构；"返回寄存器数是否等于 request.quantity"仍属 Transaction Analysis（Part 2 跨帧边界）。

---

## 4.10 One Outstanding Request

同一 session 一次只允许一个未完成 Request（Idle↔AwaitingResponse）。AwaitingResponse 时再次 begin → `Busy`；**不 queue、不覆盖当前 request**。这个约束让没有 Transaction ID 的 RTU v1 保持简单可靠（结合 T007 的 address/function/语义三重校验）。

---

## 4.11 Serial Timeout 职责边界

- Qt timer（adapter）推进时间 → 时间到 -> session 的 timeout 收口路径，把 elapsed 交给 analyzer；分析器按 elapsed≥threshold 判定 Timeout。
- **不是 Slave 产生 Timeout**；slave 只是"没响应"（或响应没到），判定属于本机分析层。

---

## 4.12 Serial Port Discovery（真实 contract）

- 生产唯一发现源 = `QSerialPortInfo::availablePorts()`。
- refresh 只允许：重新枚举。不得：open、write、probe Modbus、自动连接。
- 端口列表可以为空——**空列表不是程序故障、不是 SerialError**。
- 不声称固定存在 N 个 port。

---

## 4.13 No-port 历史事实（证明 vs 未知）

- **已证明**：本项目历史环境曾观察到过 serial devices（档案记录）；最终一次 production Qt runtime 受控 probe 得到 `QSerialPortInfo::availablePorts().size() == 0`。
- **设计约束**：UI 提供明确空态（"未检测到串口"）、Refresh 恒可用、Connect disabled、不伪造端口。
- **未知（不得发明 RCA）**："现在为何是 0"（驱动/USB/COM 占用等一律是未经证明的假设）。这一纪律与 ISSUE-008/009 同源：不把 hypothesis 写成 fact。

---

## 4.14 三模式对照表（以代码为准）

| | Simulator | Replay | Serial |
| --- | --- | --- | --- |
| 数据来源 | 内存 SimulatedSlave + Fault | 历史 .mlog 文件 | QSerialPort 实时字节 |
| 是否实时 | 是（点击即算） | 否（批式重分析） | 是 |
| 是否真实等待 | 否 | 否（elapsed 是历史值） | 是（Qt timer） |
| Request 来源 | makeFc03Read 演示构造 | requestWire 字节 | encodeReadHoldingRegistersRequest |
| Response 来源 | Slave 应答（±Fault） | responseWire 历史字节 | readyRead 字节 |
| 物理串口 | 无 | 无 | 有（可选） |
| 使用 RTU codec | ✓（encode/decode 校验） | ✓（双 wire decode） | ✓（session framing） |
| 使用 T007 analyzer | ✓ | ✓ | ✓ |
| 使用 T007 statistics | ✓ | ✓ | ✓（单元素批） |
| 可产生 NoResponse | ✓（DropResponse） | ✓（NO_RESPONSE 字段） | ✓（超时无响应） |
| 主要错误来源 | 构造错误（演示代码防御 qWarning） | Parse/Execution 错误（文件坏） | Transport error（端口/写失败） |
| 测试方式 | SIM/FAULT tests | REPLAY/A + I | SERIAL + adapter + UI bridge |

---

## 4.15 三条真实调用链（都到 Analysis + Statistics）

**A. Simulator Demo（Success/CRC/Timeout 四种）**
`runDemoBatch() → makeFc03Read → SimulatedSlave.handleRequest → (DEMO-3) encodeRtuFrame → applySimulationFault(CorruptCrc) → decodeRtuFrame → RtuDecodeError → (DEMO-4) applySimulationFault(DropResponse) → DroppedResponse→NoResponse → analyzeFunction03Transaction(request, obs, elapsed, 1000ms) ×4 → summarizeTransactions → applySnapshot + setEntries → QML`

**B. Replay 一条事务**
`loadReplayFile → parseReplayLog → ReplayLog → analyzeReplayLog → 每条: requestWire→decode(可信) / responseWire→decode|NoResponse → analyzeFunction03Transaction → ReplayTransactionOutcome → 全批 summarizeTransactions → Controller publish → QML`

**C. Serial FC03 read**
`connectSerial → openPort(QSerialPort 8N1+波特率) → readHoldingRegistersOnce → startTransaction → encodeRequest + write → readyRead chunk(s) → feedResponseBytes → candidate complete → codec/analyzer → transactionCompleted(TransactionAnalysis) → publishSerialResult → single-row model + single-element statistics → QML`

---

## 4.16 IFrameSource 决策（为什么不强行统一）

三种来源的调用形状完全不同：Simulator 是"Request→Response"同步算式，Replay 是"Batch→Analysis"批处理，Serial 是"async bytes + session 生命周期"。早期为了"架构好看"强行抽 `IFrameSource` 只会造出既不适配三个形状、又无消费者的抽象。**结论：共用点落在分析核心（Transaction/Statistics），而不是采集接口。**

---

## 4.17 Source vs Analysis（核心思想）

Source / Transport / Runtime（Simulator、Replay、Serial）可以各不相同；Protocol / Transaction / Statistics（Core）必须共享。这就是"三模式同口径"的架构表达：**来源负责把事实送达，核心负责裁决事实。**

---

## 4.18 Tests 边界（每类证明什么）

- **Simulator tests（SIM-T）**：从站语义正确（正常/异常/越界），无注入参杂。
- **Fault tests（FAULT-T）**：四模式注入结果形态（DeliveredWire/DroppedResponse）+ 确定性。
- **Simulator integration**：T002→T005 首次全链路闭环（slave→codec→analyzer）。
- **Replay log tests（REPLAY-A）**：text→ReplayLog 解析矩阵（header/wire/行号/CRLF/注释）。
- **Replay analysis tests（REPLAY-I）**：batch→outcome；坏请求三错误码；坏响应→诊断事实；golden 与 Demo 同口径。
- **Serial session tests（SERIAL-A）**：Zero Qt 状态机、framing、Busy/NotActive、partial vs CrcError。
- **Serial adapter tests**：QSerialPort 路径（含 PE-4 错误风暴有界性）——唯一链接 QtSerialPort 的测试 target。
- **UI bridge tests**：三模式发布路径（UI-R / UI-S / UI-B）与 stale guard。

---

## 4.19 Common Misconceptions（≥12 条）

1. ❌ "Simulator 是假数据直接塞 UI。" ✅ 真实经过 core analyzer/statistics。
2. ❌ "DropResponse 就是 Timeout。" ✅ 注入≠裁决；阈值决定。
3. ❌ "Replay 按 elapsed 真实 sleep 回放。" ✅ 批式瞬间重算。
4. ❌ "坏 Request 应产生 CrcError 事务。" ✅ 应产生 execution error（请求不可信 ≠ 响应损坏）。
5. ❌ "Serial 一次 read 就是一帧。" ✅ 任意切块；需 buffer+framing。
6. ❌ "framing 应判断 quantity mismatch。" ✅ framing 只看字节结构；匹配归 T007。
7. ❌ "refresh 串口可以顺便 open/probe。" ✅ discovery-only，禁自动 side-effect。
8. ❌ "端口列表为空就是 SerialError。" ✅ 是合法空态；有 UI 提示、无错误语义。
9. ❌ "历史见过 2 个 port，现在必须有 2 个。" ✅ 设备可用性随时间变化；当前 Qt 探针=0。
10. ❌ "三种模式必须共用一个大 interface 才算好架构。" ✅ 共用点=分析核心，非采集接口。
11. ❌ "ArtificialDelay 会让 core 真的睡一会。" ✅ metadata 延迟，由测试/demo 赋 elapsed。
12. ❌ "Serial 超时是 slave 产生的。" ✅ 由本机 elapsed+threshold 收口判定。

---

## 4.20 Code Navigation

| 主题 | 文件 | 关键符号 |
| --- | --- | --- |
| Simulator | src/core/simulator/SimulatedSlave.{h,cpp} | `handleRequest` |
| Fault | src/core/simulator/SimulationFault.{h,cpp} | `applySimulationFault`, `SimulatedDelivery` |
| Replay | src/core/replay/ReplayLog.{h,cpp}, ReplayAnalysis.{h,cpp} | `parseReplayLog`, `analyzeReplayLog`, `ReplayTransactionRecord` |
| Serial Session | src/core/serial/SerialTransactionSession.{h,cpp} | `beginReadHoldingRegisters`, `feedResponseBytes`, `onResponseTimeout` |
| Serial Adapter | src/ui/serial/SerialPortAdapter.{h,cpp} | `openPort`, `startTransaction`, `cancelPending` |
| Controller 集成 | src/ui/AnalysisController.cpp | `runDemoBatch`, `loadReplayFile`, `connectSerial`, `readHoldingRegistersOnce`, `publishSerialResult` |
| Tests | tests/：test_simulated_slave / test_simulation_fault / test_simulator_integration / test_replay_log / test_replay_analysis / test_serial_session / test_serial_adapter / test_ui_bridge | — |

---

## 4.21 Self-Test（15 题，无答案）

**基础 5**
1. SimulatedSlave 的输入输出形态是什么？它会不会故意坏 CRC？
2. Fault Injection 的四种模式各产生什么 variant？
3. ReplayTransactionRecord 三个字段的语义（responseWire=nullopt 表示什么）？
4. Serial Session 的两个状态与两个 begin 边界错误是什么？
5. 三条来源最终共用的判定函数名是什么？

**边界 5**
6. CorruptCrc 为什么不能作用于 Frame 而必须作用于 wire？
7. Replay 坏请求与坏响应为什么必须分类不同（各落到哪）？
8. `01 03 04 / 00 64 / 00 C8 BA 7A` 三个 chunk 到达时 session 做什么？
9. 为什么 framing 收齐的依据是 byteCount 而非 quantity 匹配？
10. 端口列表为空时为什么不是 SerialError？

**追问 5**
11. 为什么 ArtificialDelay 不在 core 里 sleep？（纯函数纪律向内推导……从哪些 API 能看出？）
12. Serial 超时的"发起者/收到者/判定者"分别在哪层？
13. 为什么不抽 IFrameSource？请在三种模式的调用形状上给出证据。
14. 集成测试的 15ms 与 Demo 的 25ms/1000ms 分别如何进入统计？（跨 Part 3/4 追问）
15. "历史见过 2 个 port"与"现在 Qt probe=0"这件事，哪些能说、哪些不能说？为什么？

---

*Part 4 完。后续 Part 不在本阶段创建。*---

# Part 5 — Qt/QML Adapter & Presentation（T008/T008.1/T009B/T010/T011/T012/T013 UI 边界）

> 与代码互核：`main.cpp`（`QQuickStyle::setStyle("Fusion")`、`engine.loadFromModule("ModbusLens","Main")`、`--qml-smoke-test` 分支）；`CMakeLists.txt`（`qt_add_qml_module(URI ModbusLens VERSION 1.0 ...)`、QML 模块直接挂在 exe target）；`AnalysisController`（Q_PROPERTY/Q_INVOKABLE 两族）；`TransactionListModel`（7 roles，QAbstractListModel）；ADR001。

---

## 5.1 中文术语表

- **Adapter（适配层）**：把一种数据/API 转换成另一层可使用的形式。
- **Controller（控制器）**：应用层编排，向 UI 暴露状态与命令；不是业务核心。
- **Presentation（展示层）**：格式化/颜色/布局/文字。
- **Q_PROPERTY**：QObject 暴露给 Qt/QML 可绑定的属性。
- **Q_INVOKABLE**：可由 QML 调用的 C++ 方法。
- **Model**：给 ListView 等控件提供结构化列表数据的模型。
- **Role**：一行数据中的命名字段。
- **Binding（绑定）**：QML 属性绑定（property 变了自动刷新）。
- **Signal**：Qt 信号，状态变化通知。
- **Smoke Test（冒烟测试）**：快速证明最基本运行路径正常。
- **Deployment（部署）**：把应用与运行时依赖整理成用户可运行的交付目录。

---

## 5.2 最终依赖方向（Core Zero Qt 的约束意义）

```
QML / Qt Quick
      ↓
AnalysisController / TransactionListModel（App/Adapter）
      ↓
modbuslens_core（Pure C++20，Zero Qt）
```

**严格禁止** `modbuslens_core → QObject/QString/QVariant/QML`。价值：
- Core 单测不需要 GUI/事件循环；
- 协议逻辑不受 UI 框架影响；
- Replay/Simulator/Serial 共用同一 Core；
- 未来换 UI 技术不重写协议事实；
- AI/Agent 不需要从 QML 字符串反推事实。

---

## 5.3 App 启动结构（真实 main.cpp）

`QGuiApplication` → `QQuickStyle::setStyle("Fusion")` → `QQmlApplicationEngine` → `engine.loadFromModule("ModbusLens", "Main")` → `--qml-smoke-test` 分支（实例化后直接 return 0）。

**为什么不用 QApplication+QMainWindow+Qt Widgets**：T008 是真实的 bootstrap 迁移——Qt Quick/QML（ADR001）成为最终 UI，QWidget scaffold 被删除；`qt_add_qml_module` 的模块注册在 exe target 上，静态链接也不会丢模块初始化。

---

## 5.4 QML Module / CMake

- `qt_add_qml_module(modbuslens URI ModbusLens VERSION 1.0 QML_FILES src/ui/qml/Main.qml SOURCES ...)`（模块直接挂在 exe target）。
- QML 由 CMake 正式管理（不是开发机路径下的松散 Main.qml）：clean build / resource 与模块发现 / clone 后可构建 / deployment 四件事都由此保证。

---

## 5.5 AnalysisController 的角色

= Core 与 QML 之间的 **Application Adapter / Orchestration Layer**。
**不是**：Protocol Core、Transaction Analyzer、Statistics Engine、Replay Parser、Serial Protocol Parser、AI Detector。
主要负责（真实职责）：调 Core；保存当前 active batch 与 UI-facing 状态；把 Core 结果转成 Qt/QML 友好类型；接收 UI 命令；调度 source/runtime；更新 model/properties；发 notify signals；管理应用层异步状态（busy/error/cancel/失效）。

---

## 5.6 Q_PROPERTY 四类（按真实属性归类）

- **A. Deterministic facts**：observed/pending/completedCount、success/exception/crcError/timeout/protocolErrorCount、hasSuccessRate+successRate、hasAverageSuccessLatency+averageSuccessLatencyMs。
- **B. Source / transport state**：modeLabel / sourceLabel；serialConnected / serialBusy / hasSerialError / serialErrorMessage / serialPortNames；hasReplayError / replayErrorMessage。
- **C. Diagnosis / AI state**：hasBaselineDiagnosis / baselineDiagnosisText；aiConfigured / aiDiagnosisBusy / hasAiDiagnosis / aiDiagnosisText / aiDiagnosisErrorMessage / aiModelName。
- **D. Agent state**：agentBusy / hasAgentAnswer / agentAnswerText / agentErrorText / agentAvailable / cloudAiBusy（busy 全 derived：aiBusy||runtime.isBusy）。

---

## 5.7 optional → QML（无损边界）

- Core：`std::optional<double> successRate`；completed==0 → nullopt。
- Controller **不**把它偷换成 0%；用 `hasSuccessRate（bool）+ successRate（double）` 成对暴露。
- 无值时：hasX=false；value getter 即使返回 0.0 也只是技术占位（`value_or(0.0)` 注释原文），不是业务事实。
- QML 必须**先判断 hasX 再读 value**（`hasSuccessRate ? rate+"%" : "—"`），averageSuccessLatency 同理。
- 为什么：nullopt 与 0.0 语义不同（Part 3 §3.7）——这层语义不能在 UI 边界丢失；项目早期就按 hasX+value 固化。

---

## 5.8 TransactionListModel（真实类型与 roles）

- `QAbstractListModel`；`TransactionListEntry{deviceAddress, functionCode, TransactionStatus status, elapsedMs, optional exceptionCode}`；7 roles：DeviceAddress / FunctionCode / StatusCode / StatusText / ElapsedMs / HasExceptionCode / ExceptionCode。
- **为什么不用** QML JS array / QVariantList / QList<QObject*>：roles 明确、ListView 原生适配、C++ 控制事实、model 更新通知明确（beginResetModel 式整批 replace）、Presentation 文本（statusText）与 Core enum（StatusCodeRole）分离。

---

## 5.9 Core fact vs Presentation text

例：Core=`TransactionStatus::CrcError` → Adapter 映射 `"CRC 错误"` → QML 显示文字/决定颜色/布局。QML **不得**从该字符串反推 TransactionStatus。Core enum 不保存 QString / 中英文标签——所以展示文案只存在 Adapter 一处（06 政策的"展示层唯一定义点"）。

---

## 5.10 TransactionListEntry 为什么存在

`TransactionAnalysis{status,elapsed,exceptionCode}` 是最小分析事实；UI 还需要 deviceAddress/functionCode。Adapter 组合「Request metadata + TransactionAnalysis → TransactionListEntry」——**不为了 UI 需要反向污染 T007 的最小 Core model**。

---

## 5.11 Controller → Model ownership

AnalysisController 拥有 TransactionListModel（成员，随 controller 生命周期）。QML 只读取 model（data 绑定），**不**创建业务 model、不修改 model、不 append 假数据。ListView 是纯 consumer。

---

## 5.12 Simulator / Replay / Serial 共用 Dashboard（B5 核心题）

```
Simulator ─┐
Replay  ───┼→ active result batch
Serial  ───┘        ↓
               AnalysisController
                     ↓
         statistics properties + TransactionListModel
                     ↓
                   QML
```

三种来源最终结果结构相同（同 core 产出）→ 不需要 SimulatorDashboard/ReplayDashboard/SerialDashboard。T009 Part B 明确要求复用 T008 统计卡、TransactionListModel 与 Recent Transactions（真实档案决策）。

---

## 5.13 QML 不应该计算什么（责任边界）

禁止放进 QML JS：CRC 判断、FC03 decode、Timeout 判断、TransactionStatus、successRate、average latency、Exception 分类、Replay 统计、Serial framing、Baseline diagnosis、AI facts、Agent tool facts。
QML 只做：显示、布局、输入、按钮交互、格式化、有限展示状态（如占位/空态文案）。

---

## 5.14 User Command → Core（三条真实链，代码名）

- **A. Run Demo**：Button→`runDemoBatch()`→Simulator/Fault→analyzer→summarize→`applySnapshot`+`setEntries`→signals→QML。
- **B. Replay**：FileDialog→`loadReplayFile(QUrl)`→QFile read→`parseReplayLog`/`analyzeReplayLog`→同 Dashboard 发布（stats_/model/source 原子切换）。
- **C. Serial Read**：控件→`connectSerial`（openPort）+`readHoldingRegistersOnce`→adapter write→`feedResponseBytes`→`publishSerialResult`→单行 model+单元素统计→Dashboard。

---

## 5.15 QML Binding / Notify

`Q_PROPERTY + NOTIFY + QML binding`= 推式刷新：Core state 变 → Controller 属性变（emit 信号）→ QML 绑定自动更新。**UI 不需要轮询 Controller**（`statisticsChanged/sourceChanged/serial*/aiStateChanged/agentStateChanged/cloudAiChanged` 信号族承担）。

---

## 5.16 Empty / Optional / Error states（不得混淆"没有数据"与"发生错误"）

- 无事务：「暂无通信记录」；无成功率/平均延迟：`—`（hasX=false 分支，非 0%）。
- Replay error：`replayErrorMessage`（红色显示；旧 batch 保留——原子语义）。
- Serial no-port：「未检测到串口」（空列表是合法状态，**不是** transport error）。
- Serial error：`serialErrorMessage`（transport 层错误，≠ Modbus 诊断）。
- AI not configured / error：`aiDiagnosisErrorMessage`；Agent no-data / error：`agentErrorText`（provider 失败必可见——ISSUE-009）。

---

## 5.17 Serial UI Adapter（Qt/UI 边界）

`QSerialPortInfo::availablePorts()` → Controller → serialPortNames → QML ComboBox + empty state。Refresh 不 open/write/probe；Connect 才显式连接；QML 禁止自己操作 QSerialPort。

---

## 5.18 Async UI state contract（不深入 runtime）

Controller 层：busy（derived）、answer、error、cancel、batch-change invalidation。旧 batch 的 AI/Agent 结果**不能继续显示在新 batch 上**——batch 变化时 Controller 清对应视图并 emit（深层 revision/generation 机制留 B6/B7）。

---

## 5.19 Final UI structure（为什么这样划分交互区域）

- **Header/source controls**（顶部固定）：模式与来源切换——用户第一步关心的"我在看什么"。
- **Statistics cards**：批次口径总览（核心事实的即时镜像）。
- **Recent Transactions + 固定表头/稳定列**：可扫读的证据明细。
- **Diagnosis 区域**：三种"解释性产出"按 Tab 分页（基线诊断=确定性产出、AI 解释=one-shot、Agent 问答=按需只读查询）——三类语义不同所以分页，而不是同框纵向堆叠。
- **布局约束**（ISSUE-004/T013 结晶）：root 不滚、Horizontal SplitView、左 pane 内部 Flickable（长答案 containment）、右 ListView 独立滚动。

---

## 5.20 T013 UI Review Lessons（真实三段验收）

自动测试 PASS ≠ 视觉体验 PASS。项目真实经历：自动验证通过 → 人工视觉 FAIL（深色对比度/串口空态/Tab 边框/表格对齐/宽度利用，多次）→ 多轮修正 → 人工复验 → PASS。GUI 项目需要三重验收：**Automated correctness + QML runtime warning check + Manual visual review**。

---

## 5.21 Fusion Style 事实

`main.cpp` 真实存在 `QQuickStyle::setStyle("Fusion")`。设置原因不是"更好看"，而是实际证据：Windows native style **静默忽略** ScrollBar/TabButton 的 background/contentItem 自定义并抛 `checked is not defined` 运行时警告（T013 Phase E 运行日志实证，警告随 Fusion 清零）。不扩大为"Fusion 永远优于 Native"。

---

## 5.22 UI Language / Presentation Policy（06 政策）

哪些字属 Presentation：按钮/标签/状态/错误/diagnosis/统计含义→简体中文；专业实体保留（Modbus RTU/RS485/CRC/FC03/0x02/0x03/8N1/COM/ModelScope/Qwen/.mlog/ms/AI）。显示文案绝不进 Core（Core 只有 enum/事实）——因此文案改动永远不动确定性层。

---

## 5.23 QML Smoke 的边界

`--qml-smoke-test` 真实作用：启动真实 app 的 QML module、实例化、捕捉 module/type/binding/runtime 问题、成功后快速退出。**不能证明**：布局美观、字体可读、控件宽度合理、真实鼠标交互——所以不能代替人工 Review。

---

## 5.24 UI Bridge Tests（真实覆盖，按"证明什么"）

Controller initial state（UI-B01）；snapshot→properties；model row/roles（UI-A04/A05、UI-B02）；optional semantics（hasX 与占位值）；Demo command（UI-B01~B06）；Replay command/state（UI-R01~R08）；Serial state（UI-S01~S10）；AI/Agent state（UI-AI01~AI11、UI-AG01~AG20）。以上全部为真实存在测试，未虚构。

---

## 5.25 Manual UI Review 关注点（真实经验清单）

可读性、控件状态（disabled 不该像坏掉）、布局比例、表头对齐、空状态、滚动区域、不同 source mode、Diagnosis tabs。自动 PASS 与人工 FAIL 曾分离的事实（T013 候选 commit 留存）。

---

## 5.26 Development Run vs Standalone Deployment

- Development Run：依赖正确的 Qt/MinGW 开发环境（preset 注入工具链）。
- Standalone Deployment：deploy 目录含匹配运行时依赖（windeployqt + MinGW runtime），面向双击/演示。
- **不得**要求用户永久改全局 PATH 才能运行。

---

## 5.27 ISSUE-002（现象/证据/根因/修复/验证）

- **现象**：应用在开发环境可运行，但 Explorer 双击 `modbuslens.exe` 失败（无法定位 `std::pmr::get_default_resource` 于 Qt6Gui.dll）。
- **证据**：runtime provenance（编译器三件套 SHA256）比对——错误来自 PATH 中 Anaconda 旧 libstdc++/Qt 组合而非应用本体。
- **根因（已证）**：运行时 DLL 解析冲突（旧 g++ runtime 无 pmr 符号）。
- **修复**：`deploy_windows.bat` 用 windeployqt + 匹配编译器 runtime 生成独立 deploy 目录（本地 DLL 优先）。
- **验证**：provenance SHA256 + minimal-PATH smoke + **用户 Explorer 双击确认 PASS**。
- 不泛化为"Qt DLL 缺失"。

---

## 5.28 Deployment why matters

build success ≠ 可分发/独立运行。桌面项目需要：Compile + Test + **Runtime dependency closure + Deployment validation**（minimal-PATH/双击验收）。这是面试可讲的工程能力点（不是"我打包过"）。

---

## 5.29 Common Misconceptions（≥12）

1. ❌ "AnalysisController 是业务 Core。" ✅ 是 Adapter；Core 在 src/core（Zero Qt）。
2. ❌ "QML 可以直接调协议算法。" ✅ 必须经 Controller；Core 不暴露给 QML。
3. ❌ "nullopt 在 QML 直接显示 0%。" ✅ hasX 先判，无值显示 `—`。
4. ❌ "TransactionListModel 应该放进 Core。" ✅ Qt 类型只允许在 App/Adapter。
5. ❌ "QML 可以自己算 Success Rate。" ✅ 只显示 Controller 传来的 core 值。
6. ❌ "Replay 应该做第二套 Dashboard。" ✅ 复用同一 Dashboard（同 core 口径）。
7. ❌ "不同 source mode 必须做不同页面。" ✅ 只换来源标签与数据，页面同一。
8. ❌ "statusText 是事实来源。" ✅ 是展示文案；判断永远走 StatusCodeRole/enum。
9. ❌ "QML smoke PASS 等于 UI 完全 PASS。" ✅ 只证加载无错。
10. ❌ "自动测试 PASS 就不需要人工视觉验收。" ✅ T013 反例已实证。
11. ❌ "开发环境能运行就等于可发布。" ✅ 需要 runtime closure + deployment 验证。
12. ❌ "解决 DLL 问题最简单就是改全局 PATH。" ✅ 应独立 deploy 目录 + minimal-PATH 纪律。

---

## 5.30 Code Navigation

| 主题 | 位置/符号 |
| --- | --- |
| App entry | src/main.cpp：Fusion、loadFromModule、--qml-smoke-test |
| QML module | CMakeLists.txt：qt_add_qml_module（URI ModbusLens） |
| AnalysisController | src/ui/AnalysisController.{h,cpp}：runDemoBatch / loadReplayFile / connectSerial / readHoldingRegistersOnce / askAiDiagnosis / askAgent |
| TransactionListModel | src/ui/TransactionListModel.{h,cpp}：7 roles |
| Main.qml | src/ui/qml/Main.qml：统计卡/三 Tab/SplitView/Transactions |
| UI bridge tests | tests/test_ui_bridge.cpp |
| QML smoke | ctest `qml_smoke` = modbuslens --qml-smoke-test |
| Deployment | scripts/deploy_windows.bat + minimal-PATH smoke |
| ISSUE-002 | docs/issues/ISSUE-002-explorer-launch-dll-collision.md |

---

## 5.31 Self-Test（15 题，无答案）

**基础 5**
1. 依赖方向为什么必须 QML→Controller→Core？反向会破坏什么？
2. `hasSuccessRate + successRate` 的分工是什么？QML 正确读法？
3. TransactionListModel 的 7 roles 分别是什么？StatusTextRole 与 StatusCodeRole 差别？
4. Setup 里 QML 模块如何被 CMake 管理（URI/VERSION/SOURCES）？
5. `--qml-smoke-test` 具体做什么、何时 return？

**边界 5**
6. TransactionAnalysis 与 TransactionListEntry 的字段差是什么？谁补齐差额？
7. Serial no-port 空态与 Serial transport error 在 UI/语义上怎么区分？
8. 三种模式为什么共用 Dashboard？（给一个真实口径证据）
9. batch 变化时旧 AI/Agent 结果为何必须清？（只讲 UI contract）
10. statusText 为什么不能反向推断 TransactionStatus？

**追问 5**
11. QML 绑定+NOTIFY 为什么让 UI 无需轮询？（信号族怎么组织？）
12. 为什么 Fusion style 是"修复"而不是"美化"？（实际证据是什么？）
13. ISSUE-002 的根因是什么、证据链怎么来的？（不答"Qt DLL 缺失"）
14. deployment 验证的最小链条是什么？（列出各环节证明什么）
15. T013 里 automated PASS 后人工 FAIL 的具体类目至少列三个。

---

## 5.32 UNKNOWN（严格区分）

- **当前代码事实**：Fusion style；7 roles；hasX 成对属性；三 Tab；SplitView 布局。
- **历史设计**：QWidget bootstrap→QML 迁移（ADR001/T008 文档事实）。
- **视觉判断**：T013 各轮人工结论属人工验收证据（有 commit 与档案记录），不包装成自动化事实。
- **未证明 hypothesis**：Serial 当前 0 端口的原因（Part 4 §4.13 同规）。

---

*Part 5 完。后续 Part 不在本阶段创建。*---

# Part 6 — Deterministic Diagnosis / ModelScope AI Explanation（T011）

> 与代码互核：`src/core/diagnosis`（DiagnosisContext / RuleBasedDiagnosis / DiagnosisReport，Pure C++、Zero Qt、Zero Network）；`DiagnosisPromptBuilder`（detail 上限 `kMaxDetailTransactions=20`）；`ModelScopeDiagnosisClient`（`kMaxOutputTokens=768`、max_tokens 字段）；Controller：`activeBatchRevision_` + `aiRequestGeneration_` 双维 guard、Baseline First、Ask AI 仅显式触发。

---

## 6.1 中文术语表

- **Diagnosis（诊断）**：对已确定的通信事实进行归纳与解释。
- **Baseline（基线诊断）**：不依赖云模型的确定性规则结果。
- **Rule-based（规则化）**：基于明确程序规则，而非模型自由生成。
- **Finding（发现）**：诊断发现（如"存在 CRC Error"）。
- **Recommendation（建议）**：排查建议（结构化 code，非自由文本）。
- **Structured Facts（结构化事实）**：不是 UI 字符串、不是原始聊天文本。
- **Prompt**：发给模型的指令与事实上下文。
- **Provider**：模型服务提供方（本项目最终=ModelScope）。
- **One-shot**：一次独立请求，不维护多轮会话历史。
- **Stale Response**：迟到、且已不属于当前数据批次的响应。
- **Revision**：批次版本号 / 身份标识。
- **Cancel**：取消正在进行的请求。
- **Authority**：哪一层拥有某项事实的最终决定权。

---

## 6.2 Diagnosis 的 Authority（事实唯一权威）

唯一 authority=Deterministic Core：CRC 是否正确、Frame 是否可解码、FC03 是否合法、Request/Response 是否一致、Exception Code、Timeout、ProtocolError、elapsed、TransactionStatus、Statistics、Success Rate、Avg Latency。
- AI/Baseline 可以：总结事实、标记主要问题、给可能原因、给下一步排查建议。
- **不得**：改 TransactionStatus / Statistics、重新解释 CRC、把 CrcError 说成 Timeout、自动操作 Serial、自动重发、改文件。

---

## 6.3 为什么必须有 Part A Baseline

即使无 Key / 无网 / provider 不可用 / 超时 / 额度不足 / HTTP 错误，`TransactionAnalysis+Statistics → DiagnosisContext → RuleBasedDiagnosis → Baseline` 仍然产出基础诊断。**LLM = enhancement，不是 single point of failure。**

---

## 6.4 DiagnosisContext（真实构造）

由 active batch（结构化 DiagnosisTransaction 数组）经 `buildDiagnosisContext` 构造，statistics 与同批 transactions 自洽（Part 3 同原则）。Diagnosis **不允许**读 QML statusText/"CRC Error"/颜色/ListView 文本再反推事实;只直接消费 `TransactionStatus / TransactionAnalysis / TransactionStatisticsSnapshot`。

---

## 6.5 Rule-based Baseline（真实模型）

`DiagnosisFinding{code,severity,affectedCount,optional exceptionCode,recommendedActions}` + `DiagnosisReport{findings}`;ActionCode 12 值（检查供电/核对地址/串口参数/接线/接地干扰/功能码支持/寄存器表/请求参数/设备状态/文档/协议一致性/等待完成）。CRC Error / Timeout / Exception(0x02→register map/address 类建议) / ProtocolError 各自形成确定性 finding;固定 finding 顺序（Protocol→CRC→Timeout→Exception→Pending→Healthy/NoData）——不是捍卫等级，只是确定性展示顺序。没有"健康分数"、没有自由文本专家系统。

---

## 6.6 Baseline 为什么不直接输出自由文本

structured findings + recommendation codes：稳定、可测、可统计、UI 可映射、AI 可消费、不靠字符串反推。Presentation（formatter）最后才翻译成中文可读文本（T013 已有术语校准）。

---

## 6.7 Baseline 与 UI

`Active Batch → DiagnosisContext → RuleBasedDiagnosis → DiagnosisReport → Controller(baselineDiagnosisText_) → QML 基线诊断 Tab`。QML 只显示,不运行诊断规则。

---

## 6.8 ModelScope Provider 边界

- Provider=ModelScope API-Inference;client=`ModelScopeDiagnosisClient`;协议=OpenAI-compatible Chat Completions。
- **"OpenAI-compatible" 只代表 HTTP/JSON API 形式兼容,不代表调用 OpenAI 服务**(Provider 与协议格式是两件事)。

---

## 6.9 为什么不提前做 multi-provider abstraction

当前只有一个真实 Provider(ModelScope)→ 直接 `ModelScopeDiagnosisClient`,不提前 IModelProvider/Registry/Factory/多厂商框架。原则:先解决真实需求;第二个真实 Provider 出现时再按真实共性抽象(T011 真实决策)。

---

## 6.10 Core Zero Network

`src/core/diagnosis` 继续 Pure C++20 / Zero Qt / Zero Network;QtNetwork/QNetworkAccessManager 只在 App 层。HTTP 是外部基础设施,不是 Modbus 诊断事实。

---

## 6.11 ModelScopeDiagnosisClient 职责

只做:构造 Chat Completions HTTP request(QNetworkAccessManager POST)→ timeout/cancel → HTTP/provider 解析 → success/error signal。**不得**:读 TransactionListModel、重判 CRC、算统计、构造新协议事实、操作 Serial、切 Source、读任意文件、做 Tool Calling。

---

## 6.12 Ask AI 必须显式用户操作

Run Demo / Load Replay / Serial complete / Run Baseline **都不自动调 AI**;只有用户显式 Ask AI 才发外部请求。理由:API cost、privacy、predictability、demo stability、user control。

---

## 6.13 Baseline First（真实前置）

允许 Ask AI 的真实 precondition(controller 重校验,不只靠 QML disabled):`aiConfigured && 有 active batch && hasBaselineDiagnosis && !busy(T012 后还有单飞 guard)`。典型路径:Run/Load/Serial → Run Baseline → Ask AI。deterministic facts first,AI explanation second。

---

## 6.14 Empty Batch

无 active analysis data 时 Ask AI **不发网络请求**、返回本地 application error——不为 No Data 浪费 ModelScope request。

---

## 6.15 Prompt Authority（真实 builder）

Prompt 明确要求:deterministic facts are authoritative;不得 recalculate/contradict/override/claim certain root cause。模型不能把 CrcError 写成 Timeout(ISSUE-006 又加了 evidence scope/状态正例语义/混合独立性,术语政策再加语言指引——全部不动 authority 底座)。

---

## 6.16 Prompt Input(真实内容面)

基于 DiagnosisContext+DiagnosisReport:transaction status、device address、function code、elapsed、exception code、聚合统计、baseline finding codes/counts、recommendation codes。禁/避发:raw wire、QML text、文件名、Replay comments、路径、任意本地文件、无关 COM description。

---

## 6.17 为什么 AI 不需要 raw Modbus bytes

CRC/FC03/事务分类已在 Core 完成;给模型 raw bytes 会引入重复判断/幻觉/prompt 膨胀/暴露增加/Core authority 模糊。模型消费"已验证后的结构化事实"。

---

## 6.18 Bounded Prompt（真实=20）

`kMaxDetailTransactions=20`:统计与 finding counts 永远全量(代表整批),transaction detail deterministic truncation(先失败类、原序;detail 高于 20 时打 details_truncated=true)。理由:token 上限稳定、整体事实不丢(Part A 定案)。

---

## 6.19 One-shot AI

每次 Ask AI = 独立请求;无 conversation history/thread/prev-response linkage。T011 是"解释当前批次",不是聊天机器人——状态简单、更可复现、batch ownership 清晰、stale guard 易证。

---

## 6.20 Request Shape(真实)

POST `https://api-inference.modelscope.cn/v1/chat/completions`;Authorization Bearer(仅此一处);`model`(env override 或 Qwen/Qwen3.5-27B);`messages=[system,user]`;`stream=false`;`max_tokens=768`。不是早期被替换的 Responses API。

---

## 6.21 Response Parser

`choices[]→message→content`(字符串);**HTTP 200 仍可能**:malformed JSON/empty choices/missing message/empty content → InvalidResponse。

---

## 6.22 reasoning_content

产品只用 final `content`;不显示/不保存/不拼入诊断。只有 reasoning 而无 content → 按实现为 InvalidResponse(ISSUE-008 hardening 后明确)。

---

## 6.23 Credential Boundary

`MODELSCOPE_API_KEY` 仅进程 env(只读判断 configured);不写 Git、不进 QML、不进日志、不进 issue/devlog、不打印 Authorization/完整 headers;自动测试绝不使用真实 Key(BYOK)。

---

## 6.24 Production endpoint / test endpoint

Production=固定官方 endpoint(**禁 env override**——防 token 被发往恶意 endpoint);Test=constructor seam 注入 localhost fake endpoint。这是 T011 的 exfil 防线。

---

## 6.25 Local Fake HTTP Server

localhost fake server + 真实 QNetworkAccessManager 可证:POST/headers/JSON body/解析/HTTP 映射/timeout/cancel/异步完成/stale 行为——同时不公网、不 quota、无真实 secret。

---

## 6.26 AI Error Mapping(真实枚举)

`AiDiagnosisErrorCode`:NotConfigured / InvalidConfiguration / NoData / BaselineRequired / Busy(本地前置)+ NetworkError / Timeout / Unauthorized / RateLimited / ProviderRequestError / ServerError / InvalidResponse(provider/网络)。Cancelled **不是错误**——是静默控制路径。

---

## 6.27 Provider Failure 不破坏 Baseline

Ask AI 失败只写 error 状态;Baseline 不清除、Dashboard facts 不变、rows 不变。外部 request 失败不能破坏本地 deterministic result(ai01/ai05 等 UI 测试锁定)。

---

## 6.28 Same-batch retry 语义(真实=keep old text)

同一 batch 已有成功 explanation,再 Ask AI 失败时**保留旧成功答案**(`keep old text` 注释与 UI-AI 测试锁定);不受新 error 影响自动清除错误由下一次 accepted run 清。

---

## 6.29 Batch change invalidation(stale 核心)

场景:Replay A → Ask AI(请求在网)→ 成功切 Simulator B → A 的 explanation 不得覆盖 B。`invalidateAiForBatchChange()`(成功发布批时才触达)做:++activeBatchRevision_、abort in-flight AI(BatchInvalidated)、清 view、emit。

---

## 6.30 Abort 为什么还不够

abort 只是"发出取消",异步世界仍有:回调已排队/取消与完成竞争/响应已到/回调晚执行——所以发布前**必须再验 captured revision == current**,否则丢弃。

---

## 6.31 Generation / revision guard(T011 真实机制,勿与 T012 混)

Controller 双维:`activeBatchRevision_`("还是同一批数据吗?")+`aiRequestGeneration_ / activeAiRequestId_ / requestBatchRevision_`("还是最新那次请求吗?")。capture 在发起时;发布前双检;任一不匹配=丢弃。T012 的 agentRequestGeneration_ 是平行机制,不可混写。

---

## 6.32 成功 vs 失败 source switch

成功切换(新 batch 真变了)必须 invalidate 旧 AI state;Replay load 失败 / Serial 失败→active batch 没变→**旧有效 AI state 不被无故清除**("attempted switch"≠"batch changed",UI-AI07/r-系列锁定)。

---

## 6.33 Cancel

Cancel AI:用户动作→client cancel(静默)→busy 清→**保留**旧 answer/不写红色错误(UI-AI11 cancel/stale 锁定)。它与 provider timeout/error 语义不同。

---

## 6.34 AI 永远不能修改 Dashboard(脑内测试)

Core:Success=1/Exception=1/Crc=1/Timeout=1/rate=25%。模型说"没有 CRC Error,主要是 Timeout"→ Dashboard 仍是 Crc=1/Timeout=1/25%。**AI 文本可以错,Core facts 不变**(ai10 测试)。

---

## 6.35 Live ModelScope Evidence(真实历史保留)

- Attempt#1=BLOCKED(insufficient balance)——**历史保留**;它同时证明 provider 失败不破坏 Baseline/Dashboard/Transactions。
- Attempt#2=PASS:真实端到端 Context→Prompt→ModelScope→Parser→Controller→QML 成立。

---

## 6.36 LLM Over-inference(真实观察)

模型曾输出比事实更强的因果表述(把多类失败归结为单一 signal integrity 问题,如"rather than"句型)。这属于 **prose quality issue**,不是 deterministic corruption——因为 0x02/CRC/Timeout 事实仍独立存在。面试金句:**AI can be wrong in prose without corrupting protocol facts**(ISSUE-006 治理后用 evidence scope/正例语义/混合独立/术语政策收口)。

---

## 6.37 Baseline 与 AI 的 UI 关系

Diagnosis 三 Tab:基线诊断(确定性)/AI 解释(one-shot)/Agent 问答(只读工具)。本 Part 只讲前两者:AI text **不覆盖** baseline text,是两个独立视图/结果。

---

## 6.38 T011 与 T012 的边界

- T011:one-shot explanation、无 tools、无 function calling、无 tool loop、无 Agent action。
- T012:native tool calling、读 structured immutable snapshot、本地只读 tools、多轮 tool loop(有界)。
- 都调 ModelScope ≠ 同一层。

---

## 6.39 Tests(真实覆盖,按"证明什么")

- test_diagnosis:规则矩阵(NoData≠Healthy、Pending≠failure、异常码映射、finding 顺序)。
- test_ai_client:prompt authority/bounded、request shape、parse success、invalid response、401/403、429、server error、reasoning ignored、cancel、timeout、request identity、ISSUE-006 B14~B17。
- test_ui_bridge:UI-AI01~AI11(AI state/invalidation/failed switch/cancel/stale/never-changes-facts)+UI-AG 集成。
- fake server 脚本化回合。仅列真实存在项。

---

## 6.40 Common Misconceptions(≥15)

1. ❌ "AI 负责检测 CRC。" ✅ Core 负责;AI 只能解释。
2. ❌ "有 AI 就不需要 Baseline。" ✅ 反向:Baseline 先、AI 后。
3. ❌ "Baseline 是 AI 失败的 fallback 文本。" ✅ 独立确定性路径。
4. ❌ "Serial 每笔完成后自动 Ask AI。" ✅ 绝无自动触发。
5. ❌ "LLM 应直接看 raw wire 自己诊断。" ✅ 结构化事实已足够、且防幻觉。
6. ❌ "模型输出可以修正 TransactionStatus。" ✅ 永不。
7. ❌ "模型与 Dashboard 冲突时信模型。" ✅ 信 Core。
8. ❌ "AI error 应清空 Baseline。" ✅ 独立保留。
9. ❌ "HTTP 200 就一定成功。" ✅ 还需 message/content。
10. ❌ "choices[0] 永远可用。" ✅ 需判空/非 object。
11. ❌ "reasoning_content 应展示给用户。" ✅ 只用 final content。
12. ❌ "abort 了就不需要 revision guard。" ✅ 双保险缺一不可。
13. ❌ "尝试切 source 就清旧 AI。" ✅ 成功切批才清;失败保留。
14. ❌ "OpenAI-compatible=调用 OpenAI。" ✅ 协议形式≠服务商。
15. ❌ "T011 已经是 Agent。" ✅ one-shot 无 tools,不是 Agent。

---

## 6.41 Code Navigation

| 主题 | 位置/符号 |
| --- | --- |
| DiagnosisContext | src/core/diagnosis/DiagnosisContext.{h,cpp}:`buildDiagnosisContext` |
| Rule-based Baseline | src/core/diagnosis/RuleBasedDiagnosis.{h,cpp}:`diagnoseTransactions`,`DiagnosisReport` |
| Prompt | src/ui/ai/DiagnosisPromptBuilder.{h,cpp}:`buildDiagnosisPrompt`,`kMaxDetailTransactions=20` |
| AI client | src/ui/ai/ModelScopeDiagnosisClient.{h,cpp}:`requestDiagnosis/cancel`,`kMaxOutputTokens=768` |
| Controller AI | src/ui/AnalysisController.cpp:askAiDiagnosis/cancelAiDiagnosis/invalidateAiForBatchChange/handleAi* |
| QML | Main.qml Diagnosis 区域(基线诊断/AI 解释 Tab) |
| Tests | tests/test_diagnosis.cpp;tests/test_ai_client.cpp;tests/test_ui_bridge.cpp(UI-AI*) |

---

## 6.42 Self-Test(15 题,无答案)

**基础 5**
1. Diagnosis 全部事实的唯一 authority 是什么层?列 5 个 AI 不能改的事实。
2. Baseline 的 Finding/Report/Action 结构是什么?为什么不是自由文本?
3. buildDiagnosisContext 的输入是什么?它为什么自洽?
4. Prompt 里"authoritative"句子防止什么?
5. Ask AI 触发需要哪几个前置条件?

**边界 5**
6. 空 batch 为什么零网络请求?错误落在哪层?
7. 为什么 HTTP 200 仍可能 InvalidResponse?(列四个形状)
8. reasoning_content 与 content 同时存在时产品取谁?只有 reasoning 时呢?
9. 为什么 raw bytes 不进 prompt?(至少四条理由)
10. detail=20 截断时,什么仍然代表整批?截断标记是什么?

**追问 5**
11. activeBatchRevision_ 与 aiRequestGeneration_ 各防什么问题?能合并成一个吗?
12. abort 之后为什么还要 publish 前校验?
13. Replay 加载失败与成功切换对旧 AI 状态的差别是什么?为什么?
14. Live attempt#1(insufficient balance)证明什么、没证明什么?
15. 模型把三种失败归为单一信号完整性问题时,产品哪部分错了、哪部分没错?(ISSUE-006/PART 6 视角)

---

## 6.43 UNKNOWN(严格区分)

- 代码事实:768 tokens、20 detail、双维 guard、Baseline First、BYOK。
- Provider contract:OpenAI-compatible Chat Completions(Gate 0/请求形状 live 实证)。
- 真实 live evidence:Try#1 BLOCKED(余额)、Try#2 PASS。
- 模型质量观察:over-inference 属 prose 问题(ISSUE-006 治理)。
- 未证明:ISSUE-008 历史 InvalidResponse 的 exact producer(①~④);LLM 的 possible cause 不是项目证明的 root cause。

---

*Part 6 完。后续 Part 不在本阶段创建。*---

# Part 7 — Agent / Tool Calling Knowledge Ownership（T012）

> 与代码互核:`kMaxAgentToolRounds=3`、`kMaxAgentTotalToolCalls=6`、`kMaxRecentAnomalies=20`、`kMaxAgentQuestionChars=1000`(AgentRuntime.h / AgentTools.h);测试实数是 test_agent_tools=10 个 a*,test_agent_runtime=26 个 b*(B01~B25 区间含新增),test_agent_integration=22 个 ag*(AG01~AG22)。ADR002 + ISSUE-006/007/008/009 为本 Part 权威参考。

---

## 7.1 Agent vs LLM(为什么 T012 算 Agent,而 T011 只是 AI Explanation)

- **T011**:structured deterministic facts → **one-shot** LLM 请求 → explanation。没有 tools、没有 tool_calls、没有本地执行、没有多轮 loop。
- **T012**:user question → model 决定是否需要工具 → assistant.tool_calls → 本地 C++ 校验/执行 → role=tool → 模型继续 → final answer。
- **LLM 是 Agent 的 reasoning/decision component,但 LLM != Agent**。完整 Agent = LLM + AgentRuntime + Provider Adapter + Tool Schemas + Validator + Dispatcher + Tool Layer + State/Budget/Stale Guards。**"接了 Qwen,所以就是 Agent"是错误表述。**

---

## 7.2 Final Agent Architecture(逐层职责)

```text
QML Question                        ← 输入;只传文本
  ↓
AnalysisController::askAgent        ← 应用层前置(单飞/空批/未配置)+ snapshot 构造 + 结果/错误发布
  ↓ makeAgentToolContext            ← immutable 快照(含 captured revision)
AgentRuntime                        ← FSM/轮次/总调用预算/整批校验编排/消息历史/双 guard 应用
  ↓ requestRound
ModelScopeAgentClient               ← 每轮 Chat Completions POST(返回完整 assistant message 对象)
  ↓
Qwen native tool_calls              ← model 只能"请求"工具,不能执行程序
  ↓ strict C++ validation           ← 白名单/参数/预算/ID 唯一,先全验后执行
  ↓ dispatchAgentTool               ← enum + explicit dispatcher(无 registry)
  ↓ 3 read-only tools               ← 本地确定性查询,输出 typed JSON DTO
  ↓ role=tool(tool_call_id 原样)
  ↓ next request → final content
AnalysisController                  ← handleAgentCompleted/Failed/Cancelled → answer/error/busy
  ↓ PlainText QML                   ← 只展示
```

每层**不负责什么**:AgentRuntime 不是协议 detector;Dispatcher 不是 LLM;Client 不解析业务事实;QML 不执行任何工具。

---

## 7.3 Authority Boundary(Agent is not detector)

Deterministic Core 唯一负责:CRC correctness、Frame validity、FC03 semantics、request/response consistency、TransactionStatus、Timeout、ProtocolError、Exception Code、elapsed、Statistics、success rate、latency。
Agent/LLM 可以:read / summarize / compare / reason over supplied facts / explain / suggest checks。
**不得**:重判 CRC、改 TransactionStatus、重判 Timeout、改 Statistics、改 Exception Code、制造不存在的确定性协议事实。
**Tool Calling 没有改变 T011 已建立的 authority boundary**——它只是给"读取事实"增加了受控通道。

---

## 7.4 三个 Tool 最终语义(以当前代码为准)

### A. get_session_summary()
- 无参数;零联网;不访问 Controller live state(只读 snapshot)。
- 输出:observed/completed/pending + 五分类 count + transaction_count + optional success_rate / average_success_latency_ms(无值省略字段,绝不伪造 0)+ `evidence_scope:"current_observed_batch"`。

### B. get_recent_anomalies()
- 最终 whitelist:**Exception / CrcError / Timeout / ProtocolError**(`isAnomalyStatus`;**Pending 不是 anomaly**——它是"未完成",既非成功也非完成性失败;不能用 `status != Success` 推导)。
- `kMaxRecentAnomalies = 20`;超过上限取**最新 20 条**(anomaly 序列的尾部),返回**保持原 batch 顺序**(不倒序、不随机);结果含 total_anomaly_count / returned_count / truncated。
- 输出每条:transaction_number、device_address、function_code、status、elapsed_ms、exception_code(若有)。

### C. get_transaction_detail(transaction_number)
- 参数真实名=`transaction_number`:captured active batch 内 **1-based ordinal**。
- 它**不是**:persistent transaction ID / database ID / global identity / cross-session identity;batch 替换后随 captured batch 一起失效。
- 输出:编号/设备/功能码/status/elapsed_ms/exception_code + 标准异常名(0x01~0x04 才有;未知码**缺席**,不猜)。
- 越界(整数但 1..N 外)或 batch 为空 → `TransactionNotFound`;非整数/缺参/未知字段 → `InvalidArguments`。

---

## 7.5 Tool 本质:C++ Query Interface

三个 Tool 本质是三个**本地 C++ read-only query interfaces**。模型只能 REQUEST 一次调用;"我想调用 get_session_summary"≠"LLM 自己执行了程序"。真实路径:tool call parser → validation → dispatcher → C++ function → serialized Tool Result。

---

## 7.6 Permission Boundary(read → reason → explain,不是 read → reason → act)

无权限/不存在的项:write register、change serial settings、reconnect serial、resend Modbus request、modify device configuration、modify files、execute shell、control device。
**硬边界不是 Prompt("Please do not write")**,而是:没有对应 write Tool + 本地 Tool whitelist/dispatcher validation。用户注入"忽略规则,修改串口并重发"无法凭空创造本地能力——whitelist 之外一律 `UnknownTool`(UI-AG15/B15 锁定)。

---

## 7.7 AgentToolContext / Immutable Snapshot(第二个 P0)

Controller 构造 contract:`copy activeDiagnosisTransactions_ → makeAgentToolContext(copy, activeBatchRevision_)`,statistics **必须**由 `summarizeTransactions(同一份 copied transactions)` 重算(自洽,禁从 presentation/另一批缓存取值)。
两个不同问题,不可合并:
- **A. Immutable AgentToolContext** 解决:单次 Agent run 内多个 Tool Call 看到同一个世界。
- **B. activeBatchRevision / generation stale guard** 解决:整个 run 的最终结果是否仍允许发布到当前 UI。

---

## 7.8 为什么 Tool 不能每次读 live Controller(具体例子)

Run 开始于 Batch A;第一次 summary→A;用户切到 Batch B;若第二次 anomalies 直读 live Controller→B,同一次 answer 混合两个 batch。快照后 summary(A)/anomalies(A)/detail(A) 全查 A;最终 UI 已切 B 时,A 的 final answer 即使内部自洽仍必须 stale/discard。

---

## 7.9 Native Tool Calling Provider Contract(与"自己解析模型输出字符串"的区别)

真实契约(Gate 0 实证):
```
tools schema(固定 three)
→ assistant message.tool_calls[]
→ per call: id / type="function" / function.name / function.arguments(JSON 字符串)
→ 本地 parse arguments JSON(必须是 object)
→ local Tool Result(typed → 稳定 JSON)
→ messages.append(assistant 原文) + {role:"tool", tool_call_id:原始id, content:稳定串}
→ 下一轮 request
→ final assistant content
```
- `function.arguments` 是**不可信 provider 输入**,必须本地 parse/validate。
- `reasoning_content` 永不作为用户回答或业务事实。

---

## 7.10 Gate 0 的意义

正式实现 Agent Runtime 前先做 Provider Capability Probe,因为一次 HTTP request 不能证明完整 native tool calling:
- Request #1(question+tools → native tool_calls)只证明 provider 接受 schema 并能回 tool_calls。
- 完整 round trip = 本地 Tool Result 回传(Request #2:assistant tool_call history + role=tool + matching tool_call_id → usable final content)。
最终实证:ModelScope API-Inference + Qwen/Qwen3.5-27B,**native round trip PROVEN**(Request#1 tool_calls + Request#2 final content 引用 observed=4/timeout=1)。

---

## 7.11 AgentRuntime FSM(真实流程,非 enum 名字重点)

start run → send request → model response:
- usable final content → complete;
- tool_calls → parse all → validate all → budget check → execute allowed tools → append role=tool results → next request。
直到 final answer / bounded failure / cancel / stale invalidation。状态族:`Idle / WaitingForModel / ExecutingTools / Completed / Failed`。

---

## 7.12 Multiple Tool Calls

一次 assistant response 可以含多个 tool_calls;v1 允许(全部只读+同一 immutable snapshot)。原则:**parse all → validate all → budget check → execute**;任一 call malformed/unknown/invalid args/重复或缺失 tool_call_id/budget 超限 → **整个 batch 零部分执行**。即使现在只读,仍采用 transaction-like all-valid-before-execute 语义,防止未来带状态工具的半执行状态。

---

## 7.13 Runtime Budget(只写最终实现)

- `MAX_TOOL_ROUNDS = 3`;`MAX_TOTAL_TOOL_CALLS = 6`。
- Tool Round = 一个含 tool_calls 的 assistant response(不论 N 个 call)= 1 轮。Total Tool Calls = run 内本地 Tool Call 总数。
- 例:一轮含 summary+anomalies+detail(2) = 1 round / 3 calls。
- **rounds 停在 3 管 provider loop 深度;total 从 3 到 6 管本地只读查询量**(reasoning 见 §7.14,不是"为了测试过")。

---

## 7.14 ISSUE-007 真实工程故事(本 Part 重点)

1. 第一次 Live Agent:合法 multi-step 问题 → `ToolCallLimitExceeded` → FAIL。
2. 系统表现:无限循环 ✗、crash ✗、修改事实 ✗、绕过 guard ✗——**正确 fail closed**。
3. RCA:`MAX_TOTAL_TOOL_CALLS=3` 对自然 bounded multi-step read-only diagnosis 过严。这是 **Agent orchestration / planning budget issue**,不是 Core/deterministic data/QML/provider 问题。
4. 修复:rounds 保持 3;total 3→6;+ Tool Efficiency/Planning Discipline(最少调用、不重复查询已有聚合、summary/anomalies 各至多一次、detail 仅相关时调、事实足够立即 final)。
5. **安全 guard 未删除**:没有 unlimited、没有自动 retry、没有加 round 深度、没有写权限扩张;只是区分 provider loop depth vs local read-only query count。
6. 同题 Real Agent Live Re-Smoke → PASS。

---

## 7.15 不得编造 exact Tool Sequence

production UI/日志不暴露 exact tool sequence → **not externally observable**(如实写)。不得编"先 summary 再 anomalies 再 detail(2)…";文档必须区分已证明 vs 合理推测(ISSUE-008/009 同纪律)。

---

## 7.16 Stale Guard / Run Identity(以最终代码为准)

- `capturedBatchRevision`(并入 `AgentToolContext`)解决"这个 run 属于哪一批数据"。
- `runGeneration_ / currentAgentGeneration_`(Controller 的 `agentRequestGeneration_` 单调 ++ 产生 run 身份)解决"同一 batch 上更新的 run 是否 supersede 旧 run"。
- publish 前条件:`captured==currentBatchRevision` 且 `runGeneration==currentAgentGeneration`;否则静默丢弃。
- T011 的 AI generation 与 T012 的 Agent generation 是**平行概念,不共享同一个 request ID**。

---

## 7.17 Cancel / Batch Invalidation(四种形态不是同一种错误)

- user cancel → 静默、busy 清、保留旧 answer、无红错误;
- provider failure → 可见错误文案(八类映射,ISSUE-009);
- batch invalidation → `invalidateForBatchChange()`:先使旧 run identity 不可发布(++generation)再 cancel(BatchInvalidated)→ 零用户可见信号;迟到 callback 无法发布;
- newer run supersede → 旧 run 被新 run 顶替(产物只认最新 generation)。

---

## 7.18 T011 vs T012 对照

| | T011 AI Diagnosis | T012 Agent |
| --- | --- | --- |
| 输入 | 固定结构化上下文 | 用户自由问题 |
| Tools | 无 | 3 read-only(原生 tool calling) |
| 循环 | one-shot | 有界多轮(≤3 rounds/≤6 calls) |
| 事实获取 | 一次性全部注入 | 按需动态查询 snapshot |
| 前置 | Baseline First | 仅 configured+非空批+单飞 |
| 输出 | explanation | final answer |

共同点:同一 provider;deterministic facts authoritative;不篡改事实;stale guard;外部失败不破坏 Core。**都调 ModelScope ≠ 同一层/同一功能。**

---

## 7.19 Agent Prompt / Question Boundary(最终指令要点)

- read-only diagnostic agent;deterministic tool results authoritative;use provided tools;never invent tool/capability;no false action claims;current observed batch only;no long-term reliability generalization;mixed anomaly types independent unless evidence proves otherwise;do not reinterpret CRC/status/exception/statistics;Tool Efficiency/Budget discipline。
- 用户问题:`kMaxAgentQuestionChars=1000`;空/纯空白→`InvalidQuestion`(Runtime 权威,零网络,不截断)。

---

## 7.20 0x02 / Register Address Limitation(最终数据模型)

Agent detail 不含 FC03 startAddress/quantity(未 enrichment)。因此可以说:0x02=Illegal Data Address + 建议检查 register map/地址配置;但**不能**声称某一具体寄存器地址错误(如"40017 不存在")。保留为 known limitation/future enhancement。

---

## 7.21 ISSUE-008 / ISSUE-009 UNKNOWN Discipline

- ISSUE-008:历史 InvalidResponse 的 exact producer(Client ①~③ / Runtime ④)= **UNKNOWN / not proven**;desired final-answer contract 已 HARDENED;同场景重放 NOT REPRODUCED。
- ISSUE-009:历史 silent breakpoint = **UNKNOWN**;visibility contract 已 HARDENED(ag21/22);quota reproduction count=0。
- 不得为"漂亮 RCA"补根因;区分 known code paths / hypothesis / live observation / proven / not proven。

---

## 7.22 Tests as Knowledge Evidence(真实代表案例)

- Tool 层(a01~a10):summary deterministic、recent anomaly selection(latest-20 原序)、detail、not found、unknown tool、invalid arguments、read-only whitelist、deterministic 重放、snapshot isolation、snapshot builder 自洽。
- Runtime(b01~b25):direct final、one tool round、multiple calls、malformed、unknown tool、round/call budget(7 calls 零执行)、batch 切换 two-window、supersede、cancel、provider failure、facts 不变、injection 无法造写、no config、question 校验、reasoning-only(B24)、non-string content(B25)、multi-tool plan(B23)。
- UI(ag01~22):availability/busy/answer/error/cancel/batch invalidation/source switching/rows+statistics unchanged/429 与 malformed-200 可见/ST-A 不可表达的 controller 侧处理。

---

## 7.23 Final Live Evidence 层级

A. Part A 只读确定性工具(全测试);B. Gate 0 真实原生能力 PROVEN;C. Phase 1 Runtime 自动测试;D. Phase 2 Controller+QML;E. Offline Manual UI Smoke;F. 第一次真实 multi-step Live FAIL(ToolCallLimitExceeded);G. budget refinement(3→6+discipline);H. 同题 Real Agent Live Re-Smoke PASS。
有价值的是 **failure→evidence→RCA→constrained fix→same-scenario re-validation**,不是"最后 PASS"。

---

## 7.24 面试讲法(可复述版)

- 15 秒:T011 是一键解释已确定事实;T012 是让模型在严格只读边界内决定"查什么"(三个 C++ 工具+白名单+预算+快照),回答用户自由问题。
- 45 秒:补"Agent=LLM+Runtime+校验+Dispatcher+只读工具"与权限边界(写能力类型层面不存在)。
- 2 分钟:补 Gate 0 实证、immutable snapshot、3/6 预算来源(ISSUE-007 fail-closed→RCA→修复→同题重验)、stale guard、0x02 能力边界。
- 追问知识点(能答):Agent≠聊天;工具由 C++ 执行;模型不能控串口;只读理由;快照与 revision 都要;多调用合法;不部分执行;3/6 由来;3/3 不是 provider bug;transaction_number 非 ID;0x02 无具体寄存器;不污染 Dashboard;injection 不增权;模型胡说→事实不动。

---

## 7.25 Misconceptions(15 条:错误→错在→正确设计)

1. "用了 Qwen 就叫 Agent" → LLM 只是 reasoning 组件 → Agent=LLM+Runtime+校验+工具层。
2. "模型自己执行 Tool" → 模型只输出 tool_calls → C++ dispatcher 执行。
3. "Tool Call 合法就不需要本地 validation" → provider 输出不可信 → whitelist/参数/预算三关。
4. "Prompt 说只读就足够安全" → 文字不是边界 → 无 write Tool+whitelist 才是硬边界。
5. "所有 non-Success 都是 anomaly" → Pending 是未完成 → 白名单四态。
6. "transaction_number 是永久 ID" → batch-scoped 1-based ordinal。
7. "Snapshot 和 revision guard 是一回事" → 前者 run 内世界一致,后者能否发布。
8. "有 round limit 就不需要 call limit" → 深度 vs 总量两维。
9. "multiple tool calls 一定不安全" → 全只读同快照下合法(仍整批先验)。
10. "第一个合法 Tool 可先执行,后面失败再说" → 零部分执行的 transaction-like 语义。
11. "3→6 等于取消安全限制" → 只读查询总量(ISSUE-007)非写权限。
12. "Agent 可以重新判断 CRC" → Core 唯一权威。
13. "0x02 就能知道具体错误寄存器" → 无 startAddress/quantity,只能建议核对。
14. "Provider failure 可以清掉 deterministic facts" → 只写 Agent 错误视图。
15. "T011 与 T012 只是 UI 名字不同" → one-shot vs 有界工具循环,架构不同。

---

## 7.26 Self-Test(15 题,无答案)

**基础 5**
1. T011 与 T012 各把 LLM 放在哪个位置?三层余下各包括什么?
2. 三个 Tool 的输入/输出/权限分别是什么?
3. get_recent_anomalies 为何把 Pending 排除?latest-20 的取值方向与返回顺序?
4. transaction_number 与"ID"的区别?
5. 权限边界为什么不是 Prompt,而是类型/白名单?

**边界 5**
6. 快照为何要 summarize 同一份 copy?给一个"混批"具体反例。
7. multiple calls 的 parse→validate→execute 顺序;若第三 call 非法,前两个执行了吗?
8. 3 rounds / 6 calls 各自管什么?一个含 4 个调用的响应算几轮几个 call?若超载如何?
9. Provider 200 但 content 空且 tool_calls 空会怎样?reasoning-only 呢?
10. stop 未到、batch 切换:谈快照层与 revision 层的两段应对。

**追问 5**
11. ISSUE-007 里 fail closed 具体保护了什么?为什么修复不加 round 深度?
12. 为什么 3/3 预算失败不是 Provider bug?(最优解出一道 evidence 问题)
13. 自己如何向面试官证明"注入不能新增写权限"?(三层防御)
14. 为什么 batch 替换后 transaction_number 就失效?跨批问题为何 v1 不支持?
15. 0x02 已知什么/未知什么?从哪行代码界出?(ISSUE-008 视角总结)

---

## 7.27 Code Navigation(B7)

| 入口/层 | 位置与符号 |
| --- | --- |
| 用户问题入口 | AnalysisController::askAgent(前置四级 guard) |
| Controller Agent 状态 | agentBusy(answer) / hasAgentAnswer / agentErrorText / agentAvailable / cloudAiBusy(全 derived) |
| Snapshot | AgentToolContext.h:makeAgentToolContext |
| Tool Context | src/ui/agent/AgentToolContext.{h,cpp} |
| Tool 实现 | src/ui/agent/AgentTools.{h,cpp}:dispatchAgentTool/validateAgentToolCall, kMaxRecentAnomalies=20 |
| Prompt | src/ui/agent/AgentPromptBuilder.{h,cpp} |
| Provider client | src/ui/agent/ModelScopeAgentClient.{h,cpp}:requestRound/cancel(ISSUE-005 契约复刻) |
| Runtime | src/ui/agent/AgentRuntime.{h,cpp}:start/cancel/invalidateForBatchChange, 3/6 常量, 空题 1000 |
| QML | Main.qml「Agent 问答」Tab |
| Tests | test_agent_tools / test_agent_runtime / test_agent_integration / test_ui_bridge |
| 档案 | docs/tasks/T012-agent-tools.md;ADR002;ISSUE-007/008/009 |

---

## 7.28 Evidence Classification(本 Part 最后一道纪律)

- Known from current code:常量、自洽快照、whitelist、双 guard、错误枚举、验证顺序。
- Known from automated tests:62 个 agent 相关测试函数(tool 10 + runtime 26 + integration 22;ui_bridge 交错)。
- Known from real provider probe:Gate 0 native round trip PROVEN。
- Known from real Live Agent evidence:第一次 FAIL(ToolCallLimitExceeded)、同题 re-validation PASS。
- Historical failure observation:3/3 预算下 fail-closed(有 UI 文案证据)。
- Hypothesis only:ISSUE-008 的 exact producer(①~④)、ISSUE-009 的断点("额度响应超表"假设)。
- Not externally observable:exact tool sequence、exact provider request count。
- Future enhancement / out of scope:startAddress/quantity 详情、第 4 个工具、多 provider。

---

# Phase B Knowledge Ownership Closure

B1 Architecture / B2 Modbus Protocol Core / B3 Transaction·Statistics / B4 Execution Modes / B5 Qt·QML Adapter / B6 Baseline·AI Diagnosis / B7 Agent·Tool Calling —— **均已完成知识证据归档**(docs/08_KNOWLEDGE_OWNERSHIP.md Part 1~7)。

- B8:NOT DEFINED / NOT CREATED(不硬造)。
- Next:M8 Interview / Portfolio Packaging —— 等待用户批准,不自动开始。