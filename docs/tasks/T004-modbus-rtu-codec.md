# T004 — Modbus RTU Codec

> 状态：**IN PROGRESS**｜Part A（RTU Wire Codec）：**Test Design ✅（docs-only）→ Implementation ⬜**｜Part B（Function 0x03 Codec）：⬜ 未开始
> 协议依据：MODBUS over Serial Line V1.02（帧/CRC/字节序）+ Modbus Application Protocol V1.1b3（0x03 字段语义）。
> 本阶段（Part A Test Design）边界：只设计测试与接口。**禁止**（全部未做）：修改 `src/`、`tests/`、`CMakeLists.txt`；实现 encode/decode/CRC validation；Part B；Simulator/Replay/Serial/QML/Agent。

## Goal

在写 Codec 代码之前定死两件事：① Part A 的接口与错误模型（调用方必须能知道"为什么失败"）；② 七个测试的输入/期望/存在理由/优先级。Part A 的最终职责只有一条：**`ModbusRtuFrame ↔ 完整 RTU wire bytes`**。

## Background

- T002 交付 CRC 计算、T003 交付语义帧模型；Part A 是两者的"接线层"。
- ModbusLens 是诊断工具——**失败原因本身就是重要数据**，因此 decode 的错误模型从设计阶段就按"结构化错误"定案，而不是事后补。
- 为控制复杂度，T004 拆 Part A（wire 编解码）与 Part B（0x03 语义），分别 TDD。

## Technical Decisions（累计）

| 阶段 | 决策 | 内容 | 理由 |
| --- | --- | --- | --- |
| Learning | Part 拆分 | Part A：Frame↔wire（CRC 计算/低字节在前/验证）；Part B：0x03 data 解释 | 通用容器转换与功能码语义正交 |
| Learning | 依赖方向 | Part B 只消费 Part A 产出，不重新实现 CRC | CRC 唯一事实来源 = T002 KAT 锁定函数 |
| Learning | 范围 | fuzz/benchmark 不在 T004；异常码→文字映射归 analysis | 小步交付；性能优化需 profiling 依据 |
| Learning | 字节序 | 全部显式逐字节处理，禁 memcpy uint16 | 协议字节序 ≠ 平台端序 |
| **Part A** | **错误模型** | `std::variant<ModbusRtuFrame, RtuDecodeError>` + `enum class RtuDecodeErrorCode` | 见 §Interface Design：诊断工具必须知道失败原因 |
| **Part A** | **接口形态** | `encodeRtuFrame(const ModbusRtuFrame&) -> std::vector<std::uint8_t>`；`decodeRtuFrame(std::span<const std::uint8_t>) -> RtuDecodeResult` | 纯函数、零 Qt、非拥有输入 |

## 1. Codec 的含义

- **Encoder（编码）**：C++ 对象 → wire bytes。语义对象变成线路上可发送的完整字节序列（含正确 CRC）。
- **Decoder（解码）**：wire bytes → C++ 对象。字节序列经长度检查与 CRC 验证后还原为可信语义对象。

**为什么 Serial Transport 不应该自己实现 Modbus 编码逻辑？** ① 职责不同：transport 搬字节+帧切分（t3.5/缓冲），不懂"03 后面是起始地址"；② 可测试性：独立 Codec 是纯函数，离线毫秒级测试；③ 复用性：Simulator 与 Replay 没有串口但需要同一套编解码（三模式共享核心）；④ 分层解耦：任何一层可替换不牵连另一层。

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

例（验收金样）：Frame{0x01, 0x03, {00,00,00,01}} → CRC 0x0A84 → wire `01 03 00 00 00 01 84 0A`。

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

## 3–5. Part B 字段解释（Learning 阶段已定稿，保留）

- **0x03 Request**：`01 03 00 00 00 01 84 0A` → address 01、function 03、startAddress `00 00`=0、quantity `00 01`=1（均 16-bit big-endian）、CRC 84 0A。
- **0x03 Normal Response**：`01 03 04 00 64 00 C8 …` → byteCount=4；`2 寄存器 × 2 字节 = 4 数据字节`；`00 64`=100、`00 C8`=200。
- **Exception Response**：`0x83 = 0x03 | 0x80`；`0x02` 为 Exception Code——T004 只结构化保存，码值→自然语言映射归 analysis 层。

