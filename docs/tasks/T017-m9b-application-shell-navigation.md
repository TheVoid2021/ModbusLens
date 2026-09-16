# T017 — M9-B Application Shell & Navigation（Learning & Design）

- **Goal**：为 ModbusLens V2 的 **Application Shell / Navigation** 建立有真实代码证据支撑的信息架构（IA）与导航设计，并让开发者掌握 Qt Quick 应用外壳所需的生命周期与状态归属知识——**本阶段不实现**。
- **Background / User Value**：V1 是"单页纵向堆叠"：899 行 Main.qml 从 Header/操作按钮/串口控制/统计一路排到 SplitView（诊断 + 事务表）。在 700 逻辑高（最小窗口）下，SplitView 之前的固定内容吃掉约 511px，工作区只剩 **≈190px**——事务表可读性已到极限（T016 §3-9 实测口径）。M10（主动读写）、M11（寄存器解码）、M12（Device Profile/手册问答）会继续增加面板；没有 shell/navigation，只能继续加长单页。本任务回答"什么该去哪一页、状态归谁、怎么迁移才不炸"。
- **状态**：**M9-B Phase 1 = Learning / Design（本文档）；Implementation = NOT STARTED。** 无任何 QML/src/tests/CMake/scripts/samples 改动。

## 1. Preflight（2026-09-15）

```text
pwd                        → /e/desktop/ModbusLens
git rev-parse --show-toplevel → E:/desktop/ModbusLens
git rev-parse --short HEAD → 6d06ad2（Preflight 时；证据提交后为 fae2d96）
git branch --show-current  → main
git status                 → 仅 1 个 untracked 用户文件（M9-A Foundation.png，见 §1.3，已归档）
git log --oneline -10      → 6d06ad2 … 65c24d5（完整链见 §1.1）
git describe --tags --always → v1.0.0-14-g6d06ad2
v1.0.0 tag                 → ae067ab（immutable）
V2 verified LKGC           → 6562dd3（在链上）
origin/main                → a40d935（local ahead，behind 0）
```

### 1.1 有效基线

| 项 | 值 |
| --- | --- |
| branch | `main` |
| HEAD（M9-B 开始时） | `6d06ad2` → 截图证据提交后 `fae2d96` |
| V2 verified LKGC | **`6562dd3`**（不变） |
| V1 immutable tag | **`v1.0.0` → `ae067ab`（永久不变）** |
| remote | `origin/main = a40d935`；local **ahead 6**（证据提交后 7）、**behind 0** |
| production working tree | clean（src/tests/QML/CMake/scripts/samples 零修改） |

### 1.2 Repository integrity vs Publication state（本轮 Preflight 经验，按用户要求记录）

**Local branch ahead of remote is not itself a product-safety failure.** 两类检查必须分开：

- **Repository integrity（开发安全门）**：HEAD 是否符合预期、verified LKGC 是否在链上、V1 tag 是否未被移动、production 路径是否有 dirty diff、历史是否被 amend/rewrite。这些**任何任务都不得妥协**。
- **Publication / synchronization state（发布状态门）**：`ahead/behind/remote HEAD`。它只在三种情况下阻塞：
  - A. `behind > 0` 且存在尚未审查的远端提交；
  - B. 用户明确要求先同步远端；
  - C. remote divergence 影响当前任务的历史/发布操作（如需要基于远端历史做 release）。
- 单纯 `ahead > 0 && behind == 0` **不构成 docs-only Learning/Design 任务的阻塞条件**（本轮用户的裁定）。AGENTS.md 的 no-push 规则继续有效、未被修改。
- 未来 Preflight 只记录 `ahead / behind / remote HEAD` 三个数字，不做"必须相等"的断言。

### 1.3 截图证据处置（M9-A accepted baseline）

`docs/assets/screenshots/M9-A Foundation.png`（untracked，用户放入）已核验并归档为 `docs/assets/screenshots/m9a-foundation-accepted.png`（**未改任何像素**；untracked 文件用 重命名+add，`git mv` 不适用于未跟踪文件）。核验依据：① mtime 13:30 紧接 closure 提交 13:28；② 尺寸 1280×937 = 1024×720 逻辑 × 125% DPI + 37px 原生标题栏，与既有 `v1-ui-baseline.png`（1280×937）同机同规格；③ 用户自取文件名 "M9-A Foundation"；④ 该时间段仅一次人工复核（M9-A 修复后）。已作为**用户提供的证据**记入 T016 §18，与可复现产物（geometry-1024x720.png / geometry-1000x700.png）并列。独立 evidence commit：`fae2d96`。

## 2. Scope / Non-goals

- **Scope（本 Phase 1）**：IA 与导航设计、状态归属图、用户任务映射、导航方案比较、shell 概念设计、页面生命周期与迁移策略、布局预算、知识地图、风险与测试策略。
- **Non-goals（本阶段及 M9-B 实施均不做）**：修改任何 QML/src/Controller/tests/CMake/scripts；拆 Main.qml；开始 shell 实现；M10/M11/M12 功能；push；推进 LKGC。

## 3. Evidence Base（本设计依据的真实文件，非截图推断）

| 文件 | 用途 |
| --- | --- |
| `src/ui/qml/Main.qml`（899 行） | 逐块分解映射（§12）；真实布局与绑定现状 |
| `src/ui/AnalysisController.h/.cpp`（1320 行） | 状态归属（§4）与 source switching 语义（§5/§17）真实实现 |
| `src/ui/TransactionListModel.h` | 事务集合的所有权与 roles |
| `src/ui/qml/DS/DesignSystem.qml` + `components/*` | M9-A 既有资产（shell 需在其上继续） |
| `src/main.cpp` | DS context property、`--qml-smoke-test`、`--qml-geometry-check`（含 grabWindow dump） |
| `tests/test_ui_bridge.cpp`（a/b/r/s/d/ai/t 共 40+ 用例） | V1 冻结契约的**真实测试名**（§17） |
| `src/ui/serial/SerialPortAdapter.h`、`core/serial/SerialTransactionSession.h` | 串口状态机与"Qt 侧唯一 QSerialPort 所有者"事实 |
| `docs/tasks/T016-*.md`、`docs/11_V2_UPGRADE_PLAN.md` | V2 治理、M9-A 结论、UX 问题清单 |

## 4. State Ownership Map（真实代码证据）

### 4.1 A–L 逐项回答

- **A. Simulator / Serial / Replay 如何表示？** 不是三个对象，而是**一个 session 的三种来源**：`AnalysisController` 用 `modeLabel_`（"模拟器模式"/"串口模式"/"回放模式"）+ `sourceLabel_`（"确定性演示" / "COM3 @ 9600" / 文件名）表示当前来源；数据本身统一落在 `statistics_` + `transactionModel_` + `activeDiagnosisTransactions_` 三件套（同一批次三视图，cpp:1194-1206 / 1310-1316）。"Simulator" 没有独立设备对象：`runDemoBatch()` 每次调用**重建**一个确定性 `SimulatedSlave`（寄存器 100/200/1500）并产出 4 笔固定事务（Success/Exception/CRC/Timeout，cpp:1070-1192）。
- **B. source mode 保存在哪里？** Controller 成员 `modeLabel_` / `sourceLabel_` / `serialSourceLabel_`（header:264-265, 276），QML 只读（`modeLabel`/`sourceLabel` properties, header:51-52）。
- **C. source switching 谁负责？** 只有 Controller 的三个命令：`runDemoBatch()` / `loadReplayFile()` / `connectSerial()`。三者都遵守同一原子律：**失败不动旧状态**（r03/r04/r05、s02、d04、ai07）；成功切换时旧批次清空或替换并递增 `activeBatchRevision_`（AI/Agent 失效）。`clearResults()` **永不切换来源**（cpp:1211-1224，r08）。
- **D. Serial connection state 谁拥有？** Controller（`serialConnected_` + `serialAdapter_`，Qt 侧唯一 QSerialPort 所有者，cpp:152-157）；adapter 只上报 `transactionCompleted` / `transportError`。
- **E. Serial busy/pending 谁拥有？** Controller（`serialBusy_` + `pendingSerialAddress_`，cpp:986-987）；底层单事务状态机在 `SerialTransactionSession`（adapter 持有）。
- **F. Replay loaded state 谁拥有？** Controller（批次本体 + `hasReplayError_`/`hasReplayNotice_`，cpp:1287-1319）。"回放加载"是一次**原子 source 切换**（含成功后才 teardown 串口的 SB-13 规则）。
- **G. Transaction collection 谁拥有？** Controller 内的**单实例** `TransactionListModel transactionModel_`（header:257）；QML 以 `transactionModel` 只读消费（CONSTANT property）。
- **H. Statistics 谁拥有？** Controller（`statistics_` 快照，header:256），12 个只读 property + `statisticsChanged` 信号。
- **I. Diagnosis result 谁拥有？** Controller：结构化 `activeDiagnosisTransactions_`（事实）+ `baselineDiagnosisText_`（呈现文本），header:282-284；`clearDiagnosis()` 只清诊断、`clearResults()` 清批次并连带失效。
- **J. AI result 谁拥有？** Controller（`aiDiagnosisText_`/错误文本 + 二维失效守卫 `activeBatchRevision_` × `aiRequestGeneration_`，header:290-302；ai06/ai08）。
- **K. Agent state / answer 谁拥有？** Controller 持有 `AgentRuntime`（异步有效性全在 runtime）+ `ModelScopeAgentClient`；QML 只见 `agentBusy/hasAgentAnswer/agentAnswerText/agentErrorText/agentAvailable/cloudAiBusy`（header:304-314）。
- **L. Main.qml 自有的纯 presentation 状态**：`diagnosisTabs.currentIndex`（当前诊断 tab）、`agentQuestionInput.text`（问题草稿）、串口表单（`serialPortCombo.currentIndex`、`serialBaudCombo.currentIndex`、`serialSlaveSpin/StartSpin/QuantitySpin/TimeoutSpin.value`）、`replayFileDialog`（FileDialog 实例）、root 调色别名与 SplitView 列宽 token。**这些是唯一允许被导航重组的 QML 状态**。

### 4.2 归属总表

| State | Owner（真实代码） | Consumers | Must survive navigation? | Risk if page-local |
| --- | --- | --- | --- | --- |
| source mode / label | Controller | Shell 状态条、各页 | **是**（session 级） | 页面各存一份 → 冲突/漂移，来源身份丢失 |
| serial connection | Controller + adapter | Communication 页、Shell chip | **是** | 页面销毁=断开？违反"导航不改连接"（s02/s09） |
| serial busy / pending | Controller（+session） | Communication 页 | **是** | 切页=丢 pending → 结果成为 stale/孤儿（s10 的滥用） |
| replay loaded state | Controller | Replay 页（错误/提示）+ 全页批次视图 | **是** | 切页重载=重复 IO；错误提示消失 |
| transaction collection | Controller（单一 model） | Communication/Replay 表格 | **是** | 每页各自 model → 双份数据、双份排序/格式 |
| statistics | Controller | Dashboard | **是** | 页面重算/复制 → 与事实批次失同步 |
| diagnosis result | Controller（结构化批次） | Diagnosis 页 | **是** | 切页丢结果 → 与 d02/d03 契约冲突 |
| AI result | Controller（+二维守卫） | Diagnosis 页 AI tab | **是** | 页面销毁 → in-flight 结果无处投递 |
| agent answer | Controller（runtime 持有有效性） | Diagnosis 页 Agent tab | **是** | 同上；答案呈现丢失 |
| serial port list | Controller | Communication 页 | 是（缓存可） | 每次进页枚举串口=副作用 IO |
| tab index / 草稿文本 / 表单值 | Main.qml（页面本地） | 单页内 | **否**（页面本地合法） | 若被"提升"进 Controller 反而是污染 |

## 5. Source vs Workspace

**Source / Mode = 数据从哪来（session 属性）**：`Simulator（确定性演示） | Serial（实时串口） | Replay（历史日志）`。同一时刻恰好一个；切换是 Controller 的原子命令；来源身份全局可见。

**Workspace = 用户在做什么任务（UI 属性）**：`总览 Dashboard | 通信 Communication | 回放 Replay | 诊断 Diagnosis | 设备 Device`。Workspace 不拥有数据，只消费当前 session。

```text
Source Model（数据来源，session 唯一）
                 ┌───────────────────────────────┐
                 │        AnalysisController     │
                 │  modeLabel_ / sourceLabel_    │
                 └───────────────┬───────────────┘
        ┌────────────────────────┼────────────────────────
        │                        │                        │
   [Simulator]              [Serial]                 [Replay]
   runDemoBatch()        connectSerial()          loadReplayFile()
   确定性 4 笔批次        COM 实时单笔/多次        .mlog 整批
        └────────────┬───────────┴────────────┬───────────┘
                     ▼                        ▼
          statistics_  +  transactionModel_  +  activeDiagnosisTransactions_
             （同一批次的三视图，所有页面只读消费）
```

```text
Workspace Model（UI 任务区，与 Source 正交）
   Dashboard 总览 ── 看：当前 session 健康（统计卡）+ 演示入口
   Communication 通信 ── 做：连串口、发请求（M10）、看实时事务表
   Replay 回放 ── 做：加载/校验日志、看该批事务、读 unsupported 提示
   Diagnosis 诊断 ── 分析：对"当前批次"跑基线 / AI / Agent（与来源无关）
   Device 设备 ── 知识：Profile / 手册（M12，与来源无关）
        ▲ 任何 Workspace 都不改变 Source；Source 切换不强制跳页
```

**逐问回答**：

- **Simulator 是否独立 Workspace？** **否**。Simulator 是一种 source（确定性演示数据），不是任务区；它的入口动作（运行演示批次）属于 Dashboard 的"看健康/快速开始"任务。
- **Serial 是 Workspace 还是 Communication 的一种 source/context？** **后者**：Communication 是任务区（与总线交互），Serial 是它的 source/context。未来 M10 的请求构造/下发也落在 Communication（同一任务：与实时总线对话）。
- **Replay 为什么同时具有 source 属性 + 独立 workflow 属性？** 它的**产物**是 session 数据（统计/事务/诊断都基于它，所以是 source）；它的**过程**是文件工作流（选择文件→解析→unsupported 披露→错误恢复），这个过程只在 Replay 页有意义 → 独立 Workspace 承载 process，product 流出到全页只读视图。V1 的 r03-r07（失败保留旧状态、成功替换来源）正是这条 workflow 的冻结契约。
- **Diagnosis 是否独立于 source？** **是**。基线/AI/Agent 的输入是 `activeDiagnosisTransactions_`（结构化批次），与批次由谁产生无关——这是 V1 架构（T011/T012）的核心事实，也是把 Diagnosis 独立成页的依据。
- **Device 是否独立于 source？** **是**（未来）：Profile/手册是设备知识，与传输通道正交；M12 的 Device 页不需要 source。

## 6. User Task Analysis（任务 → Workspace → 必须持续的 state）

| # | 用户意图 | Workspace | 必须持续的 state（Owner 见 §4） |
| --- | --- | --- | --- |
| 1 | 快速看当前会话是否健康 | Dashboard | statistics_、mode/source（chip）、最近批次摘要 |
| 2 | 连接真实串口设备 | Communication | serial adapter/connection、port list、表单值（页本地） |
| 3 | 主动发送请求（M10） | Communication | connection、pending/busy、事务集合（追加语义由 M10 设计） |
| 4 | 加载历史日志 | Replay | 无（动作本身切换 source；失败保留旧状态） |
| 5 | 查看最近异常 | Dashboard / Communication | transactionModel_（含 issueText）、统计 |
| 6 | 运行基线诊断 | Diagnosis | activeDiagnosisTransactions_、baselineDiagnosisText_ |
| 7 | 让 AI 解释问题 | Diagnosis | 批次 revision、AI 结果/错误、in-flight 请求身份 |
| 8 | 问 Agent 当前会话问题 | Diagnosis | AgentRuntime 状态、答案文本、cloudAiBusy |
| 9 | 查看寄存器读数（M11） | Communication（读数区） | 当前响应原始值（未来结构化扩展） |
| 10 | 选择设备 Profile（M12） | Device | Profile 选择（未来持久化设计） |
| 11 | 上传说明书（M12） | Device | 文档入库状态 |
| 12 | 查询说明书内容（M12） | Device | 问答结果 + evidence/provenance |

**用它验证设计**：12 个任务中 4 个（#2/#3/#9）落在 Communication、3 个（#6/#7/#8）落在 Diagnosis、3 个（#10/#11/#12）落在 Device、2 个跨 Dashboard/Replay——五个 Workspace 的切分正好按"任务半径"分组；且**每个任务的持续 state 都在 Controller**（表末列），没有一项要求"页面存活"。这是"页面可自由切换"的设计依据。

## 7. Navigation Alternatives

当前真实约束（§14 预算）：**垂直 700 逻辑高是稀缺轴**（V1 工作区只剩 ≈190px）；水平 1000 逻辑宽相对宽裕（事务表最小 520 + 诊断最小 300 = 820 可满足）。

**A. 现状单页演进（ColumnLayout + ScrollView/折叠）**
- Pros：零结构风险、零迁移成本；Ctrl-F 式"全在一屏"对老用户友好。
- Cons：不解决根因——700 高下依旧要先滚过 511px 固定区；M10-M12 继续加长；"哪个区域属于哪个任务"继续模糊。
- 1000×700：**不合格**（工作区 ≈190px 是既定事实）。
- M10-M12 扩展性：差（线性堆叠）。
- 迁移风险：最低。

**B. 顶部 Workspace Tabs**
- Pros：横向分组直观、实现简单（TabBar 已在本项目用过）、左上到右下的阅读顺序自然。
- Cons：占用**稀缺的垂直轴**（≈36-40px 常驻）且随窗口高度缩短无从回收；tab 标签 + 全局 source chip 挤在首行，1000 宽下 5 个中文 tab（各 3-4 字）尚可但无增长余量；M12 再加页就溢出。
- 屏幕宽度：1000 下 ≈ 5×~110px = 550，可放但紧张。
- 可发现性：高。工程工具感：中（更像浏览器/网页）。
- 迁移复杂度：低。

**C. 左侧 Navigation Rail / Sidebar**
- Pros：**不吃垂直轴**（正是稀缺轴）；分组纵向可扩展（M12 之后仍有余量）；桌面工程工具的主流形态（IDE/仪器软件）；rail 可折叠，窄窗口可退 56px。
- Cons：吃**水平轴**（56-168px）；需要新的尺寸/焦点/选中态 token；实现成本高于 B。
- 内容宽度损失（§14 实测预算）：折叠 56 → 内容 888 ≥ 820 ✓；展开 168 → 内容 776 < 820 ✗（B1-B4 期间 SplitView 仍在时），需要"展开仅在 ≥1060 宽允许"的规则（B5 拆掉 SplitView 后限制解除）。
- 五个 workspace：纵向一排 5 项（每项 ≈40px + 间距）合计 ≈260px，700 高下余 300+ ✓。
- 未来扩展性：好（可加第 6-7 项）。
- 视觉层级：rail 用 surface 系，内容卡用 card 系，层级清晰（M9-A token 已覆盖大半）。
- 折叠可能性：有（56/168 双档）。
- 实现成本：中（新组件 NavRail + NavItem，约 2 个文件，size ~150 行）。

**D（补充案，真实项目常见）：命令面板 / 全局快捷键跳页（Ctrl+1..5）+ 保留单页**
- Pros：零结构改动、键盘驱动快。
- Cons：可发现性差（新用户看不见功能分区）、不解决密度。
- 结论：作为 C 的**附件**（键盘快捷跳页）采纳，不单独作为方案。

## 8. Recommended Navigation = C（Left Rail）+ 键盘快捷跳页

理由：① 稀缺轴是垂直，rail 恰好吃富余的水平轴；② 五个 workspace 的纵向列表在 700 高下有 300+px 余量，M12 后仍可扩；③ 桌面工程工具形态与本产品定位（Modern Industrial Diagnostic Workbench）一致；④ 折叠档让 1000 宽下的 B1-B4 期间也不破 SplitView 最小宽（§14 给出精确阈值）；⑤ 与 M9-A 组件/令牌自然衔接。

**拒绝 A 的理由**：不解决已实测的密度塌陷，M10-M12 只会更糟。
**拒绝 B 的理由**：占用稀缺垂直轴；1000 宽 + 5 项目已接近上限，无增长余量；与"工程工具"气质弱于 rail。

## 9. Application Shell Proposal（概念，不含实现）

```text
┌──────────────────────────────────────────────────────────────────┐
│ AppBar (≈40)   [ModbusLens]                    [ 来源状态 chip ] │
│                                                 [ 清空结果 ]     │
├───────┬──────────────────────────────────────────────────────────┤
│ Nav   │  Workspace Content（StackLayout，仅一个页面可见）        │
│ Rail  │  ┌────────────────────────────────────────────────────┐  │
│ 56/   │  │ 当前页自己的 PanelCard / 滚动容器 / 表格           │  │
│ 168   │  ────────────────────────────────────────────────────  │
───────┴──────────────────────────────────────────────────────────
```

- **Logo / 产品名**：AppBar 左侧（保留品牌；M9-E 再谈图标）。
- **Current Source**：AppBar 右侧**全局状态 chip**：`模式 · 来源`，如 `模拟器模式 · 确定性演示` / `串口模式 · COM3 @ 9600` / `回放模式 · demo_v1.mlog`。唯一副本，全页可见。
- **Serial connected/disconnected 是否全局可见？** **是**（chip 内状态段：`串口模式 · COM3 @ 9600 · 已连接`）。理由：来源身份是诊断工具的元信息；切到 Diagnosis 页时用户仍需知道"当前批次是实时还是回放"。
- **Simulator deterministic badge？** **不需要单独 badge**——chip 的 `模拟器模式 · 确定性演示` 已完整表达；再挂 badge 是同一信息的第二副本。
- **全局 Clear Results？** **留在 AppBar（次级按钮）**：它是 session 级、与来源无关的动作（cpp:1211 语义：清结果不换来源），放全局可避免每个页面各带一个"清空"造成多所有者。**否决**"每页自带清空"。
- **Load Replay？** **Replay 页的主动作**（含它自己的 FileDialog）：这是 workflow（选文件→校验→披露），只在该页有意义；Source chip 会在成功后全局体现结果。AppBar 不放"加载回放"。
- **Run Demo Batch？** **Dashboard 页的上下文动作**（含空态引导）：它是一次 source 切换（runDemoBatch 会 teardown 串口，cpp:1066-1068），语义上是"进入演示会话"，属于 Dashboard 的"快速开始/看健康"任务。
- **Replay error / notice**：仅 Replay 页展示（单一所有者）；不复制到 chip。
- **Serial error**：仅 Communication 页。

## 10. Page Lifecycle（StackLayout vs Loader vs visible switching）

