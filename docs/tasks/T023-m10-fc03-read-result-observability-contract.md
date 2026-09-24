# T023 — M10 FC03 Read Result Taxonomy / UI Contract Freeze（Read-Only Audit + Docs-Only Contract）

> **状态：CONTRACT PROPOSED —— AWAITING HUMAN REVIEW。本轮零代码改动、零测试改动、零构建、零打包。**
> **M10 = REOPENED / CORRECTION；M10-F = HOLD；M11 = HOLD / NOT STARTED（本轮禁止开始 M11）。**
> verified LKGC = **`d08ab55c71f54211e35f6bcdf0c2ec026a1d185f`**（本轮**不推进**，docs-only 提交永不作 LKGC）。
> 文档类型：**只读架构审计 + 契约冻结（定义）**。它**不是**实现文档，不承诺任何代码已存在或已修改。
> 唯一的下一个允许动作（Round N+1 = Review → Implementation Round）：**Human Review 本契约**，
> 认可后才另起一轮按 §10 的 gap matrix 实施。**本轮不得自动开始写代码。**

## 0. 本轮为什么存在（Human Discovery）

Human 在 M10 收口后报告了一条**产品可用性**发现，而非崩溃或构建缺陷：

> 用「读取保持寄存器」成功读到寄存器后，**界面只告诉我「成功」**。
> 我看不到设备到底返回了什么值，也看不到实际发出去/收回来的字节。
> 于是「成功」对我而言**不可解释**——我无法判断这次读取是否真的读到了我关心的寄存器。

这不是一个「缺一个灯」的显示问题，而是一个**语义缺口**：当前产品把「事务分类结论」当成了
「读取结果」的全部。**一次成功的 FC03 读取，其产品价值主要在返回的寄存器数据上，而该数据
今天没有任何呈现路径。**

本轮的产出 = **把「一次 FC03 读取的结果」正式定义成一个可冻结的产品契约**，
包括分类法、TX/RX 字节语义、UI 契约、校验矩阵、缺口矩阵、测试契约与 UnknownResponse 策略。
它先把**事实**与**待实现**分开，避免下一轮边写代码边改语义。

## 1. 硬性语义规则（贯穿全文，不可违反）

```text
R-FACT   UI 只能陈述「已观测到的事实」，不得陈述「可能的原因」。
         分类、字节、耗时、异常码 = 已观测事实；「接线不良」「地址配错」「PLC 程序问题」
         — 一律禁止（继承 T014 issueDetailText 与 DiagnosisPromptBuilder 的既有纪律）。

R-SRC    UI 呈现的每一个事实都必须来自 Core/Controller 的既有 authority，
         不得在 QML 里重新解析 byte、重新判定 status、重新数寄存器。

R-STRONG 更强的观测覆盖更弱的推断：一旦观测到可信响应，就永远不得写「可能已发送」。
         （ActiveTransactionEvidence.h:26–30 既有规则）

R-HONEST 空字节不是「失败」，是「未观测到任何字节」的诚实表示；
         残缺/损坏字节同样必须保留（ActiveTransactionEvidence.h:49–53）。
```

---

## 2. 审计：现有 FC03 读取路径的完整事实链（12 问）

全部引用真实代码位置；本节只陈述**已核实**的事实。

### 2.1 完整 call chain（QML → Core → 传输 → 回呈现）

```text
[UI]      CommunicationPage.qml:286–297  commReadButton.onClicked
            -> page.analysisController.readHoldingRegistersOnce(
                   serialSlaveSpin.value, serialStartSpin.value,
                   serialQuantitySpin.value, serialTimeoutSpin.value)

[CTRL]    AnalysisController.cpp:980  AnalysisController::readHoldingRegistersOnce
          (a) 6 道本地守卫，全部走 setSerialError（**串口错误：** 文案）：987–1010
          (b) 构 ActiveRequestIntent{function=ReadHoldingRegisters, unitId, timeout,
                 payload=ReadHoldingRegistersIntent{startAddress, quantity}}   1015–1023
          (c) encodeActiveRequest(intent) -> ActiveRequestDescriptor{intent, frame, wire}  1024
          (d) startActiveDescriptor(descriptor)                              1040

[CTRL]    AnalysisController.cpp:1045  startActiveDescriptor（FC03 与写路径**共用**）
            serialTransport_->startActiveRequest(descriptor)                    1052
            accepted=false -> 归档 ShortSubmission 终态证据（若有）后原样返回      1060–1068
            accepted=true  -> pendingRequest_ = descriptor; serialBusy_ = true   1071–1072
                              preparedWriteStore_.invalidate(BusyBecameTrue)     1078
                              clearSerialError(); emit serialStatusChanged()     1082–1083

[XPORT]   SerialPortAdapter.cpp:~200–272  SerialTransactionAdapter::startActiveRequest
            全量写入 -> elapsed_.start(); timeoutTimer_.start(intent.timeout)   269–271
                        return ActiveStartResult{true, PossiblySent}            271
            部分写入 -> ActiveTransportTerminal{ShortSubmission, acceptedByteCount} 247–262
                        （无 Modbus 结论，证据不丢）

[XPORT]   SerialPortAdapter.cpp:317  handleReadyRead
            observedResponseBytes_ += 本次读到的全部字节（**先存证据**）          322–323
            session_.feedResponseBytes(bytes, elapsed)                          329
              完成候选帧 -> emit transactionCompleted(ActiveTransactionResult{
                                request = *pending,                       ← 发送时快照
                                responseAdu = observedResponseBytes_,     ← 逐字节保留
                                disposition = PossiblySent,
                                analysis = *analysis})                  336–341

[XPORT]   SerialPortAdapter.cpp:347  handleTimeout
            session_.onResponseTimeout(elapsed)
              无字节 -> NoResponse（诚实空）; 有残缺字节 -> 解码整段后判 CrcError/ProtocolError
            emit transactionCompleted(... responseAdu = observedResponseBytes_ ...) 357–364

[CORE]    SerialTransactionSession.cpp:275 / 280  analyzeAndReset -> analyzeActiveResponse
            0x03 -> analyzeFunction03Transaction(request.frame, observation,
                                                elapsed, timeoutThreshold)      286–291

[CORE]    TransactionAnalysis.cpp:83  analyzeFunction03Transaction
            ① NoResponse      -> Pending（elapsed<threshold）/ Timeout            86–91
            ② RtuDecodeError  -> CrcMismatch⇒CrcError；FrameTooShort⇒ProtocolError 96–110
            ③ address 不符    -> ProtocolError + ResponseAddressMismatch         120–128
            ④ 0x83            -> decodeReadHoldingRegistersException
                                 成功⇒ Exception + exceptionCode
                                 形状失败⇒ ProtocolError + MalformedExceptionResponse 130–149
            ⑤ 0x03            -> decodeReadHoldingRegistersRequest/Response
                                 请求可解码 + 响应可解码 + 数量一致 ⇒ **Success**   151–184
                                 数量不一致 ⇒ ProtocolError + QuantityMismatch
                                 响应格式失败 ⇒ ProtocolError + MalformedNormalResponse
            ⑥ 其它功能码      -> ProtocolError + UnexpectedResponseFunction      186–192

[CTRL]    AnalysisController.cpp:2329  handleSerialTransactionCompleted
            陈旧守卫：pendingRequest_ 存在 且 result.request == *pendingRequest_   2335–2343
            appendActiveSerialTransaction(ActiveTransactionRecord{
                sessionId, request = *pendingRequest_,
                evidence = result.evidence(),      ← requestAdu = request.wire（原样）
                analysis = result.analysis })                                2348–2353
            写路径 terminal notice（isWriteFunction gate）—— **READ 被显式排除**     2366–2374

[CTRL]    AnalysisController.cpp:2265  appendActiveSerialTransaction
            activeSerialRecords_.push_back(record)      ← authority（含字节证据） 2273
            transactionModel_.appendEntries({makeSessionRow(record)})          2274
            refreshActiveSessionDerivedViews()                                 2275
              -> summarizeTransactions(...) 统计快照
              -> 重建 activeDiagnosisTransactions_（{unit, fc, analysis, requestIssues={}}）
              -> invalidateAiForBatchChange()

[CTRL]    AnalysisController.cpp:2278  makeSessionRow —— 唯一的行投影
            deviceAddress / functionCode / status / elapsedMs / exceptionCode /
            issueText / activeSerialProvenance{sessionId, request, evidence}    2284–2296

[UI]      TransactionListModel.cpp:175  data() —— 8 个 role
            deviceAddress, functionCode, statusCode, statusText,
            elapsedMs, hasExceptionCode, exceptionCode, issueText               184–206
```

### 2.2 十二问逐项作答

