# T011 — AI Diagnosis

> 状态：**IN PROGRESS**｜Part A（Diagnosis Context + Rule-based Baseline）：**DONE ✅**（Learning / Test Design + Implementation + Manual Baseline Smoke PASS）｜Part B（LLM Diagnosis Integration）：⬜ Not Started
> T011 整体 IN PROGRESS；M6 按既有定义保持 IN PROGRESS（T012 未完成）。
> 前置确认：T008/T009/T010 DONE、M5 CLOSED、LKGC = `33ed197`、HEAD = `caa449c`、T011 未开始。M6 = AI 诊断与 Agent 工具（T011, T012），按 BACKLOG 既有名称与范围，不自行重命名。

## Goal

建立**确定性规则诊断基线**（Deterministic Rule-based Baseline）：即使无 API Key、无网络、LLM 不可用，ModbusLens 也能源于确定性 TransactionAnalysis 给出基础诊断。LLM 只是 enhancement，**不是单点故障**。

## 最核心架构原则（T011 的宪法）

**AI 不负责产生协议事实。** 以下事实的唯一 authority 永远是 Deterministic Core：CRC 是否正确、Frame 是否可解码、Function03 是否合法、Request/Response 是否一致、Exception Code、Timeout、ProtocolError、elapsed、Success Rate、Average Success Latency。AI / Rule Diagnosis 只能：解释已确定的事实、总结主要问题、给出可能原因、给出排查建议。

**禁止：LLM output → 修改 TransactionStatus；→ 修改 statistics；→ 自动改串口配置/自动重发请求/自动操作文件。**

## Part A Scope

做：Pure C++ Diagnosis Context、Pure C++ Rule-based Diagnosis、结构化 Findings、结构化 Recommendation Codes、deterministic tests、Controller active-batch diagnosis seam、minimal baseline QML panel、Run Baseline Diagnosis、stale diagnosis invalidation、deployment/manual smoke。

**禁止（全属 Part B 或永不）**：HTTP、QNetworkAccessManager、API Key、cloud provider、local model、prompt template、JSON API、RAG、embeddings、vector DB、MCP、Agent tools、function calling、multi-agent、auto actions。

## Diagnosis 不重新读取 QML

Diagnosis 输入必须来自 `core::TransactionStatus` 与结构化 `TransactionAnalysis`——**绝不**从 statusText/"CRC Error"/"Timeout" 这类 Presentation string 反推。QML 是输出层，不是事实来源。

## Diagnosis Current Batch

诊断对象 = **当前 active analysis batch**（非跨模式混合历史）：Replay 4 条→分析这 4 条；Serial 1 条→分析这 1 条。

## 结构化 Active Batch（Implementation 前检查结论）

**已核验**：AnalysisController 当前发布 paths（runDemoBatch/loadReplayFile/publishSerialResult）都只在局部构建 analyses、computed 后即丢弃——**未保留结构化 batch**；TransactionListModel 只有 Presentation roles。因此 Implementation 必须新增最小结构化 active batch（**不得从 QAbstractListModel roles 反推 Core 数据**）：

```cpp
// src/core/diagnosis/（Pure）：诊断视角的"一条事务"
struct DiagnosisTransaction {
    std::uint8_t deviceAddress{};
    std::uint8_t functionCode{};
    TransactionAnalysis analysis;

    bool operator==(const DiagnosisTransaction&) const = default;
};
```

Controller 每次成功发布 batch 时同步更新 `std::vector<DiagnosisTransaction> activeDiagnosisTransactions_`——与 statistics、rows **同一批数据**，不维护三套独立数字。

## Atomic Batch Invariant 延续（三来源同心）

Simulator publish：analyses = rows = diagnosis transactions = statistics（同一批 Demo）；Replay publish：ReplayBatchAnalysis → rows → diagnosis transactions → statistics（同一批）；Serial publish：1 analysis → 1 row → 1 diagnosis transaction → statistics。**failed Replay load / failed Serial connect / transport error 若未改变 active batch，Diagnosis 输入也不得悄悄改变。**