| 维度 | StackLayout（全部实例化一次） | Loader + source 切换（按需创建/销毁） | `visible` 手动切换 |
| --- | --- | --- | --- |
| Creation timing | 启动时全部创建（本应用 5 页都轻量，成本可忽略） | 首次访问创建；重建有成本与首帧闪烁 | 同 StackLayout |
| State preservation | 天然保留（实例不销毁） | **销毁即丢**（页本地状态随之丢失） | 保留 |
| Binding preservation | 保留（绑定持续有效） | 重载后重建绑定（易漏） | 保留 |
| Memory | 5 页常驻（可忽略） | 更低但无必要 | 同 StackLayout |
| Initialization side effects | 一次性、可预测 | 每次进入重新触发（枚举串口/IO 副作用风险） | 一次性 |
| Controller 连接重复 | 结构性排除（只实例化一次） | 每次实例化都可能重复 connect（需手工防重） | 结构性排除 |
| 导航复杂度 | 最低（currentIndex） | 需要映射/source 管理/lazy 策略 | 最低但需手工管 z-order/尺寸 |

**结论：StackLayout（全部页面一次性实例化，切页仅改 currentIndex）**。理由基于本产品真实语义：AI/Agent 请求在用户切页时**必须继续**（Controller 持有异步，页面只是显示面）；页本地状态（问题草稿、tab 选择、表单值）在切页往返后保留是明确的 UX 收益；Loader 的销毁语义会把这些全部打碎并引入"重复 connect"这一类新风险，而收益（内存）在本应用量级下没有意义。

- **Communication 页隐藏时 Serial 应否断开？** **不应**。V1 语义：断开是显式用户动作（`disconnectSerial()`），导航是呈现层；`serialConnected_` 与来源身份必须跨页存续（s02/s09 的契约精神）。页面隐藏不改传输状态。
- **Diagnosis 页隐藏时结果应否销毁？** **不应**。诊断/AI/Agent 状态归 Controller；批次变更才失效（d02/d03/ai06），UI 可见性不是失效条件。in-flight 请求继续，取消仍是显式动作。

## 11. State Ownership Rule（M9-B 实施期强制）

1. **业务/session 状态不得因 Workspace 导航转移为 Page-local**（serial connection、pending transaction、transaction model、statistics、diagnosis facts、AI/Agent 结果、replay 状态继续由 Controller 持有）。
2. **Page 组件只做消费/展示/发信号**：读 property、调用 Q_INVOKABLE；不得在页面内复制 Controller 状态、不得在页面内 new 第二个 model。
3. **页本地（合法且应当本地）**：tab 选择、输入草稿、表单值、滚动位置、纯显示开关。
4. **禁止"页面化 = 每页一份状态"**：任何"页 A 的 serialConnected 与页 B 不一致"都视为实施失败。
5. 页面**不得**在 Component.onCompleted 里做有副作用的 IO（枚举串口等）——需要数据就绑定 Controller 已有 property。

## 12. Main.qml Decomposition Map（899 行 → 未来所有者）

| 现行条块（行号，2026-09-15） | 未来所有者 | 标记 |
| --- | --- | --- |
| root 调色别名/背景（1-48） | App Shell（保留；继续 alias 到 DS） | **STAY GLOBAL** |
| AnalysisController + replay FileDialog（50-67） | Controller 实例留 shell；**FileDialog → Replay 页** | MOVE（部分） |
| Header（产品名 + mode/source）（70-101） | Shell AppBar（吸收并升级为来源 chip） | **MOVE → Shell** |
| 分隔线（92-102） | Shell chrome | STAY GLOBAL |
| Top Actions 三按钮（104-128） | `Run Demo → Dashboard`；`Load Replay... → Replay`；`Clear Results → Shell AppBar` | **SPLIT** |
| Replay error Label（130-137） | Replay 页 | MOVE |
| Replay notice Label（139-147） | Replay 页 | MOVE |
| Serial GroupBox（149-276） | Communication 页（核心区） | MOVE |
| Serial error Label（278-286） | Communication 页 | MOVE |
| Statistics（SectionHeader+PanelCard，288-381） | Dashboard 页（session 健康） | MOVE |
| 分隔线（383-392） | 由 shell 布局取代 | **REMOVE（shell 取代）** |
| SplitView 左：Diagnosis 面板（TabBar+StackLayout+输入，394-718） | Diagnosis 页 | MOVE |
| SplitView 右：Transactions 面板（720-896） | Communication 页（实时视图）**与** Replay 页（批次视图）共享 `TransactionTable` 组件 | MOVE（含组件抽取）→ 抽取时机 **DEFER**（见 §13 B3/B4） |
| 窗口/任务栏图标 | M9-E | FUTURE |
| Request Builder（主动读写） | M10 → Communication | FUTURE M10 |
| 寄存器解码读数区 | M11 → Communication（读数）+ Dashboard（摘要） | FUTURE M11 |
| Device Profile / 手册 / 问答 | M12 → Device 页（3 个任务） | FUTURE M12 |
| Dashboard 的"最近异常"紧凑视图 | M9-C 细化 | DEFER |

## 13. Migration Strategy（每步可运行、可独立回滚）

> 原则：**先加护栏，再搬家具**——B1 先落地 shell 与新几何/导航断言，之后每一步只搬一个条块且护栏保持全绿。禁止 big-bang 拆页。

- **M9-B1 Shell skeleton**：加入 AppBar + NavRail + StackLayout；内容暂时**原样**放进唯一的 `DashboardPage`（等价搬迁，绑定不动）；来源 chip 接 `modeLabel/sourceLabel/serialConnected`；`Clear Results` 移入 AppBar。护栏：扩展 `--qml-geometry-check`（rail 宽>0、workspace 内容宽高>0、双尺寸）+ 新增 `--qml-nav-check`（见 §18）。验收=旧绑定零变化 + 双 smoke + ctest 25/25 + 人工。
- **M9-B2 Dashboard extraction**：统计卡与 `运行演示批次` 归位 Dashboard；空态引导（"运行演示批次查看确定性诊断"）。
- **M9-B3 Communication extraction**：Serial GroupBox + Serial error + 事务表（首次抽取 `TransactionTable` 组件，Replay 复用）→ Communication；此时 SplitView 从 Dashboard 页消失，进入 Communication 的"单表 + 控制区"布局。
- **M9-B4 Replay extraction**：FileDialog + 加载按钮 + error/notice + 批次表视图归位 Replay。
- **M9-B5 Diagnosis extraction**：TabBar/StackLayout/Agent 输入迁入 Diagnosis 页；删除最后的分栏依赖；`qml_geometry_check` 的 SplitView 断言退场、换为"页内最小宽"断言。
- （M9-C/D 再做各页的视觉打磨，不在 M9-B 范围。）

**每步验证**：build（debug-local）→ `--qml-smoke-test` → `--qml-geometry-check`（含 nav）→ full ctest（25/25）→ 人工视觉（1024×720 + 1000×700）→ 单提交 → 回滚=revert 该提交。

## 14. Responsive / Minimum Window 布局预算（设计预算，实施时以测量为准）

固定量：outer padding 16×2；AppBar 40；rail 折叠 56 / 展开 168；工作区卡内边距 12×2。

**垂直（700 最小高）**：
```text
700 − 32(outer) − 40(AppBar) − 12(gap) = 616  工作区可用高
616 − 24(卡内边距)                      = 592  页内容高
对照 V1 当前：700 − 32 − 40 − 1 − 34 − 118 − 21 − 168 − 1 − 96(spacings) ≈ 190  工作区高
```
→ **净收益 ≈ +400px/页**；每页各自滚动，互不挤压。

**水平（1000 最小宽）**：
```text
1000 − 56(rail 折叠) − 32(outer) − 24(卡内边距) = 888  可用内容宽
  事务表最小 520 ✓（V1 SplitView 最小值不变）
  诊断+表格并行（B1-B4 期间）= 300 + 520 = 820 ≤ 888 ✓
rail 展开 168：776 < 820 ✗（B1-B4 期间） → 规则：展开档仅在窗口 ≥ 1060 允许（820+24+168+32=1044，留 16 余量）
   B5 之后不再并行分栏 → 展开档在 1000 下也可用（单表 520 ≤ 776 ✓）
```
**M10 预留**：Request Builder 与事务表并行时预算 ≈ 888 − 520 − 24 = 344 宽；不足则改为表格上方/下方的堆叠区（设计约束记录，M10 细化）。
**结论**：rail 折叠档在 1000×700 下不压缩任何既有工作区；展开档需 ≥1060 或等待 B5 完成。

## 15. Visual Hierarchy（基于 M9-A 令牌；不新增大量 token）

层级：App 背景（`DS.background`）→ NavRail 表面（`DS.surface` + 右分隔线 `DS.separator`）→ AppBar（`DS.surface` + 底分隔线）→ Workspace 卡（`DS.surface` + `DS.border` + `DS.radiusM`）→ Panel/Card（`DS.cardSurface`，M9-A 既有）。

**M9-B 实施可能真实需要的 token（仅登记，不实现）**：`appBarHeight`、`navRailWidth` / `navRailCollapsedWidth`、`navItemHeight`、`navItemSpacing`、`workspacePadding`、`chipBackground`/`chipBorder`/`chipText`、`navActiveSurface`/`navActiveBorder`、`focusRing`。优先复用现有 spacing/radius/semantic 色，避免造第二套色板。

## 16. Knowledge Before Implementation（每条回答"解决 ModbusLens 哪个真实问题"）

| # | 知识点 | 解决的真实问题 |
| --- | --- | --- |
| 1 | Information Architecture | M10-M12 内容无处安放（当前只能加长单页） |
| 2 | Navigation Model | 700 高下工作区仅 ≈190px；垂直轴稀缺需从导航形态入手 |
| 3 | Workspace | "哪个区域属于哪个任务"模糊导致每个新功能都往同一列里塞 |
| 4 | Source / Mode | 用户必须随时知道"当前数据从哪来"（实时/演示/回放），否则会误读结论 |
| 5 | State Ownership | 防止拆页时把 session 状态拆散；也解释为什么 Controller 不能被页面复制 |
| 6 | StackLayout | AI/Agent 进行中的请求在切页后必须继续、结果必须可达 |
| 7 | Loader | 理解其销毁语义为何会打破"状态存续"，从备选中排除 |
| 8 | Component lifecycle | 避免"每进一次页就重新 connect/枚举串口"这类隐性副作用 |
| 9 | Persistent state（跨导航） | 串口连接、批次、诊断结果跨页存续是产品语义而非实现细节 |
| 10 | Page-local presentation state | 问题草稿/tab 选择属于页面，不该被提升污染 Controller |
| 11 | Shell vs Page | 全局来源 chip 只做一份；页面不重复 header/来源信息 |
| 12 | Incremental migration | 934→899 行单文件的拆分只有"每步可运行+护栏先行"才安全（M9-A 的 FAIL/remediation 就是护栏价值的实证） |

## 17. V1 / M9-A Contracts at Risk（导航不得改变的既有行为）

| 契约 | 证据（真实测试名/代码） | 导航改造的红线 |
| --- | --- | --- |
| source switching 原子性 | `r03_parseErrorPreservesState`、`r04_executionError`、`r05_fileOpenFailure`、`s02_failedConnectAtomicPreservation`、`d04_failedSwitchKeeps`、`ai07_failedSwitchKeepsAi` | 按钮搬家只能改"位置"，不得改调用顺序/时机；不得在页面里预校验后自行切换 |
| 成功切换语义 | `r07_sourceReplace`、`s07_serialReplace`（revision 递增 → 派生失效） | 页面不得缓存旧批次而不随通知刷新 |
| Clear 语义 | `r08_clearKeepsSource`、`s08_clearSerialResults`、`d08_clearDiagnosisOnly` | Clear Results 移到 AppBar 后仍必须"不切来源、不断连接" |
| Serial pending / stale | `s10_staleCompletionGuard`、`s09_serialErrorRecovery` | 切页不得取消/覆盖 pending；不得重复 start |
| 统计绑定 | `b01_runDemoStatistics`、`r01_goldenReplay`、`STAT-*`（core 层） | Dashboard 只是新家：取值与格式化（toFixed(1)"%"/" ms"/"—"）逐字保持 |
| 诊断/AI/Agent 失效规则 | `d02_clearResultsInvalidates`、`d03_newBatchInvalidates`、`ai06_newBatchInvalidatesAi`、`ai08_crossBatchStaleGuard` | 页面可见性不是失效条件；不得新增"离开页面即清除" |
| 事务模型 | `a03..a06`、`t01..t05`（roles/双行/多 issue 顺序） | 复用同一 `transactionModel`；不得 per-page model |
| Top Actions 接线 | Main.qml 104-128（三 onClicked） | Split 时保持调用名与参数 100% 一致 |
| M9-A 资产 | DesignSystem tokens + 四组件 + `qml_geometry_check` | 不重定义令牌；几何护栏只允许**加强**（新增断言），不允许放宽/删除 |
| 最小窗口 | 1000×700 + SplitView min 300/520 | rail 展开档需遵守 §14 的 ≥1060 规则，或等 B5 后再放宽 |
| UI 文案政策 | `docs/06_UI_LANGUAGE_POLICY.md` | 导航项命名（总览/通信/回放/诊断/设备）走中文界面 + 术语保留策略 |

## 18. Testing Strategy（M9-B 实施后；本阶段不实现）

- **既有基线**：full ctest **25** 目标（含 `qml_smoke`、`qml_geometry_check`、`ui_bridge` 40+ 用例）——每一步迁移后必须继续全绿。
- **新增最小护栏（设计）**：
  1. 扩展 `--qml-geometry-check`：+ NavRail 宽>0、workspace 内容区宽高>0（默认 + 1000×700 双尺寸）。
  2. 新 `--qml-nav-check`（同一 CLI 机制，不引入 Qt Quick Test 框架）：程序化激活每个 rail 项 → 断言 `currentIndex` 变化、目标页 visible、其余隐藏；断言**页面对象身份跨切换不变**（=状态存续的结构性证明）；带批次时切换后 `transactionModel.rowCount` 与 `serialConnected` 读数不变。
  3. 不新增 ui_bridge 用例（Controller 不动）；上述 guard 只覆盖 shell/导航层。
- **人工（每步必做）**：1024×720 与 1000×700 两尺寸；逐 workspace 检查：导航选中态/键盘可达、resize 不崩坏、source chip 正确、跨页状态存续（切走再回来：连接仍在、批次仍在、AI/Agent 结果仍在）、事务表可见性与列宽未退化、Serial/Diagnosis 旧风格区域行为不退化。

## 19. Accessibility / Keyboard（最低要求）

- rail 项与 chip/清空按钮全部可聚焦（延续 M9-A `AppButton` 的 `Qt.StrongFocus` 习惯），Tab 顺序：NavRail → AppBar → 页内容。
- **焦点指示器必须可见**（Fusion 默认焦点框或显式 `focusRing` token）；不得为了"干净"设 `focusPolicy: Qt.NoFocus`。
- 选中态（当前 workspace）与 hover/disabled 三态显式区分；选中态不得只靠颜色（文字加粗/左侧强调条 + 文本，延续 M9-A"颜色只是辅助通道"原则）。
- 键盘：Enter/Space 激活 rail 项；键盘快捷跳页（Ctrl+1..5）作为 C 的附件；不使用会吞掉 Tab 的自定义按键处理。
- 禁用态语义沿用现状（如无串口时"连接"禁用），导航不引入新的禁用逻辑。

## 20. Deliverables / Status

- **本阶段交付**：本文档（IA/导航/state map/分解图/迁移/预算/知识/风险/测试/无障碍）。
- **Implementation = NOT STARTED**：未创建 NavRail/NavItem 组件、未拆 Main.qml、未改任何代码或测试。
- **下一步**：M9-B Phase 1 Review（用户）；批准后按 §13 的 B1→B5 顺序逐阶段实施（每阶段单独 Review 与提交）。


## 21. M9-B Phase 1 Review = PASS（用户，2026-09-15）+ 实施 Guardrails

用户批准进入 **M9-B1 — Application Shell Skeleton**，并追加以下实施约束（本文档为 authority）：

- **A. modeLabel / sourceLabel 仅为 display state。** 禁止从 label 字符串反推业务 source；禁止在 QML 创建第二份 source truth。QML 只能消费 `modeLabel`/`sourceLabel`/`serialConnected`（authoritative property）做显示。
- **B. Shell workspace host 使用 StackLayout**（本设计结论）**但不禁止未来使用 Loader**：未来 heavy optional subview（例如 M12 手册阅读器）若有真实需求，可单独设计（届时另立设计段落）。
- **C. M9-B1 不创建可交互的假 Workspace**：尚未迁移的入口必须 disabled / non-interactive；不得出现"点击 → Coming Soon 空页"。
- **D. Rail 展开不得把真实业务内容压缩到低于最低宽度。** 1060 是设计预算值而非常数：若未来实现展开档，必须依据"available content width ≥ required content width"重新计算，不得把 1060 写成永久 magic number。（B1 决策：**只实现 compact rail，不实现 expanded mode**。）

### 21.1 Baseline（实施前核验）

```text
branch=main；HEAD=416be79；working tree clean；v1.0.0=ae067ab；ahead 8 / behind 0（已知允许）
ctest --preset debug-local → 100% tests passed, 0 tests failed out of 25（含 qml_smoke + qml_geometry_check）
```


## 22. M9-B1 — Application Shell Skeleton（Implementation Record，2026-09-15/16）

### 22.1 Implementation

- **Shell 结构**（Main.qml）：`ApplicationWindow → ColumnLayout(spacing:0) → [AppBar(40, objectName appBar) + 1px hairline] + RowLayout → [NavigationRail(id navigationRail, 56) + 1px 分隔 + StackLayout(objectName workspaceHost) → Item(objectName legacyWorkspace) → ColumnLayout(anchors.fill + margins 16)]`。旧 Header（产品名 + 双行 mode/source Label）被 AppBar 吸收；旧根布局的 16px 外边距下沉为 legacy 页内缩。
- **AppBar**：产品名（DS.fontSection bold）+ 全局 session/source 显示（`modeLabel` bold + `·` + `sourceLabel` + 条件 `serialConnected → "· 已连接"` 绿色）+ `清空结果` AppButton（objectName appBarClearResults）。**只消费既有 authoritative display properties**；未新增任何 Controller 状态；未从字符串反推 source（guardrail A）。
- **Clear Results 迁移动机核验**：`AnalysisController::clearResults()` 注释与实现（cpp:1211-1224）+ 契约 `r08_clearKeepsSource` / `s08_clearSerialResults` 证明它是 session 级动作（清结果、不换 source、不断连接）→ 允许迁至 AppBar；`onClicked` 与调用名逐字保持（无 enabled 绑定，保持原样）。
- **NavigationRail**（新组件，presentation-only）：compact 56px；6 个入口 = `工作台`（唯一真实 workspace，enabled、selected）+ `总览/通信/回放/诊断/设备`（未来 workspace，**全部 disabled**）。选中态三通道（navigationSelectedSurface 底 + 3px DS.primary 左条 + 加粗）；`activate()` 是唯一选中变更路径（鼠标与 Enter/Space 共用），且首行 `if (!enabled) return`。**未实现 expanded 模式**（§21-D；B1 无真实需要）。
- **WorkspaceHost**：StackLayout，`currentIndex: navigationRail.currentWorkspaceIndex`；子项为**纯 Item**（页根模式：StackLayout 拥有其几何，Item 内部再做锚定内缩）。
- **LegacyWorkspace**：承载全部旧功能（Run Demo、Load Replay、Replay error/notice、Serial Controls、Statistics、Baseline/AI/Agent、Transactions）——原样搬迁、绑定未动；仅顶部注释与缩进变化。
- **DS 新增（3 个最小 token）**：`appBarHeight: 40`、`compactNavWidth: 56`、`navigationSelectedSurface: "#F5F7FA"`（与 surfaceAlt 同值但独立语义位，M9-C 可单独重绘导航）。未调整任何 M9-A 既有 token。
- **回归护栏扩展**（main.cpp `--qml-geometry-check`，测试名不变）：新增 shell 断言——appBar/navigationRail/workspaceHost w/h>0；rail 与 host 不重叠；host 右缘 ≤ 窗口宽；**host 宽度 ≥ 852**（= 300+520 SplitView 最小 + 2×16 页内缩，常量带注释）；原 statistics/Diagnosis 断言全部保留。新增 NAV 断言——`currentWorkspaceIndex ∈ [0,5]` 且初始为 0；legacyWorkspace 在 index 0 可见；navItem_0 enabled、navItem_1 disabled；**直接 invoke `activate()` 于禁用项后 index 不变**（且检查 invoke 返回值，防止方法不可解析导致断言空转）；重复激活当前项为 no-op。
- **部署**：新增 QML 文件沿用 qt_add_qml_module + 生成模块整目录复制（ISSUE-011 经验），无手写 qmldir。

### 22.2 Files Changed

- 新增：`src/ui/qml/components/NavigationRail.qml`。
- 修改：`src/ui/qml/Main.qml`（shell 化重构，899 → 987 行；全部业务块原样保留）、`src/ui/qml/DS/DesignSystem.qml`（+3 token）、`CMakeLists.txt`（QML_FILES 注册）、`src/main.cpp`（shell 几何 + NAV 断言）。
- 未改动：AnalysisController/TransactionListModel/Serial/Replay/Diagnosis/AI/Agent/core/tests/scripts/samples。

### 22.3 Problems Encountered

1. **StackLayout 子项 anchors = undefined behavior**（smoke stderr 抓到）：`legacyWorkspace` 初版以 `anchors.fill/anchors.margins` 填充 host，运行时警告 "Detected anchors on an item that is managed by a layout"。属 ISSUE-012 同族（layout 管理的 item 不得再自行锚定）。
2. **StackLayout 不兑现 `Layout.margins`**（实测）：改用 `Layout.margins: DS.spacingL` 后 dump 显示 legacy 铺满 host（x=0 w=967），页内缩丢失——不能拿它当 anchors 的替代。
3. **验证脚本自身的假阳性（工具教训，二次出现）**：像素检查里"函数内 Write-Output + 表达式 `-and`"会把输出吞进表达式值，导致判定恒真、检查空转（M9-A 的 ps_pixel_check 同类问题在 B1 的 shell 区复现）。已统一改为"函数只返回 bool + 顶层裸语句打印"；并给 NAV 断言补了 invoke 返回值检查，防止"方法不存在 → 断言空转"。

### 22.4 RCA / Solution

1. **根因**：StackLayout（Layouts 家族）对子项拥有几何所有权；anchors 与 Layout.margins 都不属于"把内缩交给页面自己"的正确手段。**解决**：确立**页根模式**——StackLayout 子项一律是纯 `Item`（不锚定、不设 Layout.margins），页面内缩/内容布局由 Item 内部（anchors 或子布局）自行完成。此模式将直接复用于 B2–B5 的每个 Page。
2. **根因**：自动化"通过"不等于断言真的执行过。**解决**：断言函数分两类——纯判定函数只返回 bool；打印一律顶层语句；对反射式调用（invokeMethod）检查返回值，杜绝空洞通过。

### 22.5 Verification（真命令 + 真输出）

```text
build（debug-local）                               → Linking modbuslens.exe（干净）
--qml-smoke-test                                   → EXITCODE=0，stderr 仅字体目录环境提示（anchors 警告已消除）
--qml-geometry-check（默认 + 1000×700）            → GEOMETRY CHECK PASS；EXITCODE=0；NAV 断言含 invoke 返回值校验
  双尺寸实测：appBar 1024×40/1000×40；rail 56×679/56×659；host 967/943（≥852 预算）；
            legacy Item 铺满 host；statistics 卡片几何与 M9-A 完全一致（row1 h=72 / row2 h=64）
full ctest --preset debug-local                    → 100% tests passed, 0 failed out of 25（测试名不变，断言已扩展）
git diff --check                                   → 通过
deploy_windows.bat + 无开发 PATH deploy smoke      → [OK] + EXITCODE=0
截图像素自检（ps_pixel_check_b1，双尺寸）          → FILE VERDICT: PASS（AppBar 文字/按钮面、rail 主色选中条 40px、禁用项灰字、统计 11 卡文字均 OK）
```

