# Devlog 2026-09-08 — T011 Part A Implementation（Diagnosis Context + Rule-based Baseline）

## 今日工作

- **T011 Part A Implementation 完成**（真实 RED→GREEN，code commit `06ef801`）：
  - `src/core/diagnosis/`（Pure C++20 Zero Qt）：`DiagnosisTransaction`（三字段纯事实）/`DiagnosisContext` + `buildDiagnosisContext`（summarizeTransactions 唯一统计规则保证自洽）/`RuleBasedDiagnosis`（七 FindingCode、三级 Severity、十二 ActionCode、Finding/Report；全规则按档案定案：NoData≠Healthy、Healthy 三条件、Pending 非 failure、CRC/Timeout 只述事实、Protocol=Error、Exception 升序分组 + 0x01~0x04 映射、固定 finding/action 顺序）。
  - Controller：`activeDiagnosisTransactions_` 三条发布路径同源（Demo 同批 makeEntry / Replay outcome 映射 / Serial 单条）；五处成功 batch 变化统一 clearDiagnosisState（失败切换保留）；runBaselineDiagnosis/clearDiagnosis；formatter 只翻译 report（确定性去重、无 root cause、无 AI 字样）。
  - QML：Diagnosis GroupBox（Deterministic Baseline 命名——Part A 无 LLM）。
  - 测试：DIAG-A01~A10（结构化断言）+ UI-D01~D08（ui_bridge 40 函数）；ctest 19/19；clean 114 targets 零警告。
  - **用户 Manual Baseline Smoke = PASS（A~E）**：golden 三 findings 正确、无 root-cause 宣称、Clear Diagnosis/Results 分工正确、Replay 与 Simulator finding 语义一致、Serial 无回归（无需硬件）。
- 架构结论落档：`AI is interpreter, not detector.`；Part A 产品代码零 HTTP/provider/API Key/LLM client/prompt/Agent/tool calling（grep 佐证）。

## 遇到的问题

- **PE-6**：heredoc 转义把 formatter 字面量 `\n` 写成真实换行（编译错）；以 chr(92)/chr(10) 拼接替换免疫双层转义修复。教训：跨 shell+python 写含转义字符的 C++ 源码，尽量用 Write 工具或 ASCII 码拼接，并自查生成内容。
- code commit 初版漏 ui_bridge 测试文件 → 按 Git 策略 amend 并入（当前任务最新未 push 提交）。

## 验证

```text
RED：buildDiagnosisContext / diagnoseTransactions undefined reference
GREEN：diagnosis 10/10、ui_bridge 40/40；ctest 19/19
clean：114 targets，warning/error 0
qml smoke exit=0；Core Zero Qt；无 LLM 痕迹 grep 零实现命中
deploy + Qt6SerialPort provenance + minimal-PATH smoke PASS
Manual Baseline Smoke = PASS（用户）
```

## Git

- Part A Implementation（code/config）：`06ef801`（**新 LKGC**）
- 本 devlog 随 docs-only 确认提交；LKGC 不再次推进。

## 下一步（待用户指令，不自动开始）

- T011 Part B — LLM Diagnosis Integration：Learning / Test Design（provider/network/credential/prompt/超时/取消/fallback 全部届时定案；原则已锁：不自动调 LLM、secrets 不入库、不做 Agent）。