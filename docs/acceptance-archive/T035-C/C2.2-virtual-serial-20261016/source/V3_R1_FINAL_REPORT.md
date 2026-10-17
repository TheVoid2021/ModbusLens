# V3_R1_FINAL_REPORT.md — T035-C Round C2.2-V3-R1 RCA + Controlled Recovery

日期：2026-10-16 20:18
结论：**C2.2-V3 = BLOCKED（情况 C：PAIR CONFIGURATION LOST，含 VSPD 版本切换 11→10）**

## A. 故障前后证据

见 V3_R1_ENVIRONMENT_BEFORE.md（完整快照）与 V3_R1_CAUSAL_AUDIT.md（时间线）。
要点：20:02:21 v10 驱动 evsbc9.sys 覆盖写入 → 20:03:49 端口消失；v11 目录已被卸载删除。

## B. VSPD Bundle 状态

**当前无任何 pair 端口实例**（VSBC9 bus 运行中，DEVICES 0000/0001 已移除）。未启动新 VSPD 进程、未执行任何 get/set 配置命令（按 §3 只读边界）。

## C. COM 名称映射结果

- SERIALCOMM：仅蓝牙 COM3/COM4；GetPortNames()：COM3/COM4；Win32_SerialPort：COM3/COM4
- 打开测试：COM7/COM8 均「端口不存在」FAIL
- 五项区分（A Pair 配置 / B PnP 实例 / C 名称映射 / D 可打开 / E 可互传）：**全部为「否」**

## D. 设备与服务状态

VSBC9 Running(0)；evserial9 Stopped(31，按需无实例)；全系统 0 问题设备；驱动签名全部 **Valid（WHQL）**——**安全状态无异常**。

## E. 进程/脚本清理审计

矩阵脚本仅按精确 PID 清理自身子进程（完整命中列表见 CAUSAL_AUDIT）；slave/probe 零进程操作；无服务/注册表/VSPD 干预。**脚本无过错（CONFIRMED）**。

## F. 20:04:46 GUI 事件证据与因果分类

- 20:04:46 vspdpro 出现 = **VSPD 10 安装过程的组成部分**（20:02 驱动写入、20:08-20:09 文件收尾、20:10:31 GUI 启动——完整安装序列）；
- 端口失效时间 **20:02:21（驱动替换）先于 GUI 出现（20:04:46）** ⇒ **「GUI 重启导致端口消失」的假设被时间戳否定**；
- 分类：**CONFIRMED ROOT CAUSE = 外部 VSPD 10 覆盖安装**；LIKELY CONTRIBUTOR = 版本切换后 pair 未重建；UNRESOLVED = 无。

## G. 是否需要 Human 执行恢复

**是。** 情况 C ⇒ 本轮 STOP，不自行重建。Human 最小恢复操作（建议顺序）：
1. 在已打开的 VSPD 10 GUI（D:\Virtual Serial Port Driver 10\vspdpro.exe）中创建一对虚拟串口 **COM7 ↔ COM8**（null-modem / paired 模式，勿选网络重定向），与 V2 相同；
2. 确认设备管理器中 `Electronic Team Virtual Serial Port (COM7->COM8)/(COM8->COM7)` 出现且 SERIALCOMM 重新包含 COM7/COM8；
3. 通知 Agent —— Agent 将依序执行：**V2 复验（双向字节 A/B/C/D）→ 环境复核 → V3 全矩阵 S0–S7（8/8 必须从新运行取得）→ 结果落 V3_R1_RERUN.log**。
注：本轮验证基线已从 VSPD 11 变为 VSPD 10，**V2 复验结果将代表 v10 环境**，报告中会如实披露版本。

## H. V2 复验结果（若执行）

**未执行**（情况 C，端口不存在）。

## I. S0–S7 全矩阵结果（若执行）

**未执行**（情况 C）。现有证据保持：第二轮矩阵 S0/S1/S3/S4/S5/S6 PASS（v11）；S2/S7 装置修复后待重跑。

## J. 测试后 COM7/COM8 是否仍正常

不适用（未运行）。当前 COM7/COM8 仍不存在（同快照）。

## K. Git/EXE 安全审计

HEAD `3adaea4282a2282a3d340f3811b433d334031800` 未变 · staged=0 · diff-check PASS · 冻结 Release `4044edae…` MATCH · C2.1 Debug EXE `9cadc2c5…` MATCH · 产品源码零修改 · 无真实设备/Provider 请求 · 无 commit/push/tag/package/LKGC。

## L. 是否满足进入 V4 的先决条件

**不满足（BLOCKED）**：pair 不存在、V2 未复验、V3 未达成 8/8。须先完成 §G 恢复路径。

---

**C2.2-V3 = BLOCKED（情况 C：PAIR CONFIGURATION LOST；根因 CONFIRMED = 外部 VSPD 10 覆盖安装）**
**C2.2-V4 = NOT AUTHORIZED（未就绪）**
**REAL HARDWARE = PENDING**

本轮未启动 ModbusLens，未进入 V4。**STOP — RETURN TO HUMAN / CHATGPT REVIEW。**
