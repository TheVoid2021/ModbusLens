# 05 — CAPABILITIES AND LIMITATIONS（能力与边界的五档分类）

> 用途：**防止在面试或简历中夸大能力**。本文件把当前产品真相严格分档，每一档都给出证据锚点。
> 证据分级：`[V-nnn]` = VERIFIED；`[C-nn]` = CONFLICT；`[U-nn]` = UNVERIFIED。
>
> **五档定义**：
> - **A. IMPLEMENTED + VERIFIED** —— 已实现，且有自动化验证（+ 适用时的人工验收）。
> - **B. IMPLEMENTED BUT NOT REAL-HARDWARE VERIFIED** —— 已实现且有 harness/seam 级验证，
>   但**没有真实 Modbus 设备的验证证据**。
> - **C. OPTIONAL / PROVIDER-DEPENDENT** —— 依赖外部 provider 配置；**不可用时核心不受影响**。
> - **D. DEFERRED** —— 有档案的**显式推迟**（不是做不到，是当前不做）。
> - **E. NOT IMPLEMENTED** —— **从未实现**（不得与 DEFERRED 混淆）。

---

## 1. 必答的十项边界判定（面试前必须逐条记住）

| # | 问题 | 判定 | 依据 | |
| --- | --- | --- | --- | --- |
| 1 | 是否验证过**真实物理 Modbus 硬件**？ | **否 —— REAL MODBUS HARDWARE = NOT VERIFIED** | `README.md:146`、`docs/04_TEST_STRATEGY.md` 文首 CURRENT STATE、T027 §45.8/§119.18 | `[V-300]` |
| 2 | 是否验证过**真实串口写**（写寄存器到真实设备）？ | **否**（写路径只有自动化 + harness/seam 验证） | `README.md` §6「真实硬件写验证不在当前证据范围内」 | `[V-301]` |
| 2b | 那么**真实串口硬件到底用过没有**？ | **用过，但只验证了"适配器存在性/热拔"，没有做任何 Modbus I/O** | `--serial-hotplug-probe` **2026-09-23 实机验收 PASS**；打开后 `ioPerformedAfterOpen=0` | `[V-302]` |
| 3 | AI provider 是否**必需**？ | **否**——无 Key / 断网时 Simulator / Replay / Serial、统计、基线诊断、手册导入**完整可用** | `docs/01_REQUIREMENTS.md` FR-AG-03；`README.md` §8/§11 | `[V-303]` |
| 4 | Agent 是否有**写权限**？ | **否**——Agent 工具层**没有任何写 API**；写能力在类型层面不存在 | `ADR002` D-A1；`grep -rniE "write_coil\|write_register\|set_register\|device_write" src/ui/agent/` → **0 命中** | `[V-304]` |
| 5 | 是否存在 **Modbus TCP**？ | **不存在** | `grep -rniE "mbap\|modbus.?tcp\|transactionid" src/ tests/` → **0 命中**；`README.md` §11 列为"未实现" | `[V-305]` |
| 6 | 是否有 **installer**？ | **否**——只有便携 ZIP；仅存在于打包器 README 的否定陈述中 | `scripts/make_package.py:85` "No installer" | `[V-306]` |
| 7 | 是否有**代码签名**？ | **否** | `scripts/make_package.py:96` "not code-signed"；T027 §119.18 DEFERRED 列 code signing | `[V-307]` |
| 8 | 支持哪些**运行时模式**？ | **Simulator / Replay / Serial 三种** | `README.md` §5；`docs/02_ARCHITECTURE.md` §3 | `[V-308]` |
| 9 | **功能码边界**在哪？ | 一等支持 = **FC03 读**（主动 + 被动）、**FC06 / Function 0x10 写**（被动分析 + 用户确认式主动下发）；FC04/custom 在**解码资格**上一致对待；其余功能码 = **显式 Unsupported**（绝不伪装成错误） | `README.md` §11；`docs/11_V2_UPGRADE_PLAN.md` §1；T015/T023/T024 | `[V-309]` |
| 10 | **register map / 工程单位层**是否存在？ | **存在**（M11 32-bit 解码 + M12 设备档案 register map/scale/offset/unit）。**但**异常码 0x02 的**诊断路径**仍只建议"核对地址映射"，**不指认具体寄存器语义** | `src/core/analysis/RegisterDecode.h`、`src/core/profile/DeviceProfile.h`；**与 `README.md` §11 冲突，见 `[C-03]`** | `[C-03]` |

