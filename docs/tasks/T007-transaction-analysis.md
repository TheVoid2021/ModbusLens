# T007 — Transaction Analysis

> 状态：**DONE ✅**（2026-09-06）｜Part A（Single Transaction Analysis）：**DONE ✅**｜Part B（Statistics Snapshot）：**DONE ✅**（Learning / Test Design + Implementation，RED→GREEN 全程留痕）
> 前置确认：T006 DONE、M3 DONE。
> 交付边界回顾：Part A 单事务分析 + Part B batch→snapshot 纯函数统计；**未实现** mutable accumulator、rolling window、history store、database、p95/p99、charts、Qt model、QML、Agent；T008 未开始。

## Implementation 前追加规则（2026-09-06，定案）

**A. elapsed 对所有 TransactionStatus 原样保留**：Pending/Success/Exception/CrcError/Timeout/ProtocolError 六种状态的 `TransactionAnalysis.elapsed` 都等于调用方传入的 elapsed；Analyzer 不自行修改或重新测量。

**B. exceptionCode invariant**：仅当 `status == TransactionStatus::Exception` 时 `exceptionCode` 有值；其余状态（Success/Pending/Timeout/CrcError/ProtocolError）一律 `std::nullopt`。

**C. Part A 只保留异常码数值**（如 0x02）：不在 Transaction Analyzer 中转换为 "Illegal Data Address" 等文字——映射继续属于未来 Presentation / Diagnosis 层。

**防御性解码分支（补充）**：Normal Response 路径为取 quantity 会调用 `decodeReadHoldingRegistersRequest(request)`——虽然契约上 request 合法，若该防御性调用意外失败，映射为 `ProtocolError`（不崩溃、不新增第七个状态）。RtuDecodeError 分支采用**无 default 的穷举 switch**：新增枚举值时 `-Wswitch` 会报警（不静默吞新状态），switch 后保留显式兜底 return 保证函数必然返回。

## Implementation 前追加规则（结束）

## Goal

交付**单条事务的分析器**：给定一个 Function 0x03 Request、一次 Response/Failure Observation、elapsed 与 timeout threshold，产出一个结构化的事务结果（状态 + 时延 + 可选异常码）——把 T002–T006 的"帧正确性"升级为"事务正确性"，这是协议库变成诊断平台的第一步。

## Background

- 此前所有任务判断的都是**单个对象**：帧合法性（T003/T004A）、字段语义（T004B）、设备行为（T005）、传输故障（T006）。
- 诊断的核心问题是**跨对象**的："这个 Response 是否正确回答了那个 Request？"——包括格式合法但答非所问的情况（数量不符、地址不符、功能不符）。
- 为控制复杂度，T007 拆分：Part A 单事务分析（本阶段设计），Part B 统计快照（后续独立设计）。

## Scope（Part A）

**只实现**：一次 Function 0x03 Request + 一次 Response Observation + elapsed + timeout threshold → 一条事务分析结果。

**禁止**（不属于 Part A）：SessionManager、queue、polling loop、QTimer、thread、serial、QML model、database、history persistence、statistics accumulator（Part B）、dashboard、Agent。

**范围收缩记录**：原 BACKLOG T007 行的"超时判定"保留在 Part A（NoResponse + threshold → Timeout 状态）；"统计快照"移入 Part B；real timer/polling manager/session manager/database 本就未列入，现显式写为禁止项；broadcast 语义（地址 0 请求无需响应的事务口径）推迟到 Session/Runtime 层（与 T005 决策一致），Part A 只分析调用方交付的配对输入。

## 什么叫 Transaction（核心定义）

**Transaction 不是单个 Frame**。Transaction = **Request + 对应的 Response / Failure Observation**：

```text
Request:  device=1, function=03, quantity=2
Response: device=1, function=03, values={100,200}
        ────────────────────────────────────
合起来才是一条事务
```

- T004 判断的是：**单 Frame 自身是否合法**（长度、CRC、字段格式）；
- T007 判断的是：**Response 是否能正确回答 Request**——跨帧一致性（地址、功能、数量）只有在这里才可能、也必须校验。

## RTU Pairing 的基本概念

Modbus RTU Frame **没有 Transaction ID**（TCP 的 MBAP 才有）。ModbusLens v1 采用**典型串行主站模型**：

```text
发送一个 Request → 等待对应 Response → 事务完成 → 再发下一个
```

因此 Part A **不设计**多请求并发匹配表；只分析调用方已经认为属于同一次等待窗口的 Request + Observation，然后校验：slave address、function code、Function03 请求/响应语义一致性。真正的请求调度属于未来 runtime/session 层（配对策略的扩展点也留在那里）。

## TransactionStatus（定案）

```cpp
enum class TransactionStatus {
    Pending,        // 仍未收到响应，但 elapsed < timeoutThreshold
    Success,        // 收到合法、匹配的正常响应
    Exception,      // 收到合法且匹配的 Modbus Exception Response
    CrcError,       // 收到 wire，但 wire decode 因 CRC mismatch 失败
    Timeout,        // 没有响应，且 elapsed >= timeoutThreshold
    ProtocolError,  // 收到数据，但无法成为本 Request 的合法匹配响应
};
```

ProtocolError 的典型来源：FrameTooShort、response address mismatch、response function mismatch、malformed Function03 response、返回寄存器数 ≠ 请求数量、malformed exception response。

## ResponseObservation 模型（定案）

```cpp
struct NoResponse {
    bool operator==(const NoResponse&) const = default;
};

using ResponseObservation = std::variant<
    ModbusRtuFrame,     // 已成功通过 T004A wire decode
    RtuDecodeError,     // 收到 wire，但 wire decode 失败
    NoResponse          // 没有任何响应被交付
>;
```

