# 10 — KNOWLEDGE OWNERSHIP MAP（知识所有权地图）

> **用途**：这不是题库，而是**"我自己必须真正掌握什么"的第一版地图**——为 Human owner 准备，
> 不是为了把文档写好看。每个主题都要求能**脱稿讲**、必要时能**现场画数据流**。
> **事实来源**：只使用已验收 Fact Pack（`docs/interview/00`–`06`）；**不新增** `[V-]` 标记。
> **本文不含**：大型问答题库、模拟面试、背诵卡、压力题——那些属 **Phase C**（须经 Human 对 Phase B 的 review）。

---

## 0. 三档所有权定义与使用方式

| 档 | 含义 | 面试时的自我约束 |
| --- | --- | --- |
| **A · MUST EXPLAIN WITHOUT NOTES** | 必须能**不看稿**讲清，并能在白板上画出数据流或状态迁移 | 没到这个程度就先别讲这条，宁可说"这块我记得是…但细节要回去核实" |
| **B · SHOULD UNDERSTAND** | 应当理解其含义与边界，被追问细节时能定位到仓库 | 可以讲，但**不要主动展开细节** |
| **C · ROADMAP / DO NOT CLAIM AS IMPLEMENTED** | 未实现或仅有方向 | **绝不能说成已实现**；只能作为"我知道边界在哪" |

**使用建议**：先按 §5 的白板清单自测（能画出来吗），再回查 §6 的"最容易讲错"清单。

---

## 1. 一张白板图（必须能默画）

```text
        ┌── Simulator：确定性虚拟从站 + 故障注入四模式 ──┐
        ├── Replay：.mlog v1 文本日志 → 整批重析 ────────┤
        └── Serial：USB-RS485 · 一次一笔事务 ───────────┘
                          ↓ 只产生"字节"
        Deterministic Modbus Core（modbuslens_core · Zero Qt 静态库）
          protocol : CRC-16/MODBUS · RTU 帧模型 · wire 编解码 · FC03/FC06/FC16 语义
          analysis : 请求-响应配对 → 归一化状态(7) + 正交 Issue(两轴)
          stats    : 三计数 / 五分类 / optional 成功率与延迟
          diagnosis: 规则引擎 → 纯 enum finding + 建议检查项
                          ↓
        App / Adapter 层（Qt 类型只允许出现在这一层）
          Controller · QAbstractListModel · SerialPortAdapter · provider 客户端 · ProfileStore(Qt JSON)
                          ↓
        QML UI（6 工作区）
                          ↓ 可选上层（无 provider 时全部不参与，核心仍完整）
        AI Explanation（one-shot）· Read-only Agent（3 个只读工具，回合/调用硬预算）
```

**画完后必须能补的三句话**：① 三种模式共享的是**核心函数**，没有统一数据源接口；② 协议事实
只由核心产生，UI / AI / Agent 都只是消费者；③ 写路径是**用户确认式**的串口操作，与 Agent 无关。
`[C-02]` `[V-158]` `[V-229]`

---

## 2. A 档 —— MUST EXPLAIN WITHOUT NOTES

> 格式统一：**是什么 / 用在哪 / 为什么存在 / 设计选择 / 可替代方案 / 遗留限制 / 证据 / 数据流**。

### K-01 · 产品定位与"三个不是" — A
- **是什么**：Modbus RTU 通信分析与故障诊断工具，把字节流变成"这笔事务发生了什么"的可复查证据链。
- **用在哪**：全项目定位与所有对外表述。
- **为什么存在**：现场排障时只有十六进制字节，判断"无响应 / 校验错 / 异常码"成本很高。
- **设计选择**：**主动说边界**——不是 SCADA、不是完整 Modbus Master、不是"AI 自动修设备"。
- **可替代**：做成通用抓包分析器或全功能码 Master（都超出单人项目的范围纪律）。
- **遗留限制**：功能码**有意收敛**（一等支持 FC03 读 + FC06/0x10 写）；无 register map 语义层进入诊断。
- **证据**：`01` P1–P14；`03` D1/D2；`05` §1 十项边界。`[V-309]` `[V-300]`
- **数据流**：能讲"字节 → 帧 → 事务 → 统计/诊断"四段即可。

### K-02 · Zero-Qt 核心与依赖方向 — A
- **是什么**：协议与诊断核心编译为静态库 `modbuslens_core`，**不链接任何 Qt**。
- **用在哪**：`src/core/` 全部；构建图。
- **为什么存在**：① 三模式必须口径一致；② 核心要能在无显示器环境完整测试。
- **设计选择**：严格单向依赖 `QML → Controller/Model → core`；**禁止反向**；Qt 类型只允许在 App/Adapter 层。
- **可替代**：把逻辑写进 `QObject`（省一层适配，但核心无法脱离 Qt 测试与复用）。
- **遗留限制**：边界纪律有成本——`optional` 必须在边界显式拆成 `hasX + value`，不能顺手用 `QString`。
- **证据**：`modbuslens_core` 无 `target_link_libraries`；`grep` Qt include 于 `src/core/` = **0**。`[V-158]` `[V-196]`
- **数据流**：能指出"哪一层允许出现 Qt 类型"。

### K-03 · 三数据源共享**核心函数**（不是共享接口） — A ⚠️ 高频讲错
- **是什么**：Simulator / Replay / Serial 复用同一批分析函数与统计。
- **用在哪**：`ReplayAnalysis` → `analyzeObservedTransaction`；`SerialTransactionSession` → `analyzeFunction03Transaction`；最终汇入 `summarizeTransactions`。
- **为什么存在**：三种模式若各写一套判断，结论口径必然漂移。
- **设计选择**：**有意不提取**统一数据源接口——"单数据源阶段不过早设计，等出现真实共性再提取"。
- **可替代**：定义 `IFrameSource`（`open/start/stop/close` + 帧回调 + 统一 `RawFrame`）。
- **遗留限制**：**该接口从未实现**——`grep -rn IFrameSource src/ tests/` = **0 命中**，`src/io/`、`src/app/` 不存在。
  正确说法是"复用核心函数 + 口径断言"，**不要说"有统一数据源接口"**。
