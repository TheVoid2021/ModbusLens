# ISSUE-006: AI 解释过度归因 — 单批小样本推出链路长期不稳定 / 共通根因

- **状态**：OPEN（Review 完成，方案待批准，Implementation 未开始）
- **发现日期**：2026-09-09（Manual/Live ModelScope Smoke 中人工观察，T011 档案已记 non-blocking 观察，见 `docs/tasks/T011-ai-diagnosis.md` §594 行）
- **关联**：T011 Part B（T011 本身 DONE，本 Issue 为独立 follow-up，**不重写 T011 历史**）；不影响 LKGC `9e79558` / T012 NOT STARTED / M6 IN PROGRESS

---

## 1. 现象

真实 ModelScope（Qwen/Qwen3.5-27B）诊断输出中出现"措辞过度归因"类句子，而当时 batch 只有 **observed=4 / completed=4**（1 Success + 1 CRC Error + 1 Timeout + 1 Exception 0x02）：

- "当前 Modbus RTU 通信链路稳定性较差" —— 4 笔样本推出长期链路性质
- "协议状态混乱" —— 对多 finding 的非事实性概括
- "CRC 校验失败往往源于物理层信号干扰或电压波动" —— 把 possible cause 定调为 typical cause（伪因果）
- "混合错误出现暗示连接间歇性中断" —— 多种独立错误被合并为单一根因叙事

另有 T011 档案 §594 已记录的同类观察："Inconsistent results imply intermittent signal integrity problems rather than permanent configuration errors"（"rather than" 过强比较）。

**已知的确定性事实**仅为：1 笔 CRC 校验失败、1 笔无响应超时、1 笔设备异常 0x02。四句中的"稳定性较差 / 混乱 / 往往 / 暗示间歇性"均超出了这些事实。

## 2. 影响

- **产品原则受损**（非 Core 事实错误）：违反"AI 只能解释事实、不得把 possible cause 写成 certain root cause、样本量小时不得推断长期链路稳定性、EC 0x02 保持 Illegal Data Address 语义、混合错误不得自动推出同一根因"。
- 对真实调试场景是**误导**：0x02 本是寄存器地址表语义，被渲染成链路问题会把用户引向错误的排查方向。
- 无确定性数据损坏：TransactionStatus / Statistics / Baseline 全部由 Core 决定，AI 只作为解释文本透传（PlainText），已知边界有效。

## 3. 复现步骤

1. （需用户授权消耗 API quota）配置 `MODELSCOPE_API_KEY`，启动应用，运行 Demo Batch（4 笔），运行 Baseline Diagnosis，Ask AI。
2. 人工检查 AI 解释中是否出现：长期稳定性断言、单一/共通根因叙事、0x02 被归因为物理链路、possible cause 以确定语气表述。
3. 多次请求措辞有随机性（模型温度），因此本问题以概率形式出现——不能用"输出了没有"作为唯一验收，应以 prompt 约束 + 人工多轮 smoke 共同判定。

## 4. 定位过程（A–F 逐项核验，2026-09-09）

