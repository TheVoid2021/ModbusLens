# T034 — Configurable LLM Provider / BYOK

- **Goal**: 把 ModelScope-specific AI integration 演进为 small configurable
  OpenAI-compatible provider layer，供桌面用户 BYOK；解除真实
  candidate-extraction quality 验收对 ModelScope 额度的依赖。
- **Status**: PHASE A（LEARNING / DESIGN ONLY）= 本文件。未获 Human 批准前
  不得进入 implementation。
- **Background**: T033 已 COMPLETE / HUMAN ACCEPTED——真实说明书 existing-cache
  → canonical text → candidate pipeline 闭环（retest #2 观察到 `no_candidate`
  与 `provider_rejected_status`，属 seam 之后的独立层）。ModelScope 当前额度
  耗尽，无法继续真实 provider 验收；非 T033 defect。T032-C 保持 PAUSED。

## Phase A — Provider Coupling Audit（2026-10-08 实测）

### A.1 现有四条 AI 调用链

| # | 链路 | 调用栈 | HTTP 模型 | 请求体 | 响应消费 |
|---|---|---|---|---|---|
| 1 | AI 诊断 | `AnalysisController` → `ModelScopeDiagnosisClient` | **async**（signals，单 reply，单 QTimer owner） | `{model, messages, stream:false, max_tokens:768}`（Qt JSON） | `choices[0].message.content` 文本；`reasoning_content` 不消费 |
| 2 | Agent | `AgentRuntime` → `ModelScopeAgentClient` | **async**（round 制，generation guard） | `{model, messages(含 tool_calls/tool), tools, stream:false, max_tokens:768}` | 整个 `choices[0].message` object 交 runtime 解析 tool_calls |
| 3 | Manual Q&A | `ManualQaController` → `ModelScopeManualQaRunner` → `ModelScopeAgentClient` | async（复用 #2 客户端） | #2 + **`chat_template_kwargs.enable_thinking=false`**（Qwen/ModelScope 特有） | 严格 fail-closed parser → `ManualQaParsedResult` + citation id 回解 |
| 4 | Candidate 提取 | `CandidateExtractionController` → `ModelScopeCandidateRunner` → `ModelScopeCandidateAdapter` → `ModelScopeExtractionTransport` → `IModelScopeHttpClient` | **同步 seam**（90s bounded event loop） | 手写 JSON `{model, messages, stream:false, max_tokens}`（string body） | 2xx → `choices[0].message.content` → 严格 JSON 递归下降 parser |

### A.2 配置与凭据现状

- Endpoint：**编译常量** `https://api-inference.modelscope.cn/v1/chat/completions`
  （#1/#2 直接编译；#4 经 `ModelScopeCandidateRunner::officialEndpoint()`）。
  设计理由（DiagnosisClient.h 注释）：禁止 env endpoint + env token 组合导致
  token 外送到任意主机。
- 凭据：进程环境 `MODELSCOPE_API_KEY`（fail-closed：缺失 ⇒ begin()=false，
  零 dispatch）；model 覆盖 `MODBUSLENS_MODELSCOPE_MODEL`。
- `main.cpp:21002-21014`：自动化门禁下 unset 两个 env（gate 无凭据前提）。
- **`QSettings` 在 `src/` 零命中**；无 settings UI、无持久化 provider 配置、
  无 Windows Credential Manager 使用。ProfileStore 的持久化先例 = Qt JSON
  + 原子写（QSaveFile 模式）。
- 所有链共享安全契约：单 QTimer timeout owner（ISSUE-005 单一 owner 教训）、
  same-origin redirect policy（token 不跨主机）、TLS peer verification ON、
  token 不落日志/不进 body/不进错误消息。

### A.3 ModelScope-specific vs OpenAI-compatible

**真正 ModelScope-specific（很少）：**
1. endpoint 常量 `api-inference.modelscope.cn`；
2. canonical 默认 model id（Qwen 系列）+ env 覆盖名；
3. `chat_template_kwargs.enable_thinking=false`（#3 Qwen 模板特有；#1/#2/#4 未用）；
4. `reasoning_content` 字段的存在认知（各链一律"携带但不消费"，语义同
   OpenAI 兼容网关的 reasoning 字段处理）。

**实为 OpenAI-compatible Chat Completions 通用逻辑（可复用主体）：**
- wire 契约：`POST {endpoint}/chat/completions`、Bearer auth、
  `application/json`、`{model, messages, stream:false, max_tokens}`；
- 响应信封：`choices[0].message(.content)`；
- 错误分类骨架（`AiDiagnosisErrorCode`：401→Unauthorized、429→RateLimited、
  5xx→ServerError、transport→NetworkError/Timeout）；
- 超时/取消/generation guard/stale drop 的全部生命周期代码；
- 测试 seam：`IModelScopeHttpClient`（同步 capture）与
  `FakeChatCompletionsServer`（localhost fake，async capture/scripted）。

### A.4 Phase A Learning Gate（A–G）

- **A 解决什么问题**：真实手册已能进入 provider 阶段，但 ModelScope 额度耗尽；
  桌面用户需 BYOK 到任意 OpenAI-compatible provider 才能完成真实
  candidate-extraction quality 验收与日常使用。
- **B 当前代码如何工作**：见 A.1–A.3。四条链各自持有 HTTP 客户端/传输层，
  wire 契约相同但实现分叉（async Qt JSON ×3 + 同步 string JSON ×1）。
- **C 新增知识**：OpenAI-compatible Chat Completions 是事实标准
  （ModelScope/DashScope-compatible、SiliconFlow、DeepSeek、OpenRouter、
  本地 llama-server/vLLM/Ollama OpenAI 模式均兼容该信封）；差异点集中在
  endpoint、model id 与少量 provider extras；secret 管理 = OS 级凭据库
  （Windows Credential Manager，CredWrite/CredRead，`wincred.h`）。