- **证据**：`[C-02]`（冲突已并列记录）；口径一致性有断言：Golden Replay == Simulator Demo 统计严格相等。`[V-198]`
- **数据流**：能画三条来源如何汇入同一个核心函数。

### K-04 · Modbus RTU 帧结构与"知识 ≠ 采集能力" — A
- **是什么**：ADU = 从站地址(1B) + 功能码(1B) + 数据(0–252B) + CRC16(2B，低字节先)。
- **用在哪**：帧模型与编解码；协议知识库。
- **为什么存在**：一切解析与配对的物理基础。
- **设计选择**：**帧内存模型刻意不存 CRC**——CRC 属线路层，由编解码器负责。
- **可替代**：把 CRC 放进帧结构（会让"语义对象"混入传输层细节）。
- **遗留限制（重要）**：**t1.5 / t3.5 只作为协议知识存在，不是已实现的采集能力**——
  Serial 按"期望响应长度 + 超时"定界，**没有通用静默间隔扫描**；Replay v1 无 per-byte/chunk 时间戳。
  另：帧内断帧阈值应为 **t1.5**（t3.5 是帧间分离量），fixture 里曾写错并已记录。
- **证据**：`[V-162]` `[V-163]` `[V-172]` `[V-175]` `[V-393]`
- **数据流**：能画出 ADU 布局并指出哪几个字节参与 CRC。

### K-05 · 功能码边界与数量上限的推导 — A
- **是什么**：一等支持 FC03 读、FC06 写单寄存器、Function 0x10 写多寄存器。
- **用在哪**：`Function03/06/16` 解码器与主动请求编码器。
- **为什么存在**：功能码决定请求/响应的合法形状，是校验的依据。
- **设计选择**：只把这三个做成"一等公民"，其余功能码在被动分析里**显式 Unsupported**；
  FC04/custom 只在**解码资格**上一致对待（不硬编码 0x03）。
- **可替代**：实现全功能码（远超单人项目范围，且会稀释验证深度）。
- **遗留限制**：FC01/02/04 等**没有主动支持**。
- **证据 + 能脱口而出的推导**：FC03 quantity **1..125**（响应 `5+2N ≤ 256` ⇒ `N ≤ 125`）；
  FC16 quantity **1..123**（请求 `9+2N ≤ 256` ⇒ `N ≤ 123`）；FC16 `byteCount = 2×quantity`。`[V-309]` `[V-164]` `[V-166]`

### K-06 · 异常响应规则与 MSB 前置守卫 — A
- **是什么**：异常响应 = `请求功能码 | 0x80` + 一个单字节异常码（0x01 非法功能 / 0x02 非法数据地址 /
  0x03 非法数据值 / 0x04 从站故障…）。
- **用在哪**：generic exception matcher（被动分析）。
- **为什么存在**：区分"设备拒绝"与"设备正常回数据"。
- **设计选择**：匹配前必须满足 **`(request.functionCode & 0x80) == 0`**；未知异常码**不猜**。
- **可替代**：只为已知异常码建表（会漏掉厂商扩展码）。
- **遗留限制**：matcher 只要求五个事实（地址、`fn|0x80`、单字节 payload），**不代表支持被拒函数的正常语义**
  （"能诊断 FC08 的异常响应" ≠ "支持 FC08 正常语义"）。
- **证据（含一次真实 bug）**：缺该守卫时 **0x88/0x88 会自我匹配**成 "Exception 0x01"，
  由 Review 前的**专项语义审计**用 RED 测试抓出（21 passed / 1 failed）后修掉。`[V-167]` `[V-285]`
- **数据流**：能画"请求 fc → 期望异常 fc"的映射并解释守卫为何必需。

### K-07 · 广播语义与 `ExpectedNoResponse` — A
- **是什么**：地址 0 = 广播，**写类**功能码才可广播、**谁都不回应**。
- **用在哪**：状态归一化与统计公式。
- **为什么存在**：旧六状态对广播**没有诚实落点**——判 `Success` 是假称写入成功，判 `Timeout` 是把合法沉默当故障。
- **设计选择**：新增**第七状态** `ExpectedNoResponse`（response expectation / observed outcome）；
  统计 `successRate = success / (completed − expectedNoResponse)`，**分母 0 ⇒ `nullopt`**（不是 0%）。
  被动支持集 = **{0x06, 0x10}**（read 永远不可广播）。
- **可替代（ADR-003 已穷尽论证）**：① 六状态不动 + 加 `kind` 字段——**不能独立成立**（仍需一个诚实落点）；
  ② 广播独立 outcome 不进统计——**长期漂移风险最高**（行计数 ≠ observed，两套世界）。
- **遗留限制**：该状态**不证明写入成功、也不证明设备健康**；广播却收到响应 ⇒
  `ProtocolError + UnexpectedResponseForBroadcast`（真异常）。
- **证据**：ADR-003 的完整三案论证与公式；`completed` 含广播**不含 Pending**；
  Baseline 中为 Info 且**不得仅凭广播宣布 Healthy**。`[V-216]`–`[V-220]` `[V-169]`
- **数据流**：能画四情形判定表（单播无响应 / 广播无响应 / 广播+请求违例 / 广播却收到响应）。

