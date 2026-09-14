# 11 — Project Final Retrospective（项目最终复盘：定位 · 亮点 · 边界）

> 状态：v1（2026-09-14，T015 收盘后建立）。
> 与 [07_FINAL_PROJECT_REVIEW](07_FINAL_PROJECT_REVIEW.md) 的分工：07 是 M8 Phase A 的**事实总账**（"所有结论出自 Git 历史"）；本文件是收盘后的**定位 + 面试叙事 + 边界声明**版复盘，两者互相引用，事实冲突时以 Git/代码为准。
> 本文件不是 README marketing——它允许（并要求）写"不足"；每条事实都指向可核查的证据（文件 / 测试矩阵 / 档案）。

## 1. 一句话定位

> **一个基于 C++20 / Qt6 的 Modbus RTU 通信分析与故障诊断工具：解决协议解析、"一笔事务最终发生了什么"的事务级异常识别、离线的被动日志分析，以及确定性诊断事实与 AI 解释之间的边界四个问题。**

三个明确的"不是"（面试时主动说，比被动辩解强）：

- **不是 SCADA**：没有组态、没有趋势、没有业务语义层。
- **不是完整 Modbus Master**：主动路径只有 FC03 只读；FC06 / 0x10 只有被动分析（understanding），没有任何写路径（类型层不存在 encoder/下发 API，仓库级 grep 零命中作为回归锚）。
- **不是"AI 自动修设备"**：LLM 是只读解释器，不产生协议事实、不执行任何动作。

## 2. 事实底盘（写简历/面试前先记住，全部可核查）

| 项 | 值 | 证据 |
| --- | --- | --- |
| 项目时间 | **2026-09-05 → 2026-09-14**（以 Git 为准；对外材料不得超出此范围） | `git log --reverse/first/last` |
| 提交数 | 155+（随维护增长） | `git rev-list --count HEAD` |
| 任务 | T001~T015 + T001.1 / T008.1，里程碑 M1~M8（含 M8.1）**全部 DONE** | [BACKLOG](BACKLOG.md) |
| Last Known Good Commit | **`ae067ab`**（T015 整体 DONE） | [PROJECT_STATUS](PROJECT_STATUS.md) |
| 测试 | **ctest 24 个目标、24/24 全绿**；passive 55/55（53 test slots + init/cleanup，release 实跑 rc=0）；ui_bridge 55；历史矩阵 CRC-T/RTU-A/F03-B/SIM-T/FAULT-T/TX-A/STAT-B/REPLAY-A/SERIAL-A/DIAG-A/AI-B/AGENT-A·B/PASSIVE-P/MULTI-C/BCAST-C | `CMakeLists.txt`（add_test ×24）、tests/ 各矩阵文件 |
| 代码规模 | src+tests ≈ 16,946 行；**Core 纯 C++20 静态库、Zero Qt**（成品可直接被无 Qt 的 g++ 链接） | `find src tests -name "*.cpp" -o -name "*.h"` |
| 工具链 | MinGW g++ 13.1.0 · Qt 6.11.1（QtSerialPort）· CMake 3.30.5 · Ninja；clean 重建 153 targets 零警告 | PROJECT_STATUS §5 |
| 架构决策 | **ADR ×3**：001 UI=Qt Quick/QML；002 Agent 只读工具架构；003 广播语义（第七状态 `ExpectedNoResponse`） | [docs/adr/](adr/) |
| 问题档案 | **Issue ×9**（001~007 RESOLVED；008/009 = OPEN/MONITORING、不阻塞） | [docs/issues/](issues/) |
| 性能 | Replay 生产链路 ≈1.35~1.4M 条/s（10k~1M 近线性）；口径/原始数据/复现脚本全在仓库 | [10_REPLAY_PERFORMANCE_BENCHMARK](10_REPLAY_PERFORMANCE_BENCHMARK.md) |
| 文档体系 | 11 份编号文档 + 15 个任务档案 + devlog/Issue/ADR 全链留痕 | AGENTS.md 文档地图 |

## 3. 成果维度总览（面试价值表）

