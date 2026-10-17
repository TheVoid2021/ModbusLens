# R8_13_FINAL_REPORT — T035-C Round SOURCE-REPRO-R8 Completion Cleanup and Full Regression

日期：2026-10-18 00:16（本轮始于 2026-10-17 23:52）
结论：**SOURCE-REPRO-R8 = PASS（全部门禁）**

```text
FIX-02c = PASS
C35 = PASS        R16 = PASS        K125 = PASS
TARGETED REGRESSION = 7/7 PASS
MANUAL_IMPORT = PASS（本次运行）
ISSUE-025 = OPEN / UNRESOLVED（不因单次通过而关闭）
FULL BUILD = PASS（628/628，0 error，50 测试目标）
FULL CTEST = 74/74 PASS，CTEST_EXIT_CODE = 0
SOURCE COMMIT ELIGIBILITY = READY FOR HUMAN REVIEW
SOURCE / TEST COMMIT = NOT EXECUTED
```

---

## A. Trusted Resync

HEAD `517d78c5e5d780d831af5e6d038d70cfe8747ebc`（MATCH）· branch `main` · staged=0 · diff-check PASS · WIP **50**（基线已存，`main.cpp` 起始 `382fd8f1…`）· 冻结 Release/Debug **MATCH**。

## B. R7 已确认根因（复述）

`startActiveRequest` 让**成员 `session_`** begin；`completeRead()` / `completeReadWithTimeout()` 用**局部 session** 完成 ⇒ 成员永久停在 `AwaitingResponse` ⇒ 下一次 read 的 begin 失败（`begin_ok=0`）⇒ R4 FIX-02 诚实 `NotSent` ⇒ C35（busy 未 true / reason 被 disconnected 覆盖）与 R16（无 pending / terminal=0）。

## C. 完成事件与 Session 清理顺序审计

原顺序（两函数同构）：
```
① 局部 session 完成（feed / onResponseTimeout）
② pending_.reset()
③ （缺失：成员 session 终止）
④ emit transactionCompleted(...)
```

**R7 建议的「emit 之后 cancel」未机械采用**（§2.1）。**审计结论**：
- `HarnessWriteTransport` 与 `AnalysisController` 同线程；Qt `AutoConnection` ⇒ **`emit transactionCompleted` 同步调用 `handleSerialTransactionCompleted`**；
- 该回调会更新 readResult / `serialBusy_` 并可能触发 QML 绑定；**若回调将来启动新请求，emit 后的 cancel 会误杀新事务**；
- ⇒ **采用 §2.1 推荐顺序**：**在 `pending_.reset()` 之后、`emit` 之前**终止成员 session。

## D. Qt 信号重入风险的实际检查

| 检查 | 结果 |
|---|---|
| `transactionCompleted` 连接方式 | 同线程 `AutoConnection`（**同步**） |
| 回调内是否 `connectSerial`/`readHoldingRegistersOnce` | **未发现**（仅更新结果状态） |
| 结论 | **cancel 必须在 emit 前**（已实施）；即便未来回调变为启动新请求，也不会被误 cancel |

## E. FIX-02c 精确修改位置（仅 `src/main.cpp`）

| 函数 | 位置 | 新增 |
|---|---|---|
| `HarnessWriteTransport::completeRead()` | `src/main.cpp:5172-5179` | `session_.cancel();`（在 `pending_.reset()` 之后、`emit` 之前） |
| `HarnessWriteTransport::completeReadWithTimeout()` | `src/main.cpp:5212-5216` | 同上 |

`grep -n "FIX-02c"` → 两处；`grep -n "session_.cancel()"` → 4 处（含 R4 `simulateAdapterRemoval`、R6 `closePort`，语义分别为「适配器移除」「断开连接」）。

## F. 修改前后事务不变量

| 不变量 | 修改前 | 修改后 |
|---|---|---|
| 完成请求：local done + pending cleared + **exactly one result** | ✔ | ✔（未变） |
| 完成请求：**成员 session 终止** | ✘（残留 AwaitingResponse） | **✔** |
| 下一合法请求 `beginActiveRequest` | ✘（失败） | **✔ 成功** |
| 合法活动请求不被意外 cancel | ✔ | ✔（cancel 仅在完成分支、pending 已清后） |
| 无效 descriptor 仍被拒 | ✔ | ✔（`beginOk` 检查保留） |
| 重入新请求不被旧清理 cancel | —（原会残留） | **✔（cancel 在 emit 前）** |

## G. 隔离构建

`build/t035c-source-fix-r8-20261017/`（预先不存在）· CONFIGURE **0** · BUILD **0（628/628，0 error）** · 测试可执行文件 **50** · `modbuslens.exe` = `b1d483e350dabe41325580849236e9ffdfc7312239e09c776c3c5e880be13146` · `SOURCE_MODIFIED_BY_BUILD = NO`（WIP 仍 50，仅 `src/main.cpp` 变化）

## H/I/J/K/L. 七项目标回归（一次完整序列，EXE 同上）

