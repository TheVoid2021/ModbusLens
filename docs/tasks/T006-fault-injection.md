# T006 — Deterministic Fault Injection

> 状态：**IN PROGRESS — Learning / Test Design**（2026-09-06，docs-only）｜Implementation ⬜
> 前置确认：T005 DONE（`SimulatedSlave` 就绪）、M3 进行中、LKGC = `3a896df`。
> 协议依据：V1.1b3 / V1.02（故障不影响协议语义本身——故障发生在传输模拟层）。
> 本阶段边界：只学习与设计。**禁止**（全部未做）：修改 `src/`、`tests/`、`CMakeLists.txt`；实现 Fault Injector；T007 及以后的一切。

## Goal

在"始终正确的 SimulatedSlave"之上叠加一层**确定性故障注入**：把正确的 response wire 变成"正常送达 / 丢弃 / CRC 损坏 / 标记延迟"四种可显式选择的结果，让诊断链路（错误统计、Timeout 判定、Demo A 的异常场景）有可控的输入。

## Background

- Demo A（DEMO_GUIDE）需要"注入异常"脚本：从站超时、CRC 错误——当前 Simulator 只会永远正确地回答。
- 诊断平台的差异化能力在"错误样本"：没有故障注入，错误统计（T007）与诊断规则（T008）只能靠手造帧测试。
- T005 范围收缩时已承诺：Timeout / CRC fault / delay 归 T006——本任务兑现。

## Scope（已收缩，理由见 §Technical Decisions）

**T006 只支持四种模式**：`None`（正常透传）、`DropResponse`（丢弃响应）、`CorruptCrc`（损坏 CRC）、`ArtificialDelay`（标记延迟，不真等）。

**明确删除 / 推迟**（本任务不做）：

| 删除项 | 理由 |
| --- | --- |
| random fault / probability / random seed | 随机性破坏确定性与测试稳定性；Demo 需要可复现 |
| real sleep / QTimer / QThread / timeout timer | T006 是纯函数层，无真实时间；Timer 属未来 Session/Runtime |
| packet loss probability / burst error / noise simulation | 统计模型属于更后面的传输仿真需求，v1 无消费者 |

第一版故障必须由**测试或用户显式选择**（config 传入），绝不随机触发。理由：行为确定 → 测试稳定 → Demo 可复现 → 两周项目不过度设计。

## Technical Decisions

| 决策 | 内容 | 理由 |
| --- | --- | --- |
| 载体 | `src/core/simulator/SimulationFault.{h,cpp}`，自由函数 `applySimulationFault`，进 `modbuslens_core` | 纯函数、无状态、零 Qt；与 SimulatedSlave 同目录（同属"模拟传输"职责），**不是** IFrameSource（接口推迟决策不变） |
| 作用层 | 故障只作用于 **encoded wire bytes**（输入约定来自 `encodeRtuFrame` 的完整合法 wire） | CRC 属于线路层；语义 Frame 不该知道 CRC（T003 决策） |
| SimulatedSlave 不动 | T006 原则上不为故障注入修改 SimulatedSlave | Slave=始终正确的协议端点；FaultInjector=故障层。若实现阶段确需改动，必须先说明原因 |
| 结果模型 | `variant<DeliveredWire, DroppedResponse>` | 二选一结果 + delay 元数据；比 bool/出参清晰，比 Result framework 简单 |
| CorruptCrc 策略 | **固定**：最后一个 CRC 字节 XOR 0x01（payload 不动） | 确定性：同一输入永远同一输出；无随机位选择 |
| Timeout | **不由 T006 判定**；T006 只交付 DropResponse/NoResponse | Timeout = Master/Session 按"等待时长"做出的判断，需要一个时钟与等待方——归 T007 |

## Timeout 与 DropResponse 的区别（关键概念）

Simulator / Fault Injector **不能直接"产生 Timeout 报文"**——Timeout 不是一个报文，而是一个判断：

