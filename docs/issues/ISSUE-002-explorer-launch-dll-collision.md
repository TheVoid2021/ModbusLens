# ISSUE-002: Explorer 启动失败——全局 PATH 旧 MinGW runtime 抢占（DLL collision）

> 状态：**RESOLVED ✅**（2026-09-06，用户从 Explorer 双击 build/deploy/ModbusLens.exe 确认）

## Symptom（现象）

用户从 Windows Explorer 直接双击启动 `build/debug/modbuslens.exe`，弹出：

> 无法定位程序输入点 `_ZNSt3pmr20get_default_resourceEv` 于动态链接库 `D:\QT\6.11.1\mingw_64\bin\Qt6Gui.dll`

应用无法启动。Manual Visual UI Smoke = FAIL / BLOCKED。

同一 exe 在开发终端（Git Bash + 临时前置 PATH）下启动正常、自动化测试全绿——问题仅在 Explorer 的 DLL 解析环境出现。

## Environment（环境）

- Windows 11，NTFS，多 MinGW 工具链共存（见 §Root Cause）
- 应用：T008 Part A 的 Qt Quick 构建（`build/debug/modbuslens.exe`，依赖 Qt6Core/Gui/Qml + libstdc++-6/libgcc_s_seh-1/libwinpthread-1）
- 复现：Explorer 双击，或任意**未前置正确 runtime 的终端**启动

## Root Cause（根因）

**Runtime toolchain collision**：Windows 按序搜索 DLL（应用目录 → System32 → … → PATH）。本机 PATH 中存在**多套异构 MinGW C++ runtime**，且旧版排在 Qt kit runtime 之前：

| DLL | PATH 首个命中 | 第二命中 | Qt 13.1 runtime |
| --- | --- | --- | --- |
| libstdc++-6.dll | `D:\Git\mingw64\bin`（Git 自带） | `D:\mingw64\bin`（**MinGW 8.1**，2018-05） | `D:\QT\Tools\mingw1310_64\bin` |
| libgcc_s_seh-1.dll | `D:\Git\mingw64\bin` | `D:\mingw64\bin`（8.1） | 同上 |
| libwinpthread-1.dll | `D:\Git\mingw64\bin` | `D:\Anaconda3\Library\mingw-w64\bin` | 同上 |
| g++.exe（参考） | `D:\mingw64\bin`（8.1） | — | 同上 |

`where` 真实输出（2026-09-06 采集，见 Evidence）显示 Qt 的 runtime 排在第三位。Explorer 启动时按此顺序解析，加载了**与 Qt 6.11.1 (MinGW 13.1) 不匹配的旧 libstdc++-6.dll**；其中（8.1 版，2018-05 构建）经 `objdump -p` 实测**缺失导出符号** `_ZNSt3pmr20get_default_resourceEv`（GCC 9+ 才有 std::pmr）→ 符号解析失败 → 报错弹窗。

精确符号导出对照（`objdump -p | grep -c "_ZNSt3pmr20get_default_resourceEv"`）：

| libstdc++-6.dll 来源 | 该符号 |
| --- | --- |
| `D:\Git\mingw64\bin` | 1（存在，但与 13.1 版本仍不匹配，属未定义混用） |
| `D:\mingw64\bin`（8.1） | **0（缺失——报错直接来源）** |
| `D:\QT\Tools\mingw1310_64\bin`（13.1） | 1 |
| `D:\QT\6.11.1\mingw_64\bin` | 1 |

（C:\Windows\System32 经检查无任何 MinGW runtime，排除系统目录污染。）

## Evidence（证据）

1. `where` 五连查真实输出（上表）；
2. `objdump -p` 精确符号对照（8.1 版 pmr 符号缺失 = 0）；
3. `CMakeCache.txt`：`CMAKE_CXX_COMPILER:UNINITIALIZED=D:/QT/Tools/mingw1310_64/bin/g++.exe`（MinGW 13.1）；
4. 13.1 runtime 三件套（libstdc++/libgcc/libwinpthread）在 `D:/QT/Tools/mingw1310_64/bin` 完整存在；
5. **临时 PATH 验证**：会话内前置 `D:/QT/6.11.1/mingw_64/bin + D:/QT/Tools/mingw1310_64/bin` 后启动 → 进程稳定存活（pid 32048）、窗口 "ModbusLens" 正常、可访问性树 22 元素完整（统计 0/—/空状态全正确）、stderr 无 QML warning。

