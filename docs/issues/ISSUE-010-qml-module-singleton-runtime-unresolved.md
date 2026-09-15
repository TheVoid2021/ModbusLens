# ISSUE-010: QML 模块单例（pragma Singleton）在 exe 内嵌 qrc 模块中运行时全部解析为 "DS is not defined"

- **日期**：2026-09-15
- **关联任务**：T016 M9-A Phase 2（DesignSystem tokens + 首批组件迁移）

## 现象

按照 Qt 6.11 文档实现的 QML 模块单例（`pragma Singleton` + `set_source_files_properties(... QT_QML_SINGLETON_TYPE TRUE)`）自检如下：

- 生成的 `qmldir` 正确包含 `singleton DesignSystem 1.0 src/ui/qml/DS/DesignSystem.qml`（已验证生成物）。
- `QML_IMPORT_TRACE` 显示类型解析成功：`resolveType: Main.qml "DesignSystem" => "DesignSystem" QUrl("qrc:/ModbusLens/src/ui/qml/DS/DesignSystem.qml") TYPE/URL-SINGLETON`，且 DesignSystem.qml 自身的属性（`color` 等）编译解析正常。
- 但运行时 Main.qml/AppButton.qml/StatCard.qml/PanelCard.qml/SectionHeader.qml 中每一处 `DS.*` 引用都抛出 `ReferenceError: DS is not defined`（约 80 条），窗口对象仍能创建（EXITCODE=0，视觉为全空 token 值）。

## 影响

Module singleton 形态在本项目不可用——继续使用会导致全部 token 失效、样式退化。阻塞 M9-A Phase 2 实施（DesignSystem 是全组件 token 源）。

## 复现步骤

1. Qt 6.11.1 + mingw，`qt_add_qml_module` 内嵌 qrc 模块（exe-attached，无 plugin）。
2. DesignSystem.qml 首行 `pragma Singleton`；对源码文件设置 `QT_QML_SINGLETON_TYPE TRUE`（在模块创建前）。
3. 构建、运行 `modbuslens.exe --qml-smoke-test`（QT_QPA_PLATFORM=offscreen），stderr 观察 `ReferenceError: DS is not defined`。
4. 全量 qmlcachegen 重生成（touch 全部 QML 后重建）**不能消除**该错误。

## 定位过程

1. 首次现象是一个更有迷惑性的间接失败：Git Bash 中直接运行 exe 得到 exit 127、无任何输出——Windows 负退出码（-1）在 MSYS 中被映射为 127，掩盖了真实信号。改用 PowerShell `Start-Process -PassThru` 拿到原始 `ExitCode=-1`，再配合 `QT_ASSUME_STDERR_HAS_CONSOLE=1` 强制 GUI 子系统进程把 qWarning/QML 错误写入重定向 stderr，才拿到 `ReferenceError` 与后续真实错误文本。
2. 初版还叠加了 `Label is not a type`（SectionHeader/StatCard 缺失 `import QtQuick.Controls`——Label 定义在 Controls 而非 QtQuick）与 `card is not defined`（组件内部 id 对实例化方不可见）。逐项修复后仍剩 DS 解析失败。
3. 验证 qmldir 三步（pragma / source property / 生成行）全部满足后仍失败，`QML_IMPORT_TRACE` 证明类型层已按 SINGLETON 解析。
4. 排查 qmlcache（AOT）：强制全量重生成（全部 QML touch 重建、qmldir 重新生成、重新链接），错误不变——排除缓存陈旧假设。

## 根因（诚实声明：机理级根因未完全隔离）

- **未隔离部分**：三重机制全部就位后运行时查找仍失败，指向 exe-attached 内嵌 qrc 模块 + composite singleton + AOT qml cache 组合下的已知问题族（社区同族报告：单例查找在编译缓存加载路径中未注入文档作用域）。受里程碑边界约束（本轮目标 = token 集中化 + 组件迁移，而非 vendoring Qt internals），未继续深挖 Qt 内部实现。
- **已确认的教训**：QML 模块单例的正确生成需要**三个环节全部满足**（文件内 `pragma Singleton`；模块创建前对该文件设 `QT_QML_SINGLETON_TYPE TRUE`；生成 qmldir 出现 `singleton` 行）——缺一即静默降级或运行期失败，且**三者齐备仍不保证**本项目形态可用。

## 解决方案

改用 Qt 官方同样认可的稳定形态：**engine root context property**。

- `src/ui/qml/DS/DesignSystem.qml`：移除 `pragma Singleton`（保留为普通 QtObject，token 值一字不改）。
- `src/main.cpp`：`QQmlComponent` 从 `qrc:/ModbusLens/src/ui/qml/DS/DesignSystem.qml` 创建一次，`QQmlEngine::setObjectOwnership(ds, QQmlEngine::CppOwnership)` + 挂 engine 为 parent，`rootContext()->setContextProperty("DS", ds)`，失败即 `return -1`（带错误串）。
- `CMakeLists.txt`：移除 `QT_QML_SINGLETON_TYPE` 属性块，留下说明注释指向本 Issue。

## 验证

- Debug exe smoke：`EXITCODE=0`，stderr 仅剩无字体的环境提示（QFontDatabase note），**0 条 ReferenceError**。
- 全量 ctest：**24/24 通过**（含起效的 qml_smoke）。
- deploy 候选 smoke：`EXITCODE=0`（另见 ISSUE-011 的部署模块漂移修复）。

## 教训

1. GUI-subsystem Windows 程序诊断：负退出码会被 MSYS 映射成 127、stderr 默认不与重定向管道连通——PowerShell 拿原始 ExitCode + `QT_ASSUME_STDERR_HAS_CONSOLE=1` 是可靠取证组合。
2. "文档说了这样就行"≠本项目形态可行：实测优先，方案在验证证据前不落定。
3. 记录"未隔离"比编一个根因更重要——本 Issue 的机理部分明确标注为未完全隔离。