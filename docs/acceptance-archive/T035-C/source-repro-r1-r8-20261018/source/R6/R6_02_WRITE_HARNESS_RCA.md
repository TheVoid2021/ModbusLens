# R6_02 / R6_03 — Write Harness RCA + 事务状态真值表

## R6_02 — 三项 write 测试回归的机制

### 失败断言（R5 原始，未改动）

| 测试 | 断言 |
|---|---|
| `qml_write_foundation_check`(#58) | `WRITEFAIL C35: the reason was overwritten (disconnected)` · `WRITEFAIL DLG5b: the read did not make busy true` · `WRITEFAIL DLG5b: the reason is not BusyBecameTrue` |
| `qml_production_write_check`(#59) | 同上 + `DLG5b: the dialog is still visible after the busy invalidation` · `PRODWRITEFAIL R16: no pending request to lose` · `R16: terminals=0, expected one more` |
| `qml_write_foundation_check_windows`(#71) | 同 #58 |

**注意**：`DLG5b` 断言的是 **read**（「the **read** did not make busy true」）⇒ 落在 `HarnessWriteTransport::startActiveRequest` 的 **READ 分支**（R4 FIX-02 改动处）。

### 全量 `pending_.reset()` 清点（9 处）与 session 归属

| reset 位置 | 所在函数 | 是否触碰成员 `session_` |
|---|---|---|
| :5171 | `completeRead()` | **否**（用**局部** `SerialTransactionSession session`） |
| :5203 | `completeReadWithTimeout()` | **否**（局部 session） |
| :5272 | `completeReadWithBytes()` | 是（`session_.feedResponseBytes` / `onResponseTimeout` ⇒ 内部 `analyzeAndReset` **自动复位**） |
| :5339 | `completeWriteWithTimeout()` | **否**（局部 session） |
| :5372 | `completeWriteWithTimeout16()` | **否**（局部 session） |
| :5405 | `completeWriteWithEcho()` | **否**（局部 session） |
| :5450 | `completeWriteWithEcho16()` | **否**（局部 session） |
| :5299 | `simulateAdapterRemoval()` | **是（R4 FIX-02 已加 `session_.cancel()`）** ✅ |
| **:5463** | **`closePort()`** | **❌ 未复位（当时只有 `portOpen_=false; pending_.reset();`）** |

### 机制（CONFIRMED）

```
DLG5b 场景：
  read 发起（session_ 进入 AwaitingResponse）
    ↓
  断开连接 → closePort(): portOpen_=false; pending_.reset();   ← session_ 仍是 AwaitingResponse
    ↓
  再次 read → session_.beginActiveRequest(...) 返回 error
    ↓
  R4 FIX-02: beginOk==false ⇒ return NotSent（不设 pending）
    ↓
  AnalysisController 不置 serialBusy_ ⇒ "the read did not make busy true"
  reason 序列被 NotSent 路径覆盖 ⇒ "the reason was overwritten (disconnected)"
  无 pending 可丢 ⇒ "R16: no pending request to lose / terminals=0"
```

**⇒ 根因 = `closePort()` 未复位 `session_`**（**显式终止路径遗漏**），**FIX-02 的诚实拒绝只是把隐藏的残留暴露为可见失败**。

**⇒ 修复（R6，最小且语义正确）**：`closePort()` 增 `session_.cancel()`（**断开连接 = 所有未完成事务终止**）。
**未采用**「通用 cancel + retry」：那会掩盖真实的无效 descriptor，且可能取消合法 active request（§3.1 禁止）。

### 证据等级

`CONFIRMED`（源码级清点：7 处用局部 session / 1 处已修复 / **1 处 `closePort` 遗漏**；与三项失败断言的 `disconnected` 语义完全吻合）

---

## R6_03 — 事务状态真值表（依据实际 Harness 契约，非为迁就旧断言）

| CASE | Session 状态 | Pending | 新请求 | Busy | Reason | 允许 cancel | 允许重新 begin | Terminal 次数 |
|---|---|---|---|---|---|---|---|---|
| **A** IDLE + 合法请求 | Idle → AwaitingResponse | 建立 | **接受** | true | 无 | — | — | 0（等待完成） |
| **B** ACTIVE + 新请求 | AwaitingResponse | 已有 | **拒绝（NotSent）** | 保持 true | 保持 | 否 | 否 | 0 |
| **C** 已终止（adapter removal / closePort） | **必须回到 Idle** | **清空** | **接受** | 由新请求决定 | terminal 设置后允许后续覆盖 | **是（终止时）** | **是** | **1** |
| **D** 无效 descriptor | 任意 | 不建立 | **拒绝（NotSent）** | false | 无 | 否 | 否 | 0 |
| **E** Terminal 清理不完整（本次缺陷） | **残留 AwaitingResponse** | 已清 | **begin 失败** | false（错误） | 被 NotSent 覆盖 | 应在终止时 cancel | 否 | 0（丢失） |
| **F** 完成/超时/取消 | `analyzeAndReset` → Idle | 清空 | 接受 | false | 由 analysis 决定 | — | — | 1 |

**⇒ 本次修复把 CASE E 归正为 CASE C**（终止路径同步复位 session），**未改变 A/B/D/F**。
