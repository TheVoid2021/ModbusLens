# T018 — M9-C Dashboard Redesign

> **本文件是 M9-C 的 canonical task document（2026-09-17 起）。**
> Phase 1 Learning / Design 的历史快照保留在 [T017 §52/§53](T017-m9b-application-shell-navigation.md)（M9-B 任务档），**本文件不复制其内容，只引用**；自 C1 起，M9-C 的一切实施/验证/归档记录写入本文件，**不再向 T017 append M9-C 活动**。
> 任务归属来源：`docs/BACKLOG.md` 的 `T018 | M9-C Dashboard Redesign` 行；用户在 M9-C Phase 1 Review 中批准 T018 canonicalization。

- **Goal** … 在不新增任何业务数据与语义权威的前提下，把 Dashboard 从"页面边界正确但信息层级与余量归属未设计"改造为 **current-session situational awareness** 页面；分阶段（C1–C5）交付，每阶段可运行、可测试、可回滚。
- **Background / User Value** … B2 只确定了 Dashboard 的**边界**（Overview + Run Demo + Statistics）并显式把"视觉丰富化"延到 M9-C；B3 记录同族 **surplus-space** 现象并留给 M9-C；B5 完成后五个 workspace 全部可达、Legacy 只剩 Statistics + Transactions。用户价值：工程师打开 Dashboard 能一眼判断"有没有数据 / 是否异常 / 分布如何 / 下一步去哪"，而不是面对被布局引擎随机摊开的统计块。
- **Phase History（引用，不复制）** … [T017 §52](T017-m9b-application-shell-navigation.md) = M9-C Phase 1 完整设计（现状 UI Map / StatisticsOverview inventory / 实测几何取证 / 用户任务排序 / 数据能力审计 / 语义冻结 / IA 三案 / deferred 裁定 / Legacy 边界 / Overview 拆分策略 / surplus-space 策略 / 层级 / 颜色 / chart / 异常 / 诊断 / CTA / Run Demo / 空态 / 两尺寸预算 / 无障碍 / DS 审计 / 验证与截图计划 / 回归面 / C1–C5 序列 / 人工清单 / 7 知识问答）；[T017 §53](T017-m9b-application-shell-navigation.md) = 当时的 Next。
- **Technical Decisions** … （C1）Dashboard 纵向布局拆为 **natural content region + explicit tail surplus owner**；不新增内容；不修改共享组件。详见 §C1 Technical Design。
- **Implementation** … （C1）见下文 Implementation Record。
- **Files Changed** … 见下文。
- **Problems Encountered / Solutions / RCA** … 见下文。
- **Verification** … 见下文（真实命令与输出）。
- **Result** … 见下文。
- **Knowledge Learned** … 见下文（Knowledge Ownership 纪律）。
- **Potential Interview Questions** … 见下文。
- **Git Commit** … 见下文。

---

## 0. V2 Task Execution Protocol 对应

| 步骤 | 本任务落点 |
| --- | --- |
| Preflight | 每阶段 Cx 开工前实测 HEAD/LKGC/diff-check（见各阶段记录） |
| V1 Contract Review | §1（受影响冻结契约清单） |
| Learning / Design | T017 §52（Phase 1 已完成并通过 Review） |
| Test Plan | §3（每阶段的断言与回归面） |
| Implementation | 各 Cx 的 Implementation Record |
| Targeted Verification / Full Regression | §3 门禁 + 各 Cx 的 Verification |
| Manual Review | 每阶段结束提交人工清单（C5 为最终人工包） |
| Documentation / Knowledge Ownership | 本文件 + PROJECT_STATUS/BACKLOG/devlog/INTERVIEW_NOTES |
| Git Commit / LKGC decision | 每阶段独立提交；LKGC 仅在**人工验收通过**后按 Git tree classification 裁定 |

## 1. V1 Contracts at Risk（M9-C 全阶段）

