# T010 — Serial Mode

> 状态：**DONE ✅（2026-09-08）**｜Part A：**DONE ✅**｜Part B：**DONE ✅**（Implementation + Manual Serial UI Smoke PASS + Hardware Smoke NOT RUN/hardware unavailable）
> T010 整体 DONE；**M5 关闭**；下一任务：T011 AI Diagnosis（未开始）。
> 前置确认：T008 DONE、T009 DONE、LKGC = `bb0a652` 之前的代码提交 `d473d36`（LKGC）、T010 未开始、ISSUE-001/002 RESOLVED。
> **ISSUE-003 RESOLVED ✅**（2026-09-07）：Qt Serial Port 已补装到正确 MinGW kit（五步实证全过，根因=多 kit 错位）。

## Goal

Serial Mode Part A：FC03 单事务串口运行时（Pure C++ 状态机）+ QtSerialPort 薄适配层。请求经真实串口发出→任意分块字节累积→candidate response 判终→复用 T004 Codec 与 T007 Analyzer→现有 Dashboard。**无硬件也可完全自动化验证。**

## Background

T009 完成后三种来源齐全：Simulator（现场生成 Response）、Replay（历史记录重新分析）、Serial（真实字节流）。三者最终必须继续汇聚于 TransactionAnalysis → Statistics → AnalysisController → 现有 Dashboard。**Serial 不允许建立第二套分析逻辑。**

## T010 的本质（vs Simulator/Replay）

| 模式 | 数据流 |
| --- | --- |
| Simulator | Request → SimulatedSlave → Response |
| Replay | Historical .mlog → Recorded Response / NoResponse |
| Serial | Request → encode wire → OS serial port → 设备 → 任意 byte chunks → 重建 candidate response → 现有 Codec → 现有 Analyzer |

## Part A Scope

做：FC03 single-transaction serial runtime；Pure C++ serial transaction state machine；request wire 构造；任意分块累积；response 判终；response timeout；partial-response-at-timeout 语义；one outstanding request 规则；QtSerialPort 薄 adapter；hardware-free deterministic tests；QSerialPort open/config failure smoke（可行时）。

不做（禁止）：Serial QML page、COM selector UI、continuous polling、QTimer polling loop、QThread/worker thread、read history DB、multi-request queue、Modbus TCP、FC04/FC06/FC16、passive RTU sniffer、t1.5/t3.5 generic framer、AI、Agent。Part B 才做 UI。

## Framing 前提（T010 核心知识点）

`QSerialPort::readyRead()` **不保证一次 signal = 一帧**。合法 response 可能被拆成多 chunk（如 `01 03` / `04 00 64` / `00 C8 BA 7A`）。禁止每次 readAll 直接当完整 RTU Frame decode。必须有 receive buffer 跨 readyRead 累积。

## 为什么 v1 不先做 t3.5 Gap Scanner

通用 passive framing 依赖 t1.5/t3.5 字符间隔；但当前场景是**主动 Master + 一次一个 outstanding FC03 request + 已知 quantity**——正常 response 期望长度可推导。v1 采用 **transaction-aware framing** 而非 generic passive sniffer framing：更简单、更确定；Windows/QTimer 毫秒级调度不适合伪装精密 gap detector；当前不是抓总线工具。**这不是说 t3.5 不重要**，而是当前 master transaction scope 不需要通用 RTU sniffer；未来做被动抓包再单独设计 gap-based framer。

## FC03 Response 长度规则

- 正常 response wire 总长 = **5 + 2×N**（Address 1 + Function 1 + ByteCount 1 + Data 2N + CRC 2）。quantity=2 → 9 bytes。request 发出后 expectedNormalResponseLength 已知。
- **Exception response 固定 5 bytes**（Address + 0x83 + ExceptionCode + CRC Lo + CRC Hi）。buffer ≥2 bytes 后若 `buffer[1] == 0x83`，期望完整长度 = 5——不要等 normal length。

## Completion Rule（v1 定案）

- 若 buffer function == 0x83 且 size == 5 → candidate complete；
- 否则 buffer.size() == expectedNormalResponseLength → candidate complete；
- candidate complete 后：`decodeRtuFrame(buffer)` → ResponseObservation → `analyzeFunction03Transaction`。
- **即使 function/address/byteCount 业务不匹配，只要 wire candidate 已形成，也必须交给现有 Codec/Analyzer 判断**——Serial runtime 不得自己实现 ProtocolError 规则。

## Oversized Buffer（不截断）

buffer.size() > 当前预期长度时：**不**偷偷截前 N bytes 丢弃尾部——那会掩盖 trailing garbage/frame boundary 错误。策略：保持整个 buffer，不立即发布，等 response timeout 收口，然后 `decodeRtuFrame(entireBuffer)`——通常得 CrcMismatch 或其他 Protocol 诊断。不为 T010 新增 FrameTooLong 错误码（除非实现阶段发现真实不可避需求，必须先说明）。

## Timeout Semantics（关键区分）

- **A. 一个 byte 都没收到**：buffer 空 → `ResponseObservation{NoResponse{}}` → analyze(elapsed, threshold)，elapsed≥threshold → **Timeout**。
- **B. 收到部分 bytes**（如 `01 03 04 00`）：**不能判 Timeout**——Timeout 的语义是"没有 Response"，而设备已发回了一些 bytes。正确路径：`decodeRtuFrame(partialBuffer)`（通常 FrameTooShort/CrcMismatch）→ 交 Analyzer → 通常 **ProtocolError 或 CrcError**。

**重点：No bytes by threshold = Timeout；Some bytes but incomplete/corrupt = Protocol/CRC diagnosis。禁止"Timer 到期就全部标 Timeout"。这是 Serial Mode 最重要的诊断语义之一。**

## elapsed 定义（v1）

从 request 被 Serial adapter 接受准备发送起，到完整 candidate 收齐或 response timeout fired。Qt App/adapter 层用 `QElapsedTimer`，只把 `std::chrono::milliseconds` 传给 Pure runtime/analyzer——**Core 不用 QElapsedTimer**。elapsed 含 request 传输 + 设备处理 + response 传输，对诊断工具足够；不追求微秒级 wire timing。

## Response Timeout

UI 默认建议 1000ms（与 Simulator/Replay 口径一致）；Serial runtime 允许配置 timeoutThreshold（约 100~10000ms，UI 范围 Part B 定）。Pure runtime 只接收 threshold 数值，**不创建 Qt timer**。

## One Outstanding Request

同一 Serial session 一次只允许一个未完成 request。Pending 时再次 begin → **Busy**（不 queue、不并行、不覆盖）。理由：Modbus RTU master 当前简单架构下 request-response 串行；未来 polling scheduler 在更高层排队。

## FC03 Request Encoder（核验结论 + 决策）

核验代码事实：T004 已有 `ReadHoldingRegistersRequest` 语义模型与 `decodeReadHoldingRegistersRequest`，但**没有任何 encode/make API**（T004 Part B 明确未实现，Simulator/Demo 用测试内 makeFc03Read 私有 helper；T009 用 fixture wire）。**决策：Part A Implementation 在 `src/core/protocol/Function03.{h,cpp}` 补最小 Pure C++ encoder**：

