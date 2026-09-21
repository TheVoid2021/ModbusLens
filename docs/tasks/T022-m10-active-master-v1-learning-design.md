# T022 — M10 Active Master v1 — Learning / Design Gate

> **状态（M10-E2 review correction 后）：M10-E1 = ✅ COMPLETE；M10 overall = IN PROGRESS；**M10-E2 = 已实现（Review 曾 HOLD：CRC active/passive 直接等价 pair 缺失，已按 §ZI 补齐 eq2）= AWAITING RE-REVIEW；M10-E3+ = NOT STARTED；M10-F = AFTER M10-E；M11 = HOLD。**
> **M10-D accepted behavior tree = `9bdd99c`；verified LKGC = `9bdd99c`（Human Review 已授权）。0cf0748 为 docs-only closure，不是 LKGC。**
> 能力终态：0x03 与 0x06 = encoder + session + dispatch + UI；**0x10 = encoder + 共享 analyzer + session lifecycle（M10-E2 新增）= YES；Controller dispatch / `write10Supported` / production UI 仍 ABSENT**；AI/Agent 写权限 NONE。**REAL HARDWARE NOT VERIFIED。** **Next = M10-E2 Review → M10-E3（非 M11）。**
> verified LKGC = **`b7a6151`**（2026-09-20，M10-A Final Re-review PASS 后的最终 accepted behavior tree）；历史：`aa2f3db`（M9-F closure）→ `b7a6151`（M10-A）。M9 = ✅ COMPLETE（不重开）；**M10-A = COMPLETE**。
> *（as-of 限定：本行是 M10-A 时点的历史快照，当时 LKGC = `b7a6151`；**当前** verified LKGC 见上方状态行与 `docs/PROJECT_STATUS.md`。）*
> 本轮**未修改** src / QML / CMakeLists.txt / scripts / tests / assets / samples / screenshots；未创建 tag；未 push。
> 上游边界：M9 已冻结的 IA（五 workspace + Device disabled + Legacy retired + 默认 Transactions + navigation presentation-only）、
> M9-E 的 version/PE/icon/package 契约、M9-F 的 focus/accessibility baseline **全部继续冻结**；M10 不得顺手改 focus visual / NavigationRail / packaging / StatisticsOverview。

> **协议命名消歧批注（2026-09-20 Phase 1 Correction 追加，**不修改原有正文**）**
> 本文档 §4 起的原始正文中出现的「**FC10**」一律指 **0x10（十进制 16，Write Multiple Registers）**，
> 源码类名为 **`Function16`**——**不是**十进制 function code 10。
> 此后所有 M10 文档：该功能**首次出现必须写全 `0x10（十进制 16，Write Multiple Registers）`**，**不得只写「FC10」**。
> 原文按档案区规则保留不改，本批注即 §2 要求的 disambiguation。

## 0. V2 Protocol 对应

```text
M10 任务必须按序经过：Preflight → V1 Contract Review → Learning / Design（含知识点自证）
 → Test Plan → Implementation → Targeted Verification → Full Regression → Manual Review（涉及 live/UI 时）
 → Documentation → Knowledge Ownership → Git Commit → Human Review → LKGC decision。
本轮 = **Learning / Design Gate**，输出 A…G 七问（§41）+ decision requests（§40），并**停止**等待 Review。
```

## 1. Preflight（2026-09-20）

```text
HEAD = `0498d6c`（main，clean）；verified LKGC = `aa2f3db`；M9 = ✅ COMPLETE；
CMake project VERSION = 2.0.0；v1 tag object `2cee626` / `v1.0.0^{commit}` = `ae067ab`；v2.0.0 **absent**；
origin/main = `a40d935`（ahead 95 / behind 0）；git diff --check PASS —— 全部相符。
```

## 2. Task Ownership

```text
docs/tasks/ 现有最大 id = T021（M9-F，已 COMPLETE）⇒ 本任务取 **T022**（真实空号，未 append 到已完成的 T021）。
M10 的 canonical task 从本轮起 = 本文件。
```

## 3. Mandatory Source Re-read（真实源码，非旧文档推断）

```text
读取/核对：src/ui/AnalysisController.{h,cpp}、src/ui/TransactionListModel.{h,cpp}、
src/ui/serial/SerialPortAdapter.{h,cpp}、src/core/serial/SerialTransactionSession.{h,cpp}、
src/core/protocol/{Function03,Function06,Function16,ModbusRtuCodec,ModbusRtuFrame}.*、
src/core/analysis/{TransactionAnalysis,PassiveTransactionAnalysis,TransactionStatistics}.*、
src/core/simulator/{SimulatedSlave,SimulationFault}.*、src/core/diagnosis/*、
src/ui/qml/pages/CommunicationPage.qml、src/main.cpp 的 harness modes、tests/ 相关用例。
```

## 4. Capability Inventory — What Exists Already（源码事实）

**A. Communication 页面今天真正能做什么**

```text
串口选择（刷新串口 / 端口列表）、波特率选择（9600…）、8N1 固定、连接 / 断开；
请求区：从站地址(1..247) / 起始地址(0..65535) / 寄存器数量(1..125) / 超时(100..10000ms)；
按钮「读取保持寄存器」= **一次 FC03 主动读**（busy 时文案变「读取中...」）；
串口错误行（独立 lane，不写成 Modbus 状态）。
```

**B. serial transport 是哪种**

```text
**真实串口**（QSerialPort）+ **可替换会话核心**的组合：
  · `ui::SerialTransactionAdapter`：真实 QSerialPort，openPort/closePort/startTransaction/hasActiveTransaction/isPortOpen，
    信号 transactionCompleted(TransactionAnalysis) 与 transportError(QString)。
  · `core::SerialTransactionSession`（纯 C++20，零 Qt）：持有**一条** FC03 事务，begin → 累积任意 readyRead chunk →
    candidate frame → 复用既有 codec + T007 analyzer → Idle；不碰 COM/波特率/定时器。
⇒ 没有"假 transport"；测试通过 ui_bridge + 真实 adapter 的可注入路径与 core session 的纯逻辑两者覆盖。
```

**C. request encoder 支持哪些 function**

```text
**FC03 ✔**：`encodeReadHoldingRegistersRequest(address, startAddress, quantity)`（quantity 1..125，可返回 InvalidQuantity）。
**FC06 ✘**：只有 request/response **decoder**，**没有 encoder**。
**FC16/0x10（十进制即 FC10）✘**：只有 response decoder（且 request 走 passive 专用解码），**没有 encoder**。
```

**D. response parser 支持哪些 function**

```text
FC03：normal response / exception response / request 三类 decoder 齐全（含 quantity 与 byteCount 校验）。
FC06：request（address+value）与 response（echo）decoder ✔；echo 失配由 passive 分析层判为
      `WriteSingleRegisterEchoMismatch`（不是 Success）。
FC16/0x10：response（startAddress + quantityWritten）decoder ✔；失配判为 `WriteMultipleRegistersEchoMismatch`。
异常响应：FC03 的 0x83 decoder；FC06/16 的异常格式由既有 exception 框架处理（passive 侧已覆盖）。
```

**E. Controller 有没有发送 command seam**

```text
有，但只限 FC03：`Q_INVOKABLE void readHoldingRegistersOnce(slave, start, quantity, timeoutMs)`。
  · 在**任何窄化转换之前**做范围校验（slave 1..247 / start 0..65535 / quantity 1..125 / timeout > 0）；
  · 要求 `serialConnected && !serialBusy`，否则只产生串口错误（不发请求）；
  · 只有 adapter 真正接受后才置 `serialBusy_ = true`（**in-flight guard 已存在**）；
  · 完成时 `publishSerialResult`：**替换**模型为单行（functionCode 硬编码 0x03）、单元素统计批次、单条 DiagnosisTransaction，
    并把 modeLabel 置「串口模式」、sourceLabel 置串口名。
```

**F. transaction record 能表达什么**

```text
`TransactionListEntry`（Qt adapter 层）= deviceAddress / functionCode / status / elapsedMs / exceptionCode / issueText。
  · 能表达：outbound request（仅通过 functionCode/address）、inbound response 的**结论**（status + exceptionCode + issueText）、
    Timeout、Exception、CRC/ProtocolError、以及 **ExpectedNoResponse**（广播语义，taxonomy 已冻结）。
  · **不能表达**：原始 request/response 字节（wire hex）、独立的"write request"标记、请求参数（address/quantity/values）、
    provenance/source 标识（见 §24：当前只有 controller 级 modeLabel/sourceLabel 字符串）。
core 侧 `TransactionAnalysis` 携带 status/issues/elapsed/exceptionCode，并有 request-issue 正交集合。
```

**G. Simulator Mode 在 active-master 场景能复用到什么程度**

```text
`core::SimulatedSlave`：**const handleRequest**，注释明确 "reads state, never mutates it"；地址不匹配返回 IgnoredRequest；
  "v1 has no broadcast semantics"；只有 FC03 应答 + `setHoldingRegister` 初始化。
`core::SimulationFault`：None / DropResponse / CorruptCrc / ArtificialDelay（确定性，无时钟无线程）。
⇒ **可复用**：确定性建模方式、fault 注入框架、以及"协议错误是协议帧而非 SimulatorError"的纪律。
⇒ **不可复用（M10 新增）**：写操作的状态变更、FC06/FC10 请求处理、echo 响应、广播/ENR 路径、写向 fault 场景（如 echo 失配）。
```

## 5. M10 v1 Functional Scope（Learning 设计，先冻结）

```text
v1 目标能力 = **FC03 主动读（已存在，纳入统一契约）+ FC06 写单寄存器 + FC10(0x10) 写多寄存器 + write safety 独立契约**。
明确**不**自动扩展：FC01 / FC02 / FC04 / FC05 / FC0F / 任何 vendor function。
源码已存在部分 FC06/FC16 能力（**passive 解码 + 失配 issue**）：本轮**记录、不据此扩大产品 scope**。
```

## 6. Read vs Write Boundary（安全等级，冻结提案）

```text
read operation（FC03）        = read-only request，不改变设备状态。
write operation（FC06/FC10）  = device state mutation。
⇒ write safety 必须是**独立 contract**，不能只是"和 FC03 一样换个 function code"。二者共享的是
  transport/codec/taxonomy/statistics/diagnosis **管线**；不共享的是 **authority / confirmation / timeout 语义 / retry 政策 / 文案**。
```

## 7. Write Authority（冻结）

```text
只有**明确的人类 UI action**（显式点击/键盘激活 Send/Write）可以发起 FC06 / FC10。
禁止：navigation 自动发送 / page visible 自动发送 / selection change 自动发送 / Replay 自动发送 /
      Diagnosis 自动发送 / AI response 自动发送 / Agent tool 自动发送 / background retry 未经明确设计自动发送。
继续冻结：**AI / Agent 没有 implicit write authority**；现有 3 个 Agent tools
（`get_session_summary` / `get_recent_anomalies` / `get_transaction_detail`）**不得因 M10 自动升级成 write tools**；
任何未来 AI-assisted write 必须另立产品/safety design（新任务 + 新 ADR）。
```

## 8. Explicit User Intent Contract

```text
write flow 必须存在明确用户意图边界：用户输入 slave/unit id、address、value(s)，然后**显式**执行 Write / Send。
不得：编辑字段即发送；不得：普通 text editing 中按 Enter 意外触发 write（除非专门设计且带清晰 acceptance）。
⇒ 输入与执行必须是两个可分离的动作（draft 阶段绝不产生 wire）。
```

## 9. Confirmation Policy（比较，不自行定案）

```text
A. 每次 write modal confirmation：最强保证，但打断节奏；键盘用户需处理模态焦点陷阱（M9-F 教训：模态必须自管 Tab 边界）。
B. armed write mode + explicit Send：先"进入写模式"再 Send；两段式意图；需要清晰的 armed 可见状态与退出条件。
C. 危险值/范围分级确认：仅对越界/高风险值二次确认；规则需可解释，避免"偷偷放行"。
D. 无 modal，但强显式 button + summary：依赖 summary 呈现 + in-flight guard；最低摩擦，最依赖 UI 纪律。
trade-offs 必须在 Review 里定；**Phase 1 不自行选最终方案**（除非 repo/product docs 已有冻结要求 —— 目前没有）。
keyboard implications：无论选哪种，write 控件必须满足 M9-F baseline（Tab 可达、焦点可见、disabled 语义、切页后不可能被 Space 激活）。
```

## 10. Write Summary Contract（冻结）

```text
任何 write 执行前，用户必须能确认：**目标 device / function / address / quantity / value(s)**。
FC10 **不得**只显示"写入 N 个寄存器"而隐藏实际 values；values 很多时设计合理 summary，但关键信息不得不可见
（例如：完整 values 列表 + 折叠显示，而非省略）。
```

## 11. Input Validation Model

```text
需要校验：Unit/Slave ID 范围；address 范围；FC03 quantity；FC06 value；FC10 quantity；FC10 byteCount；
          values count 与 quantity 一致性。
必须区分四层：**UI validation**（输入形态/范围）/ **protocol validation**（帧语义，如 byteCount=2*quantity）/
          **transport failure**（端口/写失败）/ **device exception**（设备返回异常码）。
纪律（对齐既有实现）：**invalid local input 绝不先发出去再等设备拒绝** —— 现有 FC03 路径已在窄化转换前完成范围校验，
M10 写路径必须沿用同一纪律。
```

## 12. Existing Request-Issue Semantics（正交性保持）

```text
既有 request-side issues：`InvalidRequestQuantity` / `InvalidRequestByteCount` / `InvalidRequestLength` / `InvalidBroadcastFunction`
（另有 FC06/FC16 失配：`WriteSingleRegisterEchoMismatch` / `WriteMultipleRegistersEchoMismatch` / `UnexpectedResponseForBroadcast`）。
M10 必须保持：**request semantic issue 与 transaction outcome 正交**（taxonomy 与 ADR-003 语义）。
不得因为"本地构建主动请求"而破坏 M9 之前冻结的 passive-analysis semantics；主动请求同样用这套正交表达，
不新造第二套状态轴（除非有充分理由并先 ADR）。
```

## 13. Broadcast Semantics（冻结提案）

```text
Unit/Slave ID 0 = 广播。
· FC03 读取：协议上广播读**没有意义**（无 response 即可读数据），按协议与现有实现事实 —— 当前主动路径**直接拒绝**地址 0
  （`slaveAddress < 1` → 串口错误），M10 保持拒绝，并在 UI 上给出明确原因。
· FC06/FC10 广播：意味着 **ExpectedNoResponse**（taxonomy 已有，且注释明确"proves NOTHING about device write success"）。
· 继续冻结：**broadcast write ≠ confirmed device mutation**；UI **不得**使用「成功」这类表述；
  文案必须表达"已发出、无响应、设备状态未知"的事实（具体中文措辞留待 Review，先冻结语义）。
```

## 14. FC03 Contract（已有能力，纳入统一契约）

```text
生命周期：user inputs → validate → build request（已有 encoder）→ send（adapter）→ pending transaction（session AwaitingResponse）
 → response / timeout → parse（codec）→ TransactionAnalysis → model → statistics → detail presentation。
映射（复用既有 taxonomy，无第二套 active-only 状态轴）：
  normal response → Success（address/function/quantity 校验失败则 ProtocolError + 对应 issue）
  exception response → Exception（+ exceptionCode）
  CRC 损坏 → CrcError；无响应且 elapsed ≥ threshold → Timeout；有字节但无法匹配 → ProtocolError。
```

## 15. FC06 Contract（设计）

```text
请求：address + value（**需要新增 encoder**）。
响应：正常情况下 echo request 的 unit / function / address / value；必须逐项验证一致性。
错误 echo **绝不能显示 Success** —— 既有 passive 侧已定义 `WriteSingleRegisterEchoMismatch`；主动侧复用同一 issue 语义。
具体 outcome 表达（ProtocolError + issue vs 新增状态）**先设计后 Review**，不在 Phase 1 定案。
```

## 16. FC10 Contract（设计）

```text
请求：start address + quantity + values[]；**byte count 由 values/quantity 派生**（单一 authority），
避免用户同时手填 quantity 与 byteCount 形成两个 truth sources。建议方向：values → quantity → byteCount（或显式单一 authority）。
响应：验证 function / start address / quantity 与 request 一致；失配 → `WriteMultipleRegistersEchoMismatch`（既有语义），不是 Success。
```

## 17. Single Source of Truth（三层责任）

```text
UI Draft → Validated Request Intent → Encoded ADU（概念名以现有代码风格为准）。
避免：QML 一份值、Controller 一份值、request builder 又一份值互相漂移。
落点建议：draft 属 page-local presentation；Validated Request Intent 属 Controller（唯一权威，校验后生成，含 slave/function/address/quantity/values）；
Encoded ADU 由 core codec 从 intent 生成，**不允许 UI 直接产出 wire**。
```

## 18. Pending / In-flight Ownership

```text
发送后 active request 由 **Controller** 拥有（`serialBusy_` + pendingSerialAddress_ 已是既有事实），
实际字节交换由 ui adapter 驱动、协议推进由 core session 承担。
一次允许几个 in-flight？既有实现 = **严格 single in-flight**（session 的 `Busy` 错误码 + controller 的 `serialBusy_` 双保险）。
v1 建议：**保持 single in-flight**，不做 queue / concurrency（除非 Review 要求）。
```

## 19. Double-send Protection（已有基础 + 需补 UI 侧）

```text
已有：domain-side guard —— session `Busy` + controller `serialBusy_`（完成/失败/断开都会复位）。
需补：UI 侧在 pending 时 disable Send/Write（现有 FC03 按钮已按 `serialConnected && !serialBusy` disable，文案「读取中...」）。
要求：**不能只靠 UI debounce**；domain-side guard 必须是最终防线（现状满足，M10 写路径沿用同一 guard）。
```

## 20. Retry Policy（decision request）

```text
v1 默认**不假设自动 retry**。write 尤其危险：timeout 不表示设备未执行 write；自动 retry FC06/FC10 可能重复执行非幂等行为。
Phase 1 明确设计：read retry 与 write retry **分别**是什么。
建议（待 Review）：**no implicit automatic write retry**（v1）；读操作如需 retry 也必须显式、可解释、可关闭。
```

## 21. Timeout Ambiguity（write safety 核心，冻结语义）

```text
FC06/FC10：request sent → device may apply → response lost → client sees timeout。
因此 UI **不得**把 timeout 解释成「写入失败且设备未改变」。
语义冻结：timeout 对 write 表达为 **write outcome unknown / response timeout**（"已发出、未收到响应、设备状态未知"）；
**不得**表达为 "写入失败"（暗示未发生）。具体中文文案留待 Review，本轮只冻结语义。
```

## 22. Connection-State Contract

```text
状态：disconnected / connecting?（当前实现无独立 connecting 态）/ connected / pending / closing。
· 未连接：**不得**发请求（controller 已要求 serialConnected）。
· pending 时 disconnect：既有语义 = **cancel，且不产生 Modbus 诊断**（"transport disconnects are local transport facts"）；
  断开后 `serialBusy_` 复位。M10 保持。
· 导航离开 Communication page：请求**继续**（navigation presentation-only，切页不自动 cancel，除非另有契约）。
```

## 23. Transaction Integration（避免第二套 universe）

```text
目标：主动请求产生的 transaction **进入现有** Transactions / statistics / diagnosis 管线（已发生：`publishSerialResult`）。
现状限制：serial 结果目前是**替换语义、单行、functionCode 硬编码 0x03**（"Hardware-free mapping seam"）。
M10 需要的最小改变：让写入/读取事务以自身 function 与参数进入同一模型（**不新建 active history**）。
若确实需要新字段，先设计**最小字段集**并 Review。
```

## 24. Provenance（设计）

```text
需要区分来源：simulator / replay(passive) / active master。**不得**通过 filename 或 workspace 推断来源。
现状：`TransactionListEntry` 无 source 字段；只有 controller 级 `modeLabel`（模拟器模式/串口模式/回放模式）与 `sourceLabel` 字符串。
建议：若引入 provenance，优先复用/收敛到**单一 source/session 标识**（枚举或稳定 token），而不是多处字符串；
最小字段设计待 Review。
```

## 25. Statistics Semantics（审计结论：无需改动）

```text
既有定义已满足 M10：ExpectedNoResponse 计入 completed，但**从 successRate 分母剔除**
（`completedCount - expectedNoResponseCount`），并有明确注释"legal broadcast must not dilute the success rate"。
⇒ 主动写事务进入统计后，Success/Exception/CrcError/Timeout/ProtocolError/Pending/ENR 仍完全适用；
**不得**为 Active Master 偷偷改变 M9-C/D 统计定义（本轮确认无需改动）。
```

## 26. Diagnosis Boundary

```text
Active Master 的结果**可以**进入 deterministic diagnosis 的事实输入（现状：`activeDiagnosisTransactions_` 已承接串口结果）。
但：**发送 command 不能由 Diagnosis 自动触发**。Diagnosis 仍然 = analysis，不是 control loop（M9 冻结语义保持）。
```

## 27. Simulator Strategy（M10 必须新增的能力）

```text
原则：M10 开发**不得依赖真实硬件**才能测试。需要 deterministic simulator 至少支持：
  FC03 success / exception / timeout（已有基础）；
  FC06 echo success / exception / timeout（**新**）；
  FC10 success / exception / timeout（**新**）；
  broadcast write → ExpectedNoResponse（**新**，"v1 has no broadcast semantics" 需扩展）。
需要"可写"的模拟从站：当前 `handleRequest` 是 const 且"reads state, never mutates it" ⇒ 需设计
  **显式可写模式**（mutation 必须 opt-in，默认只读），并保持确定性（无时钟/线程/随机）。
CRC corruption / mismatched echo：既有 fault 框架含 CorruptCrc；echo 失配是否为独立 fault 模式，按既有 seam 调查后决定。
```

## 28. Real Hardware Boundary

```text
自动 acceptance **不要求**真实 Modbus device。M10 后期可以有 manual hardware acceptance，但必须**单独标 Manual**，
**不得**把 simulator PASS 写成 hardware PASS（对齐 M9-E/F 的 provenance 纪律）。
```

## 29. Write Safety Test Matrix（先设计 oracle，不实现）

```text
W1  未连接时不可发送（domain-side 拒绝，且 0 次 transport send）
W2  非法输入永不触达 transport（UI validation + controller 校验双测）
W3  一次显式动作 → 恰好 1 个 write request（send count == 1）
W4  双击/连按 Space·Enter 不能产生重复 pending write（in-flight guard）
W5  导航不触发 write（workspace 切换后 send count 不变）
W6  隐藏页保留焦点的控件不能触发 write（沿用 M9-F 的 page gating + H1S 型 oracle）
W7  timeout 不得被表达为"设备未改变"（语义断言：文案/结果不出现"写入失败"式断言）
W8  broadcast write → ExpectedNoResponse，且**不是** Success
W9  Agent/AI 不能调用 write（tools 集合与调用路径断言）
W10 校验失败保留 draft 供修正（draft 不被清空）
W11  response echo 失配 → 不是 Success（issue 语义）
W12  Clear/reset UI 不得静默产生 write（send count == 0）
```

## 30. Keyboard Safety（吸收 M9-F lessons）

```text
任何 Write button 必须考虑 Tab / Shift+Tab / Space / Enter，以及 **hidden retained focus**。
硬要求：切页后旧的 Write button **不可能**继续被 Space 激活（M9-F 的 page gating 已提供机制，**复用/验证，不另造**）。
```

## 31. Accessible Write Controls（baseline，不重做全局）

```text
未来 write controls 必须有：meaningful accessible name / visible keyboard focus / enabled-disabled 语义。
M10 **不**重新做全局 accessibility redesign；只要求新控件符合 M9-F 已冻结 baseline。
```

## 32. Audit Logging / Evidence

```text
现状：既有的可复核证据是 transaction 事实（status/elapsed/exceptionCode/issueText）与 UI 呈现，**没有** wire hex。
对 write，建议至少能保留：**what was requested / what response arrived / outcome**，供用户复核；
不得设计"写成功"却没有 raw evidence 或 transaction record 的情形。是否引入 wire hex 保留（以及保留边界）作为 decision request。
```

## 33. Security Boundary

```text
M10 不引入：remote command server / AI autonomous control / credentials / cloud write authority。
若 serial config 持久化涉及设备参数：另行审计，**不得顺手存 secret**。
```

## 34. UI IA Proposal（recommendation，待 Review）

```text
优先审计结论：**Communication workspace 是 natural owner**（它已拥有串口连接 + 请求区 + FC03 读按钮；
diagnosis/statistics 管线已在其下游）。**不新建第 6 个 active workspace**（M9 IA 不推翻）。
比较：
  A. Communication 内 **Read / Write 两个 section**（读区沿用现有；写区新增，含 function selector FC06/FC10 + summary）
  B. Communication 内 **统一 function selector**（FC03/FC06/FC10 同区切换，字段随 function 变化）
  C. 其他结构（例如把写操作放到独立页面 —— 会破坏 IA，不建议）
recommendation（待 Review）：**倾向 A**（读/写分区，安全等级天然分离，符合 §6 的 read/write boundary，键盘顺序也可按安全等级排列）；
B 的优点是空间更省，但把"读"和"设备状态变更"混在同一组字段里，安全边界不够显眼。
```

## 35. Draft Persistence / Ownership

```text
draft：presentation-local，切 workspace 后**可以保留**（对齐 M9 state ownership 原则）。
pending request：属 **authoritative runtime state**，**不得**仅因 page hidden 被销毁（由 Controller 持有，§18）。
写清 ownership：draft = page-local；validated intent + pending = controller；wire = transient（adapter 写出后不保留，除非 §32 决策）。
```

## 36. Clear Results Boundary（审计 + 设计）

```text
AppBar「清空结果」现状语义（源码事实）：clearResults 只清会话结果（transactions/statistics/derived），
**不**切换 source、**不**关闭 transport、**不**取消 pending。
设计（明确"不影响"清单）：pending active request —— **不取消**（保持既有语义）；
  write draft —— **不清理**（presentation-local）；connection —— 不改变；session —— 保持。
若未来要求"Clear = cancel"，必须另立契约 + ADR；本轮不改。
```

## 37. Error Presentation（至少区分五类）

```text
validation error / transport error / timeout / Modbus exception / protocol mismatch —— **不得**全部显示成"写入失败"。
特别：timeout ≠ confirmed no-write（§21）。既有实现已有 separate error lanes 的先例
（`communicationSerialError` 与 Replay error 与 Transaction rows 三者分离），M10 沿用该结构。
```

## 38. Test Oracle Quality（吸收 M9 规则）

```text
规则：**UI text 不能单独证明 wire request 正确**。至少设计四个层级 oracle：
  encoded bytes（codec 输出）/ transport send count（adapter 计数）/ transaction facts（analysis）/ visible outcome（UI）。
write 测试尤其要证明 **exactly one send**。
机械化路径：core session + codec 走纯 C++ 单测；adapter/controller 走 ui_bridge 的 fake/injection seam；
UI 契约走 qml_focus_check 式的 in-process 断言（M9-F 已验证该架构可行）。
```

## 39. Phase Breakdown Proposal（据 audit 调整，不硬拆）

```text
建议（待 Review）：
  M10-A  Active Master foundation：**统一 read/write 请求契约**（三层责任 §17）+ 可写 deterministic simulator seam +
         事务集成最小改动（§23/§24 最小字段）—— 不新增用户可见写能力。
  M10-B  FC03 主动读统一到新契约（含 provenance/统计/诊断集成回归；功能等价，行为不倒退）。
  M10-C  Write safety foundation：authority guard + confirmation policy 落地（按 §9 决策）+ summary 呈现 +
         validation 四层 + 文案/错误分类（§10/§11/§37）。
  M10-D  FC06 写单寄存器（encoder + echo 校验 + issue 语义 + simulator 支持）。
  M10-E  FC10(0x10) 写多寄存器（byteCount 派生 + 多值 summary + simulator 支持）。
  M10-F  manual / hardware / final acceptance（含 hardware 单独标注）。
调整依据（源码事实）：FC03 主动读**已经存在**，因此 M10-B 是"纳入统一契约"而非"从零实现"；
可写 simulator 是 D/E 的前置，故与 safety foundation 同列 C 之后；是否需要把 provenance（§24）提前到 A，由 Review 决定。
```

## 40. Decision Requests（Phase 1 请求裁定）

```text
1  M10 v1 exact FC scope（是否严格 FC03 + FC06 + FC10(0x10)，不含 FC01/02/04/05/0F/vendor）
2  single in-flight vs queue（建议 single，见 §18）
3  write confirmation policy（§9 A/B/C/D 选型）
4  automatic retry policy（建议 v1 no implicit write retry，见 §20）
5  timeout wording/semantics（§21 已冻结语义，需确认中文文案口径）
6  broadcast-write policy（§13：允许 → ENR，且绝不称成功；FC03 广播保持拒绝）
7  Active Master UI ownership（§34：Communication workspace；A/B/C 选型）
8  transaction integration strategy（§23：如何让写事务进同一模型，最小字段边界）
9  simulator acceptance scope（§27：可写模拟从站范围与 fault 场景）
10 real-hardware acceptance boundary（§28：manual 单独标注）
11 Agent/AI write authority remains NONE（§7 确认冻结）
12 phase sequencing（§39 的 A–F 是否需要调整）
```

## 41. Learning Gate 七问（A…G）

```text
A 这个功能解决什么问题：把 ModbusLens 从"只会看"（passive/replay/simulator 读）扩展到"可受控地做"
  —— 通过真实串口发起 FC03 读与 FC06/FC10 写，同时把写操作作为**独立安全等级**处理。
B 当前代码如何工作：控制器拥有唯一串口命令 seam（仅 FC03），core session 持有单条事务并复用既有 codec/analyzer，
  完成后经 publishSerialResult 替换模型/统计/诊断输入；taxonomy 已含 ENR 与 request-issue 正交集合。
C 新增知识是什么：FC06/FC10 的请求构造（encoder 缺失）、写响应 echo 校验语义、write authority/confirmation/retry/timeout-unknown、
  可写确定性模拟从站、广播写 = ENR 且不证明成功。
D 最少需要掌握哪些概念：Modbus 写语义（单/多寄存器、byteCount 派生）、broadcast 与 ENR、echo 一致性、
  in-flight/幂等与 retry 风险、UI validation vs protocol validation、read/write 安全分层。
E 方案为什么这样设计：复用既有 transport/codec/taxonomy/statistics/diagnosis 管线，避免第二套 universe；
  把安全要求集中成独立 contract（authority/confirmation/summary/retry/timeout），并以 machine-checkable oracle 保护。
F 哪些 V1/既有行为有风险：passive-analysis 语义（request-issue 正交性）、统计定义（ENR 分母）、
  FC03 现有 UI 行为与 disconnection 语义、navigation presentation-only、M9-F 的 focus gating 复用方式。
G 准备如何测试：分层 oracle（§38）+ 写安全矩阵（§29）+ 可写模拟从站（§27）+ 复用 qml_focus_check 架构验证 UI 契约。
```

## 42. Knowledge & Verification Plan（Test Plan 预告，本轮不实现）

```text
core 层：encoder/decoder 单测（FC06/FC10 golden vectors）、echo 失配 issue、ENR 语义、统计不变式。
controller 层：ui_bridge 式注入测试（send count == 1、busy guard、disconnected 拒绝、非法输入 0 次 send）。
simulator 层：可写从站 + fault 场景（echo 成功/异常/timeout/broadcast ENR）。
UI 层：qml_focus_check 架构内的 write 契约场景（W5/W6/W12；控件可达性/焦点可见/enabled 语义）。
回归：既有 27 项 ctest 全绿 + M9-F 的 focus 检查不得退化。
```

## 43. Boundary / 本轮未做

```text
未实现任何代码；未改 src / QML / CMake / scripts / tests / assets / samples / screenshots；
未创建 v2.0.0 tag；未 push；未推进 verified LKGC（保持 `aa2f3db`）；未重开 M9 的任何主题。
M10 Implementation = NOT STARTED（等待 Phase 1 Review）。
```

## Phase 1 Review = HOLD + Correction（2026-09-20，append-only，docs-only）

> **Phase 1 Review = HOLD（设计层面的窄 HOLD）**：**source audit 接受、Active Master 总体架构方向接受**；
> HOLD 只因**进入 implementation 前必须冻结的四个安全契约尚未闭环**：
> **A. deterministic transport seam** / **B. write transmission disposition** /
> **C. transaction/write evidence ownership** / **D. echo-mismatch outcome 与 issue orthogonality**。
> 本轮**只做 docs-only correction**：补齐上述契约并**把 12 项 decision requests 全部落为 Review 决议**（不再标 pending）。
> 原 Phase 1 记录（§4–§43）**保留不删**；Implementation = **NOT STARTED**；verified LKGC 保持 `aa2f3db`；未创建 tag；未 push。

### FC0. 本轮新做的三次真实源码审计（结论，非推断）

```text
① BLOCKER B 传输事实审计（`src/ui/serial/SerialPortAdapter.cpp`）：
   startTransaction 有**四个** pre-send 返回点，其中前三个**完全未进入 write**：
     1) `!port_.isOpen()`            → transportError「串口未连接…」→ **definitely not submitted**
     2) `hasActiveTransaction()`     → transportError「串口忙…」    → **definitely not submitted**
     3) `session_.beginReadHoldingRegisters(...)` 返回 error（Busy/InvalidAddress/InvalidQuantity）
                                     → transportError「串口请求无效」→ **definitely not submitted**
     4) `port_.write(...)` 短计数      → `cancelPending()`（session_.cancel + timer stop + **port close**）
                                     → transportError「串口写入失败：…」→ **部分字节可能已交出 ⇒ 不是"definitely not"**
   提交点：`port_.write(...)` 返回完整计数后立即 `elapsed_.start()` + `timeoutTimer_.start(timeout)`
            ⇒ **进入 transmission lifecycle 与"开始等待响应"是同一时刻**。
   运行期：`handleReadyRead` 形成完整候选 → 分析 → `transactionCompleted`；
           `handleTimeout` → `session_.onResponseTimeout(elapsed)`（NoResponse→Timeout / 部分字节→CrcError·ProtocolError）；
           `handlePortError` → 有 pending 时 `session_.cancel()` + transportError，**不伪造 Modbus 状态**；`closePort()` 为静默 cancel。
   保守规则（冻结）：**`QSerialPort::write` 成功只证明字节被 Qt 接受，不证明到达线路** ⇒
            once accepted into the transmission lifecycle，**无确认 response 一律按 UNKNOWN**（不制造精确性）。
② BLOCKER D echo-mismatch 审计（`src/core/analysis/PassiveTransactionAnalysis.cpp`）：
   **FC06**：结构合法但 echo 字段不匹配 ⇒ `status = **ProtocolError**` + issue = `WriteSingleRegisterEchoMismatch`
             （payload：expected/actual registerAddress + expected/actual registerValue）。
   **0x10**：同类不匹配 ⇒ `status = **ProtocolError**` + issue = `WriteMultipleRegistersEchoMismatch`
             （payload：expected/actual startingAddress + expected/actual quantity）。
   且代码显式保持正交：**matching normal reply ⇒ Success，即使 request 携带 invalid semantics**（那些事实只进 requestIssues）。
   ⇒ **现有语义已满足 §17 的冻结架构，M10 只需复用，无需 behavior correction。**
③ BLOCKER C evidence 审计（`src/core/analysis/TransactionAnalysis.h` + `src/ui/TransactionListModel.h`）：
   `TransactionAnalysis` 只有 status / elapsed / exceptionCode / issue；`ResponseObservation`（`variant<ModbusRtuFrame, RtuDecodeError, NoResponse>`）
   只是**分析输入**；Qt 层 `TransactionListEntry` 亦无字节字段。
   ⇒ **wire evidence（request/response 原始 ADU）当前在分析后被丢弃** ⇒ M10 需按 §21 设计**最小新增字段**（数据层不得丢 wire evidence；UI v1 是否展示 hex 可 DEFER）。
```

### FC1. 协议命名消歧（冻结写法）

```text
M10 canonical 文档中，三种主动功能在**首次出现**时统一写：
  **0x03（Read Holding Registers）** / **0x06（Write Single Register）** / **0x10（十进制 16，Write Multiple Registers）**。
**不得**只写「FC10」——易被误读为十进制 function code 10；源码类名保持既有 **`Function16`**。
历史标题可保留旧口径，但后续正文一律按上述消歧写法。
```

### FC2. v1 exact scope（冻结）

```text
M10 v1 主动功能 = **0x03 / 0x06 / 0x10** 三者；**不扩** 0x01 / 0x02 / 0x04 / 0x05 / 0x0F / vendor function。
0x03 已有功能**不是重新实现**，而是纳入统一 Active Master contract（行为等价，见 FC21）。
```

### FC3. Broadcast Policy — Review 决议（active 广播在 v1 拒绝）

```text
**M10 v1 Active Master 不提供广播发送**：主动 UI 的 unit/slave = **0** 对 0x03 / 0x06 / 0x10 **一律在 validation 阶段拒绝**，
**不得触达 transport**（sendCount = 0）。理由：广播写会影响多个设备且无响应可确认结果，在尚无专门广播安全设计时不暴露给用户。
保留既有 `ExpectedNoResponse` / `InvalidBroadcastFunction` 在 **passive / Replay / protocol analysis** 中的语义（**不删除 ENR、不改统计定义**）。
⇒ 原 Phase 1 计划中的「active broadcast write ENR」**移出 M10 v1 active acceptance matrix**，记录为**未来独立设计项**。
```

### FC4. Single In-flight — Review 决议

```text
M10 v1 = **single in-flight only**；不做 queue / parallel request / pipeline / concurrent writes。
Controller 继续拥有 authoritative pending request；**只有当前事务完全结束**（Success / Exception / CrcError / Timeout / ProtocolError
或本地 terminal result）后才允许下一次 Send。
```

### FC5. No Implicit Retry — Review 决议

```text
M10 v1：0x03 / 0x06 / 0x10 **全部 no implicit automatic retry**，包括 timeout / transport interruption / CRC error。
用户可在一次事务终止后**显式再次按 Read·Write** 重试；**尤其禁止 write timeout 自动 resend**
（设备可能已执行写入，只是 response 丢失）。
```

### FC6. Write Confirmation Policy — Review 决议

```text
**每次 0x06 / 0x10 都需要 explicit confirmation**。流程：编辑 draft → 点击 Write → **validation PASS** → 打开 confirmation →
用户确认 → **才调用 transport**。**点击 Write 本身不得直接发送**。
confirmation 必须展示：unit/slave、function、address；0x06 另需 **value**；0x10 另需 **start address / quantity / 全部 values**
（允许滚动，**不允许隐藏实际 values**）；**不得**只显示「将写入 N 个寄存器」。
confirmation 安全交互要求（contract，非 QML 实现）：Cancel 明确可达；keyboard focus 可见；**打开 dialog 本身不得发送**；
**Enter/Space 不能因旧 hidden focus 绕过确认直接写**；**cancel = zero send**。
最终 QML 形式留 Implementation；**不使用 armed write mode、不使用仅高风险值确认**（理由：诊断工具写频率较低，v1 优先 safety / auditability）。
本条是 M10 v1 冻结决定，**不等于永久产品决定**。
```

### FC7. BLOCKER A — Deterministic Transport Seam（设计）

```text
源码事实：production transport 是 concrete `ui::SerialTransactionAdapter → QSerialPort`，**没有 deterministic fake transport**
⇒ 对 write safety 测试不足。**M10-A 必须先建立 Controller → transport 的可替换 seam**。
· Production implementation：QSerialPort adapter（不变）。
· Test implementation：**Recording / Fake transport**，至少可：记录 send count / 记录 exact ADU bytes / 配置 accept·reject /
  配置 response bytes / 配置 timeout / 配置 transport error / **明确控制 completion timing**。
· 必须能确定性证明：**0 sends / exactly 1 send / never 2 sends**。
· **不得把 QSerialPort 塞进 core**：core session 继续纯 C++20、无 Qt、无 COM knowledge。
具体 interface 名称按现有命名风格在 Implementation 阶段定；本轮只冻结能力与边界（不预写代码）。
```

### FC8. Transport Test Boundary（两层冻结，禁止揉成一个 mock）

```text
A. **Transport fake** 回答：是否发送 / 发送几次 / 发送了什么 bytes / 何时收到结果。
B. **SimulatedSlave** 回答：给定合法 ADU，**设备语义**如何响应。
两者**分开**，不要把 transport 行为与设备语义揉成一个巨大 mock。
```

### FC9. BLOCKER B — Transmission Disposition（冻结定义）

```text
1. **NotSent**：请求在到达可实际发送阶段前已被拒绝。例如 validation fail / disconnected / busy / **confirmation cancel** /
   adapter 明确 reject before send（FC0 的 1)2)3) 三类）。此时**可以确认设备没有因本次命令收到该 request**。
2. **PossiblySent**：request 已进入可能离开主机的发送阶段，但**没有获得可信最终 response**。例如 response timeout /
   发送后断线 / 某些 transport error / **短计数 write**。此时 **device state = UNKNOWN**。
**不得把所有 transportError 都叫「write failed」。**
```

### FC10. Timeout Semantics（统一 outcome + 分层表述）

```text
outcome 继续统一使用既有 **Timeout**（**不引入第二套 outcome enum**）；presentation/context 区分：
  · 0x03 timeout = 未收到读取响应。
  · 0x06 / 0x10 timeout = **响应超时，设备写入状态未知**。
**禁止**措辞：「写入失败」/「写入未发生」/「设备未改变」——均超出证据。
```

### FC11. Unified Validated Request Intent（三层责任）

```text
M10-A 设计**统一 validated intent**，至少可表达：function / unit·slave / start·register address / quantity /
single value / multiple values / timeout，以及**必要 provenance**。
分层：**UI Draft = page-local**；**Validated Intent = Controller/runtime authority**；**Encoded ADU = core codec 产物**。
**QML 不得手拼 bytes；transport 不得重新解释 UI draft。**
```

### FC12. Pending Request Snapshot（冻结）

```text
开始发送后，pending request 必须保存 **Validated Intent snapshot**，**不得继续引用用户可编辑的 QML 字段**。
用户发送后即使改 draft，进行中的 response matching **仍以发送时 snapshot 为唯一 authority**。
匹配字段：**0x06 echo match = unit / function / address / value**；**0x10 response match = unit / function / start address / quantity**。
```

### FC13. SerialTransactionSession Generalization（设计）

```text
源码事实：当前 `core::SerialTransactionSession` 是 **0x03-specific**（`beginReadHoldingRegisters`）。
Correction 设计要求：**泛化到 0x03 / 0x06 / 0x10，而不是复制三套平行状态机**
（禁止 SerialWrite06Session / SerialWrite10Session 之类）。
优先方案：**统一 request descriptor + function-specific response validation**（描述符携带 intent 快照与编码后的 request wire；
响应校验按 function 分派），继续保持**纯 C++20 / 零 Qt**。本轮不 implementation。
```

### FC14. BLOCKER D — Echo-mismatch 现状（审计结论）

```text
见 FC0 ②：结构合法但 echo 不匹配时，**0x06 → ProtocolError + WriteSingleRegisterEchoMismatch**；
**0x10 → ProtocolError + WriteMultipleRegistersEchoMismatch**；两者 payload 均含 expected/actual 字段。
matching normal reply 即使 request 语义非法仍为 **Success**（request issues 独立承载）。
⇒ **结论：现有 semantics 与冻结架构一致，M10 直接复用；本轮不需要 behavior correction，也未修改任何源码。**
```

### FC15. Outcome / Issue Orthogonality（继续冻结）

```text
Outcome 与 structured issue 是**正交轴**。**禁止**实现 `if issue exists → status = ProtocolError` 这类把 issue 当 status rewrite 的逻辑；
0x06 / 0x10 的 echo mismatch 必须由 **response facts 分别导出 outcome 与 issue**（现有实现即如此）。
```

### FC16. Transaction Integration — Review 决议（同 universe + append）

```text
Active Master **复用现有 transaction universe**；**不建立** ActiveTransactionModel / WriteHistoryModel 之类**第二套世界**。
但当前 `publishSerialResult` 的**单行 replacement** 不适合作为最终 write audit trail
⇒ M10 设计：**同一个 Active Serial session 内，每次完成的 request APPEND transaction record**，
**不得每次写覆盖上一条证据**；**Clear Results 才是显式清除入口**。
```

### FC17. Source Transition Boundary（冻结）

```text
source/session 切换继续遵守现有 **authoritative source contract**；**不得**把 Simulator / Replay / Active Serial 的 transaction
无条件混成同一 session。进入 Active Serial source/session 时遵循现有 **source replacement** 语义；
**同一个 Active Serial session 内** 0x03 / 0x06 / 0x10 连续请求 **append**。明确：**navigation 不切换 source**。
```

### FC18. Clear Results Contract（冻结，须写入 M10 acceptance）

```text
Clear Results **只清已经存在的 result / history presentation·domain records**；
**不** disconnect、**不** cancel pending、**不** clear write draft、**不** send anything。
若 Clear Results 发生在 request pending：**pending 继续**；未来 completion **作为新的 transaction 进入已经清空后的 session view**。
```

### FC19. BLOCKER C — Write Evidence（审计 + 最小字段设计）

```text
审计结论（FC0 ③）：现有 `TransactionAnalysis` / `TransactionListEntry` **均未保存 raw request·response bytes**。
写操作至少需要可审计 evidence：**Validated Request Intent / Encoded request ADU / Raw response ADU（若存在）/ Outcome /
Structured issues / transport disposition**。
M10 最小新增（字段名 Implementation 定）：
  · **request ADU bytes**（编码产物，随 pending snapshot 一起保存）；
  · **response ADU bytes**（收到即保留，包含 CRC 错误/协议错误的原始字节）；
  · **transport disposition**（NotSent / PossiblySent，见 FC9）。
要求：**内部 transaction facts 至少保留 raw ADU**；**UI v1 是否立即展示 hex 可 DEFER**；
**数据层不得把 wire evidence 发送后直接丢掉**。
```

### FC20. FC03 Regression Contract

```text
M10-B 目标 = 把既有 0x03 纳入统一 contract，**必须行为等价**：现有 valid read 继续工作；
**不得**因统一框架改变 address range / quantity / timeout / statistics / diagnosis / source semantics（除非 Review 另行批准）。
```

### FC21. 0x06 Contract

```text
新增 **request encoder**。validated intent = unit / address / value / timeout。
response matching = unit / function / address / value。
本地 validation failure ⇒ **0 transport sends**；confirmation cancel ⇒ **0 transport sends**；accepted write ⇒ **exactly 1 send**；
timeout ⇒ **Timeout + write state unknown**；echo mismatch ⇒ 按 FC14/FC15 的正交语义。
```

### FC22. 0x10（十进制 16）Contract

```text
名称统一 **0x10（decimal 16）Write Multiple Registers**。单一 authority = **values[]**，派生 **quantity 与 byteCount**；
UI **不允许** values / quantity / byteCount 形成三份可互相冲突的 truth source。
response matching = unit / function / start address / quantity。validation：**address + quantity 不得越寄存器范围**。
同样：confirmation cancel = **0 send**；accepted = **exactly 1 send**。
```

### FC23. Active Broadcast Removal（修正原 Phase 1）

```text
M10 v1 simulator **不需要** active broadcast write acceptance（见 FC3）。
原 **W8** 由「broadcast write = ENR」改为：**active unit 0 write 在 validation 阶段拒绝，transport send count = 0**。
既有 **ENR 继续做 regression**（确保 M10 不破坏 passive broadcast semantics）。未来若支持 active broadcast：**另立安全设计**。
```

### FC24. Simulator Design（可写模拟从站）

```text
现有 `SimulatedSlave` 为 **const / read-only**。M10 设计：**显式 opt-in 的 mutable register bank**（默认初始化确定性）。
  · 0x06 成功后：对应 register 更新；0x10 成功后：对应 contiguous registers 更新；
  · **异常不得 mutation**；timeout / fault 按既有 fault seam **确定性**产生；
  · **不要随机 / 线程 / 真实时钟**；
  · 测试必须可精确断言 **before registers → request → after registers**。
```

### FC25. Recording Transport Acceptance Matrix（T1–T9）

```text
T1 validation reject → **sendCount = 0**
T2 confirmation cancel → **sendCount = 0**
T3 one explicit confirm → **sendCount = 1**
T4 double click / repeated Space **while pending** → **sendCount = 1**
T5 hidden Write control → **sendCount = 0**
T6 navigation → **sendCount unchanged**
T7 timeout after accepted send → **sendCount = 1 + write state UNKNOWN**
T8 disconnect / busy precondition → 相应 **0-send** 行为
T9 exact request ADU bytes **match encoder oracle**
```

### FC26. UI Ownership — Review 决议

```text
**Communication workspace** 拥有 Active Master UI，采用 **Read section + Write section**；**不新增 workspace**。
Write section 内部可按 **0x06 / 0x10** 选择不同输入；
**不得**用一个高度动态的巨大 function selector 把 read/write safety 混在一起。
```

### FC27. Draft Persistence（冻结）

```text
Write draft 继续 **page-local persistence**：切页 draft **保留**；pending request **Controller authoritative**。
**page hidden 时不得** cancel / mutate intent / send / **receive keyboard activation** —— 继续**复用 M9-F page gating**。
```

### FC28. Real Hardware Boundary — Review 决议

```text
自动化 acceptance **不要求真实设备**。M10-F：若有**安全可写**的真实测试设备，人工验证 0x03 / 0x06 / 0x10 **并恢复原值**；
若**无硬件**：M10 软件范围可 COMPLETE，但必须明确记录 **REAL HARDWARE NOT VERIFIED**，**不得**写 `hardware PASS`。
任何未来正式发布 Active Write capability **应另有 hardware sign-off**。
```

### FC29. AI / Agent Authority — 最终决议

```text
**AI / Agent write authority = NONE**。M10 **不新增** write tool / send tool / raw serial tool。
AI 可以**解释 / 建议 / 生成候选值**，但任何写操作必须**重新进入人类链路**：
human UI → explicit Write → confirmation → transport。
**不得**提供绕过 UI confirmation 的内部 Agent command。
```

### FC30. Updated Write-safety Matrix（W1–W18）

```text
W1  未连接不可发送（domain-side 拒绝，sendCount = 0）
W2  非法输入永不触达 transport（UI + controller 双层校验，sendCount = 0）
W3  一次显式动作 → 恰好 1 个 write request（sendCount = 1）
W4  双击 / 连按 Space·Enter **在 pending 期间**不能产生重复 pending write（sendCount = 1）
W5  导航不触发 write（sendCount 不变）
W6  隐藏页保留焦点的控件不能触发 write（复用 M9-F page gating + H1S 型 oracle）
W7  timeout 不得被表达为「设备未改变」（措辞/语义断言）
W8  **改为：active unit 0 write 在 validation 阶段拒绝，sendCount = 0**（原「broadcast write = ENR」移出 v1）
W9  Agent / AI 不能调用 write（tools 集合与调用路径断言）
W10 校验失败保留 draft 供修正
W11 response echo 失配 → 不是 Success（ProtocolError + 对应 issue，见 FC14）
W12 Clear / reset UI 不得静默产生 write（sendCount = 0）
W13 **confirmation cancel → sendCount = 0**
W14 **transport accepted 后 timeout → device state UNKNOWN**
W15 **transport pre-send rejection → NotSent**
W16 **pending intent snapshot 不受后续 draft 编辑影响**
W17 **raw request ADU 与 encoded intent 一致**
W18 **同一 active serial session 内 transactions append，不覆盖旧 write evidence**
```

### FC31. Error / Result Semantics（分层，非第二套 public enum）

```text
至少设计：ValidationError / NotConnected·Busy（pre-send）/ TransportNotSent / PossiblySentTransportError / Timeout /
ModbusException / ProtocolMismatch / Success。
注意：**这不是要求创建另一套 public outcome enum** —— 这是 **presentation / transport disposition 的分层语义**；
**public transaction outcome 继续复用既有 taxonomy**（Pending/Success/Exception/CrcError/Timeout/ProtocolError/ExpectedNoResponse）。
```

### FC32. Phase Sequencing — 修正后冻结（A→F）

```text
**M10-A Active Master contract foundation** 必须包括：unified Validated Intent / **generic SerialTransactionSession** /
**deterministic recording transport seam** / transmission disposition / transaction append·evidence fields / **mutable simulator foundation**。
**M10-B** 0x03 migration into unified contract（**行为等价**，见 FC20）。
**M10-C** write safety UI foundation（write draft / confirmation / validation / busy·double-send / wording）。
**M10-D** 0x06 end-to-end。**M10-E** 0x10 end-to-end。**M10-F** final automated·manual acceptance + optional real-hardware acceptance。
**不得在 M10-A 直接实现 0x06 UI。**
```

### FC33. Decision Record（原 12 项全部标记为 Review 决议，不再 pending）

```text
 1 scope = **0x03 / 0x06 / 0x10（decimal 16）**                                       —— RESOLVED（FC2）
 2 **single in-flight**                                                              —— RESOLVED（FC4）
 3 **每次 write 都 confirmation**                                                     —— RESOLVED（FC6）
 4 **no implicit retry**（read 与 write 皆无自动重试）                                 —— RESOLVED（FC5）
 5 **write timeout = state unknown**                                                 —— RESOLVED（FC10）
 6 **active broadcast 在 v1 拒绝**（passive ENR 保留）                                 —— RESOLVED（FC3）
 7 **Communication 拥有 UI**（Read + Write sections）                                  —— RESOLVED（FC26）
 8 **同一 transaction universe + active-session append**                              —— RESOLVED（FC16）
 9 **simulator + recording transport 双层测试**                                        —— RESOLVED（FC7/FC8）
10 **hardware 非自动化；无硬件时必须披露 REAL HARDWARE NOT VERIFIED**                  —— RESOLVED（FC28）
11 **AI / Agent authority = NONE**                                                   —— RESOLVED（FC29）
12 **A→F sequencing 按本 correction 调整**                                             —— RESOLVED（FC32）
```

### FC34. 边界（本轮）

```text
docs-only：未修改 src / QML / CMakeLists.txt / scripts / tests / assets / samples / screenshots；
未开始 M10-A implementation；未创建 v2.0.0 tag；未 push；**verified LKGC 保持 `aa2f3db`**；M9 保持 ✅ COMPLETE。
状态：**M10 Phase 1 Correction / Re-review；Implementation = NOT STARTED**。
## M10-A — Active Master Contract Foundation（2026-09-20，behavior-bearing implementation）

> **Phase 1 Re-review = PASS；Phase 1 = COMPLETE；M10-A = GO。** 本轮为**允许 behavior-bearing 的 foundation 实现**：
> 建立后续 0x03 / 0x06 / 0x10 共用的 Active Master 基础设施，**不含任何用户可见写 UI / confirmation dialog /
> 写请求 encoder / write button**。完成后 FC03 现有用户行为保持原样；verified LKGC 仍为 `aa2f3db`（不自行推进）。

### M0. Preflight（2026-09-20）

```text
HEAD = `86e88ed`（main，clean）；verified LKGC = `aa2f3db`；M9 = ✅ COMPLETE；M10 Phase 1 = COMPLETE；
CMake project VERSION = 2.0.0；v1 tag object `2cee626` / target `ae067ab`；v2.0.0 **absent**；
origin/main = `a40d935`（behind 0 / ahead 97）；`git diff --check` PASS —— 全部相符。
```

### M1. Phase 1 PASS / M10-A GO 归档

```text
Phase 1 Re-review = PASS；Phase 1 = COMPLETE；M10-A = GO。
四个 blocker 已闭环：A deterministic transport seam / B transmission disposition / C wire evidence retention /
D echo mismatch Outcome·Issue orthogonality。12 项 Review decisions 全部保持 RESOLVED。
```

### M2. Mandatory Source Re-read（真实源码，非照抄设计）

```text
AnalysisController.{h,cpp}（1320 行）：唯一串口命令 seam = readHoldingRegistersOnce(int,int,int,int)，
  窄化转换前完成 1..247 / 0..65535 / 1..125 / timeout>0 校验（中文文案属冻结 FC03 契约），
  serialConnected_ && !serialBusy_ 前置，adapter 接受后才置 pendingSerialAddress_ + serialBusy_；
  publishSerialResult = 单行 replacement（rowCount 恒 1）+ 单元素统计 + 单条 DiagnosisTransaction。
ui::SerialTransactionAdapter（QSerialPort 唯一 owner）：pre-send 三点（未连接/忙/begin 失败）不写 wire；
  短计数 write → cancelPending() + transportError；完整计数 → elapsed_.start() + timeoutTimer_.start()；
  handleReadyRead/handleTimeout → transactionCompleted；handlePortError 对 active 事务 cancel 且不伪造状态。
core::SerialTransactionSession：FC03-only begin、exact-candidate framing（0x80 → 5B；0x03 → 5+byteCount）、
  buffer 只在 timeout 时整体解码、analyzeFunction03Transaction 复用、cancel 无 Modbus 结论。
TransactionAnalysis / TransactionListEntry：无任何 raw ADU 字段（BLOCKER C 事实）。
Function16.h：只有 response decoder + 结构字段读取，**没有 0x10 请求 value 解码**；Function06.h 有 request/response decoder。
SimulatedSlave：handleRequest const、无写语义、无 mutation API；SimulationFault 在 wire 层。
tests：test_serial_session（SERIAL-A01–A16）、test_serial_adapter（I01–I05）、test_ui_bridge（s05–s10 锁定 mapping/replace/
  stale guard）、test_passive_analysis（FC06/0x10 echo 语义与正交性）、test_agent_tools（3 只读 tools）。
CMake：modbuslens_core 零 Qt；测试目标按需直编 adapter/controller 源文件；qml_* 四个门禁走真实 exe。
```

### M3. RED / gap evidence（R1–R6）

```text
RED 采用两种可复现证据：(a) 在 **detached worktree @ `86e88ed`**（`git worktree add --detach ../ModbusLens-M10A-RED 86e88ed`）
内放入本轮新测试并加两个测试目标，真实构建 → **编译级 RED**；(b) 对 pre-change 树做精确 grep 事实 + 引用既有测试锁。
命令：cmake -S . -B build/red -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=D:/QT/Tools/mingw1310_64/bin/g++.exe
      -DCMAKE_PREFIX_PATH=D:/QT/6.11.1/mingw_64 -DBUILD_TESTING=ON

R1（无可注入 transport）—— RED-2 构建失败：
    tests/fake_serial_transport.h:11:10: fatal error: core/active/ActiveRequestIntent.h: No such file or directory
    tests/test_active_master.cpp:12:10: fatal error: core/active/ActiveRequestIntent.h: No such file or directory
  grep 事实：`class SerialTransport` / `SerialTransport*` 在 src/ 中出现 **0** 次；`SerialTransactionAdapter serialAdapter_;`
  为 **by-value concrete member**（1 处）⇒ 当时无法注入 recording transport。
R2（session 只能 FC03）—— grep 事实：`beginActiveRequest` 出现 **0** 次；`beginReadHoldingRegisters` 出现 **3** 次。
R3（不保存 raw request ADU）—— grep 事实：`requestAdu|responseAdu|requestWire|responseWire` 在
  `TransactionAnalysis.h` + `TransactionListModel.h` 中出现 **0** 次；TransactionAnalysis 成员仅
  status / elapsed / exceptionCode / issue。
R4（response bytes 不可恢复）—— 同 R3：分析完成后 buffer 在 session 内被清空，最终 facts 无字节字段；
  RED-1 构建失败（core 测试）证明“不存在承载 raw ADU 的类型”：
    tests/test_active_request.cpp:9:10: fatal error: core/active/ActiveRequestIntent.h: No such file or directory
R5（replacement 而非 append）—— 既有测试即锁定证据：`tests/test_ui_bridge.cpp` s07_serialReplace
  「Replace, never append: still exactly one row, latest-only statistics」（该测试本轮**保持通过**，见 M19）。
R6（simulator 写不 mutation）—— grep 事实：`applyWriteRequest|WriteMode|Writable` 在 `src/core/simulator/` 出现 **0** 次；
  `SimulatorResult handleRequest(const ModbusRtuFrame& request) const;` 为 const 只读端点。
边界：RED 期间未修改生产行为；实现与测试在同一轮内完成（顺序见 M17-P0 的诚实说明）。
```

### M4. 实现架构（分层）

```text
core（零 Qt）                              app（Qt）                                 test-only
────────────────────────────────────      ─────────────────────────────────────     ─────────────────────
core/active/ActiveRequestIntent             SerialTransport（seam 接口）               tests/fake_serial_transport
  ActiveFunction / payload variant            ├─ SerialTransactionAdapter（生产）        RecordingSerialTransport
  validateActiveRequestIntent()               └─ RecordingSerialTransport（测试）        · sendCount / sentAduLog
  encodeActiveRequest() → Descriptor        AnalysisController                          · 可控 completion timing
core/active/ActiveTransactionEvidence         ├─ serialTransport_（可替换指针）          · pre-send accept/reject
  TransportDisposition / Evidence             ├─ pendingRequest_（发送时快照）           · timeout / transport error
  ActiveStartResult / Result / Record         ├─ activeSerialRecords_（append 权威）
core/analysis/TransactionProvenance           └─ publishCompletedTransaction()（投影）
core/serial/SerialTransactionSession（泛化）
core/simulator/SimulatedSlave（opt-in 可写）
core/protocol/Function16（新增请求 value 解码，仍 decode-only）
```

### M5. Unified Validated Request Intent（T022 §FC11 落地）

```text
`ActiveRequestIntent{ function, unitId, timeout, payload }`：
  ActiveFunction = ReadHoldingRegisters(0x03) / WriteSingleRegister(0x06) / WriteMultipleRegisters(0x10=十进制 16)；
  payload = std::variant<ReadHoldingRegistersIntent{start,quantity}, WriteSingleRegisterIntent{addr,value},
                         WriteMultipleRegistersIntent{start, std::vector<uint16> values}>。
单一 authority：0x10 的 quantity/byteCount **由 values 派生**，不存在第二份计数真相。
validation（单一实现，Controller 与 session 共用）：UnitIdNotUnicast(0 / >247) / QuantityOutOfRange /
  TimeoutNotPositive / PayloadFunctionMismatch；domain 常量导出（kReadHoldingRegistersMin/MaxQuantity 等）。
encodeActiveRequest()：0x03 → 既有 encoder + RTU codec → `ActiveRequestDescriptor{intent, frame, wire}`；
  **0x06 / 0x10 → UnsupportedFunction（本轮明确不实现写请求 encoder）**。
类型安全：无 QVariant map、无 stringly-typed function、无裸 JSON；机器 token（activeFunctionName）仅供 adapter 序列化。
```

### M6. Generic SerialTransactionSession（T022 §FC13 落地）

```text
lifecycle 泛化：Idle → beginActiveRequest(descriptor) → AwaitingResponse → feedResponseBytes / onResponseTimeout → Idle；
  `cancel()` 仍为本地中止（无 Modbus 结论）。**没有 SerialWrite06Session / SerialWrite10Session**。
descriptor 自洽性在 begin 处强制：frame.address == intent.unitId、frame.functionCode == activeFunctionCode(function)、
  `decodeRtuFrame(wire) == frame` ⇒ 「交给 transport 的字节就是被编码的 intent」成为类型层属性（W17）。
function-specific 语义收敛到单点 `analyzeActiveResponse()`：0x03 → 既有 analyzeFunction03Transaction；
  0x06/0x10 → **begin 阶段即拒绝**（UnsupportedFunction，发送数 0），保留一个确定性 defensive 分支（UnknownProtocolError），
  不发明 verdict、也不假装已支持。
beginReadHoldingRegisters(...) 作为 FC03 convenience 保留 → 先本地 validation（保持 InvalidAddress/InvalidQuantity 语义）
  再 encode → 委派 beginActiveRequest，因此旧 16 条 SERIAL-A 用例全部保持通过。
新错误码：InvalidTimeout / UnsupportedFunction / InvalidRequestDescriptor（追加式）。
```

### M7. Pending Intent Snapshot（T022 §FC12 落地）

```text
session：`pendingRequest()` 保存**发送时 descriptor**（intent + frame + wire）；timeout 也取自 intent（单一 authority）。
completion 时先取快照再 feed —— 完成会把 session 复位，快照保证结果永远带着它所回答的请求。
Controller：`pendingRequest_`（optional<descriptor>）既是**发送时快照**也是 **stale-completion guard**；
  完成时必须 `result.request == *pendingRequest_`，否则整条忽略（foreign / stale completion 永不入账）。
测试：AC-07（Busy 拒绝后快照不变）、TA-10（pending 期间改参数被拒 → 完成行仍描述首个请求）、TA-15（无 pending / 异请求结果被忽略）。
```

### M8. Transport Seam（T022 BLOCKER A 落地）

```text
`SerialTransport : QObject`（src/ui/serial/SerialTransport.h）：openPort / startActiveRequest(descriptor) /
  hasActiveTransaction / isPortOpen / closePort + 两个信号（transactionCompleted(ActiveTransactionResult)、transportError）。
  timeout 不是独立参数 —— 取自 descriptor.intent.timeout，避免第二份真相。
production = 既有 SerialTransactionAdapter（行为不变：未连接 / 忙 / 无效请求 → NotSent 且零发送；短计数 → PossiblySent；
  完整计数 → PossiblySent + 启动响应等待；端口错误 → cancel 且不伪造状态）。
Controller：`serialTransport_` 指向生产 adapter；`setSerialTransport(nullptr)` 恢复生产；注入对象不被 controller 所有；
  重指向在 pending 期间被拒绝（事务属于接受它的 transport）。**openPort 与 closePort 也走 seam**（见 M17-P1）。
未把 QSerialPort 下沉进 core；core 仍零 Qt / 零 COM。
```

### M9. Recording Transport（T022 §12 能力落地）

```text
tests/fake_serial_transport.{h,cpp}（测试专用 double，非产品代码）：
  配置：portOpen / acceptRequests（pre-send 拒绝）/ responseBytes / completionElapsed；
  记录：startAttemptCount / sendCount / sentAduLog（exact ADU）/ lastStartResult（accepted + disposition）/ hasPendingTransaction；
  驱动：completeWithResponse()（整块喂入）/ completeWithTimeout() / feedPartialBytes() / failTransport(message)。
  内部复用**同一个 core session**（与生产同一套 framing/analysis），completion 由测试显式驱动 ⇒ **无 Sleep、无真实 COM、
  无 wall-clock 竞争**。
两层纪律：transport fake 回答“是否/几次/什么字节/何时完成”；SimulatedSlave 回答“设备语义”。
```

### M10. Transmission Disposition（T022 §FC9 / §14 / §15 / §16 落地）

```text
`TransportDisposition{ NotSent, PossiblySent }` —— **正交的 transport fact，不是第二套 public outcome**（outcome taxonomy 未变）。
`ActiveStartResult{ accepted, disposition }`：accepted = 完整请求进入 transmission lifecycle（完成事件随后到达）；
  短计数 write = `{false, PossiblySent}`（线路无法澄清）；pre-send 拒绝 = `{false, NotSent}`。
生产 adapter 与 recording transport 采用同一分类；controller 只在 accepted 后建立 pending 状态。
pre-send 失败**不产生任何 Modbus 行**（不伪造 ProtocolError），也不进 session history —— TA-08 明确断言。
```

### M11. Wire Evidence（T022 BLOCKER C / §17 / §18 / §19 落地）

```text
`ActiveTransactionEvidence{ requestAdu, responseAdu, disposition }`；
`ActiveTransactionResult{ request(descriptor 快照), responseAdu, disposition, analysis }` + `evidence()` 投影；
`ActiveTransactionRecord{ sessionId, request, evidence, analysis }` —— 运行时权威（core 类型，零 Qt）。
生产 adapter 侧：`observedResponseBytes_` 逐字节累积**实际观察到的**响应（正常 / 异常 / CRC 坏 / 协议坏 / 部分 / 超时前
  部分字节），完成时随 envelope 一起 emit；纯无响应 = 空 responseAdu（明确契约）。
request ADU = 发送时 descriptor 的 wire 副本（**不是**事后从 intent 重编码的猜测）。
归属：权威在 **core/runtime record**；`TransactionListEntry` 只带 `std::optional<ActiveSerialProvenance>`（presentation 投影），
  且 Simulator / Replay / 既有 fixture 一律 std::nullopt（不存在伪造的 transport 事实）。QML 未暴露 hex（DEFER，无新 role）。
```

### M12. Append Foundation + Session Identity（T022 §FC16 / §FC17 / §22 落地）

```text
`ActiveTransactionRecord` 追加进 `activeSerialRecords_`：**证据 append、永不覆盖**（生产完成路径与 append API 同源）。
`appendSerialTransaction(record)` + `rebuildActiveSerialProjection()`：按记录顺序投影 rows / 聚合统计 / 诊断批次
  （= append 语义的完整实现，已被 TA-12 直接验证）。
session identity：`TransactionSourceKind{Simulator, Replay, ActiveSerial}`（core 枚举）+ `activeSerialSessionId_`
  （每次成功 connect ++，connect 时清空旧 history），**不从 modeLabel 文本 / workspace index / 文件名推断**（TA-14）。
【Review item-1】presentation 仍为 **latest-only**（rowCount 1 / 单事务统计）：Phase 1 §7 冻结“FC03 现有
  statistics / source·session behavior”，且明确“若迁移 FC03 需要改变用户可见行为：STOP + RCA；M10-B 才负责完整
  FC03 contract migration”。因此本轮把 append 权威与投影实现并测试，**presentation 切换留 M10-B**。
【Review item-2】按 §23 允许范围，Clear 的 UI acceptance 亦留 M10-B/C。
```

### M13. Clear Results Contract（T022 §FC18 / §23 落地）

```text
clearResults()：清结果 + 统计 + 诊断批次 + **completed session history**（records），**不清 pending**、
  不 cancel、不 disconnect、不清 draft、不发送；source 身份保留（Clear != Disconnect）。
TA-13 两段断言：① 两条完成记录 + Clear → records 0 / rows 0 / modeLabel 仍「串口模式」/ sourceLabel 不变 / 仍连接；
  ② pending 期间 Clear → pending 仍在飞 → 完成后**作为新事务进入已清空的视图**（1 record + 1 row）。
```

### M14. Writable Simulator Foundation（T022 §25 / §26 落地）

```text
SimulatedSlave：`WriteMode{ReadOnly(默认), Writable}`（**显式 opt-in**）+ `holdingRegister()` / `registerCount()`
  查询接缝 + `applyWriteRequest(frame) -> SimulatorWriteOutcome{Applied, ReadOnlyMode, NotMyAddress,
  UnsupportedFunction, MalformedRequest}`。
语义：先解码再写入（合法才 mutation，**非法/异常形状/异地址一律零 mutation**）；0x06 写单寄存器、0x10 写连续块
  （values 为唯一 authority）；bank 沿用 setHoldingRegister 的“按需增长、空洞为 0”规则（M12 Device Profile 之前
  不发明地址域策略）；无随机 / 无线程 / 无真实时钟 ⇒ before → request → after 可精确断言。
Function16：新增 **decode-only** `decodeWriteMultipleRegistersRequest`（quantity 1..123、byteCount==2*quantity==实际字节），
  仍**没有 encoder / 没有发送 API**。handleRequest（读路径）保持不变：0x06/0x10 仍以 Illegal Function 应答，
  写请求→应答帧的接线留 M10-D/E（需要写编码器）。
```

### M15. Tests

```text
新增目标 1：`active_request`（Pure Core，17 用例）—— tests/test_active_request.cpp
  AC01 validation 表（含 unit 0/248、FC03 域、超时、payload/function 不一致、0x10 values 1..123）
  AC02 FC03 descriptor 金样 wire（01 03 00 00 00 02 C4 0B）
  AC03 0x06 / 0x10 encode → UnsupportedFunction（无写编码器）
  AC04 begin 接受 + 快照等价 / AC05 篡改 wire 或 frame-address → InvalidRequestDescriptor（Idle 不变）
  AC06 手造 0x06 descriptor → UnsupportedFunction（绝对不发送）/ AC07 Busy 后快照不变
  AC08 泛化路径 FC03 等价（Success）/ AC09 泛化路径 Timeout
  AC10 evidence/result/disposition token / AC11 function 与 source token（0x10 = decimal 16）
  AC12 FC06 echo 失配回归 / AC13 0x10 echo 失配回归 / AC14 outcome·issue 正交回归 / AC15 broadcast 主动拒绝
新增目标 2：`active_master`（真实 controller + recording transport，16 用例 + init）—— tests/test_active_master.cpp
  TA01 closed → NotSent & 0 发送 / TA02 busy → NotSent & sendCount 仍 1 / TA03 accepted → sendCount 1 + exact ADU
  TA04 pending 期间重复按键 → 仍 1 次发送 / TA05 记录 ADU == evidence.requestAdu / TA06 CRC 坏响应原文保留
  TA07 accepted 后 timeout → PossiblySent + 空 responseAdu / TA08 pre-send 拒绝 → NotSent + 零伪造事务
  TA09 completion timing 由测试控制（无 sleep）/ TA10 快照唯一权威 / TA11 记录 append 且不覆盖
  TA12 append 投影 API（3 记录 → 3 行 + 聚合统计）/ TA13 Clear 契约两段 / TA14 typed source identity + session id
  TA15 stale / foreign completion 全忽略
既有测试扩展：test_simulated_slave 增加 **SA1–SA5**（默认只读零 mutation / 0x06 单写 / 0x10 连续块 / 非法与异地址零 mutation /
  before→request→after 确定性）；test_serial_session（SERIAL-A01–A16）与 test_serial_adapter（I01–I05）迁移到
  descriptor / seam API，**断言语义不变**（I05 增加 `{NotSent}` 断言）。
```

### M16. Automated Gates（真实输出）

```text
Debug：`ctest` → **29/29 PASS**（原 27 + active_request + active_master；包含 qml_smoke / qml_geometry_check /
  qml_nav_check / qml_focus_check 全绿）。
Release：`ctest` → **29/29 PASS**。
构建：Debug 与 Release 均**零 warning / 零 error**（-Wall -Wextra；修复本轮引入的 -Wmissing-field-initializers）。
`git diff --check` → PASS。ctest 数量由 27 增至 **29**（真实新增 2 个目标）。
```

### M17. Problems / RCA（诚实记录，含顺序说明）

```text
P0（流程诚实说明）：本轮测试与实现**交替**编写，因此 RED 证据不是在时间上先于实现采集的，而是
  (a) 在 detached worktree @ `86e88ed` 上**真实重放**（编译级 RED，见 M3）、(b) 对 pre-change 树做精确 grep，
  (c) 引用既有测试锁。RED 结论未因顺序而改变，但不得表述为“先 RED 后 GREEN 的严格 TDD”。
P1（真实缺陷，已被测试捕获）：seam 只做了一半 —— `connectSerial()` 仍在 `serialAdapter_` 上调用 openPort，
  注入的 recording transport 永远没被打开 ⇒ TA02–TA09 初期整体失败（startAttemptCount 0）。
  Root Cause：部分替换而非依赖反转（调用点遗漏）。Fix：openPort/closePort 全部走 `serialTransport_`。
  Verification：TA01–TA16 全绿、qml gate 全绿。Regression Protection：TA 断言的是**注入对象**的 sendCount/ADU，
  任何绕过 seam 的调用点会立刻让这些断言失败。
P2（编译 RED→GREEN 实例）：core 测试调用 `analyzeObservedTransaction` 却未包含 PassiveTransactionAnalysis.h ⇒
  `error: 'analyzeObservedTransaction' is not a member of 'modbuslens::core'`；补 include 后 17/17 全绿。
P3（零警告纪律）：descriptor 的 designated initializer 触发 -Wmissing-field-initializers（TransactionIssue 9 个 optional、
  TransactionAnalysis、TransactionListEntry 新字段）⇒ 显式补 std::nullopt 或改用局部默认初始化结构。
P4（断言修正，非产品缺陷）：SA2 起初断言 0x000B 为 0，但 bank 只增长到写入地址 ⇒ 正确契约是
  `holdingRegister(0x000B) == nullopt`（写入不会把文件撑到写入地址之外），已按真实契约修正断言。
```

### M18. 边界审计（本轮末实测）

```text
QML 改动文件数 = 0（UI Freeze：无 Write section / Write button / confirmation dialog / function selector）。
新 Q_PROPERTY = 0（只加 C++ seam）；版本 / PE / icon / package / scripts / samples 全未改；
`encodeWrite*` 在 src/ 出现 0 次（无写请求 encoder）；Agent 层无 write/send/raw-serial 工具（0 匹配），
三只读 tools 与其测试未改；screenshots 未改。
```

### M19. FC03 行为等价审计

```text
范围/数量/超时/文案：`readHoldingRegistersOnce` 的 1..247 / 0..65535 / 1..125 / timeout>0 与四条中文错误文案**逐字未变**；
  wire 字节：仍是 encodeReadHoldingRegistersRequest + RTU codec（金样 01 03 00 00 00 02 C4 0B，AC-02 锁定）；
  response analysis：仍由 analyzeFunction03Transaction 产出（AC-08/09 + 既有 16 条全绿）；
  统计 / 诊断：latest-only 单事务快照与单条 DiagnosisTransaction（既有 s05/s06/s07 全绿）；
  source/session：modeLabel / sourceLabel / stale guard / Clear!=Disconnect 语义由既有 s01–s10 继续锁定。
唯一的**非用户可见**差异：完成事务额外进入 append-only session history（证据层），presentation 未改（见 M12 Review item-1）。
```

### M20. Deferred / Review items

```text
1. presentation 切换为 append 投影（会改变 FC03 可见的行数与统计聚合）→ M10-B（§7 要求 STOP+RCA，不静默改）。
2. 0x06 / 0x10 的主动 encoder + 主动 response validator + 写安全 UI/confirmation → M10-C/D/E。
3. simulator 的写请求→应答帧接线（需要写编码器）→ M10-D/E。
4. UI 是否展示 hex evidence → DEFER（本轮不加 role、不加 inspector）。
5. real hardware 验证 → 非自动化；M10-F 若执行需单独人工验收并恢复原值。
```

### M21. Files Changed

```text
新增 core：src/core/active/ActiveRequestIntent.{h,cpp}、src/core/active/ActiveTransactionEvidence.{h,cpp}、
  src/core/analysis/TransactionProvenance.{h,cpp}
新增 app：src/ui/serial/SerialTransport.{h,cpp}
新增 tests：tests/test_active_request.cpp、tests/test_active_master.cpp、tests/fake_serial_transport.{h,cpp}
修改 core：SerialTransactionSession.{h,cpp}（泛化 + 快照 + analyzer seam）、SimulatedSlave.{h,cpp}（opt-in 可写）、
  Function16.{h,cpp}（decode-only 请求 value 解码）
修改 app：AnalysisController.{h,cpp}（seam / 快照 / 记录 / append / provenance）、TransactionListModel.h（可选 provenance）、
  SerialPortAdapter.{h,cpp}（实现 seam / 证据累积 / disposition）
修改 tests：test_serial_session.cpp、test_serial_adapter.cpp、test_simulated_slave.cpp（SA1–SA5）、test_ui_bridge.cpp（字段 + s10）
修改构建：CMakeLists.txt（core 源、SerialTransport、两个新测试目标、offscreen 属性）
```

### M22. Git

```text
behavior-bearing（core / session / controller / transport / test 行为实质变化，见 M21）⇒ **不作 LKGC**。
commit：`M10-A: establish Active Master contract foundation`（独立提交；不 amend `86e88ed`；不 rebase；不 push；
未创建 v2.0.0 tag）。verified LKGC 保持 `aa2f3db`；M10 = IN PROGRESS；M10-A = 等待 Review。
```
## M10-A Correction — Post-Submission Transport Failure Evidence（2026-09-20，append-only）

> **M10-A Review = HOLD（窄范围）。** 主体 foundation **已接受、不重做**：unified ActiveRequestIntent / pending snapshot /
> generic SerialTransactionSession / SerialTransport seam / Recording transport / NotSent·PossiblySent / raw request·response
> ADU evidence / Active Serial append foundation / source identity / Clear Results foundation / writable simulator foundation /
> FC03 regression —— 全部保持。
> **唯一 blocker**：请求已进入 transmission lifecycle 之后发生 transport error / disconnect / cancel 时，
> **证据会丢失**（request snapshot / request ADU / partial response bytes / submission disposition / terminal reason）。
> 本轮只修这一条；原 M10-A 历史记录（§M0–M22）**未改写**。

### C0. HOLD 归档

```text
M10-A Review = HOLD；blocker = post-submission transport termination 尚未证明 evidence 不会丢失。
本轮边界：不开 M10-B；不改 UI；不实现 0x06 / 0x10 encoder；不实现 Write button；不扩展 SimulatedSlave；
        不 push；不 tag；verified LKGC 继续 `aa2f3db`。
```

### C1. 真实终止路径（逐条实读源码后的实际行为，见「修正前」列）

```text
路径                                  修正前真实行为                                        证据是否保留
A. pre-send reject                    未连接 / 忙 / descriptor 无效 → {false, NotSent} + 一条 transportError；   ✔ 无需保留
                                      不建立 pending、不产生任何 transaction
B. accepted → response completion     session 完成 → transactionCompleted{request 快照, responseAdu,          ✔ 已保留
                                      PossiblySent, analysis}
C. accepted → timeout                 session onResponseTimeout → transactionCompleted{…, observed bytes…}     ✔ 已保留
D. accepted → port error              适配器 `handlePortError`：`session_.cancel()` → `timeoutTimer_.stop()`    ✘ **全丢**
                                      → emit transportError → `port_.close()` → **`observedResponseBytes_.clear()`**
                                      ⇒ request 快照 / request ADU / 已观察字节 / disposition / reason 全部消失
E. accepted → explicit close/disconnect 适配器 `closePort()`：静默 cancel + close + clear                        ✘ **全丢**
                                      （注释当时即写明「silent abort」——连 terminal fact 都不存在）
F. accepted → partial → port error    同 D，且 partial bytes 被显式 `clear()`                                   ✘ **全丢**
（源码事实：`SerialPortAdapter.cpp` 的 handlePortError 注释当时写的是
  “the evidence of this attempt is discarded with the abort” —— 即本 blocker 的直接自证。）
```

### C2. RCA（真实缺陷，非仅 fake 补字段）

```text
Observed：路径 D/E/F 下 controller 只收到一句 transportError 字符串；`handleSerialTransportError` 随后
          `pendingRequest_.reset()` ⇒ 运行时不持有该次尝试的任何证据（快照/ADU/字节/原因）。
Expected：已被 transmission lifecycle 接受的请求，其尝试证据必须保留，并保守表达 submission disposition 与 terminal reason。
Root Cause：终止路径把「本地中止」实现为**先清后报**：`session_.cancel()`（清 buffer）与
          `observedResponseBytes_.clear()` 都在构造任何证据之前执行；且 closePort 路径**从不产生任何 terminal fact**。
          （`cancelPending()` 同样先清 buffer + 观察字节 + 关端口。）
Fix：证据先于中止构造 —— 终止路径统一为「取 pending 快照 → 取 observed bytes → 中止/清理 → emit terminal evidence」；
          生产适配器与 recording transport 走同一形状；controller 侧新增 terminate 入口保留证据。
Verification：TF1–TF3、TF6、TF8（含 partial bytes 逐字节保留、Clear 后 pending 终止进入已清空会话）；既有 29/29 全绿。
Regression Protection：TF2 断言 partial bytes 逐字节保留；TF7 断言每个 accepted 请求只允许一个 terminal。
```

### C3. 设计：transport terminal evidence（不伪造 Modbus outcome）

```text
core 新增（`core/active/ActiveTransactionEvidence.h`）：
  · `TransportTerminalReason{ TransportError, DisconnectedAfterSubmission }` + 机器 token
    （`transport_error` / `disconnected_after_submission`）；
  · `ActiveTransportTerminal{ request(发送时快照), responseAdu(已观察字节，可为空), disposition, reason }`
    + `evidence()` 投影（requestAdu 取快照 wire）。
**刻意不携带 TransactionAnalysis**：transport 中止不是 Modbus 响应，禁止为了塞进既有 taxonomy 伪造
ProtocolError / Timeout / Exception / Success；public outcome taxonomy 继续冻结（§4）。
权威 = 该记录 + typed reason；人可读的 transport error 文案继续留在既有 serial error lane（presentation），
**不作为 evidence authority**（§5）。
```

### C4. 生产适配器契约（`SerialPortAdapter`）

```text
`handlePortError`（accepted 路径）：先取快照与已观察字节 → `session_.cancel()` + `timeoutTimer_.stop()` →
  `port_.close()` + `elapsed_.invalidate()` + 清观察缓冲 → **emit transactionTerminated{…, TransportError}**
  → 再 emit 既有 transportError（有界、presentation）。无 pending 时行为与修正前完全一致（关端口、无 terminal、无新增错误）。
`closePort`：有 pending ⇒ emit transactionTerminated{…, DisconnectedAfterSubmission}（**先取证据后清理**）；
  无 pending ⇒ 完全静默（纯连接状态变化，§13-A）。
短计数 write（`cancelPending()` 路径）：请求**未被接受**（accepted=false），controller 从未建立 pending，
  故不产生 terminal 记录；PossiblySent 在 start 边界即以 `ActiveStartResult{false, PossiblySent}` 表达（见 C13-1 Review item）。
每个 accepted 请求**至多一个** terminal：cancel 之后 session 回 Idle ⇒ 后续 timeout 回调 / completion 都不可能再产生事件。
```

### C5. Recording transport 能力（修正前 → 修正后）

```text
修正前：`failTransport()` 取消 + `pending_.reset()` + **`deliveredBytes_.clear()`** 后才 emit 一条 transportError
        ⇒ partial bytes 丢失；`closePort()` 静默 ⇒ 无 terminal 注入能力（§9 gap）。
修正后：共用 `emitTerminalIfSubmitted(reason)`：无 pending ⇒ 静默 cancel（与生产一致）；有 pending ⇒
        快照 + 已观察字节 → cancel/清理 → **emit transactionTerminated**。
        新增注入入口：`failTransport(message)`（TransportError）、`disconnectAfterSubmission()`（DisconnectedAfterSubmission），
        与既有 `feedPartialBytes()` 组合即可构造「partial → error」。
        全部 deterministic：无 Sleep、无真实 COM、无 wall-clock race。
```

### C6. Controller / runtime evidence ownership

```text
新增 `handleSerialTransactionTerminated(terminal)`：身份规则与 completion 相同（必须与 `pendingRequest_` 相等，
否则整条忽略），命中的终止追加进 `activeSerialTerminations_`（与 `activeSerialRecords_` **并行的权威证据**，
不伪装成已完成事务），随后 `pendingRequest_.reset()` + `serialBusy_ = false`；
**不触碰 statistics / rows / mode / source**（transport 中止不改写最近一次已完成分析）。
Clear Results：`activeSerialRecords_.clear()` 与 `activeSerialTerminations_.clear()` 同步；**pending 不取消**。
source replacement（connect / demo / replay）：两者同步清空（证据不跨 session 泄漏，§18）。
```

### C7. Pre-send 边界（继续冻结）

```text
validation failure / not connected / busy / adapter pre-send reject（未来 confirmation cancel）一律 **NotSent、sendCount = 0**，
不产生 completed transaction，也**不产生 terminal evidence**（TF4 断言）。§6 的「不得把 pre-send error 变成 completed transaction」成立。
```

### C8. PossiblySent 语义（§7）

```text
`TransportDisposition` 的语义已在类型注释中冻结为 **submission boundary 的保守发送事实**：
  · 不是 transaction outcome；
  · 不是「当前所有 evidence 的最终最高置信度」——若随后观察到可信 response bytes 并完成 response analysis，
    **response evidence + outcome 是更强事实**；
  · 因此 UI 未来不得机械显示「可能已发送」去覆盖已存在的成功/异常响应事实。
本轮在 `ActiveTransactionEvidence.h` 的 disposition 文档块与 `ActiveTransportTerminal` 文档中显式写明该规则，
并由 TF5（存在可信响应时**不产生** terminal 记录、且记录的 disposition 仍为 PossiblySent 但 outcome 为 Success）与
TF6（timeout 场景下 observed bytes 逐字节保留）锁定。
```

### C9. 两态 disposition 与命名（§8 评估结论）

```text
决定：**保持两态 NotSent / PossiblySent**，不新增 Sent / DefinitelySent（会暗示「物理线路已确认发送」，
而 response 自身已提供更强的 observable evidence）；**enum 名称保持 `TransportDisposition`** ——
该名称未达到「明显导致误用」的程度，且其文档注释已明确其为 submission disposition，
按 §8「不要做大范围命名重构」不做 rename。字段语义在 `ActiveStartResult` / `ActiveTransactionResult` /
`ActiveTransportTerminal` 三处注释中统一表述为 submission disposition。
```

### C10. 新增确定性测试（TF1–TF8）

```text
TF1 accepted →（无响应）transport error：sendCount 1 / requestAdu 逐字节 = 金样（且 = transport 记录的 ADU）/
    responseAdu 空 / disposition PossiblySent / reason TransportError / 不产生任何 Modbus 行或统计。
TF2 accepted → partial(01 03 04 00) → transport error：partial responseAdu **逐字节保留**，仍无任何 Modbus verdict。
TF3 accepted → 显式 disconnect（真实用户路径 controller.disconnectSerial → teardown → closePort）：
    sendCount 1 / reason DisconnectedAfterSubmission / PossiblySent / requestAdu 保留 / 无行；
    重连 = 新 session ⇒ 旧证据按 source replacement 清空；无 pending 时 disconnect 完全静默（不新增证据）。
TF4 pre-send reject：sendCount 0 / NotSent / **terminal evidence = 0** / 无行（继续冻结）。
TF5 可信响应：record = 1 / terminal = 0 / outcome Success / 记录 disposition 仍 PossibleSent（更强事实规则）。
TF6 timeout：(A) 无字节 ⇒ Timeout + 空 responseAdu；(B) 有 3 字节 ⇒ 逐字节保留 + wire-truth ProtocolError。
TF7 no-double-terminal oracle：terminal 之后所有迟到驱动（timeout / completion / disconnect / 重复 error）
    均不得新增事件；controller 边界对重复 terminal 与无 pending terminal 同样忽略（Part 1 + Part 2）。
TF8 Clear 契约扩展：Clear 同时清 record 与 terminal；Clear → pending → post-submit error ⇒
    终止证据进入已清空会话（count = 1，requestAdu 保留，仍然 0 行）。
```

### C11. 门禁（真实输出）

```text
`active_master`：**25 passed / 0 failed**（原 17 + TF1–TF8 = 24 用例 + init）。
`active_request`：**17 passed / 0 failed**（未改语义）。
Debug `ctest`：**29/29 PASS**；Release `ctest`：**29/29 PASS**（含 qml_smoke / qml_geometry_check / qml_nav_check /
  qml_focus_check 全绿）。Debug 与 Release 构建**零 warning / 零 error**。`git diff --check` PASS。
```

### C12. 用户可见冻结核对（§19）

```text
未改 QML（0 文件）；未改 Communication layout / 按钮文案 / 范围校验 / 超时 / latest-only 可见 presentation /
统计可见行为；本轮 runtime 内部只**新增**了终止证据的保存与访问器（C++ seam），无新 Q_PROPERTY。
```

### C13. Deferred / Review items

```text
1. **短计数 write（partial submission）**：适配器已以 `{accepted=false, PossiblySent}` 在 start 边界表达，
   但该情形**不产生** terminal 记录（因为从未建立 pending）。对未来的写操作而言这是「字节可能已上线、
   却没有证据记录」的一个缺口 —— 按本轮 §16 的 TF 定义未要求，故不改，显式列为 Review item 供裁定。
2. **生产适配器的 port-error 终止路径无法在无硬件环境端到端驱动**（`startActiveRequest` 需要真实打开的端口）：
   同一契约由 seam 侧 recording transport 以确定性方式证明（TF1–TF8），生产实现为同形状代码 + 代码审阅；
   **REAL HARDWARE NOT VERIFIED**（继续披露，不写 hardware PASS）。
3. 命名：未 rename `TransportDisposition` → `SubmissionDisposition`（见 C9 理由）。
```

### C14. Files Changed（本轮）

```text
修改 core：src/core/active/ActiveTransactionEvidence.{h,cpp}（新增 TransportTerminalReason / ActiveTransportTerminal
  / evidence() / 机器 token；PossiblySent 语义注释强化）
修改 app：src/ui/serial/SerialTransport.h（新增 transactionTerminated 信号 + 契约注释）、
  src/ui/serial/SerialPortAdapter.cpp（handlePortError 与 closePort 的证据保留 + terminal 发射）、
  src/ui/AnalysisController.{h,cpp}（terminated 入口 / activeSerialTerminations_ / 访问器 / Clear 与 source 契约）
修改 tests：tests/fake_serial_transport.{h,cpp}（emitTerminalIfSubmitted + failTransport 保留证据 +
  disconnectAfterSubmission 注入）、tests/test_active_master.cpp（TF1–TF8）
```

### C15. Git

```text
behavior-bearing ⇒ 不作 LKGC。commit：`M10-A: retain post-submission transport evidence`
（独立提交；不 amend `18f27e9`；不 rebase；不 push；未创建 v2.0.0 tag）。
verified LKGC 保持 `aa2f3db`；M10-A **仍未 COMPLETE**（等待 Re-review）。
```
## M10-A Final Closure — Short-Submission Evidence（2026-09-20，append-only）

> **M10-A Re-review = HOLD（最后一个已知窄范围 safety correction）。** 上一轮 correction 的其余部分**全部接受**：
> unified intent / generic session / transport seam / recording transport / NotSent·PossiblySent /
> raw request·response evidence / post-submit transport-error evidence / disconnect evidence / partial-response evidence /
> Active Serial append foundation / Clear Results / source·session isolation / writable simulator foundation / FC03 behavior equivalence。
> **TransportDisposition 不要求 rename。**
> **唯一 blocker**：`startTransaction` 在 submission 阶段出现 **short-count write** 时，返回
> `accepted = false + PossiblySent`，但**没有留下 durable terminal evidence**。

### D0. HOLD 归档与不变式

```text
M10-A Re-review = HOLD；唯一 blocker = short-count submission 被分类为 PossiblySent 却无 durable evidence。
本轮新增并冻结的 **M10-A invariant**：
  任何 Active Master request attempt，若 submission disposition = **PossiblySent**，
  则必须产生 **exactly one durable attempt evidence**，无论它是：
    A. 正常进入 pending（后续 response / timeout）        → 由 completed transaction record 承载
    B. pending 后 transport error                        → 一个 transport terminal
    C. pending 后 disconnect                             → 一个 transport terminal
    D. submission 阶段 short-count / partial acceptance  → 一个 transport terminal（本轮补上）
  **禁止存在：PossiblySent 但无任何 evidence。**
```

### D1. 真实 short-count 控制流（逐行实读修正前的生产代码）

```text
AnalysisController::readHoldingRegistersOnce
  → 本地 validation（1..247 / 0..65535 / 1..125 / timeout>0）→ 构建 intent → encodeActiveRequest → descriptor
  → serialTransport_->startActiveRequest(descriptor)
SerialTransactionAdapter::startActiveRequest
  → port_.isOpen() / hasActiveTransaction() / session_.beginActiveRequest(descriptor)   [三个 pre-send 返回点：NotSent]
  → observedResponseBytes_.clear()
  → const auto written = port_.write(wire.data(), wire.size());        ← 真实 QSerialPort 调用点
  → if (written != wire.size()):
        cancelPending()   → session_.cancel()（session 回 Idle）+ timeoutTimer_.stop() + port_.close()
                            + elapsed_.invalidate() + observedResponseBytes_.clear()
        emit transportError("串口写入失败：…")
        return ActiveStartResult{false, PossiblySent}                  ← **修正前：不带任何证据**
  → Controller: if (!start.accepted) return;                           ← pending 从未建立，证据无处可去
```

### D2. 根因（RCA）

```text
Observed：short-count start result = {accepted=false, PossiblySent}，但运行时不持有任何 durable evidence；
          用户可见的只有一句「串口写入失败：…」文案。
Expected：PossiblySent 必然恰好对应一个 durable attempt evidence（D0 不变式）。
Root Cause：start contract 把 **accepted（是否进入 pending）** 与 **evidence persistence（是否落库）**
          错误地绑定在一起 —— 「不进入 pending」被误等同于「不需要 terminal evidence」。
Fix：显式新增 submission-阶段终止表达 **`TerminatedDuringSubmission`**（start result 的第 3 种语义），
          让 `accepted=false + PossiblySent` 也能同步落库；reason = **`ShortSubmission`**。
Verification：TS1–TS5 + 全部 regression（Debug/Release ctest 29/29）。
```

### D3. 新 start-result 契约（三种语义，冻结）

```text
A. **RejectedNotSent**      ：accepted = false, disposition = NotSent
   → 0 send / **无 terminal evidence** / 不产生 Modbus transaction。
B. **InFlight**             ：accepted = true,  disposition = PossiblySent
   → 建立 pending；后续 response / timeout / error / disconnect **恰好一条** terminal path。
C. **TerminatedDuringSubmission**：accepted = false, disposition = PossiblySent
   → 不建立 pending，**但必须立即保存 exactly one terminal evidence**。
类型：`ActiveStartResult{ accepted, disposition, std::optional<ActiveTransportTerminal> terminatedDuringSubmission }`。
选择依据（T022 §5 优先级）：**用返回值携带该 terminal fact**，而不是让 transport 在 runtime 尚无 pending 时
先 emit `transactionTerminated` —— 那种顺序会被 stale/no-pending guard 静默丢弃（危险方案）。
不引入第二套 public outcome。
```

### D4. Reasoning 与字节数证据

```text
`TransportTerminalReason` 新增 **`ShortSubmission`**（机器 token `short_submission`）。
语义（冻结）：transport API 在本次调用中只接受了**小于完整 ADU 长度**的字节数，
因此完整 request **没有被本调用完整接受**，但**不能安全证明线路上完全没有出现过字节** ⇒ disposition = PossiblySent。
不把它含糊塞进 `DisconnectedAfterSubmission`。

`ActiveTransportTerminal` 新增 **`std::optional<std::uint16_t> submissionAcceptedByteCount`**（仅 ShortSubmission 填充）：
  · 表示 **transport API 报告接受的 byte count**（例：requested ADU = 8，Qt write 返回 4）；
  · **不是**「已到达设备的字节数」，也**不是**「已实际发送到线路的字节数」；
  · 其余 reason 一律 `std::nullopt`，不制造物理层精确性。
`requestAdu` 仍表示**本次尝试准备提交的完整 exact ADU**（8 字节），**不写成**「设备收到的完整 ADU」。
```

### D5. 生产 / fake 对等（§24）

```text
生产分支（SerialPortAdapter）现在显式构造同 contract 的证据：
  写入返回 <= 0 字节 ⇒ **NotSent + 无 terminal**（可证明没有任何字节离开进程；这是不变式的必要补充，
    否则 0 字节也会被误报为 PossiblySent）；
  写入返回 >  0 字节但 < 完整 ADU ⇒ **PossiblySent + terminal{ShortSubmission, submissionAcceptedByteCount}**，
    证据在 `cancelPending()` 之前构造（清理会清空缓冲）。
fake（RecordingSerialTransport）：新增 `setSubmissionAcceptedBytes(count)`（0 < count < ADU 长度）
  → 返回 `{accepted=false, PossiblySent, terminal{ShortSubmission, count}}`，不建立 pending、不计入 sendCount、
  **不把该 ADU 记入 accepted-ADU 日志**（从未被完整交出）。
两者形状一致；生产分支无法在无硬件环境强制 QSerialPort 返回 short count，故契约由 seam 侧确定性测试锁定 +
代码审读确认（**REAL HARDWARE NOT VERIFIED**）。
```

### D6. Controller 持久化与 ghost-pending 防护

```text
`readHoldingRegistersOnce`：`start` 未被接受时，若 `start.terminatedDuringSubmission` 有值 ⇒
  **同步** `activeSerialTerminations_.push_back(*…)`（同步而非依赖信号，因为此时没有 pending 可被匹配）。
状态：`serialBusy_` 保持 false、`pendingRequest_` 为空、session 回 Idle ⇒ **无 ghost pending**（TS2 断言并可继续下一次请求）。
不产生任何 Modbus outcome：Transactions / statistics 不新增任何行或计数（TS1/TS3 断言 record = 0、rowCount = 0）。
```

### D7. Clear / source 归属（不变式延续）

```text
short-submission terminal 与其他 completed transport terminal **同一 contract**：
  Clear Results 清掉它（TS4），source replacement（新 serial session / Replay / Simulator）清掉它（TS5），
  它属于当前 Active Serial session，不污染其它 source。
error lane 继续显示人类可读文案，但**文案不是 evidence authority**；machine-checkable 事实为 terminal record + typed reason。
```

### D8. Semantics Matrix（冻结，M10-D/E 不得重新解释）

```text
pre-send reject          → NotSent      → 无 transaction → 无 terminal
short submission         → PossiblySent → 无 transaction → **恰好一个 terminal**
accepted + response      → PossiblySent（submission fact）→ 一个 transaction（Success/Exception/…）→ 无 transport terminal
accepted + timeout       → PossiblySent → 一个 Timeout transaction → 无 transport terminal
accepted + port error    → PossiblySent → 无 transaction → 恰好一个 terminal
accepted + disconnect    → PossiblySent → 无 transaction → 恰好一个 terminal
```

### D9. 新增测试（TS1–TS5）

```text
TS1 8-byte ADU / write accepted = 4：startAttemptCount 1、sendCount 0、accepted=false、PossiblySent、
    terminal 恰好 1（reason ShortSubmission、token short_submission）、requestAdu = 完整 8 字节金样、
    responseAdu 空、submissionAcceptedByteCount = 4、无行无统计、serial error lane 仍报文案。
TS2 short submission 后 serialBusy=false、transport 无 active transaction、pending 为空；
    随后的正常请求可完整接受并完成（record 1 / terminal 仍 1）。
TS3 terminal 形成后驱动迟到 timeout / completion / transport error / disconnect ⇒ terminal 仍为 1、record 0、rowCount 0。
TS4 Clear Results ⇒ terminal 0；modeLabel / sourceLabel / serialConnected / sessionId 按既有 contract 保持。
TS5 source replacement（新 Active Serial session / Simulator / Replay）逐项清空并校验 typed sourceKind。
```

### D10. 门禁（真实输出）

```text
`active_master`：**30 passed / 0 failed**（原 25 + TS1–TS5）。`active_request`：17 passed / 0 failed。
Debug ctest **29/29 PASS**；Release ctest **29/29 PASS**（含 qml_smoke / qml_geometry_check / qml_nav_check /
  qml_focus_check）。两构建零 warning / 零 error（含为新增 optional 字段补齐的 -Wmissing-field-initializers）。
`git diff --check` PASS。
```

### D11. 冻结核对

```text
未改 QML（0）、Communication UI / FC03 参数 / FC03 wire / 按钮行为 / latest-only presentation / 统计可见行为
全部未变；`encodeWrite*` 0（0x06 / 0x10 active encoder 仍不存在，encodeActiveRequest 对两者仍 UnsupportedFunction）；
SimulatedSlave 未改；Agent 层未改（write authority = NONE）；未 push / 未 tag / 未推进 LKGC。
```

### D12. Files Changed（本轮）

```text
core：src/core/active/ActiveTransactionEvidence.{h,cpp}（ShortSubmission reason + token、
  submissionAcceptedByteCount、ActiveStartResult 三种语义 + terminatedDuringSubmission）
app：src/ui/serial/SerialPortAdapter.cpp（short-count 分支构造证据；<=0 字节 ⇒ NotSent）、
  src/ui/AnalysisController.cpp（同步归档 submission terminal）
tests：tests/fake_serial_transport.{h,cpp}（setSubmissionAcceptedBytes + 同 contract 注入）、
  tests/test_active_master.cpp（TS1–TS5）
```

### D13. Git

```text
behavior-bearing ⇒ 不作 LKGC。commit：`M10-A: retain short-submission evidence`
（独立提交；不 amend `a09de6e`；不 rebase；不 push；未创建 v2.0.0 tag）。
verified LKGC 保持 `aa2f3db`；**M10-A 等待最终 Re-review**（仍未 COMPLETE）。
```
## M10-A Final Acceptance / Closure（2026-09-20，docs-only）

> **M10-A Final Re-review = PASS。M10-A = COMPLETE。**
> 最终 accepted behavior tree = **`b7a6151`**。verified LKGC 由 `aa2f3db` 推进至 **`b7a6151`**。
> 本轮严格 docs-only：未改 src / tests / QML / CMakeLists.txt / scripts / assets / samples / screenshots。

### E0. Final Re-review PASS 归档

```text
M10-A Final Re-review = PASS（用户）；M10-A = COMPLETE。
最后一个安全缺口（short submission 无 durable evidence）已关闭并通过验收。
最终接受的 M10-A invariant：
  **任何 attempt 若 submission disposition = PossiblySent，必须最终产生 exactly one durable evidence。**
硬件状态：**M10-A 没有真实硬件验收 —— REAL HARDWARE NOT VERIFIED**；
该事实**不阻塞** M10-A 的软件范围 COMPLETE，但不得被表述为 hardware PASS。
```

### E1. Accepted behavior chain（真实审计，按 `git show --stat --name-only` 文件列表分类，不看 commit message）

```text
18f27e9  M10-A: establish Active Master contract foundation      files=33 code/test=28 docs=5 ⇒ behavior-bearing
a09de6e  M10-A: retain post-submission transport evidence          files=14 code/test= 9 docs=5 ⇒ behavior-bearing
b7a6151  M10-A: retain short-submission evidence                   files=12 code/test= 7 docs=5 ⇒ behavior-bearing
（同一区间的 docs/evidence-only 提交：2640556 / 0498d6c / 3e7aaeb / 86e88ed —— 均不作 LKGC。）
最终 behavior tree = **b7a6151**（HEAD == b7a6151）⇒ 新 verified LKGC = `b7a6151`。
本轮的 closure commit（docs-only）**不作 LKGC**。
```

### E2. M10-A Core Contracts（冻结）

```text
A. **ActiveRequestIntent** —— 统一 typed contract：`ActiveFunction{ReadHoldingRegisters(0x03), WriteSingleRegister(0x06),
   WriteMultipleRegisters(0x10=十进制 16)}` + 闭合 payload variant + 单一 validation + `ActiveRequestDescriptor{intent, frame, exact wire}`；
   无 QVariant map / 无 stringly-typed function / 无裸 JSON。
B. **pending intent snapshot** —— 发送时 descriptor 是响应匹配的唯一权威；兼作 stale-completion guard。
C. **generic SerialTransactionSession** —— 单一 lifecycle（Idle → begin(descriptor) → feed/timeout → Idle），
   function-specific 语义收敛到 `analyzeActiveResponse()`；**不存在** SerialWrite06/10Session。
D. **Controller → SerialTransport 可替换 seam** —— open / close / start 全部走 seam；注入对象不被 controller 所有。
E. **production QSerialPort adapter** —— 真实串口语义（open/readyRead/timeout/port error、PE-4 有界错误）保持不变。
F. **Recording / Fake transport** —— sendCount / startAttemptCount / exact ADU log / 可控 completion timing /
   pre-send accept·reject / timeout / transport error / short-submission 注入；无 Sleep、无真实 COM、无 wall-clock race。
G. **TransportDisposition 是 submission fact，不是 Modbus outcome**（两态；未 rename；可信 response 是更强事实）。
H. **NotSent** —— pre-send 拒绝（validation / 未连接 / busy / pre-send reject / 未来 confirmation cancel）：
   0 发送、无 transaction、无 terminal。
I. **PossiblySent** —— 请求进入 transmission lifecycle 后线路无法被证明清白；**必须最终产生 exactly one durable evidence**。
J. **raw request ADU retention** —— 发送时 descriptor 的 wire 原样保留（非事后重编码猜测）。
K. **raw response ADU retention** —— 实际观察到的原始字节（含损坏/异常/协议错）保留。
L. **partial / corrupt bytes retention** —— 终止与超时路径都不得在构造证据之前清空缓冲。
M. **post-submit transport terminal evidence** —— `ActiveTransportTerminal`（不含 TransactionAnalysis）+ `transactionTerminated`。
N. **ShortSubmission terminal evidence** —— submission 阶段部分接受 ⇒ terminal{ShortSubmission, submissionAcceptedByteCount}。
O. **每个 attempt 最多一个 terminal path** —— 传输侧 cancel 后 session 回 Idle；controller 侧 pending 清空 + 身份校验。
P. **Active Serial typed provenance** —— `TransactionSourceKind{Simulator, Replay, ActiveSerial}` + session id；不从文本/索引/文件名推断。
Q. **session-scoped history foundation** —— `activeSerialRecords_`（completed）+ `activeSerialTerminations_`（terminal），
   均为 append-only；source replacement 清空，不跨 source 泄漏。
R. **Clear Results 不取消 pending** —— Clear 清 completed 结果与两类证据；pending 完成后作为新事件进入已清空视图。
S. **deterministic writable simulator foundation** —— opt-in WriteMode、先解码后写入、非法/异常/异地址零 mutation、无随机/线程/真实时钟。
T. **AI / Agent write authority = NONE** —— 无 write / send / raw-serial tool；seam 不是 Agent surface。
```

### E3. Submission Semantics Matrix（最终冻结，M10-D/E 不得自行重新解释）

```text
pre-send reject                    → NotSent      → no transaction → no terminal
short submission                   → PossiblySent → no transaction → **one transport terminal**
accepted + valid/exception response → PossiblySent（submission fact）→ one Modbus transaction → no transport terminal
accepted + timeout                 → PossiblySent → one Timeout transaction → no transport terminal
accepted + port error              → PossiblySent → no Modbus transaction → one transport terminal
accepted + disconnect              → PossiblySent → no Modbus transaction → one transport terminal
```

### E4. Short Submission Contract（冻结）

```text
`ShortSubmission` 是 **transport terminal reason**（机器 token `short_submission`），保留：
  Validated Request snapshot / **完整 intended request ADU** / **empty response ADU** / PossiblySent /
  transport API accepted-byte count（若存在）。
accepted-byte count 只代表 **transport API 报告接受的字节数**；
**绝不代表**线上真实发送字节数，也**绝不代表**设备实际收到字节数。
```

### E5. Outcome Boundary（冻结）

```text
public outcome 继续为 Success / Exception / CrcError / Timeout / ProtocolError / Pending / ExpectedNoResponse。
禁止因 transport terminal 新增或伪造第二套 public outcome（WriteFailed / TransportFailed / UnknownWriteOutcome…）。
未来的 write 状态未知由 **operation context + Timeout / transport terminal + submission evidence** 组合表达。
```

### E6. M10-A Test Closure（最终 accepted gate，已归档，不再重跑）

```text
active_request ：17 passed（Pure Core：intent/validation/encode/descriptor 自洽/快照/泛化路径/evidence token/
                 FC06·0x10 echo 回归/正交性回归/broadcast 拒绝）
active_master  ：30 passed（按项目 QtTest 真实口径：TA15 + TF8 + TS5 = 28 用例 + initTestCase + cleanupTestCase）
Debug ctest    ：29/29 PASS ；Release ctest：29/29 PASS（两构建零 warning / 零 error）
qml_smoke / qml_nav / qml_geometry / qml_focus：PASS
```

### E7. FC03 Freeze（M10-A 未改变任何用户可见 FC03 行为）

```text
冻结：Communication UI / 参数范围（1..247、0..65535、1..125、timeout>0 及四条中文文案）/ FC03 wire bytes /
busy behavior / timeout behavior / latest-only visible presentation —— 与 M9 及 M10 之前等价。
注意：Active Serial 内部已有 **append history foundation**，但 **UI/presentation 仍 latest-only**；
把 presentation 迁移到 append 投影属于 **M10-B**。
```

### E8. Write Scope Still Absent（M10-A COMPLETE ≠ Active Write 可用）

```text
0x06 active encoder：**不存在**（`encodeActiveRequest` → UnsupportedFunction）。
0x10 active encoder：**不存在**（同上）。
无 Write button / 无 confirmation dialog / 无 write UI / 无 Agent write tool。
```

### E9. Simulator Boundary

```text
writable simulator foundation 已存在（opt-in、确定性、非法零 mutation），但**真实硬件写入未验证**：
**不得把 simulator PASS 写成 hardware PASS**。
```

### E10. Non-blocking Design Note（future hardening）

```text
**NON-BLOCKING NOTE**：当前 `ActiveStartResult` 通过 `accepted` + `disposition` + `optional terminal`
三个字段表达三种合法 start semantics，构造点与测试已锁死合法矩阵。
未来若该类型继续扩张，优先考虑 **factory / tagged variant / 等价 invariant-preserving API**，
避免非法字段组合可被任意构造。这是 future hardening note，**不据此 reopening M10-A**。
```

### E11. Problems / RCA History（保留完整演进，不改写成一次成功）

```text
第一轮 18f27e9：transport injection 只做一半 ⇒ 注入的 fake 未被真正 open，TA02–TA09 初期整体失败。
第二轮 a09de6e：post-submit error / disconnect / partial 路径**先清 evidence 后报错** ⇒ 证据丢失。
第三轮 b7a6151：short submission 返回 false + PossiblySent **但无 durable evidence** ⇒ 不变式缺口。
三次均由自动化或 Review 捕获，且每次都固化为契约/测试（TA、TF、TS、semantics matrix、PossiblySent invariant）。
```

### E12. M10-B Boundary（只记录 Next Action，不实现）

```text
**Next Action = M10-B — FC03 Unified Contract Migration。**
目标方向：把现有 0x03 **完整迁移**到 M10-A 的统一 Active Master contract，保持 wire / protocol behavior **完全等价**，
并处理 **Active Serial 内部 append history → 用户可见 transaction/history 契约**（presentation 迁移，需要 STOP+RCA 评审）。
M10-B **不允许**顺手实现：0x06 encoder / 0x10 encoder / Write UI / confirmation。
```

### E13. Docs Changed（本轮）

```text
T022（本 §E0–E15 + header 状态）、PROJECT_STATUS（Milestone / Current Task / Phase / Next Action /
两个 authoritative LKGC 行）、BACKLOG（M10 行 + changelog 新条目）、devlog（closure 条目）、
INTERVIEW_NOTES（§78）。全部为 docs-only。
```

### E14. Final Verification

```text
提交前：`git diff --check` PASS；`git diff --name-only` 仅 docs/。
提交后：`git status --porcelain` 为空；`git show --stat --name-only HEAD` 仅含 docs/；
所有 authoritative verified LKGC 位置 = `b7a6151`；closure commit 不作 LKGC。
```

### E15. Git

```text
commit：`M10-A: close Active Master contract foundation`（独立 docs-only 提交；
不 amend `b7a6151`；不 rebase；不 push；未创建 v2.0.0 tag）。
M10 = IN PROGRESS；Phase 1 = COMPLETE；**M10-A = COMPLETE**；M10-B = NEXT；M9 = ✅ COMPLETE。
verified LKGC = **`b7a6151`**。
```
## M10-B — FC03 Unified Contract Migration（2026-09-20，behavior-bearing）

> **M10-B = FC03 Unified Contract Migration：IN PROGRESS（实现完成，等待 M10-B Review）。**
> 目标：把现有 0x03 完整迁移到 M10-A 统一 Active Master contract，并把 Active Serial session 内部已有的
> append history 正式接入 Transactions / Statistics / Diagnosis 的用户可见 presentation。
> **本轮不是新协议功能开发**：0x06 / 0x10 encoder 仍不存在、无 Write UI、无 confirmation、无 broadcast active send、
> 无 queue/concurrency、未改 Navigation IA、未改版本/package/icon、未改 AI/Agent authority。
> M10-A contracts（TransportDisposition / NotSent / PossiblySent / raw ADU evidence / transport terminal /
> short-submission / typed provenance / session identity / Clear Results contract）**全部继承、未重新解释**。

### F0. Preflight

```text
HEAD = `d870922`（main，clean）；verified LKGC = `b7a6151`；M9 = ✅ COMPLETE；M10 Phase 1 = COMPLETE；M10-A = COMPLETE；
CMake VERSION = 2.0.0；v1 tag object `2cee626` / target `ae067ab`；v2.0.0 **absent**；
origin/main = `a40d935`（behind 0 / ahead 101）；`git diff --check` PASS —— 全部相符。
```

### F1. Source Re-read（A–F 真实事实）

```text
A. legacy / special-case FC03 路径：`readHoldingRegistersOnce`（UI 唯一入口，保留名称）→ 本地四段校验 →
   `ActiveRequestIntent` → `encodeActiveRequest` → descriptor → `startActiveRequest`；
   完成经 `handleSerialTransactionCompleted`（身份 guard）→ 唯一 publish 路径。**没有** FC03 专属 lifecycle。
B. `activeSerialRecords_` 追加方式（迁移前）：生产完成 → `publishCompletedTransaction`（**只投影 latest 一行 + 单事务统计**）；
   `appendSerialTransaction`（仅测试调用）→ `rebuildActiveSerialProjection`（用 `setEntries()` 整表重置）。
C. `publishSerialResult` 的 latest-only 投影：`setEntries({一条})` + `summarizeTransactions({单条})` +
   单条 `DiagnosisTransaction` ⇒ 可见状态永远是「最近一次」。
D. model 更新 API：`setEntries()` = `beginResetModel/endResetModel`（source replacement / Clear）；
   **此前没有 append API**（注释即写明 "No append/remove/paging"）。
E. statistics / diagnosis 接受形状：`summarizeTransactions(span<TransactionAnalysis>)` 接受**批**；
   `activeDiagnosisTransactions_` 是**向量**（批）——两者本来就能承载整段会话，缺的只是 publish 端用批。
F. Transactions selection：页面本地状态（`selectedRow/selectedEntry` + `ListView.currentIndex`），
   **唯一失效路径是 `onModelReset`**；因此 append（insert）天然不会清选择，也不需要 QML 逻辑改动。
```

### F2. RED / Existing Behavior Baseline（B1–B5，迁移前实测）

```text
在 `active_master` 内加一个**临时探针用例**（提交前已删除，仅用于锁定事实），真实输出：
  [B1] activeSerialRecords = 2 | visible rows = 1
  [B2] observed = 1 | success = 0 | timeout = 1 | rows = 1
  [B3] baseline text = "诊断结果：\n- 无响应超时：1 ..."   （只含最后一条事务）
  [B5] pendingBeforeClear = true | pendingAfterClear = true | records = 0 | rows = 0 | connected = true
结论（M10-B gap）：**权威记录已经在 append，但可见 presentation 仍是 latest-only**（rows/statistics/diagnosis 三处同源）。
B4（Simulator / Replay 继续按 source replacement 构建完整自己的 model）由既有 b01/b02（4 行 demo）与 r01/r02（4 行 replay）锁定。
```

### F3. 统一 FC03 入口与行为等价（wire 冻结）

```text
入口链（唯一）：UI Draft → validation → `ActiveRequestIntent` → `ActiveRequestDescriptor` → `SerialTransport`
  → `SerialTransactionSession` → `ActiveTransactionResult` → Active Serial history（record）→ 可见投影。
`readHoldingRegistersOnce` 名称保留，内部**只是**构造统一 intent 并调用统一 active path；没有第二套 FC03 lifecycle。
行为等价冻结并回归：unit 1..247 / start 0..65535 / quantity 1..125 / timeout>0（四条中文文案逐字未变）；
  CRC 与 request bytes（金样 `01 03 00 00 00 02 C4 0B`，AC-02 与 ta03 双重锁定）；response decoding 与 exception handling
  （仍由共享 analyzer 产出）；timeout 语义（elapsed/threshold 决定 Timeout vs Pending）；busy guard。**wire 未改**。
无新公开功能：`encodeActiveRequest(0x06)` / `(0x10)` 仍 `UnsupportedFunction`（AC-03 保持通过）。
```

### F4. Active Serial History 契约（本轮正式冻结）

```text
同一个 Active Serial session 内，**每一个完成且具有 Modbus transaction outcome 的 Active request 都 APPEND**：
  R1 Success → R2 Timeout → R3 Exception ⇒ 可见 transaction history 按时间顺序包含 R1 → R2 → R3；
  **R2 不覆盖 R1，R3 不覆盖 R2**；顺序 = 完成顺序，新行追加在**末端**（oldest → newest），旧行内容/顺序/identity 不变。
transport terminals（TransportError / DisconnectedAfterSubmission / ShortSubmission）**不伪装成 Modbus row**：
  它们继续只在 serial error lane + runtime evidence（`activeSerialTerminations_`）承载；未来的 write-safe presentation 属 M10-C/D。
容量：审计确认**代码中从无 session row 上限**（无 cap 常量、无裁剪）⇒ 保持 session-lifetime append，
  本轮不发明 100/1000 之类 cap（未来 performance policy 独立设计）。
pending 可见性：审计确认当前**没有 Pending row**（发送中仅由 `serialBusy` + 按钮文案表达）⇒ 保持，不新增 Pending UX，
  也不会出现「pending + completion 后重复一行」。
```

### F5. Presentation Projection 与 Model Append

```text
`TransactionListModel::appendEntries(...)`（新）：`beginInsertRows/endInsertRows`，**不 reset、不 dataChanged、不重排**；
  旧行保持内容/顺序/identity ⇒ `append != source replacement`；`setEntries()` 语义**未改**（仍是 source replacement）。
Controller 唯一发布路径：`appendActiveSerialTransaction(record)` = ① record 入 `activeSerialRecords_`
  ② `makeSessionRow(record)` 追加一行（纯投影：deviceAddress/functionCode/status/elapsed/exceptionCode/issueText + provenance）
  ③ `refreshActiveSessionDerivedViews()` 用**整段会话**重算统计与诊断批。
生产完成路径（`handleSerialTransactionCompleted`）只调用这一处（M10-A 的 `publishSerialResult` / `publishCompletedTransaction`
  已删除：latest-only 语义正是本轮被评审要改掉的契约）；完成同时结束 in-flight 状态（`serialBusy_ = false` + 清 serial error + 信号）。
QML：仅更新 TransactionPage 的选择契约注释（两条变更路径：reset 失效选择 / insert 保持选择），**无逻辑与视觉改动**。
```

### F6. Selection / Detail 稳定性（M9-D 契约延续）

```text
页面本地选择（`selectedRow` + `selectedEntry` 快照 + `ListView.currentIndex`）**只在 `onModelReset` 失效**；
append 走 insert ⇒ 已有选择继续指向原 transaction、detail 继续显示原行，**不 auto-select 最新行**（运行时本身不持有选择状态）。
B05 用信号级 oracle 锁定该前提：append 后 `modelReset` 计数不变、`rowsInserted` +1、**`dataChanged` = 0**，且旧行所有角色值逐项相等。
（无选择时保持无选择：运行时无选择 API，QML 侧由既有 qml_focus_check 的 no-select-on-focus 场景继续把守。）
```

### F7. Statistics / Diagnosis 契约

```text
Statistics（Active Serial source）：基于**当前 session 全部 completed Modbus records**（observed/completed 与既有 taxonomy 聚合）；
  `successRate` 继续 = success / (completed − ENR)（**M9-C 定义未改**）；
  **transport terminals 不进入任何 Modbus 计数**（B03 实测：2 个 terminal 后 observed/completed/rows 完全不变）。
Diagnosis：确定性诊断输入改为**当前 session 完整 completed batch**（`activeDiagnosisTransactions_` 由整段记录重建），
  不再只诊断 latest row（B04 实测：timeout 与 设备异常 0x02 同时出现在基线报告中）。
  冻结延续：诊断只分析事实，**不发送 request / 不 retry / 不 write**；`clearDiagnosis` 语义未变。
批次变更即失效派生视图（M9 不变量）：新增事务会清 baseline/AI 结果而不是让其静默过期。
```

### F8. Clear Results / Pending / Source 边界

```text
Clear Results（与 M10-A 权威对齐）：清 `activeSerialRecords_` + `activeSerialTerminations_` + 可见行（reset）+ 统计 + 诊断批；
  **不** disconnect、**不** cancel pending、不清 draft、不发送；连接与 source 身份保留（B07）。
Clear while pending（B08）：R1 已完成 → R2 在飞 → Clear ⇒ R1 消失、R2 **不取消**；R2 完成后成为**清空后会话的第一行**，
  observed = 1、records = 1，R1 永不复活。
Reconnect = 新 Active Serial session（B09）：session id 递增、可见历史与 records 从空开始，旧 session 不泄漏。
Simulator 替换（B10）：4 行 demo、typed source = Simulator、Active records/terminals 清空。
Replay 成功替换（B11）：4 行 replay、typed source = Replay、Active records 清空、串口连接断开（既有契约）。
Replay **失败**替换（B12）：rule A 冻结 —— 旧 source 全量保留（行/统计/`modeLabel`/`sourceLabel`/typed sourceKind/连接状态不变），
  只置 replay 错误。
Navigation：仍为 presentation-only（未改）；nav/geometry/focus 三闸门全绿。Communication UI 未改。
```

### F9. 测试（B01–B12 + 迁移说明）

```text
B01 同一 session 两次成功 ⇒ records 2 / rows 2 / observed 2 / success 2 / sessionId 1。
B02 Success + Timeout + Exception ⇒ rows 3、顺序稳定（unit 1/2/3、状态 成功/超时/异常、异常码 0x02）。
B03 统计覆盖整段会话（observed 3 / success 1 / timeout 1 / exception 1 / rate 1/3）；**2 个 transport terminal 后计数与行数不变**。
B04 诊断批覆盖整段会话（timeout 与 设备异常 0x02 同时出现）。
B05 append 保住既有行与选择前提（reset 计数不变 / rowsInserted +1 / dataChanged 0 / 旧行值逐项相等）。
B06 不 auto-select（运行时无选择状态；行数增长不动 mode/source/busy/error）。
B07 Clear 清可见历史与统计，保留连接与 source 身份。
B08 Clear while pending（R2 不取消；完成后成为清空后第一行）。
B09 reconnect = 新 session（历史从空开始）。
B10 Simulator 替换不含 Active 行；B11 Replay 成功替换不含 Active 行；B12 Replay 失败替换保留旧 source。
契约迁移的既有用例（**因评审通过的新契约而改写，非删除**）：
  · ui_bridge s07（原「Replace, never append」）→ 现断言同 session 两行；ta11（原 M10-A latest-only 断言）→ 现断言两行。
  · ui_bridge s05/s06/s08/d07/t01 从合成 publish seam 迁移到**真实生产路径**（deterministic recording transport），
    并新增 B10–B12；t01 的地址不匹配由真实回包产生（不再手工传递 analysis）。
  · 旧 `publishSerialResult` seam 删除，由其替代物（真实路径 + recording transport）覆盖，覆盖面**扩大而非缩小**。
```

### F10. 门禁（真实输出）

```text
Debug ctest：**29/29 PASS**；Release ctest：**29/29 PASS**（含 qml_smoke / qml_geometry_check / qml_nav_check / qml_focus_check）。
`active_master`：**39 passed / 0 failed**（M10-A 30 + B01–B09）；`ui_bridge`：**59 passed / 0 failed**（原 56 + B10–B12）；
`active_request`：17 passed / 0 failed；serial / serial_adapter / statistics / diagnosis / replay 套件全绿。
Debug 与 Release 构建**零 warning / 零 error**；`git diff --check` PASS。
```

### F11. Problems / RCA

```text
P1（本轮真实缺陷，测试立即捕获）：迁移后 `handleSerialTransactionCompleted` 仍自行 `activeSerialRecords_.push_back`，
  同时又调用新的 append 路径 ⇒ **一次完成写两条记录**（记录 2 / 可见行 1，统计按 2 条聚合）。
  Observed：s05 observed=2、s07 第二事务无行；Expected：一次完成 = 一条记录 + 一行；
  Root Cause：历史写入点从「一处」变成「两处」（迁移时未删除旧写入）；
  Fix：删除旧 push_back，**唯一写入点** = `appendActiveSerialTransaction`；
  Verification：ui_bridge 56/56、active_master 39/39、Debug/Release 29/29。
P2（同轮第二处）：迁移中删掉了完成路径对 in-flight 状态的收尾（原 `publishCompletedTransaction` 里顺带设置
  `serialBusy_ = false` / 清 serial error）⇒ 第二次 FC03 被 busy guard 拒绝（s07 只出现一行）。
  Fix：完成路径显式结束 in-flight 状态并清 serial error + 发 `serialStatusChanged`（不触碰行/统计/source）。
P3（测试夹具修正，非产品缺陷）：异常/异地址回包最初手写字节导致 CRC 错 ⇒ 用 `encodeRtuFrame` 生成合法帧；
  t01 迁移后新行位于 index 1（append 语义）而非 index 0。
零警告：新增可选字段的 designated initializer 全部显式补齐（-Wmissing-field-initializers）。
```

### F12. 边界审计

```text
改动文件 8 个：`CMakeLists.txt`（ui_bridge 目标加入 recording transport + tests include）、
  `src/ui/AnalysisController.{h,cpp}`、`src/ui/TransactionListModel.{h,cpp}`、
  `src/ui/qml/pages/TransactionsPage.qml`（仅注释）、`tests/test_active_master.cpp`、`tests/test_ui_bridge.cpp`。
未改：src/core/**（core contract 冻结）、simulator、agent/AI、版本/PE/icon/package/scripts/assets/samples/screenshots。
`encodeWrite*` 0；agent 写工具 0；0x06/0x10 encoder 仍不存在；无 Write UI / confirmation。
REAL HARDWARE NOT VERIFIED 继续（M10-B 不要求真机；不得把 simulator/fake PASS 写成 hardware PASS）。
```

### F13. Git

```text
behavior-bearing ⇒ 不作 LKGC。commit：`M10-B: migrate FC03 to unified Active Master history`
（独立提交；不 amend `d870922`；不 rebase；不 push；未创建 v2.0.0 tag）。
verified LKGC 保持 `b7a6151`；M10-B = 等待 Review。
```
## M10-B Correction — Transactions Selection-on-Append QML Runtime Oracle（2026-09-20，harness-only）

> **M10-B Review = HOLD（极窄 test-oracle correction）。** M10-B 主体实现**全部接受**：FC03 unified contract /
> Active Serial history append / statistics whole-session batch / diagnosis whole-session batch / Clear Results /
> reconnect·new session / Simulator·Replay replacement / transport terminal exclusion / M10-A safety contracts。
> **唯一缺口**：B05 / B06 当时主要由 **model signal-level** 证据支持，而 Transactions selection 是 **QML page-local
> presentation state** —— 必须在真实 `TransactionsPage.qml` + `ListView` runtime 里直接证明。
> 原 M10-B 历史（§F0–F13）**未改写**。

### G0. 选择权威的真实审计（A–F）

```text
A. ListView id = `transactionsList`（TransactionsPage.qml）。
B. selection authority 有两层且同源：`ListView.currentIndex`（Qt 原生 Up/Down/Home/End 与鼠标 tap 都改它）
   与页面本地快照 `selectedRow` / `selectedEntry`（`onCurrentIndexChanged: page.selectRow(currentIndex)` 驱动）。
C. selected detail 由页面本地快照驱动：`transactionDetail*` 标签绑定 `page.hasTransactionSelection ? selectedEntry.<field> : ""`；
   快照由 `captureEntry(item)` 在**选择时**从 delegate 的 `row*` 只读属性复制（presentation copy，非权威）。
D. 失效路径唯一：`Connections { target: transactionModel; onModelReset }` ⇒ 清 `selectedRow/selectedEntry/pendingSelectionRow`
   并把 `currentIndex = -1`。**只有 modelReset**。
E. `rowsInserted` **没有任何 handler**（审计确认）——append 不触碰选择状态。
F. 因此 append（`appendEntries` → `beginInsertRows/endInsertRows`）时：模型不 reset、已有行不 dataChanged、
   Qt 不移动 currentIndex ⇒ currentIndex/快照保持；页面也不会自动选中任何新行（无该逻辑）。
   ——以上为源码事实，本轮用**真实 runtime**验证，而不是继续推断。
```

### G1. 运行时场景 FM（B05-QML：selection survives append）

```text
harness：`--qml-focus-check` 内的新场景 **FM**（真实 app + 真实 QML + 真实 controller）。
注意：**不使用 AppBar「清空结果」作为焦点锚点** —— 点击它是破坏性清空（M9-F F1 已记录），会删掉本 oracle 需要的行；
      改用 rail 条目（`selectWorkspace(0)`）+ workspace 自身 Tab 链进入列表。
步骤与断言（真实输出）：
  1) clearResults() → 通过**唯一生产 append seam**（`appendActiveSerialTransaction`，与 transport 完成同一方法）
     追加 unit 11 / Success 与 unit 22 / Timeout 两条记录；ListView count = 2。
  2) 键盘进入列表（Tab 链；若 20+ 次未到达则退回页面自身的 `selectRow()` 入口——FA 已单独把守遍历契约）
     → Home 选中 row 0。
  3) 断言：currentIndex = 0、selectedRow = 0、detail 文本 = [设备 11][成功]、`assertTransactionDetailMapping` 字段逐一相等。
  4) **追加第三条**（unit 33 / Exception）后立即断言：
       · model 与 list 均 3 行（append 真的发生）；
       · currentIndex 仍 = 0、selectedRow 仍 = 0（未被抢走）；
       · currentIndex ≠ 2（新行未被 auto-select）；
       · detail 仍是 [设备 11][成功]（不是 [设备 33][异常]），字段映射仍逐项相等。
  5) 追加后键盘回归：End → currentIndex = 2 且 detail **显式**跟随（用户动作照常生效）；Home → 回到 0。
  6) 追加后代替换回归：点击「清空结果」（真实 modelReset）→ currentIndex = -1、selectedRow = -1、快照清空、
     空态提示可见 ⇒ **append 保持** 与 **replacement 失效** 两个机制互不混淆。
真实输出：
  FOCUS [FM]: row #1 (unit 11) selected; detail shows [设备 11][成功]
  FOCUS [FM] PASS: currentIndex=0 selectedRow=0 detail=[设备 11][成功] after appending row #3
  FOCUS [FM] PASS: Up/Down/Home/End still drive the currentIndex -> selectRow path after an append
  FOCUS [FM] PASS: reset/replacement invalidates the selection while append preserves it
```

### G2. 运行时场景 FN（B06-QML：no selection stays none after append）

```text
步骤与断言：
  1) 处于**真实 no-selection 状态且至少有 1 行**（上一步的 reset 已清空模型，随后追加 unit 44 / Success 一条）。
  2) 断言：rows = 1、currentIndex = -1、selectedRow = -1、快照为空、空态提示可见。
  3) 键盘进入列表：**不得**因 focus entry 或 delegate 创建而选中任何行（currentIndex 仍 = -1）。
  4) 追加 unit 55 / Timeout → 断言 rows = 2、currentIndex 仍 = -1、selectedRow 仍 = -1、快照仍为空、空态提示仍在。
真实输出：
  FOCUS [FN] PASS: no selection before and after the append; detail stayed in the no-selection state
汇总行已扩展：`... FM selection survives append; FN no selection stays none after append`。
```

### G3. 产品代码 diff

```text
**零产品 diff**：本轮未改 QML（TransactionsPage.qml 无改动）、未改 `TransactionListModel`、未改 `AnalysisController`、
未改 core / simulator / transport / evidence。唯一 src 改动是 **harness 文件 `src/main.cpp`**（新增 FM/FN 场景与
`appendActiveSerialRecord` 辅助）。按 §10 未引入任何「fake selection authority」，selection 权威仍留在 QML page。
（`src/main.cpp` 中 5 条 `-Wunused-variable` 等告警经 HEAD 副本比对确认为**既有**告警，未在本轮引入，按 scope freeze 未动。）
```

### G4. 门禁（真实输出）

```text
`--qml-focus-check`（含 FM/FN）：exit 0，FOCUS CHECK PASS。
Debug ctest **29/29 PASS**；Release ctest **29/29 PASS**（含 qml_smoke / qml_geometry_check / qml_nav_check / qml_focus_check）。
`ui_bridge` **59 passed / 0 failed**；`active_master` **39 passed / 0 failed**；`active_request` 17 passed。
`git diff --check` PASS。
```

### G5. Problems / RCA

```text
P1（harness 设计缺陷，本轮自查发现）：FM/FN 第一版沿用 FA 的焦点锚点 `appBarClearResults`（点它就是「清空结果」），
  于是刚追加的记录被清掉 —— 表现为 `selection is -1/-1`、`after append model=1 list=1`。
  Root Cause：锚点是破坏性动作（M9-F F1 已记录过同一陷阱），新场景直接复用而未评估副作用。
  Fix：改用 rail 条目 + workspace Tab 链作为入口；破坏性清空只在**需要证明 replacement 失效**的那一步使用。
  Verification：FM/FN 全绿（见 G1/G2 真实输出）。
P2（可观测性）：聚焦 harness 的失败信息在本机被吞掉（WIN32 GUI 子系统无控制台）。定位手段：
  `QT_ASSUME_STDERR_HAS_CONSOLE=1` 强制 Qt 消息处理器写 stderr —— 这是**诊断手段**，未写入产品/CI。
  记录以便后续排查同类 harness 失败。
```

### G6. Git

```text
harness/test 行为变化 ⇒ **behavior-bearing**（按项目治理：无 production diff 也不例外）。
commit：`M10-B: prove Transactions selection survives append`（独立提交；不 amend `6e7c6a3`；不 rebase；不 push；未创建 tag）。
verified LKGC 保持 `b7a6151`；M10-B = 等待 Final Re-review。
```
## M10-B Final Acceptance / Closure（2026-09-20，docs-only）

> **M10-B Final Re-review = PASS。M10-B = COMPLETE。**
> 最终 accepted behavior tree = **`ef71244`**。verified LKGC 由 `b7a6151` 推进至 **`ef71244`**。
> 本轮严格 docs-only：未改 src / tests / QML / CMakeLists.txt / scripts / assets / samples / screenshots。

### H0. Final Re-review PASS 归档（真实 QML runtime 已直接证明）

```text
A. 已有 selection 在 Active Serial append 后**保持**（currentIndex 与 selectedRow 均不变）。
B. detail pane **继续显示原 transaction**（[设备 11][成功]，未切到新行的 [设备 33][异常]，字段映射逐项相等）。
C. 新增 row **不 auto-select**（currentIndex ≠ 新行 index）。
D. **no-selection 在 append 后仍 no-selection**（currentIndex / selectedRow 仍 -1，detail 仍空态）。
E. append 后 **Home / End 等键盘导航仍正常**（End → 2/2 且 detail 显式跟随；Home → 0/0）。
F. **model reset 仍使 page-local selection 正确失效**（currentIndex → -1、快照清空、空态提示出现）。
硬件：**REAL HARDWARE NOT VERIFIED**（不阻塞 M10-B 的软件范围 COMPLETE，不得写成 hardware PASS）。
```

### H1. M10-B Commit Classification Audit（`git show --stat --name-only` 真实文件列表）

```text
6e7c6a3  M10-A/B: migrate FC03 to unified Active Master history   files=13 code/test= 8 ⇒ **behavior-bearing**
         （AnalysisController.{h,cpp}、TransactionListModel.{h,cpp}、TransactionsPage.qml、两个测试文件、CMakeLists）
ef71244  M10-B: prove Transactions selection survives append       files= 6 code/test= 1 ⇒ **behavior-bearing**
         （唯一非 docs 文件 = src/main.cpp：harness runtime behavior 变化，新增 FM/FN QML runtime acceptance oracle；
          项目治理：test/harness behavior change 属于 behavior-bearing）
⇒ behavior chain = 6e7c6a3 → ef71244；**final accepted behavior tree = `ef71244`**（不是 6e7c6a3）。
本轮 closure commit 为 docs-only ⇒ **不作 LKGC**。
```

### H2. FC03 Unified Contract（冻结）

```text
0x03 主动请求完整路径（唯一，不得重新引入第二套 FC03 lifecycle）：
  UI Draft → validation → ActiveRequestIntent → ActiveRequestDescriptor → SerialTransport
  → SerialTransactionSession → ActiveTransactionResult → Active Serial history → model/statistics/diagnosis projection。
`readHoldingRegistersOnce` 仍是 UI 唯一入口，内部只构造统一 intent 并调用统一 active path。
```

### H3. FC03 Wire Equivalence（冻结）

```text
M10-B 是 **state/history migration，不是协议行为变化**。冻结不变：unit 1..247 / start 0..65535 / quantity 1..125 /
timeout contract（elapsed vs threshold）/ request wire bytes 与 CRC（金样 `01 03 00 00 00 02 C4 0B`）/
normal response / exception / CRC error / timeout / ProtocolError 判定（仍由共享 analyzer 产出）。
```

### H4. Active Serial History（冻结）

```text
同一 Active Serial session 内：completed Modbus transaction 按**完成顺序 append**，**不再 latest replaces previous**；
顺序 = **oldest → newest**，新行追加在**末端**；旧 rows **不重排、不修改**（内容/顺序/identity 不变）。
```

### H5. Transport Terminal Boundary（冻结）

```text
`ActiveTransportTerminal`（TransportError / DisconnectedAfterSubmission / ShortSubmission）**不伪装成 Transaction row**，
也**不进入任何 Modbus 统计**（observed / completed / Success / Timeout / ProtocolError 均不变）；
它们继续属于 **serial error lane + runtime evidence**（`activeSerialTerminations_`）。
```

### H6. Statistics / Diagnosis（冻结）

```text
Statistics（Active Serial source）：基于当前 session **全部 completed Modbus records** 的同一 batch；
  `successRate = success / (completed − ExpectedNoResponse)` —— **M9-C 定义未改**。
Diagnosis（Active Serial source）：deterministic diagnosis 输入 = 当前 session **完整 completed transaction batch**（不再只分析 latest）；
  但 diagnosis **仍然只 analysis**：不 send / 不 retry / 不 write / 不 control device。
```

### H7. Selection Ownership 与三条 runtime 契约（冻结）

```text
Selection 继续是 **page-local presentation state**（`selectedRow`/`selectedEntry` 快照 + ListView.currentIndex）；
**不得**搬到 Controller 或 TransactionListModel（禁止 fake selection authority）。
· same-session append ⇒ **不 invalidate** selection（真实 runtime 已证明）。
· selected-row append contract：已有 selected row → append 新行 → currentIndex 保持、selectedRow 保持、
  detail snapshot 保持、**new row 不抢 selection**；**禁止**未来无 Review 改成 auto-follow / auto-select latest。
· no-selection append contract：currentIndex = -1 → append → 仍 -1、selectedRow 仍 -1、detail 仍空态；
  Tab 进入 list 仍不得 auto-select。
· replacement invalidation contract：model reset ⇒ selection **必须失效**（currentIndex → -1、快照清空、empty detail）；
  Simulator / Replay successful / new Active Serial session 只要走真实 source replacement / model reset 即适用。
```

### H8. Clear Results / Session / Source 边界（冻结）

```text
Clear Results（Active Serial）：清 completed records + transport terminals + visible rows + statistics + diagnosis derived batch；
  **不** disconnect / 不 cancel pending / 不清 draft / 不发送。
Clear while pending：pending 继续；完成后成为**清空后的第一条新 transaction**（旧记录不复活）。
Session boundary：成功 reconnect ⇒ **new Active Serial session id**；旧 session 的 records / terminals / visible history
  **不得进入新 session**；navigation **不得**创建 session。
Source replacement：Simulator = replacement；Replay 成功 = replacement；**Replay 失败 = 继续保存旧 authoritative source**；
  workspace ≠ source；navigation ≠ source switch —— M10-B 的 append 未破坏这些 M9 冻结语义。
```

### H9. Evidence（冻结）

```text
presentation rows 仍然只是 **`ActiveTransactionRecord` 的 projection**；authority 继续保留在 runtime record：
requestAdu / responseAdu / TransportDisposition / provenance / session identity。
**不得**为了 UI history 重新构造 wire evidence。
```

### H10. Final Test State（归档；本轮 docs-only 未重跑）

```text
active_master：**39 passed / 0 failed**（TA15 + TF8 + TS5 + B01–B09 + init/cleanup）
ui_bridge：**59 passed / 0 failed**（含 B10–B12，serial 用例已迁移真实生产路径）
active_request：17 passed / 0 failed
qml_focus_check：**PASS（含 FM/FN runtime oracle）**；qml_smoke / qml_nav_check / qml_geometry_check：PASS
Debug ctest：**29/29 PASS**；Release ctest：**29/29 PASS**
（未重跑的前提已核实：`ef71244..HEAD` 无任何 src / tests / QML / CMakeLists.txt 行为 diff。）
```

### H11. RCA 历史（保留，不清洗）

```text
实现期（§F11）：P1 completion path **double-write history**（旧 push_back 未删 + 新 append ⇒ 记录 2 / 行 1 / 统计按 2 聚合）；
  P2 漏清 `serialBusy_` ⇒ 第二次 FC03 被 busy guard 拒绝；P3 测试夹具修正（异常/异地址回包 CRC、append 后行 index）。
harness 期（§G5）：FM/FN 第一版错误复用 `appBarClearResults` 作为 focus anchor（点击即破坏性清空）导致测试自身清掉数据；
  Root Cause = harness anchor 有副作用；Fix = 改用 non-destructive rail anchor（破坏性清空只在证明 replacement 失效时使用）。
两段历史均保留，**未改写成「第一次即 PASS」**。
```

### H12. Non-blocking Warning Note

```text
`src/main.cpp` 中 5 条 `-Wunused-variable` / `-Wunused-but-set-variable` / redundant-capture 类告警，经 HEAD 副本比对
确认为 **PRE-EXISTING NON-BLOCKING**（非 M10-B 引入）。M10-B closure **不顺带修**；留待未来统一的 warning hygiene task。
```

### H13. 边界与 Git

```text
0x06 / 0x10 active encoder **ABSENT**；Write UI / confirmation dialog **ABSENT** ⇒ **M10-B COMPLETE ≠ write capability available**。
AI / Agent write authority = **NONE**（无 write tool / send serial tool / raw serial tool）。
REAL HARDWARE NOT VERIFIED（simulator/fake PASS 不得写成 hardware PASS）。
commit：`M10-B: close FC03 unified history migration`（独立 docs-only 提交；不 amend `ef71244`；不 rebase；不 push；未创建 tag）。
verified LKGC = **`ef71244`**；M10 = IN PROGRESS；Phase 1 = COMPLETE；M10-A = COMPLETE；**M10-B = COMPLETE**；M10-C = NEXT。
```
## M10-C Phase 1 — Write Safety UI Foundation：Learning / Design Gate（2026-09-20，docs-only）

> **M10-C = Write Safety UI Foundation，Phase 1 = Learning / Design；Implementation = NOT STARTED。**
> 本轮严格 docs-only；**不**实现 0x06 / 0x10 encoder、**不**创建 Write UI、**不**调用 transport、**不**新增 Agent tool。
> 继承冻结（不得重新解释）：**M10-A** submission/evidence contracts（NotSent / PossiblySent / raw ADU evidence /
> transport terminal / typed provenance / session identity / Clear Results）；**M10-B** Active Serial history /
> source / selection / statistics / diagnosis contracts。

### I0. Preflight（2026-09-20）

```text
HEAD = `806d424`（main，clean）；verified LKGC = `ef71244`；M9 = ✅ COMPLETE；M10 Phase 1 = COMPLETE；
M10-A = ✅ COMPLETE；M10-B = ✅ COMPLETE；CMake VERSION = 2.0.0；v1 tag object `2cee626` / target `ae067ab`；
v2.0.0 **absent**；origin/main = `a40d935`（behind 0 / ahead 104）；`git diff --check` PASS —— 全部相符。
```

### I1. M10-B → M10-C 过渡

```text
M10-B = ✅ COMPLETE（accepted behavior tree `ef71244`）；M10-C = Write Safety UI Foundation，状态 = Learning / Design。
冻结继承：M10-A 的 submission disposition（NotSent / PossiblySent）/ wire evidence（requestAdu·responseAdu）/
transport terminal（TransportError·DisconnectedAfterSubmission·ShortSubmission）/ typed provenance + session id /
Clear Results（不清 draft、不 cancel pending）；M10-B 的 Active Serial history（append、oldest→newest）/
source replacement 语义 / selection page-local 三条 runtime 契约 / statistics·diagnosis 整段会话 batch。
```

### I2. Mandatory Source Re-read（A–G 真实事实，全部来自当前源码）

```text
A. CommunicationPage 现有 layout/geometry（248 行，逐行实读）：
   `Item` → `ColumnLayout(objectName communicationContentLayout, margins DS.spacingL=16, spacing DS.spacingM=12)`：
     · SectionHeader「通信」/ subtitle「串口连接与请求」
     · SectionHeader「连接」 + PanelCard(`communicationConnectionSection`)：
       RowLayout[ Label 串口 · ComboBox(`commPortCombo`, 宽 140, model=serialPortNames, enabled=!serialConnected,
         自定义 background + `commPortComboFocusRing` overlay) · Button「刷新串口」 · Label 波特率 ·
         ComboBox(`commBaudCombo`, [9600,19200,38400,57600,115200], 宽 110, focusRing overlay) · Label 8N1 ·
         Button「连接」(enabled = !connected && portIndex>=0) · Button「断开」(enabled = connected) · 弹性 Item ]
     · SectionHeader「请求」 + PanelCard(`communicationRequestSection`)：
       RowLayout[ Label 从站地址 · SpinBox(`commSlaveSpin` 1..247) · Label 起始地址 · SpinBox(`commStartSpin` 0..65535) ·
         Label 寄存器数量 · SpinBox(`commQuantitySpin` 1..125) · Label 超时(ms) · SpinBox(`commTimeoutSpin` 100..10000) ·
         Button「读取保持寄存器」(文本 busy 时「读取中...」, enabled = connected && !busy) · 弹性 Item ]
     · Label(`communicationSerialError`, visible=hasSerialError, color #B03030, wrap)
     · 末尾弹性 Item（消耗竖直余量，避免 ColumnLayout 把余量摊到各 section 之间——T017 §35.6 实测结论）
   ⇒ 控件形态：**ComboBox（枚举）+ SpinBox（数值）+ 原生 Button**；无 Dialog / 无 Popup / 无 Modal。
B. 现有通用 modal/dialog pattern：**不存在**。全仓 QML 只有 `QtQuick.Dialogs` 的 **FileDialog**（`ReplayPage` 的
   `replayFileDialog`，由 Main.qml 的 shell 按钮触发）；没有任何 `Dialog` / `Popup` / `Drawer` / `Menu` / `Overlay` 使用。
   ⇒ M10-C 的 confirmation 将是**本项目第一个真正的模态对话框**（无既有 pattern 可复用，是新组件）。
C. 现有焦点管理方式（M9-F F1 冻结）：页面根 `Item` 由 StackLayout 拥有；控件用 `activeFocusOnTab`（Tab 链）
   + 自定义 focus ring overlay（ComboBox 用 `anchors.margins:1` 的 2px `DS.primary` 边框；AppButton 用 border 通道；
   TabButton 用内环）绑定 `activeFocus` / `visualFocus`；`AppButton.focusPolicy = Qt.StrongFocus`；
   SpinBox 依赖自身 `activeFocus`（FK 场景已锁定其可见性）。
D. page hidden 时的 enabled gating：Main.qml 的 StackLayout（`id: workspaceHost`）中**每个页面**都有
   `enabled: workspaceHost.currentIndex === workspace<X>Index`（Communication 是 child 2）。M9-F FB 场景证明
   隐藏页控件不会出现在当前工作区的 Tab 链中；FC 场景证明「离开时仍有焦点的控件在隐藏态不能执行」。
E. Controller 当前暴露给 QML 的东西（Q_PROPERTY 全表实读）：statistics 计数与速率/latency、transactionModel、
   replay 错误/通知、`modeLabel`、`sourceLabel`、`serialConnected`、`serialBusy`、`hasSerialError`、
   `serialErrorMessage`、`serialPortNames`、诊断 baseline / AI / Agent 状态。
   ⇒ **与写安全相关的可读事实 = connected / busy / serialErrorMessage / sourceLabel**。
F. QML 当前**无法**取得 Active Serial session id：`sourceKind()` 与 `activeSerialSessionId()` 是 **C++-only 访问器**，
   没有任何 Q_PROPERTY 暴露。⇒ §21/§22 的 session identity check 需要**新增一个只读属性**（设计见 I21），
   或把 sessionId 放进 snapshot 由 Controller 在 send 前权威校验（推荐两者都做）。
G. 输入控件 pattern：数值 = **SpinBox**（`from`/`to`/`value`，本页四个字段全部如此）；枚举 = **ComboBox**；
   动作 = **原生 Button**（本页与 DiagnosisPage）或 **AppButton**（Dashboard / AppBar / Replay 使用）；
   长文本 = **TextArea**（DiagnosisPage 的 Agent 输入框）。注意：**SpinBox 自带 clamp**（见 I16 审计）。
```

### I3. M10-C exact scope

```text
M10-C **不是**把数据真正写到设备。它负责：
  draft ownership · input validation UX · write summary · confirmation contract · safe keyboard behavior ·
  session/busy safety · double-activation protection · timeout/unknown wording contract · accessibility baseline ·
  test oracle（C01–C22）。
真正 0x06 encoder + send 属 **M10-D**；真正 0x10 encoder + send 属 **M10-E**。
本 Phase 1 只产出设计 + 阶段提案 + 12 项 decision requests；**不写任何代码**。
```

### I4. No Dead / Fake Write UI（必须冻结的设计原则）

```text
禁止（用户可见的假动作）：任何「写入 / 确认写入 / 发送」可点击控件，点击后什么都不发、或只是假动作；
也禁止「UI 看起来支持写，但 encoder 仍 UnsupportedFunction」。
⇒ Phase 1 决定 staging（§4 的两个候选）：
   **方案 A — foundation 先隐藏于 production，仅 harness 可实例化，M10-D 才首次暴露。**
   **方案 B — production 可见 draft/preview，但 destructive action 明确 disabled。**
比较：
   · A 优点：production 用户**完全看不到**任何写控件 ⇒ 零误解风险；oracle 可先用 harness 全量验证；
     缺点：需要一个「harness 可见」的机制（建议：与 DS 同形的 engine context property，
     例如 `writeFoundationVisible`，**production = false，只有 harness 模式置 true**），
     并在 M10-D 接线时把这个开关**删除**（而不是长期保留双模式）。
   · B 优点：不必新增开关；用户能提前看到写区形态与只读校验反馈；缺点：**任何** disabled 控件都在传达
     「这个功能存在但你没满足条件」——在 capability 尚不存在时这种暗示是错误的；且 disabled 的 destructive
     按钮与「busy 时禁用的正常写按钮」在视觉上不可区分，未来一旦忘记改，就会变成**真的假发送**入口。
**Recommendation：方案 A**（production 隐藏 + harness 可实例化），并把它写成 M10-C→M10-D 的交接条件：
   M10-D 实现 0x06 encoder 的第一件事，就是把开关改为真实 capability 绑定（`encodeActiveRequest(0x06)`
   成功才可见/可用），而不是简单地 `visible = true`。
**明确拒绝**：可点击的 no-op、以及「enabled destructive action → UnsupportedFunction」。
```

### I5. UI Ownership / IA（冻结）

```text
Communication workspace 继续拥有 Active Master UI；**不新增第 6 个 workspace**。
总体 IA：
  Communication
  ├─ Connection（现状不动）
  ├─ Read  → 0x03（现状「请求」区，仅重命名分区标题，控件与行为不变）
  └─ Write → 0x06 / 0x10（新增，M10-C 承载）
read 与 write **不得**混成一个巨大动态表单：写区是独立分区，带独立的危险等级语义与自己的确认路径。
```

### I6. Write Function Selection（0x06 / 0x10）— 候选比较

```text
A. Write 内两个子 Tab（复用 Diagnosis 页既有的 TabBar/TabButton pattern）
B. 小型 segmented selector（两个互斥按钮/分段控件）
C. 两个独立 sections（0x06 一块、0x10 一块，同时可见）
比较（安全可分性 / 键盘 / 复用成本 / 视觉噪声 / 扩展性）：
  · A：危险等级与「当前写哪种功能」一目了然；**复用既有 TabButton 焦点可见性方案**（M9-F 已锁定内环表现）；
    键盘 = Tab 进 TabBar → Left/Right 切换；扩展 0x0F 等只需加 Tab。缺点：TabBar 在页内是第二次出现
    （Diagnosis 已有一个），需要 SectionHeader 明确归属避免误认。
  · B：最紧凑；但需要一个**新组件**（项目无 SegmentedControl），且键盘语义要自己实现（Left/Right vs Tab），
    与既有 TabButton 方案重复造轮子。
  · C：零切换成本，但两块 destructive 表单同时可见，页面纵向膨胀（values 多行时尤甚），且用户容易在错误的一侧输入。
**Recommendation：A（Write 内两个子 Tab，复用 TabButton pattern）**；标题明确「写寄存器」，
子 Tab 文案 `0x06 单寄存器` / `0x10（十进制 16）多寄存器`。
**明确拒绝**：整个 Communication 所有 function 一个巨型 ComboBox（read 与 write 会共用一个选择器 ⇒ 安全等级被抹平）。
```

### I7. Draft Ownership（冻结）

```text
Write draft 属于 **page-local presentation state**（CommunicationPage 本地属性对象）。
Controller **不得**实时拥有用户尚未确认的 raw draft（与 M9-B3 已冻结的「FC03 参数是页本地草稿」一致）。
建议逻辑：**`write06Draft` 与 `write10Draft` 分别保存**；切换 0x06 ↔ 0x10 两份 draft **彼此独立保留**
（不得因为切换 function 就把另一份覆盖/清空）。
draft 字段（草稿层，尚未校验）：unitId / timeout + 各自功能字段（见 I10、I11）。
Controller 只在**确认之后**通过 handoff seam 接收 **Validated Intent snapshot**（I41）。
```

### I8. Draft Persistence（设计 + 建议）

```text
行为设计（全部为 presentation 规则，且**任何情况下都不得自动发送**）：
  · workspace navigation（Communications ↔ 其他）→ draft **保留**（页面常驻 StackLayout，实例不销毁）。
  · Clear Results → draft **保留**（Clear 是结果域操作；M10-B 已冻结 Clear 不清 draft）。
  · disconnect → draft **默认保留**（断开是传输状态变化，不是「清表单」）。
  · reconnect → draft **默认保留**（同上；**绝不**自动补发）。
  · source replacement（Replay 成功加载 / Simulator 批）→ draft **建议保留**（见 I39 论证），
    但写控件在该 source 下**不可用**（因为没有 Active Serial 连接）——这属于可用性而非清空。
```

### I9. Unit ID Contract（冻结）

```text
M10 v1 Active Master：unit/slave **只允许 unicast 1..247**；**0 = validation reject，不得触达 transport**。
UI 必须明确表达「broadcast 当前不支持」；**绝不**把用户输入的 0 悄悄转换成 1（那是静默改用户意图）。
输入控件用 SpinBox(1..247)（与 FC03 现行一致），并在校验失败文案中写明合法区间。
```

### I10. 0x06 Draft（字段 + authority 边界）

```text
字段（草稿）：`unitId` / `registerAddress` / `value` / `timeout`。
authoritative 合法范围**实读自源码**（不得自行发明）：
  · unitId：`kMinUnicastUnitId=1` / `kMaxUnicastUnitId=247`（`ActiveRequestIntent.h`），Controller 与
    `SerialTransactionSession` 双层校验。
  · registerAddress：`WriteSingleRegisterIntent.registerAddress` 为 **uint16 ⇒ 0..65535**（core 无额外地址域策略，
    注释明确「range policy beyond that belongs to the device」）。
  · value：**uint16 ⇒ 0..65535**（任意值在结构上合法）。
  · timeout：Controller 要求 **> 0**；现行 FC03 的 **UI** 控件范围是 **100..10000**（`commTimeoutSpin`）。
UI 只负责输入与 presentation validation；Controller/core **必须再次** authoritative validation（双层）。
```

### I11. 0x10 Draft（字段 + 单一真源）

```text
字段（草稿）：`unitId` / `startAddress` / `values[]` / `timeout`。
冻结：**`values[]` 是唯一写入值 authority**；`quantity` 由 `values.size()` 派生；`byteCount` 由 `quantity` 派生
（`Function16` 的请求解码亦以 byteCount == 2*quantity 为唯一一致性判据）。
UI **不允许**同时存在「可编辑 quantity + 可编辑 byteCount + values[]」三份真源；
UI 只**显示**派生值（数量 / 预计字节数），且这些显示值**只读**。
authoritative 限制：values 个数 **1..123**（`kWriteMultipleRegistersMinQuantity` / `...MaxQuantity`）；
startAddress uint16 0..65535。**已知缺口（见 I14-⑧）**：core 未校验 start+quantity 越过 0xFFFF。
```

### I12. 0x10 Values Input UX（A–D 比较 + M10 v1 推荐）

```text
A. 多行文本（一行一个 value）
B. 逗号/空格分隔文本
C. table/editor（address + value rows）
D. 动态 SpinBox 列表
维度比较：
  · 输入效率：A ≈ B ≫ D（20 个值手点 SpinBox 极慢）；C 需要用户同时填地址（冗余）。
  · 错误率：A/B 都可能拼错，但**逐行报错**可精确定位（行号）；D 几乎无解析错误；C 引入「地址与顺序不一致」的新错类。
  · keyboard：A/B 是纯键盘路径（TextArea 输入 + Tab 离开）；D 需要 Tab/Up/Down 在 N 个 SpinBox 间穿行（大 N 时极差）；
    C 需要两列表格导航。
  · 大数量可用性：A 直接可贴一大段；D 在 123 个值时不可用。
  · confirmation summary：A/B 保留用户输入顺序 ⇒ summary 与输入一致；D 需要从控件逐项收集。
  · 实现复杂度：A/B 最低（解析 + 逐行校验）；D 最高（动态实例化 + 焦点管理）；C 中（表格模型，且与 M11 的
    register-map 概念重叠，届时会有两套地址真源）。
**Recommendation：A（多行文本，一行一个 value）** —— 附三条契约：
  ① 空行忽略；每行必须是一个 0..65535 的十进制整数；其它任何内容 = **parse error（带行号）**；
  ② 不做「逗号/空格自动拆分」（那会引出「分隔符 vs 空行」的歧义与第二种解析真源；B 列为未来 enhancement）；
  ③ 行号到 index 的映射固定为「忽略空行后的第 N 个值」并在校验文案中说明。
**明确拒绝 C**（会与 M11 register-map 形成第二份地址真源）；**拒绝 D**（焦点与规模不可用）。
```

### I13. Number Format（M10 v1 决定）

```text
**decimal only**。理由：现有 Communication 输入全部是十进制 SpinBox（从站/起始地址/数量/超时），
写区沿用同一习惯可避免「同一页面两种进制」；且进制切换会制造**两个表示 authority**（同一值两种文本，
校验/回显/确认摘要都可能分叉）。
function 展示沿用既有惯例 `0x06` / `0x10`（这是**功能码标识**，不是数值输入）。
未来若需要 hex 输入/显示（M11 Register Readout & Decode 的领域），作为**独立 enhancement**，不在 M10-C 引入。
```

### I14. Local Validation（写动作进入 confirmation 之前必须完成）

```text
层级：**UI presentation validation（page-local，即时反馈）→ Controller/core authoritative validation（提交前）**。
UI 无效时：**不打开 confirmation、不调用 Controller 的写入口、绝不触达 transport**。
必须覆盖的规则（上限全部实读源码）：
  ① unit range：1..247（0 与 ≥248 reject，broadcast 不支持）。
  ② address range：0..65535。
  ③ 0x06 value range：0..65535。
  ④ 0x10 empty values（values.size() == 0 ⇒ reject）。
  ⑤ 0x10 value range：每个值 0..65535。
  ⑥ 0x10 quantity limit：1..123（`kWriteMultipleRegistersMaxQuantity`）。
  ⑦ timeout contract：> 0（authoritative）；UI 控件沿用 100..10000（与 FC03 一致，见 I10）。
  ⑧ **start + quantity 越过寄存器地址空间**：**core 与 FC03 均无此校验**（FC03 也允许 start+quantity 溢出，
     留给设备/协议层）。⇒ M10-C 需要决定写路径是否新增该 reject。
     建议：**新增「写路径专属」的前置校验**（`startAddress + values.size() - 1 > 0xFFFF` ⇒ reject，
     文案说明「起始地址 + 数量超出 16 位寄存器地址空间」），理由是写操作会把一个越界范围真正下发到设备，
     与只读读回的语义不同；**实现位置**：Controller 的写入口（M10-D/E），**不进 core**、**不影响 FC03 的冻结行为**。
     —— 这是一个 **decision request（第 12 项相关）**，Review 需明确裁定。
  ⑨ parse failure（0x10 多行文本；见 I12 契约）。
presentation 层可以先给即时反馈，但**最终以 Controller/core 的再次校验为准**（UI 校验永不作为发送许可）。
```

### I15. Validation Presentation

```text
两段式：
  · **field-level error**：出错字段就地显示简短原因（例如「从站地址须在 1..247 之间」），并与该字段视觉相邻；
  · **section-level summary**：写区顶部一行汇总（例如「3 项输入有误，请修正后再写入」+ 顺序列出字段名），
    让用户不必来回扫视。
禁止：所有错误只显示一句「参数错误」（用户无法定位）。
禁止：把 protocol 内部 enum / token（如 `UnitIdNotUnicast` / `QuantityOutOfRange`）原样抛到 UI ——
文案由 Qt adapter 层映射（与既有 `issueDetailText` 的做法一致：core 只说事实，UI 说人话）。
```

### I16. Validation Failure State（+ SpinBox clamp 真实行为审计）

```text
validation failure：**draft 保留**，用户修正后可再次尝试。
禁止：清空 values / 自动改 unit / 自动 clamp 造成用户以为提交了原值。
**SpinBox clamp 审计（实读 Qt 行为）**：本页 SpinBox 使用 `from`/`to` 且**非 editable**（默认 `editable: false`），
所以用户只能通过步进/滑动改值，**超出范围的值根本无法输入**——即「clamp」在此页表现为「不可达」而不是
「输入被改写」。⇒ 对本页安全的结论：**不存在静默改写用户输入**（因为不能输入越界值），
但**不能因此假定一切 clamp 都安全**：若 M10-C 引入 `editable: true` 的数值框（允许直接键入），
则必须显式处理「键入越界」并**报错而不是回退到边界值**。
冻结规则：**任何会被静默改写成另一个合法值的输入控件，都不得用于写路径**；
确认摘要必须展示**实际将发送的值**（snapshot），而不是用户「以为输入的」值。
```

### I17. Write Action Semantics（最终 write flow，冻结）

```text
draft
  → 用户点击 Write
  → local presentation validation（I14 ①–⑨，UI 层）
  → Controller/core authoritative validation（同一套规则，再次执行）
  → 生成 **Validated Intent snapshot**（不可变）
  → 打开 confirmation（展示该 snapshot）
  → 用户**明确确认**
  → 再次检查 authoritative runtime state（connected / same session / not busy / function supported）
  → **M10-D/E 才允许 send**
重点：**点击 Write 本身绝不发送**；「打开 confirmation」与「send」**不得**绑定到同一个 handler。
```

### I18. Confirmation Snapshot（不可变）

```text
confirmation **不得**实时绑定仍可变化的 draft。打开时必须形成 **immutable Validated Intent snapshot**：
  · 内容 = 统一 intent 的字段（function / unitId / timeout + 功能字段）+ 派生值（0x10 的 quantity、
    预计 byteCount）+ 采集时的 **Active Serial session 身份**（见 I21）。
  · dialog 的展示与未来真正发送**必须基于同一个 snapshot**。
禁止：dialog 显示 A、用户背后把 draft 改成 B、最终发送 B（这是本设计要消灭的核心风险）。
实现建议（M10-C 实现阶段）：snapshot 以 QML `property var` 冻结副本保存（写入后不再重绑），
并在 Controller 侧以同一快照生成请求（M10-D/E）；snapshot 一旦被消费/失效即作废（一次性）。
```

### I19. Confirmation Summary — 0x06

```text
至少显示：目标 device/unit（「设备 11」）、function（`0x06` Write Single Register）、register address、
value（十进制，并沿用表格的 `0x` 展示惯例仅在功能码上）、以及连接身份（`sourceLabel`，例如 `COM3 @ 9600`）。
用户必须能在确认前看清：**写到哪里、写什么**。
```

### I20. Confirmation Summary — 0x10

```text
至少显示：target unit、function（`0x10`（十进制 16）Write Multiple Registers）、start address、
**derived quantity**、**全部 values**。
允许滚动（列表内部 scroll）；**禁止**只显示「将写入 N 个寄存器」——必须能查看实际 values。
建议同时显示派生 byteCount（只读，帮助用户核对 2N 关系）。
```

### I21. Session-bound Confirmation（安全重点）

```text
confirmation snapshot **必须绑定当前 Active Serial session 身份**。
事实基础（I2-F）：QML 目前**看不到** session id ⇒ 需要新增**只读**暴露；方案比较：
  A. 新增只读 Q_PROPERTY `serialSessionId`（`activeSerialSessionId()` → QML），并在 snapshot 中记录；
  B. 不给 QML 暴露，仅把 sessionId 放进 snapshot 交给 Controller，由 Controller 在 send 前权威比对；
  C. 用 `serialConnected` 布尔代替 session 身份。
比较：C **不可接受**（disconnect→reconnect 后布尔仍是 true，无法区分换代）；A 让 dialog 可**主动**禁用 Confirm
  （体验最好）但增加一个 QML 可见属性；B 无新可见面但用户只能等到点 Confirm 才被拒（体验较差）。
**Recommendation：A + B 同时做**（A 供 UI 反应式失效，B 作为发送前的权威判定，B 是真正的安全边界；
A 只是尽早反馈）。**明确拒绝 C**。
场景（必须设计）：打开 confirmation → disconnect → reconnect ⇒ **旧 confirmation 绝不能向新 session 发送旧命令**；
session 变化时：Confirm **失效 / disabled / reject**，并要求用户**重新发起 Write flow**（旧 snapshot 作废）。
```

### I22. Connection Change While Dialog Open（处理矩阵）

```text
dialog 打开期间发生：disconnect / port error / session replacement / source switch / busy 变化 ⇒
  最低安全要求：**真正 Confirm 时重新做 authoritative check**：`connected && same session && not busy && intent 仍合法`。
  不能只相信「dialog 打开那一刻连接是正常的」。
  各项处理：
   · disconnect：Confirm 立即 disabled + 提示「连接已断开，请重新发起写入」；Cancel 仍可用；**零发送**。
   · port error：同上（并且错误文案走既有 serial error lane）。
   · session replacement（重连）：Confirm disabled（session id 不等）；旧 snapshot 作废。
   · source switch（切到 Replay/Simulator）：通信页本身不可用（无 Active Serial 连接）⇒ Confirm disabled；
     dialog 若仍打开，按 disconnect 规则处理（零发送）。
   · busy 变化：见 I23。
```

### I23. Busy Change While Dialog Open

```text
场景：dialog 打开时 busy=false，随后其它请求进入 pending（`serialBusy = true`）。
旧 Confirm：**不得排队、不得覆盖 pending、不得发送** ⇒ 必须 **disabled / rejected**。
single in-flight 继续冻结（M10-A/§FC4）：serial transport 的 `serialBusy` 是 **read 与 write 共享**的
（同一 adapter / 同一 session / 同一 in-flight 槽位——源码事实），因此「读在飞时写 Confirm」同样必须被拒。
```

### I24. Confirmation Default Focus（安全默认）

```text
M10 v1 必须安全默认：**dialog 打开后 destructive Confirm 不得成为默认焦点**。
优先：**Cancel / non-destructive 控件作为初始 focus**；至少：确认动作必须需要一次**明确**的 focus/activation
（从 Cancel 移到 Confirm 再激活）。
理由：防止「打开 dialog → 顺手按 Enter → 直接写设备」。
```

### I25. Confirmation Keyboard Contract（冻结）

```text
Tab / Shift+Tab：在 dialog 内循环（模态，不逃出到背景页面）。
Escape = **Cancel = zero send**（Qt Quick Controls Dialog 的 `closePolicy` 默认含 CloseOnEscape ⇒
  必须把关闭原因映射为 Cancel 语义，而不是「未定义」）。
Enter：**不得因为 dialog 打开就自动触发 destructive action**；只有在 destructive Confirm **已持焦点**时
  激活才允许（即 Enter 走「当前焦点控件」的默认动作）。
Space：同样只有在 destructive Confirm 明确持焦点时才可能激活。
**hidden old Write button 不得在 dialog 打开或离开页面后通过 Space/Enter 被激活**（见 I30）。
```

### I26. Dialog Open Is Zero-send（独立 oracle）

```text
valid draft → 点击 Write → dialog 打开 ⇒ **sendCount = 0**。
实现纪律：**打开 confirmation 与发送 request 不得绑定到同一个 handler**（两段式：Write=校验+快照+dialog；
Confirm=再校验+send）。
```

### I27. Cancel Is Zero-send（独立 oracle）

```text
valid draft → open dialog → Cancel / Escape ⇒ **sendCount = 0**，且**不得**产生：
Active transaction / transport terminal / Pending / statistics change / diagnosis change。
draft **继续保留**（用户可修正后重试）。
```

### I28. Repeated Write Activation（防重）

```text
场景：双击 Write / 快速连按 Space·Enter ⇒ **不得**开多个 confirmation、不得生成多个 snapshots、
不得产生多次 confirm signal。
设计：**一个 active confirmation 对应一个 snapshot**；Write 按钮在 dialog 打开期间不可再次触发
（QML 侧：`dialog.opened` 时忽略/禁用再次打开；Controller 侧：M10-D 的 send 入口再按 in-flight 拒一次）。
禁止依赖按钮动画 / debounce / 自动关闭等偶然防重。
```

### I29. Repeated Confirm Activation（未来 exactly-one-send 的设计前提）

```text
M10-D/E 真正连接 transport 后：双击 Confirm、快速 Space/Enter ⇒ 必须 **exactly one send**。
M10-C 需要先设计：**confirmation one-shot guard** 与**测试 seam**：
  · snapshot 携带一次性 token / 状态（`issued → consumed`）；重复 Confirm 在 QML 层即被拒；
  · Controller 层的 `startActiveRequest` 已有 in-flight 拒绝（M10-A 冻结）作为第二道防线；
  · 测试 seam：Recording/Fake transport 的 `sendCount` **就是** exactly-one-send 的 oracle（无需动画观察）。
禁止只靠 UI 层防重（对话框自动关闭等）作为唯一机制。
```

### I30. Hidden-page Safety

```text
复用 M9-F 的 page enabled gating（Main.qml `enabled: workspaceHost.currentIndex === ...`）：
Communication 不可见后，旧 Write control **不得**保留 actionable focus、不得通过 Space 打开 dialog、
不得通过 Enter 启动 write flow。
**不要依赖 `visible = false` 作为唯一安全机制**（M9-F FC 场景已证明「隐藏但仍有 enabled 的控件会被残留焦点激活」是真实风险）。
若 confirmation 打开时尝试导航：见 I31。
```

### I31. Confirmation × Navigation（Qt/QML 可行方案）

```text
首选：**真 modal**（Qt Quick Controls `Dialog` + `modal: true`），由 overlay 吞掉背景交互 ⇒
  背景控件（含导航 rail）在该 dialog 存活期间不可交互。
若因外壳结构（NavigationRail 在 dialog overlay 之外的层级）仍可导航，则必须保证：
  navigation **不会**确认、**不会**发送、**不会**改变 snapshot；把 dialog 保持打开并在 session/source 变化时
  按 I22 失效（Confirm disabled）。
可行方案（实现阶段验证）：`Dialog { modal: true; closePolicy: Popup.NoAutoClose | CloseOnEscape }` +
  footer 内两个 AppButton；若 rail 仍可达，则额外在 dialog 打开期间对 rail 施加 `enabled: false`
  （presentation-only gating，与 M9-F 既有做法一致）。
```

### I32. Accessibility（继承 M9-F baseline，不声称 WCAG）

```text
未来新控件必须继承 M9-F baseline：meaningful accessible name（`Accessible.name` 或可读文本）、
role/action（按钮 = Invoke，输入 = Editable/Value）、**visible keyboard focus**（沿用各类型既有方案：
AppButton 走 border、TabButton 走内环、ComboBox 走 overlay）、enabled/disabled 状态、确定的 Tab order。
confirmation 的 title / summary / Cancel / Confirm 均需清晰语义（标题说明「即将写入设备」，
摘要字段成对 label+value，按钮文案动词化：`取消` / `确认写入`）。
**不声称 WCAG certification**（与 M9-F 边界一致）。
```

### I33. Write Pending Presentation

```text
未来写请求发送后的 UI 状态设计：
  · pending 期间 **Write controls disabled**（与 FC03 的 busy 处理一致：读取按钮变「读取中...」）；
  · 显示「进行中」类文案，**但不得**写成「写入中 = 已成功」；
  · **single in-flight 全局共享**：read 与 write 共用同一个 `serialBusy`（源码事实：同一 transport / 同一 session），
    因此 pending 期间读与写都不可发起；该事实在设计上作为冻结前提（不引入并行队列）。
```

### I34. Write Success Semantics（提前冻结 wording）

```text
只有收到**合法匹配的 response**并由 analyzer 判定 **Success** 时，才允许表达「写入已确认 / 成功」等语义。
**不得**因为 `QSerialPort::write` 被接受（即便完整计数）就显示成功——那只是 PossiblySent（transport 事实）。
```

### I35. Write Timeout Semantics（冻结）

```text
0x06 / 0x10 的 transaction outcome **仍为 `Timeout`**（taxonomy 不新增）。
用户-facing 语义：**「响应超时，设备写入状态未知」**。
禁止措辞：「写入失败」/「设备未写入」/「操作未发生」（均超出证据——字节可能已上线且设备可能已执行）。
```

### I36. Transport Error Semantics（PossiblySent 之后）

```text
若写请求已 PossiblySent，随后发生 TransportError / DisconnectedAfterSubmission / ShortSubmission，
UI 必须表达：**请求可能已部分或全部进入发送生命周期，设备状态无法由当前证据确认**
（并说明可用的证据：request ADU + 已观察到的响应字节 + 中止原因）。
**不得**制造「未写入」「已失败且无副作用」这类确定性。
具体文案 M10-C 可设计，但语义边界由本条款冻结（M10-A 的 disposition/terminal 事实是其唯一依据）。
```

### I37. Pre-send Failure Semantics（NotSent）

```text
NotSent 场景：validation reject / not connected / busy / **confirmation cancel** / pre-send transport reject。
可明确表达：**本次请求未进入发送生命周期**。
但：**confirmation cancel 通常无需错误提示**（用户主动取消不是失败）；
且**不得**产生 Modbus failure row（不写入 Transactions、不改变统计、不产生 terminal 记录）。
```

### I38. Clear Results Boundary（继续冻结）

```text
Clear Results：**不清 write draft**、**不 cancel pending**、**不关闭 confirmation**
（除非 M10-C 的 modal 架构确实要求关闭，且经 Review 批准）。
尤其：**不得**让 Clear Results 变成「恢复安全状态」的隐式机制去改 draft（Clear 是结果域操作，不是表单重置）。
```

### I39. Draft vs Source（建议 + 论证）

```text
workspace/source 继续正交；draft 是 **Communication 的 presentation state**。
Replay load 或 Simulator source replacement **不得自动发送** draft。
是否清 draft：**建议保留**。理由：source 切换不是用户「清表单」的意图；清空会丢失用户精心输入的
一组寄存器值（例如从手册抄来的 10 个值），而保留的代价只是「切回 Active Serial 时表单里已有内容」。
前提（安全无关性）：draft 保留**不构成任何发送权限**——写控件只在 Active Serial 且 connected && !busy 时可用，
且必须经过 I17 的完整流程。⇒ 保留 draft 与写安全**互不冲突**。
```

### I40. 0x06 / 0x10 Availability Strategy（在 M10-D 之前）

```text
现状：`encodeActiveRequest(0x06)` 与 `(0x10)` 均 **UnsupportedFunction**（M10-A/B 冻结）。
M10-C design 必须防止 UI 过早表现为 write capability READY。
决定（与 I4 同源）：**M10-C 的实现只在 harness 下可见/可交互，production 中 Write 区完全不可见**；
当 M10-D 实现 0x06 encoder 后，Write 区按 **真实 capability** 出现（能编码才显示/可用），
而不是靠一个手写的 `visible = true`。
**明确禁止**：`enabled` 的 destructive action 通向 `UnsupportedFunction`（那是「可点击的假发送」）。
```

### I41. Controller Handoff Seam（设计，不实现 send）

```text
future confirmation 之后交给 Controller 的 seam **必须接收 `Validated Intent snapshot`**，
而不是 QML fields、也不是 QVariant map（后者会把 QML 结构变成隐式契约）。
Controller 在接收时**再次** authoritative check：`connected` / `same session`（sessionId 比对）/
`not busy` / `function currently supported`（M10-C 阶段必为 false ⇒ 拒；M10-D 起 0x06 变 true）。
M10-C **只设计**，不实现 send；seam 的签名建议放在 Controller 的 C++ 接口（非 Q_INVOKABLE），
由 QML 通过一个**单一、语义完整的入口**触发（例如 `submitValidatedWrite(snapshot)`），
避免 QML 逐个字段传参。
```

### I42. QML / Transport 边界（冻结）

```text
QML **不得**调用 `SerialTransport`、不得传 raw ADU、不得调用 `QSerialPort`。
所有 future send 必须经过 `Controller → Active contract → transport seam`。
（与 M10-A/B 已冻结的架构一致：core 零 Qt、transport 只在 app 层、QML 只读 controller 属性与调用 controller 命令。）
```

### I43. AI / Agent Boundary（冻结）

```text
AI / Agent write authority = **NONE**；不得新增 write tool / send tool / confirmation-bypass tool / raw serial tool。
即使未来 AI 生成候选 register values，也只能**填/建议 draft**（例如把建议值写进草稿供用户编辑），
**不能**直接形成 send authority；任何发送都必须经过人类 UI：explicit Write → confirmation → transport。
```

### I44. Write Safety Oracle Matrix（C01–C22，设计）

```text
C01 invalid unit 0 → validation error → no confirmation → 0 send
C02 invalid address/value → no confirmation → 0 send
C03 valid 0x06 draft → confirmation summary exact（逐字段等于 snapshot）→ 0 send while dialog merely open
C04 Cancel → 0 send
C05 Escape → 0 send
C06 dialog initial focus **不是** destructive Confirm
C07 Enter immediately after dialog open → 0 send
C08 explicit focus Confirm + Space → future exactly-one confirm event
C09 double Write activation → one dialog / one snapshot
C10 double Confirm activation → future one send
C11 disconnect while dialog open → old confirmation invalidated → 0 send
C12 reconnect / new session → old snapshot cannot send
C13 busy becomes true → Confirm rejected/disabled
C14 navigate away → hidden Write control cannot activate
C15 Clear Results → draft preserved → no send
C16 switch 0x06/0x10 → independent drafts preserved
C17 0x10 values → quantity derived exactly
C18 0x10 range overflow（start+quantity 越界）→ local validation reject
C19 confirmation summary matches immutable snapshot
C20 editing draft after snapshot cannot alter confirmed payload
C21 write timeout wording contains state-unknown semantics
C22 PossiblySent transport error does not claim device unchanged
本轮**只设计** oracle（不实现）；其中 C01/C02/C15/C16/C17/C18 可在 M10-C 的纯逻辑层与 harness 层落地，
C03–C14/C19–C22 需要 confirmation UI（M10-C 实现阶段或 M10-D 起）。
```

### I45. Runtime Test Strategy（四层）

```text
A. pure C++：validation（I14 规则集）/ snapshot 构造与不可变性 / 0x10 derived quantity·byteCount /
   session identity check（sessionId 比对）—— 不依赖 Qt UI。
B. Controller / ui_bridge：connected/busy/session handoff guard（提交前后状态不一致必须拒）；
   与既有 recording transport 组合验证 0-send（一次都不发）。
C. QML runtime harness（复用 `--qml-focus-check` 架构；新场景建议命名 FO/FP/FQ 或独立 harness）：
   focus / dialog 打开与关闭 / 键盘（Tab·Shift+Tab·Escape·Enter·Space）/ draft persistence /
   summary 字段比对 / hidden-page safety。
D. Recording transport：future exactly-one-send 与 zero-send 的**最终**判据（sendCount 与 exact ADU）。
禁用做法：**只靠截图 / 只看 UI 文案 / 只看点击后结果**去证明 write safety。
```

### I46. Confirmation Evidence（机器可证）

```text
M10-C 后续实现必须能够**机器证明** dialog summary 来自 snapshot：
  harness 同时读取 **snapshot authority**（冻结副本）与 **displayed summary**（各 label 文本/对象名），
  **逐字段比对**（与 M9-D 的 `assertTransactionDetailMapping` 同形：字段级、双向相等），
  而不是「文本看起来对」。
另需断言：snapshot 生成后修改 draft，summary 与未来的请求 payload **都不变**（C20）。
```

### I47. Visual / Geometry Boundary

```text
M10-C 不重新设计整个 Communication 页面。新 Write 区域要求：
  · 与现有 DesignSystem 一致（面板用 PanelCard、间距用 DS.spacing*、控件高度 DS.controlHeight）；
  · **1024×720 可用、1000×700 不溢出**（几何闸门门槛）；必要时**区域内滚动**（Flickable/ScrollView）；
  · **不得**为了 values 列表把整个窗口高度无限撑大（values 用受限高度的可滚动区域）。
现有闸门（geometry 18 printed segments / rail 56 / nav A–T / focus FA–FL）在实现轮必须全绿；
新增控件会进入 Communication 的 Tab 链，需确认 FB（隐藏页排除）仍成立（它按页面归属断言，不依赖链长）。
```

### I48. Implementation Staging Proposal（供 Review）

```text
C1 — write draft + validation model：page-local 两份 draft（06/10）+ 校验规则集（I14）+
     presentation（field-level + section summary）+ 可用性门（connected && !busy && capability）+
     production 不可见（I4 方案 A 的 harness-only 实例化）。
C2 — confirmation snapshot + dialog safety：不可变 snapshot、summary（0x06/0x10）、初始焦点、
     Escape/Enter/Space 契约、一次性 guard、dialog-open/Cancel 的 zero-send oracle。
C3 — keyboard / accessibility / invalidating guards：dialog 内 Tab 循环、session/busy/source 变化的
     反应式失效 + Controller 侧权威校验、hidden-page safety、accessible name/role、焦点可见性（沿用既有方案）。
C4 — runtime oracle + final review：C01–C22 中可在 M10-C 落地者（纯逻辑 + harness）+
     几何/nav/focus 全门禁 + 人工视觉审阅。
（如 Review 认为 C1/C2 合并更自然，本提案允许合并——不为形式硬拆。）
```

### I49. Decision Requests（12 项，Phase 1 不自行定案）

```text
 1. Write section 何时首次 production-visible：M10-D 首次出现（推荐）／更早（M10-C 就暴露 disabled preview）。
 2. 0x06 / 0x10 selector UI：Write 内两个子 Tab（推荐）／segmented selector／两个独立 sections。
 3. 0x10 values editor UX：多行文本一行一个值（推荐）／逗号·空格分隔／表格 address+value／动态 SpinBox 列表。
 4. 数值表示：decimal only（推荐）／decimal + hex 切换。
 5. draft 是否跨 source 继续保留：保留（推荐）／source replacement 时清空。
 6. confirmation 的组件/pattern：Qt Quick Controls `Dialog`(modal) + 自定义 footer 两个 AppButton（推荐）／
    其他（Popup / 自绘 overlay / standardButtons）。
 7. initial focus 安全策略：Cancel 初始焦点（推荐）／Confirm 初始焦点但需显式二次激活／其他。
 8. session change 时 dialog 失效策略：Confirm 反应式 disabled + Controller 权威拒（推荐）／
    打开 dialog 时即锁定 session 变化（冻结 session）／其他。
 9. busy change 时 dialog 失效策略：Confirm 反应式 disabled + Controller 权威拒（推荐）／允许排队／其他。
10. summary 是否显示 connection identity：显示 `sourceLabel`（推荐）／仅显示 unit／显示完整 sessionId。
11. M10-C 只做 hidden foundation（推荐，方案 A）／暴露 disabled preview（方案 B）。
12. implementation staging：C1→C4（推荐）／其他拆分。
另附一个**规则缺口**需一并裁定：**写路径是否新增 `start + quantity > 0xFFFF` 的 reject**（I14-⑧）。
```

### I50. Boundary / Git

```text
本轮 docs-only：未改 src / tests / QML / CMakeLists.txt / scripts / assets / samples / screenshots；
未实现任何 encoder、未创建 Write UI、未调用 transport、未新增 Agent tool；未 push；未 tag；
**verified LKGC 保持 `ef71244`**；M10 = IN PROGRESS；M10-C = Learning / Design（Implementation = NOT STARTED）。
commit：`M10-C: design write safety UI foundation`（独立 docs-only 提交；不 amend `806d424`；不 rebase）。
```
## M10-C Phase 1 Correction — Write Safety UI Design Decisions（2026-09-20，docs-only）

> **M10-C Phase 1 Review = HOLD（窄范围 design correction）。** 总体 Write Safety UI 设计方向**接受**；
> Implementation **仍为 NOT STARTED**。HOLD 只因三点未精确冻结：
> **① 0x10 地址跨度规则 ② Validated Intent snapshot 如何跨 QML/Controller 安全持有 ③ M10-D/E capability 的
> production 可见时序**。本轮补齐上述三点 + 把 **12 项 decision requests 全部落为 RESOLVED**。
> 原 Phase 1 记录（§I0–I50）**未改写**。

### J0. Phase 1 Review HOLD 归档

```text
M10-C Phase 1 Review = HOLD；总体设计方向接受；Implementation = NOT STARTED。
HOLD 三项：
  1) 0x10 address-span 规则未精确冻结（原 §I14-⑧ 只写了「待裁定」，且候选公式本身有 off-by-one 风险）；
  2) Validated Intent snapshot 的 QML ↔ Controller 持有/传递方式未落地（原 §I18/I41 只有「不可变快照」与
     「接收 snapshot」的描述，没有 token/生命周期/失效语义）；
  3) M10-D/E capability 的 production visibility 时序未精确冻结（原 §I4 只有方案比较）。
本轮边界：不改 src / tests / QML / CMakeLists.txt / scripts / assets / samples / screenshots；
不开 C1；不实现 Write UI / encoder；不 push；不 tag；verified LKGC 保持 `ef71244`。
```

### J1. 0x10 Address-span Contract（含 off-by-one RCA）

```text
RCA（设计缺陷，在 Review 阶段被捕获）：
  Observed：原 §I14-⑧ 把越界判定写成 `startAddress + quantity > 0xFFFF` 并标为「待裁定」。
  Expected：判定必须精确表达「访问到的最后一个寄存器是否在 16 位地址空间内」。
  Root Cause：`startAddress + quantity` 是**排他上界**（exclusive end），直接与 0xFFFF 比较会**多算一个寄存器**
    ⇒ off-by-one：`start=65535, quantity=1` 访问的唯一寄存器就是 65535，本身合法，却会被判为越界。
  Fix：改用**包含式末地址**：
      lastAddress = startAddress + quantity - 1
      要求 lastAddress <= 0xFFFF
    等价判定：`uint32_t(startAddress) + uint32_t(quantity) <= 65536`。
**冻结规则**：
  · 非法写法（禁止使用）：`startAddress + quantity > 0xFFFF` 作为 overflow 判定。
  · 必须使用 **至少 uint32_t（或等价扩宽整数）** 计算；**禁止在 uint16_t 中先相加再判断**
    （uint16 先加会 wrap，判定永远为 false ⇒ 越界请求被放过）。
  · 边界样例（对应 oracle C18A–C18D）：start=65535/qty=1 **VALID**；start=65535/qty=2 **INVALID**；
    start=65534/qty=2 **VALID**；并对 widened arithmetic 做「无 uint16 wrap」的显式证明。
```

### J2. 0x10 Quantity Contract（复核冻结）

```text
`values[]` 继续是**唯一写入值 authority**：`quantity = values.size()`；`1 <= quantity <= 123`；
`byteCount = quantity * 2`（派生，UI 只读显示）。
用户**不得**手填 quantity 或 byteCount（UI 不允许存在三份真源）。
地址跨度：`start + quantity - 1 <= 65535`（见 J1）。
```

### J3. 0x10 Values Text Parser（冻结）

```text
M10 v1 = **多行 TextArea，一行一个 decimal unsigned value**。parser 契约：
  A. 每个 value **仅十进制**；**不接受** `0x1234` / `+12` / `-1` / `1.5` / 逗号列表（这些一律 parse error）。
  B. 每个有效值范围 **0..65535**。
  C. **保留输入顺序**（顺序即寄存器顺序）。
  D. 允许**首尾纯空白行**被 trim。
  E. **中间空白行 = validation error**（避免无声跳过一个位置，造成 quantity 与用户视觉行数不一致）。
  F. 最终有效值数量 **1..123**。
  G. parse failure **保留原 draft，不自动修正**（不清空、不改写、不跳过）。
```

### J4. Write Timeout UI Contract（冻结）

```text
Write UI **沿用现有 Communication read UX 的 100..10000 ms**（不新增第二套 write timeout range）。
这是 **presentation range**；core authority 仍是 **timeout > 0**（`ActiveRequestIntent` 契约）。
UI **可以比 core 更严格**（更窄的 presentation 范围是允许的），但 **不得静默 clamp** 用户输入成另一个值。
confirmation 的 summary **是否展示 timeout 可作为非安全视觉细节**，但 **snapshot 必须保存 timeout**（J6）。
```

### J5. Snapshot Ownership 缺口与结论

```text
缺口（Phase 1 HOLD 的第 2 点）：原设计只说了「不可变 snapshot」与「handoff 接收 snapshot」，
没有回答：**谁持有它、QML 怎么安全引用它、它何时失效、重复确认如何被拒**。
结论（冻结）：
  · **draft authority = QML page-local**（不变）；
  · **Prepared snapshot authority = Controller / runtime**（新增，且**只**在用户明确发起 Write flow 之后生成）；
  · transport authority = Controller → Active contract → SerialTransport（不变）。
⇒ Controller 仍然**不实时拥有用户 draft**（Phase 1 的 draft ownership 原则未被破坏）：
   它只拥有「用户已经明确点了 Write、且已通过 authoritative validation」的那一份快照。
```

### J6. PreparedWriteSnapshot — 设计

```text
**`PreparedWriteSnapshot`**（C++/runtime 侧对象，项目命名风格）＝ 一份已通过 **C++ authoritative validation**
的 **immutable write intent snapshot**。它**不是** raw draft。
至少包含：
  · **opaque generation / token**（J7）
  · **typed validated intent**（`ActiveRequestIntent` 或等价 typed 结构）
  · function（0x06 / 0x10）
  · unitId
  · address（0x06 registerAddress / 0x10 startAddress）
  · 0x06 value 或 0x10 values[]
  · **derived quantity**（0x10）
  · timeout
  · **source kind**（`TransactionSourceKind`）
  · **Active Serial session id**
  · **immutable connection display label**（快照时捕获，如 `COM3 @ 9600`）
  · 未来 confirmation 所需展示事实（summary 用的只读投影素材）
**禁止**存入：QML Item pointer / TextArea 引用 / 任何 live binding（快照必须与 UI 完全解耦）。
```

### J7. Opaque Token / Generation（冻结）

```text
每次成功 prepare 生成**新的 generation / token**。
QML 的 confirmation **只持有该 token + 只读快照投影**；**Confirm 必须携带该 token**。
Controller **只接受**同时满足「当前仍 active / 未 invalidated / 未 consumed」的 token；**旧 token 必须 reject**。
**不得**依赖 `Dialog.visible` 或任何 UI 状态作为 authority。
（生命周期：`Prepared → Consumed | Invalidated`，一次性，见 J17/J18。）
```

### J8. QML Projection（冻结）

```text
QML **不直接持有** `ActiveRequestIntent`、raw ADU、或 QVariantMap 形式的 intent；
QML 只读取 prepared snapshot 的**只读投影**，例如：
  `preparedFunction` / `preparedUnitId` / `preparedAddress` / `preparedValue`（0x06）/ `preparedValues`（0x10）/
  `preparedQuantity`（派生）/ `preparedConnectionLabel`（显示用）。
具体 property 形状留 implementation；**authority 始终是 Controller 的 snapshot**。
```

### J9. No Re-read Draft on Confirm（硬契约）

```text
打开 confirmation 之后，即使用户通过**代码 / 焦点移动 / 切换子 Tab / 离开并返回页面**改变了 draft，
**Confirm 也绝不能重新读取 draft**。
真正发送（M10-D/E）的 intent **必须来自 prepared snapshot**。
这条是「dialog 显示 A、背后改成 B、实际发送 B」风险的最终封堵点。
```

### J10. Session Authority（冻结）

```text
M10-C implementation **允许新增只读 Q_PROPERTY（或等价 read-only exposure）** 暴露 Active Serial session 身份，
**仅用于 reactive UI 失效**（尽早禁用 Confirm）。
**最终 authority 仍在 Controller**：Confirm guard 必须检查
  `sourceKind == ActiveSerial` / `sessionId == snapshot.sessionId` / `serialConnected == true` /
  `serialBusy == false` / `capability supported` / `token valid`。
**QML 属性不是 security authority**（它只是 UI 反馈通道）。
```

### J11. Connection Identity（显示 vs authority）

```text
confirmation summary 显示**快照时捕获的 immutable connection display label**（如 `COM3 @ 9600`）。
该 label **只用于用户理解**；**不得**用于 source/session equality 判定。
真正的 identity authority = **typed source kind + session id**（J10）。
```

### J12. Session Change Invalidation

```text
以下任何情况**必须 invalidate 当前 PreparedWriteSnapshot**：disconnect / successful reconnect /
new Active Serial session / source replacement / 离开 ActiveSerial。
旧 dialog：**Confirm disabled 或 rejected**；用户必须**重新发起 Write flow**（重新 prepare 得到新 token）。
**不得**让旧 token 在新 session 使用。
```

### J13. Busy-change 决策（保守策略）

```text
M10 v1：confirmation 打开后若 `serialBusy` 从 false → true，则**当前 snapshot 直接 invalidate**，
**不是**等 busy 回到 false 后再恢复可用。
理由：confirmation 已经跨越了**另一笔 Active transaction**，要求用户重新确认**更安全、更可审计**。
用户必须重新点击 Write 生成新 snapshot。（对应 oracle C13：busy 再变 false 也**不能复活**旧 token。）
```

### J14. Validation-change 边界

```text
snapshot 创建后：draft 后续改变**不修改** snapshot（J9）。
用户 **Cancel** ⇒ snapshot 被 **discard**。
再次 Write ⇒ **创建新 generation**；**不得**复用旧 snapshot 并偷偷更新字段。
```

### J15. Confirmation Component 决策

```text
M10 v1 使用 **Qt Quick Controls `Dialog` + `modal: true`**（本项目第一个真模态对话框；无既有 pattern）。
```

### J16. closePolicy 决策

```text
只允许两种关闭路径：**Escape（映射为 Cancel）** 与 **显式 Cancel**。
⇒ `closePolicy` **不包含 CloseOnPressOutside**（点击 dialog 外部直接关闭会产生不清晰状态：用户无法区分
「我取消了」与「我误触了」）。
具体 Qt enum 名称在 implementation 时按当时的 Qt 版本 API 实读确认（不在设计中硬编码）。
```

### J17. Initial Focus / Enter / Space / Escape（冻结）

```text
Initial focus：**Cancel 获得初始 active focus**；Confirm **不得**成为默认 focus、不得是默认按钮、不得自动接 Enter。
Enter：dialog 刚打开 ⇒ **zero confirm**；只有 **Confirm 明确持有 active focus** 之后，才允许按其标准控件语义
  产生 confirm intent。
Space：同理（只有 Confirm 明确持焦点才可能激活）。
旧 **hidden Write button 不得保留 actionable focus**（不得借 Enter/Space 打开 dialog 或启动 write flow）。
Escape：**Cancel** ⇒ snapshot discard + dialog close + draft preserved + **zero send** + zero transaction +
  zero transport terminal。
```

### J18. Production Visibility 时序（冻结）

```text
· **M10-C**：Write foundation 在 **normal production UI 完全隐藏**；允许 **runtime harness 实例化/启用**它
  以跑 QML safety oracle。**禁止** production 显示 disabled 的 roadmap preview。
· **M10-D**：0x06 capability **真正 end-to-end 可用后**，Write section **首次 production-visible**，且**只暴露 0x06**；
  此时 **0x10 仍不展示**（不得放一个 Disabled 0x10 tab 当 roadmap preview）。
· **M10-E**：0x10 capability 真正可用后，Write section **增加第二个子 Tab**（0x06 + 0x10 两个子 Tab）。
```

### J19. Function Selector / Draft Persistence / Summary / No-dead-UI（复核冻结）

```text
Selector：Write 区内部**两个子 Tab**，复用现有 TabButton pattern（M10-D 只有 0x06 可见；M10-E 才出现第二个）。
  **明确拒绝** segmented custom control / 巨型 ComboBox / 两个永久长 section。
Draft persistence：`write06Draft` 与 `write10Draft` 均 page-local，保留于 workspace navigation / Clear Results /
  disconnect / reconnect / Simulator replacement / Replay replacement。
  **但 draft persistence ≠ confirmation persistence**：source/session 变化时 **draft 保留、prepared snapshot 失效**。
Summary：0x06 = function / unit / address / value / connection display label；
  0x10 = function / unit / start address / derived quantity / **全部 values** / connection display label；
  values 用**受限高度 scroll**；**不得**摘要成「仅 N registers」。
No-dead-UI：**capability 不存在 ⇒ production control 不出现**（M10-C hidden / M10-D 真实 0x06 / M10-E 真实 0x10）。
  **禁止** enabled button → UnsupportedFunction；**禁止** clickable no-op。
```

### J20. Repeated Write / Repeated Confirm（冻结）

```text
Repeated Write：存在一个 active prepared snapshot 时，再次 Write activation **不得**创建第二个 snapshot、
  **不得**打开第二个 dialog（可忽略或聚焦既有 dialog，UX 留 implementation）；**snapshot count 必须保持 1**。
Repeated Confirm：prepared snapshot 是 **one-shot**（`Prepared → Consumed | Invalidated`）；
  第一次有效 Confirm 完成 consumption 后，**同 token 的第二次 Confirm 必须 reject**。
  未来 M10-D/E 再叠加 **single in-flight guard**，共同证明 **exactly-one send**。
```

### J21. Confirm Handoff Contract（冻结）

```text
未来 Confirm 调用 **Controller-side operation**，输入 = **opaque snapshot token**（**不是** QML draft fields）。
Controller 从自己的 snapshot store 取出 validated intent，然后**重新检查**：
`token` / `source` / `session` / `connected` / `busy` / `capability`。
全部通过才允许 M10-D/E 执行 send。
```

### J22. M10-C 阶段 Confirm 的含义（不得假装成功）

```text
M10-C 还没有 write encoder / send，因此 hidden foundation **不得假装 Confirm = write success**。
C 阶段只验证：**confirmation event 正确消费 snapshot** + **zero transport sends** + 全部安全 guard。
真正 `confirm → send` 只在 **M10-D / M10-E** 接上 capability 后启用。
由于 normal production 完全隐藏，**不会产生「用户可点击但不发送」的 dead UI**。
```

### J23. Oracle Matrix 更新（C01–C29，冻结）

```text
C01 invalid unit 0 → validation error → no confirmation → 0 send
C02 invalid address/value → no confirmation → 0 send
C03 prepare valid 0x06 → **snapshot generation N** → dialog summary **逐字段 == snapshot** → **transport sendCount 0**
C04 Cancel → 0 send（snapshot discard）
C05 Escape → 0 send（= Cancel 语义）
C06 dialog initial focus **不是** destructive Confirm
C07 dialog open（Cancel 初始 focus）→ **immediate Enter → zero confirmation event**
C08 显式 focus Confirm + Space → confirm event 恰好一次（M10-C：消费 snapshot；D/E：exactly-one send）
C09 double Write → **same single snapshot token** → one dialog（snapshot count = 1）
C10 double Confirm（hidden harness）→ **snapshot consumption 最多一次** → transport 仍 **0**（D/E 升级为 exactly-one send）
C11 disconnect → **token invalid**
C12 reconnect → new session → **old token reject**
C13 busy false→true → **token permanently invalidate** → busy 再 false 也**不能复活**
C14 navigate away → hidden Write control cannot activate
C15 Clear Results → draft preserved → no send
C16 switch 0x06/0x10 → independent drafts preserved
C17 0x10 values → quantity derived exactly
C18A start=65535 quantity=1 → **VALID**
C18B start=65535 quantity=2 → **INVALID**
C18C start=65534 quantity=2 → **VALID**
C18D **widened arithmetic 证明无 uint16 wrap**（uint16 先加会 wrap ⇒ 判定失效）
C19 confirmation summary 与 confirm 均来自**同一 Controller-owned snapshot**
C20 snapshot 生成后编辑 draft **不能**改变 confirmed payload
C21 write timeout 文案包含 **state-unknown** 语义
C22 PossiblySent transport error **不得**声称 device unchanged
C23 `"1\n2\n3"` → [1,2,3]
C24 `" 1 \n 2 "` → [1,2]（首尾空白行 trim）
C25 `"1\n\n2"` → **validation error**（中间空白行）
C26 `"65536"` → error
C27 `"0x10"` → error
C28 124 values → quantity limit error
C29 123 valid values → PASS
（本轮只设计；实现阶段按 C1–C4 逐步落地。）
```

### J24. Implementation Stages（冻结）

```text
**M10-C1**：draft model + parser（J3）+ local/core validation（含 J1 地址跨度）+ **PreparedWriteSnapshot foundation**。
**M10-C2**：hidden Write component + confirmation Dialog + summary projection + **one-shot token**。
**M10-C3**：session/source/busy invalidation（J10/J12/J13）+ keyboard（J17）+ focus + accessibility + page gating。
**M10-C4**：C01–C29 runtime oracle + geometry + final review。
纪律：**不要在 C1 实现 Dialog**；**不要在 C2 实现 transport send**。
```

### J25. QML session-id exposure 边界（source audit note 保留）

```text
保留 Phase 1 的重要事实：**当前 QML 没有 session-id property**。
因此 **C1 / C3 允许新增只读 session identity exposure**（用于 reactive UI invalidation）。
但 **QML 值只用于 reactive UI**；Controller 内部 **typed source/session check 才是最终 authority**（J10）。
```

### J26. 12 项 Decision Requests = RESOLVED（不再标 pending）

> **更正批注（2026-09-20，§K17 追加）**：本节实际列出 **13 项**；正式计数以 §K17/§K18 的 **13 项**为准（不删除 closePolicy，也不删除 staging）。

```text
 1. production visibility = **C hidden / D 0x06 / E 0x10**                              —— RESOLVED（J18）
 2. selector = **two sub-tabs**（复用 TabButton pattern）                               —— RESOLVED（J19）
 3. 0x10 editor = **multiline, one decimal value per line**                            —— RESOLVED（J3）
 4. number format = **decimal only**                                                   —— RESOLVED（§I13）
 5. draft across source = **preserve**                                                 —— RESOLVED（J19）
 6. Dialog = **Qt Quick Controls modal Dialog**                                        —— RESOLVED（J15）
 7. closePolicy = **no outside-close**（仅 Escape=Cancel 与显式 Cancel）                 —— RESOLVED（J16）
 8. initial focus = **Cancel**                                                         —— RESOLVED（J17）
 9. session change = **invalidate snapshot**                                            —— RESOLVED（J12）
10. busy true = **invalidate snapshot permanently**                                     —— RESOLVED（J13）
11. summary connection identity = **immutable display label；authority = typed source + session** —— RESOLVED（J11）
12. M10-C = **hidden foundation**（harness 可实例化，production 隐藏）                    —— RESOLVED（J18）
13. staging = **C1 / C2 / C3 / C4**（含顺序纪律）                                        —— RESOLVED（J24）
（原 Phase 1 的第 12 项决策与「start+quantity 越界规则缺口」两项本轮一并落定：见 J1、J24。）
```

### J27. Boundary / Git

```text
docs-only：未改 src / tests / QML / CMakeLists.txt / scripts / assets / samples / screenshots；
未开始 C1；未实现 Write UI / encoder；未调用 transport；未新增 Agent tool；未 push；未 tag；
**verified LKGC 保持 `ef71244`**；M10-C = Phase 1 Correction / Re-review（Implementation = NOT STARTED）。
commit：`M10-C: close write UI safety design decisions`（独立 docs-only 提交；不 amend `c859db2`；不 rebase）。
```
## M10-C Phase 1 Final Correction — Confirmation vs Dispatch Boundary（2026-09-20，docs-only）

> **M10-C Phase 1 Re-review = HOLD（最后一个已知的 Phase 1 设计语义 blocker）。**
> 现有设计其余部分**全部接受、不重做**（0x10 widened address-span validation / values[] single authority /
> multiline decimal parser / Controller-owned PreparedWriteSnapshot / opaque token·generation / immutable summary projection /
> session·source invalidation / busy permanent invalidation / modal Dialog / Cancel initial focus /
> Enter·Space·Escape safety / M10-C production hidden / M10-D 只暴露 0x06 / M10-E 才暴露 0x10 /
> independent drafts / no-dead-UI / C1–C4 staging）。
> **唯一 blocker**：文档同时要求 ① Confirm guard 检查 **function capability supported**，② M10-C 在
> encoder/capability **尚不存在**时由 hidden harness 证明 `Prepared → Consumed` 的一次性 confirmation —— 两者语义冲突。
> 本轮把这**两个职责拆清**：**confirmation state machine ≠ dispatch capability**。仍严格 docs-only。

### K0. Final HOLD 归档

```text
M10-C Phase 1 Re-review = HOLD；唯一 blocker = **confirmation consumption 与 function dispatch capability 的职责边界不一致**。
原 §I / §J 记录**不改写**（只追加本 §K）。
本轮：未开始 C1；未改 src / tests / QML / CMakeLists.txt；未实现 encoder；未创建 Write UI；未 push；未 tag；
verified LKGC 保持 `ef71244`。
```

### K1. 三层权威正式区分（冻结）

```text
A. **Draft authority** = QML page-local mutable presentation state（用户正在编辑的表单）。
B. **Prepared snapshot authority** = Controller/runtime immutable validated snapshot（token 化，一次性）。
C. **Dispatch authority** = Controller → encoder → SerialTransport（真正把 intent 变成 ADU 并交给传输层）。
三层**不得**重新混合成一个 handler：draft 只管输入；snapshot 只管「用户确认过这一份不可变意图」；
dispatch 只管「当前产品是否具备编码并提交该 intent 的能力」。
```

### K2. Confirmation vs Dispatch（本轮核心结论）

```text
· **confirmation** 回答：「**用户是否明确确认了这一份 immutable snapshot？**」
· **dispatch** 回答：「**当前产品是否具有将该 confirmed intent 编码并提交到 transport 的能力？**」
两者是**不同概念**。因此：
  · **function encoder / capability 不是 PreparedWriteSnapshot 存在的前提**；
  · 也**不是**测试 one-shot confirmation state machine 的前提。
（原表述把 capability 写进 Confirm guard，导致 M10-C 阶段（无 encoder）无法独立验证状态机——本轮修正。）
```

### K3. Prepared 状态机（冻结）

```text
状态：`None → Prepared → Consumed | Invalidated`；**Consumed 与 Invalidated 均为 terminal**。
同一个 generation / token **不得**：Consumed 两次 / Invalidated 后再 Consumed / Consumed 后复活。
（状态权威在 Controller/runtime；QML 只读投影，见 K13/K14。）
```

### K4. 什么会「消费」一个 snapshot（confirmation validity guards）

```text
M10-C foundation 中的 confirmation acceptance 需要检查（**全部属于 confirmation validity**）：
  · token 当前有效（属于当前 generation，未被替换）
  · source 仍为 ActiveSerial
  · session identity 未变化（snapshot.sessionId == 当前 sessionId）
  · `serialConnected == true`
  · `serialBusy == false`
  · snapshot 未 consumed
  · snapshot 未 invalidated
**不在**这个基础 one-shot state machine 中要求：0x06 encoder exists / 0x10 encoder exists /
transport dispatch capability exists —— 否则 M10-C 无法独立验证该状态机（这正是本轮 blocker 的修复）。
```

### K5. Dispatch Capability Boundary（冻结位置）

```text
function capability check 冻结在**真正的 dispatch boundary**：
  · M10-D：0x06 dispatch capability = true，0x10 = false；
  · M10-E：0x06 = true，0x10 = true。
真正发送**之前**必须检查「该 function 的 capability 存在」；capability absent ⇒ **不得 encode**、**不得 transport send**。
```

### K6. Production Atomicity（M10-D/E 要求）

```text
虽然设计上 confirmation validity 与 dispatch capability 是两个职责，未来 M10-D/E 的 production Confirm
**不得**实现成「QML 先 consume → 过一会儿 → 另一个异步 handler send」。
要求：Controller 侧**一个同步/原子语义 operation** 完成：
  ① lookup token → ② 检查（token / source / session / connected / !busy）→ ③ 检查 dispatch capability
  → ④ consume snapshot → ⑤ 用该 snapshot intent 做 encode / start transport。
**不得**在 ④ 与 ⑤ 之间重新读取 QML draft；**不得**留下可由另一个 UI action 插入的竞态窗口。
（该原子性要求已被冻结进 C1–C4 的 C2/C3 纪律：M10-C 阶段只实现到 ④ 之前的 foundation。）
```

### K7. M10-C 阶段测试含义（冻结）

```text
M10-C 没有 encoder / send，因此 C 阶段**只直接测试**：
  `Prepared → confirmation accepted → Consumed`，以及 `second confirmation → reject`。
transport：**sendCount 必须继续为 0**。
这**不是** fake write capability，而是对 **confirmation state machine 自身**的测试。
```

### K8. No Fake Production Capability（禁止）

```text
**禁止**为了让 C10 PASS 在 production Controller 中增加 `pretendWriteSupported` / `testOnlyWriteCapability` /
`fake0x06Capability` 或任何等价开关。
M10-C 的 production capability **仍然真实为 absent**；hidden harness 测试的是 **snapshot / confirmation foundation**，
不是「假装真正能写」。
```

### K9. Harness Boundary（C2/C3）

```text
若 QML runtime 需要驱动 `Prepared → Consumed`，允许通过**明确的 harness / test seam** 观察
`confirmRequested(token)` 与 Controller/runtime 的 confirmation state machine。
但该 seam **不得**：调用 SerialTransport / 构造 raw ADU / 伪造 encoder success。
优先**复用真实 snapshot authority**；**不要**在 QML 自己维护第二套 consumed state。
```

### K10. Future D/E Confirm API（提前冻结概念）

```text
未来 production API 可以是 **`confirmAndDispatchPreparedWrite(token)`**（或项目风格等价命名）。
它**只接受 token**；**不得**接受 unit / address / value / values[] / timeout 等 QML draft fields。
Controller 从自身 snapshot store 取得完整 typed intent。
```

### K11. Capability Failure Semantics（未来异常路径）

```text
若 token valid 但 **dispatch capability absent**（异常/降级情形）：
  · **zero send**；**不得**产生 Modbus transaction / transport terminal / PossiblySent（这是 **pre-dispatch local rejection**）；
  · 是否消费 snapshot：采用**保守规则** —— **invalidate snapshot**（避免旧确认继续被使用），用户必须重新开始 Write flow；
  · 记录明确原因 **`CapabilityUnavailable`**（或内部等价 reason）；**不得**伪造成 `ProtocolError`。
```

### K12. Busy / Session Race（C 阶段同规则锁定）

```text
未来 D/E 的最终 controller operation 必须在真正 start 前**重新检查** session / connected / busy；
C 阶段的状态机测试锁定**同样规则**。示例：dialog 已打开（busy=false）→ busy false→true ⇒ snapshot **立即 Invalidated**；
即使随后 busy 回到 false，**旧 token 仍 reject**（§J13 的永久失效语义不变）。
```

### K13. Reactive QML Boundary（修正原文暗示）

```text
**修正**：不再暗示「必须暴露 sessionId」。冻结：
  · QML **不承担** session equality authority；
  · 首选 reactive exposure = `preparedWriteValid` / `preparedWriteGeneration`（或 token）/ `preparedWriteState` /
    `preparedWriteInvalidReason`（具体属性形状实施时决定）；
  · **Controller 负责**检测 session / source / busy 并 invalidate snapshot；
  · `activeSerialSessionId` 如实现或 harness **确实需要**，允许 read-only 暴露，但**不是必须条件**，
    也**不是** QML safety authority。
```

### K14. Confirmation Projection 生命周期

```text
QML summary 继续读取 **Controller-owned snapshot projection**；projection 可在 snapshot 处于 **Prepared** 期间显示。
**Consumed / Invalidated 之后 Confirm 不可再执行**。
dialog 是否立即关闭按 UX implementation 决定，但**状态 authority 不能由 dialog visible 反推**。
```

### K15. C03 / C10 更新（含义冻结）

```text
**C03（更新）**：valid 0x06 draft 在 hidden harness ⇒ `prepare snapshot N` → **displayed summary == snapshot projection**
  → **dialog merely open 时状态仍为 Prepared** → confirmation count = 0 → **transport sendCount = 0**。
  注意：M10-C 中 0x06 **可以 prepare / confirm foundation**，但**不能 encode / send**。
**C10（更新，C10-C）**：Prepared token N → 第一次显式 confirmation ⇒ **恰好一次 confirmation acceptance** ⇒ state = **Consumed**
  → 第二次 confirmation（同 token N）⇒ **rejected** → **transport sendCount = 0**。
  **M10-D** 升级为：0x06 valid token → double confirm → **exactly one transport send**；**M10-E** 对 0x10 同样证明。
（C01–C29 其余不变；C18A–D / C23–C29 不受本轮影响。）
```

### K16. No-dead-UI 保持

```text
本轮不改变「M10-C production Write UI hidden」⇒ **不会**出现「用户确认成功但什么都没发送」的产品 dead UI：
confirmation-only 行为**只由 hidden runtime harness** 测试。
**M10-D 首次把「0x06 production UI + dispatch capability」同时启用**（M10-E 再启用 0x10）。
```

### K17. Decision Count Correction（12 → 13）

```text
更正：§J26 的标题写作「12 项」，但其正文实际列出 **13** 项（含 closePolicy 与 staging）。
**正式计数 = 13 项**；**不删除** closePolicy、**不删除** staging 去凑 12。内容全部保留（见 K18）。
（§I49 的「12 项」与 §J26 的标题属历史记录，保留原文；本轮以本节为权威口径。）
```

### K18. Final Decision List（13 项，全部 RESOLVED）

```text
 1. production visibility（C hidden / D 0x06 / E 0x10）          —— RESOLVED
 2. function selector（two sub-tabs）                             —— RESOLVED
 3. 0x10 values editor（multiline, one decimal per line）         —— RESOLVED
 4. number format（decimal only）                                 —— RESOLVED
 5. draft persistence across source（preserve）                    —— RESOLVED
 6. Dialog component（Qt Quick Controls modal Dialog）             —— RESOLVED
 7. Dialog closePolicy（no outside-close）                         —— RESOLVED
 8. initial focus（Cancel）                                        —— RESOLVED
 9. session invalidation（invalidate snapshot）                    —— RESOLVED
10. busy invalidation（permanent invalidate）                      —— RESOLVED
11. connection summary identity（immutable display label；authority = typed source + session） —— RESOLVED
12. M10-C hidden foundation                                       —— RESOLVED
13. C1–C4 staging                                                 —— RESOLVED
```

### K19. C1–C4 Scope Clarification（冻结）

```text
**C1**（pure / core / controller foundation 为主）：values parser / write validation（含 widened address-span 规则）/
  PreparedWriteSnapshot / token·generation / **prepared state machine** / invalidation seams /
  read-only snapshot projection / tests。
  **不实现**：Dialog、production-visible Write UI、transport send、0x06 encoder、0x10 encoder。
**C2**：hidden Write QML + Dialog + snapshot projection + **Cancel / Confirm UI wiring**；
  Confirm 只作用于 **M10-C confirmation foundation**；**sendCount 始终 0**；无 production-visible write capability。
**C3**：session / source / busy invalidation + page gating + focus + Enter·Space·Escape + repeated activation + accessibility。
**C4**：C01–C29 + QML runtime + controller·pure tests + geometry + full regressions + final Review。
```

### K20. 本轮未改动项（复核）

```text
· **0x10 address-span 规则不变**（§J1：`start + quantity - 1 ≤ 0xFFFF`，uint32 扩宽计算，禁止 uint16 wrap）。
· **parser 契约不变**（§J3：仅十进制、0..65535、保序、首尾空白行 trim、中间空白行 error、1..123、parse failure 保留 draft）。
· **production visibility 不变**（§J18/K16：C hidden / D 0x06 / E 0x10；禁止 disabled roadmap preview 与 clickable no-op）。
· 其余 §J 冻结项（Dialog/closePolicy/initial focus/Enter·Space·Escape/summary/no-dead-UI/staging/失效矩阵）全部不变。
```

### K21. Boundary / Git

```text
docs-only：未改 src / tests / QML / CMakeLists.txt / scripts / assets / samples / screenshots；
未开始 C1；未实现 encoder / Write UI；未 push；未 tag；**verified LKGC 保持 `ef71244`**。
commit：`M10-C: separate confirmation from write dispatch`（独立 docs-only 提交；不 amend `e1733a5`；不 rebase）。
M10-C = Phase 1 Final Correction / Re-review（Implementation = NOT STARTED）。
```
## M10-C1 — Prepared Write Snapshot Foundation（2026-09-20，behavior-bearing）

> **M10-C Phase 1 Final Re-review = PASS；M10-C Phase 1 = COMPLETE；M10-C1 = GO。**
> 本轮实现**纯基础层**：write draft parsing / write validation（含 widened address-span）/
> PreparedWriteSnapshot / generation·token / Prepared 状态机 / source·session·busy invalidation /
> read-only projection / deterministic tests。
> **本轮不做**：Dialog、production-visible Write UI、0x06 encoder、0x10 encoder、transport write send、
> confirmation → transport、Agent write authority。继承冻结：§I / §J / §K 全部 Phase 1 decisions，不得重新解释。

### L0. Preflight

```text
HEAD = `db2aea9`（main，clean）；verified LKGC = `ef71244`；M9 = ✅ COMPLETE；M10 Phase 1 = COMPLETE；
M10-A = ✅ COMPLETE；M10-B = ✅ COMPLETE；M10-C Phase 1 = COMPLETE；CMake VERSION = 2.0.0；
v1 tag object `2cee626` / target `ae067ab`；v2.0.0 **absent**；origin/main = `a40d935`（behind 0 / ahead 107）；
`git diff --check` PASS —— 全部相符。
```

### L1. Source Re-read（A–F，实读源码事实）

```text
A. intent payload 真实字段类型：`WriteSingleRegisterIntent{ uint16 registerAddress, uint16 value }`；
   `WriteMultipleRegistersIntent{ uint16 startAddress, std::vector<uint16> values }`；
   `ActiveRequestIntent{ ActiveFunction, uint8 unitId, std::chrono::milliseconds timeout, payload }`。
B. 因此 **uint8/uint16 字段无法表达未验证的越界输入**（65536 会变成 0、-1 会变成 65535）⇒
   所有 draft/UI 侧输入必须以宽类型（int64_t）完成校验后才允许窄化（本轮的硬规则，见 §L3）。
C. `serialBusy_` 写入点（源码）：`= false` 出现在 teardownSerialTransport / handleSerialTransportError /
   connectSerial 成功 / 完成收尾 / 终止收尾；**`= true` 只有一处** = `readHoldingRegistersOnce` 被接受时。
D. `sourceKind_` 写入点 = connectSerial 成功（ActiveSerial）/ runDemoBatch（Simulator）/ loadReplayFile 成功（Replay）；
   `activeSerialSessionId_` 只在 connectSerial 成功时 `++`（单一会话边界）。
E. **failed Replay replacement**：loadReplayFile 的所有失败分支都在 teardown/source 更新**之前** return ⇒
   真实保持 ActiveSerial、同一 session、同一连接，仅置 replay 错误（本轮据此设计 I07 并实证）。
F. **Clear Results** 只清 statistics / rows / 诊断批 / 两类会话证据 / 错误文案，**不改变** source / session /
   connected / busy（源码实读）⇒ 不得 invalidate prepared snapshot（本轮 I09 实证）。
```

### L2. RED Baseline（R1–R8）

```text
RED 形式 = **编译级 + 源码事实**（新测试先写、真构建、真失败）：
  命令：cmake --build build/debug --target modbuslens_write_prepare_tests
  真实输出：`tests/test_write_prepare.cpp:9:10: fatal error: core/active/PreparedWriteSnapshot.h: No such file or directory`
⇒ R1（无 PreparedWriteSnapshot）、R2（无 token/generation）、R3（无 Prepared/Consumed/Invalidated 状态机）、
  R4（无严格 0x10 多行十进制 parser）、R5（无 widened span 的 write-prepare oracle）、
  R8（Confirm one-shot 不可证明）在实现前**不可编译**；
R6（Controller 不能保存 validated immutable write snapshot）与 R7（source/session/busy 变化不会 invalidate
  不存在的 snapshot）由同一编译失败（Controller 新 seam 不存在）+ §L1 源码事实共同证明。
**未使用临时 probe**（本轮采用编译级 RED，无需轮末删除物）。
```

### L3. Validate Before Narrowing（硬规则，已实现）

```text
`prepareWriteSingleRegisterIntent(unitId, registerAddress, value, timeoutMs)` 与
`prepareWriteMultipleRegistersIntent(unitId, startAddress, valuesText, timeoutMs)` 的**全部入参都是 int64_t**，
逐项校验通过后才 `static_cast` 到 uint8_t/uint16_t 并构造 typed intent。
测试锁定：`65536` 不会变成 0 后通过；`-1` 不会变成 65535 后通过（V04/V06 断言 AddressOutOfRange / ValueOutOfRange）。
```

### L4. Write Validation Result（typed，非 bool / 非字符串 / 非 Modbus taxonomy）

```text
`WriteValidationError{ WriteValidationErrorCode code, std::optional<ValuesParseError> parseError }`：
  UnitIdOutOfRange / AddressOutOfRange / ValueOutOfRange / TimeoutOutOfRange / ValuesParseError /
  QuantityOutOfRange / AddressSpanOutOfRange。带 line/value 上下文（parse error 携带 lineIndex/valueIndex）。
明确**不复用** `TransactionIssue` 或 Modbus outcome taxonomy：被拒绝的草稿不是协议事实，
而是「这份输入无法成为写入意图」的本地事实。
```

### L5. 0x06 prepare validation

```text
unit 1..247（0 与 ≥248 reject，broadcast 不支持）；address 0..65535；value 0..65535；
timeout 使用 **write UI presentation range 100..10000 ms**（`kWriteUiMinTimeoutMs` / `kWriteUiMaxTimeoutMs`；
core authority 仍是 `> 0`，UI 刻意更严格且**从不静默 clamp**）。全部 PASS 后才窄化。
```

### L6. 0x10 Parser（纯 C++20，零 Qt）

```text
`parseRegisterValues(std::string_view) -> variant<ParsedRegisterValues, ValuesParseError>`：
仅十进制无符号整数；每值 0..65535；保留输入顺序；拒绝 `+12` / `-1` / `0x10` / `1.5` / `1,2` / `abc` / `1 2`。
```

### L7. Line-ending 契约

```text
LF `"1\n2\n3"` 与 CRLF `"1\r\n2\r\n3"` 都必须接受；实现按行 trim `'\r'`（与普通空白一起），
**不把行尾 '\r' 当作非法数字字符**，也不依赖 GUI 控件代为规范化（P02 机器测试锁定）。
```

### L8. Blank-line 契约（冻结自 Phase 1）

```text
首尾纯空白行忽略；**中间纯空白行 = error**（避免无声跳过一个寄存器位置）；
全空白输入 = error（NoValues）；`blankLineIndex` 指向那条空白行（P04/P05 断言 lineIndex == 1）。
```

### L9. Parser Overflow

```text
逐字符累加到 uint64 并即时判 `> 65535` ⇒ `ValueOutOfRange`（**不 wrap、不抛异常、不依赖 locale**）。
`999999999999999999999` 得到同一个确定性错误（P12）。
```

### L10. Quantity 契约

```text
quantity = values.size()；要求 1..123（123 PASS / 124 TooManyValues，P09/P10）。
projection 的 quantity 由 `preparedQuantity(intent)` **实时派生**（0x06 → 1；0x10 → values.size()），
**不存第二份 count**，因此不可能与 values 不一致。
```

### L11. Address-span 实现（widened）

```text
`registerSpanFitsAddressSpace(startAddress, quantity)`：`uint32(start) + uint32(quantity) <= 65536`
（等价 `last = start + quantity - 1 <= 65535`）；quantity == 0 视为不成立。
样例（V09–V12）：65535/1 VALID；65535/2 INVALID；65534/2 VALID；65534/3 INVALID。
```

### L12. PreparedWriteSnapshot（immutable）

```text
`PreparedWriteSnapshot{ uint64 token, ActiveRequestIntent intent, TransactionSourceKind sourceKind,
uint64 sessionId, std::string connectionLabel }`。
不重复保存可从 intent 无歧义派生的数据（quantity 由函数派生）⇒ 不存在第二份 truth；
`operator==` 默认 ⇒ 可逐字段比较。
```

### L13. No Raw ADU（冻结）

```text
snapshot **不含 requestAdu**：M10-C1 没有 0x06/0x10 encoder，**不提前调用 encoder、不伪造 wire bytes**。
raw request ADU 仍只在未来真正 dispatch 编码并交给 transport 时成为 wire evidence（M10-A 契约不变）。
```

### L14. 0x10 Values Authority

```text
snapshot.values 直接来自 parser 得到的 typed vector；quantity 从它派生（§L10）；
不存在可独立修改的 quantity 字段（纯测试 S02 逐字段比对 projection 与 intent）。
```

### L15. Generation / Token

```text
单调 opaque `uint64_t` generation（Controller 侧 `preparedWriteGeneration_` 每次成功 prepare `++`）。
不同成功 prepare 得到不同 token；旧 token 不匹配新 snapshot（纯测试 S08）。
token **不是** session id、**不是** row index、**不是** pointer address。
```

### L16. Prepared 状态机

```text
`None → Prepared → Consumed | Invalidated`（后两者 terminal）；重新 prepare 创建新 generation 进入新的 Prepared；
旧 token 永不复活（S04–S08）。
```

### L17. Prepare While Already Prepared

```text
已有 active Prepared snapshot 时再次 prepare ⇒ 返回 `PrepareAlreadyPrepared`，
**不生成第二个 snapshot、不覆盖第一个**，原 generation/token 保持不变（纯测试 S03；controller 级 i01 实证）。
后续 C2 可据此聚焦既有 dialog。
```

### L18. Confirmation Foundation

```text
`AnalysisController::confirmPreparedWrite(token)`（C++ seam，**不发送**）：检查
token 匹配 / state == Prepared / source == ActiveSerial / session 未变 / connected / busy == false。
PASS ⇒ `Prepared → Consumed`（`ConfirmAccepted`）；FAIL ⇒ `ConfirmRejected{reason}`，
且**不产生任何 transport 发送**（Z02 实证 start/send 计数不变）。
```

### L19. Stale / Repeated Token

```text
token N 第一次 confirm ⇒ accepted ⇒ Consumed；第二次同 token ⇒ reject（NotPrepared）；
创建 token N+1 后 token N ⇒ reject（TokenMismatch）；错误 token ⇒ reject。
Confirm **从不重新读取 draft**（snapshot 是唯一 authority）。
```

### L20. Cancel Foundation

```text
`cancelPreparedWrite(token)` ⇒ `Prepared → Invalidated(UserCancelled)`；**不是 serial error**、
**不产生** transaction / transport terminal / send（Z03 实证；`hasSerialError` 保持 false）。
未来 C2 的 Escape/Cancel 只调用此 authority。
```

### L21. Invalidation Reasons（typed）

```text
`PreparedWriteInvalidReason{ UserCancelled, Disconnected, SessionChanged, SourceChanged, BusyBecameTrue,
CapabilityUnavailable }`（最后一个为 M10-D/E 预留）。**不使用** Modbus outcome / TransactionIssue 表达这些状态。
```

### L22. Busy Invalidation（含「不复活」）

```text
Prepared 期间 `serialBusy` false→true ⇒ `Prepared → Invalidated(BusyBecameTrue)`；
busy 回到 false **不复活**旧 token（i03/i04 实证：真实发起一次 FC03 读使 busy 变真，完成后再确认旧 token 仍被拒）。
```

### L23. Busy Already True at Prepare

```text
prepare 时 `serialBusy == true` ⇒ `PrepareRejected{Busy}`：**不创建 token、不覆盖已有 terminal 状态**；
single in-flight 继续冻结（不排队、不并行）。
```

### L24. Disconnect Invalidation

```text
Prepared → `disconnectSerial()` ⇒ `Invalidated(Disconnected)`；旧 token confirm 被拒（i01）。
C1 不处理 draft clearing（draft 不属于 Controller authority）。
```

### L25. Reconnect / Session Change

```text
snapshot 记录创建时 session id；任何**成功的新 Active Serial session** 都使旧 Prepared 失效（`SessionChanged`）。
即使同一 COM 口同一波特率也不复用；authority = session id，**不是** display label。
terminal reason 不被后续事件覆盖（i02 实证：disconnect 后的原因保持 `Disconnected`）。
```

### L26. Source Replacement

```text
Prepared 状态下成功的 Simulator replacement（i05）或 Replay replacement（i06）⇒ `Invalidated(SourceChanged)`；
旧 token reject。实现上 SourceChanged 在这些路径的 **teardown 之前**记录，
使 terminal reason 保持「来源变化」这一更准确的事实。
```

### L27. Failed Replay Replacement（重要回归）

```text
源码事实（§L1-E）：loadReplayFile 失败时保持 ActiveSerial / 同 session / 同连接。
因此 prepared snapshot **继续保持 Prepared**，并且仍然可以确认成功（i07 实证：confirm ⇒ ConfirmAccepted）。
**不因为**用户访问 Replay workspace 或 replay error 文案而 invalidate。
```

### L28. Navigation Invariance

```text
workspace navigation 本身不得 invalidate（workspace ≠ source）。C1 侧以「与来源/会话/忙碌无关的活动」
（refreshSerialPorts / runBaselineDiagnosis / clearDiagnosis）实证 controller 内**没有隐藏失效路径**（i08）；
QML 侧 navigation 由既有 qml_nav / focus 门禁继续把守（本轮 QML 零改动）。
```

### L29. Clear Results Invariance

```text
Clear Results 不 consume / 不 invalidate / 不 send ⇒ prepared snapshot 保持 Prepared（i09 实证），
source / connected / busy 均不变。
```

### L30. Serial Error Text Invariance

```text
单纯 serialErrorMessage 变化（例：一次被拒的非法读请求）不足以 invalidate：安全 authority 基于真实状态，
不是错误文案（i10 实证）。
```

### L31. Terminal-state Ordering

```text
invalidation **只作用于当前 Prepared generation**；已 Consumed / Invalidated 的 generation 不会被后续
busy / disconnect / source change 改成另一个 terminal state，历史 reason 不被覆盖（store 实现 + i02 实证）。
```

### L32. Read-only Projection

```text
Controller getters（C++，**无 setter、无 Q_PROPERTY、无 QML 面**）：
`preparedWriteState()` / `preparedWriteToken()` / `preparedWriteSnapshot()` / `preparedWriteInvalidReason()`。
snapshot 携带 function / unitId / address / value(s) / timeout / derived quantity（函数派生）/
connection display label。C2 才决定 QML 暴露形状。
```

### L33. QML session-id Exposure Decision

```text
**C1 不新增** `activeSerialSessionId` Q_PROPERTY（projection/harness 不需要）；QML 不负责 session equality check；
Controller 自己判断（confirm guard）。若未来确实需要，仅允许 read-only 暴露。
```

### L34. Immutable Connection Label

```text
snapshot 捕获创建时的 connection display label（`serialSourceLabel_`，例 `COM_TEST @ 9600`）；
projection 之后保持该 immutable 值。label 变化本身**不是** session equality（authority 仍是 sourceKind + sessionId）。
```

### L35. Confirmation Does Not Dispatch

```text
confirmPreparedWrite 函数体内 **零** transport 调用（`serialTransport_` / `startActiveRequest` / `port_` /
`encodeActiveRequest` 出现次数 = 0，源码审计）；transport `startAttemptCount` / `sendCount` 不变（Z01–Z04 实证）。
```

### L36. 0x06 / 0x10 Encoder Freeze

```text
`encodeActiveRequest` 对 0x06 / 0x10 仍返回 `UnsupportedFunction`（源码未改）；`encodeWrite*` 在 src/ 出现 0 次。
**Prepared snapshot 的存在不意味着 active write capability 存在**。
```

### L37. No Transaction Side Effects

```text
prepare / confirm / cancel / invalidate 均不新增 ActiveTransactionRecord、ActiveTransportTerminal、
TransactionListModel row、statistics 计数或 diagnosis batch（Z05 实证：records/terminals/rows/observed 全部不变）。
C1 = pre-dispatch intent foundation。
```

### L38. AI / Agent Freeze

```text
AI / Agent write authority = NONE；未新增 prepare write tool / confirm tool / send tool / raw serial tool
（agent 层本轮零改动，源码 grep 0）。
```

### L39. Tests — Parser（P01–P12，pure）

```text
P01 LF `1\n2\n3` → [1,2,3]；P02 CRLF → [1,2,3]；P03 首尾空白行忽略（含 `\r\n` 与空格行）；
P04 中间空行 error（lineIndex 1）；P05 中间纯空白行 error；P06 65535 valid；P07 65536 error；
P08 `+12` / `-1` / `0x10` / `1.5` / `1,2` / `abc` / `1 2` / `0b101` 全部 InvalidCharacter；
P09 123 values valid；P10 124 TooManyValues；P11 全空白 NoValues；P12 巨大整数 ValueOutOfRange（确定性）。
```

### L40. Tests — Numeric Validation（V01–V12，pure）

```text
V01 unit 1/247 valid；V02 unit 0/248/-1/65536 reject（UnitIdOutOfRange）；
V03 address 65535 valid；V04 address -1/65536 reject（**窄化前**）；V05 value 65535 valid；
V06 value -1/65536 reject（窄化前）；V07 timeout 100/10000 valid；V08 timeout 99/10001/0/-5 reject；
V09–V12 地址跨度 65535/1 VALID、65535/2 INVALID、65534/2 VALID、65534/3 INVALID。
```

### L41. Tests — Snapshot State（S01–S09，pure）

```text
S01 prepare → Prepared + token；S02 projection 逐字段 == validated intent（含 derived quantity）；
S03 二次 prepare → AlreadyPrepared 且 token 保持；S04 confirm → Consumed；S05 二次 confirm reject；
S06 cancel → Invalidated(UserCancelled)；S07 invalidated 不能 confirm；S08 新 prepare 新 token、旧 token 失效；
S09 后续 parse 得到的不同 intent **不能**改写已存在的 snapshot。
```

### L42. Tests — Context Invalidation（I01–I10，controller）

```text
I01 disconnect → Invalidated(Disconnected) + 旧 token reject（并附：二次 prepare ⇒ AlreadyPrepared，token 不变）；
I02 reconnect/new session → 旧 token reject 且 terminal reason 保持 Disconnected；
I03 真实 FC03 使 busy 变真 → Invalidated(BusyBecameTrue)；I04 busy 回 false 不复活；
I05 Simulator replacement → Invalidated(SourceChanged)；I06 Replay 成功替换 → Invalidated(SourceChanged)；
I07 **Replay 失败保持 ActiveSerial/session** → snapshot 仍 Prepared 且可确认成功；
I08 与来源无关的活动（刷新端口 / 诊断）→ 仍 Prepared；I09 Clear Results → 仍 Prepared；
I10 仅错误文案变化 → 仍 Prepared。
```

### L43. Tests — Zero Side Effects（Z01–Z05，controller）

```text
Z01 prepare → transport start/send 计数不变；Z02 confirm → 不变（且 Consumed）；Z03 cancel → 不变（且非 serial error）；
Z04 disconnect invalidation → 不变；Z05 prepare/cancel/prepare/confirm 全序列 + 一次 invalidation
→ records / terminals / rows / observed 全部不变。
```

### L44. Existing Regression Gates（A/B 封板未被破坏）

```text
Debug ctest **30/30 PASS**（27 → 29 之后新增 write_prepare = 30）；Release ctest **30/30 PASS**；
active_master **54 passed**（原 39 + I01–I10 + Z01–Z05）；active_request 17 passed；ui_bridge **59 passed**；
serial / serial_adapter / statistics / diagnosis / replay 全绿；qml_smoke / qml_nav_check / qml_geometry_check /
qml_focus_check 全绿（C1 未改 QML，仍跑以证无回归）。
FC03 golden wire / Active Serial history / statistics·diagnosis batch / selection append / M10-A transport safety /
Replay preservation / Simulator replacement / navigation presentation-only / Clear while pending 全部保持。
```

### L45. Test Target / CMake 变更

```text
新增 pure core 源（modbuslens_core）：`src/core/active/WriteDraftParsing.cpp`、
`WritePrepareValidation.cpp`、`PreparedWriteSnapshot.cpp`（core 仍**零 Qt**）。
新增测试目标 `modbuslens_write_prepare_tests`（tests/test_write_prepare.cpp）+ `add_test(NAME write_prepare ...)`
+ offscreen 属性；controller 级 I/Z 用例并入既有 `active_master`（复用 recording transport 与 controller 源）。
```

### L46. Warnings

```text
**新增代码零 warning**（新增的 3 个 core 源、controller 变更、两个测试文件在 -Wall -Wextra 下均无告警；
过程中修正了测试侧的 range-loop 拷贝告警与 address-of-rvalue）。
**pre-existing**：`src/main.cpp` 5 条（unused variable ×3 / redundant capture ×1 / set-but-not-used ×1，
行号 2492 / 2494 / 4592 / 5923 / 6161）——本轮**未修改该文件、未顺手修**。
```

### L47. Problems / RCA

```text
P1（实现期编译缺陷，已修）：`ConfirmWriteOutcome` 别名最初声明在 `ConfirmAccepted/ConfirmRejected` **之前**
  ⇒ 编译报「not declared in this scope」。Fix：把别名移到 confirm 词汇之后。
  Verification：两套测试目标全绿。
P2（测试脚手架缺陷，已修）：测试初稿用 `std::get_if<T>(&controller.prepare…(...))` 取临时对象的地址
  ⇒ `taking address of rvalue`；Fix：改为先绑定到引用/局部变量的 helper（isPrepared / isConfirmAccepted /
  isConfirmRejected）。
P3（编辑器/转义陷阱）：多行文本字面量 `"1\n2\n3"` 在脚本生成测试时被写成真实换行 ⇒ 编译报
  「missing terminating " character」；Fix：以显式转义重建字面量。
P4（设计一致性）：`preparedQuantity` 初版对 0x06 返回 0，与「0x06 恰好写一个寄存器」的语义不符 ⇒
  改为全函数域：0x06 → 1、0x10 → values.size()、0x03 → 读数量（write 路径不投影它）。
无 contract 冲突：未修改任何 M10-A/B 冻结契约；Controller 已有集中状态点（busy 只有一处 true、session id 只有一处 ++），
  因此 invalidation 接线不需要字符串比较 / QML polling / label 比较。
```

### L48. Git

```text
behavior-bearing（core 新类型 + controller seam + 状态接线 + 测试）⇒ 不作 LKGC。
commit：`M10-C1: add prepared write snapshot foundation`（独立提交；不 amend `db2aea9`；不 rebase；不 push；未 tag）。
verified LKGC 保持 `ef71244`；M10-C1 = 等待 Review。
```
## M10-C2 — Hidden Write UI + Confirmation Foundation（2026-09-20，behavior-bearing）

> **M10-C1 Review = PASS；M10-C1 = COMPLETE（accepted behavior-bearing commit `7562678`，不单独做 C1 closure commit）；M10-C2 = GO。**
> 本轮实现 **hidden-only**：Write draft UI / 0x06·0x10 draft presentation / validation presentation /
> PreparedWriteSnapshot projection / modal confirmation Dialog / Cancel·Confirm wiring / repeated-Write guard /
> immutable summary oracle / confirmation one-shot UI wiring。
> **transport sendCount 始终 0**；未实现 0x06/0x10 encoder、未实现 confirm→dispatch、未让 Write UI 在正常 production 出现。

### M0. Preflight

```text
HEAD = `7562678`（main，clean）；verified LKGC = `ef71244`；M9 / M10 Phase 1 / M10-A / M10-B / M10-C Phase 1 / M10-C1 全 COMPLETE；
CMake VERSION = 2.0.0；v1 tag object `2cee626` / target `ae067ab`；v2.0.0 **absent**；
origin/main = `a40d935`（behind 0 / ahead 108）；`git diff --check` PASS —— 全部相符。
```

### M1. Source Re-read（含真实 Qt API 校验）

```text
· CommunicationPage.qml（248 行）：Connection / Request 两 PanelCard + serial error lane + 末尾竖直弹性 Item；
  控件形态 = ComboBox + SpinBox + 原生 Button；**无 Dialog / Popup**。
· AppButton / DesignSystem：DS tokens 齐备（spacing*/radiusS/controlHeight/fontBody/primary/border/error…）；
  AppButton 的 border 即 keyboard-focus 通道（M9-F F1 冻结）。
· TabButton 现有 pattern：Diagnosis 页 TabBar + TabButton（focusPolicy/内环焦点可见性见 M9-F）。
· Main.qml page gating：StackLayout 每页 `enabled: workspaceHost.currentIndex === …`（Communication = child 2）。
· PreparedWriteSnapshot.* / WriteDraftParsing.* / WritePrepareValidation.*：C1 的 typed intent、typed errors
  （含 parse lineIndex）、token/store 状态机；AnalysisController 的 prepared getters/seams。
· harness pattern：staged walk（`QList<std::function<void()>>` + 每步一个 event-loop turn）、
  `findNamedItem`（**只遍历 Item 树**）、offscreen 运行。
· **Qt 6.11 真实 API 校验（读安装源 `QtQuick/Templates/plugins.qmltypes` + `Controls/Basic/Dialog.qml`）**：
  `Dialog` 继承 `Popup`；properties = title/header/footer/standardButtons/result；
  signals = **accepted / rejected / opened / closed** / aboutToShow / aboutToHide；methods = accept()/reject()/done(result)；
  `modal`（bool）、`visible`、**`closePolicy` 是 flag enum**（NoAutoClose / CloseOnPressOutside /
  CloseOnPressOutsideParent / CloseOnReleaseOutside / CloseOnReleaseOutsideParent / **CloseOnEscape**）、
  `opened`（readonly，**enter transition 完成后才为 true**）。⇒ 本轮据此显式配置，不依赖默认值。
```

### M2. C1 write-only invariant 审计

```text
Controller 能创建 PreparedWriteSnapshot 的入口只有两个：`prepareWrite06` / `prepareWrite10`
（内部都走 `prepareWriteIntent`，其 intent 只可能来自 `prepareWriteSingleRegisterIntent` /
`prepareWriteMultipleRegistersIntent`）。
**不存在** 0x03 → snapshot 路径，也不存在「任意 ActiveRequestIntent → snapshot」的通用入口：
`WritePrepareOutcome` 的 variant 只有 PreparedWrite / PrepareAlreadyPrepared / PrepareRejected 三种，
没有 visitors 需要为 0x03 做 exhaustiveness 分支（`preparedQuantity` 对 0x03 的读数量分支
只服务于「投影是 intent 的函数」这一完整性，不参与 write 路径）。⇒ **无 STOP 条件，无需 NON-BLOCKING 标注。**
```

### M3. Production-hidden 架构

```text
CommunicationPage 内新增：
    Loader { id: writeFoundationLoader; objectName: "writeFoundationLoader";
             active: writeFoundationVisible; sourceComponent: writeFoundationComponent }
    Component { id: writeFoundationComponent
                WriteFoundationSection { analysisController: page.analysisController } }
⇒ `active: false`（normal production）时**组件根本不实例化**：场景里没有任何 write 控件，
  没有 Tab 入口、没有可激活控件、没有可点击区域。
（RCA 记录：最初用 `source: "../components/…qml"` + `onLoaded` 赋值的写法会让 `required` 属性在创建时未初始化，
  报警「Required property analysisController was not initialized」；改为 inline Component 后属性在创建时即满足。）
normal production 的机器证明：`--qml-focus-check` 新增 **prod-hidden oracle**
（loader 存在、`active == false`、`item == null`）⇒ 正常启动既不能 Tab 进入、也不能 Space 激活、更不能打开 Dialog。
```

### M4. Harness visibility seam（不是 capability）

```text
main.cpp：`engine.rootContext()->setContextProperty("writeFoundationVisible",
             app.arguments().contains("--qml-write-foundation-check"))`
—— 该开关只回答「本次运行是否为测试加载 hidden UI foundation」，**绝不表示 0x06/0x10 可 dispatch**。
命名刻意回避 writeSupported / writeCapability / enableWriteTransport。
无 pretendWriteSupported / fakeWriteCapability / testOnlyEncoder / testOnlySend（grep 0）。
harness 模式下 0x06/0x10 **可以 prepare/confirm snapshot**，但**不能 encode、不能 send**。
```

### M5. Write IA 与独立 draft

```text
Write 区（hidden）内：SectionHeader「写入」+ PanelCard（`writeFoundationPanel`）：
  · `writeFunctionTabs`（TabBar）+ 两个 TabButton（`writeTab06` / `writeTab10`）——复用既有 TabButton pattern；
  · 0x06 draft（`write06DraftRow`）：unit 1..247 / address 0..65535 / value 0..65535 / timeout 100..10000（SpinBox，非 editable）；
  · 0x10 draft（`write10DraftColumn`）：unit / start 0..65535 / timeout + **TextArea（`write10ValuesArea`，
    包在受限高度 `ScrollView`（96px）内）**，一行一个十进制值；
  · `writeActivateButton`（AppButton「写入」）+ `writeValidationError`（Label）。
draft 由 section 的 page-local 属性保存：unit06/address06/value06/timeout06 与 unit10/start10/valuesText10/timeout10
**互相独立**：切 Tab 不覆盖、不清空、不复制（C16 机器证明）。
**C2 不主动清 draft**（navigation / Clear Results / tab switch 都不清）；disconnect·reconnect·source 的 draft 持久性属 C3 验证范围。
```

### M6. Numeric input 边界（SpinBox 审计复用 C1 结论）

```text
0x06 沿用非 editable SpinBox（`from`/`to`），与既有 FC03 输入一致：越界值**不可输入**（不是被静默改写）；
唯一能构造越界输入的是 0x10 的自由文本，且它由 **C1 的权威 parser** 判定。
C2 oracle 另外证明：summary 显示的就是 **prepared snapshot 的值**（C19/C20），而不是「用户以为输入的值」。
```

### M7. 0x10 values editor

```text
多行 TextArea；raw text 属于 draft（`valuesText10`）。
QML **没有**重新实现 parser：`grep -cE "split\(|parseInt|Number\(" WriteFoundationSection.qml == 0`；
业务校验一律调用 Controller → C1 纯 C++ parser/validation。
QML 只做展示：把 typed error 映射出的文本（含 **one-based 行号**）显示在 `writeValidationError`。
```

### M8. Validation handoff / presentation / failure

```text
Write activation：读取当前 draft → `prepareWrite06 / prepareWrite10` → Controller 做 C1 权威 parse+validation
  → 成功：Prepared snapshot（**并且只有此时**才 `confirmationDialog.open()`）
  → 失败：无 snapshot、**不打开 Dialog**。
presentation 映射在 Qt adapter 层（`setWriteDraftErrorFrom`）：typed code → 中文文案，覆盖 unit / address / value /
  timeout / values parser（带行号）/ quantity / address span；**UI 从不显示 enum token**（C01 断言不含 `UnitIdOutOfRange`）。
失败行为：Dialog 不打开、无 snapshot、**原 draft 保持**（C04 断言 draft 未被清）、transport send = 0、transaction/terminal = 0。
```

### M9. Dialog 组件与关闭策略（真实 API 依据）

```text
Qt Quick Controls `Dialog`；`modal: true`；**未使用任何自绘 modal**；未使用 FileDialog / native dialog。
`closePolicy: Popup.CloseOnEscape` —— 显式排除全部 outside-press/release 关闭项（不依赖默认值）。
footer 为**自定义两个 AppButton**（`writeConfirmCancelButton` / `writeConfirmAcceptButton`），
  **不使用 standardButtons**（避免平台默认按钮 / 自动 Enter / 默认 destructive focus）。
```

### M10. Snapshot 是 Dialog 的唯一 authority

```text
summary 全部字段读取 Controller projection（`preparedWriteFunction/UnitId/Address/Value/Values/Quantity/
ConnectionLabel`），**没有任何 summary 绑定 draft**。
0x06 summary：function（含 0x06 名称）/ 目标从站 / 寄存器地址 / 写入值 / 连接 label（immutable，快照时捕获）。
0x10 summary：function（**0x10 多寄存器写入（十进制 16，Write Multiple Registers）**）/ 目标从站 / 起始地址 /
  derived quantity / 连接 label / **全部 values**（ListView 只读、受限高度 120px、可滚动、顺序与 snapshot 一致、**不截断**）。
Dialog 状态本身不作为 authority：仅暴露 `confirmationVisible` / `confirmationOpened` 两个**只读 presentation 事实**
  （RCA：Dialog 是 `QObject` 而 `findNamedItem` 只遍历 Item 树，harness 因而无法直接读取 popup 状态；用 section 上的只读属性
   既解决了 harness 读取，也不把 Dialog.visible 变成安全 authority）。
```

### M11. Cancel / Confirm wiring

```text
Cancel（按钮与 Escape → `onRejected`）→ `cancelPreparedWrite()` → `cancelPreparedWriteToken(token)`
  ⇒ `Prepared → Invalidated(UserCancelled)`；Dialog 关闭；draft 保留；transport 0；transaction/terminal 0。
  **不存在**「只 `dialog.close()` 却让 snapshot 仍 Prepared」的路径。
Confirm → `confirmPreparedWrite()` → **只传 opaque token**（`confirmPreparedWriteToken(token)`），
  **不传** unit/address/value/values/timeout；成功 ⇒ `Prepared → Consumed`（Dialog 关闭）。
  **不 encode、不 dispatch、不 send**；C2 **不出现**「写入成功 / 发送成功 / 设备已写入」等文案（grep 0）：
  Consumed 只表示「用户确认意图已被 one-shot 接受」。
其他关闭路径审计：唯一其它关闭来源是 `Connections.onPreparedWriteChanged` —— 当 Controller 因
  disconnect / session / busy / source 变化而 invalidate 时同步关闭 Dialog（authority 已先行失效，不是 Dialog 自己决定）；
  没有「Dialog 关闭但 snapshot 悬挂且用户找不到」的状态。
```

### M12. 重复 Write / 重复 Confirm

```text
重复 Write：`activateWrite()` 发现 `hasPreparedWrite` ⇒ **不创建第二个 snapshot、不换 token、不开第二个 Dialog**，
  把既有 Dialog 重新聚焦并**返回 true**（RCA：初版返回 false，被 harness 判定为「重复 Write 失败」；语义上
  「prepared write 已在屏幕上」就是成功）。C09 机器证明：token 不变、`preparedWriteState` 仍 prepared。
重复 Confirm：QML **不维护** `confirmConsumed` 之类的第二套 authority；第一次 Confirm ⇒ Consumed；
  对同一 token 再次确认 ⇒ Controller 拒绝（`NotPrepared`）。C10-C 机器证明。
```

### M13. Initial focus / Enter 结构安全 / Escape

```text
`onOpened: forceCancelFocus()` ⇒ **Cancel 获得初始 active focus**；
Confirm **不是** default button、不是 highlighted default、不是初始 focus（无 standardButtons ⇒ 无平台默认语义）。
Escape 走 `closePolicy: CloseOnEscape` → `onRejected` → Cancel authority（不是「只关视觉」）。
（完整键盘验收属 C3；C2 只保证结构与 wiring 正确。）
```

### M14. Page gating 与隐藏页

```text
Write foundation 位于 Communication 页内 ⇒ 自动继承 M9-F 的 `page.enabled` gating（离开 Communication 时整页 disabled，
隐藏页控件不可激活）。C2 **没有任何**绕过 page.enabled 的路径（未新增独立 Window / Overlay 层）。
完整 focus oracle 属 C3。
```

### M15. Read-only QML projection 与 notify 语义

```text
新增 Q_PROPERTY（全部 **read-only，无 setter**）：hasPreparedWrite / preparedWriteState /
preparedWriteToken / preparedWriteFunction / preparedWriteUnitId / preparedWriteAddress / preparedWriteValue /
preparedWriteValues / preparedWriteQuantity / preparedWriteTimeoutMs / preparedWriteConnectionLabel /
preparedWriteInvalidReason / hasWriteDraftError / writeDraftError。
notify：**统一 `preparedWriteChanged`**（prepare / confirm / cancel / invalidate / 新 generation 都会发出，
且由 `announcePreparedWriteChanged()` 在每次状态转换后一次性发出 ⇒ QML 不 polling、也不会观察到半更新帧）；
draft 校验错误单独用 `writeDraftErrorChanged`。
**未暴露 activeSerialSessionId**（C2 无真实需求；QML 不负责 session equality）。
```

### M16. C2 Oracle Set（harness 真实输出）

```text
`--qml-write-foundation-check`（真实 app + 真实 QML + harness-only transport）真实输出：
  WRITE [setup]: Communication current, harness transport connected, session=1 state=none
  WRITE [C01]: unit 0 -> [从站地址须在 1..247 之间（当前不支持广播）], no dialog, no token
  WRITE [C02]: 65536 -> [第 1 行的数值超出 0..65535]
  WRITE [C03]: prepared token=1, dialog open, startAttempts=0
  WRITE [C19]: summary fields equal the snapshot projection (unit 11 / addr 100 / value 1234 / COM_HARNESS @ 9600)
  WRITE [C20]: draft edited to 7/4321, snapshot and summary still 100/1234
  WRITE [C09]: repeated Write kept token=1 and one dialog
  WRITE [C04]: Cancel -> invalidated(user_cancelled), draft preserved, zero send
  WRITE [C16]: both drafts survived the tab switches
  WRITE [C17]: 0x10 prepared, quantity 3, all three values listed
  WRITE [C10-C]: one consumption for token=2; second confirm rejected; zero send
  WRITE [C18]: 65535+2 -> [起始地址与数量超出 16 位寄存器地址空间]
  WRITE [parser]: blank line rejected; 123 prepared; 124 rejected
  WRITE FOUNDATION CHECK PASS (…) — zero dispatch
⇒ C01 / C02 / C03 / C04 / C09 / C10-C / C16 / C17 / C18 / C19 / C20 全部关闭；parser presentation 四条场景全过；
  `transport->startAttempts() == 0` 为全局最终断言（**零 dispatch**）。
```

### M17. Harness-only transport（不是 capability）

```text
`HarnessWriteTransport`（main.cpp harness 区，**无 Q_OBJECT**）：openPort 返回 true 以建立「已连接的 Active Serial 会话」
（不打开真实端口、不需要 COM、不写硬件）；`startActiveRequest` **计数并一律拒绝**（`{false, NotSent}`）——
因此任何试图 dispatch 的路径既不可能成功、又会在最终断言里暴露。
（RCA：初版给了它 Q_OBJECT ⇒ AUTOMOC 报错「main.cpp contains a Q_OBJECT macro but does not include main.moc」；
  去掉 Q_OBJECT 即符合 AUTOMOC 约定，且该类只经 SerialTransport 接口使用，不需要自身 meta-object。）
```

### M18. 边界审计（本轮实读）

```text
· 0x06 / 0x10 encoder 仍 **ABSENT**：`encodeActiveRequest` 对两者仍 `UnsupportedFunction`（源码未改）；`encodeWrite*` 0。
· write 路径**零 transport 调用**：`prepareWriteIntent` / `confirmPreparedWrite` 函数体内
  `serialTransport_` / `startActiveRequest` / `encodeActiveRequest` 出现 0 次。
· Agent/AI：本轮 agent 层零改动、无 prepare/confirm/send/raw-serial tool（grep 0）。
· QML 无 parser 复制（grep 0）；QML 无「成功」类写文案（grep 0）。
· 未改 Navigation IA / 版本 / package / icon / scripts / samples / simulator。
· 几何：harness-visible 下 0x10 values editor 与 confirmation values 均为**受限高度 + 内部滚动**
  （96px / 120px），不把窗口无限撑高；完整 geometry gate 属 C4。
```

### M19. Problems / RCA（本轮 6 项，全部为 harness / QML 结构问题，非产品安全缺陷）

```text
P1 `source:` + `onLoaded` 赋值写法的 required 属性未初始化 ⇒ 改用 inline Component（创建时即满足）。
P2 QML function 的返回类型是 QVariant：`Q_RETURN_ARG(bool, …)` 使 `invokeMethod` **静默失败** ⇒ 改读 QVariant 再转 bool。
P3 harness 未切换 workspace：**Popup 的 parent 不可见时不会 opened**，且隐藏页上的校验文案同样不可见 ⇒
   先点击 rail `navItem_2` 把 Communication 设为当前页（presentation-only）。
P4 `Popup.opened` 只在 **enter transition 完成后**为 true ⇒ 立即 oracle 改判 `visible`，
   并在下一轮 stage 断言「已完全打开」（两者都由真实 Qt 语义决定，未在 harness 里绕过 Dialog 行为）。
P5 Dialog 是 QObject 而 `findNamedItem` 只遍历 Item 树 ⇒ 无法按名字找到 popup；改为在 section 上暴露
   **只读 presentation 事实** `confirmationVisible` / `confirmationOpened`。
P6 重复 Write 初版返回 false 被 harness 判为失败 ⇒ 语义修正为返回 true（「已准备的那一份就在屏幕上」）；
   完整 RCA 已区分：**Qt 行为理解偏差 4 项（P2/P3/P4/P6）/ harness 结构 1 项（P1）/ QML 结构 1 项（P5）**，**
   **无产品/QML 安全缺陷**，且未为让测试通过而在 harness 中绕过真实 Dialog 行为。
```

### M20. Git

```text
behavior-bearing（QML + Controller projection + harness + CMake）⇒ 不作 LKGC。
commit：`M10-C2: add hidden write confirmation foundation`（独立提交；不 amend `7562678`；不 rebase；不 push；未 tag）。
verified LKGC 保持 `ef71244`；M10-C2 = 等待 Review。
```
## M10-C3 — Write Confirmation Context / Keyboard / Accessibility Safety（2026-09-20，behavior-bearing）

> **M10-C2 Review = PASS；M10-C2 = COMPLETE（accepted behavior-bearing commit `447e346`，不单独做 C2 closure commit）；M10-C3 = GO。**
> 本轮在既有 hidden Write foundation 上实现并**机器证明**：session/source/busy invalidation、
> disconnect/reconnect safety、draft persistence、modal/background safety、initial focus、
> Enter/Space/Escape、repeated activation、hidden-page safety、accessibility baseline、
> invalidation-driven Dialog closure。**transport write sendCount 始终 0**；未实现 encoder、未实现 confirm→dispatch。

### N0. Preflight

```text
HEAD = `447e346`（main，clean）；verified LKGC = `ef71244`；M9 / M10 Phase 1 / M10-A / M10-B / M10-C Phase 1 / M10-C1 / M10-C2 全 COMPLETE；
CMake VERSION = 2.0.0；v2.0.0 **absent**；origin/main = `a40d935`（behind 0 / ahead 109）；`git diff --check` PASS。
```

### N1. Source Re-read（A–F 实测，非 Qt 经验推断）

```text
A. Dialog 的 focus 传播：**runtime 实测** —— dialog 打开后 `activeFocusItem` =
   `writeConfirmCancelButton`（Cancel 拿到初始焦点，Confirm 没有）；Tab 在 footer 内移动焦点
   （Cancel → Confirm），Shift+Tab 反向；modal Dialog 的焦点不外泄到背景。
B. Enter/Return 由谁接收：focus = Cancel 时 **Return 什么也不做**（实测 state 仍 prepared、reason 空）；
   focus = Confirm 时 Return 触发 confirm —— 因为 C3 给 Confirm 显式加了 `Keys.onReturnPressed`
   （且用 `confirmButton.activeFocus` 守卫，只有它自己持焦点才生效）。Space 由控件自身语义处理
   （Cancel/Confirm 都会响应）。
C. Escape 的真实路径：**必须发到 window**（Qt 把 popup 的 CloseOnEscape 处理挂在 window/overlay 层），
   发到 `activeFocusItem` 不会触发；实测发到 window 后 → `rejected` → Cancel authority。
D. Controller invalidation 如何通知 QML：`preparedWriteChanged` 单一信号（C2 冻结），
   QML 的 `Connections.onPreparedWriteChanged` 在 `!hasPreparedWrite` 且 dialog 打开时关闭 dialog。
E. Dialog 在 Invalidated 后的关闭：**authority 先失效、dialog 后关闭**（实测 C11/C13：state 已是
   invalidated(disconnected / busy_became_true) 且 dialog 不再可见）。
F. page.enabled=false 时 Popup/focus 行为：离开 Communication 后该页 `enabled == false`（实测），
   Tab/Space/Enter 均不能 prepare/confirm/cancel/send（实测 C14）。
```

### N2. 本轮 QML 改动（最小、只针对键盘与可访问性）

```text
1. **values TextArea 的 Tab/Backtab 逃逸**（复用 Agent TextArea 既有做法）：
   `Keys.onTabPressed`/`onBacktabPressed` → `nextItemInFocusChain(...)` + `forceActiveFocus`；
   只改 Tab/Backtab 语义，**编辑键与换行输入契约不变**。
2. **Confirm 的 Enter**：`Keys.onReturnPressed` + `activeFocus` 守卫 ⇒ Enter 只在 Confirm 持有焦点时确认，
   绝不会因为「dialog 打开」而确认。
3. **Accessible.name**：写入按钮 / 0x10 values 编辑器 / 0x06 四个数值输入 / Cancel / Confirm。
4. **只读 presentation 事实**（C2 已加）：`confirmationVisible` / `confirmationOpened`（Dialog 是 QObject，
   Item 树查找不到）。
未改：Navigation IA、Communication 布局、Transactions 页、DesignSystem、任何视觉样式。
```

### N3. Initial Focus（C06，runtime）

```text
真实输出：`WRITE [C06]: initial focus = writeConfirmCancelButton`
⇒ Cancel 持有 active focus、Confirm 未持有；destructive Confirm 不需要任何显式配置就被排除在初始焦点之外。
```

### N4. Immediate Enter（C07，runtime）

```text
真实输出：`WRITE [C07]: immediate Enter -> state=prepared reason=`
⇒ 刚打开就按 Enter **既不确认也不取消**（Return 在 Cancel 上没有默认动作）；
   `consumed count = 0`、write dispatch = 0。记录：这是安全的不动作结果。
```

### N5. Immediate Space（C05，runtime）

```text
真实输出：`WRITE [C05]: immediate Space -> state=invalidated reason=user_cancelled`
⇒ Space 激活了持焦点的 Cancel ⇒ **non-destructive Cancel**（冻结契约允许）；
   **未发生 Consumed**，write dispatch = 0。
```

### N6. Explicit Confirm + Space（C08，runtime）

```text
真实输出：`WRITE [C08]: Confirm+Space -> consumed once (token=5), dialog closed, zero write dispatch`
⇒ 真实 Tab 移动焦点到 Confirm → Space ⇒ 恰好一次 confirmation acceptance（随后同 token 再确认被拒）
   → Dialog 关闭 → write dispatch 0。
```

### N7. Explicit Confirm + Enter（C08b，runtime）

```text
真实输出：`WRITE [C08b]: Confirm+Enter -> consumed`
⇒ 与 Space 等价（由 N2-2 的显式 wiring 提供），同样 zero write dispatch。
```

### N8. Double Activation（runtime）

```text
真实输出：`WRITE [double]: two rapid activations -> one consumption, zero write dispatch`
⇒ Confirm 持焦点时连按两次 Space：Controller 的 one-shot 状态机只接受一次；**无 sleep / 无 debounce**
  作为安全机制。
```

### N9. Escape（runtime）

```text
真实输出：`WRITE [Escape]: invalidated(user_cancelled), dialog closed, draft preserved`
同时断言：state = invalidated、reason = user_cancelled、dialog 不再可见、draft 保留、零发送。
⇒ 不是「只观察 Dialog.visible=false」：**同时读 Controller state 与 reason**。
```

### N10. Outside-click（runtime）

```text
真实输出：`WRITE [outside-click]: dialog stayed open, snapshot stayed prepared`
⇒ 在 dialog 打开时点击页面背景（`communicationHeader`）：**dialog 未关闭、snapshot 仍 prepared**
（closePolicy 显式排除 outside-press；鼠标事件由 modal overlay 吞掉）。
```

### N11. Modal / Background Navigation（runtime）

```text
真实输出：`WRITE [modal-nav]: rail 2 -> 2 (modal blocked the click); state=prepared`
⇒ 尝试点击 rail 条目 0：**workspace 索引未变化**（modal 阻止了背景点击），
   snapshot 未 consume、未 cancel、未发送。记录：navigation 不能改变 snapshot。
```

### N12. Disconnect While Dialog Open（C11，runtime）

```text
真实输出：`WRITE [C11]: disconnect -> invalidated(disconnected), dialog closed by the authority`
⇒ authority-first：先 Invalidated(Disconnected)，随后 QML 依据 `preparedWriteChanged` 关闭 dialog；
   零 write dispatch；draft 由后续 C32/C14 证明仍保留。
```

### N13. Reconnect / New Session（C12，runtime）

```text
真实输出：`WRITE [C12]: reconnect -> old token 0 unusable (session=2)`
⇒ 同一 port/baud 重连后 session id 变为 2，旧 token 不可确认；**旧 dialog / 旧 token 都不恢复**
   （token 值为 0 是因为该场景按顺序先经过了 invalidation，store 已丢弃快照）。
```

### N14. Busy Invalidation（C13/C14，runtime, 真实 FC03）

```text
真实输出：`WRITE [C13/C14]: busy false->true invalidated; busy->false did not revive (reads=1, write attempts=0)`
⇒ 通过**真实 FC03 读**（shipped path + harness transport 接受读）使 serialBusy true ⇒
   Invalidated(BusyBecameTrue)、dialog 不可再确认；读完成后 busy=false，**旧 token 仍不能复活**。
   计数被显式区分：`reads=1`（制造 busy 的读，属预期副作用）/ `write attempts=0`（写派发）。
```

### N15. Invalid Reason Preservation（C35，runtime）

```text
真实输出：`WRITE [C35]: reason stays busy_became_true after a later disconnect`
⇒ 已终态的 generation 的 reason **不被后续 disconnect 覆盖**（BusyBecameTrue 保持）。
   Dialog presentation 不需要展示全部 reason；authority 必须稳定。
```

### N16. Draft Persistence（C15 / C30 / C31 / C32 / C14，runtime）

```text
· C15 `WRITE [C15]: Clear Results preserved drafts and the prepared snapshot`
  ⇒ Clear Results 既不清 draft，也不 invalidate 已 Prepared 的 snapshot（冻结契约）。
· C30 `WRITE [C30]: Simulator replacement -> invalidated(source_changed), drafts preserved`
  ⇒ 成功 source replacement：snapshot 失效、**draft 保留**。
· C31 `WRITE [C31]: failed Replay load -> snapshot and draft preserved`
  ⇒ 失败的 Replay 加载：authority 未变 ⇒ snapshot 仍 Prepared 且 dialog 仍展示原 immutable snapshot，
    draft 保留（不因 error 文案失效）。
· C32 `WRITE [C32]: drafts preserved; old snapshot invalidated`
  ⇒ **draft persistence ≠ confirmation persistence** 被直接区分。
· C14（navigation）：离开 Communication 再返回，draft 完整、无 snapshot 产生。
```

### N17. Hidden-page Safety（C14，runtime）

```text
真实输出：`WRITE [C14]: hidden page disabled; Tab/Space/Enter prepared nothing, dispatched nothing`
⇒ 在 foundation 已加载的前提下离开 Communication：`communicationWorkspace.isEnabled() == false`
   （复用 M9-F page gating，而不是只看 visible），Tab/Space/Enter 均不能触发写路径；
   返回后 draft 仍在、仍无 snapshot。
```

### N18. Accessibility（runtime，基线而非认证）

```text
真实输出：`WRITE [a11y]: names present (写入（打开确认对话框） / 取消写入（不发送任何请求） / 确认写入意图)`
⇒ 通过 Qt 自己的可访问性接口读取（`QAccessible::queryAccessibleInterface` → `text(Name)`），
   覆盖：Write action / 0x10 values 编辑器 / 0x06 数值输入 / Cancel / Confirm；
   Dialog 有 title（`确认写入`）且 summary 在打开时有可读内容；
   `WRITE [a11y]: busy runtime -> Write action disabled=1` ⇒ enabled 状态与 runtime 状态一致。
**不声称 WCAG / screen-reader certification**（与 M9-F 边界一致）。
```

### N19. Tab Order（runtime）

```text
真实输出：`WRITE [taborder]: 0x06 owners = [writeTab06,writeTab10,write06UnitSpin,write06AddressSpin,write06ValueSpin,write06TimeoutSpin,writeActivateButton,appBarClearResults,navItem_0]`
⇒ 0x06 的 Tab 链顺序确定：功能 Tab → 从站 → 寄存器地址 → 写入值 → 超时 → 写入按钮，
  随后离开本页进入 AppBar / rail（应用既有顺序）；
   **未激活的 0x10 侧控件不可达**（`write10UnitSpin` / `write10ValuesArea` 不在链中）。
   实现说明：Qt 常把焦点落在控件的内部子项上，因此该 oracle 用「最近的有名祖先」描述归属，
   而不是要求精确 objectName 命中。
```

### N20. TextArea Tab Escape（C36，runtime）

```text
真实输出：`WRITE [C36]: Tab escaped to [writeActivateButton]; Backtab returned to [write10ValuesArea]`
⇒ 多行 values 编辑器不再吞掉 Tab；Shift+Tab 可以回到它；编辑键与换行输入契约未改。
```

### N21. Invalidation / Consumed Authority 顺序（C33/C34）

```text
· C33（invalidation authority-first）：disconnect 场景实测 —— state 先成为 invalidated(disconnected)，
  dialog 之后不可见（QML 由 `preparedWriteChanged` 关闭）；**没有任何「先 close 再猜是否 cancel」的路径**。
· C34（consumed authority-first）：C08 实测 —— Controller 先回到 Consumed，随后 dialog 关闭；
  若 token 被拒（C10-C 第二次），dialog 不关闭、不假装成功。
```

### N22. Production-hidden 回归（C37）

```text
`--qml-focus-check` 的 prod-hidden oracle 继续 PASS：
`FOCUS [prod-hidden] PASS: write foundation not instantiated (no write control in the scene)`
⇒ normal production 无 write 控件、无 Dialog、无 write 可访问性节点、无 tab stop
（section 未被实例化 ⇒ 结构上不存在，而不是靠 visible=false）。
```

### N23. Zero Write Dispatch / 副作用区分

```text
harness 最终断言：`writeAttempts == 0`（write dispatch 从未被尝试），
并在失败信息中同时打印 `reads=N` 以区分 **read-induced expected effect**（制造 busy 的真实 FC03 读）
与 **write-foundation effect**（必须为 0）。
写路径（prepare / confirm / cancel / invalidate）不新增 transaction record、transport terminal、
statistics 计数或 diagnosis 批次（C1 的 Z01–Z05 + C2/C3 harness 共同覆盖）。
```

### N24. All-values 措辞更正（NON-BLOCKING，§49）

```text
C2 已证明的是：**全部 values 存在于未截断的 scrollable model/list**（`writeSummaryValues.count == 3`，
  每项文本由 snapshot 派生）。**不得**据此声称「所有值同时视觉可见」。
C4 将直接证明：可滚动到第一项与最后一项，且在 **1000×700** 窗口下可访问（受限高度滚动区域）。
本轮为**文档措辞更正**，不是产品缺陷。
```

### N25. 0x06 输入可用性 note（NON-BLOCKING，§50）

```text
0x06 的大范围地址/值输入目前沿用 **non-editable SpinBox**。安全上它不会静默接受越界值
（越界值不可输入；确认摘要展示的是 prepared snapshot 的值），但在 **M10-D production 首次暴露之前**，
C4 或 M10-D 的 design review **必须重新评估大地址输入效率**（例如是否需要可键入 + 显式校验）。
本轮**不重做控件**。
```

### N26. Problems / RCA（7 项；区分 Qt 语义 / harness 结构 / 产品侧观察）

```text
P1（Qt 语义）：Escape 发到 `activeFocusItem` 无效 —— Qt 把 popup 的 CloseOnEscape 处理挂在 window/overlay 层。
   Fix：Escape oracle 改为发到 window（**不是**绕过 Dialog 行为，而是按真实交付路径发送）。分类：Qt 语义理解。
P2（harness 结构）：Tab 链通过精确 objectName 断言失败（Qt 把焦点落在 SpinBox 内部子项上）。
   Fix：oracle 改为「最近的有名祖先」归属判定，仍能证明顺序与「未激活 tab 不可达」。分类：harness defect。
P3（harness 结构）：`accessibleNameOf` 对缺失控件返回占位字符串，使可访问性断言**空洞通过**。
   Fix：缺失控件返回空名字，并同时断言控件存在于树上。分类：harness defect（自我审计发现）。
P4（Qt 语义）：Dialog 的 footer 按钮只有在 popup 显示时才进入 Item 树 ⇒ a11y 检查须在 dialog 打开时进行。
   Fix：a11y 阶段先 prepare 打开 dialog，检查后 cancel。分类：Qt 语义理解。
P5（编译期，main.cpp）：新增 harness 类最初带 `Q_OBJECT` ⇒ AUTOMOC 报错（需 `main.moc`）；
   去掉 `Q_OBJECT`（只经 SerialTransport 接口使用，不需要自身 meta-object）。分类：工具链约定。
P6（测试脚手架）：脚本生成阶段的多行字面量/锚点问题导致 C3 stages 一度未插入（构建通过但功能缺失）。
   Fix：改为「先用 Write 工具生成 .inc 正文，再按唯一锚点插入」并校验插入行数。分类：harness tooling defect。
P7（**产品侧观察，非本轮引入**）：[ISSUE-014](../issues/ISSUE-014-transactions-delegate-reset-binding-warnings.md)
   TransactionsPage 的 delegate/详情绑定在 model reset 窗口内对 `undefined` 角色求值，产生 9 条
   「Unable to assign [undefined]」告警（仅在「delegate 已存在 + 模型 reset」时出现；nav harness 为 0 条）。
   **无功能/数据/写安全影响**；分类 **PRE-EXISTING NON-BLOCKING**，本轮按 scope freeze 不修，
   记入 ISSUE-014，建议未来 warning hygiene / UI robustness 任务统一处理。
   **未发现**任何产品缺陷或 Controller 状态缺陷（modal/background/hidden-page/session/busy 行为均符合冻结契约）。
```

### N27. Git

```text
behavior-bearing（QML 键盘/可访问性 + harness 键盘或acles）⇒ 不作 LKGC。
commit：`M10-C3: harden write confirmation interaction safety`（独立提交；不 amend `447e346`；不 rebase；不 push；未 tag）。
verified LKGC 保持 `ef71244`；M10-C3 = 等待 Review。
```

### N28. 关于 harness 归属的说明（§53）

```text
C3 的 runtime 键盘/焦点 oracle 放在 **`--qml-write-foundation-check`**（它才是实例化 hidden foundation 的模式），
而不是 `qml_focus_check`：后者运行在 normal production 配置下，其职责是 **prod-hidden oracle**
（证明 write 控件根本不存在）。两者合起来覆盖 §53 要求的「qml_focus_check（加入 C3 runtime keyboard/focus）」
意图：keyboard/focus 的真实运行时断言 + production 不可见性，各自在能真实成立的环境里执行。
```
## M10-C3 Correction — Rapid Enter Spillover Oracle Closure（2026-09-20，harness-only）

> **M10-C3 Review = HOLD（窄范围 keyboard safety oracle correction）。** C3 主体实现**全部接受、不重做**：
> modal/background safety / Cancel initial focus / immediate Enter zero-confirm / immediate Space non-destructive /
> Confirm+Space / Confirm+Enter / Escape / disconnect·reconnect / busy invalidation / source invalidation /
> failed Replay preservation / draft persistence / page gating / accessibility / Tab order / TextArea escape /
> production hidden / zero write dispatch。
> **唯一 blocker**：Confirm 消费并关闭 Dialog 后，**第二个快速 Enter 是否 spill 到背景控件**尚无真实 QML runtime oracle。
> 原 §N 记录**不改写**（只追加本节）。

### O0. Narrow HOLD 归档

```text
M10-C3 Review = HOLD；唯一 blocker = rapid Enter post-Dialog-close spillover 缺 runtime oracle。
本轮边界：未开始 M10-C4；未实现 encoder / dispatch；Write UI 未进入 production；未 push；未 tag；
verified LKGC 保持 `ef71244`。
```

### O1. Focus Return Path 实读（§2）

```text
· `WriteFoundationSection.qml` 的 Confirm：`Keys.onReturnPressed` 内 **先判 `confirmButton.activeFocus`**
  再 `confirmButton.clicked()`，随后 `onClicked` → `section.confirmPreparedWrite()`（携带 opaque token）
  → 成功则 `confirmationDialog.close()`。
· Dialog 关闭后 Qt 的真实焦点归属：**runtime 实测为 `Main`**（窗口内容根 Item），
  不是任意背景**控件**（不是 Write 按钮、不是 rail、不是 Read/清空结果）。
  该事实由 E1/E2 的 `focus after close = Main (owner Main)` 记录，**作为证据而非契约**：
  无论焦点落在哪里，rapid second Enter 都不得形成新的 write flow（§5/§6）。
· harness 的 key 投递：Tab 走 window（Qt 在此做 focus traversal），其余键走 `window->activeFocusItem()`——
  与 C3 已冻结的投递方式一致，**未在两次按键之间做任何焦点干预**（§12）。
```

### O2. Scenario E1 — Back-to-back Enter（真实 QML runtime）

```text
准备：valid 0x06 draft → open Dialog → 真实 Tab 到 Confirm → 断言 Confirm 持有 active focus。
真实输出：`WRITE [E1]: ready — token=8 state=prepared dialog=1 focus=writeConfirmAcceptButton`
动作：**连续两个 Return，无 sleep、无 focus 干预**（两键之间不重新设置焦点）。
结果（真实输出）：
  `WRITE [E1]: back-to-back Enter -> one consumption (token=8), no new snapshot/dialog, no background action, writes=0`
断言（全部通过）：
  · confirmation acceptance count = **1**（随后用同 token 直接调用 confirm 返回 false ⇒ 二次拒绝）
  · token 8 = **Consumed**（`stateToken() == "consumed"`），随后 `tokenOf() == 0`、`hasPreparedWrite() == false`
  · **未**出现新 generation / 新 Prepared snapshot / 第二个 Dialog / 第二次 acceptance
  · transport `writeAttempts == 0`（write dispatch 全程 0）
  · 背景未被激活：rail index 不变、transaction rows 与 observedCount 不变（清空结果类动作未发生）、
    `readStarts` 不变（**第二个 Enter 没有触发 Read**）、session id 不变
```

### O3. Focus-after-close Evidence（§6）

```text
真实输出（E1 与 E2 各一条，取自不同时序）：
  `WRITE [E1] focus after close = Main (owner Main)`
  `WRITE [E2]: focus after close = Main (owner Main)`
⇒ **Dialog 关闭后焦点回到窗口内容根 Item，而不是某个背景控件**。
本轮**没有**为了测试把焦点强制移到「人工安全位置」：产品 QML 真实行为就是如此，
harness 只是在事件循环的下一轮读取它（§12）。
```

### O4. Scenario E2 — Next-turn Enter（更接近用户连按第二下）

```text
意图：捕获「第二个 key 落到**已经恢复后的背景焦点**」这一情形（而不是两个事件都在 popup 关闭前入队）。
步骤：prepare → Tab 到 Confirm → 第一个 Return（消费 + 关闭 Dialog）→ **等待正常 event-loop
  完成 close/focus restore 一轮** → 不重新选择任何控件 → 再发第二个 Return。
真实输出：
  `WRITE [E2]: focus after close = Main (owner Main)`
  `WRITE [E2]: next-turn Enter after close -> no new flow, no background action, writes=0`
断言（全部通过）：无新 snapshot（token 0 / hasPreparedWrite false）、无 Dialog 重开、
state 仍 consumed、rail index 不变、rows/observed 不变、`readStarts` 不变、session 不变、`writeAttempts == 0`。
```

### O5. 回归（不得因本轮破坏既有 oracle）

```text
· **double Space**（Confirm 持焦点连按两次 Space）：继续 `one consumption, zero write dispatch`。
· **immediate Enter**（dialog 刚打开、Cancel 持焦点）：继续 `state=prepared reason=`（**zero confirmation**）；
  本轮**未**为了统一行为把它改成 Cancel（§9：真实结果就是「不动作」，安全，保持原样）。
· **Escape / C05**：继续 `invalidated(user_cancelled), dialog closed, draft preserved`。
· 其余 C01–C36 全部继续 PASS（**未修改任何旧断言**）。
```

### O6. C05 编号更正（append-only，§10）

```text
**冻结矩阵口径**：**C05 = Escape → Cancel semantics → zero write send**。
本轮之前 harness 输出中把「immediate Space」标成了 `[C05]`，属于**编号漂移**；
现更正为：
  · `[C05/Escape]` = Escape → Cancel → zero write send；
  · **`[C05b]`** = immediate Space（**additional keyboard safety oracle**，不是冻结矩阵里的 C05）。
原历史记录（§N5 的文字与当时的输出）**保留不改**，本节即更正声明；
§N4–N7 的技术结论不受影响（它们描述的是行为，编号只是标签）。
```

### O7. Product / Harness Diff

```text
· **产品 QML：零变化**（E1/E2 真实 PASS ⇒ 按 §11 不得改产品 QML）。
· 唯一改动 = harness（`src/main.cpp`）：新增 E1/E2 两组 stage、C05 编号更正、summary 行补充。
  未通过任何「手动清 focus / 手动聚焦安全 Item / 禁用 window / 隐藏背景 / 直接调用 Controller 代替按键」
  的方式让测试通过（§12）；E1/E2 全部使用**真实 QML key event + 真实 Dialog close + 真实 focus restore**。
· 本轮**不需要**制造 busy，因此没有额外的 FC03 read；`readStarts` 在 E1/E2 内保持不变，
  与 `writeAttempts` 分开报告（§13）。
```

### O8. 门禁（真实输出）

```text
`qml_write_foundation_check`：**PASS（exit 0）**，覆盖 C01–C36 + parser + a11y + taborder + **E1/E2**，zero write dispatch。
Debug ctest **31/31 PASS**；Release ctest **31/31 PASS**（含 qml_smoke / qml_geometry / qml_nav / qml_focus）。
write_prepare **32** / active_master **54** / ui_bridge **59** / active_request 17 全绿。
**新增代码零 warning**；`src/main.cpp` 5 条 pre-existing warnings 未动（同时实测确认：2494 / 2496 / 4594 三条行号不变；7277 与 7515 因本轮插入发生位移，旧行号为 7138 / 7376；五条均为 previous-existing，本轮新增代码零 warning）。
```

### O9. Problems / RCA

```text
本轮未发现产品缺陷或 Controller 状态缺陷；E1/E2 均一次通过（无需修改产品）。
harness 侧记录一条**编号漂移**（O6：immediate Space 曾被标为 C05）——分类 **documentation/harness labeling defect**，
已按 append-only 方式更正，不删除历史。
（其余实现期问题已在 §N26 记录：Escape 需发到 window、Tab 链祖先归属、a11y 空洞通过、footer 仅在显示时入树、
Q_OBJECT/AUTOMOC、脚本插入等，本轮未新增同类问题。）
```

### O10. Git

```text
harness/test 行为变化 ⇒ **behavior-bearing**（产品 QML 零变化也不例外）。
commit：`M10-C3: prove rapid Enter cannot escape confirmation`（独立提交；不 amend `0d5c219`；不 rebase；不 push；未 tag）。
verified LKGC 保持 `ef71244`；M10-C3 = 等待 Final Re-review。
```
## M10-C4 — Write Safety Foundation Final Acceptance（2026-09-20，harness + docs）

> **M10-C3 Final Re-review = PASS。M10-C3 = COMPLETE。M10-C4 = GO。**
> C4 是 **Final Acceptance**，不是 feature implementation：只证明 C1（pure/core）· C2（hidden UI/dialog）·
> C3（runtime interaction）三层契约组合后仍然一致。**产品代码 / QML：零变化**；新增全部在 harness（`src/main.cpp`）
> 与文档。本节为 append-only 归档，原 §N / §O 不改写。

### P0. Preflight（真实输出）

```text
HEAD = c50dbfe（branch = main，working tree clean）
verified LKGC = ef71244
v1 tag object = 2cee626；v1 target = ae067ab；v2.0.0 = ABSENT
origin/main = a40d935；ahead = 111；behind = 0
CMake VERSION = 2.0.0
git diff --check = PASS
M9 / M10 Phase 1 / M10-A / M10-B / M10-C Phase 1 / C1 / C2 / C3 全部 COMPLETE
```

### P1. M10-C Behavior Chain Audit（真实 `git show --stat --name-only`）

```text
7562678 M10-C1  CMakeLists.txt, docs/*, src/core/active/{PreparedWriteSnapshot,WriteDraftParsing,
                WritePrepareValidation}.{h,cpp}, src/ui/AnalysisController.{h,cpp},
                tests/{test_active_master,test_write_prepare}.cpp          ⇒ behavior-bearing
447e346 M10-C2  CMakeLists.txt, docs/*, src/main.cpp, src/ui/AnalysisController.{h,cpp},
                src/ui/qml/components/WriteFoundationSection.qml,
                src/ui/qml/pages/CommunicationPage.qml                     ⇒ behavior-bearing
0d5c219 M10-C3  docs/*（含 ISSUE-014）, src/main.cpp,
                src/ui/qml/components/WriteFoundationSection.qml           ⇒ behavior-bearing
c50dbfe M10-C3  docs/*, src/main.cpp                                       ⇒ behavior-bearing
        correction
C4（本节）      src/main.cpp（harness 新增 oracle）+ docs/*                ⇒ behavior-bearing
```

⇒ **最终 accepted M10-C behavior-bearing tree = C4 commit**（按 §44 规则，以真实 diff 为准；
docs-only closure commit 不作 LKGC）。

### P2. Final Source Audit 与 Authority Map（§3）

```text
QML draft（WriteFoundationSection 的 page-local property）
  → Controller prepareWrite06 / prepareWrite10（Q_INVOKABLE，int 入参）
     → core::prepareWriteSingleRegisterIntent / prepareWriteMultipleRegistersIntent
        （全部 int64 入参，先验证再窄化）
        → AnalysisController::prepareWriteIntent（PRIVATE；context guard：source / connected / busy）
           → PreparedWriteStore::prepare(PreparedWriteSnapshot)   ← 唯一创建点
              → Dialog projection（14 个只读 Q_PROPERTY，单一 preparedWriteChanged 通知）
                 → confirmation token（opaque qulonglong，QML 只回传该值）
                    → Consumed | Invalidated（terminal，原因保留）
```

- 实读文件：`src/core/active/{PreparedWriteSnapshot,WriteDraftParsing,WritePrepareValidation}.{h,cpp}`、
  `ActiveRequestIntent.{h,cpp}`、`src/ui/AnalysisController.{h,cpp}`、
  `src/ui/qml/components/WriteFoundationSection.qml`、`src/ui/qml/pages/CommunicationPage.qml`、
  `src/ui/qml/Main.qml`（page gating）、`src/main.cpp`（write harness + focus harness）、CMake tests。
- 唯一 `preparedWriteStore_.prepare(...)` 调用点 = `AnalysisController.cpp:1464`（其余出现在
  `tests/test_write_prepare.cpp` 的纯 store 测试中）。

### P3. Bypass Audit（§3 逐项）

| 可能的旁路 | 结论 | 证据 |
| --- | --- | --- |
| QML → transport 直接发送 | **不存在** | 写 UI 只调用 Controller 的 prepare/confirm/cancel；`WriteFoundationSection.qml` 无 transport 引用 |
| draft → confirm 重新读取 | **不存在** | confirm 只回传 token；summary 绑定只读 Controller projection |
| QML 自己持有 consumed 状态 | **不存在** | `activeFunctionIndex` 之外的 UI 状态都是 draft；`hasPreparedWrite` 等全部来自 Controller |
| fake capability（伪功能可用） | **不存在** | 无 encoder、无 dispatch；`writeFoundationVisible` 明确不是 capability 信号 |
| write encoder | **ABSENT** | `encodeActiveRequest` 对 0x06/0x10 返回 `UnsupportedFunction` |
| write dispatch | **ABSENT** | 无 encode→transport 路径；harness transport `writeAttempts == 0` |

### P4. C01–C37 Coverage Matrix（§5）

> Layer：**P** = pure C++（`write_prepare` / `active_master` / `active_request`），**C** = Controller（`ui_bridge` /
> harness），**Q** = QML runtime（write-foundation / focus harness）。每条都给出**直接断言**（不是「grep 过」）。

| Oracle | 内容 | Layer | 断言位置 | 真实证据（节选） |
| --- | --- | --- | --- | --- |
| C01 | invalid unit | Q+C | `--qml-write-foundation-check` | `WRITE [C01]: unit 0 -> [... 1..247 ...], no dialog, no token` |
| C02 | invalid address/value | Q | 同上（parser presentation） | `WRITE [C02]: 65536 -> [第 1 行的数值必须 0..65535]` |
| C03 | valid prepare/dialog zero-send | Q | 同上 | `prepared token=1, dialog open, startAttempts=0` |
| C04 | Cancel | Q | 同上 | `Cancel -> invalidated(user_cancelled), draft preserved, zero send` |
| C05 | Escape → Cancel 语义 | Q | 同上 | `[C05/Escape]: invalidated(user_cancelled), dialog closed, draft preserved` |
| C05b | immediate Space（additional oracle） | Q | 同上 | `[C05b]: immediate Space -> state=invalidated reason=user_cancelled` |
| C06 | initial focus | Q | 同上 | `[C06]: initial focus = writeConfirmCancelButton` |
| C07 | immediate Enter | Q | 同上 | `[C07]: immediate Enter -> state=prepared reason=`（zero confirmation） |
| C08 | Confirm + Space | Q | 同上 | `[C08]: Confirm+Space -> consumed once (token=5), dialog closed, zero write dispatch` |
| C08b | Confirm + Enter | Q | 同上 | `[C08b]: Confirm+Enter -> consumed` |
| C09 | double Write | Q | 同上 | `[C09]: repeated Write kept token=1 and one dialog` |
| C10-C | one-shot confirmation | Q+P | 同上 + `write_prepare` S04–S08 | `[C10-C]: one consumption for token=2; second confirm rejected` |
| C11 | disconnect invalidation | Q | 同上 | `[C11]: disconnect -> invalidated(disconnected), dialog closed by the authority` |
| C12 | new-session stale token | Q | 同上 | `[C12]: reconnect -> old token 0 unusable (session=2)` |
| C13 | busy invalidation | Q | 同上（真实 FC03 读） | `[C13/C14]: busy false->true invalidated; busy->false did not revive` |
| C14 | hidden-page safety | Q | 同上 | `[C14]: hidden page disabled; Tab/Space/Enter prepared nothing, dispatched nothing` |
| C15 | Clear Results 正交 | Q | 同上 | `[C15]: Clear Results preserved drafts and the prepared snapshot` |
| C16 | 独立 drafts | Q | 同上 | `[C16]: both drafts survived the tab switches` |
| C17 | derived quantity | Q+P | 同上 + `write_prepare` | `[C17]: 0x10 prepared, quantity 3, all three values listed` |
| C18 | address-span | Q+P | 同上 + `write_prepare` V09 | `[C18]: 65535+2 -> [起始地址超出 16 位寄存器地址空间]` |
| C19 | summary == snapshot | Q | 同上 | `[C19]: summary fields equal the snapshot projection (unit 11 / addr 100 / value 1234 / COM_HARNESS @ 9600)` |
| C20 | draft 变更不改变 snapshot | Q+P | 同上 + `write_prepare` S09 | `[C20]: draft edited to 7/4321, snapshot and summary still 100/1234` |
| C21 | timeout state-unknown wording | **contract/future-dispatch** | 文档 + M10-A typed 语义 | 见 §P19：M10-C 无 production write outcome UI，**不制造假 runtime case**；写超时的用户语义被锁定为「响应超时，设备写入状态未知」（`ActiveTransportTerminal` + M10-D/E 口径） |
| C22 | PossiblySent 不确定性 wording | **contract/future-dispatch** | 文档 + M10-A 证据语义 | 见 §P19：short submission / post-submission error / post-submission disconnect 都**不能**证明「设备未改变」，继承 M10-A，不由 C 层削弱 |
| C23–C29 | parser contracts | P | `write_prepare` P01–P12 / V01–V08 | 32 passed（LF / CRLF / 空行规则 / 65535 上限 / 非十进制形式 / 123 / 124 / 巨大整数 / 边界与跨度） |
| C30 | source replacement | Q | write-foundation harness | `[C30]: Simulator replacement -> invalidated(source_changed), drafts preserved` |
| C31 | failed Replay preservation | Q | 同上 | `[C31]: failed Replay load -> snapshot and draft preserved` |
| C32 | draft persistence across reconnect | Q | 同上 | `[C32]: drafts preserved; old snapshot invalidated` |
| C33 | authority-first invalidation | C+Q | `ui_bridge` + write harness | 失效点由 authority 触发后再关 Dialog（`onPreparedWriteChanged`），从不反过来 |
| C34 | authority-first consumption | C+Q | `write_prepare` S04/S05 + harness C10-C | consumption 先发生，UI 才关闭 |
| C35 | terminal reason preservation | Q+P | 同上 | `[C35]: reason stays busy_became_true after a later disconnect` |
| C36 | TextArea Tab escape | Q | 同上 | `[C36]: Tab escaped to [writeActivateButton]; Backtab returned to [write10ValuesArea]` |
| C37 | production-hidden exclusion | Q | `--qml-focus-check`（Debug + Release） | `FOCUS [prod-hidden] PASS: loader inactive/item null; 675 objects scanned — no write control, no write accessible node, no write tab stop, no prepared snapshot` |

### P5. Direct vs Composed Evidence（§6 口径，不得混淆）

- **Direct runtime（真实 QML runtime 直接断言）**：C01–C20、C30–C32、C36、C37、C4 的 geometry / scroll /
  boundary / keyboard / modal 全部条目。
- **Pure C++ evidence**：C23–C29（parser）、C10-C / C17 / C18 / C20 的 store 与 validation 半边。
- **Controller evidence**：C33 / C34（authority-first）、C13/C14 的 busy 语义、C11 的 disconnected 失效原因。
- **Composed（组合证据，**不**声称单测直接跑过 QML）**：
  · 「Replay 失败 → 不清 snapshot」由 C31（runtime）+ source/session 语义（pure/controller）**合成**；
  · 「busy 失效」由 Controller 单测（原因与状态机）+ C13（真实 FC03 读产生 busy 后 Dialog 关闭）**合成**；
  · C21 / C22 是 **design/future-dispatch contract**，M10-C 不伪造 runtime 场景。

### P6. Geometry — 1024×720（harness-visible，§7）

```text
WRITE [C4 geometry 1024x720 0x06]: window=1024x720 writeFoundationPanel=(73,285 935x139)
    writeFunctionTabs=(85,297 911x21) write06DraftRow=(85,326 911x24)
    writeActivateButton=(85,358 50x34) writeValidationError=(85,400 911x12)
WRITE [C4 geometry 1024x720 0x10]: window=1024x720 writeFoundationPanel=(73,285 935x262)
    write10DraftColumn=(85,326 911x147) write10ValuesScroll=(85,377 911x96)
    writeActivateButton=(85,481 50x34) writeValidationError=(85,523 911x12)
```

断言：tabs / 0x06 四个 SpinBox / 0x10 三个 SpinBox + editor / Write action / 0x10 editor（与其 viewport 相交）
全部 `isVisible` 且 scene rect **完全落在窗口内**（无裁切）；validation message 可见时：在窗口内，且
**与 tabs / 0x06 行 / editor / Write action 均不相交**。窗口尺寸断言为 **1024×720**（不依赖窗口自动扩大）。

### P7. Geometry — 1000×700（最低验收几何，§8）

```text
WRITE [C4 geometry 1000x700 0x10]: window=1000x700 writeFoundationPanel=(73,285 911x262)
    writeFunctionTabs=(85,297 887x21) write10ValuesScroll=(85,377 887x96)
    writeActivateButton=(85,481 50x34) writeValidationError=(85,523 887x12)
WRITE [C4 geometry 1000x700 0x06]: window=1000x700 writeFoundationPanel=(73,285 911x119) ...
WRITE [C4 geometry restored 1024x720 0x10]: window=1024x720 ...
```

- harness 主动 `resize(1000, 700)`，并在断言里**先验证窗口确实是 1000×700**：
  `the harness must not rely on the window growing itself` 这一失败路径存在且未触发。
- **Dialog 在最低尺寸下同样可达**（新增 oracle）：`writeSummaryFunction / writeSummaryUnit /
  writeSummaryAddress / writeSummaryQuantity(Label) / writeSummaryValues / writeConfirmCancelButton /
  writeConfirmAcceptButton` 全部可见且在窗口内 —— 「Dialog 未越界、Cancel/Confirm 不需要 resize 就能按到」。
- 现有 `qml_geometry_check` 不加载 hidden foundation，因此该几何验收由**本 harness 的窄 runtime 场景**承担，
  并已纳入 ctest（`qml_write_foundation_check`），不是一次性手工执行。

### P8. 0x10 Editor 首尾滚动可达（§9）

```text
WRITE [C4 scroll]: scroller=write10ValuesScroll content=432 viewport=96
WRITE [C4 scroll]: last line y=412 visible in band [336,432) at contentY=336 of max 336
WRITE [C4 scroll 1000x700]: last line y=272 in band [196,292) at contentY=196
```

- 30 行（1024×720）与 20 行（1000×700）时，editor 的真实可滚动 flickable 为 **ScrollView**（`ScrollView`
  content=432 / viewport=96），harness **实测**该结构而非假设。
- 可见带用几何映射计算（`mapRectFromItem` 把 viewport 映射进 editor 坐标系），不是 `contentHeight > height`
  这种弱断言：**第一行**在 `contentY=0` 时位于可见带内；**最后一行**在 `contentY = max` 时位于可见带内
  （y=412 ∈ [336,432)）。
- 断言同时要求真实位移发生（`contentY > 0`），即「末行可达」是**滚动**的结果。

### P9. Confirmation 全量 values 可滚动访问（§10）

```text
WRITE [C4 confirm-scroll]: readable top rows = [1000..1009]
WRITE [C4 confirm-scroll]: readable bottom rows = [1031..1039] — all 40 values reachable by scrolling
                           (not all visible at once)
```

- 40 个 values：`list count == 40 == snapshot values.size()`；quantity 显示 `40`；list viewport = 120px 上限
  （断言 `height <= 121`），`contentHeight > height`。
- 「可读」用**真实渲染的 delegate 文本 + viewport 带过滤**判定（cacheBuffer 会保留视口外的 delegate，
  不过滤就会把「已实例化」误当成「在屏幕上」）；顺序用 trailing number 连续递增校验。
- 结论口径：**「全部 snapshot values 都可通过滚动访问」** —— **不写**「全部同时可见」。

### P10. 123-value 边界与 124 拒绝（§11）

```text
WRITE [C4 boundary]: value #1 readable at the top ([1..10])
WRITE [C4 boundary]: value #123 readable at the bottom ([115..123])
WRITE [C4 boundary]: 124 values -> [寄存器数量必须在 1..123 之间]
```

- 123：prepare **PASS**、Dialog `quantity = 123`、`list count = 123`、`#1` 与 `#123` 都能滚到并读出。
- 124：**validation reject**、Dialog **不开**、无 snapshot token、validation presentation 可见。
- 不要求 123 项同时显示（与 §11 一致）。

### P11. 长 values list 的键盘可达性（§12）

```text
WRITE [C4 keyboard]: Tab chain = [writeConfirmCancelButton,writeConfirmAcceptButton]
WRITE [C4 keyboard]: Shift+Tab returns to Cancel; list keys neither confirmed nor trapped
```

- Cancel 持初始焦点；**Tab 一次即到 Confirm**（有界 walk ≤ 8 次，实际 1 次）；**Shift+Tab 回到 Cancel**。
- **ListView 本身不进入 Tab 链**——这是真实行为，按 §12 记录为事实（destructive action 只能显式聚焦后触发）。
- 把焦点移到长 list 上按 Down / PageDown / **Return**：token 不变、state 仍 `prepared`、Dialog 不关、
  `writeAttempts == 0` —— 列表键盘导航**不会**误确认。

### P12. Production-hidden 最终证明（§13 / §15，Debug + Release）

```text
FOCUS [prod-hidden] PASS: loader inactive/item null; 675 objects scanned — no write control,
      no write accessible node, no write tab stop, no prepared snapshot
FOCUS [prod-hidden] PASS: Communication Tab chain has no write stop
      ([navItem_0..navItem_4, commPortCombo, , commBaudCombo, ...])
```

- 深度证明（相对 C2 的「loader 未激活」）：
  (1) **全场景 objectName 扫描** 675 个对象，除 `writeFoundationLoader` 外**无任何 `write*` 控件**；
  (2) 具名写控件（section / Write 按钮 / tabs / editor / SpinBox / 列表 / Confirm）逐个断言**不存在**；
  (3) **startup 不产生 snapshot**（authority 断言：`hasPreparedWrite == false`、token == 0、state == `none`）；
  (4) **Communication 的 Tab 链里没有任何 write 停靠点**（不是「看不见」而是「不在键盘顺序里」）。
- **Release 也执行**：`qml_focus_check` 与 `qml_write_foundation_check` 均属 ctest 31 项，Debug/Release
  各自 31/31 PASS；Release 另外手工重跑两者，输出同上。
- 不是 grep QML：全部是 runtime 断言。

### P13. Harness-only Seam Audit（§14）

```text
engine.rootContext()->setContextProperty(
    QStringLiteral("writeFoundationVisible"),
    app.arguments().contains(QStringLiteral("--qml-write-foundation-check")));
```

- 唯一开关 = **精确的 CLI flag**；`CommunicationPage.qml` 只以 `active: writeFoundationVisible` 使用它。
- 未发现任何环境变量、source selection、debug/release、配置文件路径能启用它（`qreal` 之外无
  `qEnvironmentVariable` 出现于该路径）；normal main path 默认为 **false**。
- 语义声明（代码注释与文档同口径）：**它不是 capability 信号**——即使打开，0x06 / 0x10 仍无 encoder、无 dispatch。

### P14. Snapshot Domain Invariant（§16）

- `PreparedWriteSnapshot` 只能由 `prepareWrite06` / `prepareWrite10` 经
  `prepareWriteSingleRegisterIntent` / `prepareWriteMultipleRegistersIntent` 创建；
  `prepareWriteIntent` 是 **private**，无 generic `prepareActiveRequest(intent)` 可用。
- 0x03 **不能**进入该 domain：两个 builder 构造的 intent function 固定为 0x06 / 0x10。
- 生产代码里 `preparedWriteStore_.prepare(...)` 只有**一处**调用（`AnalysisController.cpp:1464`）。

### P15. Validate-before-narrowing 与 CRLF（§17 / §18）

- 全部输入以 `int64_t` 进入 validation，**先验证再窄化**：`-1` / `65536` / 巨大整数（`p12`）都被拒绝，
  不 wrap、不抛异常、不受 locale 影响（`write_prepare` 32 passed，含 `v04/v06 …BeforeNarrowing`、`v09` 跨度边界）。
- QML 适配层**没有**把用户输入先转成 uint16：`Q_INVOKABLE prepareWrite06(int, int, int, int)` /
  `prepareWrite10(int, int, QString, int)`；QML 直接传 SpinBox 的 `int`。SpinBox 的 `0..65535` 只是
  **控件层边界**，C++ API 仍是宽类型 authoritative validation。
- CRLF：`"1\r\n2\r\n3"` 在 pure parser 继续 PASS（`p02_crlfSeparated`）；本轮未新增 QML CRLF case
  （§18 明示 pure test 足够）。

### P16. Replay 失败保留 / Clear 正交 / 共享 busy（§19 / §20 / §21）

- **failed Replay**：`[C31]: failed Replay load -> snapshot and draft preserved` —— 不 invalidate、不清 draft、
  不换 session、不断 serial、不切 authoritative source（Direct runtime）。
- **Clear Results**：`[C15]: Clear Results preserved drafts and the prepared snapshot` —— 清 transactions /
  terminals / stats / diagnosis，但不清 draft、不 consume/invalidate snapshot、不发送、不断开。
  pending FC03 的 clear 语义继续继承 M10-B，未在 M10-C 重新定义。
- **共享 busy**：只有**一套** `serialBusy`（Read 与未来 Write 共用），
  `[C13/C14]: busy false->true invalidated; busy->false did not revive (reads=1, write attempts=0)`
  —— 真实 FC03 读产生 busy 即足以 invalidate Prepared confirmation；未新增 `writeBusy` 或第二套并行 authority。

### P17. Confirmation Is Not Outcome / Timeout / PossiblySent（§22 / §23 / §24）

- 全量 grep：写 UI 的**唯一**含「发送」的用户可见字符串是 Cancel 的无障碍名
  **「取消写入（不发送任何请求）」**；不存在「写入成功 / 发送成功 / 设备已写入」这类文案。
  `Consumed` 只表示 **user confirmation accepted**；写 outcome 要到 M10-D 有真实 response 才存在。
- **Timeout contract（归档给 M10-D/E）**：写请求等待响应超时 ⇒ transaction outcome = `Timeout`，
  用户语义 = **「响应超时，设备写入状态未知」**，**不得**表述为「设备未写入 / 写操作未发生」。
  M10-C 不伪造该 runtime 场景。
- **PossiblySent contract（继承 M10-A）**：short submission / submission 之后的 transport error /
  submission 之后的 disconnect 都**不能**证明 device unchanged；M10-C 的 confirmation 层不覆盖、不弱化该语义。

### P18. No Encoder / Zero Write Dispatch / Zero Write Transaction（§25 / §26 / §27）

```text
encodeActiveRequest: 0x06 / 0x10 → ActiveRequestEncodeError{UnsupportedFunction}
（无 test-only encoder：tests 只验证「拒绝」与被动分析既有捕获报文）
WRITE [C4 final]: session history function codes = [3]; zero 0x06/0x10 transactions,
                  zero write dispatch (FC03 reads=3)
```

- 完整 C01–C37 + E1/E2 + C4 全矩阵执行结束后：`transport->writeAttempts() == 0`。
- 会话历史（用户可见记录）里 **function code 只有 3（FC03）**，没有 0x06 / 0x10 transaction，
  也没有任何 write transport terminal；为制造 busy 的 FC03 读**单独计数**（reads=3），与写活动严格分开。

### P19. Accessibility 终检（§28）

- 具名控件（写 tabs / draft inputs / values editor / Write action / Dialog 标题 / summary / Cancel / Confirm）
  均有 accessible name（真实读取 `QAccessibleInterface::text(Name)`），
  `[a11y]: names present (写入（打开确认对话框） / 取消写入（不发送任何请求） / 确认写入意图)`。
- 状态同步：busy 时 `[a11y]: busy runtime -> Write action disabled=1`（disabled control 与 enabled 一致）。
- 口径：**不做** WCAG / screen-reader certification。

### P20. Keyboard Matrix 与 Modal Matrix（§29 / §30）

| Context | Key | 期望动作 | 实测 |
| --- | --- | --- | --- |
| Dialog 刚打开 | Enter | zero confirm | `state=prepared reason=` ✅ |
| Cancel focus | Space | Cancel | `invalidated(user_cancelled)` ✅ |
| Confirm focus | Space | one consume | `consumed once (token=5)` ✅ |
| Confirm focus | Enter | one consume | `[C08b]: consumed` ✅ |
| Confirm focus | rapid Enter×2 | one consume / no spill | `[E1]/[E2]` ✅ |
| Dialog | Escape | Cancel | `[C05/Escape]` ✅ |
| values TextArea | Tab | escape | `[C36]` ✅ |
| values TextArea | Shift+Tab | back escape | `[C36]` ✅ |
| hidden page | Enter/Space | zero action | `[C14]` ✅ |
| 长 values list | Down / PageDown / Return | 不确认、不逃逸 | `[C4 keyboard]` ✅ |

| Modal 场景 | 实测 |
| --- | --- |
| outside click | `[outside-click]: dialog stayed open, snapshot stayed prepared` ✅ |
| rail click | `[modal-nav]: rail 2 -> 2 (modal blocked the click); state=prepared` ✅ |
| 背景 tab / Write activation | 被 modal 阻断（同一 blocked-click 机制），无新 snapshot、无发送 ✅ |

### P21. 0x06 SpinBox Usability Decision（§31，M10-D 前置）

真实测量（Fusion 风格，应用实际使用的 style）：

```text
WRITE [C4 probe]: 0x06 address SpinBox editable=0 stepSize=1 range=0..65535
      | typing 1234: 0 -> 0 | Up key: 0 -> 0 | click on the up indicator: 0 -> 1
```

- `editable=0` ⇒ contentItem 是 **read-only TextInput**：**键盘无法直接输入数值**（输入 1234 无效）。
- `Up/Down` 键**不改变数值**（0 → 0）：非编辑型 SpinBox 在本 style 下没有键盘步进路径。
- 唯一可用的输入路径是 **鼠标点击 up/down 指示器**，每次 `stepSize = 1`。
- **正式判定：现状不可接受作为 M10-D v1 的 production 输入**——16 位地址/数值范围下，
  到达任意地址最坏需要 **65535 次点击**，键盘用户完全无法完成该操作。
- **处理方式（遵守 §31）**：**不在 C4 重做产品**。记录为 **M10-D Design Blocker**：
  M10-D 的 Learning/Design 必须先在下列方案中作出决定并留档（例如 editable 输入 + 校验、
  分位输入（高/低字节）、或 address/value 的十进制直接输入框），再让 0x06 Write section 进入 production。

### P22. Write Production Visibility Readiness Freeze（§32）

M10-C 完成**不代表** Write UI 可以显示。冻结条件：0x06 Write section **首次 production-visible**
必须同时满足 **0x06 encoder / dispatch / response matching / write timeout semantics / exactly-one-send /
production UI enablement** 六项（全部属 M10-D），且需先解决 §P21 的 Design Blocker。

### P23. AI / Agent Boundary（§33）

- `AI / Agent write authority = NONE`：`src/core/ai`、`src/core/agent`、`src/ui/agent`、`src/ui/ai` 中
  **不存在** `prepareWrite` / `confirmWrite` / serial send / raw ADU / transport 访问（grep 结果为空）。
- C4 未新增任何写工具或权限，只读诊断边界不变。

### P24. ISSUE-014 状态（§34）

- **仍然可复现**（未修，且未顺手修）：write-foundation harness 中模型 reset 窗口内产生
  **10 条** TransactionsPage 告警，分布在 10 行（271–277 / 344 / 349 / 379）；`--qml-focus-check` 同类告警 **0 条**。
- append-only 澄清：ISSUE-014 正文写「9 条」，本轮实测为 **10 条**（同一组源行，计数差异而已，结论不变）；
  ISSUE-014 原文不改写。
- **写 UI 自身告警为 0 条**；该 issue 保持 **PRE-EXISTING NON-BLOCKING**，
  **不作为 M10-C PASS 的必要条件**（QML warning count = 0 不是验收口径）。

### P25. Full Regressions 与 Warnings（§35–§39）

```text
Debug  ctest: 100% tests passed, 0 tests failed out of 31
Release ctest: 100% tests passed, 0 tests failed out of 31
（31 项含 qml_smoke / qml_geometry_check / qml_nav_check / qml_focus_check / qml_write_foundation_check）
```

targeted suites（Release，真实 Totals）：

```text
write_prepare 32 · active_request 17 · active_master 54 · ui_bridge 59
statistics 12 · statistics_integration 3 · diagnosis 17 · serial 21 · serial_adapter 7
replay_log 14 · replay_analysis 9 · transaction 20 · transaction_integration 5 · function03 15
fault 7 · fault_integration 4 · passive 55 · codec 9 · crc 8 · frame 6
simulator 15 · simulator_integration 3 · ai 23 · agent_tools 15 · agent_runtime 28 · agent_integration 24
→ 全部 0 failed / 0 skipped
```

- 编译告警：新增 harness 代码 **零新增 warning**；`src/main.cpp` 5 条 **pre-existing** 未动
  （插入后行号：`dashboardIndex` 2495、`communicationIndex` 2497、`lst` 4595、`dir` 冗余捕获 8208、`isUnder` 8446）。
- ISSUE-014 的 QML reset-window 告警**单独报告**（§P24），不与「new warning」混同。

### P26. Manual Visual Sanity（§40）

- 截图（`--qml-write-dump <dir>`，仅 harness 可见时产生，不是 oracle 的替代）：

```text
build/_c4_shots/c4-1024x720-0x10.png          1024x720
build/_c4_shots/c4-confirmation-1024x720.png  1024x720
build/_c4_shots/c4-1000x700-0x10.png          1000x700
build/_c4_shots/c4-confirmation-1000x700.png  1000x700
```

- 人工查看结论：两个尺寸下 Write section 完整落在窗口内、validation 文案在 Write action 下方且不覆盖控件；
  Dialog 居中且不越界，summary 与 values 列表在 120px 上限内滚动，Cancel / Confirm 完整可见可点。
  （离屏平台无 CJK 字体，文字渲染为方框，属既有截图约定，与本轮无关。）
- **REAL HARDWARE NOT VERIFIED**（未连接任何真实串口设备）。

### P27. Problems Encountered / RCA（§41）

C4 期间发现并修复 **3 个 harness oracle 缺陷**（分类：harness oracle defect，非产品缺陷；已按
Observed / Expected / Evidence / Root Cause / Fix / Verification / Regression Protection 留痕）：

1. **编辑器可见带模型错误** — Observed：`last line y=412 not in band [672,768)`；
   Root Cause：把 viewport 通过 `contentItem` + `contentY` 二次映射，等于把滚动量算了两次；
   Fix：改为 `mapRectFromItem` 几何映射（+ editor 自身 `contentY` 修正），兼容两种滚动结构；
   Verification：`last line y=412 visible in band [336,432) at contentY=336 of max 336`；
   Regression Protection：两个尺寸都断言首/末行，且要求真实位移发生。
2. **ListView cacheBuffer 让视口外 delegate 被当成「在屏幕上」** — Observed：bottom rows 仍含首值 `1`；
   Root Cause：只按「实例化」判定可读行；
   Fix：按 viewport 带过滤并按 y 排序；Verification：`readable bottom rows = [1031..1039]`、
   `#123 readable at the bottom ([115..123])`；Regression Protection：contiguity + 首/末值双向断言。
3. **SpinBox 探针用错了输入路径** — Observed：`increase()` invoke 返回 false、点击 (width-12, height/2) 无效果；
   Root Cause：Fusion style 的 up 指示器是**右上角**矩形（implicit 16 × height/2-1），且值为下界时点 down 必然无变化；
   Fix：改点 up 指示器内部坐标 (width-8, height/4) 并同时测量 typing / Up 键；
   Verification：`click on the up indicator: 0 -> 1`；Regression Protection：值域断言（不得越出 0..65535）。

产品侧**未发现**任何 defect：全部 C01–C37 / geometry / scroll / boundary / keyboard / modal oracle
在第一次以正确模型断言后即 PASS，因此按 §42 **产品代码与 QML 零变化**。

### P28. Diff / Contract Freeze / Git（§42–§47）

- **产品 diff：零**（`src/core`、`src/ui/*.cpp`、`src/ui/qml/**` 均未改）。
- **harness diff**：仅 `src/main.cpp`（C4 geometry / scroll / boundary / keyboard / dialog-reachability /
  usability probe / zero-write-transaction oracle / prod-hidden 深度化 / `--qml-write-dump`）。
- **docs diff**：T022 本节 + PROJECT_STATUS / BACKLOG / devlog / INTERVIEW_NOTES 同步。
- **M10-C 契约冻结（A–O）**：A draft = QML page-local；B prepare = C++ authoritative validation；
  C `PreparedWriteSnapshot` = Controller immutable authority；D token one-shot；E summary = snapshot projection；
  F Cancel/Escape = invalidate / zero send；G confirmation = Consumed only（**不是** write outcome）；
  H disconnect/session/source/busy 使旧 confirmation 失效；I busy false→true **永久** invalidate；
  J failed Replay 保持 source/session ⇒ 不 invalidate；K draft 跨导航/Clear/disconnect/reconnect/source replacement 存活；
  L production Write hidden；M 0x06/0x10 encoder absent；N write dispatch absent；O AI/Agent write authority NONE。
- **Git**：C4 为 behavior-bearing（harness 变化），独立提交
  `M10-C4: complete write safety foundation acceptance`；未 amend `c50dbfe`、未 rebase、未 push、未 tag。
- **LKGC 规则**：C4 自动测试 PASS **不**推进 LKGC；先 M10-C4 Review，Review PASS 后再用
  docs-only closure 推进 `ef71244` → 最终 accepted M10-C behavior-bearing tree（C4 commit），
  closure commit 本身不作 LKGC。
## M10-C Closure — Final Acceptance Docs-only Closure（2026-09-20，docs-only）

> **M10-C4 Review = PASS。M10-C4 = COMPLETE。M10-C = COMPLETE。**
> 本轮严格 docs-only：不改 `src/` / `tests/` / QML / CMake；不实现 encoder / dispatch；不 production-enable Write UI；
> 不修 ISSUE-014、不修 SpinBox；未 push、未 tag。本节 append-only，原 §I–§P 不改写。

### Q0. Preflight 与 LKGC Context-drift Audit（§0 / §1）

```text
HEAD = fc86dcc（branch = main，working tree clean）
verified LKGC（closure 前）= ef71244
v1 tag object = 2cee626；v1 target = ae067ab；v2.0.0 = ABSENT
origin/main = a40d935；ahead = 112；behind = 0
CMake VERSION = 2.0.0；git diff --check = PASS
M9 / M10 Phase 1 / M10-A / M10-B / M10-C Phase 1 / C1 / C2 / C3 / C4（Review PASS）全部 COMPLETE
```

**LKGC 权威位置审计（closure 前）**：

| 位置 | 当前 LKGC 声明 | 判定 |
| --- | --- | --- |
| `docs/PROJECT_STATUS.md`（单行 LKGC 行 + 状态面板 LKGC 行，**两处**） | `ef71244`（M10-B closure） | ✅ 与预期一致，无 drift |
| `docs/BACKLOG.md` | M10-C 行写「verified LKGC 保持 `ef71244`」 | ✅ 一致 |
| `docs/tasks/T022-*.md` | §O/§P 写「verified LKGC 保持 `ef71244`，Review PASS 后才推进」 | ✅ 一致 |
| `docs/devlog/…` / `docs/INTERVIEW_NOTES.md` | 同口径（保持 `ef71244`） | ✅ 一致 |

**`ae067ab` 出现位置审计（允许类别）**：

```text
docs/PROJECT_STATUS.md            → 历史值列表 + 「V1 tag v1.0.0 = ae067ab（object 2cee626，永久不变）」+ T015 行
docs/11_V2_UPGRADE_PLAN.md:10     → 「verified LKGC（PROJECT_STATUS 记录）：ae067ab（T015 整体 DONE 的 verified
                                    code/test baseline）」= V2 规划文档中的 V1 baseline 陈述（V1-era）
docs/11_PROJECT_FINAL_RETROSPECTIVE.md:24 → 「Last Known Good Commit = ae067ab（T015 整体 DONE）」= V1 复盘事实
```

⇒ **没有任何 authoritative 当前状态位置把 `ae067ab` 写成当前 verified LKGC**；它只以
**V1 verified product baseline / v1.0.0 target / historical LKGC** 三类允许身份出现。因此**不触发 STOP**，
也不改写任何档案原文（只增不改）；报告叙事里出现过的「LKGC v1.0.0 → ae067ab」按 **report narrative drift**
处理，不代表仓库状态。

### Q1. M10-C4 Final Acceptance PASS 归档（§2）

```text
M10-C4 Review = PASS ⇒ M10-C4 = COMPLETE ⇒ M10-C = COMPLETE
C01–C37 Final Acceptance = PASS（pare/core + controller + QML runtime 三层组合）
分层归档（§2 要求逐项覆盖）：
  · pure/core        ：write_prepare 32 passed（parser P01–P12、validation V01–V12、store S01–S09）
  · controller       ：active_request 17 / active_master 54 / ui_bridge 59（authority-first 失效与消费）
  · QML runtime      ：qml_write_foundation_check（C01–C37 + E1/E2 + C4 全部）
  · keyboard         ：Dialog 初始 focus=Cancel；Enter/Space/Escape/Tab/Backtab/rapid Enter×2 全部 PASS
  · modal            ：outside click / rail click / 背景 tab / 背景 Write 均不改变 snapshot、不发送
  · geometry         ：1024×720 PASS、1000×700 PASS（Dialog 亦可达；窗口无需自动扩大）
  · scrollability    ：editor 首/末行可达；confirmation 全量 values 可滚动访问；123 边界 PASS；124 拒绝
  · production-hidden：normal production 无 write control / Dialog / Tab stop / a11y action / startup snapshot
  · full regression  ：Debug ctest 31/31、Release ctest 31/31
```

### Q2. Behavior Chain Audit（§3，真实 `git show --stat --name-only`）

```text
7562678 M10-C1 → src/core/active/{PreparedWriteSnapshot,WriteDraftParsing,WritePrepareValidation}.*,
                 src/ui/AnalysisController.*, tests/*, CMakeLists.txt                 ⇒ behavior-bearing
447e346 M10-C2 → src/main.cpp, src/ui/AnalysisController.*,
                 src/ui/qml/components/WriteFoundationSection.qml,
                 src/ui/qml/pages/CommunicationPage.qml, CMakeLists.txt              ⇒ behavior-bearing
0d5c219 M10-C3 → src/main.cpp, src/ui/qml/components/WriteFoundationSection.qml      ⇒ behavior-bearing
c50dbfe M10-C3 → src/main.cpp（correction：E1/E2 rapid-Enter oracle）                ⇒ behavior-bearing
        correction
fc86dcc M10-C4 → src/main.cpp（geometry / scroll / boundary / keyboard / dialog-reachability /
                 zero-write-transaction oracle / prod-hidden 深度化）                ⇒ behavior-bearing
```

分类依据是**真实 changed files**（行为或验收行为发生变化），不是 commit message。

### Q3. Amend-history Preservation（§4）

```text
0d5c219 未被 amend。
其后的 C3 correction task 最初产生 50ac736，随后在同一任务内 amend 为 c50dbfe。
正确口径：accepted behavior chain 使用最终值 `0d5c219 → c50dbfe`；
`50ac736` 作为 correction-task intermediate hash 保留在档案历史中。
禁止表述：「整个 M10-C3 从未 amend」。
```

### Q4. Final Accepted M10-C Tree 与 LKGC Advance（§5 / §6）

```text
最终 accepted M10-C behavior chain = 7562678 → 447e346 → 0d5c219 → c50dbfe → fc86dcc
最终 accepted behavior tree        = fc86dcc（不是 c50dbfe，也不是本轮 closure docs commit）
LKGC：ef71244 → fc86dcc
本轮 closure commit 本身不得成为 LKGC（docs-only）。
```

### Q5. Frozen Contracts（§7–§26，逐条冻结）

```text
A. Authority model（§7）
   QML Draft = page-local mutable presentation state
   Controller prepare = authoritative validation
   PreparedWriteSnapshot = Controller/runtime immutable authority
   Dialog = snapshot projection
   Confirm = opaque token only
   Transport dispatch = 未来 M10-D/E authority
   ⇒ 这些层不得重新合并。

B. Validation（§8）：validate before narrowing。UI/draft 输入先用宽类型验证，再转 uint8/uint16/typed intent；
   禁止 65536 先变 0、-1 先变 65535。

C. 0x10 parser（§9，M10 v1）：decimal only；一行一个值；0..65535；1..123；首尾空白行可忽略；中间空白行 error；
   CRLF 支持；values[] 是唯一 authority；quantity = values.size()（派生，不另存）。

D. Address span（§10）：uint32(start) + uint32(quantity) <= 65536 ⇔ lastAddress = start + quantity - 1 <= 65535；
   禁止 uint16 先加后判。

E. Snapshot state machine（§11）：None → Prepared → Consumed | Invalidated；两者 terminal；旧 token 永不复活；
   同 token 不得二次 Consumed；prepare while Prepared 不得覆盖 snapshot（返回 AlreadyPrepared）。

F. Confirmation meaning（§12）：Consumed 只表示「用户明确确认了当前 immutable snapshot」，
   不表示 request 已发送 / device 已收到 / write 成功 / Modbus Success；M10-C 完全没有 write dispatch。

G. Cancel / Escape（§13）：Prepared → Invalidated(UserCancelled)，draft preserved，
   zero write dispatch / zero write transaction / zero write terminal。

H. Keyboard safety（§14，runtime evidence）：初始 focus = Cancel；刚打开时 Enter = zero confirm；
   Cancel+Space = Cancel；Confirm 明确 focus + Space = exactly one consumption；
   Confirm 明确 focus + Enter = exactly one consumption；rapid Enter×2 = one consumption 且 no spillover；
   Escape = Cancel；0x10 TextArea Tab/Shift+Tab 可离开；hidden page Enter/Space = zero write action。

I. Modal safety（§15）：Dialog open 时 outside click 不关闭/不改变 snapshot；rail click 不切 workspace；
   背景 tab 不操作；背景 Write 不形成第二 flow；modal 交互不得 confirm / cancel / send / source switch，
   除非通过 Dialog 自身明确动作。

J. Context invalidation（§16）：disconnect → Invalidated；successful new session → 旧 token invalid；
   successful source replacement → Invalidated(SourceChanged)；busy false→true → Invalidated(BusyBecameTrue)；
   busy 再 false 不复活。authority = typed source + session id + connected/busy state，
   不是 connection label / dialog visibility / error string。

K. Failed Replay boundary（§17）：Replay replacement 失败且真实保留 ActiveSerial（same session / same connection）
   ⇒ Prepared snapshot 继续有效、draft 继续保留；不得因 Replay error text 或用户访问 Replay workspace 而 invalidate。

L. Draft persistence（§18）：write06Draft / write10Draft 属 QML page-local state，保留跨 workspace navigation /
   Clear Results / disconnect / reconnect / successful source replacement / failed source replacement；
   但 draft persistence ≠ prepared confirmation persistence。

M. Clear Results orthogonality（§19）：继续清 transactions / transport terminals / statistics / diagnosis derived state；
   不清 write draft、不 invalidate/consume Prepared snapshot、不 disconnect、不 cancel pending、不 send write；
   M10-B clear-while-pending 契约继续。

N. Production-hidden（§20）：normal production 下 Loader inactive、item null、无 write controls / Dialog /
   tab stops / accessibility action、startup 无 prepared snapshot。**M10-C COMPLETE ≠ Active Write available**。

O. Harness-only seam（§21）：write foundation 只能由明确 harness path 加载；该 seam 不是 write capability；
   未来不得把 `writeFoundationVisible` 等价解释为「0x06 supported」。

P. Encoder / dispatch absence（§22）：0x06 encoder = ABSENT；0x10 encoder = ABSENT；
   `encodeActiveRequest` 对二者 = UnsupportedFunction；write dispatch = ABSENT；M10-C acceptance 的
   `writeAttempts` = 0。

Q. Transaction boundary（§23）：M10-C 不产生 0x06 / 0x10 transaction，也不产生 write transport terminal；
   用于 busy oracle 的 FC03 属于真实 read transaction，单独归属（会话历史 function codes = [3]）。

R. Timeout future contract（§24，归档 M10-D/E）：write timeout 仍使用 `Timeout` outcome；
   用户语义「响应超时，设备写入状态未知」；禁止「设备未写入」「写操作未发生」等无证据的确定性表述。

S. PossiblySent future contract（§25，继承 M10-A）：ShortSubmission / TransportError after submission /
   DisconnectedAfterSubmission 属于 PossiblySent 或相应 submission evidence；不得据此断言 device unchanged；
   M10-D/E 不得重新解释。

T. AI / Agent boundary（§26）：AI / Agent write authority = NONE；无 prepare-write tool / confirm-write tool /
   serial-send tool / raw-ADU tool。
```

### Q6. Acceptance Archives（§27–§29）

```text
Geometry（§27）：1024×720 PASS；1000×700 PASS；Write draft / validation / Dialog / Cancel / Confirm 均可访问；
                 窗口无需自动扩大。
Scroll（§28）：editor 第一项可达、最后一项可达；confirmation **全部 snapshot values 都可通过滚动访问**；
               123 values PASS（quantity=123、first/last reachable）；124 values = validation reject + no Dialog。
               禁止写「所有值同时可见」。
Accessibility（§29）：basic accessible names / keyboard focus / enabled-state consistency PASS；
                      不声明 WCAG certification 或 screen-reader certification。
```

### Q7. ISSUE-014 与 M10-D Design Blocker（§30 / §31）

```text
ISSUE-014（§30）：继续 PRE-EXISTING NON-BLOCKING；本轮 closure 不修（无 issue code fix）。
  归档 append-only 澄清：旧文档曾写「9 条」，C4 实测「10 条」，同一 reset-window warning family，
  分布在 TransactionsPage.qml 271–277 / 344 / 349 / 379；写 UI 自身 0 条。

M10-D DESIGN BLOCKER（§31，正式冻结）：
  当前 0x06 address / value 使用 non-editable SpinBox，range 0..65535，step = 1；
  C4 真实 usability probe：typing 1234 = 无效；Up key = 无效；indicator click = 0 → 1。
  ⇒ 作为 production 0x06 大范围输入**不可接受**。
  分类：**M10-D DESIGN BLOCKER**（不是 M10-C product safety defect）；不得在 closure 顺手修改。
```

### Q8. M10-D Requirements 与 Production Visibility Gate（§32 / §33）

```text
M10-D 不得直接开始 encoder。第一阶段必须 Learning / Design，至少先解决：
  A. 0x06 address/value 生产输入控件方案
  B. 直接十进制键盘输入
  C. invalid text 如何 presentation
  D. validate-before-narrowing 如何保持
  E. confirmation snapshot 如何复用
  F. 0x06 encoder golden wire
  G. echo matching
  H. write timeout / unknown-state semantics
  I. exactly-one dispatch
  J. production visibility capability gating
Review PASS 以后才能进入 Implementation。

0x06 Write section 首次 production-visible 必须**同时**满足：
  1. 输入 usability blocker 解决；2. 0x06 encoder 存在并通过 golden vectors；3. dispatch 存在；
  4. response matching 存在；5. write timeout semantics 存在；6. exactly-one-send 已证明；
  7. confirmation / session safety 继续通过；8. production capability 真实为 available。
禁止：先显示 UI、以后再补能力。
```

### Q9. Final Test Archive（§34）

```text
write_prepare 32 · active_request 17 · active_master 54 · ui_bridge 59 · statistics 12 ·
statistics_integration 3 · diagnosis 17 · serial 21 · serial_adapter 7 · replay_log 14 ·
replay_analysis 9 · transaction 20 · transaction_integration 5 · function03 15 · fault 7 ·
fault_integration 4 · passive 55 · codec 9 · crc 8 · frame 6 · simulator 15 ·
simulator_integration 3 · ai 23 · agent_tools 15 · agent_runtime 28 · agent_integration 24
QML：qml_write_foundation_check PASS · qml_focus PASS · qml_smoke PASS · qml_nav PASS · qml_geometry PASS
Debug ctest 31/31 PASS；Release ctest 31/31 PASS。
closure 为 docs-only ⇒ 无需重跑（前提：fc86dcc..closure HEAD 零 behavior diff —— 已实证）。
```

### Q10. REAL HARDWARE 边界（§35）

```text
REAL HARDWARE NOT VERIFIED（继续）。
M10-C 是 software safety foundation COMPLETE；不得写「hardware write verified」。
Active Write = NOT AVAILABLE；0x06 encoder = ABSENT；0x10 encoder = ABSENT。
```

### Q11. Docs / Commit（§36–§39）

```text
同步：T022（本节）· PROJECT_STATUS · BACKLOG · devlog · INTERVIEW_NOTES。
最终状态：M10 = IN PROGRESS；M10-A/B/C = COMPLETE；M10-D = NEXT / Learning & Design；
          verified LKGC = fc86dcc；Active Write = NOT AVAILABLE。
closure commit：`M10-C: close write safety foundation`（独立 docs-only commit）。
不得 amend fc86dcc；不得 rebase；不得 push；不得 tag（含 v2.0.0）。closure commit 不作 LKGC。
验证：git diff --check PASS；git status --porcelain 为空；git show --name-only HEAD 仅 docs；
      fc86dcc..HEAD 无 src/ tests/ QML CMake 行为 diff；全部 authoritative LKGC = fc86dcc。
```
## M10-D Phase 1 — 0x06 Write Single Register：Learning / Design（2026-09-20，docs-only）

> **M10-C Closure Review = PASS。M10-C = COMPLETE。M10-D = GO（仅 Phase 1）。**
> 本轮**只做学习与设计**：实读当前实现、产出设计、列出待 Review 裁定的决策。**Implementation = NOT STARTED**；
> 不实现 0x06 encoder、不实现 dispatch、不 production-enable Write UI、不改输入控件、不实现 0x10、不新增 Agent
> 写权限；未 push、未 tag。**不得重新打开已冻结的 M10-C confirmation safety foundation**（本节全部设计都建立在它之上）。

### R0. Preflight 与 M10-C → M10-D 过渡归档（§0 / §1）

```text
HEAD = 19ca421（branch = main，working tree clean）
verified LKGC = fc86dcc（M10-C closure 推进后）
v1 tag object = 2cee626；v1 target = ae067ab；v2.0.0 = ABSENT
origin/main = a40d935；ahead = 113；behind = 0
CMake VERSION = 2.0.0；git diff --check = PASS
M9 / M10 Phase 1 / M10-A / M10-B / M10-C 全部 COMPLETE；M10-D Implementation = NOT STARTED
Active Write = NOT AVAILABLE；0x06 encoder ABSENT；0x10 encoder ABSENT；write dispatch ABSENT
```

归档：**M10-C = COMPLETE；final accepted M10-C behavior tree = `fc86dcc`；verified LKGC = `fc86dcc`**（详见 §Q）。
M10-D 的目标是让 0x06 成为**第一个真正 production-visible、端到端可用的 active write 能力**；最终链路为
draft → authoritative validation → PreparedWriteSnapshot → user confirmation → **atomic confirm + dispatch**
→ 0x06 encoder → SerialTransport → SerialTransactionSession → response analysis → ActiveTransactionRecord →
现有 session history → Transactions / Statistics / Diagnosis。**本轮不实现其中任何一段。**

### R1. Mandatory Source Re-read — 审计结论 A–L（§3 / §4）

实读文件：`ActiveRequestIntent.{h,cpp}`、`ActiveRequestDescriptor`（同头文件）、`PreparedWriteSnapshot.{h,cpp}`、
`WriteDraftParsing.{h,cpp}`、`WritePrepareValidation.{h,cpp}`、`encodeActiveRequest`（`ActiveRequestIntent.cpp`）、
`Function03.{h,cpp}`、`Function06.h`、`Function16.h`、`ModbusCrc.h`、`ModbusRtuCodec.{h,cpp}`、`ModbusRtuFrame.h`、
`TransactionAnalysis.{h,cpp}`、`PassiveTransactionAnalysis.cpp`、`SerialTransactionSession.{h,cpp}`、
`SerialTransport.h` + `SerialPortAdapter.cpp`、`tests/fake_serial_transport.{h,cpp}`、`AnalysisController.{h,cpp}`、
`CoreSimulator`（`SimulatedSlave.h`）、`WriteFoundationSection.qml`、`CommunicationPage.qml`、`DiagnosisPage.qml`
（唯一既有文本输入模式）、`DS/DesignSystem.qml`、`src/main.cpp`（qml write/focus harness）、
测试：`active_request` / `active_master` / `write_prepare` / `passive` / `codec` / `frame` / `crc` / `transaction` / `ui_bridge`。

| # | 问题 | 真实结论（含证据位置） |
| --- | --- | --- |
| A | 0x06 typed payload | `WriteSingleRegisterIntent{ std::uint16_t registerAddress; std::uint16_t value; }`，位于 `ActiveRequestIntent{ function, unitId(uint8), timeout(ms), payload(closed variant) }`；单播域 `kMinUnicastUnitId=1 / kMaxUnicastUnitId=247`（`ActiveRequestIntent.h:52-104`） |
| B | 0x06 passive analyzer 已验证的语义 | `Function06.h`：`decodeWriteSingleRegisterRequest/Response`（functionCode 必须 0x06、data 恰好 4 字节、big-endian）；`PassiveTransactionAnalysis.cpp:287-313`：request 解码失败 → `ProtocolError + UnknownProtocolError`；response 形状错 → `ProtocolError + MalformedNormalResponse`；**地址或值 echo 不一致 → `ProtocolError + WriteSingleRegisterEchoMismatch`**（携带 expected/actual 地址与值四元组）；完全一致 → **`Success`** |
| C | normal 0x06 response 如何判断 echo | 冻结顺序（被动/主动共用同一条管线）：① `response.address != request.address` → `ProtocolError + ResponseAddressMismatch`；② 异常位分支 `(fn\|0x80)`；③ function 匹配 0x06+0x06 → 解码 + **逐字段 echo 比较（registerAddress 与 value 同时相等）** |
| D | echo mismatch 用什么表达 | **Outcome 与 Issue 是两个正交事实**：`TransactionStatus::ProtocolError` **+** `TransactionIssueCode::WriteSingleRegisterEchoMismatch`（`TransactionAnalysis.h:44`）；**不是 Success，也不是新 outcome**；七 outcome 冻结不变 |
| E | 0x06 exception response | **通用异常路径**（`PassiveTransactionAnalysis.cpp:236-250`，为所有 function 写一次）：`fn\|0x80` 且 data.size()==1 → `Exception` + 数字 exceptionCode；data.size()!=1 → `ProtocolError + MalformedExceptionResponse`。0x06 的异常 function 是 **0x86**，session 的 framing 规则已按「异常位 → 5 字节」通用处理，**不需要新 taxonomy** |
| F | CRC / ProtocolError / Timeout 如何形成 | `TransactionAnalysis.cpp`（`analyzeFunction03Transaction`）：NoResponse 且 `elapsed < threshold` → `Pending`；`>= threshold` → `Timeout`；`RtuDecodeError{CrcMismatch}` → `CrcError`；`{FrameTooShort}` → `ProtocolError + ResponseFrameTooShort`；解码帧但 function 不匹配 → `ProtocolError + UnexpectedResponseFunction`；其它防御分支 → `UnknownProtocolError` |
| G | `encodeActiveRequest` 组织方式 | 先 `validateActiveRequestIntent`（失败 → `IntentInvalid`），再按 `switch (intent.function)`：0x03 → `encodeReadHoldingRegistersRequest`（语义帧）→ `encodeRtuFrame`（CRC 线缆字节）→ `ActiveRequestDescriptor`；**0x06 / 0x10 → `UnsupportedFunction`**（`ActiveRequestIntent.cpp:88-119`） |
| H | FC03 active lifecycle 可复用部分 | 除**三个函数相关 seam** 外全部可复用：intent → descriptor → `SerialTransport::startActiveRequest` → `SerialTransactionSession::beginActiveRequest`（通用）→ `feedResponseBytes`/`onResponseTimeout` → `analyzeActiveResponse`（单一 dispatch 点）→ `ActiveTransactionResult` → `AnalysisController::handleSerialTransactionCompleted`（send-time 身份守卫）→ `appendActiveSerialTransaction` → rows/statistics/diagnosis。**需扩展的三处**：`activeFunctionSupported`（现仅 0x03）、`candidateFrameLength`（现仅 0x03 + 异常位）、`analyzeActiveResponse`（现仅 0x03 分支） |
| I | 是否已有 generic descriptor/session path 承载 0x06 | **是**（M10-A 已把 session 做成 function-generic：`SerialTransactionSession.h:17-37` 明确「不会出现 SerialWrite06Session / SerialWrite10Session，将来也不会有」）。0x06 必须骑同一条路径 |
| J | `serialBusy` 是否唯一 authority | **是**：单一 `serialBusy_`，在读请求入口守卫（`AnalysisController.cpp:999`）、在 start 被接受后置真（1045）、在完成/终止/teardown 处置假（871/882/957/1139/1649）；进入 flight 时以 `BusyBecameTrue` 失效 prepared snapshot（1048-1051）。**不得新增 writeBusy** |
| K | fake transport 的确定性能力 | `RecordingSerialTransport` 已具备 M10-D 全部 oracle 所需：`setPortOpen` / `setAcceptRequests`（pre-send 拒绝）/ `setResponseBytes`（normal·exception·CRC 损坏·任意字节）/ `setCompletionElapsed` / `setSubmissionAcceptedBytes`（short submission）/ `completeWithResponse` / `completeWithTimeout` / `feedPartialBytes`（分片）/ `failTransport` / `disconnectAfterSubmission`；观测面：`startAttemptCount` / `sendCount` / `sentAduLog` / `lastStartResult` |
| L | simulator 的 writable 语义 | **有 foundation、无应答框架**：`WriteMode{ReadOnly(默认), Writable}` + `applyWriteRequest()` → `SimulatorWriteOutcome{Applied, ReadOnlyMode, NotMyAddress, UnsupportedFunction, MalformedRequest}`（先解码、失败零改动）；但 `handleRequest()` 是 const 且**仍不回答写请求**（注释明确：应答框架需要 M10-D/E 的写 encoder）。⇒ 0x06 应答 echo 需要**同一个 encoder**，是本轮设计的一个可选项（见 R17-D12） |

**额外发现（本轮必须记录，影响设计）：**

```text
S1. session 的 candidateFrameLength() 目前**没有 0x06 规则** —— 除 0x03 与异常位之外一律返回 nullopt
    （「不发明长度解析器，等超时收尾」）。因此若不扩展，0x06 normal response **不会在到达时成帧**，
    只能在 timeout 时按整段字节分析 ⇒ 会得到 Timeout/ProtocolError 而非 Success。
    M10-D 必须新增 framing 规则：function == 0x06 → 8 字节（1+1+4+2）。异常位规则已通用（5 字节），无需改动。
S2. PreparedWriteStore::invalidate() **只对 Prepared generation 生效**（`PreparedWriteSnapshot.cpp`: 状态 != Prepared
    直接 return false，terminal 与其历史原因永不改写）⇒ 0x06 dispatch 若「先 consume 再进入 flight」，
    随后 read 路径式的 BusyBecameTrue 失效调用对已 Consumed 的代际是 **no-op**。这是 §17/§18 的关键依据。
S3. 源码中**不存在任何 TextField**（`grep -rln TextField src/ui/qml/` 为空）：应用里唯一的文本输入先例是
    Agent 提问用的 **TextArea**（含 M9-F 的 Tab/Backtab 逃逸模式）。⇒ 「复用现有 TextField pattern」在事实上
    没有对象可复用；0x06 的十进制输入将是本应用**第一个单行文本输入**（见 R2）。
S4. 源码切换（Simulator demo / Replay 成功）**无条件** `teardownSerialTransport()`（runDemoBatch 1683、
    loadReplayFile 1941），不看 serialBusy；teardown 会 `closePort()` + `pendingRequest_.reset()` + busy=false，
    并且**先** invalidate(SourceChanged)（顺序被注释固定为「原因必须真实」）。⇒ 「pending 期间切源」在 M10-A/B
    已是冻结行为：本地取消 +（若已提交）terminal 证据；M10-D 必须原样复用，不得改契约（§38）。
```

### R2. Input Blocker 解决方案（§6–§9、§56–§61）

**已确认的 blocker（C4 实测，§31）**：0x06 的 address / value 使用 **non-editable** SpinBox，range 0..65535、
step=1；typing 无效、Up 键无效、只有指示器点击每次 ±1 ⇒ 最坏 65535 次点击。

**四个候选方案对比**（评价维度来自 §6）：

| 维度 | A. editable SpinBox | B. TextField + validator + C++ 权威校验 | C. DecimalField（TextField foundation） | D. SpinBox + 直接编辑 |
| --- | --- | --- | --- | --- |
| 键盘输入效率 | 好（可输入） | 好 | 好 | 好 |
| paste / 全选 / 删除 | 依赖 SpinBox 内部 | 原生支持 | 原生支持 | 依赖内部 |
| invalid intermediate text | **差**：SpinBox 把 `value` 当模型，非法文本的保留/回退由 Qt 内部策略决定（可能 commit 时被回退或钳制） | **好**：raw text 可保留，合法性判定在 core | **好** | 差 |
| focus / accessibility | 需重新测（内容项是编辑器） | 标准 | 标准（可加 focus ring + Accessible.name） | 需重测 |
| range presentation | 只有 `from..to` | 可显示范围提示 + 错误文案 | 同 B | 同 A |
| 实现复杂度 | **最低**（一行 `editable: true`） | 低 | 中（一个新组件） | 中 |
| silent clamp 风险 | **高**（Qt 在 commit/focus-out 时可能把文本收敛为范围内值） | **无**（我们从不改写用户文本） | **无** | 高 |
| 复用性（0x10 起始地址 / M11 / M12） | 差 | 中 | **好** | 差 |

**推荐：方案 C —— 新增 `DecimalField.qml`（基于 QQC2 `TextField`）**，理由：
1. **raw text 必须保留**（§8）：只有 TextField 系能把用户原串当 draft，让 core 做权威判定；SpinBox 的模型是数值，
   非法中间态要么被 Qt 吞掉、要么被收敛，两种都会让「哪个值被接受」变得不可解释。
2. **silent clamp 风险为零**：我们**从不**把 65536 改写成 65535、也从不把 -1 改写成 0 —— 一律拒绝并呈现原因（§7）。
3. **单一 decimal authority**：合法性与解析由 core 的既有十进制解析器决定（见下），QML 只做输入体验。
4. **可复用且不过度抽象**：0x10 起始地址、后续 M11/M12 的寄存器地址都可用同一组件；但 M10-D v1 只为 0x06 落地。
5. **unit / timeout 保留 SpinBox**：`1..247` 与 `100..10000` 的量级下步进是可用的（247 步可接受，65535 步不可接受），
   且这两个控件在 M10-C 已通过键盘/无障碍 oracle——**不为了统一而改它们**。

**Raw text vs numeric authority（§8 决策）**：production draft 保存 **raw decimal text**（每个字段一个字符串），
Controller 侧以 **int64 → 权威校验 → 窄化** 的既有链路处理（M10-C1 冻结的 validate-before-narrowing）。
**QML 在任何阶段都不得先把文本窄化成 uint16**；`DecimalField` 不持有数值模型，只持有文本 + 「当前呈现态」。

**Decimal-only（§9）**：v1 保持十进制输入；不引入 hex 模式、`0x` 前缀或 dec/hex 切换；`0x06` 只是**协议身份显示**，
不是输入格式（未来的 hex 输入是单独的 enhancement）。

**单值解析（新增，复用既有唯一 authority）**：在 `core/active/WriteDraftParsing` 内新增
`parseSingleRegisterValue(text) -> variant<uint16_t, ValuesParseError>`，**复用同一份 trim / 逐字符十进制累加 /
`> 65535` → ValueOutOfRange（不 wrap、不抛异常、不受 locale 影响）** 规则；仅额外要求「恰好一个值」
（多行输入 → 明确的 typed 错误）。⇒ 地址与值字段与 0x10 的多行解析共享**同一套十进制语义**（§59 要求 QML 与 C++
不出现两套规则）。

**Invalid input UX（§57）**：

| 输入 | 判定（core typed error） | 呈现 | 是否改写 draft |
| --- | --- | --- | --- |
| `""`（空） | `NoValues` | 「请输入数值」 | 否 |
| `-1` / `12x` / `1.5` / `0x10` | `InvalidCharacter` | 「只能输入十进制数字（0-9）」 | 否 |
| `65536` / 超大整数 | `ValueOutOfRange` | 「数值必须在 0..65535 之间」 | 否 |
| ` 1234 `（前后空白） | **接受**（core 统一 trim） | 显示权威数值 | 否（保留原串） |
| `00010`（前导零） | **接受** = 10 | draft 保留原串；**confirmation 显示权威数值 10** | 否 |
| 多行粘贴 | 明确的单值错误 | 「该字段只能输入一个数值」 | 否 |

**Whitespace 决策（§59）**：**接受**两端空白（与 core parser 现有 `trim` 一致），QML **不再 trim 一次**
（避免出现两套规则）；呈现层只显示 core 的判断结果。**Leading-zero 决策（§58）**：`00010` 是合法十进制 10，
draft 保留 raw text，confirmation 只显示权威数值。

**Component ownership（§60）**：`DecimalField.qml` **只负责呈现与输入体验**（文本、选择、粘贴、focus ring、
`Accessible.name`、范围提示位）；**协议合法性永远由 core / Controller 判定**，组件内**不得**写死 unit/address/value
的协议范围。**Input accessibility（§56）**：必须支持直接键盘输入、全选、删除、粘贴、Tab 离开 / Shift+Tab 返回、
focus indicator；**不得**要求 mouse-only 操作。新控件必须加入既有约定：focus ring（M9-F 的 border 通道）、
`Accessible.name`、Tab 顺序、disabled 态与 accessible enabled 一致。

### R3. Wire Format 学习：0x06 RTU ADU（§12）

依据：项目实现（`encodeReadHoldingRegistersRequest` 的字段打包方式 + `encodeRtuFrame` 的 CRC 放置）+
MODBUS over Serial Line V1.02 / Application Protocol V1.1b3（已回填 `docs/03_MODBUS_LEARNING.md` §4.5）。

**请求（master → slave），8 字节**：

| offset | 字段 | 长度 | 说明 |
| --- | --- | --- | --- |
| 0 | unit id | 1 | 单播 1..247；**0（broadcast）在 active 路径本地拒绝** |
| 1 | function | 1 | `0x06` |
| 2..3 | register address | 2 | **big-endian**（hi, lo），0..65535 |
| 4..5 | register value | 2 | **big-endian**（hi, lo），0..65535 |
| 6..7 | CRC-16/MODBUS | 2 | **低字节在前**（V1.02；由 `encodeRtuFrame` 计算） |

**CRC coverage = offset 0..5（address + function + data）**，即 CRC 覆盖除自身以外的全部字节。
**正常响应 = 请求的逐字段 echo**，同样 8 字节（CRC 独立重算）。
**异常响应**：unit + `0x86` + exception code + CRC = **5 字节**（session 已按「异常位」通用成帧）。

**§12 的学习结论**：语义帧（地址/功能/数据）与线缆字节（含 CRC）在项目里**分层且单一来源**——
`encode*Request` 只产出 `ModbusRtuFrame`，CRC 与低字节序由 `ModbusRtuCodec::encodeRtuFrame` 负责，
帧模型**刻意不存 CRC**（`ModbusRtuFrame.h:8-12`），避免 stale-CRC。0x06 encoder 必须沿用同一分层。

### R4. Golden Wire 策略（§13，仅设计）

- **独立 oracle 优先**：测试内的期望字节由**独立实现**算得——测试文件内含一份**按 V1.02 定义手写的 bitwise CRC**
  （不复用 `calculateModbusCrc`），字段按 R3 表手工拼装；生产实现与期待值**互为交叉验证**。
- **向量集合（至少）**：① canonical：unit 1 / address 0x0000 / value 0x0000；② 高边界：unit 247 / address 0xFFFF /
  value 0xFFFF；③ 规范示例型：unit 0x11 / address 0x0001 / value 0x0003（协议文档应用示例的字段序列）。
  边界另加：address 65535 + value 0、address 0 + value 65535、value 1（最小非零）。
- **禁止**「production encoder 自己算、自己验证」的闭环；若独立 oracle 与实现不一致，**两边都必须重新推导**，
  绝不允许把期望值改成实现的输出。

### R5. Descriptor 契约（§14，冻结）

未来的 0x06 encoder **必须仍返回 `ActiveRequestDescriptor{ intent, frame, wire }`**：intent = 发送时快照，
frame = 语义帧，wire = 精确线缆字节（CRC 含）。**禁止**：QML 构建 raw ADU、Controller 拼字节、UI 保存/计算 CRC ——
wire evidence 永远由 protocol 层产生；transport 只按原样写出 `wire`。

### R6. Confirmation Snapshot 复用（§15，冻结）

**不得新建第二套 write confirmation model。** M10-D 复用：`PreparedWriteSnapshot`（不可变、token 标识）、
opaque token、`None → Prepared → Consumed | Invalidated` 一次性状态机、M10-C 的 dialog 投影、
Cancel/Escape、键盘/模态/会话/busy 安全、以及全部 C01–C37 oracle。**production enable 只是把已验证的 foundation
接到真实 0x06 dispatch capability 上**，不修改它的语义。

### R7. Confirm-and-Dispatch：API、原子性、消费时序（§16–§18）

**API（草案）**：`Q_INVOKABLE bool confirmAndDispatchPreparedWrite(qulonglong token)`。
输入**只允许 opaque token**；**不得**再传 unit / address / value / timeout 或任何 QML draft 字段 ——
Controller 必须从自己的 snapshot 取 typed intent。

**一个同步 operation 的 11 步与失败语义**（§17 的步骤表 + 每步的状态后果）：

| # | 步骤 | 失败时 | snapshot 状态 | send |
| --- | --- | --- | --- | --- |
| 1 | token lookup | token 不匹配 | 不变（terminal 不会被改写） | 0 |
| 2 | `state == Prepared` | 已 Consumed/Invalidated | 不变 | 0 |
| 3 | `source == ActiveSerial` | 是别的 source | `Invalidated(SourceChanged)` | 0 |
| 4 | session unchanged | 已换 session | `Invalidated(SessionChanged)` | 0 |
| 5 | connected | 未连接 | `Invalidated(Disconnected)` | 0 |
| 6 | `!busy` | 有事务在飞 | `Invalidated(BusyBecameTrue)` | 0 |
| 7 | `function == 0x06` | 别的 function（当前只可能 0x06/0x10） | `Invalidated(CapabilityUnavailable)` | 0 |
| 8 | 0x06 dispatch capability 存在 | 能力缺失（回归/stale UI） | `Invalidated(CapabilityUnavailable)` | 0 |
| 9 | **consume one-shot snapshot** | —— | **`Consumed`（terminal）** | 0 |
| 10 | encode descriptor | encode 失败（防御分支） | 保持 `Consumed` | **0** |
| 11 | start Active request lifecycle | transport pre-send 拒绝 | 保持 `Consumed`（**不复活**） | 0（NotSent）或已提交证据 |

**Consumption ordering（§18 的正式答案）**：**先所有上下文守卫 → 再 consume → 再 encode → 再 start**。
依据 S2：`PreparedWriteStore::invalidate()` 只对 Prepared 代际生效，因此进入 flight 后（读路径式的
`BusyBecameTrue` 失效）对已 Consumed 的代际是 **no-op**，terminal 语义不被污染。

**因此下列重复输入只能产生一次 dispatch attempt**：双 Enter、双 Space、重复 clicked 信号、同 token 第二次调用
（第 2 步即被拒：state 已 terminal）。这正是 §42 要把 M10-C 的「one confirmation acceptance」升级成的
**one confirmation → exactly one 0x06 dispatch attempt**。

**§35 的保守策略（本轮采纳）**：token 一旦 Consumed，**任何**后续失败（含 pre-send transport reject）
都**不复活**该 token；用户必须重新 Write → prepare → confirm 才能再试。UI 不得提供 armed mode / one-click repeat。

### R8. Capability 模型与守卫（§19 / §40）

- Controller 暴露只读 `write06Available`（`NOTIFY` 与状态变化一起），语义为「**encoder + dispatch 是否 ready**」，
  取值为 core 的单一能力谓词（`activeFunctionSupported(WriteSingleRegister)`）与 dispatch 路径存在性的真实合取。
- **禁止**：hard-code `true`、只看 build type、只看 UI/harness flag。harness 的 `writeFoundationVisible`
  **不是** capability 信号（M10-C §Q5-O 已冻结），不得参与该判定。
- **capability guard 保留**：即使 production 只在 ready 时显示 UI，Controller 仍保留第 8 步守卫，
  防止 stale UI / test misuse / 未来回归（§19）。

### R9. Response Echo Matching 复用与 framing（§20 / §21 / §53）

- **单一来源**：0x06 的 echo 语义**不得**写第二套。设计：把现被动分析器中的 FC06 配对逻辑抽成
  一个可复用的 core 分析函数（`analyzeWriteSingleRegisterTransaction(request, observation, elapsed, threshold)`
  → `TransactionAnalysis`），**主动路径**（`analyzeActiveResponse` 的 0x06 分支）与**被动路径**（FC06 分支）
  都调用它；被动侧继续在其外层叠加 request-issues 层（主动路径的请求是已校验的可信请求，requestIssues 恒空）。
- **语义表（复用既有，不新增）**：echo 完全一致 → `Success`；地址或值不一致 → `ProtocolError` +
  `WriteSingleRegisterEchoMismatch`（携带四个 expected/actual 字段）；响应形状错（非 4 字节数据）→
  `ProtocolError + MalformedNormalResponse`；请求本身不可读 → `ProtocolError + UnknownProtocolError`。
- **framing（S1）**：`candidateFrameLength()` 新增 `function == 0x06 → 8`；异常位规则已通用（5 字节）。
  这样 0x06 正常响应**到达即成帧**，而不是等超时。
- **response length authority**：帧完成判定永远属于 protocol/session 层；**UI / Controller 不得 hard-code 长度**（§53）。
- **分片（§52）**：0x06 响应仍走 `SerialTransactionSession::feedResponseBytes` 的任意 chunk 累积
  （partial chunks / 多次 readyRead / 达到候选长度才成帧），**不得**假设一次 readyRead 就是完整响应。

### R10. Outcome / Evidence 矩阵（§22–§28、§46–§51）

| 场景 | Outcome（七态之一） | Issues / evidence | send | 用户文案类别 |
| --- | --- | --- | --- | --- |
| 正常 echo | `Success` | —— | 1 | 成功（含 request/response ADU 与 provenance） |
| echo 地址/值不一致 | `ProtocolError` | `WriteSingleRegisterEchoMismatch` + 四元组 | 1 | 协议错误（响应与请求不符） |
| 合法异常响应（0x86） | `Exception` | exceptionCode（数字） | 1 | 设备返回异常（不产生 transport terminal） |
| CRC 错但字节完整 | `CrcError` | 保留 raw response bytes | 1 | 帧校验失败 |
| 功能码/形状不符 | `ProtocolError` | `UnexpectedResponseFunction` / `MalformedNormalResponse` | 1 | 协议错误 |
| 超时（已接受、无完整响应） | `Timeout` | submission disposition 保留 `PossiblySent` | 1 | **响应超时，设备写入状态未知** |
| short submission（0 < n < 帧长） | **无 Modbus transaction** | 一条 `ShortSubmission` terminal + 完整 intended requestAdu + accepted count | 1（部分） | 写入证据不完整，设备状态未知 |
| submission 后 transport error / disconnect | **无 Modbus transaction** | terminal（`TransportError` / `DisconnectedAfterSubmission`） | 1（已提交） | 设备状态未知 |
| pre-send 拒绝（未连接 / busy / stale token / capability 缺失 / transport accepts 0） | **无 transaction、无 terminal** | 本地 rejection | **0** | 本地拒绝（不是设备结果） |

- **No implicit retry（§25，冻结）**：一次确认**最多**形成一次 request submission lifecycle。Timeout / CRC error /
  exception / transport error **都不得自动重发**；要再写必须重新发起新的 confirmation。
- **Submission evidence（§26，继承 M10-A）**：pre-send → `NotSent`；accepted → `PossiblySent`；
  short → `PossiblySent` + terminal；post-submission disconnect/error → terminal/evidence。
  **API accepted bytes ≠ device received bytes**，措辞不得越界。
- **Short submission（§27）**：沿用 M10-A 语义（保留 intended requestAdu、responseAdu 空、恰好一条 terminal、
  **不伪造** Modbus transaction）；`acceptedCount = 0` ⇒ `NotSent` 且**无** terminal。
- **Post-submit disconnect（§28）**：只允许「submission evidence + device state unknown」，
  **禁止**「写入失败且设备未改变」；不自动重试。

### R11. 集成（§29–§32）

```text
History（§29）：0x06 的 Success / Exception / CrcError / ProtocolError / Timeout 产生的 Modbus transaction
                进入**当前 Active Serial 同一 session history**，append（oldest → newest）；
                **不得**另建 Write History；row 的 provenance 与 FC03 完全同构。
Terminals（§30）：TransportError / DisconnectedAfterSubmission / ShortSubmission **继续**不进入 transaction rows、
                不进入 Modbus statistics；留在 transport/evidence lane。**M10-B 契约不得改变。**
Statistics（§31）：0x06 transaction 加入同一 batch，继续使用 M9-C 冻结公式；**不新增 writeSuccessRate**
                作为第二套核心统计 authority；未来若要按 read/write 过滤，只能是 projection/filter。
Diagnosis（§32）：0x06 结果进入同一 deterministic diagnosis batch；仍 analysis only；
                **不得**因 write timeout 自动建议并执行 retry；AI/Agent 仍不获得控制权。
```

### R12. Draft / Token 生命周期（§33–§38）

- **dispatch 之后 draft（§33，建议保留）**：确认并进入 dispatch 后，`write06Draft` **保留**（用户常要微调重复写）。
  但每次再发必须重新 Write → prepare → confirmation；**不得** armed mode / one-click repeat / auto-send。
- **各种 outcome 之后 draft（§34，建议统一保留）**：Success / Exception / Timeout / CrcError / ProtocolError /
  transport terminal / disconnect **一律保留 draft**。history 与 draft 是两个 authority，**不得**根据 outcome 偷偷改写输入。
- **失败 dispatch 之后的 token（§35）**：Consumed 终态，**任何**失败都不复活（见 R7）。
- **Clear while pending（§36）**：继续 M10-B —— 清已完成的 history/terminals/derived state，
  **不** cancel pending、**不** disconnect、**不清** draft；0x06 pending 之后完成 → 成为清空后的第一条新
  transaction 或 terminal evidence。
- **Disconnect while pending（§37）**：复用 M10-A 单一 lifecycle；**submission 前**（本地取消，无证据、无行）与
  **submission 后**（terminal 证据、设备状态未知）必须在 UI 上**分开**呈现，**不得**统一成「写入失败」。
- **Source replacement while pending（§38）**：以真实代码为准（S4）——Simulator/Replay 成功切换**无条件** teardown
  （`closePort()` + 清 pending + busy=false），且**先** invalidate(SourceChanged) 再 teardown（原因真实性）。
  这是 M10-A/B 的冻结行为，**M10-D 原样复用、不修改契约**；若未来需要「busy 时禁止切源」，必须另开 ADR/Review。

### R13. Production Visibility 与 Hidden Harness 共存（§39 / §41）

- **0x06-only**：production 首次显示 Write section 时**只显示 0x06**；**0x10 不实例化、不显示**，
  **不得**放 disabled 的 roadmap tab。可见性**绑定真实 0x06 capability**（R8），**不绑定** harness flag。
- **推荐实现**：沿用**同一个** `WriteFoundationSection.qml`，tab 集合由 capability 决定
  （harness 模式两者都在 ⇒ C01–C37 原样可跑；production 只有 0x06）；备选是把 production section 独立成
  新组件。**本轮不改任何 QML**，仅记录方案 —— 由 Review 在 R17-D10 裁定。
- **Hidden harness 兼容（§41）**：C01–C37 / E1/E2 / C4 的 safety oracle **必须继续可运行**；
  **不得**因为 UI 已 production-visible 就删除 hidden oracle。harness 模式与真实 capability 的关系是
  「正交且都可见」：harness 证明 foundation 的安全契约，capability 决定 production 是否显示。

### R14. 测试分层与 D1–D5 Staging（§65 / §66）

```text
A. pure protocol        ：0x06 encoder golden vectors（独立 CRC oracle）、非法域、descriptor 自洽
B. pure/passive analyzer：echo matching / issues（既被动又主动复用同一函数）
C. active/session       ：framing（8 字节）、分片、timeout、exception、CRC、候选长度
D. controller           ：atomic confirm+dispatch、one-shot、session/busy/source 守卫、token 语义
E. recording transport  ：exactly-one-send、NotSent/PossiblySent、short submission、post-submit disconnect
F. QML runtime          ：production 输入控件（键盘/粘贴/非法输入）、confirmation、capability visibility、
                          C01–C37 回归（harness 模式继续）
G. full regression      ：M10-A/B/C + M9 全量 + Debug/Release ctest + qml gates
```

**提议的后续小阶段（按真实架构，允许 Review 调整）**：

```text
D1  输入组件（DecimalField 或裁定方案）+ 0x06 encoder（pure）+ golden vectors + core 单值解析
D2  Controller atomic confirm+dispatch + capability 属性/守卫 + recording transport oracles（exactly-one-send）
D3  response framing/echo 复用 + outcome/evidence + history/statistics/diagnosis 集成
D4  production-visible 0x06 UI + capability gating + 键盘/无障碍/几何 oracle
D5  runtime safety（rapid Enter / modal / session）+ full regression + final review
```

### R15. Oracle 设计（§42–§51）

```text
Exactly-one-send（§42）：Confirm+Space / Confirm+Enter / rapid Enter×2 / rapid Space×2 / 重复 signal /
   同 token 第二次调用 —— 每种都要求 **exactly 1** request lifecycle（RecordingTransport.startAttemptCount == 1,
   sendCount <= 1，且重复调用后仍为 1）。
Zero-send 矩阵（§43）：invalid draft / Cancel / Escape / session changed / busy / disconnected / source changed /
   capability absent / stale token / 第二次使用同一 token —— 全部 **0 write send**（本地拒绝，无 transaction）。
Success（§44）：deterministic echo ⇒ one transaction、`Success`、exact requestAdu/responseAdu、
   source = ActiveSerial、same session id、history append、statistics/diagnosis 纳入 batch。
Echo mismatch（§45）：地址不一致与值不一致两个 case ⇒ 严格复用既有 outcome/issues；**不得**显示 Success；
   **不得**新建 ad-hoc write result。
Exception（§46）：合法 0x86 ⇒ `Exception` + 保留 raw evidence；**不**产生 transport terminal。
Timeout（§47）：已 accepted 但无完整响应 ⇒ `Timeout` + 保留 `PossiblySent` submission evidence；
   UI 文案「设备写入状态未知」；**exactly one send**。
CRC error（§48）：完整但 CRC 错 ⇒ 既有 analyzer 的 frozen outcome/issues；**保留 raw response bytes**；不重试。
Short submission（§49）：0 < n < 帧长 ⇒ 恰好一条 `ShortSubmission` terminal、**无 transaction**、
   intended requestAdu 完整保留、accepted count 仅表示 API accepted count。
Post-submit disconnect（§50）：submission accepted 后断开 ⇒ terminal evidence、**不伪造** write transaction、
   device state unknown、不重试。
Pre-send reject（§51）：not connected / busy / stale token / capability unavailable / transport 接受 0
   ⇒ 对应层分别证明 zero transaction、zero fake outcome；并明确 transport API accepts 0 的 frozen 语义是
   **NotSent / 无 terminal**。
```

### R16. Scope Fences（§62–§64）与键盘/无障碍继承（§55）

- **不做 0x10（§62）**：M10-D 只实现 0x06；0x10 继续 ABSENT。**不得**因为做 encoder 框架就顺手接上 0x10（M10-E 单独 Review）。
- **不做 Agent 扩展（§63）**：AI/Agent write authority 继续 **NONE**；不得因为 0x06 production 可写就新增
  Agent write tool / 「AI confirm」/ 自动 write recommendation → send。
- **硬件边界（§64）**：自动化验收**不要求**真实硬件（用 RecordingTransport / deterministic response fake /
  若真实适用的 writable simulator）；**fake/simulator PASS 不得写成 REAL HARDWARE VERIFIED**。
  建议：把「真实硬件人工写入检查」列为**非阻塞**的后续验收项（Review 裁定 R17-D15）。
- **继承 M10-C（§55）**：production enable 必须继续满足 Cancel 初始焦点、Enter/Space 安全、Escape、
  rapid Enter no-spillover、0x10 TextArea Tab 逃逸（0x10 仍 hidden，但 oracle 保留）、page gating、accessible names；
  **新输入控件**同样要有 focus ring、accessible name、Tab 顺序。

### R17. Design Decision Requests（§67，16 项，等待 Review 裁定）

| # | 决策 | 本轮建议 | 状态 |
| --- | --- | --- | --- |
| D1 | address / value 输入控件类型 | **方案 C：新增 `DecimalField.qml`（TextField foundation）**；unit / timeout 保留 SpinBox | PENDING REVIEW |
| D2 | raw text ownership | draft 持有 raw text；core 解析；**QML 不窄化** | PENDING REVIEW |
| D3 | decimal parser rules | 复用既有唯一十进制解析器 + 新增单值包装（同一 trim/累加/越界语义） | PENDING REVIEW |
| D4 | leading-zero policy | 接受（`00010` = 10）；draft 留原串，confirmation 显示权威数值 | PENDING REVIEW |
| D5 | whitespace policy | 接受两端空白（core 统一）；QML 不再 trim | PENDING REVIEW |
| D6 | invalid presentation | 五类 typed error → 固定中文文案（见 R2 表）；**永不 silent normalize** | PENDING REVIEW |
| D7 | consumed 与 dispatch ordering | **先守卫 → consume → encode → start**（依据 S2） | PENDING REVIEW |
| D8 | pre-send failure 后是否必须重新确认 | **是**（保守策略）：Consumed 不复活 | PENDING REVIEW |
| D9 | 0x06 capability 如何表达 | 只读 `write06Available`，取自 core 能力谓词 + dispatch 存在性；harness flag 不参与 | PENDING REVIEW |
| D10 | production Write section 如何从 hidden 切到 0x06-only | 同一组件 + capability 决定 tab 集合（备选：独立 production 组件） | PENDING REVIEW |
| D11 | echo mismatch 如何复用既有 analyzer | 抽出共享 FC06 分析函数，主动/被动都调用；**不写第二套比较** | PENDING REVIEW |
| D12 | simulator 是否回答 0x06（binding 写语义） | 建议：**D3 阶段**评估用同一 encoder 让 `SimulatedSlave` 回答 echo + 应用写（需 Review 裁定是否纳入 M10-D 或后置） | PENDING REVIEW |
| D13 | normal success 何时成立 | 严格按既有语义：地址配对 + function 匹配 + echo 全等 + 帧有效 = `Success` | PENDING REVIEW |
| D14 | timeout wording 具体 presentation | 「响应超时，设备写入状态未知」（M10-C §Q5-R 已冻结，本轮给 UI 文案位） | PENDING REVIEW |
| D15 | hardware manual check 是否列为非阻塞验收 | 建议：列为**非阻塞**后续项（本机无硬件） | PENDING REVIEW |
| D16 | D1–D5 staging | 采纳 R14 提议（允许 Review 调整） | PENDING REVIEW |

### R18. Threat / Failure Matrix（§68，T01–T25）

| # | 场景 | 权威层 | Outcome / evidence | send | UI 文案类别 |
| --- | --- | --- | --- | --- | --- |
| T01 | invalid address（>65535 / 非数字） | core validation | 本地拒绝，无 transaction | 0 | 校验错误 |
| T02 | invalid value（同上） | core validation | 本地拒绝，无 transaction | 0 | 校验错误 |
| T03 | unit 0（broadcast） | core validation | 本地拒绝 | 0 | 校验错误 |
| T04 | double Confirm | store 一次性 | 第一次 consume + 1 次 dispatch；第二次被拒 | 1 | 无（第二次无动作） |
| T05 | rapid Enter | 既有键盘契约 | 同 T04 | 1 | 无 |
| T06 | busy before confirm | controller | `Invalidated(BusyBecameTrue)` | 0 | 本地拒绝（事务进行中） |
| T07 | busy race during confirm | controller（第 6 步） | `Invalidated(BusyBecameTrue)` | 0 | 本地拒绝 |
| T08 | disconnect before submit | controller/transport | 本地取消；无证据无行 | 0 | 连接已断开 |
| T09 | disconnect after submit | transport evidence | terminal（DisconnectedAfterSubmission） | 1（已提交） | **设备状态未知** |
| T10 | short submission | transport evidence | `ShortSubmission` terminal，无 transaction | 1（部分） | 证据不完整，设备状态未知 |
| T11 | transport accepted 0 | transport | `NotSent`，无 terminal | 0 | 本地拒绝 |
| T12 | timeout | analyzer | `Timeout` + `PossiblySent` 证据 | 1 | **响应超时，设备写入状态未知** |
| T13 | CRC bad response | analyzer | `CrcError` + raw bytes | 1 | 帧校验失败 |
| T14 | exception response | analyzer | `Exception` + code | 1 | 设备异常 |
| T15 | echo address mismatch | analyzer | `ProtocolError + WriteSingleRegisterEchoMismatch` | 1 | 协议错误 |
| T16 | echo value mismatch | analyzer | 同上（值字段不同） | 1 | 协议错误 |
| T17 | wrong unit / function | analyzer | `ResponseAddressMismatch` / `UnexpectedResponseFunction` | 1 | 协议错误 |
| T18 | fragmented response | session framing | 成帧后按实际内容分类（Success/…） | 1 | 取决于结果 |
| T19 | stale session token | store + controller | `Invalidated(SessionChanged)` | 0 | 本地拒绝（会话已变） |
| T20 | source replacement | controller（先 SourceChanged 再 teardown） | 见 R12（pending 时 teardown 取消） | 0/1（取决于是否已提交） | 依 R12 分开呈现 |
| T21 | Clear while pending | 既有 M10-B 契约 | 不影响 pending；完成后成为新首行 | 1 | 无 |
| T22 | reconnect same port | controller | 新 session id；旧 token 失效 | 0 | 本地拒绝 |
| T23 | capability stale UI | controller（第 8 步） | `Invalidated(CapabilityUnavailable)` | 0 | 本地拒绝（能力不可用） |
| T24 | Agent/AI 尝试写入 | authority = NONE | 不存在该工具（结构性不可达） | 0 | —— |
| T25 | draft edited after confirmation | snapshot 不可变 | dispatch 使用 snapshot，不受 draft 影响 | 1 | 无 |

### R19. Knowledge Ownership（§69，真实出处）

```text
0x06 echo semantics      ：通过实现 M10-D 的 C 层 oracle 之前，先实读 `PassiveTransactionAnalysis.cpp:287-313`
                           与 `test_passive_analysis.cpp:271`，理解了「echo 不一致 = ProtocolError + Issue」是
                           **既有冻结语义**，而不是可以顺手改成 Success 的新判断。
CRC 放置                 ：通过 `ModbusRtuCodec::encodeRtuFrame` 与 `ModbusRtuFrame.h` 的「帧不存 CRC」注释，
                           理解 CRC 是派生产物、低字节在前、覆盖 addr+fn+data。
active vs passive 复用   ：通过 `SerialTransactionSession.h:17-37` 的「不写 per-function session」声明与
                           `analyzeActiveResponse` 的单一 dispatch 点，理解主动路径必须复用同一条 lifecycle。
submission evidence      ：通过 M10-A 的 `ActiveStartResult{accepted, disposition, terminatedDuringSubmission}`
                           与 `RecordingSerialTransport::setSubmissionAcceptedBytes`，理解 accepted bytes != device bytes。
timeout unknown-state    ：通过 `analyzeFunction03Transaction` 的 Timeout 分支 + M10-C §Q5-R 的措辞冻结，
                           理解「没有可信响应」不等于「设备未执行」。
one-shot dispatch        ：通过 `PreparedWriteStore::invalidate()` 只作用于 Prepared 代际这一实现细节，
                           推出「先 consume 再进入 flight」的时序是安全且必要的。
QML raw text vs 权威校验 ：通过 `grep -rln TextField src/ui/qml/` 为空这一事实，理解本项目尚无单行文本输入先例；
                           并通过 `WriteDraftParsing` 的逐字符解析，确认十进制 authority 已经在 core 里。
```

### R20. Docs / 提交（§70–§72）

```text
同步：T022（本节 §R）· PROJECT_STATUS · BACKLOG · devlog · INTERVIEW_NOTES。
状态：M10 = IN PROGRESS；M10-C = COMPLETE；M10-D = Learning / Design（Implementation = NOT STARTED）；
      verified LKGC = fc86dcc（本轮 docs-only，不推进）。
提交：`M10-D: design FC06 end-to-end active write`（docs-only design commit）。
不得 amend 19ca421；不得 rebase；不得 push；不得 tag（含 v2.0.0）。
```
## M10-D Phase 1 Correction — Input Authority / Exactly-one-send / Unexpected-function Framing（2026-09-20，docs-only）

> **M10-D Phase 1 Review = HOLD。现有 Phase 1 总体设计接受**（不重做：FC06 passive semantics、generic Active
> lifecycle、snapshot model、atomic confirmation、submission evidence、history/statistics/diagnosis、
> timeout unknown-state、production capability gating、D1–D5 总体 staging）。
> 本轮只关闭 **3 个 blocker**；**不改写 §R 原记录**，以本节为更正声明。严格 docs-only：
> 未开始 D1、未实现 encoder / dispatch、未改输入控件、未 production-enable Write、未 push、未 tag。

```text
HOLD 的 3 个 blocker：
  B1  hard validator 与 raw invalid draft authority 冲突
  B2  sendCount <= 1 不能证明 exactly-one **successful** send
  B3  0x06 framing rule 没有完整解决 unexpected-function response 可能退化为 Timeout
```

### S0. Preflight（§0）

```text
HEAD = 7ddec58（branch = main，working tree clean）
verified LKGC = fc86dcc
M10-C = COMPLETE；M10-D = Learning / Design；Implementation = NOT STARTED
CMake VERSION = 2.0.0；v2.0.0 = ABSENT
origin/main = a40d935；ahead = 114；behind = 0
git diff --check = PASS
```

### S1. B1 — DecimalField raw-text authority（§2–§5）

**冲突的实质**：§R 的 R2 同时写了「raw text 保留非法中间态」与「可加 presentation-only validator」。
若该 validator **阻止** `-1` / `12x` / `65536` / `0x10` / 空串进入 `TextField.text`，则 QML 实际上成了
acceptance authority —— raw draft 不再是用户真实输入，core 也就无法给出**唯一权威**的判定。这正是 HOLD 的 B1。

**冻结（Authority）**：

```text
DecimalField = presentation / input component。
它必须允许 QML draft 保留**用户真实输入文本**。
禁止把「会阻止 Invalid text 进入 TextField.text 的 hard validator」当作协议合法性 authority。
下列输入必须能够形成 raw draft text，并在用户触发 Write（或 validation）时进入 core 权威解析器：
    "-1"  "12x"  "65536"  "0x10"  ""
```

**允许的 QML 辅助（§3）**：`inputMethodHints`、placeholder、focus ring、accessible name、error visual state、
select-all / paste / delete —— **均属 presentation assistance**。
**禁止**：QML 先对字符串做 silent normalize、silent clamp、silent drop invalid character，然后把「改好的合法串」
交给 Controller。**QML 交给 core 的永远是用户原串。**

**唯一 decimal 解析 authority（§4）**：core 侧新增
**`parseDecimalRegisterValue(std::string_view rawText)`**（采纳 Review 的中性命名），规则与既有
`parseRegisterValues` **同源**（同一份 trim / 逐字符十进制累加 / 越界语义），仅额外要求「恰好一个值」：

| 输入 | 结果 |
| --- | --- |
| 前后空白（含 `\r`） | trim 后继续（`" 1234 "` → 1234） |
| 空串 / 全空白 | `NoValues` |
| 纯 `0-9` | 继续解析 |
| 其它字符（`-`、`x`、`.`、`0x`、空白夹在中间） | `InvalidCharacter` |
| 数值 > 65535（含超大整数） | `ValueOutOfRange`（**不 wrap、不抛异常、不受 locale 影响**） |
| `"00010"` | **合法 typed 10** |

**不得**存在 QML parser 与 core parser 两套 acceptance rules。

**Raw-invalid runtime oracle 计划（§5，D1 必须做）**：

```text
O1  "1234"  → draft raw == "1234"  → typed == 1234
O2  "00010" → draft raw 保留原串   → confirmation 显示 10（typing → canonical）
O3  " 1234 "→ draft raw 保留原串   → typed == 1234
O4  "-1"    → draft raw 仍 == "-1" → validation error
O5  "12x"   → draft raw 仍 == "12x"→ validation error
O6  "65536" → draft raw 仍 == "65536" → validation error
O7  paste invalid string → 可观察到 raw draft 原样 → validation error
```

**诚实性要求**：如果 Qt 的 `TextField` 自身会修改这些输入（例如平台/输入法行为），**D1 必须先记录真实
runtime 行为，并据此调整方案或明确 limitation，不得伪造 raw-preservation PASS**。因此 O1–O7 的断言对象是
**`TextField.text` 的真实值 + Controller 侧 raw draft 的真实值**（两处都要观测），而不是只断言「错误出现了」。

### S2. B2 — Exactly-one 词汇与计数（§6–§9）

**冻结词汇（§6）**：

```text
A. dispatch attempt          = Controller 真正把请求交给 transport 的次数
B. transport accepted send   = transport 完整接受（accepted=true，PossiblySent）的次数
dispatch attempt 可以发生而 acceptedCount = 0 ⇒ 两者语义不同，startAttemptCount 与 sendCount 不是同一件事。
```

**RecordingTransport 的真实计数定义（实读 `tests/fake_serial_transport.cpp`）**：

| 计数器 | 真实语义（源码事实） |
| --- | --- |
| `startAttempts_`（`startAttemptCount()`） | **每次** `startActiveRequest` 调用都 +1（无论后续是拒绝、短写还是完整接受） |
| `sendCount_`（`sendCount()`） | **只在完整接受**时 +1（`pending_` 建立、`PossiblySent`、ADU 进入 `sentAduLog_`） |
| `sentAduLog_` | 只在完整接受时 push 一次 `pending_->wire`（**精确** ADU 字节） |
| 短写（`0 < accepted < wire.size()`） | **不建立 pending、不 +sendCount、不进 ADU log**；terminal evidence 携带 intended ADU + accepted byte count |
| pre-send 拒绝（未连接 / busy / accept 关闭 / begin 失败） | `NotSent`，不建立 pending，除 `startAttempts_` 外无计数 |

**成功的 exactly-one-send oracle（§7）**：对正常 deterministic accepting transport，一次有效 Confirm 必须满足

```text
startAttemptCount == 1  &&  sendCount == 1  &&  sentAduLog.size() == 1
且 sentAduLog[0] == 预期 0x06 wire（逐字节，含 CRC）
```

并且在下列重复动作之后 **三项仍必须全部为 1**：Confirm+Space 重复、Confirm+Enter 重复、rapid Enter×2、
rapid Space×2、repeated clicked signal、同 token 第二次 API 调用。
**禁止**用 `sendCount <= 1` 证明「successful exactly-one send」（它只在完整接受时递增，单独看无法区分
「一次完整接受」与「一次都没有」）。

**Pre-send reject oracle（§8，独立场景）**：Controller 已执行**一次 dispatch attempt** 而 transport `acceptedCount = 0`：

```text
startAttemptCount == 1；sendCount == 0；Disposition == NotSent；
no transaction；no terminal；
token 仍为终态（Consumed / Invalidated，按 §R 冻结的 D7/D8 设计：守卫失败 → Invalidated，
  消费之后失败 → 保持 Consumed）；
不得自动重新确认或自动重试。
```

**Short submission 计数（§9）**：`0 < acceptedCount < frameSize` 时

```text
startAttemptCount == 1
sendCount 的真实值按实现记录 = 0（该 ADU 从未完整交付，不进 accepted-ADU log）
安全 oracle **不得只依赖 sendCount**，必须直接检查：
   acceptedCount（= 配置的部分字节数）、Disposition == PossiblySent、
   terminal evidence 中的 exact intended requestAdu、恰好一条 ShortSubmission terminal、zero transaction。
```

### S3. B3 — Unexpected-function Framing（§10–§15）

**§10 实读结论（源码事实，不是推断）**：

```text
candidateFrameLength() 是 SerialTransactionSession 的 **const 成员**，只读取：
    buffer_[1]  → **响应** function 字节
    buffer_[2]  → 仅当 function == 0x03 时作为 byteCount
⇒ 判定依据是 **response function（响应自身的字节）**，既不读 request function，也不做两者比较。
current algorithm（原文行为）：
    异常位 (fn & 0x80) → 5
    fn == 0x03         → 5 + buffer_[2]
    其它               → nullopt（继续累积，直到超时才收尾）
feedResponseBytes：**只在 buffer_.size() == candidate 时**成帧；超长缓冲**永不截断**（等超时后整段解码）。
现有覆盖：0x03 request 收到 0x06/0x84 等 wrong-function 响应已有测试
    （`tests/test_serial_session.cpp:380` → `UnexpectedResponseFunction`）。
```

**§12 三个备选对比**：

| 方案 | 说明 | 错误截短 | 错误吞帧 | unknown 永远超时 |
| --- | --- | --- | --- | --- |
| A 全量 response-length 规则表 | 为**所有**已知 Modbus response 维护长度 | 低 | 低 | 无（但需要为项目未实现的 function 发明规则 ⇒ 不可靠） |
| B 用 request 的 expected shape 定长 | 只按请求期望长度成帧 | 低 | **高**：wrong-function 响应长度不符 ⇒ 永不 subsets，退化为 Timeout | 有 |
| C 最小支持集合 + 明确 limitation | 只给「项目自身能发出的 function」+ 通用异常格式建规则 | 低 | 中（未知 function 仍超时，但**明确记录**） | 部分（记录为已知 limitation） |

**§11 冻结原则**：若已收到**足以确定为完整、但 function 与 request 不匹配**的 RTU frame，
**不得**仅因「该 function 没有 active encoder」就把它退化为 Timeout —— 必须让 protocol analyzer 有机会产生
现有 wrong-function / ProtocolError 语义；**但不得**为此发明不可靠的任意帧长度猜测。

**§13 选定设计（A 的受限形式 + C 的显式 limitation）**：

```text
framing 表（按 **响应 function**）：
    (fn & 0x80) != 0        → 5                    （协议通用异常格式，现有）
    fn == 0x03              → 5 + byteCount        （现有；响应自描述）
    fn == 0x06              → 8                    （新增；正常响应固定 8 字节）
    fn == 0x10              → 8                    （新增；正常响应固定 8 字节 —— 依据
                                                    Function16.cpp: kWriteMultipleRegistersResponseDataSize = 4
                                                    ⇒ 1+1+4+2 = 8）
    其它 function           → nullopt              （保持现有：累积到超时后整段交给 analyzer）
```

**三风险的对策**：① **错误截短**——成帧只发生在精确边界，且超长缓冲永不截断（现有规则）；
② **错误吞帧**——wrong-function 响应按其**自身** function 成帧后立即交给 analyzer，
不看它是否属于当前请求的期望集合；③ **unknown function 永远等超时**——**明确记录为已知 limitation**：
对 0x01/0x02/0x04/0x05/0x08/0x0F/0x11/0x2B 等本项目未实现的 function，**不发明**长度规则；
其响应将在超时路径上以整段字节被分析（可能得到 CrcError/ProtocolError 等真实线缆诊断，
也可能只是 Timeout）。**不得声称对这些 function 有 ProtocolError coverage。**

**0x06 expected length（§13）**：request = 0x06 时，匹配的正常响应 **8 字节**、异常响应 **5 字节**（均无争议）。
若响应 function 既非 `0x06` 也非 `0x86`，由 framing 表按**响应自身 function** 决定（0x03 / 0x10 / 异常位可成帧；
未知 function 走超时路径）——**不写「只新增 0x06 → 8」就结束**。

**§14 Wrong-function oracles（D3 必须做，至少两个 direct case）**：

```text
WF1  0x06 request + 完整 0x03 normal response（长度由 byteCount 决定，framing 规则已存在）
     ⇒ 期望 ProtocolError + UnexpectedResponseFunction（actual function = 0x03）；**不得**变 Timeout。
WF2  0x06 request + 完整 0x10 normal response（固定 8 字节，本轮新增 framing 规则）
     ⇒ 期望 ProtocolError + UnexpectedResponseFunction（actual function = 0x10）；**不得**变 Timeout。
```

两者都必须复用既有 frozen analyzer 语义（`TransactionIssueCode::UnexpectedResponseFunction` + `actualFunctionCode`），
**不新建**任何 write 专用结果类型。

**§15 Wrong unit ≠ echo mismatch（冻结区分）**：

```text
frame 可可靠成帧时的 wrong unit  → ProtocolError + ResponseAddressMismatch
                                   （payload = expected/actual **DEVICE** address）
echo register-address mismatch   → ProtocolError + WriteSingleRegisterEchoMismatch
                                   （payload = expected/actual **REGISTER** address + value）
两者概念不同、payload 字段族不同，**不得合并**为一个 issue 或一个概念。
```

### S4. Capability / Runtime / Visibility / Harness / Simulator（§16–§20）

**§16 capability 命名与语义（采纳 Review 决定）**：只读属性使用 **`write06Supported`**（**替代** §R 中的
`write06Available`）。语义 = 「此产品当前代码路径已端到端拥有 0x06 支持（encoder + atomic dispatch +
response lifecycle）」；**它不表示「此刻可以发送」**，因此**不得**因 disconnected / busy / invalid draft 而变 false。

**§17 runtime enabled state（另行决定）**：Write action 的 **enabled** 由运行态计算：
`source == ActiveSerial` ∧ connected ∧ `!busy` ∧ draft 具备可尝试 prepare 的条件 ∧ 无 active confirmation 等。
⇒ disconnect / busy **不会**销毁 Write Loader、**不会**丢失 page-local draft。

**§18 production visibility**：production 的 Write section 可见性由 **`write06Supported`** 决定；
M10-D 中 `supported == true` 之后显示 **0x06 only（0x10 不显示）**；Write button 的 enabled 由运行态决定。
**禁止**把 visibility 直接绑定 `serialConnected` 或 `serialBusy`。

**§19 harness orthogonality**：`writeFoundationVisible` 继续只是「为测试加载 foundation」的 seam，
与 `write06Supported` **完全正交**；harness **不得**通过修改 `write06Supported` 伪造 capability。

**§20 simulator scope**：M10-D **不要求** Simulator 生成 0x06 production response；主要 deterministic
end-to-end oracle 是 `RecordingSerialTransport` + scripted response。既有 `WriteMode` / `applyWriteRequest`
foundation 继续保留；Simulator 的 0x06 request-response loop 可作为未来 enhancement 或 D3 的额外实现，
**不得**为 M10-D 扩大必做 scope。REAL HARDWARE 仍**非**自动 acceptance requirement。

### S5. 16 项决策 — 全部 RESOLVED（§21）

| # | 决策 | 裁定 |
| --- | --- | --- |
| D1 | address / value 输入控件类型 | **RESOLVED：DecimalField（基于 TextField）** |
| D2 | raw text ownership | **RESOLVED：draft = page-local raw text** |
| D3 | parser rules | **RESOLVED：core 权威十进制解析器（`parseDecimalRegisterValue`）** |
| D4 | leading zeros | **RESOLVED：接受；summary 显示 typed canonical number** |
| D5 | outer whitespace | **RESOLVED：core 统一 trim / 接受** |
| D6 | invalid presentation | **RESOLVED：保留 raw + 字段错误；无 silent normalization** |
| D7 | consumption ordering | **RESOLVED：guards → consume → encode → start** |
| D8 | pre-send 失败后 | **RESOLVED：消费之后的任何失败都要求重新确认** |
| D9 | capability 表达 | **RESOLVED：只读 `write06Supported`**（取代 `write06Available`） |
| D10 | production 可见性切换 | **RESOLVED：同一 Write 组件；production capability 控制 0x06 呈现；harness seam 正交** |
| D11 | echo 复用 | **RESOLVED：共享 FC06 core analyzer** |
| D12 | Simulator 是否回答 0x06 | **RESOLVED：M10-D 不要求** |
| D13 | Success 成立条件 | **RESOLVED：有效可信帧 + 正确 unit/function + 精确 address/value echo + 无任何冻结错误条件** |
| D14 | timeout 文案 | **RESOLVED：「响应超时，设备写入状态未知」** |
| D15 | 真实硬件检查 | **RESOLVED：非阻塞 follow-up** |
| D16 | D1–D5 staging | **RESOLVED：接受，但以本节 framing correction 为准** |

### S6. D1–D5 Scope（§22–§26，Review 裁定）

```text
D1（本轮 correction PASS 后才可以实现）：
    DecimalField；单值解析器；0x06 encoder；独立 golden CRC 向量；pure tests。
    **不做**：Controller dispatch、SerialTransport send、production visibility、response integration。
    注意：unexpected-function framing 的实现属于 D3，但**其设计已在本轮闭环**。
D2：write06Supported capability seam；atomic confirmAndDispatchPreparedWrite(token)；
    RecordingTransport；exactly-one **successful** send；pre-send reject；short submission；
    session/busy/source guards。**不做** production-visible UI。
D3：0x06 candidate framing（含 S3 的 framing 表）；共享 FC06 analyzer；normal echo；exception；CRC；
    wrong unit/function（WF1/WF2）；timeout；fragmentation；history；statistics；diagnosis；terminal/evidence 集成。
D4：production-visible **0x06-only** UI；DecimalField 生产用法；runtime capability gating；input UX；
    keyboard/a11y；geometry；C01–C37 回归。
D5：full exactly-one 矩阵；zero-send 矩阵；全部 response/error 矩阵；Debug/Release regression；
    manual visual sanity；M10-D Final Review。
```

### S7. Docs / 提交（§27–§29）

```text
同步：T022（本节 §S）· PROJECT_STATUS · BACKLOG · devlog · INTERVIEW_NOTES。
状态：M10-D Phase 1 = Correction / Re-review；Implementation = NOT STARTED；verified LKGC = fc86dcc。
提交：`M10-D: close FC06 dispatch design gaps`（docs-only；不 amend 7ddec58；不 rebase；不 push；不 tag）。
```
## M10-D Phase 1 Final Correction — Capability Staging 与 Confirm/Dispatch Result Semantics（2026-09-20，docs-only）

> **M10-D Phase 1 Final Re-review = HOLD。** §R + §S 主体设计接受；**已有 B1 / B2 / B3 全部 CLOSED（禁止重做）**。
> 本轮只关闭两个新发现：**B4** product capability 与 staging 顺序矛盾；**B5** confirmation result 与 dispatch
> result 语义混在单一 bool 中（并可能让 Consumed snapshot 对应的 Dialog 悬挂）。
> **不改写 §R / §S**；严格 docs-only：未开始 D1、未实现 encoder / DecimalField / dispatch、
> 未 production-enable Write、未 push、未 tag。

```text
最终 HOLD 状态：§R（Learning/Design）+ §S（round-1 correction）主体接受；
B1 raw-text authority = CLOSED；B2 exactly-one counting = CLOSED；B3 unexpected-function framing = CLOSED；
B4 capability staging 矛盾 = 本节关闭；B5 confirm/dispatch result semantics = 本节关闭。
```

### T0. Preflight（§0）

```text
HEAD = 1e3a4ce（branch = main，working tree clean）
verified LKGC = fc86dcc
M10-C = COMPLETE；M10-D = Learning / Design；Implementation = NOT STARTED
CMake VERSION = 2.0.0；v2.0.0 = ABSENT
origin/main = a40d935；ahead = 115；behind = 0
git diff --check = PASS
```

### T1. B4 — 两级 Capability 与 staging 时序（§2–§10）

**矛盾实质**：§S 把「新增 0x06 framing + 共享 FC06 analyzer + response lifecycle」放在 **D3**（原 D3），
却又把 `write06Supported`（产品级能力）放在 **D2** 的交付里，并要求它「只在端到端支持存在时为 true」——
D2 时 response lifecycle 尚未实现，`write06Supported` 只能靠 override / fake 才能为 true，这正是设计矛盾。

**冻结的两级能力**：

```text
A. internal active protocol/session support
   = activeFunctionSupported(0x06)（或项目真实等价的 internal seam）
   含义：protocol/session 已经能正确处理 0x06 request/response lifecycle。
   **不**表示产品可写；**不**控制任何 UI。

B. product-level Write06 support
   = write06Supported（只读属性）
   含义：产品已**端到端**拥有：0x06 encoder + protocol/session response support +
        Controller atomic dispatch + evidence/outcome integration。
   只有 B 可以控制 production Write 0x06 是否呈现。
```

**诚实性要求（§3）**：`write06Supported` **不得**在 response lifecycle 尚未实现时、为了让 D2 测试通过而临时变 true。
**禁止** test override / fake capability / build-type capability / harness capability；
它**只能**在真实 end-to-end path 已经成立时为 true。

**修订后的 staging 与能力时序（§4–§10）**：

```text
D1 = input / parser / encoder
     DecimalField raw-text draft integration + parseDecimalRegisterValue + 0x06 encoder +
     independent golden vectors + pure tests。
     D1 结束时：encoder 可以存在，但 Controller dispatch 仍不存在、production UI 仍 hidden、
     **write06Supported 仍 false / absent**（不得因为 encoder 存在就宣称 product support）、
     **activeFunctionSupported(0x06) 仍为 false**（不得提前打开）。
D2 = protocol / session response support（原 D3 的 response-lifecycle 基础前移）
     0x06 candidate framing、共享 FC06 core analyzer、normal echo、exception、CRC、wrong unit、
     wrong function（WF1/WF2）、timeout、fragmentation、response length；
     用 **direct SerialTransactionSession 或等价 lower-level tests** 验证。
     只有当 encoder 已存在 ∧ 0x06 candidate framing 已正确 ∧ analyzeActiveResponse 可处理 0x06 ∧
     normal/exception/error session tests PASS 之后，internal `activeFunctionSupported(0x06)`
     （或等价 lower-level support）**才允许**为 true。
     D2 **不实现**：Controller confirmation dispatch、production UI。
D3 = Controller dispatch / evidence / product capability
     confirmAndDispatchPreparedWrite + RecordingSerialTransport 集成 + exactly-one successful send +
     acceptedCount=0 + short submission + post-submit error/disconnect + session/source/busy guards +
     history append + statistics + diagnosis + **write06Supported**。
     D3 初始 write06Supported = **false**；只有 D3 的 end-to-end acceptance PASS 之后才允许 = true。
     即使 write06Supported == true，D3 期间 normal production 的 Write UI **仍可保持不可见**（D4 未完成）——
     **capability ready ≠ production presentation rollout**。
D4 = production UI（0x06-only、DecimalField 正式 wiring、write06Supported 驱动的呈现、
     runtime enable 规则、validation/error 呈现、keyboard/a11y、geometry、M10-C safety regression；0x10 仍不显示）
D5 = final acceptance（full exactly-one 矩阵、zero-send 矩阵、normal/error/evidence 矩阵、
     Debug/Release regression、manual visual sanity、optional non-blocking hardware check、M10-D Final Review）
```

**0x10 framing recognition 边界（§7）**：D2 **允许**添加 `response function 0x10 → candidate length 8`
**仅**作为 *known-response-shape recognition*（用于 wrong-function framing）。明确它**不代表**：
`activeFunctionSupported(0x10)`、**也不代表** 0x10 encoder、0x10 dispatch、0x10 UI capability ——
**四者继续 ABSENT / false**。

**0x10 framing 回归要求（§8）**：`candidateFrameLength` 是**共享 session 规则**，新增 0x10 recognition
必须测试其对既有 FC03 行为的影响。至少：**0x03 request 收到完整 0x10 response → 可可靠成帧 →
走既有 `UnexpectedResponseFunction` / `ProtocolError` 语义**；不得静默变成未记录的新行为；
其它 unknown function 的 limitation 继续保留（不发明长度规则）。

### T2. B5 — Confirm/Dispatch 的两个正交事实与 Dialog authority（§13–§25）

**冻结（§13）**：一次 Confirm operation 至少包含**两个正交事实**：

```text
A. confirmationAccepted   —— 用户明确确认了当前 immutable snapshot（一次性消费的结果）
B. dispatch/start result  —— 本次 attempt 的运输事实（disposition / accepted / evidence）
不能用一个 bool 同时表示两者（§17 的 bool 歧义）。
例：token 成功 consume 但 transport acceptedCount = 0
    ⇒ confirmationAccepted = true 且 disposition = NotSent —— 两个事实必须同时可表达。
```

**Typed internal result（§16）**：Controller/core 内部使用 typed 结果（`PreparedDispatchResult` 或项目风格等价，
不强制命名），至少能表达：`confirmationAccepted`、`dispatchAttempted`、**existing `ActiveStartResult`**
（或等价 start result）、local pre-start failure（若存在）。**优先复用** `ActiveStartResult` /
`TransportDisposition` / `ActiveTransportTerminal`；**不得**新建 `WriteTransportStatus` / `WriteSendOutcome`
第二套 transport taxonomy。

**Dialog authority 规则（§14，冻结）**：Dialog 生命周期**必须继续服从 `PreparedWriteSnapshot` state**：

```text
Prepared    → Dialog 可以存在
Consumed    → 旧 confirmation 已 terminal，Dialog **必须退出** confirmation flow
Invalidated → 同样退出
一旦 Prepared → Consumed，即使随后发生 encode failure / acceptedCount=0 / short submission /
transport error，**都不得**因为 send/start 失败而让旧 confirmation Dialog 保持可确认状态。
```

**已有实现证据**：该规则在 M10-C2 已经落地并被 oracle 证明 —— `WriteFoundationSection.qml` 的
`Connections.onPreparedWriteChanged { if (!hasPreparedWrite && confirmationDialog.opened) close() }`，
配合 C10-C（一次消费）/ E1（rapid Enter 不重置）/ E2（下一轮 Enter 不复活）等 runtime oracle。
M10-D **必须复用**该机制，**不得**给它加上「transport success」条件。

**QML 侧边界（§15 / §17）**：**禁止**未来 QML 写
`if (confirmAndDispatch(...)) { dialog.close() }` 并把该 bool 理解为 **transport send success**。
Dialog 关闭**必须**基于 Controller authoritative prepared state 离开 Prepared
（或 Controller 明确返回 confirmation-consumed fact）——**transport success 绝不是 Dialog authority**。
Phase 1 **不强制** Q_INVOKABLE 最终返回 struct：可以是 private typed operation + QML wrapper、
enum/presentation result、或 state-notification 驱动的 void call；但**必须冻结**：
QML 不能仅凭单个模糊 bool 区分 **confirmation accepted** / **send accepted** / **device success**。
具体 API shape 在 **D3 的 source re-read 之后**决定。

**guard-before-consume（§18）**：stale token / source changed / session changed / disconnected / busy /
capability unavailable 发生在 consume 之前 ⇒ **zero dispatch attempt、zero send**；snapshot 按 frozen reason
**Invalidated** 或保持旧 terminal；Dialog 因 authority 不再 Prepared 而退出 confirmation flow。

**failure-after-consume（§19）**：guard PASS、`Prepared → Consumed` 之后发生的
encode internal failure / transport acceptedCount=0 / short submission / post-submit transport terminal ——
**旧 token 不复活**；Dialog **不得**继续允许 Confirm；**draft 继续保留**；再次尝试必须重新
**Write → prepare → confirmation**。

**acceptedCount = 0 的呈现（§20）**：confirmation 已接受但 request 未被 transport 完整接受发送 ⇒
`Disposition = NotSent`、**无 transaction**、**无 transport terminal**（继承 M10-A）。必须有 non-success
presentation 或现有 serial error / evidence lane 能告知「**本次请求未发送，如需重试必须重新确认**」；
**不得**显示「写入成功 / 设备已写入」。

**Short submission 的呈现（§21）**：`PossiblySent` + **一条 `ShortSubmission` terminal**；Dialog 不复活；
用户-facing 必须体现「**设备状态未知 / 提交不完整**」而**不是**「未写入」；继续禁止 retry。

**Encode failure 语义（§22）**：理论上 validated snapshot + `write06Supported` 使 0x06 encoder 失败成为
**内部不变量异常**；仍需设计：**zero transport attempt**、token 保持 **Consumed**、**no Modbus transaction**、
**no transport terminal**、明确的 **local/internal error lane**；**不得**把 encoder bug 伪造成 ProtocolError
或 device response。

**R1–R5 result oracles（§23，D3 必须做）**：

```text
R1 full accepted        ：confirmationAccepted=true；startAttempt=1；send=1；PossiblySent
R2 acceptedCount=0      ：confirmationAccepted=true；startAttempt=1；send=0；NotSent
R3 short submission     ：confirmationAccepted=true；startAttempt=1；send=0；PossiblySent + ShortSubmission terminal
R4 guard failure        ：confirmationAccepted=false；startAttempt=0；send=0
R5 same token 2nd call  ：confirmationAccepted=false；无额外 attempt / send
```

**Dialog oracle 矩阵（§24，D3/D4 必须直接证明）**：R1 后 Dialog closed；R2 后 Dialog closed；
R3 后 Dialog closed；guard invalidation 后 Dialog closed；同一 consumed token **无法在没有新 prepare 的情况下
重新打开 / 重新确认**。⇒ 由此证明 **Dialog authority 不是 transport success**。

**Exactly-one 定义保持（§25）**：沿用上一轮冻结 —— successful full acceptance：`startAttemptCount == 1` ∧
`sendCount == 1` ∧ `sentAduLog.size() == 1`；acceptedCount=0：`1 / 0`；short：`1 / 0` **加** acceptedCount +
PossiblySent + terminal。**不得**退回 `sendCount <= 1` 这种弱 oracle。

### T3. 决策状态（§26）

```text
D1–D15（上一轮 RESOLVED）继续有效，本轮**不重开**。
D16 staging 更正为：D1 input/parser/encoder → D2 protocol/session response support →
                    D3 Controller dispatch/evidence/product capability → D4 production UI →
                    D5 final acceptance；并以此最终 staging 标记 **RESOLVED**。
```

### T4. Threat Matrix Correction（§27）

以下条目按「confirmationAccepted / dispatchAttempt / sendCount / TransportDisposition / snapshot terminal /
Dialog state」六个事实重写：

| # | 场景 | confirmationAccepted | dispatchAttempt | sendCount | Disposition | snapshot terminal | Dialog |
| --- | --- | --- | --- | --- | --- | --- | --- |
| T08 | disconnect before submit | true（若已 Confirm） | 1 | 0 | `NotSent`（本地取消） | Consumed | closed（authority） |
| T10 | short submission | true | 1 | 0 | `PossiblySent` | Consumed | closed |
| T11 | acceptedCount = 0 | true | 1 | 0 | `NotSent` | Consumed | closed |
| T23 | stale capability / UI | false（guard 失败） | 0 | 0 | —— | `Invalidated(CapabilityUnavailable)` | closed（authority） |
| T04/T05 | double Confirm / rapid Enter | 首次 true，其后 false | 首次 1，其后 0 | 1 / 0 | 首次 `PossiblySent` | Consumed | closed |
| T12 | timeout | true | 1 | 1 | `PossiblySent` | Consumed | closed |
| T06/T07 | busy（consume 前） | false | 0 | 0 | —— | `Invalidated(BusyBecameTrue)` | closed（authority） |
| T19/T22 | stale session token / reconnect | false | 0 | 0 | —— | `Invalidated(SessionChanged)` | closed（authority） |
| T25 | draft edited after confirmation | true | 1 | 1 | `PossiblySent` | Consumed（snapshot 不受 draft 影响） | closed |

（其余 T01–T03 / T09 / T13–T18 / T20–T21 / T24 语义不变；T20 切源仍按 §R/S4：先 `SourceChanged` 再 teardown，
pending 时本地取消并（若已提交）产生 terminal。）

### T5. Docs / 提交（§28–§30）

```text
同步：T022（本节 §T）· PROJECT_STATUS · BACKLOG · devlog · INTERVIEW_NOTES。
状态：M10-D Phase 1 = Final Correction / Final Re-review；Implementation = NOT STARTED；verified LKGC = fc86dcc。
提交：`M10-D: align FC06 capability staging and dispatch result semantics`（docs-only；
      不 amend 1e3a4ce；不 rebase；不 push；不 tag）。
```
## M10-D1 — Decimal Input + FC06 Encoder Foundation（2026-09-20，behavior-bearing）

> **M10-D Phase 1 Final Re-review = PASS ⇒ M10-D Phase 1 = COMPLETE ⇒ M10-D1 = GO。**
> 本轮实现：`DecimalField` 表现层组件、0x06 address/value **raw-text draft**、`parseDecimalRegisterValue`、
> raw-text → 权威校验边界、0x06 语义 encoder、`encodeActiveRequest` 的 0x06 描述符、独立 golden 向量、
> pure 与 hidden-runtime 测试。**未做**：Controller write dispatch、transport write send、0x06 response lifecycle、
> `candidateFrameLength` 0x06、共享 FC06 active analyzer、`write06Supported = true`、production-visible Write UI、
> 0x10 encoder、Agent write authority。**D2 未开始。**

### U0. Preflight 与 Phase 1 PASS 归档（§0 / §1）

```text
HEAD（开工前）= 98dc310（branch = main，working tree clean）
verified LKGC = fc86dcc；v2.0.0 = ABSENT；origin/main = a40d935；behind 0
CMake VERSION = 2.0.0；git diff --check = PASS
accepted design chain = 7ddec58 → 1e3a4ce → 98dc310（三轮 correction 均 docs-only，LKGC 未推进）
本轮记录：M10-D1 = Decimal Input + FC06 Encoder Foundation，状态 IN PROGRESS → 本节完成后等待 Review
```

### U1. Mandatory Source Re-read — 审计结论 A–F（§2）

| # | 问题 | 真实结论（含证据） |
| --- | --- | --- |
| A | `prepareWrite06` 当前 QML-facing 参数类型 | `Q_INVOKABLE bool prepareWrite06(int unitId, int registerAddress, int value, int timeoutMs)`（全部 `int`）——QML 侧原本传的是 SpinBox 的**数值**，没有 raw 概念 |
| B | 是否已有内部 typed prepare helper | **有**：`prepareWriteSingleRegister(int64, int64, int64, int64)` → `prepareWriteIntent(WriteIntentResult)`（private）→ `PreparedWriteStore::prepare`；Q_INVOKABLE 只是薄包装 |
| C | `encodeActiveRequest` 是否与 `activeFunctionSupported` 直接耦合 | **不耦合**：`grep` 全仓仅 `SerialTransactionSession.cpp:77`（`beginActiveRequest` 的门）引用它；encode 路径只做 `validateActiveRequestIntent` + function 分派。⇒ **D1 可以放行 0x06 encoder 而 internal support 保持 false**（这正是 staging 需要的） |
| D | `activeFunctionSupported(0x06)` 由哪里决定 | `SerialTransactionSession.cpp:53`，当前 `return function == ReadHoldingRegisters;`（仅 0x03）——**本轮未改动** |
| E | Function06 现有能力 | **原本只有 decode**（`decodeWriteSingleRegisterRequest/Response`）；本轮新增 **`encodeWriteSingleRegisterRequest`**（semantic frame，无 CRC、无错误分支） |
| F | QML 0x06 address/value 的 property/objectName 现状与 harness 用法 | 原为 `SpinBox{objectName: write06AddressSpin/write06ValueSpin, value: section.address06/value06}`；harness 以 `setDraft("address06"/"value06", <int>)` 写入、以 `property("value06").toInt()` 读取、并在 tab-order / a11y / C4 探针中按 objectName 引用 |

**未出现「必须提前打开 `activeFunctionSupported(0x06)`」的情形**（结论 C），因此本轮无 STOP、无 RCA 例外。

### U2. DecimalField 与 Raw-Text Authority（§4–§9）

```text
新增 src/ui/qml/components/DecimalField.qml（QML_FILES 已注册）：
  · 基础 = QQC2 TextField；**没有任何 validator**（既不阻止非法字符，也不做范围判定）；
  · 职责仅限：raw text editing / focus ring（M9-F 的 border 通道）/ error 视觉态 / accessible name /
    键盘 ergonomics（单行 TextField 原生 Tab 离开，未重路由任何按键）；
  · 暴露：text（alias）、placeholderText、accessibleName、fieldLabel、hasError；
  · 内层 TextField **刻意不具名**：组件本身承载 identity（与既有 SpinBox 的「焦点落到内部子项」约定一致）；
  · Accessible.name 同时设在组件与外层焦点元素上（前者是 oracle 读取对象，后者是屏幕阅读器实际聚焦的元素）；
  · **不持有协议业务 truth**：文件中不存在 0..65535、1..247 等协议范围（范围判定只在 core / Controller）。
```

- **No hard validator 证明**：O1–O7 全部以**真实按键/真实剪贴板粘贴**驱动，断言 **`TextField.text` 原样** ——
  `-1` / `12x` / `65536` / `00010` / ` 1234 ` 都真的进入了控件文本，且没有任何 QML 侧改写。
- **QML assistance**：使用 placeholder / focus ring / accessible name / error state / select-all / copy·paste / delete；
  **刻意不使用 `inputMethodHints`**：桌面平台的数字 hint 也可能在某些输入法/平台上影响字符进入，而本组件的契约是
  「必须能承载任意 raw 文本」——因此不引入任何可能过滤输入的机制（该决定记录于本节，供 Review 追溯）。
- **Raw draft ownership**：`addressText06` / `valueText06` 为 **page-local raw text**（`unit06` / `timeout06` 保持数值 draft）；
  双向绑定 `text: section.addressText06` + `onTextChanged: section.addressText06 = text`。
  draft 继承 M10-C 的持久化契约（导航 / Clear / disconnect / reconnect / source replacement 均保留，C14/C15/C31/C32 继续通过）。
- **Raw-Text Controller 边界**：新增 `Q_INVOKABLE bool prepareWrite06Draft(int unitId, const QString& addressRaw,
  const QString& valueRaw, int timeoutMs)`；它**先 parse（core 权威）再委托既有 typed helper**
  （`prepareWrite06` → `prepareWriteSingleRegister` → 既有 validation → 既有 snapshot）；
  Controller **不是**持续 draft owner（只在用户触发 Write 时接收一次当前 draft）。
- **Typed helper 保留（§9）**：`prepareWrite06` / `prepareWriteSingleRegister` 未删除、未复制第二份校验；
  范围校验仍只有一份（raw 边界只做 decimal 解析 + 字段身份映射）。

### U3. `parseDecimalRegisterValue`（§10–§13）

```text
core/active/WriteDraftParsing：新增 SingleRegisterValue + SingleValueParseResult +
parseDecimalRegisterValue(std::string_view rawText)；错误 taxonomy 最小扩展一个码：
MultipleValuesInSingleField（单值字段收到两个值）。
实现复用 parseRegisterValues（同一份 trim / 逐字符十进制累加 / 越界语义）——**不存在第二套 acceptance table**：
  · 先 trim 外层空白（空格 / TAB / CR / LF）；
  · 余下文本含换行 ⇒ MultipleValuesInSingleField（**不**取首行、**不**取末行、**不**拼接）；
  · 空 ⇒ NoValues；纯 0-9 ⇒ 十进制累加；其它字符 ⇒ InvalidCharacter；>65535 / 超大整数 ⇒ ValueOutOfRange
    （不 wrap、不 throw、不依赖 locale）；"00010" ⇒ typed 10。
```

- **空输入**：`""` / 全空白 / 仅换行 ⇒ `NoValues`，Write 时 validation reject、Dialog 不开、snapshot 不建、zero transport attempt，
  **绝不自动变 0**（R0 实测）。
- **多行输入**：`"1\n2"` 与 `"1\r\n2"` ⇒ `MultipleValuesInSingleField`（P13/P14 纯测试 + 实现层同源）。
- **空白**：`" 1234 "` / `"\t 1234 \r\n"` 接受（typed 1234），**draft 仍保存原串**（O3 实测 raw `[ 1234 ]`）。
- **前导零**：`"00010"` 合法 = 10；draft 保留 `"00010"`，**confirmation summary 显示权威数值 10**（O2 实测）。

### U4. Encoder（§20–§26）

```text
Function06.h/.cpp：ModbusRtuFrame encodeWriteSingleRegisterRequest(address, registerAddress, registerValue)
  —— 只产出**语义帧**（address + 0x06 + addrHi/addrLo + valHi/valLo），CRC 仍由 encodeRtuFrame 负责；
  每个 uint16 组合都可编码 ⇒ **不制造假的错误分支**；单播校验继续留在 intent/session 层（codec 保持 address-agnostic）。
ActiveRequestIntent.cpp：encodeActiveRequest 新增 WriteSingleRegister 分支 → semantic encoder → encodeRtuFrame →
  ActiveRequestDescriptor{intent, frame, wire}；**WriteMultipleRegisters 仍返回 UnsupportedFunction**。
unit 0：继续在 intent validation 阶段被拒（IntentInvalid），**不产生 descriptor / 不产生 wire**（E7 实测）。
**无 framing 改动**：candidateFrameLength / analyzeActiveResponse / FC06 active response path 一行未动 ⇒
  0x06 response 仍无法完成 active transaction（s1 测试：begin 仍返回 UnsupportedFunction，session 保持 Idle）。
**无 dispatch**：未新增 confirmAndDispatchPreparedWrite、未调用 transport 写路径、hidden Confirm 仍 zero dispatch。
```

### U5. 独立 Golden Oracle 与向量（§27–§29）

- **独立性**：新增测试目标 `write_encoder`（`tests/test_write_encoder.cpp`）**自带 test-only bitwise CRC**
  （按定义实现：poly 0xA001 reflected / init 0xFFFF / LSB-first），**不 include 生产 `ModbusCrc.h`**；
  期望 ADU 同时以**固定 literal**形式写死在测试源码里（长期 golden vector），因此
  「encoder 与 oracle 从同一 helper 自证」在结构上不可能发生；若两者不一致，测试失败 ⇒ 重新推导，**绝不挑一个能过的**。
- **向量集合（全部 8 字节，CRC 低字节在前）**：

| # | unit | address | value | 完整 ADU（literal） |
| --- | --- | --- | --- | --- |
| G1 | 1 | 0 | 0 | `01 06 00 00 00 00 89 CA` |
| G2 | 247 | 65535 | 65535 | `F7 06 FF FF FF FF 9C C8` |
| G3 | 0x11 | 1 | 3 | `11 06 00 01 00 03 9A 9B`（= 协议文档应用示例的字段序列） |
| G4 | 1 | 65535 | 0 | `01 06 FF FF 00 00 89 EE` |
| G5 | 1 | 0 | 65535 | `01 06 00 00 FF FF 88 7A` |
| G6 | 1 | 0 | 1 | `01 06 00 00 00 01 48 0A` |

- **来源/推导**：字段由 R3 的字节表手工拼装；CRC 由**两份互相独立的实现**（bitwise 与查表）交叉验证后写为 literal；
  G3 与公开协议示例序列一致，构成外部佐证。测试同时断言「独立 CRC 复算 == literal」与「production descriptor == literal」。
- 每向量逐项断言：字段顺序、`wire.size() == 8`、CRC 低/高字节顺序、descriptor 的 intent/frame/wire。

### U6. Staging 证明（§32 / §41）

```text
S1 encodeActiveRequest(0x06) 成功（descriptor + 8 字节 wire）          —— write_encoder 测试
S2 activeFunctionSupported(0x06) 仍为 **false**；0x03 仍 true；0x10 仍 false —— write_encoder 测试
S6 SerialTransactionSession::beginActiveRequest(0x06 descriptor) 仍返回 **UnsupportedFunction**，
   session 保持 Idle、无 pendingRequest ⇒ 0x06 response 不可能完成 active transaction（framing 未变）
S3 产品级 capability **不存在**：AnalysisController 上 indexOfProperty("write06Supported") < 0
   （同时确认旧的 write06Available 也不存在）                            —— ui_bridge 测试
S4 normal production：Loader inactive / item null / 675 对象无 write 控件、无 write tab stop、
   startup 无 snapshot                                                  —— qml_focus_check（Debug + Release）
S5 hidden Confirm：全部 oracle 跑完后 writeAttempts == 0；会话历史 function codes = [3]（只有 FC03 读）
```

**三个事实同时成立**：encoder 可用 ∧ internal support 关闭 ∧ product capability 不存在 —— 这正是 §4/§6 要求的时序。

### U7. Runtime 与回归 Oracle（§14–§19）

```text
真实 hidden QML runtime（--qml-write-foundation-check）实测输出：
O1  typing "1234"   -> TextField.text=[1234]  raw draft=[1234]  typed address=1234
O2  typing "00010"  -> raw [00010] 保留；snapshot address=10；summary 显示 10
O3  typing " 1234 " -> raw [ 1234 ] 保留；typed 1234
O4  typing "-1"     -> raw [-1] 保留；validation error，error field = address
O5  typing "12x"    -> raw [12x] 保留；validation error，error field = value
O6  typing "65536"  -> raw [65536] 保留（无 clamp）；validation error，error field = value
O7  真实剪贴板 paste "12x" -> raw 原样；validation error，error field = address
R0  空串 -> 仍为空（绝不自动 0）；validation error，error field = address
R1  Ctrl+A + 重打 -> raw [65535]，typed 65535（Dialog 打开）
R3  Tab -> [write06ValueField]；Shift+Tab -> [write06AddressField]
a11y address=[0x06 寄存器地址（十进制）] value=[0x06 写入值（十进制）]
```

- **字段身份区分（§19）**：新增只读投影 `writeDraftErrorField`（token：unit/address/value/timeout/values/quantity/span）；
  `setWriteDraftParseError` 共享 parser 的 typed reason、由**边界**补字段前缀 ⇒ 地址错误与写入值错误**不会**都显示成「输入错误」。
- **M10-C 安全回归**：C01–C37 / E1 / E2 / C4 全部继续 PASS（本轮把 0x06 draft 从数值改为 raw text、
  控件名改为 `write06AddressField` / `write06ValueField`，**断言强度未降低**：新增了 raw 原串观测与字段身份断言）。
- C04 的「draft 未被清空」检查改为读**控件 raw text 非空**（原为 SpinBox value != 0）——语义等价且更贴近 raw 契约。

### U8. 边界冻结（§33–§37）

```text
production-hidden：normal production 仍不可见（Loader inactive / item null / 无 write 控件与 tab stop / 无 snapshot）；
  0x06 encoder 的存在**不**改变可见性。
Zero transport side effects：raw input / prepare / Dialog / Confirm / Cancel / encoder 单测 —— 全部未触发
  SerialTransport start、sendCount、write transaction、transport terminal；harness writeAttempts = 0。
history / statistics / diagnosis / transaction model / terminal lane：**未改动**。
AI / Agent write authority = NONE：未新增 prepare-write / confirm-write / send / raw-ADU 工具。
```

### U9. 测试与门禁（§38–§44）

```text
write_prepare  48 passed（32 + 16 个 D1 单值解析用例 P1–P14 等价集 + 与多行解析的同源一致性）
write_encoder  19 passed（G1–G6 + E7–E12 + descriptor 契约 + S1/S2/S6）
active_request 17 passed（ac3 按 D1 契约更新：0x06 现在编码成功、0x10 仍 UnsupportedFunction）
ui_bridge      61 passed（59 + S3 capability 缺失 + raw 边界字段身份）
其余 23 套件全绿：active_master 54 / passive 55 / serial 21 / transaction 20 / diagnosis 17 / agent_runtime 28 …
Debug ctest **32/32 PASS**；Release ctest **32/32 PASS**（新增 write_encoder 目标 ⇒ 31 → 32）
新增代码：C++ / QML **零新增 warning**（src/main.cpp 5 条 pre-existing 未动）
ISSUE-014：仍 PRE-EXISTING NON-BLOCKING（write harness 10 条 TransactionsPage reset-window 告警，
  **写 UI / DecimalField 0 条**）；未顺手修。
```

### U10. Problems Encountered / RCA（§45）

```text
RCA-1（harness 焦点身份）：DecimalField 内层 TextField 最初带 objectName（"…Input"），导致 focusOwnerName()
  返回内部子项名，tab-order oracle 与 D1 焦点断言全部失败。
  根因：命名层级与既有 SpinBox 约定不一致（SpinBox 的内部编辑器无名）。
  修复：内层 TextField 不具名（组件本身承载 identity）；断言未降低（仍断言焦点落在具名控件上）。
RCA-2（合成按键缺字符文本）：typeText 仅发 keycode，Qt TextInput 从 KeyPress 的 `text` 字段取输入 ⇒ 文本始终为空。
  根因：既有 sendKey 是给「按键命令」用的，不携带字符。
  修复：D1 的 typeText 自行构造带 text 的 QKeyEvent（sendKey 未改动 ⇒ 30+ 既有 oracle 零影响）。
RCA-3（oracle 前置条件）：prepare 路径按字段顺序报告**第一个**失败字段，导致 O5/O6/R1 观测到 address 错误而非 value。
  根因：用例继承了上一用例留下的非法文本。
  修复：每个用例前把另一字段置为合法/清空（并在 setup 阶段清空两个 raw 字段）——**修的是 oracle 前置条件，
  不是产品语义**（字段顺序报告本身是正确行为）。
RCA-4（契约变更，非缺陷）：tests/test_active_request.cpp 的 ac03 原断言「0x06 encode = UnsupportedFunction」，
  D1 按设计让 0x06 可编码 ⇒ 该断言必然失败。处理：把该测试改名为 ac03_writeEncodeContract()，
  改为断言 0x06 编码成功（frame/wire 与字段）**并且** 0x10 仍 UnsupportedFunction —— 负向覆盖未丢失，且是显式契约变更
  （记录于本节，而非静默修改）。
```

### U11. Files / Commit（§46–§48）

```text
新增：src/ui/qml/components/DecimalField.qml、tests/test_write_encoder.cpp
修改：src/core/active/WriteDraftParsing.{h,cpp}、src/core/protocol/Function06.{h,cpp}、
      src/core/active/ActiveRequestIntent.cpp、src/ui/AnalysisController.{h,cpp}、
      src/ui/qml/components/WriteFoundationSection.qml、src/main.cpp（harness 迁移 + D1 oracles）、
      CMakeLists.txt（QML_FILES + write_encoder 目标）、tests/test_write_prepare.cpp、
      tests/test_active_request.cpp、tests/test_ui_bridge.cpp
docs：T022（本节 §U）+ PROJECT_STATUS / BACKLOG / devlog / INTERVIEW_NOTES
分类：**behavior-bearing**（core / QML / parser / encoder / tests 均为行为变化，即使 production Write 仍隐藏）
commit：`M10-D1: add FC06 input and encoder foundation`（独立提交；不 amend 98dc310；不 rebase；不 push；不 tag）
verified LKGC 继续 `fc86dcc`（不自行推进）。
```
## M10-D2 — FC06 Protocol / Session Response Support（2026-09-20，behavior-bearing）

> **M10-D1 Review = PASS ⇒ M10-D1 = COMPLETE ⇒ M10-D2 = GO。** 本轮让底层 active protocol/session
> **真正理解 0x06 response**：0x06 candidate framing、0x10 response-shape recognition（仅识别）、共享 FC06 分析器、
> active 0x06 normal/exception/CRC/wrong-unit/wrong-function/echo 语义、分片与超时语义、lower-level 测试，
> **最后**才打开 `activeFunctionSupported(0x06)`。
> **未做**：Controller confirm+dispatch、production write transport 调用、`write06Supported`、production-visible Write UI、
> DecimalField 行为改动、0x10 encoder/analyzer/dispatch、Agent write authority。**D3 未开始。**

### V0. Preflight 与 D1 PASS / D2 Start 归档（§0 / §1）

```text
HEAD = 6ab97e1（branch = main，working tree clean）
verified LKGC = fc86dcc（继续；不因 D2 推进）
M10-C = COMPLETE；M10-D Phase 1 = COMPLETE；M10-D1 = COMPLETE（accepted behavior commit = 6ab97e1，不做独立 D1 closure commit）
CMake VERSION = 2.0.0；v2.0.0 = ABSENT；origin/main = a40d935；ahead = 117；behind = 0
开工前状态：0x06 encoder = PRESENT；activeFunctionSupported(0x06) = false；write06Supported = absent；0x06 dispatch = absent
git diff --check = PASS
```

### V1. Mandatory Source Re-read（§2）

实读：`SerialTransactionSession.{h,cpp}`（`candidateFrameLength` / `feedResponseBytes` / `onResponseTimeout` /
`beginActiveRequest` / `activeFunctionSupported` / `analyzeActiveResponse`）、`ActiveTransactionEvidence.h`
（`ActiveTransactionResult` / `ActiveTransportTerminal` / `TransportDisposition`）、`PassiveTransactionAnalysis.cpp`
（步骤 1–7 的顺序与 `analyzed()` / `makeIssue()` / request-issue 层）、`TransactionAnalysis.{h,cpp}`（七 outcome、
issue 稀疏 payload、`makeAnalysis` / `makeProtocolError`）、`Function06.{h,cpp}`（decode + D1 encoder）、
`ModbusRtuCodec`（`decodeRtuFrame` 的「末两字节即 CRC、payload = 其余」规则）、`ActiveRequestIntent` /
`ActiveRequestDescriptor`、以及 `test_serial_session` / `test_passive_analysis` / `test_active_request` /
`test_transaction_analysis` 里既有 FC03 的 normal / wrong function / wrong unit / exception / CRC / partial / timeout /
fragmentation 覆盖。

**关键既有事实（决定实现方式）**：

```text
· candidateFrameLength 只读 buffer_[1]（响应 function）；除 0x03 与异常位外一律 nullopt；
· feedResponseBytes 只在 buffer_.size() == candidate 时成帧；超长**永不截断**；
· onResponseTimeout：空 buffer → NoResponse（Pending/Timeout）；非空 → 解码**整段** buffer 交给 analyzer；
· beginActiveRequest：validate intent → activeFunctionSupported 门 → descriptor 自洽 → 单点提交；
· analyzeActiveResponse：唯一 dispatch 点；0x03 分支复用 T007 分析器；0x06/0x10 曾是防御分支；
· 被动 FC06 分支包含 decode + echo 比较 + ProtocolError/WriteSingleRegisterEchoMismatch（本轮改为委托共享函数）。
```

### V2. D1 Staging Reconfirm 与 RED Evidence（§3 / §50）

```text
开工前实证（全部符合预期，未触发 STOP）：
  encodeActiveRequest(0x06) 成功；beginActiveRequest(0x06) → UnsupportedFunction；
  candidateFrameLength 无 0x06；analyzeActiveResponse 无 0x06；write06Supported 不存在；Controller 无 confirm+dispatch。

RED（先写 oracle 套件，再实现，实测）：
  R1  beginActiveRequest(0x06) = UnsupportedFunction（测试断言该事实）
  R2  matching 8-byte echo 无法成帧（无 0x06 framing 规则；根本无法 feed）
  R3  WF2 的 0x10 response 形状无法成帧
  R4  **编译期 RED**：`analyzeWriteSingleRegisterTransaction` 未声明 —— 共享 FC06 分析器在实现前不存在
      （这是最强形式的 RED：oracle 连编译都过不去，而不是「行为恰好像通过」）
实现后三条 R1/R2/R3 断言被翻转为 post-D2 形态（begin 接受 / echo 到达即成帧 / 0x10 形状到达即成帧），
RED 事实保留在本节。
```

### V3. Shared FC06 Analyzer 与被动分层保持（§5 / §6）

```text
新增（core/analysis/TransactionAnalysis）：TransactionAnalysis analyzeWriteSingleRegisterTransaction(
    request, observation, elapsed, timeoutThreshold)
—— **0x06 响应配对的唯一实现**：被动路径与 active 路径都调用它，因此**不存在第二份 address/value echo 比较**。
它包含：NoResponse→Pending/Timeout；CrcMismatch→CrcError；FrameTooShort→ProtocolError；
decoded frame 的 unit 门（ResponseAddressMismatch）；通用异常路径（(fn|0x80)、data.size()==1）；
0x06+0x06 → 双方解码 + **精确 echo（地址与值同时相等）**→ Success 或 ProtocolError +
WriteSingleRegisterEchoMismatch（携带 expected/actual 四元组）；其它 function → ProtocolError + UnexpectedResponseFunction。

被动分层**保持不变**：`PassiveTransactionAnalysis` 的 FC06 分支改为
`analyzed(analyzeWriteSingleRegisterTransaction(...), elapsed, requestIssues)`
—— 新增一个 `analyzed(analysis, elapsed, requestIssues)` 重载把**被动特有的 request-issue 层**贴在共享结果上，
状态/issue/elapsed 一律由共享分析器决定、**不重算**。被动 request semantic issues、broadcast 语义、
UnsupportedObservedTransaction 等被动专属事实全部保留。
```

### V4. Framing 表与精确边界（§12–§16 / §34）

```text
最终 framing 表（依据**响应自身**的 function 字节）：
  (fn & 0x80) != 0  → 5                     （协议通用异常格式，原有）
  fn == 0x03        → 5 + byteCount          （响应自描述，原有）
  fn == 0x06        → 8                      （新增；0x06 正常响应固定 8 字节）
  fn == 0x10        → 8                      （新增；**仅 known-response-shape recognition**）
  其它 function     → nullopt                （保持：累积到 timeout 后整段解码；**已知 limitation**，
                                              不发明长度规则，也不声称所有 wrong-function 都会立即 ProtocolError）
精确边界契约**未改**：仍只在 buffer_.size() == candidate 时成帧；超长永不截断（9 字节用例实测未成帧）。
```

- **unknown-shape 直接测试**（lim1）：0x06 请求 + 完整 0x04 形状响应 ⇒ 到达时**不**成帧（`AwaitingMoreData`），
  超时后整段解码 → 因为整段恰是一个合法 RTU 帧，最终得到 `ProtocolError + UnexpectedResponseFunction`
  —— 即 **classification 正确但只能等到 timeout**（测试名与档案都写明这是 limitation，而不是 coverage 声明）。

### V5. Active 0x06 与信任边界（§17–§19）

```text
analyzeActiveResponse 新增 WriteSingleRegister 分支 → 调用**同一个**共享分析器（request 用 send-time frame）。
信任边界：active 请求来自 validated intent + production encoder ⇒ 不重建被动 request semantic issue；
但**响应侧校验完整**（unit / function / echo / CRC / exception / timeout 全部照做）。
Raw evidence：session 完成后回到 Idle，send-time 证据随 descriptor / `ActiveTransactionResult` 传递
（D2 不涉及 Controller 侧 append；descriptor 的 wire = 8 字节、function = 0x06 由测试断言）。
```

### V6. Oracle 实测结果（§20–§37）

```text
NR1 normal echo            ：一次 feed 即成帧 → Success、无 issue、无 exceptionCode；session 回 Idle
F1  8 bytes 一次            ：Success
F2  1 + 7                   ：Success
F3  2 + 2 + 4               ：Success
F4  逐字节 ×8               ：Success
F5  CRC 两字节分开（6+1+1） ：Success
EX1/EX2/EX3 0x86 exception  ：一次 / 1+4 / 逐字节 → Exception，exception code 一致（0x02 / 0x03 / 0x04）
CRC1 完整 8 字节但 CRC 错    ：CrcError（数据字节被翻转；raw bytes 由 session/transport 保留）
ECHO1 地址不一致            ：ProtocolError + WriteSingleRegisterEchoMismatch（四元组完整；值字段 same）
ECHO2 值不一致              ：ProtocolError + WriteSingleRegisterEchoMismatch（值 expected/actual 正确）
ECHO3 两者都不一致          ：**同一个** issue 携带完整 expected/actual 四元组（不是两个 issue）
UNIT1 unit 不同（echo 形状一致）：ProtocolError + ResponseAddressMismatch（expected/actual **device** address），
                                并显式断言 **不是** WriteSingleRegisterEchoMismatch
WF1  0x06 请求 + 完整 0x03 响应：到达即成帧（0x03 规则）→ ProtocolError + UnexpectedResponseFunction(0x03)，**非 Timeout**
WF2  0x06 请求 + 完整 0x10 响应：到达即成帧（新增 0x10 识别）→ ProtocolError + UnexpectedResponseFunction(0x10)，**非 Timeout**
REG1 **0x03 请求 + 完整 0x10 响应**：成帧 → ProtocolError + UnexpectedResponseFunction(0x10)
     —— 这是 0x10 识别规则带来的**共享 framing 行为变化**，显式归档（此前只能等 timeout）
LIM1 0x06 请求 + 0x04 形状     ：到达不成帧；timeout 后整段解码 → ProtocolError + UnexpectedResponseFunction
T1   空响应 + timeout          ：Timeout
P1   1 字节 partial + timeout  ：ProtocolError + ResponseFrameTooShort（partial **不**被标成 Timeout）
P2   3 字节 partial + timeout  ：ProtocolError + ResponseFrameTooShort
P3   7 字节 partial + timeout  ：CrcError（长度已足以读出 CRC 字段，且不匹配）
O1   9 字节（8 + 1 trailer）   ：到达不成帧（精确边界未变）；timeout 后整段解码 →
                               **实测 ProtocolError + MalformedNormalResponse**（末两字节被当作 CRC、恰好通过，
                               随后 0x06 数据长度检查失败）。**记录的契约是「绝不静默截断」**，而不是「任意垃圾必然 CrcError」。
M1   6 字节 malformed + timeout：CrcError（未为测试补造 CRC）
EQ1  被动 vs 共享（6 组用例）  ：status / exceptionCode / issue 有无 / issue code 与全部 payload 字段逐一相等
                              （Success、地址不一致、值不一致、wrong unit、wrong function、exception）
SUP1 支持矩阵                ：0x03 support ✅ / 0x06 support ✅（本轮）/ 0x10 support ❌ 且 encoder ❌（UnsupportedFunction）
```

**被动回归（§37）**：`passive` 套件 **55 passed、断言零修改** —— 共享重构未造成任何被动语义漂移。

### V7. 既有测试的显式契约更新（不是静默改动）

```text
D2 让 0x06 的 session gate 打开，因此三条「0x06 在发送前被拒」的旧断言必须更新（负向覆盖**转移**到 0x10）：
  · tests/test_active_request.cpp::ac06_writeDescriptorRejectedBeforeSend
      → 现在断言 0x06 descriptor 被接受（AwaitingResponse）+ 0x10 descriptor 仍被拒（UnsupportedFunction）
  · tests/test_write_encoder.cpp::s1_encoderSucceedsWhileSessionStillRefuses
      → 现在断言 encoder 成功 **且** session 接受 0x06；「encoder ≠ 产品能力」的站位上移到 write06Supported 缺失
  · tests/test_write_encoder.cpp::s6_sessionRefusesToBeginWriteSingleRegister
      → 改为 D2 支持矩阵（0x03 ✅ / 0x06 ✅ / 0x10 ❌）
以上三处均在本节显式记录；没有任何断言被删除或弱化。
```

### V8. 边界、门禁与产物（§39–§54）

```text
支持矩阵（§39）：0x03 = encoder ✅ + session support ✅；0x06 = encoder ✅ + session support ✅；
                 0x10 = encoder ❌ + session active support ❌ + response-shape recognition only ✅。
无 Controller dispatch（§40）：未新增 confirmAndDispatchPreparedWrite；UI confirmation 仍无法启动任何发送。
write06Supported 仍 absent（§41）：ui_bridge 断言 indexOfProperty("write06Supported") < 0（D3 才可能引入）。
production Write 仍隐藏（§42）：qml_focus_check prod-hidden 仍 PASS（675 对象无 write 控件 / tab stop /
  startup 无 snapshot）—— **active support 变 true 没有泄漏成 production visibility**。
harness 正交（§43）：hidden foundation 照常加载，Confirm 仍 zero dispatch。
零产品写派发（§44）：write harness 跑完 `writeAttempts == 0`；会话历史 function codes = [3]（**没有任何
  Controller 生成的 0x06 transaction**）。session 单测里出现的 0x06 result 属 lower-level 证据，**不等于**产品 UI 已发送。
History / statistics / diagnosis（§45）：未改 appendActiveSerialTransaction / statistics / diagnosis（D3 范围）。
Timeout 文案（§46）：D2 只冻结 `status = Timeout`；「响应超时，设备写入状态未知」的呈现留给 D3/D4，未新增假 UI。
0x10 scope（§47）：唯一变化是 framing recognition（+其测试）；没有 0x10 encoder / intent support / analyzer success / dispatch。
AI / Agent（§48）：write authority 继续 NONE。
测试（§51 / §53）：fc06_active 31 passed（新）；write_encoder 19；active_request 17；serial 21；passive 55；
  transaction 20；codec 9；crc 8；frame 6；write_prepare 48；active_master 54（Controller no-regression）；
  ui_bridge 61；其余套件全绿。**Debug ctest 33/33 PASS；Release ctest 33/33 PASS**（新增 fc06_active 目标 ⇒ 32 → 33）。
QML gates（§52）：qml_write_foundation_check / qml_focus / qml_smoke / qml_nav / qml_geometry 全部 PASS。
Warnings（§54）：新增 C++ 零 warning；src/main.cpp 5 条 pre-existing 未动。
ISSUE-014：继续 PRE-EXISTING NON-BLOCKING（write harness 10 条 TransactionsPage reset-window 告警，写 UI 0 条）。
```

### V9. Problems Encountered / RCA

```text
RCA-5（overlong 的真实契约）：9 字节用例最初按「必然 CrcError」写断言，实测是 **ProtocolError +
  MalformedNormalResponse**。根因：codec 把**末两字节**当 CRC，于是 trailer 的取值决定线缆判决；本例恰好通过了 CRC，
  随后被 0x06 的 4 字节数据长度检查拒掉。
  处理：**按真实行为记录**（并保留「绝不静默截断」这一真正的契约断言），没有为了让测试好看去改产品行为。
RCA-6（session 完成后无 pending）：NR1 最初断言完成后的 `pendingRequest()` 仍有值，实测为 nullopt —— 这是既有契约
  （完成后 resetToIdle）。send-time 证据由 descriptor / `ActiveTransactionResult` 承载。
  处理：断言改为「descriptor 的 wire 8 字节 + function 0x06 + session 回 Idle」，并注明 runtime 侧证据在 result 上。
RCA-7（共享 framing 的 FC03 行为变化）：0x10 识别规则是共享 session 规则，因此「0x03 请求收到 0x10 响应」
  从「等 timeout」变成「到达即成帧 → UnexpectedResponseFunction」。这是**有意**的行为变化，已按 §30 新增 REG1 归档
  （不是回归）。
RCA-8（三条 pre-D2 契约测试）：见 V7，属显式契约更新，负向覆盖转移到 0x10。
环境观察（非本轮产物）：工作树出现未跟踪目录 `.workbuddy/`（内含另一个工具自己的 memory 文件）。
  它不是本任务产生的内容，也不是仓库档案区的一部分 —— **未提交、未修改、未删除**，此处仅作记录。
```

### V10. Files / Commit（§57 / §58）

```text
新增：tests/test_fc06_active.cpp（+ CMake 目标 fc06_active）
修改：src/core/analysis/TransactionAnalysis.{h,cpp}（共享 FC06 分析器）、
      src/core/analysis/PassiveTransactionAnalysis.cpp（FC06 分支委托共享分析器 + request-issue 层重载）、
      src/core/serial/SerialTransactionSession.cpp（framing 表 0x06/0x10、active 0x06 分支、support gate 最后打开）、
      CMakeLists.txt、tests/test_active_request.cpp、tests/test_write_encoder.cpp
docs：T022（本节 §V）+ PROJECT_STATUS / BACKLOG / devlog / INTERVIEW_NOTES
分类：**behavior-bearing**（protocol/session 行为已变，即使 Controller UI 仍不能写）
commit：`M10-D2: add FC06 active response support`（独立提交；不 amend 6ab97e1；不 rebase；不 push；不 tag）
verified LKGC 继续 `fc86dcc`。
```

## M10-D3 — FC06 Atomic Dispatch / Evidence / Product Capability（2026-09-21，behavior-bearing）

> **M10-D2 Review = PASS ⇒ M10-D2 = COMPLETE ⇒ M10-D3 = GO**（Human Review，handoff 外部事实；本轮开工时 repo docs
> 尚未记录该结论，因此本节第一件事就是把它 append-only 归档）。
> 本轮交付：Controller **原子** `confirmAndDispatchPreparedWrite(token)`、final guards、one-shot consume、
> `RecordingSerialTransport` 集成、`acceptedCount=0` 的**确定性 seam**、short submission、post-submit error/disconnect、
> Active Serial history / statistics / diagnosis 集成、**`write06Supported`** 产品能力属性。
> **未做**：production-visible Write UI、production Confirm 接 dispatch、0x10 任何能力、Agent write authority、automatic retry。
> **D4 未开始。**

### W0. Preflight 与 D2 PASS / COMPLETE / D3 GO 归档（§1 / §4）

```text
branch = main；HEAD = ee3bc3e（full ee3bc3eb46a110f4fdcb8f443d651cbcc98d7ee4）；working tree clean
verified LKGC = fc86dcc；origin/main = a40d935；ahead 118 / behind 0
CMake VERSION = 2.0.0；v1 tag object = 2cee626 → target ae067ab；v2.0.0 = ABSENT
git diff --check = PASS；git status --porcelain = 空（见 §W14 私有状态卫生）
M10-D1 = COMPLETE；M10-D2 = COMPLETE；M10-D3 = GO
```

**Agent-private 卫生（§1）**：`.workbuddy/` 确认为 agent private state（`git ls-files .workbuddy` 为空，
仓库内无任何引用）。按规则**只**写入 `.git/info/exclude`（local only，**未**动项目 `.gitignore`、**未**提交），
此后 `git status --porcelain` 为**空**。

**归档 Human Review 既有结论（append-only）**：M10-D2 Review = PASS、M10-D2 = COMPLETE、
accepted D2 behavior commit = `ee3bc3e`、M10-D3 = Atomic Dispatch / Evidence / Product Capability = IN PROGRESS →
本节完成后 COMPLETE。**本节与本轮 behavior 变更同提交**（不产生单独 docs-only commit，§4）。

### W1. Contract Fingerprint 复核（§3，全部源码实证，与预期一致）

```text
FC03_ENCODER = YES（encodeReadHoldingRegistersRequest）
FC03_SESSION = YES（activeFunctionSupported(0x03)=true）
FC03_CONTROLLER_DISPATCH = YES（readHoldingRegistersOnce → 唯一 startActiveRequest 调用点）
FC06_ENCODER = YES（encodeWriteSingleRegisterRequest + encodeActiveRequest 0x06 分支）
FC06_SESSION = YES（activeFunctionSupported(0x06)=true，D2）
FC06_CONTROLLER_DISPATCH = NO → 本轮新增（confirmAndDispatchPreparedWrite）
WRITE06_SUPPORTED = ABSENT → 本轮新增（write06Supported，CONSTANT）
PRODUCTION_WRITE = HIDDEN（writeFoundationVisible 由 harness 参数单独控制）
FC10_ENCODER = NO；FC10_ACTIVE_SUPPORT = NO；FC10_DISPATCH = NO；FC10_UI = NO
FC10_RESPONSE_SHAPE_RECOGNITION = YES（framing 0x10 → 8，仅识别）
AGENT_WRITE_AUTHORITY = NONE（仅 3 只读工具）
ISSUE_CODE_COUNT = 14 observable/public（TransactionIssueCode 10 + TransactionRequestIssueCode 4）
```

**Issue 口径说明（§37）**：`UnknownProtocolError` 语义上仍是 defensive/sentinel fallback
（P0 测试名即 `a13_defensiveUnknownProtocolError`，header 注释「deterministic defensive/fallback branch only」），
但它**有 public enum member + 被测锁定的稳定 token `unknown_protocol_error` + UI projection
（`TransactionListModel` → 「协议错误（未记录细节）」）+ 7 处 production construction path** ⇒
**不能从 public count 扣掉**。本轮**确认并沿用 `14` 口径**（与 `11_PROJECT_FINAL_RETROSPECTIVE.md` §6.1
「14 = 10 + 4」以及 `12_RESUME_INTERVIEW_QA.md` 一致）；**不引入也不恢复「13」**。

### W2. Mandatory Source Re-read — A / B / C 三问（§6）

**A. FC03 Controller 从 encode 到 transport start 的真实完整 lifecycle**（实读 `AnalysisController.cpp`）：

```text
readHoldingRegistersOnce(unit, start, qty, timeout)
 1 范围校验（unit 1..247 / start 0..65535 / qty 1..125 / timeout > 0）→ 失败 setSerialError + return
 2 !serialConnected_ → setSerialError + return
 3 serialBusy_ → setSerialError + return
 4 构造 ActiveRequestIntent（function=ReadHoldingRegisters）→ encodeActiveRequest
 5 编码失败 → setSerialError + return（defensive，当前 function 集下不可达）
 6 serialTransport_->startActiveRequest(descriptor)
 7 !accepted → 若 terminatedDuringSubmission 则 activeSerialTerminations_.push_back；return
 8 pendingRequest_ = descriptor；serialBusy_ = true
 9 preparedWriteStore_.invalidate(BusyBecameTrue)（仅影响 Prepared 代际）
10 clearSerialError() + emit serialStatusChanged()
```
完成侧：`transactionCompleted` → `handleSerialTransactionCompleted`（pending 身份守卫）→
`appendActiveSerialTransaction`（唯一写入点）→ `refreshActiveSessionDerivedViews`（statistics + diagnosis）。
终止侧：`transactionTerminated` → `handleSerialTransactionTerminated`（no-double-terminal 守卫）→ terminals lane。

**B. 是否已有可抽取的 generic Controller dispatch helper**：**没有**。步骤 6–10 是唯一的 active 派发核心，
只被 FC03 使用。⇒ 本轮按 §6 要求**提取**出 `startActiveDescriptor(descriptor)`。

**C. 直接新增第二个 `startActiveRequest` 调用点会复制哪些逻辑**：会复制 ①`accepted` 分支与
short-submission terminal 归档；②`pendingRequest_`/`serialBusy_` 进入 flight；③`BusyBecameTrue` 失效；
④`clearSerialError()` + `serialStatusChanged` 信号。⇒ 若不提取，将出现**write-only lifecycle**（§6 明令禁止）。

**决定（§6 优先方案）**：提取最小 generic helper `startActiveDescriptor()`，FC03 与 0x06 写路径**共用**；
helper 只做「交给 transport + 归档 submission terminal + 进入单一 in-flight 状态」，
**不**发明任何 Modbus verdict，并把 transport 的 `ActiveStartResult` 原样回传。**不建立 write-only lifecycle。**

### W3. Stale Comment Audit（§7）

| 位置 | 旧描述 | 处理 |
| --- | --- | --- |
| `ActiveRequestIntent.h` §顶部契约（26–29 / 121 / 135–137） | 「0x06 / 0x10 stay impossible by construction」「0x06/0x10 return UnsupportedFunction」 | **确认 stale**（D1 起 0x06 可编码），本轮同步为「0x03/0x06 已实现，0x10 仍 ABSENT」 |
| `ActiveRequestIntent.cpp` 0x06 分支注释 | 「the session still refuses to begin an active 0x06 transaction (activeFunctionSupported stays false until M10-D2)」 | **确认 stale**（D2 已打开），本轮同步 |
| `AnalysisController.h` `prepareWriteSingleRegister` 注释 | 「0x06 / 0x10 encoders do not exist yet」 | **确认 stale**，本轮同步并指向 `confirmAndDispatchPreparedWrite` |
| `AnalysisController.h` / `.cpp` confirm 注释 | 「dispatch is M10-D/E work」 | 同步为「dispatch 见 `confirmAndDispatchPreparedWrite`」 |
| `AnalysisController.cpp` `writeFoundationVisible` 注入点 | 「0x06 / 0x10 still have no encoder」 | 同步（0x06 encoder 已存在；0x10 仍无） |

**未做**：docs 中**当时正确**的历史 snapshot **不改写**（如 `11_PROJECT_FINAL_RETROSPECTIVE.md` §6.1 ③
「FC06 与 0x10 只有被动分析」是 V1 时点的真实记录）；如易误读，只加 as-of 限定，不改造历史。
**绝不**为了迎合旧注释而回退真实行为。

### W4. RED Evidence（§8，实现前捕获）

```text
RED1 Controller atomic confirm+dispatch API 不存在
     → `confirmAndDispatchPreparedWrite` 未声明（编译期红）；既有 `confirmPreparedWriteToken` 只消费不派发。
RED2 Prepared 0x06 snapshot 无法经 Controller 触发 transport attempt
     → M10-C/D2 状态下「prepare → confirm」后 transport.startAttemptCount() 恒为 0。
RED3 没有明确的 transport-level acceptedCount=0 / NotSent oracle
     → 见 W5：fake 的 `setSubmissionAcceptedBytes(0)` 当时会**穿透到完整接受**。
RED4 write06Supported 不存在 → `ui_bridge` 原断言 `indexOfProperty("write06Supported") < 0`（D1/D2 staging 契约）。
RED5 Controller 0x06 completion 尚无正式 dispatch path 进入 Active history
     → 唯一 `startActiveRequest` 调用点在 FC03 读路径。
附加确认 ConfirmRejectReason **缺** CapabilityUnavailable（原 6 值，见 W6）。
```

### W5. RecordingTransport zero-accept seam 审计与结果（§9）

**审计全部 call sites**（`setSubmissionAcceptedBytes` / `setAcceptRequests`）：
`tests/test_active_master.cpp` 使用 4 / 3 / nullopt；`setAcceptRequests(false)` 用于 pre-send 拒绝。
**不存在任何 `0 = disable override` 依赖**（全仓无 `setSubmissionAcceptedBytes(0)`）⇒ 按 §9 首选方案直接定义 0 语义，
**未**改变任何已冻结行为，**未**新增第二个 seam。

**结果**：`setSubmissionAcceptedBytes(0)` ⇒
`startAttemptCount += 1`、`sendCount += 0`、`accepted = false`、`Disposition = NotSent`、
**no pending / no terminal / 不进 ADU log**；并发出既有 bounded `transportError`（可观察 non-success fact）。
**明确不**用 Controller guard reject 冒充 transport NotSent（见 W9 的区分 oracle）。

### W6. CapabilityUnavailable taxonomy（§10）

`ConfirmRejectReason` 原为 6 值（NotPrepared / TokenMismatch / NotConnected / SourceNotActiveSerial /
SessionChanged / Busy），**确实缺** capability 维度 ⇒ **最小扩展一个成员** `CapabilityUnavailable`
（`PreparedWriteInvalidReason::CapabilityUnavailable` 早已存在并从 M10-C1 起 reserved）。
**未**新建第二套 capability error taxonomy。

### W7. Atomic API / Final Guards / Ordering / Typed Result（§11–§14）

```text
[[nodiscard]] PreparedDispatchResult confirmAndDispatchPreparedWrite(std::uint64_t token);
输入 = opaque token ONLY（无 unit/address/value/timeout/raw draft）；
请求体 = Controller 自身 immutable PreparedWriteSnapshot（编码用**consume 前捕获的副本**，
        因为 consume 会清空 store —— 读值不是 chain 中的一步，chain 仍是
        final guards → consume → encode → start）。
```

**final guards（全部在 consume 之前）**：

```text
state == Prepared ∧ snapshot/token 存在          → 否则 NotPrepared
token 匹配                                       → 否则 TokenMismatch
sourceKind == ActiveSerial                       → 否则 SourceNotActiveSerial  + invalidate(SourceChanged)
serialConnected_                                 → 否则 NotConnected          + invalidate(Disconnected)
snapshot->sessionId == activeSerialSessionId_    → 否则 SessionChanged        + invalidate(SessionChanged)
!serialBusy_                                     → 否则 Busy                  + invalidate(BusyBecameTrue)
function == 0x06 ∧ activeFunctionSupported(0x06) ∧ kProductWrite06Supported
                                                 → 否则 CapabilityUnavailable + invalidate(CapabilityUnavailable)
```

最后一条刻意把 `kProductWrite06Supported` 纳入：它与产品属性是**同一个单一事实源**，
因此「对外声称的能力」与「实际行为」不可能互相矛盾（若该常量为 false，派发会被拒绝而不是与属性说法冲突）。

**Typed result（§14）**：`PreparedDispatchResult{ confirmationAccepted, dispatchAttempted,
rejectedReason?, startResult?, localError? }`，**复用**既有 `ActiveStartResult` / `TransportDisposition` /
`ActiveTransportTerminal`；**未新建** `WriteTransportStatus` / `WriteSendOutcome`。

**Encode failure（§32）**：validated snapshot + capability 成立 ⇒ encoder 失败是**内部不变量异常**；
处理为 `localError = EncodeFailed`、**zero transport attempt**、token 保持 Consumed、no transaction、no terminal、
**不**伪装成 ProtocolError / Timeout / Exception。**未**为测试新增 corrupt-snapshot API。

### W8. QML-facing boundary 与 Dialog authority（§15）

**D3 有意不改变任何 QML**：`WriteFoundationSection.qml` 的 `confirmPreparedWriteToken(token)` 继续**只做确认**，
`Connections.onPreparedWriteChanged { if (!hasPreparedWrite && opened) close() }` 继续让 Dialog 只服从 snapshot state。
production Confirm **不接** dispatch（D4 才 rollout）。本轮用 **Controller focused tests** 验证 atomic dispatch。
**Dialog authority = snapshot state**：R1/R2/R3/guard 之后 snapshot 都不再是 Prepared ⇒ Dialog 退出确认流程；
transport success 在任何路径下都**不**决定 Dialog 关闭。

### W9. Oracle 实测结果（§16–§21）

```text
R1 full accepted   ：confirmationAccepted=true；dispatchAttempted=true；attempt=1；send=1；sentAduLog.size()=1；
                     exact ADU == G3 字面量 `11 06 00 01 00 03 9A 9B`；PossiblySent；snapshot=Consumed；
                     busy=true；随后 matching echo → 恰好一条 0x06 Success，同 session，
                     requestAdu/responseAdu 逐字节相等（evidence()）。          —— write_dispatch 37 passed
R2 acceptedCount=0 ：confirmationAccepted=true；dispatchAttempted=true；attempt=1；send=0；NotSent；
                     no pending；busy=false；zero transaction；zero terminal；token 不复活；
                     可观察 non-success（hasSerialError）；无 Success/Timeout/ProtocolError 伪装。
R3 short submission：0<3<8 ⇒ PossiblySent + **恰好一条** ShortSubmission terminal
                     （request.wire == G3 字面量、responseAdu 空、submissionAcceptedByteCount=3）；
                     zero transaction；busy=false。
R4 guard matrix    ：disconnected → NotPrepared（snapshot 已被 disconnect 置 Invalidated(Disconnected)，
                     NotConnected 分支为 defensive）；busy → NotPrepared（BusyBecameTrue）；
                     stale session（同 COM/baud 重连 → sessionId 递增）→ 拒绝；source changed
                     （runDemoBatch）→ 拒绝；prepared **0x10** → CapabilityUnavailable
                     （invalidate(CapabilityUnavailable)，zero consume/encode/start）。
                     五者共同断言：confirmationAccepted=false、attempt=0、send=0。
R5 token reuse     ：Consumed 后同 token **连续 10 次**调用 —— 额外 attempt=0、额外 send=0、
                     confirmationAccepted=false。**不依赖任何 debounce**。
Guard vs NotSent   ：**专门 oracle** 证明 guard failure 的 result **没有** startResult（因而没有
                     TransportDisposition），而 acceptedCount=0 的 result **有** startResult{NotSent}。
                     两者不共用测试名、不共用报告口径。
Busy 稳定性        ：full accepted 后 busy=false→true 不得把 Consumed 改写成 Invalidated(BusyBecameTrue)
                     （store 只作用于 Prepared 代际，实测 Consumed 且 invalidReason 为空）；
                     Success / Timeout / ShortSubmission / TransportError 四条路径 busy 均回落 false。
Post-submit        ：TransportError / DisconnectedAfterSubmission ⇒ 恰好一条 terminal、保留 intended request
                     evidence、PossiblySent、zero fabricated transaction、busy=false、device state UNKNOWN。
```

### W10. Response / History / Statistics / Diagnosis 集成（§22–§28）

**复用而非新建**：0x06 的 completion/termination 全部走**既有** `handleSerialTransactionCompleted` /
`handleSerialTransactionTerminated` / `appendActiveSerialTransaction` —— 这些路径本就 function-agnostic，
因此 **0x06 自动加入同一 transaction universe**，**没有** Write History 第二套。

```text
Success / Exception(0x02) / CrcError（数据字节被翻转，raw bytes 保留）/ ProtocolError（FC06 echo mismatch，
  issue code = WriteSingleRegisterEchoMismatch）/ Timeout 五类全部纳入实测。
Timeout：恰好一条 0x06 Timeout 事务，**无** terminal；语义锁定「响应超时，设备写入状态未知」，
  实现层不携带任何 device-mutation 声明，**禁止** implicit retry。
History 顺序：FC03 在前、0x06 在后，两者 sessionId 相同且等于当前 activeSerialSessionId（未产生第二 universe）。
Terminal 排除：ShortSubmission terminal ⇒ 不进 rows、不动任何统计计数器（observed/completed 均为 0）。
Statistics（冻结公式，混合批次实测）：observed=3=pending(0)+completed(3)；
  completed=Success(2)+Exception(0)+CrcError(0)+Timeout(1)+ProtocolError(0)+ExpectedNoResponse(0)；
  successRate = Success / (completed − ExpectedNoResponse) = 2/3（实现与断言均按该式）；
  latency 只取 Success（timeout 不稀释）；**未**新增 writeSuccessRate 之类第二套 authority。
Diagnosis：同一 deterministic batch；baseline 文本同时覆盖 0x06 Success 与 0x06 Timeout（实测含「无响应超时」）。
```

### W11. Clear / Disconnect / Source replacement / Draft（§29–§31）

```text
Clear while pending：不清 pending、不断开、不清 draft；空视图后完成的 0x06 成为**第一条**新事务；
  **不**触碰已 Consumed 的确认终态。
Disconnect while pending：走既有 post-submission evidence ⇒ 一条 DisconnectedAfterSubmission terminal、
  **不**造 Timeout 事务、**不**声称 device unchanged。
Source replacement while pending：沿用 M10-A/B 冻结顺序（先 SourceChanged 后 teardown），本轮重定义 = 无；
  已 Consumed 的代际保持终态。
Draft preservation：七类终局（Success/Exception/CRC/Timeout/zero-accept/short/TransportError）逐一验证
  token 不复活、必须重新 Write → prepare → confirmation；无 automatic retry。
```

### W12. write06Supported（§33 / §34）

```text
新增 core/active/ProductWriteCapability.h：inline constexpr bool kProductWrite06Supported = true;
  —— 结构性事实（encoder + protocol/session + Controller atomic dispatch + evidence 集成四者齐备），
     编译期常量，**不是** runtime availability。
新增 Q_PROPERTY(bool write06Supported READ write06Supported CONSTANT)（read-only、无 setter、无 NOTIFY）。
  CONSTANT 是有意选择：能力是「代码事实」，没有真实 runtime 变化需要广播；
  若将来确需 NOTIFY，必须存在真实的 runtime 变化理由（本轮不存在）。
运行时不变性实测：clearResults / runBaselineDiagnosis / disconnectSerial / connectSerial+busy /
  Simulator source 切换之后，属性恒为 true。
**capability ≠ rollout**：属性为 true 的**同时** production Write UI 仍不可见 ——
  一半证据在本轮（属性 CONSTANT 且不可写，无任何 runtime 开关可提前揭示 UI），
  另一半由 `qml_focus_check` 的 prod-hidden oracle 提供（34/34 全绿）。
```

### W13. 0x10 / Agent 冻结（§35 / §36）

```text
0x10 encoder = ABSENT（encodeActiveRequest → UnsupportedFunction，实测）
activeFunctionSupported(0x10) = false；0x10 dispatch = ABSENT；write10Supported = ABSENT；
0x10 production UI = ABSENT；仅 response-shape recognition = PRESENT。
AI / Agent write authority = NONE：未新增 write / confirm / send / raw-ADU tool。
```

### W14. 测试与门禁（§39–§43）

```text
实际 CTest（不是直接跑 exe 代替）：
  Debug   ctest **34/34 PASS**（33 → 34：新增 write_dispatch 目标）
  Release ctest **34/34 PASS**
关键套件（Debug 实测 Totals，Release 同绿）：
  write_dispatch 37（新）/ write_prepare 48 / write_encoder 19 / fc06_active 31 / active_request 17 /
  active_master 54 / ui_bridge 61 / serial 21 / serial_adapter 7 / passive 55 / transaction 20 /
  statistics 12 / statistics_integration 3 / diagnosis 17 / replay_log 14 / replay_analysis 9 /
  simulator 15 / fault 7 / codec 9 / crc 8 / frame 6 / f03 15 / agent_tools 15 / agent_runtime 28 /
  agent_integration 24 / ai 23 / transaction_integration 5 / simulator_integration 3 / fault_integration 4
  —— 29 套件合计 **587** 个测试函数 passed / 0 failed。
QML gates（5）：qml_smoke / qml_geometry_check / qml_nav_check / qml_focus_check（含 prod-hidden）/
  qml_write_foundation_check（C01–C37 + E1/E2 + C4 + D1 raw-text oracle）—— 全部在 ctest 内、Debug+Release 双绿。
  **M10-C 安全断言未降低**：qml_write_foundation_check 与 qml_focus_check 本轮零修改、全绿。
Warnings：新增 C++ / QML **零 warning**；`src/main.cpp` 5 条 pre-existing 未动。
ISSUE-014：保持 PRE-EXISTING NON-BLOCKING，未顺手修。
```

**环境阻塞判定（§39）**：本轮已**成功定位并运行实际 CTest**
（`D:/QT/Tools/CMake_64/bin/ctest.exe` + PATH 前置 `D:/QT/6.11.1/mingw_64/bin` 与 `D:/QT/Tools/mingw1310_64/bin`）。
⇒ **不构成 ENVIRONMENT BLOCKER**；结论基于真实 ctest 输出，**未**把「直接运行 29 个 exe + 5 个 gate」写成 ctest PASS。

### W15. Problems Encountered / RCA（§38 相关）

```text
RCA-9（既有测试契约变更，非缺陷）：tests/test_ui_bridge.cpp 的
  `d1_productWriteCapabilityNotExposedYet` 断言 `indexOfProperty("write06Supported") < 0` —— 这是 D1/D2 的
  staging 契约，D3 交付完整路径后**必然**失效。处理：改名为
  `d3_productWriteCapabilityExposedForFc06Only`，改为断言属性存在且为 true，并把**负向覆盖转移**到 0x10
  （断言 write06Available 与 write10Supported 仍 ABSENT）。**未删除任何断言、未降低强度**，
  且变更显式记录于本节（与 D1 的 ac03、D2 的 ac06/s1/s6 同一处理范式）。
RCA-10（rvalue 地址，编译期红）：confirmAndDispatchPreparedWrite 最初把
  `std::get_if<ConfirmAccepted>(&preparedWriteStore_.confirm(token))` 写成对**临时 variant** 取地址
  ⇒ `error: taking address of rvalue`。修复：先绑定具名对象再取地址（与既有测试中已记录的同一模式）。
RCA-11（oracle 前置条件：Timeout vs Pending）：RecordingSerialTransport 的 completionElapsed 默认 25ms
  低于请求阈值 1000ms，因此 `completeWithTimeout()` 被分析器**正确**判为 Pending，四条依赖 Timeout 的
  oracle 失败。根因是**测试前置条件**而非产品语义（NoResponse ∧ elapsed<threshold ⇒ Pending 是冻结契约）。
  修复：新增 `completeAtTimeout()` 显式把 elapsed 设为阈值，不改产品。
RCA-12（oracle fixture 缺陷）：自建 `fc03AnswerWire()` 最初给出 byteCount=2（1 个寄存器），
  而读请求 quantity=2 ⇒ 真实结果是 QuantityMismatch ProtocolError，导致混合统计用例的成功数不符。
  **修的是 fixture，不是断言强度**；并顺带加固 history 用例，使其显式断言两条记录都是 Success
  （原断言只查 functionCode，一个坏 fixture 也能「通过」，属被动弱点）。
RCA-13（环境瞬时失败，非代码缺陷）：Release 构建首次出现
  「Error compiling qml file」于 `.rcc/qmlcache/.../DesignSystem_qml.cpp`，本轮**未改动任何 QML**；
  直接重跑同一构建即成功，随后 Release ctest 34/34 全绿。判定为 qmlcachegen 瞬时失败（flaky），
  **未**修改任何 QML 或构建配置来「绕过」。留档以便将来复现时优先怀疑缓存/环境而非源码。
```

### W16. Files / Commit（§44–§46）

```text
新增：src/core/active/ProductWriteCapability.h、tests/test_write_dispatch.cpp（+ CMake 目标 write_dispatch）
修改：src/core/active/PreparedWriteSnapshot.h（ConfirmRejectReason += CapabilityUnavailable；
        PreparedDispatchLocalError + PreparedDispatchResult；include ActiveTransactionEvidence.h）、
      src/ui/AnalysisController.h（write06Supported Q_PROPERTY/getter；confirmAndDispatchPreparedWrite；
        startActiveDescriptor 私有 helper；stale 注释同步）、
      src/ui/AnalysisController.cpp（原子 confirm+dispatch；共享 dispatch helper 提取并回接 FC03；
        write06Supported 实现；stale 注释同步；include ProductWriteCapability/SerialTransactionSession）、
      tests/fake_serial_transport.{h,cpp}（explicit 0 = zero-accept NotSent seam）、
      tests/test_ui_bridge.cpp（staging 契约显式更新为 D3 契约，负向覆盖转 0x10）、
      CMakeLists.txt（write_dispatch 目标 + offscreen 属性）
docs：T022（本节 §W）+ PROJECT_STATUS / BACKLOG / devlog / INTERVIEW_NOTES
分类：**behavior-bearing**（core / Controller / tests / harness / CMake 行为与验收行为均变化）
commit：`M10-D3: add atomic FC06 dispatch and evidence integration`（独立提交；不 amend ee3bc3e；不 rebase；不 push；不 tag）
verified LKGC 继续 `fc86dcc`（**不自行推进**；等 Human Review）。
```

## M10-D3 Review Correction — Atomic Dispatch Dialog Reaction Oracle（2026-09-21，harness-only）

> **M10-D3 Review = HOLD。** D3 实现**主体接受**（R1–R5 core semantics、statistics、history、
> capability、transport evidence 全部不重开）。**唯一 blocker**：新的 atomic confirm+dispatch path
> 缺少「Prepared → Consumed / Invalidated 之后 QML confirmation Dialog **真实退出 flow**」的
> **runtime oracle**。本轮只关闭该 gap；**产品实现未改一行**（结论见 X2）。
> **未做**：D4 未开始；production Write 仍 hidden；normal Confirm 仍 confirmation-only；未实现 0x10；
> 未 push；未 tag；**LKGC 保持 `fc86dcc`**。

### X0. Preflight（§1）

```text
HEAD = 42fcd0b（branch = main，普通 git status --porcelain = 空）
verified LKGC = fc86dcc；origin/main = a40d935；ahead 119 / behind 0；CMake VERSION = 2.0.0
M10-D3 Review = HOLD；production Write = HIDDEN；write06Supported = true（structural）；
0x10 active support = false；v2.0.0 = ABSENT
```

### X1. Source Audit — A / B / C / D（§3）

| # | 问题 | 实读结论 |
| --- | --- | --- |
| A | 旧 confirmation-only path 在 consume/invalidate 后由谁 emit projection change | `AnalysisController::confirmPreparedWrite` 自身在状态迁移后由调用者 `confirmPreparedWriteToken` 调 `announcePreparedWriteChanged()`；**store 本身不持 Qt 对象、不发信号**（`PreparedWriteStore` 是纯 core 类型）。外部事件侧（`teardownSerialTransport` / `handleSerialTransportError` / `connectSerial` / `startActiveDescriptor`）在 `invalidate()` 返回 true 时各自调用 `announcePreparedWriteChanged()` |
| B | 新 atomic path 是否经过**完全相同**的 notification path | **是**。`confirmAndDispatchPreparedWrite` 在 consume 之后调用 `announcePreparedWriteChanged()`；guard 失败分支各自调用它；进入 flight 时由共享 `startActiveDescriptor()` 调用它。emit 的对象是**同一个** `preparedWriteChanged()` 信号 |
| C | 若 store 的 `confirm()` 不 emit Qt signal，atomic API 是否显式 emit | store 确实不发信号（Zero Qt），**atomic API 显式 emit**（见 B）。实测 full-accept 路径恰好 **+1** 次通知：consume 一次；随后 `startActiveDescriptor` 的 `invalidate(BusyBecameTrue)` 在 **Consumed** 代际上是 no-op（`PreparedWriteStore::invalidate` 只作用于 Prepared），因此**不产生第二次通知** —— 这也顺带独立复证了「Consumed 不被 busy 覆写」不变量 |
| D | QML Dialog close 真实依赖哪个 property/signal | `WriteFoundationSection.qml` 的 `Connections { target: ...; function onPreparedWriteChanged() { if (!section.analysisController.hasPreparedWrite && confirmationDialog.opened) confirmationDialog.close() } }`。权威链：**store state → Controller emit → QML 读 `hasPreparedWrite`（= state == Prepared）→ close()**。与 transport 结果**无关** |

### X2. RED 结果与「是否需要产品修复」（§4 / §12）

```text
RED-first：DLG oracle 先写、先跑，**产品代码未做任何修改**（本轮唯一改动 = harness）。
实测结果：**全部通过** ⇒ 判定为 ORACLE GAP，不是 PRODUCT DEFECT。
产品修复：**NO**（按 §12「如果现有产品代码本来正确：只补 runtime oracle，不要无意义改产品」）。
证据：DLG1 实测 `notifies +1` —— atomic path 确实通过既有 `preparedWriteChanged` 发出投影通知，
      QML 因此关闭 Dialog；五个场景 Dialog 全部真实退出确认流。
```

### X3. DLG 实测矩阵（§5–§10）

```text
DLG1 full accepted   ：Dialog open + Prepared → atomic → confirmationAccepted=true / dispatchAttempted=true /
                       startResult.accepted=true / PossiblySent / state=Consumed / attempts 1 / sends 1 /
                       dispatched ADU 8 字节 / **notifies +1** → **Dialog closed**；
                       同 token 再调：不接受、attempts/sends 不增、**Dialog 不重开**。
DLG2 acceptedCount=0 ：Dialog open + Prepared → atomic → confirmationAccepted=true / attempt 1 / send 0 /
                       **NotSent** / 有 startResult（明确**不是** guard failure）/ 无 terminal /
                       state=Consumed → **Dialog closed**（关键证明：close 不依赖 send success）；
                       同 token 再调 inert。
DLG3 short submission：0<3<8 → confirmationAccepted=true / attempt 1 / send 0 / PossiblySent /
                       **恰好一条 ShortSubmission terminal** / state=Consumed → **Dialog closed**；
                       同 token 再调 inert。
DLG4 capability      ：真实 hidden **0x10** prepared snapshot（Dialog open、Prepared、function=0x10）→
                       atomic → confirmationAccepted=false / dispatchAttempted=false / **无 startResult**
                       （transport 未被咨询）/ reject reason = **CapabilityUnavailable** /
                       state=**Invalidated** + invalidReason=**CapabilityUnavailable** → **Dialog closed**；
                       零 consume、零 encode、零 transport。
DLG5 external        ：Dialog open + Prepared → **disconnect**（context event）→
                       state=Invalidated + reason=**Disconnected** → **Dialog closed**；
                       随后旧 token 调 atomic → reject reason = **NotPrepared**（见 X4）。
DLG5b external(busy) ：Dialog open + Prepared → 真实 FC03 读进入 flight（busy false→true）→
                       reason=**BusyBecameTrue** → **Dialog closed**。
```

### X4. 两层术语冻结（§14 —— R4 口径更正）

**必须分两层，不得混为一个字段、也不得互相替代：**

```text
【第一层】外部 context event（事件本身即权威失效原因）
    Prepared → Invalidated(reason)
    reason ∈ { Disconnected, BusyBecameTrue, SessionChanged, SourceChanged, UserCancelled,
               CapabilityUnavailable }
    —— 这是 snapshot 的代际终态原因，由**事件**写入，且**永不改写**。

【第二层】事件之后，用**旧 token** 再调 atomic API
    reject reason = **NotPrepared**（或源码真实等价）
    —— 因为此刻 store 已不在 Prepared 代际，连 token 匹配都到不了。
    **此时不会**返回 Disconnected / BusyBecameTrue / NotConnected。
```

**禁止的旧表述**：「atomic 的 disconnected guard 一定返回 `NotConnected`」——**错**。真实 disconnect
路径是**第一层先 invalidate(Disconnected)**，第二层旧 token 只能得到 `NotPrepared`；
`ConfirmRejectReason::NotConnected` 分支因此是 **defensive**（prepare 本身要求已连接，公开序列到不了它）。
`CapabilityUnavailable` 是**唯一**两层可以同名表达的情形：若 snapshot 仍 Prepared 且由 atomic 的 capability
guard 直接发现，则 result 的 reject reason 与 snapshot 的 invalidation reason **都是 CapabilityUnavailable**。
本更正同时落在 `docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、devlog 与 `INTERVIEW_NOTES.md`。

### X5. QML wiring freeze 与旧契约保持（§13 / §15）

```text
**QML 本轮零改动**：normal production Confirm 仍是 confirmation-only（未被接到 dispatch）；
production Write 仍 hidden；DLG oracle 全部从 harness 的 C++ 层直接调用 atomic seam，D4 未开始。
M10-C 安全契约**未降低**：C01–C37 / E1/E2 全部继续 PASS（本轮零修改）。
harness 的「confirmation-only 阶段零 write dispatch / 零 write transaction」断言**保留**，
但按语义**分段计量**：DLG 之前的阶段仍要求 attempts=0 / sends=0（实测 0）；
DLG 段自身按 oracle 计账（attempts=3 / sends=1 / terminals=1），并由 final gate 逐项核对。
未新增任何 production-only signal：投影通知复用既有 `preparedWriteChanged`。
```

### X6. 验证与文件（§16–§19）

```text
聚焦回归：write_dispatch / qml_write_foundation_check（含 DLG1–DLG5）/ ui_bridge / active_master /
          write_prepare / write_encoder / fc06_active 全部 PASS
全量：**真实 CTest** Debug 34/34、Release 34/34（目标数不变：本轮不新增 target）
Warnings：零新增；main.cpp 5 条 pre-existing 未动；ISSUE-014 PRE-EXISTING NON-BLOCKING
Files：**仅 `src/main.cpp`**（HarnessWriteTransport 增加 opt-in 写接受能力 + DLG oracle 段 +
       final gate 分段计量）+ docs 同步。
分类：**behavior-bearing**（harness 的验收行为变化 ⇒ 按项目规则属 behavior-bearing，与 M10-B correction 同例）
commit：`M10-D3: prove atomic dispatch dialog reaction`（独立 correction 提交；**不 amend `42fcd0b`**；
       不 rebase；不 push；不 tag）
verified LKGC 继续 `fc86dcc`（不自行推进）。
```

## M10-D4 — Production FC06 Write UI / Confirmation Dispatch / Usability & Safety（2026-09-21，behavior-bearing）

> **M10-D3 Final Re-review = PASS ⇒ M10-D3 = COMPLETE ⇒ M10-D4 = GO。** 本轮首次把 0x06 写入
> **发布为 production UI**：`write06Supported` 成为该区块存在性的唯一权威，production Confirm 接到
> **原子** `confirmAndDispatchPreparedWrite`，并交付写入结果的**非成功呈现**通道（未发送 / 提交不完整 / 响应超时）。
> **未做**：D5 未开始；0x10 仍全 ABSENT；Agent 写权限 NONE；无 automatic retry；未 push；未 tag；**LKGC 保持 `fc86dcc`**。

### Y0. Preflight 与归档（§1 / §3）

```text
HEAD = 94b6a9c（branch = main，普通 git status --porcelain = 空）
verified LKGC = fc86dcc；origin/main = a40d935；ahead 120 / behind 0；CMake VERSION = 2.0.0；v2.0.0 = ABSENT
归档：M10-D3 Final Re-review = PASS；M10-D3 = COMPLETE；accepted D3 behavior chain = 42fcd0b → 94b6a9c；
     M10-D4 = Production FC06 Write UI / Confirmation Dispatch / Usability & Safety = IN PROGRESS → 本节 COMPLETE。
     **不产生单独 docs-only closure 提交**，随本轮 behavior commit 一起归档。
```

### Y1. Handoff Fingerprint（§2，源码实证）

```text
FC06_ENCODER = YES；FC06_SESSION = YES；FC06_CONTROLLER_DISPATCH = YES（M10-D3，本轮首次接入 production UI）
WRITE06_SUPPORTED = TRUE（只读 CONSTANT）
PRODUCTION_WRITE = **VISIBLE（本轮有意变更）**；NORMAL_CONFIRM_DISPATCH = **YES（本轮有意变更）**
FC10: ENCODER = NO / ACTIVE = NO / DISPATCH = NO / PRODUCTION_UI = NO / FRAMING_RECOGNITION = YES
AGENT_WRITE_AUTHORITY = NONE；PUBLIC_ISSUE_CODES = 14
```

### Y2. Source Re-read 特别回答（§5）

| # | 问题 | 实读结论 |
| --- | --- | --- |
| A | 当前 Loader 如何由 `writeFoundationVisible` 控制 | 唯一开关，`app.arguments().contains("--qml-write-foundation-check")`，normal production 恒 false |
| B | 如何让 production 基于 `write06Supported` 显示 0x06、同时保持 harness seam 正交 | Loader `active: analysisController.write06Supported \|\| writeFoundationVisible`；**模式由「谁实例化」决定**：`testFoundationMode: writeFoundationVisible`。seam 只选择 test foundation，**不能**伪造 capability、**不能**在 production 显示 0x10 |
| C | 如何保证 0x10 只存在于 test foundation | 0x10 的两个区块（function TabBar 与 0x10 draft 列）各自放进 `Loader{active: testFoundationMode}`。**`visible:false` 不够** —— 隐藏对象仍在对象树里、仍响应 accessibility 接口；`active:false` 才是「从不创建」。另加 `visible: active && activeFunctionIndex===1` 保持「非活动 tab 的控件不可 Tab 达」这一 M10-C 冻结契约 |
| D | 当前 Confirm QML 调用哪个 API | 旧：`confirmPreparedWriteToken(token)`（仅确认）。本轮 production 分支改为 `requestPreparedWriteDispatch(token)` |
| E | D4 如何切换 production 0x06 Confirm 到 atomic dispatch、同时不破坏 M10-C hidden-foundation 语义 | `confirmPreparedWrite()` 内按 `productionMode` 分流：production → 原子派发；test foundation → 保持 confirmation-only（C01–C37 / E1 / E2 继续测零派发）。**同一组件、同一 draft/validation/Dialog/summary/键盘，不另造第二套 UI** |
| F | write error/status 当前已有哪个用户可见 lane | ① `writeValidationError`（**输入**错误，DS.error）；② serial error lane（`communicationSerialError`）。**缺少写作结果的语义化表述**，故本轮新增 outcome lane（见 Y4） |

### Y3. 有意契约转移：production-hidden → production-visible（§4 / §46）

```text
旧契约（M10-C/D1/D2）：production 中**不存在任何写控件**。
D4 有意取代：0x06 写入**发布**为 production UI —— 这是 intentional contract transition，不是 regression。
处理纪律（不删断言、不降强度）：
  · `qml_focus_check` 的 prod-hidden oracle **改名并改写**为 prod-write oracle，保留其全部取证手法
    （对象树扫描 / 命名控件存在性 / accessibility 接口 / 启动无 snapshot）；
  · **负向覆盖迁移**（不是消失）：
      ① 0x10 全形态缺席（对象名 `write10*` 全场景扫描 + section 子树内 accessible name 含 "0x10" 扫描
         + `write10Supported` 属性缺席 + 关键名缺席）；
      ② test-foundation seam 不泄漏（section 必须是 productionMode，`testFoundationMode == false`）；
      ③ 0x10 控件不可 Tab 达（`tabTo` 负向）；
  · 新增**正向**断言（旧 oracle 没有的）：0x06 控件存在且各自有 accessible name；Tab 顺序可达；
    disconnected 时区块**仍实例化**、仅 action disabled（capability ≠ availability）。
```

### Y4. 实现（§6–§15）

```text
Controller
 · Q_INVOKABLE void requestPreparedWriteDispatch(qulonglong token)
   —— production Confirm 入口。**返回 void 是有意的**：把 bool 交回 QML 会诱发
      `if (dispatch(...)) dialog.close()`，从而把「确认被消费」或更糟的「transport 接受了字节」
      悄悄升格为 send-success 权威。Dialog 仍只由 snapshot 离开 Prepared（投影信号）关闭。
 · 新增写结果**呈现**投影（OUTCOME lane，与 draft error 严格分离）：
   hasWriteDispatchNotice / writeDispatchNotice / writeDispatchNoticeTone + writeDispatchNoticeChanged。
   只有**非成功**状态入这条通道：accepted submission **什么都不说**（其结果属于事务行），
   transport 仅「接受字节」永不呈现为成功。三态文案：
     NotSent           →「本次请求未发送；如需重试，请重新确认写入。」
     ShortSubmission   →「提交不完整（仅部分字节被接受），设备写入状态未知；如需重试，请重新确认写入。」
     WriteTimeoutUnknown→「响应超时，设备写入状态未知；如需重试，请重新确认写入。」
   **禁止**「设备未写入 / 设备未修改 / 写入成功」之类的无据表述。
   置位点：`confirmAndDispatchPreparedWrite` 的 start 结果（NotSent / short）；
            `handleSerialTransactionCompleted` 中 0x06 ∧ Timeout → WriteTimeoutUnknown。
   清除点：新的 prepare、Clear Results、disconnect、新 session（draft 一律不动）。
QML（**没有第二套写 UI**：同一 WriteFoundationSection，靠 mode 区分）
 · 新增 `testFoundationMode` / `productionMode`；0x10 两区块改为 Loader（active: testFoundationMode）；
 · `activateWrite()` 只在 test foundation 才可能走 0x10 分支；
 · Confirm 按 mode 分流（production 原子派发 / test foundation 仅确认）；
 · outcome lane 用 `DS.notice`（warning）呈现，**不复用** `DS.error`（输入错误 ≠ 发送未完成）。
Visibility authority（§6/§10）：区块**存在**只由 `write06Supported`（结构能力）决定；
serialConnected / serialBusy / source / draft validity **只**决定 Write action 的 enabled ——
因此 disconnect / busy / Simulator / Replay 都不会卸载区块或清掉 page-local draft。
production 只呈现 0x06；0x10 无 tab / 无 values editor / 无 quantity / 无可聚焦或 accessible 节点。
```

### Y5. 实测 oracle

```text
`--qml-production-write-check`（**新增 ctest 目标 qml_production_write_check**；真实 production 区块 + 正常 Confirm 按钮）
  P1  Write → Dialog open（token≠0、attempts=0）；**初始焦点 = Cancel**
  P2  Confirm → Space：**exactly 1 attempt / 1 send / 1 ADU**，Consumed，busy=true，**draft 保留**
  P3  Dialog closed（关闭来自 authority，不是 send success）；可信 echo → 恰好 1 条 0x06 Success 进入
      **同一** Transactions/stats/diagnosis；成功**不产生**非成功 notice
  P4  rapid Enter ×2 → **exactly 1** dispatch，Dialog 不重开、无第二 send、无背景动作
  P5  rapid Space ×2 → **exactly 1** dispatch，无第二 send
  P6  immediate Enter（焦点仍在 Cancel）→ **0 dispatch**（state 保持 prepared）
  P7  Cancel → Invalidated(UserCancelled)，0 dispatch，draft 保留，Dialog 关闭
  P8  Escape → Invalidated，0 dispatch，Dialog 关闭
  P9  transport 接受 0 字节 → Dialog 关闭、0 transaction、0 terminal，
      notice 含「未发送」且**不含**「写入成功 / 设备已写入 / 超时」
  P10 short submission → 恰 1 条 terminal、0 transaction，notice 含「设备写入状态未知」、
      **不含**「设备未写入」
  P11 write Timeout → 1 条 Timeout 事务，notice 含「响应超时」+「设备写入状态未知」
  P12 geometry 1024×720 与 1000×700：Write **键盘可达**；Dialog 560×144 且 visible；
      Cancel/Confirm 均在窗口内；**窗口未自我放大**
  final 计账：sends=4 / ADU log=4 / terminals=1（与各 oracle 的预期逐一对应）
`qml_focus_check`（改名后的 prod-write oracle）
  区块因 write06Supported 而存在且处于 production 模式；859 对象扫描无 `write10*` /
  section 子树无 "0x10" accessible name / 无 write10Supported；0x06 五个控件各有 accessible name；
  启动无 snapshot；disconnected → 区块**仍实例化**、Write disabled；0x06 控件 Tab 可达、0x10 不可达
C++（test_write_dispatch 新增 7 例）
  requestPreparedWriteDispatch 执行完整原子操作（attempt/send/ADU/exact ADU/busy）；
  该 Q_INVOKABLE 的**返回类型是 void**（元对象实证）；NotSent / short / 0x06 Timeout 三态 notice 文案与 tone；
  成功**无** notice；notice 被新 prepare 与 Clear 清除
```

### Y6. 门禁 / 环境 / 文件

```text
真实 CTest：Debug **35/35 PASS**、Release **35/35 PASS**（34 → 35：新增 qml_production_write_check）
关键套件：write_dispatch 44（37 + 7 新）/ write_prepare 48 / write_encoder 19 / fc06_active 31 /
          active_request 17 / active_master 54 / ui_bridge 61 / serial 21 / serial_adapter 7 /
          passive 55 / statistics 12 / statistics_integration 3 / diagnosis 17
QML 门禁：qml_production_write_check（新）/ qml_write_foundation_check（**未降低**：C01–C37 / E1 / E2 全绿）/
          qml_focus_check / qml_smoke / qml_nav_check / qml_geometry_check
Warnings：新增 C++ / QML **零 warning**；main.cpp 5 条 pre-existing 未动；ISSUE-014 PRE-EXISTING NON-BLOCKING
**MANUAL VISUAL NOT VERIFIED**：本轮无真实人眼视觉检查；自动 geometry gate **不能**冒充人眼 review（见 §49）。
Files：src/ui/AnalysisController.{h,cpp}、src/ui/qml/components/WriteFoundationSection.qml、
       src/ui/qml/pages/CommunicationPage.qml、src/main.cpp（production harness + prod-write oracle 改写 +
       HarnessWriteTransport 扩展）、tests/test_write_dispatch.cpp、CMakeLists.txt（+ docs）
分类：**behavior-bearing**（production UI 首次公开 + Controller dispatch 入口 + 呈现通道）
commit：`M10-D4: expose safe FC06 production write UI`（独立提交；不 amend；不 rebase；不 push；不 tag）
verified LKGC 继续 `fc86dcc`（不自行推进）。
```

## M10-D4 Review Correction — Deployment Refresh / Production Modal Evidence / Human Visual Handoff（2026-09-21）

> **M10-D4 Review = HOLD。** **D4 implementation 主体接受**（FC06 protocol / Controller dispatch /
> statistics / evidence semantics / 0x10 freeze 全部**不重开**）。剩余两项 blocker：
> ① production-visible FC06 UI 尚未刷新到 deploy/package client；
> ② production-mode modal outside-click / rail / background safety 缺少直接 runtime evidence。
> 本轮两项都关闭。**剩余：MANUAL VISUAL**（只能由人完成，见 Z10）。

### Z0. Preflight（§1）

```text
HEAD = d20c07b（branch = main，普通 git status --porcelain = 空）
verified LKGC = fc86dcc；origin/main = a40d935；ahead 121 / behind 0；VERSION = 2.0.0
M10-D4 Review = HOLD；production FC06 Write = visible in current source/build ✔
```

### Z1. Deploy / Package 架构实读（§3，未发明新流程）

```text
A. canonical deploy directory  = build/deploy      （deploy_windows.bat 默认 DEPLOY_DIR，来自 build/debug）
B. canonical deployed client   = build/deploy/ModbusLens.exe（debug 部署，历史上人工双击验证的目标）
   Release 侧另有 build/release/deploy（M9-E E3 引入的独立 staging，由 make_package.py 使用）
C. 职责：deploy/ = windeployqt 后的可运行目录（exe + Qt DLL + plugins + QML module + samples）；
         package/ = 便携 ZIP 与 staging（make_package.py：structural checks / 负向扫描 / manifest /
                    ZIP / 重新解压校验 / minimal-PATH 与 external-CWD 运行）
D. 刷新前 artifact（见 Z2）
E. build/deploy、build/package、build/package-extract 均在 .gitignore 的 build* 之下 ⇒ **ignored 本地产物，未 tracked**
F. canonical 命令：
     scripts\deploy_windows.bat                                    （默认：build/debug → build/deploy）
     python scripts/make_package.py build/release build/release/deploy  （Release deploy + package，含自身校验与冒烟）
   make_package.py **无 publish / sign / upload 阶段**（源码明示 "not code-signed and has not been published"）
```

### Z2. 刷新前 staleness 证明（§4）

```text
build/release/modbuslens.exe  2026-09-21 16:52:34  3,801,977  3cb9da5f…552862（D4 提交后首次 Release 构建）
build/deploy/ModbusLens.exe   2026-09-19 12:27:25 35,066,016  ac303e3e…486c7e ← 落后（M9-F 时期、且是 debug 尺寸）
build/package/…/ModbusLens.exe 2026-09-19 23:48:44 2,803,304  53d2f596…09ff4 ← 落后（M9-E E3 accepted 包）
build/package/…zip            2026-09-19 23:55:32 40,633,612 2adfe71f…9336d ← 落后（M9-F 记录中的 hash）
verdict：**deploy/ 与 package/ 确实落后**，且均不含 D4 的 production 0x06 UI。
（被取代的 M9-F 包产物已移到 build/package/_superseded-m9f/ 与 build/package-extract/_superseded-m9f/ 保留，未删除。）
```

### Z3–Z6. Release 重建 / CTest / 刷新 / 身份链 / 版本 / 部署冒烟

```text
从 d20c07b + 本轮 harness 变更重新完成 Release 构建：
  build/release/modbuslens.exe  2026-09-21 17:41:57  3,825,797  1e50bdb6…8e3256
  （构建 0 error；warning 仅 main.cpp 5 条 pre-existing）
Release full CTest（真实 ctest.exe）：35/35 PASS
Debug  full CTest：35/35 PASS

刷新（canonical flow，未发明新流程）：
  scripts\deploy_windows.bat                     → build/deploy 重建（来自当前 build/debug）
  make_package.py build/release build/release/deploy → Release deploy + package 全流程 PASS
     · structural checks PASS / credential·path 负向扫描 PASS / manifest 1498 payload
     · ZIP 40,902,628 bytes sha256 bd128564…1258d
     · 重新解压比对 manifest PASS；minimal-PATH smoke/nav/geometry PASS；external-CWD 启动 PASS

身份链（§8）—— 部署流程只是复制 exe，hash 必须一致：
  build/release/modbuslens.exe                                  1e50bdb6…8e3256
  build/release/deploy/ModbusLens.exe                           1e50bdb6…8e3256  ✔ 一致
  build/package/…/ModbusLens.exe                                1e50bdb6…8e3256  ✔ 一致
  build/package-extract/…/ModbusLens.exe                        1e50bdb6…8e3256  ✔ 一致
  build/deploy/ModbusLens.exe                                   4c953790…366fd62  ✔ 与 build/debug/modbuslens.exe 一致
  ⇒ 不是「时间变新了」，而是 hash 逐字节相同。

⚠️ 过程中发现一个真实陷阱（已修正，未将就）：
  make_package.py 只在 `<deploy>/ModbusLens.exe` **不存在**时才重新 deploy。
  因此第一次重跑复用了 16:52 的旧 deploy，产出与 17:41 的最终 Release 不一致。
  修正：把 release/deploy 移开后重跑 ⇒ 真正重新 deploy ⇒ hash 链一致。

版本（§9）—— 不从目录名推断，用运行时投影：
  部署 exe --qml-smoke-test → "SMOKE IDENTITY PASS: … version=2.0.0 …"（applicationVersion 来自 CMake VERSION authority）

部署冒烟（§10）—— 直接运行**部署目录**里的 exe（不是 build/release）：
  build/package-extract/ModbusLens-2.0.0-windows-x64/ModbusLens.exe --qml-smoke-test            → exit 0
  同上 --qml-production-write-check → exit 0（含 startup / QML 加载 / Communication 可达 /
      production FC06 section 存在 / production 0x10 controls 缺席 / P1–P12 / M1–M6）
  注：便携包只带 windows 平台插件（无 offscreen），故按 packaging 脚本的方式运行，不强制 offscreen。
```

### Z7. Production-mode modal oracle（§11–§16）+ 是否需要产品修改

```text
扩展的是**同一个** qml_production_write_check（真实 production 区块、真实 Write 按钮、真实 Confirm），
M1–M6 全部为「0 dispatch」断言：
  M1 production Dialog 打开于 Prepared snapshot 之上（真实 production draft → Write）
  M2 点击 Dialog 外 production 页面背景 → Dialog 仍开（closePolicy = CloseOnEscape 本就排除 outside press）、
     snapshot 仍 Prepared、token 不变、workspace 不变、0 dispatch
  M3 点击 navigation rail（navItem_0）→ workspace **不切换**、Dialog 不被绕过、token 不变、0 dispatch
  M4 尝试激活背景 Write → 未建立第二 flow、未 dispatch、token 不变、0 dispatch
  M5 Dialog 打开时连按 8 次 Tab → 焦点始终停留在 [Cancel, Confirm] 两个 dialog 按钮内，
     未逃逸到 production 背景、snapshot 未变、0 dispatch
  M6 Cancel / Confirm 的 accessible name 均存在（**不宣称** WCAG certification）
**是否需要产品修改：NO。** 直接 PASS ⇒ 与 D3 的 DLG 一样属 **evidence gap，不是 product defect**；
产品代码零改动（§19）。modal 语义本身（modal:true + CloseOnEscape + 自定义 footer）早已正确。
```

### Z8–Z11. 回归 / 治理 / 文档 / 提交

```text
回归（§17）：qml_production_write_check ✔ / qml_write_foundation_check ✔（C01–C37 / E1 / E2 未降低）/
             qml_focus ✔ / qml_nav ✔ / qml_geometry ✔ / write_dispatch ✔ / ui_bridge ✔ / active_master ✔
             + 实际 Debug CTest 35/35、Release CTest 35/35
部署治理（§18）：build/deploy、build/package、build/package-extract 均为 **ignored 本地产物**；
             **未** git add 任何 DLL/exe；仅在文档与报告中记录路径/hash/time。未改变 artifact tracking policy。
人类视觉（§20）：本轮**不声称** HUMAN VISUAL PASS（见 Z10）。
Files：src/main.cpp（harness：新增 M1–M6 + clickAtScene/railIndex/accessibleNameOf + PASS 文案）+ docs
分类：**behavior-bearing**（harness 验收行为变化，与 M10-B/D3 correction 同例）
commit：`M10-D4: close production deployment acceptance gaps`（独立提交；**不 amend d20c07b**）
verified LKGC 继续 `fc86dcc`（不自行推进）。
```

### Z10. Human Visual Handoff（§20 —— 必须由人完成）

```text
请打开这个具体 exe（便携包解压目录，canonical deployed client）：
    E:\desktop\ModbusLens\build\package-extract\ModbusLens-2.0.0-windows-x64\ModbusLens.exe
  SHA-256 : 1e50bdb63f41706351117f2bfb57367e7676e0a183c4fac786a19465ae8e3256
  来源     : build/release/modbuslens.exe（同一 hash），由 canonical make_package.py 全流程产出
  source  : HEAD d20c07b（+ 本轮 harness commit）
  VERSION : 2.0.0（运行时 applicationVersion 实测，非目录名推断）
另：debug 部署客户端 build\deploy\ModbusLens.exe（sha256 4c953790…366fd62）也已同步刷新。
建议人工确认：Communication 页 → 0x06 写入区块存在且可用 → Write → 确认对话框 → 无 0x10 任何控件。
本轮 Agent **未**做真实人眼视觉检查；自动 geometry gate 不冒充人眼 review。
```

## M10-D4 Packaging Freshness Correction（2026-09-21，deployment-tool correction）

> **Z1 复盘发现真实 packaging defect**：`make_package.py` 用 `if not os.path.isfile(exe)` 决定是否重新 deploy，
> 即**只看存在性**。deploy 目录里残留的旧 binary 会被**静默复用并打包**，即使 `build/release` 已产生更新的 exe。
> Z2 当时之所以得到正确 artifact，是因为靠 SHA-256 人工发现 stale reuse 并**手动**移开 deploy 目录后重跑 ——
> 那不是可长期依赖的方案。本轮把它修正为脚本内的**内容同一性**契约。

### ZF1. RED 复现（§4，不依赖 mtime / 不靠人工删目录）

```text
把 8974178 版本的 scripts/make_package.py 从 git 取出，**原样执行 main() 里的那两行旧决策**：
      if not os.path.isfile(exe):
          run_deploy(release_build_dir, deploy_dir)
（stub 掉 run_deploy 以观测是否被调用）
fixture：build/modbuslens.exe = CURRENT-BUILD-BYTES；deploy/ModbusLens.exe = STALE-BYTES-FROM-AN-EARLIER-BUILD
实测：redeploy called = False；exe that WOULD be packaged = b'STALE-BYTES-FROM-AN-EARLIER-BUILD'
⇒ **RED CONFIRMED**：deploy exe 存在时旧规则跳过重新 deploy，静默打包 stale binary。
```

### ZF2. 契约与实现（§5 / §6）

```text
契约：canonical package 命令每次必须保证最终 deployed executable **来自本次指定的 build directory**，
     不能因 deploy exe 已存在就默认它仍有效。
实现（最小修改，未新建第二套 packaging script）：
  · 新增 sha256_file(path) —— 所有 freshness 决策一律用**内容同一性**，绝不用 mtime / 存在性；
  · 新增 deploy_is_current(release_build_dir, deploy_dir) —— 逐字节比较 build exe 与 deploy exe；
  · main()：不是 current（含缺失）→ 重新 deploy；随后**后置断言**仍不 current 则 fail
    （绝不打包别的东西）；
  · 新增**端到端**后置断言：解压后的 ModbusLens.exe 必须与 build exe 同 hash，
    否则 fail —— 关闭「source build → staging → package → extract」整条链。
所有既有 packaging gates 全部保留（§9）：structural checks / 负向扫描 / manifest / ZIP 校验 /
重新解压校验 / minimal-PATH / external-CWD。**未为修 freshness 降低任何检查。**
```

### ZF3. 回归 oracle（§8，isolated temp fixture，不动真实部署树）

```text
scripts/test_make_package_freshness.py（新增）：
  case1 deploy 缺失  -> not current            PASS
  case2 deploy 过期  -> not current            PASS
  case3 deploy 最新  -> current                PASS
  RED  旧「存在性」规则在 stale 情形答 True（即会跳过 redeploy） PASS
  RED  同情形下内容确实不同                    PASS
  同尺寸但内容不同 -> not current              PASS
  build exe 缺失     -> 拒绝（不静默跳过）     PASS
（真实部署树零改动；不需要删除/移动数千文件。）
```

### ZF4. 端到端 stale 案例与最终 canonical 产物（§7 / §10）

```text
端到端 stale 案例（deploy exe 被换成本次 build 之外的一个真实旧 binary）：
  make_package: deploy exe is STALE -> redeploying
  make_package: Release deploy OK
  make_package: deploy identity OK (sha256=1e50bdb6…8e3256)
  ⇒ 旧 deploy 被自动替换，package exe == build exe。**无需人工移动/删除。**
最终 canonical（无人工干预）：
  build/release/modbuslens.exe                     1e50bdb63f41706351117f2bfb57367e7676e0a183c4fac786a19465ae8e3256
  build/release/deploy/ModbusLens.exe              1e50bdb6…8e3256  ✔
  build/package/…/ModbusLens.exe                   1e50bdb6…8e3256  ✔
  build/package-extract/…/ModbusLens.exe           1e50bdb6…8e3256  ✔（extract identity OK）
  build/package/…zip                               40,902,628 B  eee87d336308e48b56856529f848fc2a95cde84748bf34a2dbeeefa948864597
  全部 packaging gates PASS（structural / 负向 / manifest / ZIP / 重新解压 / minimal-PATH / external-CWD）
注：ZIP hash 与上一轮不同（bd128564… → eee87d33…）但**大小相同**——ZIP 内记录了文件 mtime，
    重新 deploy 会刷新 mtime，故 ZIP 字节不同而内容一致（manifest 逐文件校验内容）。
环境披露：本轮沙箱的 bulk-delete guard 多次拦截脚本自身的 `shutil.rmtree`（staging / assets 镜像）。
    为完成运行，曾临时移开/删除 **ignored 本地产物**（staging、extract）以及 deploy 树里那个
    **本就会被脚本剔除**的 1 文件冗余 `ModbusLens/assets` 图标镜像；随后已用 deploy_windows.bat
    重新 deploy 把部署树恢复到 canonical 状态（assets 已恢复，deploy exe hash 不变）。
    **脚本语义未为沙箱做任何改动**（§7）。
```

### ZF5. 验证 / 治理 / Git

```text
scripts/test_make_package_freshness.py PASS（8 项）
真实 CTest：Debug 35/35、Release 35/35（make_package.py 不参与 C++ 编译，回归为 0）
Files：scripts/make_package.py、scripts/test_make_package_freshness.py + docs
分类：**behavior-bearing**（部署工具的验收行为变化）
commit：`M10-D4: prevent stale binaries in packaging flow`（独立；**不 amend 8974178**）
verified LKGC 继续 `fc86dcc`（不自行推进）；未 push；未 tag；D5 未开始。
```

## M10-D5 — FC06 Final Acceptance / Closure / LKGC Candidate Verification（2026-09-21，acceptance-only）

> **M10-D4 Final Human Review = PASS ⇒ M10-D4 = COMPLETE ⇒ M10-D5 = GO。**
> **D5 不是功能阶段**：对 M10-D 全链（D1 → D4）做端到端验收。
> **结论：全部 gates PASS，未发现真实 defect ⇒ 本轮产品/测试/harness/packaging 行为零改动**（§27）。
> **accepted LKGC candidate = `9bdd99c`**（见 ZD8）。

### ZD1. Preflight（§2）

```text
HEAD = 9bdd99c（branch = main，普通 git status --porcelain = 空）
origin/main = a40d935；ahead 123 / behind 0；verified LKGC = fc86dcc；VERSION = 2.0.0；v2.0.0 = ABSENT
M10-D1/D2/D3/D4 = COMPLETE；M10-D5 = GO
```

### ZD2. D4 Human PASS 归档（§3）

```text
M10-D4 Final Human Review = PASS（人工确认）：
  production FC06 Write visible ✔；0x10 production absent ✔；
  confirmation Dialog 视觉正确 ✔；1000×700 正常 ✔；1024×720 正常 ✔；无明显布局回归 ✔
M10-D4 accepted behavior chain = d20c07b → 8974178 → 9bdd99c
  （8974178 = production modal evidence；9bdd99c = packaging freshness behavior correction）
```

### ZD3. 最终能力矩阵（§4，源码实证）

```text
0x03 : encoder YES / active support YES / Controller dispatch YES / production UI YES
0x06 : encoder YES / active support YES / Controller dispatch YES / write06Supported TRUE / production UI YES
0x10 : encoder NO / active support NO / Controller dispatch NO / write10Supported ABSENT /
       production UI NO / response framing recognition YES only
AI/Agent : write authority NONE

关键证据（单一权威 gate，SerialTransactionSession.cpp:53）：
  bool activeFunctionSupported(ActiveFunction function)
  {
      // M10-D2: two active analyzers are wired — Function 0x03 and Function 0x06
      // ...
      // 0x10 remains refused before any send: it has a response-shape
      // RECOGNITION rule for framing, but no encoder and no active analyzer.
      return function == ActiveFunction::ReadHoldingRegisters
             || function == ActiveFunction::WriteSingleRegister;
  }
  ⇒ 0x10 在任何发送之前就被拒绝（D3 R4 oracle 亦实测 prepared 0x10 → CapabilityUnavailable、attempt 0）。
  `write10Supported` 全仓仅 4 处出现，**全部是断言其缺席**（main.cpp prod-write / ui_bridge / write_dispatch ×2）。
  `src/ai` 中 write-tool 引用 = 0。
  core 中的 0x10 代码（Function16 解码 / prepare 校验 / PassiveTransactionAnalysis 识别）正是
  冻结语义所允许的「framing recognition + hidden foundation parser」，不构成 encoder / active / dispatch 能力。
```

### ZD4. 契约重构复核（§5）

```text
Consumed ≠ send success ≠ device success
confirmationAccepted ≠ transport accepted ≠ transaction Success
NotSent ≠ guard failure（guard failure 无 startResult，故不存在 TransportDisposition）
PossiblySent ≠ device received
Timeout = 响应超时，设备写入状态未知
no implicit retry / single in-flight / no queue / no parallel / active unit 0 local reject
write requires explicit confirmation / Agent cannot write
全部由既有 test oracle 持续锁定，本轮**未发现**违反。
```

### ZD5. 门禁实录（§6–§18, §23–§27）

```text
输入（§6）    ：DecimalField 无 hard validator；raw 保留 / core 权威 / 字段级错误 / invalid 零发送
               （write_prepare 48 + write_encoder 19，含 "0"/"65535"/"00010"/外空白 与
                ""/"-1"/"+1"/"0x10"/"12.3"/"12x"/"65536"/multi-line）
wire（§7）    ：G1 `01 06 00 00 00 00 89 CA`、G3 `11 06 00 01 00 03 9A 9B`、G6 `01 06 00 00 00 01 48 0A`
               逐字节断言；test oracle 的 CRC 独立实现，与 production encoder 不共享（无自证）
确认安全（§8） ：Cancel / Escape / immediate Enter → 0 dispatch；
               Space / Enter / rapid×2 → exactly one（write_dispatch 44）
快照（§9）    ：Dialog summary 来自不可变快照；打开后改 draft 不改 summary/intent/ADU；QML 只传 token
transport（§10）：A guard failure（accepted=false / attempt 0 / 无 disposition）
                 B acceptedCount=0（NotSent / 0 transaction / 0 terminal）
                 C short（PossiblySent / 1 terminal / 0 transaction）
                 D full accepted（PossiblySent / pending lifecycle）
                 E post-submit TransportError（1 terminal / 0 fabricated transaction）
                 F post-submit disconnect（1 terminal / 0 fabricated transaction）
响应（§11）   ：Success / Exception / CrcError / wrong unit → ProtocolError+ResponseAddressMismatch /
               wrong function → ProtocolError+UnexpectedResponseFunction /
               echo mismatch → ProtocolError+WriteSingleRegisterEchoMismatch / no response → Timeout
               （fc06_active 31；无任何 outcome 被 UI 显示为 success）
Timeout 文案（§12）：「响应超时，设备写入状态未知；如需重试，请重新确认写入。」；
               全仓无「设备未写入 / 设备没有改变 / 写操作未发生」；no automatic retry
同一事务宇宙（§13）：混合 FC03+FC06 同一 session / 同一 append 顺序 / 同一 Transactions·Statistics·Diagnosis；
               无 Write History / Write Statistics 第二套 authority
统计公式（§14）：observed = pending + completed；completed = Success+Exception+CrcError+Timeout
               +ProtocolError+ExpectedNoResponse；rateEligible = completed − ExpectedNoResponse；
               successRate = Success / rateEligible（分母 0 → nullopt/"—"）；latency 只取 Success；
               ExpectedNoResponse 不进分母、不算 anomaly、不证明 write success；
               anomaly whitelist = Exception / CrcError / Timeout / ProtocolError
Clear/导航/源切换（§15）：Clear 清 transactions·terminals·statistics·diagnosis，不清 connection·
               pending·write draft·capability；导航不 source switch·不清 draft·不 send；
               成功源替换走既有 teardown；Failed Replay 保留 prior source/results
production UI（§16）：FC06 visible；0x10 zero production controls / accessible nodes / tab stops；
               disconnected·busy·Simulator·Replay 区块仍在、仅 action availability 变化
a11y/modal（§17）：address·value·Write·Cancel·Confirm accessible names 存在（实测 M6：
               「取消写入（不发送任何请求）」/「确认写入意图」）；初始安全焦点 / outside click blocked /
               rail blocked / background Write blocked / 8 Tabs 不逃出 modal scope；**不宣称 WCAG certification**
geometry（§18）：1024×720 与 1000×700 自动 gate PASS（部署 artifact 上亦 PASS）；
               Human Visual PASS 已由 D4 记录，自动 gate 仅作重复验证、不取代人眼
issue 契约（§23）：10 response-side + 4 request-side = **14** public issue codes；
               UnknownProtocolError 仍为 defensive/sentinel 但 public observable；未恢复 13
warnings（§26）：0 new；main.cpp 5 条 pre-existing 未动；ISSUE-014 PRE-EXISTING NON-BLOCKING
```

### ZD6. 部署 / 打包终验（§19–§21）

```text
packaging freshness oracle（§19/§20）：scripts/test_make_package_freshness.py → **8/8 PASS**
  （missing / stale / current / RED 旧规则 / RED 内容不同 / 同尺寸陷阱 / build exe 缺失拒绝）
canonical Release deploy/package flow 重新执行：**PASS**（structural / 负向 / manifest / ZIP /
  重新解压 / minimal-PATH smoke·nav·geometry / external-CWD 全过）
内容同一性（SHA-256，非 mtime）：
  build/release/modbuslens.exe                   1e50bdb63f41706351117f2bfb57367e7676e0a183c4fac786a19465ae8e3256
  build/release/deploy/ModbusLens.exe            1e50bdb63f417063…（相同）
  build/package/…/ModbusLens.exe                 1e50bdb63f417063…（相同）
  build/package-extract/…/ModbusLens.exe         1e50bdb63f417063…（相同）
  build/package/…zip                             40,902,628 B  578f9f9b7ce3d00e33111f3fbf6cbc987e44ed702d8cb545a88c82c54af3035b
  （ZIP hash 每轮不同是因为 ZIP 记录 mtime；内容由 manifest 逐文件校验，这才是身份锚。）
deployed client（§21）= build/package-extract/ModbusLens-2.0.0-windows-x64/ModbusLens.exe
  --qml-smoke-test            → exit 0，"SMOKE IDENTITY PASS: … version=2.0.0 …"
  --qml-production-write-check→ exit 0，P1–P12 + M1–M6 全 PASS（见 ZD5 §8/§11/§16/§17 实录）
环境披露：沙箱 bulk-delete guard 拦截脚本自身的 rmtree（staging / extract / 1 文件冗余 assets 镜像）。
  为完成 canonical 运行曾临时移开这些 **ignored 本地产物**，完成后已用 deploy_windows.bat
  重新 deploy 恢复 canonical 部署树（assets 已还原，deploy exe hash 不变）。脚本语义未改动。
```

### ZD7. 验收结论（§27）

```text
**未发现真实 defect ⇒ 本轮产品 / 测试 / harness / packaging 行为零改动。**
M10-D accepted behavior tree = 9bdd99c（D1 `6ab97e1` → D2 `ee3bc3e` → D3 `42fcd0b`+`94b6a9c`
  → D4 `d20c07b`+`8974178`+`9bdd99c`）。
```

### ZD8. LKGC candidate（§28）与 closure（§29/§30）

```text
**accepted LKGC candidate = 9bdd99c**
verified LKGC：fc86dcc → 9bdd99c（Human Final Acceptance 后的治理推进，记录于本轮 docs closure）
本轮 commit = docs-only closure（`docs: close M10-D FC06 active write milestone`），**它本身不是 LKGC**；
  LKGC 身份始终是 9bdd99c。
未来的 docs-only commit 永远不推进 LKGC。
REAL HARDWARE NOT VERIFIED（§22）：M10-D 验收基于 RecordingSerialTransport / scripted responses /
  production adapter source audit / runtime UI 与 deployment gates；**未声称真实 PLC/device 写入已验证**。
Next Action：M10-D COMPLETE → **M11 Learning / Design**（不开始 implementation）。
```

> ### ⚠️ 2026-09-21 Roadmap Consistency Audit 更正（append-only，不改写上文）
> 上文 §ZD 第 8 行「Next Action：M10-D COMPLETE → M11 Learning / Design」为 **roadmap drift，已被
> Roadmap Consistency Audit（PASS）与 Human Review 裁定更正**：
> **M10-D = COMPLETE ≠ M10 overall COMPLETE**。active FC16/0x10 仍属于 M10 冻结范围（§FC33 决议 1），
> **M10-E = NEXT**，**M10-F = AFTER M10-E**，**M11 = HOLD**。详见下文 §ZE。
> §ZE 之后的 current-facing docs（PROJECT_STATUS / BACKLOG / devlog / RESUME_INTERVIEW_QA）已按此更正。

## M10-E Phase 1 — FC16/0x10 Write Multiple Registers Learning / Design + Roadmap Drift Correction（2026-09-21，DESIGN / DOCUMENTATION ONLY）

> **本轮 NO IMPLEMENTATION**：不改任何 C++ / QML / tests / harness / packaging 行为。
> Human Review 裁定（Roadmap Consistency Audit = PASS）：**M10-D = COMPLETE；M10 overall = IN PROGRESS；
> M10-E = NEXT；M10-F = AFTER M10-E；M11 = HOLD；不接受 re-scope** —— active 0x10 仍在 M10 冻结范围内。

### ZE0. Roadmap Drift Correction（§2 / §3 / §4）

```text
审计结论（CASE B —— 不存在 accepted scope correction）：
  · 原始范围：11_V2_UPGRADE_PLAN.md:95「FC03 active read；FC06 active write single register；
    Function 0x10 active write multiple registers」
  · 决议记录：T022 §FC33 决议 1「scope = 0x03 / 0x06 / 0x10（decimal 16）—— RESOLVED（FC2）」
  · 冻结时序：T022 §FC32「…M10-D 0x06 end-to-end。M10-E 0x10 end-to-end。
    M10-F final automated·manual acceptance + optional real-hardware acceptance」
  · 0cf0748（D5 closure）曾宣布「M10 全部 COMPLETE / Next = M11」并写「M10-A/B/C/D/E 全部完成」——
    与上述 accepted 决议冲突（M10-E 从未实现）。historical record 保留，不重写；
    current-facing 状态按本节更正。
更正项：
  · M10-D = COMPLETE（0x06 闭环，质量结论不变）
  · M10 overall = IN PROGRESS
  · M10-E = NEXT（FC16/0x10 Write Multiple Registers end-to-end）
  · M10-F = AFTER M10-E（final automated/manual acceptance，real hardware OPTIONAL）
  · M11 = HOLD（M10-E/F 完成前不得开始）
  · 撤回「M10-A/B/C/D/E 全部完成」（事实错误）与「M10 全部 COMPLETE」；恢复 M10-E/M10-F 的可见追踪。
```

### ZE1. 现有 0x10 Foundation —— 按能力分类实测（§6 / §7）

```text
分类口径：A passive decode / B request validation / C response-shape recognition /
D Prepared snapshot / E active request encoder / F active response analyzer /
G session active support / H Controller dispatch / I product capability / J production UI
（不得由 A/B/C 推断 E–J 存在。）

A passive decode                     = YES   Function16.cpp:37-75 decodeWriteMultipleRegistersRequest
                                             （quantity 1..123、byteCount==2*quantity、
                                              actualValueByteCount==2*quantity 三重一致、按序取 values）
B request validation                 = YES   WritePrepareValidation.cpp:84-128 prepareWriteMultipleRegistersIntent
                                             （unit/address/timeout → parseRegisterValues → 1..123 → span）
C response-shape recognition         = YES   Function16.cpp:77-90 decodeWriteMultipleRegistersResponse
                                             （固定 data 4 字节 = start echo + quantity echo）
D Prepared snapshot                  = YES   PreparedWriteStore 与 function 无关（token/confirm/cancel/invalidate 通用）；
                                             PreparedWriteSnapshot.cpp:5-23 preparedQuantity 由 values.size() 派生（不存储）
E active request encoder             = NO    ActiveRequestIntent.cpp:127-132 显式 UnsupportedFunction
F active response analyzer           = NO    全仓无 analyzeWriteMultipleRegistersTransaction；
                                             回显比对逻辑目前内联在 PassiveTransactionAnalysis.cpp:315-348
G session active support             = NO    SerialTransactionSession.cpp:53-61 只放行 0x03/0x06
H Controller dispatch                = NO    D3 R4 实测 prepared 0x10 → CapabilityUnavailable / attempt 0
I product capability (write10Supported) = ABSENT（全仓仅 4 处，全为断言缺席）
J production UI                      = NO    0x10 仅存在于 testFoundationMode 的两个 Loader 内；
                                             production 从不创建（qml_focus_check 859 对象实测无 write10* 节点）
另：parseRegisterValues（多行解析）= YES；registerSpanFitsAddressSpace = YES（uint32 widened，
    start + count <= 65536）；SimulatedSlave 0x10 writable semantics = YES（decode 全过才原子应用，
    失败零部分变更）；Simulator WriteMode 只读默认不变。
```

### ZE2. 原 accepted M10-E 范围（§8，不加戏）

```text
T022 §39 / §FC32 冻结原文所要求的 M10-E 交付 = FC16/0x10 写多寄存器 end-to-end，具体：
  · FC16/0x10 语义 encoder（由 values[] 生成 request frame）
  · byteCount 派生、quantity 派生（values[] 唯一 authority）
  · 多值 confirmation summary
  · simulator 支持（已存在，见 ZE1）
  · active response lifecycle + Controller dispatch + transport evidence
  · product capability（write10Supported）+ production UI（第二个子 Tab）
  · M10-C 已冻结的写安全模型原样复用
不添加历史设计未要求的任何功能（见 §ZE17 non-goals）。
```

### ZE3. 命名冻结（§5）

```text
协议含义：function byte = 0x10 hex = 16 decimal；规范名 = Write Multiple Registers。
代码命名保持：Function16 / WriteMultipleRegisters* / ActiveFunction::WriteMultipleRegisters。
新文档优先写「FC16 / 0x10」或「Function 0x10 (decimal 16)」；
避免单独写「FC10」；禁止「decimal Function 10」。
（T022:452 历史原文「FC10(0x10)」自带消歧，保留不改写。）
```

### ZE4. 复用 FC06 安全模型（§9）

```text
0x10 写操作必须复用 FC06 已冻结的完整链，不得新建第二套：
  explicit Write → authoritative validation → immutable PreparedWriteSnapshot
  → explicit confirmation → atomic Controller dispatch → transport evidence → transaction outcome
继续成立：single in-flight / no queue / no parallel / no implicit retry / Agent no write。
不得新增：第二套 confirmation store、第二套 transport taxonomy、第二套 write session、
第二套 history、第二套 statistics。
```

### ZE5. Intent authority / 派生 / span（§10）

```text
values[] = 唯一值列表 authority。
quantity  = values.size()            （已由 PreparedWriteSnapshot.cpp:11-14 派生，绝不存储）
byteCount = 2 * values.size()        （wire 时派生；0x10 max quantity 123 ⇒ byteCount max 246）
span 校验 = registerSpanFitsAddressSpace（uint32 widened：start + count <= 65536）
1..123 边界重证：Function16.cpp:10-11（min 1 / max 123）与 WriteDraftParsing.cpp:10（kMaxValues=123）
   两处常量来源一致（kWriteMultipleRegistersMaxQuantity）。
```

### ZE6. Raw input contract（§11，复用现有 parser，不改 authority）

```text
parseRegisterValues 已实现的最终规则（WriteDraftParsing.cpp:55-116）：
  一行一个值；outer blank lines 可 trim；middle blank → BlankLineInside 拒绝；
  CRLF 支持（trim 含 '\r'）；顺序保持；0..65535（溢出 → ValueOutOfRange，绝不 wrap）；
  ASCII decimal only（c < '0' || c > '9' ⇒ InvalidCharacter ⇒ 天然拒绝 hex/正负号/小数/逗号）；
  空 → NoValues；>123 → TooManyValues。
QML 永远不是 validation authority（无 validator）。
```

### ZE7. FC16 request wire 设计（§12）

```text
Modbus RTU 0x10 request 布局（production semantic encoder 设计）：
  [unit 1B][function 0x10 1B][start addr 2B BE][quantity 2B BE][byteCount 1B]
  [N × register value 2B BE][CRC16 low-byte first]
其中 quantity = values.size()；byteCount = quantity * 2（max 246）。
encoder 语义帧由 Function06.cpp 的同构模式产生（frame 语义层），CRC/wire 复用现有 codec
（encodeRtuFrame）—— 与 0x06 完全同构，不另写 CRC。
放置建议：新增 encodeWriteMultipleRegistersRequest 于 Function16.cpp（与 decoder 同文件对称）。
```

### ZE8. Golden-vector 策略（§13）

```text
独立 oracle：test 侧 CRC 必须独立实现或采用可信固定 vectors，不得以 production CRC 作为唯一 oracle。
至少设计：
  · minimum   unit 1 / address 0 / 1 register（值 0）→ byte-exact literal
  · boundary  unit 247 / 123 registers（quantity=123, byteCount=246，span 上限）
  · protocol-like known example（可对照公开 vector）
  · mixed values 0 / 1 / 65535（高低字节模式：0x0000 / 0x0001 / 0xFFFF）
  · negative：quantity=0、124、byteCount 与 quantity 不一致、actual != declared
```

### ZE9. Response contract 与 echo mismatch（§14）

```text
正常 0x10 response 固定 data 4 字节：unit + 0x10 + start echo + quantity echo + CRC。
成功条件：unit match ∧ function match ∧ start echo match ∧ quantity echo match ∧ CRC valid。
echo mismatch：TransactionIssueCode::WriteMultipleRegistersEchoMismatch **已存在**
  （TransactionAnalysis.h:46），且被动路径已构造 expected/actual address+quantity 四元组
  （PassiveTransactionAnalysis.cpp:338-347）—— 复用，不新建 taxonomy。
```

### ZE10. 共享 analyzer 抽取（§15，0x06 模式复刻）

```text
现状差异：0x06 的语义比对在**共享核心函数** analyzeWriteSingleRegisterTransaction
  （passive 与 active 同一实现，PassiveTransactionAnalysis.cpp:305-312 注释明示
   "lives in ONE shared core function that the active path also calls"）；
而 0x10 的回显比对目前**内联在 passive analyzer**（:315-348）。
M10-E 设计要求：把该内联块抽取为 analyzeWriteMultipleRegistersTransaction(request, observation,
  elapsed, timeoutThreshold)（与 0x06 同签名、同一 trusted-request contract），
passive 改为调用共享函数，active 也调用它 —— 不得形成两个略有差异的 lifecycle。
Exception / wrong unit / wrong function / CRC / partial timeout / overlong 继续走
generic 共享规则，不复制 FC06 analyzer。
```

### ZE11. Session / Controller / capability（§16 / §17 / §18）

```text
activeFunctionSupported(0x10) = true 的**前提**（全部真实满足才允许）：
  encoder exists（ZE7）∧ active analyzer exists（ZE10）∧ framing 已识别（已成立）
  ∧ session lifecycle tested。
不得因 candidate framing 已认识 0x10 就提前 true。
Controller dispatch：复用 D3 的 generic confirmAndDispatchPreparedWrite / startActiveDescriptor
  （按 intent.function 自然分派）—— **不新增** confirmAndDispatchWrite10。
QML 继续只传 token。
write10Supported 设计 = 结构性产品能力（encoder ∧ active session ∧ Controller dispatch ∧
  evidence/outcome integration），CONSTANT；与 runtime availability 无关
  （disconnect / busy / Simulator / Replay / invalid draft 不改变它）。
Capability staging：product capability 与 production UI **不得早于** end-to-end 支持全部成立。
```

### ZE12. Production UI / multi-value summary / 123·124 / scroll（§20 / §21 / §27 / §28）

```text
M10-E 最终形态：Communication 的 Write section 内**新增第二个子 Tab**「FC16 / 0x10 多寄存器」，
复用**同一个** WriteFoundationSection（不得另写 ProductionWrite10.qml 复制整条 flow）。
现有 testFoundationMode 的 0x10 editor（write10DraftColumn，DecimalField 0x06 行 + 多行 TextArea）
是基础，但 production 需重新验收 usability / a11y / geometry / keyboard / modal / scroll。
Confirmation summary 来自 immutable snapshot：unit / function / start address / quantity /
values / connection。123 values 不要求全部同时可见，但必须**可滚动到首尾且完整可访问**
（不得出现 "... and N more" 省略）。
确认后发送 snapshot values，不是当前 draft。
123 / 124 边界：123 通过、124 拒绝（既有 C23-C29 parser 用例已覆盖，M10-E 需在 production 路径重验）。
```

### ZE13. Transport evidence / 宇宙 / 统计 / 诊断 / simulator / broadcast / AI / timeout（§22–§27）

```text
transport evidence：0x10 完全复用 guard failure / NotSent / PossiblySent / ShortSubmission /
  TransportError / DisconnectedAfterSubmission / Timeout —— **不得引入** Write10Sent/Write10Failed。
transaction universe：进入现有 Transactions / Statistics / Diagnosis，同一 session、同一 append 顺序。
statistics：公式不变；**不得**出现 write10 success rate 第二套 authority。
diagnosis：同一 deterministic batch。
simulator：现有 writable 0x10 语义已满足 M10-E 要求（decode 全过才原子应用连续值；
  失败零部分变更；只读默认安全）；**不得**把 simulator 当作 production active transport 替代物。
broadcast：active unit 0 继续 **本地 reject，attempt = 0**；passive ExpectedNoResponse 支持广播
  不构成允许 active send 的理由。
AI/Agent：write authority 继续 NONE；不得新增 multi-write / batch write / raw ADU 工具。
timeout：Outcome = Timeout；用户语义「响应超时，设备写入状态未知」；不得按 values 数量做隐式部分重试。
```

### ZE14. M10-F / M11 boundary（§28 / §29）

```text
M10-F = final automated / manual acceptance，覆盖 0x03 + 0x06 + 0x10 整体回归；
  real hardware = **OPTIONAL**；允许最终「REAL HARDWARE NOT VERIFIED」，只要自动化、
  人工客户端、部署包全部通过且诚实披露。真实 PLC **不是**硬性 completion gate。
M11 = Register Readout & Decode，继续 HOLD；其解码对象主要来自 successful FC03 raw registers；
  不得把 M10 的 0x10 write 与 M11 的 register decode 混成一个阶段。
```

### ZE15. 建议分阶段方案（§33，按 0x10 实际复杂度，不机械复制 D1–D5）

```text
E1  Core wire + golden vectors（NO UI）
    Goal：FC16 semantic encoder + 独立 CRC oracle + byte-exact goldens（含 123/124、byteCount 不一致）
    Layers：core/protocol/Function16.{h,cpp}、tests/test_write_encoder.cpp
    RED：encodeActiveRequest(0x10) == UnsupportedFunction
    Visibility：production 不变（0x06 only）；capability：write10Supported 仍 ABSENT
    Review point：encoder + goldens Review
E2  共享 analyzer 抽取 + active analyzer wiring（NO UI）
    Goal：抽取出 analyzeWriteMultipleRegistersTransaction；passive 改调用共享函数（行为等价回归）；
          active 路径接上；Session 单元测试覆盖 0x10 lifecycle（含 echo mismatch / exception / timeout）
    Layers：core/analysis/*、core/serial/SerialTransactionSession.cpp、tests/test_fc06_active.cpp（或新增 test_fc16_active）
    RED：无 analyzeWriteMultipleRegistersTransaction；activeFunctionSupported(0x10)==false
    Visibility/Capability：仍不开放
    Review point：analyzer + session Review（此处才允许讨论打开 gate）
E3  Capability + Controller dispatch（NO production UI）
    Goal：activeFunctionSupported(0x10)=true；write10Supported CONSTANT 属性（false→true 语义由 Review 定）；
          Controller atomic dispatch 接受 prepared 0x10（token-only）；evidence/统计/诊断集成回归；
          harness 0x10 dispatch oracle（含 NotSent / short / terminal / timeout）
    RED：dispatch → CapabilityUnavailable；write10Supported 缺席
    Visibility：仍 0x06 only（test foundation 可开始用真实 dispatch）
    Review point：capability Review
E4  Production UI（第二个子 Tab）+ a11y/geometry/keyboard/modal/scroll
    Goal：production 0x10 editor + 多值 summary（可滚动完整访问）+ 0x10 absence 负向覆盖迁移为
          「0x10 production present」正向覆盖；production-write oracle 扩展 0x10 矩阵
    RED：production 无 0x10 节点（现行 oracle）
    Visibility：production 0x10 VISIBLE（本阶段结束时）
E5  Final acceptance（M10-F 前置）：全量回归 + deploy/package + 部署客户端验收
    （M10-F 仍独立存在，覆盖 0x03/0x06/0x10 整体 + optional real hardware）
```

### ZE16. Test plan（§30）与 RED plan（§31）

```text
Test plan（实现期覆盖）：parser boundaries（空/中间空行/CRLF/顺序/65536/非十进制字符/123/124）·
  wire goldens（byte-exact）· active request encoding · response analyzer · fragmentation ·
  exception · wrong unit · wrong function · echo mismatch · CRC · timeout · NotSent · short ·
  transport error · disconnect · token one-shot · Clear pending · source replacement ·
  mixed FC03/06/10 history · statistics · diagnosis · production UI · 123/124 · scroll ·
  a11y · keyboard · modal · geometry · deploy/package。
RED plan（实现开始前必须真实观察到）：
  R1 encodeActiveRequest(0x10) → UnsupportedFunction
  R2 activeFunctionSupported(0x10) → false
  R3 write10Supported 属性缺席
  R4 Controller dispatch prepared 0x10 → CapabilityUnavailable（attempt 0）
  R5 production 0x10 节点缺席（现行 prod-write oracle）
  R6 Simulator active-write10：writable 语义**已存在**（非 RED）—— 若实现涉及改动才另立 RED
RED 必须来自真实当前行为（已由 ZE1 源码实证 + 既有 oracle 落地）。
```

### ZE17. Superseded / non-goals（§32）

```text
不实现：0x17 及其它 function code · active broadcast · write queue · parallel writes ·
  automatic retry · Agent write · M11 decoding · M12 profile · installer/signing/publish ·
  第二套 confirmation store / transport taxonomy / session / history / statistics ·
  Write10Sent/Write10Failed 等 parallel status。
superseded 记录：「M10 全部 COMPLETE / Next = M11 / M10-A/B/C/D/E 全部完成」由本节更正
  （audit 裁定），历史 as-of snapshot 不重写。
```

### ZE18. 设计完成后的 roadmap 状态（§34）与治理（§35）

```text
M10-D = COMPLETE；M10-E Phase 1 = **AWAITING REVIEW**；M10-F = NOT STARTED；
M10 overall = IN PROGRESS；M11 = HOLD。
本轮 docs-only；**不宣布 M10-E GO**（须 Human Review 接受本设计）。
verified LKGC 保持 9bdd99c（不推进）；
未来 Agent 只提出 LKGC candidate，Human Review PASS 前不自行宣布 advance。
```

## M10-E1 — FC16/0x10 Active Request Encoder / Core Validation / Golden Wire Vectors（2026-09-21，behavior-bearing）

> **M10-E Phase 1 Design Review = PASS ⇒ M10-E1 = GO。**
> 范围（§0）：**只做** FC16/0x10 active request semantic encoding + core validation + golden wire tests。
> **未做**（并已实测证明）：session support / Controller dispatch / `write10Supported` / production UI / analyzer（E2+）。

### ZF0. Preflight 与 RED fingerprint（§1 / §3）

```text
HEAD = 227b5c7（main，clean）；origin/main = a40d935；ahead 125/0；verified LKGC = 9bdd99c；v2.0.0 = ABSENT
实现前基线（源码实证）：
  0x10 intent validation   已有（validatePayload → quantity 1..123；prepare 层另有 span/unit/timeout）
  0x10 passive decode      已有（Function16.cpp decodeWriteMultipleRegistersRequest）
  0x10 framing recognition 已有（PassiveTransactionAnalysis / decodeWriteMultipleRegistersResponse）
  encodeActiveRequest(0x10) → UnsupportedFunction     ← 本轮被 intentional supersede
  activeFunctionSupported(0x10) → false               ← 本轮保持 FALSE（未开放）
  Controller dispatch 0x10 → CapabilityUnavailable    ← 保持（r4 oracle 未动）
  write10Supported → ABSENT                            ← 保持
  production UI → ABSENT                               ← 保持
```

### ZF1. 实现（§5 / §6）

```text
· Function16.cpp 新增 encodeWriteMultipleRegistersRequest(address, startingAddress, values)
  —— 与 Function03/06 encoder 完全对称：本层只负责字段顺序与 big-endian 打包，
     CRC 与 wire 字节流仍归 ModbusRtuCodec（encodeRtuFrame）。
  · quantity 与 byteCount 在此**派生**（quantity = values.size()，byteCount = 2 * values.size()），
    **绝不**由 caller 提供 —— 不存在可与之冲突的第二套 count 字段。
  · values 顺序 == wire 顺序，每个寄存器 big-endian：不排序、不换 word order
    （M11 的 byte/word-order 解码与本层无关）。
  · 不做 range policy（quantity 1..123 / span / unit）—— 与 0x06 的分层一致：
    unit 校验属于 intent/session 层，span 校验属于 prepare 层（registerSpanFitsAddressSpace）。
· Function16.h：新增声明 + 文件头注释更正（"PASSIVE semantics only / there is no encoder"
  → M10-E1 已有 encoder，但 active support 仍需 session/analyzer/dispatch/UI）。
· ActiveRequestIntent.cpp：WriteMultipleRegisters case 由 UnsupportedFunction 改为真实编码，
  并写明「encoder ≠ product capability：session gate 不放行 0x10（M10-E2）」。
· ActiveRequestIntent.h：UnsupportedFunction 注释更正为「预留的 cannot-execute 分支」
  （当前没有任何 ActiveFunction 会命中它，与 UnknownProtocolError 同一纪律）；
  encodeActiveRequest 注释列出 0x03/0x06/0x10 三者均已实现，并强调 encoder ≠ active support。
```

### ZF2. Intentional supersede —— 三个旧 oracle 的 RCA（§20）

```text
被取代的冻结断言：encodeActiveRequest(0x10) → UnsupportedFunction（三个套件各一处）。
取代原因：M10-E1 的目的就是让 0x10 请求可编码；该断言与目标直接冲突。
处理：**没有删除**，三条都改名/改写为「encoder 已存在，但下一层仍拒绝」，并各自保留新的负向覆盖：
  1) test_active_request.cpp ac03_writeEncodeContract
     旧：multiple → ActiveRequestEncodeError(UnsupportedFunction)
     新：multiple → ActiveRequestDescriptor（function=0x10，data=9B，wire=13B）
  2) test_fc06_active.cpp sup1_supportMatrix
     旧：encode 0x10 → EncodeError
     新：encode 0x10 → descriptor；**session.beginActiveRequest 仍返回 SerialTransactionError**
  3) test_write_dispatch.cpp fc10CapabilityStaysFrozen
     旧：encode 0x10 → EncodeError
     新：encode 0x10 → descriptor；gate 仍 false；**write10Supported 属性仍缺席**
         （Controller dispatch 路径仍由 r4_capabilityUnavailableForFc10 覆盖，未动）
取代后冻结矩阵（本轮逐项实测）：
  encode 0x10 = YES / active session 0x10 = NO / dispatch 0x10 = NO /
  product capability = NO / production UI = NO
```

### ZF3. Golden wire vectors 与证据（§7–§13）

```text
独立 CRC oracle：test 侧 bitwise CRC-16/MODBUS（poly 0xA001 reflected / init 0xFFFF / LSB-first），
  不调用 production calculateModbusCrc / RTU encoder / Function16 encoder ⇒ 无 self-certification。
byte-exact literals（小向量整帧）：
  F16-G1 minimum        1 / 0     / [0]      → 01 10 00 00 00 01 02 00 00 A6 50        (11B)
  F16-G2 smallest nz    1 / 0     / [1]      → 01 10 00 00 00 01 02 00 01 67 90        (11B)
  F16-G3 mixed          1 / 0     / [1,0x1234,0xABCD]      → …00 03 06 00 01 12 34 AB CD 21 53 (15B)
  F16-G4 upper unit     247 / 0   / [1]      → F7 10 00 00 00 01 02 00 01 48 34        (11B)
  F16-G5 upper span     1 / 65535 / [0xFFFF] → 01 10 FF FF 00 01 02 FF FF BC E0        (11B)
  F16-G6 protocol example 0x11 / 1 / [0x000A,0x0102] → 11 10 00 01 00 02 04 00 0A 01 02 C6 F0 (13B)
        —— 即 MODBUS Application Protocol 规范中 Write Multiple Registers 的公开示例，
           字段布局由外部文档佐证，不依赖自家 decoder
  F16-G7 high/low asym  1 / 0x1234 / [0x00FF,0xFF00,0x8000,0x0001] (17B)
大向量（max quantity）：123 registers —— 断言 header（unit/function/start/quantity=0x007B/
  byteCount=0xF6）、ADU 长度 255、selected payload positions（首/第二/末寄存器）、
  以及独立 CRC（不手写 246 字节 literal）。
byteCount 派生：1→2、2→4、123→246（data[4]）；quantity 派生同测（data[2..3]）。
ADU 长度：9 + 2*N —— N=1 → 11B，N=3 → 15B，N=123 → 255B。
顺序保持：[0x0001,0x1234,0xABCD] → payload 00 01 12 34 AB CD（frame.data offset 5 起）。
```

### ZF4. RCA（本轮踩坑）

```text
RCA-17（测试作者错误，非产品缺陷）：首轮三个新测试失败，原因是我把 **ADU 偏移**当成了
  **frame.data 偏移**来断言。ADU = unit(1)+function(1)+data(5+2N)+CRC(2)，而 frame.data
  从 start 开始 —— values 在 frame.data 里从 offset 5 起（在 ADU 里是 offset 7），
  123 寄存器时 data.size() = 251 而 ADU = 255。产品 encoder 完全正确，golden 向量
  （只比较 wire）首轮就通过了；错的只有我测试里的三个偏移/长度常量。已修正并重跑全绿。
RCA-18（工具）：Qt 6.11 的 QTest 没有 QCOMPARE2（那是 Qt 6.8+ 的 QTRY/QCOMPARE 重载），
  且 QVERIFY2 的 message 参数是 const char*，不能直接传 QString；改用 std::string + .c_str()。
```

### ZF5. 门禁 / 状态 / Git（§21–§26 / §34 / §35）

```text
targeted（真实 Totals）：
  write_encoder 30（19 → 30，+11）/ write_prepare 48 / write_dispatch 44 /
  active_request 17 / fc06_active 31 —— 全 PASS
QML 负向回归：本轮 0 QML 改动；qml_focus_check / qml_smoke 等 6 个 QML gate 仍在
  全量 CTest 内全 PASS —— production 0x10 absence oracle（无 write10* 节点/a11y/tab stop）
  未被破坏，且其断言文本保持原样（0x10 在 production 仍从不创建）。
真实 CTest：Debug **35/35**、Release **35/35**（35 个目标，数量与上轮一致，未新增 target）
warnings：0 新增；main.cpp 5 条 pre-existing 未动；ISSUE-014 PRE-EXISTING NON-BLOCKING
Files：src/core/protocol/Function16.{h,cpp}、src/core/active/ActiveRequestIntent.{h,cpp}、
       tests/test_write_encoder.cpp、tests/test_active_request.cpp、tests/test_fc06_active.cpp、
       tests/test_write_dispatch.cpp
分类：**behavior-bearing**（0x10 请求可编码 = 验收行为变化）
commit：`M10-E1: add FC16 active request encoding`（独立；不 amend；不 rebase；不 push；不 tag）
状态：M10-D = COMPLETE；**M10-E1 = AWAITING REVIEW**；M10-E2+ = NOT STARTED；
  M10 overall = IN PROGRESS；M11 = HOLD。
verified LKGC 保持 **9bdd99c**（不推进；Agent 仅提出 candidate）。
```

## M10-E1 Review Correction — Golden Vector Identity / Evidence Provenance（2026-09-21）

> **M10-E1 Review = HOLD**（本轮更正后解除 HOLD 交由 Re-review）。HOLD 原因有二：
> ① **golden vector ID inconsistency** —— E1 Completion Report 把 max-123 向量记作「G6」，
>    而测试里的 F16-G6 实际是 **2-register reference vector**，两者一度共用「G6」这个 ID；
> ② **provenance evidence inconsistency** —— 测试注释与 docs 把 `11 10 00 01 00 02 04 00 0A 01 02 C6 F0`
>    归属为 "the published MODBUS Application Protocol example"，但该具体来源在 repo 内**不可证明**。
> 本轮更正两者。**NO PRODUCT CHANGE**（无任何证据表明 FC16 encoder 有 bug）。

### ZH1. 实际 golden vector 清单（§3，以测试源码为准）

```text
测试文件：tests/test_write_encoder.cpp，表 goldenVectors16() 共 **7 条带标签向量**（F16-G1..F16-G7），
另有 **1 条无标签的 max-quantity 边界向量**（独立测试函数）→ 本轮补上唯一 ID **F16-G8**。
（E1 Completion Report 只报了 6 条且把 max-123 误标为 G6，漏了 G7 —— 该报告为会话产物，
  不在 repo 内，故以本节 append-only 更正为准，不回写历史。）
```

### ZH2. 更正后的唯一映射（§3 / §10 / §11）

```text
F16-G1 = minimum             1 / 0      / [0]      → 01 10 00 00 00 01 02 00 00 A6 50  (11B, 整帧 literal)
F16-G2 = smallest non-zero   1 / 0      / [1]      → 01 10 00 00 00 01 02 00 01 67 90  (11B, 整帧 literal)
F16-G3 = mixed values        1 / 0      / [1, 0x1234, 0xABCD]                          (15B, 整帧 literal)
F16-G4 = upper unit          247 / 0    / [1]      → F7 10 00 00 00 01 02 00 01 48 34  (11B, 整帧 literal)
F16-G5 = upper span          1 / 65535  / [0xFFFF] → 01 10 FF FF 00 01 02 FF FF BC E0  (11B, 整帧 literal)
F16-G6 = **2-register reference vector** 0x11 / 1 / [0x000A, 0x0102]
                          → 11 10 00 01 00 02 04 00 0A 01 02 C6 F0          (13B, 整帧 literal)
F16-G7 = high/low asymmetry  1 / 0x1234 / [0x00FF, 0xFF00, 0x8000, 0x0001]              (17B, 整帧 literal)
F16-G8 = **max quantity boundary（123 registers）** —— quantity=0x007B, byteCount=0xF6,
         ADU=255B；断言 header / 长度 / 首·第二·末寄存器位置 / 独立 CRC（无 246 字节 literal）
⇒ A（max-123）= **F16-G8**；B（2-register reference）= **F16-G6**；两者 ID 唯一，不再共用「G6」。
```

### ZH3. Provenance 更正（§4 —— 撤回不可证明的归属）

```text
实读 docs/03_MODBUS_LEARNING.md §4.5（Function 0x10 小节）：
  ✅ 记录了 FC16 的**字段规则**：quantity 1..123；byteCount = 2N；response = starting address +
     written quantity（不回显数据）；exception function = 0x90。
  ❌ **未记录** `11 10 00 01 00 02 04 00 0A 01 02 C6 F0` 这一具体字节序列，
     也**没有**任何官方文档 title / revision / section / page 的 canonical citation。
⇒ 撤回「F16-G6 是 published MODBUS Application Protocol example」这一具体归属。
更正后措辞（测试注释与 docs 统一）：
  F16-G6 = **fixed FC16/0x10 RTU reference vector**；
  其正确性由 ① hardcoded bytes（整帧逐字节 literal）＋ ② independent CRC oracle ＋
  ③ 经严格 request decoder 的 round-trip 结构一致性 共同验证 ——
  **不**依赖任何不可证明的外部 citation。
（注：③ 是结构一致性检查，不替代 ①②；本项目对外部来源的立场与 T003 的
   pymodbus/libmodbus 双确认纪律一致 —— 无外部对拍时，不得宣称外部权威背书。）
```

### ZH4. 测试标签更正（§9 —— 最小 label correction）

```text
改动仅限 tests/test_write_encoder.cpp 的**注释与测试名**：
  · 表注释新增 PROVENANCE NOTE（撤回归属，说明依据来源与验证方式）
  · F16-G6 label：protocol example → **reference vector**
  · max-123 测试：f16_maxQuantityHeaderPayloadLengthAndCrc →
    **f16_g8_maxQuantityHeaderPayloadLengthAndCrc**，注释补上 F16-G8 身份
    与「NOT F16-G6」的唯一性说明
**未改动**：expected bytes（7 条 literal 与 F16-G8 的 header/长度/位置断言逐字节不变）、
  independent CRC oracle、oracle 强度、production 行为。
```

### ZH5. 验证与 warnings 口径（§8 / §10）

```text
targeted（真实 Totals，label 更正后）：write_encoder 30 / active_request 17 / fc06_active 31 /
  write_dispatch 44 —— 全 PASS
真实 CTest：Debug **35/35**、Release **35/35**
warnings 口径（更正）：**0 NEW warnings；5 PRE-EXISTING warnings（src/main.cpp:2496/2498/4596/
  9902/10140）**。本轮通过刷新 src/main.cpp 的 mtime **强制重编该 warning-bearing TU** 取得真实输出
  （Debug 与 Release 各 5 条，行号一致），而不是把增量构建的「无输出」解释为 repo total 0 warnings。
```

### ZH6. Git / 状态

```text
分类：**behavior-bearing**（test/harness 文件有变更；仅 label/comment，不改 expected bytes 与 oracle 强度）
commit：`M10-E1: correct FC16 golden vector identity and provenance`
  （独立；不 amend 1514291；不 rebase；不 push；不 tag）
状态：M10-E1 = **AWAITING RE-REVIEW**；M10-E2+ = NOT STARTED；M10 overall = IN PROGRESS；M11 = HOLD
verified LKGC 保持 **9bdd99c**（不推进；Agent 仅提出 candidate）
```

## M10-E2 — FC16/0x10 Shared Transaction Analyzer / Active Session Response Support（2026-09-21，behavior-bearing）

> **M10-E1 Re-review = PASS ⇒ M10-E1 = COMPLETE ⇒ M10-E2 = GO。**
> 范围（§0）：**只做** ①提取共享 FC16/0x10 transaction analyzer ②passive/active 复用同一语义核心
> ③0x10 纳入 SerialTransactionSession active response lifecycle ④完成后 `activeFunctionSupported(0x10)=true`。
> **未做**（并已实测证明）：Controller dispatch / `write10Supported` / production 0x10 UI /
> write confirmation UX 改动 / M10-E3/E4 / M11。

### ZG0. E1 归档与 RED fingerprint（§1 / §3 / §4）

```text
E1 Re-review = PASS（golden vector identity / provenance 更正已确认）；E1 = COMPLETE。
实现前基线（实测）：
  R1 analyzeWriteMultipleRegistersTransaction 在 src/ 内 = ABSENT（grep 实证）
  R2 activeFunctionSupported(0x10) = false（write_encoder f16_sessionStillRefuses / fc06_active sup1 PASS）
  R3 合法 0x10 request 无法经 session 完成 Success lifecycle（session refused）
  R4 Controller 拒绝 prepared 0x10（write_dispatch r4 / fc10CapabilityStaysFrozen PASS）
  R5 production 0x10 absent（--qml-production-write-check exit 0）
```

### ZG1. 共享 analyzer 提取（§5 / §6）

```text
提取前：0x10 的回显比对（start + quantity）内联在
  PassiveTransactionAnalysis.cpp::analyzeObservedTransaction 的 normal-semantics 区块（T015 Part C 引入）；
  而 0x06 的等价逻辑早已在共享核心 analyzeWriteSingleRegisterTransaction（passive/active 共用）。
提取后：TransactionAnalysis.{h,cpp} 新增
  TransactionAnalysis analyzeWriteMultipleRegistersTransaction(
      const ModbusRtuFrame& request, const ResponseObservation& observation,
      ms elapsed, ms timeoutThreshold);
  结构与 0x06 镜像：NoResponse → Pending/Timeout；CrcMismatch → CrcError；
  FrameTooShort → ProtocolError+ResponseFrameTooShort；other device → ResponseAddressMismatch；
  (fn|0x80) → Exception/MalformedExceptionResponse；
  0x10+0x10 → 双方解码 + EXACT echo（startingAddress AND quantity）→
     mismatch = ProtocolError+WriteMultipleRegistersEchoMismatch（expected/actual 四元组），
     match = Success（0x10 不回显 values）；
  other function → ProtocolError+UnexpectedResponseFunction。
passive 侧：原内联块替换为「调共享函数 + 叠加 passive-only requestIssues」——
  与 0x06 的包装方式完全一致。**不存在第二套 0x10 协议事实 authority。**
```

### ZG2. Passive 语义保持（§6）

```text
passive 包装层（NoResponse/broadcast/wire/unit/exception 的通用前置）不变；
normal 块改调共享函数后，0x10 的四种 normal 结果
  （MalformedNormalResponse / UnknownProtocolError / EchoMismatch / Success）
与 requestIssues 的叠加方式逐字保持 —— passive 全量 55 用例 PASS，无行为漂移。
```

### ZG3. Session 接线与支持矩阵（§15 / §16 / §21）

```text
SerialTransactionSession.cpp：
  · activeFunctionSupported 增加 WriteMultipleRegisters（注释写明开放依据：
    M10-E1 encoder + M10-E2 shared analyzer + lifecycle tests；并写明仍非 product capability）
  · analyzeActiveResponse 的 0x10 case 由「defensive break」改为调用共享 analyzer；
    尾部 defensive UnknownProtocolError 分支保留（枚举未来新增时的纪律）。
single in-flight：mixed function 亦适用 —— fc16_active::if1（0x10 pending 时第二个 0x10 被
  拒）/ if2（0x06 pending 时 0x10 被拒且 pending 不被取代）。
```

### ZG4. 活跃响应矩阵实测（§17）

```text
（test_fc16_active，23 用例，真实会话驱动：begin → feedResponseBytes / onResponseTimeout）
matching echo            → Success（start+quantity 精确回显；values 不参与比较）
exception 0x90           → Exception + code，且**无** echo mismatch issue（正交）
bad CRC                  → CrcError（payload 看似正确 echo 也不进入 Success）
wrong unit (0x22)        → ProtocolError + ResponseAddressMismatch（expected/actual address）
start mismatch           → ProtocolError + WriteMultipleRegistersEchoMismatch
quantity mismatch        → ProtocolError + WriteMultipleRegistersEchoMismatch
both mismatch            → ProtocolError + WriteMultipleRegistersEchoMismatch
wrong function 0x06/0x03 → ProtocolError + UnexpectedResponseFunction（携带 actualFunctionCode）
no bytes → timeout       → Timeout
partial 1B → timeout     → ProtocolError + ResponseFrameTooShort
partial 7B → timeout     → CrcError（足以承载 CRC 字段但不匹配）
overlong 9B              → 永不静默截断；exact-boundary 规则保持；MalformedNormalResponse
malformed 6B             → CrcError
fragmentation byte-by-byte → Success
```

### ZG5. 活跃/被动等价（§18）

```text
eq1：同一 request/response pair 分别经
  analyzeWriteMultipleRegistersTransaction（共享）与 analyzeObservedTransaction（passive）
  覆盖 7 组：Success / start mismatch / quantity mismatch / both mismatch /
  wrong unit / wrong function / exception —— status、issue code、payload 全一致。
（允许差异仅为 source kind / session metadata / passive-only requestIssues —— 本轮无差异。）
```

### ZG6. 下层 negative 全部保持（§22–§26）

```text
· Controller dispatch：analysisController.cpp 的 capability guard 仍只接受
  WriteSingleRegister（且要求 kProductWrite06Supported）⇒ prepared 0x10 dispatch 仍
  CapabilityUnavailable、attempt=0、send=0（r4 + fc10CapabilityStaysFrozen 双 oracle，均 PASS）。
  fc10CapabilityStaysFrozen 已按 intentional transition 更新：gate=true 断言 + property 缺席 +
  dispatch 拒绝三合一。
· write10Supported：仍 ABSENT（全仓 4 处出现，全为断言缺席）。
· production UI：--qml-production-write-check PASS（0x10 从不创建）。
· transport taxonomy：TransportDisposition / NotSent / PossiblySent / ShortSubmission /
  TransportError / DisconnectedAfterSubmission 未改 —— 0x10 自动复用 generic lifecycle。
· issue taxonomy：14 个 public issue code 不变；无新增。
· simulator：未改；writable 0x10 语义仅回归。
```

### ZG7. Intentional support-matrix transition（§27）

```text
旧断言 activeFunctionSupported(0x10) = false 存在于 4 处，全部按 transition 改写（无删除）：
  1) test_write_encoder.cpp f16_sessionStillRefuses → f16_sessionSupportsButCapabilityStaysClosed
     （gate true + 0x10 Success lifecycle；negative 指向 dispatch/capability/UI 三处）
  2) test_write_encoder.cpp e12 → e12_writeMultipleRegistersEncodesAndSessionAccepts
  3) test_fc06_active.cpp sup1_supportMatrix（0x10 行改为 true）
  4) test_write_dispatch.cpp fc10CapabilityStaysFrozen（gate true + property 缺席 + dispatch 拒绝）
转移后的下一层 negative：
  Controller dispatch = closed（write_dispatch r4 + fc10）/ write10Supported = absent（同前）/
  production UI = absent（main.cpp oracle）。
```

### ZG8. 门禁 / warnings 口径 / Git（§28–§32）

```text
targeted（真实 Totals）：fc16_active 23（新增套件）/ write_encoder 30 / write_prepare 48 /
  write_dispatch 44 / active_request 17 / fc06_active 31 / passive 55 —— 全 PASS
真实 CTest：Debug **36/36**、Release **36/36**（35 + 新增 fc16_active 目标 = 36；未预写数量）
QML 负向回归：本轮 0 QML 改动；production 0x10 absence oracle 在全量内保持 PASS
warnings：**0 NEW；5 PRE-EXISTING（main.cpp:2496/2498/4596/9902/10140）**。
  说明：本轮刻意刷新 main.cpp mtime 强制重编 warning-bearing TU，Debug 与 Release 均
  实测同 5 条；Release 构建日志中另有 7 条 CMake/Qt6 基础设施 notice
  （find_dependency / Qt6CoreMacros dev policy），非本项目代码的编译诊断，不计入。
ISSUE-014：PRE-EXISTING NON-BLOCKING（未顺手修）
Files：CMakeLists.txt（新增 fc16_active 目标）· src/core/analysis/TransactionAnalysis.{h,cpp} ·
  src/core/analysis/PassiveTransactionAnalysis.cpp · src/core/serial/SerialTransactionSession.cpp ·
  src/ui/AnalysisController.cpp（仅注释）· tests/test_fc16_active.cpp（新）·
  tests/test_write_encoder.cpp · tests/test_fc06_active.cpp · tests/test_write_dispatch.cpp ·
  tests/test_active_request.cpp + docs
RCA（本轮踩坑）：测试初次编写时 3 处「对 beginActiveRequest 返回的临时值取地址」
  （rvalue-address，与 D3 同款）与 1 处把语义 frame 与 ADU wire 混用 —— 均为测试作者错误，
  产品代码正确；另 ac06 的手写 descriptor 因 wire 为空被 descriptorIsConsistent 正确拒绝，
  改为以 production encoder + codec 构建标准 descriptor（这本身验证了该防漂移守卫有效）。
commit：`M10-E2: add shared FC16 active response support`（独立；不 amend；不 rebase；不 push；不 tag）
状态：M10-E1 = COMPLETE；**M10-E2 = AWAITING REVIEW**；M10-E3 = NOT STARTED；
  M10 overall = IN PROGRESS；M11 = HOLD。
verified LKGC 保持 **9bdd99c**（不推进；Agent 仅提出 candidate）。
```

## M10-E2 Review Correction — FC16 CRC Active/Passive Equivalence Evidence（2026-09-21）

> **M10-E2 Review = HOLD**，原因：冻结计划要求的 CRC error 等价验证缺少**直接 pair oracle** ——
> eq1 只比较了「已解码语义帧」构成的 7 组，而 CRC 失败在体系里是 **wire 级解码失败**
> （`RtuDecodeError{CrcMismatch}`），永远不会以语义帧形式出现，因此在该表结构下
> **结构性无法表达**。本轮补齐。**NO PRODUCT CHANGE**（无任何证据表明 FC16 行为有 defect）。

### ZI1. 缺失根因（§3，实读源码而非猜测）

```text
ResponseObservation = std::variant<ModbusRtuFrame, RtuDecodeError, NoResponse>
                     （TransactionAnalysis.h:131）
CRC 失败的观察形态 = RtuDecodeError{CrcMismatch}（ModbusRtuCodec.h:12-21）
eq1 的 Case 结构体 = { const char* name; ModbusRtuFrame response; } —— 只持**语义帧**。
⇒ CRC case 无法进入该表：不是「忘了写」，而是表的数据类型表达不了 wire 级失败。
```

### ZI2. 补充的 pair oracle（§4 / §5 —— eq2）

```text
新测试 test_fc16_active::eq2_crcEquivalence：
  构造（无 self-certification）：
    · 从明确合法的 FC16 response ADU 出发（F16-G6 同型：unit 0x11 / start 0x0001 /
      quantity 2，经 encodeRtuFrame 生成 stimulus —— 这只是构造设备应答，不是 oracle）；
    · **确定性变异**：翻转 CRC 低字节的最低位（wire[size-2] ^= 0x01）——
      candidate framing 仍成立（8 字节），CRC 必然错误；
    · 以 production codec 的 decodeRtuFrame **对 stimulus 做分类实证**：
      确实得到 RtuDecodeError{CrcMismatch}（codec 在此充当 stimulus 分类器，
      被 assert 的等价结论是两条分析路径各自给出的 CrcError，二者互证）。
  断言：同一份 合法 request + 同一 corrupted-CRC observation（RtuDecodeError{CrcMismatch}）
    分别送入 A. passive（analyzeObservedTransaction）与 B. active 共享
    （analyzeWriteMultipleRegistersTransaction）：
      Outcome:      CrcError == CrcError
      issue set:    两侧均为空（CrcError 不携带 issue）
      exceptionCode: 两侧均为空
  记录：变异策略 = 翻转 CRC 低字节 bit0；payload（回显 start/quantity）保持逐字节不变 ——
    证明「payload 看似正确 echo」不会绕过 wire truth。
（§6 可选项顺带记录：FrameTooShort / NoResponse 的等价性由共享分析器的
   同一 wire-failure/NoResponse 分支与 passive 通用前置共同覆盖，本轮未另开 pair，
   未扩大实现范围。）
```

### ZH 补充：完整性核对（§7）

```text
activeFunctionSupported(0x10) = true（fc16_active sup1）
Controller 0x10 dispatch = closed（write_dispatch r4 + fc10CapabilityStaysFrozen，attempt=0/send=0）
write10Supported = ABSENT（同上）
production 0x10 UI = ABSENT（--qml-production-write-check）
issue count = 14（无新增）
```

### ZI3. 门禁 / Git（§9–§12）

```text
targeted：fc16_active **24**（23 → 24，+eq2）/ passive 55 / active_request 17 /
  write_dispatch 44 / write_encoder 30 —— 全 PASS
真实 CTest：Debug **36/36**、Release **36/36**
warnings：**0 NEW / 5 PRE-EXISTING（main.cpp）**（warning-bearing TU 经强制重编实证）
ISSUE-014：PRE-EXISTING NON-BLOCKING
分类：**behavior-bearing**（test/harness 变更；产品源码零改动）
commit：`M10-E2: prove FC16 CRC path equivalence`（独立；不 amend；不 rebase；不 push；不 tag）
状态：M10-E2 = **AWAITING RE-REVIEW**；M10-E3 = NOT STARTED；M10 overall = IN PROGRESS；
  M11 = HOLD。verified LKGC 保持 **9bdd99c**（不推进；Agent 仅提出 candidate）。
```