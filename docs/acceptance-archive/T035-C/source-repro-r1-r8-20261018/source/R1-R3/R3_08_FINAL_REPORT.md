# R3_08_FINAL_REPORT — T035-C Round SOURCE-REPRO-R3 Four-Failure Mechanism Verification

日期：2026-10-17 20:40
结论：**R3 ROOT CAUSE = PARTIAL**（2 项 CONFIRMED / 1 项 STRONGLY SUPPORTED / 1 项 PARTIAL）

---

## A. Trusted Resync

HEAD `517d78c…`（MATCH）· staged=0 · diff-check PASS · WIP **50**（43 M + 7 ??）· 冻结 Release/Debug **MATCH** · 隔离构建产物 `7a06e6f7…` 存在。详见 `R3_01_RESYNC.txt`。

## B. R1/R2 原始失败证据

- R1：`SOURCE BUILD = PASS (628/628)`；`AUTOMATED TESTS = FAIL 70/74`（`CTEST_EXIT=8`）
- 失败：`register_readiness` / `qml_read_result_check` / `qml_register_map_check` / `qml_active_profile_check`
- R2：`RELEASE COMPARISON = NOT EQUIVALENT`（Release EXE 不含 C1/C2 源码；`build/release/build.ninja` 中 `RegisterReadiness`=0、`EngineeringReadController`=0）
- **R1/R2 的 FAIL 记录未被改动**

## C. `register_readiness` — 结构缺陷与实际失败的因果边界

**结构缺陷 = CONFIRMED**：`test_register_readiness.cpp:354/:613` 对 `QDir::tempPath()`（全机共享）做「fileName|size|mtime(ms)」全量快照并 `QCOMPARE(after,before)`。

**原始 ctest 失败原因 = STRONGLY SUPPORTED**（非 CONFIRMED）：R1 日志仅 `***Failed 0.77 sec`，**无 stdio / 无 exception code / 无 crash 痕迹** ⇒ 与 `QCOMPARE` 不相等后正常退出一致；**但无法确认进入的是 c16 还是 d18**（`--output-on-failure` 未捕获）。

**受控验证（诊断 #1）**：独立 TEMP（`r3_isolated_temp_17482`）下 **27 passed / 0 failed / EXIT 0**（9 ms）⇒ **FIX-01 方向可行**；**不据此认定原始失败已证明**。

## D. K 场景（125 寄存器）完整事务链

**链路**：`scanRead(1,0,125,1000)`（`setCompleteReadImmediately(false)` + `readHoldingRegistersOnce`）→ `transport->completeReadWithBytes(responseWith(1, values), 25ms)`（255 字节，byteCount=0xFA=250）→ 断言 `readResultValueCount()==125` 得 **0**。

**装置缺陷（源码级 CONFIRMED）**：
1. `simulateAdapterRemoval`（`main.cpp:5238`）**只 `pending_.reset()`，未复位 `session_`**（C2.1 新增成员）；
2. `startActiveRequest`（`:5075`）**`session_.beginActiveRequest(request)` 返回值被丢弃**；
3. `completeReadWithBytes`（`:5188`）改用 `session_` 后**按 session 的旧 descriptor 校验新响应**。

**J→K 传递**：J 的 `scanRead(qty=2)` → `simulateAdapterRemoval`（session_ 停在 AwaitingResponse）→ K 的 begin(qty=125) 失败被忽略 → session 仍持 qty=2 → 255 字节响应校验失败 → `onResponseTimeout` fallback 产出非 ReadValues 的 analysis → **value count = 0**。

**分类**：**K5 CONTROLLER_RESULT_REJECTED**（完成事件被接受，结果因 analysis class 被拒）。**证据等级 STRONGLY SUPPORTED**（缺运行期 trace）。

**性质：测试装置缺陷（P1），非产品缺陷**（`HarnessWriteTransport` 仅 argv 门禁分支可达；`SerialTransactionSession`/`AnalysisController` 未参与成因）。

## E. `qml_register_map_check` 布局与滚动可达性

**诊断 #2 数据**：`entryEditorDialog` y=73.5 h=595（bottom **668.5**，在窗口内）；`regUnitField` y=698.5 h=32（bottom **730.5**，超窗口 30.5 / 超 dialog 62）；`stage 17: editor fields focusable` **PASS**（字段可聚焦）。

```
LAYOUT_ROOT_CAUSE = PARTIAL
FIELD_SCROLL_ACCESSIBILITY = NOT VERIFIED   ← 门禁未输出 scroll/flickable/contentHeight
PRODUCT_LAYOUT_DEFECT = UNRESOLVED
TEST_ASSERTION_DEFECT = UNRESOLVED
```
C1 使 `DeviceProfilePage.qml` 从 2134 行增至 2558 行（+424），dialog 高度 595 内已容不下 `regUnitField`（y 698.5）。
**不得**判「断言过严」（滚动未验证）；**不得**判「产品必然缺陷」（可达性未验证）。