```text
Master 发出请求
    ↓ （Fault 层：本该送达的 Response 被丢弃 → No Response）
Master / Session 层等待
    ↓ 超过规定时间仍无响应
Session/Transaction 层判定：Timeout
```

因此 T006 模拟的是 **DropResponse / NoResponse**（交付层事实）；未来 Session / Transaction 层（T007）根据"等待时间 + 无响应"把它**判定**为 Timeout。**不得把 Timeout 当成 Modbus Exception Response**——异常响应是设备真实回复的合法帧，与"根本没有响应"是完全不同的两件事（见 §三者的区别）。

## CRC Fault 所在层（关键约束）

```text
SimulatedSlave                    ← 始终生成正确 semantic Response Frame（T006 不改它）
    ↓
Response Frame
    ↓ encodeRtuFrame()            ← 正确 Wire Bytes（CRC 已计算，T002/T004A）
    ↓
Fault Injector（本任务）          ← 可选择故意破坏 CRC
    ↓
SimulatedDelivery（DeliveredWire / DroppedResponse）
```

- CorruptCrc **必须作用于 encoded wire bytes**；
- **禁止**为了制造 CRC Error 修改 SimulatedSlave、或让 Frame 保存（错误）CRC——CRC 是派生值且属线路层（T003 决策）；
- 这样产生的 CRC Error 语义 = "正确设备响应在传输中被损坏"——与真实现场一致，正是诊断系统要捕获的样本。

## ArtificialDelay 的语义

- **不是** sleep / thread / timer——T006 **不真的等待**；
- ArtificialDelay 只是**元数据**："如果真实模拟执行，这个 Response 应该延迟多久交付"（如 500ms）；
- 未来 SessionController / Simulation Runtime 消费该 metadata 决定交付时机；
- 因此测试无需真实等待，`ArtificialDelay` 模式的执行耗时与其他模式相同。

## 数据模型（定案，不实现）

### 方案比较

| 方案 | 形态 | 优点 | 缺点 | 结论 |
| --- | --- | --- | --- | --- |
| A. 自由函数 + variant | `applySimulationFault(wire, config) -> SimulatedDelivery` | 纯函数、无状态、测试最简、Slave 零改动 | — | ✅ **定案** |
| B. 装饰器 Slave | `FaultInjectingSlave` 包一层 handleRequest(config) | 调用方少一步 | 把故障配置耦合进端点调用路径；CorruptCrc 需要 wire，装饰器内还得 encode——混层 | ❌ |
| C. 故障配置放进 SimulatedSlave 构造 | Slave 自带故障模式 | — | 污染协议端点；违反"Slave 始终正确"原则 | ❌ |

### 定案签名（namespace `modbuslens::core`，Implementation 时落文件）

```cpp
enum class SimulationFaultMode {
    None,            // 正常透传
    DropResponse,    // 丢弃响应（未来 Session 判定为 Timeout）
    CorruptCrc,      // 破坏 wire CRC（固定策略）
    ArtificialDelay, // 透传 + 标记延迟元数据
};

struct SimulationFaultConfig {
    SimulationFaultMode mode{};
    std::chrono::milliseconds artificialDelay{0};
};

struct DeliveredWire {
    std::vector<std::uint8_t> bytes;
    std::chrono::milliseconds artificialDelay{0};
    bool operator==(const DeliveredWire&) const = default;
};

struct DroppedResponse {
    bool operator==(const DroppedResponse&) const = default;
};

using SimulatedDelivery = std::variant<DeliveredWire, DroppedResponse>;

SimulatedDelivery applySimulationFault(
    std::span<const std::uint8_t> validWire,
    const SimulationFaultConfig& config);
```

满足约束：Pure C++20 ✓；零 Qt ✓；无真实时间等待 ✓；Delivered/Dropped 可区分 ✓；携带 delay ✓；测试容易 ✓；无 framework ✓。边界记录：`validWire` 的契约是"来自 `encodeRtuFrame` 的完整合法 wire"；对更短输入的行为属契约违规，Implementation 阶段以注释声明前置条件（不新增错误通道，避免过度设计）。