| 维度 | 最终成果 | 面试价值 |
| --- | --- | --- |
| 协议核心 | CRC-16/MODBUS（按位实现）、RTU Frame（value 语义、不存 CRC）、Wire Codec（`variant<Frame, RtuDecodeError>` 错误模型）、FC03 / FC06 / Function 0x10；被动分析 Exception/CrcError/Timeout/ProtocolError | 字节序（CRC 低字节在前）、协议建模、边界校验、纯 C++ |
| 事务分析 | 裸帧 → Request/Response transaction；**7 类 outcome**；判定时刻的原因以独立 issue 保留（T014） | 状态机、数据建模、职责分层 |
| Passive Replay | 合法响应、非法 Request、Unsupported function、Broadcast、Generic Exception 共存；per-record 化——"一条坏记录不再毒死整批"；unsupported 显式披露不静默丢行 | 容错与观测思维 |
| 多维诊断（最强设计） | `TransactionStatus` 与 `requestIssues` **正交**：允许 `Success + InvalidRequestByteCount + InvalidRequestLength`、`ExpectedNoResponse + InvalidBroadcastFunction` | "响应成功 ≠ 请求符合协议"的建模案例 |
| Serial | Qt SerialPort 实际接入：chunk accumulation framing、timeout 双路语义（空 buffer→Timeout / partial→真实 wire 事实）、one outstanding | 异步 I/O、Qt/C++ 边界 |
| Simulator | 确定性故障注入四模式（None/DropResponse/CorruptCrc/ArtificialDelay），**不靠随机制造不可复现问题** | 可测试性设计 |
| Baseline Diagnosis | 基于结构化事实的确定性规则诊断，9 类 finding；"可能原因"只出现在 possible checks，绝不包装成事实 | 工程可信度 |
| AI Diagnosis | ModelScope/Qwen 只解释 deterministic facts（bounded 20 行结构化 prompt）；不重判 CRC/Timeout/Exception | "AI 在工业软件里怎么用"的最佳答法 |
| Agent | 3 个只读工具（summary/anomalies/detail）+ immutable snapshot + tool-call 全量校验 + 预算（3 rounds / 6 calls）+ 五层 stale guard | LLM tool calling + 权限边界 |
| Qt/QML | Core Zero Qt；Controller/Model 适配；QML 纯展示（Light + TabBar 三页，T013 人工视觉验收） | 分层、MVC/MVVM 思路 |
| 测试工程 | ctest 24/24；protocol/passive/replay/serial/diagnosis/AI/Agent/UI bridge 全矩阵；语义审计测试（PASSIVE-P16 等） | 非常适合 C++ 校招 |
| 性能基准 | 同机 Release 复测链 + 生成器 + harness 落入 `scripts/bench_replay/`；数字对账入档 | 性能工程 + 证据纪律 |
| 工程过程 | RED→GREEN（每阶段记录断言级失败数）、LKGC 单步推进、docs-only commit 区分、Architecture Gate 制、manual smoke、deploy/minimal-PATH | "不只是把代码跑起来" |

## 4. 真正的技术亮点（面试深挖顺序）

### 4.1 Pure C++ Protocol/Core 与 Qt UI 解耦（依赖方向）

CRC、帧模型、编解码、事务判定、统计、诊断都在 `src/core/`（`libmodbuslens_core.a`，编译产物可被无 Qt 的 g++ 直接链接——本文件建档时实测过）；Qt 只承担 Serial adapter、网络 client 和 UI 桥接（`AnalysisController`/`TransactionListModel`/`SerialPortAdapter`/`ModelScopeDiagnosisClient`）。这说明你理解依赖方向，而不是把所有逻辑塞进 `QObject`。

- 证据：`src/core/` 零 Qt include；CMake 目标 `modbuslens_core` 只链标准库。
- 面试话术：*"协议事实的权威在 Core。UI、AI、Agent 都只是它的消费者——所以三种数据源结论口径必然一致，也所以离线 CI 能测核心。"*

### 4.2 建模演化主线（从"帧合法吗"到"这笔事务有几个独立问题"）

```text
问题一：这一帧是否合法？            （T002/T003/T004：CRC → Frame → Codec，variant 错误模型）
    ↓
问题二：这一笔 Request/Response 最终发生了什么？
                                    （T007：七状态 outcome + 统计）
    ↓
问题三：outcome 归一化后，判定时刻已知的原因去哪了？
                                    （T014：orthogonal `TransactionIssue`（10 值 + 稀疏载荷），
                                     production invariant：ProtocolError ⇒ issue 必有）
    ↓
问题四：请求自己违反的协议约束，靠 outcome 表达得出来吗？
                                    （T015：`TransactionRequestIssue` 有序 collection；与状态正交）
```

