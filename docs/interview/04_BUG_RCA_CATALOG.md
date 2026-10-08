# 04 — BUG / RCA CATALOG（真实缺陷与根因分析目录）

> 用途：面试"遇到最难的问题 / 怎么定位"的可辩护素材库。**只收录仓库里有档案的真实问题**——
> 不制造戏剧化的 Bug 故事。
> 证据分级：`[V-nnn]` = VERIFIED；`[C-nn]` = CONFLICT；`[U-nn]` = UNVERIFIED。
>
> **收录范围**：`docs/issues/ISSUE-001`…`ISSUE-020`（20 条，全部有独立档案）
> + 任务档案内的 RCA（无独立 Issue 文件，12 条，编号 `RCA-A`…`RCA-L`）。共 **32 条**。`[V-263]`
>
> **⚠️ 读这份目录时必须先记住的三条边界**：
> 1. **ISSUE-008 / ISSUE-009 的历史 exact root cause 至今 UNKNOWN**，且**没有编造 RCA**——
>    这是本项目最值得讲的工程态度之一，不是"未解决的问题"。
> 2. **ISSUE-014 与 ISSUE-017 记录的是同一个现象**（重复建档），系刻意保留的档案记录。
> 3. **ISSUE-015 / RCA-M 是环境缺陷，不是产品缺陷**——产品侧零改动是**正确**的处置。

---

## 第一部分 — Issue 目录（ISSUE-001 … ISSUE-020）

### ISSUE-001 — variant 测试辅助函数返回悬垂指针（临时 variant 生命周期）
- **Related task**：T005（并波及 T004 已提交的三个测试文件）
- **Symptom**：`SimulatedSlaveTest::t01_readOneRegister` debug 构建下**偶发**断言失败
  （`Compared values are not the same`），而 t02~t07 全过；同批新增集成测试**编译失败**
  （GCC `taking address of rvalue [-fpermissive]` ×4）。
- **Initial wrong assumption**：先怀疑**实现**——但 t02~t07 与 t01 走完全相同代码路径却全过，
  指向测试基建而非被测代码。
- **Investigation**：集成测试的 `&右值` 编译错误提示了 variant 临时值生命周期主题；
  审查 `as<T>` 签名 `const T* as(const Variant& r) { return std::get_if<T>(&r); }`——
  引用绑定的是调用方传入的**临时**，指针在其析构后失效。
- **Root cause**：`std::get_if` 需要指向**存活 variant** 的指针；把"取自临时对象的指针"
  作为返回值带出语句边界即悬垂指针（UB）。GCC 对显式 `&右值` 报错，却对
  「`const&` 绑定临时 + 内部取址」这种等价形态**静默**。
- **Fix**：辅助函数改**拷贝语义**返回 `std::optional<T>`；调用点从指针改为值 + `QVERIFY(has_value())`。
- **Why the fix works**：把正确性做进辅助函数本身，而不是寄希望于每个调用点都先具名局部再取指针。
- **How verified**：集成测试 4 处编译错误消失；t01 稳定通过；`SIM-T01~T07` + `SIM-I01` 全绿；
  既有 T004 测试同法修复后 ctest 全绿（**断言内容逐字未动**，仅测试脚手架生命周期修正）。
- **Regression protection**：修复后测试本身即保护。
- **Commit**：T005 实现轮（见 T005 档案）。
- **Interview knowledge points**：返回"指向结果的指针"必须先问"结果本身活多久"；
  **"全部通过"的测试也可能带 UB**（-O0 栈布局恰好掩盖）；编译器不报 ≠ 正确。
- **Source**：`docs/issues/ISSUE-001-variant-test-dangling-pointer.md`（全文 57 行）。`[V-264]`

### ISSUE-002 — Explorer 启动失败：全局 PATH 旧 MinGW runtime 抢占（DLL collision）
- **Related task**：T008 Part A / T008.1（RESOLVED）
- **Symptom**：用户从 Windows Explorer 双击启动弹出
  `无法定位程序输入点 _ZNSt3pmr20get_default_resourceEv 于动态链接库 D:\QT\6.11.1\mingw_64\bin\Qt6Gui.dll`；
  同一 exe 在开发终端下启动正常、**自动化测试全绿**。Manual Visual UI Smoke = FAIL/BLOCKED。
- **Initial wrong assumption**：报错**指向 Qt DLL**，容易被读成"Qt 坏了/链接错了"。
- **Investigation**：`where` 五连查证明 Qt runtime 在 PATH 里排在**第三位**
  （`D:\Git\mingw64\bin`、`D:\mingw64\bin`（MinGW 8.1，2018-05）在前）；
  `objdump -p | grep -c "_ZNSt3pmr20get_default_resourceEv"` 精确对照：
  8.1 版 = **0（缺失——报错直接来源）**，13.1 版 = 1；`CMakeCache.txt` 证实编译器为 MinGW 13.1；
  System32 无任何 MinGW runtime（排除系统目录污染）；临时前置正确 runtime 后进程稳定存活
  （pid 32048、可访问性树 22 元素、stderr 无 QML warning）。
- **Root cause**：**Runtime toolchain collision**。Windows 按"应用目录 → System32 → … → PATH"搜索 DLL；
  本机 PATH 存在多套异构 MinGW C++ runtime 且旧版在前。旧 libstdc++（GCC 8.1）**缺 std::pmr 符号**
  （GCC 9+ 才有）→ 符号解析失败。
- **Fix（方向 + 落地）**：**交付/调试时把匹配 runtime 随应用部署**。
  `scripts/deploy_windows.bat`：从 `build/debug/CMakeCache.txt` 自动解析编译器 bin 与 Qt bin（**无硬编码路径**）
  → 创建干净 `build/deploy/` → 复制 exe → windeployqt（`--qmldir` 扫描 `src/ui/qml`）→
  **强制从编译器 bin 覆盖三件套 runtime** → 部署应用 QML 模块 → 关键文件校验。
- **Why the fix works**：应用目录是 DLL 搜索**首位**，天然屏蔽 PATH 污染；
  且 provenance SHA256 复核保证部署的 runtime 与编译器 bin **byte-identical**。
- **How verified**：Runtime Provenance = VERIFIED（三件套 SHA256 与编译器 bin 完全一致）；
  **Minimal-PATH smoke = PASS**（`PATH=C:\Windows\System32;C:\Windows` 下 `--qml-smoke-test` exit=0）；
  部署脚本可重复生成 = PASS；**用户 Explorer 双击确认 = PASS**。**业务代码修改 = NONE**。
- **Regression protection**：minimal-PATH 纪律后来成为**所有**打包/部署门的底座
  （deployment gate、canonical package runtime gates）。
- **Commit**：`28f38b0`（T008.1 standalone Windows deployment workflow）。
- **Interview knowledge points**：**exe 能链接通过 ≠ 能启动**；同名 DLL 多套共存是 Windows 经典事故；
  `where` 顺序即故障顺序；"符号缺失报错指向 Qt DLL"不代表 Qt 坏，而是它的依赖被换了；
  **部署正确性是内容身份问题，不是"文件在不在"**；自动 14/14 全绿仍挡不住启动环境问题
  ⇒ 分层验收缺一不可。
- **Source**：`docs/issues/ISSUE-002-explorer-launch-dll-collision.md`。`[V-265]`

### ISSUE-003 — 本机 Qt 6.11.1 未安装 QtSerialPort 组件（T010 阻塞）
- **Related task**：T010 Part A（RESOLVED）
- **Symptom**：T010 Learning 阶段"本机 QtSerialPort 能力实证"逐项**不存在**：
  headers / CMake package / `Qt6::SerialPort` target / DLL / 类全部缺失；
  仅有 `exampleurl-qtserialport.qdocconf` 与 `qtserialport_*.qm`（文档/翻译的**安装残留**）。
