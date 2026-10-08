# 02 — TECH STACK MAP（实际使用技术的逐项核验）

> 用途：面试"你用了什么技术栈"的可辩护版本。**只列 ModbusLens 真正使用并可在仓库内验证的技术**。
> 证据分级：`[V-nnn]` = VERIFIED；`[C-nn]` = CONFLICT；`[U-nn]` = UNVERIFIED。
>
> **核验方法（本轮实际执行）**：先读 `CMakeLists.txt` 的 `find_package` / `target_link_libraries` /
> `add_library` 事实，再对每项技术在 `src/` `tests/` `scripts/` 中实测命中文件数——
> **不因为"同类项目通常会用到"就写入**。本轮因此**剔除了一项常见误报**：`std::filesystem`
> 在 `src/` 与 `tests/` 中命中 **0** 个文件（持久化实际由 adapter 层的 Qt JSON 承担），故不列入。`[V-119]`

---

## 1. 构建 / 语言 / 工具链

### C++20
- **What**：语言标准（`set(CMAKE_CXX_STANDARD 20)` / `STANDARD_REQUIRED ON` / **`CXX_EXTENSIONS OFF`**）。`[V-120]`
- **Where**：全项目。
- **Problem it solves**：协议与状态机需要"成功或具体错误"的显式表达、值语义模型与无异常的控制流。
- **Why selected**：项目学习价值（charter 明确）＋ 值语义/`variant`/`optional` 契约适合协议与事务分析。
- **Alternatives**：Rust / Go——生态与学习成本权衡后未采用（`docs/07` 第三章）。
- **Limitations**：`CXX_EXTENSIONS OFF` 意味着不使用 GCC 扩展；CI 未接，跨平台仅 Windows 被实测验证。
- **Anchor**：`CMakeLists.txt:9-11`；`docs/07_FINAL_PROJECT_REVIEW.md` 第三章。

### CMake ≥ 3.21 + Presets
- **What**：构建系统与两层 preset 结构。
- **Where**：全项目；提交通用 preset `debug` / `release`，机器私有 `debug-local` / `release-local`
  由 gitignored 的 `CMakeUserPresets.json` 提供（模板 `CMakeUserPresets.example.json`）。`[V-121]`
- **Problem it solves**：换机器/换 AI 平台时"clone → 复制模板 → 三条命令"即可重建，不继承原机器的路径假设。
- **Why selected**：presets 可迁移 + 原生集成 CTest。
- **Alternatives**：Makefile（跨平台组织差）。
- **Limitations**：真实本机 preset **永不提交**；`CMakePresets.json` 不得绑定生成器与工具链绝对路径。`[V-122]`
- **Anchor**：`docs/ENVIRONMENT.md` §2/§3/§6；`git check-ignore CMakeUserPresets.json` 应命中。

### MinGW-W64 g++ 13.1.0 + Ninja 1.12.1 + CMake 3.30.5（唯一官方验证过的组合）
- **What**：编译器/生成器/构建工具版本组合。
- **Where**：全部构建与测试证据。
- **Why selected**：Qt 安装器自带的 Desktop MinGW kit 一步到位（含 CMake/Ninja/MinGW）。
- **Alternatives**：MSVC kit——**未验证**（且 ISSUE-003 的根因正是组件被装到了 MSVC prefix）。
- **Limitations**：`docs/ENVIRONMENT.md` §1 明确"其他组合（Qt 版本、编译器、平台）**未经验证**"。`[V-123]`
- **Anchor**：`docs/ENVIRONMENT.md` §1。

### CTest（70 个测试目标）
- **What**：CMake 自带测试运行器；每目标独立 `add_test(NAME … COMMAND …)`。
- **Where**：`CMakeLists.txt`；目标清单见 `docs/interview/06_SOURCE_TRACEABILITY.md` §4。`[V-124]`
- **Problem it solves**：一条命令跑全部、退出码人机均可消费、clone 后结果可复现。
- **Why selected**：Qt 已是依赖，不必再引测试编排层。
- **Limitations**：**未接 CI**——验证链是本机的 configure / build / ctest / QML 门禁 / deploy（诚实边界，见 §4）。
- **Anchor**：`CMakeLists.txt`；`docs/04_TEST_STRATEGY.md` §4。

