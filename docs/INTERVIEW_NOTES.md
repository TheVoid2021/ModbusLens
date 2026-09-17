# INTERVIEW_NOTES — 面试问答素材

> 规则：**每个任务完成后**，把该任务可被考问的问题与答题要点追加到 §3（编号 `T00x-题号`）。
> 素材只沉淀"已被验证事实"的内容：答案必须能在代码/文档/测试中找到依据，不写没做过的事。

## 1. 项目三十秒叙事（电梯法则）

> "我做了一个 C++20 + Qt6 的工业通信诊断平台 ModbusLens。它支持三种数据源：无硬件模拟器、历史日志回放、真实串口监听，三种模式共用同一套协议解析和诊断核心，所以结论口径一致。整个开发过程全程留档在 Git 仓库里——每个任务为什么做、怎么设计、踩了什么坑、怎么测试，全部可追溯。核心分析不依赖大模型，LLM 只是只读的增强插件。"

## 2. 叙事主线（深挖时按这条线讲）

```text
痛点（工业 Modbus 排障难 / 学生没设备练） 
  → 定位（第三方旁路诊断 + 三模式解决"无设备/回放复盘"）
  → 架构（IFrameSource 抽象 → 纯函数协议核心 → 确定性分析核心 → 薄 UI）
  → 工程化（可追溯任务档案 / 可移交 AI 平台 / CTest 全绿 / 演示即验收）
  → 亮点（口径一致性护栏、异常注入演示、只读 Agent 边界）
```

## 3. 已可问答（按任务累积）

### T001（本任务已实现内容）

**T001-Q1：为什么用 C++20，而不是 Python 快速搞定？**
要点：① 目标岗位是 C++ 方向，项目要与应聘方向一致；② 工业采集对性能和内存控制有真实要求（无 GC、可控缓冲）；③ C++20 特性在本项目中落到具体位置（`std::optional`/`std::span` 将用于解码结果与字节视图、concepts 用于 IFrameSource 约束、`std::jthread` 用于采集线程）——特质具体化比说"性能好"更有说服力；④ 自己实现 CRC/帧解析的学习价值（不引库，见 02 架构 §5）。

**T001-Q2：为什么用 CMake Presets + CTest，而不是 IDE 点按钮？**
要点：① 玩法可复现：configure/build/test 一样不落全在文件里；② 可移交性：任何 AI 平台/开发者 clone 后按 README 跑通；③ 机器差异隔离：`CMakePresets.json`（提交，通用）vs `CMakeUserPresets.json`（gitignore，本机工具链路径）——答案要能讲清"为什么分两份"；④ CTest 统一驱动，未来 CI 只跑一条命令。

**T001-Q3：为什么冒烟测试要加 `QT_QPA_PLATFORM=offscreen`？**
要点：CI/无显示器机器上 Qt 程序需要离屏插件渲染，否则测试起不来；这也证明"测试应该能脱离 GUI 运行"的取舍——核心逻辑将来全部不依赖界面，UI 只做薄壳。

**T001-Q4：为什么这项目文档这么"重"（任务档案、ADR、Issue、devlog）？**
要点：① 面试即交付：文档是项目价值的载体，面试官看不到开发过程，文档能证明；② AI 协作刚需：上下文会丢，仓库不会丢（"状态只存仓库 Markdown"）；③ 记录迫使思考：写清"为什么"才能复盘；④ 引出规范插入点：AGENTS.md 是给接手的（人或 AI）看的规约。可讲一个真实案例：T001 里 moc 许可证提示如何被定位与规避（见 T001 档案）。

**T001-Q5：Qt 的签名槽/AUTOMOC/元对象系统一问？**
要点（基础题预警）：moc 预处理器扫描 Q_OBJECT 宏生成元对象代码，AUTOMOC 由 CMake 集成（qt_standard_project_setup）；信号槽第五参连接类型（Auto/Direct/Queued/BlockingQueued）各适合什么线程场景；Qt 事件循环模型与 `QApplication::exec`。这些是 Qt 岗位常规问题，代码简但需能讲。

**T001-Q6：PATH 中存在多个编译器/多个 Qt 时怎么保障构建正确性？（工程素养题）**
要点：① 问题本质：ABI 不匹配（g++ 8.1 vs Qt 6 不兼容；Anaconda Qt5 qmake 串扰 find_package 结果）；② 解法：不依赖 PATH 运气，用 preset 显式注入 `CMAKE_CXX_COMPILER`、`CMAKE_PREFIX_PATH`、工具链 PATH；③ 验证手段：configure 输出中核对"Detecting CXX compiler"与 Qt 版本；`qtpaths --qt-version` 确认 kit。此题展示排查与工程化能力，是 T001 真实经历。

### 待实现内容的预告（任务完成后才可答，此处仅列题类）

