# T001 — 项目引导：骨架、文档体系与最小 Qt6 应用

## Goal

建立 ModbusLens 的工程骨架与开发文档体系，用最小的 CMake + Qt6 工程证明"可构建、可测试、可演示"三条生命线成立，为后续所有任务（协议核心、三种模式、诊断、Agent）提供统一的平台与规约。本任务不实现任何 Modbus 业务功能。

## Background

- 秋招项目需要一个完整的 C++20 + Qt6 工程；在写任何业务代码前，必须先解决三个元问题：**可追溯**（开发全程留档）、**可移交**（状态只存仓库 Markdown，任何 AI Coding 平台可接手）、**三模式复用**（Simulator/Serial/Replay 共享核心——这是后续架构的约束前提）。
- 本机环境为 Windows + Qt 6.11.1（MinGW kit，Qt 自带 CMake/Ninja/MinGW 工具链），但系统 PATH 存在过旧 g++ 8.1 与 Anaconda Qt5，容易造成工具链污染。

## Technical Decisions

| 决策 | 选择 | 理由 |
| --- | --- | --- |
| 构建系统 | CMake ≥ 3.21 + **Presets** | 命令可复现、机器差异可隔离，移交成本最低 |
| Preset 分层 | 提交 `CMakePresets.json`（通用）/ 本地 `CMakeUserPresets.json`（gitignore，注入本机 Qt 路径与工具链） | 仓库不污染他人环境，本机一条命令可用 |
| 编译器/生成器 | Qt 自带 MinGW g++ 13.1 + Ninja（经 local preset 显式注入） | 与 Qt 6.11.1 MinGW kit ABI 严格匹配；规避 PATH 中的脏工具链 |
| 最小应用 | 单一 `QMainWindow`（无自定义窗口类） | 任务边界内最小实现，UI 分层留给 T009 |
| 测试框架 | QtTest + CTest；测试强制 `QT_QPA_PLATFORM=offscreen` | 无显示器环境可跑，CI 友好；QtTest 与工程同生态 |
| 文档体系 | AGENTS.md 规约 + 数字前缀文档 + tasks/issues/adr/devlog 档案区 | 满足三大核心要求的执行力载体 |
| 里程碑划分 | M1–M8 | 让 BACKLOG 有序、面试有节奏 |

## Implementation

1. 目录骨架：`src/ tests/ demo/ docs/{tasks,issues,adr,devlog}`。
2. `CMakeLists.txt`：C++20 强制；`include(CTest)`（BUILD_TESTING 默认 ON）；`qt_standard_project_setup()`（AUTOMOC）；两目标 `modbuslens`（GUI 主程序，WIN32 无控制台）与 `modbuslens_tests`（QtTest 冒烟）；GCC/Clang 开 `-Wall -Wextra`。
3. `src/main.cpp`：QApplication + 空 QMainWindow（标题 ModbusLens，1024×720）。
4. `tests/test_smoke.cpp`：QtTest 两用例——默认主窗口无中心部件、窗口标题正确；`QTEST_MAIN` + offscreen 环境变量。
5. Presets：通用 `debug/release/default`（不绑定生成器与 Qt 路径）；本机 `debug-local/release-local`（绑定 Ninja + 注入工具链 PATH + `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`）。
6. 文档：AGENTS.md（13 条工作纪律 + 任务流程 + 档案模板）、README、charter/requirements/architecture/modbus-learning/test-strategy/demo-guide、PROJECT_STATUS、BACKLOG、ENVIRONMENT、INTERVIEW_NOTES、本档案、devlog。
7. Git 初始化并提交（main 分支，提交信息 `T001: ...`）。

## Files Changed

新增：

- `.gitignore`、`CMakeLists.txt`、`CMakePresets.json`（提交）；`CMakeUserPresets.json`（gitignore，本机专用不提交）
- `src/main.cpp`、`tests/test_smoke.cpp`
- `AGENTS.md`、`README.md`
- `docs/00_PROJECT_CHARTER.md`、`01_REQUIREMENTS.md`、`02_ARCHITECTURE.md`、`03_MODBUS_LEARNING.md`、`04_TEST_STRATEGY.md`、`05_DEMO_GUIDIDE.md`、`PROJECT_STATUS.md`、`BACKLOG.md`、`ENVIRONMENT.md`、`INTERVIEW_NOTES.md`
- `docs/tasks/T001-project-bootstrap.md`（本文件）
- `docs/devlog/2026-09-05-bootstrap.md`、`demo/README.md`、`docs/{issues,adr,devlog}/.gitkeep`

