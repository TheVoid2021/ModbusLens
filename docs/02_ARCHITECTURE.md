# 02 — 架构文档

> 状态：**骨架版 v0.1**（T001 建立总体分层与原则；各层具体接口在 T002–T011 落地，重要取舍随 ADR 固化）

## 1. 分层总览

```text
┌──────────────────────────────────────────────────────────────────┐
│ UI 层（src/ui：Qt Quick/QML ← ADR001，T008 起）                   │
│  Main.qml Shell/Dashboard；QMainWindow 仅为旧 scaffold，T008 删除 │
├──────────────────────────────────────────────────────────────────┤
│ App/Adapter 层（src/ui：AnalysisController + TransactionListModel）│
│  QObject/QAbstractListModel 桥接：core 快照 → 可绑定属性/role；    │
│  optional → hasX + value；statusText 等 label 适配在此层           │
├──────────────────────────────────────────────────────────────────┤
│ App 装配层（src/app）                                             │
│  会话管理：按模式装配「数据源 + 分析核心」，生命周期与线程调度     │
├──────────────────────────────────────────────────────────────────┤
│ 分析核心（src/core，纯逻辑，无 I/O / 无 GUI / 无 LLM 依赖）        │
│  ├─ 协议解析：帧 → PDU → 语义（三模式共用，唯一实现）              │
│  ├─ 事务分析：请求-响应配对、时延、超时、广播                      │
│  ├─ 统计：帧/字节/错误/功能码分布                                  │
│  └─ 诊断：确定性规则引擎 → 诊断报告                                │
├──────────────────────────────────────────────────────────────────┤
│ 数据源适配层（src/io，统一 IFrameSource 接口）                    │
│  ├─ SimulatorSource：内存虚拟从站/主站（确定性、可注入异常）        │
│  ├─ ReplaySource：读取 MLog 日志按时间轴回放                       │
│  └─ SerialSource：QtSerialPort 采集 + 帧切分（t3.5）               │
└──────────────────────────────────────────────────────────────────┘
```

## 2. 核心设计决策（D 系列）

**依赖方向（T008 定案，强制）**：

```text
QML / Qt Quick
       ↓
AnalysisController / TransactionListModel（App/Adapter 层，Qt 类型仅允许于此）
       ↓
modbuslens_core（Pure C++20，Zero Qt）
```

严格禁止 `modbuslens_core → Qt/QML` 反向依赖；Qt 类型（QString/QVariant/QObject 等）只能出现在 app/ui adapter 层与 UI 测试。optional 语义在边界拆为 hasX + value（不把 nullopt 偷偷变成 0）。

### D1 统一数据源抽象：IFrameSource（三模式复用的关键）

```text
IFrameSource      — open()/start()/stop()/close() + 帧回调/拉取
   └─ 产出统一结构 RawFrame { timestamp, direction, bytes, sourceName }
```

- 三种模式只是三种**数据源实现**；下游协议解析、事务、统计、诊断完全一致。
- 这是满足「三模式复用同一核心」的架构支柱（后续以 ADR 固化接口细节）。

### D2 协议核心 = 纯函数 + 值类型

- 解码函数签名形如 `std::optional<Pdu> decode(const Bytes&)`：无全局状态、无 I/O，天然可单元测试。
- 所有字节序/字序解释显式化，禁止依赖宿主机字节序的隐式转换。

### D3 分析核心与 I/O、GUI、LLM 三层隔离

- 分析核心只消费 `RawFrame` 流、产出结果对象（事务表/统计快照/诊断报告）。
- 该层编译不依赖 QtGUI/SerialPort/Network，只允许 QtCore/QtTest（测试）与 STL。
- **实现载体（T002 落地）**：独立静态库 target `modbuslens_core`，**不链接任何 Qt**；`modbuslens` 应用与各测试 target 只依赖它——协议逻辑与 GUI 在构建系统层面物理隔离，Simulator/Replay/Serial 与未来 CLI 均复用同一核心。
- 保证：Simulator 与 Replay 的统计口径一致；核心可在 CI 无显示器环境跑全套测试。

### D4 线程模型（初步，T005 首个数据源落地时细化）

- 采集（I/O 线程）→ 无锁/少锁队列 → 分析（工作线程）→ 快照发布（信号槽）→ UI。
- Serial 采集用 QtSerialPort 的 `readyRead` + 环形缓冲；Simulator 用可控虚拟时钟驱动，保证确定性。

### D5 Agent 边界（M6）

- Agent 通过 HTTP 与只读 Service 通信；Service 所能触及的数据仅为统计快照与报告。
- 写操作能力在类型层面不存在（无任何"写"API），从架构上保证 FR-AG-02。

### D6 诊断细节与归一化状态正交（T014，2026-09-13 落地）

