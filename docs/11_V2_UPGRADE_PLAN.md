# 11_V2_UPGRADE_PLAN — ModbusLens V2 Upgrade Plan（Gate 0：Freeze V1 & Establish Upgrade Safety Baseline）

> 状态：**V2 Gate 0**（2026-09-14 建立，docs-only，未实现任何 M9~M12 内容）
> 原则：**extend, do not silently redefine** —— V2 所有工作都建立在 V1 deterministic core 与既有验证资产之上；不得为 V2 重新定义旧 TransactionStatus 语义、改变 Statistics 语义、改变 Replay 已验证行为、改变 Broadcast ExpectedNoResponse 语义、让 AI 重新拥有协议事实判断权、让 Agent 获得隐式设备写权限、删除旧测试以让新实现通过、降低 validation gate。
> 编号说明：本文件按任务指令命名 `11_V2_UPGRADE_PLAN.md`，与既存 `11_PROJECT_FINAL_RETROSPECTIVE.md` 共用 `11_` 前缀（数字前缀在此仓库并非全局唯一 ID）；如 Review 认为应按序改号，另行处理。

## 0. V1 基线事实（以 Git + 文档核验，非记忆）

- 当前 HEAD：`a132f9c`（`docs: optimize GitHub project overview`）。
- verified LKGC（PROJECT_STATUS 记录）：**`ae067ab`**（T015 整体 DONE 的 verified code/test baseline）。
- `ae067ab` 之后全部提交：`66c464a`、`555fa30`、`66b2b1a`、`19db1c2`、`65c24d5`、`3885447`、`a132f9c`。
  - **docs-only**：`66c464a`（T015 Final Acceptance 归档）、`66b2b1a`（复盘落库）、`19db1c2`、`65c24d5`（简历/面试素材）、`a132f9c`（README + 架构图移动）。
  - **docs + 复现工具/样本入库（无产品行为变化）**：`555fa30`（benchmark 证据 `docs/10` + `scripts/bench_replay/` + `.gitignore`）、`3885447`（架构图 + 7 个验收 sample 文件入库为 tracked）。
  - **结论：`ae067ab` 之后没有任何 production / tests 行为改动。** V1 产品基线 = `ae067ab`；当前 HEAD 只是在其上叠加展示与文档资产。
- remote = `origin/https://github.com/TheVoid2021/ModbusLens.git`（main 与 origin/main 同步）；仓库当前**无任何 tag**。

## 1. V1 Frozen Behavioral Contracts（真实当前行为）

### Modbus Core
- CRC-16/MODBUS：按位实现、低字节在前、规范向量 KAT 对拍（`123456789`→0x4B37；`01 03 00 00 00 01`→wire `84 0A`）。
- RTU 帧模型 `ModbusRtuFrame{address, functionCode, data}`（不存 CRC）；codec `encodeRtuFrame` / `decodeRtuFrame`（错误：FrameTooShort / CrcMismatch）。
- FC03：请求 quantity **1..125**（越界=请求侧 issue）、正常响应 byteCount=2N、异常 `0x83`+单字节；正常响应值仅在解码期使用、**不进入 TransactionAnalysis**。
- FC06：**仅 passive 语义**（request decoder + exact-echo matcher；无 encoder/send API）。
- Function 0x10：**仅 passive 语义**（structural field reader；quantity **1..123**、byteCount=2×quantity、data=5+byteCount；normal response=起始地址+写入数量恰 4 字节；异常经 generic exception matcher `0x90`；无 encoder/send API）。

### Transaction Analysis
- `TransactionStatus` 七值：`Pending / Success / Exception / CrcError / Timeout / ProtocolError / ExpectedNoResponse`。语义冻结：Success=“观察到匹配 normal response”；Timeout=“NoResponse ∧ elapsed≥阈值”；`ExpectedNoResponse`=**response expectation / observed outcome**（与 request validity 正交，不证明写入成功）。
- 响应侧 `TransactionIssueCode`：**9 业务值 + 1 防御哨兵 `UnknownProtocolError`**（业务口径不计哨兵）：ResponseFrameTooShort / ResponseAddressMismatch / MalformedExceptionResponse / MalformedNormalResponse / QuantityMismatch / UnexpectedResponseFunction / WriteSingleRegisterEchoMismatch / UnexpectedResponseForBroadcast / WriteMultipleRegistersEchoMismatch。
- 请求侧 `TransactionRequestIssueCode`：**4 值**：InvalidRequestQuantity（observed/minAllowed/maxAllowed）/ InvalidRequestLength（observed/expected，request-data 字节单位，uint16）/ InvalidRequestByteCount（observed/expected）/ InvalidBroadcastFunction。
- requestIssues 为**有序 collection**（reporting order ≠ discard priority；防 cascade 依赖规则冻结）；`Success + requestIssues`、`ExpectedNoResponse + requestIssues` 为合法组合。
- Generic exception matcher 前置守卫冻结：`(request.functionCode & 0x80) == 0`（0x88/0x88 绝不判 Exception）。

