# ISSUE-005: Ask AI 失败显示"操作被取消"——OperationCanceledError 裸漏 + 双 timeout owner 竞态

> 状态：**OPEN**（2026-09-08；修复已编码并全自动化 GREEN，Live 27B 复验待用户）

## Symptom（现象）

T011 Part B Live 环境（Provider: ModelScope / Model: Qwen/Qwen3.5-27B）点击 Ask AI 后出现：

> Latest AI request failed: 操作被取消

与之前的 `malformed response` 是**完全不同的问题**（397B non-thinking 的兼容性问题与本 issue 不混合）。

## Impact（影响）

- Live Ask AI 不可用（真实 27B 请求被本地取消，用户只见 Qt 底层本地化错误串）。
- 错误文案无业务语义：用户无法区分 Timeout / 用户取消 / 意外取消。

## Repro（复现步骤）

真实环境：`MODELSCOPE_API_KEY` + 候选 27B → Run Demo → Baseline → Ask AI → 约 30s 后（推断）出现"操作被取消"。

## 定位过程（Root Cause，代码级定性）

1. **"操作被取消" = `QNetworkReply::OperationCanceledError` 的 Qt 本地化 errorString**。当前 `handleFinished` 的 transport-error 分支把 `reply->errorString()` 原样 emit 成用户文案——classification 的 code 映射其实是正确的（mapReplyError 把 OperationCanceled→Timeout），但 **UI 呈现的是错误字符串本体**，泄漏了底层 transport 语义。同时证明**请求确实被本地 abort 了**（不是 ModelScope 业务层错误）。
2. **双 timeout owner 竞态**：实现同时存在 `QNetworkRequest::setTransferTimeout(30s)`（Qt native transfer timeout，内部 abort）与 `QTimer(30s)`（应用层 timer，也 abort）。两者都会产生 `OperationCanceledError`：native 路径先到则 QTimer 未置态、finished 回调无从区分"谁取消的"——**原因归属缺失**（QNetworkReply::error() 本身不含 abort 主体信息）。
3. **30s 对 27B 非流式推理偏短**：Live evidence（本地 abort 出现在用户交互感知内）提示 27B 推理在负载下可能超过 30s。用户规则：不在无证据时拍数字；本 issue 的唯一数字依据 = 本地 abort 确已发生（用户实测外显"取消"）→ 采用用户给定候选 90s。

## 解决方案（已编码，待 Live 复验后提交）

- **Abort Reason Ownership**：`enum class AiAbortReason { None, UserCancel, Timeout, BatchInvalidated, DiagnosisCleared, SupersededRequest }`；每次主动 `reply->abort()` 之前必须先记录 reason（cancel→UserCancel；timeout→Timeout；batch 变化→BatchInvalidated；clearDiagnosis→DiagnosisCleared）。Controller 三个取消调用点各传对应 reason。
- **单一 timeout owner**：移除 `setTransferTimeout`（消除 native/QTimer 竞态）；QTimer 为唯一拥有者——fire 时先置 `abortReason_=Timeout` 再 abort，并立即以业务文案 fail（"AI request timed out"）；其后的 finished() 因 busy=false 静默。
- **OperationCanceledError 分类**：Timeout reason→`AiDiagnosisErrorCode::Timeout`；其他内部 reason→静默（UserCancel/BatchInvalidated/DiagnosisCleared/SupersededRequest 均不 emit）；`None`→unexpected cancellation→`NetworkError`（"AI request was cancelled unexpectedly"）。**任何路径不再把 Qt errorString 当用户文案**（transport 失败固定 "network error"；provider 错误 sanitized body）。
- **production timeout 30s → 90s**；测试仍注入 50~100ms 快速覆盖。
- 未修改 model ID / messages / prompt / parser / reasoning_content / max_tokens（与取消无关）。

## Verification（自动化，已全绿）

