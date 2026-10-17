# R6_12_FINAL_REPORT — T035-C Round SOURCE-REPRO-R6

日期：2026-10-17 23:30
结论：**SOURCE-REPRO-R6 = PARTIAL**
**FAILED_GATE = `WRITE/READ TARGETED = 4/7`**（3 项 write 门禁仍 FAIL）

```
MANUAL_IMPORT      = NOT EXECUTED（§9 前置不满足；R5 已证 ISSUE-025 CONFIRMED）
FULL CTEST         = NOT EXECUTED（§10 前置不满足）
SOURCE COMMIT      = NOT EXECUTED
```

---

## A. Trusted Resync

HEAD `517d78c5e5d780d831af5e6d038d70cfe8747ebc`（MATCH）· staged=0 · diff-check PASS · WIP **50**（R6 基线已存）· 冻结 Release/Debug **MATCH**。

## B. R5 执行纪律偏差

```
R5 PROCESS DEVIATION = CONTINUED AFTER INITIAL TEST FAILURE
```
R5 在首次 FIX-04 测试失败后继续迭代（3 轮），未遵守严格 STOP。成果保留，FAIL 记录未改写。

## C. 三项 Write 测试原始失败（R5）

见 `R6_02_WRITE_HARNESS_RCA.md` §列表（`C35` reason overwritten / `DLG5b` busy 与 reason / `R16` pending 与 terminals）。

## D. 状态机真值表

见 `R6_02_WRITE_HARNESS_RCA.md`（CASE A–F；CASE E = 本次缺陷：terminal 清理不完整导致残留 AwaitingResponse）。

## E. FIX-02 回归的机制与证据等级

**CONFIRMED**：全量清点 9 处 `pending_.reset()`——
- 7 处（`completeRead` / `completeReadWithTimeout` / `completeWriteWithTimeout*` / `completeWriteWithEcho*`）**使用局部 session**，不触碰成员；
- 1 处（`simulateAdapterRemoval`）R4 已加 `session_.cancel()`；
- **1 处 `closePort()` 遗漏**（只有 `portOpen_=false; pending_.reset();`）。

**机制**：`closePort()`（显式终止）后 `session_` 仍为 `AwaitingResponse` ⇒ 后续 read 的 `beginActiveRequest` 失败 ⇒ R4 的 FIX-02 诚实返回 `NotSent` ⇒ `serialBusy_` 未置位（`DLG5b`）、reason 被覆盖（`C35`）、无 pending 可丢（`R16`）。

## F. 实施的条件修复（精确）

**唯一编辑**：`HarnessWriteTransport::closePort()`

```cpp
    void closePort() override
    {
        portOpen_ = false;
        pending_.reset();
        // FIX-02b (SOURCE-REPRO-R6): closePort is an EXPLICIT TERMINATION path
        // (disconnect). Every unfinished transaction ends here, so the shared
        // session must leave AwaitingResponse too. ...
        session_.cancel();
    }
```

**未采用**通用 cancel+retry（§3.1 禁止）；**保留** `startActiveRequest` 的 `beginOk` 诚实拒绝（最后防线）；**保留** `simulateAdapterRemoval` 的 cancel。

## G. 合法事务被错误 cancel 的风险

**无**：`cancel()` 仅在 **`closePort()`**（断开 = 终止）与 **`simulateAdapterRemoval()`**（适配器移除 = 终止）两个**显式终止路径**触发；**不影响 CASE A/B/D/F**（正常进行中的请求不会被 cancel）。

## H. K 场景是否仍通过

**是**：`qml_read_result_check EXIT=0`（125 寄存器 K 场景仍 PASS）。

## I. Busy / Reason / Pending / Terminal 回归

**部分改善**：
- `DLG5b: the read did not make busy true` — **已消失** ✅
- `DLG5b: the reason is not BusyBecameTrue` — **已消失** ✅
- `DLG5b: the dialog is still visible after the busy invalidation` — **已消失** ✅
- **`C35: the reason was overwritten (disconnected)` — 仍存在** ❌
- **`R16: no pending request to lose` / `R16: terminals=0, expected one more` — 仍存在** ❌

## J. 三项 Write 测试结果

| 测试 | R5 | R6 | 剩余失败断言 |
|---|---|---|---|
| `qml_write_foundation_check` | FAIL（3 条） | **FAIL（1 条）** | `C35: the reason was overwritten (disconnected)` |
| `qml_production_write_check` | FAIL（5 条） | **FAIL（2 条）** | `R16: no pending request to lose` · `R16: terminals=0, expected one more` |
| `qml_write_foundation_check_windows` | FAIL（3 条） | **FAIL（1 条）** | `C35: the reason was overwritten (disconnected)` |

## K. 其余原四项回归

| 测试 | EXIT | 结果 |
|---|---|---|
| `qml_read_result_check` | **0** | **PASS**（K 场景） |
| `register_readiness` | **0** | **PASS（27/27）** |
| `qml_register_map_check` | **0** | **PASS**（滚动 viewport 契约） |
| `qml_active_profile_check` | **0** | **PASS**（正例 + 负例） |

## L. Manual Import 与 ISSUE-025 的关系

**分类 = M1（ISSUE-025 已知 contentHash 不稳定复现）· 证据等级 CONFIRMED**

