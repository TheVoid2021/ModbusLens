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


## 32. Post-T018 M9-C C3 条目（2026-09-17 追加）

- **Q：一个"看起来只是画条"的组件，最容易做错的是什么？** A：**分母**。分布条的分母是 completed（ExpectedNoResponse 是一种完成结局，必须占一段）；成功率的分母刻意剔除 ExpectedNoResponse。两者只差一个状态，但语义完全不同。所以本轮专门造了一条全 ExpectedNoResponse 的批次做探针：bar 应该 100% 是 ExpectedNoResponse、而成功率仍是 "—" —— 两个分母一旦混淆，这条断言立刻红。
- **Q：一条没有数据的 bar 有什么可测的？** A：零态本身就是契约：整个组件隐藏（不渲染空轨道）、宽度计算里不能出现除零或 NaN。而且 `visible: false` 不阻止绑定求值，所以"隐藏"并不能豁免"数学必须安全"——单位宽在零态被显式置 0，NaN 从根上不可能出现。
- **Q：这次 bar 的 bug 是怎么被抓住的？** A：段宽断言 + 一行 DIAG 输出。症状是"段宽全 0、末段却吃满整条"，DIAG 显示 `completedTotal=4` 但 `unitWidth=0`，stderr 里 `ReferenceError: barTrack is not defined` —— bar 的 Item 忘了写 id，绑定静默失败成 0。教训有二：①新绑定的依赖目标必须真实存在（缺 id 不报加载错误）；②stderr 的 ReferenceError 计数必须是一级门禁，不能只看 exit code。
- **Q：为什么在 Dashboard 上断言"wrapper 不存在"，在 Legacy 上断言"wrapper 必须存在"？** A：两侧同时锁，才能证明这次是**真迁移**而不是"复制一份新组合、旧壳留着冒充"。消失与存在都是契约——结构迁移的验证必须同时覆盖"新形态在"与"旧形态不在"。
- **Q：分布条的信息已经和六张卡重复了，为什么还画它？** A：它们回答的问题不同：卡片回答"每类**多少**"，条回答"哪类**占多数**"。重复的只是数字来源，不是信息维度；并且刻意不再复制第二排文字图例——六张卡就是 legend，颜色只是辅助通道。


## 33. Post-T018 M9-C C4 条目（2026-09-18 追加）

- **Q："attention 求和"和"health score"的边界到底在哪？** A：求和是**可逆的呈现动作**——四个权威计数相加，用户能从 breakdown 还原每个输入；health score 是**不可逆的语义发明**——权重、阈值、Healthy/Unhealthy 分档都不是数据里有的东西。本轮把边界写进测试：数值 oracle 锁"和=四计数之和"，措辞契约锁"只谈事务结果"，任何分档/命名都会同时破坏两者。
- **Q：三个 runtime 状态为什么是 demo/broadcast/protocol-error 这一组？** A：它们把 attention 公式的每个输入维度都推到非零：demo 证明 breakdown 的省略规则（protocol=0 不出现）、broadcast 证明 ExpectedNoResponse 不进和且零 attention 时措辞仍是 outcome-limited、protocol-error 证明协议错误真的参与求和（demo 无法证明这一点）。剩下一个 pending 只能靠公式审计——为它伪造串口状态得不偿失，但报告里必须诚实标注"formula covered，非 runtime"。
- **Q：为什么 diagnosis cue 选在这一轮实现而不是再 DEFER？** A：因为 C4 是最后一个内容阶段，C5 只做 deploy/截图/人工。Phase 1 已把 cue 裁定为 NOW，如果 C4 不做，它就变成"无主设计项"——要么现在实现（成本一行文本 + 一个布尔读取），要么正式改判 DEFER 并说明原因。规则是：**设计决策的消失必须有解释**。
- **Q：新增 targeted geometry pass 时踩了什么坑？** A：demo 趟跟在最小尺寸趟后面，而 transition 只会"缩到最小"——结果 tag 写 1024×720、实际量的是 1000×700。修复是给测量步骤加"恢复默认尺寸"的能力。教训：**步骤标签是承诺，机器必须验证量到的尺寸和承诺一致**（本轮正是靠 dump 里的 page 高度发现）。


## 34. Post-T018 M9-C C5 条目（2026-09-18 追加）

- **Q：一张"广播批次"的 Dashboard 截图，怎么证明它不是误导性的？** A：三层证据合流——机器状态断言（expectedNoResponse=1、completed=1、hasSuccessRate=0、attention=0）在 grab 之前完成；分布条的 ENR 段宽度经 item 几何断言为整条；然后才是截图本身。截图证明"这个状态被正常呈现了"，断言证明"这个状态语义正确"——顺序不能反。
- **Q：可见性断言为什么会在别的页面上"误报"？** A：可见性是**相对于激活页**的契约。Dashboard 的 attention/cue 在 Legacy 激活时不可见，这是 StackLayout 常驻多页的正常表现，不是回归。教训：把契约函数应用到新场景前，先问"这条断言隐含的前置条件是什么"——本轮的修复是把 Legacy 趟的断言收窄到 Legacy 自己的契约。
- **Q：deploy checklist 和 module 自动部署的区别是什么？** A：xcopy 全树是**机制**，它保证文件跟着模块走；checklist 是**守卫**，它在部署完成的那一刻验证关键文件真的存在。机制正确时 checklist 是冗余的，但机制失效（比如未来有人改部署方式）时它是第一道报警——为 5 个承载 Dashboard 核心呈现的新组件补上条目，成本一行、收益是"空工作区"类回归在部署期就被拦截。


## 35. Post-T018 M9-C closure 条目（2026-09-18 追加）

- **Q：一个里程碑的"最后一个 behavior-bearing commit"凭什么当 LKGC？** A：凭两件事同时成立——①它的文件列表里有真实 product/test/harness/deploy 行为变更（`git show --name-only` 说话，不看 commit message）；②它的**完整仓库树**通过了全部门禁与人工验收。M9-C 的 `bc754be`（evidence harness + deploy checklist + 截图）满足两者；其后的三个 docs-only 提交不改变产品树，所以不能把它顶掉。
- **Q：working tree 里出现旋转副本，为什么不直接"修好再提交"？** A：因为逐像素比对证明旋转发生在 commit **之后**、仓库之外——capture 产物（committed blob）本来就是正确的 landscape。此时"修复 capture"是修一个不存在的缺陷，而"提交旋转副本"是把变异固化进历史。正确动作是：用 `git restore --source=HEAD` 把 working tree 恢复到已验证状态，把"哪个外部步骤旋转了文件"如实记录为 NOT IDENTIFIED。
- **Q：M9-C 之后，Dashboard 的信息架构还能再改吗？** A：能，但分层已经冻结——Dashboard=态势感知；attention=四计数的呈现聚合（非 health score）；distribution 分母=completed；successRate 分母=completed−expectedNoResponse；diagnosis cue=存在性线索。未来 M9-D 的 transaction 域、M10 的主动控制、M11 的寄存器读数各有自己的入口预算，Dashboard 不吞并它们。
- **Q：这轮 closure 里最重要的可复用纪律是什么？** A：**"证据分层 + 术语收紧"**。orientation 事件里我们把证据分成 capture output / committed blob / working-tree mutation 三层，每层独立验证；结论只写到证据能支撑的精度——已证明的清白逐条列出，未证明的变异步骤明确写 NOT IDENTIFIED。宁可结论窄而真，不可宽而假。


## 36. Post-T019 M9-D Phase 1 条目（2026-09-18 追加）

- **Q：为什么"选中的那一行"通常不该进 Controller？** A：selection 只有本页 detail 消费它，没有任何 backend 或其它 subsystem 需要它的权威引用——而 `ListView.currentIndex` 本身就是呈现态。把它写进 Controller 会造成页面状态污染事实层；跨页往返的保留需求由 StackLayout 常驻自然满足（与 B5 的 tab/草稿同机制）。
- **Q：为什么一条 transaction 的 issue 不能改写它的 status？** A：两者是正交轴。status 是事务**结论**（七值），issue 是确定性**观察细节**（响应侧 9 值 / 请求侧 4 值），`Success + InvalidRequestByteCount` 是合法组合。用 issue 改写 status 会把观察事实升级成结论——这正是 V1 冻结契约禁止的语义漂移。
- **Q：什么时候可以删掉一个页面？** A：当它**不再有任何独有能力**、且保留会造成**长期重复呈现**时；同时拆除过程必须满足三点——可回滚（单提交）、无能力真空（新承载先验证）、最小索引扰动（M9-D 用"替换 index 0"而不是"新增 index"，让 rail 形状与设备 disabled 位完全不变）。"终于能删"本身不是理由，证据才是。
- **Q：为什么不能为选中的 transaction 做个"原始报文"视图？** A：因为**数据根本不存在**：`TransactionAnalysis` 只保存 status/elapsed/exceptionCode/issue，没有任何字节；adapter 也不存。做出来只能是假报文。审计后端"确实有什么"这一步，往往比设计界面更早决定方案的边界。
- **Q：milestone 名字里有 Diagnosis，为什么这轮不动 Diagnosis 页？** A：因为 B5 刚以人工验收交付了它，而 milestone 名字不构成重构理由。M9-D 与 Diagnosis 的接触面被收窄到"一行存在性文本线索"，写在 Transactions 页——批次级 Baseline 语义因此不会被逐条选择污染。


## 37. Post-T019 M9-D D1 条目（2026-09-18 追加）

- **Q：新增一个 workspace 时，哪两处顺序必须同时改？** A：**导航栏条目顺序**和**StackLayout 子项顺序**——它们是同一份 index 契约的两个物理表示。D1 里我把新页插到诊断页之前，rail 顺序看着没问题，但 StackLayout 的 child 4 被空事务页抢占，"诊断站"立刻不可见。既有断言（`diagnosisPage visibility does not follow selection`）一次就把这个错位抓了出来——**这就是"先有护栏再搬家"的价值**。
- **Q：navigation entry 数为什么不能直接等于 geometry 趟数？** A：趟数只统计**可达（active）**的 workspace。D1 有两个 disabled 条目（事务/设备），它们是"入口占位"而不是"可测量页面"；给隐藏页写非零几何契约等于给未来的自己埋假失败。D1 的矩阵因此仍是 5×2=10，D2 启用事务后才是 6×2=12，D5 退役 Legacy 后回到 5×2=10。
- **Q：为什么页本地 selection 的"存续规则"要分三种情况？** A：因为"切页"与"换批次"是两种不同的事件。同一个 model/批次下切页往返，selection 只是呈现态，保留它符合用户预期；但 authoritative model 被替换（新批次/新来源成功加载）时，旧 selection 指向的行可能已不存在，**必须失效**；而"来源替换失败、model 未变"时数据没变，selection 反而**必须继续有效**。把三种情况写进契约，才能避免"切页就丢选中"或"换批次后 detail 指向幽灵行"两种错误。
- **Q：为什么不在 D1 顺手把表迁过来？** A：那会造成"已迁移但不可达"或"两个入口各有一份表"的中间态。项目已确立的纪律是**迁移与启位必须同提交**（B3/B4/B5 的原子性不变量），所以 D1 只放一个真正空的 shell，用 disabled 条目占位，D2 再一次性搬表并启用。


## 38. Post-T019 M9-D D2 条目（2026-09-18 追加）

- **Q：一次"搬页面"的迁移，为什么会在产品里留下真实布局回归？** A：因为页面里隐式存在"谁吃掉多余高度"的契约。事务 pane 是 Legacy 列里**唯一**带 `fillHeight` 的子项，它一走，列布局就把余量分摊到行内，统计块被推到 y=226 ——**迁移删掉的不只是 UI，还删掉了一个布局角色**。修法与 Dashboard C1 完全相同：给余量一个显式所有者，并加一条"统计块顶端 == 页面 margin"的常驻断言。
- **Q：怎么证明"只有一个呈现所有者"而不是嘴上说说？** A：在真实 item 树上走父子链——`underItem(pane, transactionsPage)` 必须为真、`underItem(pane, legacyWorkspace)` 必须为假，再配合全树同名校验（恰好 1 个）与"视图行数 == model 行数"。grep 源码只能证明"某处写了"，运行时父子链才能证明"树里真的只有一个"。
- **Q：为什么迁移时要补 objectName？** A：原实现只给了 `id`（QML 内部引用够用），但 `id` 不是可寻址的观测契约——harness 只能按 objectName 找。补的名字是**中性、稳定**的（`transactionsTableHeader`/`transactionsList`/`transactionsEmptyHint`），并把旧名 `legacyTransactionsPane` 换成 `transactionsPane`，避免在新页里长期挂一个语义错误的旧名。
- **Q：搬完页面却 segfault，最可能是什么？** A：新加的共享状态只声明、没赋值。本轮 `transactionsPtr` 只进了 lambda 的 capture 列表，stage 0 忘了捕获实例，第一次 `(*transactionsPtr)->isVisible()` 就崩了。教训：**指针型共享状态必须在同一处声明与赋值**（或统一写成一个 capture 回调），否则崩溃点离原因很远。
- **Q：为什么用同一批次在三处（demo/broadcast/protocol）验证"行呈现"?** A：因为三种状态分别代表三种语义边界：demo 证明多行 + 行高规则；broadcast 证明 `ExpectedNoResponse` 是**中性结局**（不是成功、不是超时、不是失败）；protocol 证明**确定性 issue 详情与 status 是两条正交轴**（详情存在但状态不被改写）。三个 fixture 都是仓库既有的 tracked 样本，没有为测试新造数据。

## 39. Post-T019 M9-D D3 条目（2026-09-18 追加）

- **Q：选中一条事务后，为什么不能直接读 `ListView.currentItem`？** A：因为 `ListView` 会**虚拟化并回收**代理项——滚出视口的行会被销毁，`currentItem` 随之为 null 或指向另一个 index；把 detail 绑到它，滚动就可能让详情"消失/错行"。本轮改为在**选中的那一刻**从屏幕上的代理项拷贝出一份 page-local 快照（`captureEntry`），之后 detail 只依赖快照，与代理项生命周期解耦。
- **Q：那快照会不会过期（stale detail）？** A：不会，而且这个结论是**从真实源码审计来的**，不是假设：`TransactionListModel` 只有 `setEntries()` 一个 mutation API，它走 `beginResetModel/endResetModel` **整批替换**，全模型**没有任何 `dataChanged`**，也没有 `rowsInserted/rowsRemoved`。也就是说"行内容在原地悄悄变化"这条路径**根本不存在**——唯一的变更就是整批替换，而整批替换必然 reset，reset 就清 selection。所以快照不可能滞后。
- **Q：为什么选 page-local 快照，而不是给 Controller 加一个 `selectedTransaction()`？** A：因为那会把**呈现状态升级成权威状态**，让 Controller 多出一个"当前选中事务"的概念，而它并不参与协议分析；而且这会成为任务明确列出的 STOP 条件。page-local 方案零后端改动、可回滚、且是唯一的 owner。
- **Q：selection 的"存续规则"为什么要分三种情况？** A："切页"和"换批次"是两种本质不同的事件。①切页（同一 model，只是隐藏/显示）→ 事实没变，选择应当**保留**；②成功的回放/批次替换 → model 整批 reset，旧行代表的那个事务已不一定存在 → **必须失效**，且**不能自动重选第 0 行**（那是伪装成 persistence 的撒谎行为）；③失败的回放替换 → `setEntries` 根本没被调用、model 没变 → 和①一样**保留**。这三条都由 Scenario P1–P5 在真实数据上断言过，不是文字承诺。
- **Q：怎么保证 detail 里的"状态"和"详情"不会互相污染？** A：两条都直接来自 model 的独立 role（`statusText` / `issueText`），detail **只做拷贝不做解释**——没有拿 issueText 反推 severity 或改写状态文本；测试里逐字段比对 7 个键与 model role 相等，并额外断言状态文本不包含详情文本（这条只是**辅助** negative oracle，真正依据是 role 来源本身）。
- **Q：键盘选择是怎么做的，自己写了状态机吗？** A：没有。`ListView.focus: true` 让 Qt 自己处理 Up/Down/Home/End，页面只监听 `onCurrentIndexChanged` 把它映射成 selection；鼠标是 delegate 根上的 `TapHandler` 设 `currentIndex`。两条输入因此**共轭到同一条** `selectRow` 路径，只有一份选中语义。诚实边界：本轮 harness 是直接设 `currentIndex` 驱动断言的（没有 Qt input synthesis），实机鼠标/键盘验证在 D6。
- **Q：为什么测试里单选一行要"延后一拍"（`Qt.callLater`）？** A：因为虚拟化下目标行可能还没被实例化，`itemAtIndex(row)` 返回 null。这时记下 `pendingSelectionRow`，用 `Qt.callLater` 在下一轮事件循环重试；若仍拿不到就**明确清空 selection**（宁可不显示，也不显示错的）。这避免了"选中了一个不存在的行却渲染出上一个事务的详情"。
- **Q：本轮踩到的坑里，哪一个最能说明 QML 的作用域陷阱？** A：delegate 里**嵌套子项**用 `ListView.view.currentIndex` 会报 `TypeError: Cannot read property 'currentIndex' of null`——`ListView.view` 这个 attached property 只挂在**委托根**上，子项上没有。修法是在委托根上定义 `rowSelected`，子项读 `parent.rowSelected`。另一个同源坑是 `onModelReset` 在启动期可能**早于** ListView 创建触发，此时 id 还是 null，必须加守卫。

## 40. Post-T019 M9-D D3 Review HOLD 条目（2026-09-18 追加）

