# 09 — 诊断覆盖审计（Diagnostic Coverage Audit）

> 状态：v1（M8.1，2026-09-13 建立；STRICT DOCS-ONLY，未改任何代码/测试/配置）
> 触发：用户在完整理解项目后，用更接近真实现场的 `samples/demo_v2.mlog`（14 场景）检验当前产品，发现五个疑点：
> 1. 不能表达/诊断所有真实 Modbus RTU 场景；2. TransactionStatus 易被误解为 Device Health；
> 3. Replay v1 对 Broadcast / timing event 表达不足；4. FC03-centric request trust contract 可能丢掉
> “客户端发错请求但设备合法返回 Exception”这一诊断事实；5. 部分物理故障只能看到症状、无法仅凭 wire 定唯一根因。
> 本审计只建立“当前系统真正能观察什么 / 保存什么 / 分类什么 / 诊断什么”的事实，并给出分级结论与决策问题。
> 编号命名：`docs/09_*.md` 符合仓库 `0X_` 前缀规范，直接采用。

## 0. Purpose（为什么做 / 不做什么）

- **Goal**：以最终真实代码 + 独立 CRC 复核 + 真实代码只读 harness（链接已构建的 `modbuslens_core`）为证据，逐场景审计 demo_v2 的 14 个 Record，明确区分 fixture 缺陷 / 故意畸形 / 产品不支持 / 观察数据不足四类情况，并以证据驱动给出最小扩展建议。
- **本阶段不做**：不改任何代码/测试/CMake/QML/scripts；不修 CRC；不推进 LKGC；不修改 PROJECT_STATUS / BACKLOG / 08_KNOWLEDGE_OWNERSHIP（除非发现必须纠正的事实冲突——本审计未发现）；`samples/demo_v2.mlog` 只读分析，不提交/修改/删除/格式化，也不在本阶段把它变成测试 fixture（validated fixture 仅作未来建议，等用户批准）。
- **证据纪律**：所有“当前系统行为”结论均来自 (a) 当前工作区源码逐行核验；(b) 链接真实 `libmodbuslens_core.a` 的只读 harness 实测输出；(c) 独立实现的 CRC-16/MODBUS 计算（以项目两条 KAT 锚定）；(d) 仓库文档原文引用。任何推理/意图/假设都显式标注分类。

## 1. Terminology Model（术语模型，全文基础）

| 层级 | 术语 | 定义（基于当前最终代码） | 例子 |
| --- | --- | --- | --- |
| A | **Transaction Status** | 一次 Request/Response transaction 的**归一化结果**，六类互斥：`Pending / Success / Exception / CrcError / Timeout / ProtocolError`（`src/core/analysis/TransactionAnalysis.h:14-21`） | `Timeout` |
| B | **Observation / Symptom** | 实际观测到的**现象事实**（wire 层或等待层） | CRC 校验失败、无响应字节、响应地址不符、功能码不符、响应形状非法、异常码 0x02 |
| C | **Possible Cause** | 基于现象可以提出的**候选原因假设**（只保证值得检查，不保证为真） | 接线、串口参数、设备供电、噪声、地址配置错误 |
| D | **Proven Root Cause** | 被**证据唯一证明**的物理/配置根因 | （当前模型在 14 个场景中没有任何一例能唯一证明） |

三条铁律（本审计反复使用）：

1. **TransactionStatus ≠ Device Health State**
2. **Observation ≠ Root Cause**
3. **Suggested Check ≠ Proven Cause**

注明：六状态枚举已从真实代码重新核验——`TransactionStatus` 恰为上述六类，无第七个状态（`TransactionAnalysis.h:14-21`；T007 档案“不新增状态”的定案）。

## 2. One Transaction vs One Device

- **一笔 Transaction** → 恰好一个归一化 `TransactionStatus`（`TransactionAnalysis` 是单事务纯函数，`TransactionAnalysis.h:40-47`）。
- **一个 Device / Slave Address** 在一个 batch / 时间窗内可以产生**多笔 Transaction**，每笔状态可以不同：

```text
device 1
  TX #1 → Success
  TX #2 → Exception       ← 这是四笔不同的 transaction
  TX #3 → CrcError         不是一笔 transaction 同时有四种状态
  TX #4 → Timeout          更不是“设备瞬间拥有四个健康状态”
```

- 一个真实设备底层甚至可能**同时存在多个并发问题**（例如 configuration issue + wiring/noise issue），但**每一笔 transaction 依然只有一个归一化 outcome**。
- 当前系统不保存任何“设备健康”对象：`ReplayTransactionOutcome` / `DiagnosisTransaction` 只携带 `deviceAddress / functionCode / analysis(status, elapsed, exceptionCode?)`（`ReplayAnalysis.h:36-42`、`DiagnosisContext.h:17-23`）。含义：**“设备 1 超时”的正确读法 = “地址 1 的一笔事务以 Timeout 归一化”，不是“设备 1 掉线了”。**

## 3. Current Architecture Boundary（当前系统到底保存什么——从最终代码核验）

| 层 | 保存的事实字段 | 不保存（有意或缺口） |
| --- | --- | --- |
| RtuDecodeResult | `ModbusRtuFrame{address, functionCode, data}` 或 `RtuDecodeError{FrameTooShort, CrcMismatch}`（`ModbusRtuCodec.h:12-23`） | 失败字节偏移、原始 wire（仅调用方持有）；**无 resync**：整段输入按“一个候选帧”处理，首字节即 address（`ModbusRtuCodec.cpp:25-50`） |
| TransactionAnalysis | `status`、`elapsed`、`exceptionCode?`（仅 Exception 有值）（`TransactionAnalysis.h:32-38`） | ProtocolError 的**具体原因**（address/function/quantity/shape 五个分支全部归一为同一 status，见 §13）；CrcError 的破坏形态；request 内容（契约输入）；响应寄存器值 |
| ReplayTransactionOutcome / DiagnosisTransaction | `deviceAddress`、`functionCode`、`analysis`（`ReplayAnalysis.h:36-42`、`DiagnosisContext.h:17-23`） | 寄存器**值**（decode 期间瞬态获得、校验后丢弃——见 §15）；raw wire；startAddress/quantity（T007 定案不保留，`T007:104`） |
| ReplayLog record | `elapsed`、`requestWire`（原样字节）、`responseWire?`（原样字节或 nullopt）（`ReplayLog.h:17-23`） | 每字节到达时间、chunk 时间戳、t1.5/t3.5 事件、方向之外任何时序信息 |
| TransactionStatisticsSnapshot | 统计计数 + optional successRate / avgLatency（T007 Part B） | 每设备维度、时间窗维度 |
| DiagnosisReport | 7 类 finding（`RuleBasedDiagnosis.h:22-30`）+ action 建议（12 种） | 因果关系、物理根因（T011 明确“只说 Observed + Possible checks”） |
| Serial session / adapter | 字节 chunk、elapsed、transport error 文本（`SerialTransactionSession.h`；adapter 只保留 `QSerialPort::errorString()`） | parity/framing/break/overrun 结构化事实、每字节时间戳（§16） |
| Agent tool 事实 | 与 Core 相同的模型（地址/功能码/状态/elapsed/exceptionCode + 统计；`AgentTools.h`） | 任何超出 Core 事实源之外的数据 |

