# V4_R1_FINAL_REPORT.md — T035-C Round C2.2-V4-R1（ModbusLens Production QSerialPort E2E）

日期：2026-10-17 16:44
结论：**C2.2-V4 = BLOCKED — REQUEST HUMAN TEST PROFILE PREPARATION**（§6.1 前置条件不满足）

## 摘要

本轮已完成全部不依赖测试 Profile 的只读阶段（Preflight、R3 证据校验、VSPD 11 环境独立核验、生产路径源码审计、外部从站静态预检）。**§6.1 Device Profile 核验发现：现有 3 个已保存档案均不符合 V4 契约**（无 FC03 @ 49 / Linear / scale=0.01 / offset=0 / unit=s 的条目），按 §6.1 明确条文 **STOP，不得继续任何实际收发测试**。未启动 ModbusLens、未启动外部从站、未发送任何请求、未伪造任何 TX/RX。

---

## A. Git / EXE 冻结指纹

| 项 | 值 | 判定 |
|---|---|---|
| HEAD | `3adaea4282a2282a3d340f3811b433d334031800` | **MATCH** |
| staged | 0 | ✅ |
| `git diff --check` | PASS | ✅ |
| 冻结 Release SHA256 | `4044edae6933ed98acd6424483399dbcdd5185b26840ebfd6dc5008f14a93fe5` | **MATCH** |
| C2.1 Debug SHA256（本轮验收对象） | `9cadc2c53d80e7b74e9a4bf65dd042068d58b78a2e4e128f7b573c9445876915`（83,084,566 B） | **MATCH** |
| 验收程序路径与启动方式 | `build/debug-h2/modbuslens.exe`（隔离启动器 `start_modbuslens_debug.ps1` 仅设 PATH/工作目录，无测试参数） | 已确认，未重编译/未更换 |

## B. VSPD 11 环境基线（**Agent 本轮独立实测**）