- **Q：为什么"12 个 passes"和"16 个 passes"会同时出现在一个仓库里而不算 bug？** A：因为它们数的是**两个不同的东西**。`steps` 向量里 **12 个标准 step** = 6 个 active workspace × 2 个尺寸；而其中 **diagnosis 那 2 个 step 各自会 sweep 三个 tab**（代码里 `kDiagnosisTabs = 3`），所以**打印出来的 `GEOMETRY [...]` 段落是 20 段**（10 + 6 + 2 + 2）。**step 数（16 = 12+2+2）是覆盖度口径，打印段数是测量次数口径**。真正的教训不是"哪个数字对"，而是**报告里写下的数字必须能被命令输出逐条对上**——我当时的 "14" 既不是 16 也不是 20，是纯粹的加法错误。
- **Q：`Qt.callLater(page.applyPendingSelection)` 到底捕获了什么？** A：**什么都没捕获**。传进去的是**函数引用**，不是 `() => applyPendingSelection(row)` 这样的闭包，所以行号从来没有被"记"在回调里；回调第一行 `const row = pendingSelectionRow;` 是**执行时现读**。这是整个 deferred 设计能安全的前提——**deferred 完成体不能在创建时固化一个可能过期的值**。
- **Q：modelReset 之后，那个还在队列里的回调怎么就不会复活一个旧选择？** A：reset handler 把 `pendingSelectionRow` 清成 `-1`，回调读到 `-1` 就直接 `return`，**一个字段都不写**。关键在于它**没有别的地方能拿到旧行号**——如果实现写成了捕获闭包，reset 清 `pendingSelectionRow` 就完全无效，因为回调根本不读它。这正是我用 mutation probe 复现出来的那条 `GEOFAIL: a STALE deferred selection resurrected into the replacement model (selectedRow=0)`。
- **Q：新的选择已经产生，旧的 deferred 回调还会不会把它覆盖掉？** A：不会。若新选择**能立即完成**，`selectRow` 会把 `pendingSelectionRow` 置 `-1`，旧回调读到 `-1` 直接 no-op；若新选择**同样被 park**，`pendingSelectionRow` 被**覆写为最新行号**（而且 Qt 对**同一个函数**的多次 `callLater` 会**合并成一次**调用），于是回调读到的**就是最新请求**——结果只可能是**最新行**或**显式 no-selection**，永远回不到旧行。Scenario Q2 断言的就是这条：越界行 99 park 之后，同 turn 选中真实行 2，回调执行后**仍然是行 2**。
- **Q：为什么 `row < 0` 一条判断就能同时证明"仍属当前 model"和"仍属当前请求"？** A：因为它依赖一条**已被审计证明**的不变式：`pendingSelectionRow ≥ 0` **当且仅当**存在一个尚未被消费的**最新**请求、且**自该请求以来没有发生过 model reset**。前半句由"只有 `selectRow` 会写入非负值"保证，后半句由"reset 是模型的唯一变更路径、且它必然清 pending"保证（`TransactionListModel` 只有 `setEntries()` 一个 mutation API，走 `beginResetModel/endResetModel`，全模型没有 `dataChanged`）。所以**不需要 generation token**——token 只是把这条不变式换一种写法，并不会让它更真。
- **Q：什么情况下我真的需要 generation / modelGeneration？** A：当权威数据存在**不触发整模型 reset 的就地变更**时（例如逐行 `dataChanged`、或 model 被**换成一个新实例**而 selection 仍被保留）。那时"pending 非负"就不再蕴含"数据没变过"，就必须显式比较**代次**或**模型身份**。本项目当前**没有**这条路径，所以加了也只是形式主义——但这条判据必须写下来，否则将来模型引入就地更新时，这个"看起来还成立"的论证会静默失效。
- **Q：怎样证明一个并发/延迟安全的测试不是"永远绿"的空测试？** A：两条：①**反空洞断言**——先断言**真的进入了那条路径**（我断言 park 后同 turn `pendingSelectionRow == 0`，若委托已实例化则该断言失败并直接写明 "this scenario would be vacuous"）；②**mutation probe**——把实现改成**缺陷版**，确认测试**变红**并给出**预期的失败信息**，再回滚。只做 ① 不够：能进入路径不等于能抓住缺陷。
- **Q：哪些边界必须诚实地标为"未覆盖"，而不是用文字论证顶上去？** A：① **pending→pending** 变体：两次连续不可满足请求且无 reset，最终结果（no-selection）与 stale capture 实现**完全一致**，**不可区分 ⇒ 没有判别力**，写测试只会得到一条永远绿的假证据；②真实的 park 场景由**键盘移到未实例化的委托**产生，而 4 行批次里每个委托都已物化，所以本 harness 是**从页面自身的 `selectRow` 进入**同一代码路径，**不是键盘驱动**；③鼠标/键盘**实机**交互仍在 D6。这三条都写在 Scenario Q 的输出与 T019 §R4 里，而不是埋在"已覆盖"的表述下。

## 41. Post-T019 M9-D D4 条目（2026-09-18 追加）

- **Q：一个"是否有诊断结果"的提示，为什么值得单独一个阶段？** A：因为它的风险不在"画一行字"，而在**状态来源**。如果让页面自己判断（比如"baseline 文本是否为空""当前选中行有没有异常"），就会出现第二套 diagnosis 事实、或把 session 级结论绑到单条事务上——两者都是语义事故。D4 的全部工作量几乎都在**证明 cue 只读 Controller 的既有 `hasBaselineDiagnosis`**，并且用 Scenario R 证明它对 selection、批次替换、导航、clearDiagnosis 的行为都由 Controller 语义决定，页面零自实现逻辑。
- **Q：为什么可见性规则是 `observedCount > 0` 而不是"永远显示"？** A：复用 Dashboard cue 的同一规则。空会话时 Transactions 页已经有自己的空态提示（「暂无通信记录」/「选择一条事务查看详情」），再叠一行"尚未运行基线诊断"是噪音；而且"session 存在"本来就是 `observedCount` 表达的事实——**两个 cue 用同一把尺子**，将来任何一页改规则都有先例可查。
- **Q：为什么 cue 放在页面最底部，而不是放在表头附近让用户第一眼看到？** A：Phase 1 的用户任务排序里，Transactions 的主任务是 **evidence inspection**（看记录、看详情），Diagnosis 是 **session interpretation**。存在性 cue 是**低层级辅助信息**：放主视图中心会暗示"你应该去诊断"，越俎代庖；放在 detail 之后既保持可发现（无 session 时隐藏，有 session 时常驻一行），又不与列表抢空间。1000×700 下列表仍有 437px（≥216 预算两倍），空态列表高度与 D3 **逐值相同**——隐藏的 cue 在 ColumnLayout 里不参与空间分配。
- **Q：新批次发布后 cue 自动翻回"尚未运行基线诊断"，是谁做的？** A：**Controller，不是页面**。`setEntries` 的每个发布点都会调 `invalidateAiForBatchChange()` → `clearDiagnosisState()` → `diagnosisChanged`，QML 的绑定自然重读 `hasBaselineDiagnosis`。页面上**没有**任何 `modelReset → 清 cue` 的代码——这一点是刻意留空的：如果页面自己实现 invalidation，就会出现"页面认为该清、Controller 认为不清"的第二套真相。R13 专门断言了这条链路。
- **Q：`clearDiagnosis` 之后怎么证明"只清了诊断"？** A：R5 在执行前后对 rows（仍 4）、`observedCount`（仍 4）、selection（仍 row 2）、以及 17 个 Controller 快照字段做了稳定断言——这与 B5 阶段冻结的 `clearDiagnosis` 注释语义（"the batch, rows, statistics and source all stay; the batch revision does NOT change"）逐条对应。**页面没有为 Agent 或 AI 增加任何对称清理**。
- **Q：这个 cue 和"选中行的诊断"是什么关系？** A：**没有关系，而且是刻意没有**。Baseline diagnosis 是 batch/session 级的（消费全批事务 + revision）；cue 回答的是"当前 session 是否已有解释"。点一行永远不会改变它（R3），它也永远不会因为选中行的 outcome（ENR/协议错误/需关注）而改变措辞（不同轴）。真正的 single-transaction diagnosis 需要 Core/Controller 新能力，Phase 1 明确 DEFER 到新 milestone。
- **Q：为什么最终没有做任何 polish？** A：因为 polish 的四类触发条件（cue 引发 spacing defect、D3 选中态 visual collision、detail/cue 层级不清、1000×700 真实拥挤）在取证后**全部不存在**：空态几何逐值不变、批态只占 12px 自然高度、层级是"detail=行级字段 / cue=session 级存在性"、列表余量 221px。规程里写明"无真实 defect 时报告 No additional polish required 是完全可接受结果"——**不为了阶段名义凑工作量**，这本身就是 scope 纪律的一部分。
- **Q：为什么 cue 的文案要与 Dashboard 逐字相同，却不抽一个共享组件？** A：共享的是**语义边界**（两态措辞、可见性规则、只读 authority），不是**呈现结构**——两页的布局上下文完全不同（Dashboard 在 Session Overview 列里，Transactions 在表格+详情之下）。抽组件的门槛是"出现两个真实消费者的结构复用需求"，而这里重复的只有 6 行属性绑定，抽出来只会让两页被一个不该共有的壳绑住。字符串可以相同，结构不必相同。

## 42. Post-T019 M9-D D5 条目（2026-09-18 追加）

- **Q：为什么"退休"必须以"运行树里根本没有这个对象"来定义，而不是 `visible: false`？** A：因为 `visible:false` 是伪装——对象还在树上，还能接收绑定更新、还能被 objectName 查到、它的私有状态还在消耗注意力。D5 用**四重 absence oracle**（geometry 每趟 / shell-nav 每次 / runNavAssertions 每次 / Scenario S）断言 `legacyWorkspace`、`statisticsOverview_legacy`、`legacyTailSpacer` 在运行树 **count = 0**，连 `workspaceLegacyIndex` 这个 property 都要在 **metaObject 层面**证明不存在（`indexOfProperty == -1`）——读一个不存在的 property 会返回 0，恰好和"legacy 在 0 号位"撞车，所以不能用值断言。
- **Q：Phase 1 说 legacy 引用有 29 处，为什么你数出来 144 行？** A：29 是 **Phase 1 时点**的计数（D1–D4 又新增了大量含 legacy 字样的断言、注释与场景）。规约要求"以当前 HEAD 实搜，数量变了就报真实数"——所以 D5 的分类表按 144 行逐行归类，而不是硬凑 29。教训是：**量化基线是快照，不是契约**；复用时必须重新取证。
- **Q：怎么做到"不 blind replace"？举一个具体例子。** A：场景里的 `switchTo(0)` 原意是"去 Legacy 工作台证明业务状态不变"。D5 后 index 0 是 Transactions——**调用点一个都没改**，因为 station 的语义本来就是"去另一个工作台再回来看数据"，Transactions 完全满足；改的只有 context 标签（`@legacy`→`@transactions`）和两条汇总文案，**断言逐字未动**。反过来，8 处 `switchTo(5)`（旧 Transactions）必须全部改成 `switchTo(0)`，否则就会激活 disabled 的 Device 条目。替换前先问"这处引用的语义是什么"，而不是"这个字符串在哪里"。
- **Q：为什么 basic path 里的站点可以换，而 I/K/K' 的 Replay、L 的 Diagnosis persistence、N 的 Agent draft 语义不能动？** A：因为**站点是呈现路由，断言是业务契约**。前者换工作台不改变要证明的事实（快照比对在任何工作台都成立）；后者绑定的是具体页面的页本地状态或权威数据流（回放 source、baseline、Agent 草稿），换了就测不到原来的东西。D5 的分类表把这两类显式分开（C 类可 retarget，E 类必须 remove+replacement）。
- **Q：`legacyPtr` 这种"测试专用的页面指针"为什么要删干净？** A：留着它就是给未来埋雷：指针永远指向一个不再存在的对象，只有"代码路径不走到那里"才不出事——这正是 §24 点名的反模式。D5 把它从共享状态、lambda 捕获、identity 断言和日志里全部移除（`grep -c legacyPtr` = 0），identity 证明只对**仍然存在**的五个页面做。
- **Q：StatisticsOverview 一个消费者都不剩了，为什么文件还要留？** A：因为"component 的去留"和"workspace 的去留"是两个决策。D5 的授权是退休 workspace；component 是一个有完整契约（required properties、instanceId 命名、隐式尺寸规则、ISSUE-012 修复）的抽象，删它属于另一个变更，需要自己的学习/评审。现在它处于 **zero-consumer 但保留** 状态：文件、CMake registration、deploy guard 都原样（CMake/deploy zero diff），留给专门的 ownership decision。同时，"Legacy 统计几何"这类**绑定在该实例上的呈现 oracle** 随实例一起退役——统计的**业务语义**仍由 Controller/Core 测试和 Dashboard 回归覆盖，没有测试能力净损失。
- **Q：默认工作台从 Legacy 变成 Transactions，怎么证明这只是"展示"变了而不是"行为"变了？** A：三件证据：①smoke test 本身就是冷启动路径，EXIT=0，启动后 `rowCount=0`、source/mode 保持真实初始值——没有自动 runDemo/loadReplay/connect/clear；②Scenario S1 从 stage 0 捕获的 `startupIndex` 证明启动选中的就是 index 0；③S6 的五工作台往返断言 16 字段权威快照逐值不变——Transactions 作为工作台只**呈现** authoritative state，自己不是任何事实的来源。
- **Q：这轮最险的一步是什么？** A：**两套 index 映射的同步**。产品里 index 契约集中声明在 Main.qml，但 harness 里有三处独立的 `pageIndex → property key` 映射（geometry 的 switchWorkspace、nav 的 switchTo、evidence 的 switchTo）加 8 处硬编码 `switchTo(5)`。D1 就发生过 child-order/index mismatch，所以 D5 先把"最终 index 契约"写成表（§D5.4），再逐处迁移，并且让 RED 先证明新契约断言真的会在旧树上失败——而不是等绿了以后才发现某处映射漏改、被"恰好没走到"掩盖。

## 43. Post-T019 M9-D D6 条目（2026-09-19 追加）

- **Q：部署版跑自动化门禁时报 `STATUS_DLL_INIT_FAILED`，为什么不是部署坏了？** A：因为我给验证脚本加了 `QT_QPA_PLATFORM=offscreen`（build-tree 跑 harness 一直这么做），而 **deploy checklist 里没有 qoffscreen 平台插件**——windeployqt 默认只装 qwindows。Qt 找不到指定平台插件就报 0xC0000142。M9-C 的部署验证脚本本来就不设这个变量（真实 windows 平台在桌面会话里同样可自动化）。教训：**offscreen 是开发机便利，不是部署契约的一部分**；部署验证必须复刻用户真实的启动面。
- **Q：为什么每张截图 grab 前都要做一遍机器断言？** A：因为截图是"状态的像"，不是状态本身。文件名（`...empty...`、`...broadcast...`）只是标签，**永远不能当 state oracle**——M9-C 的 orientation 事件已经证明"文件说的"和"实际内容"可能不一致。所以每张图 grab 前先断言 workspaceIndex、navItem、rowCount、selectedRow、detail 逐字段、hasBaselineDiagnosis、status/issue——断言全过才 grabWindow。这样人工看图时，图的**状态前提**已经被机器锁死，人只需要判断"这个状态下长得对不对"。
- **Q：logical 1024×720、pixel 1280×900，这算撒谎吗？** A：不算，这是 **125% DPI** 的正常映射：逻辑坐标是 Qt 布局的几何事实，像素是屏幕上实际渲染的事实。M9-C 的 RCA 立的规矩是**两个都要记录、双向核对**：断言"logical 横向 ⇒ pixel 横向"（旋转会破坏这个蕴含，而简单缩放不会），而不是弱断言"pixel ≥ logical"。DPI 值不写死——环境变了报告真实值。
- **Q：integrity 检查（landscape/nonblank/distinct）都过了，为什么不直接宣布视觉 PASS？** A：因为 **integrity ≠ visual correctness**。像素统计能证明"图是有效、横向、有内容、彼此不同"，不能证明"选中行看起来克制""ENR 不像错误"。后者是人的判断——这正是 Manual Visual Review 存在的意义。机器把状态和完整性锁死，把审美和语义可读性留给用户。
- **Q：evidence 里怎么建立"选中第 2 行"？这能代表用户操作吗？** A：不能代表，而且我明确申报了。evidence 用 harness seam（`page.selectRow(2)` / 设 `currentIndex`）建立状态——它和真实点击/键盘走的是**同一条选中语义路径**（D3/Q 已机器验证），但**不产生输入事件**，所以不证明物理交互。物理鼠标点击和 Up/Down/Home/End 留给 Manual Interaction Review 的第 3/4 步。报告里不写"键盘 PASS"。
- **Q：StatisticsOverview 一个实例都没有，为什么部署清单里必须还有它？** A：因为 **runtime instance = 0** 和 **component packaged = present** 是两个独立事实。前者是 D5 的 retirement oracle 证明的运行态；后者是打包完整性——组件还在 QML module 里注册着（zero-consumer 但保留的决策），部署清单漏了它才说明打包坏了。混淆这两者会导致"没人用就顺手删"的滑坡。
- **Q：M9-C 的 `m9c-legacy-regression.png` 里是已退休的 Legacy 工作台，为什么不删掉？** A：因为 docs/assets/screenshots 是**历史 evidence 档案区**（只增不改原则）。那张图证明的是"M9-C 时点的 Legacy 统计在 D2 提走事务后原样"——它作为当时的历史证据仍然真实有效。D6 的新集不含它， retirement 后的证据由 `m9d-*` 集承担。删历史图只会破坏证据链的时间完整性。
- **Q：D6 改了 main.cpp，这算产品变更吗？** A：算 behavior-bearing（harness 行为变了），但**不算产品 UI 变更**——diff 只在 `--qml-evidence-capture` 的阶段表里，产品 QML 全部 zero diff。Git 分类按真实文件清单：D6 = evidence harness + 新 PNG + docs；M9-D Final Closure 时 LKGC 的裁定也按 `git show --name-only` 来，不按 commit message 猜。

## 44. Post-T019 M9-D 键盘导航 correction 条目（2026-09-19 追加）

- **Q：鼠标选中明明正常，为什么键盘全死了？** A：因为**鼠标点击和键盘焦点在 Qt Quick 里是两条独立的路**。`TapHandler` 和 `MouseArea` 都是 pointer 处理器，**不抓键盘焦点**；真正夺焦点的是 Control（Button 等，focusPolicy=StrongFocus）。用户先点了 Run Demo（AppButton 拿到 activeFocus），再点 rail（MouseArea，焦点不动），再点行（TapHandler，焦点还是不动）——于是 activeFocusItem 一直是**那个已经隐藏的 Run Demo 按钮**，四个键全投给它。焦点审计原话：`activeFocusItem=dashboardRunDemo (AppButton)`，此时 `list.focus=0`（同 FocusScope 内 focus 标志被按钮夺走后不会自己回来）。
- **Q：为什么修复是"先设 currentIndex 再 forceActiveFocus"？顺序真的重要吗？** A：实测重要。QQuickItemView 在**自己持有 activeFocus 时**会把 activeFocus 交给 current delegate——我先调 forceActiveFocus（list 拿到焦点）再设 currentIndex，焦点立刻被推给 delegate（审计：`activeFocusItem=QQuickRectangle`，普通 Item 的默认 key 处理会吞键）。改成先设 currentIndex 再 forceActiveFocus，终态就是 list 持焦。§5 预写了"注意顺序需实测"，就是防这个。
- **Q：Up/Down 是 Qt 原生的，Home/End 呢？** A：实测（QKeyEvent 直接投给 ListView）：Up 2→1 ✓、Down 1→2 ✓ 是原生的；**Home=2、End=2——QQuickItemView 根本没实现这两键**。所以只补了 Home/End：视图层 `Keys.onPressed` 里 Home→0、End→count-1（count>0 守卫）。这正好落在规程预判的 Case B："ListView 支持 Up/Down 但不支持 Home/End"。
- **Q：delegate 为什么会持有 activeFocus？Keys.forwardTo 是干什么的？** A：QQuickItemView 在视图持焦时会把 active focus 交给 current delegate（让内嵌编辑器之类能工作的设计）。但 delegate 是普通 Item，它的默认 key 处理会把事件吃掉，事件到不了视图。`Keys.forwardTo: [transactionList]` 让投给 delegate 的按键**先**交给视图处理——Up/Down 走原生导航，Home/End 走视图自己的 handler，全部汇入同一条 `onCurrentIndexChanged → selectRow` 路径。
- **Q：自动化怎么证明"物理键盘路径"？** A：两层：①**真实鼠标点击合成**——QMouseEvent 经 window 投递，走真实 pick→handler 路径，MouseArea/TapHandler/Control 的焦点行为与物理点击一致（复现出与人工完全相同的坏焦点状态）；②**真实 QKeyEvent 合成**——sendEvent 到 activeFocusItem，走 QQuickItem::keyPressEvent → ItemView 导航的**真实按键处理路径**。但要诚实：这不是 OS 级物理按键（没有硬件事件、没有窗口管理器层），所以最终确认仍是用户手测。规程原话："automated physical key path 能力边界必须申报"。
- **Q：这轮的 harness 教训是什么？** A：三条，都值得记。①`selectRow()` 不设 currentIndex——它是页本地 presentation 入口，而 currentIndex 才是 Qt 导航的权威输入；鼠标路径靠 TapHandler→currentIndex 耦合，我第一版 probe 用 selectRow 建状态，造出"Up 从 -1 出发没反应"的假象。②`QMetaObject::invokeMethod` 调 `itemAtIndex` 必须 `Q_RETURN_ARG(QQuickItem*)` + `Q_ARG(int)`——返回/参数类型不匹配会**静默失败**，害我把"调用失败"误诊成"delegate 未物化"。③`Keys` attached property 没有 onHomePressed/onEndPressed（只有 Up/Down/Left/Right/Return/Enter/Space/Escape 等），Home/End 要用通用 onPressed 判断 event.key。
- **Q：修复后为什么不用重做 Screenshot Visual Review？** A：因为本轮 QML diff 只有 focus/Keys 行为，没有任何布局/颜色/字形变化——并且给出了像素级证据：修复后部署版重摄的 7 张图与已通过视觉审查的提交版**灰度差 max delta = 1/255、差值 >8 的像素数为 0**（纯渲染噪声）。规程写明"如产生可见 UI diff 才 STOP"；这里用数据证明了"不可见"。顺带把 7 张图刷新为修复后 binary 的重摄件，让 evidence 的 provenance 与最终 candidate 严格对齐。

