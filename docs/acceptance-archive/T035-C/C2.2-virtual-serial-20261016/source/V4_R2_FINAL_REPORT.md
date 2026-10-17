# V4_R2_FINAL_REPORT.md — T035-C Round C2.2-V4-R2（Test Profile Verification + Production E2E）

日期：2026-10-17 17:12
结论：**C2.2-V4 PRODUCTION QSERIALPORT E2E = PASS（E1–E7 全 PASS）**
**C2.2 VIRTUAL SERIAL ACCEPTANCE = PASS**
**C2 REAL HARDWARE = PENDING** · **T035-C OVERALL = PENDING FINAL HUMAN REVIEW**

---

## A. Git / EXE 冻结指纹

| 项 | 值 | 判定 |
|---|---|---|
| HEAD | `3adaea4282a2282a3d340f3811b433d334031800` | **MATCH** |
| staged | 0 | ✅ |
| `git diff --check` | PASS | ✅ |
| 冻结 Release SHA256 | `4044edae6933ed98acd6424483399dbcdd5185b26840ebfd6dc5008f14a93fe5` | **MATCH** |
| C2.1 Debug SHA256（验收对象） | `9cadc2c53d80e7b74e9a4bf65dd042068d58b78a2e4e128f7b573c9445876915` | **MATCH**（83,084,566 B） |

## B. VSPD 11 环境基线

VSPD **11.0.1096** 唯一安装 · GUI 签名 Valid · 驱动 evserial9 10.6.960.0 / evsbc9 10.6.983.0 / sershare 10.0.191.0 全 **Valid**（WHQL）· evserial9 + VSBC9 **Running(0)** · 全系统 **0 问题设备**。详见 `V4_R1_PRE_ENVIRONMENT.md`。

## C. COM7/COM8 Pair 与端口归属

- PnP：`(COM7->COM8)`=`VSBC9\DEVICES\0000` · `(COM8->COM7)`=`VSBC9\DEVICES\0001`，均 **OK / CM_PROB_NONE**
- SERIALCOMM：`VSerial9_0=COM7` · `VSerial9_1=COM8`
- 归属：**COM7 → ModbusLens（生产 QSerialPort）· COM8 → 外部 RTU 从站**；矩阵期间未运行 V3 probe/matrix

## D. 生产串口调用链源码审计（PASS）

`EngineeringPage.qml:158/165/441` → `EngineeringReadController`（三重门禁，`savedRegisterReadiness`@:155）→ `AnalysisController`（`serialTransport_ = &serialAdapter_`@:165；`startActiveRequest`@:1430）→ `SerialPortAdapter`（QSerialPort 8N1/NoFlowControl@:136-139；`QSerialPortInfo`@:38）→ `session_.beginActiveRequest`@:217 / `feedResponseBytes`@:340（CRC/RTU 校验）→ `captureReadResultFromRecord` → UI。禁用项：Main.qml / EngineeringPage.qml / EngineeringReadController.h 中 fake transport 引用 = **0**（`AnalysisController.h` 仅 seam 声明，正常启动不调用）。详见 `V4_R1_PRODUCTION_PATH_AUDIT.md`。

## E. 正式 GUI 启动方式及运行模式（**重点核验项**）

- 启动：`start_modbuslens_debug.ps1` → `build/debug-h2/modbuslens.exe`（**无任何测试参数**）
- **运行模式核实（源码 + 实测）**：
  - 「模拟器模式 · 确定性演示」= **未连接串口时的默认数据源**（源码 `AnalysisController.cpp:3921-3922`，`loadSimulator`）
  - **连接 COM7 成功后**：`modeLabel_ = "串口模式"` + `sourceLabel_ = "COM7 @ 9600"`（`:1311-1312`），`sourceKind_ = ActiveSerial`（`:1307`）
  - **Human 实测确认顶部由「模拟器模式」变为「串口模式 / COM7 @ 9600」**——**该截图不作为 E2E 证据，真正证据见 G/H**
- 无 Simulation/Replay/Harness 注入参与读取

## F. Device Profile 与 EngineeringReady

- 档案：**`V4-E2E-Test`**（profileId `88412dd8-a3ed-4c40-bb47-84d0bf98c9f7`，2026-10-17 16:53 保存）
- 只读 JSON 核验：`readFunctionCode=3` · `address=49` · `quantity=1` · `dataType=UInt16` · **`decoderKind=linear`** · `scale=0.01` · `offset=0` · `unit=s` · **`scaleConfigured/offsetConfigured/unitConfigured = True/True/True`** · 名称「测试时间间隔」 ⇒ **完全符合契约**
- Human GUI：Readable = PASS · **EngineeringReady = PASS** · Slave ID = 1 · Timeout = 1000 ms

