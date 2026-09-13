# PROJECT_STATUS — 项目状态单一事实源

> 规则：本文件在每个任务**完成时**更新（AGENTS.md 工作纪律 5）。任何协作者以此文件为真相，其次才是聊天上下文。
| Last Known Good Commit | **`cc8393a`**（T014 Diagnostic Detail Preservation 完成：`TransactionIssue` 正交诊断细节 + prompt/agent/UI additive 传播；用户 Manual UI Review PASS 后正式推进。历史值：`99f17d6`（T013）、`3572cf7`、`b322cc3`、`01841b1`、`9e79558`、`bb3f3b4`、`85699ff`、`06ef801`） |

## 状态面板

| 项 | 值 |
| --- | --- |
| 当前版本 | **0.1.0**（2026-09-05，T001 建立；T001.1 未改代码，版本不变） |
| 当前 Milestone（Current Milestone） | M1~M6 ✅；**M7 ✅ DONE（T013 Final Integration & Demo 完成，用户视觉 PASS）**；全部里程碑完成 |
| Last Known Good Commit | **`cc8393a`**（T014 Diagnostic Detail Preservation 完成：`TransactionIssue` 正交诊断细节 + prompt/agent/UI additive 传播；用户 Manual UI Review PASS 后正式推进。历史值：`99f17d6`（T013）、`3572cf7`、`b322cc3`、`01841b1`、`9e79558`、`bb3f3b4`、`85699ff`、`06ef801`） |
| Build 状态 | ✅ **通过** — Debug/MinGW 13.1.0/Qt 6.11.1（含 QtSerialPort 组件）/CMake 3.30.5，零警告（clean 全量重建 142 targets） |
| Test 状态 | ✅ **22/22 通过**（ctest 22 个测试目标全绿（T012 Part A agent_tools：AGENT-A01~A09+A10；Part B Phase 1 agent_runtime：AGENT-B01~B18）
| 已完成任务 | T001 · T001.1 · T002 · T003 · T004 · T005 · T006 · T007 · T008 · T009 · T010 · **T011** |
| 当前任务（Current Task） | **T015 Passive Replay Expansion — IN PROGRESS**（**Phase B IMPLEMENTED / AWAITING REVIEW**：七状态 `ExpectedNoResponse` + passive analyzer + FC06 + per-record 化；LKGC candidate = `6944fd5`；**Function 0x10 = NOT STARTED（Part C）**；Manual UI Smoke = WAITING FOR USER） |
| 最近完成任务（Last Completed Task） | **T014 Diagnostic Detail Preservation**（DONE；verified LKGC `cc8393a`） |
| 当前阶段（Current Phase） | T015 Phase B 实现完成（clean 152 零警告、ctest 24/24、qml smoke、deploy+minimal-PATH 全过；待用户 Review） |
| 下一步动作（Next Action） | **用户 T015 Phase B Review（含 Manual UI Smoke 清单）**；批准后：Part C（Function 0x10 normal semantics）另行立项 |
| 下一 Part（Next Part） | **T012 Part B Phase 2 — Controller Agent Integration + QML Agent UI（ST-A integration test requirement 已入档）** |
| 下一任务（Next Task After T011） | **T012 Agent Tools** |
| Known Issues | 见 §4 |
| 开发环境 | 见 §5 |
| 标准 Build/Test 命令 | 见 §6 |

## 1. 已完成任务

| 任务 | 标题 | 结果摘要 | 档案 |
| --- | --- | --- | --- |
| T001 | 项目引导：骨架、文档体系、最小 Qt6 应用 | 骨架可构建、smoke 测试通过、文档体系建立 | [T001](tasks/T001-project-bootstrap.md) |
| T001.1 | Bootstrap Documentation Cleanup | 文档更名、preset 示例模板、环境文档重写、BACKLOG 细粒度拆分 | [T001.1](tasks/T001.1-bootstrap-cleanup.md) |
| T002 | Modbus CRC16 | `modbuslens_core` 落地；CRC-16/MODBUS 按位实现 + 6 个测试（T01–T06）RED→GREEN 全程留痕；全项目 ctest 2/2 | [T002](tasks/T002-modbus-crc16.md) |
| T003 | Modbus RTU Frame Model | `ModbusRtuFrame`（address/functionCode/data，value 语义，不存 CRC）+ `isExceptionResponse`；FRAME-T01~T04 全绿；ctest 3/3；范围修订：fuzz 移除、编解码归 T004 | [T003](tasks/T003-modbus-rtu-frame-model.md) |
| T004 | Modbus RTU Codec | Part A：`ModbusRtuCodec`（Frame↔wire，variant 错误模型）+ RTU-A01~A07；Part B：`Function03`（0x03 三个 decoder + 语义模型）+ F03-B01~B12（含 V1.1b3 官方金样）；ctest 5/5 | [T004](tasks/T004-modbus-rtu-codec.md) |
| T005 | Simulator Basic Slave | `SimulatedSlave`（单地址 + 连续寄存器文件 + 0x03，const 纯应答端点）；SIM-T01~T07 + SIM-I01 全链路闭环（T002→T005 首次通电）；ctest 7/7；范围收缩兑现（IFrameSource 等推迟） | [T005](tasks/T005-simulator-basic-slave.md) |
| T006 | Deterministic Fault Injection | `applySimulationFault` 四模式（None/DropResponse/CorruptCrc/ArtificialDelay，确定性、wire 层、元数据延迟）；FAULT-T01~T05 + I01/I02 全绿；SimulatedSlave 零修改；ctest 9/9 | [T006](tasks/T006-fault-injection.md) |
| T007 | Transaction Analysis | Part A：`analyzeFunction03Transaction`（六状态、跨帧校验、双不变量）+ TX-A01~A12/I01~I03；Part B：`summarizeTransactions`（三计数/五分类/optional rate 与 latency/四不变量）+ STAT-B01~B08/I01；ctest 13/13 | [T007](tasks/T007-transaction-analysis.md) |
| T008 | Qt Quick / QML Analysis UI | Part A：QML 迁移 + AnalysisController/TransactionListModel 桥接 + UI-A01~A06 + Manual Visual Smoke 12/12；Part B：runDemoBatch/clearDemo 确定性 Demo Dashboard + UI-B01~B06 + QML Presentation 修正 + Manual Demo Smoke PASS；T008.1 standalone 部署（ISSUE-002 RESOLVED）；ctest 14/14 零警告 | [T008](tasks/T008-qt-quick-qml-analysis-ui.md) |
| T009 | Replay Mode（Replay Log Format + Replay Core / Replay UI Integration） | Part A：`.mlog` v1 解析 + 批量回放分析（request 可信链三错误码；坏 response 为诊断事实；复用 T004/T007）；REPLAY-A01~A08+I01~I05（24 函数）。Part B：loadReplayFile 原子发布 + clearResults + 错误/来源状态 + FileDialog；UI-R01~R08；canonical sample `samples/demo_v1.mlog`（git mv 单一源头）；**Manual Replay Smoke 用户确认 PASS**；ctest 16/16 零警告 | [T009](tasks/T009-replay-mode.md) |
| T010 | Serial Mode（Serial Transaction Runtime + Adapter / Serial UI Integration） | Part A：`SerialTransactionSession`（Zero Qt 单事务状态机 + 修正版 framing + timeout 双路语义）+ `encodeReadHoldingRegistersRequest` + QtSerialPort 薄 adapter；SERIAL-A01~A16 全绿；ISSUE-003 RESOLVED（QtSerialPort 组件多 kit 错位）。Part B：transport/transaction 生命周期拆分（openPort/startTransaction/closePort）+ Controller serial 全套 + QML Serial Controls；UI-S01~S10 + SERIAL-I02（PE-4 有界）/I03/I05；**Manual Serial UI Smoke 用户确认 PASS；Hardware Smoke = NOT RUN（hardware unavailable）**；ctest 18/18 零警告；Qt6SerialPort.dll provenance 验证 | [T010](tasks/T010-serial-mode.md) |