## 45. Post-T019 M9-D Final Closure 条目（2026-09-19 追加）

- **Q：verified LKGC 为什么落在 `07561d9` 而不是 D6 的 `d28e1cb` 或更早的 D5？** A：规则是"最后一个 **behavior-bearing** 且其**完整树**通过了全部验证与人工验收的提交"。D5 的 `3c1bc3f` 之后 D6 又改了 `src/main.cpp`（evidence harness）和截图——`d28e1cb` 之后键盘修复又改了**产品 QML**（`TransactionsPage.qml`）和 harness——所以 D5/D6-candidate 的树都不是"最终被验收的产品"。`07561d9` 的完整树 = 最终重新部署的 candidate + 键盘修复后的产品 + 刷新后的 evidence + 用户人工验收的树，且部署版三模式（含 Scenario T）在这棵树上重跑全 0。之后的 closure commit 是 docs-only，按规则不得成为 LKGC。
- **Q：LKGC 裁定为什么要看 `git show --name-only` 而不是 commit message？** A：因为 message 是人写的摘要，文件列表才是事实。本轮分类里最有说服力的例子是 `ce57d9a`（D1）：message 看起来只是"shell and navigation contract"，但真实文件列表里还有 **`CMakeLists.txt` 和 `scripts/deploy_windows.bat`**（deploy checklist 注册）——不查文件就会把 deploy 相关行为变更漏分类。九个 M9-D 提交逐一按真实列表归类：1 个 docs-only + 8 个 behavior-bearing。
- **Q：这次 closure 里做了哪些"口径修正"？为什么要修正？** A：两处。①截图长宽比此前被写成"16:9"——但 1000×700 的图是 10:7（逻辑）与 1250×875（像素），不是 16:9；正确表述是 **landscape**（logical 宽 > 高 ⇒ pixel 宽 > 高），并配对记录 1024×720→1280×900、1000×700→1250×875（125% DPI）。②截图数量——**D6 新提交 7 张**与 capture 集 **8 张**（多出一张未提交的 pre-existing regression frame）是两个口径，必须分开写，否则证据清点会对不上。历史表述不删除，只在新记录里给出正确口径。
- **Q：Manual Interaction 的验收边界怎么划，才不虚报？** A：只记录用户**实际确认**的项目（mouse/Up/Down/Home/End/首末边界/导航往返+再点击+键盘），没执行的写"NOT manually exercised"（failed Replay replacement 由自动化 P3 覆盖）。Tab-only focus traversal 明确为 **NOT REQUIRED for M9-D**——它不是失败，是登记到 M9-F 的全局 accessibility/focus-chain audit。验收记录里"没做"和"做了但没通过"是两种完全不同的事实，必须分开写。
- **Q：M9-D 收尾时最值得带走的三条冻结契约是什么？** A：①**selection 是页本地呈现状态**——reset 失效、失败保留、stale deferred callback 不得跨 reset 复活（冻结的是外部行为，`pendingSelectionRow` 的字段形状不是 public API）；②**键盘四键契约**——Up/Down/Home/End 全部汇入 `currentIndex → onCurrentIndexChanged → selectRow → detail` 单一路径，不越界，highlight 与 detail 同步；③**Diagnosis 与 selection 是两个轴**——Transactions 只有非命令式存在性 cue，baseline 是 batch 级的，选行永远不驱动 single-transaction diagnosis（那需要新 milestone 的新能力）。
- **Q：StatisticsOverview 现在零消费者，为什么 closure 不顺手宣布"已删除"？** A：因为它**没有被删除**——文件、QML registration、deploy guard 都还在，M9-D 只是让它的 runtime instance 归零。把"zero-consumer but kept"误写成"dead-code removed"会误导下一个接手的人。它的去留是独立的 ownership decision，已登记留档。
- **Q：M9-D 全程走完，最通用的一条工程教训是什么？** A：**"自动测试绿"和"用户手上的产品好用"之间隔着一类只能靠真实输入路径填补的证据**。D3–D5 的 harness 一直直接设 `currentIndex`，每个状态断言都绿；直到 D6 用户一上手，才发现键盘焦点从未落在列表上。本轮的补救是把真实点击合成与真实 QKeyEvent 合成做成常驻回归（Scenario T），并把"harness 状态注入 ≠ 物理交互"写进每个相关文档的边界声明。以后任何 UI 交互功能，都应在自动化里尽量走到真实事件路径，并给物理交互留出明确的 manual 验收项。

## 46. Post-T020 M9-E Phase 1 条目（2026-09-19 追加）

- **Q：window icon 和 PE executable icon 为什么不是同一件事？** A：读取者和时机完全不同。window icon 是**运行时**的——QGuiApplication/窗口把它交给 Windows shell，画在 titlebar/taskbar/Alt-Tab，来源可以是 qrc 里的图；PE icon 是**编译时**嵌进 exe 资源段（.rc → RT_GROUP_ICON）的，Explorer 和安装器在**不运行程序**时读取它来显示文件图标。前者缺失 → 跑起来难看；后者缺失 → 文件本身是"白板"图标。机制（qrc vs .rc）、验证（窗口取证 vs PE inspection）、缺失后果三者都不同，所以 M9-E 把它们列为两条独立交付。
- **Q：milestone 名字里有 Packaging，为什么可以不做 installer？** A：因为"packaging"的定义是**产出可交付的本地 artifact**，而当前真实 baseline（deploy_windows.bat + windeployqt + checklist）产出的 portable folder **已经是**可交付形态。installer 解决的是安装/卸载/快捷方式/注册表/升级这些**分发体验**问题——本项目一个需求信号都没有（§20 逐条否决）。为名字对齐就造 installer，是用 scope 膨胀换"听起来专业"。裁定 zip NOW / installer DEFER，触发条件写实。
- **Q：版本号现在有几个来源？** A：两个，这是个隐患：CMake `project(VERSION 0.1.0)` 和 main.cpp 里硬编码的 `setApplicationVersion("0.1.0")`——目前**巧合一致**，没有任何机制保证下次改版本时两处同步。Phase 1 裁定收敛为 CMake 单源（configure_file 生成版本头/.rc 输入），并在 PE VERSIONINFO 里体现。
- **Q：为什么 v1.0.0 不能跟着开发往前走？** A：它是 V1 完成时点的历史锚（annotated tag object `2cee626` 指向 `ae067ab`），所有"V1 与 V2 对比"的追溯都以它为基准；移动它 = 篡改历史，和 Git 政策"不 rewrite 历史"是同一条纪律。新版本号应该是**新增** tag——而那属于 publication 决策（需要显式授权），不属于 M9-E。
- **Q：为什么 packaging candidate 不能顺手继续用 Debug？** A：因为 **Debug accepted ≠ Release accepted**：Qt runtime DLL 不同、断言与优化行为不同、体积差数倍；此前所有"部署版验证"验证的都是 Debug 二进制。如果把 debug-local deploy 直接当交付物，等于交付了一个**从未跑过任何门禁的构建配置**。Phase 1 裁定 Release candidate + 全部关键门禁在 Release 上重跑。
- **Q：截图为什么证明不了 PE metadata？** A：截图拍的是运行中窗口的内容，而 PE icon/VERSIONINFO 是 exe 文件里的资源段，**不运行程序**也能被 Explorer 读取。层级不同（window runtime vs file resource）、读取者不同（shell session vs 文件解析）、验证工具必然不同（窗口取证 vs .rsrc 段解析/资源浏览器）。用截图冒充 PE 证据，就是用错误的 oracle 假装验证。
- **Q：StatisticsOverview 现在零消费者，为什么 closure 和 M9-E 都不删它？** A：M9-D closure 把它定性为"zero-consumer **but kept**，去留另行 ownership decision"——这是一个**待决事项**，不是垃圾。M9-E 的授权范围是 branding/icon/packaging，删组件会同时触碰 QML module contract 和 deploy checklist 两侧，是另一类变更。两轮都拒绝"顺手删"，正是 scope 纪律的体现；它已登记留档，不会丢。
- **Q：为什么 package 必须有 credential negative scan？** A：AI/Agent 功能让开发机上存在 provider token（T011 设计：token 在运行环境，不在仓库）。打包本质是"目录快照"，最容易把开发机局部状态（配置、日志、绝对路径、缓存）一起打进去。`aiConfigured=true` 恰恰是警示：运行时有 token，不代表包里有，但**必须用机器扫描证明**包里没有——这是唯一可复核的防线，靠"我记得没放"不算证据。

## 47. Post-T020 M9-E Phase 1 correction 条目（2026-09-19 追加）

- **Q：CMake 里写着 0.1.0，tag 打着 v1.0.0——到底哪个是"真"版本？** A：两个都是真事实，但**语义不同**：`v1.0.0`（annotated "verified product baseline"）是 V1 主线完成时的**正式发布版本锚点**；CMake 的 `VERSION 0.1.0` 是 **T001 项目引导时敲下的默认值**，git 历史证明它从 2026-09-05 引入后**一次都没改过**——连 v1.0.0 打 tag 的那个 commit 里它都还是 0.1.0。所以它不是"另一个版本语义"，是 **stale 值**。本轮的结论是：机制上 CMake VERSION 应当成为唯一版本权威，但**它的当前值与历史发布锚矛盾**，把值改成什么（1.0.x？2.x？）是用户的 release decision，不由设计阶段推断。
- **Q：既然裁定 CMake 是唯一权威，为什么不顺手把 0.1.0 改成 1.0.0？** A：两个理由。①改值就是 release decision：版本号表达的是"这次交付算什么"，1.0.1/1.1.0/2.0.0 各自传达不同的兼容性承诺——这是产品决策，不是技术修复；②本 correction 是 docs-only，规程明确禁止 bump。所以 Policy 先行（机制、派生关系、映射冻结），Value 保持 UNRESOLVED 并显式入档——tag 与 CMake 的 mismatch 被写成**已知开放事实**，而不是被悄悄掩盖。
- **Q："deterministic zip" 错在哪？** A：我把"脚本生成"误当成了"字节可复现"。`Compress-Archive` 会写入时间戳、且条目顺序/元数据不受控——同一个树跑两次可能得到不同 SHA256。要叫 byte-reproducible 必须满足四条契约（stable entry ordering、normalized timestamps、no machine-specific metadata、two-run SHA256 equality），当前工具链一条都保证不了。修正：现在的产物叫 **scripted portable ZIP**（A 案），byte-reproducible 作为后续增强单独立项验证。教训：**术语要跟验证能力走，不能跟愿望走**。
- **Q：icon 的 source 和 derived 为什么要这样分层？normal build 为什么不能依赖 ImageMagick？** A：`icon.svg` 是唯一手写母版（source），`ModbusLens.ico` 是从它生成的派生资产（derived）——但 derived 必须**入库**，否则任何人 clone 后没装 ImageMagick/icotool 就构建不了，违反"仓库可独立构建"的底线。工具只在 **E2 生成步**用一次（先探测、记录 tool+version+固定命令、验证 ICO 内含尺寸、禁 silent fallback），日常构建只消费已入库的 .ico。临时 PNG/previews 只允许在 build/ 里，防止生成垃圾进 assets。
- **Q：LICENSE 文件不存在，为什么 M9-E 不直接写一个 MIT 进去？** A：选择 license 是**法律决策**（ MIT/Apache/专有 各自意味着不同的权利授予与责任），一个简历/作品集仓库选哪个 license 完全是作者的战略选择，AI 不该替他签。审计结论是"missing"，处置是"不生成、留待用户"；同时 package 里的 readme/notes 可以谈使用说明，但**不能替代**法律 license。同理 CompanyName/LegalCopyright 继续留空——没有权威主体信息时，编造一个比留空糟糕得多。
- **Q：为什么把 installer/signing 的 REJECT 措辞改掉？** A：因为 REJECT 会被读成"永久否决"，而真实裁定是**范围性推迟**：installer 等到有卸载/快捷方式/升级需求时重启；signing 等到有证书和 release workflow 时做。措辞统一成 **DEFERRED / NOT IN M9-E IMPLEMENTATION**，把"本轮不做"和"永远不做"区分开——文档措辞的歧义会变成未来的错误决策依据。

## 48. Post-T020 M9-E Version Decision 条目（2026-09-19 追加）

- **Q：为什么 2.0.0 而不是 1.0.1？** A：这是**产品语义选择，不是数学题**。v1.0.0 是 V1 完成锚；此后 V2 完成了整个 UI/UX 重构（应用壳、全部工作台重设计、Legacy 退役、新工作区、键盘契约）——这是对用户可见形态的**代际变更**，不是补丁。1.0.1 会传达"小修小补"，与事实不符。但关键是：**这个判断属于产品所有者**，AI 只能陈列证据（Phase 1 correction 就是这么做的）；本轮由用户显式拍板 2.0.0，决策链完整。
- **Q：版本决策为什么不顺带把 tag 也打了？** A：因为 **version decision ≠ publication**。决定"下一个版本号是什么"是产品语义；创建 `v2.0.0` tag 是发布动作——它意味着"这棵树就是 2.0.0 交付物"，而 E1–E4 还没跑、2.0.0 的二进制还不存在。规程明确：tag 创建是独立的 release/publication gate，E1–E4 全程禁止自动打 tag。否则会出现"tag 先行、代码追认"的倒挂。
- **Q：CMake VERSION 从 0.1.0 改成 2.0.0，中间跳过了 1.x，git 历史上会有问题吗？** A：没有。语义化版本比较的是**相邻发布之间的关系**，不是数字连续性；2.0.0 相对 v1.0.0 是"重大变更"，正符合 V2 的实际内容。git/tag 层面唯一要保证的是：v1.0.0 这个历史锚不动（它记录的是"V1 在那时是 1.0.0"这个事实），新版本以**新 tag** 记录——而且那要在 E4 之后、由 publication gate 决定。
- **Q：为什么 applicationVersion、PE、包名都要"派生"而不允许各自写死？** A：D6 之前仓库就吃过**双源**的暗亏：CMake 写 0.1.0、main.cpp 硬编码 0.1.0，靠"碰巧有人记得同时改"保持一致——这种一致性没有机制保障，下次改版本大概率漏一处（Explorer 属性和关于信息各说各话）。单源 + 派生（configure_file）让"CMake VERSION 是唯一要改的地方"，其余全部机械同步，漏改在编译/审计期就会暴露。
- **Q：`ModbusLens-2.0.0-<verified-architecture>` 里的 x64 为什么现在不能写死？** A：因为 Phase 1 取证只证明了**工具链目录**是 mingw1310_64（64 位编译器），没有从构建产物/缓存里做过正式的架构取证——规程禁止"根据文件名猜"。E3 会从 CMakeCache/编译器目标实测后填写；如果实测结果推翻了假设，命名约定还没固化，改起来零成本。
- **Q：E1 被授权了，为什么这条边界里 icon 还是不能碰？** A：因为版本/元数据（E1）和视觉资产（E2）的**风险面完全不同**：E1 改的是构建配置与资源编译，验证靠机器（ctest/PE inspection/字符串审计）；E2 涉及资产生成工具链、多尺寸视觉质量、人工 icon 验收——两者混在一个阶段，任何一处失败都会让另一处无法独立回滚。分阶段是"可运行、可测试、可回滚"原则的直接应用。

## 49. Post-T020 M9-E E1 条目（2026-09-19 追加）

- **Q：为什么 applicationVersion 要走 configure_file 生成的头文件，而不是 main.cpp 里写个 "2.0.0"？** A：因为 Version Decision 冻结了**单一 authority**：CMake `project(VERSION)`。如果 main.cpp 里再写一个 "2.0.0"，仓库就有两个版本源——下次改版本漏改一处，运行时版本和 PE 元数据就会各说各话（正是 0.1.0 时代埋下的隐患）。生成的 `modbuslens_version.h` 让 C++ 侧**只读不写**：改版本 = 改 CMake 一处，重新 configure 全链同步。生成的文件留在 build tree 不提交——提交的是模板（version.h.in / .rc.in）。
- **Q：smoke 测试里的 `"2.0.0"` 字面量，算不算违反"禁止第二硬编码"？** A：不算，而且**必须**是字面量。那个字面量在**测试期望**里，不在版本源里：oracle 的职责是独立钉住"用户决策的版本是 2.0.0"，如果期望值也从 MODBUSLENS_VERSION_STRING 派生，测试就成了永远为真的同义反复——authority 被误改它照样绿。这和 C4 的 golden facts 是同一原理：**期望值必须独立于被测物**。代价是未来 release decision 时要同步更新期望值，这个耦合是刻意买的检测能力。
- **Q：PE numeric version 是怎么机器验证的？** A：分层。①**runtime/PE inspected**：PowerShell `VersionInfo.FileVersionRaw/ProductVersionRaw`——Raw 属性读取的是 VS_FIXEDFILEINFO 的**二进制 numeric 字段**，返回 `2.0.0.0`，证明 `2,0,0,0` 真正进了资源段；②**source/configure inspected**：configure_file 生成的 .rc 内容里 `FILEVERSION 2,0,0,0`。报告里两类分开写，不用字符串属性冒充 numeric 证据。
- **Q：windres 连续报 syntax error，学到了什么？** A：两条 .rc 语言的硬知识：①**windres 的注释是 `//` 或 `/* */`，不认 `--`**（我按 CMake 习惯写注释直接语法错误）；②`VERSIONINFO` 里的 `VOS_NT_WINDOWS32`/`VFT_APP` 不是 rc 内建关键字，是 **windows.h 的宏**——.rc 必须 `#include <windows.h>`，windres 用 MinGW 自己的 include 路径能找到。另有一条 Qt API 知识：`applicationDisplayName` 在 **QGuiApplication** 上，不在 QCoreApplication。
- **Q：OriginalFilename 为什么写 ModbusLens.exe，build 里的 exe 不是叫 modbuslens.exe 吗？** A：审计确认：CMake target id 是小写 `modbuslens`（build 树 = modbuslens.exe），deploy 脚本从 T008.1 起 `copy /y` 成 `ModbusLens.exe`（产品交付名）。差异 = 大小写 + 既有部署改名，**无更实质的 rename**，不触发 STOP。OriginalFilename 按**用户实际拿到的文件名**写 ModbusLens.exe；不改 target OUTPUT_NAME 或 deploy naming 来"对齐"——那会破坏既有 deploy checklist 与文档。
- **Q：applicationDisplayName 修复前断言就是绿的，那设置它有什么意义？** A：诚实记录：修复前 Qt 的 fallback 让 applicationDisplayName **缺省等于 applicationName**，断言值相同。显式设置的意义不在值，在**来源**：之前"恰好等于产品名"是巧合 fallback，现在是**代码声明的品牌事实**——fallback 行为属于 Qt 实现细节，品牌事实不该寄生在 fallback 上。
- **Q：clean reconfigure 测试防的是什么？** A：防"generated 文件恰好躺在旧 build 目录里"的假 PASS。删掉 `build/debug/generated/` 整个目录再重新 configure——如果模板没提交、或 configure 没把生成逻辑接上，构建会当场失败。实测：两份生成文件从 committed templates 再生，rebuild 后 identity oracle 照常 PASS。

