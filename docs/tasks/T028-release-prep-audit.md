# T028 — Release Prep R1: Documentation & Evidence Audit / GitHub 封板审计

- **Goal**：v2.0.0 已公开发布（H1–H8 PASS、R5G 独立验证、main 已推送）。本任务为**封板审计**：盘点真实证据、建立 GitHub 展示材料缺口与截图合同、分类 P0/P1/P2 问题——只记录，不修复。
- **Background**：发布链见 T027 §108–§118；closure 见 §119；main 已 fast-forward 至 `2f53197…` 并推送。
- **Mode**：STRICT DOCS / EVIDENCE AUDIT ONLY。零产品/UI/打包改动。

## 1. Preflight（实测）

```text
HEAD = 2f53197cc149bbf75e876b3b94234a2f934a8048（POST-M12 closure，docs-only）
branch = main；tracked/cached diff = empty；git diff --check = PASS
tags = v1.0.0 / v2.0.0；v2.0.0 → bdabd6fd…（object）→ b18e378…（peeled）
未解释 tracked modification：无（BASELINE OK）
```

## 2. Evidence Inventory（状态分类）

| 项 | 证据 | 状态 |
| --- | --- | --- |
| A Project status | PROJECT_STATUS 最新块（post-release closure） | VERIFIED |
| B Backlog | 时间线至 2026-10-07（R4/R5/CLOSE 全在案） | VERIFIED |
| C tasks | 25 份（T001–T027，含发布链 §108–§118、closure §119） | VERIFIED |
| D issues | ISSUE-001~020（008/009 = MONITORING/NON-BLOCKING；020 = harness 凭据 live-dispatch，已档案化） | VERIFIED |
| E ADR | 2 份（ADR001 QML、ADR002 只读 Agent） | VERIFIED |
| F devlog | 逐日至 2026-10-07 | VERIFIED |
| G tests | CTest 70/70（REL-R2/R3 fresh 树两轮 + closure 前 main 推送基线） | VERIFIED |
| H samples | demo_v1.mlog（canonical 黄金样本）+ Test_Manual_*（repo-only） | VERIFIED |
| I screenshots | 59 张（M9-A..M9-F 时代，最新 = m9f-f2-*，1024x720/1000x700） | **STALE**（全部早于 M10/M11/M12 UI：无 Device Profile 工作区/Manual Q&A/UX 修复后状态） |
| J architecture image | docs/assets/architecture.png（2026-09-15，M9-A 时点） | **STALE**（缺 Manual Intelligence 层） |
| K benchmark | docs/10（Replay 141 万 records/s canonical 口径，core 侧） | VERIFIED（与 UI 无关） |
| L package/release | v2.0.0 Release PUBLIC + R5G 独立验证 + H1–H8 PASS + main 已推送 | VERIFIED |
| M README | 本日已事实修正（Serial FC03+FC06/0x10、Manual Intelligence、Release 链接、ctest 70/70） | VERIFIED |
| N build/run | README + ENVIRONMENT（Qt 6.11.1/presets/Python 变量） | VERIFIED |
| O known limitations | README + T027 §119.18 + 07 第九章 | VERIFIED |
| P demo instructions | 05_DEMO_GUIDE §2 十步主线 + §8 v2.0.0 扩展 + fallback | VERIFIED |

## 3. Release Gap List

**P0 = 无**（发布本体已完成且独立验证；未发现核心功能错误/崩溃/安全问题）。

| ID | Sev | Area | Evidence / Symptom | Why it matters | Next phase |
| --- | --- | --- | --- | --- | --- |
| GAP-01 | P1 | GitHub 展示 | docs/assets/screenshots 59 张全部为 M9-F 前后时代，**无一张**反映 v2.0.0 最终 UI（Device Profile 工作区、Manual Q&A、UX 修复后状态） | README/Release 是公众第一印象；旧图 ≠ 现产品 | UI Release Audit → 修复 → UI FREEZE → ZCode 生成 Final Set |
| GAP-02 | P1 | GitHub 展示 | README 无 hero/supporting 截图引用（架构图除外） | 无图作品集项目说服力弱 | Final Screenshot Set 完成后引用 |
| GAP-03 | P1 | 文档 | `docs/02_ARCHITECTURE.md` §（line 89）仍称"Serial 永远只发 FC03 读（写能力在类型/API 层不存在）" | 与 M10 事实冲突且已随 main 公开 | 文档修正任务（逐字更新 + 注明演进） |
| GAP-04 | P1 | 文档 | `docs/04_TEST_STRATEGY.md` 停在 T015 时代（ctest 24/24、"无 FC06 encoder"grep 锚）；`docs/05_DEMO_GUIDE.md` §1/§3 仍写 ctest 23/23 | 公开文档数字与现实（70）冲突 | 文档修正任务 |
| GAP-05 | P1 | 法律/发布 | 无 LICENSE 文件（M9-E 决策"不发明"，当时为私有阶段；**现已公开**） | 公开仓库无许可证 = 他人法律上不可复用 | **Human 法律决策**（选择并提交许可证；ZCode 不得代选） |
| GAP-06 | P1 | GitHub 元数据 | repository description / topics / social preview 未经确认（gh 不可用，本环境无法只读核验） | 搜索/第一印象 | Human 于 GitHub 网页端设置 |
| GAP-07 | P2 | 文档 | 无 CHANGELOG（Release notes 在 GitHub Release 页内） | 可选；Release 页已承载 | 可由 Release notes 派生（P2） |
| GAP-08 | P2 | 展示 | 无 demo GIF（可选） | 可选增强 | FINAL UI 后可选 |
| GAP-09 | P2 | 展示 | 无 .github（CI/issue 模板/social preview） | 可选；无 CI 为已知取舍 | ROADMAP |
| GAP-10 | P2 | 文档 | docs/07、docs/11 为历史时点快照（23/24 目标、"无写能力"），未加 superseding note | 历史档案本身合法，但公开读者可能混淆 | P2：加"历史时点"批注 |

