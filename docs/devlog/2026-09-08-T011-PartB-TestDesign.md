# Devlog 2026-09-08 — T011 Part B Learning / Test Design（LLM Diagnosis Integration，docs-only）

## 今日工作

- **T011 Part B 启动（Phase: Learning / Test Design，docs-only）**；全部定案落于 [T011 档案](../tasks/T011-ai-diagnosis.md)（PB-A ~ PB-R）。核心决策：
  - **Provider 定案**：ModelScope API-Inference + OpenAI-compatible Chat Completions（显式声明：兼容性仅指 HTTP/JSON 协议形状——绝不使用 OpenAI 服务/SDK/Responses API/OPENAI_API_KEY）；不建 multi-provider abstraction。
  - **凭据边界**：`MODELSCOPE_API_KEY` 仅 process env；BYOK 定性入档（桌面端共享 Token 风险的明确声明）；QML 永不接触 Token；生产 endpoint 禁 env override（token+endpoint 双可替换 = 真 token 泄漏路径）；测试 seam 改为 constructor 注入。
  - **Prompt 设计**：structured-facts-only（DiagnosisContext+Report；禁 QML text/文件名/注释/自由文本）、bounded（≤20 detail，确定性非 Success 优先选择 + truncation 语义）、确定性、system authority 指令（do not recalculate/contradict/claim root cause）、plain text 输出 768 tokens。
  - **异步安全**：activeBatchRevision（uint64）stale guard（P0）+ 增/不增触发清单与 Part A atomic invariant 完全一致；batch 变化 abort in-flight + 清旧 AI；失败切换全保留；clearDiagnosis 扩展为"清当前 batch 全部派生诊断"（仍 ≠ clearResults）。
  - **客户端**：ModelScopeDiagnosisClient（App 层、Idle/Requesting、one-shot、timeout/cancel、11 值错误分类 + HTTP mapping、zero retry、stream=false、reasoning_content 忽略）。
  - **测试策略**：localhost QTcpServer fake endpoint（随机 ephemeral 端口 + 显式 fake-test-token——真 token 绝不可能进 test/log 的硬约束）；矩阵 AI-B01~B12 + UI-AI01~AI10 落库。
  - **QtNetwork kit 预检（提前实证）**：headers/CMake package/DLL 存在 + 仓库外 probe configure/compile/link/run 全 PASS + runtime `sslBuild=yes`——Implementation 无环境阻塞。
- 文档同步：PROJECT_STATUS（面板/§2/§7）、BACKLOG（M6/T011 行/路线/变更记录）。

## 验证（docs-only）

- git 状态复核（LKGC `06ef801` / HEAD `ecb60f0`；Part B 未开始）✓；QtNetwork probe 四步 PASS（不入仓库）✓
- `git diff --check` PASS；`src/tests/CMakeLists.txt/scripts` 零修改；docs-only commit；LKGC 保持 `06ef801`。

## 下一步（待用户指令，不自动开始）

- T011 Part B — Implementation（34 步：prompt builder→parser→client→fake server→AI-B→Controller revision/cancel→UI-AI→QML→deploy/TLS/provenance→Manual AI UI Smoke→可选 Live ModelScope Smoke——无真 token 时如实记 NOT RUN）。不开始 T012。
## 追加（同日）· Implementation 完成与 Manual AI UI Smoke PASS

- Implementation（code `85699ff`，含 ISSUE-004 SplitView workspace 修复；弃 f087275）：client/prompt builder/Controller 双 stale guard/QML Flickable+SplitView 面板；ctest 20/20、clean 126 零警告、deploy/provenance/TLS/minimal-PATH 全过。
- 有效取证：runtime geometry 实证 Diagnosis 滚动机制（flickH=133/contentH=214/contentY 可变）→ blocker 重分类为 workspace allocation → SplitView 定案 → 用户 Layout PASS + Manual AI UI Smoke PASS（A~I）。
- **Live ModelScope Smoke = WAITING FOR USER**；T011 Part B/T011/M6 仍 IN PROGRESS；verified LKGC `06ef801`；候选 `85699ff` 待 Live PASS 推进。