## G. 正常 FC03 E2E 请求原始证据（**生产 QSerialPort 实发**）

```
COM8 SIMULATOR RX: 01 03 00 31 00 01 D5 C5   (accumulated 8)
COM8 SIMULATOR REQUEST: slave=1 fc=3 addr=49 qty=1 bytes=01 03 00 31 00 01 D5 C5 CRC=VALID
```
- 该 8 字节由 **ModbusLens 生产路径（EngineeringPage → readOnce → readHoldingRegistersOnce → QSerialPort → COM7 → VSPD → COM8）** 发出，**未注入任何测试 transport**
- 共发生 **2 次**成功事务（`FINISH_TRANSACTION #1/#2`）

## H. 正常 FC03 E2E 响应原始证据

```
COM8 SIMULATOR TX: 01 03 02 04 D2 3A D9 (len=7, CRC recomputed dynamically)
```
- Byte Count = 02 · Register = **0x04D2 = 1234** · CRC = **VALID**（动态计算）
- 经 COM8 → VSPD → COM7 由 **ModbusLens 生产接收链** 接收并解析

## I. 原始值 1234 与工程值 12.34 s

- Raw = **1234**（0x04D2，从响应字节解出）
- Engineering = 1234 × 0.01 + 0 = **12.34**
- **Human UI 确认：大号数值 12.34 · 单位 s**（两次事务均一致）

## J. E2–E7 异常矩阵逐项结果（全部 PASS，均为新的真实串口事务）

| 场景 | 从站写出 | 关键断言 | Human UI | 结果 |
|---|---|---|---|---|
| E2 CRC_ERROR | `01 03 02 04 D2 3A 26` | 期望 CRC=3A D9，帧内 3A 26 ⇒ **INVALID as designed** | 失败/CRC 错误 | **PASS** |
| E3 TIMEOUT | （无响应，bounded silence） | 请求到达 2 次，0 响应字节 | 超时 | **PASS** |
| E4 WRONG_SLAVE | `02 03 02 04 D2 7E D9` | CRC 验证 7E D9 **VALID**；slave=2≠1 | 失败/拒绝 | **PASS** |
| E5 WRONG_FUNCTION | `01 04 02 04 D2 3B AD` | CRC 验证 3B AD **VALID**；fc=4≠3 | 失败/拒绝 | **PASS** |
| E6 EXCEPTION | `01 83 02 C0 F1` | CRC 验证 C0 F1 **VALID**；合法异常帧 | 显示从站异常 | **PASS** |
| E7 FRAGMENTED | `01 03` \| `02 04 D2` \| `3A D9`（3 段/120ms） | 累计解析成功 | **12.34 s** | **PASS（扩展压力测试）** |

**E7 时序声明**：120 ms 段间延迟显著超出标准 RTU 帧内间隔（9600 baud 下 1.5 字符 ≈ 1.56 ms）⇒ 仅证明「分段写入可被累积解析」，**不构成标准 RTU 帧边界合规结论**。

详见 `V4_R1_ERROR_MATRIX.log` + 各 `V4_R2_E<n>_*_slave.log`。

## K. 端口、进程、超时及清理

- 从站：每场景 `MaxRequests=3` 后 bounded exit；FRAGMENTED 于 17:11:03 释放 COM8；无遗留
- ModbusLens：Human 已断开串口；COM7 可独占打开
- 清理：Agent 仅对**精确 PID**（34116）执行过一次 Stop-Process；未按名批量结束任何进程
- 超时：E3 中 ModbusLens 在自身 1000 ms 边界结束事务，未无限等待

## L. Human GUI 观察与截图

- Human 逐轮提供界面反馈（E1 12.34 s / E2 失败 / E3 超时 / E4 失败 / E5 失败 / E6 异常 / E7 12.34 s）
- 记录见 `V4_R1_GUI_EVIDENCE.md`；**截图由 Human 侧保存**（Agent 未读取图片）
- **`HUMAN GUI ACCEPTED` 的最终裁定权属于 Human**；本报告只声明「Human GUI 观察已记录」

## M. 测试前后环境稳定性

| 项 | 测试前 | 测试后 |
|---|---|---|
| GetPortNames | COM3/4/7/8 | **COM3/4/7/8** |
| PnP COM7/COM8 | OK | **OK（CM_PROB_NONE）** |
| evserial9 / VSBC9 | Running(0) | **Running(0)** |
| SERIALCOMM | VSerial9_0/1 | **未变** |
| 问题设备 | none | **none** |
| 最小双向校验 | （V2 4/4） | **`5A A5 7E` 3/3 EXACT PASS** |