**不把 `DroppedResponse`（T006 类型）耦合进 Transaction API**：T007 应处理抽象的"No Response observation"，而不是只认识 Simulator/T006 的具体故障类型——未来 Serial 超时、用户手动取消同样会自然产生 NoResponse。转换（T006 的 DroppedResponse → NoResponse）由调用方/集成层完成。

## TransactionAnalysis 模型（定案）

```cpp
struct TransactionAnalysis {
    TransactionStatus status{};
    std::chrono::milliseconds elapsed{0};
    std::optional<std::uint8_t> exceptionCode;   // 仅 status == Exception 时有值

    bool operator==(const TransactionAnalysis&) const = default;
};
```

`returnedRegisterCount` **暂不加入**：当前测试与已规划 UI 均无真实消费者（values 数量一致性校验发生在 analyzer 内部，不必外泄）；Part A 保持最小，未来有需求再加（结构上无破坏）。

## 推荐 API（定案）

```cpp
TransactionAnalysis analyzeFunction03Transaction(
    const ModbusRtuFrame& request,
    const ResponseObservation& observation,
    std::chrono::milliseconds elapsed,
    std::chrono::milliseconds timeoutThreshold);
```

- Pure C++20、零 Qt、无真实等待、**无内部时钟**；
- 调用契约：`request` 是已构造/验证过的 Function 0x03 Request（生产路径来自 T004B decoder 成功产物）；
- 防御性校验讨论：对 request 再跑一次 `decodeReadHoldingRecordsRequest` 属廉价防御，但失败时没有合适的 TransactionStatus 可表达（会污染六状态语义）——**定案：v1 记录契约、不做 request 侧防御分支**，聚焦 response/transaction outcome；未来若需要，走独立断言/日志而非新增状态。

## 判定规则

### 1. NoResponse → Pending / Timeout

| elapsed | threshold | 结果 |
| --- | --- | --- |
| 800ms | 1000ms | **Pending** |
| 1000ms | 1000ms | **Timeout**（边界：`>=` 即超时） |
| 1200ms | 1000ms | **Timeout** |

明确：**Timeout 不是 Modbus Exception**——它是等待方基于 `NoResponse + elapsed >= threshold` 做出的事务判断（T006 已铺垫，此处落地）。

### 2. Wire Decode Error → CrcError / ProtocolError

- `RtuDecodeErrorCode::CrcMismatch` → **CrcError**（产品明确需要单独统计的诊断类别）；
- `RtuDecodeErrorCode::FrameTooShort` → **ProtocolError**（wire malformed 归并，Part A 不创建过多状态）。

### 3. Response Address Pairing

`response.address == request.address`，否则 **ProtocolError**（Request device=1 / Response device=2 不能判 Success）。

### 4. Function Pairing

对 functionCode=0x03 的请求：合法正常响应 = **0x03**；合法异常响应 = **0x83**；其他（0x04、0x84、0x06…）→ **ProtocolError**。不要把任意带 0x80 的功能码都当作本请求的异常。

### 5. Exception Response 分析

functionCode == 0x83 时**必须复用** `decodeReadHoldingRegistersException(response)`（禁手写 `data[0]`）：成功 → `Exception` + `exceptionCode = 数值`（如 0x02；文字映射仍归 Analysis/Presentation）；decoder 失败 → **ProtocolError**。

### 6. Normal Response 分析

functionCode == 0x03 时**必须复用** `decodeReadHoldingRegistersRequest(request)`（取 quantity）与 `decodeReadHoldingRegistersResponse(response)`（取 values）：response decoder 失败 → **ProtocolError**。禁止在 Transaction 层重复手写 byteCount/uint16 parser。

### 7. Quantity Consistency（首次真正的跨帧校验）

```text
Request  quantity = 2, Response values = {100,200}     → Success
Request  quantity = 2, Response values = {100,200,1500} → ProtocolError
```

Response 自身格式完全合法，但 `values.size() != quantity`——这就是该检查**不能放 T004B**的原因：单帧视角根本不知道请求要几个。

### 8. Elapsed / Latency

`elapsed` 由调用方作为**事实**传入（如 25ms）；收帧事务保存该值，Pending/Timeout 同样保存当前 elapsed。Part A 不使用 `steady_clock::now()` / sleep / QElapsedTimer / QTimer——计时归未来 Runtime/Session，Analyzer 只消费 duration。

## Test Design

| Test ID | 场景 | Expected | Priority |
| --- | --- | --- | --- |
| TX-A01 | Success：req{1,03,start=0,qty=2} + resp{1,03,{04,00,64,00,C8}}，elapsed=25ms/th=1000ms | Success，elapsed=25ms，exceptionCode 空 | **P0** |
| TX-A02 | Exception：resp{1,0x83,{0x02}} | Exception，exceptionCode=0x02 | **P0** |
| TX-A03 | Observation=RtuDecodeError{CrcMismatch} | CrcError | **P0** |
| TX-A04 | NoResponse，elapsed=800ms/th=1000ms | Pending | **P0** |
| TX-A05 | NoResponse，elapsed=1000ms 与 1200ms/th=1000ms | Timeout（两种都） | **P0** |
| TX-A06 | Response device=2（其余合法） | ProtocolError | **P0** |
| TX-A07 | Response function=0x04（其余合法） | ProtocolError | **P0** |
| TX-A08 | Malformed normal response{1,03,{04,00,64}}（byteCount 不自洽） | ProtocolError | **P0** |
| TX-A09 | Quantity mismatch：resp{1,03,{06,00,64,00,C8,05,DC}}（3 值 ≠ qty 2） | ProtocolError | **P0** |
| TX-A10 | Malformed exception{1,0x83,{}} | ProtocolError | P1 |
| TX-A11 | Observation=RtuDecodeError{FrameTooShort} | ProtocolError | P1 |
| TX-A12 | Success + elapsed=37ms | TransactionAnalysis.elapsed == 37ms（不实际等待） | P1 |

