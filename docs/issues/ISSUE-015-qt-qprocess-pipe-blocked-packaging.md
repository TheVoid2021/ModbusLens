# ISSUE-015: 本机 Qt QProcess 无法派生带管道的子进程 — windeployqt / canonical packaging 全链路阻塞

- **日期**：2026-09-23
- **关联任务**：T022 M10-F Corrected Portable Artifact Refresh（本轮阻塞）
- **严重度**：BLOCKER（环境级，非产品缺陷；阻断 `scripts/make_package.py` 与 `scripts/deploy_windows.bat`）
- **V2 Trace 字段**：Observed ✅ / Expected ✅ / Evidence ✅ / Root Cause ✅ / Fix ✅ / Verification ✅ / Regression Protection ✅

## Observed（现象）

执行 M10-F 修正后的 fresh portable artifact 刷新时，retention move 与 freshness oracle 均正常，但 canonical packaging 在 deploy 步骤失败：

```text
make_package FAIL: Release deploy failed: [ERROR] windeployqt failed with exit code 1
Unable to query qtpaths: Error running binary qtpaths: pipe:
```

`windeployqt` 在**最早阶段**（`Running: qtpaths -query`）即失败，因此从未复制任何部署文件。`--dir` 强制部署同样产出 0 个文件：

```text
windeployqt --dir build/_wdqprobe build/release/ModbusLens.exe
Unable to query qtpaths: Error running binary qtpaths: pipe:
EXIT=1        # 部署文件数 = 0
```

## Expected（期望行为）

```text
1. windeployqt 应能通过 QProcess 调起 qtpaths -query（rc=0）取得 Qt 安装布局，
   然后正常复制 platform/imageformats/QML 模块等运行期依赖。
2. canonical `make_package.py build/release build/release/deploy` 应 exit 0，
   产出 staging → manifest → ZIP → fresh extract → 外部 CWD 运行全部通过。
3. 部署必须是 windeployqt 的真实产物（provenance 契约），
   以便 A/B/C/D identity（exe full SHA-256 四方一致）具备可验证意义。
```

实测 1 与 2 均未达成；3 因此**无法在不破坏契约的前提下达成**（见"解决方案—否决方案"）。

## 影响

- canonical `make_package.py` 无法完成 → 无法产出 corrected portable ZIP。
- `scripts/deploy_windows.bat` 同样依赖 `windeployqt`，同样不可用。
- 因此 M10-F 的 portable gates（在新 D 上）与 fresh Human #10/#11 replay **无法在本机当前状态下执行**。
- 已完成的仓库内验证不受影响：Debug/Release CTest 36/36、QML 六门禁、freshness oracle、commit 完整性审查均 PASS。

## 复现步骤

1. `D:\QT\6.11.1\mingw_64\bin\windeployqt.exe --version`
2. 或 `D:\QT\6.11.1\mingw_64\bin\windeployqt.exe --dir <empty> build\release\ModbusLens.exe`
3. 或 `python scripts/make_package.py build/release build/release/deploy`

## 定位过程

关键判别：把 `windeployqt` 与 **Qt / ModbusLens 完全无关**的最小 Qt QProcess 程序分开测试，以区分"产品缺陷"与"环境缺陷"。

**证据 1 — `qtpaths` 自身完全正常**（因此不是 Qt 安装损坏）：

```text
D:\QT\6.11.1\mingw_64\bin\qtpaths.exe -query        → rc=0，输出完整 25 行 QT_INSTALL_* 表
D:\QT\6.11.1\mingw_64\bin\qtpaths.exe --qt-version  → 6.11.1
```

**证据 2 — 工程外最小 QProcess 探针**（`build/_qprocess_probe.cpp`，仅链 Qt6Core，与 windeployqt / ModbusLens 无关）：

```text
waitForStarted = false
error = 0 (pipe: 系统找不到指定的文件。)
```

**证据 3 — 通道模式 / 分离启动探针**（`build/_qprocess_probe2.cpp`）：

```text
A default-pipes: started=false error=0 (pipe: 系统找不到指定的文件。)
B forwarded    : started=false error=0 (pipe: 系统找不到指定的文件。)
C startDetached: ok=true  error=5 (Unknown error)     ← 仅 startDetached 成功
```

**证据 4 — 三重父子链一致**：Git Bash、原生 PowerShell 父进程、以及探针自身，三者均得到同一失败；`dangerouslyDisableSandbox` 沙箱外重跑结果**完全相同**（故非沙箱策略）。

**证据 5 — windeployqt `--verbose 2` 的原始行**：

```text
Running: qtpaths -query
QProcess: CreateFile failed. (所有的管道范例都在使用中。)      ← ERROR_PIPE_BUSY
Unable to query qtpaths: Error running binary qtpaths: pipe:
```

