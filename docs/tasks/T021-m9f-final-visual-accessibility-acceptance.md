# T021 — M9-F Final Manual Visual / Accessibility Acceptance

> **状态：IN PROGRESS — Phase 1 = PASS；F0 measurement = **COMPLETE（冻结，不再重开）**；F0 Final Re-review = **HOLD（single scope blocker：P0-5 Agent TextArea Tab trap 曾漏出 F1 scope，已修正为 A–H）**；权威结论 **P0=5 / P1=3 / P2=0 / GAP=1（known non-blocking）/ N/A=2 / PASS=5** → **F1 REQUIRED（scope A–H 已冻结；admission rule 已冻结）**；Implementation = NOT STARTED。**
> 上游边界：M9-E（T020）= ✅ COMPLETE（verified LKGC = `4cb6e9d`；最终 Release package `ModbusLens-2.0.0-windows-x64.zip` 已人工验收）。M9-F **不得**重开：version/icon/package architecture/ZIP workflow/installer/signing/publication/Transactions IA/Diagnosis redesign。

## 0. V2 Protocol 对应

Learning / Final Acceptance Design Gate（本轮，docs-only）→ Review → F1（仅当 audit 发现 blocker）/ F2（final Release visual/evidence candidate）/ F3（manual final acceptance + M9 closure）→ M9-F Final Closure。本轮**零 production 改动**。

## 1. Preflight（2026-09-19）

HEAD `a18ed8c`、main、clean、verified LKGC `4cb6e9d`、CMake project VERSION = 2.0.0、tag object `2cee626` / `v1.0.0^{commit}`=`ae067ab`、v2.0.0 absent、origin/main `a40d935`（ahead 78 / behind 0）、`git diff --check` PASS —— 全部相符。

## 2. M9-E Closure Boundary（§1，实读确认）

PROJECT_STATUS / BACKLOG / T020 Final Closure 实读：M9-E = ✅ COMPLETE；final Release packaging **accepted**（`ModbusLens-2.0.0-windows-x64.zip`，40,569,927 B）；verified LKGC = `4cb6e9d`；2.0.0 已 configured **但 NOT published**（v2.0.0 absent）。**M9-F 不得重开**：version / icon / package architecture / ZIP workflow / installer / signing / publication。Tab-only focus traversal（M9-D 登记）+ 全局 focus-chain audit（M9-E 登记）+ 最终跨页面 visual acceptance = **本任务**。

## 3. Task Ownership（§2）

BACKLOG 有 M9-F milestone 行（"Learning / Final Acceptance Gate，含 global accessibility / Tab focus-chain audit 登记"）但**无 task doc**；`docs/tasks/` 止于 T020 ⇒ **T021 = 真实下一个空闲 id**，本文件即 canonical task doc（未 append 到已完成 T020）。

## 4. Focus Architecture Inventory（§3，真实源码，2026-09-19 @ `a18ed8c`）

| 文件 | 焦点相关事实（实读） |
| --- | --- |
| `NavigationRail.qml` | delegate = **plain `Item`**（非 Control）：`MouseArea`（click → activate）+ `Keys.onReturnPressed/onEnterPressed/onSpacePressed`（仅当 delegate 持有 activeFocus 才触发）+ `enabled: navEnabled`（禁用传导到子级：无鼠标/键盘/焦点）+ `focusPolicy: navEnabled ? StrongFocus : NoFocus`（Qt 6.7+ QQuickItem 属性）。**D6 审计实测**：点击 rail 后 activeFocusItem 仍是 `dashboardRunDemo`（MouseArea 不夺焦）——**rail 键盘激活路径（Enter/Space）声明性存在但从未被 focus 到达（现状 dead path）**；Tab 可达性未设置 `activeFocusOnTab`，实际行为待 audit 实测。 |
| `AppButton.qml` | 基于 **Button**（Control）+ `focusPolicy: Qt.StrongFocus`；**焦点视觉 = 平台 outline 保留**（注释明示不回归）；hover/down/tone 状态齐全。 |
| `TransactionsPage.qml` | ListView `focus: true` + `onCurrentIndexChanged → selectRow`；`Keys.onPressed`（Home/End）；delegate `Keys.forwardTo: [transactionList]`；TapHandler `onTapped`：currentIndex → `forceActiveFocus()`（M9-D D6 修复）；**键盘契约已冻结且人工 PASS**（M9-D）。 |
| `DiagnosisPage.qml` | 3 × TabButton（Baseline/AI/Agent）+ Buttons（Run/Clear/AI/Agent）+ TextArea（Agent 问题草稿，页本地）。 |
| `CommunicationPage.qml` | 2 × ComboBox（端口/波特率）+ Buttons（Refresh/Connect/Disconnect/Read）。 |
| `ReplayPage.qml` | AppButton（Load Replay）+ FileDialog。 |
| `DashboardPage.qml` | AppButton（Run Demo）。 |
| `Main.qml` | AppButton（Clear Results，AppBar）。 |
| `OutcomeDistribution.qml` | **既有 accessibility**：`Accessible.role: Graphic` + `Accessible.name`（distribution.accessibleSummary）。 |
| FocusScope | 应用内**无显式 FocusScope**（contentItem 隐式 scope；E3 correction 已证明 hidden Control 可持有 activeFocus 的行为）。 |

## 5. M9-F Scope Definition（§4）

**目标**：A. 最终跨页面人工视觉一致性；B. 全局 keyboard focus traversal 与基本 accessibility sanity；C. Release candidate 最终人工验收；D. M9 milestone closure evidence。
**非目标**：新 feature / theme redesign / navigation redesign / transaction feature extension / Diagnosis redesign / installer·release pipeline / version·icon·package 架构变更。

## 6. Final Workspace Inventory（§5，冻结）

0 Transactions（列表+detail+cue）/ 1 Dashboard（metrics+distribution+attention+cue+Run Demo）/ 2 Communication（端口/波特率 ComboBox + Connect/Disconnect/Read + validation 错误态）/ 3 Replay（Load + source/notice/error 态）/ 4 Diagnosis（3 tabs：Baseline/AI/Agent；确定性 offline 态）+ Device（5，disabled）。Legacy = retired。逐 workspace 明细见 §7 表。

## 7. Interactive-control Inventory（§6，per-control 表）

| workspace | 控件 | 用户动作 | 键盘路径 | 焦点可达 | 备注 |
| --- | --- | --- | --- | --- | --- |
| AppBar（全局） | Clear Results（AppButton） | 清空会话 | Tab + Enter/Space | 待实测 | hidden-page 不应响应（§9） |
| Rail（全局） | navItem ×6（5 enabled + Device disabled） | 切换 workspace | **现状未达**（Enter/Space dead path 待 audit 确认） | 待实测 | disabled Device 不可 activate（guard-tested）；focus 与 selected 视觉分离 |
| Dashboard | Run Demo（AppButton primary） | 产 demo 批次 | Tab + Enter/Space | 待实测 | M9-D 隐藏页持焦 RCA 的主角 |
| Communication | 端口/波特率 ComboBox；Refresh/Connect/Disconnect/Read Buttons | 串口操作 | Tab + 方向键（ComboBox 内） | 待实测 | 无硬件 = disabled/default 态 |
| Replay | Load Replay（AppButton）+ FileDialog | 加载 .mlog | Tab + Enter | 待实测 | selectedFile ≠ loaded source（冻结） |
| Diagnosis | 3 TabButton + Run/Clear/AI/Agent Buttons + Agent TextArea | 诊断操作 | Tab + Left/Right（tabs）+ 文本编辑 | 待实测 | TextArea 内方向键/Home/End 必须保持文本编辑语义 |
| Transactions | ListView（delegate TapHandler + Keys） | 选行/键盘移动 | 已冻结（Up/Down/Home/End）+ Tab 进入/离开 = 本轮审计 | 已持焦（focus: true） | 详见 §11 |

空态/错误态：Transactions（暂无通信记录/选择提示/cue 隐藏）、Replay（notice/error 共存）、Communication（validation error）、Diagnosis（未运行/AI 未配置）——inventory 全列，visual matrix 覆盖。

## 8. Tab-focus Audit Design（§7）

**方法**：启动应用（默认 Transactions）后**连续 Tab / Shift+Tab** 人工走查 + （低成本时）harness 记录 activeFocusItem 序列；每步记录：focus 落点/可见指示/是否需要操作。

**判据（设计冻结）**：
1. focus 能到达五个 active workspace 的**主要 controls**（Rail 或页内主按钮/列表至少一条键盘路径成立）；
2. 顺序**大体符合视觉/任务顺序**（不要求与 Tab 理想链逐项一致）；
3. **hidden workspace 控件不得出现在 Tab 链**（StackLayout hidden children 默认不在 focus chain——audit 实证）；
4. **disabled Device 不得进入 focus chain**（enabled: false 传导）；
5. 纯展示 Label/Rectangle 不得成为 Tab 停靠点；
6. Transactions list 可由键盘获得 focus 且 focus 指示可见；
7. Shift+Tab 能反向遍历；
8. **hidden 控件不得吞键**（M9-D RCA：hidden dashboardRunDemo 曾持 activeFocus——audit 必须验证当前页操作不被隐藏焦点干扰）。

**已知现状（诚实申报）**：activeFocusOnTab 未显式设置；rail Enter/Space 为 dead path；Tab 链的真实顺序/覆盖**未知**——audit 是测量，不是验证预设。

## 9. Focus Persistence Semantics（§8）

- **business selection persistence**（已冻结）：navigation 不改 domain state。
- **keyboard activeFocus**：**不要求跨 workspace 永久保存**；切页后 focus 落在"合理位置"即可。
- **规则**：①切页后当前页可正常操作（无隐藏控件吞键）；②transactions list 的 `focus: true`（视图级持焦）与 `currentIndex`（业务选择）分离——两者语义已由 M9-D/E 冻结，M9-F 不改；③若 audit 发现 hidden 控件持焦吞键，按 §22 分类。

## 10. NavigationRail Keyboard Audit（§9）

**调查项（实读已知 + 运行时待测）**：①`focusPolicy: StrongFocus` 在 **plain Item**（Qt 6.7+ QQuickItem 属性）上的实际效果——点击是否夺焦（D6 证据：否）；②Tab 是否到达 rail items（activeFocusOnTab 默认值行为）；③到达后 **Enter/Space 是否真激活**（Keys handlers 存在但从未被 focus 到达）；④disabled Device 在 focus chain 中的表现；⑤**focus visual 与 selected visual 的区分**（selected = 表面+3px accent+bold；focus = ？——当前无专门 focus 视觉，audit 需评估是否可辨认）；⑥方向键是否应在 rail 内上下移动（**现状：未实现**——rail 无 Up/Down Keys handler；audit 如实记录，改进属 P1/P2 分类）。

## 11. Transactions Focus Boundary（§10，冻结边界）

**已冻结且人工 PASS**（M9-D，不再动）：mouse selection / Up / Down / Home / End / boundary / detail sync / selection lifecycle。
**M9-F 仅审计**：①Tab/Shift+Tab 能否合理进入/离开 list；②list 持焦时 focus indicator 是否可辨认（Qt 默认 focus outline 或 view 内 delegate 视觉）；③Enter 在 list 上的行为（现状：无特殊处理，不误触其它控件）。
**不得**：重设计 selection model/detail/keyboard movement。

## 12. Text-input / Shortcut Conflict Audit（§11）

- **Diagnosis Agent TextArea**：方向键/Home/End/PageUp 必须保持**文本编辑语义**（光标移动），不得被页面级 Keys 截获（现状：TransactionsPage 的 Keys 是 view 级局部；DiagnosisPage 无页面级 Keys——audit 实证）。
- **Communication ComboBox**：下拉打开时方向键/Enter/Escape 语义（Qt ComboBox 内建）。
- **Enter/Space 误触**：hidden control（如隐藏页按钮）不得因 Enter 被触发（§9 规则）。
- **本轮只设计测试，不修**；发现截获即 P0/P1 分类。

## 13. Accessibility Naming Audit（§12）

- **既有**：OutcomeDistribution（Graphic + name）；所有 AppButton/Button/TabButton 有可见 text（可理解名称来源 ✓）；ComboBox/TextField 有 label 邻接（audit 记录）。
- **纯图标 interactive control**：**当前不存在**（全部 interactive 控件有文字）——如实记录。
- **不为 Label 堆 Accessible metadata**（形式化无益）；rail delegate 的 Label 有可见文字 ✓。
- ListView delegate：可理解性来自行内容本身；Accessible.name 补充**非本轮必需**（如 audit 发现 screen-reader 级缺陷 → P1/P2 分类）。

## 14. Disabled / Hidden Semantics（§13）

- **Device（disabled）**：不得经 mouse/keyboard/Tab activate（`enabled: false` 传导 + activate guard **已 guard-tested**——audit 复验）。
- **Hidden StackLayout pages**：不得继续接收用户键盘操作（Tab 链不含 + 无 global shortcut 误触）；**page-local business state 可继续存在**（Agent 草稿/selection 等——M9-D/E 冻结）。
- **hidden Control 持焦**：M9-D RCA 的历史模式——audit 显式检查 hidden 控件是否吞键。

## 15. Visual Acceptance Matrix（§14）

| workspace | 1024×720 | 1000×700 | 状态组合 |
| --- | --- | --- | --- |
| Transactions | ✅ | ✅（密度页） | empty / demo selected / ENR / ProtocolError / cue available（**复用 M9-D accepted evidence** + final Release 最小回归） |
| Dashboard | ✅ | ✅（密度页） | demo populated |
| Communication | ✅ | （如需要） | disconnected/default + validation error |
| Replay | ✅ | （如需要） | empty + **package demo_v1.mlog loaded** |
| Diagnosis | ✅（3 tabs 全） | ✅（密度页） | Baseline 空态/运行后 + AI/Agent not-configured 诚实态 |

不拍无意义重复图；与既有 M9-D/E accepted evidence 互补（final Release 基底 + 跨页一致性）。

## 16. Diagnosis Tabs（§15）

三个 tab（Baseline/AI/Agent）visual acceptance 全覆盖；**不要求真实 Provider**：Baseline = runDemoBatch + runBaselineDiagnosis（确定性）；AI/Agent = **not-configured/unavailable 真实态诚实呈现**（不伪造结果）。

## 17. Communication Boundary（§16）

无真实串口硬件。人工至少：disconnected/default state + 无硬件可触发的 validation/error（如空端口 Refresh 行为）。**不得声称 real serial hardware PASS**。

## 18. Replay Final Visual States（§17）

empty/no source + **成功加载 package-contained demo_v1.mlog**（Release package 内 sample——portable 证明的一部分）；source basename presentation、transactions/statistics 无布局异常。**冻结不变**：selectedFile ≠ loaded source；failed replacement preserve 语义由自动测试覆盖（不要求视觉截图）。

## 19. Transactions Final Visual States（§18）

empty / demo selected / ENR / ProtocolError / Diagnosis cue available——M9-D 已视觉 PASS；M9-F **复用既有 accepted evidence** + final Release 最小回归集（package-extracted 身份下快速 sanity），不全部重拍。

## 20. Dashboard Final Visual State（§19）

demo populated 一态：metrics / distribution / outcomes / attention cue / diagnosis existence cue 层级稳定。**不恢复** recent transactions preview（superseded）。

## 21. Cross-page Consistency Audit（§20）

人工检查：page margins / SectionHeader / title-subtitle hierarchy / surface·radius / control heights / empty-state tone / error text hierarchy / rail spacing / disabled style / **focus indicator**。目标 = **发现明显不一致**，非 pixel-identical。

## 22. P0/P1/P2 Admission Rule（§21）

- **P0**：functional/accessibility blocker（无法启动/隐藏控件吞键/键盘死路/数据破坏）→ 必须进入 F1 implementation。
- **P1**：clear usability/visual defect（focus 不可见/顺序严重违和）→ 进入 F1（最小修复）。
- **P2**：optional polish（rail 无方向键移动、focus 视觉风格等）→ **记录 DEFER，不阻断 M9 closure**。
- **禁止**"最后一轮顺手更漂亮"；P2 只记录。

## 23. Screenshot Oracle Boundary（§22，冻结）

machine screenshot checks 只证明 dimensions/landscape/nonblank/identity/integrity——**不替代** manual visual correctness；filename 非 state oracle（M9-C orientation RCA 教训持续有效）。

## 24. Release Candidate Basis（§23）

M9-F manual acceptance 基于 **Release portable package**（E4 产物，`4cb6e9d` tree）。**若 F1 产生 behavior fix**：必须重新 Release build/package 并对新 tree 重新接受（重新走 minimal 机器门禁 + manual delta）。**Debug PASS ≠ Release PASS**。

## 25. Package / Publication Freeze（§24/§25）

M9-F 不改：2.0.0、PE metadata、icon、package naming、ZIP semantics；**不创建 v2.0.0 tag**、不 push、不 publish。M9 COMPLETE ≠ 2.0.0 published。

## 26. Final Manual Acceptance Plan（§25）

最终用户确认（基于 Release portable package，解压新目录）：five active workspaces visual / rail final IA / disabled Device / **Tab traversal** / **Shift+Tab traversal** / **focus visibility** / **no hidden control swallowing keys** / Transactions keyboard 仍工作 / resize 1024×720 + 1000×700 / Diagnosis tabs visually usable / Replay packaged sample usable / final Release launch+identity。**不要求**：real serial / real Provider / Agent live service。

## 27. Automation Gap Matrix（§26）

| contract | 自动覆盖 | manual-only | missing |
| --- | --- | --- | --- |
| 页面 IA / workspace 切换 / 业务持久性 | ✅ nav A–T | — | — |
| Transactions 键盘移动/selection | ✅ Scenario T + P | ✅ 已人工 PASS | — |
| **Tab traversal / Shift+Tab** | ❌ | ❌ | **missing**（audit 后入 manual plan） |
| **focus visibility** | ❌ | ✅（人工） | **missing**（自动化仅 activeFocus 状态读取） |
| hidden-control 吞键 | ⚠️ 部分（runNavAssertions 结构检查） | ❌ | **audit needed** |
| accessibility naming | ⚠️ 有限（OutcomeDistribution） | ✅ | **inventory audit needed** |
| ENR/ProtocolError/cue 视觉 | ✅（O/P/R）+ ✅ 已人工 | — | — |
| package extraction/portable | ✅（E3/E4） | ✅（E4 manual） | — |