## 各模式行为定义

### Normal（None）

输入 `validWire`，Expected：`DeliveredWire`，bytes 与输入**逐字节相同**，`artificialDelay = 0ms`。**Fault Injector 不重新计算 CRC**——它不是协议参与者，只搬运或破坏。

### DropResponse

Expected：`DroppedResponse{}`，**不返回任何 wire bytes**。命名用 DropResponse 而非 Timeout——Timeout 是未来 Master/Session 的判断（见上）。

### CorruptCrc

输入必须来自 `encodeRtuFrame()`（完整合法 RTU wire）。固定策略：**复制 wire bytes → 最后一个 CRC 字节 XOR 0x01**：

```text
正确：01 03 04 00 64 00 C8 BA 7A
故障：01 03 04 00 64 00 C8 BA 7B
```

payload（前 7 字节）完全不变；随后 `decodeRtuFrame()` 必须得到 `CrcMismatch`。不随机选择修改位置——确定性是本层的存在理由。

### ArtificialDelay

Expected：`DeliveredWire`，bytes 与输入一致，`artificialDelay = 500ms`（按 config）。**本阶段不得真实等待 500ms**。

## Test Design

| Test ID | Input | Expected | Why This Test Exists | What Bug It Can Catch | Priority |
| --- | --- | --- | --- | --- | --- |
| FAULT-T01 | `01 03 04 00 64 00 C8 BA 7A`，mode=None | DeliveredWire，bytes 逐字节相同，delay=0ms | 正常路径是其他模式的基线；透传不得改字节 | injector 意外改写 payload/CRC、误算 delay | **P0** |
| FAULT-T02 | 同一合法 wire，mode=DropResponse | `DroppedResponse{}`，无 bytes | 交付层"无响应"事实的唯一样式 | 错误地返回空 wire 而非 Dropped、半实现 drop | **P0** |
| FAULT-T03 | 同一合法 wire，mode=CorruptCrc | payload 7 字节不变；末字节 `7A→7B`；`decodeRtuFrame()` → `CrcMismatch` | 固定破坏策略 + 与 T004A 的解码联动 | 破坏位置随机/破坏 payload/忘 XOR | **P0** |
| FAULT-T04 | mode=ArtificialDelay, delay=500ms | DeliveredWire，bytes 不变，delay=500ms；**测试不真等 500ms** | delay 是元数据不是行为 | 实现 sleep、阻塞测试 | **P0** |
| FAULT-T05 | 同 input+config（CorruptCrc）连续调用两次 | 两次结果完全一致 | 证明注入层无随机性（确定性护栏） | 隐藏的随机源/全局状态 | P1 |

优先级：P0 = T01/T02/T03/T04；P1 = T05（确定性护栏）。

## Integration Test

| Test ID | 流程 | Expected | Purpose | Priority |
| --- | --- | --- | --- | --- |
| **FAULT-I01** | Request Frame → `SimulatedSlave` → 正确 Response Frame → `encodeRtuFrame` → 正确 wire → `applySimulationFault(CorruptCrc)` → `decodeRtuFrame` | `CrcMismatch` | 证明 CRC Error = "**正确 Slave 响应在传输模拟层被破坏**"，而不是"Simulator 生成错误业务数据"——错误样本的来源被端到端锚定 | **P0** |
| FAULT-I02 | SimulatedSlave → Response Frame → encode → `applySimulationFault(DropResponse)` | `DroppedResponse`（无 wire 交付） | 验证"无响应"链路；**不在 T006 判 Timeout**——未来 Session 层做"无响应 + 等待阈值 → Timeout" | P1 |

## Exception Response / CRC Error / DropResponse 的区别（不得混淆）

