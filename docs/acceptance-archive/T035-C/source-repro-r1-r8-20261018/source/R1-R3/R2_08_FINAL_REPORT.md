# R2_08_FINAL_REPORT — T035-C Round SOURCE-REPRO-R2 Four-Test Failure RCA

日期：2026-10-17 20:24
结论：**FOUR-FAILURE RCA = PARTIAL**（1 项 CONFIRMED / 3 项 P1 候选且机制未完全判定）
**SOURCE COMMIT ELIGIBILITY = BLOCKED / PENDING HUMAN DECISION**

---

## A. R1 原始失败记录与可信度

| 事实 | 值 | 可信度 |
|---|---|---|
| SOURCE BUILD | PASS（628/628，0 error，11m52s） | 高（独立隔离构建） |
| AUTOMATED TESTS | **FAIL 70/74（CTEST_EXIT=8）** | 高（原始日志 `03_ctest_full.log` 保留） |
| 失败项 | `register_readiness` / `qml_read_result_check` / `qml_register_map_check` / `qml_active_profile_check` | 高 |
| 失败详情完整性 | `register_readiness` **无 stdio 输出**；3 项 QML 有 FAIL 行 | 中（register_readiness 缺断言详情） |

## B. R1 STOP 后继续诊断的过程偏差

```
R1 PROCESS DEVIATION = DIAGNOSTICS CONTINUED AFTER INITIAL FAILURE
```
R1 在 4 项 FAIL 后未立即 STOP，继续执行了单项测试与三重对照（违反原指令 §7）。证据保留、FIAL 记录未被改写。
**R1 的「Release PASS ⇒ Debug-only」结论已由本轮 §C 判定为无效（对照不等价）。**

## C. `register_readiness` 机制分析

**根因 CONFIRMED（机制级，源码可证）**：`tests/test_register_readiness.cpp:340/354` 与 `:613/616` 的
`c16_deterministicAndSideEffectFree()` / `d18_wholeValidationChainHasNoSideEffects()`
对 **`QDir::tempPath()`（全机共享 TEMP 目录）** 做全量快照（文件名 + 大小 + mtime 毫秒）并 `QCOMPARE(after, before)`。
**全套 ctest 运行期间任何同机进程/其他测试对 TEMP 的写入**都会导致差异 ⇒ 失败；单独运行（无干扰）= 27/27 PASS。

**⇒ 测试脆弱性（P2 非阻塞），非产品缺陷**（被测函数为纯函数；无 I/O）。

## D. `qml_read_result_check`（K 场景）机制分析

- 失败断言：`READFAIL K: value count is 0, expected 125`
- 场景源码（`src/main.cpp:10826-10845`）：
  1. `scanRead(1, 0, 125, 1000)` → `transport->setCompleteReadImmediately(false)` + `controller->readHoldingRegistersOnce(...)`
  2. 下一步 `transport->completeReadWithBytes(responseWith(1, values), 25ms)` ← **手动完成注入，存在**
  3. 断言 `readResultValueCount() == 125`
- **C2.1 曾修改 `completeReadWithBytes`**（改用成员 `session_` 的 `begin→feed` 同一状态机，替代「局部 session 重放」）——**该修改是否影响 K 场景的 125 寄存器完成路径，尚未验证**
- 失败形态（value count = 0）与「completion 未被 `handleSerialTransactionCompleted` 接受」一致

**证据等级：PARTIAL / UNRESOLVED**（机制候选明确，但缺少 K 场景专项诊断执行）

## E. `qml_register_map_check` 机制分析

- 失败断言：`regUnitField outside: x=257.5 y=698.5 w=729 h=32 win=1000x700`（全套/offscreen）
  ；单跑 windows 平台为 `y=705.5`（**两个平台均溢出**：底部 730.5 / 737.5 > 700）
- 断言实现（`src/main.cpp:16628` `requireInsideWindow`）：`QRectF(0,0,w,h).contains(rect)` — **严格包含，不允许滚动可达但仍溢出**
- 布局来源：`DeviceProfilePage.qml:2495 objectName: "regUnitField"`（`TextField`，位于寄存器编辑对话框的 `Layout.fillWidth: true` 行内）
- **规模差异**：HEAD 版 `DeviceProfilePage.qml` = **2134 行**；当前 WIP 版 = **2558 行（+424 净增，C1 改动）**

**证据等级：PARTIAL**（布局溢出为事实；是否由 C1 的 +424 行改动引入**未证明**——需 HEAD 源码 Debug 构建对照）
**判定候选**：**A（实际不可操作）/ C（布局确有缺陷）** 均可能；**不能**判为 B（滚动可达）——因为断言用的是 `contains` 而非可滚动性检查，且 5.5–37.5px 溢出在 1000×700 最小验收尺寸下可能遮挡/裁切。

