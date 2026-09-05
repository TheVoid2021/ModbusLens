# BACKLOG — 任务清单与里程碑

> 规则：每个任务完成后更新（状态、依赖、优先级）；任务档案在 `docs/tasks/`。
> 状态取值：`Done` / `In Progress` / `Ready` / `Backlog`
> 优先级：`P0` 必须现在做 · `P1` 尽快 · `P2` 有余力再做

## 里程碑总览（T001.1 起按细粒度任务规划）

| ID | 里程碑 | 任务 | 状态 |
| --- | --- | --- | --- |
| M1 | 工程引导与文档体系 | T001, T001.1 | ✅ 完成 |
| M2 | Modbus 协议核心 | T002, T003, T004 | 🔄 进行中（T002 ✅ / T003 ✅ / T004 待启动） |
| M3 | 模拟与故障注入 | T005, T006 | ⬜ |
| M4 | 事务分析与界面 | T007, T008 | ⬜ |
| M5 | 回放与串口模式 | T009, T010 | ⬜ |
| M6 | AI 诊断与 Agent 工具 | T011, T012 | ⬜ |
| M7 | 收尾与演示 | T013 | ⬜ |

## 任务表

| ID | 标题 | 里程碑 | 优先 | 状态 | 依赖 | 说明 / 关键产出 |
| --- | --- | --- | --- | --- | --- | --- |
| T001 | 项目引导：骨架 + 文档体系 + 最小 Qt6 应用 | M1 | P0 | ✅ Done | — | 目录骨架、AGENTS 规约、全套文档、CMake Presets、offscreen 冒烟测试 |
| T001.1 | Bootstrap Documentation Cleanup | M1 | P0 | ✅ Done | T001 | 文档更名为 05_DEMO_GUIDE、preset 示例模板、ENVIRONMENT 重写、BACKLOG 细粒度拆分 |
| T002 | **Modbus CRC16** | M2 | P0 | ✅ Done | T001.1 | `modbuslens_core`（无 Qt 纯 C++20 静态库）+ `calculateModbusCrc(std::span<const std::uint8_t>)` 按位实现；6 测试（KAT/边界/敏感性/zero-remainder）RED→GREEN 全程留痕。按约定未做：查表优化、benchmark、fuzz、序列化 |
| T003 | **Modbus RTU Frame Model** | M2 | P0 | ✅ Done | T002 | 交付：`ModbusRtuFrame`（address/functionCode/data，C++20 value 语义，**不存 CRC**）+ `isExceptionResponse`；FRAME-T01~T04 全绿。**范围修订（T003 执行时确认）**：wire 编解码、CRC 校验集成、fuzz-lite **移出 T003**——编解码与 CRC 校验随 T004 codec 承接，fuzz 待 codec 存在后评估；t1.5/t3.5 时序归 T010 |
| T004 | **Modbus RTU Codec**（Part A Wire Codec + Part B 0x03 Codec） | M2 | P0 | In Progress — **Part A ✅ DONE**（实现+7 测试全绿）/ **Part B ⬜ NOT STARTED** | T003 | **Part A — RTU Wire Codec** ✅：`ModbusRtuCodec`（encode/decode + `variant<Frame, RtuDecodeError>` 错误模型 + CRC 低字节在前序列化/验证）落地 `modbuslens_core`；RTU-A01~A07 全绿。**Part B — Function 0x03 Codec** ⬜：解释 0x03 的 data 字段（request: startAddress/quantity，大端；normal response: byteCount/寄存器值，大端；exception: 结构化保存 exceptionCode）。两 Part 分别 TDD。**范围**：fuzz/benchmark 不在 T004；异常码→文字映射归 analysis 层 |
| T005 | **Simulator Basic Slave** | M3 | P0 | Backlog | T004 | IFrameSource 首个实现；寄存器表；虚拟主站轮询闭环；虚拟时钟 + 种子确定性（ADR）；offscreen 可跑 |
| T006 | **Fault Injection** | M3 | P0 | Backlog | T005 | 超时/CRC 错帧/异常码注入开关；确定性复现；供 Demo A 使用的场景脚本 |
| T007 | **Transaction Analysis** | M4 | P0 | Backlog | T004（+T005 提供流量） | 请求-响应配对（含广播/超时）、时延计算、错误与功能码统计快照；合成流量单测 |
| T008 | **Qt Analysis UI** | M4 | P0 | Backlog | T007 | 主窗口、模式切换骨架、帧/事务/统计/报告视图；UI 薄壳与核心解耦；offscreen 冒烟扩展 |
| T009 | **Replay Mode** | M5 | P0 | Backlog | T007 | MLog 日志格式 v1（ADR）与读写；回放/暂停/调速/时间轴；**口径一致性测试**（Simulator 与 Replay 同流同结论） |
| T010 | **Serial Mode** | M5 | P0 | Backlog | T007 | QtSerialPort 采集、t3.5 帧切分、环形缓冲；com0com/socat 虚拟串口对集成测试；真机核对清单 |
| T011 | **AI Diagnosis** | M6 | P2 | Backlog | T007 | 诊断模块：**确定性规则引擎（不依赖 LLM）** + 报告导出（Markdown/JSON）；可选 LLM 自然语言解释（可插拔、缺失不影响） |
| T012 | **Agent Tools** | M6 | P2 | Backlog | T007/T008/T011 输出 | 只读 Agent 工具集：读日志摘要/统计/报告；架构强制**无写 API**（类型层面不存在） |
| T013 | **Final Integration & Demo** | M7 | P1 | Backlog | T008（含 T009–T012 可用能力） | 打包/便携发布；演示脚本与素材齐备（demo/ 目录）；文档终稿；可选：CRC 查表优化与基准（单独拆分，不并入任何协议任务） |