---

## 2. A 档 —— IMPLEMENTED + VERIFIED

### A1. 协议核心（Zero-Qt 静态库 `modbuslens_core`）
- CRC-16/MODBUS **按位实现**（`0x8005` / 反射 `0xA001` / init `0xFFFF` / 低字节先上线），KAT 对拍
  （`"123456789"→0x4B37`；`01 03 00 00 00 01`→数值 `0x0A84` / 线上 `84 0A`）。`[V-310]`
- RTU 帧模型（`{address, functionCode, data}`，**不存 CRC**）+ wire codec
  （`variant` 错误模型：`FrameTooShort` / `CrcMismatch`）。`[V-311]`
- FC03 语义（quantity 1..125、`byteCount = 2N`、异常 `0x83` + 单字节）。`[V-312]`
- FC06 被动语义（exact-echo matcher、异常 `0x86`）。`[V-313]`
- Function 0x10 被动语义（quantity 1..123、`byteCount = 2×N`、正常响应 = 起始地址 + 写入数量）。`[V-314]`
- generic exception matcher（含 `(fn & 0x80) == 0` 前置守卫）。`[V-315]`
- 证据：`crc` / `frame` / `codec` / `f03` / `fc06_active` / `fc16_active` / `passive` 测试目标。

### A2. 事务分析模型
- `TransactionStatus` **7 个枚举成员**（Pending / Success / Exception / CrcError / Timeout / ProtocolError /
  ExpectedNoResponse）。`[V-316]`
- **正交 Issue 两轴**：响应侧 `TransactionIssueCode`（**10 个枚举成员，含 1 个防御哨兵** ⇒ 业务值 9 个）
  + 请求侧 `TransactionRequestIssueCode`（4 个）⇒ **枚举口径 14 / 业务口径 13**。`[V-317]`
- 生产不变量：`ProtocolError ⇒ issue.has_value()`（**只在 `analyzeFunction03Transaction()` 生产输出中成立**）。`[V-318]`
- 表达能力示例：`Success + InvalidRequestByteCount + InvalidRequestLength`、
  `ExpectedNoResponse + InvalidBroadcastFunction`。`[V-319]`
- 证据：`transaction` / `statistics` / `passive` 测试目标。

### A3. 统计
- `observed = pending + completed`；`completed` 含广播、**不含 Pending**；
  `successRate = success / (completed − expectedNoResponse)`，分母 0 ⇒ **`nullopt`**（不是 0%，不崩溃）；
  `averageSuccessLatencyMs` **只计 Success**。`[V-320]`
- requestIssues **不是第八状态**、**不产生统计维度**（正交性由 `STAT-B09/B10`、`DIAG-A14` 锁定）。`[V-321]`

### A4. 确定性基线诊断（不依赖 LLM）
- `RuleBasedDiagnosis`：finding 为**纯 enum** + 建议检查项（12 种 action）；
  **"可能原因"只出现在 possible checks，绝不包装成事实**；不存在 health score。`[V-322]`
- finding 固定顺序：Protocol → CRC → Timeout → Exception(asc) → Pending → ExpectedNoResponse →
  RequestIssue → Healthy/NoData。`[V-323]`
- 证据：`diagnosis` 测试目标（`DIAG-A01~A15`）。

### A5. 三种数据源
| 模式 | 能力 | 证据 | |
| --- | --- | --- | --- |
| **Simulator** | 确定性虚拟从站 + **故障注入四模式**（`None` / `DropResponse` / `CorruptCrc` / `ArtificialDelay`）；黄金演示批次 = 4 笔（Success 25 ms / Exception 0x02 18 ms / CRC 17 ms / Timeout 1000 ms） | `simulator` / `fault` / `simulator_integration` / `fault_integration` | `[V-324]` |
| **Replay** | `.mlog` v1 批处理重析；**per-record** 化（语法失败整文件 fail；坏请求/unsupported 按记录保留并显式披露）；统计与 Simulator golden **严格相等** | `replay_log` / `replay_analysis` / `passive` | `[V-325]` |
| **Serial** | 主动 FC03 单次读（8N1、一次一笔 outstanding、Busy 拒绝、chunk 累积 + candidate 收口、oversized 不截断）；FC06/FC16 用户确认式写；timeout 双路语义；stale completion guard | `serial` / `serial_adapter` / `write_*` / `qml_production_write_check` | `[V-326]` |

