# ADR002 — T012 Agent v1：Read-only Tool Architecture

- **状态**：Accepted（2026-09-09，T012 Learning / Test Design 定案）
- **关联**：T012 Agent Tools；FR-AG-01/02；ADR001（UI 分层）延伸
- **Supersedes**：无（不推翻 ADR001，只在其 App/Adapter 层上新增 Agent 元素）

## 背景

T011 已交付 one-shot LLM 解释（deterministic facts → bounded prompt → 单次回答，AI is interpreter, not detector）。
T012 引入用户自由提问 + 模型按需调用**只读 tools** 读取已确定的诊断事实。这带来三个此前不存在的架构问题：
(1) LLM 首次获得"间接读取产品状态"的能力，越权面增大；(2) 工具返回值的类型与序列化边界未定；(3) Tool Calling 是多轮异步交互，T011 的单请求 stale guard 需要延伸到整个 run。

## 决策

**D-A1（read-only tool contract）**：Agent v1 只允许三个白名单工具（get_session_summary / get_recent_anomalies / get_transaction_detail），全部只读取 active batch 的 deterministic 状态（TransactionAnalysis / StatisticsSnapshot / DiagnosisTransaction）。产品代码中**不存在**任何写操作 tool 的类型、名字、schema 或调度分支；"写能力在类型层面不存在"从"没有写 API"升级为"写能力连名字都不在 contract 里"。模型无权决定"能做什么"，只能从 C++ 下发的固定 schema 中"选择读什么"。

**D-A2（tool results are typed deterministic facts）**：工具返回链为 `Core typed facts → App 层 typed tool-result struct（每工具一个）→ provider 适配层 JSON 序列化`。禁止 Core/Controller 拼一大段人类文案 QString 让模型"自己猜语义"；JSON 只从 typed struct 机械生成；所有字段来自 deterministic 事实，不包含 QML text、statusText 展示文案、文件名、路径、Replay comments 或用户自由文本（Presentation is output, not authority 的新形态）。

**D-A3（agent run binds activeBatchRevision）**：一次 Agent run（从用户提问到最终回答，含多轮 tool 调用）在启动时捕获 `activeBatchRevision_` 与全新的 run generation（复用 T011 `aiRequestGeneration_` 模式，Component 级新增独立 `agentRequestGeneration_`）。run 内每一轮 HTTP 响应、每一次 tool 执行结果在应用前都校验 `capturedRevision == activeBatchRevision`；不相等（用户切换了 batch）或 generation 过期（同 batch 新 run 产生）→ 静默丢弃，绝不写 UI。上限轮数见 T012 档案。

## 理由

- 三个决策共同构成 Agent 的安全/正确性模型：**能力边界（D-A1）、数据保真（D-A2）、时间有效性（D-A3）**，缺一不可。
- D-A1 延续并强化了 FR-AG-02 与 T011 宪法"AI 不产生协议事实"：工具只是"按需读事实"的通道，不是计算入口。
- D-A2 是 ISSUE-006 教训的延伸：凡影响模型判断的事实都必须结构化、显式下发，模型不能从展示文案反推业务语义。
- D-A3 直接复用 T011 双层 stale guard 的成熟模式，不引入第三套异步版本系统。

## 后果

- 正面：越权面被 contract 冻结为三函数；自动测试可完整覆盖（AGENT-A/B 矩阵）；面试可解释性强（类型级强制 vs 约定）。
- 负面：若未来要加第 4 个 tool，必须显式改 contract + schema + dispatcher（这恰是设计意图）；batch-scoped transaction_id（1-based 序号）在 batch 切换后失效，跨 batch 的问题查询天然不支持（v1 无历史存储，符合 no-memory 原则）。

## 关联记录

- 实现档案：`docs/tasks/T012-agent-tools.md`（本轮 Learning / Test Design）
- 反向约束：不重复 D5（Agent 只读 via HTTP 的早期草案）；本 ADR 落地形态以 C++ 本地 Browser 内 tool 调用为实，D5 的"HTTP 与只读 Service"形态按 T012 实际设计修正解释。


## Implementation Review Refinement（2026-09-09，Part A 前追加）

- 参数命名 D-A2 细化：batch-scoped ordinal 一律称 `transaction_number`（不称 transaction_id——不存在真正稳定 ID，措辞不得暗示持久性）。
- D-A2 新增要件：tools 只读 **immutable AgentToolContext snapshot**（run 启动时一次构建，const& 注入 dispatcher）；revision guard 仍属 runtime（Part B），两者的职责分离在本 ADR 内固化。
- D-A1 细化：`get_recent_anomalies` 的 "recent" 语义锁定为 latest-20（原序返回、truncated 标志），消除"最早 20 条"歧义。
- 措辞边界：Agent layer 是 **offline / zero-network**，并非 Zero Qt——QtCore JSON 仅存在于 arguments / serialization adapter boundary；档案与代码注释不得声称 Part A 为 "Pure C++ / Zero Qt"。