## 2. 当前任务

- **T015 — Passive Replay Expansion（IN PROGRESS；Phase B = IMPLEMENTED / AWAITING REVIEW）**。Gate A~F 全批（Phase A Review）后落地：`TransactionStatus::ExpectedNoResponse`（七状态，ADR-003 Accepted）+ 批准统计公式（completed 含广播；rate = success/(completed−expectedNoResponse)；分母 0 ⇒ nullopt）；`PassiveTransactionAnalysis`（request 分类单点、generic exception 单点、FC03 复用 T007、broadcast=addr0∧FC06）；`TransactionRequestIssue`（Gate B 独立于 T014 issue）；`Function06` 被动语义（无 encoder）；Replay per-record 化（analyzed + unsupportedRecords 显式披露，Gate F）；Baseline `ExpectedNoResponseObserved` + Healthy 四条件；Prompt/Agent/UI additive；RED 13 条断言失败 → GREEN ctest 24/24；clean 152 零警告；qml smoke 与 deploy+minimal-PATH PASS；Active Serial 写权限零新增（grep 取证）。**Function 0x10 = NOT STARTED（Part C，另行立项）**。LKGC candidate = `6944fd5`（待 Review）。详见 [T015 档案](tasks/T015-passive-replay-expansion.md) + [ADR-003](../adr/ADR-003-broadcast-outcome-semantics.md)。

## 3. 下一任务

- **用户 T015 Phase B Review**（含 Manual UI Smoke；批准后推进 verified LKGC）。
- **T015 Part C — Function 0x10 normal semantics**（已批准但仍 NOT STARTED；Phase B 明确禁止实现）。
- Backlog：Replay v2 timing、UART diagnostics、register-map 语义层、per-device 时间窗等。

## 4. Known Issues（当前已知问题）

| # | 问题 | 影响 | 状态/应对 |
| --- | --- | --- | --- |
| K1 | 本机 Qt 在 AutoMoc 阶段出现 qtlicd 证书服务不可用的构建期警告 | 仅为构建日志噪音，产物正常 | **临时环境处理**：仅在本机（gitignored 的 CMakeUserPresets.json）注入 `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`。项目代码与提交文件**不依赖**该变量（T001.1 已澄清措辞，见 [ENVIRONMENT](ENVIRONMENT.md) §6） |
| K2 | 系统 PATH 中存在 Anaconda 的 Qt5 qmake 与 MinGW g++ 8.1.0（过旧） | 若直接裸用会产生 Qt/编译器 ABI 不匹配 | 规避：统一通过 `*-local` preset 注入 Qt 自带工具链；见 [ENVIRONMENT](ENVIRONMENT.md) |
| K3 | `Could NOT find WrapVulkanHeaders`（configure 提示） | 无（Qt Widgets 不依赖；仅影响未来 QtQuick/RHI 功能） | 记录观察，不处理 |
| K4 | ~~**[ISSUE-002] Explorer 启动 modbuslens.exe 失败**~~ **[RESOLVED ✅]**（无法定位输入点 `_ZNSt3pmr20get_default_resourceEv` 于 Qt6Gui.dll） | 仅影响"不经终端直接双击启动"场景；终端前置正确 PATH 后启动正常；**正确 runtime 下用户已人工确认 UI 12/12 正常** | **已解决（T008.1）**：`scripts/deploy_windows.bat` 生成 build/deploy 独立目录（runtime provenance SHA256=编译器 bin VERIFIED + minimal-PATH smoke PASS）；**用户 Explorer 双击确认 PASS**。ISSUE-002 置 RESOLVED |
| K5 | ~~**[ISSUE-003] 本机 Qt 6.11.1 未安装 QtSerialPort 组件**~~ **[RESOLVED ✅]**（首次补装落错 MSVC kit `D:\QTDesign`，随后装到正确 MinGW kit；五步实证+临时 CMake probe 全过） | 曾阻塞 T010 Part A 的 Qt adapter/SERIAL-I01 | 已解决；T010 Part A 全绿交付 |
| K6 | [ISSUE-006](issues/ISSUE-006-ai-explanation-overattribution.md) AI 解释过度归因——4 笔小样本（1 CRC + 1 Timeout + 1 Exception 0x02）被渲染为“链路稳定性差/协议混乱/往往源于物理层/间歇中断”等确定语气结论 | 确定性数据零损坏；措辞可能误导排查方向、违背“possible cause ≠ certain cause”产品原则 | **RESOLVED ✅**：evidence-scope guard + 状态正例语义 + 混合错误独立性 + Facts/Explanations/Checks 纪律；AI-B14~B17；用户 Manual UI Regression Smoke PASS + 经授权 Live ModelScope Smoke 五项验收全 PASS；verified LKGC = `01841b1` |
| K7 | [ISSUE-007](issues/ISSUE-007-live-agent-tool-budget-exhaustion.md) Live Agent tool budget exhaustion（MAX_TOTAL_TOOL_CALLS=3 对多步只读诊断过严，真实 run 触发 ToolCallLimitExceeded） | 合法多步问题无法完成一次诊断（超限即安全终止） | **FIXED / AWAITING LIVE RE-VALIDATION**（`e922c19`：total 3→6 + planning discipline；rounds=3 不变；待用户授权 Live Re-Smoke 后 RESOLVED） || K7 | [ISSUE-007](issues/ISSUE-007-live-agent-tool-budget-exhaustion.md) Live Agent tool budget exhaustion（MAX_TOTAL_TOOL_CALLS=3 对多步只读诊断过严，真实 run 触发 ToolCallLimitExceeded） | 合法多步问题无法完成一次诊断（超限即安全终止） | **LIVE RE-VALIDATION PASS / AWAITING USER FINAL CLOSURE**（`e922c19`：total 3→6 + planning discipline；同一问题一文未改的唯一 run 约 60s 产出合规 final answer，零超限；用户最终确认后 RESOLVED） || K7 | [ISSUE-007](issues/ISSUE-007-live-agent-tool-budget-exhaustion.md) Live Agent tool budget exhaustion（MAX_TOTAL_TOOL_CALLS=3 对多步只读诊断过严，真实 run 触发 ToolCallLimitExceeded） | 合法多步问题无法完成一次诊断（超限即安全终止） | **RESOLVED ✅**（`e922c19`：total 3→6 + planning discipline；同题 Live Re-Validation PASS；用户 Final Review PASS） |
## 5. 开发环境

| 项 | 值 |
| --- | --- |
| OS | Windows 11 (10.0.26200)，Shell: Git Bash |
| CMake | 3.30.5（Qt 自带 `D:/QT/Tools/CMake_64`） |
| 生成器/构建器 | Ninja 1.12.1（Qt 自带） |
| 编译器 | MinGW-W64 g++ 13.1.0（`D:/QT/Tools/mingw1310_64`，posix-seh） |
| Qt | 6.11.1（`D:/QT/6.11.1/mingw_64`，Desktop MinGW kit） |
| Git | 2.55.0（identity: Zhiwei Fu <1627017595@qq.com>） |

> 各平台安装方法与常见坑：[ENVIRONMENT.md](ENVIRONMENT.md)

## 6. 标准 Build/Test 命令

```bash
# —— 本机（Windows，Qt 6.11.1 MinGW；工具链由 CMakeUserPresets.json 注入，不提交）——
cmake --preset debug-local          # configure
cmake --build --preset debug-local  # build
ctest --preset debug-local          # test

# —— 其他平台（Qt 在系统默认位置）——
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

# Qt 不在默认位置时：
cmake --preset debug -DCMAKE_PREFIX_PATH=<Qt6前缀>
```