- ✅ 已可答（T002，见任务档案两个 Knowledge 清单）：CRC-16/MODBUS 原理与实现、规范向量对拍、数值 vs 线上字节序、`std::span` 接口取舍、独立 core target 的意义、TDD RED 证据纪律、KAT vs invariant 证据分级
- ✅ 已可答（T003，见任务档案）：RTU 帧结构（Address+Function+Data+CRC，≤256B）、内存模型为何不存 CRC（stale-data）、value 语义与 defaulted `==`、`std::vector<uint8_t>` vs QByteArray、0x03/0x83 关系；fuzz-lite 已按用户指示移出 T003（待 codec 后评估）；wire 编解码/CRC 校验归 T004
- ✅ 已可答（T004 Part A，见任务档案）：wire 编解码实现、`variant<Frame, RtuDecodeError>` 错误模型取舍、CRC 低字节在前序列化与 `84 0A→0x0A84` 还原、linker-error 式 RED 留痕、round-trip vs KAT；Part B（0x03 字段语义）待实现
- ✅ 已可答（T004 Part B，见任务档案）：0x03 请求/响应/异常 data 字段解释、quantity 1~125 由帧上限反推、byteCount 语义与 2~250 偶数规则（含 byteCount=0 修正案例：单帧规则 vs 跨帧一致性）、40001 与 0-based 协议地址的边界；Function 03 encode 未实现（待 Simulator 需要时再评估）
- ✅ 已可答（T005，见任务档案 + ISSUE-001）：模拟从站端点设计（Frame 进 Frame 出、const 纯应答）、连续寄存器文件 vs 稀疏映射、越界判定防 uint16 回绕、异常响应即协议输出、为什么推迟 IFrameSource（rule of three）；**ISSUE-001**：variant 临时生命周期 → 测试悬垂指针（"测试全过 ≠ 无 UB"）→ optional 拷贝语义修复
- ✅ 已可答（T006，见任务档案）：确定性故障注入设计（四模式、单 config 单模式）、Timeout=Session 层判断（DropResponse 只是交付层事实）、CRC fault 只作用 wire 层（payload 不动 + 固定 XOR 策略）、ArtificialDelay=元数据不真等、Exception/CRC Error/Drop 三层区分；**T006 补充**：确定性优先于拟真的取舍论证
- ✅ 已可答（T007 Part A，见任务档案）：事务=请求+观察（帧合法≠回答正确）、六状态语义（含 Timeout=NoResponse+阈值）、RTU 无事务 ID 的串行配对模型、数量一致性首次跨帧校验、elapsed/exceptionCode 双不变量经 makeAnalysis 漏斗保证、穷举 switch 无 default 的防静默扩展
- ✅ 已可答（T007 Part B，见任务档案）：statistics snapshot 的四条不变量与测试锁定方式（A：observed=pending+completed；B：completed=五分类之和；C：successRate 有值 ⟺ completed>0；D：avg latency 有值 ⟺ success>0）、optional 为空在 UI 上呈现 "—" 的语义（"没有统计"≠"0"）、completed 按分类之和构造的防漏记风险
- ✅ 已可答（T008 Part B + T008.1，见任务档案 + ISSUE-002）：确定性 demo 与真实 core 链路的结合（演示即集成测试）、batch publish 原子性、Runtime Provenance（编译器三件套 SHA256）与 minimal-PATH smoke、Windows PATH 冲突（旧 libstdc++ 无 pmr 符号）的系统性排查
- ✅ 已可答（T009 Part A，见任务档案）：Simulator vs Replay 的本质区别（现场计算 vs 历史重新分析）、`.mlog` v1 版本化文本格式为何不选 JSON（无标准库 parser、Core 边界）、解析分层 Text Syntax→Wire Codec→Transaction Analysis（parser 不懂 CRC）、坏 request（ReplayExecutionError 三错误码）vs 坏 response（CrcError/ProtocolError 诊断事实）的非对称处理、NO_RESPONSE 用 `optional` 而非空 vector、CRLF 兼容一行搞定、lineNumber（1-based physical）vs transactionIndex（0-based vector）、from_chars 完整消费 vs atoi、Golden Replay 与 Simulator Demo 统计同口径（D1 承诺的可验证形式）
- ✅ 已可答（T009 Part B，见任务档案）：UI 与数据来源解耦（Simulator/Replay 共用一套 Dashboard+Model，两批统计严格同口径）、原子发布三分法（失败只动 error state 保全旧 batch+mode+source；成功一次性发布；Run Demo 显式切换来源）、`std::string_view` 跨 QByteArray 生命周期的边界纪律（Core 返回值必须自持有）、QFile/QUrl 只出现在 App/Controller 层（ADR001）、错误文案适配器（Core 保持 enum+line/index，展示层 +1 与人类可读）、Qt Quick FileDialog 动态 QML plugin（无需 CMake 组件的实证过程）、canonical fixture 单一源头（git mv + SHA256 一致性）、replace semantics 与多来源切换
- ✅ 已可答（T010 Part A，见任务档案 + ISSUE-003）：`readyRead` ≠ 一帧（OS 任意切块 → 累积 buffer + candidate 判终）、transaction-aware framing vs t3.5 gap scanner 的取舍（主动 Master + 单一 outstanding 下长度可推导）、FC03 响应 5+2N 与 Exception 固定 5 bytes、`(fn & 0x80)` 判异常格式而不硬编码 0x83（A15 实证）、response byteCount 推导而非 request quantity（A16 实证）、partial-response timeout ≠ Timeout（真实 wire-truth：4B 恰为最小 RTU 帧 → CrcError）、Transport Error 与 TransactionStatus 分层、one-outstanding 与 Busy、Zero Qt session + Qt thin adapter 的边界（QElapsedTimer/QTimer 只在 adapter）、**QSerialPort errorOccurred 反馈风暴的实战排查**（open 失败端口发射 0/10 无限序列 → QueuedConnection + suppress 标志；gdb + 最小 repro 二分定位）、Qt 组件缺失的多 kit 错位根因（ISSUE-003：MSVC vs MinGW 前缀）、无硬件如何测 Serial（byte span 注入）
- ✅ 已可答（T010 Part B，见任务档案）：transport/transaction 生命周期拆分（Port Open ≠ Transaction Pending；openPort/startTransaction/closePort 三 API 的演进理由）、Connect 成功/失败的原子 source 语义（与 Replay 失败加载同构）、Disconnect 与 Clear 的职责分离、Read Once 的 replace=1 语义与 0% 合法 rate、C++ 层 range validation 防 int→uint8_t narrowing、hardware-free mapping seam（publishSerialResult 验证 presentation 映射而非伪造业务链）、stale completion guard（异步迟到的防呆）、port discovery 只 enumerate 的安全纪律、QtSerialPort 动态链接图（app 链 SerialPort、core 零 Qt）、deploy DLL provenance（多 kit 环境的 SHA256 归因）、PE-4 有界断言（QSignalSpy 恰 1 次）
- 三模式为什么能共享核心（IFrameSource 抽象落地细节）（T005/T009/T010）
- 虚拟时钟与确定性模拟（T005/T006）
- 事务配对的启发式算法与广播/超时处理（T007）
- 线程模型：采集线程 ↔ 分析线程如何传递数据不丢帧（T005+ 首个数据源落地时）
- 虚拟串口对如何做集成测试、t3.5 帧切分实现（T010）
- ✅ 已可答（T011 Part A，见任务档案）："AI is interpreter, not detector"——确定性事实与概率性解释的分层（LLM 禁改 TransactionStatus/统计/自动行动）、Deterministic Rule Baseline 的生存价值（无网/无 key 可诊断）、DiagnosisContext 自洽快照与 summarizeTransactions 单一规则、NoData≠Healthy / Pending≠failure / 0% 是合法 rate 的语义细节、Exception 按 code 分组与 0x01~0x04 标准映射、为什么不建 health score（异质问题不可压缩成单数）、finding 顺序≠根因排序、诊断失效模型（batch 变则诊断清、失败切换保留）、回译（presentation formatter 只翻译 Core report）、结构化 active batch 与 UI 只读分离、"prompt injection 边界与 secrets 规则"的预告式设计
- ✅ 已可答（T011 Part B，见任务档案 + ISSUE-004）：ModelScope OpenAI-compatible 协议的客户端实现（raw Chat Completions parser、reasoning_content 忽略、12 值错误分类与 HTTP 映射、QT 中非 2xx 也置 reply->error() 的 mapping 顺序坑）、BYOK 凭据边界（生产 endpoint 禁 env override 的 token 防泄漏理由）、双维异步有效性（batchRevision×requestGeneration：'same batch?' AND 'latest request?'）、**QML 布局四轮迭代的真实教训**（Layout.* 在非 Layout 父下被静默忽略 → GroupBox content 填充失去意义 → Flickable 必须显式 content extent；局部 overflow 与 workspace 分配缺陷是两个层级的 bug；runtime geometry 取证方法论）、**Live LLM 证据**："AI can be wrong in prose without corrupting protocol facts"（真实 ModelScope 曾输出过强因果推断，而全部 deterministic facts 不变）+ non-blocking over-inference 观察（"rather than" 类措辞收紧属 prompt polish）、insufficient balance 失败记录保留与 recover evidence、AI 输出强制 Text.PlainText 的 XSS 式防御
- ✅ 已可答（UI Localization Pass，见 docs/06_UI_LANGUAGE_POLICY.md + devlog 2026-09-09）：展示层与内核的文案边界（Core 只出 enum/结构化事实，中文化全部落在 App/UI 适配层）、专业实体保留原则（CRC/RS485/FC03/8N1/0x02/COM/ModelScope/ms 不翻译、无冗余双语）、错误定位为何禁止依赖展示文案（`if (statusText == "超时")` 是反模式，判断永远走 enum）、`const char*` + QLatin1String 对 UTF-8 中文必然乱码而 QStringLiteral 安全（源字符集 → UTF-16 语义）、单语言场景不引入 Qt Linguist/.ts/.qm 的取舍、LLM 输出语言的治理方式（事实通道保持英文结构化、system prompt 只约束输出语言、Text.PlainText 渲染兜底）
- ✅ 已可答（ISSUE-006，见 docs/issues/ISSUE-006-ai-explanation-overattribution.md）：为什么"样本数量阈值"是错误设计而"证据范围"才是正确语义（count 不能论证代表性，observed=4 与 30 同受 guard）；LLM 过度归因治理定位在"离生成点最近的确定性文本"（system prompt 正例语义，与 ISSUE-005 的 timeout owner 哲学同构）；prompt contract 测试的诚实边界（自动化锁定"builder 必然输出什么"，不假装能证明"LLM 一定服从"——后者只能 Live Smoke 人工验收）；异常码语义（0x02=Illegal Data Address）属 presentation 层映射而非 Core 契约的依据；severity 是分级不是因果强度的澄清；Live 证据证明正例语义优于否定式禁令（模型主动复述 evidence_scope 且产出"可能是"句式）
- ✅ 已可答（T012 Learning，见 docs/tasks/T012-agent-tools.md + ADR002）：one-shot 解释 vs 按需工具的 Agent 分层（T011 保底 fallback）；read-only contract 的类型级强制（写能力连名字都不进 contract）；dispatcher vs registry 在"三工具两周"下的取舍；transaction_id 为什么是 batch-scoped 1-based 序号而非 row index；工具结果必须 typed fact → JSON 单向序列化；模型 arguments 是不可信输入（Qwen 官方点名 malformed 必须自解析）；loop 上限把模型死循环变成确定性终止错误；单 run 绑定 activeBatchRevision + generation 复用 T011 双层 guard；本地 Agent 的 prompt injection 三层防线（authority 置顶/schema 代码生成/白名单 dispatch）；无 key 时 Baseline+Ask AI 照常 = 增强不阻塞核心
- ✅ 已可答（T012 Part A 落地，见 docs/tasks/T012-agent-tools.md + ADR002）："non-Success is not equivalent to anomaly"（Pending 是未完成态而非失败态；anomaly = 显式 whitelist {Exception/CrcError/Timeout/ProtocolError}）；latest-20 必须按 anomaly 序列选取而非"最后 20 条事务再过滤"（尾部 Pending 不消耗名额，Review 实测教训）；snapshot isolation 的测试写法（dispatcher 只收 const&，同快照跨外部变更重复分发字节级一致）；异常码 unknown 时 exception_name 缺席不猜；read-only whitelist 的测试化 enforcement（11 个写类名逐一 UnknownTool）；理论表述修正：offline/zero-network ≠ Zero Qt（QtCore JSON 仅限 arguments/serialization boundary）
- ✅ 已可答（T012 Part B Gate 0 实证，见 docs/tasks/T012-agent-tools.md Gate 0 段）：ModelScope API-Inference + Qwen/Qwen3.5-27B 的 native tool calling round trip 已真实证明（tools→tool_calls→role=tool→final，finish_reason=tool_calls/stop 两种形态、message.content 为空的行为、tool_call_id 匹配要求、synthetic deterministic result 被模型正确消费）；capability probe 的预算纪律（2+1 授权制、Attempt 历史如实记录、临时脚本 gitignored 即删）
- ✅ 已可答（T012 Part B Phase 1，见 docs/tasks/T012-agent-tools.md + ADR002）：四种身份分离的最终定案（snapshot identity / live identity / run identity / generation，双条件消费、无第三套版本）；"snapshot identity 不得覆盖 live-world identity"与"重复身份元数据使不一致态可表达"两条实战教训（删字段 vs 加校验：类型层消灭非法态）；stale 三窗口防御（start 前零请求 / 工具执行后 batch 切换丢弃 / 迟到交付丢弃）；bounded Agent FSM 的双 3 上限与整批 validate-then-execute（无部分执行）；ModelScope native tool calling 全链（Gate 0 实证）上的 Runtime/adapter 分层与 fake-server 多回合脚本测试法
- ✅ 已可答（T012 Part B Phase 2 Learning，见 docs/tasks/T012-agent-tools.md Phase 2 段）：derived state 代替第三份 mutable bool（cloudAiBusy = aiBusy || agentBusy，UI+backend 双 guard 防竞态）；batch-change invalidation seam 的设计理由（seam 只改 live revision 不终止请求 → 加最小 invalidateForBatchChange 静默终止，三类 abort reason 的 UI 语义分家：UserCancel/BatchInvalidated/Superseded）；stale-start preflight 与 ST-A（stale 尝试必须零副作用且不打扰在途 run）；snapshot 唯一合法链在集成层的贯彻（copy→makeAgentToolContext→request，禁止展示层反推）；single-flight 产品定案（busy 禁用 + backend Busy；runtime supersede 降级为 defensive capability）
- ✅ 已可答（T012 Part B Phase 2 实现，见 docs/tasks/T012-agent-tools.md Phase 2 Implementation 段）：同一 validated config 喂两个 cloud client（测试 seam 与生产构造共用，杜绝 aiConfigured 与 Agent 配置漂移）；derived busy 状态在 Qt property 里的落地（NOTIFY 信号在每个 busy 转变点同步发射 cloudAiChanged，避免第三份 mutable bool）；批切换的 ordering 契约（revision++ → seam 先行 → 静默 terminate → 清呈现）与"abort 邻域迟到回调零发布"的确定性测试写法（不用 sleep 竞态）；集成层 stale start 不可表达时的诚实处理（runtime 层 B22 保契约、controller 层测 seam 同步不误杀新 run）
- ✅ 已可答（T012 Live Agent Smoke 教训，见 docs/tasks/T012-agent-tools.md Live Smoke 证据段）：工具调用上限在真实生产中被触发的完整证据链（模型长指令→并行/多轮工具→ToolCallLimitExceeded 防护按契约整批拒绝→零 facts 变化、无崩溃）；设计上限的取舍复盘（TOTAL_TOOL_CALLS=3 在多步探查场景下的张力与三个候选改进）
- ✅ 已可答（ISSUE-007，见 docs/issues/ISSUE-007-live-agent-tool-budget-exhaustion.md）：硬预算与模型计划空间之间的试调方法论（只读+immutable snapshot 使"提高本地查询上限"不扩权；rounds 守 loop 深度、total 守查询总量两者不可混）；RCA 的事実/推测纪律（exact sequence 不可观察时只引用可证事实，禁止把猜序写成事实）；one-tool-per-round 被否决的理由（多调用能力已测、并行使 latency 最优化）
- ✅ 已可答（ISSUE-007 完整故事，见 docs/issues/ISSUE-007-live-agent-tool-budget-exhaustion.md + T012 Final Acceptance）：第一次真实 Live E2E 未顺利通过——合法 multi-step query 触发 tool-call hard budget，但系统正确 fail closed（无死循环/无崩溃/零事实污染）；随后区分 provider round budget（rounds=3 不动）与 local read-only tool-call budget（3→6）并加 planning efficiency instruction；同一问题原文真实 re-validation PASS——bounded agent orchestration trade-off 的完整工程案例（含"不可观察 sequence 时 RCA 只依赖可证事实"的纪律）
- ✅ 已可答（T012 Post-Closure Stabilization，见 docs/issues/ISSUE-008 ~ 009）："closure 后真实使用回归"的工程常态与处置纪律（REOPENED/STABILIZATION 状态表达、不回退 LKGC、先 RCA 后实施）；fail closed 与可诊断性的张力（空 content → InvalidResponse 可见，但观察者无从知道"为什么空"——离线 RCA 的证明边界，hypothesis 与 evidence 显式标注）；额度/限流类问题"无稳定机器特征就不建专门枚举"的克制（合并 UX 文案 + 留待证据）；configured ≠ healthy 的 UI 语义审计
- ✅ 已可答（T012 Stabilization Final，见 docs/tasks/T012-agent-tools.md Stabilization Final Closure）：真实问题不可复现时的工程闭环五讲——不为了关闭 Issue 反复重试 Provider；区分 root-cause evidence 与 defensive contract hardening；"契约本已 fail closed、测试只补 coverage gap"的诚实陈述；provider failure UX 用 localhost fake HTTP 做 deterministic integration regression；历史 silent breakpoint 未证明即保 MONITORING 而不伪造 RCA
- ✅ 已可答（T013 收尾，见 docs/tasks/T013-final-integration-demo-polish.md §20~22）：Qt Quick Controls 原生 style 会静默忽略 QML 自定义 background/contentItem（运行时警告+checked ReferenceError）——切换 Fusion 的排查与证据链；表格对齐"single geometry owner"原则（表头与行共享派生宽度属性，禁止两个独立 RowLayout 各自分配）；视觉多轮人工闭环的工程节奏（自动验证 PASS ≠ 人工视觉 PASS；每次 FAIL 修正范围极小化）；QSerialPortInfo 与 .NET/PnP 证据等级区分（生产 API 事实 vs 辅助线索）；Machine serial availability 变化 vs 应用回归的 hypothesis 纪律
- ✅ 已可答（T014 完整故事，见 docs/tasks/T014-diagnostic-detail-preservation.md + docs/09_DIAGNOSTIC_COVERAGE_AUDIT.md）：**粗粒度 ProtocolError → M8.1 coverage audit 以 14 场景真实 fixture 证明“判定时刻已知道的原因在归一化后全部丢失” → T014 保持六状态不变 → 增加 orthogonal `TransactionIssue`（7 值 + 稀疏载荷）**。必答点：①为什么不拆状态——统计/仪表盘/Agent whitelist 消费归一轴，拆轴会连锁破坏 exhaustive switch 与金样；②`optional<struct>` vs `variant` 的取舍（7 值规模下 visit 开销与 breaking 演进不值）；③production invariant 的单向性质（`analyzeFunction03Transaction` 输出中 ProtocolError ⇒ issue 必有；下游对手工构造 issue=nullopt 防御 omit/unspecified fallback，绝不伪造 reason——防御与不变量各守一边）；④Statistics 逐位不变（STAT-B09 用“带 issue 批次 vs 手构造同 status 批次”锁定）；⑤Replay/Serial 零逻辑改动同漏斗继承（三模式共享 Core reason）；⑥Baseline authority 不变、batch breakdown defer（无消费者不写）；⑦AI/Agent 只消费 structured deterministic detail（machine token 与人类文案分层、system 语义句族禁止转译成 root cause）；⑧UI 最小 secondary text（ProtocolError 行第二行、非协议行零变化）；⑨RED-first 工程细节：模型壳先行使 RED 是 9 条断言失败而非编译错误；-Wmissing-field-initializers 两波与 value-init 工厂；⑩验证链：自动全绿 + clean/ctest/QML smoke/deploy 后**人工视觉验收 PASS**（临时 non-repo fixture 构造行级验收，Agent 不自报视觉 PASS）
- ✅ 已可答（T015 完整故事，见 docs/tasks/T015-passive-replay-expansion.md + docs/adr/ADR-003-broadcast-outcome-semantics.md）：**active-master 的 trusted-request 契约被错配到被动历史流量** —— FC06/0x10/generic Exception/invalid request/broadcast 全部进不了模型且“一条坏记录毒死整批”。必答点：①active vs passive 双契约（不推翻 T007：自己构造的请求可信；历史流量里坏请求就是要诊断的事实）；②三种坏输入三分（语法=load 失败 / 捕获请求协议非法=诊断事实 / 不支持≠非法）；③generic exception 只需五个事实（地址 + fn|0x80 + 单字节 code），因此“能诊断 FC08 的异常”≠“支持 FC08 正常语义”；④invalid request 的双事实表达（status=Exception + 独立 `TransactionRequestIssue`，Gate B 不改 T014 issue 职责）；⑤广播为什么升级为用户架构决策：六状态无诚实成员 → 穷尽论证 → 三案 + 精确数学（最终第七状态 `ExpectedNoResponse`；completed 含广播、成功率分母排除广播，避免“合法广播稀释成功率”与“假称写入成功”）；⑥per-record 化如何不静默丢记录（unsupportedRecords 显式事实 + UI 非致命提示 + 统计只声明 analyzed 子集）；⑦FC06 echo mismatch 必须独立 issue（格式合法≠语义匹配）；⑧RED 13 条断言量化旧链路代价、GREEN 24/24；⑨Passive understanding ≠ Active capability（写权限 grep 级回归锚）；⑩0x10 明确分 Part C（定长 vs 变长、取证量与矩阵体量）；⑪semantic audit 故事（用户 Review 前专项）：generic exception 规则 `response.fn == (request.fn | 0x80)` **必须前置 `(request.fn & 0x80) == 0`**——初版遗漏导致 request=0x88/response=0x88 `0x88|0x80==0x88` 自我匹配、1 字节载荷被误判 Exception 0x01；RED 21 passed/1 failed → 最小 guard → 0x88/0x88 落入既有 Unsupported，合法 0x08→0x88/0x01 仍为 Exception 0x01；GREEN passive 22/22 + ctest 24/24 + 零警告。**定性：generic exception matcher 的 protocol-semantic edge-case bug——不是 Provider/parser/device bug**；verified LKGC `02ce302`；⑫Function 0x10 passive（Part C）：requestIssues 由单值迁移为**有序 collection**（ordering=reporting order ≠ discard priority；防 cascade 依赖：quantity invalid 不派生 byteCount 期望、payload↔declared 仅需 byteCount 可读——MULTI-C01 两项并存/MULTI-C02 仅一项锁定）；structural reader 保留 odd-byte 事实但**不传播 register values**（Gate C8）；normal response=恰 4 字节 echo（起始地址+写入数量），`WriteMultipleRegistersEchoMismatch` 复用 T014/T015 载荷列而非新造字段（格式非法与语义不匹配分家）；broadcast 集显式 {0x06,0x10} 且 **expectation 与 request validity 正交**（invalid broadcast + NO_RESPONSE = ExpectedNoResponse + requestIssues，绝不 Timeout）；ExpectedNoResponse 三处旧“valid”表述同步为 orthogonal 口径、统计公式未动；RED 42/12 → GREEN 54/54 + ctest 24/24；写权限 grep 零命中；T015 整体 DONE（Phase A/B/Part C；Manual UI Review PASS），verified LKGC `ae067ab`（final production `da8ce48` + tests-only 锚 `ae067ab`）

