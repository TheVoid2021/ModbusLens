# T004 — Modbus RTU Codec

> 状态：**IN PROGRESS**｜Part A（RTU Wire Codec）：**DONE ✅**｜Part B（Function 0x03 Codec）：**Learning / Test Design ✅（docs-only）→ Implementation ⬜**
> 协议依据：MODBUS Application Protocol **V1.1b3 §6.3**（Part B 主要依据：0x03 Read Holding Registers）+ MODBUS over Serial Line V1.02（Part A：帧/CRC/字节序）。
> Part B Implementation 边界预告（未实现，禁止提前）：不实现 0x06、不实现业务请求 encode、不实现 Simulator/Transaction matcher/Replay/Serial/QML/Agent、不做 fuzz/benchmark。

## Goal

交付 `ModbusRtuFrame ↔ 完整 RTU wire bytes` 的双向转换（Part A）：encode 重算 CRC 并按 V1.02 低字节在前序列化；decode 做长度检查、CRC 验证，成功才产出语义 Frame，失败返回结构化错误。Part B（0x03 字段解释）独立推进。

## Background

- T002 交付 CRC 计算、T003 交付语义帧模型；Part A 是两者的"接线层"。
- ModbusLens 是诊断工具——**失败原因本身就是重要数据**，错误模型在设计期定案（variant），实现期不改弦。
- 知识维度拆分：字节序+CRC（Part A，通用）与功能码语义（Part B，业务）分开学、分开测。

## Technical Decisions（累计）

| 阶段 | 决策 | 内容 | 理由 |
| --- | --- | --- | --- |
| Learning | Part 拆分 | Part A：Frame↔wire；Part B：0x03 data 解释 | 通用容器转换与功能码语义正交 |
| Learning | 依赖方向 | Part B 只消费 Part A 产出，不重新实现 CRC | CRC 唯一事实来源 = T002 KAT 锁定函数 |
| Learning | 范围 | fuzz/benchmark 不在 T004；异常码→文字映射归 analysis | 小步交付；性能优化需 profiling 依据 |
| Learning | 字节序 | 全部显式逐字节处理，禁 memcpy uint16 | 协议字节序 ≠ 平台端序 |
| Part A | 错误模型 | `std::variant<ModbusRtuFrame, RtuDecodeError>` + `enum class RtuDecodeErrorCode{FrameTooShort, CrcMismatch}`（struct 包装便于扩展诊断字段） | 诊断工具必须知道失败原因；optional/bool 不够 |
| Part A | 接口形态 | `encodeRtuFrame(const ModbusRtuFrame&) -> std::vector<std::uint8_t>`；`decodeRtuFrame(std::span<const std::uint8_t>) -> RtuDecodeResult` | 纯函数、零 Qt、非拥有输入 |
| **Part B** | 语义模型 | `ReadHoldingRegistersRequest{startAddress, quantity}`、`ReadHoldingRegistersResponse{values: vector<uint16_t>}`、`ModbusExceptionResponse{exceptionCode}`（类型即语义，不重复存 functionCode） | 最小聚合 + value 语义；与 T003 Frame 同风格 |
| **Part B** | 错误模型 | `enum class Function03DecodeErrorCode{WrongFunctionCode, InvalidRequestLength, InvalidQuantity, InvalidByteCount, InvalidExceptionLength}` + struct + `std::variant` | **不复用** `RtuDecodeErrorCode`：Part A 是传输层失败域，Part B 是功能码语义失败域，混枚举会让上层无法区分错误层级 |
| **Part B** | exception 命名 | `ModbusExceptionResponse`（不带 03 前缀） | 异常形状 `0x80|fn + code` 是跨功能码通用结构，未来 0x04/0x06 的 codec 可复用 |
| **Part B** | 40001 边界 | Core 只用 0-based 协议地址（0x0000 起）；40001 式人类参考编号的换算归未来 UI / Device Profile 层 | 混用两种口径是现场数据"看不懂"的头号来源（03_LEARNING §7） |

## 1. Codec 的含义

- **Encoder（编码）**：C++ 对象 → wire bytes；**Decoder（解码）**：wire bytes → C++ 对象。
- **为什么 Serial Transport 不自己实现协议编码？** 职责不同（搬字节+帧切分 vs 语义转换）；可测试性（纯函数离线毫秒级）；复用性（Simulator/Replay 无串口但需要同一套编解码）；分层解耦。

## 2. Part A 数据流

