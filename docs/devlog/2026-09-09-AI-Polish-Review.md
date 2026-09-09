# Devlog 2026-09-09 — AI Explanation Language/Reasoning Polish Review（独立 follow-up，非 T012）

## 今日工作

- **AI Explanation Polish Review 完成（docs-only，未改 production code）**：针对人工 smoke 发现的过度归因措辞（"链路稳定性较差 / 协议状态混乱 / CRC 失败往往源于物理层干扰 / 混合错误暗示间歇性中断"，而 batch 仅为 observed=4：1 Success + 1 CRC + 1 Timeout + 1 Exception 0x02），完成仓库事实核验与根因分析：
  - **A–F 来源核验**：A（DiagnosisContext）纯事实无措辞；B（RuleBasedDiagnosis/Report）纯 enum（severity 为协议影响分级非因果强度）；C（prompt builder）缺 0x02 语义与小样本标注——可控缺口；D（system instruction）只有通用否定式规则、缺正例状态语义与小样本/混合错误禁令——主要可控缺口；E（模型先验）直接来源；F（UI Presentation）PlainText 透传无责任。
  - **根因**：模型对 {CRC+Timeout+0x02} 组合的先验叙事在语言层约束不足时胜出。
  - **最小方案**（production 仅改 `src/ui/ai/DiagnosisPromptBuilder.cpp`）：
    1. system instruction 追加 "Attribution discipline" 段：三种状态的确定性语义正例（CRC=校验失败，干扰/接线/参数仅为 possible checks；Timeout=未见响应，禁称 offline/broken；0x02=Illegal Data Address，关联寄存器表而非链路）+ 标准异常码 0x01~0x04 语义表 + small-sample 规则 + 混合错误不得推出 shared root cause + possible explanation 必须用不确定措辞（"可能"/"may indicate"）。
    2. user prompt 追加动态事实行 `small_sample=true`（total ≤ 10，新常量 `kSmallSampleThreshold`），全程确定性可测。
    3. 明确不做：Agent / JSON schema / RAG / post-hoc 规则引擎 / 改 deterministic facts / 改 Client-Controller-QML。
  - **回归测试设计**：AI-B14 `promptAttributionDiscipline`（system 含状态语义/offline 禁令/Illegal Data Address/shared root cause 禁令/small_sample 规则；user 含 `small_sample=true`）× 覆盖点 1~6；AI-B15 `promptSmallSampleBoundary`（30 tx 不含 small_sample 行）。UI 事实不变性由既有 ai10 覆盖，不需新 UI 测试。
- **档案**：新建 `docs/issues/ISSUE-006-ai-explanation-overattribution.md`（OPEN，Review 完成、Implementation 待批准）；PROJECT_STATUS Known Issues K6 + 变更记录；BACKLOG 变更记录。**T011 历史文档只增不改**（原 §594 观察保留，本 Issue 引用之）。

## 原则复述（本 Review 自检）

Deterministic Core 仍旧是协议事实唯一 authority；方案只约束解释层语言空间，不产生、不修改任何协议事实；治理位置选在"离生成点最近的确定性文本"（与 ISSUE-005 同构哲学）。

## 状态

- 无 implementation commit；无 quota 消耗；无真实 ModelScope 调用；LKGC 保持 `9e79558`；T011 DONE / ISSUE-004/005 RESOLVED 不变；M6 IN PROGRESS；**T012 NOT STARTED**。
- 下一步：等待用户批准 Implementation（届时执行 §8 验证计划：build+ctest → Manual AI UI Smoke → 用户授权的 Live ModelScope Smoke → 独立 commit → 用户确认后推进 LKGC）。