```text
clean build：126 targets，warning/error 0
full ctest：20/20（AI-B12 新增 message 断言 = "AI request timed out"（非底层串）；
           AI-B11 cancel silent、AI-B13/UI-AI08/UI-AI11 stale guards 不回归）
qml smoke exit=0；deploy exit=0；Qt6Network provenance 一致；minimal-PATH exit=0
```

**Live 27B Smoke = WAITING FOR USER**（真实 Ask AI 复验；通过后才提交 fix 与推进）。

## Learning（教训）

- **错误分类 ≠ 错误呈现**：code mapping 正确但 errorString 泄漏会让用户看见 transport 底层语义——业务文案必须与底层字符串彻底解耦。
- **多个 timeout 机制并存即竞态**：native transfer timeout 与 app timer 都会 abort，finished 回调只能靠"事先声明归属"来分类。
- **abort 主体信息在 QNetworkReply::error() 中不存在**：必须以自有状态补位（reason recorded BEFORE abort）。

## Related

- 前置：T011 Part B 已完成（§归档），本 issue 为完成后的 Live 真实使用发现
- 修复 files：`src/ui/ai/ModelScopeDiagnosisClient.{h,cpp}`、`src/ui/AnalysisController.cpp`、`tests/test_ai_client.cpp`
- 状态：OPEN / Fix Candidate Awaiting Live Verification；提交与 LKGC 待 Live PASS 后按规则处理
## RESOLVED — Live 27B Regression Smoke PASS（2026-09-08）

用户使用修复后的最新 deploy 完成真实 ModelScope 复验：

- Provider：ModelScope API-Inference；Model：Qwen/Qwen3.5-27B（known-good baseline，未换模型）。
- Run Demo → Run Baseline Diagnosis → Ask AI → **成功返回并显示真实 AI explanation**。
- 未再出现"操作被取消"，也未出现 "AI request timed out"。

**最终根因（结论）**：ModelScope 请求本身并非必然失败。旧实现同时存在 `QNetworkRequest transfer timeout` 与 `QTimer` 两个 timeout owner，请求被本地 abort 后 `QNetworkReply` 返回 `OperationCanceledError`，旧 presentation 又直接使用 `reply->errorString()`——UI 暴露 Qt transport 层字符串，无法表达真实业务原因。

**最终修复**：单一 QTimer timeout owner（production 90s）+ abort 前显式记录 `AiAbortReason` + Timeout 映射业务 Timeout + UserCancel/stale/clear/superseded 静默 + 不再把 Qt errorString 当用户文案。

### Evidence limitation（诚实保留）

旧失败请求的**精确 elapsedMs 未实测**（WIN32 GUI 无输出通道）。本档案不改写为"已实测旧请求恰在 30.000s timeout"——仅保留诚实描述：旧实现存在 30s 双 timeout owner、真实 UI 出现 OperationCanceledError、代码级分析确认本地 abort/cancel 分类与呈现缺陷；修复后 90s 单一 owner + AbortReason 并经真实 27B Live request PASS。

### Non-blocking presentation observation

真实 AI 文本出现 `**Suggested checks**`——因 UI 强制 `Text.PlainText`，Markdown 星号按字面显示。非安全/功能 bug；记录为 Final Integration / prompt polish candidate。**不启用 RichText/Markdown/HTML 渲染**，PlainText 安全边界保持。

### 自动化证据（按最终实际结果重跑核验）

clean build 126 targets 0 warnings/0 errors；ctest 20/20（AI-B11 cancel silent、AI-B12 timeout→"AI request timed out"、AI-B13 identity、UI-AI08/UI-AI11 guards 全过）；qml smoke exit=0；deploy exit=0；Qt6Network provenance 一致；minimal-PATH exit=0。

**Live ModelScope Regression Smoke = PASS**（真实 explanation 显示；未记录 MODELSCOPE_API_KEY、完整 prompt / Authorization header）。

**状态：RESOLVED ✅**（fix commit 见 git log；docs-only 归档不推进 LKGC）。