### A6. 主动写安全路径（FC06 / FC16）
- prepare/validate（地址、数量、值域）→ 写前快照 + 原值回显 → **确认对话框**
  （目标从站 / 地址 / 数量 / 值清单 / PDU / RTU 帧）→ 用户点「确认写入」→ 经已打开的 Serial 会话派发。`[V-327]`
- **连接是前提**（未连接串口时写入不可用；未通过校验时预览与写入按钮保持禁用）。`[V-328]`
- **确认与派发分离**（确认对话框出现前不产生任何下发；rapid Enter 无法逃逸确认）。`[V-329]`
- **PREVIEW == WIRE**（预览字节来自同一个 production encoder，`pv1–pv4` 逐字节锁定）。`[V-330]`
- **失败不得呈现为成功**：写超时覆盖 0x06 **与** 0x10（R17 断言"恰一个 Timeout + 冻结文案 + 无 Success"）。`[V-331]`
- **Agent 无关**：Agent 无写工具、无写 API。
- 证据：`write_prepare` / `write_dispatch` / `write_encoder` / `fc06_active` / `fc16_active` /
  `qml_write_foundation_check(_windows)` / `qml_production_write_check`。

### A7. UI（6 工作区 + 设计系统）
- 工作区 = **事务 / 总览 / 通信 / 回放 / 诊断 / 设备**；设计系统 token + 可复用组件；
  最小窗口 **1000×700**（`Main.qml minimumWidth/Height`），验收尺寸 1024×720。`[V-332]`
- 焦点与可访问性：可见焦点环（选中态与 focused 态可区分）、Tab 顺序（M9-F F1 验收）。`[V-333]`
- QML 门禁族：`qml_smoke` / `qml_nav_check` / `qml_geometry_check` / `qml_focus_check` + 各业务 oracle
  （含 `_windows` 真实平台变体），全部带 `FAIL_REGULAR_EXPRESSION`。`[V-334]`
- 证据：27 张 UI 审计证据 PNG + 4 份 run log（`docs/evidence/ui-release-audit/`）。`[V-335]`

### A8. M11 寄存器读数解码
- 7 种解码类型（Hex / Binary / UInt16 / Int16 / UInt32 / Int32 / Float32）+ 寄存器内**字节序** +
  **字序**（HighWordFirst AB CD 默认 / LowWordFirst CD AB）+ 滑动窗口 alignment。
- **raw 与 derived 双列分离**（原始十六进制 / 原始十进制**恒显**，解析结果随配置变化）。`[V-336]`
- 5 个 decode status（Ok + 4 个 decode/configuration failure states），与 wire result **正交**。`[V-337]`
- 证据：`register_decode`（50）/ `ui_bridge`（95，READ-D1..D11）/ `qml_read_result_check` +
  两个 demo harness（`qml_read_result_demo` / `qml_read_result_demo32`）。

### A9. M12 设备档案
- 领域模型（`RegisterEntry{address, type, scale, offset, unit, …}`）+ 冻结公式
  `decodedValue * scale + offset` + JSON 持久化（schema version 1）+ 路径安全硬化。`[V-338]`
- **按 read function code 划分寄存器空间**（`RegisterSpaceKey`）；**同一 read function 内拒绝跨条目地址范围重叠**；
  **custom read function codes 保持一等公民**（非 0x03 硬编码）。`[V-339]`
- Register Map 编辑器（Add / Edit / Delete + dirty）、Active Profile 选择器、
  Read Result **语义叠加**（解码值 + scale/offset/unit + 单位）。`[V-340]`
- 证据：`device_profile` / `profile_controller` / `profile_semantic` / `register_decode` /
  `qml_profile_editor_check` / `qml_register_map_check` / `qml_active_profile_check` /
  `qml_profile_semantic_check`。