- **Initial wrong assumption**：用户反馈"已通过 MaintenanceTool 手动安装完成"——
  **按规则不因口头确认跳过检测**，重实证 = **FAIL**：`D:\QT\InstallationLog.txt` mtime 仍是 Sep 1 19:42
  （**无当日记录**），组件仍不存在。全盘检查确认唯一在盘的 `Qt6SerialPort.dll` 位于
  `D:\QT\Tools\QtCreator\bin\`（QtCreator 自带运行时，**与 6.11.1 kit 无关**）。
- **Root cause**：**多 prefix / 多 kit 依赖错位**——组件被装到了另一个 Qt 6.11.1 **MSVC** 安装
  （`D:\QTDesign\6.11.1\msvc2022_64`），而项目使用 **MinGW kit**
  （`D:\QT\6.11.1\mingw_64` + `D:\QT\Tools\mingw1310_64`）。
- **Fix**：用户通过 `D:\QT\MaintenanceTool.exe` 把官方 Qt Serial Port 装到**正确的 MinGW kit**。
- **How verified**：逐项复核（13 个头文件 / CMake package / DLL / `.a` 全部 EXISTS）+ 临时 CMake probe
  （`find_package` + link + 引用 `QSerialPort`/`QSerialPortInfo`）configure/compile/link/run 全过
  （`probe ok: 2 serial device(s) visible`）；compiler 不变；**无第三方 serial 库**；
  **无全局 PATH 修改**；**无 Qt 版本切换**。
- **Regression protection**：Learning 阶段必做**能力实证**——本次在写任何代码前拦截了环境缺口。
- **Commit**：`b31233b`（随后 T010 Part A 恢复并完成）。
- **Interview knowledge points**：**"按规则不因口头确认跳过检测"**——用户说装了不算，
  InstallationLog mtime 才算证据；多 Qt prefix 的组件错位是真实高频坑；
  `QtCreator` 自带 DLL ≠ kit 组件。
- **Source**：`docs/issues/ISSUE-003-qtserialport-not-installed.md`（含两轮 FAILED 调查的完整保留）。`[V-266]`

### ISSUE-004 — Diagnosis 面板纵向 overflow 使 AI 控件与事务列表不可达（**四轮定位**）
- **Related task**：T011 Part B（RESOLVED）；阻塞 Manual AI UI Smoke 与 Live ModelScope Smoke
- **Symptom**：点 Run Baseline Diagnosis 后 Diagnosis 内容纵向展开，把
  **AI Explanation / Ask AI / Cancel** 按钮与 **Recent Transactions** 推到可视区以下；页面无纵向滚动能力，
  这些控件与内容**不可达**。**定性 = 功能性 UI bug，不是 cosmetic polish**（严禁按 polish 降级处理）。
- **Initial wrong assumption / 逐轮演进（**四轮，这是本 issue 最有价值的部分**）**：
  1. **第 1 轮**：以为只是"缺滚动容器 + Diagnosis 无高度约束" ⇒ 加 `minimumHeight/maximumHeight/preferredHeight`
     + ScrollView ⇒ 用户复验 **FAIL**；
  2. **第 2 轮（Layout bounds ≠ Rendering clip）**：截图证明两层问题——
     QuickControls2 `ScrollView` **未显式给 `contentWidth/contentHeight`** ⇒ 底层 Flickable 的 contentItem
     尺寸未定义；`ListView` 默认 **`clip: false`** ⇒ delegate 绘制到 viewport 外并覆盖 heading。
     **"ScrollView 存在 ≠ 滚动正确工作"** ⇒ 用户复验**仍 FAIL**；
  3. **第 3 轮**：把长文本区从 `ScrollView` 改为**显式 Flickable**（`Layout.minimumHeight: 0` 是关键——
     content 的 implicitHeight 不再成为 viewport 最小值）⇒ 仍 FAIL；
  4. **第 4 轮（runtime geometry 取证）**：用户提供 debug overlay 读数
     `groupH=280 colH=245 flickH=133 flickCH=214 flickCY=81 contentH=214 contentIH=214`
     证明**滚动机制本身已正确**；同时证明"动态正文不可见"的根因是
     **`GroupBox` 的 contentItem 不是 Layout parent ⇒ `Layout.*` 附加属性被静默忽略 ⇒ ColumnLayout 未填充 ⇒ viewport≈0**，
     由 `diagnosisColumn.anchors.fill: parent` 修复。**随之暴露新 blocker**：
     为 Diagnosis 设的固定高度（~280px）在 Root ColumnLayout 纵向串联中**持续侵占** Transactions 空间
     —— 这是 **workspace 结构问题**，不是继续调 scroll 参数能解决的。
- **Root cause（最终收敛）**：三层叠加 →（a）局部 overflow（implicitHeight 无界 + 无全局滚动）；
  （b）动态正文 viewport 失效（`Layout.*` 在非 Layout 父下静默失效）；（c）最终本质 =
  **workspace vertical allocation defect**。
- **Fix**：**Horizontal SplitView workspace**（用户定案结构）——根 ColumnLayout 之上，
  左 pane = Diagnosis（**删除全部纵向 min/preferred/max 高度控制与状态驱动高度**；
  内部保留已验证的 `anchors.fill` ColumnLayout + Flickable，`childrenRect` 高度、AsNeeded scrollbar、PlainText、clip）；
  右 pane = Recent Transactions（`ListView clip: true` + `StopAtBounds`，delegate/model/roles/行高**不变**）；
  根布局不滚，两 pane 各滚各的；`ApplicationWindow minimumWidth: 1000; minimumHeight: 700`
  （**1000 = 左 pane 300 + handle + 右 pane 520 保底**，非拍脑袋）；移除**全部** DBG overlay/Label/Timer/probe
  （grep `DBG`/`geoProbe`/`TEMP` 计数 0）。
- **How verified**：用户人工确认**9 项**证据（左右布局、baseline 正常、独立 vertical scrollbar、
  内容不越 pane、Transactions 不再被挤到窗口底部、golden 4 transactions 一次可见、
  delegate 不覆盖 heading、两 viewport 互不干扰、SplitView 布局可接受）。
  自动化：clean 126 targets 零警告；ctest 20/20；qml smoke exit=0；deploy exit=0；minimal-PATH exit=0。
- **Regression protection**：SplitView 结构 + `clip: true` + `StopAtBounds`；
  后续由 ISSUE-012/013 的 `qml_geometry_check` 承接尺寸合同守卫。
- **Commit**：`85699ff`（`fix(T011): use split workspace for diagnosis and transactions`）。
- **Interview knowledge points**：**QML 的静默失败需要测量证据链**——没有 runtime geometry 读数就无法
  区分"滚动没配好"与"viewport 是 0"；`Layout.*` 附加属性在非 Layout 父下**被忽略而非报错**；
  **Layout allocation 与 Rendering clip 是两层不同的问题**；
  **取证通道本身要能工作**：WIN32 GUI 子系统无标准输出、QML `console.log` 无法重定向捕获，
  offscreen 可实例化但非可视渲染 ⇒ 最终靠用户真实截图 + 静态对象树。
- **Source**：`docs/issues/ISSUE-004-diagnosis-vertical-overflow.md`（**四轮全部失败过程原样保留**）。`[V-267]`

### ISSUE-005 — Ask AI 显示"操作被取消"：`OperationCanceledError` 裸漏 + 双 timeout owner 竞态
- **Related task**：T011 Part B（RESOLVED，Live 27B 回归 Smoke PASS）
- **Symptom**：真实 Live（ModelScope / `Qwen/Qwen3.5-27B`）点 Ask AI 后显示
  `Latest AI request failed: 操作被取消`——**用户无法区分 Timeout / 用户取消 / 意外取消**。
- **Initial wrong assumption**：与之前 397B 的 `malformed response` 是**完全不同的问题**（明确不混合）。
- **Investigation（代码级定性）**：
  1. "操作被取消" = `QNetworkReply::OperationCanceledError` 的 **Qt 本地化 errorString**。
     classification 的 code 映射其实是**正确**的（`mapReplyError` 把 `OperationCanceled → Timeout`），
     但 **UI 呈现的是错误字符串本体** ⇒ 泄漏底层 transport 语义。同时证明请求**确实被本地 abort**。
  2. **双 timeout owner 竞态**：`QNetworkRequest::setTransferTimeout(30s)`（Qt native，内部 abort）
     **与** `QTimer(30s)`（应用层，也 abort）**同时存在**；native 路径先到则 QTimer 未置态、
     `finished` 回调**无从区分"谁取消的"**——而 `QNetworkReply::error()` **本身不含 abort 主体信息**。
  3. 30s 对 27B 非流式推理偏短（本地 abort 确已发生是唯一数字依据；用户规则：不在无证据时拍数字）。
- **Root cause**：**错误分类 ≠ 错误呈现**（code mapping 对但 errorString 泄漏）＋ **多个 timeout 机制并存即竞态**。
- **Fix**：
  - `enum class AiAbortReason { None, UserCancel, Timeout, BatchInvalidated, DiagnosisCleared, SupersededRequest }`；
    **每次主动 `reply->abort()` 之前必须先记录 reason**；Controller 三个取消调用点各传对应 reason；
  - **移除 `setTransferTimeout`**（消除 native/QTimer 竞态）；QTimer 为**唯一 owner**
    （fire 时先置 `abortReason_=Timeout` 再 abort，并立即以业务文案 fail；其后的 `finished()` 因 busy=false 静默）；
  - `OperationCanceledError` 分类：Timeout reason → `AiDiagnosisErrorCode::Timeout`；
    其他内部 reason → **静默**；`None` → unexpected → `NetworkError`；
  - **任何路径不再把 Qt errorString 当用户文案**（transport 失败固定 "network error"；provider 错误 sanitized body）；
  - production timeout **30s → 90s**（测试仍注入 50~100ms）。
- **Why the fix works**：abort 主体信息在 Qt API 中不存在 ⇒ **必须以自有状态补位**
  （reason recorded **BEFORE** abort）；单一 owner 让"谁取消的"变成可判定事实。
- **How verified**：clean 126 targets 零警告；ctest 20/20（`AI-B12` 新增 message 断言 =
  "AI request timed out" **非底层串**；`AI-B11` cancel silent、`AI-B13`/`UI-AI08`/`UI-AI11` stale guards 不回归）；
  qml smoke / deploy / minimal-PATH 全 exit=0；**Live 27B Regression Smoke = PASS**
  （真实 explanation 显示，未再出现"操作被取消"）。
- **未修改**：model ID / messages / prompt / parser / `reasoning_content` / `max_tokens`（与取消无关）。
- **Evidence limitation（诚实保留）**：旧失败请求的**精确 elapsedMs 未实测**（WIN32 GUI 无输出通道）——
  档案**不改写**为"已实测旧请求恰在 30.000s timeout"。
- **Regression protection**：`AI-B11`（cancel 静默）/ `AI-B12`（timeout 业务文案）/ `AI-B13`（identity）。
- **Commit**：`bb3f3b4`（`fix(T011): make AI timeout cancellation deterministic`）。
- **Interview knowledge points**：**多个 timeout 机制并存即竞态**；**abort 主体信息必须自有状态补位**；
  Qt 网络错误分层映射要**显式设计**；`errorString()` 是本地化字符串，**不是业务文案**。
- **Source**：`docs/issues/ISSUE-005-ai-operation-canceled-leak.md`。`[V-268]`

### ISSUE-006 — AI 解释过度归因（单批小样本推出链路长期不稳定 / 共通根因）
- **Related task**：T011 Part B follow-up（RESOLVED）；verified LKGC = `01841b1`
- **Symptom**：真实 ModelScope 输出出现——"当前 Modbus RTU 通信链路稳定性较差"（4 笔样本推出长期链路性质）、
  "协议状态混乱"（非事实性概括）、"CRC 校验失败往往源于物理层信号干扰或电压波动"
  （**possible cause 被定调为 typical cause，伪因果**）、"混合错误出现暗示连接间歇性中断"
  （多种独立错误被合并为单一根因叙事）。而当时的**确定性事实只有**：1 笔 CRC 失败、1 笔超时、1 笔异常 0x02。
- **Initial wrong assumption**：不是"AI 胡说"这么简单——需要**逐层定责**。
- **Investigation（A–F 逐项核验，本 issue 最有方法论价值的部分）**：

  | 来源 | 核验结论 |
  | --- | --- |
  | A. deterministic context | **无责任**（纯事实 struct，不含"稳定性/物理层/链路"任何词） |
  | B. baseline findings wording | **无责任**（纯 enum；进 prompt 的是英文 code 名）。但 `severity` 是**协议影响分级而非因果强度**，模型可能误读为"证据强度"——是可被 prompt 澄清的**诱导因子**，不是措辞来源 |
  | C. prompt builder | **轻度责任**——user prompt 是纯结构化事实，但**缺少**两点：① 未显式提供异常码标准语义（0x02 = Illegal Data Address）；② 未标记"这是小样本" |
  | D. system instruction | **主要可控责任**——现有 authority 段全是**通用否定式**规则，缺少**正面的、按状态语义的**定义，缺小样本护栏，缺"混合错误≠共通根因"禁令 |
  | E. 模型自由生成 | **直接来源**——模型对 {CRC + Timeout + 0x02} 组合的条件先验天然偏向"物理层干扰→间歇掉线"；温度引入的措辞随机性使问题以**概率**出现 |
  | F. UI presentation | **无责任**（QML 仅 `Text.PlainText` 透传） |

- **Root cause**：**直接** = E；**可控** = D + C。即核心事实通道（A/B/F）**干净且值得保持**，
  修正点集中在 prompt 语言层。
- **Fix（生产改动只有**一个文件**：`src/ui/ai/DiagnosisPromptBuilder.cpp`，+19 行）**：
  - **Deterministic Status Semantics（正例语义）**：CRC = 接收字节未通过 RTU CRC 校验，
    serial settings/wiring/grounding/noise/EMI/capture **仅为 possible explanations or suggested checks、
    never stated causes**；Timeout = 阈值前未观察到有效响应，**禁称** offline/broken/disconnected/link interrupted；
    0x02 = **Illegal Data Address**（寄存器地址超出设备寄存器表），关联 register address / register map /
    device documentation / request configuration；0x01~0x04 标准语义表；未知码不猜；
    独立禁令句：**Never explain an exception code as a wiring failure, CRC problem, link interruption or
    electrical interference**；
  - **Mixed Error Independence**：anomaly types 是**独立观察**；多类异常**不推出** shared root cause；
  - **Facts / Explanations / Checks** 三层：可能原因必须显式不确定措辞（可能 / may / may indicate / possible）；
  - **Evidence Scope Guard（替代原设计的阈值方案）**：user prompt **无条件**追加
    `evidence_scope=current_observed_batch`；system 追加
    "The supplied evidence describes only the current observed batch, never a history or a long-run population"
    + 长期性措辞禁令；
  - 原有 authority 规则**一字未动**（追加在其后）。
- **为什么取消 `<=10` 阈值（设计被明确推翻）**：**count 本身不能论证长期代表性**——
  observed=30/100 若仍来自同一 batch，同样不得外推。guard 必须**无条件绑定"证据范围"这一数学事实**，
  而非数量门槛。
- **How verified**：clean 全量重建 126 targets 零警告；ctest **20/20**；
  新增 `AI-B14`（attribution discipline 全要素）、`AI-B15`（evidence scope 非计数：4tx/30tx 双 case、
  **无 small_sample 痕迹**）、`AI-B16`（0x02 语义行**不含** interference/wiring/CRC）、
  `AI-B17`（mixed 独立）；既存 `AI-B01~B13` + `ai10` 全保留 PASS；
  **测试期零公网 / 零 token / 零 quota（localhost fake only）**；
  用户 Manual UI Regression Smoke = **PASS（10 项）**；
  **Live ModelScope Smoke = PASS**（经用户授权，最小配额 **1 次真实请求**）——
  五项验收全过（A Evidence Scope / B CRC / C Timeout / D 0x02 / E Mixed），
  结构为"中文为主 + 术语保留 + PlainText + 四段语义清晰"。
- **Regression protection**：`AI-B14~B17`；`ai10_aiNeverChangesFacts`（misleading 文本原样透传
  但 **structured facts 零变化**）。
- **Commit**：`01841b1`（= 新 LKGC，原则：LKGC 只在 Manual + Live **双 PASS** 后推进）。
- **Interview knowledge points**：**事实通道与语言通道分离的正确姿势**——凡是影响模型判断、
  又可由 builder 计算的事实（total / exception semantics / evidence scope）都应**显式下发**，
  不该期望模型"数得出来、记得住标准"；
  **否定式规则不足以塑造先验**——必须给出每种状态**唯一允许的事实表述**；
  **行为约束放在离生成点最近的确定性位置**（ISSUE-005 是 timeout owner + abort reason，本 issue 是 prompt 文本），
  而不是在输出端修补。
- **Source**：`docs/issues/ISSUE-006-ai-explanation-overattribution.md`（含被推翻的 §6.2 原文保留）。`[V-269]`

### ISSUE-007 — Live Agent 工具预算耗尽（合法多步诊断触发 `ToolCallLimitExceeded`）
- **Related task**：T012 Part B Phase 2（RESOLVED，同题 Live re-validation PASS）
- **Symptom**：唯一授权的真实 Agent run 在获得 final answer 之前，UI 显示
  「工具调用次数已达上限。」（`ToolCallLimitExceeded`），run 在约 12 秒后终止；无崩溃、facts 零变化。
- **Initial wrong assumption**：RCA **严格区分已证明事实与不可证明事实**——
  - **已证明**：真实 run 在 final answer 前触发超限；当时 hard limits = `MAX_TOOL_ROUNDS=3` /
    `MAX_TOTAL_TOOL_CALLS=3`；**guards 本身工作正确**（整批拒绝、zero partial execution、`agentBusy` 回 false、
    deterministic statistics/rows/baseline 未变、无 crash/QML 损坏/false action）；
  - **不可证明（禁止写成事实）**：具体调用顺序（exact tool-call sequence 在 deployed UI **不可观察**；
    按禁令**未加任何调试设施**）。"模型重复调用 summary"或"一次调用四个 detail"**均为推测，不得记载为事实**。
- **Root cause**：一个合法、自然的 multi-step 只读诊断问题暴露出
  `MAX_TOTAL_TOOL_CALLS=3` 对该类 bounded read-only diagnosis **过于严格**。
  **这是 Agent orchestration / planning budget issue**——不是 Modbus Core、deterministic data、
  provider outage 或 QML 问题。
- **Fix**：① `MAX_TOOL_ROUNDS = 3` **保持**（严格限制 provider-loop 深度）；
  ② `MAX_TOTAL_TOOL_CALLS = 3 → 6`（允许 aggregates + 少量 targeted details；全部 read-only、
  单 immutable snapshot、bounded result——**无权限扩张**）；
  ③ Agent system instruction 加 **planning discipline**（最小化工具调用、不重复请求已获聚合信息、
  summary/anomalies 通常各至多一次、detail 只在直接相关时调用、足够事实后立即给 final、
  运行时预算 3 rounds / 6 calls；**通用 policy，不硬编码 demo 四事务 / transaction #2 / 0x02**）；
  ④ **否决** one-tool-per-round（Phase 1 的 multiple tool calls 是完整支持且已测试的能力，
  强制一轮一个会人为增加 round trips 并废弃已有能力）；
  ⑤ 既有 authority 规则（含 ISSUE-006 全部约束）**一字未弱化**。
- **How verified**：
  - `AGENT-B05`（重写）：**6 calls 一个响应 → 全部执行**（`tool_call_id` 逐一回传）；
    **7 calls → `ToolCallLimitExceeded` 且整批零部分执行**（`requestCount==1`）；
  - 新增 `AGENT-B23`（**Live-failure regression shape**）：round1 = summary+anomalies（2 calls）→
    round2 = detail(2)/detail(3)/detail(4)（累计 5 ≤ 6）→ round3 = final；
  - `AGENT-B04` 保持（第 4 个 tool round → `ToolRoundLimitExceeded`，**round guard 未被 total 调整破坏**）；
  - **RED 证据**：新 B05/B23 在**旧上限 3** 下运行 → B05 FAIL（6 calls 被拒）、B23 FAIL（累计 5 被拒）
    ——**精确命中旧失败形状**；改上限 + prompt 后 GREEN；
  - clean 147 零警告；ctest 23/23；
  - **Live Re-Validation（唯一 run）**：与上次**完全相同**的问题原文（一字未改），
    仅一次点击、无 retry：**结果 PASS**——约 60 秒内得到 usable final answer，
    **未触发** `ToolCallLimitExceeded` / `ToolRoundLimitExceeded`；final answer 中模型主动写出的数字
    均与事实一致（4 笔、25%、transaction 2 / 18 ms），**未编造任何具体寄存器地址**。
- **Regression protection**：`B05`（新边界）+ `B23`（多步计划）+ `B04`（round guard）。
- **Commit**：`e922c19`（`fix(T012): ISSUE-007 — raise agent total tool budget 3→6 + planning discipline`）。
- **Interview knowledge points**：**Hard budget 必须在"模型可用计划空间"与"资源上界"之间试调**；
  **只读 + immutable snapshot + bounded result 从根本上改变了"提高本地调用上限"的风险面**
  （不增加写权限与状态面）；**上线前无法观察 exact sequence 属已知盲区**——RCA 只依赖可证事实完成修复，
  **不靠猜 sequence**。
- **Source**：`docs/issues/ISSUE-007-live-agent-tool-budget-exhaustion.md`。`[V-270]`

### ISSUE-008 — 合法 0x02 提问可终止于 `InvalidResponse`（**历史 exact producer UNKNOWN**）
- **Related task**：T012 Post-Closure Stabilization；**Disposition：MONITORING / NON-BLOCKING**
- **Symptom**：用户在正常 Simulator Demo batch 连续测试约 15 类自然语言问题（其余 14 类正常），
  其中合法问题「异常码 0x02 在这个批次里代表什么？…请区分'已知事实'和'建议检查项'。」
  最终 UI 显示「模型响应格式无效」。
- **定位（代码核验，产出**四**个 producer 的完整清单）**：
  - `ModelScopeAgentClient::handleFinished`：① `choices` 缺失或非 array；② `choices` 空数组或 `choices[0]` 非 object；
    ③ `choices[0].message` 非 object（HTTP 非 2xx 走独立 `mapHttpStatus`，**不产生** `InvalidResponse`）；
  - `AgentRuntime::handleRoundSucceeded`：④ 助理消息**无 tool_calls 且 content 缺失/null/空串/纯空白**
    → `failProvider(InvalidResponse, "响应中无可用最终回答")` —— **这正是用户可见文案的来源**；
  - `tool_calls` 非空时 content 被忽略、工具优先——这是 **precedence rule，不是第 5 个 producer**。
- **Root cause**：**UNKNOWN**。用户看到的 = 路径 ④（HTTP 200 且结构合法，但 `message.content` 为空/纯空白
  且无 tool_calls）。**为什么该问题的最终轮会空 content：unknown**。
  可行假设（标注 `hypothesis only / not proven`）：模型在最终轮把输出预算消耗于 `reasoning_content`
  （`max_tokens=768` **统一适用所有轮**）。
  **不得写成事实**：reasoning 耗尽预算 / `finish_reason=length` / 模型行为异常——均未经 sanitized 响应证据证明。
- **追加更正（append-only）**：原措辞"本次 0x02 最可能进入路径④"**被撤回**——
  Controller 的中文映射只能证明**最终 error class**，**不能区分 ①~④**；
  可证明范围 = 上述 ①~④ 之一。
- **Live Diagnostic（经授权唯一 reproduction）**：**NOT REPRODUCED**——同一问题本次成功产出 usable final answer
  （约 60s）。sanitized per-round metadata 显示 round1/round2 `finish_reason=tool_calls`、
  round3（final）`finish_reason=stop`、`content present rlen=714/tlen=714 (usable)`、
  `reasoning present/786`——**"reasoning 存在"本身不必然导致空 content**（假设**未被支持、未被排除**）。
- **Phase 3 Hardening（Desired final-answer contract = AUTOMATED / HARDENED）**：
  `B20` 扩展 whitespace 全族（""、空格、双空格、tab、LF-only、CRLF+周围空格、混合）；
  新增 `B24`（`reasoning_content` 非空 + content 空 + 无 tool_calls → `InvalidResponse`，
  **reasoning 永不升级为 answer 的回归锁**）；新增 `B25`（非 string content **fails closed**）；
  `ag22` 集成锁（HTTP 200 + 无可用 content/tool_calls → `InvalidResponse` 必见）。
  **如实记录**：B20/B24/B25 **首次即 PASS** ⇒ 记为 **coverage gap only**（既有契约已正确），
  **未声称 TDD RED**。
- **遗漏的极边角缺陷（仅记录，单独测试列入未来矩阵）**：content 为**数字等意外类型**时
  `.toString()` 会非空（如 `"123"`）→ 会被当作 final answer。
- **Regression protection**：`B20` / `B24` / `B25` / `ag22`。
- **Final disposition**：Desired contract = HARDENED / AUTOMATED PASS；Historical failure = NOT REPRODUCED；
  Historical exact producer = **UNKNOWN**（①~③ / ④ 保持开放可能）；
  Future policy = **仅当问题自然再次发生且用户另行批准 diagnostic 时才继续取证，不主动重复请求寻找 failure**。
- **Interview knowledge points**：**"成功一次不能否定此前真实失败"**；
  **拒绝把 hypothesis 写成事实**；**先保证可观测再断言**（该轮本可直接修，但选择了硬化契约 + 保持 OPEN）；
  `finish_reason` **运行时从不读取**（api 契约扭曲无影响）。
- **Source**：`docs/issues/ISSUE-008-valid-0x02-query-invalid-response.md`（含两次 Correction 与 Live 元数据）。`[V-271]`

### ISSUE-009 — Provider 额度/请求失败未向用户呈现（**silent breakpoint UNKNOWN**）
- **Related task**：T012 Post-Closure Stabilization；**Disposition：MONITORING / NON-BLOCKING**
- **Symptom**：ModelScope API 无可用额度时，点 Agent → 「分析中...」约 1 秒 → busy 消失 →
  **没有持久可见错误提示**。用户无法判断是额度不足 / Provider 拒绝 / 网络问题 / 模型问题。
- **Error-path audit（逐层核验）**：`ModelScopeAgentClient`（非 2xx）→ `mapHttpStatus`
  （401/403=Unauthorized、429=RateLimited、400/404/422=ProviderRequestError、≥500=ServerError）→
  `sanitizeProviderMessage`（无 message 时用兜底文案）→ `roundFailed` → `AgentRuntime::handleRoundFailed`
  （stale 则静默）→ `failProvider` → `runFailed` → `AnalysisController::handleAgentFailed` →
  `agentErrorText_` → QML error label `visible: agentErrorText !== ""`。
  **8 类终态逐项核对**：NotConfigured / NetworkError / Timeout / Unauthorized / RateLimited /
  ProviderRequestError / ServerError / InvalidResponse —— **每一类都 emit、都映射文案、都写 `agentErrorText`、都可见**。
- **Root cause**：**在既有代码路径里，没有任何一条路径会"busy 消失且不写错误"** ⇒
  用户观察到的静默**无法由已核验代码解释**。最可能断点（**hypothesis，未经响应证据证明**）：
  额度不足场景 provider 返回 **HTTP 200 + 非标准/异常 body**，形态不在 audit 表内。
  **精确断点 = unknown**。**已证**：`configured != healthy`、`configured != quota available`
  ——顶部"模型服务：ModelScope — 模型：%1"只表示 **config 存在**。
- **Quota vs Rate Limit（定案：不做无法证明的区分）**：当前代码**无法区分** quota exhausted 与 rate limited
  （429 一律 RateLimited；无 `QuotaExceeded` 枚举）。项目历史真实证据（T011 Live attempt#1
  insufficient balance）**未保存 response shape** ⇒ 无法凭现有证据建立稳定 machine-readable 映射。
  **不新增 QuotaExceeded enum**；采用合并 UX 文案。
- **Live Diagnostic**：**PRECONDITION NOT AVAILABLE / BLOCKED**——同阶段 ISSUE-008 的授权 run 成功完成
  （3 个真实 Provider 请求全部通过）**直接证明当前账户额度可用**；"无可用额度的真实状态"本刻不可获得，
  且按规则**不得伪造 quota 响应冒充 Live evidence、不得用其它错误替代** ⇒ **未发出额外请求**。
  **Live quota reproduction count = 0**。
- **Phase 3 Hardening（HARDENED / AUTOMATED PASS）**：8 类 provider 错误全部非空、持久可见中文文案；
  `ag21` 锁定「429 → RateLimited → runFailed → busy false + `agentErrorText` 持久保留、
  仅 accepted run 才清」；顶部前缀改「**模型配置：**」（避免"服务"被误读为运行状态）。
  **本阶段不声称根因已修复**。
- **Enum 术语审计（不重构 enum）**：同一 `AiDiagnosisErrorCode` 实际承担两类语义必须区分——
  **A. provider/request-path failures（对用户必须可见）**；**B. local configuration / precondition /
  runtime states**（NotConfigured / InvalidConfiguration / NoData / BaselineRequired / Busy）
  ——后者**并非"Provider 失败"，不得统称**。
- **Regression protection**：`ag21`（429 全链路 + lifetime）/ `ag22`（malformed-200 可见）；
  回归约束：`B13`（cancel 静默）与 `UI-AG09/AG10`（batch invalidation 静默）不得因新文案误伤。
- **Future policy**：真实账户**自然出现** quota / request-limited 状态时可作 Live re-validation opportunity；
  **不得人为耗尽额度**。
- **Interview knowledge points**：**"无法证明的区分不做"**——缺少 response shape 就不新增 `QuotaExceeded` 枚举；
  **不伪造 provider 响应**（宁可记 BLOCKED 也不编 evidence）；
  `configured ≠ healthy` 是配置类 UI 文案的通用陷阱。
- **Source**：`docs/issues/ISSUE-009-provider-failure-ux-silent.md`。`[V-272]`

### ISSUE-010 — QML 模块单例运行时全部解析为 `DS is not defined`（**机理根因未完全隔离**）
- **Related task**：T016 M9-A Phase 2
- **Symptom**：`pragma Singleton` + `QT_QML_SINGLETON_TYPE TRUE` 齐备，生成的 qmldir 正确包含
  `singleton DesignSystem 1.0 …`；`QML_IMPORT_TRACE` 显示
  `resolveType: Main.qml "DesignSystem" => … TYPE/URL-SINGLETON`，且 `DesignSystem.qml` 自身属性解析正常；
  **但运行时 Main.qml/AppButton.qml/StatCard.qml/PanelCard.qml/SectionHeader.qml 中每一处 `DS.*` 引用
  都抛 `ReferenceError: DS is not defined`（约 80 条）**，窗口仍能创建（EXITCODE=0，视觉为全空 token 值）。
- **Initial wrong assumption / 诊断过程（含两次误导）**：
  1. 首次现象是**更有迷惑性的间接失败**：Git Bash 直接运行得到 `exit 127`、无任何输出
     ——这是 **Windows 负退出码（-1）在 MSYS 中被映射为 127**，掩盖了真实信号。
     改用 PowerShell `Start-Process -PassThru` 拿到原始 `ExitCode=-1`，
     再配合 `QT_ASSUME_STDERR_HAS_CONSOLE=1` 强制 GUI 子系统进程把 qWarning/QML 错误写入重定向 stderr，
     才拿到 `ReferenceError` 与后续真实错误文本；
  2. 初版还叠加 `Label is not a type`（SectionHeader/StatCard **缺失 `import QtQuick.Controls`**
     ——`Label` 定义在 Controls 而非 QtQuick）与 `card is not defined`（组件内部 id 对实例化方不可见），逐项修复后**仍剩 DS 解析失败**；
  3. 验证 qmldir 三步全部满足后仍失败；
  4. **排查 qmlcache（AOT）**：强制全量重生成（全部 QML touch 重建、qmldir 重新生成、重新链接）——**错误不变**，
     排除缓存陈旧假设。
- **Root cause（诚实声明：机理级根因未完全隔离）**：三重机制全部就位后运行时查找仍失败，
  指向 **exe-attached 内嵌 qrc 模块 + composite singleton + AOT qml cache 组合**下的已知问题族
  （社区同族报告：单例查找在编译缓存加载路径中未注入文档作用域）。
  受里程碑边界约束（本轮目标 = token 集中化 + 组件迁移，**而非 vendoring Qt internals**），**未继续深挖**。
- **Fix**：改用 Qt 官方同样认可的稳定形态 **engine root context property**——
  `DesignSystem.qml` 移除 `pragma Singleton`（保留为普通 `QtObject`，**token 值一字不改**）；
  `src/main.cpp` 用 `QQmlComponent` 从 `qrc:/ModbusLens/src/ui/qml/DS/DesignSystem.qml` 创建一次 +
  `setObjectOwnership(ds, CppOwnership)` + 挂 engine 为 parent + `rootContext()->setContextProperty("DS", ds)`
  （失败即 `return -1` 带错误串）；`CMakeLists.txt` 移除 `QT_QML_SINGLETON_TYPE` 属性块，
  **留下说明注释指向本 Issue**。
- **How verified**：Debug exe smoke `EXITCODE=0`，stderr 仅剩无字体的环境提示，**0 条 ReferenceError**；
  全量 ctest **24/24**（含起效的 qml_smoke）；deploy 候选 smoke `EXITCODE=0`。
- **Regression protection**：`qml_smoke` 的 exit code + 后续 ISSUE-016 加的 `FAIL_REGULAR_EXPRESSION`。
- **Interview knowledge points**：**GUI-subsystem Windows 程序诊断的可靠取证组合** =
  PowerShell 拿原始 ExitCode + `QT_ASSUME_STDERR_HAS_CONSOLE=1`（否则诊断写到 OutputDebugString，
  管道里什么都看不到）；**MSYS 把负退出码映射成 127 是假信号**；
  **"文档说了这样就行"≠本项目形态可行**——实测优先，方案在验证证据前不落定；
  **记录"未隔离"比编一个根因更重要**。
- **Source**：`docs/issues/ISSUE-010-qml-module-singleton-runtime-unresolved.md`。`[V-273]`

### ISSUE-011 — 部署目录中手写 qmldir 与真实模块漂移
- **Related task**：T016 M9-A Phase 2 验证环节
- **Symptom**：deploy 候选用 `--qml-smoke-test` 冒烟时
  `file:///E:/desktop/ModbusLens/build/deploy/ModbusLens/Main.qml:292:9: SectionHeader is not a type`
  ——M9-A Phase 2 引入组件后，部署包落回旧模块描述，
  `Main.qml` 引用的 `SectionHeader`/`PanelCard`/`StatCard`/`AppButton` **全部缺失**。