### Encode（对象 → 线路）

```text
ModbusRtuFrame
    ↓ 取 address + functionCode + data
    ↓ calculateModbusCrc()          （T002，数值）
    ↓ append CRC low byte           （低字节在前）
    ↓ append CRC high byte
wire bytes
```

### Decode（线路 → 对象）

```text
wire bytes
    ↓ 长度基础检查（最小 4 字节；不含 t1.5/t3.5 时间判断——属 T010）
    ↓ 分离 payload / wire CRC（末尾两字节）
    ↓ 对 payload 全部字节重算 CRC
    ↓ 比较
    ├─ 一致 → ModbusRtuFrame（语义模型，不含 CRC 字段）
    └─ 不一致 → 结构化错误 CrcMismatch（不得产出可信 Frame）
```

## 3–5. Part B 字段解释（Learning 定稿，未实现）

- **0x03 Request**：`01 03 00 00 00 01 84 0A` → startAddress `00 00`=0、quantity `00 01`=1（16-bit big-endian）。
- **0x03 Normal Response**：`01 03 04 00 64 00 C8 …` → byteCount=4；2 寄存器×2B=4 数据字节；`00 64`=100、`00 C8`=200。
- **Exception Response**：`0x83 = 0x03 | 0x80`；exceptionCode 结构化保存，文字映射归 analysis。

## 6. Big-endian Register Field

`00 64 → 0x0064 → 100`；公式 `value = (highByte << 8) | lowByte`。寄存器字段高字节在前 vs CRC 低字节在前是两条独立规则；禁止用 CPU 端序隐式决定协议顺序。

## Part A — Test Design（已实现，矩阵留档）

期望值来源：T002 KAT 向量 + 一次性独立 Python 复核（脚本不进仓库）。

| Test ID | Input | Expected Result | Why This Test Exists | What Bug It Can Catch | Priority |
| --- | --- | --- | --- | --- | --- |
| RTU-A01 | Frame{0x01,0x03,{00,00,00,01}} | wire `01 03 00 00 00 01 84 0A` | 拼接顺序 + T002 CRC 集成 + 低字节在前序列化 | 字段顺序错、CRC 高低位写反 | **P0** |
| RTU-A02 | wire `01 03 00 00 00 01 84 0A` | Frame{0x01,0x03,{00,00,00,01}} | decoder 分离 CRC→验证→恢复语义 Frame | payload/CRC 切分错、比较对象错 | **P0** |
| RTU-A03 | wire `01 03 00 00 00 02 84 0A`（真 CRC 0x0BC4） | `CrcMismatch`，不产生 Frame | 损坏报文不得进入业务链路 | CRC 比较写反、"先建 Frame 再校验" | **P0** |
| RTU-A04 | 空；`01 03 00` | `FrameTooShort`，不越界 | container 层最小 4 字节硬下界 | 读越界、边界 `<`/`<=` 错 | **P0** |
| RTU-A05 | Frame{0x01,0x07,{}} | wire `01 07 41 E2`（0xE241） | data 为空仍可成帧（container 级）；选 0x07 避免暗示空 data 的 0x03 请求合法 | 隐含"data≥1"假设 | P1 |
| RTU-A06 | Frame{0x01,0x83,{0x02}} | wire `01 83 02 C0 F1`（0xF1C0）；round-trip 相等 | 异常形态**透明处理**；不解释 0x02 含义 | 对功能码值的隐藏特判 | P1 |
| RTU-A07 | Frame{0x01,0x03,{0x02,0x00,0x64}} | wire `01 03 02 00 64 B9 AF`（0xAFB9）；round-trip 相等 | 对称性；**辅助证据**——对称错误时 round-trip 仍通过，不能替代 KAT | encode/decode 不对称 | P1 |

优先级：**P0 = A01/A02/A03/A04**（正确、安全的最小功能）；P1 = A05/A06/A07（边界与对称性）。KAT 主要证据、round-trip/invariant 补充证据。

### CRC mismatch 的设计意义

未来统计四类结果：Success / Timeout / CRC Error / Exception——Wire Decoder 必须能显式报 `CrcMismatch`。Part A 只返回结构化错误：**不做**日志、统计、Transaction、UI。

### Raw Bytes 所有权边界

`decodeRtuFrame` 只接受非拥有 `std::span`；CRC Error 时不保存 raw bytes；未来由更高层（Transport/Session/Transaction）按诊断需求复制保存。Codec = 纯转换/验证函数，无状态。