### 22.6 Manual Review

- 新截图：`docs/assets/screenshots/m9b1-shell-1024x720.png`、`m9b1-shell-1000x700.png`（grabWindow 真值；M9-A 证据文件未覆盖）。
- 状态：**PENDING USER REVIEW**。复核清单（§19 A–J）：AppBar 不截字；Rail 与内容不重叠；无水平裁切；Statistics 不回归；Serial Controls/Diagnosis/Transactions 可达；mode/source 显示真实；disabled 未来 workspace 不误导；resize 后结构稳定（1000×700 与 1024×720 两尺寸）。
- **自动测试 PASS 不构成验收，也不推进 LKGC。**

### 22.7 Knowledge Learned（结合真实实现，四问必答）

1. **shell state vs business state**：Shell 里唯一的新状态是 `currentWorkspaceIndex`（选中态）与焦点/呈现，全部属于导航呈现；source/serial/批次/诊断/AI/Agent 仍由 Controller 独占——AppBar 的 session 显示直接绑定 `modeLabel/sourceLabel/serialConnected`，没有任何第二份 truth（guardrail A 的落地方式就是"只读绑定 + 不加 Controller 状态"）。
2. **StackLayout lifetime**：全部页面一次性实例化、切换只改 currentIndex——本次 legacy 页在 epilogue 全程存活（切页不触发任何 Component.onCompleted 副作用）；`currentIndex` 绑定 rail 后，"禁用项无法改 index"不再是 UI 装饰，而是**页面存续与页面可见性的结构前提**（护栏把这条前提钉死）。
3. **compact rail width budget**：56px 不是审美常数——它由"1000 − 56 − 32(页边距) = 912 ≥ 852（SplitView 300+520+2×16 的既有最低预算）"推出；实测 host=943 ≥ 852 ✓。expanded 档本次不实现；未来实现时必须重算 available ≥ required（guardrail D）。
4. **source display vs source authority**：`modeLabel/sourceLabel` 只是 Controller 拿来做显示的字符串；authority 是三命令的原子切换语义（失败不动旧状态、成功递增批次 revision）。AppBar 只显示不判断——"从 label 反推 source"被 guardrail A 明令禁止，因为字符串可翻译、可重排，而业务判定必须继续走 authoritative property 与 Controller 命令。

### 22.8 Potential Interview Questions（M9-B1 新增）

1. 为什么 StackLayout 的子项必须是纯 Item？——Layouts 拥有子项几何，anchors/Layout.margins 都会被运行时判为 undefined behavior 或被忽略（两个都实测过）；"页根模式"把内缩收回页面内部。
2. 壳层怎么做到"导航不碰业务"？——shell 只新增选中态；显示全部只读绑定 authoritative properties；护栏里专门断言禁用项无法改变选中 index。
3. 自动通过为什么还要人工验收？——B1 的 anchors 警告与 Layout.margins 被忽略都是"自动链路不够看"的实例；几何断言能证明合同，但不能证明视觉质量（M9-A 的 PASS/FAIL 循环已是项目惯例）。
4. compact rail 的宽度怎么定的？——从最小窗口与既有业务最低宽度反推（1000−56−32 ≥ 852），不是拍脑袋。

### 22.9 Candidate Commit

- `189c62c`（main，未 push）— `M9-B1: application shell skeleton`（12 files：NavigationRail 新增 + Main.qml/DS/main.cpp/CMakeLists + docs + 两张截图；**candidate 提交，不推进 LKGC**——待 Manual Visual Review PASS 后再议）。

## 23. Next

- **M9-B1 Manual Visual Review = PENDING USER REVIEW**（截图 `m9b1-shell-1024x720.png` / `m9b1-shell-1000x700.png`；复核清单见 §22.6）。
- PASS 之前：不推进 LKGC（verified 保持 `6562dd3`）、不 push、不开始 B2（Dashboard extraction）。


## 24. M9-B1 Manual Visual Review = PASS（用户，2026-09-16）

**人工确认（用户原话要点）**：AppBar 不截字；session/source 信息可读；Rail 与 Workspace 不重叠；1024×720 无明显水平裁切；M9-A statistics 无回归；Serial Controls 可见；Diagnosis 可见；Transactions 可见；disabled future workspace 明显不可用；resize / shell structure 未见视觉崩坏。

**PASS 边界（明确声明）**：本 PASS **仅表示 Shell Skeleton 达到验收要求**；**不表示** M9-B 全部完成，**也不表示** M9 UI Refresh 全部完成。

## 25. Deferred Visual Work（明确留待后续，非 B1 regression）

- temporary "工作台" 入口（B1 的过渡命名，拆分页面后由真实 workspace 入口取代）
- real Dashboard navigation（B2 起）
- navigation icons / richer visual identity（M9-E）
- SessionChip visual refinement（M9-C）
- Serial controls styling（M9-D）
- Diagnosis styling（M9-D）
- Transaction workspace styling（M9-D）
- window / taskbar icon（M9-E）
- native-title / content coherence（M9-E 或专门设计）

以上均为**计划内后续工作**，不计入 M9-B1 缺陷。

## 26. M9-B1 Final Status = COMPLETE

**交付清单**：AppBar · compact NavigationRail · StackLayout WorkspaceHost · LegacyWorkspace preservation（全部旧功能）· authoritative mode/source display · shell geometry regression assertions · disabled future navigation guard · accessibility baseline（StrongFocus/焦点迁移/三通道选中态）· deploy validation（生成模块整目录复制）· **用户 Manual Visual PASS**。

提交链：`189c62c`（实现，= **V2 verified LKGC**）→ `b7e7d72`（docs-only 哈希回填，非 LKGC）。

## 27. M9-B1 Knowledge Closure（基于真实实现，8 点）

1. **Shell presentation state 与 business/session state 的边界**：shell 新增的状态只有 `currentWorkspaceIndex` 与焦点/呈现；AppBar 的 session 显示全部是**只读绑定**（modeLabel/sourceLabel/serialConnected）。边界检验方法=看"删掉壳层后业务是否仍成立"：串口连接、批次、诊断/AI/Agent 在壳层不存在时依然完整——证明它们从未被壳层拥有。
2. **为什么 modeLabel/sourceLabel 只能显示不能做 authority**：它们是可翻译、可重排的展示字符串；authority 是三命令的原子语义（失败不动旧状态、成功后递增批次 revision、clear 永不换 source）。任何"label 含'串口'就当作串口模式"的推断都会在文案改动时静默失效——guardrail A 因此写成禁令而不是建议。
3. **为什么 StackLayout 当前比 Loader 更适合**：B1 实测确认了设计假设——页面单实例、切换零副作用；而且 `currentIndex` 绑定 rail 后，"禁用项不能改 index"从 UI 细节升级为**页面存续的结构前提**（被护栏钉死）。Loader 的销毁语义会引入"重进页面重建实例/重连信号"的风险，收益（内存）在 5 个轻量页面下没有意义；guardrail B 保留未来为 heavy optional subview 单独设计的权利。
4. **StackLayout child 为什么用纯 Item page-root pattern**：本轮**实测两次**——anchors 于 layout-managed 子项 → 运行时 undefined behavior 警告（qml_smoke 抓到）；`Layout.margins` → 被 StackLayout 忽略、内缩丢失（dump 抓到）。结论：Layouts 拥有子项几何，页面只能"被摆放"；内缩/内容布局收回页面内部。B2–B5 每个 Page 复用此模式。
5. **compact rail 的空间预算依据**：56px 来自"最小窗 1000 − rail 56 − 页边距 32 = 912 ≥ 既有业务最低宽度 852（SplitView 300+520+2×16）"；实测 host=943 ≥ 852 ✓。expanded 档未实现；未来实现必须先算 available ≥ required（guardrail D，1060 只是当时预算值）。
6. **为什么 disabled future navigation 比假 Coming Soon 页面更安全**：假页面把"不存在的能力"呈现为"可进入但空"，用户会把它当作 bug 或半成品；禁用项把信息架构**预告**出来而不承诺——且护栏断言"disabled 项无论如何都无法改变 index"（含 invoke 返回值校验），使"不可用"成为受测事实而非视觉印象（guardrail C）。
7. **为什么 geometry test 仍不能替代人工视觉验收**：本轮自动链全绿时仍抓到 anchors 警告与 Layout.margins 失效——自动断言只能证明"我测量过的不变量成立"，无法覆盖"我没想到要测的观感"（截字、拥挤、层级混乱）。M9-A 与 B1 两次都是"自动 PASS + 人工裁决"才闭环；几何/像素断言锁合同，人工锁质量。
8. **如何复用 ISSUE-012 / ISSUE-011 的经验**：ISSUE-012（layout 尺寸合同）→ 直接催生本轮的页根模式与"Layouts 拥有几何"检查清单，并在 smoke 里第一时间识别出 anchors 警告属同族；ISSUE-011（部署模块漂移）→ 新增 NavigationRail 后**零额外部署工作**（生成模块整目录复制机制自动覆盖），验证了"单一机制"的复利。另有验证工具教训（M9-A 同类假阳性在 B1 复现）→ 统一"判定函数只返回 bool + 顶层裸语句打印 + 反射调用校验返回值"。

## 28. LKGC Decision（用户批准）

- **V2 verified LKGC = `189c62c`**（M9-B1 最后一个包含真实 product/QML changes 且通过 qml_smoke + qml_geometry_check + full ctest 25/25 + deploy smoke + **manual visual PASS** 的提交）。
- `b7e7d72` 为 docs-only 哈希回填，**不得作为 LKGC**。
- **V1 immutable tag `v1.0.0` → `ae067ab` 永久不变**（与 V2 LKGC 是两个概念）。

## 29. B1 Completion Commit

- `60709ae` — `M9-B1: complete application shell after visual acceptance`（docs-only，5 files：T017 §24–§29 + PROJECT_STATUS + BACKLOG + devlog + INTERVIEW_NOTES）。
- 本回填提交（docs-only）为哈希记录；两个提交均**不推进 LKGC、不 push**。
## 30. M9-B2 Phase 1 — Dashboard Boundary / Navigation Persistence（Learning & Design，2026-09-16）

**Task-document 决策（§20）**：**本设计 append 到 T017，不新建 T018**。依据仓库既有粒度惯例：T014（一个任务含 Phase A/B/C 三阶段）、T015（Phase A + Phase B + Part C 同档）、T016（M9-A 的 Phase 1 设计 + Phase 2 实施 + remediation 同档）；T017 已覆盖 M9-B 全链路（§13 明确列出 B1→B5 迁移计划），B1 亦已落在 T017。B2 是 M9-B 的第二阶段，**不构成独立任务**。Implementation = NOT STARTED。

### 30.1 Preflight（2026-09-16）

```text
pwd                           → /e/desktop/ModbusLens
git rev-parse --show-toplevel → E:/desktop/ModbusLens
git rev-parse --short HEAD    → 7e23245
git branch --show-current     → main
git status                    → clean
git diff --check              → pass (exit 0)
git log --oneline -10         → 7e23245 … 7abd887（见 §29 链）
git rev-list --left-right --count origin/main...main → 0  12（behind 0 / ahead 12）
V2 verified LKGC              → 189c62c（在链上）
V1 immutable tag              → v1.0.0 → ae067ab
```

### 30.2 现状取证（B1 实现复核，逐项真实代码）

| 事实 | 证据 |
| --- | --- |
| Rail 入口顺序与状态 | `NavigationRail.qml` entries：`[0]工作台 enabled`、`[1]总览 disabled`、`[2]通信`、`[3]回放`、`[4]诊断`、`[5]设备`（后四者 disabled） |
| 选中变更唯一路径 | `activate()`：`if (!navItem.enabled) return;` → 写 `rail.currentWorkspaceIndex`（鼠标/Enter/Space 共用） |
| Workspace host | `StackLayout { objectName: "workspaceHost"; currentIndex: navigationRail.currentWorkspaceIndex }`，当前唯一子项 = `Item { objectName: "legacyWorkspace" }`（页根模式） |
| Legacy 内容 | Demo controls（Run Demo + Load Replay）→ Replay error/notice → Serial GroupBox + error → Statistics 段（`statisticsPanel`/`statCard_*`/`statusCard_*`）→ 分隔线 → SplitView(诊断 | 事务) |
| AppBar | 产品名 + session/source 只读显示 + `appBarClearResults` |
| Run Demo 接线 | Main.qml:201 `onClicked: analysisController.runDemoBatch()` |
| Load Replay 接线 | Main.qml:206 `onClicked: replayFileDialog.open()` |
| Clear 接线 | Main.qml:135 `onClicked: analysisController.clearResults()` |
| 几何/导航护栏 | main.cpp `runGeometryAssertions`（shell + statistics）+ `runShellNavAssertions`（index 合法/legacy 可见/navItem_0 enabled/navItem_1 disabled/禁用项改不动 index）；`--qml-geometry-check` 双尺寸 |
| 测试总数 | ctest 25（含 qml_smoke、qml_geometry_check） |

### 30.3 Dashboard 产品边界（§2 比较）

| 维度 | A：Overview + Statistics | **B：Overview + Run Demo + Statistics（推荐）** | C：B + Recent Transactions | D：更大聚合页 |
| --- | --- | --- | --- | --- |
| User value | 只读；冷启动无动作入口 | 冷启动即可"看健康 + 一键进入演示会话" | 多一张表，但表的主场是 Communication | 内容多但任务不清 |
| Migration risk | 低 | 低（Run Demo 是现成命令的单点搬运） | 中（事务表 520 最小宽 + 双行 delegate 需同迁） | 高 |
| Duplication risk | 无 | 无 | **事务表在 Legacy 与 Dashboard 双呈现** | 高 |
| 1000×700 density | 空 | 松（见 §30.12 预算：余 300+px） | 紧张（表要 120+ 高） | 不可控 |
| M9-C 重构 | 简单 | 简单 | 需处理双表 | 大 |
| V1 behavior risk | 无 | 无（仅复用既有命令） | 行高/列宽（root-owned token）迁移风险 | 高 |

**结论：B**。用户建议候选（Run Demo + Statistics）经真实代码核对成立：两者都已是**单点实现 + 单点接线**，Dashboard 首版只做"聚合搬运"而不新建事实——这正是"第一个真实 page"最合适的范围。C 的表迁移留给 B3（Communication）与 M9-C/D；D 无对应真实用户任务，拒绝。

### 30.4 Run Demo Ownership（§3）

- **谁触发**：仅用户显式点击（现状 Main.qml:201；提取后为 Dashboard 页内按钮）。**导航本身绝不得调用**。
- **谁拥有**：`AnalysisController::runDemoBatch()`（cpp:1061-1209）。副作用全清单（真实代码）：① `teardownSerialTransport()`（先离开串口，cpp:1066-1068）② 重建确定性 `SimulatedSlave{0x01}`（寄存器 100/200/1500，cpp:1070-1074）③ 产出 4 笔固定事务（Success/Exception/CRC-Timeout，cpp:1103-1192）④ 原子发布（model + snapshot + diagnosis batch，cpp:1197-1201）⑤ `invalidateAiForBatchChange()` ⑥ 设 `modeLabel_=模拟器模式 / sourceLabel_=确定性演示` ⑦ 清 replay/serial 错误 ⑧ emit statisticsChanged + sourceChanged。
- **为什么属于 Dashboard contextual 而非 AppBar global**：判定准则是**是否切换 source**——`clearResults` 不换 source（r08/s08）⇒ session 级 ⇒ 归 AppBar；`runDemoBatch` **会**切换 source（并 teardown 串口）⇒ 是"进入演示会话"这一具体任务的入口 ⇒ 归承载该任务的工作区（Dashboard）。AppBar 只放"与来源无关的 session 动作 + 身份显示"。

### 30.5 Statistics Ownership（§4）

| UI Field | Controller property（真实） | 格式化 owner（现状） | 现 binding 位置 |
| --- | --- | --- | --- |
| 已观测 / 已完成 / 进行中 | `observedCount` / `completedCount` / `pendingCount` | 无（整数直显） | Main.qml Repeater model（statCard_0..2） |
| 成功率 | `hasSuccessRate` + `successRate` | Main.qml：`(successRate*100).toFixed(1)+"%"`，无值 → `qsTr("—")` | statCard_rate |
| 平均延迟 | `hasAverageSuccessLatency` + `averageSuccessLatencyMs` | Main.qml：`toFixed(1)+qsTr(" ms")`，无值 → `"—"` | statCard_latency |
| 成功/异常/CRC 错误/超时/协议错误/预期无响应 | `successCount` / `exceptionCount` / `crcErrorCount` / `timeoutCount` / `protocolErrorCount` / `expectedNoResponseCount` | 无（整数直显 + 各自 tone 色） | statusCard_0..5 |

**结论**：全部来自 Controller 的 `statistics_` 快照（header:33-45，NOTIFY statisticsChanged）；**Dashboard extraction 不得复制任何一项**——抽取的组件只是把同一条绑定链换个宿主。

### 30.6 双 Workspace 导航模型（§5）

- 现契约：`navItem_0` = 工作台（enabled），`navItem_1` = 总览（disabled）。
- **B2 决策：保持 index 语义只做 append**——`workingIndex0=工作台`、`index1=总览`**启位**（enabled），其余四个保持 disabled。理由：B1 的自动护栏直接断言 `navItem_0/navItem_1` 的 enabled 语义与 index 合法性；改序（把总览提到 0）会破坏"当前页默认选中"的过渡体验并需要同步改动护栏（§5 明确禁止"不改 guard 就改 index 语义"）。StackLayout 子项顺序**必须**与 rail index 一一对应：child0 = legacyWorkspace，child1 = dashboardWorkspace。
- 未来（B5 全部页面就位后）若要把"总览"抬到首位，属显式的**重排任务**：须同时改 rail、StackLayout 与全部 guard 断言，并在任务文档中作为独立决策记录。

### 30.7 Navigation vs Source 不变量（§6）

**点击 Dashboard（激活 index 1）只允许改变：`rail.currentWorkspaceIndex` 与 `StackLayout.currentIndex`（呈现层）。**

不得改变：`modeLabel` / `sourceLabel` / `serialConnected` / `transactionModel` 行数与内容 / `statistics_` 全部计数与率 / 诊断结果与 `hasBaselineDiagnosis` / AI 结果与错误 / Agent 答案与状态。

不得调用：`runDemoBatch` / `loadReplayFile` / `connectSerial` / `clearResults`。业务命令只允许由**页面内的显式用户动作**触发（Run Demo 按钮、Load Replay 按钮……）。该不变量以 `qml_nav_check` 断言形式落地（§30.8）。

### 30.8 状态存续场景与 qml_nav_check 设计（§7/§8）

| 场景 | 步骤（全部 offline、确定性） | 断言 |
| --- | --- | --- |
| **A** | invoke `runDemoBatch()` → 记录 observed/completed/success 等 → `navItem_1.activate()`（切 Dashboard）→ `navItem_0.activate()`（回 Legacy） | 切换前后全部统计值逐项相等（演示批次是确定性的） |
| **B** | invoke `runBaselineDiagnosis()` → 记录 `hasBaselineDiagnosis` + `baselineDiagnosisText` → 来回切换 | 文本与标志逐字不变 |
| **C（serial/session）** | 需要真实串口或传输层 fake；当前 QML 层无可用 seam（`publishSerialResult` 是 C++ 测试 seam，不经 QML） | **DEFER（不造假）**：明确记录"offline 不可可靠验证，留待有硬件或专门 seam 时补"，由人工验收覆盖连接态显示 |
| **D（Clear）** | invoke `clearResults()` → 记录 → 切换 | 两页同时归零/占位（同一 authoritative state） |

**`qml_nav_check` 设计**（新 CLI `--qml-nav-check` + 新 ctest target；与只读的 geometry check 分离——nav check 会**写状态**（invoke 命令），二者失败语义不同）：

- A. 两个 page object 同时存在：`legacyWorkspace` 与 `dashboardWorkspace` 均能按 objectName 找到（StackLayout 全实例化）。
- B. 切换 0 → 1 → 0（走真实 `activate()` 路径）。
- C. **对象身份稳定**：切换前后保存的 `QQuickItem*` 指针相等（页面未被重建）。
- D. `currentWorkspaceIndex` 与 `StackLayout.currentIndex` 始终一致且正确。
- E. 可见性正确：index0 → legacy visible / dashboard hidden；index1 反之。
- F. 禁用未来项（navItem_2..5）invoke `activate()` 后 index 不变（沿用 B1 的 invoke 返回值校验，防空洞通过）。
- G. 业务值不变（场景 A/B/D 的断言集）。
- **身份匿名的边界说明**：C 只能证明**生命周期稳定**（对象没被销毁重建 → 页面本地状态不丢、无重复初始化副作用），**不能**证明业务状态正确——业务正确性必须由 G 的逐值断言（走 Controller authoritative properties）独立证明。两者是"结构证据 + 状态证据"，缺一不可。

### 30.9 DashboardPage 组件与依赖方式（§9）

- 形态：`src/ui/qml/pages/DashboardPage.qml`（新 `pages/` 目录，§12 分解图既定），根为**纯 Item**（页根模式，B1 教训），`objectName: "dashboardWorkspace"`。只负责 layout / bindings / 用户意图；不创建第二份 Controller、不复制 statistics、不拥有 simulator、不自行切 source。
- 依赖方式比较：

| 维度 | A′：注入 Controller 引用（推荐） | B：page 暴露 signal，由 shell 接线 |
| --- | --- | --- |
| coupling | 显式依赖（`property AnalysisController controller`，由 shell 传入实例；无全局、无第二实例） | 动作解耦但**显示仍必须**拿引用 → 双机制并存 |
| testability | 与现状一致（CLI 按 objectName 找到页面，controller 由 shell 注入） | 动作路径经 shell 转发，grep 调用点变难 |
| boilerplate | 最低（直接绑定 + 直接调用 onClicked） | 每个动作一条 shell 转发线 |
| 项目规模适配 | 契合（Legacy 的按钮本来就是直接调用） | 过度设计 |
| B3–B5 一致性 | 全部 page 同一模式 | 两种模式混用 |

**推荐 A′**：显示用绑定、动作用 `onClicked → controller.<command>`（与 Legacy 一致、可在 grep 中看到全部调用点）。**否决 B 的理由**：显示绑定无论如何都要 controller 引用，signal 只覆盖"动作"半边，造成双机制；且转发层会把"谁在调用业务命令"从代码里藏起来。**红线**：任何 `controller.*` 调用不得出现在 `Component.onCompleted` / 可见性变化 / `currentIndex` 变化路径中（§30.7）。

### 30.10 Statistics 复用方案（§10）

