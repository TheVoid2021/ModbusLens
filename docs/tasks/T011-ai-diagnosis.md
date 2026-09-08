# T011 — AI Diagnosis

> 状态：**IN PROGRESS**｜Part A（Diagnosis Context + Rule-based Baseline）：**DONE ✅**｜Part B（LLM Diagnosis Integration）：**Learning / Test Design ✅（docs-only，Provider=ModelScope API-Inference）→ Implementation ⬜**
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

> LKGC = `06ef801`；Part B 未开始；T012 未开始。---

# Part B — LLM Diagnosis Integration：Learning / Test Design（2026-09-08，docs-only）

> 本章为 Part B 设计定案；**Implementation ⬜ 未开始**。红线：本阶段 `src/`、`tests/`、`CMakeLists.txt`、`scripts/` 零修改；不实现任何网络代码、不真实调用 ModelScope、不向 ZCode 索要 Token。

## PB-A 架构原则

```text
Simulator / Replay / Serial → Deterministic Modbus Core → TransactionAnalysis + Statistics
→ DiagnosisContext → Rule-based Baseline → ModelScope LLM Explanation → QML
```

**LLM 是 interpreter，不是 detector。** LLM 不得：修改 TransactionStatus / 修改 statistics / 重新判 CRC / 重新判 Timeout / 把 exception 重解释成另一个 code / 修改 deterministic DiagnosisReport / 自动操作 Serial / 自动重发请求 / 修改文件 / 调用工具。**Deterministic Baseline 永远保留，绝不被 AI explanation 覆盖。**

## PB-B Provider v1 定案（不可谈判项）

| 项 | 定案 |
| --- | --- |
| Provider | **ModelScope**（魔搭 API-Inference） |
| Transport | Qt6 Network / raw HTTPS REST |
| API style | **OpenAI-compatible Chat Completions**（仅 HTTP/JSON 协议兼容性——**绝不等同于调用 OpenAI 服务或依赖 OpenAI 账户/Key**） |
| Base | `https://api-inference.modelscope.cn/v1` |
| Endpoint | `https://api-inference.modelscope.cn/v1/chat/completions` |
| 禁区 | OpenAI SDK / Python openai 包 / curl 库 / Boost.Beast / 第三方 HTTP client / Responses API（/v1/responses、output[] 解析）/ OPENAI_API_KEY |

不做 multi-provider abstraction：不建 ILLMProvider / ProviderRegistry / ProviderFactory / plugin / multi-provider UI——当前只有一个真实 provider，Over-engineering 无益。QNetworkAccessManager（项目已有 Qt6）足够一个简单异步 POST。

## PB-C Model / Credential / Endpoint 边界

- **Model ID**：候选默认 `Qwen/Qwen3.5-27B`（Learning 阶段不把"模型页面存在"误写为"用户 Token 一定有权限"）；`MODBUSLENS_MODELSCOPE_MODEL` 作为可选 env override。Implementation 后由**用户本机**做一次最小 live capability probe；若候选不可用，**不得猜另一个 model**——由用户给出自己实际可调用的 Model ID。
- **Token env**：`MODELSCOPE_API_KEY`（ModelScope Access Token），只允许当前 process environment 提供。禁止：源码 hardcode、CMake cache/CMakeLists、QML TextField、README/docs/Issue/devlog 真 token、test fixture 真 token、screenshot、Git tracked config。**QML 永远看不到 Token 字符串**（只知 `aiConfigured`）。
- **BYOK 定性（风险入档）**：这是"local developer / portfolio BYOK 桌面程序"——用户自己的 Token 经本机 env 提供给本地程序；**不是**"把项目方共享生产 Token 安全分发进桌面 exe"的方案。未来正式发布应由 backend/proxy/credential service 承担服务端安全边界；当前不建 backend，但绝不把共享 secret 烘焙进 exe。
- **不允许生产 endpoint override**：不设 MODELSCOPE_ENDPOINT / MODELSCOPE_BASE_URL / OPENAI_BASE_URL 等环境开关——Token 与 endpoint 均可被环境替换的组合会把真实 Token 导向未知主机。测试如需 fake server 只能走 C++ constructor test seam 注入（生产路径固定官方 endpoint）。
- **QtNetwork 预检（本阶段已实证）**：`include/QtNetwork/QNetworkAccessManager`、`lib/cmake/Qt6Network`、`bin/Qt6Network.dll` 均 EXISTS；compiler 仍 MinGW13.1；仓库外 probe（find_package(Qt6 6.11 REQUIRED COMPONENTS Core Network) + QNetworkAccessManager/QSslSocket 真实编译链接运行）**四步全 PASS，runtime `sslBuild=yes`**——QtNetwork/TLS 基线可用；Implementation 前无需另立 ISSUE。

