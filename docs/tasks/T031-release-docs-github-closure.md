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

## Phase B — README Finalization：**NOT STARTED**

（README 引用 01/02/04 Final 截图、Hero 段落打磨——需单独 Human 授权。）

## Phase C — GitHub Metadata / Web Operations：**NOT STARTED**

（LICENSE 决策、description/topics/social preview、CHANGELOG 发布联动、demo GIF——需单独 Human 授权；gh 不可用时为 Human 网页操作。）

## Files changed（Phase A）

CHANGELOG.md（新增）、docs/02_ARCHITECTURE.md、docs/04_TEST_STRATEGY.md、
docs/05_DEMO_GUIDE.md、docs/07_FINAL_PROJECT_REVIEW.md、
docs/11_PROJECT_FINAL_RETROSPECTIVE.md、docs/tasks/T031-…md（新增）、
docs/PROJECT_STATUS.md、docs/BACKLOG.md、docs/devlog/2026-10-07.md。

## Next gate

Human review of Phase A → T031-B（README finalization）授权。