```cpp
enum class Function03EncodeErrorCode { InvalidQuantity };

using ReadHoldingRegistersEncodeResult =
    std::variant<ModbusRtuFrame, Function03EncodeError>;

ReadHoldingRegistersEncodeResult
encodeReadHoldingRegistersRequest(std::uint8_t address, std::uint16_t quantity);
// 只生成 semantic ModbusRtuFrame（data = big-endian start+quantity）；
// 仅传 quantity（start 恒 0？——见下）……

// 实际定案（避免歧义）：start/quantity 双参数，与 decode 对称：
ReadHoldingRegistersEncodeResult
encodeReadHoldingRegistersRequest(std::uint8_t address, std::uint16_t startAddress,
                                  std::uint16_t quantity);
```

要求：只看语义生成 ModbusRtuFrame（wire CRC 仍由 encodeRtuFrame 负责）；quantity 1~125（无效 → InvalidQuantity）；**不在 Controller 手写 `00 00 00 02` byte layout**；数量/地址校验放 Core semantic boundary（不让 QML 当唯一防线）。

## Slave Address

Serial FC03 read v1 只允许 unicast slave **1~247**。不允许 0（broadcast：FC03 读取要 Response，broadcast 无正常 reply 语义）；248~255 也不作普通 slave。address validation 放 Serial transaction start / Core semantic boundary。

## SerialTransactionSession（Pure Model，概念）

新目录 `src/core/serial/`（Zero Qt）。`SerialTransactionSession` **不打开 COM 口**。职责：begin FC03 transaction、保存 request/quantity/timeout、返回 request wire、累积 response chunks、判断 candidate complete、timeout 收口、调现有 Codec/Analyzer、完成后回 Idle、enforce one outstanding request。禁止 include：QObject/QSerialPort/QTimer/QElapsedTimer/QString/QByteArray。

```cpp
enum class SerialTransactionState { Idle, AwaitingResponse };
// 无 Connected/Disconnected——连接状态属于 QtSerialPort adapter，不是 transaction protocol state。
// 不建巨大状态机 framework。
```

## Start / Feed / Timeout API（概念定案）

```cpp
enum class SerialTransactionErrorCode { Busy, InvalidAddress, InvalidQuantity };

struct SerialRequestStart {
    ModbusRtuFrame requestFrame;
    std::vector<std::uint8_t> requestWire;   // adapter 直接 write，不得重新 encode
};
using SerialStartResult = std::variant<SerialRequestStart, SerialTransactionError>;

struct AwaitingMoreData { bool operator==(const AwaitingMoreData&) const = default; };
using SerialFeedResult = std::variant<AwaitingMoreData, TransactionAnalysis>;

SerialStartResult beginReadHoldingRegisters(std::uint8_t address, std::uint16_t startAddress,
                                            std::uint16_t quantity,
                                            std::chrono::milliseconds timeoutThreshold);
SerialFeedResult  feedResponseBytes(std::span<const std::uint8_t> bytes,
                                    std::chrono::milliseconds elapsed);
// onResponseTimeout：前提 state==AwaitingResponse；Idle 误用用 variant/error 表达，不用 assert 当 API 合同。
TransactionAnalysis onResponseTimeout(std::chrono::milliseconds elapsed);
void cancel();   // 丢弃当前 pending → Idle；transport disconnect 不得伪造成 Timeout/ProtocolError
```

失败以 error 返回，**不 throw、不返回空 wire、不用 qWarning 当业务控制流**。feed 不做 sleep/blocking wait。

## Completion 后 Reset

无论 Success/Exception/CrcError/ProtocolError/Timeout，Session 必须清 receive buffer、清 current request、state=Idle，随后允许下一次 transaction——上一次 response bytes 不得污染下一次请求。

## Transport Error 与 TransactionStatus 分层

Port not found / Permission denied / Port busy / Open failed / Write failed / USB 断开 = **Serial Transport Error**，不是 CrcError/Timeout/ProtocolError/Exception。TransactionStatus 只描述 Modbus request/response 观察。此边界必须清晰。

## QtSerialPort Adapter（薄层，概念）

位置：Qt/App 层（`src/ui/serial/` 或等效）。职责：QSerialPort + QTimer response timeout + QElapsedTimer + SerialTransactionSession。流程：open/configure → `session.begin...` → `serialPort.write(requestWire)` → start elapsed → single-shot timeout；`readyRead` → `readAll` → `session.feedResponseBytes(...)`，完成则 stop timeout + 结果回调；timeout → `session.onResponseTimeout(...)` → 结果。Adapter **不**重新做 CRC/Frame decode/FC03 analysis/Statistics。

**禁止 UI 主线程 `waitForReadyRead/waitForBytesWritten/sleep`**（会阻塞 QML）。用 QSerialPort signals + single-shot QTimer。**T010 第一次允许 QTimer，但仅用于 response timeout——不是 polling loop。**

## Serial Port 配置 v1

固定 8N1（8 data / None parity / 1 stop / no flow control）。Baud 列表（Part B 可选）：9600 / 19200 / 38400 / 57600 / 115200。不支持：1.5 stop bits、mark/space parity、custom baud editor、RS485 direction GPIO（真实需求出现再议）。

## RS485 边界

QSerialPort 管 COM/serial byte stream；USB-RS485 adapter 管电转换；ModbusLens 不控制 DE/RE GPIO、不处理终端电阻、不做波形采样——正常 dongle 由驱动暴露 COM port。

## Pure Session 测试矩阵（SERIAL-A01~A13）

| Test ID | 场景 | Expected | 优先 |
| --- | --- | --- | --- |
| SERIAL-A01 | begin address=1 start=0 qty=2 | request wire=`01 03 00 00 00 02 C4 0B`；state=AwaitingResponse | **P0** |
| SERIAL-A02 | feed 完整 `01 03 04 00 64 00 C8 BA 7A` elapsed=25ms | Success、elapsed=25、state→Idle | **P0** |
| SERIAL-A03 | 分三 chunk feed（`01 03`/`04 00 64`/`00 C8 BA 7A`） | 前两次 AwaitingMoreData，第三次 Success；证明 readyRead chunk ≠ RTU frame | **P0** |
| SERIAL-A04 | feed `01 83 02 C0 F1` elapsed=18ms | Exception、code=0x02、state→Idle | **P0** |
| SERIAL-A05 | feed `…C8 BA 7B`（坏 CRC） | CrcError | **P0** |
| SERIAL-A06 | begin 后不 feed，onResponseTimeout(1000ms) | Timeout | **P0** |
| SERIAL-A07 | feed 部分 `01 03 04 00` → Awaiting → onResponseTimeout | **不是 Timeout**；按真实 decode→T007 结果为 ProtocolError/CrcError | **P0** |
| SERIAL-A08 | begin 未完成再 begin | Busy；第一事务保持 pending 不被覆盖 | **P0** |
| SERIAL-A09 | 完成后再 begin | 成功开始；旧 buffer 不污染 | P1 |
| SERIAL-A10 | begin→feed 部分→cancel→begin 新 | 新请求干净开始 | P1 |
| SERIAL-A11 | 合法 CRC 但 address=2（request address=1） | ProtocolError（证明 T007 判断而非 Serial） | P1 |
| SERIAL-A12 | begin quantity=0 | InvalidQuantity，不生成 wire | **P0** |
| SERIAL-A13 | begin address=0 / 248 | InvalidAddress | **P0** |

SERIAL-A14（补充设计）：oversized buffer（先 normal 长度 bytes 后多 1 byte trailing）→ candidate 不提前发布，timeout 收口 decode(entire) → CrcError/诊断（锁定"不截断"语义）。P1。

## Adapter 无硬件测试（SERIAL-I01）