最终的表达能力：

```text
Success + InvalidRequestByteCount + InvalidRequestLength
ExpectedNoResponse + InvalidBroadcastFunction
```

**响应成功不等于请求本身符合协议**——这是很多工业软件会建模错的点，也是本项目最有深度的设计。

- 证据：`src/core/analysis/TransactionAnalysis.h`（枚举与稀疏载荷）、`PassiveTransactionAnalysis.cpp`（request 分类单点）、MULTI-C01/02、BCAST-C01/02、DIAG-A14（正交锁定）。
- 面试话术：*"如果我把 request 的问题折进 outcome，就会污染统计（成功率、Dashboard、Agent 白名单都消费 outcome 轴）；单独一维后，健康判定也把'带 request-issue 的事务'显式排除在 Healthy 之外（五条件）。"*

### 4.3 Broadcast → `ExpectedNoResponse`（统计诚实）

> 速记卡（协议定义 / 四情形判定表 / 追问话术）见 §5「广播与广播语义」。

广播（addr=0）按协议不回应。把它归进 `Success` 是"假称写入成功"；归进 `Timeout` 是"把合法行为当故障"。最终新增第七状态 `ExpectedNoResponse`，且统计公式排除它：

```text
completed 包含广播；rate = success / (completed − expectedNoResponse)；分母 0 ⇒ nullopt
```

（分母 0 返回 nullopt 而不是 0/NaN——"全批都是广播"应当显示"无成功率可计算"，而不是 0% 或崩溃。）

- 证据：[ADR-003](adr/ADR-003-broadcast-outcome-semantics.md)（三案穷尽论证 + 精确数学）、STAT-B10、PASSIVE-P14。
- 面试话术：*"广播难题本质是'期望'与'合法性'两个维度被错误折叠。我们把期望做成状态、把请求合法性做成独立 issue，两者正交：非法广播 + 无响应 = ExpectedNoResponse + requestIssues，绝不会误报 Timeout。"*

### 4.4 `Unsupported ≠ ProtocolError`（工具边界 vs 设备错误）

被动流量里三种坏输入三分：

| 类别 | 例子 | 处置 |
| --- | --- | --- |
| 语法层失败 | 文本格式坏 | 整文件 load 失败（parser 契约） |
| 捕获的请求协议非法（CRC 合法但字段违例、响应 CRC 坏） | FC03 quantity=126 | **诊断事实**：per-record request-issue / CrcError |
| 工具不支持的正常语义 | FC08 normal response | per-record `Unsupported` 显式披露，**绝不混称 invalid** |

"能诊断 FC08 的异常响应" ≠ "支持 FC08 正常语义"（generic exception matcher 只需要五个事实：地址、fn|0x80、单字节 code）。这个区分直接决定诊断软件的可信度——工具能力边界不能伪装成设备故障。

- 证据：`ReplayAnalysis.h`（`unsupportedRecords`）、PASSIVE-P04/P07/P15、T015 档案 §6/§35。

### 4.5 AI/Agent 边界（facts authority 与只读强约束）

一句话版本：**AI 不产生协议事实。** CRC、Timeout、Exception、ProtocolError、request issue 全部由 C++ Core 确定；LLM 只读取结构化事实，负责解释与问答。

- 输入侧：DiagnosisPromptBuilder 把 bounded 20 行确定性事实 + system authority 指令打包；ISSUE-006 之后增加 evidence-scope guard 与状态正例语义，杜绝"4 笔小样本被渲染成链路稳定性差"。
- 工具侧：3 个只读工具（`GetSessionSummary`/`GetRecentAnomalies`/`GetTransactionDetail`）；白名单 dispatcher **按构造封闭**——不存在可派发写操作的枚举值、分支或 schema。
- 运行时侧：immutable snapshot（`AgentToolContext`）、tool_call arguments 全量校验、双硬预算（rounds=3/total calls=6）、captured-vs-current + request-generation 双重 stale guard、batch 变更即失效。
- Live 证据：原生 tool calling 经真实 ModelScope（Qwen/Qwen3.5-27B）全链实测；ISSUE-007（预算超限）真实触发→修复→同题复验通过。

