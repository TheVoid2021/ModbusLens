# 07 — Final Project Review（项目事实总账）

> 定位：M8 Phase A 事实版复盘。不是 README marketing / 简历 / 背书稿。所有结论来自真实 Git 历史（115 个提交）、14 个任务档案、9 个 Issue 档案与 2 个 ADR；无法证明的事实一律标注。

---

## 第一章 — Project Definition

- **项目名称**：ModbusLens
- **一句话定义（最终产品事实）**：ModbusLens 是一个 C++20 + Qt6（Quick/QML）的**工业 Modbus RTU 诊断平台**——它把三类数据源（Simulator / Replay / Serial）归一为同一套确定性的「事务 → 统计 → 诊断」核心，并提供可选的只读 LLM 解释层（AI 一键解释 + Agent 工具调用问答）。
- **解决什么问题**：Modbus RTU 通信故障（无响应、CRC 错误、异常码）难以从原始字节里快速定位与解释；需要一个可重复、口径一致、可演示可测试的诊断工作台。
- **用户是谁**：工业通信调试工程师（产品视角）；秋招面试官与项目评审（工程叙事视角）。
- **为什么 Modbus RTU**：协议简单但真实存在 CRC/超时/异常码/字节序等完整知识点，适合单人在学习型项目中实现完整链路（帧 → 校验 → 事务 → 统计 → 诊断）。
- **为什么 C++20**：学习价值与系统边界控制（值语义、`std::variant/std::optional/std::span` 的错误模型与生命周期纪律在项目中都有真实用武之地）。
- **为什么 Qt6 / Qt Quick / QML**：ADR001 定案——QML 声明式 UI + C++ Controller 桥接是 Qt 现代桌面应用的主流形态；项目用它验证「UI 与核心解耦」。
- **为什么 CMake/CTest**：NFR-01/02 可构建性、可测试性；presets + CTest 是全程「构建-测试-档案」闭环的执行载体。
- **AI / Agent 为什么存在**：增强而非核心——自然语言解释与按需只读查询；两者都在 Core 的确定性事实之上工作。
- **没有 AI 时还能做什么**：Simulator / Replay / Serial、事务语义分析、统计、确定性基线诊断全部可用（API Key 是 optional 增强配置）。

**三个层次的严格区分（项目宪法）**：
- **Deterministic Core**：CRC 真值、TransactionStatus、Timeout、ProtocolError、异常码、统计、延时的唯一 authority（Zero Qt）。
- **AI Explanation**：one-shot 解释既有事实；`AI is interpreter, not detector`。
- **Agent Tool Calling**：用户提问 + native tool calling + 三个只读工具 + 有界运行时；read → reason → explain，无任何写能力。

---

## 第二章 — Final Architecture

```text
 Transport: ┌── Simulator（虚拟从站应答） ┐
            ├── Replay（demo_v1.mlog 批式回放） ┘
            └── Serial（QtSerialPort + 单事务状态机）

               ↓
 Deterministic Modbus Core（src/core，Zero Qt）
   Protocol: ModbusCrc / ModbusRtuFrame / ModbusRtuCodec / Function03
   Simulator: SimulatedSlave / SimulationFault
   Analysis:  TransactionAnalysis（六状态）/ TransactionStatistics
   Replay:    ReplayLog / ReplayAnalysis
   Serial:    SerialTransactionSession
   Diagnosis: DiagnosisContext / RuleBasedDiagnosis（确定性基线）

               ↓
 App/Adapter 层（src/ui）
   AnalysisController + TransactionListModel（QObject/QAbstractListModel 桥）
   SerialPortAdapter（薄 QtSerialPort 适配）
   ModelScopeDiagnosisClient（T011 one-shot 客户端）
   AgentRuntime / ModelScopeAgentClient / AgentTools / AgentPromptBuilder（T012，app 层 read-only agent）

               ↓
 Qt Quick / QML（Main.qml）
   Dashboard（统计卡）｜Diagnosis（基线诊断/AI 解释/Agent 问答 三 Tab）｜Recent Transactions（固定表头+稳定列）

Agentshield 路径（单独画）:
   QML Question → AnalysisController
   → active structured batch（copy activeDiagnosisTransactions_）
   → makeAgentToolContext（immutable snapshot，statistics 由 summarizeTransactions 重算）
   → AgentRuntime（bounded FSM：rounds ≤ 3 / tool calls ≤ 6）
   → ModelScope（Qwen）native tool_calls
   → strict C++ validation（白名单/参数/预算，零部分执行）
   → read-only tools：get_session_summary / get_recent_anomalies / get_transaction_detail
   → role=tool（原 tool_call_id）
   → final answer → Controller → PlainText QML
```

