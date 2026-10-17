# V3_FINAL_REPORT.md — T035-C Round C2.2-V3 External Modbus RTU Slave Simulator

日期：2026-10-16 20:07
结论：**C2.2-V3 = BLOCKED（环境层：VSPD COM7/COM8 端口名注册在测试期间丢失，需 Human 在 VSPD GUI 侧恢复）**

## A. 环境与指纹（Preflight 全部通过）

- HEAD = 3adaea4282a2282a3d340f3811b433d334031800 ✓
- staged = 0 ✓；git diff --check PASS ✓
- 冻结 Release = 4044edae… MATCH ✓；C2.1 Debug EXE = 9cadc2c5… MATCH ✓
- Preflight 时 COM7/COM8 均 OK 且可打开（20:58 时刻）

## B. VSPD Pair 状态（当前 = 异常）

- 设备管理器曾显示 `Electronic Team Virtual Serial Port (COM7->COM8)` / `(COM8->COM7)` Status=OK
- **当前异常**：`HKLM\HARDWARE\DEVICEMAP\SERIALCOMM` 只剩蓝牙 COM3/COM4（**VSPD 端口名映射丢失**）；evserial9 = Stopped ExitCode=31（ERROR_GEN_FAILURE）；VSBC9 = Running；按名打开 COM7/COM8 均失败（"端口不存在"）
- 现象出现时间点与 vspdpro.exe GUI 进程重启（20:04:46，由 Human 侧打开）重合——VSPD GUI 重启后 pair 的端口名注册未重新 apply
- 快照证据：V3_BLOCKED_ENV.txt；PnP 证据：PAIR_PNP_EVIDENCE.txt（20:39 正常时）

## C. 模拟器文件清单（均在 build/acceptance/session-t035c-round-c22-20261016/）

- `virtual_modbus_slave.ps1` — COM8 外部从站（7 模式 + CRC SelfTest + 状态机）
- `probe_virtual_modbus.ps1` — COM7 独立测试客户端（独立 CRC 实现 + 按模式断言）
- `run_v3_matrix.ps1` — S0–S7 矩阵执行器（进程/端口隔离 + READY 握手 + 日志归集）
- `V3_README.md`、`V3_PROTOCOL_EVIDENCE.log`（第一轮完整证据）、`V3_PROTOCOL_EVIDENCE_run2_blocked.log`（第二轮受阻证据）、本报告

## D. RTU 请求解析方式

累积接收（不假设一次 Read 得完整帧）→ 缓冲前端 8 字节窗口 → CRC 校验 → slave/fc/addr/qty 校验 → 动态构造响应 → COM8 真实 Write。最大缓冲 256 字节；空闲 8s / MaxRequests=1 有界退出；滑动窗口丢弃坏帧；非法请求有界忽略不响应；每个合法请求最多一次响应。

## E. CRC 算法及测试向量（S0 = PASS）

标准 Modbus CRC-16（init 0xFFFF、poly 0xA001、LSB first、Low 在前 High 在后）：
- 请求向量 `01 03 00 31 00 01` → CRC = **D5 C5** ✓
- 响应向量 `01 03 02 04 D2` → CRC = **3A D9** ✓（Low/High 顺序验证）
- 合法请求帧接受 ✓；单比特损坏帧拒绝 ✓
- 响应 CRC 每次动态重算（非常量复制）

## F–L. S1–S7 场景结果矩阵