### 最大长度：知识 + Deferred Decision

ADU 最大 256 bytes（data ≤ 252）已记录；oversized policy / streaming parser / partial buffer / t1.5 / t3.5 属 Serial framing/transport（T010），Part A v1 不做。未来若加检查，扩展 `RtuDecodeErrorCode` 即可（struct 错误无需改类型）。

## Part A — Implementation（本阶段实录）

### 新增文件

- `src/core/protocol/ModbusRtuCodec.h`：`RtuDecodeErrorCode` / `RtuDecodeError` / `RtuDecodeResult` 类型 + 两个函数声明；注释写明职责边界（不拥有输入、CRC 失败不产 Frame、不解释功能码数据）。
- `src/core/protocol/ModbusRtuCodec.cpp`：实现（见下）。
- `tests/test_modbus_rtu_codec.cpp`：QtTest A01–A07；`std::get_if` 辅助函数区分 Frame/Error 分支。
- `CMakeLists.txt`：`modbuslens_core` 加入 `ModbusRtuCodec.cpp`；新增 target `modbuslens_codec_tests` + ctest `codec`。

### encode 实际步骤（与设计逐条对应）

```cpp
std::vector<std::uint8_t> wire;
wire.reserve(2 + frame.data.size() + 2);      // 上限已知，一次分配
wire.push_back(frame.address);                // address
wire.push_back(frame.functionCode);           // function
wire.insert(wire.end(), frame.data...);       // data（可空）
const std::uint16_t crc = calculateModbusCrc(wire);  // 对前三段重算（此刻无 CRC）
const auto lowByte  = static_cast<std::uint8_t>(crc & 0xFF);
const auto highByte = static_cast<std::uint8_t>((crc >> 8) & 0xFF);
wire.push_back(lowByte);                      // 低字节在前（V1.02）
wire.push_back(highByte);
return wire;
```

无 memcpy/无 reinterpret_cast：CRC 顺序是**显式语句**，与 CPU 端序无关。

### decode 实际步骤

```cpp
if (bytes.size() < 4)  return RtuDecodeError{FrameTooShort};   // container 最小帧
const auto payload = bytes.first(bytes.size() - 2);            // 分离 CRC
const auto crcLow  = static_cast<std::uint16_t>(bytes[payloadSize]);     // 84
const auto crcHigh = static_cast<std::uint16_t>(bytes[payloadSize + 1]); // 0A
const std::uint16_t receivedCrc = static_cast<std::uint16_t>(
    (static_cast<unsigned>(crcHigh) << 8) | crcLow);           // 0x0A84
if (calculateModbusCrc(payload) != receivedCrc)
    return RtuDecodeError{CrcMismatch};                        // 不产 Frame
return ModbusRtuFrame{ .address=bytes[0], .functionCode=bytes[1],
                       .data = {payload.begin()+2, payload.end()} };  // 不解释内容
```

- `84 0A → 0x0A84`：低字节放低位、高字节左移 8 位后按位或——与 encode 的拆分互为镜像，均显式。
- `payload` 是 `bytes` 的子视图（非拥有）；`calculateModbusCrc(payload)` 调用期间原缓冲由调用方持有——生命周期安全。
- 隐式转换：`std::vector<uint8_t>` 与 `span<const uint8_t>` 互转零代码（C++20 range 构造），未遇构造问题。
- 无异常控制流；`(unsigned)crcHigh << 8` 用 unsigned 计算避免窄 int 平台的符号溢出隐患。

### 依赖关系（Part A 后）

```text
QML/UI → (future Controller/Model) → modbuslens_core（STATIC，零 Qt）
                                         ├─ ModbusCrc（T002）
                                         ├─ ModbusRtuFrame（T003）
                                         └─ ModbusRtuCodec（T004 Part A）
测试：modbuslens_tests / modbuslens_crc_tests / modbuslens_frame_tests / modbuslens_codec_tests
     （core + QtTest；Qt 仅在测试侧使用，core 本身零 Qt）
```

## Part B — Function 0x03 Codec Design（Learning + Test Design，本阶段定稿）

### 数据流与职责边界

```text
RTU Wire
    ↓ Part A（encode/decode + CRC，已完成）
ModbusRtuFrame
    ↓ Part B（仅解释 functionCode = 0x03 的 data 字段）
Function 0x03 semantic object
```

