# T031 — Release Docs / GitHub Closure（分阶段）

- **Purpose**：v2.0.0 发布后的封板收尾。关闭 T028 审计发现的文档真实性缺口（C1–C4）、建立 CHANGELOG、明确 architecture.png 与 GitHub 展示材料的处置——分阶段执行，每阶段独立可验证。
- **Human decision（输入）**：T030-R2 engineering = ACCEPTED；05-serial RECAPTURE = BLOCKED → **DROP 05 FROM README**（被拒版本保留于 Git 历史 6982c78）；03-replay = **KEEP AS REPOSITORY EVIDENCE / DO NOT USE AS README SHOWCASE**；最终 README 截图集 = **01-hero-dashboard / 02-transactions / 04-diagnosis-baseline**（均 ACCEPTED）；UI FREEZE = ACTIVE；PRODUCT CODE = UNCHANGED。

## Phase A — Documentation Truth Closure（本任务，docs-only）

### A.0 Scope

关闭 T028 缺口 C1–C4；创建 CHANGELOG；architecture.png 处置；T031 任务建档。README/CLIENSE/GitHub 元数据 = Phase B/C，本轮不做。

### A.1 C1 resolution — 02_ARCHITECTURE truth（docs/02_ARCHITECTURE.md，+3/−3）

- D5（Agent 边界）：精确化为 "**Agent** 工具层无写 API（FR-AG-02 不变）"，并与 M10 用户确认式 Serial 写路径（用户操作面）明确区分。
- D7（Active 契约）：更新为当前事实——FC03 读路径不变；**M10 起新增 FC06/FC16 主动写路径**（prepare/validate + 写前快照原值回显 → 确认对话框 → 派发；连接前提；Confirmation vs Dispatch 边界）。
- D7 尾（T015 grep 锚）：标注该锚已被 FR-AG-02 Agent 工具层约束取代。
- 未声称超出 repo 证据的能力；未做大规模重写。

### A.2 C2 resolution — 04_TEST_STRATEGY truth（docs/04_TEST_STRATEGY.md，+9/−1）

- 文首新增 **CURRENT STATE** 快照：70 个 CTest 目标（HEAD `1c4f9a0` 时点）、明确下文 23/24 为历史叙事、T015 "无 FC06 encoder" grep 锚已被 M10 取代、硬件边界不变（harness/seam 证据，REAL HARDWARE = NOT VERIFIED）。
- T015 时代 grep 锚声明就地标注【T015 时点历史事实…】。
- **实际执行的当前验证**：`ctest`（build/release，凭据缺席）= **70/70 PASS，exit 0**（结果记录于本文件 §A.6 与 devlog；该运行是对当前 HEAD 产品状态的验证快照，非"永恒架构事实"）。

### A.3 C3 resolution — 05_DEMO_GUIDE truth（docs/05_DEMO_GUIDE.md，+4/−4）

- §1 演示=验收：ctest 23/23 → 发布时点 70/70。
- §3 checklist：23/23 → 全量全绿（发布时点 70/70）。
- §2 步骤 6：Serial 面板补 M10 写入区与确认安全边界（未接设备时写入禁用）。
- §2 步骤 9：Agent 权限边界措辞精确化（Agent 无写 API；产品写路径与 Agent 无关）。
- 未发明 live COM demo；未提及 COM_HARNESS 为产品能力。

### A.4 C4 resolution — historical snapshot clarity（07/11，各 +2/−0）

- 07_FINAL_PROJECT_REVIEW.md / 11_PROJECT_FINAL_RETROSPECTIVE.md 顶部各加**历史快照声明**：记录的是当时 milestone 状态；当前状态权威 = PROJECT_STATUS；发布准备权威 = T028/T031。历史正文原样保留。

### A.5 architecture.png disposition

- 来源核查：仓库内**无**生成源/recipe（二进制资产于 a132f9c 直接入库）。
- 处置：**ARCHITECTURE.PNG = STALE / DO NOT USE IN README**（缺 Manual Intelligence 层）；建议 T031-B 以 Mermaid/文本架构替代。未伪造重绘。

### A.6 CHANGELOG result

- 新建 **CHANGELOG.md**：`## 2.0.0 — 2026-10-07`（首次公开发布；资产身份；自 v1.0.0 历史 tag 以来的证据支撑能力清单；已知边界）+ `## 1.0.0 — 2026-09-14（历史 tag，未发布 Release）`。发布日期取自 tag/Release 证据（2026-10-07），未虚构版本史。

### A.7 Verification

