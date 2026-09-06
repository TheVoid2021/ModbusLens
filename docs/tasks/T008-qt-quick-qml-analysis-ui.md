# T008 — Qt Quick / QML Analysis UI

> 状态：**IN PROGRESS**｜Part A（Qt Quick Migration + C++/QML Bridge）：**DONE ✅**（Learning / Test Design + Implementation + 用户人工验收 12/12，RED→GREEN + Manual UI Smoke 全程留痕）｜Part B（Analysis Dashboard + Deterministic Demo）：**Learning / Test Design ✅（docs-only）→ Implementation ⬜**
> 前置确认：T007 DONE、LKGC = `28f38b0`（T008.1 代码/脚本提交）、ISSUE-002 RESOLVED。
> ⚠ 独立遗留：Standalone Explorer Launch = FAIL / ISSUE-002 OPEN（runtime collision 为部署/环境问题，不影响 Part A 验收，修复待立项）。
> Part B Implementation 边界预告（未实现，禁止提前）：Run Demo Batch、Clear Demo、真实调用 T005/T006/T007 链路、Dashboard 统计更新、事务列表填充；轮询/Serial/Replay/Agent/AI/database/timer/thread 全部不做。

## Implementation 前追加规则（2026-09-06，定案）

**A. QML load smoke 必须验证真正的最终 App/QML Module**：禁止为测试复制第二份 Main.qml 或创建与最终 App 不同的 QML module。实现采用：App executable 增加**极小 `--qml-smoke-test` 诊断参数**——加载真正的 QML（loadFromModule）后 rootObjects 非空即立即返回成功，CTest 以 `QT_QPA_PLATFORM=offscreen` 运行最终 app 的 smoke mode。这样测到的是实际交付模块。（若改用可复用 QML module target 方案亦可，但不得复制 QML 文件、不得依赖绝对路径、需说明理由——本项目采用 smoke 参数方案。）

**B. Manual UI Smoke 必须是真实人工/视觉验收**：Agent 必须真实启动 ModbusLens。若 Agent 环境能直接观察 GUI，按 checklist 验证；若无法可靠观察桌面窗口，**不得写 Manual UI Smoke = PASS**，必须写 `WAITING FOR HUMAN CONFIRMATION` 并停下来要求用户确认。Automated build/test PASS 不能代替视觉 UI PASS。

**C. 类型注册单一机制**：本任务采用 `QML_ELEMENT + qt_add_qml_module`（Qt QML type registrar 自动注册）；**禁止**再叠加手工 `qmlRegisterType` 形成两套注册机制并存。具体 API 以 Qt 6.11.1 实际 configure/build 结果为准。

## Implementation 前追加规则（结束）

## Goal

把 QWidget bootstrap scaffold 正式替换为 **Qt Quick/QML 应用骨架**，并建立 Core → Qt Adapter → QML 的桥接层（AnalysisController + TransactionListModel），让 T007 的统计快照与事务数据有了一条**可测试、可被 QML 消费**的通路——Part B 再把真实数据填进 Dashboard。

## Background

- T007 交付了 TransactionAnalysis 与 StatisticsSnapshot（纯 core 对象）；ADR001 早已定案最终 UI 为 QML，但 scaffold 仍是 QMainWindow。
- 本任务是第一个 GUI 任务：先立骨架与桥接（Part A），再填真实数据与演示行为（Part B）——把"UI 能跑"与"UI 有用"分开验收。

## Scope

**Part A 只设计/实现**：QGuiApplication 启动、QQmlApplicationEngine、qt_add_qml_module、Main.qml、AnalysisController（QObject）、TransactionListModel（QAbstractListModel）、Core→Qt Adapter 边界、QML load smoke test、Controller/Model bridge tests。

**禁止（Part A）**：Simulator 自动轮询、Serial、Replay、QSerialPort、Agent、AI、database、chart framework、动画大工程、theme system、persistent settings、real-time timer、thread、networking。

**Part B 才负责**：把 T005/T006/T007 数据真正填进 Dashboard（Run Demo Batch、fault 注入按钮、模型填充、统计更新、Clear Demo、基础视觉整理）。Part B 仍不做 Replay/Serial/AI/Agent。

## 最终依赖方向（架构约束）

```text
QML / Qt Quick
       ↓
AnalysisController / TransactionListModel   （Qt App/UI adapter 层）
       ↓
modbuslens_core                             （Pure C++20，Zero Qt）
```

**严格禁止反向**：`modbuslens_core → Qt/QML`。Qt 类型只能出现在 app/ui adapter 层与 UI 测试。详见 02_ARCHITECTURE（本阶段已同步）。

## QWidget → Qt Quick Migration（Part A Implementation 计划）

```cpp
// 最终启动概念（具体 API 按 Qt 6.11.1 环境核实后定案）：
QGuiApplication app(argc, argv);
QQmlApplicationEngine engine;
engine.loadFromModule("ModbusLens", "Main");
return app.exec();
```

- 正式**移除** QApplication + QMainWindow bootstrap（src/main.cpp 重写；若 QWidget 完全不再使用，删除对应依赖）；
- App target 的 UI 依赖从 `Qt6::Widgets` 迁移到 `Qt6::Gui + Qt6::Qml + Qt6::Quick + Qt6::QuickControls2`；
- 测试 target 仍允许 `Qt6::Test`；
- 不为兼容旧 scaffold 保留 QWidget 最终 UI。

## QML Module（定案）

- 使用 **`qt_add_qml_module`**（CMake 正式管理 QML 资源；clean build 后资源完整；不依赖开发机绝对路径；换机/clone 可构建）——而非手工复制松散 QML 文件；
- URI：**`ModbusLens`**；版本 **1.0**；入口 **`Main.qml`**；
- 目录：**`src/ui/qml/Main.qml`**（与规划的 src/ui 分层一致）。

## AnalysisController 的职责

QObject，**Core 与 QML 之间的 application adapter**——不是新的业务 Core。Part A 暴露统计字段：

```text
observedCount / pendingCount / completedCount
successCount / exceptionCount / crcErrorCount / timeoutCount / protocolErrorCount
hasSuccessRate + successRate
hasAverageSuccessLatency + averageSuccessLatencyMs
```

**为什么 optional 拆成 hasX + value**：`std::optional<double>` 不应强行暴露给 QML。例：completedCount==0 → Core `successRate = nullopt` → Controller `hasSuccessRate = false` → QML 显示 "—"。**不得把 nullopt 偷偷变成 0%**（破坏 T007 语义）。

## Q_PROPERTY 设计（定案）

