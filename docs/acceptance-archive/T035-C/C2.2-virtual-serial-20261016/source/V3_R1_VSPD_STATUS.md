# V3_R1_VSPD_STATUS.md — VSPD 官方/系统只读状态（2026-10-16 20:17）

## 官方工具

- v10 安装目录：`D:\Virtual Serial Port Driver 10\`（vspdpro.exe / vspdpro_service.exe / NT6x64\*.exe）
- 按 V3-R1 §3：**未启动任何新 VSPD 进程**；vspdpro（pid 4056，20:10:31 启动）为**已运行实例**（Human 侧）。
- 只读 CLI 探测：v10 的 `vspdpro_service.exe` **未运行**；GUI 的 pair 状态需 Human 在场观察（本轮不改动配置、不启动新进程）。

## 当前 Bundle / Pair 状态（经系统 PnP 与端口映射交叉核对）

- VSBC9 bus（Virtual Serial Bus Enumerator 10）: **Running**，ROOT\SYSTEM\0004，CM_PROB_NONE
- **Pair（VSBC9\DEVICES）: 无端口实例** —— v11 的 pair 在 v10 覆盖安装时被移除，v10 尚未创建任何 pair
- SERIALCOMM: 仅蓝牙 COM3/COM4
- 未发现 opened-by 占用者（端口不存在，无法被占用）

## 驱动状态（v10）

| 驱动 | 文件版本 | 签名 | 服务状态 |
|---|---|---|---|
| evsbc9.sys | 10.5.871.0 | Valid（Microsoft Windows Hardware Compatibility Publisher） | VSBC9 = Running, ExitCode=0 |
| evserial9.sys | 10.6.960.0 | Valid（同上） | Stopped, ExitCode=31（无端口实例，按需未启动；非签名/加载故障） |
| sershare.sys | 10.0.191.0 | Valid（同上） | Stopped, ExitCode=1077（按需未启动） |

- 全系统问题设备：**0 个**（无 Code 52/577）
- DriverStore：v11 包（3ff5a5b6…/83aef5d7…）与 v10 包（6af30472…/2ee62e07…）并存，当前生效为 v10

## 许可证/试用（只读）

- 未在注册表 Eltima 键下发现可读的许可证状态值（证书/授权信息由 GUI 内展示；需 Human 观察）。
- v10 GUI 当前运行中（pid 4056）——**未做任何交互**。

## 与 V1/V2 验证基线的差异（必须披露）

- V1/V2 的验证基线 = **VSPD 11.0.1096**（evsbc9 10.6.x、evserial9 10.6.x、pair 由 v11 GUI 创建）。
- 当前环境 = **VSPD 10.0.992**（evsbc9 10.5.871.0、evserial9 10.6.960.0）——**版本已变化**；pair 需要在 v10 GUI 中重建后，V2 复验（双向字节）与 V3 矩阵才有意义。
