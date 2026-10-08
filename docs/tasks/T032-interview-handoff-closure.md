# T032 — Interview & Handoff Closure（分阶段）

- **Goal**：把 ModbusLens 从项目启动到 T031 的**真实开发历史**从 Git 与 canonical 文档中还原出来，
  形成后续秋招面试、项目复盘与 Agent 移交共用的**唯一事实底稿**。
  本项目已有大量面试素材（`docs/INTERVIEW_NOTES.md`、`docs/11_PROJECT_FINAL_RETROSPECTIVE.md`、
  `docs/12_RESUME_INTERVIEW_QA.md`、`docs/07_FINAL_PROJECT_REVIEW.md`、`docs/08_KNOWLEDGE_OWNERSHIP.md`），
  但其中相当一部分是**历史时点快照**，数字与能力边界与 v2.0.0 最终状态不一致（见 Phase A 的 CONFLICT 清单）。
  本任务的目的是先生成一份**逐条可追溯、显式标注冲突**的事实底稿，再谈表达。
- **Background**：T031-C 收口后，PROJECT_STATUS 记录的 NEXT = HUMAN PUBLICATION ACTIONS → T032 INTERVIEW KNOWLEDGE CLOSURE。
  本轮为 T032 的第一个阶段（Phase A）。
- **Mode**：STRICT DOCS / FACT EXTRACTION ONLY。零产品/UI/测试/打包改动；不重新构建、不重新验收、不重开 T001~T031。
- **Baseline（本轮 preflight 实测）**：
  ```text
  repo      = E:/desktop/ModbusLens
  branch    = main
  HEAD      = 69a58aedf059794b4485dc897587b4c6962f8946
  commits   = 498（git rev-list --count HEAD）
  tags      = v1.0.0（object 2cee626b… → peeled ae067ab7…）
              v2.0.0（object bdabd6fd… → peeled b18e378f68a3fe19556d2562b03a3335ba086bf6）
  verified LKGC = 9c065f23a7f06beb73beedb680a3fe3b03e4c596（存在且在产品历史内）
  worktree  = tracked/cached diff EMPTY；仅两个 untracked 脚手架文件（_ctx.py / _dump.py，非本任务产物，未提交）
  git diff --check = PASS
  ```
  仓库真实状态与任务基线**一致**，未触发 BASELINE MISMATCH。

## Phase A — Verified Project History / Fact Extraction（本轮）

### A.0 Scope

只做四件事：读取仓库证据 → 事实提取 → 历史整理 → docs-only 归档。
明确**不做**：不写 2 分钟 / 5 分钟项目介绍，不写简历 bullet，不写背诵稿，不写模拟面试题——
那些属于 T032 后续阶段（须经 Human 对 Phase A 事实底稿的 review 之后再启动）。

### A.1 交付物（新增 `docs/interview/`）

| 文件 | 内容 |
| --- | --- |
| `docs/interview/00_FACT_INDEX.md` | 事实索引：用途、证据分级规则（VERIFIED/CONFLICT/UNVERIFIED）、事实计数、冲突与未核实清单、参考基线快照 |
| `docs/interview/01_PROJECT_TIMELINE.md` | **14 个工程阶段**（P1–P14）的真实历史重建 + 阶段间因果链 |
| `docs/interview/02_TECH_STACK_MAP.md` | 实际使用技术的逐项核验（含替代方案、当前限制、源码锚点） |
| `docs/interview/03_ARCHITECTURE_DECISIONS.md` | **20 个架构决策**（含演化历史，不把后期修正写成 Day-1 完美设计） |
| `docs/interview/04_BUG_RCA_CATALOG.md` | 真实缺陷 / RCA 目录（Issue ×20 + 任务档案内 RCA ×12 = **32 条**）+ 分档 + **最强五个** |
| `docs/interview/05_CAPABILITIES_AND_LIMITATIONS.md` | 能力与边界的五档分类 + **必答十项边界判定** + 不成立的夸大表述自查 |
| `docs/interview/06_SOURCE_TRACEABILITY.md` | 引用完整性核验：commit / Task / Issue / ADR / 资源 / 阅读范围诚实声明 |

