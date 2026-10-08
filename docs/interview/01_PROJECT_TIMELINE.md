# 01 — PROJECT TIMELINE（ModbusLens 真实开发历史重建）

> 用途：秋招面试 / 项目复盘 / Agent 移交的**时间线事实底稿**。
> 证据分级：`[V-nnn]` = VERIFIED；`[C-nn]` = CONFLICT；`[U-nn]` = UNVERIFIED。
> 计数口径与全部来源见同目录 `06_SOURCE_TRACEABILITY.md`；用 `grep -c '\[V-'` 可复现计数。
>
> **不是 commit-by-commit 流水账**：本文件按有工程意义的阶段重建；阶段名与顺序全部来自仓库证据
> （`docs/BACKLOG.md` 里程碑视图、任务档案、`git log --reverse`）。
>
> 全项目基线（本轮 preflight 实测）：
> - 仓库时间范围 = **2026-09-05 → 2026-10-08**（首提交 `aa337f6`，末提交 `69a58ae`，author date `2026-10-08 08:30:45 +0800`）`[V-001]`
> - 提交总数 = **498**（`git rev-list --count HEAD`）`[V-002]`
> - 分支 = `main`；远程 = `origin https://github.com/TheVoid2021/ModbusLens.git`；本地领先 `origin/main` **7 个提交**（`origin/main` = `2f53197`）`[V-003]`
> - tags = `v1.0.0`（annotated object `2cee626b…` → peeled commit `ae067ab7…`）、`v2.0.0`（annotated object `bdabd6fd…` → peeled commit `b18e378f…`）`[V-004]`
> - 任务档案 = **30 份**（`docs/tasks/*.md`，HEAD 实测）；issue = **20 份**（`ISSUE-001`…`ISSUE-020`）；ADR = **3 份** `[V-005]`
> - CTest 目标 = **70**（`grep -cE "^\s*add_test\(" CMakeLists.txt`）`[V-006]`
>
> **时间范围冲突**：`docs/11_PROJECT_FINAL_RETROSPECTIVE.md` §2 与 §11 声称「对外材料时间范围不得超出
> 2026-09-05 → 2026-09-14」，`docs/12_RESUME_INTERVIEW_QA.md` §6 问题 27 亦称「集中在 2026-09-05 到 09-14」。
> 这两处是 **M8/T015 时点的历史快照**（该文件自我声明「事实底盘随每个任务完成后刷新」，实际未刷新），
> 与 Git 实测的 2026-09-05 → 2026-10-08 冲突。`[C-01]`

---

## P1 — 工程引导与文档体系（M1）· 2026-09-05

- **任务 / 时间**：T001、T001.1；2026-09-05
- **Goal**：建立可构建 / 可测试 / 可演示的最小 Qt6 应用与"仓库即唯一事实源"的文档体系。
- **Implemented**：`AGENTS.md`（最高工作规约）、`docs/00_PROJECT_CHARTER.md`、`docs/01_REQUIREMENTS.md`、
  `docs/02_ARCHITECTURE.md`、`docs/04_TEST_STRATEGY.md`、`PROJECT_STATUS` / `BACKLOG` 单一事实源、
  最小 Qt 应用（`QMainWindow` scaffold）、`smoke` 测试、CMake Presets 两层结构
  （提交通用 preset + gitignored 的 `*-local` 机器私有 preset）。`[V-007]`
- **Technologies**：Qt 6、CMake ≥3.21 + Presets、CTest、MinGW-W64 g++ 13.1.0。
- **Why**：可追溯性与可移植性被立为最高规约（charter G3：项目状态只存于仓库 Markdown，不依赖任何平台私有 Memory）。`[V-008]`
- **Problems**：构建期 `qtlicd`（Qt 许可证服务）警告 → 仅在本机 gitignored preset 中设置
  `QTFRAMEWORK_BYPASS_LICENSE_CHECK`，绝不进入提交文件。`[V-009]`
- **Final result**：clean 构建 + smoke 通过；文档地图成为后续 30 份任务档案的载体。
- **Representative commits**：`aa337f6`（T001 骨架）、`bfa73c6`（回填 LKGC 与哈希）、`ffd2b94`（T001.1 cleanup）。
- **Interview value**：为什么"先立规约再写代码"；把项目状态做成可迁移资产而非私有记忆。
- **Sources**：`docs/00_PROJECT_CHARTER.md`、`docs/ENVIRONMENT.md` §2/§3、`docs/tasks/T001-project-bootstrap.md`。

---

## P2 — Modbus 协议核心（M2）· 2026-09-05 → 2026-09-06

- **任务 / 时间**：T002（CRC16）、T003（RTU 帧模型）、T004（Wire Codec + FC03 语义）；2026-09-05 → 09-06
- **Goal**：把 "这一帧是否合法" 做成纯 C++20、零 Qt、可单元测试的协议核心。
- **Implemented**：`src/core/protocol/ModbusCrc.{h,cpp}`（CRC-16/MODBUS **按位实现**）；
  `ModbusRtuFrame.h`（`{address, functionCode, data}` 值语义，**刻意不存 CRC**）；
  `ModbusRtuCodec.{h,cpp}`（Frame ↔ wire 编解码，错误模型 `std::variant<Frame, RtuDecodeError>`，
  错误 `FrameTooShort` / `CrcMismatch`）；`Function03.{h,cpp}`（请求/正常响应/异常响应三个 decoder）。
  编译产物 = 独立静态库 `modbuslens_core`，**不链接任何 Qt**。`[V-010]`
- **Technologies**：C++20（`std::variant`、`std::optional`、`std::span`、值语义）、CMake 静态库 target、CTest。`[V-011]`
- **Why**：不引入第三方协议库（libmodbus / pymodbus 仅作对拍参考）——协议实现本身是项目核心学习价值。`[V-012]`
- **Problems**：**CRC 线上字节序被当成数值记录**。规范上印的 `41 12`、`76 87` 是**线上字节序**，
  却被误记为 0x4112 / 0x7687；断言当场抓出。根因是把"计算值"与"线上两字节顺序"两个概念混同。`[V-013]`
- **Verification（KAT / 金样）**：Vector A `ASCII "123456789"` → CRC 数值 `0x4B37`；
  Vector B `01 03 00 00 00 01` → 数值 `0x0A84` / 线上 `84 0A`。测试矩阵 `CRC-T01~T06`
  （KAT / 边界 / 敏感性 / zero-remainder 性质）、`FRAME-T01~T04`、`RTU-A01~A07`、`F03-B01~B12`（含 V1.1b3 官方金样）。`[V-014]`
- **Final result**：M2 完成；`modbuslens_core` 成为后续所有模式的唯一协议权威。
- **Representative commits**：`e8ef30c`（CRC16）、`a44a6d2`（帧模型）、`73825c6`（wire codec）、`e8b62f6`（FC03 decoder）。
- **Interview value**：CRC 为什么不加密/不认证；为什么初值 0xFFFF；为什么判 LSB 而非 MSB；为什么低字节先上线。
- **Sources**：`docs/03_MODBUS_LEARNING.md` §5/§10、`docs/04_TEST_STRATEGY.md` §3、`docs/tasks/T002~T004`、
  `docs/12_RESUME_INTERVIEW_QA.md` §5.3。

> **注（与文件时间戳的冲突）**：`docs/09_DIAGNOSTIC_COVERAGE_AUDIT.md` §1 与 §27 出现三处
> `TransactionAnalysis.h` 行号引用（`:14-21` / `:32-38` / `:40-47`），该文件在审计提交时点为真；
> 后续 T014/T015 向同一头文件追加了 issue 模型与第七状态，行号已位移。属**历史行号**，不是错误，但不可直接复用。`[U-01]`

---

## P3 — 确定性模拟与故障注入（M3）· 2026-09-06

- **任务 / 时间**：T005（Simulator 基础从站）、T006（故障注入）；2026-09-06
- **Goal**：在**零硬件**前提下产生可精确复述的通信流量与可复现的故障。
- **Implemented**：`src/core/simulator/SimulatedSlave.{h,cpp}`（内存虚拟从站端点，Frame 进 Frame 出，const 纯应答）；
  `SimulationFault.{h,cpp}`（确定性 wire 层故障注入四模式 `None / DropResponse / CorruptCrc / ArtificialDelay`）。`[V-015]`
