# T019 — M9-D Transaction & Diagnosis Workspace

> **本文件是 M9-D 的 canonical task document（2026-09-18 起）。**
> 任务归属：`docs/BACKLOG.md` **未登记 T019** ⇒ 按仓库真实下一个空闲 task id 取 **T019**（T018 已用于 M9-C，且 M9-D 不并入 T018）。T018 只作为 **M9-C 历史证据**引用（§C6 边界与登记项）。
> Phase 1 = Learning / Design Gate（docs-only，**Implementation = NOT STARTED**）。

- **Goal** … 把 Legacy 里最后的 **Transactions presentation** 提取为正式 workspace，与既有 **Diagnosis** workspace 形成"证据 → 解释"的工作流边界；同时给 **Legacy 的最终命运**一个基于真实证据的决定。全程不得重定义任何 V1 契约，不得把两者合成"万能故障页"。
- **Background / User Value** … M9-B 把 Dashboard/Communication/Replay/Diagnosis 全部抽出后，Legacy 只剩 **StatisticsOverview + Transactions**. M9-C 又把 Dashboard 的统计呈现做成 direct composition，并把 "recent transactions" 明确 **DEFER 到 M9-D** 重新裁定。工程师目前看事务证据只能去"工作台"，而诊断解释在"诊断"页——两者之间没有正式的工作流边界。
- **Technical Decisions** … 见 §5–§33（IA 采纳 C、Legacy 退役采纳 A（D5 单阶段执行）、selection = page-local、detail 只展示既有字段、filters DEFER）。
- **Implementation** … 未开始（分阶段计划见 §33）。
- **Files Changed** … 本轮 docs-only（见 §36）。
- **Problems Encountered / Solutions / RCA** … 见 §35 与各阶段的 Problems/RCA（实施时补齐）。
- **Verification** … 计划见 §30（实施时以真实命令与输出补齐）。
- **Result** … Phase 1 设计完成；实施未开始。
- **Knowledge Learned** … 见 §34。
- **Potential Interview Questions** … 见 §34。
- **Git Commit** … 见 §37。

---

## 0. V2 Task Execution Protocol 对应

| 步骤 | 本任务落点 |
| --- | --- |
| Preflight | §1 |
| V1 Contract Review | §4（模型能力审计）+ §11–§13（issues/ENR/Unsupported 冻结） |
| Learning / Design | 本文档（§2–§34） |
| Test Plan | §30 |
| Implementation | D1–D6（§33），逐阶段独立 Review/提交 |
| Targeted / Full Verification | §30 门禁 + 各阶段记录 |
| Manual Review | §32（提前设计，实施后执行） |
| Knowledge Ownership | §34 |
| Git / LKGC | 每阶段独立提交；LKGC 仅在人工验收通过后按 Git tree classification 裁定 |

## 1. Preflight（2026-09-18）

```text
branch = main；HEAD = 5b1879e；working tree clean；git diff --check PASS
V2 verified LKGC = bc754be；v1.0.0^{commit} = ae067ab（annotated tag 对象 2cee626）
origin/main = a40d935；ahead 58 / behind 0
```

## F0. 强制重读履行记录

实读：`PROJECT_STATUS`、`BACKLOG`、`docs/02_ARCHITECTURE.md`（D1–D7 与依赖方向）、`docs/04_TEST_STRATEGY.md`、`docs/06_UI_LANGUAGE_POLICY.md`、`docs/11_V2_UPGRADE_PLAN.md`（§1 冻结契约、§3 门禁、§4 roadmap、§8 协议）；T017（§14 响应预算、§30 B2 边界与 defer 表、§37.7 surplus-space、§50 M9-B closure）；T018（§52 Phase 1 IA、§C1–C6 全部）；真实 QML：`Main.qml`（Legacy 全文含事务 pane 393 行区间）、`DiagnosisPage.qml`、`DashboardPage.qml`、`NavigationRail.qml`、四个统计组件；真实 C++：`AnalysisController.h/.cpp`（Q_PROPERTY 全表、composeIssueText、replay/demo/serial 三条 entries 路径）、`TransactionListModel.h/.cpp`（8 roles 与两个 formatter）、`TransactionAnalysis.h`；harness 四套。**未依据聊天摘要决定页面结构。**

## 2. Current Legacy Inventory（完整，逐项）

`Main.qml` 的 `legacyWorkspace`（StackLayout child 0）今天只有两项内容：

| # | UI element | owner | authoritative source | action | geometry role | current tests | 迁移期必要性 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `StatisticsOverview { instanceId: "legacy" }`（含 `statisticsHeader_legacy` / `statisticsPanel_legacy` / rows / 11 卡） | Main.qml | Controller 全 11 项统计 + 两个 availability | — | natural（168 panel） | 几何：panel/rows/卡非零、row2 不重叠 row1；nav 场景 A/D 经快照 | **已无**（Dashboard 有直接组合的同一批数据） |
| 2 | 分隔线 Rectangle（h=1, "#D0D0D0"） | Main.qml | — | — | natural | 无 | 无 |
| 3 | `transactionsPane`（`legacyTransactionsPane`）：标题「最近通信记录」18px + 固定表头 5 列 + `ListView`（`analysisController.transactionModel`，delegate 36/64px、issue 次行）+ 空态「暂无通信记录」 | Main.qml | `TransactionListModel`（8 roles） | 只读（无命令） | `Layout.fillWidth/Height`，`Layout.minimumWidth: 520`；列宽由 `tableUsableWidth` 单一 owner 派生（15/15/20/18%/剩余，min 64/60/96/84/96） | 几何：pane 非零/在列内/在统计块之下（ISSUE-004 守卫）；nav 场景 A（observed=4）/D（清零）/J/K/K′（replay 会话）经 model 计数 | **无**（M9-D 迁移目标） |

**结论（回答 §2 的问题）**：M9-B5 之后 Legacy **确实只剩 Statistics + Transactions**，**没有任何其它独有能力**（Run Demo/Load Replay/Serial/Diagnosis/clear 均已迁出；AppBar 承载 session 与 Clear Results）。唯一"独有"的是**同页聚合**（统计与事务同屏），而 Dashboard（统计）与新 Transactions workspace（事务）将分别覆盖两者。

## 3. Current Diagnosis Inventory（`DiagnosisPage.qml`）

| 维度 | 事实 |
| --- | --- |
| authoritative state | baseline（`hasBaselineDiagnosis`/`baselineDiagnosisText`）、AI（`aiConfigured/busy/hasAiDiagnosis/text/error`）、Agent（`agentBusy/hasAgentAnswer/agentAnswerText/agentErrorText/agentAvailable`）、`cloudAiBusy`（= AI busy ∨ Agent busy，双向 single-flight）——**全部在 Controller** |
| page-local state | `diagnosisTabs.currentIndex`、`agentQuestionInput.text`（草稿）、三个 Flickable 滚动位置 |
| actions | 运行基线诊断 / 清除诊断 / 生成 AI 解释 / 取消 / 询问 Agent / 取消（全部 `onClicked`，零生命周期钩子） |
| async lifecycle | 二维守卫（generation × batchRevision）在 Controller；切页≠cancel（B5.3 Scenario L/N 证明） |
| scrolling | 三个 bounded Flickable（`clip:true` + `Layout.fillHeight` + `minimumHeight:0`） |
| states | 空态（"尚未运行基线诊断"/"尚未生成 AI 解释"）、error、result（PlainText、wrap、lineHeight 1.35） |
| geometry/test | 页体/header/tabs/tabContent/选中 tab 的控件与视口/三段几何 + tab sweep（B5.3 10 趟起） |

**重申**：Diagnosis **已完成 workspace extraction**。M9-D **不是**重新搬 Diagnosis，而是研究 **transaction evidence ↔ diagnosis interpretation** 的工作流边界（§14）。

## 4. Current Transaction Model Audit（`TransactionListModel`，实读）

**Entry 字段（adapter 层）**：`deviceAddress:int`、`functionCode:int`、`status:TransactionStatus`、`elapsedMs:qint64`、`exceptionCode:optional<uint8_t>`、`issueText:QString`。

| Role（QML 名） | meaning | source | deterministic? | 当前 UI 显示 | M9-D 有用? | 需要新 backend? |
| --- | --- | --- | --- | --- | --- | --- |
| `deviceAddress` | 从站地址（**wire/协议事实**） | request 帧 | ✅ | 「设备 N」列 | ✅ 行 + detail | 否 |
| `functionCode` | 功能码（**wire 事实**） | request 帧 | ✅ | `0xNN` 列 | ✅ 行 + detail | 否 |
| `statusCode` | `TransactionStatus` 七值（**事务结论**） | Core `TransactionAnalysis.status` | ✅ | 未用（QML 未绑定） | ✅ 可用于状态着色/过滤（若要） | 否 |
| `statusText` | 状态中文（adapter 映射，含"预期无响应"） | `statusText()` | ✅ | 「状态」列 | ✅ | 否 |
| `elapsedMs` | 耗时（**测量事实**） | analysis.elapsed | ✅ | 「耗时」列 | ✅ | 否 |
| `hasExceptionCode`/`exceptionCode` | 异常码（hasX/value 模式；absent 时占位 0，**必须先用 hasX 判断**） | analysis.exceptionCode | ✅ | 「异常码 0xNN / —」列 | ✅ | 否 |
| `issueText` | **composeIssueText(响应侧 T014 issue, 请求侧 T015 issues)**：两部分以「；」连接、请求侧按 **Core 顺序**、多 issue 以「；」join；无 issue 时为空串 | Core issues → adapter formatter | ✅ | ProtocolError/请求 issue 行的次行（11px、最多 2 行、wrap+elide） | ✅（detail 可**不截断**展示） | 否 |

**关键缺口（必须冻结，防止 M9-D 凭空发明）**：

1. **没有 raw/hex 帧字节**——`TransactionAnalysis` 只存 status/elapsed/exceptionCode/issue（`grep` 证实零 bytes 字段）；`TransactionListEntry` 同样没有。⇒ **不得**做"原始报文/HEX 视图"。
2. **没有结构化的 request/response 分轴**——`issueText` 是**合成后的单一字符串**；QML 无法把"响应侧 issue"与"请求侧 issue"分开呈现（需要 adapter 新 role）。
3. **没有请求参数（起始地址/数量）的结构化字段**——它们只出现在 `issueText` 的文案里。
4. **unsupported replay records 不进入 model**（`entries` 只由 `batch.transactions` 构建；unsupported → `replayNoticeText` 披露）⇒ 不得为"表格完整"伪造行。
5. Model 是**整批替换**（`setEntries`），无 proxy、无 filter；`ListView` 走 delegate ⇒ **天然虚拟化**。

**四轴分类（M9-D 必须保持正交）**：wire/protocol 事实（deviceAddress/functionCode）· 事务结论（statusCode/statusText/elapsedMs）· issue 事实（issueText；response-issue 与 request-issue **同轴呈现**）· diagnosis interpretation（**不在 model**，属 Diagnosis workspace）。

## 5. User Task Definition

| 候选任务 | 价值 | 现有 deterministic 支撑 | 归属 |
| --- | --- | --- | --- |
| **A. 快速浏览当前 session 的 transaction evidence** | 高 | model 8 roles + 表 + 虚拟化 | **Transactions** |
| **B. 找到异常 transaction** | 高 | status/issueText（**无 filter** 时靠扫描与状态色） | **Transactions** |
| **C. 理解某条 transaction 为什么异常** | 中高 | `issueText`（确定性观察事实，**非根因**） | **Transactions（证据层）**；更深的解释属 Diagnosis |
| **D. 从 transaction evidence 进入 deterministic diagnosis context** | 中 | `hasBaselineDiagnosis`（batch 级） | **边界（文本线索）** |
| **E. 查看 AI/Agent interpretation** | 中 | AI/Agent 属 DiagnosisPage | **Diagnosis（不变）** |

**优先级：A > B > C > D > E。**

**边界（推荐并采纳）**：**Transactions = evidence inspection**（发生了什么）；**Diagnosis = batch/session interpretation**（意味着什么）。**不合成"万能故障页"**。

## 6. M9-D Scope Alternatives（三案）

| 维度 | A. Separate Workspaces（Transactions 独立 + Legacy 退役） | B. Unified Analysis Workspace（表 + detail + Diagnosis 同页） | **C. Transactions workspace + 既有 Diagnosis（推荐）** |
| --- | --- | --- | --- |
| 结构 | 新 Transactions workspace；Legacy 退役 | 单页含三块 | 新 Transactions workspace；DiagnosisPage **零结构变化**；非命令式线索连接 |
| 用户任务覆盖 | A/B/C | A–E | A/B/C + D（文本线索） |
| 状态 ownership 风险 | 低 | **高**（batch baseline 被错误绑到 selected row 的诱惑） | 低 |
| V1 语义风险 | 低 | 中高 | 低 |
| 1000×700 密度 | 可控 | 挤压（表+detail+三 Tab 同页） | 可控（表 + detail 两件） |
| Navigation churn | 中（含 Legacy 退役） | 中 | 中（含 Legacy 退役，与 A 同） |
| Legacy 重复 | 消除 | 消除 | 消除 |
| 与 M10/M11 重叠 | 无 | 无 | 无 |
| 可测性 | 好 | 差（耦合面大） | 好 |
| 可回滚性 | 好（分阶段） | 差 | 好 |

**结论：C**。理由：B 把"逐条证据"与"批次解释"塞进同一页，最容易催生"点一行就变成单条诊断"的语义漂移（§14 明令禁止）；A 与 C 的差别只在 Legacy 处置，而 C 保留了"Diagnosis 页零改动"这一已被人工验收的成果。

## 7. Legacy Retirement Decision（Phase 1 P0）

**事实基础**：Legacy 仅剩 Statistics（Dashboard 已直接组合同一批数据）+ Transactions（M9-D 迁移目标）；`StatisticsOverview` 的 consumer 在 Dashboard 于 C3 改直连后**只剩 Legacy 一个**；harness 中 `legacyIndex/legacyWorkspace` 出现 **29 处**（退役 churn 的真实度量）。

