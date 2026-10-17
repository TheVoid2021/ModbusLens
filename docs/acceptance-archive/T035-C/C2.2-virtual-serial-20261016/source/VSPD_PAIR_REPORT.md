# VSPD_PAIR_REPORT.md — COM7<->COM8 Pair 建立与双向字节验证（2026-10-16 19:39+08）

## 1. 管理员只读核验（Human 在管理员 PowerShell 执行）

- **SecureBoot = True**（Confirm-SecureBootUEFI 直读，管理员权限）
- **testsigning: NOT PRESENT IN BCD（= 默认 No/未设置）**（bcdedit /enum {current} 全文，无 testsigning 行）
- **nointegritychecks: NOT PRESENT IN BCD（= 默认 No/未设置）**
- 证据文件：ADMIN_VERIFY_RESULT.txt（含 bcdedit 全文：nx OptIn、hypervisorlaunchtype Auto、无 testsigning）

## 2. Pair 创建前检查（全部通过）

- COM7 未被现有设备占用/保留；COM8 未被占用/保留
- 两个端口均可打开（无其他进程占用）
- VSPD 11.0.1096（Electronic Team, Inc.）已安装、Bus Enumerator 运行中
- 未占用/替换任何既有端口，未删除其他设备

## 3. COM7/COM8 设备归属与 Pair 关系（Win32_PnPEntity / Win32_PnPSignedDriver）

| 端口 | 设备名 | Instance ID | 状态 | 驱动 |
|---|---|---|---|---|
| COM7 | Virtual Serial Port 10 (Electronic Team) | VSBC9\DEVICES\0000 | OK, CM_PROB_NONE | Electronic Team DriverVersion 10.6.960.0, **Signed=True**, Signer=Microsoft Windows Hardware Compatibility Publisher |
| COM8 | Virtual Serial Port 10 (Electronic Team) | VSBC9\DEVICES\0001 | OK, CM_PROB_NONE | 同上 |

**Pair 关系证据**：两者同挂 VSBC9 bus（Virtual Serial Bus Enumerator 10，ROOT\SYSTEM\0004），设备实例编号相邻（0000/0001）——VSPD null-modem 成对端口。全系统 0 个错误状态设备（无 Code 52/577）。蓝牙 COM3/COM4（BTHENUM）完全未变。

## 4. 双向原始字节测试（PowerShell + .NET SerialPort，无 ModbusLens / 无 fake / 无 replay / 无 pyserial）

脚本：verify_virtual_com_pair.ps1（finally 关闭释放；超时 FAIL；二进制比较；Open 成功不视为 PASS）

| 测试 | 方向 | TX | RX | 结果 |
|---|---|---|---|---|
| A | COM7 → COM8 | `01 03 00 31 00 01 D5 C5`（8B） | `01 03 00 31 00 01 D5 C5`（8/8，EXACT 二进制匹配） | **PASS** |
| B | COM8 → COM7 | `01 03 02 04 D2 3A D9`（7B） | `01 03 02 04 D2 3A D9`（7/7，EXACT 二进制匹配） | **PASS** |
| C | COM8 → COM7 分段（3 段：`01 03` / `02 04 D2` / `3A D9`） | 分段写 | 累计 `01 03 02 04 D2 3A D9`（7/7，EXACT） | **PASS**（仅证明累计字节；RTU 帧边界算法不在本轮范围） |
| D | 端口释放/重开 | `55 AA` roundtrip | `55 AA` | **PASS**（关闭→重开→roundtrip→关闭，无泄漏） |

完整日志：VSPD_PAIR_TEST.log。EXIT_CODE=0，failures=0。

**注意**：测试字节恰为合法 RTU 帧，但本阶段仅验证串口原始字节传输，**未宣称 ModbusLens 生产链路已通过**。

## 5. 环境指纹（测试后）

- HEAD = 3adaea4282a2282a3d340f3811b433d334031800（未变）
- staged = 0；git diff --check PASS
- 冻结 Release = 4044edae…（MATCH 未变）
- C2.1 Debug EXE = 9cadc2c5…（MATCH 未变）
- 残留 modbuslens 进程 = 0；测试后 COM 列表 = COM3/COM4（蓝牙）+ COM7/COM8（VSPD pair）
