# 08 — TECHNICAL DEEP DIVE（5 分钟技术展开）

> **用途**：面试第二层——当对方说"再展开讲讲"时用的**技术骨架 + 详细版本 + 证据注解**。
> **与 `07` 的关系**：`07` 是问题→产品→边界的口述；**本文件不是 `07` 的加长版**，
> 而是沿**真实架构与工程流**往下走一层：数据路径 → 确定性边界 → 事务模型 → Qt 边界 →
> 诊断权限边界 → AI/Agent 边界 → 验证体系 → 三个真实 RCA。
> **事实来源**：只使用已验收的 Fact Pack（`docs/interview/00`–`06`）；**不新增** `[V-]` 标记。
> **禁止事项**：见 `07` 开头同一清单（真机验证 / 真实串口写 / Modbus TCP / Agent 写权限 / OCR /
> 当前远端已复核 / 历史数字当现状）。

---

## A. 口述骨架（5 分钟，含时间分配）

```text
0:00–0:40  ① 数据路径总览：三种数据源 → 同一个核心 → 呈现 / 可选解释
0:40–1:30  ② 为什么协议事实必须确定性：Zero-Qt 核心 + 依赖方向
1:30–2:20  ③ 事务模型：状态轴与 Issue 轴正交（含它是怎么"长出来"的）
2:20–3:00  ④ Qt/QML 边界：核心不变，UI 是可替换的消费者
3:00–3:40  ⑤ 两条权限边界：诊断不宣称物理根因；AI/Agent 不拥有协议事实
3:40–4:20  ⑥ 验证体系：为什么"自动化全绿"不等于"断言覆盖"
4:20–5:00  ⑦ 一个真实 RCA（按面试官兴趣二选一，见 §C）
```

**讲法提示**：每一段都先给**工程约束**，再给**实现**——面试官更容易记住"你为什么不得不这样设计"。

---

## B. 详细版本

### ① 数据路径总览（三数据源 → 一个核心 → 多消费者）

```text
Simulator（确定性虚拟从站 + 故障注入四模式）
Replay（.mlog 文本日志，整批重析）
Serial（真实 USB-RS485 串口，一次一笔事务）
        ↓
  Deterministic Modbus Core（Zero Qt 静态库 modbuslens_core）
    · 协议：CRC-16 / RTU 帧模型 / wire 编解码 / FC03·FC06·FC16 语义
    · 事务：请求-响应配对 → 归一化状态 + 正交 Issue
    · 统计：三计数 / 五分类 / optional 成功率与延迟
    · 诊断：规则引擎 → 结构化 finding + 建议检查项
        ↓
  App / Adapter 层（Qt 类型只允许在这一层）
    Controller / QAbstractListModel / SerialPortAdapter / provider 客户端
        ↓
  QML UI（6 工作区）
  可选上层：AI Explanation（one-shot 解释）· Read-only Agent（3 个只读工具）
```

- **关键点不是"有三种模式"**，而是三种模式**只负责产生字节**，判断与统计全部在同一个核心。`[C-02]` `[V-198]`
- **口径一致性有可执行证据**：`demo_v1.mlog` 的 Golden Replay 与 Simulator Demo 批次，
  统计快照**严格相等**（observed/completed 4/4、success/exception/crc/timeout 1/1/1/1、
  成功率 0.25、成功平均延迟 25.0 ms）。这不是"应该一致"，是断言。`[V-027]` `[V-198]`
- **可选层不可用时核心仍完整**：无 provider 配置时 Simulator / Replay / Serial、统计、
  基线诊断、手册导入全部照常可用。`[V-303]` `[V-365]`

### ② 为什么协议事实必须是确定性的（Zero-Qt 核心）

- **工程约束**：诊断结论的可信度要求"事实只有一个来源"。如果 UI 或模型也能参与判定，
  同一批数据可能出现两个结论，而这个工具的全部价值就是"口径一致、可复查"。
- **实现**：`modbuslens_core` 是**无 `target_link_libraries`** 的静态库——
  也就是说它在构建图里**不链接任何 Qt**；`src/core/` 对 Qt 头文件的 include 命中数为 **0**。
  配套纪律是 **穷举 `switch` 不带 `default`**：新增枚举值必须触发编译期错误，
  防止静默扩展。`[V-158]` `[V-196]` `[V-136]`
- **备选与代价**：把逻辑写进 `QObject` 会省掉一层适配（`optional` 在边界要显式拆成 `hasX + value`），
  但代价是核心无法在无显示器环境完整测试、也无法保证三模式复用同一实现。