## PB-D 分层与 Client 定案

- **Core Zero Qt 红线延续**：QtNetwork 不入 modbuslens_core；`src/core/diagnosis/` 继续 Pure C++20 Zero Qt/Zero Network。HTTP/ModelScope client 属 App/Adapter 层（推荐 `src/ui/ai/`）。
- **ModelScopeDiagnosisClient : QObject**（推荐）：职责仅——接收 caller 的 prompt/request data、构造 Chat Completions request、QNetworkAccessManager POST、timeout、cancel、解析 HTTP/provider response、emit success/error。**不得**：读 TransactionListModel / 决定 CRC / 算 statistics / 自建 DiagnosisReport / 操作 Serial / 管 source switching / 读任意文件 / Agent/tool calling。
- **Client state**：Idle / Requesting（完成回 Idle）。结果状态归 Controller：`aiDiagnosisBusy`、`hasAiDiagnosis`、`aiDiagnosisText`、`aiDiagnosisErrorMessage`、`aiConfigured`。**无 conversation state / chat history / thread ID / 多轮**——每次 Ask AI 都是独立 one-shot。

## PB-E 触发与前置（产品路径）

- **LLM 永不自动触发**：runDemoBatch/loadReplayFile/Serial 完成/runBaselineDiagnosis 都不连带 API。只有用户显式点击 **Ask AI** 才发生 external request（成本/隐私/可控性/Demo 稳定性）。
- **Baseline First**：Ask AI 仅在 `hasBaselineDiagnosis==true` 且 active batch 非空时允许；**C++ 侧必须再验证**（不依赖 QML disabled）。人工路径：Run Demo → Run Baseline Diagnosis → Ask AI。
- **Empty batch 不调 API**：显示 "No analysis data available" application error；Part A deterministic NoData baseline 已足够。

## PB-F Prompt 定案

- **只输入 structured facts**：DiagnosisContext + DiagnosisReport（status/device address/function code/elapsed/exception code/聚合统计/baseline finding codes+counts/action codes）。**禁止输入**：QML text、.mlog 注释、文件名、路径、sourceLabel、COM 描述、用户自由文本、任意外部指令（prompt injection surface 保持极小）。
- **不送 raw wire v1**：Core 已把 wire 转成协议事实；LLM 不需要重解 Modbus/重算 CRC（也省 token、缩 privacy surface、避免"模型结论与 Core 冲突"）。
- **Size bound**：完整 statistics + baseline findings 永远包含；transaction detail **最多 20 条**、deterministic selection（优先非 Success：Protocol/Crc/Timeout/Exception/Pending，保持原始 batch order；不足 20 以 Success 按原序补足）；超出则 prompt 明确 `total_transactions=N` / `detailed_transactions=20` / `details_truncated=true`。**不用 random sampling。**
- **Prompt determinism**：同一 Context+Report+config → system/user message 完全确定；timestamp/UUID/unordered iteration 不进 prompt（HTTP 层的 request id 属 transport 不进 prompt）。
- **System instruction 语义**（定案要点）：industrial Modbus RTU diagnostic explainer；"The supplied protocol facts and deterministic baseline are authoritative. Do not recalculate or contradict CRC status, transaction status, exception codes, statistics. Do not claim a certain root cause unless the supplied facts prove it. Clearly distinguish observed facts / possible explanations / suggested checks. Do not propose automatic actions. Do not claim access to data that is not supplied. Return a concise final explanation." 不让模型"analyze raw packets and decide what happened"——what happened 已由 Core 决定。
- **输出格式**：plain text（非 JSON schema/Structured Outputs——LLM output 只作 human explanation，不再进 Core）；建议 sections=Summary / Observed facts / Possible explanations / Suggested checks；`max_tokens` 约 768 + 要求 concise ≤250 words（Implementation 前经真实 Chat Completions contract 确认参数名；不支持则先记录真实 response/error 再定案，不得盲改参数名）。
- **Prompt 不打日志**：生产默认不 qDebug 完整 prompt、不入 devlog/request body（单测可对 deterministic fixture 检查 builder 输出）。

