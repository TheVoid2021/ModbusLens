# 00 — FACT INDEX（T032 Phase A 事实索引）

> **本目录是 T032 Phase A 的交付物**：ModbusLens 从项目启动到 T031 的**唯一事实底稿**。
> 它是**证据材料**，不是话术——不包含 2 分钟 / 5 分钟项目介绍、简历 bullet、背诵稿或模拟面试题
> （那些属于 T032 后续阶段，须经 Human 对 Phase A 的 review 之后才启动）。
> 证据分级：`[V-nnn]` = VERIFIED；`[C-nn]` = CONFLICT；`[U-nn]` = UNVERIFIED。

---

## 1. 这个目录解决什么问题

项目**已有**大量面试素材（`docs/INTERVIEW_NOTES.md`、`docs/07`、`docs/11`、`docs/12`、`docs/08`），
但它们有各自的**撰写时点**，其中若干是**历史快照**——数字与能力边界与 v2.0.0 最终状态不一致，
且部分快照**没有加 superseding 声明**（T031-A 只给 `docs/07` / `docs/11` 加了）。

因此本目录的目标是：
1. 从 **Git 历史 + canonical 文档 + 源码 + 测试 + CMake** 重新提取事实，
   **不依赖任何 Agent 记忆或旧聊天上下文**；`[V-537]`
2. 对冲突处**并列呈现两个来源**并给出可裁决的源码级依据，**不静默择一**；`[V-538]`
3. 对无法核实的主张标 **UNVERIFIED**，**不改写成肯定句**；`[V-539]`
4. 公开**本轮实际读了什么、没读什么**，避免把定点读取伪装成全文通读。`[V-540]`

---

## 2. 文件地图

| 文件 | 内容 | 面试用途 |
| --- | --- | --- |
| `00_FACT_INDEX.md`（本文件） | 索引 + 证据规则 + 事实计数 + 冲突与未核实清单 | 先看这里；确认口径是否可辩护 |
| `01_PROJECT_TIMELINE.md` | **14 个工程阶段**（P1–P14）的真实历史重建 + 阶段间因果链 | "项目怎么演进的 / 每个阶段解决了什么" |
| `02_TECH_STACK_MAP.md` | 实际使用技术的逐项核验（含替代方案、当前限制、源码锚点） | "你用了什么技术栈 / 为什么选它" |
| `03_ARCHITECTURE_DECISIONS.md` | **20 个架构决策** + 演化历史（不把后期修正写成 Day-1 设计） | "为什么这么设计 / 备选是什么 / 代价是什么" |
| `04_BUG_RCA_CATALOG.md` | **32 条 RCA**（Issue ×20 + 任务档案内 ×12）+ 分档 + **最强五个** | "遇到最难的问题 / 怎么定位" |
| `05_CAPABILITIES_AND_LIMITATIONS.md` | 能力五档分类 + **必答十项边界判定** + 不成立的夸大表述自查 | "这个项目到底能做什么" |
| `06_SOURCE_TRACEABILITY.md` | 引用完整性核验（commit / Task / Issue / ADR / 资源 / 阅读范围声明） | 被追问"你怎么证明"时指向这里 |

---

## 3. 证据规则（本轮强制，可自查）

1. **每条重要事实至少一处仓库来源**：任务文档 / Issue / ADR / devlog / commit SHA / 源文件 /
   测试证据 / PROJECT_STATUS / BACKLOG / 发布证据。`[V-541]`
2. **不得作为证据**：Agent 记忆、旧聊天记忆、假设、文件名猜测、通用技术常识。`[V-542]`
3. **三档分类**：VERIFIED / CONFLICT / UNVERIFIED。`[V-543]`
4. **冲突不择一**：标明两处来源，必要时给出源码级裁决依据。`[V-544]`
5. **无法核实不改写**：保持 UNVERIFIED 并写明为什么。`[V-545]`
6. **行内标记可机器复现**：每个事实以 `[V-nnn]` / `[C-nn]` / `[U-nn]` 结尾；
   计数用 `grep -ohE "\[V-[0-9]+\]" docs/interview/*.md | sort -u | wc -l`。`[V-546]`

---

## 4. 事实计数（本轮实测）

```bash
cd docs/interview
grep -ohE "\[V-[0-9]+\]" *.md | sort -u | wc -l    # → 557
grep -ohE "\[C-[0-9]+\]" *.md | sort -u | wc -l    # →   5
grep -ohE "\[U-[0-9]+\]" *.md | sort -u | wc -l    # →  10
```

| 档 | 计数 | 含义 |
| --- | --- | --- |
| **VERIFIED** | **557** | 有明确仓库来源且本轮实测核验（含源码 / CMake / git / 测试级回源） |
| **CONFLICT** | **5** | 两处来源互相冲突，已并列呈现（见 §5） |
| **UNVERIFIED** | **10** | 无法核实或本轮未覆盖，保持未断言（见 §6） |