- **诚实边界**：**"共享核心"共享的是核心函数，不是统一接口类型**——
  架构文档里作为"复用关键"出现的 `IFrameSource` 抽象**在代码中 0 命中**，
  `src/io/`、`src/app/` 两个规划目录也从未创建。对外正确说法是
  "复用同一批分析函数 + 口径断言"，而不是"有统一数据源接口"。`[C-02]`

### ③ 事务模型：状态轴与 Issue 轴正交（**这段最能体现演进**）

- **工程约束**：一笔事务的"归一化结局"必须能进统计、进界面、进 Agent 白名单；
  但判定当时看到的**具体违规事实**（地址不匹配、数量不符、回显不一致）如果折进结局，
  就永远丢了。
- **最终形态**（源码实测）：
  ```text
  TransactionStatus            = 7 个枚举成员
  TransactionIssueCode         = 10 个枚举成员（含 1 个防御哨兵 ⇒ 业务值 9）
  TransactionRequestIssueCode  =  4 个枚举成员
  ⇒ 枚举口径 14 ／ 业务口径 13
  ```
  `[V-215]` `[V-317]`
- **为什么要拆两轴而不是扩状态**：状态轴被统计、界面、Agent 白名单共同消费；
  从 6 个状态扩到 20 个会连锁破坏所有穷举分支与既有数据集。Issue 是**旁挂的一维**，
  老数据行为逐位不变。`[V-321]`
- **演化历史（不要讲成 Day-1 设计）**：T007 时定案是**六状态、不新增**；
  随后的覆盖审计把"ProtocolError 细节丢失"列为 P0（五个真实分支全部归一为同一状态）；
  T014 加**响应侧** Issue 并建立生产不变量 `ProtocolError ⇒ issue.has_value()`
  （只在生产输出中成立，防御分支用哨兵）；T015 再加**请求侧** Issue，
  于是才有 `Success + InvalidRequestByteCount + InvalidRequestLength` 这种复合表达。`[V-211]`–`[V-214]`
- **同一段历史里还有一次"同日反转"**：广播在旧六状态里**没有诚实落点**——
  判成功是假称写入成功，判超时是把合法沉默当故障。ADR-003 新增第七状态 `ExpectedNoResponse`，
  统计上 `successRate = success / (completed − expectedNoResponse)`，分母为 0 返回 `nullopt`
  而不是 0%。ADR 明确写了这次扩展**显式覆盖**前一轮"六状态不动"的红线，而非静默突破。`[V-216]`–`[V-220]`
  > 注：`docs/09` 里"恰为六状态、无第七个"是**同一日更早时点**的记录（两条当时都为真），
  > 引用时必须说明是历史快照。`[C-04]`

### ④ Qt / QML 边界：核心不变，UI 是可替换的消费者

- **依赖方向是强制的**：QML/Qt Quick → Controller/Model（Qt 类型只允许在此）→ Zero-Qt 核心；
  **禁止反向依赖**。`[V-020]` `[V-195]`
- **QML 上踩到的三条"静默失败"语义**（都能直接支撑"我理解我用的东西"）：
  1. **无限定名不是词法作用域**——解析链只含「自身属性 + 组件 **root** 对象属性 + id」，
     **不含中间祖先**。把属性放在中间对象上、让嵌套子项按名读，会得到运行时 `ReferenceError`，
     而**退出码仍是 0**。`[V-140]` `[V-279]`
  2. **`Item` 作为自定义组件根若无 implicit 尺寸**（默认 0），父布局会**静默少算整块内容**，
     表现为子项"溢出渲染"而不是布局报错——定位时必须把 scene 矩形逐项打印出来才看得见。`[V-141]` `[V-281]`
  3. **anchors 消费父尺寸但不产生父的 implicit 尺寸**——容器组件的 implicit 必须由
     content-layout 的 implicit 派生，否则整块会塌缩。`[V-060]` `[V-275]`
- **样式层的真实教训**：Qt Quick Controls 的原生 style 会**静默忽略** QML 自定义，
  该教训直接导致一次视觉返工与最终改用 `Fusion`。`[V-138]` `[V-294]`
- **持久化刻意留在 Adapter 层**：设备档案/手册库用 Qt JSON（`QFile` / `QJsonDocument` /
  `QSaveFile`）写，**核心的领域模型保持零 Qt**（`DeviceProfile.h` 只 include
  `<cstdint> <string> <string_view> <vector>`）。`[V-154]` `[V-252]`

### ⑤ 两条权限边界（本节是"工业软件里 AI 怎么用"的核心答法）