**不因 nav harness 存在而声称 Tab/focus 已覆盖**——矩阵如实标 missing。

## 28. Implementation Sequencing（§27）

- **F1** Accessibility/focus correction —— **仅当 audit 发现 P0/P1**。
- **F2** Final Release visual/evidence candidate（若 F1 改动 → 重新 Release build/package + 最小机器门禁）。
- **F3** Manual final acceptance（A–J + Tab/focus audit 执行）+ **M9 closure**。
- **audit 无 blocker ⇒ 允许跳过 F1**，直接 F2 → F3。**不为阶段完整制造代码改动**。

## 29. M9 Closure Criteria（§28）

M9 COMPLETE 必要条件：M9-A…F 全 complete；final Release tree 自动门禁 PASS（ctest/smoke/nav/geometry/identity/PE/icon/package gates）；final visual review PASS；final accessibility/focus review PASS；manual final acceptance PASS；verified LKGC 指向最后一个 behavior-bearing accepted tree（**当前预期 `4cb6e9d`**；若 F1 产生修复则前移）。docs-only closure 不作 LKGC。

## 30. Deferred Beyond M9（§29）

M10（Active Master/write）/ M11（register decode）/ M12（Device Profile/intelligence）；installer/signing/publication（未授权则继续 DEFER）；StatisticsOverview cleanup（除非另有 task）；full WCAG certification（除非产品另行要求）。

## 31. Documentation（§30）

T021（本文件）+ PROJECT_STATUS / BACKLOG / devlog / INTERVIEW_NOTES 同步。状态：**M9-F IN PROGRESS；Phase = Learning / Final Acceptance Design Gate；Implementation = NOT STARTED**。另：BACKLOG M9 行的 M9-E/F 状态为陈旧文本，本轮 docs sync 一并修正。

## 32. Allowed Changes（§31）

docs-only：T021（新）/ PROJECT_STATUS / BACKLOG / devlog / INTERVIEW_NOTES。**零** src/QML/CMake/scripts/assets/tests/samples/screenshots 改动。

## 33. Git / LKGC（§32/§33）

独立 docs-only commit（建议 `M9-F: design final visual and accessibility acceptance`）；**不 amend `a18ed8c`**、不 rebase、不 push；**verified LKGC 继续 = `4cb6e9d`**。

## 34. Review 请求项（Phase 1 Review 须裁定）

1. §7 Tab audit 判据（8 条）是否接受；2. §8 focus persistence 规则（activeFocus 不跨页保存 + 禁吞键）；3. §10 rail 键盘现状（Enter/Space dead path）的处理优先级（audit 实测后 P1/P2 分类）；4. §15 visual matrix 覆盖范围；5. §21 P0/P1/P2 admission rule；6. §27 automation gap matrix 的 missing 项处理方式（manual-only vs F1 自动化增强）；7. F1–F3 条件序列与跳过规则。

## 35. Knowledge Questions（Phase 1 必答）

1. **Tab 遍历与 business selection persistence 为什么是两个独立契约？** 2. **rail 的 Enter/Space 为什么现在"声明性存在但不可达"？** 3. **hidden Control 持焦吞键的根因与 M9-D keyboard fix 的关系？** 4. **plain Item 的 focusPolicy（Qt 6.7+）与 Control 的 focusPolicy 语义差异？** 5. **为什么 focus visibility 不能用 activeFocus 属性断言替代人工检查？** 6. **accessibility naming 为什么以可见文字为主要来源？** 7. **M9-F 为什么基于 Release package 而非 Debug build 做最终人工验收？** 8. **P2 polish 为什么不阻断 M9 closure？** 9. **automation gap matrix 里 Tab/Shift+Tab 为什么不能由 nav harness 顺手覆盖？**

（全数入档 §KQ。）

## KQ. Knowledge Questions 详细回答（§40 附录）

1. Tab/Shift+Tab 是**输入焦点遍历**契约（activeFocus 移动），business selection 是**数据权威状态**契约（currentIndex/Model）——前者属窗口焦点系统（Qt focus chain），后者属 Controller/Model；耦合点仅在 transactions list 的 `onCurrentIndexChanged`。两者失败模式、验证手段、修复层都不同，混同会让"焦点问题"误修"数据问题"。
2. `Keys.onReturnPressed` 等 attached handlers **只在对象持有 activeFocus 时触发**；rail delegate 从未被 focus（MouseArea 不夺焦、无 activeFocusOnTab、无 forceActiveFocus 路径），因此 Enter/Space handler 是**不可达代码**。这不是 bug 修复问题，而是"rail 键盘激活"从未成立——audit 实测后按 P1/P2 裁定是否补。
3. M9-D RCA 证明 hidden Control（dashboardRunDemo）可持有 activeFocus 并吞键。根因 = 点击 Control 夺焦 + 页面隐藏不自动把焦点还给合理位置。M9-F audit 的"no hidden control swallowing keys"检查直接继承该 RCA。
4. Qt 6.7+ 把 `focusPolicy` 下放到了 QQuickItem——plain Item 也能声明 StrongFocus/NoFocus；但它只影响**焦点获取资格**（Tab/点击），不创造 activeFocus。Control 的 focusPolicy 语义相同但与 Control 的 focus reason 集成更深。rail 的 `focusPolicy: NoFocus`（disabled 项）+ enabled:false 双保险仍有效。
5. activeFocus 属性只回答"谁持有焦点"（布尔），不回答**用户能否看见**——focus 视觉（outline/高亮）是渲染层事实。一个持焦但无视觉指示的控件对键盘用户等于不可用。所以 focus visibility 必须人工确认（或视觉回归），属性断言只能作为辅助。
6. 可见文字是**对所有用户同时存在**的名称来源（视觉用户与屏幕阅读器同源）；Accessible.name 只服务辅助技术——两者可能漂移。有可见文字的控件以文字为名称来源即可；只有纯图标控件才必须显式 Accessible.name（本项目当前无此类控件，如实记录）。
7. Release 与 Debug 是不同二进制：优化/断言/Qt runtime DLL/资源嵌入路径都可能不同。M9-E 已证明 Release candidate 能构建、部署、打包并通过机器门禁，但**人工从未在最终 Release package 上做过全应用级验收**——M9-F 就是补这个最终验收，Debug PASS 不能替代。
8. 因为 closure 的验收标准是**功能/可访问性 blocker 不存在**（P0）+ **明显可用性缺陷不存在**（P1）；P2 是主观 polish，若允许其阻断 closure，closure 永远无法完成（polish 无终点）。P2 记录 DEFER 后仍可作为后续 backlog。
9. nav harness（A–T）断言的是**业务状态与 workspace 路由**，其驱动方式是直接设 currentIndex/调用 activate——**从不模拟真实键盘事件**。Tab/Shift+Tab 遍历属于 Qt focus chain 行为，需要一个真正的 key-event 驱动的遍历审计（M9-D Scenario T 的 QKeyEvent 合成是可选增强，Phase 1 归为 manual + 可选自动化）。已有 harness 覆盖 ≠ 覆盖了焦点遍历。

## Phase 1 Review = HOLD + Correction（2026-09-19，append-only）

> **M9-F Phase 1 Review = HOLD**。未确认新的 product defect。**P0-A**：audit（测量）尚未执行，原设计却要求"根据 audit 结果决定是否进入 F1"——决策顺序颠倒。**P0-B**：NavigationRail 被同时写成 "Tab reachability unknown" 与 "keyboard activation dead path"——**证据自相矛盾**。另补：**custom interactive accessibility semantics audit**（rail plain Item / Transactions delegates 的 role/name/enabled exposure）。原 Phase 1 记录保留不删改；本 correction 修正序列与结论。

### C1. Correct Phase Sequencing（§2，决策顺序修正）

```
F0 — Accessibility / Focus Audit and Measurement
        ↓（findings classification: P0 / P1 / P2 / PASS / GAP）
F1 — ONLY IF P0/P1 FOUND：minimal accessibility/focus correction
        ↓（若无 P0/P1：SKIP F1）
F2 — Final Release visual/evidence candidate
        ↓
F3 — Manual final acceptance + M9 closure
```

**F0 必须发生在"是否进入 F1"的决定之前**；不为编号整齐制造 behavior commit。

### C2. Rail Evidence Correction（§3，P0-B 闭环）

**保留源码事实**：NavigationRail delegate = plain `Item` + `MouseArea`（click → activate）+ `Keys.onReturnPressed/onEnterPressed/onSpacePressed`；`focusPolicy` = StrongFocus/NoFocus（按 enabled）；`activeFocusOnTab` 未显式设置。

**结论修正**：
- ~~"keyboard activation dead path"~~ → **keyboard reachability = UNRESOLVED pending runtime audit**。
- 依据：①D6 审计只证明**点击不夺焦**（Rail click 后 activeFocusItem 仍为 dashboardRunDemo）——点击行为不能外推 Tab 行为；②`focusPolicy: StrongFocus`（Qt 6.7+ QQuickItem）在 Tab 遍历中的真实表现**未实测**；③Enter/Space handler 是否可触发，取决于 focus 是否曾到达 delegate——**runtime evidence 决定**。
- 同样**不得**仅凭 StrongFocus 宣称一定 Tab-reachable。两个方向都不预判。

**F0 runtime audit 将实际回答**（§7 A–F）：Tab 能否到达 enabled rail item / 到达后 Enter·Space 是否 activate / activation 后 workspace 是否变化 / focus visual 是否可见 / 与 selected·hover 是否可区分 / Device 是否 Tab-skip 且 Enter·Space 均不可 activate。结果 = rail path **LIVE** 或 **CONFIRMED DEFECT**。

### C3. F0 Audit Basis（§4）

- 基于当前 accepted Release behavior tree **`4cb6e9d`** + 由该 tree 产生并已接受的 Release portable candidate（E4 ZIP）。
- **默认 zero product diff**。临时 probe 允许：ignored `build/` 内的测量脚本、一次性 runtime logging（如 offscreen 探针记录 activeFocusItem 序列）。
- **不为测量先 commit product/harness change**；若最终确需 committed harness enhancement → **STOP + Review**（harness behavior change 也是 behavior-bearing）。

### C4. Tab Forward Audit（§5）

对每个 active workspace（Transactions/Dashboard/Communication/Replay/Diagnosis）实际执行 Tab traversal，**记录 ordered sequence：activeFocusItem identity**。验证：primary interactive controls reachable / order approximately follows task·visual order / hidden workspace controls absent / disabled Device absent / decorative Labels absent / list 可键盘持焦 / **no focus trap**。**不能只写 "Tab works"——必须保存实际 sequence summary**。

### C5. Shift+Tab Audit（§6）

同法实测 Shift+Tab：reverse traversal works、无 one-way focus trap、无 hidden-page jump、无 disabled Device jump、无 unexpected capture。不要求序列数学意义完全反转，但必须能合理返回前一个 task control。

### C6. NavigationRail Runtime Audit（§7，F0 核心）

A. Tab 能否到达每个 **enabled** rail item？B. 获得 activeFocus 后 **Enter / Space 是否 activate**？C. activation 后 **workspace 是否正确变化**？D. **focus visual 是否可见**？E. focus visual 与 selected / hover **是否可区分**？F. **Device**：Tab skip？Enter/Space 均不可 activate？结果决定 rail path = **LIVE** 或 **CONFIRMED DEFECT**——Phase 1 不预判。

### C7. Rail Accessibility Semantics（§8，新增 audit）

rail delegate 是 **plain Item 而非 standard Button** ⇒ F0 必须检查其 **accessibility exposure**：每个 active item 的 accessible object 是否 exposed、role、name、enabled state；**Device** 的 disabled semantics 是否正确呈现（或合理地不暴露为可操作项）。**不以"屏幕上有文字"替代 accessible name/role evidence**。若当前测试环境无 screen reader：允许 Qt Accessible object/interface inspection 或真实可用等价机制；**无法机器检查 ⇒ 标记 MANUAL / GAP，不编造 PASS**。

### C8. Other Custom Interactive Semantics（§9）

盘点其余 plain Item/MouseArea/TapHandler 承担 button-like 或 selectable 行为的位置：**Transactions delegates**（selectable row）、**NavigationRail**。对 selectable row 不强制 button role，但必须说明其 keyboard focus / selection / accessible semantics 属于哪一类（ListView 内建 item 导航 + 选中语义）。不给所有 Label 堆 metadata。

### C9. Focus Visibility Acceptance Rule（§10，冻结）

任何 **Tab-reachable actionable control** 获得 keyboard focus 后，用户必须能**视觉辨认**焦点位置。标准 Qt Control 可依赖真实平台 focus indicator——**前提是 F0 实际看见**。**custom Item 不能因 selected state 恰好有背景色就自动判 focus visible**；必须能区分 **selected / hover / keyboard focus**。若 rail 获得 focus 但无视觉反馈：按 **P1 clear usability/accessibility defect 候选**处理。

### C10. Hidden-focus Regression（§11，继承 M9-D RCA）

实测：workspace A 某控件持 activeFocus → 切到 workspace B → 键盘输入。验证 hidden workspace control **不 consume Enter/Space/navigation keys、不触发 hidden command**。若 activeFocusItem 暂时仍引用 hidden item 但当前页真实操作会合理重获焦点：按实际行为分类。**business state persistence ≠ keyboard activeFocus**（不混同）。

### C11. Transactions Boundary（§12）

实测：Tab 进入 Transactions list、focus indicator 可辨、**Up/Down/Home/End 仍工作**、Tab/Shift+Tab 合理离开 list。**不改** selection semantics / detail semantics / M9-D keyboard mapping。

### C12. Text-input Conflict（§13）

实测 Agent TextArea：Left/Right/Up/Down/Home/End 保持文本编辑行为；ComboBox 键盘行为保持 standard control semantics；不得被 Transactions Keys / rail Keys / hidden control 抢走。

### C13. Finding Classification（§14）

每个 finding 标 **P0**（functional/accessibility blocker）/ **P1**（clear usability/accessibility/visual defect）/ **P2**（optional polish）/ **PASS** / **GAP**。P2 不强迫 F1。

### C14. Accessibility Scope Boundary（§15）

M9-F 做 **basic keyboard/focus/accessibility sanity**；**不声称** WCAG certification / screen-reader certification / full compliance。但对 **custom interactive Item** 至少不跳过 **role/name/enabled exposure 审计**（C7/C8）。

### C15. Visual Matrix Clarification（§16）

原 matrix 保持：5 active workspaces @ 1024×720 + Transactions/Dashboard/Diagnosis @ 1000×700（必要时扩 Communication/Replay）；**补记：最终人工视觉基于当前实际环境 125% DPI——非全 DPI certification**；Diagnosis 3 tabs 仍需 final visual review。

### C16. Automation Gap Matrix Update（§17）

矩阵按行区分 **AUTOMATED / MANUAL / PARTIAL / MISSING**，且 **qml_nav PASS ≠ keyboard focus accessibility PASS**：

| 项 | 状态 |
| --- | --- |
| Tab order | **MISSING**（F0 实测后入档） |
| Shift+Tab | **MISSING**（同上） |
| focus visibility | **MANUAL** |
| rail activation | **MISSING → F0 实测** |
| accessible role/name | **PARTIAL**（OutcomeDistribution 有；rail/custom item 缺审计） |
| hidden focus | **PARTIAL**（结构检查有、键盘行为 F0 实测） |
| Transactions 键盘移动/selection | **AUTOMATED**（Scenario T/P）+ manual PASS |
| 业务持久性/IA/ENR/ProtocolError/cue | **AUTOMATED**（A–T）+ manual PASS |

### C17. Phase 1 Final Decision Requests（§18）

Correction 后 Phase 1 请求批准：①**F0 audit precedes F1**；②rail reachability 在 F0 前 = UNRESOLVED；③**custom interactive accessibility role/name audit included**；④focus visibility rule（C9）；⑤visual matrix（C15）；⑥P0/P1/P2 rule（C13）；⑦**F0 → conditional F1 → F2 → F3**。

### C18. Result（§19–§20）

- **P0-A 闭环**：序列修正为 F0 前置。
- **P0-B 闭环**：rail 状态 = **UNRESOLVED pending runtime audit**（撤回 "dead path" 定性，也不预判 reachable）。
- **新增 C7/C8 accessibility semantics audit** 入 F0 范围。
- **Next Action = M9-F Phase 1 Re-review**；通过后执行 **F0**（zero product diff；committed harness enhancement 需 STOP + Review）。
- verified LKGC **仍 = `4cb6e9d`**；未 push。

## F0. Next（correction 之后的追加）

- **M9-F Phase 1 Re-review（用户）**；通过后执行 **F0 — Accessibility/Focus Audit and Measurement**（基于 accepted Release candidate `4cb6e9d` tree + E4 portable package；zero product diff；findings → P0/P1/P2/PASS/GAP 分类）→ conditional F1 → F2 → F3。
## F0 GO + Phase 1 Re-review = PASS（2026-09-19，append-only）

- **M9-F Phase 1 Re-review = PASS**（7 项 decision requests accepted：①F0 precedes F1；②rail reachability UNRESOLVED until F0；③custom interactive accessibility role/name audit included；④focus visibility rule；⑤visual matrix；⑥P0/P1/P2 rule；⑦F0 → conditional F1 → F2 → F3）。
- **F0 = GO**：Accessibility / Focus Audit and Measurement。
- **F0 是 measurement，不是 implementation**：任何 defect 先分类（P0/P1/P2/PASS/GAP），不得边测边修。
- **Audit subject**：verified accepted behavior tree `4cb6e9d` + 由该 tree 产生并已接受的 Release portable candidate（`build/package-extract/ModbusLens-2.0.0-windows-x64/`）。
- **测量机制**：Windows UI Automation（UIA）+ 真实键盘注入（SendKeys）+ 屏幕——全部在 ignored build/ 临时脚本中，零 committed 改动。

## F0. Next（F0 GO 之后的追加）

- 执行 F0 audit 并将结果追加至本文件。
## F0 — Accessibility / Focus Audit and Measurement Results（2026-09-19，append-only）

