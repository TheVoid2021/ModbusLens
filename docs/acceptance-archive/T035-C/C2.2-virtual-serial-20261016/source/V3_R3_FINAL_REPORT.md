# V3_R3_FINAL_REPORT.md — T035-C Round C2.2-V3-R3（VSPD 11 Recovery + V2/V3 Acceptance）

日期：2026-10-17 16:14
结论：**C2.2-V3 = PASS（8/8, EXIT=0）· C2.2-V2 复验 = PASS（4/4）· V4 = READY FOR SEPARATE HUMAN AUTHORIZATION**

## A. VSPD 11 新环境基线（Agent 本轮实测，非 Human 证据转述）

- **版本**：Virtual Serial Port Driver **11.0.1096**（Electronic Team, Inc.），`D:\Virtual Serial Port Driver 11\` 唯一安装（v10 已不存在）
- **GUI**：`vspdpro.exe` ver=11.0.1096，签名 **Valid**
- **内核驱动**（逐文件核验）：`evserial9.sys` **10.6.960.0 Valid** · `evsbc9.sys` **10.6.983.0 Valid** · `sershare.sys` 10.0.191.0 Valid —— Signer 全部 = Microsoft Windows Hardware Compatibility Publisher
- **驱动服务**：evserial9 **Running ExitCode=0** · VSBC9 **Running ExitCode=0** · sershare Stopped(1077 按需)
- **端口设备**：`(COM7->COM8)` / `(COM8->COM7)` **Status=OK, CM_PROB_NONE**
- **SERIALCOMM**：`\Device\VSerial9_0 = COM7` · `\Device\VSerial9_1 = COM8`（+ 蓝牙 COM3/COM4）
- **GetPortNames**：COM3 / COM4 / COM7 / COM8
- **打开测试**：COM7 **OPEN OK** · COM8 **OPEN OK**
- **问题设备**：全系统 **(none)**（无 Code 52/577）
- **蓝牙 COM3/COM4**：保持正常（BTHENUM，未变）
- 完整快照：V3_R3_VSPD11_BASELINE.md

## B. COM7/COM8 Pair 状态

同属 VSBC9 bus（ROOT\SYSTEM\0004）——成对 null-modem pair；设备实例 `VSBC9\DEVICES\0000/0001`；两端均 OK。**无版本混杂、无签名异常、无未预期环境变化** ⇒ §2 基线核验 PASS。

## C. V2 A/B/C/D 本轮结果（**4/4 PASS，EXIT=0**）

日志：V3_R2_V2_RETEST.log（纯 UTF-8）

| 测试 | 方向 | TX | RX | 结果 |
|---|---|---|---|---|
| A | COM7→COM8 | `01 03 00 31 00 01 D5 C5` | 同（8/8 EXACT） | **PASS** |
| B | COM8→COM7 | `01 03 02 04 D2 3A D9` | 同（7/7 EXACT） | **PASS** |
| C | COM8→COM7 分段 3 段 | `01 03` \| `02 04 D2` \| `3A D9` | 累计同（7/7 EXACT） | **PASS** |
| D | 关闭→重开→roundtrip | `55 AA` | `55 AA` | **PASS** |

## D. S0–S7 本轮结果（**8/8 PASS，MATRIX_EXIT=0**）

完整日志：V3_R3_FULL_MATRIX.log（矩阵汇总）+ 各场景原始日志 `slave_log_*.log` / `probe_log_*.log`（权威原始证据，UTF-8）

| 场景 | 结果 | 关键证据（本轮实测） |
|---|---|---|
| S0 CRC_SELFTEST | **PASS** | 请求向量→D5 C5；响应向量→3A D9（Low/High 顺序）；合法帧接受；单比特损坏拒绝 |
| S1 NORMAL | **PASS** | REQ `01 03 00 31 00 01 D5 C5`（COM8 收到，slave=1 fc=3 addr=49 qty=1 CRC=VALID）→ TX `01 03 02 04 D2 3A D9` → COM7 RX 同（7/7） |
| S2 CRC_ERROR | **PASS** | TX `01 03 02 04 D2 3A 26`（仅 CRC 损坏：D9→26）→ RX 同 → 断言「INVALID CRC, only CRC damaged」 |
| S3 TIMEOUT | **PASS** | REQUEST_RECEIVED → NO RESPONSE → COM7 RX `(none) len=0`（waited 3014ms） |
| S4 WRONG_SLAVE | **PASS** | TX `02 03 02 04 D2 7E D9`（slave=2，CRC 正确）→ RX 同 |
| S5 WRONG_FUNCTION | **PASS** | TX `01 04 02 04 D2 3B AD`（fc=4，CRC 正确）→ RX 同 |
| S6 EXCEPTION | **PASS** | TX `01 83 02 C0 F1`（合法异常帧，CRC 正确）→ RX 同（len=5） |
| S7 FRAGMENTED | **PASS** | TX 3 段（120ms 间隔）`01 03` \| `02 04 D2` \| `3A D9` → COM7 累计 7/7 EXACT |

- **未复用任何历史 PASS**：以上全部由本轮 16:12–16:13 运行产生。
- **未跳过 S2/S7**；断言未降低；无 mock/replay 替代真实串口收发。

## E. S2 修复验证

本轮 S2 通过，模拟器日志显示 TX `01 03 02 04 D2 3A 26` —— 若 CRC 字段未被正确构造为 byte[]，SerialPort.Write 会因重载解析失败而完全不发送（上一轮 FAIL 形态）；本轮实际发出且仅 CRC 被改 ⇒ **显式 `[byte[]]` 修复生效**。

## F. S7 修复验证

本轮 S7 通过（probe 累计 7/7 EXACT），且矩阵端口释放检查（5×400ms 有界重试）在所有 7 个场景均通过 ⇒ **S7 修复（有界重试）生效且未软化断言**（失败仍会 FAIL）。

## G. 测试前后环境对比

- 测试前（16:11）：COM7/COM8 OK、驱动 Running、0 问题设备
- 测试后（16:14）：**完全一致**（GetPortNames 4 端口、PnP 全 OK、驱动 Running、0 问题设备、无残留进程、最小双向校验 PASS）
- 详见 V3_R3_VSPD11_BASELINE.md 与 V3_R3_POST_ENVIRONMENT.md

## H. 原始 RTU TX/RX

见 §D 表格与 `slave_log_*.log` / `probe_log_*.log`（每场景独立原始日志）。全部经真实 COM7↔COM8 双向字节通路；**未启动 ModbusLens**。

## I. 端口释放和进程检查

每场景：READY 握手 → 单次请求 → 从站有界退出（MaxRequests=1 / IdleExit=8s）→ 精确 PID 清理 → 端口释放验证（5× 重试）**全部通过**；测试后无 `virtual_modbus/probe_virtual/run_v3_matrix` 残留进程；COM7/COM8 可再次打开。

## J. Git / EXE 安全验证

HEAD `3adaea4282a2282a3d340f3811b433d334031800` ✓ · staged=0 ✓ · `git diff --check` PASS ✓ · 冻结 Release `4044edae6933ed98acd6424483399dbcdd5185b26840ebfd6dc5008f14a93fe5` **MATCH** ✓ · C2.1 Debug EXE `9cadc2c53d80e7b74e9a4bf65dd042068d58b78a2e4e128f7b573c9445876915` **MATCH** ✓ · 产品源码零修改 · 未安装/卸载/切换 VSPD · 未修改 Windows 安全设置 · 无真实设备请求 · 无 commit/push/tag/package/LKGC。

## K. 未验证事项

- **V4 生产串口 E2E 未执行**（本轮禁止）——ModbusLens GUI + 生产 SerialPortAdapter 对虚拟从站的端到端仍待下一轮明确授权。
- **真实硬件（RS485 物理层/真实设备）未验证**。
- 日志说明：矩阵汇总日志曾因 PowerShell `Tee-Object` 混编（UTF-8 BOM + UTF-16）；**权威原始证据以每场景独立 `slave_log_*.log` / `probe_log_*.log` 为准**，内容完整。

## L. V4 准备状态

**READY FOR SEPARATE HUMAN AUTHORIZATION**。前提条件全部满足：COM7/COM8 稳定、V2 4/4、V3 8/8、测试后环境一致、安全/指纹全通过。

---

## 状态

**C2.2-V2 REVERIFICATION ON VSPD11 = PASS（4/4）**
**C2.2-V3 EXTERNAL RTU SLAVE SIMULATOR = PASS（8/8, EXIT_CODE=0, FAILURES=0）**
**C2.2-V4 = READY FOR SEPARATE HUMAN AUTHORIZATION**
**C2 REAL HARDWARE = PENDING**
**T035-C OVERALL = NOT YET CLOSED**

本轮未启动 ModbusLens，未进入 V4。**STOP — RETURN TO HUMAN / CHATGPT REVIEW。**