**边界 A：诊断不宣称物理根因，也不消费设备档案。**
- 规则引擎输出**纯 enum** 的 finding + 12 种建议检查项；**不存在 health score**，
  "可能原因"只出现在 possible checks 里，绝不包装成事实。`[V-322]`
- 异常码 0x02 只给出"非法数据地址"这一**协议事实**并建议核对地址映射，
  **不指认具体寄存器语义**。`[V-168]`
- **C-03 修正后的当前能力口径（Human 已裁定，权威）**：M11 多类型解码（7 种类型 + 字节序/字序，
  raw 与解析结果分列）与 M12 设备档案 register map + **用户显式填写**的 scale/offset/unit，
  **只作用于 Read Result 的语义呈现**；本阶段新增证据确认**诊断引擎不消费设备档案**
  （`grep -rn "DeviceProfile\|RegisterEntry\|profile" src/core/diagnosis/` → **0 命中**）。
  边界内仍有：无设备模板库、无自动工程单位推断、不做量程/报警阈值判定。`[C-03]` `[V-558]`

**边界 B：AI 是解释器，Agent 只读、有界。**
- **AI is interpreter, not detector**：CRC 正确性、`TransactionStatus`、Timeout、异常码、统计与延迟
  的权威只在核心；`DiagnosisPromptBuilder` 下发的是**已算好的结构化事实**（一次最多 20 条事务明细），
  模型没有机会重新判 CRC 或超时。`[V-031]` `[V-346]`
- **Agent 的写能力在类型层面不存在**：白名单 dispatcher、恰好 3 个只读工具、
  `grep` 写工具为 **0 命中**。运行时有硬预算（**rounds=3 / total calls=6**）与
  **双层 stale guard**（`capturedBatchRevision == currentBatchRevision` **AND**
  `runGeneration == currentAgentGeneration`），且 **validate-then-execute、零部分执行**。`[V-209]` `[V-210]` `[V-347]`
- **产品写路径与 Agent 严格隔离**：设备写只能由用户在界面显式确认后经串口派发
  （Confirmation vs Dispatch 分离；**PREVIEW == WIRE**：预览字节必须来自 dispatch 用的
  同一个 production encoder，逐字节锁定）。`[V-229]` `[V-330]`
- **同一条哲学延伸到文档智能**：手册问答的引用改为 **app-resolved citationIds**——
  模型只能在 app 给的上下文块 id 里"选择"，offset/hash 由 app 回填，**未知 id fail-closed**。
  一句话总结：**LLM 输出只能做选择，不能做权威数据**。`[V-096]` `[V-288]`
- **边界纪律的真实代价**：`ISSUE-008` / `ISSUE-009` 的历史 exact root cause **至今 UNKNOWN**，
  仓库的处置是**硬化契约 + `MONITORING / NON-BLOCKING`**，并写清"不得写成事实"的 hypothesis 清单。
  ——**拒绝编造根因**本身是可以讲的工程能力。`[V-271]` `[V-272]` `[V-299]`

### ⑥ 验证体系：为什么"自动化全绿"不等于"断言覆盖"

- **现状**：CTest **70** 个目标（`grep -cE "^\s*add_test\(" CMakeLists.txt` 实测 70）；
  Release 配置 **70/70 PASS, exit 0**（2026-10-07 验证快照）。
  引用历史文档的"23/24 个目标"时必须注明那是**当时为真的快照**。`[C-05]` `[V-354]`
- **确定性来自"把外部世界搬进本地"**：AI 与 Agent 的测试用**本地 fake Chat Completions server**
  （零真实网络）；串口测试用 **fake serial transport**。`[V-030]` `[V-355]`
- **负向对照纪律**：真实 mutate → 观察目标测试 **RED** → **精确逆向 patch** 还原 → 基线复绿；
  **没有真实 RED 不算 evidence**。`[V-356]`
- **两条门禁铁律**（都是踩出来的，见 `03` D12）：
  1. **要断言某类缺陷，必须先保证该缺陷可观测**——WIN32 子系统 exe 的诊断默认写到
     OutputDebugString，门禁只看 exit code 时**其实什么都没断言**；`[V-279]` `[V-232]`
  2. **门禁的平台必须与被验收的平台一致**——offscreen 与真实 windows QPA 的行高差 4px，
     逐行累计足以把一个"刚好放得下"的页面推过窗口边缘。`[V-281]` `[V-232]`
- **负向对照也会失手**：有一次负向对照的**变异没有还原**就进了行为提交
  （源码里留下 `// NC-B1: ...` 标记注释），最后由门禁抓出。这说明"负向对照必须有
  还原后复绿的闭环证据"。`[V-093]` `[V-282]`