**事实所有权**：LLM/Agent 不拥有 CRC truth、TransactionStatus truth、Timeout truth、ProtocolError truth、Exception code truth、Statistics truth——以上全部归 Core。

---

## 第三章 — Technology Stack Inventory（只列真实使用）

| 分类 | 技术 | 哪里使用 | 为什么选 | 替代方案与被否原因 |
| --- | --- | --- | --- | --- |
| Language | C++20 | 全项目 | 值语义/span/variant/optional 契约适合协议与状态机 | Rust/Go：学习与生态权衡，未用 |
| Build | CMake 3.21+（Presets） | 全项目构建 | presets 可迁移、CTest 集成 | Makefile：跨平台组织差 |
| UI | Qt 6.11 Quick/QML | Main.qml + Controller 桥 | ADR001；声明式 UI 与核心解耦 | QWidget 最终态：被 ADR001 取代（仅早期 bootstrap 用过） |
| Style | Quick Controls Fusion | main.cpp `QQuickStyle::setStyle("Fusion")` | Windows native style 静默忽略 QML 自定义（T013 真实教训） | Material/Basic：Fusion 最接近桌面习惯且可定制 |
| Protocol | Modbus RTU：CRC-16/MODBUS 按位实现、RTU 帧、FC03 读保持寄存器 | src/core | 学习价值 + 零第三方依赖 | libmodbus：可靠但会跳过核心学习点 |
| Serial | Qt 6 SerialPort | SerialPortAdapter + SerialTransactionSession | 官方组件、跨平台 | 裸 Win32 COM API：不可移植 |
| Networking | Qt Network（QNetworkAccessManager） | ModelScope 客户端（T011/T012） | 单一 QTimer timeout owner（ISSUE-005 教训） | libcurl：Qt 生态内自足 |
| AI Provider | ModelScope API-Inference + Qwen/Qwen3.5-27B | AI 解释与 Agent | OpenAI-compatible raw Chat Completions、可用额度环境、中文模型 | OpenAI SDK：被明确禁止（仅协议兼容）；DashScope：非当前端点 |
| Testing | CTest（23/23，本地 fake HTTP server，零真实网络） | 全程 | 分层矩阵 + offscreen 无头运行 | 额外框架：规模不需要 |
| Deployment | windeployqt（deploy_windows.bat） | build/deploy + minimal-PATH smoke | 独立目录、provenance 验证（ISSUE-002） | 安装包系统：portfolio 规模不需要 |
| Version Control | Git（115 commits；task/docs 双轨） | 全程 | 可追溯历史是 AGENTS.md 规约的载体 | — |
| Docs/Process | Markdown 单一事实源（AGENTS.md/PROJECT_STATUS/BACKLOG/tasks/issues/adr/devlog） | 全程 | NFR-03/04 可追溯、可迁移 | 外部知识库：违反可移植原则 |

**明确不在本项目**（不写进 resume）：数据库、Docker、MCP、RAG、vector DB、multi-agent、OpenAI SDK、双语言 i18n 框架、CI 流水线。

---

## 第四章 — T001~T013 Development Timeline（统一档案格式）

> 每个任务只列：Goal / 学了什么 / 实现 / 关键取舍 / 主要模块 / 验证 / 真实问题→解法 / 结果 / 关键 code commit / 面试追问点。Commit 哈希以 `git log` 为准（关键值见各任务档案 "Git Commit" 节）。

