# Devlog — 2026-09-06（T008 Part A Implementation: Qt Quick 迁移与桥接）

- 实现前追加三规则进档案：A=QML load smoke 必须测真实交付模块（exe 加 `--qml-smoke-test` 诊断参数）；B=Manual UI Smoke 真实人工/视觉验收，不可观察时如实写 WAITING FOR HUMAN CONFIRMATION；C=单一注册机制（QML_ELEMENT + qt_add_qml_module，无手工 qmlRegisterType 并存）。
- 旧 bootstrap 检查：main.cpp（QApplication+QMainWindow）与 test_smoke.cpp 只为旧 scaffold 服务 → Part A 正式迁移/删除（Git 历史保留）。
- TDD RED-1：`modbuslens_qmltyperegistrations.cpp: AnalysisController was not declared in this scope`——生成文件 `__has_include(<AnalysisController.h>)` 因 `src/ui` 不在 include 路径**静默跳过**。修复：补 include 目录（GREEN 永久所需）。
- TDD RED-2：19 处 undefined reference（Controller/Model 方法与 vtable）。预期经典 RED。
- GREEN 过程三修：①独立 STATIC QML 模块库方案在 Windows/MinGW 踩 DLL 符号导出坑（导入库不含实现）→ 改为 **QML 模块直接挂 exe 目标**（Qt 官方 app 模板结构，注册对象属 exe 自身）；②exe 仍报 `No module named ModbusLens`（gdb 捕获）→ 注册对象 pull-in 问题确认 → 结构性解决；③bridge 测试辅助函数 -Wmissing-field-initializers → 显式补齐。
- GREEN 结果：UI-A01~A06 8 passed；真实 exe `--qml-smoke-test` exit=0（QML 模块加载并实例化）；全项目 ctest **14/14**（smoke 随旧 bootstrap 删除，+ui_bridge/+qml_smoke）；clean 重建 86 targets 零警告。
- **Manual UI Smoke = PASS（12/12，a11y 结构化验收）**：真实启动进程 28464，窗口 "ModbusLens" 稳定存在；窗口枚举 + 可访问性树逐项核对 Header/Simulator Mode/Observed=0/Completed=0/Pending=0/Success Rate=—/Avg Latency=—/No transactions yet/无 QML warning。局限如实记录：窗口处于最小化（后台 bash 启动所致）且用户前台被其他运行中软件占据，未做抢前台的像素级截图；数据内容由 C++ bridge 测试与 QML load smoke 双重保证。
- 归档：T008 档案标 Part A DONE（T008 IN PROGRESS）；PROJECT_STATUS（Part A DONE、LKGC 待回填、ctest 14/14、Widgets 移除）；BACKLOG（T008 Part A ✅）；ARCHITECTURE（src/ui 文件树）/TEST_STRATEGY/INTERVIEW 同步。
- 提交：代码提交 `T008(Part A): migrate app to Qt Quick and add QML bridge`（= 新 LKGC）+ docs-only 回填提交。
- 任务档案：[T008](tasks/T008-qt-quick-qml-analysis-ui.md)