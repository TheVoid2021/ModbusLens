# Devlog 2026-09-07 — T009 Part A Implementation（Replay Log Format + Replay Core）

## 今日工作

- **T009 Part A Implementation 完成**（真实 TDD，RED → GREEN）：
  - `src/core/replay/ReplayLog.{h,cpp}`：`.mlog` v1 数据模型（ReplayTransactionRecord/ReplayLog，defaulted `==`）；parser 错误模型八值 + lineNumber；`parseReplayLog(std::string_view)` 纯函数实现——逐物理行 1-based 计数、header/TXN 两态、`std::from_chars` 整段消费（拒符号/尾垃圾/溢出）、hex token 恰 2 字符、CRLF/空行/`#` 注释兼容、NO_RESPONSE 仅 response field 合法。零 Qt。
  - `src/core/replay/ReplayAnalysis.{h,cpp}`：`analyzeReplayLog`——request 可信链（decodeRtuFrame → functionCode==0x03 → decodeReadHoldingRegistersRequest），失败 → InvalidRequestWire / InvalidRequestFunction / **InvalidRequestData**（0-based transactionIndex）；response decode 失败为诊断事实进 T007（CrcError/ProtocolError）而不是 replay 失败；统计经 summarizeTransactions 与 Simulator 同源。**不调用 SimulatedSlave、不 sleep、不手工分类**。
  - `tests/data/demo_v1.mlog` golden fixture（configure_file COPYONLY + test-only compile definition 定位）；`tests/test_replay_log.cpp`（REPLAY-A01~A08 + 扩展，17 函数）、`tests/test_replay_analysis.cpp`（REPLAY-I01~I05 + I03B/I03C，7 函数）。
- 文档归档：T009 档案（Implementation/Problem/Verification/Result 等全章节）、PROJECT_STATUS（面板/§2/§7）、BACKLOG（M5/T009 行/变更记录）、02_ARCHITECTURE（Replay 行与目录树）、04_TEST_STRATEGY（Replay 夹具与口径一致性）、INTERVIEW_NOTES（T009 问答 + 补录 T007B/T008B）、本 devlog。

## 遇到的真问题与解决

- **PE-1：parseInteger 写进局部变量，out 参数从未赋值** → version 恒 0 → 所有 header 误报 UnsupportedVersion（大批测试失败）。用最小 standalone repro（g++ 直链 libmodbuslens_core.a）二分定位："局部=1、外部=0"两行打印锁死根因；修正为 from_chars 直接写 out。教训：out-param 模板助手必须让 from_chars 的目标就是 out 本身。
- **PE-2**：两处 `-Wrange-loop-construct`（range-for 按值拷贝 string）→ 改 `const std::string&`，零警告恢复。
- RED 期间另修一处测试自身编译错误：QVERIFY 用于非 void 函数 `readGoldenFixture` → 改显式 if 返回空串。

## 验证（真实命令与输出）

- RED：`cmake --build` → ld 101 处 `undefined reference to parseReplayLog(...)`（modbuslens_replay_log_tests）；analyzeReplayLog 同理。
- GREEN：`ctest -R "replay_(log|analysis)"` → 2/2 Passed；全量 `ctest --preset debug-local` → `100% tests passed, 0 tests failed out of 16`。
- `cmake --build --preset debug-local --clean-first` → 96 targets，warning/error 命中 0。
- Core Zero Qt：`grep -rniE "#include +<Q|QString|..." src/core/replay/` → exit=1（无引用）。
- ISSUE-001 防回归：3 处 `std::get_if` 均作用于具名局部变量。
- QML smoke：`--qml-smoke-test` exit=0。
- 提交前 `git diff --check` PASS；临时 build/green/red/clean log 与 repro 程序已删除（不入库）。

## Git

- 学习/测试设计 docs-only commit：`6159918`
- **Part A Implementation 代码提交（新 LKGC）：`e4920da`**
- 归档回填 docs-only commit（本 devlog 所在提交），LKGC 维持 `e4920da`

## 下一步

- T009 Part B — Learning / Test Design（Replay UI Integration）：FileDialog 加载 .mlog、填现有 Model/Dashboard、文件名显示、Clear Replay；无 real-time playback。待用户指令启动。