- 话术：*"比'我调了个 API 做智能诊断'强的地方在于：AI 错一百次，事实也一个字不坏——它只能解释错，不能判定错。"*

### 4.6 工程纪律与两个可讲的排错故事

- RED→GREEN 全程留痕（每阶段记录"多少断言失败、为什么"——如 T015 Part C 的 42 passed/12 failed）；LKGC 由用户 Review/实跑证据单步推进（回填式，无证据不许动）；docs-only commit 与 code commit 严格区分；重要设计过 Architecture Gate（T015 的 Gate A~F 全批）。
- **故事一（semantic audit）**：generic exception matcher 初版缺 `(request.fn & 0x80) == 0` 前置守卫，request=0x88/response=0x88 会自我匹配成"Exception 0x01"。用户 Review 前专项审计用一条 RED 测试（21 passed/1 failed）抓出，最小 guard 修复，定性为 protocol-semantic edge-case——**主动在 Review 前自审的故事比"测试都过"更有力**。
- **故事二（ISSUE-002）**：Explorer 双击启动报 `pmr::get_default_resource` 缺失。逐步证明是 runtime PATH 里 8.1 旧 libstdc++ 抢占 Qt 所需符号（8.1 版实测缺 pmr 符号），而不是链接/部署问题；解法不是"改开发机"，而是部署脚本强制编译器 bin 三件套 + provenance SHA256 验证。**"ABI/runtime collision"是面试官爱听的真实工业问题。**

## 5. 核心模型速记（面试前默写）

### 7 类事务状态（`TransactionAnalysis.h`）

`Pending`（未到超时）/ `Success` / `Exception`（合法异常响应）/ `CrcError` / `Timeout` / `ProtocolError`（数据到了但不可能是合法配对）/ `ExpectedNoResponse`（广播，ADR-003）。

### 14 类结构化 Issue

响应侧 10（`TransactionIssueCode`）：帧过短 / 地址失配 / 异常帧畸形 / 正常帧畸形 / 数量失配 / 意外功能码 / 防御性 Unknown / FC06 回显失配 / 广播收到响应 / 0x10 回显失配。
请求侧 4（`TransactionRequestIssueCode`）：数量越界 / 长度非法 / byteCount≠2N / 非法广播功能。

### 诊断 finding 9 类（`RuleBasedDiagnosis.h`）

NoData / Healthy / Pending·Exception·CrcError·Timeout·ProtocolError Observed / ExpectedNoResponseObserved / RequestIssueObserved。

### 广播与广播语义（高频考点）

- **协议层**：地址 0 = 广播——总线上所有从站执行、**谁都不回应**（避免应答冲撞）；只对写类功能码成立，本仓库被动支持集 = {0x06, 0x10}（"广播读"不成立：地址 0 + FC03 ⇒ 请求侧 `InvalidBroadcastFunction`）。工程用途：多设备统一启停/统一下发设定值，代价是主站拿不到任何确认。
- **诊断难点**：addr=0 且未观察到响应时，旧六状态无诚实落点——判 `Success` 是假称写入成功，判 `Timeout` 是把合法沉默当故障。
- **判定规则**：

| 报文情形 | 判定 |
| --- | --- |
| 单播（addr≠0）无响应 | `Timeout`（该响应却没响应） |
| 广播（addr=0，0x06/0x10）无响应 | `ExpectedNoResponse`（协议合法行为；不证明写入成功，也不证明设备健康） |
| 广播 + 请求本身违例 | `ExpectedNoResponse` **+** requestIssues（两者正交，绝不 Timeout） |
| 广播却收到响应字节 | `ProtocolError` + `UnexpectedResponseForBroadcast`（真异常：从站不该回应） |