## 50. Post-T020 M9-E E1 correction 条目（2026-09-19 追加）

- **Q：oracle 里的 "2.0.0" 不是运行时版本源，为什么也算违规？** A：因为 E1 的核心契约不是"运行时值正确"，而是 **"change CMake VERSION once → all version consumers follow"**。oracle 里的硬编码期望值是一个**第二维护点**：下次 release decision 把 CMake 改成 2.1.0 时，忘改它，identity oracle 就会拿旧期望去"纠正"正确的新版本——测试从守护者变成绊脚石。它是维护事实，不是数据流事实。
- **Q：那把期望值改成 `MODBUSLENS_VERSION_STRING`，测试不就成了同义反复？** A：单看 version 断言确实如此——这正是**职责重划**的关键：version 一致性的端到端证明改由 **mutation probe** 承担（临时把 CMake 改成 2.0.1，configure/build 后运行时与 PE 全部变成 2.0.1，回滚后全部恢复——机器证明了"改一处、全跟随"）；oracle 的职责收缩为**一致性哨兵**：验证运行时值 == 生成的 authority 值，捕捉"有人绕过 authority 写死版本"这类回归。两个 oracle 各干各的活，谁也不再撒谎。
- **Q：mutation probe 具体怎么做的，为什么它有说服力？** A：把 CMake `VERSION` 临时改成 **2.0.1**（唯一改动、未提交）→ 重新 configure → 生成的 `modbuslens_version.h` 变成 "2.0.1"/2/0/1 → rebuild → smoke identity oracle **PASS 且报 version=2.0.1**（oracle 代码零改动）→ PE 检查 `FileVersionRaw/ProductVersionRaw = 2.0.1.0`（二进制 numeric 字段）→ `git checkout -- CMakeLists.txt` 回滚 → 重新 configure/build → "2.0.0" 全部恢复、工作树只剩 oracle 修复本身。**单一改动点驱动全部消费者**，这才是 change-once 契约的机器证明。
- **Q：还有别的容易踩的"第二版本源"吗？** A：本轮审计划出了边界：CMakeLists 里还有 `cmake_minimum_required(3.21)` 和 `qt_add_qml_module(VERSION 1.0)`——后者是 **QML 模块接口版本**（qmldir 语义），不是产品版本，二者并存是正常的（不同的版本轴）；templates 只允许 `@PROJECT_VERSION@` 占位符；测试期望字面量（如 2.0.0 的 oracle）属于**必须独立钉住的黄金期望**，与版本源性质相反。审计时按"这个字面量被谁消费"分类，而不是按"长什么样"分类。
- **Q：为什么 mutation probe 必须回滚而不提交？** A：2.0.1 只是**证明机制**的临时假设值，不是 release decision——真正的版本值已由用户决策为 2.0.0（Version Decision 节）。提交 2.0.1 等于无授权的版本变更；回滚后还要用 `git status` 证明工作树无残留、用 generated header 和 smoke 输出证明恢复。probe 的价值在证据，不在结果。

## 51. Post-T020 M9-E E2 条目（2026-09-19 追加）

- **Q：window icon 和 PE icon 为什么必须用同一个 ICO 文件？** A：因为它们是**同一个品牌事实的两个投放面**。如果 Qt 用 A.ico、PE 用 B.ico，未来改一次品牌就会出现"任务栏换了图标、exe 文件图标还是旧的"的分裂。E2 用一条路径把它钉死：canonical `assets/brand/icon.svg`（唯一手写母版）→ 派生 `assets/brand/windows/ModbusLens.ico`（唯一 committed 二进制）→ Qt 的 `RESOURCES` 和 Windows 的 `rc.in ICON` **都引用这一个文件**——引用路径的 identity 由 CMake/rc.in 的 source audit 证明。
- **Q：为什么 ICO 的验证要用 struct 手工解析，而不信生成工具的"成功"输出？** A：因为 ICO 的关键契约是**目录里真的有那 6 个尺寸帧**——工具说"成功"不代表每帧都在（PIL 的 sizes 参数只是请求，实际帧以目录为准）。所以 `verify_ico` 用 `struct.unpack` 独立解析 ICONDIR（reserved=0/type=1/count + 每条目的 width/height），逐帧核对 16/24/32/48/64/256。这个 fail-fast 设计立刻回本：它先后拦下了 QSize 比较错误和 QImage→PIL 缺转换两个 generator 缺陷。
- **Q：普通构建为什么必须不依赖 ImageMagick/icotool？** A：因为 derived ICO 是**入库资产**——任何 clone 仓库的人 configure+build 时不应被迫安装图标工具。证明方式：①CMakeLists/build.ninja 零 generator 引用（build.ninja 里仅有的 2 个 "python" 命中是 Qt 自带 SBOM cmake 文件名，不是调用）；②删掉整个 generated 目录、在过滤掉 Anaconda/python 的 PATH 下从 committed 源完整重建，PASS。icon 生成是 **maintainer 工具**（改品牌时才用），不是 end-user/build 依赖。
- **Q：部署环境里 ICO 解码不怕缺插件吗？** A：怕——这正是 §14 列的 E2 P0 风险。实测结果：windeployqt 早就把 `imageformats/qico.dll` 拷进了 deployed tree（E1 之前就有，只是当时没人用它），所以严格最小 PATH 下 `QIcon(":/....ico")` 照常解码——**部署版 smoke 的 identity PASS 行里 `windowIconSizes=[16x16 ... 256x256]` 就是运行时解码成功的机器证据**。依赖事实先实测、再决策，没有提前改 deploy checklist。
- **Q：PE 资源里怎么机器证明"我们的 icon 进去了"？** A：`pefile` 解析 PE 资源目录：E2 后出现 **RT_ICON ×6 + RT_GROUP_ICON ×1**（E1 tree 上是 NONE——这就是 RED）。RT_ICON 的 6 个条目正对应 ICO 的 6 帧。注意边界：资源编译器可能重组字节，所以**不做**"PE 提取字节与 .ico 文件 byte-identical"的契约，只断言资源类型/数量/层级存在；壳层 fallback 图标（ExtractAssociatedIcon）也不会被误当证据，因为 pefile 读的是真实资源表。
- **Q：setWindowIcon 是怎么被"遗漏"又被发现的？** A：集成时我先加了资源、oracle 和 PE 语句， rebuild 后 PE oracle 已绿（RT_ICON 在 exe 里了），但 runtime oracle 报 `application window icon = '<null>'`——因为**从没调用过 setWindowIcon**。这正是分层 oracle 的价值：PE 层和 runtime 层各自独立取证，哪一层缺了立刻指认，而不是靠一张截图糊在一起。
- **Q：图标设计上最大的克制是什么？** A：只做一个几何母题（放大镜 ring 套 2×2 寄存器格 + 手柄），全部用块面与粗描边（16px 仍可辨），蓝色 tile 取自 DS primary 家族但**明确声明不是 DS token contract**，无渐变/无发光/无主题变体（Phase 1 冻结单 icon）。品牌工作的产出是"可识别"，不是"一套视觉体系"——后者才需要重新立项。

## 52. Post-T020 M9-E E3 条目（2026-09-19 追加）

- **Q：cmd 的参数列表为什么会"丢参数"？** A：Windows 的 cmd 批处理对**空引号参数**（`""`）的处理是丢弃——我用 python subprocess 传 `["bat", build_dir, "", "", deploy_dir]` 想让 bat 自己从 CMakeCache 推导 QT_BIN/MINGW_BIN，结果 `%2`/`%3` 消失，后面的 `%4`（DEPLOY_DIR）前移成了 `%2`——deploy 就落到了默认目录。教训：**跨进程传参不要依赖 cmd 的空参数语义**，要么传实值（脚本自己读 CMakeCache 推导后显式传），要么换机制。这个坑的代价是第一版"成功"输出其实部署到了错误目录。
- **Q：为什么 Resource Mirror（ModbusLens/assets/）要从包里删掉？** A：`qt_add_qml_module RESOURCES` 会把资源镜像到 deployed QML 模块目录，但**图标的真身已经编译进 exe 的 qrc**——运行时从 `:/ModbusLens/...` 读的是内嵌资源。磁盘镜像对部署是冗余文件，而且它会被"禁止 loose ModbusLens.ico"检查命中。删掉镜像后 runtime icon 照常工作（部署版 identity 行 windowIconSizes 全 6 尺寸），证明内嵌才是真相。
- **Q：scripted portable ZIP 和 byte-reproducible ZIP 的区别为什么重要？** A：scripted = "固定脚本、固定输入树、产出可交付 zip"；byte-reproducible = "同树两次构建 SHA256 完全一致"。后者要求控制 zip 内部的时间戳、条目顺序、压缩元数据——Compress-Archive/python zipfile 都不天然保证。E3 明确只 claim 前者，并用**连续三次运行的 ZIP sha256 互不相同**作为反证记录（这正是 scripted ZIP 的诚实语义）。不做假 reproducibility（时间戳归一化等 hack 没实现就没实现）。
- **Q：manifest 为什么排除它自己？** A：manifest 记录包内每个 payload 文件的 SHA256——如果 manifest 也给自己算 hash，就会产生"hash 的 hash 的 hash"自指死循环。排除自身是这类完整性清单的标准做法（校验时：先验 manifest 之外的所有文件，manifest 本身作为随附索引）。
- **Q：架构标签为什么必须用 PE Machine 而不是"我 Windows 是 64 位的"？** A：宿主是 64 位不代表编译产物是——交叉编译/32 位工具链都能在 64 位宿主上产出 32 位 exe。规程要求"至少两类证据合理组合"，E3 用了 **PE Machine（0x8664=AMD64，直接读最终交付物）** + 编译器工具链族（x86_64-w64-mingw32）交叉确认，packaging 脚本还内建了"配置 label 与 Machine 不符即 fail"的拒绝逻辑。
- **Q：README 里的 2.0.0 为什么不算"第二版本字面量"？** A：因为 README.txt **不是 committed 源**——它由 packaging step 在运行时从 authority（generated version header）模板化生成。commit 进仓库的只有 make_package.py 的模板逻辑（含 `{version}` 占位），没有任何手写的 release 数字。规则是：**仓库里维护的版本字面量只能有一处（CMake）；一切下游出现都是派生**。
- **Q：fail-fast 在这个脚本里具体指什么？** A：每个 gate（Release exe 存在/版本可取/架构识别/windeployqt/required 文件/forbidden 内容/credential 扫描/manifest/zip/解压/minimal-PATH 运行）失败即 `sys.exit(1)`，绝不打印 warning 继续打包——否则产出一个"看起来完整"的坏包比失败更危险。本轮 4 次真实失败（空参数部署、PE 解析、镜像目录、subprocess decode）全被 fail-fast 定位修复，机制经实战验证。

## 53. Post-T020 M9-E E3 correction 条目（2026-09-19 追加）

- **Q："成功路径全绿"为什么还不算 E3 完成？** A：因为 packaging 的价值主张是**双向的**：好包必须能产出（已证），**坏包必须被拒绝**（此前只有开发期偶然失败的留痕，没有确定性证据）。如果 fail-fast 只有源码里的 `fail()` 字样而没有注入验证，那"坏输入会被拦截"只是愿望。本轮用 9 个 deterministic probes 把九类失败条件全部变成可复现的运行时证据——每个 probe 断言非零退出和准确的失败原因。
- **Q：F4 的"架构拒绝"怎么测，又不把假 exe 发出去？** A：三层隔离：①**不改真实 exe**——用 pefile 读取 Release exe 后把 Machine 改成 0x014C (i386)，**写入 build/e3-failure-probes/ 下的副本**；②gate 拿到的是"架构不支持"的副本，`fail("PE Machine is 0x014C...")` → SystemExit(1)；③probe 断言这个非零退出后，副本留在 ignored build/ 里，永不进入 package。这也顺手验证了"packaging 脚本拒绝架构不一致"的承诺。
- **Q：ZIP hash 越跑越不一样，到底说明什么？** A：两件事，方向相反。**不能说明**：包内容变了或打包坏了——两轮的 manifest 逐字节相同（1496 个文件的 SHA256 全同），zip 尺寸也完全相同（40,569,927）；差异只来自 zip 容器内部的时间戳/元数据。**能说明**：本产物是 scripted portable ZIP，byte-reproducibility 未声明——如果哪天两次运行 hash 相同了反而要检查是不是时间戳被意外归一化了。
- **Q：那 idempotence 的正确证据是什么？** A：**运行前主动清理、运行后集合相同**：两轮各自删重建 staging/ZIP/extraction，然后对比 manifest——**逐字节相同**（1496 文件的相对路径 + SHA256 完全一致，manifest 文件自身 sha256 也相同）。这证明"结果由输入决定，不依赖上一次残留"，比 hash 相同弱一点但正是打包需要的性质。
- **Q：Debug exe 的"348.7 MB"是怎么回事？** A：**转写错误**——我在报告里把 34,870,210 写成了 348,702,210（多敲一位数字）。实测：build/debug 与 deployed 均为 **35,066,016 bytes ≈ 35.07 MB**，与 D6 的 34,782,210 差值是 E1/E2/E3 正常演进（键盘修复 + 图标资源嵌入）。教训：**尺寸/计数类数字必须从工具输出原样转贴，不凭记忆转写**；且 348 MB 这种量级如果真出现，和 Debug 构建的合理范围差了一个数量级，本应在写入时就触发怀疑。
- **Q：probe 为什么直接 import make_package 调函数，而不是每次跑完整脚本？** A：分层——完整脚本是**集成证据**（E3 主报告已有全绿记录）；直接调用 gate 函数是**单元级注入证据**（可以精确制造"只有这一个条件坏"的输入，其余全部正常）。两者互补：集成证明"真包能过"，注入证明"坏包会被拦"。§5 允许这种 maintainer-script refactor，且成功路径 contract 未变。
- **Q：probes 会成为构建依赖吗？** A：不会。probes 全部在 ignored `build/e3-failure-probes/`，runner 在 `build/e3_probe_runner.py`——两者都不进 git、不被 CMake 引用（build.ninja 零 generator/python 调用引用）。maintainer 想重跑就 `python build/e3_probe_runner.py`。

## 54. Post-T020 M9-E E4 条目（2026-09-19 追加）

- **Q：为什么 E4 要从 committed HEAD 把 Release 完整重新构建一遍，而不复用 E3 的 ZIP？** A：因为 E3 correction 之后仓库又前进了一次，"最终 candidate"必须可追溯为**当前 HEAD 的确定性产物**。E4 删除了整个 build/release 从零 configure/build/打包——ZIP 的 sha256 与 E3 时不同正是这种可追溯性的体现（输入树变了）。复用旧 ZIP 等于让 candidate 脱离它的 source provenance。
- **Q：为什么人工验收坚持要求解压到 repo/build 之外的新目录？** A：portable 的承诺是"不需要仓库邻接文件"。如果直接在 build 树里跑，Windows 的 DLL 搜索、Qt 资源解析都有可能"恰好"借用了仓库邻接的文件，掩盖真正的可移植性缺陷。解压到全新目录 + 严格最小 PATH 双重隔离后，能启动才能证明包自包含。
- **Q：unsigned 的安全提示算不算 packaging bug？** A：不算。签名在 Phase 1 已裁定为 release/security workflow 的事（无证书、无管线），E3/E4 的 artifact 定性就是 unsigned local candidate。Windows 对未签名 exe 的提示是预期行为，如实记录即可；只有**无法启动**才升级为 BLOCKER。反过来，docs 也不承诺"signed"——承诺必须与证据一致。
- **Q：用户在 UI 里看不到 2.0.0，怎么确认版本？** A：本应用当前没有 About/version UI surface（E4 范围里明确不加），版本确认走 **PE 层**：Explorer 属性→详细信息显示 File version/Product version = 2.0.0，配合打包脚本的 authority 交叉验证。UI 上显示版本是未来的 About surface 决策，不是 packaging 的必要条件。
- **Q：为什么 manual 清单里的 Demo/sample 只要求"快速 sanity"，而 M9-D 当时验收了全部场景？** A：因为 E4 验收对象是**这个包**，不是产品功能本身。Release 与此前人工验收过的 Debug 候选共享同一 committed 源，且 Release 的三模式/ctest/identity 已全绿；人工部分只需确认"换了一个构建配置、换了一个目录之后，核心用户路径（启动/演示/样本/图标/身份）依然成立"。重复全部 A–T 是没有信息量的仪式。
- **Q：包里那个 StatisticsOverview.qml 一个人都不用，为什么还要验证它在包里？** A：因为"零消费者"是**源码事实**，"保留"是**已登记的决策**——两者的载体就是这个文件还在 QML 模块里。如果某次打包清理顺手把它删了，未来的 ownership decision 就失去了前提，而且违反"extend, do not silently redefine"。E4 的机器检查明确包含"StatisticsOverview retained"这一条。

## 55. Post-T020 M9-E Final Closure 条目（2026-09-19 追加）

