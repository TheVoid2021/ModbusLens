# V4_R3_HUMAN_FINAL_ACCEPTANCE.md — C2.2 虚拟串口验收最终归档

日期：2026-10-17
轮次：T035-C Round C2.2-V4-R3
模式：**EVIDENCE-ONLY / DOCUMENTATION-ONLY**（未启动任何程序、未打开任何端口、未发送任何 RTU 请求、未重新编译、未运行任何测试）

---

## A. 本轮授权范围

Human 已明确确认 C2.2-V4 最终验收条件满足，授权本轮**仅做证据归档与虚拟串口验收状态收口**。
本轮**不是**开发轮、**不是**测试轮、**不是**真实硬件验收轮。禁止为获得新的 PASS 而重复测试。

---

## B. 仓库与 EXE 指纹（本轮只读核验，来源：本轮 Agent 只读）

| 项 | 实测值 | 判定 |
|---|---|---|
| HEAD | `3adaea4282a2282a3d340f3811b433d334031800` | **MATCH** |
| staged | 0 | ✅ |
| `git diff --check` | PASS（exit 0） | ✅ |
| `git status --porcelain -uall` | 56 项未暂存改动（既有前几轮 docs/acceptance 归档，非本轮） | 说明项 |
| 冻结 Release SHA256 | `4044edae6933ed98acd6424483399dbcdd5185b26840ebfd6dc5008f14a93fe5` | **MATCH** |
| C2.1 Debug EXE SHA256 | `9cadc2c53d80e7b74e9a4bf65dd042068d58b78a2e4e128f7b573c9445876915` | **MATCH** |

与 R2 最终报告 §A 一致；**无指纹异常、无证据矛盾** ⇒ §1 resync 门禁通过。

---

## C. V2 / V3 / V4 已有测试结果引用（**历史证据，非本轮执行**）

| 轮次 | 结论 | 证据来源（归档路径） |
|---|---|---|
| C2.1 离线读取 / ISSUE-026 | PASS / CLOSED | `docs/devlog/2026-10-14.md` §38；`V3_R3_FINAL_REPORT.md` 引用 |
| C2.2-V2 双向字节（VSPD 11 复验） | **PASS（4/4）** | `V3_R2_V2_RETEST.log`（16:12 实测 A/B/C/D；文件名沿用 R2 占位名，归属见 `V4_R1_GIT_EXE_BASELINE.md`） |
| C2.2-V3 外部 RTU 模拟器矩阵 | **PASS（8/8，EXIT=0）** | `V3_R3_FULL_MATRIX.log` + `slave_log_*.log` / `probe_log_*.log` |
| C2.2-V4 正常 E2E + 异常矩阵 | **PASS（7/7）** | `V4_R1_NORMAL_TXRX.log`、`V4_R1_ERROR_MATRIX.log`、`V4_R2_E1…E7_*_slave.log`、`V4_R2_FINAL_REPORT.md` |

**声明**：以上均为**历史归档证据**（`historical evidence ≠ currently executed`）；本轮未重新执行任何一项。

---

## D. E1 正常生产通信证据

**来源：Agent R2 原始从站日志**（`V4_R2_E1_NORMAL_slave.log` = `slave_log_NORMAL.log` 快照，UTF-8 原文，未改写）

生产请求（ModbusLens → COM7 → VSPD → COM8，由外部从站真实收到）：
```
01 03 00 31 00 01 D5 C5
```
从站侧独立校验：Slave ID = 1 · Function = FC03 · Address = 49 · Quantity = 1 · **CRC = VALID**

从站动态构造并写出（COM8 → VSPD → COM7，由 ModbusLens 生产链路接收）：
```
01 03 02 04 D2 3A D9
```
Byte Count = 02 · Register = **0x04D2 = 1234** · CRC = VALID（动态计算）

**产品侧结果（来源：Human GUI 观察，见 E 节）**：
Raw = **1234** · Decoder = **Linear** · Scale = **0.01** · Offset = **0** · Unit = **s** ⇒ Engineering = **12.34 s**

**证据分离声明**：TX/RX 字节来自 **Agent R2 从站日志**；「界面显示 12.34 s」来自 **Human GUI 观察/截图**。二者**分别标注、互不替代**；未重新生成或改写任何历史 TX/RX 数据。

---

## E. E2–E6 Human 错误显示确认

**来源：Human 最终明确确认（2026-10-17，ChatGPT 审核轮）**，原始转述见 `V4_R1_GUI_EVIDENCE.md` R3 附录。

Human 确认原文要点：「**E2–E6 发生错误时，ModbusLens 明确显示失败、超时或异常，没有将之前的 12.34 s 继续标记为本次读取成功。**」