使用明确不存在的 port name（或平台可靠等价），Expected：Transport error；不 crash、不产出 TransactionStatus::Timeout。P1。若环境中不稳定跨平台，降级为 adapter/config helper unit test + compile/QObject signal smoke——不制造依赖具体 COM 列表的脆弱测试。

## 无硬件自动化与 Hardware Smoke 完成语义

自动化验收不要求 USB-RS485/真实 slave/COM3。真实硬件属 Part B Manual Hardware Smoke。**若用户无硬件：必须记录 `Hardware Smoke = NOT RUN, Reason = hardware unavailable`，不得伪报 PASS**。但只要 Pure session + QtSerialPort integration + UI + automated tests + open/error handling + standalone deployment 全过，T010 软件实现仍可完成——"无硬件也可开发/演示"的项目原始目标。

## Part B 预告 + Serial UI 结果语义（定案）

Part B 才做：Refresh Ports / Port ComboBox / Baud ComboBox / Connect/Disconnect / Slave Address / Start Address / Quantity / Timeout / Read Holding Registers Once / Serial Mode header / Transport error display / 复用现有 Dashboard+ListModel / optional hardware smoke。不做：Auto Poll / Start-Stop Monitoring / interval / multi-slave scheduler。

**结果语义定案**：每次 Read Once 完成后，**替换当前 active batch 为 1 transaction**（不追加 history）——与 Simulator/Replay 的 replace 语义统一。Success 单次 → Observed=1/Completed=1/Success=1/100%；Timeout 单次 → Observed=1/Timeout=1/0%。continuous history 未来独立任务再加。

## QtSerialPort 本机实证（2026-09-07，只读取证）

| 项 | 结果 |
| --- | --- |
| include/QtSerialPort | **不存在** |
| lib/cmake/Qt6SerialPort | **不存在** |
| bin/Qt6SerialPort.dll | **不存在** |
| QSerialPort/QSerialPortInfo | 不可用 |
| 残留 | 仅 doc config 与 translations |
| D:\QT\MaintenanceTool.exe | **存在**（官方补装组件的工具） |

→ **ISSUE-003（OPEN）**：Implementation 的 Qt adapter/SERIAL-I01 阻塞；Pure Session（SERIAL-A01~A14）不受影响。修复选项已备：MaintenanceTool 勾选 Additional Libraries → Qt Serial Port（给现有 kit 补官方组件，非装新 Qt、非第三方库）；待用户决策。

## 为什么不抽 IFrameSource（重新观察）

三模式调用形状仍不同：Simulator = Request→Generated Response；Replay = Recorded Batch→Analysis；Serial = Async byte stream→one transaction。真正共享的是 Modbus Frame / TransactionAnalysis / Statistics / UI Model，而不是统一的 "Frame Source"。仍不抽 IFrameSource，除非 Implementation 出现真实重复接口需求。

## Knowledge I Must Be Able To Explain（18 题）

**S-Q1 readyRead 为什么不能当"一帧"？** OS/驱动任意切块：一次 signal 的字节数无任何成帧保证；必须跨信号累积 buffer 再判终。
**S-Q2 为什么 v1 不先做 t3.5 通用 framer？** 当前是主动 Master + 单一 outstanding FC03 + 已知 quantity，正常响应长度可推导（transaction-aware framing 更简单确定）；t3.5 gap 检测需要精密时序，Windows/QTimer 毫秒调度不适配；当前不是抓包工具。
**S-Q3 正常 FC03 response 长度？** Address+Function+ByteCount 3 字节 + 2N 数据 + 2 CRC = 5+2N。
**S-Q4 Exception 为什么固定 5 bytes？** Address+0x83+ExceptionCode+CRC×2，无数据段；buffer[1]==0x83 时直接按 5 收口，不必等正常长度。
**S-Q5 为什么 partial response timeout 不是 Timeout？** Timeout 业务语义="没有 Response"；设备已发回部分 bytes 是"有 Response 但残缺/损坏"——应 decode 后得 FrameTooShort/CrcMismatch 路径，进 Protocol/Crc 诊断。
**S-Q6 为什么 Port Open Failed 不是 TransactionStatus？** TransactionStatus 描述 Modbus 请求/响应观察；打开失败是本地传输问题，属 Transport Error 层——混层会让"设备超时"与"线没插"无法区分。
**S-Q7 为什么一次只允许一个 outstanding？** Modbus RTU 无事务 ID、串行应答；并行请求的响应无法配对。v1 简单架构先串行，轮询调度留高层。
**S-Q8 为什么不能 waitForReadyRead？** 阻塞主线程 = UI 冻结；Qt 事件循环 + signals 是异步正确姿势。
**S-Q9 QTimer 为什么 T010 允许而 T008 不允许？** 用途不同：T008 的禁用它防的是 polling loop/播放动画；这里 single-shot QTimer 专用于 response timeout 定时，是异步 I/O 的超时收口，不是轮询。
**S-Q10 QElapsedTimer 为什么只能在 Qt adapter？** Core 是 Pure C++（Zero Qt），时间作为输入参数（std::chrono::milliseconds）传入；测时是 I/O 边界职责。
**S-Q11 RS485 与 QSerialPort 的关系？** QSerialPort 管应用侧 COM 字节流；USB-RS485 dongle（驱动暴露为 COM 口）管 USB↔RS485 电气转换与收发方向；ModbusLens 不含 GPIO/DE/RE 控制。
**S-Q12 8N1？** 8 data bits、No parity、1 stop bit、无流控——Modbus RTU 的常规配置；每字节 10 bit 线上时隙。
**S-Q13 为什么 FC03 禁 broadcast address 0？** FC03 是读请求，broadcast 无 reply 语义→必然"超时"假象；248+ 保留。
**S-Q14 为什么 Serial 仍复用 T007？** 一样的事实（状态机产出 Observation + elapsed + threshold）只允许一份分类实现；Serial runtime 只负责"形成 wire candidate"。
**S-Q15 为什么没硬件也能测 Serial runtime？** Pure Session 的输入是 byte span + elapsed，测试注入任意 chunk/超时序列即可全覆盖；硬件只在 Part B Manual Smoke。
**S-Q16 为什么仍不抽 IFrameSource？** 三源调用形状不同；共享的是模型层（Frame/Analysis/Statistics/UI Model）。FORCED abstraction 无益。
**S-Q17 三模式真正共享什么？** ModbusRtuFrame、Codec、analyzeFunction03Transaction、summarizeTransactions、Controller/ListModel/Dashboard。
**S-Q18 Serial 如何完成三模式共享 Core 的闭环？** Serial 只新增"字节流→ResponseObservation"这一特有环节，终点仍是 T007/T007B 与现有 UI——第三个来源不引入任何第二套分析/统计/展示逻辑。

## Implementation Plan（Part A，22 步）

1. 核验 QtSerialPort（已核验=缺失→ISSUE-003 待决）
2. 核验现有 FC03 encoder（已核验=无→补最小 encoder）
3. 实现 `encodeReadHoldingRegistersRequest`（Function03 补 encode API + 测试）
4. 创建 `src/core/serial/SerialTransactionSession.{h,cpp}`（Zero Qt）
5. 定义 state/error/result 类型
6. 实现 begin（含地址/数量校验、one outstanding）
7. 实现 arbitrary chunk feed + 累积
8. 实现 response boundary（normal/exception/oversized 规则）
9. 实现 onResponseTimeout（空 buffer 与 partial 两路）
10. 实现 cancel/reset
11. SERIAL-A01~A14 tests（+encoder 用例）
12. RED
13. GREEN
14. 创建 thin QSerialPort adapter（Qt/App 层；QSerialPort+QTimer+QElapsedTimer+Session）
15. SERIAL-I01（invalid port open）或稳定等价 smoke
16. clean build
17. full ctest
18. Core Zero Qt
19. docs 归档
20. code commit
21. LKGC
22. docs backfill

