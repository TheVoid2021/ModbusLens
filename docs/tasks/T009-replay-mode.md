# T009 — Replay Mode

> 状态：**IN PROGRESS**｜Part A（Replay Log Format + Replay Core）：**Learning / Test Design ✅（docs-only）→ Implementation ⬜**｜Part B（Replay UI Integration）：⬜ Not Started
> 前置确认：T008 DONE、M4 CLOSED、LKGC = `4075223`、ISSUE-002 RESOLVED。
> Part A Implementation 边界预告（未实现，禁止提前）：versioned `.mlog` v1 格式、transaction-oriented text log、纯 C++ parser、Replay semantic model、Replay batch analyzer、复用 T004/T007 模块、deterministic tests、sample fixture；QML FileDialog、Replay 页面、playback、pause/resume、speed、real-time sleeping、filesystem watcher、database、binary format、compression、Serial、AI、Agent 全部不做。

## Goal

设计 Replay Mode 的数据模型与分析链路：读取历史 `.mlog` 文件中的事务记录，通过既有 T004/T007 Core 模块重新分析，产出与 T008 Demo Dashboard 同口径的统计结果——证明"无硬件也能排查历史问题"。

## Background

- T005 SimulatedSlave 从**当前模拟设备现场计算** Response；
- Replay 从**历史文件**读取**已经保存**的 Request/Response/NoResponse，**重新分析**。
- Replay 不应调用 SimulatedSlave 来重新生成历史 Response——它必须尊重日志里记录的原始结果。

## Replay 的角色（vs Simulator）

| | Simulator | Replay |
| --- | --- | --- |
| 模拟对象 | 当前模拟设备（现场计算 Response） | 过去已发生的通信记录（重新分析） |
| 输入 | Request（当前构造） | 历史文件中的 Request + Response/NoResponse |
| 输出 | Response Frame | TransactionAnalysis + Statistics |
| 调用链 | handleRequest → encode → fault → decode → analyze | parse → decode(request) → decode(response) → analyze → summarize |
| 接口形状 | Request → Response | Recorded Batch → Analysis |

Replay 不模拟 Slave 行为——它模拟的是"过去已经发生过的一段通信记录"。因此 Replay 不应调用 SimulatedSlave 来重新生成历史 Response。

## `.mlog` v1 格式（定案）

### Header

```text
MODBUSLENS_MLOG|1|timeout_ms=1000
```

- Format ID：`MODBUSLENS_MLOG`
- Version：`1`（当前唯一支持版本）
- timeout threshold：`timeout_ms=1000`（整个文件级别，v1 不做逐条 threshold）

### Transaction Record

```text
TXN|elapsed_ms|request_wire_hex|response_wire_hex
```

或（无响应时）：

```text
TXN|elapsed_ms|request_wire_hex|NO_RESPONSE
```

- `elapsed_ms`：非负整数毫秒
- `request_wire_hex`：空格分隔的十六进制字节
- `response_wire_hex`：空格分隔的十六进制字节，或 `NO_RESPONSE`

### Comments / Blank Lines

- 空行：忽略
- `#` 开头的行（trim 后）：忽略
- 不支持 inline comments、复杂 escaping、quoted fields——保持简单

### 为什么不用 JSON

C++20 标准库没有 JSON parser。如果为了一个简单 Replay v1：
- 引 Qt JSON → 污染 pure Core 边界
- 引第三方 JSON library → 增加项目依赖

**当前选择**：简单 versioned line-oriented format。优点：human-readable、Git diff friendly、手工可检查、parser 小、无第三方依赖、面试容易解释、后续可以版本升级。**不声称自定义格式永远优于 JSON**——这是当前项目范围下的工程取舍。

## 数据模型（定案，不实现）

### ReplayTransactionRecord

```cpp
struct ReplayTransactionRecord {
    std::chrono::milliseconds elapsed{0};
    std::vector<std::uint8_t> requestWire;
    std::optional<std::vector<std::uint8_t>> responseWire;
};
```

`responseWire` 有值 = 确实收到 bytes；`nullopt` = NO_RESPONSE。**不用空 vector 表示 NO_RESPONSE**——"收到 0 字节"和"没有 Response"语义不同（与 T006 DeliveredWire/DroppedResponse 设计一致）。

### ReplayLog

```cpp
struct ReplayLog {
    std::chrono::milliseconds timeoutThreshold{1000};
    std::vector<ReplayTransactionRecord> transactions;
};
```

### Parser Error Model

```cpp
enum class ReplayParseErrorCode {
    MissingHeader,
    UnsupportedVersion,
    InvalidHeader,
    InvalidRecord,
    InvalidElapsed,
    InvalidHex,
    MissingRequest,
    InvalidResponseField
};

struct ReplayParseError {
    ReplayParseErrorCode code;
    std::size_t lineNumber{};
};

using ReplayParseResult = std::variant<ReplayLog, ReplayParseError>;
```