优先级：P0 = A01–A09（六大状态 + 三个跨帧校验维度的最小覆盖）；P1 = A10–A12。

## Integration Test

| Test ID | 流程 | Expected | Priority |
| --- | --- | --- | --- |
| **TX-I01** | Request → SimulatedSlave → Response Frame → Analyzer | Success，且 quantity 与返回 values 数量一致 | **P0** |
| **TX-I02** | SimulatedSlave → encode → T006 DropResponse →（转 NoResponse）→ Analyzer 两次：elapsed=500ms/th=1000ms → **Pending**；elapsed=1000ms/th=1000ms → **Timeout** | 证明 T006 的 DropResponse 不直接等于 Timeout，而是经 T007 "NoResponse + threshold → Timeout" | **P0** |
| **TX-I03** | SimulatedSlave → encode → T006 CorruptCrc → decodeRtuFrame → RtuDecodeError{CrcMismatch} → Analyzer | CrcError | **P0（纳入）** |

I03 纳入 P0 的决定：它与 I01/I02 构成"成功 / 超时 / CRC 错"三种事务结局的完整闭环，实现成本仅多一条断言链，价值显著。不再扩大更多场景。

## Part B 边界（一句话）

Part B（后续独立任务）只负责 **Statistics Snapshot**——把一批已完成的 `TransactionAnalysis` 聚合成 completed/success/exception/crcError/timeout/protocolError 计数、成功率与延迟摘要；不涉及 database、chart、Qt model、persistent history。

## Knowledge I Must Be Able To Explain（14 题）

**TX-Q1. Frame 和 Transaction 有什么区别？** Frame 是单条消息；Transaction = Request + 对应的 Response/Failure Observation。前者问"这帧合法吗"，后者问"这次问答成功了吗"。
**TX-Q2. 为什么 T004 不能判断 quantity mismatch？** T004B 一次只见一帧；quantity 是请求侧事实，响应侧无法知晓——跨帧一致性必须在同时看到两帧的层执行。
**TX-Q3. Modbus RTU 为什么不能靠 Transaction ID 配对？** RTU 帧（ unlike TCP MBAP）没有事务标识字段；串行总线的典型模型是"一发一收再发"，靠时序窗口而非 ID 配对。
**TX-Q4. NoResponse 和 Timeout 有什么区别？** NoResponse 是观察（没有响应被交付）；Timeout 是判断（观察 + elapsed≥threshold）。同一 NoResponse 在阈值前是 Pending。
**TX-Q5. Pending 和 Timeout 的边界是什么？** `elapsed >= timeoutThreshold` 即 Timeout（1000/1000 → Timeout），之前是 Pending；边界值划给 Timeout 保证"超时必然被报告"。
**TX-Q6. 为什么 CRC Error 要单独分类？** 它是诊断的关键类别（指向线路/干扰/波特率问题），与"帧太短"等协议错误的原因和处置完全不同——混在一起会丢掉统计口径。
**TX-Q7. 为什么 FrameTooShort 当前归 ProtocolError？** v1 状态集最小化：FrameTooShort 是 wire malformed 的一种，暂时没有独立的统计消费者；未来需要时再拆状态（枚举扩展不破坏结构）。
**TX-Q8. 为什么 Response address 必须等于 Request address？** 总线上多设备并存，别人的回复不能算我的事务结果；地址不符意味着配对错了，属于协议错误而非成功。
**TX-Q9. 为什么 0x83 才是 0x03 的匹配 Exception？** 异常响应 = 原功能码 | 0x80；0x84 是 0x04 的异常。乱认会把别的请求的异常算到本事务头上。
**TX-Q10. 为什么 Response decoder 必须复用 T004？** 同一事实只允许一份实现（T004 决策延续）；复用自动继承 B06–B12 的全部格式保证，Transaction 层专注跨帧规则。
**TX-Q11. 为什么 values.size 要和 request.quantity 比较？** 这是"答非所问"的典型形态：格式合法但内容与请求不符——只有事务层能发现，且正是现场排障的高频问题。
**TX-Q12. 为什么 elapsed 由外层传入，而不是 Analyzer 自己计时？** Analyzer 必须是纯函数（同输入同输出、测试零等待）；计时依赖时钟与等待，属于 Runtime/Session 的职责。
**TX-Q13. Exception 与 ProtocolError 有什么区别？** Exception = 设备**明确回答了**"你的请求我不能照办"（合法 Modbus 异常帧，含 exceptionCode）；ProtocolError = 收到的数据**构不成**对本请求的有效回答（配对错、格式坏、数量不符）。前者是设备的意见，后者是通信的失败。
**TX-Q14. T007 如何把协议库变成诊断平台？** 协议库回答"字节是什么意思"（T002–T006）；事务层回答"这次通信发生了什么、属于哪类问题"（Success/Exception/CrcError/Timeout/ProtocolError）——有了这个分类，统计、诊断规则、报告才有了原材料。

## Implementation Plan（Part A 下一阶段）

