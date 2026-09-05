# T004 — Modbus RTU Codec

> 状态：**IN PROGRESS — Learning / Scope Refinement**（2026-09-05，docs-only）
> 内部拆分：**Part A — RTU Wire Codec** 与 **Part B — Function 0x03 Codec**，两 Part 分别经"Test Design → Implementation"推进，**不得一次性实现**。
> 协议依据：MODBUS over Serial Line V1.02（帧/CRC/字节序）+ Modbus Application Protocol V1.1b3（0x03 字段语义）。
> 本阶段边界：只学习与留痕。**禁止**（本次全部未做）：修改 `src/`、`tests/`、`CMakeLists.txt`；实现 Codec；开始 Simulator；fuzz；benchmark。

## Goal

在写任何 Codec 代码之前，讲清楚三件事：① Codec 这一层为什么存在、装什么；② Part A（wire 编解码）与 Part B（0x03 字段解释）的职责边界；③ 协议里两种字节序规则（寄存器大端 vs CRC 低字节在前）为什么不同、为什么都不能依赖 CPU 端序。

## Background

- T002 交付 CRC 计算、T003 交付语义帧模型（`ModbusRtuFrame`，不含 CRC）。两者之间缺一层转换：**T004 = 把语义对象与线路字节相互转换的 Codec 层**。
- 为控制学习与实现复杂度，T004 拆成两个 Part，各自独立 TDD，避免"一个任务同时理解字节序、CRC、功能码语义三件事"。

## Technical Decisions（Learning 阶段已确定）

| 决策 | 内容 | 理由 |
| --- | --- | --- |
| Part 拆分 | Part A：`ModbusRtuFrame ↔ 完整 RTU wire bytes`（含 CRC 计算/低字节在前序列化/验证）；Part B：解释 0x03 的 data 字段 | 通用容器转换与功能码语义正交；拆开后每层可独立测试、独立讲解 |
| 依赖方向 | Part B 只消费 Part A 产出的 `ModbusRtuFrame`；**不重新实现 CRC** | CRC 唯一事实来源是 T002 的 `calculateModbusCrc`（KAT 锁定）；重复实现=两份可能分叉的真相 |
| 范围修订 | fuzz、benchmark 确认不在 T004；"一次性实现全部 Codec"禁止 | fuzz 待 codec 落地后再评估；性能优化需 profiling 依据；小步交付 |
| 异常码 | T004 只结构化保存 `exceptionCode`；码值→自然语言映射归 analysis 层 | 文本映射是展示/诊断职责，不是编解码职责 |
| 字节序 | 一切字段顺序显式处理，禁止依赖主机端序（禁 memcpy uint16） | 协议字节序是协议约定，不是平台属性 |

## 1. Codec 的含义

- **Encoder（编码）**：C++ 对象 → wire bytes。把内存里的语义对象（`ModbusRtuFrame`）变成线路上可发送的完整字节序列（含正确 CRC）。
- **Decoder（解码）**：wire bytes → C++ 对象。把收到的字节序列经长度检查与 CRC 验证后，还原成可信的语义对象。

**为什么 Serial Transport 不应该自己实现 Modbus 编码逻辑？**

1. 职责不同：transport 层的职责是"搬字节 + 帧切分"（t3.5 静默判定、缓冲管理），它不该知道"03 后面跟的是起始地址"。
2. 可测试性：编码逻辑埋在串口代码里，就必须有（虚拟）串口才能测协议；独立 Codec 是纯函数，离线毫秒级测试。
3. 复用性：Simulator 与 Replay **没有串口**，但都需要同一套编解码——协议逻辑必须在 transport 之上独立成层（三模式共享核心的架构承诺）。
4. 分层即解耦：transport 只交付"一段完整帧字节"，Codec 负责语义；任何一层可替换而不牵连另一层。

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

例（T002 的 Vector B，也是 Part A 的验收金样）：

```text
Frame:   address=0x01, functionCode=0x03, data={00,00,00,01}
CRC value: 0x0A84
Wire:    01 03 00 00 00 01 84 0A     （84=低字节在前，0A=高字节在后）
```