### A10. M12 手册智能
- **手册导入**：TXT / Markdown / **PDF / DOCX**——PDFium（pinned）+ libzip + zlib（static），
  **离线物化 + 配置期 hash 断言**。**本地**抽取，不依赖 provider。`[V-341]`
- **AI 提取候选**：evidence-backed candidate（value / evidence / 来源页节 / confidence），
  **云端同意门**（consent-gated orchestration）。`[V-342]`
- **人工审核权威**：Accept / Edit / Reject；**只有用户确认后才进入 verified Device Profile**；
  支持 Human-authored candidate value edit（C3-H8）。`[V-343]`
- **引用校验式手册问答**：**app-resolved citations**（provider 只能选 id，offset/hash 由 app 回填，
  未知 id fail-closed）；结构化输出契约（显式 `enable_thinking=false` + `max_tokens` + `qa36`）。`[V-344]`
- 手册库：冷启动 hydrate、安全删除、预览。`[V-345]`
- 证据：`manual_import` / `manual_import_pdf_docx` / `manual_extraction` / `manual_delete` /
  `candidate_extraction` / `candidate_adapter` / `candidate_orchestration` / `candidate_review` /
  `manual_qa` / `manualqa` QML 门禁族 / `c1b_dependency_materializer`。

### A11. AI 解释 + 只读 Agent（**能力本身**在 A 档；**可用性**属 C 档）
- one-shot 解释：结构化事实 → bounded prompt（**最多 20 条事务明细**）→ 单次回答 → PlainText 渲染。`[V-346]`
- Agent：**恰好 3 个只读工具**（`get_session_summary` / `get_recent_anomalies` / `get_transaction_detail`）；
  immutable snapshot；白名单 dispatcher；**validate-then-execute、零部分执行**；
  **预算 = 3 rounds / 6 calls**；**双层 stale guard**（`capturedBatchRevision == currentBatchRevision`
  **AND** `runGeneration == currentAgentGeneration`）。`[V-347]`
- 证据：`ai_client`（B01~B25）/ `agent_tools`（A01~A13）/ `agent_runtime`（B01~B23）/
  `agent_integration`（UI-AG01~AG22），全部使用 **本地 fake Chat Completions server（零真实网络）**。

### A12. 发布工程（v2.0.0 发布链 = **仓库记录的 COMPLETE / INDEPENDENTLY VERIFIED**）

> **证据等级声明（T032-A1 追加，务必按此口径表述）**：
> - **VERIFIED（仓库事实）**：本仓库的档案与 Git 对象记录了 v2.0.0 发布/公开流程**已完成**，
>   并保留了本地 release/tag 证据（tag 对象、资产大小与 SHA-256、R5G 验证记录、H1–H8 人工验收记录）。
>   这些是**当时执行并留档**的历史证据。`[V-561]`
> - **UNVERIFIED（当前远端事实）**：**当前** GitHub 页面状态、远端 tag ref 是否存在、
>   Social Preview / Description / Topics 是否已应用，**在 T032-A / A1 期间均未做实时核验**（离线环境无法判定）——见 `[U-10]`。`[U-10]`
> - **禁止的表述**：不得把"仓库记录了发布完成"升级为"当前远端状态已复核"；
>   也不得反过来把真实的历史留档**降级**为"未发布"。`[V-562]`

- candidate 契约（从零重建 + 逐文件 SHA-256 manifest + Release/Debug 各自独立树）。`[V-348]`
- fail-fast 打包器（9 类异常全部 `SystemExit(1)`）+ freshness oracle。`[V-349]`
- deployment 门（sanitized environment + manifest/runtime hash 校验）。`[V-350]`
- **v2.0.0 发布链（仓库留档）**：tag `v2.0.0`（annotated `bdabd6fd…` → commit `b18e378f…`）；
  资产 `ModbusLens-2.0.0-windows-x64.zip` = **35,814,735 B** /
  SHA-256 `6db918f35b62b3bb7638b7e0660cb74d9137d22aada1ca512513bd0a2654449d`。`[V-351]`
- **公开资产独立验证（R5G，仓库留档）**：公开下载 35,814,735 B 并算 SHA-256 与验收档案比对——
  **不解压、不重打包**，字节级一致。`[V-352]`