### ⑦ 三个真实 RCA（选讲，忠实于 `04_BUG_RCA_CATALOG.md`）

> 下面三条是 `04` 的"最强五个"中的前三条。**症状 → 定位 → 根因 → 修复 → 验证**逐项照档案写，
> 未加工成戏剧化故事。

#### RCA-1 · ISSUE-002：部署机双击启动失败 —— ABI / runtime collision `[V-265]`

- **症状**：用户从 Explorer 双击启动弹出
  `无法定位程序输入点 _ZNSt3pmr20get_default_resourceEv 于动态链接库 …Qt6Gui.dll`；
  同一 exe 在开发终端下正常、**自动化测试全绿**。
- **定位**：报错**指向 Qt DLL**，很容易误判"Qt 坏了"。实际用了四步收敛——
  ① 报错符号 demangle → 推断是 GCC 版本差（`std::pmr` 是 GCC 9+ 才有）；
  ② `where` 五连查证明 Qt 的 runtime 在 PATH 里排**第三**，前面是两套异构 MinGW；
  ③ `objdump -p | grep -c` 精确对照导出符号：MinGW **8.1 版该符号 = 0（缺失）**，13.1 版 = 1；
  ④ 临时前置正确 runtime 后进程稳定存活（单变量验证）。
- **根因**：Windows 按"应用目录 → System32 → … → PATH"搜索 DLL，本机 PATH 里旧 runtime 先被解析；
  旧 libstdc++ 缺 `std::pmr` 符号 → 符号解析失败。**报错指向 Qt，但被换掉的是 Qt 的依赖。**
- **修复**：`scripts/deploy_windows.bat` 生成独立部署目录，**强制从编译器 bin 覆盖 MinGW 三件套**，
  并做 **provenance SHA-256 复核**（与编译器 bin byte-identical）；启动门用 **minimal-PATH**
  （PATH 只含应用目录与系统目录）。**业务代码零改动。**
- **验证**：Runtime provenance = VERIFIED；minimal-PATH 下 `--qml-smoke-test` **exit=0**；
  部署脚本可重复生成 PASS；**用户 Explorer 双击确认 PASS**。
- **工程价值**：这条纪律后来升格为**全项目所有打包/部署门的底座**。
  一句话：**"exe 能链接通过 ≠ 能启动"**，部署正确性是**内容身份**问题，不是"文件在不在"。

#### RCA-2 · ISSUE-004：Diagnosis 面板纵向 overflow —— 四轮定位 `[V-267]`

- **症状**：点击运行基线诊断后，诊断正文纵向展开，把 AI 控件与事务列表推到可视区以下，
  页面无纵向滚动能力 ⇒ 这些控件**不可达**。定性为**功能性 UI bug，不是 cosmetic polish**。
- **定位（四轮，每轮推翻上一轮假设）**：
  ① 以为"缺滚动容器 + 无高度约束" → 加上下界 + ScrollView → 用户复验 **FAIL**；
  ② 发现 **Layout bounds ≠ Rendering clip**：`ScrollView` 未显式给 `contentWidth/contentHeight`，
     底层 Flickable 的 contentItem 尺寸未定义；`ListView` 默认 `clip: false`，delegate 会画到 viewport 外
     → 再 FAIL；
  ③ 长文本区从 `ScrollView` 换成**显式 Flickable**（关键是把 `Layout.minimumHeight` 设为 0，
     让 content 的 implicitHeight 不再成为 viewport 最小值）→ **仍 FAIL**；
  ④ 靠 **runtime geometry 取证**（`flickH=133 / contentH=214 / contentCY=81 …`）证明
     **滚动机制本身已正确**，真正原因是 **`GroupBox.contentItem` 不是 Layout parent ⇒
     `Layout.*` 附加属性被静默忽略 ⇒ viewport≈0**；修掉之后又暴露**新 blocker**：
     给 Diagnosis 设的固定高度在纵向串联中**持续侵占**事务列表空间——
     这是 **workspace 结构问题**，不是继续调 scroll 参数能解决的。
- **根因（三层叠加）**：局部 overflow（implicitHeight 无界 + 无全局滚动）→
  动态正文 viewport 失效（`Layout.*` 在非 Layout 父下静默失效）→ 本质是
  **workspace vertical allocation defect**。
- **修复**：改为 **Horizontal SplitView**（左 Diagnostics / 右 Transactions，各自独立滚动，
  根布局不滚）；最小窗口定为 **1000×700**（由"左 300 + handle + 右 520 保底"推出，不是拍脑袋）。
