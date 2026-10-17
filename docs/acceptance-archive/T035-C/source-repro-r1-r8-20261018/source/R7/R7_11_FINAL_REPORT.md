# R7_11_FINAL_REPORT — T035-C Round SOURCE-REPRO-R7 C35/R16 Causal Trace

日期：2026-10-17 23:56
结论：**C35 ROOT CAUSE = CONFIRMED · R16 ROOT CAUSE = CONFIRMED · 两项同源 = YES（CONFIRMED）**
**本轮仅诊断（instrumentation-only），未实施任何行为修复。**

---

## A. Trusted Resync

HEAD `517d78c…`（MATCH）· branch `main` · staged=0 · diff-check PASS · WIP **50**（基线已存，`main.cpp` 起始 `32c36216…`）· 冻结 Release/Debug **MATCH**。

## B/C. C35 完整 Reason 写入链与实际机制

**链路（trace 原文）**：
```
read-start #1 begin_ok=1 qty=2             ← C13/C14 的 read（completeRead() 完成）
read-start #2 begin_ok=0 qty=2             ← C35 的 read：begin 被拒
read-start rejected: session refused beginActiveRequest
[R7C35] after completeRead:  busy=0 reason=（空）   terminals=0
[R7C35] after disconnect:     busy=0 reason=disconnected terminals=0
WRITEFAIL C35: the reason was overwritten (disconnected)
```

**机制**：C35 的 read **从未被接受**（`beginOk=0`）⇒ `serialBusy()` 从未 false→true ⇒ **`busy_became_true` 从未写入** ⇒ 随后 `disconnectSerial()` 写入 `disconnected`（成为唯一 reason）⇒ 断言「期望 `busy_became_true`」失败。

**Reason 写入点**：① busy→true 时写 `busy_became_true`（未触发）；② `disconnectSerial()` 写 `disconnected`（触发）。
**REASON_PRECEDENCE**：`busy_became_true` 优先于 `disconnected`（C11/C14 已固定），**前提是前者真实写入** ⇒ **断言正确，非断言错误**。

## D/E. R16 完整 Request/Pending/Terminal 链与实际机制

```
read-start #1 begin_ok=1 qty=2    ← R16 前的 read（completeRead() 完成，成员 session 残留）
read-start #2 begin_ok=0 qty=2    ← R16 的 click：begin 被拒
read-start rejected: session refused beginActiveRequest
[R7R16] after click: busy=0 readStarts=2 portOpen=1
PRODWRITEFAIL R16: no pending request to lose
PRODWRITEFAIL R16: terminals=0, expected one more
```
**机制**：click 未被接受 ⇒ 无 pending、busy=false ⇒ 后续 `simulateAdapterRemoval` 无 pending 可终止 ⇒ **terminal=0**（两条断言是同一原因的两个症状）。**分类 = `R16-C`（STALE_SESSION_REJECTED_NEW_REQUEST）**。

## F. 是否同源

**YES（CONFIRMED）**——共同证据：
1. 两者 trace 均为 **`read-start #N begin_ok=0` + `session refused beginActiveRequest`**；
2. 两者前一场景均有 **`begin_ok=1` 的 read**；
3. 共同机制：**`completeRead()` / `completeReadWithTimeout()` 使用局部 session 完成，但 `startActiveRequest` 让成员 `session_` begin ⇒ 成员永久停在 AwaitingResponse**（`src/main.cpp:5152/:5187` 的 `SerialTransactionSession session;`）。

## G. 新增诊断改动精确范围（**无行为变更**）

| 位置 | 内容 |
|---|---|
| C35 `push2` | `completeRead()` 后 / `disconnectSerial()` 后各一条 `[R7C35]` `qWarning`（`busy / reason / terminals`） |
| R16 click 后 | 一条 `[R7R16]` `qWarning`（`busy / readStarts / portConnected`） |

均以 `qEnvironmentVariableIsSet("MODBUSLENS_HARNESS_TRACE")` 守卫；**未新增 cancel / retry / pending 修改 / reason 修改 / 断言修改 / QML 修改 / Session API 修改 / CMakeLists 修改**。

## H. 构建

`build/t035c-source-diagnostic-r7-20261017/`（预先不存在）· CONFIGURE **0** · BUILD **0（129/129，0 error）** ·
`modbuslens.exe` = `7df5f1b5607ab38acb130622a5a78297e0c8c9478b32654bf51576197f869d5b`

## I. 诊断命令与退出码

