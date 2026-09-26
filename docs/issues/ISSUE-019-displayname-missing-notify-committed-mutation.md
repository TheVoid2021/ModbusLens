# ISSUE-019: setDisplayName 缺失 NOTIFY 信号 —— NC-B1 负向对照变异被提交进行为提交

## 现象（Observed）

M12-B first slice 的自动化验收门禁 `qml_profile_editor_check` 7 项 FAIL
（Debug/Release ctest 43/44），其中最反直觉的一项：控制器 C++ 状态完全正确
（`validationText="设备名称必填"`、`dirty=true`），但 QML 的
`profileValidationText` Label 与 `profileDirtyIndicator` 在事件循环多转一圈后
仍然显示旧值（text=[]、visible=0），且全程零 QML 警告。

## 期望（Expected）

任何 identity 字段写入都必须发出 `editorChanged`，所有绑定它的 QML 属性
（dirty 指示、validation 文本、编辑器字段）必须同步刷新。

## 影响

- 仅改 displayName 时：dirty 指示不出现、validation 文本不出现。
- 这是**通知缺陷（notification defect）**而非计算缺陷：所有 getter
  （`dirty()`/`validationText()`）都是按需计算，C++ 单元测试全部 PASS，
  只有 QML 绑定可见 —— 23 个 controller tests 无一能抓住它。

## 复现步骤

1. `ctest -R qml_profile_editor_check`（修复前）→
   `PROFFAIL: the validation text is not visible`。
2. 或：打开种子档案 → 只把「设备名称」清空 → dirty 指示不亮、
   validation 标签不出现（C++ 侧 `dirty()` 返回 true）。

## 定位过程

用临时仪器化（PROFSTATE/PROVDEFER/PROVIDENT）证实：控制器对象同一、
getter 返回新值、QML 无警告 → 唯一可能是 **NOTIFY 信号没有发出**。
读 `src/ui/profile/ProfileController.cpp` 的 `setDisplayName()`：

```cpp
m_draft.displayName = converted;
// NC-B1: dirty not set          ← 变异标记注释
}                                  ← emitEditorChanged() 缺失
```

`git show HEAD:src/ui/profile/ProfileController.cpp` 证实该变异
**已被提交**（behavior commit `da07f43`）——NC-B1 负向对照做完后忘了还原，
且变异标记注释留在了提交里。

## 根因（Root Cause）

流程缺陷 × 技术缺陷叠加：

1. **流程**：NC-B1 负向对照（临时移除 emit → 门禁红 → 还原）的“还原”步骤
   被跳过，变异源码进入了行为提交 `da07f43`。
2. **技术**：`editorChanged` 是唯一 NOTIFY；getter 按需计算使 C++ 测试
   对“缺通知”完全免疫 —— 通知契约没有任何自动化回归保护。

## 解决方案（Fix）

- `setDisplayName()` 恢复 `emitEditorChanged()`，删除变异注释
  （behavior commit `b502ea8`）。
- 新增通知契约测试 **b1c21**（displayName 写入 ⇒ editorChanged 恰好 1 次；
  幂等写入 ⇒ 0 次）与 **b1c22**（5 个 setter 各通知 1 次）——
  用 QSignalSpy 把“通知”本身变成被测对象。

## 验证（Verification）

- `modbuslens_profile_controller_tests`：**35 passed, 0 failed**。
- `qml_profile_editor_check`：**PASS**（stage 6 由 FAIL 转 PASS）。
- Debug/Release full CTest：**44/44 / 44/44**。

## 教训（Lessons）

1. **负向对照必须有“还原后复绿”的闭环证据**，否则变异可能就停在源码里；
   含变异标记注释（`// NC-B1: ...`）的源码出现在 `git show` 输出里是可直接
   grep 的红旗。
2. **NOTIFY 契约需要显式测试**：按需计算的 getter 让“状态对但界面不刷”
   成为 C++ 测试盲区；QSignalSpy 是最小充分手段。
3. 上轮报告在 43/44 的情况下写 “AUTOMATED PASS”，把门禁失败归因为
   “QQuickPopup automation 限制”——实际 6/7 项失败是真实产品缺陷或
   harness 缺陷（见 T027 §37 逐项分类）。
