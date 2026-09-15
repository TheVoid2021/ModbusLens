# T016 — M9-A UI Foundation / Design System（Learning & Design）

- **Goal**          彻底理解当前真实 UI 架构，建立 ModbusLens V2（M9 UI/UX Refresh）的 Design System 设计与可复用组件方案，并让开发者本人掌握相关 Qt/QML 知识——**不实现 UI**。
- **Background / User Value**：V1 的 Main.qml 为单文件 934 行承载全部界面：样式散落（23 种不同 hex 颜色、21 处 spacing、21 处 pixelSize、14 处 radius 字面量），组件零复用（13 个 Button 各自内联样式），顶部诊断 TabBar 与整体单页 section 布局并存；后续 M10（Active Master）、M11（Register Decode）、M12（Device Profile/Manual Q&A）将显著增加信息密度与交互复杂度——没有 Design System 与 Navigation 规划，每加一屏都是复制粘贴与样式漂移。
- **状态**           **T016 = IN PROGRESS；Phase 1（Learning & Design）= DONE / AWAITING REVIEW；Implementation = NOT STARTED。**

## 1. Preflight 事实（2026-09-14）

- HEAD=`a40d935`（main，clean）；`git tag --list`=`v1.0.0`，`git rev-parse v1.0.0^{}`=`ae067ab7e…` ⇒ **V1 tag 已存在且指向 verified LKGC `ae067ab`**；`git describe --tags --always`=`v1.0.0-8-ga40d935`。
- 视觉基线：`docs/assets/screenshots/v1-ui-baseline.png` 已由用户放入（untracked）——本阶段将其**纳入 tracking、不改动像素内容**，作为 M9-F Before/After 对比基线。

## 2. Current UI Architecture（真实代码重建，非截图推断）

```text
QGuiApplication（main.cpp：Fusion style / 组织与应用名 / 无 setWindowIcon / --qml-smoke-test）
  └─ ApplicationWindow（Main.qml，唯一天文件 934 行；no named Window/toolbar chrome 定制）
       ├─ root property 组（surface/surfaceAlt/border/textPrimary/textSecondary/… + 表格列宽比例 token，共 21 个 property）
       ├─ ColumnLayout（整窗单一纵向流）
       │    ├─ Top Actions Row（Run Demo / 加载回放 / 刷新串口 / 清空结果 / 断开 + FileDialog）
       │    ├─ Replay Error Label + Replay Notice Label（条件可见）
       │    ├─ Serial Controls GroupBox（从站地址/寄存器数量/波特率 SpinBox×2+ComboBox / 单次读取 Button）
       │    ├─ Statistics Cards（7 张计数卡 + 成功率 + 平均延迟，Repeater 驱动）
       │    ├─ Diagnosis Workspace SplitView（左：诊断 TabBar（基线诊断 / AI 解释 / Agent 问答，3 TabButton）+ 内容；右：最近通信记录 ListView（5 列静态表头 + T014/T015 双行 detail））
       │    └─ （Agent 输入/回答区在 Diagnosis 左 pane 内）
       └─ （无顶部 Navigation；无 ToolBar；无 ScrollView；无 TextField（QML 侧 0 个））
```