## clearResults 与新 batch invalidate 规则

- `clearResults()` 除了 statistics/rows 清空，还必须：active diagnosis batch 清空 + **当前 baseline diagnosis result 清除**（否则 Dashboard 已"无数据"而下面还挂着 "CRC Error detected" = stale diagnosis）。
- 任何**成功的数据变化**（runDemoBatch、loadReplayFile 成功、Serial 完成、clearResults、Serial connect 成功清旧 batch）→ `clearDiagnosisState()`（Diagnosis 是某个具体 batch 的派生结果；不用复杂 revision framework）。
- **失败的 source switch（Replay load failed / Serial connect failed）若 active batch 未变 → Diagnosis 保留**——与既有 atomic source transition 对齐。

## Pure Diagnosis Core（Zero Qt 铁律）

`src/core/diagnosis/DiagnosisContext.{h,cpp}`、`RuleBasedDiagnosis.{h,cpp}`：Pure C++20、Zero Qt、**不得 include** QObject/QString/QVariant/QJson*/QNetwork*/QFile/QUrl；modbuslens_core 保持 Zero Qt。diagnoseTransactions：deterministic、no I/O、no Qt、no clock、no random、no network。

## DiagnosisContext

```cpp
struct DiagnosisContext {
    std::vector<DiagnosisTransaction> transactions;
    TransactionStatisticsSnapshot statistics;

    bool operator==(const DiagnosisContext&) const = default;
};

// Builder：从 transaction.analysis 提取 vector<TransactionAnalysis>，
// 真实调用 summarizeTransactions(...) 生成 statistics → context 自洽、
// 可独立测试；禁止 Controller 手工 success++/手工算 rate。
DiagnosisContext buildDiagnosisContext(
    std::span<const DiagnosisTransaction> transactions);
```

**为什么 context 自己重算 statistics**：它是一份自洽、可独立测试的输入 snapshot——transactions 与 statistics 必须一致；复用 summarizeTransactions = 唯一统计规则（这不是重复实现统计）。**Context 不放 Qt sourceLabel**：Simulator/Replay/Serial 对同一组 TransactionAnalysis 应给同样的协议事实诊断；sourceLabel 属 Presentation——未来 LLM Part B 需要 "Replay Mode/COM3 @ 9600" 时由 Controller 作为额外 metadata 安全注入 prompt，不污染 deterministic Core。

## Finding Code / Severity / Finding / Report（数据模型定案）

```cpp
enum class DiagnosisFindingCode {
    NoData, Healthy, PendingObserved, ExceptionObserved,
    CrcErrorObserved, TimeoutObserved, ProtocolErrorObserved
};   // 不建 100 个诊断码

enum class DiagnosisSeverity { Info, Warning, Error };
// 定案（含理由）：NoData=Info（无观察≠故障）；Healthy=Info；Pending=Info（未完成不算失败）；
// Exception=Warning（设备明确拒绝，语义确定但非链路损坏）；
// CrcError=Warning（字节损坏:线路/设置/抓包皆可能）；
// Timeout=Warning（多种可能原因，事实只是"无响应"）；
// ProtocolError=Error（帧可解码事务却违背契约:地址/功能/数量不一致→配置或实现级错配的最强证据；
//   三级标尺无 Critical，Error 留给最强一档；取舍记录在案）。
// 不引入 Critical/Emergency/health score。

struct DiagnosisFinding {
    DiagnosisFindingCode code{};
    DiagnosisSeverity severity{};
    std::size_t affectedCount{};
    std::optional<std::uint8_t> exceptionCode;   // 仅 Exception finding 携带
    std::vector<DiagnosisActionCode> recommendedActions;
    bool operator==(const DiagnosisFinding&) const = default;
};

struct DiagnosisReport {
    std::vector<DiagnosisFinding> findings;
    bool operator==(const DiagnosisReport&) const = default;
};

DiagnosisReport diagnoseTransactions(const DiagnosisContext& context);
```