```cpp
class AnalysisController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(int observedCount READ observedCount NOTIFY statisticsChanged)
    // ...其余 7 个计数同型...
    Q_PROPERTY(bool hasSuccessRate READ hasSuccessRate NOTIFY statisticsChanged)
    Q_PROPERTY(double successRate READ successRate NOTIFY statisticsChanged)
    Q_PROPERTY(bool hasAverageSuccessLatency READ hasAverageSuccessLatency NOTIFY statisticsChanged)
    Q_PROPERTY(double averageSuccessLatencyMs READ averageSuccessLatencyMs NOTIFY statisticsChanged)
    Q_PROPERTY(QAbstractItemModel* transactionModel READ transactionModel CONSTANT)
public:
    explicit AnalysisController(QObject* parent = nullptr);
    // C++ 侧数据入口（非 Q_INVOKABLE；Part B 由 Demo 流程调用）：
    void applySnapshot(const TransactionStatisticsSnapshot& snapshot);
    void setTransactionEntries(std::vector<TransactionListEntry> entries);
signals:
    void statisticsChanged();
};
```

**类型选择记录**：QML-facing 计数用 **`int`**（而非 `qsizetype`/`std::size_t`）——① int 是 QML 最稳定的数值类型；② 事务计数在 int 范围内绰绰有余；③ 避免 qsizetype(qint64)→QML number 的转换歧义。`successRate`/`averageSuccessLatencyMs` 用 `double`。

**Optional 的 QML 语义**：`hasSuccessRate == false` 时 `successRate()` 返回 **0.0 占位**——文档明确：该 0.0 只是无值时的安全占位，**QML 必须先看 hasSuccessRate**，业务语义由 hasX 决定（hasAverageSuccessLatency 同理）。

## TransactionListModel（定案）

`QAbstractListModel`（而非 QVariantList/QList<QObject*>/JS array）：ListView 原生适配、role 明确、更新可通知、C++ 控数据、面试价值高。

**App-layer DTO**（Qt/Presentation Adapter 层，**不是 protocol core**）：

```cpp
struct TransactionListEntry {
    int deviceAddress{};
    int functionCode{};
    TransactionStatus status{};
    qint64 elapsedMs{};
    std::optional<std::uint8_t> exceptionCode;
};
```

**设计边界（重要）**：TransactionAnalysis（T007）只存 status/elapsed/exceptionCode——**没有** request address/function，这是正确的最小分析结果。T008 要显示 Device/Function 时，Controller 在创建 UI entry 时**组合 Request metadata + TransactionAnalysis** 形成 TransactionListEntry；**不得反向修改 T007 Core model**。

### Model Roles（定案）

```cpp
enum Role {
    DeviceAddressRole = Qt::UserRole + 1,  // int
    FunctionCodeRole,                      // int
    StatusCodeRole,                        // int (TransactionStatus)
    StatusTextRole,                        // QString: Pending/Success/Exception/CRC Error/Timeout/Protocol Error
    ElapsedMsRole,                         // qint64
    HasExceptionCodeRole,                  // bool
    ExceptionCodeRole,                     // int（无值时 0，配合 Has flag）
};
int rowCount(...) const override;
QVariant data(...) const override;
QHash<int, QByteArray> roleNames() const override;
void setEntries(std::vector<TransactionListEntry> entries);  // 非 Q_INVOKABLE，仅 C++ Controller 调用
```

StatusText 映射由 **Qt Adapter 层**完成（enum → UI label 的适配）；**不把 QString 文本写进 TransactionStatus core enum**（Core 保持语言无关）。Part A 不设计 append/remove/history paging（避免过量接口）。

**Controller → Model 暴露**：Controller 持有 `TransactionListModel transactionModel_`，经 `Q_PROPERTY(QAbstractItemModel* transactionModel READ transactionModel CONSTANT)` 暴露；QML 只读 `model: controller.transactionModel`——**不让 QML new/修改 model**。

## Part A 初始状态（验证 T007 nullopt 语义贯穿到 QML）

Controller 初始 = 空 snapshot（observed/pending/completed 全 0、分类全 0、hasSuccessRate=false、hasAverageSuccessLatency=false）、ListModel rowCount=0。Main.qml 初始显示：统计卡数字 0、Success Rate "—"、Avg Latency "—"、Recent Transactions "No transactions yet"。

## Main.qml Part A 范围（Shell）

```text
ApplicationWindow
├── Header（ModbusLens + Simulator Mode label）
├── Statistics Area（Observed / Completed / Pending / Success Rate / Avg Latency）
└── Recent Transactions Area（empty-state / ListView）
```

**不做**：最终视觉系统、复杂动画、sidebar 大系统、图表、串口配置、fault controls——Part B 再补真正 Dashboard 行为。

## Formatting Rule（边界）

Core 返回数值：successRate=0.858 → **QML** 显示 "85.8%"；averageSuccessLatencyMs=23.4 → **QML** 显示 "23.4 ms"。字符串格式属 Presentation。**例外**：StatusText 由 Adapter 提供——它属于 enum→UI label 的适配，不是格式化。

## Part A Test Matrix

| Test ID | 场景 | Expected | Priority |
| --- | --- | --- | --- |
| UI-A01 | 构造 AnalysisController | observed/pending/completed=0，分类全 0 | **P0** |
| UI-A02 | 初始 optional 状态 | hasSuccessRate=false，hasAverageSuccessLatency=false（不得把空统计当 0%） | **P0** |
| UI-A03 | 空 TransactionListModel | rowCount == 0 | **P0** |
| UI-A04 | setEntries({device=1, fn=0x03, Success, 25ms}) | rowCount=1；deviceAddress=1、functionCode=3、statusText="Success"、elapsedMs=25、hasExceptionCode=false | **P0** |
| UI-A05 | setEntries({device=1, fn=3, Exception, 18ms, code=0x02}) | statusText="Exception"、hasExceptionCode=true、exceptionCode=2 | **P0** |
| UI-A06 | setEntries(第一批) 再 setEntries(第二批) | rowCount/内容完全反映第二批（Part B 重算 Demo batch 时可安全替换） | P1 |

## QML Smoke / Integration

| Test ID | 设计 | 决定 | Priority |
| --- | --- | --- | --- |
| **UI-I01** | QGuiApplication + QQmlApplicationEngine 加载 `ModbusLens/Main`，断言 rootObjects 非空、无 QML load error；`QT_QPA_PLATFORM=offscreen` 由 CTest 环境设置（已有惯例）；不得因 headless 跳过测试 | **实现**；若 Qt 6.11.1 MinGW 出现 platform plugin 问题，记录真实错误并做最小修复；不引入 screenshot pixel test | **P0** |
| UI-I02 | 初始 QML 状态的对象级验证 | **不实现（记录决定）**：文本/像素级查找脆弱；Part A 重点是 load smoke + bridge unit tests + Main.qml 明确绑定；Part B 有真实数据后再评估 findChild 级验证 | — |

## CMake Migration Plan（Part A Implementation，18 步）

1. 查当前 App/CMake Widget bootstrap；2. 配置 Qt Quick/QML components；3. 创建 QML module（qt_add_qml_module，URI ModbusLens）；4. main.cpp 切 QGuiApplication；5. 删除 QMainWindow bootstrap；6. 创建 AnalysisController；7. 创建 TransactionListModel；8. Main.qml 初始绑定；9. Bridge tests；10. QML load test；11. RED/GREEN；12. clean build；13. full ctest；14. 手工启动 App；15. 文档归档；16. code commit；17. 推进 LKGC；18. docs backfill。