Part B **不重新**：计算 CRC、验证 CRC、解析 RTU wire bytes、保存 raw bytes、操作串口——这些分别属于 T002 / Part A / 未来诊断层 / T010。Part B 的输入永远是 Part A 已验证的 `ModbusRtuFrame`。

### Request 数据模型（定案）

```cpp
struct ReadHoldingRegistersRequest {
    std::uint16_t startAddress;   // 0-based 协议地址
    std::uint16_t quantity;       // 1 ~ 125
};
```

规范记录（V1.1b3 §6.3）：

- Function = 0x03；**Request data 恒为 4 字节**；
- `data[0..1]` = Starting Address，`data[2..3]` = Quantity of Registers；
- 两字段均 **16-bit big-endian**；
- Quantity 合法范围 **1 ~ 125**；
- 模型**不重复保存** `functionCode = 0x03`——类型本身已表达语义（Frame.functionCode 仍在，用于分发与校验）。

### Response 数据模型（定案）

```cpp
struct ReadHoldingRegistersResponse {
    std::vector<std::uint16_t> values;   // 每寄存器 2B，高字节在前
};
```

- `Frame.data[0]` = **byteCount**；其后为寄存器字节；
- **byteCount 必须等于后续寄存器数据的实际字节数**（一致性校验）；
- **byteCount 必须为偶数**：1 register = 2 bytes；
- 例：`04 00 64 00 C8` → byteCount=4，values = {100, 200}。

### Exception 数据模型（定案）

```cpp
struct ModbusExceptionResponse {
    std::uint8_t exceptionCode;   // 仅结构化保存，不做文字映射
};
```

- `functionCode = 0x83`（= 0x03 | 0x80）+ `data = {code}` 是 Function 0x03 的异常响应；
- 命名**不带 03 前缀**：异常形状是跨功能码通用结构，未来 0x04/0x06 codec 可复用；
- **不**把 `0x02 = Illegal Data Address` 等文字映射写进 codec 核心——映射属 Analysis / Presentation 层。

### Part B 错误模型（定案）

```cpp
enum class Function03DecodeErrorCode {
    WrongFunctionCode,        // frame.functionCode 不是 0x03（或异常分支不是 0x83）
    InvalidRequestLength,     // request data != 4 bytes
    InvalidQuantity,          // quantity 不在 1~125
    InvalidByteCount,         // byteCount 与实际数据不符 / 为奇数
    InvalidExceptionLength,   // exception data != 1 byte
};
struct Function03DecodeError { Function03DecodeErrorCode code; };
// 三个 decoder 均返回 std::variant<对应语义对象, Function03DecodeError>
```

与 Part A 的 `RtuDecodeErrorCode` **分开**：传输层失败域（TooShort/Crc）与功能码语义失败域是不同层级，混枚举会让上层无法区分错误来源；两层的 variant 用法一致，初学者心智模型单一。校验顺序：先 WrongFunctionCode → 再长度 → 再内容规则，保证错误码稳定可预期。

### 官方规范复核记录（V1.1b3 §6.3）

- 官方示例 request data `00 6B 00 03` → startAddress=**107**、quantity=**3**（独立复核通过）——即 T002 Phase B 已对拍过的 `11 03 00 6B 00 03` 请求；
- 官方示例 response data `06 02 2B 00 00 00 64` → byteCount=6，values = **{555, 0, 100}**（`02 2B`=0x022B=555；独立复核通过）；
- 官方 PDF 在本环境抓取返回 404（与 V1.02 同样情况）：仅说明当前工具/网络环境无法访问，**不代表 Modbus Organization 官方资源失效**；复核采用"公开参数 + 官方印刷示例数值对拍"。

### Part B 测试矩阵

期望值均经一次性独立 Python 复核（脚本不进仓库）。解码入口均为 `ModbusRtuFrame`（Part A 产物）。

**Request（decode request）**