## Recommendation Codes（结构化 action code；不把 UI prose 写死进 Core）

```cpp
enum class DiagnosisActionCode {
    WaitForCompletion,
    CheckDevicePower, CheckSlaveAddress, CheckSerialSettings, CheckWiring,
    CheckNoiseAndGrounding,
    CheckFunctionSupport, CheckRegisterMap, CheckRequestParameters,
    CheckDeviceHealth, CheckDeviceDocumentation,
    InspectProtocolConsistency
};
// Rule Core：事实 → action codes；Qt Adapter：code → 用户 readable text；
// 未来 LLM：可读 codes 作为 deterministic baseline。
```

## 规则定案

- **NoData**：`transactions.empty()` → 仅 1 条 finding = NoData(Info)。**不得 Healthy**——没有观察数据 ≠ 通信健康。
- **Healthy**：`completedCount > 0 && successCount == completedCount && pendingCount == 0` 才 Healthy；affectedCount = successCount（如 2）。1 Success + 1 Pending 不得 Healthy；0 事务不得 Healthy。
- **Pending**：pendingCount > 0 → PendingObserved（count=pendingCount，action=[WaitForCompletion]）。Pending 不算 completed failure（尊重 T007 语义、与 successRate 口径一致）。
- **Timeout**：timeoutCount > 0 → TimeoutObserved（count，actions=[CheckDevicePower, CheckSlaveAddress, CheckSerialSettings, CheckWiring]）。事实只描述 "No response bytes were observed before timeout threshold"——都是 **possible troubleshooting steps 而非确定 root cause**，禁写 "device is broken"。
- **CRC Error**：crcErrorCount > 0 → CrcErrorObserved（actions=[CheckSerialSettings, CheckWiring, CheckNoiseAndGrounding]）。事实="received response bytes failed RTU CRC verification"；**禁断言"一定是线路干扰"**（baud/parity 错配、software/capture corruption 也可能）。
- **Protocol Error**：protocolErrorCount > 0 → ProtocolErrorObserved（actions=[InspectProtocolConsistency, CheckDeviceDocumentation]）。事实="response observed but failed protocol/transaction consistency（wrong address/function/shape/quantity mismatch）"；**不重新实现 subtype 判断**（T007 只给 ProtocolError，Diagnosis 不凭空猜）。
- **Exception 分组**：exceptionCount > 0 → 按 exceptionCode 分组、**code 升序 deterministic**（3 条 0x02/0x02/0x03 → 两条 finding：0x02×2、0x03×1）。
- **标准 Exception 建议映射**：0x01→CheckFunctionSupport；0x02→CheckRegisterMap；0x03→CheckRequestParameters；0x04→CheckDeviceHealth；其他→CheckDeviceDocumentation。Rule Core 只映射 recommendation code；human-readable text 放 Qt adapter。
- **Mixed failures**（golden：Success+Exception 0x02+CrcError+Timeout）：**不用** 模糊的 MixedFailure、**不建** 0~100 health score——同时保留 ExceptionObserved(0x02)/CrcErrorObserved/TimeoutObserved 各自 finding，让用户看见多种独立症状。
- **Finding Order（固定）**：ProtocolError → CrcError → Timeout → Exception（code ascending）→ Pending → Healthy/NoData。**文档明确：此顺序只是 deterministic presentation order，不是因果优先级或根因置信度排名。**

## Baseline 不宣称 Root Cause（安全/可信原则）

规则只能说 "Observed: CRC errors；Possible checks: wiring/serial settings/noise"——**不得说 "Root cause: bad cable"**；Timeout 不得说 "device offline"。除非当前事实真能证明。

## Controller Baseline API 与 UI 定案

