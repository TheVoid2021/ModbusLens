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


## C2 — Statistics Presentation Extraction（Implementation Record，2026-09-17）

### C2.14 Implementation Record（2026-09-17）

**C2.1 Preflight**

```text
branch = main；HEAD = cea044c；working tree clean；git diff --check PASS
V2 verified LKGC = 6cc84c3；v1.0.0^{commit} = ae067ab（annotated tag 对象 2cee626）
origin/main = a40d935；ahead 47 / behind 0
```

**C2.2 强制重读**

T018 全文（Phase 1 引用、guardrails、C1 记录、C2 计划）；真实 `StatisticsOverview.qml`（改前 115 行逐行）、`DashboardPage.qml`、`Main.qml` Legacy 段、`StatCard`/`PanelCard`/`SectionHeader`/`DesignSystem`、`CMakeLists.txt` 的 `qt_add_qml_module QML_FILES`、`main.cpp` geometry harness。**先画出改前 item tree**（见 C2.3），未按 Phase 1 文档猜结构。

**C2.3 StatisticsOverview 对外 contract（改前冻结记录）**

| 项 | 改前（cea044c） | C2 后 |
| --- | --- | --- |
| required properties | `analysisController`、`instanceId` | **不变** |
| 根类型 / implicit | `ColumnLayout`，implicit **864×195**（= header 15 + spacing 12 + panel 168） | **不变**（新增 `objectName: "statisticsOverview_" + instanceId` 使其可观测，断言 implicit > 0） |
| `Layout.fillWidth` | true | 不变 |
| spacing | `DS.spacingM`（12） | 不变 |
| objectName 方案 | `statisticsHeader_/statisticsPanel_/statisticsRow1_/statisticsRow2_/statCard_N_/statCard_rate_/statCard_latency_/statusCard_N_ + instanceId` | **全部保留且仍在同一视觉 item 上**（行名随组件根走）；新增 `statisticsOverview_<id>` |
| 消费者 | Legacy（Main.qml）、Dashboard（DashboardPage.qml）**都只实例化 wrapper** | **不变**（两个 consumer 均未改，仍只写 `StatisticsOverview { … }`） |
| visible/enabled 语义 | 无特殊 | 不变 |

**改前 item tree（实测）**：

```text
ColumnLayout(overview, spacing=12)                      implicit 864×195
├─ SectionHeader  statisticsHeader_<id>                 15
└─ PanelCard      statisticsPanel_<id>  fillWidth       implicit 864×168
   └─ ColumnLayout(contentLayout, anchors.fill+margins 12, spacing=8)
      ├─ RowLayout statisticsRow1_<id> fillWidth sp=12   implicit 840×72
      │   5×StatCard(140/140/140/180/180×72) + Item(fillWidth)   （6 子项 ⇒ 5×12 间距）
      └─ RowLayout statisticsRow2_<id> fillWidth sp=12   implicit 732×64
          6×StatCard(110×64, tone=DS.*) + Item(fillWidth)        （7 子项 ⇒ 6×12 间距）
```

**C2.4 Baseline evidence（改前取证）**

`cea044c` 树（未改 QML）运行 `--qml-geometry-check` → `build/c2_baseline.txt`：**10/10 PASS、0 GEOFAIL**，含 Legacy/Dashboard × 2 sizes 的 panel/rows/11 卡几何。此为 extraction before/after 的**同候选环境结构证据**（非新 golden pixel）。

**C2.5 StatisticsMetrics 抽取**

新文件 `components/StatisticsMetrics.qml`：**根就是原 row1 的 RowLayout**（无额外包裹层），`objectName: "statisticsRow1_" + instanceId`，内容**逐字**搬运：3 卡 Repeater（已观测/已完成/进行中）+ 成功率卡（`hasSuccessRate ? (successRate*100).toFixed(1)+"%" : "—"`）+ 平均延迟卡（`hasAverageSuccessLatency ? averageSuccessLatencyMs.toFixed(1)+" ms" : "—"`）+ 尾部 `Item{Layout.fillWidth}`。**绑定表达式、optional 语义、格式、单位、StatCard 用法、accessibility（文本 label + 数值）全部机械保持**；`successRate` 仍直接消费 Controller 冻结值（**未**在 QML 重算 success/completed，**未**改分母）。

**C2.6 StatisticsOutcomes 抽取**