### T001 Project Bootstrap
- **Goal**：可构建、可测试、可演示的最小 Qt 应用 + 文档体系。
- **学了什么**：Qt6 CMake Presets、CTest 注册、qtlicd 许可提示。
- **实现**：文档体系（AGENTS/STATUS/BACKLOG/architecture 等）、最小 Qt 应用、smoke 测试。
- **关键取舍**：把"项目状态只存于仓库 Markdown"立为最高规约（可迁移、防私有记忆）。
- **验证**：clean 构建 + smoke；真实问题：qtlicd 构建期证书警告 → 本机 gitignored preset 环境变量，不入库。
- **面试追问**：工程引导为什么先立规约？可追溯性如何强制？

### T001.1 Bootstrap Cleanup
- 文档更名、preset 模板、环境文档、任务细粒度拆分 T002~T013。

### T002 CRC16
- **实现**：`modbuslens_core` 落地，CRC-16/MODBUS 按位实现；KAT（规范串 "123456789"→0x4B37）。
- **关键取舍**：自研按位实现而非查表/第三方——学习价值优先（T013/Backlog 才有"可选查表优化"）。
- **验证**：CRC-T01~T06（KAT/边界/敏感性/zero-remainder）。
- **面试追问**：为什么算出的 CRC 在线路上低字节在前？按位与查表的权衡？

### T003 RTU Frame Model
- 帧内存模型（address/functionCode/data，value 语义、不存 CRC）+ `isExceptionResponse`；FRAME-T01~T04。

### T004 RTU Codec / FC03
- Frame↔wire 编解码（variant 错误模型）+ Function03 三个 decoder；官方帧规范 + V1.1b3 金样对拍；RTU-B / F03-B 矩阵。

### T005 Simulator Basic Slave
- 虚拟从站（单地址、寄存器文件、FC03 只读应答端点）；SIM-T01~T07 + SIM-I01（T002→T005 首次全链路闭环）。

### T006 Fault Injection
- `applySimulationFault` 四模式（None/DropResponse/CorruptCrc/ArtificialDelay，确定性 wire 层注入）；FAULT-T01~T05。

### T007 Transaction Analysis + Statistics
- `analyzeFunction03Transaction`（六状态、跨帧校验、双不变量）+ `summarizeTransactions`（三计数/五分类/optional rate与latency/四不变量）；TX-A / STAT-B 矩阵。
- **关键取舍**：Timeout=NoResponse+阈值（不是"设备离线"）；0% 是合法成功率（hasX 语义）。

### T008 Qt Quick / QML UI
- ADR001 落地：Main.qml + AnalysisController + TransactionListModel（QRoles 7 个）；演示批次 runDemoBatch（UI-B01~B06）+ qml_smoke；**Manual Demo Smoke 用户确认**。
- **真实问题**：QML 布局第一轮教训的起点（ISSUE-002 部署见 T008.1）。

### T008.1 Deployment（真实存在）
- deploy_windows.bat + runtime provenance SHA256 + minimal-PATH smoke；**ISSUE-002 RESOLVED**（Anaconda 旧 libstdc++ 无 pmr 符号 → Explorer 双击失败与前置 PATH 定位）。

### T009 Replay Mode
- `.mlog` v1 格式 + `parseReplayLog` + `ReplayAnalysis`（批式离线重分析；request 可信链三错误码；坏 response 是诊断事实不是失败）；REPLAY-A/I；UI-R01~R08（loadReplayFile 原子发布）；**Manual Replay Smoke PASS**。
- **关键取舍**：批式分析而非实时时间轴播放；坏 request=加载失败、坏 response=诊断事实的非对称处理。

### T010 Serial Mode
- SerialTransactionSession（Zero Qt 单事务状态机 + framing）+ `encodeReadHoldingRegistersRequest` + SerialPortAdapter（薄 Qt 适配）；SERIAL-A01~A16；PE-4（errorOccurred 反馈风暴 → QueuedConnection+suppress）；UI-S01~S10；**Manual Serial UI Smoke PASS；Hardware Smoke NOT RUN（无硬件，按政策）**。
- **真实问题**：ISSUE-003（QtSerialPort 组件装错 MSVC kit）→ 正确 MinGW kit 重装。