## 6. Big-endian Register Field

`00 64 → 0x0064 → 100`；`05 DC → 0x05DC → 1500`；概念公式 `value = (highByte << 8) | lowByte`。**两条不同规则**：寄存器/16-bit 协议字段 = 高字节在前；CRC 透传 = 低字节在前（V1.02）。不得用 CPU 本机端序隐式决定协议字节顺序（x86 memcpy uint16 会把寄存器字段打反、对 CRC 是"碰巧对"）。

## Part A — RTU Wire Codec Design（本阶段定稿）

### 职责边界（重申）

Part A **不得理解**：startAddress、quantity、byteCount、register values、exception code 含义——这些属 Part B 或 Analysis。Part A 只做通用容器转换与 CRC 校验。

### Interface Design（比较与定案，不创建代码）

| 方案 | 形态 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- | --- |
| ① bool | `bool decode(..., Frame& out)` | 简单 | 丢失失败原因；出参易误用 | ❌ |
| ② optional | `std::optional<ModbusRtuFrame>` | 惯用 | 只有"有/没有"，说不出为什么失败——对诊断工具是硬伤 | ❌ |
| ③ **variant + enum** | `std::variant<ModbusRtuFrame, RtuDecodeError>` | 调用方能区分失败原因；`RtuDecodeError` 是 struct，未来可加字段（如出错字节偏移）不破坏类型；纯 C++20 | 需要 `std::holds_alternative/get`（可读性尚可） | ✅ **定案** |
| ④ std::expected | C++23 | 语法优雅 | 项目基线 C++20，明确不用 | ❌ |
| ⑤ 异常 / 第三方 Result 库 | — | — | core 不用异常做控制流；不引第三方 | ❌ |

定案签名（namespace `modbuslens::core`，Phase Implementation 时落文件）：

```cpp
enum class RtuDecodeErrorCode { FrameTooShort, CrcMismatch };

struct RtuDecodeError {
    RtuDecodeErrorCode code;
};  // struct 而非裸 enum：未来可加诊断字段而不改变量类型

using RtuDecodeResult = std::variant<ModbusRtuFrame, RtuDecodeError>;

std::vector<std::uint8_t> encodeRtuFrame(const ModbusRtuFrame& frame);
RtuDecodeResult           decodeRtuFrame(std::span<const std::uint8_t> bytes);
```

满足约束：Core 零 Qt 依赖 ✓；调用方知道失败原因 ✓；初学者可懂（variant = "要么 Frame 要么错误"）✓；无 Result framework ✓；无 C++23/第三方库 ✓。

### Part A 测试矩阵

期望值来源：T002 已锁定的 KAT 向量（A01/A02/A03）+ 一次性独立 Python 参考实现（A05/A06/A07 的 CRC/wire 形态，脚本不进仓库）。