```cpp
Q_INVOKABLE void runBaselineDiagnosis();   // 用 activeDiagnosisTransactions_ 构建 context → 规则诊断 → Presentation adapter → baselineDiagnosisText
Q_INVOKABLE void clearDiagnosis();

Q_PROPERTY(bool hasBaselineDiagnosis ...)      // NOTIFY diagnosisStateChanged
Q_PROPERTY(QString baselineDiagnosisText ...)  // 定案：单 QString 多行（\n 分隔）
// 取舍记录：QStringList 亦可行；单串 + hasX 与现有 optional→hasX+value 风格一致、最简单可测；
// Part B 的 AI Explanation 用独立属性共存，互不侵占。
```

Presentation adapter（Controller 层）示例（golden Demo）：

```text
Observed issues:
- CRC integrity errors: 1
- No-response timeouts: 1
- Device exception 0x02: 1

Suggested checks:
- Verify register map
- Check serial settings
- Inspect wiring / noise
```

UI 明确叫 **"Baseline Diagnosis"（Deterministic Diagnosis）**——**不得叫 "AI Diagnosis"**（Part A 没有 LLM）。Main.qml 轻量 Diagnosis 区域：`Run Baseline Diagnosis` 按钮 +（可选）`Clear Diagnosis` 按钮 + hasBaselineDiagnosis 时显示 baselineDiagnosisText；"Deterministic Baseline" 小标。Part B 后同一区域增加 "Ask AI"——**AI 永远不能覆盖/隐藏 baseline facts**。

## Part B 边界预告与原则锁定

- Part B Learning/Test Design 时才决定：LLM/provider、network client、credential source、request format、structured prompt、timeout/retry、cancellation、unavailable fallback、AI result UI。**Part A 不偷跑、不预选 provider、不写 API endpoint、不放 API key。**
- **AI 不自动调用**：禁止每次 Serial Read/Replay load 自动调 LLM/上传数据——未来必须用户明确点击 Ask AI 才发生 external request（成本/隐私/可控性/Demo 稳定性）。
- **Secrets 规则**：API Key 绝对禁止进入 Git/CMakeLists/README 真值/docs 真值/source/QML/screenshots/test fixtures；只能环境变量或 local ignored config（Part B 再定）。**Part A 不创建任何 key/config。**
- **T011 不做 Agent**：LLM 只是 Context→Explanation，**不得**调 tools/读任意文件/连串口/改参数/发请求/删日志——Agent tools 属 T012，且早期仍 read-only。
- **Prompt Injection 边界预告**：Part B 不应把 .mlog comments/文件名/任意自由文本当 system instruction；Part A Context 只含 structured deterministic facts（Replay parser 已忽略 comments，保持）；**不在 Part A 提前实现 prompt-security framework**。

## 测试矩阵（DIAG-A01~A10 + UI-D01~D07）