1. 创建 `src/core/analysis/TransactionAnalysis.h`（NoResponse、ResponseObservation、TransactionStatus、TransactionAnalysis、analyzer 声明）；
2. 创建 `TransactionAnalysis.cpp`（按判定规则顺序实现）；
3. 写 TX-A01~A12 测试（`tests/test_transaction_analysis.cpp`）；
4. 写 TX-I01~I03 测试（`tests/test_transaction_integration.cpp`）；
5. CMake：两个 target + ctest `transaction` / `transaction_integration`；
6. RED（linker error，如实记录）；
7. 实现至 GREEN；
8. clean build + full ctest（11/11 预期：既有 9 + 2，按实际记录）；
9. 文档归档；code commit；推进 LKGC；docs backfill（如需要）。

**禁止**：Session/queue/线程/timer/统计/持久化；不修改 T002–T006 任何既有代码。

## Implementation（实录，2026-09-06）

### 新增文件

- `src/core/analysis/TransactionAnalysis.{h,cpp}`：`TransactionStatus` 六值、`NoResponse`、`ResponseObservation`、`TransactionAnalysis`（defaulted `==`）、`analyzeFunction03Transaction`；落点 `src/core/analysis/`（Analysis 是 Protocol 的**上层**，依赖方向 analysis → protocol，禁止反向）。
- `tests/test_transaction_analysis.cpp`：TX-A01~A12，12 个测试函数。
- `tests/test_transaction_integration.cpp`：TX-I01~I03（T005→T007 语义直连；T006 Drop→NoResponse 适配；T006 CorruptCrc→decode→CrcError）。
- `CMakeLists.txt`：core 加入 `TransactionAnalysis.cpp`；两个测试 target + ctest `transaction` / `transaction_integration`。

### 实际分析顺序（与设计一致，按 observation 类型先分支）

```text
ResponseObservation
    ├─ NoResponse      → elapsed < threshold ? Pending : Timeout
    ├─ RtuDecodeError  → 穷举 switch（无 default）：CrcMismatch→CrcError；FrameTooShort→ProtocolError
    └─ ModbusRtuFrame  → ① address 配对（≠→ProtocolError）
                         ② 0x83 → decodeReadHoldingRegistersException → Exception + 数值码（失败→ProtocolError）
                         ③ 0x03 → decodeReadHoldingRegistersRequest/Response（防御失败→ProtocolError）
                                   → 数量一致性（uint16→size_t 显式提升后比较，≠→ProtocolError）→ Success
                         ④ 其他功能码 → ProtocolError
```

三条规则落实：**A** `makeAnalysis` 单一漏斗保证 elapsed 六状态原样保留；**B** exceptionCode 仅 Exception 分支赋值（其余路径 nullopt）；**C** exceptionCode 只存数值。RtuDecodeError 分支无 default——新增枚举值时 `-Wswitch` 报警而非静默吞掉，switch 后保留显式兜底 return 保证函数 total。

## Part B — Statistics Snapshot Design（Learning + Test Design，本阶段定稿）

### 职责

- Part A：一个 Transaction → 一个 TransactionAnalysis；
- Part B：**一批 TransactionAnalysis → 一个 TransactionStatisticsSnapshot**。

Part B 只回答"这一批事务**现在**呈现什么统计特征"。它**不是** Transaction Manager / Session Manager / History Store / Database，也不是实时计时器——保持纯函数：输入一批分析结果，一次计算一个快照。

### 三个计数概念（输入可包含 Pending）

实际 Dashboard 的当前集合会同时存在已完成与未完成事务，因此输入允许包含所有 `TransactionAnalysis`：

```text
observedCount  = 输入总数
pendingCount   = status == Pending 的数量
completedCount = observedCount - pendingCount
               = Success + Exception + CrcError + Timeout + ProtocolError
```

### successRate 的严格定义

```text
successRate = successCount / completedCount      （Pending 不进入分母）
```

例：Success=8, Exception=1, Timeout=1, Pending=90 → observed=100, completed=10 → **80%**，而不是 8/100=8%。原因：Pending 事务尚未产生最终结果，**不能提前当失败**。

### completedCount == 0 → successRate = nullopt

- 输入为空或全部 Pending 时 `completedCount = 0`，成功率没有数学意义；
- **不除以 0，也不返回 0%**——0% 会被理解成"已经完成但没有一次成功"，而真实含义是"**目前还没有完成事务**"；
- 因此 `successRate` 类型为 `std::optional<double>`：completedCount==0 → `nullopt`；>0 → successCount/completedCount。

### Latency Summary 的 v1 定义

只统计**成功事务延迟**：`std::optional<double> averageSuccessLatencyMs`。

- Success 的 elapsed = 一次成功响应完成所需时间，语义清晰；
- Pending/Timeout/Exception/CrcError/ProtocolError 的 elapsed **不混入**——尤其 Timeout 的 elapsed 只表示"等待阈值已经达到"，拉进来会虚高"成功响应平均延迟"；
- successCount==0 → `nullopt`；>0 → 所有 Success.elapsed 的算术平均（毫秒）。例：10/20/30ms → 20.0；
- **暂不做** min/max/median/p95/p99/标准差——有真实需求再加。

### 数据模型（定案，不实现）

```cpp
struct TransactionStatisticsSnapshot {
    std::size_t observedCount{};
    std::size_t pendingCount{};
    std::size_t completedCount{};

    std::size_t successCount{};
    std::size_t exceptionCount{};
    std::size_t crcErrorCount{};
    std::size_t timeoutCount{};
    std::size_t protocolErrorCount{};

    std::optional<double> successRate;
    std::optional<double> averageSuccessLatencyMs;

    bool operator==(const TransactionStatisticsSnapshot&) const = default;
};
```

**不增加**：QString、QVariant、chart series、device name、timestamps、history vector。

### API（定案）