| Test ID | Input | Expected Result | Why This Test Exists | What Bug It Can Catch | Priority |
| --- | --- | --- | --- | --- | --- |
| RTU-A01 | Frame{0x01, 0x03, {00,00,00,01}} | wire `01 03 00 00 00 01 84 0A` | 验证拼接顺序（address→function→data→CRC）、T002 CRC 集成、CRC **低字节在前**序列化 | 字段拼接顺序错、CRC 高低位写反、用错 CRC 函数 | **P0** |
| RTU-A02 | wire `01 03 00 00 00 01 84 0A` | Frame{0x01, 0x03, {00,00,00,01}} | 验证 decoder 分离 CRC→验证→恢复语义 Frame 的完整链路 | payload/CRC 切分错、比较对象错（比了 wire 而非 payload） | **P0** |
| RTU-A03 | wire `01 03 00 00 00 02 84 0A`（改一 data byte、保留原 CRC；实际 CRC 应为 `C4 0B`/0x0BC4，已独立复核） | decode failure，`error.code == CrcMismatch`；**不产生 Frame** | 损坏报文不得进入正常业务链路——诊断工具的第一道安全闸 | CRC 比较逻辑写反、"先建 Frame 再校验"的顺序错误 | **P0** |
| RTU-A04 | 空；以及 `01 03 00`（3 字节） | `FrameTooShort`；不得越界读取 | 最小完整 wire frame = address(1)+function(1)+CRC(2) = **4 bytes**；container 层硬下界 | 读越界、长度检查缺失或 `<`/`<=` 边界错 | **P0** |
| RTU-A05 | Frame{0x01, 0x07, {}} | wire `01 07 41 E2`（CRC 0xE241，独立复核） | Part A 允许 data 为空仍能成帧（address+function+CRC）。**注**：这不代表某具体功能码允许空 data——Function 级合法性属 Part B；选 0x07 而非 0x03 以免暗示"空 data 的 0x03 请求合法" | 隐含"data 至少 1 字节"的假设 | P1 |
| RTU-A06 | Frame{0x01, 0x83, {0x02}}（其 wire `01 83 02 C0 F1`，CRC 0xF1C0，独立复核） | encode→decode 后 Frame 与原值相等 | Part A 不需要理解 0x83 也能**透明处理**异常响应形态（0x02=Illegal Data Address 的解释**不在本测试**） | 对功能码值的隐藏假设/特判 | P1 |
| RTU-A07 | Frame{0x01, 0x03, {0x02,0x00,0x64}}（其 wire `01 03 02 00 64 B9 AF`，CRC 0xAFB9，独立复核） | `decode(encode(frame)) == frame` | 基本对称性。**round-trip 是辅助证据**：encoder 与 decoder 犯相同错误时仍可能互相兼容，不能替代 A01/A02 的外部 KAT | encode/decode 不对称（如一侧多剥一字节） | P1 |

### 测试优先级

- **P0：RTU-A01 / A02 / A03 / A04** —— 直接决定 Part A 是否具备正确、安全的最小功能（正确编码、正确解码、拒收坏 CRC、拒收过短帧）。
- **P1：RTU-A05 / A06 / A07** —— 边界与对称性补充。
- 原则不变：**Known Answer Tests 是主要证据；round-trip/invariant 是补充证据**。

### CRC mismatch 的设计意义

CRC mismatch 不是"解析失败后什么都不知道"。未来 ModbusLens 要统计 **Success / Timeout / CRC Error / Exception** 四类结果，因此 Wire Decoder 必须能明确告诉上层 `CrcMismatch`（而非 bool/optional）。但 **Part A 当前不做**：日志系统、统计更新、Transaction 创建、UI 状态——它只返回结构化错误，消费是上层的事。

### Raw Bytes 所有权边界

- `decodeRtuFrame` 只接受 `std::span<const std::uint8_t>`：**不拥有输入**，生命周期归调用方。
- CRC Error 时 Part A **不负责永久保存 raw bytes**。
- 未来若 Transport / Session / Transaction 层为诊断需要保留 `01 03 …` 原始字节，应由**更高层复制并保存**。
- 理由：Wire Codec 是纯转换/验证函数，不承担会话日志存储职责（保持可测试性与无状态）。

### 最大长度规则：知识记录 + Deferred Decision

- 知识：Modbus RTU ADU 最大 **256 bytes**（即 data ≤ 252）。
- 本阶段**不**自动扩展：oversized frame policy、streaming parser、partial frame buffer、t1.5、t3.5——均属 Serial framing / transport 边界设计（T010）。
- **Future Validation / Deferred Decision**：若未来 decoder 增加 `>256 bytes` 检查，将以新错误码扩展 `RtuDecodeErrorCode`（struct 形态的错误使其无需改类型）；何时做由 transport 设计任务决定，Part A v1 不做。

### Phase Implementation Plan（Part A — Implementation，下一阶段）

1. 创建 `RtuDecodeErrorCode` / `RtuDecodeError` / `RtuDecodeResult` 类型；
2. 声明 `encodeRtuFrame()`；
3. 声明 `decodeRtuFrame()`；
4. 写 RTU-A01~A07 测试；
5. 运行并记录真实 RED（stub 或编译失败，只留工作区不入库）；
6. 实现最小 encode；
7. 实现最小 decode；
8. GREEN；
9. clean build；
10. full ctest（smoke/crc/frame 不破坏）；
11. 更新文档；
12. 推进 LKGC；
13. code commit；
14. docs hash backfill（如需要）。