## Problems Encountered

1. 首次构建时 AutoMoc 阶段打印 qtlicd 许可证错误：*"Cannot acquire license to use qtframework … set QTFRAMEWORK_BYPASS_LICENSE_CHECK=1"*（本机 Qt 6.11 安装的 License Service 版本不匹配）。构建虽最终完成，但输出有噪音且有阻塞风险。
2. 系统 PATH 混有 g++ 8.1.0（D:/mingw64）与 Anaconda Qt5 qmake，直接构建存在 ABI 不匹配风险。
3. configure 输出 `Could NOT find WrapVulkanHeaders`。

## Solutions

1. 按官方提示在 CMakeUserPresets.json 的 environment 中注入 `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`；重构建后 grep 确认无 license/warning/error 输出。
2. 通过 local preset 显式指定 `CMAKE_CXX_COMPILER=D:/QT/Tools/mingw1310_64/bin/g++.exe` 与 `CMAKE_PREFIX_PATH=D:/QT/6.11.1/mingw_64`，并注入工具链 PATH；configure 输出确认编译器来自 Qt 自带 MinGW 13.1。
3. WrapVulkanHeaders 仅影响 QtQuick/RHI 场景，当前 Widgets 无关，记录观察（PROJECT_STATUS K3），不处理。

## Verification

```text
# 工具链
cmake 3.30.5 | ninja 1.12.1 | g++ 13.1.0 (MinGW-Builds posix-seh) | Qt 6.11.1 mingw_64

$ cmake --preset debug-local          → Configuring done (12.7s) / Generating done (0.9s)
$ cmake --build --preset debug-local  → [8/8] Linking CXX executable modbuslens_tests.exe
   （许可证修复后重构建：grep -iE "license|warning|error" 无任何输出 — 零警告）
$ ctest --preset debug-local
   100% tests passed, 0 tests failed out of 1   （smoke 内含 2 个 QtTest 用例）

$ QT_QPA_PLATFORM=offscreen timeout 5 ./build/debug/modbuslens.exe
   → exit=124：事件循环正常跑满 5 秒被 timeout 终止（应用启动无崩溃）

产物：build/debug/modbuslens.exe、build/debug/modbuslens_tests.exe
```

## Result

✅ 达成全部验收目标：
- 骨架目录与文档体系按规格建立（含用户要求的全部文件与结构）；
- 最小 Qt6 工程可 configure、可 build（零警告）、可 test（1/1 通过）；
- 主程序离屏启动验证通过；
- Git 提交完成，LKGC 已记录（见 Git Commit）；
- 未越界：未实现 CRC/Modbus/串口/模拟器/LLM/复杂 UI。

## Knowledge Learned

- Qt 6 新版权检查（qtlicd）会在 AutoMoc 阶段介入；开源/无法注册 LicenseService 场景下 `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1` 是官方给出的构建期开关。
- `QT_QPA_PLATFORM=offscreen` 让 GUI 程序在无显示器环境可启动、可测试——GUI 项目 CI 化的关键。
- CMake Presets 的"通用 + 机器私有"双层结构是多人/跨机器/AI 协作的标准姿势。
- 工具链验证要点：永远以 configure 输出中的编译器探测结果为准，而不是 PATH 第一个 `g++`。

## Potential Interview Questions

见 [INTERVIEW_NOTES §3 T001](../INTERVIEW_NOTES.md)——包括：为什么 C++20 / 为什么 Presets+CTest / offscreen 的作用 / 文档体系的价值 / moc 许可证问题的定位过程 / 多工具链环境下的构建正确性。

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| 主提交 | `PENDING` | `T001: 项目引导 — 骨架、文档体系、最小 Qt6 应用` |
| 回填提交 | `PENDING` | `T001: 回填任务记录（LKGC 与提交哈希）` |