- ✅ 已可答（Post-T015 benchmark 落库，见 docs/10_REPLAY_PERFORMANCE_BENCHMARK.md + scripts/bench_replay/）：**简历性能数字（10 万条中位 70.9ms / ≈141 万条每秒 / 0.709μs 每条 / 1M ≈686.7ms / 10k~1M 近线性）怎么测、被追问怎么办**。必答点：①口径=生产 Replay 链路（读 .mlog → `parseReplayLog` → `analyzeReplayLog` 含统计），单线程 Release/-O3、MinGW 13.1，不含 UI/Agent/渲染；②"中位"=同一规模多轮完整重跑后取中位数（非单次非均值），逐轮原始输出全在 §5.2；③混合样本构成确定可复现（8 类路径全铺：成功/异常/响应 CRC 错/超时/FC06/0x10/两类广播，生成器内置与 demo_v1 金样的 CRC 断言）；④同机独立复测对照：1M 档去 IO 同口径偏差 ≈4%、100k 档 ≈19%（parse 相 47~66ms 抖动是本机噪声带 + 原测构成未记录）——数量级一致即判定可信；⑤三数字内部换算自洽（70.9ms÷10 万=0.709μs；10 万÷0.0709s≈141 万/s；686.7/70.9≈9.7 倍≈近线性，复测 8.3~8.5 倍更优）；⑥诚实边界主动交代：原测当时未落库→本轮补齐复现脚本，基数为主机相对值非跨机 SLA——"发现证据缺口立即补证据"本身就是可讲的工程态度。