- **统计口径**：`completed` 含广播；`successRate = success / (completed − expectedNoResponse)`，分母 0 ⇒ `nullopt`——广播既不稀释成功率，也不冒充成功。
- **下游**：Baseline 中为 Info（排在 Pending 之后，**不得仅凭广播宣布 Healthy**）；Agent anomaly 白名单不含它；UI 文案「预期无响应」。
- **一问一答**：*"广播写成功了吗？"→"通信层无法证明，工具只报'观察到广播请求且未观察到响应'；要确认得靠后续回读。"* / *"为什么不算 Timeout？"→"广播本就不允许响应，判超时是制造假故障。"*
- 证据：[ADR-003](adr/ADR-003-broadcast-outcome-semantics.md)、STAT-B10、PASSIVE-P09·P14、BCAST-C01·C02、DIAG-A12·A13、AGENT-A12、UI-T02。

## 6. 数值与单位速查（简历三段声明逐项拆解）

### 6.1 协议分析条：7 与 14

| 数字 | 单位 | 含义 |
| --- | --- | --- |
| 7 | 个枚举成员 | `TransactionStatus` 七状态（见 §5） |
| 14 | 个枚举成员（= 10 + 4） | 响应侧 `TransactionIssueCode` 10 个 + 请求侧 `TransactionRequestIssueCode` 4 个；是**两组正交枚举**，不是"一个枚举 14 个值" |
| "结构化" | — | 每个 issue = 枚举码 + 稀疏载荷（expected/actual 字段按需填充）+ 稳定机器 token（如 `response_address_mismatch`），非自然语言描述 |

**口径纠正**：① CRC / Timeout / Exception 是**状态**，不是 issue（issue 覆盖的是地址/数量/回显/请求格式类事实）；② `FC06/Function 0x10` 混了两种记法，统一写 `FC03 / FC06 / FC16（0x10）`；③ FC03 主被动都支持，**FC06 与 0x10 只有被动分析**（无 encoder、无写路径）。

### 6.2 架构设计条：没有性能数字，但有两个预算数字

`per-record` = 判定粒度是单条记录（一条坏记录不再毒死整批；统计只描述 analyzed 子集，unsupported 显式披露）；`ExpectedNoResponse` 见 §5 与 6.3 公式；**3** = 只读工具数（`GetSessionSummary` / `GetRecentAnomalies` / `GetTransactionDetail`）。相关预算：**rounds = 3**、**单次 run 工具调用上限 = 6**、prompt 下发明细 **bounded 20 条**。

### 6.3 性能条：每个数值与单位的含义

| 简历数值 | 单位含义 | 准确解读 |
| --- | --- | --- |
| 10 万条 / 1M | 1 "条" = 1 条 TXN 记录 = **一次请求-响应事务**（含 NO_RESPONSE 的超时事务）；**不是线路帧** | 测试规模 10⁵ / 10⁶ 条事务 |
| 中位 70.9 ms | ms = 10⁻³ 秒；"中位" = 多次重复运行耗时的**中位数**（抗离群） | 整批 10 万条的墙上时钟耗时 |
| 约 141 万条/秒 | 吞吐 = 记录数 ÷ 耗时 | 100000 ÷ 0.0709 s ≈ 1,410,437 条/s |
| 0.709 μs/record | μs = 10⁻⁶ 秒，per record = 平摊到单条 | 70.9 ms ÷ 100000 = 0.709 μs = **709 ns**；与吞吐互为倒数 |
| 1M ≈ 686.7 ms | — | 对应吞吐 ≈1.456 M 条/s |
| 10k~1M 近线性 | 规模跨 100 倍 | 686.7 ÷ 70.9 ≈ **9.69 倍**（数据 10 倍）⇒ 单条成本近似恒定、O(N) 无性能悬崖 |

**三处自洽性**（被追问要能当场算）：① 141 万条/秒 与 0.709 μs/条互为倒数（1 ÷ 1.41e6 ≈ 0.709 μs）；② 70.9 ms（整批）与 0.709 μs（单条）是同一数字的两个量级；③ 1M 实测 686.7 ms 比按 100k 速率外推的 709.2 ms 快约 3.2%（固定开销摊薄 + 缓存行为），不是错误。

**限定词不能省**：**Release** = `-O3`、无调试断言（换 Debug 差数倍）；**单线程** = 单核成绩，不宣称并行扩展性；**生产 Replay 链路** = 读 `.mlog` → `parseReplayLog` → `analyzeReplayLog`（逐记录被动分析 + `summarizeTransactions` 统计），**不含**界面渲染 / AI / Agent / 网络。