- **Technologies**：C++20 纯逻辑；无时钟依赖（虚拟时钟驱动，保证确定性）。
- **Why**：**不靠随机制造不可复现问题**——故障必须可复现才能被测试锁定。
- **Problems**：**ISSUE-001 悬垂指针**。测试辅助函数 `as<T>(const Variant&)` 返回 `std::get_if` 取出的指针，
  而形参绑定的是**临时 variant**，语句结束即悬垂；debug 构建下偶发断言失败，集成测试另有 4 处
  GCC `taking address of rvalue` 编译错误。根因是把"指向结果的指针"带出语句边界（UB）。
  修复：辅助函数改**拷贝语义**返回 `std::optional<T>`。影响面含 T004 已提交的三个测试文件。`[V-016]`
- **Verification**：`SIM-T01~T07` + `SIM-I01`（T002→T005 首次全链路闭环）、`FAULT-T01~T05`（含确定性护栏）、
  `FAULT-I01/I02`。`[V-017]`
- **Final result**：M3 完成；Simulator 成为面试与验收的**最安全默认路径**。
- **Representative commits**：`3a896df`（simulated slave）、`2c8d850`（fault injection）。
- **Interview value**：ISSUE-001 是"把辅助函数当所有权"的经典反例；"全部通过"的测试也可能带 UB。
- **Sources**：`docs/issues/ISSUE-001-variant-test-dangling-pointer.md`、`docs/tasks/T005`、`docs/tasks/T006`。

---

## P4 — 事务分析 / 统计 / Qt Quick 界面（M4）· 2026-09-06

- **任务 / 时间**：T007（Part A 单事务分析 + Part B 统计快照）、T008（Part A QML 迁移 + Part B Dashboard）、
  T008.1（独立部署）；2026-09-06
- **Goal**：把裸帧变成"一笔事务最终发生了什么"，并让它可视化。
- **Implemented**：`TransactionAnalysis.{h,cpp}`（`analyzeFunction03Transaction`，跨帧校验）；
  `TransactionStatistics.{h,cpp}`（`summarizeTransactions`：三计数 / 五分类 / optional rate 与 latency / 四不变量）；
  `src/ui/AnalysisController.{h,cpp}`（core 快照 → 可绑定属性，`optional` 在边界拆为 `hasX + value`）；
  `src/ui/TransactionListModel.{h,cpp}`（`QAbstractListModel`，7 roles，`setEntries` 整批替换）；
  `src/ui/qml/Main.qml`（ApplicationWindow Shell）。`[V-018]`
- **Technologies**：ADR001 落地——Qt Quick/QML + C++ Controller/Model 桥；`QQuickStyle::setStyle("Fusion")`。`[V-019]`
- **Why**：ADR001（2026-09-05 用户确认、T003 期间补录）定案最终 UI 用 Qt Quick/QML 而非 QWidget；
  QMainWindow 仅为 bootstrap scaffold，T008 切换。`[V-020]`
- **Problems**：**ISSUE-002 Explorer 双击启动失败**（缺 `std::pmr::get_default_resource` 符号）。
  逐步证明是 runtime PATH 里旧 MinGW 8.1 的 `libstdc++-6.dll` 抢占 Qt 6.11.1（13.1）所需符号，
  而非 Qt 损坏或链接问题。修复 = `scripts/deploy_windows.bat` 独立部署目录 + 强制编译器 bin 三件套 +
  **minimal-PATH 纪律**（`PATH=C:\Windows\System32;C:\Windows`）。`[V-021]`
- **Verification**：`TX-A01~A12`、`TX-I01~I03`、`STAT-B01~B08`、`STAT-I01`、`ui_bridge`、
  `qml_smoke`（真实 exe 加载 QML 后退出）；Manual Demo Smoke 用户确认 PASS。`[V-022]`
- **Final result**：M4 完成；"流量 → 事务 → 统计"闭环可演示。
- **Representative commits**：`14982f6`（事务分析）、`0f3109a`（统计）、`76030a2`（QML 桥）、`28f38b0`（部署）。
- **Interview value**：`optional` 的 `hasX` 语义防"假 0"；0% 是合法成功率；`Timeout = NoResponse + 阈值` 而非"设备离线"。
- **Sources**：`docs/issues/ISSUE-002-explorer-launch-dll-collision.md`、`docs/adr/ADR001-use-qt-quick-qml.md`、
  `docs/tasks/T007`、`docs/tasks/T008`、`docs/07_FINAL_PROJECT_REVIEW.md` 第四章。

---

## P5 — Replay 与 Serial 两种数据源（M5）· 2026-09-07 → 2026-09-08

- **任务 / 时间**：T009（Replay）、T010（Serial）；2026-09-07 → 09-08
- **Goal**：在 Simulator 之外增加"历史日志"与"真实链路"两条数据源，证明三模式复用同一核心。
- **Implemented**：`src/core/replay/ReplayLog.{h,cpp}`（`.mlog` v1 文本格式 + `parseReplayLog`，
  八错误码 + 1-based 物理行号，无 Qt，`string_view` 进值出）；
  `ReplayAnalysis.{h,cpp}`（`analyzeReplayLog` 批量重析：请求可信链 → T007 → `summarizeTransactions`；
  **不 sleep、不调用 SimulatedSlave**）；
  `SerialTransactionSession.{h,cpp}`（Zero Qt 单事务状态机 + chunk accumulation framing）；
  `SerialPortAdapter.{h,cpp}`（薄 QtSerialPort 适配）。`[V-023]`
- **Technologies**：Qt SerialPort（`QSerialPort` / `QSerialPortInfo`）、`QElapsedTimer`、`QTimer`、`QByteArray`。
- **Why**：Replay 采用**批处理/分析式回放**而非实时时间轴播放（历史记录瞬间重新分析）——
  Simulator/Replay 统计口径因此可严格相等并可自动化断言。`[V-024]`
- **Problems**：
  1. **ISSUE-003**：本机 Qt 6.11.1 **未安装 QtSerialPort 组件**，T010 Implementation 阻塞。
     首次补装"用户声称已装"但机器证据不支持（`InstallationLog.txt` 无当日记录）→ 重实证 FAIL；
     最终根因是组件装到了**另一个 Qt 6.11.1 MSVC prefix**（`D:\QTDesign\…\msvc2022_64`），
     而项目用 MinGW kit（`D:\QT\6.11.1\mingw_64`）——多 prefix / 多 kit 依赖错位。`[V-025]`
  2. **Serial `errorOccurred` 反馈风暴（PE-4）**：改为 `QueuedConnection` 投递 + 抑制重复报告，
     且 transport error 绝不伪造 `TransactionStatus`。`[V-026]`
- **Verification**：`REPLAY-A01~A08`（解析矩阵）+ `REPLAY-I01~I05`（集成矩阵，含
  **demo_v1.mlog Golden Replay 与 Simulator Demo 统计快照严格相等**：4/4/0、1/1/1/1/0、0.25、25.0 ms）；
  `SERIAL-A01~A16`；UI-R01~R08（`loadReplayFile` 原子发布）、UI-S01~S10。`[V-027]`
- **Final result**：M5 CLOSED；Manual Replay Smoke PASS、Manual Serial UI Smoke PASS；
  **Hardware Smoke = NOT RUN**（无真实 Modbus 设备，按政策如实标注）。`[V-028]`
- **Representative commits**：`e4920da`（replay log + core）、`d473d36`（replay UI）、
  `b31233b`（serial runtime）、`33ed197`（serial UI）。
- **Interview value**：三模式共享核心的**可自动验证形式**（Demo == Replay 的断言）；
  坏 request = 加载失败 vs 坏 response = 诊断事实的**非对称处理**。
- **Sources**：`docs/issues/ISSUE-003-qtserialport-not-installed.md`、`docs/tasks/T009`、`docs/tasks/T010`、
  `docs/04_TEST_STRATEGY.md` §3。

---

## P6 — 确定性诊断 → LLM 解释 → 只读 Agent（M6）· 2026-09-08 → 2026-09-10

- **任务 / 时间**：T011（Part A 规则诊断 + Part B LLM 解释）、T012（Part A 只读工具层 +
  Gate 0 探针 + Part B Phase 1 运行时 / Phase 2 UI 集成）；2026-09-08 → 09-10