| # | 问题 | 结论（代码事实） |
| --- | --- | --- |
| 1 | 点击后走了哪些层？ | QML → AnalysisController（Qt 适配层）→ core::encodeActiveRequest → SerialTransactionAdapter → core::SerialTransactionSession → core::analyzeActiveResponse → core::analyzeFunction03Transaction → Controller 归档 → TransactionListModel → QML 行。**解析与分析都在 V1 core，零 Qt。** |
| 2 | **TX bytes 是否真实保存在 transaction truth 中？** | **是。** `ActiveTransactionEvidence.requestAdu = request.wire`（`ActiveTransactionEvidence.cpp:19`），由**发送时描述符**逐字节复制，**永不事后重新编码**。保存在 `ActiveTransactionRecord.evidence`，随 `activeSerialRecords_` 长期驻留，并**同时**进入行的 `activeSerialProvenance`。 |
| 3 | TX bytes 的 authority 在哪里？ | `ActiveRequestDescriptor.wire`（`ActiveRequestIntent.h:118`，编码**只发生一次**）→ 复制进 record。描述符同时进 `pendingRequest_` 作陈旧守卫。 |
| 4 | **RX 保存在哪里？** | `ActiveTransactionEvidence.responseAdu`，来源 `SerialPortAdapter::observedResponseBytes_`（`SerialPortAdapter.cpp:322–323` 先存证据再喂 session）。完成路径 `:338`、超时路径 `:361`、断开 `:309`、端口丢失 `:462`。**残缺与损坏字节一律保留，永不丢弃。** |
| 5 | **注册表值（decoded values）保存在哪里？** | **哪里都不保存。** `decodeReadHoldingRegistersResponse` 产出的 `ReadHoldingRegistersResponse{values}`（`Function03.cpp:50–78`）**只在** `analyzeFunction03Transaction` 内部短暂存在（`TransactionAnalysis.cpp:161–168`），用途**仅是** `values.size()` 与请求 quantity 做一致校验；函数返回后该 vector 即被销毁。 |
| 6 | `TransactionAnalysis` 携带什么？ | 仅 `status / elapsed / exceptionCode / issue`（`TransactionAnalysis.h:133–145`）。**没有任何字段承载寄存器值。** |
| 7 | `TransactionIssue` 有值载荷吗？ | 有，但只承载**不匹配/不符**类事实（address/functionalCode/quantity/echo 四元组）。**没有「成功时的值」位置。** |
| 8 | RX 字节有消费者吗？ | **没有产品消费者。** `grep requestAdu\|responseAdu` 全仓命中的消费者只有 `src/main.cpp`（harness 断言）与 `SerialPortAdapter.cpp`（生产者）。`AnalysisController` 呈现格式化、`TransactionListModel` role、**全部 QML**、`DiagnosisPromptBuilder`、`AgentTools` 均不读。 |
| 9 | `activeSerialProvenance`（行级）有消费者吗？ | **没有。** 它在 `TransactionListEntry` 中被填充（`AnalysisController.cpp:2291`），但 `TransactionListModel::roleNames()` 只暴露 8 个 role，**没有 provenance role**；QML 无法触及。⇒ 证据**进了呈现数据结构，却没有出口**。 |
| 10 | 用户今天能看到的 FC03 结果有哪些？ | ① Transactions 行：`设备 N` / `0x03` / 状态 / `N ms` / `异常码 0xNN 或 —` / 可选 issueText 副行；② 统计计数器（observed/pending/completed/success/exception/crcError/timeout/protocolError/expectedNoResponse）；③ Communication 页仅有**请求预览**（PDU/RTU），**没有任何响应区**。 |
| 11 | 请求参数（起始地址/数量）在结果里呈现吗？ | **不呈现。** 预览区显示的是「将要发送」的参数；结果面（行/统计）**不含 startAddress，也不含 quantity**。 |
| 12 | 本地守卫失败走哪条呈现？ | `setSerialError`（**`串口错误：…`**），渲染于 `communicationSerialError`（`CommunicationPage.qml:184–191`）。⇒ **本地参数校验失败与传输失败共用同一呈现通道**（既有耦合，本轮只记录，不顺手改）。 |

### 2.3 缺口的定性（backend vs presentation）

```text
不是「backend 缺失」：所需事实在 core 里**已经全部存在**——
  TX 字节 = record.evidence.requestAdu（逐字节、原样）
  RX 字节 = record.evidence.responseAdu（逐字节、原样，含损坏）
  传输处置 = record.evidence.disposition（NotSent / PossiblySent）
  分类结论 = record.analysis（status + elapsed + exceptionCode + issue）
  请求意图 = record.request.intent（unitId / timeout / payload{startAddress, quantity}）

是「presentation 缺失」+「一处 core 派生缺失」：
  ① 【presentation】证据无出口：8 个 model role 不含任何字节/参数/provenance 字段；
     Communication 页无响应区；QML 零消费 requestAdu/responseAdu。
  ② 【core 派生】成功时的**寄存器值**从未被保留：解码结果在分析器内部被丢弃，
     因此「Success 意味着读到了什么」在**当前数据模型里无法表达**——
     这是唯一的真 backend 缺口，且它是一处**派生/保留**问题，
     不是解析能力问题（解析器已存在且有测试）。
  ③ 【presentation】本地参数错误与传输错误共用同一通道，语义被合并。
```

**⇒ 本轮结论：缺陷等级 = 产品可用性缺陷（不可解释的「成功」），不是正确性缺陷。
现有 classify 逻辑未被发现错误；缺的是「把已经存在的事实说出来」。**

---

## 3. 结果分类法（RESULT CLASS TAXONOMY）—— 10 类

分类法**不新增第二套 outcome authority**：它与既有 `TransactionStatus`（7 值）+ 
`TransportDisposition`（2 值）+ `TransportTerminalReason`（3 值）是**一层纯映射**，
每一条都可追溯到已存在的核心事实。分类编号 `CLASS-01…CLASS-10` 是**本契约新定义**，
仅用于文档/测试引用，**不是** runtime 枚举。

| # | 类名（中文） | 机器 token（建议） | 追溯到的既有事实 | 有 TX 字节？ | 有 RX 字节？ | 产出位置 |
| --- | --- | --- | --- | --- | --- | --- |
| CLASS-01 | **本地拒绝（未发送）** | `local_rejected` | 无 record；无 descriptor；`disposition = NotSent` | 否（未编码，或已编码未提交） | 否 | `readHoldingRegistersOnce` 6 道守卫；`startActiveDescriptor` accepted=false 且无 terminal |
| CLASS-02 | **发送未完整** | `short_submission` | `ActiveTransportTerminal{reason=ShortSubmission, submissionAcceptedByteCount=n}` | 是 | 否（或空） | `SerialPortAdapter.cpp:247–262` |
| CLASS-03 | **提交后传输中止** | `transport_terminated` | `ActiveTransportTerminal{reason=TransportError \| DisconnectedAfterSubmission}` | 是 | 可能是部分 | `SerialPortAdapter.cpp:309 / 462` |
| CLASS-04 | **无响应超时** | `timeout` | `TransactionStatus::Timeout`；`responseAdu` 为空 | 是 | 空（诚实） | `TransactionAnalysis.cpp:86–91` + `handleTimeout` |
| CLASS-05 | **字节到达但不可信** | `wire_error` | `CrcError`，或 `ProtocolError` + {`ResponseFrameTooShort`, `MalformedNormalResponse`, `MalformedExceptionResponse`} | 是 | **是（逐字节保留）** | 同上 :96–110 / :130–149 / :157–163 |
| CLASS-06 | **响应归属其它实体** | `foreign_response` | `ProtocolError` + {`ResponseAddressMismatch`, `UnexpectedResponseFunction`} | 是 | 是 | 同上 :120–128 / :186–192 |
| CLASS-07 | **设备异常响应** | `device_exception` | `TransactionStatus::Exception` + `exceptionCode` | 是 | 是 | 同上 :130–149 |
| CLASS-08 | **数量不匹配** | `quantity_mismatch` | `ProtocolError` + `QuantityMismatch{expectedQuantity, actualQuantity}` | 是 | 是 | 同上 :171–181 |
| CLASS-09 | **读取成功** | `read_success` | `TransactionStatus::Success`；**值目前不可得**（§2.2 #5） | 是 | 是 | 同上 :151–184 |
| CLASS-10 | **未知 / 未记录分支** | `unknown_response` | `ProtocolError` 无 issue，或 `transactionIssueName` 的 fallback token，或本 build 未识别的枚举值 | 是 | 是 | 同上 :110 / :154–158；`TransactionAnalysis.cpp:33–35` |

### 3.1 分类法的完备性论证

