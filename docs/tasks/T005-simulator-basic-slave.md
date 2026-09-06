# T005 — Simulator Basic Slave

> 状态：**DONE ✅**（2026-09-06）｜Learning / Test Design ✅｜Implementation ✅（RED→GREEN 全程留痕）
> 前置确认：T004 DONE、M2 Protocol Core DONE。
> 交付边界回顾：只实现 `SimulatedSlave`（单设备地址 + Function 0x03）；**未实现** IFrameSource/VirtualMaster/轮询/虚拟时钟/seed（推迟）、Timeout/CRC fault/delay（T006）、broadcast；未开始 T006。

## Goal

设计一个**纯 C++ 协议端点** `SimulatedSlave`：一个设备地址 + 一段连续 Holding Registers，收到 `ModbusRtuFrame` 请求后产生协议正确的响应 Frame（正常响应或 Modbus 异常响应），让 T002→T003→T004→T005 第一次组成完整可验证闭环。

## Background

- 核心层已齐备：CRC（T002）、语义 Frame（T003）、wire 编解码（T004A）、0x03 字段解释（T004B）。缺的是"会响应的设备"。
- Demo A（DEMO_GUIDE）的保底演示完全依赖 Simulator；越早有一个最小可用的 Slave，后续 Fault Injection（T006）、Transaction（T007）、UI（T008）都有真实数据可用。

## Scope（已收缩，收缩理由见 §Technical Decisions）

**T005 只实现**：

- 一个设备地址
- Holding Registers（连续空间）
- Function 0x03
- Normal Response
- Illegal Data Address 异常响应（0x83 / 0x02）

**明确删除 / 推迟**（本任务不做）：

| 删除/推迟项 | 去向 | 理由 |
| --- | --- | --- |
| IFrameSource 抽象 | 推迟（无定期） | 当前只有 Simulator 一个真实数据源；过早抽象=为想象中的需求设计接口。等 Replay（T009）/Serial（T010）出现后按真实共同需求提取（rule of three） |
| VirtualMaster / polling loop | 推迟 | 轮询属于"会话编排"，不属于"设备端点"；T007 事务分析时按需设计 |
| virtual clock / random seed | 推迟 | 只有引入时间与随机性才需要它们；v1 Simulator 是纯同步纯函数式端点 |
| Timeout / delay / CRC fault / random fault / frame loss | **T006 Fault Injection** | 故障注入是独立关注点，在能用的正常响应之上叠加 |
| Replay / Serial / QML / Agent | T009+ / T010+ / T008 / T012+ | 不变 |
| Modbus broadcast（地址 0） | 后续（无定期） | v1 明确不支持，收到即按地址不匹配处理（见 §Device Address） |

## Simulator 的角色

Simulator 模拟的是 **Modbus Slave / Server**——一个纯 C++ 协议端点：

- **不是 GUI**、**不是 LLM**、**不是串口**；不拥有线程、时钟与随机性。
- 输入：`ModbusRtuFrame`（请求）；输出：`ModbusRtuFrame`（响应）或"不是发给我的"。
- 例：

```text
Simulated Slave #1（Holding Registers: 0=100, 1=200, 2=1500, 3=67, 4=1）
收到：function=03, startAddress=0, quantity=2
返回：values = {100, 200}（组装为 byteCount=4 的 0x03 正常响应 Frame）
```

## Technical Decisions

