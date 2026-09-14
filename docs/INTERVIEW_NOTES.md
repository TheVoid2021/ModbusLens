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