### Qt Test（`Qt6::Test`）作为断言框架 + QSignalSpy
- **What**：断言框架（`QCOMPARE` / `QVERIFY`）；`QSignalSpy` 用于**通知契约**测试。`[V-125]`
- **Where**：`find_package(Qt6 REQUIRED COMPONENTS Test)`；测试目标链接 `Qt6::Test`；
  `QSignalSpy` 在 7 个测试文件中命中（例：ISSUE-019 新增的 `b1c21`/`b1c22`）。
- **Problem it solves**：ISSUE-019 暴露"按需计算的 getter 让 C++ 测试对缺通知完全免疫"——
  `QSignalSpy` 把"通知"本身变成被测对象。
- **Alternatives**：GoogleTest / Catch2——规模不需要，且 Core 保持零 Qt 仅测试目标链接 Qt。
- **Limitations**：CTest 与断言框架是两层，报"目标数"不等于"用例数"（口径见 `docs/12` §5.1）。
- **Anchor**：`CMakeLists.txt:348`；`docs/issues/ISSUE-019-*.md`。

---

## 2. C++20 标准库特性（按实测命中文件数）

| 特性 | src 命中 | 用途与解决的问题 | Anchor |
| --- | --- | --- | --- |
| `std::optional` | 33 | **防"假 0"**：在 Qt 边界拆为 `hasX + value`，不把 `nullopt` 偷偷变成 0；统计的 `successRate` / `averageSuccessLatencyMs` 分母为 0 时返回 `nullopt` 而非 0% | `TransactionStatistics.h`；`docs/07` §8 | `[V-126]` |
| `std::string_view` | 41 | 解析器"进值出"：`parseReplayLog(text)` 不复制；跨 `QByteArray` 生命周期的显式纪律 | `src/core/replay/ReplayLog.*`；T009 | `[V-127]` |
| `std::vector` | 58 | 容器主力（帧数据、寄存器列表、报告集合） | — | `[V-128]` |
| `std::variant` | 18 | **错误模型**：`variant<Frame, RtuDecodeError>`——成功与失败都是返回值的一部分，调用方必须显式处理；不用异常也不用错误码返回值 | `ModbusRtuCodec.h`；T004/T009 | `[V-129]` |
| `std::chrono` | 20 | 时延测量与虚拟时钟 | `TransactionAnalysis`；benchmark `steady_clock` | `[V-130]` |
| `std::span` | 15 | 非拥有的字节视图（wire 解析） | `ModbusRtuCodec` | `[V-131]` |
| `std::unique_ptr` | 3 | 少量所有权表达 | — | `[V-132]` |
| `std::array` | 1 | 定长缓冲 | — | `[V-133]` |
| `std::shared_ptr` | 1 | 单点共享所有权 | — | `[V-134]` |

> **未使用**：`std::filesystem`（src/tests 命中 **0**）——持久化由 adapter 层 Qt JSON 承担；`std::thread`（线程模型见 §3 说明）。
> `[V-135]`

**穷举 `switch` 无 `default`** 是本项目的编码纪律：新增枚举值必须触发编译期穷举错误，
防止静默扩展（`docs/07` 第十章、`docs/08` §3.11）。`[V-136]`

---

## 3. Qt 6

### Qt 6.11.1（Desktop MinGW kit）· Qt Quick / QML + Qt Quick Controls 2
- **What**：UI 技术栈（ADR001 定案）。
- **Where**：`src/ui/qml/`；`QQmlApplicationEngine`（`src/main.cpp` 单点）；
  `QQuickStyle::setStyle("Fusion")`（1 处）。`[V-137]`
- **Problem it solves**：声明式 UI 与核心解耦；QML 只做展示，C++ Controller/Model 做桥接。
- **Why selected**：ADR001——开发者已有 QML 能力，两周量级项目显著降低 UI 学习/试错成本；
  现代 Qt 桌面应用的主流形态。