- **六种 `TransactionStatus` 保持不变**；协议/事务级确定性诊断细节由 `TransactionIssue`（`TransactionIssueCode` 七值 + 稀疏 optional 载荷）随 `TransactionAnalysis` 保存（追加末尾、默认 nullopt——aggregate 源兼容）。
- 生产不变量：`ProtocolError ⇒ issue.has_value()`（只能在分析器产生；防御路径用 `UnknownProtocolError` sentinel，下游不得反向伪造）；其余状态 issue 恒缺席；Exception 的 detail 仍只有 exceptionCode；CrcError 不重复挂 detail（状态名已承载唯一确定性来源）。
- 下游（Baseline/AI Prompt/Agent Tool/UI）**只读不重判**：reason 仅在 `analyzeFunction03Transaction` 产生，三模式（Simulator/Replay/Serial）经同一漏斗结构性同口径；serialization token（`transactionIssueName`）与人类文案分层（QString/UI prose 不进 Core）。
- `TransactionStatistics` 仍只按 high-level status 聚合（STAT-B09 锁定）；detail 不产生任何统计维度。

## 3. 模式实现策略

| 模式 | 数据来源 | 关键点 | 复用程度 |
| --- | --- | --- | --- |
| Simulator | 内存虚拟从站响应 + 虚拟主站轮询 | 确定性种子、虚拟时钟、异常注入 | 复用全部核心 |
| Replay | `.mlog` v1 日志文件（transaction-oriented text log） | **更新（T009 Part A）**：批处理/分析式回放——历史记录瞬间重新分析，非实时播放；parser 只管文本语法，CRC/长度/0x03 语义归 protocol 层 | 复用全部核心 |
| Serial | 真实串口字节流 | 帧切分、t3.5 判定、缓冲区 | 复用全部核心 |

## 4. 目录规划（随任务落地）

```text
src/
├── app/     # 装配与生命周期（T008 起）
├── core/    # 协议编解码（T002–T004）、事务统计（T007）、诊断（T011）
│   ├── protocol/ModbusCrc.{h,cpp}       # ✅ T002：CRC-16/MODBUS 按位实现（数值）
│   ├── protocol/ModbusRtuFrame.h        # ✅ T003：RTU 帧内存模型（不存 CRC）+ isExceptionResponse
│   ├── protocol/ModbusRtuCodec.{h,cpp}  # ✅ T004 Part A：Frame↔wire 编解码（variant 错误模型）
│   ├── protocol/Function03.{h,cpp}      # ✅ T004 Part B：0x03 三个 decoder + 语义模型（大端 helper）
│   ├── simulator/SimulatedSlave.{h,cpp} # ✅ T005：模拟从站端点（Frame 进 Frame 出，const 纯应答）
│   ├── simulator/SimulationFault.{h,cpp}# ✅ T006：确定性故障注入四模式（wire 层，元数据延迟）
│   ├── analysis/TransactionAnalysis.{h,cpp} # ✅ T007 Part A：单事务分析（六状态，跨帧校验；Analysis 为 Protocol 上层）
│   ├── analysis/TransactionStatistics.{h,cpp} # ✅ T007 Part B：统计快照（三计数/五分类/optional rate/latency）
│   ├── replay/ReplayLog.{h,cpp}      # ✅ T009 Part A：.mlog v1 模型 + parseReplayLog（纯文本解析，八错误码+1-based 物理行号；无 Qt、string_view 进值出）✅
│   └── replay/ReplayAnalysis.{h,cpp} # ✅ T009 Part A：批量回放分析（request 可信链→T007→summarizeTransactions；不 sleep、不调用 SimulatedSlave）✅
├── io/      # 数据源适配（IFrameSource 抽象**推迟**：单数据源阶段不过早设计，等 Replay/Serial 出现真实共性再提取；Simulator 端点先行，见 T005）
├── ui/      # ✅ T008 Part A 起（Qt App/Adapter 层，ADR001：Qt 类型仅允许于此）
│   ├── qml/Main.qml                     # ApplicationWindow Shell（统计卡/事务列表/空状态）
│   ├── AnalysisController.{h,cpp}       # core 快照 → 可绑定属性（optional→hasX+value）
│   └── TransactionListModel.{h,cpp}     # QAbstractListModel（7 roles，setEntries 整批替换）
└── main.cpp # 入口
tests/
├── test_smoke.cpp          # 当前：骨架冒烟测试
├── unit/  component/  integration/   # 随核心落地扩充
└── data/   # 测试夹具（CRC 规范向量、示例 MLog 日志）
```

## 5. 技术约束（与 INTERVIEW_NOTES / ENVIRONMENT 联动）

- C++20；Qt ≥ 6.5（基线 6.11）；CMake ≥ 3.21 + Presets；CTest 全程驱动验证。
- 源码文件保持 ASCII 安全；文档（含中文）统一 UTF-8。
- 本项目暂不引入第三方协议库（pymodbus 等仅作测试对拍参考），CRC 与帧解析自己实现——这是项目核心学习价值所在。

## 6. 演进规则

- 任何对 D1–D6 的变动 → 新建 ADR，禁止静默改架构。
- 本文件随 ADR 诞生而更新「决策索引」，保持与代码事实一致。

### 决策索引

- **ADR001**（2026-09-05）：最终 UI 采用 Qt Quick/QML + Qt Quick Controls；QMainWindow 仅为 bootstrap scaffold，T008 切换。C++ 侧以 Controller/`QAbstractListModel`/`QSerialPort` 桥接；Modbus Core 不得依赖 QML。