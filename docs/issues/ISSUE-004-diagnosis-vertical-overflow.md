# ISSUE-004: Diagnosis 面板纵向 overflow 使 AI 控件与事务列表不可达 —— Manual AI UI Smoke blocker

> 状态：**RESOLVED ✅**（2026-09-08，用户先证 blocker 后证修复）

## Symptom（现象）

用户在 T011 Part B Manual AI UI Smoke 中实测：点击 **Run Baseline Diagnosis** 后，Diagnosis 内容（baseline 文本，以及有 AI 解释时更长的 AI 文本）**纵向展开**，把下方的 **AI Explanation / Ask AI / Cancel** 按钮和 **Recent Transactions** 推到窗口可视区域以下；页面无纵向滚动能力，这些控件与内容**不可达**（截图实证）。

## Impact（影响）

直接阻塞 T011 Part B 的人工验收链：
- 用户无法执行 **Ask AI** → Manual AI UI Smoke BLOCKED；
- Live ModelScope Smoke 无法开始；
- T011 Part B / T011 / M6 均不得标完成。

**定性：功能性 UI bug，不是 cosmetic polish。**（严禁按 polish 降级处理。）

## Repro（复现步骤）

1. 启动 `build/deploy/ModbusLens.exe`（约 1024x720）；
2. Run Demo Batch → Run Baseline Diagnosis；
3. 观察：Diagnosis GroupBox 占用超过其内容可示高度，Recent Transactions 标题与列表消失/不可及；AI Explanation/Ask AI/Cancel 与列表区在窗口下方无法滚到。

## 定位过程（Root Cause）

读取 `src/ui/qml/Main.qml` 的布局结构后确认：
- 根 `ColumnLayout`（anchors.fill）无任何滚动容器；
- `Diagnosis GroupBox` 没有 `Layout.minimumHeight/maximumHeight/preferredHeight` 约束，`implicitHeight` 随 baseline/AI 文本无界增长；
- 唯一 fillHeight 的 Recent Transactions ListView 容器被压缩至 0，页面整体无法滚动。
- 结论：Diagnosis 内容的 implicitHeight 无界 + 外层无纵向 overflow 处理 —— Diagnosis 内容推挤下方内容出窗口。

## 解决方案（最小可靠修复，QML-only）

- **不把整个 ApplicationWindow 包进大 ScrollView**（避免与 Recent Transactions 自身的 ListView/Flickable 形成嵌套滚动）。
- Diagnosis GroupBox 采用**受限高度策略**：`minimumHeight: 170`、`maximumHeight: 320`、`preferredHeight` 状态驱动（无诊断 170 / 有诊断或 AI busy 280）。
- Diagnosis 内部：**固定区**（Run/Clear 行、AI Explanation 标题、Provider/Model 行、Ask/Cancel/Requesting 行）不参与滚动；**可滚动区**为 `ScrollView`（vertical only，`ScrollBar.horizontal.policy: AlwaysOff`），内容 Column `width: diagnosisScroll.availableWidth`，全部文本 `wrapMode: Text.Wrap`。
- AI output 继续 `textFormat: Text.PlainText`（PlainText 回归守卫：`<b>`/`<script>` 必须字面显示）。
- Recent Transactions 列表容器增加 `Layout.minimumHeight: 120`，保证 heading + 至少一部分列表在标准窗口尺寸下可见；ListView 保持既有自身滚动能力，不改为无限 contentHeight。

## Verification（验证）

```text
qml smoke（真实 exe，含新布局）：--qml-smoke-test exit=0
clean build：126 targets，warning/error 0
full ctest：20/20 PASS（AI-B01~B13 与 UI-AI01~AI11 全部不回归——纯 QML 改动）
deploy_windows.bat：exit=0；Qt6Network/Qt6SerialPort provenance 一致；TLS runtime 不回退
minimal-PATH：--qml-smoke-test exit=0
用户人工复验：Diagnosis 内部可滚动、Ask AI/Cancel 可达、Recent Transactions 保留、
             窗口缩小（约 1000x700）无控件永久丢失、无横向 overflow → PASS（用户确认）
```