```cpp
TransactionStatisticsSnapshot summarizeTransactions(
    std::span<const TransactionAnalysis> transactions);
```

纯 C++20、无 Qt、无持久状态。输入变化 → 重新调用重算即可；**不创建** StatisticsManager / StatisticsAccumulator / Singleton / QObject。

### Snapshot Invariants（实现后必须测试锁定）

- **A**：`observedCount == pendingCount + completedCount`
- **B**：`completedCount == successCount + exceptionCount + crcErrorCount + timeoutCount + protocolErrorCount`
- **C**：`successRate` 有值 ⟺ `completedCount > 0`
- **D**：`averageSuccessLatencyMs` 有值 ⟺ `successCount > 0`

对 QML 的价值：UI 不需要猜字段之间是否一致——快照自洽。

### Status Counting（实现约定）

遍历 + **穷举 `switch(TransactionStatus)`**（无 if-else 串、无 `map<TransactionStatus,int>`）：直接累加到 struct 字段——enum 固定且字段明确，显式字段最容易被 QML 暴露，新增枚举值时编译器强制处理。

### 浮点测试规则

1.0、20.0 这类可精确值直接断言；`2.0/6.0` 用 QtTest 的浮点容差比较（`qFuzzyCompare` 语义），不断言字符串/百分比格式。Core 返回数学值；"33.33%" 这类**展示格式属未来 QML/Presentation**。

### Part B 测试矩阵

| Test ID | Input | Expected | Why This Test Exists | What Bug It Can Catch | Priority |
| --- | --- | --- | --- | --- | --- |
| STAT-B01 | `{}`（空） | observed/pending/completed 全 0，successRate=nullopt，avgLatency=nullopt | 空输入是最小契约点；nullopt 语义的锚 | 除零、把空当 0% | **P0** |
| STAT-B02 | Success 10/20/30ms | observed=3, completed=3, success=3, rate=1.0, avg=20.0 | 全成功基线 + 平均值计算 | 平均算错（和/个数）、rate 上限 >1 | **P0** |
| STAT-B03 | Success 10/30 + Exception 20 + CrcError 15 + Timeout 1000 + ProtocolError 12 | observed=6, completed=6, 五分类 2/1/1/1/1，rate=2/6，avg=20.0（**Timeout 1000ms 不进平均**） | 混合状态分箱 + 延迟隔离 | 分箱漏状态、非 Success elapsed 混入平均 | **P0** |
| STAT-B04 | Success 25ms + Pending 500ms + Pending 800ms | observed=3, pending=2, completed=1, success=1, **rate=1.0（不是 1/3）**, avg=25.0 | Pending 不进分母的核心语义 | 分母用 observed、Pending 当失败 | **P0** |
| STAT-B05 | Pending ×2 | observed=2, pending=2, completed=0, rate=nullopt, avg=nullopt | "尚无最终结果" ≠ "成功率 0%" | 用 0.0 冒充未定义 | **P0** |
| STAT-B06 | Exception + CrcError + Timeout + ProtocolError | completed=4, success=0, **rate=0.0**, avg=nullopt | 与 B05 的关键对比：有 completed 且 0 success 才是 0% | nullopt/0.0 语义混淆 | **P0** |
| STAT-B07 | Success 10/30 + Exception 200 + CrcError 400 + Timeout 1000 + ProtocolError 500 | avg=20.0（仅 Success 进入） | 成功延迟隔离性 | 所有 elapsed 一锅炖 | **P0** |
| STAT-B08 | 混合批次（复用 B03 数据） | Invariant A：observed==pending+completed；Invariant B：completed==五分类之和 | 快照自洽性护栏（未来 QML 不用猜） | 计数器更新路径不一致 | P1 |

优先级：P0 = B01–B07；P1 = B08（不变量护栏，可与 B03 数据合并实现但保持独立测试函数）。

### STAT-I01 — 真实链路聚合（Integration，P0）

不手造状态，至少用现有模块真实产生 4 条 TransactionAnalysis：

```text
1× Success    ：SimulatedSlave → analyzer（elapsed 显式 15ms）
1× Exception  ：向 Slave 请求越界地址 → 0x83/{0x02} → analyzer
1× CrcError   ：SimulatedSlave → encode → CorruptCrc → decode → analyzer
1× Timeout    ：NoResponse + elapsed ≥ threshold → analyzer
→ summarizeTransactions(...)
```

Expected：observed=4, completed=4, pending=0；success=1, exception=1, crcError=1, timeout=1, protocolError=0；successRate=**0.25**；averageSuccessLatencyMs=**15.0**。

目的：证明 T005 + T006 + T007A + T007B 形成**真正的诊断统计闭环**。不再扩大更多集成场景。

### Part B 不负责的内容（显式禁止）

rolling statistics、incremental accumulator、history store、database、persistent log、time window、per-device aggregation、per-function aggregation、p95/p99、charts、Qt model、QAbstractListModel、QML、Agent。当前只要 **batch → snapshot**。

### 为什么不做 mutable accumulator（设计理由）

纯 snapshot 函数：输入明确、输出明确、无隐藏状态、测试简单；Replay 未来可对任意一批事务直接重算；QML Controller 也可按当前数据随时重求快照。而 mutable accumulator 会立刻带来 reset / remove / rollback / history sync / thread safety 一整串问题——当前没有任何消费者需要它们。

#### Implementation 细则（追加，2026-09-06）

**A. Success latency 用整数毫秒累加**：聚合阶段使用 `std::int64_t successLatencyTotalMs`，每个 Success 累加 `transaction.elapsed.count()`；仅在计算平均值时转 double：`averageSuccessLatencyMs = double(totalMs) / double(successCount)`。理由：milliseconds 本身是整数单位，先整数累加更直接、可解释、无多余浮点累计误差。

