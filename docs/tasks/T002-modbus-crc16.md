# T002 — Modbus CRC16

> 状态：**IN PROGRESS**｜Phase A: ✅ DONE（Learning Checkpoint）｜Phase B: ✅ DONE（Test Design，docs-only）｜Phase C: ⬜ NOT STARTED（Test First + Implementation）
> 协议依据：MODBUS over Serial Line Protocol and Implementation Guide **V1.02**（主要，CRC Generation 章节）；Modbus Application Protocol **V1.1b3**（辅助，功能码/示例帧）。
> 任务边界（沿用 BACKLOG）：仅 CRC-16/MODBUS 按位实现与测试。**不含** Frame Codec（T003）、查表优化、benchmark、fuzz、Simulator（T005）、Serial（T010）。

## Goal

交付一个正确、可解释、可测试的 CRC-16/MODBUS **按位法计算**模块，并完成配套测试（KAT + 边界 + invariant）。分三阶段：Phase A 已把"面试要讲的话"落库；Phase B（本阶段）已把"测试矩阵、接口形态、TDD 顺序"定稿落库；Phase C 将在后续按计划实现。

## Background

- ModbusLens 是旁路监听诊断平台；CRC 校验是帧解析的第一道闸门，决定一帧进"可信数据"还是"错误统计"。
- CRC-16/MODBUS 参数琐碎（初值、反射多项式、线上字节序），先学习（Phase A）、再设计测试（Phase B）、最后实现（Phase C），保证每一步都留痕、经得起面试追问。
- 项目坚持"协议自己实现、外部实现只做对拍"（02_ARCHITECTURE §5），因此测试设计的可信度至关重要。

## Technical Decisions（已确定）

| 阶段 | 决策 | 内容 | 理由 |
| --- | --- | --- | --- |
| A | 协议依据 | RTU 层以 V1.02 为主；字节序约定直接引用 V1.02 规范 | 与 T003 帧模型、T005 帧切分共用同一权威来源 |
| A | 算法形态 | 反射按位算法：初值 0xFFFF、右移、判 LSB、条件 XOR 0xA001 | 与串口按位到达方向一致；最直白、适合面试讲解 |
| A | 查表/性能 | 不在 T002 范围 | 遵循 BACKLOG 边界；先正确性后性能 |
| A | 测试基础 | 两个 KAT 向量锁定（0x4B37 / 0x0A84），Phase A 已独立复核 | 权威输入输出，Phase C 直接引用 |
| A | 留痕位置 | 学习内容在 03_MODBUS_LEARNING §10；面试问答在本文件 | 知识底座与任务档案可独立引用 |
| B | 测试证据分级 | P0 KAT 为主要证据；P1 边界/invariant；P2 行为辅助 | KAT 能与"外部权威"对齐；自洽的错实现可能骗过 property test |
| B | 接口形态 | `modbuslens::core::calculateModbusCrc(std::span<const std::uint8_t>) -> std::uint16_t`（仅文档定案，Phase C 建文件） | 见 §Interface Design：核心不依赖 Qt UI、零拷贝、可测试 |
| B | T002 与 T003 的职责割线 | T002 只做 CRC **calculation**（返回数值）；wire byte serialization（低字节在前）归 T003 Frame Model | 数值与字节序正交；提前序列化=越界实现 Frame Codec |
| B | TDD 顺序 | RED→GREEN 13 步（见 §Phase C Execution Plan）；**不提交故意错误代码**，RED 证据只写在档案/devlog | 红绿证据真实，仓库永远可构建（纪律 13/优先可构建性） |

## Test Design（Phase B 定稿）

> 两个概念先分清：**CRC calculation**（输入字节序列 → 输出 uint16 数值，T002 的唯一职责）与 **CRC byte serialization**（数值 → 帧尾两个字节，低字节在前，T003 Frame Model 职责）。本任务矩阵里出现的 "wire bytes" 只作为**知识验收**，用于验证我们对 V1.02 字节序约定的理解，**不在 T002 实现**。

### 测试矩阵

