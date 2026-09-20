# ISSUE-014: TransactionsPage delegate bindings evaluate against invalid roles during a model reset

## 观察到的现象（Observed）

新增 M10-C3 的 write-foundation harness（`--qml-write-foundation-check`）在 **Clear Results / source
replacement 之后**输出 9 条 QML 运行时告警：

```text
qrc:/ModbusLens/src/ui/qml/pages/TransactionsPage.qml:271:29: Unable to assign [undefined] to int
qrc:/ModbusLens/src/ui/qml/pages/TransactionsPage.qml:272..277: Unable to assign [undefined] to int/bool/QString
qrc:/ModbusLens/src/ui/qml/pages/TransactionsPage.qml:344: TypeError: Cannot call method 'toString' of undefined
qrc:/ModbusLens/src/ui/qml/pages/TransactionsPage.qml:349/379: Unable to assign [undefined] to QString
```

同样序列在 `--qml-nav-check`（同样会清空结果）中**不出现**（实测 0 条），因此该现象只在
「Transactions 页的 ListView 已创建了 delegate，随后模型被 reset」这一组合下出现。

## 影响（Impact）

- **无功能影响、无数据损坏**：`transactionModel_.setEntries(...)`（`beginResetModel/endResetModel`）
  期间 delegate 会被短暂地按无效 index 求值，角色值为 `undefined`；reset 完成后绑定立即恢复正确值。
- **无 write-safety 影响**：与 M10-C 的 prepared snapshot / confirmation / transport 完全无关
  （写路径对 transport 的调用次数为 0，且这些告警来自 Transactions 页而非写 UI）。
- 对使用者的可见影响：无（仅日志噪声）。

## 复现步骤（Reproduction）

1. `QT_ASSUME_STDERR_HAS_CONSOLE=1 QT_QPA_PLATFORM=offscreen modbuslens --qml-write-foundation-check`
2. 该 harness 先经真实 FC03 读产生一条 transaction（Transactions 页创建 delegate），随后执行
   `clearResults()` / `runDemoBatch()`（模型 reset）⇒ 出现上述 9 条告警。
3. 对照：`--qml-nav-check`（也清空结果，但执行时 Transactions 页从未创建过 delegate）⇒ 0 条。

## 定位过程（Diagnosis）

- 告警行号落在 `TransactionsPage.qml` 的 row delegate（`model.deviceAddress` / `model.statusText` …）
  与详情面板的 `page.selectedEntry.*` 绑定上。
- 只有「delegate 已实例化 + 模型 reset」同时成立时才出现 ⇒ 结论是 **reset 窗口内的角色求值**，
  而非 M10-C2/C3 新控件的问题（写 UI 的告警在同一日志中为 0 条）。
- `git status` 确认本轮未修改 `TransactionsPage.qml`（该文件自 M9-F F2 定稿后未变）。

## 根因（Root Cause）

Qt Quick 的 model reset 语义：`beginResetModel()` 之后、`endResetModel()` 之前，view 可能重新求值
既有 delegate 的绑定，而此时 `index` 已无对应的模型行，`model.<role>` 返回 `undefined`。
`TransactionsPage` 的 delegate/详情绑定**直接**把角色值赋给强类型属性（`int` / `bool` / `QString`）
或调用其方法（`.toString()`），因此产生类型转换告警。

## 处理（Decision）

**本轮不修**（M10-C3 scope freeze：本轮只做 write confirmation 的 context/keyboard/accessibility 安全，
不改 Transactions 页视觉与绑定结构）。记录为：

- 分类：**PRE-EXISTING NON-BLOCKING**（产品 QML 既有的健壮性细节；本轮只是首次由新 harness 路径**观察到**）。
- 归属：Transactions 页绑定健壮性；建议在未来的 **warning hygiene / UI robustness** 任务中统一处理
  （例如给 delegate 绑定加 `?? 0` / `?? ""` 或把详情绑定改为在 `selectedEntry` 为空时不求值）。

## 验证（Verification）

- `--qml-write-foundation-check`：**PASS**（exit 0，全部 C01–C36 oracle 通过，zero write dispatch），
  9 条告警不影响任何断言。
- `--qml-nav-check` / `--qml-focus-check`：PASS，0 条同类告警。
- Debug/Release `ctest`：**31/31 PASS**。

## 回归保护（Regression Protection）

C3 的 harness 会持续执行「产生 transaction → 清空结果」这一序列，因此该告警若变成**失败**
（例如升级 Qt 后把 undefined 赋值提升为错误）会立刻在 `qml_write_foundation_check` 中暴露。