```text
FC03 unicast 读取的「观测空间」被三重维度完全覆盖：
  A. 是否离开本进程？          NotSent / PossiblySent
  B. 是否有可信 Modbus 响应？  无 / 有（异常形 / 正常形）
  C. 若有响应，是否与请求配对？ 配对（Success/Exception）/ 不配对（ProtocolError 五种 issue）

⇒ CLASS-01 覆盖 A=NotSent；CLASS-02/03 覆盖 A=PossiblySent 且 B=无（传输终态）；
   CLASS-04 覆盖 B=无（超时）；CLASS-05/06 覆盖 B=不可信；
   CLASS-07/08 覆盖 B/C=可信但不符；CLASS-09 覆盖完全配对；
   CLASS-10 覆盖「本 build 无法给出确定性理由」的防御分支。

**ExpectedNoResponse 不参与 FC03 读取**：0（广播）在 `validateActiveRequestIntent`
（ActiveRequestIntent.h:90–95 UnitIdNotUnicast）即被拒 ⇒ 主动 FC03 永远不属于 ENR。
（ENR 的广播语义见 ADR-003，属被动/passive 路径，与 Read Result 无关。）
```

---

## 4. TX 语义契约（READ-TX）

```text
READ-TX-1  「实际发出的字节」= 发送时描述符的 wire，逐字节，含 CRC。
           它在 core 里已经存在（record.evidence.requestAdu），UI 只做投影。

READ-TX-2  字节的 authority 是**编码一次**：预览与派发调用**同一个** encodeActiveRequest，
           故 PREVIEW == WIRE 是**构造性**成立，不是约定（CommunicationPage.qml:301–304 注释已述）。

READ-TX-3  UI 可以说：「本次请求以如下字节发出」+ 字节。
           UI **不得**说：「设备已收到」「已成功写入总线」「传输无误」。
           理由：disposition = PossiblySent 只证明**传输 API 接受了这次写入**，
           字节是否抵达 DEVICE 不可证（ActiveTransactionEvidence.h:24–30 原文）。

READ-TX-4  传输处置必须可见且分档：
             NotSent(CLASS-01)      -> 「未发送」
             PossiblySent           -> 「已提交传输（设备是否收到不可证）」
           **禁止**把 PossiblySent 渲染成「已发送」。

READ-TX-5  请求参数回显（unit / startAddress / quantity / timeout）必须与字节**并列**呈现，
           地址与值一律 **DEC 为准、HEX 为显示投影**（沿用既有冻结：
           `Addresses/values are protocol truth: DEC for input, HEX as a display
           projection only (never an input format)` — AnalysisController.h:384–385）；
           HEX 一律 4 位大写 `0x00NN`（沿用 `hex4` 惯例，TransactionListModel.cpp:81–84）。
```

---

## 5. RX 语义契约（READ-RX）

```text
READ-RX-1  「实际收到的字节」= transport 观测到的原始字节，逐字节保留，含损坏。
           空 = 「未观测到任何字节」，**不是**「失败」，也**不是**「空响应」。
           残缺/超长字节**永不截断、永不丢弃**（既有冻结，不得为了好看而裁剪）。

READ-RX-2  分类结论只来自 core 分析器；UI **不得**重新解码、重新判 CRC、重新数寄存器。
           若 UI 需要寄存器值，它必须**通过 core 的解码器**取得（见 READ-RX-4），
           不得在 QML/C++ 呈现层自写解析。

READ-RX-3  「更强的观测覆盖更弱的推断」：一旦存在可信响应（CLASS-07/08/09），
           TX 侧就不得再出现「可能已发送」这类措辞。

READ-RX-4  **寄存器值的唯一来源**：对「本身就是 Success 的那段 RX 字节」调用
           core 的 `decodeReadHoldingRegistersResponse`，取 `values`。
           呈现层的职责只是「把同一段字节交给同一个解码器」，绝不是自己算。
           ⇒ 这同时给出一个可被测试的**不变量**：
             「UI 显示的 N 个值」== 「对 UI 显示的同一段 RX 字节调用 production decoder
              得到的 values」，且 N == 请求 quantity（Success 的定义即如此）。

READ-RX-5  解码失败时禁止「尽力而为」呈现数值：CLASS-09 之外的任何类**不得**显示任何寄存器值，
           哪怕字节里恰好看得出两个像数的字节对。**展示未验证的值 = 编造设备内容。**
```

---

## 6. 结果 UI 契约（READ-UI）

### 6.1 一次读取必须恰好进入一个「结果面」状态

```text
READ-UI-1  一次读取（无论成败）必须产出**恰好一个**结果面状态，由 §3 分类法唯一决定。
           **不允许**「有分类但无字节」「有字节但无分类」「两个分类同时显示」这三种残缺帧。

READ-UI-2  结果面必须**同时**包含四块信息，缺一不可：
             (a) 请求参数回显：unit / 起始地址(DEC + 0xHEX) / 数量 / 超时
             (b) TX 字节（含 CRC）+ 传输处置分档措辞（READ-TX-4）
             (c) RX 字节；为空时必须是明确的「未观测到任何字节」文本
             (d) 分类结论 + 该分类的确定性依据（issue 明细 / 异常码 / 数量对比）

READ-UI-3  只有 CLASS-09 可以出现**寄存器值区**；其它 9 类一律不出现（READ-RX-5）。
           寄存器值区必须带：index / 地址(DEC + 0xHEX) / 值(DEC) / 值(0xHEX)。
           ——index/地址一列是为了回答「我读的是不是我要的那几个寄存器」，
             这正是 Human 发现的缺口本体。

READ-UI-4  措辞纪律（继承 R-FACT）：只列观测事实与「建议检查项」，
           禁止根因断言。禁用词示例：接线不良 / 信号干扰 / 地址配错 / PLC 程序错误 /
           设备老化 / 通信不稳定。（可复用的既有安全措辞见 §8「建议检查项」列。）

READ-UI-5  结果面必须**可被再次查看**：新读取可以替换它，但**不得**在用户未操作时静默清空；
           与既有 Clear Results / 源切换语义保持一致（清空 = 用户显式动作）。
```

### 6.2 结果面必须是「结论行 + 证据区」两层，而不是一坨字节

```text
READ-UI-6  结果面分两层：
             第一层（恒显、单行）：分类结论 + 关键量（耗时 / 异常码 / 值个数或 TX·RX 长度）
             第二层（证据区）：参数回显 + TX 字节 + RX 字节 + 寄存器值表
           第一层必须在**不展开**的情况下就能回答「这次算成功还是失败、失败在哪一类」。

READ-UI-7  证据区必须**可滚动且高度有界**，不得让内容撑破页面。
           RX 字节在 125 个寄存器时可达 255 字节（`byteCount ≤ 250` + 1 + addr + fc + CRC），
           单行平铺必然溢出 ⇒ 必须换行/滚动，并显式处理「超长字节」这一合法输入。
```

---

## 7. 信息架构与 1000×700 预算（READ-IA）

### 7.1 硬约束（实测，来自 T022 §ZMK）

```text
· Communication 页在 **1000×700 / 真实 windows QPA** 下内容高度预算 ≈ **627px**；
  写区 C4 断言要求 `writeFoundationPanel` 底边 ≤ 700（当前 **675**，余量仅 **25px**）。
· windows QPA 行高 **16px** vs offscreen **12px** ⇒ **offscreen 的绿不代表真实平台绿**
  （`qml_write_foundation_check_windows` 已存在专为此复跑，CMakeLists.txt）。
· 任何在 Communication 页**恒显**新增的内容都会直接吃掉这 25px 余量，
  并会再次把写区推出窗口 —— 这正是 `ISSUE-016` / `ISSUE-018` 两次真实缺陷的形态。
```

### 7.2 采纳的 IA 方案（方案 B：同页分层 + 有界证据区）

```text
方案 A（否决）**在 Communication 页新增恒显响应区**：
  ⇒ 必然超预算；要么压写区（不可，写区是 M10-F 刚修好的红线），
     要么压请求区信息（不可，违反 READ-UI-2(a)）。
方案 B（采纳）**结论行恒显（1 行）+ 证据区按需展开（有界高、可滚动）**：
  ⇒ 恒显成本 ≈ 单行高度（真实平台 16–20px），在 25px 余量内；
  ⇒ 长内容进滚动容器，不影响页面总高；不新增独立 workspace、不改导航。
方案 C（否决）**新建独立「读取结果」页**：
  ⇒ 触碰 M9 已冻结 IA（五 workspace + Device disabled + navigation presentation-only），
     属 M9 冻结面，**本轮不越界**，且会引入新的导航权威。
```

```text
READ-IA-1  Communication 页只新增**一行级**结论面；证据区以展开/滚动方式存在。
READ-IA-2  新增内容必须经**真实 windows QPA / 1000×700** 的几何门禁验证
           （复用既有 oracle：`--qml-write-foundation-check` 的 C4 断言机制，
           扩名字清单即可，不新建框架）。
READ-IA-3  **不得**通过缩小字号、删减字段、隐藏信息来腾出空间（M10-F 已冻结的纪律）。
READ-IA-4  寄存器值表在 125 项时必须完全可达（滚动/分页皆可），不得截断。
```

### 7.3 对既有冻结的**有意超越**（必须显式披露，交 Human Review 裁定）