- **Alternatives**：QWidget（曾作为 T001 bootstrap scaffold，被 ADR001 取代）。
- **Limitations（真实教训）**：
  1. **Qt Quick Controls 原生 style 会静默忽略 QML 自定义**（T013 真实教训 → 改用 `Fusion`）；`[V-138]`
  2. **`Layout.*` 附加属性在非 Layout 父下被静默忽略**（ISSUE-004）；`[V-139]`
  3. **QML 无限定名解析只含「自身 + 组件 root + id」，不含中间祖先**（ISSUE-016）；`[V-140]`
  4. **`Item` 默认 `implicitHeight = 0`** ⇒ 自定义组件根若无 implicit 尺寸，父布局会静默少算整块内容（ISSUE-018）；`[V-141]`
  5. **module singleton（`pragma Singleton`）在 exe 内嵌 qrc + AOT qmlcache 组合下运行时不可用**（ISSUE-010，
     机理级根因未完全隔离，改用 engine root context property）；`[V-142]`
  6. `Drawer` 是 **position-driven**（`opened`/`position` 才是布局事实，`visible` 穿越过渡）。`[V-143]`
- **Anchor**：`docs/adr/ADR001-use-qt-quick-qml.md`；`src/main.cpp`；`docs/issues/ISSUE-004/010/016/018`。

### Qt SerialPort（`QSerialPort` / `QSerialPortInfo`）
- **What**：串口采集与主动写。
- **Where**：`src/ui/serial/SerialPortAdapter.{h,cpp}`（8 个文件命中 `QSerialPort`）；
  `QSerialPortInfo::availablePorts()` 用于**适配器存在性**判定（4 个文件命中）。`[V-144]`
- **Problem it solves**：跨平台串口访问（对比裸 Win32 COM API 不可移植）；`readyRead` 异步模型。
- **Why selected**：Qt 官方组件；与既有 Qt 依赖一致。
- **Alternatives**：裸 Win32 COM API（不可移植，未采用）。
- **Limitations**：
  1. **`QSerialPort::isOpen()` 是句柄自述状态**——设备消失后仍为 `true`；"适配器还在不在"必须问 OS
     （`availablePorts()` 走 `DIGCF_PRESENT` 实时枚举）；`[V-145]`
  2. **不采集 UART 层事实**：parity / framing / break / overrun 不可见，adapter 只保留 `QSerialPort::errorString()`
     一次性通用文本；`[V-146]`
  3. **无 per-byte / per-chunk 时间戳**，也没有通用 t1.5/t3.5 gap scanner
     （`docs/09` 明确：v1 禁止 t1.5/t3.5 generic framer 与 passive sniffer，
     Windows/QTimer 毫秒级不适合伪装 gap detector）；`[V-147]`
  4. 组件属 Qt 官方 **addons，默认不安装**（ISSUE-003 的直接原因）。`[V-148]`
- **Anchor**：`docs/issues/ISSUE-003`；`docs/04_TEST_STRATEGY.md` §3 Serial；`docs/09` §16。

### Qt Network（`QNetworkAccessManager` / `QNetworkReply`）
- **What**：provider HTTP/TLS 客户端。
- **Where**：`src/ui/ai/ModelScopeDiagnosisClient.*`、`src/ui/agent/ModelScopeAgentClient.*`、
  M12 的 candidate/QA transport（9 个文件命中 `QNetworkAccessManager`）。`[V-149]`
- **Problem it solves**：OpenAI-compatible Chat Completions 调用；TLS。
- **Why selected**：Qt 生态内自足（对比 libcurl）。
- **Limitations（真实教训）**：
  1. **`QNetworkReply::errorString()` 不可当用户文案**——它是本地化 transport 字符串，
     曾把 `OperationCanceledError` 泄漏成"操作被取消"（ISSUE-005）；`[V-150]`
  2. **`QNetworkReply::error()` 不含 abort 主体信息** ⇒ 必须自有状态补位（abort 前记录 `AiAbortReason`）；`[V-151]`
  3. **多个 timeout 机制并存即竞态**（native `setTransferTimeout` vs `QTimer`）⇒ 单一 timeout owner；`[V-152]`
  4. **Qt 非 2xx 也会置 reply error**（原文档记录的 HTTP/网络错误分层映射顺序坑）。`[V-153]`
- **Anchor**：`docs/issues/ISSUE-005`；`docs/07` 第十章。