### T011 AI Diagnosis
- Part A：DiagnosisContext + RuleBasedDiagnosis（瞳 NoData≠Healthy、Pending≠failure、action 建议不含 root-cause）+ baseline 面板；DIAG/UI-D 矩阵；**ISSUE-006**（过度归因）收官。
- Part B：ModelScopeDiagnosisClient + bounded prompt builder + 双层 stale guard（batchRevision×requestGeneration）+ fake HTTP；AI-B01~B13 + UI-AI01~AI11；**ISSUE-004**（Diagnosis 纵向溢出 → SplitView workspace）与 **ISSUE-005**（OperationCanceledError 泄漏 + 双 timeout owner → AiAbortReason + 单一 QTimer + 90s）；AI-B14~B17（ISSUE-006 evidence scope）。Manual AI UI Smoke + Live ModelScope Smoke 双 PASS。
- **关键取舍**：AI 不产生协议事实；API Key 仅进程环境（BYOK）；生产 endpoint 禁 env override。

### T012 Agent Tools
- Part A：AgentToolContext（immutable snapshot）+ 三只读工具白名单 + explicit dispatcher + typed JSON DTO；AGENT-A；Pending≠anomaly（Review fix）。
- Gate 0：真实 ModelScope native tool calling PROVEN（tools→tool_calls→role=tool→final）。
- Phase 1：AgentRuntime FSM（rounds=3/total calls=6 双上限、validate-then-execute）+ ModelScopeAgentClient + 提示器；AGENT-B01~B23；captured/current revision + runGeneration 双 guard（三轮 Review 收口）。
- Phase 2：Controller+QML 集成；UI-AG01~AG20；**ISSUE-007**（Live tool budget 3→6 + planning discipline，同题 re-validation PASS）。
- **Post-Closure Stabilization**：ISSUE-008（合法 0x02 问题 InvalidResponse，历史 exact producer UNKNOWN，硬化+监控）、ISSUE-009（provider 失败可见性硬化；silent breakpoint UNKNOWN）；B20/B24/B25、UI-AG21/22。

### T013 Final Integration & Demo
- Phase A 审计 → README/05_DEMO_GUIDE 重写 → Phase B 深色 polish（Manual FAIL）→ Phase C Light+TabBar+稳定表格（Re-Review FAIL：串口空态/Tab 边框/表格对齐）→ Phase D 修复（再 FAIL：串口空态视觉+表格宽度）→ Phase E：Qt 同 runtime 取证（QSerialPortInfo=0，机器 serial availability 变化，非应用回归）+ Fusion style（原生 style 忽略自定义的教训）+ 比例列宽 → **Manual Visual Re-Review PASS**。
- **真实工程价值**：automated PASS ≠ human visual PASS；视觉三连 FAIL→PASS 是测试金字塔之外的人工验收证据。

---

## 第五章 — Milestone Evolution

| M | 任务 | 输入状态 | 里程碑后首次具备 |
| --- | --- | --- | --- |
| M1 | T001, T001.1 | 空仓库 | 工程骨架 + 文档体系 + 最小 Qt 应用 |
| M2 | T002~T004 | 骨架 | Modbus 协议核心：CRC/帧/编解码/FC03（Zero Qt 静态库） |
| M3 | T005~T006 | 协议核心 | 确定性模拟与故障注入（无硬件演示能力） |
| M4 | T007~T008 | 模拟注入 | 事务分析+统计+Qt Dashboard（可演示闭环） |
| M5 | T009~T010 | Dashboard | 回放与串口两种真实性更强的数据源 |
| M6 | T011~T012 | 三模式 | 确定性基线诊断→LLM 解释→只读 Agent 工具调用 |
| M7 | T013 | 全功能 | 最终视觉/文档/演示包装与人工验证收口 |