| Test ID | Input | Expected Result | Why This Test Exists | What Bug It Can Catch |
| --- | --- | --- | --- | --- |
| CRC-T01 | ASCII `123456789`（字节 31–39） | CRC value `0x4B37`（wire `37 4B`） | CRC 世界的通用基准串；任何规范实现的必过锚点 | 初值、多项式、位序、迭代次数任一参数的错误——"自洽但错"的实现无法通过 |
| CRC-T02 | `01 03 00 00 00 01`（真实 Modbus 请求） | CRC value `0x0A84`（wire `84 0A`） | ① 真实帧 KAT：验证"参与计算的字节范围=地址+功能码+数据（不含 CRC 自身）"理解正确；② wire 字节验收对 V1.02 低字节在前的理解 | 字节范围错误（多算/漏算字段）、字节序理解错误 |
| CRC-T03 | 空字节序列 | `0xFFFF`（原样返回初值） | 纯 CRC 函数边界行为要有明确契约。**注意**：空数据**不是**合法 Modbus RTU 帧；帧合法性判定是 T003 的职责，本测试只保证 CRC 函数本身行为确定 | 隐藏的"至少读一个字节"假设、初始化/收尾缺陷 |
| CRC-T04 | `00`（单字节） | `0x40BF`（一次性独立参考实现复核，见下） | 最短真实输入：让"并入一个字节 + 8 轮迭代"的最小循环骨架只跑一次，骨架有错立刻暴露 | 循环次数 off-by-one、XOR 并入顺序等骨架缺陷 |
| CRC-T05 | `01 03 00 00 00 00` vs `01 03 00 00 00 01` | 两结果不同：`0xCA45` ≠ `0x0A84`（独立复核） | 行为辅助验证：数据一变结果必变（敏感性演示）。**不宣称碰撞保证**：CRC 不是密码学 hash | "实现没真正消费输入"类静态值 bug |
| CRC-T06 | 完整序列 `01 03 00 00 00 01 84 0A`（Vector B + 正确的 wire CRC） | `0x0000`（zero remainder） | 本参数/字节序下的有用 invariant（辅助证据） | 自洽性破坏（如分期计算 bug）。**不能替代 KAT**：错误但内部自洽的实现也可能通过 |

### 期望值来源与复核方式（可解释性要求）

- CRC-T01/T02/T06：规范/公开基准向量（T01 为 CRC 通用校验值；T02 为 Modbus 经典请求帧；T06 为 84 0A 追加后的余数性质）。
- CRC-T03/T04/T05：由**一次性独立参考实现**（Python 脚本，按 V1.02 公开参数独立编写，与未来 C++ 实现互不共享代码）计算并断言；脚本**不进仓库**（防止污染产品代码基线），方法与结果记录于此。
- 规范示例对拍（复核 V1.02/V1.1b3 的 CRC Generation 章节）：V1.02 示例 `02 07` → value `0x1241`、wire `41 12`（规范印刷为 41 12）；V1.1b3 RTU 示例 `11 03 00 6B 00 03` → value `0x8776`、wire `76 87`（规范印刷为 76 87）。两者与我们算法完全一致。官方 PDF 在线抓取返回 404（2026-09-05），故复核采用"公开参数 + 印刷示例数值对拍"。**教训实录**：核对过程中我一度把规范印刷的线上字节序（41 12 / 76 87）误记成数值（0x4112 / 0x7687），被断言当场抓出——这正是"CRC 数值 vs 线上字节序"区别的活教材（见 Problems）。

### 测试优先级

| 级别 | 定义 | 用例 |
| --- | --- | --- |
| **P0 — 必须通过** | 算法正确性的主要证据（与外部权威对齐） | CRC-T01、CRC-T02、CRC-T03 |
| **P1 — 应该通过** | 边界与 invariant 补充证据 | CRC-T04、CRC-T06 |
| **P2 — 行为辅助验证** | 行为演示性证据 | CRC-T05 |

判断原则：**Known Answer Tests 是主要证据，property/invariant tests 是补充证据**。KAT 的期望由"别人"给出，invariant 的期望由"我们自己"给出——证据力天然不同级。

## Interface Design（Phase B 前瞻定案，不实现）

> 目标：Modbus Core 尽量不依赖 Qt UI；核心逻辑可测试；不复制不必要数据；生命周期安全；初学者能理解；不过度设计。