| 来源 | 核验结论 |
| --- | --- |
| **A. deterministic context**（`DiagnosisContext`） | **无措辞责任**。纯事实 struct：`DiagnosisTransaction{deviceAddress, functionCode, TransactionAnalysis}` + `DiagnosisContext{transactions, statistics(summarizeTransactions 自洽)}`；不含"稳定性/物理层/链路"任何词。 |
| **B. baseline findings wording**（`RuleBasedDiagnosis` → `DiagnosisReport`） | **无措辞责任**。纯 enum：`FindingCode`/`Severity`/`exceptionCode`/`ActionCode`；人话中文文案只在 App 层 formatter，进入 prompt 的是英文 code 名（`ExceptionObserved severity=Warning count=1 exception_code=0x02`）。注意点：`severity` 是协议影响分级而非因果强度，模型可能把它误读为"证据强度"，这是可以被 prompt 澄清的**诱导因子**，不是措辞来源。 |
| **C. prompt builder**（`DiagnosisPromptBuilder`） | **轻度责任**。user prompt 是纯结构化事实（total/detailed/statistics/baseline findings/transaction details），无归因词；但**缺少**两点：① 没有向模型显式提供异常码标准语义（0x02 = Illegal Data Address）；② 没有标记"这是小样本"这一事实。模型必须自行推断 0x02 含义并自行判断样本大小 → 给了自由发挥空间。 |
| **D. system instruction** | **主要可控责任**。现有 authority 段（authoritative / Do not recalculate / distinguish facts-explanations-checks / no certain root cause unless proved / no auto actions / no raw packet re-analysis）是**通用否定式**规则，但缺少**正面的、按状态语义的**定义（CRC/Timeout/0x02 各自只允许的事实表述），缺少小样本护栏、缺少"混合错误≠共通根因"禁令。模型在分类决策上被留给先验。 |
| **E. 模型自由生成** | **直接来源**。模型对 {CRC + Timeout + Exception 0x02} 组合的条件先验天然偏向"物理层干扰→间歇掉线"叙事；在 D/C 无更强约束时这一先验胜出。温度引入的措辞随机性是问题以概率出现的机制。 |
| **F. UI presentation** | **无责任**。QML 仅绑定 `aiDiagnosisText` 以 `Text.PlainText` 透传，不做任何改写/翻译/润色。 |

## 5. 根因

**直接**：E（模型先验自由生成过度归因措辞）。
**可控**：D（system instruction 只有通用否定规则，缺少确定性状态语义定义、小样本护栏、混合错误独立性禁令）+ C（0x02 语义未随事实下发；小样本事实未标注）。

即：核心事实通道（A/B/F）是干净且值得保持的；修正点集中在 prompt 语言层（C+D），这与"确定性 Core 是协议事实唯一 authority"完全一致——不动任何 Core 事实。

## 6. 解决方案（最小修正方案，待批准后 Implementation）

Production 改动**只有一个文件**：`src/ui/ai/DiagnosisPromptBuilder.cpp`（App 层 prompt 文本）。Core / Client / Controller / QML / enums / API 全部零改动。

### 6.1 system instruction 追加 "Attribution discipline" 段（在 authority 段之后；全部英文规则 + 少量中文示例词）

原文（现有 authority 规则）**一字不动**，追加：

```
Deterministic status semantics:
- CRC Error: the received data failed RTU CRC validation. Interference, wiring,
  grounding and serial settings are possible CHECKS, not established causes.
- Timeout: no valid response was observed before the timeout threshold.
  Do not claim the device is offline, broken or permanently unreachable.
- Exception 0x02: Illegal Data Address — the requested register address is outside
  the device register map. Associate it with register-map/function-support checks,
  NOT with link interruption.
Standard Modbus exception meanings: 0x01 Illegal Function, 0x02 Illegal Data Address,
0x03 Illegal Data Value, 0x04 Slave Device Failure.
```

```
Small-sample discipline: a single small batch proves nothing about long-run link
stability. When the prompt marks small_sample=true, state the observed counts as
one-batch facts and say explicitly that the sample is too small to conclude the link
is stable or unstable. Do not use trending conclusions such as '长期不稳定'、
'持续性差'、'间歇性故障'.
```

```
Multiple different findings in one batch do NOT imply a shared root cause or an
intermittent connection. Explain each finding independently with its own possible
explanations and suggested checks; do not unify them into one causal story.
```

```
Every possible explanation must be worded as an uncertain estimate (e.g. '可能',
'may indicate', 'cannot be ruled out'); a suggested check is NOT a confirmed cause.
Never conclude with '链路不稳定'/'链路质量差' unless the supplied facts prove it.
```

### 6.2 user prompt 增加小样本事实标注（动态、确定性）

在 `total_transactions=%1` 之后追加一行，仅当 `total_transactions <= 10`（新匿名常量 `kSmallSampleThreshold = 10`）：

```
small_sample=true
```

> 理由：system 是静态文本，无法知道 batch 大小；小样本是**每请求事实**，应由 user prompt 携带。阈值 10 是保守边界（Demo/Replay golden=4、Serial=1 恒标记；30 笔回放不标记）。该行为全程确定性、可测。