- **D 最少概念**：provider profile（endpoint/auth/model/extras）、
  credential vault（local machine scope）、chat_template_kwargs 类
  provider extras 的透传边界、Test Connection 的最小探针（1 token 请求）。
- **E 方案设计**：见 §Phase A Design（下）。
- **F V1 行为风险**：endpoint 可配置 = key 外送风险（须 profile 白名单）；
  同步 seam（#4）与 async（#1–#3）并存，抽象层不得破坏既有 guard 语义；
  env 凭据仍是 CI/自动化通道，不能移除。
- **G 测试**：见 Phase A 报告 test plan（fake server 扩展 + 每链
  begin-with-each-provider 矩阵 + fail-closed 矩阵）。

## Phase A Design Proposal（待 Human 批准，未实现）

### D.1 最小架构（非框架）

```text
enum class AiProviderKind { ModelScope, DeepSeek, SiliconFlow, OpenAiCompatibleCustom(后期) };
struct AiProviderProfile {        // 编译期只读 profile 表
    AiProviderKind kind;
    QString displayName;
    QUrl chatCompletionsEndpoint; // 固定官方 endpoint，非用户可编辑
    bool usesEnableThinkingKwarg; // ModelScope/Qwen 模板特有
};
struct AiProviderConfig {         // 用户可配置（normal settings）
    AiProviderKind kind;
    QString modelId;              // 用户可编辑
};
// secret：不入 QSettings/config 文件；Windows Credential Manager
//（vault 名 "ModbusLens AI Key / <kind>"）；缺省回落进程 env（CI 兼容）。
// QML 只拿 hasCredential()/maskedHint，永不接触明文。
class OpenAiCompatibleClient { … }   // 从 DiagnosisClient/AgentClient 抽公共体
```

- 调用链改动 = 把各 Runner/Client 的"取 env + 编译 endpoint"替换为
  `resolveProviderConfig(kind)`；wire 构造、parser、guard 全部保留。
- **不做**：DLL 插件、ProviderRegistry、MCP 框架、arbitrary endpoint v1。

### D.2 v1 provider 名单（固定官方 endpoint）

1. ModelScope（现有，保持兼容）；
2. DeepSeek（`https://api.deepseek.com/v1/chat/completions`，OpenAI-compatible）；
3. SiliconFlow（`https://api.siliconflow.cn/v1/chat/completions`，承载多 model，
   便于真实手册验收）——第 3 个仅在实现成本低时保留，否则 v1 砍掉。
Custom OpenAI-Compatible（任意 endpoint）= **不在 v1**（endpoint/key 外送风险
需单独设计 + 风险提示 + credential isolation）。

### D.3 Secret storage（Windows first）

- Windows Credential Manager（`wincred.h` CredWrite/CredRead，
  CRED_PERSIST_LOCAL_MACHINE）——评估结论：成本可控（无新依赖、Win32 API 直调）。
- 读取顺序：Credential Manager → 进程 env（MODELSCOPE_API_KEY 及 per-provider
  env）→ fail-closed。
- 禁止：Git-tracked config、明文 JSON、QSettings 存 secret。
- staged plan（若实现超支）：Phase 1 = env only + Settings UI 只选 provider/model；
  Phase 2 = Credential Manager；**不默认退化成明文持久化**。

### D.4 Endpoint security model

- v1 = known provider profiles 固定官方 endpoint（编译常量表）；
- Authorization 只附 profile endpoint；same-origin redirect、TLS ON 继承；
- Custom endpoint = 后期单独 ADR（风险提示 + 隔离），v1 拒绝。

### D.5 Configuration UX

- Settings → AI Provider（新页或 Diagnosis/DeviceProfile 内既有 AI 区扩展）：
  Provider（下拉，profile 表）· Model（文本，含 placeholder 默认）·
  API Key（password field，写入 vault；显示 masked hint）· Test Connection
  （最小 1-token 请求，显示 sanitized 结果）· 未配置 = AI 区显示未配置态，
  全部确定性 Modbus 功能不受影响（冻结纪律 #11）。

### D.6 Migration / backward compatibility

- env `MODELSCOPE_API_KEY` / `MODBUSLENS_MODELSCOPE_MODEL` 继续有效（无 vault
  时回落 env）→ 现有自动化/CI/gate 零破坏；
- 无 provider 配置时行为 = 现状 fail-closed（`not_configured`），UI 文案不变；
- 不迁移、不删除旧 env 语义；settings 持久化只存 kind+model（非 secret）。

### D.7 Test plan（Phase B 起）

1. fake server（既有 `FakeChatCompletionsServer` + `IModelScopeHttpClient`
   capture）扩展 per-provider endpoint/auth 断言；
2. 每链 × 每 provider 的 begin/dispatch/fail-closed 矩阵；
3. Test Connection 探针的 deterministic tests（fake 200/401/429/超时）；
4. vault 读写用可注入 seam（测试不碰真实 Credential Manager）；
5. 既有 70 targets 全量回归 + `git diff --check`；QML settings 页 smoke。

### D.8 Real-manual acceptance plan

真实已导入的《模拟量输入系列使用手册(RS485版).pdf》（existing cache，不重导入）：
AI 提取候选 → chosen provider → 真实响应 → parser → UI candidate list；
随后独立评估 extraction quality（schema 支持度 / no_candidate 真因 /
parser- validator 严苛度 / prompt 对工业手册表达覆盖）——不得硬编码该手册。

### D.9 预期改动文件（Phase B 起）