**架构结论**：三种模式的诊断事实带宽完全一致——**Core 保存什么，Diagnosis/Agent 就只能基于什么**。这是架构优点（口径一致），也定义了覆盖上限：凡 Core 未保存的事实，任何上层都无从谈起。

## 4. UI Terminology Audit（只审计，不改 QML）

当前最终 UI（`src/ui/qml/Main.qml` + `TransactionListModel.cpp`）：

- 列头：`设备`（Main.qml:795）、`状态`（Main.qml:805）；行文本：`设备 %1`（Main.qml:848）；状态文案：进行中/成功/异常/CRC 错误/超时/协议错误（`TransactionListModel.cpp:7-24`）。

**风险**：同一地址的四笔事务会渲染成四行完全相同的“设备 1”，只有“状态”列不同——极容易被读成“设备 1 处于四种状态/四种健康度”，这正是 TransactionStatus 与 Device Health 被混淆的 UI 级放大器。

**候选术语（recommendation，本阶段不实施）**：

| 当前 | 建议候选 | 理由 |
| --- | --- | --- |
| 列头 `设备` | `设备地址` / `从站地址` | 列内值本身就是 address，不是设备本体 |
| 列头 `状态` | `事务结果` | 六状态是 transaction outcome，不是设备状态 |
| 行文本 `设备 1` | 保留数字地址 + 可选未来列 `事务 #`、`时间戳` | 帮助表达“同一地址的四笔不同事务” |

目标读法示例：`设备地址1 Success / 设备地址1 Exception / 设备地址1 CRC 错误 / 设备地址1 超时` → “同一 Modbus address 的四笔 transaction”。只记录，不改动。

## 5. demo_v2 Fixture Raw Layout（45 逻辑/物理行（L1–L45）· 44 个换行符（末行无 trailing newline，`wc -l` 类工具因此报 44）· 14 TXN 记录）

| # | 物理行 | 注释意图（场景作者） | FC | request wire | response 表示 |
| --- | --- | --- | --- | --- | --- |
| 1 | 6 | FC03 读多寄存器（32 位 float） | 03 | `01 03 00 02 00 02 65 CB` | `01 03 04 41 A0 00 00 3F 6D` |
| 2 | 9 | FC06 写单寄存器（回显） | 06 | `01 06 00 0A 00 64 A9 98` | 同 request |
| 3 | 12 | FC10 写多寄存器 | 10 | `01 10 00 10 00 02 04 00 01 00 02 62 10` | `01 10 00 10 00 02 41 CD` |
| 4 | 15 | 广播写（地址 0，从机不应答） | 06 | `00 06 00 01 00 01 19 9B` | `BROADCAST_NO_RX`（token） |
| 5 | 18 | 不支持 0x08 → Exception 01 | 08 | `01 08 00 00 00 00 E0 0B` | `01 88 01 87 D0` |
| 6 | 21 | 数量超限 → Exception 03 | 03 | `01 03 00 00 00 7E C5 F2` | `01 83 03 80 F0` |
| 7 | 24 | 从机设备故障 → Exception 04 | 03 | `01 03 00 20 00 01 85 C0` | `01 83 04 41 33` |
| 8 | 27 | 从机忙 → Exception 06 | 03 | `01 03 00 00 00 02 C4 0B` | `01 83 06 C1 32` |
| 9 | 30 | RS-485 悬浮杂波（前导 FF 垃圾） | 03 | `01 03 00 00 00 02 C4 0B` | `FF 01 03 04 00 64 00 C8 3B 2A` |
| 10 | 33 | DE/RE 切换过早丢首字节 | 03 | `01 03 00 00 00 02 C4 0B` | `03 04 00 64 00 C8 BA 7A` |
| 11 | 36 | 帧中字符间隙断帧（注释写 >3.5t） | 03 | `01 03 00 00 00 02 C4 0B` | `FRAME_FRAGMENT:01 03 04` \| `FRAME_FRAGMENT:00 64 00 C8 BA 7A` |
| 12 | 39 | 比特翻转 → CRC 失败 | 03 | `01 03 00 00 00 02 C4 0B` | `01 03 04 00 64 00 C8 BA 7B` |
| 13 | 42 | 地址冲突（两个从站同时应答） | 03 | `02 03 00 00 00 01 84 39` | `FF E0 18 00 7F 3C 00 A4 C1` |
| 14 | 45 | 掉电/断线/波特率错 → 完全超时 | 03 | `01 03 00 00 00 02 C4 0B` | `NO_RESPONSE` |

注意：Scenario 11 的 TXN 行有 **5 个字段**（TXN + elapsed + request + 两个 FRAME_FRAGMENT response 字段），超过 v1 exactly-4-fields 契约（`ReplayLog.cpp:151`）。

## 6. Independent CRC Audit（独立重算，不信任注释与聊天）

方法：独立实现的 CRC-16/MODBUS（init 0xFFFF、poly 0xA001 反射、无 xorout），先以项目两条已知 KAT 锚定工具正确性——
`"123456789" → 0x4B37` ✓（T002）；`01 03 00 00 00 01 → 0x0A84`（wire `84 0A`）✓（T003）。随后对 demo_v2 全部字节序列逐条重算（wire 序：CRC 低字节在前）——共 20 条：**19 条为文件中出现的 unique 完整 raw RTU wire field**，**1 条为 S11 两 FRAME_FRAGMENT 拼接得到的 derived candidate**（表中以 derived 标注）。与外部预审给定的候选值逐条吻合（11 项全部一致），另独立补算 8 条预审未覆盖的序列（7 条 raw wire + 1 条 derived）。

| wire | 长度 | provided | 独立计算 | 匹配 | 正确 wire CRC |
| --- | --- | --- | --- | --- | --- |
| S1 request | 8 | 65 CB | 65 CB | ✅ | — |
| S1 response | 9 | 3F 6D | EE 2D | ❌ | `EE 2D` |
| S2 request=response | 8 | A9 98 | A8 23 | ❌ | `A8 23` |
| S3 request | 13 | 62 10 | 22 A2 | ❌ | `22 A2` |
| S3 response | 8 | 41 CD | 40 0D | ❌ | `40 0D` |
| S4 request | 8 | 19 9B | 18 1B | ❌ | `18 1B` |
| S5 request | 8 | E0 0B | E0 0B | ✅ | — |
| S5 response | 5 | 87 D0 | 87 C0 | ❌ | `87 C0` |
| S6 request | 8 | C5 F2 | C5 EA | ❌ | `C5 EA` |
| S6 response | 5 | 80 F0 | 01 31 | ❌ | `01 31` |
| S7 request | 8 | 85 C0 | 85 C0 | ✅ | — |
| S7 response | 5 | 41 33 | 40 F3 | ❌ | `40 F3` |
| S8 request | 8 | C4 0B | C4 0B | ✅ | — |
| S8 response | 5 | C1 32 | C1 32 | ✅ | — |
| S9 response | 10 | 3B 2A | AE 75 | ❌ | `AE 75` |
| S10 response | 8 | BA 7A | B1 A1 | ❌ | `B1 A1` |
| S11 两段拼接（derived，非文件 raw wire field） | 9 | BA 7A | BA 7A | ✅ | — |
| S12 response | 9 | BA 7B | BA 7A | ❌（故意） | `BA 7A` |
| S13 request | 8 | 84 39 | 84 39 | ✅ | — |
| S13 response | 9 | A4 C1 | 02 B5 | ❌ | `02 B5` |

