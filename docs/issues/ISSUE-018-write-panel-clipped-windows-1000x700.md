# ISSUE-018: writeFoundationPanel 在真实 windows QPA / 1000x700 下被窗口裁切 —— 写区从未被计入页面布局

- **日期**：2026-09-23
- **关联任务**：T022 M10-F final portable verification（打包前源码收口）
- **严重度**：HIGH —— 真实平台（`QT_QPA_PLATFORM=windows`）下 1000x700 最小窗口的**验收门禁直接 FAIL**
- **V2 Trace 字段**：Observed ✅ / Expected ✅ / Evidence ✅ / Root Cause ✅ / Fix ✅ / Verification ✅ / Regression Protection ✅

## Observed（现象）

在**真实 Windows 平台**（`QT_QPA_PLATFORM=windows`，非 offscreen）下运行写区几何门禁：

```text
WRITEFAIL C4 geometry 1000x700 0x10: writeFoundationPanel is clipped by the window (scene 73,395 911x319 vs window 1000x700)
WRITEFAIL C4 geometry 1000x700 0x10: the validation message is clipped by the window
```

`exit code = 1`。`0x06` 变体通过，只有 `0x10` 变体（FC16 草稿列可见、面板高 319）失败。

## Expected（期望行为）

1000x700 下：

```text
panel bottom  <= window bottom (700)
validation message 可见
按钮可达
无 overlap
无 clipping
```

## Evidence（证据）

### E1 —— 修复前的精确几何（windows QPA, 1000x700）

```text
communicationWorkspace      (57, 41) 943x659   bottom=700
communicationContentLayout  (73, 57) 911x627   bottom=684   ← 可用内容高度 627
  communicationHeader            (73, 57)  911x20
  communicationConnectionHeader  (73, 89)  911x20
  communicationConnectionSection (73,121)  911x48
  communicationRequestHeader     (73,181)  911x20
  communicationRequestSection    (73,213)  911x138
  writeFoundationSection         (73,363)  911x**0**    ← 高度被布局分配为 0
  writeFoundationPanel           (73,395)  911x319   bottom=714  ✗ > 700
  writeValidationError           (85,686)  887x16    bottom=702  ✗ > 700
```

### E2 —— 这不是 packaging / portable-only 问题

同一二进制（`build/release/modbuslens.exe`，与 NEW D 字节相同）在 **source tree + windows QPA**
下报**完全相同**的两条失败；而在 **offscreen** 下 `rc=0`。

⇒ 差异来自 **QPA 字体度量**：实测同一行文字在 windows 下 16px、offscreen 下 12px
（`writeValidationError` 高度 16 vs 12；`writeFoundationPanel` 319 vs 305）。

### E3 —— 这不是「一直如此」的平台假象

修正前的旧产物 `build/package-retention-20260923-m10f-corrected/release-deploy-stale-0832/ModbusLens.exe`
（sha256 `d5a49582cc2033ce39a0ab2f727222ec57bacf768cceb795f5a237d112b11e87`）在**同样 windows QPA**
下 `rc=0`：

```text
OLD : writeFoundationPanel=(73,305 911x271) → bottom=576   ✓
NEW : writeFoundationPanel=(73,395 911x319) → bottom=714   ✗
```

⇒ 是**近期 UI 增量**把底边推过了阈值：**y 下移 +90px**（主因）、**h 增高 +48px**（次因）。

### E4 —— y +90px 与 h +48px 的精确来源（逐项量化）

| 项 | 变化 | 来源 |
| --- | --- | --- |
| `communicationRequestSection` | 80 → **138**（+58） | `cc3c6f8`（FC03 Function 标签 + 请求行加宽）+ `8d78ddb`（请求行由 1 行拆为 2 行，恢复窗口内可达性） |
| 页面纵向间距 | 6 × 12 = **72** | 既有 `spacing: DS.spacingM` |
| `writeFoundationPanel` (h) | 271 → **319**（+48） | `cc3c6f8` 在写区新增 FC16 Quantity/ByteCount 派生行与预览行（`write10DraftColumn` 152 → 200） |

⇒ **y +90px 的主因不在写区，而在它上方的请求区（+58）与页面间距（72 中的一部分）**。

## Root Cause（根因）

两个叠加缺陷：

1. **`WriteFoundationSection` 根 `Item` 没有 implicit 尺寸**（`Item` 默认 `implicitHeight = 0`），
   而它内部 `ColumnLayout { anchors.fill: parent }` 的内容仍然按自然尺寸渲染
   （SectionHeader 20 + spacing 12 + PanelCard 319 = **351**）。
   ⇒ 页面 `ColumnLayout` 认为该 section 高 **0**，**整页高度计算完全漏掉写区**，
   写区永远以「溢出」形式绘制，布局对它没有任何控制权。
2. **内容总高超过可用高度**：自洽高度 = 306（页头/连接/请求）+ 12 + 351 = **657** > 可用 **627**，
   超出 **30px** —— 与实测完全吻合（panel 底边 714 − contentLayout 底边 684 = **30**）。