新增：`src/ui/ai/AiProviderProfile.{h,cpp}`（profile 表 + resolve）、
`src/ui/ai/OpenAiCompatibleClient.{h,cpp}`（抽公共 async 客户端）、
`src/ui/settings/AiProviderSettingsController.{h,cpp}`、Settings QML 页；
修改：四条链的 runner/client 配置装配点、`CMakeLists.txt`、`main.cpp`
（env 清理 gate 语义核对）；
测试：per-provider 矩阵 + vault seam + Test Connection。

### D.10 Risks / tradeoffs

- 多一个配置面 = 新 fail-closed 路径与 UI 文案；同步/异步双 seam 共存是
  既有债务，v1 不合并（合并 = 契约变更，需单独评审）；
- `enable_thinking` extras 若对非 Qwen provider 发送会被部分网关拒绝 ⇒
  profile 开关控制；
- Credential Manager 为 Windows-only——非 Windows 平台 staged（v1 = env 回落）；
- provider 额度/兼容性差异可能让 quality 验收结论绑定具体 provider——
  验收记录须注明 provider/model。

## Verification

Phase A = docs-only（本文件 + PROJECT_STATUS/BACKLOG 登记 + devlog）。
未改 src/tests/CMake；未构建；未 push；T032-C 保持 PAUSED。

## Human Review（T034-A）= CONDITIONAL PASS / DESIGN CORRECTIONS REQUIRED（2026-10-08 晚）

四点设计修正（已按 Human 原文采纳，B1 实现遵循）：

1. **Windows Credential Manager wording**：`CRED_TYPE_GENERIC` + `CredWriteW/CredReadW`；
   `CRED_PERSIST_LOCAL_MACHINE` = 当前用户的凭据在同一机器的后续登录会话中持久，
   **不是**跨 Windows 用户共享的机器级 secret；`wincred.h` = Windows SDK，
   链接 `Advapi32.lib` ⇒ 无第三方依赖，**但有 Windows 系统库链接依赖**。
2. **QML secret boundary（删除"QML 永不接触明文"绝对表述）**：可执行边界 =
   stored credential plaintext is NEVER returned from C++/vault to QML；QML 可
   瞬时持有新输入的 key → 立即交 C++ `saveCredential()` → 成功/失败处理后立即
   清空输入框；不持久化到 QSettings/JSON、不写日志、不进截图/诊断/devlog、
   无返回 stored plaintext 的 getter。vault 在 B2；B1 不做 Settings UI。
3. **Provider-specific credential isolation**：严禁共享 secret 跨 provider。
   B1：ModelScope → `MODELSCOPE_API_KEY`（仅 provider==ModelScope 时读，
   向后兼容）；DeepSeek → `DEEPSEEK_API_KEY`（仅 provider==DeepSeek 时读）。
   禁止任何交叉读取；vault TargetName 将来也按 provider namespaced。
4. **v1 provider scope**：B1 只实现 ModelScope + DeepSeek；SiliconFlow
   DEFER TO LATER PROVIDER EXPANSION（目标 = 最小改动解除真实说明书测试对
   ModelScope 额度的依赖）。

## B1 — PROVIDER PROFILE + CANDIDATE CHAIN（IMPLEMENTED / AUTOMATED PASS / PENDING HUMAN LIVE TEST）

**scope（严格按 Human 授权）**：Provider Profile + Candidate extraction 链 +
ModelScope 向后兼容 + DeepSeek 支持 + deterministic fake tests。不做：
Settings UI、Credential Manager、Diagnosis/Manual QA/Agent 三链迁移、
SiliconFlow、Custom endpoint、plugin/registry 框架、MCP 改动。

### B1.1 DeepSeek verified profile（官方当前 contract）

- Endpoint：`https://api.deepseek.com/chat/completions`（编译常量，base
  `https://api.deepseek.com`）；Authorization = `Bearer <DEEPSEEK_API_KEY>`；
  OpenAI-compatible Chat Completions shape。
- **Candidate 请求显式关闭 thinking**：请求体加
  `"thinking": {"type": "disabled"}`（DeepSeek thinking mode 默认开启；
  Candidate structured extraction 只消费最终 content，不让 reasoning_content
  参与 parser）。
- Model = 用户可配置字符串（`MODBUSLENS_AI_MODEL`）；不宣称任何 model 永久
  可用；不自动调 models API；不猜 fallback model；缺失 → fail-closed。

### B1.2 实现（provider-neutral rename 仅限 Candidate 链）

- 新增 `src/ui/ai/AiProviderProfile.{h,cpp}`（Qt-free）：`enum class
  AiProviderKind { ModelScope, DeepSeek }` + 编译期 profile 表（endpoint 固定
  官方 + `sendThinkingDisabledExtra` 由 profile 决定）+ env 常量 + token 解析
  （"modelscope"/"deepseek"；unknown → nullopt ⇒ fail-closed）。
- rename（git mv + 全量引用更新，零扩散出 Candidate 链）：
  `IModelScopeHttpClient → IChatCompletionsHttpClient`、
  `ModelScopeHttpRequest/Result → ChatCompletionsHttpRequest/Result`、
  `QtModelScopeHttpClient → QtChatCompletionsHttpClient`
  （文件 → `ChatCompletionsHttpClient.{h,cpp}`）；
  `ModelScopeExtractionTransport → ChatCompletionsTransport`（文件
  `ChatCompletionsTransport.{h,cpp}`）；`ModelScopeCandidateRunner →
  CandidateProviderRunner`；`ModelScopeCandidateAdapter →
  CandidateProviderAdapter`；`ModelScopeAdapterConfig → CandidateAdapterConfig`
  （增 `kind` 字段）；`extractModelScopeResponseContent →
  extractChatCompletionsResponseContent`。