```text
T019（M9-D）§611 曾把「**无 raw-hex** / 无 request-response 分轴」列为 scope freeze 的一部分，
当时的语境是「M9-D 不改 Transactions 结构」。

本契约**有意**在**读取结果**这条路径上重新打开该边界，理由：
  ① 该冻结属于 M9-D 的阶段边界，不是产品永久契约；
  ② Human 的发现本身就是「证据不可见」，与「无 raw-hex」直接冲突；
  ③ M10 已把字节证据做成了**权威事实**（ActiveTransactionEvidence），
     产品却没有任何出口 —— 权威与呈现脱节。
⇒ 本项为**待 Human 裁定的冻结超越**，未经裁定不得实施。
```

---

## 8. Transactions 页的关系（READ-TXN）

```text
READ-TXN-1  一行 = 一个事务，FC03 与 0x06/0x10 共用同一行模型 —— **行结构不改**。
READ-TXN-2  Transactions 行继续只做「摘要」（设备/功能码/状态/耗时/异常码/issue），
            它**不是**读取结果的详情面；把 255 字节塞进行是明确否决的。
READ-TXN-3  证据的**权威**永远是 `activeSerialRecords_`（record），
            行只是投影。新增的任何「读取结果」呈现都必须**从 record 取数**，
            不得从行反推、不得复制第二份权威。
READ-TXN-4  `activeSerialProvenance` 目前**有结构无出口**（§2.2 #9）。
            本契约不规定必须由 model role 暴露它；但**必须**为「读取结果」提供
            一条**从 record 到 UI 的单一取数路径**，且该路径不得绕过 record。
READ-TXN-5  Simulator / Replay 源**没有** wire 证据（无真实收发）——
            不得为它们伪造字节。读取结果面必须在无证据时明确呈现「该源不提供收发字节」，
            而不是显示空串或假 hex。（既有纪律：`activeSerialProvenance = std::nullopt`
            正是这个意思，AnalysisController.cpp:2440 / 2637。）
```

---

## 9. 精确用户可见文案契约（READ-MSG）

`〔待 Review〕` 标记的文案为**本契约新提出**；其余为**逐字沿用既有已冻结文案**。

| 类 | 结论行（新提出） | 依据行（沿用/新提出） | 建议检查项（安全措辞，新提出） |
| --- | --- | --- | --- |
| CLASS-01 | **未发送**·`local_rejected` 〔待 Review〕 | 沿用既有守卫原文，如「串口错误：寄存器数量须在 1..125 之间」 | 按提示修正参数后重试 |
| CLASS-02 | **发送未完整**〔待 Review〕 | 「传输层仅接受 N 字节，未完整发送」（N = `submissionAcceptedByteCount`） | 检查串口设备与接线后重试 |
| CLASS-03 | **传输中止**〔待 Review〕 | 沿用「串口设备不可用：%1」/「串口写入失败：%1」 | 检查串口设备后重新连接 |
| CLASS-04 | **无响应超时** | 沿用行状态「超时」+「未观测到任何字节」〔待 Review〕 | 核对从站地址、串口参数、RS485 接线（复用 DiagnosisActionCode 既有建议集） |
| CLASS-05 | **响应不可信**〔待 Review〕 | 沿用 issueDetailText：「CRC 错误」/「响应帧过短（不足最小帧长）」/「正常响应格式非法」/「异常响应格式非法」 | 检查串口参数与接线 |
| CLASS-06 | **响应不属于本次请求**〔待 Review〕 | 沿用「响应地址不匹配（请求 0x%1 / 响应 0x%2）」/「响应功能码不符（实际 0x%1）」 | 核对从站地址；确认总线上是否存在其它主机/从站 |
| CLASS-07 | **设备异常** | 沿用行状态「异常」+「异常码 0x%1」（既有 `hex2` 惯例） | 按异常码含义检查设备状态（不解释异常码物理成因） |
| CLASS-08 | **寄存器数量不匹配**〔待 Review〕 | 沿用「寄存器数量不匹配（请求 %1 / 响应 %2）」 | 核对请求数量与设备寄存器范围 |
| CLASS-09 | **读取成功** | 「共 N 个寄存器」〔待 Review〕+ 值表 | ——（无须检查项） |
| CLASS-10 | **响应无法判定**〔待 Review〕 | 沿用「协议错误（未记录细节）」 | 保存原始字节并复现（不猜测原因） |

```text
READ-MSG-1  所有新文案必须保持「观测事实」语气；不得引入设备健康/可靠性/长期稳定性判断。
READ-MSG-2  机器 token（如 `local_rejected`）**不得**直接进 UI（沿用既有纪律：
            "Stable machine token — adapter/tool serialization only, never human UI prose"）。
READ-MSG-3  中文为 UI 语言（`docs/06_UI_LANGUAGE_POLICY.md`）；字节一律 HEX 大写。
READ-MSG-4  文案一经冻结，改动须走契约修订（与既有 frozen FC03 守卫文案同等对待）。
```

---

## 10. 响应校验矩阵（RESPONSE VALIDATION MATRIX）

输入 = **实际观测到的字节**；输出 = 分类。每一行都可被测试独立复现。

| 观测字节（形态） | 前置（配对） | core 判定 | 分类 | RX 保留 |
| --- | --- | --- | --- | --- |
| （无字节，未提交） | —— | 无 record | CLASS-01 | 无 |
| （无字节，`elapsed ≥ threshold`） | 已提交 | `NoResponse` → `Timeout` | CLASS-04 | 空 |
| （无字节，`elapsed < threshold`） | 已提交 | `NoResponse` → `Pending` | （过渡态，非终态） | 空 |
| `< 最小 RTU 帧长` | —— | `RtuDecodeError{FrameTooShort}` → `ProtocolError` + `ResponseFrameTooShort` | CLASS-05 | 全量 |
| 帧长可解但 CRC 不符 | —— | `RtuDecodeError{CrcMismatch}` → `CrcError` | CLASS-05 | 全量 |
| `addr != 请求 addr` | —— | `ProtocolError` + `ResponseAddressMismatch{expected,actual}` | CLASS-06 | 全量 |
| `fc == 0x83`，恰好 1 数据字节 | —— | `Exception` + `exceptionCode` | CLASS-07 | 全量 |
| `fc == 0x83`，数据长度 ≠ 1 | —— | `ProtocolError` + `MalformedExceptionResponse` | CLASS-05 | 全量 |
| `fc == 0x03`，`byteCount` 或长度非法 | —— | `ProtocolError` + `MalformedNormalResponse` | CLASS-05 | 全量 |
| `fc == 0x03`，`values.size() != quantity` | —— | `ProtocolError` + `QuantityMismatch{expected,actual}` | CLASS-08 | 全量 |
| `fc == 0x03`，完全配对 | —— | **`Success`** | CLASS-09 | 全量 |
| `fc ∉ {0x03, 0x83}`（含 0x84 等异常形） | —— | `ProtocolError` + `UnexpectedResponseFunction{actual}` | CLASS-06 | 全量 |
| 请求帧在分析器内不可解码（防御） | —— | `ProtocolError` + `UnknownProtocolError` | CLASS-10 | 全量 |
| 任何本 build 未识别的枚举/无 issue 的 ProtocolError | —— | fallback token | CLASS-10 | 全量 |

```text
MAT-1  超长字节（> 255B）**永不截断**：session 不在候选边界关闭，等超时后解码**整段**
       并判 wire 级错误 ⇒ 归 CLASS-05（既有冻结，SerialTransactionSession.h:104–120）。
MAT-2  分片到达（同一响应跨多次 readyRead）：必须在**任意分片边界**下得到与整段相同的分类。
MAT-3  分类**必须**与 RX 字节长度和时间无关（除 Pending/Timeout 的 elapsed 判定）。
MAT-4  矩阵中「过渡态 Pending」不是用户可见终态；结果面不得停在 Pending。
```

---

## 11. Backend 缺口矩阵（GAP MATRIX）

| 能力 | 现状（§2 实证） | 读取结果契约要求 | 缺口类型 |
| --- | --- | --- | --- |
| FC03 请求编码 | ✅ `encodeActiveRequest` | 回显用 | **无** |
| 请求预览 | ✅ `previewReadRequest`（同 encoder） | TX 字节同源 | **无** |
| TX 字节保留 | ✅ `evidence.requestAdu`（发送时快照） | 呈现 | **仅 presentation** |
| RX 字节保留 | ✅ `evidence.responseAdu`（逐字节，含损坏） | 呈现 | **仅 presentation** |
| 传输处置 | ✅ `disposition` / `TransportTerminalReason` | 分档措辞 | **仅 presentation** |
| 分类结论 | ✅ `TransactionStatus` + `TransactionIssue`（9 code） | 10 类映射 | **仅 presentation（映射层）** |
| 异常码 | ✅ `analysis.exceptionCode` | 呈现 | **无** |
| 请求参数回显 | ✅ `record.request.intent.payload` | 呈现 | **仅 presentation** |
| **寄存器值** | ❌ 解码后在分析器内部**被丢弃**（§2.2 #5） | CLASS-09 值表 | **⭐ CORE 派生缺口（唯一真 backend 缺口）** |
| 值的 index/地址 | ❌ 未派生（值本身未保留） | 值表列 | **⭐ 随上一行** |
| record → UI 取数路径 | ❌ 无（8 role 无证据出口） | 单一路径 | **presentation 接口缺口** |
| UI 结果面 | ❌ Communication 页无响应区 | 结论行 + 证据区 | **presentation 缺口** |
| 结果面几何保护 | ⚠️ 有 oracle，但读区不在其名字清单内 | 需纳入 | **测试/门禁缺口** |