## PB-G Request Shape / Parser 定案

- **Request**：`POST {endpoint}/chat/completions`；Headers `Authorization: Bearer <MODELSCOPE_API_KEY>` + `Content-Type: application/json`；Body 概念 `{ "model": "<ModelScope Model ID>", "messages": [ {system...}, {user...} ], "stream": false, "max_tokens": 768 }`（实际字段以真实 probe + chosen model 为准）。**禁止** tools / tool_choice / function calling / web search / file search / MCP / conversation history。
- **Raw parser**（无 SDK，直接解析 raw JSON）：成功文本路径 `choices[] → message → content`；**不用 Responses API 的 output[]/output_text**；**不得假定 choices[0] 唯一有效**——按 choices 原始顺序找第一个含非空 usable `assistant.message.content`；多 choices 时用第一个（request 不要求 n>1）。测试锁死"choices[0] 无 content 不 crash"。
- **reasoning_content 规则**：不显示/不保存/不打日志/不作诊断事实/不回喂 Core；只消费最终 message.content。**只有 reasoning_content 而无可用的最终 content → InvalidResponse。**
- **Success contract**：HTTP success + JSON parse 成功 + choices 存在 + 至少一个 choice 有非空 usable final content 才算 success。HTTP 200 但 choices null/空/message missing/content empty/malformed → InvalidResponse。finish_reason 可作 metadata 记录但不得改确定性事实；final content usable 即视为 explanation success。
- **Error taxonomy（AiDiagnosisErrorCode）**：Preconditions = NotConfigured / InvalidConfiguration / NoData / BaselineRequired / Busy；Transport/Provider = NetworkError / Timeout / Unauthorized / RateLimited / ProviderRequestError / ServerError / InvalidResponse。**Cancelled = silent control path，不是诊断失败。**
- **HTTP mapping**：401/403→Unauthorized；429→RateLimited；400/404/422→ProviderRequestError；5xx→ServerError；Qt network failure→NetworkError；transfer timeout→Timeout；2xx malformed→InvalidResponse。Provider error body 可提取 sanitized message 供 UI；Authorization/Token/完整 headers 绝不入 error/log。

## PB-H 异步策略（timeout/retry/stream/cancel/stale）

- **不自动 retry**（zero retries）：429/timeout/5xx 只显示最新 error，用户自行再次 Ask AI（避免额度重复消耗与状态复杂化；无 retry scheduler/backoff）。
- **不 streaming**：`stream=false`——即使 ModelScope 示例多用 stream；禁止 SSE/delta parser/token 动画。简化 cancel/timeout/fake-server 测试/parse。
- **Timeout**：生产建议 30s（Implementation 以 Qt 6.11 实际 API 选择 QNetworkRequest transfer timeout 或最小 QTimer+abort）；禁 thread sleep/waitFor*/busy loop。超时 → abort → AiDiagnosisErrorCode::Timeout → busy=false；Baseline 不变。
- **Cancel**：`cancelAiDiagnosis()`——Requesting 时 abort reply + busy=false；取消是 user control（不显示红色 error）；同 batch 发起新 Ask 时保留旧 success；新请求成功 replace 旧结果、失败/cancel 保留旧 success；无旧 success 则 cancel 后无 AI result。
- **Stale async response（P0）**：引入 `activeBatchRevision_`（uint64）。每次 active deterministic facts 真正变化 `++revision`；Ask AI 捕获 `requestRevision`；reply 完成时 `requestRevision != currentRevision` → **silently discard**——即便已有 abort，revision guard 仍是必需防线。
- **Revision 增/不增（与 Part A atomic batch invariant 完全一致）**：增=runDemoBatch 成功、Replay 成功、Serial 完成结果、Serial connect 成功（空批）、clearResults；**不增**=runBaselineDiagnosis、clearDiagnosis、失败 Replay load、失败 Serial connect、Serial transport error（旧 completed batch 不变时）、refresh ports。
- **Batch 变化的 AI 清理**：每次 revision++ 若 AI Requesting → abort + busy=false；旧 AI explanation 与旧 AI error 一并 invalidate；Baseline 按 Part A 既有规则处理（新事实 → 所有旧派生解释失效）。
- **失败切换**：Replay/Serial 切换失败 → batch/revision/baseline/AI 全部保留、in-flight AI 允许继续（与 Part A failed-switch-keeps-baseline 同构）。