- `CandidateProviderRunner`：provider 解析（env `MODBUSLENS_AI_PROVIDER`，
  空/缺省 = ModelScope 向后兼容；unknown = fail-closed 零 dispatch）；
  secret = **仅** profile 指定变量（结构上不可能交叉读取）；model precedence =
  `MODBUSLENS_AI_MODEL` →（仅 ModelScope）`MODBUSLENS_MODELSCOPE_MODEL` →
  （仅 ModelScope）canonical default `Qwen/Qwen3.5-27B`；DeepSeek 无 model →
  fail-closed。遗留 statics（`hasConfiguredCredential`/`configuredModelId`/
  `officialEndpoint`/`productionTimeoutMs`）保留 = ModelScope 语义（P-matrix
  不回归）。
- `CandidateProviderAdapter`：wire body 由手写 string JSON 改为
  **QJsonDocument/QJsonObject** 构造（运行时 model/prompt 的 escaping 权威化
  —— 消除 quotes/backslash/newline/unicode 依赖手写转义的风险）；extras 仅由
  profile 决定（DeepSeek 加 thinking disabled；ModelScope 零 extras = 验证过
  的 wire 语义不变）。响应侧自写严格 JSON reader（H8 fail-closed 冻结资产）
  **原样保留**。
- `main.cpp` harness gate：`--qml-*` 下新增 unset `DEEPSEEK_API_KEY` /
  `MODBUSLENS_AI_PROVIDER` / `MODBUSLENS_AI_MODEL`（与既有凭据 unset 同块）。
- Endpoint security：无任何 `*_BASE_URL`/endpoint env 覆盖；test-only 注入 =
  既有 constructor/fake seam。

### B1.3 Tests（TEST FIRST — REAL RED ×5 → GREEN）

新增 `tests/test_candidate_provider_matrix.cpp` + target
`modbuslens_candidate_provider_tests`（CTest `candidate_provider_matrix`，对
pre-B1 API 以 env 输入驱动 → 行为级 RED，非编译失败）：

- **REAL RED（pre-implementation，exact assertions）**：
  - B `b_deepSeekReachesFakeServer`：`'started' returned FALSE`（provider env
    被忽略、DEEPSEEK key 未读）；
  - C `c_deepSeekBodyHasNoChatTemplateKwargs`：`begin(...) returned FALSE`；
  - E `e_deepSeekIgnoresModelScopeKey`：`'!started' returned FALSE`（错误读取
    MODELSCOPE key）；
  - G `g_unknownProviderFailsClosed`：`'!started' returned FALSE`（未知 token
    被忽略）；
  - H `h_jsonEscapingRoundTrip`：`begin(...) returned FALSE`。
  Totals: **5 passed, 5 failed**（A/D/F 控制组双态 GREEN）。
- **GREEN（post-implementation）**：provider matrix **10/10**（A–H 全过：
  DeepSeek 到 fake server、Bearer=fake-deepseek-key、endpoint
  api.deepseek.com、model 正确、stream=false、thinking.type=disabled、
  无 chat_template_kwargs、跨 provider key 隔离、missing key 零 dispatch、
  unknown provider 零 dispatch、含 quotes/backslash/newline/unicode 的
  model+prompt 全文往返）。
- 零回归：candidate_transport **22/22**、candidate_adapter **20/20**、
  candidate_orchestration **25/25**。
- Full Release CTest：`ctest -N` = **71 targets**（+candidate_provider_matrix）
  → **71/71 PASS / 0 failed / exit 0 / 1043.86 s**。
- **deployment_startup_check（用 B1 production exe 重建 candidate 后重跑）**
  → Passed 212.87 s；candidate exe SHA-256 = production exe（见下）。

### B1.4 Production build（吸取 T033 stale-binary RCA）

- production target 显式重建：`cmake --build build/release --target modbuslens`。
- production exe = `E:/desktop/ModbusLens/build/release/modbuslens.exe`，
  **7,028,128 B / SHA-256
  `9f84579a949344941f9c1833a8ae4f231b79623b25ee626747ff88bc360d51f1` /
  LastWrite 2026-10-08 21:54:08**（源文件 mtime 20:11–20:15 < 21:54 ⇒ exe 晚于
  全部源修改）；candidate 树由 deployment gate 重建后 exe 同 SHA
  （`9f84579a…d51f1`，21:57:51）。
- **构建环境 RCA（如实记录，非产品代码问题）**：本轮多次 build 失败 =
  ①`qmltyperegistrar` 对**已存在**的 `modbuslens.qmltypes` merge 时静默
  exit 1（删除该 build 产物后重跑即成功）；②qmlcachegen 间歇性
  `拒绝访问`（失败点逐轮漂移 = AV/扫描器间歇拦截，重试循环消耗通过）；
  ③Qt License Service `qtlicd 3.6.4` 版本不匹配告警（`--register` 修复注册，
  AutoMoc 警告但不阻断）。教训：**构建失败必须定位到具体 FAILED edge 再继续
  ；CTest PASS ≠ production exe 最新。**

### B1.5 边界与不变项