- **Goal**：建立"事实归 Core、语言归 LLM"的三层边界，并让 Agent 只读且有界。
- **Implemented**：
  - `src/core/diagnosis/`：`DiagnosisContext` + `RuleBasedDiagnosis`（纯 enum finding + 建议检查 action，
    **无用户文案**）；
  - `src/ui/ai/DiagnosisPromptBuilder.{h,cpp}`（bounded 结构化 prompt）、
    `ModelScopeDiagnosisClient.{h,cpp}`（OpenAI-compatible raw Chat Completions，非 2xx 独立映射）；
  - `src/ui/agent/`：`AgentToolContext`（immutable snapshot）、`AgentTools`（白名单 dispatcher + typed JSON DTO，
    三工具 `get_session_summary` / `get_recent_anomalies` / `get_transaction_detail`）、
    `AgentRuntime`（bounded FSM，`MAX_TOOL_ROUNDS=3`、`MAX_TOTAL_TOOL_CALLS`）、
    `ModelScopeAgentClient`、`AgentPromptBuilder`。`[V-029]`
- **Technologies**：Qt Network（`QNetworkAccessManager`）、JSON、native tool calling（OpenAI-compatible `tool_calls`）、
  fake HTTP server（`tests/fake_chat_completions_server.*`，127.0.0.1，**零真实网络**）。`[V-030]`
- **Why**：**AI is interpreter, not detector**——CRC / TransactionStatus / Timeout / 异常码 / 统计 / 延迟
  的权威只在 Zero-Qt Core；LLM 只解释既有结构化事实。API Key 为 BYOK 进程环境变量，
  无 Key 时核心功能完整可用。`[V-031]`
- **Problems**（本阶段缺陷密度最高）：
  1. **ISSUE-004 Diagnosis 纵向 overflow**：QML `Layout.*` 附加属性在**非 Layout 父**下被静默忽略
     （`GroupBox.contentItem` 不是 Layout parent），导致 viewport≈0、内容绘制出边框、delegate 溢出覆盖 heading。
     经**四轮**定位（含 runtime geometry 取证 `groupH=280 colH=245 flickH=133 …`），
     最终收敛为 workspace 结构问题 → **Horizontal SplitView**（左 Diagnosis / 右 Recent Transactions，各自独立滚动）。`[V-032]`
  2. **ISSUE-005 AI "操作被取消"**：`QNetworkReply::OperationCanceledError` 的 Qt 本地化 `errorString()`
     被原样 emit 成用户文案；同时存在**双 timeout owner 竞态**
     （`QNetworkRequest::setTransferTimeout(30s)` + `QTimer(30s)`，两者都会 abort 且 `finished` 回调无从分辨谁取消）。
     修复：`enum class AiAbortReason`（abort **之前**记录 reason）+ 移除 native transfer timeout
     （QTimer 单一 owner）+ 生产 timeout 30s → **90s** + 任何路径不再把 Qt errorString 当用户文案。`[V-033]`
  3. **ISSUE-006 AI 解释过度归因**：4 笔小样本被写成"链路稳定性较差 / 协议状态混乱"，
     0x02（Illegal Data Address）被归因为物理链路，possible cause 被定调为 typical cause。
     逐项核验 A–F 后确认**责任只在 prompt 语言层**（Core / Client / Controller / QML 零责任）。
     修复只改一个文件 `DiagnosisPromptBuilder.cpp`：追加 Deterministic Status Semantics（正例语义）+
     Mixed Error Independence + Facts/Explanations/Checks 区分 + **Evidence Scope Guard**
     （无条件 `evidence_scope=current_observed_batch`，替代原设计的 `<=10 → small_sample` 阈值）。`[V-034]`
  4. **ISSUE-007 Live Agent tool budget exhaustion**：真实 run 在 final answer 前触发
     `ToolCallLimitExceeded`（当时 `MAX_TOTAL_TOOL_CALLS=3`）。RCA 严格区分**已证明事实**与
     **不可证明事实**（exact tool-call sequence 在 deployed UI 不可观察，拒绝编造）。
     修复：`rounds=3` 不变、`total 3 → 6` + prompt planning discipline；同题 Live re-validation PASS。`[V-035]`
  5. **ISSUE-008 / ISSUE-009**：合法 0x02 提问偶发 `InvalidResponse`；额度不足时 provider 失败**静默不可见**。
     两例的**历史 exact root cause 至今 UNKNOWN**（ISSUE-008 精确 producer 范围为 Client ①~③ / Runtime ④；
     ISSUE-009 silent breakpoint 未复现，Live quota reproduction count = 0）。
     处置：契约**硬化**（B20 whitespace 全族 / B24 reasoning-only 不升级 / B25 非 string fail-closed / ag21/ag22）
     + `Disposition: MONITORING / NON-BLOCKING`，**不编造 RCA**。`[V-036]`
- **Verification**：`DIAG-A01~A15`、`AI-B01~B25`（fake provider）、`AGENT-A01~A13`、
  `AGENT-B01~B23`、`agent_integration` UI-AG01~AG22；**Gate 0 真实 ModelScope native tool calling PROVEN**
  （tools → tool_calls → role=tool → final）；Live 证据一次性、审批制预算。`[V-037]`
- **Final result**：M6 DONE；ADR002（Read-only Tool Architecture）Accepted；T011/T012 全部收口。
- **Representative commits**：`06ef801`（规则诊断基线）、`f087275`（ModelScope 解释）、
  `9921efd`（只读工具层）、`da453a7`（native tool-calling runtime）、`01841b1`（ISSUE-006 修复）、
  `e922c19`（ISSUE-007 预算放宽）。
- **Interview value**：本阶段是"工业软件里 AI 怎么用"的最佳答法来源——事实通道与语言通道分离、
  否定式规则不足以塑造先验（必须给正例语义）、资源上界要按维度分别治理、拒绝编造 RCA 也是工程能力。
- **Sources**：`docs/issues/ISSUE-004~ISSUE-009`、`docs/adr/ADR002-readonly-tool-agent-architecture.md`、
  `docs/tasks/T011`、`docs/tasks/T012`、`docs/06_UI_LANGUAGE_POLICY.md` §5。

---

## P7 — 最终集成与人工视觉收口（M7）· 2026-09-11 → 2026-09-12

- **任务 / 时间**：T013（Phase A 审计 → B 深色 polish → C Light+TabBar → D → E）；2026-09-11 → 09-12
- **Goal**：把功能完备的产品收敛到"人工视觉可接受"的成品级呈现 + README/DEMO_GUIDE 重写。
- **Implemented**：视觉样式 V1~V7、用户可见术语指引、Light + TabBar 三页布局、稳定事务表格、
  串口端口刷新修复、比例列宽、README / `docs/05_DEMO_GUIDE.md` 重写。`[V-038]`
- **Technologies**：Qt Quick Controls 2 style 切换（最终 `Fusion`）、SplitView、proportional column widths。
- **Why**：**自动化 PASS ≠ 人工视觉 PASS**。本阶段的核心价值不是代码而是这条纪律。
- **Problems**：**Manual Visual Review 连续三轮 FAIL**：
  Phase B（深色对比度/层次）FAIL → Phase C（串口空态 / Tab 边框 / 表格对齐）FAIL →
  Phase D（串口空态视觉 + 表格宽度）FAIL/PARTIAL → Phase E 才 PASS。
  期间另发现 **Qt Quick Controls 原生 style 会静默忽略 QML 自定义**（改用 `Fusion`），
  以及"串口枚举为 0"经 Qt 同 runtime 取证确认是**机器 serial availability 变化，非应用回归**。`[V-039]`
- **Final result**：Manual Visual Re-Review PASS；失败 candidate（`aea1e64` / `5a2f60c` / `a25d63c`）
  **全部保留在 Git 历史**（证据不删除）。`[V-040]`
- **Representative commits**：`aea1e64`、`5a2f60c`、`a25d63c`（三次失败 candidate）、
  `99f17d6`（Phase E 修复）、`325f27c`（Phase E 视觉 PASS 收口）。
- **Interview value**：测试金字塔之外的人工验收证据；质检纪律的代价与收益。
- **Sources**：`docs/tasks/T013`、`docs/07_FINAL_PROJECT_REVIEW.md` 第四章 T013、
  `docs/04_TEST_STRATEGY.md` §3 GUI。

---

## P8 — V1 事实总账与知识资产化（M8 / M8.1）· 2026-09-12 → 2026-09-14

- **任务 / 时间**：M8 系列（无独立 T0xx 编号）+ M8.1 诊断覆盖审计；2026-09-12 → 09-13
- **Goal**：把 115 个提交、14 份任务档案、9 份 Issue、2 份 ADR 的证据固化为可面试的事实总账与知识所有权地图。
- **Implemented**：`docs/07_FINAL_PROJECT_REVIEW.md`（事实总账，十章）、
  `docs/08_KNOWLEDGE_OWNERSHIP.md`（1921 行架构知识所有权，含"15 个为什么"、Code Navigation Map、
  Architecture Red Flags）、`docs/09_DIAGNOSTIC_COVERAGE_AUDIT.md`（14 场景覆盖审计与 Claim Risk）。`[V-041]`