| 决策 | 内容 | 理由 |
| --- | --- | --- |
| 载体 | `src/core/simulator/SimulatedSlave.{h,cpp}`，进 `modbuslens_core` | 纯协议端点=核心逻辑；零 Qt、可 offscreen 测试 |
| 寄存器存储 | `std::vector<std::uint16_t>`（连续空间，set 时按需增长、补 0） | 见 §存储方案比较：slave 寄存器文件天然连续，vector 最简单直白 |
| 结果模型 | `SimulatorResult = std::variant<ModbusRtuFrame, IgnoredRequest>` | 协议级结果一律用 **Frame 表达**（含异常响应）；唯一非 Frame 结果是"不是发给我的"。不建庞大 Error system |
| 复用解码 | 请求解释**必须**调用 `decodeReadHoldingRegistersRequest()`（T004B） | 禁止手写第二份 0x03 parser（T004 Learning Q3/Q4 决策延续） |
| 异常码集合 | 0x01 Illegal Function（fn≠0x03）、0x02 Illegal Data Address（越界）、0x03 Illegal Data Value（malformed request data） | 三者都是 V1.1b3 规定的 slave 标准行为，非文字映射；自然语言说明仍归 Analysis 层 |
| 地址不匹配 | 返回 `IgnoredRequest`，绝不代表别的设备回答 | 一台从站只对自己的地址负责；broadcast v1 不支持 |
| 越界判定 | `startAddress + quantity > registers.size()` 即 0x02（用 32 位计算防 uint16 溢出） | 覆盖"起点越界"与"区间跨越末尾"两类（SIM-T04/T05） |

## 存储方案比较（vector vs unordered_map）

| 方案 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- |
| `std::vector<std::uint16_t>` | O(1) 下标、缓存友好、语义=连续寄存器空间、测试断言直观 | 稀疏大地址会浪费内存；set 需要扩容策略 | ✅ **v1 采用** |
| `std::unordered_map<uint16_t,uint16_t>` | 稀疏地址省内存 | 遍历无序、代码噪音大、真实 slave 寄存器文件本就是连续块 | ❌ 过度设计 |

扩容策略：`setHoldingRegister(addr, v)` 时若 `addr+1 > size` 则 resize 到 `addr+1` 并以 0 填充（未初始化寄存器=0，行为确定、可测）。

### 寄存器空间语义（v1 明确设计行为）

当前 `SimulatedSlave` 使用 `std::vector<std::uint16_t>` 表示**连续 Holding Register 空间**。例如执行 `setHoldingRegister(4, 100)` 时，若此前 vector 小于 5，则 `resize(5, 0)`——此后合法地址范围为 **0 ~ 4**，其中未显式写入的位置默认值为 **0**。它模拟的是**连续寄存器文件**，而不是稀疏寄存器映射；未来如果真实设备 profile 需要稀疏寄存器空间，再重新评估数据结构（v1 不做复杂 map/profile 系统）。

## 数据模型（定案，不实现）

```cpp
struct IgnoredRequest {};   // 帧不是发给本设备的（v1 无 broadcast）

using SimulatorResult = std::variant<ModbusRtuFrame, IgnoredRequest>;

class SimulatedSlave {
public:
    explicit SimulatedSlave(std::uint8_t address);

    void setHoldingRegister(std::uint16_t address, std::uint16_t value);

    // 协议级结果（正常响应/异常响应）都用 Frame 表达；
    // 只有"地址不匹配"返回 IgnoredRequest。v1 无 SimulatorError——
    // 协议错误走异常响应，内部误用（未来 API misuse）届时再议。
    SimulatorResult handleRequest(const ModbusRtuFrame& request);

private:
    std::uint8_t address_;
    std::vector<std::uint16_t> holdingRegisters_;
};
```

## 正常 Request 流程（全部复用既有核心）

```text
Request Frame {address=1, functionCode=3, data={00,00, 00,02}}
    ↓ handleRequest：确认 address == 1
    ↓ decodeReadHoldingRegistersRequest()        （T004B，禁手写解析）
      startAddress=0, quantity=2
    ↓ 越界检查：0 + 2 ≤ registers.size()
    ↓ 读 registers[0]=100, registers[1]=200
    ↓ 组装 response data：{04, 00,64, 00,C8}     （byteCount + 大端寄存器字节）
ModbusRtuFrame {address=1, functionCode=3, data={04, 00,64, 00,C8}}
```

