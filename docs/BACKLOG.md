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
| M5 | 回放与串口模式 | T009, T010 | 🔄 进行中（T009 Part A Learning+Test Design） |
| M6 | AI 诊断与 Agent 工具 | T011, T012 | ⬜ |
| M7 | 收尾与演示 | T013 | ⬜ |

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
| T009 | **Replay Mode**（Part A Log Format+Core / Part B UI Integration） | M5 | P0 | In Progress — **Part A: Learning / Test Design ✅（docs-only）/ Implementation ⬜**；Part B Backlog | T007 | **Part A**：Replay 角色定案（历史记录重新分析，不调用 SimulatedSlave）；`.mlog` v1 格式（`MODBUSLENS_MLOG\|1\|timeout_ms=` header + `TXN\|elapsed\|request\|response/NO_RESPONSE` records + 注释/空行）；数据模型（ReplayTransactionRecord/ReplayLog）；错误模型（ReplayParseErrorCode 八值 + ReplayExecutionError）；API（parseReplayLog string_view 纯函数 / analyzeReplayLog→ReplayBatchAnalysis）；分层 Text Syntax→Wire Codec→Transaction Analysis；矩阵 REPLAY-A01~A08 + I01~I04/I05 已定稿。**范围**：Part A 无 QML FileDialog/页面/playback/pause/speed/real-time sleep/watcher/database/binary/压缩/Serial/AI/Agent；**不做 IFrameSource**（等 T010 出现后观察三者共性）。**Part B 边界**：Load .mlog（FileDialog）+ 填现有 Model/Dashboard + 文件名显示 + Clear Replay；无 real-time playback |
| T010 | **Serial Mode** | M5 | P0 | Backlog | T007 | QtSerialPort 采集、t3.5 帧切分、环形缓冲；com0com/socat 虚拟串口对集成测试；真机核对清单 |
| T011 | **AI Diagnosis** | M6 | P2 | Backlog | T007 | 诊断模块：**确定性规则引擎（不依赖 LLM）** + 报告导出（Markdown/JSON）；可选 LLM 自然语言解释（可插拔、缺失不影响） |
| T012 | **Agent Tools** | M6 | P2 | Backlog | T007/T008/T011 输出 | 只读 Agent 工具集：读日志摘要/统计/报告；架构强制**无写 API**（类型层面不存在） |
| T013 | **Final Integration & Demo** | M7 | P1 | Backlog | T008（含 T009–T012 可用能力） | 打包/便携发布；演示脚本与素材齐备（demo/ 目录）；文档终稿；可选：CRC 查表优化与基准（单独拆分，不并入任何协议任务） |

## 建议路线（默认执行顺序）

```text
T001 ✅ → T001.1 ✅ → T002 CRC16 ✅ → T003 Frame Model ✅ → T004 Codec ✅
→ T005 Simulator ✅ → T006 Fault Injection ✅ → T007 Transaction Analysis ✅
→ T008 Qt UI（整体 ✅ Done）→ T009 Replay（Part A Learning+Test Design ✅ → Implementation）→ T010 Serial → T011 AI Diagnosis
→ T012 Agent Tools → T013 Final Integration & Demo
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