## F. `qml_active_profile_check` 机制分析

- 失败断言：`ACTFAIL: a semantic overlay object appeared`（`src/main.cpp:17532`）
- 检测逻辑：`walk(window->contentItem())` 遍历**全窗口所有已实例化子项**，`objectName().contains("semantic", Qt::CaseInsensitive)` 命中即失败
- **命中源已定位（CONFIRMED）**：`src/ui/qml/pages/CommunicationPage.qml:1039 objectName: "readSemanticCell"`
- **`readSemanticCell` 在 HEAD 中已存在**（`git show HEAD:…` 命中 1 处），引入提交 = **`f1567a5 M12: add profile semantic readout`（2026-09-26）**，且 `CommunicationPage.qml` **不在本轮 WIP 中**
- HEAD 版 `main.cpp` 亦含该断言 ⇒ **HEAD 源码自身即存在该断言与 M12 功能的冲突**

**证据等级：CONFIRMED（冲突源）/ UNRESOLVED（为何 Release PASS）**
**待解释矛盾**：冻结 Release（2026-10-14，由 HEAD 源码构建）跑同一门禁 **EXIT 0** —— 若 Release 源码含 `readSemanticCell` 与断言，则应当 FAIL。**该矛盾本轮未解决**（可能为：该 cell 位于延迟加载的详情面板中，Release/Debug 的实例化时机不同；或 Qt 版本内部 objectName 行为差异）。
**⇒ 不可声称「Debug-only 无害」。**

## G. Release/Debug 对照有效性

```
RELEASE COMPARISON = NOT EQUIVALENT
```
**决定性证据**：`build/release/build.ninja` 中 `RegisterReadiness` = **0**、`EngineeringReadController` = **0**
⇒ **Release EXE 由 HEAD 时代源码构建，不含 C1/C2 全部 WIP**。
因此 Release 的 3 项 EXIT 0 **不能**证明新源码下的失败是无害 Debug 现象。详见 `R2_06_DEBUG_RELEASE_COMPARISON.md`。

## H. 原始测试数据与持久化状态检查

- 4 项失败测试均**不访问用户 Device Profile**（readiness 为纯函数；QML 门禁使用内部构造数据/临时 managed root）
- **未打开 COM7/COM8**、**未触发真实外部 API**
- 未设置 APPDATA/TEMP 重定向（**对照可比性未被破坏，但也意味着 `register_readiness` 的 TEMP 共享问题必然存在**）

## I. 本轮新增诊断次数及结果

```
R2 NEW DIAGNOSTIC RUNS = 0（上限 6）
```
全部结论来自**静态源码分析 + R1 保留日志**。未执行任何新测试（§6 授权未使用）。

## J. 每项失败的因果分类与证据等级

| 测试 | 因果分类 | 证据等级 | 说明 |
|---|---|---|---|
| `register_readiness` | 测试断言过宽（全机 TEMP 快照） | **CONFIRMED** | 非产品缺陷 |
| `qml_read_result_check` | **候选：C2.1 `completeReadWithBytes` 改动的副作用**（K 场景 125 寄存器完成路径） | **PARTIAL / UNRESOLVED** | 需 K 场景专项诊断 |
| `qml_register_map_check` | **候选：C1 `DeviceProfilePage.qml` +424 行引入的布局溢出（30.5–37.5px）** | **PARTIAL** | 需 HEAD 源码 Debug 对照 |
| `qml_active_profile_check` | **CONFIRMED 冲突源**（`readSemanticCell` vs 断言）；**Release PASS 机制 UNRESOLVED** | **PARTIAL** | 断言与 M12 功能冲突为事实 |

## K. 产品质量影响级别

| 测试 | 级别 | 理由 |
|---|---|---|
| `register_readiness` | **P2** | 纯函数行为已单独验证；不影响已验收功能 |
| `qml_read_result_check` | **P1**（候选） | 若为真，表示「125 寄存器读取的完成路径」在 C2.1 改动后不可用——**可能影响 read-result 面板的多值展示**（非 V4 单寄存器路径） |
| `qml_register_map_check` | **P1** | 1000×700 最小验收尺寸下 `regUnitField` 溢出 30.5px ⇒ 可能裁切/不可操作 |
| `qml_active_profile_check` | **P1** | 门禁断言与已批准产品功能（M12 readSemanticCell）冲突 ⇒ **测试需更新或明确豁免**，否则该门禁永久为红 |

## L. 是否需要新增 Issue