### Qt JSON（`QJsonDocument` / `QJsonObject` / `QSaveFile`）
- **What**：设备档案与手册库的持久化格式。
- **Where**：`src/ui/profile/ProfileStore.cpp`（17 文件命中 `QJsonDocument`，22 命中 `QJsonObject`）；
  使用 `QSaveFile` 原子写 + `QStandardPaths` + `QCryptographicHash`。`[V-154]`
- **Problem it solves**：人类可读、可 diff、无数据库依赖的持久化；schema version = 1。
- **Why selected**：portfolio 规模不需要数据库；JSON 便于人工审阅与测试 fixture。
- **Alternatives**：SQLite（`docs/INTERVIEW_NOTES` F9 反思"会坚持 SQLite/JSON 持久化层设计先行"）、
  `std::filesystem` + 手写文本（未采用）。
- **Limitations**：**ManualStore 现为文件型 JSON**（无事务、无并发控制）；持久化层属 **adapter 层**
  （Qt），`modbuslens_core` 本身仍零 Qt。`[V-155]`
- **Anchor**：`src/ui/profile/ProfileStore.cpp`；`docs/tasks/T027` §25（Minimal JSON v1 shape）。

### QObject / QAbstractListModel（App/Adapter 层桥接）
- **What**：core 快照 → 可绑定属性的桥。
- **Where**：`src/ui/AnalysisController.{h,cpp}`、`src/ui/TransactionListModel.{h,cpp}`（`QObject` 在 78 个文件命中）。`[V-156]`
- **Problem it solves**：把纯 C++ 值类型暴露给 QML，同时把 `optional` 语义在边界拆成 `hasX + value`。
- **Limitations**：**delegate 的 `model.*` 读取必须容错**——模型 reset（清空 / 重载 / 换源）时 QML 会在
  index 已失效时再求值一次绑定，把 `undefined` 赋给有类型的属性或调用其方法都会静默产生诊断（ISSUE-014 → ISSUE-017）。`[V-157]`
- **Anchor**：`docs/02_ARCHITECTURE.md` §2；`docs/issues/ISSUE-017`。

### Qt 模块链接边界（Zero-Qt Core 的构建系统级证据）
```text
find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 SerialPort Network Test)
modbuslens      → modbuslens_core Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::SerialPort Qt6::Network
modbuslens_core → （无任何 target_link_libraries —— 不链接任何 Qt）
```
- `grep -rn "#include <Q\|#include <Qt" src/core/` → **0 命中**。`[V-158]`
- **Anchor**：`CMakeLists.txt:16`、`:304-310`；`docs/02_ARCHITECTURE.md` D3。

### 明确**未使用**的 Qt / 生态技术
- **Qt Linguist / `.ts` / `.qm` / 运行时语言选择器**：产品仅支持简体中文界面，属过度设计（`docs/06` §6 取舍记录）。`[V-159]`
- **QProcess**：`src/` 与 `tests/` 命中 **0**。ISSUE-015 的 QProcess 失败来自 **Qt 自带 `windeployqt` 内部**，
  不是本项目代码。`[V-160]`

---

## 4. Modbus / 工业协议技术