**证据 6 — 独立管道微基准**（ctypes，绕过 Qt 完整重建 Qt 的命名管道时序）：

```text
server CreateNamedPipeW(OUTBOUND)           → ok, err=0
client CreateFileW(name, GENERIC_READ)      → FAIL, err=231 (ERROR_PIPE_BUSY)
server CreateNamedPipeW(INBOUND)            → ok, err=0
client CreateFileW(name, GENERIC_WRITE)     → ok, err=0
server CreateNamedPipeW(DUPLEX)             → ok, err=0
client CreateFileW(name, READ|WRITE)        → ok, err=0
```

即：**服务端管道总能建立，但"服务端 OUTBOUND + 客户端 GENERIC_READ"这一 Qt `QWindowsPipeWriter` 实际使用的配对**总是失败（`ERROR_PIPE_BUSY`）；`INBOUND` 与 `DUPLEX` 配对正常。用随机 UUID 管道名重复 4 次结果一致（排除名称冲突假设）；`CreatePipe`（匿名管道）正常；`CreateProcess`（`startDetached`）正常。

**排除的假设**：Qt 安装损坏（`qtpaths`/`qmake`/`qmlformat`/`qmlimportscanner` 均正常）、PATH 遮蔽（`which -a qtpaths` 曾暴露 Anaconda Qt 5.15.2 在前，已用干净 PATH 复核，失败不变）、非 ASCII 的 TEMP 路径（改用 `C:\MLtmp` 不变）、`QT_INSTALL_*` 环境变量缺失（显式注入全部 25 项后不变）、沙箱策略（沙箱外不变）、进程/句柄泄漏（无遗留 qt 进程，管道命名空间仅 282 项且无冲突）、二进制变体（`windeployqt6.exe`、msvc2022_64、QtDesignStudio 副本同样失败，其中 QtDesignStudio 6.8.7 副本因走 `qconfig.pri` 路径而"看似成功"，未使用 `QProcess`）。

## 根因（Root Cause）

本 Windows 会话（Windows 10.0.26200.9457）无法完成 Qt 6.11.1 `QProcess` 命名管道客户端的建立：以 `PIPE_ACCESS_OUTBOUND` 建立服务端后，同进程/子进程以 `GENERIC_READ` 连接返回 `ERROR_PIPE_BUSY`。Qt 的 `QWindowsPipeWriter` 依赖该配对转发子进程 stdout/stderr，因此**任何 QProcess 管道通道**（含 `ForwardedChannels` 之外的默认模式；`ForwardedChannels` 在 Windows 上仍需管道承载）都失败；只有不建立管道的 `startDetached` 成功。

`windeployqt` 在启动时用 `QProcess` 调 `qtpaths -query` 读取 Qt 安装布局，因此在该环境下必然失败，且失败发生在任何文件复制之前。

这是一项**环境/宿主缺陷**，与 ModbusLens 代码、CMake 配置、打包脚本逻辑均无关——今日 08:32 与 12:51 的成功部署已证明本机此前可用。

## 解决方案（Fix）

**本轮采取的 Fix**：不实施任何产品/脚本改动，而是
(1) 把本轮全部残缺产物以整目录 Move 方式 safe-retention；
(2) 按 V2 Trace 规约建档本 Issue；
(3) 同步 T022 §ZE / PROJECT_STATUS / BACKLOG / devlog；
(4) 把 M10-F 明确置为 **HOLD / PENDING FRESH HUMAN #10/#11** 并给出人工解除路径。
**理由**：根因在环境而非仓库；在产品侧"修复"一个环境缺陷只会制造伪证据。

**本轮未采用的方案（明确否决）**：

- 不得手工拼装部署树以冒充 `windeployqt` 产物 —— 会破坏"部署树 provenance = windeployqt + 编译器 bin 三件套"这一被冻结契约，并使 A/B/C/D identity 失去意义。
- 不得复用陈旧的 `build/package` / ZIP / deploy 树 —— 它们早于 FC16 修正（`build/release/ModbusLens.exe` 15:56 含 R17 标记；陈旧 deploy 08:32 与 ZIP 12:51 均**不含** R17），复用即等于用修正前的产品冒充修正后（ZIP 非 identity criterion，但陈旧 exe 是）。
- 不得修改 DPI/沙箱开关绕过 —— 已证沙箱内外一致，非策略问题。
- 不得为了让打包"成功"而删除既有测试或放宽 `REQUIRED_FILES` —— 属 V2 明令禁止。

**待人工执行的解除路径**（任选其一，随后重跑 `scripts/make_package.py`）：