**CRC 审计结论（主口径 = 19 条 unique raw wire）**：demo_v2 中出现的 unique 完整 raw RTU wire 共 **19 条 = 6 条 CRC-valid**（S1req、S5req、S7req、S8req、S8resp、S13req）**+ 13 条 CRC-invalid**。13 条 invalid 中 S12 为**故意的 1-bit flip**（BA 7A→BA 7B，注释属实），其余 12 条为 fixture 手工书写错误（含 S9/S10/S13 三条“故意畸形”场景里的 response——它们的垃圾性质由字节内容表达，CRC 恰巧也未通过；S10 是“原合法帧去掉首字节后 CRC 未重算”）。**另有 1 条 derived candidate 不计入 raw 口径**：S11 两个 FRAME_FRAGMENT 字段拼接出的 `01 03 04 00 64 00 C8 BA 7A`，CRC-valid，但不是原文件中的完整 raw wire field——**若把 derived 也计入，本次共计审计 20 条字节序列 = 7 valid + 13 invalid**。S11 拼接体恰好合法，这本身就是 Timing Gap 的最强证据（见 §20）。

## 7. 14-Scenario Audit Matrix（真实代码实测）

> 实测口径：`parseReplayLog` / `analyzeReplayLog` 由链接 `build/debug/libmodbuslens_core.a` 的只读 harness 运行；“As-is”= 文件原样（单记录隔离）；“CRC 修正后”= 响应/请求 CRC 按 §6 独立计算值修正后的同等 probe。“整文件”= 45 逻辑行原文件（L1–L45，末行无 trailing newline）一次性解析。

| # | 行 | v1 语法 | 整文件 parser（真实） | 单行 parser（真实） | As-is 分析（真实） | CRC 修正后分析（真实） | 修正后 T007 状态 | Baseline finding | Agent 可给事实 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 6 | ✅ | ✗（永不抵达：L15 先失败） | ✅ | CrcError（resp CRC 错） | Success | Success | Healthy | 地址/FC/status/elapsed（值不保留，§15） |
| 2 | 9 | ✅ | ✗ | ✅ | InvalidRequestWire（req CRC 错） | InvalidRequestFunction | —（不进 T007） | —（batch 不发布） | 无 |
| 3 | 12 | ✅ | ✗ | ✅ | InvalidRequestWire（req CRC 错） | InvalidRequestFunction | — | — | 无 |
| 4 | 15 | ❌ `BROADCAST_NO_RX` | ✗ InvalidHex @L15（**整文件在此失败**） | ✗ InvalidHex | 不可达；若替换为 NO_RESPONSE → InvalidRequestWire | InvalidRequestFunction（FC06 门先挡住） | — | — | 无 |
| 5 | 18 | ✅ | ✗（L15 先失败） | ✅ | InvalidRequestFunction（FC08 门） | InvalidRequestFunction（resp CRC 修正后仍同） | — | — | 无 |
| 6 | 21 | ✅ | ✗（L15 先失败） | ✅ | InvalidRequestWire（req CRC 错） | InvalidRequestData（quantity=126） | — | — | 无 |
| 7 | 24 | ✅ | ✗（L15 先失败） | ✅ | CrcError（resp CRC 错） | Exception exc=0x04 | Exception | ExceptionObserved(0x04)+CheckDeviceHealth | 状态+异常码 0x04 |
| 8 | 27 | ✅ | ✗（L15 先失败） | ✅ | **Exception exc=0x06（As-is 即正确，全文件唯一全对场景）** | 同左 | Exception | ExceptionObserved(0x06)+CheckDeviceDocumentation（fallback） | 状态+异常码 0x06 |
| 9 | 30 | ✅ | ✗（L15 先失败） | ✅ | CrcError（10B 整体 CRC 错） | （resp CRC 修正后）decode 出 addr=0xFF/fc=0x01 → **ProtocolError** | CrcError（as-is） | CrcErrorObserved | 状态（无前导垃圾上下文） |
| 10 | 33 | ✅ | ✗（L15 先失败） | ✅ | **CrcError**（8B，CRC 对剩余字节仍不成立；并非“地址漂移成 3/04”的 ProtocolError） | 同左（无 CRC 可修：字节本身是截断产物） | CrcError | CrcErrorObserved | 状态 |
| 11 | 36 | ❌ 5 字段 + `FRAME_FRAGMENT` token | ✗ | ✗ InvalidRecord | 不可达 | 两段拼接 `01 03 04 00 64 00 C8 BA 7A` → **Success**（字节层面完美） | Success（假想） | Healthy（假想） | 无（原样进不来） |
| 12 | 39 | ✅ | ✗（L15 先失败） | ✅ | **CrcError（当前系统最成熟覆盖：req 合法、resp 故意翻转）** | Success（修正 BA 7A） | CrcError（as-is） | CrcErrorObserved | 状态 |
| 13 | 42 | ✅ | ✗（L15 先失败） | ✅ | CrcError（req 合法、resp 垃圾 CRC 错） | 同左 | CrcError | CrcErrorObserved | 状态 |
| 14 | 45 | ✅ | ✗（L15 先失败） | ✅ | **Timeout**（elapsed=1000 ≥ threshold=1000；边界探针 e=999 → Pending） | 同左 | Timeout | TimeoutObserved + 4 action | 状态+elapsed |

**实测要点（对照疑似结论）**：

1. **整个 demo_v2.mlog（45 逻辑行）永远以 `InvalidHex @ line 15` 失败**——parser 是 fail-fast 的，S4 的非法 token 让 S5~S14 全部不可达；即使 S4 修复，S11 的 5 字段=InvalidRecord 会再次整文件失败。**v1 语义 = 全部或全无（all-or-nothing）**。
2. 修正后能进入 TransactionAnalysis 的只有 **S1/S7/S8/S9/S10/S12/S13/S14**（请求均为合法 FC03）。
3. S6 的“地址越界 Exception 03 合法响应”会被请求侧 quantity=126 的 `InvalidRequestData` 挡在门外——**设备合法拒答的事实被整个丢弃**（§19）。
4. S5 请求 CRC 本已合法，仅因 FC≠0x03 被 `InvalidRequestFunction` 拒之门外——**合法性异常响应与异常码 01 一并不可见**（§19）。
5. S10 实测为 **CrcError 而非 ProtocolError**：8 字节 ≥ 最小帧长，decode 走 CRC 校验且失败。场景注释“首字节丢失”无法从最终字节唯一还原。

## 8. Per-Scenario Disposition（分类 + 能/不能确定的事实）