| Test ID | 场景 | Expected | 优先 |
| --- | --- | --- | --- |
| DIAG-A01 | Context empty | 1 finding = NoData(Info)；**无 Healthy** | **P0** |
| DIAG-A02 | 2 Success | Healthy，affectedCount=2；无 Timeout/CRC/Exception/Protocol/Pending | **P0** |
| DIAG-A03 | 1 Timeout | TimeoutObserved count=1 + power/address/settings/wiring 四 action；无 Healthy | **P0** |
| DIAG-A04 | 1 CrcError | CrcErrorObserved count=1 + serial settings + wiring/noise | **P0** |
| DIAG-A05 | 1 Exception 0x02 | ExceptionObserved code=0x02 count=1 + CheckRegisterMap | **P0** |
| DIAG-A06 | 1 ProtocolError | ProtocolErrorObserved count=1 + InspectProtocolConsistency | **P0** |
| DIAG-A07 | Mixed golden（Success/Exc 0x02/Crc/Timeout） | 无 Healthy；三条 failure findings；count 各 1；顺序按固定规则；context.statistics 经 summarizeTransactions = 4/4/0·1/1/1/1/0·0.25·25.0 | **P0** |
| DIAG-A08 | Exception 0x02/0x03/0x02 | 两条 finding：0x02×2、0x03×1；deterministic 顺序 | P1 |
| DIAG-A09 | 仅 Pending | PendingObserved；不产 Healthy、不产 failure final finding | P1 |
| DIAG-A10 | 同一 context diagnose 两次 | DiagnosisReport 完全相同 | P1 |
| UI-D01 | runDemoBatch → runBaselineDiagnosis | 含 Exception 0x02/CRC/Timeout；不含 ProtocolError/Healthy | **P0** |
| UI-D02 | Demo → diagnose → clearResults | hasBaselineDiagnosis=false，旧诊断不再显示 | **P0** |
| UI-D03 | Demo → diagnose → 成功 Replay load/新 batch | 旧 diagnosis 被清；需重新 Run Baseline Diagnosis | **P0** |
| UI-D04 | Demo → diagnose → 失败 replay load/failed serial connect | active batch 未变 → baseline diagnosis 保持 | P1 |
| UI-D05 | clearResults → runBaselineDiagnosis | 明确显示 "No analysis data available"，不 crash/不空白 | P1 |
| UI-D06 | Simulator golden 与 Replay demo_v1.mlog 事实相同 | Deterministic Core 得同样 finding set（诊断不依赖来源） | **P0** |
| UI-D07 | hardware-free serial seam 发布 Timeout → diagnose | 仅 TimeoutObserved；Dashboard 1 transaction；不依赖硬件 | P1 |

**禁止为 Part A 建 FakeOpenAI/MockLLM/PromptClient**——测试只验证 Deterministic Diagnosis。

## Manual Baseline Smoke（Implementation 全绿后，用户人工）

deploy 版 A. Run Demo → B. Run Baseline Diagnosis（显示 Exception 0x02 / CRC Error / Timeout + 合理 deterministic checks）→ C. Clear Results（Dashboard 空、Diagnosis 清）→ D. Load Replay demo_v1.mlog → Diagnose（与 Simulator golden finding set 一致）→ E. Serial UI 仍正常（不要求 Hardware Smoke）。**用户确认前 Manual Baseline Smoke ≠ PASS。**

## Deployment

Part A 原则上不新增第三方/runtime dependency；实现后 clean build、full ctest、qml smoke、deploy_windows.bat、minimal-PATH smoke 全部 PASS；QtSerialPort provenance 继续验证；ISSUE-002/003 不回归。

## Knowledge I Must Be Able To Explain（20 题）