CRC 由调用方经 Part A `encodeRtuFrame()` 完成——Simulator 只产出 Frame（SIM-I01 验证全链路）。

## Illegal Address 流程

寄存器数量=5（合法 0~4）时收到 `startAddress=100, quantity=2`：

```text
decode 成功 → 越界检查失败
→ ModbusRtuFrame {address=1, functionCode=0x83, data={0x02}}
```

- `0x02` 在这里是 **Illegal Data Address** 的数值：Simulator 必须知道它才能产生符合协议的响应；
- 但文字"Illegal Data Address"**不进入** core response model——数值即契约，翻译归 Analysis（与 T004 exceptionCode 决策一致）。

## Device Address 行为（v1 定案）

Slave address=1 收到 `request.address=2`：

- 返回 **`IgnoredRequest`**，**不得**代表 Device 1 回复 Device 2（协议角色错误）；
- 同理：v1 不模拟 broadcast（地址 0）——收到 broadcast 帧按"地址不匹配"处理并记录在该决策中，未来需要时再单独设计"处理但不响应"的语义。

## Malformed Request 策略（SIM-T07 讨论，定案）

请求 Frame 的 functionCode=0x03 但 data 畸形（长度≠4 或 quantity 越界）：

- **不手写 parser 补救**；直接把 T004 decoder 的错误映射为协议异常：
  - `InvalidRequestLength` / `InvalidQuantity` → 异常响应 `0x83 / 0x03`（Illegal Data Value，V1.1b3 规定的值错误响应）；
  - `WrongFunctionCode`（functionCode≠0x03，含把响应当请求送进来）→ 异常响应 `fn|0x80 / 0x01`（Illegal Function）。
- 理由：这三类异常码都是 slave 必须实现的标准行为，实现成本≈0（同一条异常构造路径），并且避免"静默吞掉畸形帧"；未引入文字映射、未扩 API。

## Test Design

| Test ID | 场景 | Expected | Why This Test Exists | What Bug It Can Catch | Priority |
| --- | --- | --- | --- | --- | --- |
| SIM-T01 | Slave#1，reg[0]=100；请求 read start=0, qty=1 | Frame{1, 0x03, {02, 00,64}} | 单寄存器最小正例 | 响应组装、byteCount 计算 | **P0** |
| SIM-T02 | reg[0]=100, reg[1]=200；qty=2 | Frame{1, 0x03, {04, 00,64, 00,C8}} | 多寄存器顺序拼接 | 寄存器次序/字节对错位 | **P0** |
| SIM-T03 | reg[0..2]=100/200/1500；start=1, qty=2 | values {200,1500} → Frame{1, 0x03, {04, 00,C8, 05,DC}} | 非零起始地址（0x05DC=1500 验证大端写侧） | startAddress 偏移错误、写侧大端颠倒 | **P0** |
| SIM-T04 | reg[0..2]；start=10, qty=1 | Frame{1, 0x83, {0x02}} | 起点即越界 → Illegal Data Address | 缺越界检查、异常码错 | **P0** |
| SIM-T05 | reg[0..2]；start=2, qty=2（reg 3 不存在） | Frame{1, 0x83, {0x02}} | **区间跨越末尾**（start 合法但 start+qty 越界） | 只查起点不查区间的典型漏洞 | **P0** |
| SIM-T06 | Slave#1；request.address=2 | `IgnoredRequest`；无任何 Frame | 设备地址归属：Slave#1 不得替 Device2 回答 | 地址检查缺失、误回复 | P1 |
| SIM-T07 | 请求 fn=0x03 但 data 畸形（长度≠4 / qty 越界）；以及 fn≠0x03 | 0x83/0x03（malformed）；fn\|0x80 / 0x01（unknown fn） | 复用 T004 decoder 且不静默吞帧 | 手写第二份 parser、静默失败 | P1 |
| **SIM-I01** | **全链路闭环**（见下） | 最终 values == {100, 200} | T002+T003+T004+T005 第一次组成完整闭环 | 层间接缝（Frame 所有权、CRC 校验、语义解释） | **P0** |

