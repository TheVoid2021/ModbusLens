# T022 — M10 Active Master v1 — Learning / Design Gate

> **状态：M10-A = ✅ COMPLETE（accepted behavior tree `b7a6151`，verified LKGC）。**M10-B = FC03 Unified Contract Migration：Review = HOLD → Correction（selection-on-append QML runtime oracle）已实施，等待 M10-B Final Re-review**（IN PROGRESS）。**
> verified LKGC = **`b7a6151`**（2026-09-20，M10-A Final Re-review PASS 后的最终 accepted behavior tree）；历史：`aa2f3db`（M9-F closure）→ `b7a6151`（M10-A）。M9 = ✅ COMPLETE（不重开）；**M10-A = COMPLETE**。
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