## 建议路线（默认执行顺序）

```text
T001 ✅ → T001.1 ✅ → T002 CRC16 ✅ → T003 Frame Model ✅ → T004 Codec（A: Wire / B: 0x03，进行中）
→ T005 Simulator → T006 Fault Injection → T007 Transaction Analysis
→ T008 Qt UI → T009 Replay → T010 Serial → T011 AI Diagnosis
→ T012 Agent Tools → T013 Final Integration & Demo
```

## 变更记录

| 日期 | 事件 |
| --- | --- |
| 2026-09-05 | T001 建立本文件与任务表草案（旧编号 T002–T011） |
| 2026-09-05 | T001 完成（提交 `aa337f6`）；M1 关闭；T002 保持 Ready |
| 2026-09-05 | T001.1：任务细粒度重排为 T002–T013（T002 仅 CRC16；帧模型/0x03 编解码/模拟器/故障注入/UI/回放/串口/AI/Agent 各自独立）；里程碑重组为 M1–M7 |
| 2026-09-05 | T002 进入 Phase A（Learning Checkpoint，docs-only）；状态改为 In Progress，未标完成 |
| 2026-09-05 | T002 Phase B（Test Design）完成：测试矩阵与优先级、接口定案（std::span）、Phase C TDD 计划落库 |
| 2026-09-05 | T002 完成（Phase C 实现 + RED→GREEN，T002 **DONE**）；T003 转 Ready；里程碑 M2 进行中（T003/T004 待启动） |
| 2026-09-05 | T003 完成（Frame 内存模型 + 4 测试全绿，T003 **DONE**）；范围修订：fuzz-lite 移出 T003、wire 编解码/CRC 校验集成归 T004；补录 ADR001（最终 UI = Qt Quick/QML，用户于 T003 开始前确认）；T004 转 Ready |
| 2026-09-05 | T004 启动（Learning，docs-only）：更名 **Modbus RTU Codec** 并拆 Part A（Wire）/ Part B（0x03）；确认 fuzz/benchmark 不在 T004、异常码文字映射归 analysis；T005 不变 |
| 2026-09-05 | T004 Part A Test Design（docs-only）：接口/错误模型定案、测试矩阵 RTU-A01~A07（P0×4）；下一步 = Part A Implementation |
| 2026-09-06 | T004 Part A 完成（`ModbusRtuCodec` 实现 + A01~A07 RED→GREEN，Part A **DONE**）；Part B 未开始，T004 保持 IN PROGRESS |