# V3_R1_ENVIRONMENT_BEFORE.md — Round C2.2-V3-R1 环境快照（2026-10-16 20:16:57）

## VSPD 进程
- vspdpro pid=4056 start=20:10:31 path=D:\Virtual Serial Port Driver 10\vspdpro.exe
- （无 vspdpro_service 进程运行）

## VSPD 服务/驱动
- evserial9: State=Stopped StartMode=Manual ExitCode=31（ERROR_GEN_FAILURE）Path=C:\WINDOWS\system32\DRIVERS\evserial9.sys
- VSBC9: State=Running StartMode=Manual ExitCode=0 Path=C:\WINDOWS\system32\drivers\evsbc9.sys
- sershare: State=Stopped ExitCode=1077（按需未启动，非错误）

## PnP 设备
- OK | System | Virtual Serial Bus Enumerator 10 (Electronic Team) | ROOT\SYSTEM\0004 | CM_PROB_NONE
- **VSBC9\DEVICES\0000（原 COM7）/ \0001（原 COM8）设备实例已不存在**
- 全系统问题设备：(none)

## 端口名映射与打开测试（交叉核对）
- SERIALCOMM 注册表：仅 \Device\BthModem1=COM4、\Device\BthModem0=COM3（蓝牙）
- GetPortNames()：COM3、COM4
- Win32_SerialPort：仅 COM3/COM4（蓝牙）
- 直接打开：COM7 FAIL「端口不存在」；COM8 FAIL「端口不存在」

## 驱动签名（当前文件，v10 安装后）
- evserial9.sys: Status=Valid Ver=10.6.960.0 Signer=Microsoft Windows Hardware Compatibility Publisher
- evsbc9.sys: Status=Valid Ver=10.5.871.0 Signer=Microsoft Windows Hardware Compatibility Publisher
- sershare.sys: Status=Valid Ver=10.0.191.0 Signer=Microsoft Windows Hardware Compatibility Publisher

## 版本切换证据（关键时间线，文件系统 mtime）
| 时间 (10-16) | 事件 | 证据 |
|---|---|---|
| 18:55 | VSPD 11 驱动包安装 | evserial9.inf_amd64_83aef5d7… updated 18:55 |
| 19:33 | VSPD 11 evserial9.inf 包 | evserial9.inf_amd64_83aef5d7… updated 19:33 |
| 19:39-19:41 | V2 双向字节测试 PASS（v11） | VSPD_PAIR_TEST.log |
| 20:00:58-20:02:19 | V3 矩阵第二轮：S0/S1/S3/S4/S5/S6 PASS（v11 仍工作） | V3_PROTOCOL_EVIDENCE.log |
| **20:02:21** | **v10 驱动 evsbc9.sys 被写入（created）——VSPD 10 开始覆盖安装** | 文件 created 时间戳 |
| 20:02 | v10 驱动包 evsbc9.inf_amd64_6af30472… / evserial9.inf_amd64_2ee62e07… 进入 DriverStore | DriverStore mtime |
| **20:03:49** | **V3 矩阵第三轮 S1 FAIL「端口 COM8 不存在」——端口在驱动替换后消失** | V3_PROTOCOL_EVIDENCE_run2_blocked.log |
| 20:08:53 | D:\Virtual Serial Port Driver 10\unins000.exe modified（安装收尾） | 文件时间戳 |
| 20:09:07 | D:\Virtual Serial Port Driver 10\ 目录创建完成 | 目录 created |
| 20:10:31 | vspdpro（v10 GUI）启动 | 进程 StartTime |

## 卸载注册项（当前）
- Virtual Serial Port Driver 10 by Electronic Team, Inc. (Build 10.0.992) | InstallLocation=D:\Virtual Serial Port Driver 10\
- **VSPD 11（Build 11.0.1096）注册项已消失；D:\Virtual Serial Port Driver 11\ 目录已不存在** ⇒ VSPD 11 已被卸载

## 五项状态区分（V3-R1 §2 要求）
- A. Pair 配置仍存在：**否**（VSBC9\DEVICES 0000/0001 已移除）
- B. PnP 设备实例仍存在：**否**（仅剩 Bus Enumerator ROOT\SYSTEM\0004）
- C. COM 名称映射仍有效：**否**（SERIALCOMM 无 COM7/COM8）
- D. COM 可正常打开：**否**（「端口不存在」）
- E. COM7/COM8 可互相发送字节：**否**（端口不存在，无法执行）