- 只读 Agent 的架构边界如何强制（类型层面无写 API）（T012）
- 无锁队列/环形缓冲的使用场景与取舍（T008/T010+）

## 4. 行为面素材

- **讲一次定位问题**：T001 的 qtlicd 提示 → 读官方提示 → 加环境变量 → 重构建零提示（证据都在任务档案）。
- **讲一次权衡**：为什么把 Modbus 解析自己写而不是引 libmodbus（学习价值 vs 生产力；对拍保证正确性）。
- **讲一次"防呆"**：AGENTS.md 把"每次只做一个任务、必须跑测试、必须更新文档"写成规约，约束自己也知道约束 AI。

## 5. 维护注意

- 更新时保留历史题目（不删不改），新题追加；被推翻的答案注明"已随任务 T00x 更新说法"。
## 6. Post-T016 M9-A 条目（2026-09-15 追加）

- **Q：为什么 token 单例不在 QML 层做模块单例，而走 context property？** A：先按 Qt 6.11 官方三步实现（pragma Singleton + QT_QML_SINGLETON_TYPE 源属性 + 生成 qmldir singleton 行），实测 exe-attached qrc 模块下运行期全量 `DS is not defined`；机理未完全隔离（诚实记录于 ISSUE-010），改 engine root context property——同样单实例、跨全局、AOT 稳定，且经 ctest/smoke 实证。
- **Q：怎么证明"presentation may change, behavior must not"？** A：组件只暴露 text/tone/label/valueText，不做任何业务判断；onClicked 与取值绑定留在 Main.qml 原样接线；统计格式化（toFixed(1)+"%"、"—" 占位、hasXxx 条件）逐一 diff 比对；回归靠 ui_bridge 55 slots + qml_smoke + 全量 ctest 24/24。
- **Q：QtQuick.Controls 的 Label 为什么在组件里报 "is not a type"？** A：Label 定义在 QtQuick.Controls 而非 QtQuick；QML 的 import 是逐文件的，组件文件必须显式 import 所用类型的模块——qmlcachegen 编译期不报、引擎实例化期才报，检测要走到运行。
- **Q：部署包为什么要复制生成模块而不是手写 qmldir？** A：手写第二份模块清单必然漂移（ISSUE-011：模块 1 类型→6 类型时部署态解析失败）；单一机制：xcopy 构建系统生成的模块整体，`prefer :/ModbusLens/` 保证内嵌资源是权威解析面。
- **Q：GUI 程序的 QML 错误取证技巧？** A：GUI-subsystem 进程 stderr 默认不通管道且 Windows 负退出码被 MSYS 映射成 127；PowerShell `Start-Process -PassThru` 取原始 ExitCode + `QT_ASSUME_STDERR_HAS_CONSOLE=1` 强制写 stderr，是本次定位的可靠组合。


## 7. Post-T016 M9-A remediation 条目（2026-09-15 追加）