⚠ Implementation 前置：**ISSUE-003 需先解决**（用户决策补装 QtSerialPort 组件后，configure 以 `find_package(Qt6 COMPONENTS SerialPort)` 实证）。不得开始 Part B。

## Files Changed（本阶段）

- 新增：`docs/tasks/T010-serial-mode.md`（本文件）、`docs/issues/ISSUE-003-qtserialport-not-installed.md`、`docs/devlog/2026-09-07-T010-PartA-TestDesign.md`
- 修改：`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`
- 未改动：`src/`、`tests/`、`CMakeLists.txt`、`scripts/`

## Verification（本阶段，docs-only）

```text
QtSerialPort 只读取证：include/cmake/bin/lib 均无 SerialPort → 缺失（ISSUE-003）
FC03 encoder 核验：src/core/protocol/Function03.h 无 encode API → 需补
MaintenanceTool.exe 实证存在：D:\QT\MaintenanceTool.exe
git status 干净；git log 确认 LKGC=d473d36、HEAD=bb0a652、T010 未开始
git diff --check 通过；改动仅 docs/
```

## Result

Part A Learning / Test Design 完成：Serial 角色定案、framing 前提与 completion 规则定案、timeout 双路语义定案、Session 模型与 API 定案、矩阵 SERIAL-A01~A14 + I01 落库、18 题问答、22 步计划齐备；**发现并建档 ISSUE-003（QtSerialPort 未安装）——Implementation 的 Qt adapter 部分受阻待用户决策**。Serial 未实现；Part B 未开始；T010 整体 IN PROGRESS。

## Knowledge Learned

- **新任务的"能力实证先行"收益**：Learning 阶段 15 分钟只读取证拦下了一个会在 Implementation 第一天爆炸的环境缺口——写任何代码前解决。
- **transaction-aware framing vs gap-based framing**：主动 Master 场景下期望长度可推导，无需用不合适的工具（QTimer 毫秒级）伪装精密 gap detector。
- **Timeout vs partial response** 是 Serial 最精微的语义分界：它把"线断了"与"设备坏了"分开。

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| Part A Learning / Test Design | 见 `git log` | `T010(Part A): Serial 事务运行时设计 — Learning / Test Design（docs-only）`（含 ISSUE-003） |

> LKGC 维持 `d473d36` 不变（docs-only 不推进）。
---

# Part A — Implementation（2026-09-07）

## 环境前置：ISSUE-003 最终验证 PASS → RESOLVED

本轮先执行"安装后实证"：正确 MinGW kit 的 headers/CMake package/DLL/.a 全部 EXISTS，compiler 未变；仓库外临时 CMake probe（`find_package(Qt6 6.11 REQUIRED COMPONENTS Core SerialPort)` + link `Qt6::Core Qt6::SerialPort` + 真实 include/使用 QSerialPort/QSerialPortInfo）configure/compile/link/run 四步全过（运行时可见 2 个串口设备）。**ISSUE-003 = RESOLVED**（根因：初次补装落入 MSVC kit `D:\QTDesign\6.11.1\msvc2022_64`；随后装到正确 MinGW kit）。probe 不入仓库。

## Implementation

- **FC03 request encoder（T004 最小补全，Pure Core）**：`encodeReadHoldingRegistersRequest(address, startAddress, quantity) → variant<ModbusRtuFrame, Function03EncodeError>`。quantity 1~125 由 encoder 校验（InvalidQuantity）；**unicast 地址 1~247 校验留在 Serial Session 层**（原因：通用 codec 应保持地址无关；Session 是 unicast 语义的自然边界——分层理由已记入头文件与档案）。只生成语义 Frame，wire+CRC 仍归 encodeRtuFrame。
- **SerialTransactionSession（src/core/serial/，Pure C++20 Zero Qt）**：两态 `Idle/AwaitingResponse`（无 Connected/Disconnected——连接属 adapter）；`beginReadHoldingRegisters`（Busy/InvalidAddress/InvalidQuantity 失败即返回、绝无半初始化状态；成功清 buffer 存可信 request 返回 frame+wire）；`feedResponseBytes(span, elapsed)`（任意 chunk 累积；exact-candidate 才收口；oversized **不截断**等 timeout 整 buffer decode）；`onResponseTimeout(elapsed)`（空 buffer→NoResponse→T007（Timeout 由 elapsed≥threshold 判定）；非空→整 buffer decode→真实诊断；Idle 误用→NotActive variant 错误）；`cancel()`（丢弃 pending→Idle，不产出 TransactionStatus）；完成后必 reset（buffer/request 清空），上一响应不污染下一事务。
- **最终 framing 规则（按修正版实现并用测试锁死）**：
  - buffer < 2 → AwaitingMoreData；
  - `(buffer[1] & 0x80) != 0` → 异常格式 candidate = **5 bytes**（**不硬编码 0x83**——A15 以 0x84 锁死：5 字节即收口，T007 判 ProtocolError，不等 timeout）；
  - buffer[1] == 0x03 → buffer<3 则等；否则 candidate = **5 + buffer[2]（响应自身 byteCount，不是 request quantity 推导）**——A16 以 byteCount=2 的 7 字节响应锁死"不硬等 9 bytes"；
  - 其他正常 function code：v1 不发明长度解析器，保 buffer 至 timeout 整 buffer decode/analyze；
  - exact-candidate：`size == candidate` 立即收口；`size > candidate` 不截断（A14：9+1=10 字节单 chunk → Awaiting → timeout → CrcError 而非截断的 Success）。
- **QtSerialPort 薄 adapter（src/ui/serial/，Qt/App 层）**：QSerialPort（8N1 + caller baud）+ QElapsedTimer + single-shot QTimer（**仅 response timeout，非轮询**）+ Session；异步：readyRead→readAll→feed→完成则 stop timer + emit transactionCompleted；timeout→onResponseTimeout→emit；禁 waitForReadyRead/waitForBytesWritten/sleep；**write 失败与端口 fatal → session.cancel() + Transport Error，绝不伪造成 Timeout**；closePort/cancel 不产出 TransactionStatus。
- **CMake**：`find_package` 组件加 SerialPort；`serial`（session 测试）与 `serial_adapter`（Qt6::SerialPort **只链接这一个测试 target**——红线：modbuslens_core 不碰 Qt；app binary 也不链 SerialPort，等 Part B UI 接入才是真实 link graph）。

## Problems Encountered

- **PE-4（真问题，价值最高的一课）：QSerialPort 打开不存在端口时的 errorOccurred 反馈风暴**。实测：`open()` 失败后 errorOccurred 会发射"0（NoError）→ 10（DeviceNotFoundError）× 无限循环"；handler 里 close() 每轮又触发更多 error，导致堆栈崩坏（adapter 测试 SIGSEGV，gdb/最小 repro 定位）。修复：① errorOccurred 改 **Qt::QueuedConnection**（避免在 open() 调用栈内重入半开端口）；② `suppressPortErrors_` 标志——fatal error 只处理一次，后续发射至下次 startTransaction 前忽略；③ open 失败走 startTransaction 自身的同步返回 + 自 emit transportError，不依赖该信号。
- **PE-5（测试期望 vs 真实 wire-truth）**：SERIAL-A07 原按"partial→ProtocolError"写期望，实际 4 字节 partial `01 03 04 00` **恰好等于 RTU 最小帧形状（addr+fn+CRC=4）**，codec 将其按帧解释 → CrcMismatch → **CrcError**。修正：A07 双场景锁定真实行为（4B→CrcError；2B 低于最小帧→FrameTooShort→ProtocolError），两种都 ≠ Timeout——"以真实为准"原则的又一次践行。
- RED 期：serial_adapter 测试 target 引用了尚不存在的 SerialPortAdapter.cpp → configure 报 "Cannot find source file"（RED 调整后重录：15 处 undefined reference）。