**不人为制造复杂 bug。**

## Knowledge I Must Be Able To Explain

### Learning 阶段 9 题（保留）

**Q1. Codec 是什么？** 把内存语义对象与线传输字节相互转换的独立一层，使"协议含义"与"传输事实"解耦、可离线测试、三种模式复用。
**Q2. Encode 和 Decode 分别是什么？** Encode：Frame→算 CRC→低字节在前追加→wire bytes；Decode：wire bytes→长度检查→分离 CRC→重算比较→一致才生成 Frame。
**Q3. 为什么 Wire Codec 不应该理解 Holding Register？** 通用层只管容器；寄存器语义随功能码变化，属 Part B；混层=每个功能码复制一遍 CRC/长度逻辑。
**Q4. 为什么 0x03 Codec 不应该重新实现 CRC？** CRC 唯一事实来源是 T002 KAT 锁定函数；重复实现=两份可能分叉的真相。
**Q5. `00 64` 为什么等于 100？** 16-bit 大端：`(0x00<<8)|0x64 = 100`。
**Q6. 为什么寄存器高字节在前而 CRC 低字节在前？** 两条互不相干的协议约定（数值表示规范 vs 校验字段透传规定），各自显式实现。
**Q7. 为什么不能 memcpy 一个 uint16_t 到串口？** 主机端序隐式决定字节顺序：寄存器字段会打反、CRC 碰巧对、换平台漂移。
**Q8. CRC 校验失败后为什么不能继续产生正常业务数据？** 内容不可信；生成 Frame 会污染事务配对与统计。正确路径=错误通道（保留 raw bytes、计数、诊断）。
**Q9. Request 和 Response 的 data 为什么格式不同？** 同一功能码两方向语义不同（"要什么" vs "给什么" vs "出错了"），由规范分别定义。

### Part A 阶段 10 题（本阶段新增）

**A-Q1. Encoder 和 Decoder 分别负责什么？** Encoder：Frame→wire（含算 CRC 与低字节在前序列化）；Decoder：wire→长度检查→分离 CRC→重算比较→Frame 或结构化错误。
**A-Q2. 为什么 Decoder 不能只返回 bool？** bool 丢失"为什么失败"；TooShort 与 CrcMismatch 的后续处理完全不同（丢帧 vs 记干扰），上层需要区分。
**A-Q3. 为什么 optional 对诊断工具不够？** optional 只表达"有/没有"；诊断工具的失败原因本身就是数据（要进 Success/CRC Error 统计），必须结构化。
**A-Q4. 为什么 CRC mismatch 不应该产生正常 Frame？** 校验失败的帧内容不可信；让它进入事务分析会污染配对、时延与统计。宁可"少算一条"，不可"算错一条"。
**A-Q5. 为什么 round-trip PASS 不能单独证明 Codec 正确？** 自洽性≠正确性：encoder/decoder 犯对称错误时 round-trip 照样通过（如双侧都把 CRC 高低位写反）。外部 KAT 才锚定"与别人一致"。
**A-Q6. 为什么 Part A 能处理 0x83 却不需要理解异常码？** 对 Part A 而言 0x83 只是"一个功能码字节"，data 只是字节——通用容器不解释内容（A06 验证透明性）；解释是 Part B/Analysis 的事。
**A-Q7. 为什么最低 wire frame 可看作 4 bytes？** 任何 RTU 帧 = address(1)+function(1)+CRC(2)，再短就无法容纳"一帧"的结构要素；4 字节是 container 层硬下界。
**A-Q8. 为什么具体 function 的长度合法性不能由 Part A 判断？** "0x03 请求必须 4 字节 data"是功能码语义；Part A 对所有功能码一视同仁，否则通用层被业务特判污染（0x83 透明处理同理）。
**A-Q9. 为什么 decode 用 span 而不是拥有 vector？** 纯函数不拥有输入：调用方（transport 缓冲）持有生命周期；span 零拷贝、也迫使"raw bytes 归高层保存"的边界清晰。
**A-Q10. CRC Error 的 raw bytes 最终由哪一层保存？** 更高层（Transport/Session/Transaction，按诊断需求），由它复制并关联时间戳/来源；Codec 保持无状态纯函数。