## Manual UI Smoke（第一项 GUI 任务的强制验收）

Implementation GREEN 后除自动化测试外**必须实际启动 ModbusLens**，人工确认：窗口能显示、无 QWidget/QMainWindow、标题正确、初始统计显示、Success Rate 为 "—"、Avg Latency 为 "—"、空事务状态可见、无 QML runtime warning。记录 **Manual UI Smoke = PASS/FAIL**——不得只说"build pass 所以 UI 肯定能开"。

## Core 零 Qt 检查（T008 最重要的架构验收）

Implementation 后必须检查 `modbuslens_core` 未新增 Qt6::Core/Gui/Qml/Quick 依赖（CMakeLists target_link_libraries 复核 + git diff 复核）。允许 Qt 的是 App/UI target 与 UI tests。

## Knowledge I Must Be Able To Explain（18 题）

**UI-Q1. 为什么 Core 不能直接给 QML 用 Qt 类型？** 依赖方向必须 QML→Adapter→Core；core 沾 Qt 就失去可测试性/可移植性，且 QML 类型（QVariant/QString）会把协议语义绑死在 UI 框架上。
**UI-Q2. QObject Controller 解决什么问题？** 它是 Core（纯数据/纯函数）与 QML（声明式视图）之间的**有状态适配器**：把 core 快照翻译成可绑定的属性，并把用户操作翻译回 core 调用。
**UI-Q3. Q_PROPERTY 为什么可以驱动 QML 更新？** QML 对 Q_PROPERTY 建立绑定；属性 getter 依赖变化时发出 NOTIFY 信号，QML 引擎重新求值绑定——声明式响应的机制基础。
**UI-Q4. QAbstractListModel 解决什么问题？** 给 QML ListView 提供带 role 的行式数据协议（rowCount/data/roleNames），支持变更通知，是 Qt 中"列表数据进视图"的正规通道。
**UI-Q5. 为什么列表不用 QVariantList 草草实现？** 无 role 协议、无变更通知粒度、大列表性能差、类型信息弱；QAbstractListModel 才是可维护方案。
**UI-Q6. 什么是 model role？** 一行数据里某列的名字→值映射（roleNames 定义、data 按角色返回）；QML 里 model.roleName 即可访问。
**UI-Q7. 为什么 std::optional 要拆成 hasX + value？** QML 无 optional 类型；拆开后"有值吗"和"值是多少"成为两个可绑定属性，null 语义显式化。
**UI-Q8. 为什么空 successRate 不能直接暴露成 0%？** 0% 是"完成且全败"的否定结论；空是"尚无数据"——混用会破坏 T007 语义并误导用户。
**UI-Q9. 为什么 UI DTO 不应该塞回 T007 Core？** device/function 是 UI 展示需要的组合信息，不是分析结果的一部分；塞回会让 Core 被展示需求牵引（T007 保持最小结果）。
**UI-Q10. 为什么 status string 不属于 Core？** Core 保持语言无关；"Success"等文案是 UI label，属 enum→label 适配（Adapter 层职责）。
**UI-Q11. 为什么 QML 负责百分比和 ms 格式？** 展示格式（小数位/单位/本地化）随视图变化；Core 只提供数学值，同一数值可被多种视图格式化。
**UI-Q12. 为什么 QWidget bootstrap 要在 T008 删除？** ADR001 定案最终 UI 是 QML；保留双 UI 会保留 Widgets 依赖与两套启动路径——迁移完成即删，不留兼容尾巴。
**UI-Q13. qt_add_qml_module 有什么作用？** 把 QML/资源纳入 CMake 构建体系：生成模块注册代码、编译期校验、资源进二进制——路径无关、clone 可构建。
**UI-Q14. 为什么需要 QML load smoke test？** QML 是运行时语言，编译期不查；load smoke 在 offscreen 下验证"模块能解析、组件能实例化"，是 QML 的最小自动化防线。
**UI-Q15. 为什么不做 screenshot pixel test？** 像素断言脆弱（平台/字体/DPI 差异），维护成本高、误报多；数据正确性由 bridge tests 保证，渲染正确性由人工 smoke 保证。
**UI-Q16. 为什么 Part A 先做空 UI，再做真实 Demo？** 先立"可加载、可绑定、可测试"的骨架，Part B 填数据时每个绑定都有既有通路；避免 UI 与数据源同时引入变量。
**UI-Q17. Core → Controller → QML 的依赖方向是什么？** QML → Controller/Model → Core；严禁 Core → Qt/QML（方向反转会让协议层被 UI 框架绑架）。
**UI-Q18. T008 如何保持前面 T002~T007 的可测试性？** Core 不动（零 Qt 依赖不变）；所有 Qt 逻辑集中在 Controller/Model/入口——它们各自有 C++ 测试（bridge tests），QML 只有 load smoke。

## Implementation Plan（Part A 下一阶段，18 步）

见 §CMake Migration Plan（1–18 步，含 RED/GREEN 与 Manual UI Smoke）。

## Part B — Analysis Dashboard + Deterministic Demo Implementation（实录，2026-09-06）

### Demo 编排实现

`AnalysisController` 新增两个 `Q_INVOKABLE` 方法：

- **`runDemoBatch()`**：编排完整协议链——创建确定性 `SimulatedSlave{1}`（reg[0]=100, reg[1]=200, reg[2]=1500）→ 依次执行四个场景（Success / Exception / CRC Error / Timeout），每个场景真实调用 `handleRequest` → `encodeRtuFrame` → `applySimulationFault` → `decodeRtuFrame` → `analyzeFunction03Transaction` → 收集 `TransactionAnalysis` + `TransactionListEntry` → `summarizeTransactions` → 原子发布（`setEntries` + `statistics_` + `emit statisticsChanged`）。
- **`clearDemo()`**：复用 `summarizeTransactions(空批)` 恢复 Core 空快照语义 + `setEntries({})` 清空列表。

**Replace 语义**：每次 `runDemoBatch` 从同一初始状态重建 Slave，产生相同四条结果；`setEntries` 整批替换（非 append），rowCount 恒=4。

**defensive strategy**：固定 Demo fixture 不应失败；若内部 invariant 意外失败，采用 `qWarning` + 早退（不发布半成品 batch）。

### 四条 Demo 事务

| # | Request | Slave 响应 | 路径 | Analyzer 结果 | elapsed |
| --- | --- | --- | --- | --- | --- |
| DEMO-1 | start=0, qty=2 | 正常 {100,200} | handleRequest → Analyzer | Success | 25ms |
| DEMO-2 | start=100, qty=1 | 越界 0x83/{0x02} | handleRequest → Analyzer | Exception 0x02 | 18ms |
| DEMO-3 | start=0, qty=2 | 正常 → CorruptCrc | encode→fault→decode→Analyzer | CrcError | 17ms |
| DEMO-4 | start=0, qty=2 | 正常 → DropResponse | encode→fault→适配 NoResponse→Analyzer | Timeout | 1000ms |

### Expected Statistics

