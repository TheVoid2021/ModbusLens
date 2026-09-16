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