- **Investigation**：错误 URL 是 **磁盘文件**而非 exe 内嵌 qrc 副本 ⇒ 引擎走了磁盘解析路径。
  检查 `build/deploy/ModbusLens/`：仅含 `Main.qml` + 内容只有 `Main 1.0 Main.qml` 的 **手写迷你 qmldir**；
  而 debug 构建树的生成模块（`build/debug/ModbusLens/`）含带 **`prefer :/ModbusLens/`** 与
  **全部 6 个类型条目**的 qmldir。
- **Root cause**：`deploy_windows.bat`（T008.1 时代）在部署目录**手写**了一个迷你 qmldir 与单文件模块副本，
  与 `qt_add_qml_module` 生成的模块描述**各自维护**。模块从 1 个类型长到 6 个类型时这份**第二清单漂移**，
  且缺 `prefer :/ModbusLens/` 使引擎走了磁盘解析路径。
- **Fix**：**单一机制**——step 7 改为整目录复制**生成的**模块
  （`xcopy /e /i /y "%BUILD_DIR%\ModbusLens" "%DEPLOY_DIR%\ModbusLens\"`），删除手写 qmldir 生成逻辑；
  step 8 校验清单同步为生成树文件。
- **How verified**：`deploy_windows.bat` 输出 `[OK] Deployment directory ready`；
  deploy 候选在**不带 Qt 开发环境 PATH** 的 PowerShell 会话中 `--qml-smoke-test` `EXITCODE=0`、stderr 为空。
- **Interview knowledge points**：**任何"从生成物复制一份手工维护的清单"都会在模块演进时漂移**；
  部署脚本应复制构建系统**生成的**模块整体；**单一机制原则从 CMake 扩展到部署管线**；
  `prefer :/ModbusLens/` 是把解析拉回 exe 内嵌资源的关键行。
- **Source**：`docs/issues/ISSUE-011-deploy-qmldir-module-drift.md`。`[V-274]`

### ISSUE-012 — M9-A 可复用组件迁移后 statistics 布局塌缩
- **Related task**：T016 M9-A Phase 2（candidate `4fc934f`/`7abd887` 的 Manual Visual Review = FAIL）
- **Symptom（用户截图 + 运行时取证）**：statistics labels/values 在左边缘重叠；Stat card 背景与文本内容
  **空间上分离**；statistics 区高度塌缩 / 内容溢出；统计行不保持预期卡片布局；
  下方 Diagnosis 工作区被视觉侵入；统计不可读。
- **Expected**：两行稳定统计；每张卡非零稳定几何；label/value 留在各自卡内；统计区拥有足够高度；不与 Diagnosis 重叠。
- **Evidence（`--qml-geometry-check` 实测，修复前，1024×720，offscreen + windows 双平台一致）**：
  ```text
  statisticsPanel: x=0 y=227 w=992 h=0   implicit=0x0     ← 容器高度 0
  statisticsRow1:  x=0 y=0   w=840 h=72                  ← 两行 y 均为 0
  statisticsRow2:  x=0 y=0   w=732 h=64                  ← 与 row1 完全重叠
  diagnosisWorkspace: y=252；统计内容实际绘制至 ~y=300 → 侵入 48 px
  ```
- **Initial Hypotheses（**证据收集前列出，并标注 `UNDER INVESTIGATION` 而非先填结论**）**：
  ① PanelCard（Rectangle）无 implicit 尺寸；② StatCard 依赖 `Layout.preferred*` 而自身 implicit=0；
  ③ 组件内部 anchors 布局不回馈父 implicit；④ `qml_smoke` 只断言"实例化成功"，不覆盖几何——为什么没拦住。
- **Root cause（测量后才追加批注）**：**PanelCard 的 root Rectangle `implicitWidth/Height = 0x0`**，
  其内部内容通过 **anchors 填充**卡片——而 **anchors 消费父尺寸但不产生父的 implicit 尺寸**；
  根 ColumnLayout 对子项按 preferred（缺省 = implicit）高度分配 ⇒ PanelCard 高度 **0**；
  内部 ColumnLayout 在 0 高度空间里把两行都摆到 `y=0`（重叠绘制、无裁剪溢出），
  统计内容绘制到 Diagnosis 区之上——**全部症状与实测数字一一对应**。
  **次生缺陷（同族，本次一并修复）**：`StatCard` 自身 implicit=0x0，几何完全外包给调用点的 `Layout.preferred*`，
  在任何非 Layout 容器中同样会塌缩。
- **Fix（最小粒度，只动 contracts，不动行为）**：① `PanelCard.qml`：
  `implicitWidth/Height = contentLayout.implicit + 2×padding`，内容经
  `default property alias contentData: contentLayout.data` 进入内层 ColumnLayout
  （**anchors 只负责摆放、不再承担尺寸来源**）；② `StatCard.qml`：新增由 `labelColumn.implicit` 派生的
  implicit，保留 `Layout.preferred*` 作为布局首选值（调用点的 180/110/64 覆盖不变）；
  ③ `Main.qml`：statistics 调用点删除 wrapper ColumnLayout 与 anchors，两行 RowLayout 直接进入
  `contentData`，并新增 `objectName` 探针锚点；④ **AnalysisController 绑定、统计取数与格式化语义零改动**。