## F. `qml_active_profile_check` — semantic 对象生命周期与断言冲突

- 断言（`main.cpp:17532`）对 `window->contentItem()` **全窗口递归**，匹配 `objectName` 含 `semantic`（CaseInsensitive）即 `fail`
- **命中源 CONFIRMED**：`src/ui/qml/pages/CommunicationPage.qml:1039 objectName: "readSemanticCell"`
- 该对象属于 **M12 `f1567a5 "M12: add profile semantic readout"`（2026-09-26）**，**HEAD 已含**，`CommunicationPage.qml` **不在 WIP**
- 生命周期：`readSemanticCell` 定义在通信页 read-result 详情表格的 delegate/单元格内 —— **若详情面板未实例化则不命中**（但门禁在 Stage 13 已访问通信页，是否实例化取决于 QML Loader/Component 时机）
- **未解决矛盾**：冻结 Release（由 HEAD 源码构建，同样含该 cell 与断言）**EXIT 0** ⇒ 说明 Release 下该 cell **未实例化或被裁出遍历范围**。**该机制本轮未能证实**（门禁不打印命中项）

```
VALID_EXISTING_PRODUCT_OBJECT  ← CONFIRMED（readSemanticCell 为合法产品语义展示）
ACTUAL_UNWANTED_OVERLAY        ← 未发现证据
OVERBROAD_TEST_SCAN            ← STRONGLY SUPPORTED（全窗口 + 子串匹配）
OBJECT_LIFECYCLE_TIMING        ← UNRESOLVED（Release/Debug 差异的真正机制）
```

## G. Release 对照不等价（重申）

`RELEASE COMPARISON = NOT EQUIVALENT`（R2 已证，本轮复核保留）。
**禁止**再以「Release PASS ⇒ Debug FAIL 无害」作为依据。

## H. 本轮实际新增诊断命令数量

```
R3 DIAGNOSTIC RUNS = 2 / 6
```

## I. 每次诊断的退出码与原始日志

| # | 测试 | EXIT | 日志 |
|---|---|---|---|
| 1 | `modbuslens_readiness_tests`（独立 TEMP） | **0** | `R3_run01_readiness_isolated_temp.txt` / `R3_run01_stdout.log` / `R3_run01_stderr.log` |
| 2 | `modbuslens.exe --qml-register-map-check` | **1** | `R3_run02_q64_full.log` |
| 3–6 | **NOT EXECUTED**（无法在不改门禁代码下获得区分性证据） | — | — |

## J. 四项失败的根因分类

| 测试 | 分类 | 等级 |
|---|---|---|
| `register_readiness` | 测试脆弱性（全机 TEMP 快照） | **CONFIRMED** |
| `qml_read_result_check` | **装置缺陷**（`simulateAdapterRemoval` 未复位 `session_` + begin 返回值未检查） | **STRONGLY SUPPORTED** |
| `qml_register_map_check` | dialog 内容溢出（C1 新增字段后）+ 滚动可达性未验证 | **PARTIAL** |
| `qml_active_profile_check` | **断言扫描过宽**（全窗口 + semantic 子串命中合法产品 `readSemanticCell`） | **CONFIRMED 冲突 / UNRESOLVED 时机** |

## K. 影响等级

| 测试 | 级别 | 理由 |
|---|---|---|
| `register_readiness` | **P2** | 纯函数已单独验证 |
| `qml_read_result_check` | **P1** | 门禁装置缺陷（非产品），但使 125 寄存器读-result 门禁永久为红 |
| `qml_register_map_check` | **P1** | dialog 内容溢出（可达性未验证，风险未排除） |
| `qml_active_profile_check` | **P1** | 断言与 M12 已批准产品功能冲突 ⇒ 门禁永久为红 |

## L. 最小修复文件集合建议（**本轮不执行**）

| 编号 | 范围 | 文件 | 内容 |
|---|---|---|---|
| **R3-FIX-01** | **仅测试** | `tests/test_register_readiness.cpp` | `snapshotDir` 改用 `QTemporaryDir` 专属子目录（或 `tempPath()/readiness-probe-<uuid>`） |
| **R3-FIX-02** | **仅门禁装置**（`src/main.cpp` 的 `HarnessWriteTransport`） | `src/main.cpp` | ① `simulateAdapterRemoval` 及各 terminal 路径增 `session_.cancel()`；② `startActiveRequest` 检查 `beginActiveRequest` 返回值（失败则 `NotSent` + 不设 pending）；③ 可选：`completeReadWithBytes` 前断言 `session_` 的 descriptor == `*pending_` |
| **R3-FIX-03** | **产品 QML 优先** | `src/ui/qml/pages/DeviceProfilePage.qml` | 给 `entryEditorDialog` 内容加 `ScrollView/Flickable`（若缺）或压缩行高使 `regUnitField` 落入 595；**不**放宽 `requireInsideWindow` |
| **R3-FIX-04** | **仅测试** | `src/main.cpp`（`runQmlActiveProfileCheck`） | semantic 扫描限定为「overlay 层对象」或排除已批准的产品语义单元格（白名单 `readSemanticCell`），**保留** semantic 检查本体 |

