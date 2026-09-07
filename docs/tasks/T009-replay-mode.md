# T009 — Replay Mode

> 状态：**IN PROGRESS**｜Part A（Replay Log Format + Replay Core）：**DONE ✅**（Learning / Test Design + Implementation + 全量验证）｜Part B（Replay UI Integration）：**Learning / Test Design ✅（docs-only）→ Implementation ⬜**
> 前置确认：T008 DONE、M4 CLOSED、LKGC = `4075223`、ISSUE-002 RESOLVED。Part A 完成验收见文末 Verification；T009 整体完成后才标 DONE。
> Part A Implementation 边界（已实现，见文末；超范围禁项仍然成立）：versioned `.mlog` v1 格式、transaction-oriented text log、纯 C++ parser、Replay semantic model、Replay batch analyzer、复用 T004/T007 模块、deterministic tests、sample fixture；QML FileDialog、Replay 页面、playback、pause/resume、speed、real-time sleeping、filesystem watcher、database、binary format、compression、Serial、AI、Agent 全部不做。

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

## Parser Robustness 规则（Implementation 前定案）

A. **CRLF**：parser 必须正确接受 `\r\n`（按 `\n` 切行，尾部 `\r` 在 trim 后不污染 field）。REPLAY-A07 加入 CRLF case。
B. **elapsed**：必须 `>= 0`；`-1` / `abc` / 整数溢出 → InvalidElapsed。
C. **timeout**（header `timeout_ms`）：必须 `> 0`；`0` / 负数 / 非数字 / 溢出 / trailing garbage（如 `1000abc`）→ InvalidHeader。不建新 enum，InvalidHeader 足够。

## Line Number 规则（Implementation 前定案）

`ReplayParseError.lineNumber` = **1-based physical line number**（含 blank/comment 行的实际位置）。错误发生在文件第 6 行 → lineNumber=6。

- 整个文件没有任何非空/非注释行 → MissingHeader，lineNumber = **0**（无具体 offending line）。
- 第一个 meaningful line 就是 TXN → MissingHeader，lineNumber = 该物理行号。

`ReplayExecutionError.transactionIndex` = **0-based vector index**（定位 ReplayLog.transactions；第一条=0）。lineNumber 给人看文件位置，transactionIndex 给程序定位 records——两个不同概念，不混用。

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

## Replay Execution Error（Implementation 前修正：三值定案）

**Request 可信链（必须全过才允许进入 T007 Analyzer）**：

```text
requestWire
↓ decodeRtuFrame()
合法 RTU Frame
↓ functionCode == 0x03
↓ decodeReadHoldingRegistersRequest()
合法 Function03 semantic request
→ 才允许调用 analyzeFunction03Transaction()
```

```cpp
enum class ReplayExecutionErrorCode {
    InvalidRequestWire,     // RTU decode 失败（CRC mismatch / FrameTooShort）
    InvalidRequestFunction, // RTU Frame 合法但 functionCode != 0x03
    InvalidRequestData      // Frame 与 function 合法但 0x03 语义校验失败
                            //（quantity=0 / quantity=126 / request data 长度错误）
};

struct ReplayExecutionError {
    ReplayExecutionErrorCode code;
    std::size_t transactionIndex{};   // 0-based vector index（非 lineNumber）
};
```

**不把 InvalidRequestData 混成 ProtocolError**：T007 analyzer 的输入 contract 要求 request 本身已经可信；Replay 必须先满足该 contract，再谈响应分类。当前只支持 Function 0x03；Request decode 成功但 functionCode != 0x03 → `InvalidRequestFunction`。

**InvalidRequestData 金样**：`01 03 00 00 00 00 45 CA` —— payload `01 03 00 00 00 00`，CRC = `0xCA45`（wire `45 CA`，CRC 本身正确）；quantity=0 → decodeRtuFrame 成功、decodeReadHoldingRegistersRequest 失败（InvalidQuantity）→ `ReplayExecutionErrorCode::InvalidRequestData`（REPLAY-I03B，P0）。不用坏 CRC 测这一条——否则无法证明 semantic validation 存在。

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
| REPLAY-A07 | 空行 + `# comment`（含 CRLF 行尾） | 正确忽略；CRLF 行尾不污染 field | P1 |
| REPLAY-A08 | `TXN\|abc\|...` / `TXN\|-1\|...` | InvalidElapsed | P1 |
| （附加） | `timeout_ms=0` / 非数字 / trailing garbage | InvalidHeader | P1 |