- **How verified**：`--qml-geometry-check` 修复前 `EXITCODE=1`（输出即上文 Evidence）；
  修复后 **PASS（默认 + 1000×700 双尺寸）**；修复后同探针：
  `panel 992×168（implicit 864×168）；row2 y=80 ≥ row1 底部 72；Diagnosis y=420 ≥ panel 底部 407`；
  qml_smoke `EXITCODE=0`；全量 ctest **25/25**（新增 `qml_geometry_check`）；`git diff --check` 通过；
  deploy 重建 `[OK]` + 无开发 PATH deploy 冒烟 `EXITCODE=0`；
  **grabWindow 真值截图两张 + 像素级自检**（逐卡文字像素存在且位于卡内）PASS；
  **Manual Visual Review = PASS**（9 项）。
- **Regression protection**：常驻 **CTest `qml_geometry_check`**（`modbuslens --qml-geometry-check`，offscreen）
  ——加载真实 QML、事件循环 settle 后断言：header 与 panel 不重叠；`statisticsPanel` 与每张可见 StatCard
  `w/h > 0`；`row2.y ≥ row1` 底部；`Diagnosis.y ≥ panel` 底部；**默认尺寸断言后 resize 至 1000×700 再断言一次**；
  缺失对象按 100ms×5 重试后才判失败（布局/委托实例化是异步的）。
  **明确边界：这不是视觉验收替代品**（有人工 M9-F），它只锁"尺寸合同不再断裂"。
- **Commit**：`6562dd3`（`M9-A: fix statistics layout regression`，经用户批准为 verified LKGC）。
- **Interview knowledge points**：**容器组件的尺寸合同必须自洽**——implicit 来自 content-layout implicit（children 派生），
  **anchors 永远不产生 implicit**；**"自动化全绿"≠"布局没塌"**（`qml_smoke` 的断言面是实例化不是几何——
  盲区要由最小的运行时几何断言单独覆盖）；**测量先于结论**；
  取证探针本身也要防坑（`QObject::findChild` 漏 Repeater delegate；**DPI 虚拟化让外部截图拿到错误像素尺寸**）。
- **Source**：`docs/issues/ISSUE-012-m9a-statistics-layout-collapse.md`（UNDER INVESTIGATION → 实证根因全保留）。`[V-275]`

### ISSUE-013 — Geometry 断言变量遮蔽：两条断言长期**不可证伪**（evidence vacuity）
- **Related task**：M9-B5.2（重指 ISSUE-004 守卫时发现）
- **Symptom**：`runGeometryAssertions` 在函数顶部声明外层 `QQuickItem *row2/header/panel = nullptr`，
  而 `if (statsVisible)` 块内用 `auto *row2 = findNamedItem(...)` 等**重新声明了同名局部**
  ⇒ 外层变量**从未被赋值、恒为 `nullptr`**。
- **Expected vs Actual**：Expected = "row2 与 row1 重叠" / "workspace 下缘侵入 statisticsPanel" 两条断言
  在布局回归时 **FAIL**；Actual = 两条断言的条件都以 `if (row1 && row2 && …)` 开头，
  `row2`/`panel` 恒 null ⇒ **条件恒假、断言恒"通过"**——**不可证伪**。
  任何行重叠 / 下缘侵入类回归都不会被该门禁拦截（此前捕获的 992x0 塌缩与 surplus-spread
  是由同函数**其它**"尺寸非零/MISSING"断言拦下的，故未暴露）。
  附带：外层 `row2`/`header` 死变量产生 `-Wunused-variable` 警告（构建日志可见但未被处置）。
- **Root cause**：同函数两个作用域内**同名局部遮蔽**；MinGW 默认**未开 `-Wshadow`**，编译器不报警。
  写法漂移自多次向该函数**增量追加**断言（外层声明是后来为块外断言加的，追加者未核对内层是否已有同名声明）。
- **Fix**：去掉内层 `auto *` 声明，直接给外层变量赋值，并在该处留 ISSUE-013 注释。
- **How verified**：
  1. **正控（PASS 路径）**：`--qml-geometry-check` exit 0、**0 GEOFAIL**、8 趟全过（ctest 同步 PASS）；
  2. **变异探针（证非空转）**：临时把两条断言的比较常数**各 +1000.0**（制造必然违规）→ 重建 →
     同一命令 **exit 1**，输出
     `GEOFAIL: DEFAULT legacy: legacyTransactionsPane y=220 invades statisticsPanel (y=27 h=168)` /
     `GEOFAIL: DEFAULT legacy: statisticsRow2 y=80 overlaps row1 (y=0 h=72)`
     ⇒ 两条断言在违规输入下**真实触发**；随后**还原源码、重建、门禁恢复全绿（探针未入库）**；
  3. 真值快照（修复后 dump）：`statisticsPanel_legacy y=27 h=168`（下缘 195），
     `legacyTransactionsPane y=220`（≥195 ✓）；`statisticsRow1 y=0 h=72`，`statisticsRow2 y=80`（≥72 ✓）。
- **Regression protection**：断言本体即保护（`qml_geometry_check` 是常驻 ctest 项）；
  **变异探针方法记录于此**，供后续"断言非空转"审计复用——
  **对任何 `if (ptr && …)` 形守卫，须证明 ptr 分支可达（存在 + 可触发）**。
- **Commit**：B5.2 同提交修复 + 变异探针证明。
- **Interview knowledge points**：**"断言写了"≠"断言在断言"**——以短路条件守卫的断言，
  其指针解析失败会**退化为永真**；该项目已**三次**踩证据侧假阴性/假阳性
  （B2.3 旧图、B4.3 键集/旧图、本例空转守卫），共同模式是**证据链自身缺真值对照**；
  **变异探针（故意让断言应 FAIL 并观察其 FAIL）应成为守卫类断言的最小验证动作**；
  `-Wshadow` 未开是这类遮蔽能潜伏的直接原因。
- **Source**：`docs/issues/ISSUE-013-geometry-guard-shadowed-locals.md`。`[V-276]`

### ISSUE-014 — TransactionsPage delegate 在模型 reset 期间按无效角色求值（**首见，本轮不修**）
- **Related task**：M10-C3 write-foundation harness 首见；**结论 = PRE-EXISTING NON-BLOCKING，本轮不修**
- **Symptom**：新增 `--qml-write-foundation-check` 在 **Clear Results / source replacement 之后**
  输出 **9 条** QML 运行时告警（271–277 行 7 条 `Unable to assign [undefined] to int/bool/QString`、
  344 行 `TypeError: Cannot call method 'toString' of undefined`、349/379 行各 1 条）。
- **关键判别**：**同一序列在 `--qml-nav-check`（同样会清空结果）中不出现（实测 0 条）**
  ⇒ 该现象只在「Transactions 页的 ListView **已创建了 delegate**，随后模型被 reset」这一**组合**下出现。
- **Impact（如实分级）**：**无功能影响、无数据损坏**——`setEntries()` 使用**正确**的
  `beginResetModel()/endResetModel()`，reset 期间 delegate 被短暂按无效 index 求值、reset 完成后立即恢复；
  **无 write-safety 影响**（与 prepared snapshot / confirmation / transport 完全无关，
  写路径对 transport 调用次数为 0，且这些告警来自 Transactions 页而非写 UI）；
  **对使用者的可见影响：无（仅日志噪声）**。
- **Root cause**：Qt Quick 的 model reset 语义——`beginResetModel()` 之后、`endResetModel()` 之前，
  view 可能**重新求值既有 delegate 的绑定**，而此时 `index` 已无对应模型行，`model.<role>` 返回 `undefined`；
  delegate/详情绑定**直接**把角色值赋给强类型属性（`int`/`bool`/`QString`）或调用其方法（`.toString()`）。
- **Decision（本轮不修，理由可辩护）**：**M10-C3 scope freeze**——本轮只做 write confirmation 的
  context/keyboard/accessibility 安全，不改 Transactions 页视觉与绑定结构。
  记录为 `PRE-EXISTING NON-BLOCKING`（**产品 QML 既有的健壮性细节；本轮只是首次由新 harness 路径观察到**）；
  建议在未来的 **warning hygiene / UI robustness** 任务统一处理
  （例如给 delegate 绑定加 `?? 0` / `?? ""`，或把详情绑定改为 `selectedEntry` 为空时不求值）。
- **How verified**：`--qml-write-foundation-check` **PASS**（exit 0，全部 C01–C36 oracle 通过，
  **zero write dispatch**），9 条告警**不影响任何断言**；`--qml-nav-check` / `--qml-focus-check` PASS，0 条同类告警；
  Debug/Release ctest **31/31**。
- **Regression protection**：C3 的 harness 会**持续**执行「产生 transaction → 清空结果」序列，
  因此该告警若变成**失败**（例如升级 Qt 后把 undefined 赋值提升为错误）会立刻在
  `qml_write_foundation_check` 中暴露。
- **后续闭合（追加批注，不改写历史）**：本文档记录的实现问题**后续已由 ISSUE-017 载明的修复闭合**——
  delegate 根改为经 `id` 单点容错读取全部角色，六个 QML 诊断模式下
  `ReferenceError` / `TypeError` / `Unable to assign` 计数**均为 0**，
  修复方式与本文件当初建议的「加 `?? 0` / `?? ""`」**一致（只是集中在一处完成）**。
  **两个历史编号及各自原始记录均保留（未合并、未重写、未删除）**；ISSUE-017 内部亦有指向本文件的批注。
- **Interview knowledge points**：**正确使用 `beginResetModel/endResetModel`** 不阻止
  delegate 在 reset 窗口内被再求值一次——这是 Qt Quick 的语义，不是模型信号缺陷；
  **"分类要靠调用路径，不能靠谁先发现"**；
  以及一条诚实的文档纪律教训：**同一现象被重复建档**，见 ISSUE-017 的教训节。
- **Source**：`docs/issues/ISSUE-014-transactions-delegate-reset-binding-warnings.md`。`[V-277]`

### ISSUE-015 — 本机 Qt `QProcess` 无法派生带管道的子进程（环境级 BLOCKER）
- **Related task**：T022 M10-F Corrected Portable Artifact Refresh；**严重度 = BLOCKER（环境级，非产品缺陷）**
- **Symptom**：canonical packaging 在 deploy 步骤失败：
  `make_package FAIL: Release deploy failed: [ERROR] windeployqt failed with exit code 1`
  / `Unable to query qtpaths: Error running binary qtpaths: pipe:`。
  `windeployqt` 在**最早阶段**（`Running: qtpaths -query`）即失败 ⇒ **从未复制任何部署文件**；
  `--dir` 强制部署同样产出 **0 个文件**。
- **Evidence（6 条，逐条隔离）**：
  1. **`qtpaths` 自身完全正常**（`-query` rc=0、输出完整 25 行 `QT_INSTALL_*` 表；`--qt-version` = 6.11.1）
     ⇒ **不是 Qt 安装损坏**；
  2. **工程外最小 QProcess 探针**（`build/_qprocess_probe.cpp`，**仅链 Qt6Core，与 windeployqt / ModbusLens 无关**）
     ⇒ `waitForStarted = false`、`error = 0 (pipe: 系统找不到指定的文件。)`；
  3. **通道模式探针**：`A default-pipes: started=false`、`B forwarded: started=false`、
     **`C startDetached: ok=true error=5 (Unknown error)` ← 仅 startDetached 成功**；
  4. **三重父子链一致**：Git Bash、原生 PowerShell 父进程、探针自身三者同一失败；
     **沙箱外重跑结果完全相同**（故非沙箱策略）；
  5. `windeployqt --verbose 2` 原始行：`QProcess: CreateFile failed. (所有的管道范例都在使用中。)`
     ← **ERROR_PIPE_BUSY**；
  6. **独立管道微基准（ctypes，绕过 Qt）**：
     `server CreateNamedPipeW(OUTBOUND) → ok`；`client CreateFileW(name, GENERIC_READ) → FAIL err=231 (ERROR_PIPE_BUSY)`；
     `INBOUND → ok`；`DUPLEX → ok`。
     ⇒ **服务端管道总能建立，但"服务端 OUTBOUND + 客户端 GENERIC_READ"这一
     Qt `QWindowsPipeWriter` 实际使用的配对总是失败**。随机 UUID 管道名重复 4 次结果一致（排除名称冲突）；
     `CreatePipe`（匿名管道）正常；`CreateProcess`（startDetached）正常。
- **排除的假设（逐项）**：Qt 安装损坏（`qtpaths`/`qmake`/`qmlformat`/`qmlimportscanner` 均正常）、
  PATH 遮蔽（曾暴露 Anaconda Qt 5.15.2 在前，用干净 PATH 复核后失败不变）、
  非 ASCII 的 TEMP 路径（改用 `C:\MLtmp` 不变）、`QT_INSTALL_*` 环境变量缺失（显式注入全部 25 项后不变）、
  沙箱策略（沙箱外不变）、进程/句柄泄漏（无遗留 qt 进程，管道命名空间仅 282 项且无冲突）、
  二进制变体（`windeployqt6.exe`、msvc2022_64、QtDesignStudio 副本同样失败；
  QtDesignStudio 6.8.7 副本因走 `qconfig.pri` 路径而"看似成功"、**未使用 QProcess**）。
- **Root cause**：本 Windows 会话（10.0.26200.9457）无法完成 Qt 6.11.1 `QProcess` 命名管道客户端的建立；
  Qt 的 `QWindowsPipeWriter` 依赖该配对转发子进程 stdout/stderr ⇒ **任何 QProcess 管道通道**都失败，
  只有不建立管道的 `startDetached` 成功。`windeployqt` 启动时用 QProcess 调 `qtpaths -query`
  ⇒ 该环境下必然失败，且失败发生在任何文件复制之前。
  **这是一项环境/宿主缺陷**，与 ModbusLens 代码、CMake 配置、打包脚本逻辑**均无关**
  （今日 08:32 与 12:51 的成功部署已证明本机此前可用）。
- **Fix（**本轮采取的正确处置**）**：**不实施任何产品/脚本改动**，而是
  ① 把本轮全部残缺产物以**整目录 Move** 方式 safe-retention；
  ② 按 V2 Trace 规约建档；③ 同步状态文档；④ 把 M10-F 置为 **HOLD / PENDING FRESH HUMAN #10/#11** 并给出人工解除路径。
  **理由：根因在环境而非仓库；在产品侧"修复"一个环境缺陷只会制造伪证据。**
- **明确否决的方案（**这是本 issue 最重要的部分**）**：
  - **不得手工拼装部署树**以冒充 `windeployqt` 产物——会破坏"部署树 provenance = windeployqt +
    编译器 bin 三件套"这一**被冻结契约**，并使 A/B/C/D identity 失去意义；
  - **不得复用陈旧的 `build/package` / ZIP / deploy 树**——陈旧 deploy（08:32）与 ZIP（12:51）
    **均不含 R17 标记**，复用即等于用修正前的产品冒充修正后
    （ZIP 非 identity criterion，但**陈旧 exe 是**）；
  - 不得修改 DPI/沙箱开关绕过（已证沙箱内外一致）；
  - 不得为了让打包"成功"而删除既有测试或放宽 `REQUIRED_FILES`（**V2 明令禁止**）。
- **How verified**：`qtpaths -query` rc=0；工程外最小探针在三种父子链下**一致复现**；
  通道探针显示仅 `startDetached` 成功；ctypes 基准定位到 `OUTBOUND + GENERIC_READ` 配对特有失败；
  仓库侧不受影响的门禁已复跑（**Debug CTest 36/36**、**Release CTest 36/36**）；
  `test_make_package_freshness.py` **7 项全 PASS**（missing / stale / current / **RED old-rule** /
  **same-size trap** / missing-build-exe）；`git status --porcelain` 空。