## Fix（修复方向）

- **本项目正确解**：交付/调试时将匹配 runtime 随应用部署——`windeployqt` 或把 13.1 三件套复制到**应用目录**（DLL 搜索首位，天然屏蔽 PATH 污染）。此属部署配置，待用户确认后作为独立小任务落地（可能引入一个 `windeployqt` CMake 后置步骤或发布脚本）。
- **开发机治理（建议，未自动执行）**：调整用户 PATH 让 Qt/13.1 runtime 先于旧 MinGW；或移除 D:\mingw64\bin——影响全局其他工具，必须由用户自行决定。

## Why automated tests passed（为什么自动化全绿却 Explorer 失败）

ctest/终端内运行时，开发会话的 PATH 恰好（或经临时前置）让**正确 runtime** 先被解析；Explorer 使用系统全局 PATH 顺序，旧 runtime 抢占。**测试环境与最终启动环境的 DLL 解析顺序不同**——自动化无法覆盖"干净 Explorer 环境"这一维度，这正是 Manual UI Smoke 存在的意义。

## Why Explorer launch failed（为什么偏偏 Explorer 失败）

同上：Explorer 子进程继承系统全局 PATH；D:\Git\mingw64\bin 与 D:\mingw64\bin 排在 Qt runtime 之前。Git Bash 会话中因 MSYS 前置与临时 export 掩盖了该问题（这也解释了 T005–T007 期间所有终端内启动均正常）。

## Interview takeaway

1. **Windows GUI 程序的部署边界**：exe 能链接通过 ≠ 能启动；runtime DLL 解析依赖启动环境，交付必须以"目标环境能解析"为准（应用目录部署 / windeployqt）。
2. **同名 DLL 多套共存是 Windows 生态经典事故**：`where` 顺序即故障顺序；`objdump -p` 精确核对缺失符号；"符号缺失报错指向 Qt DLL"不代表 Qt 坏，而是它的依赖被换了。
3. **Manual Smoke 的价值实证**：自动化 14/14 全绿仍挡不住启动环境问题——分层验收（自动化 + 人工真实启动）缺一不可。
4. 诊断路径：报错符号 demangle（`std::pmr::get_default_resource`）→ 推断 GCC 版本差 → `where` 顺序 + `objdump` 导出对照 → 临时 PATH 单变量验证。

## Fix 验证与关闭（2026-09-06，T008.1 完成）

**修复已实施并验证（T008.1）**：

- `scripts/deploy_windows.bat`：从 `build/debug/CMakeCache.txt` 自动解析编译器 bin 与 Qt bin（无硬编码路径），创建干净的 `build/deploy/`，复制 exe → windeployqt（--qmldir 扫描 src/ui/qml）→ **强制从编译器 bin 覆盖三件套 runtime** → 部署应用 QML 模块（ModbusLens/qmldir + Main.qml，修复"类型 Main 不在模块中"的加载失败）→ 关键文件校验。
- Runtime Provenance = **VERIFIED**：deploy 三件套 SHA256 与编译器 bin 完全一致（windeployqt 部署的版本本就正确，脚本仍按规则强制覆盖并复核）。
- **Minimal-PATH smoke = PASS**：`PATH=C:\Windows\System32;C:\Windows` 下 `ModbusLens.exe --qml-smoke-test` exit=0；普通运行（无 smoke 参数）进程存活验证 PASS。
- 部署脚本可重复生成验证 = PASS（清空 build/deploy 后重跑脚本 → 再验 smoke 通过）。
- 业务代码修改 = **NONE**（仅新增 scripts/deploy_windows.bat 与文档）。

**用户 Explorer 双击确认 = PASS（2026-09-06）**：用户从 Windows Explorer 直接打开 `build/deploy/ModbusLens.exe`，确认不再出现入口点错误、不再出现 Qt6Core/Gui/Qml 等 DLL 入口点错误、窗口正常打开。**ISSUE-002 = RESOLVED ✅**