### K-08 · CRC-16/MODBUS 与"计算值 vs 线上字节" — A
- **是什么**：Poly `0x8005`（反射形式 `0xA001`）、Init `0xFFFF`、RefIn/RefOut true、XorOut `0x0000`；
  **按位实现**（非查表）。
- **用在哪**：帧校验；`crc` 测试目标。
- **为什么存在**：RS-485 现场链路的字节翻转/丢失不可避免，需要廉价可靠的整帧一致性判定。
- **设计选择**：按位实现（教学清晰、可逐位验证）；**必须手工"先写低字节、再写高字节"**——
  绝不能把 `uint16_t` 直接 `memcpy` 进帧（那依赖主机端序）。
- **可替代**：查表法（更快；但 v1 场景是 10 帧/秒，**无 profiling 证据不优化**）。
- **遗留限制**：CRC 保证**检错不纠错**，**不是加密也不是身份认证**——项目永不把"CRC 通过"当安全证据。
- **证据 + 能脱口而出**：KAT `"123456789"` → 数值 `0x4B37`；`01 03 00 00 00 01` → 数值 `0x0A84` / 线上 `84 0A`。
  **真实踩坑**：曾把规范上印的**线上字节序**（如 `41 12`）误当**数值**（0x4112）记录，被断言当场抓出——
  这说明"自己跟自己比会一致地错"，必须有外部参照。`[V-161]` `[V-284]`
- **数据流**：能指出 CRC 覆盖"地址+功能码+数据"，不含 CRC 自身与静默时间。

### K-09 · 解码错误模型（`variant`）与"无 resync" — A
- **是什么**：`decode()` 返回 `std::variant<Frame, RtuDecodeError>`，错误为 `FrameTooShort` / `CrcMismatch`。
- **用在哪**：wire codec。
- **为什么存在**：Modbus 解析失败是**正常业务情况**，不是异常事件。
- **设计选择**：成功与失败都是**返回值的一部分**，调用方必须显式处理失败分支；不用异常、不用错误码返回值。
- **可替代**：抛异常（影响时序语义、易被忽略）；单一错误码（丢失错误种类）。
- **遗留限制**：编解码**无 resync**——整段输入按"一个候选帧"处理，首字节即 address；
  失败时**不保存**失败字节偏移与原始 wire。
- **证据**：`FrameTooShort` / `CrcMismatch`；`codec` 测试目标。`[V-311]` `[V-163]`
- **数据流**：能画"字节 → decode → 分支处理"。

### K-10 · 事务归一化：7 状态 — A
- **是什么**：`Pending / Success / Exception / CrcError / Timeout / ProtocolError / ExpectedNoResponse`。
- **用在哪**：`TransactionAnalysis` 的 `status` 字段；统计、UI、Agent 白名单都消费它。
- **为什么存在**：把"请求-响应发生了什么"归一成一个可统计、可展示的结局。
- **设计选择**：**语义冻结**——`Success` = 观察到匹配的正常响应；`Timeout` = NoResponse ∧ elapsed ≥ 阈值
  （**不是"设备离线"**）；`CrcError` = 数据到了但校验失败；`ProtocolError` = 数据到了但不可能是合法配对。
- **可替代**：用状态文本判断（**严禁**——控制流必须用 enum，展示文案可随时改）。`[V-242]`
- **遗留限制**：状态**不等于设备健康**；一笔事务一个状态，同一设备多笔事务可各有不同状态。
- **证据 + 演化**：T007 定案六状态 → `docs/09` 记录"恰为六类、无第七个"（**同一日更早时点，是历史快照**）
  → ADR-003 同日批准第七状态。对外统一说 **7 个**。`[V-316]` `[C-04]`
- **数据流**：能画状态迁移与判定阈值。

### K-11 · 正交 Issue 两轴（**13 vs 14 的口径**） — A ⚠️ 高频讲错
- **是什么**：`TransactionIssueCode`（响应侧）+ `TransactionRequestIssueCode`（请求侧），带稀疏 optional 载荷。
- **用在哪**：判定时刻的**具体违规事实**，旁挂在状态之外。
- **为什么存在**：ProtocolError 有五个真实分支全部归一为同一状态 ⇒ **信息在归一化瞬间丢失**。
- **设计选择**：**状态轴与 Issue 轴正交**；Issue 是**旁挂的一维**，老数据集行为逐位不变；
  `requestIssues` 是**有序 collection**（reporting order ≠ discard priority）。
- **可替代**：把细节折进状态（把 6 扩成 20 会**连锁破坏**所有穷举分支、统计、UI 与白名单）。
- **遗留限制**：下游**只读不重判**；requestIssues **不是第八状态、不产生统计维度**。
- **证据 + 口径**：枚举成员 **10 + 4 = 14**（响应侧 10 含 **1 个防御哨兵** `UnknownProtocolError`）；
  **业务口径 9 + 4 = 13**。对外说 **13 个业务 Issue**，被数出 14 时解释哨兵不计业务语义。
  生产不变量：`ProtocolError ⇒ issue.has_value()` **只在生产输出中成立**（防御分支用哨兵）。
  `[V-215]` `[V-317]` `[V-318]` `[V-319]`
- **数据流**：能举 `Success + InvalidRequestByteCount + InvalidRequestLength` 并解释为何这是两维。

### K-12 · Replay per-record 化与 `Unsupported ≠ ProtocolError` — A
- **是什么**：`.mlog` v1 文本日志；语法失败整文件 fail，有效帧之后的一切按**单条记录**保留。
- **用在哪**：`parseReplayLog` → `analyzeReplayLog`。
- **为什么存在**：旧实现把"自己构造的请求一定可信"的**主动模式契约**套在历史流量上 ⇒
  **一条坏记录毒死整批**，而"客户端发错请求 + 设备合法回 Exception"**正是诊断对象**。