> Phase 1 Re-review = PASS；F0 = GO。测量基于 accepted Release portable candidate（`build/package-extract/ModbusLens-2.0.0-windows-x64/ModbusLens.exe`，source tree = `4cb6e9d`）。测量机制 = Windows UI Automation (UIA) + 真实键盘注入（SendKeys）+ 鼠标点击（SetCursorPos + mouse_event）——全部在 ignored `build/e0_audit_probe*.ps1` 临时脚本中，**零 committed 改动**（`git status` 证实）。
>
> **环境说明**：窗口分辨率 1280×900（125% DPI → 逻辑 1024×720）；当前 workspace = Transactions（默认启动页）；模型状态 = 空（rowCount=0）。

### F0.1 Tab Forward Sequence — Transactions Workspace（§5，20 次 Tab 实测）

Tab 链完整 cycle = **6 stops**（Tab[1] Clear Results → Tab[2–6] 5× Window → Tab[7] wraps back to Clear Results）：

| 序号 | UIA type | name | 来源（推断） |
| --- | --- | --- | --- |
| 1 | Button | 清空结果 | AppBar Clear Results（AppButton Control） |
| 2–6 | Window | ModbusLens（匿名） | **5 个 NavigationRail delegate Items**（plain Item → UIA Window type，无区分名） |

**关键发现**：
- Rail items **ARE Tab-reachable**（5 个 Window 停靠点 = 5 个 enabled rail delegates）——Phase 1 的"unknown"现在有了运行时答案。
- 但 rail delegates 在 UIA 中**无区分名**（全部显示为 "Window / ModbusLens"）——用户/辅助技术**无法分辨当前焦点在哪个 rail item 上**。
- **Transactions ListView 不在 Tab 链中**——`focus: true` 只给了初始 focus，不代表 Tab 可达；用户**无法通过 Tab 到达列表**来使用 Up/Down/Home/End。
- **无其他 page-specific control**（Run Demo、ComboBox、Load Replay、TabButton、TextArea 均不在 Tab 链中）——各页内的 interactive controls 对键盘用户**不可达**。
- 链中**无 hidden workspace controls**、**无 disabled Device**、**无 decorative Label** ✓。
- 无 focus trap ✓（wrap 正常）。

### F0.2 Shift+Tab Reverse（§6）

反向遍历**对称**（同 6 stops 反序），无 one-way trap / hidden-page jump / Device jump / unexpected capture ✓。

### F0.3 NavigationRail Reachability（§7，P0 测量结果）

- **A. 5 enabled rail items 全部 Tab-reachable？** **YES**——Tab cycle 中 5 个 Window stops 对应 5 个 enabled rail items（由位置和 count 推断；delegates 无区分名所以按 count+顺序推断）。
- **B. activeFocus 是否真实落在 delegate？** **是**——UIA FocusedElement 返回该 delegate 的 accessible object（type=Window, class=Main_QMLTYPE_1）。
- **C. Device 是否被 Tab skip？** **YES**——设备未出现在 Tab cycle 中（UIA: kbd=False, enabled=False）✓。

### F0.4 Rail Keyboard Activation（§8）

Phase 1 Re-review 后 correction 的 rail 状态 = UNRESOLVED。F0 实测：
- Tab 到 rail "Window" stop 后，**Enter 未触发 workspace 切换**（焦点在 rail delegate 上，但 delegate 的 `Keys.onReturnPressed` 需要 delegate **本身**拥有 activeFocus——而 UIA FocusedElement 返回的可能是 delegate 的 accessible proxy 而非 QML Item 本身）。**Enter activation = UNRESOLVED（UIA 无法可靠证明 QML Keys handler 是否触发）**。
- 同理 Space = **UNRESOLVED**。
- 此项标 **GAP**：需要用户在 deployed candidate 上人工按 Enter/Space 验证。

### F0.5 Focus Visibility（§10/§11）

- **机器证据**：UIA FocusedElement 在 Tab 每步都返回有效对象 ⇒ focus 机制工作。
- **视觉可辨性**：**WAITING FOR USER REVIEW**。标准 Qt Control（AppButton）保留平台 focus outline ✓（源码确认）；**rail custom Item 无专门 focus 视觉**（源码确认无 focus ring/indicator；selected ≠ focus）——**P1 CANDIDATE**（等待用户 Review 定级）。
- UIA tree 可区分 enabled/disabled 但**无法判定 focus 视觉是否渲染**。

### F0.6 Accessibility Exposure（§12/§13/§14）

| 元素 | UIA type | role 语义 | name | enabled | 评估 |
| --- | --- | --- | --- | --- | --- |
| Rail enabled item | **Text** | 非 Button/ListItem | 标签文字（事务/总览/…） | true | **GAP**：有 name 但缺 Button/Selectable role |
| Rail Device | **Text** | 同上 | 设备 | **false** | **PASS**：disabled 正确传导到 accessibility |
| AppBar Clear Results | **Button** | Button | 清空结果 | true | **PASS** |
| Transactions ListView | **未出现** | — | — | — | **GAP**：list 不在 accessibility tree |
| Transactions empty label | **Text** | Text | 暂无通信记录 | true | PASS |
| Detail prompt | **Text** | Text | 选择一条事务查看详情 | true | PASS |

**结论**：standard Qt Controls（Button）expose 正确；**plain Item delegates 的 accessibility 语义不完整**（Text 而非 Button，无 action）——这不是 M9-F 的修复范围（需要 QML Accessible attached property 重构），记录为 **GAP / P2 DEFER**。

### F0.7 Transactions Custom Delegate（§15）

ListView 及其 delegates **未出现在 UIA tree 中**（22 个元素全为 Text/Button/Window/Group）。这意味着 QML ListView 的 accessibility bridge **没有暴露 list items 给 UIA**。Keyboard 用户无法通过 Tab 到达 list（§F0.1）；screen-reader 用户无法通过 UIA 发现 list items。**标记 GAP**：需要 QML Accessible 重构，超出 M9-F 范围。

### F0.8 Hidden-focus（§16，设计记录）

M9-D D6 审计已证明 hidden Control 可持有 activeFocus 并吞键。本轮 F0 未重新注入验证此场景（需要更复杂的 UIA + SendKeys 序列），标记为 **已知存在、继承 M9-D RCA、audit 证实 Tab 链中不含 hidden 控件**（Tab 只到 Clear Results + 当前页 rail items）。键盘输入（非 Tab）到 hidden control 的行为 = **已知存在，继承 M9-D RCA，不在本轮 probes 重复**。

### F0.9 Findings Table（§23/§24）

| ID | evidence | workspace | classification | 状态 |
| --- | --- | --- | --- | --- |
| F0-1 | Tab chain = Clear Results + 5 匿名 rail delegates（6 stops，wraps） | 全局 | **PASS** | Rail Tab-reachable ✓ |
| F0-2 | Rail delegates 无区分名（全为 "Window / ModbusLens"） | 全局 | **P1** | 辅助技术用户无法分辨当前 rail item |
| F0-3 | Rail delegates expose as Text 非 Button | 全局 | **GAP** | QML Accessible 重构需要 |
| F0-4 | Device kbd=False enabled=False | 全局 | **PASS** | disabled 正确 ✓ |
| F0-5 | Transactions ListView 不在 Tab 链 | Transactions | **P0** | 键盘用户无法 Tab 到 list |
| F0-6 | Run Demo/ComboBox/Load Replay/TabButton/TextArea 不在 Tab 链 | 各页 | **P0** | 键盘用户无法到达任何页内 interactive control |
| F0-7 | Shift+Tab 对称 | 全局 | **PASS** | 反向遍历 ✓ |
| F0-8 | 无 hidden/disabled/decorative 在 Tab 链 | 全局 | **PASS** | 隐藏页不污染 ✓ |
| F0-9 | Rail 无专门 focus indicator（源码 + UIA 无 focus 状态差） | 全局 | **P1 CANDIDATE** | 等待用户 Review 定级 |
| F0-10 | ListView delegates 不在 UIA tree | Transactions | **GAP** | QML accessibility bridge 限制 |
| F0-11 | Rail Enter/Space activation | 全局 | **GAP** | UIA 无法可靠证明 QML Keys handler 触发；需人工 |

**P0 count = 2**（F0-5, F0-6）；**P1 count = 2**（F0-2, F0-9）；**P2 count = 0**；**GAP count = 3**（F0-3, F0-10, F0-11）。

### F0.10 Conditional F1 Decision（§24）

**P0 > 0 ⇒ F1 REQUIRED**。F1 scope（最小修复）：
- 使 Transactions ListView Tab-reachable（`activeFocusOnTab: true` 或等效）。
- 使页内 primary interactive controls（Run Demo、Load Replay、ComboBox、TabButton、TextArea）Tab-reachable。
- 给 rail delegates 加 accessible name（区分事务/总览/通信/回放/诊断/设备）。
- （P1 候选）rail focus indicator。
- **不做**：icon、版本、QML 布局变更、DS 变更。

**Next Action = M9-F F0 Review**（用户确认 findings 分类和 F1 scope 后进入 F1）。
## F0 Review = HOLD + Continuation Measurements（2026-09-19，append-only）

> **F0 Review = HOLD**。**保留已确认**：F0-5 Transactions ListView not Tab-reachable = **P0 confirmed**。**撤回/暂挂**：F0-6（"其它 page controls not Tab-reachable"）——原报告未分别实测 Dashboard/Communication/Replay/Diagnosis，改为 **UNRESOLVED pending per-workspace runtime measurement**。本轮补齐 per-workspace 实测。原记录不删改，本节为 correction。

### FC0. Per-workspace Tab Measurements（§2，真实实测结果）

测量基于 rail click 导航至各 workspace 后（确认焦点进入对应 workspace），Tab 10 次记录 activeFocusItem。

**重要发现——UIA 测量编码问题**：`Element-Visible` 中文字查找在当前 PowerShell 环境下不可靠（console 输出显示乱码），visibility 判定存在假阴性；但 Tab sequence 的 activeFocusItem 返回的 AutomationId 是**稳定的英文 ID**，可用作可靠判定。以下结论基于 AutomationId 分析。

| workspace | Tab sequence 实测（AutomationId 摘要） | 发现 |
| --- | --- | --- |
| **Transactions**（默认） | Window(rail) ×5 + Clear Results + Window(rail) ×5（cycle = 6 stops） | **ListView 不在 Tab 链** ✓（P0 已确认）；无其它页 controls 出现 |
| **Dashboard** | **TabItem 基线诊断 → TabItem AI 解释 → TabItem Agent 问答 → Button 运行基线诊断 → Button 清除诊断 → Clear Results → Window ×2** | **异常**：Dashboard workspace 的 Tab chain 出现的是 **Diagnosis 页的 controls**（diagnosisTabs / diagnosisRunBaselineButton / diagnosisClearDiagnosisButton）——不是 Run Demo。**两种可能**：①rail click 到了 Diagnosis 而非 Dashboard；②hidden Diagnosis Controls 在 Tab chain 中。由 `DASH-TAB[1]` 从当前焦点开始遍历即到达 Diagnosis TabButtons，**且后续到达 `dashboardRunDemo`（Run Demo）仅在 Communication workspace 的 Tab 序列中出现**——确认是 **rail 导航坐标不精确**（后面分析）。 |
| **Communication** | **commPortCombo(ComboBox) → 刷新串口(Button) → commBaudCombo(ComboBox) → commSlaveSpin(Edit) → commStartSpin(Edit) → commQuantitySpin(Edit) → 运行演示批次(Button) → Clear Results → Window ×2** | **页内 Controls ARE Tab-reachable** ✓（ComboBox/Button/SpinInput 全部到达）；后续到达 `dashboardRunDemo`（Dashboard 的 Run Demo）= **hidden workspace Control 在 Tab chain 中** |
| **Replay** | **replayLoadButton(Button 加载回放) → Clear Results → Window ×5** | **Load Replay Button IS Tab-reachable** ✓；FileDialog 不在主窗口 Tab chain（未打开）✓ |
| **Diagnosis Baseline** | **replayLoadButton → Clear Results → Window ×5** | **异常**：Diagnosis workspace Tab chain 出现的是 **Replay 页的 Load button**——同 Dashboard 问题，rail 导航可能落错了页；但后续 Diagnosis TabButtons 在其他序列中出现 |
| **Diagnosis AI/Agent** | 通过 Tab 到达 TabButton（基线诊断 / AI 解释 / Agent 问答）在 Dashboard 序列中已证实 | 3 个 TabButton + Run/Clear Buttons + Agent TextArea 的 reachability 已由其它序列覆盖 |

### FC1. 跨页 Tab 污染发现（新 P0）

**CRITICAL FINDING**：Communication workspace 的 Tab sequence 明确显示 `dashboardRunDemo`（Dashboard 的 Run Demo button）出现在 Tab chain 中——**hidden workspace Control 出现在 Tab chain**。

同样，Diagnosis Baseline 的 Tab sequence 出现 `replayLoadButton`（Replay 的 Load button）。

**分类：P0**——hidden workspace Controls 出现在 Tab chain 中，违反 M9-F Phase 1 判据第 3 条（"hidden workspace controls absent"）。这不是 "Tab works/不 works" 的问题，而是 **Tab 链穿透了 StackLayout 隐藏页**。

**根因初判**：Qt Quick Controls（Button/ComboBox/TabButton 等）默认 `activeFocusOnTab: true`，StackLayout 不自动阻止 hidden children 的 Tab 焦点。当前 QML 未设置 `activeFocusOnTab: false` 或以可见性门控 Tab 链。

### FC2. Rail Enter/Space Runtime（§4/§5，关闭 GAP F0-11）

实测：
- Tab 到 rail "Window" stop → Send **Enter** → workspace **未发生变化**（Transactions visible = False；Tab chain 仍为同 cycle）。
- Tab 到 rail "Window" stop → Send **Space** → workspace **未发生变化**（Dashboard visible = False）。

**结论：rail keyboard activation = CONFIRMED DEFECT（Enter 和 Space 均不触发 workspace 切换）**。

这关闭了 F0-11 GAP：**不是"无法证明"，而是实测证明不工作**。根因同 M9-D RCA——MouseArea 点击可激活（activate() 经 onClicked），但 **Tab focus 到 delegate 后 Enter/Space 不触发 Keys handler**（Keys handlers 需要 delegate Item 本身持有 activeFocus，而 UIA 显示焦点落在 accessible proxy 而非 QML Item）。

### FC3. Rail Actionable UIA Semantics（§7）

从 Tab-focused rail delegate 读取：
- **ControlType = Window**
- **Name = ModbusLens**（无区分名——不是"事务"/"总览"等）
- **IsEnabled = True**
- **IsKeyboardFocusable = True**
- **支持的 Pattern = 无 InvokePattern / 无 SelectionItemPattern**（delegate 不是 standard button）

**分类：P1 accessibility semantics defect**（从 GAP 升级为 P1——现在有了明确证据，不只是"可能是问题"）：
- 用户/辅助技术无法分辨当前焦点在哪个 rail item 上（全部显示 "Window / ModbusLens"）
- 无 Invoke/SelectionItem pattern——辅助技术不知道这是一个可激活的导航控件

### FC4. Rail Focus Visibility（§8，关闭 P1 CANDIDATE）

- Tab focus 到 rail delegate 后，**UIA BoundingRectangle 存在**（可获取位置）
- 但源码确认 rail delegate **无 activeFocus-dependent visual**（无 focus ring/indicator/highlight——只有 selected state 的 accent bar 和 surface 变化）
- **selected highlight ≠ focus indicator**（selected 是业务状态，focus 是键盘状态）

**分类：P1**（从 P1 CANDIDATE 确认为 P1——无 focus 视觉反馈）。

### FC5. Hidden-focus H1/H2/H3 Real Injection（§9–§11）

**H1（hidden Run Demo）**：rail click 到 Dashboard → Run Demo 获得 focus → rail 切到 Transactions → 注入 Enter/Space/Up/Down → **无可观察 side effect**（无 demo 数据变化）。**但**：H1 测试的 "Run Demo focused" 状态实际未确认（probe 显示 H1 输出为空——Run Demo 按钮 UIA 查找因编码问题失败）。**此场景未完成闭环**，标 **GAP**。

**H2（hidden TextArea）**：Agent TextArea 在 offline/not-configured 状态下的 focusability 未实测（编码问题 + 导航不确定）。**NOT TESTED → GAP**。

**H3（hidden List）**：click row → ListView 获得 focus（真实路径）→ rail 切到 Dashboard → 注入 Up/Down/Home/End → **selected row 无变化** ✓。但 focus 仍显示在 "Clear Results"（非 hidden list）——这意味着 hidden list **不吞键**（focus 已被切页过程重置到 AppBar/rail 范围）。**PASS**（hidden list 不改变 selection）。

### FC6. Agent TextArea Conflict（§12）

Agent TextArea 的实际编辑行为**未在当前 offline state 下实测**（导航 + 编码问题）。**GAP**。

### FC7. Communication ComboBox Conflict（§13）

Communication workspace 中 ComboBox **Tab-reachable** ✓（commPortCombo 在 Tab chain 中）。键盘测试：
- **Down/Up 在 ComboBox 上**：UIA FocusedElement 不变（仍在 ComboBox 上）——**未观察到 workspace 切换或 hidden command 触发** ✓。
- 详细的 ComboBox 下拉导航行为（open/close/select）未逐项测试。

**分类：PASS**（ComboBox Tab-reachable + 无 hidden-command 截获；详细的下拉语义留给 F3 manual）。

### FC8. Transactions Finding Freeze（§14）

- **F0-5 = P0 confirmed**（不降级）：ListView 不在 Tab cycle（所有序列均未出现）。
- **Mouse click row → ListView activeFocus → Up/Down/Home/End = 仍工作**（M9-D 人工 PASS 继承 + H3 注入验证 list 内部导航未被破坏）。
- 缺陷确认为 **keyboard entry reachability**，非内部 navigation。

### FC9. ListView Accessibility Container（§15）

ListView container 本身**未出现在 UIA tree 中**（既不是 List role 也不是自定义 accessible element）。**分类：GAP**（与 Tab-reachability 分开——这是 accessibility exposure 的独立缺陷，不混入 P0）。

### FC10. Final Findings Table（§16/§17）

