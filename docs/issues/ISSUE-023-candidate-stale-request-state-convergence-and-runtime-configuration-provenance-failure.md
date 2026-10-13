# ISSUE-023: Candidate Stale Request State Convergence and Runtime-Configuration Provenance Failure

> 任务关联：T034-B2.1（Slice C1 / C1.1 审计 / C1.2）。状态：FIXED IN WORKING
> TREE / TARGETED REGRESSION PASS / NOT YET COMMITTED / FINAL FULL RELEASE
> REGRESSION PENDING / NOT CLOSED。Human 最终验收（B2.1 acceptance）与
> committed verification 之前不得标记 CLOSED。

## Observed

同一异步请求生命周期/来源（provenance）问题暴露为**两层缺陷**：

**Phase 1 —— Liveness 失败（P1）**

Candidate 请求处于 Running 期间，`AiRuntimeConfiguration` revision 因
provider/model 变更或 credential save/replace/delete 而递增。旧完成被
revision stale guard 正确拒绝，但该 revision-mismatch 分支是**纯早期
return**（`completeAttempt` 内）：

- 不清除/失效 `activeAttempt`
- 不离开 Running
- 不清除 busy
- 不调度后继请求

结果：Candidate 可**无限期停留在 Running**。后果包括：

- 同文档重试被 single-flight guard 吞掉（Running + 同 documentId/contentHash
  ⇒ `requestExtractionFor` 直接忽略）
- `deleteBlockerFor` 的 Running blocker 持续生效（该说明书无法删除）
- 不存在任何残余异步回调/timer/信号能够终止该 attempt

工程区分（关键证据口径）：

- **STALE RESULT SAFETY = PASS**（旧结果从不发布）
- **STATE CONVERGENCE / LIVENESS = FAIL**（控制器永不离开 Running）

**Phase 2 —— Provenance / Safety 失败（P0）**

在审查 Phase 1 修复（C1）时发现的更深层缺陷。C1 初版实现处理
`runtimeConfiguration` owner A → owner B swap（请求 X 由 A 创建、仍在
Running）的方式是**rebase**：把 `pendingConfigRevision_` 采纳为
`B.configRevision()`。请求 X 同时保留：同 generation、同 document/content
身份、Running 状态。因此 X 可以通过**全部** stale guards 并发布——尽管它
源自 A（其完成由 A 的凭据/epoch 授权）。

且 A → nullptr 时请求保持 Running，而 revision guard 的前置条件
`runtimeConfiguration_ != nullptr && ...` 使该 guard **整体被绕过**。

定性：**P0 stale/wrong-configuration result publication risk**。

如实记录：本缺陷发现于**未提交的开发期**（C1.1 只读审计）；无已知生产
事故；未观察到 Human credential/data exposure。

## Expected

- **Safety**：作废（obsolete）请求的完成**永不发布**——无论其陈旧原因
  （generation supersede、document/content 变化、revision 变更、owner
  变更）。
- **Liveness**：控制器必须**最终离开 Running**；不允许无限期 busy。
- **Provenance**：在途请求必须**终身绑定**创建它的 configuration
  owner/epoch；**REQUEST PROVENANCE MUST NEVER BE REBASED TO A DIFFERENT
  CONFIGURATION OWNER**。

## Impact

- Phase 1：P1 request-lifecycle / liveness 缺陷——无限期 busy UI、同文档
  重试不可达、目标说明书被永久禁止删除。
- Phase 2：P0 stale-result safety 缺陷——A 期授权的完成可在 B 所有权下
  通过全部 guard 发布（wrong-configuration publication path）；nullptr
  路径下 revision 保护被整体旁路。两者均发生于未提交开发期，被审计在
  发布前捕获；无生产影响、无凭据/数据暴露。

## Root Cause

**Phase 1**：generation-stale 请求通常有一个更新的在途 attempt 负责"终局
收敛"（其完成会落地终态）；revision-stale 请求**不必然**有后继 attempt。
实现把两类 stale 情形当作同一处理——假设"稍后的 attempt 会负责收敛"。
该假设对 configuration 变更为假：被丢弃 attempt 之后没有任何人负责终局。

**Phase 2**：在途请求的 provenance 仅由 `generation`、document/content、
独立标量 `pendingConfigRevision_` 表示；`activeAttempt` **不携带**
runtime configuration owner 身份。首版收敛实现因此把新 owner 的当前
revision 错当作属于旧在途请求（rebase），并在 nullptr 旁留下
条件性 guard 的旁路。

## Corrective Actions

