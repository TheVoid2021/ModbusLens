# Devlog 2026-09-07 — T009 Part B Learning / Test Design（Replay UI Integration，docs-only）

## 今日工作

- **T009 Part B 启动（Phase: Learning / Test Design，docs-only）**；所有定案落于 [T009 档案](../tasks/T009-replay-mode.md) Part B 章（PB-0 ~ PB-30）。摘要：
  - **单 Dashboard 复用**：Simulator/Replay 两来源 → 同一 AnalysisController + TransactionListModel → QML；不新建任何第二套模型/页面。
  - **Controller 新 API 定案**：`loadReplayFile(const QUrl&)`；`clearDemo()` **直接重命名** `clearResults()`（理由：名字过窄 + 避免双逻辑双名字；改动面=Main.qml 1 处 + UI-B05/B06 2 处）；`hasReplayError`/`replayErrorMessage`/`modeLabel`/`sourceLabel` 四个 Q_PROPERTY。
  - **失败策略定案**：旧成功结果保持不变 + 显示 error（原子发布 invariant：read→parse→analyze 全成功才一次性发布；不得半成品）。成功后/切回 Demo 时清除旧 error。
  - **错误映射**：parse error 保留 1-based line；execution error 仅 Presentation 层 +1 显示 "transaction N"；Core 索引不动。
  - **FileDialog 本机实证**：Qt 6.11.1 确认 `QtQuick.Dialogs` QML module 与 `Qt6::QuickDialogs2` CMake 组件存在（`.../qml/QtQuick/Dialogs/quickimpl/qml/FileDialog.qml`）；Implementation 再以 configure/build 实证。
  - **Canonical sample 定案（方案 A）**：唯一 `samples/demo_v1.mlog`（Implementation 时 git mv 现 tests/data fixture），tests + deployment + manual smoke 共用，杜绝两份漂移。
  - **矩阵**：UI-R01~R08（Golden 加载/四行细节/parse error/execution error/file open 失败/error 恢复/replace 语义/clear 语义）；Manua Smoke checklist A~E；deploy regression 计划。
- 文档同步：PROJECT_STATUS（面板/§2/§7）、BACKLOG（M5/T009 行/变更记录）。

## 关键决策

- **clearDemo → clearResults 直接重命名（不做转发）**：cleaner API + 须同步两处测试调用点；比长期双名/转发壳更符合"一个 API 一个语义"。
- **失败留旧不留半**：一次失败的文件加载不销毁用户已有的可用结果；区别于"clear-then-show-error"策略（后者惩罚用户）。
- **PB-24 定则**：Manual Replay Smoke 必须人工点击 native FileDialog —— Agent 不得自报 PASS，一律 WAITING FOR USER CONFIRMATION。

## 验证（docs-only）

- 压缩后状态核验：T008 DONE / T009 Part A DONE / LKGC `e4920da` / HEAD `7235e83`，与仓库事实一致；工作区 clean。
- 全部 docs 检索：无 `TXN|<elapsed>=0>` 类笔误（T009 档案即为 `TXN|elapsed_ms|`，注明 >= 0）。
- FileDialog/CMake 组件本机实证（只读取证，见上）。
- docs-only 提交，LKGC 维持 `e4920da`。

## 下一步

- T009 Part B — Implementation（==待用户指令，不自动开始==）：23 步计划见 T009 档案 PB-28；预计 16-test ui_bridge 扩充 + 2→3 次部署回归 + Manual Replay Smoke（WAITING FOR USER）。