| 方案 | 用户价值 | 重复 UI | 测试成本 | index churn | rollback | M9-F 清理 |
| --- | --- | --- | --- | --- | --- | --- |
| **A. M9-D 完成后退役 nav entry（采纳，D5 单阶段）** | 消除"哪个视图才是正式"的歧义 | **消除** | 一次性机械重指（29 处引用 + 场景 index 0 语义） | **0**（用新 workspace **替换 index 0**，rail 仍 6 项、设备仍 disabled@5） | 单提交 revert（Legacy QML 只在该提交删除） | 无需 |
| B. 保留 Legacy 作为兼容总览 | 低（同页聚合已由两处更好覆盖） | **长期保留**（统计双实例 + 事务双呈现） | 低（不动） | 0 | 无 | 需 M9-F 处理 |
| C. 保留但隐藏/开发者入口 | 极低（用户看不到） | 保留（代码与测试仍在） | 中（隐藏语义 + 入口守卫） | 中（额外入口约定） | 无 | 需 M9-F 决策 |
| D. 延后到 M9-F | 无变化 | 保留至 M9-F | 推迟（churn 进入验收里程碑） | 中（M9-D 需先加 index，M9-F 再收） | 无 | 全压 M9-F |

**结论：A**，**但严格按证据而非"终于能删"**：用户价值论证 = Legacy 无独有能力；重复论证 = 保留即长期双呈现（B2 曾明确拒绝"事务表双呈现"）；churn 论证 = **替换 index 0 而非新增 index**，rail 形状与设备 disabled 位**完全不变**；风险控制 = 退役在 **D5 单阶段**执行，且**必须发生在 Transactions workspace 完全验证之后**（D2–D4 期间 Legacy 保持完整，不制造能力真空；此迁移窗口内的双呈现与 B2–B4 统计双实例同性质，且**逐阶段可 revert**）。

## 8. Navigation Contract / Index 设计

**现状**：rail 6 项 —— 工作台(0)/总览(1)/通信(2)/回放(3)/诊断(4)/设备(5, disabled)。

**设计（采纳"新增 → 替换"两段式，最终形状与今天一致）**：

| 阶段 | rail | 说明 |
| --- | --- | --- |
| D1（shell） | 工作台(0)/总览(1)/通信(2)/回放(3)/诊断(4)/**事务(5, disabled)**/设备(6, disabled) | 空页不可达（B5.1 先例）；契约属性 `workspaceTransactionsIndex = 5`、`workspaceDeviceIndex = 6` |
| D2（原子迁移+启位） | 事务(5) **enabled** | 表在 Legacy 与新页**同一提交**完成迁移+启位（禁止"已迁移但不可达"）；Legacy 事务 pane 同提交移除（Legacy = 统计）|
| D5（退役） | **事务(0)**/总览(1)/通信(2)/回放(3)/诊断(4)/设备(5, disabled) | 删除 Legacy entry 与页面；index 紧凑重排（5→0 等）；rail 恢复 6 项、设备回 disabled@5 |

**契约属性**（集中声明，测试与 QML 共读）：`workspaceLegacyIndex`（D5 后删除）→ `workspaceTransactionsIndex`（D5 后 = 0）、`workspaceDashboardIndex/CommunicationIndex/ReplayIndex/DiagnosisIndex/DeviceIndex`。

**Navigation 只能改变 presentation index** ✓ 不得 select transaction / run diagnosis / clear state / load Replay / switch source（B 系列不变量继续由 nav check 断言）。

## 9. Transaction Selection Ownership

| 方案 | 判定 |
| --- | --- |
| **A. page-local selected row（采纳）** | 唯一消费者是本页的 detail pane；无其它 subsystem 需要 authoritative reference；`ListView.currentIndex` 天然是呈现态 |
| B. Controller authoritative selection | ❌ 会把呈现态塞进事实层（M9-C 已冻结"Controller=事实、页面=呈现"） |
| C. model selection state | ❌ 污染 `QAbstractListModel`（它只做整批替换的数据面） |

**navigation away/back**：StackLayout 常驻 ⇒ **selection 自然保留**（与 B5.3 的 tab/草稿同机制），并作为未来 Scenario 的可测点；**不写回 Controller**。**不得**为 detail pane 把 selection 塞进 `AnalysisController`。

## 10. Transaction Detail Boundary（逐项核验 backend）

| detail field | existing source | safe now? | 说明 |
| --- | --- | --- | --- |
| 设备地址 | `deviceAddress` role | ✅ | 行内同源 |
| 功能码 | `functionCode` role（`0xNN`） | ✅ | 行内同源 |
| 状态 | `statusText`（+可选 `statusCode` 着色） | ✅ | **逐一显示六种 outcome，不得合并为"错误"** |
| 耗时 | `elapsedMs` + " ms" | ✅ | 行内同源 |
| 异常码 | `hasExceptionCode` → `exceptionCode`（`0xNN`） | ✅ | 必须先用 hasX 判断（absent 显示 "—"） |
| 确定性详情 | `issueText`（完整、**不截断**） | ✅ | detail 的核心增量价值 |
| 起始地址 / 数量 / 请求参数字段 | **不存在**（仅在 issueText 文案里） | ❌ 本轮 | 结构化需要 adapter 新 role → DEFER |
| request-issue 与 response-issue **分轴**展示 | **不存在**（合成字符串） | ❌ 本轮 | 需要 adapter 新 role → DEFER |
| raw / hex 报文 | **不存在任何字节** | ❌ **REJECT** | 不得发明假报文本 |

## 11. Request / Response Issue Presentation（冻结）

- V1 事实：**响应侧 `TransactionIssue`（9 业务值 + 哨兵）** 与 **请求侧 `TransactionRequestIssue`（4 值）** 都是**独立于 `TransactionStatus` 的正交轴**；requestIssues 为**有序 collection**（顺序语义冻结，adapter 不重排）。
- 当前 UI 通道：两者经 `composeIssueText` 合成为**一行次行文本**（§4 表）。
- **M9-D 必须保持 outcome 轴与 issue 轴正交**：状态列/状态字段只表达 `TransactionStatus`；issue 文本/详情只表达 issue 事实。**不得把 issue 偷偷改写 status**（例如"有 issue ⇒ 显示为失败"），也不得把 issue 折进状态色。`Success + InvalidRequestByteCount` 这类组合**允许同时出现**且必须如实呈现。

## 12. ExpectedNoResponse Presentation（延续 M9-C 冻结）

`ExpectedNoResponse` = **completed outcome**、**不是 anomaly**、**不代表写成功**。M9-D 的行与 detail **延续中性语义**：文案保持「预期无响应」（`statusText` 冻结映射），**不得**显示"成功写入/失败/timeout/warning"，**不得**以异常色编码（除非真实其它事实支持——例如该行同时带 request issue，那是 issue 轴的事）。状态色若启用，使用 `DS.expectedNoResponse`（中性青灰），与 M9-C 的 outcome 卡一致。

## 13. Unsupported Replay Boundary（冻结）

- Replay 契约：**Unsupported ≠ ProtocolError**；unsupported records 是显式事实、**不进统计**。
- **实测**：unsupported records **不进入 `TransactionListModel`**（`entries` 仅由 `batch.transactions` 构建；披露走 `replayNoticeText`）⇒ **M9-D 不得为"表格完整"伪造 transaction 行**。
- **B4 notice lifecycle 不得破坏**：`replayNoticeText` 的归属仍是 Replay workspace（M9-D 不复制、不清除、不改语义）；若未来本页要显示披露，属**同权威第二呈现**，须单独设计（本轮 DEFER）。

## 14. Diagnosis Relationship（P0 边界）

**Transaction selection 是否应直接驱动 Diagnosis？默认否。**

理由：**Baseline Diagnosis 是 batch/session 级**（它消费 `activeDiagnosisTransactions_` 全批 + `activeBatchRevision_`），点一行**不能**把它悄悄变成 single-transaction diagnosis。

| 方案 | 判定 |
| --- | --- |
| A. 完全独立 | ⚠️ 可行但少了可发现的边界线索 |
| **B. 仅提供"当前 session 诊断可用"文本线索（采纳）** | 在 Transactions 页显示一行**存在性线索**（读 `hasBaselineDiagnosis`），复用 M9-C 的冻结文案语义（"尚未运行基线诊断。" / "已有基线诊断结果，可在诊断工作区查看。"）——**纯文本、非命令、不自动导航、不触发任何命令** |
| C. 未来新增 selected-transaction diagnosis | **明确 DEFER → 新 milestone**（需要新的 Core/Controller 能力，不是 UI redesign） |

**不得在 M9-D 改变 Baseline semantics**（Batch 级、确定性、只读诊断事实）。

## 15. AI / Agent Boundary

AI/Agent **继续属于 DiagnosisPage**。Transactions 页**默认不得**复制：AI 输出、Agent 回答、问题输入框、provider 配置。
若考虑"用 AI 解释所选 transaction"——那是**新 capability**（新 prompt 通道 + 新身份守卫），**不是视觉 redesign** ⇒ **明确 DEFER**（并不得在 M9-D 顺手实现）。

## 16. Recent Transactions from M9-C（重新裁定）

M9-C 曾把 Dashboard 的 recent-anomaly 预览 **DEFER 到 M9-D 重新裁定**。**本轮裁定：不实现（NO）**。

- 正式 Transactions workspace 已解决"浏览/定位"任务；Dashboard 再放一份预览 = **重复呈现 + 同步成本**，且需要 proxy/过滤机制（§24/§25 属 behavior-bearing adapter 工作）。
- 用户价值论证：Dashboard 的任务是**当前会话态势**（M9-C 冻结），逐笔浏览有专门 workspace ⇒ 额外价值 < 成本。
- **该 deferred 项自此关闭为 "superseded by the Transactions workspace"**（不是 implemented，也不是遗留未决）。

## 17. Transaction Table IA

**现状**：5 列（设备/功能码/状态/耗时/异常码）+ issue 次行（36/64px 行高）；列宽由 `tableUsableWidth`（= pane 宽 − 24）单一 owner 派生：15%/15%/20%/18%/剩余，min 64/60/96/84/96。

**1000×700 真实预算**（新 workspace 页内容宽 ≈ 911；表内可用 ≈ 887）：列 mins 合计 **400 ≤ 887** ⇒ 余量充足；1024×720 更宽。

| 方案 | 判定 |
| --- | --- |
| **A. 现有 5 列精炼（采纳，列集不变）** | 保留可扫描性；列宽 owner 机制与断言可原样迁移 |
| **B. 主表减列 + detail pane（采纳，与 A 组合）** | 详情搬进 detail，**主表列集不减**（现有 5 列已在预算内，减列反而降低扫描性） |
| C. 响应式列隐藏 | ❌ 本预算下无必要；隐藏会破坏"列几何单一 owner"的稳定性 |
| D. horizontal scroll | ❌ 破坏扫描（本项目 5 列在 1000 宽下本就放得下） |

**行高/虚拟化/滚动**：沿用 36/64 + `ListView`（虚拟化）；secondary line 保持 11px、最多 2 行 wrap+elide。

## 18. Master-detail Layout（含 1000×700 预算）

| 方案 | 1000×700 预算 | 判定 |
| --- | --- | --- |
| **A. vertical split：表上 / detail 下（采纳）** | 内容 ≈ 911×627：header ~34 + 表（flex，min ~260）+ detail（**固定 ~200**）+ spacing；表可显示 ≥6 行 | ✅ 表保持**全宽**（列 owner 与既有断言零改动）；长 issue 文本全宽可读；**与 T017 §14 已记录的"M10 并行预算不足则改上方/下方堆叠区"一致** |
| B. horizontal split：表左 / detail 右 | 表 min 520 + detail ~320 + 间距 ≈ 852 ≤ 911 ✓（勉强） | ⚠️ 可行但把表压到 520~560（接近 Legacy 最小值），且与 M10 Request Builder 的未来横向布局争空间 |
| C. drawer / expandable row | — | ❌ 交互新颖度高、滚动与焦点复杂，收益低于固定 detail |
| D. no detail pane | — | ⚠️ 保底方案（若 Review 认为 detail 价值不足）：表单独成立 |

**采纳 A**；detail 为固定高度区域（**不参与 surplus 分配**），页面 surplus 由表区吸收（表是唯一的弹性件）。

## 19. Diagnosis Layout Re-evaluation

**裁定：A. 零结构变化**（若 Review 要求视觉打磨 → 转 B，但需单独证据）。
理由：B5 刚以人工 PASS 交付 DiagnosisPage；milestone 名字含 "Diagnosis" **不构成**重构理由。M9-D 与 Diagnosis 的**唯一接触点** = §14 的**文本线索**（写在 Transactions 页，不改 Diagnosis 页）。**不做 C（侵入式联动）、不做 D（大幅 redesign）。**

## 20. Statistics Placement（Legacy 退役后）

事实：Dashboard 于 C3 改为 direct composition 后，`StatisticsOverview` 的 consumer **只剩 Legacy**；D5 退役后 consumer = **0**（`StatisticsMetrics`/`StatisticsOutcomes` 仍被 Dashboard 使用）。

**裁定**：**M9-D 不删除 `StatisticsOverview`**（不得因 consumer 数下降顺手删）；D5 后它成为**未被引用的兼容 wrapper**，登记 BACKLOG 条目 "StatisticsOverview 去留裁定"（候选：未来 milestone 删除；或若出现第二个需要"标题+卡"整体组合的页面则保留）。**判定依据留待真实 ownership/回归价值评估**，不在 M9-D 提前定论。

## 21. Empty State

Transactions workspace：`observed == 0` ⇒ 一行文本（示例，最终按 UI 语言政策定稿）：**"当前没有通信记录。可运行演示批次，或前往「通信」「回放」工作区获取数据。"** —— 纯文本指引（**≠ navigation command**）、rail 仍是导航 authority；**不得**隐式 connect/load/run。Diagnosis 空态**保持既有语义**（"尚未运行基线诊断"/"尚未生成 AI 解释"）。

## 22. Status Color Semantics

- 复用既有 DS 状态色（`success/exception/crcError/timeout/protocolError/expectedNoResponse/pending`）；**颜色只是辅助通道**——行内**必须同时有文字 status**（现状已知足）。
- **不得**建立 severity score / health score；**不得**把 Exception/CRC/Timeout/ProtocolError 一律涂成同一种"红色错误"而丢掉可扫描差异（每类沿用其 DS token，与 M9-C 的 outcome 卡一致）。
- `ExpectedNoResponse` 使用中性色（§12）。

## 23. Accessibility