调用方知道：错误类别 + 哪一行。不建通用 Parser framework / 异常 class hierarchy。不把原始整行错误文本复制进 Core。

## Hex Parser Rule

wire field 规则：
- byte token 必须恰好表示 0x00~0xFF
- 大小写十六进制均允许
- token 间允许普通空格
- 空 request 不允许
- `NO_RESPONSE` 只允许出现在 response field

非法例如：`GG`、`100`、`0x03`、单个半字节。

`01 03` 在语法上可以是合法 hex bytes——至于是不是合法 RTU Frame，交给后面的 T004 decoder 判断。**不要让 text parser 重复实现 RTU 规则。**

## Parser 与 Protocol Validation 分层

```text
Text Syntax → Wire Codec → Transaction Analysis
```

Replay Parser 只负责**文本格式**。例如 `"01 03"` 可以成功解析为两个 bytes。然后 Replay Analyzer 调 `decodeRtuFrame()` 才发现 FrameTooShort。**不要在 text parser 重新检查 CRC / 重新理解 Function03 / 重新判断 Modbus Frame 长度。**

## Request 与 Response 的错误处理不同（重要设计点）

Request wire → decodeRtuFrame：
- CRC Error / FrameTooShort → **Replay 无法建立正常 Transaction**
- → Replay execution error（不是伪造 TransactionStatus）

Response wire → decodeRtuFrame：
- CrcMismatch → **TransactionStatus::CrcError**（现场诊断结果）
- FrameTooShort → T007 ProtocolError

所以：**坏 Request = Replay record 无法分析**；**坏 Response = 往往正是 Replay 想诊断的历史故障**。必须明确区分。

## Replay Execution Error（定案，不实现）

```cpp
enum class ReplayExecutionErrorCode {
    InvalidRequestWire,
    InvalidRequestFunction
};

struct ReplayExecutionError {
    ReplayExecutionErrorCode code;
    std::size_t transactionIndex{};
};
```

当前只支持 Function 0x03。Request decode 成功但 functionCode != 0x03 → `InvalidRequestFunction`。

## Replay Outcome（定案，不实现）

```cpp
struct ReplayTransactionOutcome {
    std::uint8_t deviceAddress{};
    std::uint8_t functionCode{};
    TransactionAnalysis analysis;
};

struct ReplayBatchAnalysis {
    std::vector<ReplayTransactionOutcome> transactions;
    TransactionStatisticsSnapshot statistics;
};
```

不为 UI 把 QString/StatusText/颜色放进 Core。

## Replay Analyzer API（定案，不实现）

```cpp
using ReplayAnalysisResult =
    std::variant<ReplayBatchAnalysis, ReplayExecutionError>;

ReplayAnalysisResult analyzeReplayLog(const ReplayLog& log);
```

流程：for each record → requestWire → decodeRtuFrame → 得到可信 Request → responseWire 有值 ? decodeRtuFrame → ResponseObservation : NoResponse → analyzeFunction03Transaction(request, observation, record.elapsed, log.timeoutThreshold) → 最后 summarizeTransactions。

**不得重新实现 Transaction 分类逻辑。**

## Replay 不真实等待

虽然记录中有 elapsed=25 / elapsed=1000，Part A Replay **不 sleep**。elapsed 是历史事实，直接传给 Analyzer。整份日志可以瞬间完成分析。这叫 **Batch Replay / Analytical Replay**，不是 Real-time Playback（后者不属于 v1）。

## Wire 金样独立复核（2026-09-06）

| Wire | CRC 数值 | Wire bytes | 来源 |
| --- | --- | --- | --- |
| Success request `01 03 00 00 00 02` | `0x0BC4` | `C4 0B` | T008 Demo DEMO-1 同源 |
| Success response `01 03 04 00 64 00 C8` | `0x7ABA` | `BA 7A` | T008 Demo DEMO-1 同源 |
| Exception request `01 03 00 64 00 01` | `0xD5C5` | `C5 D5` | 新（T009 扩展场景） |
| Exception response `01 83 02` | `0xF1C0` | `C0 F1` | 新（T009 扩展场景） |
| CRC Error wire `01 03 04 00 64 00 C8 BA 7B` | 故意坏 CRC（正确 7A） | `BA 7B` | T008 Demo DEMO-3 同源 |

全部经一次性独立 CRC 脚本复核通过（2026-09-06）。

## Parser Test Matrix（REPLAY-A01~A08）

