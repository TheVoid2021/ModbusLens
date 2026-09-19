# T021 — M9-F Final Manual Visual / Accessibility Acceptance

> **状态：IN PROGRESS — Phase 1 Learning / Final Acceptance Design Gate（2026-09-19）。Implementation = NOT STARTED。**
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

## 36. Status

**M9-F IN PROGRESS；Phase = Learning / Final Acceptance Design Gate；Implementation = NOT STARTED**。docs-only 本轮；verified LKGC **不变 = `4cb6e9d`**；未 push。