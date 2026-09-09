# 06 — UI Language Policy（用户界面语言政策）

> 状态：生效（2026-09-09 UI Localization Pass 起）
> LKGC 关联：`9e79558`（chore(ui): localize user-facing text to Simplified Chinese）

## 1. 背景

产品面向中文工业通信工程师与调试人员，三类数据源（Simulator / Replay / Serial）共用同一套 Dashboard、统计与诊断核心。2026-09-09 起的 **UI Localization Pass** 将全部用户可见文案切换为**简体中文为主**，同时保留工业/协议领域的标准英文缩写与实体名。本文档固化该语言政策，任何后续 UI 改动必须遵守。

## 2. 总原则

1. **简体中文为主**：用户操作、状态说明、诊断解释、错误提示、页面标题、统计含义一律使用简体中文。
2. **专业实体保留英文**：协议与工业领域的标准术语、缩写、品牌名、文件名保留英文原文，不做音译/意译。
3. **禁止冗余双语**：不写"循环冗余校验（CRC）"这类解释性双语，直接使用"CRC 错误"。
4. **展示层唯一翻译点**：Core（`modbuslens_core`）只产生 enum / 结构化事实，**绝不出用户文案**；所有中文文案在 UI/App 适配层（QML、Controller formatter、Adapter 消息）产生。
5. **判断永不依赖文案**：任何控制流判断必须使用 enum/结构（`TransactionStatus`、`DiagnosisActionCode`、`AiDiagnosisErrorCode` 等）；严禁 `if (statusText == "超时")` 之类的字符串比较。展示文案可以随时改，业务语义与测试不受影响。
6. **单一语言策略**：产品仅支持简体中文界面；**不引入** Qt Linguist / `.ts` / `.qm` / 运行时语言选择器（见 §6 取舍）。

## 3. 保留英文的实体清单（不翻译）

| 类别 | 保留示例 |
| --- | --- |
| 产品名 / 品牌 | ModbusLens、ModelScope、Qwen/* |
| 协议术语 | Modbus、Modbus RTU、RS485、CRC、FC03 |
| 数值/位模式 | 0x03 / 0x02、8N1、1..247、0..65535 |
| 硬件/端口 | COM*（COM3 等） |
| 文件/环境 | `.mlog`、`MODELSCOPE_API_KEY`、`MODBUSLENS_MODELSCOPE_MODEL` |
| 单位 | ms（如"超时 (ms)"、"%1 ms"） |
| 泛称缩写 | AI（如"AI 解释"、"AI 请求超时"） |

规则：上述实体在中文句子中直接嵌入（如"核对从站地址"、"设备异常 0x02：1"、"生成 AI 解释"）。

## 4. 已本地化的核心文案（局部摘录）

- 模式/来源：模拟器模式 / 确定性演示 / 回放模式 / 串口模式（来源标签如 `demo_v1.mlog`、`COM3 @ 9600` 保持原文——**文件名与端口名属于数据，不是文案**）。
- 状态：成功 / 异常 / CRC 错误 / 超时 / 协议错误 / 进行中。
- 基线诊断格式：`诊断结果：` + `- <finding>`（如 `设备异常 0x02：1`、`CRC 错误：1`、`无响应超时：1`）+ `建议检查：` + 12 条动作（等待当前通信完成 / 检查设备供电 / 核对从站地址 / 检查串口参数 / 检查 RS485 接线 / 检查接地与线路干扰 / 确认设备是否支持该功能码 / 核对寄存器地址表 / 核对请求参数 / 检查设备运行状态 / 查阅设备通信文档 / 检查请求与响应的协议一致性）。
- 错误前缀：最近一次 AI 请求失败：/ 回放解析错误（第 %1 行）：/ 回放分析错误（事务 %1）：/ 回放加载失败：/ 串口错误：/ 串口传输错误：。

## 5. AI 输出语言政策（T011 Part B 延伸）

- **事实通道**：发给 LLM 的 user prompt（total_transactions / statistics / baseline findings / transaction details）保持英文结构化标识——它是机器通道，不是用户可见 UI，不参与本地化。
- **输出约束**（system prompt）：`Return the explanation in concise Simplified Chinese.` + `Keep protocol terms in English as-is: Modbus, RTU, CRC, function codes, register addresses, exception codes.` + `Do not use Markdown formatting. Use plain text only.`；原有英文 authority 规则（authoritative / Do not recalculate / no root cause 等）**一字未改**。
- **渲染兜底**：QML 中 AI 解释一律 `Text.PlainText`——AI 输出中文化改变措辞，不改变安全边界。

## 6. 取舍记录（为什么不引入 Qt Linguist）

- 产品无多语言需求（目标用户单语种），`.ts/.qm`/`tr()` 运行时切换属于过度设计，增加构建与部署面。
- 单语言直写 `qsTr("中文")` 保留未来迁移余地（文案已集中在 QML 与适配层），届时引入 Linguist 无需触碰 Core。
- 若未来需要第二种语言：先立 ADR，再迁移；本政策即约束当前态。

## 7. 工程约束（本 pass 实际教训）

- **中文严禁经 `const char*` + `QLatin1String` 转换**：UTF-8 字节会按 Latin-1 逐字节映射产生乱码。中文一律 `QStringLiteral`（源文件 UTF-8 → UTF-16 编译期语义）。本 pass 已将 replay 错误短语从该组合结构性修正。
- 源文件保持 UTF-8（无 BOM）；MinGW/GCC 默认 input charset=UTF-8。
- 测试更新原则：仅 presentation 断言随文案同步更新，语义不变（如英文 `!contains("Healthy")` → 中文 `!contains("全部")`）；Core 测试从不因文案改动而变。

## 8. 验证记录

- **2026-09-09 Manual Localization Smoke = PASS（用户 9 项人工确认）**：三模式文案中文化；专业实体正确保留；Serial Controls / Statistics / Diagnosis / Recent Transactions 文案自然；Baseline 中文正确；Replay `demo_v1.mlog` 文件名不翻译；真实 ModelScope AI 解释以简体中文为主且术语保留得宜；输出保持 PlainText（无 Markdown/RichText）；中文化后 SplitView / Serial Controls / Statistics 无明显截断、重叠或功能回归。
- 自动化：clean 全量重建 126 targets 零警告；ctest **20/20**（含 qml_smoke）；`scripts/deploy_windows.bat`；minimal-PATH smoke PASS。
- verified code/config commit：**`9e79558`**（= Localization Pass 落地即 LKGC）。