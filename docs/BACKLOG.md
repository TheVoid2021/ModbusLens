# BACKLOG — 任务清单与里程碑

> 规则：每个任务完成后更新（状态、依赖、优先级）；任务档案在 `docs/tasks/`。
> 状态取值：`Done` / `In Progress` / `Ready` / `Backlog`
> 优先级：`P0` 必须现在做 · `P1` 尽快 · `P2` 有余力再做

## 里程碑总览（T001.1 起按细粒度任务规划）

| ID | 里程碑 | 任务 | 状态 |
| --- | --- | --- | --- |
| M1 | 工程引导与文档体系 | T001, T001.1 | ✅ 完成 |
| M2 | Modbus 协议核心 | T002, T003, T004 | ✅ 完成 |
| M3 | 模拟与故障注入 | T005, T006 | ✅ 完成 |
| M4 | 事务分析与界面 | T007, T008 | ✅ 完成 |
| M5 | 回放与串口模式 | T009, T010 | ✅ 完成 |
| M6 | AI 诊断与 Agent 工具 | T011, T012 | ✅ 完成（T011 ✅ + T012 ✅ 含 Stabilization；ISSUE-008/009=MONITORING/NON-BLOCKING） |
| M7 | 收尾与演示 | T013 | ✅ 完成（T013 Done；verified LKGC `99f17d6`） |
| M8 | 诊断深化与知识固化 | M8.1 ✅；T014 ✅ Done（verified LKGC `cc8393a`）；T015 🔄（IN PROGRESS，**Phase B IMPLEMENTED/AWAITING REVIEW**；candidate `6944fd5`；Part C 0x10 NOT STARTED）；后续（Replay v2 timing / UART / register-map）⏸ | 🔄 进行中 |

## 任务表