**K1 为什么 AI 不能判断 CRC？** CRC 是位级确定性算法，权威在 Core 的实现与测试；LLM 输出是概率性文本，交由它判定协议正确性=把事实主权让渡给不可验证的东西。
**K2 为什么 TransactionStatus 是事实、AI output 是解释？** Status 由确定函数对 wire 字节产生（可测试）；LLM 文本不可判真伪，只能解释已判定的事实。
**K3 Rule baseline 有什么价值？** 无网/无 key/无 provider 时仍可诊断；也给未来 LLM 一个可靠的事实骨架（事实与解释分离）。
**K4 为什么没 API Key 产品仍应能诊断？** 项目第一原则"核心不依赖 LLM"；诊断准确性的底线由 deterministic core 承担。
**K5 DiagnosisContext 为什么结构化？** 自洽 snapshot（transactions+statistics 一致）；独立可测;LLM prompt 需要结构化事实而非 UI 字符串。
**K6 为什么不能从 QML statusText 反推？** Presentation string 可被文案改动/z再翻译；反推=重新解析自己的输出——数据流反向且脆弱。
**K7 为什么 context statistics 仍复用 summarizeTransactions？** 一份统计规则只有一个 owner;context 重算是自洽性手段不是第二实现。
**K8 为什么 NoData ≠ Healthy？** 无观察≠健康；宣称健康需要正向证据（completed>0 且全成功）。
**K9 为什么 Pending 不算 failure？** T007 语义=未完成；rate 的分母是 completed;诊断赶在未完成时叫 WaitForCompletion 而非告警。
**K10 为什么 Exception 按 code 分组？** 不同 code 是不同事实（非法地址 vs 非法数据），分组且升序保证 deterministic 与 LLM prompt 稳定。
**K11 为什么 CRC Error 不能断言"线坏了"？** CrcMismatch 只证明字节被破坏；来源可能是线缆/参数/抓取层——断言越界会误导排障。
**K12 为什么 Timeout 不能断言"设备掉线"？** 无响应≠设备失电；地址错/波特率错/线断都可能——事实描述须与根因建议分开。
**K13 为什么不用 health score？** 0~100 的权重是拍脑袋；结构化多 finding 保留各自事实，避免把异质问题压成一个数。
**K14 为什么 finding order 不是根因优先级？** Order 只是 deterministic presentation 序；各 finding 是并列症状，谁先谁后无因果含义。
**K15 为什么新 batch 必须清旧诊断？** 诊断是某批数据的派生结果;数据变了结果即失效，不建 revision framework 就整体 invalidate。
**K16 为什么 failed source switch 可保留诊断？** active batch 未变则诊断的输入未变——与 atomic source transition 同一逻辑（失败不动现状）。
**K17 为什么 Part A 不做网络？** baseline 的价值恰在"无外部依赖";网络/LLM 全部推给 Part B 单点决策，避免范围爆炸。
**K18 为什么 T011 不做 Agent tools？** Agent 是行动能力（工具调用/写操作），诊断是解释能力；两件事的安全边界完全不同，T012 才处理且初期 read-only。
**K19 为什么 LLM 必须用户主动触发？** 成本/隐私/可控性/Demo 稳定性——每次读操作自动上传工控数据不可接受。
**K20 baseline 如何降低 hallucination？** 把"可验证事实"与"解释"分层：LLM 只解释已锁定的 findings/actions，即使幻觉也不会污染事实层与 UI 统计。

## Implementation Plan（23 步）

1. 定案 Diagnosis 数据模型（DiagnosisTransaction/Context/Report/FindingCode/Severity/ActionCode）
2. buildDiagnosisContext（真实调用 summarizeTransactions）
3. RuleBasedDiagnosis（全部规则 + 固定顺序 + exception 分组映射）
4. DIAG-A01~A10 tests（RED）
5. RED 证据
6. GREEN（Core 全绿）
7. Controller active diagnosis batch（三个 publish path 同步更新）
8. 新 batch/clearResults/connect-success → clearDiagnosisState；失败切换保留
9. runBaselineDiagnosis / clearDiagnosis + hasBaselineDiagnosis/baselineDiagnosisText
10. Presentation adapter（code → readable text）
11. QML Diagnosis panel（Baseline Diagnosis 命名；不叫 AI）
12. UI-D01~D07 tests
13. clean build 0 warnings
14. full ctest（记录 target/test 数）
15. Core Zero Qt（core/diagnosis 无 Qt）
16. qml smoke
17. deploy_windows.bat + QtSerialPort provenance + minimal-PATH smoke
18. Manual Baseline Smoke → WAITING FOR USER
19. docs 归档（本档案 Implementation 章）
20. code/config commit（用户确认前 LKGC candidate）
21. 用户确认后 LKGC 推进 + docs-only backfill
22. 不开始 Part B / T012
23. 不 git push

## Files Changed（本阶段）

- 新增：`docs/tasks/T011-ai-diagnosis.md`（本文件）、`docs/devlog/2026-09-08-T011-PartA-TestDesign.md`
- 修改：`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`
- 未改动：`src/`、`tests/`、`CMakeLists.txt`、`scripts/`

## Verification（本阶段，docs-only）

```text
git status/log 复核：T008/09/10 DONE、T011 未开始、LKGC=33ed197、HEAD=caa449c ✓
§6 前置检查：Controller 未保留结构化 batch（需 Implementation 新增 DiagnosisTransaction 向量）✓
git diff --check → 通过；改动仅 docs/
```