- **设计选择**：两类失败分开——文本坏了整文件失败；帧能解析之后，语义违规 / 不支持的功能码都是**per-record 事实**；
  统计只描述 analyzed 子集，**unsupported 显式披露**（绝不伪装成错误）。
- **可替代**：保持整批失败；或静默丢弃 unsupported 记录（会静默丢数据）。
- **遗留限制**：`request-wire` corruption（CRC / FrameTooShort）仍属旧失败契约（deferred）；
  帧内断帧在 v1 中**不可观察**。
- **证据**：RED 是 **13 条断言在旧链路上真实 FAIL**（8 passed / 13 failed）→ GREEN；
  两个桶的行数之和等于有效记录数（**不静默丢记录**）。`[V-048]` `[V-223]` `[V-225]`
- **数据流**：能画"文件 → 语法层 → 记录层 → 统计桶 + unsupported 桶"。

### K-13 · Serial 事务会话契约 — A
- **是什么**：主动串口路径——一次一笔 outstanding、Busy 拒绝、chunk 累积 + candidate 收口、oversized 不截断。
- **用在哪**：`SerialTransactionSession`（Zero Qt）+ `SerialPortAdapter`（薄 Qt 适配）。
- **为什么存在**：Modbus RTU 是"一问一答"协议，串口是字节流，必须在两者之间建立事务边界。
- **设计选择**：**transport 生命周期与 transaction 生命周期分离**（连接 ≠ 事务挂起）；
  timeout **双路语义**：空 buffer → NoResponse → `Timeout`；有部分字节 → **wire truth**
  （`CrcError` / `ProtocolError`）——**绝不伪造 Timeout**；transport error **不等于** Modbus 诊断。
- **可替代**：单 open API（状态面小但混淆两种生命周期）。
- **遗留限制（重要）**：`serialConnected` **只表示本地串口已打开**；
  `QSerialPort::isOpen()` 是**句柄自述状态**（设备消失后仍为 true）——
  "适配器还在不在"必须问 OS（`availablePorts()` 走 `DIGCF_PRESENT`）。
  **这不是远端 Modbus 存活检测**（不寻址、不发帧；从站沉默仍保持端口存在）。
- **证据**：`SERIAL-A01~A16`；`test_serial_adapter` i09–i13；R15/R16。`[V-326]` `[V-145]` `[V-361]`
- **数据流**：能画"开始事务 → 写请求 → 收字节（累积/收口）→ 判定 → 终态"。

### K-14 · 主动写安全路径（Confirmation vs Dispatch / PREVIEW == WIRE） — A
- **是什么**：FC06 / FC16 的用户确认式写：prepare/validate → 写前快照 + 原值回显 → 确认对话框 → 派发。
- **用在哪**：写 UI + Controller dispatch + 串口会话。
- **为什么存在**：一旦工具能写设备，它就从"旁路观察者"变成"协议参与者"，风险面完全不同。
- **设计选择**：**确认与派发严格分离**（确认对话框出现前不产生任何下发；rapid Enter 无法逃逸确认）；
  **PREVIEW == WIRE**——预览字节必须来自 dispatch 用的**同一个 production encoder**，
  预览入口绝不 dispatch / 绝不建 snapshot（逐字节契约 `pv1–pv4`）。
- **可替代**：点一下直接下发（UX 更顺，但用户确认的不是实际要发的字节时是**安全性缺陷**）。
- **遗留限制**：**真实串口写未验证**（只有自动化 + harness/seam 证据）；
  未连接串口时写入禁用；**Agent 与写路径严格隔离**。
- **证据**：`db2aea9`（确认-派发分离）、`c50dbfe`（rapid Enter 无法逃逸）、`pv1–pv4`、R17
  （写超时覆盖 0x06 **与** 0x10；"恰一个 Timeout + 冻结文案 + 无 Success"）。`[V-067]` `[V-229]` `[V-330]` `[V-301]`
- **数据流**：能画"参数校验 → 快照 → 确认 → 编码一次 → 原样写 wire"。

### K-15 · `optional` 防假 0 与统计的 `nullopt` 语义 — A
- **是什么**：`std::optional` 在 Qt 边界拆为 `hasX + value`；统计的成功率/延迟为 optional。
- **用在哪**：`TransactionStatistics`；Controller 属性。
- **为什么存在**：**0% 与"无法计算"是两件事**——不能把"无数据"偷偷变成 0。
- **设计选择**：`observed = pending + completed`；`completed` 含广播**不含 Pending**；
  `successRate = success / (completed − expectedNoResponse)`，**分母 0 ⇒ `nullopt`**（不是 0%、不崩溃）；
  `averageSuccessLatencyMs` **只计 Success**。
- **可替代**：用 0 表示"无数据"（会让界面显示"成功率 0%"这种假事实）。
- **遗留限制**：统计只按 high-level status 聚合；**Issue 不产生统计维度**。
- **证据**：`STAT-B09/B10`、`DIAG-A14`（正交性锁定）。`[V-126]` `[V-320]` `[V-321]`
- **数据流**：能画统计口径与四不变量。

