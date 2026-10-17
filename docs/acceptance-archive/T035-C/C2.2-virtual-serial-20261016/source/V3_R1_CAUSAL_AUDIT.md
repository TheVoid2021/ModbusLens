# V3_R1_CAUSAL_AUDIT.md — 因果审计（2026-10-16 20:17）

## 问题

COM7/COM8 在 20:03:49 起无法打开（「端口不存在」），V3 矩阵第三轮 BLOCKED。
需要裁定：是测试脚本/矩阵导致，还是外部操作导致？

## 事件先后顺序（证据链）

1. **19:39-19:41**：V2 双向字节测试全 PASS（VSPD **11**，COM7↔COM8 正常）——VSPD_PAIR_TEST.log
2. **20:00:58-20:02:19**：V3 矩阵**第二轮**：S0 PASS；S1 NORMAL、S3 TIMEOUT、S4 WRONG_SLAVE、S5 WRONG_FUNCTION、S6 EXCEPTION 均 PASS（完整 TX/RX 证据）——**此期间 COM7/COM8 完全正常**（v11）
3. **20:02:21**：`C:\Windows\System32\drivers\evsbc9.sys` 被**创建**（created=20:02:21）——**VSPD 10 驱动覆盖安装写入时刻**；DriverStore 同步出现 v10 包（evsbc9.inf_amd64_6af30472…、evserial9.inf_amd64_2ee62e07…，updated 20:02）
4. **20:03:46-20:04:08**：V3 矩阵**第三轮**（CRC_ERROR 修复 + 释放重试）：S0 PASS；S1-S7 全 FAIL「端口 COM8 不存在」——**驱动替换后 pair 设备被移除**
5. **20:04:46**：vspdpro（pid 45380）出现——**v10 安装过程的 GUI/服务启动**（非我们的进程）
6. **20:08:53-20:09:07**：`D:\Virtual Serial Port Driver 10\` 安装收尾（unins000.exe modified 20:08:53；目录 created 20:09:07）
7. **20:10:31**：vspdpro（v10，pid 4056，当前）启动
8. **当前**：卸载注册项仅剩 VSPD 10（Build 10.0.992）；**VSPD 11 目录已删除**（v11 卸载完成）

## 脚本清理行为审计（READ-ONLY，全部命中列出）

`run_v3_matrix.ps1` 的进程操作：
- `:26` `Get-Process powershell | Where Id -ne $PID | Out-Null` —— **只读取后丢弃（Out-Null），无 Stop-Process**
- `:36` `Start-Process powershell.exe -File virtual_modbus_slave.ps1`（自己的从站，-PassThru）
- `:46` `Stop-Process -Id $slave.Id -Force`（**精确 PID = 自己启动的从站**）
- `:50` `Start-Process ... probe_virtual_modbus.ps1`（自己的客户端，-Wait）
- `:56` `Stop-Process -Id $slave.Id -Force`（同上，精确 PID）

`virtual_modbus_slave.ps1` / `probe_virtual_modbus.ps1`：**零进程操作**（grep 无命中）。

审计结论：**无按进程名杀进程、无 vspdpro/vspdpro_service 目标、无服务操作、无注册表写、无 VSPD 配置工具调用、无不释放 SerialPort 的分支（均 finally 关闭）**。矩阵按 PID 精确清理，目标范围仅限自身子进程。

## 因果裁定

- **CONFIRMED 外部原因（非本项目脚本）**：端口消失的直接原因 = **VSPD 10 覆盖安装（20:02:21 驱动文件被替换）**。驱动替换导致 v11 的 pair 设备实例（VSBC9\DEVICES\0000/0001）被移除，COM 名映射随之失效。时间戳是决定性证据：20:02:21 驱动写入 vs 20:03:49 端口消失，间隔 88 秒；v11 卸载后 pair 配置未迁移到 v10。
- **NOT our scripts**：脚本清理仅按精确 PID（完整审计见上）；无任何操作可影响 vspdpro 或驱动状态。**「GUI 在 20:04:46 出现」的因果 = v10 安装过程的一部分，而非「GUI 重启导致端口消失」**——端口在 GUI 出现前 2 分钟（20:03:49 第三轮矩阵起始失败、20:02:21 驱动替换）已失效。
- **LIKELY CONTRIBUTOR（环境）**：VSPD 版本切换（11 → 10）后 pair 未重建；v10 的 evserial9 服务当前 Stopped(ExitCode=31，因无端口实例按需未启动，非故障)。
- **UNRESOLVED**：无（因果链完整且证据闭合）。

## 与我们脚本无关的旁证

- 20:03:49 失败的第三轮会话中，S0（CRC 自测）依然 PASS——脚本本身健康，只是 COM8 无法打开。
- 第二轮矩阵（20:00-20:02）在同样脚本下 6 场景 PASS——脚本行为未变。
- 驱动替换发生在第二次与第三次矩阵运行之间，与脚本运行窗口无重叠。