| Test ID | Input（Frame） | Expected | Why This Test Exists | What Bug It Can Catch | Priority |
| --- | --- | --- | --- | --- | --- |
| F03-B01 | {0x01, 0x03, {00,00,00,01}} | startAddress=0, quantity=1 | 基础金样：T002/T003 链路数据的语义解释 | 大端拼装颠倒、字段偏移错 | **P0** |
| F03-B02 | {*, 0x03, {00,6B,00,03}}（官方 §6.3 示例） | startAddress=107, quantity=3 | 外部权威金样：证明不是只对全零高字节工作 | 隐式假设高字节为 0、按平台端序拼装 | **P0** |
| F03-B03 | data={}；data={00,00,00} | `InvalidRequestLength`，不越界 | request data 恒 4 字节的硬约束 | 长度检查缺失、`<`/`<=` 边界错 | **P0** |
| F03-B04 | quantity=0（{00,00,00,00}）；quantity=126（{00,00,00,7E}） | `InvalidQuantity` | 规范范围 1~125；0 与 126 分别卡下/上界 | 范围检查用 `>`/`>=` 弄反、边界值±1 | **P0** |
| F03-B05 | functionCode=0x04（或任意 ≠0x03） | `WrongFunctionCode` | 不把其他功能码的 data 当 0x03 request 解释 | 分发缺失、先解释后校验 | P1 |

**Response（decode response）**

| Test ID | Input（Frame.data） | Expected | Why This Test Exists | What Bug It Can Catch | Priority |
| --- | --- | --- | --- | --- | --- |
| F03-B06 | {02,00,64} | values={100} | 单寄存器最小响应 | 奇偶/步进错误 | **P0** |
| F03-B07 | {04,00,64,00,C8} | values={100,200} | 多寄存器顺序拼接 | 寄存器次序、字节对错位 | **P0** |
| F03-B08 | {06,02,2B,00,00,00,64}（官方 §6.3 示例） | values={555,0,100} | 外部权威金样，避免只测自己设计的数据 | 同上 + 非零高字节处理 | **P0** |
| F03-B09 | {04,00,64}（byteCount 声称 4，实际 2） | `InvalidByteCount` | byteCount 与实际数据一致是响应可信的前提 | 只看 byteCount 不数实际长度 | **P0** |
| F03-B10 | {03,00,64,01}（byteCount=3，奇数） | `InvalidByteCount` | 1 register=2 bytes，奇数必然非法 | 缺奇偶校验 | **P0** |

**Exception（decode exception）**

| Test ID | Input（Frame） | Expected | Why This Test Exists | What Bug It Can Catch | Priority |
| --- | --- | --- | --- | --- | --- |
| F03-B11 | {0x01, 0x83, {0x02}} | exceptionCode=0x02（仅结构化） | 异常形状的最小正例；不转文字 | 把 0x83 当 0x03 解释、取错 data[0] | **P0** |
| F03-B12 | 0x83 且 data={} 或 {0x02,0x03} | `InvalidExceptionLength` | 异常 data 恒 1 字节 | 长度检查缺失 | P1 |

优先级：**P0 = B01/B02/B03/B04/B06/B07/B08/B09/B10/B11**（语义正确性与安全拒绝的最小集合）；**P1 = B05/B12**（分发与异常边界补充）。

### Deferred To Transaction Analysis（T007，禁止提前实现）

单独看 Response 格式可能自洽，但与 Request **不匹配**：如 request `quantity=2` 而 response `byteCount=6`（3 个寄存器）——Part B 无法也不应判断这种"跨帧一致性"。以下全部推迟到 T007：

- request/response **配对**（pairing）；
- quantity 与 byteCount/values 数量的**一致性**；
- latency、timeout、transaction status。

同理：byteCount=0（结构上自洽的"零寄存器响应"）在 Part B 通过格式校验，其与请求 quantity≥1 的矛盾由 T007 配对发现——Part B 不加特判。

### 40001 不进入 Core

Core 中 `startAddress` 一律是 **0-based 协议地址**（0x0000 起）。设备手册的人类参考编号（40001、40002 = 寄存器 0、1）与协议地址的换算属于未来 UI / Device Profile 层，**v1 不实现**——两种口径混用是现场数据错乱的头号来源（03_LEARNING §2/§7）。

### Part B Knowledge I Must Be Able To Explain（12 题）