| 契约 | 风险来源 | 防护 |
| --- | --- | --- |
| statistics 公式（`observed=pending+completed`、`completed=Σ6状态`、`rate=success/(completed−expectedNoResponse)`） | 任何"重算/缓存/派生"冲动 | 只读 Controller 属性；禁止副本与重算（T017 §52.8） |
| `successRate` 是**分数**、`averageSuccessLatencyMs` 单位 ms | 显示层格式化 | 沿用既有 `hasX ? value : "—"` 与 ×100 规则，不改口径 |
| `TransactionStatus` 七值 / `ExpectedNoResponse` 正交 / issue 语义 | 视觉分组或"anomaly"措辞 | §2-A/§2-B guardrails（见下） |
| Replay analyzed subset / source transition | 任何自动命令 | 页面零副作用；进入/离开 Dashboard 不触发任何命令（§5） |
| Diagnosis / AI / Agent authority | 想在 Dashboard 展示其内容 | 本轮只允许**状态线索**，禁止复制内容（T017 §52.19） |
| Legacy 视觉（两次人工验收） | 改共享 `StatisticsOverview` | C1 不改共享组件；C2 拆分时以"Legacy 组合不变"为硬约束 |
| navigation index 契约 / 五 workspace 导航 | 布局改动引入新入口 | 导航唯一权威仍是 `NavigationRail`；不新增 CTA（T017 §52.20） |
| ISSUE-012 尺寸链教训 | 退回 anchored-only 写法 | 新组件 implicit 必须由内容 implicit 派生；不得依赖 anchors 回馈 |

## 2. Review Guardrails（M9-C Phase 1 Review Addendum，用户批准）

- **A. OutcomeDistribution 将来若实现**：分母 **= `completedCount`**；segments **必须**为 Success / Exception / CrcError / Timeout / ProtocolError / ExpectedNoResponse；**Pending 不进入**。**不得与 `successRate` 的分母混淆**——`successRate` 继续使用现有冻结语义（分母 `completed − expectedNoResponse`）。
- **B. `ExpectedNoResponse` 不是 anomaly**。未来若实现 attention aggregation：`attentionCount = exceptionCount + crcErrorCount + timeoutCount + protocolErrorCount`；**不得包含** Success、Pending、ExpectedNoResponse；**不得**由此生成 health score、Healthy/Unhealthy 判定或任何 severity authority。
- **C. SessionChip / AppBar refinement 移出 M9-C scope**（M9-C **不**改 AppBar shell presentation）。T017 §52.10 中"SessionChip NOW"的裁定被本 guardrail **取代**。
- **D. B5 冻结字面量 → DS token 清理不属于 M9-C 默认范围**；只有真实 Dashboard 实现产生 **≥2 个明确复用点**时才重新评估。
- **E. Run Demo**：始终可达且**只能**显式触发；但 **always visible ≠ must always dominate visual hierarchy**。
- **F. T017 §52 的 `≤380` 高度是 responsive design budget，不是强制 magic-number contract**；**Legacy preservation 也不是 PNG bit-for-bit contract**（用项几何与既有断言证明，不用像素逐位比对）。

## 3. Test / Acceptance Plan（全阶段共同门禁）

- 全阶段必跑：`build` → `--qml-smoke-test` → `--qml-nav-check` → `--qml-geometry-check`（**保持完整 10-pass matrix：5 workspace × 2 sizes**）→ full `ctest` → `git diff --check`。
- **nav check 场景语义不变**：basic five-workspace path + A/B/D/E/F/G'/H/I/J/K/K'/L/N 全部实际执行并 PASS；**M 继续 DEFERRED BY DESIGN**。
- 几何断言**只允许加强**：新增 Dashboard 专属断言（见各 Cx），不得删除/弱化既有断言。
- **oracle 纪律**（B4/B5 教训 + T017 §52.26）：不用脆弱固定像素带作为业务正确性；用 QML item geometry 与 DS token 的**相对关系**；data-derived 断言优先；每趟保留 `activePage` 非空转守卫。
- 视觉阶段（C3+）另需 deployed screenshot evidence + 人工视觉验收。