`[V-547]`

**各文件 VERIFIED 分布**（`grep -ohE '\[V-[0-9]+\]' <file> | sort -u | wc -l`）：

| 文件 | V 计数 |
| --- | --- |
| `00_FACT_INDEX.md` | 21 |
| `01_PROJECT_TIMELINE.md` | 118 |
| `02_TECH_STACK_MAP.md` | 76 |
| `03_ARCHITECTURE_DECISIONS.md` | 68 |
| `04_BUG_RCA_CATALOG.md` | 37 |
| `05_CAPABILITIES_AND_LIMITATIONS.md` | 101 |
| `06_SOURCE_TRACEABILITY.md` | 136 |

`[V-557]`

> **计数口径说明（诚实披露）**：`[V-nnn]` 标记的是**陈述单元**（一条事实 / 一个阶段字段 / 一行技术条目 / 一次核验结果），
> **不是"独立知识点"数**。同一条事实若在多个文件中以不同角度复述，会带各自的标记。
> 因此 **557 是"可追溯陈述"的数量，不是"互不相同的事实"的数量**；后者会显著更小——
> 从本文档的冲突清单（5 条）与未核实清单（10 条）可见，**真正需要人来裁定的分歧点很集中**。`[V-548]`

---

## 5. CONFLICT 清单（5 条，全部并列来源）

### `[C-01]` 项目时间范围
| 来源 | 主张 |
| --- | --- |
| `docs/11_PROJECT_FINAL_RETROSPECTIVE.md` §2 / §11 | 项目时间 = **2026-09-05 → 2026-09-14**；并声明"对外材料中的时间与数字必须与本文件 §2 一致" |
| `docs/12_RESUME_INTERVIEW_QA.md` §6 问题 27 | 项目时间"集中在 **2026-09-05 到 09-14**" |
| **Git 实测** | 首提交 `aa337f6`（2026-09-05）→ 末提交 `69a58ae`（**2026-10-08 08:30:45 +0800**）；498 个提交 |

**性质**：`docs/11` 是 **M8/T015 时点的历史快照**，且**其自维护条款要求"事实底盘随每个任务完成后刷新"——实际未刷新**。
两者**都不是错误**（各自撰写时点为真），但**对外口径必须用 Git 实测值**。
**裁定建议**：以 `2026-09-05 → 2026-10-08` 为对外范围；`docs/11` §2 需追加 superseding 批注。

### `[C-02]` `IFrameSource` 统一数据源抽象：设计文档 vs 代码
| 来源 | 主张 |
| --- | --- |
| `docs/02_ARCHITECTURE.md` §2 **D1** | "统一数据源抽象：**IFrameSource**（三模式复用的关键）"，列为**核心设计决策** |
| `docs/02_ARCHITECTURE.md` §4 | `src/io/` 与 `src/app/` 为规划目录 |
| **源码实测** | `grep -rn "IFrameSource" src/ tests/` → **0 命中**；`ls -d src/io src/app` → **均不存在**；`find src -type d` 只有 `core` / `platform` / `ui` |

**性质**：`docs/02` **同一文件内 §4 已写明该抽象被"推迟"**（"单数据源阶段不过早设计，等 Replay/Serial 出现真实共性再提取"），
因此**不是隐瞒**——但 D1 以"架构支柱"的口径出现，与 `AGENTS.md` 的 `planned ≠ implemented` 纪律存在张力。
**实际成立的共享形式**：三模式复用同一批**核心函数**
（`SerialTransactionSession.cpp` → `analyzeFunction03Transaction`；`ReplayAnalysis.cpp` → `analyzeObservedTransaction`；
最终都汇入 `summarizeTransactions`），并由可执行断言锁定口径一致
（`demo_v1.mlog` Golden Replay 与 Simulator Demo 统计快照严格相等）。
**裁定建议**：对外**说"共享核心函数 + 可执行口径断言"，不要说"有统一数据源接口"**。

### `[C-03]` README 声称"无 register map / 工程单位层"，但 M11/M12 已交付
| 来源 | 主张 |
| --- | --- |
| `README.md` §11 已知限制 | "**无 register map / 工程单位层**：异常码 0x02 可指向'非法数据地址'并建议核对映射，但不指认具体寄存器语义。" |
| `docs/INTERVIEW_NOTES.md` F8 | "无 register map/工程单位语义层（0x02 只能指向'核对地址映射'）" |
| **源码实测** | `src/core/analysis/RegisterDecode.h`：7 种解码类型（含 `UInt32=4` / `Int32=5` / `Float32=6`）+ `RegisterByteOrder` + `RegisterWordOrder`；`src/core/profile/DeviceProfile.h`：`RegisterEntry{…, scale, offset, unit}` + 冻结公式 `decodedValue * scale + offset` |
| **仍成立的部分** | 异常码 **0x02 的诊断路径**仍只建议"核对地址映射"，**不指认具体寄存器语义** |