**B-Q1. Function 0x03 是做什么的？** Read Holding Registers：读保持寄存器（可读写的 16-bit 数据区），是最常用的 Modbus 功能码。
**B-Q2. Request 为什么 data 固定 4 bytes？** 语义只有两个字段：起始地址 + 数量，各 2 字节，因此恒为 4——长度固定使校验简单可靠。
**B-Q3. startAddress 和 quantity 为什么都是 16-bit？** Modbus 寻址空间与单次读取量都按 16 位字段设计：地址 0x0000~0xFFFF，数量上限 125（0x7D）也由协议响应长度上限反推得出。
**B-Q4. quantity 为什么是 1~125？** 下界 1：读 0 个寄存器无意义；上界 125：响应 = 250 数据字节 + 3 字节头，不能超 RTU 帧的 256 字节上限（V1.1b3 规定）。
**B-Q5. Response 为什么用 byteCount 而不是 register count？** 让接收方无需理解功能码就能确定后续字节长度（对任意寄存器宽度都自洽）；register count 可由 byteCount/2 推出，冗余存储反而可能不一致。
**B-Q6. 为什么 byteCount 必须是偶数？** 寄存器固定 2 字节；奇数 byteCount 意味着半截寄存器——必然是损坏或伪造帧。
**B-Q7. `00 64` 为什么等于 100？** 大端 16-bit：`(0x00<<8)|0x64 = 100`。
**B-Q8. 为什么 `04 00 64 00 C8` 表示两个寄存器？** byteCount=4 → 4/2=2 个寄存器：`00 64`=100、`00 C8`=200，高字节在前。
**B-Q9. Part A 与 Part B 有什么区别？** Part A：字节容器 ↔ wire（CRC、字节序，对所有功能码一致）；Part B：某功能码 data 的语义解释（0x03 专有）。输入输出粒度不同：Frame vs semantic object。
**B-Q10. 为什么 Function Codec 不做 CRC？** CRC 属于传输完整性，Part A 已在 wire 层验证；Frame 到达 Part B 时已可信，重做既浪费又引入第二份真相。
**B-Q11. 为什么 Request/Response 是否匹配不能由单独的 Response decoder 判断？** 它一次只看一帧，没有"另一帧"的上下文；配对与一致性需要时间序与事务状态——属 T007 Transaction Analysis。
**B-Q12. 为什么协议地址 0 和手册里的 40001 不能直接混用？** 40001 是 1-based 人类编号（40001→寄存器 0），协议 PDU 用 0-based 地址；差 1 的换算混用是现场读写错地址的经典事故，Core 只认 0-based，展示层再翻译。

### Part B Implementation Plan（下一阶段，18 步）

1. 创建最小 Function 03 semantic structs（三个模型）；
2. 创建 `Function03DecodeErrorCode` / `Function03DecodeError`；
3. 声明 request decoder；
4. 声明 response decoder；
5. 声明 exception decoder；
6. 创建 B01~B12 tests；
7. 运行并记录真实 RED（仅声明无定义 → linker error，或如实记录其他形态）；
8. 实现 big-endian uint16 读取 helper（仅在必要范围，文件内 static）；
9. 实现 request decode；
10. 实现 response decode；
11. 实现 exception decode；
12. GREEN；
13. clean build；
14. full ctest（smoke/crc/frame/codec/f03 不破坏、新增全过）；
15. 文档归档；
16. code commit；
17. 推进 LKGC；
18. docs backfill（如需要）。

**不额外实现**：0x06、业务请求 encode、Simulator、Transaction、fuzz、benchmark。若未来 Simulator（T005）需要构造 0x03 响应，再单独评估是否加 Function 03 encode helper——不提前设计过量 API。

## Files Changed