- **Q：自动化全绿为什么还会出布局塌缩？** A：qml_smoke 只断言"组件树实例化成功"，不断言几何；PanelCard implicit 0x0 时根 ColumnLayout 分给它 0 高度，两行在 y=0 重叠绘制——实例化成功≠合同成立。补上的 `qml_geometry_check` 只锁尺寸合同（panel/每卡 w>0、行序、Diagnosis 不侵入），明确不是视觉验收替代品。
- **Q：为什么 container 的 implicit 不能靠 anchors 得到？** A：anchors 消费父尺寸（子跟随父），而 implicit 是"孩子汇合到父"的另一个方向；两个方向不能互相代替——PanelCard 的 implicit 必须来自内容布局的 implicit（children 派生）+ padding，这正是 ISSUE-012 的根因。
- **Q：运行时几何取证怎么落地？** A：CLI 探针加载真实 QML → 事件循环 settle（100ms×重试）→ 断言 + `QQuickWindow::grabWindow` 输出真值位图；两个坑：QObject findChild 找不到 Repeater delegate（要走 childItems visual 树）；外部截图的 DPI 虚拟化会拿错像素（实测 1024x720 裁剪 vs 真实 1280x900）。


## 8. Post-T016 M9-A closure 条目（2026-09-15 追加）

- **Q：M9-A 交付了什么、怎么收的口？** A：tokens（单一语义源）+ 四个有真实迁移用例的组件 + Top Actions/Statistics 两处迁移 + 几何回归守卫 `qml_geometry_check`；流程是"自动全绿→人工 FAIL→运行时取证→最小修复→自动+人工双 PASS"，verified LKGC = 修复提交 `6562dd3`。
- **Q：为什么人工 PASS 只覆盖 M9-A？** A：PASS 的语义边界必须写清：仅证明"首次迁移的视觉回归已解决"，不证明"整个 M9 视觉刷新完成"——Serial/Diagnosis/Transaction styling、icon、shell/navigation、native-title 一致性都是后续里程碑的事；把关单范围收窄是工程纪律，不是保守。
- **Q：为什么不一次把 9 个组件都写完？** A：组件清单是"由真实重复模式决定的候选集"，不是库存 KPI；没有迁移用例的组件（StatusBadge/FieldRow/SegmentedTabs/EmptyState/Banner）留到对应区域真正迁移时再引入，避免做出来没人用、还得跟着需求返工。

## 9. Post-T017 M9-B Phase 1 条目（2026-09-15 追加）

- **Q：导航为什么选左侧 rail 而不是顶部 tab？** A：用真实布局预算说话——1000×700 最小窗口下垂直轴已到极限（单页堆叠时工作区只剩 ≈190px），顶部 tab 吃常驻垂直空间且 5 项无增长余量；rail 吃的是富余的水平轴，折叠 56px 时内容 888 ≥ 既有并行分栏最小值 820，700 高下页面可用 592px（+400 收益）。屏幕轴宽裕度决定导航形态，不是审美。
- **Q：Source 和 Workspace 什么关系？** A：Source 是"数据从哪来"（Simulator/Serial/Replay，session 唯一，由 Controller 原子切换）；Workspace 是"用户在做什么任务"（总览/通信/回放/诊断/设备）。两者正交：任何 workspace 不改变 source，source 切换不强制跳页；全局来源 chip 是 source 唯一可见副本。
- **Q：切页会不会把正在进行的 AI/Agent 请求打断？** A：不会——因为设计上页面只是显示面：StackLayout 全实例化、页面不销毁；异步有效性在 Controller（批次 revision × 请求 generation 二维守卫），页面可见性不是失效条件。反过来 Loader 的销毁语义会打碎这个保障，所以被排除。
- **Q：为什么要专门写"State Ownership Rule"？** A：拆页的最大风险不是视觉，是把 session 状态拆散成页面各自一份（例如每页一个 transaction model / 每页自己记 serialConnected），这会直接违反 r07/r08/s02/s10 等既有契约。规则一句话：业务状态留 Controller，页面只消费与发信号。


## 10. Post-T017 M9-B1 条目（2026-09-15/16 追加）

- **Q：StackLayout 的子项为什么必须"纯 Item + 内部锚定"？** A：Layouts 家族对子项拥有几何所有权——子项自己 anchors 会被运行时判为 undefined behavior，`Layout.margins` 则被 StackLayout 直接忽略（两个都是本项目实测）；把内缩收回页面内部（页根模式），B2–B5 的每个 Page 都会复用这条模式。
- **Q：壳层如何做到"导航不改业务"？** A：壳层唯一新增状态是选中 index；来源/串口/批次全是只读绑定；护栏直接断言"禁用入口无法改变 index"（且校验 invoke 返回值，防止断言空转）——把不变量交给 CI 而不是口头约定。
- **Q：为什么 clearResults 可以进 AppBar？** A：先核验语义再搬家——clearResults 清结果但不换 source、不断连接（代码注释 + r08/s08 契约），属 session 级动作；搬迁只改位置，onClicked 逐字保持。
- **Q：自动测试通过后为什么还要人工视觉？** A：B1 期间 smoke 抓到 anchors 警告、dump 抓到 Layout.margins 被忽略——都是"自动通过≠正确"的实例；几何/像素断言锁合同，视觉质量仍由人工验收（M9-A 已有先例）。


## 11. Post-T017 M9-B1 closure 条目（2026-09-16 追加）

- **Q：LKGC 怎么判定？** A：看「真实 product/QML/Qt 代码变更 + 全链路验证 + 人工验收」三件套——M9-B1 的 `189c62c` 满足（smoke/geometry/ctest 25/25/deploy/manual PASS），docs-only 回填（`b7e7d72`）永远不作 LKGC；V1 的 `v1.0.0` tag 与 V2 LKGC 是两个概念，前者永久不动。
- **Q：为什么"改版"可以只搬位置不改行为还值得大动干戈？** A：因为行为不变本身是可验证的承诺——Clear Results 搬家前先核验 r08/s08 语义、onClicked 逐字保留，护栏把"禁用入口不能改 index"钉成断言；presentation may change, behavior must not 是流程（先证明语义，再动手），不是口号。


## 12. Post-T017 M9-B2 Phase 1 条目（2026-09-16 追加）

- **Q：迁移期怎么避免"两处显示同一状态"变成"两处各存一份状态"？** A：共享 presentation 组件（StatisticsOverview）+ 单一 Controller 快照：两处渲染的是同一条绑定链；判定准则一句话——"删掉任一处视图，另一处必须照常工作"。
- **Q：为什么"页面对象身份不变"不能单独证明状态存续？** A：身份只能证明实例没被销毁重建（生命周期证据）；业务值是否真的不变必须逐值断言 authoritative properties（状态证据）——`qml_nav_check` 把两类证据分开写、分别失败，避免用一条弱证据冒充两条。
- **Q：为什么 Run Demo 在 Dashboard 而 Clear Results 在 AppBar？** A：唯一判据是"是否切换 source"：runDemoBatch 会 teardown 串口并改 mode/source（上下文任务动作）；clearResults 明确不换来源不断连接（与来源无关的 session 动作）。


## 13. Post-T017 M9-B2 实施条目（2026-09-16 追加）

- **Q：怎么证明"Legacy 迁移后视觉零变化"？** A：不是靠眼睛说没变，而是把 B1 的像素区域检查在新截图上重跑，计数逐项相同（158/234/744/40/59/30 + 每卡 dark）；迁移的第一步必须可证明是恒等变换，否则后续所有对比都失去基线。
- **Q：导航测试最有价值的断言是哪一条？** A：全字段业务快照跨切换不变——它同时覆盖"没调用业务命令"与"没重置状态"；页面身份指针相等只证明生命周期，两者缺一不可。再加禁用项 invoke 不可切，就构成"选择合法性 + 状态守恒 + 不可达约束"的三角。
- **Q：为什么测量脚本必须先 transition→settle→measure？** A：Qt 布局是异步的：切页/缩放后立刻读，会拿到上一趟的几何（实测 dashboard 面板 864 vs settle 后 935）；从未激活的隐藏页更是 0×0。测量纪律本身是证据质量的一部分。


## 14. Post-T017 M9-B2 closure 条目（2026-09-16 追加）

- **Q：怎么给"完成"选 LKGC？** A：用 Git 文件列表分类：最后一个**行为承载**提交且该行为处于本轮终验范围内者当选（B2 的 `53685d5` 只改了 `src/main.cpp` 的 nav check 场景，终验正是跑它）；docs/截图/回填提交即使命名为"candidate"也永不作为 LKGC——**命名不构成证据，文件列表才构成证据**。
- **Q：多页应用里"状态还在"怎么证明？** A：三层证据：对象身份指针（没重建）+ 全字段 authoritative 快照（业务值没变）+ 真实命令场景（demo/诊断/clear 跨页后逐值相等）；再加上人工行为链确认"切页没有触发 source transition"。


