# ISSUE-016: M10-F QML `ReferenceError` + FC03 request-row overflow — 两个真实缺陷同时躲过了全部 source-tree 门禁

- **日期**：2026-09-23
- **关联任务**：T022 M10-F correction（行为修正轮；本文档记录 follow-up correction）
- **严重度**：HIGH —— ① 便携产物门禁直接 FAIL（阻断 M10-F）；② Communication 页主操作按钮在最小窗口下**不可达**
- **V2 Trace 字段**：Observed ✅ / Expected ✅ / Evidence ✅ / Root Cause ✅ / Fix ✅ / Verification ✅ / Regression Protection ✅

## Observed（现象）

native PowerShell 下 canonical `make_package.py` 已经走到 portable runtime gate：deploy ✅、
deploy identity ✅（exe SHA-256 `1d2e7dc5…dd9f`）、staged 1498 entries + README.txt、ZIP ✅
（40991232 B，`b8533dc2…e335`）、fresh extraction ✅、extract identity ✅ —— 但最终：

```text
make_package FAIL: minimal-PATH --qml-smoke-test emitted warning containing 'referenceerror'
make_package EXIT=1
```

复现 canonical gate 后得到精确原始行（`build/_qt_rca/repro_minimal_path.txt`）：

```text
qrc:/ModbusLens/src/ui/qml/pages/CommunicationPage.qml:287: ReferenceError: preview is not defined
qrc:/ModbusLens/src/ui/qml/pages/CommunicationPage.qml:285: ReferenceError: preview is not defined
qrc:/ModbusLens/src/ui/qml/pages/CommunicationPage.qml:296: ReferenceError: preview is not defined
qrc:/ModbusLens/src/ui/qml/pages/CommunicationPage.qml:294: ReferenceError: preview is not defined
qrc:/ModbusLens/src/ui/qml/pages/CommunicationPage.qml:304: ReferenceError: preview is not defined
qrc:/ModbusLens/src/ui/qml/pages/CommunicationPage.qml:303: ReferenceError: preview is not defined
```

**undefined symbol = `preview`**；受影响对象 = 三个嵌套 `Label`（`visible` / `text`）。

## Expected（期望行为）

1. portable minimal-PATH smoke / nav / geometry **不产生任何 warning**（`referenceerror` / `typeerror` / …）。
2. FC03 请求预览在 Communication 页**真实渲染** PDU / RTU 字节。
3. 页面主操作（`commReadButton`）在受支持的最小窗口 1000x700 下**可被鼠标点击**。

## Evidence（证据）

### E1 — 不是环境问题：同二进制在 build 树同样复现

`build/package-extract/…/ModbusLens.exe` 与 `build/release/modbuslens.exe` **同一 SHA-256**；
在 **dev PATH** 下运行 `--qml-smoke-test` 同样输出 6 条 ReferenceError。四种组合
（release/debug × offscreen/windows）**全部 6 条**、`rc=0`。⇒ 与 PATH / cwd / QPA 无关，是 QML 自身。

### E2 — 根因是 QML 作用域规则（用 canonical QML 运行时实证）

`build/_qt_rca/scope_test.qml`（控制组 + 实验组）：

```text
RESULT t1.text(root prop)  = "1"        <- 组件 ROOT 对象的属性：可见
RESULT t2.text(mid prop)   = ""         <- 中间对象的属性：不可见
scope_test.qml:16: ReferenceError: midProp is not defined
```

**QML 无限定名解析只包含「对象自身属性 + 组件 ROOT 对象属性 + id」；中间（非 root）对象的属性
不在嵌套子项的解析链里。** 因此 `CommunicationPage.qml` 中声明在**中间 `ColumnLayout`**（第 279 行）
上的 `readonly property var preview` 对其嵌套 `Label` **不可见** —— 即便两者是父子关系。

对照：同一提交在 `WriteFoundationSection.qml` 中把 `write06Preview` / `write10Preview` /
`preparedPreview` 声明在**组件 root**（第 77–86 行），因此那里用无限定名是**合法**的。

### E3 — 归属：`cc3c6f8` 引入（非 pre-existing）