```text
GAP-1  唯一的 core 侧改动是「**保留**已经算出来的解码结果」（派生，不是新解析）。
       可选实现方向（**本轮不选定，待 Review 决策**）：
         (a) 在 `TransactionAnalysis` 增加可选 `std::vector<std::uint16_t> values`
             —— 侵入最小，但会扩大 V1 冻结分析对象；
         (b) 在 Active 层新增 `ActiveReadResult`（由 record + 对同一段 RX 字节的解码派生）
             —— 分析对象保持冻结，派生显式，代价是多一个类型；
         (c) 呈现层按需调用 core 解码器（对 `evidence.responseAdu` 调
             `decodeReadHoldingRegistersResponse`）—— 零 core 改动，
             但「按需」意味着**每次渲染都重解码**，且必须在测试里锁定
             「显示的字节 == 解码的字节」这一不变量。
       ⇒ 三种都满足 §5 READ-RX-4 的不变量；**决策属于 Human Review**。
GAP-2  禁止用「UI 自己解析」来填补 GAP-1（违反 R-SRC / READ-RX-2）。
GAP-3  缺口不得被填成「fake success」：CLASS-09 的值必须来自 production decoder 的可复现输出。
```

---

## 12. 测试契约（READ-R1 … READ-R9）

命名 `READ-Rn` 为**契约条目**；实施时应落在既有测试目标内（不新建框架），
并尽量复用既有 fixture（`tests/fake_serial_transport.*`、既有 golden wire 向量惯例）。

| ID | 名称 | 断言（可机械化） | 目标层 |
| --- | --- | --- | --- |
| **READ-R1** | TX 字节身份 | 一次真实读取路径中，结果面呈现的 TX 字节 == `encodeActiveRequest` 对该 intent 的输出，逐字节 == `record.evidence.requestAdu`；且 == 预览字节（PREVIEW==WIRE，构造性） | core + controller |
| **READ-R2** | RX 字节保真 | 对 CRC 变异、帧过短、超长（>255B）、分片四种输入，结果面呈现的 RX 字节 == 注入字节，逐字节一致、长度一致、**不截断** | core (session) + controller |
| **READ-R3** | 10 类映射完备 | §10 校验矩阵**每一行**都必须产出其指定的 CLASS-xx，且**恰好一个**；矩阵无未覆盖行（枚举驱动） | core + 映射层 |
| **READ-R4** | 值的唯一来源 | CLASS-09 时，结果面显示的值列表 == 对**同一段已显示的 RX 字节**调用 production `decodeReadHoldingRegistersResponse` 的输出；且 `count == request.quantity`（含 1 / 2 / 125 边界） | core 派生 + 呈现投影 |
| **READ-R5** | 非 Success 不出现值 | CLASS-01…08、10 的**任何**情形下，值区**必须不存在**（含「字节里看起来像数对」的诱饵 fixture） | 呈现投影 |
| **READ-R6** | 处置措辞分档 | `NotSent` 呈现「未发送」；`PossiblySent` 呈现「已提交传输（不可证设备已收到）」，且**永不**呈现「已发送」；存在可信响应时 TX 侧不得出现「可能已发送」 | 呈现文案 |
| **READ-R7** | 无字节的诚实呈现 | 纯超时（`responseAdu` 空）呈现「未观测到任何字节」，**不得**呈现空串 / 「空响应」/ 伪造的 00 字节 | 呈现文案 |
| **READ-R8** | 无证据源不伪造 | Simulator / Replay 源下，读取结果面明确呈现「该源不提供收发字节」，且无任何 hex 输出；`activeSerialProvenance == nullopt` 路径不被填充造假 | controller + QML |
| **READ-R9** | 1000×700 真实平台几何 | 真实 **windows QPA / 1000×700**：新结论面 + 证据区（含 125 寄存器展开态）全部在窗口内；写区 C4 断言（`writeFoundationPanel` 底边 ≤ 700）**仍 PASS**；无 `ReferenceError`/`TypeError`/`Unable to assign` | harness (QML 门禁) |

```text
READ-T-1  READ-R3/R4/R5 是**新语义的锚点测试**：没有它们，本契约只是文档。
READ-T-2  READ-R9 必须在**真实 windows QPA** 下跑（offscreen 12px 行高会掩盖溢出，
          这是 ISSUE-016/018 两次真实缺陷的根因）；扩既有名字清单，不建新框架。
READ-T-3  READ-R6/R7 属于**文案冻结**测试，须锁定**逐字**字符串（防止未来被"改得更简洁"）。
READ-T-4  全部 READ-Rn 需**负向对照**（能复现失败条件），否则视为死断言
          （教训来自 M10-F R15/R16 加固）。
```

---

## 13. UnknownResponse 策略（CLASS-10）

```text
UNK-1  CLASS-10 是**合法且必须存在**的终态，不是缺陷证据。
       它覆盖三类真实情形：
         (a) 防御分支：请求帧在分析器内不可解码（本不应发生）→ `UnknownProtocolError`；
         (b) 无 issue 的 `ProtocolError`（既有 refinement A：宁可少说，不猜原因）；
         (c) 本 build 未识别的枚举值 → `transactionIssueName` fallback。

UNK-2  **禁止**为 CLASS-10 编造更具体的原因来「让 UI 更好看」。
       宁可显示「响应无法判定」+ 原始字节，也不显示一个可能错误的分类。
       依据：既有 `UnknownProtocolError` 注释原文「deterministic defensive/fallback
       branch only」+ `transactionIssueName` 的「cannot be given a fabricated meaning」。

UNK-3  CLASS-10 必须**同时**呈现原始 RX 字节与 TX 字节 —— 这是它唯一的价值：
       用户/开发者据字节自行判断，产品不越权解释。

UNK-4  READ-R3 必须覆盖 CLASS-10：即「无 issue 的 ProtocolError」与
       「fallback token」两条路径都要有测试，且都必须**不崩溃、不显示值、不显示猜测原因**。

UNK-5  若未来新增 `TransactionIssueCode`，`transactionIssueName` 的 switch 是
       `-Wswitch` 强制的（无 default），故新增码**必然**被迫补全映射 ——
       CLASS-10 不会因为新增码而静默退化。此性质须在测试中保持。
```

---

## 14. M11 边界（READ-M11）

```text
M11-B1  M11 = Register Readout & Decode，**继续 HOLD / NOT STARTED**；本轮**禁止开始 M11**。
M11-B2  本契约**只**定义「一次 FC03 读取的结果如何被观察」，
        **不**定义寄存器值如何被**解释**（Hex / Binary / UInt16 / Int16 / UInt32 / Int32 /
        Float32、byte/word order、raw data vs device physical semantics）——**全部属 M11**。
M11-B3  本契约的 CLASS-09 值表只呈现**原始 uint16**（DEC + 0xHEX）与 index/地址；
        任何「这个值代表什么」的语义解释**不得**在 M10 出现。
M11-B4  本契约的 core 派生（GAP-1）只保留 `std::vector<std::uint16_t>` 原始值，
        **不**引入任何 decode/interpretation 类型；M11 可在其上加解码视图。
M11-B5  M11 的 scope 需由 Human 另行立项（§ZE14 既有边界不变）。
```

---

## 15. 冻结范围 / 非目标（本轮与下一轮共同适用）

```text
冻结（不得改动）：
  · V1 deterministic core 的既有分析语义与 9 个 TransactionIssueCode（本契约不改判定）
  · 既有 8 个 model role 的语义（新增 role 属后续决策，不得静默改写既有 role）
  · FC03 的 6 条本地守卫文案（"frozen FC03 messages stay there for the dispatch path"）
  · Transactions 页行结构、导航 IA、M9-E/F 的 version·PE·icon·package 契约、
    M9-F 的 focus/accessibility baseline
  · 0x06 / 0x10 的既有行为（本契约只谈 FC03 读取结果的**观察**）
非目标：
  · 不做实时轮询 / 连续读取 / 自动重试
  · 不做寄存器写回 / 不做 decode 语义（M11）/ 不做设备 profile（M12）
  · 不给 AI/Agent 任何写权限或新的权威
  · 不为呈现效果裁剪字节、缩小字号或隐藏字段
  · 不新建独立 workspace（触 M9 冻结 IA）
```