最终采纳语义（C1 + C1.2，均限定在
`CandidateExtractionController.{h,cpp}`）：

**C1（same-owner revision 收敛）**：控制器通过 `configurationChanged`
连接主动响应 `AiRuntimeConfiguration` 的 epoch 变更。同一 owner 的
revision 在 Running 期间变更时：

- 当前 attempt 被逻辑失效：`activeAttempt_.generation = 0`（对旧完成
  永不可匹配）+ document/content 身份清空
- state 立即 `Running → Idle`；busy 变 false
- 无 success、无 provider failure、无自动重试、无新 secret 读取、
  无 Profile/Manual/cache 变更、无网络取消

**C1.2（owner-swap provenance）**：`setRuntimeConfiguration`：

- **same pointer A → A**：精确 no-op（早退）；当前请求保持有效；无连接
  扰动。
- **不同 owner A → B 且 Running**：旧 attempt **立即失效**；state → Idle；
  **绝不 rebase revision**。
- **A → nullptr 且 Running**：同样立即失效；state → Idle。
- **非 Running 时的不同 owner**：连接/owner 正常交换；不重写终局产品
  状态、不清除已接受 Candidate 事实。

共享私有辅助 `invalidateActiveAttempt()` 统一执行：attempt generation
失效 + document/content 身份清空 + state 收敛 Idle。
`completeAttempt` 现有 guards（generation / state / document+content /
revision）**全部保留、零弱化**。

连接所有权（审计 PASS）：旧 `configurationChanged` 连接在 owner 替换前
显式断开；新非空 owner 恰好连接一次；same-pointer setter 早退不产生重复
连接；QObject context 语义保证控制器析构时自动断连。

关于双 guard 的**准确措辞**（不得声称同一回调上两者都执行）：stale 回调
首先被 **generation guard** 拒绝；Idle state 是附加的独立防御条件。

## Verification

RED → GREEN 证据（仅 `modbuslens_ai_settings_tests` 定向目标）：

**C1 REAL RED**（修复前）：

- `set21_oldRevisionCompletionIsStale` — `"running"` vs 期望 `"idle"`
- `staleCred01_credentialReplacementInvalidatesInFlight` — `"running"` vs
  `"idle"`
- `staleCred02_credentialDeletionInvalidatesInFlight` — `"running"` vs
  `"idle"`
- `staleCred03_failedCredentialMutationKeepsInFlightValid` — 保持 GREEN

C1 修复后：36/36 定向 PASS。

**C1.2 REAL RED**（修复前）：

- `ownerSwap_invalidatesInFlightAttempt` — `"running"` vs 期望 `"idle"`
- `nullOwner_invalidatesInFlightAttempt` — `"running"` vs 期望 `"idle"`
- `sameOwner_reassignmentIsNoOp` — 修复前即 GREEN（控制组）

RED 总计：37 passed / 2 failed = 39 executed。
owner-swap 修复后：**39/39 PASS**。

账目：37 个常规 QTest slots + `initTestCase` + `cleanupTestCase` =
39 executed。

隔离证据：PUBLIC PROVIDER NETWORK REQUESTS = 0（QWARN 计数 0）；Human
credential access = 0；Windows Credential Manager access = 0。

## Regression Protection

已覆盖场景（全部在 `tests/test_ai_settings.cpp` 确定性矩阵内）：

- same owner + revision 变更（provider/model）→ 立即失效/idle → 旧完成
  不发布（set21）
- credential replacement → 同行为（staleCred01）
- credential deletion → 同行为 + **同文档重试不再被 single-flight 吞掉**
  → 经既有路径到达 `failed + not_configured`（staleCred02）
- failed credential mutation → revision 不变 → 在途请求保持有效、正常
  完成（staleCred03）
- A → B owner swap → 失效/idle → 旧 A 完成不可发布（ownerSwap）
- A → nullptr → 失效/idle → 旧完成不可发布（nullOwner）
- A → A → no-op → 正常完成保持有效（sameOwner）
- delete blocker：Running blocker 随失效消失（staleCred02 断言）

## Remaining Boundary

**NETWORK CANCEL = NOT IMPLEMENTED。** 作废的 provider 请求仍可能物理
完成并消耗配额；其结果在逻辑失效后不能发布。这是**当前被接受的边界**，
不是"取消已存在"的声明。修复仅覆盖逻辑发布/状态安全。

## Status

**FIXED IN WORKING TREE / TARGETED REGRESSION PASS / NOT YET COMMITTED /
FINAL FULL RELEASE REGRESSION PENDING / NOT CLOSED。**
在最终 B2.1 acceptance 与 committed verification 完成前不得标记 CLOSED。