| # | 分类（A/B/C/D/E，定义见 §10） | 能确定的事实（证据类：代码/实测/CRC） | 不能确定的事实 | unsupported feature | fixture defect | 建议处置 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | A（协议层）+ E + D（应用语义） | 修正 CRC 后两寄存器值 `0x41A0 0x0000` 数量一致 → Success | “45 A0 0000 组成 IEEE-754 float 20.0°C”——Core 不解释 | 应用/寄存器语义层 | resp CRC 错 | 修 CRC 可成为 canonical normal 案例；float 语义留给未来 register-map 层 |
| 2 | C + E | 请求与响应字节同为 FC06 写 1 寄存器（值 0x0064） | “设备写入成功”——回显不等于写入成功 | FC06 被动语义解析 | req=resp CRC 都错 | v1 不分析；未来被动 FC06 解码候选 |
| 3 | C + E | FC10 写 2 寄存器（0x0001, 0x0002）的 wire 结构清晰 | 写入结果 | FC10 被动语义解析 | req/resp CRC 都错 | 同上 |
| 4 | C + E | 地址 0 + FC06 是广播写形状；正常广播**不应有响应** | “广播成功送达” | BROADCAST_NO_RX token、broadcast 事务语义 | token 非法 + req CRC 错 | 未来模型候选：`TransactionKind=Broadcast` / `ExpectedNoResponse`（§18） |
| 5 | C + E | 请求 FC08 合法（CRC 正确）；响应形状=异常 01 | “设备不支持 0x08”——被 FC03 门挡住看不出 | 非 FC03 请求+异常的被动表达 | resp CRC 错 | 未来 passive model：unknown FC request + exception response（§19） |
| 6 | C + E | quantity=126 超过 FC03 规范 1..125（`Function03.cpp:9-10`） | “设备因数量超限合法拒答”——当前链条看不到该异常响应 | invalid-request 作为可观察历史事实 | req/resp CRC 都错 | 未来 `RequestIssue::InvalidQuantity + Response::Exception(0x03)` 表达（§19，本阶段最重架构案例） |
| 7 | A（修正后）+ E | 修正后 Exception 0x04 被正确存储；Baseline→CheckDeviceHealth | “传感器损坏/Flash 校验失败”（0x04 具体物因） | — | resp CRC 错 | 修 CRC 即可成为 exception 0x04 覆盖金样 |
| 8 | A（存储）+知识缺口 | As-is 全对：Exception 0x06 被存储；Baseline→CheckDeviceDocumentation fallback | 0x06 具体含义（无专有规则） | — | 无 | 已验证的 0x06 存储/fallback 案例 |
| 9 | B + C（无 resync） | 10 字节 response CRC 不合法 → CrcError | 偏置电阻缺失/总线悬浮/EMI（全是场景意图，不是证据） | 无 resync 前导噪声恢复 | 故意畸形（内容表达） | 负向覆盖案例保留；原因推断只作 troubleshooting direction |
| 10 | B | 截断 response CRC 不成立 → CrcError | DE/RE 切换过早（无 driver-enable/UART trace 不可能证明） | — | 故意畸形 | 症状可检测、根因不可唯一确定，如实保留 |
| 11 | C + D | 字节若拼接 CRC 合法（实测 Success）——**原样行无法进入系统（InvalidRecord）**；timing evidence 完全缺席 | 发生过字符间隙/调度打断 | FRAME_FRAGMENT token、per-byte 时序、Replay v2 event | token + 5 字段非法 | 未来 Replay v2 event/timing 设计候选；注释术语需修正（§20） |
| 12 | A（最成熟）+ B | CrcError 被精确检测（翻转 1 bit） | 翻转是 EMI/线缆/波特率/捕获损坏/软件哪一类 | — | 故意翻转（正确构造） | 与 demo_v1 成熟覆盖同构，保留为负向金样 |
| 13 | B + D | 地址 2 的请求 CRC 正确、response 垃圾 → CrcError | 地址冲突（需 bus timing/多应答者/scope/库存等额外观察） | — | 故意畸形 | 只得症状；原因不可唯一证明，如实保留 |
| 14 | A（状态）+ B | NoResponse + elapsed≥threshold → Timeout（边界 e=999→Pending 实测） | 掉电/断线/波特率/校验/地址/忙碌/收发器故障哪一类 | — | 无 | 最成熟 no-response 覆盖；建议只作 troubleshooting directions |

## 9. Replay v1 Format Gaps（格式缺口清单）

1. **合法 response token 只有一个**：`NO_RESPONSE`（`ReplayLog.cpp:174-182`）。`BROADCAST_NO_RX`、`FRAME_FRAGMENT:` 在代码/文档/测试中零存在（repo-wide grep）。
2. **TXN 行恰好 4 字段**：多字段 → `InvalidRecord`（`ReplayLog.cpp:151`）——S11 直接违反。
3. **fail-fast 全文件语义**：第一个 parse 错误终止解析（S4@L15），后续记录全部不可达；bad request 亦是 batch 级 `ReplayExecutionError`（`ReplayAnalysis.cpp:21-36`），UI 端 `loadReplayFile` 原子发布：任一失败 → 整个批量不发布、旧批次保留（`AnalysisController.h:71-75`）。**一条非法/不支持记录会“毒死”其余 13 条**——对有效性是优点，对异构历史日志覆盖是硬缺口。
4. **无每字节/chunk 时序**：`ReplayTransactionRecord{elapsed, requestWire, responseWire?}` 就是全部（`ReplayLog.h:17-23`）——v1 “本来就没有” timing/event 表达能力，不是缺陷而是模型边界。
5. 无 per-record override：timeout 是文件级单一阈值（`ReplayLog.h:27-32`）。

## 10. Four/Five-way Coverage Classification（每个场景的最终归类）

| 类 | 定义 | 命中的场景 | 汇总 |
| --- | --- | --- | --- |
| A. SUPPORTED / DETERMINISTICALLY DIAGNOSABLE | 系统已有足够事实且给出可靠确定性分类 | S1（协议层，修正后）、S7（修正后）、S8、S12、S14 | 5/14（其中 S1/S7 需修 fixture CRC） |
| B. SYMPTOM DETECTABLE / ROOT CAUSE NOT PROVABLE | 能看到异常结果，但注释中的物理原因不能被唯一证明 | S9、S10、S13（均为 CrcError；另 S12/S14 的“物因”层面也属 B） | 3/14（主类） |
| C. CURRENTLY UNSUPPORTED / MODEL OR FORMAT GAP | parser/协议模型/回放模型无法表达或分析 | S2、S3、S4、S5、S6、S11 | 6/14 |
| D. UNPROVABLE FROM CURRENT OBSERVATIONS | 即使写更多 rule、仅凭当前保存 facts 也无法唯一确定 | S1（应用语义）、S11（gap 发生过）、S13（冲突身份） | 3/14（叠加） |
| E. FIXTURE DEFECT | “本应合法”的场景数据本身不合法（CRC 写错等） | S1resp、S2、S3、S4req、S5resp、S6、S7resp | 7/14 场景含 CRC 缺陷（共 9 条 wire；S9/S10/S13 的 resp 属故意畸形，不计入 E） |