| 场景 | 从站实际写出（Agent R2 日志） | CRC 独立验证 | Human GUI 状态确认 |
|---|---|---|---|
| E2 CRC_ERROR | `01 03 02 04 D2 3A 26` | 期望 3A D9 → 实际 3A 26 = **INVALID（设计如此）** | 失败 / CRC 错误 ✅ |
| E3 TIMEOUT | （0 响应，bounded silence） | 请求到达 2 次 | 超时 ✅ |
| E4 WRONG_SLAVE | `02 03 02 04 D2 7E D9` | **7E D9 VALID** | 失败 / 拒绝 ✅ |
| E5 WRONG_FUNCTION | `01 04 02 04 D2 3B AD` | **3B AD VALID** | 失败 / 拒绝 ✅ |
| E6 EXCEPTION | `01 83 02 C0 F1` | **C0 F1 VALID**（合法异常帧） | 显示 Modbus 从站异常 ✅ |

**裁定**：`HUMAN GUI ERROR-STATE ACCEPTANCE = PASS`
**边界**：属 **Human GUI 行为确认**，**不扩展**为内部状态机测试结论，也不替代 C2.1 的离线单元测试证据。

---

## F. E7 扩展压力测试边界

**来源：Agent R2 日志（`V4_R2_E7_FRAGMENTED_slave.log`）+ Human GUI 观察**

从站分段写出（3 段，段间 **120 ms**）：`01 03` | `02 04 D2` | `3A D9`；Human 界面得到 **12.34 s**。

**证据边界（必须保持）**：
- **支持**：`RECEIVE ACCUMULATION STRESS TEST = PASS`（生产接收链可跨分段累积并完整解析）
- **不支持**：`RTU STANDARD TIMING COMPLIANCE = PASS`（120 ms 段间延迟显著超出 9600 baud 下 1.5 字符 ≈ 1.56 ms 的标准帧内间隔）

---

## G. Human 进程退出确认

**来源：Human 提供的事后只读检查（2026-10-17）** —— 非 Agent 本轮独立实测

Human 执行：
```powershell
Get-CimInstance Win32_Process -Filter "Name='modbuslens.exe'" |
Select-Object ProcessId, CreationDate, ExecutablePath
```
回报**无输出** ⇒ 该次查询时未发现 `modbuslens.exe` 进程。

**结论**：R2 观察到的 PID 33524 **不再构成未关闭进程阻塞**；结合 R2 已有的 COM7/COM8 开闭与双向校验记录，**收尾门禁裁定 PASS**。
**限制声明**：进程退出时间未记录；Agent 本轮未复测进程状态。

---

## H. 测试后串口环境稳定性

**来源：Agent R2 实测（17:12，`V4_R1_POST_ENVIRONMENT.md` §7）**

GetPortNames = COM3/COM4/COM7/COM8 · PnP `(COM7->COM8)`/`(COM8->COM7)` **OK / CM_PROB_NONE** ·
evserial9/VSBC9 **Running(0)** · SERIALCOMM `VSerial9_0/1` 未变 · 全系统 **0 问题设备** ·
COM7/COM8 开闭 OK · 最小双向校验 **`5A A5 7E` 3/3 EXACT PASS** · 蓝牙 COM3/COM4 未变。

**结论**：测试前后环境一致，Pair 完好（`historical` 证据，本轮未复测）。

---

## I. 保留的 OPEN OBSERVATIONS（本轮仅记录，不修复）

### OBS-V4-001 — 首次未定论超时
R2 中「工程值」页首次读取曾发生一次「响应超时」，随后同会话两次完整成功。
- 故障已记录（`V4_R1_NORMAL_TXRX.log`「关键提示」）
- **根因未确定**；不得推测为已确认的模拟器调度问题
- 不得因后续 PASS 而删除历史失败记录
- **状态：OPEN OBSERVATION / NON-BLOCKING FOR CURRENT ACCEPTANCE**（若再复现应立独立 RCA）

### OBS-V4-002 — 工程值页面文案过时
界面仍出现类似「仅做就绪校验，不进行真实读取、不计算数值」的说明，而当前程序已能完成真实串口读取与 Linear 工程计算。
- **状态：UI COPY ISSUE / DEFERRED**；本轮**未修改** QML 或产品代码

### OBS-V4-003 — 非标准分段压力测试
E7 的 120 ms 分段测试通过仅支持「接收累积压力测试 PASS」，**不支持**「RTU 标准时序合规 PASS」。见 F 节。

---

## J. C2.2 Virtual Serial Acceptance — 最终裁定

**来源：Human 最终确认（2026-10-17）**

