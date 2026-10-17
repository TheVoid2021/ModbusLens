# ENVIRONMENT.md — 阶段 0 + 阶段 2 检测记录（2026-10-16 15:22+08）

## 阶段 0：环境预检（全部通过）

- git HEAD = 3adaea4282a2282a3d340f3811b433d334031800
- git status --short = 多文件 M（前几轮文档/实施改动，符合预期）
- staged 文件数 = 0
- git diff --check = PASS
- 产品 EXE 路径 = build/debug-h2/modbuslens.exe
- EXE SHA-256 = 9cadc2c53d80e7b74e9a4bf65dd042068d58b78a2e4e128f7b573c9445876915（**MATCH**）
- EXE 大小 = 83,084,566 B
- 启动脚本 = build/debug-h2/start_modbuslens_debug.ps1
- 残留 modbuslens.exe 进程 = 无

## 阶段 1：生产串口路径审计（通过，详见 PRODUCTION_SERIAL_PATH.md）

正常 GUI 启动 → EngineeringPage → EngineeringReadController → AnalysisController
→ SerialTransactionAdapter（生产 serialAdapter_）→ SerialPortAdapter/QSerialPort → Windows COM。
无 setSerialTransport(fake)、无 RecordingSerialTransport、无 HarnessWriteTransport、
无 runC2ReadCheck、无 --qml-c2-read-check（全部仅在门禁函数内，正常启动不经过）。

## 阶段 2：虚拟 COM pair 检测

- 当前 COM 列表：**COM3、COM4**
- COM3 = 蓝牙链接上的标准串行（BTHENUM），打开失败（信号灯超时——蓝牙未连接设备）
- COM4 = 蓝牙链接上的标准串行（BTHENUM），可打开（空闲）但为蓝牙单一端口
- com0com = **未安装**（Win32_PnPEntity 无 CNCA/CNCB 设备；安装目录不存在）
- 其他虚拟 null-modem provider = **未检测到**（Hyper-V/VMware/ToDesk 均为以太网/USB/键盘虚拟，非串口）
- Python = 3.11.7 可用；**pyserial 未安装**（ModuleNotFoundError: serial）
- COM3 <-> COM4 互联 = **否**（两者是同一蓝牙设备的传出/传入端口，非 null-modem pair）

## 判定

**缺少虚拟 COM pair provider。**
按 Round C2.2 硬性边界 7：STOP，不自行安装驱动，不修改 Windows 安全设置。