## PB-I Controller API / State 定案

```cpp
Q_INVOKABLE void askAiDiagnosis();
Q_INVOKABLE void cancelAiDiagnosis();

Q_PROPERTY(bool aiConfigured ...)         // env 有非空 MODELSCOPE_API_KEY（+ model config 有效）
Q_PROPERTY(bool aiDiagnosisBusy ...)
Q_PROPERTY(bool hasAiDiagnosis ...)
Q_PROPERTY(QString aiDiagnosisText ...)
Q_PROPERTY(QString aiDiagnosisErrorMessage ...)
Q_PROPERTY(QString aiModelName ...)
```

- **绝无** apiKey property/getter/tokenChanged/QML credential field。
- **aiConfigured 语义**：只表"当前 process env 存在非空 Token 且 model config 有效"（采用 candidate default 时仅需 Token 非空；若最终无 verified default 则 Token+Model ID 都非空——文档明确）。Configured ≠ Token valid ≠ Model available ≠ Quota available ≠ Network reachable。
- **env 读取时机**：Controller/Client 初始化时读一次（process env 不动态变化；不做 Refresh/Reload；改环境须重启 app）。用户需在**同一 PowerShell** 设置 env 后启动 deploy app。
- **clearDiagnosis() 语义扩展（Part B 后）**：清当前 batch 的所有派生诊断 = baseline + AI explanation + AI error；AI Requesting 时 abort + busy=false；**但不得**清 Dashboard/active batch/断 Serial/改 source/增 revision（facts 未变→仍 Clear Diagnosis ≠ Clear Results）。
- **runBaselineDiagnosis()**：只更新 baseline、不发网络；同 batch 已有 AI explanation 保留（facts 未变，想重问须再点 Ask AI）。
- **Test seam**：ModelScopeDiagnosisClient constructor 注入 endpoint URL / API key / model ID / timeout。生产：官方 endpoint + env Token + configured model；测试：127.0.0.1 fake endpoint + fake-test-token + fake-model。**不建** INetworkTransport/IHttpClient/通用 mock framework。**Fake server 绝不读取/接收开发者机器真实 MODELSCOPE_API_KEY**（测试显式注入 fake token——即使本机已设真 Token 也不可能把它发给 localhost 或写入测试日志；硬约束）。

## PB-J 测试矩阵

### AI-B01~B12（client/prompt layer，localhost fake server，不发外网）

| ID | 场景 | Expected | 优先 |
| --- | --- | --- | --- |
| AI-B01 | golden context → prompt builder | 含 deterministic facts/statistics/findings/exception 0x02 + authority instruction（facts authoritative/do not recalculate/do not contradict/do not claim certain root cause）；不含 raw wire/文件名/COM 描述/QML 状态/Replay 注释 | **P0** |
| AI-B02 | >20 transactions | detail ≤20；statistics+findings 仍表全批；details_truncated=true；非 Success 优先、补足按原序（deterministic） | **P0** |
| AI-B03 | fake server 捕获 request | POST 到注入路径；Authorization: Bearer fake-test-token；Content-Type: application/json；body=model/messages/stream=false/output-limit 参数；messages=system+user；无 tools/tool_choice/function calling/conversation/previous_response_id/web_search/file_search/MCP | **P0** |
| AI-B04 | HTTP 200 + choices[0].message.content | 正确提取 content；success signal；busy=false；绝不用 Responses output[] | **P0** |
| AI-B05 | choices[0] 无 usable content、choices[1] 有 | 不 crash；取第一个 usable | P1 |
| AI-B06 | malformed JSON / 200 但 choices 空/message missing/content empty | InvalidResponse，不 crash | **P0** |
| AI-B07 | 401/403 | Unauthorized；Baseline/Dashboard facts 不变 | P1 |
| AI-B08 | 429 | RateLimited；request count==1（零 retry） | P1 |
| AI-B09 | 500/503 | ServerError；count==1 无 retry | P1 |
| AI-B10 | message 含 reasoning_content + content | 只显示 content；reasoning 不显示/不存/不打日志；only-reasoning → InvalidResponse | **P0** |
| AI-B11 | fake server 延迟 → cancel | busy=false、无 success、无 user-visible transport error、不 crash；未变 batch 的旧 success 保留 | P1 |
| AI-B12 | 短注入 timeout（50~100ms） | Timeout；reply aborted；busy=false | P1 |

