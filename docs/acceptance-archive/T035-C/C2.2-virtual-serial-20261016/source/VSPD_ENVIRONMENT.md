# VSPD_ENVIRONMENT.md — Round C2.2-V1 READ-ONLY 验证报告（2026-10-16 19:13+08）

## Secure Boot
**ON**（注册表 HKLM\SYSTEM\CurrentControlSet\Control\SecureBoot\State → UEFISecureBootEnabled = 1；
PolicyPublisher = {77fa9abd-0359-4d32-bd60-28f4e78f784b}）。
Confirm-SecureBootUEFI 需管理员被拒（访问被拒绝），已按指令记录实际错误并用注册表
等价证据替代，未将查询失败解释为关闭。

## TESTSIGNING
**OFF**（bcdedit 需管理员被拒——无法直接读取；但 Secure Boot = ON 时 TESTSIGNING
不可能启用，两者互斥；且 bcdedit 无参输出中无 testsigning 行）。nointegritychecks
同样未出现（= 默认 No）。

## Driver Signature Enforcement
**正常**（Secure Boot ON + 无 Code 52/577 设备 + 全系统无错误状态设备 + 驱动签名
为 Microsoft Windows Hardware Compatibility Publisher 合法签名）。

## VSPD 安装状态（四态区分）

| 状态 | 判定 | 证据 |
|---|---|---|
| 软件已安装 | ✓ | 卸载注册项「Virtual Serial Port Driver 11 by Electronic Team, Inc. (Build 11.0.1096)」Publisher = Electronic Team, Inc. |
| 驱动包已安装 | ✓ | DriverStore\FileRepository 有 evsbc9.inf_amd64_… 与 evserial9.inf_amd64_… |
| 驱动已加载 | ✓ | VSBC9（Virtual Serial Bus Enumerator）State=Running ExitCode=0；evserial9=Stopped ExitCode=1077（ERROR_SERVICE_NEVER_STARTED = 按需未启动，非错误）；PnP「Virtual Serial Bus Enumerator 10 (Electronic Team)」Status=OK Problem=CM_PROB_NONE |
| 虚拟端口已创建 | **✗（尚未创建）** | 当前 COM 列表仍只有 COM3/COM4（蓝牙），无 VSPD Ports 设备 |

## 驱动签名（.sys 文件逐一核验，非凭产品名推断）

- evserial9.sys：Status=**Valid**，Signer=CN=Microsoft Windows Hardware Compatibility Publisher, O=Microsoft Corporation
- evsbc9.sys：Status=**Valid**，Signer=同上
- sershare.sys：Status=**Valid**，Signer=同上
（WHQL/硬件兼容性计划签名——Secure Boot ON 下合法可加载；无 Code 52/577）

## 设备管理器错误状态

- VSPD 设备：OK，Problem=CM_PROB_NONE
- 全系统非 OK 状态设备：**0 个**（无 Code 52/577 或其他签名错误）

## 当前 COM 列表（逐项分类）

- COM3 = 蓝牙串口（BTHENUM，蓝牙链接上的标准串行）——**原有，未变**
- COM4 = 蓝牙串口（BTHENUM）——**原有，未变**
- VSPD 虚拟 COM：**NONE（尚未创建）**

未把蓝牙 COM3/COM4 误认为虚拟 null-modem pair。

## 项目安全状态

- HEAD = 3adaea4282a2282a3d340f3811b433d334031800 ✓
- staged = 0 ✓
- git diff --check = PASS ✓
- 冻结 Release SHA256 = 4044edae…（MATCH，未变）✓
- C2.1 Debug EXE SHA256 = 9cadc2c5…（MATCH，未变）✓
- 残留 modbuslens.exe 进程 = 0 ✓

## 本轮禁止事项遵守

未修改任何源码/Profile/驱动/安全设置；未执行 bcdedit /set；未创建/删除 COM pair；
未连接真实设备；未发送 Modbus 请求；未 commit/push/tag/package。