新文件 `components/StatisticsOutcomes.qml`：根 = 原 row2 的 RowLayout，`objectName: "statisticsRow2_" + instanceId`，6 卡 Repeater（成功/异常/CRC 错误/超时/协议错误/预期无响应，`tone: DS.*`）+ 尾部 Item。**未加入** Pending / attention / severity / health / percent / chart / distribution bar；`ExpectedNoResponse` 仍只是既有 outcome count（guardrail B）。

**C2.7 依赖注入决策**

**方案 A：向两个新组件注入整个 `analysisController` 引用**（`required property var analysisController`），**不**改为传 11 个标量。理由：①与 `StatisticsOverview` 现有注入风格一致（feature 组件持 Controller 引用）；②避免在 wrapper 里复制 11 条绑定（第二处失同步点）；③两种方案都不引入业务计算，A 的绑定面更小；④为 C3 的 `OutcomeDistribution` 保持注入形态对称。**authoritative source 仍只有 Controller**；数据流 = `AnalysisController → StatisticsOverview → 注入子件`。

**C2.8 instanceId / objectName 契约**

`instanceId` 继续由 wrapper 持有并**向下传递**（`instanceId: overview.instanceId`）；两个新组件的根 objectName = 原行名（`statisticsRow1_/statisticsRow2_ + instanceId`）——**不改任何 harness 已使用的旧名**，原 objectName 仍附着在同一视觉 item 上。每个实例的 rows 唯一（`_legacy` / `_dashboard`）；harness 断言两行是**不同 item** 且都位于**本实例的 panel** 之内。

**C2.9 Implicit size 结果（ISSUE-012 契约）**

- wrapper 新 objectName 使其可观测：`statisticsOverview_<id>` **implicit 864×195**（与改前推算值一致），断言 `implicit > 0` 通过 ⇒ **内容派生尺寸未丢**。
- 未引入任何 `anchors.fill` 尺寸依赖；两个新组件根都有真实 implicit（由 StatCard 的 implicit + Layout 偏好派生）⇒ 可独立作为 presentation building block。
- PanelCard implicit 保持 **864×168**（contentLayout implicit 144 + 2×12 padding）。

**C2.10 RED → GREEN**

- **RED（先落断言、未改 QML）**：`--qml-geometry-check` exit 1 ⇒ `GEOFAIL: DEFAULT legacy: statisticsOverview_legacy not found`（新 wrapper 名尚不存在；同时证明新断言可达、非空转）。
- **GREEN（抽取 + 注册后）**：exit 0，**10/10 PASS、0 GEOFAIL**。

**C2.11 中立性证据（per-item 比较，非像素）**

对 `c2_baseline.txt`（改前）与 `c2_green.txt`（改后）的 **4 个含统计的趟**（Legacy/Dashboard × 2 sizes）按 **item 名**比较 x/y/w/h/implicitW/implicitH：

```text
GEOMETRY (x/y/w/h/implicitW/implicitH): IDENTICAL across all 4 passes
PARENT changes（仅有、且为命名可见化）:
  statisticsPanel_{legacy,dashboard}: parent <unnamed> -> statisticsOverview_{…}（4 趟）
NEW items: statisticsOverview_{legacy,dashboard}（4 趟，即被命名后的 wrapper 本身）
```

⇒ **11 张卡、两行、panel 的几何逐值不变**；唯一差异是 wrapper 获得名字（panel 的 parent 名从 `<unnamed>` 变为可见名，嵌套结构本身未变）与新增的 wrapper 行。

**C2.12 注册**

`CMakeLists.txt` 的 `qt_add_qml_module QML_FILES` 增加 `StatisticsMetrics.qml` / `StatisticsOutcomes.qml`（**同一 module 机制**，build-tree 与未来 deploy 共用；无手写第二套 qmldir、无运行时复制旁路、无 Loader——ISSUE-011 教训）。

**C2.13 Verification**

```text
cmake --preset debug-local && cmake --build --preset debug-local → [63/63] Linking modbuslens.exe（0 error）
--qml-smoke-test   → EXITCODE=0；stderr 无 ReferenceError/TypeError/binding loop/is not a type/Required property …（计数 0）
--qml-nav-check    → EXITCODE=0；14 项判决全 PASS；M DEFERRED BY DESIGN；stderr 卫生计数 0
--qml-geometry-check → EXITCODE=0；10/10 PASS、0 GEOFAIL；statistics 几何 4 趟逐 item IDENTICAL
ctest --preset debug-local → 100% tests passed, 0 failed out of 26
git diff --check → PASS
```