### UI-AI01~AI10（Controller level，QSignalSpy/lambda + fake client）

| ID | 场景 | Expected | 优先 |
| --- | --- | --- | --- |
| UI-AI01 | 无 MODELSCOPE_API_KEY | aiConfigured=false；Ask AI 不发网络、不清 baseline/Dashboard；sanitized message（不含 Token 内容） | **P0** |
| UI-AI02 | batch 非空但未跑 Baseline → askAiDiagnosis | 拒绝；无 HTTP；Dashboard 不变 | P1 |
| UI-AI03 | clearResults（空批）→ Ask AI | "No analysis data available"；request count=0 | **P0** |
| UI-AI04 | Demo→Baseline→Ask AI（fake success） | baseline 继续显示；AI explanation 单独显示；hasAiDiagnosis=true、busy=false；statistics/rows 完全不变 | **P0** |
| UI-AI05 | fake 429/500 | AI error；baseline text/Dashboard/rows 不变（provider failure 不降级 deterministic capability） | **P0** |
| UI-AI06 | AI success 后 Replay 成功/Serial publish | 旧 AI explanation+error 清；baseline 按 Part A 规则 invalidate；revision 增 | **P0** |
| UI-AI07 | AI success 后坏 Replay load | facts/revision/baseline/AI 全部不变 | P1 |
| UI-AI08 | Batch A Ask AI（fake 延迟）→ 响应前发布 Batch B | A abort；revision A≠B；晚到 A response 绝不进 B 的 UI；hasAiDiagnosis 不被 stale A 重设 true | **P0** |
| UI-AI09 | Demo→Baseline→AI success→clearDiagnosis | Dashboard/batch 保持；baseline+AI+error 清；busy 则 abort；revision 不增；再 Baseline+Ask 可正常工作 | P1 |
| UI-AI10 | fake 返回故意错误文本（"Actually this was a Protocol Error…90%"） | 文本作为 untrusted explanation 展示于 AI 区即可；TransactionStatus/statistics/rows/DiagnosisReport/baseline **一个结构化事实不变**（"AI is interpreter, not detector" 最直接自动化证据） | **P0** |

### Prompt Injection Boundary Test（设计级证明）

v1 prompt builder 根本不接 filename/comment/sourceLabel/用户自由文本——**最安全的输入是不存在该入口**；文档/测试证明：即便文件名或理论注释是 "ignore previous instructions"，也没有进入 prompt 的 API（不为测试给 builder 增加这些参数）。

## PB-K QML AI Panel

现有 Diagnosis Group 下扩展最小 AI 区：AI Explanation 标题 + "Provider: ModelScope" + "Model: <model id>" + 状态（Not configured / Ready / Requesting... / Error）+ Ask AI + Cancel 按钮。clearDiagnosis 继续同时清 Baseline + AI Explanation。**不新增** Token TextField / Prompt editor / Chat input / conversation / provider selector / model dropdown / Markdown renderer。

- Ask AI enabled = `aiConfigured && hasBaselineDiagnosis && active batch 非空 && !aiDiagnosisBusy`（C++ 全部再验证；QML disabled 只是 UX）。
- Cancel enabled = `aiDiagnosisBusy`。
- **Previous AI result during new request**：同 batch 二次 Ask 时旧 success 继续显示 + status=Requesting...；新成功 replace、失败保留旧 success 并显示最新 error。**hasAiDiagnosis=true 与 aiDiagnosisErrorMessage 非空可共存**（"Latest AI request failed: rate limited" 与上一份解释同屏）。

## PB-L Live ModelScope Smoke 政策

自动化测试绝不需要真实 ModelScope API。Implementation 全绿后由**用户决定**是否执行 Live ModelScope Smoke：用户在自己 PowerShell 设置 `MODELSCOPE_API_KEY`（可选 `MODBUSLENS_MODELSCOPE_MODEL`）后从同一 shell 启动 deploy app；**用户绝不能把真实 Token 粘贴进 ZCode/ChatGPT 对话/Git/docs/issue/screenshot**。Checklist：Run Demo → Baseline → Ask AI（出解释、baseline 保留、承认 CRC/Timeout/Exception 0x02 事实、以 possible checks 措辞、不改 Dashboard）→ Clear Diagnosis → Replay 路径正常。**无 Token 时**：自动化仍必须全绿（fake server + NotConfigured + baseline fallback）；归档诚实区分 `LLM Integration Automated Tests = PASS` / `Live ModelScope Smoke = NOT RUN (credentials unavailable)`——**绝不伪报真实 provider PASS**；Part B 是否在无 live 运行下标 DONE 由用户人工验收阶段按项目目标决定（Learning 阶段不预造结论）。