- 未 commit（等待 Human live test + review）；`git add .`/`-A` 未用。
- T032 interview 07–10、`_ctx.py`/`_dump.py`、T032-C：未触碰 / PAUSED。
- Diagnosis / Agent / Manual QA 三链：零迁移（仍 ModelScope 专用配置装配）。
- 不加 OCR/不重做 UX/不动 release 资产；verified LKGC 不适用（B1 未 commit）。
- **Human Live Test 指引**：
  ```text
  set MODBUSLENS_AI_PROVIDER=deepseek
  set DEEPSEEK_API_KEY=<Human 自己的 key，勿发给 WorkBuddy/日志/截图>
  set MODBUSLENS_AI_MODEL=<Human 选定的 DeepSeek model>
  E:/desktop/ModbusLens/build/release/modbuslens.exe
  → 选中原有已导入的《模拟量输入系列使用手册(RS485版).pdf》（不重导入）
  → AI 提取候选
  ```
  Gate 1 = provider transport works；Gate 2 = candidate extraction quality
  （独立评估）。`provider accepted + no_candidate` ⇒ B1 integration PASS、
  quality 另开 issue——不把 no_candidate 算作 provider failure。

## B1 Live RCA #1 — `malformed_response`（PENDING EVIDENCE / FORENSICS ADDED，2026-10-08 深夜）

Human live test（DeepSeek + 原有真实 PDF cache）结果 = `malformed_response`。
**未改 parser、未放宽 schema、未 commit。**

### 静态归因（token 映射，决定性初步结论）

`providerExtractionFailureToken`（`src/core/candidate/ProviderExtractionContract.cpp`）：

```text
transport_error          = TransportError          （请求未产生响应）
provider_rejected_status = ProviderRejectedStatus  （非 2xx）
malformed_response       = MalformedResponse       ← Human 观察
schema_violation         = SchemaViolation         ← strict parser 拒绝专用
```

⇒ **`malformed_response` 只能由 envelope/content-extraction 层产生**
（`extractChatCompletionsResponseContent` = ok=false：choices 缺失/非 array、
message 缺失、content 非 string 或**空字符串**），**strict structured parser
未到达**。初步 owner = **A 侧（Chat Completions envelope）**；已知高概率
触发候选 = assistant `content` 为空串（契约要求 non-empty string）或
`finish_reason=length` 截断。**最终归因待 sanitized 证据确认。**

### 最小安全取证路径（已实现，本阶段唯一代码改动）

`CandidateProviderAdapter.cpp` 新增 sanitized forensics（`writeProviderForensics`
+ `describeContentStructure`，零 parser/parser 契约改动，零返回语义变化）：

- 每次提取在三个阶段边界（transport 失败 / envelope 失败 / parser 完成）输出：
  `transport_ok/status_ok/response_body_len`（SESSION N seam 刻意不暴露数值
  HTTP status，如实记录）→ `envelope_json_parse` → `choices_count` →
  `message_exists` → `content_type/content_len` → `reasoning_content_exists/len`
  → `finish_reason` → `envelope_content_extraction` → `content_outer_nonempty`
  → 结构分析（`structured_json_parse` / `top_level` / `proposals_size` /
  逐 item 首个异常类别：`invalid_json`/`wrong_top_level_type`/
  `missing_required_field`/`wrong_field_type`/`unknown_field_count`/
  `unknown_target`/`empty_evidence_or_value`）→ `parser_result` token。
- **禁止项全部满足**：无 API key / Authorization header / manual text /
  prompt / 任何 raw provider 或 assistant 正文。
- 渠道：`qWarning` + 追加文件 `%TEMP%\modbuslens_provider_forensics.log`。
- 构建环境同前（AV 间歇拦截，重试消耗）；production exe 重建 =
  **`376ffea17f9a1d9220e4f2bf452775365014cd6996ef07bdaeb70b05a27c0f60`**
  （22:25:55）。回归：candidate 四 target **10/22/20/25 全 PASS**（forensic
  为纯诊断路径，零行为变化）。

### NEXT（Human 复跑一次 → 证据回填）

Human 用重建后的 exe（同 B1 live test 步骤，env 不变）复跑一次提取，然后由
Agent 读取 `%TEMP%\modbuslens_provider_forensics.log`（纯 sanitized 字段）
完成 §5 报告。**在证据回填前：不改 parser、不做 normalization、不 commit。**

## B1 Live RCA #2 — SANITIZED EVIDENCE：provider 非 2xx 拒绝（2026-10-08 深夜）

Human 用 forensic exe（`376ffea1…0f60`）复跑一次，`%TEMP%\modbuslens_provider_forensics.log`
唯一记录（22:31:48.909，逐字转录，纯 sanitized 字段）：

```text
http_status=<not exposed by seam> transport_ok=1 status_ok=0
response_body_len=96
envelope_json_parse=ok
choices_count=-1
message_exists=0
content_type=<absent> content_len=-1
reasoning_content_exists=0 reasoning_len=-1
finish_reason=<absent>
envelope_content_extraction=FAIL(failure_owner=envelope)
failure_owner=envelope (strict parser NOT reached)
```

### 定性（依据 transport 实测语义 `statusOk = (200 ≤ status < 300)`）

- **transport = PASS**（DeepSeek 返回了 96 B 响应体）；
- **HTTP status = 非 2xx**（`status_ok=0`）⇒ `CandidateProviderRunner::begin`
  走 `!exchange.statusOk` 分支 = **ProviderRejectedStatus**（本轮 UI token 应为
  `provider_rejected_status`，非 `malformed_response`）；
- 96 B body = 合法 JSON object 且**无 choices** ⇒ DeepSeek 的 **error envelope**
  （典型 401 authentication / 402 insufficient balance / 429 rate limit /
  422 invalid request——数值 status 与 error.code 未暴露给 seam，无法从当前
  sanitized 记录区分）；
- envelope content 提取失败 = 非 2xx 的**下游结果**（error body 本无 choices）；
- strict parser / local Evidence validation = **NOT REACHED**。

### Root cause category

**A–E 之外 = 新 CASE F：provider 端非 2xx 拒绝**（Human 的 A–E 均假设 2xx
envelope；本次连 2xx 都没有）。

