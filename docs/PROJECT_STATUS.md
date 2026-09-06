# PROJECT_STATUS — 项目状态单一事实源

> 规则：本文件在每个任务**完成时**更新（AGENTS.md 工作纪律 5）。任何协作者以此文件为真相，其次才是聊天上下文。
> Last Known Good Commit = 最近一次**构建+测试双通过**的主线提交；其后仅文档回填的提交以"补充提交"形式注明，不改动 LKGC。

## 状态面板

| 项 | 值 |
| --- | --- |
| 当前版本 | **0.1.0**（2026-09-05，T001 建立；T001.1 未改代码，版本不变） |
| 当前 Milestone（Current Milestone） | M2 ✅ / M3 ✅ 完成；**M4 进行中（T007 ✅ / T008 Part A ✅ DONE）**。注：按 BACKLOG 既有定义 M4=事务分析与界面（T007,T008），T008 完成后才关闭 |
| Last Known Good Commit | **`28f38b0`**（T008.1 代码/脚本提交：build+ctest 14/14+deploy minimal-PATH smoke 双重验证；当前 HEAD 为其后的 docs-only 回填提交，不改变 LKGC。历史值：T008A `76030a2`、T007B `0f3109a`） |
| Build 状态 | ✅ **通过** — Debug/MinGW 13.1.0/Qt 6.11.1/CMake 3.30.5，零警告（clean 全量重建 86 targets；App 已迁移 Qt Quick，Widgets 依赖移除） |
| Test 状态 | ✅ **14/14 通过**（ctest：`crc` / `frame` / `codec` / `f03` / `simulator` / `simulator_integration` / `fault` / `fault_integration` / `transaction` / `transaction_integration` / `statistics` / `statistics_integration` / `ui_bridge` / `qml_smoke`，69 个测试函数全过；原 `smoke` 随 QWidget bootstrap 移除） |
| 已完成任务 | T001 · T001.1 · T002 · T003 · T004 · T005 · T006 · T007 |
| 当前任务（Current Task） | **T008 Qt Quick / QML Analysis UI**（IN PROGRESS——Part B 未完成） |
| 最近完成任务（Last Completed Task） | **T007 Transaction Analysis**（Part A + Part B 全部 DONE） |
| 当前阶段（Current Phase） | **Part A: DONE ✅**（用户人工验收 12/12；Part B: Not Started） |
| 下一步动作（Next Action） | **T008 Part B — Learning / Test Design** |
| 下一 Part（Next Part） | **Part B — Analysis Dashboard + Deterministic Demo** |
| 下一任务（Next Task After T008） | **T009 Replay Mode** |
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

## 2. 当前任务

- **T008 Qt Quick / QML Analysis UI — IN PROGRESS（Part A: Qt Quick Migration + C++/QML Bridge ✅ DONE，用户人工验收 12/12）**：QWidget bootstrap → QML 迁移完成（Widgets 依赖移除）；AnalysisController + TransactionListModel 桥接落地（optional→hasX+value 贯穿 QML）；UI-A01~A06 + 真实 exe 的 QML load smoke 全绿；Manual Visual UI Smoke = PASS（用户确认）。**Part B（Dashboard+Demo）Not Started**；下一步动作 = T008 Part B — Learning / Test Design。
- ⚠ 独立遗留：Standalone Explorer Launch = FAIL / **ISSUE-002 OPEN**（runtime collision，部署/环境问题，修复待立项；不影响 Part A 验收结论）。

## 3. 下一任务

- **T009 — Replay Mode**（详见 [BACKLOG](BACKLOG.md)）：MLog 日志格式 v1（ADR）与读写、回放/暂停/调速/时间轴、口径一致性测试。依赖 T005/T006/T007 提供的分析链路（已就绪）；在 T008 完成后启动。

## 4. Known Issues（当前已知问题）

| # | 问题 | 影响 | 状态/应对 |
| --- | --- | --- | --- |
| K1 | 本机 Qt 在 AutoMoc 阶段出现 qtlicd 证书服务不可用的构建期警告 | 仅为构建日志噪音，产物正常 | **临时环境处理**：仅在本机（gitignored 的 CMakeUserPresets.json）注入 `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`。项目代码与提交文件**不依赖**该变量（T001.1 已澄清措辞，见 [ENVIRONMENT](ENVIRONMENT.md) §6） |
| K2 | 系统 PATH 中存在 Anaconda 的 Qt5 qmake 与 MinGW g++ 8.1.0（过旧） | 若直接裸用会产生 Qt/编译器 ABI 不匹配 | 规避：统一通过 `*-local` preset 注入 Qt 自带工具链；见 [ENVIRONMENT](ENVIRONMENT.md) |
| K3 | `Could NOT find WrapVulkanHeaders`（configure 提示） | 无（Qt Widgets 不依赖；仅影响未来 QtQuick/RHI 功能） | 记录观察，不处理 |
| K4 | ~~**[ISSUE-002] Explorer 启动 modbuslens.exe 失败**~~ **[RESOLVED ✅]**（无法定位输入点 `_ZNSt3pmr20get_default_resourceEv` 于 Qt6Gui.dll） | 仅影响"不经终端直接双击启动"场景；终端前置正确 PATH 后启动正常；**正确 runtime 下用户已人工确认 UI 12/12 正常** | **已解决（T008.1）**：`scripts/deploy_windows.bat` 生成 build/deploy 独立目录（runtime provenance SHA256=编译器 bin VERIFIED + minimal-PATH smoke PASS）；**用户 Explorer 双击确认 PASS**。ISSUE-002 置 RESOLVED |

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