| 技术 | 项目内用法 | 关键参数 / 边界 | Anchor | |
| --- | --- | --- | --- | --- |
| **CRC-16/MODBUS** | 帧完整性校验 | 规范多项式 `0x8005`，**反射形式 `0xA001`**，Init `0xFFFF`，RefIn/RefOut = true，XorOut `0x0000`；**按位实现**（非查表）；**线上低字节先** | `src/core/protocol/ModbusCrc.*`；`docs/03` §5/§10 | `[V-161]` |
| **RTU 帧模型（ADU）** | 帧内存表示 | `{address, functionCode, data}` **刻意不存 CRC**（CRC 属线路层） | `ModbusRtuFrame.h` | `[V-162]` |
| **RTU 编解码** | Frame ↔ wire | 错误模型 `variant`；`FrameTooShort` / `CrcMismatch`；**无 resync**（整段输入按一个候选帧处理，首字节即 address） | `ModbusRtuCodec.{h,cpp}` | `[V-163]` |
| **FC03 Read Holding Registers** | 一等读路径（主动 + 被动） | quantity **1..125**（RTU 帧 256 B：响应 `5+2N ≤ 256` ⇒ `N ≤ 125`）；正常响应 `byteCount = 2N`；异常 `0x83` + 单字节 | `Function03.{h,cpp}` | `[V-164]` |
| **FC06 Write Single Register** | 被动语义 + 主动写（用户确认式） | 正常响应 = **echo**（字段与值完全回显）⇒ exact-echo matcher；异常 `0x86`；回显不匹配 ⇒ `WriteSingleRegisterEchoMismatch`（**不是** `MalformedNormalResponse`） | `Function06.*`；`docs/03` §4.5 | `[V-165]` |
| **FC16 / 0x10 Write Multiple Registers** | 被动语义 + 主动写（用户确认式） | quantity **1..123**（请求 `9+2N ≤ 256`）；`byteCount = 2×quantity`；正常响应 = **起始地址 + 写入数量**（不回显数据）；异常 `0x90`；字段顺序 = startAddr(2B) + qty(2B) + byteCount(1B) + values(2N) | `Function16.*`；`docs/03` §4.5 | `[V-166]` |
| **Generic exception matcher** | 识别"被拒函数"的异常 | `exception function = request.function + 0x80`，payload = **一个 exception code**（单字节）；**前置守卫 `(request.functionCode & 0x80) == 0` 冻结**（否则 0x88/0x88 会自我匹配） | `PassiveTransactionAnalysis.*`；PASSIVE-P16 | `[V-167]` |
| **异常码语义** | 诊断文案与 AI prompt 的确定性事实 | `0x01` Illegal Function / `0x02` **Illegal Data Address** / `0x03` Illegal Data Value / `0x04` Slave Device Failure；未知码不猜 | `docs/03` §4；ISSUE-006 | `[V-168]` |
| **广播（address 0）** | 一等可诊断事务 | 协议上**写类功能码**才可广播、**谁都不回应**；本仓库被动支持集 = `{0x06, 0x10}`（read 永远不可广播）；无响应 ⇒ `ExpectedNoResponse`；收到响应 ⇒ `ProtocolError + UnexpectedResponseForBroadcast` | ADR-003；`docs/11` §5 | `[V-169]` |
| **寄存器字节序 / 字序** | 读数解码 | 16-bit 寄存器帧内**大端**；32-bit 跨两寄存器时**字序设备间不一致**是现场"数据看不懂"头号原因；支持 `RegisterByteOrder` + `RegisterWordOrder`（HighWordFirst AB CD 默认 / LowWordFirst CD AB） | `RegisterDecode.h`；T024 | `[V-170]` |
| **0-based vs 40001 人读地址** | 诊断必须区分两种口径 | PDU 内地址从 0 计数；`40001` 是 1 计数人读编号 ⇒ 常见 off-by-one 源头 | `docs/03` §2；T023 | `[V-171]` |
| **t1.5 / t3.5 静默** | **协议知识**，不是已实现的采集能力 | **帧间**必须 ≥ 3.5 字符时间（否则两帧粘连）；**帧内** > 1.5 字符时间视为帧中止；9600-8E1 下 1 字符 ≈ 1.146 ms、t3.5 ≈ 4.01 ms、t1.5 ≈ 1.72 ms | `docs/03` §3 | `[V-172]` |
| **确定性故障注入（4 模式）** | Simulator 诊断素材 | `None / DropResponse / CorruptCrc / ArtificialDelay`，wire 层注入，元数据延迟；**不靠随机** | `SimulationFault.{h,cpp}`；FAULT-T01~T05 | `[V-173]` |

> **协议知识的两处诚实边界**：
> 1. `docs/09` §20 记录 `samples/demo_v2.mlog` S11 的注释把**帧内断帧阈值写成 t3.5**，
>    按项目自有协议资料应为 **t1.5**（t3.5 是帧间分离量）——`Audit 标记：demo_v2 comment terminology needs correction`。`[V-174]`
> 2. **t1.5/t3.5 只作为知识存在，不是已实现的采集能力**：Serial 的帧定界按"期望响应长度 + 超时"，
>    没有通用静默间隔扫描（`docs/09` §16/§20；T010 档案明确推迟）。`[V-175]`

---

## 5. AI / Agent 技术