**C2.14 负向 scope 检查**

- 新文件中 `OutcomeDistribution/attention/healthScore/cache/ViewModel` 计数 = 0；`anomaly/percent/severity` 各 1 次命中**均在注释中**（"adds NO interpretation — no anomaly predicate / no percentage, no chart / no severity"——是边界声明，不是实现）。
- `git diff --name-only`：`CMakeLists.txt`、`src/main.cpp`、`StatisticsOverview.qml`、新 2 文件。**`DashboardPage.qml` diff = 0**、**`Main.qml` diff = 0**（Legacy 消费层零架构变化）、`DesignSystem.qml`/`StatCard`/`PanelCard`/`SectionHeader`/`NavigationRail`/三页/`AnalysisController`/`TransactionListModel` **零改动**。
- 无 StatisticsViewModel、无 statistics cache、无 health score、无 Controller 新 property。

**C2.15 Problems / RCA**

- 无布局/语义失败。一次比较脚本缺陷（自捕获）：首轮 before/after 比对按**行序** zip，被新增的 wrapper dump 行错位，误报 10 处差异；改为**按 item 名对齐**后结论为 IDENTICAL + 2 类预期差异。**分类：geometry oracle（比对方法）**，非产品问题。
- 无因 extraction 红灯而触碰 Controller/统计公式/业务语义。

**C2.16 Result**

- `StatisticsOverview` 现为**兼容 wrapper/composition**（SectionHeader + PanelCard[Metrics + Outcomes]），public contract 不变；两个消费者（Legacy/Dashboard）**一行未改**仍只实例化 wrapper。
- **Legacy 外观/布局不变、Dashboard 外观/布局不变、statistics 语义不变、public binding contract 不变**——全部由逐 item 几何 IDENTICAL + 既有断言证明。
- 为 C3 铺平：Dashboard 侧未来可直接组合 pieces + 新增 `OutcomeDistribution`，而 Legacy 继续走 wrapper。
- verified LKGC **不变 = `6cc84c3`**；未 push。

**C2.17 Knowledge Learned / Ownership**

- **通过哪件真实事情理解了"组合式抽取的中立性判据"**：把 row1/row2 变成组件根（而不是再包一层 Item），使 `statisticsRow1_<id>` 这个**旧 objectName 继续落在同一个视觉 item 上**——harness 的 4 趟逐 item 比对因此能直接证明"抽取前后几何逐值相同"。若当初多包一层，parent 链和隐式尺寸都会变，中立性就得靠更多解释。
- **通过哪件真实事情理解了 implicit size 在组合中的传播**：PanelCard 的 implicit = 内容 implicit + padding；把两行换成两个组件根后，contentLayout 的 implicit 仍是 max(840,732) × (72+8+64) ⇒ panel 864×168 不变——**ISSUE-012 的教训（implicit 必须来自内容）在拆分时是可验证的**，wrapper 新增的 `implicit>0` 断言把它变成常驻护栏。
- **通过哪件真实事情理解了 instanceId 的作用**：`_legacy`/`_dashboard` 后缀让同一组件的两个实例在 harness 里可分别寻址；抽取时把 instanceId **向下传**而不是让子件自取名，避免了"两个组件各自取名导致 wrapper 与子件身份脱钩"。

**C2.18 Potential Interview Questions**

- **Q：抽取组件时如何做到"视觉零变化"可被证明？** A：三件事——①组件根直接沿用原 item（不新增层级，旧 objectName 原地保留）；②抽取前后在同一候选环境跑同一几何 dump，按 item 名比较 x/y/w/h/implicit；③把 wrapper 的 implicit size 变成常驻断言。像素比对不必要也不够稳健（DPI/抗锯齿噪声）。
- **Q：为什么给子组件传整个 Controller 而不是 11 个标量？** A：标量注入看似更"纯"，但会在 wrapper 处复制一整份绑定清单，成为新的失同步点；而该组件本来就是同一 feature 家族的呈现件，持引用与现有风格一致。判据是"是否引入业务计算/第二权威"，不是"引用传得深不深"。
- **Q：required property 在这里起什么作用？** A：它把"忘了注入"从静默错误变成加载期错误——若 wrapper 没传 `instanceId`/`analysisController`，组件无法实例化，QML 直接报错；这正是本轮 RED（`statisticsOverview_legacy not found`）与后续卫生检查（stderr 计数 0）所依赖的机制。