---

## C1 — Dashboard Layout Shell + Surplus-space Ownership

### C1.1 Preflight（2026-09-17）

```text
branch = main；HEAD = e45b1b9；working tree clean；git diff --check PASS
V2 verified LKGC = 6cc84c3；v1.0.0^{commit} = ae067ab（annotated tag 对象 2cee626）
origin/main = a40d935；ahead 44 / behind 0
```

### C1.2 Scope（用户批准）

**只允许**：`src/ui/qml/pages/DashboardPage.qml` + 验证 C1 所必须的**最小** `src/main.cpp` geometry/nav harness 扩展。

**本轮明确不改**：`StatisticsOverview.qml`、`DesignSystem.qml`、`Main.qml`、`NavigationRail.qml`、AppBar/SessionChip、`CommunicationPage.qml`、`ReplayPage.qml`、`DiagnosisPage.qml`、`AnalysisController`/Core/backend。

**Product goal**：解决 surplus vertical space ownership 与 section spacing；**不新增任何业务内容**。C1 后 Dashboard 仍只含：SectionHeader、Run Demo、empty hint（按当前状态出现）、StatisticsOverview——**不新增** chart / distribution bar / attention summary / diagnosis cue / recent rows / CTA / session block。

### C1.3 Technical Design

**布局契约（natural content region + explicit tail surplus owner）**：

```text
DashboardPage (Item)
└─ ColumnLayout(anchors.fill, margins = DS.spacingL, spacing = DS.spacingM)
   ├─ SectionHeader      dashboardHeader      natural
   ├─ RowLayout          (Run Demo action)    natural
   ├─ Label              dashboardEmptyHint   natural（visible: observedCount === 0）
   ├─ StatisticsOverview (instanceId dashboard) natural（本轮不改其实现）
   └─ Item               dashboardTailSpacer  Layout.fillHeight: true   ← 余量唯一所有者
```

**原理**：`ColumnLayout` 在**没有任何 `fillHeight` 子项**时会把多余纵向空间分摊到子项之间（B3 实测；Dashboard 本轮实测 action→statistics 空档 184.8px）；一旦存在一个 `fillHeight` 的尾部项，余量**全部**归它，其余子项保持 implicit 高度按 `spacing` 紧凑排列（B3 Communication 已验证同族机制）。**C1 只引入这一个结构性元素 + 断言**。

**proposed 与实测的差别**：T017 §52.23 的 ≤380 是设计预算（Review guardrail F 明确其非强制 contract）；C1 的实际布局以上表为准，验收只要求"间距 = token、余量归尾部、无重叠/裁切"。

### C1.4 Alternatives / Why This Design

| 方案 | 判定 |
| --- | --- |
| **尾部 `fillHeight` spacer（采纳）** | 项目内已验证（B3），零新组件、零新数据、可断言、可回滚 |
| 让 StatisticsOverview 长高填满 | 会让统计卡被拉伸变形（`StatCard` 尺寸有语义），且属于改共享组件行为 |
| 手工固定各段高度 | 引入 magic number，两个尺寸下都要重算，违反 guardrail F |
| 把 ContentLayout 换成 anchors 布局 | 退回 ISSUE-012 的 anchored-only 风险面 |
| 现在就把 C3 的分布条塞进来填空白 | 越界（用户 §4/§7 明令），且属 C3 |

### C1.5 Files Expected to Change

- `src/ui/qml/pages/DashboardPage.qml`（+ 尾部 spacer + 注释）
- `src/main.cpp`（Dashboard 专属几何断言 + dump 条目 + 间距 oracle）
- 文档：本文件、PROJECT_STATUS、BACKLOG、devlog

### C1.6 Test Plan（C1）

