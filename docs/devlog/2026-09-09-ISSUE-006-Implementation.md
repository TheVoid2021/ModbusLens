# Devlog 2026-09-09 — ISSUE-006 Implementation + Live ModelScope Smoke（归档）

## 今日工作（ISSUE-006 的实现与验收，Review 见同日 Review devlog）

- **Implementation（code/test commit `01841b1`）**，production 改动仅 `src/ui/ai/DiagnosisPromptBuilder.cpp`（+19）：
  - **Evidence Scope Guard**（替代被推翻的 `small_sample<=10` 阈值）：user prompt 无条件 `evidence_scope=current_observed_batch`；system 明确证据范围=当前 batch、禁止长期/持续性/间歇性外推（v1 无 longitudinal evidence）。取消阈值的理由入档：**count 不能论证长期代表性**——30/100 笔若仍是一次 session，同样不可外推。
  - **Deterministic Status Semantics**（正例语义）：CRC=未过 RTU CRC 校验（干扰/接线/接地/EMI 仅为 possible checks）；Timeout=阈值前未见有效响应（禁 offline/broken）；0x02=Illegal Data Address（关联寄存器地址/寄存器表/文档/请求配置）+ 0x01~0x04 标准表 + 未知码查文档 + 异常码禁止解释为 wiring/CRC/interruption/interference。
  - **Mixed Error Independence**：独立观察、无 shared root cause、禁"间歇中断/signal integrity/rather than"式比较。
  - **Facts/Explanations/Checks**：不确定措辞义务（可能/may/may indicate/possible）、不反写。原有 authority 规则一字未动。
  - 测试 AI-B14~B17（prompt contract 锁定；确认不假装"证明 LLM 一定听话"）；AI-B01~B13 + ai10 保留全 PASS。
- **自动验证**：clean 126 targets 零警告；ctest 20/20；deploy + minimal-PATH PASS；测试期零公网/零 token/零 quota。
- **用户 Manual ISSUE-006 UI Regression Smoke = PASS（10 项）**。
- **经授权 Live ModelScope Smoke = PASS（1 次最小配额，完整 production path，Qwen/Qwen3.5-27B）**，五项验收：
  - A Evidence Scope：模型明确 "evidence_scope 限于 current_observed_batch"、"当前批次共 4 次…观察到 multiple anomaly types"，无长期/持续性/间歇结论。
  - B CRC："表示接收到的响应字节校验失败，**可能与 serial settings、wiring、grounding 或 EMI 有关**"。
  - C Timeout："在 configured timeout threshold 前未收到有效响应，可能与链路延迟有关"，无 offline/broken。
  - D 0x02："对应 Illegal Data Address，可能与 request configuration 中的 register address 超出设备 register map 有关"，建议优先对照设备文档验证 register addresses。
  - E Mixed："Anomaly types 是独立观察…不意味着 shared root cause"。
  - 形态：简体中文为主、术语保留（Modbus RTU/CRC/Illegal Data Address/0x02/register map）、PlainText、四段语义清晰。模型对 guard 的关键句出现忠实复述（正例语义 > 否定式禁令的 live 证据）。
- **归档**：ISSUE-006 → **RESOLVED**（原文与设计演进保留）；PROJECT_STATUS（LKGC `01841b1`、K6 RESOLVED、变更记录×2）；BACKLOG；INTERVIEW_NOTES（5 问）。T011 仍 DONE；T012/T013 NOT STARTED；M6 IN PROGRESS。

## Live 输出摘要（完整原文见 ISSUE-006 §11；不含任何 secret）

概述 / 观测事实 / 可能原因 / 建议检查 四段；statistics 与 transaction details 准确复述；三项异常各以自身确定性事实解释；结论保持在"本次批次"与"可能"语气内。

## 提交

- code/test（verified LKGC）：`01841b1` `fix(T011): evidence-scope guard against AI over-attribution (ISSUE-006)`
- docs-only 归档：本 devlog 所在提交（**不**推进 LKGC；LKGC 仍为 `01841b1`）