详见 `V4_R1_POST_ENVIRONMENT.md`。

## N. 已发现问题与证据局限

1. **一次未定论观察**：「工程值」页首次读取曾报「响应超时」，随后同会话两次成功。成因未定论（可能为从站启动初期调度/时序），**未掩盖、未重跑掩盖**。
2. **「通信」页读取与「工程值」页读取的区分**：早期 Human 在「通信」页点击发出 `01 03 00 00 00 02 C4 0B`（addr 0/qty 2），从站按契约拒绝——**这同时证明生产 TX 通路存在**，但不属 V4 目标路径。
3. **残留 GUI 进程**：`modbuslens` pid 33524（17:06:26）在 Human 报「已关闭」后仍存在（未持串口）——待 Human 确认。
4. 证据局限：日志为从站侧 UTF-8 原始文件（权威）；矩阵汇总日志历史混编问题已在 R3 说明，本轮不依赖汇总日志。

## O. 未验证事项

- **真实硬件（RS485 物理层 / 真实设备行为）**：**PENDING**（不属本轮）
- **标准 RTU 帧边界时序**（E7 为扩展压力测试，非标准时序）
- 连续轮询、多寄存器、写入类功能（本轮明确禁止）

## P. T035-C 关闭条件与后续授权需求

- C2.2-V4 已 PASS；**C2.2 虚拟串口验收 = PASS**
- T035-C 整体关闭仍需：Human 最终 GUI 验收裁定（Human GUI ACCEPTED）+ 后续里程碑授权；**真实硬件验收另行安排**
- 不得由 Agent 自行宣布 T035-C 关闭

---

## 状态（严格区分）

| 状态项 | 判定 |
|---|---|
| TEST EXECUTED | **YES**（E1–E7 全部执行） |
| TEST PASSED | **YES**（E1–E7 = 7/7 PASS） |
| AGENT REVIEWED | **YES**（证据链完整：源码审计 + 从站原始日志 + CRC 独立验证） |
| HUMAN GUI ACCEPTED | **PENDING HUMAN 最终裁定**（Human 已逐项观察并反馈） |
| REAL HARDWARE VERIFIED | **NO（PENDING）** |

**C2.2-V4 PRODUCTION QSERIALPORT E2E = PASS**
**C2.2 VIRTUAL SERIAL ACCEPTANCE = PASS**
**C2 REAL HARDWARE = PENDING**
**T035-C OVERALL = PENDING FINAL HUMAN REVIEW**

本轮未修改产品源码 / 现有 Profile / VSPD / 系统设置；未使用 fake transport / replay / 内部测试入口；无 commit/push/tag/package/LKGC。

**STOP — RETURN TO HUMAN / CHATGPT REVIEW。**


---

## R3 附录 — Human 最终验收补充（2026-10-17，来源：Human 最终口头确认 + 只读核验）

> 本附录为**追加**记录。上文（R2 报告主体）保持 2026-10-17 17:12 当时的原始状态与时间线不变，
> 其中 HUMAN GUI ACCEPTED 的 PENDING 表述**不追改为当时已 PASS**。

**Human 最终确认（2026-10-17，ChatGPT 审核轮）**：

1. **H1 GUI 进程正常退出**：Human 执行
   Get-CimInstance Win32_Process -Filter "Name='modbuslens.exe'" | Select-Object ProcessId, CreationDate, ExecutablePath
   回报**无输出** ⇒ 该次查询时未发现 modbuslens.exe 进程；R2 观察到的 PID 33524 不再构成未关闭进程阻塞。
   （此为 Human 提供的事后检查结果，非 Agent 本轮独立实测；进程退出时间未记录。）
2. **H2 异常响应安全显示**：Human 明确确认 E2–E6 出错时 ModbusLens 显示失败/超时/异常，
   **未将之前的 12.34 s 继续标记为本次读取成功** ⇒ HUMAN GUI ERROR-STATE ACCEPTANCE = PASS。
   （属 Human GUI 行为确认，不扩展为内部状态机测试结论。）

**裁定（本轮）**：C2.2-V4 = **HUMAN ACCEPTED / PASS（7/7）**；C2.2 Virtual Serial Acceptance = **HUMAN ACCEPTED / CLOSED**。

**边界重申**：PRODUCTION QSERIALPORT VIRTUAL E2E = VERIFIED ON VSPD 11（仅 Windows 虚拟 COM 下的生产软件串口链路）；
**不代表**真实 USB-RS485 转换器 / RS485 物理电气层 / 现场总线 / 真实 Modbus 设备已验证（C2 Real Hardware = PENDING）。