R5 失败原文（`R5_09_FULL_CTEST.log`）：
```
tests/test_manual_import_pdf_docx.cpp(695) : failure location
Actual   (a.document.contentHash): "c44db754e9b368873b49e8b5669374931d5217bcee11986c6320086ed445c199"
Expected (b.document.contentHash): "a51a5cd4410740e36720cc65f9c9c2637d5e49ecbf681dfb436f69813d37319a"
```
**同一 DOCX 字节两次导入 contentHash 不同** = ISSUE-025 的定义特征。R1 同 preset/同 unset-QTLICD 口径下该项 `Passed 1.70s` ⇒ **不稳定（flaky）**，非 R6 引入。

**未修改**：manual import 源码 / 测试源码 / License 逻辑 / ISSUE-025 状态（本轮禁止）。

## M. Manual Import 独立执行

```
MANUAL_IMPORT = NOT EXECUTED
```
原因：§9 前置（§8 七项目标 7/7）未满足。**依据 R5 日志判定为 ISSUE-025 CONFIRMED**（不再为该问题消耗执行）。

## N. 隔离构建

| 阶段 | 结果 |
|---|---|
| `build/t035c-source-repro-r6-20261017/`（预先不存在） | ✅ 新建 |
| CONFIGURE（`debug-local` preset） | **PASS**（exit 0，4.6s tail） |
| BUILD（全量） | **PASS**（**628/628**，exit 0，**0 error**） |
| 测试可执行文件 | **50** |

## O. 完整 CTest

```
FULL CTEST = NOT EXECUTED
```
原因：§10 前置（7/7 + MANUAL_IMPORT PASS）未满足。**未执行、未重跑、未改写 R1/R5 记录。**

## P. 执行命令与退出码汇总

| # | 命令 | EXIT |
|---|---|---|
| 1 | `cmake --preset debug-local -B build/t035c-source-repro-r6-20261017` | **0** |
| 2 | `ninja -C …`（全量） | **0**（628/628） |
| 3 | `--qml-read-result-check` | **0** PASS |
| 4 | `--qml-write-foundation-check` | **1** FAIL |
| 5 | `--qml-production-write-check` | **1** FAIL |
| 6 | `modbuslens_readiness_tests` | **0** PASS（27/27） |
| 7 | `--qml-register-map-check` | **0** PASS |
| 8 | `--qml-active-profile-check` | **0** PASS |
| 9 | `--qml-write-foundation-check`（windows 平台） | **1** FAIL |

## Q. WIP / Git / EXE / 安全

HEAD 未变 · staged=0 · diff-check PASS · WIP **50** · **CHANGED = 1（仅 `src/main.cpp`）**、其余 49 项逐字节一致 · 两冻结 EXE **MATCH** · 归档 79 / 原始证据 75 完整 · canonical docs 未改 · **无 add/commit/push/tag** · 未碰 VSPD/COM7-COM8 · 未改 License/系统配置 · **VERIFIED LKGC = UNCHANGED**

## R. 46 文件提交资格

```
SOURCE COMMIT ELIGIBILITY = BLOCKED
```
（7 项目标 4/7；全量 ctest 未执行）

## S. 下一轮最小待授权事项

**剩余 2 类失败需要定位「第三个残留源」**（`closePort` 修复后仍有）：

1. **`C35: the reason was overwritten (disconnected)`** —— 某场景设置 reason 后 `disconnected` 覆盖。**需在 `runQmlWriteFoundationCheck` 的 C35 场景加 §6 诊断**（`REASON_BEFORE/AFTER`、`PENDING_PRESENT`、`ACTIVE_REQUEST_PRESENT`），定位「哪个 emit 覆盖了 reason」。
2. **`R16: no pending request to lose` / `terminals=0`** —— R16 场景期望有 pending 可丢。**需定位「该场景前哪个请求被 NotSent 拒绝」**（同诊断）。

**建议授权**（最小）：
- 在 `runQmlWriteFoundationCheck` / `runQmlProductionWriteCheck` 的 C35 / R16 段加 `MODBUSLENS_HARNESS_TRACE` 守卫的诊断输出（**仅 `src/main.cpp`**，≤1 处/场景）；
- 重建（R6 目录增量）+ 跑 2 项 → 依证据再决定 FIX-02c；
- **不预先**采用「取消 beginOk 检查」（会重新引入 K 场景的 stale-descriptor 错误）。

---

## 最终状态

```text
FIX-02b（closePort session cancel）= IMPLEMENTED / PARTIALLY EFFECTIVE
  （DLG5b busy/reason 3 条已修复；C35 / R16 仍存在）
WRITE/READ TARGETED REGRESSION = 4/7 PASS
MANUAL_IMPORT = NOT EXECUTED（ISSUE-025 = CONFIRMED，依据 R5 日志）
FULL BUILD = PASS（628/628，0 error）
FULL CTEST = NOT EXECUTED
SOURCE-REPRO-R6 = PARTIAL
FAILED_GATE = WRITE/READ TARGETED（3 项 write）
SOURCE COMMIT ELIGIBILITY = BLOCKED
SOURCE / TEST COMMIT = NOT EXECUTED

C2.2 VIRTUAL SERIAL = HUMAN ACCEPTED / CLOSED
C2 REAL HARDWARE = PENDING / NOT VERIFIED
T035-C OVERALL = NOT YET CLOSED
VERIFIED LKGC = UNCHANGED
```
