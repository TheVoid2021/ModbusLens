# V3_README.md — C2.2-V3 External Modbus RTU Slave Simulator 说明

## 用途

独立于 ModbusLens 的**外部 Modbus RTU 从站模拟器** + **独立测试客户端**，通过真实 VSPD 虚拟串口对 COM7<->COM8 验证协议层行为。本轮不启动 ModbusLens。

## 文件

- `virtual_modbus_slave.ps1` — COM8 从站。参数：-Mode（NORMAL/CRC_ERROR/TIMEOUT/WRONG_SLAVE/WRONG_FUNCTION/EXCEPTION/FRAGMENTED）、-MaxRequests、-IdleExitSeconds、-SelfTest。
- `probe_virtual_modbus.ps1` — COM7 客户端。参数：-Mode 等。构造请求→写 COM7→累积读→按模式断言→退出码 0/1。
- `run_v3_matrix.ps1` — S0（CRC 自测）+ S1–S7 矩阵：进程/端口隔离、READY 握手、日志归集到 V3_PROTOCOL_EVIDENCE.log。

## 运行

```
powershell -NoProfile -ExecutionPolicy Bypass -File run_v3_matrix.ps1
```

## 模拟器协议实现

- 累积接收（不假设一次 Read 得完整帧）；最大缓冲 256 字节；前端 8 字节窗口 CRC 校验；坏帧滑动 1 字节丢弃。
- Slave=1、FC03、Address=49、Quantity=1 才响应；其他请求有界忽略（不响应、记录原因）。
- 响应每次**动态重算 CRC**（无常量复制、无 replay 文件）。
- 有界退出：MaxRequests（默认 1）或 IdleExitSeconds（默认 8）；finally 释放端口。
- 寄存器值 raw=1234（0x04D2）仅代表原始寄存器数据；scale/offset/unit 不属于 RTU 层（留待 V4）。

## 已知实现要点（踩坑记录）

- `param()` 必须是脚本第一条语句（`$ErrorActionPreference` 不能在其之前）。
- `-bxor` 运算后显式转回 `[byte[]]`，否则 SerialPort.Write 重载解析失败。
- 端口释放检查加重试（VSPD 设备在进程退出后有瞬时不可见窗口）。

## 当前状态

见 V3_FINAL_REPORT.md：S0/S1/S3/S4/S5/S6 PASS；S2/S7 已修复待重跑；矩阵第二轮因 VSPD 端口名注册丢失（环境层）BLOCKED。