### SIM-I01 — Full Protocol Round Trip（集成测试，P0）

```text
测试充当 Master：
① 构造 Request Frame {1, 0x03, {00,00,00,02}}
② encodeRtuFrame()            → 完整 wire request（T004A + T002 CRC）
③ decodeRtuFrame(wire)        → 还原 Frame（验证 wire 层无损）
④ SimulatedSlave.handleRequest(frame) → Response Frame
⑤ encodeRtuFrame(response)    → 完整 wire response
⑥ decodeRtuFrame(wire)        → 还原 Response Frame
⑦ decodeReadHoldingRegistersResponse() → values
断言：values == {100, 200}
```

仍不涉及：串口、线程、Timer、QML。这一条测试是 M2→M3 的"系统首次通电"。

## Forbidden（T005 全程禁止）

sleep、QTimer、QThread、random、timeout、CRC corruption、frame loss、latency、serial、QSerialPort、QML、Agent——全部归 T006 或更后。

## Knowledge I Must Be Able To Explain（12 题）

**S-Q1. Simulator 模拟的是什么？** Modbus Slave/Server——一个纯 C++ 协议端点：收 Frame、回 Frame。不是 GUI、不是 LLM、不是串口。
**S-Q2. 为什么 Simulator 应该是纯 C++？** 协议端点与 UI/IO 解耦才能离线毫秒级测试、被三种模式与未来 GUI 复用；Qt 类型会把"协议设备"绑死在应用框架上。
**S-Q3. 为什么必须复用 Function03 decoder？** 同一协议事实只允许一份实现（T004 决策）；复用=自动继承 B01~B12 的全部测试保证，手写第二份 parser 早晚分叉。
**S-Q4. 为什么不能重新手写一套请求解析？** 双实现无法保证字节序/边界/错误码口径一致，且使测试矩阵翻倍却只增加风险。
**S-Q5. Holding Register 在 Simulator 中如何保存？** `std::vector<std::uint16_t>` 连续空间，set 时按需扩容补 0；读越界即协议异常（0x02）。
**S-Q6. 为什么越界访问应返回 Exception Response？** 这正是 Modbus 协议对"地址不存在"的规定行为（0x02 Illegal Data Address）——slave 必须表现得像真设备，而不是崩溃或静默。
**S-Q7. 为什么请求别的 Device Address 不应由当前 Slave 回复？** 总线上每个从站只对自己的地址负责；替别人回答会让诊断数据失真（真实设备不会这么干）。v1 收到即 IgnoredRequest。
**S-Q8. Normal Response 和 Exception Response 的区别？** 正常：fn 原样、data=byteCount+寄存器字节；异常：fn|0x80、data=单字节异常码。对 Simulator 而言两者都是"合法的 Frame 输出"。
**S-Q9. Simulator 为什么暂时不做 Timeout？** Timeout 是"请求发出后等待多久"的主站/会话行为；本 Simulator 是被动应答端点，同步返回、无时间概念——Timeout 归 T006/T007。
**S-Q10. Full Protocol Round Trip 测试证明了什么？** T002（CRC）→T003（Frame）→T004A（wire）→T004B（0x03 语义）→T005（设备行为）的层间接缝第一次被端到端验证——各层正确≠组合正确。
**S-Q11. 为什么现在不做 IFrameSource 抽象？** 只有一个真实数据源时提取的接口来自想象；等 Replay/Serial 出现真实共同需求再抽象（rule of three），避免为假设设计。
**S-Q12. 为什么没有硬件时 Simulator 对项目很重要？** 它让"无设备也能开发/测试/演示"成立：T006 故障注入、T007 事务分析、T008 UI、Demo A 全部踩在它之上；也是 CI 里唯一确定的流量来源。