组合例：S6 = **C + E**；S13 = **B + D**。故意畸形（S9/S10/S12/S13/S14 的“负向内容”）与 fixture defect（E）严格区分：前者字节故意坏是场景本意，后者是正常场景写错。

## 11. Current Diagnostic Coverage Families（按架构家族，不只 14 条）

| 家族 | 当前 observation | 当前 deterministic classifier | 当前 diagnosis | 当前 limitation |
| --- | --- | --- | --- | --- |
| Request issues | 仅 FC03：wire decode→FC 门→语义校验三关（`ReplayAnalysis.cpp:18-36`；Serial 侧 InvalidAddress/InvalidQuantity，`SerialTransactionSession.h:33-38`） | InvalidRequestWire / InvalidRequestFunction / InvalidRequestData（replay）；Busy/InvalidAddress/InvalidQuantity/NotActive（serial） | replay 是加载失败而非诊断事实 | 非 FC03 请求一律 `InvalidRequestFunction`，**无“invalid request + device exception”组合表达** |
| Modbus Exception responses | 任意 FC03 事务：`exceptionCode`（uint8，任意值）保存（`TransactionAnalysis.h:35`） | 单帧 0x83 且地址/形状合法 → Exception | 0x01→CheckFunctionSupport；0x02→CheckRegisterMap；0x03→CheckRequestParameters；0x04→CheckDeviceHealth；其他→CheckDeviceDocumentation（`RuleBasedDiagnosis.cpp:11-20`） | 只有 FC03 请求能到达异常分类；0x06+ 等 code 无专有知识（只有 fallback） |
| Response wire integrity | CrcMismatch / FrameTooShort（`ModbusRtuCodec.h:12-15`） | CrcMismatch→CrcError；FrameTooShort→ProtocolError（`TransactionAnalysis.cpp:40-47`） | CrcErrorObserved + settings/wiring/noise 三检查 | 无破坏形态细节（§13）；decoder 无 resync（S9） |
| Request/response semantic consistency | 地址、功能码、数量跨帧校验（`TransactionAnalysis.cpp:53-97`） | 全归一为 **ProtocolError** | ProtocolErrorObserved + InspectProtocolConsistency/CheckDeviceDocumentation | **subtype 丢失**（§13）是五分支归一的结果 |
| No response / timeout | `NoResponse` 观测 + elapsed 阈值（`TransactionAnalysis.cpp:30-35`） | elapsed < threshold → Pending；≥ → Timeout（边界实测 999/1000） | TimeoutObserved + power/address/settings/wiring | “谁没回答/为什么没回答”不可知 |
| Expected no response | **无观测模型**（mlog v1 无该 token；T007 无广播口径，`T007:35` 推迟） | 无；地址 0 + NO_RESPONSE 会被当普通等待（合成探针实测：e=0→Pending、e=1000→**Timeout**） | 无 | 正常广播会被误读为 Pending/Timeout（§18） |
| RTU framing/timing | replay：完全无 per-byte 时间；serial：transaction-aware expected-length framing（`SerialTransactionSession.h:107-111`），明确不做 t1.5/t3.5 scanner（`T010:28/34-36`） | 无 timing 分类 | 无 | S11 字节拼接后 Success——timing 证据缺席 |
| UART errors | `QSerialPort::errorString()` 一次性的通用文本（`SerialPortAdapter.cpp:155-177`） | 无结构化 parity/framing/break/overrun fact；transport error ≠ Modbus status（设计优点 §17） | 无（只报文本+中止事务） | 现场硬件问题只能落成 CrcError/Timeout/transport error |
| RS-485 physical layer | 无观测 | 无 | 只出现在 CRC/Timeout 的候选检查清单里 | 偏置电阻/悬浮/端接等是 hypothesis 不是 observation |
| Configuration faults | 无直接观测（波特率/校验/地址是发起参数） | 无 | 作为检查建议（CheckSerialSettings/CheckSlaveAddress） | 无法区分“硬件坏”与“配置错” |
| Application/register semantic faults | FC03 响应寄存器值在 decode 时瞬态可读（`Function03.h:24-29`） | 仅数量一致性（`TransactionAnalysis.cpp:88-91`） | 无 | 值不保留；float/单位/范围/传感器语义不存在（§15） |
| Time-window/trend faults | 每 batch 聚合统计（`summarizeTransactions`） | 四不变量 + rate/latency | 无 per-device 视图 | 无 per-device/time-window 聚合、无趋势（§22） |

### What Is Deterministically Known（汇总）

CRC 真值、六状态归一化、异常码数值、统计与不变量、跨帧一致性（地址/功能/数量）、NoResponse+阈值语义——全部由 Core 唯一决定（与 README:33 “AI is interpreter, not detector” 一致）。

### What Is Only a Symptom（汇总）

CrcError（值的只是“收到的字节没过 CRC”）、Timeout（“阈值内没收到字节”）、ProtocolError（“收到数据但不匹配”）、Exception code（“设备这样告了的数值”）。

### What Cannot Be Proven（汇总，仅凭当前保存 facts）

EMI/线缆/波特率/捕获损坏/软件 corruption 的具体分类；设备掉电/断线/忙碌的具体哪条；地址冲突；DE/RE 时序；总线悬浮；传感器损坏；寄存器语义错误。**以上全部都只能给 troubleshooting directions。**

## 12. Transaction Status vs Device Health（正式结论）

- 当前产品**没有** Device Health 概念：无 health score、无 Healthy/Unhealthy 设备判定（T011 明确不建 0~100 分；`RuleBasedDiagnosis` 的 `Healthy` 是“本 batch 全部成功且无在途”的聚合 finding，不是设备健康认证）。✅ 设计正确。
- 但 **UI 术语（“设备/状态”）会把 TransactionStatus 包装成设备属性**（§4），这是用户误读的主入口。
- 未来若需要“设备条件”，正确方向是 **per-device aggregation / time-window view**（§22），而不是把六状态膨胀成设备健康维度。

## 13. Information Lost by Current Models（信息损失盘点）

### 13.1 ProtocolError Detail Loss（五个真实分支归一为一个 status）

`analyzeFunction03Transaction` 中能产生 `ProtocolError` 的**全部真实分支**（`TransactionAnalysis.cpp`）：

1. Response 解码 FrameTooShort（:44-46）
2. Response 地址 ≠ Request 地址（:53-56）
3. 0x83 异常形状解码失败（:60-64）
4. FC03 normal 双臂解码失败 / 数量不一致（:77-91）
5. 任何其他功能码（0x04/0x06/0x84...）回应 0x03 请求（:95-97）

`TransactionAnalysis{status, elapsed, exceptionCode?}` **不保存任何 subtype**；`DiagnosisContext` 同样只有 status（`DiagnosisContext.h:17-23`）。**结论：Analyzer 判定阶段曾经知道具体 failure reason（AddressMismatch / FunctionMismatch / QuantityMismatch / 形状问题…），normalize 之后 Diagnosis/Agent/UI 只看到 `ProtocolError`。** Baseline 只能给 `ProtocolErrorObserved`，无法告诉用户是哪一种 mismatch。T011 明确“不凭空猜 subtype”（`T011:138`）——正确但意味着信息在 T007→Diagnosis 边界被有意识地丢弃。