**性质**：**历史遗留表述**——该句在 T015 时点为真（当时确实没有），M11/M12 落地后**无人回头改 README 的已知限制清单**。
`README.md` §2 自己已提到"设备档案 + 手册智能"，因此 README **内部也不自洽**。
**裁定建议**：这是**自动化面试风险最高的一条**——若面试官打开仓库会发现能力比简历/README 声明的**更强**；
应修正 README §11（属 Human 授权范围；本轮 docs-only 边界内未擅自修改已验收 README 的事实条目）。

### `[C-04]` 事务状态数：`docs/09` 的"六状态" vs ADR-003 与现状的"七状态"
| 来源 | 主张 |
| --- | --- |
| `docs/09_DIAGNOSTIC_COVERAGE_AUDIT.md` §1 / §27 | "`TransactionStatus` 恰为上述六类，**无第七个状态**"（引 `TransactionAnalysis.h:14-21`；"T007 档案'不新增状态'的定案"） |
| `ADR-003` + `src/core/analysis/TransactionAnalysis.h` | 第七状态 `ExpectedNoResponse` **已实现**；枚举实测为 **7 个成员** |

**性质**：**同一天内的历史时点差异**——`docs/09` 写于 ADR-003 批准**之前**（均为 2026-09-13），两条记录**当时都为真**。
`docs/09` **未加历史快照声明**（与 `docs/07`/`docs/11` 不同）。
**裁定建议**：对外统一说 **7 个状态**；`docs/09` 需追加批注说明其后被 ADR-003 扩展。

### `[C-05]` 历史口径数字：CTest 目标数 / ADR 数 / 任务档案数
| 来源 | 主张 | 实测 |
| --- | --- | --- |
| `docs/12_RESUME_INTERVIEW_QA.md` §1 / §4.1 | "**24 个 CTest 目标全绿**" | 现状 **70**（`grep -cE "^\s*add_test\(" CMakeLists.txt`） |
| `docs/INTERVIEW_NOTES.md` F10 / `docs/tasks/T028` §2E | "**2 份 ADR**" | `docs/adr/` 实测 **3** 份（`ADR-003` 自 `f649270` 起在库内，**早于 T028**） |
| `docs/INTERVIEW_NOTES.md` F10 | "**25 份任务档案**" | HEAD 实测 `docs/tasks/*.md` = **30** 份 |

**性质**：
- "24 个目标"是 **v1（2026-09-14）历史快照**——该文件 §2.4 有 M10 之后的局部更新批注，但 **§1 定稿与 §4.1 未更新**；
- "2 份 ADR"是**事实错误**（ADR-003 在其自身提交时点就已存在）；
- "25 份任务档案"是**计数口径差异**（是否含 `T001.1`）叠加 T028–T031 新增。
**裁定建议**：对外统一 **70 个 CTest 目标 / 3 份 ADR / 30 份任务档案**（并说明"70"是快照而非永恒常量）。

---

## 6. UNVERIFIED 清单（10 条）

| ID | 主张 | 为什么保持未断言 |
| --- | --- | --- |
| `[U-01]` | `docs/09` 引用的 `TransactionAnalysis.h` 行号（`:14-21` / `:32-38` / `:40-47`）**当前仍指向原文** | T014/T015 已向同一头文件追加内容 ⇒ **行号已位移**。属历史行号，不可直接复用 |
| `[U-02]` | `docs/02` §6 规则"任何对 D1–D7 的变动 → 新建 ADR"**是否被完全遵守** | D3（`IFrameSource` 未提取）与 D10（M10 主动写路径）以**就地更新**维护而非各立 ADR。**是否构成"静默改架构"是治理判断，不是仓库单向可判定的事实** |
| `[U-03]` | `docs/tasks/T022`（776 KB / 11593 行）**全文内部细节** | 本轮未按行通读；已用 canonical 文档 + `git log` + 源码回源覆盖其对外结论 |
| `[U-04]` | `docs/tasks/T017` / `T018` / `T019` / `T020` / `T021` **全文内部细节** | 本轮为标题级 + Issue 档案等价覆盖 |
| `[U-05]` | `docs/tasks/T027` **§54–§118** 内部细节 | 本轮仅标题级覆盖（该区间为发布链与 M12-C/D 密集档案区） |
| `[U-06]` | `docs/08_KNOWLEDGE_OWNERSHIP.md`（1921 行）**正文内容** | 仅读取标题结构 |
| `[U-07]` | 68 篇 `docs/devlog/*.md` 的**具体内容** | 仅取文件清单与日期分布（用于确认"逐日留档到 2026-10-07"） |
| `[U-08]` | `docs/PROJECT_STATUS.md` **全部历史状态块**（2421 行） | 仅读最新 3 块 + 关键断言 `grep` |
| `[U-09]` | `docs/BACKLOG.md` **全部 445 行** | 仅 `grep` 定位 T025/T026 行与里程碑表 |
| `[U-10]` | GitHub **网页端当前状态**：Description / 15 Topics / Social Preview 是否已应用；`v2.0.0` tag ref 是否存在于远端；Release 页与公开资产在**当下**是否仍可访问 | **本地仓库无法判定远端网页状态**；任务基线亦明确"Social Preview: do not assume whether Human has applied it"。已知：`origin/main` = `2f53197` 的祖先链**包含** `b18e378`（v2.0.0 的 peeled commit），但 **tag ref 本身是否在远端无法离线核验** |