回答 §1 的 A~J：
- A. Main.qml 职责 = 全部：窗口、所有 section、复用 token 的雏形（root properties）、诊断页内 TabBar、事务列表 delegate。
- B. 页面/section = 单一纵向单页（Top Actions → Serial → Statistics → Diagnosis Workspace → Transactions 表），**当前无“页面”概念**。
- C. 现有 navigation：**无顶层导航**；仅 Diagnosis workspace 内有 3 个 TabButton（基线/AI/Agent）。T013 的“TabBar 三页”实际指此诊断页签，不是应用导航。
- D. reusable components：**0 个**（唯一的非控件对象是 AnalysisController 与 FileDialog；button/tab/card 样式全部内联重复）。
- E. 样式机制 = **散落式为主 + 最小 token 雏形**：root property 提供 surface/border/text 色与列宽，但 13 个 Button、TabButton background/contentItem、GroupBox、卡片 border 大量内联。
- F. 硬编码颜色：`#[0-9A-F]{6}` 出现 **35 次、23 种不同值**（未计 error/warning 专用色与全 zero 语义化的状态色）。
- G. spacing 字面量 **21 处**；font.pixelSize **21 处**（11/13/18/20… 多种字号混合）；radius **14 处**（3/4/6 混用）。
- H. Qt Quick Controls Style = **Fusion**（T013 Phase E 定案：原生 Windows style 会忽略自定义 background/contentItem；文档留痕于主代码注释）。
- I. Layout vs anchors：主体 **ColumnLayout/RowLayout（9+12）+ SplitView**；anchors 用于局部居中与空态；ListView delegate 内为手工 Row+width 计算（表格列宽由 root 属性共同派生——T013 “single geometry owner” 原则）。
- J. 窗口：默认 **1024×720**、最小 **1000×700**（T013 Policy：mins 保护最小窗》。

## 3. Current UX Problems（结合真实 QML + v1 截图，逐项具体化）

1. **Visual hierarchy**：16 个“卡”（统计卡+面板）视觉同级；无 SectionHeader，用户无法快速定位“串口操作在何处结束、诊断从何处开始”。
2. **Spacing**：21 处 spacing 字面量、多档混用（0/2/4/6/12），同一类元素间距不一致（TabButton 间距 vs 卡片间距）。
3. **Typography**：21 处 pixelSize 且档位混杂（13/18/20 等），无字号梯度定义；异常码/次要信息用色不统一。
4. **Buttons**：13 个 Button 各自内联样式与 `palette.buttonText` 修正（历史坑：原生 palette 被 style 忽略），无主/次/危险/图标按钮分化——用户无法凭外观判断动作权重。
5. **Input controls**：Serial 参数 SpinBox/ComboBox 无统一 label-对齐模式（`从站地址/寄存器数量/波特率` 行高与内边距各异）。
6. **Cards / sections**：统计卡、Serial GroupBox、诊断区边框混用 surfaceAlt/surface/border 三色，卡片层级感弱。
7. **Status colors**：状态文案靠文字本身（`statusText`）表达；颜色仅有 error/warning 少量语义且散落——颜色是辅助，但当前**辅助也无系统**。
8. **Navigation**：无顶层导航；未来 M10~M12 内容无处安放，只能继续加长单页（信息密度失控风险）。
9. **Information density**：Single column 全部堆叠，820px 高以内可见内容不足（顶部操作+串口+统计就占去大半屏，事务表几乎不可见）。
10. **Transaction table readability**：行高 36/64px 两档 + 静态 5 列表头 + 手调列宽比例（deviceColumnWidth 等），诊断详情双行靠 delegate 高度条件切换，密度与对齐方式单一。
11. **Diagnosis workspace**：SplitView 左 pane 无 scroll 容器（历史 ISSUE-004 溢出教训），三 Tab 内容区高度受限；Right pane 表格无列排序/无 hover 行态。
12. **Serial controls**：控件 enable 逻辑（busy/connected）工程正确，但“Reading…”状态与 disabled 视觉区分弱。
13. **Empty states**：仅“暂无通信记录”一条；串口无端口（“未检测到串口”）与无诊断（“尚未运行基线诊断”）文案样式不统一。
14. **Error / disabled / busy**：错误两 Label 颜色硬编码（#B03030/#806000）；busy/disabled 无通用视觉语言（SpinBox 禁用态依赖控件默认）。
15. **Window/application icon**：窗口左上与 taskbar 图标缺失——见 §6 icon 调查（根因已证明）。
16. **taskbar icon**：同上，exe 无嵌入图标资源。
17. **原生标题栏与内容区视觉割裂**：原生深色 chrome（系统主题）与浅色内容区不一致；T013 曾记录原生 style 问题——Fusion 解决控件渲染，但**窗口 chrome 仍原生**。

## 4. Product Design Direction