---

## 16. 验证（本轮的验证边界）

```text
本轮 = **docs-only 契约冻结**，故：
  · 未 build / 未 test / 未 package / 未 windeployqt / 未跑任何 QML 门禁
  · 未修改 src / QML / tests / CMakeLists.txt / scripts / assets / samples
  · 未新增 tag；未 push；未 amend 既有提交
  · **未推进 verified LKGC**（docs-only 提交永不作 LKGC）
审计所用的**只读**证据 = 真实源码文件与行号（§2 全部标注位置）+ 既有 CTest 目标清单
（37 个 `add_test`）+ 既有 issue/文档检索（确认无重复建档）。
```

---

## 17. 下一步（唯一允许的动作）

```text
STEP-1（唯一） **Human Review 本契约**，重点裁定三件事：
  ① §7.3 对 T019「无 raw-hex」冻结的**有意超越**是否批准；
  ② §11 GAP-1 的三种实现方向选哪一个（(a) 扩 TransactionAnalysis /
     (b) 新增 ActiveReadResult / (c) 呈现层按需调 core 解码器）；
  ③ §9 新提出文案（标〔待 Review〕者）是否逐字采纳。
STEP-2 取得裁定后，**另起一轮** = Implementation Round：
  按 GAP 矩阵实施 + READ-R1…R9 测试 + 真实 windows QPA 几何门禁 → 人工验收。
未经 STEP-1 裁定，**不得**开始 STEP-2；不得自动开始 M11。
```

---

## 18. 知识问答（Knowledge Ownership）

| # | 问题 | 答案要点 |
| --- | --- | --- |
| 1 | 一次 FC03 读取成功后，产品今天告诉用户什么？ | 只有「成功」+ 耗时；**不含值、不含参数、不含字节**。 |
| 2 | TX 字节今天存在吗？在哪里？ | 存在。`record.evidence.requestAdu` = 发送时描述符的 wire，逐字节含 CRC，永不事后重编码。 |
| 3 | RX 字节今天存在吗？在哪里？ | 存在。`record.evidence.responseAdu` = transport 观测原始字节，含损坏、永不截断。 |
| 4 | 那么为什么用户看不到？ | 8 个 model role 无字节/参数出口；Communication 页无响应区；QML 零消费这些字段。 |
| 5 | 真正的 backend 缺口是什么？ | 只有一个：**成功时的寄存器值在分析器内部被丢弃**（`decodeReadHoldingRegistersResponse` 的 `values` 未被保留）。 |
| 6 | 这是正确性缺陷吗？ | 不是。既有分类逻辑未被发现错误；缺的是「把已存在的事实说出来」。 |
| 7 | 为什么不能把这些字节塞进 Transactions 行？ | 行是摘要（5 列 + 可选副行）；255 字节会破坏 M9 冻结的行结构与 1000×700 预算。 |
| 8 | 为什么不能在 Communication 页恒显一个响应区？ | 1000×700 下写区仅余 25px；会复现 ISSUE-016/018 的越界缺陷。 |
| 9 | 为什么要用真实 windows QPA 验证？ | offscreen 行高 12px vs 真实 16px，offscreen 绿不代表真实平台绿。 |
| 10 | PREVIEW == WIRE 是约定还是构造？ | 构造：预览与派发调用同一个 `encodeActiveRequest`。 |
| 11 | 为什么 PossiblySent 不能写成「已发送」？ | 它只证明传输 API 接受了写入；字节是否抵达设备不可证。 |
| 12 | 空 RX 应该怎么写？ | 「未观测到任何字节」——空不等于失败，也不是「空响应」。 |
| 13 | 非 Success 时可以显示「看起来像数的字节对」吗？ | 绝对不可以：展示未验证的值 = 编造设备内容。 |
| 14 | UnknownResponse 该怎么处理？ | 合法终态：呈现 TX/RX 原始字节 + 「响应无法判定」，禁止编造更具体原因。 |
| 15 | 值解释（Float32 / byte order）属于哪一轮？ | M11（Register Readout & Decode），本轮只做原始 uint16 + index/地址。 |
| 16 | 本轮改了代码吗？ | 没有。只读审计 + docs-only 契约冻结。 |
| 17 | 本轮推进 LKGC 了吗？ | 没有。docs-only 提交永不作 LKGC。 |
---

# Part B — Implementation Round（2026-09-24，T023 实施）

> 本节为 T023 契约**实施轮**的过程档案追加（§7 Human 三项裁定通过后执行；历史 Part A
> 原文不变）。涉及 Human 三项裁定的落地方式：**DECISION 1**（有限超越 T019「无 raw-hex」
> 冻结，仅限 FC03 Read Result evidence）→ 已归档为 **T019 §40** 追加批注；
> **DECISION 2**（GAP-1 选 **(a) 扩展 canonical `TransactionAnalysis`**，两处 UI 共用同一
> transaction truth）；**DECISION 3**（10 个用户可见主标题逐字冻结，并**改序**为
> CLASS-01 请求未发送 … CLASS-10 读取成功 —— 本 Part 的编号一律为该新序）。

## B1. 实施范围（按契约章节映射）

| 契约条目 | 落地 |
| --- | --- |
| §5 GAP-1（core 派生缺口） | `TransactionAnalysis` 追加 `std::vector<std::uint16_t> values`（append-last，保持聚合兼容）；`analyzeFunction03Transaction` 的 Success 分支保留已解码 payload（RAW uint16，无任何解释） |
| §8 READ-TXN（Transactions 关系） | **未**扩大 `TransactionListEntry` / model role（READ-TXN-2）；读取结果经 `AnalysisController` 的 canonical read-result projection 出口，与 session record 同源（`captureReadResultFromRecord` 直接取自刚归档的 record） |
| §6 READ-UI（结果面两层） | Communication 页 `requestPreviewPanel` 列内新增恒显结论行（summary + fact + 紧凑「查看详情」文字入口）+ 页末有界可滚动 `readResultDialog`（请求回显 / Actual TX + 处置分档 / Actual RX / 接收字节数 / 确定性依据 / 可能原因 / CLASS-10 原始寄存器表） |
| §3 分类法 | `ReadResultClass`（10 值）+ 纯映射 `classifyFc03ReadResult` + `readResultClassMachineToken` + `readResultClassTitle`（逐字冻结）+ `readResultPossibleCausesFor`（无根因词）+ `standardExceptionNameZh`（仅 0x01…0x04） |
| §19 WAITING | 传输**接受**请求的瞬间进入等待态（非终态），终态出现即被**恰好一个** terminal 替换 |
| §29 exactly-one-terminal | 本地拒绝产生 0 record / 0 terminal；完成 = 1 record；提交后中止 = 1 terminal + 0 record |
| 源切换语义 | Simulator / Replay 切换与 `connectSerial` 新会话均清空读取结果（不得跨会话/跨源展示旧字节） |

## B2. Files Changed（行为/测试提交）

- `src/core/analysis/TransactionAnalysis.h/.cpp` — `values` 字段 + Success 分支保留 payload；
- `src/ui/TransactionListModel.h/.cpp` — 10 类分类、机器 token、冻结标题、可能原因、HEX 投影；
- `src/ui/AnalysisController.h/.cpp` — read-result snapshot（唯一写入点 = 3 个 capture helper）+ 28 个只读投影属性 + 等待态入口 + 本地拒绝/终端/记录三路捕获 + `connectSerial` 会话清空；
- `src/ui/qml/pages/CommunicationPage.qml` — 结论行 + 详情对话框（有界滚动）；
- `src/main.cpp` — 聚合初始化补 `.values = {}`；新增 harness 方法 `completeReadWithBytes`；新增门禁 `--qml-read-result-check`；
- `src/core/analysis/PassiveTransactionAnalysis.cpp`、`src/core/serial/SerialTransactionSession.cpp`、`tests/*` — 聚合初始化补 `.values = {}`（消除 `-Wmissing-field-initializers`，零行为变化）；
- `tests/fake_serial_transport.h/.cpp` — 新增 `completeWithResponseInChunks`（READ-R2 分片刺激）；
- `tests/test_ui_bridge.cpp` — 新增 **rr01–rr08**（READ-R1…R8 断言层）；
- `CMakeLists.txt` — 新增 CTest 条目 `qml_read_result_check`（**37 → 38**）。

## B3. 1000×700 几何与布局决策（§7 / READ-UI-6）

首版把结果区做成独立 `ColumnLayout` + 34px `AppButton`，真实 windows QPA 下
`--qml-write-foundation-check` 复现 **ISSUE-016/018 形态**：
`WRITEFAIL C4 geometry 1000x700 0x10: writeFoundationPanel is clipped by the window
(scene 73,398 911x319 vs window 1000x700)`（bottom = 717 > 700）。
**修复**：结果行并入既有预览列、详情入口降级为紧凑文字行（复用 T023 方案 B 的
「恒显结论行 + 有界详情」），复测 `writeFoundationPanel=(73,372 911x319)`（bottom 691 ≤ 700）。
该最坏情形（写区双 Tab 的 test-foundation 模式）由既有 `qml_write_foundation_check` +
`qml_write_foundation_check_windows` 在真实平台持续把守；本轮新增的
`--qml-read-result-check` 在**生产模式**（仅 0x06）下额外实测：