| 类别 | 本质 | wire 表现 | CRC | 谁产生 |
| --- | --- | --- | --- | --- |
| **Exception Response** | 设备**真的回复了**合法 Modbus 帧（如 `0x83 / 0x02`） | 完整、**CRC 正确** | 正确 | SimulatedSlave（协议行为） |
| **CRC Error** | 设备的**正确响应在线路模拟阶段被损坏** | 完整但 CRC 被破坏 | 错误 | Fault Injector（CorruptCrc） |
| **DropResponse** | **没有响应被交付**（未来被判 Timeout） | 无 | — | Fault Injector（DropResponse） |

三者是三个不同层级的"坏事"：设备说不行 / 线路把话弄坏了 / 话根本没送到。T007 统计与 T008 诊断规则将分别消费它们。

## SimulatedSlave 不动原则

原则上 T006 不为故障注入修改 SimulatedSlave 的任何协议行为。若 Implementation 阶段发现确有必要，必须先在任务档案说明原因并记录决策。保持：**SimulatedSlave = 始终正确的协议端点；FaultInjector = 故障模拟层**。

## Knowledge I Must Be Able To Explain（12 题）

**F-Q1. Fault Injector 为什么和 SimulatedSlave 分开？** 单一职责：Slave 是"始终正确的协议端点"，Injector 是"传输故障层"。分开后各自可独立测试，故障层可叠加在任何正确端点上（未来真实设备采集同理）。
**F-Q2. 为什么 Timeout 不能直接由 Slave 产生？** Timeout 不是报文而是判断，需要"等待方 + 时钟"；Slave 是同步纯应答端点，无时间概念。
**F-Q3. DropResponse 和 Timeout 有什么区别？** DropResponse 是交付层事实（本该送达的响应被丢弃）；Timeout 是等待方对该事实加上时间阈值后的判断。前者 T006 产生，后者 T007 判定。
**F-Q4. CRC Fault 为什么必须作用在 wire bytes？** CRC 只存在于线路帧（T003 决策：Frame 不存 CRC）；现场 CRC 错误就是线路字节被损坏，模拟层必须复刻同一层级。
**F-Q5. 为什么不能修改 Frame 里的 CRC？** Frame 模型没有 CRC 字段（防 stale CRC）；让 Frame 携带错误 CRC 会破坏"派生值不入模型"的设计，并让故障语义泄漏进协议层。
**F-Q6. 为什么 ArtificialDelay 不真的 sleep？** sleep 让测试变慢且引入真实时间依赖（不稳定）；延迟在 v1 只是交付元数据，消费者（Session/Runtime）未来再决定如何兑现。
**F-Q7. 为什么第一版不做随机故障？** 随机破坏确定性：测试会抖、Demo 不可复现、失败难定位；显式选择的故障同样能覆盖诊断场景，成本却低一个量级。
**F-Q8. Deterministic simulation 有什么好处？** 同输入必同输出：测试可精确断言、Demo 可重复演示、失败可回放、CI 无抖动——对诊断工具尤其重要（错误样本可复现才谈得上验证诊断规则）。
**F-Q9. Exception / CRC Error / DropResponse 有什么区别？** 设备真回复了合法帧 / 正确响应在线路被损坏 / 根本没有交付——三个层级、三种统计口径（见 §表格）。
**F-Q10. 未来哪一层负责真正判定 Timeout？** Session / Transaction 层（T007）：它握有请求发出时间与响应到达情况，"无响应 + 超过阈值"才构成 Timeout。
**F-Q11. 为什么 CorruptCrc 固定翻转同一个 bit？** 确定性要求"同一输入永远同一故障"；固定策略让断言可以逐字节写死（7B），且实现/测试零随机依赖。
**F-Q12. T006 如何帮助无硬件 Demo？** Demo A 的"注入异常"脚本 = 点一下按钮切换 fault mode：超时（Drop）、CRC 错误（CorruptCrc）当场复现，且每次演示结果一致——这是没有真硬件也能展示诊断能力的关键。