| 方案 | migration safety | duplication | visual consistency | rollback | M9-C |
| --- | --- | --- | --- | --- | --- |
| A：统计直接 MOVE 到 Dashboard，Legacy 不再显示 | 低（Legacy 出现大片空洞） | 无 | 单点 | 回滚=恢复大段 markup | 好 |
| **B：抽 `StatisticsOverview.qml`，迁移期 Legacy + Dashboard 共用（推荐）** | **高**（Legacy 视觉零变化，Dashboard 增量出现） | 无 | 单点 | 回滚=换回内联/改回单实例 | 好（一处改两处生效） |
| C：临时复制 markup | 低 | **有**（违反 §30.5 精神：同一事实两份渲染代码，日后改一处漏一处） | 漂移风险 | 差 | 差 |

**推荐 B**。提取时组件化两条硬约束：① 组件持有**全部 11 卡与格式化逻辑**（toFixed(1) / "%" / " ms" / "—" 逐字保持），消费注入的 controller 引用；② **objectName 必须可按实例命名**——迁移期两个实例同时存在（StackLayout 全实例化），现有 guard 的 `statisticsPanel` / `statCard_*` 名字会重复，因此组件暴露 `instanceId`（如 `stats_legacy` / `stats_dashboard`），objectName 形如 `statisticsPanel_stats_dashboard`；guard 改为按后缀定位**每个实例**各自的卡。

### 30.11 Legacy Workspace after B2（§11）

B2 之后 Legacy 内容：Load Replay（控件行唯一按钮）→ Replay error/notice → Serial GroupBox + error → **StatisticsOverview（共用组件，位置与视觉不变）** → 分隔线 → SplitView(诊断 | 事务)。Run Demo 迁出（其唯一合法入口变为 Dashboard）。
- 空洞问题：控件行只剩一个"加载回放..."按钮——**保留该行**（B4 会把整个 Replay 流程收拢到 Replay 页），B2 不做视觉重设计，只在注释与文档中标注"过渡期单按钮行"。

### 30.12 Dashboard 布局预算（§14）

```text
窗口 1000×700：host 高 659（B1 实测）− 页内缩 32 = 627 可用高
Dashboard 内容：页标题 21 + 12 + Run Demo 行 34 + 12 + StatisticsOverview（标题 21 + 12 + 面板 168 = 201）
             ≈ 280  →  余量 ≈ 347px（无需滚动）
窗口 1024×720：host 679 − 32 = 647 可用 → 余量 ≈ 367px
宽度：host 943/967 − 页内缩 32 → 页内容 911/935；统计两行实测 887/911 ≥ 隐式宽 840/732 ✓
```
**结论**：两尺寸均无需滚动；余量记录在案，**不添加填充性 widget**（§14 明令）。

### 30.13 Empty / No-Data 状态（§12）

现状：未跑任何来源时显示 `Observed 0 / Success rate — / Avg latency —`——**数字本身是诚实的**（"没有数据"而非"失败"），保持。设计（不实现）：Dashboard 首版加**一行轻量引导文案**，条件 `visible: analysisController.observedCount === 0`，内容含两个合法入口名（"运行演示批次" / "加载回放日志"）。**不新建 EmptyState 组件**（M9-C 候选；当前唯一使用点，先内联一行 Label）。Legacy 同期不加（它还有 Serial 等入口，语义不同）。

### 30.14 Clear Results 行为验收（§13）

`clearResults()` 清空 `statistics_` → 两页（同一 authoritative 绑定）同时归零、率/延迟回 `—`。验收点：
- 自动：nav check 场景 D（在 Dashboard 激活状态下 invoke → 断言全零 + `hasSuccessRate==false` + `hasAverageSuccessLatency==false`）。
- 人工：在 Dashboard 点 AppBar 清空 → 本页立即归零；切到 Legacy → 同样归零（证明无第二份 state）。

### 30.15 Accessibility / Focus（§15）

- `总览` 启位后：mouse / Enter / Space 均经 `activate()`（与 B1 同路径）；选中态沿用三通道（底 + 主色条 + 加粗），**不依赖颜色单通道**。
- Run Demo 按钮用既有 `AppButton`（`Qt.StrongFocus` 保持）。
- **切页后焦点策略：A —— 焦点留在被激活的 rail 项**。理由：焦点跟随用户最后操作的控件是桌面惯例；自动把焦点扔进页面（B）会在用户只想"看一眼"时抢走键盘上下文，且页面首个可聚焦控件并不总是用户意图（Dashboard 的 Run Demo 是破坏性不大的动作，但同类假设在 B3 的 Serial 页会直接有风险）。页面内容只在用户显式 Tab 之后接管焦点。无理由不抢焦点。

### 30.16 V1 / M9 契约风险清单（§16）

| 契约 | 真实证据 | B2 红线 |
| --- | --- | --- |
| runDemo 行为 | cpp:1061-1209 + `b01_runDemoStatistics`/`b02`/`b04_deterministicReRun`/`b06` | 只搬按钮位置；命令零改动；导航不得触发 |
| source switching 原子律 | `r03`/`s02`/`d04`/`ai07`（失败不动旧状态） | 不受影响；但 Run Demo 按钮新位置必须保持"点击才切换" |
| statistics 绑定 | `a01`-`a06`、STAT-*；Main.qml 382-460 现绑定 | 逐字段、逐格式搬运；禁止复制 state |
| clearResults | `r08_clearKeepsSource`、`s08`、`d08`；cpp:1211-1224 | 留在 AppBar 不动 |
| 诊断存续 | `d02_clearResultsInvalidates`/`d03_newBatchInvalidates`/`d05`/`d06` | 导航不得清诊断；场景 B 断言 |
| AI/Agent 存续 | `ai06`/`ai08`/`ai10` | 导航不得触碰（B2 不涉及，护栏不回归） |
| TransactionModel | `a03`-`a06`、`t01`-`t05` | B2 不动事务表 |
| M9-A 统计视觉 | ISSUE-012 修复链 + `qml_geometry_check` 统计断言 | 提取后断言必须**按实例名**继续全过；不得删断言 |
| M9-B1 shell 几何 | `qml_geometry_check` shell 段（appBar/rail/host/≥852） | 新页必须满足 host 预算；导航断言不得弱化 |
| 禁用导航守卫 | main.cpp `runShellNavAssertions`（含 invoke 校验） | navItem_2..5 保持禁用断言；新增 index1 合法断言 |
| 最小窗口 | 1000×700（T013 Policy）+ SplitView min 300/520 | Dashboard 预算按 §30.12；不许引入滚动依赖 |

### 30.17 B2 增量实施计划（§17）

> 护栏先行、每步可构建/测试/回滚；不得一次搬完再调试。

- **B2.1 — StatisticsOverview 提取（视觉零变化）**：新建 `components/StatisticsOverview.qml`（instanceId + 全 11 卡 + 格式化）；Legacy 改用该组件（`instanceId: "legacy"`）；`qml_geometry_check` 的统计断言改为实例名寻址。验证：ctest 25/25（总数不变）+ 截图对比无变化。
- **B2.2 — DashboardPage + 第二 workspace（结构步）**：新建 `pages/DashboardPage.qml`（页标题 + Run Demo + StatisticsOverview(`instanceId: "dashboard"`) + 空态引导行）；rail `index1` 启位；StackLayout 增 child1；**删除 Legacy 的 Run Demo 按钮**（避免双入口）；新增 `--qml-nav-check` + ctest `qml_nav_check`（结构断言 A–F）。验证：ctest **26**（如实报告）+ 双 smoke + 人工。
- **B2.3 — 状态存续断言（行为步）**：nav check 增加场景 A/B/D 的逐值断言（含 clear 归零）。验证：ctest 26/26。
- **B2.4 — 收尾**：Legacy 注释/文档更新（单按钮行说明）+ 截图 + 人工验收包（§30.18 清单）。
- 每步独立提交；回滚 = revert 单提交。

### 30.18 Test / Acceptance Plan（§18）

- **Baseline**：ctest 25（qml_smoke + qml_geometry_check 全保留）。
- **新增**：`qml_nav_check`（新 ctest target，**总数 25 → 26，如实报告**）；geometry check 断言按实例名扩写（不删旧断言）。
- **覆盖**：两真实 workspace 并存 / 0→1→0 / 页面身份稳定 / index 与可见性 / 禁用项不可切页（invoke 校验）/ 统计与诊断逐值存续 / clear 归零。
- **人工**（1000×700 与 1024×720）：Dashboard 导航与选中态 / Legacy 导航 / Run Demo（从 Dashboard 跑出确定性批次）/ Statistics 两页一致 / Clear 两页同步归零 / 诊断与 AI 结果切页存续 / Transactions 与 Serial 区域可见性 / 禁用入口不误导 / resize 稳定。

### 30.19 Knowledge Before Implementation（§19，8 项，各自对应真实问题）

| # | 知识点 | 解决 ModbusLens 的哪个真实问题 |
| --- | --- | --- |
| 1 | View duplication vs state duplication | 迁移期最危险的不是"两处显示统计"，而是"两处各存一份统计"——本设计用共享组件 + 单一 Controller 快照把这个坑封死 |
| 2 | Page ownership | 页面的所有权边界=layout/bindings/用户意图；Run Demo 这类 source 切换命令只能被"页面内的显式动作"触发，不能被"进入页面"触发 |
| 3 | Page signal vs direct Controller binding | 显示必须绑 Controller，动作若再走 signal 就形成双机制与隐藏调用点——选注入引用 + 直接 onClicked（可 grep、可测） |
| 4 | Navigation invariant | "切页只改呈现"必须成为可断言事实（nav check），否则未来重排/加页时静默违约 |
| 5 | Object lifetime vs business state | 对象身份不变只证明生命周期；业务正确性要另证——两类证据分离是 nav check 的设计骨架 |
| 6 | Migration compatibility | Legacy 在 B2–B4 期间必须继续可用且视觉不塌：统计共用组件（方案 B）是对"兼容期"问题的直接回答 |
| 7 | Empty state | "0 / —"已经诚实；真正缺的是"接下来做什么"的引导——一行条件文案即可，不为一个使用点造组件 |
| 8 | Dashboard information hierarchy | 首版层级 = 会话健康（统计）> 入口动作（Run Demo）> 引导；不堆填充物，700 高下的松是特性不是缺陷 |

### 30.20 Status

- **M9-B2 Phase 1 = Learning / Design 完成（docs-only）；Implementation = NOT STARTED。**
- 下一步：**B2 Phase 1 Review（用户）**；批准后按 §30.17 从 B2.1 开始，每步单独 Review 与提交。


## 31. M9-B2 Phase 2 — Dashboard Extraction Implementation

### 31.0 Phase 1 Review = PASS（用户，2026-09-16）+ Implementation Guardrails

用户批准进入 B2 Phase 2，并追加约束（本文档为 authority）：

- **A. StatisticsOverview 是 ModbusLens feature/presentation component**：可复用，但**不是 Design System primitive**——不得放入 `DS/`，不得把 Modbus 业务语义（事务状态、成功率口径）塞进 DesignSystem。
- **B. DashboardPage 使用显式注入的现有 AnalysisController 引用**：允许读 authoritative properties、调用既有明确 user-action command（如 `runDemoBatch()`）；**不得**创建第二 Controller、复制业务状态、自行实现 source transition、依赖隐藏字符串解析。
- **C. workspace index source of truth**：Legacy/Dashboard index 由**一处 presentation-level mapping** 集中定义（Main.qml readonly properties）；不得在多个文件散落裸 `0/1`；测试必须消费同一契约（读 root properties）或通过稳定 objectName/UI API 获取。
- **D. Dashboard empty-state 文案**：B2 时 Replay workspace 尚未启用——不得暗示可通过 disabled Replay nav 直接加载文件；允许 "暂无通信数据。可运行演示批次，或在工作台加载回放日志。"；**不得制造不可达操作**。

### 31.1 B2.1 — StatisticsOverview Extraction

- 新建 `src/ui/qml/components/StatisticsOverview.qml`（feature component，非 DS primitive）：
  - `required property var analysisController`（显式依赖）+ `required property string instanceId`（presentation identity，如 `stats_legacy`）；
  - 封装已验收的 SectionHeader + PanelCard + 11 卡与全部格式化语义（`toFixed(1)+"%"` / `+" ms"` / `"—"` 占位 / 各 status label / semantic tone 逐字保持）；
  - objectName 由 `instanceId` 派生：`statisticsHeader_<id>` / `statisticsPanel_<id>` / `statisticsRow1_<id>` / `statisticsRow2_<id>` / `statCard_<n>_<id>` / `statusCard_<n>_<id>`（避免"第一个同名对象"式查找）；
  - **零状态**：不存副本、不算 successRate、不定义分母、不解释 ExpectedNoResponse。
- Legacy 改用组件（`instanceId: "legacy"`），视觉与数值零变化；`qml_geometry_check` 统计断言改为**按活动实例寻址**（不再裸名），并按 §20 只在相应 workspace 激活时验证其活动统计实例。
- CMakeLists QML_FILES 注册（沿用 qt_add_qml_module；无手写 qmldir）。

### 31.2 B2.2 — DashboardPage + 第二真实 Workspace

- 新建 `src/ui/qml/pages/DashboardPage.qml`：页根为**纯 Item**（StackLayout 直接子项，页根模式），内部 ColumnLayout（margins 16）再处理布局；只含 page heading + Run Demo 动作 + 轻量 no-data hint + StatisticsOverview(`instanceId: "dashboard"`)；**不含** Recent Transactions/Charts/Diagnosis/AI/Device/Replay 控件。
- `Main.qml`：新增 workspace index 契约（readonly properties `workspaceLegacyIndex: 0` / `workspaceDashboardIndex: 1`，集中定义、带注释指向 rail 顺序与测试）；`AnalysisController` 增加 `objectName`（测试按名取权威对象，不改 Controller 代码）；StackLayout 增加 child1 = DashboardPage(`objectName: "dashboardWorkspace"`)；**Run Demo 迁至 Dashboard并删除 Legacy 的按钮**（杜绝双入口）；Load Replay 留 Legacy、Clear Results 留 AppBar。
- `NavigationRail.qml`：entries 中 `总览` 由 disabled 改为 enabled（顺序不变；工作台=0 保持默认选中）。
- `main.cpp`：`--qml-geometry-check` 改为**四趟测量**（默认尺寸 legacy / 默认尺寸 dashboard / 1000×700 dashboard / 1000×700 legacy），断言对象随"当前可见 workspace"选择（隐藏页不做脆弱断言，§20）；新增 `--qml-nav-check` 模式 + ctest `qml_nav_check`（**测试总数 25 → 26**）。
- nav check 结构断言（A–F）：两页并存 / 初始 index 合法 / 0→Dashboard→Legacy / **page object 身份指针不变** / 可见性与选中一致 / 禁用项 invoke activate 后 index 不变（沿用 invoke 返回值校验）；并含"导航不改变任何业务值"的全字段快照比较（即 §15-G）。

### 31.3 B2.3 — Persistence Scenarios

- **Scenario A**：invoke `runDemoBatch()` → 记录 *authoritative snapshot*（modeLabel/sourceLabel + 11 统计值 + hasSuccessRate/successRate + hasAverageSuccessLatency/averageSuccessLatencyMs）→ Dashboard→Legacy 每次切换后**逐值相等**（runDemoBatch 本身允许改 source；断言的是**导航之后**不再变化）。
- **Scenario B**：在确定性批次上 invoke `runBaselineDiagnosis()` → 记录 `hasBaselineDiagnosis` + `baselineDiagnosisText` → 往返后不变（用最 authoritative 可稳定比较的 property；不为测试改 Controller）。
- **Scenario D（Clear）**：invoke `clearResults()` → 断言全部计数 0 + `hasSuccessRate==false` + `hasAverageSuccessLatency==false`，切换后同值（两 View 消费同一 state）。
- **Serial 场景：继续 DEFER**（无硬件、offline seam 不足；不新增 fake serial 行为只为导航测试）；Serial Controls 的可达性由人工验收覆盖（§18）。

### 31.4 B2.4 — Deploy / Manual Candidate

- 四张真值截图（两尺寸 × 两页）：`m9b2-legacy-1024x720.png` / `m9b2-dashboard-1024x720.png` / `m9b2-dashboard-1000x700.png` / `m9b2-legacy-1000x700.png`（grabWindow；M9-A/B1 截图不覆盖）。
- deploy 重建 + 无 PATH smoke；Manual 方案 = PENDING USER REVIEW（清单见 §25）。

### 31.5 Verification（最终，真命令 + 真输出）

```text
B2.1 gate：build → qml_smoke → qml_geometry_check（实例化寻址）→ full ctest 25/25 → git diff --check → 视觉零变化确认
B2.2 gate：build → qml_smoke → qml_geometry_check（四趟）→ qml_nav_check（结构）→ full ctest 26/26
B2.3 gate：nav check 场景 A/B/D 逐值断言全绿 → ctest 26/26
最终：deploy_windows.bat [OK] + 无 PATH deploy smoke EXITCODE=0 + 四张截图 + 像素自检
```

（各步真实输出与截图路径在执行时回填于本节下方——见 §31.6。）

### 31.6 Results（执行回填，真实输出）

- **B2.1（`c4291db`）**：`StatisticsOverview.qml` 落地、Legacy 替换为零视觉变化；`qml_smoke` EXITCODE=0；`qml_geometry_check` 双尺寸 PASS（实例化寻址 `statisticsPanel_legacy`=935×168 / 911×168，与 M9-A 一致）；full ctest **25/25**；`git diff --check` 通过；像素验证对齐 B1 区域后与 B1 计数**逐项相同**（158/234/744/40/59/30 + 每卡 dark 计数）——视觉零变化是证明出来的，不是假定。
- **B2.2（`93aabb2`）**：DashboardPage + index 契约 + 总览启位 + Run Demo 迁移；几何四趟：`DEFAULT legacy`（panel_legacy 935×168）→ `DEFAULT dashboard`（panel_dashboard 935×168）→ `MIN 1000x700 dashboard`（943×659, panel 911×168）→ `MIN 1000x700 legacy`（911×168），全 PASS；`--qml-nav-check` 结构断言 PASS（`index 0→1→0`，可见性随选中）；full ctest **26/26**（新增 `qml_nav_check`，如实报告 25→26）。
- **B2.3（`53685d5`）**：场景 A/B/D 全绿——`NAV [scenario A @dashboard]: observed=4 mode=模拟器模式 source=确定性演示`（全字段快照跨两跳不变）；场景 B（`hasBaselineDiagnosis`+文本双跳存续，静默通过）；`NAV [scenario D @dashboard]: observed=0 hasRate=0 hasLatency=0`（clear 后两页同态）；场景 C 维持 DEFER（无硬件，不造假）。
- **B2.4**：四张真值截图（`docs/assets/screenshots/m9b2-{legacy,dashboard}-{1024x720,1000x700}.png`）；像素自检四图全 PASS，且 **rail 选中 accent 的数字即证据**（legacy 图 item0=40/item1=0；dashboard 图 item0=0/item1=40——选中态随页面移动）；deploy 重建 `[OK]`、无 PATH deploy smoke EXITCODE=0、**部署版 exe 的 `--qml-nav-check` 同样 PASS**。

### 31.7 Problems / RCA

0. **验证工具第三次假阳性（本次最耗时的一课）**：B2.1 首轮像素验证出现"大面积 FAIL"，追查（ASCII 图 → 包围盒 → 逐区域计数 → 同图并排实现）后确认：`ps_pixel_verify.ps1` 的路径覆盖行在派生时**没有生效**，脚本一直在读 `docs/assets/screenshots/geometry-1024x720.png`（**M9-A 时期的旧截图**）做 B1 区域的测量——FAIL 全是测量工件。修复（按行号重写路径）+ 复查后，同一脚本对同图给出与 B1 完全一致的计数。**教训：检查器的输入必须被锁定并核对（路径+哈希），否则"红"和"绿"都不可信。**
1. **截图 dump 与测量趟次耦合**：四趟测量下，旧的两张 dump 命名（按尺寸）会互相覆盖——改为**按"页 × 尺寸"命名**（`m9b2-<page>-<size>.png`），命名即语义，避免用后写的图覆盖先写的图。
2. **测量时序（四趟引入的新坑）**：初版在同一回调里"切页/缩放 → 立刻 dump+断言"，导致 dashboard 趟读到隐式宽 864（未 settle）——修复为 `transition → settle → measure` 的状态机（切页或缩放后先经 100ms 再测量）。另确认：**从未激活过的隐藏页几何为 0×0**（首次激活后才获得尺寸）——这从数据上验证了"隐藏页不做断言"的规则是必要的而非教条。
3. **测试对 index 的依赖方式**：test 不再硬编码"dashboard=1"，而是**读取 root 上的契约 properties**（`workspaceDashboardIndex`/`workspaceLegacyIndex`）——契约改动时测试自动跟随；若 rail 顺序与契约漂移，行为断言（激活后 dashboard 必须可见）会失败。三方（shell 常量 / rail 顺序 / 测试）由一个行为测试钉住。
4. **隐藏页断言纪律**（B2 预研结论落地）：StackLayout 隐藏子项的可见性/几何不做假设式断言，改为"切到目标 workspace 再验证其活动实例"（§20 要求）。
5. **既有工具假阳性纪律沿用**：nav check 的 invoke 调用仍检查返回值；比较用全字段快照而非抽样。

### 31.8 Manual Review

- 四张截图（§31.4）+ deploy 候选；清单 13 项（两页切换 / selected 正确 / 四项仍 disabled / Run Demo 可用 / source chip 更新 / 两页统计一致 / Clear 同步归零 / 诊断往返不丢 / Transactions 不丢 / Serial Controls 可达 / 无裁切重叠 / 1000×700 可用）。
- 状态：**PENDING USER REVIEW**；**自动 PASS 不推进 LKGC**。

### 31.9 Knowledge Learned（结合真实实现）

1. **view duplication vs state duplication**：Legacy 与 Dashboard 各有一个 StatisticsOverview 实例（两处 markup、两处绑定），但**零状态副本**——两者读同一 Controller 快照。判定法：删掉任一视图，另一视图与全部业务行为不受影响；反之，若某处存了副本，删视图会连带丢状态。
2. **feature component vs design-system component**：`StatisticsOverview` 含 Modbus 业务词汇（事务状态语义、成功率口径），归 `components/` 作为 feature 组合件；`DS/` 只放与业务无关的 primitive/token（guardrail A 的边界即"语义归属"，不是"被复用次数"）。
3. **explicit Controller dependency**：`required property var analysisController` + `required property string instanceId` 让依赖与身份都在组件签名上可见、可测；页面/组件不解析字符串、不查全局、不建第二份。
4. **workspace index contract**：常量集中在 Main.qml，rail 顺序以注释指向它，测试从 root 读同一属性——"三处一致"由行为断言（激活 X 后 Y 可见）兜底，而不是靠三处各自写 0/1。
5. **object lifetime vs authoritative state**：nav check 同时证明两件事——身份指针跨切换不变（生命周期/无重建）与全字段快照不变（业务状态）——并把它们作为**两类独立断言**，避免用身份证据冒充状态证据。

### 31.10 Potential Interview Questions

1. 迁移期怎么做到"两处显示、一份状态"？——共享 feature 组件 + 单一 Controller 快照 + instanceId 命名；断言全集在 nav check。
2. 为什么导航测试要读契约属性而不是硬编码 index？——把"index 语义"变成一处定义、三处消费、行为兜底；重排时测试随契约走，rail 漂移时行为断言立刻红。
3. 为什么 StatisticsOverview 不进 DS/？——DS 是业务无关原语；组件语义含 Modbus 术语（成功率分母、状态名），放 DS 会把业务概念污染进设计系统。
4. 隐藏页为什么不做几何断言？——StackLayout 隐藏子项的几何/可见性不构成产品契约；断言只对"用户此刻看得到的页面"生效（§20）。