- **Problems / 重要发现**：
  1. **fixture 缺陷**：`samples/demo_v2.mlog` 的 14 个场景中，独立重算 CRC 发现 **19/20 条 wire CRC 错误**
     （仅 S1 request、S5 request 正确）——该 fixture 从未被采用为测试样本。`[V-042]`
  2. **产品定位口径风险**：文档中"智能诊断平台 / 诊断平台"在无限定词时容易被理解为全功能码、全物理层覆盖，
     而实测覆盖是 FC03-centric。审计给出收敛措辞建议（`FC03-centric、transaction-level、deterministic diagnostics`）。`[V-043]`
  3. **术语风险**：`TransactionStatus ≠ Device Health State`——UI 列头「设备」「状态」会把同一地址的四笔事务
     误读成"设备的四种健康度"。审计提出候选术语（设备地址 / 事务结果），**只记录不实施**。`[V-044]`
- **Verification**：独立 CRC 复核以两条 KAT 锚定；真实 `libmodbuslens_core.a` 只读 harness 实测；
  证据分级八类（Known from current code / tests / bytes / calculation / docs / protocol reference /
  Inferred / Hypothesis only / Not observable / Not supported）。`[V-045]`
- **Final result**：M8 / M8.1 完成；`docs/09` 的 P0/P1 建议（ProtocolError detail preservation、
  invalid request 作为可观察事实、broadcast 语义）**直接催生 T014 与 T015**。`[V-046]`
- **Representative commits**：`2a5be8e`、`eb42947`、`5167d14`、`017d978`、`81ae7e0`、`eef075e`、`19fa2cb`、
  `4039ade`、`dab9b5f`、`5f21911`。
- **Interview value**：**主动审计自己的覆盖缺口**比宣称能力更有说服力；fixture 自身也要被审计。
- **Sources**：`docs/07_FINAL_PROJECT_REVIEW.md`、`docs/09_DIAGNOSTIC_COVERAGE_AUDIT.md` §5–§6/§24/§26。

---

## P9 — 被动回放扩展与广播语义定案 · 2026-09-13 → 2026-09-14

- **任务 / 时间**：T014（诊断细节保留）、T015（Passive Replay Expansion，Part A/B/C + post 基准与复盘）；2026-09-13 → 09-14
- **Goal**：解决 `docs/09` 审计暴露的三处信息损失与模型缺口。
- **Implemented**：
  - `TransactionIssue`（响应侧，`TransactionIssueCode`）+ `TransactionRequestIssue`（请求侧）**正交**旁挂模型，
    带稀疏 optional 载荷（`expectedAddress`/`actualAddress` 等按需填充）。
    生产不变量：`ProtocolError ⇒ issue.has_value()` **只在 `analyzeFunction03Transaction()` 生产输出中成立**。`[V-047]`
  - `PassiveTransactionAnalysis`（`analyzeObservedTransaction`）：per-record 化——语法失败仍整文件 fail，
    有效 RTU 请求之后的 semantic-invalid / unsupported 全部按记录保留；
    `UnsupportedObservedTransaction` 是**显式事实**，绝不伪装成 `TransactionStatus`。`[V-048]`
  - FC06 被动语义（request decoder + exact-echo matcher）、Function 0x10 被动语义（structural reader +
    normal response matcher）、generic exception matcher、broadcast 集 = `{0x06, 0x10}`。`[V-049]`
  - **ADR-003**：新增第七状态 `ExpectedNoResponse`（response expectation / observed outcome），
    统计公式 `successRate = success / (completed − expectedNoResponse)`，分母 0 ⇒ `nullopt`。`[V-050]`
- **Problems**：
  1. **语义审计 bug（0x88 自我匹配）**：generic exception matcher 初版缺前置守卫
     `(request.functionCode & 0x80) == 0`，request=0x88 / response=0x88 会被误判为 "Exception 0x01"。
     由 **Review 前的专项语义审计**用一条 RED 测试抓出（21 passed / 1 failed），最小 guard 修复。`[V-051]`
  2. 统计正交性须显式锁定：requestIssues **不是**第八状态、不产生统计维度（`STAT-B09/B10` / `DIAG-A14`）。`[V-052]`
- **Verification**：`tx 11 passed / 9 failed` → `tx 20/20`（T014 RED→GREEN）；T015 Part C `42 passed / 12 failed` → GREEN；
  passive 目标 13 条断言 RED → `passive 22/22` → `54/54`；ctest `24/24`。`[V-053]`
- **Final result**：T014 / T015 全部 `DONE`；v1.0.0 历史 tag 指向 T015 收口 commit `ae067ab`（**未发布 GitHub Release**）。`[V-054]`
- **Representative commits**：`0ec6e06`（issue model surface）、`2ba719f`（passive core）、
  `02ce302`（MSB guard 修复）、`92ccf16`（Function16 surface）、`ae067ab`（T015 最终锁定）。
- **Interview value**：**主动在 Review 前自审抓出协议语义边界 bug** 比"测试都过"更有力；
  广播难题本质是"期望"与"合法性"两个维度被错误折叠。
- **Sources**：`docs/adr/ADR-003-broadcast-outcome-semantics.md`、`docs/tasks/T014`、`docs/tasks/T015`、
  `docs/11_PROJECT_FINAL_RETROSPECTIVE.md` §4.6、`docs/04_TEST_STRATEGY.md` §3。

---

## P10 — V2 Gate 0 与 M9 UI 现代化 · 2026-09-14 → 2026-09-20

- **任务 / 时间**：V2 Gate 0（`a40d935`）+ T016（M9-A 设计系统）→ T017（M9-B 应用外壳与导航）
  → T018（M9-C Dashboard）→ T019（M9-D 事务与诊断工作区）→ T020（M9-E 品牌/图标/打包）
  → T021（M9-F 最终视觉与可访问性验收）；2026-09-14 → 09-20
- **Goal**：把 V1 "可用级" UI 升级到 release 级桌面产品观感，同时**不改变任何确定性行为**。
- **Gate 0（2026-09-14）**：`docs/11_V2_UPGRADE_PLAN.md` 冻结 V1 行为契约（Modbus Core / Transaction Analysis /
  Replay / Serial / Statistics / Diagnosis / AI / Agent 八节）与 V1 Regression Assets（行为 → 真实测试名映射），
  确立 `extend, do not silently redefine` 原则与 V2 Non-Regression Rules。`[V-055]`
- **Implemented**：
  - 设计系统 `src/ui/qml/DS/DesignSystem.qml`（颜色/字号/spacing/radius token）+ 组件库
    （`PanelCard` / `StatCard` / `AppButton` / `SectionHeader` 等）；
  - 应用外壳与 6 工作区导航（事务 / 总览 / 通信 / 回放 / 诊断 / 设备）；
  - Dashboard 信息层级重构（统计卡 → 分布条 → 需关注结果 → 基线诊断入口）；
  - Transactions 工作区（选中行 + 详情联动 + 键盘导航）；
  - 品牌与图标（`assets/brand`）、CMake 单一版本权威（`project(... VERSION 2.0.0)` → 应用与 Windows VERSIONINFO 同源）、
    `scripts/make_icon.py`、便携打包工作流；`[V-056]`
  - M9-F 焦点与可访问性（可见焦点环、Tab 顺序、1000×700 最小窗口）。`[V-057]`
- **Technologies**：QML 模块（`qt_add_qml_module`）、QML 组件抽取、`grabWindow` + state-oracle harness 截图、
  `windeployqt`、PE VERSIONINFO 资源、`QQuickStyle`。