**单位陷阱**："条/秒"里的"条"是事务记录而非线路帧（一笔 FC03 成功在线路上是请求+响应 2 帧，按帧计数量翻倍）；0.709 的单位是**微秒**，不是毫秒。本机独立复测对照见 §7。

### 6.4 口径风险清单（面试前过一遍）

1. 把"中位"说成"平均"——若被问轮次，诚实答"原测轮次未记录，已补脚本与文档，复跑按 5~9 轮取中位"。
2. 漏掉"Release + 单线程"限定，变成无前提的速度宣称。
3. 把性能数字说成跨机/生产 SLA——它是**主机相对值**（本机实测波动 ±10~15%）。
4. 七状态漏掉第七个 `ExpectedNoResponse`；14 说成"单枚举 14 值"。
5. 把 CRC / Timeout / Exception 说成 issue（它们是状态）。
6. 说"线性扩展"而不说"单条成本恒定、无超线性增长"。

## 7. 性能基准

详见 [10_REPLAY_PERFORMANCE_BENCHMARK](10_REPLAY_PERFORMANCE_BENCHMARK.md)：生产链路（读文件 → parse → analyze+统计，单线程 Release）本机两次独立运行，100k `parse+analyze` 中位 73.4~84.1 ms、1M 716.9~738.7 ms，≈1.35~1.4M 条/s，10k→1M 近线性（8.3~8.5 倍用时/10 倍数据）。**诚实口径**：主机相对值（±10~15% 噪声带）、非跨机 SLA；脚本与原始输出均在 `scripts/bench_replay/` 与文档 §5 可复现。

## 8. 不足与边界（要主动讲，并习惯"因为…所以没做"句式）

| # | 边界 | 事实 |
| --- | --- | --- |
| 1 | 主动 Serial 只支持 FC03 只读 | 设计范围；FC06/0x10 是 **Passive Replay understanding**，不是主动写寄存器；全仓库无写类 encoder/下发 API（grep 级回归锚） |
| 2 | Replay 是 transaction-oriented 日志（`.mlog` v1 自定格式） | 无 t1.5/t3.5 字符时间分析、无真实时间推进播放、无通用抓包导入 |
| 3 | 无 UART 层诊断 | parity/framing/overrun 错误不可见（那是串口驱动层数据，当前 adapter 不采集） |
| 4 | 无业务语义 | 没有 register map、float32/int32 解码、单位、工程量范围 |
| 5 | CRC 错误未证明物理根因 | 只报告"观察到 CRC 失配"+ possible checks（接地/波特率/EMI 候选），绝不宣称原因 |
| 6 | 性能相关取舍 | CRC 按位实现未做查表优化（v1 无需求有金样；NFR 只要求不丢帧）；无 fuzz |
| 7 | 无 CI | 验证链是本机命令式（configure/build/ctest/smoke/deploy），未接任何 CI 服务 |
| 8 | Hardware Smoke = NOT RUN | 无真实 Modbus 设备，串口硬件链路仅有 Qt 层证据（诚实标注，不伪报） |
| 9 | Agent 只有读 | 无任何控制/配置能力；预算限制使多步深问可能触顶（ISSUE-007 已调宽到 3 rounds/6 calls） |
| 10 | AI 质量依赖 provider 与小样本 | ISSUE-006（过度归因）已修复；ISSUE-008/009（偶发 InvalidResponse、额度不足 UX）仍 MONITORING |
| 11 | UI 视觉是"可用"级 | Light + TabBar 三页 + 稳定表格，无工业风格的深色/主题工程（已按 T013 人工验收收敛） |
| 12 | 单机单文件 | 无数据库、无多设备时间窗聚合、无滚动窗口实时统计 |

**面试句式模板**：*"我做的是 X；我没有做 Y，因为 Z（范围纪律/证据不足/没有真实需求），这在 BACKLOG 里单独立项等待。"*——把边界说清楚，通常比声称"实现了智能根因诊断"更加分。

## 9. 面试叙事线

**30 秒版**（与 INTERVIEW_NOTES §1 一致）：C++20+Qt6 的 Modbus RTU 诊断平台，三种数据源（模拟器/日志回放/真实串口）共用同一协议与诊断核心，结论口径一致；核心分析不依赖 LLM，AI/Agent 是只读增强；全程在仓库留档，每个决策可追溯。