### 与 live #1（`malformed_response`）的关系——两次不是同一失败类别

- live #1（exe `9f84579a`，无取证）：若当时同样非 2xx，UI 必显示
  `provider_rejected_status`（该分支自 B1 初版即存在）——故 live #1 的
  `malformed_response` 说明当时 statusOk=1（2xx）而 envelope content 提取
  失败（content 空/缺失/非 string）。**当时细节不可回溯（无取证），如实声明。**
- live #2（本次）：非 2xx。DeepSeek 免费额度/余额在两次请求之间耗尽是常见
  现实原因（与 T033 时代 ModelScope 额度耗尽同型），**须由 Human 在 DeepSeek
  控制台核对 key 有效性 / 余额 / 限流**——seam 不暴露数值 status，
  sanitized 记录无法替 Human 断言具体码。

### Smallest technically correct fix（供 Human 裁定，本轮未实施）

1. **最先做（零代码）**：Human 核对 DeepSeek 账户状态（key / 余额 / 限流）。
2. **最小 forensic 增强（可选，Human 裁定后再做）**：把数值 HTTP status 与
   error envelope 的 `error.type`/`error.code`（枚举元数据，非正文）纳入
   sanitized 记录，使 401/402/429/422 可区分。
3. **不建议**：放宽 parser（无关）、改 prompt/thinking/max_tokens（本次证据
   不指向请求契约——除非 Human 核对后确认是 422 invalid request）。

### Forensic instrumentation 收敛（按 Human §5 登记）

当前 forensic = TEMPORARY DIAGNOSTIC INSTRUMENTATION。B1 commit 前 Human 必须二选一：
A. 删除；B. 收敛为受控、无敏感数据、**默认关闭**（env 开关）的正式 diagnostic。
默认建议 = B（envelope/结构分析对后续 provider RCA 有真实价值），但以 Human 裁定为准。

## B1 Live RCA #3 — GATED NUMERIC-STATUS FORENSICS ADDED（2026-10-08 深夜）

按 Human 指令在**底层持有数值 status 的 transport 层**（`ChatCompletionsTransport::send`，
`result.status` 处——SESSION N `Exchange` seam 刻意不带数值 status，public API
不为取证扩散）新增**显式 gated** 取证：`MODBUSLENS_PROVIDER_FORENSICS=1` 时记录
`http_status=<integer>` + error envelope 的 `error_code_type`/`error_code`/
`error_type_type`/`error_type`（bounded scalar ≤64 字符，超长只记长度）；未设置
时 **zero file output + zero qWarning output**（adapter 侧 `writeForensics` 同
开关）。禁止项照旧：无 key/Authorization/error.message/raw body/prompt/
manual text/assistant content/reasoning content。
production exe 重建 = **`cb364e6329b11f1fa58f4858b57087bbcb1a151ce1ce76b4be216370f6148905`**
（7,053,249 B / 22:59:04；期间一次链接被运行中的 ModbusLens PID 55708 锁定，
Human 关闭后完成）。**复跑步骤**：`set MODBUSLENS_PROVIDER_FORENSICS=1`（连同
既有三个 env）→ 启动 exe → 原有 PDF 触发一次提取 → Agent 读 forensic 日志按
官方 DeepSeek 语义分类（401 auth / 402 balance / 422 params / 429 limit /
5xx server）。**本轮零 parser/contract 改动；未 commit。**

## B1 Live RCA #4 — HTTP 200 全链路到 parser OK，进入 Evidence Validation 层（2026-10-08 23:0x）

Human 用 forensic exe（`cb364e63…8905`）复跑，forensic 记录（23:04:29.951，逐字）：

```text
[transport] http_status=200 transport_ok=1 response_body_len=638 error_envelope=absent
[adapter]   transport_ok=1 status_ok=1 envelope_json_parse=ok choices_count=1
            message_exists=1 content_type=string content_len=160
            reasoning_content_exists=0 finish_reason=stop
            envelope_content_extraction=ok content_outer_nonempty=1
            structured_json_parse=ok top_level=object proposals_size=1
            items=structurally_conformant
            parser_result=ok (local evidence validation follows)
```

**定性**：HTTP 200；envelope 完整（1 choice、message、string content 160 字符、
finish_reason=stop、无 reasoning_content）；content 结构化 JSON 合法
（top-level object、proposals=1、**structurally_conformant**——target/value/
evidence 三字段齐、类型正确、target 已知、无 unknown/empty 字段）；**strict
parser = PASS**。链条已推进到 **local Evidence validation 层（forensic 未覆盖
的最后一层）**。之前的 provider 拒绝（非 2xx）未再复现——与 Human 侧账户/
额度状态恢复一致。

**UI 最终 token 未在 forensic 覆盖范围内**（Evidence validation 发生在
`CandidateExtractionController::completeAttempt`）：若本次 UI 显示候选卡片则
全链闭环；若显示 `no_candidate` = Evidence validation 拒绝（最可能 =
excerpt 在 canonical text 中无法唯一重定位——PDF 提取文本的空白/换行差异是
此类失败的典型来源），届时再取证该层。**本轮零代码改动；等 Human 报告本次
UI 最终显示。**

## B1 Live RCA #5 — GATED EVIDENCE-VALIDATION FORENSICS ADDED（2026-10-08 深夜）