- **表格键盘导航**：`ListView` 可聚焦（`focus: true` + `keyNavigationEnabled`），Up/Down/Home/End 移动 `currentIndex`（= selection）⇒ 键盘可达选中；焦点可视（平台轮廓保留）。
- **焦点顺序**：rail → 页 header → 表 → detail → （线索行）。detail 为只读文本，不抢焦点（除非未来出现可交互元素）。
- **屏幕阅读**：行需要可读标签 —— 行级 `Accessible.name`（例："设备 1，功能码 0x03，状态 成功，耗时 25 毫秒"）或在 detail 提供等价文本；状态**始终是文字**。
- **1000×700 滚动**：表区为唯一滚动容器（`clip + StopAtBounds`），detail 长文按需自身滚动（或固定高度 + elide，二选一在 D3 定稿）。
- **不得因 master-detail 退掉 M9-B accessibility baseline**（rail 键盘、focusPolicy 等）。

## 24. Performance Boundary

- 现状：`ListView` **delegate 虚拟化** ✓；model 为整批替换（`setEntries`），无 proxy。
- 公开 perf 基线含 100k/1M offline analysis ⇒ **UI 不得假设可无成本渲染全部细节**。
- **硬性禁止**：在 QML 对全 model 做 JS `filter/map` 生成 anomaly list（M9-C §52.18 同源结论）。
- **detail pane 只绑定 selected row**（单条数据），不遍历 model。
- 任何需要 proxy/过滤的功能 ⇒ **新的 adapter 决策**（behavior-bearing），Phase 1 只设计不实现（§25）。

## 25. Filtering / Search Decision

| 候选 | 需要什么 | 判定 |
| --- | --- | --- |
| status filter | proxy model（adapter）或 QML 迭代 | **DEFER**（M9-D 先交付诚实的呈现；过滤是新 adapter 工作） |
| anomaly-only filter | 同上 + **必须复用冻结 predicate**：exception/crc/timeout/protocol（**ENR 不是 anomaly**） | **DEFER**（实现时必须复用该 predicate，不得另立一套） |
| function filter | 同上 | **DEFER** |
| text search | 同上 | **DEFER** |

**结论**：M9-D **默认不实现任何过滤/搜索**；若 Review 批准，属 **D4** 且必须走 **QSortFilterProxyModel（adapter 层）**，明确标注 **behavior-bearing**。

## 26–28. 跨里程碑边界

- **vs M10（Active Master）**：M9-D **不得**加入 write editor / register write / active request composer / write confirmation flow；本页只展示**已有 session evidence**。Communication 的既有 FC03 active 行为**保持不变**。
- **vs M11（Register readout）**：**不得**实现 UInt16/Int16/Float32、word order、byte order、寄存器语义解码；只展示当前已有的 raw deterministic 字段（本页实际没有寄存器值可展示）。
- **vs M12（Device Profile / Manual Intelligence）**：**不得**做设备自动识别、profile 卡、手册语义、AI 设备诊断。

## 29. Design Alternatives Scorecard（最终候选 vs 备选）

评分维度（**不使用"更现代"**）：User task fit / State ownership risk / V1 semantic risk / 1000×700 density / Navigation churn / Legacy duplication / M9-D scope fit / M10-M12 overlap / Testability / Rollbackability。

| 维度 | **最终候选（§6-C + §7-A + §18-A + §19-A）** | 备选 B（统一分析页） | 备选 A'（独立页 + 保留 Legacy） | 备选 D'（无 detail） |
| --- | --- | --- | --- | --- |
| User task fit | **高**（A/B/C + D 线索） | 高（A–E） | 高 | 中（C 弱） |
| State ownership risk | **低** | 高（selection↔baseline 诱惑） | 低 | 低 |
| V1 semantic risk | **低** | 中高 | 低 | 低 |
| 1000×700 density | **可接受**（表全宽 + detail 200） | 挤压 | 可接受 | 宽松 |
| Navigation churn | 中（替换 index 0，最终 0） | 中 | 低（不动） | 中 |
| Legacy duplication | **消除** | 消除 | **长期保留** | 消除 |
| M9-D scope fit | **高** | 中（侵入 Diagnosis） | 中（少交付退役） | 中 |
| M10/M11/M12 overlap | **无** | 无 | 无 | 无 |
| Testability | **好**（几何/detail 映射/选择存续可断言） | 差 | 好 | 好 |
| Rollbackability | **好**（D1–D6 分阶段） | 差 | 好 | 好 |

## 30. Verification Plan（未来 implementation）

- 继续全部门禁：build / `--qml-smoke-test` / `--qml-nav-check` / `--qml-geometry-check` / full ctest / `git diff --check`；stderr 卫生（ReferenceError/TypeError/binding loop/NaN/Infinity/required/missing）计数 0；case 标签连续唯一。
- **geometry matrix 按真实 active workspaces × 2 sizes**：D1（7 项，1 个新 shell 不可达）→ D2（7 项全可达，含事务）→ **D5 后回到 5 个 active workspace × 2 = 10 趟 + C4 的 2 趟 targeted**（**不得机械保留"5×2"**，也不得让旧 workspace 的回归消失——每个角色保留自己的趟）。
- **新增断言（至少）**：empty transactions（零行 + 空态文案可见）、demo 4 行（计数 + 每行 roles 映射）、broadcast ENR 行（状态文字「预期无响应」+ **中性色** + 行内无"失败"语义）、protocol-error 行（issue 次行出现 + detail 映射一致）、**selection persistence**（选行 → 五页往返 → currentIndex 保持）、**selected detail mapping**（detail 字段 == 选中行 roles 逐值）、**navigation no side effect**（切页不改变 model 计数/统计/source）。
- 复用既有 oracle 纪律：filename 非 oracle、item identity 对齐、logical tag = 真实 resize、RED 先行。

## 31. Evidence / Screenshot Plan

至少：`Transactions demo @1024×720`、`Transactions demo @1000×700`、`Transaction detail selected`、`ExpectedNoResponse row/detail`、`Diagnosis regression`、`Legacy retirement navigation evidence`（若 D5 执行）。继续 candidate identity / state assertion（计数 + 选中行 roles）/ logical vs pixel 尺寸分别记录 / 自检 integrity+distinctness（+ landscape orientation 契约）。

## 32. Manual Acceptance Plan（提前设计，不执行）

transaction table scanability · 1000×700 · selection（鼠标 + 键盘）· detail mapping · status colors（六类可区分、非单一红）· **ENR neutrality**（不呈 error/失败/成功）· **issue/outcome 正交**（Success+issue 可共存且不改状态）· scrolling · nav persistence（选行跨页存续）· Diagnosis unchanged（或若 Review 批准 polish 则按批准版）· Dashboard/Communication/Replay regression · Legacy retirement behavior（若有）· 空态。

## 33. Implementation Sequencing（每阶段 runnable / testable / rollbackable）

> 不机械采用模板；下述按"先契约后内容、退役最后"排序，且**任何阶段都不得让 Transactions 消失**。

- **D1 — 契约与 shell**：rail 增 `事务`(5, disabled)、`设备`→6；`workspaceTransactionsIndex/DeviceIndex`；`TransactionsPage` 空 shell（不可达）；nav 矩阵 + 几何 dump/case 更新。**零行为变化**。
- **D2 — 原子迁移 + 启位**：事务呈现在**同一提交**迁入 `TransactionsPage`（列 owner、表头、delegate、空态、Pane 几何契约原样）并启用导航；**Legacy 同提交移除事务 pane**（Legacy = 统计）。nav 场景重指（事务流程→index 5）。
- **D3 — Selection + detail**：page-local selection（`ListView.currentIndex`）+ 垂直 split detail（仅既有字段）；键盘导航 + 可读标签；selection 存续断言。
- **D4 — filters（默认不做）**：**仅当 Review 明确批准**才实施；需 proxy model（adapter，behavior-bearing）并复用冻结 predicate。
- **D5 — Legacy 退役**：删除 Legacy entry + 页面；index 紧凑重排（事务→0、设备→5）；harness 29 处引用重指；**单提交、可 revert**。
- **D6 — geometry/evidence/manual candidate**：几何矩阵收口（回到 5×2 + targeted）、截图组、deploy + 严格最小 PATH、人工包。

**每阶段 DoD**：build + smoke + nav + geometry + full ctest + diff-check +（视觉阶段）证据截图；**禁止 big-bang**。

## 34. Knowledge Questions（Phase 1 必答）

1. **Transactions 与 Diagnosis 分别解决什么任务？** → 证据 vs 解释：前者回答"发生了什么"（逐条、确定性观察事实），后者回答"这批意味着什么"（batch 级确定性诊断 + 可选 AI/Agent 解释）；界面边界据此划分（§5）。
2. **为什么 selected transaction 通常是 presentation state？** → 只有本页 detail 消费它；没有任何 backend/其它 subsystem 需要它的权威引用；而 `ListView.currentIndex` 本身就是呈现态。把它写进 Controller 会造成"页面状态污染事实层"，且跨页往返的保留需求由 StackLayout 常驻自然满足（§9）。
3. **M9-D 是否是 Legacy retirement point？** → **是**（采纳 A）：Legacy 在提取后无独有能力、保留即长期双呈现；且以"替换 index 0"实现 ⇒ index churn 为 0。退役放在 D5（内容全部验证之后）以保证可回滚与无能力真空（§7）。
4. **为什么 transaction issues 不能替代 `TransactionStatus`？** → 两者是正交轴：status 是事务结论（七值），issue 是确定性观察细节（响应侧 9 值 / 请求侧 4 值）；`Success + InvalidRequestByteCount` 是合法组合。用 issue 改写 status 会把"观测事实"升级成"结论"，破坏 V1 语义（§11）。
5. **为什么 `ExpectedNoResponse` 不能算 anomaly？** → 它是**响应预期与观测结果的对照**（广播请求本就不期待响应），既不代表失败也不代表设备健康；把它计入异常会制造假告警，也与 M9-C 的 attention/distribution 冻结口径冲突（§12/§25）。
6. **为什么 Baseline Diagnosis 不能因 row selection 变成单条 diagnosis？** → 它是 batch/session 级确定性引擎（消费全批 + batch revision，二维守卫）；"点一行就变单条"会静默重定义其输入与身份，且丢失批次上下文。单条解释若需要，属新能力（新 milestone），不是本页的 redesign（§14）。
7. **为什么 recent transactions 不一定还需要出现在 Dashboard？** → M9-C 冻结 Dashboard=态势感知；逐笔浏览有了正式 workspace 后，Dashboard 的预览就是重复呈现 + 同步成本，且需要 proxy 机制。用户价值 < 成本 ⇒ 关闭该 deferred 项（"superseded"）（§16）。
8. **1000×700 下 master-detail 如何成立？** → 用**垂直 split**：表保持全宽（列 owner 与断言零改动、可显示 ≥6 行），detail 为固定 ~200px 的只读区；这与 T017 §14 已记录的"M10 并行预算不足改上下堆叠"一致，也避免与 M10 的横向布局争空间（§18）。
9. **哪些内容必须留给 M10/M11/M12？** → 写操作与请求编排（M10）、寄存器解码与字/字节序（M11）、设备识别/Profile/手册语义（M12）——本页只展示已有 session evidence（§26–§28）。

## 35. Phase 1 Status / 已知边界

- M9-D **IN PROGRESS — Phase 1 Learning / Design**；**Implementation = NOT STARTED**。
- 已冻结：用户任务与边界、IA 采纳 C、Legacy 退役采纳 A（D5）、selection=page-local、detail 仅既有字段、issues/ENR/Unsupported 语义、Diagnosis 零结构变化、StatisticsOverview 保留、filters DEFER、M10/M11/M12 边界。
- 已知风险：①D5 的 harness 重指（29 处引用）是本任务最大单点工作量，须一次性可验证完成；②detail 的增量价值依赖"不截断 issue 文本"，若 Review 认为不足则退化为 §18-D（无 detail）；③迁移窗口内（D2–D5）Legacy 与新页短暂双呈现（与 B2–B4 统计双实例同性质，逐阶段可 revert）。

## 36. Allowed Changes（本轮）

**docs-only**：`docs/tasks/T019-*.md`（新建）、`docs/BACKLOG.md`、`docs/PROJECT_STATUS.md`、`docs/devlog/`、`docs/INTERVIEW_NOTES.md`。**不得修改**：`src/`、QML、`tests/`、`CMakeLists.txt`、`scripts/`、`samples/`、screenshots。

## 37. Git / LKGC

- 独立 **docs-only** commit（建议信息：`M9-D: design transaction and diagnosis workspace`）；**不 amend `5b1879e`**、不 rebase、不 push。
- **verified LKGC 继续 = `bc754be`**（本 Phase 1 docs-only 不推进）。

## D1 — Transactions Workspace Shell + Navigation / Index Contract（Implementation Record，2026-09-18）

### D1.0 Phase 1 Review = PASS（用户）+ 四条 binding corrections

用户裁定 **M9-D Phase 1 Review = PASS**，并追加以下**约束性更正**（后续阶段必须遵守）：

- **A. Geometry count correction**：**navigation entry 数 ≠ active workspace 数**。
  - **D1**：active = Legacy/Dashboard/Communication/Replay/Diagnosis（5）；disabled = Transactions/Device ⇒ **标准 geometry 仍为 5 active × 2 sizes = 10 passes**。
  - **D2**（Transactions enabled 后）：**6 active × 2 = 12 standard passes**。
  - **D5**（Legacy 退役后）：**5 active × 2 = 10 standard passes**。
  - **disabled 的 Device / disabled 的 Transactions 不建立非零 hidden geometry contract**。
- **B. Selection lifetime**：selected transaction 仍是 **page-local presentation state**，但 **navigation persistence ≠ batch replacement persistence**：
  - **same model/batch + workspace navigation → selection may persist**；
  - **authoritative transaction model replacement/reset → selection/detail 必须 invalidate**；
  - **failed source replacement 且 model/batch 未变 → selection 应保持有效**；
  - **不得**为做到这一点把 selection 搬进 Controller。
- **C. Detail data-access seam**：**D3 之前**必须基于真实 Qt/QML 确定 selected-row roles 如何被 detail pane 读取；**不得默认 `ListView.currentItem` 一定是长期 authority**；**不得**为 detail 新增 `Controller.selectedTransaction*` 类 authoritative properties。若采用 page-local snapshot，必须证明：①只是 presentation copy ②model reset 时清除 ③不成为业务 authority ④不产生 stale detail。
- **D. Legacy reference audit**：**D5 之前**必须把 29 处 legacy harness 引用**分类**——presentation index / geometry target / navigation station / state-persistence station / Legacy-specific regression / Statistics-Transactions oracle——**逐类处理**；**禁止** blind global replace（legacy → transactions）。