## PB-M TLS / Deploy / Provenance

- **QTcpServer fake server**：监听 127.0.0.1 随机 ephemeral port（不固定端口，避免冲突），最小 Chat Completions fake endpoint——验证真实 QNetworkAccessManager→localhost HTTP→headers/JSON body/async completion/HTTP mapping/cancel/timeout。
- **CMake link graph**：modbuslens（app）与 AI client tests → Qt6::Network + Qt6::Test；**modbuslens_core 继续 Zero Qt/Zero Network**。
- **TLS**：Implementation 后检查 Qt TLS/SSL runtime loading（本阶段 probe 已证 `sslBuild=yes`）；Qt Windows 需要 TLS plugin/backend 时由 windeployqt 正常部署——禁止手工乱拷 DLL；TLS 检查不用真实 Token。
- **Deployment**：重跑 deploy_windows.bat；Qt6Network.dll 由 windeployqt 自动部署；TLS backend 文件记录真实 deployed files；**Qt6Network provenance SHA256 == `D:\QT\6.11.1\mingw_64\bin\Qt6Network.dll`**（D:\QTDesign 当前不存在，按实际环境记录）；Qt6SerialPort provenance 不得回归；minimal-PATH `--qml-smoke-test` exit=0、无 QtNetwork/TLS/entry-point error；ISSUE-002/003 不回归。

## PB-N RED / GREEN Plan（9 阶段）

A. QtNetwork kit verification + 仓库外 probe（**本阶段已提前完成，全 PASS**）→ B. Prompt builder + request/response parser + AI client tests RED → C. ModelScopeDiagnosisClient GREEN → D. Controller state/revision tests RED → E. Controller integration GREEN → F. QML → G. deploy/TLS/provenance → H. 用户 Manual AI UI Smoke → I. 可选 Live ModelScope Smoke。**全程不得顺手开始 T012。**

## PB-O Knowledge I Must Be Able To Explain（30 题要点）