### Replay
- `.mlog v1` 语法冻结（仅 `NO_RESPONSE` token；4 字段 TXN；fail-fast 语法错误）。
- 请求信任链拆为 passive per-record：valid RTU wire 之后 semantic-invalid / supported / unsupported 全部 per-record；**bad record 不无条件污染整批**；request-wire corruption（CRC/FrameTooShort）仍 deferred（Gate E）。
- **Unsupported ≠ ProtocolError**：`UnsupportedObservedTransaction` 是显式事实，不进 TransactionStatistics。

### Serial
- 主动能力 = **FC03 单次读**（8N1，一次一笔 outstanding；Busy 拒绝；chunk 累积 + candidate 收口；oversized 不截断）；FC06 / Function 0x10 **无任何主动写路径**（源码 grep 级锚证）。
- Timeout 双路语义：空 buffer→NoResponse→Timeout；partial→wire-truth 诊断（CrcError/ProtocolError），绝不伪造 Timeout；transport error ≠ Modbus 诊断；stale completion 有 pending-metadata guard。

### Statistics
- `observed=pending+completed`；`completed=success+exception+crc+timeout+protocol+expectedNoResponse`；**successRate=success/(completed−expectedNoResponse)**（分母 0⇒nullopt）；`averageSuccessLatencyMs` 仅 Success；requestIssues **不是** 第八状态、不产生统计维度（DIAG-A14 锁定正交）。

### Diagnosis
- deterministic baseline 拥有全部协议事实（`ProtocolError/CRC/Timeout/Exception/ExpectedNoResponse/RequestIssue` 六类 finding 族 + Healthy 五条件）；finding 固定顺序 = Protocol→CRC→Timeout→Exception(asc)→Pending→ExpectedNoResponse→RequestIssue→Healthy/NoData。

### AI
- **interpreter, not detector**：LLM 不重判 CRC/TransactionStatus/原始 wire；success/high rate 不擦除 request issues；system prompt 属性纪律冻结（ISSUE-006 家族）。

### Agent
- 恰好 **3 个 read-only tools**：`get_session_summary` / `get_recent_anomalies` / `get_transaction_detail`；**无任何 device write/control**；anomaly predicate=status-anomaly OR requestIssues 非空；snapshot 自洽 + batch-revision/run-generation 双层 stale guard + 预算硬上限（3 rounds / 6 calls）。

## 2. V1 Regression Assets（行为 → 证据映射，全部为真实现有测试名）