| 测试 | EXIT | 关键断言 |
|---|---|---|
| `qml_read_result_check` | **0** | K125：125 寄存器 begin 成功、255 字节正确解析、value count=125 |
| `qml_write_foundation_check` | **0** | **C35：busy→true 建立，reason 保持 `busy_became_true`（disconnect 不再覆盖）** |
| `qml_production_write_check` | **0** | **R16：click 被接受、pending 存在、adapter removal 产生正好 1 个 `TransportError/PossiblySent` terminal** |
| `qml_write_foundation_check_windows` | **0** | 同 C35（windows 平台） |
| `register_readiness` | **0** | **27 passed / 0 failed** |
| `qml_register_map_check` | **0** | 滚动 viewport 契约（R5）仍有效 |
| `qml_active_profile_check` | **0** | semantic 正例 + **内建负例**（visible unauthorised overlay detected） |

**TARGETED = 7/7 PASS · FAILURES = 0 · 无任何 `WRITEFAIL/PRODWRITEFAIL/READFAIL/REGFAIL/ACTFAIL` 输出**

## M/N. Manual Import 独立复核

| 项 | 结果 |
|---|---|
| 命令 | `ctest -R "^manual_import_pdf_docx$"`（R8 目录） |
| EXIT | **0** |
| 结果 | **Passed 1.90 sec**（1/1，0 failed） |
| **ISSUE-025** | **OPEN / UNRESOLVED**（**单次通过不证明 flaky 已修复**；R5 的 contentHash mismatch 记录保留） |

## O. 完整 CTest（唯一一次）

```
TESTS_DISCOVERED = 74
TESTS_APPROVED   = 74（与此前白名单一致；未新增真实串口/VSPD/硬件/外部 API 测试）
TESTS_EXECUTED   = 74
TESTS_PASSED     = 74
TESTS_FAILED     = 0
TESTS_SKIPPED    = 0
CTEST_EXIT_CODE  = 0
Total Test time  = 235.12 sec
```
`LastTest.log` 已另存 `R8_11_LastTest_copy.log`。**R1（70/74）、R5（70/74）、R6（4/7）记录均未改写。**

## P. FIX-02c 是否影响生产路径

**否**。改动仅位于 `HarnessWriteTransport`（`src/main.cpp` 内、仅 `--qml-*-check` argv 分支可达）：
- `SerialPortAdapter` / `AnalysisController` / `EngineeringReadController` / 产品 QML / `CMakeLists.txt` **本轮零修改**（`CHANGED = 1` 证实）；
- `SerialTransactionSession`（生产核心）**未修改**；`session_.cancel()` 是其公开 API；
- 不打开 COM7/COM8，不产生 RTU，不影响已冻结的 V4 验收产物。

## Q. Git / WIP / EXE 安全审计

HEAD 未变 · staged=0 · diff-check PASS · WIP **50** · **CHANGED = 1（仅 `src/main.cpp`）**、ADDED=0、REMOVED=0（50 项逐字节比对）· 其余 49 项（含 4 份面试资料、canonical 7 份、`tests/test_register_readiness.cpp` 的 FIX-01）**完全未变** · 两冻结 EXE **MATCH** · 归档 79 / 原始证据 75 完整 · 无 add/commit/push/tag/package · 未碰 VSPD/COM7-COM8 · **VERIFIED LKGC = UNCHANGED**

## R. 46 文件源码提交资格

```
SOURCE COMMIT ELIGIBILITY = READY FOR HUMAN REVIEW
```
（**仍需 Human 审核后才可提交**；本轮按 §12 明确不提交）

## S. 下一轮 Human 决策事项

1. **是否授权提交 46 文件源码**（按 P 节「原子提交」方案，或再拆分）；
2. **ISSUE-025**：本轮 manual_import 单次通过，但问题仍 OPEN——是否授权独立修复（contentHash 稳定性）或书面风险接受；
3. **R7 遗留的 trace 代码**（`MODBUSLENS_HARNESS_TRACE` 守卫的 `[R7C35]/[R7R16]` 与更早的 `[HARNESS]/[SEMDIAG]/[REGDIAG]`）：是否在源码提交前保留（**建议保留**：全部受环境变量守卫、零默认开销）；
4. 远程推送策略（ahead 26）与外部备份。

---

## 最终状态

```text
FIX-02c = PASS
C35 = PASS
R16 = PASS
K125 = PASS
TARGETED REGRESSION = 7/7 PASS
MANUAL_IMPORT = PASS (THIS RUN ONLY)
ISSUE-025 = OPEN / UNRESOLVED
FULL BUILD = PASS（628/628，0 error）
FULL CTEST = 74/74 PASS（CTEST_EXIT_CODE = 0）
SOURCE-REPRO-R8 = PASS
SOURCE COMMIT ELIGIBILITY = READY FOR HUMAN REVIEW
SOURCE / TEST COMMIT = NOT EXECUTED

C2.2 VIRTUAL SERIAL = HUMAN ACCEPTED / CLOSED
C2 REAL HARDWARE = PENDING / NOT VERIFIED
T035-C OVERALL = NOT YET CLOSED
VERIFIED LKGC = UNCHANGED
```