## Verification

```text
核对正确 kit + 临时 CMake probe（不入仓库）：configure/compile/link/run 四步 PASS
RED：15 处 undefined reference（Session 全部 API + encoder + Adapter 全部成员/vtable）
GREEN：cmake --build exit=0
serial 测试：21/21（SERIAL-A01~A16 + encoder 3 用例）
serial_adapter 测试：2/2（SERIAL-I01 invalid port open→Transport Error 不崩不超时；I02 idle wiring）
ctest --preset debug-local：100% tests passed, 0 tests failed out of 18（原 16 + serial + serial_adapter）
clean build（--clean-first）：106 targets，编译 warning/error 命中 0（configure 期 QTP0001/QTP0004 dev 提醒为 Qt 6.11 既有现象，未在本任务引入/扩大）
Core Zero Qt：src/core/serial 仅标准库/自有 core 头文件（无 Qt include）
ISSUE-001 复查：6 处 get_if 全部作用于具名 local result
deploy_windows.bat：exit=0；Qt6SerialPort.dll 按设计不在 deploy（app binary 未链 SerialPort，真实 link graph 记录在案）；minimal-PATH --qml-smoke-test exit=0
ISSUE-003：RESOLVED ✅（closure evidence 见 Issue 文档）
```

## Result

**T010 Part A = DONE**（自动化全绿，完全无硬件：session 以 byte span+elapsed 注入全覆盖）。**T010 overall 仍 IN PROGRESS**（Part B 未开始）；**M5 仍 IN PROGRESS**（T009 ✅ / T010 Part A ✅ / T010 Part B ⬜）。无需用户人工验收的 Manual Smoke 门槛本轮（无 UI 变更——Serial UI 属 Part B）。

## Files Changed

- 新增：`src/core/serial/SerialTransactionSession.{h,cpp}`、`src/ui/serial/SerialPortAdapter.{h,cpp}`、`tests/test_serial_session.cpp`、`tests/test_serial_adapter.cpp`
- 修改：`src/core/protocol/Function03.{h,cpp}`（+encodeReadHoldingRegistersRequest）、`CMakeLists.txt`（SerialPort 组件 + serial/serial_adapter 两 test target + offscreen 列表）、`docs/issues/ISSUE-003-*.md`（RESOLVED）

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| Part A Implementation（code/config） | `b31233b` | `T010(Part A): implement serial transaction runtime and QtSerialPort adapter`（**新 LKGC**） |
| 归档 docs-only | `<docs-only HEAD>` | LKGC 哈希回填 |

> LKGC = `b31233b`；Part B 未开始。
---

# Part B — Serial UI Integration + Hardware/No-Hardware Smoke：Learning / Test Design（2026-09-08，docs-only）

> 本章为 Part B 设计定案；**Implementation ⬜ 未开始**。红线：本阶段 `src/`、`tests/`、`CMakeLists.txt`、`scripts/` 零修改。全部设计基于已核验的真实 Part A API（SerialPortAdapter.h：`startTransaction(portName, baudRate, slave, start, qty, timeout) -> bool`、`closePort()`、`hasActiveTransaction()`、`isPortOpen()`、signals `transactionCompleted(TransactionAnalysis)` / `transportError(QString)`）。

## SB-0 压缩后状态复核

git log/status 复核：T009 DONE、T010 Part A DONE、LKGC=`b31233b`、HEAD=`072ce16`、ISSUE-003 RESOLVED、工作区干净——与此一致。

## SB-1 Part B 核心目标与数据链

Part B 只完成：Serial Port discovery + Connect/Disconnect + FC03 Read Once + Transport error presentation + existing Dashboard integration + standalone deployment + manual no/some-hardware smoke。最终链路：

```text
QSerialPort -> SerialPortAdapter -> SerialTransactionSession -> T004 Codec
-> T007 Analyzer -> T007 Statistics -> AnalysisController
-> 现有 TransactionListModel -> 现有 QML Dashboard
```

**禁止第二套 Serial Dashboard / 第二套 Transaction model / Serial 专用统计器。**

## SB-2 Serial Controls Scope（UI 只加这些）

Refresh Ports / Port ComboBox / Baud ComboBox / Connect / Disconnect / Slave Address / Start Address / Quantity / Timeout / Read Holding Registers Once。**固定 8N1 + No Flow Control**（UI 显示静态 "8N1"，无 parity/stop/flow 选择器、无 custom baud editor、无 Auto Poll/Monitoring/Interval/Thread/Queue/Multi-slave scheduler）。

## SB-3 默认值与 Baud 选项

Baud=9600（选项固定 9600/19200/38400/57600/115200）、Slave=1、Start=0、Quantity=2、Timeout=1000ms。

## SB-4 Serial Port Discovery（只 enumerate，绝不自动 open）

`AnalysisController` 提供：`Q_PROPERTY(QStringList serialPortNames READ serialPortNames NOTIFY serialPortsChanged)` + `Q_INVOKABLE void refreshSerialPorts()`。来源**必须**是 `QSerialPortInfo::availablePorts()`（不扫注册表/不手排 COM1~256/不调 PowerShell）；QML 不直接接触 QSerialPortInfo。**此前 probe 看到的 2 个串口设备只是 discovery evidence，不证明它们是 Modbus 设备**——自动化与应用启动只能 enumerate；任何形式的自动 open/write/发请求一律禁止；真实打开只能来自用户明确操作。

## SB-5 Controller Serial State / Commands（命名沿现有风格）

```cpp
Q_PROPERTY(bool serialConnected READ serialConnected NOTIFY serialConnChanged)
Q_PROPERTY(bool serialBusy READ serialBusy NOTIFY serialStatusChanged)
Q_PROPERTY(bool hasSerialError READ hasSerialError NOTIFY serialErrorChanged)
Q_PROPERTY(QString serialErrorMessage READ serialErrorMessage NOTIFY serialErrorChanged)
Q_PROPERTY(QStringList serialPortNames READ serialPortNames NOTIFY serialPortsChanged)

Q_INVOKABLE void refreshSerialPorts();
Q_INVOKABLE void connectSerial(const QString& portName, int baudRate);
Q_INVOKABLE void disconnectSerial();
Q_INVOKABLE void readHoldingRegistersOnce(int slaveAddress, int startAddress,
                                          int quantity, int timeoutMs);
```

语义红线：**Replay parse error ≠ Serial transport error**——两个独立 error state（沿用 T009 最小增量，不重构既有错误系统）。不把 `QSerialPort*` 暴露给 QML；QML 不 import QtSerialPort（C++ module）。

## SB-6 输入边界 C++ 再验证（防 narrowing）

QML 可用 SpinBox/Validator，但 C++ Controller 必须在任何 cast 前验证：slave 1~247、start 0~65535、quantity 1~125、timeoutMs > 0、baud ∈ {9600,19200,38400,57600,115200}。非法 → setSerialError + 返回（不写 port、不建 transaction）。`slave=-1 / start=70000 / quantity=126` 不得"先 cast 再让 Core 背锅"。

