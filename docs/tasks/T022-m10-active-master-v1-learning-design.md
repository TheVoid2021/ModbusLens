# T022 — M10 Active Master v1 — Learning / Design Gate

> **状态：IN PROGRESS — Phase = Learning / Design（docs-only）→ Phase 1 Review = HOLD → **Correction 已完成**（四个安全契约 A–D 闭环 + 12 项 decision requests 全部落为 Review 决议）；Implementation = NOT STARTED（等待 Re-review）。**
> verified LKGC = `aa2f3db`（M9-F closure 后的 accepted behavior tree）；M9 = ✅ COMPLETE（不重开）。
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
