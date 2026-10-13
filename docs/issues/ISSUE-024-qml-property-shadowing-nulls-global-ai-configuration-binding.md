# ISSUE-024: QML Property Shadowing Silently Nulls the Global AI Configuration Binding (B2.2-A-F2)

> 任务关联：T034-B2.2-A（引入，`0a49caa`）/ T034-B2.2-AB（修复，`ee24e01`）。
> 状态：**FIXED / COMMITTED（`ee24e01`，2026-10-12）**。

## Observed

B2.2-A 把 Diagnosis 链迁移到全局 AI 配置权威后，`Main.qml` 的绑定写作：

```qml
aiRuntimeConfiguration: aiRuntimeConfiguration
```

`AnalysisController` 的 QML 面自有属性恰与 root context 注入的 context
property **同名**（`aiRuntimeConfiguration`）。**QML 名称解析规则：对象自有
属性优先于 context property** ⇒ 绑定右侧解析到**对象自身** ⇒ **自引用绑定，
求值为 null**。

后果：生产 UI 中 Diagnosis 链的 `runtimeConfiguration` 为 **NULL**，控制器
**静默失去全局 AI 配置权威**（无报错、无崩溃；配置解析不可达）。

## Expected

QML 对象注入的全局配置绑定必须**实际生效**：被绑定属性非空、类型正确、且
**就是应用全局 `AiRuntimeConfiguration` 实例**（指针身份）。

## Impact

- 生产路径缺陷（Diagnosis 链的全局配置权威失效）；**静默性质**（无错误输出
  可依赖）。
- 发现于 B2.2-A 同日审计（AB 轮修复）；未进入任何 Human 验收/打包。
- 无数据/凭据暴露；无硬件影响。

## Root Cause

QML 名称解析作用域规则 + 命名冲突：**对象自有属性遮蔽同名 context
property** 时，`prop: prop` 形式的绑定变为自引用。
（对照：`CandidateExtractionController` / `AiSettingsPage` /
`ManualQaController` 的既有工作惯例使用 `runtimeConfiguration` 作为 QML 面
属性名——B2.2-A 未沿用该命名惯例。）

## Corrective Actions（`ee24e01`）

- `AnalysisController` 的 QML 面属性改名：`aiRuntimeConfiguration` →
  **`runtimeConfiguration`**（C++ accessors 不变）；
- `Main.qml` 绑定改为 `runtimeConfiguration: aiRuntimeConfiguration`
  （左侧自有属性、右侧 context property，无同名冲突）；
- Manual Q&A 链同样以 `runtimeConfiguration:` 绑定。

## Verification

- **AIWIRE oracle**（`runAiConfigWiringAssertions`，`src/main.cpp`）：对
  **真实加载的 Main.qml 对象图**做运行时**指针身份证明**——每个 probe
  controller（`analysisController` / `manualQaController`）的
  `runtimeConfiguration` 非空、类型正确、**且 === 全局实例**；在
  `runNavCheck` 中执行一次（不是 source grep；失败模式明确：NULL / 类型错 /
  非同一实例）。
- B2.2 工程证据：targeted 9/9 矩阵 PASS；full Release build exit 0；
  **CTest 73/73 PASS**；5/5 native runtime smoke PASS（含 QML Smoke）。

## Regression Protection

- AIWIRE 身份断言常驻 `runNavCheck`（QML Smoke / Nav gate 组成部分）；
- 命名惯例固化：新 QML 绑定注入全局配置时，QML 面属性名一律用
  `runtimeConfiguration`（与 context property `aiRuntimeConfiguration`
  错开）。

## Lesson（工程沉淀）

1. **QML 绑定 `x: x` 在「自有属性与 context property 同名」时是自引用**
   ——静默 null，无警告可依赖（需运行时身份断言捕获）；
2. **对外注入的属性命名必须与注入源命名体系错开**（本仓惯例：
   context property = `aiRuntimeConfiguration`；对象属性 =
   `runtimeConfiguration`）；
3. **跨层接线的回归门禁用「运行时指针身份」而不是「存在性 / 源码搜索」**
   ——身份证明同时排除 NULL、类型错误与「错误实例」三类故障；
4. 与 ISSUE-016（QML 无限定名解析不含中间祖先）同族：**QML 名称解析规则
   的陷阱必须用运行时断言兜底**。

## Status

**FIXED / COMMITTED（`ee24e01`）/ B2.2-C + LC2 checkpoint 回归 73/73 PASS /
HUMAN ACCEPTED（B2.2 范围，2026-10-12）。**
