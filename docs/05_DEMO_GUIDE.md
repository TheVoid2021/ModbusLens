# 05 — 演示指南（Demo Guide）

> 状态：**T013 重写版**（与当前真实产品一致；§7 保留历史规划稿存档）。
> 演示是每轮面试/评审的核心环节：**先 Simulator 保底，再 Replay 加分，最后（可选）Serial 说明**。所有步骤都不要求真实硬件。

## 1. 演示总原则

- **30 秒内开始**：`ctest --preset debug-local` 全绿 → 启动 `modbuslens.exe` → 运行演示批次。
- **一条主线**：流量 → 事务 → 统计 → 诊断 →（可选）解释，三种模式共享同一确定性核心。
- **先彩排后真机**：任何真实硬件演示之前必须先用 Simulator 完整走一遍。
- **演示=验收**：关键口径对应 04_TEST_STRATEGY 的自动测试（ctest 23/23）。

## 2. 最终 3~5 分钟面试 Demo 主线（10 步）

1. **启动** ModbusLens（无 API Key 也能完整演示到第 6 步）。
2. **Simulator golden batch**：点击「运行演示批次」——固定 4 事务：1 Success（25 ms）/ 1 Exception 0x02（18 ms）/ 1 CRC Error（17 ms）/ 1 Timeout（1000 ms）。
3. **Dashboard**：讲解统计口径 —— Observed=4 / Completed=4 / Pending=0；Success=1 / Exception=1 / CRC=1 / Timeout=1 / Protocol=0；Success Rate=**25%**；Avg Success Latency=**25 ms**。
4. **Run Baseline Diagnosis**：演示**无 Key、无 LLM 时核心诊断仍可用**——"AI is interpreter, not detector" 的现场证明（发现 3 类异常 + 建议检查项，均为确定性输出）。
5. **Replay demo_v1.mlog**：加载回放文件，展示同一通信证据可重复分析（统计与 Simulator 完全同口径）。
6. **Serial explanation**：展示串口控制面板（串口枚举 / 波特率 / 起始地址 / 寄存器数量 / 读取保持寄存器），说明真实场景经 USB-RS485 接 Modbus RTU 设备（不要求现场硬件）。
7. **Ask AI**（需配置 Key）：deterministic facts → one-shot 自然语言解释。
8. **Ask Agent**（需配置 Key）：输入"本批次主要有什么异常？"→ question → native tool calling → 只读工具 → final answer（展示真实工具路径）。
9. **权限边界**：明确 Agent **不能** 改串口设置、写寄存器、重发请求、控制设备（写能力在类型层面不存在）。
10. **（可选 20~30s）ISSUE-007 故事**：真实 Live 多步诊断 → tool budget fail closed → RCA → total-call budget 3→6 + planning discipline → 同题真实 re-validation PASS。

**演示纪律**：Audit/Implementation 阶段不得为 Demo 消耗真实 quota；真实 Live 已有一份历史证据可直接引用。

## 3. Offline Demo Fallback

无 API Key / 无额度 / 无网络 / 无硬件时，仍可完整展示：启动 → Simulator golden（2~3）→ Baseline（4）→ Replay（5）→ Serial 说明（6）。这样主线 60% 不依赖任何外部条件。

## 4. 演示前检查清单（Checklist）

- [ ] `ctest --preset debug-local` 23/23 全绿
- [ ] clean build 0 警告
- [ ] `scripts\deploy_windows.bat` 后 minimal-PATH 冒烟 PASS
- [ ] Simulator golden 批次数字与本文档完全一致（4/4/0 · 1/1/1/1/0 · 25% · 25 ms）
- [ ] Demo 界面不出现任何 API Key / Authorization / 绝对路径 / 调试信息
- [ ] 窗口在 1000x700 与常规大尺寸各走一遍

## 5. Screenshot 状态准备（供用户自行截取 3~5 张）

| 状态 | 前置动作 | 应显示 | 不应显示 |
| --- | --- | --- | --- |
| A. Simulator Dashboard | 运行演示批次 | 四事务列表 + 9 项统计卡 | — |
| B. Baseline Diagnosis | A 后运行基线诊断 | 诊断结果 + 建议检查 | 任何 AI 正文 |
| C. Agent 问答 | （配置 Key）输入问题并询问 | 问题 + 最终回答（PlainText） | 工具调用/调试信息 |
| D. Replay | 加载 demo_v1.mlog | 模式=回放、来源 demo_v1.mlog、同口径统计 | — |
| E. Serial controls | 视图滚动至串口面板 | 串口/波特率/地址/数量/读取按钮 | 真实串口号（无硬件时为空） |

禁止出现：API key、Authorization、个人绝对路径、账户状态、临时日志、调试 UI。

## 6. 权限红线（全演示过程）

ModbusLens 的 Agent/AI 只读；演示过程中**绝不下发任何写操作**；主站请求只来自内置演示或外部工具。

## 7. 历史规划稿 v0.1（存档，不作演示依据）

> 以下为项目初期的规划设想（多从站轮询、旁路监听、时间轴播放等），与当前真实产品不一致，存档备查：

<details>
<summary>展开历史规划稿摘要</summary>

原 v0.1 Demo A/B/C 设想：1 主站轮询 + 3 从站场景与异常注入、Replay 时间轴拖动、双 USB-RS485 旁路监听、9600-8E1 等。真实产品为：Simulator 4 事务黄金演示、Replay 批处理式离线回放（无时间轴）、Serial 8N1 单次 FC03 读（可配置波特率）。以 §2 为准。

</details>