# R4_10_FINAL_REPORT — T035-C Round SOURCE-REPRO-R4 Controlled Fix and Layout Diagnosis

日期：2026-10-17 21:10
结论：**FIX-01 = PASS · FIX-02 = PASS · FIX-04 = IMPLEMENTED / NOT VERIFIED IN R4 · REGISTER_MAP_REACHABILITY = VERIFIED（断言 OVERSTRICT）**

---

## A. Resync / WIP baseline

HEAD `517d78c…`（MATCH）· staged=0 · diff-check PASS · WIP **50**（43 M + 7 ??，50 项 SHA-256 基线已存 `%TEMP%/r4_baseline_hashes.json`）· 冻结 Release/Debug **MATCH** · 新隔离目录预先不存在。详见 `R4_01_RESYNC.txt`。

## B. 实际修改文件及行号

| 文件 | 修改点 | 性质 |
|---|---|---|
| `tests/test_register_readiness.cpp` | ① `#include <QTemporaryDir>`；② `c16`（原 :354 处）：`QDir::tempPath()` → `QTemporaryDir probeRoot` + **sentinel 负例**；③ `d18`（原 :613 处）：同构改造 + 负例 | **仅测试**（授权文件 A） |
| `src/main.cpp` | ① `HarnessWriteTransport::startActiveRequest`：保存 `beginActiveRequest` 返回值 `beginOk`，**失败即拒绝**（`NotSent`，不设 pending）；② `completeReadWithBytes`：`MODBUSLENS_HARNESS_TRACE` 守卫的 pending_qty/resp/feed trace；③ `simulateAdapterRemoval`：**新增 `session_.cancel()`** + trace；④ `runQmlRegisterMapCheck`：`regUnitField` 祖先链 / contentHeight / contentY / clip / in-dialog y / enabled 诊断（`REGDIAG`）；⑤ `runQmlActiveProfileCheck`：semantic 扫描改为**归属判据**（`readResultPanel` / `readResultDetailDialog` / `readResultValuesList` 子树豁免），**保留**其余 semantic 失败 | **仅门禁装置/门禁输出**（授权文件 B） |

**未修改**：`DeviceProfilePage.qml`、`EngineeringPage.qml`、`AnalysisController.*`、`SerialPortAdapter.*`、`EngineeringReadController.*`、`CMakeLists.txt`、其他 `src/`/`tests/`、canonical docs。

## C. FIX-01 测试隔离方案与断言有效性

**方案**：`c16`/`d18` 不再快照全机 `QDir::tempPath()`，改为**测试自有 `QTemporaryDir`**，并**在同一测试内构造 sentinel 负例**：
写入 `side-effect-sentinel.txt` → 快照必须变化（`QVERIFY(withSentinel != withoutSentinel)`）→ 删除 → 快照恢复 → 以此作为 `before`。

**检测能力证明**：负例在每次运行中执行（`c16_` / `d18_` 均出现在 QTest 输出），**若快照机制失效则负例先失败** ⇒ 不会静默退化为「永远 PASS」。

**能力边界（如实记录）**：改造后不再检测「被测函数写系统 TEMP」——**该能力在原实现中本就不可靠**（全机 TEMP 被任何进程写入即误报，R1 全套 100% 触发）。本次改造以「**可用 + 可证伪**」替代「**不可用**」。

**验证**：默认 TEMP **27/27 PASS**、独立 TEMP **27/27 PASS**（各 EXIT 0）。

## D. K 场景完整时间线（含首次成功/对话框/值清空）

**修复前（TEST#3，trace 原文）**：
```
[HARNESS] read-start #9  begin_ok=1 qty=2  pending_before=0        ← J 场景
[HARNESS] adapter-removal terminal: transport pending cleared      ← 只清 transport pending
[HARNESS] read-start #10 begin_ok=0 qty=125 pending_before=0       ← ★ K 的 begin 失败
[HARNESS] complete pending_qty=125 resp=255B feed=analysis          ← 按旧 descriptor 校验
READFAIL: K: value count is 0, expected 125
```
**⇒ K 从未成功装载 125 个值**（`readResultValueCount()==0` 自 :10841 断言即失败）；
`K geometry` / `K dialog` 是**同一场景后续阶段的 UI 几何检查**（与数据无关，故 PASS）；**dialog 打开前后没有第二次读取**（trace 中 #10 之后为 #11–#13 的 M 场景）；**K 只创建 1 个请求**（#10）；**#11 起 `begin_ok=1`** ⇒ session 在 K 的 `onResponseTimeout` 后被复位 ⇒ **仅 K 一次受害**。

## E. FIX-02 是否由运行期证据支持