## 15. Post-T017 M9-B3 Phase 1 条目（2026-09-16 追加）

- **Q：什么是 command draft，为什么不能进 Controller？** A：draft 是"尚未提交的候选"——Slave=5 在点击读取前不是任何设备/session 事实（真实代码：`pendingSerialAddress_` 只在 adapter 真正接受 startTransaction 后才写入）；把 draft 写回 Controller 会制造第二份事实源，并让"失败不产生痕迹"的原子语义失去意义。
- **Q：连接成功的 port/baud 和 ComboBox 里的选择是同一个东西吗？** A：不是。前者是 `serialSourceLabel_`（会话身份，AppBar 显示），后者是页面候选；失败连接**不留任何会话痕迹**（s02 的原子保留）——这两个概念在迁移中最容易被压平。
- **Q：为什么 Serial 跨导航测试只能"部分自动化"？** A：adapter 是具体 `QSerialPort` 成员、无依赖注入，离线无法产生 connected=true；能诚实覆盖的只有失败路径（connectSerial 到不存在端口→错误置位→跨页不变），完整 connected/pending 场景保持 DEFER 并由人工验收——**不制造与真实行为不一致的 fake contract**。


## 16. Post-T017 M9-B3 实施条目（2026-09-16 追加）

- **Q：为什么"刷新串口"没有 enabled 绑定也必须原样保留？** A：extraction 的验收标准是可证等价：任何"顺手优化"（busy-disable、自动 refresh）都会污染迁移的恒等性；行为改进属于独立 task——这条边界让回滚与回归对照都保持干净。
- **Q：Qt Quick Layouts 里"多余的纵向空间"归谁？** A：归显式声明 fillHeight 的子项；**一个都没有时余量会以你意想不到的方式出现**（本项目实测：ColumnLayout 把 458px 余量散布到子项之间，189 内容 vs 647 实高的页面）。Legacy 一直正常只因为 SplitView 恰好带 fillHeight；显式尾部 spacer 是把这条隐式契约变成可读代码。
- **Q：怎么在无硬件条件下验证 serial 失败语义的跨页保持？** A：走真实命令的真实失败分支（connectSerial 到不存在端口）并断言**稳定布尔属性**（hasSerialError）与 mode/source 原子性，而不是伪造端口或绑定 OS 错误文本；完整 connected/busy 场景继续 DEFER 并由人工覆盖。


## 17. Post-T017 M9-B3 closure 条目（2026-09-16 追加）

- **Q：surplus-space 问题与 ISSUE-012 的塌缩问题有何本质区别？** A：ISSUE-012 是**尺寸链断裂**（容器 implicit=0、父子合同崩溃、几何塌缩）；B3 是**尺寸链完好但多余空间的归属没有设计**（implicit 189 vs 实高 647，余量被散布）。前者修合同（implicit 来源），后者修意图（显式 spacer）——症状相似、根因与修法完全不同，混为一谈就会用错药。
- **Q：为什么 LKGC 不能由 commit message 决定？** A：`072fe34` 命名含 "candidate"，Git 文件列表却是 docs+screenshots only；`382ecfb` 名字普通，却是最后一个行为承载提交（仅 main.cpp 的 nav check 行为，处在终验范围内）。**文件列表 + 是否经过完整验证才是判据**。
- **Q：pixel helper 与人工视觉验收的关系？** A：它是合同探测工具（几何/存在性/签名），不是视觉质量判据；本轮它自己连续出过三类假结果（陈旧截图/过期坐标/颜色碰撞），所以验证器本身也要有"输入可验证"纪律——但即使全绿，视觉验收仍归人工。


## 18. Post-T017 M9-B4 Phase 1 条目（2026-09-16 追加）

- **Q：用户选中的文件和"当前回放来源"是同一个事实吗？** A：不是。selectedFile 是对话框候选，只在 onAccepted 瞬间被消费；只有 parse→analyze→adapt 全部成功后 `sourceLabel_` 才被赋值为**文件名**（完整路径永不进 UI）——失败选择零痕迹（r03/r05）。把选择当来源会破坏这条原子契约。
- **Q：回放加载失败后，"上一次成功的披露"还在吗？** A：在——当前真实契约是**失败只置 error、不清 notice**（notice 属于上一次成功加载）；这是"旧披露+新错误"并存的现状，被 r06 依此测试。迁移原样冻结；是否改进属未来任务。
- **Q：为什么 FileDialog 可以随页常驻？** A：StackLayout 全实例化 + 页面 identity 断言（B2/B3 证据）保证对话框实例零重建；对话框没有需要持久化的业务状态（selectedFile 是瞬时候选）。


## 19. Post-T017 M9-B4 Review Correction 条目（2026-09-16 追加）

- **Q：状态放在 Controller 里就等于"会话事实"吗？** A：不。所有权≠语义类目：replayError 是 per-attempt（描述最近一次尝试），replayNotice 是 per-loaded-session（描述当前活跃批次的 unsupported 披露）——都由 Controller 持有，但语义生命周期完全不同。分类要按"事实属于谁的生命周期"而不是"字段存在哪个类里"。
- **Q：怎么避免引用测试证明它没证明的东西？** A：引用前复读测试原体。我曾写"r06 证明失败保留 notice"——复读 r06 原体发现它只断言 error 恢复；正确做法是撤回证据表述、把行为重定性为 code 观测，并补上缺失的自动断言（Scenario K′）。


## 20. Post-T017 M9-B4.2 条目（2026-09-16 追加）

- **Q：原子迁移+启位为什么必须同提交？** A：拆开会产生"能力暂时消失"的已提交状态（迁走了但页面不可达）——违反 M9-B 的增量不变量。B3.1（迁移+通信启位同提交）与 B4.2（迁移+回放启位同提交）都是这条不变量的执行。
- **Q：navigation ≠ source transition 怎么持续被证明？** A：nav check 每新增一个真实 workspace，就把全字段 authoritative 快照比较延伸到新站——B4.2 后 Replay 站的加入使"切到回放页不触发加载/切源"成为第四次机器实证（r07 的 source 交替语义由显式命令保持，与导航无关）。


## 21. Post-T017 M9-B4.3 条目（2026-09-16 追加）

- **Q：K 和 K′ 为什么要分开？** A：K 证明"非空会话的失败替换是原子的"（error 置位 + source/rows/statistics 不变）；K′ 专门证明 **replayNotice 的生命周期**——unsupported 会话的成功加载不是 error、notice 属于该会话、失败尝试后 notice **逐字节**保持。合并会丢掉"披露语义"这个独立断言面。
- **Q：oracle 键集缺陷给你们什么教训？** A：捕获快照和比较快照用了不同键集（捕获 20 键、比较 16 键），缺失键的比较永远取到空值——假 FAIL。修法：比较与捕获必须同一键集函数；更普适的教训是"验证器的输入与期望必须同源"。
- **Q：为什么 Replay 页面"大空白"不是 bug？** A：v1 的 Replay 页只有加载动作与结果披露，剩余空间是 M10 请求/结果区的预留容量（八趟几何实测：内容止于 ~89 逻辑高，页面 627 可用）——空白是设计容量，不是布局缺陷。


## 22. Post-T017 M9-B4.3 HOLD 条目（2026-09-17 追加）

- **Q：同一个 oracle 键集缺陷为什么会出现三次？** A：因为每次新场景都手工扩展比较逻辑——第一次（M9-A）是固定坐标、第二次（B3）是显示文本、第三次（B4.3）是键集不同源。根治=比较与捕获强制走同一个快照函数、扩展键集集中声明（takeExtendedSnapshot）；三次教训的共性是“验证器的输入与期望必须同源且可自证”。
- **Q：加强断言后测试红了，怎么区分“产品回归”和“oracle 缺陷”？** A：看失败签名与代码证据的交叉：K′ 假 FAIL 只报 noticeText 一个键且取值为空（取值缺失的特征），而生产代码 grep 证明失败路径零 notice 调用——两边证据合流指向 oracle；若生产代码真有清除调用，那就是真回归。


## 23. Post-T017 M9-B4.4 条目（2026-09-17 追加）