| 方案 | 形态 | 优点 | 缺点 |
| --- | --- | --- | --- |
| A. Qt 容器 | `uint16_t calcModbusCrc(const QByteArray&)` | 在 Qt 生态内调用方便 | 核心绑定 QtCore；非 Qt 场景（CLI、测试工具、未来移植）被拖累；隐式共享多一层概念 |
| B. 标准 C++ | `uint16_t calculateModbusCrc(std::span<const std::uint8_t> data)` | 零拷贝、不拥有数据、任何字节容器通用、核心与 Qt 解耦 | 调用侧从 QByteArray 进来需一行适配（T003 落地） |

**推荐：方案 B**。理由：

1. **核心可测试**：测试直接喂 `std::array`/`std::vector`，无需构造任何 Qt 对象；
2. **零拷贝**：span 是"指针+长度"的只读视图，不复制数据（QByteArray 入参则可能触发隐式复制或要求 const 引用语义辨析）；
3. **生命周期安全**：span 不拥有数据，所有权仍在调用方，语义简单直白；
4. **初学者可懂**：接口= "给我一段字节区间，还你一个 16 位校验数"，一节课讲得清；
5. **不过度设计**：不引入模板元编程与 concept 约束，不搞"可接受任意 range"的泛型签名。

配套定案：

- 命名空间 `modbuslens::core`；函数名 `calculateModbusCrc`；参数 `std::span<const std::uint8_t>`；返回 `std::uint16_t`（**数值 CRC**，不含序列化）。
- 序列化（低字节在前 → 帧尾两字节）是 T003 Frame Model 的职责；QByteArray 到 span 的适配（`span(reinterpret_cast<const std::uint8_t*>(ba.constData()), size_t(ba.size()))`）亦随 T003 落地。
- 明确排除的"现代 C++ 炫技"：双迭代器版本、`std::ranges::input_range` 模板约束版、为字节序包装模板类型——全部不需要。
- **本阶段不创建任何 .h/.cpp**；文件创建发生在 Phase C 第 1 步。

## Phase C Execution Plan（TDD，13 步）

1. 创建 CRC public interface（`src/core/` 头文件 + 声明，按 §Interface Design 签名）；
2. 创建 CRC tests（`tests/unit/`，实现 §Test Design 的 T01–T06）；
3. 在 CRC 尚未正确实现（stub 返回固定值）时运行测试，**保留 RED 阶段真实失败证据**（记录进本档案 Verification 与 devlog）；
4. 实现最简单的按位 CRC-16/MODBUS（初值 0xFFFF、右移、判 LSB、条件 XOR 0xA001）；
5. 再运行测试；
6. 达到 GREEN（P0/P1/P2 全部通过）；
7. Build 全项目；
8. CTest 全部通过；
9. 如果遇到 Bug，建立 Problems Encountered / Issue 记录；
10. 更新 Knowledge Learned；
11. 更新 PROJECT_STATUS / BACKLOG；
12. 更新 LKGC（届时是含业务代码的构建+测试双通过提交）；
13. Git commit。

> 强调：**不为了人为制造 RED 而提交故意错误的业务代码**。RED 证据只存在于任务档案/devlog 的实际运行输出；仓库任何时刻保持可构建。

## Knowledge I Must Be Able To Explain（Phase A 必答清单）

**Q1. CRC 是干什么的？**
完整性校验码：发送方对"地址+功能码+数据"算出 2 字节校验附在帧尾，接收方重算比对，不一致即判定帧在传输中损坏并丢弃。它解决"意外损坏检测"，不解决"内容加密"或"来源证明"。

**Q2. CRC 和普通 checksum 有什么不同？**
普通 checksum（字节求和、LRC 等）是弱校验：比如两个字节交换位置，求和结果不变，照样漏检。CRC 本质是模 2 多项式除法取余，结果对字节位置敏感，可检测全部 1-bit 错误、奇数个位错误、长度 <16 bit 的突发错误以及绝大多数更长的突发错误；16-bit 输出随机化后随机漏检率约 1/65536。两者实现成本同量级，CRC 检出能力显著更强，因此被选为 RTU 级校验。

