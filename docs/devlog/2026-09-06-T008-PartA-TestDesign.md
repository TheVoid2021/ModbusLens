# Devlog — 2026-09-06（T008 Part A Learning: Qt Quick 迁移与桥接测试设计）

- 范围拆分：Part A = Qt Quick 迁移 + C++/QML 桥接（QGuiApplication/QQmlApplicationEngine/qt_add_qml_module/Main.qml/AnalysisController/TransactionListModel/load smoke）；Part B = Analysis Dashboard + Deterministic Demo（T005/T006/T007 数据填充）。轮询/Serial/Replay/networking 等全部禁止。
- 依赖方向定案并写入架构：QML → Controller/Model → modbuslens_core，严禁反向；Qt 类型仅允许在 app/ui adapter 层。
- 迁移计划定案：main.cpp 切 QGuiApplication + loadFromModule("ModbusLens","Main")；CMake Widgets→Gui/Qml/Quick/QuickControls2；QMainWindow bootstrap 删除。
- 桥接设计：AnalysisController（optional→hasX+value，0.0 仅为占位、QML 先看 hasX；计数用 int 而非 qsizetype 的理由记录）；TransactionListModel（QAbstractListModel，7 roles，DTO 组合 Request metadata + TransactionAnalysis，不反向改 T007；setEntries 非 Q_INVOKABLE）。
- 测试设计：UI-A01~A06（初始计数/optional 初始态/空 model/roles/exception entry/替换）+ UI-I01 QML load smoke（offscreen）；**UI-I02 决定不做**（文本/像素查找脆弱，记录理由）。
- Manual UI Smoke 计划：GREEN 后必须实际启动确认窗口/标题/初始统计/—/空状态/无 QML warning，记录 PASS/FAIL。
- 本阶段 docs-only，src/、tests/、CMakeLists.txt 未改动，LKGC 保持 `0f3109a`。
- 任务档案：[T008](tasks/T008-qt-quick-qml-analysis-ui.md)