未来推荐：**保留六种 high-level status 不动**，另加 `TransactionIssue / ObservationDetail / ProtocolErrorReason` 侧信道（只增不改，符合 T007“不改状态枚举”的档案纪律），而不是把 TransactionStatus 膨胀成几十个 enum。

### 13.2 CRC Detail Loss

`CrcError` 只留下 `status=CrcError`（+elapsed）；不保留破坏形态（前导垃圾/截断/翻转/捕获损坏）与 raw decode 上下文。**但要诚实**：有些 detail 从 wire 本身也未必能唯一获得——设计 subtype 不能凭空造出物理原因，只能保存“可识别形态”（例如前导垃圾若未来实现了 resync 报告、截断可由长度启发式标记），必须与“物因推断”分成两层。

### 13.3 Response 寄存器值 Loss（与 §15 联动）

`decodeReadHoldingRegistersResponse` 产出 `values`（`Function03.h:24-29`），但校验数量一致后即被丢弃（`TransactionAnalysis.cpp:83-92`；T007 定案不保留 `returnedRegisterCount`，`T007:104`）。这是“应用语义层彻底缺席”的根因之一。

## 14. Exception Coverage Audit（存储能力 vs 诊断知识能力）

**两个独立概念**：

| 概念 | 当前事实 |
| --- | --- |
| Exception **Storage** Coverage | 任意 `exceptionCode`（uint8）都能存（`TransactionAnalysis.h:35`）；实测 0x04（S7 修正后）、0x06（S8 as-is）均正确落入 `Exception` 状态 |
| Exception **Diagnostic Knowledge** Coverage | 0x01→CheckFunctionSupport、0x02→CheckRegisterMap、0x03→CheckRequestParameters、0x04→CheckDeviceHealth、**其余→CheckDeviceDocumentation**（`RuleBasedDiagnosis.cpp:11-20`）；Agent 工具 `standardExceptionName` 同样仅 0x01~0x04（`AgentTools.h:143-145`） |

“能保存 exceptionCode” ≠ “有专业诊断规则”。0x06 在 demo_v2 中是唯一全对场景，正好实证了这条边界。标准 Modbus 全表（0x05~0x0B 等）如需知识化 → **Protocol Reference / Future Knowledge Expansion**，本阶段不替项目添加未实现映射。

## 15. Application Data Semantics Gap（Scenario 1 案例）

- S1 修正后：`01 03 04 41 A0 00 00` → 两个寄存器值 `0x41A0 / 0x0000`，数量一致 → **Success**。
- Core 看到的只是 **two 16-bit register values**；它不知道（也不声称知道）：是否组成 IEEE-754 float32、word order、byte order、工程单位、合法值域、传感器含义。
- **Protocol Success ≠ Business Data Validity**：CRC 对 + FC03 对 + 值解析正常，但“温度 = 不可能值”的场合，当前系统依然 Success。
- 这属于 **future domain/register-map semantic layer**（<Concept> 而非 bug）。需要用户在决定产品方向时知情。

## 16. Serial / UART Observation Audit（从 T010 真实 API 核验）

- `SerialTransactionSession` 的完整输入输出面（`SerialTransactionSession.h`）：`begin/fedResponseBytes/onResponseTimeout/cancel` + state + 4 个 session 错误；事实 = 字节 chunk + caller 提供的 elapsed。**无** parity/framing/break/overrun 结构、**无** chunk/byte 时间戳。
- Adapter（`SerialPortAdapter.h/.cpp`）：只有 QSerialPort + QTimer + QElapsedTimer；端口错误仅 `port_.errorString()` 一次性通用文本，且 PE-4 后**有界报告**、不伪造 TransactionStatus（`SerialPortAdapter.cpp:155-177`）。
- **结论**：Current Serial Mode ≈ byte chunks + timeout/runtime facts 的 **active master 事务运行时**，**不是 UART diagnostic recorder**。这解释了为什么真实现场硬件问题最终只能落成 CrcError / Timeout / transport error 三类。
- 引用已有档案定案：v1 明确禁止 t1.5/t3.5 generic framer、passive sniffer（`T010:28`）；Windows/QTimer 毫秒级不适合伪装 gap detector（`T010:34-36`）；“不追求微秒级 wire timing”（`T010:63`）。

## 17. Transaction vs Transport Error（现有设计优点，勿打破）

当前已正确分开（`T010:56-59/308/497`；代码 `SerialPortAdapter.cpp:155-177`、`serialAdapter_` 信号路径）：

| 事件 | 归宿 |
| --- | --- |
| 请求已上总线、阈值内无字节 | `TransactionStatus::Timeout`（合法 Modbus 诊断） |
| 收到部分字节但坏 | CrcError / ProtocolError（wire truth，绝不算 Timeout） |
| cannot open COM / USB 适配器缺失 / 权限 / write 失败 | **transport error 文本**，事务 cancel，**不伪造任何 TransactionStatus** |

审计结论：这是**架构优点**。未来任何覆盖扩展都不得为“扩大覆盖”而把 transport 失败塞进 TransactionStatus（§33 非目标相应固化）。

## 18. Broadcast / Expected-No-Response Gap（Scenario 4）

- `BROADCAST_NO_RX` 不是 v1 合法 token（实测：文件在 L15 以 InvalidHex 失败）；mlog v1 唯一 no-response 表达是 `NO_RESPONSE`。
- 即使把 S4 改成 `NO_RESPONSE`：请求 FC06 先被 `InvalidRequestFunction` 挡住（实测）；**合成 FC03 广播探针**（地址 0、请求 CRC 正确 `... D4 1B`）实测：`e=0 → Pending`、`e=999 → Pending`、`e=1000 → Timeout`。
- **语义错误确认**：当前 T007 只有 `NoResponse + elapsed ≥ threshold → Timeout` 一条路径，**“广播本就不应有响应”会被误归为 Pending（等待中）或 Timeout（超时）**——正常 Broadcast 没有 Response 不能算设备 Timeout。
- 未来最小模型候选（本阶段只设计不实现）：
  - a) `ExpectedNoResponse` 观测类别；或
  - b) `TransactionKind / RequestMode = {Unicast, Broadcast}`（地址 0 ⇒ 期望无响应、不计超时）；或
  - c) mlog v2 `BROADCAST_NO_RX`-style token 由 Record 显式声明 kind。
  - 附带注意：地址 0 只对写类功能码有意义（`03_MODBUS_LEARNING.md:38`），未来模型应把 broadcast 校验锚在功能码上。

## 19. Invalid-Request Gap（S5/S6 —— 本次最重要的架构审计点之一）

现状（`ReplayAnalysis.cpp:18-36` + T009 定案）：request 可信链 = wire decode → FC==0x03 → FC03 语义合法，**任一失败即整条记录（乃至整文件 batch）失败**：