能力演进的骨架：只有 CRC → 能解析帧 → 能模拟总线 → 能度量事务 → 能可视化 → 能复现与实连 → 能确定性诊断 → 能自然语言解释 → 能按需只读查询 → 成品级呈现与部署。## 第六章 — Testing Strategy（真实分层）

```text
纯 C++ 单元测试（协议/CRC/帧/语义/统计/诊断规则）
  ↓
集成测试（Simulator 全链路 / Replay golden fixture / Serial 无硬件 session）
  ↓
Qt 桥接测试（Controller/Model，ui_bridge；offscreen）
  ↓
fake HTTP 测试（AI 客户端 / Agent Runtime / 集成——127.0.0.1 fake Chat Completions，零真实网络）
  ↓
QML 加载 smoke（真实 exe 实例化并退出）
  ↓
deployment smoke（windeployqt 独立目录 + minimal-PATH 启动存活）
  ↓
Manual UI Smoke（用户逐项人工确认）
  ↓
Live Provider Smoke（一次性真实 ModelScope 证据，预算受控）
  ↓
Manual Visual Review（T013 特有的视觉人工验收）
```

- 当前自动基线：**ctest 23/23**（clean 0 警告）。
- **为什么不能只靠自动测试（T013 真实证据）**：Phase B/C 自动化全绿、部署 smoke PASS，但用户 Manual Visual Review 连续两次 FAIL（深色对比度/层次、串口空态、Tab 边框、表格对齐、宽度利用）。只有第三轮人工 Re-Review 才 PASS。失败 candidate（aea1e64/5a2f60c/a25d63c）全部保留在 Git 历史——**自动化是下限，人工视觉是成品上限**。
- Live Provider Smoke 政策：一次授权一个场景、请求预算明确（Gate 0：≤2 后追加 1；ISSUE-007 re-validation：≤4）、失败即停、绝不自动重试。

---

## 第七章 — Engineering Problems 索引（真实 Issue 一览）

| Issue | Symptom | 已知 Evidence | Fix | 验证 | 状态/教训 |
| --- | --- | --- | --- | --- | --- |
| ISSUE-001 | 测试辅助函数悬垂指针 | variant 测试脚手架悬垂引用 | 值语义修正 | 回归测试 | RESOLVED |
| ISSUE-002 | Explorer 启动失败（无 pmr 符号） | Anaconda 旧 libstdc++ 抢占 PATH；SHA256 provenance | 独立部署目录 + minimal-PATH 纪律 | 用户双击 PASS | RESOLVED |
| ISSUE-003 | QtSerialPort 缺失 | 组件装错 MSVC kit（kit 前缀错位） | 重装到 MinGW kit | 五步实证 | RESOLVED |
| ISSUE-004 | Diagnosis 纵向溢出 | QML GroupBox contentItem 不是 Layout parent → attach 属性被静默忽略 | SplitView workspace + runtime geometry 取证 | 用户 Layout PASS | RESOLVED（四轮，证据第一） |
| ISSUE-005 | Ask AI "操作被取消" | 本地 abort 经 errorString 泄漏 + 双 timeout owner 竞态 | AiAbortReason + 单一 QTimer owner + 文案解耦 + 90s | Live 27B 回归 PASS | RESOLVED |
| ISSUE-006 | AI 解释过度归因 | 小样本被写成长期链路稳定性；可能=确证 | Evidence Scope Guard + 状态正例语义 + mixed independence | B14~B17 + Live 五项验收 | RESOLVED |
| ISSUE-007 | 多步诊断触发 tool budget | 真实 Live ToolCallLimitExceeded；budget=3 对自然多步过严 | round=3 不变、total 3→6 + planning discipline | 同题 Live re-validation PASS | RESOLVED |
| ISSUE-008 | 合法 0x02 问题 InvalidResponse | 历史 exact producer UNKNOWN（client ①~③ / runtime ④ 四种可能） | 契约硬化（B20/B24/B25/ag22），不声称修根因 | 自动 HARDENED；同场景复现 NOT REPRODUCED | **OPEN + MONITORING/NON-BLOCKING（无 exact RCA，不编造）** |
| ISSUE-009 | 额度不足时 silent 无提示 | silent breakpoint UNKNOWN；Live quota reproduction count=0（当时额度可用） | 八类 provider 文案全可见 + 顶部"模型配置：" + ag21/22 | 自动 HARDENED | **OPEN + MONITORING/NON-BLOCKING（无 exact RCA，不编造）** |