> **行内标记约定**：Fact Pack 中每条陈述以 `[V-nnn]` / `[C-nn]` / `[U-nn]` 结尾，
> 使计数可用 `grep` 复现，而不是由 Agent 声称。

### A.2 证据规则（本轮强制）

1. 每条重要事实必须至少可追溯到一处仓库来源（任务文档 / Issue / ADR / devlog / commit SHA / 源文件 / 测试证据 / PROJECT_STATUS / BACKLOG / 发布证据）。
2. **不得**作为证据：Agent 记忆、旧聊天上下文、假设、文件名猜测、通用技术常识。
3. 事实分三档：**VERIFIED** / **CONFLICT** / **UNVERIFIED**。
4. 两处历史记录冲突时 → 标 CONFLICT 并**同时列出两处来源**，不静默择一。
5. 无法核实的主张 → 标 UNVERIFIED，**不得**改写成肯定句。

### A.3 事实计数（本轮实测，`grep` 可复现）

```bash
cd docs/interview
grep -ohE "\[V-[0-9]+\]" *.md | sort -u | wc -l    # → 557
grep -ohE "\[C-[0-9]+\]" *.md | sort -u | wc -l    # →   5
grep -ohE "\[U-[0-9]+\]" *.md | sort -u | wc -l    # →  10
```

```text
VERIFIED   = 557
CONFLICT   =   5
UNVERIFIED =  10
```

> 计数口径与逐条来源见 `docs/interview/00_FACT_INDEX.md` §4（含各文件分布）与
> `06_SOURCE_TRACEABILITY.md` §1/§2。
> **口径披露**：`[V-nnn]` 计的是"可追溯陈述单元"，**不是"互不相同的事实"数**；
> 后者由冲突清单（5 条）与未核实清单（10 条）反映——**真正需要 Human 裁定的分歧点很集中**。
> CONFLICT 中「历史快照型」与「定义差异型」占多数，真正的**文档事实错误**为两条（见 A.4）。

### A.4 本轮发现的两条真实文档事实错误（非历史快照可解释）

1. **`README.md` §11「无 register map / 工程单位层」**——与 M11/M12 已交付能力冲突：
   `src/core/analysis/RegisterDecode.h` 实现 7 种解码类型（Hex/Binary/UInt16/Int16/UInt32/Int32/Float32）+ 字节序/字序；
   `src/core/profile/DeviceProfile.h` 实现 `RegisterEntry{scale, offset, unit}` 与冻结公式 `decodedValue * scale + offset`。
   `INTERVIEW_NOTES.md` F8 有同一表述。**README 自身也不自洽**（§2 已提到设备档案）。
   **保留的真实部分**是：0x02 异常码的诊断路径仍只建议"核对地址映射"，不指认具体寄存器语义。
   → `docs/interview/00_FACT_INDEX.md` `[C-03]`
2. **`docs/tasks/T028-release-prep-audit.md` §2E「ADR 2 份」**——该提交（`2f53197`）上 `docs/adr/`
   实际有 3 份 ADR（`git ls-tree -r --name-only 2f53197 -- docs/adr` 实测），
   ADR-003 自 `f649270` 起就在库内，**早于 T028**（同一处 `docs/INTERVIEW_NOTES.md` F10 亦有相同表述）。

另有三条**属历史快照或定义差异**，不是错误但必须统一对外口径：
`[C-01]` 项目时间范围（`docs/11`/`docs/12` 写 09-05→09-14 vs Git 实测 09-05→10-08）；
`[C-02]` `IFrameSource` 设计文档 vs 代码（0 命中、`src/io`/`src/app` 不存在）；
`[C-04]` `docs/09` 的"六状态"vs ADR-003 与现状的"七状态"（同日时序差）；
`[C-05]` 历史数字（24→70 CTest / 2→3 ADR / 25→30 任务档案）。

全部冲突按只增不改原则**不修改既有档案**，只在本 Fact Pack 中标注，供 Human 决定修正归属。

### A.5 验证

```text
git diff --name-only                          → 仅允许清单内的 docs/ 文件
git diff --name-only HEAD -- src tests cmake scripts samples CMakeLists.txt
                                              → EMPTY（实测）
git diff --name-only HEAD -- docs/assets      → EMPTY（截图资产未动，实测）
git diff --check                              → PASS
```

