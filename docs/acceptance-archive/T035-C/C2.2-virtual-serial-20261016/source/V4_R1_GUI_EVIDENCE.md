# V4_R2_GUI_EVIDENCE — Human GUI 观察记录（2026-10-17 16:55–17:11）

## 前一轮（V4-R1）占位说明

V4-R1 时本文件为 NOT EXECUTED（§6.1 阻塞）。**本轮（V4-R2）Human 实际执行了 GUI 操作并逐项反馈**，本文件为逐轮原文转述记录。

## 启动与模式

| 项 | Human 观察 |
|---|---|
| ModbusLens 启动 | 正常（`build/debug-h2/start_modbuslens_debug.ps1`，无测试参数） |
| 启动后顶部模式 | 「**模拟器模式 · 确定性演示**」（未连接串口时的默认源） |
| 连接 COM7 (9600) 后 | 顶部变为「**串口模式 / COM7 @ 9600**」 |
| 断开连接后 | 界面与串口状态恢复正常（无卡死/异常关闭） |

## Device Profile 与 EngineeringReady

| 项 | Human 观察 |
|---|---|
| 档案 | `V4-E2E-Test` |
| 寄存器 | `FC03 @//0.0.0.49/ — 测试时间间隔` |
| Readable | PASS |
| **EngineeringReady** | **PASS** |
| Decoder | Linear（显式） |
| Slave ID / Timeout | 1 / 1000 ms |
| Serial（最初截图） | NOT CONNECTED |
| Read（最初截图） | NOT EXECUTED |

## E1–E7 逐项 Human 反馈（原文转述）

| 轮次 | Agent 提问 | Human 选择 |
|---|---|---|
| E1 | 工程值页读取后的显示 | **「显示 12.34 s」** |
| E2 | CRC_ERROR 后界面 | **「显示失败/CRC 错误」** |
| E3 | TIMEOUT 后界面 | **「显示超时」** |
| E4 | WRONG_SLAVE 后界面 | **「显示失败/拒绝」** |
| E5 | WRONG_FUNCTION 后界面 | **「显示失败/拒绝」** |
| E6 | EXCEPTION 后界面 | **「显示从站异常」** |
| E7 | FRAGMENTED 后界面 | **「得到 12.34 s」** |

补充（E1 前期）：
- 首次在「通信」页点击读取（非工程值页）：发出 `01 03 00 00 00 02 C4 0B`（从站日志实证），被从站按契约拒绝。
- 「工程值」页首次点击曾报「响应超时」一次；随后同会话两次点击均成功。
- **该次超时成因未定论**（见 V4_R1_NORMAL_TXRX.log「关键提示」），**如实记录**。

## 收尾

| 项 | Human 反馈 |
|---|---|
| 断开串口 + 关闭 ModbusLens | 「已断开并关闭」 |
| 观察项 | 落档时检测到 `modbuslens` 进程（pid 33524，17:06:26 启动）仍在运行（**未持串口**，COM7/COM8 均可打开）——疑为第二个 GUI 实例未关闭，**待 Human 确认** |

## 声明边界

本文件记录的是 **Human 的界面观察**（Human 主动提供）。截图由 Human 侧保存（Agent 未直接读取图片文件）。
**`HUMAN GUI ACCEPTED` 的最终裁定权在 Human**；本文件只做忠实记录，不代替验收结论。


---

## R3 附录 — Human 最终确认补充（2026-10-17）

> 追加记录。上文（R2 逐轮观察）保持原样；当时「HUMAN GUI ACCEPTED = PENDING HUMAN 最终裁定」的表述
> 不追改为当时已受裁定。

**H1 — 进程退出**：Human 于 2026-10-17 执行
Get-CimInstance Win32_Process -Filter "Name='modbuslens.exe'" | Select-Object ProcessId, CreationDate, ExecutablePath
**无输出** ⇒ 该次查询时 modbuslens.exe 已不存在；R2 记录的 PID 33524 收尾观察解除。
（Agent 未独立复测该进程状态；此为 Human 提供的结果。）

**H2 — 异常响应安全显示（Human 明确确认原文要点）**：
「E2–E6 发生错误时，ModbusLens 明确显示失败、超时或异常，没有将之前的 12.34 s 继续标记为本次读取成功。」
- E2 CRC_ERROR：失败状态正确
- E3 TIMEOUT：超时状态正确
- E4 WRONG_SLAVE：拒绝异常响应
- E5 WRONG_FUNCTION：拒绝错误功能码响应
- E6 EXCEPTION：显示 Modbus 从站异常
- 旧成功值未被冒充为当前新的成功读数

**裁定**：HUMAN GUI ERROR-STATE ACCEPTANCE = **PASS**（Human GUI 行为确认，不扩展为内部状态机测试结论）。

**截图落档状态说明**：Human 在对话中提供了 GUI 截图；**截图文件是否已落盘至本证据目录未经 Agent 核实**（本轮为证据归档轮，未获取图片文件）。