observed=4, completed=4, pending=0；success=1, exception=1, crcError=1, timeout=1, protocolError=0；successRate=0.25；averageSuccessLatencyMs=25.0。

### Core Integration Guard

Controller 实际引用（grep 验证）：`SimulatedSlave` / `applySimulationFault` / `encodeRtuFrame` / `decodeRtuFrame` / `analyzeFunction03Transaction` / `summarizeTransactions`——全部为 T002–T007 既有 Core 模块，Controller 仅做编排，不伪造 TransactionStatus。

### QML Dashboard 扩展

- 新增 Demo 控制区：Run Demo Batch / Clear 两个按钮
- 新增 5 个状态计数卡：Success / Exception / CRC Error / Timeout / Protocol Error
- Transaction delegate 保持使用现有 model roles

### Issues during implementation

1. **QStringLiteral 不接受运行时 char***：b02 测试中 QStringLiteral(expected[row].status) 编译失败。修复：改用 QString(...) 构造。
2. **命名空间限定遗漏 + 双重前缀**：`ModbusRtuFrame` 等类型在 runDemoBatch 中未加 `modbuslens::core::` 前缀导致多个编译错误；批量修正时又出现双重前缀（`modbuslens::core::modbuslens::core::`）。修复：精确修正为单次前缀。

## Files Changed（Part B 实现）

- 修改：`src/ui/AnalysisController.{h,cpp}`（Q_INVOKABLE 方法 + 编排实现）、`src/ui/qml/Main.qml`（Run/Clear 按钮 + 状态计数卡）、`tests/test_ui_bridge.cpp`（UI-B01~B06）、`docs/tasks/T008-qt-quick-qml-analysis-ui.md`（本文件 Part B 设计+实现补齐）
- 文档：`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、`docs/04_TEST_STRATEGY.md`
- 新增 devlog
- 未改动：`src/core/` 全部、`src/ui/TransactionListModel.{h,cpp}`、`src/ui/qml/Main.qml` 的 Part A 部分布局、presets

## Problems Encountered（Part B）

1. **QStringLiteral 不接受运行时 `const char*`**：测试 b02 中 `QStringLiteral(expected[row].status)` 编译失败。修复：改用 `QString(expected[row].status)`。
2. **命名空间限定遗漏 + 双重前缀**：`runDemoBatch` 中 `ModbusRtuFrame` 等类型未加 `modbuslens::core::` 前缀——多个编译错误。修复：全部加 `modbuslens::core::` 前缀；一次 Python 批量替换导致 `ResponseObservation` 双重前缀（`modbuslens::core::modbuslens::core::`），立即用 Edit 工具修正。
3. **实现本身：无 Core 集成问题**。SimulatedSlave/encodeRtuFrame/applySimulationFault/analyzeFunction03Transaction/summarizeTransactions 全部正常协作。

## Solutions

1. QStringLiteral → QString 构造函数（运行时 char* 可用）。
2. 逐个加命名空间限定 + 修正双重前缀。
3. （无 Core 集成问题需解决。）

## Verification

### RED（方法声明无定义；未提交）

```text
$ cmake --build --preset debug-local
undefined reference 共 2 处：
  `AnalysisController::runDemoBatch()`
  `AnalysisController::clearDemo()`
compile 通过，仅 link 失败——预期 RED。
```

### GREEN（实现后）

```text
$ cmake --build --preset debug-local              → 全部链接成功
$ ./build/debug/modbuslens_ui_bridge_tests.exe
  PASS: b01~b06  Totals: 14 passed, 0 failed (7ms)
  （a01~a06 + b01~b06 共 14 个测试函数全过）

$ ctest --preset debug-local
14/14: 全部 Passed（含新增 b01~b06 于 ui_bridge target 内）

$ cmake --build --preset debug-local --clean-first
警告/错误 grep = 0（零警告）；ctest 再次 14/14

$ QT_QPA_PLATFORM=offscreen ./build/debug/modbuslens.exe --qml-smoke-test
  exit=0（QML 模块加载并实例化成功——含 Part B 新增的 runDemoBatch/clearDemo Q_INVOKABLE）

$ scripts/deploy_windows.bat + minimal-PATH deploy smoke
  exit=0（standalone deployment 回归 PASS，ISSUE-002 未回归）