- **Q：verified LKGC 为什么落在 4cb6e9d 而不是 E4 的 9ca079c？** A：`9ca079c` 是 docs-only（仅 T020 与状态文档），按规则**任何 docs-only commit 都不能成为 LKGC**——LKGC 的定义是"最后一个 behavior-bearing 且其完整树通过全部验证与人工验收的提交"。4cb6e9d 是 M9-E 最后一个改动了产品/打包行为的提交（validator/fail-closed 加固），而**用户最终验收的 ZIP 正是 E4 从 4cb6e9d 的树全新再生的**——树、证据、人工验收三者对齐。
- **Q：M9-E 有 9 个提交，其中 3 个 docs-only、6 个 behavior-bearing——这个比例说明什么？** A：说明 M9-E 是**决策密度最高**的里程碑之一：Phase 1/2.0.0 决策/E4 记录三个关键决策点都是 docs-only（决策先于代码），而 6 个 behavior-bearing 提交全部有机器证据链（mutation probe、pefile oracle、9/9 fail-closed probes、全门禁）。反过来也说明：**没有决策记录的行为提交是危险的，没有行为验证的决策记录是空洞的**——两者必须成对出现。
- **Q：Company/LegalCopyright/organizationDomain 都留空，"有意 omission"怎么在文档里自证？** A：三处独立证据互相印证：①Phase 1 §16 明确"仓库没有权威信息，不编造法人名称/版权主体/域名"；②PE metadata 实测 CompanyName/LegalCopyright 为空（非缺失字段而是空值，说明模板就是按 omission 设计的）；③Version Decision 的"2.0.0 = explicit human product decision"证明**需要人工权威输入的事项都被显式标出等人决策**——omission 是同一原则在法律字段上的应用。
- **Q：StatisticsOverview 从"zero-consumer but kept"到 M9-E closure 还是没删，这个决定会永远悬着吗？** A：不会——它已登记为独立的 ownership decision（BACKLOG 留档 DEFER），有明确的触发条件（专门 task）。M9-D/M9-E 两轮都拒绝顺手删，是因为删除组件会同时触碰 QML module contract 与 deploy checklist 两侧，需要自己的学习/评审/验证循环。"悬着"正是显式 DEFER 的正确状态。
- **Q：M9-E 的验收链里出现两次"correction"（E1/E3），这对流程说明了什么？** A：说明**验收 HOLD 不是流程失败，而是流程在工作**。E1 HOLD 抓的是 oracle 里的第二版本字面量（维护事实问题），E3 HOLD 抓的是 fail-fast 未被确定性验证（证据充分性问题）——两个都是 Reviewer 从"完成度"视角才能看到、实现者视角容易自证清白的盲区。correction commit 让每个 HOLD 都以可验证的机器证据收口，HOLD→correction→re-review 的循环本身成了质量机制。
- **Q：M9-E 之后，M9 还剩什么？** A：只剩 **M9-F — Learning / Final Acceptance Gate**：global accessibility / Tab focus-chain audit（M9-D/M9-E 两次显式登记到这里）+ 最终跨页面 manual visual acceptance（E4 package-level acceptance 之上，对整个应用做最后一轮全局视觉/交互验收）。M9-F 完成后 M9 整体 COMPLETE，V2 的 UI/UX 里程碑收官。

## 56. Post-T021 M9-F Phase 1 条目（2026-09-19 追加）

- **Q：为什么 M9-F 的 focus audit 强调"audit 是测量，不是验证预设"？** A：因为源码里能确定的事实只有一半：rail delegate 有 Enter/Space handler（声明性存在）、MouseArea 不夺焦（D6 实测）、activeFocusOnTab 未设置。但 **Tab 链的真实顺序、hidden 页是否真的不入链、disabled Device 的焦点表现**——这些是 Qt focus 系统的运行时行为，只能实测。如果 audit 前先写"预期结果"，就变成验证预设而非测量。Phase 1 的判据写的是"什么算合格"，不是"现状是什么"。
- **Q：rail 的 Enter/Space 是 dead path——这是 bug 吗？** A：是**现状缺口**，不是回归：rail delegate 从未持有过 activeFocus（MouseArea 不夺焦、无 activeFocusOnTab），所以 Enter/Space handler 从第一天起就是不可达代码。是否修（比如给 delegate 加 activeFocusOnTab 或处理 Tab 进入）是 P1/P2 分类问题——先 audit 实测，再按用户影响裁定。诚实地说：**键盘用户目前无法用键盘切换 workspace**（除非 audit 证明 Tab 链能到达），这可能是 P1。
- **Q：activeFocus 不跨页保存，和 M9-D 的"selection 跨页保存"矛盾吗？** A：不矛盾，因为它们是**两个不同的契约**。business selection（currentIndex/selectedEntry）是数据权威状态——M9-D 冻结它跨页保留；keyboard activeFocus 是**输入焦点位置**——切页后落在哪是 UX 细节，只要当前页可正常操作即可。把两者混同会导致"为了保焦点而保数据"或"为了保数据而伪装焦点"的错误设计。M9-F 的规则明确：焦点语义跟着 Qt 窗口系统走，业务语义跟着 Controller/页本地状态走。
- **Q：为什么 focus visibility 不能靠断言 activeFocus == true？** A：activeFocus 只证明**焦点在谁那里**（逻辑层），不证明**用户看得见**（渲染层）。一个持焦但无视觉指示的控件，对键盘用户等于不可见——这正是 accessibility audit 要人工看的原因。属性断言可以作为 harness 辅助（读 activeFocusItem），但最终判定必须人工确认 outline/高亮可见。
- **Q：为什么 accessibility naming 以"可见文字"为主来源，而不是给所有控件加 Accessible.name？** A：因为可见文字**同时服务**视觉用户和屏幕阅读器——单一来源，永不漂移；Accessible.name 只服务辅助技术，与可见文字可能不一致。给已有可见文字的控件再加 Accessible.name 是冗余且引入漂移面。规则：**只有纯图标/无文字控件才必须显式 Accessible.name**——本项目当前没有这种控件，如实记录即可。
- **Q：为什么 M9-F 的人工验收基于 Release package 而非 Debug build？** A：因为验收对象是**要交付的东西**。Release 与 Debug 的二进制、Qt runtime、资源嵌入路径都不同；E3/E4 已证明 Release candidate 能构建/部署/打包并通过机器门禁，但**人从未在最终 Release package 上做过全应用级验收**——M9-F 补的就是这最后一步。且若 F1 产生修复，必须重新 Release build/package 并对新树重新接受——Debug PASS 不能豁免。
- **Q：automation gap matrix 里为什么承认 Tab/Shift+Tab 是 missing？** A：因为 nav harness（A–T）驱动方式是**直接设 currentIndex/调 activate**，从不模拟键盘事件；Tab/Shift+Tab 遍历属于 Qt focus chain 的运行时行为。承认 missing 是为了让 Phase 1 Review 明白：M9-F 的 audit 是在**填补真实的证据空白**，而不是重复已有覆盖。

## 57. Post-T021 M9-F Phase 1 correction 条目（2026-09-19 追加）

- **Q：为什么 "audit 还没做" 会成为 HOLD？设计文档先写判据、后做测量，不是很正常吗？** A：判据先行没问题，问题是**决策依赖的方向反了**。原设计写的是"audit 无 blocker ⇒ 跳过 F1"——但在 audit 执行之前，"有没有 blocker" 是未知数，等于让一个**尚未发生的测量**来决定**流程是否继续**。正确顺序是：F0 测量 → findings 分类 → 再决定 F1 是否存在。这不是措辞问题，而是把"计划中的验证"当成了"已完成的验证"来编排流程。
- **Q：rail 同时被写成 "Tab reachability unknown" 和 "dead path"，矛盾出在哪？** A：**"unknown" 是认识状态**（我还不知道），**"dead path" 是结论**（我确定它不通）。我当时引用的证据只覆盖了**点击路径**（D6 审计：点击 rail 不夺焦），却把结论写到了**键盘路径**上——而 Tab 遍历走的是 Qt focus chain，与点击夺焦是两个机制。StrongFocus 也不能反证可达（focusPolicy 只声明资格）。正确表述：**UNRESOLVED pending runtime audit**，双向都不预判。
- **Q：为什么 rail 的 accessibility exposure 要单独审计？它不是有可见文字吗？** A：可见文字解决**视觉用户**的识别问题；**accessible object（role/name/enabled）**解决辅助技术的问题——两者机制不同。rail 的特殊之处在于它是 **plain Item 而非 standard Button**：Qt 不会自动给它 Button 的 role/语义，暴露什么、怎么暴露都是未知的。Device 项的 disabled 语义尤其重要：辅助技术用户需要知道"这一项存在但不可操作"，而不是以为它坏了或者根本不知道它存在。
- **Q："business state persistence ≠ keyboard activeFocus" 这条区分为什么反复出现？** A：因为它们**表现相似但机制和修复层完全不同**。M9-D 冻结的是"切页后选中行还在"（数据）；M9-F 审计的是"切页后焦点会不会藏在隐藏控件里吞键盘"（输入）。如果把 hidden-focus 当成 state 问题去修，就会错误地把业务状态和焦点绑定迁移，破坏 M9-D 已验收的 persistence 契约。区分清楚后，两个问题各自的修复互不干扰。
- **Q：focus visibility 为什么不能用 "selected 背景色" 代替？** A：因为 selected 和 keyboard focus 是**两种可能同时存在、也可能单独存在**的状态。用户 Tab 到一个**非选中**行时，如果只有 selected 样式，这行看起来就是"普通未选中行"——焦点在哪完全不可见。所以可见性判定必须是：**keyboard focus 状态本身有独立可辨的视觉**（outline/高亮/其他），并且与 selected、hover 可区分。这条在 Phase 1 冻结为验收规则，F0 实测时逐项核对。
- **Q：automation gap matrix 里把 qml_nav PASS 和 focus accessibility 明确划清界限，会不会显得之前的工作"不算数"？** A：不算数的是**声称的范围**，不是工作本身。qml_nav 验证的是业务状态持久性与 workspace 路由——它在自己的范围内是有效的。问题只在于**不能把它外推成焦点可访问性证据**。gap matrix 的价值就是诚实地标出每类契约的证据来源与缺口，让 M9-F 的测量有明确的靶子。

## 58. Post-T021 M9-F F0 条目（2026-09-19 追加）

- **Q：Tab 链为什么只到 Clear Results 和 rail items，不到 ListView/ComboBox？** A：Qt Quick 的 Tab 遍历**只覆盖显式声明了 Tab 焦点资格的控件**。`focus: true` 给 ListView 的只是"初始 activeFocus"，不是"Tab 可到达"；AppButton/Button 之所以可达是因为 Control 基类默认 `activeFocusOnTab: true`。plain Item/ListView 没有这个默认值，必须显式声明。这就是"focus: true ≠ Tab-reachable"的本质——两个属性控制两个不同机制。
- **Q：rail delegates 在 UIA 里显示为 "Window / ModbusLens"——这意味着辅助技术用户看到什么？** A：五个 rail 项在辅助技术用户看来是**五个完全相同的匿名窗口**，无法区分哪个是"事务"哪个是"总览"。虽然每个 delegate 里有一个 Label 子项暴露了文字（"事务"/"总览"/…），但焦点落在 delegate Item 上而非 Label 上——所以辅助技术读到的是 delegate 的 accessible object（无区分名），不是 Label 的文字。修复方向：给 delegate 加 accessible name（如 `Accessible.name: modelData.label`）。
- **Q：F0 的 "P0=2" 具体指什么？为什么标 P0 而不是 P1？** A：①键盘用户**无法 Tab 到 Transactions list**——这意味着 M9-D 人工验收的 Up/Down/Home/End 键盘导航对键盘 Tab 用户来说根本**无法到达**（除非用鼠标点击列表区域先获取焦点）；②键盘用户**无法到达任何页内 interactive control**——Run Demo、Connect、Load Replay 等核心操作全部超出键盘可达范围。这两条是**功能阻断**（不是体验劣化），所以标 P0。
- **Q：GAP 和 P0/P1 的区别是什么？为什么不把 GAP 也标成 P0 或 P1？** A：P0/P1 是**产品缺陷**（有明确的修复方案和用户影响）；GAP 是**证据/工具限制**（我们知道有问题，但当前工具无法量化严重程度或验证修复效果）。例如"rail delegates expose as Text 非 Button"是 GAP——修复需要 QML Accessible attached property 重构，但重构后我们无法在当前环境可靠验证 accessible tree 是否改善（无 screen reader）。把 GAP 混入 P0/P1 会给出无法兑现的修复承诺。

## 59. Post-T021 M9-F F0 continuation 条目（2026-09-19 追加）

- **Q：为什么 F0 的第一版报告会被 HOLD？** A：因为它**把 Transactions 的 Tab 测量外推到了所有 workspace**。原报告写"全部页内 interactive controls 不在 Tab 链"，但 Dashboard/Communication/Replay/Diagnosis **从未被分别实测**——我只测了启动页（Transactions）。实际补齐测量后发现：Communication 的 ComboBox/SpinInput、Replay 的 Load Button、Diagnosis 的 TabButtons **全都在 Tab 链里**。原结论是"未实测 → 外推"，这在 measurement 任务里是致命的——测量报告的价值就是**只写测到的东西**。
- **Q：F0-C1（hidden workspace Controls 在 Tab chain）是怎么被发现的？** A：逐页测量时我注意到 Communication 的 Tab 序列里出现了 `dashboardRunDemo`——Dashboard 的 Run Demo 按钮，而当时当前页是 Communication。同理 Diagnosis 序列里出现了 `replayLoadButton`。这说明 **Tab 链穿透了 StackLayout 的隐藏页**：Qt Quick Controls 默认 `activeFocusOnTab: true`，而 StackLayout 只是把 hidden children 设为不可见，**不自动把它们移出 Tab 链**。这是一个真实的、之前从未被任何测试覆盖的缺陷——nav harness 只验证业务状态，从不模拟 Tab。
- **Q：rail Enter/Space 的"GAP"为什么能变成"confirmed defect"？** A：因为 F0 的实测方式从"读 UIA 属性"变成了"**注入真实按键并观察可观察结果**"——用户在 rail 上按 Enter/Space 后 workspace 是否切换，这是**用户可见的结果判定**，不需要证明 QML handler 内部调用链。实测：Enter 不切换、Space 不切换 ⇒ **CONFIRMED DEFECT**。这也回答了一个方法论问题：当工具无法证明内部机制时，**用可观察行为作为 oracle** 而不是留 GAP。
- **Q：为什么 rail 的 accessibility semantics 从 GAP 升级成了 P1？** A：因为本轮**实际读取了 focused delegate 的 UIA 属性**（ControlType/Name/Patterns）。结果明确：ControlType = Window（不是 Button/MenuItem）、Name = "ModbusLens"（不带 rail item 的区分名）、无 InvokePattern 无 SelectionItemPattern。这不是"工具无法判断"，而是"判断结果明确为缺陷"——辅助技术用户面对五个完全相同的匿名窗口，且不知道它们可以被激活。**GAP 只应该留给真正无法判断的情况。**
- **Q：为什么"selected highlight"不能当 focus indicator？** A：因为它们是**两种独立状态**：selected 是"当前所在的 workspace"，focus 是"键盘焦点所在"。当用户 Tab 到**另一个**（非 selected 的）rail item 时，那个 item 没有任何视觉变化——用户的键盘焦点是"不可见的"。这两个状态可能同时存在（Tab 到当前 workspace 的 rail item）、也可能分离（Tab 到别的 item）——而后者才是 Tab 遍历的常态。所以必须有一个**独立于 selected 的 focus 视觉**。
- **Q：F1 的 scope 为什么只有三条（外加 P1 待定）？** A：因为 findings 的每一条都对应一个具体、最小的修复点：①ListView 加 Tab entry；②hidden children 移出 Tab 链；③rail 的 keyboard activation 修好。这三条都是**焦点系统层面的最小改动**，不涉及布局、样式、功能语义。P1 的三条（rail name/role/focus indicator）是同一文件（NavigationRail.qml）的补充，是否入 F1 由 Review 决定——**不把"看起来相关的都带上"**。
- **Q：这轮的 probe 为什么删掉了？** A：因为 evidence 已经落在 T021 文档里（序列、计数、分类），而 probe 脚本是一次性测量工具——保留它们可能让未来的人误以为它们是项目基础设施。规则是：**测量工具用完即弃，测量结果入档**。删除后 `git status` 只显示 docs 变更，`build/` 里也没有残留的 e0_audit 文件——cleanliness 是可以被验证的（`ls build/e0_audit*` 返回不存在）。

## 60. Post-T021 M9-F F0 Final Closure 条目（2026-09-19 追加）

- **Q：为什么测量前必须先"机器证明 workspace"，而不能直接按 Tab？** A：因为按 Tab 的结果只有在你知道**焦点落在哪个页面上**时才有意义。F0 第一版把"点击坐标"和"rail 顺序"当作导航成功的证据——结果有一轮 rail 点击根本没生效（窗口不是 foreground 时，第一次点击被 Windows 当作激活点击消费掉），测出来的"Dashboard 序列"其实是 Transactions 序列，**结论完全错位**。所以本轮定了硬规则：**先证明 expected workspace == actual exposed workspace，再解释 sequence；验证失败该次 sequence 直接作废**。这条规则本身就把一个假结论挡在了报告之外。
- **Q：怎么在 UIA 里机器证明"当前显示的是哪个 workspace"？** A：先做一个事实发现——**UIA tree 只暴露当前显示页的元素**：隐藏页的 page-exclusive 元素（`dashboardRunDemo`、`diagnosisAgentQuestion`）在其页面隐藏时**完全不在 tree 中**。于是判别式变成「页面独占元素暴露 ⇔ 该页正在显示」。注意判别式必须覆盖**同一页的所有状态**：最初的版本用 `transactionsEmptyHint` 判断 Transactions，但那个元素 `visible: observedCount == 0`——**有数据行时它不暴露**，于是"有 4 行数据的 Transactions"被误判成非 Transactions（假阴性）。修正为 4 个状态互斥元素的 OR 后才稳定 5/5。
- **Q：TabBar 的 UIA SelectionPattern 为什么不可靠？** A：实测 12 次读取有 6 次返回 `<none selected>`，其中一次与"哪个 pane 正在显示"的事实**直接矛盾**（报告 selected = AI 解释，而实际暴露的 pane 是 Baseline）。结论：这个映射只在"由点击引发的选择变化"后可信，作为通用 oracle 不可用。**降级办法**：改用 **pane-exposure oracle**——哪个 tab pane 的独占元素被暴露，就是哪个 pane 正在显示。这是一个更贴近用户所见的事实。
- **Q：本轮最强的证据是什么？** A：**H1S**。先用键盘把焦点移到"运行基线诊断"（**从不点击它**，因为点击就已经执行了），再用 rail 切到 Transactions，然后注入一个 **SPACE**——结果 Transactions 页面上的文案从「尚未运行基线诊断。」翻转为「已有基线诊断结果，可在诊断工作区查看。」这意味着：**一个当前不可见的控件收到了键盘输入并真实执行了业务动作**，改写了 Controller 的 `hasBaselineDiagnosis`。这不再是"焦点管理不干净"的工程洁癖，而是**有用户可见后果的缺陷**。（附一个真实细节：Qt Quick `Button` 的键盘激活键是 **Space**，`ENTER` 并不激活——这一点也是实测出来的，不能凭直觉写。）
- **Q：为什么 H1（Run Demo）看不到副作用，却不能判它"无害"？** A：因为我在源码里查到了原因：`TransactionListEntry`（`src/ui/TransactionListModel.h:15`）只有 deviceAddress / functionCode / status / elapsedMs / exceptionCode / issueText——**没有时间戳**，而 demo 批次由固定种子的 SimulatedSlave 生成，**完全确定性**。也就是说"重复发布一次 demo 批次"和"什么都没发生"**状态等价**。所以"看不到变化"是**这个控件的幂等性**造成的，不是"按键没送到"——焦点读数（注入前后都停在 `dashboardRunDemo`）证明按键确实送到了。这个区分很重要：**不要把"测不出影响"当成"没有影响"**。
- **Q：H2 的 TextArea 为什么算 P0 而不是"体验问题"？** A：因为它**污染数据**：在隐藏状态下注入的按键进入了控件的文本内容（value 从 `[\t\t]` 变成 `[\t\tQY]`）。一个用户看不见、也没打算编辑的输入框，替他改了提问内容——这是数据完整性问题。同一个控件在可见状态下还有另一个缺陷：**Tab 被当作字符写入**（value 从 `[]` 变 `[\t\t]`），导致焦点永远出不去（正 14/14、反 6/6 都停在它上面）——**根因和现象是同一条**。
- **Q：为什么把 rail 的三条 P1 冻结进 F1，而不推迟到 P2？** A：因为三条来自**同一个根因**：`NavigationRail.qml:54-113` 的 delegate 是裸 `Item` + `MouseArea`，不是 `Control`——Qt 的 accessibility bridge 对它**没有对应 accessible object**，所以 ①没有区分名（回落到窗口名 "ModbusLens"）②没有 actionable role/pattern（只有 Window/Group，没有 Invoke/Selection）③没有键盘焦点视觉。这三条是**同一个设计选择的三张面孔**，分开修反而要改三次同一个文件。同时这也是"admission rule"的纪律：P0/P1 进 F1，P2 记 DEFER，**不允许"最后一轮顺手做得更漂亮"**。
- **Q：GAP 如何收口？** A：本轮把 GAP 从 3 降到 **1**，剩下的是**明确接受的非阻塞项**：Transactions ListView 的行与容器不对 UIA 暴露（QML accessibility bridge 限制）——所以行级无障碍断言**无法自动化**，F1 里必须走人工/视觉验收。另外两个原来的 GAP（H1/H2）不是"被忽略"，而是**被真实注入实测消灭了**——measurement 任务的正确收口方式是补测，不是把不确定项留成 GAP 混过去。
- **Q：本轮修正了两个此前已入档的结论，为什么不直接改旧文字？** A：因为仓库规约把 `docs/tasks`、`issues`、`adr`、`devlog` 视为**只增不改的档案区**：如果需要修正事实，用**追加批注 + 明确"取代"关系**，不覆盖原文。所以 FC5/FC6 里写着 "H1/H2 = GAP" 的原文**保持原样**，新的 §FD4/§FD5 明确声明"取代"。这看起来啰嗦，但它保留了一条真实的认知轨迹：**我先前为什么那么判断、后来用什么证据改判**——这在面试里比"一直正确"更有说服力。
- **Q：探针脚本自身的缺陷也写进档案，会不会显得"不专业"？** A：正好相反——本轮记录的四条探针缺陷（PD-1 判别式假阴性、PD-2 无 BOM 导致中文字面量被 ANSI 破坏、PD-3 非 foreground 时首次点击被吞、PD-4 SelectionPattern 不可靠）**每一条都曾导致过一个错误结论的候选**。把它们写清楚，等于把"我的测量结论是在什么工具条件下得到的"讲清楚了：**一个没有记录工具缺陷的测量报告，读者无法判断它的可信边界**。这也是"不得只修掉后删除痕迹"在测量环节的落地。
## 61. Post-T021 M9-F F0 Final Re-review HOLD → scope correction 条目（2026-09-19 追加）