> **ISSUE-008 / ISSUE-009 的历史 exact root cause 也属"未证明"**，但它们是**产品缺陷的根因**而非本 Fact Pack 的主张，
> 且仓库已按 `MONITORING / NON-BLOCKING` 显式处置（**不编造 RCA**）——见 `04_BUG_RCA_CATALOG.md`。

---

## 7. 参考基线快照（本轮 preflight 实测，可复现）

```text
repo            = E:/desktop/ModbusLens
branch          = main
HEAD            = 69a58aedf059794b4485dc897587b4c6962f8946
commits         = 498（git rev-list --count HEAD）
时间范围        = 2026-09-05 → 2026-10-08（author date 2026-10-08 08:30:45 +0800）
tags            = v1.0.0（object 2cee626b16aa0a722245d9e46e2c6c10a28e5f35
                            → peeled ae067ab7e542509310358a6e3405dfa356fefbd7）
                  v2.0.0（object bdabd6fd9024f52e38530def90f93e8cdeb3816c
                            → peeled b18e378f68a3fe19556d2562b03a3335ba086bf6）
remote          = origin https://github.com/TheVoid2021/ModbusLens.git
origin/main     = 2f53197cc149bbf75e876b3b94234a2f934a8048
divergence      = 0 behind / 7 ahead（本地 7 个提交未推送）
verified LKGC   = 9c065f23a7f06beb73beedb680a3fe3b03e4c596
CTest 目标      = 70
docs/tasks      = 30 个 .md（T001, T001.1, T002–T024, T027–T031）
docs/issues     = 20 个（ISSUE-001 … ISSUE-020）
docs/adr        = 3 份（ADR001 / ADR002 / ADR-003）
worktree        = tracked/cached diff EMPTY；仅 2 个 untracked 脚手架文件（_ctx.py / _dump.py）
git diff --check = PASS
状态口径        = PRODUCT CODE = FROZEN / UNCHANGED；UI FREEZE = ACTIVE
                  TAG = UNCHANGED；RELEASE = UNCHANGED（v2.0.0 资产未动）；PUSH = NONE
```
`[V-549]`

**重大版本事实**：v2.0.0 公开发布于 **2026-10-07**；资产 `ModbusLens-2.0.0-windows-x64.zip` =
**35,814,735 B** / SHA-256 `6db918f35b62b3bb7638b7e0660cb74d9137d22aada1ca512513bd0a2654449d`；
人工验收 **H1–H8 PASS**；公开资产经**独立下载逐字节验证（R5G）**（历史证据，本轮未重新执行）。`[V-550]`

---

## 8. 使用这份 Fact Pack 的三条纪律

1. **先说边界，再说亮点。** 本目录的 `05` 档把"REAL MODBUS HARDWARE = NOT VERIFIED"放在必答十项之首——
   面试时主动说，比被动辩解强。`[V-551]`
2. **任何对外数字先过 `05` §7 的"不成立表述"自查表**，再过 `06` §2 的 SHA 表。
   找不到锚点的说法，**先补证据再写入**。`[V-552]`
3. **发现本文档与仓库冲突时，仓库胜。**
   本目录是**某时点的提取结果**，不是永久真相；AGENTS.md 的 Cross-Agent Anti-Drift 同样适用于它。`[V-553]`

---

## 9. 本目录不包含什么（范围声明）

- ❌ 2 分钟 / 5 分钟项目介绍　❌ 简历 bullet　❌ 背诵稿　❌ 模拟面试题
  —— 全部属于 **T032 后续阶段**，须经 **HUMAN FACT REVIEW** 通过后才启动。`[V-554]`
- ❌ 对 T001–T031 的重新验收或重新执行　❌ 任何产品 / UI / 测试 / 打包改动。`[V-555]`
- ❌ 对历史档案的修改（`docs/tasks/`、`docs/issues/`、`docs/adr/`、`docs/devlog/` 为**只增不改**档案区）。`[V-556]`