## Replay Integration Test Matrix（REPLAY-I01~I04/I05）

| Test ID | 场景 | Expected | Priority |
| --- | --- | --- | --- |
| REPLAY-I01 | 加载 demo_v1.mlog → parse → analyzeReplayLog | 4 transactions, status 顺序 Success/Exception/CrcError/Timeout, statistics 4/4/0, 1/1/1/1/0, rate=0.25, avg=25.0 | **P0** |
| REPLAY-I02 | 同一 ReplayLog analyze 两次 | ReplayBatchAnalysis 完全一致 | P1 |
| REPLAY-I03 | Request CRC 损坏 | ReplayExecutionError{InvalidRequestWire, transactionIndex} | **P0** |
| REPLAY-I03B | Request `01 03 00 00 00 00 45 CA`（CRC 正确但 quantity=0） | ReplayExecutionError{InvalidRequestData, transactionIndex} | **P0** |
| REPLAY-I04 | Request 合法 + Response CRC 损坏 | 整个 Replay 不失败；该 transaction = CrcError | **P0** |
| REPLAY-I05 | Response CRC 正确但 address 不匹配（ModbusRtuFrame+encodeRtuFrame 生成，不新增 hardcoded KAT） | 正常进入 T007 Analyzer → ProtocolError | P1 |

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

## Implementation

**Part A Implementation 完成**（真实 TDD：RED → GREEN → 全量验证）：

- `src/core/replay/ReplayLog.h` — 数据模型（ReplayTransactionRecord/ReplayLog，operator== default）、parser 错误模型（八值 + lineNumber）、`parseReplayLog(std::string_view)` 声明。零 Qt 依赖。
- `src/core/replay/ReplayLog.cpp` — parser 实现：anonymous namespace 小助手（trim/splitFields/parseInteger/parseHexWire/fitsMilliseconds），逐物理行 1-based 计数，header/record 两态机；`std::from_chars` 整段消费（拒绝符号/trailing garbage/溢出）；hex token 恰 2 字符 + 0x00~0xFF。
- `src/core/replay/ReplayAnalysis.h/.cpp` — `analyzeReplayLog`：request 可信链（decodeRtuFrame → functionCode==0x03 → decodeReadHoldingRegistersRequest）任何一环失败 → ReplayExecutionError（新错误码 **InvalidRequestData**）；response 侧 decode 失败**不是** replay 失败，进入 T007 → CrcError/ProtocolError；统计经 summarizeTransactions（与 Simulator/Demo 同源）。
- 所有 variant 先具名再 get_if（ISSUE-001 纪律）；Replay 不调用 SimulatedSlave、不 sleep、不自己分类事务。

## Files Changed

- 新增：`src/core/replay/ReplayLog.h` / `ReplayLog.cpp`、`src/core/replay/ReplayAnalysis.h` / `ReplayAnalysis.cpp`、`tests/data/demo_v1.mlog`（golden fixture）、`tests/test_replay_log.cpp`、`tests/test_replay_analysis.cpp`、`docs/devlog/2026-09-07-T009-PartA-Implementation.md`
- 修改：`CMakeLists.txt`（modbuslens_core 源列表追加 replay 两文件；新增 `replay_log` / `replay_analysis` 两个 CTest target；configure_file COPYONLY 复制 fixture 进 build 树；set_tests_properties 补两个名字）
- 文档同步：`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、`docs/02_ARCHITECTURE.md`、`docs/04_TEST_STRATEGY.md`、`docs/INTERVIEW_NOTES.md`

## Problems Encountered

- **PE-1（本次真 bug）：parseInteger 丢弃解析结果。** from_chars 写进 lambda 局部 `value`，忘记写回 out 参数 → version 恒 0、timeout/elapsed 全 0 → 每个 header 被误报 UnsupportedVersion（REPLAY-A01/A02/A03/A07 等大批失败）。定位：最小 standalone repro（g++ 直链 libmodbuslens_core.a，打印 version 恒 0）证明 from_chars 解析正常但外部值为 0 → 根因三行内锁定。教训：out-param 模板助手，from_chars 目标必须直接就是 out。
- **PE-2：测试目标挑刺 —— 两处 -Wrange-loop-construct**（`for (const std::string text : {…})` 按值拷贝）→ 改为 `const std::string&`。零警告恢复。

## Solutions

见 PE-1/PE-2。PE-1 修正为 `std::from_chars(text.data(), text.data()+text.size(), out, base)` 直接写 out。重跑后全部 GREEN。

## Verification

```text
RED（真实记录，工作树不留存）：
  cmake --build → link 失败 101 处 undefined reference to modbuslens::core::parseReplayLog / analyzeReplayLog
  （另先修一处测试代码自身编译错误：QVERIFY 用于非 void 函数 readGoldenFixture）

