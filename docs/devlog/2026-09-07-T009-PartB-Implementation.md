# Devlog 2026-09-07 — T009 Part B Implementation（Replay UI Integration）

## 今日工作

- **T009 Part B Implementation 完成（自动化全 GREEN）**，代码提交 `d473d36`（LKGC candidate）：
  - `AnalysisController`：`loadReplayFile(QUrl)`（isLocalFile→QFile ReadOnly→readAll→QByteArray 持有临时 string_view→parseReplayLog→analyzeReplayLog→TransactionListEntry 映射→原子发布）；`clearDemo()` 直接重命名 `clearResults()`；`hasReplayError`/`replayErrorMessage`/`modeLabel`/`sourceLabel`；错误 adapter（parse 八码保留行号、MissingHeader line=0 不显示 line 0；execution 三码展示层 +1 "transaction N"）。
  - 原子语义三分（PB-31）：失败只动 error state，旧 batch+mode/source 原样保留；成功一次性发布；runDemoBatch 显式切回 Simulator 并清 error。
  - `Main.qml`：`Load Replay...` 按钮 + `QtQuick.Dialogs.FileDialog`（Qt 6.11.1 实证；动态解析，CMake 零新增链接——最少必要依赖）；Header 绑定 modeLabel/sourceLabel（只显示 basename）；轻量错误 Label。
  - Canonical sample：`tests/data/demo_v1.mlog` → `samples/demo_v1.mlog`（git mv；tests/deploy/manual 共用一份；CMake configure_file COPYONLY + test-only compile definition，无硬编码路径）。
  - `deploy_windows.bat`：步骤 7b 复制 canonical sample 至 `build/deploy/samples/` + 校验清单含该文件。
- 测试：UI-R01~R08 新增八项（ui_bridge 22/22）；ctest 16/16；clean 96 targets 零警告；qml smoke exit=0；Core Zero Qt；deploy exit=0 + sample SHA256 两端一致 + minimal-PATH smoke exit=0。

## 实际遇到的问题

- **PE-3（部署脚本行尾事故）**：编辑工具改写 `deploy_windows.bat` 后行尾变 LF，cmd.exe 解析断裂（"'f' 不是内部或外部命令"、变量为空）。`file` 命令定位 → 恢复 CRLF → deploy exit=0。教训：Windows 批处理必须 CRLF；本仓库 .bat 改动后须核验行尾。
- RED 为真实信号：clearDemo 重命名后实现未同步 → `no declaration matches 'void AnalysisController::clearDemo()'` 编译错误。

## 当前状态

- A 状态：**AUTOMATED GREEN / DEPLOYMENT GREEN / MANUAL REPLAY WAITING FOR USER**（PB-24 定则：native FileDialog 不可可靠自动化，Agent 不自报 PASS）。
- 用户验收清单（A~E）：Demo 正常 → Load Replay 选 `build/deploy/samples/demo_v1.mlog`（Replay Mode / demo_v1.mlog / 4-4-0 / 1-1-1-1-0 / 25.0% / 25.0ms / 四行）→ Clear 清空但 source 保留 → Run Demo 切回 Simulator → 再 Load Replay 替换不追加。
- 用户确认前：T009 Part B ≠ DONE、T009 ≠ DONE、LKGC 归档不推进、不开始 T010。

## Git

- Part B Implementation（code/config）：`d473d36`（LKGC candidate）
- 本 devlog 随 docs-only 归档提交；LKGC 维持 Part A 的 `e4920da`，待用户 Manual Smoke PASS 后正式推进。

## 追加（同日）· 用户手动验收 PASS

- **User Manual Replay Smoke = PASS（A~E 全项）**：Replay 加载统计/四行正确、Clear 保留来源、Demo↔Replay 互切不追加。
- **T009 Part B = DONE；T009 整体 = DONE**。LKGC 推进至 `d473d36`（`git cat-file -t` 核验为真实 commit）；docs-only 确认提交不再次推进 LKGC。M5 按 BACKLOG 既有定义（T009,T010）保持进行中。未 push；未开始 T010。