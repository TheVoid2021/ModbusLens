# C2.2 虚拟串口验收证据归档（T035-C）

## A. 归档身份

```text
PROJECT            = ModbusLens
TASK               = T035-C
ACCEPTANCE         = C2.2 Virtual Serial
SOURCE ENVIRONMENT = VSPD 11（11.0.1096）
ACCEPTANCE STATE   = HUMAN ACCEPTED / CLOSED
REAL HARDWARE      = PENDING
ARCHIVE STATE      = ARCHIVE COPY VERIFIED / GIT TRACKING PENDING
```

## B. 原始来源

原始证据目录：`build/acceptance/session-t035c-round-c22-20261016/`

本归档的 `source/` 是该目录 **75 个文件**的**字节级副本**（不转码、不重写行尾、不格式化、不重新生成）。
逐文件 SHA-256 见同目录 `SHA256SUMS.csv`。原始目录**仍完整保留在原位置**（本归档为副本，不替代原件）。

## C. 验收结果

```text
C2.2-V2  双向字节传输        = 4/4 PASS
C2.2-V3  外部 RTU 从站矩阵    = 8/8 PASS（EXIT_CODE=0）
C2.2-V4  生产 QSerialPort E2E = 7/7 PASS
```

- E1 正常读取原始报文：REQUEST `01 03 00 31 00 01 D5 C5` → RESPONSE `01 03 02 04 D2 3A D9`（raw 1234）
- Human GUI 确认工程值：**12.34 s**（Decoder=Linear / Scale=0.01 / Offset=0 / Unit=s）
- Human 确认 E2–E6 异常时明确显示失败/超时/异常，未将历史成功值冒充为本次成功

## D. 能力边界

**已经验证**：

- Windows VSPD 虚拟 COM7 ↔ COM8（VSPD 11.0.1096，驱动 WHQL 签名有效）
- 生产 QSerialPort 软件串口链路（EngineeringPage → EngineeringReadController → AnalysisController → SerialTransactionAdapter → SerialPortAdapter）
- 外部 Modbus RTU 从站模拟器（COM8，动态解析 + 动态 CRC + 真实收发）
- 正常与异常 RTU 收发（FC03 / CRC 错 / 超时 / 错从站 / 错功能码 / 合法异常帧 / 分段）
- 静态 Linear 工程值计算与 GUI 展示

**尚未验证**：

- 真实 USB-RS485 转换器
- RS485 A/B 物理电气层（差分电平、终端电阻、总线拓扑、共模与干扰）
- 真实 Modbus 设备
- 现场总线（多从站、长线、屏蔽、接地）与真实通信时序

## E. 保留观察

| 编号 | 事项 | 状态 |
|---|---|---|
| OBS-V4-001 | 首次工程值读取出现一次未定论超时（随后两次成功） | **OPEN / NON-BLOCKING**（根因未知，未归类为已修复） |
| OBS-V4-002 | 工程值页面文案过时（仍称「不进行真实读取、不计算数值」） | **UI COPY ISSUE / DEFERRED** |
| OBS-V4-003 | E7 120ms 分段接收 = 扩展压力测试 | **LIMITATION RECORDED**（不构成标准 RTU 时序合规结论） |

## F. 验证方式（如何用清单复算）

在归档根目录执行（PowerShell）：

```powershell
Import-Csv .\SHA256SUMS.csv | ForEach-Object {
    $p = Join-Path .\source ($_.RelativePath -replace '/','\')
    $h = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLower()
    [PSCustomObject]@{
        Path   = $_.RelativePath
        Match  = ($h -eq $_.SHA256)
        SizeOK = ((Get-Item -LiteralPath $p).Length -eq [int64]$_.SizeBytes)
    }
} | Where-Object { -not $_.Match -or -not $_.SizeOK }
```

无输出 = 全部一致。清单共 75 行，路径唯一，**不含** README / 本清单 / 保存报告自身的条目。

## G. 证据来源限制（必须严格区分）

| 证据类型 | 归档内是否有原件 | 说明 |
|---|---|---|
| Agent 原始通信日志（`slave_log_*.log` / `probe_log_*.log` / `V3_R3_FULL_MATRIX.log` 等） | **有**（`source/` 下） | 权威 TX/RX 与 CRC 证据 |
| Agent 验收报告（`V4_R2_FINAL_REPORT.md`、`V4_R3_HUMAN_FINAL_ACCEPTANCE.md`、`V4_R1_ERROR_MATRIX.log`、`V4_R1_NORMAL_TXRX.log` 等） | **有** | 引用与裁定记录 |
| Human GUI 截图 | **无** | **Human 在对话中提供的截图未落盘到源目录**（源目录扩展名分布无任何图片文件）；本归档**不包含截图原文件**，截图仅存在于对话记录中 |
| Human 对话确认（H1 进程退出、H2 异常显示、E1 12.34 s 观察） | **无原件** | 以 `V4_R1_GUI_EVIDENCE.md` 的转述记录为准，属 Human 提供，非 Agent 独立实测 |

## H. Git 状态（重要）

**本轮仅完成字节级复制与 SHA-256 校验，尚未执行任何 Git 操作。**

- 归档目录当前为 **untracked**（`ARCHIVE COPY VERIFIED / GIT TRACKING PENDING`）
- **未执行** `git add` / `commit` / `push` / `tag` / `package`
- **不得**声称本证据已被远程仓库永久保存；如需纳入 Git，须单独审批精确暂存与提交范围（避免混入既有 WIP）
- 外部备份（网盘/离线归档）**未验证**

## 备注：`.flag` 文件性质

`source/` 中的 7 个 `slave_ready_*.flag` 是外部从站启动时的**运行期就绪标记**（内容为 `READY`），
不属于验收结论证据，但为忠实反映源目录状态而一并归档（体积可忽略）。