- **Problems**（本阶段 Issue 密集区）：
  1. **ISSUE-010 QML 模块单例运行时全部解析失败**：`pragma Singleton` + `QT_QML_SINGLETON_TYPE TRUE` +
     qmldir `singleton` 行三者齐备，`QML_IMPORT_TRACE` 证明类型层已按 SINGLETON 解析，
     但运行时每一处 `DS.*` 仍抛 `ReferenceError: DS is not defined`（约 80 条）。
     **机理级根因未完全隔离**（诚实标注），方案改为 **engine root context property**
     （`rootContext()->setContextProperty("DS", ds)`）。`[V-058]`
  2. **ISSUE-011 部署目录手写 qmldir 漂移**：`deploy_windows.bat` 维护了第二份手写迷你 qmldir，
     模块从 1 个类型长到 6 个类型后漂移且缺 `prefer :/ModbusLens/`，导致引擎走磁盘解析路径。`[V-059]`
  3. **ISSUE-012 statistics 布局塌缩**：`PanelCard` 根 Rectangle `implicit 0x0`（anchors 消费父尺寸但
     **不产生**父的 implicit 尺寸）→ 统计区高度 0、两行重叠、侵入 Diagnosis 区。
     修复 = 容器 implicit 来自 content-layout implicit；新增常驻 CTest `qml_geometry_check`（尺寸合同守卫）。`[V-060]`
  4. **ISSUE-013 几何断言变量遮蔽（evidence vacuity）**：`runGeometryAssertions` 内层
     `auto *row2/header/panel` 遮蔽外层同名变量，外层恒 `nullptr` ⇒ 两条断言**恒真、不可证伪**。
     用**变异探针**（比较常数 +1000）证明断言可触发后才修复。`[V-061]`
  5. **ISSUE-014 TransactionsPage delegate 在 model reset 时按失效 index 求值**：
     9 条 `Unable to assign [undefined]` + 1 条 TypeError。M10-C3 首见，当时分类
     `PRE-EXISTING NON-BLOCKING` 不修；**后由 ISSUE-017 修复**（见 P11）。`[V-062]`
- **Verification**：ctest 从 24 → 25（ISSUE-012 新增 `qml_geometry_check`）；
  QML 门禁族（smoke / nav / geometry / focus / evidence-capture）；逐切片 Human Manual Visual Acceptance；
  M9-F F2 生成 15 张最终截图。`[V-063]`
- **Final result**：M9 完成（`0498d6c`）；version authority 定案 **2.0.0**；
  产品首次具备便携 ZIP 发布能力。**版本号冲突（已解决）**：M9-E 期间先记录 2.0.0 决策（`6e248b0`），
  再由 `0463207` 统一应用与 Windows 版本身份；无残留冲突。`[V-064]`
- **Representative commits**：`a40d935`（V2 Gate 0）、`516d3b8` / `4fc934f`（M9-A）、
  `6562dd3`（布局修复 + geometry gate）、`189c62c`（外壳）、`0cd852a` / `ab3a079`（图标 / 打包）、
  `736d957`（focus）、`0498d6c`（M9 收口）。
- **Interview value**：QML 的 `implicit` 尺寸合同、anchors 不产生 implicit；
  **"断言写了"≠"断言在断言"**（变异探针成为守卫类断言的最小验证动作）；多 kit/多 prefix 依赖错位。
- **Sources**：`docs/11_V2_UPGRADE_PLAN.md`、`docs/issues/ISSUE-010~ISSUE-014`、
  `docs/tasks/T016~T021`、`docs/06_UI_LANGUAGE_POLICY.md`。

---

## P11 — M10 主动主站（写路径）· 2026-09-20 → 2026-09-24

- **任务 / 时间**：T022（M10 Active Master v1，含 A/B/C/D/E/F 六段）+
  T023（M10 FC03 Read Result Observability）；2026-09-20 → 09-24
- **Goal**：产品首次获得**主动** Modbus 能力——FC03 读 + 用户确认式 FC06/FC16 写；
  同时修复"M10 = 首次主动"这一能力跃迁带来的可观测性缺口。
- **Implemented**：
  - Active Master 契约基础（`src/core/active/`）：active request 模型、`encodeActiveRequest` 生产编码器、
    FC03 统一历史迁移、FC06/FC16 编码与响应分析；`[V-065]`
  - **写安全基础（M10-C）**：prepared write snapshot（写前快照 + 原值回显）、hidden write confirmation、
    **Confirmation vs Dispatch 分离**（`db2aea9`）、context/keyboard/accessibility 安全
    （`c50dbfe` 证明 rapid Enter 无法逃逸确认）；`[V-066]`
  - FC06 端到端（M10-D）+ FC16 端到端（M10-E）：参数校验（地址/数量/值域）→ 写前快照 →
    确认对话框（目标从站/地址/数量/值清单/PDU/RTU 帧）→ 经已打开的 Serial 会话派发（一次一笔事务）；`[V-067]`
  - **PREVIEW == WIRE 单一事实源**：请求预览字节必须来自 dispatch 使用的同一个 production encoder；
    控制器 preview 入口绝不 dispatch / 绝不建 snapshot / 绝不改 write store（`test_ui_bridge` pv1–pv4 逐字节锁定）；`[V-068]`
  - M10-F：**结构化请求构造器 + 协议透明性基础**（FCnn(0xNN) 统一展示、PDU / 0-based 地址语义、
    uint16 DEC/HEX、Quantity/ByteCount 自动派生）；`[V-069]`
  - T023：FC03 读出可观测性——10 类结果分类法 CLASS-01…10、TX/RX/UI 语义契约、
    响应校验矩阵 14 行、Backend 缺口矩阵 GAP-1…3、读取功能码可编辑 + 波特率扩展。`[V-070]`
- **Technologies**：Qt SerialPort 写路径、`QSerialPortInfo::availablePorts()`（`DIGCF_PRESENT` 实时枚举）、
  PE/`windeployqt` 打包链、`QDialog` 模态确认、DEC/HEX 双制呈现。
- **Problems**：
  1. **FC16 terminal defect（Human review 发现）**：FC16 无响应派发**没有任何可见 terminal**。
     根因 = `handleSerialTransactionCompleted` 的 notice gate 仅覆盖 `0x06`，
     而 harness 只能驱动 0x06 timeout ⇒ **FC16 timeout 从未被 oracle 覆盖**。
     修复 `cc3c6f8`：写超时 notice（`WriteDispatchNoticeKind::WriteTimeoutUnknown`）覆盖 `0x06` 与 `0x10`，
     回归 = `test_write_dispatch::fc10WriteTimeoutNoticeSaysUnknown` + R17（harness `completeWriteWithTimeout16()`）。`[V-071]`
  2. **ISSUE-015 本机 QProcess 无法派生带管道子进程**：`windeployqt` 在最早阶段
     （`Running: qtpaths -query`）失败，`QProcess: CreateFile failed.（ERROR_PIPE_BUSY）`。
     用**与产品完全无关的最小 QProcess 探针** + ctypes 管道微基准定位到
     `PIPE_ACCESS_OUTBOUND + GENERIC_READ` 这一 Qt `QWindowsPipeWriter` 实际使用的配对特有失败
     （`INBOUND`/`DUPLEX` 正常）。定性为**环境缺陷**，产品侧零改动；**拒绝**手工拼装部署树冒充 windeployqt 产物。`[V-072]`
  3. **ISSUE-016 两个真实缺陷同时躲过全部 source-tree 门禁**：
     ① `preview` 声明在**中间** `ColumnLayout` 却被嵌套 `Label` 以无限定名读取 ⇒
     **QML 无限定名解析只含「自身 + 组件 root + id」，不含中间祖先** ⇒ 6 条 `ReferenceError`，
     预览功能实际从未渲染；
     ② 同一提交（`cc3c6f8`）在不换行的 `RowLayout` 内加宽标签，把 `commReadButton` 推出 1000×700 窗口
     （scene x 1186 > 1000），合成点击静默失效。
     检测缺口根因：门禁只查 exit code，而 exe 是 WIN32 子系统程序、未设 `QT_ASSUME_STDERR_HAS_CONSOLE=1`
     时 Qt 诊断写到 OutputDebugString，**CTest 捕获到的 Output 块是空的** ⇒ 门禁实际上什么都没断言。`[V-073]`
  4. **ISSUE-017 / ISSUE-014 闭环**：TransactionsPage delegate 在模型 reset 时对失效 index 再求值，
     产出 9 条 `Unable to assign` + 1 条 TypeError。**生产可达**（AppBar「清空结果」绑定的同一控制器方法）
     ⇒ 本轮修复（delegate 根 `id` + 7 个容错 `readonly property`，表现层统一走该 id）。
     建档纪律教训：ISSET-017 与 ISSUE-014 记录同一现象，建档前应检索 `docs/issues/`。`[V-074]`
  5. **ISSUE-018 writeFoundationPanel 在真实 windows QPA / 1000×700 被裁切**：
     `WriteFoundationSection` 根 `Item` 无 implicit 尺寸（`Item` 默认 `implicitHeight = 0`）
     ⇒ 页面 `ColumnLayout` 认为该 section 高 0，**整页高度计算完全漏掉写区**。
     **为什么直到 portable 验收才发现**：source-tree 六个 QML 门禁**全部**用 `offscreen`，
     而 offscreen 行高 12px vs 真实平台 16px，逐行累计后 offscreen 仍有 41px 余量——**门禁的平台与验收的平台不是同一个**。`[V-075]`
  6. **本机真实 USB 串口热拔检测**：`serialConnected` 只表示本地串口已打开；
     `QSerialPort::isOpen()` 是**句柄自述状态**（设备消失后仍为 true），
     "适配器还在不在"必须问 OS（`QSerialPortInfo::availablePorts()`）。
     新增文档化、不进 ctest 的 `--serial-hotplug-probe` 人工真机仪器，**2026-09-23 实机验收 PASS**
     （真实 USB 串口硬件插拔，与生产 adapter 相同配置打开、打开后 `ioPerformedAfterOpen=0`）。`[V-076]`
