# T022 — M10 Active Master v1 — Learning / Design Gate

> **状态：IN PROGRESS — Phase = Learning / Design（docs-only）；Implementation = NOT STARTED。**
> verified LKGC = `aa2f3db`（M9-F closure 后的 accepted behavior tree）；M9 = ✅ COMPLETE（不重开）。
> 本轮**未修改** src / QML / CMakeLists.txt / scripts / tests / assets / samples / screenshots；未创建 tag；未 push。
> 上游边界：M9 已冻结的 IA（五 workspace + Device disabled + Legacy retired + 默认 Transactions + navigation presentation-only）、
> M9-E 的 version/PE/icon/package 契约、M9-F 的 focus/accessibility baseline **全部继续冻结**；M10 不得顺手改 focus visual / NavigationRail / packaging / StatisticsOverview。

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