**Q3. CRC 能防止恶意篡改吗？**
不能。算法无密钥、完全公开，攻击者篡改数据后重算 CRC 即可通过验证。CRC 只防"意外损坏"；防篡改需要密钥/签名/MAC。因此 ModbusLens 绝不把 CRC 通过当作安全证据。

**Q4. 为什么 Modbus CRC 初始值是 0xFFFF？**
两层：① 它是 CRC-16/MODBUS（IBM 系）算法家族的规范参数，与多项式一样由协议定义（V1.02 规定）；② 功能上，若初值为 0，全 0 数据和前导 0 字节不会改变寄存器，接收方无法区分"没数据"与"全 0 数据"；0xFFFF 保证任何输入都会改变状态。

**Q5. 0xA001 是什么？**
反射后的 Modbus 生成多项式。规范多项式是 0x8005（x¹⁶+x¹⁵+x²+1）；本项目采用右移/判 LSB 的反射算法，每步"减去除数"需 XOR 它的位反转形式 0xA001。模 2 减法即 XOR。

**Q6. 为什么代码里 CRC 是 0x0A84，但报文里看到的是 84 0A？**
0x0A84 是寄存器算得的 uint16 数值；帧尾两个字节要按 Modbus 规定的"低字节在前"放置：低字节 0x84 先发、高字节 0x0A 后发，所以线上是 84 0A。数值与字节序是两个正交概念，与主机端序无关，实现时必须手工按字节写入。

**Q7. 如果 CRC 校验失败，我们的 ModbusLens 后续应该怎么处理？**
ModbusLens 是旁路观察者，不是协议参与者，处理口径：1) **不丢弃**——保留原始字节+时间戳+方向，标记校验失败；2) **计数**——进入错误统计（错误率、按从站/功能码分布），供诊断规则消费；3) **隔离**——坏帧绝不进入事务分析配对；4) **不纠错不重传**——观察者不做协议动作；5) **可见**——UI 层坏帧高亮（T008）。

## Testing Knowledge I Must Be Able To Explain（Phase B 必答清单）

**TQ1. 为什么不能只测试一个 CRC 输入？**
一个输入锚定不了算法身份的全部参数：初值、多项式、位序（反射）、迭代次数、字节并入方式，五个自由度里任意一个出错，都可能碰巧在单个输入上不显形（尤其短输入）。多输入组合——标准基准串（钉参数）、真实请求帧（钉字节范围）、边界长度（钉循环骨架）——才能把每个自由度逐一钉死。所以 P0 用三个 KAT，而不是一个。

**TQ2. Known Answer Test 是什么？**
输入与期望输出都由**外部权威**（协议规范、标准实现、公开基准）给定的测试：我们不"算"期望，而是"引用"答案。互操作的正确性 = 与别人的实现一致，KAT 直接把"别人"锚定进测试，因此它是算法正确性的主要证据。

**TQ3. 为什么空输入测试仍然有价值？**
它测的是纯 CRC 函数的**确定性边界行为**（空区间 → 原样返回初值 0xFFFF），防止实现里隐藏"至少读一个字节"的假设在将来被误用。要双重界定：空数据不是合法 Modbus 帧；"帧合不合法"是 T003 帧模型的职责。边界测试的价值 = 让每个函数在边角处也有明确契约。

**TQ4. 为什么 zero-remainder test 不能替代 Known Answer Test？**
它是内部自洽性检查：错误但自洽的实现（用错多项式但始终与自身一致、字节序整体镜像等）同样可能"append 后为零"。KAT 与外部权威对齐，invariant 只与自己对齐，证据力不同级——所以 KAT 是 P0 主力，zero-remainder 只能做 P1 辅助。

**TQ5. 为什么 CRC calculation 和 CRC byte serialization 应该分开测试？**
两者正交：一个是数值算法（bug 面=参数/流程），一个是字节序约定（bug 面=高低字节顺序/拼接）。分开测试=失败定位精确（数值错→算法层；数值对 wire 错→序列化层）。也对应任务边界：T002 只实现 calculation；wire 字节先作为知识验收写进 CRC-T02，序列化实现交给 T003。

