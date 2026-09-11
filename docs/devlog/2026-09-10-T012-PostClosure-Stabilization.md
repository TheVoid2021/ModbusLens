# Devlog 2026-09-10 — T012 Post-Closure Agent Stabilization Review（docs-only）

## 今日工作

- **真实使用回归发现两个新问题，完成 RCA + Test Design（零代码、零真实调用、零 push）**：
  - **ISSUE-008 建档**：合法 0x02 问题最终显示「模型响应格式无效」。InvalidResponse 全部真实产生路径逐一核验（client 3 处结构判定 + runtime 1 处空 content + B21 工具优先语义）；定案可见文案来自 runtime「无 tool_calls 且 content 空/纯空白」路径——链路 fail-closed 正确；**该次 provider 响应为何空 content = unknown**（reasoning 耗尽同一 `max_tokens=768` 输出预算 = hypothesis only，未证明）；finish_reason 运行时从不读取。Test design：B20 扩展纯空白 + 新 AGENT-B24（reasoning-only 不升级为 answer）+ 真实 shape 待 sanitized evidence 后入 fixture。
  - **ISSUE-009 建档**：额度不足时「分析中…约 1 秒→busy 消失→无持久可见错误」。逐层核验 8 类 provider 错误全部会写 agentErrorText 且 QML 可见——**无既证静默路径**；最可能断点为额度不足的 provider 超表响应形态（hypothesis）；顶部「模型服务：ModelScope — 模型：%1」真实语义 = configured（≠ healthy、≠ quota available），推荐改「模型配置：」方案 B；**不建 QuotaExceeded 枚举**（quota/rate-limit 当前无稳定区分证据），采用合并文案；UI-AG21/22 未来测试设计。
  - **Live Diagnostic 判定**：两 Issue 均需「最多 1 次 user-authorized sanitized diagnostic reproduction」（本阶段不执行）。
- **状态**：T012 = REOPENED / STABILIZATION；M6 = IN PROGRESS；T013 = NOT STARTED；ISSUE-008/009 = OPEN；verified LKGC `e922c19` 不回退。

## 关联档案

- [ISSUE-008](../../issues/ISSUE-008-valid-0x02-query-invalid-response.md)；[ISSUE-009](../../issues/ISSUE-009-provider-failure-ux-silent.md)；[T012 任务](../../tasks/T012-agent-tools.md)（Post-Closure Stabilization 段）。


## 追加（Sanitized Live Diagnostic）— Evidence captured ✅

- ISSUE-008：同题唯一 run → **NOT REPRODUCED**（3 rounds/HTTP 200/final content 714 可用/reasoning 与 content 共存/usage 对象存在；producer 仍 unknown ①~④；budget hypothesis 未证实未排除）。
- ISSUE-009：**BLOCKED / PRECONDITION NOT AVAILABLE**（008 成功=额度可用；零额外请求；不伪造不替代）。
- 临时插桩完全 `git checkout` 恢复：src/tests/CMake/QML 相对 HEAD 零 diff。ISSUE-008/009 OPEN；LKGC `e922c19`；未 push。


## 追加（Phase 3）— Offline Defensive Hardening ✅（`95ad9e7`）

- 8 类 provider 文案契约 + 「模型配置：」+ B20/B24/B25 + ag21/ag22；真实 RED=ag05/21/22 文案，其余 coverage-only；ctest 23/23、clean 0 警告、deploy+minimal-PATH。ISSUE-008/009 OPEN（根因均未声称已修）；candidate `95ad9e7` 未推进。


## 追加（Final Closure）— T012 / M6 DONE ✅

- B20 全族覆盖 verify（test-only `3572cf7` 补齐 LF/CRLF）；enum 语义审计 A/B 分层；五条工程结论入 T012 档案（不重试关闭 Issue / evidence vs hardening / fail-closed 原有契约 / fake HTTP 集成回归 / 不伪造 RCA）。
- **verified LKGC：`e922c19` → `3572cf7`**；ISSUE-008/009 = OPEN + MONITORING/NON-BLOCKING；T012 DONE、M6 DONE、T013 NOT STARTED。未 push、零真实请求。
