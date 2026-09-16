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

- B2 的三个实施提交：`c4291db`（B2.1）、`93aabb2`（B2.2）、`53685d5`（B2.3）；B2.4 的截图/文档随候选提交（哈希见 §32 回填）。
- 全部**不 push、不推进 LKGC**（verified 保持 `189c62c`，待人工视觉 PASS 后再议）。

## 32. Next

- **M9-B2 Manual Visual Review = PENDING USER REVIEW**（四张截图 + deploy 候选；清单 §31.8）。
- PASS 之前：不推进 LKGC（保持 `189c62c`）、不 push、不开始 B3（Communication extraction）。