1. **为什么直接 ModelScope 不做 multi-provider abstraction？** 唯一真实 provider；抽象成本（registry/factory/plugin）在当前无益，YAGNI。
2. **"OpenAI-compatible" ≠ 用 OpenAI 服务？** 仅指 HTTP/JSON 协议形状兼容（Chat Completions 请求/响应结构）；服务宿主、账户、Token 全是 ModelScope 的。
3. **为什么桌面应用不能内置共享 ModelScope Token？** 桌面 exe 可被提取，共享 token 泄露=配额被盗+责任归属；BYOK 把凭据留在用户侧。
4. **BYOK vs production backend？** BYOK=用户自有 token 本机 env 使用；正式产品需 server-side proxy/credential service 做安全边界，客户端只拿短期凭证/代理请求。
5. **为什么 production endpoint 禁 env override？** Token（env）+ endpoint（env）双可替换 = 真实 Token 可能被发送到恶意主机；endpoint 仅测试 seam 可注入。
6. **为什么 Token 不暴露给 QML？** QML 层可被动态检查/修改面大；凭据只留在 C++ process env 读取，UI 只知 configured bool。
7. **为什么 AI 不需要 raw Modbus bytes？** Core 已产出权威协议事实；重传 wire = token 浪费 + privacy surface 扩大 + 模型易与 Core 事实冲突。
8. **为什么 prompt 必须 bounded？** 未来 Replay 大 batch 的成本/延迟/可读性；固定 20 + 显式 truncation 语义。
9. **为什么完整 statistics/baseline 保留、detail 可截断？** 聚合事实完整==权威诊断不变；detail 是示例性 evidence，截断不失真。
10. **为什么 prompt 不读 QML text？** QML 是输出层 presentation；prompt 唯一输入是 structured facts——防反推与注入。
11. **为什么 filename/.mlog 注释不进 prompt？** 自由文本=prompt injection surface；parser 忽略注释的既有行为顺势成为安全属性。
12. **为什么 one-shot 而非 conversation？** 每次诊断独立自足；history 带来 stale context 与注入面。
13. **为什么不维护 conversation history？** 无跨批次语义连续性需求；存储敏感事实+增加状态机复杂度。
14. **为什么不自动调用 AI？** 成本/隐私/可控性/Demo 稳定性；诊断请求必须由用户显式发起。
15. **为什么不自动 retry？** 重复额度消耗、复杂化 cancel 与 stale 状态；用户自行重试更可控。
16. **为什么 v1 不 streaming？** 目标是"稳定完成一次解释"；非流式大幅简化 cancel/timeout/测试/解析。
17. **为什么解析 choices/message/content 而非 output[]？** Chat Completions（v1 协议）为标准路径；output[] 属 Responses API（被禁）。
18. **为什么不能无条件 choices[0]？** provider 可能返回多 choice 或空 choice；健壮解析=遍历找第一个 usable。
19. **为什么 HTTP 200 仍可能 InvalidResponse？** 传输成功≠业务成功；JSON/结构/内容三重校验缺一不可。
20. **为什么不展示/保存 reasoning_content？** 产品只需最终人类可读解释；内部思考链不是业务状态，且可能含未审核内容。
21. **为什么 AI error 不清 baseline？** 确定性能力与增强能力正交；provider 故障不得降级确定性诊断。
22. **为什么同 batch retry 失败保留旧 success？** 事实 batch 未变，上一份解释依然有效；网络失败不等于解释作废。
23. **为什么 batch 变化必须清旧 AI？** 解释是某个事实 batch 的派生结果；事实变了，派生全部失效。
24. **为什么 abort 之外还要 revision guard？** abort 是尽力而为；晚到/已入队 response 只能靠版本比较兜底。
25. **为什么失败切换不取消 in-flight AI？** 失败切换未改变事实；对有效 batch 进行的解释工作不应被用户一次尝试破坏。
26. **为什么模型返回错误事实也绝不改 Dashboard？** 结构化事实只在 deterministic Core 侧由测试锁定；LLM 文本是 untrusted 展示物（UI-AI10 自动化证明）。
27. **为什么 client 属 App/Adapter 层？** HTTP/Qt 与 Core Zero Qt 原则；诊断语义层不被 transport 污染。
28. **为什么 T011 禁 tools/function calling/MCP？** T011 只做解释；行动能力（tools）属 T012 且初期 read-only——安全边界按能力类型切分。
29. **T011 与 T012 架构边界？** T011=Context→Explanation（只读输出）；T012=工具调用/行动接口（略读，类型层面无写 API）——先解释后行动的渐进路线。
30. **为什么 Provider 可替换而 Core 不改？** prompt builder/解析器与 client 绑 provider 协议；Lang 变化只碰 App 层，deterministic Core 输入输出契约不变。

## PB-P Implementation Plan（34 步，摘要）

1 QtNetwork kit verification + probe（✅ 本阶段已提前 PASS）→ 2 ModelScope provider/config boundary → 3 model configuration → 4 bounded deterministic prompt builder → 5 Chat Completions request builder → 6 raw parser → 7 reasoning_content 规则 → 8 ModelScopeDiagnosisClient → 9 localhost fake HTTP server → 10 AI-B01~B12 → 11 Controller AI properties/API → 12 activeBatchRevision → 13 cancellation → 14 stale guard → 15 AI invalidation 语义 → 16 UI-AI01~AI10 → 17 QML AI panel → 18 RED → 19 GREEN → 20 clean build → 21 full ctest → 22 Core Zero Qt → 23 QtNetwork link graph → 24 无 Agent/tool-call 验证 → 25 qml smoke → 26 deployment → 27 Qt6Network provenance → 28 TLS/runtime 验证 → 29 minimal-PATH smoke → 30 Manual AI UI Smoke → 31 可选 Live ModelScope Smoke → 32 code commit → 33 LKGC candidate → 34 user confirmation backfill。不得自动开始 T012。

## PB-Q PROJECT_STATUS（本阶段执行）

Current Task=T011；Current Part=Part B — LLM Diagnosis Integration；Current Phase=Learning / Test Design；Provider=ModelScope API-Inference；Next Action=T011 Part B — Implementation；Next Task After=T012；T011 overall=IN PROGRESS；M6=IN PROGRESS；Part A=DONE。

## PB-R docs-only 验证（本阶段执行）

`git diff --check`；src/tests/CMakeLists.txt/scripts 零修改；docs-only commit；LKGC 保持 `06ef801`；不得 push、不得 amend `06ef801`、不得开始 Implementation。