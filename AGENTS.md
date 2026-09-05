# AGENTS.md — ModbusLens 开发代理规约

> **本文件是仓库的最高工作规约。** 任何 AI 编码代理（ZCode、Claude Code、Cursor、Copilot、CodeBuddy 等）以及人类开发者，在接手本仓库工作时，必须**先完整阅读本文件**，再阅读 PROJECT_STATUS / BACKLOG / ARCHITECTURE 和对应任务文档。

## 项目一句话

ModbusLens 是一个 C++20 + Qt6 的工业通信（Modbus）智能诊断平台，支持 **Simulator / Replay / Serial** 三种数据源模式，三种模式共享同一套协议解析、事务分析、统计与诊断核心。项目第一优先级是：**可构建、可测试、可演示**；并为一手秋招面试准备完整、真实、可追溯的开发记录。

## 核心工作纪律（必须全部遵守）

1. **一次只执行一个有编号的任务。** 每轮工作只推进一个 `T00x` 任务；禁止并行推进多个任务、夹带与当前任务无关的改动。
2. **开始任务前必须按顺序阅读**：`docs/PROJECT_STATUS.md` → `docs/BACKLOG.md` → `docs/02_ARCHITECTURE.md` → `docs/tasks/T00x-*.md`（当前任务文件）。读完才允许动手。
3. **修改代码后必须执行构建和相关测试。** 未构建、未测试的代码不允许标记任务完成；失败必须修复或如实记录进任务文档。
4. **完成任务后必须更新对应 Task 文档**（`docs/tasks/T00x-*.md`），按模板补齐全部章节，Verification 要贴真实命令与输出。
5. **完成任务后必须更新 `docs/PROJECT_STATUS.md` 和 `docs/BACKLOG.md`**，两个文件同步更新，不得只更新其一。
6. **遇到值得记录的技术问题时，在 `docs/issues/` 创建 Issue 文档**，命名 `ISSUE-00x-<slug>.md`，并在任务文档的 Problems/Solutions 章节中链接。
7. **做出重要架构选择或显式取舍时，在 `docs/adr/` 创建 ADR**，命名 `ADR-00x-<title>.md`，并在相关任务/Issue 中引用。
8. **不得删除历史开发记录。** `docs/tasks/`、`docs/issues/`、`docs/adr/`、`docs/devlog/` 是只增不改的档案区（如需修正事实，用追加批注，不得覆盖原文）。
9. **不进行与当前任务无关的大规模重构。** 发现需要重构处，记入 BACKLOG 或 Issue，由专门任务处理。
10. **Simulator、Serial、Replay 三种模式必须共享协议解析、事务分析、统计与诊断核心。** 禁止为单一模式私有化复制核心逻辑；新功能优先落在共享层。
11. **核心通信分析功能不得依赖 LLM 才能运行。** LLM/Agent 是增强插件；断网、未配置、卸载时核心功能必须完整可用。
12. **Agent 初期只允许只读诊断能力**（读日志、读统计、生成诊断说明）；严禁让 Agent 具备写线圈/写寄存器等任何下发控制能力。
13. **项目状态必须落在 Git 仓库的 Markdown 文件中。** 任何重要决策、问题、进度不得只存在于聊天上下文或任何平台的私有 Memory；仓库文档是唯一事实来源。

## 标准任务流程（每轮工作按此执行）

```text
1. 阅读项目状态           PROJECT_STATUS → BACKLOG → ARCHITECTURE → 任务文档
2. 明确任务边界            目的 / 验收标准 / 明确"不做什么"
3. 实施                    小步修改，每步保持可编译
4. 验证                    构建 + 测试（命令见 PROJECT_STATUS）
5. 归档                    更新任务文档 → PROJECT_STATUS → BACKLOG
                           （必要时创建 Issue / ADR / devlog 条目）
6. 提交                    git commit，格式见下
7. 自检                    对照本文件"核心工作纪律"逐条确认
```