RCA #4 确认链条已推进到 local Evidence validation（forensic 未覆盖的最后一层）。
本轮在 **ui 层 controller**（`CandidateExtractionController::completeAttempt`，
core frozen pure function 零改动）新增 gated evidence forensic：
`MODBUSLENS_PROVIDER_FORENSICS=1` 时，对 provider 返回的每个 proposal 以
**与冻结 validator 完全相同的只读逻辑**重放匹配并记录：`target=<token>`
`value_len` `evidence_len` `match_count`（-1=empty excerpt / 0=not found /
1=unique / ≥2=ambiguous）`stage=`（`schema:unsupported_target` /
`schema:empty_value` / `evidence:evidence_excerpt_missing` /
`evidence:evidence_not_found` / `evidence:evidence_not_unique` / `accepted`）
——**全部为代码中真实存在的分支**（`extractProfileFieldCandidates` +
`locateUniqueExcerpt`），未发明任何新枚举。汇总记 `proposals_count` /
`refused_total` / `candidates`。禁止项照旧：无 value/evidence/canonical/
provider 正文。unset = 零输出。
production exe 重建 = **`d6fda11ce7fa35962525d9a2c466613451c2bd13e8beaf3f7e1c10f2fe91bc62`**
（23:12:02）；candidate 四 target **10/22/20/25 零回归**。
**NEXT = Human 复跑一次（env 不变 + MODBUSLENS_PROVIDER_FORENSICS=1）→
读 `[evidence forensics]` 记录 → 报告 reject stage / match_count → 归入
prompt / evidence normalization / validator / no-fix 四类之一（仅建议，
不实施）。**

## B1 Live RCA #6 — content 非法 JSON（CASE D：structured-parse 失败映射为 malformed_response）（2026-10-08 23:17）

Human 用 `d6fda11c…bc62` exe 复跑。Forensic（23:17:11，逐字）：

```text
[transport] http_status=200 transport_ok=1 response_body_len=652 error_envelope=absent
[adapter]   status_ok=1 envelope_json_parse=ok choices_count=1 message_exists=1
            content_type=string content_len=172 reasoning_content_exists=0
            finish_reason=stop envelope_content_extraction=ok content_outer_nonempty=1
            structured_json_parse=FAIL category=invalid_json
            parser_result=malformed_response
```

**定性**：
- **Evidence validation = NOT ENTERED**（`[evidence forensics]` 无记录——parser 在
  adapter 层已失败，proposals 从未产生）；per-proposal / refused_total / match_count
  = N/A；PendingReview = 0；UI token = `malformed_response`（与 parser_result 一致）。