## Result

Part A Learning / Test Design 完成：AI 与 Core 责任边界定案、数据模型/规则全套（NoData/Healthy/Pending/Timeout/CRC/Protocol/Exception 分组/固定顺序/root-cause 禁令）定案、矩阵 DIAG-A01~A10 + UI-D01~D07 落库、20 题问答、23 步计划齐备；Part B 原则（不自动调 LLM/Secrets/不做 Agent/Prompt 边界）提前锁定。**Diagnosis 未实现**；Part B 未开始；T011 整体 IN PROGRESS；M6 保持 IN PROGRESS（不得提前关闭）。

## Knowledge Learned

- **事实与解释分层**是本任务的产品级决策：diagnostic 系统的可信度 = 可验证事实层的完整覆盖 + 解释层永远不反写事实。
- **诊断数据流的单向性**：Core → structured batch → 规则 → findings → presentation →（未来）prompt；任何一层不得反向推断上一层。

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| Part A Learning / Test Design | 见 `git log` | `T011(Part A): 诊断语境与规则基线 — Learning / Test Design（docs-only）` |

> LKGC 维持 `33ed197` 不变（docs-only 不推进）。# T011 Part A 完成归档章（追加内容）

---

# Part A — Implementation（2026-09-08）

## Implementation

- **Pure Diagnosis Core（src/core/diagnosis/，Zero Qt）**：`DiagnosisContext.{h,cpp}`（DiagnosisTransaction 三字段纯事实 / DiagnosisContext / buildDiagnosisContext：复制事务、提取 analyses、`summarizeTransactions` 唯一统计规则 → 自洽 snapshot）；`RuleBasedDiagnosis.{h,cpp}`（FindingCode 七值 / Severity 三级 / ActionCode 十二值 / Finding / Report；diagnoseTransactions 纯函数）。规则实现：空批仅 NoData(Info)；Healthy 三条件（completed>0 且 success==completed 且 pending==0；唯一状态性 finding）；Protocol=Error；CRC/Timeout 仅述事实 + 定案 actions（顺序锁定）；Exception 按 code 升序分组 + 0x01~0x04 标准映射（code-less Exception 防御性跳过）；Mixed 多 finding、无 health score；finding 全序 Protocol→CRC→Timeout→Exception→Pending→Healthy/NoData（仅 presentation 序）。
- **Controller 集成**：`activeDiagnosisTransactions_`（std::vector\<DiagnosisTransaction\>）三条发布路径同源（Demo makeEntry 同批、Replay outcome 映射、Serial 单条）；五处成功 batch 变化统一 `clearDiagnosisState()`（Demo/Replay 成功、Serial 完成、connect 成功清批、clearResults），失败切换保留；backing state 先一致再发信号（无"新 stats+旧诊断"可见态）。`runBaselineDiagnosis()`（buildContext→diagnose→format，空批=有效 NoData 报告）/`clearDiagnosis()`（只清诊断，d08 锁"再跑恢复同文本"）+ `hasBaselineDiagnosis`/`baselineDiagnosisText`。
- **Presenter**：Controller 匿名 namespace `formatDiagnosisReport`——只翻译 report（finding/action phrase + Suggested checks 按 enum 声明序确定性去重）；不重判/不重数/不宣称 root cause/无 "AI says"。
- **QML**：Diagnosis GroupBox（Run Baseline Diagnosis / Clear Diagnosis / "Deterministic Baseline" / hasBaselineDiagnosis 双态显示）——命名明确非 "AI Diagnosis"。
- **测试**：`tests/test_diagnosis.cpp` DIAG-A01~A10（结构化断言 code/severity/count/exceptionCode/action 顺序，Core 不用 QString 测试）；ui_bridge 扩 UI-D01~D08（40 函数总）。

## Files Changed

