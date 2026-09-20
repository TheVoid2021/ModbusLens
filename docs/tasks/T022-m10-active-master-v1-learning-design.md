# T022 — M10 Active Master v1 — Learning / Design Gate

> **状态：M10-A = ✅ COMPLETE；M10-B = ✅ COMPLETE（accepted behavior tree `ef71244`，verified LKGC）；**M10-C = Write Safety UI Foundation：Phase 1（§I）→ §J → §K → C1（§L）→ C2（§M）→ C3（§N：context / keyboard / accessibility safety）→ Review = HOLD → **Correction 已落库（§O0–O10：E1/E2 rapid-Enter spillover oracle；产品 QML 零变化），等待 M10-C3 Final Re-review；C4 = NOT STARTED**（无 encoder、无 confirm→dispatch）。**
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