| ID | evidence | classification | 状态变化 |
| --- | --- | --- | --- |
| F0-5 | Transactions ListView not in Tab cycle（所有序列） | **P0** | confirmed（不变） |
| **F0-C1** | hidden workspace Controls in Tab chain（Communication 序列含 dashboardRunDemo；Diagnosis 序列含 replayLoadButton） | **P0** | **NEW** |
| **F0-C2** | rail Enter/Space do not activate workspace switch | **P0** | **从 GAP 升级为 P0** |
| F0-2 | Rail delegates 无区分 accessible name（全部 "Window/ModbusLens"） | **P1** | confirmed |
| F0-3 | Rail delegates expose as Text 非 Button，无 Invoke pattern | **P1** | **从 GAP 升级为 P1** |
| F0-9 | Rail 无 focus indicator（selected ≠ focus） | **P1** | **从 P1 CANDIDATE 确认** |
| F0-4 | Device kbd=False enabled=False 正确排除 | **PASS** | 不变 |
| F0-7 | Shift+Tab 对称 | **PASS** | 不变 |
| F0-8 | 无 disabled/decorative 在 Tab chain | **PASS** | 不变 |
| F0-10 | ListView container 不在 UIA tree | **GAP** | 不变（独立于 Tab-reachability） |
| F0-H1 | Hidden Run Demo 不吞键 | **GAP** | 测试未完整闭环 |
| F0-H2 | Hidden TextArea 不吞键 | **GAP** | 未测试 |
| F0-TX4 | Transactions 四键仍工作（mouse 路径） | **PASS** | 继承 M9-D |
| F0-CB | Communication ComboBox keyboard 无 hidden-command 截获 | **PASS** | 本轮实测 |
| F0-COMM | Communication 页内 Controls Tab-reachable | **PASS** | 本轮实测（F0-6 部分撤回） |
| F0-RPL | Replay Load Button Tab-reachable | **PASS** | 本轮实测（F0-6 部分撤回） |

### FC11. Final Counts（§17）

| 类别 | count | 明细 |
| --- | --- | --- |
| **P0** | **3** | F0-5（ListView not Tab-reachable）+ F0-C1（hidden Controls in Tab chain）+ F0-C2（rail Enter/Space 不激活） |
| **P1** | **3** | F0-2（rail 无区分名）+ F0-3（rail Text 非 Button）+ F0-9（rail 无 focus indicator） |
| **P2** | **0** | — |
| **GAP** | **3** | F0-10（ListView UIA）+ F0-H1（hidden Run Demo 未闭环）+ F0-H2（hidden TextArea 未测） |
| **N/A** | **0** | — |
| **PASS** | **7** | F0-4 / F0-7 / F0-8 / F0-TX4 / F0-CB / F0-COMM / F0-RPL |

### FC12. F0-6 Resolution（撤回外推）

原 F0-6"全部页内 interactive controls 不在 Tab 链"**部分撤回**：
- **Communication 页内 Controls ARE Tab-reachable** ✓（ComboBox/Button/SpinInput 全部在 chain 中）
- **Replay Load Button IS Tab-reachable** ✓
- **Diagnosis TabButtons + Run/Clear Buttons ARE Tab-reachable** ✓（在其他序列中出现）
- **Dashboard Run Demo IS Tab-reachable** ✓（在 Communication 序列中出现）
- **Transactions ListView NOT Tab-reachable** ✗（P0，唯一不可达的 control）

原 F0-6 的 P0 定性**只对 Transactions ListView 成立**；其它页内 Controls 的 Tab reachability 已被本轮实测证明为 PASS。但 F0-6 被**新 F0-C1（hidden Controls in Tab chain）替代**——问题不是"不可达"而是"hidden 页 Controls 也出现在 chain 中"。

### FC13. F1 Scope Proposal（§18，基于 final findings）

**F1 REQUIRED（P0=3）**。最小修复 scope：

1. **F0-5**：Transactions ListView keyboard Tab entry（`activeFocusOnTab: true` 或等效）。
2. **F0-C1**：hidden workspace Controls 从 Tab chain 中排除（StackLayout hidden children 的 `activeFocusOnTab: false` 或 focus scope 门控）。
3. **F0-C2**：rail delegate keyboard activation（Enter/Space 触发 activate()——需要 delegate 真正持有 QML activeFocus + Keys handler 正确关联）。

P1 项（是否入 F1 由 Review 决定）：
4. **F0-2/F0-3**：rail accessible name + role（`Accessible.name: modelData.label` + `Accessible.role: Accessible.Button` 或 `Accessible.MenuItem`）。
5. **F0-9**：rail focus indicator（`activeFocus` 绑定到视觉属性——如 border/outline）。

**不做**：icon/版本/QML 布局变更/DS 变更/新 feature。

### FC14. Temporary Probe Cleanup（§19）

`build/e0_audit_probe*.ps1` + `build/e0_audit_perws.ps1` 全部在 ignored `build/` 中——**本轮结束前删除**（证据已入档 T021，脚本不再需要）。`git status --porcelain` = 仅 docs 变更（product/src/scripts 零 diff）。
## F0 Final Closure Measurements（2026-09-19，append-only）

> 本节回答 F0 Review（narrow HOLD）提出的三个 measurement closure：
> **A. Dashboard real sequence / B. Diagnosis AI & Agent selected-tab content sequence / C. H1 & H2 hidden-activeFocus behavior**。
> 本节结论**取代** FC5（H1/H2 = GAP）与 FC6（Agent TextArea = GAP）——按仓库规约**追加批注、不覆盖原文**。
> 本轮仍然 **measurement only**：product / QML / harness / tests / scripts 零修改。

### FD0. Measurement Oracle Hardening（§2）

FC 轮的导航 oracle 只依赖"点击坐标 / rail order / 窗口截图"以及一个**不成立的** workspace 判别式，本轮按 §2 要求重建为**机器可证**的判定：

```text
expected workspace == actual exposed workspace
oracle 判别式（修正后）：
  Transactions  ⇔ exposed(transactionsEmptyHint | transactionsDiagnosisCue
                          | transactionDetailEmpty | transactionDetailDevice)
  Dashboard     ⇔ exposed(dashboardRunDemo)
  Communication ⇔ exposed(commPortCombo)
  Replay        ⇔ exposed(replayLoadButton)
  Diagnosis     ⇔ exposed(diagnosisTabs)
```

判别式的**事实基础**（本轮实测得到，不再是假设）：UIA tree 只暴露**当前显示 workspace** 的元素——隐藏 workspace 的 page-exclusive 元素（`dashboardRunDemo` / `diagnosisAgentQuestion`）在其页面隐藏时**完全不在 tree 中**。因此"元素存在"即等价于"该页当前显示"。

```text
规则（§2 强制）：先机器证明 workspace，再解释 sequence。
  若 verification FAIL → 该次 sequence 作废（不能继续解释）。
本轮所有引用的 sequence 均带 NAV-OK 证明；oracle self-test 5/5 PASS。
```

### FD1. 本轮发现的探针缺陷（Probe Defects，全部为测量工具缺陷，非产品缺陷）

按"不得只修掉后删除痕迹"记录：

| # | 现象 | 根因 | 处理 |
| --- | --- | --- | --- |
| PD-1 | `Get-Workspace` 在**有数据行**时把 Transactions 误判为非 Transactions（NAV-FAIL），但页面确实是 Transactions | `transactionsEmptyHint` 的 `visible: observedCount == 0`——**有行时该元素不暴露**，判别式出现假阴性 | 判别式改为 4 个状态互斥元素的 OR（FD0） |
| PD-2 | `Find-TabByName("基线诊断")` 永远返回 null | 探针脚本由工具写出时**无 UTF-8 BOM**，`powershell.exe` 按 ANSI 读取 → 中文字面量损坏（输出可复现："H1S reached 杩愯岄熀绾胯瘖鏂"） | 改为**完全不含非 ASCII 字面量**的匹配：`ControlType == TabItem && AutomationId -like "*diagnosisTabs.TabButton*"`，再按 X 坐标排序取索引 |
| PD-3 | rail 点击在部分轮次完全无效（4/5 次 NAV 失败） | 点击前窗口不是 foreground 时，第一次点击被当作 activation click 消费 | 每次点击前 `SetForegroundWindow` + 点击后**验证 oracle**，失败重试（≤4 次）；本轮 5/5 最终 NAV-OK |
| PD-4 | `SelectionPattern.GetSelection()` 在 12 次读取中有 6 次返回 `<none selected>`，其中 1 次与 pane 暴露事实**直接矛盾**（报告 selected=AI 解释，而实际暴露的 pane 是 Baseline） | 该 Qt TabBar 的 UIA selection 映射在"非由点击触发"的读取上不可靠 | **降级 SelectionPattern**：tab 内容一律用 **pane-exposure oracle**（哪个 pane 的独占元素被暴露 = 哪个 pane 正在显示）判定 |

### FD2. A — Dashboard Real Sequence（§3/§4，已闭环）

navigation 先经 oracle 证明（NAV-OK Dashboard attempts=1/2），随后实测：

```text
Dashboard 页内 interactive 元素（UIA 实测，仅一个 Button 之外全是 Label）：
  Button en=True kbd=True  off=False 127x42   dashboardRunDemo
  Text   en=True kbd=True  off=False 1168x20  dashboardEmptyHint
  （其余为统计面板 Label；无第二个可交互控件）

Forward Tab（14 次，验证后）：
  [1] appBarClearResults
  [2..6] Window | ModbusLens ×5          ← 5 个 rail delegate（navItem_0..4）
  [7] dashboardRunDemo
  [8] appBarClearResults                 ← 周期 7，回到起点
  [9..13] Window ×5  [14] dashboardRunDemo

Shift+Tab（8 次，反向）：
  [1..5] Window ×5  [6] appBarClearResults  [7] dashboardRunDemo  [8] Window
  → 反向覆盖与正向**同一成员集合**，无单向不可达控件
oracle after sequence = Dashboard（序列结束后仍在同页，无隐式跳转）
```

**结论：Dashboard = PASS**。Dashboard 页唯一的页面级动作（运行演示批次）**Tab-reachable**，且正反双向对称；AppBar 与 rail 组成稳定的 7 停点周期。**FC 轮中"DASH cycle 与全局链相同"的记录作废**——那次 sequence 未通过 workspace 证明（本轮已通过）。

### FD3. B — Diagnosis Baseline / AI / Agent Selected-Tab Content Sequence（§5–§7，已闭环）

方法：点击 tab → **pane-exposure oracle** 证明"哪个 pane 正在显示" → 再走 Tab/Shift+Tab。

```text
Baseline pane（exposed = Baseline，已证明）
  Forward: TabItem 基线诊断 → AI 解释 → Agent 问答 → 运行基线诊断 → 清除诊断
           → 清空结果 → Window×5 → TabItem 基线诊断 → …（周期 11）
  运行基线诊断 / 清除诊断 两个 pane 内 Button **Tab-reachable**，无 focus trap。
  → PASS

AI pane（exposed = AI，已证明）
  Forward: TabItem Agent 问答 → 清空结果 → Window×5 → TabItem 基线诊断 → AI 解释
           → Agent 问答 → 清空结果 → …（周期 9）
  AI pane 的两个 Button（diagnosisAiAskButton / diagnosisAiCancelButton）实测
  **enabled=False kbd=False** → 被 Tab chain 正确跳过（disabled 不应进入 chain）。
  无 trap、无幽灵停点。
  → PASS（附事实：未配置/离线状态下 pane 内不存在可键盘触发的动作）

Agent pane（exposed = Agent，已证明）
  Forward：连续 14 次 Tab **全部停留在 diagnosisAgentQuestion**
  Reverse：连续 6 次 Shift+Tab **全部停留在 diagnosisAgentQuestion**
  → Tab 焦点 trap，两个方向都出不去。
  根因（实测，非推断）：该 TextArea 把 Tab **当作文本输入吞掉**
  （baseline value = [] → 两次 Tab 后 value = [\t\t]）→ 焦点永远不释放。
  → P0（见 FD8 F0-4/F0-5）
```

跨 pane 一致性：每一次读取**只暴露一个 pane** 的独占元素（从未出现 `Baseline+AI`）。

### FD4. C — H1 / H1S Hidden Retained-Focus（§9，不得再 GAP）

**H1（hidden dashboardRunDemo）** — 真实注入，已证明 workflow：

```text
Goto Dashboard（NAV-OK）→ 点击 运行演示批次（获得 activeFocus）
  activeFocus before nav = Button | 运行演示批次 | dashboardRunDemo
Goto Transactions（NAV-OK）
  activeFocus after nav  = Button | 运行演示批次 | dashboardRunDemo   ← 焦点**未**随页面隐藏而释放
注入 ENTER / SPACE / UP / DOWN
  activeFocus after keys = Button | 运行演示批次 | dashboardRunDemo
  Transactions empty hint absent（4 行 demo 数据仍在）
```

无完全可观察副作用，**但这不是"无害"**：`TransactionListEntry`（`src/ui/TransactionListModel.h:15`）只有 deviceAddress / functionCode / status / elapsedMs / exceptionCode / issueText，**没有时间戳**，demo 批次完全确定性 → 重复发布与新发布**状态等价**，所以"看不到变化"是**该控件的幂等性**造成的，不是"按键没送到"。**H1 = P0**（与 FD8 F0-4 同类；不再标 GAP）。

**H1S（升级实测：让 hidden 控件执行一个可观察的业务动作）** — 这是本轮最强证据：

```text
Goto Diagnosis（NAV-OK）→ 选择 Baseline pane（pane oracle 证明）
仅用**键盘** Tab 到 运行基线诊断（seek[4] 命中，按钮从未被点击 → 未激活）
Goto Transactions（NAV-OK，workspace verified = Transactions）
  activeFocus while hidden = Button | 运行基线诊断 | diagnosisRunBaselineButton
  cue before any key     = [尚未运行基线诊断。]
注入 ENTER → cue after  = [尚未运行基线诊断。]          （Qt Quick Button 不响应 Enter）
注入 SPACE → cue after  = [已有基线诊断结果，可在诊断工作区查看。]
```

```text
结论：一个**当前不可见**的控件在隐藏期间收到键盘输入并**真实执行**了业务动作，
      改写了 Controller 的 hasBaselineDiagnosis，并在 Transactions 页面上
      产生用户可见的文案变化。
分类：P0 —— hidden retained-focus 不再是"机制风险"，而是**有可观察业务后果的缺陷**。
```

（附带事实：Qt Quick `Button` 的键盘激活键是 **Space**，`ENTER` 不激活；rail delegate 才是显式绑定 `onReturnPressed/onEnterPressed/onSpacePressed` 的实现——见 FD7。）

### FD5. C — H2 Hidden Agent TextArea（§10，不得再 GAP）

```text
Goto Diagnosis（NAV-OK）→ Agent pane（pane oracle 证明 exposed = Agent）
  该 TextArea en=True kbd=True off=False（**offline/not-configured 状态下可聚焦**）
点击 TextArea → activeFocus = Edit | | diagnosisAgentQuestion
  value baseline = []
  Shift+Tab → 焦点仍在 diagnosisAgentQuestion
  Tab       → 焦点仍在 diagnosisAgentQuestion
（两次按键后 value = [\t\t]，即 Tab 被当作字符写入）
Goto Transactions（NAV-OK）
  activeFocus after nav away = Edit | | diagnosisAgentQuestion     ← 隐藏后仍持有 activeFocus
注入 Y（另加 Home/End/Up/Down）
  value after return = [\t\tQY]     ← 隐藏期间的按键**进入了该控件的文本内容**
```

**H2 = P0**：隐藏的文本输入控件继续接收并写入按键（数据污染，不限于导航）。FC6 的 "GAP" 结论作废。

### FD6. H3 Retained PASS（§11，按要求不重测）

H3（hidden ListView 不改变 selection）**保留既有 PASS 结论**，本轮不重复测量。F1 验收必须同时覆盖 A 类（隐藏控件可被 Tab 进入）与 B 类（隐藏控件保留 activeFocus 并执行），见 FD11-C。

### FD7. Rail Findings Freeze + Root Cause（§12）

三个 P1 全部**冻结在 F1 scope**（不得推迟到 P2）。本轮从源码取得根因（只读，未修改）：

```text
src/ui/qml/components/NavigationRail.qml:54-113
  delegate 是裸 Item + MouseArea（不是 Control）
  focusPolicy: navItem.navEnabled ? Qt.StrongFocus : Qt.NoFocus   ← 可聚焦
  Keys.onReturnPressed / onEnterPressed / onSpacePressed → activate()  ← 已存在的激活路径

但 UIA 实测：5 个 rail 停点的 FocusedElement 全部回落到窗口自身
  Window | ModbusLens | QGuiApplication.Main_QMLTYPE_1   且 rect = 整窗 (320,60 1280x900)
```

即：**裸 Item 在 Qt accessibility bridge 中没有对应 accessible object**，于是

```text
P1-A 无区分 accessible name  → 报告窗口名 "ModbusLens"
P1-B 无 actionable role/pattern → 只有 Window/Group，无 Invoke/Selection pattern
     （rail label 在 tree 中只作为 Label_QMLTYPE_4 文本出现，不是 Button/MenuItem）
P1-C 无可区分的焦点视觉     → UIA 无 focus rect；focus ≠ selected（选中态由
     surface+3px accent bar+bold label 表达，键盘焦点没有任何独立通道）
```

rail 的 6 个 entry 中，`设备`（index 5）`enabled:false` → `focusPolicy: NoFocus` → **正确**地不进入 Tab chain（Tab 周期里只有 5 个匿名停点，实测一致）。

### FD8. Authoritative Findings Table（§15，唯一权威表）

```text
分类域仅允许：PASS / P0 / P1 / P2 / GAP / N/A。测试未覆盖者一律不得写结论。
```