**C2.19 Git Commit**

- **C2 code commit = `07b03d1`**（`M9-C C2: extract statistics presentation components`）；hash 回填 = 本 docs-only 提交；**不 amend `a94a7b5`、不 rebase、不 push**。

### C2.20 C2 Review = PASS（用户）+ Review Addendum

用户裁定 **M9-C C2 Review = PASS**。补充裁定（对本文件既有内容的收紧与澄清）：

- **A. `analysisController` / `instanceId` / implicit-size behavior 属于 component contract**（组件被谁实例化、注入什么、自然尺寸来自哪里——这些是消费方可见的行为契约）。
- **B. `objectName` 属于 verification observability contract，不是 public product API**——它服务于 harness 与证据链；改名/增名只影响测试寻址，不构成产品语义变化（但仍须按 §C2.8 保持稳定）。
- **C. C2 的 before/after geometry equality 证明的是本轮 extraction 的 **structural neutrality**（同一候选环境、同一测试机制下的结构中立），**不是跨平台 pixel-perfect guarantee**。

## C3 — Dashboard Composition + OutcomeDistribution + Empty-state Refresh（Implementation Record，2026-09-17）

### C3.1 Preflight

```text
branch = main；HEAD = 4e14022；working tree clean；git diff --check PASS
V2 verified LKGC = 6cc84c3；v1.0.0^{commit} = ae067ab（annotated tag 对象 2cee626）
origin/main = a40d935；ahead 49 / behind 0
```

### C3.2 强制重读与 property 核验

T018 全文（Phase 1 IA / guardrails A–F / C1 / C2 / C3 计划）；真实 `DashboardPage.qml`、`StatisticsOverview.qml`、`StatisticsMetrics.qml`、`StatisticsOutcomes.qml`、`StatCard`、`PanelCard`、`SectionHeader`、`DesignSystem`、`CMakeLists.txt`、harness。Controller 统计属性逐一以 `AnalysisController.h` 源码为准：`observedCount/completedCount/pendingCount/successCount/exceptionCount/crcErrorCount/timeoutCount/protocolErrorCount/expectedNoResponseCount/hasSuccessRate/successRate/hasAverageSuccessLatency/averageSuccessLatencyMs`。

### C3.3 broadcast fixture 审计（§21）

`samples/t015_broadcast.mlog`（tracked）= `MODBUSLENS_MLOG|1|timeout_ms=1000` + 一条 `TXN|25|00 06 00 01 00 01 18 1B|NO_RESPONSE`：**地址 0x00 + FC06 = 广播写单寄存器、无响应** ⇒ 语义上应产出 **ExpectedNoResponse**（广播能力集 ∋ FC06；request 字段合法 ⇒ 无 request issue）。此前无任何测试消费它。按既有机制接线：`configure_file(... COPYONLY)` 进 `test_data/` + `MODBUSLENS_BROADCAST_MLOG_PATH` compile definition（repo-relative，无机器绝对路径）。**实测（探针 stage 91）证实**：`sourceLabel=t015_broadcast.mlog`、`expectedNoResponseCount=1`、`completedCount=1`、`hasSuccessRate=0`。

### C3.4 Dashboard composition before → after

**Before（C2）**：Dashboard 持有共享 wrapper `StatisticsOverview { instanceId: "dashboard" }`。

**After（C3）**：Dashboard **直接组合**（wrapper 不再实例化；`statisticsOverview_dashboard` 从树中消失，由断言证明）：

```text
DashboardPage ColumnLayout
├─ dashboardHeader / dashboardRunDemo / dashboardEmptyHint（措辞刷新）
├─ SectionHeader  statisticsHeader_dashboard          （标题迁到本页，同名）
├─ PanelCard      statisticsPanel_dashboard           （同名）
│   ├─ StatisticsMetrics   statisticsRow1_dashboard   （C2 注入决策沿用）
│   ├─ OutcomeDistribution outcomeDistribution_dashboard ← 新增（Dashboard-only）
│   └─ StatisticsOutcomes  statisticsRow2_dashboard   （C2 注入决策沿用）
└─ dashboardTailSpacer（唯一 stretch owner，随内容变高而合理缩短）
```

