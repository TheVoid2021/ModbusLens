# ModbusLens — 工业通信智能诊断平台

ModbusLens 是一个用于工业现场 **Modbus RTU 通信监听、分析与诊断** 的 C++20 / Qt6 桌面软件：把难以阅读的原始通信，转成可解释的事务、统计与自然语言诊断。它同时是一份开发过程全程可追溯的秋招软件工程项目。

## Why ModbusLens

Modbus RTU 通信故障（无响应、CRC 错误、异常码）很难从原始字节快速定位和解释。ModbusLens 用一个共享的确定性分析核心，把三类数据源归一成同一套 **事务 → 统计 → 诊断** 视图，再以可选的 LLM 层做只读的自然语言解释——**核心诊断永远不依赖 LLM**。

## Features

- **Simulator Mode**：内置虚拟从站，零硬件产生确定性的 Modbus 流量（黄金演示：4 笔事务）。
- **Replay Mode**：加载 `.mlog` 历史日志离线重新分析（便于开发、测试、演示与复盘）。
- **Serial Mode**：通过真实串口（USB-RS485）连接 Modbus RTU 设备，单次读取保持寄存器并分析。
- **确定性事务分析**：CRC 校验、超时、异常码、成功率的完整统计口径三模式一致。
- **Baseline Diagnosis**：确定性规则基线诊断（无需 API Key、无需网络即可输出结构化发现与建议检查项）。
- **AI Diagnosis**：一键请求 LLM 对已确定事实做自然语言解释。
- **只读 Agent**：用户自由提问，模型按需调用三个只读工具读取确定性事实后回答。

## Architecture

```text
Transport (Simulator / Replay / Serial)
        ↓
deterministic Modbus Core（协议解析 / 事务分析 / 统计 / 规则诊断，Zero Qt）
        ↓
App / Controller 层（QML ↔ C++ 边界）
        ↓
QML UI（统计卡 / 事务列表 / 诊断面板）
        ↑
即可选：AI Diagnosis / read-only Agent（ModelScope · Qwen）
```

核心原则：**AI is interpreter, not detector** —— CRC 正确性、TransactionStatus、Timeout、异常码、统计与延时全部由 deterministic Core 决定；LLM 只解释，不产生协议事实。

## AI vs Agent

| | AI Diagnosis（Ask AI） | Agent（Ask Agent） |
| --- | --- | --- |
| 形态 | 一次请求，解释当前 deterministic facts | 用户自由提问 + 本地只读工具查询 |
| 工具 | 无 | 3 个 read-only tools（原生 tool calling） |
| 运行时 | 简单单发 | 有界 FSM（最多 3 轮工具 / 6 次工具调用） |

三个只读工具：

- `get_session_summary()` —— 当前批次确定性统计摘要
- `get_recent_anomalies()` —— 最近 20 条异常（Exception/CRC/Timeout/ProtocolError）
- `get_transaction_detail(transaction_number)` —— 指定事务的确定性详情

## Safety / Authority

LLM（AI 与 Agent 同规则）**不能**：修改串口设置、写寄存器、重发请求、修改文件、控制设备、改写任何 deterministic 事实。写能力在类型层面不存在（架构强制，非约定）。工具调用全部经过白名单、参数校验与预算硬上限；多轮回答受 batch revision 与 run generation 双重时效保护。

## Demo

黄金演示批次（Simulator 与 Replay `demo_v1.mlog` 同口径）：

- 4 transactions：1 Success · 1 Exception 0x02 · 1 CRC Error · 1 Timeout · 0 Protocol Error
- Success Rate = **25%** · Avg Success Latency = **25 ms**

三步演示：运行演示批次 → 运行基线诊断 → （可选）Ask AI / Ask Agent。

## Build & Run

环境要求见 [docs/ENVIRONMENT.md](docs/ENVIRONMENT.md)。

```bash
cmake --preset debug-local      # 或 debug（Qt 位于系统默认位置）
cmake --build --preset debug-local
ctest --preset debug-local
./build/debug/modbuslens.exe
```

独立部署（无需 Qt 开发环境）：

```bash
scripts\deploy_windows.bat    # 产出 build/deploy/ModbusLens.exe + 全部运行时依赖
```

## ModelScope configuration

API Key 是**可选的**增强配置：设置环境变量 `MODELSCOPE_API_KEY`（可选 `MODBUSLENS_MODELSCOPE_MODEL`）后启用 AI/Agent。无 Key 时 Simulator / Replay / Serial / 统计 / Baseline Diagnosis 全部照常工作。

> 不要把真实 Key 写入仓库文档或示例值。

## Testing

- 自动测试：ctest **23/23**（协议/CRC/事务/统计/回放/串口/诊断/AI 客户端/Agent 工具/Agent Runtime/UI 桥接/集成），全部通过 **localhost fake provider**，零真实网络、零 quota 消耗。
- QML 加载 smoke、独立部署 + minimal-PATH 冒烟均纳入验证链。
- 真实 Provider 验证为**一次性历史证据**（native tool calling 全链、同题 Live re-validation），不构成生产 SLA 或工业认证。

## Limitations

- v1 聚焦 FC03（读保持寄存器）；无 FC06/FC10 等写功能。
- Agent 详情不含 FC03 startAddress/quantity —— 可确定 `0x02 = Illegal Data Address` 并建议核对寄存器映射，但不能回答"具体哪个寄存器地址有问题"。
- 无 write tools、无 RAG/MCP/multi-agent、无对话历史。
- API Key 采用桌面级进程环境配置（portfolio-scale 设计，非多租户产品）。

## Project History / Engineering Notes

- 项目状态：见 [docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md)
- 任务档案 / Issue / ADR / 每日 devlog：见 [docs/](docs/)
- 面试素材：见 [docs/INTERVIEW_NOTES.md](docs/INTERVIEW_NOTES.md)