## SB-7 Connect 的 source 语义（与 T009 原子规则同构）

**成功 Connect = 显式来源切换**：`modeLabel="Serial Mode"`、`sourceLabel="<portName> @ <baud>"`（如 `COM3 @ 9600`，无长路径）、**清空旧 Simulator/Replay active batch**（statistics 空 / rowCount 0 / rate=— / avg=—）——禁止 "Header=Serial Mode 但列表还是 Replay rows" 混合态。**失败 Connect = 原子保留**：旧 statistics/rows/mode/source 全部不动，仅 hasSerialError=true + message（对应 UI-S02）。

## SB-8 Disconnect vs Clear

`disconnectSerial()`：pending 事务 → session cancel；close port；serialConnected=false、serialBusy=false；**不清最后一次 Serial transaction result**——Disconnect 是关 transport 不是删诊断结果；Header 保持 Serial Mode/COM3 @ 9600。`clearResults()` 延续统一语义：清 statistics+rows+（serial error），**只清结果不切来源、也不强制断连接**（Clear ≠ Disconnect，职责分离，对应 UI-S08）。

## SB-9 Read Once 前置与启动

前置：`serialConnected==true && serialBusy==false`，否则 setSerialError（**不得构造 TransactionStatus::Timeout、不得建 row**）。启动成功（adapter 接受）：serialBusy=true；**清除旧 serial error**；**保留上一条 completed result 同屏**（直到新结果 replace——等待期 UI 显示 "Reading..."）；此时才保存 pending metadata：`pendingSerialAddress_`（functionCode 恒 0x03）；transport failure/cancel 清 pending metadata——旧 metadata 不得污染下一事务。

## SB-10 Read Once 完成：active batch = 1（replace 语义）

五种结果都是合法 TransactionAnalysis。完成时：pending metadata + analysis → 构造 **1 个** TransactionListEntry；`summarizeTransactions({analysis})` → **replace** 当前 batch（**禁止 append**，rowCount 恒 1，对应 UI-S07）。

Success（elapsed 25）：Observed=1/Completed=1/Pending=0/Success=1/Rate=100.0%/Avg=25.0。Timeout（elapsed 1000）：Observed=1/Completed=1/Timeout=1/**Rate=0.0（合法值，非 nullopt）**/Avg=—（hasAverageSuccessLatency=false）。

## SB-11 Transport Error 只进 error UI

Open failed/Permission denied/Port busy/Write failed/Fatal serial error/USB 断开 → **只** serial error UI；pending 则 cancel + serialBusy=false；**不得生成 Timeout/ProtocolError/CrcError、不得发布新 row**；旧 completed batch 保持。与 Transaction row 的 Timeout/CRC Error/Protocol Error 是两类东西，UI 上必须可区分（label 前缀 "Serial transport error:"）。

## SB-12 Hardware-free Controller Mapping Seam（关键测试缝）

自动化不能依赖真实 COM，但要证明 "Serial TransactionAnalysis → 现有 Dashboard" 映射正确。**定案：非 Q_INVOKABLE 的 C++ 应用层发布 helper**：

```cpp
// C++-side entry point（与 applySnapshot/setTransactionEntries 同风格，不进 QML）：
void publishSerialResult(const QString& sourceLabel, int deviceAddress,
                         const modbuslens::core::TransactionAnalysis& analysis);
// 内部：1 entry + summarizeTransactions({analysis}) replace batch；
//       modeLabel="Serial Mode"；sourceLabel=入参；clearSerialError；serialBusy=false。
```

生产路径：`connectSerial` 成功时存 `serialSourceLabel_="<port> @ <baud>"`；adapter.transactionCompleted → `publishSerialResult(serialSourceLabel_, pendingSerialAddress_, analysis)`。**测试直接构造 TransactionAnalysis fixture 调该 helper**（Success 25ms / Timeout 1000ms）——与 T008 "Demo 禁伪造 status" 不冲突：T008 是产品业务链必须真实跑 Core；此处只验证"已得到的 Core result 如何映射 Qt UI"，Serial Core 正确性已由 A01~A16 锁定。**不建** ISerialTransport framework / 通用 DI framework / IFrameSource。

## SB-13 Source Switching 全景（原子性矩阵）

| 切换 | 行为 |
| --- | --- |
| Run Demo Batch（Serial 已连） | cancel pending（如有）→ close port → serialConnected/Busy=false → 发布 Demo → mode/source=Simulator → **清 serial error**（后台 COM 不得在 Simulator Mode 仍占用） |
| loadReplayFile | 先完整 read/parse/analyze——**Replay 失败不动 Serial 任何状态**（不关、不切、不毁 batch）；只有全部成功后：cancel pending→close→发布 Replay→mode/source=Replay → 清 serial error |
| Connect Serial 成功 | 清旧 batch → Serial Mode/新 sourceLabel |
| Connect Serial 失败 | 旧 source/batch 原样保留，仅 serial error |

## SB-14 QML Serial Panel（轻量 GroupBox）

`GroupBox "Serial Controls"`：Port ComboBox + Refresh / Baud ComboBox / Connect / Disconnect（RowLayout）；Slave/Start/Quantity/Timeout 四个 SpinBox + "8N1" 静态文本；`Read Holding Registers Once` 按钮。Enable 规则：Port/Baud ComboBox connected 时 disabled；Connect enabled = !serialConnected && 有选中 port；Disconnect enabled = serialConnected；Read Once enabled = serialConnected && !serialBusy；输入字段 busy 时 disabled；busy 时显示 "Reading..."。无动画 framework。

## SB-15 Serial Error UI

现有 Replay error label 保持；**新增独立 Serial error label**（`hasSerialError` 可见 + 前缀 "Serial transport error:"）——轻量 Label，无 MessageBox/Toast/Notification center。与 Transaction row 视觉可区分。

## SB-16 测试矩阵（UI-S01~S09 + PE-4 回归）

| Test ID | 场景 | Expected | 优先 |
| --- | --- | --- | --- |
| UI-S01 | 初始状态 | serialConnected=false、serialBusy=false、hasSerialError=false；refreshSerialPorts() 不 crash；serialPortNames 可读 | P1 |
| UI-S02 | 先 Demo/Replay golden，再 connectSerial(不存在 port) | hasSerialError=true；旧 statistics/rows/mode/source 全部不变；无新 TransactionStatus row | **P0** |
| UI-S03 | 未连接时 readHoldingRegistersOnce | serial error；row/statistics/source 不变；**不得 Timeout** | **P0** |
| UI-S04 | 非法输入（slave=0 / start=65536 / quantity=126 / timeout=0 各取代表） | 拒绝；不 narrowing、不写 port、不建 transaction | P1 |
| UI-S05 | seam：publishSerialResult("COM_TEST @ 9600", 1, Success 25ms) | mode=Serial Mode；sourceLabel=COM_TEST @ 9600；rowCount=1（Success 25ms）；statistics 1/1/0、rate=1.0、avg=25.0 | **P0** |
| UI-S06 | seam：publishSerialResult(…, Timeout 1000ms) | rowCount=1（Timeout）；successRate has=true value=0.0；avg has=false | **P0** |
| UI-S07 | seam：publish Success → publish Timeout | rowCount 仍 1（不 2）；statistics 只表最新 | **P0** |
| UI-S08 | Serial result 存在 → clearResults() | all counts=0、rowCount=0；mode/source 仍 Serial；连接状态不因 Clear 改变 | P1 |
| UI-S09 | failed connect（error=true）→ 成功 source switch（runDemoBatch） | hasSerialError=false、message 空（过时 transport error 不得常驻） | P1 |
| SERIAL-I02（PE-4 回归） | QSignalSpy 计 transportError：open 不存在 port + processEvents | 信号数**有界=1**；adapter not open、session Idle、no crash | **P0**（补进 adapter 测试——Part A 现测仅断言有信号，未断言有界） |
| SERIAL-I03 | 主动 disconnect 静默（hardware-free 等价：未打开 port 上 closePort() + processEvents） | 无 transportError 信号；无 crash | P1（真实已打开态无法无硬件稳定制造——文档注明限制，不造假） |