### K-16 · QML 三条"静默失败"语义 — A ⚠️ 高频讲错
- **是什么**：QML 里三类**不报错却做错事**的语义。
- **用在哪**：全部 QML 与自定义组件。
- **为什么存在**：QML 的绑定失败多是**运行时诊断**且**不改退出码**，极易逃过门禁。
- **设计选择（三条必须能背）**：
  1. **无限定名不是词法作用域**——解析链只含「自身属性 + 组件 **root** 对象属性 + id」，**不含中间祖先**；
     放在中间对象上让嵌套子项按名读 ⇒ `ReferenceError` 而退出码仍 0。
  2. **`Item` 作为自定义组件根若无 implicit 尺寸**（默认 0）⇒ 父布局**静默少算整块内容**，
     表现为子项"溢出渲染"而非布局报错。
  3. **anchors 消费父尺寸但不产生父的 implicit** ⇒ 容器组件 implicit 必须由 content-layout 派生，否则塌缩。
- **可替代**：依赖框架报错（它不报）；靠人工目测（会漏）。
- **遗留限制**：这些只能靠**运行时几何断言 + 负向对照**兜住，不是写对一次就永久安全。
- **证据**：`[V-140]` `[V-279]`（ISSUE-016）；`[V-141]` `[V-281]`（ISSUE-018）；`[V-060]` `[V-275]`（ISSUE-012）。
- **数据流**：能指出"属性该放在哪一层才可见"。

### K-17 · 单一 timeout owner + abort reason — A
- **是什么**：网络请求的超时必须**只有一个** owner，且取消原因必须**在 abort 之前**记录。
- **用在哪**：AI 客户端。
- **为什么存在**：`QNetworkReply::error()` **不含 abort 主体信息**；多个 timeout 机制并存 ⇒ 竞态。
- **设计选择**：`enum class AiAbortReason { None, UserCancel, Timeout, BatchInvalidated, DiagnosisCleared,
  SupersededRequest }`；**移除 native transfer timeout**，`QTimer` 成为唯一 owner；生产 timeout 30s → **90s**；
  **任何路径不再把 Qt `errorString()` 当用户文案**。
- **可替代**：直接用 `errorString()` 展示（会泄漏 transport 语义，用户看到"操作被取消"）。
- **遗留限制**：旧失败请求的**精确 elapsedMs 未实测**（WIN32 GUI 无输出通道）——档案**没有**编造该数字。
- **证据**：ISSUE-005（最强 RCA #3）；"错误分类 ≠ 错误呈现"。`[V-033]` `[V-268]`
- **数据流**：能画"发起 → 两个可能的取消来源 → 如何区分归属"。

### K-18 · 双层 stale guard（captured vs current + generation） — A
- **是什么**：一个 Agent run 在启动时捕获批次版本与 run 代数，之后每次应用结果前校验两者。
- **用在哪**：Agent runtime 与 Controller 接线。
- **为什么存在**：多轮异步交互里，"数据还对吗"与"run 还新吗"是**两个正交问题**。
- **设计选择**：`capturedBatchRevision == currentBatchRevision` **AND** `runGeneration == currentAgentGeneration`；
  **无第三套版本系统**；batch 变化 ⇒ 立即静默失效。
- **可替代**：单一版本号（无法表达两种失效）。
- **遗留限制**：batch-scoped `transaction_number`（1-based 序号）在 batch 切换后失效 ⇒
  **跨 batch 查询天然不支持**（v1 无历史存储）。
- **证据**：ADR002 D-A3 与 Phase 1 identity model；`AGENT-B01~B23`。`[V-347]` `[V-178]`
- **数据流**：能画"捕获 → 多轮 → 每轮校验 → 丢弃或提交"。

### K-19 · harness 的环境也是被测契约 — A
- **是什么**：确定性 harness 必须**声明自己的配置真值**，不能继承 ambient 环境。
- **用在哪**：全部 `--qml-*` 确定性门禁。
- **为什么存在**：`MODELSCOPE_API_KEY` 曾在 Windows User 级持久存在 ⇒ 注入每个测试进程。
- **设计选择**：任何 `--qml-*` harness 在创建 controller 前 **`qunsetenv` 凭据**；
  入口加 **guard**（凭据可见即 `refusing to run`，exit 1）；并补 `QT_ASSUME_STDERR_HAS_CONSOLE=1` 契约。
- **可替代**：信任调用者不设凭据（真实事故即由此发生）。
- **遗留限制 / 为什么重要**：**事故真实发生过**——同意门点击被降级为**真实云端推理**
  （`candidateCount=1`；`15.87s − 1.87s ≈ 14s ≈` HTTPS 往返），并向 Human 做了治理披露；
  且"授权的 Live smoke"**不得与此混同**。
- **证据**：ISSUE-020（最强 RCA #4）；单变量对照 4 个 Run；负向对照 MUTATION-NX1 证明 guard 可触发。`[V-094]` `[V-283]`
- **数据流**：能讲"哪一层继承了什么环境变量、如何切断"。

### K-20 · 诊断：纯 enum finding + 不宣称物理根因 + **不消费设备档案** — A
- **是什么**：`RuleBasedDiagnosis` 输出纯 enum finding + 12 种建议检查项。
- **用在哪**：Baseline 诊断面板与 prompt 事实源。
- **为什么存在**：诊断软件的可信度来自"只说观察到什么、建议检查什么"，不宣称物理根因。
- **设计选择**：**不存在 health score**（0–100 被明确列为设计红线）；"可能原因"只出现在 possible checks；
  finding 有固定顺序（Protocol → CRC → Timeout → Exception(asc) → Pending → ExpectedNoResponse →
  RequestIssue → Healthy/NoData）。
- **可替代**：让模型/规则直接给"根因结论"（会制造假事实）。
- **遗留限制（**C-03 当前权威口径**）**：M11 多类型解码与 M12 设备档案 register map + scale/offset/unit
  **只作用于 Read Result 的语义呈现**；**诊断引擎不消费设备档案**
  （`grep -rn "DeviceProfile\|RegisterEntry\|profile" src/core/diagnosis/` → **0 命中**）；
  0x02 仍**不指认具体寄存器语义**；无设备模板库、无自动工程单位推断、不做量程/报警阈值判定。