## Implementation Plan（下一阶段）

1. 创建 `src/core/simulator/SimulatedSlave.h`（模型 + IgnoredRequest + SimulatorResult）；
2. 创建 `SimulatedSlave.cpp`（构造/set/handleRequest；复用 `decodeReadHoldingRegistersRequest`；异常构造 helper）；
3. 写响应侧大端 helper（write side，仅文件内使用）；
4. 加入 `modbuslens_core`；新建 `tests/test_simulated_slave.cpp`（SIM-T01~T07）与 `tests/test_simulator_integration.cpp`（SIM-I01）；
5. CMake：两个测试 target（或合并为一个 target 两个套件——实现时定）+ ctest `simulator`；
6. RED（仅声明无定义 → linker error，如实记录）；
7. 实现至 GREEN；
8. clean build + full ctest（6/6：smoke/crc/frame/codec/f03/simulator，既有全保）；
9. 文档归档；code commit；推进 LKGC；docs backfill（如需要）。

**禁止**：Function 03 之外的任何功能码、encode 请求侧（Master 角色归测试本身）、线程/时钟/随机性。

## Implementation（实录，2026-09-06）

### 新增文件

- `src/core/simulator/SimulatedSlave.{h,cpp}`：`IgnoredRequest`（defaulted `==`）、`SimulatorResult = variant<ModbusRtuFrame, IgnoredRequest>`、`SimulatedSlave`（explicit ctor / `setHoldingRegister` / **const** `handleRequest`——从站是纯应答端点，状态只经 set 改变，const 无需任何妥协）。
- `tests/test_simulated_slave.cpp`：SIM-T01~T07（T07 拆 a/b），8 个测试函数。
- `tests/test_simulator_integration.cpp`：SIM-I01 全链路闭环（encode→decode→handleRequest→encode→decode→0x03 解释，wire 金样 `…C4 0B` / `…BA 7A` 逐字节断言）。
- `CMakeLists.txt`：core 加入 `SimulatedSlave.cpp`；两个测试 target + ctest `simulator` / `simulator_integration`。

### handleRequest 实际处理顺序（与设计一致）

1. **地址归属**：`request.address != address_` → `IgnoredRequest{}`（含地址 0，v1 无 broadcast）；
2. **功能分发**：functionCode≠0x03 → 异常帧 `fn|0x80 / {0x01}`（Illegal Function）；
3. **复用 T004**：`decodeReadHoldingRegistersRequest(request)`，任何 `Function03DecodeError` → 异常帧 `0x83 / {0x03}`（Illegal Data Value）——不静默、不 SimulatorError；
4. **越界判定**（32 位提升防 uint16 回绕）：`start ≥ size || start+count > size` → 异常帧 `0x83 / {0x02}`（Illegal Data Address），不做部分读取；
5. **正常响应**：`byteCount = 2×count`（≤250 安全入 uint8）+ 每寄存器显式移位"高字节在前"（禁 memcpy）。

寄存器空间语义（实现前已定案）：vector 连续寄存器文件，set 时 `resize(addr+1, 0)` 补 0——中间地址默认 0 是明确行为，不是疏忽。

## Files Changed（本阶段实现）

- 新增：`src/core/simulator/SimulatedSlave.{h,cpp}`、`tests/test_simulated_slave.cpp`、`tests/test_simulator_integration.cpp`、`docs/issues/ISSUE-001-variant-test-dangling-pointer.md`、`docs/devlog/2026-09-06-T005-Implementation.md`
- 修改：`CMakeLists.txt`（core 源 + 两个 target）、`docs/tasks/T005-simulator-basic-slave.md`（本文件补齐）、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、`docs/02_ARCHITECTURE.md`、`docs/04_TEST_STRATEGY.md`、`docs/INTERVIEW_NOTES.md`
- **测试脚手架修复（语义零变化）**：`tests/test_modbus_rtu_codec.cpp`、`tests/test_function03.cpp` 中与 ISSUE-001 相同的悬垂指针模式被一并修复（断言内容逐字未动）——见 Problems #2
- 未改动：`ModbusCrc.{h,cpp}`、`ModbusRtuFrame.h`、`ModbusRtuCodec.{h,cpp}`、`Function03.{h,cpp}`、`src/main.cpp`、`test_smoke.cpp`、`test_modbus_crc.cpp`、`test_modbus_rtu_frame.cpp`、presets