**Legacy 不变**：仍 `StatisticsOverview { instanceId: "legacy" }`（wrapper + Metrics + Outcomes），`Main.qml` diff = 0。

### C3.5 OutcomeDistribution 组件（P0 语义）

- **分母 = `completedCount`**（guardrail A；**不是** observedCount、**不是** successRate 的 `completed − expectedNoResponse` 分母）。
- **segments 六项、顺序冻结**：Success → Exception → CRC 错误 → 超时 → 协议错误 → 预期无响应；`completedCount == 六段之和`；**Pending 不进入**；**ExpectedNoResponse 不得从 bar 中删掉**。
- **段宽**：`unitWidth = bar.width / completedTotal`（completedTotal == 0 时 **unitWidth = 0**，全程无除零/NaN/Infinity）；`segment_i.x = unitWidth × segmentStartCount(i)`（累计边界）；**最后一段以剩余宽度收口**（`parent.width − x`，杜绝浮点漂移留缝）；**无 minimum segment width**（不扭曲比例）；count == 0 ⇒ 段宽 0。
- **零态**：`visible: completedTotal > 0`（整个组件隐藏，bar 与 caption 一起消失）；隐藏态几何**不是 contract**。
- **颜色**：沿用与六张 outcome 卡**相同的 DS 状态色**；无新 severity/semantic/health color；**ExpectedNoResponse 沿用既有呈现 tone**（`DS.expectedNoResponse`），不被编码成 error/anomaly。
- **无障碍**：bar 挂 `Accessible.role: Graphic` + `Accessible.name`（"已完成结果分布（共 N 笔）：成功 N，异常 N，CRC 错误 N，超时 N，协议错误 N，预期无响应 N"）；同时 **StatisticsOutcomes 六卡仍完整提供全部标签与数字** —— bar 不是唯一信息来源。
- **文字图例策略**：不复制第二排相同六项数字（避免信息重复）——六张 outcome 卡就是 legend；bar 上方仅一行简洁说明 `已完成结果分布`。
- **文本层**：caption `outcomeDistributionCaption_<id>`；bar `outcomeDistributionBar_<id>`；segments `outcomeSegment_0..5_<id>`（frozen order = index）。

### C3.6 Empty-state wording（B2 遗留刷新）

`dashboardEmptyHint` 措辞从 B2 的 "…或在工作台加载回放日志。"（当时回放 workspace 尚不可达）刷新为：

> 暂无通信数据。可运行演示批次，或前往"通信"、"回放"工作区获取数据。

**仅导航指引**——不暗示切到 Communication 会自动连接、切到 Replay 会自动加载文件；**无 CTA 按钮**（rail 仍是唯一导航权威）。harness 断言：可见时 `text` 含 "回放" 且 **不含 "工作台"**（锁住刷新语义）。

### C3.7 RED → GREEN

- **RED（harness 先行，QML 已同分支编写但先跑旧断言目标）**：nav check exit 1 —— `distribution demo: outcomeSegment_0..3 width 0 != expected 227.75 (bar 911, completed 4)`、`outcomeSegment_5 width 911 != expected 0`，即**段宽全错、ENR 段吃满整条**。
- **RCA（真实产品 bug，harness 抓到）**：`OutcomeDistribution.qml:60: ReferenceError: barTrack is not defined` —— bar 的 `Item` **漏写 `id: barTrack`**，`unitWidth` 绑定求值失败回退 0 ⇒ 所有段宽 0、末段收口吃满整条。**分类：product QML（presentation）**，由 DISTRIBUTION DIAG（`qml.completedTotal=4 qml.unitWidth=0`）+ stderr ReferenceError 双证据定位。修复 = 补 `id: barTrack`。
- **GREEN**：`DISTRIBUTION DIAG: qml.completedTotal=4 qml.unitWidth=227.75`；段宽/顺序/收口断言通过；broadcast 探针 `enrSegmentWidth=911 = barWidth`；zero 态干净。

### C3.8 Verification（真实命令与输出）