### 31.11 Candidate Commit

- B2 实施提交：`c4291db`（B2.1）、`93aabb2`（B2.2）、`53685d5`（B2.3）；候选提交 `387dcaf`（B2.4：文档 + 四张截图）。
- 全部**不 push、不推进 LKGC**（verified 保持 `189c62c`，待人工视觉 PASS 后再议）。

## 32. Next

- **M9-B2 Manual Visual Review = PENDING USER REVIEW**（candidate HEAD `387dcaf`；四张截图 + deploy 候选；清单 §31.8）。随后的哈希回填（docs-only）为其后一提交，不作 LKGC。
- PASS 之前：不推进 LKGC（保持 `189c62c`）、不 push、不开始 B3（Communication extraction）。
## 33. M9-B2 Completion / Archive（2026-09-16）

### 33.1 Manual Visual Review = PASS（用户）

用户已人工复核真实界面（Dashboard 1024×720、Legacy Workbench 1024×720），确认：

**Dashboard**：总览选中态正确；工作台仍可进入；Communication / Replay / Diagnosis / Device 仍明显 disabled；Run Demo 位于 Dashboard；session/source AppBar 信息正常；Statistics 无裁切、无重叠；M9-A statistics visual contract 保持。

**Legacy**：Run Demo 已移除、**无重复入口**；Load Replay 仍可用；Serial Controls 完整；Statistics 与 Dashboard 一致；Diagnosis 完整；Transactions 完整；无迁移导致的明显布局空洞或回归。

**行为链确认**：workspace navigation 只改变 presentation view，**不产生额外 source transition**。

### 33.2 PASS 边界（明确声明）

本 PASS 表示 **M9-B2 Dashboard Extraction 的功能、导航、状态保持与当前视觉达到本阶段验收要求**；**不表示** Dashboard 最终视觉设计完成。当前 Dashboard 较大的空白空间属 **M9-C Dashboard Redesign** 的未来可利用区域，**不是 B2 regression**；closure 阶段不得为了填满空白新增 widget。

### 33.3 LKGC 候选的 Git 证据裁定（原样命令）

```text
$ git show --stat --oneline 53685d5
53685d5 M9-B2.3: nav check persistence scenarios (A/B/D)
 src/main.cpp | 140 ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++-
 1 file changed, 138 insertions(+), 2 deletions(-)

$ git show --stat --oneline 387dcaf
387dcaf M9-B2.4: dashboard extraction candidate (docs + visual evidence)
 docs/BACKLOG.md                     |   3 ++-
 docs/INTERVIEW_NOTES.md             |   7 +++++++
 docs/PROJECT_STATUS.md              |  16 +++++++++-------
 docs/assets/screenshots/m9b2-*.png  | Bin ×4
 docs/devlog/2026-09-16-m9b2-phase2.md | 8 ++++++++
 docs/tasks/T017-...md               | 21 ++++++++++++---------
 9 files changed, 38 insertions(+), 17 deletions(-)
```

**分类**：`53685d5` = production/test **behavior-bearing**（唯一改动文件 `src/main.cpp`，即 `--qml-nav-check` 的真实行为——场景 A/B/D 断言）；`387dcaf` = **docs/screenshots/evidence-only**（零 src/tests/QML/CMake/scripts 改动）。

**裁定**：按 §3 规则（最后一个包含真实 product/QML/test behavior change 且处于本轮终验范围内的提交）→ **V2 verified LKGC 推进 `189c62c` → `53685d5`**。`387dcaf`（candidate 命名）与 `dd8bd12`（回填）均为 docs/evidence-only，**不得作为 LKGC**——命名不构成证据。终验范围覆盖 `53685d5` 的行为：最终 `qml_nav_check` PASS（场景断言即该提交实现）、ctest 26/26、部署版 nav check PASS 均为其验证证据。

### 33.4 B2 Final Status = COMPLETE

**交付清单**：StatisticsOverview feature component · DashboardPage · Run Demo migration（单入口）· two real workspaces · Dashboard navigation enabled · single workspace index contract · `qml_nav_check` · Scenario A（导航存续）· Scenario B（诊断存续）· Scenario D（clear 双视图同步）· dual-view authoritative statistics · dual-size geometry protection（四趟）· deploy validation · **manual visual PASS**。

提交链：`c4291db`（B2.1）→ `93aabb2`（B2.2）→ `53685d5`（B2.3，**= verified LKGC**）→ `387dcaf`（候选：docs+截图）→ `dd8bd12`（回填）→ `004e0a0`（closure，docs-only，非 LKGC）。

### 33.5 架构决策（已验证事实，正式记录）

1. **Workspace navigation ≠ Source transition**：切页仅改 `currentWorkspaceIndex`/`currentIndex`；业务命令只由页面内显式用户动作触发（nav check 的全字段快照断言 + 人工行为链确认共同证明）。
2. **Legacy 与 Dashboard 是两个 View，共享同一个 authoritative Controller state**：场景 D 在 Dashboard 断言与 Workbench 同态；场景 A 全字段跨页相等。
3. **StatisticsOverview 是 feature presentation component，不是 Design System primitive**：含 Modbus 业务词汇，归 `components/`；`DS/` 保持业务无关（guardrail A）。
4. **StackLayout 中两页面同时存在、页面 identity 稳定**：nav check 以指针相等证明（切换前后同对象）。
5. **Object lifetime evidence 与 business-state evidence 必须分别验证**：身份（生命周期）与全字段快照（业务正确性）是两类独立断言，互不替代。
6. **Serial persistence 自动测试仍 DEFER**：无硬件、无可靠 offline seam——**不得把未验证内容写成已验证**；Serial Controls 可达性由人工 PASS 覆盖。

### 33.6 Deferred Work（不得在 B2 closure 提前实现）

M9-B3 Communication Extraction · M9-B4 Replay Extraction · M9-B5 Diagnosis Extraction · M9-C Dashboard Redesign / visual enrichment（含利用 Dashboard 空白的视觉丰富化）。

### 33.7 最终验证记录（终验结论，不因 closure 重新宣称）

| 验证项 | 结果 |
| --- | --- |
| qml_smoke | **PASS**（EXITCODE=0） |
| qml_geometry_check | **PASS**（Legacy/Dashboard × 1024×720/1000×700 四趟） |
| qml_nav_check | **PASS**（结构 + 场景 A/B/D） |
| full ctest | **26/26** |
| deploy smoke | **PASS**（含部署版 nav check） |
| manual visual | **PASS**（用户，Dashboard + Legacy） |

本 closure 为 docs/status-only，**不产生新的产品验证结果**——上表即为 `53685d5` 行为的终验证据。

### 33.8 B2 Knowledge Closure（基于本轮真实过程）

1. **View duplication vs state duplication**：Legacy/Dashboard 各有一个 StatisticsOverview 实例（两份 markup、两份绑定），但零状态副本——两处渲染同一 Controller 快照。判定法：删掉任一视图，另一视图与全部业务行为不受影响；反之若某处存了副本，删视图会连带丢状态（场景 A/D 即为该判定的机器证据）。
2. **Feature component vs generic DS component**：归属看**语义**不看复用次数——StatisticsOverview 携带事务状态名、成功率口径等 Modbus 词汇 → feature 层；DS 只放业务无关 primitive/token。
3. **Workspace navigation vs source transition**：两者是不同维度的状态变更；把"切页"误当"切源"会引入静默的 source 变化（本项目用快照断言把这条不变量钉死，人工行为链复核确认）。
4. **Explicit Controller dependency**：`required property var analysisController` 让依赖出现在组件签名上（可静态检查、可测试注入），并且**不引入全局单例、不做字符串解析**。
5. **Workspace index contract**：一处定义（Main.qml readonly properties）→ rail 顺序 + StackLayout 顺序 + 测试三方消费；行为断言（激活后目标页必须可见）兜底顺序漂移。
6. **Object identity vs authoritative-state verification**：身份证据（指针相等）证明"没重建/没丢页本地状态"；状态证据（全字段快照）证明"业务值没变"。两类证据分开写、分别失败——避免用弱证据冒充强结论。
7. **隐藏 StackLayout 页面不应做未经验证的 geometry 假设**：本轮实测**从未激活的隐藏页几何为 0×0**，首次激活后才获得尺寸；因此几何断言一律"先切到目标 workspace 再验证其活动实例"，隐藏页零断言。
8. **verification oracle itself must have verifiable inputs**：本轮真实问题——像素验证脚本的路径覆盖行未生效，脚本**一直在读 M9-A 时期的旧截图**做 B1 区域的测量，产出大量 false FAIL（追查链：ASCII 图 → 颜色包围盒 → 逐区域计数 → 同图并排实现对比）。经验固化为设计原则（**仅记录，不要求本轮继续开发脚本**）：未来 pixel helper 最低应显式声明并回显 `VERIFY_INPUT`（被验证文件路径+哈希）、`IMAGE_SIZE`（读取到的实际尺寸）、`EXPECTED_PAGE`（该图应对应哪个 workspace），使"检查器读错输入"在输出里立刻可见。

### 33.9 Next

- **M9-B3 — Communication Extraction（Learning / Design Gate）**：待用户 GO；**本轮不开始实现**。## 34. M9-B3 Phase 1 — Serial UI Boundary / Command-State Ownership（Learning & Design，2026-09-16）

**任务档决策（§29）**：继续 **append 到 T017**——与 B1/B2 同惯例（同一 M9-B 迁移计划，§13 明确列出 B1–B5），无需独立建档。**Implementation = NOT STARTED。**

### 34.1 Preflight（2026-09-16）

```text
pwd/toplevel → /e/desktop/ModbusLens，E:/desktop/ModbusLens
HEAD=e3fce34；branch=main；git status clean；git diff --check pass
git log --oneline -12 → e3fce34 … 189c62c（链完整）
origin/main...main → 0  20（behind 0 / ahead 20；已知允许）
V2 verified LKGC = 53685d5；V1 tag v1.0.0 = ae067ab
```

### 34.2 当前 Serial UI 全量清单（逐项：控件 → 值来源 → enabled/visible → 命令 → 状态依赖 → 副作用）

| # | 控件 | QML 值来源 | enabled / visible 表达式（逐字） | 命令 | 状态依赖 | 副作用 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 端口选择 `serialPortCombo` | `analysisController.serialPortNames`（model） | `enabled: !analysisController.serialConnected` | —（选择仅本地） | serialConnected、serialPortNames | 无 |
| 2 | "未检测到串口" Label | 静态文案 | `visible: serialPortNames.length === 0` | — | serialPortNames | 无 |
| 3 | 刷新串口 | 静态 | **无 enabled 绑定（恒可用）** | `refreshSerialPorts()` | — | 枚举端口、更新 serialPortNames（DisplayRole 文案） |
| 4 | 波特率 `serialBaudCombo` | 字面量 `[9600,19200,38400,57600,115200]`，`currentIndex: 0` | `enabled: !serialConnected` | —（选择仅本地） | serialConnected | 无 |
| 5 | "8N1" Label | 静态文案 | — | — | — | — |
| 6 | 连接 | 静态 | `enabled: !serialConnected && serialPortCombo.currentIndex >= 0` | `connectSerial(currentText, Number(baudText))` | serialConnected、端口列表 | **source transition（成功后清旧批次、mode=串口模式、设 serialSourceLabel_）；失败仅置错误（原子保留）** |
| 7 | 断开 | 静态 | `enabled: serialConnected` | `disconnectSerial()` | serialConnected | 静默关闭传输；**保留上次结果与来源身份** |
| 8 | 从站地址 SpinBox | `value: 1`，`from: 1, to: 247` | `enabled: !serialBusy` | —（draft） | serialBusy | 无 |
| 9 | 起始地址 SpinBox | `value: 0`，`from: 0, to: 65535` | `enabled: !serialBusy` | — | serialBusy | 无 |
| 10 | 寄存器数量 SpinBox | `value: 2`，`from: 1, to: 125` | `enabled: !serialBusy` | — | serialBusy | 无 |
| 11 | 超时 SpinBox | `value: 1000`，`from: 100, to: 10000` | `enabled: !serialBusy` | — | serialBusy | 无 |
| 12 | 读取保持寄存器 | 文案随 busy：`serialBusy ? "读取中..." : "读取保持寄存器"` | `enabled: serialConnected && !serialBusy` | `readHoldingRegistersOnce(slave, start, qty, timeout)` | serialConnected、serialBusy | 校验范围→adapter.startTransaction→`serialBusy_=true`、`pendingSerialAddress_=slave`；异步完成走 transactionCompleted/transportError |
| 13 | Serial 错误 Label | `serialErrorMessage` | `visible: hasSerialError` | — | hasSerialError | 由 transport 错误/输入校验失败置位；connect 成功/读取接受/clearResults 清除 |

### 34.3 状态分类（§3 输出表）

| State | Current Owner（真实代码） | Proposed Owner after B3 | Must Survive Navigation? | Authoritative? | Why |
| --- | --- | --- | --- | --- | --- |
| serialConnected | Controller（`serialConnected_`，adapter 持有真实口） | **不变（Controller）** | 是 | **是** | 传输事实；AppBar chip 与按钮 enabled 都读它 |
| serialBusy / pending | Controller（`serialBusy_` + `pendingSerialAddress_`） | **不变** | 是 | **是** | 事务在途事实；stale guard 依赖 pending |
| current source（modeLabel/sourceLabel/serialSourceLabel_） | Controller | **不变** | 是 | **是** | 会话身份；B1/B2 已冻结 |
| serial error（hasSerialError_/serialErrorMessage_） | Controller | **不变** | 是（错误需跨页可见性归 Communication，状态本身不动） | **是** | 传输错误是会话级事实 |
| serialPortNames | Controller（`serialPortNames_`） | **不变** | 是（缓存） | **是（枚举事实）** | 刷新才变化 |
| 端口选择 index | QML（`serialPortCombo.currentIndex`） | **Page 本地（Communication）** | 是（StackLayout 常驻自然保留） | **否** | 未提交的选择不是设备事实 |
| 波特率 index | QML | **Page 本地** | 是 | 否 | 同上（connectSerial 接受后才成为会话事实并进入 sourceLabel） |
| slave/start/qty/timeout | QML（SpinBox value） | **Page 本地** | 是 | **否** | 只有提交（读取/未来写）才成为事务事实 |
| focus / 装饰 | QML | Page 本地 | 否 | 否 | 纯呈现 |

### 34.4 Authoritative vs Command-Draft（§4 结论，引用真实语义）

- **authoritative state** = 控制器里由**真实事件**产生、被其他组件当作事实读取的字段：`serialConnected_`（真实 open 的结果）、`pendingSerialAddress_`（adapter 真正接受 startTransaction 之后才写入，cpp:986-987 —— 代码注释明说"Accept only writes metadata AFTER the adapter really accepted"）、`serialSourceLabel_`（连接成功后由 port+baud 构造，cpp:926）。
- **command draft / form state** = 用户输入但**尚未提交**的候选值：Slave=5 在点击"读取保持寄存器"之前**不是任何设备或会话的事实**——它没有进入 Controller，没有进入 pending，没有影响任何统计；点击后才由 `readHoldingRegistersOnce` 校验并（adapter 接受后）写 `pendingSerialAddress_`。
- **"连接成功后的真实 port/baud"与"尚未提交的 port selector"是两个概念**：前者是 `serialSourceLabel_ = "COM3 @ 9600"`（会话身份，AppBar 显示它）；后者是 ComboBox 的 currentIndex（一个候选，失败时不产生任何会话痕迹——s02 的原子保留语义即为此）。B3 的边界：**draft 永远留在页面，事实永远来自 Controller**。

### 34.5 Communication 第一版产品边界（§5 四案比较）

| 方案 | User task | Migration risk | M10 扩展 | Duplication | 1000×700 密度 | V1 行为风险 |
| --- | --- | --- | --- | --- | --- | --- |
| A 仅连接控制 | 无法完成"读寄存器"任务（请求控件不在） | 低 | 需补搬一次 | 无 | 最松 | 低 |
| **B 连接 + 现有 FC03 请求（推荐）** | **完整覆盖"连上并读一次"** | **低（整块 GroupBox 平移）** | 请求区可直接扩展 | 无 | 松（≈150px 内容） | 低（表达式逐字迁移） |
| C 再加 statistics/transactions | 任务重叠 | 高（表 520 宽 + 行高） | 中 | **统计第三份/事务第二份视图** | 紧张 | 高（绑定面扩大） |
| D 预留空 Request Builder | 无真实动作 | 中 | — | 无 | 占位空洞 | **禁止（placeholder）** |

**结论：B**。真实代码核对支持：Serial GroupBox 本身就是"连接行 + 请求行"两行的单一实现（Main.qml 149-276），B = 原样搬运，不改语义；C 的统计已有 Dashboard+Legacy 两视图、事务表留在 Legacy（§34.6），再来一份是纯重复。

### 34.6 Dashboard vs Communication 边界（§6）

| 内容 | 归属 | 理由 |
| --- | --- | --- |
| Statistics | **Dashboard**（+Legacy 迁移期共享，B5 后退出） | 会话健康概览；B2 已定 |
| Recent Transactions | **Legacy（B3 不动）**；未来归 Communication 的时机由 B5/M9-D 决定 | 本阶段不制造第二份表视图 |
| Current Request（FC03 参数） | **Communication** | 与总线交互的上下文 |
| Serial Error | **Communication**（详细） | 命令上下文错误，见 §34.16 |
| Connection Status（简明） | **AppBar chip**（`串口模式 · COMx @ baud · 已连接`） | 会话身份，B1 已定；Communication 页内提供完整控制 |

**不重复原则**：同一组件（StatisticsOverview）已存在的视图不再加第三份；transactions 表本阶段不搬。

### 34.7 M10 兼容预留（只做边界分析，§7）

Communication 页的结构边界（B3.2 落地）：**Connection 区（顶）→ Request 区（中）→ 状态/错误行（下）**。M10 的三件事（FC03/FC06/0x10）全部落在 **Request 区内部**扩展（新增模式/参数/写入值列表），**Connection 区与状态行不动**；不预留任何空控件、不引入通用 request model（禁止项明确）。约束记录：写入值列表需要宽度 ≈300+ 且高度可变 → §34.21 预算已记录余量。

### 34.8 命令映射（§8，逐命令核验）

| 命令 | Trigger | Args | Preconditions（真实） | Authority | Source 变化 | 异步完成 | 错误 | 既有测试 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `refreshSerialPorts()` | 刷新串口按钮（恒可用） | 无 | 无（**发现-only，绝不 open**） | Controller（cpp:883-896） | 无 | 无（同步枚举） | 无 | —（约束写在注释） |
| `connectSerial(port, baud)` | 连接按钮 | portName、baudRate | portName 非空；baud ∈ `kSupportedSerialBauds`（cpp:317/904） | Controller | **成功后=source transition**（清批次、mode=串口模式、设 sourceLabel）；失败=原子保留 | open 同步；后续完成异步 | transportError → 错误 lane（状态不动） | `s02`、adapter `i01/i02` |
| `disconnectSerial()` | 断开按钮 | 无 | 无（恒安全） | Controller（cpp:937-943） | **无**（保留结果与来源身份） | 无 | 无（静默） | adapter `i03_intentionalCloseIsSilent` |
| `readHoldingRegistersOnce(slave,start,qty,timeout)` | 读取按钮 | 4 个 int（QML Number） | C++ 先验范围（1..247 / 0..65535 / 1..125 / >0）+ `serialConnected && !serialBusy`（cpp:945-975） | Controller | 无（串口模式内） | `transactionCompleted` / `transportError` 异步到达 | 校验失败→serial error；超时→Timeout 分析 | `s03/s04`、session `a01-a16`、`s05/s06/s10` |
| `clearResults()`（AppBar） | 清空结果 | 无 | 无 | Controller | **无** | 无 | 无 | `r08`、`s08` |

### 34.9 Enabled / Busy 逐项（§9，迁移红线）

上表 §34.2 的 `enabled:`/`visible:` 表达式为**冻结文本**：B3 迁移必须逐字复制（含 `!serialConnected && serialPortCombo.currentIndex >= 0`、`serialConnected && !serialBusy`、busy 文案三元式）。**禁止"看起来等价"的改写**；改写即视为契约违规（`qml_geometry_check`/`qml_nav_check` 之外由人工+diff 复核）。

### 34.10 跨导航 Serial 生命周期（§10 invariant）

- **进入 Communication 不自动 connect；离开不 disconnect；导航不 cancel pending；导航不 reset serial state。**
- 架构天然支持：adapter/session 全部由 Controller 持有（`serialAdapter_` 成员），页面只是 consumer；StackLayout 全实例化（B2 已验证页面 identity 稳定），无 Loader 销毁路径。
- **不存在 QML 销毁影响 signal connection/timer/adapter 的路径**——除非页面犯错：B3 明令禁止页面在 `Component.onCompleted`/`onVisibleChanged`/`currentIndex` 变化里调用任何 serial 命令（`qml_nav_check` 的 Scenario H 将断言"激活页面不产生任何业务副作用"）。

### 34.11 离线可测性调查（§11，不造假）

现有 seam 盘点：

| 层 | seam | 能力 | 局限 |
| --- | --- | --- | --- |
| C++ adapter | `test_serial_adapter.cpp` `i01-i05` | 失败打开/有界错误/静默关闭/初始态/未开就读 | **无依赖注入**：`QSerialPort port_` 是具体成员（SerialPortAdapter.h:71），无法替换为 fake port |
| C++ controller | `publishSerialResult(...)`、`handleSerialTransactionCompleted(...)`（非 Q_INVOKABLE） | 在无硬件下证明 publish 映射、单结果批次、**stale guard**（s05/s06/s07/s10） | 不经 QML；无法产生"connected=true" |
| QML | `connectSerial("不存在的端口", baud)` | **真实失败路径**：非空校验→open 失败→transportError→错误置位且状态原子保留 | 只能覆盖失败分支 |

**结论**：**完整 connected/pending 跨导航验证继续 DEFER**（无硬件、无 DI seam、不制造与真实行为不一致的 fake contract）。**新增诚实的部分覆盖（Scenario G′，B3.3 落地）**：调用 `connectSerial(不存在端口)` → 断言错误置位且 `serialConnected==false` → 三页往返 → 断言错误与状态逐值不变（真实语义，非 fake）。人工验收覆盖 UI 行为；**不得宣称真实 hardware connection PASS**。

### 34.12 Stale Completion Guard（§12）

真实机制：`handleSerialTransactionCompleted` 首行 `if (!pendingSerialAddress_.has_value()) return;`（cpp:1032-1044）——没有 pending 元数据的完成**永不覆盖**当前 Simulator/Replay 批次；`publishSerialResult` 之后 `pendingSerialAddress_.reset()`。`readHoldingRegistersOnce` 只在 adapter **真正接受**后写 pending（cpp:986）。

**为什么 UI 迁移理论上不应触碰它**：迁移只移动 QML 控件，pending 的写入/判定全在 C++ 路径（按钮 onClicked → 命令 → adapter → 完成回调）。