- **人工验收 H1–H8 PASS**（启动 / 解压 / Simulator 演示 / Replay 样例 / 三模式边界 /
  内容泄漏检查 / 手册问答收回 / 长结果滚动）。`[V-353]`
- 证据：`test_make_canonical_package.py`（PKG-01..26）、`test_make_package_freshness.py`（7 项）、
  `deployment_startup_check`、`c1b_dependency_materializer`。

### A13. 测试与验证基建
- **CTest 70 个目标**；Release 配置 **70/70 PASS，exit 0**（2026-10-07 release verification snapshot）。`[V-354]`
- 手写断言 harness（Qt Test）+ QSignalSpy；fake HTTP server + **fake serial transport**
  （`tests/fake_serial_transport.*`）。`[V-355]`
- **负向对照纪律**：真实 mutate → 观察目标测试 **RED** → **精确逆向 patch** 还原 → 基线复绿
  （**没有真实 RED 不算 evidence**）。`[V-356]`
- QML 诊断回归：全部六个门禁带 `FAIL_REGULAR_EXPRESSION`；
  一个**真实平台**变体（`qml_write_foundation_check_windows`）。`[V-357]`
- Replay 性能基准（可复现）：`scripts/bench_replay/`（确定性混合样本生成器 + 分段计时 + 多轮中位）。`[V-358]`

---

## 3. B 档 —— IMPLEMENTED BUT NOT REAL-HARDWARE VERIFIED

| 项 | 已实现 + 已自动/harness 验证 | **未验证的部分** | |
| --- | --- | --- | --- |
| Serial 读（FC03 单笔事务） | `test_serial_session`（a01…a16）/ `test_serial_adapter`（i09–i13）/ `qml_production_write_check` | **从未与真实 Modbus 从站设备完成过一笔真实事务** | `[V-359]` |
| Serial 写（FC06 / FC16 确认式） | `write_prepare` / `write_dispatch` / `write_encoder` / `fc06_active` / `fc16_active` / R15–R17 | **真实硬件写验证不在当前证据范围内** | `[V-360]` |
| Serial 适配器存在性 / 热拔检测 | `--serial-hotplug-probe` **2026-09-23 实机验收 PASS**（真实 USB 串口硬件；与生产 adapter 相同配置打开、打开后**不做任何 I/O**、`ioPerformedAfterOpen=0`） | **不是**远端 Modbus 存活检测（不寻址任何从站、不发任何帧；从站沉默仍保持端口存在） | `[V-361]` |
| 多设备 / 多从站工业现场泛化 | —— | **NOT VERIFIED**（T027 §119.8 明确列为未验证项） | `[V-362]` |
| 工业现场环境（EMI / 长线 / 接地 / 波特率偏差） | —— | 诊断规则只给 **possible checks**，**绝不宣称物理根因** | `[V-363]` |

**一句话口径（面试直接用）**：
> "串口读/写路径**都实现了**，有自动化与 harness/seam 级验证，还用真实 USB 串口硬件做过**适配器热拔检测**的实机验收；
> 但**没有真实 Modbus 从站设备的验证证据**，所以我在 README 和简历里都不写'已在现场设备上验证'。"

`[V-364]`

---

## 4. C 档 —— OPTIONAL / PROVIDER-DEPENDENT

| 能力 | 依赖 | 不可用时的行为 | |
| --- | --- | --- | --- |
| **AI 解释**（one-shot） | `MODELSCOPE_API_KEY` + `MODBUSLENS_MODELSCOPE_MODEL`（BYOK，进程环境变量） | 该功能不可用；三模式 + 统计 + 基线诊断**完整可用** | `[V-365]` |
| **只读 Agent 问答** | 同上 | 同上 | `[V-366]` |
| **手册 AI 提取候选**（C2） | provider + **用户同意门**（云端处理同意） | 手册**导入/本地抽取仍可用**；候选提取不可用 | `[V-367]` |
| **引用校验式手册问答**（M12-D） | provider | 不可用；产品其余部分不受影响 | `[V-368]` |
| **手册导入（TXT/MD/PDF/DOCX）** | **不依赖 provider**（本地 PDFium/libzip） | **始终可用** | `[V-369]` |