## Engineering Lesson

本次事件横跨异步请求生命周期的三个层次，逐层递进：

1. **Safety**（stale guard）最初即成立——作废结果从不发布；
2. 纯早期 return 破坏了 **Liveness**——"稍后会有 attempt 收敛"的假设对
   configuration 变更为假，Running 永久化并连带删除阻断；
3. 首个 liveness 修复（C1 rebase）重新打开了 **Provenance/Safety**——
   把新 owner 的 epoch 错配给旧请求，制造 P0 发布洞；
4. 最终设计把 **owner 身份变更本身视为失效边界**：请求属于创建它的
   owner/epoch，身份变更即作废，永不 rebase。

教训：异步请求的"安全"不是单一 guard 的属性，而是 Safety（作废结果不可
发布）、Liveness（状态必须可达终局）、Provenance（请求终身绑定其创建
epoch）三条不变量的合取；修复其中一条时必须重新验证另外两条。另如实
记录：C1 期间 `staleCred01` 曾在测试内初始凭据 setup 之前捕获 revision R
基线导致假失败——定性为 **test-fixture defect，非产品缺陷**（基线移至
setup 之后修正），不单独立 issue。

---

## RESULT UPDATE（〔T034-DOC-RECOVERY-P1 补记 · 2026-10-13〕）

> 本节只追加状态更新与同族复发记录，不改写上文任何历史表述。

### 1. 修复提交与验收（证据链）

- 修复（C1 + C1.2，`CandidateExtractionController.{h,cpp}`）已随
  **`cf58fd6`**（2026-10-11，T034-B2.1 checkpoint，35 files）提交；同批含
  B3a（变更方法去 slot 化）与 B3b（main.cpp 生命周期次序）等结构性加固；
- B2.1 于 2026-10-12 由 `docs/PROJECT_STATUS.md` 时间线记录为
  **HUMAN ACCEPTED**；
- 含本修复的代码树在 B2.2-C / LC2 轮执行全量回归 =
  **CTest 73/73 PASS**（`8321f92` checkpoint 证据）。

**不声称**：B2.1 单独执行过 final full release ctest（档案未记录）。

**状态推进**：

`FIXED IN WORKING TREE / TARGETED REGRESSION PASS / NOT YET COMMITTED /
FINAL FULL RELEASE REGRESSION PENDING / NOT CLOSED` →
**`FIXED / COMMITTED（cf58fd6）/ HUMAN ACCEPTED（B2.1 层）/ 全量回归 73/73`**。

本 issue 原定的「Human 最终验收与 committed verification 之前不得标记
CLOSED」两项条件均已由上述证据满足；**是否正式标记 CLOSED 建议由 Human
在本轮 review 中裁定**（Agent 不自行宣布）。

### 2. 同族复发实例（Agent 链，`b1be954` / B2.2-C）

同一主题——**stale 交付携带共享状态副作用**——在 Agent 链迁移（B2.2-C）中
再现（对应 `docs/devlog/2026-10-12.md` §4）：

- **现象（D3→F1）**：stale callback 曾执行 `state_ = State::Idle`，会清掉
  **更新一轮 run** 的 busy（实测 `T5 busy=1,pend=2` → `T6 busy=0,pend=1`）
  ⇒ 新 owner 的在途 run 暴露为可重入。
- **修复**：stale delivery 修为**纯 `return`（no-op）**；busy 释放只走显式
  失效契约（`invalidateForBatchChange()` /
  `invalidateForConfigurationChange()`）。
- **oracle 对齐（F1-T1）**：b10/b11/b19 三条旧断言更新为显式失效契约
  （不把「stale 回调顺带释放 busy」当可依赖行为）。
- **覆盖**：B22G-01…11 套件（provider/model/credential/revision 切换、
  fail-closed、same-run snapshot 一致性、stale/new-owner overlap、startup
  no-network）。
- **状态**：FIXED / COMMITTED（`b1be954`）/ HUMAN ACCEPTED。

### 3. 模式提炼（防复发）

**「作废一个 attempt」与「清理它的副作用」是两个动作**——失效路径只允许
执行「使旧交付不可发布」的变更；任何共享状态（busy / 单飞锁 / 删除阻断）
的释放必须由**显式契约动作**承担，不得由 stale 回调顺带完成。Candidate 链
（本 issue）与 Agent 链（§2 复发）两处实现共同验证了这条规则。

### 4. 边界复述

**NETWORK CANCEL = NOT IMPLEMENTED**（接受的当前边界，不弱化、不改变）。