**B3 最危险的误操作（只列真实风险）**：① 页面销毁/重建时断开 adapter 信号（StackLayout 下不会发生，但若有人改用 Loader 就会）② 切页时"顺手取消在途请求"（违反 §34.10，且会把 stale guard 掩盖的真问题变成常态）③ 在页面复制 `serialBusy` 状态（第二份事实源，硬件完成时不同步）④ 在页面包装 `connectSerial` 做"预校验后自行切换来源"（绕过原子语义）。全部列为禁止项。

### 34.13 CommunicationPage Ownership（§13）

与 B2 一致：`required property var analysisController`（显式注入，无全局、无第二实例）；页面负责 form/draft presentation、binding、用户动作；Controller 负责 session state/commands/异步事实。**允许 Page 本地的 draft property** = §34.3 表中的"否/Page 本地"四项（端口/波特率 index、四个 SpinBox value、焦点与装饰）。

### 34.14 Draft Persistence（§14）

StackLayout 常驻实例使 draft 天然保留：切走再回来，SpinBox/ComboBox 值不变（Scenario F 将断言）。规则：**draft 保留是页面生命周期的自然结果，不把 draft 写回 Controller**；"已执行请求的 authoritative facts"（批次行/统计/错误）与 draft 是两个层级，前者永远来自 Controller。

### 34.15 Validation / 输入语义（§15）

既有范围（逐字）：slave 1..247、start 0..65535、quantity 1..125、timeout 100..10000、baud ∈ 固定五项、连接需 `currentIndex >= 0`；C++ 端 `s04_inputValidation` 再验一遍（防御 QML 侧被绕过）。**B3 只迁移，不新增 validation layer**；任何新校验（如地址区间提示）DEFER 到 M10/独立任务。

### 34.16 Serial 错误位置（§16）

推荐：**错误详情归 Communication 页**（命令上下文错误的自然位置）；**AppBar 只保留简洁状态**（来源 chip 已有 `· 已连接`；不新增错误文本到 AppBar）。理由：AppBar 是会话身份条，错误需要"触发它的上下文"（哪个命令、什么参数）；`serialErrorMessage` 是会话级**事实**但呈现归上下文页——状态在 Controller、呈现单一化。

### 34.17 Legacy after B3（§17）

Legacy 保留：Load Replay（单按钮行）→ Replay error/notice → **StatisticsOverview(legacy)** → 分隔线 → SplitView(诊断|事务)。串口 GroupBox 与 Serial 错误行移出后：SplitView 净增 ≈140px 垂直空间（**不是空洞，是增益**）。允许最小收口（注释/间距），不做视觉重设计；不动 Replay/Diagnosis/Transactions。

### 34.18 导航模型（§18）

索引契约扩展（Main.qml 集中定义）：`workspaceCommunicationIndex: 2`；启用**工作台/总览/通信**；保持 disabled：回放(3)/诊断(4)/设备(5)。**不重排**；rail 顺序、StackLayout 顺序（child2 = CommunicationPage）、测试三方继续共读同一契约。

### 34.19 跨导航持久化场景（§19）

- **Scenario E**：Dashboard → Communication → Dashboard：统计全字段与 mode/source 不变（复用 B2 快照机制）。
- **Scenario F**：在 Communication 设置 draft（slave=7, start=10, qty=3, timeout=2500, baudIndex=2）→ 三页往返 → draft 逐值保留（页面本地，非 Controller）。
- **Scenario G′（部分，真实语义）**：见 §34.11；完整 connected/pending 场景 **DEFER**（不造假）。
- **Scenario H**：激活 Communication 前后**全字段业务快照相等**（无 connect/disconnect/read/source transition）——可观察证据 = nav check 快照 diff 为空 + 页面激活轨迹日志。

### 34.20 qml_nav_check 扩展（§20）

不新建第二套机制：现有 `--qml-nav-check` 扩为 **3 个真实 workspace**，路径 `legacy → dashboard → communication → legacy`；断言：page identity（3 指针）、visibility 随选中、index 合法、禁用项（3/4/5）invoke 不可切、Scenario E/F/H（+G′）；全部消费 root 上的 index 契约属性。

### 34.21 布局预算（§21）

```text
700 高：host 659 − 页内缩 32 = 627 可用
Communication 内容 = 页标题 21 + 12 + Connection 区（一行控件 34 + PanelCard 内边距 24 ≈ 58）+ 12
                   + Request 区（一行 34 + 24 ≈ 58）+ 12 + 错误行（可见时 ≈17）≈ 190
→ 余量 ≈ 437px（无需滚动；不填填充物）
未来 M10 约束（只记录）：写入值列表需 ≈300+ 宽、可变高；当前余量足够，但 M10 落地前需按实际控件复核。
宽度：页内容 911/935；控制行最宽组合实测在 1024 下无换行需求（横向 RowLayout + 尾部 spacer 保留）
```

### 34.22 UI 结构方案（§22 比较与推荐）

| 方案 | migration risk | clarity | M10 | visual change | testability |
| --- | --- | --- | --- | --- | --- |
| A 单 GroupBox 直接 MOVE | 最低 | 连接/请求混杂 | 仍需拆分 | 最小 | 好 |
| **B 拆两个 presentation section：Connection / Request（推荐）** | 低（先整块 MOVE，再纯结构拆分） | 清晰 | **Request 区即 M10 落点** | 受控（控件与表达式逐字不变） | 好 |
| C 再拆 Result 区 | 中 | 更细 | 过度预拆 | 大 | 一般（Result 目前只有一行错误） |

**推荐 B**，分两提交落地：B3.1 整块 MOVE（零结构改动）→ B3.2 纯结构拆分（两个 PanelCard 区）。C 无真实内容支撑，拒绝。

### 34.23 可复用组件需求（§23，逐候选裁决）

| 候选 | 本 B3 使用点 | 裁决 |
| --- | --- | --- |
| FieldRow（label+control 对齐） | 逻辑上 ≥10 个 label+control 对，但全是**同一行内的内联 Label**；提取会重写布局结构 | **DEFER 到 M10**（届时请求参数数量翻倍，收益最大化；B3 保持逐字迁移以压低风险） |
| StatusBadge | 无（状态=chip 文本 + 连接按钮 enabled） | DEFER |
| ErrorBanner/InfoBanner | 1 处（serial error 行） | DEFER（单使用点，M9-C 统一处理） |

### 34.24 Accessibility（§24）

Tab 顺序（页内自然顺序）：端口 → 刷新 → 波特率 → 连接 → 断开 → 从站 → 起始 → 数量 → 超时 → 读取。切到 Communication 后**焦点继续留在 rail 项**（B2 先例，不抢焦点）；键盘用户 Tab 进入页面；disabled/busy 语义沿用 §34.9 表达式（busy 期间参数禁用 + 读取按钮变"读取中..."）。

### 34.25 V1 / V2 契约风险清单（§25）

| 契约 | 真实证据 | B3 红线 |
| --- | --- | --- |
| connect/disconnect 语义 | cpp:898-943 + `s02` + adapter `i01-i03` | 只搬控件；命令与原子律零改动 |
| refresh ports | cpp:883-896（发现-only 注释） | 恒可用语义保持（无 enabled 绑定） |
| 串口 source transition | cpp:918-934（成功才切换） | 不得在页面做预校验/自行切换 |
| busy/pending | cpp:986-987 + `s05/s06` | 不得复制 busy；pending 只由 C++ 写 |
| 超时/读取参数 | session `a06/a07`、`s06` | 参数逐字传递，不做界面层裁剪 |
| stale completion | cpp:1032-1044 + `s10` | §34.12 四个禁止项 |
| 请求匹配/异常 | session `a04/a11/a15/a16` | 不触及 |
| serial errors | `s09`、T015 双 lane 语义 | 错误 lane 不变；呈现移到 Communication |
| clearResults | `r08`/`s08` | AppBar 不动 |
| statistics/transactions/诊断存续 | B2 场景 A/B/D 断言 + `d07` | 导航不得改变（Scenario E 复验） |
| M9 shell/nav + index 契约 | `qml_geometry_check`/`qml_nav_check` + §34.18 | 三页共读同一契约，不散落 magic number |
| 最小窗口 | 1000×700（T013 Policy）+ §34.21 预算 | M10 落地前复核 |

### 34.26 增量实施计划（§26）

- **B3.1 — CommunicationPage 外壳 + 整块 MOVE**：新建 `pages/CommunicationPage.qml`（页根纯 Item + 标题 + 原 GroupBox **逐字**内嵌）；index 契约 + `workspaceCommunicationIndex: 2`；rail `通信` 启位；StackLayout child2；**Legacy 同步删除串口 GroupBox 与错误行**（单入口，无重复窗口）。验证：build + qml_smoke + geometry（五趟：三页×尺寸循环按现有四趟模式扩展）+ ctest 26。
- **B3.2 — 结构拆分**（纯呈现）：Connection / Request 两个 PanelCard 区（表达式逐字不变）。验证同 B3.1（geometry 断言按新结构微调、语义断言不变）。
- **B3.3 — nav check 三 workspace 扩展**：`legacy → dashboard → communication → legacy`；Scenario E/F/H + G′。验证：ctest（26，断言扩展；如实报告是否新增 target）。
- **B3.4 — 收尾**：截图（三页 × 两尺寸）、deploy + 无 PATH smoke（含部署版 nav check）、人工验收包。
- 每步可 build/test/rollback；回滚 = revert 单提交。

### 34.27 测试计划（§27）

- **Baseline**：26（qml_smoke / qml_geometry_check / qml_nav_check 全保留）。
- **扩展**：nav check 三 workspace + 场景 E/F/H/G′；geometry check 第五趟纳入 Communication（按其真实布局断言，隐藏页零断言规则沿用）。
- **新增 offline serial 覆盖**：仅 G′ 的真实失败路径（§34.11）；**不新建脆弱 UI test、不造 fake port**。
- **人工**（两尺寸）：Communication nav 与选中态 / draft 跨页保留 / 刷新串口（本机枚举真实端口列表）/ 连接与断开按钮 enabled 逻辑（**无硬件时不宣称真实连接**）/ FC03 参数范围 / Serial 错误显示 / Dashboard·Legacy 状态不变 / 其余三项仍 disabled / resize 与无裁切。

### 34.28 Knowledge Before Implementation（§28，10 项各自对应真实问题）

| # | 知识点 | 解决 ModbusLens 的哪个真实问题 |
| --- | --- | --- |
| 1 | authoritative session state | 防止迁移期把"会话事实"搬进页面导致跨页不一致（connected/busy/source 必须单一来源） |
| 2 | command draft state | 解释为什么 Slave=5 未提交前不是设备事实——避免把 draft 误当事实写进 Controller |
| 3 | presentation state | focus/装饰/输入候选不进业务层，页面边界清晰 |
| 4 | command vs state | 命令是**动作**（可产生 source transition），状态是**结果**；混淆两者正是"切页误切源"类事故的来源 |
| 5 | async operation ownership | 完成回调必须由 Controller 承接（B3 若让页面接回调，切页即丢） |
| 6 | stale completion | 无 pending 的完成绝不覆盖当前批次——UI 迁移最不该碰的机制，四个禁止项即防线 |
| 7 | UI extraction vs behavior rewrite | extraction 的验收标准是"表达式逐字 + 行为等价可证"，不是"看起来一样" |
| 8 | form persistence | 用户切页回来参数还在——由页面生命周期天然提供，不需要把 draft 写回 Controller |
| 9 | connected configuration vs editable draft | `COM3 @ 9600`（事实）与 ComboBox 选择（候选）是两个概念，失败连接不留痕（s02） |
| 10 | future-compatible UI boundary | Connection/Request/Status 三段边界让 M10 只动中段——避免"未来推倒重来" |

### 34.29 Documentation / Status

- 本文档 append 于 T017 §34；**M9-B3 Phase 1 = Learning / Design 完成（docs-only）；Implementation = NOT STARTED**。
- 下一步：**B3 Phase 1 Review（用户）**；批准后按 §34.26 从 B3.1 开始。


## 35. M9-B3 Phase 2 — Communication Extraction Implementation

### 35.0 Phase 1 Review = PASS（用户，2026-09-16）+ Implementation Guardrails

用户批准进入 B3 Phase 2，并追加约束（本文档为 authority）：

- **A. MOVE before restructure**：B3.1 先机械迁移整块 Serial UI；行为/绑定确认保持后，B3.2 才允许拆成 Connection / Request 两个 presentation section。不得把 move + redesign + behavior cleanup 混在第一步。
- **B. Draft remains page-local**：port 选择 / baud / slave / start / quantity / timeout 继续作为 CommunicationPage 的 command draft；**不得**新建 RequestDraftModel / SerialConfigModel / Controller draft properties。若真实现有架构无法保留行为，先停止汇报。
- **C. Draft persistence tests**：断言读真实 property（`currentIndex` / `value`），不得以格式化显示文本作为唯一证据。
- **D. Serial failure test portability**：Scenario G′ 不得硬编码 OS 相关 QSerialPort 错误文本；断言 `connect failure` / `serialConnected == false` / source 原子性 / `hasSerialError`（稳定布尔属性）及其跨导航保持。
- **E. Extraction is not behavior rewrite**：Refresh 现状无 enabled 绑定 → B3 保持现状；不得顺手新增 busy/connected-disable、自动 refresh、自动 connect/disconnect——行为改进另开 task/issue。

### 35.1 Baseline（实施前）

```text
branch=main；HEAD=7bc13e6；working tree clean；git diff --check pass
V2 verified LKGC=53685d5；v1.0.0=ae067ab；ahead 21 / behind 0（已知允许）
ctest --preset debug-local → 100% tests passed, 0 tests failed out of 26（含 qml_smoke / qml_geometry_check / qml_nav_check）
```

（B3.1 → B3.4 的真实执行结果回填于本节下方。）

### 35.2 B3.1 — CommunicationPage Shell + Mechanical MOVE（`d957ff7`）

- 新建 `pages/CommunicationPage.qml`（纯 Item 页根 + 标题 + 整块串口 GroupBox + Serial 错误行）：13 项控件的类型/范围/model/enabled/visible/文案/onClicked/参数**逐字迁移**；唯一文本替换是主题访问器（`root.surface/border/textPrimary/textSecondary` → `DS.*`，二者按构造等值并有注释说明）。
- Shell：index 契约 + `workspaceCommunicationIndex: 2`；rail `通信` 启位（回放/诊断/设备仍 disabled）；StackLayout child2；Legacy **同步删除**串口块与错误行（单入口，无双窗口期）。
- 护栏：NAV 矩阵扩为三真实 workspace（navItem_0/1/2 enabled；3..5 disabled + invoke 校验）；geometry 六趟（legacy/dashboard/communication × 默认 + communication/dashboard/legacy × 1000×700），断言按可见页分派；dump 表跟随可见页并新增 parent 名称。
- 门禁：build 干净、qml_smoke EXITCODE=0、六趟 geometry PASS、nav check PASS、ctest **26/26**（无新 target）、diff-check 通过。

### 35.3 B3.2 — Connection / Request Presentation Split（`c3269dc`）

- 单 GroupBox → 两个 presentation section（`communicationConnectionSection` / `communicationRequestSection`，各配 SectionHeader）；控件、表达式、绑定零变化（guardrail A：先 MOVE 后结构）。
- **发现并修复的真实布局问题（本轮最有价值的取证）**：初次拆分后 dump 显示子项被"摊开"——contentLayout implicit **189** vs 实高 **647**，connection 在 y=229、request 在 y=521（空档 ~204/244px）。根因（观测层面）：**没有任何 fillHeight 子项时，该 ColumnLayout 会把多余空间散布到子项之间**；Legacy 之所以一直正常，只是因为它的 SplitView 恰好带 `Layout.fillHeight` 吸收了余量。修复 = 显式尾部弹性 spacer（把 Legacy 的隐式机制显式化）；修后位置 y=0/27/54/114/141、内容止于 201px ✓ 与设计预算一致。Qt 内部精确机制本轮**未完全隔离**（如实记录）。
- **同族现象如实上报**：已验收的 Dashboard 页存在同样的"摊开"（统计带落在 ~449 逻辑位而非紧致 ~89）——用户曾将其表述为"较大的空白空间（属 M9-C）"。本轮**不触碰**（范围冻结 + 用户已裁定其非 B2 regression），仅在报告中提交供后续决定。
- 门禁：build/smoke/六趟 geometry/nav check/ctest 26/diff-check 全绿。

### 35.4 B3.3 — nav check 三 Workspace + Scenarios E/F/G′/H（`382ecfb`）

- `--qml-nav-check` 重写为三 workspace 完整路径 `Legacy → Dashboard → Communication → Dashboard → Legacy`（真实激活路径），逐站验证：三页同时存在、页面 identity 三指针稳定、可见性随选中、index 合法性（读 root 契约）、禁用项 3/4/5、全字段业务快照（Scenario E + H，并输出完整切换轨迹）。
- **Scenario F（draft 持久化）**：以真实 property 断言（guardrail C）——slave=7 / start=10 / quantity=3 / timeout=2500 / baud index=2（5 项）经 `Legacy ↔ Communication` 全程往返后逐值不变；端口项在本机（且启动时未执行 refresh）无端口 → **显式 DEFERRED**（不伪造端口存在）。
- **Scenario G′（真实失败路径）**：`connectSerial("MODBUSLENS_NO_SUCH_PORT", 9600)` → 断言操作失败、`serialConnected=false`、**authoritative `hasSerialError=true`**（稳定布尔，不用 OS 错误文本，guardrail D）、mode/source 快照不变（原子性）、并跨 `Communication → Dashboard → Communication` 保持。实测日志：`serialConnected=0 hasSerialError=1 mode=模拟器模式 source=确定性演示` ✓。
- Scenario A/B/D 保留于同一状态机；未新建第二套导航测试机制。

### 35.5 B3.4 — Deploy / Screenshots / Manual Candidate

- 截图（新命名，不覆盖既有证据）：`m9b3-communication-1024x720.png` / `m9b3-communication-1000x700.png` / `m9b3-legacy-1024x720.png` / `m9b3-dashboard-1024x720.png`（后两张作防回归对照）。
- 自适应像素自检（四图 **全部 PASS**）：legacy/dashboard 用**自动检测卡带**（旧固定坐标已因统计区上移 ~101px 失效——教训：期望坐标必须自适应或与生成时同源）；通信页用两个内容带（connection 220 / request 421 文本像素）+ **无统计面签名**（竖向连续 ≥30px 的 F4F4F4 列 = 1，阈值 <10）；rail 选中 accent 按**文件名对应页**断言（legacy/dashboard/communication 三档各 40，其余档 ≤4）——选中随页移动成为数字化证据。
- deploy：重建 `[OK]`、无 PATH smoke EXITCODE=0、**部署版 `--qml-nav-check` 完整 PASS**（含三 workspace 轨迹与 G′）。

### 35.6 Problems / RCA

1. **布局摊开（P0 级，已修复）**：无 fillHeight 子项的 ColumnLayout 把余量散布于子项之间（实测 189 vs 647；Legacy 因 SplitView 幸免）。修复=尾部弹性 spacer；同族现象存在于已验收的 Dashboard（如实上报，不越界修改；M9-C 可决定是否收紧）。Qt 精确机制未完全隔离。
2. **陈旧证据（第二次"验证器输入"教训）**：首轮像素自检大面积 FAIL——截图实为 **spacer 修复前**的旧 dump（我拷贝时没有重新出图）；ASCII 图与 dump 数值互相矛盾才暴露。规则升级：**证据文件必须由通过门禁的同一二进制在同一轮重新生成**；配合上周的 `VERIFY_INPUT/IMAGE_SIZE/EXPECTED_PAGE` 原则一并执行。
3. **像素签名的设计陷阱（第三次教训）**：通信页"无统计面"检查先后被两类假象骗过——surfaceAlt 控件**抗锯齿杂点**（水平结构化判据失效）与 **Fusion 按钮竖向渐变中段**（颜色恰为 F4F4F4，bbox 精确覆盖各按钮）。最终签名=**竖向连续 ≥30 行的卡面列**（真 StatCard ~90 行连续，按钮渐变仅几行）——签名的选择必须能区分"真实控件族"，而不是只匹配颜色。
4. **诊断工具增强**：dump 表新增 `parent=` 名称（定位子项属于哪个容器时一眼可见，本轮靠它排除了"父子关系猜错"）。

### 35.7 Knowledge Learned / Interview Questions

**Knowledge（结合真实实现）**：
1. extraction 的第一步必须是**可证恒等**的机械迁移（表达式逐字；唯一替换 DS 访问器并说明理由）；结构整理放第二步。
2. Qt Quick Layouts 的"余量归属"是显式契约：**谁 fillHeight 谁吸收余量**——没有消费者时余量会以你不期望的方式出现；显式 spacer 是让布局意图可见的最便宜方式（Legacy 的隐式版本在本轮才被看见）。
3. draft（页面）与 authoritative（Controller）的边界在迁移期必须用**真实 property**断言（`value/currentIndex`），显示文本永不作为唯一证据。
4. 离线诚实边界：无 DI seam 时用**真实失败路径**（G′）部分覆盖，完整 connected 场景保持 DEFER；绝不制造 fake contract。
5. 验证器三原则（本会话三次教训的沉淀）：输入可验证（路径+哈希+尺寸+页别）、期望自适应（或与生成同源）、签名可区分（颜色不够，形状/结构才够）。

**Interview Questions**：
1. 为什么"刷新串口"没有 enabled 绑定也要原样保留？——extraction 的边界：行为改进是独立 task；本轮任何"顺手优化"都会污染迁移的可证等价性。
2. 无 fillHeight 子项的 ColumnLayout 会发生什么？——余量散布（本项目实测）；Legacy 因为 SplitView 的 fillHeight 而幸免；显式 spacer 把这条隐式契约变成可见代码。
3. 怎么在无硬件条件下测 serial 失败语义？——用真实命令的真实失败分支（不存在端口 → `hasSerialError` 稳定布尔 + mode/source 原子性 + 跨页保持），而不是伪造端口。

### 35.8 Candidate Commits

- `d957ff7`（B3.1 页面外壳 + 机械迁移）、`c3269dc`（B3.2 结构拆分）、`382ecfb`（B3.3 三 workspace nav check + 场景）、`072fe34`（B3.4 候选：文档 + 四张截图）。哈希回填列为 docs-only 提交，不作 LKGC。
- 全部**不 push、不推进 LKGC**（verified 保持 `53685d5`，待人工视觉 PASS）。

## 36. Next

- **M9-B3 Manual Visual Review = PENDING USER REVIEW**（Communication 双尺寸截图 + 既有页面防回归对照 + deploy 候选）。
- PASS 之前：不推进 LKGC（保持 `53685d5`）、不 push、不开始 B4。
## 37. M9-B3 Completion / Archive（2026-09-16）

### 37.1 Manual Visual Review = PASS（用户）

**Communication 确认**：通信 navigation 选中态正确；Connection / Request 分区清楚；Port / Refresh / Baud / 8N1 可见；Connect / Disconnect 可见；FC03 Slave / Start / Quantity / Timeout 完整；Read Holding Registers 可见；Serial error 位于 Communication 上下文；error 显示未导致布局挤压；页面无裁切、无重叠；**大面积剩余空间不构成本阶段 regression**。