```
READ [geometry 1000x700 125-reg]: window=1000x700
  communicationRequestSection=(73,182 911x131) readResultPanel=(85,290 887x11)
  readResultSummary=(85,290 99x11) readResultSummaryFact=(192,290 728x11)
  readResultDetailsButton=(928,290 44x11) commReadButton=(880,245 92x24)
  writeFoundationPanel=(73,344 911x148) writeValidationError=<hidden>
  writeActivateButton=(85,446 50x34)
```

## B4. Verification（真实命令与输出）

```text
# Debug 全量（真实 ctest，Windows 交互式会话）
ctest -C Debug
  → 100% tests passed, 0 tests failed out of 38        （37 → 38）
  → Test Passed count: 38；ReferenceError 0 / TypeError 0 / Unable to assign 0
  → qml_read_result_check PASS（约 5 s）
  → ui_bridge 76 passed（含新增 rr01–rr08）

# 新门禁（真实 app + 真实 QML）
modbuslens --qml-read-result-check   → exit 0
  READ [C waiting]: TX=[01 03 03 E8 00 02 44 7B] (== preview == encoder)
  READ [C/CLASS-10]: 共 2 个寄存器… 值=100/200 地址=1000/1001 RX=01 03 04 00 64 00 C8 BA 7A
  READ [D/CLASS-03]…[J/CLASS-02]：八类终端逐类 PASS
  READ [K dialog]: 125-register evidence view reachable and scrollable at 1000x700
```

**负向对照（READ-T-4，实测）**：临时把 `readResultRxText()` 的空态文案改为
`"NEGATIVE-CONTROL"` 后重建 → `rr07` **FAIL**（exit 1）且
`--qml-read-result-check` **FAIL**（C/D 两处 READFAIL）→ 还原后二者复绿。
证明断言不是死断言。

## B5. CLASS-09（无法识别的响应）可达性结论（如实报告）

按 §28/READ-T-4「若架构无法稳定构造 Unknown 分支则不得伪造测试」的要求，本轮**枚举了
全部可达输入**：`analyzeFunction03Transaction` 对每一条 `ProtocolError` 路径都附加了
确定性 issue（FrameTooShort / AddressMismatch / Malformed×2 / QuantityMismatch /
UnexpectedResponseFunction）；两条 `UnknownProtocolError` 路径（请求帧不可解码的防御
分支、`analyzeActiveResponse` 的防御尾部）在**受信任的主动 FC03 请求**下均不可达
（请求恒由 `encodeActiveRequest` 产生并校验）。**结论：CLASS-09 在生产主动 FC03 线上
不可达**。处理方式：**不伪造** wire 场景；契约在**映射层**断言——`rr01` 直接驱动
production `classifyFc03ReadResult` 覆盖 §10 矩阵全部 14 行（含无 issue 的
ProtocolError、`UnknownProtocolError`、越界枚举 fallback token 与 `ExpectedNoResponse`），
并验证其标题/机器 token/可能原因契约。该结论属架构事实记录，**不是缺陷**（UNK-1）。

## B6. Result / 状态

- Debug CTest **38/38** PASS；诊断计数 **0/0/0**；`qml_read_result_check` 新增并 PASS；
- M10 = **REOPENED / CORRECTION（实施完成，待 Human Review）**；M10-F = **HOLD**；
  **M11 = HOLD / NOT STARTED**；REAL MODBUS HARDWARE = NOT VERIFIED；
- **verified LKGC 未推进** = `d08ab55c71f54211e35f6bcdf0c2ec026a1d185f`
  （行为提交仅为 **candidate**；推进需 Human 明确授权）；
- 未 push / 未 tag / 未 amend；本轮**未做 package**（无 windeployqt / make_package.py）。

## B7. Knowledge Learned

1. **「同一提交里存在某能力」≠「结构性保证成立」**：portable 证据只能证明能力进入产物；
   本轮 `TX==preview==encoder` 由**构造**保证（同一 `encodeActiveRequest`），并用
   `rr02` 把它锁成断言。
2. **QML 无限定名解析规则再次生效**：结果行并入既有 `requestPreviewPanel` 列内，读取全部
   走 `page.analysisController.*` 显式限定，避免中间祖先属性不可见坑。
3. **`Item` 根必须给 implicit 尺寸**：新增 RowLayout 挂在既有 ColumnLayout 内，
   未新增根为 `Item` 的自定义组件，规避了 ISSUE-016 的 0 高度坑。
4. **终态写入点的唯一性**是可测性的前提：snapshot 只有 3 个 capture helper 可写，
   `rr01–rr08` 才能只断言投影而不必重新推导分类。
5. **负向对照必须真的跑**：本轮实测注入文案缺陷后 rr07 与门禁同时变红，还原后复绿。

## B8. Git Commit

- 行为/测试提交：**`a494d9c`**（T023 implementation：core values + read-result
  projection + Communication read-result UI + rr01–rr08 + `qml_read_result_check`）
- 文档归档提交：见本节下方（docs-only，永不作 LKGC）（本 Part B + T019 §40 批注 + PROJECT_STATUS/BACKLOG/devlog）
---

# Part C — Read Function Code Editable + Text-Input UX（2026-09-24，M10 correction）

> Human 新发现（Part B 交付后）：Request 区不得再用 SpinBox/上下箭头输入，且
> 「读取功能码」必须可直接键盘输入（默认 03）。本 Part 记录该轮实施；Part A/B 原文不变。

## C1. 需求 → 设计

- **语义/字节分离（§5）**：`ReadHoldingRegistersIntent` 追加 `functionCode`
  （append-last，默认 0x03）—— 这条 UI 始终是 **register-read schema**
  （请求 = Start+Quantity，响应 = ByteCount+uint16 字），wire function byte
  是该 schema 的用户可选事实。`activeRequestFunctionCode()` 是 wire byte 的
  唯一权威。FC01/FC02 不得被解释成 bit/coils —— schema 恒为 16-bit 寄存器。
- **验证（§3）**：文本 parser 只做 trim/格式/溢出；十进制字段复用 core
  `parseDecimalRegisterValue`，功能码字段新增 core `parseReadFunctionCode`
  （1..2 位 HEX、可选 0x/0X、大小写均可；拒绝空/GG/0x/123/0x123/负数）。
  业务范围仍由 controller typed guards + 新增 `ReadFunctionCodeOutOfRange`
  （0x01..0x7F；0x80..0xFF 为异常位空间，不得静默接受）。
- **动态异常（§11）**：分析器从 REQUEST FRAME 派生期望码 —— F、F|0x80
  （03→83、04→84、41→C1），无 per-code 副本。
- **Read Result 动态（§12）**：snapshot 携带请求功能码；证据对话框请求回显
  含「功能码 FCnn (0xnn)」+ 新增「响应功能码」行（Success = 同码；
  UnexpectedResponseFunction = core `actualFunctionCode`）。

## C2. UI 布局（§1/§7/§13/§16）

```text
Row1 = 从站地址 [文本] ｜ 读取功能码 [文本] ｜ 动态紧凑说明（elide）
Row2 = 起始地址 [文本] ｜ 寄存器数量 [文本] ｜ 超时(ms) [文本] ｜ [读取寄存器]
然后 = 起始地址 HEX echo → PDU/RTU Preview → Read Result summary
```

HEX echo 改为 controller preview 投影（删除 QML `Number()` 二次解析）；静态
「Function: FC03…」标题删除；按钮文案 → 「读取寄存器」。控件复用既有
`DecimalField`（presentation-only raw-text，无 QML 校验器 —— 与 write drafts
同一纪律）。

## C3. 1000×700 预算（§17，真实 windows QPA 实测）

初版字段沿用 DS 34px（`DS.controlHeight`）→ **writeFoundationPanel bottom
711 > 700**（ISSUE-016/018 形态再次复现，本机真实平台门禁捕获）。修复 =
五个字段 `implicitHeight: 24`（= 被替换 SpinBox 行高）→ 恢复 691 ≤ 700。
最终测量（1000×700 生产模式）：
`communicationRequestSection=(73,197 911x139) readResultPanel=(85,310 887x14)
readResultDetailsButton=(928,310 44x14) commReadButton=(892,241 80x24)
writeFoundationPanel=(73,372 911x151)`；写区双 Tab 最坏情形
`writeFoundationPanel=(73,372 911x319)` → bottom 691。

## C4. 测试（READ-FC1…FC9，§20）