```text
C2.1 = PASS / CLOSED
C2.2-V2 = PASS (4/4)
C2.2-V3 = PASS (8/8)
C2.2-V4 = HUMAN ACCEPTED / PASS (7/7)
C2.2 VIRTUAL SERIAL ACCEPTANCE = HUMAN ACCEPTED / CLOSED
PRODUCTION QSERIALPORT VIRTUAL E2E = VERIFIED ON VSPD 11
VERIFIED LKGC = UNCHANGED
```

---

## K. 真实硬件仍未验证的边界

```text
C2 REAL HARDWARE = PENDING / NOT VERIFIED
```

`VERIFIED ON VSPD 11` **仅指** Windows 虚拟 COM（VSPD 11.0.1096）环境下的**生产软件串口链路**
（EngineeringPage → EngineeringReadController → AnalysisController → SerialTransactionAdapter → QSerialPort → COM7）。

**绝不代表**以下任一项已验证：
- 真实 USB-RS485 转换器
- RS485 物理电气层（差分电平、终端电阻、总线拓扑、干扰/共模）
- 现场总线行为（多从站、长线、接地、屏蔽）
- 任何真实 Modbus 设备

---

## L. T035-C 剩余关闭条件

```text
T035-C OVERALL = NOT YET CLOSED
```

剩余条件（均需**单独 Human 授权**）：
1. **真实硬件验收**（C2 Real Hardware）——需真实 RS485 设备与现场条件；
2. **是否将本轮归档同步至 canonical docs**（`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、`docs/tasks/T035-*.md`、`docs/devlog/`）——**本轮授权明确排除**，列为**后续待授权动作**；
3. **LKGC / package 决策**——本轮不推进（`VERIFIED LKGC = UNCHANGED`）；
4. 是否处理 OBS-V4-002（UI 文案）作为独立小任务。

---

## M. 本轮实际修改文件清单与安全审计

### 本轮实际修改（仅证据目录内的 Markdown 归档）

| 文件 | 操作 | 说明 |
|---|---|---|
| `V4_R2_FINAL_REPORT.md` | **追加** `## R3 附录 — Human 最终验收补充` | 主体与时间线保持原样 |
| `V4_R1_GUI_EVIDENCE.md` | **追加** `## R3 附录 — Human 最终确认补充` | 原 R2 逐轮观察保持原样 |
| `V4_R1_POST_ENVIRONMENT.md` | **追加** `## R3 附录 — 进程退出确认` | 原 §7 实测保持原样 |
| `V4_R3_HUMAN_FINAL_ACCEPTANCE.md` | **新建**（本文件） | A–M 全章节 |

**幂等性**：追加函数以 `## R3 附录` 为存在性标记，重复执行会 SKIP（本轮首执行，全部 APPENDED）。

### 安全审计（本轮只读 + 归档）

| 项 | 判定 |
|---|---|
| HEAD 未改变 | ✅ `3adaea42…` |
| staged | ✅ 0 |
| `git diff --check` | ✅ PASS |
| 冻结 Release / C2.1 Debug SHA256 | ✅ 均 MATCH |
| 产品源码（`src/`、`tests/`、`CMakeLists.txt`、QML）零修改 | ✅ 本轮未触碰 |
| Device Profile JSON | ✅ 未修改 |
| VSPD 配置 / Windows 服务 / 注册表 | ✅ 未修改 |
| 启动 ModbusLens / VSPD GUI / 从站 / 打开 COM7/COM8 / 发送 RTU | ✅ **全部未执行** |
| 重新编译 / full CTest / V2/V3/V4 矩阵 | ✅ **全部未执行** |
| 原始 Slave/Probe 日志重写/覆盖/重编码 | ✅ **未发生** |
| 伪造截图或 PowerShell 输出 | ✅ **未发生** |
| commit / push / tag / package / LKGC | ✅ **全部未执行** |
| `docs/PROJECT_STATUS.md` 等 canonical docs | ✅ 未修改（列为待授权动作） |

### 证据来源标注汇总（不得合并）

| 结论 | 来源 |
|---|---|
| TX/RX 字节、CRC 校验、从站行为 | **Agent R2 原始从站日志**（UTF-8 归档） |
| 界面显示 12.34 s、E2–E6 错误态显示 | **Human GUI 观察/截图 + Human 最终确认** |
| 进程已退出 | **Human 提供的事后只读检查**（非 Agent 实测） |
| 仓库/EXE 指纹、本轮文件改动 | **本轮 Agent 只读核验** |
| V2 4/4、V3 8/8 | **历史归档证据**（R3 轮实测日志） |

---

## 最终输出

```text
C2.2 Virtual Serial Acceptance = HUMAN ACCEPTED / CLOSED
C2 Real Hardware = PENDING
T035-C Overall = NOT YET CLOSED
```

**STOP — RETURN TO HUMAN / CHATGPT REVIEW。**
