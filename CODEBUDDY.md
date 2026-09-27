# ModbusLens — WorkBuddy / CodeBuddy Bootstrap

@AGENTS.md

> 本文件是**薄 bootstrap adapter**：只负责把 WorkBuddy / CodeBuddy 接入本仓库的
> 通用规约（AGENTS.md）与 canonical truth 入口。**不复制规约、不缓存状态、
> 不建立第二份事实。**

## Repository truth（唯一事实来源）

canonical truth 位于 Git 仓库内：source / tests、任务契约（`docs/tasks/`）、
`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、`docs/devlog/`。

Auto Memory、旧 WorkBuddy 会话、task handoff summary、压缩上下文
**全部只是提示（hints）**；与 repo / canonical docs 冲突时，**repo 胜**。

## Mandatory resync before write

在以下任何操作之前，必须**主动重新读取**（不得凭记忆或摘要）：

- behavior implementation
- docs state transition
- LKGC change
- package / staging operation

重新读取清单：

1. `docs/PROJECT_STATUS.md`
2. `docs/BACKLOG.md`
3. 当前任务契约 —— **当前任务必须从 PROJECT_STATUS / BACKLOG 识别**；
   进行 M12 相关工作时使用
   `docs/tasks/T027-m12-device-profile-manual-intelligence-contract.md`。
   （不要假定任何 milestone 永远处于 current 状态。）
4. `git rev-parse HEAD` 与 `git status --porcelain`
5. 与任务相关的 source / tests

并按 AGENTS.md「Cross-Agent Context / Anti-Drift」第 3 条执行 resync，
第 4/5/6/7/8 条的状态等式、验收边界与 commit 分类纪律同时生效。

## Volatile facts（禁止缓存）

以下内容**禁止**从本文件或任何 agent memory 缓存 / 沿用旧值，必须实时读取：

- current HEAD / branch
- verified LKGC
- test count / gate 数量
- package / staging hash
- milestone current state（各 Mxx / T0xx 进行到哪一步）

## Evidence（不得互相替代）

`historical evidence ≠ currently executed`；`automated PASS ≠ Human accepted`；
`Human accepted ≠ verified LKGC`；`verified LKGC ≠ package`；
`deterministic demo / simulation ≠ real hardware evidence`。
完整规则见 AGENTS.md「Cross-Agent Context / Anti-Drift」第 4、8 条与
「操作安全 / Mutation Safety」。

## Language

- 分析、进度说明、风险与最终报告：**中文**；
- 命令、代码、路径、函数名、测试名、hash、原始日志：**保持原文**。

---

维护规则：本文件保持极薄。需要新增规则时优先写入 AGENTS.md（跨 Agent 通用），
本文件只在需要 WorkBuddy 专属 bootstrap 行为时追加**引用**，不复制条文。