## Learning（教训）

- **每次给"现有页面"追加"会增长的纵向内容"后，必须检查该页面的纵向 overflow 预案**：本 bug 的本质是 Part A 的 Diagnosis 区已是"文本会增长"的组件，Part B 在其下方又加了同样会增长的 AI 区，却没有给限量——增长内容 + 无界 implicitHeight + 外层无 scroll 的组合必然挤压 fillHeight 兄弟。
- QML 布局验证缺陷如实记录：qml smoke 只证明"可实例化"，无法证明"内容可到达"——这属于人工视觉验收的职责范围（本项目 QML 测试基础设施不建像素测试，符合 04_TEST_STRATEGY）。

## Related

- 发现于：T011 Part B Manual AI UI Smoke（WAITING FOR USER 期，用户反馈）
- 阻塞：T011 Part B / T011 DONE 判定、Live ModelScope Smoke
- 修复提交：amend 于 T011 Part B code/config commit（Git Policy 允许：当前任务最新未 push 提交），amend 后真实 hash 见 git log
## Reopened — Manual Smoke Failure #2（2026-09-08）

状态更新：**REOPENED**（自动化仍 GREEN，但用户人工复验 = FAIL/BLOCKED）。以上一轮 RESOLVED 及验证记录为历史事实保留，此处追加第二轮调查。

### 新截图实证观察

A. **Diagnosis GroupBox 下边框已结束，baseline/Suggested checks 文本仍继续绘制到边框外** —— Diagnosis scroll viewport 没有真正约束 child painting。
B. **Recent Transactions ListView 滚动时，四条 delegate 绘制到 viewport 之外并覆盖 "Recent Transactions" heading** —— ListView 缺少有效 viewport clipping。

### 第二层根因（Layout bounds ≠ Rendering clip）

上一轮 minimumHeight/maximumHeight/preferredHeight 只解决了 **layout allocation**；本轮截图证明还有第二层：**painting containment**。

- **Diagnosis 内容画出边框**：QuickControls2 `ScrollView` 未显式给出 `contentWidth/contentHeight` —— 其底层 Flickable 的 contentItem 尺寸未定义：内容按自身 implicit 尺寸铺开（不被认为"超出 viewport 故可滚"），且视图层 clip 的是 ScrollView 自身边界而非内容挤压层。ScrollView 存在 ≠ 滚动正确工作。
- **delegate 覆盖 heading**：`ListView` 默认 `clip: false` —— delegate 在滚动中可绘制到 viewport 外，与 Diagnosis 文本问题是同源异构（都缺渲染裁剪边界）。

### 修复（Fix Candidate）

- Diagnosis ScrollView：显式 `contentWidth: availableWidth`、`contentHeight: diagnosisContent.implicitHeight`（Column 加 id）；`clip: true`；横向 `AlwaysOff`、纵向 `AsNeeded`；文本区 Column 内每个 Label `width: parent.width` + `wrapMode: Text.Wrap`；AI 输出保持 `Text.PlainText`。
- Recent Transactions：ListView `clip: true` + `boundsBehavior: Flickable.StopAtBounds`；heading 保持 sibling 结构；**不改 delegate 内容/高度/模型**；**不用 z-order 掩盖**。
- Diagnosis 高度策略保留（170/280/320 状态驱动）但已复核：固定 controls + 明确的 viewport 剩余高度（fillHeight）分配。

### 修复后验证状态

自动化：clean 126 targets 零警告；ctest 20/20；qml smoke exit=0；deploy exit=0；Qt6Network provenance 一致；minimal-PATH smoke exit=0。

**状态：Fix Candidate — Awaiting Manual Verification。** 用户未再次人工确认"Diagnosis 内部滚动 PASS + Transaction clip PASS"之前，不得写 RESOLVED。