```text
cmake --preset debug-local && cmake --build --preset debug-local → [79/79] Linking modbuslens.exe（0 error）
--qml-smoke-test   → EXITCODE=0；stderr 卫生计数 0
--qml-nav-check    → EXITCODE=0
  NAV [distribution demo]: completed=4 success=1 exception=1 crc=1 timeout=1 protocol=0 expectedNoResponse=0 hasSuccessRate=1
  NAV [distribution broadcast]: expectedNoResponse=1 completed=1 hasSuccessRate=0 barWidth=911 enrSegmentWidth=911
  NAV [distribution zero]: completed=0 distribution hidden, zero state clean
  NAV DISTRIBUTION CHECK: PASS (denominator = completedCount; 6 segments in the frozen order; demo + 100% ExpectedNoResponse broadcast probe + zero state)
  NAV SCENARIOS: basic five-workspace path PASS, A PASS, B PASS, D PASS, E PASS, F PASS, G' PASS, H PASS, I PASS, J PASS, K PASS, K' PASS, L PASS, N PASS
  NAV SCENARIO M: DEFERRED BY DESIGN（不变）
  NAV CHECK PASS（five workspaces；scenarios …/L/N asserted；M deferred by design）
--qml-geometry-check → EXITCODE=0；10/10 PASS、0 GEOFAIL
  DASHBOARD LAYOUT: header.top=16 action.top=43 stats.top=113 spacer.height=343（1024×720，空态下分布条隐藏 ⇒ 与 C1 一致）
  DASHBOARD LAYOUT: … spacer.height=323（1000×700）
ctest --preset debug-local → 100% tests passed, 0 failed out of 26
git diff --check → PASS
stderr 卫生：ReferenceError/TypeError/binding loop/NaN/Infinity/required missing/module missing 计数 = 0（geo/nav/smoke）
```

**case 标签连续性**：94 个标签 0..93 连续且唯一（新增 87..93 为分布探针；**未伪称 Scenario O**）。

### C3.9 Legacy 零变化证明

`c2_green.txt`（C2 后）vs `c3_geo.txt`（C3 后）的 **Legacy 两趟**逐 item（x/y/w/h/implicit）比较 ⇒ **IDENTICAL**（`statisticsOverview_legacy` wrapper、panel、两行、11 卡全部逐值不变；`Main.qml` diff = 0）。

### C3.10 负向 scope 检查

`attentionCount/attention/diagnosisCue/recentTxn/sessionChip/healthScore/insight/deviceCard/timeSeries/ViewModel/cache` 在 DashboardPage 与三个统计组件中计数 **0**（`CTA` 的 1 次命中为 `Rectangle` 子串误报，已人工核实）；无 DS primitive、无 Controller 新 property、无 statistics cache；`deploy_windows.bat` 未顺手扩展（§26：module 自动部署已覆盖新组件；正式 deploy candidate 归 C5）。

### C3.11 Backend freeze

`git diff --name-only` 与 `AnalysisController` / `TransactionListModel` / `core/*` / `tests/*` / AI / Agent / Replay / Serial **零交集**；OutcomeDistribution 只消费既有 deterministic facts；**未发现需要新增 Controller statistics property 的情况**（§28 的停止条件未触发）。

### C3.12 Files Changed（C3）

新增 `src/ui/qml/components/OutcomeDistribution.qml`；修改 `src/ui/qml/pages/DashboardPage.qml`（wrapper→直接组合 + 措辞刷新）、`CMakeLists.txt`（注册 + fixture）、`src/main.cpp`（共享分布契约函数 + C2 断言重构 + dump + 探针 87..93 + verdict 行）；docs：本文件、PROJECT_STATUS、BACKLOG、devlog、INTERVIEW_NOTES。

### C3.13 Result

- Dashboard 现为**直接组合**：SectionHeader + PanelCard[Metrics + OutcomeDistribution + Outcomes]；Legacy 保持 wrapper（`statisticsOverview_legacy` 存在性由断言锁住）。
- 分布条三态全部机器证明：demo（比例正确、顺序正确、hasSuccessRate=1）、broadcast（**100% ExpectedNoResponse 且 hasSuccessRate=0** —— 两个分母不可混淆的直接证据）、zero（隐藏、无除零）。
- 空态措辞刷新并锁定（含 "回放"、不含 "工作台"）；无 CTA。
- C1 shell 契约不变（空态下布局与 C1 完全一致，spacer 343/323；有数据时 spacer 合法缩短）。
- **Manual Review = PENDING**（C3 视觉终验并入 C5）；verified LKGC **不变 = `6cc84c3`**；未 push。