| ID | 标题 | 里程碑 | 优先 | 状态 | 依赖 | 说明 / 关键产出 |
| --- | --- | --- | --- | --- | --- | --- |
| T001 | 项目引导：骨架 + 文档体系 + 最小 Qt6 应用 | M1 | P0 | ✅ Done | — | 目录骨架、AGENTS 规约、全套文档、CMake Presets、offscreen 冒烟测试 |
| T001.1 | Bootstrap Documentation Cleanup | M1 | P0 | ✅ Done | T001 | 文档更名为 05_DEMO_GUIDE、preset 示例模板、ENVIRONMENT 重写、BACKLOG 细粒度拆分 |
| T002 | **Modbus CRC16** | M2 | P0 | ✅ Done | T001.1 | `modbuslens_core`（无 Qt 纯 C++20 静态库）+ `calculateModbusCrc(std::span<const std::uint8_t>)` 按位实现；6 测试（KAT/边界/敏感性/zero-remainder）RED→GREEN 全程留痕。按约定未做：查表优化、benchmark、fuzz、序列化 |
| T003 | **Modbus RTU Frame Model** | M2 | P0 | ✅ Done | T002 | 交付：`ModbusRtuFrame`（address/functionCode/data，C++20 value 语义，**不存 CRC**）+ `isExceptionResponse`；FRAME-T01~T04 全绿。**范围修订（T003 执行时确认）**：wire 编解码、CRC 校验集成、fuzz-lite **移出 T003**——编解码与 CRC 校验随 T004 codec 承接，fuzz 待 codec 存在后评估；t1.5/t3.5 时序归 T010 |
| T004 | **Modbus RTU Codec**（Part A Wire Codec + Part B 0x03 Codec） | M2 | P0 | ✅ Done | T003 | **Part A** ✅：`ModbusRtuCodec`（encode/decode + `variant<Frame, RtuDecodeError>` 错误模型 + CRC 低字节在前序列化/验证）落地 `modbuslens_core`；RTU-A01~A07 全绿。**Part B** ✅：`Function03`（`ReadHoldingRegistersRequest/Response` + `ModbusExceptionResponse` 三个 decoder、`Function03DecodeErrorCode` 五值、big-endian helper），F03-B01~B12 全绿（V1.1b3 §6.3 官方金样）。修正：byteCount=0 单帧即非法（Part B 直接拒绝，不推迟 T007）；byteCount 非帧定界符（帧定界属 T010）。配对/一致性归 T007；fuzz/benchmark 不在范围 |
| T005 | **Simulator Basic Slave** | M3 | P0 | ✅ Done | T004 | **范围收缩后交付**：`SimulatedSlave`（单设备地址 + `vector<uint16_t>` 连续寄存器文件补 0 + Function 0x03 正常响应 / 0x02 Illegal Address / 0x01 Illegal Function / 0x03 Illegal Data Value；地址不匹配 → `IgnoredRequest`；**复用** T004B decoder；const 纯应答端点）；SIM-T01~T07 + SIM-I01 全链路闭环全绿。**Deferred 兑现**：IFrameSource/VirtualMaster/轮询/虚拟时钟/seed 未实现（等真实共性）；Timeout/CRC fault → T006。附带：ISSUE-001（variant 测试悬垂指针）建档并修复 |
| T006 | **Fault Injection** | M3 | P0 | ✅ Done | T005 | **交付**：`applySimulationFault(wire, config)` 四模式（None 透传 / DropResponse 丢弃 / CorruptCrc 固定 XOR 末 CRC 字节 / ArtificialDelay 元数据延迟），结果 `variant<DeliveredWire, DroppedResponse>`；FAULT-T01~T05（含确定性双调用与模式隔离断言）+ I01/I02 全绿；SimulatedSlave 零修改。**范围收缩兑现**：random/seed/real sleep/丢包概率等未实现；Timeout 判定归 T007 |
| T007 | **Transaction Analysis**（Part A 单事务 + Part B 统计快照） | M4 | P0 | ✅ Done（整体） | T004（+T005/T006 提供流量） | **Part A** ✅：`analyzeFunction03Transaction`（六状态 Pending/Success/Exception/CrcError/Timeout/ProtocolError；观察 `variant<Frame, RtuDecodeError, NoResponse>`；跨帧校验地址/功能/数量；elapsed 与 exceptionCode 双不变量经 makeAnalysis 漏斗保证），TX-A01~A12 + I01~I03 全绿。**Part B** ✅：`summarizeTransactions(batch)` → `TransactionStatisticsSnapshot`（三计数 + 五分类 + optional successRate/averageSuccessLatencyMs + 四不变量），STAT-B01~B08 + I01 全绿。**范围收缩兑现**：real timer/polling/session manager/database/persistence/rolling window/per-device 聚合未实现 |
| T008 | **Qt Analysis UI**（Part A 迁移+桥接 / Part B Dashboard+Demo） | M4 | P0 | ✅ Done（整体） | T007 | **Part A** ✅：QGuiApplication+QQmlApplicationEngine（QMainWindow scaffold 与 Widgets 依赖已删除）、qt_add_qml_module（URI ModbusLens/Main.qml，QML 模块直接挂 exe）、AnalysisController（optional→hasX+value，int 计数）、TransactionListModel（7 roles/DTO/setEntries）、UI-A01~A06 + 真实 exe 的 QML load smoke + Manual Visual Smoke（用户确认）。**Part B** ✅：runDemoBatch/clearDemo（真实调用 T005/T006/T007 链路）、四条确定性 Demo、Dashboard 统计卡扩展、QML Presentation 修正（0xNN 格式/Clear 可见性）、矩阵 UI-B01~B06 + **Manual Demo Smoke 用户确认** 全过。**范围**：无轮询/Serial/Replay/QSerialPort/Agent/AI/database/chart/动画大工程/theme/persistent settings/timer/thread/networking |
| T008.1 | **Windows Standalone Deployment Fix**（ISSUE-002 修复） | M4 | P0 | ✅ Done（自动化全过；**用户 Explorer 双击确认 = PASS**；**ISSUE-002 = RESOLVED**） | T008 | `scripts/deploy_windows.bat`（CMakeCache 自动取路径→干净 build/deploy→windeployqt→强制编译器 bin 三件套→部署应用 QML 模块→关键文件校验）；Runtime Provenance SHA256 = 编译器 bin VERIFIED；minimal-PATH smoke（--qml-smoke-test exit=0）+ 普通运行存活全过；脚本可重复生成验证 PASS |
| T009 | **Replay Mode**（Part A Log Format+Core / Part B UI Integration） | M5 | P0 | ✅ Done（整体） | T007 | **Part A** ✅：`src/core/replay/`（Pure C++20 Zero Qt）——`ReplayLog`（`.mlog` v1 模型 + parseReplayLog 纯文本解析：from_chars 整段消费 / CRLF 兼容 / 空行与 `#` 注释 / 八种解析错误 + 1-based 物理行号，0 = 无违规行）+ `ReplayAnalysis`（analyzeReplayLog：request 可信链 decodeRtuFrame→0x03→decodeReadHoldingRegistersRequest，失败 → InvalidRequestWire/InvalidRequestFunction/**InvalidRequestData** + 0-based transactionIndex；坏 response 为诊断事实进 T007 → CrcError/ProtocolError 而不失败；统计经 summarizeTransactions 与 Simulator 同源）；golden fixture `tests/data/demo_v1.mlog`（Implementation 迁 `samples/demo_v1.mlog` canonical）；矩阵 REPLAY-A01~A08 + I01~I05（+I03B/I03C）24 测试函数 RED（101 处 undefined reference）→GREEN；ctest 16/16、clean 96 targets 零警告。**Part B** ✅：loadReplayFile(QUrl)（QFile→string_view→parse→analyze 复用 Part A 后**原子发布**；失败只动 error state、旧 batch+mode/source 完整保留）+clearResults（clearDemo 直接重命名）+hasReplayError/replayErrorMessage/modeLabel/sourceLabel+Main.qml FileDialog（QtQuick.Dialogs 动态解析，CMake 零新增链接）/Header 绑定/错误 label；canonical sample `samples/demo_v1.mlog`（git mv 单一源头，tests/deploy/manual 共用）；UI-R01~R08 全过（ui_bridge 22/22）；ctest 16/16 零警告；deploy sample SHA256 一致 + minimal-PATH smoke PASS；**用户 Manual Replay Smoke PASS（A~E）**。**范围**：无 real-time playback/QTimer/sleep/speed/pause/seek/drag-drop/recent DB/watcher/Serial/AI/Agent/IFrameSource/第二套 Dashboard |
| T010 | **Serial Mode**（Part A Transaction Runtime+Adapter / Part B UI+Hardware Smoke） | M5 | P0 | ✅ Done（整体） | T007（T004） | **Part A**：FC03 单事务运行时——`SerialTransactionSession`（Idle/AwaitingResponse 两态、one outstanding→Busy、任意分块累积、completion：buffer[1]==0x83→5 bytes / size==5+2N、oversized 不截断、timeout 双路：空 buffer→NoResponse→Timeout、partial→decode→CrcError/ProtocolError、completion/cancel 后回 Idle）；`encodeReadHoldingRegistersRequest`（T004 缺失的最小 encoder，语义 Frame + quantity 1~125）；QtSerialPort 薄 adapter（signals+single-shot QTimer 只做 timeout，禁 blocking API）；矩阵 SERIAL-A01~A14 + I01；配置 8N1 + baud 列表；Transport Error 与 TransactionStatus 分层。**Part A ✅ 落地并验证**：session（Zero Qt）+ encoder（quantity 1~125 校验、unicast 地址校验留 Session 层）+ QtSerialPort thin adapter（QueuedConnection+suppress 修复 errorOccurred 反馈风暴 PE-4）；SERIAL-A01~A16 锁定最终 framing（bit7 异常格式 5B / 0x03=5+响应 byteCount / exact-candidate 不截断 / timeout 双路：空→Timeout、partial→真实 wire-truth CrcError/ProtocolError）；ctest 18/18、clean 106 targets 零警告；app 未链 SerialPort（Part B UI 接入才是真实 link graph）。**ISSUE-003（OPEN）**：本机 Qt 6.11.1 无 QtSerialPort 组件，Qt adapter 实现受阻（Pure session 不受影响）。**范围**：无 Serial QML/COM selector/polling/QThread/FC04/06/16/TCP/sniffer/t3.5 framer/AI/Agent；不抽 IFrameSource（三模式观察后仍不强制统一）。**ISSUE-003 RESOLVED ✅**（Qt Serial Port 已装到正确 MinGW kit，五步实证全过）。**Part B** ✅：refreshSerialPorts（QSerialPortInfo::availablePorts，只 enumerate 绝不自动 open）/ connectSerial（成功=来源切换清旧 batch+mode=Serial Mode+source="COM3 @ 9600"；失败=原子保留旧 source/batch）/ disconnectSerial（保留最后结果，Disconnect≠Clear）/ readHoldingRegistersOnce（前置 connected&&!busy、C++ range validation 防 narrowing、pending metadata、replace=1）；`publishSerialResult` hardware-free seam（非 Q_INVOKABLE，生产与测试共用）；SB-13 四向 source switching 原子矩阵；QML Serial Controls GroupBox（enabled 规则、Reading...、独立 serial 错误 label）；矩阵 UI-S01~S09 + SERIAL-I02（PE-4 有界回归）/I03；app 链接 Qt6::SerialPort（core 保持 Zero Qt）；Adapter API 拆分（openPort/startTransaction/closePort）落地；UI-S01~S10+SERIAL-I02（PE-4 有界）/I03/I05 全绿；deploy Qt6SerialPort.dll provenance SHA256=MinGW bin 验证；**用户 Manual Serial UI Smoke PASS（A~F）**；**Hardware Smoke = NOT RUN（hardware unavailable，政策内，不阻塞）**；ctest 18/18、clean 108 targets 零警告。三模式（Simulator/Replay/Serial）共享同一协议核心与 Dashboard 的架构闭环达成。**边界**：无 Auto Poll/Monitoring/parity 选择器/第二套 Dashboard |
| T011 | **AI Diagnosis**（Part A Rule Baseline / Part B LLM Integration） | M6 | P2 | ✅ Done（整体） | T007 | **Part A 定案**：核心原则"AI 不产生协议事实"（事实 authority 永远是 deterministic Core；禁止 LLM 改 TransactionStatus/statistics/自动行动）；`src/core/diagnosis/`（Zero Qt，计划）——DiagnosisTransaction/DiagnosisContext（buildDiagnosisContext 复用 summarizeTransactions 保证自洽）+ RuleBasedDiagnosis（NoData≠Healthy、Healthy 三条件、Pending 不算 failure、Timeout/CRC 只述事实给 possible checks 不宣 root cause、Exception 按 code 升序分组 + 0x01~0x04 映射、Mixed 多 finding 无 health score、固定 finding 顺序仅 presentation）；Controller：activeDiagnosisTransactions_ 与 batch 同源 + runBaselineDiagnosis/clearDiagnosis + 新 batch invalidation（失败切换保留）；QML Diagnosis panel（称 Baseline Diagnosis，不叫 AI）；矩阵 DIAG-A01~A10 + UI-D01~D07；20 题问答。**Part B 原则提前锁定**：LLM 不自动调用（用户显式 Ask AI）、API Key 永不入库（env/local ignored）、不做 Agent（属 T012）、prompt injection 边界预告。**Part A ✅ 落地并验证**：src/core/diagnosis/（Zero Qt）Context+builder（summarizeTransactions 自洽）+ RuleBasedDiagnosis 全规则；Controller activeDiagnosisTransactions_ 三发布同源 + invalidation（失败切换保留）；formatter 只翻译；QML Deterministic Baseline 面板；DIAG-A01~A10 + UI-D01~D08 全绿；**用户 Manual Baseline Smoke PASS**；产品代码零 LLM/HTTP/key（grep 佐证）；架构结论 "AI is interpreter, not detector"。**Part B** ✅：Provider=ModelScope API-Inference（OpenAI-compatible Chat Completions 仅协议兼容；禁 OpenAI SDK/Responses API/多 provider）；credential=MODELSCOPE_API_KEY 仅 process env（BYOK、QML 不见 Token、生产 endpoint 禁 override）；candidate model Qwen/Qwen3.5-27B + env override + 用户 live probe；ModelScopeDiagnosisClient（App 层 QNetworkAccessManager、one-shot、Idle/Requesting）；Baseline First/空批不调 API；structured-facts-only prompt（bounded 20、确定性、system authority 指令、plain text、768 tokens）；raw parser choices→message→content + reasoning_content 忽略；错误 11 值 + HTTP mapping；zero retry/无 streaming/30s timeout/cancel 静默；activeBatchRevision stale guard（P0）+ AI invalidation 与失败切换保留；fake server 随机端口+fake token 硬约束；矩阵 AI-B01~B12 + UI-AI01~AI10；QtNetwork 预检已实证（sslBuild=yes）；Live Smoke 政策（无 token 记 NOT RUN 不伪报）。**Part B ✅ 落地并验证**：ModelScopeDiagnosisClient/prompt builder/双 stale guard（batchRevision+requestGeneration）/grep 级零 Agent；AI-B01~B13 + UI-AI01~AI11 全绿；ISSUE-004（Diagnosis workspace 溢出→SplitView）RESOLVED 全轨迹；**Manual AI UI Smoke PASS + Live ModelScope Smoke PASS（Qwen/Qwen3.5-27B 真实验证，attempt#1 余额失败记录保留）**；non-blocking over-inference 质量观察入档："AI can be wrong in prose without corrupting protocol facts"。**范围**：Part A 无任何 HTTP/网络/key/provider/prompt mock；Part B 无 tools/tool calling/MCP/Agent/conversation |
| T012 | **Agent Tools** | M6 | P2 | ✅ Done（Final Review PASS；Stabilization DONE；verified LKGC `3572cf7`；ISSUE-008/009=MONITORING/NON-BLOCKING） | T007/T008/T011 输出 | 只读 Agent 工具集：三工具 white-list（summary/anomalies/detail）；架构强制**无写能力连名字都不在 contract**；详见 T012 档案 + ADR002 |
| T013 | **Final Integration & Demo** | M7 | P1 | ✅ Done（用户 Manual Visual Re-Review PASS；verified LKGC `99f17d6`） | T008（含 T009–T012 可用能力） | 打包/便携发布；演示脚本与素材齐备（demo/ 目录）；文档终稿（README/DEMO_GUIDE 重写）；深色对比度/视觉层级/AI·Agent·Baseline 视觉身份/术语 polish（见 T013 档案 §2/§4）；CRC 查表优化留待单独任务 |
| T014 | **Diagnostic Detail Preservation（诊断细节保留）** | M8 | P0 | ✅ Done（**用户 Manual UI Review = PASS**；Phase A/B DONE；verified LKGC `cc8393a`；`213bba5` 仅 docs 归档不作 LKGC） | T007（下游传播涉 T011/T012/T013 既有结构） | Core：`TransactionIssueCode` 七值 + 稀疏载荷 + `TransactionAnalysis.issue`（append-last）+ production invariant（`ProtocolError ⇒ issue` 必有；防御 sentinel `UnknownProtocolError`；下游对手工构造 issue=nullopt 防御 omit 不伪造）。Statistics 逐位不变（STAT-B09）。Replay/Serial 零逻辑改动同漏斗继承。Baseline finding 不变；batch summary = DEFERRED（无消费者）。Prompt 行 `issue=<token>`+载荷 additive；AgentTools detail 全量/anomalies 简化 code，hasX 语义；UI 仅 ProtocolError 行 issueText secondary text（9 个生产文件）。测试：TX a13~a18 + a01~a11 强化、STAT-B09、REPLAY-i05、SERIAL a07/a11/a15/a16、DIAG-A11、AI-B18/B19、AGENT-A11、UI-T01。RED 9→GREEN tx 20/20、ctest 23/23、clean 零警告、qml smoke、deploy+minimal-PATH、**Manual UI Review PASS**。详见 [T014 档案](tasks/T014-diagnostic-detail-preservation.md) |
| T015 | **Passive Replay Expansion（被动回放诊断扩展）** | M8 | P1 | 🔄 In Progress（**Phase B IMPLEMENTED / AWAITING REVIEW**：七状态 + passive analyzer + FC06 + per-record；candidate `6944fd5` 待 Review；**Part C Function 0x10 = NOT STARTED**） | T009、T014（复用 TransactionIssue/下游链） | Phase A 定案（§档案）+ Phase B 落地：`TransactionStatus::ExpectedNoResponse`（Gate C/ADR-003：completed 含广播，rate=success/(completed−expectedNoResponse)，分母 0 ⇒ nullopt）；`PassiveTransactionAnalysis`（Gate A：request 分类单点 / generic exception 单点 / FC03 复用 T007 / broadcast=addr0∧FC06 / unsupported 显式事实）；`TransactionRequestIssue`（Gate B：InvalidRequestQuantity 载荷 126/125、InvalidRequestLength、InvalidBroadcastFunction）；`Function06` 被动 decoder（**无 encoder**；echo mismatch 独立 issue）；Replay per-record 化（Gate F：analyzed + unsupportedRecords + 统计仅 analyzed）；Baseline `ExpectedNoResponseObserved` + Healthy 四条件；Prompt/Agent/QML additive（新统计卡 + 非致命提示条）。RED 13 断言失败 → GREEN ctest 24/24、clean 152 零警告、qml smoke、deploy+minimal-PATH；demo_v1/T014/Active Serial 零回归（写权限 grep 零命中）。**Function 0x10 normal semantics 明确留 Part C**。详见 [T015 档案](tasks/T015-passive-replay-expansion.md) + [ADR-003](../adr/ADR-003-broadcast-outcome-semantics.md) |

## 建议路线（默认执行顺序）

```text
T001 ✅ → T001.1 ✅ → T002 CRC16 ✅ → T003 Frame Model ✅ → T004 Codec ✅
→ T005 Simulator ✅ → T006 Fault Injection ✅ → T007 Transaction Analysis ✅
→ T008 Qt UI ✅ → T009 Replay ✅ → T010 Serial ✅ → T011 AI Diagnosis ✅ → T012 Agent Tools → T013 Final Integration & Demo
→ T012 Agent Tools → T013 Final Integration & Demo
→ M8.1 Diagnostic Coverage Audit ✅（docs-only，用户 Final Review PASS）
→ T014 Diagnostic Detail Preservation 🔄（Phase A docs-only 完成，Phase B 待批准）
→ T015 Passive Replay Expansion ⏸（Backlog；方向待用户决策）
```

## 变更记录

| 日期 | 事件 |
| --- | --- |
| 2026-09-05 | T001 建立本文件与任务表草案（旧编号 T002–T011） |
| 2026-09-05 | T001 完成（提交 `aa337f6`）；M1 关闭；T002 保持 Ready |
| 2026-09-05 | T001.1：任务细粒度重排为 T002–T013（T002 仅 CRC16；帧模型/0x03 编解码/模拟器/故障注入/UI/回放/串口/AI/Agent 各自独立）；里程碑重组为 M1–M7 |
| 2026-09-05 | T002 进入 Phase A（Learning Checkpoint，docs-only）；状态改为 In Progress，未标完成 |
| 2026-09-05 | T002 Phase B（Test Design）完成：测试矩阵与优先级、接口定案（std::span）、Phase C TDD 计划落库 |
| 2026-09-05 | T002 完成（Phase C 实现 + RED→GREEN，T002 **DONE**）；T003 转 Ready；里程碑 M2 进行中（T003/T004 待启动） |
| 2026-09-05 | T003 完成（Frame 内存模型 + 4 测试全绿，T003 **DONE**）；范围修订：fuzz-lite 移出 T003、wire 编解码/CRC 校验集成归 T004；补录 ADR001（最终 UI = Qt Quick/QML，用户于 T003 开始前确认）；T004 转 Ready |
| 2026-09-05 | T004 启动（Learning，docs-only）：更名 **Modbus RTU Codec** 并拆 Part A（Wire）/ Part B（0x03）；确认 fuzz/benchmark 不在 T004、异常码文字映射归 analysis；T005 不变 |
| 2026-09-05 | T004 Part A Test Design（docs-only）：接口/错误模型定案、测试矩阵 RTU-A01~A07（P0×4）；下一步 = Part A Implementation |
| 2026-09-06 | T004 Part A 完成（`ModbusRtuCodec` 实现 + A01~A07 RED→GREEN，Part A **DONE**）；Part B 未开始，T004 保持 IN PROGRESS |
| 2026-09-06 | T004 Part B Learning / Test Design（docs-only）：语义模型×3、错误模型五值、矩阵 F03-B01~B12（官方 §6.3 金样复核）；配对/一致性显式 Deferred 至 T007；下一步 = Part B Implementation |
| 2026-09-06 | T004 Part B 完成（三个 decoder + big-endian helper，B01~B12 RED→GREEN，T004 **DONE**）；byteCount=0 口径修正（单帧即非法）；**M2 Protocol Core 关闭**；T005 转 Ready |
| 2026-09-06 | T005 启动（Learning / Test Design，docs-only）：**范围收缩**——IFrameSource / VirtualMaster / 轮询 / 虚拟时钟 / seed 移出 T005（单数据源阶段不过早抽象，等 Replay/Serial 出现真实共性再提取）；Timeout / CRC fault / delay 归 T006；T006 不变 |
| 2026-09-06 | T005 完成（SimulatedSlave + SIM-T01~T07/SIM-I01 RED→GREEN，T005 **DONE**）；ISSUE-001 建档（variant 测试悬垂指针，T004 测试脚手架同批修复）；T006 转 Ready |
| 2026-09-06 | T006 启动（Learning / Test Design，docs-only）：**范围收缩**——random fault / probability / seed / real sleep / QTimer / timeout timer / 丢包概率 / burst / noise 移出 T006（确定性优先：故障由测试或用户显式选择）；Timeout 判定显式归 T007；下一步 = T006 Implementation |
| 2026-09-06 | T006 完成（`SimulationFault` 四模式，FAULT-T01~T05/I01/I02 RED→GREEN，T006 **DONE**）；SimulatedSlave 零修改；**M3 模拟与故障注入关闭**；T007 转 Ready |
| 2026-09-06 | T007 启动：Part A Learning / Test Design（docs-only）——Transaction 定义、六状态、观察/结果模型、跨帧校验规则、矩阵 TX-A01~A12 + I01~I03 落库；统计快照拆入 Part B |
| 2026-09-06 | T007 Part A 完成（analyzeFunction03Transaction 六状态 + 跨帧校验，TX-A01~A12/I01~I03 RED→GREEN，Part A **DONE**）；统计快照拆入 Part B；T008 转 Ready |
| 2026-09-06 | T007 Part B Learning / Test Design（docs-only）：Statistics Snapshot 模型/API/四不变量/矩阵 STAT-B01~B08 + I01 落库 |
| 2026-09-06 | T007 Part B 完成（TransactionStatistics 聚合 + STAT-B01~B08/I01 RED→GREEN，T007 **整体 DONE**）；**M4 事务分析关闭**；T008 转 Ready |
| 2026-09-06 | T008 启动：Part A Learning / Test Design（docs-only）——Part A/B 拆分、依赖方向定案、QML 模块/迁移计划、Controller/Model 设计、矩阵 UI-A01~A06 + I01（I02 记录不做）、Manual UI Smoke 计划落库；T008 标记 IN PROGRESS |
| 2026-09-06 | T008 Part A 完成（QML 迁移+桥接+UI-A01~A06+QML smoke 全绿 + **Manual Visual Smoke 用户确认 12/12**，Part A **DONE**）；Part B 未开始，T008 保持 IN PROGRESS；ISSUE-002 保持 OPEN |
| 2026-09-06 | T008.1（Windows Standalone Deployment Fix）：deploy_windows.bat 落地，deploy 目录 + 三件套 provenance VERIFIED + minimal-PATH smoke 全过；**Explorer 双击确认 = 待用户**，确认后 ISSUE-002 置 RESOLVED |
| 2026-09-06 | **用户 Explorer 双击 build/deploy/ModbusLens.exe = PASS**：ISSUE-002 置 RESOLVED ✅，T008.1 DONE |
| 2026-09-06 | T008 Part B 启动：Learning / Test Design（docs-only）——四条确定性 Demo、runDemoBatch/clearDemo API、矩阵 UI-B01~B06 落库 |
| 2026-09-06 | T008 Part B 完成（runDemoBatch/clearDemo 实现 + UI-B01~B06 全绿）；**用户 Manual Demo Smoke 二轮确认 PASS**（含 QML Presentation 修正 `4075223`）；**T008 Part B DONE → T008 整体 DONE → M4 事务分析与界面 CLOSED**；LKGC = `4075223`；T009 转 Ready |
| 2026-09-07 | **T009 启动：Part A Learning / Test Design（docs-only）**——Replay 角色 / `.mlog` v1 格式 / 数据与错误模型 / API 定案 + wire 金样独立复核 + 矩阵落库；T009 标记 IN PROGRESS；M5 转进行中 |
| 2026-09-07 | **T009 Part A 完成**（`e4920da`）：ReplayLog parser + ReplayAnalysis 批量回放分析落地 `src/core/replay/`（Zero Qt）；request 可信链三错误码；REPLAY-A01~A08 + I01~I05/03B/03C 24 测试函数 RED→GREEN；ctest 16/16、clean 96 targets 零警告；**Part A DONE；T009 保持 IN PROGRESS**；T009 Part B 转 Ready |
| 2026-09-07 | **T009 Part B 启动：Learning / Test Design（docs-only）**——单 Dashboard / Controller 新 API / 失败策略 / FileDialog 本机实证 / canonical sample / UI-R01~R08 矩阵落库；Part B 未实现，T009 保持 IN PROGRESS |
| 2026-09-07 | **T009 Part B Implementation 完成**（`d473d36` LKGC candidate）：loadReplayFile/clearResults/错误与来源状态/FileDialog 落地；UI-R01~R08 全过；canonical sample 迁 samples/；deploy 回归 PASS；**Manual Replay Smoke = WAITING FOR USER** |
| 2026-09-07 | **用户 Manual Replay Smoke = PASS**：**T009 Part B DONE → T009 整体 DONE**；LKGC = `d473d36`；T010 转 Ready（M5 保持进行中，按既有定义含 T010） |
| 2026-09-07 | **T010 启动：Part A Learning / Test Design（docs-only）**——Serial 事务运行时设计落库；**ISSUE-003（QtSerialPort 未安装）建档 OPEN**；T010 标记 IN PROGRESS |
| 2026-09-07 | **T010 Part A 完成**（`b31233b` 新 LKGC）：session/encoder/adapter 落地；SERIAL-A01~A16+adapter 测试全绿（ctest 18/18）；ISSUE-003 RESOLVED；**T010 保持 IN PROGRESS**（Part B 未开始） |
| 2026-09-08 | **T010 Part B 启动：Learning / Test Design（docs-only）**——Serial UI 设计定案；T010 保持 IN PROGRESS，Serial UI 未实现 |
| 2026-09-08 | **T010 Part B 完成**（`33ed197`）：Adapter API 拆分 + Controller serial 全套 + QML Serial Controls；UI-S01~S10 全绿；**用户 Manual Serial UI Smoke PASS**；Hardware Smoke NOT RUN（policy）；**T010 整体 DONE → M5 CLOSED**；T011 转 Ready |
| 2026-09-08 | **T011 启动：Part A Learning / Test Design（docs-only）**——AI/Core 责任边界与规则基线定案；T011 标记 IN PROGRESS，Diagnosis 未实现 |
| 2026-09-08 | **T011 Part A 完成**（`06ef801` 新 LKGC）：deterministic baseline 落地 + DIAG/UI-D 全绿；**用户 Manual Baseline Smoke PASS**；T011 保持 IN PROGRESS（Part B 未开始） |
| 2026-09-08 | **T011 Part B 启动：Learning / Test Design（docs-only）**——ModelScope/凭据/prompt/客户端/stale guard 全套定案；网络代码未实现 |
| 2026-09-08 | **T011 Part B Implementation 完成（自动化 GREEN）**：AI-B01~B13+UI-AI01~AI11 全绿；ISSUE-004 RESOLVED（SplitView workspace）；**用户 Manual AI UI Smoke PASS（A~I）**；Live ModelScope Smoke = WAITING；T011 保持 IN PROGRESS |
| 2026-09-08 | **T011 Part B 完成 → T011 整体 DONE**：Live ModelScope Smoke PASS（Qwen/Qwen3.5-27B 真实验证）；verified LKGC `85699ff`；M6 保持进行中（T012 未完成）；T012 转 Ready |
| 2026-09-09 | **UI Localization Pass（独立 pass）完成**：code/config commit `9e79558` = verified LKGC；**用户 Manual Localization Smoke PASS**。non-blocking 记录：深色主题下部分 Label/Diagnosis 正文对比度偏低 → 归入 T013 统一处理（本次不实现、不扩 scope） |
| 2026-09-09 | **AI Explanation Polish Review（独立 follow-up，非 T012）**：ISSUE-006 OPEN——AI 解释措辞过度归因的根因（模型先验/system 约束缺口/0x02 语义与小样本未下发）与最小方案（仅 DiagnosisPromptBuilder：system Attribution discipline + user `small_sample=true`≤10；测试 AI-B14/B15）已记录；**Implementation 待用户批准**；T011 历史不改写 |
| 2026-09-09 | **ISSUE-006 RESOLVED**：evidence-scope guard 落地（`01841b1`，取消 small_sample 阈值→Evidence Scope Guard）+ AI-B14~B17 + 用户 Manual UI Regression Smoke PASS + 经授权 Live ModelScope Smoke 五项全 PASS；verified **LKGC = `01841b1`** |
| 2026-09-09 | **T012 Learning / Test Design 完成（docs-only）**：三工具/dispatcher/transaction_id/loop 上限/revision 绑定/注入边界/UI scope/Part A·B 拆分全部定案（T012 档案 TD-1~TD-11）；ADR002 新建；AGENT-A/B 测试矩阵与 Demo 三问答定稿；Implementation 待用户批准 |
| 2026-09-09 | **T012 Part A IMPLEMENTED（`9921efd`）**：read-only tool layer（AgentToolContext/dispatcher/typed results/JSON DTO）+ AGENT-A01~A09；ctest 21/21；T011 零改动；LKGC candidate，待用户审核推进；Part B NOT STARTED |
| 2026-09-09 | **T012 Part A Review fix（`797269a`）**：Pending 不算法 anomaly（whitelist 筛选 + latest-20 按 anomaly 序列）；A02/A03 扩充；ctest 21/21；**最新 Part A candidate = `797269a`，LKGC 未推进** |
| 2026-09-09 | **用户 T012 Part A Review = PASS** → **Part A = DONE**；verified LKGC 推进至 `797269a`；Part B NOT STARTED（Probe 待授权） |
| 2026-09-09 | **T012 Part B Gate 0（Probe）**：Request #1 实证 ModelScope 原生 tool_calls（PASS）；累计 2 次真实请求已达本轮授权上限；round-trip Request #2 待用户追加授权 |
| 2026-09-09 | **T012 Part B Gate 0 ✅ PROVEN**：Request #2 round trip PASS（role=tool 接受、id 匹配、final 引用 observed=4/timeout=1）——Qwen/Qwen3.5-27B native tool calling 全链实证；累计 3 次真实请求；Agent Runtime Implementation 待批准 |
| 2026-09-09 | **T012 Part B Phase 1 IMPLEMENTED（`da453a7` candidate）**：native Agent Runtime（FSM/双 3 上限/validation/dispatcher/stale guard/cancel/supersede）+ provider adapter + AGENT-B01~B18；ctest 22/22；待用户 review 推进 LKGC；Phase 2 NOT STARTED |
| 2026-09-09 | **T012 Part B Phase 1 Review fix（`2becc41`）**：batch identity 单一来源（删重复 revision）+ 空答案→InvalidResponse + tool_calls 优先；B19/B20/B21；ctest 22/22；最新 Phase 1 candidate 待最终 Review |
| 2026-09-09 | **T012 Part B Phase 1 Final Review fix（`b322cc3`）**：captured/current revision seam 分离 + preflight stale guard + B22；ctest 22/22；最新 Phase 1 candidate 待最终 Review |
| 2026-09-09 | **T012 Part B Phase 1 = DONE（用户 Final Review PASS）**：verified **LKGC = `b322cc3`**；Phase 2（Controller+QML）NOT STARTED，待 Learning/Integration Plan 批准 |
| 2026-09-10 | **T012 Part B Phase 2 Learning / Integration Plan 完成（docs-only）**：invalidate seam/single-flight/NoData/generation/cancel/UI/UI-AG01~18 全部定案；register-address limitation 记为 T013/T015 candidate；Implementation 待批准 |
| 2026-09-10 | **T012 Part B Phase 2 IMPLEMENTED（`d781ab0` candidate）**：Controller+QML 集成 + UI-AG01~AG20；ctest 23/23；待用户 Manual UI Review 推进 LKGC |
| 2026-09-10 | **ISSUE-007 修复（`e922c19`）**：MAX_TOTAL_TOOL_CALLS 3→6 + planning discipline + B05/B23；ctest 23/23；Phase 2 candidate 刷新，待 Live Re-Smoke |
| 2026-09-10 | **ISSUE-007 Live Re-Validation = PASS**（同一问题唯一 run；60s 产出合规 final answer，零超限）；ISSUE-007 与 Phase 2 待用户最终 closure |
| 2026-09-10 | **用户 T012 Final Review = PASS**：ISSUE-007 RESOLVED；T012 DONE；**M6 DONE**；verified LKGC = `e922c19`；T013 NOT STARTED |
| 2026-09-10 | **T012 Post-Closure Stabilization Review（docs-only）**：ISSUE-008/009 建档 OPEN（RCA + test design + 文案方案定案）；T012 REOPENED；M6 转 IN PROGRESS；LKGC `e922c19` 不回退 |
| 2026-09-10 | **Sanitized Live Diagnostic Evidence**：ISSUE-008 NOT REPRODUCED（成功率样例 3 rounds 入档）；ISSUE-009 PRECONDITION NOT AVAILABLE；插桩恢复零 diff；两 Issue 仍 OPEN 待 Implementation 批准 |
| 2026-09-10 | **Phase 3 Offline Hardening（`95ad9e7`）**：Provider failure UX 契约 + defensive tests；ctest 23/23；ISSUE-008/009 仍 OPEN；新 LKGC candidate 未推进 |
| 2026-09-10 | **T012 Stabilization Final Review = PASS → Closure**：LKGC `e922c19` → `3572cf7`；ISSUE-008/009 MONITORING/NON-BLOCKING；T012/M6 DONE；T013 NOT STARTED |
| 2026-09-11 | **T013 Phase A Final Polish Audit 完成（docs-only）**：UI/术语/Demo/golden/README/部署/截图/面试全审计；建议 8 项实现集；T013 IN PROGRESS，Implementation 待批准 |
| 2026-09-11 | **T013 Phase B 落地（`aea1e64` candidate）**：V1~V7 样式 + 术语 polish + README/DEMO_GUIDE 重写；ctest 23/23、deploy+minimal-PATH；待 Human Visual Review |
| 2026-09-11 | **T013 Manual Visual Review FAIL → Phase C**：Light 主题/TabBar 三页/表头与稳定列/scrollbar 重设计；busy-error 视觉待重新人工验证 |
| 2026-09-11 | **T013 Phase C 完成（`5a2f60c`）**：Light/TabBar/稳定表格/scrollbar；ctest 23/23、deploy+minimal-PATH；待 Manual Visual Re-Review |
| 2026-09-11 | **T013 Phase C Re-Review FAIL → Phase D**：串口空态缺失 + Tab 选中边框 + 表格对齐重设计 |
| 2026-09-11 | **T013 Phase D 完成（`a25d63c`）**：串口空态/Tab 边框/表格对齐修复；ctest 23/23、deploy+minimal-PATH；待 Re-Review |
| 2026-09-11 | **T013 Phase D Re-Review FAIL/PARTIAL → Phase E**：Qt-side serial 取证（count=0）+ Fusion style + 表格比例列宽 |
| 2026-09-11 | **T013 Phase E 完成（`99f17d6`）**：Qt 取证+Fusion+串口空态视觉+表格比例列宽；ctest 23/23、deploy+minimal-PATH；待 Re-Review |
| 2026-09-11 | **用户 Manual Visual Re-Review PASS → T013 Done**：verified **LKGC = `99f17d6`**；M7 DONE |
| 2026-09-13 | **M8 Phase B Knowledge Ownership（B1~B7，docs-only）完成**（architecture/Modbus core/statistics/execution modes/Qt QML adapter/AI diagnosis/Agent tool-calling 七 Part + Phase B Closure，`4039ade`）；LKGC `99f17d6` 不变 |
| 2026-09-13 | **M8.1 Diagnostic Coverage Audit 建立并通过用户 Final Review**（docs-only `dab9b5f` + bookkeeping 修正 `5f21911`；`docs/09_DIAGNOSTIC_COVERAGE_AUDIT.md`）；LKGC `99f17d6` 不变 |
| 2026-09-13 | **T014 启动：Phase A Learning / Test Design（docs-only，本提交）**——ProtocolError 七分支重构 + 最小数据模型（TransactionIssue A′ + 六不变量）+ Statistics 不变契约 + 三级测试矩阵定案；**T014 标记 In Progress，未实现；Phase B = WAITING FOR USER APPROVAL**；同步建立 T015（Backlog，方向待用户决策） |
| 2026-09-13 | **T014 Phase B IMPLEMENTED（A/B/C 三提交）**：A `0ec6e06`（模型表面 + RED：tx 11 passed/9 failed，9 条 issue 缺失断言）；B `900131d`（analyzer 接线，tx 20/20 + ctest 23/23）；C `cc8393a`（prompt/agent/UI 传播 + 8 测试文件强化/新增，13 文件）。clean 147 零警告、ctest 23/23、qml smoke、deploy+minimal-PATH 全过；**LKGC candidate = `cc8393a`（待用户 Review + Manual UI Smoke）；T014 IN PROGRESS；T015 NOT STARTED** |
| 2026-09-13 | **用户 T014 Manual UI Review = PASS → Final Acceptance**：demo regression 正常；`t014_protocol_error.mlog` ProtocolError 第二行 detail 视觉验收 PASS（readable/行高/无重叠裁剪/列对齐/仅确定性措辞）。**T014 = Done；verified LKGC 推进 `99f17d6` → `cc8393a`**（`213bba5` 仅 docs 归档不作 LKGC）；T015 NOT STARTED（WAITING FOR USER） |
| 2026-09-13 | **T015 Phase A Review = PASS（Gate A~F 全批；Gate C 批准第七状态与统计公式）→ Phase B IMPLEMENTED**：docs `c5cfbf7`（口径订正 + 03 §4.5 官方协议回填 + ADR-003 Accepted-candidate）；B `477ed44` RED（passive 8 passed/13 failed）；C `2ba719f` passive core（Function06/PassiveTransactionAnalysis/per-record/统计/Baseline）→ ctest 24/24；D `6944fd5` 下游传播（dashboard/prompt/agent/UI）。clean 152 零警告、qml smoke、deploy+minimal-PATH 全过；Active Serial 写权限零新增；**LKGC candidate = `6944fd5`（待 Review + Manual UI Smoke）；T015 仍 In Progress；Part C Function 0x10 NOT STARTED** |






























| 2026-09-08 | ISSUE-005（AI"操作被取消"泄漏）RESOLVED：AiAbortReason 归属 + 单一 timeout owner 90s + 文案解耦；Live 27B Regression Smoke PASS；verified LKGC `bb3f3b4` |