**B. 浮点断言规则**：Snapshot 保留 defaulted `operator==`，但测试**不得**依赖整个 Snapshot 的精确 `==`（浮点字段）——`successRate = 2.0/6.0` 用测试框架的 fuzzy/tolerance 比较分别验证；计数字段仍精确比较。Core 不负责 "33.33%" 这类百分比字符串格式。

## Part B Knowledge I Must Be Able To Explain（14 题）

**PB-Q1. Statistics Snapshot 和 Transaction Analysis 有什么区别？** Part A 把一个事务翻译成一个分类结果；Part B 把一批结果聚合成一个统计快照——层级不同、输入输出粒度不同。
**PB-Q2. 为什么 Pending 不能算失败？** Pending 表示"还没有最终结果"（可能在途）；提前计失败会把正常的等待扭曲成故障，成功率随轮询节奏波动。
**PB-Q3. observed / pending / completed 有什么区别？** observed=输入总数；pending=仍无最终结果的；completed=已有最终结果的（五分类之和）；恒等式 observed = pending + completed。
**PB-Q4. successRate 的分母为什么是 completedCount？** 成功率回答"已决事务里多少成功"；Pending 未决，进分母会让成功率随等待时长被动下降。
**PB-Q5. 为什么 completedCount=0 时 successRate 是 nullopt 而不是 0？** 0% 有明确否定语义（"完成了，但没成功"）；nullopt 表达"没有数据"——两者必须可区分，UI 才能显示 N/A 而非 0%。
**PB-Q6. 为什么 completedCount>0 且 success=0 时 successRate 才是 0？** 此时才有真实否定证据：全部已决事务都失败了——这才是数学意义上的 0%。
**PB-Q7. 为什么 Timeout elapsed 不进入 averageSuccessLatency？** Timeout 的 elapsed 是"等待阈值达到的时刻"，不是"响应完成时间"；混入会系统性虚高成功延迟指标。
**PB-Q8. 为什么当前 latency 只统计 Success？** 只有 Success 的 elapsed 语义统一（响应完成耗时）；Exception/CrcError 的耗时语义各不相同，混算无解释力。
**PB-Q9. 为什么没有成功事务时 averageSuccessLatency 是 nullopt？** 同 PB-Q5："无数据"≠"数据为零"；0ms 平均会暗示"有成功样本且瞬间完成"。
**PB-Q10. 为什么 Core 返回 0.25 而不是字符串 "25%"？** 数学值与展示格式分离：格式（小数位/本地化/颜色）属于 Presentation；Core 返回可计算的数值。
**PB-Q11. 为什么现在不用 StatisticsManager？** batch→snapshot 纯函数无隐藏状态，UI/测试/Replay 都能随时重算；Manager 带来生命周期、线程安全与同步问题，当前无消费者。
**PB-Q12. 为什么 batch→snapshot 对未来 Replay 很友好？** Replay 的任意历史片段都是"一批 TransactionAnalysis"，直接重算快照即可，无需维护增量状态或重放进度。
**PB-Q13. 两个 count invariant 分别是什么？** A：observed == pending + completed；B：completed == success + exception + crcError + timeout + protocolError——快照内部自洽，UI 不必猜。
**PB-Q14. T007 Part B 如何为 QML Dashboard 提供数据？** 快照字段与仪表盘指标一一对应（计数/成功率/平均延迟），且四条 invariant 保证自洽；Controller 每当事务集合变化时调用 summarize 得到新快照即可绑定。

## Part B — Implementation（实录，2026-09-06）

### 新增文件

- `src/core/analysis/TransactionStatistics.{h,cpp}`：`TransactionStatisticsSnapshot`（defaulted `==`）+ `summarizeTransactions`；注释写明四不变量与"Success latency 整数毫秒累加"细则。
- `tests/test_transaction_statistics.cpp`：STAT-B01~B08（8 个测试函数；B06 用精确 `== 0.0`——qFuzzyCompare 对 0 不可靠，而 0/4 数学上精确）。
- `tests/test_statistics_integration.cpp`：STAT-I01（真实产生 Success/Exception/CrcError/Timeout 四条 analysis 后聚合）。
- `CMakeLists.txt`：core 加入 `TransactionStatistics.cpp`；两个测试 target + ctest `statistics` / `statistics_integration`。

### 聚合实现顺序（与设计一致）

```text
observedCount = size
遍历 + 穷举 switch(TransactionStatus)（无 default，-Wswitch 保护）：
    Pending → pendingCount++            （不入 completed）
    Success → successCount++；successLatencyTotalMs += elapsed.count()（int64）
    Exception/CrcError/Timeout/ProtocolError → 各自 ++（不计 latency）
completedCount = 五分类之和            ← Invariant B 由构造方式直接保证
completedCount > 0 ? successRate = double(success)/double(completed) : nullopt
successCount   > 0 ? averageSuccessLatencyMs = double(totalMs)/double(successCount) : nullopt
```

四不变量落实：B 由"completed 按分类之和计算"**结构性成立**（非第二遍扫描校验）；A 因 observed 先行赋值 + completed 由子集构成而自动成立；C/D 由两个 if 构造；STAT-B08 在混合批次（含 Pending）上断言 A–D。

## Files Changed（本阶段实现）