- **Root cause category = CASE D**（Human 分类）+ 输出形态根因：assistant content
  为**非空、非合法 JSON**（172 字符）——高度疑似 markdown ```json fence 或前后
  说明文字（DeepSeek 典型输出习惯；上次成功记录 content_len=160 结构合法，本次
  172 字符无法解析）。
- **发现一个真实的 failure-token mapping 议题（Human 上轮 CASE D 预判命中）**：
  `parseStrictCandidateProposals` 对"content 无法解析为 JSON"走
  `malformedFailure()`（`MalformedResponse`），对"JSON 合法但违反 schema"才走
  `schemaFailure()`（`SchemaViolation`）⇒ structured-parse 失败被 UI 显示为
  `malformed_response`。是否把"content 非法 JSON"细分为独立 token（例如
  `invalid_content_json`）= Human 裁定项（涉及 token 契约与 UI 文案，本轮未动）。

**Smallest next-fix category（建议，未实施）= prompt**（DeepSeek 请求的
output instruction：要求仅输出裸 JSON object、禁止 markdown fence/说明文字；
属 provider-specific request extras 的合理范围）或 **adapter normalization**
（fence 剥离——但 Human 上轮已明确不自动 strip prose，须裁定）。本轮零改动。

## B1 JSON OUTPUT ROUND（IMPLEMENTED / AUTOMATED PASS / PENDING PRODUCTION LINK + HUMAN 3-RUN，2026-10-08 深夜）

Human 裁定：provider transport gate = PASS；root cause = DeepSeek assistant
content 未被约束为稳定合法 JSON（RUN A structured PASS / RUN B invalid_json，
非 transport/envelope/parser/evidence/truncation defect）。采用 **DeepSeek
native JSON Output**（Human fix decision）：

- `AiProviderProfile` 增两个 profile 决策字段（DeepSeek=true / ModelScope=false）：
  `sendJsonOutputResponseFormat`（请求体增 `"response_format":{"type":"json_object"}`）
  与 `jsonOnlyPromptInstruction`（Candidate prompt 增 JSON-only 指令：仅裸
  JSON、无 Markdown fence、无前后 prose、严格按 schema——schema 示例**从
  parseStrictCandidateProposals 真实契约恢复**（target/value/evidence），
  未发明字段）。
- **strict parser / empty-content failure / max_tokens 全部不变**（本次真实
  失败 finish_reason=stop content_len=172，非 truncation；empty content 仍
  fail-closed；不引入 retry）。
- **Tests TEST FIRST**：`ds_j01`（DeepSeek 必含 response_format=json_object）、
  `ds_j02`（prompt 必含 JSON-only 语义：JSON 要求 + no Markdown fence/prose +
  真实 schema 字段 proposals/target/value/evidence；无 golden prompt string）、
  `ds_j04`（response_format 与 thinking.disabled 共存）⇒ **REAL RED ×3**；
  控制组 ds_j03（ModelScope 无 response_format）/ ds_j05（DeepSeek 非法
  content ⇒ malformed_response fail-closed）/ ds_j06（schema 违例 ⇒
  schema_violation）/ ds_j07（false evidence ⇒ no_candidate）/ fn01（forensic
  默认关 ⇒ 零文件输出）/ fn02（gated on ⇒ 写 sanitized 记录）pre-fix 全
  GREEN。**post-fix provider matrix 19/19 全 PASS**。
- Forensic 收敛（Human §8 裁定落实）：保留为 **DEFAULT-OFF** 受控诊断
  （`MODBUSLENS_PROVIDER_FORENSICS=1` 才写文件/qWarning；未设置零输出），
  fn01/fn02 双向证明。B1 commit 形态待 Human 最终确认（删除 or 正式保留）。
- **回归**：full CTest `ctest -N` = 71 → **71/71 PASS / exit 0 / 1448.51s**；
  transport 22 / adapter 20 / orchestration 25 / extraction 14 / review 36
  零回归。
- **production exe 链接 PENDING**：全量 build 的 2 个 qmlcachegen（AV 间歇）
  已消耗，最后一步 `ld` 链接被运行中的 ModbusLens 实例（PID 55468）锁定
  （`cannot open output file modbuslens.exe: Permission denied`）。Human 关闭
  实例后完成链接并报告新 SHA-256，**3-run live test 必须使用含 JSON OUTPUT
  修复的新 exe**（旧 `d6fda11c…` 不含）。
- **Wording 修正（§11）**：档案中 "ModelScope wire semantics byte-for-byte
  unchanged" 类表述已改为 "ModelScope request semantic contract remains
  backward-compatible"（请求序列化已迁移 QJsonDocument，不声称 byte-for-byte）。

## B1 Production Link + Deployment Verification COMPLETE（2026-10-09 00:53–00:58）

- **production exe**：`E:/desktop/ModbusLens/build/release/modbuslens.exe` =
  **7,056,357 B / SHA-256
  `3ce382719d3aa720701ecca8a4b9e2d82eb9efda4e7170f833d03a3e3f5481a4` /
  LastWrite 2026-10-09 00:53:30**（≠ `d6fda11c…` RCA #5 pre-fix binary；含
  DeepSeek `response_format=json_object` + JSON-only prompt 修复）。
- **candidate deployment**：deployment gate 重建 candidate tree 后
  `candidate\ModbusLens\modbuslens.exe` 同 SHA（byte-identical，7,056,357 B，
  00:58:19）；**deployment_startup_check = Passed 291.75s**。
- **forensic default-off 最终确认**：fn01（未设置 ⇒ 零文件输出）/ fn02（=1 ⇒
  写 sanitized 记录）双控制测试在 provider matrix 19/19 内 PASS；禁止项清单
  维持（无 key/Authorization/raw response/assistant/reasoning/prompt/manual/
  evidence/value 正文）。**forensic 日志已清理**，等待 Human 3-run 干净证据。
- **NO COMMIT**；T032-C = PAUSED。**NEXT = Human 3-run Gate**（同一 provider/
  model/key/PDF/existing cache/binary，3 次独立提取，3/3 structured_json_parse
  PASS ⇒ B1 STRUCTURED OUTPUT RELIABILITY = PASS）。

## B1 HUMAN 3-RUN ACCEPTANCE（2026-10-09 09:48 · exe `3ce38271…481a4`）= BLOCKED BY 401 AUTHENTICATION FAILURE（非代码缺陷）

日志按 `[transport forensics]` 分组 = **恰好 3 个 request**（09:48:41.377 /
09:48:53.676 / 09:48:58.479），与 Human 三轮一一对应。三轮矩阵**完全相同**：

```text
http_status=401 · transport_ok=1 · status_ok=0 · response_body_len=204
error_code_type=string · error_code=invalid_request_error
error_type_type=string · error_type=authentication_error
envelope_json_parse=ok · choices_count=-1 · message_exists=0
content_type=<absent> · finish_reason=<absent>
envelope_content_extraction=FAIL（error envelope 本无 choices——非 2xx 的下游结果）
strict parser / Evidence validation = NOT REACHED（三轮均未到达）
```

### 裁定（按 Human §9 判据）

- **STRUCTURED OUTPUT RELIABILITY = UNDETERMINED / BLOCKED BY EXTERNAL
  AUTHENTICATION FAILURE**。三轮均为 **401 authentication_error**（DeepSeek
  官方语义 = API key 无效/错误），既非 `invalid_json` 也非
  `malformed_response` ⇒ 不构成 B1 代码 FAIL 判据；但 Gate 也未 PASS（structured
  阶段从未到达）。
- **UI 完全一致**：三轮截图均为 `provider_rejected_status`，与 forensic 的
  status_ok=0 精确吻合（token 映射再次被真实证据验证）。
- **关键对照事实**：昨晚 23:04/23:17（RCA #4/#6）同一 key 曾获得 **HTTP 200 +
  正常 envelope**（structured PASS / invalid_json 各一次）；今早 09:48 三次
  全部 401。两次时段之间 key 状态发生了变化。候选原因（按概率）：
  ① DeepSeek 控制台 key 被删除/重置/失效；② 本次 CMD 的 `set` 语法把引号
  存入了值（`set X="sk-..."` ⇒ Bearer 变 `"sk-..."` ⇒ 401——cmd 经典陷阱）；
  ③ key 复制不完整。**须 Human 核对**（Agent 无 key 访问权，不猜测）。
- **B1 代码层正面验证**：401 路径在真实 provider 下行为完全正确——错误
  envelope 解析、`error.code/type` sanitized 取证、fail-closed、UI token、
  零凭据泄露（安全检查：日志无 key/Authorization/任何正文）。

### NEXT（Human 侧，零代码）

1. 核对 DeepSeek 控制台 key 状态（是否被删除/重置）；
2. 重建 CMD 会话，用**不带引号**的 `set DEEPSEEK_API_KEY=sk-xxxx` 重设，
   只核对前缀/长度（如 `echo %DEEPSEEK_API_KEY:~0,5%`）不外发完整 key；
3. key 修正后按原协议重跑 3-run（同 PDF/existing cache/forensic 开关）。
   B1 判定顺延到首次 3/3 structured-PASS 完成。