### Decode（线路 → 对象）

```text
wire bytes
    ↓ 长度基础检查          （最小帧长等；不含 t1.5/t3.5 时间判断——那属 T010）
    ↓ 读取收到的 CRC        （末尾两字节）
    ↓ 对前面全部字节重新计算 CRC
    ↓ 比较
    ↓ 一致 → 生成 ModbusRtuFrame（不含 CRC 字段）
```

**CRC 失败时不得把该数据当作正常可信 Frame**：不生成业务对象、不进入事务分析；raw bytes 后续可保留给诊断系统（错误统计/干扰分析）。

## 3. Part B — 0x03 Request 字段解释

`01 03 00 00 00 01 84 0A`（Part A 解码后 `data = {00,00,00,01}`）解释为：

```text
address:      01
function:     03          （Read Holding Registers）
startAddress: 00 00 = 0   （16-bit big-endian）
quantity:     00 01 = 1   （16-bit big-endian）
CRC:          84 0A       （Part A 已消费验证）
```

明确：`startAddress` 与 `quantity` 都是 **16-bit big-endian 字段**。

## 4. Part B — 0x03 Normal Response 字段解释

示例：`01 03 04 00 64 00 C8 …`（省略号为 Part A 负责的 CRC）

```text
01:      device address
03:      function code
04:      byteCount = 4
00 64:   register value 100
00 C8:   register value 200
```

为什么 data 是 4 字节：**2 个寄存器 × 每寄存器 2 字节 = 4 个数据字节**（`byteCount` 只数 data 区，不含地址/功能码/CRC）。

## 5. Exception Response

示例：`01 83 02 …`

```text
0x83 = 0x03 | 0x80        （Function 0x03 + 异常标志 0x80）
0x02 = Exception Code      （本例 02，结构化保存即可）
```

T004 只需要能够**结构化保存 exceptionCode**；异常码到自然语言说明的完整映射（02 = Illegal Data Address 等）后续由 analysis 层完成。

## 6. Big-endian Register Field（初学者版）

寄存器值在帧里**高字节在前（big-endian）**：

```text
00 64 → 0x0064 → 100
05 DC → 0x05DC → 1500
```

概念公式：

```cpp
value = (highByte << 8) | lowByte;
```

必须分清的两条**不同**规则：

| 规则 | 字段 | 顺序 |
| --- | --- | --- |
| 寄存器/16-bit 协议字段 | startAddress、quantity、register value | **高字节在前**（big-endian） |
| CRC 透传 | 帧尾两个校验字节 | **低字节在前**（V1.02 规定） |

**不得使用 CPU 本机端序来隐式决定协议字节顺序**：x86 是小端，`memcpy` 一个 `uint16_t` 出去恰好会把低字节放前面——对寄存器字段是错的，对 CRC 是"碰巧对"，且换平台就变。协议字节序必须显式逐字节处理。

## Knowledge I Must Be Able To Explain（必答清单）

**Q1. Codec 是什么？**
把内存语义对象与线传输字节相互转换的独立一层：Encoder（对象→字节）+ Decoder（字节→对象），使"协议含义"与"传输事实"解耦、可离线测试、可被三种模式复用。

**Q2. Encode 和 Decode 分别是什么？**
Encode：`ModbusRtuFrame` → 计算 CRC → 低字节在前追加 → 完整 wire bytes。Decode：wire bytes → 长度检查 → 读收到的 CRC → 重算比较 → 一致才生成 `ModbusRtuFrame`。

**Q3. 为什么 RTU Wire Codec 不应该理解 Holding Register？**
Wire codec 只管"通用容器"（地址+功能码+数据+CRC）；寄存器语义随功能码而变，是 Part B 的职责。混在一起=每个功能码都要复制一遍 CRC/长度逻辑，通用层被业务污染、无法复用。

**Q4. 为什么 Function 0x03 Codec 不应该重新实现 CRC？**
CRC 唯一事实来源是 T002 的 `calculateModbusCrc`（已被 KAT 锁定）。重复实现=两份可能分叉的真相；Part B 只解释 data 字段，CRC 归 Part A 调用。