- `git diff --check` PASS；全部变更仅限 §14 允许清单。
- 产品 diff = EMPTY；README diff = EMPTY（本轮禁改）。
- 当前验证快照：CTest **70/70 PASS，exit 0**（见 A.2/A.6）。

### A.8 Release provenance boundary

- v2.0.0 已发布并独立验证；本 docs 提交在发布**之后**：不移动 tag、不重建 v2.0.0、不替换资产、不把 T031 描述为 release commit、不暗示文档编辑改变已发布二进制字节。

## Phase B — README Finalization：**COMPLETE**（2026-10-07）

- **Phase A = COMPLETE / HUMAN ACCEPTED**（输入基线）。
- **README 最终信息架构**（16 项收敛为 12 节，就近合并不凑数）：
  1 标题+一句话定位 → 2 **Hero 截图**（01）→ 3 它解决什么问题 →
  4 核心亮点 → 5 产品导览（02 事务 + 04 基线诊断）→ 6 架构（Mermaid）
  → 7 三种运行模式 → 8 **Serial 写安全边界（FC06/FC16）** → 9 技术栈
  → 10 Quick Start → 11 验证与测试 → 12 演示/文档入口 → 13 已知限制
  → 14 版本与许可。
- **采用的截图 = 01-hero-dashboard / 02-transactions /
  04-diagnosis-baseline**（仅此三张内嵌；04 明确标注"确定性基线诊断，
  非 AI 生成"）。**03-replay = 仓库内证据，不内嵌**（README 仅一句说明
  其未纳入展示的原因）；**05-serial 已 DROP**（不引用不存在资产）。
- **架构图决策 = Mermaid 内嵌**（`flowchart TB`）：呈现三数据源路径 →
  Zero-Qt 确定性 Core → App/Controller → QML UI；AI Explanation /
  Read-only Agent 为可选上层（无 LLM 时核心可用）；Agent 无写工具/写
  API 且与 Serial 用户确认式写路径**显式隔离**；**未使用
  docs/assets/architecture.png**（无生成源，不伪造替代）。
- **Agent/写边界措辞**：§4 与 §6 双处显式声明——Agent 工具层无任何写
  API；写只能经用户显式确认后的 Serial 派发；FR-AG-02 只读边界不变。
- **Quick Start 来源**：docs/ENVIRONMENT.md（presets debug/release、
  ctest、运行路径、deploy 脚本）+ 既有 Release 链接（真实 v2.0.0 URL，
  未发明 URL/版本号）；AI 配置与核心运行明确分离。
- **测试证据措辞**：CTest **70/70 PASS（2026-10-07 release
  verification snapshot，exit 0）**，显式标注为快照而非永恒常量；
  硬件边界 = REAL MODBUS HARDWARE NOT VERIFIED；未重跑 224s 套件。
- **Known Limitations 结果**：硬件未验证 / 功能码范围收敛 /
  无 register map / 未实现（TCP、时序、installer、签名）/ AI 需配置且
  核心不依赖 / Replay 批量重析——全部为当前事实，未列已修复历史 bug。
- **链接/路径验证**：14 处本地引用 **0 broken**；3 张图片路径全部存在；
  §16 审计清单（architecture.png / 05-serial / 03-replay 内嵌、23/23、
  ctest 24、FC03-only、COM_HARNESS、私有路径、凭据、版本错位、
  post-release≠release commit、AI 强制论）**全部 0 命中**。
- **文件**：README.md（唯一实质性展示变更）+ 本档案 + PROJECT_STATUS +
  BACKLOG + devlog。产品/截图资产/architecture.png/CHANGELOG = 未动；
  LICENSE 未创建。
- **Phase C = NOT STARTED**；**LICENSE = HUMAN DECISION REQUIRED**。
- **next Human gate** = README review → T031-C（GitHub 元数据 / LICENSE）。

## Phase C — GitHub Metadata / Web Operations：**NOT STARTED**

（LICENSE 决策、description/topics/social preview、CHANGELOG 发布联动、demo GIF——需单独 Human 授权；gh 不可用时为 Human 网页操作。）

## Files changed（Phase A）

CHANGELOG.md（新增）、docs/02_ARCHITECTURE.md、docs/04_TEST_STRATEGY.md、
docs/05_DEMO_GUIDE.md、docs/07_FINAL_PROJECT_REVIEW.md、
docs/11_PROJECT_FINAL_RETROSPECTIVE.md、docs/tasks/T031-…md（新增）、
docs/PROJECT_STATUS.md、docs/BACKLOG.md、docs/devlog/2026-10-07.md。

## Next gate

Human review of Phase A → T031-B（README finalization）授权。