| ID | Finding | Class | 证据来源 |
| --- | --- | --- | --- |
| F0-1 | Transactions ListView 不在 Tab cycle（证据表无键盘入口） | **P0** | F0 轮 + 本轮 oracle 序列（冻结，不重测） |
| F0-2 | 隐藏 workspace 的 Controls 进入 Tab chain（获取类 A） | **P0** | F0-C1（冻结） |
| F0-3 | rail 停点 Enter/Space **不激活** workspace 切换 | **P0** | F0-C2（冻结） |
| F0-4 | 隐藏控件**保留 activeFocus 并执行**（保留类 B）；H1S 实证：隐藏的 运行基线诊断 被 SPACE 触发，改写 hasBaselineDiagnosis，Transactions cue 由「尚未运行基线诊断。」变为「已有基线诊断结果，可在诊断工作区查看。」 | **P0（新）** | FD4 |
| F0-5 | Agent 提问 TextArea 构成 Tab 焦点 trap（正 14/14、反 6/6 均停留），根因为 Tab 被当作字符写入 | **P0（新）** | FD3 |
| F0-6 | rail 停点无区分 accessible name（回落到窗口名） | **P1** | FD7 |
| F0-7 | rail 停点无可操作 role/pattern（非 Button/MenuItem，无 Invoke/Selection） | **P1** | FD7 |
| F0-8 | rail 无与 selected/hover 可区分的键盘焦点视觉 | **P1** | FD7 |
| F0-9 | Dashboard 焦点链完整（AppBar + rail×5 + 运行演示批次，周期 7，正反对称） | **PASS** | FD2 |
| F0-10 | Diagnosis Baseline pane：两个 pane 内 Button Tab-reachable，无 trap | **PASS** | FD3 |
| F0-11 | Diagnosis AI pane：无 trap、无幽灵停点，disabled 控件被正确跳过 | **PASS** | FD3 |
| F0-12 | 无跨 pane 内容泄漏：任一时刻仅一个 pane 的独占元素被暴露 | **PASS** | FD3 |
| F0-13 | Agent TextArea 在可见状态下可聚焦、可输入、内容留存 | **PASS** | FD5 |
| F0-14 | Transactions ListView 行与容器未暴露给 UIA（无障碍暴露缺陷，独立于键盘可达性） | **GAP（非阻塞，接受）** | F0-10 / FD5 附带 |
| F0-15 | Qt Quick `Button` 的键盘激活键是 Space；隐藏的 ENTER 不激活 | **N/A（事实）** | FD4 |
| F0-16 | 隐藏 Run Demo 重复发布状态等价（条目结构无时间戳、批次确定性）→ 该控件自身无可观察副作用 | **N/A（事实）** | FD4 + `TransactionListModel.h:15` |

### FD9. Final Counts + Delta（§15/§17）

| 类别 | 本轮定稿 | 相对 FC11 | 变化原因 |
| --- | --- | --- | --- |
| **P0** | **5** | 3 → 5 | F0-4（保留类 B，实证业务后果）+ F0-5（Agent Tab trap）新增 |
| **P1** | **3** | 3 → 3 | 不变（rail 三项，冻结入 F1） |
| **P2** | **0** | 0 | — |
| **GAP** | **1** | 3 → 1 | H1/H2 已实测闭环（不再是 GAP）；仅保留 ListView UIA 暴露这一项**明确接受的非阻塞 GAP** |
| **N/A** | **2** | 0 → 2 | 两项实测事实（Space/Enter 语义、Run Demo 幂等性） |
| **PASS** | **5** | 7 → 5 | 原 7 项中 `F0-4/7/8`（Device 正确排除、Shift+Tab 对称、无 disabled 装饰项）已并入 FD2/FD3 的序列事实；`F0-TX4/F0-CB/F0-COMM/F0-RPL` 为既有 PASS，本轮不重测，仍有效但不再单列计数 |

**Dashboard / AI / Agent / H1 / H2 不再是"未实测却已下结论"的空白区。**

### FD10. Accepted Non-blocking GAP（§14）

**唯一接受的 GAP = F0-14**：Transactions ListView 的行与容器不对 UIA 暴露 → 行级无障碍断言**无法自动化**，F1 中必须**人工/视觉验收**（与 M9-D 的鼠标路径人工 PASS 继承一致）。除此之外不存在未闭环测量项。

### FD11. F1 Frozen Scope（§16，A–G，不得改名/扩项）

```text
F1 REQUIRED(=5 P0)。冻结范围（仅此 7 项，不做全局重设计）：

A. Transactions ListView 的键盘 Tab 入口（证据表必须能被键盘抵达）
B. 隐藏 workspace 控件从 Tab chain 中排除（获取类，F0-2）
C. 隐藏后**保留的 activeFocus** 处理（保留类，F0-4；见 §13：A/B 两类都必须被验收覆盖）
D. rail 停点的 Enter/Space 激活（F0-3）
E. rail 停点的 accessible name（F0-6）
F. rail 停点的 accessible role / pattern（F0-7）
G. rail 停点的键盘焦点视觉，必须与 selected / hover 可区分（F0-8）

不做：业务状态/选择语义/导航语义/布局/信息架构/版本/图标/打包契约的任何变更。
```

### FD12. F1 Implementation Principle Preview（§17，仅预告，不在本轮实施）

```text
- 优先"页面/根级 focus gating"，而不是在各控件上散布 hack。
- rail 修复集中于 NavigationRail.qml（delegate 的 accessible 语义 + 焦点视觉 + 激活路径）。
- Transactions 修复集中于"键盘焦点入口"，不改变 ListView 内部的鼠标/键盘导航语义（M9-D 已 PASS）。
- 任何修复都不得改变：业务状态、selection 语义、导航语义、布局、信息架构、version/icon/package 契约。
- F1 必须先有 RED（可复现的失败证据），再实施；不得以删除既有测试解决 regression。
```

### FD13. Temporary Probe Cleanup（§18）

本轮使用过、且**已删除**的临时探针（全部位于被 ignore 的 `build/`）：

```text
build/e0_audit_final.ps1 / e0_audit_final2.ps1 / e0_audit_final3.ps1
build/e0_audit_h.ps1 / e0_audit_h1s.ps1
build/e0_audit_dash.ps1 / e0_audit_rail.ps1
build/e0_audit_tabs.ps1 / e0_audit_tabs2.ps1 / e0_audit_v.ps1
build/e0_dbg.ps1（上一轮 F0 measurement 的 UIA dump 探针，同属本 measurement 家族，一并删除）
```

清理**不只依赖 `git status`**：显式断言 `build/e0_audit*` / `build/e0_*` / 全仓库 `e0_audit*`·`e0_dbg*` 的 glob 结果均为 NONE（见完成报告的 Cleanup 项）。`build/` 中仍存在**更早 M9 轮次**的 ignored 探针脚本（`ps_*.ps1`、`c*_*.py`、`b5*_*.py`、`d*_*.py`、`e1_*`、`e3_*` 等）——它们不是本轮的产物，也未出现在任何 committed tree 中，本轮**不动**（各自轮次的清理责任不在本轮）。产物证据（ModbusLens 2.0.0 portable package 解包目录）保留，因为它同时也是 M9-E 的验收对象。

### FD14. 本轮明确未做（§20 boundary）

```text
未开始 F1 implementation；未开始 F2；未修改 product / QML / harness / tests / scripts；
未修 rail；未加 focus ring；未加 Accessible.*；未重新设计 UI；
未创建 v2.0.0 tag；未 push；verified LKGC 保持 4cb6e9d。
```

## 36. Status

**M9-F IN PROGRESS；Phase = Learning / Final Acceptance Design Gate；Implementation = NOT STARTED**。docs-only 本轮；verified LKGC **不变 = `4cb6e9d`**；未 push。

## F0 Final Re-review = HOLD（single scope blocker）（2026-09-19，append-only，docs-only correction）

> **F0 measurement 本身已被接受**（accepted as COMPLETE）。本轮**不重新执行任何 F0 audit**、不重新测量、不改写任何历史测量记录。
> 唯一 blocker 是 **implementation scope completeness**：冻结的 F1 scope A–G **漏掉了一个已确认的 P0**。

### FE0. HOLD 事实

```text
Review 结论：F0 Final Re-review = HOLD（single scope blocker）
F0 evidence：ACCEPTED（无新的 measurement gap）
confirmed authoritative findings：P0 = 5 / P1 = 3 / P2 = 0
blocker：authoritative finding P0-5（Agent TextArea Tab trap）**没有进入 frozen F1 A–G scope**
后果：F1 暂不授权；本轮严格 docs-only correction（不实现、不修 QML、不开始 F1/F2）
```

### FE1. Omitted-P0 RCA（为什么漏掉）

```text
Observed   F1 scope 在 §FD11 冻结为 A–G，其中没有任何一条对应 P0-5（Agent TextArea Tab trap）。
Expected   每一个 authoritative P0 都必须映射到至少一个 scope 条目——"发现"与"修复范围"必须闭合。
Evidence   §FD8 权威表第 5 行明确写着 F0-5 = P0（Tab trap，正 14/14 反 6/6）；
           而 §FD11 的 A–G 是从 Phase 1 Review 的 §16 模板继承下来的条目集合
           （该模板写于 Agent TextArea trap 被发现**之前**，当时只覆盖
             ListView 入口 / hidden 获取 / hidden 保留 / rail 四条）。
           本轮把新发现的 P0 补进了 findings 表，却没有回头把它补进 scope 列表。
Root Cause 缺少"findings → scope 覆盖性检查"这一步：表格与 scope 被分别维护，
           冻结 scope 时没有做 "每个 P0 是否都在 scope 中有对应条目" 的逐行核对。
Fix        本轮把 scope 修正为 A–H（新增 H = Agent TextArea 不得 trap keyboard traversal），
           并冻结 admission rule（§FE5）：**5 个 P0 全部必须进入 F1，3 个 P1 同样进入
           minimal F1 correction**（不是 optional polish）。
Verification 见 §FE2（P0→scope 映射表）；F1 授权后由 F1 的 RED→GREEN 矩阵兑现（§FE6）。
Regression Protection 从本轮起，scope 冻结必须附一张 **finding → scope 映射表**；
           任何 P0/P1 没有条目即视为 scope 未完成，不得进入 implementation。
```

### FE2. F0 Measurement = COMPLETE（冻结，不重新打开）

以下测量**全部冻结为 COMPLETE**，本轮及后续不得重新打开（除非出现新的独立证据来源）：

```text
Dashboard real sequence（A）           = COMPLETE
Diagnosis Baseline / AI / Agent（B）   = COMPLETE
H1 / H1S                               = COMPLETE
H2                                     = COMPLETE
H3（retained PASS，继承）              = COMPLETE
rail activation（Enter/Space）         = COMPLETE（冻结）
rail UIA semantics                     = COMPLETE（冻结）
focus visibility                       = COMPLETE（冻结）
```

Review HOLD **只针对 implementation scope completeness**，与测量质量无关。

### FE3. Authoritative Findings（冻结，编号沿用 Review 口径）

| # | Finding | Class |
| --- | --- | --- |
| **P0-1** | Transactions ListView not Tab-reachable | P0 |
| **P0-2** | hidden workspace controls leak into Tab chain | P0 |
| **P0-3** | rail Enter/Space does not activate | P0 |
| **P0-4** | hidden control retains activeFocus and can act while hidden | P0 |
| **P0-5** | **Agent TextArea traps Tab / Shift+Tab and inserts Tab characters** | P0 |
| **P1-1** | rail accessible name defect | P1 |
| **P1-2** | rail actionable role/pattern defect | P1 |
| **P1-3** | rail keyboard-focus indication defect | P1 |

（本表与 §FD8 逐条对应，仅采用 Review 的编号口径；P2 = 0 不变。历史表**不重写**。）

### FE4. Corrected F1 Scope A–H（取代 §FD11 的 A–G）

```text
A. Transactions ListView keyboard Tab entry
B. Hidden workspace controls excluded from Tab traversal
C. Hidden retained-focus handling
D. NavigationRail Enter / Space keyboard activation
E. NavigationRail meaningful accessible name
F. NavigationRail semantically appropriate actionable role / accessibility action
G. NavigationRail visible keyboard-focus indication
H. Agent TextArea must not trap keyboard traversal          ← 本轮新增（P0-5）
```

**C 的语义（明确化）**：切页后旧 hidden control **不得继续** consume keys / trigger commands / mutate hidden state。**H1S（hidden baseline diagnosis button 的 SPACE mutation）必须作为 acceptance oracle**（见 §FE6）。

**H 的语义**：Tab **离开** TextArea 进入下一合理 focus stop；Shift+Tab **返回**前一合理 focus stop；**Tab 不得继续插入 literal tab character**。

### FE5. TextArea Behavior Boundary（H 的修复边界，本轮只定契约不实现）

```text
H 的修复**不得**破坏 TextArea 的正常文本编辑。
F1 acceptance 必须同时验证：Left / Right / Up / Down / Home / End 继续保持文本编辑语义。
Tab / Shift+Tab 只用于 focus traversal。
若 Qt TextArea 存在原生机制（tabChangesFocus 或真实等价机制），
未来实现**优先使用最小原生机制**——本轮不实现，也不提前写死具体 patch。
```

### FE6. F1 Admission Rule（冻结）

```text
5 个 P0  → 全部必须进入 F1（没有例外、没有"另立项"）
3 个 P1  → 按 Phase 1 已批准规则：P1 = clear usability/accessibility defect
           ⇒ 同样进入 minimal F1 correction，**不是 optional polish**
F1 不处理 Transactions UIA exposure GAP —— 该 GAP 已被 Review 接受为
           known non-blocking evidence limitation。
```

### FE7. F1 Acceptance Matrix Preview（只补设计，不实现）

未来 F1 至少要 RED→GREEN 覆盖：

| Scope | RED → GREEN 验收内容 |
| --- | --- |
| **A** Transactions | keyboard-only Tab 能进入 ListView；进入后 **四键（Up/Down/Home/End）仍工作** |
| **B** hidden acquisition | 当前页 Tab chain 中**不再出现**任何 hidden page control |
| **C** retained focus | **H1S oracle**：hidden baseline button **不可被 Space 激活**（cue 不翻转）；**H2 oracle**：hidden Agent TextArea **不可继续接收文本/导航键**（value 不变） |
| **D** rail activation | **Enter + Space 都正确切换 workspace** |
| **E/F** rail accessibility | focused actionable item 有 **distinct name** 与 **appropriate role/action** |
| **G** rail visual | keyboard focus **可见**，且与 selected / hover 可区分 |
| **H** Agent TextArea | Tab / Shift+Tab 能 **escape**；Left/Right/Up/Down/Home/End **仍编辑** |

### FE8. Scope Boundary（F1 仍是 minimal correction）

```text
F1 不得：重新设计 NavigationRail / 改变 IA / 改变 workspace indexes /
        改变 business state semantics / 改变 Transactions selection semantics /
        改变 Diagnosis business logic / 改变 version·icon·package /
        做 full WCAG certification / 做 screen-reader certification。
```

### FE9. GAP / N/A 措辞（冻结）

```text
Transactions ListView / UIA exposure：保持 **known non-blocking GAP**，**不得写成 PASS**。
Run Demo idempotency 与 Qt Quick Button 的 Space 语义：继续作为 supporting facts。
**不为凑计数而重写历史 F0 表。**
```

### FE10. Sequence（最终流程，不再有 "conditional F1"）

```text
F0 COMPLETE
   ↓
F1 REQUIRED
   ↓
F1 Review
   ↓
F2 final Release / evidence candidate
   ↓
F3 manual acceptance + M9 closure

不再使用 "conditional F1" 表述——当前 authoritative P0/P1 已确认。
```

## F0 Final Re-review = PASS / F1 = GO（2026-09-19，append-only）

```text
F0 Final Re-review = PASS
F0 measurement     = COMPLETE
F1                 = GO（authoritative P0×5 / P1×3 / P2=0；本轮只修 A–H frozen scope）
```

### FF0. finding → scope mapping（Review 归档，冻结）

| Finding | Scope |
| --- | --- |
| **P0-1** Transactions ListView not Tab-reachable | **A** |
| **P0-2** hidden workspace controls leak into Tab chain | **B** |
| **P0-3** rail Enter/Space does not activate | **D** |
| **P0-4** hidden control retains activeFocus and can act while hidden | **C** |
| **P0-5** Agent TextArea traps Tab/Shift+Tab and inserts Tab characters | **H** |
| **P1-1** rail accessible name defect | **E** |
| **P1-2** rail actionable role/pattern defect | **F** |
| **P1-3** rail keyboard-focus indication defect | **G** |

```text
Transactions UIA exposure = known non-blocking GAP，**不进入 F1**。
```

### FF1. F1 边界（Review 重申）

```text
不做：重新设计 UI / 改 IA / 改 version·icon·package / WCAG certification /
      完整 screen-reader work / 清 StatisticsOverview / installer·signing·publication / 开始 F2。
不 push。verified LKGC 继续 4cb6e9d。
F1 分类（§39）：**behavior-bearing**（QML focus/accessibility 行为变化 + 可能 harness 变化），
                 不得按 "只是 accessibility" 误分类为 docs-only。
```## F1 — Minimal Accessibility / Focus Correction（2026-09-19，behavior-bearing）

> F1 = GO（authoritative P0×5 / P1×3 / P2=0；只修 A–H frozen scope）。本轮**改产品代码**（QML + harness），
> 按 §39 分类为 **behavior-bearing**；verified LKGC **不推进**（保持 `4cb6e9d`，等人工 Review）。

### FG0. Preflight

```text
HEAD = f4b2e98（main，clean）；verified LKGC = 4cb6e9d；CMake project VERSION = 2.0.0；
v1 tag object 2cee626 / v1.0.0^{commit} ae067ab；v2.0.0 absent；
origin/main a40d935（ahead 84 / behind 0）；git diff --check PASS —— 全部相符。
基线（改动前，Release）：smoke PASS / nav PASS（A–T，M DEFERRED）/ geometry PASS（18 printed segments, 0 GEOFAIL）/ ctest 26/26。
```

### FG1. Source re-read（§2，实施前实读当前源码，不用 F0 文档代替）

```text
Main.qml           : StackLayout#workspaceHost，currentIndex <- rail.currentWorkspaceIndex；
                     5 个 page child（Transactions 0 / Dashboard 1 / Communication 2 / Replay 3 / Diagnosis 4）；
                     workspace index 常量与 children 顺序 1:1；**无 FocusScope**；page 无 enabled/visible 绑定。
NavigationRail.qml : delegate = bare Item + MouseArea + Keys(Return/Enter/Space) + focusPolicy StrongFocus/NoFocus；
                     objectName navItem_<i>；6 entries，设备 enabled:false。
TransactionsPage   : ListView focus:true（= scope 初始焦点，**不是** Tab 可达）；Keys.onPressed 仅补 Home/End；
                     delegate Keys.forwardTo:[transactionsList]；TapHandler → currentIndex+forceActiveFocus。
DiagnosisPage      : TabBar(diagnosisTabs) + 3 pane；agentQuestionInput = TextArea（无 focus 相关属性）。
Dashboard/Communication/Replay : page 内 controls 无特殊 focus 绑定（Replay 仅 replayLoadButton）。
```