**TQ6. 什么是 boundary test？**
在定义域边界（空输入、最短合法输入=单字节、上限长度）验证行为。off-by-one、初始化与收尾错误最常在边界暴露。例如单字节输入让"并入+8 轮迭代"骨架恰好只跑一次，骨架有错它第一个报警。

**TQ7. 我们为什么暂时不做 benchmark？**
v1 没有性能需求：目标场景 10 帧/秒、帧长 ≤256 B，按位法每帧约 2K 次位运算，在本机上远小于 1 ms，构不成瓶颈。没有真实性能需求与 profiling 证据就不优化；benchmark 还会引入 CI 时间抖动等测试噪声。性能优先级在 NFR-05 量化（T007 之后）确有需求时再立项，且先 profiling 再动手。

**TQ8. 我们为什么暂时不实现 lookup-table CRC？**
查表是"空间换时间"的优化，只在性能需求成立时才有价值；代价是可读性/可解释性下降、以及"程序生成表再与按位法对拍"的额外正确性风险。**T002 的原则：v1 优先正确性、可解释性与项目进度**——按位法每一行都能讲给面试官听、逐位可验证。性能优化必须有真实性能需求和 profiling 依据，届时走独立任务 + ADR（BACKLOG T013 备注已登记）。

## Before Coding Checklist（进入 Phase C 前必须逐项打勾）

- [ ] 能一句话说出 CRC 的用途：检测传输意外损坏，**不是安全机制**
- [ ] 初始值：0xFFFF（家族规范参数 + "全 0 盲区"解释）
- [ ] 基本按位流程能默写：init → 每字节 XOR 进寄存器 → 8 次 ×（看 LSB → 右移 → LSB 为 1 则 XOR 0xA001）
- [ ] polynomial：规范 0x8005 / 反射 0xA001，并能解释反射的含义
- [ ] CRC byte order：计算值（uint16 数值）与线上字节（低字节在前）区分清楚，`0x0A84` → wire `84 0A`
- [ ] 两个测试向量：`"123456789"`→`0x4B37`（37 4B）；`01 03 00 00 00 01`→`0x0A84`（84 0A）
- [ ] 坏帧的处理口径（Q7）能在不查文档的情况下复述
- [ ] 测试矩阵与优先级能默述：P0=T01/T02/T03（KAT）；P1=T04/T06；P2=T05；KAT 是主要证据、invariant 是补充证据
- [ ] 接口签名能默写：`modbuslens::core::calculateModbusCrc(std::span<const std::uint8_t>) -> std::uint16_t`（数值，不含序列化）

## Implementation

**未发生。** Phase A/B 不编写任何 CRC 业务代码；`src/`、`tests/`、`CMakeLists.txt` 零改动。Phase C 按 §Phase C Execution Plan 执行。

## Files Changed