## M. 是否仅改测试或涉及产品代码

- FIX-01 / FIX-04：**仅测试**（`tests/` 与 `src/main.cpp` 门禁段）
- FIX-02：**门禁装置**（`src/main.cpp` 内 `HarnessWriteTransport`，**不属产品运行时路径**，仅 argv 分支可达）
- FIX-03：**产品 QML**（唯一可能触及产品的修复项；若确认为「可滚动可达」则可能改为测试断言调整，需先验证可达性）

## N. 未来目标测试复验要求

```
BUILD = PASS
TARGETED: register_readiness / qml_read_result_check / qml_register_map_check / qml_active_profile_check = PASS
```
（每项必须提供**修复后全新运行**的日志，不得复用 R1/R3 结果）

## O. 未来完整 CTest 复验要求

修复后需重跑**经预审的完整安全 ctest 集合**（74 项，unset `QTLICD_*`，offline），取得**全新** `N/74` 记录；
当前基线 `R1 FULL CTEST = 70/74 PASS` **不得改写**。
若修复触及生产串口事务逻辑（FIX-02 仅装置类 ⇒ 预期不触及），须评估是否重开 C2.2 虚拟串口门禁。

## P. 46 文件源码提交条件

```
SOURCE COMMIT ELIGIBILITY = BLOCKED / PENDING HUMAN DECISION
```
理由：3 项 P1（2 项测试/装置缺陷 + 1 项产品 QML 待判）尚未处置；**不得**在已知 FAIL 状态下提交源码。

## Q. Git / EXE / WIP 安全审计

HEAD `517d78c…` 未变 · staged=0 · diff-check PASS · WIP **50** 未变 · 冻结 Release/Debug **MATCH** ·
源码/测试/QML/CMakeLists **零修改** · 原始 75 份验收证据未修改 · C2.2 归档提交与 canonical docs 提交均在历史 ·
**无 add/commit/push/tag/package** · **未启动 VSPD / 未打开 COM7-COM8 / 无真实 RTU** · **VERIFIED LKGC = UNCHANGED**

新增文件仅在 `_repro_evidence/`：`R3_01_RESYNC.txt`、`R3_02_03_04_MECHANISM_ANALYSIS.md`、`R3_08_FINAL_REPORT.md`、`R3_run01_*`（3）、`R3_run02_q64_full.log`；**未覆盖 R1 01–15 与 R2 文件**。

## R. Human 下一轮需要授权的具体修改范围

1. **是否授权 R3-FIX-01 + FIX-02 + FIX-04**（仅测试/装置，3 处，风险低）；
2. **FIX-03 的判定路径**：授权一次「滚动可达性诊断」（需在门禁中加入 contentHeight/flickable 打印——**属 tests/门禁修改**）以区分「产品布局缺陷」vs「断言过严」；
3. 是否建立 4 个 Issue 档案（`docs/issues/`）——**需单独授权**；
4. 修复后的复验门禁（N/O 节）；
5. 46 文件源码提交的时序（建议：FIX 完成后单独授权）。

---

## 最终状态

```text
SOURCE BUILD = PASS (R1, 628/628)
R1 AUTOMATED TESTS = FAIL (70/74)
R3 ROOT CAUSE = PARTIAL
  register_readiness            = CONFIRMED（测试脆弱性）
  qml_read_result_check         = STRONGLY SUPPORTED（装置 session_ 未复位）
  qml_register_map_check        = PARTIAL（dialog 溢出；可达性未验证）
  qml_active_profile_check      = CONFIRMED 冲突 / UNRESOLVED 时机
SOURCE COMMIT ELIGIBILITY = BLOCKED / PENDING HUMAN DECISION
SOURCE / TEST COMMIT = NOT EXECUTED
C2.2 VIRTUAL SERIAL = HUMAN ACCEPTED / CLOSED
C2 REAL HARDWARE = PENDING / NOT VERIFIED
T035-C OVERALL = NOT YET CLOSED
VERIFIED LKGC = UNCHANGED
```

未修改任何源码/测试/QML/CMakeLists；未重编译；未暂存/提交/推送；未操作 VSPD 或真实串口；未推进 LKGC。