**Modern Industrial Diagnostic Workbench**：专业、克制、清晰、现代、偏工程软件；非炫技 Dashboard / 非网页 SaaS 套壳 / 非游戏 UI。
- Light-first UI；主色 **深蓝 / 青蓝**；Neutral = 灰白 surface/background。
- Status semantic colors（Success/Exception/CRC Error/Timeout/Protocol Error/Expected No Response/Pending）——颜色永远只是**辅助通道**，必须同时保留文字（状态名）与图标（后续）表达；不得出现“只有颜色能区分”的状态。
- 布局节奏：SectionHeader + Card + 固定 tokens；信息密度按 workspace 分区管理。

## 5. Design Token Strategy（本阶段只设计）

概念先行定义（开发者学习，见表 §7）：
- token 类别：Color / Typography / Spacing / Radius / Border / Control Height / Icon Size（Elevation/Shadow 与 Animation Duration 评估后决定是否引入——Windows 原生桌面软件阴影收益低）。

**两种组织方案比较**：

| 方案 | 形态 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- | --- |
| A. QML singleton（`pragma Singleton`，DS.theme.colors.surface 式） | 全局 token 对象 | Qt 官方推荐形态；一处定义全处引用；支持 dark/light 切换；无附加依赖 | 需要新目录 + qmldir + module 注册 | **采用**（colors/spacing/radius/typography 全部走 singleton） |
| B. Main.qml root property 扩大化（沿用 V1 现状） | 继续在 root 上堆 property | 改动最小 | token 与窗耦合、无法跨窗口/跨组件复用；M10~M12 会继续膨胀 | 否决（V1 已证明上限） |

最终组织：`ui/qml/DS/`（`DesignSystem.qml` singleton + `qmldir`），通过现有 `qt_add_qml_module` 注册；root 层只保留列宽比例等**本窗专属**几何 token。**本阶段不实现**。

## 6. Icon Investigation（只调查不修复）

事实（已证明，基于文件取证）：

- `src/main.cpp` **无** `QGuiApplication::setWindowIcon` / `engine` 无 `setWindowIcon` 调用 ⇒ **运行时窗口标题栏/左上角 icon 从未被设置**。
- `CMakeLists.txt` 中**无** `.rc` 文件、无 `.ico`、无 `qt_add_resources`（icon）配置 ⇒ **exe 未嵌入 Windows 图标资源** ⇒ taskbar/Explorer 显示默认图标。
- 结论：两块**都缺失**是根因（已证明：两处配置点均不存在）；修复后视觉是否完全符合预期 = 待 M9-E 验证（例如不同 DPI 下多尺寸 icon 效果）。方案构思（仅规划）：运行时 `QIcon`（含多尺寸）+ Windows `.rc`/`.ico` 资源（CMake `qt_add_executable` 支持 WIN32_EXECUTABLE/资源编译）。不实现。

## 7. Knowledge Before Implementation（开发者学习章节，映射到真实问题）