**Q5. `00 64` 为什么等于 100？**
寄存器字段 16-bit 大端：`(0x00 << 8) | 0x64 = 0x0064 = 十进制 100`。

**Q6. 为什么寄存器字段高字节在前，而 CRC 却低字节在前？**
两条互不相干的协议约定：数据字段的大端表示是 Modbus PDU 的数值规范；CRC 低字节在前是 V1.02 对校验字段透传顺序的规定。各自显式实现，不互相推导。

**Q7. 为什么不能直接 memcpy 一个 uint16_t 到串口？**
`memcpy` 按主机端序排字节（x86 小端）：寄存器字段会被打反，CRC 恰好"蒙对"，且行为随平台漂移。必须显式 `(high<<8)|low` / 逐字节写出。

**Q8. CRC 校验失败后为什么不能继续产生正常业务数据？**
校验失败=内容不可信：若生成业务 Frame 进入事务分析，会污染配对与时延统计。正确路径是走错误通道——保留 raw bytes、计数、交诊断规则，不产出可信对象。

**Q9. Request 和 Response 的 data 字段为什么格式不同？**
同一功能码两个方向语义不同：request 表达"要什么"（startAddress+quantity），response 表达"给什么"（byteCount+寄存器字节），异常响应则是 exceptionCode 一个字节。由规范分别定义，Codec 必须分别建模。

## Implementation

**未发生。** Learning 阶段不写任何代码；`src/`、`tests/`、`CMakeLists.txt` 零改动。Part A 的 Test Design → RED → GREEN 流程待下一阶段执行。

## Files Changed（本阶段）

- 新增：`docs/tasks/T004-modbus-rtu-codec.md`（本文件）、`docs/devlog/2026-09-05-T004-Learning.md`
- 修改：`docs/BACKLOG.md`（T004 更名"Modbus RTU Codec"并拆 Part A/B、范围修订留痕、路线图同步）、`docs/PROJECT_STATUS.md`（四段式状态）
- 未改动：`src/`、`tests/`、`CMakeLists.txt`、presets

## Problems Encountered

无实现问题（本阶段 docs-only）。范围事项：BACKLOG 原 T004 行把 wire 编解码与 0x03 语义捆绑在同一行、隐含"一次做完"，与本次拆分决策冲突 → 已改写并留痕（fuzz/benchmark 确认不在 T004：fuzz 此前仅出现在旧 T003 行并已删除，benchmark 从未列入）。

## Solutions

T004 行改写为 Part A / Part B 两段职责 + "后续 Phase 分别测试与实现"；变更写入 BACKLOG 变更记录，历史记录不删改。

## Verification（本阶段，docs-only）

```text
git diff --check        → 通过（无空白/行尾问题）
git diff --name-only    → 仅 docs/ 下文件；src/、tests/、CMakeLists.txt 未出现
```

## Result

Learning / Scope Refinement 阶段完成：Codec 概念、Part A/B 数据流与边界、0x03 三种帧字段解释、双字节序规则、9 题必答全部落库。**Part A/B 均未实现**；T004 整体 IN PROGRESS。

## Knowledge Learned

- "Codec"不是一个类，是一个**职责层**：对象↔字节的转换边界，越窄越好测。
- 拆 Part A/B 的本质是按"知识维度"切任务：字节序+CRC（通用）与功能码语义（业务）分开学、分开测。
- 协议里可以同时存在两种字节序规则且互不相关——这正是"显式处理、禁 memcpy"的根因。
- 范围细化也应留痕：BACKLOG 行级改写 + 变更记录，让"为什么当时这么做"可追溯。

## Potential Interview Questions

见 §Knowledge I Must Be Able To Explain（9 题）。后续 Part A/B 的 Test Design 与 Implementation 阶段将补充：decode 失败的返回形态（`std::optional`/错误枚举取舍）、round-trip 测试设计、`byteCount` 一致性校验等。

## Git Commit

- 本阶段提交信息：`T004(Learning): RTU Codec 学习与范围细化 — Part A/B 拆分（docs-only）`
- 哈希：见 `git log`（docs-only 不推进 LKGC；LKGC 保持 `a44a6d2`）。