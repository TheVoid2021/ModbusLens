# ISSUE-017: TransactionsPage delegate 在 model index 失效后仍读取角色 — 生产路径（清空结果）触发 TypeError

- **日期**：2026-09-23
- **关联任务**：T022 M10-F pre-package acceptance cleanup（本章为 M10-F 最终验收前的源码清理轮）
- **严重度**：MEDIUM-HIGH —— 生产可达（AppBar「清空结果」），每次清空都会在 stderr 产生 9 条
  `Unable to assign [undefined] to …` 与 1 条 **TypeError**；M10-F 的 QML 验收门禁不再允许静默诊断
- **V2 Trace 字段**：Observed ✅ / Expected ✅ / Evidence ✅ / Root Cause ✅ / Fix ✅ / Verification ✅ / Regression Protection ✅

## Observed（现象）

`--qml-write-foundation-check`（`QT_ASSUME_STDERR_HAS_CONSOLE=1`，offscreen）在 **C15「Clear Results」**
步骤前后输出**一整块** 10 条诊断（完整原文，未做任何截断）：

```text
qrc:/ModbusLens/src/ui/qml/pages/TransactionsPage.qml:277:29: Unable to assign [undefined] to QString
qrc:/ModbusLens/src/ui/qml/pages/TransactionsPage.qml:276:29: Unable to assign [undefined] to QString
qrc:/ModbusLens/src/ui/qml/pages/TransactionsPage.qml:275:29: Unable to assign [undefined] to int
qrc:/ModbusLens/src/ui/qml/pages/TransactionsPage.qml:274:29: Unable to assign [undefined] to bool
qrc:/ModbusLens/src/ui/qml/pages/TransactionsPage.qml:273:29: Unable to assign [undefined] to int
qrc:/ModbusLens/src/ui/qml/pages/TransactionsPage.qml:272:29: Unable to assign [undefined] to int
qrc:/ModbusLens/src/ui/qml/pages/TransactionsPage.qml:271:29: Unable to assign [undefined] to int
qrc:/ModbusLens/src/ui/qml/pages/TransactionsPage.qml:344: TypeError: Cannot call method 'toString' of undefined
qrc:/ModbusLens/src/ui/qml/pages/TransactionsPage.qml:349:41: Unable to assign [undefined] to QString
qrc:/ModbusLens/src/ui/qml/pages/TransactionsPage.qml:379:37: Unable to assign [undefined] to QString
```

**触发模式**：只有 `--qml-write-foundation-check` 会输出这一块（smoke / nav / geometry / focus /
production-write 都不输出）。**触发步骤**：C15 —— 该步骤在别的断言之间调用
`controller->clearResults()` 两次。

## Expected（期望行为）

清空/重载事务列表（模型 reset）不应产生任何 QML 运行时诊断：delegate 在 index 失效时不应对
不存在的角色做**类型赋值**或**方法调用**。

## Evidence（证据）

### E1 — `model` 的角色被**整体**判为 undefined ⇒ 是 index 失效，不是数据缺字段

10 条诊断覆盖 delegate 读取的**全部** 7 个角色
（deviceAddress / functionCode / elapsedMs / hasExceptionCode / exceptionCode / statusText / issueText）。
若只是某个字段缺失，不会 7 个同时为 undefined。

### E2 — 只有「转换」和「调用」会报，纯 JS 比较不会 ⇒ 与 undefined 语义完全自洽

```text
会报：271-277（赋值给 int/bool/QString 属性）、344（undefined.toString() → TypeError）、349 / 379（赋值给 QString）
不报：297  (model.issueText !== "")      -> undefined !== "" 是纯 JS 比较，结果 false 分支安全
不报：338  (.arg(model.deviceAddress))    -> arg 接受 QVariant，不产生诊断
不报：355  (model.elapsedMs + " ms")      -> 字符串拼接，得到 "undefined ms" 但不报错
不报：361/363/365 -> 三元判断先看 hasExceptionCode（undefined 为假），含 toString 的分支未被求值
```

### E3 — 一次 reset 恰好产生**一块**诊断 ⇒ delegate 在销毁/重置时被再求值一次

`TransactionListModel::setEntries()` 使用**正确**的 `beginResetModel()/endResetModel()`
（`src/ui/TransactionListModel.cpp:225-227`），因此不是模型信号缺陷。
C15 调用 `clearResults()` 两次：第一次移除**唯一**一行（1 个 delegate 被销毁 → 1 块诊断），
第二次已无行可移（无 delegate → 无诊断）。观察到的正是**一块**。

