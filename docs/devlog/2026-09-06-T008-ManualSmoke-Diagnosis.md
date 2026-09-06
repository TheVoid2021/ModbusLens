# Devlog — 2026-09-06（T008 Part A：Manual UI Smoke 阻塞诊断 → ISSUE-002）

- 用户从 Explorer 直接启动 build/debug/modbuslens.exe 失败：无法定位输入点 `_ZNSt3pmr20get_default_resourceEv` 于 Qt6Gui.dll。Manual Visual UI Smoke = FAIL / BLOCKED。
- 诊断（按指令逐项，未改任何系统配置/业务代码）：
  - `where` 五连查：libstdc++-6.dll/libgcc/libwinpthread 首个命中均为 **D:\Git\mingw64\bin**（Git 自带 runtime），第二为 **D:\mingw64\bin**（MinGW 8.1，2018-05）；Qt 13.1 runtime 排第三；g++ 唯一命中 = 8.1。
  - CMakeCache：`CMAKE_CXX_COMPILER=D:/QT/Tools/mingw1310_64/bin/g++.exe`（13.1）；其目录三件套 runtime 完整。
  - `objdump -p` 精确对照：`_ZNSt3pmr20get_default_resourceEv` 在 8.1 版 **=0（缺失）**、Git 版/13.1 版/Qt bin 版 =1。
  - System32 无 MinGW runtime（排除系统目录污染）。
  - **临时 PATH 验证**（Qt bin + 13.1 bin 会话前置，不改系统）：应用正常启动、进程 32048 存活、窗口与 a11y 树 22 元素完整、stderr 无 QML warning → **根因坐实 = Runtime toolchain collision**。
- 结论：非 Core bug、非 QML binding bug、非迁移代码 bug；为开发机 DLL 解析环境问题。修复方向 = 部署期 runtime 随应用部署（windeployqt/应用目录三件套），待用户确认后立项；未动系统 PATH/未删旧 MinGW/未复制 DLL/未改 src/CMake。
- 归档：ISSUE-002 建档（Symptom/Root Cause/Evidence/Fix/takeaway）；T008 档案 Manual Smoke 段追记 FAIL/BLOCKED→WAITING FOR USER（保留先前的 a11y 初验记录）；PROJECT_STATUS（Current Phase=WAITING FOR USER、K4 新增、LKGC 维持 76030a2 不变）。
- 应用当前以正确 runtime 保持运行中（pid 32048），等待用户视觉确认 12 项 checklist。
- Issue：[ISSUE-002](issues/ISSUE-002-explorer-launch-dll-collision.md)