- provider 配置是**桌面级进程环境模型**（BYOK），**非多租户产品**。`[V-370]`
- **Live provider 证据为一次性历史验证**，不构成生产 SLA 或工业认证。`[V-371]`
- **AI 输出一律 PlainText**（不启用 RichText/Markdown/HTML 渲染）——安全边界保持。`[V-372]`
- **Agent 永远只读（3 个工具）**；无 RAG / MCP / 多智能体 / 持久会话历史。`[V-373]`
- **provider families 与 provider-selection UI** = DEFERRED（见 D 档）。`[V-374]`

---

## 5. D 档 —— DEFERRED（有档案的显式推迟）

来源：T027 §119.18（DONE / DEFERRED / NOT IMPLEMENTED / ROADMAP），逐项核对。

| 项 | 备注 | |
| --- | --- | --- |
| **代码签名** | 便携包为未签名 ZIP | `[V-375]` |
| **installer** | 只有便携 ZIP | `[V-376]` |
| **byte-reproducible ZIP** | 已验证**字节一致性**（公开下载比对），但**未追求字节级可重建** | `[V-377]` |
| StatisticsOverview cleanup、AppBar logo | M9-A 遗留清理项 | `[V-378]` |
| M12-A DEFER 项 | 见 T027 §19 的 P0/P1/DEFER 重分类 | `[V-379]` |
| NCR / multi-manual RAG / whole-library RAG / conversation memory / durable history / cross-session state | 检索增强与跨会话能力 | `[V-380]` |
| provider families 与 provider-selection UI | 当前仅 ModelScope 一类 | `[V-381]` |
| M12-D mutation 能力、M12-C reopening | 问答只回答，不改手册/档案 | `[V-382]` |
| **RUI-11…RUI-16**（T029 新增 P2） | 留白密度 / Replay 空态提示 / Agent 无 provider 提示 / contentHash 展示 / 演示数据命名 / header 版本号 | `[V-383]` |
| **ISSUE-008 / ISSUE-009** | `MONITORING / NON-BLOCKING`——**无 exact RCA，不编造** | `[V-384]` |
| ISSUE-015 的三条防复发动作 | 增强 `deploy_is_current()` / 增加 `windeployqt --version` 前置探测 / R17 指纹固化 | `[V-385]` |
| ISSUE-014/017 的文档整理 | 两份 Issue 记录同一现象，是否合并留待整理（**只增不改**） | `[V-386]` |
| `docs/06` 的未来多语言迁移 | 若需第二种语言：**先立 ADR，再迁移** | `[V-387]` |

---

## 6. E 档 —— NOT IMPLEMENTED（从未实现，**勿与 DEFERRED 混淆**）

| 项 | 证据 | |
| --- | --- | --- |
| **Modbus TCP（MBAP）** | `grep -rniE "mbap\|modbus.?tcp\|transactionid" src/ tests/` → **0 命中**；`docs/01_REQUIREMENTS.md` FR-CORE-02 状态仍是 `Proposed` | `[V-388]` |
| **Modbus ASCII** | 无实现（协议知识库里作为背景知识存在） | `[V-389]` |
| PLC / 多设备工业现场验证 | T027 §119.18「NOT IMPLEMENTED」列 | `[V-390]` |
| 工业认证 | 同上 | `[V-391]` |
| 真实硬件泛化验证 | 同上（**REAL MODBUS HARDWARE = NOT VERIFIED**） | `[V-392]` |
| **t1.5 / t3.5 时序分析** | 只作为**协议知识**存在；Serial 帧定界按"期望响应长度 + 超时"，**无通用静默间隔扫描**；Replay v1 **无 per-byte/chunk 时间戳** | `[V-393]` |
| **UART parity / framing / break / overrun 明细** | adapter 只保留 `QSerialPort::errorString()` 一次性通用文本；`docs/09` §16 | `[V-394]` |
| **扫描件 PDF / OCR** | T027 §47.6 明确边界 | `[V-395]` |
| **数据库 / Docker / MCP / RAG / vector DB / multi-agent / OpenAI SDK / i18n 框架 / CI 流水线** | `docs/07` 第三章「明确不在本项目，不写进 resume」 | `[V-396]` |
| **Health score（0~100）/ 自动判 Healthy-Unhealthy** | `docs/09` §22 设计红线明确禁止 | `[V-397]` |
| **通用物理根因判定** | 诊断只给 observed + possible checks；`docs/09` §25 列为 non-goal | `[V-398]` |
| **FC03 以外的读功能码主动支持**（FC01/02/04） | 一等支持仅 FC03；FC04/custom 只在**解码资格**上被一致对待 | `[V-399]` |