- **证据**：`[V-322]` `[V-323]` `[V-168]` `[C-03]` `[V-558]`
- **数据流**：能画"事实 → finding 族 → 建议检查项"，并说清**设备档案不在这条链上**。

### K-21 · AI is interpreter, not detector（含"正例语义"） — A
- **是什么**：LLM 只解释已算好的结构化事实；**不产生协议事实**。
- **用在哪**：`DiagnosisPromptBuilder` → provider → PlainText 渲染。
- **为什么存在**：如果让模型参与协议判定，幻觉会污染诊断口径；且断网/未配置时产品会残废。
- **设计选择**：下发**结构化事实**（一次最多 20 条事务明细）+ system instruction 给**每种状态唯一允许的表述**
  （CRC/Timeout/0x02 各一条正例语义）+ **Mixed Error Independence** + **Evidence Scope Guard**
  （无条件 `evidence_scope=current_observed_batch`）；AI 输出一律 **PlainText**。
- **可替代**：只给"不要宣称根因"这类**否定式**规则——**实测挡不住**过度归因。
- **遗留限制**：输出质量仍依赖 provider 与小样本；`ISSUE-008`/`ISSUE-009` 的历史根因**至今 UNKNOWN**。
- **证据**：ISSUE-006 的 A–F 逐层定责（Core/Client/QML **零责任**，修正只在 prompt 语言层）；
  `AI-B14~B17`；**Live 1 次授权请求五项验收 PASS**。`[V-031]` `[V-269]` `[V-346]`
- **数据流**：能画"事实通道 vs 语言通道"分离。

### K-22 · Agent 只读：白名单 + 预算 + "LLM 只能选择" — A
- **是什么**：恰好 **3 个只读工具**（`get_session_summary` / `get_recent_anomalies` / `get_transaction_detail`）。
- **用在哪**：Agent runtime + dispatcher + QML 问答区。
- **为什么存在**：让模型按需读事实，同时把越权面冻结在 contract 里。
- **设计选择**：**写能力在类型层面不存在**（无枚举值、无分支、无 schema）；
  immutable snapshot（run 启动时一次构建）；**validate-then-execute、零部分执行**；
  硬预算 **rounds=3 / total calls=6**；`get_recent_anomalies` 的 "recent" = **latest-20**。
- **可替代**：动态 tool registry（扩展容易但**放弃类型级保证**）。
- **遗留限制**：多步深问可能触顶（total 预算由 3 → 6 是**真实 Live 超限后经 RCA 调整**的）；
  同一条哲学在文档域表现为 **app-resolved citationIds**：模型只能在 app 给的块 id 里"选择"，
  offset/hash 由 app 回填，**未知 id fail-closed** ⇒ **LLM 只能做选择，不能做权威数据**。
- **证据**：ADR002 D-A1/D-A2/D-A3；`grep` 写工具 **0 命中**；`AGENT-A01~A13` / `AGENT-B01~B23`；
  ISSUE-007（拒绝编造 tool sequence）。`[V-209]` `[V-304]` `[V-270]` `[V-288]`
- **数据流**：能画"问题 → 快照 → tool_calls → 校验 → 三工具 → role=tool → final"。

### K-23 · 验证体系：70 目标 + fake 组件 + 负向对照 + 门禁两铁律 — A
- **是什么**：CTest **70** 个目标；本地 fake provider / fake serial；负向对照纪律；两条门禁铁律。
- **用在哪**：全项目验证。
- **为什么存在**：诊断项目的可信度 = 证据的可信度。
- **设计选择**：AI 与 Agent 测试用**本地 fake Chat Completions server（零真实网络）**；
  串口用 **fake serial transport**；**负向对照** = 真实 mutate → 观察 RED → **精确逆向 patch** 还原 → 基线复绿
  （**没有真实 RED 不算 evidence**）；门禁两铁律 = ① 要断言必须**先可观测**（WIN32 子系统需
  `QT_ASSUME_STDERR_HAS_CONSOLE=1`，否则门禁什么都没断言）② **门禁平台必须与被验收平台一致**
  （offscreen vs windows QPA 行高差 4px 会累积越界）。
- **可替代**：只查 exit code（实测漏掉过整整一类缺陷）；只用 offscreen（实测漏掉真实平台几何）。
- **遗留限制**：**未接 CI**，验证链是本机的；`docs/12` 的"24 个目标"是 v1 历史快照，
  **现状真值是 70**（历史值引用时必须注明）。
- **证据**：`[V-354]` `[V-030]` `[V-355]` `[V-356]` `[V-232]` `[C-05]`
- **数据流**：能讲清"哪一层用什么手段验证、以及它不证明什么"（自动化不证明视觉可接受、
  人工不证明无回归、发布验证不证明工业适用性）。`[V-363]` 区域

### K-24 · 部署身份：minimal-PATH + runtime provenance — A
- **是什么**：部署正确性是**内容身份**问题，不是"文件在不在"。
- **用在哪**：`scripts/deploy_windows.bat`、candidate 树、打包链、启动门。
- **为什么存在**：ISSUE-002 证明 **"exe 能链接通过 ≠ 能启动"**。
- **设计选择**：独立部署目录（应用目录是 DLL 搜索首位，天然屏蔽 PATH 污染）+
  **强制从编译器 bin 覆盖 MinGW 三件套** + **provenance SHA-256 复核** +
  启动门用 **minimal-PATH**（只含应用目录与系统目录）；打包消费 **candidate 树**（从零重建），
  以**逐文件 SHA-256 manifest** 与 fail-fast 校验防"打包错产品"。
