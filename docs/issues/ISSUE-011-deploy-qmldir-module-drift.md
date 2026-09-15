# ISSUE-011: 部署目录中手写 qmldir 与真实模块漂移 — deploy 候选加载缺失新组件类型

- **日期**：2026-09-15
- **关联任务**：T016 M9-A Phase 2 验证环节

## 现象

更新 `scripts/deploy_windows.bat` 之前构建的 deploy 候选，用 `--qml-smoke-test` 冒烟时：

```text
QQmlApplicationEngine failed to load component
file:///E:/desktop/ModbusLens/build/deploy/ModbusLens/Main.qml:292:9: SectionHeader is not a type
```

## 影响

deploy 候选（对外分发形态）无法启动 UI——M9-A Phase 2 引入组件后，部署包落回旧模块描述，`Main.qml` 引用的 `SectionHeader`/`PanelCard`/`StatCard`/`AppButton` 全部缺失。

## 复现步骤

1. M9-A Phase 2 代码就绪后运行 `scripts/deploy_windows.bat`。
2. 在 deploy 目录以 `--qml-smoke-test` 启动 `ModbusLens.exe`。

## 定位过程

- 错误 URL 是 `file:///.../build/deploy/ModbusLens/Main.qml`——引擎加载的是**磁盘上的文件副本**而非 exe 内嵌 qrc 副本。
- 检查 `build/deploy/ModbusLens/`：仅含 `Main.qml` + 内容只有 `Main 1.0 Main.qml` 的 qmldir。而 debug 构建树的生成模块（`build/debug/ModbusLens/`）含 qmldir（带 `prefer :/ModbusLens/` 与全部 6 个类型条目）+ src 镜像树。
- `prefer :/ModbusLens/` 缺失 → 相对 URL（`Main.qml`）被解析到磁盘文件；手写文件清单只覆盖 Main → 新组件类型在部署形态下无法解析。

## 根因

`deploy_windows.bat`（T008.1 时代）在部署目录**手写**了一个迷你 qmldir 与单文件模块副本，与 `qt_add_qml_module` 生成的模块描述**各自维护**。模块从 1 个类型长到 6 个类型时，这份第二清单漂移，且缺少 `prefer :/ModbusLens/` 使引擎走了磁盘解析路径。

## 解决方案

单一机制：step 7 改为整目录复制**生成的**模块（`xcopy /e /i /y "%BUILD_DIR%\ModbusLens" "%DEPLOY_DIR%\ModbusLens\"`），删除手写 qmldir 生成逻辑；step 8 校验清单同步为生成树文件（`ModbusLens\qmldir`、`src\ui\qml\Main.qml`、`DS\DesignSystem.qml`、4 个 components）。生成 qmldir 自带的 `prefer :/ModbusLens/` 同时把解析拉回 exe 内嵌资源（单一事实源）。

## 验证

- `deploy_windows.bat` 输出 `[OK] Deployment directory ready`。
- deploy 候选在**不带 Qt 开发环境 PATH** 的 PowerShell 会话中运行 `--qml-smoke-test`：`EXITCODE=0`，stderr 为空。

## 教训

任何“从生成物复制一份手工维护的清单”都会在模块演进时漂移；部署脚本应当复制构建系统生成的模块整体，而不是维护第二份描述。单一机制原则从 CMake 扩展到部署管线。