- 新增：`src/core/diagnosis/DiagnosisContext.{h,cpp}`、`RuleBasedDiagnosis.{h,cpp}`、`tests/test_diagnosis.cpp`
- 修改：`src/ui/AnalysisController.{h,cpp}`（diagnosis 全套 + 三 publish 同源）、`src/ui/qml/Main.qml`（Diagnosis 面板）、`tests/test_ui_bridge.cpp`（+UI-D01~D08）、`CMakeLists.txt`（core 源 + `diagnosis` test target）

## Problems Encountered

- **PE-6（真实过程事故，已修复）**：heredoc 转义层把 formatter 四个 QString 字面量的 `\n` 写成了真实换行（编译错 "QStringLiteral was not declared"/"expected '}' at end of input"）。修复：用 ASCII 码（chr(92)/chr(10)）拼接 old/new 串，免疫 bash/python 两层转义；写 C++ 字面量内换行一律自查。
- RED 证据：`buildDiagnosisContext`/`diagnoseTransactions` undefined reference（真实链接失败）。
- amend 说明：code commit 初版漏暂存 tests/test_ui_bridge.cpp（UI-D 测试），按 Git 修订策略 1（仅 amend 当前任务最新未 push 提交）amend 并入——最终 code commit = `06ef801`。

## Verification

```text
RED：buildDiagnosisContext / diagnoseTransactions undefined reference
GREEN：core 后 diagnosis 10/10；Controller 后 ui_bridge 40/40（含 UI-D01~D08）
ctest --preset debug-local：100% tests passed, 0 tests failed out of 19（+diagnosis）
clean build（--clean-first）：114 targets，warning/error 命中 0
qml smoke（真实 exe，含 Diagnosis 面板）：--qml-smoke-test exit=0
Core Zero Qt：src/core/diagnosis 无 Qt include
无 LLM/AI 痕迹：grep OpenAI|Anthropic|Gemini|API_KEY|Bearer|QNetworkAccessManager|endpoint|prompt|tool call|embedding
  → 仅 2 处注释性日常词义命中（"endpoint"=从站端点、"no provider, no prompt"=面板注释）；零 AI/HTTP/key 产品实现
ISSUE-001 复查：get_if 全部具名 local
deploy_windows.bat：exit=0；Qt6SerialPort provenance SHA256 仍 == MinGW bin；minimal-PATH --qml-smoke-test exit=0
**用户 Manual Baseline Smoke = PASS（A~E）**：golden 三 findings 正确（CRC 1/Timeout 1/Exception 0x02 各 1）
  无 Healthy/Protocol Error 误报、未宣称 root cause（无 bad cable/device offline）、
  Clear Diagnosis 与 Clear Results 分工正确、无 stale diagnosis、
  Replay 的 finding semantics 与 Simulator golden 一致、Serial Controls 无回归（无需硬件）
```

## Result

**T011 Part A = DONE（2026-09-08）。** 架构结论（本任务核心命题）：

```text
Deterministic Core
     ↓
DiagnosisContext（自洽事实快照）
     ↓
Rule-based Diagnosis（观察 + 结构化排查建议，永不宣称 root cause）
     ↓
Human-readable baseline（Presentation 层翻译，永不反写事实）

AI is interpreter, not detector.
```

Part A 实现零 HTTP/provider/API Key/LLM client/prompt/Agent/tool calling（grep 佐证）——断网/无 key/无 provider 时诊断能力完整可用（项目第一原则）。

**T011 overall 仍 IN PROGRESS**（Part B — LLM Diagnosis Integration 未开始）；**M6 按 BACKLOG 既有定义继续 IN PROGRESS**（T012 未完成，不提前关闭）。

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| Part A Implementation（code/config） | `06ef801` | `T011(Part A): add deterministic diagnosis baseline`（**新 LKGC**） |
| 用户确认归档（docs-only） | `<docs-only HEAD>` | Manual Baseline Smoke PASS 回填 |

> LKGC = `06ef801`；Part B 未开始；T012 未开始。