- **可替代**：直接交付 `build/<cfg>/modbuslens.exe`（实测会失败）；改开发机 PATH（掩盖问题而非修复）。
- **遗留限制**：流程长且**环境阻塞会直接阻断整条链**（曾因宿主 QProcess 管道缺陷挂起，
  且**拒绝**手工拼装部署树冒充产物）；包**未签名**、**无 installer**。
- **证据**：ISSUE-002（最强 RCA #1）；`[V-021]` `[V-265]` `[V-237]` `[V-306]` `[V-307]`
- **数据流**：能画"构建输出 → candidate → manifest → ZIP → 验收身份（exe SHA-256）"。

---

## 3. B 档 —— SHOULD UNDERSTAND

> 结构从简：**是什么 / 证据 / 遗留限制或代价**。可以讲，但不主动展开。

| # | 主题 | 是什么（一句话） | 证据 | 遗留限制 / 代价 |
| --- | --- | --- | --- | --- |
| K-25 | 字节序 vs 字序 | 16-bit 寄存器帧内**大端**；32-bit 跨两寄存器时**字序**设备间不一致，是现场"数据看不懂"头号原因 | `[V-170]` | 解码是**呈现层**能力，raw 与解析结果**分列**，绝不回写原值 `[V-256]` |
| K-26 | 0-based vs 40001 | PDU 地址从 0 计数；`40001` 是 1 计数人读编号 ⇒ off-by-one 源头 | `[V-171]` | 项目用 **PDU / 0-based** 口径并在 UI 标注 |
| K-27 | 规则诊断 finding 族与顺序 | 固定顺序：Protocol→CRC→Timeout→Exception(asc)→Pending→ExpectedNoResponse→RequestIssue→Healthy/NoData | `[V-323]` | Healthy 有**五条件**；不得仅凭广播宣布 Healthy |
| K-28 | Simulator 故障注入四模式 | `None / DropResponse / CorruptCrc / ArtificialDelay`，wire 层、元数据延迟 | `[V-173]` | **不靠随机**——故障必须可复现才能被测试锁定 |
| K-29 | Replay 是批处理不是实时播放 | 历史记录瞬间重析，无时间轴/调速/`sleep` | `[V-199]` | `.mlog` v1 语法稳定；无通用抓包导入 |
| K-30 | QML 原生 style 静默忽略自定义 | 改了样式不生效且不报错 | `[V-138]` `[V-294]` | 最终改用 `Fusion`；这条属"踩过才知道" |
| K-31 | 持久化在 Adapter 层 | 设备档案/手册库用 Qt JSON（`QSaveFile` 原子写 + 路径安全硬化） | `[V-154]` `[V-252]` | **文件型 JSON，无事务、无并发控制**；schema version = 1 |
| K-32 | delegate 在 model reset 时被再求值 | 正确使用 `beginResetModel/endResetModel` **不阻止** delegate 按失效 index 再求值一次 | `[V-280]` | 修法是**容错读取**（不用 `required property`，那会**冻结**值、破坏实时更新） |
| K-33 | 版本权威单一来源 | CMake `project(... VERSION 2.0.0)` 是唯一公开版本权威，PE VERSIONINFO 同源派生 | `[V-243]` `[V-188]` | 版本改动必须经单一入口；曾分三步才收敛 |
| K-34 | 档案不可变 + LKGC 单步推进 | 档案区只增不改；**HEAD ≠ verified LKGC**；**docs-only 永不作 LKGC** | `[V-248]` `[V-245]` | 代价：文档会长期并存互相过时的表述，须靠 superseding note 维护 |
| K-35 | UI 语言政策 | 简中为主 + 专业实体保留英文 + **判定永不依赖文案** + Core 绝不出用户文案 | `[V-242]` `[V-240]` | 产品单语；若要多语言须**先立 ADR** |
| K-36 | PDF/DOCX 手册导入 | PDFium（pinned）+ libzip + zlib，**离线物化 + 配置期 hash 断言** | `[V-341]` `[V-181]` | **扫描件 OCR 未实现**；依赖体积进入便携包 |
| K-37 | candidate 契约（发布输入） | Human 验收/打包的唯一输入树，每次**从零重建**，Release/Debug 各自独立树 | `[V-185]` | 禁止消费 raw output、禁止复用历史 deploy/package 树 |
| K-38 | QML 单例的务实替代 | `DS` 用 engine root context property 而非 module singleton | `[V-058]` `[V-251]` | **机理级根因未完全隔离**（受里程碑边界约束未深挖 Qt internals）——讲这条要**如实说未隔离** |

---

## 4. C 档 —— ROADMAP / DO NOT CLAIM AS IMPLEMENTED

> **绝不能说成已实现**。只能作为"我知道边界在哪"来讲。