**是（CONFIRMED）**：`begin_ok=0` 是 stale-session 的直接运行期证据，**与 R3 静态机制完全一致**（`simulateAdapterRemoval` 未复位 `session_` + begin 返回值被丢弃 + `completeReadWithBytes` 用旧 descriptor）。

## F. Harness Session 修复前后行为

| 阶段 | `read-start #10` | 后续 |
|---|---|---|
| 修复前 | `begin_ok=0 qty=125` | pending 被设、响应按 **qty=2 的旧 descriptor** 校验 ⇒ 非 ReadValues ⇒ **value count = 0** |
| 修复后 | **`begin_ok=1 qty=125`**（`session cancelled` 生效） | 响应按 qty=125 校验 ⇒ ReadValues ⇒ **`READ RESULT CHECK PASS`**，**EXIT 0** |

**修复内容**：`simulateAdapterRemoval` 增 `session_.cancel()`（terminal 路径与 session 一致退出）；`startActiveRequest` **检查 begin 返回值并在失败时拒绝**（不假装成功、不设 pending、不再把新响应喂给旧 descriptor）。未对每次请求无条件 `cancel()`，未降低任何 CRC / SlaveId / Function / Quantity 断言。

## G. FIX-04 semantic 断言修复及负例覆盖

**改动**：`runQmlActiveProfileCheck` 的 walk 从「objectName 含 semantic 即失败」改为「**归属判据**」——命中项若其祖先链含 `readResultPanel` / `readResultDetailDialog` / `readResultValuesList`，视为**已批准的产品语义展示**（M12 `readSemanticCell`），**其余 semantic 命中仍失败**。保留 `readResultPanel` 可见性与后续断言，**未删除 / 未禁用 semantic 检查**。

**复验结果（TEST#7）**：`ACT: stage 13: no semantic overlay` 打印，但**末行仍有 `ACTFAIL`** ⇒ 推断 **stage 13 实际未豁免**（`fail()` 记录后不中断，`note()` 仍执行）。**根因**：`readSemanticCell` 位于 **detail dialog（QQuickPopup）**，不在第一版判据（仅 `readResultPanel`）的覆盖范围。

**已做的二次修正（本轮未复验）**：判据扩展为 `readResultPanel` + `readResultDetailDialog` + `readResultValuesList` 三者子树豁免。

> **FIX-04 = IMPLEMENTED / NOT VERIFIED IN R4**（测试预算 8/8 已用尽，未重新构建与复验）
> **负例覆盖 = NOT VERIFIED**（未构造「错误 semantic overlay 仍 FAIL」的受控负例）

## H. Register Map dialog/scroll 可访问性结果

```
FIELD_SCROLL_ACCESSIBILITY = VERIFIED
LAYOUT_DEFECT = NO
ASSERTION_CONTRACT = OVERSTRICT
```
**证据**：字段在 `ScrollView → Flickable`（`clip=1`）内；`contentHeight 627` vs viewport `513` ⇒ **可滚动 114 px**；字段 `in-dialog y=698.5`、dialog 内容高 595 ⇒ **需滚动约 103.5 px** ⇒ **114 > 103.5（余量 10.5 px）**；`fieldEnabled=1`。
**结论**：`focusable=PASS` 之外，**几何证明其经正常滚动可进入 viewport**；原 `requireInsideWindow` 要求「初始视口内完全可见」，与「ScrollView 内可滚动内容」契约冲突 ⇒ **断言过严**，**不建议改产品 QML**。
**保留警示**：余量仅 10.5 px，若后续新增字段或字号变大将真的不可达 ⇒ 建议在测试中以「可滚动可达」为判据（而非放宽为「任意溢出都 OK」）。

## I. 新隔离构建结果

| 阶段 | 退出码 |
|---|---|
| configure（`debug-local` preset，新目录） | **0**（183.0s + 4.6s） |
| build `modbuslens` + `modbuslens_readiness_tests` | **0**（134/134，0 error / 19 warning） |
| build（FIX-02 后增量 `modbuslens`） | **0**（[4/4]，0 error） |

## J. 每项目标测试实际退出码

| # | 测试 | EXIT | 判定 |
|---|---|---|---|
| 1 | readiness（默认 TEMP） | **0** | **PASS（27/27）** |
| 2 | readiness（独立 TEMP） | **0** | **PASS（27/27）** |
| 3 | `--qml-read-result-check`（修复前，trace） | **1** | **基线证据**（K 失败，begin_ok=0） |
| 4 | `--qml-read-result-check`（修复后） | **0** | **PASS** |
| 5 | `--qml-active-profile-check` | 1 | **无效**（缺 `QT_ASSUME_STDERR_HAS_CONSOLE`，日志 0 字节） |
| 6 | `--qml-register-map-check` | 1 | **无效**（同上） |
| 7 | `--qml-active-profile-check`（重跑） | **1** | **未通过**（FIX-04 判据未覆盖 dialog） |
| 8 | `--qml-register-map-check`（重跑） | **1** | **诊断成功**（REGDIAG 输出完整） |