- 新增：`src/core/analysis/TransactionStatistics.{h,cpp}`、`tests/test_transaction_statistics.cpp`、`tests/test_statistics_integration.cpp`、`docs/devlog/2026-09-06-T007-PartB-Implementation.md`
- 修改：`CMakeLists.txt`（core 源 + 两个 target）、`docs/tasks/T007-transaction-analysis.md`（本文件补齐）、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、`docs/02_ARCHITECTURE.md`、`docs/04_TEST_STRATEGY.md`、`docs/INTERVIEW_NOTES.md`
- 未改动：T002–T007A 全部既有源码与测试、`src/main.cpp`、presets

Part B Learning / Test Design（本阶段，docs-only）：
- 修改：`docs/tasks/T007-transaction-analysis.md`（本文件：Part B 设计、三计数/successRate/nullopt 语义、四不变量、矩阵 STAT-B01~B08 + I01、浮点规则、mutable accumulator 取舍、14 题问答、16 步计划）
- 修改：`docs/PROJECT_STATUS.md`（Current Part/Phase/Next Action）、`docs/BACKLOG.md`（T007 行 Part B 状态）
- 新增：`docs/devlog/2026-09-06-T007-PartB-TestDesign.md`

始终未改动：`src/`、`tests/`、`CMakeLists.txt`、presets。

## Problems Encountered

1. **RED 如预期**：仅声明无定义时两个测试目标链接失败——10 处 `undefined reference to analyzeFunction03Transaction(...)`（Part A）。未提交 RED 状态。
2. **实现本身（Part A）：No significant implementation issue encountered.** 无 variant/类型比较/CMake 问题，一次实现即 GREEN；ISSUE-001 模式未复发（全部具名局部量 + optional 拷贝辅助）。
3. **RED（Part B）如预期**：7 处 `undefined reference to summarizeTransactions(...)`。未提交 RED 状态。
4. **-Wmissing-field-initializers 警告（Part B，真实小问题）**：测试辅助 `makeAnalysis` 的 designated initializer 漏写 `exceptionCode` 成员，clean 重建的零警告 grep 抓出。修复：显式补 `.exceptionCode = std::nullopt`。教训：partial designated init 在 GCC 下触发该警告，显式补齐更清晰。
5. **实现本身（Part B）：No significant implementation issue encountered.** 无聚合/类型/浮点问题；ISSUE-001 模式未复发。

## Solutions

1. RED 证据存档后按定案顺序实现（observation 分支优先、makeAnalysis 单一漏斗）。
2. Part B 警告修复后 clean 重建复归零警告。

## Verification

### RED（仅声明、无定义；未提交）

```text
$ cmake --preset debug-local      → configure PASS
$ cmake --build --preset debug-local
两个测试目标链接失败；undefined reference 共 10 处：
  `modbuslens::core::analyzeFunction03Transaction(ModbusRtuFrame const&,
   variant<ModbusRtuFrame, RtuDecodeError, NoResponse> const&, duration, duration)`
compile 全部通过，仅 link 失败——预期 RED。
```

### GREEN（实现后）

```text
$ cmake --build --preset debug-local              → [26/26] 全部链接成功
$ ./build/debug/modbuslens_transaction_tests.exe
  PASS: a01~a12  Totals: 14 passed, 0 failed (2ms)
$ ./build/debug/modbuslens_transaction_integration_tests.exe
  PASS: i01/i02/i03  Totals: 5 passed, 0 failed (2ms)

$ ctest --preset debug-local
11/11: smoke | crc | frame | codec | f03 | simulator | simulator_integration |
       fault | fault_integration | transaction | transaction_integration 全部 Passed
100% tests passed, 0 tests failed out of 11

$ cmake --build --preset debug-local --clean-first
警告/错误行数 grep = 0（零警告，57 targets）；ctest 再次 11/11 通过
```

RED → GREEN 状态变化实录：`transaction`/`transaction_integration` 从"无法链接（10 undefined references）"变为 "Passed"；既有九个测试全程未破坏。

### Part B RED（仅声明、无定义；未提交）

```text
$ cmake --preset debug-local      → configure PASS
$ cmake --build --preset debug-local
两个测试目标链接失败；undefined reference 共 7 处：
  `modbuslens::core::summarizeTransactions(std::span<TransactionAnalysis const, ...>)`
compile 全部通过，仅 link 失败——预期 RED。
```

### 过程问题（Part B）

```text
clean 全量重建零警告 grep 命中 1 行：
  test_transaction_statistics.cpp:21 warning: missing initializer for member
  'TransactionAnalysis::exceptionCode' [-Wmissing-field-initializers]
→ 显式补齐后复归 0 命中。
```

### Part B GREEN（实现后）

```text
$ cmake --build --preset debug-local              → [30/30] 全部链接成功
$ ./build/debug/modbuslens_statistics_tests.exe
  PASS: b01~b08  Totals: 10 passed, 0 failed (3ms)
$ ./build/debug/modbuslens_statistics_integration_tests.exe
  PASS: i01  Totals: 3 passed, 0 failed (2ms)

$ ctest --preset debug-local
13/13: smoke | crc | frame | codec | f03 | simulator | simulator_integration |
       fault | fault_integration | transaction | transaction_integration |
       statistics | statistics_integration 全部 Passed
100% tests passed, 0 tests failed out of 13

$ cmake --build --preset debug-local --clean-first
警告/错误行数 grep = 0（零警告，66 targets）；ctest 再次 13/13 通过
```

RED → GREEN 状态变化实录：`statistics`/`statistics_integration` 从"无法链接（7 undefined references）"变为 "Passed"；既有十一个测试全程未破坏。

### Part B Learning / Test Design（本阶段，docs-only）

```text
git diff --check      → 通过
git diff --name-only  → 仅 docs/；src/、tests/、CMakeLists.txt 未出现
```