1. **间距契约（可证伪）**：`header.bottom → action.top`、`action.bottom → 下一段.top` 的垂直间距 **= `DS.spacingM`（±0.5）**——因该间距**直接由 token 绑定**（guardrail 允许等值断言），且**能捕获"余量再次被分摊到 section 之间"这一真实 regression**。
2. **余量归属**：`dashboardTailSpacer` 存在、可见、`height > 0`（@1024×720 与 @1000×700），且位于 StatisticsOverview 之下。
3. **段几何**：header / action / statistics 全部 nonzero、在 page bounds 内、无重叠、无裁切。
4. **两个尺寸**：1024×720 与 1000×700 均满足上述。
5. 既有门禁全绿；nav check 场景语义不变；10 趟矩阵保留。

### C1.7 Implementation Record（2026-09-17）

**结构变更（最小）**：`DashboardPage.qml` 的页内 `ColumnLayout` 末尾新增**唯一**一个 `Item { objectName: "dashboardTailSpacer"; Layout.fillWidth: true; Layout.fillHeight: true }`（+18 行，含解释性注释）。**未新增任何内容元素、未改任何绑定、未改 Run Demo 接线、未改 `StatisticsOverview` 调用点参数**。

**机制**：`ColumnLayout` 在**没有任何 `fillHeight` 子项**时不会把多余高度留在末尾，而是把余量分摊进各个 section 行内（实测证据见 C1.8）。加入尾部 `fillHeight` 项后，余量**全部**归它，其余子项保持 implicit 高度、按 `spacing` 紧凑排列——与 B3 Communication 的尾部 spacer 同族机制，但**不复用其结构**（Dashboard 只有一层页内布局）。

**harness 扩展（`src/main.cpp`，+176 行，纯验证）**：

1. 从**同一个 DS 单例**（`qmlContext(...)->contextProperty("DS")`）读取 `spacingM`/`spacingL`——**断言直接对 token 取值**，不是硬编码像素；读不到即 fail（防止断言静默退化）。
2. `dumpGeometryTable` 在 Dashboard 页增加 4 个条目：`dashboardHeader` / `dashboardRunDemo` / `dashboardEmptyHint` / `dashboardTailSpacer`。
3. `runGeometryAssertions` 新增 **Dashboard layout shell 断言块**：
   - 内容区顶端 **= `DS.spacingL`**（±0.5）；
   - 顺序 band（header → action → [hint 若可见] → statisticsHeader → statisticsPanel）**每一段相邻 gap = `DS.spacingM`（±0.5）**，且 gap ≥ 0（无重叠）——**该契约正是"余量再次被分摊到 section 之间"这一真实 regression 的探针**；
   - `dashboardTailSpacer` 存在、可见、非零、位于统计块之下，且其**底边 = 页面内容底边**（`page.height − DS.spacingL`，±0.5）——证明"余量确实由它拥有"。

**未做（严格按 scope）**：无 `OutcomeDistribution`、无 attention summary、无 diagnosis cue、无 recent rows、无 CTA、无 SessionChip/AppBar 改动、无新 DS primitive、无新 Controller property、无 statistics 副本或缓存。

### C1.8 Problems / RCA

**问题（RED 取证，先证断言可证伪）**：先落 harness 断言、**不改 QML** 直接跑 `--qml-geometry-check` ⇒ exit 1，精确量化了缺陷：

```text
GEOFAIL: DEFAULT dashboard: dashboard content starts at y=27, expected the page margin 16
GEOFAIL: DEFAULT dashboard: gap dashboardHeader -> dashboardRunDemo is 46, expected the DS.spacingM token 12
GEOFAIL: DEFAULT dashboard: gap dashboardRunDemo -> dashboardEmptyHint is 44, expected the DS.spacingM token 12
GEOFAIL: DEFAULT dashboard: gap dashboardEmptyHint -> statisticsHeader_dashboard is 155, expected the DS.spacingM token 12
GEOFAIL: DEFAULT dashboard: dashboardTailSpacer not found
```