- **Q：截图证据怎么做到"来自哪个候选"可自证？** A：evidence-capture 模式内嵌于被部署的同一二进制，出图前先断言业务状态（notice 逐值、source basename、observed=4），日志同时记录 IMAGE_SIZE 与状态值——再配合"每轮重新出图、禁止复用旧图"的纪律，构成 VERIFY_INPUT 闭环。
- **Q：失败替换的视觉证据怎么造？** A：用真实命令加载真实失败（不存在的路径）——零临时 fixture；error 与旧 notice **并存**的画面正是"失败不污染旧会话"的 K′ 视觉对应。


## 24. Post-T017 M9-B4 closure 条目（2026-09-17 追加）

- **Q：M9-B 四步迁移后，"状态在 Controller"这条原则带来了什么？** A：四页任何一个的迁出/迁入都没有触碰 Controller 一行代码；跨页状态一致性由统一快照断言机器化证明（A/B/D/E/F/G′/H/I/J/K/K′）；人工验收只需看视觉与交互——架构分工让自动化与人工各管一面。
- **Q：error 与 notice 同时可见，用户会困惑吗？** A：不会混淆——notice 描述**仍然活跃的旧会话**（unsupported 披露），error 描述**最新一次失败尝试**；两者语义对象不同。若未来要做 UX 改进（来源/上下文标注），属 M9-C/M10 的呈现课题，不是语义错误。
- **Q：验证器为什么连续出三类假结果还能被信任？** A：因为每次假结果都被**取证分类**（oracle bug / 拼接错误 / 坐标过期）而非掩盖，且修正后沉淀为原则（快照同源、输入身份、自适应期望、可区分签名）；"验证器可信"来自它自身被验证的历史。


## 25. Post-T017 M9-B5 Phase 1 条目（2026-09-17 追加）

- **Q：AI 请求进行中切到别的页面，结果会丢吗？** A：不会——结构上 aiClient_/AgentRuntime 在 Controller，完成回调按（generation × batchRevision）二维守卫核对身份，与页面可见性无关；B2 起的页面 identity 断言证明页面只是呈现面。切页≠cancel，这是冻结契约而非实现巧合。
- **Q：Ask AI 和 Ask Agent 能同时进行吗？** A：不能——single-flight 双向互斥（cloudAiBusy=AI busy ∨ Agent busy，无第三个 bool）。两边的前置检查顺序不同（AI 先查 Agent busy、Agent 先查 AI busy）但语义同为"云工作流单飞"；迁移时这两个前置序必须逐字保留。
- **Q：Agent 的问题草稿算会话事实吗？** A：不算——它是 QML 页本地 draft（askAgent 参数直传，Controller 从不存储问题文本）；切页往返草稿保留由 StackLayout 常驻自然提供，不需要也不应该写回 Controller。


## 26. Post-T017 M9-B5.2 条目（2026-09-17 追加）

- **Q：怎么证明一次"迁移"没有偷偷改需求？** A：把判据做成可执行的比对，而不是靠人眼读 diff。以迁移前的原文件为基准，把两边都规范化成**语义语句多重集**（controller 绑定/命令、qsTr 文案、冻结色、visible/enabled/onClicked 表达式），再做差集：107 条语句、78 条唯一 → 迁移后 78/78 全在、零发明，只剩 4 行"页面外框"差异（18px 标题与 pane 卡片外框被统一页骨架替代）需要单独申报。把"有意替换"和"意外丢失"分开计数，是迁移类改动的验证核心。
- **Q：把 SplitView 换成 Layout 之后，Transactions 面板为什么必须重新指定 objectName？** A：因为它同时是**布局子项**和**几何断言的锚点**。旧守卫用 `diagnosisWorkspace`（被删除的 SplitView）判断"工作区下缘不侵入统计面板"；若不同步改锚点，`findNamedItem` 返回空，`if (ptr && ...)` 短路 → 断言**静默变成永真**。删掉一个被测对象时必须回头找它的所有断言消费者。
- **Q：什么是"空转的断言"，为什么会长期存活？** A：`if (p && p->… )` 形守卫只证明"当 p 存在且违规时失败"，不证明 p 存在。ISSUE-013 里内层 `auto *row2/header/panel` 遮蔽了外层同名局部，外层恒为 nullptr ⇒ 行重叠与下缘侵入两条断言从加入起就没生效过。它存活的原因正是**它的失败路径从未被走过**。修法除了去掉遮蔽，还要做**变异探针**：故意让断言在正确输入下应当 FAIL（本轮把比较常数 +1000.0），确认它真的报 FAIL，再还原。
- **Q：为什么"迁移+启位"必须在同一个提交里完成？** A：拆开会产生一个已提交的中间态：能力已经搬走，但入口还没打开——用户在那次提交上无法使用诊断功能（B4 序列修正确立的不变量）。B3/B4/B5 三次迁移都遵循同一条：`move` 与 `enabled: true` 同提交，`workspaceXIndex` 同提交，Legacy 侧的删除也同提交。
- **Q：Legacy 工作台在 B5.2 之后还剩下什么？** A：只剩统计总览（legacy 实例）与最近通信记录两件，且从"左右分栏"变成"上下全宽"——这是移除左栏的**机械后果**（SplitView 包装消失、右子提升为列直接子项、min 520 作为内部约束保留），不是重新设计。判断标准是：新功能落共享层（Controller/DS/共享组件），页面只做呈现；本轮 `git diff --stat` 里没有任何 Controller/Core/backend 文件。


## 27. Post-T017 M9-B5.3 条目（2026-09-17 追加）

- **Q：一个没有暴露给 UI 的内部字段（batch revision），怎么在"不改产品代码"的前提下被测试断言？** A：用它的**失效不变量**做代理。源码核验 `++activeBatchRevision_` 只出现在一个函数里，而该函数无条件清空 baseline——于是"baseline 仍在且逐字不变"就等价于"revision 没动"。测试要的是可证伪性，不是字段可见性；为测试给 Controller 加 Q_PROPERTY 会把测试需求倒灌进产品接口。
- **Q：怎么区分"这是持久化测试"还是"重算测试"？** A：看测试是否重建了被测状态。Scenario L 只在开头建立一次 demo 批次与 baseline，之后**只导航、不重算**；任何一站发现不一致都必须报失败，而不是"再跑一次 baseline 让后面继续绿"。前者证明状态真的跨页存续，后者只是规模更大的冒烟测试。
- **Q：为什么"当前选中的 tab"是几何契约的边界？** A：因为 StackLayout 里隐藏 tab 的几何**没有语义**——从未选中的 tab 停在 implicit 尺寸（444×48），选中过又隐藏的 tab 保留上次实际尺寸（935×591），差异纯属历史。把历史当契约就是给未来的自己埋假失败。想覆盖三个 tab 就要**真的逐个选中它**（tab sweep），而不是对隐藏内容下断言。
- **Q：pass 了 10 趟几何，怎么知道这些断言不是空转的？** A：加一条与断言正交的守卫——每趟测量前断言"当前激活页确实是这一趟的目标 workspace"。否则一个没被真正激活的页面会让它那一段断言整段跳过，而整趟仍然"PASS"（ISSUE-013 的同类风险）。断言的可信度来自"它有能力失败"，不来自它这次通过。
- **Q：把场景通过与否做成"区间内失败增量为 0"有什么讲究？** A：这样判决**只能少给通过、不会错给**。区间取保守外包（例如 Scenario E 的区间覆盖了中间的 Replay/Diagnosis 站），代价是失败归因变粗，收益是绝不可能出现"自身断言失败却报 PASS"；未跑完的场景显式报 NOT RUN 并计失败，也不会被静默省略。
- **Q：为什么 Scenario N 只测"问题草稿"，不顺手测 Agent 的回答？** A：草稿是 QML 页本地状态，测它零基础设施；回答是 Controller/runtime 生命周期，要测就得往 app 里塞一个 fake provider，为一次 extraction 测试引入新的传输层 harness 风险大于收益。所以回答侧保持 DEFERRED BY DESIGN，由既有 fake/offline 测试与人工覆盖——**自动化覆盖的边界要写清楚，而不是用近似物冒充**。


## 28. Post-T017 M9-B5.4 条目（2026-09-17 追加）