**Legacy 确认**：Serial controls 已移出、无重复入口；Load Replay 仍可达；Statistics 正常；Diagnosis 正常；Recent Transactions 正常；Serial 区迁出后布局**没有空洞/塌陷**。

### 37.2 PASS 边界（明确声明）

本 PASS 表示 **M9-B3 Communication Extraction 的页面边界、导航、draft persistence、失败路径与当前视觉达到本阶段要求**。**不表示** 真实 Modbus hardware serial connection 已经人工验证；**full connected / busy / pending 跨 workspace 自动验证 = 继续 DEFER**。**不得把 G′ failure-path PASS 扩写成 real hardware PASS。**

### 37.3 Deferred Visual Work（非 B3 regression）

Qt ComboBox / SpinBox 仍为较默认风格 · Connect / Disconnect 控件待后续统一样式 · Communication 页面当前存在较大空白 · richer status presentation 尚未实现。**Communication 的剩余空间应作为未来 M10 Active Master 请求/结果区域的潜在空间**；closure 阶段未填充任何 widget。

### 37.4 LKGC 候选的 Git 证据裁定（原样命令）

```text
$ git show --stat --oneline c3269dc
c3269dc M9-B3.2: split communication page into connection and request sections
 src/main.cpp                           |  56 +++++--
 src/ui/qml/pages/CommunicationPage.qml | 276 ++++++++++++++++++---------------
 2 files changed, 198 insertions(+), 134 deletions(-)

$ git show --stat --oneline 382ecfb
382ecfb M9-B3.3: nav check goes to three workspaces with scenarios E/F/G'/H
 src/main.cpp | 452 ++++++++++++++++++++++++++++++++++++++---------------------
 1 file changed, 290 insertions(+), 162 deletions(-)

$ git show --stat --oneline 072fe34
072fe34 M9-B3.4: communication extraction candidate (docs + visual evidence)
 docs/… + 4× m9b3-*.png + devlog（9 files, 62+/18−）
```

**分类**：`c3269dc` = QML + `src/main.cpp`（**behavior-bearing**）；`382ecfb` = 仅 `src/main.cpp`（**test behavior-bearing**——nav check 三 workspace + 场景 A/B/D/E/F/G′/H 即其行为）；`072fe34` = **docs/screenshots only**。

**裁定**：最后一个包含真实 product/QML/test behavior change 且其仓库树经过完整验证（qml_smoke + geometry 六趟 + nav check + ctest 26/26 + deploy smoke + manual PASS）的提交 = **`382ecfb`** → **V2 verified LKGC 推进 `53685d5` → `382ecfb`**。**不得以 commit message 的 "candidate/visual/final" 词汇替代文件证据**（`072fe34` 命名含 candidate，实为 docs-only，不作 LKGC）。

### 37.5 B3 Final Status = COMPLETE

**交付清单**：CommunicationPage · Connection section · Request section · Serial error contextual placement · Communication navigation enabled · page-local command draft · authoritative Controller state preservation · Scenario E · Scenario F · Scenario G′ · Scenario H · three-workspace nav check · six-pass geometry guard · deploy validation · **manual visual PASS**。

提交链：`7bc13e6`（Phase 1 设计）→ `d957ff7`（B3.1）→ `c3269dc`（B3.2）→ `382ecfb`（B3.3，**= verified LKGC**）→ `072fe34`（候选：docs+截图）→ `c007013`（回填）→ `2c3955c`（closure，docs-only，非 LKGC）。

### 37.6 已验证架构（正式记录）

1. **Navigation activation 不是 Serial command**：切页只改 presentation index；`qml_nav_check` 的逐站全字段快照 + 人工行为链共同证明。
2. **Communication page-local 值**（port selection / baud / slave / start / quantity / timeout）属于 **command draft**，不是 authoritative session fact——断言用真实 property（`value`/`currentIndex`）。
3. **Connected / session / source / error / busy / pending 的 authority 仍属于 Controller / Serial subsystem**（页面零状态副本）。
4. **失败 connect 不允许污染旧 source**：G′ 实测 `serialConnected=0 / hasSerialError=1 / mode=模拟器模式 / source=确定性演示`（原子性保持）。
5. **页面隐藏/显示不得管理 async request lifetime**：adapter/session 由 Controller 持有；StackLayout 全实例化；页面生命周期与请求生命周期解耦。
6. **完整 connected/busy/pending 的跨导航持久性仍未自动证明**（DEFER 保持，人工验收覆盖 UI 行为且不宣称真实硬件 PASS）。

### 37.7 Layout Knowledge Closure（本轮真实经验）

- **现象**：Communication contentLayout 的 implicitHeight（189）远小于可分配高度（647）时，**额外纵向空间的归属未被显式设计**——Qt 把余量散布到子项之间（实测 y=229 / y=521，空档 ~204/244px）；Legacy 一直"正常"只因 SplitView 恰好带 fillHeight。
- **修复**：显式尾部弹性 spacer，使 Connection / Request 紧凑排列（y=0/27/54/114/141）、余量落在页面末尾——把隐式契约变成可读代码。
- **与 ISSUE-012 的区分（不得混为一谈）**：ISSUE-012 = **parent sizing contract collapse**（容器 implicit 为 0、父子尺寸链断裂，几何塌缩）；B3 = **surplus-space ownership unclear**（尺寸链完好，只是多余空间的归属没有设计）。两者症状/根因/修法都不同。
- **Dashboard 的同族 surplus-space 现象**：如实记录、**留待 M9-C**；B3 不越界修改。

### 37.8 Verification Oracle Closure（本轮真实经验）

本轮验证器连续经历三类真实事故：**陈旧截图**（拷贝未重新出图 → 假 FAIL 风暴）· **固定像素坐标过期**（统计区上移后 B1 时代坐标全部失效）· **Fusion button gradient 与 card surface 颜色碰撞**（渐变中段恰为 #F4F4F4，bbox 精确覆盖按钮）。

由此确立验证器四原则（记录为设计原则，非本轮开发任务）：
1. **input identity**——验证输入必须自证（路径 + 哈希 + 实际尺寸 + 页别）；
2. **current candidate evidence**——证据必须由通过门禁的同一二进制在同一轮生成（禁止复用历史 dump）；
3. **adaptive expectation**——期望自适应（自动检测带/区域）或与生成时同源；
4. **distinguishable signature**——签名必须能区分真实控件族（颜色不够：竖向连续 ≥30 行的卡面列 vs 按钮渐变仅数行）。

**pixel helper 是合同探测工具，绝不是视觉验收替代品**；M9-F 的人工验收地位不变。

### 37.9 最终验证记录（终验结论）

| 验证项 | 结果 |
| --- | --- |
| qml_smoke | **PASS** |
| qml_geometry_check | **PASS**（Legacy / Dashboard / Communication × 1024×720 / 1000×700 六趟） |
| qml_nav_check | **PASS**（三 workspace 全轨迹） |
| Scenarios | **A/B/D/E/F/G′/H PASS** |
| Scenario G（full connected/busy/pending） | **DEFER**（无 DI seam / 无硬件，不造假） |
| full ctest | **26/26** |
| deploy smoke | **PASS**（含部署版 nav check） |
| manual visual | **PASS**（Communication + Legacy） |
| **real hardware serial** | **NOT CLAIMED** |

本 closure 为 docs/status-only，不产生新的产品验证结果——上表即 `382ecfb` 行为的终验证据。

### 37.10 Knowledge Closure（基于本轮真实代码/测试）

1. **authoritative session state vs command draft**：`pendingSerialAddress_` 只在 adapter 真正接受后才写入（cpp:986 注释），而 Slave=5 未提交前只是页面表单值——C++ 的写入时机就是两者边界的代码证据。
2. **connected configuration vs editable candidate**：`serialSourceLabel_ = "COM3 @ 9600"`（连接成功后的会话身份）与 ComboBox currentIndex（候选）是两个概念；失败连接不留痕（s02 语义，G′ 复证）。
3. **extraction vs behavior rewrite**：Refresh 无 enabled 绑定原样保留；任何"顺手优化"都会破坏迁移的可证等价性——行为改进另开 task。
4. **navigation vs business command**：H 场景逐站快照证明激活页面零副作用；rail 激活路径（`activate()`）与命令调用点（onClicked）在代码上完全分离。
5. **async operation ownership**：完成回调由 Controller 承接（`handleSerialTransactionCompleted`）；页面销毁/隐藏与在途请求无关（StackLayout 全实例化是结构保障）。
6. **stale completion guard 为什么不能由页面生命周期管理**：`if (!pendingSerialAddress_.has_value()) return;` 保护的是"完成与请求的配对"——这是 C++ 侧事务身份问题；页面若 disconnect 信号或 cancel 请求，等于把身份判定搬进 UI 生命周期，必然错配。四个禁止项即防线。
7. **StackLayout 如何自然保留 page-local draft**：页面实例常驻 → SpinBox/ComboBox 值随实例存活；F 场景 5 项属性往返逐值相等即证据；无需（也不允许）把 draft 写回 Controller。
8. **offline verification boundary：为什么 G′ PASS ≠ G PASS**：G′ 只覆盖 `connectSerial` 的**失败分支**（openPort 失败 → 错误置位 + 原子保留）；connected/busy/pending 需要真实打开端口或 DI seam——两者结论不能互相外推。
9. **surplus layout space ownership**：见 §37.7——"尺寸链完好但余量归属未设计"是独立于 ISSUE-012 的布局问题类型。
10. **verification oracle 输入自身需要可验证**：见 §37.8 四原则。

### 37.11 Next

- **M9-B4 — Replay Extraction（Learning / Design Gate）**：待用户 GO；**本轮不开始实现**。**M10 亦不开始。**## 38. M9-B4 Phase 1 — Replay Workflow / Source-State Boundary（Learning & Design，2026-09-16）

**任务档决策（§29）**：继续 **append 到 T017**（B1–B5 同属 §13 的一个 M9-B 迁移计划；B1/B2/B3 均已同档落地）。**Implementation = NOT STARTED。**

### 38.1 Preflight（2026-09-16）

```text
HEAD=6b76791；branch=main；working tree clean；git diff --check pass
git log -12 → 6b76791 … 189c62c（链完整）
origin/main...main → 0  28（behind 0 / ahead 28；已知允许）
V2 verified LKGC = 382ecfb；V1 tag v1.0.0 = ae067ab
```

### 38.2 当前 Replay UI 全量清单（§2，逐项）

| # | UI element | Current owner | Value source | visible/enabled | Command | Side effect | Persistence |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `replayFileDialog`（FileDialog） | Main.qml:67-75 | 平台对话框 | 常驻（非视觉对象，open() 时弹出） | `open()` 由 Load Replay 触发 | `onAccepted → loadReplayFile(selectedFile)` | 对话框实例常驻；`selectedFile` 为瞬时候选 |
| 2 | 标题/过滤器 | 静态 | `title: 加载回放日志`；`nameFilters: [ModbusLens 回放日志 (*.mlog), 所有文件 (*)]` | — | — | — | — |
| 3 | `selectedFile` | FileDialog property | 用户选择 | — | 作为 `loadReplayFile` 参数 | **候选值，仅在接受瞬间有意义** | 不持久（无 Controller 字段） |
| 4 | `onRejected` | **未接线** | — | — | 无（对话框关闭，零状态变化） | 无 | — |
| 5 | Load Replay 按钮 | Legacy demo-controls 行（Main.qml:216） | 静态文案 | **无 enabled 绑定（恒可用）** | `replayFileDialog.open()` | 打开对话框（不发业务命令） | — |
| 6 | Replay error Label | Legacy（Main.qml:225） | `analysisController.replayErrorMessage` | `visible: hasReplayError` | — | — | 随 Controller 状态 |
| 7 | Replay notice Label | Legacy（Main.qml:235） | `analysisController.replayNoticeText` | `visible: hasReplayNotice` | — | — | 随 Controller 状态 |

**再无其他 Replay 专属 UI**。已核对：无 currentFolder/fileMode 定制（使用 Qt Quick FileDialog 默认 OpenFile 单选）；无 selectedFiles 多选。

### 38.3 Replay Source 语义（§3，真实 production code + tests）

`AnalysisController::loadReplayFile(fileUrl)`（cpp:1226-1320）的**真实执行序**：

1. `!fileUrl.isLocalFile()` → `setReplayError("回放加载失败：不是本地文件")`，**return**（旧状态全不动）。
2. `QFile.open` 失败 → `setReplayError("…无法打开文件")`，return。
3. `parseReplayLog(text)` → `ReplayParseError` → `parseErrorMessage` → error，return。
4. `analyzeReplayLog(log)` → `ReplayExecutionError` → error，return。
5. **全部成功后**（原子发布，cpp:1301-1320）：
   - unsupported 披露：`unsupportedRecords` 空 → clear notice；非空 → `setReplayNotice("提示：N 条记录当前未支持分析（功能码 0xXX 等），未计入统计。")`（T015 Gate F）；
   - `teardownSerialTransport()`（**SB-13：串口只在成功路径关闭**——失败加载绝不关闭串口）；
   - `transactionModel_.setEntries` + `statistics_ = batch.statistics` + `activeDiagnosisTransactions_`（同一批次三视图）；
   - `invalidateAiForBatchChange()`（AI 失效，ai06/ai08 语义）；
   - `modeLabel_ = "回放模式"`；**`sourceLabel_ = QFileInfo(filePath).fileName()`**（注释明言：**完整路径永不进入 UI**）；
   - clearReplayError + clearSerialError；emit statisticsChanged + sourceChanged。

**对 §3 三问的回答（代码+测试为证）**：
- **只有 load 成功后才切换 Replay source**（步骤 5 在全部成功之后；`r03/r04/r05` 证明失败时旧状态原子保留）。
- **失败保持旧 source**（modeLabel_/sourceLabel_ 不被触碰；`d04/ai07` 同族）。
- **错误时 transaction/statistics 保持旧值**（`r03_parseErrorPreservesState` 直接断言）。

### 38.4 Replay 双身份（§4）

- **Replay Workspace** = UI 任务区（加载/校验/披露工作流），激活只改 presentation index。
- **Replay Session Source** = 会话数据来源（`modeLabel_="回放模式"` + `sourceLabel_=文件名`），由 `loadReplayFile` **成功后**原子建立。

**Invariant（B4 强制）**：进入 Replay Workspace 只允许改变 presentation workspace index；**不得自动**打开文件/加载文件/clear/切 source/解析日志。真正的 source transition 只能来自既有显式 load 命令按 §38.3 的成功语义发生。

### 38.5 状态分类（§5）

| State | Current Owner（真实） | Proposed Owner after B4 | Authoritative? | Must Survive Navigation? | Why |
| --- | --- | --- | --- | --- | --- |
| modeLabel_/sourceLabel_ | Controller | **不变** | **是** | 是 | 会话身份（B1 已定 chip 语义） |
| transactions（transactionModel_） | Controller（单实例） | **不变** | **是** | 是 | 批次事实 |
| statistics_ | Controller | **不变** | **是** | 是 | 批次事实 |
| activeDiagnosisTransactions_ + batch revision | Controller | **不变** | **是** | 是 | 诊断/AI 输入 |
| hasReplayError_ / replayErrorMessage_ | Controller | **不变**（呈现归 Replay 页） | **是**（load 尝试的结果事实） | 是 | r03/r06 契约 |
| hasReplayNotice_ / replayNoticeText_ | Controller | **不变**（呈现归 Replay 页） | **是**（成功加载的披露事实） | 是 | T015 Gate F |
| FileDialog 实例 + selectedFile | Main.qml（全局） | **ReplayPage 本地** | **否** | 对话框实例随页常驻；selectedFile 瞬时、无持久承诺 | 工作流候选值（§38.6） |

### 38.6 Selected File vs Loaded Source（§6 结论）

**不是同一个事实。** `selectedFile` 是用户在对话框中的**候选**，只在 `onAccepted` 瞬间被消费；只有整条 parse→analyze→adapt 管线**全部成功**后，`sourceLabel_` 才被赋值为**文件名**（cpp:1314-1316，且完整路径永不进 UI）。因此 B4 **不得**把 selected path 当作 current session source——失败选择不留任何会话痕迹（r03/r05），把它当 source 会破坏这一契约。

### 38.7 Replay 产品边界（§7 四案比较）

| 方案 | User workflow | Migration risk | Duplication | 1000×700 | Dashboard ownership | B5/M9-C |
| --- | --- | --- | --- | --- | --- | --- |
| A 仅 Load action | 有动作无结果反馈 | 最低 | 无 | 最松 | 无冲突 | 需补 |
| **B Load + error/notice（推荐）** | **加载→看到结果/披露，闭环** | **低（三件套整体平移）** | 无 | 松 | 无冲突 | 好 |
| C 再加 statistics | 越权 | 高 | **统计第三视图** | 中 | 冲突（Dashboard 已有） | 差 |
| D 再加 transactions | 表的主场未到 | 高 | **事务第二视图** | 紧 | 无冲突 | 差（B5/M9-D 决定） |

**结论：B**。真实代码核对：error/notice 本就是 load 工作流的结果披露（r03-r06），与动作同源同迁；统计/事务已有归属（§34.6），不重复。

### 38.8 Dashboard / Replay / Legacy 边界（§8）

Statistics → Dashboard（+Legacy 迁移期实例）· **Replay load workflow（按钮+对话框）→ Replay** · **Replay error/notice → Replay**（其真实语义=load 操作结果披露，见 §38.3）· **Transactions → Legacy（B4 不动，B5/M9-D 再议）** · Diagnosis → Legacy（B5）。

### 38.9 FileDialog Ownership（§9 比较与推荐）

| 维度 | A：FileDialog 放 ReplayPage 内（推荐） | B：留在 Shell/Main，页面仅触发 |
| --- | --- | --- |
| lifetime | 随页常驻（StackLayout 全实例化——B2/B3 已证页面 identity 稳定）→ 对话框实例零重建 | 常驻窗口级 |
| page locality | 工作流三件（按钮/对话框/结果披露）同页自洽 | 按钮在页、对话框在壳，跨文件 id 引用 |
| navigation | 打开中的对话框属瞬时 UI 态，切页即关闭——可接受（无业务状态丢失） | 同 |
| maintainability | 一处阅读全部 workflow | 需两处 |
| state preservation | selectedFile 瞬时语义不变（§38.6） | 同 |
| testability | 页面暴露同样的 open() 语义；CLI 仍走 Controller 命令 | 同 |

**推荐 A**。依据：StackLayout 常驻实例使对话框随页常驻安全（非 Loader，无销毁重建）；且 Legacy 的 onAccepted 接线可逐字平移。**不凭感觉**：B2/B3 的 identity 断言机制可直接覆盖（对话框作为 page 子对象的存续由页面 identity 证明）。

### 38.10 Load 命令映射（§10）

| 项 | 真实内容 |
| --- | --- |
| Trigger | Load Replay 按钮 → `replayFileDialog.open()`；`onAccepted(selectedFile)` → 命令 |
| URL→本地路径 | `fileUrl.toLocalFile()`（非本地 URL 直接报错，cpp:1230-1233） |
| Controller command | `loadReplayFile(const QUrl&)`（Q_INVOKABLE） |
| Preconditions | QML 侧**无**（按钮恒可用）；C++ 侧全量校验（§38.3 步骤 1-4） |
| 成功副作用 | notice 设置/清除、串口 teardown（SB-13）、批次三视图发布、AI 失效、mode/source 切换、清 replay+serial 错误 |
| 失败副作用 | 仅 `setReplayError(...)`；notice **不触碰**；其余全保留 |
| Source transition | 仅成功时（回放模式 + 文件名） |
| Transaction/statistics | 仅成功时整体替换（原子） |
| Diagnosis/AI invalidation | 成功时 `invalidateAiForBatchChange()`（batchRevision 递增） |
| Existing tests | `r01_goldenReplay` / `r02` / `r03_parseErrorPreservesState` / `r04_executionError` / `r05_fileOpenFailure` / `r06_errorRecovery` / `r07_sourceReplace` / `r08_clearKeepsSource` + core `a01-a12` / `i01-i05` |

### 38.11 原子性/失败语义（§11，Observed Current Contract——B4 冻结不改）

- 失败 ⇒ **旧 source 保留** + **旧 transaction collection 保留** + **旧 statistics 保留** + `hasReplayError` 置位。
- 失败 ⇒ **notice 不清**（notice 属于上一次成功加载的披露）。
- 成功 ⇒ 整批原子替换 + notice 重设/清除 + replay error 清除 + serial error 清除 + 串口关闭（仅成功）+ source 切换。
- **future issue candidate（记录不修）**：失败不清 notice 的语义意味着"旧成功披露 + 新失败错误"可同时可见——这是**当前真实契约**（r06_errorRecovery 依此测试），B4 原样保留；是否改进由后续任务评估。

### 38.12 Per-record Passive 契约（§12，B4 冻结清单）

| 契约 | 真实证据 |
| --- | --- |
| per-record 被动分析（逐条独立，互不污染） | `ReplayAnalysis.h`（注释明言 per-record；`ReplayBatchAnalysis.transactions` 逐条 outcome）+ core `i01/i02` |
| Unsupported function 行为 | `unsupportedRecords` 披露（T015 Gate F）+ core `i03*` 族 + sample `t015_unsupported_fc08.mlog` |
| ExpectedNoResponse | `demo_v1.mlog` 第 4 笔 NO_RESPONSE + ui_bridge `r02`/统计语义（ADR-003） |
| requestIssues / response issues | `ReplayTransactionOutcome.requestIssues` + `composeIssueText`（cpp:1277）+ `t02/t03/t05` |
| bad record isolation | parse 层整体报错（结构性错误），分析层 per-record 隔离（`i04_badResponseCrc`/`i05`） |
| Statistics eligibility | 仅 analyzed 记录计入（`unsupportedRecords` 不计入，cpp:1289-1299）+ STAT-* |

**ReplayPage 只消费 load 结果 / 既有 state**，不重实现任何一层。

### 38.13 Sample / Offline 可测性（§13）

- **8 个 tracked samples**（samples/）：`demo_v1`（golden 4-outcome）· `demo_v2` · `t014_protocol_error` · `t014_t015_summary` · `t015_broadcast` · `t015_invalid_request` · `t015_partc_final` · `t015_unsupported_fc08`。
- **既有 repo-relative fixture 机制**（无绝对路径）：CMake 把 `samples/demo_v1.mlog` 复制到 `${CMAKE_CURRENT_BINARY_DIR}/test_data/` 并以 `MODBUSLENS_DEMO_MLOG_PATH` 编译定义暴露（CMakeLists:252/267/280/435）——nav check 可完全复用该模式（为失败/notice 场景增补第二个/第三个 define，如 `MODBUSLENS_UNSUPPORTED_MLOG_PATH`）。
- **判定**：无需用户机器绝对路径即可稳定执行——成功加载（demo_v1，4 笔确定性批次）、失败加载（**不存在的文件路径** → "无法打开文件"，无需新 fixture）、notice 披露（t015_unsupported_fc08，若需要第二个 fixture）。

### 38.14 导航持久化场景（§14）