| V1 行为 | 保护它们的行为工况 + 证据 |
| --- | --- |
| Broadcast ExpectedNoResponse（正交） | `test_passive_analysis.cpp`：`p09_fc06BroadcastExpectedNoResponse` / `c12_broadcastNoResponse` / `c14_invalidBroadcastNoResponse` / `bcast_c01` / `p10_broadcastUnexpectedResponse` / `c13_broadcastResponse` / `bcast_c02`；`test_transaction_statistics.cpp`：`b10_expectedNoResponseFormula`；`test_diagnosis.cpp`：`a12_expectedNoResponseObservation` / `a13_broadcastNeverHealthy`；`test_agent_tools.cpp`：`a12_t015FactsInTools` / `a13_requestIssueAnomalySemantics`；`test_ui_bridge.cpp`：`t02_t015Presentation` |
| Unsupported ≠ ProtocolError | `p07_unsupportedNormalFunction`、`c15_fc08StaysUnsupported` |
| requestIssues 多 issue + 防 cascade | `multi_c01` / `multi_c02`；`reissue_c01`（Success+issues 双维度） |
| Generic Exception MSB guard | `p16_requestFunctionWithExceptionBitIsNeverAnException`、`p04_genericExceptionFc08` |
| FC06 / Function16 passive echo | `p02`/`p03`、`c06_startAddressMismatch`/`c07_quantityWrittenMismatch`/`c01`、`f16_u01~u11`、`c08_genericException090` |
| per-record continuation | `p06_invalidRecordDoesNotPoisonBatch`、`c11_invalidDoesNotPoison` |
| Serial 主动 FC03 单事务 / Busy / timeout 双路 / partial wire-truth | `test_serial_session.cpp`：`a01_startFc03`…`a16_wrongByteCount`（16 个） |
| Serial stale completion guard / source switching atomic | `test_ui_bridge.cpp`：`s10_staleCompletionGuard`、`s02`/`r03`/`r07` 族 |
| Success-rate 公式 / 统计正交 | `b10`、`b09_issuePresenceDoesNotChangeSnapshot`、`p14_mixedBatchStatistics`、`c16_mixedFunctionStatistics` |
| Baseline finding 族/顺序/Healthy 五条件 | `test_diagnosis.cpp`：`a01`…`a15` |
| AI authority / 属性纪律（fake provider） | `test_ai_client.cpp`：`b01`…`b21` |
| Agent 三工具只读 / 白名单 / 上限 | `test_agent_tools.cpp`：`a01`…`a13`；`test_agent_runtime.cpp` B 系列；`test_ui_bridge.cpp` UI-AG 族 |
| demo_v1 golden 口径（Simulator↔Replay 相等） | `test_replay_analysis.cpp`：`i01_goldenReplay`、`i02_determinism`；`r01`/`r02`；PASSIVE `p01_fc03GoldenUnchanged` |
| QML 加载 / 部署 | ctest 目标 `qml_smoke`；`scripts/deploy_windows.bat` + minimal-PATH 冒烟（Manual 记录） |
| 人工视觉验收（不可自动化部分） | PROJECT_STATUS 变更记录中的 Manual UI Review PASS 链（T008/T009/T010/T011/T013/T014/T015 各阶段） |

## 3. V2 Non-Regression Rules（每个 V2 task 固定门禁）

1. **Before implementation**：preflight（`git status`/LKGC 核验）；列出受影响 V1 contracts（对照 §1 冻结契约清单）。
2. **After implementation**：targeted tests → **full ctest（当前 24/24 基线）** → `git diff --check` → 确认无关 V1 行为未变。
3. 涉及 **QML**：另跑 QML smoke + **Manual visual review**（对照 V1 visual baseline）。
4. 涉及 **Serial**：另跑 serial 回归 + stale-result/source-switching 回归（`s10`/`r03`/`r07` 族）。
5. 涉及 **AI**：确定性事实 authority 断言必须保持（`b01`/`b14`/`b17`/`b21` 族语义不得弱化）。
6. 涉及 **write operation**：必须 explicit user action + 明确参数 + 明确确认；失败绝不伪装成成功；**Agent 不得自主执行写操作**。
7. 任何回归**不得以删除旧测试解决**；旧测试的语义变更必须先经 Review 且记录理由。

## 4. V2 Roadmap（只规划，未实现）

### M9 — UI / UX Refresh（下一里程碑）
- **M9-A UI Foundation / Design System**：design tokens（颜色/字号/padding/radius）与 reusable QML components 抽取；消灭重复样式。
- **M9-B Application Shell & Navigation**：统一 Workspace / Navigation（概念候选：Dashboard / Communication / Replay / Diagnosis / Device）；**先读现有 Main.qml 真实导航**再定方案，不假定需要大规模重写。
- **M9-C Dashboard Redesign**、**M9-D Transaction & Diagnosis Workspace**：内容工作区改造。
- **M9-E Branding / Window Icon / Packaging**。
- **M9-F Manual Visual Acceptance**：与 V1 visual baseline 对比。
- Guardrails：默认**保留原生窗口 chrome**（当前不决定 frameless custom title bar，除非调研证明收益明显且风险可控）；不得为 UI 改动改变任何 deterministic 行为。

### M10 — Active Modbus Master v1
- 能力：FC03 active read；FC06 active write single register；Function 0x10 active write multiple registers。
- 要求：**复用当前 production encoder/parser/analyzer，不得另写一套协议判断**；写操作=用户显式动作 + 明确参数 + 明确确认 + 失败不伪装成功；**Agent 不得自动执行写操作**。