## Reopened — Manual Smoke Failure #3（2026-09-08）

状态：**继续 REOPENED**（第三次人工复验：Recent Transactions 基本解决；Diagnosis viewport 仍 FAIL——白色下边框外继续绘制 baseline/Suggested checks、无工作纵向滚动、无 vertical scrollbar）。

### 第三次静态取证（先取证后改，不盲调参数）

- **重复长文本对象检查**：`baselineDiagnosisText`（行 452）、`aiDiagnosisErrorMessage`（行 459）、`aiDiagnosisText`（行 466）各只出现一次——无重复 presentation 对象；三份动态长文本的确都在 content Column 内（非 sibling）。
- **对象树确认**：固定 controls（Run/Clear、AI Explanation、Provider、Ask/Cancel/Requesting、Baseline 标题）在 viewport 外；增长文本在 viewport 内。
- **根因聚焦为 §3 B/C**：QC2 `ScrollView` 在 GroupBox + ColumnLayout 组合下自动 content sizing 不可预测——contentWidth/contentHeight 显式设定后仍无法保证 viewport=剩余高度（视口被 layout 按内容 implicit 扩张、或 clip 边界错位）。ScrollView 属于 Controls 便捷封装，本页面的组合 sizing 需要直接可证明的 viewport 语义。
- 运行时 geometry 自动取证受限说明：Windows GUI 子系统（WIN32）无标准输出通道，QML console.log 无法经重定向捕获；offscreen 平台可实例化但非可视渲染。已尝试临时 Timer 探针（未提交）后放弃通道，以静态对象树取证 + 用户三次真实截图作为依据。**最终滚动行为由用户肉眼验收**（与本项目 QML 无像素测试的测试策略一致）。

### 第三轮修复（Flickable 显式 viewport，非 ScrollView 参数微调）

- Diagnosis 长文本区由 ScrollView 改为**显式 Flickable**（`id: diagnosisFlick`）：`Layout.fillWidth/fillHeight + Layout.minimumHeight: 0`（允许收缩到剩余空间——content 的 implicitHeight 不再成为 viewport 最小值，这是可收缩性的关键）、`clip: true`、`contentWidth: width`、`contentHeight: diagnosisContent.implicitHeight`、`boundsBehavior: Flickable.StopAtBounds`、`flickableDirection: Flickable.VerticalFlick`、`ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }`；内部简单 Column 内容结构不变（Wrap、width=parent.width、AI=PlainText）。
- `GroupBox { id: diagnosisGroup; clip: true }` 固定 controls/headings 之外加最终 containment safety net——最坏结果是文本被裁，而非污染 Recent Transactions。
- Recent Transactions 只回归验证（ListView clip + StopAtBounds 维持，不动 delegate/model/数据）。
- 临时 geometry probe 未提交（仅调查用并于本轮移除）。

### 状态

**Fix Candidate — Awaiting Manual Verification（第三次）。** 用户肉眼确认"Diagnosis 内部滚动 + 纵向 scrollbar 真实出现 + 任何文字不出边框 + Transactions 不覆盖 heading"之前，不得 RESOLVED。

## r4 取证结论 + Workspace Layout Fix（2026-09-08）

### 有效取证结论（append-only，保留此前全部失败记录）

用户提供的 runtime geometry evidence（debug overlay 读数）：

```text
groupH=280 colH=245 flickH=133 flickCH=214 flickCY=81 contentH=214 contentIH=214 bLen=267 bVis=true
```

证明：**Diagnosis 滚动机制本身已正确**——viewport 非零（133）、contentHeight(214) > viewport height、contentY 可变化（81）、baseline 正文状态与数据正常。"动态正文不可见"的根因（GroupBox contentItem 下 ColumnLayout 未填充 → Layout.* 失效 → viewport≈0）已由 `diagnosisColumn.anchors.fill: parent` 验证修复。**Diagnosis viewport 不再是主 blocker。**