- S5：请求 CRC 本已合法，仅 FC=0x08 → `InvalidRequestFunction`，**合法的 Exception 0x01 响应被一并丢弃**。
- S6：quantity=126（超 1..125，`Function03.cpp:9-10`）→ `InvalidRequestData`，**设备合法拒答 Exception 0x03 的历史事实不可见**。

评估：

| 场景 | Request trust contract 的适用性 |
| --- | --- |
| **active master mode**（Simulator/Serial 自己发的请求） | 很合理——请求是自己构造并已通过 encode 校验的，信任链成立 |
| **passive replay / historical traffic diagnosis** | **太窄**——历史流量里“客户端发错请求 + 设备合法返回 Exception”正是真实诊断对象；当前模型整个丢弃这笔历史事务 |

未来表达（只审计不实现）：**Request Validity ≠ Observed Transaction Outcome**。候选：`RequestIssue::InvalidQuantity` + `Response::Exception(0x03)` 并存的一笔历史 transaction（例如 `RequestValid=false` 标注 + 响应照常分类），而不是当执行错误丢弃；对 Simulator/Serial 生产路径保持现有信任链不变。**不得**为兼容它去放松 active 路径的校验，也不得修改 T007（本轮不实现任何东西）。

## 20. Timing / Event Gap（Scenario 11 + 术语纠正）

- 实测：S11 原行 5 字段 → `InvalidRecord`；若把两片**拼接**（`01 03 04 00 64 00 C8 BA 7A`，CRC 实测合法）→ **Success**。**字节层面完美、时间层面为零**：Replay v1 “即使最终 bytes 拼起来完全合法，也无法知道中间发生过异常 gap”——因为 v1 根本不保存 per-byte/chunk 时间戳与 t1.5/t3.5 事件（§9.4）。
- **注释术语需要纠正（不修改 sample 文件，仅记录）**：demo_v2 S11 注释写“字符间隙 > 3.5t”。按项目自有协议资料（`03_MODBUS_LEARNING.md:39-41`）：**帧间**必须 ≥3.5t 静默（否则两帧粘连）；**帧内**静默 >1.5t 视为**帧中止**（且注明“部分设备容忍度不同”）。因此帧内断帧的判定阈值表述应为 **t1.5**；t3.5 是帧间分离量。→ Audit 标记：`demo_v2 comment terminology needs correction`（t3.5→t1.5 语境）。
- 系统性结论：**帧内 gap/断帧在 v1 中不可观察（C），且即使有更多 rule 也测不到（D）**——需要 Replay v2 event trace 才能覆盖。

## 21. Function-Code Coverage Gap（S2/S3 —— 被动分析 vs 主动写权限）

- 当前 v1：非 FC03 request → `InvalidRequestFunction`，**不支持 FC06、不支持 FC10**（`ReplayAnalysis.cpp:28-30`；README:93；`07_FINAL_PROJECT_REVIEW.md:241` 均如实声明）。
- 必须区分的两个能力（本审计核心边界）：

| 能力 | 含义 | 与写权限的关系 |
| --- | --- | --- |
| A. Passive / Replay analysis support（理解历史 FC06/FC10 的语义） | 只读解析历史 write 事务并给出诊断事实 | **不涉及任何下发** |
| B. Active Serial write capability（产品发写请求） | 产生新写操作 | 涉及设备控制，与 Agent 写禁令同类世界观 |

- 未来可以考虑 A（Replay 能分析历史 FC06/FC10）**同时**产品依然：不提供写寄存器按钮、不主动发写请求、Agent 永无写工具。“支持分析 FC06” ≠ “允许 Agent/UI 写设备”。本阶段只做 scope recommendation。

## 22. Device / Time-Window Future Model（讨论；不实现）

用户发现的“同设备多结果”问题（§2）的产品级解法候选——**DeviceObservationSummary**（或等价名字）：

```text
device address = 1
  transactions = 100
  success = 92, exception = 2, crc = 3, timeout = 3
  recent anomaly types = [Exception(0x02), CrcError, Timeout]
  window = 当前 batch / 最近 N 笔 / 最近 T 分钟
```

设计红线（同理 T011 纪律）：**不要 health score、不要 0~100、不要自动判 Healthy/Unhealthy**，除非未来有明确定义与证据。当前只提出 per-device aggregation / time-window view 用于解决“Transaction Status vs Device Condition”的用户理解问题（与 §4 UI 术语建议配套）。优先级见 §24 P2。

## 23. Three Product Scope Options（三种产品方向）

| | Option A | Option B | Option C |
| --- | --- | --- | --- |
| 定位 | **保持 FC03 专项诊断示范器** | **扩展被动/回放诊断面**（推荐候选） | 全功能工业 Modbus 分析器 |
| 内容 | 现状 + 术语澄清 + 信息损失最小修复 | A + 被动 FC06/FC10/选择 FCs + invalid-request observations + broadcast semantics + 更丰富 issue detail；**Active Serial 仍只做读 FC03** | B + event trace/timing/UART errors/physical evidence/多 FCs/register maps/domain semantics/history/trends |
| 优点 | scope 小、架构成熟、秋招可控 | 诊断平台更真实、**不扩大设备控制权限**、成本可控 | 覆盖真现场 |
| 缺点/风险 | “通用 Modbus RTU 诊断平台”的命名可能显得覆盖过宽（§26） | 需要 mlog 扩展 + replay 模型扩展两个任务 | **已远超秋招项目合理 scope，不推荐** |

## 24. Recommended Minimal Next Scope（只建议，不实施；由证据驱动）

| 优先级 | 项 | 证据（为什么排这里） | 备注 |
| --- | --- | --- | --- |
| **P0** | validated diagnostic fixture | demo_v2 有 11 条 CRC 缺陷 → 任何使用它的演示/测试都会先被 fixture noise 污染；需求先把“场景意图”固化成合法 fixture 或分文件 | **需先提方案等用户批准**（§0 硬规则）；不建议一味追求 14 条全进 v1 |
| **P0** | 明确“TransactionStatus ≠ DeviceHealth”的用户可见表述 | §4 UI 术语（设备→设备地址、状态→事务结果）是误读主入口，改动小收益大 | 属 UI/文案变更，需要脱离 docs-only 阶段授权 |
| **P0** | ProtocolError detail preservation（`TransactionIssue/ObservationDetail` 侧信道） | §13.1 五个真实分支归一丢信息，Baseline 只能“协议错误”一句；不改六状态、不动 T007 枚举 | 证据：T011:138“不猜 subtype”的代价被 demo_v2 放大 |
| **P1** | invalid request 作为可观察历史事实 | S6 是本次最重要架构案例（设备合法 Exception 被整笔丢弃）；§19 | 仅 Replay/被动路径；active 信任链不动 |
| **P1** | broadcast expected-no-response 语义 | S4 合成探针实测被误归 Pending/Timeout；§18 | 模型候选已列 |
| **P1** | Replay v2 event/timing 设计 | S11 字节拼接=Success，timing evidence 为零；§20 | 先设计格式，再谈实现 |
| **P1/P2** | 被动 FC06/FC10 语义解码（无主动写权限） | S2/S3；§21 两能力分离 | 与 Agent 只读边界同源 |
| **P2** | per-device / time-window aggregation | §22；同设备多 outcome 的用户理解问题 | 红线：不建健康分 |
| **P2/P3** | UART error facts；register-map/application semantic layer | §16/§15 | 后者依赖用户提供寄存器表口径 |