## 4. Documentation Audit（冲突清单，事实）

```text
C1  02_ARCHITECTURE.md："Serial 永远只发 FC03 读" × M10 后事实（FC06/0x10 主动写）→ CONFLICT（GAP-03）
C2  04_TEST_STRATEGY.md："ctest 24/24"、"无 FC06 encoder"grep 锚 × 现实 70 目标 + FC06/FC16 encoder → STALE（GAP-04）
C3  05_DEMO_GUIDE.md §1/§3："ctest 23/23" × 现实 70 → STALE（GAP-04）
C4  07/11 复盘：23/24 目标、"无写能力" → 历史时点快照（自我声明"撰写时"）→ 保留 + P2 批注
C5  README：已于 CLOSE-R1 修正 → VERIFIED（无残余冲突；Serial 行、ctest、Limitations 已对齐）
C6  PROJECT_STATUS/BACKLOG：与 §116–§119 一致 → 无冲突
C7  版本号：CMake 2.0.0 = tag v2.0.0 = Release 页 → 无冲突
C8  benchmark：README 引用 docs/10 canonical 数字一致 → 无冲突
C9  AI/Agent 边界描述：README/02/INTERVIEW_NOTES 一致（Agent 只读；主动写不经 AI）→ 无冲突
C10 模式描述：README 三模式表与 05_DEMO_GUIDE §2 一致 → 无冲突
```

## 5. UI Release Audit 输入（下一阶段范围，RUI-xx；本轮不修改 UI）

```text
RUI-01 Device Profile 工作区整页视觉审计（唯一没有专属视觉验收轮的工作区）
RUI-02 Manual Q&A 面板：打开/收起/滚动/空态/错误态（UX-R1/R2 已验证功能，视觉细节未审）
RUI-03 手册导入 + AI 提取候选卡片（Accept/Edit/Discard 三态）视觉一致性
RUI-04 应用图标 + 任务栏图标 + 窗口标题（发布包内实际显示；M9-E 之后未复验）
RUI-05 标题/头部区与 v2.0.0 版本呈现一致性
RUI-06 长文本边界：事务列表长 issue、手册引用长文本、答案长文本（非 Q&A 页）
RUI-07 空状态：Serial 空态（T013 历史 FAIL 项）、Replay 空态、Diagnosis 空态复验
RUI-08 success/warning/error 语义色一致性（result status 卡/事务行）
RUI-09 1024x720 + 1000x700 双尺寸全 6 工作区回归（M12 后未做过全页回归）
RUI-10 设计系统一致性（DS token：typography/spacing/button hierarchy 在 M12 新增面上的回归）
```

## 6. Final GitHub Screenshot Contract（本轮制定；**不生成**）

### 6.1 Required Final Screenshot Set（≤5 张，`docs/assets/screenshots/`）

| # | 文件 | 页面 | 核心内容 |
| --- | --- | --- | --- |
| 01 | 01-hero-dashboard.png | Dashboard/Overview | Simulator 黄金批次统计卡（observed/completed/成功率/延迟分布），最终 accepted UI |
| 02 | 02-transactions.png | Transactions | 选中事务 + 7 状态与正交 Issue 明细 |
| 03 | 03-replay-analysis.png | Replay | demo_v1.mlog 加载后的统计与逐记录事实 |
| 04 | 04-diagnosis-agent.png | Diagnosis（Agent tab） | 只读 Agent 问答（确定性事实之上）或 Baseline 诊断 |
| 05 | 05-serial.png（可选，视最终价值） | Communication/Serial | FC03/写确认 UI 真实路径 |

### 6.2 Capture Recipes