Learning：`docs/tasks/T004-modbus-rtu-codec.md`、`docs/devlog/2026-09-05-T004-Learning.md`、`docs/BACKLOG.md`、`docs/PROJECT_STATUS.md`。
Part A Test Design：`docs/tasks/T004-modbus-rtu-codec.md`、`docs/devlog/2026-09-05-T004-PartA-TestDesign.md`、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`。
Part A Implementation：`src/core/protocol/ModbusRtuCodec.{h,cpp}`、`tests/test_modbus_rtu_codec.cpp`、`CMakeLists.txt`、`docs/devlog/2026-09-06-T004-PartA.md`、T004 档案、PROJECT_STATUS、BACKLOG、02_ARCHITECTURE、04_TEST_STRATEGY、INTERVIEW_NOTES。

Part B Learning / Test Design（本阶段，docs-only）：
- 修改：`docs/tasks/T004-modbus-rtu-codec.md`（本文件：Part B 设计、矩阵 B01–B12、Deferred to T007、40001 边界、12 题、18 步计划）
- 修改：`docs/PROJECT_STATUS.md`（Current Part/Phase/Next Action）、`docs/BACKLOG.md`（T004 行状态）
- 新增：`docs/devlog/2026-09-06-T004-PartB-TestDesign.md`

始终未改动：`src/`、`tests/`、`CMakeLists.txt`、presets。

## Problems Encountered

1. **RED 如预期表现为 linker error**：头文件仅有声明时，`modbuslens_codec_tests.exe` 链接失败——`undefined reference to modbuslens::core::encodeRtuFrame(...)` 与 `decodeRtuFrame(...)` 共 10 处。这是计划内、真实的 TDD RED，未伪造任何运行时失败，未提交 RED 状态代码。
2. **仓库出现无名提交（流程事件，非代码问题）**：实现完成后发现 HEAD 是一个消息为 `commit` 的提交（`c605550`，本仓库同一 git 身份、会话暂停期间产生），内容恰为本任务 Part A 的全部代码与文档。处理：`git commit --amend` 将其修正为规约信息 `T004(Part A): implement RTU wire codec with CRC validation`（内容不变，并合入当时暂存的状态面板更新），该提交即新 LKGC（`73825c6`）；过程在 PROJECT_STATUS 变更记录中留痕。
3. 细节观察（非问题）：MinGW 符号名中 `std::span<unsigned char const, 18446744073709551615ull>` 的动态 extent 以 `size_t` 最大值打印——动态长度 span 的 demangle 特征，读链接错误时不要被它迷惑。
4. **No significant implementation issue encountered.** 未出现 span 构造、vector/span 转换、CRC 字节序、高低位 cast、期望值不符、CMake target、variant 提取问题；一次实现即 GREEN。

## Solutions

1. RED 证据全文存档（见 Verification）后，按 Test Design 实现；同一次提交中 cpp 即最终实现，stub 从未入库。
2. amend 保留全部内容、仅修 message 并合入面板更新；amend 前后内容差异 = 暂存的 PROJECT_STATUS 面板（Part A DONE / LKGC 占位 / 4/4 测试）。
3. 记录符号名观察，供后续读链接错误参考。

## Verification

### RED（仅声明、无定义；未提交）

```text
$ cmake --preset debug-local      → configure PASS
$ cmake --build --preset debug-local
FAILED: modbuslens_codec_tests.exe
tests/test_modbus_rtu_codec.cpp:64: undefined reference to
    `modbuslens::core::encodeRtuFrame(modbuslens::core::ModbusRtuFrame const&)'
tests/test_modbus_rtu_codec.cpp:72: undefined reference to
    `modbuslens::core::decodeRtuFrame(std::span<unsigned char const, 18446744073709551615ull>)'
...（共 10 处 undefined reference；compile 全部通过，仅 link 失败）
为什么这是预期 RED：接口已声明、测试已写死，唯一缺的是定义本身。
```

### GREEN（实现后）

```text
$ cmake --build --preset debug-local          → [12/12] 全部链接成功
$ ./build/debug/modbuslens_codec_tests.exe -o build/green_codec.txt,txt
exit_code=0
PASS: a01_encodeKnownRequest / a02_decodeKnownRequest / a03_decodeCrcMismatch /
      a04_frameTooShort / a05_encodeEmptyData / a06_exceptionShapedRoundTrip /
      a07_normalFrameRoundTrip
Totals: 9 passed, 0 failed, 0 skipped (3ms)

$ ctest --preset debug-local
4/4: smoke Passed | crc Passed | frame Passed | codec Passed
100% tests passed, 0 tests failed out of 4

$ cmake --build --preset debug-local --clean-first
警告/错误行数 grep = 0（零警告，25 targets）；ctest 再次 4/4 通过
```

RED → GREEN 状态变化实录：`codec` 从"无法链接（10 undefined references）"变为 "Passed"；既有 smoke/crc/frame 全程未破坏。

### Part B Learning / Test Design（本阶段，docs-only）

```text
期望值独立复核（一次性 Python，不进仓库）：
  B02：00 6B 00 03 → startAddress=107, quantity=3          ✓（官方 §6.3 示例）
  B08：06 02 2B 00 00 00 64 → byteCount=6, values=[555,0,100] ✓（官方 §6.3 示例）
  B04：quantity 0 与 126 均越界                             ✓
  B09：byteCount=4 vs 实际 2 字节；B10：byteCount=3 奇数     ✓