- core：`test_active_request` +ac16–ac18；`test_transaction_analysis` +fc01–fc03。
- controller：`test_ui_bridge` +READ-FC1…FC8。
- QML 门禁：`--qml-read-result-check` 扩展 M1–M5（READ-FC2/3/6/7/8 + FC9
  UI→preview→actual TX 恒等，经真实字段与真实按钮驱动）。
- **负向对照（READ-T-4）实测**：临时让 dispatch 忽略输入功能码恒发 0x03 →
  READ-FC2/FC5 FAIL + 门禁 7 条 M1/M2 READFAIL → 还原后全部复绿。

## C5. 验证（真实命令与输出）

```text
Debug CTest   → 100% tests passed, 0 tests failed out of 38（诊断 0/0/0）
Release CTest → 100% tests passed, 0 tests failed out of 38（诊断 0/0/0）
真实 windows QPA 七门禁（Release）：
  qml-smoke-test=0  qml-production-write-check=0  qml-write-foundation-check=0
  qml-focus-check=0 qml-nav-check=0 qml-geometry-check=0 qml-read-result-check=0
  （合计 ReferenceError 0 / TypeError 0 / Unable to assign 0）
R15/R16/R17 portable markers：9 条独立 marker 行全在；
  R17: FC16 no-response dispatch -> exactly one Timeout + 写状态未知通知。
```

## C6. Result / 状态

M10 = REOPENED/CORRECTION（Part C 实施完成，待 Human Review）；M10-F = HOLD；
M11 = HOLD / NOT STARTED；REAL MODBUS HARDWARE = NOT VERIFIED；未 package
（Human 将在 WorkBuddy 外 native PowerShell 重新 canonical package）；
verified LKGC 未推进 = `d08ab55c71f54211e35f6bcdf0c2ec026a1d185f`；
行为提交 = `352b81c`；未 push/tag/amend。

## C7. Knowledge Learned

1. **34px DS 控件 vs 24px 旧控件**：替换控件时必须以「被替换控件的实际行高」
   而非设计系统默认值为预算基准 —— 1000×700 的 slack 只有 ~25px。
2. **门禁 staged-walk 中阶段内再 push 是队尾追加**：断言会推迟到全部后续
   阶段之后执行（中间的 runDemoBatch 清空了被断言状态）；同步控制器状态
   必须同阶段断言。
3. **QCOMPARE 与花括号初始化列表**：`QCOMPARE(x, std::vector<u16>{a,b})`
   的逗号会被宏当参数分隔 —— 必须外加括号。
4. **`QString::arg(int, QString)` 不存在**：混合类型多参必须链式 .arg()。
5. **`QString::arg(uint8_t, 2, 10)` 是十进制格式化**："FC41" 这类 HEX 标签
   必须先格式化 HEX 字符串再拼（0x41 被格式化成 FC65 的实测教训）。


---

# Part D — Human Final Acceptance Archive & M10 Read Correction Closure（2026-09-24，docs-only governance）

> Human 对冻结的 Final D checklist 明确回复：「全部 PASS」。本 Part 只归档该验收与既有
> machine evidence，并正式收口 M10 Read Correction。Part A/B/C 原文不变。
> 措辞纪律：不记录 COM 编号、设备型号、slave/寄存器地址、读取值、截图、时间戳；
> 不把 FC04 / 自定义 FC 的 UI·preview·feedback PASS 扩写成真实设备成功响应。

## D1. Human Final Acceptance（逐项，仅记录实际收到的 PASS）


| Final D SHA-256 checked (`D013B12F…B3FFBC65`) | PASS |
| Five text fields keyboard editing（从站地址/读取功能码/起始地址/数量/超时） | PASS |
| Row1 / Row2 layout | PASS |
| FC03 real-device regression | PASS |
| FC04 UI / preview / feedback | PASS |
| Custom FC input / preview / feedback | PASS |
| Invalid Function local rejection | PASS |
| Baud options（1200/2400/4800/9600/19200/38400/57600/115200） | PASS |
| 1000x700 layout | PASS |
| Overall M10 editable-read-function Human Review | PASS |

## D2. Real-hardware evidence boundary（依 T022 §ZE14 OPTIONAL contract）

- **Real-device FC03 read regression = HUMAN PASS**（本次 checklist 明确项）。
- **Optional full real-hardware suite（FC03 read / FC06 write / FC16 multi-write /
  actual device value verification / write restoration）= NOT FULLY VERIFIED /
  NON-BLOCKING** —— 本次 checklist 未包含、亦无新的独立真实证据；
  与原 M10-F OPTIONAL contract（§ZE14：允许以 REAL HARDWARE NOT VERIFIED 收尾，
  须显式披露）保持一致。
- FC04 / 自定义 FC 的「UI / preview / feedback PASS」**仅指界面·预览·反馈链路**，
  repo 中不存在其真实设备成功响应的独立证据，故不作该声明。

## D3. 归档的 machine evidence（既有事实，本轮未重新执行）

- Behavior commit：`352b81c82d5efa9aac5418cccaaf1605a68cd9d3`（Part C，最终行为树）。
- Behavior 前历史中间提交：`a494d9c`（Part B）—— historical intermediate，非 LKGC candidate。
- Human Review 前最后 docs archive：`13e5a13e4470c5eb593d5209410087c9341d59e0`。
- Source-tree acceptance：Debug CTest **38/38**、Release CTest **38/38**（诊断
  ReferenceError 0 / TypeError 0 / Unable to assign 0）；source-tree **Release binary**
  windows-QPA gates **7/7**；负向对照（dispatch 恒发 03 → READ-FC2/FC5 FAIL + 门禁
  7 READFAIL → 还原复绿）PASS。
- Canonical package：PASS。Final D = size **4406582**，
  SHA-256 **`D013B12FEB1BAB1AD10FEE80761AD57C8F2EF76DF474CEFA47A64F77B3FFBC65`**；
  ZIP = size **41082750**，SHA-256 **`852266176C96E472BDF839B2A4E57D9353587971057D254CFA4989A2B3289BD9`**。
- Final-D portable verification（Agent，clean child env / windows QPA）：A==B==C==D
  identity PASS；**portable 七门禁 7/7 exit 0**；诊断 0/0/0；Read Result 回归 PASS
  （八类 + 125 寄存器对话框）；R15/R16/R17 markers 9 条 PASS；1000x700 PASS；
  package-local windows platform launch PASS。
- 措辞修正：Part C 阶段运行的七门禁是 **source-tree Release binary 的 windows-QPA
  gates**（非 portable gates）；portable 七门禁指本 D3 节对 Final D 的复跑。两者全部 PASS。

## D4. 最终产品行为清单（M10 Read Correction 交付范围）

A FC03 读取事务可观测 · B 可见终态结果 · C Actual TX · D Actual RX ·
E TimeoutNoData · F Partial RX 保留 · G Modbus Exception · H CRC 失败 ·
I 响应不匹配 · J 响应格式错误 · K Success + raw uint16 寄存器（DEC/HEX）·
L 五个可编辑文本输入（Slave/Read Function/Start/Quantity/Timeout）·
M 两行请求布局（Row1 = Slave+Read Function；Row2 = Start+Quantity+Timeout+Read 按钮）·
N Read Function 默认 03 · O FC04 register-read 兼容路径 ·
P 自定义 register-read 兼容 Function Code 路径 ·
Q 动态 request/preview/TX/expected-response Function 恒等 ·
R 动态异常 Function = F|0x80 · S 非法 Function 本地拒绝 ·
T 波特率 1200/2400/4800/9600/19200/38400/57600/115200 ·
U 1000x700 布局保持 · V FC06/FC16 回归保持。

**仍不属于本范围（M11 边界，未实现）**：int16 语义解释、float32、word swap、
scaling、工程单位、寄存器表、设备专属语义解码。

## D5. Closure

依 T022 §ZE17（Agent 不自行推进 LKGC）、§ZE14（optional 硬件 NON-BLOCKING）与
T023 §12（实施 → 真实 windows QPA 门禁 → 人工验收）复核：**无未满足 hard gate**。

- **M10 Read Correction（T023 Part A–D）= CLOSED / ACCEPTED**
- **M10-F = CLOSED**（其原 acceptance 结论未被推翻；optional 硬件边界按 D2 披露）
- **M10 = COMPLETE**
- **M11 = HOLD / NOT STARTED**

## D6. LKGC

- Proposed verified LKGC candidate = **`352b81c82d5efa9aac5418cccaaf1605a68cd9d3`**
  （Reason：它是对应最终 accepted `D013B12F…B3FFBC65` portable 与 Human PASS 的
  最后 behavior-bearing tree；`13e5a13` 与本轮 closure commit 均 docs-only）。
- **verified LKGC 本轮未推进**，仍为 `d08ab55c71f54211e35f6bcdf0c2ec026a1d185f`；
  推进须 Human 明确授权（T022 §ZE17 + AGENTS.md line 130）。

## D7. Git

- closure commit：见本文件下方 Git Commit 记录（docs-only）。