## Implementation Plan（下一阶段）

1. 创建 `src/core/simulator/SimulationFault.h`（模型 + 签名）；
2. 创建 `SimulationFault.cpp`（四模式实现；CorruptCrc 末字节 XOR 0x01）；
3. 写 FAULT-T01~T05 测试（`tests/test_simulation_fault.cpp`）；
4. 写 FAULT-I01/I02 测试（`tests/test_simulation_fault_integration.cpp`，复用 SimulatedSlave + codec 链路）；
5. CMake：两个 target + ctest `fault` / `fault_integration`；
6. RED（linker error，如实记录）；
7. 实现至 GREEN；
8. clean build + full ctest（9/9 预期：既有 7 + 2，按实际记录）；
9. SimulatedSlave 零改动复核（git diff 验证）；
10. 文档归档；code commit；推进 LKGC；docs backfill（如需要）。

## Implementation

**未发生。** 本阶段 docs-only；`src/`、`tests/`、`CMakeLists.txt` 零改动。

## Files Changed（本阶段）

- 新增：`docs/tasks/T006-fault-injection.md`（本文件）、`docs/devlog/2026-09-06-T006-TestDesign.md`
- 修改：`docs/PROJECT_STATUS.md`（四段式状态）、`docs/BACKLOG.md`（T006 范围收缩 + 变更记录）
- 未改动：`src/`、`tests/`、`CMakeLists.txt`、presets、`SimulatedSlave`

## Problems Encountered

无实现问题（docs-only）。范围事项：BACKLOG 原 T006 行为"超时/CRC 错帧/异常码注入开关"的泛化描述，与本次"确定性四模式 + 显式删除随机性"的决策冲突 → 已改写并在变更记录留痕。

## Solutions

T006 行改写为四模式范围；random/seed/real delay/丢包概率移出并在变更记录写明理由；异常码注入（0x01/0x02/0x03）已由 T005 的 SimulatedSlave 交付，不在 T006 重复。

## Verification（本阶段，docs-only）

```text
git diff --check        → 通过（无空白/行尾问题）
git diff --name-only    → 仅 docs/ 下文件；src/、tests/、CMakeLists.txt 未出现
ISSUE-001 索引检查      → INTERVIEW_NOTES 已有入口（T005 时加入），无需重复
```

## Result

Learning / Test Design 完成：四模式范围定案（random/seed/real delay/丢包概率显式移出）、Timeout vs DropResponse 与 CRC fault 层级约束落库、数据模型定案、5+2 用例矩阵（P0×6 + P1×1）、12 题问答、实施计划齐备。**Fault Injector 未实现**；T006 整体 IN PROGRESS。

## Knowledge Learned

- **故障是"传输层事实"，不是"协议层谎言"**：Exception Response 是设备真实说的"不行"；CRC Error 是线路把话弄坏；DropResponse 是话没送到。三层分开，统计与诊断才有正确语义。
- **Timeout 是判断不是报文**——它需要等待方与时钟，所以注定属于 Session 层；让设备层"产生 Timeout"是范畴错误。
- **确定性优先于拟真**：v1 用固定 XOR 策略替代随机破坏，牺牲"像真实线路"换取可测/可复演/可讲解——这是两周项目的正确取舍。
- **元数据替代行为**：ArtificialDelay 用一个 `chrono::milliseconds` 字段表达"应该延迟多久"，把"真的等"留给未来消费者——行为与信息的分离让测试零等待。

## Potential Interview Questions

- 12 题见上。
- Implementation 阶段将补充：`chrono::milliseconds` 在 ABI/序列化上的注意点、SimulatedDelivery 的 get_if 模式（ISSUE-001 教训的应用验证）。

## Git Commit

- 本阶段提交信息：`T006(Learning): 确定性故障注入学习与测试设计（docs-only）`
- 哈希：见 `git log`（docs-only 不推进 LKGC；LKGC 保持 `3a896df`）。