```

## Result

✅ **Part B Implementation 完成**：`runDemoBatch` / `clearDemo` 落地 Controller；四条事务全部真实调用 T005/T006/T007 Core 链路；statistics 来自 summarizeTransactions（非手工赋值）；UI-B01~B06 全过；QML Dashboard 含状态计数卡 + Run/Clear 按钮；standalone deploy 回归 PASS。
⏳ **Manual Demo Smoke = 待用户交互验收**（deploy exe 已启动，用户可点击 Run/Clear 按钮验证）。
⬜ **Part B 最终 DONE / T008 整体 DONE / M4 关闭：等用户确认后归档**。

## Knowledge Learned

- **adapter 层是 QML 化的核心**：Controller/Model 把"纯 core 对象"翻译成"可绑定属性/role"，翻译规则（optional→hasX+value、数值→格式化）显式成文。
- **QML 是运行时语言**：它的"编译期"就是 load smoke——offscreen 加载验证是 QML 项目的最小自动化防线。
- **类型在边界处定案**：QML-facing 用 int/double/bool/QString，core 用 size_t/optional/enum——两边各自最优，翻译集中在 adapter。
- **实现阶段新增**：
  1. **QML 模块挂 exe 目标**：注册对象属 exe 自身目标文件，静态链接不可能丢注册；独立 STATIC 模块库方案在 Windows/MinGW 下连踩 DLL 符号导出与静态插件拉入两坑——YAGNI 收敛到官方 app 模板结构。
  2. **`__has_include` 静默跳过**：QML 类型注册生成文件对找不到的头文件不报错、只跳过 include——"注册代码消失"类故障要先查生成文件的探测条件。
  3. **GUI app 的后台启动会被 shell 会话终止**：验收用 `timeout`/持久后台 + 可访问性树，不要依赖一次性后台任务存活。
  4. **qFuzzyCompare 对 0 不可靠**（延续 T007B）：精确零值用 `==`。
- **Part B 实现阶段新增**：
  1. **Batch Demo 原子发布**：先完整构建 analyses+entries 两个 vector，最后一次性 setEntries+applySnapshot+emit——不允许逐条 emit 或半成品发布。
  2. **Core Integration Guard 实证**：Controller 引用 grep 验证确认所有 Core 调用存在，无伪造 status。
  3. **Replace 语义**：setEntries 整批替换（beginResetModel/endResetModel）天然实现 Replace——不需要额外的"去重"或"追加"逻辑。

## Potential Interview Questions

- 18 题见上；Implementation 阶段新增：
  1. QML 模块为什么挂 exe 而不是独立库？（静态注册对象拉入问题——独立库在 Windows/MinGW 下连踩 DLL 导出与 whole-archive 两坑，官方 app 模板结构最稳）
  2. `__has_include` 静默跳过 include 的坑怎么发现？（QML 类型注册编译失败的排查实录，见 Problems #1）
  3. QML load smoke 为什么运行真实 exe？（规则 A：测实际交付模块；`--qml-smoke-test` 只是不进事件循环）
  4. Manual UI Smoke 用什么方法验收？（窗口枚举 + 可访问性树对真实运行进程逐项核对——桌面前台被占用时不抢焦点，并如实记录方法）
- Part B 阶段新增：
  1. runDemoBatch 编排了哪些 Core 模块？（SimulatedSlave→encodeRtuFrame→applySimulationFault→decodeRtuFrame→analyzeFunction03Transaction→summarizeTransactions——完整协议链）
  2. Replace 语义为什么用 beginResetModel/endResetModel？（QAbstractListModel 的标准整批替换通知协议——比逐行 dataChanged 更简洁安全）
  3. Demo 为什么每次重建 Slave？（确定性：同一初始状态→同一输出序列→演示可重复）

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| Part A Learning | `fe9dab6` | docs-only |
| Part A 代码提交（**LKGC**） | `76030a2` | `T008(Part A): migrate app to Qt Quick and add QML bridge` |
| Part A 回填 | `36ee814` | docs-only |
| ISSUE-002 诊断 | `14fcffa` | docs-only |
| runtime 追记 | `1ee2c5c` | docs-only |
| Part B Test Design | `f7716c4` | docs-only |
| Part B 代码提交（**新 LKGC**） | `PENDING-BACKFILL` | `T008(Part B): add deterministic analysis dashboard demo` |
| 回填提交（docs-only，HEAD） | 见 `git log` | 回填哈希 |

> LKGC 推进：Part B 产生新业务代码并经 configure/clean build/full ctest（14/14）+ QML smoke + deploy regression 验证；LKGC 由 `0f3109a` 推进至 Part B 代码提交，由 docs-only 回填提交写入。**Part B DONE 待用户确认后归档；T008 整体 IN PROGRESS（Part B 未最终确认）；T009 未开始。**

## Implementation（实录，2026-09-06）

### 新增/迁移文件

- `src/main.cpp` 重写：`QGuiApplication` + `QQmlApplicationEngine` + `loadFromModule("ModbusLens", "Main")`；加载失败走 `-1` 退出；`--qml-smoke-test` 诊断参数——加载真正 QML 后 rootObjects 非空即返回 0（规则 A：smoke 测的是实际交付模块）。旧 QApplication/QMainWindow bootstrap 删除。
- `src/ui/AnalysisController.{h,cpp}`：QObject + QML_ELEMENT + 12 个 Q_PROPERTY（8 计数 int / 4 optional 拆分 hasX+value）+ `transactionModel` CONSTANT；内部持有 core 快照（初始 = `summarizeTransactions(空批)`，与 T007 同源）+ `TransactionListModel`；C++ 侧入口 `applySnapshot` / `setTransactionEntries`（非 Q_INVOKABLE）。
- `src/ui/TransactionListModel.{h,cpp}`：QAbstractListModel + 7 roles + `setEntries`（beginResetModel/endResetModel 整批替换）；StatusText 为 adapter 层穷举 switch + QStringLiteral（Core 保持语言无关）。
- `src/ui/qml/Main.qml`：ApplicationWindow Shell（Header / 5 统计卡 / Recent Transactions ListView + "No transactions yet" 空状态）；绑定含 hasX 三元判断。
- `CMakeLists.txt`：find_package Widgets → Core/Gui/Qml/Quick/QuickControls2；QML 模块直接挂在 exe 目标（见下）；新增 `modbuslens_ui_bridge_tests` target（直接编译两个 adapter 源文件，用户许可方案）+ ctest `ui_bridge`；新增 `qml_smoke` ctest（运行真实 exe + `--qml-smoke-test`，offscreen）。
- 删除：`tests/test_smoke.cpp`（T001 QMainWindow 冒烟测试，只为旧 bootstrap 服务；Git 历史保留）。

### QML 模块挂载位置（关键决策）

QML 模块**直接挂在 exe 目标**（Qt 官方 app 模板结构）：注册对象属于 exe 自身的目标文件，静态链接不可能丢注册；bridge 测试直接编译 adapter 源文件（无 QML 复制）。曾尝试独立 STATIC 模块库方案——DLL/导入库符号导出与静态插件拉入两处踩坑（见 Problems #2/#3），按 YAGNI 收敛到最简结构。

### 三条追加规则落实

- **A**：qml_smoke 运行真实 exe + `--qml-smoke-test`，加载实际交付的 QML 模块后立即退出；无 QML 复制、无绝对路径。
- **B**：Manual UI Smoke 用可访问性树对**真实运行进程**逐项验收（见 Verification），并如实记录验收方法与局限。
- **C**：单一注册机制 `QML_ELEMENT + qt_add_qml_module`，无手工 qmlRegisterType。

## Files Changed（Part A 实现）

- 新增：`src/ui/AnalysisController.{h,cpp}`、`src/ui/TransactionListModel.{h,cpp}`、`src/ui/qml/Main.qml`、`tests/test_ui_bridge.cpp`、`docs/devlog/2026-09-06-T008-PartA-Implementation.md`
- 修改：`src/main.cpp`（QGuiApplication 迁移 + smoke 参数）、`CMakeLists.txt`（Widgets→Quick 系 + QML 模块挂 exe + 两个测试 target）
- 删除：`tests/test_smoke.cpp`（旧 QMainWindow 冒烟）
- 文档：`docs/tasks/T008-qt-quick-qml-analysis-ui.md`（本文件补齐）、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、`docs/02_ARCHITECTURE.md`、`docs/04_TEST_STRATEGY.md`、`docs/INTERVIEW_NOTES.md`
- 未改动：`modbuslens_core` 全部源码与测试（Core Zero Qt 保持）、presets

## Problems Encountered

1. **RED-1（QML 类型注册编译失败）**：`modbuslens_qmltyperegistrations.cpp: AnalysisController was not declared in this scope`——生成文件用 `__has_include(<AnalysisController.h>)` 按文件名探测头文件，`src/ui` 不在 include 路径时**静默跳过** include。修复：adapter 目标补 `src/ui` include 目录。教训：`__has_include` 失败不报错，只会让注册代码"消失"。
2. **RED-2（linker error，经典 RED）**：修复后桥接测试链接失败——19 处 undefined reference（AnalysisController 构造/12 个 getter、TransactionListModel 方法与 vtable）。预期 RED。
3. **静态 QML 模块注册未链入（GREEN 阶段真问题）**：独立 STATIC 模块库方案下，`qml_smoke` 报 `No module named "ModbusLens" found`（gdb 捕获）；排查发现 exe 只链了 `libmodbuslens_ui.a` 未链 `libmodbuslens_uiplugin.a`，且静态库成员无引用即不拉入——模块注册对象被链接器丢弃。`qt_import_qml_plugins` 显式调用也未能链入。
4. **-Wmissing-field-initializers（Part B 同款）**：bridge 测试辅助的 designated initializer 漏写成员，clean 重建 grep 抓出，显式补齐修复。
5. **旧 smoke 测试处置**：`tests/test_smoke.cpp`（T001 QMainWindow 冒烟）只为旧 bootstrap 服务——按"不留 dead code"原则正式删除（Git 历史保留）。

## Solutions

1. include 目录补齐后 RED-1 消除（该修复为 GREEN 永久所需）。
2. 按矩阵实现后 RED-2 → GREEN。
3. **QML 模块改为直接挂载 exe 目标**（Qt 官方 app 模板结构）：注册对象成为 exe 自身目标文件，链接必然包含；放弃独立模块库（YAGNI + 已实证两处坑）。qml_smoke 随即 exit=0。
4. 显式补齐成员初始化。
5. 删除 + 在 CMake 留注释指向 qml_smoke。

## Verification

### RED-1（QML 注册编译失败；未提交）

```text
$ cmake --build --preset debug-local
modbuslens_qmltyperegistrations.cpp:23: error: 'AnalysisController' was not declared in this scope
（生成文件 __has_include(<AnalysisController.h>) 因 src/ui 不在 include 路径而跳过头文件）
修复：adapter 目标补 include 目录 → 该错误消除，进入 RED-2。
```

### RED-2（linker error；未提交）

```text
undefined reference 共 19 处，去重符号：
  `AnalysisController::AnalysisController(QObject*)`、12 个 getter、
  `TransactionListModel::TransactionListModel`、`rowCount/data/roleNames/setEntries`、
  `vtable for TransactionListModel`
