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
- 只读 Agent 的架构边界如何强制（类型层面无写 API）（T012）
- 无锁队列/环形缓冲的使用场景与取舍（T008/T010+）

## 4. 行为面素材

- **讲一次定位问题**：T001 的 qtlicd 提示 → 读官方提示 → 加环境变量 → 重构建零提示（证据都在任务档案）。
- **讲一次权衡**：为什么把 Modbus 解析自己写而不是引 libmodbus（学习价值 vs 生产力；对拍保证正确性）。
- **讲一次"防呆"**：AGENTS.md 把"每次只做一个任务、必须跑测试、必须更新文档"写成规约，约束自己也知道约束 AI。

## 5. 维护注意

- 更新时保留历史题目（不删不改），新题追加；被推翻的答案注明"已随任务 T00x 更新说法"。