Port discovery 断言纪律：不得断言 count==2 或任何机器 COM 数量；只断言无 crash、列表可读、有条目时 portName 非空、refresh 不导致任何 open。

## SB-17 QML Smoke / CMake Link Graph / Deployment 计划

- QML smoke：Serial UI 加入后 `--qml-smoke-test` 必须继续 exit=0（无 ReferenceError/TypeError/binding loop/missing type；QSerialPort 不进 QML）。
- **CMake link graph（Implementation 后记录）**：`modbuslens`（app）真实链接 `Qt6::SerialPort`（通过 SerialPortAdapter 并接入 qml module SOURCES）；**modbuslens_core 继续 Zero Qt、零 SerialPort 链接（红线）**。
- **Deployment**：重跑 deploy_windows.bat → build/deploy/Qt6SerialPort.dll 由 windeployqt **自动**部署（不手工复制）；**必须做 provenance**：deploy 副本 SHA256 == `D:\QT\6.11.1\mingw_64\bin\Qt6SerialPort.dll`（本机存在 MSVC 第二套 D:\QTDesign——ISSUE-003 回归保护，不得混入）；minimal-PATH `--qml-smoke-test` exit=0 + 普通窗口启动（ISSUE-002 不回退）。

## SB-18 Manual Smoke 与 Hardware Smoke 政策

- **Manual Serial UI Smoke（无硬件也可 PASS）**：A deploy app 启动；B Serial Controls 正常显示；C Refresh Ports 可点无 crash（只验证列表显示，**禁止 open 任何未知 COM**）；D Simulator Demo 正常；E Replay demo_v1.mlog 正常；F 按钮 enable/disable 合理（disconnected 态）。Agent 不操作真实端口 → 用户人工执行；PASS 前不得自报。
- **Hardware Smoke 独立记录**：只有用户明确确认拥有"安全可用的 USB-RS485 + Modbus slave + COM + baud/slave/寄存器参数"才执行真实 Connect+Read Once。否则记录 `Hardware Smoke = NOT RUN, Reason = hardware unavailable`——**不伪报**；也**不为了测试随便 open probe 发现的两个 COM**。T010 软件完成不依赖 Hardware Smoke PASS。即使未来只有 adapter 无 slave，也不假定 Read 必 Timeout（echo 等行为未知）——无已知 slave 不做强预期 hardware assertion。

## SB-19 Knowledge I Must Be Able To Explain（16 题）

**SB-Q1 为什么 QML 不直接使用 QSerialPort？** ADR001 分层：Qt 硬件对象只在 App/Controller 层；QML 只面对可绑定 plain properties；也是"三模式共享 UI"的前提。
**SB-Q2 为什么 discovery 可自动、open 不可自动？** enumerate 是只读元数据；open 是独占真实设备资源，可能向真实从站发数据——必须由用户明确意图触发（安全边界）。
**SB-Q3 为什么 Connect failure 不切 source？** 与 T009 失败加载同一 atomic rule：失败的转换不得产生混合 source state。
**SB-Q4 为什么 successful Connect 清旧 batch？** Dashboard 语义="当前 active batch"；连接后旧 Demo/Replay 结果与当前来源无关，保留会造成 Header/batch 不一致的混合态。
**SB-Q5 为什么 Disconnect 保留最后结果？** Disconnect 关的是 transport 不是诊断结论；诊断事实不因"线拔了"失效。
**SB-Q6 为什么 Clear ≠ Disconnect？** 正交动作：Clear=删结果；Disconnect=关连接。合并会造成"想清屏却断设备"或反之。
**SB-Q7 为什么 Read Once 是 replace=1 非 append？** 与 Simulator/Replay 的 replace semantics 统一；history 是独立未来任务。
**SB-Q8 为什么 Timeout transaction 与 Port Open Failure 完全不同？** 前者是合法 Modbus 诊断（请求已上总线、无响应）；后者是本地 transport 事实（请求根本未发出）。混淆 = 错误归因。
**SB-Q9 为什么 Serial statistics 仍走 summarizeTransactions？** 一份统计逻辑只有一个 owner；1 元素 batch 同路径（UI-S05/06 锁定）。
**SB-Q10 为什么 0% 是合法值非 nullopt？** completed>0 时 rate 必然有值；"没有成功"≠"没有数据"；invariant C 只跟 completed 挂钩。
**SB-Q11 为什么 range validation 必须在 C++ cast 前？** int→uint8_t/uint16_t narrowing 是静默 UB 源；QML validator 可被绕过，C++ 是最后防线。
**SB-Q12 为什么不做 Auto Poll？** v1 是手动诊断工具；轮询引入并发事务/队列/UI 生命周期问题；当前"一次一事务"语义更可控（Part A Busy rule 的直接延伸）。
**SB-Q13 为什么 hardware-free seam 不算伪造？** seam 只验证 "Core result → UI 映射"（presentation adapter 正确性），输入类型由 A01~A16 锁定正确性；T008 禁令针对产品业务链冒充。
**SB-Q14 为什么不能自动打开 detected COM？** probe 发现的端口身份未知；自动 open+write 可能向未授权设备发控制信号——工业安全底线。
**SB-Q15 为什么 deploy 必须验证 Qt6SerialPort.dll provenance？** 本机曾因 MSVC/MinGW 多 kit 错位阻塞整个任务（ISSUE-003）；DLL 混入不同 ABI 工具链产物会产生 ISSUE-002 同类运行期崩溃。
**SB-Q16 三模式最后真正共享什么？** ModbusRtuFrame/Codec/analyzeFunction03Transaction/summarizeTransactions/TransactionListModel/Dashboard——来源各异的三条链路在同一模型层收敛。

## SB-20 Implementation Plan（28 步）

1. 基于真实 SerialPortAdapter API 定案 Controller wiring（adapter 成员 + 信号连接 + handler）
2. serial port list/state/error properties
3. refresh/connect/disconnect commands
4. read once command（前置 + range validation）
5. pending metadata（accept 后保存、失败/取消清除）
6. publishSerialResult seam（1 entry + summarizeTransactions replace）
7. Statistics/Model 复用 + source switching rules（SB-13 矩阵）
8. QML Serial Controls（GroupBox + enable 规则 + Reading... 状态）
9. UI-S01~S09 tests（RED）
10. SERIAL-I02（PE-4 有界回归）+ SERIAL-I03 写入 adapter 测试
11. RED 证据
12. GREEN（Controller + adapter 扩展 + QML）
13. clean build 0 warnings
14. full ctest（记录 target/test 数）
15. qml smoke
16. Core Zero Qt（core 零 SerialPort）
17. app 真实链接 Qt6::SerialPort（link graph 记录）
18. deploy_windows.bat 重跑
19. Qt6SerialPort.dll provenance（SHA256 vs D:\QT MinGW bin）
20. minimal-PATH smoke
21. Manual Serial UI Smoke → WAITING FOR USER（A~F）
22. Hardware Smoke（SB-18 政策）
23. docs 归档（T010 档案 Implementation 章）
24. code/config commit（用户 Manual Smoke 前为 LKGC candidate）
25. 用户确认后 LKGC 正式推进
26. docs-only hash backfill
27. 不开始 T011
28. 不 git push

