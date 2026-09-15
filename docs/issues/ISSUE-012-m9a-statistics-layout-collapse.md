# ISSUE-012: M9-A statistics layout collapse after reusable component migration

- **日期**：2026-09-15
- **关联任务**：T016 M9-A Phase 2（candidate `4fc934f`/`7abd887` 的用户 Manual Visual Review = FAIL 定位与修复）
- **状态**：RESOLVED（修复提交见 T016 §18；人工视觉复审 = PENDING USER REVIEW）

## Observed（用户截图 + 运行时取证）

- statistics labels/values overlap at the left edge
- Stat card background and textual content are spatially detached
- statistics section height collapses / content overflows
- statistics rows do not preserve intended card layout
- following Diagnosis workspace is visually invaded
- statistics become unreadable

## Expected

- two stable statistics rows
- every card has non-zero stable geometry
- label/value stay inside their own card
- statistics section owns sufficient height
- no overlap with Diagnosis section

## Evidence（`--qml-geometry-check` 实测，修复前，默认 1024×720 逻辑尺寸，offscreen+windows 双平台一致）

```text
statisticsPanel: x=0 y=227 w=992 h=0   implicit=0x0     ← 容器高度 0
statisticsRow1:  x=0 y=0   w=840 h=72  implicit=840x72  ← 两行 y 均为 0
statisticsRow2:  x=0 y=0   w=732 h=64  implicit=732x64  ← 与 row1 完全重叠
statCard_0..latency / statusCard_0..5:
                 尺寸 140/180/110 × 72/64（来自 Layout.preferred*），implicit 全部 0x0
diagnosisWorkspace: y=252；统计内容实际绘制至 ~y=300 → 侵入 48 px
```

修复后同探针：panel 992×168（implicit 864×168）；row2 y=80 ≥ row1 底部 72；Diagnosis y=420 ≥ panel 底部 407；默认尺寸 + resize 1000×700 双尺寸 PASS。

## Impact

M9-A Phase 2 候选 UI 的 Statistics 区不可读且污染下方 Diagnosis 区——阻断 Phase 2 人工验收；不涉及行为/数据层（统计绑定与格式化全部正常，仅几何层断裂）。

## Initial Hypotheses（证据收集前）

1. PanelCard（Rectangle）无 implicit 尺寸，root ColumnLayout 按 implicit 分配高度 → 0。
2. StatCard 依赖 Layout.preferred* 而自身 implicit=0，脱离 Layout 即塌缩。
3. 组件内部 anchors 布局（centerIn/fill）不回馈父 implicit。
4. qml_smoke 只断言"实例化成功"，不覆盖几何——为什么没拦住。

## Root Cause

- 初版标记：**UNDER INVESTIGATION**（不先填结论，等待运行时测量）——以下为测量后追加批注：
- **已证明根因（追加）**：PanelCard 的 root Rectangle `implicitWidth/Height = 0x0`，其内部内容通过 anchors 填充卡片；anchors 消费父尺寸但**不产生父的 implicit 尺寸**；根 ColumnLayout 对子项按 preferred（缺省=implicit）高度分配 → PanelCard 高度 0。内部 ColumnLayout 在 0 高度空间里将两行都摆到 y=0（重叠绘制、无裁剪溢出），统计内容绘制到 Diagnosis 区之上——全部症状与实测数字一一对应。
- **次生缺陷（同族，本次一并修复）**：StatCard 自身 implicit=0x0，几何完全外包给调用点的 Layout.preferred*；在任何非 Layout 容器中同样会塌缩。

## Fix（最小粒度，只动 contracts，不动行为）

1. `PanelCard.qml`：`implicitWidth/Height = contentLayout.implicit + 2×padding`；内容经 `default property alias contentData: contentLayout.data` 进入内层 ColumnLayout（anchors 只负责摆放、不再承担尺寸来源）。
2. `StatCard.qml`：新增 `implicitWidth/Height`（由 labelColumn.implicit 派生）；保留 Layout.preferred* 作为布局首选值（调用点的 180/110/64 覆盖不变）。
3. `Main.qml`：statistics 调用点删除 wrapper ColumnLayout 与 anchors；两行 RowLayout 直接进入 PanelCard contentData；新增 objectName 探针锚点（statisticsPanel / statisticsRow1 / statisticsRow2 / statCard_0..2、statCard_rate、statCard_latency / statusCard_0..5 / statisticsHeader / diagnosisWorkspace）。
4. AnalysisController 绑定、统计取数与格式化语义（toFixed(1)+"%"、" ms"、"—"）零改动；Controller/Core/Serial/Replay/Diagnosis/Agent 零改动。