- **验证**：用户人工确认 **9 项**证据；clean 构建 0 警告；ctest 20/20；qml smoke / deploy /
  minimal-PATH 全部 `exit=0`。
- **工程价值**：**QML 的静默失败需要测量证据链**——没有 runtime geometry 读数，
  根本无法区分"滚动没配好"与"viewport 是 0"；而且**取证通道本身要能用**
  （WIN32 无 stdout、offscreen 非可视渲染，最终靠用户真实截图 + 静态对象树）。

#### RCA-3 · ISSUE-005：Ask AI 显示"操作被取消" —— 双 timeout owner 竞态 + `errorString` 泄漏 `[V-268]`

- **症状**：真实 Live 点"Ask AI"后显示 `Latest AI request failed: 操作被取消`；
  用户**无法区分** Timeout / 用户取消 / 意外取消。
- **定位**：① 该文案就是 `QNetworkReply::OperationCanceledError` 的 **Qt 本地化 errorString**——
  分类映射其实是对的（`OperationCanceled → Timeout`），但 **UI 呈现的是错误字符串本体**，
  泄漏了 transport 层语义；同时证明请求确实被**本地 abort**。
  ② 进一步发现实现里**同时存在两个 timeout owner**：
  `QNetworkRequest::setTransferTimeout(30s)`（Qt native，内部 abort）**与** `QTimer(30s)`——
  两者都会 abort，而 `finished` 回调**无从区分"谁取消的"**，且 `QNetworkReply::error()`
  **本身不含 abort 主体信息**。
- **根因（两层独立缺陷）**：**错误分类 ≠ 错误呈现**；**多个 timeout 机制并存即竞态**。
- **修复**：引入 `enum class AiAbortReason`，**每次主动 abort 之前先记录 reason**；
  移除 native transfer timeout，**QTimer 成为唯一 owner**；生产 timeout 30s → **90s**；
  **任何路径不再把 Qt errorString 当用户文案**。**未改** model / messages / prompt / parser。
- **验证**：clean 构建 0 警告；ctest 20/20（含"取消静默"与"超时给业务文案"的新断言）；
  **Live 27B 回归 Smoke PASS**。
- **证据边界（诚实保留）**：旧失败请求的**精确 elapsedMs 未实测**（WIN32 GUI 无输出通道），
  档案**没有**改写成"已实测恰好 30.000s"。`[V-268]`
- **工程价值**：abort 主体信息在 Qt API 里不存在 ⇒ **必须以自有状态补位**；
  且"行为约束应放在离生成点最近的确定性位置"（本例是 timeout owner + abort reason，
  同族的 ISSUE-006 是 prompt 文本），而不是在输出端修补。`[V-269]`

---

## C. Evidence Notes

1. **本文件全部事实来自已验收 Fact Pack**（`00`–`06`），未从记忆重建、未美化历史、
   未补造实现细节。凡 Fact Pack 未证实的点，本文件不写。
2. **未新增 `[V-]` 标记**：保持已验收计数 **563 / 5 / 11** 有效；本文件的注解一律**引用既有标记**。
3. **CONFLICT 的处理**：`[C-02]`（`IFrameSource` 不存在）与 `[C-04]`（六 vs 七状态）
   在正文里**就地说明了正确口径**；`[C-03]` 采用 Human 已裁定的**当前**能力口径（见 §B⑤ 边界 A）；
   `[C-05]` 的数字一律用**现状真值 70 / 3 / 30**，历史值仅作历史引用。
4. **UNVERIFIED 的处理**：`[U-10]`（当前 GitHub 远端状态）**未作为正向陈述**；
   `[U-11]`（`docs/INTERVIEW_NOTES` F8 同族表述）**未被使用**；
   `[U-03]`–`[U-09]`（部分档案为定点读取）意味着本文件不声称穷尽项目内部细节。
5. **四个已知的"讲法陷阱"**（源自 `05` §7 与 `00` §5）：
   ① 把历史快照的"23/24 个测试目标"当现状；② 把 `[U-10]` 说成"发布状态我已复核"；
   ③ 把"适配器热拔实机验收"说成"真机跑通了业务"；④ 把"理解历史写事务"说成"工具会写设备"。
6. **可深挖的下一层**：`04_BUG_RCA_CATALOG.md` 还有 29 条 RCA 未在本文件展开
   （含次强梯队：`RCA-B` 0x88 自我匹配、`ISSUE-019` 变异未还原、`ISSUE-018` QPA 字体度量、
   `ISSUE-013` 空转断言、`ISSUE-007` 预算分治、`RCA-L` 产品诚实枚举）；`03` 有 20 个架构决策的全量记录。