- 本轮为 docs-only，**不重跑**完整产品构建 / CTest 套件（Task 指令 §13 明确豁免）。
- 引用的 commit / Task / Issue / ADR / 链接目标逐条实测存在性，结果见 `06_SOURCE_TRACEABILITY.md`。

## Files Changed

- 新增：`docs/interview/00_FACT_INDEX.md`、`01_PROJECT_TIMELINE.md`、`02_TECH_STACK_MAP.md`、
  `03_ARCHITECTURE_DECISIONS.md`、`04_BUG_RCA_CATALOG.md`、`05_CAPABILITIES_AND_LIMITATIONS.md`、
  `06_SOURCE_TRACEABILITY.md`
- 新增：本文件（`docs/tasks/T032-interview-handoff-closure.md`）
- 更新：`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、`docs/devlog/2026-10-08.md`

## Problems Encountered

- 任务档案体量极端不均：`T027` = 708 KB / 12110 行，`T022` = 776 KB / 11593 行，`T017`/`T019`/`T021` 各 150–310 KB。
  无法全文顺序阅读，改用「小节标题结构 + 关键章节定点读取 + 源码/测试交叉核验」策略，并在 `06_SOURCE_TRACEABILITY.md`
  声明哪些结论来自定点读取而非全文通读。
- 并行的子代理批量提取首次调用被 provider 限流（6 个并发全部 `Provider rate limited`），改为本机直接读取，
  未因此降低证据标准。

## Solutions

- 对巨型档案先 `grep -nE "^#{2,3} "` 取结构，再按需 `Read offset/limit`；对能力类主张一律回到 `src/` 源码与 `CMakeLists.txt`
  实测（例如 Modbus TCP 用 `grep -rniE "mbap|modbus.?tcp" src/ tests/` → 0 命中；
  CTest 目标数用 `grep -cE "^\s*add_test\(" CMakeLists.txt` → 70）。
- 冲突一律并列呈现并给出源码级裁决依据，不做静默择一。

## Verification

见 §A.5。本轮全部自动化检查已在最终提交前实测执行，结果如实记录。

## Result

- T032 = **IN PROGRESS**；Phase A = **VERIFIED FACT EXTRACTION 交付**。
- **PRODUCT CODE = FROZEN / UNCHANGED；UI = FROZEN / UNCHANGED**；未 build / 未 repackage / 未 amend / 未 rebase / 未 push。
- **未推进 verified LKGC**（docs-only 提交永不作 LKGC）。

## Knowledge Learned

- **「文档写了」与「产品做了」是两件事，必须分开核验。** 本轮在 `README.md` §11 与 `INTERVIEW_NOTES.md` F8
  发现「无 register map / 工程单位层」这一在 v2.0.0 已不成立的表述——它曾经为真（T015 复盘时确实没有），
  但 M11 的 32-bit 解码与 M12 的设备档案 register map/scale/offset/unit 落地后没有人回头改它。
  通过这件事理解了**为什么「历史时点快照」必须显式标注**（T031-A 对 docs/07、docs/11 加了快照声明，
  但 README 的限制清单与 INTERVIEW_NOTES 的 F 段没有同等处理）。
- **同一个数字在两份文档里不同，不一定是有人写错。** 「13 vs 14 个 Issue」的差异完全由定义决定：
  `TransactionIssueCode` 有 10 个枚举成员，其中 `UnknownProtocolError` 是防御哨兵，
  业务口径 9 个 + 请求侧 4 个 = 13；枚举口径 10 + 4 = 14。两条都「对」，但对外必须选一条口径并说清。
  这是通过直接读 `src/core/analysis/TransactionAnalysis.h` 的枚举体确认的。
- **等距地看历史，才能区分「设计」与「演化」。** 广播语义在 `docs/09` 写成「恰为六状态，无第七个」，
  同一天 ADR-003 批准新增第七状态 `ExpectedNoResponse`——两条记录都是真的，差的是几小时。
  通过这件事理解了「不把后期修正写成 Day-1 完美设计」在时间粒度上意味着什么。

## Potential Interview Questions

1. **你项目里有两个数字我都见过，到底是 13 还是 14 个 Issue？**
   → 取决于口径：枚举成员 10 + 4 = 14（含 1 个防御哨兵 `UnknownProtocolError`）；业务值 9 + 4 = 13。
   对外统一说 13 个业务 Issue，并说明哨兵不计入业务语义。
2. **你说「无 register map」，但仓库里有 Device Profile 的 register map 和 scale/offset，怎么解释？**
   → 诚实答：v1（T015 时点）确实没有；M11 加了 32-bit 解码、M12 加了设备档案的 register map 与
   scale/offset/unit。但这只作用于 **Read Result 的语义呈现**——诊断引擎不消费设备档案，
   异常码 0x02 仍不指认具体寄存器语义。此表述已在 **T032-A1** 修正进 README §11。
3. **怎么保证这份事实底稿不是又一次「AI 总结」？**
   → 每条事实带来源（file:line / commit SHA / 测试名），冲突并列不择一，无法核实的标 UNVERIFIED；
   `06_SOURCE_TRACEABILITY.md` 对全部引用做过存在性实测。

## Git Commit

- **Phase A 提交（已回填）**：
  ```text
  5f77870ff4aed9961bd532bde8e20f38801fed5a
  T032 A: extract verified project history for interview handoff
  docs-only，11 files changed, 3804 insertions(+)，0 deletions
  父提交 = 69a58aedf059794b4485dc897587b4c6962f8946（T031 C）
  ```
  > 回填说明：Phase A 当时的档案文字指向"PROJECT_STATUS 追加批注登记的哈希"，但受"仅一个 docs-only 提交 +
  > 禁止 amend"约束，该哈希当时**未写入任何文档**。本 Phase A1（明确为 backfill/correction 提交）闭合该缺口，
  > **未 amend `5f77870`**。`[V-563]`
- **Phase A1 提交（已回填）**：`8c57e10c38e31b04270649927c89c96cc72525c3`
  （详见文末 §Phase A1 → `Git Commit（A1）`）。
- NO AMEND / NO PUSH / NO TAG / NO RELEASE；**未推进 verified LKGC**（docs-only 提交永不作 LKGC）。

## Next gate（Phase A 时点，已被 A1 取代 —— 保留原文）

**HUMAN FACT REVIEW** —— 由 Human 对本 Fact Pack 的事实与冲突清单做裁定；
批准后才可启动 T032 Phase B（对外表达层：项目介绍 / 简历材料 / 问答稿）。
在此之前不得进入 Phase B。

---

# Phase A1 — Human Fact Review Corrections（2026-10-08，docs-only）

> **输入**：T032 Phase A 的 Fact Pack 已通过 Human Review **主体审查**，
> 但在进入 Phase B 前需闭合少量事实与追溯问题。
> **输出**：本 Phase A1 = **CORRECTIONS APPLIED**。**仍不得开始 Phase B。**

## A1.0 Scope 与硬边界

- **性质**：docs-only 修正/回填轮。**不生成**项目介绍、简历 bullet、背诵稿、面试回答模板或模拟题。
- **修正范围（任务指定的授权文件集，未越界）**：
  `README.md`（仅 §11 事实条目）、`docs/tasks/T032-*.md`、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、
  当前 devlog、**相关 `docs/interview/*`**。
- **未修改**（不在授权集内或属只增不改档案区）：`docs/INTERVIEW_NOTES.md`、`docs/tasks/T027`/`T028`、
  `docs/07`、`docs/09`、`docs/11`、`docs/11_V2_UPGRADE_PLAN`、`docs/12`、全部 `docs/issues/*`、`docs/adr/*`。`[V-564]`

## A1.1 先决核验：证明 **Phase A 提交确为 docs-only**（父提交对比）

> Phase A 当时的检查是"工作树 vs HEAD"，**不足以单独证明该提交是 docs-only**。本轮改用**父提交对比**：

```bash
git diff --name-only 5f77870^ 5f77870 -- src tests cmake scripts samples CMakeLists.txt   # → EMPTY
git diff --name-only 5f77870^ 5f77870 -- docs/assets                                      # → EMPTY
git show --name-only --format= 5f77870                                                     # → 11 个 docs 路径
```

- 父提交 = `69a58ae`（T031 C）。三项实测结果：产品路径 EMPTY、资产路径 EMPTY、
  改动清单 = 11 个全部位于 `docs/`（BACKLOG / PROJECT_STATUS / devlog / docs/interview ×7 / docs/tasks/T032）。
  ⇒ **Phase A 提交经父提交对比确认为 docs-only，无产品代码、测试、构建逻辑、脚本、样本、已验收截图或发布资产被改动**。`[V-565]`

## A1.2 C-03 修正结果（README §11 能力真相）

- **Human Review 裁定**：接受 C-03 为**真实的当前文档错误**。
- **修正**：`README.md` §11 原条目「**无 register map / 工程单位层**…」**改写为限制优先的准确陈述**：
  > **语义层只覆盖读数呈现，不进入诊断判定**：M11 提供多类型读数解码（Hex / Binary / UInt16 / Int16 /
  > UInt32 / Int32 / Float32，含寄存器内字节序与字序；raw 与解析结果分列保留）；M12 设备档案提供
  > register map（按读功能码划分寄存器空间）与**用户显式填写**的 scale / offset / unit。
  > 但二者只作用于 **Read Result 的语义呈现**：诊断引擎不消费设备档案，异常码 0x02 仍只给出
  > "非法数据地址"这一协议事实并建议核对地址映射，**不指认具体寄存器语义**，也不做量程 / 报警阈值判定；
  > 无设备模板库、无自动工程单位推断。
- **Doctor-side 新证据（本轮实测，支撑"诊断引擎不消费设备档案"）**：
  `grep -rn "DeviceProfile\|RegisterEntry\|profile" src/core/diagnosis/` → **0 命中**；
  `RuleBasedDiagnosis` 唯一入口为 `diagnoseTransactions(const DiagnosisContext&)`。`[V-566]`
- **红线遵守自检**：未夸大能力（未新增任何其他节的能力声明）；保留真实限制；
  区分了"设备档案/register map 支持"与"未支持的工业能力"；**未声称真机验证**；
  **未暗示 Modbus TCP**（§11 仍列其为未实现）；**未暗示 OCR**（README 全文无 OCR 声明）。`[V-567]`
- **C-03 记录保留情况**：**该冲突未从历史中消失**——`00_FACT_INDEX.md` §5 的 C-03 条目保留原始发现，
  并追加**完整处置轨迹**（A 轮发现 → Human Review 裁定 → A1 修正），原裁定建议文字一并保留。`[V-558]`

## A1.3 C-05 分类处置结果（**未做全局替换**）

- **当前实测真值**：CTest 目标 **70** / ADR 文件 **3** / 任务文档 **30**。
- **逐处分类**（A = 当时正确的历史快照 → 不动；B = 当时即为错误 → 追加勘误或保留于 Fact Pack；
  C = 当前权威文档但已过期 → 更新为实测现值）：
  - **A（不动）**：`docs/07`（写于 2026-09-11，当时 ADR 确为 2 份、ctest 确为 23）、
    `docs/11`（2026-09-14，ctest 24）、`docs/11_V2_UPGRADE_PLAN` §3（2026-09-14，"当前 24/24"）、
    `docs/PROJECT_STATUS.md` 历史追加块、`docs/BACKLOG.md` 时间线行。
    ⇒ **这些不是错误，是当时为真的快照**；`docs/07`/`docs/11` 顶部本已有 T031-A 加的历史快照声明。`[V-568]`
  - **B（保留于 Fact Pack）**：`docs/INTERVIEW_NOTES.md` F10 的"2 份 ADR"（该节写于 2026-10-07，
    当时 ADR 已为 3 份）与"25 份任务档案"（口径差异）；`docs/tasks/T028` §2E 的"ADR 2 份"
    （该 HEAD `2f53197` 上 `git ls-tree` 实测已有 3 份 ADR）。
    ⇒ **两处均不在 A1 授权文件集内 / 属只增不改档案区**，按任务 §3 允许的方式**保留于 Fact Pack**，未回写。`[V-569]`
  - **C（未修改）**：`docs/12_RESUME_INTERVIEW_QA.md` 的"24 个 CTest 目标"（文件头部自声明 v1 / 2026-09-14，
    但在用；属"当前在用但已过期"）。**不在授权集内**，且改写其"简历定稿"属**内容决策**（Phase B / Human 授权）。`[V-570]`
- **结论**：授权集内**不存在**需要数字更新的 C 类实例（README §9 已是 70/70、`docs/04` CURRENT STATE 已是 70、
  `docs/05` 已是 70/70、`CHANGELOG` "490+ commits" 与实测 498 相容）⇒ **本轮零全局替换**。`[V-559]`

## A1.4 远端 / 发布证据等级澄清（**未升级也未降级**）

- **问题**：Fact Pack 中"v2.0.0 已公开"类措辞可能被读作**当前远端状态已复核**。
- **澄清后的精确区分**（写入 `docs/interview/05` §A12 与 `06` §8）：
  - **VERIFIED（仓库事实）**：仓库档案与 Git 对象记录了 v2.0.0 发布/公开流程**已完成**，
    并保留本地 release/tag 证据（tag 对象 `bdabd6fd…` → `b18e378f…`、资产 35,814,735 B 与 SHA-256、
    R5G 验证记录、H1–H8 人工验收记录）。
  - **UNVERIFIED（当前远端事实）**：**当前** GitHub 页面状态、远端 tag ref、Social Preview / Description /
    Topics **在 T032-A / A1 期间均未实时核验**（离线环境无法判定）⇒ `[U-10]`。
- **双向红线**：不把"仓库记录了发布完成"**升级**为"当前远端状态已复核"；
  也不把真实历史留档**降级**为"未发布"。`[V-562]`

## A1.5 追溯缺口闭合（Phase A 提交哈希回填）

- **缺口**：Phase A 档案写"见 PROJECT_STATUS 追加批注登记的真实哈希"，而该哈希实际未写入任何文档。
- **闭合**：本 §Git Commit 已回填
  `5f77870ff4aed9961bd532bde8e20f38801fed5a`（`T032 A: extract verified project history for interview handoff`，
  父提交 `69a58ae`，11 files / +3804 / −0）。**未 amend Phase A 提交**；
  本 Phase A1 提交即为任务要求的**显式 backfill/correction 提交**。`[V-563]`

## A1.6 Validation（A1 本轮实测）

```text
git diff --check                                        → PASS
git diff --name-only -- src tests cmake scripts samples CMakeLists.txt → EMPTY
git diff --name-only -- docs/assets                     → EMPTY
README 变更范围                                          → 仅 §11 一条已知限制条目（无无关改版）
Fact Pack 引用校验                                       → 见 §A1.7
```

## A1.7 事实计数（A1 后重算，`grep` 可复现）

```bash
cd docs/interview
grep -ohE "\[V-[0-9]+\]" *.md | sort -u | wc -l    # → 563
grep -ohE "\[C-[0-9]+\]" *.md | sort -u | wc -l    # →   5
grep -ohE "\[U-[0-9]+\]" *.md | sort -u | wc -l    # →  11
```

```text
               Phase A (5f77870)   Phase A1 (本轮)
VERIFIED             557        →        563
CONFLICT               5        →          5   （C-03 已修正，但记录保留并追加处置轨迹）
UNVERIFIED            10        →         11   （新增 [U-11]：INTERVIEW_NOTES F8 未在本轮授权集内）
```

- **口径提醒**：563 是**唯一标记数**；**逐文件之和 = 567**，差额 4 来自
  `[V-558]`/`[V-559]`/`[V-560]`（见 00 与 05）与 `[V-561]`（见 05 与 06）这 4 个跨文件复用的标记。
  对外引用请用 **563**。`[V-571]`
- CONFLICT **数量未变（仍 5）**是刻意的：C-03 的**修正**不删除冲突记录——
  它从"未处置的当前文档错误"变为"已发现 → 已裁定的冲突"，**发现与修正轨迹双双保留**。`[V-558]`

## Phase A1 状态收口

```text
T032                                  = IN PROGRESS
Phase A  VERIFIED FACT EXTRACTION      = COMPLETE
Human Fact Review                      = CONDITIONAL PASS → CORRECTIONS APPLIED
Phase B                                = NOT STARTED
PRODUCT CODE                           = FROZEN / UNCHANGED
UI                                     = FROZEN / UNCHANGED（UI FREEZE = ACTIVE）
TAG / RELEASE                          = UNCHANGED（v2.0.0 资产未动）
PUSH                                   = NONE
verified LKGC                          = 未推进
NEXT                                   = HUMAN REVIEW OF T032-A1 → 之后才谈 Phase B
```

## Git Commit（A1）

- **Phase A1 提交（已回填，2026-10-08 Phase B 轮内完成）**：
  ```text
  8c57e10c38e31b04270649927c89c96cc72525c3
  T032 A1: apply human fact review corrections
  docs-only，8 files changed, 406 insertions(+), 53 deletions(-)
  父提交 = 5f77870ff4aed9961bd532bde8e20f38801fed5a（T032 A）
  ```
  > 回填说明：A1 当时的档案文字指向 PROJECT_STATUS/BACKLOG 登记的哈希，但受"仅一个新提交 +
  > 禁止 amend"约束，该哈希当时**未写入任何文档**。Phase B 轮按任务 §1 的追溯规则，
  > **在更新 canonical T032 状态文档的同时**顺带回填本哈希——**未为此另开 A2 提交**。`[V-572]`
- NO AMEND（未 amend `5f77870`，也未 amend `8c57e10`）/ NO PUSH / NO TAG / NO RELEASE；**未推进 verified LKGC**。

---

# Phase B — Evidence-Grounded Interview Narrative & Knowledge Ownership（2026-10-08，docs-only）

> **输入**：Phase A = COMPLETE / HUMAN ACCEPTED；A1 = HUMAN ACCEPTED；Fact Pack = HUMAN ACCEPTED。
> **输出**：四份 Phase B 材料 + 状态收口。**Phase C 未开始。**

## B.0 Scope 与硬边界

- **性质**：docs-only。构建**准确叙事 + 简历表述 + 知识所有权地基**。
- **交付**：`docs/interview/07_PROJECT_INTRO_2MIN.md`、`08_TECHNICAL_DEEP_DIVE_5MIN.md`、
  `09_RESUME_PROJECT_ENTRY.md`、`10_KNOWLEDGE_OWNERSHIP_MAP.md`。**未覆盖 Phase A Fact Pack。**`[V-573]`
- **明确不做（属 Phase C，须经 Human review 后授权）**：大型问答题库、模拟面试记录、
  压力题演练、背诵卡、100 题清单、HR 问答库。`[V-574]`
- **未修改**：`src/**`、`tests/**`、`cmake/**`、`scripts/**`、`samples/**`、生产 QML、`CMakeLists.txt`、
  `docs/assets/**`、已验收截图、release 资产、tag、远端状态；未 build / 未 repackage。

## B.1 事实使用纪律（本轮执行）

- **唯一事实来源** = 已验收 Fact Pack（`00`–`06`）；**不从记忆重建、不美化历史、不补造实现细节**。`[V-575]`
- **VERIFIED** → 可作为正向主张；**历史快照** → 只能作为历史状态引用；
  **UNVERIFIED** → **不得**成为正向简历/面试主张；**已裁定的冲突** → 用 Human review 后的当前口径，
  必要时保留历史语境。`[V-576]`
- **标记不漂移**：Phase B 四份文档只**引用** Fact Pack 既有标记，**不新增** `[V-]` 标记——
  以保持已验收计数 **563 / 5 / 11** 继续有效。执行中曾误引 `[V-566]`（该标记原只存在于任务档案，
  不在 `docs/interview/` 计数范围内），已改为引用 pack 内的等价标记 `[V-558]`，
  复算后计数**仍为 563 / 5 / 11**。`[V-577]`

## B.2 四份材料的结构与本轮自我检查

**`07_PROJECT_INTRO_2MIN.md`** — A 口述版（约 480–540 字，**从问题起步，不从技术栈起步**）+
B 逐条证据注解（19 条）+ B.1「刻意不出现的内容及原因」（8 项）+ B.2「被追问时指向何处」。
自检：口述全文**未出现** Modbus TCP / installer / 代码签名 / OCR / 当前远端状态 / Agent 写权限；
数字只用现状真值；限制被**主动说出**而非等待追问。`[V-578]`

**`08_TECHNICAL_DEEP_DIVE_5MIN.md`** — 带时间分配的 5 分钟口述骨架 + 七段详细版本
（数据路径 → 确定性边界 → 事务两轴 → Qt 边界 → 两条权限边界 → 验证体系 → 三个 RCA）+
Evidence Notes。**三条 RCA（ISSUE-002 / ISSUE-004 / ISSUE-005）严格按档案复述**
symptom → 定位 → 根因 → 修复 → 验证，含各自的"证据边界"（如 ISSUE-005 的"精确 elapsedMs 未实测"）。`[V-579]`

**`09_RESUME_PROJECT_ENTRY.md`** — 一句话定位 + **3 条 bullet + 可选第 4 条**（第 4 条维度不同：
验证与发布工程，且在注解中说明"是否采用取决于版面"）+ 实际使用的技术关键词 +
**禁止表述清单（12 项，逐项给出可写替代）** + 口径注解。`[V-580]`

**`10_KNOWLEDGE_OWNERSHIP_MAP.md`** — 三档所有权（A: 24 个 MUST 主题，逐条 9 字段；
B: 14 个 SHOULD 主题；C: 16 项 ROADMAP/DO NOT CLAIM）+ 白板复现清单（12 项）+
**最容易讲错的 10 条自校准清单**。`[V-581]`

## B.3 三条 A1 守卫在本轮的落实

| 守卫 | 落实方式 | 证据 |
| --- | --- | --- |
| **C-03**（设备档案能力口径） | 四份文档一律表述为"**语义层只覆盖读数呈现、不进入诊断判定**"，并明写**诊断引擎不消费设备档案**（源码实测 0 命中）；`10` K-20 专设一条 MUST 主题 | `[C-03]` `[V-558]` |
| **C-05**（数字口径） | 只用现状真值 **70 / 3 / 30**；`09` 把"24 个测试目标"列入**禁止表述**；`08`/`10` 引用历史值时**就地注明是快照** | `[C-05]` `[V-559]` `[V-560]` |
| **远端/发布** | 一律表述为"仓库留档的 VERIFIED 事实"；**当前远端状态**在 `09` 的禁止表述清单中明确禁止；`07` 注解第 15 条写明不得升级为"我刚确认过线上" | `[V-562]` `[U-10]` |
| **`[U-11]`** | `07` B.1 明确"不得作为正向或负向口径使用"；`10` C-15 列入 DO NOT CLAIM | `[U-11]` |

## B.4 与 Fact Pack 的关系

- **未修改** `00`–`06`（Phase B 只新增 `07`–`10`）。`[V-582]`
- **一处已知待整理项（如实登记）**：`05_CAPABILITIES_AND_LIMITATIONS.md` §1 第 10 行仍写
  "**与 `README.md` §11 冲突，见 `[C-03]`**"——该冲突**已由 A1 修正**（C-03 已 RESOLVED IN A1），
  因此该单元格的措辞已过时。本轮**未编辑**（Phase B 不得改写已验收 Fact Pack）；
  已正确口径由 `05` §8 的 C-03 处置轨迹承载。**属待 Human 授权的小幅一致性修整**
  （本轮**不**新增 Fact Pack 级 UNVERIFIED 编号；该项登记于本任务档案）。`[V-583]`

## Phase B 状态收口

```text
T032                       = IN PROGRESS
Phase A                    = COMPLETE / HUMAN ACCEPTED
A1                         = COMPLETE / HUMAN ACCEPTED
Phase B                    = COMPLETE / PENDING HUMAN REVIEW
Phase C                    = NOT STARTED
PRODUCT CODE               = FROZEN / UNCHANGED
UI                         = FROZEN / UNCHANGED（UI FREEZE = ACTIVE）
TAG / RELEASE              = UNCHANGED（v2.0.0 资产未动）
PUSH                       = NONE
verified LKGC              = 未推进（docs-only 提交永不作 LKGC）
Fact Pack 计数              = 563 / 5 / 11（Phase B 未改变）
NEXT                       = HUMAN REVIEW OF T032-B → 之后才谈 Phase C
```

## Git Commit（B）

- 本轮提交哈希见最终报告；**本阶段不预测、不预写自身 SHA**（任务 §1 追溯规则）。
- NO AMEND / NO PUSH / NO TAG / NO RELEASE；**未推进 verified LKGC**。