**为什么一直到 portable 验收才发现**：source-tree 的六个 QML 门禁**全部**用
`QT_QPA_PLATFORM=offscreen`（为了 headless-safe），而 offscreen 的行高比真实平台小 4px，
逐行累计后在 offscreen 下仍有 41px 余量（底边 673 ≤ 700）——**门禁的平台与验收的平台不是同一个**。

## Fix（修复）

保留全部协议信息（FC03/FC06/FC16 标签、PDU / 0-based、DEC/HEX、PDU/RTU Preview、
Quantity / Byte Count），只用真正的布局手段回收 46px：

1. **`src/ui/qml/components/WriteFoundationSection.qml`** —— 让写区重新成为一等布局参与者：

   ```qml
   implicitWidth: writeContentLayout.implicitWidth
   implicitHeight: writeContentLayout.implicitHeight
   ...
   ColumnLayout { id: writeContentLayout; anchors.fill: parent; spacing: DS.spacingS }
   ```

   （内部 ColumnLayout 的 `implicitHeight` 只来自其子项，故与 `anchors.fill` 不构成绑定环。）

2. **`src/ui/qml/pages/CommunicationPage.qml`** —— PDU 与 RTU Frame 预览**并排一行**
   （各带 `elide`），保留两者且各占半宽：节省与一行等高的 ~15-18px。

3. 页面 `communicationContentLayout` 间距 `DS.spacingM`(12) → `DS.spacingS`(8)：
   6 个间隙回收 24px，**分组结构不变**。

4. 写区内部间距 `DS.spacingM`(12) → `DS.spacingS`(8)：回收 4px。

未删除任何功能、未缩小字号、未隐藏协议信息、未减少 Human #10/#11 所需信息。

## Verification（验证）

修复后（windows QPA, 1000x700）：

```text
communicationContentLayout  (73, 57) 911x627  bottom=684
  communicationRequestSection   (73,197)  911x123   ← 138 → 123
  writeFoundationSection        (73,328)  911x347   ← 0 → 347（现在被正确计入）
  writeFoundationPanel          (73,356)  911x319  bottom=675  ✓ ≤ 700（余量 25px）
  writeValidateError            (85,647)  887x16   bottom=663  ✓ 可见
  writeActivateButton           (85,605)   50x34   bottom=639  ✓ 可达
  → --qml-write-foundation-check = rc=0 PASS
```

- **windows QPA 六门禁**：smoke / production-write / write-foundation / focus / nav / geometry
  **全部 rc=0**，且 `ReferenceError` / `TypeError` / `Unable to assign` 计数**全部为 0**。
- **R15 / R16 / R17**：全部 PASS（R15/R16 的 `clickReachesNamed` 前置断言通过
  ⇒ 运行时已证明 `commReadButton` 位于 1000x700 窗口内）。
- **真实 ctest**：**Release 37/37 PASS**（88.30 s）；**Debug 37/37 PASS**（90.13 s）。

## Regression Protection（回归保护）

`CMakeLists.txt` 新增**一个**条目（不是把全部 CI 改成 windows）：

```cmake
add_test(NAME qml_write_foundation_check_windows
    COMMAND modbuslens --qml-write-foundation-check)
set_tests_properties(qml_write_foundation_check_windows PROPERTIES
    ENVIRONMENT "QT_ASSUME_STDERR_HAS_CONSOLE=1")   # 不给 offscreen ⇒ 真实平台
set_tests_properties(... qml_write_foundation_check_windows PROPERTIES
    FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;Unable to assign")
```

- 复用了既有 oracle（同一个 `--qml-write-foundation-check` 的 C4 几何断言），**未建第二套体系**。
- 测试数 36 → **37**（如实披露）。该条目需要交互式 Windows 会话。
- 它覆盖的正是本次漏掉的维度：**真实平台的字体度量下的 1000x700 布局**。

## Lessons（教训）

1. **门禁的平台必须与被验收的平台一致**：offscreen 行高 12px vs 真实平台 16px，逐行累计足以把
   一个「刚好放得下」的页面推过窗口边缘。所有 QML 门禁都用 offscreen ⇒ 真实平台几何从未被断言。
2. **`Item` 作为自定义组件根必须有 implicit 尺寸**：默认 0 会让父布局**静默少算整块内容**，
   表现为子项「溢出渲染」而不是布局错误 —— 定位时必须把 scene 矩形逐项打印出来才能看见。
3. **几何回归要按「谁在它上面」逐项量化**：本次 +90px 的下移主因在**写区上方**的请求区，
   而不是写区自身；只看失败对象（写面板）会得出错误结论。
4. **旧产物是宝贵的历史对照**：用 retention 里修正前的部署树在**同一 QPA** 下量同一坐标，
   才能把「一直如此」与「近期增量造成」区分开。