GREEN：
  test_replay_log.cpp     17/17：REPLAY-A01~A08（A01 ValidHeader / A02 OneTXN / A03 NO_RESPONSE
                          / A04 UnsupportedVersion / A05 InvalidHex+行号 / A06 RecordShape×3
                          / A07 comments+blank+CRLF≡LF（>> 相等断言）/ A08 abc+(-1)
                          / A09 MissingHeader（0 与物理行号两种）/ A10 timeout 六种错误
                          / A11 MissingRequest / A12 空 response+NO_RESPONSE 在 request）
  test_replay_analysis.cpp 7/7：REPLAY-I01 Golden（4 状态顺序/exceptionCode=0x02/统计 4,4,0,1,1,1,1,0,0.25,25.0）
                          / I02 determinism（两次全等）/ I03 坏 CRC request→InvalidRequestWire!
                          0 与 1 / I03B quantity=0→InvalidRequestData / I03C 0x06→InvalidRequestFunction
                          / I04 坏 CRC response→CrcError 不失败 / I05 地址不符→ProtocolError

  ctest --preset debug-local : 100% tests passed, 0 tests failed out of 16（原 14 + replay_log + replay_analysis）
  clean build（--clean-first）：96 targets，零警告零错误
  QML smoke：--qml-smoke-test exit=0
  Core Zero Qt：grep src/core/replay → 无 Q* 引用，exit=1 PASS
  ISSUE-001 防回归：3 处 get_if 全部作用于具名局部变量
```

## Result

**T009 Part A = DONE**。`.mlog` v1 解析 + Replay 批量分析链落地 modbuslens_core（Pure C++20、Zero Qt）；request 可信链三错误码（含 InvalidRequestData）；bad request/bad response 非对称语义经 REPLAY-I03/I03B/I03C 与 I04/I05 锁定；Golden Replay 统计与 T008 Demo Dashboard 完全同口径（4/4/0、1/1/1/1/0、0.25、25.0ms）——Simulator 现场生成 vs 历史文件加载达成一致。**T009 整体保持 IN PROGRESS（Part B Replay UI Integration 未开始）**。

## Knowledge Learned

- **from_chars 的 out-param 陷阱**：模板 helper 里把 parsed value 存入局部再忘记赋值给 out 是"合法代码、错误语义"的典型 bug；被 reprо 的"局部=1、外部=0"二分打印立刻锁定。
- **bad request ≠ bad response**：前者阻断可信链（execution error + 0-based index），后者进入诊断分类（CrcError/ProtocolError）。这是"工具自身故障"与"被诊断对象故障"的边界。
- **CRLF 兼容一行内解决**：按 `
` 切行 + trim 吃掉尾部 `
`，old-Windows 文件与 Git 的 LF 夹具产出完全相同的 ReplayLog（>> 相等断言锁定）。
- **lineNumber（1-based physical）↔ transactionIndex（0-based vector）**：给人看 vs 给程序定位，两个概念不混用。

## Potential Interview Questions

- 16 题见上 + 本阶段新增：为什么 request 要过 decodeReadHoldingRegistersRequest 这一层（T007 contract）、from_chars 为什么优于 atoi（完整消费 + 无未定义行为）、CRLF 如何兼容（切行后 trim）、解析脏数据为什么不 crash（variant 错误模型 + 行号）、Golden Replay 与 Demo Dashboard 同口径的意义（D1 架构承诺的可验证形式）。

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| Part A Learning / Test Design | `6159918` | `T009(Part A): Replay Log 格式与回放核心 — Learning / Test Design（docs-only）` |
| Part A Implementation | `e4920da` | `T009(Part A): implement versioned replay log and analysis core`（**新 LKGC**） |
| 归档回填 | `<docs-only HEAD>` | docs-only；LKGC 哈希由本提交写入 |

> LKGC = `e4920da`（Part A Implementation 代码提交）；Part B 未开始。

---

# Part B — Replay UI Integration：Learning / Test Design（2026-09-07，docs-only）

> 本章为 Part B 设计定案；**Implementation ⬜ 未开始**。红线：本阶段 `src/`、`tests/`、`CMakeLists.txt`、`scripts/` 零修改。

## PB-0 压缩后状态核验（先于一切）

从仓库事实重新读取并通过：T008 = DONE ✅；T009 Part A = DONE ✅；T009 Part B = NOT STARTED；T010 = NOT STARTED；LKGC = `e4920da`（与此一致）；HEAD = `7235e83`（Part A 归档 docs-only）。工作区 clean。全 docs 检索未见 `TXN|<elapsed>=0>` 类笔误（T009 档案 §`.mlog` v1 格式 中已为 `TXN|elapsed_ms|…` 且注明 `elapsed_ms >= 0`），无需修正。

## PB-1 Part B 核心目标与职责边界

Part B 只做三层事：**File I/O + Application orchestration + Presentation**。

```text
用户选择 .mlog
↓ QML FileDialog → QUrl
Qt/App 层读取文本（QFile）
↓ parseReplayLog()        (Part A Core)
↓ analyzeReplayLog()      (Part A Core)
ReplayBatchAnalysis
↓ 适配到现有 AnalysisController statistics + TransactionListModel
现有 Dashboard 展示
```

禁止重新实现：CRC / Frame decode / Function03 decode / Transaction classification / Statistics aggregation。

**单 Dashboard 原则**：Simulator Demo 与 Replay 数据来源不同，但最终都产出 `TransactionAnalysis` + `TransactionStatisticsSnapshot`，展示层完全共享：

```text
Simulator Demo ─┐
                ├→ AnalysisController → TransactionListModel → QML
