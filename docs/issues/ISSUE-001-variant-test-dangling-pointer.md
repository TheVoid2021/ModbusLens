# ISSUE-001: variant 测试辅助函数返回悬垂指针（临时 variant 生命周期）

## 现象

T005 实现阶段，`SimulatedSlaveTest::t01_readOneRegister` 在 debug 构建下偶发断言失败（`Compared values are not the same`），而 T02~T07 全部通过；同批新增的集成测试编译失败（GCC `taking address of rvalue [-fpermissive]` ×4）。

## 影响

- 测试结果不可信：悬垂指针读取已被覆写的栈内存，属于未定义行为——"当前全过"只说明尚未被覆写，随时可能翻车；
- 集成测试无法编译；
- 同一模式潜伏在 T004 已提交的三个测试文件中（codec/f03 均使用相同辅助函数形态）。

## 复现步骤

1. `const auto* frame = as<ModbusRtuFrame>(slave.handleRequest(request));`——`as` 返回 `std::get_if<T>(&result)`，而 `result` 是绑定到**临时 variant** 的 const 引用；
2. 语句结束时临时 variant 析构，`frame` 悬垂；
3. 下一条语句构造 `expected`（栈上对象）恰好覆写原存储；
4. `QCOMPARE(*frame, expected)` 读到被覆写的字节 → 断言失败。

## 定位过程

1. 先怀疑实现：T02~T07 与 T01 走完全相同的代码路径却全过 → 指向测试基建而非被测代码；
2. 集成测试的编译错误（`&右值`）提示了 variant 临时值的生命周期主题；
3. 审查 `as<T>` 辅助函数签名：`const T* as(const Variant& result) { return std::get_if<T>(&result); }`——引用绑定的是调用方传入的临时，指针在其析构后失效。

## 根因

`std::get_if` 需要指向**存活 variant** 的指针；把"取自临时对象的指针"作为函数返回值传出语句边界，即产生悬垂指针（UB）。GCC 对显式 `&右值` 直接报错，而对"const& 绑定临时 + 内部取址"这种等价形态不报警告——后者更隐蔽。

## 解决方案

辅助函数改为**拷贝语义**，从结构上消灭悬垂：

```cpp
template <typename T, typename Variant>
std::optional<T> as(const Variant& result)
{
    if (auto* value = std::get_if<T>(&result)) {
        return *value;   // 拷贝出来，临时对象何时析构都无关紧要
    }
    return std::nullopt;
}
```

调用点从 `const auto* frame = as<T>(f(...))` 改为 `const auto frame = as<T>(f(...))`，断言 `QVERIFY(frame.has_value())` / `QCOMPARE(*frame, expected)`。集成测试同理：先把 decoder 结果绑定到具名局部量，再 `std::get_if`。

## 验证

- 集成测试编译通过（4 处 `-fpermissive` 错误消失）；
- t01 稳定通过，SIM-T01~T07 + SIM-I01 全绿；
- 既有 T004 测试（codec/f03）以相同方式修复后 ctest 全绿——语义零变化（断言内容逐字未动），仅测试脚手架的生命周期修正。

## 教训

1. **返回"指向结果的指针"时必须问一句：结果本身活多久？** `const&` 形参绑定临时是合法的，但把内部指针带出语句边界就是 UB。
2. GCC 对显式 `&右值` 报错，却对等价的悬垂形态静默——编译器不报 ≠ 正确。
3. "全部通过"的测试也可能带 UB；debug 构建的栈布局掩盖了它（-O0 下恰好没被覆写）。
4. 修复选型：`optional<T>`（拷贝）优于"约束调用方先具名局部再取指针"——把正确性做进辅助函数，而不是寄希望于每个调用点都守规矩。