| # | 知识点 | 它将在 M9 中解决什么真实问题 |
| --- | --- | --- |
| 1 | QML Component | 把 13 个 Button / 3 个 TabButton / 统计卡的重复内联样式收敛为可复用组件的前置概念 |
| 2 | Qt Quick Controls | 现用 Button/ComboBox/SpinBox/TabButton 都是 Controls 系——定制必须走 background/contentItem（T013 Fusion 教训的直接延伸） |
| 3 | Qt Quick Controls Style | 为什么必须保持 Fusion（原生 Windows style 忽略自定义渲染）；未来每加组件都要遵守 |
| 4 | Design Token | 回答 §5 的核心：23 种散落颜色 → 单一语义源 |
| 5 | Reusable component 价值 | 13 个 Button 同风格复制 vs 一个 AppButton；样式变更单点生效、visual regression 面缩小 |
| 6 | property binding | token 变化如何自动传导 UI（DS 单例属性绑定链）；破坏绑定（JS 赋值）是 M9 视觉 bug 常见源 |
| 7 | property alias / exposed property | 组件暴露 `tone/secondary/danger/disabled` 等对外属性而不泄漏内部实现 |
| 8 | signal | 组件内点击转发；避免组件隐藏业务语义（§9/G 防泄漏） |
| 9 | anchors vs Qt Quick Layouts | 现在两者混用；M9 需要在“精确摆放（表头列）”用 anchors、在“自适应面板层”用 Layouts 的明确分工原则 |
| 10 | implicitWidth / implicitHeight | 组件在 Layout 中不显式写死尺寸、由内容+padding 派生（避免 M10 新面板反复调像素） |
| 11 | palette / semantic color | 当前 palette.buttonText 的历史 workaround；语义色（success/error/warning）应走自身 token 而非 Controls palette |
| 12 | hover / pressed / disabled states | 表格行 hover、按钮 disabled 视觉统一（V1 状态表达弱，见 §3-14） |
| 13 | DPI / scalable UI 基础 | 去掉像素死值后，125%/150% 缩放下布局稳定（当前 21 处 pixelSize 的隐患） |
| 14 | QML resource | icon/qmldir/DS 单例如何进资源与模块路径（qt_add_qml_module 内注册） |
| 15 | Window icon vs exe icon | 两个 icon 是不同机制（运行时 QIcon vs 编译期 .rc/.ico）——§6 根因的结构性解释 |

## 8. Proposed Reusable Components（由当前真实重复模式决定，不照抄清单）

| 组件 | 解决的重复问题 | 现用点 | Exposed properties（草案） | 不封装什么 / 防泄漏 |
| --- | --- | --- | --- | --- |
| `AppButton`（变体 tone: primary/secondary/danger/ghost） | 13 处 Button 内联样式 | Top Actions / Serial 读取 / 载入回放 / Agent 发送 | `text, tone, enabledNative?, onClicked, busy?` | 不封装业务语义（点击语义由调用方信号承担）；不内置文案 |
| `PanelCard` | 卡片/边框/层级散落 | 统计卡容器、Serial GroupBox、诊断区 | `title?, padding, background 半色` | 不内置内容布局规则 |
| `StatCard` | 统计卡 7+2 重复结构 | 7 张计数卡 + 率/延迟 | `label, value(model), tone` | 不决定取值来源 |
| `StatusBadge` | 状态色无系统（§3-7） | 事务表状态列、未来 Decoded Values | `status/statusCode, text 显式` | 颜色永远伴随文字，绝不只靠色 |
| `SectionHeader` | 无层级（§3-1/9） | 每 section 顶部 | `title, actionText?, actionSignal?` | 不内嵌具体操作 |
| `FieldRow`（label+控件对齐） | Serial 参数行 inner 对齐不一 | 从站地址/数量/波特率 | `label, control(delegate), hint?` | 不封装控件校验逻辑（校验在 Controller/Core） |
| `SegmentedTabs` | 诊断 3 Tab 内联样式重复 | diagnosisTabs | `model(list), selectedIndex, onSelected` | 不内含页面内容 |
| `EmptyState` | 空态文案样式不统一 | 事务空态/诊断空态/串口空态 | `title, hint?` | 不含数据 |
| `ErrorBanner / InfoBanner` | 内联错误/提示 Label 样式 | Replay error/notice | `tone, text, visible 自管` | 与 Controller 状态只读绑定 |

每个组件设计约束：accessibility（可聚焦、语义名）、disabled/hover/pressed 三态显式、**不得把业务判断搬进组件**（§9/G）。

## 9. Navigation Alternatives（M9-B 准备，本阶段只比较）

| 方案 | 形态 | Complexity | Discoverability | Screen density | Migration risk | M10~M12 extensibility | V1 behavior risk |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A 继续 single-page sections | 加长单页 | 最低 | 中（长滚动） | 低（一屏可见少） | 低 | 差（无限堆叠） | 低 |
| B Workspace / Navigation（5 区：Dashboard / Communication / Replay / Diagnosis / Device） | 侧栏/顶部导航 + 每页 workspace | 中 | 高 | 高（分区隔离） | **中高**（迁移现有单页到多页=控件移动，QML 行为风险最大） | 好 | **中**（source switching 等跨页状态需重设计） |
| C 最小改造：单页 + 分段 Section 化 + 预留语义锚 | 保留单页结构，SectionHeader 分节 + 语义 anchor 命名 + 逐步组件化 | 低 | 中 | 中 | **低**（纯结构重排，绑定不动） | 中（M10 面板可先以 section 插入，之后视密度再升导航） | 最低 |

