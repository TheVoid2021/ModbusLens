# Devlog 2026-09-07 — T009 Part A Learning / Test Design（docs-only）

## 今日工作

- **T009 Replay Mode 启动（Part A: Replay Log Format + Replay Core，Phase: Learning / Test Design，docs-only）**：
  - Replay 角色定案：历史文件重新分析，不调用 SimulatedSlave（[T009 档案](../tasks/T009-replay-mode.md)）
  - `.mlog` v1 格式定案：`MODBUSLENS_MLOG|1|timeout_ms=` header + `TXN|elapsed|request|response/NO_RESPONSE` records + 注释/空行；选择 line-oriented 而非 JSON 的理由落库
  - 数据模型/错误模型/API 定案：ReplayTransactionRecord / ReplayLog / ReplayParseErrorCode（8 值）/ ReplayExecutionError / ReplayBatchAnalysis；parseReplayLog(string_view) + analyzeReplayLog(log)；optional<bytes> vs 空 vector 语义区分（与 T006 一致）
  - 分层规则落库：Text Syntax → Wire Codec（T004 复用）→ Transaction Analysis（T007 复用）；**坏 Request = Replay execution error，坏 Response = 诊断结果 CrcError**
  - 全部 wire 金样经一次性独立 CRC 脚本复核通过：request `01 03 00 00 00 02`→`C4 0B`、ex response `01 03 04 00 64 00 C8`→`BA 7A`、ex request `01 03 00 64 00 01`→`C5 D5`、ex response `01 83 02`→`C0 F1`、坏 CRC `BA 7B`
  - 测试矩阵落库：REPLAY-A01~A08（parser）+ REPLAY-I01~I04/I05（integration）
  - 不做 IFrameSource 的决策留痕；Part B 边界（Load .mlog UI + 填现有 Dashboard，无 real-time playback）明确
- 文档同步：PROJECT_STATUS（面板/§1 补 T008 行/§2/§3/§7 变更记录）、BACKLOG（M5 转进行中、T009 行、路线图）

## 关键决策

- **不用 JSON**：C++20 无标准 JSON parser；引 Qt JSON 会污染 Core 边界，引第三方库增加依赖——line-oriented 格式在当前范围是工程取舍而非"永远优于 JSON"。
- **Bad Request vs Bad Response 非对称**：前者令 Replay 无法建立正常 Transaction（execution error）；后者正是要诊断的历史故障（CrcError/ProtocolError）。
- **Parser 纯函数化**：`std::string_view → ReplayParseResult`，让 Core 无 QFile/QString 依赖、单测无真实文件系统；文件 I/O 留给 Part B 的 Qt/App 层。

## 验证

- 独立 CRC 复核（Python 一次性脚本，不入仓）：5 条 wire 全部通过
- `git diff --check` 通过；改动限定 `docs/`（src/tests/CMakeLists.txt/scripts 未动）
- docs-only commit，**LKGC 保持 `4075223` 不变**

## 下一步

- T009 Part A — Implementation（==待用户指令，不自动开始==）：ReplayLog.h/.cpp → parseReplayLog → ReplayAnalysis.h/.cpp → analyzeReplayLog → tests/data/demo_v1.mlog → REPLAY-A01~A08 + I01~I04/I05 → RED → GREEN → 归档