dump 同时给出机制证据：`dashboardHeader: y=11`（**section 行被撑高、header 在行内垂直居中**）、`dashboardEmptyHint: y=150`、`statisticsPanel_dashboard: y=27`（其自身嵌套内）。

- **分类：layout ownership（非 implicit size / 非共享组件 / 非业务语义）**。
- **根因**：`ColumnLayout` 无 `fillHeight` 子项时把余量分摊到各行（Qt 行为，B3 已记录同族现象）；Dashboard 是唯一一个既没有 `fillHeight` 子项、也没有显式尾部 spacer 的页面。
- **修复**：加显式尾部余量所有者（上述结构变更）。**未触碰任何 statistics/business state**（§15 红线）。

### C1.9 Verification（真实命令与输出）

```text
cmake --build --preset debug-local → [7/7] Linking modbuslens.exe（0 error）
--qml-smoke-test → EXITCODE=0
--qml-nav-check  → EXITCODE=0；NAV CHECK PASS（five workspaces）
   NAV SCENARIOS: basic five-workspace path PASS, A PASS, B PASS, D PASS, E PASS,
     F PASS, G' PASS, H PASS, I PASS, J PASS, K PASS, K' PASS, L PASS, N PASS
   NAV SCENARIO M: DEFERRED BY DESIGN（不变）
--qml-geometry-check → EXITCODE=0；10/10 PASS、0 GEOFAIL、14 dump 段
   DASHBOARD LAYOUT: header.top=16 action.top=43 stats.top=113 spacer.height=343   （1024x720）
   DASHBOARD LAYOUT: header.top=16 action.top=43 stats.top=113 spacer.height=323   （1000x700）
ctest --preset debug-local → 100% tests passed, 0 failed out of 26
git diff --check → PASS
```

**两尺寸实测（Dashboard）**：

| 量 | 1024×720 | 1000×700 | 说明 |
| --- | --- | --- | --- |
| page 尺寸 | 967×679 | 943×659 | workspaceHost 内 |
| `dashboardHeader.top` | **16** | **16** | = `DS.spacingL` ✓ |
| `dashboardRunDemo.top` | **43** | **43** | gap = 12 ✓ |
| `dashboardEmptyHint`（本次运行 observed=0 ⇒ 可见） | y=73、h=12 | y=73、h=12 | gap = 12 ✓ |
| `statisticsHeader_dashboard.top` | **113** | **113** | gap = 12 ✓ |
| `statisticsPanel_dashboard` | 935×168 @y(嵌套)=27 | 911×168 @y=27 | **与 C1 前一致**（shared component 未改） |
| `dashboardTailSpacer` | y=304、h=**343** | y=304、h=**323** | 底边 = 679−16 / 659−16 ✓ |

**Legacy 零变化证明（项几何，非像素）**：把 C1 前的完整 10 趟 dump 与 C1 后的 dump 中**两个 Legacy 趟**（`DEFAULT legacy` / `MIN 1000x700 legacy`）的全部行逐字比较 ⇒ **IDENTICAL**（`StatisticsOverview` / `Transactions` 的既有 contract 全绿）。

### C1.10 Result

- Dashboard 的 **surplus space 现在有明确所有者**：section 间距恒为 `DS.spacingM`，余量（343/323）落在页面末尾的显式尾部容量中；**不再出现"顶部少量控件 + 中间被随机摊开 + 统计块被推到中段"**。
- **零新增内容**、零语义改动、零共享组件改动、零 Controller/DS/shell 改动；Legacy 几何逐字不变；nav 场景语义不变（14 项判决 PASS、M 仍 DEFERRED）。
- **Manual Review = PENDING**（C1 为布局骨架，最终视觉验收在 C3/C5；本轮人工只需确认"Dashboard 不再摊开、空白归末尾"）。
- verified LKGC **不变 = `6cc84c3`**；未 push。