`git blame -L 275,310` 显示第 275–309 行（含 `preview` 声明与三个 Label）**全部**为
`cc3c6f8b (2026-09-23)`；`git show --stat cc3c6f8` 显示本文件 +58 行。第 279 行的 `preview`
与第 285/287/294/296/303/304 行的读取均为该提交新增。

### E4 — 为什么所有 source-tree 门禁都漏掉了它（关键）

CTest 的 QML 门禁是 `add_test(NAME qml_smoke COMMAND modbuslens --qml-smoke-test)`，
**只用 exit code 判定**（无 `PASS/FAIL_REGULAR_EXPRESSION`）。而该缺陷：
- 不改变退出码（`rc=0`，QML 绑定失败只是运行时诊断）；
- 消息在默认情况下**根本不到达 CTest**：`modbuslens.exe` 是 **WIN32 子系统**程序，
  未设 `QT_ASSUME_STDERR_HAS_CONSOLE=1` 时 Qt 把诊断写到 OutputDebugString，
  CTest 捕获到的 Output 块是**空的**。

实验（`build/_qt_rca/gate_visibility.py`）：加上 `QT_ASSUME_STDERR_HAS_CONSOLE=1` 后，
**六个** QML 门禁全部开始输出，且**六个**全部含 `referenceerror`
（`--qml-write-foundation-check` 另含 `typeerror`）。⇒ 缺陷一直都在，只是**不可观测**。

### E5 — 修复 ReferenceError 后暴露的第二个真实缺陷（请求行溢出）

修好作用域后 `--qml-production-write-check` 转为 **rc=1**：

```text
PRODWRITEFAIL R15: silent slave produced 0 timeouts, expected exactly one more
PRODWRITEFAIL R16: no pending request to lose
PRODWRITEFAIL R16: terminals=0, expected exactly one more
```

逐步定位（临时诊断，已移除）：

```text
DBGR15 btn visible=1 enabled=1 w=92.0 h=24.0 scene=(1186.0,241.0) win=1000x700
DBGR15 busyBefore=0 clickNamed=1 busy=0        <- 点击没有产生 pending request
```

按钮中心坐标 **x=1186**，而窗口宽 **1000** —— 按钮在**窗口之外**。
把窗口临时加宽到 1400 后同一步骤立刻恢复（`busy=1`、`timeouts=1`、rc=0），
证明**离屏位置**就是机制。

**归属测量**（把 `CommunicationPage.qml` 换成 `cc3c6f8~1` 版本重建）：

```text
pre-cc3c6f8 : commReadButton scene=(891.0,222.0)   <- 在 1000 宽窗口内，可点击
post-cc3c6f8: commReadButton scene=(1186.0,241.0)  <- 在窗口外，不可点击
```

`git diff cc3c6f8~1 cc3c6f8 -- CommunicationPage.qml` 显示该提交在**唯一的、不换行的
`RowLayout`** 内：把 `起始地址` 改成更长的 `起始地址（PDU / 0-based）`，并**新增**
`HEX 0x0000` 回显 `Label`。行宽因此超出最小窗口，把尾部 `Button` 推出可视区域。

### E6 — 顺带发现的测试/工具脆弱性（本轮不修，另立任务）

1. `clickNamed`（`src/main.cpp`）是**基于坐标**的合成点击：取 item 的 scene 中心，
   把 `QMouseEvent` 直接发给 window；**R15/R16 调用处忽略了它的返回值**。
   因此「点击失败」不会被当场发现 —— 表现为状态断言（0 timeout / no pending）而非点击错误。
   pre-`cc3c6f8` 的 R15/R16 之所以长期为绿，是因为按钮当时**恰好在窗口内**。
2. **另一个 pre-existing 缺陷**：`TransactionsPage.qml:344`
   `TypeError: Cannot call method 'toString' of undefined`（`model.functionCode` 为 undefined）。
   归属 `9712a6cf (2026-09-18)`，**不是** `cc3c6f8`。仅在 `--qml-write-foundation-check`
   可达，故未阻塞打包门禁。

## Root Cause（根因）

1. **缺陷 A（阻断打包）**：`preview` 被声明在**中间** `ColumnLayout` 上，却被其嵌套
   `Label` 以**无限定名**读取。QML 作用域不含中间祖先 ⇒ `ReferenceError`，
   预览功能实际**从未渲染**。