- **Verification**：ctest 从 31 → 36 → 37（ISSUE-018 新增 `qml_write_foundation_check_windows` 真实平台门禁）；
  QML 门禁族扩到六个并加 `FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;Unable to assign"`；
  R15 / R16 / R17 生产写路径 oracle；A/B/C/D 四路 exe SHA-256 identity；negative control 均有 RED→精确逆向→复绿。`[V-077]`
- **Final result**：M10-F 收口（`b8abe2c`），verified LKGC 推进（`ed7625a`）；M10-D FC06 里程碑关闭；
  `REAL MODBUS HARDWARE = NOT VERIFIED`（热拔 probe 是适配器存在性验收，**不是**真实从站事务验收）。`[V-078]`
- **Representative commits**：`3e7aaeb`（M10 设计）、`db2aea9`（确认-派发分离）、`42fcd0b`（FC06 dispatch）、
  `d20c07b`（FC06 生产 UI）、`7eb3ba5`（FC16 capability）、`d923a3a`（FC16 生产 UI）、
  `cc3c6f8`（FC16 terminal 修复 + 结构化预览）、`b8abe2c`（M10-F 收口）、
  `a494d9c`（T023 实现）、`352b81c`（读功能码可编辑）。
- **Interview value**：**写能力的安全边界设计**（确认与派发分离、PREVIEW == WIRE、Agent 与写路径隔离）；
  **门禁的可观测性**（要断言某类缺陷，必须先保证该缺陷可观测）；
  **门禁的平台必须与被验收的平台一致**；环境缺陷 vs 产品缺陷的判别方法。
- **Sources**：`docs/tasks/T022`、`docs/tasks/T023`、`docs/issues/ISSUE-014~ISSUE-018`、
  `docs/04_TEST_STRATEGY.md` §3（请求预览 / Local port presence）、`docs/02_ARCHITECTURE.md` D5/D7。

---

## P12 — M11 寄存器读数与解码 · 2026-09-24 → 2026-09-25

- **任务 / 时间**：T024（契约）+ T025（First Slice 单寄存器解码，**无独立任务文件**）
  + T026（Second Slice 32-bit 视图与 word order，**无独立任务文件**）；2026-09-24 → 09-25
- **Goal**：从"读成功"走到"读到的值是什么"，并严格区分 **raw deterministic register data** 与
  **device-specific physical semantics**。
- **Implemented**：`src/core/analysis/RegisterDecode.{h,cpp}`：
  7 种解码类型 `Hex / Binary / UInt16 / Int16 / UInt32 / Int32 / Float32` +
  `RegisterByteOrder`（寄存器内字节序）+ `RegisterWordOrder`（`HighWordFirst` 默认 AB CD / `LowWordFirst` CD AB）+
  `decodeRegisterPair` / `decodeRegisterView`（5 个 decode status 全映射，含 `insufficient_words`）；
  控制器 `readDecodeType` / `readDecodeByteOrder` / `readDecodeWordOrder`（越界写忽略）+
  滑动窗口投影（行 i 解码 `words[i..i+1]`，成功行带 `decodeSpan` 地址范围）；
  详情对话框解析控件；**raw · decoded 双列分离**（原始十六进制/原始十进制恒显，解析结果随配置变化）。`[V-079]`
- **Technologies**：IEEE-754 binary32 位重解释、16/32 位二补码、滑动窗口对齐。
- **Why**：32 位跨寄存器时字序设备间不一致是现场"数据看不懂"的头号原因；
  解码资格按"成功 register-read-compatible 事务"判定（FC03/FC04/custom 一视同仁，**非 0x03 硬编码**）。`[V-080]`
- **Problems**：
  1. **视觉 candidate 撤回**：Human 双击 `build/release/ModbusLens.exe` 遇到三个 loader 错误
     （GCC 13.1 符号缺失于 ambient 旧 `libstdc++-6.dll`）⇒ candidate 定性 `INVALID / NOT LAUNCHABLE`，
     非 UI FAIL、非 M11 代码缺陷，`b95de54` **不回滚**；
     RCA = `build/release` 非 deployed tree + ambient PATH 旧 runtime 先被解析；
     修复 = 自包含 staging `build/m11-visual-candidate/`（windeployqt + `--compiler-runtime` + `--qmldir`）。`[V-081]`
  2. **`commStartHexEcho` 双参 `.arg()` 运行时错误**：QML `Invalid arguments`（自 `352b81c` 起）。
     修复 `d58fd4d`（链式 `.arg`）+ ctest `FAIL_REGULAR_EXPRESSION` 扩为 4 模式（负向对照实测 FAIL→PASS）。
     RCA 措辞收紧：`D:\mingw64` 旧 runtime = **已控复现的 offending candidate，非直接捕获的 loaded path**。`[V-082]`
  3. 两处 RCA 入档（T024 §26 W6）。`[V-083]`
- **Verification**：`register_decode` 19 → **50**（A07–A17 / A28–A31 矩阵 + 失败模式 + view 入口）；
  `ui_bridge` 88 → **95**（READ-D5..D11）；**负向对照 A/B/C**（word order 翻转 19 FAIL / byte swap 跳过 4 FAIL /
  raw 改写 3 FAIL，全部恢复）；Debug + Release ctest **41/41**。`[V-084]`
- **Final result**：M11 CLOSED（`6550ca9`）；LKGC 推进（`03d2dc1`）；最终便携包归档（`ad706f2`）。
  Human first-slice 与 second-slice 均 visual PASS（逐字结论见 BACKLOG T025/T026 行）。`[V-085]`
- **Representative commits**：`954893d`（契约）、`b95de54`（单寄存器解码）、`bc99e6e`（32-bit + word order）、
  `2f767d1` / `cc9193d`（alignment 成文与追认）、`6550ca9`（M11 收口）、`03d2dc1`（LKGC 推进）。
- **Interview value**：raw 与 derived presentation 的**分离原则**；alignment（滑动窗口）是**实现时决策 → 成文 → 审计后追认**，
  不是 pre-frozen 设计——诚实的 provenance 表述。
- **Sources**：`docs/tasks/T024`、`docs/BACKLOG.md` T025/T026 行、`docs/INTERVIEW_NOTES.md` §M11 Second Slice。
- **任务编号说明**：T025 / T026 在 `docs/BACKLOG.md` 中作为独立任务行登记，
  但**实现档案写在 `docs/tasks/T024-m11-register-readout-decode-contract.md` 之内**，仓库无
  `docs/tasks/T025-*` / `T026-*` 文件（`git ls-tree -r --name-only HEAD -- docs/tasks` 实测）。`[V-086]`

---

## P13 — M12 设备档案与手册智能 · 2026-09-25 → 2026-10-06

