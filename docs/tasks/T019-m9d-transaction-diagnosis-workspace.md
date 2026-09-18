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

## D3. Next

- **M9-D D2 Review（用户）**；通过后 **D3 — selection + detail**（须先按 Phase 1 Review guardrail C 确定 detail data-access seam；selection lifetime 三分支冻结；geometry 保持 12 趟）。
- **D4（filters，默认不做）**、**D5（Legacy 退役）**、**D6（geometry/evidence/manual）** 未开始。

- **M9-D D1 Review（用户）**；通过后 **D2 — 原子迁移 + 启位**（Transactions 表整块迁入 + `事务` 启位 + Legacy 同提交移出事务 pane；geometry 届时扩为 **6 active × 2 = 12 standard passes**；每阶段独立 Review/提交）。
- **D3（selection/detail）**、**D4（filters，默认不做）**、**D5（Legacy 退役）**、**D6（geometry/evidence/manual）** 未开始；M9-E/F 未开始。

## 39. Next（Phase 1 之后的追加）

- **M9-D D1 Review（用户）** → **D2** → D3 → D4（默认不做）→ D5 → D6；每阶段独立 Review/提交。
- **M9-E / M9-F 未开始**；M9 整体 IN PROGRESS。


- **M9-D Phase 1 Review（用户）**；批准后按 §33 从 **D1** 开始实施（每阶段独立 Review/提交）。
- **M9-E（Branding/Icon/Packaging）**、**M9-F（Final Manual Visual Acceptance）** 未开始；**M9 整体 IN PROGRESS**。
