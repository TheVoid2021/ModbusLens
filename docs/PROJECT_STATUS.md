# PROJECT_STATUS — 项目状态单一事实源

> 规则：本文件在每个任务**完成时**更新（AGENTS.md 工作纪律 5）。任何协作者以此文件为真相，其次才是聊天上下文。
| Last Known Good Commit | **`797269a`**（T012 Part A 完成：read-only agent tool layer + Pending anomaly whitelist 修复；RED→GREEN、AGENT-A01~A09、clean 131 零警告、ctest 21/21、**用户人工/架构 Review = PASS（12 项确认）**。历史值：`01841b1`（ISSUE-006）、`9e79558`（UI Localization）、`bb3f3b4`（ISSUE-005）、`85699ff`（T011B）、`06ef801`（T011A）） |

## 状态面板

| 项 | 值 |
| --- | --- |
| 当前版本 | **0.1.0**（2026-09-05，T001 建立；T001.1 未改代码，版本不变） |
| 当前 Milestone（Current Milestone） | M2 ✅ / M3 ✅ / M4 ✅ / M5 ✅；**M6 进行中（T011 ✅ DONE / T012 未开始；按 BACKLOG 既有定义 M6=T011+T012，不得提前关闭）** |
| Last Known Good Commit | **`797269a`**（T012 Part A 完成：read-only agent tool layer + Pending anomaly whitelist 修复；RED→GREEN、AGENT-A01~A09、clean 131 零警告、ctest 21/21、**用户人工/架构 Review = PASS（12 项确认）**。历史值：`01841b1`（ISSUE-006）、`9e79558`（UI Localization）、`bb3f3b4`（ISSUE-005）、`85699ff`（T011B）、`06ef801`（T011A）） |
| Build 状态 | ✅ **通过** — Debug/MinGW 13.1.0/Qt 6.11.1（含 QtSerialPort 组件）/CMake 3.30.5，零警告（clean 全量重建 142 targets） |
| Test 状态 | ✅ **22/22 通过**（ctest 22 个测试目标全绿（T012 Part A agent_tools：AGENT-A01~A09+A10；Part B Phase 1 agent_runtime：AGENT-B01~B18）
| 已完成任务 | T001 · T001.1 · T002 · T003 · T004 · T005 · T006 · T007 · T008 · T009 · T010 · **T011** |
| 当前任务（Current Task） | **T012 Part B Phase 1 IMPLEMENTED / AWAITING REVIEW（candidate `da453a7`；Gate 0 ✅ PROVEN）** |
| 最近完成任务（Last Completed Task） | **T011 AI Diagnosis**（DONE；+ ISSUE-006 收尾：evidence-scope guard，Live 五项验收全 PASS，LKGC `01841b1`）；UI Localization Pass（LKGC `9e79558`） |
| 当前阶段（Current Phase） | T012 Part B Phase 1（Native Agent Runtime）落地：RED→GREEN、AGENT-B01~B18、ctest 22/22 → 待用户 review；Phase 2（Controller+QML）NOT STARTED |
| 下一步动作（Next Action） | **T012 Part B Phase 1 人工 review（`da453a7`）；通过后 Phase 2（Controller Agent Integration + QML Agent UI，需再批准）** |
| 下一 Part（Next Part） | **T012 Part B — Agent Runtime + UI（Live Tool-Calling Probe 先行，需用户授权）** |
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

- **None**。T011 AI Diagnosis 已整体 DONE（Part A 规则基线 + Part B ModelScope LLM 集成；Manual AI UI Smoke 与 Live ModelScope Smoke 双 PASS；ISSUE-004 布局全轨迹 RESOLVED）。下一步：启动 T012 Agent Tools（见 §3 与 BACKLOG）。


## 3. 下一任务

- **T012 — Agent Tools**（详见 [BACKLOG](BACKLOG.md)）：只读 Agent 工具集——读日志摘要/统计/报告；架构强制无写 API（类型层面不存在）。依赖 T007/T008/T011 输出；在 T011 完成后启动。

## 4. Known Issues（当前已知问题）

| # | 问题 | 影响 | 状态/应对 |
| --- | --- | --- | --- |
| K1 | 本机 Qt 在 AutoMoc 阶段出现 qtlicd 证书服务不可用的构建期警告 | 仅为构建日志噪音，产物正常 | **临时环境处理**：仅在本机（gitignored 的 CMakeUserPresets.json）注入 `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`。项目代码与提交文件**不依赖**该变量（T001.1 已澄清措辞，见 [ENVIRONMENT](ENVIRONMENT.md) §6） |
| K2 | 系统 PATH 中存在 Anaconda 的 Qt5 qmake 与 MinGW g++ 8.1.0（过旧） | 若直接裸用会产生 Qt/编译器 ABI 不匹配 | 规避：统一通过 `*-local` preset 注入 Qt 自带工具链；见 [ENVIRONMENT](ENVIRONMENT.md) |
| K3 | `Could NOT find WrapVulkanHeaders`（configure 提示） | 无（Qt Widgets 不依赖；仅影响未来 QtQuick/RHI 功能） | 记录观察，不处理 |
| K4 | ~~**[ISSUE-002] Explorer 启动 modbuslens.exe 失败**~~ **[RESOLVED ✅]**（无法定位输入点 `_ZNSt3pmr20get_default_resourceEv` 于 Qt6Gui.dll） | 仅影响"不经终端直接双击启动"场景；终端前置正确 PATH 后启动正常；**正确 runtime 下用户已人工确认 UI 12/12 正常** | **已解决（T008.1）**：`scripts/deploy_windows.bat` 生成 build/deploy 独立目录（runtime provenance SHA256=编译器 bin VERIFIED + minimal-PATH smoke PASS）；**用户 Explorer 双击确认 PASS**。ISSUE-002 置 RESOLVED |
| K5 | ~~**[ISSUE-003] 本机 Qt 6.11.1 未安装 QtSerialPort 组件**~~ **[RESOLVED ✅]**（首次补装落错 MSVC kit `D:\QTDesign`，随后装到正确 MinGW kit；五步实证+临时 CMake probe 全过） | 曾阻塞 T010 Part A 的 Qt adapter/SERIAL-I01 | 已解决；T010 Part A 全绿交付 |
| K6 | [ISSUE-006](issues/ISSUE-006-ai-explanation-overattribution.md) AI 解释过度归因——4 笔小样本（1 CRC + 1 Timeout + 1 Exception 0x02）被渲染为“链路稳定性差/协议混乱/往往源于物理层/间歇中断”等确定语气结论 | 确定性数据零损坏；措辞可能误导排查方向、违背“possible cause ≠ certain cause”产品原则 | **RESOLVED ✅**：evidence-scope guard + 状态正例语义 + 混合错误独立性 + Facts/Explanations/Checks 纪律；AI-B14~B17；用户 Manual UI Regression Smoke PASS + 经授权 Live ModelScope Smoke 五项验收全 PASS；verified LKGC = `01841b1` |

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









| 2026-09-08 | **ISSUE-005 修复并 RESOLVED**：AiAbortReason 归属 + 单一 QTimer owner（90s）+ OperationCanceledError 按 reason 分类 + UI 文案与 errorString 解耦；自动化全绿后**用户真实 27B Live Regression Smoke = PASS**（不再出现"操作被取消"）；verified LKGC 推进至 `bb3f3b4`（新 code fix commit）；docs-only 归档不再次推进 |