### E4 — 归属：不是 `cc3c6f8`

`git blame` 显示该 delegate 与其 `model.*` 读取来自 `9712a6cf (2026-09-18)`。
**但按本轮规则：commit 年龄不构成豁免 —— 分类只看是否生产可达。**

### E5 — 生产可达性（决定性）

`src/ui/qml/Main.qml:145-149`：

```qml
objectName: "appBarClearResults"
text: qsTr("清空结果")
onClicked: analysisController.clearResults()
```

harness 调用的 `AnalysisController::clearResults()` **就是**这个真实 AppBar 控件所绑定的同一个方法。
`clearResults()` 里有 `transactionModel_.setEntries({})` ⇒ 任何用户在**有事务时**点击「清空结果」
都会走同一条路径并产生同一批诊断。此外任何**重置模型**的路径同理（加载 replay 日志、
切换数据源）。

⇒ **分类 = (A) 生产可达的产品缺陷**（不是 harness-only 状态）。因此本轮最小修复。

## Root Cause（根因）

QQuickItemView 在模型 reset 时会**再求值一次**每个 delegate 的绑定，而此时该行的 model index
已经失效，所以 `model.<role>` 全部读成 `undefined`。delegate 的绑定把该 undefined
**赋给有类型的属性**（`int`/`bool`/`string`）或**在其上调用方法**（`undefined.toString(16)`），
于是产生 `Unable to assign [undefined] to …` 与 `TypeError`。

## Fix（修复）

`src/ui/qml/pages/TransactionsPage.qml` —— 把 delegate 对 `model.*` 的读取收敛到**唯一一处**并使其容错：

- delegate 根 `Rectangle` 增加 `id: rowItem`；
- 7 个 `readonly property` 改为容错读取
  （`model.x !== undefined ? model.x : <默认值>`，默认值 0 / false / ""）；
- 表现层的所有读取（`height`、设备号、功能码、状态、耗时、异常码、issueText）
  一律改走 `rowItem.*`，不再各自直读 `model.*`。

修复后 `model.*` 在文件中**只出现于那 7 个容错读取点**（grep 可验证），
其他 9 处诊断点全部消失。**未**抑制日志、**未**隐藏 stderr、**未**放宽门禁。

> 为什么不用 `required property` 注入角色：那会把角色值在 delegate 创建时**冻结**，
> 而事务行在 pending → Success/Timeout 时 `statusText` / `elapsedMs` 会变化，
> 冻结会破坏实时更新。容错读取保留了原有的响应式绑定。

## Verification（验证）

```text
六个诊断模式（dev PATH + offscreen + QT_ASSUME_STDERR_HAS_CONSOLE=1）：
  smoke / nav / geometry / focus / write-foundation / production-write
  → 全部 rc=0，且全部 "no matching diagnostic"（ReferenceError / TypeError /
    Unable to assign / cannot call method / undefined 关键词命中数 = 0）
真实 ctest：Release 36/36 PASS（78.98 s）；Debug 36/36 PASS（81.30 s）
```

## Regression Protection（回归保护）

本轮把 CTest 的输出断言从「三个打包模式」扩展到**全部六个** QML 门禁，并加上本缺陷的报文类别：

```cmake
set_tests_properties(qml_smoke qml_nav_check qml_geometry_check qml_focus_check
                     qml_write_foundation_check qml_production_write_check PROPERTIES
    FAIL_REGULAR_EXPRESSION "ReferenceError;TypeError;Unable to assign"
)
```

`Unable to assign` 正是本缺陷（以及「缺 map key」那一类）的静默报文，
现在任何 QML 门禁一旦出现即 FAIL，不再依赖打包末端才发现。

## Lessons（教训）

1. **delegate 的 `model.*` 读取必须容错**：模型 reset（清空、重载、换源）会让 QML 在 index 已失效时
   再求值一次 delegate 绑定。把 undefined 赋给有类型属性、或在其上调用方法，都会静默产生诊断/异常。
2. **「只在测试里看到」不等于「测试专属」**：本缺陷在 harness 中可见，但触发它的是**真实 AppBar 按钮
   绑定的同一个控制器方法**。分类要靠**调用路径**，不能靠「谁先发现」。
3. **一次 reset 只出一块诊断**这种"数量特征"是很好的机制指纹 —— 它把范围直接锁定到
   「每个被销毁的 delegate 再求值一次」，而不是逐行/逐字段的数据问题。