**推荐：C 先行（Phase 2 实施先做组件化 + 分节）→ M9-B 引入 B 的前缀设计（navigation shell 但默认仍单页装配），当 M10 内容实质增多时再切页签导航**。理由：presentation may change, behavior must not——C 用最小迁移风险换取 token/组件基础；B 的 5 区语义（Dashboard/Communication/Replay/Diagnosis/Device）作为长期 target 保留在 M9-B 学设计中。

## 10. Window Chrome 结论（本轮不实现）

**保持 Native window chrome**。比较：frameless 自绘 titlebar 引入拖拽/resize/maximize/double-click/snap layout/DPI/阴影/无障碍/Windows 行为全套自维护成本，且当前证据（用户仅反馈颜色割裂）不足以证明收益>风险；割裂感在 native chrome 下可通过窗口 icon（§6）与内容区统一 tone 缓解。若未来要做 frameless，须单独 ADR + 原型验证 snap/拖拽行为。**不实现。**

## 11. V1 Contracts at Risk（M9 实施时必须逐一核验）

Button 信号接线（runDemoBatch/loadReplayFile/refreshSerialPorts/readHoldingRegistersOnce/clearResults/disconnectSerial/runBaselineDiagnosis/askAiDiagnosis/cancelAiDiagnosis/agent ask-cancel）→ 行为绑定必须 unchanged；Serial enabled/busy 联动（`serialConnected/serialBusy` 派生使能）；source switching 原子语义（r03/r07）；Replay 加载/错误/notice 展示逻辑；Statistics 7 卡数值 property；Diagnosis 三 tab 内容与 baseline/ai/agent 层次；Agent 输入/回答/取消路径；TransactionListModel 7+1 roles 与双行 delegate；空态显示条件；键盘/焦点（tab 顺序与 disabled 状态）；窗口最小尺寸 1000×700。

M9 原则：**presentation may change, behavior must not**——任何上述绑定/条件的变化必须出现在“Files Expected to Change”与 Test Plan 中，违者按 V2 Protocol 归档失败。

## 12. Test / Acceptance Plan（M9 后续）

- 自动：既有 full ctest（24 目标）+ `ui_bridge`（55 slots，行为面）+ `qml_smoke` + `git diff --check`；M9A 组件化后新增静态 UI 约束测试候选（如 token 引用检查、组件存在性）列入 Phase 2 计划，不现在实现。
- 人工（M9-F Before/After 视觉对比，基线=`docs/assets/screenshots/v1-ui-baseline.png`）：窗口尺寸（1024×720/最小 1000×700）、Simulator 黄金批次、Replay `demo_v1.mlog` 加载/错误/notice、Serial controls（含 disabled/busy/无端口空态）、Diagnosis 三 Tab、Agent 问答 busy、事务表 ProtocolError 双行/长 issue 文案换行、空态、错误态。
- V1 历史窗口尺寸等 acceptance 项以 PROJECT_STATUS 人工验收记录为准；无记录者标注 UNKNOWN（本任务文档不发明历史尺寸）。

## 13. Files Expected to Change（Phase 2 起，现在冻结清单）

- 新增（预计）：`src/ui/qml/DS/`（token singleton + qmldir）、`src/ui/qml/components/`（§8 组件集）；`CMakeLists.txt` 的 qt_add_qml_module 资源注册（Phase 2 才动）。
- 修改（预计，Phase 2+）：`Main.qml`（以组件替换重复块——只换呈现）；M9-E 才加 `main.cpp`/CMake 的 icon 配置。
- **本 Phase 1 零改动**：src / tests / QML / CMakeLists / scripts / samples / production config。