VSPD **11.0.1096** 唯一安装（`D:\Virtual Serial Port Driver 11\`）· GUI 签名 **Valid** · 驱动 `evserial9 10.6.960.0` / `evsbc9 10.6.983.0` / `sershare 10.0.191.0` 全 **Valid**（WHQL）· **evserial9 Running(0) · VSBC9 Running(0)** · 全系统 **0 问题设备** · 蓝牙 COM3/COM4 归属未变。详见 V4_R1_PRE_ENVIRONMENT.md。

## C. COM7/COM8 Pair 与端口归属

- PnP：`Electronic Team Virtual Serial Port (COM7->COM8)` = `VSBC9\DEVICES\0000` **OK/CM_PROB_NONE**；`(COM8->COM7)` = `VSBC9\DEVICES\0001` **OK**
- SERIALCOMM：`\Device\VSerial9_0 = COM7`、`\Device\VSerial9_1 = COM8`
- GetPortNames：COM3 / COM4 / COM7 / COM8
- **独占 open+release 测试（无从站、无 ModbusLens）**：COM7 OK、COM8 OK
- 归属约定（V4 期间）：**COM7 → ModbusLens 生产 QSerialPort · COM8 → 外部从站**；未运行 V3 probe/matrix

## D. 生产串口调用链源码审计（独立重读，PASS）

`EngineeringPage.qml:158/165/441`（requestSingleRead → readController.readOnce → 按钮 onClicked）→ `EngineeringReadController.cpp:142-157`（三重门禁 + savedRegisterReadiness）→ `analysis_->readHoldingRegistersOnce` → `AnalysisController.cpp:165 serialTransport_ = &serialAdapter_` + `:1430 startActiveRequest` → `SerialPortAdapter.cpp:136-139`（QSerialPort 8N1/NoFlowControl）+ `:38 QSerialPortInfo` → `:217 beginActiveRequest` / `:340 feedResponseBytes`（CRC/RTU 校验）→ `captureReadResultFromRecord` → UI。禁用项核验：Main.qml/EngineeringPage.qml/EngineeringReadController.h 中 fake transport 引用 = 0；`AnalysisController.h` 仅 1 处 `setSerialTransport` **seam 声明**（正常启动不调用）。详见 V4_R1_PRODUCTION_PATH_AUDIT.md。

## E. 正式 GUI 启动方式及运行模式

- 验收对象 = 冻结 C2.1 Debug EXE（正常 GUI 模式）
- **本轮未启动**（BLOCKED 于 §6.1）；下一轮将直接/经隔离启动器启动，**不加任何测试参数**

## F. Device Profile 与 EngineeringReady（**阻塞点**）

**现有已保存档案（`%APPDATA%\ModbusLens\ModbusLens\profiles\`，只读核验）**：

| 档案 | 寄存器摘要 | 是否符合 V4 契约 |
|---|---|---|
| `222222` | FC04 @ 5 · UInt16 · 0.0001 V · kind=None | ❌ |
| `重金属比色传感器` | FC04 @ 0（linear 0.01 V）、FC04 @ 4（linear 0.1 A）、@1-3/5+ unknown | ❌（无 FC03@49） |
| `1111` | FC03 @ 1000（0.1 Hz）、FC04 @ 1000（Int16 °C）、FC65 @ 1001 | ❌（地址/类型不符） |

**结论：不存在 FC03 / slave=1 / address=49 / qty=1 / Linear / scale=0.01 / offset=0 / unit=s 的已保存档案**。按 §6.1：
> 如果没有符合要求的测试 Profile：**STOP — REQUEST HUMAN TEST PROFILE PREPARATION。**
> 不得擅自修改现有设备档案来凑测试结果。

⇒ 未创建、未修改任何 Profile；未进入 GUI 启动与读取。**EngineeringReady 未评估**（无目标档案）。

## G/H. 正常 FC03 E2E 请求/响应原始证据

**NOT EXECUTED**（见 V4_R1_NORMAL_TXRX.log）。无伪造 TX/RX。

## I. 原始值 1234 与工程值 12.34 s

**NOT VERIFIED**（依赖 G/H）。

## J. E2–E7 异常矩阵

**NOT EXECUTED**（依赖 E1 正常场景先行；见 V4_R1_ERROR_MATRIX.log）。

## K. 端口、进程、超时及清理

本轮未启动任何从站/ModbusLens 进程；无端口占用；无残留进程需清理；COM7/COM8 保持空闲可开。

## L. Human GUI 观察与截图

**NOT EXECUTED**。`AUTOMATED / AGENT EVIDENCE = NOT STARTED`；**未声明 `HUMAN GUI ACCEPTED`**。Human 操作检查表已就绪：V4_R1_HUMAN_GUI_CHECKLIST.md。

## M. 测试前后环境稳定性

未运行测试 ⇒ 前后一致（见 V4_R1_PRE_ENVIRONMENT.md 与 V4_R1_POST_ENVIRONMENT.md）。无需恢复动作。

## N. 已发现问题与证据局限

1. **唯一阻塞项**：缺符合契约的测试 Device Profile（Human 侧准备）。
2. 证据局限：`V3_R2_V2_RETEST.log` 文件名沿用 R2 占位名（内容为 R3 实测，已由时间戳与内容确认，R3 报告 §C 已说明；本文件不改写）。
3. 本轮未产生任何生产收发证据——**不得以 R3 的 V2/V3 结果代替 V4 生产应用证据**。

## O. 未验证事项

- ModbusLens 生产 QSerialPort 真实收发（E1–E7 全部）——BLOCKED。
- GUI 大号数值 12.34 s 投影——NOT VERIFIED。
- Human GUI 验收——NOT EXECUTED。
- 真实硬件（RS485 物理层/真实设备）——PENDING（不属本轮）。

## P. T035-C 当前关闭条件及后续授权需求

关闭 C2.2-V4 需要（按序）：
1. **Human 准备符合契约的测试 Profile**（见检查表第 0 步）并告知 Agent；
2. Agent 启动外部从站（COM8）+ 指导 Human 启动 ModbusLens（COM7）完成 E1；
3. Human 逐项完成 E2–E7（Agent 切换从站模式）；
4. Agent 完成测试后稳定性检查与证据归档；
5. Human GUI 验收确认（截图 + 状态文字）。

---

## 状态（严格区分）

| 状态项 | 本轮判定 |
|---|---|
| TEST EXECUTED | **NO**（§6.1 前置未满足，未运行任何 E2E） |
| TEST PASSED | **N/A** |
| AGENT REVIEWED | **PARTIAL**（仅只读阶段 A–D 完成） |
| HUMAN ACCEPTED | **NO** |
| REAL HARDWARE VERIFIED | **NO（PENDING）** |

**C2.2-V4 = BLOCKED（REQUEST HUMAN TEST PROFILE PREPARATION）**
**C2.2 VIRTUAL SERIAL ACCEPTANCE = PARTIAL / PENDING**
**C2 REAL HARDWARE = PENDING**
**T035-C OVERALL = NOT YET CLOSED**

本轮未启动 ModbusLens、未发送任何 RTU 请求、未修改产品源码/Profile/VSPD/系统设置、无 commit/push/tag/package/LKGC。

**STOP — RETURN TO HUMAN / CHATGPT REVIEW。**