---

## 7. 明确**不成立**的常见夸大表述（面试前自查）

| ❌ 不能说 | ✅ 应该说 |
| --- | --- |
| "已在真实 PLC / 现场设备上验证" | "串口读/写已实现并有 harness 级验证；**真实 Modbus 硬件未验证**" |
| "支持 Modbus TCP" | "**只做 RTU**；TCP 列为未实现" |
| "AI 智能诊断" | "**确定性规则诊断** + 可选的 AI **解释**（AI is interpreter, not detector）" |
| "Agent 可以帮我操作设备" | "Agent **只读**（3 个工具，无写 API）；写只能由用户在 UI 中显式确认后经 Serial 派发" |
| "支持全部 Modbus 功能码" | "一等支持 FC03 读、FC06/0x10 写；其余功能码**显式 Unsupported**，不伪装成故障" |
| "有安装程序" | "**未签名便携 ZIP**；无 installer、无代码签名" |
| "有 CI" | "验证链是**本机**的 CTest + QML 门禁 + deployment gate；**未接 CI**" |
| "采样率 / 时序分析能力" | "**无 t1.5/t3.5 时序分析**；帧定界按期望长度 + 超时" |
| "性能已优化" | "性能基准已建立、瓶颈已定位（解析占约三分之二），但**按 10 帧/秒的需求尚未优化**" |

`[V-400]`

---

## 8. 与既有文档的边界冲突（Human review 裁定结果）

> **本节在 T032-A1（2026-10-08）追加"处置结果"列，原始发现逐条保留。**

### `[C-03]` README §11「无 register map / 工程单位层」与实际能力冲突
- **`README.md` §11 原文（修正前）**："**无 register map / 工程单位层**：异常码 0x02 可指向'非法数据地址'并建议核对映射，
  但不指认具体寄存器语义。"
- **`docs/INTERVIEW_NOTES.md` F8 同族表述**："无 register map/工程单位语义层（0x02 只能指向'核对地址映射'）"
- **仓库实际**：`src/core/analysis/RegisterDecode.h` 实现 7 种解码类型（含 UInt32/Int32/Float32 + 字序）；
  `src/core/profile/DeviceProfile.h` 实现 `RegisterEntry` 的 `scale` / `offset` / `unit`
  与冻结公式 `decodedValue * scale + offset`（定义在 `DeviceProfile.h:118-126`）。
- **仍然成立的部分**：异常码 **0x02 的诊断路径**仍只建议"核对地址映射"，**不指认具体寄存器语义**。
- **性质**：**历史遗留表述**——该句在 T015 时点为真（当时确实没有），
  M11/M12 落地后**没有人回头改 README 的已知限制清单**（与 `docs/07`/`docs/11` 加过历史快照声明不同）。
- **T032-A 处置**：只记录，**不修改**（当时为纯事实提取轮）。
- **T032-A1 处置 = 已修正（Human Review 接受 C-03 为真实当前文档错误）**：
  `README.md` §11 该条目已改写为**限制优先**的准确陈述——"语义层只覆盖读数呈现，不进入诊断判定"，
  并明确：M11 解码与 M12 register map + **用户显式填写**的 scale/offset/unit **只作用于 Read Result 的语义呈现**；
  **诊断引擎不消费设备档案**（本轮源码实测：`grep -rn "DeviceProfile\|RegisterEntry\|profile" src/core/diagnosis/` → **0 命中**）；
  0x02 仍不指认具体寄存器语义；无设备模板库、无自动工程单位推断。
  **红线遵守**：未夸大能力、保留真实限制、未声称真机验证、未暗示 Modbus TCP、未暗示 OCR、未做无关改版。