## SB-21 PROJECT_STATUS（本阶段执行）

Current Task=T010；Current Part=Part B — Serial UI Integration + Hardware/No-Hardware Smoke；Current Phase=Learning / Test Design；Next Action=T010 Part B — Implementation；Next Task After=T011；T010 overall=IN PROGRESS；M5=IN PROGRESS（T009 ✅ / T010 Part A ✅ / Part B ⬜）。

## SB-22 docs-only 验证（本阶段执行）

`git diff --check`；`src/tests/CMakeLists.txt/scripts` 零修改；docs-only commit；LKGC 保持 `b31233b`；不得 push。# T010 Part B Implementation 归档章（追加内容）

---

# Part B — Implementation（2026-09-08）

## Implementation

- **Adapter API 演进（transport/transaction 拆分）**：`openPort(portName, qint32 baudRate)`（只开/配 8N1；PE-4 有界 error 规则保留——failed open 恰 1 个 user-visible transportError）；`startTransaction(slave, start, qty, timeout)`（要求 port 已开；未开/繁忙/write 失败拒绝，不造 TransactionStatus；write 失败关 port，Controller 事后从 isPortOpen 同步）；`closePort()`（主动断开静默：cancel+close，无 transportError）。旧合体 API 无外部稳定消费者，直接演进，adapter tests 同步重写（不保留重复接口）。
- **AnalysisController 接管唯一 SerialPortAdapter**（成员持有；QML 不可见；应用层再无第二个 QSerialPort）：ctor 连接 transactionCompleted → handleSerialTransactionCompleted、transportError → handleSerialTransportError。
  - `refreshSerialPorts()`：QSerialPortInfo::availablePorts 只 enumerate，绝无 open/write/probe；空列表不是 error。
  - `connectSerial(portName, baud)`：空名/baud 白名单（5 档）先行校验 → adapter.openPort → **成功才**原子切换（清旧 batch、mode=Serial/sourceLabel="<port> @ <baud>"、清 serial error）；失败=旧 statistics/rows/mode/source 全保留（handleSerialTransportError 只动 serial error/connected）。
  - `disconnectSerial()`：teardownSerialTransport（adapter closePort 静默 + connected/busy=false + pending metadata 清空）；保留最后结果与 Serial source 身份（Clear≠Disconnect）。
  - `readHoldingRegistersOnce`：**cast 前 range 校验**（slave 1~247、start 0~65535、qty 1~125、timeout>0）→ 前置 connected&&!busy → adapter.startTransaction 接受后才 `pendingSerialAddress_` 保存 + serialBusy=true + 清旧 error；等待期旧结果保留（Reading...）。
  - `publishSerialResult(sourceLabel, deviceAddress, analysis)`（非 Q_INVOKABLE seam）：1 entry + summarizeTransactions({analysis}) replace；mode=Serial/source=入参；serialBusy=false；清 serial error。
  - `handleSerialTransactionCompleted`：**stale guard**——无 pending metadata 直接丢弃（UI-S10），有则转 publishSerialResult 后清 metadata。
  - `handleSerialTransportError`：busy=false、pending 清空、connected=adapter.isPortOpen()、setSerialError("Serial transport error: ...")——statistics/rows/mode/source 一律不动。
  - Source switching（SB-13）：runDemoBatch 开头 teardownSerialTransport（Simulator Mode 无后台 COM）+ 成功后清 serial error；loadReplayFile 成功路径**先 teardown 再发布**（Replay 失败时 Serial 原样保留）；clearResults 追加清 serial error（不断连、不切来源）。
- **QML Serial Controls**（GroupBox，无新页面/第二套 Dashboard）：Port ComboBox（model=serialPortNames）+ Refresh / Baud ComboBox（5 档固定）+ 静态 "8N1" / Connect（!connected && 有选中 port）/ Disconnect（connected）/ Slave·Start·Quantity·Timeout SpinBox / Read Holding Registers Once（connected && !busy；busy 显示 "Reading..."）；独立 Serial error label（hasSerialError 可见，Transport error 与 Transaction row 视觉可分）。QML 不 import QtSerialPort。
- **CMake link graph**：modbuslens(exe) 与 modbuslens_ui_bridge_tests 编译 SerialPortAdapter.cpp 并链接 `Qt6::SerialPort`（仅此两 target）；modbuslens_core 仍 Zero Qt。

## Files Changed

- 修改：`src/ui/serial/SerialPortAdapter.{h,cpp}`（API 演进）、`src/ui/AnalysisController.{h,cpp}`（serial 全套）、`src/ui/qml/Main.qml`（Serial Controls）、`tests/test_serial_adapter.cpp`（I01~I05 重写）、`tests/test_ui_bridge.cpp`（+UI-S01~S10）、`CMakeLists.txt`（link graph）

## Verification

```text
RED：old startTransaction 定义与头文件失配编译错误 + 12 个 Controller undefined reference
GREEN：cmake --build exit=0
serial 21/21；serial_adapter 5/5（I01 invalid open / I02 有界恰1 / I03 主动关闭静默 /
       I04 fresh idle / I05 未开口 start 拒绝）；ui_bridge 32/32（+UI-S01~S10）
ctest --preset debug-local：100% tests passed, 0 tests failed out of 18
clean build（--clean-first）：108 targets，编译 warning/error 0
qml smoke（真实 exe，含 Serial UI）：--qml-smoke-test exit=0，无 ReferenceError/TypeError/binding loop
Core Zero Qt：src/core 无 Qt include
link graph：Qt6::SerialPort 仅 modbuslens 与 ui_bridge_tests 两 target；core 零 Qt
deploy_windows.bat：exit=0；Qt6SerialPort.dll 由 windeployqt 自动部署
provenance：SHA256(build/deploy/Qt6SerialPort.dll)=7c070c3e…==SHA256(D:\QT\6.11.1\mingw_64\bin\Qt6SerialPort.dll)；
           本机 D:\QTDesign MSVC 目录已不存在（按实际环境记录）；ISSUE-003 无回归
minimal-PATH：set PATH=System32 下 ModbusLens.exe --qml-smoke-test exit=0；ISSUE-002 无回归
```

## Result

- **Manual Serial UI Smoke = PASS**（用户人工 A~F：Serial Controls 显示 / Refresh 无 crash 且不自动开任何 COM / Port 列表枚举正常且无数量假设 / Simulator Demo 回归 4-4-0 全对 / Replay golden 回归正常 / disconnected 态按钮 enable 合理）。
- **Hardware Smoke = NOT RUN，Reason = hardware unavailable**（无用户确认的安全 Modbus RS485 硬件参数；按 SB-18 政策不伪报；期间未 open 任何未知 COM）。
- **T010 Part B = DONE；T010 overall = DONE。M5 按 BACKLOG 既有定义（T009+T010）CLOSED。**

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| Part B Implementation（code/config） | `33ed197` | `T010(Part B): integrate serial controls into the analysis dashboard`（**新 LKGC**） |
| 用户确认归档（docs-only） | `<docs-only HEAD>` | Manual Serial UI Smoke PASS 回填 |

> LKGC = `33ed197`；下一任务 T011（未开始）。