### D1.1 Preflight

```text
branch = main；HEAD = 79c6517；working tree clean；git diff --check PASS
V2 verified LKGC = bc754be；v1.0.0^{commit} = ae067ab（annotated tag 对象 2cee626）
origin/main = a40d935；ahead 59 / behind 0
```

### D1.2 Exact Product Scope（本轮只做这些）

1. `TransactionsPage.qml` **shell**（页根契约 + 标题，**零事务内容**）。
2. `workspaceTransactionsIndex = 5` + `workspaceDeviceIndex = 6`（集中契约）。
3. `NavigationRail` 增加 **事务**（**enabled = false**）；**设备** index 5 → 6（**继续 disabled**）。
4. StackLayout 注册 child 5（持久实例化）。
5. 最小 harness 扩展（§D1.9）。

**明确不做**：迁移任何 transaction UI（表/表头/delegate/issue 文本/空态）、启用 Transactions nav、selection、detail pane、filter/search、proxy model、raw/hex、request/response 分轴 role、Diagnosis linkage、AI/Agent、Legacy 退役、StatisticsOverview 删除、M10/M11/M12 能力。

**D1 不可能造成能力消失**：Legacy 仍完整拥有 StatisticsOverview + Transactions（本轮零改动）。

### D1.3 Index Contract（D1 的 presentation indices）

```text
Legacy = 0　Dashboard = 1　Communication = 2　Replay = 3　Diagnosis = 4
Transactions = 5　Device = 6
```

由 `Main.qml` 根的 **presentation-level index contract** 集中声明（`workspaceLegacyIndex` … `workspaceDeviceIndex`）；**active workspace indices 0–4 不变**；唯一变化 = **Device 5 → 6**（它仍 disabled，新的 disabled Transactions 占 5）。

### D1.4 Navigation Entry（disabled 契约）

- 新增 **事务**（`qsTr("事务")`），D1 `enabled = false`；**设备**继续 disabled；两者沿用既有 disabled 呈现（视觉上明确不可用）。
- **disabled Transactions 被点击不得**：改变 `currentWorkspaceIndex` / source / model / selection，也不运行 Diagnosis 或任何 Controller command —— 由既有 disabled-entry guard（`activate()` invoke + index 不变，循环 5..5 → **5..6**）覆盖两个 disabled 条目。

### D1.5 TransactionsPage Shell

`Item` root（页根契约）、`objectName: "transactionsPage"`、`required property var analysisController`、`ColumnLayout(anchors.fill, margins DS.spacingL, spacing DS.spacingM)`、`SectionHeader { objectName: "transactionsPageHeader"; title: qsTr("事务"); subtitle: qsTr("通信记录与事务详情") }`。**零事务内容、零 placeholder card**（D1 不可通过 nav 到达）。

### D1.6 Persistent StackLayout 注册

作为 **child 5** 加入持久 StackLayout ⇒ D2 后沿用稳定 page identity（与 B2–B5 页身份断言同机制）。D1 中 Transactions **disabled** ⇒ 不建立 visible-page user workflow；`navigation 只改变 presentation index` 继续冻结。

### D1.7 Hidden Geometry Rule（D1 边界）

**不要求** hidden Transactions page 有非零 geometry；**不得**直接设 `currentWorkspaceIndex = 5` 再把隐藏页几何当产品契约。只验证 object exists / 正确 parent / index mapping / required dependency 已注入；**hidden geometry unspecified**（dump 仅信息性：实测 `transactionsPage: x=0 y=0 w=0 h=0 parent=workspaceHost`）。

### D1.8 Legacy / Diagnosis / Backend Freeze

- `Main.qml` 的 Legacy 内容（StatisticsOverview + Transactions pane）**全部原样**；D1 不移动任何 transaction UI。
- `DiagnosisPage.qml` **零修改**。
- `AnalysisController` / `TransactionListModel` / Core / Replay / Serial / Diagnosis / AI / Agent **零语义修改**；**未新增** `selectedTransaction` / `transactionSelection` / detail state / new transaction role（`git diff --name-only` 证实）。

### D1.9 D1 Structural / Nav Assertions（最小扩展，不重写旧场景语义）

在 `runShellNavAssertions` 内新增：`workspaceTransactionsIndex == 5`、`workspaceDeviceIndex == 6`；`navItem_5`/`navItem_6` **disabled** 且点击不改 index；`transactionsPage` **存在** + **直接父级 = workspaceHost** + **`analysisController` 注入非空** + **在 5 个 active workspace 选中时不可见**；`currentWorkspaceIndex` 合法范围 **0..6**。**未改**：real-workspace 集合（0..4）、`activePage()`、geometry 趟数、旧场景语义。

### D1.10 Problems / RCA（RED → GREEN）

- **RED**：首轮 nav check `exit 1` —— `GEOFAIL: diagnosis: NAVFAIL diagnosisPage visibility does not follow selection 4`（14 场景全部 NOT RUN）。
- **根因**：`TransactionsPage` 被插到 `DiagnosisPage` **之前**，在 StackLayout 里成为 **child 4**，抢占了"诊断"的槽位 ⇒ 选中 index 4 时显示的是空事务页，`diagnosisPage` 不可见。
- **分类**：**presentation index / StackLayout 顺序**（child 顺序就是 index 契约的物理载体）。
- **修复**：把 `TransactionsPage` 移到 `DiagnosisPage` **之后**（child 5）。重跑 → **GREEN**。
- **教训（写入知识条目）**：**StackLayout 的 child 顺序和 rail 的 entry 顺序是同一份 index 契约的两个物理表示**；插入新页必须同时核对两处顺序，且"诊断站"这类既有断言的失败正是顺序错位的直接信号。

### D1.11 Validation（真实命令与输出）

```text
cmake --preset debug-local && cmake --build --preset debug-local → [68/68] / [7/7] Linking modbuslens.exe（0 error）
--qml-smoke-test   → EXITCODE=0（stderr 卫生计数 0）
--qml-nav-check    → EXITCODE=0
  NAV SCENARIOS: basic five-workspace path PASS, A PASS, B PASS, D PASS, E PASS, F PASS,
    G' PASS, H PASS, I PASS, J PASS, K PASS, K' PASS, L PASS, N PASS
  NAV DASHBOARD PRESENTATION CHECK: PASS；NAV SCENARIO M: DEFERRED BY DESIGN
  NAV CHECK PASS (five workspaces; ...；M deferred by design)
--qml-geometry-check → EXITCODE=0；**标准 10 passes（5 active × 2 sizes）** + 2 targeted demo-dashboard
  passes（C4）；0 GEOFAIL；dump 16 段；`transactionsPage` 信息性条目 0×0（隐藏页无几何契约）
ctest --preset debug-local → 100% tests passed, 0 failed out of 26（数量不变）
git diff --check → PASS
```

**未伪称** D1 已有 Transactions geometry acceptance。

### D1.12 Files Changed（D1）

新增 `src/ui/qml/pages/TransactionsPage.qml`；修改 `src/ui/qml/Main.qml`（index 契约 + child 5）、`src/ui/qml/components/NavigationRail.qml`（+事务 disabled；设备 5→6）、`CMakeLists.txt`（QML 注册）、`src/main.cpp`（D1 断言 + dump 信息性条目）、`scripts/deploy_windows.bat`（page existence checklist 补 `TransactionsPage.qml` —— **deploy behavior-bearing**）；docs：本文件、PROJECT_STATUS、BACKLOG、devlog、INTERVIEW_NOTES。

### D1.13 Result

- D1 shell 与 index 契约落地：**rail 7 条目**（5 active + 2 disabled）、**标准 geometry 仍 10 趟**、14 项场景零语义变化、Legacy/Diagnosis/backend 全部冻结、**无能力消失窗口**。
- **Manual Review = PENDING**（D1 为 shell 阶段；人工验收在 D6）。
- verified LKGC **不变 = `bc754be`**；未 push。

## D2 — Atomic Transactions Migration + Workspace Activation（Implementation Record，2026-09-18）

### D2.0 D1 Review = PASS（用户）+ 一条 binding guardrail

用户裁定 **M9-D D1 Review = PASS**（index contract accepted / disabled shell accepted / child-order RED-RCA accepted；**D1 不单独要求人工视觉，最终人工包归 D6**），并新增：

- **truthful subtitle guardrail**：**D2 启用 Transactions 后，任何用户可见文案只能描述 D2 已真实存在的能力**。因此 D1 的副标题「通信记录与事务详情」**在本轮必须撤下**（detail 到 D3 才存在）——D2 使用真实文案 **「通信记录」**；D3 落地 detail 后才可恢复/更新为扩展文案。

### D2.1 Preflight

```text
branch = main；HEAD = ce57d9a；working tree clean；git diff --check PASS
V2 verified LKGC = bc754be；v1.0.0^{commit} = ae067ab（annotated tag 对象 2cee626）
origin/main = a40d935；ahead 60 / behind 0
```

### D2.2 Mandatory Inventory / 真实 item tree（迁移前实读）

Legacy 事务呈现（`Main.qml`）：`Rectangle transactionsPane`（`objectName: "legacyTransactionsPane"`，fillWidth/fillHeight、min 520、surface/border/radius 6/clip）→ `ColumnLayout(anchors.fill, margins 12, spacing 8)` → 标题「最近通信记录」(18px bold) → 固定表头 `Row`（5 个 Label：设备/功能码/状态/耗时/异常码，宽度由 pane 的**单一 owner** 派生：`tableUsableWidth = max(w−24,0)`，15%/15%/20%/18%/剩余，min 64/60/96/84/96）→ `Item(fillWidth/fillHeight, minimumHeight 120)` → `ListView`（**delegate 虚拟化**、`clip`、`StopAtBounds`、`spacing 4`、`model: analysisController.transactionModel`）→ delegate `Rectangle`（`h = issueText !== "" ? 64 : 36`；主行 5 列；issue 次行 11px、`maximumLineCount 2`）→ 空态 Label「暂无通信记录」。**objectName 审计**：`legacyTransactionsPane` = **B 类（legacy 语义）**（迁移后重命名为 `transactionsPane`）；`transactionList` 等仅为 `id`（中性，保留）。**harness 引用**：`legacyTransactionsPane` 出现在 geometry 守卫 + dump + 两处 evidence 断言。

### D2.3 Before baseline（D1 tree；`build/d1_geo.txt`）

同候选环境下的搬运前证据：Legacy 事务 pane **935×427 @1024×720**、**911×407 @1000×700**（外加 C1 时代的 935×422 记录），列几何由**未改动**的公式派生。**如实说明**：本轮未单独跑"empty + demo 两状态"的专用 baseline 采集，而是复用 D1 候选的 geometry dump（两尺寸、空态）+ 迁移后的同名宽对比；demo 行的运行时映射由 **Scenario O** 的断言覆盖（见 §D2.7）。

### D2.4 RED（先契约后搬运）

先落 D2 harness 契约、不动 QML ⇒ `--qml-geometry-check` **exit 1**：

```text
GEOFAIL: DEFAULT legacy: transactionsPane not found — the single transactions presentation is missing
GEOFAIL: DEFAULT legacy: NAV navItem_5 (transactions) must be enabled
GEOFAIL: DEFAULT legacy: NAV transactionsPane not found (the moved presentation is missing)
```

RED 明确是 **expected D2 missing capability**（单 owner 未建立 + 导航未启用），**不是**破坏旧场景。

### D2.5 Atomic MOVE（一个 behavior-bearing candidate 内同时成立）

同一提交内完成（A–E 全部成立，无 committed 中间态）：

- **A** rail `navItem_5 (事务) enabled = true`；
- **B** `TransactionsPage` 拥有完整事务呈现（整块机械搬运）；
- **C** Legacy **不再拥有**事务呈现（pane、标题、表头、ListView、空态、以及**只为事务存在的分隔线**与 layout wrapper 一并移除）；
- **D** 旧事务能力未消失（同一 `TransactionListModel`、同一 role 绑定、同一 formatter、同一列宽 owner、同一行高规则、同一 issueText 语义、同一空态语义）；
- **E** 最终**只有一个** transaction presentation owner（运行时父子链证明）。

**Move, not reimplement**：未改列、未改文案语义、未改 status 映射、未加 detail/selection/filter、未重做 delegate。**文本替换仅一类**：`root.<alias>` → `DS.<alias>`（同值），以及 `root.errorAccent` → 页内 `frozenErrorAccent: "#C0392B"`（V1 冻结字面量，无 DS token；沿用 B5 先例）。

### D2.6 Object identity / observability

- `legacyTransactionsPane` → **`transactionsPane`**（不再保留语义错误名称；**无 alias/dummy**；**不同时保留两个 pane**；containment 断言锁到新 owner）。
- 迁移时为"验证可观测性"补 **4 个 objectName**（original 只有 `id`）：`transactionsTableHeader`（表头 Row）、`transactionsList`（ListView）、`transactionsEmptyHint`（空态 Label）、以及页内 `frozenErrorAccent` 属性名。**均为中性命名的可观测性契约，不是 public product API**。

### D2.7 Runtime single-owner proof（禁止只靠 grep）

`runShellNavAssertions` 与 `assertTransactionsPresentation` 逐项在**真实 item 树**上断言：

- `transactionsPane` 存在；`underItem(pane, transactionsPage)` **= true**；`underItem(pane, legacyWorkspace)` **= false**；
- 全树中 `transactionsPane` 恰好 **1 个**（evidence 阶段另以 `countNamed()` 断言）；
- `transactionsList` 视口非零且 `ListView.count == rowCountOf(ctrl)`（视图与 model 一致）；
- `transactionsEmptyHint` 可见性 = (`rowCount == 0`)；
- 首行高度 = `issueText !== "" ? 64 : 36`（**数据驱动的行高规则随迁移存活**）。

### D2.8 Legacy after extraction（统计-only）

Legacy 现只剩 `StatisticsOverview(legacy)` + **新增 `legacyTailSpacer`**。