| Test ID | Input | Expected | Priority |
| --- | --- | --- | --- |
| REPLAY-A01 | `MODBUSLENS_MLOG\|1\|timeout_ms=1000` | version accepted, timeout=1000ms | **P0** |
| REPLAY-A02 | 一条有效 Success TXN | elapsed/requestWire/responseWire 全正确 | **P0** |
| REPLAY-A03 | TXN response field = `NO_RESPONSE` | responseWire = nullopt | **P0** |
| REPLAY-A04 | `MODBUSLENS_MLOG\|2\|timeout_ms=1000` | UnsupportedVersion | **P0** |
| REPLAY-A05 | wire field 含 `GG` | InvalidHex + 正确 lineNumber | **P0** |
| REPLAY-A06 | TXN 字段数缺失或多余 | InvalidRecord | **P0** |
| REPLAY-A07 | 空行 + `# comment` | 正确忽略 | P1 |
| REPLAY-A08 | `TXN\|abc\|...` | InvalidElapsed | P1 |

## Replay Integration Test Matrix（REPLAY-I01~I04/I05）

| Test ID | 场景 | Expected | Priority |
| --- | --- | --- | --- |
| REPLAY-I01 | 加载 demo_v1.mlog → parse → analyzeReplayLog | 4 transactions, status 顺序 Success/Exception/CrcError/Timeout, statistics 4/4/0, 1/1/1/1/0, rate=0.25, avg=25.0 | **P0** |
| REPLAY-I02 | 同一 ReplayLog analyze 两次 | ReplayBatchAnalysis 完全一致 | P1 |
| REPLAY-I03 | Request CRC 损坏 | ReplayExecutionError{InvalidRequestWire, transactionIndex} | **P0** |
| REPLAY-I04 | Request 合法 + Response CRC 损坏 | 整个 Replay 不失败；该 transaction = CrcError | **P0** |
| REPLAY-I05 | Response CRC 正确但 address/function 不匹配 | 正常进入 T007 Analyzer → ProtocolError | P1 |

## 不做 IFrameSource

Simulator 自然接口：Request → Response。Replay 自然接口：Recorded Transaction Batch → Analysis。目前没有真正相同的调用形状。等 T010 Serial Mode 出现后，再同时观察三者的真实需求，如果确实存在共同接口再提取。不为"架构看起来高级"强行统一不同概念。

## Part B 边界

T009 Part B 后续才做：Replay UI Integration——Load .mlog（Qt FileDialog）、Qt/App 层读取文件、调 parseReplayLog、调 analyzeReplayLog、填现有 TransactionListModel、填现有 statistics dashboard、显示文件名/Replay Mode、Clear Replay。仍不做 real-time playback / speed controls / seek / Serial / AI / Agent。

## Knowledge I Must Be Able To Explain（16 题）

**R-Q1. Simulator 和 Replay 的本质区别是什么？** Simulator 模拟当前设备（现场计算 Response）；Replay 读取历史文件（重新分析已保存的事务）。前者是"设备端点"，后者是"分析管线"。
**R-Q2. 为什么 v1 Replay 使用 transaction-oriented log？** 当前记录的是"一次已完成/观察到的 Transaction"——正好对应 T007 输入（Request + ResponseObservation + elapsed + threshold）。适合当前诊断目标。未来 Serial recorder 需要更底层 event trace 时可以新增格式版本。
**R-Q3. 为什么 Replay 不需要真的等待 elapsed 时间？** elapsed 是历史事实，直接传给 Analyzer。Batch Replay / Analytical Replay 瞬间完成——不是 Real-time Playback。
**R-Q4. 为什么 .mlog 要带版本号？** 未来格式变更时 parser 可以按版本分派或拒绝；避免格式升级后旧文件被误读。
**R-Q5. 为什么当前不用 JSON？** C++20 无 JSON parser；引 Qt JSON 污染 Core 边界，引第三方库增加依赖。line-oriented format human-readable/Git diff friendly/parser 小。当前工程取舍，不声称永远优于 JSON。
**R-Q6. Parser 为什么只管文本语法，不管 CRC？** 分层：Text Syntax → Wire Codec → Transaction Analysis。Parser 产出的 bytes 交给 decodeRtuFrame 才做 CRC——不在 text parser 重复实现 RTU 规则。
**R-Q7. 为什么坏 Request 和坏 Response 的处理不同？** 坏 Request → Replay 无法建立正常 Transaction → Replay execution error。坏 Response → 往往正是 Replay 想诊断的历史故障 → TransactionStatus::CrcError。
**R-Q8. 为什么坏 Response CRC 应变成 CrcError，而不是 Replay 失败？** CRC Error 是诊断结果（线路问题），不是 Replay 工具自身的问题。整个 Replay 不应因某个响应损坏而崩溃。
**R-Q9. NO_RESPONSE 为什么不用空 vector 表示？** "收到 0 字节"和"没有 Response"语义不同（与 T006 DeliveredWire/DroppedResponse 设计一致）。
**R-Q10. 为什么 timeout threshold 放在日志 header？** v1 整个文件共享一个 threshold；不需要逐条设置。
**R-Q11. 为什么 Replay 继续复用 T007 Analyzer？** 同一事实只允许一份实现。Replay 产出的 TransactionAnalysis 传给 T007 的 analyzeFunction03Transaction，不重新实现分类逻辑。
**R-Q12. 为什么 Replay Statistics 必须复用 summarizeTransactions？** 保证 Replay 的统计口径与 Simulator/Demo 完全一致（T009 Golden 对照 T008 Dashboard）。
**R-Q13. 为什么 Replay Core 不用 QFile？** Parser 是纯函数（string_view → structured data），单测无需真实文件系统。File I/O 归 Part B Qt/App 层。
**R-Q14. 为什么当前不做播放速度/暂停？** 属于 Real-time Playback，不是 Analytical Replay。当前目标是"瞬间完成分析"，不是"模拟实时通信过程"。
**R-Q15. 为什么现在仍不抽 IFrameSource？** Simulator/Replay 调用形状不同（Request→Response vs Recorded Batch→Analysis）；等 T010 Serial 出现后再观察三者共性。
**R-Q16. Replay 如何满足"无硬件也能排查历史问题"的项目目标？** 真实从站的历史通信保存为 .mlog → Replay 加载并重新分析 → 产出与现场相同的诊断结论——不需要真硬件连接。

