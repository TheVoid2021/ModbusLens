# T007 — Transaction Analysis

> 状态：**IN PROGRESS**｜Part A（Single Transaction Analysis）：**Learning / Test Design ✅（docs-only）→ Implementation ⬜**｜Part B（Statistics Snapshot）：⬜ Not Started
> 前置确认：T006 DONE、M3 DONE、LKGC = `2c8d850`。
> 协议依据：V1.1b3 §6.3（0x03 语义，复用 T004B）+ 串行主站模型（V1.02）。
> Part A Implementation 边界预告（未实现，禁止提前）：SessionManager、queue、polling loop、QTimer、thread、serial、QML model、database、history persistence、statistics accumulator、dashboard、Agent 全部不做。

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

## Implementation

**未发生。** 本阶段 docs-only；`src/`、`tests/`、`CMakeLists.txt` 零改动。

## Files Changed（本阶段）

- 新增：`docs/tasks/T007-transaction-analysis.md`（本文件）、`docs/devlog/2026-09-06-T007-PartA-TestDesign.md`
- 修改：`docs/PROJECT_STATUS.md`（四段式状态）、`docs/BACKLOG.md`（T007 拆 Part A/B + 范围收缩记录 + M4 状态）
- 未改动：`src/`、`tests/`、`CMakeLists.txt`、presets

## Problems Encountered

无实现问题（docs-only）。范围事项：原 BACKLOG T007 行把"统计快照"与单事务分析混在一行、隐含一次做完——已拆分 Part A/B 并在变更记录留痕；broadcast 事务口径显式推迟至 Session/Runtime。

## Solutions

按用户指示拆分 Part A（单事务）/ Part B（统计快照）；"统计快照"移入 Part B 并写明其一句话边界；变更写入 BACKLOG 变更记录，历史不删改。

## Verification（本阶段，docs-only）

```text
git diff --check        → 通过（无空白/行尾问题）
git diff --name-only    → 仅 docs/ 下文件；src/、tests/、CMakeLists.txt 未出现
```

## Result

Part A Learning / Test Design 完成：Transaction 定义、串行配对模型、六状态、观察/结果模型、判定规则（Pending/Timeout 边界、CRC/FrameTooShort 映射、地址/功能/数量三类跨帧校验）、12 用例矩阵 + 3 条集成测试（I03 纳入 P0）、14 题问答、实施计划落库。**Analyzer 未实现**；Part B 未开始；T007 整体 IN PROGRESS。

## Knowledge Learned

- **从"帧正确"到"事务正确"是层级跃迁**：格式合法 ≠ 回答正确；跨帧校验（地址/功能/数量）只有同时看到两帧的层才能执行。
- **观察与判断分离**：NoResponse 是事实，Timeout 是事实+阈值的判断——与 T006 的 DropResponse 呼应，三层各管一段。
- **错误分类要有消费者**：CrcError 单列（有明确诊断价值）、FrameTooShort 归并（暂无消费者）——状态设计跟着统计口径走。
- **Analyzer 纯函数化**：elapsed/threshold 全部外置，测试零等待、结果可精确断言（延续 T006 元数据思路）。

## Potential Interview Questions

- 14 题见上；Implementation 阶段将补充：variant 观察模型的分发顺序、optional exceptionCode 的语义、集成测试中 T006→T007 的类型转换归属。

## Git Commit

- 本阶段提交信息：`T007(Part A): 单事务分析学习与测试设计（docs-only）`
- 哈希：见 `git log`（docs-only 不推进 LKGC；LKGC 保持 `2c8d850`）。