**真实 RCA**：事务 pane 是 Legacy 列里**唯一的 `fillHeight` 子项**；移除后 Qt 把余量分摊进行内，统计块被推到 **y=226**（实测）——违反"top 不漂移"。修复 = Dashboard C1 的同机制**尾部余量所有者**（`legacyTailSpacer`），并新增常驻断言 **`statisticsOverview_legacy` 顶端 == 页面 margin（`DS.spacingL`）**。修复后实测 **y=0**（overview）/**panel y=27**、`implicit 864×195` 保持不变 ⇒ **统计几何无漂移**。分隔线（仅为事务存在）与事务 wrapper 已删除，**无空洞 separator / 无 0 高度 ghost pane / 无隐形事务 UI**；未 redesign StatisticsOverview、未加 filler card。

### D2.9 TransactionsPage final D2 composition

`SectionHeader(title「事务」，subtitle「通信记录」—— truthfulness guardrail) + 迁移后的 pane`。**无 placeholder detail pane、无「选中一条查看详情」类不存在能力的文案**。

### D2.10 Navigation activation / index contract

`事务 index = 5 enabled = true`；`设备 index = 6 enabled = false`；其余 index 不变（Legacy 0 / Dashboard 1 / Communication 2 / Replay 3 / Diagnosis 4）。点击 Transactions **只改变 `currentWorkspaceIndex`**：不 runDemo/load replay/switch source/clear/run diagnosis/select transaction/mutate model（nav 场景与快照比较继续保证）。

### D2.11 Basic six-workspace path

结构相位升级为 **basic six-workspace path**（Legacy → Dashboard → Communication → Replay → Diagnosis → **Transactions** → Dashboard → Legacy），判决行与最终 PASS 行同步更新为 six workspaces。**旧 A/B/D/E/F/G'/H/I/J/K/K'/L/N 业务意图不变**（未机械塞入 Transactions）；新增独立 **Scenario O** 专测迁移与导航中性。

### D2.12 Scenario O（迁移 / 导航中性 / 行呈现）

| 阶段 | 断言 | 实测输出 |
| --- | --- | --- |
| demo 建立 | `runDemoBatch()` → `rowCount == 4` + 扩展快照 | `rows=4 observed=4 source=确定性演示` |
| 进入 Transactions | identity/可见性 + 快照逐值相等 + 单 owner + 视图/model 一致 + 空态 + 行高规则 | `NAV [scenario O @transactions]: rows=4 observed=4` |
| 离开再回来 | 快照逐值相等 + 单 owner + 行呈现 | `NAV [scenario O]: the transactions workspace shows the same model across navigation (no business change)` |
| **ExpectedNoResponse 行**（`t015_broadcast.mlog`） | `rowCount == 1` 且 `statusText == "预期无响应"`（**中性：非 成功/超时/失败/写入成功**） | `NAV [scenario O broadcast]: rows=1 status=预期无响应 (neutral, not an anomaly)` |
| **ProtocolError 行**（`t014_protocol_error.mlog`） | `rowCount == 1` 且 `statusText == "协议错误"` 且 `issueText` 非空（**确定性详情随迁移存活**） | `NAV [scenario O protocol]: rows=1 status=协议错误 hasIssueDetail=1 (orthogonal axes)` |
| **正交性** | `statusText` 不得包含 `issueText`（issue 未改写 status） | 断言通过 |
| 清空 | `clearResults()` → `rowCount == 0` + 空态 | `NAV [scenario O]: transactions empty state clean after clearResults` |

**Replay 成功/失败保持**：沿用既有 J/K/K′ 语义（成功加载 → Transactions 显示权威 model；失败替换 → 旧 source/model 保留、Transactions 继续显示旧行），**未新增 source 语义**；O 的快照比较覆盖"导航不改业务值"。

### D2.13 可视中性证据（同宽对比）

| 量 | 迁移前（D1 tree, Legacy pane） | 迁移后（D2, Transactions pane） |
| --- | --- | --- |
| 1024×720 pane | 935×427 | **935×620** |
| 1000×700 pane | 911×407 | **911×600** |
| 列宽（1024/1000） | 由同一未改公式派生 | `device 137/133`、`function 137/133`、`status 182/177`、`latency 164/160`、`exception 291/284` |

**同尺寸下 content width 完全相同（935 / 911）** ⇒ 列宽逐值一致；y 偏移与高度差异来自不同父页面，**不是迁移回归**（§20）。

### D2.14 Transactions geometry contract（D2 §21）

`--qml-geometry-check` 新增 Transactions-active 断言块：page/header/pane/表头/ListView **非零且在界内**、pane 完全落在 page 内（无越界）、表头在列表之上（无重叠）、**列宽由单一 owner 派生且总和 ≤ 可用宽**（实测 911/887）、**空态可见性跟随 `observedCount`**。实测（1024×720）：`transactionsPane 935×620 @y=27`、`transactionsTableHeader 911×12`、`transactionsList 911×550`、`transactionsEmptyHint 72×12`（居中）。

### D2.15 Geometry matrix

**12 standard passes = 6 active workspaces × 2 sizes**（Legacy/Dashboard/Communication/Replay/Diagnosis/Transactions）**+ 2 targeted demo-dashboard passes（C4 继续保留）**；隐藏的 Device **不进入标准 geometry、无几何契约**。0 GEOFAIL。

### D2.16 Freezes

- **DiagnosisPage zero diff**；导航到 Transactions 不 cancel AI / 不 invalidate Agent / 不 run Baseline / 不切 tab（O 与 L/N 快照断言继续覆盖）。
- **Dashboard / Communication / Replay zero diff**；旧 navigation/source 测试继续 PASS。
- **Controller / TransactionListModel / Core / Replay parser / Serial / Diagnosis / AI / Agent zero diff**；**未新增 role**（D2 是 presentation MOVE）。
- **CMakeLists.txt / deploy_windows.bat zero diff**（D1 已注册并进入 checklist）。

### D2.17 Negative scope（明确不包含）

无 detail pane / 无 selected transaction product state / 无 filter-search / 无 proxy model / 无 raw-hex / 无新 transaction role / 无 request-response 分轴 / 无 Diagnosis redesign / 无 AI-Agent 复制 / 无 Dashboard recent-transactions preview / **无 Legacy 退役** / 无 StatisticsOverview 删除 / 无 M10 主动写 / 无 M11 解码 / 无 M12 profile-intelligence。**未把旧 ListView 的 `currentIndex` 升级成产品契约**（D3 才设计 selection lifetime）。

### D2.18 Problems / RCA（全部真实、逐条留痕）

| # | 现象 | 分类 | 根因 | 修复 |
| --- | --- | --- | --- | --- |
| 1 | RED 阶段断言未命中 nav 切换（`NAV [transactions]: index=4`） | **harness / index mapping** | nav-check 的 `switchTo` 未补 `pageIndex == 5 → workspaceTransactionsIndex`（fallback 落到 diagnosis） | 补映射（并同步 evidence `switchTo`） |
| 2 | `transactionsList not found` | **observability 缺口** | 原 Legacy 项只有 `id`，没有 objectName；迁移后 harness 找不到 | 补 3 个中性 objectName（表头/List/空态） |
| 3 | `TransactionsPage.qml: Label is not a type`（QML 加载失败，exit 127） | **QML import** | 迁移块使用 `Label` 而页面缺 `import QtQuick.Controls` | 补 import |
| 4 | **Segmentation fault（exit 139）** | **harness / 空指针** | 新增 `transactionsPtr` 共享状态只在 capture list 里声明，**stage 0 漏了赋值**，`verifyStructureAndIdentity` 解引用空指针 | stage 0 补捕获 + 存在性断言 |
| 5 | **Legacy 统计块被推到 y=226** | **layout ownership（真实产品回归）** | 事务 pane 是 Legacy 列里唯一 `fillHeight` 子项；移除后余量被分摊进行内（B3/C1 同族现象） | 加 `legacyTailSpacer`（C1 同机制）+ 常驻"top == margin"断言；实测回到 y=0 |

**没有任何一条是"改 Controller/业务语义"解决的**。

### D2.19 Validation（真实命令与输出）

```text
cmake --build --preset debug-local → Linking modbuslens.exe（0 error）
--qml-smoke-test   → EXITCODE=0
--qml-nav-check    → EXITCODE=0
  NAV [transactions]: index=5 … transactionsVisible=1（六 workspace 结构站）
  NAV [scenario O @transactions]: rows=4 observed=4 source=确定性演示
  NAV [scenario O broadcast]: rows=1 status=预期无响应（中性）
  NAV [scenario O protocol]: rows=1 status=协议错误 hasIssueDetail=1（正交轴）
  NAV SCENARIOS: basic six-workspace path PASS, A PASS, B PASS, D PASS, E PASS, F PASS,
    G' PASS, H PASS, I PASS, J PASS, K PASS, K' PASS, L PASS, N PASS, O PASS
  NAV DASHBOARD PRESENTATION CHECK: PASS；NAV SCENARIO M: DEFERRED BY DESIGN
  NAV CHECK PASS (six workspaces; …；M deferred by design)
--qml-geometry-check → EXITCODE=0；12 standard passes（6 active × 2）+ 2 targeted（C4）；0 GEOFAIL
  TRANSACTIONS COLUMNS: pane=935 usable=911 device=137 function=137 status=182 latency=164 exception=291
ctest --preset debug-local → 100% tests passed, 0 failed out of 26
git diff --check → PASS
stderr 卫生：ReferenceError/TypeError/binding loop/NaN/Infinity/required missing/is not a type 计数 0（geo/nav/smoke）
case 标签：0..111 连续唯一（112 个）
```

### D2.20 Files Changed（D2）

`src/ui/qml/pages/TransactionsPage.qml`（shell → 完整事务呈现 + 4 个观测名 + 别名映射）、`src/ui/qml/Main.qml`（移出 pane/分隔线、Legacy 只剩统计 + `legacyTailSpacer`）、`src/ui/qml/components/NavigationRail.qml`（事务 enabled）、`src/main.cpp`（ActivePage/守卫/12 趟/六 workspace 结构站/Scenario O/证据块更新）；docs。**CMakeLists / deploy script 零 diff**（D1 已注册）。

### D2.21 Result

- **原子迁移+启位完成**：Transactions 成为第六个 active workspace，Legacy 只剩统计，**任何时刻事务能力都可达**，**最终只有一个 presentation owner**（运行时证明）。
- **Manual Review = PENDING**（D6 人工包）；verified LKGC **不变 = `bc754be`**；未 push。

## D3 — Transaction Selection + Read-only Detail Presentation（Implementation Record，2026-09-18）

### D3.0 D2 Review = PASS（用户）+ 三条 note

- **A. before-baseline 性质**：D2 实际取得的是 **partial migration baseline**（D1 候选的 geometry dump + 同尺寸 content-width 对比），**不是**完整 empty+demo 双状态专门 baseline。现有证据足以接受 D2，但**不得以后夸大**。
- **B. 正交性的 oracle 层级**：「`statusText` 不包含 `issueText`」只是**辅助 negative oracle**；outcome/issue 正交的**主要依据**是 ①`TransactionStatus` role 的真实来源 ②`issueText` 的真实来源 ③QML **没有**用 issueText 重新解释 status。
- **C. `root.*` → `DS.*` 与 `frozenErrorAccent`**：按**等值机械迁移**接受；**D3 不继续做 style/token cleanup**。

### D3.1 Sequencing bookkeeping（本轮冻结）

- **D3 = selection + detail only**；
- **D4 = Phase 1 已接受的 Diagnosis existence cue + 必要的 Transactions polish**（filters/search **继续 DEFER**，不因存在 D4 编号就自动实现）；
- **D5 = Legacy retirement**；**D6 = deploy/evidence/manual candidate**。
- ⇒ Phase 1 已接受的 Diagnosis textual clue **不会无声丢失**（有明确归属阶段）。

### D3.2 Mandatory Selection-Seam Audit（真实源码）

`TransactionListModel` 的 mutation API **实读**（`TransactionListModel.cpp`）：

| mutation operation | emitted model signal | selected row 是否仍代表同一事务 | D3 结论 |
| --- | --- | --- | --- |
| `setEntries(entries)`（**唯一** API，6 个调用点） | `beginResetModel()` → `entries_ = std::move(entries)` → `endResetModel()` | 否（行集合整体替换） | **invalidate** |
| 逐行原地更新（Pending → completed 等） | **不存在**（全模型无 `dataChanged`） | — | 无需实时跟随 |
| `rowsInserted/rowsRemoved` | **不存在** | — | — |

调用点：`connectSerial`（清空）、`publishSerialResult`（单条）、`setTransactionEntries`、`runDemoBatch`、`clearResults`、`loadReplayFile`（成功路径）。**失败的回放替换在 `setEntries` 之前返回** ⇒ 不触发 reset。

**QML 侧实读**：`TransactionsPage` 的 `ListView`（`transactionsList`）delegate 绑定 8 roles 中的 7 个显示字段；`currentIndex` 原先只是 Qt 的默认呈现属性（D2 明确未升级为契约）。

### D3.3 Detail data-access seam 比较与选择

| 方案 | virtualization / lifetime | model reset | dataChanged | navigation hide/show | stale detail | backend |
| --- | --- | --- | --- | --- | --- | --- |
| A. `ListView.currentItem` 直读 | **差**：代理项会随滚动销毁/回收 ⇒ detail 读不到或读到空 | 未处理 | — | 可用 | **存在** | 无改动 |
| **B. page-local presentation snapshot（采纳）** | 好：在**选中那一刻**从屏幕上的代理项拷贝（`itemAtIndex`），之后与代理项生命周期无关 | `Connections.onModelReset` 清 selection + snapshot | 不适用（无 dataChanged） | 自然保留 | **不可能**（唯一 mutation 就是 reset，而 reset 会清） | **零改动** |
| C. 新增 model read-only row accessor | 好 | 需自行处理 | 需自行处理 | 可用 | 可控 | **需改 C++（STOP 条件 A）** |

**采纳 B**，其安全性**由 §D3.2 的审计证明**：数据只有一条变更路径（整批 reset），因此快照不可能滞后；`modelReset` 即失效。**未新增任何后端 API**；**未**引入 `AnalysisController.selectedTransaction*`（绝对禁止项）。

**Stop conditions（§34）逐条核对**：A 需新 C++ accessor → 否；B mutation 语义无法区分 stale → 否；C 必须改 Controller → 否；D 需新 domain role → 否；E 1000×700 垂直 master-detail 无法成立 → 否（实测 detail 56px、表格 457px）。**均未触发。**

### D3.4 Selection authority 与初始/reset 契约

- selection = **page-local presentation state**（`page.selectedRow` / `page.selectedEntry`），**不是** Controller / domain / Diagnosis / Replay source state。
- **`currentIndex = -1` = no explicit selection**；**model 有行时不自动选第 0 行**（D3 §8）——detail 显示 **「选择一条事务查看详情」**（`transactionDetailEmpty`）。empty model 复用同一 no-selection 态（列表另有「暂无通信记录」空态）。
- **model reset 后不得自动「重新选第 0 行」来伪装 persistence** ✓（P4 断言）。

### D3.5 Selection lifetime（冻结三类，Scenario P 实证）

| 分支 | 契约 | 实证 |
| --- | --- | --- |
| **A. same model + navigation only** | selection 保持、detail 保持 | P2：`NAV [scenario P2]: selection + detail survived Transactions -> Dashboard -> Replay -> Transactions` |
| **B. authoritative model replacement/reset**（成功回放 / `runDemoBatch` / `clearResults`） | **invalidate**：`currentIndex = -1`、snapshot 清除、detail 回 no-selection | P4：`successful replacement reset the model and invalidated the selection (no row-0 re-pick)`；P5：`a new batch reset the model; the old selection did not leak`；clearResults 后同样 |
| **C. failed replacement（model 未变）** | selection 保持、detail 保持（与 B4 的 authority 原则一致） | P3：`failed replacement kept the model, the selection and the detail` |

**exact signal**：`QAbstractItemModel::modelReset`（`Connections.onModelReset`）—— 这是 D3 实现依赖的**唯一**失效信号，在此明确记录。**未使用**「延时后猜状态」作为 lifecycle oracle：所有断言都在操作完成后的**同一轮**读取真实状态（回放加载为同步批处理）。

### D3.6 Mouse / keyboard selection

- **产品路径**：delegate 的 `TapHandler.onTapped: transactionList.currentIndex = index`（鼠标）+ `ListView.focus: true`（Qt 自身的 Up/Down/Home/End 行为）；页面统一以 `onCurrentIndexChanged: page.selectRow(currentIndex)` 把 currentIndex 映射为 selection ⇒ **鼠标与键盘共用一条路径**，未手写键盘状态机。
- **选中态呈现**：`DS.navigationSelectedSurface` 背景 + 2px `DS.primary` 左侧强调条（**复用既有 DS token，无 severity/health 色彩**）。
- **harness 边界（§23 如实申报）**：本仓库无可靠的 Qt input synthesis ⇒ D3 由 harness **直接设置 `transactionsList.currentIndex`** 驱动 selection-state → detail mapping 与 lifecycle。**这不是 mouse/keyboard 物理交互证明**；鼠标 + 键盘的实机验证**列入 D6 Manual**。未为 D3 引入 QtTest 依赖。

### D3.7 Detail presentation boundary（逐字段，按真实 8 roles）

| detail 字段 | 来源 role | 呈现（复用既有 formatter 语义） |
| --- | --- | --- |
| 设备 | `deviceAddress` | `设备 %1`（与行内一致） |
| 功能码 | `functionCode` | `0xNN`（与行内一致的 hex 规则） |
| **状态** | `statusText` | **独立字段**（`transactionDetailStatus`） |
| 耗时 | `elapsedMs` | `%1 ms` |
| 异常码 | `hasExceptionCode` → `exceptionCode` | `异常码 0xNN` / `—`（hasX 先行，沿用行内规则与 `frozenErrorAccent`） |
| 详情 | `issueText` | **独立字段**（`transactionDetailIssue`）：`详情：%1` / `详情：—`，**完整不解析、不截断语义** |

**禁止项全部遵守**：无 raw/hex 报文、无起始地址/quantity/请求参数、无 request/response 分轴、无寄存器解码、无 AI 解释；**未解析 `issueText`** 反推 anomaly/severity/issue 类型。QML 未重实现 status/exception/latency formatter（沿用与行内**同一表达式**）。

### D3.8 Outcome / Issue 正交 + ENR + ProtocolError + Unsupported

- **正交性**：`transactionDetailStatus.text == selectedEntry.statusText`（不掺 issue）；`transactionDetailIssue` 独立呈现；断言「status 文本不得包含 issue 文本」。**主要依据仍是 §D3.0-B 的三条来源事实**，negative oracle 为辅。
- **ExpectedNoResponse detail**（`t015_broadcast.mlog`，选中唯一行）：`statusText == 预期无响应`，**中性**（无 写入成功/失败/Timeout/异常/warning）；`issue` 为空 ⇒ 按既有约定显示 **`详情：—`**（不发明解释）。
- **ProtocolError detail**（`t014_protocol_error.mlog`）：`statusText == 协议错误`，`issueText` 非空且与 model presentation **逐值一致**（`assertTransactionDetailMapping` 的 7 字段逐一相等）；**issueText 未充当 status authority**。
- **Unsupported replay**：仍不进 model ⇒ 无 selection/detail/伪造行；B4 notice lifecycle 零变化。

### D3.9 Detail UI shape / surplus ownership

- **垂直 master-detail**（Phase 1 批准；**未引入第二个 workspace、未用横向 split 压窄表格、未用可拖动 SplitView**）：`SectionHeader` → pane → 表头 → **ListView（`Layout.fillHeight` = 唯一 surplus owner）** → 分隔线 → **detail（自然高度、`Layout.fillWidth`，不参与 surplus 分配）**。
- surplus 归表格（它本身就是主 evidence viewport，与 C1 的"section gap 被 stretch"不同类）；detail 高度由内容决定（实测 56px）；**无随机大 gap、section 间距未被撑开**。
- **Detail component decision**：**直接放在 TransactionsPage**（单 consumer；未新增 `TransactionDetail.qml`；未放入 DS）。

### D3.10 Geometry contract（§30）

- **标准 12 趟保持**（6 active × 2 尺寸）**+ C4 的 2 趟 targeted**；
- **新增 2 趟 targeted selected-detail**（`m9d-transactions-detail-1024x720` / `-1000x700`，先建批次、**后**选行、再测量）——**附加而非替代**；
- Transactions-active 断言新增：`transactionDetail` 非零/在页内/**不与列表重叠**；**`transactionsList` 高度 ≥ 6×36**（viewport capacity）；选中时 detail 字段逐值映射。

**实测（选中态）**：

| 尺寸 | pane | 表头 | ListView | detail |
| --- | --- | --- | --- | --- |
| 1024×720 | 935×620 | 911×12 | **911×477**（≥216 ✓） | **911×56** @y=540 |
| 1000×700 | 911×600 | 887×12 | **887×457**（≥216 ✓） | **887×56** @y=520 |

`transactionDetailStatus 72×12`、`transactionDetailIssue 887×12`；**无裁切、无重叠、page header/表头/列表/detail 全部完整**。

### D3.11 ObjectName / observability

最小稳定 anchor：既有 `transactionsList` 保留；新增 `transactionDetail`、`transactionDetailEmpty`、`transactionDetailStatus`、`transactionDetailIssue`（+ `transactionDetailDevice/Function/Latency/Exception`）。**未给每个 Label 都堆 objectName**；足够 harness 证明 selection/mapping/reset/containment。

### D3.12 Subtitle 更新（D1 Review guardrail 闭环）

detail 真正可用后，副标题由 **「通信记录」** 更新为 **「通信记录与事务详情」** ✓。

### D3.13 Freezes

- **DiagnosisPage zero diff**；Transactions 页**无** baseline result / finding count / AI 输出 / Agent 回答 / Provider 控件；**Phase 1 accepted 的 Diagnosis existence clue 明确留到 D4**。
- **Legacy zero diff**（StatisticsOverview + tail spacer 原样；未提前 retirement、未删 StatisticsOverview）。
- **Dashboard / Communication / Replay zero diff**；selection 页面状态不影响 Dashboard 统计、Communication 草稿、Replay selectedFile/source authority。
- **Controller / TransactionListModel / Core / tests / CMake / deploy script zero diff**（`git diff --name-only` 证实）。

### D3.14 Negative scope

无 `Controller.selectedTransaction*`、无新 model role、无 raw/hex、无 request/response 分轴、无 filters/search、无 proxy、无 Diagnosis cue、无 Diagnosis redesign、无 AI/Agent、无 Legacy retirement、无 StatisticsOverview 删除、无 Dashboard recent transactions、无 M10 write / M11 decode / M12 profile。

### D3.15 Problems / RCA（5 条，全部真实留痕）

| # | 现象 | 分类 | 根因 | 修复 |
| --- | --- | --- | --- | --- |
| 1 | `NAVFAIL scenario P after reset: the no-selection hint is not visible` | **harness / hidden-page 契约** | helper 在 **Transactions 页隐藏时**（当轮停在 Replay）仍断言 detail 空态可见；隐藏页可见性不是契约 | helper 仅在页面可见时断言空态可见性；并让 P4 先切回 Transactions 再替换 |
| 2 | `TransactionsPage.qml:259 TypeError: Cannot read property 'currentIndex' of null`（33×） | **product QML / attached-property 作用域** | delegate 的**嵌套子项**中用了 `ListView.view.currentIndex`——该 attached property 只挂在 **delegate 根**上 | 委托根新增 `readonly property bool rowSelected`，子项读 `parent.rowSelected` |
| 3 | `TransactionsPage.qml:256 TypeError`（启动期） | **product QML / 生命周期** | `Connections.onModelReset` 在 ListView 创建前可能触发（启动期首批发布）⇒ id 仍为 null | 处理器加 `if (transactionList)` 守卫 |
| 4 | 1024 的 targeted detail 趟 detail 仅 12px（未选中） | **harness 顺序** | transition 里 **selection 设在 `runDemoBatch` 之前**，批次发布 reset model ⇒ selection 按契约被清除 | selection 移到批次发布**之后**（这本身是 D3 契约 B 的正向印证） |
| 5 | D2 遗留的 pane 块整体缩进少 4 格 | **可读性 / 机械** | D2 dedent 基准算错 4（与 brace depth 不符） | 本轮按 brace depth 统一 +4（纯空白，无语义变化） |

### D3.16 Validation（真实命令与输出）

```text
cmake --build --preset debug-local → Linking modbuslens.exe（0 error）
--qml-smoke-test   → EXITCODE=0（stderr 卫生 0）
--qml-nav-check    → EXITCODE=0
  NAV [scenario P1]: row 2 selected, detail mapping verified field by field
  NAV [scenario P2]: selection + detail survived Transactions -> Dashboard -> Replay -> Transactions
  NAV [scenario P3]: failed replacement kept the model, the selection and the detail
  NAV [scenario P4]: successful replacement reset the model and invalidated the selection (no row-0 re-pick)
  NAV [scenario P4 detail]: expectedNoResponse kept its neutral wording
  NAV [scenario P5]: a new batch reset the model; the old selection did not leak
  NAV [scenario P]: selection lifecycle (initial -1 / navigation persists / replacement invalidates /
    failed replacement preserves / clear resets) verified
  NAV SCENARIOS: basic six-workspace path PASS, A PASS, B PASS, D PASS, E PASS, F PASS, G' PASS,
    H PASS, I PASS, J PASS, K PASS, K' PASS, L PASS, N PASS, O PASS, P PASS
  NAV DASHBOARD PRESENTATION CHECK: PASS；NAV SCENARIO M: DEFERRED BY DESIGN
  NAV CHECK PASS (six workspaces; …；M deferred by design)
--qml-geometry-check → EXITCODE=0；**12 standard passes + 2 targeted (C4) + 2 targeted (D3)**；0 GEOFAIL
  DETAIL MAPPING: row=2 status=CRC 错误 issue=<none>
ctest --preset debug-local → 100% tests passed, 0 failed out of 26
git diff --check → PASS
stderr 卫生：ReferenceError/TypeError/binding loop/NaN/Infinity/required missing/is not a type 计数 0（geo/nav/smoke）
case 标签：0..128 连续唯一（129 个）
```

### D3.17 Files Changed（D3）

`src/ui/qml/pages/TransactionsPage.qml`（selection/snapshot/modelReset 失效/鼠标+键盘路径/选中态/detail 区域/副标题/缩进统一）、`src/main.cpp`（detail 契约 helper + Scenario P + geometry selectRow 步与 detail 断言 + dump 条目）；docs。**未动** Controller/Core/tests/其它页/Main.qml/rail/CMake/deploy。

### D3.18 Result

- D3 完成：**selection 为 page-local**、三类 lifetime 分支全部机器实证、detail 只呈现既有 8 roles 中的 7 个显示字段且**逐字段与 model 相等**、正交/ENR/ProtocolError/Unsupported 边界全部遵守、垂直 master-detail 在两尺寸成立（表格保持全宽、viewport ≥ 6 行）。
- **Manual Review = PENDING**（鼠标/键盘实机与视觉均在 **D6**）；verified LKGC **不变 = `bc754be`**；未 push。

## D3 Review HOLD + Correction（2026-09-18，append-only）

> 本节为 **D3 Review = HOLD** 的两个 P0 闭环记录。**不重写 §D3 历史原文**；上文任何与本节冲突处，以本节为准。

### R0. D3 Review = HOLD（用户裁定）

两个 P0：

1. **geometry pass-count contradiction**（完成报告写 14 趟，与 12+2+2=16 不自洽）；
2. **deferred selection stale-callback safety**（`pendingSelectionRow` / `Qt.callLater` 路径在 D3 中**从未被测试覆盖**，其"陈旧完成不能跨 reset 复活"的结论只是源码论证）。

Scope freeze（本轮严格遵守）：不实现 Diagnosis cue、不做 filters/search、不做 style cleanup、不退休 Legacy、不删 StatisticsOverview、不改 model roles、不加 Controller selection、不做 mouse/keyboard 新功能、不开始 D4。

---

### R1. P0-1 Geometry Count Reconciliation（**Case A**）

**先不改代码**，重跑 `--qml-geometry-check`（EXIT=0）并逐趟分类。**exact stage list**（按 `steps` 向量顺序，tag = 步骤标识）：

| # | step tag | 打印标签 | 分类 |
| --- | --- | --- | --- |
| 1 | `m9b4-legacy-1024x720` | DEFAULT legacy | **A standard** |
| 2 | `m9b4-dashboard-1024x720` | DEFAULT dashboard | **A standard** |
| 3 | `m9b4-communication-1024x720` | DEFAULT communication | **A standard** |
| 4 | `m9b4-replay-1024x720` | DEFAULT replay | **A standard** |
| 5 | `m9d-transactions-1024x720` | DEFAULT transactions | **A standard** |
| 6 | `m9b5-diagnosis-1024x720` | DEFAULT diagnosis **[tab 0/1/2]** | **A standard**（1 步 → 3 次测量） |
| 7 | `m9b5-diagnosis-1000x700` | MIN 1000x700 diagnosis **[tab 0/1/2]** | **A standard**（1 步 → 3 次测量） |
| 8 | `m9d-transactions-1000x700` | MIN 1000x700 transactions | **A standard** |
| 9 | `m9d-transactions-detail-1024x720` | SELECTED DETAIL transactions | **C D3 targeted** |
| 10 | `m9d-transactions-detail-1000x700` | MIN 1000x700 selected detail | **C D3 targeted** |
| 11 | `m9b4-replay-1000x700` | MIN 1000x700 replay | **A standard** |
| 12 | `m9b4-communication-1000x700` | MIN 1000x700 communication | **A standard** |
| 13 | `m9b4-dashboard-1000x700` | MIN 1000x700 dashboard | **A standard** |
| 14 | `m9b4-legacy-1000x700` | MIN 1000x700 legacy | **A standard** |
| 15 | `m9c-dashboard-demo-1024x720` | DEMO dashboard | **B C4 targeted** |
| 16 | `m9c-dashboard-demo-1000x700` | MIN 1000x700 demo dashboard | **B C4 targeted** |

**计数（真实）**：

- **A standard = 12 steps**（6 active workspaces × 2 sizes = 12，**与冻结预期一致**）；
- **B C4 targeted = 2**；**C D3 targeted = 2**；
- **total steps = 16 = 12 + 2 + 2**（**与冻结预期一致**）；
- **打印出的 `GEOMETRY [...]` 段落数 = 20**，因为 diagnosis 的 2 个 step **各自 sweep 三个 tab**（2 个 step → 6 段）：10 个非 diagnosis standard step + 6 段 diagnosis + 2 + 2 = **20 段**。该 tab sweep 是 **M9-B5.3 的既有设计**（不是 D3 引入），且 harness 自身的 PASS 文案已声明 "…x 2 sizes, **the diagnosis pass sweeps its three tabs**"。

**RCA（矛盾根因）**：`16`（step）与 `20`（打印段落）两个真实数字之外，**完成报告与 PROJECT_STATUS 的 Test 状态行写成了 "14 趟"** —— 这是**报告内的加法错误**（12+2+2 被写成 14），并叠加了"趟"一词在 step / 打印段落之间**指代不清**。**14 不对应任何真实计数**：既不是 16 也不是 20。

**结论 = Case A**：真实输出自始就是 **16 = 12 + 2 + 2**（steps），**覆盖零缺失**（6 workspace × 2 尺寸 + 2 C4 + 2 D3 全部在列，无任何 step 被替换或吞掉）。因此：

- **产品代码不改**（`TransactionsPage.qml` 本轮零 diff）；
- **test/harness 计数逻辑不改**（`steps` 向量、PASS 文案、diagnosis tab sweep 全部保持原样）；
- **仅文档纠正**：PROJECT_STATUS Test 状态行的 `14 趟几何` → `16 趟几何（12 standard steps + 2 C4 targeted + 2 D3 targeted；diagnosis 的 2 个 step 各 sweep 3 个 tab，故打印 20 段）`。

### R2. P0-2 Deferred-selection Audit（逐行，真实源码）

源码位置：`src/ui/qml/pages/TransactionsPage.qml`（D3 冻结版本，无改动）。

| 位置 | 行 | 行为 |
| --- | --- | --- |
| `selectRow(row)` | 62–78 | `row < 0` ⇒ 三清（selected/entry/pending）；`itemAtIndex(row)` 命中 ⇒ 立即快照；**未命中 ⇒ `pendingSelectionRow = row` + `Qt.callLater(page.applyPendingSelection)`** |
| `applyPendingSelection()` | 80–93 | **`const row = pendingSelectionRow;`（执行时读）**；`row < 0` ⇒ **return（no-op）**；`itemAtIndex(row)` 仍无 ⇒ **显式 no-selection**（`-1` + 清 entry + pending = -1） |
| `onCurrentIndexChanged` | 223 | `page.selectRow(currentIndex)` —— 与鼠标 `TapHandler`（269–271）**共用同一入口** |
| `modelReset` handler | 98–109 | 清 `selectedRow` / `selectedEntry` / **`pendingSelectionRow`** / `currentIndex`（后者带 `if (transactionList)` 守卫） |
| `Qt.callLater` 实参 | 72 | **传的是函数引用 `page.applyPendingSelection`，不带参数** ⇒ **没有行号被捕获**；且 Qt 对**同一函数**的多次 `callLater` 会**合并为一次**调用 |

**A. callback 读取执行时的 pendingSelectionRow，还是捕获旧 row value？**
**读取执行时值**（`applyPendingSelection` 第一行现读 `pendingSelectionRow`）。第 72 行传入的是**无参函数引用**，闭包内**不存在任何捕获的行号**。⇒ **无 stale capture**。

**B. modelReset 是否清 currentIndex / selectedRow / selectedEntry / pendingSelectionRow？**
**四者全清**（98–108：`currentIndex = -1`、`selectedRow = -1`、`selectedEntry = null`、`pendingSelectionRow = -1`）。

**C. 旧 callback 在 reset 后执行时如何变成 no-op？**
reset 把 `pendingSelectionRow` 置 **-1** ⇒ callback 现读得 **-1** ⇒ 命中 `if (row < 0) return;` **直接返回，不写任何状态**。它**不可能**用旧行号复活，因为**旧行号从未被保存**。

**D. 若 currentIndex 在 callback 前再次改变，旧 callback 如何避免覆盖新 selection？**
两种子情形，均安全：
- **新请求可立即完成**（delegate 在）：`selectRow(M)` 把 `pendingSelectionRow = -1` ⇒ 旧 callback 现读 -1 ⇒ **no-op**，新 selection 保留 —— **这正是 Scenario Q2 的机器实证**。
- **新请求同样被 park**：`pendingSelectionRow` 被**覆写为最新行号**（且 `callLater` 合并为一次）⇒ 后续 callback 读到的**就是最新请求**，结果只可能是 **最新行** 或 **显式 no-selection**，**绝不回到旧行**。
- 源码级 guard 归纳：`pendingSelectionRow ≥ 0` ⟺ "存在一个**尚未被消费**的**最新**请求，且自该请求以来**未发生 model reset**"（reset 必然清它会 -1，而 reset 是模型的**唯一**变更路径，见 §D3.2）。因此 `row < 0` 这一条判断同时覆盖了**"仍属于当前 model"**与**"仍属于当前请求"**两个条件。

**是否需要 generation token？** **不需要**：源码已满足"执行时读最新 pending + reset 清 pending + 应用前检查 `row >= 0`"三条件（用户 §4 的首段判据）。**未新增 generation / modelGeneration / Controller state**（§4 末段禁令遵守）。

**一处如实记录的观察（非缺陷）**：`applyPendingSelection` 读 `transactionList.itemAtIndex(row)` **未加 `if (transactionList)` 守卫**，而 reset handler 有该守卫。二者暴露面相同（`selectRow` 先解引用同一 id，能 park 就说明 list 已存在），且本应用页面是 StackLayout **常驻子项**、运行期不销毁 ⇒ **不构成活缺陷**；按 scope freeze **不做形式性改动**，仅记录。

### R3. Binding Safety Contract 逐条证明

| 契约 | 结论 | 证据 |
| --- | --- | --- |
| **A. Reset before callback** | `currentIndex = -1`、`selectedRow = -1`、`selectedEntry` 空、**pending = -1**、detail = no-selection；**旧 row 不复活** | **Scenario Q1**（130–131）：park 后**同一 turn** 替换模型；131 断言行 0 **存在于**新模型却**未被选中** |
| **B. Latest selection wins** | 最终只能是**最新行**或**显式 no-selection**；绝不回到旧行 | **Scenario Q2**（132–133）：row 99 park → 同 turn row 2（真实入口）⇒ 133 断言仍为 row 2 且 detail 逐字段一致 |
| **C. Failed replacement** | model 未 reset ⇒ 有效 pending/current selection **继续有效**，**不无条件清 selection** | **Scenario P3** 不变（失败替换保留 selection + detail）；本轮**未**把 P3 改成"失败也清" |

### R4. Targeted Deferred-path Test（**新增 Scenario Q**）

`--qml-nav-check` 新增 **Scenario Q**（stages 129–134，`kLastStage = 134`；`scenarioOrder` 追加 `"Q"`；PASS 文案追加 `Q`）。

- **进入 pending 路径的方式**：`transactionsPage.selectRow(row)` —— **页面自身的入口函数**（与 `onCurrentIndexChanged` 调用的**同一个**），**未新增任何 production API**（遵守 §5）；空模型 / 越界行 ⇒ `itemAtIndex` 必为 null ⇒ **确定性地**走 park 分支。**非空洞性**由同 turn 断言 `pendingSelectionRow == 0` **保证**（若委托已实例化则该断言失败并显式写明 "this scenario would be vacuous"）。
- **同一 event-loop turn 内触发 authoritative reset**（§5 步骤 4）：`runDemoBatch()` 与 park 在**同一个 stage 回调**内执行，`Qt.callLater` 回调不可能插入；`modelReset` 经**同线程直连**同步清 pending。
- **不用 sleep 猜**：所有断言要么在**同 turn**、要么在**固定 settle tick 之后**读取真实状态。

**Q1 runtime result = PASS**：
```text
NAV [scenario Q1]: a selection request is parked in the deferred path (pending row 0, empty model)
NAV [scenario Q1]: the reset ran before the deferred completion; row 0 exists in the new model and is NOT selected — a stale selection cannot resurrect across a reset
```

**Q2 runtime result = PASS**：
```text
NAV [scenario Q2]: the older deferred request did not clobber the newer explicit selection (row 2 still selected, detail mapping intact)
```

**RED 判别力证明（mutation probe，已完整回滚）**：把 callback 临时改成**捕获行号**的缺陷版（`Qt.callLater(function() { page.applySelectionOfRow(row); })`），重建后：

```text
RED_EXIT=1
GEOFAIL: NAVFAIL scenario Q: a STALE deferred selection resurrected into the replacement model (selectedRow=0)
NAV SCENARIOS: ... P PASS, Q NOT RUN
```

⇒ §D3 的 P0-2 隐患类别（**stale capture 跨 reset 复活**）**被 Scenario Q 捕获**。随后 `git checkout -- src/ui/qml/pages/TransactionsPage.qml` **完整回滚**（`grep -c applySelectionOfRow = 0`，工作树仅 `src/main.cpp` 被改），重建后 **Q PASS / EXIT=0**。

**Coverage boundary（如实申报，§6 允许）**：
- **pending → pending**（两次连续不可满足请求且无 reset）**未单独覆盖**：其结果（no-selection）与 stale capture **不可区分** ⇒ **不构成判别性证据**，不做假测试；
- 真实 4 行批次**会把全部委托实例化**，故 park 路径由页面的 `selectRow` 直接进入，**不是键盘移动**产生（键盘/鼠标实机仍在 D6）。

### R5. 回归（本轮全部重跑）

- **P1–P5 全 PASS**（原文不变）：P1 显式选中 / P2 导航保留 / P3 失败替换保留 / P4 成功替换失效（不重选第 0 行）/ P5 clear·新批次失效。
- **ENR 回归**：P4 detail 仍中性；O broadcast 行 `status=预期无响应` 中性。
- **ProtocolError 回归**：O protocol 行 `status=协议错误 hasIssueDetail=1`（正交保留）。
- **Detail mapping 回归**：两趟 targeted 均 `DETAIL MAPPING: row=2 status=CRC 错误 issue=<none>`。
- **Geometry 回归**：**16 steps（12+2+2）/ 20 段**，**0 GEOFAIL**；选中态两尺寸：1024×720 → 列表 911×477、detail 911×56 @y=540；1000×700 → pane 911×600、列表 **887×457（≥216）**、detail **887×56** @y=520；detail 非零、不重叠、列表 viewport 达标。

### R6. 门禁（本轮真实输出）

```text
build（Ninja）→ 0 error（仅既有无害告警）
--qml-smoke-test       → EXIT=0；stderr 卫生 0
--qml-nav-check        → EXIT=0；scenarios …/O/P/Q 全 PASS；M DEFERRED BY DESIGN；stderr 卫生 0
--qml-geometry-check   → EXIT=0；16 steps（12 standard + 2 C4 + 2 D3）/ 20 段；0 GEOFAIL；stderr 卫生 0
ctest --preset debug-local → 100% tests passed, 0 failed out of 26
git diff --check       → PASS
```

### R7. Result

- **P0-1 = Case A**：真实计数 **16 = 12 + 2 + 2**（steps；20 段因 diagnosis tab sweep），**无缺失覆盖**；**仅文档数字纠正**（PROJECT_STATUS Test 状态行）。
- **P0-2 = 实现原本安全**（执行时读取最新 pending；reset 四清；无 stale capture）⇒ **未加 generation token、产品代码零 diff**；**新增 Scenario Q 以机器证据替代源码论证**，并以 mutation probe 证明其**判别力**。
- **Manual Review = PENDING**（鼠标/键盘实机 + 视觉仍在 D6）；verified LKGC **不变 = `bc754be`**；未 push。

## D4. Next（D3 Review HOLD 之后的追加）

- **等待 M9-D D3 Re-review（用户）**；通过后进入 **D4 — Phase 1 已接受的 Diagnosis existence cue + 必要的 Transactions polish**（filters/search 继续 DEFER）。


## D3 Re-review = PASS（用户，2026-09-18，append-only）

> 归档本轮 Review 结论与两条 P0 的**最终口径**。不重写 §R（correction）原文。

- **M9-D D3 Re-review = PASS**；两个 HOLD P0 均已闭环。

### P0-1 最终术语（冻结口径）

**geometry coverage 未丢失**。最终术语：

- **geometry steps = 16**（覆盖度口径）：**12 standard**（6 active × 2）+ **2 C4 targeted** + **2 D3 targeted**；
- **printed geometry segments = 20**（测量次数口径）：diagnosis 的 2 个 step 各 sweep 3 个 tab 所致。
- 两个口径**不是同一个计数**，报告时**必须分别写出**（该规约自此冻结）。

### P0-2 最终口径

deferred selection stale-callback 由**源码审计 + Scenario Q1/Q2 + mutation probe** 三重闭环（`RED_EXIT=1`："a STALE deferred selection resurrected into the replacement model (selectedRow=0)"）。

**非阻塞 note（记录，非 correctness contract）**：`Qt.callLater` 对**同一函数引用**的 duplicate-call **合并（coalescing）**是 Qt 的调度行为，**不得冻结为正确性契约**。本轮正确性**实际依赖**的是：

1. callback **执行时**读取最新 `pendingSelectionRow`；
2. `modelReset` 清 pending；
3. newer selection 使旧 request 无效（覆写 pending / 立即完成）。

**不得把内部字段形式（如 `pendingSelectionRow` 的具体结构、generation 计数有无）冻结为 public contract**——Q 的断言读取的是**行为结果**（selection/detail/可见性），字段形式属于实现细节。

- **D5（Legacy retirement）**、**D6（geometry/evidence/manual）** 未开始。

## D4 — Diagnosis Existence Cue（Implementation Record，2026-09-18）

### D4.0 D3 Re-review = PASS（用户）+ 归档

- 见 §「D3 Re-review = PASS」：P0-1 最终术语冻结（**geometry steps = 16 / printed segments = 20**，12 standard + 2 C4 + 2 D3，两口径必须分别报告）；P0-2 由源码审计 + Scenario Q1/Q2 + mutation probe 闭环；**非阻塞 note**：`Qt.callLater` duplicate-call coalescing **不是 correctness contract**（正确性依赖：执行时读最新 pending / reset 清 pending / newer selection 使旧 request 无效），**不得把内部字段形式冻结为 public contract**。

### D4.1 Mandatory Re-read（真实源码）

- **T019 Phase 1 §14**：Diagnosis 关系裁定 = **方案 B**——Transactions 页只提供「当前 session 诊断可用」**存在性线索**，读 `hasBaselineDiagnosis`，复用 M9-C 冻结文案；**禁止** selected-transaction diagnosis（DEFER 新 milestone）、禁止把 Baseline 改成单条语义。
- **Dashboard 先例（M9-C C4）**：`dashboardDiagnosisCue` Label = `visible: observedCount > 0`，两态文案冻结，`DS.textSecondary`，wrap，无 CTA。
- **Controller 真实 diagnosis 状态**（`AnalysisController.h/.cpp`）：`Q_PROPERTY(bool hasBaselineDiagnosis NOTIFY diagnosisChanged)`（**唯一**允许的 authority）；`clearDiagnosis()`（Q_INVOKABLE，**diagnosis-only**：batch/rows/statistics/source 不动、batch revision 不变）；`invalidateAiForBatchChange()` → `clearDiagnosisState()`（每个 `setEntries` 发布点调用 ⇒ **新批次使 baseline 失效是 Controller 既有语义**）。
- **Harness 现状**：Scenario L（baseline 持久化/失效）、Dashboard cue 断言（可见性 = observed>0、两态文案、读 authority）、D3 P/Q。**结论：复用既有 authority，不新建状态**。

### D4.2 Cue Authority（§4）

- **唯一 authority = `analysisController.hasBaselineDiagnosis`**（真实既有 property，名字与源码一致）。
- 页面**未**从 baseline text 是否为空推断、**未**解析 finding 文本、**未**从 selected transaction 推断、**未**建立 page-local diagnosis copy、**未**新增任何 Controller/model property（`git diff --name-only` 证实 Controller 零 diff）。

### D4.3 两态文案（§5）

- **A 无 baseline**：`尚未运行基线诊断。`
- **B 已有 baseline**：`已有基线诊断结果，可在诊断工作区查看。`
- 与 Dashboard cue **逐字相同**（同一冻结语义的直接复用）；**未**抽新 DS component（只有一个消费者，无重复，§5 的抽象门槛未触发）。
- **可见性规则同样复用 Dashboard 先例**：`visible: observedCount > 0`——无 session 时该页的空态提示（「暂无通信记录」/「选择一条事务查看详情」）是 owner，不叠加一条孤儿 cue。

### D4.4 Non-command Boundary（§6）

纯 `Label`：**无 Button、无 TapHandler、无 MouseArea、无 workspace index mutation、无任何 onClicked**；不可聚焦、不抢键盘焦点（§22）；点击不切 Diagnosis、不 runBaseline、不清诊断、不启动 AI/Agent。**Rail 仍是唯一 navigation authority**。

### D4.5 Selection / Model Replacement Independence（§7/§8）

- **Selection 独立**：cue 只绑定 Controller 属性；`selectedRow/-1、0、2` 时同 authority ⇒ 同 cue 状态（R3 机器实证：选中 row 2 后 `hasBaselineDiagnosis` 仍 false、cue 文本不变）。
- **Model replacement 独立**：**页面未绑定任何 `modelReset → 清 cue` 逻辑**；cue 只反映 Controller 当前 state。新批次发布时 Controller 自己的 `invalidateAiForBatchChange` → `clearDiagnosisState` → `diagnosisChanged` ⇒ cue 跟随翻回 no-baseline（R13 实证；**不是页面自实现 invalidation**）。

### D4.6 Diagnosis Lifecycle Freeze（§9/§15）

- 进入/离开 Transactions 不 cancel AI、不 invalidate Agent、不清 baseline、不 run baseline、不切 Diagnosis tab——R4 在 Transactions→Diagnosis→Dashboard→Transactions 往返后 cue 与 authority 均不变（且 selection 也保留）。
- **DiagnosisPage.qml zero diff**（`git diff --name-only` 证实）；cue 全部落点在 Transactions presentation。

### D4.7 Dashboard Precedent Reuse（§10）

只复用 **existence-cue semantic boundary**（两态文案 + `observedCount > 0` 可见性 + 只读 authority）；**未**复制 attention/distribution/statistics 任何 presentation，未复制 Dashboard 结构。

### D4.8 Placement（§11）

外层 ColumnLayout 的**最后一个子项**（detail 区之后、页底）：`Label { objectName: "transactionsDiagnosisCue"; Layout.fillWidth; visible: observedCount > 0; 两态文本; DS.textSecondary; wrap }`。**低层级辅助信息**：不进表头、不占视觉中心、不是 card。不可操作 ⇒ 不参与 focus 顺序（§22）；有明确文字 ⇒ 无颜色/图标单通道（§22）；**未**堆额外 Accessible metadata（§22）。

### D4.9 1000×700 Budget（§12/§14）

| 尺寸 | 状态 | transactionsList | transactionDetail | cue |
| --- | --- | --- | --- | --- |
| 1000×700 | 批次+选中 | **887×437**（≥216，余量 221） | 887×56 @y=500 | 887×**12** @y=564（detail 底 556 < 564，**无重叠**） |
| 1024×720 | 批次+选中 | 911×457（D3 为 477） | 911×56 @y=520 | 911×12 @y=584 |
| 1000×700 | 空（standard） | **887×501 —— 与 D3 逐值相同**（隐藏 cue **不占空间**，ColumnLayout 跳过 invisible 子项） | — | 不可见 |

**budget 全部成立**：list ≥ 216、detail 完整、cue 完整、无 overlap、无 clipping；未缩小 row height、未压扁 detail、无硬编码 maximumHeight。

### D4.10 Polish Decision（§13）

按规程先取证（geometry dump 前后对比 + 断言 + 源码审计），四类允许修复的触发条件**全部不存在**：cue placement 未引发 spacing defect（空态逐值不变；批态仅 natural-height 占位）、D3 选中态无 visual collision（cue 在 detail 区块之下、由 layout spacing 分隔）、detail/cue 层级清晰（detail=行级字段、cue=session 级存在性、同 `DS.textSecondary`）、1000×700 无真实拥挤（list 余量 221px）。⇒ **No additional polish required**（§13 明示可接受）。未换颜色、未改 typography、未改列宽、未动 DS。

### D4.11 RED → GREEN（§17）

- **RED（D3 tree + D4 harness）**：`RED_EXIT=1`、`GEOFAIL: scenario R1 no baseline: transactionsDiagnosisCue not found`、`NAV SCENARIOS: … Q PASS, R NOT RUN`——**expected D4 missing feature**；P/Q 及全部既有场景未破坏。
- **GREEN**：QML 落地后 `nav EXIT=0`，R 全 PASS。

### D4.12 Scenario R（§21，stages 135–146，kLastStage=146，scenarioOrder + PASS 文案 + R）

| 步骤 | 断言 | 结果 |
| --- | --- | --- |
| R1 | `runDemoBatch` → rows=4、authority=false、cue=「尚未运行基线诊断。」 | `NAV [scenario R1] … PASS` |
| R3 | 选中 row 2（`currentIndex`）⇒ authority 仍 false、cue 文本不变 | `NAV [scenario R3] … unchanged` |
| R2 | 真实 `runBaselineDiagnosis()` ⇒ authority=true、cue=「已有基线诊断结果，可在诊断工作区查看。」（**不解析 baseline text**，断言的是冻结整行） | `NAV [scenario R2] … available state` |
| R4 | Transactions→Diagnosis(4)→Dashboard(1)→Transactions(5) ⇒ authority 不变、cue 不变、selection 仍 2 | `NAV [scenario R4] … same authoritative state` |
| R5 | `clearDiagnosis()` ⇒ cue 回 no-baseline；**rows=4、observedCount=4、selection=2 不被误清**（17 个快照字段稳定） | `NAV [scenario R5] … untouched` |
| R13 | baseline→`runDemoBatch`（批次/revision 变化）⇒ authority 翻 false、cue 跟随；rows=4 | `NAV [scenario R13] … by the Controller's own revision semantics` |

清理：R 末尾 `clearResults()` 复位。**未把 P/Q 塞进 R 重写**；`clearDiagnosis` 复用 B5 冻结语义，**未**自造 Agent 对称清理。

### D4.13 Geometry（§24）

- **steps = 16（12 standard + 2 C4 targeted + 2 D3 targeted）不变**；**printed segments = 20**（diagnosis tab sweep，口径分列——Re-review 冻结的术语）。
- **未新增 D4 targeted geometry pass**：cue 的几何断言（存在 / 会话时 nonzero+inside+不与 detail 重叠 / 空 session 时零占位）**并入既有 D3 targeted selected-detail 趟与 standard transactions 趟的断言集**（它们已携带批次/空态上下文），因此不产生新的分类。dump 名单新增 `transactionsDiagnosisCue`。
- 判别依据：`cueExpected = observedCount > 0`（同 Dashboard 可见性规则），空态不判 nonzero（invisible 项在 ColumnLayout 下不参与分配）。

### D4.14 Regression（§25/§26/§27）

- **P1–P5 全 PASS**、**Q1/Q2 全 PASS**；cue 未触碰 `currentIndex/selectedEntry/pendingSelectionRow/modelReset` 生命周期（`TransactionsPage.qml` 的 diff 仅一个 Label 块）。
- **ENR/ProtocolError/Unsupported/正交**：Scenario O/P 原文 PASS；cue 文本只依赖 `hasBaselineDiagnosis`，**不读** ENR/ProtocolError/attention/anomaly（§26：不同轴）。
- **其它页 freeze**：Legacy/Dashboard/Communication/Replay/Diagnosis **zero diff**。

### D4.15 Gates（§28）

```text
build（Ninja）→ 0 error（仅既有无害告警：2 处 unused variable，HEAD 已存在、非本轮引入）
--qml-smoke-test      → EXIT=0；stderr 卫生 0
--qml-nav-check       → EXIT=0；scenarios A/B/D/E/F/G'/H/I/J/K/K'/L/N/O/P/Q/R 全 PASS；M DEFERRED BY DESIGN
--qml-geometry-check  → EXIT=0；16 steps / 20 segments；0 GEOFAIL
ctest --preset debug-local → 100% tests passed, 0 failed out of 26
git diff --check      → PASS
```

### D4.16 Negative Scope（§29）

无 filters/search、无 proxy model、无 selected-transaction diagnosis、无 finding count、无 baseline text 解析、无 AI explanation、无 Agent copy、无 Controller 新 property、无新 model role、无 Legacy retirement、无 StatisticsOverview 删除、无 Dashboard recent transactions、无 M10/M11/M12、无 style/token cleanup。

### D4.17 Files Changed（§39 前置）

`src/ui/qml/pages/TransactionsPage.qml`（+1 个 cue Label 块，含边界注释）、`src/main.cpp`（`assertTransactionsDiagnosisCue` helper + Scenario R stages 135–146 + geometry cue 断言 + dump 名单 + kLastStage/scenarioOrder/PASS 文案）；docs。**Controller / TransactionListModel / Core / DiagnosisPage / 其它页 / tests / CMake / deploy 零 diff。**

### D4.18 Result

- D4 完成：**存在性 cue 以最小形态落地**（一个纯文本 Label，两态冻结文案，authority = 既有 `hasBaselineDiagnosis`），selection/批次/导航三重独立由 Scenario R 机器实证；**无 polish 需求**；filters 继续 DEFER。
- **Manual Review = PENDING**（视觉 + 鼠标/键盘实机在 **D6**）；verified LKGC **不变 = `bc754be`**；未 push。

## D5. Next

- **M9-D D4 Review（用户）**；通过后 **D5 — Legacy retirement**（替换 index 0、零 index churn、紧凑重排回 6 项/设备@5）。
- **D6（deploy/evidence/manual candidate）** 未开始；M9-E/F 未开始。
## 39. Next（Phase 1 之后的追加）

- **M9-D D1 Review（用户）** → **D2** → D3 → D4（默认不做）→ D5 → D6；每阶段独立 Review/提交。
- **M9-E / M9-F 未开始**；M9 整体 IN PROGRESS。


- **M9-D Phase 1 Review（用户）**；批准后按 §33 从 **D1** 开始实施（每阶段独立 Review/提交）。
- **M9-E（Branding/Icon/Packaging）**、**M9-F（Final Manual Visual Acceptance）** 未开始；**M9 整体 IN PROGRESS**。
