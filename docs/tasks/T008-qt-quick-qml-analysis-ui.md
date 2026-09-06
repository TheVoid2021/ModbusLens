# T008 — Qt Quick / QML Analysis UI

> 状态：**IN PROGRESS**｜Part A（Qt Quick Migration + C++/QML Bridge）：**Learning / Test Design ✅（docs-only）→ Implementation ⬜**｜Part B（Analysis Dashboard + Deterministic Demo）：⬜ Not Started
> 前置确认：T007 DONE、LKGC = `0f3109a`。
> 架构依据：**ADR001**（最终 UI = Qt 6 + Qt Quick + QML + Qt Quick Controls；QMainWindow 仅为 bootstrap scaffold，T008 正式替换）。
> Part A Implementation 边界预告（未实现，禁止提前）：Simulator 自动轮询、Serial、Replay、QSerialPort、Agent、AI、database、chart framework、动画大工程、theme system、persistent settings、real-time timer、thread、networking 全部不做。

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

## Implementation

**未发生。** 本阶段 docs-only；`src/`、`tests/`、`CMakeLists.txt` 零改动。

## Files Changed（本阶段）

- 新增：`docs/tasks/T008-qt-quick-qml-analysis-ui.md`（本文件）、`docs/devlog/2026-09-06-T008-PartA-TestDesign.md`
- 修改：`docs/PROJECT_STATUS.md`（四段式状态）、`docs/BACKLOG.md`（T008 拆 Part A/B + M4 状态）、`docs/02_ARCHITECTURE.md`（依赖方向 + UI 层对齐 ADR001）
- 未改动：`src/`、`tests/`、`CMakeLists.txt`、presets

## Problems Encountered

无实现问题（docs-only）。范围事项：BACKLOG 原 T008 行为"主窗口、模式切换骨架、帧/事务/统计/报告视图"的粗粒度描述——已拆分 Part A（迁移+桥接）/ Part B（Dashboard+Demo）并在变更记录留痕。

## Solutions

T008 行改写为两 Part 范围；视图细节（帧列表等）随 Part B 真实数据一起落地。

## Verification（本阶段，docs-only）

```text
git diff --check        → 通过（无空白/行尾问题）
git diff --name-only    → 仅 docs/ 下文件；src/、tests/、CMakeLists.txt 未出现
```

## Result

Part A Learning / Test Design 完成：Part A/B 拆分、依赖方向定案、QML 模块/迁移计划定案、AnalysisController 与 TransactionListModel 设计（含 optional→hasX+value 与类型选择理由）、DTO/roles 定案、6 用例矩阵 + UI-I01（I02 记录不做）、18 题问答、18 步实施计划与 Manual UI Smoke 计划落库。**UI 未实现**；T008 整体 IN PROGRESS。

## Knowledge Learned

- **adapter 层是 QML 化的核心**：Controller/Model 把"纯 core 对象"翻译成"可绑定属性/role"，翻译规则（optional→hasX+value、数值→格式化）显式成文。
- **QML 是运行时语言**：它的"编译期"就是 load smoke——offscreen 加载验证是 QML 项目的最小自动化防线。
- **类型在边界处定案**：QML-facing 用 int/double/bool/QString，core 用 size_t/optional/enum——两边各自最优，翻译集中在 adapter。

## Potential Interview Questions

- 18 题见上；Implementation 阶段将补充：qt_add_qml_module 实际配置、offscreen 下 QML load 的坑、NOTIFY 信号的触发时机。

## Git Commit

- 本阶段提交信息：`T008(Part A): QML 迁移与桥接学习与测试设计（docs-only）`
- 哈希：见 `git log`（docs-only 不推进 LKGC；LKGC 保持 `0f3109a`）。