- **Regression Protection（该 issue 最有产出价值的部分）**：本 issue 属环境缺陷、产品代码零改动，
  因此**无需新增产品侧回归测试**；保护措施落在"**防止用陈旧/伪造产物冒充 fresh**"这一风险面——
  | 风险 | 现有保护 | 本轮实测 |
  | --- | --- | --- |
  | 陈旧 deploy 被误判为 current | `deploy_is_current()` 比较 exe SHA-256（**内容属性**） | freshness 7/7 PASS |
  | 陈旧产物冒充修正后行为 | R17 指纹（UTF-16 字符串计数）语义对照 | 新 exe = 12，陈旧 exe/ZIP = **0** |
  | 失败后只留部分部署被误判 current | **⚠️ 覆盖不足**——`deploy_is_current()` 仅比 exe，不含部署树完整性 | 本轮实测触发 `FAIL: required package file missing: platforms/qwindows.dll` |
  | 环境阻塞在 deploy 后期才暴露 | **⚠️ 缺失**——无 `windeployqt --version` 前置探测 | 本 issue 即为此缺失的直接后果 |
  **后续防复发动作（另立任务，本轮不实施）**：① 增强 `deploy_is_current()` 追加 `REQUIRED_FILES` 完整性校验；
  ② 在 `make_package.py` / `deploy_windows.bat` 增加**前置探测**（`windeployqt --version` 必须 rc=0，
  否则在进入 deploy 流程前以明确信息失败）；③ 把"R17 指纹校验"固化为便携产物验收的一部分
  （新 D 的 exe 必须 `R17 utf16 > 0`），作为 identity 之外的**语义新鲜度**佐证。
- **Interview knowledge points**：**区分"产品缺陷"与"环境缺陷"的最快判据是构造一个与产品无关的最小复现**——
  本轮若能更早写出 `build/_qprocess_probe.cpp`，就能避免在 windeployqt 的选项、PATH、环境变量、
  沙箱策略上反复排查；**环境缺陷的正确处置是如实挂起，不是在产品侧"修好"它**；
  **"ignored ≠ 无关"、"陈旧 ≠ 等价"**；"失败点前置"（fail fast at the earliest detectable point）的价值。
- **Source**：`docs/issues/ISSUE-015-qt-qprocess-pipe-blocked-packaging.md`。`[V-278]`

### ISSUE-016 — M10-F：QML `ReferenceError` + FC03 request-row overflow（**两个缺陷同时躲过全部 source-tree 门禁**）
- **Related task**：T022 M10-F correction（follow-up correction 轮）；**严重度 = HIGH**
- **Symptom**：native PowerShell 下 canonical `make_package.py` 已走到 portable runtime gate
  （deploy ✅、deploy identity ✅、staged 1498 entries + README.txt、ZIP ✅、fresh extraction ✅）但最终
  `make_package FAIL: minimal-PATH --qml-smoke-test emitted warning containing 'referenceerror'`。
  精确原始行为 **6 条** `CommunicationPage.qml:285/287/294/296/303/304: ReferenceError: preview is not defined`；
  **undefined symbol = `preview`**；受影响对象 = 三个嵌套 `Label`（`visible` / `text`）。
- **缺陷 A 的证据链（E1–E4）**：
  - **E1 不是环境问题**：`build/package-extract/…/ModbusLens.exe` 与 `build/release/modbuslens.exe`
    **同一 SHA-256**；dev PATH 下同样复现；四种组合（release/debug × offscreen/windows）**全部 6 条**、`rc=0`
    ⇒ 与 PATH / cwd / QPA **无关，是 QML 自身**；
  - **E2 根因是 QML 作用域规则**（用 canonical QML 运行时实证，`build/_qt_rca/scope_test.qml`）：
    `RESULT t1.text(root prop) = "1"` ← 组件 **ROOT 对象**的属性：可见；
    `RESULT t2.text(mid prop) = ""` + `ReferenceError: midProp is not defined` ← **中间对象**的属性：不可见。
    ⇒ **QML 无限定名解析只包含「对象自身属性 + 组件 ROOT 对象属性 + id」；中间（非 root）对象的属性
    不在嵌套子项的解析链里** ⇒ `readonly property var preview` 声明在**中间 ColumnLayout**（第 279 行）
    上，对其嵌套 `Label` **不可见——即便两者是父子关系**。
    **对照**：同一提交在 `WriteFoundationSection.qml` 把 `write06Preview`/`write10Preview`/`preparedPreview`
    声明在**组件 root**（第 77–86 行），因此那里用无限定名是**合法**的；
  - **E3 归属是测量出来的**：`git blame -L 275,310` 显示该段**全部**为 `cc3c6f8b (2026-09-23)`；
    `git show --stat cc3c6f8` 显示本文件 +58 行 ⇒ **非 pre-existing**；
  - **E4 为什么所有 source-tree 门禁都漏掉了它（**关键**）**：CTest 的 QML 门禁是
    `add_test(NAME qml_smoke COMMAND modbuslens --qml-smoke-test)`，**只用 exit code 判定**
    （无 `PASS/FAIL_REGULAR_EXPRESSION`）。而该缺陷：① 不改变退出码（`rc=0`，QML 绑定失败只是运行时诊断）；
    ② 消息在默认情况下**根本不到达 CTest**——`modbuslens.exe` 是 **WIN32 子系统**程序，
    未设 `QT_ASSUME_STDERR_HAS_CONSOLE=1` 时 Qt 把诊断写到 **OutputDebugString**，
    CTest 捕获到的 Output 块是**空的**。
    **实验（`build/_qt_rca/gate_visibility.py`）**：加上该环境变量后，**六个** QML 门禁**全部**开始输出，
    且**六个全部含 `referenceerror`**（write-foundation 另含 `typeerror`）
    ⇒ **缺陷一直都在，只是不可观测**；
- **缺陷 B 的证据链（E5）**：修好作用域后 `--qml-production-write-check` 转为 **rc=1**：
  `PRODWRITEFAIL R15: silent slave produced 0 timeouts, expected exactly one more` /
  `PRODWRITEFAIL R16: no pending request to lose`。
  临时诊断（已移除）：`DBGR15 btn visible=1 enabled=1 w=92.0 h=24.0 scene=(1186.0,241.0) win=1000x700`、
  `DBGR15 busyBefore=0 clickNamed=1 busy=0 ← 点击没有产生 pending request`。
  **按钮中心坐标 x=1186，而窗口宽 1000 ⇒ 按钮在窗口之外**；把窗口临时加宽到 1400 后同一步骤立刻恢复。
  **归属测量**（把 `CommunicationPage.qml` 换成 `cc3c6f8~1` 版本重建）：
  `pre-cc3c6f8: commReadButton scene=(891.0,222.0)` ← **在 1000 宽窗口内，可点击**；
  `post-cc3c6f8: commReadButton scene=(1186.0,241.0)` ← **在窗口外，不可点击**。
  `git diff cc3c6f8~1 cc3c6f8 -- CommunicationPage.qml` 显示该提交在**唯一的、不换行的 RowLayout** 内：
  把 `起始地址` 改成更长的 `起始地址（PDU / 0-based）`，并**新增** `HEX 0x0000` 回显 `Label`
  ⇒ 行宽超出最小窗口，把尾部 `Button` 推出可视区域。
- **Root cause（三条）**：① 缺陷 A = `preview` 声明在**中间** ColumnLayout 却被嵌套 Label 以无限定名读取
  ⇒ QML 作用域不含中间祖先；**预览功能实际从未渲染**；② 缺陷 B = `cc3c6f8` 在不换行的 RowLayout 中
  增加标签宽度，使 FC03 请求行超出 1000×700 最小窗口，把页面主操作推到窗口之外；
  ③ **检测缺口** = QML 绑定错误只产生**运行时诊断**且不改退出码，CTest 门禁只查退出码，
  而诊断在 WIN32 子系统下**默认不到达 CTest** —— 于是缺陷只能等到 `make_package.py`
  （**唯一会 grep stdout/stderr 的门禁**）才暴露。
- **Fix**：
  - **缺陷 A —— 修真正的 QML binding / ownership（不改门禁、不过滤日志）**：给预览块 ColumnLayout 加
    `id: requestPreviewPanel`；所有读取改为 **id 限定**，不再依赖作用域的隐式祖先查找；
    另增三个**有类型的投影** `previewPduText`/`previewRtuText`/`previewErrorText`（以及 `previewOk`）——
    原因：controller 的 map 在 `ok=false` 时**不含** `pduHex`/`rtuHex`、在 `ok=true` 时**不含** `error`，
    直接绑定缺失键会打印 `Unable to assign [undefined] to QString`，**等于自己制造新诊断**；
    属性留在**原块**，**未上提到页面 root，保持所有权不变**；
  - **缺陷 B —— 修真正的布局溢出**：把请求区从**一个** RowLayout 改为
    `ColumnLayout { RowLayout(从站地址/起始地址/HEX) ; RowLayout(寄存器数量/超时/读按钮) }`。
    **每一个控件、标签文本、绑定与 id 完全保留**（无文案变更、无功能变更），**只是换行**。
- **How verified**：
  1. **红→绿归因证据**：先加断言（未修）→ `qml_smoke` 失败
     `Error regular expression found in output. Regex=[ReferenceError]`；修后同测试通过；
  2. **六个诊断模式**（dev PATH + offscreen + `QT_ASSUME_STDERR_HAS_CONSOLE=1`）：
     `smoke/nav/geometry/focus/production-write → rc=0 gate_hits=none`；
     `write-foundation → rc=0 gate_hits=['typeerror']`（**E6-2 的 pre-existing 缺陷**）；
     **ReferenceError 计数 = 0（此前六个模式均为 6）**；
  3. **全量回归**：Release **36/36 PASS**（78.25 s）、Debug **36/36 PASS**（80.93 s）；
     **测试数量未变化（36），未新增 target**；
  4. **可达性**：`commReadButton` scene x 由 **1186 → 926**（窗口 1000），重新位于窗口内；R15/R16 恢复为绿。
- **E6 顺带发现的测试/工具脆弱性（本轮不修，另立任务）**：
  ① `clickNamed`（`src/main.cpp`）是**基于坐标的合成点击**：取 item 的 scene 中心把 `QMouseEvent` 直接发给 window；
  **R15/R16 调用处忽略了它的返回值** ⇒「点击失败」不会被当场发现，表现为状态断言失败而非点击错误；
  pre-`cc3c6f8` 的 R15/R16 之所以长期为绿，是因为按钮当时**恰好在窗口内**；
  ② 另一个 pre-existing 缺陷：`TransactionsPage.qml:344 TypeError: Cannot call method 'toString' of undefined`
  （归属 `9712a6cf (2026-09-18)`，**不是** `cc3c6f8`，仅在 write-foundation 可达，故未阻塞打包门禁）。
- **Regression Protection（**只加强既有门禁，不新增测试目标**）**：① 测试环境增加
  `QT_ASSUME_STDERR_HAS_CONSOLE=1`，使 Qt 诊断进入 CTest 能捕获的 stderr（**否则门禁看不到任何诊断文本**）；
  ② 对**打包门禁实际运行的同样三个模式**（`qml_smoke`/`qml_nav_check`/`qml_geometry_check`）
  增加 `FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError"`。
  **覆盖范围与不覆盖的边界（显式声明）**：覆盖 = QML 作用域/绑定失败家族（`ReferenceError`/`TypeError`）；
  **有意不覆盖**打包门禁里的 `plugin`/`missing dll`/`qimagereader`——那些是**部署完整性**关注点，
  在 source tree 里会误报（source tree 合法地从 Qt 安装加载插件）。
- **Follow-up closure（第二轮，三条全部关闭）**：① `WriteFoundationSection.qml:655/669`
  （`preparedPreview.pduHex/rtuHex` 在无 prepared 快照时 undefined，**与主缺陷同族 = map 形状不对称**，
  **每次启动、每个模式都命中**，生产可达但用户可见影响为无）已修；② `clickNamed` R15/R16 假通过洞已关闭
  （新增前置判定 `clickReachesNamed`：要求 item 存在、`isVisible`、`isEnabled`，且**中心点落在窗口矩形内**；
  R15 另加"点击必须真的产生请求"的效果断言使"凭遗留状态通过"不可能）——
  **并有负向对照证据**（临时加长标签复现"按钮被挤出窗口"→ `rc=1` 且报文指名根因
  `commReadButton is not inside the 1000x700 window, so a position-based click cannot reach it`；
  对比加固前同一变异只会报 `silent slave produced 0 timeouts`，**即根因不可见**）；
  ③ `TransactionsPage.qml` delegate 角色读取已修并**独立建档**为 ISSUE-017（结论：**生产可达**，故修复而非记为非阻塞）；
  ④ **诊断回归策略扩面**：断言从"打包门禁的三个模式"扩展到**全部六个** QML 门禁，并纳入
  `Unable to assign`——理由：它正是「缺 map key」与「失效 index 读角色」两类的**静默报文**。
  收口结果：六个诊断模式全部 `rc=0` 且 **0 条匹配诊断**；Release/Debug **各 36/36**；R15/R16/R17 全部 PASS。
- **Commit**：`cc3c6f8`（引入缺陷 A/B，同时是 M10-F 的修复主体）；follow-up 收口见后续 docs 提交。
- **Interview knowledge points**：**QML 的无限定名不是词法作用域**——只有「自身 + 组件 root + id」，
  在中间对象上放属性让子孙按名读是**静默失败**（运行时 ReferenceError，退出码仍为 0）；
  **"测试通过"与"断言覆盖"是两件事**——当被测程序把诊断写到 OutputDebugString 而门禁只看 exit code 时，
  **门禁实际上什么都没断言**；**要断言某类缺陷，必须先保证该缺陷可观测**；
  **同一提交可能藏着不止一个缺陷**（修好 A 才暴露 B）——修复必须跑到**全量回归**，而不是"目标门禁变绿就收工"；
  **合成点击是位置相关的**，控件被布局挤出窗口就静默失效，且调用处忽略返回值；
  **归属必须测量，不能推断**（把旧版本 QML 换回来重建、量同一坐标 891 vs 1186，
  比"看起来是这个提交加的"可靠得多）。
- **Source**：`docs/issues/ISSUE-016-qml-referenceerror-and-request-row-overflow.md`（含 §Follow-up closure）。`[V-279]`

### ISSUE-017 — TransactionsPage delegate 在 model index 失效后仍读取角色（**生产可达**）
- **Related task**：T022 M10-F pre-package acceptance cleanup；**严重度 = MEDIUM-HIGH**
- **Symptom**：`--qml-write-foundation-check` 在 **C15「Clear Results」** 步骤前后输出**一整块 10 条**诊断：
  271–277 行 7 条 `Unable to assign [undefined] to int/bool/QString`、
  344 行 `TypeError: Cannot call method 'toString' of undefined`、349/379 行各 1 条。
  **触发模式**：只有 `--qml-write-foundation-check` 输出（smoke/nav/geometry/focus/production-write 都不输出）；
  **触发步骤**：C15 在别的断言之间调用 `controller->clearResults()` **两次**。
- **Evidence（E1–E5，其中 E5 是决定性）**：
  - **E1**：10 条诊断覆盖 delegate 读取的**全部 7 个角色** ⇒ 若只是某字段缺失不会 7 个同时 undefined
    ⇒ 是 **index 失效**，不是数据缺字段；
  - **E2**：**只有"转换"和"调用"会报，纯 JS 比较不会** —— 与 undefined 语义完全自洽：
    会报 = 赋值给 `int/bool/QString` 属性、`undefined.toString()`、赋值给 QString；
    不报 = `model.issueText !== ""`（纯 JS 比较结果 false 分支安全）、`.arg(model.deviceAddress)`
    （arg 接受 `QVariant`）、`model.elapsedMs + " ms"`（字符串拼接得 "undefined ms" 但不报错）、
    三元判断先看 `hasExceptionCode`（undefined 为假 ⇒ 含 toString 的分支未被求值）；
  - **E3**：**一次 reset 恰好产生一块诊断** —— `TransactionListModel::setEntries()` 使用**正确**的
    `beginResetModel()/endResetModel()`（`src/ui/TransactionListModel.cpp:225-227`），
    因此**不是模型信号缺陷**；C15 调用 `clearResults()` 两次，第一次移除**唯一**一行
    （1 个 delegate 被销毁 → 1 块诊断），第二次已无行可移（无 delegate → 无诊断）——观察到的正是**一块**；
  - **E4**：归属 = `9712a6cf (2026-09-18)`（`git blame`）。**但按本轮规则：commit 年龄不构成豁免——
    分类只看是否生产可达**；
  - **E5 生产可达性（决定性）**：`src/ui/qml/Main.qml:145-149` 的 AppBar「清空结果」控件
    `onClicked: analysisController.clearResults()` 调用的**就是** harness 调用的同一个方法，
    而 `clearResults()` 里有 `transactionModel_.setEntries({})`
    ⇒ **任何用户在"有事务时"点击「清空结果」都会走同一条路径并产生同一批诊断**；
    此外任何**重置模型**的路径同理（加载 replay 日志、切换数据源）
    ⇒ **分类 = (A) 生产可达的产品缺陷**（不是 harness-only 状态），因此本轮**最小修复**。
