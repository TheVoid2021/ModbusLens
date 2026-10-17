# R7_09_CAUSAL_ANALYSIS + R7_10_MINIMAL_FIX_PROPOSAL

## 一、运行期证据（R7 trace 原文）

### C35（`R7_06_C35_TRACE.log`）
```
[HARNESS] read-start #1 begin_ok=1 qty=2 wire=8B pending_before=0
[HARNESS] read-start #2 begin_ok=0 qty=2 wire=8B pending_before=0
[HARNESS] read-start rejected: session refused beginActiveRequest
[R7C35] after completeRead: busy=0 reason= terminals=0
[R7C35] after disconnect: busy=0 reason=disconnected terminals=0
WRITEFAIL: WRITEFAIL C35: the reason was overwritten (disconnected)
```

### R16（`R7_07_R16_TRACE.log`）
```
[HARNESS] read-start #1 begin_ok=1 qty=2 wire=8B pending_before=0
[HARNESS] read-start #2 begin_ok=0 qty=2 wire=8B pending_before=0
[HARNESS] read-start rejected: session refused beginActiveRequest
[R7R16] after click: busy=0 readStarts=2 portOpen=1
PRODWRITEFAIL: R16: no pending request to lose
PRODWRITEFAIL: R16: terminals=0, expected one more
```

## 二、根因（**两项同源，CONFIRMED**）

### 共同机制
`HarnessWriteTransport::startActiveRequest`（read 分支，C2.1 起）让**成员 `session_`** 进入 `AwaitingResponse`；
但 **`completeRead()` 与 `completeReadWithTimeout()` 使用「局部 `SerialTransactionSession session`」**完成事务
（`src/main.cpp:5152` / `:5187` 内的 `SerialTransactionSession session;`），**成员 `session_` 从未被完成或复位**。

⇒ 用一次 `completeRead()` 之后，成员 `session_` 永久停在 `AwaitingResponse`；
⇒ **下一次 read 的 `beginActiveRequest` 必然失败**；
⇒ R4 的 FIX-02 诚实返回 `NotSent`（不设 pending、不置 busy）；
⇒ 表现为：
- **C35**：busy 从未 true ⇒ `busy_became_true` 从未写入 ⇒ `disconnectSerial()` 的 `disconnected` 成为唯一 reason ⇒ 断言「reason was overwritten」；
- **R16**：click 被拒 ⇒ `serialBusy()` 为 false ⇒ 「no pending request to lose」＋ 无 pending 可终止 ⇒ 「terminals=0」。

### 为什么 R6 的 `closePort` cancel 不够
`C35`/`R16` 之前的 read 是**以 `completeRead()` 正常完成**的，**不经过 `closePort` / `simulateAdapterRemoval`**，所以 R6 的两个 cancel 点覆盖不到。

### 为何 R1 时未暴露
R1 时 `startActiveRequest` **不检查 begin 结果**，因此「begin 失败」被静默掩盖：pending 仍被设置、busy 仍为 true、`completeReadWithBytes` 事后按**旧 descriptor** 校验（与 K 场景同一缺陷）。
R4 的 FIX-02 把静默掩盖改为诚实拒绝，于是**残留状态以可见失败的形式暴露**。

## 三、分类判定

| 项 | 分类 |
|---|---|
| C35 | **`R16-C` 类（STALE_SESSION_REJECTED_NEW_REQUEST）**——不是 `R16-A/B/D/E` |
| R16 | **同 C35**（同一 `read-start begin_ok=0` 证据） |
| 归属 | **测试 Harness（`HarnessWriteTransport`）**，非产品、非测试断言错误 |
| 断言正确性 | **正确**（`busy_became_true` 优先于 `disconnected`；R16 要求 pending 必须存在才能模拟丢失） |

## 四、最小修复方案（**本轮只设计，不执行**）

### FIX-02c（推荐，2 行 × 2 处）

在 `completeRead()` 与 `completeReadWithTimeout()` 的**完成分支**（`emit transactionCompleted` 之后）补一次成员会话收尾：

```cpp
// completeRead(): after pending_.reset() + emit transactionCompleted{...}
        session_.cancel();          // 局部 session 已完成；成员的 begin 由本次完成收尾

// completeReadWithTimeout(): 同样在 emit 之后
        session_.cancel();
```

**语义论证**：
- `startActiveRequest` 让成员 `session_` begin（C2.1）；`completeRead*` 代表**同一请求**的完成 ⇒ **成员会话在完成后必须回到 Idle**，`cancel()` 正是「无更多数据可喂」的正确收尾（不会发出额外 terminal，不改变 analysis）。
- 与 `completeReadWithBytes`（已用成员 `session_.feedResponseBytes` / `onResponseTimeout`，内部自动 `analyzeAndReset`）语义对齐。
- **不是**通用 cancel+retry：**不 cancel 任何仍在进行中的合法请求**（只在 `completeRead*` 完成分支、且 `pending_` 已清之后）。
- **不改变** `beginOk` 诚实拒绝（无效 descriptor 仍 `NotSent`）。
- **不影响** `closePort` / `simulateAdapterRemoval` 的 cancel。

### 备选（不推荐）
把 `completeRead()` / `completeReadWithTimeout()` 改为使用成员 `session_`（与 `completeReadWithBytes` 完全同构）。**改动更大**（需处理 `session_` 的 timeout 阈值来源），且对 `completeRead()` 的固定 9 字节响应无额外收益。

## 五、修复风险评估

| 风险 | 评估 |
|---|---|
| K 场景（125 寄存器） | **不受影响**（走 `completeReadWithBytes`，用成员 session 的 feed） |
| DLG5b（R6 已修复） | **不受影响**（`closePort` cancel 保留） |
| 真实生产路径 | **不受影响**（仅 `HarnessWriteTransport`，`--qml-*-check` 分支） |
| 重复 terminal | **不会**（`cancel()` 不发 terminal；`emit` 只在完成分支一次） |
| 断言强度 | **不降低**（busy/reason/pending/terminal 断言原样保留） |

## 六、修复后预期

```
C35: read-start #2 begin_ok=1 ⇒ busy=true ⇒ reason=busy_became_true ⇒ disconnect 不覆盖 ⇒ PASS
R16: read-start #2 begin_ok=1 ⇒ busy=true ⇒ pending 存在 ⇒ adapter removal ⇒ terminals=+1 ⇒ PASS
K125: 不变（PASS）
```

## 七、待验证门禁（下一轮）

1. 重建（增量）`modbuslens`；
2. 目标 7 项（read_result / write_foundation / production_write / write_foundation_windows / readiness / register_map / active_profile）；
3. `manual_import_pdf_docx` 单跑（判定 ISSUE-025 是否仍 flaky）；
4. **全量 74 项 ctest 一次**（全新记录，不覆盖 R1/R5/R6）。
