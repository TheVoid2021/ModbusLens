# T004 — Modbus RTU Codec

> 状态：**IN PROGRESS**｜Part A（RTU Wire Codec）：**DONE ✅**（Test Design + Implementation，RED→GREEN 全程留痕）｜Part B（Function 0x03 Codec）：⬜ NOT STARTED
> 协议依据：MODBUS over Serial Line V1.02（帧/CRC/字节序）+ Modbus Application Protocol V1.1b3（0x03 字段语义）。
> Part B 边界预告（未实现）：只解释 0x03 的 data 字段；Simulator/Replay/Serial/QML/Agent/benchmark/fuzz 全部不在 T004。

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

## Files Changed

Learning：`docs/tasks/T004-modbus-rtu-codec.md`、`docs/devlog/2026-09-05-T004-Learning.md`、`docs/BACKLOG.md`、`docs/PROJECT_STATUS.md`。
Part A Test Design：`docs/tasks/T004-modbus-rtu-codec.md`、`docs/devlog/2026-09-05-T004-PartA-TestDesign.md`、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`。
Part A Implementation（本阶段）：
- 新增：`src/core/protocol/ModbusRtuCodec.h`、`src/core/protocol/ModbusRtuCodec.cpp`、`tests/test_modbus_rtu_codec.cpp`、`docs/devlog/2026-09-06-T004-PartA.md`
- 修改：`CMakeLists.txt`（core 源 + codec target）、`docs/tasks/T004-modbus-rtu-codec.md`（本文件补齐）、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、`docs/02_ARCHITECTURE.md`、`docs/04_TEST_STRATEGY.md`、`docs/INTERVIEW_NOTES.md`
- 未改动：`ModbusCrc.{h,cpp}`、`ModbusRtuFrame.h`、`src/main.cpp`、既有三个测试文件、presets

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

## Result

✅ **Part A DONE**：`encodeRtuFrame` / `decodeRtuFrame` 按定稿接口实现并全绿；结构化错误（FrameTooShort / CrcMismatch）可用；CRC 失败不产 Frame；0x83 透明处理；全项目 ctest 4/4、零警告。
⬜ **Part B NOT STARTED**（0x03 字段解释）→ **T004 整体仍 IN PROGRESS**。

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
| Part A 代码提交（**新 LKGC**） | `73825c6` | `T004(Part A): implement RTU wire codec with CRC validation` |
| 回填提交（docs-only，HEAD） | 见 `git log` | 回填哈希 |

> LKGC 推进：Part A 产生新业务代码并经 configure/clean build/full ctest（4/4）验证；LKGC 由 `a44a6d2` 推进至本代码提交，由 docs-only 回填提交写入。**T004 整体未完成（Part B 未开始），不得开始 T005。**