官方 PDF 抓取：modbus.org V1.1b3 PDF 返回 404（本环境限制，非官方资源失效——
与 Part A Learning 阶段同口径），复核采用"公开参数 + 官方印刷示例数值对拍"。
git diff --check      → 通过
git diff --name-only  → 仅 docs/；src/、tests/、CMakeLists.txt 未出现
```

## Result

✅ **Part A DONE**：`encodeRtuFrame` / `decodeRtuFrame` 按定稿接口实现并全绿；结构化错误（FrameTooShort / CrcMismatch）可用；CRC 失败不产 Frame；0x83 透明处理；全项目 ctest 4/4、零警告。
✅ **Part B Learning / Test Design DONE（docs-only）**：三个语义模型、独立错误模型、12 用例矩阵（P0×10 + P1×2）、官方金样复核、Deferred-to-T007 清单、40001 边界、12 题问答、18 步实施计划落库。
⬜ **Part B Implementation NOT STARTED** → **T004 整体仍 IN PROGRESS**。

## Knowledge Learned（实现阶段十问索引）

1. **为什么 encode 重算 CRC 而不是 Frame 存 CRC**：CRC 是三字段派生值；Frame 存 CRC 会在改 data 后产生 stale CRC（T003 决策），encode 时对当前字段现算=永远一致。
2. **为什么显式 low-byte-first append**：V1.02 规定 CRC 透传低字节在前；显式 `push_back(low); push_back(high)` 让协议约定出现在代码里，而非藏在内存布局里。
3. **`84 0A` 如何还原成 `0x0A84`**：`(high << 8) | low = (0x0A<<8)|0x84`——与 encode 的拆分互为镜像，用 unsigned 计算避免窄整型符号溢出。
4. **为什么不能 memcpy uint16_t**：memcpy 按主机端序排字节；x86 小端会让寄存器字段打反、CRC"碰巧对"、换平台漂移（Learning Q7）。
5. **为什么 decode 先检查最小长度**：4 字节是 container 结构下界；先判长度才能安全地"分离末尾 CRC"，防读越界（A04 验证）。
6. **为什么 CRC mismatch 返回错误而不是 Frame**：内容不可信，产出 Frame 会污染事务与统计；失败原因本身是诊断数据（A-Q3/A-Q4）。
7. **为什么使用 variant**：需要"成功带值 / 失败带原因"双通道；optional 只有两态、bool 丢原因、expected 是 C++23；`RtuDecodeError` struct 为未来诊断字段留扩展。
8. **为什么 Codec 不保存 raw bytes**：无状态纯函数才能被任意层安全复用；raw bytes 的保存属于会话/诊断层（A-Q10）。
9. **为什么 Codec 不理解 0x03 的 data**：通用容器对所有功能码一视同仁（A06 证明 0x83 透明处理）；业务解释归 Part B，避免通用层被特判污染。
10. **为什么 round-trip 不能替代 KAT**：对称错误时自洽测试仍通过；只有外部 KAT 能锚定"与规范一致"（A-Q5；T002 stub 事件同源印证）。

新增实现体会：先声明后实现的 linker-error RED 是零成本留痕方式；`std::vector → span` 的隐式转换让"CRC 输入=前三段视图"零样板；span 子视图（`first()`）天然表达"payload/CRC 切分"。

## Potential Interview Questions

- 前 19 题（Learning 9 + Part A 设计 10）仍有效。
- Implementation 阶段新增：
  1. 你的 RED 是怎么产生的？（仅声明无定义 → linker error，证据存档，stub 不入库）
  2. `encodeRtuFrame` 为什么 `reserve`？（上限已知一次分配；嵌套 push 不扩张）
  3. `84 0A → 0x0A84` 代码怎么写才与端序无关？（unsigned 移位+或，再 cast）
  4. decode 返回的 Frame 的 data 怎么构造？（span 子视图迭代器对，零拷贝到 vector）
  5. 为什么测试用 `std::get_if` 而不是 `std::get`？（get 失败抛异常——core 不以异常做控制流；get_if 返回指针，测试断言更直白）

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| T003 代码 | `a44a6d2` | （前 LKGC） |
| T004 Learning | `89c9df4` | docs-only |
| T004 Part A Test Design | `f3321d1` | docs-only |
| Part A 代码提交（LKGC） | `73825c6` | `T004(Part A): implement RTU wire codec with CRC validation` |
| Part A 回填 | `015d3eb` | docs-only |
| Part B Test Design | 见 `git log` | `T004(Part B): 0x03 Codec 学习与测试设计（docs-only）` |

> LKGC 推进：Part A 产生新业务代码并经 configure/clean build/full ctest（4/4）验证；LKGC 由 `a44a6d2` 推进至本代码提交，由 docs-only 回填提交写入。**T004 整体未完成（Part B 未开始），不得开始 T005。**