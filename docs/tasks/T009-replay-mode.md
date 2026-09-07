# T009 — Replay Mode

> 状态：**IN PROGRESS**｜Part A（Replay Log Format + Replay Core）：**DONE ✅**（Learning / Test Design + Implementation + 全量验证）｜Part B（Replay UI Integration）：⬜ Not Started
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