## 提交规范

- 一个任务对应一个（或少数几个语义清晰的）提交，禁止跨任务混合提交。
- 提交信息首行：`T00x: <简短描述>`，例：`T001: 项目引导 — 骨架、文档体系、最小 Qt6 应用`。
- 正文可选，补充动机、验证结果、已知限制。

## 文档体系地图

| 文件 | 作用 | 更新时机 |
| --- | --- | --- |
| `AGENTS.md` | 本规约（工作纪律 + 流程） | 规约变化时 |
| `README.md` | 项目门面：是什么、怎么跑 | 里程碑级变化时 |
| `docs/00_PROJECT_CHARTER.md` | 愿景、目标、非目标、成功标准、里程碑 | 方向变化时 |
| `docs/01_REQUIREMENTS.md` | 需求基线（按 FR/NFR 编号） | 任务细化或变更需求时 |
| `docs/02_ARCHITECTURE.md` | 分层架构与核心设计决策 | 架构变化时（配 ADR） |
| `docs/03_MODBUS_LEARNING.md` | Modbus 协议知识库（学习笔记） | 学到新知识点时 |
| `docs/04_TEST_STRATEGY.md` | 测试策略、分层、验收口径 | 测试手段变化时 |
| `docs/05_DEMO_GUIDIDE.md` | 演示指南（三种模式的演示脚本） | 新增演示能力时 |
| `docs/PROJECT_STATUS.md` | 项目状态单一事实源 | **每个任务完成后** |
| `docs/BACKLOG.md` | 全部任务与优先级（里程碑视图） | **每个任务完成后** |
| `docs/ENVIRONMENT.md` | 环境搭建/工具链/常见坑 | 环境变化时 |
| `docs/INTERVIEW_NOTES.md` | 面试问答素材（按任务沉淀） | 每个任务完成后追加可答问题 |
| `docs/tasks/T00x-*.md` | 每个任务的过程档案（模板见下） | 任务开始时建稿，**完成时补齐** |
| `docs/issues/ISSUE-00x-*.md` | 技术问题的定位与解决记录 | 遇到值得记录的问题时 |
| `docs/adr/ADR-00x-*.md` | 架构决策记录 | 重要取舍发生时 |
| `docs/devlog/YYYY-MM-DD-*.md` | 每日开发日志（简记 + 链接） | 每个工作日结束时 |
| `demo/` | 演示素材：脚本、示例日志、录屏清单 | 演示能力落地时 |

## 任务文档模板（docs/tasks/T00x-*.md 必须包含以下章节）

```markdown
# T00x — <标题>

- **Goal**           …（为什么做，解决什么问题）
- **Background**     …（背景与现状）
- **Technical Decisions** …（技术选型与理由；重要决策需另立 ADR 并引用）
- **Implementation** …（如何设计、关键实现思路）
- **Files Changed**  …（新增/修改文件清单）
- **Problems Encountered** …（遇到的问题，链接 Issue）
- **Solutions**      …（如何定位与解决）
- **Verification**   …（构建/测试命令与真实输出）
- **Result**         …（最终结果与验收结论）
- **Knowledge Learned** …（技术收获）
- **Potential Interview Questions** …（本任务可考点的面试问题 + 答题要点）
- **Git Commit**     …（提交哈希与信息）
```

## Issue 文档要点

标题 `ISSUE-00x: <一句话现象>`；正文含：现象 / 影响 / 复现步骤 / 定位过程 / 根因 / 解决方案 / 验证 / 教训。任务文档必须引用它。

## ADR 文档要点

标题 `ADR-00x: <决策名>`；正文含：背景 / 备选方案 / 决策 / 理由 / 后果（正面与负面）/ 关联任务。已被推翻的 ADR 不删除，追加"状态：Superseded by ADR-00y"。

## 本文件自身的维护

如需修改工作纪律，须在提案中说明理由，并与修改后的 PROJECT_STATUS 一并提交。