**深挖路径**（按面试官兴趣分流）：

```text
协议字节细节 → CRC 低字节在前 / 按位实现 / 0x83 异常帧 / 官方金样对拍
  建模 → 七状态、14 issue、正交两维、ExpectedNoResponse 统计公式
  被动分析 → per-record 化、unsupported 显式披露、坏记录不毒批
  串口工程 → chunk accumulation、timeout 双路、QueuedConnection 反馈风暴
  AI/Agent → facts authority、只读白名单、预算与 stale guard、真实 Live 证据
  工程纪律 → RED-first、LKGC、Gate、semantic audit、ABI collision 排查
```

## 10. 证据映射表（问到哪、指到哪）

| 声称 | 代码/测试锚点 | 文档 |
| --- | --- | --- |
| CRC16 按位实现 | `src/core/protocol/ModbusCrc.*`；CRC-T01~06 | [T002](tasks/T002-modbus-crc16.md) |
| 七状态 / 14 issue | `src/core/analysis/TransactionAnalysis.h:15,35,86` | [T007](tasks/T007-transaction-analysis.md) / [T014](tasks/T014-diagnostic-detail-preservation.md) / [T015](tasks/T015-passive-replay-expansion.md) |
| 广播统计公式 | `TransactionStatistics.cpp`；STAT-B10、PASSIVE-P14 | [ADR-003](adr/ADR-003-broadcast-outcome-semantics.md) |
| per-record 回放 | `src/core/replay/ReplayAnalysis.h`；PASSIVE-P01~P16 | T015 §Gate F |
| 3 只读工具白名单 | `src/ui/agent/AgentTools.h:26`；AGENT-A01~A09 | [ADR-002](adr/ADR002-readonly-tool-agent-architecture.md) / [T012](tasks/T012-agent-tools.md) |
| stale guard 身份模型 | `src/ui/agent/AgentRuntime.*`；AGENT-B01~B23 | T012 Phase 1/2 |
| AI 不产生事实 | `src/ui/agent/AgentPromptBuilder.*`、`src/core/diagnosis/`；AI-B01~B25、ISSUE-006 | [T011](tasks/T011-ai-diagnosis.md) |
| 写路径不存在 | 全仓库 grep：无 FC06/0x10 encoder、无下发工具分支 | T015 档案（写权限锚） |
| 三模式共享 Core | `SerialTransactionSession.cpp`、`ReplayAnalysis.cpp`、`SimulatedSlave.*` 同源调用 `analyze*` | [T009](tasks/T009-replay-mode.md) / [T010](tasks/T010-serial-mode.md) |
| 确定性故障注入 | `src/core/simulator/SimulationFault.*`；FAULT-T01~T05 | [T006](tasks/T006-fault-injection.md) |
| 性能数字 | `scripts/bench_replay/`；两档复跑原始输出 | [10_REPLAY_PERFORMANCE_BENCHMARK](10_REPLAY_PERFORMANCE_BENCHMARK.md) §5 |
| 语义审计 bug | PASSIVE-P16；RED 21/1 → guard → GREEN | T015 档案 §semantic audit / INTERVIEW_NOTES |
| ABI collision 排查 | ISSUE-002 全轨迹 | [T008](tasks/T008-qt-quick-qml-analysis-ui.md) §T008.1 / [ISSUE-002](issues/ISSUE-002-explorer-launch-dll-collision.md) |

## 11. 维护注意

- 本文与 07 双轨并存：07=事实总账，本文件=定位叙事；任何一方发现过时，用"追加批注"方式标注新事实与日期，不覆盖原文。
- 事实底盘（§2）随每个任务完成后刷新（提交数、测试数、LKGC）。
- **对外材料（简历/README 之外的宣称）中的时间与数字必须与本文件 §2 一致**；简历时间范围以 Git 实证（2026-09-05 ~ 2026-09-14）为准。
- §6 的数值与单位口径与 [10_REPLAY_PERFORMANCE_BENCHMARK](10_REPLAY_PERFORMANCE_BENCHMARK.md) 保持同步；修改任一处后另一处必须同批更新。
- 本文件主张的每个"亮点"必须能在 §10 映射表中找到锚点；找不到锚点的新亮点先补证据再写入。