- **Root cause**：`QQuickItemView` 在模型 reset 时会**再求值一次**每个 delegate 的绑定，
  而此时该行的 model index **已经失效**，所以 `model.<role>` 全部读成 `undefined`。
  delegate 的绑定把该 undefined **赋给有类型的属性**（`int`/`bool`/`string`）或**在其上调用方法**
  （`undefined.toString(16)`）。
- **Fix**：把 delegate 对 `model.*` 的读取**收敛到唯一一处**并使其容错——
  delegate 根 `Rectangle` 增加 `id: rowItem`；7 个 `readonly property` 改为容错读取
  （`model.x !== undefined ? model.x : <默认值>`，默认值 0 / false / ""）；
  表现层的所有读取（`height`、设备号、功能码、状态、耗时、异常码、issueText）一律改走 `rowItem.*`。
  修复后 `model.*` 在文件中**只出现于那 7 个容错读取点**（grep 可验证），其他 9 处诊断点全部消失。
  **未**抑制日志、**未**隐藏 stderr、**未**放宽门禁。
  **为什么不用 `required property` 注入角色（重要的设计取舍）**：那会把角色值在 delegate 创建时**冻结**，
  而事务行在 `pending → Success/Timeout` 时 `statusText`/`elapsedMs` 会变化，
  **冻结会破坏实时更新**；容错读取**保留了原有的响应式绑定**。
- **How verified**：六个诊断模式（dev PATH + offscreen + `QT_ASSUME_STDERR_HAS_CONSOLE=1`）
  **全部 rc=0，且全部 "no matching diagnostic"**（ReferenceError / TypeError / Unable to assign /
  cannot call method / undefined 关键词命中数 = **0**）；真实 ctest **Release 36/36 PASS**（78.98 s）、
  **Debug 36/36 PASS**（81.30 s）。
- **Regression Protection**：本轮把 CTest 的输出断言从"三个打包模式"扩展到**全部六个** QML 门禁：
  `FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;Unable to assign"`——
  `Unable to assign` 正是本缺陷（以及"缺 map key"那一类）的**静默报文**，
  现在任何 QML 门禁一旦出现即 FAIL，**不再依赖打包末端才发现**。
- **建档纪律教训（**本 issue 最独特的部分**）**：本文档与既有的
  `ISSUE-014-transactions-delegate-reset-binding-warnings.md` 记录的是**同一个现象**。
  ISSUE-014（更早，提交 `0d5c219`，M10-C3 时期）已完整记录**同一组告警**（271–277 + 344 + 349/379），
  根因描述一致，当时处置为「本轮不修」+ `PRE-EXISTING NON-BLOCKING`，
  并**已建议**"在未来的 warning hygiene / UI robustness 任务统一处理（给 delegate 绑定加 `?? 0` / `?? ""`）"。
  本轮在**未检索既有 Issue** 的情况下**重复建档**，并按 ISSUE-014 已建议的方式实施了修复——
  **修复本身有效且已验证**，但本文档应视为**对 ISSUE-014 的执行闭环**，而不是一个全新的问题。
  两处观测（write-foundation 的 TransactionsPage 告警）在 ISSUE-014 的「复现步骤」里**被逐字写明**。
  **教训：建档前应先按关键字（错误文本 / 文件名 / objectName）检索 `docs/issues/`，
  而不是只凭"本轮首次观察到"就认定为新问题。**
- **Interview knowledge points**：**delegate 的 `model.*` 读取必须容错**——模型 reset（清空、重载、换源）
  会让 QML 在 index 已失效时再求值一次；把 undefined 赋给有类型属性或在其上调用方法都会静默产生诊断/异常；
  **"只在测试里看到"不等于"测试专属"**——分类要靠**调用路径**（这里是真实 AppBar 按钮绑定的同一方法），
  不能靠"谁先发现"；**"一次 reset 只出一块诊断"这种"数量特征"是很好的机制指纹**——
  它把范围直接锁定到「每个被销毁的 delegate 再求值一次」，而不是逐行/逐字段的数据问题；
  **修复选型要保住响应式语义**（容错读取 > `required property` 冻结）。
- **Source**：`docs/issues/ISSUE-017-transactions-delegate-undefined-roles.md`。`[V-280]`

### ISSUE-018 — `writeFoundationPanel` 在真实 windows QPA / 1000×700 下被窗口裁切（**写区从未被计入页面布局**）
- **Related task**：T022 M10-F final portable verification；**严重度 = HIGH**（真实平台下验收门禁**直接 FAIL**）
- **Symptom**：真实 Windows 平台（`QT_QPA_PLATFORM=windows`，**非 offscreen**）下：
  `WRITEFAIL C4 geometry 1000x700 0x10: writeFoundationPanel is clipped by the window (scene 73,395 911x319 vs window 1000x700)` /
  `WRITEFAIL C4 geometry 1000x700 0x10: the validation message is clipped by the window`；`exit code = 1`。
  **`0x06` 变体通过，只有 `0x10` 变体（FC16 草稿列可见、面板高 319）失败**。
- **Evidence（E1–E4）**：
  - **E1 修复前精确几何**：`communicationContentLayout (73,57) 911x627 bottom=684`（可用内容高度 627）；
    `writeFoundationSection (73,363) 911x**0**` ← **高度被布局分配为 0**；
    `writeFoundationPanel (73,395) 911x319 bottom=714 ✗ > 700`；
    `writeValidationError (85,686) 887x16 bottom=702 ✗ > 700`；
  - **E2 不是 packaging / portable-only 问题**：**同一二进制**在 source tree + windows QPA 下报**完全相同**的
    两条失败，而 offscreen 下 `rc=0` ⇒ 差异来自 **QPA 字体度量**：
    实测同一行文字在 windows 下 **16px**、offscreen 下 **12px**
    （`writeValidationError` 高度 16 vs 12；`writeFoundationPanel` 319 vs 305）；
  - **E3 不是"一直如此"的平台假象**（**用旧产物做历史对照**）：修正前的旧产物
    `build/package-retention-20260923-m10f-corrected/release-deploy-stale-0832/ModbusLens.exe`
    （sha256 `d5a49582…1e87`）在**同样 windows QPA** 下 `rc=0`：
    `OLD: writeFoundationPanel=(73,305 911x271) → bottom=576 ✓`；
    `NEW: writeFoundationPanel=(73,395 911x319) → bottom=714 ✗`
    ⇒ 是**近期 UI 增量**把底边推过了阈值：**y 下移 +90px**（主因）、**h 增高 +48px**（次因）；
  - **E4 逐项量化**：`communicationRequestSection 80 → 138 (+58)` 来自
    `cc3c6f8`（FC03 Function 标签 + 请求行加宽）+ `8d78ddb`（请求行由 1 行拆为 2 行，**恢复窗口内可达性**）；
    页面纵向间距 `6 × 12 = 72`（既有 `spacing: DS.spacingM`）；
    `writeFoundationPanel (h) 271 → 319 (+48)` 来自 `cc3c6f8` 在写区新增 FC16 Quantity/ByteCount 派生行与预览行。
    ⇒ **y +90px 的主因不在写区，而在它上方的请求区（+58）与页面间距（72 中的一部分）**。
- **Root cause（两个叠加缺陷）**：
  1. **`WriteFoundationSection` 根 `Item` 没有 implicit 尺寸**（`Item` 默认 `implicitHeight = 0`），
     而它内部 `ColumnLayout { anchors.fill: parent }` 的内容**仍然按自然尺寸渲染**
     （SectionHeader 20 + spacing 12 + PanelCard 319 = **351**）
     ⇒ 页面 ColumnLayout 认为该 section 高 **0**，**整页高度计算完全漏掉写区**，
     写区永远以"溢出"形式绘制，**布局对它没有任何控制权**；
  2. **内容总高超过可用高度**：自洽高度 = 306（页头/连接/请求）+ 12 + 351 = **657** > 可用 **627**，
     超出 **30px**——与实测完全吻合（panel 底边 714 − contentLayout 底边 684 = **30**）。
- **为什么一直到 portable 验收才发现（**本 issue 最核心的系统性结论**）**：source-tree 的六个 QML 门禁
  **全部**用 `QT_QPA_PLATFORM=offscreen`（为了 headless-safe），而 offscreen 的行高比真实平台小 4px，
  逐行累计后在 offscreen 下仍有 **41px 余量**（底边 673 ≤ 700）
  ⇒ **门禁的平台与验收的平台不是同一个**。
- **Fix（保留全部协议信息，只用真正的布局手段回收 46px）**：
  ① `WriteFoundationSection.qml`：让写区重新成为一等布局参与者——
  `implicitWidth: writeContentLayout.implicitWidth` / `implicitHeight: writeContentLayout.implicitHeight`
  （注释说明：内部 ColumnLayout 的 implicitHeight 只来自其子项，故与 `anchors.fill` **不构成绑定环**）；
  ② `CommunicationPage.qml`：PDU 与 RTU Frame 预览**并排一行**（各带 `elide`，各占半宽）——节省 ~15–18px；
  ③ 页面 `communicationContentLayout` 间距 `DS.spacingM`(12) → `DS.spacingS`(8)：6 个间隙回收 24px，**分组结构不变**；
  ④ 写区内部间距同样 12 → 8：回收 4px。
  **未删除任何功能、未缩小字号、未隐藏协议信息、未减少 Human #10/#11 所需信息。**
- **How verified**：修复后（windows QPA, 1000×700）
  `communicationRequestSection (73,197) 911x123 ← 138 → 123`；
  `writeFoundationSection (73,328) 911x347 ← 0 → 347（现在被正确计入）`；
  `writeFoundationPanel (73,356) 911x319 bottom=675 ✓ ≤ 700（余量 25px）`；
  `writeValidateError (85,647) 887x16 bottom=663 ✓ 可见`；`writeActivateButton (85,605) 50x34 bottom=639 ✓ 可达`；
  ⇒ `--qml-write-foundation-check = rc=0 PASS`；
  **windows QPA 六门禁**（smoke / production-write / write-foundation / focus / nav / geometry）
  **全部 rc=0**，且 ReferenceError/TypeError/Unable to assign 计数**全部为 0**；
  R15/R16/R17 全部 PASS（R15/R16 的 `clickReachesNamed` 前置断言通过 ⇒
  **运行时已证明 `commReadButton` 位于 1000x700 窗口内**）；真实 ctest **Release 37/37 PASS**（88.30 s）、
  **Debug 37/37 PASS**（90.13 s）。
- **Regression Protection**：`CMakeLists.txt` 新增**一个**条目（**不是把全部 CI 改成 windows**）——
  `add_test(NAME qml_write_foundation_check_windows COMMAND modbuslens --qml-write-foundation-check)`
  + `ENVIRONMENT "QT_ASSUME_STDERR_HAS_CONSOLE=1"`（**不给 offscreen ⇒ 真实平台**）
  + `FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;Unable to assign"`。
  **复用了既有 oracle（同一个 `--qml-write-foundation-check` 的 C4 几何断言），未建第二套体系**；
  测试数 36 → **37（如实披露）**；该条目需要**交互式 Windows 会话**；
  它覆盖的正是本次漏掉的维度：**真实平台字体度量下的 1000×700 布局**。
- **Commit**：`d08ab55`（`T022: keep the write section inside 1000x700 on the real Windows platform`）。
- **Interview knowledge points**：**门禁的平台必须与被验收的平台一致**——offscreen 行高 12px vs 真实平台 16px，
  逐行累计足以把一个"刚好放得下"的页面推过窗口边缘；**所有 QML 门禁都用 offscreen ⇒
  真实平台几何从未被断言**；**`Item` 作为自定义组件根必须有 implicit 尺寸**——
  默认 0 会让父布局**静默少算整块内容**，表现为子项"溢出渲染"而不是布局错误
  （**定位时必须把 scene 矩形逐项打印出来才能看见**）；
  **几何回归要按"谁在它上面"逐项量化**——本次 +90px 的下移主因在**写区上方**的请求区，
  而不是写区自身；**只看失败对象（写面板）会得出错误结论**；
  **旧产物是宝贵的历史对照**——用 retention 里修正前的部署树在**同一 QPA** 下量同一坐标，
  才能把"一直如此"与"近期增量造成"区分开。
- **Source**：`docs/issues/ISSUE-018-write-panel-clipped-windows-1000x700.md`。`[V-281]`

### ISSUE-019 — `setDisplayName` 缺失 NOTIFY 信号：NC-B1 负向对照变异**被提交进行为提交**
- **Related task**：M12-B first slice（自动化验收门禁 `qml_profile_editor_check`）
- **Symptom**：该门禁 **7 项 FAIL**（Debug/Release ctest 43/44），其中最反直觉的一项：
  **控制器 C++ 状态完全正确**（`validationText="设备名称必填"`、`dirty=true`），
  但 QML 的 `profileValidationText` Label 与 `profileDirtyIndicator` 在**事件循环多转一圈后仍然显示旧值**
  （`text=[]`、`visible=0`），且**全程零 QML 警告**。
- **Expected**：任何 identity 字段写入都必须发出 `editorChanged`，所有绑定它的 QML 属性
  （dirty 指示、validation 文本、编辑器字段）必须同步刷新。
- **Impact（**为什么 23 个 controller tests 一个都抓不住**）**：仅改 displayName 时，
  dirty 指示不出现、validation 文本不出现。这是**通知缺陷（notification defect）而非计算缺陷**：
  所有 getter（`dirty()` / `validationText()`）都是**按需计算**，C++ 单元测试全部 PASS，
  **只有 QML 绑定可见**。
- **Investigation**：用临时仪器化（PROFSTATE/PROVDEFER/PROVIDENT）证实：
  **控制器对象同一、getter 返回新值、QML 无警告** ⇒ 唯一可能是 **NOTIFY 信号没有发出**。
  读 `src/ui/profile/ProfileController.cpp` 的 `setDisplayName()`，看到：
  ```cpp
  m_draft.displayName = converted;
  // NC-B1: dirty not set          ← 变异标记注释
  }                                  ← emitEditorChanged() 缺失
  ```
  `git show HEAD:src/ui/profile/ProfileController.cpp` 证实该变异**已被提交**（behavior commit `da07f43`）。
- **Root cause（**流程缺陷 × 技术缺陷叠加**）**：
  1. **流程**：**NC-B1 负向对照（临时移除 emit → 门禁红 → 还原）的"还原"步骤被跳过**，
     变异源码进入了行为提交；且**变异标记注释留在了提交里**；
  2. **技术**：`editorChanged` 是**唯一** NOTIFY；getter 按需计算使 C++ 测试对"缺通知"**完全免疫**
     —— 通知契约**没有任何自动化回归保护**。
- **Fix**：① `setDisplayName()` 恢复 `emitEditorChanged()`、删除变异注释（behavior commit `b502ea8`）；
  ② 新增**通知契约测试**：`b1c21`（displayName 写入 ⇒ `editorChanged` **恰好 1 次**；
  幂等写入 ⇒ **0 次**）与 `b1c22`（5 个 setter **各通知 1 次**）
  —— **用 `QSignalSpy` 把"通知"本身变成被测对象**。