## 7. 变更记录（本文件）

| 日期 | 事件 |
| --- | --- |
| 2026-09-05 | T001 建立本文件；记录初始环境、构建与测试结果 |
| 2026-09-05 | T001 收尾：回填 LKGC=`aa337f6`，M1 关闭 |
| 2026-09-05 | T001.1 完成（docs-only；提交哈希与信息见 git log / T001.1 档案）；LKGC 依约定不变 |
| 2026-09-05 | T002 启动：Phase A Learning Checkpoint（docs-only）；T002 标记 IN PROGRESS，**未标完成** |
| 2026-09-05 | T002 Phase B（Test Design，docs-only）完成：测试矩阵 CRC-T01–T06 + 优先级 + 接口定案 + Phase C 13 步计划；状态改为四段式表达（Current Task/Phase/Next Action/Next Task After T002） |
| 2026-09-05 | T002 Phase C 完成：`modbuslens_core` + CRC 按位实现 + 6 用例 RED→GREEN；全项目 ctest 2/2、零警告。**T002 DONE**；LKGC 推进至本次代码提交（哈希由 docs-only 回填提交写入） |
| 2026-09-05 | 回填：LKGC = `e8ef30c`（T002 代码提交）；本次 HEAD 为 docs-only 回填提交，二者已区分 |
| 2026-09-05 | T003 完成：`ModbusRtuFrame` 内存模型 + FRAME-T01~T04 全绿（ctest 3/3）；范围修订——fuzz 移出 T003、wire 编解码归 T004；补录 ADR001（最终 UI = Qt Quick/QML，用户于 T003 前确认）。**T003 DONE**；LKGC 推进至 T003 代码提交（哈希由 docs-only 回填提交写入） |
| 2026-09-05 | 回填：LKGC = `a44a6d2`（T003 代码提交）；本次 HEAD 为 docs-only 回填提交，二者已区分 |
| 2026-09-05 | T004 启动：Learning / Scope Refinement（docs-only）——Codec 知识留痕、Part A/B 拆分、BACKLOG 范围同步；T004 标记 IN PROGRESS，**未标完成** |
| 2026-09-05 | T004 Part A Test Design（docs-only）：接口定案（variant 错误模型）、测试矩阵 RTU-A01~A07、Raw bytes 边界、Deferred 决策、14 步实施计划；下一步 = Part A Implementation |
| 2026-09-06 | T004 Part A 完成：`ModbusRtuCodec` 落地 `modbuslens_core`（encode/decode + variant 错误模型），A01~A07 RED（linker error）→GREEN，全项目 ctest 4/4、clean 重建零警告。**Part A DONE，T004 整体 IN PROGRESS（Part B 未开始）**；LKGC 推进至本次代码提交（哈希由 docs-only 回填提交写入） |
| 2026-09-06 | 回填：LKGC = `73825c6`（T004 Part A 代码提交；由暂停期间产生的无名提交 `c605550` amend 而来，内容不变）；本次 HEAD 为 docs-only 回填提交，二者已区分 |
| 2026-09-06 | T004 Part B Learning / Test Design（docs-only）：0x03 语义模型×3、错误模型五值、矩阵 F03-B01~B12（V1.1b3 §6.3 官方金样已数值复核）、Deferred-to-T007、40001 边界；下一步 = Part B Implementation。T004 保持 IN PROGRESS |
| 2026-09-06 | T004 Part B 完成：`Function03` 落地 `modbuslens_core`（三个 decoder + big-endian helper），B01~B12 RED（linker error ×15）→GREEN，全项目 ctest 5/5、零警告。byteCount=0 口径修正（单帧即非法，Part B 直接拒绝，不再推迟 T007）。**T004 整体 DONE，M2 Protocol Core 关闭**；LKGC 推进至本次代码提交（哈希由 docs-only 回填提交写入） |
| 2026-09-06 | 回填：LKGC = `e8b62f6`（T004 Part B 代码提交）；本次 HEAD 为 docs-only 回填提交，二者已区分 |
| 2026-09-06 | T005 启动：Learning / Test Design（docs-only）——范围收缩（IFrameSource/VirtualMaster/虚拟时钟/seed 推迟；Timeout/CRC fault 归 T006）、SimulatedSlave 模型与流程定案、SIM-T01~T07 + SIM-I01 矩阵落库；T005 标记 IN PROGRESS，**未标完成** |
| 2026-09-06 | T005 完成：`SimulatedSlave` 落地 `modbuslens_core`，SIM-T01~T07 + SIM-I01 RED（linker error ×29）→GREEN；过程中发现并修复 ISSUE-001（variant 测试辅助函数悬垂指针，含 T004 测试脚手架同批修复，语义零变化）。全项目 ctest 7/7、零警告。**T005 DONE**；LKGC 推进至本次代码提交（哈希由 docs-only 回填提交写入） |
| 2026-09-06 | 回填：LKGC = `3a896df`（T005 代码提交）；本次 HEAD 为 docs-only 回填提交，二者已区分 |
| 2026-09-06 | T006 启动：Learning / Test Design（docs-only）——四模式定案（random/seed/real delay/丢包概率移出）、Timeout=Session 判断（T006 只交付 DropResponse）、CRC fault 只作用 wire、ArtificialDelay=元数据；矩阵 FAULT-T01~T05 + I01/I02 落库；T006 标记 IN PROGRESS，**未标完成** |
| 2026-09-06 | T006 完成：`SimulationFault` 落地 `modbuslens_core`（四模式单一 switch），FAULT-T01~T05 + I01/I02 RED（linker error ×6，另修正一处测试类名不一致）→GREEN；SimulatedSlave 零修改；全项目 ctest 9/9、零警告。**T006 DONE，M3 关闭**；LKGC 推进至本次代码提交（哈希由 docs-only 回填提交写入） |
| 2026-09-06 | T007 启动：Part A Learning / Test Design（docs-only）——Transaction 定义、六状态、观察/结果模型、跨帧校验规则、矩阵 TX-A01~A12 + I01~I03 落库；T007 标记 IN PROGRESS |
| 2026-09-06 | T007 Part A 完成：`analyzeFunction03Transaction` 落地（六状态、跨帧校验、双不变量），TX-A01~A12 + I01~I03 RED（linker error ×10）→GREEN；ctest 11/11、零警告。**Part A DONE，T007 IN PROGRESS**；LKGC = `14982f6`（回填提交写入） |
| 2026-09-06 | T007 Part B Learning / Test Design（docs-only）：Statistics Snapshot 模型/API/四不变量/矩阵 STAT-B01~B08 + I01 落库 |
| 2026-09-06 | T007 Part B 完成：`TransactionStatistics` 落地（穷举 switch、completed 按分类之和构造、optional rate/latency），STAT-B01~B08 + I01 RED（linker error ×7）→GREEN；ctest 13/13、零警告。**Part B DONE，T007 整体 DONE，M4 关闭**；LKGC = `0f3109a` |
| 2026-09-06 | T008 启动：Part A Learning / Test Design（docs-only）——Part A/B 拆分、依赖方向定案、QML 模块/迁移计划、Controller/Model 设计、矩阵 UI-A01~A06 + I01（I02 记录不做）、Manual UI Smoke 计划落库；T008 标记 IN PROGRESS（M4 按定义含 T008，保持进行中） |
| 2026-09-06 | T008 Part A 完成：main.cpp 迁移 QGuiApplication（QMainWindow bootstrap 与 Widgets 依赖移除）；AnalysisController + TransactionListModel 桥接落地；UI-A01~A06 + 真实 exe 的 QML load smoke 全绿；a11y 初验 12/12。**Part A 实现完成**；LKGC = `76030a2` |
| 2026-09-06 | **[ISSUE-002] Explorer 启动失败诊断**（runtime collision：Git/8.1 旧 libstdc++ 抢占，8.1 版实测缺 pmr 符号）；Manual Visual UI Smoke = FAIL/BLOCKED → WAITING FOR USER；临时 PATH 验证启动成功 |
| 2026-09-06 | **用户人工验收 = PASS（12/12）**，Manual Visual UI Smoke = PASS；Standalone Explorer Launch 保持 FAIL / ISSUE-002 OPEN（修复待立项）。**T008 Part A 正式归档 DONE**（验收 docs-only 提交，LKGC 维持 `76030a2` 不变） |
| 2026-09-06 | 回填：LKGC = `2c8d850`（T006 代码提交）；本次 HEAD 为 docs-only 回填提交，二者已区分 |
| 2026-09-06 | T007 启动：Part A Learning / Test Design（docs-only）——Transaction 定义、六状态、观察/结果模型、跨帧校验规则（地址/功能/数量）、矩阵 TX-A01~A12 + I01~I03 落库；统计快照拆入 Part B；T007 标记 IN PROGRESS，**未标完成** |
| 2026-09-06 | T007 Part A 完成：`analyzeFunction03Transaction` 落地 `src/core/analysis/`（六状态、跨帧校验、双不变量），TX-A01~A12 + I01~I03 RED（linker error ×10）→GREEN；全项目 ctest 11/11、零警告。**Part A DONE，T007 整体 IN PROGRESS（Part B 未开始）**；LKGC 推进至本次代码提交（哈希由 docs-only 回填提交写入） |
| 2026-09-06 | 回填：LKGC = `14982f6`（T007 Part A 代码提交）；本次 HEAD 为 docs-only 回填提交，二者已区分 |
| 2026-09-06 | T007 Part B 启动：Learning / Test Design（docs-only）——Statistics Snapshot 模型/API/四不变量/矩阵 STAT-B01~B08 + I01/浮点规则/mutable accumulator 取舍落库；T007 保持 IN PROGRESS，**未标完成** |
| 2026-09-06 | T007 Part B 完成：`TransactionStatistics` 落地 `modbuslens_core`（穷举 switch 聚合、completed 按分类之和构造、optional rate/latency），STAT-B01~B08 + I01 RED（linker error ×7）→GREEN；修复一处 -Wmissing-field-initializers 测试警告；全项目 ctest 13/13、零警告。**Part B DONE，T007 整体 DONE，M4 关闭**；LKGC 推进至本次代码提交（哈希由 docs-only 回填提交写入） |
| 2026-09-06 | 回填：LKGC = `0f3109a`（T007 Part B 代码提交）；本次 HEAD 为 docs-only 回填提交，二者已区分 |
| 2026-09-06 | T008 启动：Part A Learning / Test Design（docs-only）——Part A/B 拆分、依赖方向定案、QML 模块/迁移计划、Controller/Model 设计、矩阵 UI-A01~A06 + I01（I02 记录不做）、Manual UI Smoke 计划落库；T008 标记 IN PROGRESS，**未标完成**（M4 按 BACKLOG 定义含 T008，保持进行中） |
| 2026-09-06 | T008 Part A 完成：main.cpp 迁移 QGuiApplication（QMainWindow bootstrap 与 Widgets 依赖移除）；AnalysisController + TransactionListModel 桥接落地；UI-A01~A06 + 真实 exe 的 QML load smoke 全绿；a11y 初验 12/12。**Part A 实现完成，Manual Visual Smoke 进入用户确认流程，T008 整体 IN PROGRESS** |
| 2026-09-06 | **[ISSUE-002] Manual Visual Smoke 人工验收完成 = PASS（12/12，用户确认）**；Standalone Explorer Launch 保持 FAIL / ISSUE-002 OPEN（修复待立项）。**T008 Part A 正式归档 DONE**（验收 docs-only 提交，LKGC 维持 76030a2 不变） |
| 2026-09-06 | 回填：LKGC = `76030a2`（T008 Part A 代码提交）；本次 HEAD 为 docs-only 回填提交，二者已区分 |
| 2026-09-06 | T008.1 部署修复验证：`scripts/deploy_windows.bat` 生成 build/deploy（三件套 provenance SHA256=编译器 bin VERIFIED）；minimal-PATH smoke（--qml-smoke-test exit=0）+ 普通运行存活 PASS；脚本可重复生成验证 PASS。**等待用户从 Explorer 双击确认**后 ISSUE-002 置 RESOLVED |
| 2026-09-06 | T008.1 部署修复已部署并验证：build/deploy 生成完毕（三件套 provenance VERIFIED + minimal-PATH smoke + 普通运行存活 全过）；**等待用户从 Explorer 双击 build/deploy/ModbusLens.exe 确认**后 ISSUE-002 置 RESOLVED |
| 2026-09-06 | **用户 Explorer 双击 build/deploy/ModbusLens.exe = PASS**：不再出现入口点错误，窗口正常打开。**ISSUE-002 置 RESOLVED ✅**；T008.1 DONE；LKGC 维持 `28f38b0` 不变 |
| 2026-09-06 | T008 Part B 启动：Learning / Test Design（docs-only）——四条确定性 Demo 事务、Controller runDemoBatch/clearDemo API、矩阵 UI-B01~B06、Core Integration Guard、14 题问答、22 步实施计划落库；T008 整体保持 IN PROGRESS |
| 2026-09-06 | T008 Part B 完成：`runDemoBatch`/`clearDemo` 落地 AnalysisController（四条事务真实调用 T005/T006/T007 链路，快照与行数据同源）；UI-B01~B06 RED→GREEN；ctest 14/14、clean 重建零警告、deploy minimal-PATH smoke PASS。LKGC = `4ede9b3`（Part B 代码提交，由 docs-only 回填写入） |
| 2026-09-06 | **用户 Manual Demo Smoke 第一轮**：核心数据/统计全 PASS；报告 3 个 QML Presentation 问题（Clear 按钮文字不可见 / FC 显示 10 进制 / Exception Code 显示 10 进制）→ 提交 `4075223` 修正（Clear palette.buttonText、0xNN 十六进制补零）并全量复验（ctest 14/14 / QML smoke / clean 零警告 / deploy 回归全过） |
| 2026-09-06 | **用户 Manual Demo Smoke 第二轮 = PASS（12/12 人工确认）**：4 条事务正确、重复 Run 不追加、Clear 恢复全零、Clear 文字/0x03/Code 0x02 均正常。**T008 Part B 归档 DONE；T008 整体 DONE；M4 事务分析与界面临界关闭（按 BACKLOG 既有定义含 T007+T008）**。LKGC = `4075223`；HEAD = docs-only 归档提交 `a1db0d8` |
| 2026-09-07 | **T009 启动：Part A Learning / Test Design（docs-only）**——Replay 角色定案、`.mlog` v1 格式定案、数据/错误模型与 API 定案、wire 金样独立复核、矩阵 REPLAY-A01~A08 + I01~I04/I05 落库；T009 标记 IN PROGRESS，**Replay 未实现、Part B 未开始** |
| 2026-09-07 | **T009 Part A 完成**：`src/core/replay/` 落地（ReplayLog parser + ReplayAnalysis 批量回放分析，Pure C++20 Zero Qt）；request 可信链三错误码（新增 InvalidRequestData，REPLAY-I03B 金样 `01 03 00 00 00 00 45 CA`）；REPLAY-A01~A08 + I01~I05 共 24 测试函数 RED（101 处 undefined reference）→GREEN；修复真 bug（from_chars 结果未写回 out 参数）与 2 处测试警告；ctest 16/16、clean 96 targets 零警告、Core Zero Qt、ISSUE-001 无回归、QML smoke exit=0。**Part A = DONE，T009 整体 IN PROGRESS**；LKGC = `e4920da`（回填提交写入） |
| 2026-09-07 | **T009 Part B 启动：Learning / Test Design（docs-only）**——单 Dashboard 复用、Controller 新 API（loadReplayFile/clearResults 重命名/error 与 mode-source state）、失败策略（旧结果保持+显示 error）、FileDialog 本机实证（QtQuick.Dialogs + Qt6::QuickDialogs2）、canonical sample `samples/demo_v1.mlog` 定案、矩阵 UI-R01~R08、16 题问答、23 步计划落库；T009 保持 IN PROGRESS，**Part B 未实现** |
| 2026-09-07 | **T009 Part B Implementation 完成（自动化全 GREEN）**：loadReplayFile 原子发布 + 失败保全旧 batch/source；clearResults 重命名；canonical sample 迁移 samples/（git mv + SHA256 验证）；Main.qml FileDialog/Header/错误 label；UI-R01~R08 新增全过（ui_bridge 22/22）；ctest 16/16、clean 96 targets 零警告、deploy+minimal-PATH smoke PASS。LKGC candidate = `d473d36`。**Manual Replay Smoke = WAITING FOR USER**（PB-24 定则，Agent 不自报 PASS） |
| 2026-09-07 | **用户 Manual Replay Smoke = PASS（A~E 全项人工验收）**：Replay 加载/统计/四行正确、Clear 保留来源、Demo↔Replay 互切不追加。**T009 Part B DONE → T009 整体 DONE**。LKGC = `d473d36`（核验存在后写入）；HEAD = docs-only 确认提交。M5 按 BACKLOG 既有定义保持进行中（T010 未开始） |
| 2026-09-07 | **T010 启动：Part A Learning / Test Design（docs-only）**——Serial 语义/framing/timeout/矩阵落库；**发现 ISSUE-003（QtSerialPort 未安装，OPEN）**；T010 标记 IN PROGRESS，M5 保持进行中（T009 ✅ / T010 ⬜） |
| 2026-09-07 | **T010 Part A Implementation 启动即停**：安装后重实证 FAIL——用户称 Qt Serial Port 已补装，但 6.11.1 kit 的 include/cmake package/DLL 均不存在，InstallationLog.txt 无今日记录（mtime Sep 1）。按规则（一步失败即停）停止 Implementation；ISSUE-003 追加证据与用户自查清单，保持 OPEN |
| 2026-09-07 | **T010 Part A 完成**：正确 kit 五步实证 PASS（ISSUE-003 RESOLVED：根因=初次补装落错 MSVC kit）→ 全 Implementation。`SerialTransactionSession` + `encodeReadHoldingRegistersRequest` + `SerialPortAdapter` 落地；SERIAL-A01~A16+I01+I02 全绿；RED=15 处 undefined reference；修复 PE-4（QSerialPort errorOccurred 反馈风暴 → QueuedConnection+suppress）与 PE-5（A07 真实行为 CrcError/ProtocolError 双场景）。ctest 18/18、clean 106 targets 零警告、Core Zero Qt、deploy minimal-PATH PASS。**Part A = DONE，T010 整体 IN PROGRESS**；LKGC = `b31233b` |
| 2026-09-08 | **T010 Part B 启动：Learning / Test Design（docs-only）**——Controller Serial API/状态、Connect/Disconnect/Clear 语义、Read Once replace=1、hardware-free seam、UI-S01~S09+SERIAL-I02/I03 矩阵、discovery 安全规则、Qt6SerialPort provenance 计划、Manual/Hardware Smoke 政策落库；T010 保持 IN PROGRESS，Serial UI 未实现 |
| 2026-09-08 | **T010 Part B Implementation 完成（自动化全 GREEN）**：Adapter API 拆分、Controller serial 全套、UI-S01~S10、QML Serial Controls；ctest 18/18、clean 108 targets 零警告、Qt6SerialPort provenance 验证、deploy+minimal-PATH PASS。LKGC candidate = `33ed197`；Manual Serial UI Smoke = WAITING FOR USER |
| 2026-09-08 | **用户 Manual Serial UI Smoke = PASS（A~F）**：控件显示/Refresh 无 crash 不自动 open/枚举正常/Demo 回归/Replay 回归/按钮 enable 合理。**Hardware Smoke = NOT RUN（hardware unavailable）**。**T010 Part B DONE → T010 整体 DONE → M5 CLOSED（按 BACKLOG 定义 T009+T010）**。LKGC = `33ed197`；HEAD = docs-only 确认提交。T011 未开始 |
| 2026-09-08 | **T011 启动：Part A Learning / Test Design（docs-only）**——AI 与 Core 责任边界、规则基线全套、DIAG/UI-D 矩阵落库；T011 标记 IN PROGRESS，M6 转进行中，Diagnosis 未实现 |
| 2026-09-08 | **T011 Part A Implementation 完成（自动化全 GREEN）**：deterministic diagnosis baseline 落地（core Zero Qt + Controller 同源 diagnosis batch + QML 面板）；DIAG-A01~A10 + UI-D01~D08 全绿；ctest 19/19、clean 114 targets 零警告；产品代码零 LLM/HTTP/key（grep 佐证）；LKGC candidate = `06ef801`；Manual Baseline Smoke = WAITING FOR USER |
| 2026-09-08 | **用户 Manual Baseline Smoke = PASS（A~E）**：golden 三 findings 正确、无 root-cause 宣称、Clear 职责分工正确、Replay/Simulator finding 一致、Serial 无回归。**T011 Part A = DONE；T011 整体 IN PROGRESS（Part B 未开始）；M6 保持 IN PROGRESS**。LKGC = `06ef801`；HEAD = docs-only 确认提交 |
| 2026-09-08 | **T011 Part B 启动：Learning / Test Design（docs-only）**——ModelScope 定案/凭据边界/prompt 与客户端设计/矩阵 AI-B01~B12+UI-AI01~AI10 落库；QtNetwork kit probe 已实证（sslBuild=yes）；T011 保持 IN PROGRESS，网络代码未实现 |
| 2026-09-08 | **T011 Part B Implementation 完成（自动化全 GREEN）**：ModelScope client/prompt builder/Controller 双 stale guard/QML AI 面板落地；AI-B01~B13 + UI-AI01~AI11 全绿（ctest 20/20、clean 126 零警告）；ISSUE-004（Diagnosis 纵向 overflow→SplitView workspace 修复）建档 RESOLVED 全程（用户三次失败证据 + Manual Layout Verification PASS）；code candidate = `85699ff` |
| 2026-09-08 | **用户 Manual AI UI Smoke = PASS（A~I 九项）**：AI 面板/未配置不发请求/Baseline/Clear 分工/Replay 同 semantic/Serial 无回归/SplitView 独立滚动无回归。**Live ModelScope Smoke = WAITING FOR USER**；T011 Part B / T011 / M6 保持 IN PROGRESS；verified LKGC 仍 `06ef801`（候选 `85699ff` 待 Live PASS 后推进） |
| 2026-09-08 | **用户 Live ModelScope Smoke = PASS**（真实 ModelScope API-Inference + Qwen/Qwen3.5-27B；attempt#1 insufficient balance 失败历史保留、attempt#2 成功；live 输出四段结构并正确引用 deterministic facts；non-blocking over-inference 质量观察记录；AI is interpreter not detector 真实证明）。**T011 Part B DONE → T011 整体 DONE**。verified LKGC = `85699ff`（Git 实际核验）；HEAD = docs-only 完成提交。M6 按既有定义保持进行中（T012 未完成） |
| 2026-09-08 | **[ISSUE-005] Ask AI 显示"操作被取消"**：Live 中 OperationCanceledError（本地 abort）经 errorString 泄漏至 UI；双 timeout owner 竞态定性 |
| 2026-09-09 | **UI Localization Pass 实现完成（自动化全 GREEN）**：QML 42 处静态文案 + TransactionListModel 六状态 + AnalysisController 模式/来源标签（含 .h 默认初始值）/基线格式/12 条建议动作/全错误文案 + SerialPortAdapter/ModelScopeDiagnosisClient 消息 + DiagnosisPromptBuilder 输出语言约束（简体中文、术语保留英文、no Markdown/plain text）；replay 短语 const char*+QLatin1String 乱码隐患结构性修正（QStringLiteral）；presentation 测试断言同步更新（Core 语义与 Core 测试零改动）；clean 126 targets 零警告、ctest 20/20、qml smoke、deploy+minimal-PATH PASS；code candidate = `9e79558`；Manual Localization Smoke = WAITING FOR USER |
| 2026-09-09 | **用户 Manual Localization Smoke = PASS（9 项人工确认）**：三模式文案中文化/专业实体（CRC/RS485/FC03/8N1/COM/ModelScope/Qwen/0x02/0x03/ms）正确保留/统计与诊断面板文案自然/Baseline 中文正确/Replay 文件名不翻译/真实 ModelScope AI 解释以简体中文为主且 PlainText 渲染/布局无截断无回归。**Localization Pass 归档完成**；verified LKGC = `9e79558`；HEAD = docs-only 确认提交。**T011 仍 DONE；ISSUE-004/005 仍 RESOLVED；M6 保持 IN PROGRESS；T012 NOT STARTED** |
| 2026-09-09 | **AI Explanation Polish Review（docs-only，非 T012）**：核验过度归因措辞（“链路稳定性较差/协议状态混乱/往往源于物理层/暗示间歇性中断”）来源——A/B/F 经核验无措辞责任，C/D 为可控缺口（0x02 语义与小样本事实未下发、system 缺正例语义），E 模型先验为直接来源；**ISSUE-006 建档 OPEN**（最小方案：DiagnosisPromptBuilder system Attribution discipline 段 + user small_sample 标注；回归测试 AI-B14/B15 设计）；本轮不修改 production code、不调用真实 ModelScope、不消耗 quota、不推进 LKGC、T012 保持 NOT STARTED |
| 2026-09-09 | **ISSUE-006 Implementation 完成（自动化 GREEN）**：生产改动仅 DiagnosisPromptBuilder——Evidence Scope Guard（无条件 evidence_scope=current_observed_batch；**取消原 small_sample<=10 阈值设计**：count 不能论证长期代表性）+ system 三条状态正例语义（CRC/Timeout/0x02=Illegal Data Address）+ 0x01~0x04 语义表 + Mixed Error Independence + Facts/Explanations/Checks 纪律（原 authority 规则一字未动）；新增 AI-B14~B17；clean 126 零警告、ctest 20/20、deploy+minimal-PATH PASS；code/test candidate = `01841b1`（**不推进 LKGC**）；用户 Manual UI Regression Smoke = PASS（10 项） |
| 2026-09-09 | **经授权 Live ModelScope Smoke = PASS（1 次最小配额，真实 production path，Qwen/Qwen3.5-27B）**：五项验收全过——A 无长期/持续性外推（明确限 current_observed_batch）；B CRC 仅“可能与 serial settings/wiring/grounding/EMI 有关”；C Timeout 无 offline/broken；D 0x02 正确解释为 Illegal Data Address 并优先核对 register map/文档；E 明确“独立观察、不意味着 shared root cause”。**ISSUE-006 RESOLVED**；verified LKGC 推进至 `01841b1`；HEAD = docs-only 归档。T011 仍 DONE；T012/T013 NOT STARTED；M6 IN PROGRESS |
| 2026-09-09 | **T012 启动：Agent Tools — Learning / Test Design（docs-only，`03deffd`）**：三工具（get_session_summary/get_recent_anomalies/get_transaction_detail）v1 冻结；transaction_id=1-based batch 序号定案；dispatcher 定案（否决 registry）；ModelScope tool-calling contract 核验（Qwen3 官方支持、Provider 透传未证实 → Implementation 前需授权 Live Probe）；loop FSM + max 3 rounds；run 绑定 activeBatchRevision + 独立 agentRequestGeneration；AGENT-A01~A08 + B01~B14 矩阵（14 项 P0）；Part A/B 拆分推荐；ADR002 新建；**零 production/tests/CMake 改动、零 quota**；T012 IN PROGRESS，Implementation 待批准 |
| 2026-09-09 | **T012 Part A Implementation 完成（自动化全 GREEN）**：`src/ui/agent/` 落地——不可变 AgentToolContext（R3）+ 三工具白名单 dispatcher + typed results + 参数全量校验 + provider-independent JSON DTO（transaction_number/ latest-20 原序/ exception_name 0x01~0x04/ evidence_scope）；RED=10 处 undefined reference；AGENT-A01~A09 全绿；clean 131 targets 零警告、ctest 21/21；T011 零 diff；**code candidate = `9921efd`（LKGC 未推进，待用户审核）**；Part B NOT STARTED |
| 2026-09-09 | **T012 Part A Review P0 fix（`797269a`）**：`get_recent_anomalies` 原以 `status != Success` 把 Pending 误算 anomaly → 改显式 whitelist {Exception/CrcError/Timeout/ProtocolError}；latest-20 绑定 anomaly 序列（尾部 Pending 不占名额）；RED=A02 对旧逻辑 FAIL、GREEN=白名单全过；A02/A03 扩充锁定 + 0x7E exception_name absent；Qt 措辞边界入档；clean 131 零警告、ctest 21/21；**最新 Part A candidate = `797269a`（LKGC 仍 `01841b1`）**；Part B NOT STARTED |
| 2026-09-09 | **用户 T012 Part A Review = PASS（12 项确认）**：Pending semantic fix / whitelist / latest-20 按 anomaly 序列 / 未知异常码不猜 / offline+Qt 边界 / AGENT-A01~A09 / clean / ctest 21/21 / T011 零修改。**T012 Part A = DONE**；`797269a` 经 RED-GREEN+矩阵+clean+ctest+人工 Review 全链验证 → **新 verified LKGC = `797269a`**；Part B NOT STARTED；M6 IN PROGRESS |
| 2026-09-09 | **T012 Part B Gate 0 Probe — Request #1 PASS**：真实 ModelScope（Qwen/Qwen3.5-27B）返回标准 native tool_calls（name=get_session_summary / arguments={} / id 完整 / finish_reason=tool_calls, HTTP 200）——原生路径成立；累计真实请求 2（首轮 Probe 脚本本地解析 bug 消耗 1，如实记录）；**tool round trip（Request #2）待用户追加授权**；临时脚本已删除、零 production code |
| 2026-09-09 | **T012 Part B Gate 0 — 完成 ✅ PROVEN**：追加授权后 Request #2（assistant tool_calls 原样 + role=tool + tool_call_id 完全匹配 + synthetic result）→ HTTP 200、finish_reason=stop、final content 正确引用 observed=4/timeout=1——**Native Tool Calling round trip 全链实证**（累计真实请求 3，红线内；Attempt 历史如实保留）；临时脚本删除、零 production code；Path A（原生 tools）成立，无需 Hermes |
| 2026-09-09 | **T012 Part B Phase 1 Implementation 完成（自动化全 GREEN）**：AgentRuntime（双硬上限 3/3、整批 validate-then-execute、双层 stale guard、supersede/cancel）+ ModelScopeAgentClient（native tool calling，ISSUE-005 契约复刻）+ AgentPromptBuilder（三工具固定 schema）+ Part A validate-only/snapshot builder 增量；RED=47 处 undefined；ctest 22/22、clean 142 零警告；T011 与 Part A 语义零改动；**candidate = `da453a7`（LKGC 未推进，待 review）**；Phase 2 NOT STARTED |
| 2026-09-09 | **T012 Part B Phase 1 Review P0 fix（`2becc41`）**：删除 AgentRunRequest 重复 revision（AgentToolContext 为 batch 身份单一来源；类型层消灭 context=A/request=B 分裂态）；空 final content→provider InvalidResponse（绝不空答案）；tool_calls 优先于 content（message shape 权威）；新增 B19/B20/B21（RED=B20 旧实现失败）；clean 142 零警告、ctest 22/22；**最新 Phase 1 candidate = `2becc41`（LKGC 未推进）** |
| 2026-09-09 | **T012 Part B Phase 1 Final Review fix（`b322cc3`）**：captured-vs-current seam 分离——start() 不再写 currentBatchRevision_（live world 唯一更新点=setCurrentBatchRevision）；新增 start preflight stale guard（stale snapshot 零请求零信号）+ B22（RED=旧实现发出请求）；fixture 显式建立 live world；B01~B22 全绿、clean 142 零警告、ctest 22/22；**最新 Phase 1 candidate = `b322cc3`（LKGC 未推进）** |
| 2026-09-09 | **用户 T012 Part B Phase 1 Final Review = PASS → Phase 1 封版**：`b322cc3` 升级为 **新 verified LKGC**（A01~A10+B01~B22/clean 142/ctest 22/22/三轮 Review 全链）；Identity Model 入档（snapshot/live/run/generation 四身份）；Phase 2 ST-A requirement 与 Controller contract 预告；T011 production 零改动 |
| 2026-09-10 | **T012 Part B Phase 2 Learning / Integration Plan 完成（docs-only）**：Controller 状态图/发布路径 6 处核验；Agent ownership（Controller parents client+runtime）；snapshot 唯一合法链；batch-change invalidation 最小 seam（`AgentRuntime::invalidateForBatchChange`，静默立即失效）；single-flight derived `cloudAiBusy`（UI+backend 双 guard）；Agent 不绑 Baseline；NoData 本地拒绝；answer/error/cancel/generation 语义定案；QML 左 pane 最小 UI + ISSUE-004 防回归；UI-AG01~AG18 矩阵（P0×13/P1×5）；register-address limitation 入 Backlog；零代码改动 |
| 2026-09-10 | **T012 Part B Phase 2 Implementation 完成（自动化全 GREEN，`d781ab0`）**：Controller+QML Agent 集成（同一 config 双 client/全 derived 状态/四级前置/批切换 ordering seam/single-flight 双向/answer-error 语义）+ 新 Runtime invalidate seam；口径修正（5 处发布路径；generation 措辞）；UI-AG01~AG20 全绿（RED=未接线 Controller 编译失败）；clean 0 警告、ctest 23/23、deploy+minimal-PATH PASS；**candidate 未推进（待 Manual UI Review）** |
| 2026-09-10 | **T012 Part B Phase 2 Final Real Live Agent Smoke = FAIL（证据入档）**：唯一授权 run（Qwen/Qwen3.5-27B，真实 endpoint）约 12s 内触发 ToolCallLimitExceeded（UI「工具调用次数已达上限。」）→ 无 final answer；崩溃级零问题、facts 零变化、防护按契约工作；请求数不可直接观测（硬上限保证 ≤3，远低于 4；无第二 run/retry/Ask AI）；待用户决策（prompt 收紧 / 上限调整 / 保持 v1）；H Cancel 未执行（自动证据 UI-AG08+B13 承担）；**d781ab0 未推进 LKGC** |
| 2026-09-10 | **ISSUE-007 建档并修复（`e922c19`）**：Live FAIL → RCA（可证/不可证严格分离）→ MAX_TOTAL_TOOL_CALLS 3→6（rounds 保持 3）+ Agent Tool Efficiency/Budget prompt 纪律 → B05 重写（6 过/7 拒零执行）+ 新 B23（5 calls 多步计划）；RED=新测试在旧上限双 FAIL；clean 147 零警告、ctest 23/23；**新 Phase 2 candidate = `e922c19`（LKGC 未推进，待 Live Re-Smoke）**；ISSUE-007 = FIXED / AWAITING LIVE RE-VALIDATION |
| 2026-09-10 | **ISSUE-007 Live Re-Validation = PASS（经授权唯一 run）**：同一问题原文重跑 → 约 60s 产出 usable final answer，无 ToolCall/ToolRound 超限；facts 零变化、无 crash；0x02/CRC/Timeout/独立性/无编址/无 false action 逐项通过；evaluation tool sequence 与 request count = not externally observable（按政策）；**ISSUE-007 待用户最终 closure**；candidate `e922c19` 未推进 LKGC |
| 2026-09-10 | **用户 T012 Final Review = PASS → Closure**：ISSUE-007 RESOLVED；Phase 2 DONE；T012 DONE；M6 DONE；**verified LKGC 推进至 `e922c19`**（自动回归 + ctest 23/23 + QML smoke + Offline Manual UI Smoke + 同题真实 Live Re-Validation PASS + 用户 Final Review）；T013 记录 polish 事项，NOT STARTED |
| 2026-09-10 | **T012 Post-Closure Stabilization Review（docs-only）**：真实使用回归发现 ISSUE-008（合法 0x02 问题 → InvalidResponse 空 content；RCA=fail-closed 正确、根因形态未知需诊断）与 ISSUE-009（额度不足 → busy 消失无持久提示；RCA=无既证静默路径、疑 provider 超表形态）均建档 OPEN；test design（B20 扩展/B24/UI-AG21·22）+ 文案方案定案；T012 REOPENED / M6 转 IN PROGRESS；LKGC `e922c19` 不回退 |
| 2026-09-10 | **Sanitized Live Diagnostic Evidence（授权）**：ISSUE-008 = NOT REPRODUCED（同题唯一 run 成功，3 rounds，sanitized metadata 归档；原失败 producer 仍 unknown，budget hypothesis 未证实未排除）；ISSUE-009 = BLOCKED / PRECONDITION NOT AVAILABLE（ISSUE-008 成功证明当前额度可用，零额外请求）；临时插桩完全恢复（src 零 diff）；ISSUE-008/009 保持 OPEN |
| 2026-09-10 | **Phase 3 Offline Hardening（`95ad9e7` candidate）**：8 类 provider 错误持久中文文案 + 顶部「模型配置：」+ B20 whitespace/B24 reasoning-only/B25 非 string/ag21 429 全链路/ag22 malformed-200 可见；RED=ag05/21/22 旧文案失败，B20/24/25 coverage-only；clean 0 警告、ctest 23/23、deploy+minimal-PATH；**ISSUE-008/009 保持 OPEN（不声称根因修复）**；LKGC 未推进 |
| 2026-09-10 | **T012 Stabilization Final Review Closure = PASS**：B20 全族覆盖验证（新 LF/CRLF case 由 test-only `3572cf7` 补齐，coverage-only）；enum 语义审计（A provider-path / B local-state 分离）；**verified LKGC 推进 `e922c19` → `3572cf7`**；ISSUE-008/009 = OPEN + MONITORING/NON-BLOCKING（历史 exact RCA 未证明、不阻塞）；T012 DONE / M6 DONE；T013 NOT STARTED |
| 2026-09-11 | **T013 Phase A Final Polish Audit 完成（docs-only）**：UI 视觉审计 V1~V10（P0×3/P1×4/P2×1/HUMAN）×；术语审计（AI 输出英文卡槽中文化建议）；面试 10 步 Demo 主线设计；golden 数据一致性（无分歧，README/DEMO_GUIDE 过时）；README 15 项 gap 全列；部署已达标仅需重验链；截图 4~5 张计划；面试 19 故事 + 真实 limitations；**建议实现集 8 项** + no-go 清单；T013 IN PROGRESS，Implementation 待批准 |
| 2026-09-11 | **T013 Phase B Implementation（`aea1e64` candidate）**：V1~V7 样式级 + 术语 polish（authority 不动）+ README/DEMO_GUIDE 重写（golden 一致）；clean 147 零警告、ctest 23/23、qml smoke、deploy+minimal-PATH PASS；零真实调用；**T013 AWAITING MANUAL VISUAL REVIEW（未 Final Close）**；LKGC 未推进 |
| 2026-09-11 | **T013 Manual Visual Review = FAIL（用户）**：A/C/D/E/F 不通过（其中 E=交易表可用性；busy/error=NOT FULLY EXERCISED）；G/H/I/J 通过；**方向定案=fixed Light + TabBar 三页 + 稳定表格**；Phase B candidate `aea1e64` superseded（历史保留）；进入 Phase C Remediation |
| 2026-09-11 | **T013 Phase C Remediation 完成（`5a2f60c` candidate）**：Light 主题 + TabBar 三页 + 稳定表格/表头 + 轻 scrollbar；clean 147 零警告、ctest 23/23、qml smoke、deploy+minimal-PATH PASS；零真实调用；**T013 AWAITING MANUAL VISUAL RE-REVIEW**；Phase B `aea1e64` superseded；LKGC 未推进 |
| 2026-09-11 | **T013 Manual Visual Re-Review = FAIL（用户，R1/R2/R3）**：串口下拉空白（OS probe=0 活动端口→空态缺陷，非枚举 bug）；选中 Tab 无边框；表格五列对齐失败；5a2f60c 定格 MANUAL RE-REVIEW FAILED→superseded；进入 Phase D |
| 2026-09-11 | **T013 Phase D Remediation 完成（`a25d63c` candidate）**：串口空态（OS probe=0 端口取证→仅 QML overlay）/Tab 选中恒可见边框/表格单列几何 owner 对齐；clean 147 零警告、ctest 23/23、qml smoke、deploy+minimal-PATH PASS；零真实调用；**待用户 Manual Visual Re-Review（仅 A~D）**；LKGC 未推进 |
| 2026-09-11 | **T013 Phase D Re-Review = FAIL/PARTIAL（用户）**：A 串口空态视觉 UNRESOLVED（生产 Qt API 取证 QSerialPortInfo count=0——机器 serial availability 变化，非链路 bug）；D 表格水平空间浪费；B/C PASS。另发现原生 style 忽略自定义（Fusion style 修正）。a25d63c 定格 superseded；进入 Phase E |
| 2026-09-11 | **T013 Phase E 完成（`99f17d6` candidate）**：Qt 同 runtime 取证（QSerialPortInfo count=0）+ Fusion style（定制警告清零）+ 串口可读空态 + 表格比例列宽；clean 147 零警告、ctest 23/23、qml smoke、deploy+minimal-PATH PASS；零真实调用；**待用户 Manual Visual Re-Review（A/B/C）**；LKGC 未推进 |
| 2026-09-11 | **用户 Manual Visual Re-Review = PASS（五项确认）→ T013 Final Closure**：Phase E 视觉通过；**verified LKGC 推进 `3572cf7` → `99f17d6`**（clean/ctest 23/23/qml/deploy/minimal-PATH/runtime 警告检查/人工视觉全链）；Phase A~D 历史与 FAIL 记录保留；M6 保持 DONE；ISSUE-008/009 MONITORING 不变；零真实请求 |
| 2026-09-13 | **M8 Phase B Knowledge Ownership 系列（B1~B7，docs-only）**：architecture/Modbus core/Transaction Statistics/execution modes/Qt QML adapter/AI diagnosis/Agent tool-calling 七个知识主权文档化完成（Part 7 = `4039ade`）；Phase B Closure 入档；LKGC `99f17d6` 不变 |
| 2026-09-13 | **M8.1 — Diagnostic Coverage Audit 建立并通过用户 Final Review**（docs-only `dab9b5f`；line-count/CRC-count bookkeeping 修正 `5f21911`）：`docs/09_DIAGNOSTIC_COVERAGE_AUDIT.md` 定案——术语模型、14 场景实测矩阵、独立 CRC 审计、覆盖族、Claim Risk、T014/T015 候选；LKGC `99f17d6` 不变 |
| 2026-09-13 | **T014 启动：Phase A Learning + Test Design（docs-only，本提交）**——ProtocolError 七分支重构、信息损失矩阵、最小数据模型定案（TransactionIssue A′方案）、六不变量、Statistics 不变契约、request-side/T015 边界、UI/prompt/agent 传播设计、三级测试矩阵落库；**T014 标记 IN PROGRESS，未实现**；Phase B = WAITING FOR USER APPROVAL；LKGC `99f17d6` 不变 |
| 2026-09-13 | **用户 T014 Manual UI Review = PASS → T014 Final Acceptance**：demo regression 正常（四行/Dashboard/行高/布局）；临时非仓库 `t014_protocol_error.mlog` 下 ProtocolError 行第二行 detail `响应地址不匹配（请求 0x01 / 响应 0x02）` 可读、行高扩展正确、无重叠裁剪、列对齐、仅确定性措辞。**T014 = DONE（Phase A/B DONE）；verified LKGC 推进 `99f17d6` → `cc8393a`**；`213bba5`（docs-only archive）不作 LKGC；措辞订正（生产文件 9 个；production invariant 单向、下游防御 omit）入档；T015 NOT STARTED |
| 2026-09-13 | **T015 启动：Phase A Learning + Test Design（docs-only，本提交）**——current Replay contract 重建；三种坏输入三分；active/passive 双契约；Gate A~F 设计（A=Core passive analyzer、B=outcome 级 requestIssue、C=Broadcast 用户裁决、D=FC06 先行、E=Scope A、F=per-record）；FC06/0x10 取证门槛（External Protocol Reference Required）；PASSIVE-P01~P15 矩阵落库；**ADR-003 Draft 建立（Broadcast 语义，AWAITING USER DECISION）**；**T015 标记 In Progress，未实现**；verified LKGC `cc8393a` 不变 |
| 2026-09-13 | **T015 Phase A Review = PASS（Gate A~F 全批；Gate C 批准第七状态 `ExpectedNoResponse` 与最终统计公式）→ Phase B 批准**；docs 提交 `c5cfbf7`（口径订正 + 03 §4.5 官方协议回填 + ADR-003 Accepted-candidate） |
| 2026-09-13 | **T015 Phase B IMPLEMENTED（B/C/D 三提交）**：B `477ed44` 模型表面 + RED（passive **8 passed/13 failed**，13 条断言级失败）；C `2ba719f` passive core（Function06 / PassiveTransactionAnalysis / per-record replay / 统计公式 / Baseline）→ ctest 24/24；D `6944fd5` 下游传播（dashboard 卡 + replay notice + prompt + agent tools + 测试）。clean **152 targets 零警告**、ctest 24/24、qml smoke、deploy+minimal-PATH 全过；Active Serial 写权限零新增（grep 零命中）；**Function 0x10 = NOT STARTED**；**LKGC candidate = `6944fd5`（待用户 Review + Manual UI Smoke）**；verified LKGC 仍 `cc8393a` |































| 2026-09-08 | **ISSUE-005 修复并 RESOLVED**：AiAbortReason 归属 + 单一 QTimer owner（90s）+ OperationCanceledError 按 reason 分类 + UI 文案与 errorString 解耦；自动化全绿后**用户真实 27B Live Regression Smoke = PASS**（不再出现"操作被取消"）；verified LKGC 推进至 `bb3f3b4`（新 code fix commit）；docs-only 归档不再次推进 |