### M11 — Register Readout & Decode
- FC03 successful response → raw register values → Hex / Binary / UInt16 / Int16 / UInt32 / Int32 / Float32（含 byte/word order）。
- 必须区分：**raw deterministic register data** 与 **device-specific physical semantics**。

### M12 — Device Profile & Manual Intelligence
- **M12-A Device Profile Schema**；**M12-B Profile Editor**；**M12-C Manual Import + AI Extraction**（支持 PDF / DOCX / TXT / Markdown；PDF 扫描件 OCR 仅作后续能力，除非调研证明低成本可靠可做）。
- AI 输出 = **Device Profile Candidate，不是 verified truth**：至少含 candidate value / evidence / source page-section（若有）/ confidence-uncertainty / confirmation state；用户 **Accept / Edit / Reject**，仅用户确认后进入 verified Device Profile。
- **M12-D Manual Q&A**：回答必须基于用户上传的说明书证据；找不到时明确 `Not found / Insufficient evidence`。
- AI 原则扩展：**AI is extractor/assistant, not authority。**

## 5. V1 Visual Baseline

- 现存 V1 最终 UI 无仓库内产品截图；在 M9 开始前请用户提供真实截图（当前窗口尺寸、主要 workspace、Simulator demo 状态各一张）作为对比基线。**本 Gate 不创建、不伪造截图。**

## 6. Git / Tag 分析（只建议，不执行）

- 建议 **V1 tag 指向 `ae067ab`（verified product LKGC）**，而非当前 HEAD：HEAD 在 LKGC 之上叠加的全部为 docs/资产/复现工具提交，产品行为基线在 `ae067ab` 才被全链验证。
- **不自行创建 tag**；等待人工确认后再 `git tag`。
- 本 Gate 内不得 push；`origin/main` 已与 GitHub 同步，后续是否 push 遵循既有 push 策略。

## 7. 冻结清单变更纪律

- 任何 M9~M12 任务若要偏离上述冻结契约，必须先在新 ADR 中提案并经 Review——**静默 redefine 一律禁止**。

## 8. V2 Task Execution Protocol（每个 V2 子任务的强制结构）

```
 1. Goal
 2. Background / User Value
 3. V1 Contracts at Risk
 4. Knowledge Before Implementation
 5. Technical Design
 6. Alternatives / Why This Design
 7. Files Expected to Change
 8. Test / Acceptance Plan
 9. Implementation
10. Problems Encountered
11. Root Cause Analysis
12. Solution
13. Targeted Verification
14. Full Regression
15. Manual Acceptance（如适用）
16. Result
17. Knowledge Learned
18. Potential Interview Questions
19. Git Commit
20. LKGC Decision
```

### Learning Gate（含新知识任务的强制前置）
A 问题 → B 现有代码机制 → C 新增知识 → D 最少必懂概念 → E 设计理由 → F V1 风险面 → G 测试方案；**Review 批准前不得进入 Implementation。**

### Knowledge Ownership 纪律
每个 V2 task 的 Knowledge Learned 必须回答“**通过项目中的哪件真实事情理解了它**”，并只能基于当时真实实现与测试记录。反例：“学习了 QML / 学习了 Modbus”；正例风格：“在 M10 中为 Active Master 增加 FC06，通过请求/正常响应的地址与寄存器值回显校验，理解了 FC06 的成功响应不是单独的 ACK 而是对请求关键字段的 echo，因此 TransactionAnalysis 可以复用现有 WriteSingleRegisterEchoMismatch 语义。”

### Debug / Issue Trace 纪律
失败必须留痕：Observed / Expected / Evidence / Root Cause / Fix / Verification / Regression Protection；严重问题并入 `docs/issues/`（AGENTS.md 既有规范）并关联任务档案。

### LKGC 纪律（V2 沿用）
HEAD ≠ verified LKGC；docs-only commit 不推进 LKGC；code/test candidate 先过自动验证；存在自动测试覆盖不了的 UI/hardware/live 交互时，人工验收前不得推进最终 LKGC；每次推进必须有明确 Review 记录。V1 tag 建议目标仍为 `ae067ab`（verified product LKGC），等待人工确认后执行，本 Gate 不自行创建。