- **任务 / 时间**：T027（M12 Device Profile & Manual Intelligence，含 M12-A/B/C/D）；2026-09-25 → 10-06
- **Goal**：让"寄存器地址→业务语义"可被**用户权威定义**，并从设备手册中**辅助提取**候选（人工审核后才成为事实）。
- **Implemented**（按 T027 §119.5 与任务档案的切片划分）：
  - **M12-A**：Device Profile 领域模型（`src/core/profile/DeviceProfile.{h,cpp}`：`RegisterEntry{address, type,
    scale, offset, unit, …}`，冻结公式 `decodedValue * scale + offset`）、JSON 持久化
    （路径安全硬化）、lookup/query foundation、span-overlap 规则（同 read function 内拒绝跨条目地址范围重叠）、
    **按 read function code 划分寄存器空间**（`RegisterSpaceKey`）；`[V-087]`
  - **M12-B**：Device Profile 工作区（档案列表 / 详情 / 编辑器）、Register Map 编辑器
    （Add/Edit/Delete + dirty）、Session Active Profile + Communication 选择器、
    Read Result 语义叠加（semantic overlay，解码值 + scale/offset/unit）；`[V-088]`
  - **M12-C C1a/C1b**：手册导入（TXT / Markdown / PDF / DOCX）——PDFium + libzip + zlib
    以 **pinned + 离线物化 + 配置期 hash 断言** 方式引入（`third_party/m12c-c1b/`、
    `scripts/materialize_c1b_deps.py`）、手册库冷启动 hydrate、安全删除、手册预览；`[V-089]`
  - **M12-C C2**：evidence-backed AI candidate extraction——严格 provider adapter、
    **consent-gated** orchestration（云端同意门）、每个候选携带 evidence / 来源页节 / confidence；`[V-090]`
  - **M12-C C3**：人工审核权威——Accept / Edit / Reject，`Human-authored candidate value edit`（C3-H8），
    只有用户确认后才进入 verified Device Profile；`[V-091]`
  - **M12-D**：evidence-backed single-manual Q&A——**app-resolved citations**
    （provider 只能在 app 提供的上下文块 id 中"选择"，offset/hash 由 app 回填，未知 id fail-closed）、
    结构化输出契约（显式 `enable_thinking=false` + `max_tokens`）、面板可收回 + 长结果可滚动。`[V-092]`
- **Technologies**：QtCore JSON、`std::filesystem`、PDFium（pinned binary）、libzip + zlib（static）、
  ModelScope OpenAI-compatible Chat Completions、`QDialog` 同意门、`Drawer`（position-driven）。
- **Problems**：
  1. **ISSUE-019 `setDisplayName` 缺失 NOTIFY + NC-B1 负向对照变异被提交**：
     `qml_profile_editor_check` 7 项 FAIL，而**控制器 C++ 状态完全正确**
     （`validationText="设备名称必填"`、`dirty=true`），QML 标签仍显示旧值且全程零 QML 警告。
     RCA：NC-B1 负向对照（临时移除 `emit` → 门禁红 → 还原）的**"还原"步骤被跳过**，
     变异源码进入了行为提交 `da07f43`（含 `// NC-B1: dirty not set` 标记注释）。
     流程缺陷 × 技术缺陷叠加：`editorChanged` 是唯一 NOTIFY，getter 按需计算使 C++ 测试对"缺通知"完全免疫。
     修复 `b502ea8` + 新增通知契约测试 `b1c21`/`b1c22`（`QSignalSpy` 把"通知"本身变成被测对象）。`[V-093]`
  2. **ISSUE-020 环境凭据泄漏进确定性 harness**：`MODELSCOPE_API_KEY` 在 Windows **User 级持久存在**，
     注入每个测试进程 ⇒ `--qml-consent-check` 的同意门点击被降级为**真实云端推理**
     （R07 FAIL，`candidateCount=1`；15.87s − 1.87s ≈ 14s ≈ 真实 HTTPS 往返）。
     双缺陷：RC-1 = ambient env 成为 harness 的**隐藏输入**；RC-2 = `qml_consent_check` 缺
     `QT_ASSUME_STDERR_HAS_CONSOLE=1`，把语义失败伪装成"静默崩溃"。
     修复：任何 `--qml-*` harness 进程在创建 controller 前 `qunsetenv` 凭据 + 入口 guard
     （凭据可见即 `refusing to run`）+ 补 stderr 契约；负向对照 MUTATION-NX1 证明 guard 可触发。`[V-094]`
  3. **M12-D R2B Manual Q&A "无法解析"（双根因）**：响应抽取层 + thinking 模式
     （`enable_thinking` 未关）。修复：抽取层 + 请求契约（`chat_template_kwargs` / `max_tokens`）+ 契约测试 qa36。`[V-095]`
  4. **M12-D R2E 引用校验失败**：让 provider 编造 citation offset **不可靠**（本地 round-trip 失败）。
     修复 = **app-resolved citationIds**——模型只能在 app 给的上下文块里"选择"，offset/hash 由 app 回填，
     未知 id fail-closed。教训："LLM 输出只能做选择，不能做权威数据"。`[V-096]`
  5. **M12-B automated acceptance 7 项失败逐项 RCA**：纠正上一轮「归因为 QQuickPopup automation 限制」的
     错误结论——实际 6/7 项是**真实产品缺陷或 harness 缺陷**（分类 A/B/C/D：产品缺陷 / harness 缺陷 /
     不可自动化 / oracle 错误）。`[V-097]`
  6. **C1a Debug 环境 HOLD**：Debug 构建 `ar.exe`/`ranlib.exe` 经 QProcess 路径失败（与 ISSUE-015 同族环境缺陷），
     独立轨道、**不是**同一根因；由 Human 外部 Debug 证据解除。`[V-098]`
  7. **Demo lifetime RCA**：人类语义演示进程提前退出（`13799d6` 修复）。
  8. **release 链两个 fail-closed + candidate 不可重生成**：真实 candidate 打包被 icon 镜像与
     `.a/.prl/.obj/.qrc` 工件规则拦截；worktree 缺 git-ignored distfiles。
     教训：**fixture 必须来自真实制品；"ignored ≠ 无关"**。`[V-099]`
  9. **gate 自身缺陷两例（如实记录）**：`property("open")` 应为 `opened`（反射断言静默失效）；
     `cancelCount` 绝对断言应为 drift 比较（跨 stage 累积）。
     教训：反射断言要对关键属性做**存在性校验**；计数器只能做 before/after。`[V-100]`
- **Verification**：ctest 从 **43/44（含 7 项 FAIL）** → **44/44**（ISSUE-019 修复）；
  Release 全量 + Debug 全量；QML 门禁族（profile_editor / register_map / active_profile / profile_semantic /
  manual_import / manual_qa / candidate_review / consent，各有 `_windows` 真实平台变体）；
  PKG 套件 `test_make_canonical_package.py`（PKG-01..26）。`[V-101]`
- **Final result**：M12 各切片逐一切片 Human Acceptance（C1a / C1b / C2 Live / C3 / C3 Edit / manual
  cold-start / manual delete / M12-D Q&A / UX-R1 面板收回 / UX-R2 结果滚动）；M12 关闭（`b56ef61`）；
  LKGC 推进至 `8403eab`（final accepted UX baseline）。`[V-102]`
- **Representative commits**：`e9c376c`（M12 契约）、`1c42aaf`（JSON foundation）、`da07f43`（profile workspace）、
  `b502ea8`（ISSUE-019 修复）、`becadc5`（register map editor）、`f1567a5`（semantic readout）、
  `7bcd2ca`（manual import foundation）、`fb2170e`（PDF/DOCX extraction）、`d899e55`（AI candidate foundation）、
  `19bb9cf`（ModelScope adapter）、`309ba15`（consent-gated orchestration）、`61f641e`（C3 accept/reject）、
  `4a77673`（C3 Edit）、`80e4326`（manual Q&A）、`8416fe7`（app-resolved citations）、
  `9c065f2`（Q&A 结果可滚动 = **当前 verified LKGC**）。
- **Interview value**：**负向对照必须有"还原后复绿"的闭环证据**；
  **测试的确定性必须包括环境输入**；**安全边界（凭据/同意）要有结构化防泄漏**；
  **LLM 只能做选择，不能做权威数据**；fixture 必须来自真实制品。
- **Sources**：`docs/tasks/T027`（§24–§35 契约冻结、§36–§45 M12-B、§47–§53 M12-C C1a、
  §76–§94 C2/C3、§108–§118 发布链、§119 closure）、`docs/issues/ISSUE-019`、`docs/issues/ISSUE-020`、
  `docs/INTERVIEW_NOTES.md` F5/F7。

---

## P14 — 发布工程与仓库封板 · 2026-10-06 → 2026-10-08

- **任务 / 时间**：T028（Release Prep 审计）、T029（UI Release Audit）、T030（UI FREEZE + 最终截图，
  含 R2 重拍）、T031（A 文档真实性 / B README / C LICENSE + GitHub 元数据）；2026-10-06 → 10-08