### C3.14 Knowledge Learned / Ownership

- **通过哪件真实事情理解了"分母即语义"**：broadcast 探针让 `completed=1、expectedNoResponse=1、hasSuccessRate=0` 与 `bar=100% ExpectedNoResponse` **同屏成立**——如果 distribution 用了 successRate 的分母（completed−expectedNoResponse=0），这条 bar 会消失或除零。一条 fixture 把"两个分母不可混淆"从口头纪律变成机器断言。
- **通过哪件真实事情理解了 QML 的 id 作用域**：`barTrack` 漏写 id 时**应用照常启动、布局照常工作**，只是 `unitWidth` 绑定静默失败为 0，表现为"段宽全 0、末段吃满"——ReferenceError 只在 stderr。教训：**stderr 卫生检查（ReferenceError 计数 0）必须是一级门禁**，并且"宽度断言 + DIAG 输出"能在一次运行内把问题定位到具体绑定。
- **通过哪件真实事情理解了"零态是布局契约的一部分"**：空态下分布条整件隐藏 ⇒ 面板高度回到 168、spacer 回到 343/323——零态不是"有数据情形的退化版"，而是**独立状态**，其可见性与几何都要断言。

### C3.15 Potential Interview Questions

- **Q：同一个"已完成"集合，为什么分布条和成功率要用不同分母？** A：它们回答不同问题。分布条回答"完成的事里每类占多少"，分母是 completed（ExpectedNoResponse 是一种完成的结局，必须占一段）；成功率回答"该得到响应的事里多少得到了正常响应"，分母刻意剔除 ExpectedNoResponse。混用分母会把"广播写没有响应"算成"失败率"——broadcast 探针（bar 100% ENR + successRate 为 "—"）就是防混淆的机器锁。
- **Q：段宽用浮点除法，怎么保证视觉上没有缝隙？** A：前五段按 `unitWidth × count` 定宽，**最后一段不按公式，而是用剩余宽度收口**（`bar.width − x`）；同时断言"段连续、末段右缘 ≈ bar 右缘"。这样浮点舍入误差只会落在最后一段内部，不会在段间留白。
- **Q：为什么隐藏组件还要防除零？** A：`visible: false` 不阻止绑定求值。若宽度绑定写成 `bar.width / completed`，空态时 completed=0 会产生 NaN/Infinity 并在状态切换瞬间传播；实现把单位宽在零态显式置 0，NaN 从根上不可能出现。
- **Q：怎么防止"为了测试而保留一个假 wrapper"？** A：Dashboard 侧断言 `statisticsOverview_dashboard` **不存在**，Legacy 侧断言 wrapper **必须存在**——两侧同时锁，防止"删了组合但留个空壳保 objectName"这类假迁移。

## C4–C5（后续，未开始）

- **C3 — Dashboard 组合 + `OutcomeDistribution`**：分布条（分母 = `completedCount`，segments 六项，零态不渲染，文字图例，颜色非唯一载体）+ L3 状态线索行 + 空态措辞更新（B2 的"工作台"指向已过期）。
- **C3 — Dashboard 组合 + `OutcomeDistribution`**：分布条（分母 = `completedCount`，segments 六项，零态不渲染，文字图例，颜色非唯一载体）+ L3 状态线索行 + 空态措辞更新（B2 的"工作台"指向已过期）。
- **C4 — 有限长高 + attention 聚合**：KPI/结果卡 `fillHeight` + `maximumHeight`；attention 文本按 guardrail B 的口径。
- **C5 — geometry/evidence candidate**：补齐 Dashboard 断言与截图（demo@1024、demo@1000、empty@1024），deploy + 严格最小 PATH + 人工包。

## Git Commit / LKGC

| 阶段 | commit | 说明 |
| --- | --- | --- |
| Prelude（T018 canonicalization + Review guardrails） | `6d242de` | docs-only；**不推进 LKGC** |
| C1 | `a94a7b5` | QML + harness（`DashboardPage.qml` + `src/main.cpp`） |
| C2 | `07b03d1` | 新组件 ×2 + Overview 组合化 + CMake 注册 + harness 断言 |
| C2 hash 回填 | 本 docs-only 提交 | 文档 hash 回填 |
| LKGC | **不变 = `6cc84c3`** | 直到 M9-C 有人工验收通过后按 Git tree classification 裁定 |