**纪律声明**：ISSUE-008/009 保持 MONITORING/NON-BLOCKING，历史 exact root cause 未证明；未来仅在自然复发 + 另行授权时才继续取证。

---

## 第八章 — Design Decisions（真实 ADR/档案决策）

| 决策 | 为什么 | 备选 | 代价 | 结果 |
| --- | --- | --- | --- | --- |
| Core Zero Qt（modbuslens_core 静态库） | 三模式共享唯一实现；无头环境可测 | 无线程抽象混在 UI | 边界纪律负担 | 构建系统级隔离，全程成立 |
| 最终 UI=Qt Quick/QML（ADR001） | 现代 Qt 桌面形态 + 声明式绑定 | QWidget（曾为 scaffold） | 调试/布局知识成本（见 ISSUE-004） | T013 后成型 |
| 三种运行模式共享 one core | 口径一致可验证（Demo vs Replay 严格同统计） | 每模式私实现 | — | Replay golden==Simulator demo 的断言化 |
| active batch replace semantics | 单一批次视图 + 原子发布防混态 | append 无限列表 | 历史不可见 | Clear/切换语义多轮 MU 固化 |
| Replay 原子 source switching | 失败加载保 old batch/source；坏 request=load error vs 坏 response=诊断事实 | 部分应用 | 实现复杂度 | UI-R 矩阵锁定 |
| Serial transport vs transaction lifecycle | openPort/startTransaction/closePort 分离（连接 ≠ 事务挂起） | 单 open API | 状态面稍大 | UI-S 矩阵 + PE-4 |
| 确定性 baseline 先于 LLM | 核心诊断永远可用（无 key/无网） | 直接 LLM 诊断 | 工程量大 | "AI is interpreter, not detector" 落为产品宪法 |
| read-only Agent Tools + 白名单 dispatcher | 写能力在类型层面不存在 | Registry/动态工具 | 扩展需改 contract（正是意图） | ADR002；injection tests 锁定 UnknownTool |
| immutable Agent snapshot | 单 run 事实自洽（statistics 由 summarizeTransactions 同源重算） | run 内行读 mutable controller | 无法跨批一致 | makeAgentToolContext P0 seam |
| stale guard 双维（batchRevision × generation） | '数据还对吗' 与 'run 还新吗' 正交 | 单一版本号 | 状态面 | T011/T012 全套 stale 测试 |
| bounded Agent（rounds=3 / calls=6） | 资源上界 vs 模型计划空间（ISSUE-007 实测校准） | 无上限/一轮一工具（均否决） | 多步诊断上限 | Live re-validation PASS |
| standalone deploy（windeployqt + provenance） | 无 Qt 开发机可运行 + 可追溯 | 安装包 | 体积 | ISSUE-002 RESOLVED + minimal-PATH 纪律 |
| 单一中文语言政策（06_UI_LANGUAGE_POLICY） | 单语用户 + 避免 Linguist 过度设计 | .ts/.qm 多语 | 无多语扩展 | 术语保留表 + PlainText+语言政策 |

---

## 第九章 — Known Limitations（真实且正面）

1. **FC03-focused v1**：不支持 FC06/FC10 等写功能——协议核仅实现读保持寄存器链。
2. **Agent detail 无 FC03 startAddress/quantity**：能确定 `Exception 0x02 = Illegal Data Address` 并建议核对 register map/地址范围/设备文档；**不能**可靠回答"具体哪个寄存器地址有问题"（数据模型不含该字段）。已记入 T013/T015+ future candidate。
3. **无 write tools / 无自动设备控制**：Agent 只有三个只读工具。
4. **无 RAG / MCP / multi-agent / chat history / database**。
5. **Provider 配置是桌面级进程环境模型**（MODELSCOPE_API_KEY env），属 portfolio-scale 设计而非多租户产品。
6. **真实 Provider 证据为一次性历史验证**，不构成生产 SLA 或工业认证。
7. **ISSUE-008/009 仍在 MONITORING**（历史 exact RCA 未证明）。