1. 结束阻塞命名管道能力的宿主会话（重启本机 / 注销重登 / 重启 WorkBuddy 宿主进程），使 Windows 命名管道命名空间与实例计数恢复；这对本机此前的成功部署是唯一已知差异。
2. 或在**另一个 Windows 会话**（不同的交互式登录会话）中执行 `python scripts/make_package.py build/release build/release/deploy` 与后续 portable gates。
3. 解除后必须重新执行**完整** canonical 流程（retention → freshness → make_package → identity → portable gates），不得复用本轮任何残缺产物。

## 验证（Verification）

- `qtpaths.exe -query` rc=0，输出 25 项布局（证明 Qt 工具链本体可用）。
- 工程外最小 QProcess 探针（`build/_qprocess_probe.exe`）在 Bash / PowerShell 原生 / 沙箱外三种父子链下**一致复现**。
- 通道探针显示仅 `startDetached` 成功，`SeparateChannels` 与 `ForwardedChannels` 均失败。
- ctypes 管道微基准定位到 `OUTBOUND + GENERIC_READ` 配对特有失败（err=231），`INBOUND`/`DUPLEX` 配对正常。
- 仓库侧不受影响的门禁已复跑：**Debug CTest 36/36 PASS**、**Release CTest 36/36 PASS**
  （36 项 Test time 合计：Debug 72.25 s / Release 71.51 s；17:07 / 17:16 完成；含
  `qml_production_write_check` 之 R17、`qml_write_foundation_check`、`qml_focus_check`、
  `qml_nav_check`、`qml_geometry_check`、`qml_smoke`）。
- `scripts/test_make_package_freshness.py` 7 项全 PASS（missing / stale / current / RED old-rule / same-size trap / missing-build-exe）。
- `git status --porcelain` 空；HEAD 仍为 `b7408ab…`；`cc3c6f8…b7408ab` 仅含 5 个 docs 文件。

## 回归保护（Regression Protection）

本 Issue 属**环境缺陷**，产品代码零改动，因此**无需新增产品侧回归测试**；保护措施落在
"防止用陈旧/伪造产物冒充 fresh"这一风险面：

| 风险 | 现有保护 | 本轮实测 |
| --- | --- | --- |
| 陈旧 deploy 被误判为 current | `deploy_is_current()` 比较 exe SHA-256（内容属性） | freshness 7/7 PASS |
| 陈旧产物冒充修正后行为 | R17 指纹（UTF-16 字符串计数）语义对照 | 新 exe = 12，陈旧 exe/ZIP = 0 |
| 失败后只留部分部署被误判 current | **⚠️ 覆盖不足** —— `deploy_is_current()` 仅比 exe，不含部署树完整性 | 本轮实测触发 `FAIL: required package file missing: platforms/qwindows.dll` |
| 环境阻塞在 deploy 后期才暴露 | **⚠️ 缺失** —— 无 `windeployqt --version` 前置探测 | 本 Issue 即为此缺失的直接后果 |

**后续防复发动作（另立任务，不在本轮实施）**：

1. 增强 `deploy_is_current()`：除 exe SHA-256 外，追加 `REQUIRED_FILES` 完整性校验，
   使"部分部署"不再被误判为 current。
2. 在 `make_package.py` / `deploy_windows.bat` 增加**前置探测**：`windeployqt --version`
   必须 rc=0，否则在进入 deploy 流程前即以明确信息失败（失败点前置）。
3. 把"R17 指纹校验"固化为便携产物验收的一部分（新 D 的 exe 必须 `R17 utf16 > 0`），
   作为 A/B/C/D identity 之外的**语义新鲜度**佐证。

## 遗留与后续

- 本轮产生的残缺产物已按"整目录 Move（绝不逐文件删除）"保留至
  `build/package-retention-20260923-m10f-corrected/`：`package`、`package-extract`、`package-extract-residual`、`package-staging-partial`、`release-deploy-partial-windeployqt-failed`、`release-deploy-partial-windeployqt-pipe-blocked`、`release-deploy-stale-0832`。
- M10-F 状态保持 **HOLD / PENDING FRESH HUMAN #10/#11**；未 push、未 tag、LKGC 保持 `9bdd99c`；M11 未开始。
- 建议把"打包前置自检：`windeployqt --version` 必须 rc=0"作为 `make_package.py` 的可选前置探测，使该类环境阻塞在 deploy 之前就明确报出（后续单独任务，不在本轮实施）。

## 教训

区分"产品缺陷"与"环境缺陷"的最快判据是**构造一个与产品无关的最小复现**。本轮若能更早写出 `build/_qprocess_probe.cpp`，就能避免在 windeployqt 的选项、PATH、环境变量、沙箱策略上反复排查。仓库内既有 `test_make_package_freshness.py` 已示范"用 throwaway fixture 隔离证明"的方法论，本次应同样先做隔离探针。