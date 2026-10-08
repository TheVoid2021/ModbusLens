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
   scale/offset/unit。仍然为真的部分是**异常码 0x02 的诊断路径不指认具体寄存器语义**。
   这条差异已记入 T032 的 CONFLICT 清单，README 的已知限制需要修正（属后续 Human 授权范围）。
3. **怎么保证这份事实底稿不是又一次「AI 总结」？**
   → 每条事实带来源（file:line / commit SHA / 测试名），冲突并列不择一，无法核实的标 UNVERIFIED；
   `06_SOURCE_TRACEABILITY.md` 对全部引用做过存在性实测。

## Git Commit

- 本轮提交：`T032 A: extract verified project history for interview handoff`（docs-only，见 PROJECT_STATUS 追加批注登记的真实哈希）。
- NO AMEND / NO PUSH / NO TAG / NO RELEASE；未推进 verified LKGC。

## Next gate

**HUMAN FACT REVIEW** —— 由 Human 对本 Fact Pack 的事实与冲突清单做裁定；
批准后才可启动 T032 Phase B（对外表达层：项目介绍 / 简历材料 / 问答稿）。
在此之前不得进入 Phase B。