## Problems Encountered

1. **RED 如预期**：仅声明无定义时两个测试目标链接失败——configure PASS、compile PASS、link FAIL，共 **29 处 undefined reference**（去重符号：`SimulatedSlave::SimulatedSlave(unsigned char)` / `setHoldingRegister(unsigned short, unsigned short)` / `handleRequest(...) const`）。未提交 RED 状态。
2. **ISSUE-001（本任务最重要的问题，已建档）**：首版实现后 t01 断言失败而 T02~T07 全过；同批集成测试编译失败（GCC `taking address of rvalue` ×4）。根因是**同一个**：variant 测试辅助函数 `as<T>(f())` 返回"指向临时 variant 内部"的指针——临时在语句末析构，指针悬垂（UB）；t01 的后续 `expected` 栈构造恰好覆写了该存储。编译错误与运行期失败同根：GCC 对显式 `&右值` 报错，对"const& 绑定临时 + 内部取址"的等价悬垂形态却静默。同一模式潜伏在 T004 已提交的 codec/f03 测试中（当时"全过"只是尚未被覆写）。
3. **修复**：辅助函数改 `std::optional<T>`（拷贝语义，从结构上消灭悬垂）；集成测试绑定具名局部量后再 `get_if`；T004 两个测试文件以相同方式修复（断言逐字未动）。详见 [ISSUE-001](../issues/ISSUE-001-variant-test-dangling-pointer.md)。
4. **实现本身：No significant implementation issue encountered.** 悬垂指针问题出在测试脚手架而非 SimulatedSlave——修复后 t01 立即稳定通过，反证实现无误。

## Solutions

1. RED 证据存档后按 Test Design 实现。
2. 按 ISSUE-001 的方案修复三个测试文件 + 集成测试，并创建首份 Issue 档案。
3. ctest 由"2 个 Not Run + 1 个失败"恢复为 7/7 全绿。

## Verification（本阶段，docs-only）

```text
git diff --check        → 通过（无空白/行尾问题）
git diff --name-only    → 仅 docs/ 下文件；src/、tests/、CMakeLists.txt 未出现
```

## Verification（Implementation，2026-09-06）

### RED（仅声明、无定义；未提交）

```text
$ cmake --preset debug-local      → configure PASS
$ cmake --build --preset debug-local
两个测试目标链接失败；undefined reference 共 29 处，去重符号：
  `modbuslens::core::SimulatedSlave::SimulatedSlave(unsigned char)`
  `modbuslens::core::SimulatedSlave::setHoldingRegister(unsigned short, unsigned short)`
  `modbuslens::core::SimulatedSlave::handleRequest(modbuslens::core::ModbusRtuFrame const&) const`
compile 全部通过，仅 link 失败——预期 RED。
```

### 过程问题（详见 Problems #2 / ISSUE-001）

```text
首版实现后：
  t01 FAIL（悬垂指针读被覆写栈内存），T02~T07 PASS
  集成测试 compile FAIL：taking address of rvalue ×4
→ 修复：optional 拷贝语义辅助函数 + 具名局部量（T004 两个测试文件同批修复）
```

### GREEN（修复后）