- **Q：为什么 F0 的最终复审会 HOLD？测量不是已经全部完成了吗？** A：因为 HOLD 的**不是测量，而是 implementation scope 的完整性**。F0 的证据已经被接受（ACCEPTED，没有新的 measurement gap），但冻结的 F1 scope A–G **漏掉了一个已确认的 P0**：Agent TextArea 的 Tab 焦点 trap。发现了缺陷却没把它写进修复范围，等于宣布“要修东西”时少修一件——这种错误在 implementation 阶段会变成一个永久遗留的缺陷，所以必须在授权前堵住。
- **Q：这个 P0 为什么会漏掉？** A：因为 **findings 表和 scope 列表是分开维护的**。scope A–G 是从 Phase 1 Review 的 §16 模板继承下来的，而那个模板写于 Agent TextArea trap 被发现**之前**（当时只知道 ListView 入口、hidden 获取、hidden 保留、rail 四条）。后来 F0 把新发现的 P0 补进了权威 findings 表，**却没有回头把它补进 scope 列表**。根因不是漏看，而是**缺一步机械的覆盖性检查**：没有逐行核对“每个 P0 是否都在 scope 里有对应条目”。
- **Q：怎么防止再次发生？** A：把覆盖性检查变成**冻结动作的一部分**：从本轮起，scope 冻结必须附一张 **finding → scope 映射表**，任何 P0/P1 **没有条目即视为 scope 未完成**，不得进入 implementation。这把“记忆力问题”变成了“格式问题”——格式可以检查，记忆力不行。
- **Q：为什么不直接把所有 P1 也归为“可选修饰”？** A：因为 Phase 1 已批准的规则是 **P1 = clear usability/accessibility defect**。rail 的三条（无区分名、无 actionable role、无焦点视觉）对键盘用户和辅助技术用户是**真实的阻断**：五个完全同名的匿名停点，且不知道可以激活。把它们称为 optional polish，在实践上等于“不会被修”。同理，**5 个 P0 全部必须进 F1**，没有例外——规则要能在未来降低决策成本，而不是留下讨价还价的空间。
- **Q：为什么 GAP 不一起修？** A：因为 Transactions ListView 的 UIA exposure GAP **已经被 Review 接受为 known non-blocking evidence limitation**——它是工具/证据层面的限制（QML accessibility bridge 不暴露行与容器），不是产品行为缺陷。把已接受的非阻塞项拉进 F1，会把“最小修正”变成“重构 accessibility 桥”。
- **Q：H（Agent TextArea）的修复会不会把输入框弄坏？** A：这正是本轮要先定契约的原因。契约是：**Tab / Shift+Tab 只用于 focus traversal**（Tab 离开、Shift+Tab 返回），而 **Left/Right/Up/Down/Home/End 必须继续保持文本编辑语义**；F1 acceptance 必须**同时**验证两组行为。另外，如果 Qt 本身有原生机制（`tabChangesFocus` 或真实等价机制），实现时**优先用最小原生机制**，而不是自己写一套 Tab 拦截——但这一点**本轮不实现、也不提前写死 patch**，避免在没有 RED 证据前就把方案锁死。
- **Q：H1S 和 H2 为什么要写成“acceptance oracle”？** A：因为它们是**刚刚被真实重现的可观察失败**，天然适合做回归验收：H1S = 隐藏的 运行基线诊断 被 SPACE 激活后，Transactions cue 会从「尚未运行基线诊断」翻转为「已有基线诊断结果」；H2 = 隐藏的 Agent TextArea 的 value 会被注入的字符改变。修复后两者都应该**不再发生**——这比“新写一个断言”更有说服力，因为它们是**曾经真实存在过的失败现象**。
- **Q：为什么要把序列里的“conditional F1”取消？** A：因为“conditional”是 Phase 1 在**还不知道有没有缺陷**时的写法（当时设计为“audit 无 blocker 可跳过 F1”）。现在 authoritative P0/P1 已经确认，再写 conditional 就是一个**会让后来的人重新讨论已定事实**的歧义点。序列定稿为：**F0 COMPLETE → F1 REQUIRED → F1 Review → F2 → F3 manual acceptance + M9 closure**。
- **Q：本轮为什么没有 probe 清理步骤？** A：因为本轮**不做任何测量**（不重新运行 F0 audit），自然不产生探针脚本；上一轮的探针已在那一轮删除并做过显式无残留断言。本轮只改 docs ——这也是一种可验证的声明：`git status` 应只出现 docs 文件。

## 62. Post-T021 M9-F F1 条目（2026-09-19 追加）

- **Q：为什么 F1 要先做 RED，而且 RED 必须是真实键鼠注入？** A：因为 F1 的每一条都是行为契约。读 QML 源码只能看到声明（例如 `focus: true` 或 `Keys.onReturnPressed`），而**声明不等于可达**：`focus: true` 只是作用域初始焦点，不构成 Tab 可达；而 `Keys.onXxx` 只有在该 item 真正持有 activeFocus 时才会触发。RED 用真实 SendKeys/鼠标把“用户能做什么”测出来，避免把源码推论当成结论。
- **Q：为什么会有两项 F0 P0 在 F1 里“没复现”？这不是很危险吗？** A：确实危险，所以我把它当成最重要的一件事处理：**先设计能把它们变成回归断言的实验**，再下结论。F0 的两条结论都来自一个没有 workspace oracle 的探针：它只能看到“焦点元素的 UIA 身份”，而 rail 停点的 UIA 身份就是窗口本身——于是“焦点在 rail 项上”和“焦点不在任何 rail 项上”读出来一模一样。加上当时导航只靠坐标点击（后来证实窗口非 foreground 时第一次点击会被当 activation click 消费），结论就会错位。F1 用**结构判定**（焦点元素的 parent 链属于哪个 page）和 **index-resolved**（railIndexOf(focusItem) 必须等于 k-1，且before-key 读数排除 “Tab 本身就切了”）重建了证据，两项才能安全地归为探针缺陷。
- **Q：B/C 为什么用“页面级 enabled 门控”而不是给每个控件写条件？** A：因为问题的因果在容器层：StackLayout 只把非当前子树设为不可见，而 Qt 的 focus traversal 会跳过不可见项（所以“获取”类漏洞实际不存在），**但 activeFocus 不会因不可见而释放**（所以“保留”类漏洞真实存在）。在容器上设 `enabled: false` 一箭双雕：子树不参与 Tab，且 Qt 在 disable 时清除子树内 activeFocus。如果改成给每个控件写 `activeFocusOnTab: visible`，不仅要改几十处，而且永远会漏掉新增控件，也解决不了 activeFocus 保留。
- **Q：把 rail 的 bare Item 换成 Button，不担心破坏原来已经工作的 Enter/Space 激活吗？** A：担心，而且这正是我把 D 当回归保护项处理的原因：AbstractButton 只原生处理 Space（按下/释放），**不处理 Return/Enter**（这一点在 H1S 里实测过：隐藏的 baseline 按钮对 ENTER 不响应、对 SPACE 响应）。所以实现保留了 Enter 的显式 Keys 处理器，并且删掉 `Keys.onSpacePressed`（避免与原生激活重复→双击活）；FD/FE 用 5×2 的断言把它钉住。
- **Q：为什么删了 `Keys.onSpacePressed` 而不是保留？** A：因为同一个键不能有两条激活路径。Button 的 Space 会走 `clicked` → `activate()`，而附加 Keys 处理器会在 item 层先看到事件；两者共存就可能一次按键激活两次。`activate()` 本身是幂等的（设同一个 index），但重复路径会让未来的人无法判断哪条是真正生效的。**一个动作只保留一条路径**。
- **Q：H 为什么不能用原生 `tabChangesFocus`？** A：因为那个属性只存在于 **QtWidgets**（QTextEdit/QPlainTextEdit），Qt Quick 的 TextArea 没有（我在本机 Qt 6.11.1 的头文件里逐个核对过：QQuickTextEdit 只有 `tabStopDistance`）。而 QQuickTextEdit 默认就把 Tab 当作文本输入写成 `\t`，所以我用 Qt 自己的 focus-chain API（`Item.nextItemInFocusChain`）在该控件上完成两个遍历键的转发，而不是写一个全局吞键处理器；编辑键（方向/Home/End/输入）语义不变，并且用 FG/FH 同时断言“能逃脱”和“草稿不变”。
- **Q：为什么要新建 `--qml-focus-check` 而不是扩展 `--qml-nav-check`？** A：nav check 已经是 165 个 stage 的业务场景演练，把 focus 断言塞进去会让 A–T 的历史含义模糊；而且 focus 契约有自己的独立受控面（workspace 门控 / rail 激活 / 列表入口 / TextArea 遍历）。新模式仍然遵守同一套 test-mode 架构（真实 app 加载自己的 QML、合成事件 seam、非零退出即失败），并且被 ctest 登记为 `qml_focus_check`，这样它和其它 26 项一样是每次构建都会跑的回归。
- **Q：F1 为什么不能推进 LKGC？** A：因为 LKGC 的定义是“最后一个**行为相关且完整树通过所有门禁 + 人工验收**”的提交。F1 改了焦点/隐藏页可交互行为，自动化门禁全绿，但**“焦点看得见吗”这一项只能由人看截图确认**（Focus Visual Review = WAITING FOR USER）。在人工 PASS 之前把 LKGC 推到 F1，会让 LKGC 不再代表“已验收”。
- **Q：这轮最有价值的一个技术结论是什么？** A：**“不可见”与“不可用”在 Qt Quick 里是两件事**：不可见的项会被 focus traversal 跳过（所以不会被 Tab “进入”），但**已经持有 activeFocus 的项在变不可见后仍然收键**。原因是两条不同的代码路径：前者是 `canAcceptTabFocus`（检查 enabled/visible），后者是 focus 释放机制（仅在 disable 时清除，不在 invisible 时）。这一条直接决定了修复方案：在页面根上用 `enabled` 而不是靠 visible 去“自然”解决问题。

## 63. Post-T021 M9-F F1 Correction 条目（2026-09-19 追加）

- **Q：为什么“标准 Control”不能自动等于“有可见焦点”？** A：因为样式的焦点指示只在它的 background 没被替换时才存在。AppButton 和 commPortCombo 都用自定义 background 替换了样式层——一旦替换，样式里绑定 activeFocus/visualFocus 的矩形就不再被实例化，焦点视觉随之消失。所以我用**像素差分**实测而不是读样式源码下结论：AppButton/commPortCombo/TabButton 在聚焦前后 **0 像素变化**，而 SpinBox（保留 Fusion background）有 456 个采样点变化。同一个样式，不同的 background 选择，结果完全不同。
- **Q：为什么“没有 focus cue”必须是 P1，不能是 P2？** A：因为 P2 在我们的分类里是“optional polish”，而键盘焦点可见性不是打磨——一个看不见焦点的可交互控件，对键盘用户来说相当于不存在。把它标 P2 等于用分类学把缺陷藏起来。这次修正不只是改标签：四个 P1 都实际修了，并且都有修复前后的像素证据（0→324/274/806/2457）。
- **Q：为什么 TextArea 的编辑键验收不能只断言“handler 没处理”？** A：因为“handler 不处理”是实现细节，用户关心的是结果：光标到底动了没动、文本被不被改写、有没有意外触发别的控件。FL 用多行草稿（abc、换行、def）把每个键的期望光标位置写成精确断言（Left 4→3、End 4→7、Up 4→0、Down 1→5…），并同时断言 text 不变和 workspace 不变。这才是行为级的回归保护。
- **Q：ListView 的焦点环为什么要绑定两个条件？** A：因为 QQuickItemView 在键盘导航开始后会把 active focus 交给当前行的 delegate（M9-D D6 的发现）。只绑定 list.activeFocus 的话，用户按下第一个方向键的那一刻环就会消失——恰恰是用户最需要看见焦点的时候。所以“list.activeFocus ∥ currentItem.activeFocus”才是“焦点在 list 里”的完整定义。
- **Q：这轮探针又折腾了一回，学到了什么？** A：四个都是“探针状态与被测状态的耦合”问题：①在页面隐藏时预取的 UIA 元素句柄 rect 会失效——先导航后解析；②用 rect 匹配焦点落点对嵌套 input（SpinBox）失效——改用“焦点中心点落在目标 rect 内”；③锚点点击带来的是 MouseFocusReason，visualFocus 为 false——必须用键盘遍历到达；④固定按键次数不验证落点会醉成别的停点——walk-and-verify。每一条都是“测量工具必须先证明自己看到的就是被测对象”的具体化。

## 64. Post-T021 M9-F F1 Focus Visual Correction 条目（2026-09-19 追加）

- **Q：为什么“自动像素差分说有变化”与“人看不出”可以同时成立？** A：因为两者测的不是同一件事。像素差分算的是「有没有渲染变化」（哪怕只是同一条边框从淡灰变蓝），而用户需要的是「我能不能一眼分辨它与 selected」。我上一轮把这两件事当成一件（FK 断言甚至只读 `border.width == 2` 这个实现属性），于是给了一个“测量上真、感受上假”的结论。**自动化只能证明指示存在与状态绑定，可辨认性必须交给人。**
- **Q：为什么把焦点指示从“改边框”改成“新增环”？** A：因为选中态**已经占用了边框**。当两种状态用同一条通道表达时，它们就只能表现为“同一种信号的不同强度”，用户必须对比才能判断；而新增一个**位置不同、形状完整的环**，就是一个独立的可爬取特征。实测也支持这一点：改边框时只有边框像素变（bg/label/geometry 逐字节不变），而环是**平白地多出一圈**。
- **Q：为什么要特别声明“这不是 activation failure”？** A：因为现象很像“Tab 没生效”：用户需要多按几次 Tab 才能循环回来，看上去像“按了没反应”。但实测显示 focus 确实落在三个 TabButton 上（三个连续停点），且 pane 始终是 Baseline——**Tab 本来就不应切换页面**，Space/正常激活才改 selected。把它归因成 activation 问题会引导我去改 focus chain 或 selected 语义，那是错的修复方向。
- **Q：这次只改了一个组件，为什么还要跑全部回归？** A：因为网格、尺寸与点击行为都可能被一个新增子元素影响：我选的环是 `color: transparent` + 内缩 2px（不占布局、不消耗鼠标），geometry 仍必须证明为 18 段·0 GEOFAIL·rail 宽 56；同时 ctest 27/27（Debug+Release）证明没有破坏任何旧契约。

## 65. Post-T021 M9-F F2 条目（2026-09-19 追加）

- **Q：为什么 F2 要从零重新 configure/build，而不能拿现成的 Release exe？** A：因为“candidate”身份必须可追溯。如果用旧的可执行文件，就无法证明它确实由 `b237ddc` 的 clean tree 产生。本轮 `rm -rf build/release` 后重建 207 个目标，并且后续的 deploy / package / fresh extraction / 最终截图全部链式地从这一次构建出来——**证据链上不能有一个环节是“不知道从哪来的那个文件”**。
- **Q：为什么要把旧的 `build/f2_evidence`、`build/f3_evidence` 先删掉？** A：因为证据目录也是证据：如果 F2 的目录里混着 F1 的产物，未来回看时就分不清一张截图到底是哪个 candidate 的。这正是上一轮 M9-C 名称/状态混淆的教训：**证据的可信度取决于它的命名和隔离度**。
- **Q：为什么不能直接把 E4 的 1496/1497、ZIP 大小拄过来？** A：因为 F1 改了产品行为，exe 变了（本轮 2,795,138 B），所以 ZIP 肯定不同（40,630,813 B vs E4 的 40,569,927 B）。计数恰好相同（1496/1497）是因为文件数量没变，但这个“相同”必须是**本轮重新数出来**的结果，不是复制旧数字——否则下一次真正变了也会被自动带过去。
- **Q：为什么最终截图必须从 fresh extraction 而不是 build/release？** A：因为用户拿到的就是 ZIP 里的那份；如果截图拍的是 build tree，它证明的是一个用户永远不会运行的东西。而且 fresh extraction 还能顺便暴露只在打包后才出现的问题（本轮就碰到：包里只带 windows 平台插件，强制 offscreen 会 DLL 初始化失败）。
- **Q：为什么 AI/Agent 截图不能写“offline/not-configured”？** A：因为我拍完一看发现 AI pane 实际显示的是“模型配置: ModelScope — 模型: Qwen/Qwen3.5-27B”——provider 是**已配置**的，只是没有生成结果。如果我照旧标成“not-configured”，就是用一个方便的措辞掩盖真实状态。正确做法是把截图改名为 no-result 并如实描述。
- **Q：为什么不能自己判定“最终视觉 PASS”？** A：机器能验的是尺寸、非空白、状态身份与完整性；“好不好看、焦点明不明显”只能由人判断。上一轮 TabButton 的 HOLD 正是这个道理的反面教材：机器说“有变化”，人说“看不出”，最后人对。
- **Q：F2 完成后为什么 LKGC 还不能推进？** A：LKGC 的定义里包含“人工验收”。F2 只是**候选**：自动化全绿、包已生成、证据已齐，但用户还没在真实 UI 里走完 14 项清单。推进留给 F3 closure，基于最终被接受的 behavior tree 做 classification。