compile 全部通过，仅 link 失败——预期 RED。
```

### GREEN（实现后）

```text
$ cmake --build --preset debug-local              → 全部链接成功
$ ./build/debug/modbuslens_ui_bridge_tests.exe
  PASS: a01~a06  Totals: 8 passed, 0 failed (8ms)
$ QT_QPA_PLATFORM=offscreen ./build/debug/modbuslens.exe --qml-smoke-test
  exit=0（真实 QML 模块加载并实例化成功）

$ ctest --preset debug-local
14/14: crc frame codec f03 simulator simulator_integration fault
       fault_integration transaction transaction_integration statistics
       statistics_integration ui_bridge qml_smoke 全部 Passed（smoke 已删除）
100% tests passed, 0 tests failed out of 14

$ cmake --build --preset debug-local --clean-first
警告/错误行数 grep = 0（零警告，86 targets）；ctest 再次 14/14 通过
```

### Core Zero-Qt / Widgets 清理验收

```text
CMakeLists：modbuslens_core target_link_libraries 无任何 Qt6::*；
src/core/ 无 QObject/QString/QVariant/QAbstractListModel/Qt 头文件 include；
App 构建路径 grep QMainWindow/QApplication/Qt6::Widgets → 仅存注释与删除记录。
=> Core Zero Qt = PASS；Final App Widgets Dependency = NONE
```

### QML Runtime Warning 检查

```text
qml_smoke 与真实启动的 stderr 均无 QQmlApplicationEngine failed / module not
installed / Type unavailable / binding loop / ReferenceError / TypeError。
=> QML runtime warning = 0
```

### Manual UI Smoke（真实启动验收）

~~初版验收（已被 ISSUE-002 修正，见下）~~：

```text
[历史记录] 启动方式：./build/debug/modbuslens.exe（Git Bash 会话内）
验收方法：窗口枚举 + 可访问性树（桌面前台被用户其他运行中软件占据，
恢复/聚焦本窗口会干扰之，故不抢前台做像素截图；结构与内容经 a11y 全量核对）
结果（12/12 项）：窗口/标题/Header/Simulator Mode/统计 0/—/空状态/无警告 全部确认
=> 当时记录为 PASS（a11y 结构化验收）
```

**〔2026-09-06 追记：Manual Visual UI Smoke = PASS（用户人工确认 12/12）；Standalone Explorer Launch = FAIL / ISSUE-002 OPEN〕**

用户从 **Windows Explorer 直接双击** `build/debug/modbuslens.exe` 启动失败：

> 无法定位程序输入点 `_ZNSt3pmr20get_default_resourceEv` 于 `D:\QT\6.11.1\mingw_64\bin\Qt6Gui.dll`

诊断结论：**Runtime toolchain collision**——系统 PATH 中 `D:\Git\mingw64\bin`（Git 自带 runtime）与 `D:\mingw64\bin`（MinGW 8.1，2018-05）排在 Qt 13.1 runtime 之前；Explorer 环境解析到的 libstdc++-6.dll 实测**缺失** `_ZNSt3pmr20get_default_resourceEv`（objdump 精确对照；8.1 版=0，13.1 版=1）。临时 PATH（Qt bin + 13.1 MinGW bin 前置）验证启动成功（进程存活、窗口与 a11y 树完整、无警告）——根因坐实，**非 Core/QML/迁移代码缺陷**。

**〔2026-09-06 用户人工确认（最终）〕**用户以正确 runtime 启动应用并完成视觉检查，**checklist 12/12 PASS**：窗口正常打开、标题 ModbusLens、Qt Quick/QML UI 正常显示、Header 正常、Simulator Mode 正常、Observed/Completed/Pending = 0、Success Rate = —、Avg Latency = —、No transactions yet 正常显示、无明显布局/运行时异常。

- 完整诊断与证据：[ISSUE-002](../issues/ISSUE-002-explorer-launch-dll-collision.md)
- **两个结论严格区分**：
  - Manual Visual UI Smoke = **PASS**（正确 runtime 下真实窗口人工验收 12/12）；
  - Standalone Explorer Launch = **FAIL / ISSUE-002 OPEN**（runtime collision 是部署/环境问题，未解决，不影响 Part A 验收结论）。
- Part A 归档：DONE。后续修复方向（待立项，独立任务）：部署期 runtime 随应用部署（windeployqt / 复制 13.1 三件套到应用目录）。

## Result

✅ **Part A DONE**（2026-09-06，用户人工验收确认）：QWidget bootstrap → Qt Quick 迁移完成（Widgets 依赖彻底移除）；AnalysisController/TransactionListModel 桥接 + UI-A01~A06 全绿；真实 exe 的 QML load smoke 通过（runtime warning = 0）；Core Zero Qt 保持；全项目 ctest 14/14、clean 重建零警告；**Manual Visual UI Smoke = PASS（用户 12/12 确认）**。
⛔ **Standalone Explorer Launch = FAIL / ISSUE-002 OPEN**（环境部署问题，独立于 Part A 验收，修复待立项）。
⬜ **Part B（Analysis Dashboard + Deterministic Demo）Not Started** → **T008 整体仍 IN PROGRESS**。

## Knowledge Learned

- **adapter 层是 QML 化的核心**：Controller/Model 把"纯 core 对象"翻译成"可绑定属性/role"，翻译规则（optional→hasX+value、数值→格式化）显式成文。
- **QML 是运行时语言**：它的"编译期"就是 load smoke——offscreen 加载验证是 QML 项目的最小自动化防线。
- **类型在边界处定案**：QML-facing 用 int/double/bool/QString，core 用 size_t/optional/enum——两边各自最优，翻译集中在 adapter。
- **实现阶段新增**：
  1. **QML 模块挂 exe 目标**：注册对象属 exe 自身目标文件，静态链接不可能丢注册；独立 STATIC 模块库方案在 Windows/MinGW 下连踩 DLL 符号导出与静态插件拉入两坑——YAGNI 收敛到官方 app 模板结构。
  2. **`__has_include` 静默跳过**：QML 类型注册生成文件对找不到的头文件不报错、只跳过 include——"注册代码消失"类故障要先查生成文件的探测条件。
  3. **GUI app 的后台启动会被 shell 会话终止**：验收用 `timeout`/持久后台 + 可访问性树，不要依赖一次性后台任务存活。
  4. **qFuzzyCompare 对 0 不可靠**（延续 T007B）：精确零值用 `==`。

## Potential Interview Questions

- 18 题见上；Implementation 阶段新增：
  1. QML 模块为什么挂 exe 而不是独立库？（静态注册对象拉入问题——独立库在 Windows/MinGW 下连踩 DLL 导出与 whole-archive 两坑，官方 app 模板结构最稳）
  2. `__has_include` 静默跳过 include 的坑怎么发现？（QML 类型注册编译失败的排查实录，见 Problems #1）
  3. QML load smoke 为什么运行真实 exe？（规则 A：测实际交付模块；`--qml-smoke-test` 只是不进事件循环）
  4. Manual UI Smoke 用什么方法验收？（窗口枚举 + 可访问性树对真实运行进程逐项核对——桌面前台被占用时不抢焦点，并如实记录方法）

| Part A Learning | `fe9dab6` | docs-only |
| Part A 代码提交（**新 LKGC**） | `76030a2` | `T008(Part A): migrate app to Qt Quick and add QML bridge` |
| 回填提交（docs-only，HEAD） | 见 `git log` | 回填哈希 |

> LKGC 推进：Part A 产生新业务代码并经 configure/clean build/full ctest（14/14）+ QML smoke + Manual UI Smoke 验证；LKGC 由 `0f3109a` 推进至 Part A 代码提交，由 docs-only 回填提交写入。**Part A DONE；T008 整体 IN PROGRESS（Part B 未开始）；T009 未开始。**
## Part B — Analysis Dashboard + Deterministic Demo（Learning + Test Design，本阶段定稿）

### 职责

- Part A：QWidget→QML 迁移 + 桥接骨架（**已完成**）；
- Part B：把 T005/T006/T007 数据真实填进 Dashboard——一个按钮，稳定产生完全可复现的诊断演示。

**禁止**：real polling loop、QTimer、QThread、random fault、live fault selector、chart framework、Replay、Serial、AI、Agent、database、persistent history。

### Demo Batch 定案（四条固定事务）

| # | 场景 | Request | SimulatedSlave 响应 | Analyzer 结果 | elapsed |
| --- | --- | --- | --- | --- | --- |
| DEMO-1 | Success | dev=1, fc=03, start=0, qty=2 | 正常响应 {100,200} | Success | 25ms |
| DEMO-2 | Exception | dev=1, fc=03, start=100, qty=1 | 异常 0x83/{0x02} | Exception code=0x02 | 18ms |
| DEMO-3 | CRC Error | dev=1, fc=03, start=0, qty=2 | 正常响应 → CorruptCrc → CrcMismatch | CrcError | 17ms |
| DEMO-4 | Timeout | dev=1, fc=03, start=0, qty=2 | 正常响应 → DropResponse → NoResponse | Timeout（elapsed≥threshold） | 1000ms |

**不伪造 TransactionStatus**：四条结果必须通过现有 Core 模块真实产生（SimulatedSlave → encode/fault → decode → analyzeFunction03Transaction），elapsed 由调用方显式提供（Runtime Timer 尚未实现）。

### Expected Statistics（summarizeTransactions 输出）

```text
observedCount = 4, pendingCount = 0, completedCount = 4
successCount = 1, exceptionCount = 1, crcErrorCount = 1, timeoutCount = 1, protocolErrorCount = 0
successRate = 1/4 = 0.25
averageSuccessLatencyMs = 25.0   （仅 DEMO-1 的 25ms；18/17/1000 不混入成功平均延迟）
```

### Controller Part B API（定案，不实现）

```cpp
public slots:  // 或 Q_INVOKABLE
    void runDemoBatch();
    void clearDemo();