### 6.3 明确不做（防止 scope 膨胀）

- ❌ 不增加 Agent / tool calling / JSON schema / RAG / post-hoc 规则引擎 / 输出二次裁判模型。
- ❌ 不修改 deterministic Core 的任何事实产生逻辑。
- ❌ 不修改 Client/Controller/QML 行为；AI 输出仍 PlainText 透传（安全边界不变）。
- ❌ 不变更 T011 已归档的任何文档结论（Original 一律不动，只追加本 Issue 引用）。

## 7. 回归测试设计（AI-Bxx 追加，属于 `tests/test_ai_client.cpp`）

| 新测试 | 断言（golden context = 4 tx：Success/Exception 0x02/CRC/Timeout） | 覆盖点 |
| --- | --- | --- |
| **AI-B14 `promptAttributionDiscipline`** | `system` contains: `failed RTU CRC validation`；`Do not claim the device is offline`；`Illegal Data Address`；`do NOT imply a shared root cause`（mixed）；`small_sample`（guard 存在）；`long-run link`（小样本规则）。`user` contains `small_sample=true`（4 tx 触发）。b01 现有 authority 断言全部保留不动 | 覆盖点 1/2/3/4/5 + 6（existing distinguish 句子保留） |
| **AI-B15 `promptSmallSampleBoundary`** | 30 tx 批量（复用 b02 构造）→ `user` contains `total_transactions=30` 且 `!contains("small_sample")` | 覆盖点 1 的边界（大样本不触发） |

覆盖映射：①Small Sample Guard→B14/B15；②CRC Uncertainty→B14(CRC 语义句)；③Timeout Uncertainty→B14(offline 禁令)；④Exception 0x02→B14(Illegal Data Address)；⑤Mixed Errors→B14(shared root cause 禁令)；⑥Facts/Causes/Checks 区分→保留既有 distinguish 句 + B14 新句(uncertain estimate)。

UI 层不需要新测试：点 ①②③④⑤的治理发生在"AI 生成文本"环节，UI 测试无法判定（输出本就是不可控模型文本）；事实不变性已由既有 `ai10_aiNeverChangesFacts` 锁定（misleading 文本原样透传但 structured facts 零变化）。

## 8. 验证计划（Implementation 批准后执行）

1. `cmake --build build/debug` + 全量 ctest（ai_client 函数数增加、其余不变）。
2. 用户 Manual AI UI Smoke（fake/localhost 路径 + 文案审查）。
3. **Live ModelScope Smoke（真实 API）**：Demo Batch(4 tx) 重复 Ask AI 数轮，人工审查四段输出不再出现：长期稳定性断言、共通/间歇根因叙事、0x02→链路归因、possible cause 以确定语气表述。**消耗 quota，需用户授权后执行**；无授权则按政策记 NOT RUN，不伪报。
4. 全部通过后独立 commit（信息建议形如 `fix(T011): constrain AI explanation attribution discipline`，最终以批准时商定为准）→ 该 commit 经用户确认后成为新 LKGC；docs-only 归档提交不推进 LKGC。

## 9. 教训（沉淀）

- **事实通道与语言通道分离的正确姿势**：确定性层连"小样本"这种数学事实都不该由模型心算——凡是影响模型判断的、可由 builder 计算的事实（total/small_sample/exception semantics），都应显式下发，而不是期望模型"数得出来、记得住标准"。
- **否定式规则不足以塑造先验**：只说"不要宣称 root cause"挡不住"往往源于物理层"；必须给出**每种状态唯一允许的事实表述**（正例），模型才知道边界内的语言空间长什么样。
- **Severity 是风险分级不是因果强度**：进 prompt 的 `severity=Warning/Error` 若不被解释，可能被模型当证据组合器使用。
- 与 ISSUE-005 同构的治理哲学：**行为约束放在离生成点最近的确定性位置**（ISSUE-005 是 timeout owner + abort reason，本 Issue 是 prompt 文本），而不是在输出端修补。