## 14. Potential Interview Questions

1. 为什么先 Design System 而不是直接重写界面？——934 行单文件 + 35 处颜色字面量是样式漂移的根因；token 先行让组件化/M10 面板共享同一语义源。
2. 为什么 token 用 Singleton 而不是 root property？——root 与窗口耦合、跨组件不可复用；Singleton 一处定义全处绑定（binding 自动传导）。
3. 状态色为什么不能是唯一表达？——无障碍与截图打印场景下颜色丢失；StatusBadge 强制 text+icon+color 三通道。
4. 为什么推荐导航方案 C 而不是一步到位多页？——presentation may change、behavior must not：C 迁移风险最低，为 M10~M12 攒密度再升导航。
5. 窗口 icon 与 exe icon 为什么是两个机制？——运行时 QIcon（main.cpp/QML window）与编译期资源（.rc/.ico），V1 两处都缺失（文件取证）。
6. Fusion style 为什么不能换掉？——T013 实证原生 Windows style 忽略 background/contentItem 定制；M9 组件化继续依赖该行为。

## 15. Implementation = NOT STARTED

Phase 1（本档）交付：架构重建、UX 问题清单、设计方向、token 策略（两案比较+结论）、组件计划、导航三案比较+推荐、chroma 结论、icon 根因、知识映射 15 项、V1 风险清单、测试/验收计划。**未写任何 QML/组件代码。**## 16. Phase 2 — Implementation Record（追加批注：取代 §15 与顶部状态行的 "NOT STARTED"，事实以本章为准）

**实施批次**：M9-A Phase 2（用户批准范围）：DesignSystem tokens + AppButton/PanelCard/SectionHeader/StatCard 四个组件 + 仅迁移 Top Actions 与 Statistics 两处。Serial Controls、Replay workflow、Diagnosis workspace、AI/Agent UI、Transaction ListView/delegate、顶层 Navigation、window chrome、窗口/任务栏 icon **一律未动**。

### 16.1 Implementation

- **DesignSystem.qml**（`src/ui/qml/DS/`）：token 单例对象。spacing XS4/S8/M12/L16/XL24；radius S4/M6/L8；controlHeight 34 / controlPadding 12；neutral 色复刻 V1 固定浅色板（background/surface #FFFFFF、surfaceAlt #F5F7FA、cardSurface #F4F4F4、cardSurfaceAlt #F0F0F0、border #D8DDE4、separator #EDF0F4、textPrimary #1B1F26、textSecondary #4A5568、textMuted #606060、disabledBg #EDF0F4、disabledText #98A2B3）；primary #2F6FB7 / primaryHover #3D7FC4 / primaryPressed #24527F；语义色 success #306030、exception #806000、crcError #803030、timeout #604080、protocolError #606060、expectedNoResponse #406060、pending #4A5568、error #B03030、notice #806000；typography title24/section15/body13/caption11/metric20。值与 V1 逐字对应（T013 Phase C 板对板搬入 token 层），HUMAN VISUAL REVIEW 状态顺延。
- **AppButton.qml**：`tone: "primary"|"secondary"`；implicitHeight 34；background 三态（down/hovered/normal）+ disabled 态；contentItem Text 随 tone/enabled 换色；`focusPolicy: Qt.StrongFocus`。onClicked/enabled 完全不内置（见 16.6 行为保全）。
- **PanelCard.qml**：surface + border + radius + `padding`（默认 DS.spacingM）+ `toned`；不含任何业务。
- **SectionHeader.qml**：title + 可选 subtitle；标题 fontSection bold，副标题 fontCaption + textMuted，尾随 spacer。
- **StatCard.qml**：`label`/`valueText`/`tone`/`emphasized`；默认 Layout 140×72 / cardSurface / radiusM；label=fontCaption+tone 色，value=fontMetric bold textPrimary。取值与格式化**全部来自调用方**。
- **Main.qml 迁移**：root 21 个颜色 token 中 neutral/primary 相关改为 DS 绑定（保留 root 别名让未迁移区零改动）；Top Actions 三个 Button → AppButton（runDemoBatch primary / 加载回放 secondary / 清空结果 secondary，onClickeds 原样）；Statistics 区 → SectionHeader("运行统计") + PanelCard + 两行 StatCard（第一行：已观测/已完成/进行中[140×72] + 成功率/平均延迟[180×72]；第二行：6 张状态计数卡[110×64，tone=各语义色]）。绑定来源、toFixed(1)+"%"、" ms"/"—" 等格式化语义逐一保持。
- **main.cpp**：按 ISSUE-010 把 DS 改为 engine root context property（QQmlComponent 从 qrc URL 创建一次 + setContextProperty("DS") + 失败即退出的 guard）。
- **deploy_windows.bat**：step 7 改为整目录复制生成的模块（ISSUE-011），step 8 校验清单同步为生成树文件。

