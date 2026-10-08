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