```

**为什么 Controller 而非 QML 直接调用**：Controller 本来就是 QML user action → application orchestration → Core 的适配层；QML 不应该自己创建 SimulatedSlave、调 encodeRtuFrame、调 Fault Injector、调 Analyzer、算 Statistics——所有业务编排继续在 C++ Controller。

### runDemoBatch 内部编排（概念流程）

```text
1. 创建 deterministic SimulatedSlave（reg[0]=100, reg[1]=200, reg[2]=1500）
2. DEMO-1：readRequest(1,0,2) → slave.handleRequest → 正常响应 Frame
   → ResponseObservation{frame} → analyzeFunction03Transaction(req, obs, 25ms, 1000ms) → Success
3. DEMO-2：readRequest(1,100,1) → slave.handleRequest → 异常 Frame 0x83/{0x02}
   → ResponseObservation{frame} → analyzeFunction03Transaction → Exception code=0x02
4. DEMO-3：readRequest(1,0,2) → slave.handleRequest → 正常 Frame
   → encodeRtuFrame → applySimulationFault(CorruptCrc) → decodeRtuFrame → CrcMismatch
   → ResponseObservation{RtuDecodeError} → analyzeFunction03Transaction → CrcError
5. DEMO-4：readRequest(1,0,2) → slave.handleRequest → 正常 Frame
   → encodeRtuFrame → applySimulationFault(DropResponse) → DroppedResponse
   → 适配为 NoResponse → analyzeFunction03Transaction(req, NoResponse, 1000ms, 1000ms) → Timeout
