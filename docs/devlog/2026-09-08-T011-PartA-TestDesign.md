# Devlog 2026-09-08 — T011 Part A Learning / Test Design（Diagnosis Context + Rule-based Baseline，docs-only）

## 今日工作

- **T011 启动（Part A: Learning / Test Design，docs-only）**，全部定案落于 [T011 档案](../tasks/T011-ai-diagnosis.md)：
  - **宪法原则**：AI 不产生协议事实——CRC/Frame/一致性/异常码/Timeout/统计的唯一 authority 是 deterministic Core；AI 只能解释，且禁止 LLM 反写 TransactionStatus/statistics/触发自动行动。
  - **规则基线全套定案**：NoData≠Healthy（无观察≠健康）；Healthy 三条件（completed>0 且 success==completed 且 pending==0）；Pending 不算 failure；Timeout/CRC 只述事实 + possible checks（不宣 root cause：CRC≠线坏、Timeout≠掉线）；Exception 按 code 升序分组 + 0x01~0x04 标准建议映射；Mixed 保留多条独立 finding（不建 health score）；固定 finding 顺序=deterministic presentation 序而非因果序。
  - **数据模型**：DiagnosisTransaction / DiagnosisContext（buildDiagnosisContext 复用 summarizeTransactions，自洽 snapshot）/ FindingCode 七值 / Severity 三级（ProtocolError=Error 的取舍理由入档）/ ActionCode 十二值（结构化，human text 归 adapter）/ DiagnosisFinding / DiagnosisReport。
  - **Controller 定案**：§6 前置检查确认现未保留结构化 batch → Implementation 新增 `activeDiagnosisTransactions_`（与 rows/statistics 同批）；runBaselineDiagnosis/clearDiagnosis；新 batch/clearResults/connect 成功 invalidate、失败切换保留（与 atomic source transition 对齐）。
  - **UI 定案**：baselineDiagnosisText 单 QString 多行（取舍理由入档）；面板命名 "Baseline Diagnosis"，**不得叫 AI Diagnosis**（Part A 无 LLM）。
  - 矩阵：DIAG-A01~A10（空/全成功/单类故障/golden 混合/分组/确定性）+ UI-D01~D07（含 UI-D06 两来源同 finding——诊断不依赖数据来源）。
  - **Part B 原则提前锁定**：LLM 不自动调用（用户显式 Ask AI）；API Key 永不入 Git/文档/源码（env/local ignored）；T011 不做 Agent（属 T012）；prompt injection 边界预告（.mlog comments/自由文本不作 system instruction）。
- 文档同步：PROJECT_STATUS（面板/§2/§3= T012 预告/§7）、BACKLOG（M6 转进行中/T011 行/路线/变更记录）。

## 验证（docs-only）

- git 状态/log 复核一致（LKGC `33ed197` / HEAD `caa449c`；T008~T010 DONE、T011 未开始）。
- `git diff --check` PASS；`src/tests/CMakeLists.txt/scripts` 零修改；docs-only commit；LKGC 保持 `33ed197`。

## 下一步（待用户指令，不自动开始）

- T011 Part A — Implementation（23 步：模型→builder→规则→DIAG 测试 RED→GREEN→Controller batch/invalidation→presentation→QML panel→UI-D 测试→验证链→Manual Baseline Smoke WAITING FOR USER）。不开始 Part B、不开始 T012。