### OpenAI-compatible Chat Completions（BYOK）
- **What**：provider 协议——raw HTTP + JSON，**不使用任何厂商 SDK**。
- **Where**：`ModelScopeDiagnosisClient`、`ModelScopeAgentClient`、M12 candidate/QA transport。
- **Problem it solves**：解释既有结构化事实；避免厂商 SDK 绑定。
- **Why selected**：ModelScope API-Inference + `Qwen/Qwen3.5-27B`（可用额度环境、中文模型）；
  `docs/07` 第三章明确 "OpenAI SDK：**被明确禁止**（仅协议兼容）"。
- **Limitations**：provider 配置是**桌面级进程环境模型**（`MODELSCOPE_API_KEY` +
  `MODBUSLENS_MODELSCOPE_MODEL`，BYOK），非多租户产品；无 provider family / provider-selection UI；
  **不配置时 AI / Agent / 手册问答不可用，但三模式 + 统计 + 基线诊断 + 手册导入完整可用**。
- **Anchor**：`docs/07` 第三章、§9；`docs/01_REQUIREMENTS.md` FR-AG-03；`README.md` §11。`[V-176]`

### Native tool calling（只读、有界）
- **What**：模型返回原生 `tool_calls`，经 C++ 白名单校验后执行，结果以 `role=tool` 回填（原 `tool_call_id`）。
- **Where**：`src/ui/agent/`。**恰好 3 个只读工具**：`get_session_summary` / `get_recent_anomalies` /
  `get_transaction_detail`；`get_recent_anomalies` 的 "recent" = **latest-20**（原序返回 + truncated 标志）。`[V-177]`
- **Problem it solves**：让模型按需读事实，同时把越权面冻结在 contract 里。
- **Why selected**：ADR002 D-A1/D-A2/D-A3（能力边界 / 数据保真 / 时间有效性）。
- **Limitations**：batch-scoped `transaction_number`（1-based 序号）在 batch 切换后失效，**跨 batch 查询天然不支持**
  （v1 无历史存储）；预算使多步深问可能触顶（ISSUE-007 已调宽到 3 rounds / 6 calls）。
- **Anchor**：`docs/adr/ADR002-readonly-tool-agent-architecture.md`；`README.md` §4。`[V-178]`

### 结构化输出契约（M12 Manual Q&A）
- **What**：显式 `enable_thinking=false`（经 `chat_template_kwargs`）+ `max_tokens` + 引用契约测试（`qa36`）；
  15 类解析失败类别可观测。
- **Why**：M12-D R2B 的"无法解析"双根因之一是 **thinking 模式未关**。
- **Anchor**：`docs/tasks/T027` §119.6 item 6；`docs/INTERVIEW_NOTES.md` F7。`[V-179]`

### App-resolved citations（防幻觉引用）
- **What**：provider 只能在 app 提供的上下文块 id 中**选择**；`offset` / `hash` 由 app 侧回填；未知 id **fail-closed**。
- **Why**：M12-D R2E——让 provider 编造 citation offset 不可靠（本地 round-trip 失败）。
- **Anchor**：`docs/tasks/T027` §119.6 item 7。`[V-180]`

---

## 6. 第三方二进制依赖（仅 M12 手册导入）

| 依赖 | 形态 | 引入方式 | 解决的问题 | Anchor | |
| --- | --- | --- | --- | --- | --- |
| **PDFium** | pinned 二进制（includeDir / runtimeDll / importLibrary 各自带 SHA-256） | `scripts/materialize_c1b_deps.py` **离线物化** + **配置期 hash 断言**（`third_party/m12c-c1b/`） | PDF 手册文本抽取（**本地**，不依赖 provider） | `CMakeLists.txt:93-112` | `[V-181]` |
| **libzip** | static library | 同上 | DOCX（ZIP 容器）读取 | 同上 | `[V-182]` |
| **zlib** | static library | 同上 | 压缩流 | 同上 | `[V-183]` |

- **为什么这样引入**：离线可物化 + 配置期 hash 断言 ⇒ 换机器可重建且不可静默漂移。
- **Limitations**：**扫描件 PDF / OCR 未实现**（`docs/tasks/T027` §47.6 明确边界）；
  依赖体积进入便携包；物化清单是 M12-C C1b 的专门切片。