2. **缺陷 B（UI 可达性）**：`cc3c6f8` 在**不换行的 `RowLayout`** 中增加标签宽度，
   使 FC03 请求行超出 1000x700 最小窗口，把页面主操作 `commReadButton` 推到窗口之外。
3. **检测缺口**：QML 绑定错误只产生**运行时诊断**且不改退出码；CTest 门禁只查退出码，
   而诊断在 WIN32 子系统下默认不到达 CTest —— 于是缺陷只能等到 `make_package.py`
   （唯一会 grep stdout/stderr 的门禁）才暴露。

## Fix（修复）

**缺陷 A —— 修真正的 QML binding / ownership（不改门禁、不过滤日志）**：
`src/ui/qml/pages/CommunicationPage.qml`
- 给预览块 `ColumnLayout` 加 `id: requestPreviewPanel`；
- 所有读取改为 **id 限定**（`requestPreviewPanel.preview.…`），不再依赖作用域的隐式祖先查找；
- 另增三个**有类型的投影** `previewPduText` / `previewRtuText` / `previewErrorText`
  （以及 `previewOk`）。原因：controller 的 map 在 `ok=false` 时**不含** `pduHex`/`rtuHex`，
  在 `ok=true` 时**不含** `error` —— 直接绑定缺失键会打印
  `Unable to assign [undefined] to QString`，等于自己制造新诊断。属性留在**原块**，
  未上提到页面 root，保持所有权不变。

**缺陷 B —— 修真正的布局溢出**：把该请求区从**一个** `RowLayout` 改为
`ColumnLayout { RowLayout(从站地址 / 起始地址 / HEX) ; RowLayout(寄存器数量 / 超时 / 读按钮) }`。
**每一个控件、标签文本、绑定与 id 完全保留**（无文案变更、无功能变更），只是换行。

## Verification（验证）

1. **红→绿 归因证据**：先加断言（未修）→ `qml_smoke` 失败：
   `Error regular expression found in output. Regex=[ReferenceError]`；修后同测试通过。
2. **六个诊断模式**（dev PATH + offscreen + `QT_ASSUME_STDERR_HAS_CONSOLE=1`）：

   ```text
   --qml-smoke-test             rc=0  gate_hits=none
   --qml-nav-check              rc=0  gate_hits=none
   --qml-geometry-check         rc=0  gate_hits=none
   --qml-focus-check            rc=0  gate_hits=none
   --qml-write-foundation-check rc=0  gate_hits=['typeerror']   <- E6-2 的 pre-existing 缺陷
   --qml-production-write-check rc=0  gate_hits=none
   ```

   ReferenceError 计数 = **0**（此前六个模式均为 6）。
3. **全量回归（真实 ctest）**：**Release 36/36 PASS**（78.25 s）、**Debug 36/36 PASS**（80.93 s）；
   测试数量**未变化**（36），未新增 target。
4. **可达性**：`commReadButton` scene x 由 **1186 → 926**（窗口 1000），重新位于窗口内；
   R15/R16 恢复为绿。

## Regression Protection（回归保护）

`CMakeLists.txt`（**只加强既有门禁，不新增测试目标**）：

1. 测试环境增加 `QT_ASSUME_STDERR_HAS_CONSOLE=1`，使 Qt 诊断进入 CTest 能捕获的 stderr
   （否则门禁看不到任何诊断文本）。
2. 对**打包门禁实际运行的同样三个模式**（`qml_smoke` / `qml_nav_check` / `qml_geometry_check`）
   增加 `FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError"`。

**覆盖范围与不覆盖的边界（显式声明）**：
- 覆盖：QML 作用域/绑定失败家族（`ReferenceError` / `TypeError`），这正是本次逃逸的类别；
- 有意**不**覆盖打包门禁里的 `plugin` / `missing dll` / `qimagereader`
  —— 那些是**部署完整性**关注点，在 source tree 里会误报（source tree 合法地从 Qt 安装加载插件）；
- **不覆盖** `Unable to assign [undefined] to QString`（本 Issue 已消除自己引入的那部分，
  但 `WriteFoundationSection.qml:655/669` 的 pre-existing 同类告警仍在，见 Follow-ups）；