**排序理由（成本/收益）**：P0 三项都“不扩产品边界、只补真实信息的表达”，成本小且直击用户五个疑点；P1 是“被动诊断平台”的最小可信扩展；P2+ 是长期方向，秋招窗口内可缓。

## 25. Explicit Non-goals（本轮及未来配套的非目标）

SCADA；full PLC control；FC06/FC10 active write；device auto-repair；Agent actuation（写工具）；automatic retry（未经设计）；database；cloud telemetry；RAG；MCP；multi-agent；health score（0~100）；oscilloscope replacement；universal physical root-cause detector。**特别**：被动分析 FC06 ≠ 主动写（§21）。

## 26. Claim Risk（口径风险，本阶段不动这些文档）

**现状措辞**（verbatim）：

- `README.md:1`：`# ModbusLens — 工业通信智能诊断平台`
- `README.md:3`：`“…用于工业现场 Modbus RTU 通信监听、分析与诊断 的 C++20 / Qt6 桌面软件…”`
- `docs/00_PROJECT_CHARTER.md:14`：`“构建一个 C++20 + Qt6 的 Modbus 通信智能诊断平台…”`
- `docs/07_FINAL_PROJECT_REVIEW.md:10`：`“是一个 C++20 + Qt6（Quick/QML）的工业 Modbus RTU 诊断平台…”`
- `INTERVIEW_NOTES.md:8`、`AGENTS.md:7` 同族。

**风险**：“智能诊断平台/诊断平台”在没有伴随限定词的情况下，容易被面试官/观众理解为**全功能码、全物理层、全根因覆盖**——而本审计证明的真实覆盖是 FC03-centric、事务级、五类状态归一并带三处硬缺口（FC06/10、broadcast、timing）。`01_REQUIREMENTS.md:53/61` 的回放控制与 t3.5 帧切分条目、`02_ARCHITECTURE.md:28/84` 的 Serial t3.5 表述、`04_TEST_STRATEGY.md:52` 的“虚拟时钟精确控制 t3.5”也描述的是**规划中**而非已交付能力（T005/T010 档案已推迟）——同属 Claim Risk 家族，本审计只记录不改。

**未来措辞 proposal（只提议）**：`FC03-centric、transaction-level、deterministic diagnostics，支持 Simulator/Replay/Serial 三数据源与可选只读 AI 解释层`；中文候选：`面向 FC03 读保持寄存器的、事务级确定性诊断平台（三数据源 + 可选只读 AI 解释）`。是否采纳由用户决定，属于后续（非 docs-only）阶段。

## 27. UNKNOWN / Evidence Classification（证据分级纪律）

| 类 | 含义 | 本审计用例 |
| --- | --- | --- |
| Known from current code | 从当前工作区源码/头文件逐行核验 | 六状态、mlog 4 字段、FC03 1..125、ProtocolError 五分支、exception 映射 |
| Known from automated tests | 仓库测试档案/ctest 记录 | T007/T009/T010/T011/T012 各矩阵；demo_v1 golden 4/4/0·1/1/1/1/0 |
| Known from demo_v2 bytes | 只读读取文件本身 | 45 逻辑行（44 个换行符，末行无 trailing newline）、14 TXN 记录、5 字段违规（S11） |
| Known from CRC calculation | 本审计独立计算（KAT 锚定，PASS×2） | §6 的 20 条字节序列结论（19 raw wire + 1 derived） |
| Known from project docs | 文档原文引用 | T009 mlog v1 契约、T010 禁 t1.5/t3.5 scanner、T011 不宣称 root cause |
| Known from protocol reference | 项目协议知识库 | 帧间 ≥3.5t / 帧内 >1.5t 帧中止（`03_MODBUS_LEARNING.md:39-41`）、广播只允许写类 FC |
| Inferred possible cause | 规则引擎的检查建议 | power/address/settings/wiring/noise/grounding 等 action |
| Scenario author intention | demo_v2 注释声称的场景本意 | “偏置电阻缺失”“DE/RE 切换过早”“地址冲突”“掉电断线” |
| Hypothesis only | 未被证据支持的原因候选 | S9 的任何具体物因 |
| Not observable with current model | 当前模型不可能保存的 fact | per-byte 时序、UART 物理错误、设备内部状态 |
| Not currently supported | 明确不在 v1 范围 | FC06/10 分析、BROADCAST token、FRAME_FRAGMENT |

**特别禁止**：demo 注释写“偏置电阻缺失”/“DE/RE 切换过早”，**不等于**wire 证明了该原因——它们只是 scenario intention；同理“地址冲突”“设备掉电”都不能记为 proven root cause。审计全文对此逐场景区分（§8“不能确定的事实”列）。

## 28. Decision Questions for User（少量、真正需要用户决定的）

1. **产品定位**：继续 FC03 专项（Option A），还是扩展 passive multi-function diagnosis（Option B）？
2. **写功能边界**：是否接受“Active Serial 永远保持 read-only，但 Replay/Passive Analyzer 可以理解历史 write 功能码”？
3. **修复优先级**：下一阶段是否优先修诊断“信息损失”（ProtocolError detail / 术语澄清 / validated fixture），而不是优先堆更多 Function Code？
4. **Replay v2**：是否值得设计 Replay v2 event trace（覆盖 framing/timing）？它会把“诊断平台更接近现场”，但会引入格式版本与采集/回放両侧工作。
5. **per-device view**：是否需要 per-device / time-window 视图来减少“同设备四种状态”的 UI 歧义？

以上不替用户决定。任何一项获批才开始对应阶段；本阶段到此为止。

## 29. Verification（本审计自身的验证记录）

- 独立 CRC：KAT1 `123456789→0x4B37` PASS、KAT2 `01 03 00 00 00 01→0x0A84` PASS；20 条字节序列（19 raw wire + 1 derived）结论见 §6，与外部预审候选 11 项全部吻合（另补算 8 条预审未覆盖序列：7 raw wire + 1 derived）。
- 真实代码 harness（build/ 内一次性程序，链接 `build/debug/libmodbuslens_core.a`，gitignored，不进入提交）：
  - 整文件 `parseReplayLog` → `PARSE-ERROR InvalidHex line=15`；
  - 单行隔离解析 14/14（12 行 PARSE-OK、L15 InvalidHex、L36 InvalidRecord）；
  - 场景探针（as-is 与 CRC 修正后）全套结果见 §7；边界探针 e=999→Pending、e=1000→Timeout；合成 FC03 广播 Pending/Timeout；S11 拼接→Success。
- 本阶段仓库改动 = 仅新增本文件；生产/测试/CMake/QML/scripts 零修改；`samples/demo_v2.mlog` 未提交、未修改、未删除（证据 fixture 规则 §0）。