- **Anchor**：`CMakeLists.txt`；`docs/tasks/T027` §47.4–§47.8、§49；
  ctest 目标 `c1b_dependency_materializer` + `manual_import_pdf_docx`。

---

## 7. 发布 / 部署技术

| 技术 | 用途 | 关键契约 | Anchor | |
| --- | --- | --- | --- | --- |
| **windeployqt** | Qt 运行期依赖部署 | 部署树 provenance = windeployqt + 编译器 bin 三件套；**禁止手工拼装冒充** | `scripts/deploy_windows.bat`；ISSUE-015 | `[V-184]` |
| **candidate 契约** | Human 验收/打包的**唯一**输入树 | `<build-config>/candidate/ModbusLens/`；每次从零重建；Release/Debug 各自独立树；**不得消费 raw target output**，**不得复用历史 deploy/package tree** | `cmake/modbuslens_generate_candidate.cmake`；AGENTS.md「Windows Deployment」 | `[V-185]` |
| **逐文件 SHA-256 manifest + fail-fast 打包器** | 防"打包错产品" | `scripts/make_package.py`：deploy → staging → 结构/凭据/绝对路径检查 → manifest（1498 entries）→ ZIP（1499）→ fresh extraction → minimal-PATH 三模式 → external-CWD；异常全部 `SystemExit(1)` | `scripts/make_package.py`；PKG-01..26 | `[V-186]` |
| **A/B/C/D 四路 exe identity** | 发布身份 | 验收对象 identity = exe SHA-256（**ZIP 非 identity criterion**） | T024 §29；T022 §ZE | `[V-187]` |
| **PE VERSIONINFO + CMake 单一版本权威** | 版本号唯一来源 | `project(ModbusLens VERSION 2.0.0)` 是唯一公开版本权威，应用与 Windows VERSIONINFO 同源派生 | `CMakeLists.txt:3-4`、`:177-180` | `[V-188]` |
| **minimal-PATH 启动门** | 启动环境纪律 | PATH 只含 candidate root 与 Windows 系统目录；清除 `QT_PLUGIN_PATH` / `QT_QPA_PLATFORM_PLUGIN_PATH` / `QML_IMPORT_PATH` / `QML2_IMPORT_PATH` | `tests/deployment_startup_check.cmake`；ISSUE-002 | `[V-189]` |

### 维护者侧脚本（Python，**不进产品包**）
- `scripts/make_package.py`（打包）、`test_make_canonical_package.py`（PKG-01..26）、
  `test_make_package_freshness.py`（freshness oracle）、`gen_samples.py`（**确定性**混合样本生成器，
  内置与真实帧一致的 CRC16 断言）、`bench.cpp` + `scripts/bench_replay/`（性能基准）、
  `make_icon.py`、`materialize_c1b_deps.py`。`[V-190]`
- 依赖：**仅 `pefile`**（PE Machine/version 校验用）；**包内不含 Python**。`[V-191]`
- **Anchor**：`docs/ENVIRONMENT.md` §4b；`README.md` §9/§10。

### 明确**未使用**
- 数据库、Docker、MCP、RAG、vector DB、multi-agent、OpenAI SDK、双语言 i18n 框架、**CI 流水线**
  （`docs/07` 第三章"明确不在本项目，不写进 resume"）。`[V-192]`
- **代码签名 / installer**：仅存在于打包器 README 的**否定陈述**中
  （`make_package.py:85` "No installer"、`:96` "not code-signed"），实现层面不存在。`[V-193]`

---

## 8. 技术栈一句话版（面试可直接用）

> **C++20（`variant` 错误模型 / `optional` 防假 0 / `string_view` 进值出，`CXX_EXTENSIONS OFF`）+ Qt 6.11.1
> （Quick/QML + Controls2/Fusion，SerialPort，Network，JSON）+ CMake ≥3.21 Presets + CTest 70 目标（Qt Test 断言，
> fake HTTP / fake serial 离线确定性）+ PDFium/libzip/zlib（pinned 离线物化）+ windeployqt + candidate 契约打包。
> 协议侧自研 CRC-16/MODBUS 按位实现与 RTU 帧/编解码，覆盖 FC03/FC06/FC16 与广播语义；
> 核心是 Zero-Qt 静态库。**

`[V-194]`