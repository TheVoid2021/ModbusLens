# 01 — 需求文档（基线 v0.1）

> 状态：**基线**。本文件是活的：每个任务会细化对应条目并更新状态列；需求变更须经 Issue/ADR 记录后修改本文。
> 约定：状态取值 `Proposed`（提议）→ `Groomed`（已细化，待实现）→ `Done`（实现并验证）。
> 注（T001.1）：`计划` 列的旧任务编号（T002–T011）已按 BACKLOG 细粒度拆分（T002–T013）重新映射。

## 1. 术语表

| 术语 | 含义 |
| --- | --- |
| PDU | 协议数据单元：功能码 + 数据（与具体网络无关） |
| ADU | 应用数据单元：RTU 为 地址+PDU+CRC；TCP 为 MBAP 头+PDU |
| 帧（Frame） | 线路上完整的一条消息字节序列 |
| 事务（Transaction） | 一次请求-响应配对及其时延、结果 |
| 主站 / 从站 | Master（发起请求）/ Slave（响应） |
| Simulator / Replay / Serial | 三种数据源模式（见下） |
| MLog | 本项目的日志文件格式（T009 定义） |

## 2. 功能需求

> 说明：T001/T001.1 只交付骨架与文档，以下为全量基线规划，供后续任务逐项细化。`计划` 列为负责实现该需求的任务。

### 2.1 共享诊断核心（所有模式共用）

| ID | 需求 | 状态 | 计划 |
| --- | --- | --- | --- |
| FR-CORE-01 | Modbus RTU 帧解析（地址、功能码、数据、CRC 校验） | Proposed | T002（CRC）+ T003（帧模型） |
| FR-CORE-02 | Modbus TCP 帧解析（MBAP） | Proposed | 后续（M5+） |
| FR-CORE-03 | CRC-16/MODBUS 计算与校验 | Proposed | T002 |
| FR-CORE-04 | 帧完整性判定（长度、字节流切分规则） | Proposed | T003 |
| FR-CORE-05 | 事务配对：请求-响应/广播-超时关联，计算时延 | Proposed | T007 |
| FR-CORE-06 | 流量统计：帧数、字节数、错误率、功能码分布 | Proposed | T007 |
| FR-CORE-07 | 诊断规则：确定性规则引擎（超时突增、错误集中、异常码频发等）+ 报告导出 | Proposed | T011 |
| FR-CORE-08 | 诊断报告导出（Markdown/JSON） | Proposed | T011 |
| FR-CORE-09 | 核心逻辑不依赖 LLM；Agent 缺席时功能完整 | Groomed | 贯穿 |

### 2.2 Simulator Mode

| ID | 需求 | 状态 | 计划 |
| --- | --- | --- | --- |
| FR-SIM-01 | 虚拟从站：支持常见功能码（0x03 起，逐步扩展） | Proposed | T005 |
| FR-SIM-02 | 可配置从站拓扑与寄存器初值 | Proposed | T005 |
| FR-SIM-03 | 可注入异常：超时、CRC 错误、异常码响应（用于演示诊断） | Proposed | T006 |
| FR-SIM-04 | 内置虚拟主站周期性轮询以产生流量 | Proposed | T005 |
| FR-SIM-05 | 一切行为可确定性复现（种子/虚拟时钟） | Proposed | T005 |

### 2.3 Replay Mode

| ID | 需求 | 状态 | 计划 |
| --- | --- | --- | --- |
| FR-REP-01 | 定义 MLog 日志格式：时间戳+方向+原始帧 | Proposed | T009 |
| FR-REP-02 | 导入 MLog 文件并离线回放到分析核心 | Proposed | T009 |
| FR-REP-03 | 回放控制：开始/暂停/调速/时间轴跳转 | Proposed | T009 |
| FR-REP-04 | 回放口径与 Simulator 一致（同一核心产出可比诊断） | Proposed | T009 |

### 2.4 Serial Mode

| ID | 需求 | 状态 | 计划 |
| --- | --- | --- | --- |
| FR-SER-01 | 枚举/打开/关闭串口，配置波特率、数据位、校验、停止位 | Proposed | T010 |
| FR-SER-02 | 原始字节流采集与帧切分（t3.5 静默判定） | Proposed | T010 |
| FR-SER-03 | 写入审计：记录主站下发（只供分析，非 Agent 功能） | Proposed | T010 |
| FR-SER-04 | 虚拟串口对可做集成测试（com0com/socat） | Proposed | T010 |

### 2.5 只读 LLM Agent（M6，可选项）

| ID | 需求 | 状态 | 计划 |
| --- | --- | --- | --- |
| FR-AG-01 | Agent 仅只读：读日志摘要、读统计、生成自然语言诊断说明 | Proposed | T011 |
| FR-AG-02 | Agent 无任何写线圈/写寄存器/配置下发能力（架构强制） | Groomed | T012 |
| FR-AG-03 | Agent 不可用时平台功能不受影响 | Groomed | T011+T012 |

### 2.6 UI

| ID | 需求 | 状态 | 计划 |
| --- | --- | --- | --- |
| FR-UI-01 | 主窗口 + 模式切换（Simulator/Replay/Serial） | Proposed | T008 |
| FR-UI-02 | 实时帧列表、事务列表、统计面板、诊断报告视图 | Proposed | T008 |
| FR-UI-03 | UI 与核心解耦：核心可直接被测试驱动 | Groomed | 贯穿 |

## 3. 非功能需求（NFR）

| ID | 需求 | 备注 |
| --- | --- | --- |
| NFR-01 可构建性 | 主线任意提交可构建，文档给出标准命令 | CMake Presets |
| NFR-02 可测试性 | 核心模块可脱离硬件在无 GUI 环境下测试（offscreen） | T001 已建立 |
| NFR-03 可追溯性 | 每个任务完整档案 + 状态单一事实源 | AGENTS.md 强制 |
| NFR-04 可移植性 | 项目状态仅存于仓库 Markdown；工程支持 Windows/Linux；不依赖任何机器绝对路径 | T001.1 建立红线 |
| NFR-05 性能 | 诊断分析吞吐不低于 10 帧/秒 场景下的实时要求；采集不丢帧 | T007 后量化 |
| NFR-06 技术基线 | C++20、Qt ≥ 6.5（开发基线 6.11）、CMake ≥ 3.21、CTest | 见 ENVIRONMENT |
| NFR-07 编码规范 | 源码保持 ASCII 安全（跨编译器编码安全）；文档 UTF-8 | 见 ENVIRONMENT |
| NFR-08 安全边界 | Agent 只读；日志内容不自动上传任何外部服务 | 后续任务细化 |

## 4. 验收口径

- 每个实现任务的 DoD（Definition of Done）= 构建成功 + 相关测试通过 + 任务档案/STATUS/BACKLOG 更新 + 提交。
- 需求状态翻转到 `Done` 时必须附带对应 Verification 记录（任务文档链接）。