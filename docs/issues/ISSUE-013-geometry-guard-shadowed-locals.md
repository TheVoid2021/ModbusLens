# ISSUE-013: Geometry 断言变量遮蔽 —— 两条断言长期不可证伪（evidence vacuity）

- **发现于**：M9-B5.2（2026-09-17），重指 ISSUE-004 守卫时发现
- **影响范围**：`src/main.cpp` `runGeometryAssertions`（`--qml-geometry-check` 门禁 / ctest `qml_geometry_check`）
- **状态**：RESOLVED（B5.2 同提交修复 + 变异探针证明）

## 1. 现象（Observed）

`runGeometryAssertions` 在函数顶部声明了外层局部变量：

```cpp
QQuickItem *row1 = nullptr;
QQuickItem *row2 = nullptr;
QQuickItem *header = nullptr;
QQuickItem *panel = nullptr;
if (statsVisible) {
    row1 = findNamedItem(...);                 // 外层 row1 正常赋值
    auto *row2 = findNamedItem(...);           // 遮蔽外层 row2
    auto *header = findNamedItem(...);         // 遮蔽外层 header
    auto *panel = findNamedItem(...);          // 遮蔽外层 panel
    ...
}
```

`if (statsVisible)` 块内的 `auto *row2/header/panel` 声明了**新的同名局部**，外层变量从未被赋值、恒为 `nullptr`。

## 2. 影响（Expected vs Actual）

- **Expected**：`statisticsRow2 y 与 row1 重叠` 断言、`workspace 下缘侵入 statisticsPanel`（ISSUE-004 系）断言在布局回归时 FAIL。
- **Actual**：两条断言的条件都以 `if (row1 && row2 && ...)` / `if (legacyVisible && panel && ...)` 开头，`row2`/`panel` 恒 null ⇒ **条件恒假、断言恒"通过"**——不可证伪。任何行重叠/下缘侵入类布局回归都不会被该门禁拦截（此前捕获的 992x0 塌缩与 sur-plus-spread 是由同函数其它"尺寸非零/MISSING"断言拦下的，故未暴露）。
- 附带：外层 `row2`/`header` 死变量产生 `-Wunused-variable` 警告（构建日志中可见但未被处置）。

## 3. 根因（Root Cause）

同函数两个作用域内同名局部遮蔽；MinGW 默认未开 `-Wshadow`，编译器不报警。写法漂移自多次向该函数追加断言的增量修改（外层声明是后来为块外断言加的，追加者未核对内层是否已有同名声明）。

## 4. 解决方案（Fix）

去掉内层 `auto *` 声明，直接给外层变量赋值（`row2 = findNamedItem(...)` 等），并在该处留 ISSUE-013 注释。修复后：

- `row2` 真实 ⇒ 行重叠断言复活；
- `panel` 真实 ⇒ ISSUE-004 系下缘侵入断言复活（B5.2 已将其从已删除的 `diagnosisWorkspace` 重指到提升后的 `legacyTransactionsPane`）；
- 两个 `-Wunused-variable` 警告消失。

## 5. 验证（Verification）

1. **正控（PASS 路径）**：`--qml-geometry-check` exit 0、0 GEOFAIL、8 趟全过（`ctest` `qml_geometry_check` 同步 PASS）。
2. **变异探针（证非空转）**：临时把两条断言的比较常数各 +1000.0（制造必然违规的变体），重建后同一命令 exit 1，输出：

   ```text
   GEOFAIL: DEFAULT legacy: legacyTransactionsPane y=220 invades statisticsPanel (y=27 h=168)
   GEOFAIL: DEFAULT legacy: statisticsRow2 y=80 overlaps row1 (y=0 h=72)
   ```

   即两条断言在违规输入下**真实触发**；随后还原源码、重建、门禁恢复全绿（探针未入库）。
3. 真值快照（修复后 dump）：`statisticsPanel_legacy y=27 h=168`（下缘 195），`legacyTransactionsPane y=220`（≥195 ✓）；`statisticsRow1 y=0 h=72`，`statisticsRow2 y=80`（≥72 ✓）。

## 6. 回归保护（Regression Protection）

断言本体即回归保护（`qml_geometry_check` 是常驻 ctest 项）。变异探针方法记录于此，供后续"断言非空转"审计复用：**对任何 `if (ptr && ...)` 形守卫，须证明 ptr 分支可达（存在 + 可触发）**。

## 7. 教训（Lessons）

- "断言写了"≠"断言在断言"：以短路条件守卫的断言，其指针解析失败会退化为永真。
- 该项目已三次踩证据侧假阴性/假阳性（B2.3 旧图、B4.3 键集/旧图、本例空转守卫），共同模式是**证据链自身缺真值对照**。变异探针（故意让断言应 FAIL 并观察其 FAIL）应成为守卫类断言的最小验证动作。