- **Goal**：把"能跑的产品"变成"可公开发布、可被独立验证、文档与事实一致的仓库"。
- **Implemented / 结果**：
  - **candidate 契约**（`cmake/modbuslens_generate_candidate.cmake`）：从零重建 candidate tree、
    逐文件 SHA-256 manifest、Release/Debug 同一 configuration-aware 生成逻辑；`[V-103]`
  - **fail-fast 打包器** `scripts/make_package.py`：deploy → staging → 结构/凭据/绝对路径检查 →
    manifest（1498 entries）→ ZIP（1499 entries）→ fresh extraction 校验 → minimal-PATH 三模式 → external-CWD；
    `[V-104]`
  - **v2.0.0 公开发布**：tag `v2.0.0`（annotated，object `bdabd6fd…` → commit `b18e378f…`），
    Release 资产 `ModbusLens-2.0.0-windows-x64.zip` = **35,814,735 B**，
    SHA-256 `6db918f35b62b3bb7638b7e0660cb74d9137d22aada1ca512513bd0a2654449d`；`[V-105]`
  - **公开资产独立验证（R5G）**：直接公开下载 35,814,735 B 并算 SHA-256 与验收档案比对——
    **不解压、不重打包**，字节级一致；`[V-106]`
  - **人工验收 H1–H8 PASS**（启动 / 解压 / Simulator 演示 / Replay 样例 / 三模式边界 /
    内容泄漏检查 / 手册问答收回 / 长结果滚动）；`[V-107]`
  - **发布工程**:`docs/04_TEST_STRATEGY.md` 新增 CURRENT STATE 快照（70 目标）；
    `docs/05_DEMO_GUIDE.md` 更新为发布时点口径 + §8 POST-M12 扩展；
    `docs/07` / `docs/11` 顶部各加**历史快照声明**；`CHANGELOG.md` 新建；
    **`LICENSE`（MIT，`Copyright (c) 2026 TheVoid2021`）创建** + README §12 同步 + **发布溯源说明**
    （LICENSE 于 v2.0.0 发布**之后**加入，已发布 ZIP 不包含该文件、未被重建——证据 = 两份 package manifest 中
    LICENSE 条目数均为 **0**）；`[V-108]`
  - **README 最终形态**：12 节 + 三张已验收截图（01 hero / 02 transactions / 04 diagnosis baseline），
    Mermaid 架构图内嵌（**不使用**无生成源的 `docs/assets/architecture.png`，不伪造重绘）。`[V-109]`
- **Problems**：
  1. **T028 审计发现的文档真实性缺口**：GAP-03（`02_ARCHITECTURE` 仍称"Serial 永远只发 FC03 读"）、
     GAP-04（`04_TEST_STRATEGY` 停在 ctest 24/24 + "无 FC06 encoder" grep 锚；`05_DEMO_GUIDE` 仍写 23/23）、
     GAP-01/02（59 张截图**全部**早于 M10/M11/M12 UI）、GAP-05（无 LICENSE）、GAP-07（无 CHANGELOG）。
     全部由 T031-A 关闭或在 T031 中显式处置。`[V-110]`
  2. **T029 UI 审计结论**：UI **P0 = 0 / P1 = 0**，v2.0.0 UI 达发布级；
     新增 P2 项 RUI-11..16（留白密度、Replay 空态提示、Agent 无 provider 提示、contentHash 展示、
     演示数据命名、header 版本号）。**未获得视觉证据的状态如实记录**
     （Replay 已加载态、hover/pressed 态、窗口标题栏/任务栏图标）。`[V-111]`
  3. **T030-R2 串口截图重拍 BLOCKED**：Human 拒绝 05-serial.png（原因为 ① `COM_HARNESS` 暴露自动化 fixture 命名；
     ② 页面同时出现"串口已打开"与"未检测到串口"视觉矛盾；③ 40 registers 信息密度过高——
     **不是 UI layout defect**）。重拍失败根因 = 产品**诚实**枚举：
     本机 COM3/COM4 是 Bluetooth phantom 口，Qt setupapi 按 `DIGCF_PRESENT` 正确跳过，
     而 .NET `SERIALCOMM` 注册表视图包含它们（PowerShell 看得到、产品看不到）——**产品行为正确**。
     处置 = **DROP 05 FROM README**，最终截图集 4 张；被拒版本保留于 Git 历史 `6982c78`。`[V-112]`
- **Final result**：
  - **T031-A = COMPLETE / HUMAN ACCEPTED；T031-B = COMPLETE / HUMAN ACCEPTED；T031-C = COMPLETE LOCALLY**；`[V-113]`
  - `PRODUCT CODE = UNCHANGED`；`UI FREEZE = ACTIVE`；`TAG = UNCHANGED`；`RELEASE = UNCHANGED`；`PUSH = NONE`；`[V-114]`
  - **本地领先 `origin/main` 7 个提交**（`8fc4f75`、`8956121`、`6982c78`、`1c4f9a0`、`82cea51`、`28656b3`、`69a58ae`）；
    `origin/main` = `2f53197`（POST-M12 closure，该提交及其祖先已在远端）。`[V-115]`
  - 剩余 = **HUMAN ACTION REQUIRED**：GitHub 网页端应用 description / 15 topics / social preview（+ 可选 push）。`[V-116]`
- **Representative commits**：`751eed4`（candidate 打包契约）、`4643e31`（打包）、`b18e378`（发布验收归档）、
  `2f53197`（POST-M12 closure + v2.0.0 tag）、`8fc4f75`（Release Prep 审计）、`8956121`（UI Release Audit）、
  `6982c78`（截图候选）、`1c4f9a0`（T030 R2）、`82cea51`（T031 A）、`28656b3`（T031 B）、`69a58ae`（T031 C）。
- **Interview value**：发布工程是**身份问题**（逐文件哈希 + 字节级公开验证），不是"文件在不在"；
  "candidate 契约 + fail-fast 打包器"如何让"打包错产品"变成不可能；
  诚实记录"未验证的状态"比补一张好看的图更有价值。
- **Sources**：`docs/tasks/T028`、`docs/tasks/T029`、`docs/tasks/T030`、`docs/tasks/T031`、
  `docs/ENVIRONMENT.md` §4b、`CHANGELOG.md`、`README.md` §12、`docs/PROJECT_STATUS.md` 顶部块。

---

## 附 A — 里程碑 → 阶段映射（`docs/07` 第五章演进，含 V2 后续）

| M | 任务 | 阶段 | 里程碑后首次具备 |
| --- | --- | --- | --- |
| M1 | T001, T001.1 | P1 | 工程骨架 + 文档体系 + 最小 Qt 应用 |
| M2 | T002–T004 | P2 | Modbus 协议核心（Zero Qt 静态库） |
| M3 | T005–T006 | P3 | 确定性模拟与故障注入（无硬件演示能力） |
| M4 | T007–T008 | P4 | 事务分析 + 统计 + QML Dashboard（可演示闭环） |
| M5 | T009–T010 | P5 | 回放与串口两种数据源 |
| M6 | T011–T012 | P6 | 确定性诊断 → LLM 解释 → 只读 Agent |
| M7 | T013 | P7 | 成品级视觉/文档/演示收口 |
| M8 | （无独立 T0xx） | P8 | 事实总账 + 知识所有权 + 覆盖审计 |
| — | T014–T015 | P9 | 第七状态 + 正交 Issue + per-record 被动分析 |
| M9 | T016–T021 | P10 | 设计系统 + 六工作区 + 品牌打包（v2.0.0 版本权威） |
| M10 | T022–T023 | P11 | 主动 FC03 读 + 用户确认式 FC06/FC16 写 |
| M11 | T024（含 T025/T026） | P12 | 寄存器读数解码（7 类型 + 字节序/字序） |
| M12 | T027 | P13 | 设备档案 + 手册导入/提取/审核 + 引用校验问答 |
| — | T028–T031 | P14 | candidate 打包契约 + v2.0.0 公开发布 + 仓库封板 |
| — | T032 | （本任务） | 面试与移交事实底稿（Phase A） |

`[V-117]`

## 附 B — 阶段之间的**不可省略**因果链（面试深挖用）

```text
P2  帧合法性               → P4  事务结局（"这笔事务发生了什么"）
P4  归一化丢失原因         → P9  TransactionIssue（判定时刻的原因去哪了）
P9  归一化丢不了请求自身违规？→ P9  TransactionRequestIssue（正交两维）
P8  覆盖审计发现 broadcast 无诚实落点 → P9  ADR-003 第七状态 + 统计公式
P9  理解历史写事务（被动）  → 与主动写权限**严格分离**（P11 才引入用户确认式写）
P6  AI 只解释不裁决        → P13 LLM 只能做选择不能做权威数据（app-resolved citations）
P4  手动 smoke 挡住启动环境问题 → P11 门禁可观测性 / 门禁平台一致性
P7  自动全绿仍被人工视觉连打三次 FAIL → P14 UI FREEZE + 截图 freshness 规则
```

`[V-118]`