### C1.11 Knowledge Learned / Ownership

- **通过哪件真实事情理解了"余量所有权"**：先写断言再改 QML，**RED 输出直接给出了机制**——`dashboardHeader: y=11`（section 行被撑高、item 在行内居中）而不是"间距变大"。这纠正了我原先"Qt 把余量加进 spacing"的含糊理解：**余量分配发生在"行"，表现出来才是 item 之间的视觉空档**。因此修法不是去调 spacing，而是**引入一个吃掉余量的子项**（tail spacer），让所有行回到 implicit 高度。
- **通过哪件真实事情理解了"断言可以等于 token"**：`DS.spacingM` 是**活的 token**（从 QML 使用的同一单例读取），所以"gap == 12"不是硬编码 magic number，而是"gap 必须等于设计系统声明的间距"——这正是 guardrail 允许的形态，也让断言能捕获"余量又被分摊回去"这种回归。
- **通过哪件真实事情理解了"零回归要被证明而不是被相信"**：C1 不改共享组件，理论上 Legacy 不受影响；但仍然用**两个 Legacy 趟的逐行几何比对**给出 IDENTICAL 证据——"理论不变量"要用机器证据兜底。

### C1.12 Potential Interview Questions

- **Q：为什么加一个空的 `Item` 就能修掉"被摊开的空白"？** A：因为空白不是被"加"出来的，而是被布局引擎**分配**掉的。`ColumnLayout` 没有 `fillHeight` 子项时会自行决定余量去向（分摊到各行 ⇒ item 在行内居中、视觉上表现为巨大间距）；给它一个 `fillHeight` 的尾部项，余量就有了唯一合法去处，其余子项回到 implicit 高度。
- **Q：怎么保证这次修复不是"看起来好了"，而是可回归的？** A：把设计意图写成可证伪断言——间距=token、内容顶端=页边距、尾部项底边=页面内容底边；并且**先让断言在旧布局上 FAIL**（46/44/155 vs 12），再改代码让它 PASS。
- **Q：改动这么小，为什么还要动测试 harness？** A：因为 harness 的 dump 里**没有** Dashboard 的段落条目，也就无法观察这三个 section 的真实几何——只能靠肉眼或像素猜。补 dump + 断言让"是否被摊开"变成机器可判定的属性。

### C1.13 Git Commit

- **C1 code commit = `a94a7b5`**（`M9-C C1: define dashboard layout and surplus-space ownership`）；prelude = `6d242de`；**不 amend 任何既有提交、不 rebase、不 push**。


## C2–C5（后续，未开始）

- **C2 — 呈现件拆分**：抽出 `StatisticsMetrics` / `StatisticsOutcomes`（各 2 个真实消费者）；`StatisticsOverview` 保持 Legacy 组合不变（视觉不变，以项几何证明）。
- **C3 — Dashboard 组合 + `OutcomeDistribution`**：分布条（分母 = `completedCount`，segments 六项，零态不渲染，文字图例，颜色非唯一载体）+ L3 状态线索行 + 空态措辞更新（B2 的"工作台"指向已过期）。
- **C4 — 有限长高 + attention 聚合**：KPI/结果卡 `fillHeight` + `maximumHeight`；attention 文本按 guardrail B 的口径。
- **C5 — geometry/evidence candidate**：补齐 Dashboard 断言与截图（demo@1024、demo@1000、empty@1024），deploy + 严格最小 PATH + 人工包。

## Git Commit / LKGC

| 阶段 | commit | 说明 |
| --- | --- | --- |
| Prelude（T018 canonicalization + Review guardrails） | `6d242de` | docs-only；**不推进 LKGC** |
| C1 | `a94a7b5` | QML + harness（`DashboardPage.qml` + `src/main.cpp`） |
| LKGC | **不变 = `6cc84c3`** | 直到 M9-C 有人工验收通过后按 Git tree classification 裁定 |