| # | 项 | 事实 | 证据 |
| --- | --- | --- | --- |
| C-01 | **Modbus TCP（MBAP）** | 未实现（`grep` 0 命中；需求条目仍为 `Proposed`） | `[V-305]` `[V-388]` |
| C-02 | Modbus ASCII | 未实现（仅协议知识） | `[V-389]` |
| C-03 | 真实 Modbus 硬件 / 现场泛化验证 | **REAL MODBUS HARDWARE = NOT VERIFIED**（含多设备/多从站泛化） | `[V-300]` `[V-362]` `[V-392]` |
| C-04 | 真实串口写验证 | 未验证（只有自动化 + harness/seam） | `[V-301]` `[V-360]` |
| C-05 | installer / 代码签名 | 均不存在（未签名便携 ZIP） | `[V-306]` `[V-307]` `[V-375]` `[V-376]` |
| C-06 | CI 流水线 | 未接（验证链是本机的） | `[V-396]` |
| C-07 | t1.5 / t3.5 时序分析 | 只是知识，非采集能力 | `[V-175]` `[V-393]` |
| C-08 | UART parity / framing / overrun 明细 | adapter 只保留 `errorString()` 一次性文本 | `[V-146]` `[V-394]` |
| C-09 | 扫描件 PDF / OCR | 未实现 | `[V-395]` |
| C-10 | 数据库 / Docker / MCP / RAG / vector DB / 多智能体 / OpenAI SDK | 明确不在本项目 | `[V-396]` |
| C-11 | Health score（0–100）/ 自动判健康 | 设计红线明确禁止 | `[V-397]` |
| C-12 | 通用物理根因判定 | 非目标；只给 observed + possible checks | `[V-398]` |
| C-13 | FC03 以外的主动读功能码（FC01/02/04） | 一等支持仅 FC03；FC04/custom 只在解码资格上一致 | `[V-399]` |
| C-14 | 当前 GitHub 远端页面/tag/元数据状态 | **UNVERIFIED**（未在 A/A1/B 实时核验） | `[U-10]` |
| C-15 | `docs/INTERVIEW_NOTES` F8「无 register map / 工程单位层」 | **UNVERIFIED**，且已被判定过时 ⇒ **不得正反两向引用** | `[U-11]` `[C-03]` |
| C-16 | 部分档案全文细节 | 项目/T027 部分区间为**定点读取** ⇒ 不声称穷尽内部细节 | `[U-03]`–`[U-09]` |

---

## 5. 白板复现清单（自测：能画出来才算 A 档）

| # | 必须能画的东西 | 关键要素（画漏就是没掌握） |
| --- | --- | --- |
| 1 | **整体数据流** | 三数据源 → Zero-Qt 核心四段 → Adapter → UI → 可选上层；**写路径在 UI 侧，不在 Agent 侧** |
| 2 | **ADU 帧布局** | 地址 / 功能码 / 数据 / CRC 低字节先；标注 CRC 覆盖范围 |
| 3 | **事务判定** | 请求-响应配对 → 7 状态；Timeout = NoResponse ∧ 阈值；`ProtocolError` = 数据到了但不可能是合法配对 |
| 4 | **两轴模型** | 状态轴（7）与 Issue 轴（响应 9 业务 + 请求 4 = 13；枚举 14）**正交**，举例复合结果 |
| 5 | **广播判定表** | 四情形（单播无响应 / 广播无响应 / 广播+请求违例 / 广播收到响应） |
| 6 | **统计口径** | `observed = pending + completed`；`rate = success/(completed − expectedNoResponse)`；分母 0 ⇒ `nullopt` |
| 7 | **Replay 两桶** | 语法桶（整文件失败）vs 记录桶（analyzed 统计 + unsupported 显式披露，两桶之和 = 有效记录数） |
| 8 | **写路径** | 校验 → 快照 → 确认 → **编码一次** → 原样写 wire（PREVIEW == WIRE） |
| 9 | **Agent 链路** | 问题 → immutable 快照 → tool_calls → 类型层校验 → 3 工具 → `role=tool` → final；标注双层 stale guard |
| 10 | **部署身份链** | 构建输出 → candidate（从零重建）→ manifest（逐文件 SHA-256）→ ZIP → 验收身份 = **exe SHA-256** |
| 11 | **ISSUE-002 推导链** | 报错符号 → 推断 GCC 版本差 → `where` 顺序 → `objdump` 符号对照 → 单变量 PATH 验证 → provenance 修复 |
| 12 | **ISSUE-004 四轮** | 每轮假设 → 被什么证据推翻 → 最终为何是 **workspace 结构缺陷**（SplitView） |

---

## 6. 最容易讲错的 10 条（自我校准清单）

| # | 陷阱 | 正确口径 |
| --- | --- | --- |
| 1 | "有统一数据源接口 / IFrameSource" | 共享**核心函数**；该接口**从未实现**。`[C-02]` |
| 2 | "13 个 Issue" 或 "14 个 Issue" 只说一个 | 枚举 14 / **业务 13**；被数出 14 时解释**防御哨兵不计业务语义**。`[V-317]` |
| 3 | "24 个测试目标" | 那是 **v1 历史快照**；**现状 70**。`[C-05]` |
| 4 | "6 类状态" | 早期记录（`docs/09` 同日更早时点）；**现为 7 类**。`[C-04]` |
| 5 | "已在真实设备上验证 / 真机跑通了" | **NOT VERIFIED**；用过真机硬件的是**适配器热拔检测**（不做 Modbus I/O）。`[V-300]` `[V-302]` |
| 6 | "发布状态我刚确认过" | 仓库留档 VERIFIED；**当前远端状态 UNVERIFIED**。`[U-10]` |
| 7 | "Agent 可以帮我下发" | Agent **无写 API**；写只能由用户确认后经串口派发。`[V-304]` |
| 8 | "诊断会结合设备档案判断" | **诊断引擎不消费设备档案**（0 命中）；语义层只覆盖**读数呈现**。`[C-03]` |
| 9 | "支持 t3.5 帧切分" | t1.5/t3.5 是**知识**；帧定界按"期望长度 + 超时"，**无静默间隔扫描**。`[V-175]` |
| 10 | "文档里说没有 register map" | 该表述**已被判定过时**；用 README §11 当前口径。`[C-03]` `[U-11]` |

---

## 7. 下一步（不属本阶段）

Phase B 到此为止。**下一阶段（Phase C，须经 Human review 后授权）**才做：
大型问答题库、模拟面试、压力题演练、背诵卡、100 题清单、HR 问答库。
——本文件刻意只做"**我自己必须懂什么**"的地图，不做话术库。