## Implementation

**未发生。** Part A Implementation 待下一阶段按上述 14 步执行；`src/`、`tests/`、`CMakeLists.txt` 当前零改动。

## Files Changed

Learning 阶段：`docs/tasks/T004-modbus-rtu-codec.md`（新增）、`docs/devlog/2026-09-05-T004-Learning.md`（新增）、`docs/BACKLOG.md`、`docs/PROJECT_STATUS.md`。

Part A Test Design（本阶段）：
- 修改：`docs/tasks/T004-modbus-rtu-codec.md`（本文件：接口定案、测试矩阵 A01–A07、CRC-mismatch 设计意义、Raw bytes 边界、Deferred 决策、实施计划、Part A 10 题）
- 修改：`docs/PROJECT_STATUS.md`（Current Part/Phase/Next Action/Next Part）、`docs/BACKLOG.md`（T004 行状态）
- 新增：`docs/devlog/2026-09-05-T004-PartA-TestDesign.md`

始终未改动：`src/`、`tests/`、`CMakeLists.txt`、presets。

## Problems Encountered

无实现问题（docs-only）。设计权衡记录：decode 错误模型在 bool/optional/variant 三案中定案 variant——原因与取舍已写入 §Interface Design，供 Part A Implementation 直接引用。

## Solutions

见上——文档阶段无阻塞问题。

## Verification（docs-only 各阶段）

Learning 阶段：`git diff --check` 通过；变更仅 docs。Part A Test Design（本阶段）：

```text
期望值独立复核（一次性 Python，不进仓库）：
  A03 前提：crc(01 03 00 00 00 02)=0x0BC4 ≠ 0x0A84 → CrcMismatch 用例成立
  A05：crc(01 07)=0xE241 → wire 01 07 41 E2
  A06：crc(01 83 02)=0xF1C0 → wire 01 83 02 C0 F1
  A07：crc(01 03 02 00 64)=0xAFB9 → wire 01 03 02 00 64 B9 AF
git diff --check      → 通过
git diff --name-only  → 仅 docs/；src/、tests/、CMakeLists.txt 未出现
```

## Result

Part A Test Design 完成：接口与错误模型定案、7 用例矩阵（P0×4 + P1×3）、CRC-mismatch 设计意义、Raw bytes 所有权边界、最大长度 Deferred 决策、14 步实施计划、10 题问答全部落库。**Part A/B 均未实现**；T004 整体 IN PROGRESS。

## Knowledge Learned

- 错误模型要在设计期定案：`RtuDecodeError` 用 struct 而非裸 enum，给未来"出错偏移"等诊断字段留了无损扩展点。
- 测试期望值先独立复核再落文档（延续 T002 惯例）；A03 的"改一 data byte 保留原 CRC"比随机坏 CRC 更有针对性——它隔离了"CRC 计算错"与"比较逻辑错"。
- Deferred Decision 也是决策：把"不做什么"（oversized policy/streaming/t1.5/t3.5）与理由写成显式条目，防止后续任务无意识越界。
- span 非拥有输入倒逼所有权边界清晰：Codec 无状态，raw bytes 保存职责自然落在更高层。

## Potential Interview Questions

Learning 9 题 + Part A 10 题（见两个清单）。后续 Implementation 阶段将补充：`std::variant` 访问模式与异常安全、encode 返回 vector 的分配策略、decode 与 transport 缓冲的配合。

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| T003 代码 | `a44a6d2` | （LKGC） |
| T004 Learning | `89c9df4` | `T004(Learning): RTU Codec 学习与范围细化（docs-only）` |
| Part A Test Design | 见 `git log` | `T004(Part A): Wire Codec 测试设计与接口定案（docs-only）` |

> LKGC 不推进：本阶段 docs-only（LKGC 保持 `a44a6d2`）。