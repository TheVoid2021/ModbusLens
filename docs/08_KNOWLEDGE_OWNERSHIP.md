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

*Part 4 完。后续 Part 不在本阶段创建。*