### 新 blocker：纵向 workspace 分配

前几轮为 Diagnosis 设置的固定/偏固定高度（~280px）在 Root ColumnLayout 纵向串联中持续侵占 Recent Transactions 的垂直空间——即使最大化、golden 仅 4 条 transaction，列表仍在很矮的窗口中滚动。这是 dashboard **workspace 结构**问题，不是继续调 Diagnosis scroll 参数能解决的。

### 修复：Horizontal SplitView（用户定案结构落地）

- Root ColumnLayout：Header/控制条/Serial Controls/Statistics 原样保留；之后 `SplitView { orientation: Qt.Horizontal; Layout.fillWidth/fillHeight }` 承接**全部剩余高度**。
- 左 pane = Diagnosis（`SplitView.fillHeight: true; minimumWidth 300; preferredWidth 400`）：**删除全部纵向 min/preferred/max 高度控制与状态驱动高度**；内部保留已验证的 anchors.fill ColumnLayout + Flickable（childrenRect 高度、AsNeeded scrollbar、PlainText、clip）。
- 右 pane = Recent Transactions（`SplitView.fillWidth: true; fillHeight; minimumWidth 520`）：heading 为 ListView sibling；ListView `clip: true + StopAtBounds`；delegate/model/roles/行高不变。
- 根布局不滚；Diagnosis/Transactions 各滚各的；SplitView 默认 handle 可拖动调整比例。
- Diagnosis 空态（No diagnosis run yet）令 pane 稳定存在、不再因无内容或 groupBox 高度逻辑跳动。
- ApplicationWindow `minimumWidth: 1000; minimumHeight: 700`（符合实际工具有效布局，非拍脑袋——1000=左 pane 300+handle+右 pane 520 保底）。
- 移除全部 DBG overlay/Label/Timer/probe（grep DBG/geoProbe/TEMP 计数 0）。
- **未 commit**（按本轮 Git 指示保持 working tree，等 Layout PASS 后再定最终 fix commit）。

### 状态

**REOPENED → Fix Candidate / Awaiting Manual Verification（workspace 版面）**。用户确认：A 左右布局合理、B golden 4 条 transaction 最大窗口基本一次可见、C Diagnosis 长文本内部滚动、D Transactions 内部滚动、E 无 overflow，五项全过方可 RESOLVED。

## RESOLVED — Manual Layout Verification PASS（2026-09-08）

用户人工确认（9 项证据）：Diagnosis 与 Recent Transactions 左右布局、Diagnosis baseline 正常、独立 vertical scrollbar、内容不越 pane、Transactions 不再被挤到窗口底部、golden 4 transactions 一次可见、delegate 不覆盖 heading、两 viewport 互不干扰、SplitView 布局可接受。

**最终架构**：Horizontal SplitView workspace——左 Diagnosis（全高 pane、内部 Flickable 独立滚动、clip 安全网）、右 Recent Transactions（fillWidth + ListView clip/StopAtBounds 独立滚动）；root 不滚。

**完整根因轨迹（append-only 下的演进结论）**：
1. 局部 overflow（Diagnosis implicitHeight 无界挤压下方内容 + 无全局滚动）；
2. 动态正文 viewport 失效（GroupBox contentItem 非 Layout 容器 → 子项 Layout.* 被忽略 → Flickable viewport≈0 → "内容不可见"；`anchors.fill` 修复，runtime geometry 实证：flickH=133、contentH=214、contentY 可变化）；
3. 最终收敛为 workspace vertical allocation defect（固定高度 Diagnosis 在纵向串联中侵占 Transactions）→ SplitView 定案。

**状态：RESOLVED ✅。** 视觉样式/颜色/spacing 归 Final Integration / UI polish，不在本 issue 范围。本档案保留前三轮全部失败过程记录（工程过程证据）。

- Fix commit：`85699ff`（fix(T011): use split workspace for diagnosis and transactions，最新 Part B LKGC candidate）
