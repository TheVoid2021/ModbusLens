# V3_R2_FINAL_REPORT.md — T035-C Round C2.2-V3-R2（VSPD 10 Pair Recovery + V2 Reverification + V3 Full Matrix）

日期：2026-10-16 21:00
结论：**C2.2-V3 = BLOCKED（环境恢复未完成：Pair#1 存在但处于 disabled，Human 侧启用未成功）**

## A. VSPD 10 实际版本与驱动签名

- 版本：**Virtual Serial Port Driver 10（Build 10.0.992）**（Electronic Team, Inc.）
- 驱动（逐文件核验）：`evsbc9.sys` Ver=10.5.871.0 / `evserial9.sys` Ver=10.6.960.0 / `sershare.sys` Ver=10.0.191.0——**签名全部 Status=Valid**，Signer=Microsoft Windows Hardware Compatibility Publisher（WHQL）
- 服务注册正常（evserial9→oem17.inf、evsbc9→oem77.inf）；**全系统 0 问题设备**（无 Code 52/577）
- 记录于 V3_R2_VSPD10_BASELINE.md

## B. COM7/COM8 Pair 状态（**RCA 核心增量**）

- **Pair#1 配置仍在注册表**：`VspdPro\Pair#1\IN: COM7=VSPD`、`\OUT: COM8=VSPD`（**无需重建**）
- **vspdpro.txt 日志铁证**：
  - `20:09:19 - Bundle "Pair#1" was enabled`（v10 安装程序创建并启用）
  - `20:10:18 - Bundle "Pair#1" was disabled`（切换期间被禁用，此后一直 disabled）
- **设备实例/名称映射：无**（SERIALCOMM 仅蓝牙；打开测试失败）——Pair#1 `type=7`、InMain/OutMain 空 = 未激活
- **Human 启用尝试**返回「**未能创建捆绑包**」——**最可能为动词误用**：在已被 Pair#1 占用的 COM7/COM8 上执行「创建新 pair」必然失败；**正解 = 对已存在的 Pair#1 执行 Enable（非 Create/Add）**

## C. V2 A/B/C/D 本轮结果

**NOT EXECUTED**（§2 前置条件未满足，按 §2 正确 STOP）——见 V3_R2_V2_RETEST.log。**未复用 v11 历史结果**。

## D. S0–S7 本轮结果

**NOT EXECUTED**——见 V3_R2_FULL_MATRIX.log。**未复用任何历史 PASS**。

## E. S2 修复验证

预审确认修复仍在（`[byte[]](Add-ModbusCrc …)` 显式转换，grep=1）；运行验证待环境恢复。

## F. S7 修复验证

预审确认修复仍在（端口释放检查 `attempt -le 5` 有界重试，grep=1）；运行验证待环境恢复。

## G. 测试前后环境对比

未执行测试 ⇒ 快照见 V3_R2_VSPD10_BASELINE.md（前）与 V3_R2_POST_ENVIRONMENT.md（当前）。无「测试后端口消失」类事件（未运行）。

## H. 原始 RTU TX/RX

无（未执行）。历史 v11 证据保持独立归档（V3_PROTOCOL_EVIDENCE.log），**不迁移、不复用**。

## I. 端口释放和进程检查

本时段无本项目测试进程运行；无端口占用（端口不存在）。

## J. Git / EXE 安全验证

HEAD `3adaea4282a2282a3d340f3811b433d334031800` ✓ · staged=0 ✓ · diff-check PASS ✓ · 冻结 Release `4044edae…` MATCH ✓ · C2.1 Debug EXE `9cadc2c5…` MATCH ✓ · 产品源码零修改 · 无真实设备/Provider 请求 · 无 commit/push/tag/package/LKGC。

## K. 未验证事项

- V2 复验（4/4）、V3 矩阵（8/8）、S2/S7 运行期验证、测试后稳定性复核——**全部待 Pair#1 合法启用后执行**。
- 截图指引：Human 提到上传 VSPD GUI 截图（尚未收到）——可选，用于精确按钮定位。

## L. V4 准备状态

**NOT READY**（V3 未完成）。

---

## Human 待办（唯一阻塞项）

在 **以管理员运行的 VSPD 10 GUI** 中，对**已存在的 Pair#1（COM7 ↔ COM8）**执行**启用**（Enable Bundle / 切换状态），**不要执行创建/Add（端口已被占用）**：
1. 关闭所有 vspdpro 进程 → 右键 `D:\Virtual Serial Port Driver 10\vspdpro.exe` → 以管理员身份运行；
2. 在列表中找到已存在的 **Pair#1** 行 → 右键 → **Enable**（或点行尾状态开关）；
3. 若 GUI 不显示已存在行：先尝试顶部「刷新/Reload」；仍无则在 GUI 中**删除该 bundle 后重建 COM7↔COM8**（仅建议 2 可行时机器删除操作请在管理员 GUI 内进行）；
4. 成功标志：设备管理器出现 2 个 `Electronic Team Virtual Serial Port (COM7->COM8)/(COM8->COM7)`，SERIALCOMM 出现 COM7/COM8。

启用后通知 Agent → 立即按序执行：**V2 复验（A/B/C/D）→ V3 全矩阵（S0–S7，全部新一轮）→ 测试后稳定性 → 落档**。

**C2.2-V2 REVERIFICATION ON VSPD10 = NOT EXECUTED**
**C2.2-V3 = BLOCKED（环境恢复未完成）**
**C2.2-V4 = NOT AUTHORIZED**
**C2 REAL HARDWARE = PENDING**

本轮未启动 ModbusLens，未进入 V4。**STOP — RETURN TO HUMAN / CHATGPT REVIEW。**