- **How verified**：`modbuslens_profile_controller_tests` **35 passed, 0 failed**；
  `qml_profile_editor_check` **PASS**（stage 6 由 FAIL 转 PASS）；Debug/Release full CTest **44/44 / 44/44**。
- **附加纠正（同轮报告纪律问题）**：上轮报告在 **43/44** 的情况下写了 "AUTOMATED PASS"，
  并把门禁失败归因为"**QQuickPopup automation 限制**"——**实际 6/7 项失败是真实产品缺陷或 harness 缺陷**
  （见 T027 §37 的逐项分类 A = 真实产品缺陷 / B = harness 缺陷 / C = 不可自动化 / D = oracle 错误）。
- **Interview knowledge points**：**负向对照必须有"还原后复绿"的闭环证据**，否则变异可能就停在源码里；
  **含变异标记注释（`// NC-B1: ...`）的源码出现在 `git show` 输出里是可直接 grep 的红旗**；
  **NOTIFY 契约需要显式测试**——按需计算的 getter 让"状态对但界面不刷"成为 C++ 测试**盲区**，
  `QSignalSpy` 是最小充分手段；**报告纪律**：不能在门禁失败的情况下写 PASS，
  也不能把真实缺陷归因为"框架限制"。
- **Source**：`docs/issues/ISSUE-019-displayname-missing-notify-committed-mutation.md`。`[V-282]`

### ISSUE-020 — 环境凭据泄漏进确定性 harness：同意门检查在授权下**发起真实云端推理**
- **Related task**：M12-C C2 · SESSION P transport slice（T027 §76 授权 / §77 修复授权）
- **定性**：**测试卫生 + 安全边界缺陷**（**产品代码行为符合 SESSION P 设计**；
  缺陷在于**确定性 harness 与 ambient 环境的边界**）
- **Symptom（含"静默崩溃"假象）**：
  1. 2026-09-30 14:53，WorkBuddy 在 fresh acceptance tree `build/acceptance/session-p-r1b-release` 上运行
     `ctest -R qml_consent_check`：**Test Failed，零输出，耗时 15.87s**
     （`LastTest.log`：`<end of output>`）——当时被当作"fresh-tree ctest crash"上报；
  2. ZCode 接管后复跑（ambient 环境不变）得到**语义失败而非静默崩溃**：
     `CONSENTFAIL: R07: Agree wrote a candidate without a provider result`（exit 1）；
  3. 同一二进制在 `MODELSCOPE_API_KEY` 被**显式移除**后：`CONSENT CHECK PASS`（exit 0，1.87s）。
- **Expected**：`--qml-consent-check` 是**确定性 harness**：无凭据时 Agree 只能推进到 `not_configured` 失败态，
  `candidateCount` 恒为 0（R07 断言，SESSION O-R2 已 Human 验收）。
  **任何 deterministic gate 永不发起真实网络请求。**
- **Evidence（单变量对照，4 个 Run）**：
  | Run | `MODELSCOPE_API_KEY` | `QT_ASSUME_STDERR_HAS_CONSOLE` | 结果 |
  | --- | --- | --- | --- |
  | WorkBuddy ctest 14:53（windows QPA） | **PRESENT**（User 级持久环境，len=39） | 未设置 | exit≠0、**零输出**、15.87s |
  | ZCode Run A（offscreen） | **PRESENT**（ambient） | 设置 | exit 1、**R07 FAIL（candidateCount=1）** |
  | ZCode Run B（offscreen） | **ABSENT（`env -u`）** | 设置 | exit 0 PASS、1.87s |
  | ZCode Run C（offscreen） | **ABSENT** | 未设置 | exit 0 PASS、**零输出** |
  - `MODELSCOPE_API_KEY` 在 Windows **User 级环境持久存在**（长度 39；**值未读取、未打印**），
    因此注入每个测试进程。Run A → R07 失败 ⇒ `candidateCount=1` ⇒ 已走完
    HTTP 200 → strict parser → 本地 Evidence 验证全链 ⇒ **发生了真实 ModelScope 推理**；
  - **15.87s − 1.87s ≈ 14s ≈ 真实 HTTPS 往返**，与 WorkBuddy 14:53 运行同样包含真实调度一致；
  - **零输出机制**：`qml_consent_check`（**非 `_windows` 变体**）是 GUI-subsystem exe 且
    **未设置** `QT_ASSUME_STDERR_HAS_CONSOLE=1`，ctest 管道下 Qt 诊断**全部丢弃**
    （Run C 证实：exit 0 + **0 字节输出**）⇒ **失败呈现为"静默崩溃"**。
- **Investigation（5 步）**：① 从 `LastTest.log` 确认 WorkBuddy 运行为零输出失败；
  ② offscreen 复跑 → 得到 R07 语义失败（非崩溃）；③ 检查凭据存在性（**只查存在、未读值**）→ User 级 PRESENT；
  ④ 单变量对照 Run A/B/C → **唯一决定变量 = ambient 凭据**；零输出 = stderr 契约缺失；
  ⑤ 代码链核对：`CandidateExtractionController::beginAttempt` → `ModelScopeCandidateRunner::begin` →
    （有凭据时）`ensureWired()` → `ModelScopeExtractionTransport::send` →
    `QtModelScopeHttpClient::post`（**真实网络**）。
- **Root cause（两个独立缺陷叠加）**：
  - **RC-1（核心）**：SESSION P 使 production runner **不再惰性**后，ambient `MODELSCOPE_API_KEY`
    成为确定性 harness 的**隐藏输入**。harness **没有声明自己的配置真值**，凭据从 User 环境**泄漏进 gate**，
    同意门点击被降级为**真实 provider 调度**。违反 §77.5 同构原则
    （"canonical acceptance 不得依赖 ambient 环境真值"）与 H2/H3 的 credential/consent 边界精神；
  - **RC-2（可观测性）**：`qml_consent_check` 缺少 `QT_ASSUME_STDERR_HAS_CONSOLE=1`
    （**其余所有 gate 均有**），失败诊断被静默吞掉，把**语义失败伪装成"crash"**。
- **Fix**：① `src/main.cpp`：**任何 `--qml-*` harness 进程在创建任何 controller 之前**
  `qunsetenv("MODELSCOPE_API_KEY")` + `qunsetenv("MODBUSLENS_MODELSCOPE_MODEL")`
  —— **harness 自己声明配置真值**；production（无 `--qml-` 参数）不受影响；
  ② `runConsentCheck` 入口 **guard**：若凭据对 harness 可见，立即
  `CONSENTFAIL ... refusing to run`（exit 1），**把未来的泄漏变成响亮失败而非真实调度**；
  ③ `CMakeLists.txt`：`qml_consent_check` 补 `ENVIRONMENT "QT_ASSUME_STDERR_HAS_CONSOLE=1"`。
- **How verified**：① 修复后同配置（ambient token PRESENT）`ctest -R qml_consent_check` → **Passed 3.73s**；
  ② **负向对照 MUTATION-NX1**（注释掉 token `qunsetenv`，**精确 patch**）→ rebuild →
  `CONSENTFAIL: harness credential contract violated`（exit 1，1.58s，**入口即拒绝 ⇒ 零调度**）→
  **精确逆向还原** → rebuild → **Passed 5.58s**，residue 0（grep 无残留）；
  ③ `candidate_transport`（P01–P24）Passed。
- **Regression Protection**：R07 断言本身（**无 provider 结果不得写 candidate**）保持不变
  —— 本轮它**正是探测点**；`runConsentCheck` 入口凭据 guard：任何未来回归
  （删除清理、或新增读取环境凭据的路径）都会变成**响亮的确定性失败**，而不是静默真实网络调用。
- **Governance disclosure（**必须向 Human 披露**）**：WorkBuddy 2026-09-30 14:53 的 ctest 运行与
  ZCode Run A（RCA 复现）**各自可能已发生一次真实 ModelScope 推理**
  （payload = 8 行种子手册文本；token 为 **Human 的 User 环境凭据**；模型 = 接受的默认 `Qwen/Qwen3.5-27B`）。
  两轮均**无任何 Agent 读取/打印 token 值**；seed 文本非机密。
  **此披露不构成对真实推理的授权请求，仅如实记录已发生事实。**
- **追加澄清批注（2026-10-02，不改写历史）**：本文记录的**未授权事件仅限** P-R1C 期间的两起
  （WorkBuddy 14:53 = 分级 **LIKELY**；ZCode Run A = 分级 **STRONGLY SUPPORTED**）。
  其后由 **Human 显式授权、经产品同意流**完成的 P-R1 live ModelScope smoke
  （T027 §79：Agree → Running → PendingReview，PASS）**不属于**本 issue 的未授权事件，**两者不得混同**。
- **Commit**：`613a32a`（`M12: complete session P transport and sanitize harness credentials`）；
  RCA 存档 `c78c960`。
- **Interview knowledge points**：**"测试是确定性的"必须包括环境输入**——把 ambient env 当作隐藏参数的
  harness 在某个用户机器上必然失效，**且失败模式可以是"静默真实副作用"而非可见断言失败**；
  **安全边界（credential/consent）要有结构化防泄漏**——与其信任调用者不设凭据，
  不如在 harness 入口**显式清除 + 入口断言**；
  **GUI-subsystem exe 的 gate 必须声明 stderr 契约**，否则失败不可诊断
  （本次"crash 之谜"的直接来源）；**同类问题的通用形态**：
  凡是"确定性验证"流程，都要问一句"这个进程还从环境继承了哪些我没声明的输入？"
- **Source**：`docs/issues/ISSUE-020-harness-ambient-credential-live-dispatch.md`。`[V-283]`

---

## 第二部分 — 任务档案内 RCA（无独立 Issue 文件）

### RCA-A — CRC 线上字节序被当成数值记录（T002/T004）
- **Source**：`docs/12_RESUME_INTERVIEW_QA.md` §5.3；`docs/07_FINAL_PROJECT_REVIEW.md` 第十章。
- **Symptom**：把**规范上印的线上字节序**（`41 12`、`76 87`）误当成**数值**（0x4112、0x7687）记录，
  写进断言/金样。
- **Root cause**：混淆"**计算值**（寄存器最终 uint16）"与"**线上两字节先后顺序**（低字节在前）"
  ——两者看起来像"倒了个"，实际是**数值约定 vs 字节序约定**的差别。
- **Fix**：以**官方规范原文**为参照重写金样；CRC 的"计算值 vs 线上字节"两套记法在
  `docs/03` §10.9/§10.11 显式区分并做成 KAT 表。
- **How verified**：断言当场抓出该错误（对拍机制生效）；`CRC-T01~T06` 的 KAT 覆盖。
- **Interview knowledge points**：**自己跟自己比会一致地错**（编码与解码字节序同时反了，互逆测试全绿；
  CRC 自算自验也只会"一致地错"）⇒ 必须有**外部参照**（官方规范示例报文 / 真实抓包 / 独立复算）；
  `uint16_t` 绝不能直接 `memcpy` 进帧（依赖主机端序），必须手工"先写低字节、再写高字节"。`[V-284]`

### RCA-B — Generic exception matcher 让 0x88 **自我匹配**（T015 语义审计）
- **Source**：`docs/04_TEST_STRATEGY.md` §3 被动回放扩展；`docs/11_PROJECT_FINAL_RETROSPECTIVE.md` §4.6 故事一。
- **Symptom**：旧逻辑把 **request 0x88 / response 0x88** 误判为 "Exception 0x01"。
- **Root cause**：generic exception matcher 缺前置守卫 `(request.functionCode & 0x80) == 0`
  —— 当请求自身功能码**已带 0x80 位**时，`fn|0x80` 的匹配会退化为**自匹配**。
- **Fix**：加最小 guard；修复后 0x88 进入既有 `Unsupported` 路径。
- **How verified**：**Review 前的专项语义审计**用一条 RED 测试抓出
  （`PASSIVE-P16`：**RED = 21 passed / 1 failed**）→ guard → **GREEN = passive 22/22**。
- **Commit**：`02ce302`（`T015: guard generic exception matcher against request function MSB`）。
- **Interview knowledge points**：**主动在 Review 前自审抓出协议语义边界 bug，比"测试都过"更有力**；
  `fn | 0x80` 这类位运算匹配必须校验**前置条件**（否则会自匹配）；
  该守卫现在被冻结为 V1 契约（`docs/11_V2_UPGRADE_PLAN.md` §1）。`[V-285]`

### RCA-C — FC16 无响应派发**没有任何可见 terminal**（M10-F）
- **Source**：`docs/04_TEST_STRATEGY.md` §3 请求预览 / 事务透明；T022 M10-F。
- **Symptom**：Human review 发现——FC16 无响应派发时，UI **没有任何可见终态**。
- **Root cause**：`handleSerialTransactionCompleted` 的 notice gate **仅覆盖 `0x06`**；
  且更根本的是**harness 只能驱动 0x06 timeout** ⇒ **FC16 timeout 从未被 oracle 覆盖**
  （**Human review 才发现**）。
- **Fix**：写超时 notice（`WriteDispatchNoticeKind::WriteTimeoutUnknown`）覆盖 `0x06` **与** `0x10`
  （READ 超时明确不覆盖）；同期加入 structured request builder 与协议透明性基础。
- **How verified**：`test_write_dispatch::fc10WriteTimeoutNoticeSaysUnknown` +
  `--qml-production-write-check` **R17**（FC16 无响应派发 → **恰一个 Timeout** + 冻结文案 + **无 Success**；
  harness 新增 `completeWriteWithTimeout16()`）。
- **Commit**：`cc3c6f8`。
- **Interview knowledge points**：**"测试通过"可能只覆盖了一个分支**——
  harness 的能力边界直接决定了 oracle 的覆盖边界；**"无假成功"原则**：
  失败/超时必须呈现为超时，绝不能呈现为成功；**Human review 能发现自动化永远发现不了的分支缺失**。`[V-286]`

### RCA-D — M12-D Manual Q&A "无法解析"（双根因）
- **Source**：`docs/tasks/T027` §119.6 item 6；`docs/INTERVIEW_NOTES.md` F7。
- **Symptom**：真实 provider 返回**解析失败**（"无法解析"）。
- **Root cause ×2**：① **响应抽取层**问题；② **thinking 模式未关**（`enable_thinking`）。
- **Fix**：抽取层修复 + **请求契约**修复（显式 `chat_template_kwargs` / `max_tokens`）
  + 契约测试 **`qa36`**；15 类解析失败类别可观测。
- **Interview knowledge points**：**结构化输出必须显式禁用思考模式并做契约测试**；
  "provider 返回了合法 HTTP 200" **不代表**"内容符合我方契约"；
  解析失败必须是**可观测的类别**而不是一个笼统的"无法解析"。`[V-287]`

### RCA-E — 引用校验 round-trip 失败 → app-resolved citations
- **Source**：`docs/tasks/T027` §119.6 item 7；`docs/INTERVIEW_NOTES.md` F5。
- **Symptom**：本地 citation **round-trip 失败**（provider 给出的 offset 与实际文本对不上）。
- **Root cause**：**让 provider 编造 offset 不可靠**——模型无法成为权威数据源。
- **Fix**：**app-resolved citationIds**——provider 只能在 app 提供的**上下文块 id** 中"选择"，
  `offset`/`hash` 由 app 侧**回填**；**未知 id fail-closed**。
- **How verified**：本地引用校验（六字段 citation 契约 + 本地验证）+ 真实 Live 经产品同意流
  （T027 §79：Agree → Running → PendingReview，PASS）。
- **Interview knowledge points**：**"LLM 输出只能做选择，不能做权威数据"**
  ——这是"AI 不产生协议事实"在文档智能域的同构延伸；
  工程做法 = 把不可靠的部分（offset 计算）**移出模型**，把可靠的部分（选择）**留给模型**；
  **fail-closed 是默认姿态**（未知 id 不是"尽力而为"，而是拒绝）。`[V-288]`