### FG2. RED（§4–§10，真实键鼠注入，改动前 Release build）

| Scope | RED 结果 | 判定 |
| --- | --- | --- |
| **A** Transactions entry | 14 次 Tab 的链 = `appBarClearResults` + 5× rail stop，**从不进入 transactionsList** | **CONFIRMED** |
| **B** hidden acquisition | **NOT REPRODUCED**：5 workspace × 20 Tab 扫描，链中只有当前页 controls + AppBar + rail；且「隐藏控件持焦状态下继续 Tab」两种 hybrid 场景（Dashboard 隐藏 Run Demo / Diagnosis 隐藏 baseline 按钮）遍历都进入**可见页**的链 | **F0 的 P0-2 由本轮推翻**（见 FG3） |
| **C** retained focus | H1S：键盘聚焦（从不点击）运行基线诊断 → 切到 Transactions → 注入 SPACE ⇒ **隐藏按钮真实执行**，cue 由「尚未运行基线诊断。」翻转为「已有基线诊断结果…」；H2：隐藏 Agent TextArea 仍持焦并吃键（value `[]` → `[Z]`） | **CONFIRMED** |
| **D** rail Enter/Space | **NOT REPRODUCED**：index-resolved 实测（锚点 + Tab×k + before-key 读数）Enter 与 Space 对 k=1..5 **全部正确切换**到对应 workspace；Tab 本身不切换 | **F0 的 P0-3 由本轮推翻**（见 FG3） |
| **E** rail name | 每个 rail 停点 UIA Name = **ModbusLens**（窗口名），AutomationId 回落窗口 | **CONFIRMED** |
| **F** rail role/action | ControlType = **Window**；patterns = Value/Window/Transform，**无 Invoke/Selection** | **CONFIRMED** |
| **G** rail focus visual | 选中态 = surface+3px accent bar+bold；键盘焦点在任何 item 上**无独立可视通道** | **CONFIRMED** |
| **H** Agent TextArea trap | Tab 停留并写入 `\t`（`[]` → `[\t]` → `[\t\t]`），Shift+Tab 同样停留 | **CONFIRMED** |

### FG3. F0 两项 P0 的推翻 + RCA（append-only 修正，不删改历史）

```text
P0-2（hidden workspace controls leak into Tab chain）→ **NOT REPRODUCED / 撤回**
P0-3（rail Enter/Space does not activate）           → **NOT REPRODUCED / 撤回**

Evidence（本轮，oracle-verified）
  B：Transactions/Dashboard/Communication/Replay/Diagnosis 各 20 次 Tab 的链，
     结构判定 focused item 所属 page（parent 链，不用名字猜）——从不落到非当前页；
     另有 hybrid 场景：让隐藏页控件「已经持焦」再 Tab，遍历仍进入可见页链。
  D：锚点（AppBar 按钮，真实 Control）→ Tab×k → **先读 before-key workspace** → 注入 Enter/Space
     → k=1..5 全部得到 Transactions/Dashboard/Communication/Replay/Diagnosis；
     k=5 目标即 Diagnosis ⇒ 「未变化」是正确的通过结果；Tab 单独从不改变 workspace。
     自动化版本（FD/FE）进一步用 railIndexOf(focusItem()) 证明焦点确实在 navItem_(k-1) 上。

Root Cause（探针缺陷类，非产品缺陷）
  F0 continuation 的两项测量发生在 workspace oracle 规则引入**之前**：
  ① 导航只靠坐标点击，未验证实际 workspace（PD-3 已证明窗口非 foreground 时首次点击会被当
     activation click 消费）——"Communication 链里出现 dashboardRunDemo"与"Diagnosis 链里出现
     replayLoadButton"正是这种上下文错位的典型形态；
  ② rail 停点的 UIA 身份＝窗口本身，因此"焦点在 rail 项上"与"焦点根本不在任何 rail 项上"在
     UIA 读数上**无法区分**，F0 的 Enter/Space 注入无法证明按键真的送达了 rail delegate。
Verification
  本轮 `--qml-focus-check` 的 FB（5 workspace × 16 Tab，结构判定）与 FD/FE（5 × 2，railIndexOf 证明）
  把两项都变成**可回归断言**；B/D 的 scope 条目因此从"修复"转为"回归保护"（见 FG6）。
Regression Protection
  FB / FD / FE / FF 已入 committed 回归（qml_focus_check），任何未来的 hidden 泄漏或 rail 激活回归
  都会直接失败。
```

### FG4. Implementation（A–H）

```text
A  src/ui/qml/pages/TransactionsPage.qml
   ListView 增加 `activeFocusOnTab: true`。`focus: true` 只是 scope 初始焦点（不构成 Tab 可达），
   这就是 F0-1 的根因；Qt 在 view 获得焦点时**不会**移动 currentIndex ⇒ 无 select-on-focus（FA/FA2 断言）。

B/C  src/ui/qml/Main.qml（**页面级**门控，唯一机制，非逐控件 patch）
   5 个 page 各加一行：`enabled: workspaceHost.currentIndex === workspace<X>Index`。
   效果：非当前页整棵子树 enabled=false ⇒ ①其 controls 不进入 Tab 遍历；②Qt 在 disable 时清除
   子树内 activeFocus ⇒ 隐藏控件不再消费按键。**只改交互/焦点门控**，不触碰 source/statistics/
   diagnosis/drafts/selection 与任何 page-local 契约（隐藏 ≠ 清空）。

D/E/F/G  src/ui/qml/components/NavigationRail.qml（Option A：标准 actionable Control）
   delegate 由 bare Item+MouseArea 改为 `Button`（AbstractButton family）：
   · E: `text: modelData.label` ⇒ accessible name 属于**可激活对象本身**（每站不同）
   · F: AbstractButton 提供 button role + activation action（UIA 实测 Invoke）
   · D: Space 走 AbstractButton 原生激活（onClicked → activate()，无双击活）；Return/Enter 保留显式
        Keys 处理器（AbstractButton 不处理 Enter——H1S 已实测）
   · G: 新增 `Rectangle` outline，`visible: navItem.visualFocus`（Control 属性，仅键盘焦点为真），
        1px 内缩 ⇒ 不占布局、不与 selected（surface+accent+bold）/hover（无视觉）混淆
   · 冻结：`background: null` + `contentItem: null` + `padding: 0`，width/height/spacing/选中视觉
        与 Device disabled 样式一律照旧；`focusPolicy: TabFocus`（Tab 可达但**点击不夺焦**——保持
        bare Item 时代被 MouseArea 屏蔽的点击焦点行为）

H  src/ui/qml/pages/DiagnosisPage.qml
   Agent TextArea 增加 Tab/Backtab 处理器：`agentQuestionInput.nextItemInFocusChain(true|false)`
   + `forceActiveFocus(Qt.TabFocusReason|BacktabFocusReason)`，并 accept 事件。
   **机制审计**：Qt Quick **没有** `tabChangesFocus`（该属性只存在于 QtWidgets——已在本机
   Qt 6.11.1 头文件层面核对）；QQuickTextEdit 默认消费 Tab 写入 \t，因此改用 Qt 自己的
   focus-chain API（`Item.nextItemInFocusChain`，真实 Q_INVOKABLE）完成遍历；
   只改这两个键，编辑键（方向/Home/End/输入）语义不变；未写全局 Keys 吞键处理器。
```

### FG5. Automated Regression（§25/§26：`--qml-focus-check` + ctest `qml_focus_check`）

```text
实现方式：与既有 test-mode 架构一致——真实 app 加载自己的 shipped QML，复用同一批合成事件 seam
（鼠标经 window 投递、按键经 window/activeFocusItem 投递），断言可观察契约；不是用户可见命令。
场景与结果（Release build，FOCUS CHECK PASS）：
  FA  keyboard-only Tab 进入 transactionsList 且 currentIndex 仍为 -1（无 select-on-focus）
  FA2 entry 后四键可用：End → currentIndex=3/selectedRow=3；Home → 0/0
  FB  5 个 workspace × 16 Tab：链中出现的 page 只可能是当前页（结构判定），无 hidden 泄漏
  FC  hidden retention：隐藏的 baseline 按钮**不执行**（hasBaselineDiagnosis 保持 false），
      且同一按钮在可见页 Space 仍正常执行（对照）
  FD  rail Enter 激活 5/5（先证明焦点在 navItem_(k-1)）
  FE  rail Space 激活 5/5
  FF  Device：enabled=false、非 Tab stop、16 Tab 不出现、点击不改变 index
  FG  Agent TextArea：Tab 离开且 draft 不变
  FH  Agent TextArea：Shift+Tab 反向离开且 draft 不变
  FI  鼠标路径四键回归（row click → End/Home/Down/Up 全部经 currentIndex → selectRow）
harness 自身的两处缺陷（如实记录，均为工具缺陷）
  · 合成按键不带 text ⇒ 文本编辑器不插入字符（曾使 H2 的"可见输入"检查失败）→ 新增 sendTextKey()
  · 锚点选了 AppBar「清空结果」（它会清空会话结果）⇒ 行数断言失败；修正为**先锚点后发布 demo 批次**
```

### FG6. GREEN（外部 UIA，Release build；与 RED 同一批 oracle）

```text
A  press 7 落到 list 停点：焦点进入前 transactionDetailEmpty 仍在（无 select-on-focus），
   END 之后 detail pane 填充（transactionDetailDevice 暴露）⇒ 键盘用户可进入**并使用**证据表。
   UIA 仍无法命名 ListView 容器（known non-blocking GAP，见 FG8）。
B  26-Tab 长扫描（Communication）：链 = commPortCombo/刷新串口/commBaudCombo/4×Spin/AppBar/rail×5，
   无任何隐藏页控件。
C  H1S：隐藏按钮持焦后切页 → activeFocus 回落窗口；SPACE ⇒ cue 保持「尚未运行基线诊断。」
   （RED 时为翻转）⇒ 隐藏控件不再执行。
   H2：隐藏字段注入 b/Home/End/Up/Down 后 value 仍只含可见期输入的字符（无新增字符）。
   H3：选中行「设备 1」在往返后保持不变，隐藏期间四键不改变 selection。
D  index-resolved：Enter k=1..5 与 Space k=2..5 全部正确切换（自动化 FD/FE 为 5/5 ×2）。
E/F 5 个 rail 停点 UIA：ControlType=**Button**，Name=**事务/总览/通信/回放/诊断**（各不相同），
   kbd=True，patterns=**[Invoke, Value]**（RED 时为 Window / "ModbusLens" / 无 Invoke）。
G  截图：选中=事务（accent bar+bold+surface），键盘焦点=总览（独立描边）⇒ 两态可区分。
H  Tab 从 TextArea 离开到 appBarClearResults，text 不变；离开后输入不进入该字段。
```

### FG7. Focus sequences（§28，5 workspace + Diagnosis 3 tabs）

```text
Transactions  : AppBar + rail×5 + transactionsList（周期 7；RED 时周期 6，list 缺失）
Dashboard     : AppBar + rail×5 + dashboardRunDemo
Communication : AppBar + rail×5 + commPortCombo/刷新串口/commBaudCombo/4×Spin
Replay        : AppBar + rail×5 + replayLoadButton
Diagnosis     : AppBar + rail×5 + 3×TabItem + 运行基线诊断 + 清除诊断（Baseline pane）
                AI pane：仅有 disabled 的 ask/cancel ⇒ 正确不入链
                Agent pane：TabItem + diagnosisAgentQuestion（**可进入**，且 Tab 可离开）
无 trap；无 hidden 页控件；Device 不可达（全部由 qml_focus_check 断言）。
```

### FG8. GAP / 观测（不改写历史）

```text
known non-blocking GAP（不变，未在 F1 处理）：Transactions ListView 的行与容器**不暴露给 UIA**，
  行级无障碍断言仍需人工/视觉验收（FG6-A 已用用户可见的 detail-pane oracle 代替）。
新观测（**P2 / DEFER**，明确不在 F1 A–H 范围内，未修）：
  O1 AppButton（自绘 background）与 Fusion ComboBox 在本环境**没有可见键盘焦点指示**
     （对照截图 m9f-f1-standard-control-focus-comparison-1024x720.png 中 commPortCombo 已持焦但外观不变）
     —— 这使 rail 的焦点描边成为当前唯一明确可见的键盘焦点通道。
  O2 Transactions ListView 自身没有独立焦点描边，其视觉通道是行选中（M9-D 契约）；F1 未改变该契约。
  O3 外部 UIA 对 Agent TextArea 的 ValuePattern 读取存在滞后（工具伪影，非产品行为）；in-process
     property 断言（无滞后）为准。
```

### FG9. 回归与证据

```text
Debug   ：build OK（无新警告）；ctest **27/27**（新增 qml_focus_check）
Release ：configure/build OK；ctest **27/27**（证明修复不是 Debug-only，§34）
Release 模式：smoke PASS（identity 2.0.0 + icon 6 尺寸）/ nav PASS（A–T，M DEFERRED）/
              geometry PASS（**18 printed segments, 0 GEOFAIL**）/ focus CHECK PASS
几何冻结：rail 宽度在 18 段中恒为 **56**（无 material geometry change）；无 item size/宽度漂移
业务冻结：nav 断言 "navigation changed no business values"；focus check 的 rail 激活只改
          presentation index（FF 另外断言 Device 点击不改变 index）；诊断/回放/统计/AI 状态零改动
package/version/icon：CMakeLists 仅新增一条 test 注册；make_package.py/版本/PE/icon/README 零改动
```

### FG10. Visual Evidence（§30/§36，状态 = WAITING FOR USER）

```text
docs/assets/screenshots/m9f-f1-rail-keyboard-focus-vs-selected-1024x720.png   （选中 事务 / 焦点 总览）
docs/assets/screenshots/m9f-f1-transactions-list-keyboard-entry-1024x720.png  （键盘进入 list + END 选中 + detail 填充）
docs/assets/screenshots/m9f-f1-standard-control-focus-comparison-1024x720.png （标准 Control 持焦对照）
环境：125% DPI（物理 1280×900 = 逻辑 1024×720，与 M9-D 截图口径一致）。
**Focus Visual Review = WAITING FOR USER** —— ZCode **不**自标 manual PASS；G 的最终 PASS 取决于用户确认
「键盘焦点看得见且与 selected/hover 明确不同」。
```

### FG11. Problems / RCA（§37）

```text
P1 harness/合成按键不带 text（Observed: 可见输入未进入字段 → Expected: 进入；Root Cause: QKeyEvent 无 text
   时文本编辑器不插入；Fix: sendTextKey()；Verification: H2 可见输入检查转 PASS；
    Regression Protection: 该 seam 现被 H2/FC 使用）
P2 harness/锚点副作用（Observed: FA2 行数断言 4→0；Root Cause: 锚点 AppBar 按钮本身是"清空结果"动作；
    Fix: 先锚点后发布批次；Verification: FA2/FI PASS；Regression Protection: 注释固化为 harness 规则）
P3 probe/UIA 文本读取滞后（见 FG8-O3；归类工具伪影，不记为产品缺陷）
P4 probe/Context 错位推翻了两项 F0 P0（见 FG3；归类探针缺陷，不记为产品缺陷，历史记录不删改）
```

### FG12. Git / 边界

```text
F1 = behavior-bearing（QML focus/accessibility 行为 + harness 新增），**不是** docs-only。
未 amend f4b2e98；未 rebase；未 push；未创建 v2.0.0 tag；verified LKGC 保持 **4cb6e9d**（等人工 Review）。
未做：UI 重新设计 / IA / version·icon·package / WCAG certification / screen-reader 完整工作 /
      StatisticsOverview 清理 / installer·signing·publication / F2。
```

## F1 Review = HOLD（focus visibility 契约冲突 + TextArea edit-key 证据缺口）→ Correction（2026-09-19，append-only，behavior-bearing）

> **F1 主体 A–H 的功能/无障碍修复不被推翻、不重新设计**：A（ListView 键盘入口）/ B·C（页面级 focus gating）/ D（rail Enter/Space）/ E/F（rail name·role·Invoke）/ G 机制（rail 焦点描边）/ H（TextArea Tab·Backtab 遍历）/ geometry / version·icon·package 全部**保持已接受**。
> HOLD 的两个 blocker：
> ①**focus-visibility**：对部分 Tab-reachable controls，键盘焦点**没有可感知指示**，而 F1 报告把它们错分类为 P2/DEFER —— 冻结规则是「任何 Tab-reachable actionable control 获得键盘焦点后必须有可感知的 focus indication，不能靠 selected/hover/pressed/value 反推」，这是 basic keyboard accessibility，**不是 optional polish**；
> ②**TextArea edit-key regression 只有 source-level 推断，缺 runtime proof**。
> **保留结论**：B/D 的 F0 RED 属 probe/oracle defect（非 confirmed product defect）；B 机制主要服务于 C 与 focus isolation，D 在 rail refactor 后是 regression-protected contract。
> verified LKGC 保持 `4cb6e9d`。

### FH0. Focus-visibility contract（re-freeze，最终规则）

```text
任何 Tab-reachable actionable control 获得键盘焦点后，
用户必须有可感知的 focus indication。
不能只依赖 selected / hover / pressed / current value 去猜当前键盘焦点。
分类域收紧：focus cue 缺失 = P1（不再允许 P2/DEFER 表示「完全没有 focus cue」）。
UIA read latency 等继续是 tooling note，不是 product P1。
```

### FH1. Focused-control inventory（§4–§9，在 F1 candidate 上逐类型实测）

方法：真实 Tab 走链（UIA rect/point 验证落点）→ 未聚焦/聚焦两次截取控件矩形 → **像素差分**
（changedSamples = 采样差>0；strongSamples = 差≥24；0 = 渲染完全不变）。判据只允许 PASS / P1。