```text
$ cmake --build --preset debug-local              → [12/12] 全部链接成功
$ ./build/debug/modbuslens_simulator_tests.exe
  PASS: t01~t07（t07 拆 a/b）  Totals: 10 passed, 0 failed (2ms)
$ ./build/debug/modbuslens_simulator_integration_tests.exe
  PASS: i01_fullProtocolRoundTrip  Totals: 3 passed, 0 failed (4ms)

$ ctest --preset debug-local
7/7: smoke | crc | frame | codec | f03 | simulator | simulator_integration 全部 Passed
100% tests passed, 0 tests failed out of 7

$ cmake --build --preset debug-local --clean-first
警告/错误行数 grep = 0（零警告，39 targets）；ctest 再次 7/7 通过
```

RED → GREEN 状态变化实录：`simulator`/`simulator_integration` 从"无法链接（29 undefined references）"变为 "Passed"；既有五个测试全程未破坏。

## Result

✅ **T005 DONE**：`SimulatedSlave` 落地 `modbuslens_core`（零 Qt、同步、const 接口）；SIM-T01~T07 + SIM-I01 全绿；SIM-I01 首次打通 T002→T003→T004A→T004B→T005 完整协议闭环（wire 金样逐字节断言）；全项目 ctest 7/7、clean 重建零警告；范围收缩承诺全部兑现（无 IFrameSource/VirtualMaster/时钟/seed/故障注入）。

## Knowledge Learned

- **"端点"视角**：Slave 是纯函数式协议端点（Frame 进、Frame 出），把会话/时间/故障全部留在外面——这让 T005 极小，也让 T006 注入故障时不需要改动它。
- **抽象时机纪律**：IFrameSource 推迟是本任务最重要的决定——单数据源阶段提取的接口必然来自想象；接口应从第二、第三个实现的真实共性中"长"出来。
- **协议级结果 vs 宿主级结果**：越界、malformed 都是"合法的协议结果"（Frame）；只有"不是发给我的"才是宿主级结果（IgnoredRequest）——分清两者让 variant 只需两个分支。
- SIM-I01 的设计价值：集成测试不必大——一条贯穿四层的 8 步链路就是"系统首次通电"。
- **实现阶段新增**：
  1. **返回指针必问生命周期**：`as<T>(f())` 的悬垂指针（ISSUE-001）证明"测试全过 ≠ 无 UB"；`optional<T>` 拷贝语义把正确性做进辅助函数，比约束每个调用点可靠。
  2. **异常响应也是"正常输出"**：makeExceptionFrame 与正常响应走同一条返回路径，错误处理代码量≈0 且行为可测。
  3. **越界判定要防整型回绕**：start+quantity 在 uint16 域会回绕，提升到 32 位后判定；这是"只查起点不查区间"漏洞的标准解法。
  4. **const handleRequest**：从站是纯应答者，const 既表达设计意图，也强制状态变更只能走 set 接口。

## Potential Interview Questions

- 12 题见上（Knowledge I Must Be Able To Explain）。
- Implementation 阶段新增：
  1. 越界判定为什么要提升到 32 位？（uint16 start+quantity 回绕会让非法区间"看起来合法"）
  2. 为什么 handleRequest 是 const？（纯应答端点；状态变更只经 set 接口——设计意图进类型系统）
  3. ISSUE-001 讲了什么？（variant 临时生命周期 → 悬垂指针 → optional 拷贝语义修复；"测试全过 ≠ 无 UB"）
  4. SIM-I01 的 wire 金样（C4 0B / BA 7A）从哪来？（一次性独立脚本按 T002 算法复核，断言逐字节）

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| T004 Part B 代码 | `e8b62f6` | （前 LKGC） |
| T005 Learning | `0a99870` | docs-only |
| T005 代码提交（**新 LKGC**） | `3a896df` | `T005: implement deterministic Modbus simulated slave` |
| 回填提交（docs-only，HEAD） | 见 `git log` | 回填哈希 |

> LKGC 推进：T005 产生新业务代码并经 configure/clean build/full ctest（7/7）验证；LKGC 由 `e8b62f6` 推进至 T005 代码提交，由 docs-only 回填提交写入。**T005 DONE；T006 未开始。**