- **不覆盖** `--qml-focus-check` / `--qml-write-foundation-check` / `--qml-production-write-check`
  —— 因为 E6-2 的 pre-existing TypeError 只在 write-foundation 可达，加上断言会立即变红。
  这是**已知缺口**：一旦 E6-2 修复，应把同样断言扩到全部六个 QML 门禁。

## Lessons（教训）

1. **QML 的无限定名不是词法作用域**：只有「自身 + 组件 root + id」。
   在中间对象上放属性、让子孙直接按名读，是**静默失败**（运行时 ReferenceError，退出码仍为 0）。
   本仓既有正确范例（`WriteFoundationSection.qml` 把 preview 声明在 root）。
2. **「测试通过」与「断言覆盖」是两件事**：当被测程序把诊断写到 OutputDebugString、
   而门禁只看 exit code 时，门禁实际上什么都没断言。
   **要断言某类缺陷，必须先保证该缺陷可观测**（此处 = 让 stderr 真的到得了 CTest）。
3. **同一提交可能藏着不止一个缺陷**：修好 A 才暴露 B。修复必须跑到**全量回归**，
   而不是「目标门禁变绿就收工」。
4. **合成点击是位置相关的**：`clickNamed` 用 scene 坐标 + 发给 window，
   一旦控件被布局挤出窗口就静默失效，且调用处忽略返回值。
   UI 可达性（控件是否真的在窗口内）应当本身就被断言。
5. **归属必须测量，不能推断**：把旧版本 QML 换回来重建、量同一坐标（891 vs 1186），
   比“看起来是这个提交加的”可靠得多。

## Follow-ups（另一轮，本轮不做）

1. **E6-2**：`TransactionsPage.qml:344` 的 `model.functionCode` undefined（`9712a6cf`）——
   加守卫并把它纳入全部门禁断言。
2. **E6-1**：`clickNamed` 应断言「控件在窗口内」并**检查返回值**，
   把 UI 可达性变成显式契约（而非依赖合成事件恰好送达）。
3. `WriteFoundationSection.qml:655/669`（`preparedPreview.pduHex/rtuHex` 在无 prepared 快照时为
   undefined）——与缺陷 A 同族（map 形状不对称）。本轮只消除了自己引入的那部分；
   这两行属 pre-existing，未改（避免越界），应并入同一轮 follow-up 修掉，
   之后即可把 `Unable to assign [undefined]` 也纳入断言。
4. **ISSUE-015**（windeployqt / QProcess）仍独立未解，见该文档。

---

## Follow-up closure（2026-09-23 第二轮：M10-F pre-package acceptance cleanup）

上一节列出的三条 follow-up **已全部关闭**。本节按「只增不改」原则追加，原文保留。

### ① `WriteFoundationSection.qml:655/669` — 已修（与本文档主缺陷同族）

**完整原始诊断**（六个模式**全部**输出，各至少一次；write-foundation / production-write 重复多次）：

```text
qrc:/ModbusLens/src/ui/qml/components/WriteFoundationSection.qml:655:21: Unable to assign [undefined] to QString
qrc:/ModbusLens/src/ui/qml/components/WriteFoundationSection.qml:669:21: Unable to assign [undefined] to QString
```

**触发**：**全部六个** QML 模式；在页面加载时即发生，且在 `preparedPreview` 每次重算时重复
（`preparedPreview` 绑定依赖 preparedWrite 的 NOTIFY 投影，写流程活动时反复重算）。

**undefined 的值**：`preparedPreview.pduHex`（655，`writeSummaryPdu`）与 `preparedPreview.rtuHex`
（669，`writeSummaryRtu`）。原因与本文档主缺陷同族 —— **map 形状不对称**：
`AnalysisController::previewPreparedWrite()` 在**没有 prepared 快照**时返回
`{ok:false, state:"none"}`（**正常初始状态**），只有真正 prepared 时才含 `pduHex`/`rtuHex`。
即：**每次启动、每个模式**都命中。

**分类**：**(A) 生产可达**——写区在 production 中始终存在，确认对话框的 PDU/RTU 标签在
「尚未 prepare」这一正常状态下求值。用户可见影响为**无**（对话框只在 prepared 后显示，
届时键存在且值正确，R17 断言 `10 00 01 00 02 04 00 70 04 C6` 仍通过），
但它是真实的「undefined 被赋给 QString」缺陷，且已成为门禁对象。