## 66. Post-T021 M9-F F2 Visual Correction 条目（2026-09-19 追加）

- **Q：为什么“焦点可见”做到了还会 HOLD？** A：因为可见与层级是两件事。我把列表焦点画成了 2px 高饱和蓝色整框，于是它和“选中行的 2px 蓝色条”**同色同重量**，用户看到两个同级别的强调，反而不知道该看哪个。正确的设计是：选中行（业务状态）为主，焦点（辅助状态）为次。
- **Q：为什么把 rings 的 margins 从 1 改成 3？** A：因为选中行的指示条就在列表左内缘（x=0..2）；1px 的环在 x=1 会**压在选中标记上**（用户明确要求不覆盖）。内缩 3px 后环位于 x=3，两者完全分离。
- **Q：为什么要把“环的重量”写成机器断言？** A：因为这次的缺陷正是“实现属性看起来正确、但视觉结果不对”。既然审阅给出了明确的权重要求（1px、半透明），就把它固化成 FJ 的断言：`border.width == 1 && alphaF() <= 0.6`。以后任何人把环改回粗高饱和，CI 会直接报错，而不是靠人再次用眼睛发现。
- **Q：为什么修正后包不能直接算数？** A：因为 46f68ce 那一轮打的 ZIP 是从**修正前**的树生成的，它里面的 exe 还是旧的焦点画法。如果拿它去做最终人工验收，验收的就不是审阅通过的那个行为。所以必须先重新打包。“证据链不能断”在这里具体成一句话：**人工验收的必须是人工审阅的同一个包**。

## 67. Post-T021 M9-F F2 重跑条目（2026-09-19 追加）

- **Q：为什么修正一个焦点颜色就要重新打包？** A：因为 exe 变了（2,795,138 B → **2,801,768 B**），包内容随之变化；更重要的是：**人工验收的必须是人工审阅的同一个包**。否则审阅通过的行为和最终交付的行为不是同一份东西，整个验收链就断了。
- **Q：新 ZIP 的数字怎么拿到的？** A：三个渠道独立复算：文件长度（Python）、**sha256（hashlib 重新计算，不转抄脚本输出）**、**ZIP 条目与 manifest 行数（zipfile 直接读包）**。这样“数字来自包本身”而不是“来自上一次的报告”。
- **Q：为什么重采截图前要先验证窗口归属？** A：因为这次就撞上了反例——**桌面被另一个全屏应用占用**，`SetForegroundWindow` 返回 False、目标坐标处的窗口不是应用、采到的帧内容是那个应用。如果我只看“截图文件已生成”就算通过，就会把一张游戏画面当成 ModbusLens 的验收证据。所以证据采集必须先验证**窗口归属与前景状态**，再读“画面里是不是这个应用”。
- **Q：既然采集失败，为什么不直接用上一轮的截图？** A：因为那些截图来自**被取代的旧包**（除 ListView 焦点那张来自修正后的 build tree）。拿它们冲当新候选的证据，等于用旧产物证明新产物。正确做法是：标记 SUPERSEDED、把失败的采集隔离（不进 docs）、并把“待重采”写进下一步动作。

## 68. Post-T021 M9-F F2 视觉证据重采条目（2026-09-19 追加）

- **Q：桌面被别的窗口盖住时，为什么不直接把它最小化？** A：因为那个窗口就是 **ZCode 本身**（我正在运行的宿主应用），而且它是 topmost、占满唯一屏幕。把用户的 IDE 窗口最小化去换一张截图，代价与惊吓程度都不划算。更好的思路是：**找一条不依赖桌面可见性的采集路径**。
- **Q：那条路径是什么？** A：两部分。①**输入**：虽然鼠标点击会被 z-order 拦截，但 `GetForegroundWindow()` 确实是应用 ⇒ **键盘输入照常送达**，所以全程改用 Tab/Enter/Space/END 导航（rail 用 Tab+Enter、tab 用 Tab+Space、Run Demo 用 Tab+Space）。②**像素**：`PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT)` 是“让窗口自己把内容画到一个 DC”，**与是否被遮挡无关**；实测返回 True 且内容真实（颜色数 5k–13k）。
- **Q：为什么一开始拿到的截图右侧被切了？** A：因为我用 `GetClientRect`（它在 Qt 下返回**逻辑**客户区 1024×720）去建位图，而实际像素是 125% 下的 1280×900。改成按 `GetWindowRect`（物理，含边框）建位图、再按客户区偏移裁掉边框，就与旧证据的 1280×900 口径一致了。
- **Q：为什么不能把旧截图算作新 candidate 的证据？** A：因为旧截图里 exe 的行为与新包不同（焦点环修正前后）。证据链的原则是：**人工验收的包、人工看的图、机器跑的包，必须是同一份产物**。

## 69. Post-T021 M9-F F2 Ring Inset 微调条目（2026-09-19 追加）

- **Q：为什么一个 1px 的微调也要走完整套流程（RED/像素验证/回归/commit）？** A：因为它改的是**用户看得见的行为**。只要 QML 渲染变了，之前基于旧树打的包就不再代表当前行为——所以 ZIP 必须标 SUPERSEDED，人工验收必须重做。“很小”不是不走流程的理由。
- **Q：怎么证明“向外扩了”而不是“变粗了”？** A：把新旧两张截图放在一起逐像素比：环的四条边线坐标分别外移（上 199→197、下 719→720、左 110→109、右 1240→1241），**而线本身仍是单像素、颜色一致**；同时选中行的指示条像素（106/107/108）**逐字节不变**。这三件事合起来才能说“只是位置变了”。
- **Q：为什么不直接改到 margin=1，一步到位？** A：因为 margin=1 会让 1px 的环压在选中标记上（指示条占据前 2 个逻辑像素），那就回到了上一轮被驳回的“两种状态争岚”。margin=2 是“外扩与不覆盖”的边界值，惰性上也是最小变化量；再往外就要动别的东西了，所以交给人工判定，而不是我自己推进。
- **Q：为什么这次不重新打包？** A：因为打包一次就是一次完整的 candidate 生成链（deploy/package/manifest/ZIP/extraction/全部门禁 + 15 张证据）。如果这个 1px 还要再调，就会反复重做。正确的顺序是：**先把视觉定下来，再打包**——证据链只在“行为定稿”后生成一次。

## 70. Post-T021 M9-F F2 Outer-Extent 纠正条目（2026-09-19 追加）

- **Q：为什么“把边框向外移 1px”会看不出来？** A：三个因素叠加：移动量只有 1 逻辑像素（125% 下 1.25 物理像素）、线本身只有 1px 且 alpha 0.5（存在感本就很低）、人眼对“包围尺寸”的感知需要相对变化而不是绝对位移。**机器差分能证明有变化，但证明不了人能看出来**——这正是我上一轮犯的错。
- **Q：那为什么这次改 -2 就应该有感？** A：因为相对变化量大 4 倍：从“viewport 内部 +2”到“viewport 外部 -2”，每边实际移动 **4 逻辑像素 = 5 物理像素**，而且框的位置从“在列表里”变成“包住列表”——**结构性变化比纯位移更容易被感知**。
- **Q：为什么直接改 -2 会让环消失？** A：因为它当时声明在 **ListView 内部**，而该 view 开了 `clip: true`（为了防 ISSUE-004 的行越界绘制）。子项被推出 viewport 就被裁掉。**几何断言仍然通过**（width = parent+4）——这说明只验证 "尺寸对不对" 是不够的，还得验证 "画不画得出来"（本轮靠截图发现）。
- **Q：为什么不能直接把 ListView 的 clip 关掉？** A：因为那个 clip 是为了保证 delegate **不会画到列表外面**（ISSUE-004），属于既定契约。关掉它去换一个装饰效果，是典型的“为了修 A 而破坏 B”。正确做法是把装饰从受约束的子树里**移出去**，放到不受约束的父级。
- **Q：为什么新增一条 extent 断言？** A：因为这次的缺陷正是“尺寸/位置关系被改掉了而没人发现”。把契约写成 `ring.width - viewport.width == 4`，以后任何人把它改回正数 margin，或改成别的值，CI 直接失败。它只有两次属性读，不引入 harness 改造。

## 71. Post-T021 M9-F F2 Final Candidate Regeneration 条目（2026-09-19 追加）

- **Q：为什么每次 behavior 变化都要重新跑一整套 package 链，而不能“只换 exe”？** A：因为 candidate 的定义是“**人工将验收的那一份产物”**：包内容（exe + Qt runtime + QML 资源 + sample）、manifest、ZIP 均必须与审阅的树一致。只换 exe 会让 manifest/ZIP/新 extraction 均失真，也就无法证明“验收的就是审阅的”。
- **Q：为什么要从 ZIP 里再次 extraction，而不直接跑 build/release？** A：因为只有 extraction 能证明“**打包后的东西仍然能跑、行为一致**”。本轮就是在 extraction 里跑出了 FJ 的 `extent = viewport + 4px`，这才能说“最终焦点方案确实进了包”。
- **Q：为什么主动删掉一张已经拍好的截图？** A：因为它**在为一个不成立的状态作证**：文件名与文档都写着“加载对话框已打开”，而画面里是 Replay 默认页。**证据的价值在于它能自证**；一张与声明不符的图比没有图更糟。而且本轮的 load 本来就不在自动化范围（用户明确说不要和 file dialog 缠斗）。
- **Q：为什么把 inventory 写成逐行表格？** A：因为上一轮出现过“11 + 3 = 15”的记账歧义：文字里说了数量，但没逐个点名。这次每一行都是一个真实文件（含状态、尺寸、sha256），且总数与表格行数一致——**不把数量留给下一轮去猜**。
- **Q：Final Visual Review 为什么不能因为 Release-build 截图已 PASS 就标 PASS？** A：因为两者是不同产物：一个是构建树，一个是打包树。只有从 **extraction** 重新拍的矩阵才能证明“**同一渲染确实进了最终 ZIP**”。

## 72. Post-T021 M9-F F3 — Final Acceptance / M9 Closure 条目（2026-09-20 追加）

- **Q：为什么最终 LKGC 推到 `aa2f3db` 而不是 docs HEAD `2640556`？** A：LKGC 的定义是「最后一个**行为相关且完整树通过所有门禁 + 人工验收**」的提交。`2640556` 只含 docs/截图（git show 实证），没有行为变化，不能代表行为树；本轮 closure commit 同理不作 LKGC。
- **Q：为什么要用 `git show --stat --name-only` 逐个审计？** A：message 是人写的描述，文件列表是事实。16 个 commit 的实测算术与预期一致（10 docs/evidence-only + 6 behavior-bearing），但一致必须**验证出来**；一旦不一致，LKGC 落在哪个提交就会变。
- **Q：为什么 closure 里要强调「不把 M9-F 写成一次通过」？** A：真实历程是：F0 首轮外推被 HOLD → per-workspace 重测 → P0-2/P0-3 因探针缺陷被撤回 → F1 主体 → P1 焦点可见性漏分类被 HOLD → 修正 → TabButton 可感知性 HOLD → 内环修正 → F2 首包 → ListView ring 权重 HOLD → 软化 → 人工「几乎无区别」→ 外扩策略 + clip 自我纠错 → 最终 PASS。压成「全部通过」会抹掉最有价值的部分：**人工审阅捕获了自动化看不到的缺陷**，每次 HOLD 都固化成契约（FJ 权重/extent、FK 逐类型、FL 编辑键矩阵）。
- **Q：为什么单独核对截图清单，并说明有一张被主动删除？** A：证据集合的可信度取决于能否逐一点名。上一轮出现「11+3=15」的记账歧义，其中一张后来发现**画面与声明不符**（写着对话框打开、实际是默认页）；与其留一张会撒谎的图，不如删掉并记下原因。最终 14 张逐行可查（文件名/状态/尺寸/sha256），与 `git ls-files` 实算一致。
- **Q：M9 COMPLETE 意味着 2.0.0 已发布吗？** A：不意味。完成的是「产品与包被接受」；发布是需要明确授权的另一个动作。当前 v2.0.0 tag ABSENT、未 push（origin/main 仍在 `a40d935`）、无 Release/upload、unsigned、无 installer。

## 73. Post-T022 M10 Phase 1（Learning / Design）条目（2026-09-20 追加）

- **Q：为什么 M10 第一轮不写代码？** A：因为 M10 是**会改变设备状态**的能力（FC06/FC10 写寄存器）。V2 协议要求含新知识的任务先完成 Learning / Design Gate 并输出 A…G 七问，再停下等 Review。写操作的失败模式（timeout 不代表没写、广播写不证明成功、重复执行非幂等）比读操作严重得多，先把契约设计清楚比先跑通一条 happy path 更有价值。
- **Q：audit 最大的发现是什么？** A：**FC03 主动读已经存在**——`AnalysisController::readHoldingRegistersOnce` + `core::SerialTransactionSession`（FC03-only）+ 真实 QSerialPort adapter，范围校验在窄化转换之前、`serialBusy_` 已经是 in-flight guard。所以 M10 不是"从零造 active master"，而是"把已有 FC03 路径纳入统一契约，再为写操作补齐缺失的 encoder 与安全契约"。audit 还发现 FC06/FC16 **只有被动解码没有 encoder**，simulator 的 `handleRequest` 是 **const（只读）** 且明确"v1 has no broadcast semantics"。
- **Q：为什么强调 write safety 必须独立于 FC03？** A：因为两者共享的是**管线**（transport/codec/taxonomy/statistics/diagnosis），不共享的是**权力与语义**：FC03 是只读请求，FC06/FC10 是设备状态变更。把写当作"换个 function code"会导致漏掉三件事——谁能发起（authority）、执行前用户确认什么（summary/confirmation）、以及 timeout 到底意味着什么（outcome unknown，而不是"没写"）。
- **Q：为什么 timeout 语义这么关键？** A：写请求的 timeout 是一个**歧义状态**：请求已发出、设备可能已执行、响应丢失。如果 UI 写成"写入失败"，用户会以为设备没变；如果写成"成功"，用户会以为设备变了。两种都是错误事实。所以冻结为 write outcome unknown / response timeout，并且**不得**用「成功」表述广播写（ENR 只证明没有响应，不证明写成功）。
- **Q：为什么 confirmation 方案不在 Phase 1 定案？** A：因为四种方案（每次 modal / armed mode / 分级确认 / 无 modal 但强显式）在安全性与操作性上有实质取舍，而且键盘可用性影响不同（modal 需要自管 Tab 边界）。这属于产品决策，Review 需要看到 trade-offs 后裁定；Learning 阶段替用户选一个反而是越权。
- **Q：为什么不新建第 6 个 workspace？** A：因为 Communication workspace 已经是天然 owner（它已有串口连接、请求区、FC03 读按钮，且下游的 transactions/statistics/diagnosis 管线都在同一会话里）。M9 刚冻结的 IA 不应被顺手推翻；写能力应该在同一页内用 read/write 分区表达安全等级差异。
- **Q：M10 会不会给 Agent 加写工具？** A：**不会**。冻结：AI / Agent 没有 implicit write authority，现有 3 个只读 tools（get_session_summary / get_recent_anomalies / get_transaction_detail）不得因 M10 升级；任何 AI-assisted write 必须另立产品与 safety design（新任务 + 新 ADR）。

## 74. Post-T022 M10 Phase 1 Review = HOLD → Correction 条目（2026-09-20 追加）

- **Q：Phase 1 的 source audit 已经被接受了，为什么还要 HOLD？** A：因为「理解现状」和「把安全契约冻结到可以实现」是两件事。HOLD 点名的是四个**进入代码前必须闭**的契约：确定性 transport seam、write transmission disposition、write evidence 归属、echo-mismatch 与 issue 正交；另外要求把 0x10 的命名写清、把 12 项 decision 从「待裁定」落成决议。设计方向没有被推翻，被要求的是**把模糊处写成可验证的句子**。
- **Q：为什么必须要有 recording / fake transport？** A：因为写安全最关键的断言是**否定式**的：「这次动作**没有**发出任何请求」「连按两次**只**发出一次」。真实 QSerialPort 上没法证明这件事——没有设备时你连"发出了没有"都拿不到确定性证据。所以 M10-A 必须先建立 Controller → transport 的可替换 seam，让测试能记录 sendCount 与 exact ADU bytes，并配置 accept/reject、response、timeout、completion timing。**没有这个 seam，W1–W18 里一半的断言只能靠人工看日志猜。**
- **Q：NotSent 和 PossiblySent 的区别为什么要单独立契约？** A：因为「写失败了」这句话在串口场景里可能是错的。代码审计显示：未连接 / 串口忙 / session begin 无效这三个前置拒绝**根本没进 write**，属于确定性的 NotSent；而 `write()` 后被断开或响应丢失时，字节**可能已经在线路上、设备可能已经执行**，这属于 PossiblySent，其设备状态是 **UNKNOWN**。如果 UI 把两者都写成「写入失败」，就是在告诉用户「设备没变」——一个我们并不拥有的事实。
- **Q：timeout 为什么不能改成一个新的 outcome？** A：因为 outcome 与 presentation 是两层。M9 已冻结的 taxonomy（Pending/Success/Exception/CrcError/Timeout/ProtocolError/ExpectedNoResponse）继续用，写操作用的只是 **presentation/context 上的分层表述**：「响应超时，设备写入状态未知」。另造一套 public enum 会让事务、统计、诊断三处各自分叉。
- **Q：审计里最有价值的一条发现是？** A：**FC06 与 0x10 的 echo 失配语义早已正确**：结构合法但回显字段不匹配 ⇒ `ProtocolError` + `WriteSingleRegisterEchoMismatch` / `WriteMultipleRegistersEchoMismatch`（带 expected/actual），而**匹配的回显即使请求语义非法也仍是 Success**——request issues 与 outcome 是正交轴，这一点写在源码注释里。所以 M10 不需要「修」这段行为，只需要**复用**；反过来，如果当初凭 issue 名字去猜 outcome，就会把已经正确的实现改坏。
- **Q：为什么写操作的 raw ADU 一定要留在数据层？** A：因为写是不可逆的。FC03 读错了，重读一次即可；FC06 写错了，你需要的证据是「我到底发了什么字节、对方回了什么字节」。审计显示 `TransactionAnalysis` 与 `TransactionListEntry` 目前**一个字节都不保留**（`ResponseObservation` 只是分析输入，用完即弃），所以 M10 必须补最小字段。UI 要不要显示 hex 可以以后再说，但**数据层不能在发送后就把现场丢掉**。
- **Q：为什么 active 广播写被直接拒绝，而不是用 ENR 表示？** A：广播写影响多个设备、且永远拿不到响应来确认结果；`ExpectedNoResponse` 的语义**恰恰是"不证明写成功"**。在诊断工具里给用户一个既不能确认成功、又可能同时改动多台设备的按钮，是拿安全性换功能性。v1 直接在 validation 拒绝（unit 0，sendCount = 0），passive/Replay 侧的 ENR 语义**原样保留**——它仍然是分析真实总线流量的重要状态。
- **Q：为什么写操作要求每次确认，而不是用一个「armed write mode」？** A：armed mode 把安全性建立在**用户记得自己开过锁**上，而诊断工具的使用节奏是「很久写一次」，间隔越长越容易忘记状态。每次显式确认把危险动作绑在**当次的人为决策**上，也为 audit 提供清晰的边界（哪一次点击对应哪一次发送）。这条是 M10 v1 的冻结决定，不是永久产品决定。
- **Q：为什么要求在同一个 session 内 append 事务，而不是新写一个 WriteHistoryModel？** A：因为第二套模型意味着第二套真相：统计、诊断、导出会各读一半。现有 `publishSerialResult` 是**单行替换**（FC03 场景下够用），但作为写操作的审计轨迹会把上一条证据覆盖掉——所以修的是**发布方式（append）**，不是**数据宇宙（复用现有 transaction universe）**。
- **Q：这轮有没有偷偷改动产品代码？** A：没有。本轮严格 docs-only：三次审计只读源码，四个 blocker 全部以**设计契约 + 验收矩阵**的形式写进 T022（含 W1–W18、T1–T9），未改 src / QML / CMake / scripts / tests / assets / samples / screenshots。verified LKGC 保持 `aa2f3db`，v2.0.0 tag 仍 ABSENT，未 push。