## Result

✅ **Part A DONE**：`analyzeFunction03Transaction` 落地 `modbuslens_core`（零 Qt、纯函数、无时钟）；TX-A01~A12 + I01~I03 全绿（含 elapsed/exceptionCode 双不变量与数量一致性跨帧校验）；全项目 ctest 11/11、clean 重建零警告。
✅ **Part B Learning / Test Design DONE（docs-only）**：三计数概念、successRate 严格定义（Pending 不进分母）、nullopt 语义（无数据≠零）、Success-only latency、四不变量、8 用例矩阵 + STAT-I01 真实链路聚合、mutable accumulator 取舍、14 题问答、16 步实施计划落库。
✅ **Part B Implementation DONE**：`summarizeTransactions` 落地 `modbuslens_core`（零 Qt、纯函数、穷举 switch、completed 按分类之和构造保证 Invariant B）；STAT-B01~B08 + I01 全绿（B06 精确 0.0、B08 四不变量锁定）；全项目 ctest 13/13、clean 重建零警告。

🏆 **T007 整体 DONE**（Part A + Part B 全部完成并验证）；M4 事务分析里程碑关闭。

## Knowledge Learned

- **从"帧正确"到"事务正确"是层级跃迁**：格式合法 ≠ 回答正确；跨帧校验（地址/功能/数量）只有同时看到两帧的层才能执行。
- **观察与判断分离**：NoResponse 是事实，Timeout 是事实+阈值的判断——与 T006 的 DropResponse 呼应，三层各管一段。
- **错误分类要有消费者**：CrcError 单列（有明确诊断价值）、FrameTooShort 归并（暂无消费者）——状态设计跟着统计口径走。
- **Analyzer 纯函数化**：elapsed/threshold 全部外置，测试零等待、结果可精确断言（延续 T006 元数据思路）。
- **实现阶段新增**：
  1. **单一漏斗保不变量**：`makeAnalysis` 私有 helper 让 elapsed/exceptionCode 两条不变量"写一次就处处成立"，12 个分支零遗漏。
  2. **穷举 switch 不写 default**：新增 `RtuDecodeErrorCode` 枚举时 `-Wswitch` 强制处理新分支（不静默吞），switch 后的显式兜底 return 保证函数 total——两全。
  3. **跨类型比较显式提升**：`uint16_t quantity` vs `size_t values.size()` 用 `static_cast<std::size_t>` 显式对齐，零警告。
  4. **适配发生在边界**：T006 `DroppedResponse` → `NoResponse` 的转换放在集成测试/调用边界，Transaction API 保持抽象（不认识具体故障类型）。
- **Part B 实现阶段新增**：
  1. **Invariant B 由构造保证**：completed 按五分类之和计算，恒等式不需要第二遍校验——"让错误状态无法表达"优于"事后断言"。
  2. **整数毫秒累加**：latency 先 int64 累加、平均值处才转 double，可解释且无浮点累计误差（实现细则 A）。
  3. **partial designated init 会触发 -Wmissing-field-initializers**：显式补齐每个成员更清晰（零警告 grep 抓出的真实警告）。
  4. **qFuzzyCompare 对 0 不可靠**：比较 0/4=0.0 这类精确零值用 `==`（数学上精确），非零比例才用 fuzzy。

## Potential Interview Questions

- 14 题见上；Implementation 阶段新增：
  1. 为什么用 `holds_alternative` + `get` 而不是 `get_if` 链？（先判型后取值，分支语义清晰；配合 ISSUE-001 的具名局部量原则）
  2. 六状态里为什么没有 InvalidRequest？（request 是契约输入，不是观察结果——防御失败映射 ProtocolError 即可）
  3. Pending/Timeout 为什么用 `>=` 划界？（超时必须被报告，边界时刻归属 Timeout）
  4. `makeAnalysis` 漏斗与"每个分支手写返回"的取舍？（漏斗把两条不变量固化在一点，12 个返回路径零漂移）
- Part B 阶段新增：
  1. successRate 为什么是 optional？（区分"没有数据"与"成功率为零"——B05/B06 的语义对比）
  2. completed 为什么按分类之和计算？（Invariant B 由构造保证，新增状态时编译器强制同步）
  3. 为什么 latency 用 int64 累加？（整数单位整数累加，避免浮点累计误差）
  4. 为什么 0.0 断言不用 qFuzzyCompare？（qFuzzyCompare 相对比较对 0 失效；0/4 精确为零用 ==）

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| T006 代码 | `2c8d850` | （前 LKGC） |
| T007 Part A Learning | `fa56100` | docs-only |
| Part A 代码提交（**新 LKGC**） | `14982f6` | `T007(Part A): implement single transaction analysis` |
| Part B Test Design | `f7716c4` | `T007(Part B): 统计快照学习与测试设计（docs-only）` |
| Part B 代码提交（**新 LKGC**） | `PENDING-BACKFILL` | `T007(Part B): implement transaction statistics snapshot` |
| 回填提交（docs-only，HEAD） | 见 `git log` | 回填哈希 |

> LKGC 推进：Part B 产生新业务代码并经 configure/clean build/full ctest（13/13）验证；LKGC 由 `14982f6` 推进至 Part B 代码提交，由 docs-only 回填提交写入。**T007 整体 DONE；M4 关闭；T008 未开始。**

> LKGC 推进：Part A 产生新业务代码并经 configure/clean build/full ctest（11/11）验证；LKGC 由 `2c8d850` 推进至 Part A 代码提交，由 docs-only 回填提交写入。**Part A DONE；T007 整体 IN PROGRESS（Part B 未开始）；T008 未开始。**