Phase A：
- 新增 `docs/tasks/T002-modbus-crc16.md`、`docs/devlog/2026-09-05-T002-PhaseA.md`
- 修改 `docs/03_MODBUS_LEARNING.md`（§10 深度章节）、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`

Phase B（本阶段）：
- 修改 `docs/tasks/T002-modbus-crc16.md`（本文件：Test Design、Interface Design、Phase C Execution Plan、Testing Knowledge、优先级等）
- 修改 `docs/PROJECT_STATUS.md`（四段式任务状态）、`docs/BACKLOG.md`（T002 阶段状态）
- 修改 `docs/04_TEST_STRATEGY.md`（KAT/优先级原则引用 T002 矩阵）
- 新增 `docs/devlog/2026-09-05-T002-PhaseB.md`

始终未改动：`src/`、`tests/`、`CMakeLists.txt`、`CMakePresets.json`、`CMakeUserPresets.json`。

## Problems Encountered

Phase A：无代码问题；向量先复核再落笔。

Phase B：
1. **数值/线上字节序记忆颠倒（真问题，很有教学价值）**：复核规范示例时，我最初把 V1.02/V1.1b3 印刷的线上字节序（41 12 / 76 87）误记成了"数值"（0x4112 / 0x7687），独立脚本断言当场失败。实际数值应是 0x1241 / 0x8776（其 wire 形式才是 41 12 / 76 87）。
2. **官方 PDF 直接抓取失败**：modbus.org 的 V1.02 PDF 两个候选 URL 均返回 404（文档在官网多次改版搬迁）。

## Solutions

1. 对拍逻辑修正为"以 wire 字节反推数值"：`02 07 → wire 41 12 → value 0x1241`；`11 03 00 6B 00 03 → wire 76 87 → value 0x8776`。更正后全部断言通过，并将本次"记反"事件写入档案——它就是 §Knowledge Q6（数值 vs 线序）的最佳实证。
2. 采用"公开参数（init 0xFFFF / poly 0x8005→0xA001 反射 / 低字节在前）+ 规范印刷示例数值对拍"完成 CRC Generation 章节的复核；抓取失败的实情如实记录，不假装读过原文。

## Verification

Phase A（此前已提交）：
```text
Vector A: value=0x4B37 wire=37 4B ✓ / Vector B: value=0x0A84 wire=84 0A ✓（一次性独立脚本）
```

Phase B（本阶段，一次性独立 Python 参考实现——不进仓库）：
```text
CRC-T01 "123456789"                       -> 0x4B37        ✓（wire 37 4B）
CRC-T02 01 03 00 00 00 01                 -> 0x0A84        ✓（wire 84 0A）
CRC-T03 (empty)                           -> 0xFFFF        ✓
CRC-T04 00                                -> 0x40BF        ✓
CRC-T05 01 03 00 00 00 00  vs  01 03 00 00 00 01
                                          -> 0xCA45 vs 0x0A84，二者不同 ✓
CRC-T06 01 03 00 00 00 01 84 0A           -> 0x0000        ✓
V1.02  sample 02 07                       -> value 0x1241, wire 41 12 ✓
V1.1b3 sample 11 03 00 6B 00 03           -> value 0x8776, wire 76 87 ✓
（全部断言通过；脚本为独立实现，仅用于生成期望值与复核，不进入仓库）
```

提交前检查：
```text
git diff --check          → 通过（无空白/行尾错误）
git diff --name-only      → 仅文档文件；src/、tests/、CMakeLists.txt 未出现
```

## Result

- Phase A：✅ DONE（学习与知识留痕）。
- Phase B：✅ DONE（测试矩阵 6 用例 + 优先级分级 + 接口定案 + Phase C 13 步 TDD 计划 + 8 题测试问答全部落库）。
- Phase C：⬜ NOT STARTED。
- **T002 整体仍为 IN PROGRESS，未完成**；未写任何 CRC 实现代码。

## Knowledge Learned

- "KAT 是主要证据、invariant 是补充证据"——期望值来源决定测试的证据等级。
- CRC 数值与线上字节序必须当作两个独立概念行使：本阶段我本人就犯了一次"记反"，靠独立对拍断言当场纠偏；这类错误正是矩阵中 CRC-T02 为什么要同时断言 value 与 wire 的原因。
- interface 定案先于实现：签名级决策（span 而非 QByteArray）一旦写进档案，Phase C 就没有"随手选型"的自由度，也不会牵着 T003 的接口走偏。
- RED 证据可以不靠"提交坏代码"获得：把"尚未实现时的真实失败输出"记录在档案/devlog 即可，仓库永远可构建。

## Potential Interview Questions

- Phase A 7 题与 Phase B 8 题（见两个 Knowledge 清单），对应 INTERVIEW_NOTES 的 T002 预告题类。
- 新增可答点：为什么测试矩阵要分三级优先级；TDD 的 RED 阶段如何在不污染仓库的前提下留证；接口为何选 `std::span` 而不选 QByteArray；"先把 wire 字节写进知识验收、序列化留到 T003"的职责划分逻辑。

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| Phase A | `a64ee7e` | `T002(Phase A): CRC16 学习 checkpoint — 知识留痕（docs-only）` |
| Phase B | 见 `git log` | `T002(Phase B): CRC16 测试设计 — 测试矩阵/接口定案/Phase C 计划（docs-only）` |

> LKGC 不推进：本阶段为 docs-only，无新的业务代码构建验证（沿用项目约定，LKGC 保持 `aa337f6`）。Phase C 完成的提交届时才推进 LKGC。