6. 收集 vector<TransactionAnalysis> → summarizeTransactions() → snapshot
7. 从 Request metadata + TransactionAnalysis 组合 TransactionListEntry batch
8. applySnapshot(snapshot) + setTransactionEntries(entries) → emit statisticsChanged
```

**Replace 语义**：每次 `runDemoBatch()` 重新生成同样四条结果，`setEntries` 整批替换（`beginResetModel/endResetModel`）；rowCount 恒 = 4，不追加为 8——保证演示可重复、测试可稳定、统计不因点击次数变化。

### clearDemo 语义

恢复空 batch → `summarizeTransactions(空)` → 全计数=0、hasSuccessRate=false、hasAverageSuccessLatency=false、rowCount=0。复用 `summarizeTransactions(empty)`（与 T007 同源），不手工遗漏任何 count。

### Part B Test Matrix（UI-B01~B06）

| Test ID | 场景 | Expected | Priority |
| --- | --- | --- | --- |
| UI-B01 | `controller.runDemoBatch()` | observed=4, completed=4, pending=0; success=1, exception=1, crcError=1, timeout=1, protocolError=0; hasSuccessRate=true, successRate≈0.25; hasAverageSuccessLatency=true, avg≈25.0 | **P0** |
| UI-B02 | `runDemoBatch()` 后 transactionModel | rowCount=4; 逐行 status: Success/Exception/CrcError/Timeout; elapsed: 25/18/17/1000 | **P0** |
| UI-B03 | Row 1 (Exception) | hasExceptionCode=true, exceptionCode=2; 其余三行 hasExceptionCode=false | **P0** |
| UI-B04 | `runDemoBatch()` ×2 | 统计完全相同，rowCount 仍=4（不追加为 8） | **P0** |
| UI-B05 | `runDemoBatch()` → `clearDemo()` | 全 count=0, rowCount=0, hasSuccessRate=false, hasAverageSuccessLatency=false | **P0** |
| UI-B06 | `run` → `clear` → `run` | 恢复完全相同的四条 demo | P1 |

### Core Integration Guard

Demo **不直接构造** TransactionStatus——测试必须证明 Controller 真在调用已有 Core。集成断言：CRC row 必须来自 CorruptCrc → decodeRtuFrame → CrcMismatch → analyzeFunction03Transaction → CrcError。独立脚本已复核（本阶段 Verification 节）。

### QML Dashboard 范围（Part B 新增）

Part A 已有：Observed / Completed / Pending / Success Rate / Avg Latency。

Part B 新增：**五个状态计数卡**（Success / Exception / CRC Error / Timeout / Protocol Error）+ **两个按钮**（Run Demo Batch / Clear）+ **transaction delegate 增强**（status 文字/色块区分、elapsed 加 "ms"、exception 显示 "Code 0x02"）。

**不做**：chart library、Theme Manager、全局设计系统、动画 framework——信息清晰即可。

### Demo Controls（QML）

```qml
Button { text: qsTr("Run Demo Batch"); onClicked: analysisController.runDemoBatch() }
Button { text: qsTr("Clear"); onClicked: analysisController.clearDemo() }
```

**不增加**：Start Polling / Stop / Auto Run / Interval / Fault Probability——当前没有 Runtime。

### Part B Knowledge I Must Be Able To Explain（14 题）

**PB-Q1. 为什么 QML 不直接调用 Simulator？** QML 是声明式视图，没有能力创建 C++ 对象、调用 encodeRtuFrame 或管理故障注入——这些是 C++ 编排层的职责。QML 只消费 Controller 暴露的属性和命令。
**PB-Q2. Controller 为什么属于 application orchestration 层？** Controller 把"用户意图"翻译成"Core 调用序列"：用户按 Run → Controller 编排 Simulator/Fault/Analyzer/Statistics → 把结果映射回 QML 可绑定的属性/模型。这就是 orchestration。
**PB-Q3. 为什么 Demo status 必须由 Transaction Analyzer 产生？** 防止伪造——如果 Controller 直接写 `status = Success`，那 Demo 就不是在验证协议链路，而是在测试 UI 渲染。真实 Core 调用保证 Demo 展示的是协议栈的真实行为。
**PB-Q4. 为什么 Dashboard 和 transaction list 必须来自同一批结果？** 如果统计和列表来自不同数据源，可能出现"统计说 4 条但列表只有 3 条"的不一致。同一批次 → 同一快照 → 同一模型 → 天然一致。
**PB-Q5. 为什么重复 Run 不 append？** Run 的语义是"重新执行确定性演示"，不是"追加历史"。固定 rowCount=4 保证演示可重复、统计不随点击次数变化。追加语义属于未来的 History 功能。
**PB-Q6. 为什么 Clear 后 successRate 应重新变 nullopt？** Clear 恢复到"没有事务"状态——空批的 successRate 是 nullopt（"没有数据"），不是 0%（"全部失败"）。T007 的 nullopt 语义必须在 UI 端也正确表达。
**PB-Q7. 为什么 Success Rate 是 25%？** 四条事务中仅 DEMO-1（Success）成功；1/4 = 0.25 = 25%。DEMO-2 是 Exception（设备回复了异常）、DEMO-3 是 CRC Error（线路损坏）、DEMO-4 是 Timeout（未交付）——各计 1 条非成功。
**PB-Q8. 为什么 Avg Latency 是 25ms？** 只有 Success 事务的 elapsed 进入成功延迟平均（T007 Part B 设计）；25ms 即 DEMO-1 的显式 elapsed。
**PB-Q9. 为什么 Exception 0x02 与 CRC Error 是两类不同故障？** 0x02 是设备主动回复的"非法数据地址"（设备端问题）；CRC Error 是线路把正确响应损坏了（线路端问题）。原因和处置完全不同——统计必须分开。
**PB-Q10. 为什么 DropResponse 在最终 UI 显示 Timeout？** DropResponse 是交付层事实（"没有响应被交付"）；Timeout 是等待方基于"无响应 + elapsed≥阈值"做出的判断（T006/T007 已定案）。Demo 的 DEMO-4 elapsed=1000ms ≥ threshold=1000ms → Timeout。
**PB-Q11. 为什么当前不用 Timer？** Timer 属于真实 Runtime 的职责（轮询/调度/超时测量）；Part B 的 Demo 是同步确定性场景，elapsed 显式传入——Timer 会引入不可复现的时序依赖。
**PB-Q12. 为什么 deterministic demo 适合秋招现场演示？** 同输入必同输出：演示前彩排 = 演示现场结果；不怕紧张按错；不需要真硬件也不怕现场干扰；如果面试官要求再来一次，结果完全一致。
**PB-Q13. 为什么 Part B 仍然不需要真实硬件？** SimulatedSlave 替代了物理从站；CorruptCrc/DropResponse 替代了线路故障——整个 Demo 在纯软件层闭环，真硬件要到 T010 Serial。
**PB-Q14. T002~T008 是怎样串成完整产品链的？** CRC 校验（T002）→ 语义帧（T003）→ wire 编解码（T004）→ 模拟设备响应（T005）→ 故障注入（T006）→ 事务分析（T007A）→ 统计快照（T007B）→ QML Dashboard 展示（T008B）——从字节到用户可看，全链贯通。

### Part B Implementation Plan（下一阶段，22 步）

1. 给 AnalysisController 增加 `runDemoBatch()`
2. 增加 `clearDemo()`
3. 编排四条真实 Core scenario（复用 T005/T006/T007）
4. 生成 TransactionAnalysis batch
5. 生成 TransactionListEntry batch
6. summarizeTransactions
7. 更新 Controller properties/model
8. 扩展 QML status cards
9. 增加 Run / Clear
10. 完善 transaction delegate
11. UI-B01~B06 tests
12. RED
13. GREEN
14. clean build
15. full ctest
16. deploy_windows.bat
17. minimal-PATH smoke
18. Manual Demo Smoke
19. 文档归档
20. code commit
21. LKGC
22. docs backfill

**禁止**：QTimer、QThread、sleep、automatic polling、random、database、filesystem history、Replay、Serial、Agent、AI。禁止修改 T002~T007 Core 业务语义。