---

## 第十章 — What I Actually Learned（按真实经历）

- **Modbus**：T002 里亲手按位实现 CRC-16/MODBUS，通过规范串 KAT 与线上低字节优先的字节序反转，理解了"计算值 vs 线上值"的区别；T004 里把 Frame↔wire 的编解码与内字节序推给显式编码函数；T007 里把"帧合法 ≠ 响应正确"的语义固化进六状态交易分析；0x02=Illegal Data Address 的标准语义在 T011/T012 中作为确定性事实下发而非让模型自悟。
- **C++**：`std::variant` 错误模型（T004/T009）、`std::optional` 的 hasX 语义防"假 0"（T007/T011）、`string_view` 跨 QByteArray 生命周期纪律（T009）、穷举 switch 无 default 的防静默扩展（屡次）、`const&` 注入 + 不可变快照（T012 R3）。真实 bug 的教学：ISSUE-001 悬垂指针是"把辅助函数当所有权"的经典反例。
- **Qt**：QML↔C++ Controller 桥的 property/notify 体系（T008/T012 Phase2）；QAbstractListModel 角色设计；QtSerialPort 异步模型 + PE-4 反馈风暴（T010）；QNetworkAccessManager 的 errorString 不可当用户文案 + 单一 QTimer timeout owner（ISSUE-005）；Qt Quick Controls 原生 style 静默忽略自定义（T013 真实教训→Fusion）；ISSUE-004 的 runtime geometry 取证方法论（Layout.* 在非 Layout 父下被忽略）。
- **CMake**：Presets 迁移与 qt_add_qml_module 的模块化（T008）；测试 target 与 offscreen、multikit 错位排查（ISSUE-003）；部署 target 与 provenance（T008.1）。
- **Testing**：分层矩阵（单元→集成→桥接→fake HTTP→smoke→人工→Live）；fake Chat Completions 脚本化回合把"模型行为"变为本地确定性输入（T011/T012）；RED 证据纪律（linker undefined / coverage-only 如实标注，不伪造）；T013 证明 automated PASS 与 human visual PASS 是两个闸门。
- **Networking**：OpenAI-compatible raw parser（choices→message→content，reasoning_content 忽略）；TLS/redirect/token-attachment 最小面；HTTP 状态与网络错误的分层映射顺序坑（Qt 非 2xx 也置 reply error）。Provider 证据的预算纪律（审批制最小请求）。
- **Agent**：native tool calling 全链（Gate 0 真实 PROVEN）；模型输出是不可信输入（白名单/参数/预算三层验证、先 validate 后 execute 零部分执行）；有界循环与 stale 双维守卫；ISSUE-007 的"预算试调"（round 深度与 call 总量分开治理）+ planning discipline 提示词；只读=权限边界在类型层消失。
- **Engineering Process**：AGENTS.md 式工作规约（每次一个任务、读→做→验→档→commit）；单一事实源 Markdown（跨平台可迁移）；只增不改档案区与 git 纪律（不 amend 历史、docs-only 不推动 LKGC）；"证据优先"的排错节奏（ISSUE-004/005/013 串口取证）；不编造 RCA（ISSUE-008/009 MONITORING）；AI Coding Agent 的角色定位：加速实现，而**项目边界、验收规则、架构约束、测试门禁、人工 Review 与最终理解由项目流程持续控制**。

---

## 附：版本事实

- 生成依据：2026-09-11 git 历史 `git log --oneline --reverse`（115 commits）、tasks 14 份、issues 9 份（ISSUE-001~009）、ADR 2 份、docs-only 提交 77 个。
- 关键基线（撰写时）：verified LKGC = `99f17d6`；HEAD = `325f27c`；ctest 23/23；M1~M7 完成。