## Implementation Plan（Part A 下一阶段，18 步）

1. 创建 ReplayLog.h（数据模型 + parser error/model）
2. 创建 ReplayLog.cpp（parseReplayLog 实现）
3. 创建 ReplayAnalysis.h/.cpp（execution error/model + analyzeReplayLog）
4. 建 tests/data/demo_v1.mlog（golden fixture）
5. REPLAY-A01~A08 tests
6. REPLAY-I01~I04（可选 I05）tests
7. RED
8. GREEN
9. clean build
10. full ctest
11. Core Zero Qt
12. docs 归档
13. code commit
14. LKGC
15. docs backfill
16. （预留：Part B — Replay UI Integration）

## Implementation

**未发生。** 本阶段 docs-only；`src/`、`tests/`、`CMakeLists.txt`、`scripts/` 零改动。

## Files Changed（本阶段）

- 新增：`docs/tasks/T009-replay-mode.md`（本文件）、`docs/devlog/2026-09-07-T009-PartA-TestDesign.md`
- 修改：`docs/PROJECT_STATUS.md`（四段式状态）、`docs/BACKLOG.md`（T009 行状态）
- 未改动：`src/`、`tests/`、`CMakeLists.txt`、`scripts/`、presets

## Problems Encountered

无实现问题（docs-only）。

## Solutions

（无。）

## Verification（本阶段，docs-only）

```text
独立 CRC 脚本复核（一次性，不进仓库）：
  Success request   01 03 00 00 00 02 → CRC 0x0BC4, wire C4 0B ✓
  Success response  01 03 04 00 64 00 C8 → CRC 0x7ABA, wire BA 7A ✓
  Exception request 01 03 00 64 00 01 → CRC 0xD5C5, wire C5 D5 ✓
  Exception response01 83 02           → CRC 0xF1C0, wire C0 F1 ✓
  CRC Error         01 03 04 00 64 00 C8 → 正确 CRC 0x7ABA，wire 故意 BA 7B → decode CrcMismatch ✓

git diff --check      → 通过
git diff --name-only  → 仅 docs/；src/、tests/、CMakeLists.txt、scripts/ 未出现
```

## Result

Part A Learning / Test Design 完成：Replay 角色定案、`.mlog` v1 格式定案、数据模型/错误模型/Analyzer API 定案、wire 金样独立复核、分层规则（Parser→Codec→Analyzer）落库、8+4 测试矩阵、16 题问答、18 步实施计划齐备。**Replay 未实现**；Part B 未开始；T009 整体 IN PROGRESS。

## Knowledge Learned

- **Replay 与 Simulator 的接口形状不同**：Request→Response vs Recorded Batch→Analysis——不强行统一（IFrameSource 推迟）。
- **`optional<vector<uint8_t>>` vs 空 vector**："收到 0 字节"≠"没收到"——与 T006 设计一致。
- **版本号是格式演化的保险**：`.mlog` v2 可以新增字段而 v1 parser 仍可拒绝（不是崩溃）。
- **三层分线**：Text Syntax（Parser）→ Wire Codec（T004）→ Transaction Analysis（T007）——每层只做自己那一层的事。

## Potential Interview Questions

- 16 题见上；Implementation 阶段将补充：string_view 生命周期、hex parser 的边界 case、ReplayLog 的 move 语义。

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| Part A Learning / Test Design | 见 `git log` | `T009(Part A): Replay Log 格式与分析学习测试设计（docs-only）` |

> LKGC 维持 `4075223` 不变（docs-only 不推进）。