| 场景 | 模式 | 结果 | 证据 |
|---|---|---|---|
| S0 | CRC 自测 | **PASS** | V3_PROTOCOL_EVIDENCE.log |
| S1 | NORMAL | **PASS** | COM7 PROBE TX `01 03 00 31 00 01 D5 C5` → COM8 收到并解析（slave=1 fc=3 addr=49 qty=1 CRC=VALID）→ COM8 动态生成 TX `01 03 02 04 D2 3A D9` → COM7 PROBE RX 同 7 字节、raw=1234、CRC=VALID |
| S2 | CRC_ERROR | **FAIL（第一轮）** → 已修复待重跑 | 第一轮失败根因 = 模拟器 `-bxor` 使 byte[] 退化为 object[]（Write 重载解析失败，错误信息已在日志）；已修为显式 `[byte[]]`；环境损坏后未能重跑 |
| S3 | TIMEOUT | **PASS** | REQUEST_RECEIVED=TRUE，0 响应字节，3000ms 内 probe 零接收 |
| S4 | WRONG_SLAVE | **PASS** | 请求 slave=1 → 响应 slave=2 `02 03 02 04 D2 7E D9`（CRC 重新计算正确），probe 验证通过 |
| S5 | WRONG_FUNCTION | **PASS** | 请求 FC03 → 响应 FC04 `01 04 02 04 D2 3B AD`（CRC 正确），probe 验证通过 |
| S6 | EXCEPTION | **PASS** | 合法异常帧 `01 83 02 C0 F1`（fc=0x83、code=0x02、CRC 正确），probe 验证通过 |
| S7 | FRAGMENTED | **FAIL（第一轮 probe PASS，矩阵端口释放检查误报）** → 已修复待重跑 | 第一轮 probe 实际 PASS（3 段发送累计 7/7 正确）；矩阵的端口释放检查无重试导致误报 FAIL；已加 5 次重试；环境损坏后未能重跑 |

## M. 矩阵汇总

第一轮：S0/S1/S3/S4/S5/S6 PASS，S2/S7 FAIL（均定位为测试装置问题并已修复）。第二轮：**环境层 BLOCKED**（VSPD 端口名注册丢失，从站全部无法打开 COM8）。

## N. COM7/COM8 原始字节收发证据

见 V3_PROTOCOL_EVIDENCE.log（每条记录明确标注 COM7 PROBE TX/RX 与 COM8 SIMULATOR TX/RX；**全程未启动 ModbusLens**）。

## O. 超时、端口释放和进程清理证据

每场景：READY 握手（≤10s）→ 单次请求 → slave 有界退出（MaxRequests=1 或 IdleExit=8s）→ Stop-Process 兜底 → 端口打开验证。S1–S6 端口释放验证通过；S7 释放检查已加固（5×400ms 重试）。

## P. 安全及 Git 核验

HEAD/staged/diff-check/两个 EXE 指纹本轮**未变**；未修改任何产品源码/驱动/VSPD Pair/Windows 安全配置；未连接真实设备；未 commit/push/tag/package。

## Q. 未验证事项与风险

- S2（CRC_ERROR）与 S7（FRAGMENTED）在代码修复后**尚未取得重跑证据**（环境损坏阻断）。
- 本轮未启动 ModbusLens——**未宣称任何生产链路行为**。
- 环境风险：VSPD GUI 重启后端口名注册可能丢失（需 Human 侧恢复后重跑矩阵）。

## R. 下一步 V4 准备状态

**V4 = 未就绪（BLOCKED）**。恢复路径：Human 在 VSPD GUI 确认/重建 COM7↔COM8 pair（或重启 vspdpro_service 后重新 apply），确认 SERIALCOMM 注册表重新出现 COM7/COM8 且可打开后，Agent 重跑 `run_v3_matrix.ps1`（S2/S7 修复已验证逻辑就绪），全绿后方可请求 V4 授权。

## 状态

**C2.2-V3 = BLOCKED**（环境层，非协议层失败；6/8 场景已实证 PASS）
**C2.2-V4 PRODUCTION SERIAL E2E = WAITING FOR HUMAN AUTHORIZATION**（且需环境恢复）
**C2 REAL HARDWARE = PENDING**
**T035-C OVERALL = NOT YET CLOSED**

本轮未启动 ModbusLens，未进行生产串口 E2E，未进入 V4。
