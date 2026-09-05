# T002 — Modbus CRC16

> 状态：**IN PROGRESS — Phase A: Learning Checkpoint**（2026-09-05）
> 阶段规划：Phase A 学习与知识留痕（本阶段，docs-only）→ Phase B 测试先行（设计用例，TDD）→ Phase C 实现与验证。**Phase B/C 尚未开始。**
> 协议依据：MODBUS over Serial Line Protocol and Implementation Guide **V1.02**（主要）；Modbus Application Protocol V1.1b3（辅助，功能码语义）。
> 任务边界（沿用 BACKLOG）：仅 CRC-16/MODBUS 按位实现与测试。**不含** Frame Codec（T003）、查表优化、benchmark、fuzz、Simulator（T005）、Serial（T010）。

## Goal

让开发者（我）在真正动手写 CRC 代码之前，能向面试官讲清楚 CRC-16/MODBUS 的基本原理与实现细节，并把这份理解**永久留痕**到仓库文档中，作为 Phase B 测试设计与 Phase C 实现的验收依据。本阶段不产出任何 C++ 代码。

## Background

- ModbusLens 的核心是"旁路监听 + 帧解析 + 事务分析"。CRC 校验是帧解析的第一道闸门：∑决定一帧是"可信数据"还是"错误样本"，并喂给后续错误统计与诊断规则。
- CRC-16/MODBUS 细节多（初始值、多项式反射、线上字节序），只背代码不理解原理，面试一追问就会露馅；因此先做学习 checkpoint，把这部分知识显式化。
- 本项目已确立"协议自己实现、只用外部库对拍"的原则（见 02_ARCHITECTURE §5），学习必须扎实。

## Technical Decisions（Phase A 已确定）

| 决策 | 内容 | 理由 |
| --- | --- | --- |
| 协议依据 | RTU 层以 **V1.02** 为主要依据；`least significant byte first` 等措辞直接引用该规范 | 与后续 T003 帧模型、T005 切分共用同一权威来源 |
| 算法形态 | 反射按位算法：初值 0xFFFF、右移、判 LSB、条件 XOR 0xA001 | 与串口按位到达的数据流方向一致；最直白、最适合面试讲解（02 也是这么规划的）|
| 查表/性能 | **不在 T002 范围**；按位法优先保证正确性 | 遵循 BACKLOG T002 边界；性能优化留待后续独立小任务 |
| 测试基础 | 两个 KAT 向量锁定：Vector A `"123456789"`→`0x4B37`（wire `37 4B`）；Vector B `01 03 00 00 00 01`→`0x0A84`（wire `84 0A`）；Phase A 已用一次性独立脚本复核（脚本不进仓库） | 权威输入、权威输出，Phase B 直接引用 |
| 留痕位置 | 学习内容沉淀于 03_MODBUS_LEARNING §10；面试问答沉淀于本文件 | 知识底座与任务档案分离，各自可被独立引用 |

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
ModbusLens 是旁路观察者，不是协议参与者，处理口径：
1. **不丢弃**：保留原始字节 + 时间戳 + 方向（RTU 无方向字段，用端口/采集源标注），记录校验失败标记；
2. **计数**：进入错误统计（错误帧数、错误率、按从站/功能码分布），供诊断规则消费（如"某从站 CRC 错误突增 → 疑似干扰/波特率偏差"）；
3. **隔离**：坏帧绝不进入事务分析（不能被当作有效请求或响应进行配对）；
4. **不纠错不重传**：观察者不代替主站/从站做协议动作；
5. **可见**：UI 层坏帧高亮（T008）。

## Before Coding Checklist（进入 Phase B 前必须逐项打勾）

- [ ] 能一句话说出 CRC 的用途：检测传输意外损坏，**不是安全机制**
- [ ] 初始值：0xFFFF（家族规范参数 + "全 0 盲区"解释）
- [ ] 基本按位流程能默写：init → 每字节 XOR 进寄存器 → 8 次 ×（看 LSB → 右移 → LSB 为 1 则 XOR 0xA001）
- [ ] polynomial：规范 0x8005 / 反射 0xA001，并能解释反射的含义
- [ ] CRC byte order：计算值（uint16 数值）与线上字节（低字节在前）区分清楚，`0x0A84` → wire `84 0A`
- [ ] 两个测试向量：`"123456789"`→`0x4B37`（37 4B）；`01 03 00 00 00 01`→`0x0A84`（84 0A）
- [ ] 坏帧的处理口径（Q7）能在不查文档的情况下复述

## Implementation

**未发生。** Phase A 不编写任何 CRC 业务代码；`src/`、`tests/`、`CMakeLists.txt` 零改动。实现步骤将在 Phase B/C 填写。

## Files Changed（Phase A）

- 新增：`docs/tasks/T002-modbus-crc16.md`（本文件）、`docs/devlog/2026-09-05-T002-PhaseA.md`
- 修改：`docs/03_MODBUS_LEARNING.md`（§5 交叉引用 + 新增 §10 深度章节 + 参考资源顺延为 §11）
- 修改：`docs/PROJECT_STATUS.md`（T002 置为 IN PROGRESS / Phase A）、`docs/BACKLOG.md`（T002 行状态同步）
- 未改动：`src/`、`tests/`、`CMakeLists.txt`、`CMakePresets.json`、`CMakeUserPresets.json`

## Problems Encountered

**暂无（Phase A 未写代码）。** 唯一要防的"问题"是学习内容失真：两个测试向量先经一次性独立脚本复核（Python，不进仓库）确认 0x4B37 / 0x0A84 及线上字节序无误，再写入文档。

## Solutions

同上——用"文档落笔前先用一次性脚本核对事实"消除失真风险。脚本不进仓库，避免污染项目代码基线。

## Verification（Phase A）

```text
1. 向量独立复核（one-off 脚本，非仓库代码）：
   Vector A: value=0x4B37  wire=37 4B  ✓
   Vector B: value=0x0A84  wire=84 0A  ✓
2. git diff --check → 无空白/行尾错误
3. 变更文件清单中无 src/、tests/、CMakeLists.txt（本阶段红线）
```

## Result

Phase A 达成：10 个原理点 + 伪代码逐步解读 + 两个权威向量 + 7 个必答面试题 + Before Coding Checklist 全部落库。**T002 整体仍未完成**（状态：IN PROGRESS — Phase A）；Phase B/C 待后续指令启动。

## Knowledge Learned

- "CRC 数值"与"线上字节序"必须分离记忆——这是查表工具与抓包工具显示不一致的根源。
- 反射算法三件套（右移 / 判 LSB / XOR 0xA001）互为因果，理解了镜像关系就不用死记。
- 学习 checkpoint 的做法：先把"面试要讲的话"写成文档，再据此设计测试——测试的断言点就来自这些句子。
- V1.02 规范把"低字节在前"写死在协议层，因此实现与主机端序解耦。

## Potential Interview Questions

- Phase A 已覆盖 7 题（见上"Knowledge I Must Be Able To Explain"），与 INTERVIEW_NOTES 中 T002 预告题类对应。
- Phase B 之后将补充：按位法复杂度推导、TDD 顺序怎么排、为什么先写 Vector A 再写 Vector B。
- Phase C 之后将补充：内存对齐/大小端陷阱、如何用 pymodbus 二次对拍。

## Git Commit

- 本阶段提交信息：`T002(Phase A): CRC16 学习 checkpoint — 知识留痕（docs-only）`
- 哈希：见 `git log`（沿用项目约定：docs-only 提交不推进 PROJECT_STATUS 的 LKGC，LKGC 保持 `aa337f6`）。