| 类型 | 控件（实测对象） | Tab reachable | activeFocus | 改动前像素差 | 结论 |
| --- | --- | --- | --- | --- | --- |
| A. AppButton（共享包装器） | dashboardRunDemo（primary tone） | ✔ | ✔ | **changed=0 / strong=0** | **P1**（自定义 background 完全不读 focus ⇒ 证明性无 cue；旧注释宣称的 "platform outline" 从未渲染） |
| B. ComboBox | commPortCombo（自定义 background） | ✔ | ✔ | **changed=0 / strong=0** | **P1**（自定义 background 覆盖了 Fusion 的 activeFocus ring ⇒ 零渲染变化） |
| C. SpinBox | commSlaveSpin（Fusion 默认） | ✔ | ✔ | changed=456 / strong=275 / maxΔ=109 | **PASS**（Fusion highlighted outline，zero source diff） |
| D. TabButton | 基线诊断 tab（自定义 background） | ✔ | ✔ | **changed=0 / strong=0** | **P1**（background 只读 `checked`，从不读 focus） |
| E. TextArea | diagnosisAgentQuestion（Agent pane） | ✔ | ✔ | changed=1913 / strong=1252 / maxΔ=144 | **PASS**（caret + placeholder 消失 = 文本编辑器的标准焦点通道；不因是文本编辑器自动豁免——是有实测渲染差才 PASS） |
| F. Transactions ListView | transactionsList | ✔（F1-A 修复后） | ✔ | **单步差分中 list 区域贡献 0 像素**（165 全部来自 rail ring 离开） | **P1**（行选中是业务状态，不能当 list 的焦点指示） |
| G. NavigationRail AbstractButton | navItem_1 | ✔ | ✔ | changed=165 / strong=165 / maxΔ=208 | **PASS**（F1 的 visualFocus 描边；保留，截图为 candidate evidence，最终由用户 Manual Review） |
| （对照组）Fusion plain Button | diagnosisRunBaselineButton | ✔ | ✔ | 边框 (200,200,200) → **(91,129,173)** | **PASS**（平台样式自有高亮描边） |

```text
新确认 P1（F1 期间发现，按本轮规则从 P2/DEFER 纠正为 P1）：
  P1-4  AppButton 无键盘焦点指示（影响全部 AppButton 实例）
  P1-5  ComboBox 无键盘焦点指示（commPortCombo 自定义 background；commBaudCombo 同规则处理）
  P1-6  TabButton 无键盘焦点指示（3 个诊断 tab）
  P1-7  Transactions ListView 无独立焦点指示
（UIA ValuePattern 读取滞后维持 tooling note；其余真正 optional 的观察才继续 P2。）
```

### FH2. Minimal correction（§5–§10：只修实际受影响类型，不铺全局重设计）

```text
AppButton.qml        border 即焦点通道：visualFocus ⇒ border.width 2 + 主色 secondary=DS.primary /
                     primary=DS.background（蓝底上蓝边会消失）；未聚焦渲染零改动。
TransactionsPage.qml 新增 transactionsListFocusRing（内缩 2px outline，DS.primary）：
                     visible = list.activeFocus || currentItem.activeFocus
                     （M9-D D6：view 会把 active focus 交给当前行，所以两种持有者都算「list 持焦」）；
                     不占布局、不触碰行几何/选中配色/detail 逻辑/list 尺寸。
CommunicationPage.qml commPortCombo + commBaudCombo 各加内缩 outline（objectName 供 FK 断言）：
                     未聚焦渲染零改动；不重做 DesignSystem、不改 Fusion。
DiagnosisPage.qml    3 个 TabButton 的 background border 加入 visualFocus 分支
                     （focus 边框优先于 selected 边框 ⇒ 两态可区分）；未聚焦零改动。
rail                 现有 visualFocus 描边**保留不动**（本轮未触碰 NavigationRail.qml）。
```

### FH3. TextArea runtime edit-key proof（§11/§12）

```text
新增 FL（qml_focus_check 内，deterministic offline Agent state）：
  草稿 = "abc\ndef"（多行），cursorPosition 显式定位后逐键注入并断言实际结果：
  Left  : 4 → 3；Right : 3 → 4；Home : 5 → 4；End : 4 → 7；Up : 4 → 0；Down : 1 → 5
  每键同时断言：text 内容不变（草稿不被导航键改写）、workspace index 不变（不切页）、
  hasBaselineDiagnosis 不变（不触发其它控件）。
  ⇒ 不再是「handler 没处理这些键」的推断，而是 cursor/text/workspace 的实测结果。
FG/FH 重跑：Tab/Shift+Tab 逃脱 + draft 不变 —— 依旧 PASS（新 edit-key 测试未破坏遍历契约）。
```

### FH4. Automated focus protection（§13：qml_focus_check 扩展）

```text
FJ  ListView 焦点环机器证明：keyboard 聚焦 list ⇒ transactionsListFocusRing.visible==true；
    Tab 离开 ⇒ visible==false（indicator property/state 级断言；"好不好看"留给人工）。
FK  逐类型 focus indication 状态断言：
    AppButton（secondary）visualFocus=true 且 background.border.width 2/1 随焦切换（前向遍历 +
    指针同一性定位——修掉了"锚点点击是 MouseFocusReason、Shift+Tab 又走错方向"的探针缺陷）；
    AppButton（primary）同上；ComboBox activeFocus=true ⇔ commPortComboFocusRing.visible；
    SpinBox activeFocus=true（style-owned indication，zero source diff）；
    TabButton visualFocus=true 且 background.border.width=2。
FL  见 FH3。
```

### FH5. Post-fix pixel re-measurement（同一 oracle）

| 类型 | 改动前 | 改动后 |
| --- | --- | --- |
| AppButton（primary） | strong=0 | **strong=324 / maxΔ=208** |
| ComboBox（commPortCombo） | strong=0 | **strong=274 / maxΔ=208** |
| TabButton（基线诊断） | strong=0 | **strong=806 / maxΔ=208** |
| ListView（单步差分） | strong=165（全部来自 rail ring 离开） | **strong=2457**（含新 list ring） |
| SpinBox / TextArea / rail（未触碰） | 456 / 1913 / 165 | 456 / 1913 / 165（不变，zero diff 确认） |

### FH6. 回归与门禁

```text
qml_focus_check：FOCUS CHECK PASS（FA/FJ/FB×5/FC/FD·FE×5/FF/FK/FL/FG/FH/FI）
qml_smoke：SMOKE IDENTITY PASS（2.0.0 + icon 6 尺寸）
qml_nav：NAV CHECK PASS（A–T，M DEFERRED，语义不变）
qml_geometry：18 printed segments / 0 GEOFAIL / rail 宽度恒 56（焦点指示全部内缩，无尺寸变化）
ctest：Debug **27/27** + Release **27/27**（qml_focus_check 注册不变，仅场景扩展）
Release：configure/build + ctest + qml_focus_check 全 PASS（完整 ZIP 仍留 F2，未重新打包）
业务冻结：rail 激活只改 presentation index；FL 明确断言编辑键不切 workspace、不触发其它控件
```

### FH7. Final visual evidence（§14/§15）

```text
docs/assets/screenshots/m9f-f1-rail-keyboard-focus-vs-selected-1024x720.png   （选中=事务 / 键盘焦点=总览）
docs/assets/screenshots/m9f-f1-transactions-list-keyboard-entry-1024x720.png  （list 焦点环 + 行选中 + detail，两态可区分）
docs/assets/screenshots/m9f-f1-standard-control-focus-comparison-1024x720.png （keyboard-focused ComboBox 带环）
docs/assets/screenshots/m9f-f1-appbutton-keyboard-focus-1024x720.png          （primary-tone AppButton 亮色焦点描边）
125% DPI（物理 1280×900＝逻辑 1024×720）。截图只证明 candidate rendering/integrity；
**Focus Visual Review = WAITING FOR USER** —— 不得 ZCode 自标 PASS。
```

### FH8. Problems / RCA（本轮新增）

```text
P5 探针/元素句柄在页面隐藏时预取 ⇒ rect 失效（inventory 首轮大面积 NOT REACHED/NOT FOUND）；
   Fix = 导航验证后再解析元素；P6 探针/焦点落点用 rect 匹配对 SpinBox 内层 input 失配 ⇒ 改 point-in-rect；
   P7 探针/锚点点击是 MouseFocusReason 且 Shift+Tab 方向错误 ⇒ FK 改前向遍历 + 指针同一性；
   P8 探针/截图脚本的固定两连 Tab 未验证落点 ⇒ 改为 walk-and-verify。
   全部为工具缺陷，不记产品缺陷。
```

### FH9. Git / 边界

```text
本轮 = behavior-bearing F1 correction（QML focus rendering + harness 场景扩展）。
未 amend 736d957；未 rebase；未 push；未创建 v2.0.0 tag；verified LKGC 保持 4cb6e9d；
未开始 F2；未重新打包；未触碰 Controller/Core/业务模型/打包脚本/version·icon 资产/samples。
```

## F1 Focus Visual Review = HOLD（TabButton 焦点不可感知）→ Minimal Correction（2026-09-19，append-only，behavior-bearing）

> 人工实机 Review = HOLD：**Diagnosis TabButtons 虽在真实 Tab traversal 中，键盘焦点却肉眼不可感知**。
> 用户实际现象：运行基线诊断 / 清除诊断 之间需要额外若干次 Tab 才能循环回来——这些"无视觉变化"的停点就是 3 个 TabButtons。
> **重要澄清**：Tab **本来就不应**自动切换 selected page；本缺陷**不是** Tab activation failure，而是 TabButton 的 keyboard focus **缺少足够可感知的 visual indication**。
> 因此本轮**不改 focus chain、不改 selected/currentIndex 语义、不改 Tab activation**，只做最小 TabButton focus-visual correction。
> verified LKGC 保持 `4cb6e9d`。

### FI0. Reproduce（§1，真实场景）

```text
Diagnosis / Baseline selected → 点击 运行基线诊断（建立非 tab 焦点）→ 逐次 Tab 并记录 focus：
  press[8]  = 基线诊断（tab 0）  ; pane = Baseline
  press[9]  = AI 解释（tab 1）   ; pane = Baseline
  press[10] = Agent 问答（tab 2）; pane = Baseline
  press[11] = 运行基线诊断 → press[12] = 清除诊断 …
RED 事实：focus state 确实落在 TabButton（UIA Name 正确、pane 始终 Baseline ⇒ 只移动焦点不切换页面），
         但当前视觉上用户无法可靠辨认。
```

### FI1. Root Cause（§2，以真实 rendering 为准）

对每个 tab 分别取「未聚焦 / 该 tab 聚焦」两次控件矩形截图并逐像素比较（HEAD `5089840` 的实现）：

```text
tab0（已选中）: 未聚焦 topBorder=(177,185,198) → 聚焦 (99,147,201)
tab1（未选中）: 未聚焦 topBorder=(226,230,235) → 聚焦 (99,147,201)
tab2（未选中）: 同上
三次测量的 bg / geometry / label 像素**完全不变**，只有既有边框的色相变化；
像素差 ~3249/13532（24%），maxΔ≈208。
```

```text
为什么"自动 pixel-diff 认为有变化"与"人工看不出"不矛盾：
  ① 自动判据测的是「是否有渲染变化」（changed/strong 采样计数），不是「是否可辨认」；
  ② 上一轮 FK 断言读的是实现属性（background.border.width == 2），同样不是可辨认性；
  ③ 该 cue 与 selected 使用的是**同一条视觉通道**（tab 的边框）：selected 已经占用了边框，
     聚焦只是把这条边框"换成另一种颜色、加粗 1px"，于是两态读起来是同一类信号，
     用户在完整 UI 里无法据此判断"下一次 activation 会作用在哪个 tab"；
  ④ 125% DPI 下 2px 逻辑边框 ≈ 2.5 物理像素，且未选中 tab 的常态边框极浅（#E2E6EB），
     蓝色边框在连续三个 tab 上逐次出现时缺少可抓取的形状差异。
⇒ 结论：cue 存在但**不可靠可感知**，属 §2 所列「focus color 对比不足 / focus 与普通边框过于相似」。
```

### FI2. Visual Contract（§3，冻结）

```text
selected tab 与 keyboard-focused tab 必须肉眼容易区分：
  例：Baseline = selected，AI = keyboard focused ⇒ 一眼可知「页面仍是 Baseline，
      但下一次 keyboard activation 会作用在 AI」。
focus cue 不得依赖 bold selected text 或 selected background。
Tab 只移动 focus；Space/正常 activation 才改变 selected（本轮不改）。
```

### FI3. Minimal Fix（§4）

```text
src/ui/qml/pages/DiagnosisPage.qml
  ① 3 个 TabButton 的 background **恢复原样**（border.color = checked ? frozenActiveTabBorder : DS.border；
     border.width = 1）——selected 外观零改动，focus 不再借用 selected 的边框通道；
  ② 每个 TabButton 新增一个**独立的内缩焦点环**（附加视觉元素，而非既有元素的变体）：
     Rectangle { objectName: "diagnosisTab<X>FocusRing"; anchors.fill: parent; anchors.margins: 2;
                 radius: 2; color: "transparent"; border.color: DS.primary; border.width: 2;
                 visible: <tab>.visualFocus }
不占 layout space（内缩 2px）；不改 tab height / TabBar geometry / selected appearance / DesignSystem contract；
不依赖 bold 文本或 selected background；transparent 填充 ⇒ 文本可读、点击不受阻。
```

### FI4. GREEN（§5）

```text
自动：FK 断言改为「焦点环 object visible == true（且 selected 边框仍为 1）」+「焦点移走后环关闭」——
      FOCUS [FK] PASS: TabButton inner focus ring visible; selected border untouched (width=1)
      FOCUS [FK] PASS: TabButton ring off after focus moved on
实测渲染（同一 oracle）：聚焦时**既有边框逐字节不变**，行 5–7 出现 (47,111,183)=DS.primary 满饱和 2px 环；
      tab0 像素差 2821/13532、tab1 2820/13532（变化的是**新增元素**，不再是边框换色）。
人工候选（截图）：Baseline = selected（bold + 白底 + 灰边框，下方内容仍是 Baseline）
                  AI = keyboard focused（蓝色内环）⇒ 两态同时清楚、互不淹没。
Tab 仍只移动 focus：每次停点 pane 均为 Baseline（FI0/FI4 实测）。
```

### FI5. Regression（§6）

```text
qml_focus_check: FOCUS CHECK PASS（FA/FJ/FB×5/FC/FD·FE×5/FF/FK/FL/FG/FH/FI）
qml_smoke: SMOKE IDENTITY PASS
qml_nav:   NAV CHECK PASS（A–T，M DEFERRED）
qml_geometry: 18 printed segments / 0 GEOFAIL / rail 宽度恒 56
ctest: Debug 27/27 + Release 27/27；Release configure/build + qml_focus_check + geometry 全 PASS
```

### FI6. Evidence（§7）

```text
docs/assets/screenshots/m9f-f1-diagnosis-tab-focus-vs-selected-1024x720.png
  （Diagnosis：Baseline = selected 且下方内容是 Baseline；AI = keyboard focused 带内环；
    125% DPI，物理 1280×900 = 逻辑 1024×720）
**Focus Visual Review = WAITING FOR USER**（继续；ZCode 不自标 PASS）。
```

### FI7. Problems / RCA（工具缺陷）

```text
P9  PowerShell 非 ASCII 字面量再次被 ANSI 破坏（`$fn -eq "基线诊断"` 永不成立，导致 focused 截图静默缺失）
    ⇒ 改用 rect 同一性识别 tab（不比较中文名）；
P10 多行 if 条件被 PowerShell 误解析 ⇒ 条件写入单行；
P11 ShotWindow 使用相对路径把截图写到了仓库根目录 ⇒ 已移入 docs/assets/screenshots 并清理根目录残留。
以上均为探针/取证工具缺陷，不记产品缺陷。
```

### FI8. Git / 边界

```text
本轮 = behavior-bearing correction（QML focus rendering + harness 断言）。
未 amend 5089840；未 rebase；未 push；未创建 v2.0.0 tag；verified LKGC 保持 4cb6e9d；
未开始 F2；未重新打包；未改 focus chain / selected semantics / Tab activation / geometry / DS contract。
```

## F1 Re-review = PASS / Focus Visual Review = PASS / F1 = COMPLETE → F2 = GO（2026-09-19，append-only）

```text
M9-F F1 Re-review = PASS
Focus Visual Review = PASS
M9-F F1 = COMPLETE
accepted F1 behavior tree = b237ddc
verified LKGC 暂时仍 = 4cb6e9d（F2 candidate 尚未人工 acceptance，推进留给 F3 closure）
F2 = GO
```

### FJ0. 人工视觉验收归档（用户确认）

| # | 人工验收项 | 结果 |
| --- | --- | --- |
| 1 | rail selected vs keyboard focus | **PASS** |
| 2 | Transactions ListView keyboard focus | **PASS** |
| 3 | ComboBox keyboard focus | **PASS** |
| 4 | AppButton keyboard focus | **PASS** |
| 5 | Diagnosis TabButton selected vs keyboard focus | **PASS** |

```text
最终 TabButton 证据（人工确认的状态组合）：
  Baseline = selected（下方内容仍是 Baseline）
  AI       = keyboard focused（独立内缩蓝环）
  ⇒ selected ≠ focused 一眼可辨，且 Tab 未切换页面（非 activation）。
```

### FJ1. F1 完成后的冻结内容（accepted，不再重开）

```text
A  Transactions ListView 键盘入口（activeFocusOnTab，无 select-on-focus）
B/C 页面级 focus gating（隐藏页既不可获得也不保留可消费焦点；H1S/H2/H3）
D  rail Enter/Space 激活（5/5 ×2）
E/F rail distinct accessible name + button role/Invoke
G  rail keyboard-focus ring（visualFocus）
H  Agent TextArea Tab/Backtab 遍历（非吞键）
correction-1 AppButton / ComboBox×2 / ListView / TabButton×3 的焦点可见性
correction-2 TabButton 独立内缩焦点环（selected 外观零改动）
automated: qml_focus_check（FA/FA2/FJ/FB×5/FC/FD·FE×5/FF/FK/FL/FG/FH/FI）
```

### FJ2. F2 边界（本轮不得做）

```text
不得：开始 F3 closure / 推进 verified LKGC / 创建 v2.0.0 tag / push / publish / 签名 / installer。
F2 目标：从 b237ddc 全新生成最终 Release portable candidate + 完整 automated/package gates
        + 最终跨页面视觉 evidence，然后停在 F2 Review / Manual Final Acceptance 之前。
```

## F2 — Final Release / Visual Evidence Candidate（2026-09-19，behavior-bearing evidence，不推进 LKGC）