## 75. Post-T022 M10-A（Active Master Contract Foundation）条目（2026-09-20 追加）

- **Q：为什么 M10-A 先做“契约地基”而不是直接做 0x06 写？** A：因为写操作的失败模式（timeout 不代表没写、短写可能已上线、重复执行非幂等）比读操作严重，而它们的证据都依赖三个地基：**可注入的 transport seam**（否则无法确定性证明“0 次发送/恰好 1 次/绝不 2 次”）、**发送时快照**（否则响应会被拿来跟用户刚改过的草稿比对）、**wire 证据保留**（否则写错了连“我发了什么字节”都拿不回来）。地基做完，写功能才有可验证的安全边界。
- **Q：SerialTransport seam 与“把 QSerialPort 抽象进 core”有什么区别？** A：seam 是**依赖反转**，不是搬家：core 仍然零 Qt、零 QSerialPort、零 COM 知识，它只知道 intent/descriptor/analysis；Qt 侧的 `SerialTransport` 接口把“谁提供字节流”变成可替换，生产仍是原 adapter（open/readyRead/timeout/port error 语义一字未改）。测试注入的是 recording transport —— 它回答“是否/几次/什么字节/何时完成”，而设备语义仍由 `SimulatedSlave` 回答，两层不合并。
- **Q：为什么 descriptor 要自洽性校验（wire 必须解码回 frame）？** A：因为“交给 transport 的字节就是被编码的 intent”如果只是约定，就一定会有某条路径悄悄破坏它。把这条不变量放在 `beginActiveRequest` 里，任何 frame/wire/intent 不一致的请求都会在**发送前**被拒（InvalidRequestDescriptor），于是 W17「raw request ADU 与 encoded intent 一致」成为类型层属性，而不是靠人去核对日志。
- **Q：pending snapshot 到底防的是什么？** A：防三类事故：① 用户点完发送又改了输入框，回包到了却按**新**参数解析（把 A 的响应记成 B 的结果）；② 迟到的旧完成覆盖当前来源的批次；③ 一个完成结果被喂给错误的请求。实现上 `pendingRequest_` 既是快照也是 guard，并且比对 `result.request == *pendingRequest_`，foreign/stale 一律整条忽略——测试 TA-10/TA-15 就是这两条。
- **Q：NotSent 与 PossiblySent 为什么值得单列，而不是都叫“发送失败”？** A：因为二者对**设备状态**的断言强度不同。pre-send 拒绝（未连接/忙/校验失败）可以确定“设备不可能收到”，而短计数 write 或超时只能说“字节也许已经在线上”，设备状态是 UNKNOWN。把它们混成一句“发送失败”，用户就会以为设备没变——一个我们并不拥有的事实。disposition 因此被实现为正交的 transport fact，而不是第二个 outcome 枚举。
- **Q：append-only session history 与 UI 只显示一行，不矛盾吗？** A：这正是本轮最需要 Review 裁定的一点。**证据层**必须 append（每次请求的 intent + raw ADU + 判定都不可被后来者覆盖），而 **presentation** 在本轮仍保持 latest-only —— 因为 Phase 1 冻结了“FC03 现有 statistics / source·session 行为”，而把行数与统计从“最近一次”改成“整段会话”是**用户可见**的变化，按纪律必须走 M10-B 的 FC03 contract migration，不能顺手改。所以我实现了 append 权威 + append 投影 API 并测试它（TA-12），但**不把它接到生产展示**，并在报告里显式标为 Review item。
- **Q：可写 simulator 为什么要 opt-in？** A：因为它把“只读端点”变成“会改变状态的东西”。默认 ReadOnly 保证既有 0x03/演示/回放路径一行未改；只有显式 `WriteMode::Writable` 才允许 mutation，而且**先解码成功再写**：非法长度、byteCount 不符、异常形状、异地址全部零 mutation（SA1–SA5 断言 before→request→after）。没有随机、没有线程、没有真实时钟，所以它可被精确断言。
- **Q：为什么 Function16 加了请求解码却仍然“没有写能力”？** A：解码与编码是两种能力。设备侧理解“别人发来的写请求”是本次新增的 decode-only 函数；**主动构造并发出写请求**需要 encoder + 校验器 + 确认 UI，本轮明确不做（`encodeWrite*` 在 src/ 中 0 匹配）。这也解释了为什么 0x06/0x10 的 descriptor 在 session.begin 处会被 UnsupportedFunction 拒绝——不是能力不完整，而是**故意不存在**。
- **Q：本轮抓到的真实缺陷是什么？** A：seam 只做了一半：`connectSerial` 仍在具体 adapter 上调用 openPort，于是注入的 recording transport 从来没被打开，TA02–TA09 集体失败（startAttemptCount = 0）。修法不是改断言，而是把 open/close/start 全部走 seam；保护机制是 TA 系列断言的都是**注入对象**的 sendCount 与 ADU —— 任何绕过 seam 的调用点都会立刻让它们变红。

## 76. Post-T022 M10-A Review HOLD → Post-Submission Evidence Correction 条目（2026-09-20 追加）

- **Q：HOLD 指的“证据丢失”具体丢在哪一步？** A：丢在**顺序**上。原来的终止路径是「先清后报」：`session_.cancel()` 先清掉 session 内部缓冲，随后 `observedResponseBytes_.clear()` 再把适配器自己累积的原始字节清掉，然后才 emit 一句 transportError。于是那一次尝试的 request 快照、exact ADU、已经收到的部分响应、disposition 与终止原因全部消失——源码注释当时甚至写着 "the evidence of this attempt is discarded with the abort"。修法不是加字段，而是把顺序反过来：**先构造证据，再中止**。
- **Q：为什么 transport 中止不能写成一次 ProtocolError / Timeout 事务？** A：因为那不是 Modbus 事实。outcome taxonomy（Success/Exception/CrcError/Timeout/ProtocolError/…）描述的是「请求与响应之间的关系」；而端口错误、用户主动断开根本没有响应可分析。硬塞进去就等于伪造一个设备行为，未来写操作时会把「设备状态未知」污染成「协议错误」，指错排查方向。所以新增的是**正交的 transport terminal 证据**：有快照、有字节、有 disposition、有 reason，唯独没有 TransactionAnalysis。
- **Q：PossiblySent 为什么不能被 UI 当成“最终状态”？** A：它只描述**提交边界**能保守证明的事：字节交给传输层了，但不保证上线、更不保证设备执行。一旦可信响应到达并完成分析，那个响应就是更强的证据；如果 UI 机械地把「可能已发送」叠在上面，用户会以为自己看到的成功/异常是假的。所以冻结成：PossiblySent 是 submission fact，不是 outcome，也不是证据链的最高置信度。
- **Q：“每个 accepted 请求只允许一个 terminal”怎么保证？** A：三层：① 传输侧——terminal 之前先 `session_.cancel()`，session 回到 Idle，之后 timeout 回调或 completion 都不可能再成立；② 控制器侧——terminal 命中后立刻清掉 `pendingRequest_`，重复 terminal 与无 pending 的 terminal 一律整条忽略；③ 测试侧——TF7 把迟到 timeout、迟到 completion、再次 disconnect、重复 error 全部驱动一遍，断言计数不变。
- **Q：Clear Results 与“pending 不被取消”的矛盾怎么解？** A：Clear 是**结果域**操作，不是传输操作：它清掉已完成事务记录与已完成的终止证据，但不动正在飞的请求。于是会出现「先清空、后到达」的时序——那个 pending 完成或终止时，证据作为**新记录**进入已经被清清的会话视图。TF8 就是这个时序的断言，也解释了为什么 Clear 不能顺手 cancel。
- **Q：为什么新 session 会清掉上一段的终止证据？** A：因为证据属于**某个 Active Serial session**（有 sessionId）。重新 connect 意味着新的会话边界，旧会话的证据不能污染新会话，就像 Replay/Simulator 不能拿到串口证据一样。这正是 §18「不得根据 modeLabel / workspace / filename 推断 source」的反面：来源身份是 typed 的，证据归属也必须是 typed 的。
- **Q：本轮留下了什么没做？** A：两件事显式记录而非偷偷补：① **短计数 write**（写到一半失败）在 start 边界已经报 `{accepted=false, PossiblySent}`，但因为它从未建立 pending，所以不会产生 terminal 记录——对未来的写操作这是一个「字节可能已上线却没有记录」的缺口，交 Review 裁定；② 生产适配器的 port-error 终止路径无法在无硬件环境端到端驱动，契约由 seam 侧的确定性注入测试证明，真机仍是 **REAL HARDWARE NOT VERIFIED**。

## 77. Post-T022 M10-A Final Closure — Short-Submission Evidence 条目（2026-09-20 追加）

- **Q：short-count write 到底危险在哪里？** A：危险在「部分字节可能已经离开进程」。`port_.write()` 返回小于 ADU 长度，意味着**没有完整交出请求**——设备也许完全没收到，也许收到了半条被判为畸形帧。两种可能都无法证伪，所以 disposition 必须是 PossiblySent；但如果这时只留一句「串口写入失败」文案，运行时就**没有任何机器可校验的证据**说明这次尝试发生过、发了什么、接受了几个字节。未来写操作时，这一条正是「设备状态未知」的现场。
- **Q：为什么不能靠 start 返回值的 accepted 标志就够了？** A：因为 accepted 表达的是「是否进入 pending」，而证据需求看的是「是否可能已上线」。原实现把两者绑在一起，于是 `accepted=false` 的短计数尝试直接被当成「不需要留证据」。修正的做法是给 start 结果补上第三种语义 **TerminatedDuringSubmission**：既不进入 pending，也必须携带一个 terminal evidence。
- **Q：为什么用返回值携带证据，而不是让 transport 直接 emit 一个 terminal 信号？** A：因为信号会在**运行时还没有 pending 状态**的时候到达——controller 的 stale/no-pending guard 会把它当作迟到事件静默丢弃。返回值携带是同步的、有序的、不可能被 guard 吃掉；这比依赖 emit 顺序安全得多。
- **Q：`submissionAcceptedByteCount` 为什么必须加注解？** A：因为它极易被误读。它只表示 **transport API 报告接受了的字节数**（Qt write 的返回值），既不是「到达设备的字节数」，也不是「真正上线了的字节数」。写清这一点，才不至于在 UI 或诊断里制造物理层精确性。
- **Q：写入返回 0 字节时为什么反而是 NotSent？** A：因为「一个字节都没被接受」可以确定地推出「本次调用没有任何字节离开进程」——这正是 NotSent 的定义。如果把 0 也归到 PossiblySent，不变式就会要求一个本不该存在的 terminal，同时把「确定没发」误报成「可能发了」。这是不变式自身的必要边界。
- **Q：本轮留下的不变式对新功能意味着什么？** A：它是 M10-D/E 的**记账规则**：只要一次尝试的 submission disposition 是 PossiblySent，就必须能在会话里找到恰好一条对应的 durable evidence（事务记录或 terminal 记录）。这样「我到底发过没有」不再依赖日志文案，而是可以在代码与测试里被断言。
- **Q：为什么这轮之后仍然不说 M10-A COMPLETE？** A：因为 M10-A 的定义是「通过人工 Re-review」，不是「自动测试全绿」。自动门禁（29/29 × Debug/Release、30 个 active_master 用例）只是提交条件；验收结论仍由 Review 给出，LKGC 也继续停在 `aa2f3db`。

## 78. Post-T022 M10-A Final Acceptance / Closure 条目（2026-09-20 追加）

- **Q：M10-A 的验收链为什么是三个 commit？** A：因为三次人工 Review 各挡住了一个真实缺口。`18f27e9` 建了地基（统一 intent / 泛化 session / transport seam / recording transport / evidence 保留）；Review 发现**submission 之后的终止路径会把证据丢掉**，于是有 `a09de6e`（port error / disconnect / partial 都保留快照与字节）；Re-review 又发现**short submission（写了一半）属于 PossiblySent 却没有任何证据**，于是有 `b7a6151`（ShortSubmission terminal + accepted-byte count + start-result 三语义）。classification 全部按 `git show --name-only` 的真实文件列表判定，不按 message。
- **Q：为什么 LKGC 推到 `b7a6151` 而不是 closure commit？** A：LKGC 的定义是「最后一个**行为相关**且完整树通过全部门禁 + 人工验收」的提交。closure 提交只含 docs（T022/PROJECT_STATUS/BACKLOG/devlog/笔记），没有行为变化，不能代表行为树。这和 M9-F 时把 LKGC 推到 `aa2f3db` 而非 docs HEAD 是同一条规则。
- **Q：「PossiblySent ⇒ exactly one durable evidence」到底约束了什么？** A：约束的是**记账完整性**：只要一次尝试可能已经把字节交给线路，运行时就必须能指出「这一次尝试」的证据——要么是一条已完成事务记录（走到 response/timeout），要么是一条 transport terminal（port error / disconnect / short submission）。这条不变式让未来的写操作不再依赖日志文案判断「我到底发过没有」。
- **Q：semantics matrix 为什么要冻结成六行？** A：因为写操作最危险的错误不是崩溃，而是**把「未知」说成「确定」**。六行矩阵把「有没有事务记录」「有没有 terminal 证据」与每种 disposition 一一绑定，M10-D/E 实现写确认与结果展示时只能照着填，不能重新发明分类。例如 accepted + timeout 必须是「一条 Timeout 事务、没有 transport terminal」，而 accepted + port error 必须反过来。
- **Q：为什么 short submission 的证据里还要留一个「接受字节数」？** A：为了在排障时能区分「一个字节都没被接受」与「接受了一半」。但它的语义被严格钉在 **transport API 边界**上：不是上线字节数，也不是设备收到字节数。把边界写死，才不会被当成物理层事实使用。
- **Q：M10-A COMPLETE 是否意味着可以写寄存器了？** A：不意味。0x06 / 0x10 的 active encoder 仍然**不存在**（`encodeActiveRequest` 对它们返回 UnsupportedFunction），没有 Write button、没有确认对话框、Agent 也没有任何写工具。M10-A 交付的是**写操作的安全地基**（意图、快照、证据、终止语义、可注入的传输层），真正能写要等 M10-C/D/E。
- **Q：为什么 simulator 能写不等于硬件能写？** A：simulator 是确定性的寄存器文件，写它没有任何物理后果；真机上写错寄存器可能改变设备行为。所以 M10-A 的结论只能是「软件范围 COMPLETE + REAL HARDWARE NOT VERIFIED」，绝不允许把 simulator PASS 写成 hardware PASS。
- **Q：下一步 M10-B 的边界是什么？** A：把 0x03 完整迁移到统一 contract（行为等价），并处理 Active Serial 内部 append history 与用户可见 transaction/history 契约的差异——后者是**用户可见变化**，必须先 STOP+RCA 评审。M10-B 不允许顺手做写功能（encoder / Write UI / confirmation 都不行）。

## 79. Post-T022 M10-B（FC03 Unified Contract Migration）条目（2026-09-20 追加）

- **Q：M10-B 到底迁移了什么？** A：把 0x03 的**可见结果**从「最近一次」改成「整段会话」。协议层一行没改（wire、范围、文案、超时、busy 全部等价），改的是发布路径：M10-A 时权威记录已经 append，但呈现仍每次只投影最新一条，于是「记录 2 条、界面 1 条」。M10-B 把 presentation 接到同一份 append 历史上。
- **Q：为什么不直接把 `setEntries()` 改成 append？** A：`setEntries` 是 **source replacement**（Simulator/Replay 批次、Clear Results 必须整表替换并让页面选择失效），append 是**会话增长**。两者语义不同，混成一个 API 会让「换源」和「多一条」变得无法区分。所以新增 `appendEntries`（insert 行），保留 `setEntries`（reset），并在注释里写明 `append != source replacement`。
- **Q：selection 为什么没有被 append 破坏？** A：因为页面的选择失效路径**只有 `onModelReset`**。append 走 `beginInsertRows/endInsertRows`：旧行内容、顺序、identity 都不变，也不发 dataChanged，所以页面本地快照仍然有效、detail 仍显示原来那条。B05 用信号级断言把这一点钉死（reset 0 次、insert 1 次、dataChanged 0 次、旧行所有角色逐项相等）。这也是「不要 auto-select 最新行」的实现基础：运行时根本不持有选择状态。
- **Q：transport terminal 为什么不显示成一行？** A：因为它不是 Modbus 结果。端口错误、用户断开、写了一半都没有 response 可分析，把它们塞进 Transactions 会让用户以为「设备回了什么」。它们继续走 serial error lane + runtime 证据；B03 明确断言两个 terminal 之后 Modbus 计数与行数**一点都不变**。
- **Q：统计与诊断的输入为什么必须换成整段批？** A：因为它们本来就能吃批（`summarizeTransactions(span)` 与诊断批向量），之前只是被 publish 端喂了单条。换成整段后，一次会话里的成功/超时/异常同时体现在统计与基线诊断里（B03/B04），而 `successRate = success/(completed−ENR)` 的定义没动。
- **Q：本轮抓到的最有价值的问题是什么？** A：一次「迁移时留下两个写入点」的缺陷：新的 append 路径生效后，旧的 `push_back` 忘了删，导致一次完成写两条记录（记录 2 / 可见行 1 / 统计按 2 聚合）。是 ui_bridge 的断言（observed=2）先把它抓出来。教训是：**迁移所有权时，旧写入点必须显式删除**，否则「唯一权威」变成「两份历史」。
- **Q：为什么删掉了 M10-A 的 `publishSerialResult` seam？** A：它是「手工把一条 analysis 塞进界面」的合成入口，latest-only 语义正是本轮要改的契约。删掉后，serial bridge 测试改用 **deterministic recording transport 驱动真实生产路径**（无 COM、无 sleep），断言覆盖面反而更大——测试不再验证一条影子路径，而是验证用户实际走的那条。
- **Q：REAL HARDWARE NOT VERIFIED 还在吗？** A：在。M10-B 的全部结论都来自自动化与确定性 fake；没有真机验证，也不允许把 simulator/fake PASS 写成 hardware PASS。