### 16.2 Files Changed

- 新增：`src/ui/qml/DS/DesignSystem.qml`、`src/ui/qml/components/{AppButton,PanelCard,SectionHeader,StatCard}.qml`、`docs/issues/ISSUE-010-qml-module-singleton-runtime-unresolved.md`、`docs/issues/ISSUE-011-deploy-qmldir-module-drift.md`。
- 修改：`src/ui/qml/Main.qml`、`src/main.cpp`、`CMakeLists.txt`、`scripts/deploy_windows.bat`、`docs/tasks/T016-*.md`、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、`docs/devlog/2026-09-15-m9a-phase2.md`。
- 未动（范围冻结区）：Serial Controls / SerialPortAdapter / Controller / core / tests / Replay 工作流 / Diagnosis / AI / Agent / Transaction delegate / samples。

### 16.3 Problems Encountered —（链接 Issue）

1. **DS 模块单例运行时全量 `ReferenceError: DS is not defined`**（三类机制齐备仍失败；qmlcachegen 全量重生成无效）→ [ISSUE-010](ISSUE-010-qml-module-singleton-runtime-unresolved.md)。定位过程中还误入一个坑：Windows GUI 程序负退出码在 Git Bash 显示为 127 且 stderr 全空，PowerShell 取原始 ExitCode + `QT_ASSUME_STDERR_HAS_CONSOLE=1` 才拿到真实错误。
2. **`Label is not a type`**：SectionHeader/StatCard 只 import 了 QtQuick+Layouts——Label 属于 QtQuick.Controls；加 import 即愈（运行时错误而非编译错误，qmlcachegen 编译通过而引擎实例化失败）。
3. **`card is not defined`**：Main.qml 引用 PanelCard 内部 `id: card`——组件内部 id 对实例化方不可见；改为实例处自设 `id: statisticsPanel`。
4. **deploy 候选 `SectionHeader is not a type`**：手写迷你 qmldir 漂移 → [ISSUE-011](ISSUE-011-deploy-qmldir-module-drift.md)。
5. 会话中途 Git Bash 环境两次抽风（`cd /cygdrive/e` 间歇性 No such file or directory、`spawn bash ENOENT`）——与代码无关，改用 Windows 风格路径 + `cd && pwd &&` 防护后恢复。

### 16.4 Solutions

1. ISSUE-010：放弃模块单例形态，DS 以 **engine root context property** 暴露（同样单实例、跨全 QML 可见、AOT cache 下稳定）；qmldir 仍保留 DesignSystem 为普通类型条目；CMakeLists 删除 singleton 属性块并注释指向 Issue。
2. 组件 import 约定落定：用 Controls 类型（Label/Button）的组件必须显式 `import QtQuick.Controls`。
3. 外部访问组件属性一律经实例 id，不依赖组件内部 id。
4. ISSUE-011：部署复制生成模块整体（单一机制）。

### 16.5 Verification（真命令 + 真输出）