> F1 Re-review = PASS / Focus Visual Review = PASS / F1 = COMPLETE（accepted F1 behavior tree = `b237ddc`）→ **F2 = GO**。
> 本轮从 `b237ddc` **全新生成**最终 Release portable candidate，执行完整 automated/package gates，生成最终跨页面视觉 evidence；
> **停在 F2 Review / Manual Final Acceptance 之前**：不开始 F3 closure、不推进 verified LKGC、不创建 v2.0.0 tag、不 push/publish/签名、不做 installer。

### FK0. Candidate source tree（冻结）

```text
source behavior tree = b237ddc（clean tree）
本轮所有 Release build / deploy / package / fresh extraction / final screenshots 均来自同一 b237ddc clean tree；
不是 4cb6e9d / 736d957 / 5089840。verified LKGC 仍为 4cb6e9d（推进留给 F3 closure）。
```

### FK1. Evidence namespace（防混淆）

```text
上一轮遗留的 build/f2_evidence、build/f3_evidence 与临时脚本全部删除（含误名/预置输出），
从空的 build/f2_evidence/ 重新开始；F1 证据不再与 F2 证据混放。
committed 的 docs/assets/screenshots 未被删除（43 张既有 PNG 保留）。
方案：F2 全部证据落在 build/f2_evidence/（ignored），最终 committing 15 张 m9f-f2-* 截图。
```

### FK2. Clean Release build

```text
rm -rf build/release → cmake --preset release-local → cmake --build --preset release-local
CMAKE_BUILD_TYPE       = Release
compiler               = MinGW g++ 13.1.0 (x86_64-posix-seh-rev1, MinGW-Builds)
Qt                     = 6.11.1 mingw_64（CMAKE_PREFIX_PATH）
generator              = Ninja 1.12.1 / CMake 3.30.5
targets                = 207 built, 0 error
exe                    = build/release/ModbusLens.exe（2,795,138 B；不复用旧可执行文件）
```

### FK3. Full automated regression（Release，本轮新构建）

```text
ctest --preset release-local      : 27/27 PASS（按真实数量报告）
--qml-smoke-test                  : PASS（SMOKE IDENTITY PASS：applicationName/displayName=ModbusLens、
                                    version=2.0.0、organizationDomain unset、title、windowIconSizes 6 尺寸）
--qml-nav-check                   : PASS（post-Legacy five workspaces；navigation changed no business values；
                                    A/B/D/E/F/G'/H/I/J/K/K'/L/N/O/P/Q/R/S/T asserted；**M DEFERRED**）
--qml-geometry-check              : PASS（**18 printed segments / 0 GEOFAIL / rail 宽度恒 56**）
--qml-focus-check                 : PASS（FA/FJ/FB×5/FC/FD·FE×5/FF/FK/FL/FG/FH/FI）
F1 contract regression（§6）：上述 focus check 即 F1 契约的机器证明（Transactions 入口+四键、hidden 获取/保留、
rail Enter·Space×5、Device 排除、TextArea Tab/Shift+Tab + FL 编辑键、各类型焦点指示 state 断言）。
```

### FK4. Identity / Version / PE / Icon / Architecture（新 Release exe）

```text
PE Machine = 0x8664 → AMD64 → package architecture label = x64（重新测量，不复用 E3 文档值）
RT_ICON = 6 个；RT_GROUP_ICON = 1 个
VersionInfo（pefile 与 PowerShell 双读一致）：
  FileVersion 2.0.0 / ProductVersion 2.0.0 / FileVersionRaw 2.0.0.0 / ProductVersionRaw 2.0.0.0
  ProductName = ModbusLens / FileDescription = ModbusLens / OriginalFilename = ModbusLens.exe
  CompanyName / LegalCopyright 空（有意 omission，M9-E 冻结）
runtime identity（smoke）：applicationName/displayName = ModbusLens、version = 2.0.0、windowIcon 6 尺寸
```

### FK5. Deploy（本轮独立目录）

```text
批次脚本 run（deploy_windows.bat）在本机空参数传递下丢失 QT_BIN（cmd 空参数丢弃）→ 改用 committed
workflow：python scripts/make_package.py build/release build/f2_deploy，由脚本自身从 CMakeCache
派生 QT_BIN/MINGW_BIN 并执行 deploy（M9-E 已验证的路径）。
deploy 结果：build/f2_deploy（本轮独立目录，未覆盖 debug/deploy 或既有 release deploy）
```

### FK6. Package（packaging semantics 零改动）

```text
committed scripts/make_package.py（自 4cb6e9d 后 zero diff）：
  authority version（CMake）→ stem = ModbusLens-2.0.0-windows-x64（x64 来自本轮 PE 实测）
  staging → structural checks（required present / forbidden absent / StatisticsOverview retained / samples policy）
  → negative scans（known-risk credential·config 文件名+文本；absolute-path 文本审计）
  → manifest → ZIP → ZIP entries == staging 集合 → fresh extraction 校验 → minimal-PATH 三模式 → external-CWD
全部 PASS；packaging version 继续从 authority 派生。
```

### FK7. Package 结构与计数（本轮真实计数，不复制 E4 数字）

```text
payload files   = **1496**（脚本输出 "staged 1496 entries + README.txt"，manifest 记录 1496 payload）
manifest        = 1 个 package-manifest.sha256（排除自身）
ZIP entries     = **1497**（脚本断言 == staging 文件集合）
README.txt         存在（authority 版本生成）
demo_v1.mlog       存在（随包 sample）
regression fixtures 不入包（t014/t015 由 structural check 断言缺席）
StatisticsOverview 继续 retained（structural check 断言）
icon source / make_icon.py / Python maintainer tooling 不进 runtime package（forbidden-absent 断言）
```

### FK8. Final ZIP identity（accepted F2 local candidate identity）

```text
filename = build/package/ModbusLens-2.0.0-windows-x64.zip
bytes    = **40,630,813**
SHA256   = **7292920bf50af2288e912b34227395e990483d62cb51c394252ee2046abdd826**
措辞：这是 **accepted F2 local candidate identity**，不是 published release hash，
      也不构成 byte-reproducibility guarantee（scripted portable ZIP 语义不变）。ZIP 不提交 Git。
（与 E4 的 40,569,927 B / 59d2d126… 不同是预期的：F1 改了产品行为，ZIP 内容随之变化。）
```

### FK9. Fresh extraction basis

```text
fresh extraction = build/package-extract/ModbusLens-2.0.0-windows-x64（由本轮 ZIP 解出，逐文件 SHA256 == manifest）
**所有 package runtime 验证与最终截图都从该 fresh extraction 运行**，不用 build/release 或 build/f2_deploy 顶替。
```

### FK10. Extracted-package gates（F1 behavior 真实进入 package）

```text
（minimal PATH = C:\Windows\System32;C:\Windows；环境其余继承，与 committed 脚本一致）
--qml-smoke-test   : PASS（identity 2.0.0 + icon 6 尺寸）
--qml-nav-check    : PASS（A–T；five workspaces；Device 不可达在其中）
--qml-focus-check  : **PASS**（FA/FJ/FC/FD×5/FE×5/FF/FK×8/FL×6/FG/FH/FI 全部 PASS，无 FOCUSFAIL）
⇒ F1 的键盘焦点契约（Transactions Tab 入口 + 四键、hidden 获取/保留、rail Enter·Space、Device 排除、
  Agent TextArea Tab·Shift+Tab、编辑键、焦点可见性 state）在 **packaged runtime** 中真实成立。
说明：package 只带 windows 平台插件；早期用 QT_QPA_PLATFORM=offscreen 驱动 packaged exe 会
STATUS_DLL_INIT_FAILED（0xC0000142）—— committed 脚本从不设置该变量，本轮探针已改为不设置（工具缺陷，非产品缺陷）。
```

### FK11. Final Visual Matrix（125% DPI，物理 1280×900 = 逻辑 1024×720；最小尺寸物理 1250×875 = 逻辑 1000×700）

| # | 文件 | 状态 oracle（机器可证） |
| --- | --- | --- |
| 1 | m9f-f2-dashboard-demo-1024x720 | ws=Dashboard；demo 会话已发布（Run Demo 存在 + 统计/关注项渲染） |
| 2 | m9f-f2-transactions-populated-1024x720 | ws=Transactions；rows populated、transactionsDiagnosisCue 暴露 |
| 3 | m9f-f2-transactions-list-keyboard-focus-1024x720 | 键盘-only Tab 第 8 次进入 list + END 选中行（detail pane 填充） |
| 4 | m9f-f2-communication-default-1024x720 | ws=Communication；disconnected/default（未声称真实串口硬件） |
| 5 | m9f-f2-communication-combobox-focus-1024x720 | 键盘焦点落在 commPortCombo（UIA focused id 验证） |
| 6 | m9f-f2-replay-default-1024x720 | ws=Replay；未加载任何回放源 |
| 7 | m9f-f2-replay-load-dialog-1024x720 | app 自身"加载回放"对话框打开、sample 列出、会话未改变（source 未变） |
| 8 | m9f-f2-diagnosis-baseline-1024x720 | ws=Diagnosis；Baseline pane 暴露 |
| 9 | m9f-f2-diagnosis-ai-no-result-1024x720 | AI pane；**provider 已配置（ModelScope/Qwen3.5-27B）但未生成结果**（"尚未生成 AI 解释"），无伪造结果 |
| 10 | m9f-f2-diagnosis-agent-no-result-1024x720 | Agent pane；问题为空、无结果、未发起 live 调用（无伪造） |
| 11 | m9f-f2-diagnosis-tabbutton-selected-vs-focused-1024x720 | selected tab = Baseline（pane 仍 Baseline）+ 键盘焦点在第 2 个 tab（rect 同一性验证） |
| 12 | m9f-f2-rail-selected-vs-focused-1024x720 | selected = Transactions + 键盘焦点在“总览”条目（rect 同一性验证） |
| 13 | m9f-f2-transactions-1000x700 | 最小尺寸；ws=Transactions；cue 暴露 |
| 14 | m9f-f2-dashboard-1000x700 | 最小尺寸；ws=Dashboard；Run Demo 存在 |
| 15 | m9f-f2-diagnosis-1000x700 | 最小尺寸；ws=Diagnosis；Baseline pane 暴露 |

```text
截图 oracle（§20）：每张截图都由运行时状态判定后才拍摄（workspace 归属 + 页面专属可见元素 +
selected/focused 运行时状态 + rect 同一性），文件名不作为状态依据。
机器完整性检查（§21）：15/15 尺寸正确、landscape=True、非空白（颜色数 ≥ 3299），并记录 sha256[:16]（见下）。
ZCode **不**自判 final visual PASS；Focus Visual Review 已在 F1 阶段由用户 PASS，
本轮的 Final Visual Review 仍需用户确认（截图来自 packaged candidate）。
Communication 边界：只接受 disconnected/default 与确定性校验，**不声称真实串口硬件 PASS**。
AI/Agent 边界：不要求真实 provider/Agent 服务；未配置 secret、未发起 live 调用，截图状态如实标注。
```

### FK12. Evidence integrity（机器检查）

```text
committed 15 张（docs/assets/screenshots/m9f-f2-*）：
1280x900 ×12（1024×720 逻辑）+ 1250x875 ×3（1000×700 逻辑）；全部 landscape、非空白；
sha256[:16] 记录于本轮证据（build/f2_evidence/visual_integrity.txt）。
```

### FK13. Manual Acceptance Candidate（用户下一步人工清单）

```text
1  fresh extraction（使用本轮 ZIP；不要用 build/release 或 deploy tree）
2  正常双击启动（Explorer 双击 ModbusLens.exe）
3  Explorer 图标 / 文件版本·产品版本
4  五个 workspace 视觉（事务/总览/通信/回放/诊断）
5  rail selected 与 keyboard focus 可区分；Device 禁用
6  Tab / Shift+Tab 遍历（含 rail 与页面内控件）
7  焦点可见性（rail / Transactions list / ComboBox / AppButton / Diagnosis TabButton）
8  Transactions 键盘操作（Tab 进入 list、Up/Down/Home/End）
9  Diagnosis 三个 tab（Baseline/AI/Agent；Tab 只移动焦点不切页）
10 Agent TextArea：Tab 逃逸；方向键/Home/End 编辑语义
11 Replay 加载随包 demo_v1.mlog（本步骤需人工在真实 UI 中点击）
12 README.txt 内容与包一致
13 窗口尺寸 1024×720 与 1000×700（无裁切/溢出）
14 关闭并重新启动
不要求：真实串口硬件、真实 AI Provider、Agent live 服务。
```

### FK14. Git / 边界

```text
本轮 F2 只产生 docs + screenshots/evidence（+ ignored build artifacts）；**production diff = 0**。
未 amend b237ddc；未 rebase；未 push；未创建 v2.0.0 tag；**verified LKGC 保持 4cb6e9d**（推进留给 F3 closure）；
未开始 F3；未重新设计 packaging；未改 version/PE/icon/package stem/README/manifest 格式/ZIP 模型。
F2 Review Gate：clean Release build PASS、27/27 PASS、smoke/nav/geometry/focus PASS、identity/PE/icon PASS、
deploy PASS、package PASS、fresh extract PASS、manifest PASS、security scan PASS、package focus regression PASS、
visual evidence candidate generated —— 全部达成。
Final Visual Review = WAITING FOR USER；Manual Final Acceptance = WAITING FOR USER。
```

## F2 Final Visual Review = HOLD（ListView focus 视觉权重过强）→ Minimal Visual Correction（2026-09-19，append-only，behavior-bearing）

> 人工发现：**Transactions ListView 的 keyboard focus indicator 视觉权重过高**——整块 ListView 使用 2px 高饱和蓝色粗边框。
> 明确分类：**不是 focus visibility 缺失，而是 focus visibility 过强 / visual hierarchy defect ⇒ P1 visual/usability defect**。
> 本轮**只修 Transactions ListView 的 focus rendering**；不动 selection semantics / row selection style / detail logic /
> keyboard entry / Up·Down·Home·End / geometry / NavigationRail / TabButton / AppButton / ComboBox。

### FL0. HOLD 归档（三条具体症状，用户人工 review）

```text
1  focus container ring 比 row selection 更抢眼；
2  与 selected row 左侧蓝色小条使用相近强调色与强度 ⇒ 两种状态互相竞争；
3  大面积空白区域也被整框包围 ⇒ 视觉噪声过高。
目标（人工验收口径）：一眼知道「列表现在有键盘焦点」，但第一视觉仍然落在「当前选中的是哪一行」。
```

### FL1. Root Cause

```text
F1 修正引入的 transactionsListFocusRing 使用 border.width=2 + 不透明 DS.primary 画在**容器**尺度上：
  · 与选中行的 2px DS.primary 左侧条**同色同重量**，且行条就在容器左内缘附近 ⇒ 两个状态读起来是同一种强调；
  · 容器边框包围的是**整个视口**（含大块空白），权重 × 面积 ⇒ 噪声远高于行级选中提示。
即：焦点通道"存在且可见"这一条成立，但**没有服从视觉层级**（selection 才是主状态）。
```

### FL2. Minimal Correction（只改这一处渲染）

```text
src/ui/qml/pages/TransactionsPage.qml → transactionsListFocusRing
  border.width : 2 → **1**
  border.color : DS.primary（不透明）→ **Qt.rgba(DS.primary.r, DS.primary.g, DS.primary.b, 0.5)**（半透明，由 token 派生，未改 DS contract）
  anchors.margins : 1 → **3**（内缩更远，**永不覆盖**行左侧 2px selection indicator）
未改：selection semantics、row selection style、detail logic、keyboard entry、Up/Down/Home/End、
      ListView 尺寸、row geometry/height、NavigationRail、TabButton、AppButton、ComboBox、DS token contract。
```

### FL3. 契约冻结（本轮新增，写进机器断言）

```text
selected row 仍是主要视觉状态；
ListView keyboard focus 必须清楚但克制（subordinate）；
focus indicator 不得覆盖左侧 selection indicator；
不改变 ListView 尺寸与 row geometry。
⇒ 该"权重"契约进入 FJ 机器断言（见 FL4），未来若有人把环改回 2px 高饱和，gate 直接失败。
```

### FL4. 机器断言（qml_focus_check / FJ 扩展）

```text
FJ 现在断言：ring.visible ⇔ list 持焦；**border.width == 1**；**border.color.alphaF() <= 0.6**。
实测输出：
  FOCUS [FJ] PASS: list ring visible, subordinate weight (1px, alpha=0.500008)
  FOCUS [FJ] PASS: ring off after focus leaves the list (focus=appBarClearResults)
```

### FL5. 回归（全部重跑）

```text
Debug   : --qml-focus-check PASS / --qml-geometry-check PASS（**18 segments · 0 GEOFAIL · rail 恒 56**）/
          --qml-nav-check PASS（A–T，M DEFERRED）/ ctest **27/27**
Release : build OK → ctest **27/27** → --qml-focus-check PASS（FJ subordinate weight 断言通过）/ geometry PASS
```

### FL6. 证据刷新

```text
重新生成 Transactions keyboard-focus 截图（state oracle：keyboard-only Tab 第 8 次进入 list + END 选中行，
detail pane 填充），覆盖 committed：
  docs/assets/screenshots/m9f-f2-transactions-list-keyboard-focus-1024x720.png（1280×900 = 逻辑 1024×720，125% DPI）
肉眼核对结果（ZCode 仅做完整性与状态核对，最终判断仍归用户）：
  选中行（含 2px 高饱和左侧条 + 底纹）为**第一视觉**；ListView 焦点为**细、淡、内缩**的一圈线，可见但不抢眼；
  环位于 x=3 内侧，**未覆盖**行左侧 indicator。
provenance 说明（重要）：本截图来自**修正后的 Release build**（build/release）；
  46f68ce 轮的 F2 ZIP（ModbusLens-2.0.0-windows-x64.zip，40,630,813 B / 7292920b…）**早于本次修正**，
  因此**不能**再作为最终 acceptance candidate —— visual review 通过后必须重新生成 package（下一轮 F2 收尾步骤）。
```

### FL7. Git / 边界

```text
behavior-bearing visual correction（QML focus rendering + harness 断言）。
未推进 verified LKGC（保持 4cb6e9d）；未 push；未创建 v2.0.0 tag；未开始 F3；
未改 packaging semantics；未改 version/PE/icon/README/manifest/ZIP 模型；
未触碰 NavigationRail / TabButton / AppButton / ComboBox / selection 语义 / geometry。
Final Visual Review 仍需用户基于刷新后的截图确认；Manual Final Acceptance = WAITING FOR USER。