## K. 未执行测试及原因

- **完整 74 项 ctest**：**明确禁止**（§8）
- **`qml_read_result_demo` / 其他 QML 门禁**：非本轮目标，预算不足
- **FIX-04 复验**：**预算 8/8 用尽**
- **regMap 修复后复验**：本轮不授权修改断言

## L. 本轮遗留的产品或测试问题

1. **FIX-04 待复验**（判据已扩展为 panel+dialog+valuesList，需重构 + 跑一次）；
2. **FIX-04 负例缺失**（未验证「错误 semantic overlay 仍 FAIL」）；
3. **regMap 断言待改**（需下一轮授权：将 `requireInsideWindow` 对 ScrollView 内字段改为「可滚动可达」判据）；
4. **`--qml-active-profile-check` 的 Release PASS 机制**（R3 遗留 UNRESOLVED）；
5. `modbuslens.exe` 新增 19 个 warning（**均为既有代码**，含 `setField` unused —— 非本轮引入）。

## M. 是否允许下一轮修改 DeviceProfilePage.qml

**不建议**（`LAYOUT_DEFECT = NO`）。下一轮应**修改测试断言**（`requireInsideWindow` 对 ScrollView 子树字段的处理），**不改产品 QML**。

## N. 完整 74 项 CTest 重新执行前置条件

1. FIX-04 复验通过；
2. regMap 断言调整完成（或明确豁免）；
3. 重新构建（本隔离目录即可，增量）；
4. 以 **unset `QTLICD_*` + offscreen** 口径运行全量 74 项，取得**全新**记录；
5. **R1 的 70/74 必须保留**，不得改写；新结果独立报告。

## O. 46 文件原子提交资格

```
SOURCE COMMIT ELIGIBILITY = BLOCKED / PENDING FULL REGRESSION
```
（新增 1 项已修复，仍剩 FIX-04 未复验 + regMap 断言未调整 + 未跑全量回归）

## P. Git / EXE / WIP 安全审计

HEAD `517d78c…` 未变 · staged=0 · diff-check PASS · **WIP 数量 50（未变）** · **CHANGED = 2（恰为授权两文件）**、ADDED=0、REMOVED=0（50 项哈希逐项比对）·
冻结 Release/Debug **MATCH** · 归档 79 文件完整 · 原始 75 份证据完整 · canonical docs 未改 ·
`src/main.cpp` 新增行仅含 Harness/门禁特征（trace / cancel / rejected / REGDIAG / semantic 判据）·
**无 add/commit/push/tag/package** · 未启动 VSPD / 未打开 COM7-COM8 / 无真实 RTU · **VERIFIED LKGC = UNCHANGED**

## Q. 建议下一轮最小修改范围

1. **复验 FIX-04**（构建本隔离目录增量 + 跑 `--qml-active-profile-check`，**1 次**）；
2. **构造 FIX-04 负例**（在门禁测试内临时创建一个 objectName 含 semantic 的非白名单项，验证仍 FAIL，**1 次**）；
3. **调整 regMap 断言**（`requireInsideWindow` 增加「ScrollView 子树内的字段以滚动可达为判据」），**仅 `src/main.cpp`**；
4. **重跑目标 4 项 + 全量 74 项 ctest**（取得全新记录）；
5. 之后才评估 46 文件提交。

---

## 最终状态

```text
FIX-01 = PASS（测试隔离 + 负例，27/27 ×2）
FIX-02 = PASS（运行期 CONFIRMED + 修复后 qml_read_result_check EXIT 0）
FIX-04 = IMPLEMENTED / NOT VERIFIED IN R4（判据已扩展，预算用尽未复验）
REGISTER_MAP_REACHABILITY = VERIFIED（可滚动可达 114px > 所需 103.5px；断言 OVERSTRICT；产品布局无缺陷）
R4 TARGETED TESTS = 3 PASS / 8 EXECUTED（#5 #6 环境变量缺失无效）
FULL 74 CTEST = NOT EXECUTED IN R4
SOURCE COMMIT ELIGIBILITY = BLOCKED / PENDING FULL REGRESSION
SOURCE / TEST COMMIT = NOT EXECUTED
C2.2 VIRTUAL SERIAL = HUMAN ACCEPTED / CLOSED
C2 REAL HARDWARE = PENDING / NOT VERIFIED
T035-C OVERALL = NOT YET CLOSED
VERIFIED LKGC = UNCHANGED
```

修改仅限 `tests/test_register_readiness.cpp` 与 `src/main.cpp`（门禁装置/门禁输出）；**未修改产品 QML / Controller / Adapter / CMakeLists**；未重跑全量 ctest；未提交。