```text
通用 Runtime：accepted release/RC build（UI FREEZE 后的构建）
窗口尺寸：UI acceptance 已验证尺寸（1024x720 主 / 1000x700 回归）
平台：windows（真实 GUI 会话；offscreen 渲染无字体，禁止用于 final shots）
通用红线：无 API key/token、无用户私人路径、无 IDE/ZCode 界面、
  不伪造数据、无调试残留、无阻塞对话框

01-hero-dashboard.png
  Runtime: Simulator
  Prerequisite: 干净会话（clear previous session）
  Action: 运行 canonical deterministic demo batch（黄金 4 笔）
  Expected: observed=4/成功率/延迟统计卡填充；导航/头部为最终 accepted 态
  Page: Dashboard；File: docs/assets/screenshots/01-hero-dashboard.png

02-transactions.png
  Runtime: Simulator（同一批次）
  Action: 打开 Transactions，选中一笔 Exception 0x02 事务
  Expected: 事务行选中态 + 正交 Issue 明细 + 诊断提示可见
  Page: Transactions；File: 02-transactions.png

03-replay-analysis.png
  Runtime: Replay
  Prerequisite: 加载 samples/demo_v1.mlog
  Expected: 统计与黄金批次严格一致的口径呈现
  Page: Replay；File: 03-replay-analysis.png

04-diagnosis-agent.png
  Runtime: Simulator + provider（可用时）
  Action: Agent tab 提问 → 只读工具回答；provider 不可用时改拍
          Baseline 诊断面板（同名文件，内容切换并在报告中注明）
  Page: Diagnosis；File: 04-diagnosis-agent.png

05-serial.png（可选）
  Runtime: Serial（真实/虚拟串口就绪时）
  Expected: 连接态 + FC03 结果或确认式写下发前快照
  Page: Communication/Serial；File: 05-serial.png
```

### 6.3 ZCode Screenshot Responsibility（UI FREEZE 任务内执行）

ZCode 拥有截图生成责任（**能力已验证**：M9-F F2 曾由 ZCode 以
PrintWindow + state-oracle harness 生成 15 张最终截图；本轮评估 = 可行，
**非 BLOCKED**）。要求：启动 accepted build（windows 平台）→ 按 Recipe
构造状态 → 保存至 `docs/assets/screenshots/` → 校验尺寸/可打开/内容 →
路径写入报告。禁止伪造；若环境失能 → 报告
`SCREENSHOT AUTOMATION BLOCKED` + 最小 Human fallback（Human 按 Recipe
手动截图）。

### 6.4 Freshness Rule

生成后若 navigation/layout/theme/数值/行为/icon/header/button/诊断呈现
任一变更 → 标记 **SCREENSHOT STALE**，README 不得继续引用旧图，须在
accepted build 上重生成。

## 7. GitHub Material Inventory

| 材料 | 状态 |
| --- | --- |
| README 首页 | AVAILABLE（本日事实修正版） |
| Hero/supporting screenshots | **NEEDS FINAL UI**（GAP-01/02） |
| architecture image | AVAILABLE 但 **STALE**（缺 Manual Intelligence 层；P2 重绘） |
| Release notes | AVAILABLE（GitHub Release 页 + relr5f 文件） |
| CHANGELOG | MISSING（P2/GAP-07） |
| Quick Start | AVAILABLE（README Build & Run） |
| Known Limitations | AVAILABLE（README + T027 §119.18） |
| LICENSE | **MISSING**（GAP-05，Human 法律决策） |
| repo description/topics/social preview | **UNVERIFIED**（需网页端；GAP-06） |
| demo GIF | MISSING（可选，P2） |

## 8. Release Index（本轮汇总）

```text
Current HEAD        = 2f53197cc149bbf75e876b3b94234a2f934a8048
Current version     = 2.0.0（CMake 单源；tag v2.0.0 已发布）
Build evidence      = 474/474 targets RC0（REL-R3 worktree 树）
Test evidence       = 70/70 PASS（×3 轮 fresh 树：REL-R2/R3/closure 前）
UI evidence         = 功能级 VERIFIED（H1–H8/UX-R1/R2）；视觉级 STALE（GAP-01）
Package evidence    = v2.0.0 PUBLIC + R5G 逐字节验证
Documentation ev.   = VERIFIED（本日更新）；遗留 GAP-03/04/10
Screenshot ev.      = STALE（GAP-01）；Contract 已定（§6）
GitHub material     = 见 §7
P0 list             = 无
P1 list             = GAP-01..06
P2 list             = GAP-07..10 + RUI 未审项 + 既有 DEFERRED（T027 §119.18）
NEXT REQUIRED PHASE = HUMAN REVIEW OF AUDIT → UI RELEASE AUDIT（RUI-01..10）
                      → 修复 → Manual/Automated Visual Acceptance → UI FREEZE
                      → ZCODE FINAL SCREENSHOTS → README 引用
```

## 15. 本任务动作边界声明

本轮零产品/UI/打包改动；仅新增本文档与状态记录（PROJECT_STATUS/BACKLOG/devlog）。审计发现只登记，修复全部留给后续阶段。