### RCA-F — 发布链两个 fail-closed + candidate 不可重生成（REL-R3）
- **Source**：`docs/tasks/T027` §119.6 item 8。
- **Symptom**：真实 candidate 打包被 **icon 镜像**与 **`.a`/`.prl`/`.obj`/`.qrc` 工件规则**拦截**；
  worktree **缺 git-ignored distfiles**。
- **Root cause**：**REL-R2 的 fixture 未覆盖真实 candidate 内容**；**worktree 不携带 ignored 文件**。
- **Fix**：显式排除规则 + **PKG-25/26** 断言 + distfiles 补齐。
- **Interview knowledge points**：**fixture 必须来自真实制品**（否则 fixture 与真实制品漂移，
  而漂移只在真实制品上暴露）；**"ignored ≠ 无关"**——被 gitignore 的文件可能仍是构建/打包的必需输入；
  **fail-fast 打包器把"打包错产品"变成不可能**是同一条纪律的另一面。`[V-289]`

### RCA-G — **门禁自身的缺陷**两例（如实记录）
- **Source**：`docs/tasks/T027` §119.6 item 9。
- **Symptom ①**：反射断言 `property("open")` **应为 `opened`** ⇒ 断言**静默失效**（属性不存在，
  断言取到空值而没有失败）。
- **Symptom ②**：`cancelCount` 用**绝对断言**，但该计数**跨 stage 累积** ⇒ 应改为 **drift 比较**（before/after）。
- **Fix**：修正属性名 + 改 drift 比较。
- **Interview knowledge points**：**反射断言必须对关键属性做存在性校验**
  （否则拼写错误会让断言永真——与 ISSUE-013 的"空转守卫"同族）；
  **计数器类断言只能做 before/after 比较**，绝对断言在跨阶段累积量上必然脆弱；
  **门禁本身也要被验证**，这是本项目第三次踩"证据侧缺真值对照"。`[V-290]`

### RCA-H — Demo 进程生命周期过早退出（人类语义演示）
- **Source**：T027 §44（`docs/tasks/T027-m12-device-profile-manual-intelligence-contract.md:1616-1695`）；
  commit `13799d6`。
- **Symptom**：人类语义演示（`--qml-profile-semantic-demo`）在 Human 还没看完时就自己关掉了。
- **Fix**：`13799d6`（`M12: keep the human semantic demo alive until closed`）——
  语义演示必须**保持存活直到被显式关闭**。
- **How verified**：生命周期矩阵 `build/verify_demo_lifetime.py`；随后归档为该 RCA（`7ad4501`）。
- **Interview knowledge points**：**demo harness 的生命周期本身是被测契约的一部分**——
  "自动退出"在自动化里是优点，在人工验收里是缺陷；**同一个行为在不同评审口径下评价相反**；
  凡是"给人看的"工具都要显式定义生命周期。`[V-291]`

### RCA-I — M11 视觉 candidate 因 ambient 旧 runtime 而不可启动（**撤回而非回滚**）
- **Source**：`docs/BACKLOG.md` T025/T026 行；`docs/PROJECT_STATUS.md` 历史块；commits `c08f0ba`/`d58fd4d`。
- **Symptom**：Human 双击 `build/release/ModbusLens.exe` 遇到**三个 loader 错误**
  （GCC 13.1 符号缺失于 ambient 旧 `libstdc++-6.dll`）。
- **定性（**关键判断**）**：**旧 candidate 撤回 = INVALID / NOT LAUNCHABLE**；
  **非 UI FAIL、非 M11 代码缺陷**，因此 **`b95de54` 不回滚**（代码是对的，交付形态不对）。
- **Root cause**：`build/release` **不是 deployed tree** + ambient PATH 旧 runtime 先被解析
  （与 ISSUE-002 同族，但这次发生在 M11 的 staging 路径上）。
- **Fix**：构建**自包含 staging** `build/m11-visual-candidate/`
  （`windeployqt 6.11.1` + `--compiler-runtime` + `--qmldir` + 文件系统模块目录；
  三个 MinGW runtime 与 GCC 13.1.0 工具链 **byte-identical**）；
  clean-env（`PATH=System32;Windows`）smoke / read-result（M6–M9，0/0/0）/ production-write（R15/R16/R17）全部 PASS。
- **RCA 措辞纪律**：`D:\mingw64` 旧 runtime 被描述为
  **"已控复现的 offending candidate，非直接捕获的 loaded path"**——
  **不把推断写成已捕获的事实**。
- **Interview knowledge points**：**"撤回 candidate"与"回滚代码"是两件事**——
  区分"产品缺陷"与"交付形态缺陷"能避免误伤正确代码；
  **自包含 staging 是可复现的交付形态**；
  **RCA 措辞必须区分"已捕获"与"已控复现"**。`[V-292]`

### RCA-J — `commStartHexEcho` 双参 `.arg()` 运行时错误
- **Source**：`docs/BACKLOG.md` T025/T026 行；commit `d58fd4d`。
- **Symptom**：QML 报 `Invalid arguments`（自 `352b81c` 起）。
- **Root cause**：QML 的 `String.arg()` **双参形式**在该上下文中不可用/参数不匹配。
- **Fix**：改为**链式** `.arg()`；同时把 ctest 的 `FAIL_REGULAR_EXPRESSION`
  **扩为 4 模式**（并用**负向对照**实测 FAIL→PASS，证明该断言真的能触发）。
- **Interview knowledge points**：**QML 的 `arg` 是重载家族**，多参调用在版本/类型上有差异，
  链式调用是最稳的写法；
  **"加了断言"必须配负向对照**（故意制造失败并观察其失败），否则不知道断言是否空转。`[V-293]`

### RCA-K — T013 视觉三连 FAIL 与 "原生 style 静默忽略自定义"
- **Source**：`docs/07_FINAL_PROJECT_REVIEW.md` 第四章 T013；`docs/04_TEST_STRATEGY.md` §6。
- **Symptom**：Phase B/C/D **连续三轮** Manual Visual Review FAIL
  （深色对比度与层次、串口空态、Tab 边框、表格对齐、宽度利用）。
- **Root cause（之一，最具技术性）**：**Qt Quick Controls 的原生 style 会静默忽略 QML 自定义**——
  改样式不生效且不报错 ⇒ 改用 `Fusion` style。
  另："串口枚举为 0"经 Qt 同 runtime 取证确认是**机器 serial availability 变化，非应用回归**。
- **Fix**：Light + TabBar 三页布局、稳定事务表格、比例列宽、端口刷新修复、Fusion style。
- **How verified**：第三轮 Manual Visual Re-Review **PASS**；**失败 candidate 全部保留在 Git 历史**
  （`aea1e64`/`5a2f60c`/`a25d63c`）。
- **Interview knowledge points**：**自动化是下限，人工视觉是成品上限**——
  Phase B/C 自动化全绿、部署 smoke PASS，但用户连续两次 FAIL；
  **"改了样式却没生效"要先怀疑 style 平台而不是自己写错**；
  **机器环境变化会被误报为应用回归**——必须用 Qt 同 runtime 取证区分。`[V-294]`

### RCA-L — 串口最终截图重拍 BLOCKED（产品**诚实枚举**导致的"不可构造状态"）
- **Source**：`docs/tasks/T030-ui-freeze-final-screenshots.md`（§T030-R2）。
- **Symptom**：Human 拒绝 `05-serial.png`，理由逐字为：
  ① `COM_HARNESS` 暴露自动化 fixture / internal naming；② 页面同时出现"串口已打开"与
  "未检测到串口"**视觉状态矛盾**；③ FC16 一次显示 **40 registers**，信息密度过高。
  **"不是 UI layout defect"**（UI FREEZE 继续有效）。
- **Investigation（fail-closed 过程）**：accepted exe 重验一致（`518b9485…523acb` / 7,029,729 B）；
  本机串口环境实测**枚举出 COM3 / COM4**（"蓝牙链接上的标准串行" = Bluetooth SPP 虚拟口）；
  重拍 attempt 1/2（既有 gate，使用非 production fixture、`COM_HARNESS` 硬编码于 gate harness）
  → 产生被拒的同一状态；attempt 3（真实 UI 自动化，real clicks/typing on accepted exe，9 张诊断截图）：
  d2 刷新后仍"未检测到串口"、d3/d4 下拉打开但**无任何条目（popup 空列表）**、
  d8 确认写入不可达（**写入按钮保持禁用 = M10 写安全边界在自动化下依然成立**）。
- **Root cause**：**产品诚实枚举**——本机**无物理在位的串口设备**；
  COM3/COM4 是 Bluetooth phantom 口，Qt `setupapi` 按 **`DIGCF_PRESENT`** 正确跳过；
  而 .NET 的 `SERIALCOMM` 注册表视图**包含**它们 ⇒ **PowerShell 看得到、产品看不到**
  ⇒ **产品行为正确**。一致的 "COM3 @ 9600" 真实产品状态在本机**无法构造**；
  唯一路径是被 Human 拒绝的 fixture 或修改产品/测试代码（**均被禁止**）。
- **处置（按 fallback 规则）**：`05-SERIAL-RECAPTURE = BLOCKED`；
  `RECOMMENDATION = DROP 05 FROM README`；**FINAL SCREENSHOT SET = 4 张**；
  `docs/assets/screenshots/05-serial.png` 已从仓库移除
  （**被拒绝的 COM_HARNESS 版本保留于 Git 历史 `6982c78`，未丢失**）；
  UI FREEZE 继续 ACTIVE；PRODUCT CODE / QML = UNCHANGED；
  `PUSH = NONE`；`TAG`/`Release` = UNCHANGED；`REBUILD/REPACKAGE = NONE`。
- **Interview knowledge points**：**"产品诚实枚举"可能让某个视觉状态在本机不可构造**——
  此时正确做法是**记录 BLOCKED 并降级展示集**，而不是用 fixture 冒充真实状态；
  **枚举口径差异（Qt `DIGCF_PRESENT` vs .NET 注册表视图）会让人误判"产品看不到设备"**；
  **自动化 fixture 命名出现在交付截图里是真实的内容泄漏风险**；
  **"被拒绝的资产保留在 Git 历史"比删除更有价值**（可复查拒绝理由）。`[V-295]`

---

## 第三部分 — RCA 分档与"最强五个"

### 分类统计（本轮实测）`[V-296]`

| 类别 | 条目 | 说明 |
| --- | --- | --- |
| 协议 / 语义正确性 | RCA-A、RCA-B、ISSUE-016（缺陷 A 间接相关） | 位序、位运算匹配、作用域 |
| 帧 / 事务处理 | RCA-C、ISSUE-016（缺陷 B） | 终态缺失、请求行溢出 |
| 异步回调 / 生命周期 | ISSUE-001、ISSUE-005、ISSUE-020 | 悬垂指针、双 timeout 竞态、ambient 输入 |
| Qt 运行时 / 部署 / 平台 | ISSUE-002、ISSUE-010、ISSUE-011、ISSUE-015、ISSUE-018、RCA-I | DLL 抢占、QML 单例、qmldir 漂移、QProcess 管道、QPA 字体度量 |
| QML 布局 / 绑定 | ISSUE-004、ISSUE-012、ISSUE-013、ISSUE-014、ISSUE-016、ISSUE-017、RCA-J | 四轮 overflow、implicit 尺寸、空转断言、delegate 容错 |
| Agent / AI 边界 | ISSUE-006、ISSUE-007、ISSUE-008、ISSUE-009、RCA-D、RCA-E | 过度归因、预算、UNKNOWN、引用权威 |
| 测试基建 / 流程 | ISSUE-003、ISSUE-019、RCA-F、RCA-G、RCA-H、RCA-L | 能力实证、变异未还原、fixture 漂移、门禁自缺陷 |
| 视觉验收 | RCA-K、RCA-L、ISSUE-012 | 人工视觉闸门 |

### **五个最强 RCA**（技术深度 × 证据完整度 × 可讲性）`[V-297]`

| # | RCA | 为什么最强 |
| --- | --- | --- |
| **1** | **ISSUE-002** 部署哈希溯源（ABI / runtime collision） | 完整推导链：报错符号 demangle → 推断 GCC 版本差 → `where` 顺序 → `objdump -p` 符号对照 → 临时 PATH 单变量验证 → 哈希 provenance 修复；**并升格为全项目的 minimal-PATH 纪律**（后来所有打包/部署门的底座）。"exe 能链接通过 ≠ 能启动"是面官爱听的工业真实问题。 |
| **2** | **ISSUE-004** QML 布局真相（**四轮**取证） | 唯一一条走过**四轮失败**的 RCA，每轮推翻上一轮的假设；确立了 **runtime geometry 取证方法论**；最终把"滚动 bug"重新定义为 **workspace 结构缺陷**（SplitView）；并诚实记录了取证通道本身不可用（WIN32 无 stdout、offscreen 非可视）。 |
| **3** | **ISSUE-005** 双 timeout owner 竞态 + `errorString` 泄漏 | 一个很小的用户可见症状，背后是**两层独立缺陷**：错误分类正确但呈现泄漏（`errorString` ≠ 业务文案），以及 native timeout 与 app timer **同时存在即竞态**；解法引入了 `AiAbortReason`（**abort 前记录归属**）这一可复用的模式；并诚实保留"精确 elapsedMs 未实测"。 |
| **4** | **ISSUE-020** ambient 凭据泄漏 → 真实云端推理 | 罕见的"**确定性 harness 产生了真实副作用**"事故：单变量对照（4 个 Run）把唯一决定变量锁定为 ambient 凭据；**15.87s − 1.87s ≈ 14s ≈ HTTPS 往返**是副作用发生过的物理证据；修复方式（harness 入口清凭据 + 入口 guard + stderr 契约）具有通用性；并附**向 Human 的治理披露**与"授权的 Live smoke 不得与此混同"的澄清。 |
| **5** | **RCA-E / ISSUE-016 组合**（LLM 不能做权威数据 + QML 无限定名不是词法作用域） | 一个是**架构性教训**（把不可靠的 offset 计算移出模型、把选择留给模型、未知 id fail-closed），一个是**反直觉的运行时语义**（QML 无限定名只含「自身 + 组件 root + id」），且后者完整证明了"**门禁只查 exit code + WIN32 不写 stderr ⇒ 门禁什么都没断言**"这一检测缺口。两条都能直接支撑"我理解我用的东西"的追问。 |

**次强梯队（同样有完整证据）**：RCA-B（0x88 自我匹配，Review 前自审抓出）、
ISSUE-019（负向对照变异被提交，`// NC-B1` 标记是可 grep 的红旗）、
ISSUE-018（offscreen vs 真实 QPA 字体度量差 4px 累积越界）、
ISSUE-013（`-Wshadow` 未开导致两条断言恒真，变异探针证明）、
ISSUE-007（预算按维度分治 + 拒绝编造 tool sequence）、RCA-L（产品诚实枚举导致状态不可构造）。`[V-298]`

### **本项目 RCA 最独特的三个"态度"（面试差异化点）**`[V-299]`

1. **UNKNOWN 就是 UNKNOWN**：ISSUE-008 / ISSUE-009 的历史 exact root cause 至今未证明，
   处置是**硬化契约 + MONITORING / NON-BLOCKING**，并明确写下
   "**不得写成事实**"的 hypothesis 清单与"**不主动重复请求寻找 failure**"的 future policy。
   这比编一个漂亮的根因更难，也更有说服力。
2. **门禁自己也会坏**：ISSUE-013（空转守卫）、RCA-G（`property("open")` 拼写错误 + 计数器绝对断言）、
   ISSUE-016（门禁只看 exit code 且诊断不可达）、ISSUE-018（门禁平台 ≠ 验收平台）——
   四条都是"验证工具本身有缺陷"，且每条都做了**负向对照/变异探针**来证明修复有效。
3. **环境缺陷不在产品侧"修好"**：ISSUE-015 与 RCA-L 都拒绝了"让流程看起来成功"的捷径
   （手工拼装部署树 / 用 fixture 冒充真实状态 / 删除测试放宽 `REQUIRED_FILES`），
   选择如实挂起并给出人工解除路径。