```text
$ cmake --build --preset debug-local            → …[45/45] Linking CXX executable modbuslens.exe
$ powershell -File build/run_smoke.ps1          → EXITCODE=0；stderr 仅 QFontDatabase 字体目录环境提示，0 条 ReferenceError
$ ctest --preset debug-local                    → 100% tests passed, 0 tests failed out of 24（qml_smoke Passed 2.33s）
$ git diff --check                              → 无输出（通过）
$ scripts\deploy_windows.bat                    → [OK] Deployment directory ready
$ powershell -File build/run_smoke_deploy.ps1   → EXITCODE=0（不带 Qt 开发 PATH，windows 平台原生跑）
```

### 16.6 Behavior Preservation（presentation may change, behavior must not）

- Top Actions 三个 onClicked 接线（runDemoBatch / replayFileDialog.open / clearResults）原文保留；按钮数量、位置、可见性不变。
- Statistics 全部取值仍直接绑定 analysisController（observedCount/completedCount/pendingCount/successCount/exceptionCount/crcErrorCount/timeoutCount/protocolErrorCount/expectedNoResponseCount/hasSuccessRate/successRate*100 toFixed(1)+"%"、hasAverageSuccessLatency/averageSuccessLatencyMs toFixed(1)+" ms"、"—" 占位）。
- Controller、core、事务模型零改动。
- 已知有意的呈现差异（记录在案）：状态卡 value 字号由 18 归一为 fontMetric 20（设计系统统一字号梯度）；两行统计卡背景 #F0F0F0 与 #F4F4F4 归一至 cardSurface（footer/muted 呈现仅视觉层）。

### 16.7 Knowledge Learned（真实示例，非泛泛而谈）

1. **QML 类型来源是逐文件 import**：SectionHeader.qml 编译期正常、运行期 `Label is not a type`——Label 定义在 QtQuick.Controls；组件文件内用哪个模块的类型就必须 import 哪个模块。
2. **模块单例 = 三条件且仍不保证**：pragma Singleton + `QT_QML_SINGLETON_TYPE` 源属性（模块创建前）+ 生成 qmldir `singleton` 行；本项目 exe-attached qrc 模块在全部满足下仍运行期失败（ISSUE-010），根因未完全隔离时如实记录 + 换 context property 方案并验证——先让证据决定方案，再写结论。
3. **组件内部 id 不外泄**：`PanelCard{ id: card }` 的 `card` 只在组件文件作用域可见；实例化方要用实例自己的 id。这是 QML 作用域模型，不是 bug。
4. **GUI 进程取证组合**：Windows GUI-subsystem 程序 stderr 默认不通重定向管道；`QT_ASSUME_STDERR_HAS_CONSOLE=1` + PowerShell `Start-Process -PassThru` 拿原始退出码，负退出码（-1）不会被 MSYS 卷成 127。
5. **部署清单漂移**：手工维护"第二份模块描述"必然漂移（ISSUE-011）；部署复制生成物整体。
6. **context property 与 AOT**：root context property 在 qt_add_qml_module + qmlcachegen 下稳定工作（实测 0 错误），是模块单例之外的正规暴露渠道。

### 16.8 Potential Interview Questions（Phase 2 新增）

1. 为什么用 context property 而不是 QML 模块单例暴露 token？——先按官方三条件实现，实测运行期全量解析失败（ISSUE-010 有完整证据链）→ 换同样单实例、跨 QML 全局可见、AOT 稳定的 context property；决策由验证证据驱动。
2. 组件化如何保证"行为不变"？——组件只认 text/tone/label/valueText，onClicked 与取值绑定留在 Main.qml 原样接线；验证侧靠 ui_bridge（55 slots）+ qml_smoke + 全量 ctest。
3. 为什么部署目录不能手写 qmldir？——生成模块是单一事实源，手写副本在模块从 1 类型长到 6 类型时漂移且缺 `prefer` 行（ISSUE-011）。
4. GUI 程序 QML 错误看不到时怎么取証？——PowerShell 重定向 + QT_ASSUME_STDERR_HAS_CONSOLE，原始 ExitCode 判断。

### 16.9 Git Commit

见下文 "17. Git Commit"（提交后回填哈希）。