- **Q：怎么证明"部署版能独立运行"，而不是碰巧用了开发环境的 DLL？** A：把 PATH **替换**成只剩系统目录（`C:\Windows\System32;C:\Windows`），而不是"前置"。前置写法看着像最小 PATH，实际 Qt/MinGW/Anaconda 都还在，等于没测。替换后仍能 smoke/nav/geometry 全绿，才说明依赖真的来自部署目录。
- **Q：截图证据怎么避免"文件名当状态"？** A：每张截图前显式断言状态（workspace index、navItem.selected、页面可见性、当前 tab），日志把"断言过的状态"与截图一一对应；文件名只是标签。B4 的教训正是靠文件名子串推断页面，结果读到了过期/错位的图。
- **Q：为什么逻辑尺寸和像素尺寸要分开记录？** A：DPI 缩放让两者天然不等（本机 125%：请求 1024×720 → PNG 1280×900）。把它们合并成"一个尺寸"会让任何跨机器比对失去意义，也会把正常的缩放误判成失败。
- **Q：截图的业务真值能不能靠像素/OCR？** A：不能。业务断言在截图之前用 Controller 的字段完成（golden facts、successRate、baseline 文本长度），像素只负责"这张图确实被渲染出来了、内容非纯色、五张互不相同"。图像自检证明的是**证据完整性**，不是业务正确性。
- **Q：`successRate` 为什么断言成 0.25 而不是 25？** A：因为它在 Core/Controller 里是**分数**，QML 显示时才 ×100。测试里写错单位会造出"产品缺陷"的假象——遇到这种红灯要先查数据语义，再怀疑产品（本轮正是 test oracle 错误）。
- **Q：为什么不允许为了截图去触发一次真实 AI 请求？** A：截图要证明的是"UI 在真实配置状态下布局正确"，不是"Provider 可用"。触发请求会把网络、凭据、配额、非确定性带进证据链，还可能把 token 写进日志或截图。所以 harness 只读 `aiConfigured` 布尔并断言 `hasAiDiagnosis`/busy/error 都处于"未发生"状态——**用断言证明"没有发生"，而不是用假设**。


## 29. M9-B (T017) Closure 条目（2026-09-17 追加）

- **Q：workspace 与 lifecycle 的区别是什么，为什么 B5 反复强调？** A：workspace 是**呈现归属**，lifecycle 是**状态生命周期**。把诊断 UI 搬进独立页面只改变"内容画在哪里"，不改变"谁拥有状态、什么事件会取消/清空它"。所以 `DiagnosisPage.qml` 里没有任何 `onVisibleChanged`/`Component.onCompleted` 调用业务命令——否则切页就会变成隐式生命周期事件，AI 请求会被页面可见性取消，跨页结果持久性也就不复存在。
- **Q：怎么判断一个页面该持有什么？** A：按"事实 vs 呈现"分：**事实**（批次、统计、事务、baseline、AI/Agent 状态、source/mode、revision）只能由既有 Controller/backend owner 持有；**页本地呈现**（当前 tab、问题草稿、滚动位置）由常驻页面持有并跨导航保留。草稿尤其不能为了"能持久化"而回写 Controller——它是**候选输入**，不是**事实**，回写会污染 `askAgent(question)` 的参数边界。
- **Q：extraction 阶段最容易被顺手改坏的是什么？** A：语义冻结项。B5 只搬 UI，所以 `clearDiagnosis`、Ask AI/Ask Agent 的前置序、single-flight 互斥、失败清 error 保旧文本全部逐字保留；特别地**不能**因为 Baseline/AI/Agent 现在同处一个 workspace 就推导出"三者应该有相同的 clear/cancel 生命周期"——那是产品重设计，不是迁移。
- **Q："evidence commit" 能不能因为名字而不算 LKGC？** A：不能。LKGC 的判据是**文件列表 + 该树是否经过完整验证（含人工验收）**。B4 的 `207ae96` 本身就是 harness+截图提交且被裁定为 LKGC；B5 的 `6cc84c3` 含 `src/main.cpp` 与部署脚本行为变更，判据一致适用。反过来 docs-only 提交永不成为 LKGC。
- **Q：为什么照片自检不能替代视觉验收？** A：自检只能证明"截的是那个状态、尺寸有效、不是同一帧复制"——它证明的是**证据完整性**，不是**设计正确性**。视觉正确性只能由人看：层级、密度、可读性、有无裁切，这些都不是像素计数能判定的。


## 30. Post-T017 M9-C Phase 1 条目（2026-09-17 追加）

- **Q："视觉丰富化"和"造数据"的边界在哪？** A：边界是**权威来源**。Dashboard 允许的任何元素必须是既有 Controller 属性的**派生视图**（布局/比例/文案）；一旦需要"趋势""健康分""设备健康""finding 计数"，就必须先有历史时间序列 / 业务评分定义 / Device Profile / Core 侧聚合 API——这些都不存在，所以它们不是"以后再做"，而是**现在不能做**：用当前快照画折线就是伪造时间维度。
- **Q：为什么改共享组件等于改 Legacy？** A：`StatisticsOverview` 有**两个真实实例**（`instanceId: legacy` / `dashboard`）。它不是一个"内部实现细节"，而是两个已人工验收页面的共同呈现面。判定要不要动它，看的是"这个改动会不会让另一个已验收页面变化"，而不是"改动看起来多小"。
- **Q：怎么既重组 Dashboard 又不动 Legacy？** A：把共享件按**真实复用边界**拆成呈现件（`StatisticsMetrics`、`StatisticsOutcomes`），共享件退化为**组合**（Legacy 的嵌套/行高/卡面完全不变），新元素（分布条）单独成件只挂 Dashboard。这样拆分有**两个真实消费者**支撑，不是为了组件数量而抽象；Legacy 的"零变化"还能用几何逐值比对证明。
- **Q：页面里的"空白"为什么会变成 bug？** A：空白本身不是问题，**归属不明**才是。Communication 有 459px 尾部空白却是设计（B3 的显式尾 spacer）；Dashboard 的 ~185px 出现在两个 item **之间**、同时又留 162px 在末尾——那是布局引擎在替你做决定。修法不是"消灭空白"，而是显式声明：内容紧凑堆叠、主区有限长高、余量归尾部并写明是给未来功能的预留容量。
- **Q：为什么拒绝加"去诊断页"这类 CTA 按钮？** A：因为按钮天然读作"执行命令"。项目里导航的唯一权威是 NavigationRail（单点 `activate()`、禁用项不可改 index、被 nav check 断言）。再加一排按钮会产生第二导航入口，并让"导航"与"执行"（connect / load / run baseline / Ask / 切 source）在视觉上难以区分——**引导用文本，动作才用按钮**。
- **Q：同一个数字分布，为什么选堆叠条而不是饼图？** A：因为要表达的是"**已完成**事务里各类占比"（分母=completed，pending 不属于终态）。堆叠条只需要一维宽度且天然支持"0 计数段不可见"，饼图在 7 类、小尺寸、多零值场景下既难读又必须配图例；而且堆叠条的段宽可以**逐段断言**（宽度 = 内容宽 × count/completed），饼图做不到这么直接的可证伪验证。


## 31. Post-T018 M9-C C2 条目（2026-09-17 追加）

- **Q：抽取组件时最容易破坏的隐形契约是什么？** A：**implicit size 链**。PanelCard 的尺寸来自"内容 implicit + padding"，把两行换成组件根后，链条是否还通，取决于新组件根有没有真实 implicit。所以 wrapper 的 `implicit > 0` 被写成常驻断言——ISSUE-012 的教训从"一次性修复"升级成"结构不变量"。
- **Q：instanceId 为什么由 wrapper 向下传，而不是让子组件自己生成身份？** A：因为身份的所有者是"使用场景"（legacy/dashboard 两个实例），不是组件类型。子组件自取名会让 wrapper 与子件的身份脱钩，harness 就无法证明"这一行的面板是哪一个实例的"。向下传还让旧 objectName（`statisticsRow1_legacy` 等）原地保留——改名是最廉价的破坏方式。
- **Q：怎么向 Reviewer 证明"抽取前后完全一样"？** A：按 **item 名**对齐前后两份几何 dump，逐项比较 x/y/w/h/implicitW/implicitH，并单独列出 parent 名变化与新增项。第一版比对按行序 zip，被新增的 wrapper 行整体错位，误报了 10 处"差异"——**证据工具本身也要按可辨识的键对齐，而不是按出现顺序**。
- **Q：传整个 Controller 给子组件，不怕耦合吗？** A：这里的耦合判据是"是否引入第二权威或业务计算"，不是"引用传递的深度"。该组件与 StatisticsOverview 同属一个 feature 家族，本来就是 Controller 事实的呈现面；改成 11 个标量反而要在 wrapper 复制整份绑定，制造新的失同步点。真正禁止的是：缓存、重算、字符串解析。