- **未处置**：`docs/INTERVIEW_NOTES.md` F8 同族表述——**不在 T032-A1 授权文件集内**（任务 §6），待 Human 授权后整理。`[V-558]`

### `[C-04]` `docs/09_DIAGNOSTIC_COVERAGE_AUDIT.md` 的"六状态"与 ADR-003 的"七状态"冲突
- **`docs/09` §1 / §27 原文**："`TransactionStatus` 恰为上述六类，**无第七个状态**"
  （`TransactionAnalysis.h:14-21`；"T007 档案'不新增状态'的定案"）。
- **`ADR-003` + 现状**：第七状态 `ExpectedNoResponse` 于**同一天**（2026-09-13）
  由 Gate C 批准并实现。
- **性质**：**历史时点差异**——`docs/09` 写于 ADR-003 批准**之前**，两条记录**当时都为真**。
  差的是几小时，不是谁写错了。
- **T032-A / A1 处置**：只记录。`docs/09` 未加历史快照声明（与 `docs/07`/`docs/11` 不同），
  这是 T031-A 未覆盖到的一处；属 Human 可裁定的文档整理项（**非本阶段授权范围**）。

### `[C-01]` 项目时间范围（已在 `01_PROJECT_TIMELINE.md` 记录）
- `docs/11_PROJECT_FINAL_RETROSPECTIVE.md` §2/§11 与 `docs/12_RESUME_INTERVIEW_QA.md` §6 问题 27
  声称"2026-09-05 → 2026-09-14"；Git 实测 = **2026-09-05 → 2026-10-08**。
- **T032-A / A1 处置**：只记录（对外范围以 Git 实测为准）。`docs/11` 顶部已有历史快照声明；
  `docs/12` 不在本阶段授权集内。

### `[C-05]` 测试目标数 / ADR 数 / 任务档案数的历史口径（**已按 A/B/C 分类处置**）
**当前实测真值**：CTest 目标 = **70**；ADR 文件 = **3**；任务文档 = **30**。`[V-559]`

| 来源 | 分类 | 处置 |
| --- | --- | --- |
| `docs/07`（撰写于 2026-09-11：ctest 23/23、"ADR 2 份"） | **A** 当时正确的历史快照 | **不动**（顶部已有历史快照声明） |
| `docs/11`（2026-09-14：ctest 24） | **A** | **不动**（顶部已有历史快照声明） |
| `docs/11_V2_UPGRADE_PLAN` §3（2026-09-14："当前 24/24"） | **A** | **不动** |
| `docs/PROJECT_STATUS.md` 历史追加块（23/23、24/24） | **A** | **不动**（append-only，每块描述其当时快照；最新块已用 70） |
| `docs/BACKLOG.md` 时间线行（2026-09 各日 ctest 23/23） | **A** | **不动** |
| `docs/INTERVIEW_NOTES.md` F10（2026-10-07："2 份 ADR"、"25 份任务档案"） | **B** 当时即为错误 / 口径差异 | **保留于 Fact Pack**；不在 A1 授权集内，待 Human 授权追加勘误 |
| `docs/tasks/T028` §2E（2026-10-07："ADR 2 份"，而该 HEAD 上实测已有 3 份） | **B** 当时即为错误 | **保留于 Fact Pack**；`docs/tasks/` 属只增不改档案区，按任务 §3 允许以 Fact Pack 保留差异 |
| `docs/12_RESUME_INTERVIEW_QA.md` §1/§4.1/§5.1/§6(18)/§8（"24 个 CTest 目标"） | **C** 在用文档但已过期（头部自声明 v1 / 2026-09-14） | **本轮不改**：不在授权集内，且改写其"简历定稿"属内容决策（Phase B / Human 授权） |

**结论**：A 类占绝大多数（23/23、24/24 是**当时为真的快照**，不是错误，一律不动）；
B 类 3 处、C 类 1 处均**不在 T032-A1 授权文件集内**（§6）或属只增不改档案区，**本轮均未回写**；
**授权集内不存在需要数字更新的 C 类实例** ⇒ **未做任何全局替换**（无 `24→70` / `2→3` / `25→30` 批量改写）。
**对外统一口径** = **70 / 3 / 30**，并说明"70"是验证快照而非永恒常量。`[V-560]`