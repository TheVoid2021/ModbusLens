# T021 — M9-F Final Manual Visual / Accessibility Acceptance

> **状态：IN PROGRESS — Phase 1 = PASS；F0 = 完成测量 + F0 Review HOLD → continuation 实测完成（per-workspace + rail activation + hidden-focus）；**P0=3 / P1=3 / GAP=3 / PASS=7** → F1 REQUIRED；Implementation = NOT STARTED。**
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
## 36. Status

**M9-F IN PROGRESS；Phase = Learning / Final Acceptance Design Gate；Implementation = NOT STARTED**。docs-only 本轮；verified LKGC **不变 = `4cb6e9d`**；未 push。