**最小修复**（`src/ui/qml/components/WriteFoundationSection.qml`，组件 root 上新增三个有类型投影，
两个 Label 改读投影）：

```qml
readonly property bool preparedPreviewOk: preparedPreview.ok === true
readonly property string preparedPreviewPduText:
    preparedPreviewOk ? preparedPreview.pduHex : ""
readonly property string preparedPreviewRtuText:
    preparedPreviewOk ? preparedPreview.rtuHex : ""
```

与本文档主缺陷（FC03 read preview）用的是**同一个模式**。未抑制日志、未改门禁。

### ② `clickNamed` R15/R16 假通过/假失败洞 — 已关闭（有负向对照证据）

**弱点**：`clickNamed` 是**位置式**合成点击（取 item `mapToScene(中心)`，把 `QMouseEvent` 发给
window，靠命中测试落地）。它的返回值只表示「找到且 visible」，**不表示事件真的到达了控件**；
而 R15/R16 调用处**忽略了返回值**。⇒ 控件被布局挤出窗口时，点击静默失效，
表现为「0 timeouts / no pending」这种误导性断言失败，甚至可能凭**上一步遗留的 pending 状态**通过。

**加固**（`src/main.cpp`）：
- 新增前置判定 lambda `clickReachesNamed(name)`：要求 item 存在、`isVisible`、`isEnabled`，
  且其中心点落在 `QRectF(0,0,window->width(),window->height())` 内；
- R15 / R16 各自在点击前断言该前置条件、并**检查 `clickNamed` 的返回值**，
  失败即以指名原因的报文 `fail(...)`；
- R15 另加「点击必须真的产生了请求」的效果断言（`serialBusy()`），
  使「凭遗留状态通过」不可能。

**负向对照（证明洞已关闭，而非新增死断言）**：临时把 Row 2 的一个标签加长以复现
「按钮被挤出窗口」，重建后运行 `--qml-production-write-check`：

```text
rc = 1
PRODWRITEFAIL R15: commReadButton is not inside the 1000x700 window, so a position-based click cannot reach it
PRODWRITEFAIL R15: the Read click did not start a request
PRODWRITEFAIL R16: commReadButton is not inside the 1000x700 window, so a position-based click cannot reach it
```

对比加固前同一变异只会报 `silent slave produced 0 timeouts` / `no pending request to lose`
（即**根因不可见**）。变异随后已还原（`git checkout`）。

**正向证明**：正常布局下 R15/R16/R17 全部通过（`rc=0`，无 `PRODWRITEFAIL`）；
前置判定 `clickReachesNamed` 通过即意味着**控件中心确实在 1000x700 窗口内** ——
这条不变量现在由 harness 在运行时强制，而不再依赖「合成事件恰好送达」。

### ③ `TransactionsPage.qml` delegate 角色读取 — 已修，**独立建档**

该缺陷有不同的根因（delegate 在模型 reset 时对已失效 index 再求值一次），
已另立 **`ISSUE-017-transactions-delegate-undefined-roles.md`**（含完整原文 10 条、
生产可达性证据、修复与验证）。结论：**生产可达**（AppBar「清空结果」绑定的同一控制器方法），
因此本轮修复，而非记为非阻塞。

### ④ 诊断回归策略已扩面

断言从「打包门禁的三个模式」扩展到**全部六个** QML 门禁，并纳入本族的静默报文类别：

```cmake
FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;Unable to assign"
```

理由：`Unable to assign` 正是「缺 map key」与「失效 index 读角色」两类的静默报文，
而这两类此前都只能等到 `make_package.py`（唯一 grep 文本的门禁）才暴露。

### ⑤ 本轮收口结果

```text
六个诊断模式：全部 rc=0，全部 0 条匹配诊断（此前 smoke..production-write 均有 655/669；
  write-foundation 另有 TransactionsPage 10 条）
真实 ctest：Release 36/36 PASS（78.98 s）；Debug 36/36 PASS（81.30 s）；测试数量未变化（36）
R15 / R16 / R17：全部 PASS
未做：未打包（ISSUE-015 宿主阻塞仍在）、未启动 Human #10/#11、未开始 M11、
     未修 ISSUE-015 的 deploy_is_current
```