## Verification

- `--qml-geometry-check`（修复前）EXITCODE=1，输出即上文 Evidence；修复后 **PASS（默认 + 1000×700 双尺寸）**。
- qml_smoke EXITCODE=0（stderr 无 ReferenceError）；全量 ctest **25/25**（新增 `qml_geometry_check`）；`git diff --check` 通过。
- deploy 重建 `[OK]`；deploy 冒烟（无开发 PATH）EXITCODE=0。
- grabWindow 真值截图两张（`docs/assets/screenshots/geometry-1024x720.png`=1280×900、`geometry-1000x700.png`=1250×875；本机 DPI=125%，逻辑=1024×720/1000×700）；像素级自检（逐卡文字像素存在且位于卡内）PASS。

## Regression Protection

- 永久回归测试：CTest `qml_geometry_check`（`modbuslens --qml-geometry-check`，offscreen）——加载真实 QML，事件循环 settle 后断言：header 与 panel 不重叠；statisticsPanel 与每张可见 StatCard w/h>0；row2.y ≥ row1 底部；Diagnosis.y ≥ panel 底部。默认尺寸断言后 resize 至 1000×700 再断言一次。缺失对象按 100ms×5 重试后才判失败（布局/委托实例化是异步的）。
- 明确边界：这不是视觉验收替代品（有人工 M9-F）；它只锁"尺寸合同不再断裂"。

## 教训

1. 容器组件的尺寸合同必须自洽：implicit 来自 content-layout implicit（children 派生），anchors 永远不产生 implicit。
2. "自动化全绿"≠"布局没塌"：qml_smoke 的断言面是实例化不是几何——盲区要由最小的运行时几何断言单独覆盖。
3. 测量先于结论（UNDER INVESTIGATION → 带数字的根因）；取证探针本身也要防坑（QObject findChild 漏 Repeater delegate；DPI 虚拟化让外部截图拿到错误像素尺寸）。

## Closure（追加批注，2026-09-15）

- **Status = RESOLVED / CLOSED**。
- 失败历史完整保留于本文件（Observed / Evidence / UNDER INVESTIGATION → 实证根因），未删除、未改写。

**Final Verification（终验，全部在最终代码上执行）**：

| 验证项 | 结果 |
| --- | --- |
| `qml_geometry_check`（默认尺寸 + resize 1000×700 双尺寸断言） | **PASS**（EXITCODE=0） |
| full ctest（含既有 24 目标 + 本 Issue 新增守卫） | **25/25 passed** |
| deploy 重建 + 无开发 PATH deploy smoke | `[OK]` + EXITCODE=0 |
| **Manual Visual Review（用户复核真实应用界面）** | **PASS**（statistics title 可读；两行统计稳定；labels/values 均在卡内；无文字堆叠；无卡片/内容分离；不侵入 Diagnosis；Top Actions 可用；Serial/Diagnosis/Transactions 可见性不退化） |
| `git diff --check` | 通过 |

- 修复提交 `6562dd3`（`M9-A: fix statistics layout regression`）经用户批准为 **verified LKGC**；此前 candidate `4fc934f` / 回填 `7abd887` 保留为 "automation PASS → manual visual FAIL" 的真实记录，未 amend。
- 边界：本 PASS 表示 M9-A 首次迁移的 visual regression 已解决，不代表整个 M9 视觉刷新完成（Serial/Diagnosis/Transaction styling、icon、shell/navigation、native-title 一致性仍待后续里程碑）。
- 回归保护长期生效：CTest `qml_geometry_check`（尺寸合同守卫）。明确它不是视觉验收替代品，人工验收（M9-F）地位不变。