- **Scenario I**：已有 simulator 批次 → 激活 Replay → Dashboard → 全字段快照不变（navigation ≠ source transition；H 同族）。
- **Scenario J**：Replay workspace 内**显式** `loadReplayFile(demo_v1 fixture)` → 成功 → `modeLabel=回放模式`、`sourceLabel=demo_v1.mlog`、observed=4 → 切 Dashboard/Communication/Replay → 事实逐值保持（注意：**激活页面不触发加载**；加载由场景显式命令完成）。
- **Scenario K**：存在既有批次 → 显式 `loadReplayFile(不存在的文件)` → 失败语义（§38.11）→ 三页往返 → error 与全部旧事实保持。
- （可选增强：unsupported sample 的 notice 断言——fixture 就绪后纳入。）

### 38.15 FileDialog 可测性（§15）

Native FileDialog **不自动化**（不为 B4 引入 GUI automation）。等价验证路径：场景 J/K 直接调用 Controller 的 `loadReplayFile`（真实命令、真实语义）；`onAccepted` 的 URL→命令接线由**人工验收**覆盖；对话框 cancel 由人工覆盖。不伪造对话框行为。

### 38.16 ReplayPage Ownership（§16）

与 B2/B3 一致：`required property var analysisController`（显式注入）；页面拥有 **FileDialog presentation/workflow state**（对话框实例、打开动作）；**不得拥有** transaction copy / statistics copy / Replay parser / source authority。

### 38.17 Replay Workflow Persistence（§17）

- **保留**：对话框实例（页面常驻）；无业务性 workflow 状态需要跨页保留（"选了但没加载"的路径不产生任何事实——§38.6）。
- **不保证**：未接受的对话框选择在切页后仍存在（平台对话框自身行为，非产品契约）。
- **禁止**：把 path 写进 Controller 以实现 persistence（§38.6 边界）。

### 38.18 Replay Error / Notice Placement（§18）

两者**归 Replay Workspace**（load 操作结果披露）。AppBar 继续只做 session/source 简洁显示。清除语义归 Controller：error 在成功加载或 clearResults 时清除；notice 在成功加载时重设/清除、失败时保持（§38.11——当前真实契约）。

### 38.19 Legacy after B4（§19）

Legacy 剩：**StatisticsOverview(legacy) → 分隔线 → SplitView(诊断|事务)**。Load Replay 按钮、对话框、error/notice 三件全部移出 → demo-controls 行整体消失。布局检查：无空洞（SplitView 净增 ~34px 行高 + 释放顶部空间）；允许最小注释/间距收口；**不提前迁 Diagnosis/Transactions、不删 Legacy**。

### 38.20 导航模型（§20）

契约扩展：`workspaceReplayIndex: 3`（Main.qml 集中定义）。启用：工作台/总览/通信/**回放**；保持 disabled：诊断(4)/设备(5)。**不重排**；rail 顺序、StackLayout 顺序（child3 = ReplayPage）、测试三方共读契约。

### 38.21 qml_nav_check 扩展（§21）

现有机制扩为 **4 个真实页面**：路径 `Legacy → Dashboard → Communication → Replay → Dashboard → Legacy`（5 次切换）；逐站：identity（4 指针）/ visibility / index / 禁用项（4/5）/ 全字段快照（Scenario I 融入）；Scenario J/K 以 fixture 命令调用落地（§38.13）；**旧覆盖（A/B/D/E/F/G′/H）一个不减**。

### 38.22 Geometry Guard（§22）

**8 趟**（4 workspace × 2 size），替换现 6 趟；Replay active 时断言：页面 non-zero + 动作区（`replayActions`）non-zero + 内容不越界；error/notice **仅 visible 时**做非零检查（hidden 不假设）。旧 6 趟的全部断言保留（按可见页分派的规则沿用）。

### 38.23 1000×700 布局预算（§23）

```text
Replay 页内容 ≈ 页标题 21 + 12 + 动作卡（34 + 内边距 24）58 + 12 + 错误/notice（可见时 12-34）
             ≈ 140-160px → 1000×700 可用 627 → 余量 ≈ 470px
```
**记录即可**：余量为未来 workflow 增长空间（批次元信息/结果区），**不为填空搬 statistics/transactions**（B4 拒绝 C/D 案的同一理由）。结构采用与 Communication 相同的**尾部弹性 spacer**（§37.7 经验的直接复用）。

### 38.24 Accessibility（§24）

回放 nav：mouse/Enter/Space 同 `activate()`；切页焦点留 rail 项（B2/B3 先例）；Load Replay 按钮键盘可达（AppButton StrongFocus）；FileDialog 用 Qt Quick Dialogs 当前键盘行为（不改造）；busy/disabled 语义沿用（Load 恒可用现状冻结）。

### 38.25 V1/V2 契约风险清单（§25）

| 契约 | 真实证据 | B4 红线 |
| --- | --- | --- |
| Replay 成功加载语义 | cpp:1226-1320 + `r01/r02/r07` | 三件套（按钮/对话框/披露）整体平移；接线逐字 |
| 失败原子性 | `r03/r04/r05/r06` + cpp:1230-1260 | 失败路径零改动（含 notice 不清的现状契约） |
| per-record / Unsupported / ExpectedNoResponse | §38.12 表 | ReplayPage 零重实现 |
| statistics | STAT-* + `a01/b01` | 不加第三视图 |
| TransactionModel | `a03-a06`/`t01-t05` | 不迁移不复制 |
| source switching | `r07`/`s02/s07` | 只有显式成功加载才切 source |
| diagnosis invalidation | `d02/d03`/`ai06` | 导航不触碰 |
| AI/Agent revision | `ai06/ai08` | 同上 |
| Dashboard | B2 场景断言 | 统计不受影响 |
| Communication draft | B3 Scenario F | 不受影响 |
| nav persistence | `qml_nav_check` 全场景 | 三页场景全保留 + 扩四页 |
| geometry guard | `qml_geometry_check` | 六趟→八趟；旧断言保留 |
| minimum size | 1000×700 | §38.23 预算 |

### 38.26 增量实施计划（§26）

- **B4.1 — ReplayPage + 机械 MOVE + 启位**（单提交）：`pages/ReplayPage.qml`（页根纯 Item + 标题 + 动作区[Load Replay + FileDialog 逐字] + error/notice 行 + 尾部 spacer）；Legacy 同步删除三件（按钮/对话框/两行披露）；`workspaceReplayIndex: 3` + rail 启位 + StackLayout child3；geometry 八趟 + NAV 矩阵四页。验证：build/smoke/geometry/nav/ctest 26/diff-check。
- **B4.2 — Scenarios I/J/K**：fixture define（CMake test_data 复制扩展）+ nav check 增补 J/K（I 已含于逐站快照）。验证：ctest 26（断言扩展）。
- **B4.3 — Deploy / screenshots / manual candidate**。
- 每步可 build/test/rollback；**move first、behavior preservation first**；不 big-bang。

### 38.27 测试计划（§27）

- Baseline **26** 全保留；**不新增独立 target**（nav/geometry 内扩，避免测试机制碎片化）；fixture 全部走 CMake test_data 机制（repo-relative）。
- 人工（两尺寸）：回放 nav 与选中 / Load 按钮 / FileDialog 选择 / 加载成功（**使用 sample**）/ 加载失败 / error/notice / source chip（回放模式 · 文件名）/ Dashboard·Communication·Legacy 事实不变 / disabled 诊断·设备 / resize。**人工不得把 sample replay 宣传为真实现场设备数据。**

### 38.28 Knowledge Before Implementation（§28，10 项对应真实问题）

| # | 知识点 | 解决 ModbusLens 的哪个真实问题 |
| --- | --- | --- |
| 1 | Workspace workflow vs session source | 防止"进入回放页"被误当"加载了回放"（B4 invariant 的来源） |
| 2 | selected file vs loaded source | 失败选择必须零痕迹（r03/r05 的契约延展到 UI 层） |
| 3 | successful command transition | 回放 source 只能由成功管线建立（r07）——迁移期不得新增旁路 |
| 4 | failure atomicity | r03-r06 的既有契约在 UI 搬家时必须原样存活 |
| 5 | workflow state vs business state | FileDialog/selectedFile 是候选不是事实，避免污染 Controller |
| 6 | FileDialog lifetime | StackLayout 常驻使对话框随页安全常驻（B2/B3 identity 证据支撑） |
| 7 | passive replay analysis | ReplayPage 只消费结果——防止页面重实现分析（违反分层） |
| 8 | per-record fault isolation | unsupported/bad record 的披露语义（Gate F）在 UI 搬家时逐字保持 |
| 9 | migration vs behavior rewrite | notice 不清等"奇怪"现状契约原样冻结，改进另立任务 |
| 10 | offline fixture design | CMake test_data 机制让场景 J/K 无绝对路径、可复现 |

### 38.29 Status（经 §38.30 修正后为 authority）

- **M9-B4 Phase 1 = Learning / Design 完成；Review = CONDITIONAL PASS；§38.30 修正已落档；Implementation = NOT STARTED。**
- 下一步：**B4 序列修正（§38.31）复核（用户）**；批准后按 **§38.31.2/§38.31.3**（B4.1 仅 shell（不迁移不启位）→ B4.2 迁移+启位同阶段 → B4.3 场景 I/J/K/K′+八趟 → B4.4 deploy）开始。§38.5/§38.11/§38.13/§38.14/§38.26 与 §38.30 冲突处以 §38.30 为准；**§38.30.5 的步骤定义由 §38.31 取代**（可达性修正）。### 38.30 Phase 1 Review Correction（CONDITIONAL PASS → docs-only 修正，2026-09-16）

用户 Review = **CONDITIONAL PASS**。本节为 **追加批注**，修正 §38 中被指出的四处表述/计划；与 §38.5/§38.11/§38.13/§38.14/§38.26 冲突之处**以本节为准**。零生产改动。

#### 38.30.1 状态分类修正（ownership ≠ semantic category）

原 §38.5 把 replayError/replayNotice 放进 "A. Authoritative Session State"（理由="Controller 持有"）——**分类错误**：所有权的归属不等于语义类目的归属。修正后的三类：

- **A — Authoritative Session Facts（会话事实）**：mode/source · transactions · statistics · diagnosis 批次 + revision（如经核实，其他真实批次事实）。
- **B — Replay Workflow / Draft**：FileDialog 实例/工作流状态 · selectedFile（瞬时候选）。
- **C — Replay Result / Disclosure State（结果/披露状态）**：`replayError`（**per-attempt**：描述最近一次加载尝试的失败）· `replayNotice`（**per-loaded-session**：见 38.30.2）。

C 类仍由 Controller 持有（ownership 不变、B4 不改），但其语义是"操作的披露"而非"会话批次事实"——呈现归 Replay 页的结论不变。

#### 38.30.2 replayNotice 生命周期结论（按真实代码）

全部真实调用点（grep 实证）：

| 事件 | 行为 | 代码 |
| --- | --- | --- |
| 成功加载且 unsupported 非空 | `setReplayNotice("提示：N 条…")` | cpp:1292 |
| 成功加载且 unsupported 为空 | `clearReplayNotice()` | cpp:1290 |
| `runDemoBatch()` | `clearReplayNotice()` | cpp:1205 |
| `clearResults()` | `clearReplayNotice()` | cpp:1222 |
| **失败加载（全部四类）** | **不触碰 notice** | cpp:1230-1260（无任何 notice 调用） |

**结论**：replayNotice 描述的是**当前已成功加载的 Replay 会话**中 unsupported 记录的披露（T015 Gate F，与当前活跃批次绑定）→ 应记录为 **Controller 持有的 per-loaded-session disclosure**，而非"最近一次加载尝试的结果"。replayError 才是 per-attempt。二者生命周期不同——这是 C 类内部必须区分的两个子语义。

#### 38.30.3 UI-R06 证据表述修正

**撤销**原 §38.11 的这一句："notice 不清…被 r06 依此测试"。**UI-R06（r06_errorRecovery）的实际断言**（tests/test_ui_bridge.cpp，已复读原体）：坏加载 → `hasReplayError` 为真；golden 加载 → 错误清除 + mode/source/rows/observed 断言——**没有任何 notice 断言**。r06 证明的是 error 的恢复路径，不证明 notice 的失败存续。

**"失败加载保留 notice"是 production code 的观测行为**（cpp:1230-1260 失败路径无 notice 调用——见 38.30.2 表），B4 冻结该观测行为，但**补上此前缺失的自动断言**（38.30.4）。

**future-issue 措辞修正**："旧 notice + 新 error 并存"**不是自动成立的 bug**——失败加载没有替换旧会话，notice 仍准确描述着仍然活跃的旧批次。潜在的未来 UX 议题是**旧会话披露与新尝试错误之间的来源/上下文清晰度**（用户可能分不清"哪句话属于哪个会话"），仅此而已。

#### 38.30.4 新增 notice-preservation 自动断言（B4.3 计划）

由于"notice 在失败加载后保留"进入 B4 冻结契约，**notice-bearing fixture 不再可选**：

- **Fixture 决定**：`samples/t015_unsupported_fc08.mlog`（tracked；单条 FC08 记录 → 成功加载后 notice="提示：1 条记录…0x08…"、analyzed=0）。
- **机制**：沿用既有 CMake `configure_file(... COPYONLY)` + 编译定义模式，新增 `MODBUSLENS_UNSUPPORTED_MLOG_PATH`（与 `MODBUSLENS_DEMO_MLOG_PATH` 同法）；**不新增 test target**（断言进 `qml_nav_check` 场景），**零机器绝对路径**。
- **Required scenario（Scenario K′，并入 nav check）**：
  1. `loadReplayFile(unsupported fixture)` 成功 → 捕获 source/rows/statistics/replayNotice；
  2. 确定性失败加载（不存在路径）→ 断言：`hasReplayError` 置位 + **source 不变 + rows 不变 + statistics 不变 + replayNotice 逐字不变**；
  3. 三页往返 → 全部保持。
- 原场景 I/J/K 保持不变。

#### 38.30.5 恢复增量实施边界（B4.1–B4.4）

修正 §38.26 的两步合并（原 B4.1 把"迁移+启位"绑在同一步）。恢复为：

- **B4.1 — ReplayPage shell + 机械 MOVE**（Load 按钮/FileDialog/error/notice 三件套逐字迁入；**回放 navigation 保持 disabled**；Legacy 同步删除）。验证：build + qml_smoke + 行为保持（此时 Replay 页不可达，无导航面变化）。
- **B4.2 — 启位**：`workspaceReplayIndex: 3` + rail 启用 + StackLayout child3 + 四页 identity/visibility/index/基础导航矩阵。
- **B4.3 — 场景**：Scenario I/J/K + **K′（38.30.4）** + fixture 接线 + 八趟 geometry。
- **B4.4 — deploy + 截图 + manual candidate**。

原则不变：move first、behavior preservation first、no big-bang。

#### 38.30.6 Context-decay / model-switch 纪律（实施前强制）

模型可能在 GLM-5.3-Flash / GLM-5.3 / DeepSeek-V4.1-Flash 间切换——**既往模型摘要不作为权威**。B4 实施前，当值模型必须从仓库重读（至少）：`AGENTS.md` · `docs/PROJECT_STATUS.md` · `docs/BACKLOG.md` · **T017 §38（含本修正节）** · `AnalysisController.h/.cpp` · 当前 `Main.qml` · `qml_nav_check` 实现与测试 · `qml_geometry_check` 实现与测试 · 相关 ui_bridge Replay 测试（r01-r08 原体）· CMake replay fixture/test-data 机制 · tracked replay samples。**改代码前报告实际证据**（本轮 correction 已示范：r06 原体复读、notice 调用点 grep）。### 38.31 Implementation-Sequencing Correction（B4.1/B4.2 可达性修正，2026-09-16）

用户复核指出 §38.30.5 的排序存在**实施安全冲突**。本节为追加批注，**取代 §38.30.5 的步骤定义**；§38.30 的语义结论（38.30.1–38.30.4、38.30.6）全部保持有效。零生产改动。

#### 38.31.1 可达性冲突（确认与定性）

§38.30.5 曾定义 "B4.1 = 机械 MOVE（Replay navigation 保持 disabled）"——该顺序**不安全**：把 Load Replay / FileDialog / error / notice 从 Legacy 移走的同时，新的 ReplayPage 尚不可达 ⇒ **用户可访问的加载工作流出现"暂时消失"窗口**。

这违反 M9-B 自 B1 起一贯的增量迁移不变量：**每一个已提交的步骤都必须保持既有能力可用；任何提交都不得造成能力暂时消失**。既有正确先例即 B3.1（串口三件套迁移与"通信"启位在**同一提交**内完成，正是因为单独迁走会造成不可达窗口）。

#### 38.31.2 修正后的 B4.1 = ReplayPage shell only（不迁移、不启位）

- **只做**：创建/注册/实例化 `pages/ReplayPage.qml`（页根为纯 Item，`required property var analysisController`；StackLayout 增加 child3，但**回放 navigation 仍 disabled**）。
- **明确不做**：**不迁移** Load Replay 按钮 / FileDialog / replay error / replay notice——四件套继续完整保留在 Legacy，用户能力零变化。
- **验证**：build + `qml_smoke` + **隐藏 ReplayPage 安全实例化**（页面对象存在且实例化无副作用；隐藏页几何为 0×0 属正常——按既有规则**不对隐藏页做几何断言**，仅可做存在性断言）+ 既有功能不变（Legacy 的 Load Replay 仍可用；r01–r08 等既有测试全绿）。
- **无行为变化**是这一步的验收标准。

#### 38.31.3 修正后的 B4.2 = 原子工作流迁移 + 启位（同一实施阶段）

在**同一阶段/提交**内完成，不得拆分：

1. **机械 MOVE**：Load Replay 按钮 / FileDialog / replay error / replay notice 四件套 Legacy → ReplayPage（`onAccepted → loadReplayFile(selectedFile)` 接线与既有语义逐字保持）；
2. **同时启用**：`workspaceReplayIndex: 3`（集中契约）+ rail `回放` 启位 + StackLayout child3 生效；
3. **同时扩展**：四 workspace 基础导航断言（identity / visibility / index；disabled 仅剩诊断(4)/设备(5)）。

**禁止提交任何"工作流已迁走但 Replay workspace 不可达"的中间状态**（该状态即 §38.31.1 所禁的能力消失窗口）。

#### 38.31.4 B4.3 不变 + K 与 K′ 的职责区分

- **B4.3 保持**：Scenario I / J / K / K′ + 必需 fixture 接线（`MODBUSLENS_UNSUPPORTED_MLOG_PATH`）+ **八趟 geometry**。
- **K 与 K′ 互补、不可互相替代**：
  - **K = 普通失败加载的原子性**：针对**存在非空会话**（如 demo 批次或 demo_v1 加载后的 4 笔）执行确定性失败加载 → 断言 `hasReplayError` 置位且 source / rows / statistics **逐值不变**（"不变"只有在会话非空时才有意义）。
  - **K′ = replayNotice 生命周期**：用**必需的** unsupported fixture 先成功加载（notice 置位、analyzed=0）→ 捕获 notice 文本 → 再执行确定性失败加载 → 断言 error 置位且 **notice 逐字不变**（并保持 source/rows/statistics）→ 页面往返后全部保持。
- **B4.4 保持**：deploy + 截图 + manual candidate。

#### 38.31.5 §38.30 已批准语义（冻结，不重开）

以下结论**全部保持有效、不在本次修正中重开**：workspace ≠ source · selected file ≠ loaded source · 成功加载是唯一的 Replay source transition · 失败保留旧会话事实 · `replayError` = per-attempt · `replayNotice` = 当前已发布 Replay 结果/会话的披露（per-loaded-session）· FileDialog 迁移后归 ReplayPage · ReplayPage 不持有任何业务/会话副本 · Replay Core 零改动 · Statistics 归 Dashboard · Transactions 在 B4 期间归 Legacy · Diagnosis 归 B5。

#### 38.31.6 Status

- **M9-B4 Phase 1：语义修正（§38.30）= PASS；序列修正（§38.31）= 本节；Implementation = NOT STARTED。**
- 下一步：**等待本序列修正的复核批准**；批准后从 **B4.1（仅 shell、不迁移、不启位）**开始，随后 **B4.2（迁移+启位同阶段）** → **B4.3** → **B4.4**。


## 39. B4.1 — ReplayPage Shell Only（Implementation Record，2026-09-16）

按 §38.31.2 执行（用户批准范围）：**仅 shell，零迁移、零启位、零行为变化**。

### 39.1 Implementation

- 新建 `pages/ReplayPage.qml`：纯 Item 页根（B1–B3 已验证模式）+ `required property var analysisController`；**页面体刻意为空**（B4.2 随迁移+启位原子填充——§38.31.3 禁止"已迁移但不可达"中间态，故本步不迁任何工作流件）。
- `Main.qml`：StackLayout **child3** 实例化（`objectName: "replayWorkspace"`，与 legacy/dashboard/communication 同法注入 controller）；回放 nav **保持 disabled**（NavigationRail 零改动）；**未添加** `workspaceReplayIndex`（实例化不需要它——按 §38.31 指令，仅 B4.2 启位时随契约引入）。
- `CMakeLists.txt`：QML_FILES 注册（qt_add_qml_module + 生成模块部署，无手写 qmldir）。
- `main.cpp`（仅存在性验证，无行为断言）：nav check stage 0 增加 `replayWorkspace` shell 存在断言 + 跨切换 identity 稳定检查（与其余三页同机制）；dump 表加入 `replayWorkspace`（**纯信息性**——隐藏页 0×0 按规则不做几何断言）。

### 39.2 Verification（真命令 + 真输出）

```text
build（debug-local）              → Linking modbuslens.exe（干净）
qml_smoke                         → EXITCODE=0
qml_nav_check                     → EXITCODE=0；PASS（shell 存在断言含于 stage 0；
                                    禁用回放激活不可切 index 由既有 3..5 循环覆盖）
qml_geometry_check                → 六趟 PASS；replayWorkspace 六趟均 0×0（仅信息性记录，未断言）
full ctest                        → 26/26（含 Replay r01–r08 所在的 ui_bridge 与全部既有目标）
git diff --check                  → 通过
```

**零行为变化证明**：nav check 的全字段业务快照逐站比较全部通过（ReplayPage 的存在未改变任何 authoritative 值）；r01–r08 原体已复读且全绿（ui_bridge 26/26 内）；Legacy 的 Load Replay/对话框/error/notice 四件套未动（Main.qml 仅**新增** child3，未删除任何行——`git diff` 可证）。

### 39.3 Files Changed

- 新增：`src/ui/qml/pages/ReplayPage.qml`（13 行有效内容：页根 + required 注入 + 说明注释）。
- 修改：`CMakeLists.txt`（+1 注册行）、`src/ui/qml/Main.qml`（+9 实例化块）、`src/main.cpp`（nav check 存在性/identity/dump 三处最小扩展）。
- 未动：AnalysisController、Replay Core、NavigationRail、Serial、statistics、Diagnosis、AI/Agent、samples、scripts。

### 39.4 Candidate Commit

见 §40 回填（B4.1 提交；不 push、不推进 LKGC——待 B4.2/B4.3/B4.4 与人工验收）。

## 40. Next

- **B4.2 — 原子工作流迁移 + 启位**（§38.31.3）：四件套逐字 MOVE + `workspaceReplayIndex: 3` + rail 启位 + 四 workspace 基础导航断言（同一提交内完成，禁止"已迁移但不可达"）。
- **等待用户 GO**；本轮不开始 B4.2。