**建议新增（需 Human 授权建档）**：
- `ISSUE-026`-style：`register_readiness` TEMP 全目录快照脆弱性（P2）
- `qml_read_result_check` K 场景 125 寄存器 completion 路径（P1 候选）
- `qml_register_map_check` regUnitField 1000×700 溢出（P1 候选）
- `qml_active_profile_check` semantic 断言与 M12 `readSemanticCell` 冲突（P1）

## M. 是否需要修改产品代码

- `register_readiness`：**否**（只改测试）
- 其余 3 项：**可能**（若确认回归）——**本轮不修改**

## N. 是否需要修改测试断言或测试环境

- `register_readiness`：**是**（TEMP 快照改为专属子目录）
- `qml_active_profile_check`：**是**（semantic 检测应排除 M12 已批准的 `readSemanticCell`，或改为只检查 overlay 层级）
- `qml_read_result_check` / `qml_register_map_check`：**待定**（先判定是产品回归还是断言过严）

## O. 最小修复与复验方案

1. **readiness**：`snapshotDir` 改为 `QTemporaryDir` 专属子目录（或 `QDir::tempPath() + "/readiness-probe-" + uuid`）→ 单测 + 全套 ctest 复验；
2. **K 场景**：在 `--qml-read-result-check` 中加一条独立诊断输出（完成事件前后的 `readResultValueCount`/`serialBusy`）→ 判定 completion 是否被接受；
3. **regUnitField**：在 1000×700 下打印 `entryEditorDialog` 的 `contentHeight`/`ScrollView` 状态 → 判定 A/B/C；
4. **semantic**：`walk` 时排除 `readSemanticCell`（产品已批准）→ 或把检测范围限定为「overlay 层的 semantic 对象」；
5. **复验门禁**：修复后必须重跑 **全套 74 项 ctest** + `--qml-read-result-check`/`--qml-register-map-check`/`--qml-active-profile-check` 三项 + 构建，并提供 Release 与 Debug 双口径证据。

## P. 46 文件源码提交条件

```
SOURCE COMMIT ELIGIBILITY = BLOCKED / PENDING HUMAN DECISION
```
理由：3 项 P1 候选（K 场景 / regUnitField / semantic）**机制未完全判定**，且**Release 对照不等价**（无法用 Release PASS 豁免）。
**必须由 Human 在以下三条路径中选择**：
1. **授权最小测试/断言修复**（第 O 节 1–4）→ 复验 → 再提交；
2. **授权「HEAD 源码 Debug 构建」第三组对照**（成本：一次全量 Debug 构建 ~12 min）以判定是否 WIP 引入；
3. **明确豁免**（书面记录 4 项失败为既知，接受源码提交）——**但本轮不得由 Agent 自行批准豁免**。

## Q. Git / EXE / WIP 安全检查

HEAD `517d78c…` 未变 · staged=0 · `git diff --check` PASS · WIP **50（46 源码/测试 + 4 面试）** 未变 ·
冻结 Release/Debug **MATCH** · 产品/测试/QML/CMakeLists **零修改** · 原始 75 份验收证据未修改 ·
C2.2 归档提交与 canonical docs 提交均在历史 · **未执行 add/commit/push/tag/package** · **未启动 VSPD/COM7/COM8** · **VERIFIED LKGC = UNCHANGED**

## R. Human 需要做出的决策

1. 是否接受 `register_readiness` 为 **P2 测试脆弱性**（建议：接受 + 后续修测试）；
2. 对另 3 项 P1 候选：**选 P 节 1/2/3 哪条路径**；
3. 是否需要 Agent 建立 4 个 Issue 档案（需授权，因 `docs/issues/` 属 canonical docs）；
4. 是否授权「HEAD 源码 Debug 对照构建」；
5. 源码提交的最终豁免/修复决策。

---

## 最终状态

```text
SOURCE BUILD = PASS (R1, 628/628)
R1 AUTOMATED TESTS = FAIL (70/74)
FOUR-FAILURE RCA = PARTIAL（1 CONFIRMED / 3 PARTIAL-UNRESOLVED）
RELEASE COMPARISON = NOT EQUIVALENT（关键修正）
SOURCE COMMIT ELIGIBILITY = BLOCKED / PENDING HUMAN DECISION
SOURCE / TEST COMMIT = NOT EXECUTED
C2.2 VIRTUAL SERIAL = HUMAN ACCEPTED / CLOSED
C2 REAL HARDWARE = PENDING
T035-C OVERALL = NOT YET CLOSED
VERIFIED LKGC = UNCHANGED
```

未修改任何源码/测试/QML/CMakeLists；未重编译；未暂存/提交/推送；未操作 VSPD 或真实串口。