| # | 命令 | EXIT | 说明 |
|---|---|---|---|
| 1 | `--qml-write-foundation-check`（C35） | **1** | 预期失败；trace 完整 |
| 2 | `--qml-production-write-check`（R16） | **1** | 预期失败；trace 完整 |
| 3 | `--qml-read-result-check`（K125 对照） | **NOT EXECUTED** | §6 允许：机制已足够完整 |

**总执行 = 2 / 3**（预算未用尽）。

## J. 是否出现 trace 引起的时序变化

**未观察到**：两项目标测试的失败断言与 R6 **逐字相同**（`C35: reason overwritten (disconnected)`；`R16: no pending request to lose` / `terminals=0`），失败位置未变 ⇒ **`INSTRUMENTATION_SENSITIVE` 不成立**（非时序假象）。

## K. K 场景对照

**未执行**（Test 3 未运行）。**依据**：C35/R16 的机制与 K 场景**完全解耦**（K 走 `completeReadWithBytes` → 成员 session 的 feed，本次诊断未触碰该函数），且 R6 已确认 `qml_read_result_check EXIT 0`。
**⇒ `K125 CONTROL = NOT EXECUTED`（不做新的 PASS 声明）**。

## L. 建议的最小 FIX-02c（**只设计，未执行**）

在 `completeRead()` 与 `completeReadWithTimeout()` 的完成分支（`pending_.reset(); emit transactionCompleted{...};` 之后）各补一行：

```cpp
        session_.cancel();   // 局部 session 已完成；成员会话（startActiveRequest 期间 begin）必须收尾
```

**论证**：不是通用 cancel+retry——**只在完成分支、`pending_` 已清之后**执行，不 cancel 任何仍在进行中的请求；与 `completeReadWithBytes`（成员 session 的 feed 内部自动 reset）语义对齐；不改变 `beginOk` 诚实拒绝；不改变 `closePort`/`simulateAdapterRemoval` 的 cancel。

**备选（不推荐）**：把 `completeRead*` 改为成员 session（改动更大，无额外收益）。

## M. 修复所需文件集合

**仅 `src/main.cpp`**（`HarnessWriteTransport::completeRead()` / `completeReadWithTimeout()`，2 行）。

## N. 未来门禁

1. 增量重建 `modbuslens`；
2. **目标 7 项**（read_result / write_foundation / production_write / write_foundation_windows / readiness / register_map / active_profile）；
3. `manual_import_pdf_docx` 单跑（判定 ISSUE-025 flaky 是否仍然）；
4. **全量 74 项 ctest 一次**（全新记录，不覆盖 R1/R5/R6）。

## O. ISSUE-025 独立边界

```
manual_import_pdf_docx = R5 FAILURE MATCHES ISSUE-025 SYMPTOM
R6 / R7 RETEST = NOT EXECUTED
ISSUE-025 = OPEN / UNRESOLVED
```
**不自动豁免**：即使 Harness 全修复，若 manual_import 仍 flaky，须由 Human 独立决定修复或风险接受。本轮**未运行** manual import、未改 DOCX/PDF/ContentHash/License/ISSUE 文档。

## P. 安全审计

HEAD `517d78c…` 未变 · staged=0 · diff-check PASS · WIP **50** · **CHANGED = 1（仅 `src/main.cpp`，仅 trace）**，其余 49 项逐字节一致 · 两冻结 EXE **MATCH** · 归档 79 / 原始证据 75 / canonical 7 / 面试 4 完整 · 无 add/commit/push/tag · 未碰 VSPD/COM7-COM8 · 未改 License/ISSUE-025 · **VERIFIED LKGC = UNCHANGED**

## Q. 下一轮需 Human 授权

1. **实施 FIX-02c**（`src/main.cpp` 两处 `session_.cancel()`，2 行）；
2. 重建 + 目标 7 项 + manual_import 单跑 + 全量 74 项一次；
3. （可选）是否同时把 K125 对照纳入下一轮目标集（当前 7 项已含 `qml_read_result_check`）。

---

## 最终状态

```text
R7 C35 RCA = CONFIRMED
R7 R16 RCA = CONFIRMED
R7 C35/R16 同源 = YES (CONFIRMED)
R7 INSTRUMENTATION = PASS（构建 129/129；trace 生效；无行为变更）
R7 NEW BEHAVIOR FIX = NOT EXECUTED
R7 FULL CTEST = NOT EXECUTED
SOURCE COMMIT ELIGIBILITY = BLOCKED
SOURCE / TEST COMMIT = NOT EXECUTED

C2.2 VIRTUAL SERIAL = HUMAN ACCEPTED / CLOSED
C2 REAL HARDWARE = PENDING / NOT VERIFIED
T035-C OVERALL = NOT YET CLOSED
VERIFIED LKGC = UNCHANGED
```