Replay .mlog ───┘
```

禁止创建 ReplayStatisticsModel / ReplayTransactionModel / 第二套 Dashboard。

## PB-2 AnalysisController 继续作为 orchestration 层

不新建 ReplayManager / ReplayService hierarchy / QObject framework。在现有 AnalysisController 增加最小 Replay command/state：

```cpp
Q_INVOKABLE void loadReplayFile(const QUrl& fileUrl);
```

**清理 API 泛化定案：直接重命名 `clearDemo()` → `clearResults()`，不保留转发别名。** 理由：① `clearDemo` 在 Replay 加入后名字过窄；② 转发兼容会长期共存两个名字、误导读者以为有两套逻辑；③ 改动面极小（Main.qml Clear 按钮 1 处 + test_ui_bridge.cpp UI-B05/B06 两处调用点，断言语义零变化）；④ 清理逻辑只有一套，与新 API 一一对应。Main.qml 的 Clear 按钮改调 `clearResults()`。T008 档案中出现的 `clearDemo` 为历史事实，不修改历史章节，仅在本章记录更名。

## PB-3 FileDialog 属于 QML / App 层

- QML 层使用 `QtQuick.Dialogs.FileDialog`（Qt 6.11.1，**已在本机实证**：`D:/QT/6.11.1/mingw_64/qml/QtQuick/Dialogs/quickimpl/qml/FileDialog.qml` 存在；CMake 组件目录含 `Qt6QuickDialogs2`，即 target `Qt6::QuickDialogs2`）。
- Implementation 时再以真实 configure/build 实证 `find_package(Qt6 COMPONENTS … QuickDialogs2)` + QML `import QtQuick.Dialogs`；本轮不得凭记忆写 target/import——以上述实证结果为准。
- **Core（src/core/replay）不得 include QFileDialog/QFile/QUrl。**

## PB-4 File I/O 边界（定案流程）

```text
QML FileDialog → QUrl → AnalysisController::loadReplayFile(QUrl)
→ 确认 local file → QFile ReadOnly → QByteArray 全文
→ 临时 std::string_view（只覆盖 parseReplayLog 调用）
→ parseReplayLog() → ReplayLog（完全拥有自己的数据）
```

`parseReplayLog(std::string_view)` 仍是 pure Core API；QByteArray/string_view 生命周期只需覆盖该调用；**不得把 string_view 保存进 Controller/Core result**。

## PB-5 Replay File Error UI（错误状态归属）

```cpp
Q_PROPERTY(bool hasReplayError READ hasReplayError NOTIFY replayStateChanged)
Q_PROPERTY(QString replayErrorMessage READ replayErrorMessage NOTIFY replayStateChanged)
```

错误字符串属于 Qt Presentation Adapter，不是 Core：Core 只返回 `ReplayParseErrorCode` / `ReplayExecutionErrorCode`，Controller 负责 `enum + line/index → 用户可读 QString`（一组小型 adapter helper；不做国际化系统/错误码数据库/异常 hierarchy）。

## PB-6 Parse Error 映射（保留行号）

格式：`"Replay parse error at line %1: <phrase>"`。八码映射（小型 switch，缺 default 交由编译期检查穷举）：

| ReplayParseErrorCode | phrase |
| --- | --- |
| MissingHeader | "log header is missing" |
| UnsupportedVersion | "unsupported log version" |
| InvalidHeader | "invalid log header" |
| InvalidRecord | "invalid record" |
| InvalidElapsed | "invalid elapsed value" |
| InvalidHex | "invalid hex data" |
| MissingRequest | "request field is empty" |
| InvalidResponseField | "invalid response field" |

## PB-7 Execution Error 映射（0-based → 人类编号）

Core `transactionIndex` 为 0-based，**只在 Presentation 显示时 +1**（Core 索引不变）。格式：`"Replay analysis error at transaction %1: <phrase>"`（%1 = index+1）。

| ReplayExecutionErrorCode | phrase |
| --- | --- |
| InvalidRequestWire | "invalid request wire data" |
| InvalidRequestFunction | "unsupported function code in request" |
| InvalidRequestData | "invalid request data" |

示例：`{InvalidRequestData, transactionIndex=1}` → `"Replay analysis error at transaction 2: invalid request data"`。

## PB-8 加载失败不得发布半成品（重要 invariant，附策略定案）

`loadReplayFile` 顺序：读取完整文件 → parse 完整成功 → analyze 完整成功 → 得到完整 `ReplayBatchAnalysis` → **一次性发布** Dashboard + rows。file open 失败 / parse 失败 / analysis 失败时，不得出现统计更新一半/列表保留另一批/部分 Replay rows。

**失败策略定案：旧成功结果保持不变 + 显示 replay error。** 理由：一次失败的文件加载不应销毁用户刚得到的可用分析结果；错误信息告知原因后，用户可继续操作。此语义必须测试（UI-R03/R04/R05）。

## PB-9 成功后清旧 error

第一次 load invalid（hasReplayError=true）→ 随后 load valid：`hasReplayError=false`、`replayErrorMessage` 清空，正常发布新数据。同理 `Run Demo Batch` 切回 Simulator 时也清掉旧 Replay Error——UI 不得永远挂着过时错误。

## PB-10 mode/source state（来源可见性）

```cpp
Q_PROPERTY(QString modeLabel READ modeLabel NOTIFY sourceChanged)
Q_PROPERTY(QString sourceLabel READ sourceLabel NOTIFY sourceChanged)
```

- 初始 / Run Demo Batch：`modeLabel = "Simulator Mode"`，`sourceLabel = "Deterministic Demo"`（或空）。
- Replay 成功：`modeLabel = "Replay Mode"`，`sourceLabel = 文件 basename`（如 `demo_v1.mlog`；**不长期显示绝对路径**——内部如需保存完整路径，与 presentation basename 分开）。
- QML Header 当前硬编码 "Simulator Mode" 改为绑定 `analysisController.modeLabel`。

## PB-11 ReplayBatch → TransactionListEntry 适配（不建第二套）

直接映射到现有 `TransactionListEntry`（deviceAddress/functionCode/status/elapsedMs/exceptionCode）。不得修改 TransactionAnalysis，不得创建第二套 ListEntry：

| ListEntry 字段 | 来源 |
| --- | --- |
| deviceAddress | outcome.deviceAddress |
| functionCode | outcome.functionCode |
| status | outcome.analysis.status |
| elapsedMs | outcome.analysis.elapsed.count() |
| exceptionCode | outcome.analysis.exceptionCode |

## PB-12 Statistics 直接用 Replay Core 结果

Controller 调用 `applySnapshot(replayBatch.statistics)`（或等价）。**禁止 Controller 重新 summarize / 手工 count / 重算 successRate**——Part A 已保证 Replay statistics 来自 summarizeTransactions，UI 不需要第二次统计。

## PB-13 Golden Replay UI 期望（验收口径）

加载 `samples/demo_v1.mlog` → Dashboard：4/4/0、Success=1/Exception=1/CRC Error=1/Timeout=1/Protocol Error=0、Success Rate=25.0%、Avg Latency=25.0 ms。Rows：1/0x03/Success/25ms；1/0x03/Exception/18ms/Code 0x02；1/0x03/CRC Error/17ms；1/0x03/Timeout/1000ms。**与 T008 Demo 视觉结果相同，但来源必须不同（SimulatedSlave+Fault vs mlog 历史记录）——这是本任务重要验收点。**

## PB-14 Canonical Sample 定案（单一源头）

**方案 A 定案**：唯一 canonical 文件 = `samples/demo_v1.mlog`（Implementation 做最小迁移：`git mv tests/data/demo_v1.mlog → samples/demo_v1.mlog`，CMake configure_file 源路径、deploy 脚本、Manual Smoke 全部指向它）。测试 fixture 与部署 sample 共用同一文件，**避免 tests/data 与 samples 两份漂移**（方案 B 维护两份，不采纳）。禁止硬编码用户绝对路径：测试路径经 test-only compile definition 注入（沿用 Part A 的 MODBUSLENS_DEMO_MLOG_PATH 机制）。

## PB-15 Deployment Sample

`build/deploy/` 最终包含 `ModbusLens.exe` + … + `samples/demo_v1.mlog`。`deploy_windows.bat` 从 canonical `samples/` 复制业务 sample（windeployqt 不负责业务 sample 文件）。面试现场：Load Replay → 选 `samples/demo_v1.mlog` → 立即出现四结果 Dashboard。

## PB-16 clearResults 语义

无论当前来源（Simulator 或 Replay），`clearResults()` 后：statistics = empty snapshot、transaction model = empty、Replay error 清除。**mode/source 不强制切回 Simulator**：若当前是 Replay，仍显示 "Replay Mode" + 当前文件名——用户知道自己处于什么来源，只是结果被清空。**只有 Run Demo Batch 才明确切回 Simulator Mode。** 理由：Clear 表达"清结果"，不是"切来源"；两个动作正交，分别可预期。

## PB-17 Run Demo 与 Replay 切换（replace 语义）

Replay 成功 → mode=Replay；随后 Run Demo Batch → mode=Simulator、source label 更新、Replay error 清除、Demo 四行替换 Replay rows。反向同理。**不得 append**（恒 4 行，不是 4→8→12）。Dashboard 永远只显示当前 active batch；当前不做多 session/history。

## PB-18 UI Controls

现有 `Run Demo Batch`、`Clear` 之外新增 `Load Replay...`，顺序：`Run Demo Batch | Load Replay... | Clear`。QML FileDialog 仅用于选择文件；不做 Drag&Drop / Recent Files / Folder history / File watcher。

## PB-19 Replay Error Presentation

Main.qml 增加轻量错误区域：仅 `analysisController.hasReplayError` 时可见，显示 `analysisController.replayErrorMessage`（Text/Label + 轻微 error styling，如错误色）。不引入 MessageDialog framework / Toast manager / notification system。

## PB-20 UI 测试矩阵（UI-R01~R08；全部经 Controller 直接调用，不自动化点击 FileDialog）

| Test ID | 场景 | Expected | Priority |
| --- | --- | --- | --- |
| UI-R01 | `loadReplayFile(QUrl::fromLocalFile(…))` 加载 canonical demo_v1.mlog | modeLabel="Replay Mode"、sourceLabel="demo_v1.mlog"、hasReplayError=false；statistics 4/4/0、1/1/1/1/0、0.25、25.0；rowCount=4 | **P0** |
| UI-R02 | 校验四行内容 | 顺序 Success/Exception/CrcError/Timeout；elapsed 25/18/17/1000；Exception 行 hasExceptionCode=true、code=2 | **P0** |
| UI-R03 | 先 load golden，再 load 含 `GG` 的坏文件（测试内 QTemporaryDir 构造） | hasReplayError=true；message 含 line number + invalid hex 语义；**旧 batch 保持**（rowCount 仍 4、statistics 仍 golden） | **P0** |
| UI-R04 | load 含 `01 03 00 00 00 00 45 CA`（quantity=0）的文件 | hasReplayError=true；message 表达 transaction 1 + invalid request data；旧成功 batch 保持不变 | **P0** |
| UI-R05 | load 不存在的 local file | hasReplayError=true；message 表达无法读取文件；不 crash、不清空旧 batch | P1 |
| UI-R06 | 先 load invalid（error=true）再 load golden | hasReplayError=false、message 空、Replay Dashboard 正常 | **P0** |
| UI-R07 | load replay → runDemoBatch → load replay | 每次仍 4 行（不 4→8→12）；mode 依次 Replay→Simulator→Replay | **P0** |
| UI-R08 | load replay → clearResults | 所有计数 0、rowCount 0、optional null 语义；modeLabel 仍 "Replay Mode"、sourceLabel 仍文件名（按 PB-16 定案） | P1 |

## PB-21 FileDialog 自动化边界

不为 Windows native FileDialog 写脆弱 GUI click automation。自动化=Controller.loadReplayFile(QUrl) 全覆盖 + QML module load smoke；**Manual UI Smoke 真正点 Load Replay... 人工选择文件**。不引入 Squish / WinAppDriver / 复杂 UI automation framework。

## PB-22 QML Smoke 与 CMake

加入 `QtQuick.Dialogs` import 与 `Qt6::QuickDialogs2` 后，现有 `--qml-smoke-test` 必须继续 PASS（本机 Qt 6.11.1 已实证 module/组件存在，见 PB-3）。Implementation 重点检查 module unavailable / FileDialog type unavailable / ReferenceError / TypeError / binding loop。不得引入 Qt6::Widgets。

## PB-23 Core Zero Qt 保证

Replay File I/O 只允许出现在 AnalysisController / App 层；`src/core/replay` 继续 Zero Qt（Regression 检查延续 Part A 的 grep 手段）。

## PB-24 Manual Replay Smoke（Implementation 后、自动化全过后）

clean build + full ctest + qml smoke + deploy regression 全 PASS 后，运行 `build/deploy/ModbusLens.exe`，用户人工验证：
A. 初始 / Demo 功能仍正常；B. 点 `Load Replay...` 选 `samples/demo_v1.mlog` → Header "Replay Mode" + "demo_v1.mlog"，Dashboard 4/4/0、1/1/1/1/0、25.0%、25.0 ms，四行 Success 25ms / Exception 18ms Code 0x02 / CRC Error 17ms / Timeout 1000ms；C. 点 Clear → 结果清空（mode 仍 Replay）；D. 再 Run Demo → 切回 Simulator Mode、四条 Demo 正常；E. 再 Load Replay → 替换不追加。
如 Agent 无法可靠操作 FileDialog：**必须 WAITING FOR USER CONFIRMATION，不得自报 PASS** —— 本项目判断：native FileDialog 交互不可可靠自动化，Manual Replay Smoke 一律 WAITING FOR USER。

## PB-25 Standalone Deployment Regression

`deploy_windows.bat` 重跑：build/deploy 含最新 exe、QML、Qt Quick Dialog runtime、`samples/demo_v1.mlog`；minimal-PATH `ModbusLens.exe --qml-smoke-test` exit=0；ISSUE-002 不得回归。

## PB-26 Part B Scope Red Lines

禁止：real-time playback / QTimer playback / sleep / speed slider / pause-resume / seek / drag-drop / recent file database / filesystem watcher / Serial / AI / Agent / IFrameSource。禁止修改 T002~T007 Core 业务语义；非真实 bug，不得因 UI 需求扩张 Replay Core（Part A）。

## PB-27 Knowledge I Must Be Able To Explain（16 题）

**PB-Q1 为什么 Replay UI 复用现有 Dashboard？** 两种来源最终都产出 TransactionAnalysis + TransactionStatisticsSnapshot，展示层共享即"同一事实一份 UI"；另建第二套卡片/模型只会滋生口径漂移。
**PB-Q2 为什么 File I/O 不能放进 Replay Core？** Core 三元组承诺：Zero Qt、可单测（string_view 进值出，无文件系统）、三模式复用。文件系统访问是 App 编排职责。
**PB-Q3 QFile/QUrl 为什么可以出现在 Controller？** Controller 本就是 Qt Presentation Adapter 层（ADR001：Qt 类型仅允许于此）；它负责把 file bytes 转换成文字后塞给 pure Core API。
**PB-Q4 为什么 ReplayAnalysisResult 不直接返回 QString error？** Core 若持 QString 就破坏了 Zero Qt，且错误文案是展示事务；enum+line/index 是稳定数据契约，文案可随时改而不动 Core。
**PB-Q5 lineNumber 和 transactionIndex 在 UI 怎么展示？** parse 错误显示 "line N"（1-based physical，直接可用）；execution 错误显示 "transaction N"（0-based index 在展示层 +1）。Core 索引不动。
**PB-Q6 为什么加载失败不能发布半成品 batch？** 原子性：要么完整成功一起换，要么不动。半成品（统计一半/列表另一批）会给用户一个无法解释的状态，也无法回放复现。
**PB-Q7 为什么推荐保留旧成功结果而显示 error？** 失败的加载是"新尝试失败"，不是"旧结果失效"；销毁旧结果会让一次误选文件毁掉之前的工作。错误信息单独呈现即可。
**PB-Q8 为什么 valid load 后必须清除旧 error？** 错误是加载结果的属性，不是 UI 的永久状态；成功后仍挂着旧 error 会误导（"上次失败"被误读为"这次也失败"）。
**PB-Q9 Simulator Mode 与 Replay Mode 怎么切换？** Run Demo 与 Load Replay 各自发布成功后设置 mode/source；两者互相 replace（从不清空为中间态）。Clear 只清结果不切来源。
**PB-Q10 为什么 Demo 与 Replay 都是 replace semantics？** Dashboard 语义始终是"当前 active batch"；append 会把两次不同来源的数据混成一套统计，破坏口径一致性。
**PB-Q11 为什么 Replay statistics 不在 Controller 重算？** 重算=第二份统计逻辑=口径漂移风险；Part A 已保证 statistics 来自 summarizeTransactions，Controller 直接 apply。
**PB-Q12 为什么 Replay row 可以复用 TransactionListEntry？** 列表行的显示事实（设备/功能/状态/耗时/异常码）与来源无关；Outcome 已经携带同构字段，映射即可。
**PB-Q13 为什么 sample log 应只有一个 canonical source？** 两份 golden data 长期必然漂移（改一份忘另一份），tests+deployment+manual smoke 共用一份，漂移面=0。
**PB-Q14 为什么不自动化点击 native FileDialog？** Windows native dialog 不在 QML 可达性树内，GUI click automation 脆弱且引入重型依赖；等价自动化（Controller.loadReplayFile(QUrl)）+ 人工 Smoke 已覆盖两端。
**PB-Q15 为什么 Replay 不需要 Timer？** Analytical Replay：elapsed 是历史事实直接传给分析器，不真实等待；Timer 属于 real-time playback（明确排除在外）。
**PB-Q16 T009 如何证明 UI 已与数据来源解耦？** 同一个 Dashboard 不经任何改动同时呈现 Simulator 现场生成与 Replay 历史加载的两批数据（UI-R07 切换测试 + Manual Smoke D/E），且两批统计同口径（Demo 4/4/0 与 Golden Replay 4/4/0 相等）。

## PB-28 Implementation Plan（23 步）

1. 定案 canonical sample 位置：`samples/demo_v1.mlog`
2. `git mv tests/data/demo_v1.mlog samples/demo_v1.mlog`
3. 更新 Replay tests fixture 路径（CMake configure_file 源路径）；ui_bridge 测试经 test-only compile definition 获得路径
4. Controller 增加 loadReplayFile
5. Controller 增加 replay error / source state（hasReplayError/replayErrorMessage/modeLabel/sourceLabel）
6. 清理 API 泛化：clearDemo() 重命名 clearResults()（Main.qml + UI-B05/B06 同步）
7. ReplayBatch → existing model adapter（TransactionListEntry 映射）
8. QML 增加 Load Replay FileDialog（QtQuick.Dialogs + QuickDialogs2 实证）
9. Header mode/source 动态显示
10. QML 增加 replay error label
11. UI-R01~R08 tests（RED）
12. RED 证据记录
13. GREEN（实现全部）
14. clean build 0 warnings
15. full ctest（期望 16 targets 不变 + ui_bridge 内新增用例）
16. qml smoke
17. Core Zero Qt 复查
18. deploy_windows.bat 更新 sample 复制
19. minimal-PATH deploy smoke
20. Manual Replay Smoke → WAITING FOR USER CONFIRMATION（Agent 不可自报 PASS）
21. code commit
22. LKGC 推进（docs 回填）
23. docs confirmation backfill（用户确认后）

## PB-29 PROJECT_STATUS 更新（本阶段执行）

Current Task = T009 Replay Mode；Current Part = Part B — Replay UI Integration；Current Phase = Learning / Test Design；Next Action = T009 Part B — Implementation；Next Task After T009 = T010 Serial Mode；T009 overall = IN PROGRESS；Milestone 按 BACKLOG 现有定义（M5）不变。

## PB-30 docs-only 验证（本阶段执行）

`git diff --check`；确认 `src/`、`tests/`、`CMakeLists.txt`、`scripts/` 零修改（以 git status 为准）；独立 docs-only commit；不推进 LKGC；不 git push。
