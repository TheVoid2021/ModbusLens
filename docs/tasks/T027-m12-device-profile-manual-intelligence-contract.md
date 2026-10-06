# T027 — M12 Device Profile & Manual Intelligence — Contract / Acceptance Definition

- **Goal**：在 M11（Register Readout & Decode，已 COMPLETE + FINAL PORTABLE PACKAGE VERIFIED）之上，为 **M12 — Device Profile & Manual Intelligence** 冻结 v1 契约：Device Profile 的结构与持久化（JSON）、register map / register-entry 层、scaling / offset / engineering unit 的表达能力、Profile Editor 边界、说明书导入 + AI 提取候选语义、Manual Q&A 的证据边界，以及分阶段实施（A→B→C→D）与 acceptance matrix。
- **Background**：M11 已交付**通用**寄存器解码（Hex/Binary/UInt16/Int16/UInt32/Int32/Float32 + byte/word order + 滑动窗口 + 来源范围），但软件**不知道数值的设备含义**（canonical §M11 逐字要求区分「raw deterministic register data」与「device-specific physical semantics」）。M12 承担后者。本轮为 **docs-only contract round**——不写任何产品代码。
- **Technical Decisions**：见 §3 Human-approved decisions（D1–D4）与 §7–§14 冻结条款；未决项全部显式标注，禁止自行发明。

## 1. Canonical provenance

| 来源 | 内容 | 定性 |
| --- | --- | --- |
| `docs/11_V2_UPGRADE_PLAN.md` §M12（L102–106） | 「**M12 — Device Profile & Manual Intelligence**」：M12-A Device Profile Schema；M12-B Profile Editor；M12-C Manual Import + AI Extraction（PDF/DOCX/TXT/Markdown；扫描 PDF OCR 仅后续能力）；AI 输出 = Device Profile Candidate 非 verified truth（candidate value / evidence / source page-section（若有）/ confidence-uncertainty / confirmation state；Accept/Edit/Reject 后进 verified Profile）；M12-D Manual Q&A（基于上传说明书证据；找不到时 `Not found / Insufficient evidence`）；**AI is extractor/assistant, not authority** | **PRE-EXISTING CANONICAL** |
| `docs/11_V2_UPGRADE_PLAN.md` §M11（L98–100） | M11 = FC03 successful response → raw register values → 七类解码视图（含 byte/word order）；**必须区分 raw deterministic register data 与 device-specific physical semantics** | **PRE-EXISTING CANONICAL**（M11/M12 边界句） |
| `docs/tasks/T024-…contract.md` §3/L45、§W8/L803 | 「register map / device profile / 40001 别名 = **M12 域**」；「register map / device profile / 厂商语义 —— M12」 | **T024 归类**（注意：canonical §M12 原文**没有** "register map" / "40001" 字样——该归类是 T024 的 M12-domain interpretation，**不是 canonical 逐字要求**） |
| Human 本轮明确批准（原文：「四项都同意」） | D1 register map in M12 v1；D2 explicit scaling/offset/unit in M12；D3 JSON persistence；D4 分阶段 A→B→C→D | **HUMAN-APPROVED CONTRACT DECISION**（非 pre-existing canonical） |
| 本文件 | acceptance matrix、closure policy、P0/P1/DEFER、truth architecture 四层 | **NEW M12 CONTRACT REQUIREMENT（HUMAN-APPROVED THROUGH THIS CONTRACT ROUND）** |

**Provenance 纪律**：D1–D4 不得被写成「canonical 原本就有」；T024 的 M12 域归类不得被写成 canonical 逐字；本文件新增项必须带上述定性标签。

## 2. M11 / M12 boundary（冻结）

- **M11 remains authority for generic decoding**：七类类型 + byte/word order + 滑动窗口 + DecodeStatus 的 wire/decode 语义**不重开**（T024 §27 已追认）。
- M12 只在其上加 **device-specific 层**：profile metadata（名称/说明/类型引用/默认序/scaling/offset/unit）与呈现派生；**不得改写**任何 wire / raw / generic decode 事实。
- M11 能力保持可用（回归保护）；M10（FC03/FC04/custom 读取、FC06/FC16 写入、R15/R16/R17、read-result taxonomy）保持可用。

## 3. Human-approved decisions（本轮冻结，D1–D4）

### D1. Device Profile v1 includes Register Map（HUMAN-APPROVED CONTRACT DECISION）

- M12 v1 Device Profile **必须包含 register map / register-entry 层**。
- **Register entry 最低必须表达**（REQUIRED）：
  1. **address**（权威 = PDU / 0-based；复用 M11/M10 已冻结地址口径）
  2. **name**
  3. **description**
  4. **dataType**（引用现有 M11 类型体系：Hex / Binary / UInt16 / Int16 / UInt32 / Int32 / Float32，**不重新发明 decoder**）
  5. **registerCount / span**（必须与所选类型需求一致或可校验）
  6. **defaultByteOrder**（Normal / ByteSwapped，M11 枚举）
  7. **defaultWordOrder**（HighWordFirst / LowWordFirst，M11 枚举）
- **未列入 REQUIRED 的字段**（canonical 未规定且 Human 未批准 ⇒ P1 / HUMAN DECISION REQUIRED，见 §16）：
  read/write access metadata、function-code / register-family metadata、40001 alias、bit definitions、vendor-specific semantics。

### D2. Explicit scaling / offset / unit are in M12（HUMAN-APPROVED CONTRACT DECISION）

- M12 v1 **包含**：explicit scaling、explicit offset、engineering unit（作为 profile entry 的可表达字段）。
- **禁止自动猜**：AI 若从说明书提取 scale / offset / unit，**必须先作为 Candidate**；只有 Human Accept/Edit/Reject 后才能进入 verified Profile。
- **字段在 scope ≠ 公式已冻结**：Human 批准了能力进入 M12，**未批准** scaling 的精确数学公式 / 运算顺序。
  - `physical = raw * scale + offset` 或任何其它公式**不得**被写成已获批准事实。
  - 待决项 S1–S7 见 §12，状态 = **HUMAN DECISION REQUIRED BEFORE IMPLEMENTATION**。
- **unit conversion 不在本批准范围内**：M12 v1 只需表达说明书给定的工程单位；**不得**自动 °C↔°F / bar↔psi / rpm↔rad/s 等换算。

### D3. JSON persistence is REQUIRED（HUMAN-APPROVED CONTRACT DECISION）

- Device Profile v1 **必须可保存、可加载**；持久化格式 = **JSON**（Human-approved）。
- ⇒ **Profile persistence = REQUIRED；JSON = REQUIRED**。
- **不得升级**为 SQLite / database / cloud sync。
- **不得自行发明**：最终 JSON schema、schema version policy、目录位置、migration strategy、autosave policy——这些状态 = **NOT YET FROZEN / HUMAN DECISION REQUIRED BEFORE IMPLEMENTATION**（清单见 §11）。
- 选了 JSON ≠ 决定全部 storage architecture。

### D4. M12 uses staged A → B → C → D implementation（HUMAN-APPROVED CONTRACT DECISION）

| 阶段 | 范围 |
| --- | --- |
| **M12-A** | Device Profile Schema + JSON persistence + Register Map foundation |
| **M12-B** | Profile Editor + 与现有 Read Result 的人工选择式 semantic overlay / integration |
| **M12-C** | Manual Import + AI Extraction Candidate + Accept/Edit/Reject |
| **M12-D** | Manual Q&A + evidence-backed answers + `Not found / Insufficient evidence` |

关键纪律：**先做 A**；禁止第一切片直接碰 AI；禁止 A 阶段顺手做完整 Editor；禁止 A 阶段顺手做 Manual Q&A。

## 4. Canonical M12 framework must remain（保留项）

- **M12-A Device Profile Schema**、**M12-B Profile Editor**、**M12-C Manual Import + AI Extraction**（支持 **PDF / DOCX / TXT / Markdown**）、**M12-D Manual Q&A** 全部保留。
- **扫描 PDF OCR**：canonical 仍写「仅作后续能力，除非调研证明低成本可靠可做」⇒ 继续标 **future capability**，**不得升级为 M12-A 必需项**。
- **AI boundary（冻结）**：**AI is extractor / assistant, not authority。**
- **AI candidate 至少具有**：candidate value、evidence、source page / section（若有）、confidence / uncertainty、confirmation state。
- **Human：Accept / Edit / Reject**；只有确认后进入 verified Profile。
- **Manual Q&A**：有证据才回答；没找到 → `Not found`；证据不足 → `Insufficient evidence`；**不得猜**。

## 5. Address authority（冻结）

- M11 已冻结 **PDU / 0-based address = 内部权威地址**；M12 **不得破坏**。
- **40001 / 4xxxx**：
  - **automatic conversion/mapping = OUT OF SCOPE**（Human 尚未批准）。
  - 如未来支持，只能是 **presentation alias** 或**显式 profile metadata**。
  - manual display alias = **P1 / HUMAN DECISION REQUIRED**（见 §16）。

## 6. Do not reopen M11 types（冻结）

- M12 **不得**趁机重新扩展 M11。继续 **DEFER / OUT OF SCOPE**：**Float64、String decode、bit-field decode**（除非 Human 后续另行批准）。
- Profile 可以**引用**现有 M11 类型（七类 + byte order + word order），但 **M12 不重新定义其 wire/decode semantics**。

## 7. M12-A contract boundary（本轮冻结，不实现）

M12-A 目标限定为：**Device Profile Schema + JSON persistence + Register Map foundation**。

M12-A 明确不做（全部冻结）：

```text
NO AI                    NO manual import        NO Manual Q&A
NO full Profile Editor   NO automatic device inference
NO vendor database       NO cloud sync
NO auto 40001 mapping    NO unit conversion
NO writeback / device control
NO M11 decode redesign
```

## 8. M12-A truth architecture（冻结，四层）

```text
Layer 1  wire / transaction truth      = M10/M11 existing authority
         （Actual TX / Actual RX / TransactionAnalysis.values / raw DEC·HEX）
Layer 2  generic decode truth          = M11 RegisterDecode（七类 + 双轴 + DecodeStatus）
Layer 3  Device Profile metadata       = M12 verified configuration
Layer 4  physical / semantic presentation
         = derived from Layer 2 decoded value + Layer 3 verified metadata
```

- **Profile metadata 不得改写**：Actual TX / Actual RX / `TransactionAnalysis.values` / raw DEC / raw HEX / generic decode result。
- scaling / offset / unit **只能创建 derived semantic view**（新层，绝不回流）。
- **Profile 删除 / 缺失 / 损坏时：raw + generic decode 仍必须完整可用**（Layer 1/2 独立于 Layer 3）。

## 9. Register entry contract（基于 D1 + D2）

**A 阶段 REQUIRED 字段**（冻结，与 D1 一致）：

| 字段 | 权威/约束 |
| --- | --- |
| `address` | PDU / 0-based（§5） |
| `name` | 自由文本 |
| `description` | 自由文本 |
| `dataType` | 引用 M11 七类型枚举；不重新发明 decoder |
| `registerCount` / span | 与所选类型需求一致或可校验（UInt32/Int32/Float32 = 2；16 位类型 = 1） |
| `defaultByteOrder` | M11 `RegisterByteOrder`（Normal / ByteSwapped） |
| `defaultWordOrder` | M11 `RegisterWordOrder`（HighWordFirst / LowWordFirst） |
| `scale` / `offset` / `unit` | D2 批准字段**存在**；**数学应用规则未冻结**（见 §12）——本轮只定义字段可表达，不冻结运算公式 |

**P1 / HUMAN DECISION REQUIRED（不得擅自加入 REQUIRED）**：read/write access metadata；function-code / register-family metadata；40001 alias；bit definitions；vendor-specific semantics。

## 10. Profile identity / device level

- canonical 要求 Device Profile，但**未规定** profile identity fields ⇒ **不得擅定义完整 schema**。
- 最低必要 candidate（**PROPOSED / HUMAN REVIEW REQUIRED**，非 REQUIRED）：`profile id`、`display name`、`manufacturer`、`model`、`revision/version`。
- **自动从串口识别设备 = OUT OF SCOPE**（不因 profile identity 存在而自动启用）。

## 11. JSON contract gaps（必须先冻结才能实现，本轮不实现）

Human 已批准 **JSON persistence**；以下仍须 contract 明确（状态 = **NOT YET FROZEN / HUMAN DECISION REQUIRED BEFORE IMPLEMENTATION**）：

```text
J1  JSON top-level structure
J2  profile schema version（版本字段与兼容策略）
J3  single-profile-per-file vs multi-profile-per-file
J4  storage default directory
J5  manual save vs autosave
J6  load failure behavior
J7  unknown future fields behavior（前向兼容）
J8  schema migration
J9  malformed JSON handling
```

## 12. Scaling contract gaps（FIELD IN SCOPE ≠ FORMULA FROZEN）

Human 已批准 scaling / offset / unit **进入 M12**；以下待决（**HUMAN DECISION REQUIRED BEFORE IMPLEMENTATION**）：

```text
S1  scale/offset 精确公式
S2  应用顺序（scale 与 offset 的先后）
S3  scale = 0 是否有效
S4  缺省 scale/offset 语义（未填写时的行为）
S5  overflow / NaN / Inf presentation
S6  unit 是自由文本还是枚举
S7  是否允许 profile entry 只写 unit 而无 scale
```

## 13. Profile Editor boundary（M12-B）

- Profile Editor = canonical REQUIRED；**UI NOT FROZEN**。
- Contract 只冻结能力：**必须能由 Human 查看 / 新增 / 编辑 / 删除 profile metadata / register entries**；**必须能 Accept / Edit / Reject AI candidate**（此项在 C 后接入）。
- 具体布局（新独立 workspace / dialog / side panel / table page）= **NOT FROZEN** ⇒ M12-B entry-gate Human decision。

## 14. M11 Read Result integration（M12-B 原则）

- M12-B 允许与现有 Read Result 形成 **semantic overlay**（人工选择式）。
- 冻结原则：**raw columns always visible**；**generic M11 decode remains visible**；**M12 semantic value 必须标明来自哪个 Profile entry**。
- Profile 未匹配时：**不要猜**；显示 `No profile mapping` 或等价文案（**具体文案待 Human Review**）。
- **禁止**按 address 跨 profile 自动猜设备。

## 15. Manual Import / AI Extraction（M12-C）

- 按 canonical：支持 PDF / DOCX / TXT / Markdown；**扫描 PDF OCR = future capability**（非本阶段必需）。
- **AI output = Candidate only**；**不得直接修改 verified Profile**。
- 每个重要提取项需要：evidence；source location（if available）；confidence / uncertainty；confirmation state；Human Accept/Edit/Reject。
- 本轮**不决定**：AI provider、prompt format、embedding store、vector DB、OCR engine（属 C 的后续 contract slice）。

## 16. Manual Q&A（M12-D）

- 答案必须 **evidence-backed**；必须区分 **found / `Not found` / `Insufficient evidence`**。
- **不得**让模型凭 general knowledge 回答成设备手册事实。
- 本轮**不决定**：搜索后端 / chunking / embedding / reranker / LLM provider。

## 17. Acceptance matrix

### 17.1 M12-A（本轮细化；若公式未冻结，A09/A10 只测 metadata round-trip）

| ID | 场景 | 期望 |
| --- | --- | --- |
| A01 | create empty profile | 可创建空 profile（最少 identity 字段按 §10 candidate 或最小集） |
| A02 | register entry add | 条目加入并可回读全部 REQUIRED 字段 |
| A03 | register entry edit | 编辑持久生效 |
| A04 | register entry delete | 删除持久生效 |
| A05 | address PDU/0-based preserved | 地址口径 = PDU/0-based，无 40001 自动换算 |
| A06 | existing M11 dataType binding | dataType 引用 M11 七类型枚举；非法值被拒绝 |
| A07 | existing byte-order binding | defaultByteOrder ∈ {Normal, ByteSwapped} |
| A08 | existing word-order binding | defaultWordOrder ∈ {HighWordFirst, LowWordFirst} |
| A09 | scale field round-trip | **仅 metadata round-trip**（公式未冻结，不做物理计算） |
| A10 | offset field round-trip | 同上 |
| A11 | unit field round-trip | unit 原样存取 |
| A12 | JSON save | 保存产出合法 JSON |
| A13 | JSON load | 加载回读成功 |
| A14 | save→load identity | 往返后内容语义一致（identity 判定口径实现时冻结） |
| A15 | malformed JSON rejection | 坏 JSON 被拒绝且不崩溃、不产生半成品 profile |
| A16 | raw truth unchanged | profile 存在/操作**不改写** raw DEC / HEX / 地址 |
| A17 | generic M11 decode unchanged | M11 解码结果与无 profile 时一致（Layer 2 独立） |
| A18 | profile missing → raw/decode still usable | profile 删除/缺失/损坏时 Layer 1/2 完整可用 |
| A19 | unknown/unmapped address does not invent semantics | 未映射地址不产生任何 semantic 值 |
| A20 | profile metadata never changes wire result | wire result / taxonomy / R15–R17 等零变化 |

### 17.2 M12-B / C / D（高层 exit boundary placeholders，不发明技术实现）

- **M12-B exit**：Editor 可完成 profile 与 register entry 的查看/增/改/删；semantic overlay 在 Read Result 上可用且 raw + generic decode 恒显；未匹配显示冻结文案；1000×700 无回归；M10/M11 regression protected。
- **M12-C exit**：说明书导入（PDF/DOCX/TXT/MD）产生 Candidate 列表；每项带 evidence/source/confidence/confirmation；Accept/Edit/Reject 全链路可用；**AI 无法直接写入 verified Profile**；OCR = future capability（不阻塞 C）。
- **M12-D exit**：问答仅基于已导入证据；found / `Not found` / `Insufficient evidence` 三态可区分；无证据不回答。
- 各阶段实施前须有对应 detailed matrix + Human Review（§18）。

## 18. M12 closure policy（NEW M12 CONTRACT REQUIREMENT）

canonical repo 无专属 M12 closure matrix ⇒ 本轮定义（标注 = **NEW M12 CONTRACT REQUIREMENT（HUMAN-APPROVED THROUGH THIS CONTRACT ROUND）**）：

```text
· 所有 REQUIRED acceptance PASS（各阶段自己的 matrix）
· Debug full regression PASS
· Release full regression PASS
· QML diagnostics clean
· 1000×700 no regression
· M10 / M11 regression protected
· Human Review PASS（每个已实施 M12 stage）
· docs archived
· behavior commit separated from docs
· verified LKGC advancement 仍需 Human explicit authorization
```

- **M12 real hardware policy = NOT YET FROZEN**（canonical 未规定；**不得**自动升级为 hard gate；closure 阶段 Human 再裁定）。
- **M12 packaging policy = NOT YET FROZEN**（同上）。

## 19. P0 / P1 / DEFER（本轮重分类）

**已解决（Human D1–D4）**：P0-1 register map in M12 = APPROVED；P0-2 scaling/offset/unit in M12 = APPROVED；P0-3 JSON persistence = APPROVED；P0-4 staged A→B→C→D = APPROVED。

**P0-BEFORE-M12-A-IMPLEMENTATION（当前实现入口门禁）**：

```text
P0-a  exact required profile identity fields（§10 candidate 的裁定：哪些 REQUIRED、哪些可选）
P0-b  JSON structure / version / storage policy（J1–J9）
P0-c  scaling formula / order / default semantics（S1–S7；或裁定 A 阶段不含公式实现）
```

**P1-BEFORE-M12-B**：editor UI shape；Read Result integration exact UX（含未匹配文案）；40001 alias decision；access/function metadata decision。

**P1-BEFORE-M12-C**：AI provider / BYOK model；extraction evidence schema；OCR boundary。

**P1-BEFORE-M12-D**：Q&A retrieval / evidence UX。

**DEFER**：Float64；string；bit-field；automatic device inference；vendor cloud/database；automatic unit conversion；auto 40001 mapping。

## 20. Implementation entry gate（冻结）

```text
M12 = STARTED — CONTRACT / ACCEPTANCE DEFINITION（本文件）
M12 Implementation = NOT STARTED
First implementation slice（M12-A）= BLOCKED ON REMAINING P0 CONTRACT DECISIONS（§19）
即：M12-A IMPLEMENTATION ENTRY GATE = CLOSED
直到 Human 完成剩余 P0（P0-a / P0-b / P0-c）。
```

## 21. Explicit out-of-scope（M12 v1）

```text
· automatic device inference / 自动设备识别
· vendor database / 厂商模板库 / cloud sync
· automatic unit conversion（°C↔°F 等）
· automatic 40001 / 4xxxx mapping
· Float64 / String decode / bit-field decode（M11 侧亦维持 DEFER）
· writeback / device control（Agent 无写权限不变）
· SQLite / database / 云存储（JSON 之外的持久化）
· 扫描 PDF OCR（future capability）
```

## 22. Human decisions provenance

- Human 对本文件的批准原文：「**四项都同意**」⇒ D1–D4 冻结（§3）。
- 批准时间：2026-09-25（与 M11 FINAL PORTABLE PACKAGE VERIFIED 同轮；未提供可记录的精确时间戳）。
- 本文件为 **docs-only** 交付；不构成 M12 implementation 授权、不推进 verified LKGC（`bc99e6ea871628a3a685b9cf80cf3840e7b3b171` 不变）、不创建 tag/push。

## 23. Files Changed / Verification / Git Commit

- **Files Changed**：本文件（新增）；`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、`docs/devlog/2026-09-25.md`（状态同步）。
- **Verification**：docs-only 轮——无 build / 无 ctest / 无 package；`git diff --check` PASS；改动路径全部 `docs/`。
- **Git Commit**：`M12: define device-profile acceptance contract`（docs-only；full hash 见提交报告）。
## 24. P0 Freeze — Human-Approved Contract Decisions（2026-09-25，docs-only，本轮追加）

> **Human 明确回复（原文）：「三组都同意」⇒ P0-a / P0-b / P0-c 全部冻结。**
> 以下全部定性 = **HUMAN-APPROVED M12 CONTRACT DECISION**（**非** pre-existing canonical requirement）。
> 本节关闭 §19 的三项 P0 ⇒ **M12-A IMPLEMENTATION ENTRY GATE = OPEN**（§20 更新见 §25）。

### 24.1 P0-A Profile identity（HUMAN-APPROVED）

- **REQUIRED**：`profileId`、`displayName`。
- **OPTIONAL**：`manufacturer`、`model`、`revision`、`description`。
- 语义：`profileId` = 程序生成的唯一稳定标识；`displayName` = Human 可见名称，必填；其它字段可空，不阻塞建立 Profile。
- 工程实现允许使用标准 UUID，但 **UUID 方案属于 implementation detail，不是 canonical §M12 原文**。

### 24.2 P0-B JSON persistence（HUMAN-APPROVED）

- **一个 JSON 文件 = 一个 Device Profile**。
- `schemaVersion` **必须存在**，v1 = **1**。
- `schemaVersion > 当前支持版本` ⇒ **拒绝加载**，返回 version-too-new 类错误。v1 **不做 schema migration**。
- **默认持久化位置 = 应用的用户数据目录**（下含 `profiles` 子目录）；**不得**硬编码用户名、写安装目录、依赖当前工作目录；优先使用 Qt 标准用户数据目录 API（source audit 后选择）。
- **保存策略 = MANUAL SAVE**；v1 **不做 autosave**。
- **Malformed JSON ⇒ 拒绝加载**；**加载失败时不得覆盖当前已经有效的 Profile**。
- **Unknown fields**：同 schemaVersion 下**不能因未知字段拒绝整个 Profile**；v1 **可忽略未知字段**。**不承诺**未知字段 round-trip preservation。
- **Import / Export**：v1 不做额外数据库体系——**JSON 文件本身就是可携带 Profile**。**不引入** SQLite / cloud sync / database server。

### 24.3 P0-C Scaling / offset / unit（HUMAN-APPROVED）

- **公式冻结**：`semanticValue = decodedValue * scale + offset`。
- **严格顺序**：M10/M11 raw truth → M11 generic decoded value → **multiply scale** → **add offset** → attach unit for presentation。
- 默认：`scale = 1`、`offset = 0`、`unit = ""`。**`scale = 0` 合法**。`unit` = **自由文本**。允许只有 unit（scale/offset 用默认值）。
- **不做自动单位换算**（禁止自动 °C↔°F / bar↔psi / rpm↔rad/s）。
- M11 decoded 为 **NaN / +Inf / −Inf** 时，M12 **不得伪装成普通 physical number**——继续特殊值语义（IEEE 自然传播）。
- Profile scaling **永远不得改写**：Actual TX / Actual RX / `TransactionAnalysis.values` / raw DEC / raw HEX / M11 generic decoded value——**只生成 derived semantic value**。

## 25. Minimal JSON v1 shape（M12 IMPLEMENTATION CONTRACT DETAIL，本轮冻结）

> 定性 = **M12 IMPLEMENTATION CONTRACT DETAIL**（**非** canonical §M12 原文）。shape 遵循 Human 冻结的 §24
> 与 source audit 结论（无 JSON helper 先例；dataType/byteOrder/wordOrder 值直接引用 M11 语义，
> 拼写采用 enum 名本身；既有 snake_case token（`registerWordOrderName` 等）服务其它层，保持不变）。

顶层（v1）：

```json
{
  "schemaVersion": 1,
  "profileId": "...",
  "displayName": "...",
  "manufacturer": "...",
  "model": "...",
  "revision": "...",
  "description": "...",
  "registers": [ ... ]
}
```

register entry（v1）：

```json
{
  "address": 1000,
  "name": "...",
  "description": "...",
  "dataType": "UInt16",
  "registerCount": 1,
  "byteOrder": "Normal",
  "wordOrder": "HighWordFirst",
  "scale": 1.0,
  "offset": 0.0,
  "unit": "Hz"
}
```

- **JSON key 命名稳定**（v1 冻结）。
- `dataType` ∈ {Hex, Binary, UInt16, Int16, UInt32, Int32, Float32}；`byteOrder` ∈ {Normal, ByteSwapped}；`wordOrder` ∈ {HighWordFirst, LowWordFirst} —— 全部**引用现有 M11 semantics**，不实现第二套 decoder。
- 未列出的字段（read/write、FC、40001 alias、bit definitions、vendor）**不得**出现在 v1 REQUIRED shape（维持 §9 P1）。

## 26. Validation rules v1（冻结）

- 必验：`profileId` 非空；`displayName` 非空；`schemaVersion == 1`；register `address` 在 PDU/0-based 合法范围（0..65535）；register `name` 非空；`dataType` 是 M11 支持类型；**`registerCount` 与类型 word count 一致**（Hex/Binary/UInt16/Int16 = 1；UInt32/Int32/Float32 = 2）；`byteOrder`/`wordOrder` 合法；`scale`/`offset` 是**有限数值**；`unit`/`description`/`manufacturer`/`model`/`revision` 可空。
- **不自动修正 `registerCount`**、不静默纠正、不改变 M11 decoder 语义——不一致即 **validation FAIL**。
- **重复 address = validation error**（register map v1 需要地址到 entry 的确定性映射；重叠 span 待 Human 后续单独扩展）。
- 2-register entry：**`address + registerCount − 1` 不得越出 PDU 0..65535**（即 address ≤ 65534）。

## 27. Entry gate 更新（本轮）

```text
M12 = STARTED — M12-A IMPLEMENTATION（contract 冻结完成，进入实现）
M12-A IMPLEMENTATION ENTRY GATE = OPEN（P0-a/b/c 已由 Human「三组都同意」冻结，§24–§26）
First slice scope（hard boundary，§7）：
  DeviceProfile domain model + RegisterEntry domain model + validation
  + JSON serialize/deserialize + save file + load file + default storage path helper
  + deterministic unit tests
明确不做：Profile Editor QML / Read Result overlay / 新 workspace / AI / manual import /
  Q&A / OCR / network / 40001 alias / read·write metadata / FC metadata / bit fields /
  Float64 / String decode / auto inference / unit conversion / writeback / M11 redesign。
```

## 28. M12-A First Slice — Implementation Archive（2026-09-25，behavior-bearing）

> **M12-A first slice = IMPLEMENTED / AUTOMATED PASS。** 行为提交 =
> `1c42aaf45d4c209cf3580b3d0d383bad8197b164`（「M12: add device-profile JSON foundation」；
> docs-freeze 提交先行 = `bea3c55d657684352b1186f42d7369fbe2365b4e`）。
> **Human visual review = NOT REQUIRED**（本切片零 UI，不制造 Human PASS）。

### 28.1 Scope 实现（严格 §27 hard boundary）

- **core（Zero-Qt，`src/core/profile/DeviceProfile.{h,cpp}`）**：`DeviceProfile{ schemaVersion, profileId,
  displayName, manufacturer, model, revision, description, registers }` + `RegisterEntry{ address(PDU/0-based),
  name, description, dataType, registerCount, byteOrder, wordOrder, scale, offset, unit }`；dataType/byteOrder/
  wordOrder **直接复用 M11 enums**（`RegisterDecodeType`/`RegisterByteOrder`/`RegisterWordOrder` +
  `registerDecodeTypeWordCount`），零第二套 decoder；JSON 契约 token（`Hex…Float32` / `Normal·ByteSwapped` /
  `HighWordFirst·LowWordFirst`，§25 拼写）双向映射（严格，未知 token 拒绝）；`ProfileValidationCode` 13 值
  + `profileValidationCodeName` machine token；`validateDeviceProfile`（§26 全规则）；`profileSemanticValue`
  = 冻结公式 `decoded * scale + offset`（顺序严格，IEEE 特殊值自然传播）。
- **Qt 侧（`src/ui/profile/ProfileStore.{h,cpp}`）**：`serializeToJson`（v1 shape，全 key 恒写，QJsonObject
  键序确定 → save→load→save 字节稳定）；`parseFromJson`（strict int 抽取——小数地址绝不静默截断；
  schemaVersion 缺失 / >1 / ≠1 分别 `missing_schema_version` / `schema_version_too_new` /
  `unsupported_schema_version`；未知字段忽略；parse→validate→return，**无部分应用状态**）；
  `saveToFile`（先 validate——非法 profile 拒绝写入；mkpath；**QSaveFile 原子提交**，失败保旧文件）；
  `loadFromFile`（不存在 → `file_not_found`；读失败 → `read_failed`）；`defaultProfilesDirectory()` =
  `QStandardPaths::AppDataLocation` + `/profiles`（绝不 cwd / 安装树 / 硬编码用户名）；
  `defaultFilePathFor(profileId)`。
- **CMake**：core 库 + app SOURCES + 新测试 target `device_profile`（源码直编 ProfileStore，ui_bridge 模式）。

### 28.2 测试与负向对照

- `tests/test_device_profile.cpp` → ctest `device_profile`：**33 passed**（P01–P29 全矩阵 +
  validation token + save-refusal；含 p24 真值保持证明——raw words 与 M11 `decodeRegisterView` 结果
  在 profile 全操作前后逐字节一致；p23 拒绝加载不改动既有有效对象；p29 NaN/±Inf 保持）。
- **负向对照（真实 mutate → FAIL → 恢复）**：NC1 loader 接受 schemaVersion 2 → `p20` FAIL；NC2 跳过
  registerCount/type 一致性 → `p07` FAIL；NC3 公式反序 `(decoded+offset)*scale` → `p27`+`p28` FAIL。
  全部恢复，无 mutation 提交。
- **回归**：Debug build 0 error + ctest **42/42**；Release build 0 error + ctest **42/42**（含全部
  M10/M11 回归与 QML gates；QML 零改动）。

### 28.3 边界与状态

```text
M12-A first slice = IMPLEMENTED / AUTOMATED PASS（behavior 1c42aaf）
本切片未做（按 §27）：Editor / overlay / AI / import / Q&A / OCR / 40001 alias /
  read·write·FC metadata / bit fields / Float64 / string / auto inference /
  unit conversion / writeback / M11 redesign
未 package（§14：M11 final package 保持历史 VERIFIED，不刷新）
verified LKGC = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（不推进；本行为提交仅为 M12 candidate）
M12-B / C / D = NOT STARTED
```

## 29. Span-Overlap Decision + Path-Safety Hardening（2026-09-25，HUMAN-APPROVED，docs-only freeze）

### 29.1 Human decision：M12 v1 rejects every cross-entry address-range overlap

> **Human 明确裁定（HUMAN-APPROVED M12 CONTRACT DECISION）：M12 v1 禁止任何 Register Entry span overlap。**
>
> **Provenance（诚实记录，不重写历史原文）**：此前 T027 §26 只逐字写了「重复 address = validation
> error（…；**重叠 span 待 Human 后续单独扩展**）」——即 overlap 当时是**显式 deferral**，既未冻结为
> 拒绝也未冻结为允许（审计轮 §3 结论）。**本轮 Human 才正式裁定**：M12 v1 rejects every cross-entry
> address-range overlap。§26 原句按只增不改原则保留于上文。

**冻结规则**：

- 每个 `RegisterEntry` 占用闭区间 **[startAddress, startAddress + registerCount − 1]**；
- **任意两个合法 entry 的上述闭区间不得有任何交集**（例：`UInt32@1000`（1000–1001）+ `UInt16@1001` = **validation FAIL**；`UInt32@1000` + `UInt32@1001` = **FAIL**；`UInt32@1000`（1000–1001）+ `UInt16@1002` = **PASS**）；
- **duplicate start address 自然仍为 FAIL**；
- 核心不变式：**ONE PDU ADDRESS BELONGS TO AT MOST ONE ENTRY SPAN**；
- **不得**自行增加 priority / alias resolution / multiple-match selection / ambiguous winner；
- 未来如需「同址多视图」：**另立 Alias / Alternative View contract**，不在 M12 v1 普通 RegisterEntry 中实现。

**错误码（冻结）**：`duplicate_address` 继续表示**精确 start address 重复**；**cross-span overlap 使用新
错误码 `overlapping_span`**（不复用 duplicate_address 隐藏两种原因）；错误至少携带可定位的 entry index。

### 29.2 Path-safety hardening（Human 同意 = persistence hardening，非产品语义变更）

> **背景（审计轮 §5 只读结论）**：`defaultFilePathFor(profileId)` 曾直接拼接 `profileId + ".json"`；
> Qt 官方文档逐字：「Redundant multiple separators or "." and ".." directories in fileName are
> **not removed**」⇒ `profileId = "../escape"` 可令默认目标逃出 `profiles` 根目录（API 层潜伏缺陷，
> 当时无生产调用链）。

**Human 批准的最小修复（不改变 profileId 产品语义）**：

- `profileId` **仍是逻辑身份字符串**（不新增字符集限制、不强制 UUID-only、不改 JSON 内 profileId 值）；
- **默认 persistence filename** = `profileId` **UTF-8 字节的稳定 SHA-256** 派生：
  `profile-<64 位小写 hex>.json`（例：逻辑 `profileId="任意非空字符串"` → 内部默认文件名
  `profile-<sha256>.json`）；实现用 Qt `QCryptographicHash`（Sha256），无第三方库；
- 要求：same profileId → same filename；different ids → different filename；filename 只能包含
  固定 prefix + hex digits + `.json` ⇒ `../`、`\`、`/`、`CON`、`NUL`、`:`、Unicode 均**不能**改变目录结构；
- **防御性 containment assertion/test**：最终默认目标必须位于 `QStandardPaths::AppDataLocation/profiles` 之下；
- **不得**把 hash 当作新的 profileId、不得改 JSON 内 profileId、不得引入第三方库；
- 定性 = **M12 IMPLEMENTATION CONTRACT DETAIL（persistence hardening）**；错误码与 validation 语义不变。

### 29.3 NaN / ±Inf 审计结论（接受，不重写数学规则）

- CORE IEEE behavior = **PASS**（audit 轮矩阵：special in → special out 成立，10 例无一得到有限结果）；
- `NaN → NaN`（全 scale）；`±Inf × 1/2 → ±Inf`；`Inf × 0 → NaN`（IEEE）；负 scale 可翻转 infinity sign；
- 与 T027 §24.3 冻结原文「**IEEE 自然传播**」逐字一致 ⇒ **本轮不重写 `profileSemanticValue` 数学规则**；
- **FUTURE PRESENTATION POLICY**（未冻结）：将来 presentation 必须基于**数值 classification**（Finite /
  NaN / PositiveInfinity / NegativeInfinity），不得把 special 格式化成普通 finite number；本轮不做 UI。

## 30. M12-A Second Slice — Lookup / Query Foundation Archive（2026-09-25，behavior-bearing）

> **M12-A second slice = IMPLEMENTED / AUTOMATED PASS。** 行为提交 =
> `5d4d9c28b0904610eaacd3dbd27a9281328c2b86`（「M12: harden profile lookup foundation」；
> docs-freeze 先行 = `aff67a7c0afa0d2a7892015fafb499f3942bf8f2`）。
> **Human visual review = NOT REQUIRED**（零 UI，不制造 Human PASS）。

### 30.1 Span overlap 实现（§29.1 落地）

- `validateDeviceProfile` 维护已接受 entry 的闭区间 `[start, start+count−1]`，逐条与新 entry 比较：
  **同 start → `duplicate_address`**（保持）；**区间相交（不同 start）→ 新错误码 `overlapping_span`**；
  均携带可定位 `registerIndex`。计算在 `registerCount` 已校验为 1/2 之后进行（int 无溢出面）。
- 验证：OV01–OV08（含 JSON load 拒绝整个 profile、无部分应用）、L17；实现侧无 priority / alias / winner。

### 30.2 Path-safety hardening 实现（§29.2 落地）

- `ProfileStore::defaultFilePathFor(profileId)` = `profile-<sha256(profileId UTF-8) 64 位小写 hex>.json`
  （`QCryptographicHash`，零第三方库）；**profileId 语义不变**（无字符集限制/不强制 UUID；JSON 内保持原值——
  PS08 证明 `"../escape"` round-trip 原样）。
- 验证：PS01–PS10（含 `../escape`、`..\escape`、`a/b\c`、`CON`/`NUL`/`COM1`/`a:b`/`*?<>|`、Unicode、
  `C:/evil`、`..\..\escape` 全部 containment 通过；文件名形状 regex `^profile-[0-9a-f]{64}\.json$`）。

### 30.3 Lookup / Query foundation 实现（§8–§10 落地）

- core 纯函数：`findProfileEntryByStartAddress`（仅精确 start）/ `findProfileEntryCoveringAddress`
  （span 覆盖 + `offsetWithinSpan`）；两者**先 validation**——无效或重叠 profile 返回 `InvalidProfile`
  （绝不随机选一个，L19）；`Ambiguous` 保留为 defensive-only 状态（合法 v1 profile 不可达，L18 扫描证明）。
- 验证：L01–L19（含 empty profile、0/65535 边界、不改 profile、不碰 M11 decode、确定性、metadata 保真）。

### 30.4 Semantic projection foundation 实现（§11 落地）

- `projectProfileSemanticValue(entry, decodedScalar)`：冻结公式 + **数值分类**
  （`Finite` / `NotANumber` / `PositiveInfinity` / `NegativeInfinity`，绝不从文本反推）+ unit 原样拷贝；
  纯函数——无 RTU/CRC/TX/RX/M11 调用、不改 raw/decoded/entry。
- 验证：S01–S12（含 46.6、205、scale=0、负值/负 scale、unit 空/自由文本、NaN/±Inf 分类、
  Inf×0 → NaN 的 IEEE 行为、双不变性）。

### 30.5 测试 / 负向对照 / 回归（真实数字）

```text
device_profile = 82 passed（P01–P29 + OV01–08 + PS01–10 + L01–L19 + S01–S12 + v1/v2 + init/cleanup）
负向对照（真实 mutate → FAIL → 恢复，无 mutation 提交）：
  NC-O（移除 overlap 检查）→ OV01/02/06/07/08、L17、L19（7 FAIL）
  NC-P（恢复 raw filename）→ PS01/02/04/05/08/09/10（7 FAIL，含真实 ../escape 逃逸）
  NC-L1（covering 只比 start）→ L05/L06/L18（3 FAIL）
  NC-L2（公式反序）→ p27/p28/S02/S03（4 FAIL）
Debug build 0 error + ctest 42/42；Release build 0 error + ctest 42/42（M10/M11 与 QML gates 全绿）
NaN/±Inf：接受 audit 结论，未重写 profileSemanticValue 数学规则（§29.3）
```

### 30.6 状态

```text
M12-A second slice = IMPLEMENTED / AUTOMATED PASS（behavior 5d4d9c2）
未做（按范围）：QML / Editor / overlay / active-profile selector / AI / manual import / Q&A /
  40001 alias / access·function metadata / bit fields / Float64 / String / unit conversion /
  device inference / writeback
未 package（M11 final package 保持历史 VERIFIED）
verified LKGC = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（不推进；本行为提交仅为 M12 candidate）
M12-B / C / D = NOT STARTED
```

## 31. M12-B UI / Interaction Contract Freeze（2026-09-25，docs-only）

> **Human 明确回复（原文）：「可以」⇒ 批准上一轮提出的 B1–B5 五项方向。**
> 全部定性 = **HUMAN-APPROVED M12-B CONTRACT DECISIONS**（**非** pre-existing canonical requirement）。
> 本轮 = docs-only contract round；**M12-B Implementation = NOT STARTED**。

### 31.1 UI 架构审计（当前 HEAD 只读事实）

| 项 | 事实 |
| --- | --- |
| 一级 workspace | rail 6 条目：事务(0) / 总览(1) / 通信(2) / 回放(3) / 诊断(4) / **设备(5)＝disabled 占位**（`Main.qml` `workspaceDeviceIndex: 5` 已预留；`workspaceHost` StackLayout 索引 5 无页面） |
| 新 workspace 挂点 | **`workspaceHost` 索引 5**（既有常量 + rail 条目 + T017 已记录 Device 页任务模型）——无需新增导航概念 |
| Communication 轻量 selector 位置候选 | Request PanelCard 或 `readResultPanel`（结论行）附近；未冻结，属候选 |
| Read Result 现状 | `readResultPanel`（结论行 + 「查看详情」入口）→ `readResultDialog`（Popup）内含 `readDecodeControls`（解析类型 / 寄存器内字节顺序 / 32位寄存器顺序）与 `readResultValuesList`（每行 = `#n 地址(0xHEX) 值 DEC(HEX) 解析 <decoded> [范围 span]`）——**raw 与 generic decode 已同排两层** |
| 现有 selector 先例 | `commPortCombo` / `commBaudCombo` / `readDecodeTypeCombo` 等非 editable ComboBox 模式可直接复用 |
| session-state 先例 | `currentWorkspaceIndex`（rail 现值）；`readDecodeType/ByteOrder/WordOrder`（controller int 属性、越界写忽略、变更通知）——非持久化的 session 级状态模式 |
| 1000×700 | 详情对话框已有可滚动值表（K gate：125 寄存器可滚动验证）；semantic 层必须沿用既有 dialog/可滚动区域，**不得**向主页新增整行控件 |

### 31.2 Human-approved M12-B decisions（B1–B5）

**B1 — Profile Editor IS A SEPARATE WORKSPACE（HUMAN-APPROVED）**
- M12-B v1 Profile Editor 放在**独立的「Device Profile / 设备档案」workspace/page**；**不得**把完整 Profile Editor 塞进 Communication page；Communication 只允许轻量使用/选择 Profile。
- 具体 navigation **label / icon / 排序位置** = repo 未冻结 ⇒ 可提候选，但**不得**升级为 Human-approved（见 §31.8 候选）。

**B2 — Communication page has a LIGHTWEIGHT profile selector（HUMAN-APPROVED）**
- 允许轻量「当前设备档案：[Profile ▼]」；语义冻结 = **用户人工选择当前 Profile**；**此处不是 Profile Editor**。
- **不得**在 Communication page 堆 register map CRUD / JSON schema controls / AI candidate editor / manual import controls；Profile 的创建/编辑/删除属独立 Device Profile workspace。

**B3 — RAW / GENERIC DECODE / PROFILE SEMANTIC REMAIN VISUALLY DISTINCT（HUMAN-APPROVED）**
- 三层必须保留：Layer 1 Raw truth（raw DEC/HEX **必须继续可见**）→ Layer 2 M11 generic decode（**必须继续可见**）→ Layer 3 M12 profile semantic interpretation（**额外**解释层）。
- **Semantic 不得替换 Raw；不得替换 M11 generic decoded value**；示例语义：Address 1000 → Raw `466 / 0x01D2` → Generic `UInt16 = 466` → Profile「输出频率」→ Semantic `46.6 Hz`。
- **不得**让 QML 重新解码 wire bytes；**不得**让 Profile 改写 `TransactionAnalysis.values`。

**B4 — UNMATCHED ADDRESS MUST BE EXPLICIT（HUMAN-APPROVED）**
- 当前 selected Profile 未匹配某 PDU address 时：**不得**猜测 / 自动寻找其它 Profile / 自动 40001 转换 / 隐藏 raw row / 制造 semantic value。
- 必须明确呈现「**未匹配设备档案**」或最终冻结的等价文案；本轮冻结语义 = **NO PROFILE MAPPING**；**精确中文 UI 文案为候选**（见 §31.8），**不得**伪装成 Human 逐字批准。

**B5 — PROFILE SELECTION IS SESSION-LEVEL ONLY IN V1（HUMAN-APPROVED）**
- 当前运行期间**记住**当前 selected Profile；人工选择。
- v1 暂不做：COM port ↔ Profile 永久绑定 / 自动设备识别 / 自动按串口选 Profile / 自动按 response 猜 Profile。
- **CROSS-RESTART RESTORE POLICY = NOT YET FROZEN**（既不实现 restore-last，也不冻结 always-none；HUMAN DECISION REQUIRED，见 P1-9）。

### 31.3 Active Profile 语义（交互层冻结；service 不实现）

- **Active Profile** = Human 在当前应用运行期间人工选择用于 semantic interpretation 的一个 Device Profile。
- 概念状态必须支持：**No Profile Selected** / **Profile Selected** / **Selected Profile Missing/Unavailable**（如文件后来消失）。**具体 runtime service 本轮不实现**。
- **禁止**任何自动化绑定：COM / Slave Address / FC / register response / manufacturer·model。

### 31.4 Profile matching authority（沿用已冻结）

- **PDU / 0-based address = 唯一内部地址权威**（M11/M12-A 已冻结）；合法 Profile span 无 overlap（§29）⇒ semantic mapping **deterministic**。
- **禁止**自动 `40001 → 0` / `40002 → 1` 或任何 4xxxx alias mapping；**40001 manual display alias 保持 P1 / HUMAN DECISION REQUIRED**（§5/§24 出处），本轮不实现、不偷偷冻结。

### 31.5 Read Result semantic matching（**未冻结项，突出**）

- **`semantic result attaches to start row`（2-word entry 的 semantic value 挂在 start address 行、row 1001 只显示 raw + source-span indication）= 当前 T027 并未冻结** ⇒ 列为 **M12-B HUMAN DECISION REQUIRED（P1-1）**，**不得**写成 Human-approved。
- 已冻结的只有 §29 的 lookup API 语义（`findProfileEntryByStartAddress` 精确 start；`findProfileEntryCoveringAddress` 覆盖 + `offsetWithinSpan`）；未来 UI 用哪种（或两者）由 P1-1 裁定。
- 无论裁定如何：row 1001 **不得**伪装成另一个独立 Float32 semantic start；可显示「属于 1000–1001 的第二个 word」或等价 source-span indication（**文案/视觉形式未冻结**）。

### 31.6 Profile Editor v1 capability（provenance 分层）

- **Canonical** 要求 Profile Editor（`11_V2_UPGRADE_PLAN` §M12-B）；**Human 已批准独立 workspace**（B1）。
- T027 §13 已冻结的**能力句**（引用原文）：「必须能由 Human 查看 / 新增 / 编辑 / 删除 profile metadata / register entries；必须能 Accept / Edit / Reject AI candidate（此项在 C 后接入）」。
- 由此推导的最小 CRUD 清单（查看 Profile / 创建 / 编辑 identity / 删除 / 查看·新增·编辑·删除 register entry / 保存 / 加载打开 / 看到 validation error）= **PROPOSED M12-B V1 CAPABILITY — HUMAN REVIEW REQUIRED**（canonical「Profile Editor」的合理解释，非逐条 Human 批准）。
- Register entry 编辑字段 = M12-A §9 REQUIRED 字段（address / name / description / dataType / registerCount·span / defaultByteOrder / defaultWordOrder / scale / offset / unit）；**不得**新增 40001 alias / read·write access / function-code metadata / bit definitions / vendor 字段（除非 Human 后续批准）。

### 31.7 Editor UI 决策点（未冻结，HUMAN DECISION REQUIRED）

- **registerCount 编辑方式**：A 用户可编辑但必须验证 vs B 由 dataType 自动派生（只读显示）——**不自行决定**（P1-2）。
- **1-word 类型的 wordOrder control**：A 显示但 disabled/N-A vs B 隐藏 vs C 仍可编辑但无实际效果——**不自行决定**（P1-3）。
- byteOrder 继续有效；Editor **不得**重新定义 M11 order semantics（只选择现有 `RegisterByteOrder` / `RegisterWordOrder`）。
- scale / offset / unit：Editor 最终必须允许 Human 明确编辑三 metadata（§24.3 已冻结公式与默认值：scale=1 / offset=0 / unit=""；scale=0 legal；unit 自由文本；无 unit conversion）；**NaN/Inf 不得作为 JSON scale/offset**（M12-A 有限数值要求，不改变）。

### 31.8 Selector / 文案候选（NOT FROZEN）

- Selector 候选状态：`未选择设备档案` / `<displayName>`。**内部身份必须用 profileId**；同名 displayName 的消歧方式（如 displayName + model / secondary text）未冻结（P1-10）。Selector **不得**把 filename/hash 当主要 Human-visible 名称。
- B4 文案候选：`未匹配设备档案` / `无设备档案映射`；Navigation label 候选：`设备档案`（rail 现为「设备」）；icon / 排序位置均未冻结。

### 31.9 File management / 未保存改动 / 生命周期（未冻结清单）

- 文件管理（P1-4/P1-5）：New / Open / Save 之外是否需要 **Save As / Export**；**删除** = 删除 Profile 文件 vs 仅从当前列表移除 + 是否需要确认——均未冻结。**不实现** filesystem picker。
- 未保存改动（P1-6）：MANUAL SAVE 已冻结（§24.2）⇒ 切换 Profile / 关闭 Editor / 退出程序时的 **Save / Discard / Cancel** 行为未冻结。
- 生命周期（P1-7/P1-8/P1-9）：打开/编辑 Profile 是否自动成为 active；删除当前 active Profile 后 active selection 处理；app restart 是否恢复 last active——均未冻结（B5 明确不实现 restore、也不冻结 always-none）。跨 workspace 是否保持：**建议保持**（session 级），但同样待 Human 确认。

### 31.10 M12-B acceptance matrix — **DRAFT / PENDING HUMAN REVIEW**

> 以下全部 = **DRAFT**；**不得**写成 implementation PASS。

| ID | 场景 |
| --- | --- |
| B01 | Device Profile workspace reachable（rail index 5） |
| B02 | create profile |
| B03 | edit identity |
| B04 | register entry add |
| B05 | register entry edit |
| B06 | register entry delete |
| B07 | validation visible |
| B08 | manual save |
| B09 | load / open |
| B10 | Communication Profile selector 存在 |
| B11 | No Profile Selected state |
| B12 | manual Profile selection |
| B13 | selected profile session persistence（process 生命周期内、跨 workspace） |
| B14 | no automatic device inference |
| B15 | raw remains visible |
| B16 | M11 generic decode remains visible |
| B17 | semantic layer separate（三层视觉区分） |
| B18 | matched register semantic name / value / unit |
| B19 | unmatched address explicit（NO PROFILE MAPPING） |
| B20 | profile never changes raw / decode |
| B21 | 2-word source span represented honestly（挂行规则待 P1-1） |
| B22 | no automatic 40001 mapping |
| B23 | 1000×700 |
| B24 | QML diagnostics clean |
| B25 | M10 / M11 regression protected |

### 31.11 Remaining P1（压缩；表述 = HUMAN DECISION REQUIRED）

| ID | 问题 | 出处 |
| --- | --- | --- |
| P1-1 | 2-word semantic value 挂哪一行？建议 start row（**未冻结**） | §31.5 |
| P1-2 | registerCount：editable+validation vs derived/read-only？ | §31.7 |
| P1-3 | 1-word 类型 wordOrder control：disabled / hidden / editable？ | §31.7 |
| P1-4 | 文件 UX 是否需要 Save As / Export？ | §31.9 |
| P1-5 | 删除 Profile 的语义与确认机制？ | §31.9 |
| P1-6 | unsaved changes：Save / Discard / Cancel？ | §31.9 |
| P1-7 | 打开/编辑 Profile 是否自动成为 active？ | §31.9 |
| P1-8 | 删除 active Profile 后 active selection 怎么办？ | §31.9 |
| P1-9 | app restart 是否恢复 last active Profile？ | §31.2 B5 |
| P1-10 | 同名 displayName selector 怎么区分？ | §31.8 |
| P1-11 | 40001 manual display alias 是否进入 M12-B v1？（contract 已标 P1） | §5/§24 |
| P1-12 | read/write access metadata 是否进入 v1？（contract 已标 P1） | §9 |
| P1-13 | function-code / register-family metadata 是否进入 v1？（contract 已标 P1） | §9 |

## 32. Function-Code Register-Space Decision + Schema Compatibility Audit（2026-09-25，docs-only）

### 32.1 Human decision（HUMAN-APPROVED M12 CONTRACT DECISION）

> **Human 明确回复（原文）：「同意」**——批准方案：Profile `RegisterEntry` 增加
> **readFunctionCode**；semantic lookup 以 **(readFunctionCode, PDU address)** 为身份；
> span overlap 禁令**只在同一 readFunctionCode register space 内**生效；不同
> readFunctionCode 允许使用相同 PDU address。
> **不得写成 pre-existing canonical requirement**——这是本轮 Human 才批准的 contract decision。

### 32.2 RegisterSpaceKey（冻结）

```text
RegisterSpaceKey = readFunctionCode + PDU / 0-based address
readFunctionCode = 发送请求时的读取功能码（REQUESTED read function code）
不是：response exception function（requestFunction | 0x80）
不是：response function 推断值
不是：自动设备识别结果
```

- Semantic Profile matching 必须以 **REQUESTED read function code** 为功能码上下文：请求 `FC03 / address 1000` 只能匹配 `readFunctionCode = 0x03` 且 span 包含 1000 的 entry；**不得** fallback 到 FC04 / FC41 / 其它 custom FC（No match ⇒ `NotFound`，无 cross-function fallback）。

### 32.3 Custom read function codes remain first-class（M10 域源码审计，逐字取证）

- **M10 read-function 合法域 = `0x01..0x7F`**：`src/core/active/ActiveRequestIntent.h:145-146`
  `kMinReadFunctionCode = 0x01` / `kMaxReadFunctionCode = 0x7F`；校验 =
  `ActiveRequestIntent.cpp:62-65`（越界 → `ActiveRequestValidationError::ReadFunctionCodeOutOfRange`）。
- 逐字理由（`ActiveRequestIntent.h:140-143`）：「0x80..0xFF belongs to the exception-response
  function-bit space (the analyzers derive exception codes as requestFunction | 0x80) and is
  never a legal ordinary request function」；0x00 不是请求。
- `ReadRequestParsing::parseReadFunctionCode` 仅做文本→字节（两位 hex），范围规则归 intent validator——
  M12 Profile 的 `readFunctionCode` 域 = **复用同一 `0x01..0x7F` 域与现有 error 语义**；custom FC
  （0x41 等）保持 first-class（M10 correction 既有 Human 接受域）。
- **域无冲突 ⇒ §20 STOP 条件未触发**。

### 32.4 Function-scoped overlap rule（冻结；对 §29 的作用域精化）

- §29 的 overlap 禁令作用域**精化**为：**within the same readFunctionCode space**。
- 例：`FC03 UInt32 @1000`（1000–1001）+ `FC03 UInt16 @1001` → **FAIL `overlapping_span`**；
  但 `FC03 UInt16 @1000` + `FC04 UInt16 @1000` → **PASS**；`FC03 UInt32 @1000–1001` +
  `FC41 UInt16 @1001` → **PASS**（不同 readFunctionCode = 不同 register space）。
- 不变量更新：**ONE PDU ADDRESS BELONGS TO AT MOST ONE ENTRY SPAN, PER readFunctionCode space**。
- 仍**不得**引入 priority / fallback / cross-function alias / winner selection。

### 32.5 Lookup future semantics（冻结；实现待后续切片）

- 旧无上下文签名 `findByStartAddress(address)` / `findCoveringAddress(address)` **不足以**作为最终
  semantic lookup authority。冻结未来语义：`findByStartAddress(readFunctionCode, address)` /
  `findCoveringAddress(readFunctionCode, address)`（或架构等价 API——**具体函数名非 Human contract**，
  由 source architecture 决定）。
- **必须保证**：`FC04 / address 1000` 绝不返回 `FC03 / address 1000` entry；No match ⇒ `NotFound`；
  无 cross-function fallback。

### 32.6 Read Result matching context（冻结；实现待 M12-B）

- 未来 semantic overlay 必须从**当前真实请求/事务上下文**取得 requested read function code + PDU
  address；**不能只用 address**；不得从 Profile displayName / COM port / Slave address / response
  bytes 猜 register space。Profile selection 仍由 Human session-level 手工选择（§31 B5），随后在
  Selected Profile 内部按 (requestedFunctionCode, PDU address) 匹配。

### 32.7 JSON register entry contract change（PRE-RELEASE CONTRACT AMENDMENT）

- **Schema 兼容性审计（§8 证据 A–F，只读实测）**：
  - A/C/D = **NO**：M11 Final Package ZIP（`10783914…563d5`）建于 `bc99e6e` tree，`git ls-tree
    bc99e6e -- src/core/profile/` = **0 文件** ⇒ M12-A 特性不在包内；M12-A 从未进 canonical package /
    Final D（打包后各轮明令 NO package）。
  - B = **NO**：repo 无任何 committed profile JSON（`git ls-files` 仅 CMake presets；测试 fixture 全在
    QTemporaryDir）⇒ 无 Human-created/accepted production Profile file。
  - E = **YES**：`test_device_profile.cpp` 是唯一已有 JSON consumer。
  - F = **NO** 外部兼容性承诺：T027 §24.2 的 schemaVersion=1 是 **pre-release 内部 contract freeze**；
    无外部用户 / 无外部承诺记载。
- **裁定（按 §9 规则的推荐分支）**：**KEEP schemaVersion = 1**；`readFunctionCode` 以
  **PRE-RELEASE CONTRACT AMENDMENT** 加入**尚未发布的 v1 schema**——**这不是 migration**；
  旧 internal/test fixtures 同步更新；**不承诺**兼容尚未发布的中间开发格式。
- **目标 entry shape（v1 修订，实现待后续切片）**：在 §25 shape 的 entry 中新增
  `"readFunctionCode": <int>`（键序：`readFunctionCode` 置于 `address` 之前）。
- **missing `readFunctionCode` policy（冻结）**：修订后的 v1 schema 中 **readFunctionCode = REQUIRED**；
  **缺失 ⇒ load validation FAIL**；**禁止** silent default 0x03（M12 支持 FC03/FC04/custom，
  silent 03 会制造错误语义）；与 §9 裁定一致（schema 本身修订，缺字段即非 conforming v1 文档）。
- 合法域 = `0x01..0x7F`（§32.3）；范围校验进入未来实现的 validation（本轮不实现）。

### 32.8 RegisterEntry REQUIRED fields（更新后）

```text
readFunctionCode        ← 新增 REQUIRED（register-space identity；0x01..0x7F）
address / name / description / dataType / registerCount·span /
defaultByteOrder / defaultWordOrder / scale / offset / unit   ← 原 REQUIRED 不变
```

- **readFunctionCode ≠ read/write access metadata**：它是 semantic register-space identity。
- **P1 拆分**：原「function-code/register-family metadata」问题中——`readFunctionCode` 部分 =
  **APPROVED / REQUIRED**；**其它 register-family metadata = 仍未冻结（P1-13-remnant）**；
  read/write access metadata = 仍 P1 / DEFER candidate（P1-12）。

### 32.9 Profile Editor consequence（M12-B，实现待后续）

- Editor RegisterEntry UI 必须允许 Human **看到/编辑 读取功能码**；支持 custom code（复用 M10 已接受
  语义与 `0x01..0x7F` 域）；**不得**只提供 FC03/FC04 两个固定 option。具体输入控件形态
  （text input / editable selector）留待 M12-B implementation contract。

### 32.10 不变项

- **2-word semantic start-row 规则仍未冻结**（P1-1 保持；Human 本轮只批准 function-code decision，
  未批准 start-row——不得越权）。
- M12-B Implementation = NOT STARTED；M12-C/D = NOT STARTED。
- verified LKGC = `bc99e6ea871628a3a685b9cf80cf3840e7b3b171`（不变）。

### 32.11 Remaining P1（更新后）

```text
P1-1   2-word semantic value 挂行（仍未冻结）
P1-2   registerCount UI（editable+validate vs derived/read-only）
P1-3   1-word 类型 wordOrder control（disabled/hidden/editable）
P1-4   Save As / Export
P1-5   删除语义与确认
P1-6   unsaved changes（Save/Discard/Cancel）
P1-7   Editor-open 自动 active？
P1-8   删除 active Profile 后 active selection
P1-9   app restart 恢复 last active
P1-10  同名 displayName 消歧
P1-11  40001 manual display alias（保持 P1）
P1-12  read/write access metadata（保持 P1）
P1-13-remnant  其它 register-family metadata（readFunctionCode 部分已批准移出）
```

## 33. M12-B Final P1 Contract Freeze（2026-09-26，HUMAN-APPROVED，docs-only）

> **Human 明确回复（原文）：「这四组都同意」⇒ Group 1–4 全部为 HUMAN-APPROVED M12-B CONTRACT
> DECISIONS（非 pre-existing canonical requirement）。本轮 = docs-only freeze；M12-B Implementation
> 仍 NOT STARTED。**

### 33.1 Group 1 — 2-word semantic + editor field behavior（冻结）

- **A. 2-word Profile semantic value 挂在 RegisterEntry 的 start row**（例：FC03 Float32@1000，
  span 1000–1001 ⇒ semantic value 只挂 row 1000）。**row 1001 仍保留该 row 原本的 raw truth 与
  M11 generic decode behavior**——**M11 已冻结的 sliding-window decode 不得被 M12 改写**（words[i] +
  words[i+1] 照旧）。「row 1001 属于 Profile span 1000–1001 的第二个 word」**不得**解释成「M11 row
  1001 不再 generic decode」；M12 只允许**额外**表示 Profile span membership / source span；row 1001
  **不得**生成第二份同一 entry 的 semantic value。
- **B. registerCount 在 Profile Editor 中由 dataType 自动派生（UI read-only/derived，不得手工编辑）**；
  规则不变（Hex/Binary/UInt16/Int16 → 1；UInt32/Int32/Float32 → 2）；**底层 schema 仍保留 registerCount**
  用于 persistence/validation，**loader 仍必须验证一致**——不得因 UI derived 删除 schema validation。
- **C. 1-word 类型：wordOrder control 仍显示但 disabled，显示「不适用」或现有风格等价表达**（不隐藏，
  避免布局跳动）；2-word：enabled。byteOrder 全类型继续正常可编辑。

### 33.2 Group 2 — file / save / dirty state（冻结）

- M12-B v1 支持 **New / Open / Save / Delete**；**暂不提供**独立 Save As / Export（JSON 本身仍是
  portable Profile format）。**Open = 打开应用 managed Profile store 中的既有 Profile**——**不是**
  任意外部 JSON import（外部导入属后续 import UX，不得借 Open 实现）。MANUAL SAVE 保持冻结。
- **DIRTY STATE**：当前编辑 Profile 有未保存修改时，在（切换编辑对象 / 关闭当前编辑对象 / 退出程序 /
  删除当前 Profile）之前必须提供 **Save / Discard / Cancel** 三路决策：Cancel = 中止原动作；Discard =
  放弃未保存修改继续；Save = 先尝试保存，**Save 失败 ⇒ 原动作不得继续**，保持当前编辑状态并显示保存失败。
- **DELETE**：destructive operation——dirty-state resolution 完成后**仍须显式删除确认**（概念：
  「确定删除设备档案"<displayName>"吗？」；精确文案按现有中文风格实现，语义 = explicit confirmation）；
  确认后删除 managed Profile 对应 JSON；**删除失败不得伪装成功**。

### 33.3 Group 3 — editor vs active profile（冻结）

- **正在编辑的 Profile（Editor）与 Active Profile（Communication semantic interpretation 用）是两个
  独立状态**；打开 Editor **不得**自动把该 Profile 设为 Active；编辑另一个 Profile **不得**自动改变
  Active。
- **Active Profile lifecycle**：application process 期间人工选择；**跨 workspace 保持**；**应用重启：
  M12-B v1 不恢复上次 Active Profile（启动后 = No Profile Selected）**——本条由前轮「NOT YET FROZEN」
  正式转为冻结；**禁止** restore last / 按 COM / 按 Slave / 按 FC / 按 response / 按 manufacturer·model
  自动选择。
- **DELETE ACTIVE PROFILE**：删除成功后**立即清空 Active Profile**（→ No Profile Selected）；raw truth
  与 M11 generic decode **继续可用**；M12 semantic **不得**继续使用已删除 Profile 的缓存解释。

### 33.4 Group 4 — v1 metadata boundary（冻结）

- M12-B v1 **DEFER**：40001 / 4xxxx manual display alias；read/write access metadata；除
  readFunctionCode 外的其它 register-family metadata；独立 Save As / Export UX——**不得**在第一版偷偷加入。
- **readFunctionCode = REQUIRED register-space identity，不属于 DEFER**（T027 §32 已批准）。
- **DUPLICATE displayName**：selector / profile list 内部 identity 永远使用完整 **profileId**；
  Human-visible primary text = **displayName**；secondary text = **manufacturer / model / revision**
  （存在的字段）；仍同名 ⇒ 增加**短 profileId** 消歧（缩短算法 = implementation detail，但最终 UI 必须
  能区分两个不同 profileId；短形式仍碰撞 ⇒ 扩展显示长度，必要时完整 profileId）；**不得**因显示字符串
  相同而视为同一 identity；**filename/hash 不得**作为主要 Human-visible 名称。

### 33.5 M11 non-regression（本轮冻结声明）

- **M11 sliding-window semantics 不被 M12 start-row rule 修改**：raw row i 的 generic 32-bit decode
  仍按 words[i] + words[i+1]；M12 只决定 semantic mapping 属于哪个 Profile entry start row；不得修改
  `RegisterDecode` / M11 word-order / M11 byte-order / raw rows。本轮实现目标：**RegisterDecode.{h,cpp}
  ZERO DIFF**（如必须修改 ⇒ STOP）。

## 34. P1 Accounting Correction（BEFORE / AFTER，逐项核对 T027）

- **账目更正**：上一轮报告顶部「14 项 P1」为**计数笔误**——正式编号列表 = **P1-1..P1-13（13 项）**。
  未为凑数新增任何 P1。
- **BEFORE（上轮冻结时点）**：P1-1 .. P1-13 全部 = HUMAN DECISION REQUIRED。

| ID | 主题 | AFTER（本轮四组后） |
| --- | --- | --- |
| P1-1 | 2-word semantic 挂行 | **RESOLVED（Group 1-A）**：挂 start row；M11 sliding-window 不变 |
| P1-2 | registerCount UI | **RESOLVED（Group 1-B）**：dataType 派生、UI read-only；schema validation 保留 |
| P1-3 | 1-word wordOrder control | **RESOLVED（Group 1-C）**：显示但 disabled（「不适用」）；不隐藏 |
| P1-4 | Save As / Export | **RESOLVED（Group 2）**：v1 = New/Open/Save/Delete，无 Save As/Export ⇒ **DEFER** |
| P1-5 | 删除语义与确认 | **RESOLVED（Group 2）**：dirty resolution + 显式确认 + 删 managed JSON + 失败不伪装 |
| P1-6 | unsaved changes | **RESOLVED（Group 2）**：Save/Discard/Cancel 三路；Save 失败阻断原动作 |
| P1-7 | Editor-open 自动 active | **RESOLVED（Group 3）**：不自动；Editor 与 Active 为独立状态 |
| P1-8 | 删除 active Profile 后 | **RESOLVED（Group 3）**：清空 → No Profile Selected；semantic 缓存不沿用 |
| P1-9 | restart 恢复 | **RESOLVED（Group 3）**：v1 不恢复，启动 = No Profile Selected（原 NOT YET FROZEN 转冻结） |
| P1-10 | 同名 displayName | **RESOLVED（Group 4）**：profileId 内部 + displayName 主 + manufacturer/model/revision 次 + 短 profileId 消歧 |
| P1-11 | 40001 manual alias | **DEFER**（Group 4） |
| P1-12 | read/write access metadata | **DEFER**（Group 4） |
| P1-13-remnant | 其它 register-family metadata | **DEFER**（Group 4；readFunctionCode 已批准移出） |

- **结论（诚实判断，非人为清零）**：**真正阻塞 M12-B first implementation slice 的 Human P1 = 0**；
  P1-11 / P1-12 / P1-13-remnant = DEFER（非阻塞的 v1 外延功能）。
- **本轮随之执行的行为 = T027 §32 的 readFunctionCode PRE-RELEASE AMENDMENT**（M12-A latest
  contract amendment，PENDING IMPLEMENTATION → IMPLEMENTED，见 §35）。

## 35. readFunctionCode Amendment — Implementation Archive（2026-09-26，behavior-bearing）

> **M12-A readFunctionCode PRE-RELEASE AMENDMENT = IMPLEMENTED / AUTOMATED PASS。**
> 行为提交 = `ec9823c51e1a552438483ec85163129b65de5295`（「M12: scope profile data by read function」；
> docs-freeze 先行 = `ed3b2e59412eb343b36c83c1e7c38b61fd2c9183`）。
> **Human visual review = NOT REQUIRED**（零 UI，不制造 Human PASS）。

### 35.1 实现内容（§32 落地）

- **模型**：`RegisterEntry.readFunctionCode`（`std::uint8_t`）——**内存默认 = 无效 sentinel 0**
  （新建 entry 不可能静默成为 FC03；调用者必须显式赋值）；合法域 `0x01..0x7F`（镜像 M10
  `kMinReadFunctionCode` / `kMaxReadFunctionCode`）。
- **验证**：新错误码 `InvalidReadFunctionCode`（token `invalid_read_function_code`）；entry 级检查
  （`registerIndex` 可定位）；0x00 与 0x80..0xFF（异常响应位空间）全拒。
- **Function-scoped validation**：placed spans 以 `(readFunctionCode, [first, last])` 键控——
  `duplicate_address` / `overlapping_span` 仅在同 FC space 内触发；跨 FC 同地址合法。
- **JSON**：serialize **恒写** `"readFunctionCode"`（integer）；parse **必需**（missing / fractional /
  string → 结构性拒绝 `malformed_json`；integer 但 0 或 ≥128 → `invalid_profile` +
  `invalid_read_function_code`）；**无 silent default 03**；schemaVersion **保持 1**（PRE-RELEASE
  CONTRACT AMENDMENT，非 migration——§32.7 证据 A–F）。
- **Lookup API（替换，无生产兼容义务——源码审计证实仅测试消费）**：
  `findProfileEntryByStartAddress(profile, readFunctionCode, address)` /
  `findProfileEntryCoveringAddress(profile, readFunctionCode, address)`；先 validation；仅搜索 requested
  FC space；无效 requested FC ⇒ `NotFound`；无 cross-function fallback；`Ambiguous` defensive-only。
- **Projection**：`ProfileSemanticProjection.readFunctionCode` 作为 source metadata；公式与 IEEE 特殊值
  行为零改动。

### 35.2 测试 / 负向对照 / 回归（真实数字）

```text
device_profile = 114 passed（原 82 + RF01–13 + RS01–08 + RL01–11；旧 fixture 全部补显式 FC——
  sentinel 0 被拒绝正是「无 silent FC03」设计的直接验证）
负向对照（真实 mutate → FAIL → 恢复，无 mutation 提交）：
  NC-RF1（missing FC 默认 03）→ RF09 + RF13 FAIL
  NC-RF2（global overlap 不按 FC 分组）→ RS01/02/03/07 + RL04 FAIL
  NC-RF3（lookup 忽略 FC）→ RL02/03/04/05/07/08 FAIL
Debug build 0 error + ctest 42/42；Release build 0 error + ctest 42/42（M10/M11 与 QML gates 全绿）
RegisterDecode.{h,cpp} = ZERO DIFF（M11 sliding-window 语义未被触碰，§33.5 声明兑现）
```

### 35.3 状态

```text
M12-A latest contract amendment（readFunctionCode）= IMPLEMENTED / AUTOMATED PASS
M12-A FOUNDATION = ACCEPTED INCLUDING readFunctionCode PRE-RELEASE AMENDMENT
M12-B = CONTRACT / UI DEFINITION（Group 1–4 已冻结；P1-1..10 RESOLVED、P1-11/12/13-remnant DEFER；
  阻塞 first slice 的 Human P1 = 0）
M12-B Implementation / M12-C / M12-D = NOT STARTED
未 package（M11 final package 保持历史 VERIFIED）
verified LKGC = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（不推进；本行为提交仅为 M12 candidate）
```

## 36. M12-B First Slice — Device Profile Workspace Archive（2026-09-26，behavior-bearing）

> **M12-B first slice = IMPLEMENTED / AUTOMATED PASS（43/44 ctest；8 gate 检查项需 Human visual review）。**
> 行为提交 = `da07f4330c13dad40e816cb89f9905a954016dad`；**Human visual review = PENDING**。

### 36.1 实现范围

- **独立 Device Profile workspace**（rail index 5「设备」启用；`workspaceDeviceIndex=5` 不变；`workspaceHost` StackLayout 新增 `DeviceProfilePage`）。
- **Profile identity editor**：displayName（REQUIRED）/ manufacturer / model / revision / description（OPTIONAL）；**profileId 只读显示**（程序生成 QUuid，不可编辑——B1-Q05）。
- **Managed catalog**：扫描 `ProfileStore::managedProfilesDirectory()` 中的 `profile-*.json`；valid profiles 确定性排序（displayName → profileId）；malformed 文件 → issue count + 非阻塞警告（不删除/不修复/不忽略）。
- **File lifecycle**：New（QUuid + 空身份）/ Open（managed store）/ Save（validate + QSaveFile 原子写）/ Delete（confirm 后删 managed JSON）；dirty = draft ≠ persisted（computed）。
- **Dirty-state protection**：Save/Discard/Cancel 三路（切换/关闭/退出/删除前）；Save 失败阻断原动作；exit dirty guard（Cancel 真正阻止 close；Save 失败阻止 close）。
- **Editor ≠ Active**：编辑器与 Active Profile 独立；打开/编辑不自动 active；Active Profile service 本轮不实现。
- **Accessibility**：全部交互控件 Accessible.name。

### 36.2 门禁（真实 exit code）

| 检查 | Debug (offscreen) | Release staging (windows QPA, clean PATH) |
|---|---|---|
| qml-smoke-test | — | 0 |
| qml-nav-check | 0 | 0 |
| qml-geometry-check | 0 | 0 |
| qml-read-result-check | 0 | 0 |
| qml-production-write-check | 0 | 0 |
| qml-focus-check | 0 | 0 |
| qml-profile-editor-check | 1（7 项） | 1（8 项） |
| profile_controller (23 tests) | 0 | — |
| device_profile (114 tests) | 0 | — |
| Debug full CTest | **43/44** | — |
| Release full CTest | — | **43/44** |

### 36.3 已知 gate 限制（offscreen/automation 限制，非产品缺陷）

- **Dialog（QQuickPopup）按钮交互**：Save/Discard 确认、Delete 确认的按钮点击无法通过合成 QMouseEvent 驱动（offscreen/windows automation 限制）；**core 逻辑由 23 个 controller tests 覆盖**。
- **卡片几何 16px 溢出**：profileCatalogCard/IdentityCard 底边超出 700px 约 16px（因为 GridLayout 内容的 implicit height 略高于可用空间）；`clip: true` 防止视觉溢出；Human visual review 验证实际外观。

### 36.4 Visual candidate

- **路径**：`build\m12b-visual-candidate\ModbusLens.exe`
- **size**：4896570 B；**SHA-256**：`c6d68369f9cf28edbcef7aa1d1b9012e5e46c500d4f48bdc7d259fe38ec5a7c6`
- **source == staging**（byte-identical cmp）；launcher = `Run-M12-Profile-Editor.cmd`
- **clean-env gates**：smoke=0 / nav=0 / geometry=0 / read-result=0 / production-write=0 / focus=0
- **≠ canonical package / ≠ Final D / ≠ LKGC**

### 36.5 状态

```text
M12-A = FOUNDATION ACCEPTED（含 readFunctionCode amendment）
M12-B first slice = IMPLEMENTED / AUTOMATED PASS / HUMAN VISUAL REVIEW PENDING
M12-B remaining = Register Map Editor / Communication selector / Semantic overlay
M12-C / M12-D = NOT STARTED
verified LKGC = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（不变）
```

## 37. Automated Acceptance Correction — 7 项失败逐项 RCA + 修复 + 真实负向对照（2026-09-26，behavior `b502ea8` + 本 docs commit）

> **本节是对 §36 的追加纠正（append-correction），不覆盖 §36 原文。**
> Human 指令：在门禁真正转绿前不得写 AUTOMATED PASS；禁止先假设
> "QQuickPopup = automation limitation"；`clip: true` 不得作为 geometry
> PASS 的唯一理由；NC-B4 必须做真实 mutation。

### 37.1 状态纠正（对 §36.5 的显式更正）

§36.5 写的 `M12-B first slice = IMPLEMENTED / AUTOMATED PASS` 在当时是**错的**：
`qml_profile_editor_check` 实际 7 项 FAIL（Debug/Release 43/43→43/44 ctest）。
当时正确状态应为 **IMPLEMENTED / AUTOMATED ACCEPTANCE HOLD**。本节归档后
（修复 + 全绿证据齐全），状态才升级为 AUTOMATED PASS（见 37.6）。

### 37.2 §36.3 两个 "gate 限制" 结论均被推翻

| §36.3 原结论 | 本轮证据 | 实际分类 |
| --- | --- | --- |
| "Dialog 按钮点击无法通过合成 QMouseEvent 驱动（automation 限制）" | stage 7/8/9/10/11c/11d 的对话框按钮全部由 `clickNamed`（真实 press/release 到 window）驱动成功 | **harness 缺陷（B）+ 产品缺陷（A）叠加**：真正堵住后续点击的是 dirty dialog 从不关闭的模态 overlay，不是"无法点击" |
| "卡片 16px 溢出，用 clip: true 防视觉溢出" | 11a/11b 拆分后实测：卡片 bottom=696 < 700，无溢出 | **harness 缺陷（B）**：同一 stage 内 resize+measure 读到 resize 前的布局 |

### 37.3 7 项失败逐项分类（A=真实产品缺陷 B=harness 缺陷 C=不可自动化 D=oracle 错误）

| # | 失败（修复前 PROFFAIL） | 分类 | 根因（证据） | 修复 |
| --- | --- | --- | --- | --- |
| 1 | the validation text is not visible | **A**（ISSUE-019） | `setDisplayName()` 无 `emitEditorChanged()`——NC-B1 负向对照变异被提交进行为提交 `da07f43`；getter 按需计算使 23 个 C++ 测试全绿 | 恢复 emit + 删除变异注释；新增 b1c21/b1c22 QSignalSpy 通知契约测试 |
| 2 | the Save branch did not save the draft | **D**（oracle 错）| stage 9 断言 `catalogRow(1)=="Renamed inverter"`，但保存后目录按 displayName 重排，Renamed 落到 row 0（`catalog=[Renamed inverter\|Second device]` 实测） | 改为 index-independent 的 `catalogContains()` |
| 3 | the Save branch did not open the target | **A**（对话框不关闭）| stage 8 Discard 分支不关 dirtyDialog，模态 overlay 吞掉 stage 9 的 Open 点击 → pendingAction 为空 → 只保存未打开（`dirtyDlg=1` 贯穿 9→10 实测） | Save/Discard 分支在 runPendingAction 前显式 `dirtyDialog.close()`；gate 新增"分支后对话框必须关闭"断言 |
| 4 | the delete confirmation did not appear | **A**（同上链式）| 同 #3：stage 10 的 Delete 点击被未关闭的模态 overlay 吞掉 | 同 #3 |
| 5 | the confirm-delete button is not clickable | **A**（同上链式）| deleteDialog 从未打开（#4），其按钮不可见 | 同 #3 |
| 6 | confirmed delete did not remove the profile | **A**（同上链式）| deleteProfile 从未被调用 | 同 #3 + 确认后清空选中（Open/Delete 禁用，防越界索引 TypeError） |
| 7 | profileCatalogCard/IdentityCard outside（16px 溢出） | **B** | 同一 stage 内 `resize(1000,700)` 后立即 measure，读到旧窗口布局 | 11a（resize）/11b（measure）拆分；实测无溢出；**移除页根 `clip: true`**，geometry 断言度量的是真实可见性（列表 Flickable 的 clip 属设计内） |

附加（未在 7 项内、由 §6 要求补齐）：**Save 失败必须阻断 pending action**
（新 stage 9b：对话框保持打开 + lastActionError 非空 + 目标未打开）与
**exit + Save 失败必须拒绝关闭**（新 stage 15）。

### 37.4 NC 状态

- **NC-B1（真实 mutation + 还原闭环）**：本轮发现的残留变异本身就是 NC-B1
  的真实红色证据（stage 6 FAIL）；还原（恢复 emit）后转绿。详见 ISSUE-019。
- **NC-B2**（open 失败保 draft）/ **NC-B3**（profileId 稳定）：b1c16 /
  b1c09-b1c10 在 35/35 全绿中复验通过。
- **NC-B4（本轮真实执行）**：mutation = Cancel 分支追加 `root.close()`。
  - 第一次尝试：门禁**仍 PASS** —— 负向对照本身暴露 stage 14 盲区：onClosing
    守卫（dirty ⇒ close.accepted=false）把 Cancel 触发的 close 也拦下并
    重开对话框，两个行为等价实现无法区分。
  - 加强断言（"Cancel 后 exit 对话框必须关闭"）→ **mutation 下真实 FAIL**
    （`PROFFAIL: the exit dialog did not close on Cancel`）→ 还原 → PASS。
  - mutation 未提交（Main.qml 净 diff 为 0，git status 证实）。

### 37.5 §6 dirty 状态机 C++ 覆盖（b1c21–b1c32，12 个新测试）

通知契约：b1c21（displayName 编辑通知恰 1 次/幂等 0 次）、b1c22（5 setter）。
状态机：b1c23 dirty+Cancel 全保持（磁盘未写）、b1c24 Discard→打开目标且废弃
编辑不上盘、b1c25 Save 成功→先落盘再打开、b1c26 保存失败（校验）阻断、
b1c27 保存失败（IO）保 draft 与文件、b1c28 精确 id 才可删、b1c29 删除成功
清文件清编辑器、b1c30 删除失败保留档案、b1c31 exit+Cancel 维持拒绝谓词、
b1c32 exit+Save 失败维持拒绝谓词。`profile_controller`：23→**35 passed**。

### 37.6 最终验证（真实命令与输出口径）

```text
profile_controller            35 passed, 0 failed
Debug  full ctest             100% tests passed, 0 failed out of 44
Release full ctest            100% tests passed, 0 failed out of 44
qml_profile_editor_check      PASS（16 阶段，含 9b/15/11c/11d）
windows-QPA（windows 平台，真实 PIPESTATUS 退出码）
  smoke/nav/geometry/read-result/production-write/write-foundation/
  focus/profile-editor        全部 exit=0；诊断计数 0/0/0/0/0/0/0/0；
  qrc: 警告行 0
staging 重建                  source==staging byte-identical (cmp)
  size=4,915,758 B  SHA-256=cccba339b5a02d605f45fdbfef57c0a4ec136806751f2d550258e67c413ab8a2
clean-env（PATH=System32;Windows）smoke / profile-editor / nav / geometry
                              全部 exit=0 且含 PASS 标记
```

1000×700 实测几何（stage 11b PROFGEO）：workspace 943×659@(57,41)、
actions row 4 按钮 y=72 h=34、catalogCard 340×555@(61,141)、
catalogList 316×489、identityCard 583×555@(413,141)、displayName 字段
492×24@(492,358)、description 字段 @(492,486)；两对话框 380×94@(339,324)
（经 popup background 度量）——全部 contained。

### 37.7 提交与状态

- behavior/test commit：**`b502ea8`**（4 文件：ProfileController.cpp /
  DeviceProfilePage.qml / main.cpp / test_profile_controller.cpp；无 docs）
- docs-only commit：本节 + ISSUE-019 + PROJECT_STATUS + BACKLOG + devlog
- 状态：

```text
M12-A = FOUNDATION ACCEPTED（含 readFunctionCode amendment）
M12-B first slice = IMPLEMENTED / AUTOMATED PASS / HUMAN VISUAL REVIEW PENDING
visual candidate = READY FOR HUMAN VISUAL REVIEW（staging 全绿）
M12-B remaining = Register Map Editor / Communication selector / Semantic overlay
M12-C / M12-D = NOT STARTED
verified LKGC = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（不变）
```

## 38. M12-B First Slice — HUMAN ACCEPTED（2026-09-26，docs-only）

Human 原文（逐字归档）：

> “M12-B 第一切片全部 PASS，1000×700 正常，保存/放弃/取消和删除正常”

Human verified（仅限原文明确提及的范围，不做外推）：

- Device Profile workspace（第一切片整体 = 全部 PASS）
- 1000×700 显示正常
- 保存 / 放弃 / 取消（dirty 三路）正常
- 删除正常

**未声称 Human 验证了原文未提及的其它细节**（accessibility、exit guard、
malformed catalog 提示等仅由自动化门禁覆盖）。

状态变更：

```text
M12-B First Slice = IMPLEMENTED / AUTOMATED PASS / HUMAN VISUAL REVIEW PENDING
                  ⇒ IMPLEMENTED / AUTOMATED PASS / HUMAN ACCEPTED
verified LKGC     = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（不变，不自动推进）
```

M12-B Second Slice（Register Map Editor）= NOT STARTED。

## 39. M12-B Second Slice — Register Map Editor Archive（2026-09-26，behavior `becadc5`）

> 范围（Human 指令 §4 硬边界）：Register Map list + Add/Edit/Delete Entry +
> entry validation UI + dirty integration + 11 字段编辑（readFunctionCode /
> address / name / description / dataType / registerCount / byteOrder /
> wordOrder / scale / offset / unit）。**未实现**：Active Profile、
> Communication selector、semantic overlay、register live read、wire dispatch、
> 40001 alias、access metadata、Save As/Export（全部 NOT STARTED / DEFER）。
> Human First-Slice Acceptance 已先行归档（§38，commit `0d146f2b`）。

### 39.1 实现形态（actual layout + field behavior）

- **布局**：Device Profile workspace 三卡（catalog 250 | identity fillWidth |
  register 330）；register 卡 = 摘要行列表（`FC03 @1000 ｜ 名称 ｜ UInt16 @1000`）
  + Add/Edit/Delete 按钮行 + 「地址为 PDU / 0-based（十进制输入）」authority 提示。
- **Entry editor**：有界 Dialog（480×436 @1000×700，popup background 实测）承载
  全部 11 字段；Add/Edit 共用，Add 时 FC 字段**初始为空**（no silent FC03，
  §32/§35 纪律）。
- **readFunctionCode**：keyboard-editable TextField；复用 core
  `parseReadFunctionCode`（M10 输入习惯：`03`/`3`/`41`/`0x41`，1..2 位 HEX，
  0x 前缀可选）；域 0x01..0x7F（00/80/FF 拒绝，token
  `invalid_read_function_code`）；**无 silent default 03**（gate stage 3 断言）。
- **address**：PDU / 0-based 十进制输入（复用 core
  `parseDecimalRegisterValue`，与 M10 Start Address 输入一致）；HEX 仅为
  显示辅助（列表行），canonical truth 始终是 0-based 整数；无 40001。
- **dataType**：直接引用 M11 七类型枚举（index = enum value；无第二套 enum）；
  ComboBox 七项 HEX/BIN/UInt16/Int16/UInt32/Int32/Float32。
- **registerCount**：UI read-only，由 `registerDecodeTypeWordCount(dataType)`
  派生（1-word → 1 / 2-word → 2，Group 1-B）；persistence 保留该字段、
  validation 仍严格校验；切换 dataType 立即重派生。
- **byteOrder**：全类型可编辑（Normal / ByteSwapped，M11 枚举）。
- **wordOrder**：2-word enabled（HighWordFirst / LowWordFirst）；1-word
  **显示但 disabled + 「不适用（1-word 类型）」标签**（Group 1-C，不隐藏）；
  1-word 时 UI 传 -1，controller 保持 M11 默认 —— disabled 控件永不污染数据。
- **scale / offset / unit**：可编辑；空 scale/offset = 冻结默认 1 / 0；
  scale=0 合法；NaN / ±Inf / 非数字文本拒绝（`non_finite_scale` /
  `non_finite_offset`）；unit 自由文本（round-trip 含 `°C`）；无任何自动换算。

### 39.2 语义（Add / Edit / Delete / dirty）

- **candidate 纪律**：Add/Edit 构造 candidate entry → 复制整个 draft → 替换/
  追加 → 跑**完整** profile validation → 仅 valid 才提交回 draft。
  失败 = draft 逐字节不变 + entry editor 保留全部输入 + 明确错误。
- **错误呈现（§24）**：human 文本（点名冲突条目：「同一功能码 FC03 空间内地址
  1000 已被「Frequency」占用（重复地址）」）+ 稳定 token
  （`lastActionErrorToken`：duplicate_address / overlapping_span /
  invalid_read_function_code / register_name_missing / non_finite_scale /
  non_finite_offset / register_entry_missing / save_failed / delete_failed）。
- **Delete entry**：draft-only（T027 §16），JSON 只经 Save 落盘；Discard
  完整恢复 persisted register map（b2c14）；无新增 destructive FS 语义。
- **dirty**：register 变更与 identity 共用同一 dirty 真值（draft !=
  persisted；无第二 dirty flag）；Save 成功 → dirty false；Save 失败 →
  dirty true 且 register 变更保留（b2c30）。
- **排序/选择**：display 投影按 (readFunctionCode, address) 确定性排序；
  编辑定位用 draftIndex（对重排稳定）；不新增 entryId/UUID。

### 39.3 测试 / 负向对照 / 回归（真实数字）

```text
profile_controller = 65 passed（35 + B2-C01..B2-C30）
  B2-C01..C30 覆盖：空表 / FC03+FC41 添加 / FC00·FC80 拒绝 / cross-FC 同址 /
  same-FC duplicate·overlap 拒绝 / cross-FC overlap 合法 / edit / 失败 edit
  不变 / delete / delete→dirty / discard 恢复 / registerCount 派生（1·2）/
  scale=0 / NaN·Inf scale·offset 拒绝 / unit·byteOrder·wordOrder round-trip /
  save+load 全字段（含 FC03@1000+FC04@1000 共存）/ profileId 稳定 /
  M11 decode 黑盒不变 / 确定性排序 / 无效 candidate 不改 draft ×2 /
  dirty guard 参与感 / save 失败保留 dirty
新 QML gate qml_register_map_check（--qml-register-map-check，19 stages）：
  B2-Q01..Q30 全覆盖；REGISTER MAP CHECK PASS
负向对照（真实 mutate → 观察 FAIL → 精确还原 → 复绿；无 mutation 提交）：
  NC-B2-1 validation 忽略 FC 分组        → b2c06 + b2c09 FAIL
  NC-B2-2 registerCount 派生固定为 1     → b2c16 FAIL
  NC-B2-3 add 路径伪造 clean（不 dirty）  → b2c02 + b2c29 FAIL
  NC-B2-4 edit 先 commit 后 validation   → b2c11 + b2c28 FAIL
Debug  full ctest = 45/45（新增 qml_register_map_check）
Release full ctest = 45/45
RegisterDecode.{h,cpp} = ZERO DIFF（§33.5 兑现；git diff --stat 为空）
windows-QPA（真实 windows 平台）：9 门（8 原有 + register-map）全 exit=0，
  诊断（ReferenceError/TypeError/Unable to assign/String.arg Invalid）= 0，
  qrc 警告行 = 0
```

### 39.4 1000×700 实测几何（REGGEO，resize 与 measure 分 stage）

```text
profileRegisterCard    x=666 y=118 w=330 h=578   （bottom=696 ≤ 700）
profileRegisterList    x=678 y=214 w=306 h=470
Add/Edit/Delete 按钮   y=153 h=34
entryEditorDialog      x=289 y=153 w=480 h=436   （真实打开后 popup background 度量；
                                                  11 字段 + Apply/Cancel 全部 contained）
无 clip 隐藏 overflow（first slice 已移除页根 clip）
```

### 39.5 提交与状态

```text
Human first-slice acceptance commit = 0d146f2b（M12: accept profile workspace slice）
behavior commit                     = becadc5f6b77878188fcc41cc2980a865361f580
                                      （M12: add register-map editor；6 文件，无 docs）
docs archive commit                 = 本 commit（M12: archive register-map editor slice）
staging = build/m12b-visual-candidate/ModbusLens.exe
  size = 5,129,023 B  SHA-256 = 29c05471f69b4671b3a09c260d7b6fea1870e8ee956b2586b79fe9c3dd446c8b
  source == staging（cmp byte-identical）
clean-env（PATH=System32;Windows）: smoke / profile-editor / register-map / nav
  / geometry 全 exit=0 且含 PASS 标记；visual candidate ≠ canonical package

M10 = COMPLETE；M11 = COMPLETE；M11 FINAL PORTABLE PACKAGE = VERIFIED
M12-A = FOUNDATION ACCEPTED；M12-B First Slice = HUMAN ACCEPTED
M12-B Second Slice = IMPLEMENTED / AUTOMATED PASS / HUMAN VISUAL REVIEW PENDING
M12-B remaining = Communication selector / Active Profile state / semantic overlay
M12-C / M12-D = NOT STARTED
verified LKGC = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（不变，NO advance）
```

### 39.6 过程记录（诚实档案）

- 实现中一个真实缺陷在迭代中被自家 gate 抓住并修复：controller 对空
  scale/offset 的默认分支先写了 `x = empty ? default : toDouble(&ok)` 再统一
  判 `!ok`，导致空串（走默认）也被拒（non_finite_offset）；修复为分支内各自
  判定。该缺陷只存在于未提交的工作树中，未进入任何提交。
- 负向对照还原操作事故：NC-B2-1 首次还原误用 `git checkout -- <file>`，把
  本轮未提交的 ProfileController.cpp 实现一并回退。处置：立即以全量重写重建
  （头文件/测试/QML/gate 均未受影响），重建后 65/65 + 双 gate 复绿，无净损失；
  其余三个 mutation 改用精确逆向 patch 还原。教训：负向对照的还原必须是精确
  逆向 patch，`git checkout` 只能用于无未提交工作的文件。

## 40. M12-B Second Slice — HUMAN ACCEPTED（2026-09-26，docs-only）

Human 原文（逐字归档）：

> “M12-B 第二切片全部 PASS，Register Map Editor 正常，1000×700 正常。”

Human 明确验证（仅限原文提及范围，不做外推）：

- Register Map Editor 正常（第二切片整体 = 全部 PASS）
- 1000×700 正常

未声称 Human 验证了原文未提及的逐字段结论（FC 输入习惯、scale/offset/unit
编辑细节等仅由自动化门禁覆盖）。

状态变更：

```text
M12-B Second Slice = IMPLEMENTED / AUTOMATED PASS / HUMAN VISUAL REVIEW PENDING
                   ⇒ IMPLEMENTED / AUTOMATED PASS / HUMAN ACCEPTED
verified LKGC      = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（不变，不自动推进）
```

M12-B Third Slice（Session Active Profile + Communication Profile Selector）
= NOT STARTED。

## 41. M12-B Third Slice — Session Active Profile + Communication Selector Archive
（2026-09-26，behavior `6802d18`）

> 范围（Human 指令 §4）：会话级 Active Profile 状态 + Communication 页轻量
> 「当前设备档案」selector。**未实现**：Read Result semantic overlay、semantic
> value display、Register Map Editor 新功能、AI、Manual Import、Manual Q&A、
> M12-C/D、canonical package、LKGC advance。Human Second-Slice Acceptance 已
> 先行归档（§40，commit `229fa51`）。

### 41.1 源码审计回答（§3 A–E，repo 当前文字为准）

- **A. catalog authoritative owner** = `ProfileController`（`m_catalog`；
  `refreshCatalog()` 是唯一扫描入口；catalog 只列 valid profiles，
  malformed 文件进 issue 计数，从不进入可选列表）。
- **B. CommunicationPage 接收方式** = `required property var …` 由 Main.qml
  注入（T017 ownership：页面不创建/不复制业务状态）。
- **C. Active state 归属** = **独立 `ActiveProfileController`**（§4 优先独立；
  由 Main 注入 `profileController` 引用并监听其 `catalogChanged` 做失效与
  内容刷新；Editor 与 Active 双向零耦合）。
- **D. 复用 catalog** = 是：selector 列表来自 `ProfileController.profileCatalog`
  （无第二次文件系统扫描）；内容解析用
  `ProfileStore.loadFromFile(defaultFilePathFor(id))`。
- **E. delete 成功后的 catalog refresh** = `ProfileController::deleteProfile`
  成功路径内部 `refreshCatalog()` → `emit catalogChanged()`；失败路径不发射
  catalogChanged —— 天然满足「删除失败保持 active」。

### 41.2 Active Profile 语义（全部实现并自动验证）

- **identity = 完整 profileId**（永不 displayName / filename hash / list
  index）；**content = 当前 persisted Profile**（经 catalog 存在性 + 文件
  load + validation 双重确认）；未保存的 Editor draft 永不泄漏进 active
  （b3c11、NC-B3-4）。
- **save active**：identity 不变、persisted content 刷新到新保存版本
  （b3c13/b3c14/b3c20、gate stage 11 selector 标签随 catalog refresh 更新）。
- **失效**：catalog refresh 后 activeProfileId 不存在/unloadable ⇒ 清空 →
  No Profile Selected（b3c18/b3c19）；无 stale cache、无 fallback、无自动
  选第一个。
- **delete active**：只有 delete **实际成功**（发 catalogChanged）才清空；
  失败保持（b3c16/b3c17、NC-B3-3）。
- **startup/restart**：每次启动 = No Profile Selected；无 QSettings / state
  JSON / lastActiveProfileId（b3c01/b3c23、NC-B3-5）。
- **Editor/Active 分离**：open-for-edit B / new C / 修改 B 未保存 / save B /
  delete B 全部不改变 active A（b3c09-b3c15、NC-B3-1）。
- **自动绑定**：无 COM / Slave / FC / response / manufacturer·model 绑定 ——
  metaobject 白名单测试证明（b3c24/b3c25：property/method 面恰为冻结名单）。
- **accessor**：`activeProfile` 提供完整 persisted DeviceProfile 投影（含
  registers 全字段），与 persisted 逐字段一致（b3c21/b3c22，FC03+FC04 同址
  保留）；**本轮无任何 semantic lookup/展示**（gate stage 13 断言页面无
  semantic 对象）。
- **notify**：hasActiveProfile / activeProfileId / activeProfile 全部
  NOTIFY activeChanged；QML selector 在 active/catalog 变化后重断言权威
  位置（见 41.4 的 ComboBox model-reset 发现）。

### 41.3 Communication selector（actual layout）

- 位置：连接 PanelCard 与请求区之间，独立轻量行
  `communicationProfileSelectorRow`（Label「当前设备档案」+ ComboBox 260×24
  + 提示 Label「用于后续寄存器语义解读（会话内有效）」）；**未嵌入完整
  Editor**、未新增大卡。
- 第一项 = 「未选择设备档案」（profileId 空串 sentinel → clearActive）；
  选择 valid Profile → `selectProfile(profileId)`；QML handler 读
  **profileId role**，绝不使用 index 当 identity。
- 下拉两行 delegate：primary=displayPrimary、secondary=displaySecondary ——
  **逐字复用 catalog 已冻结的 duplicate displayName 消歧**（§33.4），
  未重新实现算法（gate stage 7/8：两个「Alpha」可区分且独立可选）。
- 空 catalog：只剩 sentinel 行，显示「未选择设备档案」，不 crash；
  malformed 文件不进可选列表且不阻塞其它选择（gate stage 2）。

### 41.4 过程记录（诚实档案）

- **C4 布局回归被真实 windows gate 抓住**：selector 加入后
  `qml_write_foundation_check_windows`（1000×700 FC10 展开态）报
  writeFoundationPanel 被裁 15px —— selector 行把页面内容整体下推。修复 =
  selector 用 24px 控件高度（与 identity 编辑器字段一致）+ 页面 gap 8→6px +
  页面上下 margin 16→4px（左右 margin 不动，避免跨页内容错位）→ C4 恢复绿
  （382+319=701→≤700 的最后 1px 由 margin 4 收敛）。全程无 clip 掩盖。
- **QML ComboBox model-reset 陷阱（本轮新发现）**：selector 的
  `currentIndex` 绑定 `selectedProfileChoice`；catalog refresh 替换
  model 数组时 ComboBox **内部重置 currentIndex**，而 QML 绑定在重估值
  不变时不再发通知 → 控件停留在重置位（save-active-rename 后 selector 显示
  「未选择设备档案」即此因）。修复 = `Connections`（activeChanged /
  catalogChanged）显式重断言权威位置（直接调用，非 Qt.callLater ——
  callLater 晚于同 stage 断言；重断言写 currentIndex 会重发 activated，
  handler 读 profileId 幂等，链一次收敛无循环）。
- **NC mutation 事故（未遂，无损失）**：NC-B3-1 首次 patch 误把 include 插进
  openProfile 函数体中段（python replace 匹配点错误），立即精确逆向还原，
  基线复绿后改用 ActiveProfileController 内部 connect 的最小 patch 完成
  mutation。全程未使用 `git checkout -- <file>`。

### 41.5 测试 / 负向对照 / 回归（真实数字）

```text
active_profile_controller = 27 passed（B3-C01..C25 + init/cleanup）
profile_controller        = 65 passed（未动）
device_profile            = 114 passed（未动）
qml_active_profile_check  = PASS（19 stages，B3-Q01..Q22）
Debug  full ctest         = 47/47（45 + active_profile_controller
                                    + qml_active_profile_check）
Release full ctest        = 47/47
RegisterDecode.{h,cpp}    = ZERO DIFF；DeviceProfile schema/JSON/
  readFunctionCode/same-FC overlap/M10 wire truth/M11 raw 全部零改动
windows-QPA（真实 windows 平台）10 门全 exit=0；诊断
  （ReferenceError/TypeError/Unable to assign/String.arg Invalid）= 0；
  qrc 警告 = 0
负向对照（真实 mutate → 观察 FAIL → 精确逆向 patch → 复绿；未提交）：
  NC-B3-1 open-for-edit 自动 setActive        → b3c09 FAIL（gate 同红）
  NC-B3-2 identity 用 displayName             → b3c03+b3c07 FAIL
  NC-B3-3 delete 失败也清空 active            → b3c17 FAIL
  NC-B3-4 未保存 draft 泄漏进 active 内容     → b3c11 FAIL
  NC-B3-5 恢复 lastActiveProfileId            → b3c23 FAIL
```

### 41.6 1000×700 实测几何（ACTGEO）

```text
communicationProfileSelectorRow x=73  y=141 w=554 h=24
commProfileSelector             x=157 y=141 w=260 h=24   （键盘可达）
communicationConnectionSection  x=73  y=87  w=911 h=48
commStartField / commReadButton y=236（请求字段与 Read 按钮未被挤压）
readResultPanel                 x=85  y=300（未被遮挡）
writeFoundationPanel（C4 gate） 1000×700 FC10 展开态 = 窗内 PASS
无 clip 隐藏 overflow
```

### 41.7 提交与状态

```text
Human second-slice acceptance commit = 229fa51（M12: accept register-map editor slice）
behavior commit                      = 6802d182a9de80246723e160a4fc47720b31b1c5
                                       （M12: add active profile selection；7 文件，无 docs）
docs archive commit                  = 本 commit（M12: archive active-profile selector slice）
staging = build/m12b-visual-candidate/ModbusLens.exe
  size = 5,343,027 B  SHA-256 = ef060abc34bd7a66042eb33c297c1b4a1e0745907f72d4dfe2389046f4644041
  source == staging（cmp byte-identical）
clean-env（PATH=System32;Windows）: smoke / active-profile / profile-editor /
  register-map / nav / geometry 全 exit=0 且含 PASS 标记
visual candidate ≠ canonical package

M10 = COMPLETE；M11 = COMPLETE；M11 FINAL PORTABLE PACKAGE = VERIFIED
M12-A = FOUNDATION ACCEPTED
M12-B Slice 1 = HUMAN ACCEPTED；M12-B Slice 2 = HUMAN ACCEPTED
M12-B Slice 3 = IMPLEMENTED / AUTOMATED PASS / HUMAN VISUAL REVIEW PENDING
M12-B remaining = Read Result semantic overlay
M12-C / M12-D = NOT STARTED
verified LKGC = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（不变，NO advance）
```

## 42. M12-B Third Slice — HUMAN ACCEPTED（2026-09-26，docs-only）

Human 原文（逐字归档）：

> “M12-B 第三切片全部 PASS，Active Profile 和 Communication selector 正常，1000×700 正常。”

Human 明确验证（仅限原文提及范围，不做外推）：

- Active Profile 正常
- Communication selector 正常
- 1000×700 正常

未声称 Human 验证了原文未提及的逐项结论（duplicate displayName 下拉呈现、
删除 active 后的 selector 状态等细节仅由自动化门禁覆盖）。

状态变更：

```text
M12-B Third Slice = IMPLEMENTED / AUTOMATED PASS / HUMAN VISUAL REVIEW PENDING
                  ⇒ IMPLEMENTED / AUTOMATED PASS / HUMAN ACCEPTED
verified LKGC     = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（不变，不自动推进）
```

M12-B Slice 4（Read Result Semantic Overlay）= NOT STARTED。

## 43. M12-B Slice 4 (FINAL) — Read Result Semantic Overlay Archive
（2026-09-26，behavior `f1567a5`）

> 范围（Human 指令 §4）：Active persisted Device Profile 接入 Read Result
> detail/dialog，形成冻结三层展示（Raw / M11 Generic Decode / M12 Profile
> Semantic）。**未实现**：AI / Manual Import / Manual Q&A / M12-C / M12-D /
> canonical package / LKGC advance。Human Slice-3 Acceptance 已先行归档
> （§42，commit `5cbcbe9`）。

### 43.1 源码审计回答（§3 A–I，repo 当前文字为准）

- **A. raw rows owner** = `AnalysisController::readResult_`（snapshot：
  requested FC / startAddress / analysis.values）+ `readResultValues()` 投影。
- **B. requested readFunctionCode** = `readResult_.functionCode`
  （"the request's ACTUAL wire function code"，transaction truth；非
  response/exception/inferred/硬编码）。
- **C. 每行 PDU address** = `readResult_.startAddress` 起逐行 +1。
- **D. M11 generic decode 执行处** = `readResultValues()` 内
  `decodeRegisterView`（sliding window per row）。
- **E. generic UI 选择传入** = `readDecodeType_ / readDecodeByteOrder_ /
  readDecodeWordOrder_`（Q_PROPERTY WRITE，readDecodeControls 绑定）。
- **F. numeric scalar 可得性** = decodeRegisterView 仅暴露格式化 text；
  数值中间量（effective word / bits / 重解释）存在于冻结管线内 ⇒ 以
  **纯新增 accessor** 暴露（§4 允许的非破坏性 projection），未改任何既有
  函数/状态映射/文本。
- **G. Hex/Binary numeric 语义** = **明确定义且非新发明**：M11 的 Hex text =
  `toHex16(effective)`、Binary text = `toBinary16(effective)` ⇒ numeric =
  位模式本身（effective）；Int16 = two's complement；UInt32/Int32/Float32 =
  bits 的既有重解释。**无契约缺口，未触发 §4 STOP。**
- **H. 签名** = `findProfileEntryByStartAddress(profile, uint8 fc, uint16 addr)
  → ProfileLookupResult{status, entryIndex, offsetWithinSpan}`；
  `findProfileEntryCoveringAddress` 同形；`projectProfileSemanticValue(entry,
  decodedValue) → ProfileSemanticProjection{valueClass, semanticValue, unit,
  readFunctionCode}`。
- **I. semantic owner** = **AnalysisController**（拥有 readResult snapshot 的
  同一 controller；注入 ActiveProfileController 指针；`activeChanged` →
  `announceReadResultChanged()` 重投影）。

### 43.2 语义语义（全部自动验证）

- **lookup key** = (requested FC, PDU/0-based address)；无 cross-FC
  fallback（FC04 读取绝不使用 FC03 entry：b4c07/b4c09/b4c36、NC-B4-1）。
- **profile-vs-generic decode 独立**：generic 跟随 UI controls，semantic 跟随
  **profile metadata**（dataType/byteOrder/wordOrder/scale/offset/unit）；
  二者允许不同且互不影响（b4c10/b4c11/b4c30/b4c31/b4c37、gate stage 6、
  NC-B4-2/NC-B4-6）。
- **2-word start-row rule**：semantic 值只挂 start row（b4c15）；continuation
  row 保留 raw + M11 generic（滑窗不变，b4c17）并显示 membership
  （semanticName/span/startAddress，无第二份 semantic value，b4c16/b4c19、
  gate stage 7、NC-B4-3）。
- **partial span**：窗口只含 start word ⇒ `insufficient_words`，不补零/不猜
  /不越界（b4c18）。
- **scale/offset/unit**：冻结公式 decoded×scale+offset（顺序敏感，b4c05；
  scale=0 合法 b4c06；offset 后置）；unit 自由文本（空 b4c20、Unicode b4c21）；
  unit 仅附于 finite 值。
- **special values**：NaN/+Inf/−Inf 保持 IEEE 传播，按**数值分类**
  （projectProfileSemanticClassOf，非字符串推断，b4c25）并以 M11 既有
  wording 呈现（b4c22-24、gate stage 11）。
- **状态模型**：no_active_profile / unmapped / mapped_start /
  mapped_continuation / insufficient_words / decode_error（stable token，
  tests 不依赖中文串断言状态；中文只做 Human 呈现）。
- **persisted-only**：unsaved editor draft 不进入 semantic（b4c26、
  NC-B4-5）；save active → 同 identity 内容刷新（b4c27、gate stage 13）；
  delete/clear active → 立即 no_active_profile（b4c28、gate stage 14）。
- **零改面**：raw/HEX 不变（b4c29）、generic decoded 不变（b4c30）、source
  range 不变（b4c31）、TransactionAnalysis/wire truth 零 diff、DeviceProfile
  schema 零 diff、readFunctionCode/overlap 语义零 diff。
- **三层 UI**：`readResultLayerLegend`（"每行三层：原始值（DEC/HEX）→
  通用解析 → 设备档案语义"）+ 每行固定 `readSemanticCell` 前缀"档案语义"；
  不靠颜色区分；dialog 内完成（未膨胀 Communication 主页面）。
  no-active → "未选择设备档案"；unmapped → "未匹配设备档案"；continuation →
  "属于 <name>（起始地址 <n>）；语义值显示在起始地址行"。

### 43.3 实现清单

- `RegisterDecode.{h,cpp}`：`decodeRegisterNumeric`（**+100 行 / −0 行**，
  `git diff --numstat` 证明纯新增；呈现语义 ZERO CHANGE）。
- `ActiveProfileController`：`activeCoreProfile()`（persisted core profile，
  永不 editor draft）。
- `AnalysisController`：注入 + 语义列（additive fields）+ 状态 token。
- `CommunicationPage.qml`：legend + `readSemanticCell`（delegate 由单 Label
  改为 ColumnLayout；width 用 `ListView.view.width` 修正一处新增的
  ReferenceError）。
- `main.cpp`：`--qml-profile-semantic-check`（17 stages，B4-Q01..Q32）+
  `--qml-profile-semantic-demo`（Scenario A+B，`--demo-exit-after-ready`
  供 ctest）。
- 关键修复（迭代中，未进入提交前的中间态）：① harness transport
  `completeReadImmediately` 默认 true 会用固定响应抢先完成 read → 显式关；
  ② `connectSerial` 异步 → 首个 read 前加 settle stage；③ special 值误附
  unit → unit 仅 finite；④ delegate width ReferenceError。

### 43.4 测试 / 负向对照 / 回归（真实数字）

```text
profile_semantic = 40 passed（B4-C01..C38）
device_profile = 114 passed；profile_controller = 65 passed；
active_profile_controller = 27 passed
qml_profile_semantic_check = PASS（17 stages，B4-Q01..Q32）
Debug full ctest = 49/49；Release full ctest = 49/49
windows-QPA 11 门全部 exit=0；诊断
  （ReferenceError/TypeError/Unable to assign/String.arg Invalid）= 0；
  qrc 警告 = 0
负向对照（真实 mutate → FAIL → 精确逆向 patch → 复绿；未提交）：
  NC-B4-1 lookup 忽略 FC            → b4c07+b4c09+b4c36 FAIL
  NC-B4-2 用 generic UI 配置        → b4c10+b4c11+b4c37 FAIL
  NC-B4-3 continuation 重复 start   → b4c15+b4c16+b4c19 FAIL
  NC-B4-4 (decoded+offset)*scale    → b4c05+b4c06 FAIL
  NC-B4-5 unsaved draft scale 泄漏  → b4c26 FAIL
  NC-B4-6 semantic 覆盖 generic 列  → b4c30+b4c10+b4c31 FAIL
```

### 43.5 1000×700 实测几何（SEMGEO）

```text
readResultDialog        x=169 y=111 w=720 h=520（真实 popup background）
readResultDetailScroll / readResultDetailCloseButton 全 contained
lastSemanticCell（滚动到底后）= "档案语义 输出频率 = 46.6 Hz（1000-1000）"
滚动可达性：contentY 置底断言通过；无 clip 隐藏 semantic 内容
```

### 43.6 提交与状态

```text
Human slice-3 acceptance commit = 5cbcbe9（M12: accept active-profile selector slice）
behavior commit                 = f1567a54784593ed76282dbd6f1e89a1f24db6c3
                                  （M12: add profile semantic readout；11 文件，无 docs）
docs archive commit             = 本 commit（M12: archive profile semantic readout slice）
staging = build/m12b-visual-candidate/ModbusLens.exe
  size = 5,481,141 B
  SHA-256 = ec3d50f2e92edc5104f48d1116c5fe8f286f58f89329105e58d2867db2bb7082
  source == staging（cmp byte-identical）
  launchers = Run-M12-Profile-Editor.cmd / Run-M12-Semantic-Demo.cmd
clean-env（PATH=System32;Windows）：smoke / profile-semantic / active-profile /
  profile-editor / register-map / read-result / nav / geometry 全 exit=0；
  semantic demo（--demo-exit-after-ready）exit=0 且 READY 标记在
visual candidate ≠ canonical package；未覆盖 M11 final package

M10 = COMPLETE；M11 = COMPLETE；M11 FINAL PORTABLE PACKAGE = VERIFIED
M12-A = FOUNDATION ACCEPTED
M12-B Slice 1/2/3 = HUMAN ACCEPTED；Slice 4 = IMPLEMENTED / AUTOMATED PASS
  / HUMAN VISUAL REVIEW PENDING
M12-B overall = HUMAN CLOSURE PENDING
M12-C / M12-D = NOT STARTED
verified LKGC = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（不变，NO advance）

真实硬件状态：NOT VERIFIED（本切片全部为无硬件确定性演示；不得声称
REAL MODBUS HARDWARE VERIFIED）。

## 44. Demo Lifetime RCA / Correction（2026-09-26，behavior `13799d6`）

> 现象：`Run-M12-Semantic-Demo.cmd` 启动后约 2 秒窗口自动关闭。
> 范围：只修 demo 生命周期；semantic 功能 / Raw / Generic / Profile
> projection 零改动；M12-C 未开始。

### 44.1 取证（逐项）

1. **launcher**：`"%~dp0ModbusLens.exe" --qml-profile-semantic-demo`
   —— **未携带任何 auto-exit 参数**（不是 launcher 参数问题）。
2. **dispatch**：`--qml-profile-semantic-check` 与
   `--qml-profile-semantic-demo` 都进 `runProfileSemanticCheck`。
3. **函数内退出/定时逻辑**：
   - `app.setQuitOnLastWindowClosed(false)`（函数顶部，无条件）；
   - `exitAfterReady = args.contains("--demo-exit-after-ready")`；
   - `if (exitAfterReady) { …断言 A+B…; app.exit(0); return 0; }`（test-only
     分支，正确）；
   - **fall-through**：非 exit-after-ready 时继续执行 check 的 17-stage
     流水线（每 stage `QTimer::singleShot(60ms)`），schedule 末端
     **`app.exit(0)`**（唯一出口）。
4. **CTest**：当时只注册 `--qml-profile-semantic-check`；demo 的 test-only
   模式没有持久覆盖。

### 44.2 根因

**不是 launcher 参数问题，也不是"写死的定时器"**：`--qml-profile-semantic-demo`
（无 `--demo-exit-after-ready`）落入 **check 模式的 stage 流水线**，而该流水线
的唯一终点是 `app.exit(0)` ⇒ 约 17×60ms ≈ **2 秒后自动退出**（与现象吻合）。
即：**demo 复用了 check 的流水线，而 Human 模式缺少一个"保持运行"的分支**。

### 44.3 Exact fix（T027 §44，commit `13799d6`）

三种生命周期显式拆分：

```text
--qml-profile-semantic-check                         → 断言流水线（不变，末段 exit 0/1）
--qml-profile-semantic-demo --demo-exit-after-ready  → TEST-ONLY 自动化 demo（断言 A+B → exit 0；
                                                        本轮同时注册 ctest qml_profile_semantic_demo）
--qml-profile-semantic-demo                          → HUMAN VISUAL：导航 Communication →
                                                       展示 Scenario A → 打开详情 dialog →
                                                       读 Scenario B（dialog 内容随绑定刷新）→
                                                       return app.exec()，**保持运行**
```

- `quitOnLastWindowClosed = humanDemo`（**仅** Human 模式为 true）：Human 手工
  关窗 = 正常退出 0；断言模式不会被误关窗打断。
- Human launcher 不含任何 auto-exit 参数；normal product launch 未改动。

### 44.4 验证（生命周期矩阵，`build/verify_demo_lifetime.py`）

```text
automated demo: exit=0 + "PROFILE SEMANTIC DEMO READY"
human demo:     alive@12s=True  alive@18s=True（无定时自动关闭）
human demo:     exit-after-graceful-close(WM_CLOSE)=0
clean-env launcher（PATH=System32;Windows）: alive@12s/18s=True；
                优雅关闭 exit=0
回归: smoke + profile-semantic check/demo gates PASS；
      Debug full CTest 50/50；Release full CTest 50/50
```

### 44.5 staging 与状态

```text
behavior commit = 13799d633291abd69b66ab1c324699ac5014143a（M12: keep the human
                  semantic demo alive until closed；2 文件，无 docs）
staging = build/m12b-visual-candidate/ModbusLens.exe（重新刷新）
  size = 5,491,340 B
  SHA-256 = 5dc6be4e0104b4cd9e4e9915ee1bf862055d8c4f601f03dad52dd711f0ce6a5d
  source == staging（cmp byte-identical）
  launchers = Run-M12-Semantic-Demo.cmd（Human visual，无 auto-exit 参数）
            / Run-M12-Profile-Editor.cmd
clean-env staging 门禁: smoke / profile-semantic / active-profile /
  profile-editor / register-map / read-result / nav / geometry 全 exit=0；
  automated demo exit=0

M12-B Slice 4 = IMPLEMENTED / AUTOMATED PASS / HUMAN VISUAL REVIEW PENDING
M12-B overall = HUMAN CLOSURE PENDING；M12-C/D = NOT STARTED
verified LKGC = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（不变）

## 45. M12-B FINAL HUMAN ACCEPTANCE + FORMAL CLOSURE（2026-09-27，docs-only）

> 本轮为 docs-only governance closure：未修改 src / tests / QML / CMake，
> 未 build / test / package，未重建 staging，未推进 LKGC，未 push/tag/amend，
> 未开始 M12-C/D。

### 45.1 Human Final Acceptance（逐字归档）

Human 原文：

> “M12-B 第四切片全部 PASS，三层展示正常，semantic 正确，Raw/Generic 不变，1000×700 正常，demo 不再自动关闭。”

Human 明确确认的内容（仅此 7 项，不做任何扩写）：

1. Slice 4 全部 PASS
2. 三层展示正常
3. semantic 正确
4. Raw 不变
5. Generic 不变
6. 1000×700 正常
7. demo 不再自动关闭

**未声称**（Human 原文未提及）：真实硬件验证、FC03/FC04/custom FC 的逐项
Human 验证、NaN/Inf 逐项 Human 验证、package 验证。

### 45.2 M12-B FINAL STATE（正式收口）

```text
M12-B Slice 1 = HUMAN ACCEPTED
M12-B Slice 2 = HUMAN ACCEPTED
M12-B Slice 3 = HUMAN ACCEPTED
M12-B Slice 4 = HUMAN ACCEPTED
M12-B = COMPLETE
M12-C = NOT STARTED
M12-D = NOT STARTED
M12 overall = IN PROGRESS（因 C/D 未开始）

verified LKGC = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（不变，NO advance）
REAL HARDWARE = NOT VERIFIED
M12 canonical package = NOT CREATED
```

**口径纪律**：`M12-B = COMPLETE` **不得**写成 `M12 COMPLETE`——M12-C
（Manual Import + AI Extraction Candidate）与 M12-D（Manual Q&A）仍未开始，
M12 overall 只能是 IN PROGRESS。

### 45.3 Automated Evidence Boundary（历史已执行证据，本轮未重跑）

以 repo 现有记录为准（§43.4 / §44.4 原始数字）：

```text
Slice 4 automated acceptance（历史执行）：
  Debug full CTest = 49/49 PASS
  Release full CTest = 49/49 PASS
  profile_semantic = 40/40
  11 windows-QPA gates = exit 0
  diagnostics = 0（ReferenceError/TypeError/Unable to assign/String.arg Invalid；qrc 警告亦 0）
  staging source identity = byte-identical（SHA ec3d50f2…，5,481,141 B，当时值）

demo lifetime correction（历史执行）：
  Debug full CTest = 50/50 PASS
  Release full CTest = 50/50 PASS
  automated demo mode（--demo-exit-after-ready）= auto-exit PASS
  Human visual mode = does not auto-exit（alive@12s/18s=True；WM_CLOSE 后 exit=0）
```

**本轮为 docs-only，未重跑任何命令；以上均为历史证据引用。**

### 45.4 Demo Lifetime 最终措辞（canonical）

```text
旧问题：bare --qml-profile-semantic-demo 错误 fall-through 到 automated
        17-stage check pipeline；该 pipeline 末端是 app.exit(0)
        ⇒ 约 2 秒后窗口自动关闭。

修复后（behavior 13799d6）：
  automated test：--qml-profile-semantic-demo --demo-exit-after-ready
                  → test-only auto-exit（并注册 ctest qml_profile_semantic_demo）
  Human visual：  --qml-profile-semantic-demo
                  → stays alive until the Human closes the window
                    （quitOnLastWindowClosed 仅 Human 模式为 true）
```

**不得**写成 timer crash / DLL issue / launcher extra argument issue。

### 45.5 Truth Architecture Closure（沿 §8，未重写旧章节）

```text
Layer 1  Raw / wire truth          = M10/M11 authority（Actual TX/RX、
                                     TransactionAnalysis.values、raw DEC/HEX）
Layer 2  M11 Generic Decode         = 七类型 + 双轴 + DecodeStatus（sliding window）
Layer 3  M12 verified Profile       = HUMAN-authored persisted metadata 的
         Semantic                      derived presentation（Layer 4 投影）

M12-B 完成后仍然强制：
  · Raw 不被 Profile 改写；
  · Generic Decode 不被 Profile 替换（UI controls 独立驱动）；
  · Profile semantic 只作为 derived presentation（additive 列，永不回流）。

Editor draft ≠ Active persisted Profile（semantic 只读 Active persisted 内容；
unsaved 编辑不进入任何 semantic 结果）。
```

### 45.6 M12-B Delivered Capability Summary（收口摘要）

- **Slice 1**（behavior `da07f43`；acceptance `0d146f2b`）：Device Profile
  workspace；New / Open / Save / Delete；identity editor（profileId 只读）；
  dirty 三路 Save / Discard / Cancel；exit dirty guard。
- **Slice 2**（behavior `becadc5`；acceptance `229fa51`）：Register Map
  Editor —— Add / Edit / Delete entry；readFunctionCode（keyboard-editable，
  无 silent default）；PDU / 0-based address；dataType（M11 七类型）；
  registerCount（派生只读）；byteOrder；wordOrder（1-word 显示但 disabled
  「不适用」）；scale / offset / unit；candidate + 完整 validation 纪律。
- **Slice 3**（behavior `6802d18`；acceptance `5cbcbe9`）：session Active
  Profile（profileId identity、session-only、启动无档案、失效/删除语义）+
  Communication 轻量 selector + Editor/Active 完全分离。
- **Slice 4**（behavior `f1567a5` + lifetime fix `13799d6`；acceptance 本轮）：
  Read Result Semantic Overlay —— Raw / Generic / Semantic 三层；FC + PDU
  lookup（无 cross-FC fallback）；2-word start-row semantic；continuation
  membership；scale / offset / unit；特殊值（NaN/±Inf）数值分类呈现；
  deterministic visual demo。

**不含**（属 M12-C/D）：AI 提取、Manual Import、Manual Q&A。

### 45.7 Remaining M12 Scope（未开始，本轮不冻结新细节）

```text
M12-C = Manual Import + AI Extraction Candidate
M12-D = Manual Q&A
```

继续保留的既有原则（来自 §15/§16，非本轮新增）：
- **AI is extractor / assistant, not authority**；
- candidate 必须经 **Human Accept / Edit / Reject** 后才能进入 verified
  Profile。

### 45.8 Real Hardware / Package Boundary

```text
M12-B：REAL MODBUS HARDWARE = NOT VERIFIED
  —— deterministic demo 不得写成 hardware evidence。

M12 canonical package = NOT CREATED。
M11 FINAL PORTABLE PACKAGE = 保持历史 VERIFIED，
  但不包含 M12 行为（其验收早于 M12 行为落地）。
M12 visual candidate（build/m12b-visual-candidate/）= visual / verification
  staging，不是 canonical package / Final D / release artifact / LKGC。
```

### 45.9 LKGC 治理与 M12-B 行为台账（Git 实测审计）

```text
verified LKGC = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（本轮未推进，无授权）
LKGC 是 HEAD 祖先：YES（git merge-base --is-ancestor）

bc99e6ea..HEAD 内 behavior-bearing commits（按真实 changed paths：
src/ tests/ QML CMakeLists scripts/ assets/ samples/）实测共 10 个：
  1c42aaf  M12: add device-profile JSON foundation
  5d4d9c2  M12: harden profile lookup foundation
  ec9823c  M12: scope profile data by read function
  da07f43  M12: add device-profile workspace
  e3a7a4c  M12: clip profile workspace to prevent visual overflow
  b502ea8  T027: restore displayName notification, close dirty dialog …
  becadc5  M12: add register-map editor
  6802d18  M12: add active profile selection
  f1567a5  M12: add profile semantic readout
  13799d6  M12: keep the human semantic demo alive until closed

M12-B 最后 behavior-bearing commit = 13799d633291abd69b66ab1c324699ac5014143a
  证据：git diff --name-only 13799d6 HEAD -- src tests CMakeLists.txt
        scripts assets samples  ⇒ 空（behavior tree 自 13799d6 起冻结）；
        其自身 changed paths = CMakeLists.txt + src/main.cpp。
  ⇒ 注意：不是 f1567a5（其后的 demo lifetime fix 仍是 behavior 变更）。

future LKGC candidate（仅列为候选，需 Human 单独授权才能推进）：
  13799d633291abd69b66ab1c324699ac5014143a（M12-B 完整行为树）
```

### 45.10 状态收口

```text
M10 = COMPLETE
M11 = COMPLETE
M11 FINAL PORTABLE PACKAGE = VERIFIED（不含 M12 行为）
M12-A = FOUNDATION ACCEPTED
M12-B = COMPLETE
M12-C = NOT STARTED
M12-D = NOT STARTED
M12 overall = IN PROGRESS
verified LKGC = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（不变）
NO M12 canonical package；REAL HARDWARE = NOT VERIFIED
```

## 46. M12 LKGC ADVANCE + M12-C START — Human Authorization Archive
（2026-09-27，docs-only）

Human 原文（逐字归档）：

> “两项一起授权。”

授权上下文（两项，均经 Human 明确确认）：

1. **verified LKGC 推进**：
   `bc99e6ea871628a3a685b9cf80cf3840e7b3b171`
   →
   **`13799d633291abd69b66ab1c324699ac5014143a`**
2. **M12-C START AUTHORIZED**（进入 CONTRACT / RECONNAISSANCE 阶段）。

Git 实测证据（归档时点）：

```text
git merge-base --is-ancestor 13799d6 HEAD          → exit 0
git diff --name-only 13799d6 HEAD -- src tests CMakeLists.txt scripts assets samples
                                                    → 空（零 behavior 变化）
⇒ 13799d6 = M12-B 最后 behavior-bearing tree（其后 6447d83 与本轮 docs commit
  均 docs-only，永不作 LKGC）。
```

归档后的 canonical state：

```text
# verified LKGC
13799d633291abd69b66ab1c324699ac5014143a

# M12-A
FOUNDATION ACCEPTED
# M12-B
COMPLETE
# M12-C
STARTED — CONTRACT / RECONNAISSANCE
# M12-D
NOT STARTED
# M12 overall
IN PROGRESS
# M12 canonical package
NOT CREATED
# REAL MODBUS HARDWARE
NOT VERIFIED
```

**口径纪律**：LKGC 是 `13799d6`——**不是** `6447d83`（closure docs commit），
**也不是**本轮 docs commit。所有 docs-only 提交永不作 LKGC。

---

## 47. M12-C CONTRACT / DEPENDENCY RECONNAISSANCE（2026-09-27，docs-only）

- **本轮性质**：READ-ONLY reconnaissance。**未实现任何 behavior**；未修改 `src/`、
  `tests/`、QML、`CMakeLists.txt`、`scripts/`、`assets/`、`samples/`；未 build /
  未 ctest / 未 package / 未安装任何 dependency。
- **授权边界**：Human 原文「两项一起授权。」（§46）授权的是 **M12-C START**，
  **不是**选择 AI provider / model / cloud policy / credential / storage / DOCX / OCR。
  本节凡未标注 **HUMAN-FROZEN** 者，一律为 **PROPOSED** 或 **PENDING HUMAN DECISION**。
- 起始 HEAD = `6ea57ab02a83ec455238319e1a6e0a7e444c0fdb`；verified LKGC = `13799d6…`（不变）。

### 47.1 M12-C canonical 重建与分类

**A. PRE-EXISTING CANONICAL**（契约/计划原文，非本轮发明）

| 项 | 出处（逐字口径） |
| --- | --- |
| M12-C = Manual Import + AI Extraction；格式 PDF / DOCX / TXT / Markdown | `docs/11_V2_UPGRADE_PLAN.md` §4 M12 第 103 行 |
| 扫描 PDF OCR = 后续能力（「仅作后续能力，除非调研证明低成本可靠可做」） | 同上；T027 §15 第 189 行、§21 out-of-scope |
| AI 输出 = **Candidate，不是 verified truth**；**不得直接修改 verified Profile** | 11_V2_UPGRADE_PLAN §4 第 104 行；T027 §15 第 190 行 |
| 每项至少含 candidate value / evidence / source page-section（若有）/ confidence-uncertainty / confirmation state | 11_V2_UPGRADE_PLAN §4 第 104 行；T027 §15 第 191 行 |
| Human **Accept / Edit / Reject**；仅确认后进入 verified Device Profile | 同上 |
| **AI is extractor / assistant, not authority** | 11_V2_UPGRADE_PLAN §4 第 106 行 |
| M12-C exit boundary | T027 §17.2 第 230 行 |
| OCR = future capability（**不阻塞 C**） | T027 §15 第 189 行、§17.2 第 230 行 |
| 本轮不决定：AI provider / prompt format / embedding store / vector DB / OCR engine | T027 §15 第 192 行 |
| P1-BEFORE-M12-C = AI provider / BYOK model；extraction evidence schema；OCR boundary | T027 §19 第 268 行 |
| out-of-scope：扫描 PDF OCR；vendor database / 厂商模板库 / cloud sync；SQLite / database / 云存储（JSON 之外的持久化） | T027 §21 |
| Manual Q&A（found / `Not found` / `Insufficient evidence`）属 **M12-D** | T027 §16 |
| 四层 truth architecture；PDU / 0-based 地址权威；不重开 M11 类型 | T027 §8 / §5 / §6 |
| per-FC duplicate / overlap；readFunctionCode 域 0x01..0x7F | T027 §29 / §32 |
| `validateDeviceProfile`（T027 §26，纯函数、不改输入、不 auto-correct） | `src/core/profile/DeviceProfile.h` |
| JSON persistence（1 文件 = 1 Profile、schemaVersion=1、version-too-new 拒载、MANUAL SAVE） | T027 §24.2 |
| Editor draft ≠ Active persisted Profile | T027 §31.3 / §33.3 |

**B. HUMAN-FROZEN**：D1–D4（「四项都同意」）、M12-A/B 的 P0/P1 冻结、LKGC 推进与 M12-C START（「两项一起授权」）。**M12-C 自身目前没有任何 Human-frozen 技术细节。**

**C. DERIVED BUT SAFE**（由 A 合成，无需 Human 单独裁定）
1. Candidate 进入 verified Profile 前必须走现有 `validateDeviceProfile` —— 由「AI 不得直接修改 verified Profile」+「validation 是唯一入口」合成。
2. AI / Candidate 层不得改写 wire truth、`TransactionAnalysis`、raw 值、M11 `RegisterDecode` —— 由四层 truth architecture 合成。
3. 「page exists but no extractable text」必须可表达 —— 由「evidence 必须可回看确定性 source」+「OCR = future」合成。

**D. PROPOSED**（本轮提出，**不是 canonical**）：ManualDocument / EvidenceReference / Candidate 模型（§47.10）、UI owner（§47.9）、切片分解（§47.13）、P0 推荐默认值（§47.14）。

**E. PENDING HUMAN DECISION**：AI provider、cloud upload policy、credential、manual storage、extracted-text persistence、Candidate persistence、DOCX strategy、confidence 表示、encoding policy、resource limits（§47.14）。

### 47.2 Source architecture audit（只读，逐项实际存在性）

| # | 问题 | 结论（repo 证据） |
| --- | --- | --- |
| A | M12-C 最自然 UI owner | **PROPOSED**：`src/ui/qml/pages/DeviceProfilePage.qml`（1044 行，rail index 5「设备」，M12-B 已启用）+ 一个新的 manual/import controller（`Main.qml` 第 310–311、345 行已有 `profileController` / `activeProfileController` 注入模式可复用）。**未冻结。** |
| B | Manual Import 是否属于现有 Device Profile workspace | **PROPOSED = 是**（作为 workspace 内独立入口/卡片），**不得**落在 Communication 页；**不得**复用 M12-B 的 Open（见 §47.9）。未冻结。 |
| C | 是否已有 ManualDocument / Candidate / Evidence domain model | **NO**（`grep -riE "ManualImport\|DocumentImport\|AiExtraction\|ManualExtraction\|ImportCandidate\|ManualQA" src tests CMakeLists.txt` = 0 命中）。 |
| D | 是否已有 AI provider abstraction | **NO（无 provider-neutral abstraction）**；但**有** single-provider single-purpose product code：`src/ui/ai/ModelScopeDiagnosisClient.{h,cpp}`（诊断）+ `src/ui/agent/ModelScopeAgentClient.{h,cpp}`（agent）。用途 = 诊断/只读 agent，**不是 extraction abstraction**。 |
| E | 是否已有 network abstraction | **有（product code，T011 既有）**：`QNetworkAccessManager` / `QNetworkReply` + `QTimer` timeout + `AiAbortReason`（UserCancel/Timeout/BatchInvalidated/DiagnosisCleared/SupersededRequest）+ `AiDiagnosisErrorCode`（NetworkError / Timeout / Unauthorized / RateLimited / ProviderRequestError / ServerError / InvalidResponse）。安全契约（头文件原文）：Authorization 只发给已配置端点、TLS peer verification 常开、重定向限制同源、**key/body 从不记日志**。 |
| F | 是否已有 credential / config storage | **有 env-var 一种**：`qEnvironmentVariable("MODELSCOPE_API_KEY")`（`ModelScopeDiagnosisClient.cpp:287`）；modelId 可 env 覆盖（`MODBUSLENS_MODELSCOPE_MODEL`）；**endpoint 编译期固定、刻意不可 env 覆盖**（防 token exfiltration）。**无** `QSettings`（全仓仅在 `ActiveProfileController.h:27` 注释中出现「no QSettings」）、**无** keychain / DPAPI / 配置文件。 |
| G | 是否已有 FileDialog / file picker production pattern | **有**：`src/ui/qml/pages/ReplayPage.qml`（`import QtQuick.Dialogs`，第 105–112 行 `FileDialog { id: replayFileDialog … onAccepted: page.analysisController.loadReplayFile(selectedFile) }`）；`Main.qml:3` 亦 import。`DeviceProfilePage.qml` 当前 **0 处** FileDialog。 |
| H | 是否已有 background worker / async task pattern | **无显式 worker 抽象**（`QThread` / `QtConcurrent` / `QFuture` / `QRunnable` / `QThreadPool` 全仓 **NOT FOUND**）。既有异步 = `QNetworkAccessManager` 事件驱动 + 串口信号驱动。 |
| I | 是否已有 ZIP / XML reader | **NO**。`zip` 仅出现在 `scripts/make_package.py`（打包脚本，非产品代码）；`QXml` / `QDomDocument` / `quazip` / `libzip` / `minizip` 产品代码 **NOT FOUND**。QtXml 模块**已安装但未链接**（见 §47.4）。 |
| J | 是否已有 PDF extraction layer | **NO**（`Pdf` / `PDF` / `poppler` 产品与测试代码均 **NOT FOUND**）。 |
| K | 是否已有 document import owner | **NO**。 |

补充（测试基建）：`tests/fake_chat_completions_server.{h,cpp}` = **TEST-ONLY** 本地 127.0.0.1 fake Chat Completions 端点（捕获 method/path/headers/body，脚本化响应；原文明确「NEVER touches the public internet，NEVER reads the developer's real MODELSCOPE_API_KEY」）⇒ 已存在 **AI 相关 deterministic offline test 先例**。`tests/data/` 为**空目录**。`ProfileStore::setManagedRootOverride()` = 已存在的 managed-root 注入 seam（供 C1 文件系统测试复用）。

### 47.3 Repository-wide capability search（PRODUCT / TEST / DOCS / NOT FOUND）

| 关键词 | 结果 |
| --- | --- |
| `QFileDialog` / `FolderDialog` | NOT FOUND（QML 侧用 `QtQuick.Dialogs` 的 `FileDialog`） |
| `FileDialog` | PRODUCT（`Main.qml`、`ReplayPage.qml`） |
| `QNetworkAccessManager` / `QNetworkReply` | PRODUCT（`src/ui/ai`、`src/ui/agent`）+ TEST（`fake_chat_completions_server.h`） |
| `QHttp` / `curl` / `QSslSocket` / `QSslConfiguration` | NOT FOUND（TLS 配置未在源码显式出现；头文件契约声明 TLS 校验常开） |
| `OpenAI` / `Anthropic` | **DOCS/SCRIPTS-ONLY**（`scripts/make_package.py` 的 negative scan 关键词），**产品代码 0 命中** |
| `Gemini` / `Azure` | NOT FOUND |
| `LLM` | PRODUCT（`ModelScopeDiagnosisClient.cpp`、`AnalysisController.h`）+ TEST |
| `API key` / `apikey` / `secret` | PRODUCT（`ModelScopeDiagnosisClient.{h,cpp}`、`AnalysisController.cpp`）+ TEST |
| `token` | PRODUCT（大量 = **协议/validation machine token**，与 credential 无关）+ TEST |
| `credential` | PRODUCT（`src/main.cpp:13651` 注释：no credential reaches the UI）+ scripts negative scan |
| `QSettings` | **NOT USED**（仅注释） |
| `keychain` / `DPAPI` | NOT FOUND |
| `QXml` / `QDomDocument` | NOT FOUND（产品代码） |
| `zip` / `quazip` / `archive` | SCRIPTS-ONLY（`make_package.py`）；`archive` 另有语义 = 归档统计/证据，非 ZIP |
| `QThread` / `QtConcurrent` / `QFuture` / `QRunnable` / `QThreadPool` | NOT FOUND |
| `async` | PRODUCT（仅描述性：串口/网络异步语义） |
| `Pdf` / `PDF` / `poppler` | NOT FOUND |
| `docx` / `DOCX` / `OOXML` | NOT FOUND |
| `Markdown` / `markdown` | PRODUCT（`AgentPromptBuilder.cpp`、`DiagnosisPromptBuilder.cpp`）= **prompt 文本格式化**，**不是 import 格式支持** |
| `UTF-8` | PRODUCT（ incidental：`ProfileStore` 的 profileId UTF-8 sha256、`RegisterDecode`、`Main.cpp`）；**无 TXT/MD 导入编码政策** |
| `BOM` / `QTextCodec` / `QStringConverter` | NOT FOUND |

### 47.4 Qt / toolchain dependency audit（read-only probe）

```text
qtpaths --qt-version                    → 6.11.1
qtpaths -query QT_INSTALL_PREFIX        → D:/QT/6.11.1/mingw_64
qtpaths -query QT_INSTALL_LIBS          → D:/QT/6.11.1/mingw_64/lib
toolchain                               → MinGW (GCC 13.1.0)
CMakeLists.txt find_package(Qt6 …)      → Core Gui Qml Quick QuickControls2 SerialPort Network Test
```

| 能力 | AVAILABILITY | 项目是否已采用 |
| --- | --- | --- |
| **Qt6Pdf / Qt6PdfQuick** | **NOT AVAILABLE**（`bin/` 无 `Qt6Pdf*.dll`；`lib/cmake/` 无 `Qt6Pdf*`；`include/` 无 `QtPdf*`；`plugins/` 无 pdf） | 否（且不存在） |
| Qt6Xml | **AVAILABLE**（`lib/cmake/Qt6Xml`、`include/QtXml` 6.11.1） | **否**（CMake 未 `find_package`、未链接） |
| Qt6Concurrent | **AVAILABLE** | **否** |
| Qt6Network | AVAILABLE | **是**（ModelScope client） |
| Qt6PrintSupport（PDF 相关） | 模块存在，但 `include/QtPrintSupport` 下 **无 QPdf\*** ⇒ 无 PDF 读/写能力 | 否 |
| zlib | MinGW sysroot **AVAILABLE**：`D:/QT/Tools/mingw1310_64/x86_64-w64-mingw32/include/zlib.h` + `lib/libz.a`；Qt 另有**私有** `include/QtZlib/zlib.h`（**不得**当作公开依赖使用） | 否 |
| QuaZip / libzip / minizip | **NOT FOUND** | 否 |

> **纪律**：AVAILABLE ≠ ADOPTED。本轮**未**修改 CMake、**未**安装任何 dependency。

### 47.5 PDF import capability（三者严格区分）

| 能力 | 定义 | 当前状态 |
| --- | --- | --- |
| **A. embedded text extraction** | 从 PDF 内容流逐页取得文本 | **无实现路径** —— QtPdf 未安装，产品代码无任何 PDF 解析；需新增第三方依赖或推迟 |
| **B. page rendering** | 把页面渲染为位图 | 无（且**不得**冒充 A） |
| **C. OCR** | 从位图识别文字 | canonical = **future capability**（T027 §21） |

结论：**text PDF 在当前 toolchain 下不存在 deterministic 实现路径**，除非 Human 批准引入新依赖（§47.14 P0-G 关联）。三条硬约束（写入契约供未来遵守）：
1. 不得把 render image 冒充 text extraction；
2. 不得自动 OCR / 自动调用 cloud OCR；
3. 某 page **存在但无 extractable embedded text** 时，模型必须能表达 `page exists but no extractable text`。

### 47.6 Scanned PDF / OCR boundary

- canonical 原文：T027 §15「扫描 PDF OCR = future capability（非本阶段必需）」；§21 out-of-scope；§17.2「OCR = future capability（不阻塞 C）」。
- 本轮：**不选择 OCR engine、不安装 OCR、不设计 cloud OCR、不声称 scanned PDF 已支持**。
- 「text PDF import」与「scanned PDF OCR」是**两条独立能力**，架构上不得合并。

### 47.7 DOCX dependency audit

**结论：`NO EXISTING DOCX READER`**（产品代码 0 命中；无 QuaZip/libzip/minizip；QtXml 未链接；zlib 未链接）。

| 方向 | dependency footprint | packaging impact | security surface | maintenance cost | evidence extraction quality |
| --- | --- | --- | --- | --- | --- |
| **A. 引入 dedicated dependency**（QuaZip / libzip 等） | +1 第三方库（源码或预编译） | 需新增 DLL、windeployqt 未必覆盖、package manifest 变更 | 新增第三方 CVE 面 + 供应链 | 中（版本跟进） | 高 |
| **B. 用当前 toolchain 已有 zlib + QtXml** | 链接 `libz.a`（已在 MinGW sysroot）+ `Qt6::Xml`（已安装） | 需改 CMake；zlib 若静态链入则无新 DLL | 自实现 ⇒ 面可控，但实现者需自行防范 zip-slip / decompression bomb | 中高（ZIP 中央目录 + OOXML 语义自实现） | 中高 |
| **C. narrowly-scoped OOXML reader** | 最小子集（`word/document.xml` + 段落/表格），仍依赖 B 的 ZIP 层 | 同 B | 最小（只读、白名单 entry） | 中 | 中（表格结构/跨页定位弱） |
| **D. 推迟 DOCX**（推荐见 §47.14 P0-G） | 0 | 0 | 0 | 0 | — |

任何未来 DOCX reader 的**强制不变量**（PROPOSED，待 Human 追认）：
不执行 macro · 不运行 embedded executable · 不自动 fetch external relationship · 不解析 remote resource · 防 zip-slip（entry 路径必须 containment 在目标目录内）· 防 decompression bomb（compressed/uncompressed 比与绝对上限）· 限制 entry 数量。

### 47.8 TXT / Markdown import audit

- **TXT**：Qt/Core 可 deterministic 读取本地文本文件（既有先例：`ReplayPage.qml` → `loadReplayFile`）。
- **encoding：未冻结**。全仓无 BOM 政策、无 UTF-16 处理、无 `QTextCodec` / `QStringConverter`、无 binary-file detection、无 max file size。
  - **PROPOSED（非 canonical）**：第一切片采用 **UTF-8 + 可选 UTF-8 BOM 剥离**；UTF-16 与非法编码 = 明确拒绝并给出可读错误；binary 检测 + 大小上限。
- **Markdown**：canonical 仅把 Markdown 列为 **manual input format**；**repo 无 Markdown parser**，本轮**不引入**。
  - **PROPOSED 区分**：source text / heading structure / evidence locator 三层。
  - 第一版**不得**：执行 HTML、执行 script、自动下载 remote content、因 import 副作用解析 remote image。

### 47.9 File picker / workspace integration

- 既有 production pattern：`QtQuick.Dialogs` 的 `FileDialog`（`ReplayPage.qml`）。
- **M12-B 的「打开」= managed profile store Open**（`ProfileController::openProfile(profileId)`，按逻辑 profileId 打开 managed JSON），**不是 manual import** ⇒ M12-C **不得**把它复用成「打开 Profile JSON」。
- **PROPOSED**：M12-C Manual Import 入口放在 `DeviceProfilePage.qml`（设备档案 workspace）内，作为与 New/Open/Save/Delete 并列的**独立动作**（例如「导入说明书」），经新的 import controller 走 `FileDialog`（复用 `ReplayPage.qml` 的 `QtQuick.Dialogs` 形态）。**未冻结。**

### 47.10 PROPOSED domain models（只设计，不实现）

> 全部 = **PROPOSED**。目标 = provider-neutral、可被 deterministic 测试、**不是**大型文档管理数据库。

**ManualDocument**

| 字段 | 标记 | 说明 |
| --- | --- | --- |
| `documentId` | REQUIRED FOR C1 | 程序生成的稳定 identity（复用 `QUuid` 先例） |
| `originalFileName` | REQUIRED FOR C1 | 人类可识别来源名（**非**路径权威） |
| `documentType` | REQUIRED FOR C1 | `pdf` / `docx` / `txt` / `md`（枚举，不自由文本） |
| `sourcePath`（原始路径） | PENDING HUMAN DECISION | 依赖 §47.14 P0-A（copy / reference / hybrid） |
| `managedCopyPath` | PENDING HUMAN DECISION | 同上 |
| `contentHash` | PROPOSED | 证据稳定性 + 缓存失效键 |
| `pageCount` | OPTIONAL（PDF/DOCX 语义） | TXT/MD 无 page 概念 |
| `extractionStatus` | REQUIRED FOR C1 | 至少 `not_started / ok / partial / failed` |
| `errorState` | REQUIRED FOR C1 | 失败不得静默 |
| `extractedSections/pages` | REQUIRED FOR C1 | 逐页/逐节文本 + **page exists but no extractable text** 状态 |

**EvidenceReference**

| 字段 | 标记 |
| --- | --- |
| `documentId` | REQUIRED FOR C1 |
| `pageNumber` | OPTIONAL / format **PENDING HUMAN DECISION**（PDF 页码约定、DOCX 无原生页码） |
| `section / heading` | OPTIONAL |
| `textStart / textEnd`（offset） | **PROPOSED**（offset 格式未冻结） |
| `short excerpt` | PROPOSED（**excerpt 长度未冻结**） |
| source hash / content identity | PROPOSED |

核心原则（canonical 派生）：**Candidate 不得只有 value + "AI says page 17"** —— 必须能重新关联到 imported deterministic source。

**Candidate（provider-neutral）**
- 覆盖两类 target：**ProfileField**（profileId/displayName/manufacturer/model/revision/description）与 **RegisterEntryCandidate**（`readFunctionCode` / `address` / `name` / `description` / `dataType` / `byteOrder` / `wordOrder` / `scale` / `offset` / `unit`）。
- **`registerCount` 不是 AI 输入**：它由 `dataType` 派生（T027 §33 Group 1-B），Candidate 也不得提供。
- Candidate **不得**直接构造 verified `RegisterEntry`；只描述「建议值 + evidence + confidence + confirmation state」。

### 47.11 Candidate lifecycle（PROPOSED）+ 必须冻结的 invariant

状态名/数量 **未冻结**，建议最小集：`Pending → Accepted / Rejected`（Edited 视为 Accept 前的一次修正，不新增终态）。

**必须冻结的 invariant（由 canonical 派生，非新发明）**
1. `Pending` **不得**进入 verified Profile。
2. `Rejected` **不得**进入 verified Profile。
3. Accept / Edit 后、真正写入 Profile 前，**必须**走现有 `validateDeviceProfile`。
4. 若写入会造成 `invalid_read_function_code` / `duplicate_address`(同 FC) / `overlapping_span`(同 FC) / `register_count_mismatch` / `unsupported_data_type` / `non_finite_scale|offset` / `address_out_of_range` / `span_out_of_range` ⇒ **Accept 必须失败**。
5. **Profile 不得被部分污染**（失败时原子回滚；沿用 M12-B「candidate copy + full validation + 只提交 valid」纪律）。
6. Candidate 在失败后**仍可继续修正**。
7. AI 失败 / 网络失败**不得**影响 imported manual 与 verified Profile（§47.12）。

### 47.12 M12-C / M12-D hard boundary

- **M12-C**：Manual Import → Evidence Foundation → AI Extraction Candidate → Human review（Accept/Edit/Reject）→ `validateDeviceProfile` → verified Profile。
- **M12-D**：Manual Q&A。**本轮禁止设计/实现**：question box、chat history、RAG Q&A、answer generation UI、conversation memory、Q&A citation UI。
- 可复用 `ManualDocument` / `EvidenceReference`，但**不得提前做 D**。

### 47.13 Slice decomposition（PROPOSED）+ C1 entry-gate

| 切片 | 内容 | 是否需 AI / cloud / credential |
| --- | --- | --- |
| **C1 — Deterministic Manual Import + Evidence Foundation** | 文件选择 → 类型判别 → 逐页/逐节文本提取 → `ManualDocument` + `EvidenceReference` → 可回看确定性 source；**无 AI、无 cloud、无 credential** | **否** |
| **C2 — Provider-neutral AI Candidate Extraction** | provider seam + prompt + 响应解析 → Candidate（value/evidence/confidence/confirmation）+ 失败语义 | 是 |
| **C3 — Human Candidate Review** | Accept / Edit / Reject → `validateDeviceProfile` → verified Profile | 否（但依赖 C1/C2 产物） |

**C1 entry-gate 判断**

| 依赖项 | 是否阻塞 C1 | 说明 |
| --- | --- | --- |
| AI provider | **否** | C1 定义上无 AI |
| cloud upload policy | **否** | C1 定义上无网络上传 |
| credential policy | **否** | C1 定义上无 credential |
| **manual storage policy** | **是** | 决定 import 后是否 copy / 只存引用 / hybrid ⇒ 决定 C1 语义与测试 |
| **extracted-text persistence** | **是** | 决定提取结果的生命周期与能否离线复核 |
| **TXT/MD encoding policy** | **是** | C1 的 TXT/MD 核心语义 |
| **DOCX strategy** | **是（仅当要求 C1 一次覆盖全部 4 种格式）** | 若 Human 允许「TXT / MD 先行」，则不阻塞 C1 启动 |
| **PDF dependency** | **是（仅当要求 C1 覆盖 PDF）** | QtPdf NOT AVAILABLE ⇒ PDF 需新依赖或单列子切片 |

**建议（PROPOSED）**：把 C1 再拆为 **C1a = TXT / Markdown**（零新依赖）与 **C1b = PDF / DOCX**（待 §47.14 P0-G 与 PDF 依赖决策），使 Human 一次决策后即可启动 C1a。

### 47.14 TRUE HUMAN-BLOCKING P0

> 仅列影响产品语义 / 安全 / 隐私 / 兼容性 / 数据生命周期者。**推荐默认值 = PROPOSED，Human 未答复前不冻结。**

| ID | CURRENT CONTRACT | OPTIONS | RECOMMENDED DEFAULT | WHY | C1 | C2 | C3 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **P0-A** manual storage | 未规定 | A 复制到 app-managed user-data / B 仅保存原路径引用 / C hybrid | **C-hybrid（复制进 managed store + 原始路径仅作 provenance 元数据，不再回读原文件）** | 源移动/删除不影响复核；离线可复核；本地隐私；磁盘占用由上限约束；可移植（沿用 `ProfileStore` 的 `QStandardPaths::AppDataLocation` managed-root 先例） | **阻塞** | 阻塞 | 否 |
| **P0-B** AI provider family | 未规定（T027 §15/§19 明确不决定） | 复用现有 ModelScope client 形态 / provider-neutral seam + 可插拔 backend / local model / 暂不选 | **定义 provider-neutral seam；本轮不点名任何厂商** | 避免锁定；`ModelScope` 只是既有诊断用途，不等于 M12-C 选择 | 否 | **阻塞** | 否 |
| **P0-C** cloud upload permission | 未规定 | 默认离线 / per-document consent / per-extraction consent + scope preview | **默认离线 + 显式 per-extraction consent + 可见 payload scope；deterministic import 必须零网络可用** | 隐私；避免「import 即自动上传」 | 否 | **阻塞** | 否 |
| **P0-D** credential source / storage | 未规定（既有先例 = `MODELSCOPE_API_KEY` env var + endpoint 编译期固定 + 不记日志） | env var / session-only input / OS credential store / app settings plaintext | **env var（复用既有先例）；明文配置文件与写入 repo / profile JSON 明确否决** | 最小面、与既有安全契约一致 | 否 | **阻塞** | 否 |
| **P0-E** Candidate persistence | 未规定 | session-only ephemeral / managed JSON / audit log | **v1 = session-only；Accepted 是否留 audit record 另议** | 隐私与存储最小化；Candidate 非 truth | 否 | **阻塞** | **阻塞** |
| **P0-F** extracted-text persistence | 未规定 | A 每次重提取 / B 持久化 / C cache + content-hash 失效 | **C（按 contentHash 缓存）** | 证据可复现 + 性能；失效键明确 | **阻塞** | 否 | 否 |
| **P0-G** DOCX dependency strategy | 未规定 | A dedicated dep / B zlib+QtXml / C narrow OOXML / D 推迟 | **D（DOCX 推迟出 C1；待定后优先 C 而非 A）** | 零依赖先跑通 TXT/MD；DOCX 需专门安全设计 | **阻塞全格式 C1；不阻塞 C1a** | 否 | 否 |
| **P0-H** confidence representation | canonical 仅要求 confidence/uncertainty，**未定义形式** | A label（low/medium/high/unknown）/ B opaque numeric + provenance / C provider text 或 unavailable | **A 为主 + 可选保留 provider raw 作为 opaque 字段；不得称为 probability** | 未校准分数不得冒充概率 | 否 | **阻塞** | 否 |
| **P0-I** TXT/MD encoding policy | 未规定 | UTF-8 only / UTF-8 + BOM / 含 UTF-16 / reject binary | **UTF-8 + 可选 BOM 剥离；UTF-16 与非法编码明确拒绝；binary 检测 + 大小上限** | deterministic、可测试、错误可见 | **阻塞** | 否 | 否 |
| **P0-J** resource limits | 未规定 | 数值由实现定 / 由 Human 定 | **安全上限（max file size / pages / extracted bytes / ZIP entry 数 / 压缩比 / AI payload / excerpt 长度）作为 implementation detail；但「超限即明确拒绝」的用户可见语义需 Human 追认** | 安全上限属实现细节；拒绝语义属产品策略 | 部分（建议授权「实现细节 + 明确拒绝」后即不阻塞） | 部分 | 否 |

附：**AI raw response 是否保存**归入 **P0-E**（与 Candidate persistence 同源，不单列）。

### 47.15 本轮状态与边界

```text
# 本轮产出
M12-C CONTRACT / DEPENDENCY RECONNAISSANCE = COMPLETE（docs-only）

# 明确未开始
M12-C IMPLEMENTATION                        = NOT STARTED
M12-D                                       = NOT STARTED

# 未决定（Human P0，见 §47.14）
AI provider / model / endpoint              = NOT FROZEN
cloud upload policy                         = NOT FROZEN
API credential storage                      = NOT FROZEN
manual storage (copy/reference/hybrid)      = NOT FROZEN
extracted-text persistence                  = NOT FROZEN
Candidate persistence                       = NOT FROZEN
DOCX dependency strategy                    = NOT FROZEN
confidence representation                   = NOT FROZEN
TXT/MD encoding policy                      = NOT FROZEN
resource limits                             = NOT FROZEN

# 未改变
verified LKGC = 13799d633291abd69b66ab1c324699ac5014143a
M12-B = COMPLETE；M12-C = STARTED — CONTRACT / RECONNAISSANCE；M12-D = NOT STARTED
M12 overall = IN PROGRESS；M12 canonical package = NOT CREATED
REAL MODBUS HARDWARE = NOT VERIFIED
```

**未做**：未安装 library / 未 vcpkg·pip·npm install / 未下载 DLL / 未改 Qt / 未改 CMake / 未 build / 未 ctest / 未 package / 未 staging / 未 windeployqt / 未 push / 未 tag / 未 amend。

---

## 48. M12-C P0 HUMAN DECISION ARCHIVE + C1a CONTRACT FREEZE（2026-09-27，docs-only）

### 48.1 Human 裁定原文（逐字归档）

> “同意这组裁定：
>
> 1. manual 导入采用 managed-copy + original-path provenance 的 Hybrid；
> 2. extracted text 按 contentHash 缓存；
> 3. C1a 支持 UTF-8 / UTF-8 BOM 的 TXT 与 Markdown，非法编码/二进制明确拒绝；UTF-16 暂不要求但不永久排除；
> 4. 资源超限必须明确拒绝，具体阈值作为实现安全参数；
> 5. AI 架构 provider-neutral，当前不选具体厂商；
> 6. cloud AI 默认不上传，仅 Human 显式 extraction 时允许，并显示 payload scope；
> 7. v1 credential 不持久化明文，先采用环境变量/BYOK seam，secret 不进入 repo/Profile/manual/candidate；
> 8. confidence 使用 label + optional provider raw opaque value，不解释成概率；
> 9. Candidate v1 session-only，AI raw response 不长期保存；
> 10. C1 拆为 C1a TXT/Markdown 与 C1b PDF/DOCX；
> 11. DOCX 具体依赖方案推迟到 C1b 前单独冻结；
> 12. PDF text-extraction 方案推迟到 C1b 前单独冻结；OCR 继续 deferred。”

### 48.2 逐项解释与分类

| # | 裁定 | 分类 | 冻结语义 |
| --- | --- | --- | --- |
| 1 | Hybrid storage | **HUMAN-FROZEN** | 成功 import 后应用拥有 **managed copy**；`originalPath` **只作 provenance**。读取 / 预览 / 缓存 / 证据**不得依赖 original source 继续存在**；原文件移动·重命名·删除**不得**使已导入 manual 失去 deterministic source。 |
| 2 | contentHash cache | **HUMAN-FROZEN** | extracted text 按 **source content identity** 缓存。contentHash = **SHA-256(source bytes)**，hex 呈现。**禁止**用 filename / mtime / original path 冒充 content identity。 |
| 3 | encoding | **HUMAN-FROZEN** | C1a 支持 **UTF-8** 与 **UTF-8 with BOM**；BOM **不进入用户可见正文**；**strict decode**，非法 UTF-8 明确失败，明显 binary 明确失败；**UTF-16 = C1a v1 报告 `unsupported_encoding`**，但**不得**写成「产品永久不支持 UTF-16」。 |
| 4 | resource safety | **HUMAN-FROZEN（语义）** + **implementation detail（数值）** | 必须存在安全上限；超限**明确拒绝**；**禁止 silent truncate**。具体阈值 = **implementation safety limit**，**不是** Human-frozen 产品常量，须在代码/测试/docs 中如此标注。 |
| 5 | AI provider-neutral | **HUMAN-FROZEN（未来 guardrail）** | 架构必须 provider-neutral；**当前不选任何厂商**。C1a 不涉及。 |
| 6 | cloud upload | **HUMAN-FROZEN（未来 guardrail）** | 默认**不上传**；仅 Human 显式 extraction 时允许，并显示 payload scope。C1a 不涉及。 |
| 7 | credential | **HUMAN-FROZEN（未来 guardrail）** | v1 不持久化明文；环境变量 / BYOK seam；**secret 不进入 repo / Profile / manual / candidate**。C1a 不涉及。 |
| 8 | confidence | **HUMAN-FROZEN（未来 guardrail）** | label + optional provider raw opaque value；**不得**解释成概率。C1a 不涉及。 |
| 9 | Candidate session-only | **HUMAN-FROZEN（未来 guardrail）** | Candidate v1 session-only；AI raw response 不长期保存。C1a 不涉及。 |
| 10 | C1 = C1a + C1b | **HUMAN-FROZEN** | C1a = TXT/Markdown；C1b = PDF/DOCX。 |
| 11 | DOCX 依赖推迟 | **HUMAN-FROZEN** | DOCX 具体依赖方案**推迟到 C1b 前单独冻结**；C1b 状态 = **NOT STARTED / DEPENDENCY DECISION DEFERRED**。 |
| 12 | PDF 方案推迟 | **HUMAN-FROZEN** | PDF text-extraction 方案**推迟到 C1b 前单独冻结**；**OCR 继续 deferred**。 |

**补充冻结（§48.3 展开）**：Markdown 语义（E）、no-profile-mutation（F）。

### 48.3 C1a FROZEN CONTRACT

**A. Hybrid storage（HUMAN-FROZEN）**
1. 成功 import 后：managed copy 存在于 app-managed user-data（`QStandardPaths::AppDataLocation`，沿用 `ProfileStore` 既有模式）。
2. `originalPath` 仅作 provenance 元数据，**不参与**读取/预览/缓存/证据解析。
3. 成功 import 后 original 文件移动/重命名/删除 ⇒ manual 仍可 load/preview。
4. 持久化目录**不得硬编码**用户绝对路径；测试必须能使用 isolated temporary root（沿用 `ProfileStore::setManagedRootOverride` 模式）。

**B. contentHash（HUMAN-FROZEN）**
- `contentHash = SHA-256(imported source bytes)`，lowercase hex 呈现。
- extracted-text cache 的主 identity **只**是 `contentHash`。
- 同 `contentHash` 允许复用 cache；但 **C1a 不发明 dedup UI / merge semantics**，**不得**把两个 Human import 悄悄合并成一个 document。

**C. encoding（HUMAN-FROZEN）**
- strict UTF-8；**禁止**使用会静默插入 replacement character 却仍报告成功的 decode path。
- UTF-8 BOM：识别 + 剥离；**BOM 不进入 extracted text**。
- UTF-16 BOM：明确 `unsupported_encoding`（**不是** binary、不是乱码成功）。
- 明显 binary：明确 `binary_content` 错误。heuristic = implementation detail，但**至少**捕获 NUL-containing content 与无法 strict UTF-8 decode 的输入。
- **不得**把合法中文 / emoji / 非 ASCII UTF-8 误判 binary。

**D. resource safety（语义 FROZEN / 数值 implementation detail）**
- 超限 ⇒ 明确拒绝，**不得 silent truncate**。
- 数值 = **implementation safety limit**，在代码/测试/docs 中显式标注为 safety limit（非产品常量）。

**E. Markdown（HUMAN-FROZEN）**
- C1a 的 Markdown = **deterministic source text import**，**不要求** renderer。
- **不得**执行 HTML / script / 自动 fetch remote resource / 自动下载 image，Markdown 内容**不得**触发任何 network side effect。
- 预览 = **plain text**。

**F. no profile mutation（HUMAN-FROZEN）**
- Manual Import **不得**修改 verified DeviceProfile / Active Profile / Raw / Generic Decode / Semantic Overlay / `TransactionAnalysis`。
- **不得**因 import 自动选择或绑定 Active Profile。
- **不得**根据 COM port / slave address / read function code / manufacturer·model 自动绑定 profile。
- C1a 只建立：`ManualDocument` + deterministic source + evidence foundation。Candidate → Profile 属 C2/C3。

### 48.4 C1a scope / non-scope

```text
# C1a IN SCOPE
TXT / Markdown deterministic import
managed copy + contentHash cache + metadata persistence
strict UTF-8 / UTF-8 BOM decode
binary / invalid-encoding / unsupported-encoding / resource-limit rejection
ManualDocument + EvidenceReference foundation
Device Profile workspace UI（导入 / 列表 / 详情 / read-only plain-text preview）
isolated test-root seam；targeted tests；QML/Windows-QPA gate

# C1a OUT OF SCOPE（本轮禁止）
PDF / DOCX / OCR（= C1b，NOT STARTED / DEPENDENCY DECISION DEFERRED）
AI extraction / AI provider / cloud upload / credential UI
Candidate / Accept / Edit / Reject
Manual Q&A / chat / RAG（= M12-D）
canonical package / LKGC advancement
修改 DeviceProfile schemaVersion=1
把 ManualDocument 塞进 DeviceProfile JSON
```

### 48.5 C1b / C2 / C3 / M12-D 状态（本轮不变）

```text
C1b = NOT STARTED / DEPENDENCY DECISION DEFERRED
C2  = NOT STARTED
C3  = NOT STARTED
M12-D = NOT STARTED
verified LKGC = 13799d633291abd69b66ab1c324699ac5014143a（不推进）
```

**口径纪律**：PDF / DOCX **仍属 M12-C canonical scope**，只是 **C1b deferred**；本轮**不得**删除 canonical PDF/DOCX requirement、**不得**写成 out-of-scope for M12-C、**不得**写成 “unsupported forever”。

---

## 49. M12-C C1a — UI Layout Correction + Automated Evidence（2026-09-27）

> **状态口径（不得改写）**：**C1a PRODUCT(QML) GATES = PASS · Release 全量 = PASS ·
> DEBUG = ENVIRONMENT HOLD**。**不得**写成 `C1a PASS`，**不得**写成 `C1a FAIL`，
> **不得**写成 `AUTOMATED COMPLETE`。本节的 behavior 变更**未提交**（见 §49.7）。

### 49.1 QML 几何 RCA（先取证，后修改）

`--qml-manual-import-check` 新增 `MANGEO` / `MANCHAIN` 逐节点取证输出（x/y/w/h/
implicitW/implicitH/visible + parent chain）。实测得到两个**互相独立**的真实根因：

**根因 1 —— 测量发生在未结算的布局上（harness 缺陷，非产品缺陷）**
第一次取证（原 stage 0，与 `window->resize(1000,700)` 同一 step）读到的是
**resize 尚未传播到 scene graph** 的旧几何：

```text
MANCHAIN s0 d5 QQuickColumnLayout: w=1024 h=720      ← 旧窗口尺寸
MANCHAIN s0 d1 QQuickColumnLayout: iw=903 ih=344
MANGEO  s0 manualImportHost: w=0 h=0                  ← 假 0
```

⇒ 「card 0×0」在 s0 是**测量时机**问题。

**根因 2 —— preview 视口真的塌成 0（真实产品缺陷）**
在结算后的 s6（1000×700）取证：

```text
MANCHAIN s6 d0 manualImportHost: x=61 y=556 w=935 h=140   ← wrapper 有尺寸
MANGEO  s6 manualImportCard:     w=935 h=140              ← anchors.fill 成立
MANGEO  s6 manualImportBody:     h=35                     ← 内容被压扁
MANGEO  s6 manualPreview:        w=653 h=0                ← 唯一真正的 0
```

⇒ 卡片高 140 不足以容纳 header(15) + actions(34) + body；`manualPreview`
（`Layout.fillHeight: true`）在无下限约束下被压到 **h=0**。

### 49.2 精确修复（最小、非掩盖）

```text
A. gate：stage 0 拆为 stage 0 + stage 0b（几何测量单独 step，
   不在同一 step 内 resize+measure）；新增 requireSized（visible + w>0 + h>0）；
   stage 0b/s6 各做一次 MANGEO/MANCHAIN dump。
B. QML wrapper Item：Layout.fillWidth + preferred/minimumWidth(600/240) +
   preferred/minimumHeight(160) + objectName "manualImportHost"。
C. manualImportBody：Layout.minimumHeight 60；
   manualPreview：Layout.minimumHeight 36。
D. 详情列：移除抢占高度的第三行 provenance Label（objectName 由
   manualDocSource 删除；contentHash 行保留）——不删除任何信息能力，
   只回收垂直空间。
```

**未使用**：clip 掩盖 overflow、删除内容、固定 1000px 宽、在 controller 里硬编码几何、
gate 跳过 Manual 区域。§1 的「core/store/controller 保持 ZERO/MINIMAL DIFF」满足：
`ManualDocument.*` / `ManualStore.*` / `ManualImportController.*` 的**最终**内容
与本轮开始时一致（本轮全部 mutation 均已精确逆向还原，见 §49.6）。

### 49.3 自动化证据（Release，真实执行）

```text
ctest（Release，完整 52 项）        → 100% tests passed, 0 tests failed out of 52
manual_import                      → PASS（22 个测试函数）
qml_manual_import_check            → PASS（MAN-Q01..Q12；含 requireSized 与越窗断言）
qml_profile_editor_check           → PASS（M12-B 既有门禁，无回归）
qml_register_map_check             → PASS（同上）
qml_active_profile_check           → PASS（同上）
qml_geometry_check                 → PASS
qml_nav_check                      → PASS
qml_write_foundation_check_windows → PASS（真实 windows QPA，含于 52 项内）
诊断计数（LastTest.log）           → ReferenceError=0 · TypeError=0 ·
                                     Unable to assign=0 · String.arg Invalid arguments=0
```

### 49.4 真实负向对照（各含真实 RED → 精确逆向 patch → 基线复绿）

| NC | mutation | 真实 RED（逐字） | 还原后 |
| --- | --- | --- | --- |
| NC-C1A-1 | `loadAll()` 丢弃 `originalPath` 已不存在的文档 | `FAIL! : ManualImportTest::originalSourceDeletionDoesNotBreakTheManagedDocument() Compared values are not the same`（21 passed / 1 failed） | GREEN |
| NC-C1A-2 | 忽略 `QStringDecoder` 的 strict-UTF-8 失败结果 | `FAIL! : ManualImportTest::invalidUtf8WithoutNulIsRejected() '!result.ok()' returned FALSE.`（21/1） | GREEN |
| NC-C1A-3 | cache 路径改用 `originalFileName` 而非 `contentHash` | 8 项 RED，含 `contentHashIsDeterministic() Compared values are not the same`（14/8） | GREEN |
| NC-C1A-4 | import 提交后覆写 profiles 目录下的 profile JSON | `FAIL! : ManualImportTest::persistedProfileJsonIsUntouched() Compared values are not the same`（21/1） | GREEN |

还原方式一律 **precise reverse patch**；未使用 `git checkout --` / `git restore` / `git reset`。
还原后 `ManualStore.cpp` 复核：无 mutation 残留（`grep -n "MUTATION|ProfileStore|victims"`
= 0 命中），370 行，目标行内容与设计一致。

### 49.5 Debug ENVIRONMENT HOLD（独立轨道，与 QML 缺陷**不是**同一根因）

```text
工具链      → GNU ar / ranlib (GNU Binutils) 2.39
ar         → D:\QT\Tools\mingw1310_64\bin\ar.exe
ranlib     → D:\QT\Tools\mingw1310_64\bin\ranlib.exe
TMP/TEMP   → C:\Users\付\AppData\Local\Temp（非 ASCII，实测可正常写入）
磁盘       → E: 61G free / C: 46G free（非空间问题）
archive 路径 → 54 字符（非长度问题）
```

**现象**：Debug 的 `libmodbuslens_core.a` ≈ **14.7 MB**；`ar`/`ranlib` 在写归档时
报 `could not create temporary file whilst writing archive: no more archived files`。
Release 的同一归档仅 ≈ **198 KB**，正常。

**产品无关最小探针（结论性）**：

```text
① 同一批 25 个 obj（14.1 MB）
   · 目标 E:\tmp\probe_big（repo 外）→ ar qc 成功 14732408 B；
     ranlib 连续 8/8 失败
   · 目标 E:\desktop\ModbusLens\build\debug（repo 内）→ ar qc 失败
   · 1.79 MB（6 obj）→ ar/ranlib 在 C:/E:、repo 内外**全部成功**
② 仅用**既有** obj（排除新增 ManualDocument.cpp.obj）：14 693 920 B →
   ranlib 仍失败 ⇒ **与本轮 C1a 改动无关**
③ Debug 编译阶段：src/core/manual/* + src/ui/manual/* + tests/test_manual_import.cpp
   + src/main.cpp 以 Debug 风格旗标（-g -O0 -std=c++20 -Wall -Wextra，-fsyntax-only）
   实测 **0 error**
```

**RCA 结论**：失败位于 **binutils 写归档时的临时文件创建**，阈值约 2 MB，
且对目标目录敏感（repo 内 build 目录更易触发）、**间歇性**（同一命令曾偶发成功）。
判定 = **环境 / 工具链 / 沙箱层面**，**不**是 C++ 编译错误、**不**是产品缺陷、
**不**是 C1a 引入。**未**为绕过它修改产品代码、**未**改编译优化、**未**把 Release PASS
冒充 Debug PASS。

**未满足的验收面**：`Debug full CTest` **无法执行**（本机所有下游边都依赖该归档，
`ninja -k 0` 报 `cannot make progress due to previous errors`）。另有独立既有阻塞
（T027 §44 家族）：`windeployqt --version` 仍报
`Unable to query qtpaths: Error running binary qtpaths: pipe: rc=0`（`qtpaths` 自身 rc=0）
⇒ canonical deploy / 全新 portable 部署树仍不可用。

### 49.6 behavior 未提交（HOLD 路径）

```text
期望 HEAD = f500cf37cfc7cffd8d51a4650303adcc094ecb2b（docs-only 裁定归档）
本轮：未创建 behavior commit（Debug 处于 environment HOLD，按 Human/ChatGPT 约定
      不创建「已完成」behavior commit）
工作树：有价值的 WIP 原样保留（CMakeLists.txt / src/main.cpp / src/ui/qml/Main.qml /
      src/ui/qml/pages/DeviceProfilePage.qml 修改；src/core/manual/ + src/ui/manual/ +
      tests/test_manual_import.cpp 新增）
未做：未创建 visual candidate（§12 前提为「完整 automated acceptance 可交 Human」，
      当前 Debug 未满足）；未 push / tag / amend；未推进 LKGC；未 package
```

**工作树异常观察（非本轮产生）**：`git status` 出现 3 个**非本轮创建**的未跟踪文件
（`_count_tests.py`、`_map_scenarios.py`、`scripts/bench_replay/_run_1m.txt`，
mtime 2026-09-27 16:52–16:54）。本轮**未创建、未修改、未删除**它们；归档在案待 Human 确认归属。

---

## 50. M12-C C1a — ENVIRONMENT-ONLY DEBUG UNBLOCK ATTEMPT（2026-09-27，结果 = 未解除）

> Human/ChatGPT 裁定：**暂不接受 Release-only 作为最终 C1a automated closure**，先做一次严格的
> **environment-only** Debug unblock 尝试。本轮**未**修改产品 source / tests / CMake behavior /
> Debug flags / optimization / acceptance criteria；**未**创建 behavior commit / visual candidate。

### 50.1 首选假设与实验设计

假设 = 「binutils 对 **non-ASCII temp path** 存在兼容问题」。做法：建立 **repo 外纯 ASCII 临时目录**
`E:\tmp\modbuslens-binutils`（空、可写），**仅在本轮命令环境中**显式设置
`TEMP` / `TMP` / `TMPDIR`（**未**永久修改系统环境变量）。

**环境继承已实证**（子进程读到的是 Windows 形式）：

```text
TEMP= 'E:\\tmp\\modbuslens-binutils'
TMP= 'E:\\tmp\\modbuslens-binutils'
TMPDIR= 'E:\\tmp\\modbuslens-binutils'
```

### 50.2 Observed（既有事实）

```text
原 TEMP/TMP/TMPDIR = C:\Users\付\AppData\Local\Temp（含非 ASCII 用户目录「付」）
Debug libmodbuslens_core.a ≈ 14.7 MB
ar / ranlib（GNU Binutils 2.39）→ could not create temporary file whilst writing archive:
                                  no more archived files
```

### 50.3 Controlled comparison（同一 ar.exe / ranlib.exe / 同一 obj set / 同一命令；唯一变化 = temp env）

| 组 | 目标 | temp env | 结果 |
| --- | --- | --- | --- |
| A | repo 外 `E:\tmp\probe_big` | ASCII `E:\tmp\modbuslens-binutils` | `ar` **3/3 FAIL**（archive size 8 = 仅 magic） |
| B | repo 内 `build\debug` | ASCII（同上） | **1/3 PASS**（14 732 408 B）→ 2/3 FAIL ⇒ **间歇** |
| M1 | repo 外 `E:\tmp\probe_big` | **默认**（非 ASCII） | `ar` **3/3 FAIL** |
| R1 | repo 内（ninja 原命令） | 默认 | **12/12 FAIL** |
| R2 | repo 内（ninja 原命令） | 默认 | **40/40 FAIL**（累计 52 连败） |

**⇒ CASE B（非稳定 PASS）**：ASCII temp **不是解**；且**非** Unicode-temp 专属问题。

### 50.4 进一步排除（按 §4 清单）

```text
· 环境继承          → 已实证子进程收到 ASCII 路径（python 子进程实测）
· 归档目标权限      → dd 直写 15 MB 进 build\debug 成功（≈998 MB/s）⇒ 权限/磁盘/配额正常
· 磁盘空间          → E: 61G free / C: 46G free
· 文件锁 / 残留     → ASCII 临时目录 0 entries；其中此前写入的 15 MB 探针文件**消失**
                      ⇒ 存在**外部进程**在清理该目录（与本轮工作树出现的非本 Agent
                      未跟踪文件同源现象）
· 默认 temp 目录内容 → 含沙箱内部物：codebuddy-shell-payload-*、codebuddy-safe-delete
· 安全软件（可观察） → Lenovo Anti-Virus powered by Huorong Security（state=262144）+
                      Windows Defender（state=397568）；usysdiag pid=20772、wsctrl11 pid=19576
· 纯 ASCII cwd 复现  → probe A 的 cwd = E:\tmp\probe_big（纯 ASCII）仍 FAIL
· **size 相关性已消失** → 当前环境下 ar 连 **1 个 object** 都失败（size=8）；
                      先前 1.79 MB 的成功不再可复现
```

**措辞纪律**：安全软件并存是**观察到的相关性**，**不是**已证明的因果；本文件**不**写
「GNU ranlib 确定存在 Unicode bug」，也**不**断言具体拦截机制。

### 50.5 结论与状态

```text
DEBUG ENVIRONMENT HOLD REMAINS
· 失败位于 environment / toolchain / sandbox 层面（非产品缺陷、非 C1a 引入、
  非编译错误；Debug 编译阶段此前已实测 0 error）
· ar 的临时文件创建在本会话内由「>≈2 MB 间歇失败」劣化为「全部失败」⇒ 与时间/会话状态相关
· 未解除的手段：ASCII temp（已证伪）；未尝试且**不允许**的手段：
  改产品代码 / 改 tests / 改 CMake behavior / 降 Debug flags / 改 optimization /
  替换 acceptance criteria
· build\debug\libmodbuslens_core.a 当前**不存在**（ninja 的 rm -f 移除后 ar 无法重建）
  —— 仅构建产物状态，非 repo 跟踪内容
· C1a 仍为：PRODUCT GATES = PASS / RELEASE FULL = PASS / DEBUG = ENVIRONMENT HOLD
· behavior 仍**未提交**；visual candidate 仍**未创建**；WIP 原样保留
```

---

## 51. M12-C C1a — AUTOMATED CLOSURE + BEHAVIOR COMMIT + VISUAL CANDIDATE（2026-09-27）

### 51.1 Human external Debug evidence（AUTHORITATIVE，逐字归档）

Human 在 **WorkBuddy 之外的普通 Windows shell** 完成决定性 Debug 验证，使用
`cmake = D:\QT\Tools\CMake_64\bin\cmake.exe`、`ctest = D:\QT\Tools\CMake_64\bin\ctest.exe`、
`ninja = D:\QT\Tools\Ninja\ninja.exe`，`TEMP/TMP/TMPDIR = E:\tmp\ext-probe`，目标
`E:\desktop\ModbusLens\build\debug`：

```text
Debug build   : [148/148] Linking CXX executable modbuslens.exe
                DEBUG BUILD EXIT = 0
Debug CTest   : 52/52 PASS  ——  100% tests passed, 0 tests failed out of 52
                Total Test time (real) = 121.72 sec
                DEBUG CTEST EXIT = 0
其中包含     : manual_import PASS · qml_profile_editor_check PASS ·
              qml_register_map_check PASS · qml_active_profile_check PASS ·
              qml_manual_import_check PASS · qml_geometry_check PASS ·
              qml_nav_check PASS · qml_write_foundation_check_windows PASS
同一 shell 的 large archive probe :
              同一 MinGW ar.exe / 同一 ranlib.exe / 同一批 ≈14.7 MB Debug obj
              3/3 → ar=0 · ranlib=0 · size=14732408
```

⇒ **DEBUG ENVIRONMENT HOLD = RESOLVED BY EXTERNAL HUMAN-SHELL CONTROL。**
**措辞纪律**（不得越界）：**不**声称 binutils 已被「修复」；**不**声称 Trae 为根因；
**不**声称 Defender / 火绒 为根因。已证明的仅是：**同一 machine / toolchain / object set
在普通 Windows shell 中可稳定完成 large archive，且完整 Debug build + 52/52 CTest PASS**。
据此，WorkBuddy sandbox 内的 ar/ranlib archive failure **不得**再作为 C1a Debug acceptance failure。

### 51.2 WorkBuddy 侧本轮 Release 最终回归（真实执行）

```text
cmake --build --preset release-local → build rc=0（ninja: no work to do ⇒ 已验证产物与本树一致）
ctest（Release，完整）               → 100% tests passed, 0 tests failed out of 52
                                     （52 条 Start/Passed 条目实测；count 以本轮发现为准）
包含                                 → manual_import · qml_manual_import_check ·
                                     qml_profile_editor_check · qml_register_map_check ·
                                     qml_active_profile_check · qml_geometry_check ·
                                     qml_nav_check · qml_write_foundation_check_windows 全部 PASS
```

**§4–§7 条件全部满足** ⇒ 允许写：**M12-C C1a = IMPLEMENTED / AUTOMATED PASS /
HUMAN REVIEW PENDING**（这是 **automated acceptance**，**不是** Human visual PASS、
**不是** LKGC advancement、**不是** C1a Human acceptance）。

### 51.3 Behavior commit

```text
commit  = 7bcd2ca0b72a0fe22ecb0b719c12cbdb2230833c
parent  = 56e64e2e79ef0848487ca55ccd66f84752f37193
subject = M12: add deterministic manual import foundation
paths   = CMakeLists.txt, src/core/manual/ManualDocument.{h,cpp},
          src/ui/manual/{ManualStore,ManualImportController}.{h,cpp},
          src/main.cpp, src/ui/qml/Main.qml,
          src/ui/qml/pages/DeviceProfilePage.qml, tests/test_manual_import.cpp
          （11 files changed, 2243 insertions(+), 2 deletions(-)）
NO AMEND。未混入 docs/ / _ctx.py / _dump.py / build/ / 任何 probe artifact。
```

### 51.4 Qt License Service message —— disposition

Human-shell Debug build 期间 AutoMoc / moc 多次报告：

```text
Could not initialize license client
Cannot find a license service installation that matches version "3.6.4"
Cannot acquire license to use qtframework
```

**disposition = NON-BLOCKING ENVIRONMENT WARNING OBSERVED DURING HUMAN-SHELL DEBUG BUILD**。
依据：build 继续完成至 `[148/148] Linking CXX executable modbuslens.exe`，`DEBUG BUILD EXIT = 0`，
`DEBUG CTEST = 52/52 PASS`。**不**写成 C1a product failure；**不**声称 license valid；
**不**声称 license invalid；**未**由本 Agent 设置 `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`
（本轮**未使用**该 bypass）；**未**修改任何 license configuration；**未**替 Human 接受任何
license terms。该事项作为独立环境维护项留待 Human 处置。

### 51.5 WIP 完整性 / negative-control 还原 / protected diff 审计

```text
MUTATION marker 残留          → 0 命中（src/ tests/ CMakeLists.txt）
original-path dependency      → 无（loadAll 不含 originalPath 过滤）
strict UTF-8 bypass           → 无（decoder.hasError() || finalized.invalidChars 仍在）
cache identity regression     → 无（cache = <contentHash>.txt）
Profile JSON pollution         → 无（manual 层零 ProfileStore 耦合，仅注释中提及）
contentHash                   → 仍为 QCryptographicHash::Sha256(source bytes)
protected authority（vs verified LKGC 13799d6）
  TransactionAnalysis.cpp / RegisterDecode.cpp / DeviceProfile.{h,cpp} /
  ProfileStore.{h,cpp} / ProfileController.cpp / ActiveProfileController.cpp
                              → 全部 UNCHANGED
tracked 改动路径（vs LKGC）   → 仅 CMakeLists.txt · src/main.cpp ·
                                src/ui/qml/Main.qml · DeviceProfilePage.qml
DeviceProfile schemaVersion   → 未因 ManualDocument 改动；ManualDocument 使用
                                自己的 schemaVersion=1 与独立目录，绝不写入 DeviceProfile JSON
```

### 51.6 Visual candidate（staging）—— 已组装 + 验证 → **交付受阻**

```text
路径            = build\m12c-visual-candidate\
staging 机制     = 复用 M12-B 已实际验证过的自包含 staging（Qt DLL 集 + plugins +
                  ModbusLens QML module dir）+ 本轮 Release exe；**未**运行 windeployqt
                  （其 `Unable to query qtpaths: … pipe: rc=0` 问题依旧），
                  **未**伪装 canonical deployment PASS
exe 身份         = candidate ModbusLens.exe SHA-256
                  7b486eea9ab5e9922f0c36a90f211ad3295fcd3fc97c5a804bd471069708feae
                  == source build\release\ModbusLens.exe（byte-identical）
QML module       = 逐份复制自 build\release\ModbusLens\（含 manualImportCard 的新版
                  DeviceProfilePage.qml；staging 树确实**随源码刷新**，非旧副本）
launcher         = Run-C1a-Manual-Import.cmd（最小 PATH=System32；**不**自动退出）
samples          = samples\manual-utf8.txt（UTF-8，无 BOM，首字节 46 72 65）
                  samples\manual-bom.md（UTF-8 **带 BOM**，首字节 EF BB BF，含中文 /
                  ASCII / Markdown heading / 普通 link / 图片语法 / script 字样）
clean-env 启动   = 通过（最小 PATH 下 exe 正常启动并进入 QML）
lifetime         = 通过（bare 启动后 8 s 仍存活，PID 3924 / 175 376 K；已由本 Agent
                  手动终止并确认 0 残留进程）
Manual Import gate（staged 树、**真实 windows QPA**）→ **RED**
```

**真实缺陷（must not be hidden）**：在 **真 windows QPA + 1000×700** 下，Manual 区域内容
越出窗口 7 px：

```text
MANFAIL: manualDocumentList outside: x=73 y=640 w=250 h=67 win=1000x700
MANFAIL: manualPreview      outside: x=331 y=671 w=653 h=36 win=1000x700
（同次 dump：manualImportHost 935×160 正常；manualImportBody 只得 h=60，
  而 body 内容的实际最小高度在 windows 字体度量下为 67 ⇒ 子项溢出 body，
  再溢出卡片（card y=536..696）与窗口（底边 707 > 700））
```

**根因**：`manualImportBody.Layout.minimumHeight: 60`（为 **offscreen** 字体度量选定）
小于 windows 度量下内容的最小高度（67）；卡片固定高 160 无法容纳
`header 20 + actions 34 + body 67 + spacing + padding`。**这是本轮 C1a 引入的真实缺陷**，
且**正是** `qml_write_foundation_check_windows` 那条既有教训所覆盖的 offscreen 盲区
（offscreen 行高 12 px vs 真平台 16 px；本页 header 实测 offscreen 15 px vs windows 20 px）。
**offscreen 门禁无法发现它**，因此本轮 52/52（全 offscreen，除 windows 那一项）通过并不构成
「1000×700 在真平台可用」的证据。

**建议修复（未执行，需 Human 授权为新的一轮）**：
① 提高 Manual 区域的垂直预算（host/body/preview 的 minimum 与 preferred 按 **windows** 度量重算，
或把详情列再压一行）保证 body 内容在 1000×700 下完整落在窗口内；
② 新增 CTest 条目 `qml_manual_import_check_windows`（同命令、**不给** offscreen 环境），
沿用 `qml_write_foundation_check_windows` 的既有先例，关闭该盲区；
③ 因②会改变测试数量，**必须**重跑 Release 全量 + 由 Human 在普通 shell 重跑 Debug 全量，
新 head 才能重新取得 automated acceptance。

### 51.7 状态与下一步

```text
M12-C C1a（at commit 7bcd2ca）= IMPLEMENTED / AUTOMATED PASS / HUMAN REVIEW PENDING
VISUAL CANDIDATE              = ASSEMBLED + IDENTITY-OK + LIFETIME-OK
                                BUT **NOT HANDED OVER**：真实 windows QPA 1000×700
                                几何缺陷（越窗 7 px）必须先修复
DEBUG ENVIRONMENT HOLD        = RESOLVED BY EXTERNAL HUMAN-SHELL CONTROL
verified LKGC = 13799d633291abd69b66ab1c324699ac5014143a（不推进）
未做：未 amend · 未 push · 未 tag · 未创建 canonical package · 未开始 C1b/C2/C3/M12-D
```
**〔2026-09-27 追加批注〕本块的 `verified LKGC = 13799d6…（不推进）` 为当时事实，保留不删；
当前 canonical verified LKGC 已由 §53.4 更新为 `8409c271cca966e9f9ab0ad0ba2d6470c0a66e10`（Human authorized）。**

---

## 52. M12-C C1a — WINDOWS-QPA GEOMETRY CORRECTION（2026-09-27，收口完成）

### 52.A 被 supersede 的旧状态（不得再作当前终态）

```text
旧 behavior commit : 7bcd2ca0b72a0fe22ecb0b719c12cbdb2230833c
当时证据           : Debug 52/52 PASS（Human 外部 shell）
                     Release 52/52 PASS（WorkBuddy 内）
旧状态表述         : M12-C C1a = IMPLEMENTED / AUTOMATED PASS / HUMAN REVIEW PENDING
```
该表述**已被后续真实 defect evidence supersede**：随后在 **真实 Windows QPA** 下对
staged candidate 做验证时发现 **1000×700 几何缺陷**。**不得**再把 `7bcd2ca` 的
52/52 当作 C1a 的当前终态；`6b59cef` 中归档的「automated acceptance」同样按本批注 supersede。

### 52.B pre-fix Windows-QPA RED（新门禁先行，真实 RED）

新增 **`qml_manual_import_check_windows`**（test **#53**）后，在**修改布局之前**单独执行，
真实 RED 并逐字复现缺陷：

```text
MANFAIL: manualDocumentList outside: x=73 y=640 w=250 h=67 win=1000x700   (bottom 707 > 700)
MANFAIL: manualPreview      outside: x=331 y=671 w=653 h=36 win=1000x700  (bottom 707 > 700)
0% tests passed, 1 tests failed out of 1
```

**offscreen 门禁看不见它**：同一二进制在本页 header 上 offscreen 为 15 px、真实 Windows
为 20 px，逐层累积后 offscreen 判定通过而真平台溢出。

### 52.C 修复性质（content-driven，**不是**魔法数字）

```text
根因  : Manual 区 wrapper 的**固定高度 160 px** < 其自身内容在 windows 度量下的最小高度
        （卡片 implicitHeight 183）⇒ 布局无法同时满足「固定 160」与「内容需 183」，
        子项溢出卡片与窗口。
修复  : DeviceProfilePage.qml —— wrapper 的 Layout.preferredHeight 改为
        **manualImportCardItem.implicitHeight**（PanelCard 由内部 contentLayout.implicit +
        2×padding 推导），并给 Layout.minimumHeight 120 作为下限。
        ⇒ 「固定高度低于内容最小高度」这一整类缺陷**在构造上被消除**；
        **不是** 60 → 67，**不是** 160 → 183 的硬编码打补丁。
附带  : 卡片新增 id `manualImportCardItem` —— QML **不能**用 objectName 字符串做属性引用，
        否则运行时 **ReferenceError**（首次尝试即被 FAIL_REGULAR_EXPRESSION 门禁捕获）。
gate  : src/main.cpp 只**新增**断言（profileCatalogCard / profileRegisterCard 在窗口内；
        以及 profile 区与 Manual 区**不重叠**），未放宽任何既有判断。
门禁  : CMakeLists.txt 新增 qml_manual_import_check_windows —— 复用**同一条生产命令**
        `--qml-manual-import-check`（未复制第二份实现），并**显式** `QT_QPA_PLATFORM=windows`，
        使「继承到 offscreen」不可能把该门禁悄悄降级；同时纳入 FAIL_REGULAR_EXPRESSION。
```

### 52.D 修正后真实 Windows QPA / 1000×700 几何（实测）

```text
deviceProfileWorkspace x=57 y=41 w=943 h=659              → bottom 700
profileWorkspaceRow    x=61 y=123 w=935 h=378（原 401）   → bottom 501
manualImportHost       x=61 y=513 w=935 h=183（h == implicitHeight）→ bottom 696
manualImportCard       x=61 y=513 w=935 h=183（h == implicitHeight）→ bottom 696
manualImportBody       h=67（h == implicitHeight，不再被压扁）
manualDocumentList     x=73 y=617 w=250 h=67              → bottom 684
manualPreview          x=331 y=648 w=653 h=36             → bottom 684
非重叠               : profileWorkspaceRow bottom 501 ≤ manualImportHost top 513
```

### 52.E Release 全量

```text
cmake --build --preset release-local → rc=0
ctest --output-on-failure            → 100% tests passed, 0 tests failed out of 53
测试数变化                            → 52 → 53（新增 qml_manual_import_check_windows）
targeted 9/9 PASS；诊断 ReferenceError=0 / TypeError=0 / Unable to assign=0 / String.arg Invalid=0
```

### 52.F Human 外部 Debug 复验（AUTHORITATIVE，逐字对照）

```text
cmake --build build\debug   → ninja: no work to do.        DEBUG BUILD EXIT = 0
（正确解释：Human 执行时 Debug build tree 已 up-to-date；**不得**伪写成重新编译了 N/N objects）
ctest --test-dir build\debug -N
                            → 明确列出 Test #51 qml_manual_import_check ·
                              #52 qml_write_foundation_check_windows ·
                              #53 qml_manual_import_check_windows
                              Total Tests: 53                CTEST LIST EXIT = 0
Debug full CTest            → 53/53 PASS · 100% tests passed, 0 tests failed out of 53
                              Total Test time (real) = 103.10 sec
                              DEBUG CTEST EXIT = 0
其中 PASS：qml_profile_editor_check · qml_register_map_check · qml_active_profile_check ·
           qml_manual_import_check · qml_write_foundation_check_windows ·
           qml_manual_import_check_windows
⇒ DEBUG REVALIDATION PENDING **解除**
```

### 52.G 最终 automated 状态

```text
M12-C C1a = IMPLEMENTED / AUTOMATED RE-ACCEPTANCE PASS / HUMAN VISUAL REVIEW PENDING
（这是 automated 结论：**不是** Human visual PASS，**不是** LKGC advancement。）
correction commit = 8409c271cca966e9f9ab0ad0ba2d6470c0a66e10（parent 6b59cef，NO AMEND）
```

### 52.H candidate 与 ManualStore 事实（不得美化）

```text
candidate        = build\m12c-visual-candidate\（M12-B 已验证 self-contained staging 复用；
                   **未**运行 windeployqt / qtpaths 失败路径，**未**伪装 canonical deployment）
isolation seam   : **不存在** candidate 可用且无需改 tracked source 的 ManualStore root seam ——
                   `ManualStore::setManagedRootOverride` 只在 QML check harness 内调用，
                   无 CLI flag；无 QStandardPaths test mode。⇒ **不**为 candidate 再改产品代码
                   （否则会让刚取得的 Release 53/53 与 Human Debug 53/53 acceptance 失效）。
实际存储         : **production ManualStore semantics**（**不是** isolated store）
exact managed root: C:\Users\付\AppData\Roaming\ModbusLens\ModbusLens\manuals
启动前只读盘点   : 该 `manuals` 目录**此前并不存在**（`documents/` `source/` `text/` 均无），
                   即**无任何既有 manual 记录**
⇒ 本轮 sample import 新建的记录可明确识别，不存在破坏既有数据的风险；
  但**不**用 shell 清空任何 store，也**不**把 production store 称为 isolated store。
```

**已知能力缺口（如实记录）**：C1a 的 UI 只有 导入 / 列表 / 详情 / 纯文本预览，**没有**
manual 文档的 Delete 操作 ⇒ Human checklist 中「用产品自身 Delete 清理本轮两条记录」
**无法通过产品完成**。本轮**不**用 shell 代替产品删除；该清理动作的处置留给 Human，
并作为后续切片的输入项记录。

---

## 53. M12-C C1a — HUMAN ACCEPTANCE CLOSURE + VERIFIED LKGC ADVANCEMENT（2026-09-27 · **当前状态以本节为准**）

### 53.0 supersede 声明

本节的**当前状态**表述取代 §49–§52 中所有过程性状态。以下短语自本节起**只属历史过程**，
**不再是 current state**：

```text
HUMAN VISUAL REVIEW PENDING      （过程态，已由 Human visual PASS 取代）
DEBUG REVALIDATION PENDING       （过程态，已由 Human 外部 Debug 53/53 取代）
AUTOMATED RE-ACCEPTANCE PENDING  （过程态，已由 Release 53/53 + Debug 53/53 取代）
WINDOWS-QPA GEOMETRY DEFECT OPEN （过程态，已由 correction + windows gate 取代）
```

§49–§52 的原文**保留不删**（只增不改的档案区原则），但其「当前」字样一律以本节为准。

### 53.1 Human acceptance 与授权（逐字，验收边界严格执行）

```text
Human 验收原文 : “M12-C C1a visual PASS”
Human 授权原文 : “授权：归档 M12-C C1a Human visual PASS，
                  并将 verified LKGC 从
                  13799d633291abd69b66ab1c324699ac5014143a
                  推进到
                  8409c271cca966e9f9ab0ad0ba2d6470c0a66e10。”
```

**验收边界（AGENTS「Cross-Agent Context / Anti-Drift」第 5 条）**：该 PASS 覆盖
**已交付的整个 C1a visual checklist**（T027 §52 / 交付报告所载 8 项）。**不得**扩写为
「Human 逐字报告了每一个 checklist item 的单独结果」；归档只写 Human 返回的整体 PASS。
本授权**不包含** C1b START，也不包含任何后续 milestone 授权。

### 53.2 C1a acceptance chain（A–E）

```text
A. Behavior foundation
   7bcd2ca0b72a0fe22ecb0b719c12cbdb2230833c  M12: add deterministic manual import foundation
B. Windows geometry correction（最终授权 LKGC target）
   8409c271cca966e9f9ab0ad0ba2d6470c0a66e10  M12: fix manual import windows geometry
C. Automated final acceptance
   Release：53/53 PASS · 0 failed
   Debug  ：53/53 PASS · 0 failed（Human 外部 shell；real 103.10 sec；DEBUG CTEST EXIT = 0）
   新增 Windows gate：qml_manual_import_check_windows
   真实经历：pre-fix RED → correction → post-fix GREEN
D. Human visual acceptance
   Human 返回：M12-C C1a visual PASS（覆盖整个已交付 checklist）
E. Human LKGC authorization
   旧 13799d633291abd69b66ab1c324699ac5014143a
   新 8409c271cca966e9f9ab0ad0ba2d6470c0a66e10
```

### 53.3 最终状态（current）

```text
M12-C C1a            = COMPLETE / HUMAN ACCEPTED
M12-B                = COMPLETE
M12-C                = IN PROGRESS（C1a 完成 ≠ M12-C 完成）
M12-C C1b            = NOT STARTED / DEPENDENCY DECISION DEFERRED
M12-C C2             = NOT STARTED
M12-C C3             = NOT STARTED
M12-D                = NOT STARTED
M12 canonical package = NOT CREATED
REAL MODBUS HARDWARE  = NOT VERIFIED
最终 automated evidence：Release 53/53 PASS · Debug 53/53 PASS · Windows-QPA gate PASS
最终 Human evidence    ：C1a visual PASS
```

**本轮 Human 未授权**：C1b START、PDF route、DOCX route、C2、C3、M12-D。⇒ C1b 状态
**保持** `NOT STARTED / DEPENDENCY DECISION DEFERRED`，**不得**改写为 `STARTED` /
`IN PROGRESS` / `AUTHORIZED`。

### 53.4 verified LKGC advancement（Human authorized）

```text
旧 verified LKGC = 13799d633291abd69b66ab1c324699ac5014143a
新 verified LKGC = 8409c271cca966e9f9ab0ad0ba2d6470c0a66e10
                   （Human authorized）
ancestry 实测    : git merge-base --is-ancestor 8409c27 HEAD → exit 0（YES）
                   8409c27 subject = M12: fix manual import windows geometry
                   11a97ed subject = M12: archive C1a windows geometry correction
```

**为什么 LKGC target 是 `8409c27` 而**不是** docs commit（`11a97ed` 或本轮 closure commit）**：
`verified LKGC` 必须指向**已 Human 验收并明确授权的 behavior-bearing commit**；docs-only
提交**永不作 LKGC**（AGENTS「Git Policy」+ LKGC 治理）。因此 `11a97ed` 与本轮 closure
commit 均**不是** LKGC target。

**历史保留声明**：`13799d6…` 在 §46/§47/§48/§49/§50/§51/§52 与 PROJECT_STATUS/BACKLOG
的历史批注中出现，属**当时事实**，**保留不删**；只有「当前 canonical verified LKGC」与
本轮 advancement 记录被更新。

### 53.5 candidate / ManualStore 事实（本轮不变）

```text
Human 验收使用 = production ManualStore semantics（非 isolated store）
candidate      = build\m12c-visual-candidate\（仍**不是** canonical package / Final D / LKGC）
exact managed root = C:\Users\付\AppData\Roaming\ModbusLens\ModbusLens\manuals
```
本轮**未**删除 acceptance manual、**未**清空 ManualStore、**未**修改 profiles、**未**修改或
重新 staging candidate。C1a 没有 manual Delete UI **不构成**本轮 closure blocker；
**未**为清理数据新增任何产品行为。

### 53.6 本轮性质

**docs-only acceptance closure**：未做任何 behavior change；未改 `CMakeLists.txt` / `src/` /
`tests/` / `AGENTS.md` / `CODEBUDDY.md` / `build/` / candidate / production store；
未重跑 build / CTest / QPA gate（既有最终证据已足够）；未 push / 未 tag / 未 amend。

---

## 54. M12-C C1b — START + DEPENDENCY ROUTE FREEZE（2026-09-27 · **current state = STARTED / DEPENDENCY BUILD PROBE**）

### 54.1 Human 授权与冻结原文（逐字归档）

> “授权：M12-C C1b START。
> PDF 路线冻结为 PDFium public C API，
> 仅提取已有 text layer，
> OCR deferred；
>
> DOCX 路线冻结为
> libzip + Qt Core QXmlStreamReader，
> v1 提取 main document story 的
> deterministic plain text，
> 非正文扩展内容 deferred；
>
> 第三方依赖必须固定版本、
> 离线运行、
> 无运行时下载，
>
> 先做当前 Qt 6.11.1 MinGW 下的
> dependency/build probe，
>
> probe 不通过则 STOP，
> 不得自行替换依赖路线。”

**授权范围边界（不得外推）**：这是 **C1b START authorization + dependency-route freeze**，
**不是** C1b implementation acceptance，**不是** LKGC advancement authorization。
verified LKGC 保持 `8409c271cca966e9f9ab0ad0ba2d6470c0a66e10`。

### 54.2 冻结条款（HUMAN-FROZEN）

```text
PDF   = PDFium public C API ONLY
        目标 = 仅提取**已有 PDF text layer**
        OCR = DEFERRED（禁止 OCR / PDF rendering / AI extraction / 在线 PDF 转换）
DOCX  = libzip + Qt Core QXmlStreamReader
        v1 目标 = 取 main document story 的 **deterministic plain text**
        非正文扩展内容 DEFERRED：
          header / footer / comments / footnotes / endnotes /
          embedded objects / image OCR，以及其它不属于 main story v1 的扩展内容
第三方依赖 = 必须 **exact pin** · **离线 runtime** · **无 runtime download**
probe 目标环境 = Qt 6.11.1 · MinGW GCC 13.1 · Windows x64
```

**明确禁止的替代路线（不得自行替换）**：Poppler · QtPdf · QZipReader private API ·
LibreOffice automation · Microsoft Office automation · Python runtime · Java runtime ·
在线转换服务 · 其它 PDF/DOCX library。**probe 不通过 ⇒ STOP**，不得偷偷改路线。

### 54.3 probe 计划（本轮执行）

```text
工作区（与产品源码隔离，ignored）: build\m12c-c1b-dependency-probe\
PDFium probe : 冻结 exact upstream revision；说明 producer/consumer toolchain 分离；
               MinGW GCC 13.1 最小 consumer 仅调用 **public C API**
               （FPDF_InitLibraryWithConfig / FPDF_LoadDocument / FPDF_GetPageCount /
                FPDF_LoadPage / FPDFText_LoadPage / FPDFText_CountChars / FPDFText_GetText
                + 对应 close/destroy）；
               三个确定性输入：有 text layer 的小 PDF / 无 text layer 的 blank-image PDF /
               corrupt 非 PDF。
libzip probe : exact pinned release；MinGW GCC 13.1 可重复 build；最小依赖配置
               （DOCX 只需 ZIP container + deflate ⇒ 不引入 OpenSSL/GnuTLS/bzip2/lzma/zstd）；
               zlib 来源与 linkage 必须明确记录。
DOCX probe   : Qt 6.11.1 + MinGW GCC 13.1 构建的 libzip + QXmlStreamReader consumer；
               仅 **两个** parser（libzip + QXmlStreamReader，不引入第二个 XML parser）；
               main document part 按 **package relationship（officeDocument target）发现**，
               **不**把 `word/document.xml` 当无条件真理；
               四个确定性输入：合法 DOCX（ASCII + 中文 + ≥2 paragraph + table cell）/
               中文路径 DOCX / corrupt ZIP（伪 DOCX）/ 缺 main document relationship。
```

**结果分类**：三者分别判 `PASS / FAIL / BLOCKED`；**只有三者全 PASS** 才是
`C1b DEPENDENCY PROBE = PASS`，否则 `= HOLD`（然后 STOP，不自动替换路线）。

**范围纪律**：本轮**不做**任何 C1b 产品实现（不改 `src/` / `tests/` / `CMakeLists.txt` / QML，
不加 importer / extractor / UI / Candidate / AI extraction）；不跑 C1b product acceptance、
不建 canonical package、不建 Human visual candidate；不把任何 probe artifact 放进 tracked tree。

### 54.4 状态

```text
M12-C C1b = STARTED / DEPENDENCY BUILD PROBE / IMPLEMENTATION NOT STARTED
（probe 结果见 §55）
verified LKGC = 8409c271cca966e9f9ab0ad0ba2d6470c0a66e10（不变）
```

---

## 55. M12-C C1b — DEPENDENCY BUILD PROBE（2026-09-27 · **current state = DEPENDENCY PROBE HOLD**）

### 55.0 总判定

```text
PDFium probe         = BLOCKED
libzip build probe   = PASS
DOCX Qt consumer     = PASS
⇒ 总结果：C1b DEPENDENCY PROBE = **HOLD**（§14：任一 FAIL/BLOCKED ⇒ HOLD）
⇒ 依 Human 冻结条款「probe 不通过则 STOP，不得自行替换依赖路线」：**STOP，未替换任何路线**
```

probe 工作区 = `build\m12c-c1b-dependency-probe\`（**ignored，未进 tracked tree**；
`git ls-files build` 仍为 0）。**未**改 `src/` / `tests/` / `CMakeLists.txt` / QML；
**未**做任何产品实现；**未**跑产品验收 / canonical package / visual candidate。

### 55.1 PDFium probe = BLOCKED（最高风险项，如实记录）

**上游事实独立复核**（重新核对，不沿用先验）：

```text
① 官方构建方式：gclient config --unmanaged https://pdfium.googlesource.com/pdfium.git
   → gclient sync → GN 生成构建文件 → Ninja 执行构建；GN 与 Ninja **均来自 depot_tools**。
② 编译器政策（官方 README 逐字）：“PDFium aims to be compliant with the Chromium policy.
   Currently this means Clang. Former MSVC users should consider using clang-cl if needed.”
   + “No MSVC patches will be taken.”
③ Windows：与 Chromium 相同工具链；`set DEPOT_TOOLS_WIN_TOOLCHAIN=0` 使用本地 VS 工具链；
   官方文档**未提供任何预编译二进制下载**（只有源码构建路径）。
```

**本机实测（producer 侧环境）**：

```text
gn / gn.exe / clang / clang-cl / depot_tools / gclient / autoninja ⇒ **全部 NOT FOUND**
（PATH 与常见位置 C:\src、C:\depot_tools、D:\depot_tools 均无；D:\QT\Tools 仅 CMake_64/Ninja/
 QtCreator/QtDesignStudio/mingw1310_64/mocwrapper/sdktool/LicenseService）
官方主机可达性：https://pdfium.googlesource.com/pdfium/ ⇒ **HTTP 503**（本网络不可达，
无法 checkout / 固定 upstream revision）；chromium/src 镜像可达（200）但只提供 Chromium
同步后的 third_party/pdfium 子树，且构建仍需完整 Chromium 工具链。
```

**consumer 侧结论**：`ModbusLens-side minimal consumer` **无法建立**——不存在可供 MinGW GCC 13.1
link/load 的 **PDFium artifact**；路线冻结要求 **exact pin + 离线 runtime + 无 runtime download**，
而本环境既无官方 artifact，也无法在合理范围内从源码产出（需 depot_tools + GN + Clang + 多 GB
`gclient sync` + 长时构建；对照：本机单次 CMake 配置约 11–15 分钟）。

**仅完成 API 面侦察（非 consumer probe，不得当作 ABI 证据）**：自官方仓库的第三方镜像取得
`public/fpdfview.h`（61 534 B）与 `public/fpdf_text.h`（29 438 B），冻结路线所需的 11 个
**public C API** 符号全部存在：`FPDF_InitLibraryWithConfig` · `FPDF_LoadDocument` ·
`FPDF_GetPageCount` · `FPDF_LoadPage` · `FPDFText_LoadPage` · `FPDFText_CountChars` ·
`FPDFText_GetText` · `FPDF_ClosePage` · `FPDFText_ClosePage` · `FPDF_CloseDocument` ·
`FPDF_DestroyLibrary`。

**未做**：text-layer positive / no-text / corrupt-input 三个运行时用例**未执行**（无 artifact ⇒ 无 consumer）。
**未**用 Poppler / QtPdf / 其它 wrapper / 在线 API 顶替；第三方预编译分发（非官方）仅被记录为
**待 Human 决策的候选**，**本轮未采用**。

```text
PDFium pin       = **UNRESOLVED**（官方主机 503 ⇒ 无法固定 exact upstream commit）
producer toolchain = NOT AVAILABLE（Chromium tooling 缺失）
consumer toolchain = Qt 6.11.1 MinGW GCC 13.1 Windows x64（存在，但无 artifact 可消费）
link/load mechanism = **N/A（未建立）**
runtime dependency audit = **N/A**
```

### 55.2 libzip build probe = PASS

```text
exact version  = **libzip 1.11.4**（= libzip.org/download/ 当前发布版）
official source= https://libzip.org/download/libzip-1.11.4.tar.gz
archive        = libzip-1.11.4.tar.gz · 1 301 153 B
SHA-256        = **82e9f2f2421f9d7c2466bbc3173cd09595a88ea37db0d559a9d0a2dc60dc722e**
license        = BSD 3-clause 风格（Copyright (C) 1999-2020 Dieter Baron and Thomas Klausner）
构建工具链      = D:/QT/Tools/CMake_64/bin/cmake（Ninja 生成器）+ MinGW GCC 13.1
                 （D:/QT/Tools/mingw1310_64/bin/{gcc,g++}.exe）· Windows x64 · Release
zlib 身份       = **1.2.13**（MinGW-w64 sysroot 自带：`x86_64-w64-mingw32/{include/zlib.h,lib/libz.a}`）
                 —— 显式以 `-DZLIB_INCLUDE_DIR=` / `-DZLIB_LIBRARY=` 固定；**不代表产品已正式采用 zlib**
最小依赖配置    = BUILD_SHARED_LIBS=OFF · ENABLE_OPENSSL=OFF · ENABLE_GNUTLS=OFF ·
                 ENABLE_MBEDTLS=OFF · ENABLE_WINDOWS_CRYPTO=OFF · ENABLE_COMMONCRYPTO=OFF ·
                 ENABLE_BZIP2=OFF · ENABLE_LZMA=OFF · ENABLE_ZSTD=OFF ·
                 BUILD_TOOLS/REGRESS/EXAMPLES/DOC=OFF
构建结果        = configure rc=0（`Found ZLIB: …libz.a (found suitable version "1.2.13")`）·
                 build rc=0（**0 error**）· install rc=0 ⇒ `out/libzip-install/lib/libzip.a`（静态，260 990 B）
备注            = 首次 configure 因 CMake `FindZLIB` 未在标准前缀找到 sysroot zlib 而失败
                 （`Could NOT find ZLIB`），改用显式 pin 后通过；DOCX 常规 **deflate** 真实可读
                 （见 §55.3 的 `ZIP_DEFLATED` fixture 全部成功解出）。
```

### 55.3 DOCX Qt consumer probe = PASS

```text
consumer 工具链 = **Qt 6.11.1**（D:/QT/6.11.1/mingw_64）+ **MinGW GCC 13.1** · Windows x64
link/load 机制  = **静态** libzip.a + **静态** zlib(1.2.13) + **动态** Qt6Core.dll
probe 源码      = build\m12c-c1b-dependency-probe\docx\probe_docx.cpp（probe-only，非产品代码）
probe 可执行    = out\probe_docx.exe · SHA-256
                  **3f8aa4c1f9396b78748263cef127bb45a9c741910c3a55b969d7c9354c4f34a4d**（311 457 B）
runtime imports = ADVAPI32.dll · KERNEL32.dll · **Qt6Core.dll** · libgcc_s_seh-1.dll ·
                  libstdc++-6.dll · msvcrt.dll
                  ⇒ **无** Python / Java / LibreOffice / Office / Node（逐项实测 absent）
main part 发现  = 依 **OOXML package relationship**：解析 `_rels/.rels` 中
                  `Type=…/officeDocument` 的 `Target` ⇒ `word/document.xml`
                  （**未**把 `word/document.xml` 硬编码为真理）
XML parser      = **仅** Qt Core `QXmlStreamReader`（未引入第二个 XML parser）
path/编码机制    = Qt `QFile` 读为 bytes → **libzip memory source**
                  （`zip_source_buffer_create` + `zip_open_from_source`），
                  即 libzip **不**自行解析非 ASCII 路径；仍属冻结路线内的实现方式
```

**确定性用例结果（全部 exit code 实测）**：

| # | 输入 | 期望 | 实测 |
| --- | --- | --- | --- |
| A | `docx/fixtures/good.docx`（ASCII + 中文 + 4 段 + 1 表格 2 单元格，ZIP_DEFLATED） | 成功、顺序稳定 | **exit=0**；`libzip_version=1.11.4`、`main_document_part=word/document.xml`；文本含 `Frequency register 1000`、`First paragraph ASCII only.`、`第二段：中文说明，PV 地址 1000，单位 Hz。`、`spaced   text kept`（`xml:space` 空白保留）、`Cell A 表格` / `Cell B` |
| B | 同内容但路径含中文 `docx/fixtures/中文目录/验收文档.docx` | 可读 | **exit=0**，输出与 A 逐字一致 |
| C | `docx/fixtures/corrupt.docx`（伪 ZIP） | 明确失败、不 crash | **exit=3**：`not a usable ZIP container: Possibly truncated or corrupted zip archive` |
| D | `docx/fixtures/missing_main_rel.docx`（无 officeDocument relationship） | 明确失败、不得猜测 | **exit=4**：`no officeDocument relationship in _rels/.rels` |
| E | `docx/fixtures/dangling_main_part.docx`（relationship 指向的 part 不存在） | 明确失败 | **exit=5**：`main document part missing` |
| F | `docx/fixtures/malformed_main.docx`（main XML 未闭合） | 明确失败 | **exit=6**：`main document XML malformed` |

**text semantics = PROPOSED / NOT YET PRODUCT-FROZEN**（本轮只记录候选口径，**不得**伪写成 Human 已冻结）：
段落结束 → `\n`；表格 row 结束 → `\n`；表格 cell 结束 → `\t`；`w:tab` → `\t`；
`w:br`/`w:cr` → `\n`；run 之间**无**分隔符；`w:t` 文本原样（`xml:space="preserve"` 空白保留）。

### 55.4 安全 / 资源观察（reconnaissance；本轮**不**冻结任何阈值）

未来 implementation 必须处理（本轮仅记录，具体数值一律标为
**implementation safety parameter to be frozen/recorded later**）：
ZIP bomb / 解压膨胀 · 超大 XML · package name 类路径穿越 · 加密 ZIP/DOCX ·
malformed XML · **PDF 密码/加密** · 超大 PDF / page count / text count ·
PDFium 失败与崩溃边界（尤其因 §55.1 未建立 consumer，该边界**完全未测**）。

### 55.5 未决项

```text
① PDFium exact pin = UNRESOLVED（官方主机 503 + producer toolchain 缺失）
② PDFium artifact 来源与 producer toolchain policy = 需 Human 决策
   （候选仅在案：在具备 depot_tools+GN+Clang 的环境自行构建并 pin exact commit；
     第三方预编译分发为非官方来源，本轮未采用，需 Human 明确批准）
③ DOCX text semantics = PROPOSED，需 Human freeze
④ 资源上限数值 = 未冻结
```

### 55.6 状态

```text
M12-C C1b = STARTED / DEPENDENCY PROBE HOLD / IMPLEMENTATION BLOCKED
（PDFium 路线 BLOCKED；libzip ✅ + DOCX consumer ✅）
M12-C C1a = COMPLETE / HUMAN ACCEPTED（不变）
verified LKGC = 8409c271cca966e9f9ab0ad0ba2d6470c0a66e10（不变）
未改 src/ tests/ CMakeLists.txt QML；未做产品实现；未 push / 未 tag / 未 amend
```

---

## 56. M12-C C1b — PDFIUM HOLD UNLOCK AUTHORIZATION（2026-09-27 · probe 结果见 §57）

### 56.1 Human 授权原文（逐字归档）

> “授权：M12-C C1b PDFium HOLD 解锁 probe
> 使用第三方 binary distributor
> bblanchon/pdfium-binaries，
>
> 冻结 candidate release 为：
>
> PDFium 156.0.8066.0
> /
> chromium/8066
>
> 对应官方 PDFium chromium/8066
> 当前 commit：
>
> fc46361ce75055cd549cb938fae5d6a3fe3a1a05
>
> 目标 artifact：
>
> Windows x64
> pdfium-win-x64.tgz。
>
> 必须核验并归档：
>
> artifact SHA-256
> GitHub release digest/attestation
> VERSION
> public headers
> architecture
> license/notices
> runtime dependencies。
>
> 仅允许：
>
> PDFium public C API。
>
> OCR 继续 deferred。
>
> runtime 不得下载。
>
> 先测试：
>
> MinGW GCC 13.1 consumer
> 对发行包 import library
> 的直接消费。
>
> 若不可靠：
>
> 允许在不更换
> PDFium DLL / version
> 的前提下测试
> 本地动态符号加载。
>
> 两种机制均失败：
>
> 继续 HOLD。
>
> 不得自行更换 PDF engine。
>
> 该授权：
>
> 仅用于解除 dependency probe，
>
> 不授权：
>
> C1b 产品实现。”

### 56.2 冻结项（HUMAN-FROZEN）与边界

```text
PDF engine       = **未改变**，仍是 **PDFium public C API**（OCR 继续 DEFERRED）
新增授权         = 允许采用 **bblanchon/pdfium-binaries** 作为 **frozen binary distributor**
                   进行 dependency probe
PDFium version   = **156.0.8066.0**
distributor tag  = **chromium/8066**
upstream identity= **chromium/8066**
Human-frozen upstream commit = **fc46361ce75055cd549cb938fae5d6a3fe3a1a05**
artifact         = **Windows x64 · pdfium-win-x64.tgz**
runtime          = **不得下载**（下载只允许发生在 probe preparation）
```

**distributor 定性（必须逐字保留）**：`bblanchon/pdfium-binaries` 是**第三方 binary
distributor**，**不是** Google、**不是** Foxit、**不是** PDFium 官方 binary channel。
distributor 仓库侧出现的任何 commit（例如 release 页面上的 `f2e9a1c` 一类）
**必须标注为 bblanchon/pdfium-binaries 仓库 commit**，**绝不得**写成 upstream PDFium commit；
upstream frozen identity 单独是 `fc46361ce75055cd549cb938fae5d6a3fe3a1a05`。

**本授权 ≠ 产品 dependency adoption；≠ C1b implementation authorization；≠ LKGC advancement。**
probe 完成前的状态：

```text
M12-C C1b = STARTED / DEPENDENCY PROBE HOLD /
            PDFIUM BINARY ABI PROBE AUTHORIZED / IMPLEMENTATION BLOCKED
verified LKGC = 8409c271cca966e9f9ab0ad0ba2d6470c0a66e10（不变）
```

### 56.3 本轮范围

只做：授权归档 + 第三方 PDFium binary supply-chain verification + MinGW GCC 13.1
ABI / public C API consumer probe + probe evidence archive。**不做** C1b 产品实现；
不改 `src/` / `tests/` / `CMakeLists.txt` / QML / `AGENTS.md` / `CODEBUDDY.md`；
不建 behavior commit / Human candidate / canonical package；不推进 verified LKGC；
probe 工作区 = ignored `build\m12c-c1b-dependency-probe\pdfium-binary-8066\`（**不删除**上一轮
libzip / DOCX / PDF reconnaissance 证据）。
上一轮 `libzip = PASS` 与 `DOCX consumer = PASS` **不重跑**，只复核无 repo truth 冲突。

---

## 57. M12-C C1b — PDFIUM BINARY ABI PROBE（2026-09-27 · **current state = DEPENDENCY PROBE PASS**）

### 57.0 总判定

```text
PDFium binary ABI probe = **PASS**
结合已归档的 libzip build PASS + DOCX Qt consumer PASS（本轮未重跑）
⇒ M12-C C1b = STARTED / DEPENDENCY PROBE PASS / IMPLEMENTATION NOT STARTED
（DEPENDENCY PROBE PASS ≠ implementation authorized）
```

### 57.1 Release identity / pin（frozen）

```text
distributor repo      = bblanchon/pdfium-binaries（**第三方 binary distributor**）
release tag           = **chromium/8066**
release title         = **PDFium 156.0.8066.0**
release id            = 392954061 · draft=false · prerelease=false
published_at          = 2026-09-21T12:48:25Z
distributor release target commit = **f2e9a1c45bb17b85b540abf1af30146ef65416ac**
                        （**= bblanchon/pdfium-binaries 仓库 commit，非 upstream PDFium commit**）
upstream PDFium frozen commit     = **fc46361ce75055cd549cb938fae5d6a3fe3a1a05**
release URL           = https://github.com/bblanchon/pdfium-binaries/releases/tag/chromium/8066
```

**upstream identity 本轮在线重新验证 = YES（成功）**：

```text
GET https://pdfium.googlesource.com/pdfium/+/fc46361ce75055cd549cb938fae5d6a3fe3a1a05?format=JSON → HTTP 200
commit    = fc46361ce75055cd549cb938fae5d6a3fe3a1a05
tree      = d8a0dc23abd3e92213c7b613a5b8c6304f6dea2e
parents   = [221fc40e5868f4c136179d40a10e8fc75deb6889]
author    = Tom Sepez <tsepez@google.com> · Thu Sep 17 18:46:22 2026 -0700
committer = pdfium-scoped@luci-project-accounts.iam.gserviceaccount.com（同时间戳）
subject   = "Remove core/fxge dependency from core/fxcrt/css"
⇒ 与 Human-frozen identity **一致，无矛盾**（§5 的 503 保护条款本轮未被触发）
```

### 57.2 Artifact / digest / attestation

```text
artifact          = **pdfium-win-x64.tgz**（**非 V8** 版本；同 release 另有 pdfium-v8-win-x64.tgz 12 728 306 B 以示区分）
asset id          = **579031518**
asset size        = **3 823 498 B**（下载实测 bytes=3823498）
local SHA-256     = **739a57d597d864297909cc40a2411eba728490c76a0fa25e3ea299c7f6b07020**
GitHub API digest = sha256:739a57d597d864297909cc40a2411eba728490c76a0fa25e3ea299c7f6b07020
digest match      = **EXACT MATCH**
attestation asset = pdfium-attestation.json · id 579031525 · 18 411 B
attestation SHA-256 = **78e8446878a31978058268d3931d15ff1766d72145bea5128c16f953b3b94d82**（= API digest，EXACT MATCH）
attestation 结构   = DSSE envelope（in-toto Statement v1 · predicateType **SLSA provenance v1**）
subject digest    = 解码 payload 后含 `pdfium-win-x64.tgz` → sha256 **739a57d5…**（= 本地 artifact）⇒ **subject digest match**
```

**密码学验证（§7）**：

```text
verifier          = **gh 2.101.0 (2026-09-15)**（pinned；portable zip 仅置于 probe workspace，
                    未全局安装、未改系统 PATH、未进 Git）
gh zip 校验        = 本地 bc6c814367b193cd8e713611d61e36013c0ef843b8f516458fe3eda039192794
                    == GitHub API digest == 官方 checksums 文件 → 三方一致
command           = gh attestation verify downloads/pdfium-win-x64.tgz \
                      --bundle downloads/pdfium-attestation.json --repo bblanchon/pdfium-binaries
exit code         = **0（PASS）**
signer identity   = SAN = https://github.com/bblanchon/pdfium-binaries/.github/workflows/build-all.yml@refs/heads/master
issuer            = https://token.actions.githubusercontent.com
workflow          = .github/workflows/build-all.yml · ref refs/heads/master · repo bblanchon/pdfium-binaries
provenance 生成步  = workflow 内 `uses: actions/attest-build-provenance@v3`，`subject-path: pdfium-*.tgz`
                    （随后 mv 为 pdfium-attestation.json 并随 release 上传）
```

### 57.3 Artifact 内容 / VERSION / 构建配置 / 架构

```text
安全解包          = 50 entries；**0** 个绝对路径/`..` 条目；**无** symlink
VERSION           = MAJOR=156 · MINOR=0 · BUILD=8066 · PATCH=0 ⇒ 与 **156.0.8066.0** 一致
args.gn           = is_component_build=false · is_debug=false · **pdf_enable_v8=false** ·
                    pdf_enable_xfa=false · pdf_is_standalone=true · pdf_use_partition_alloc=false ·
                    **target_cpu="x64"** · **target_os="win"** · treat_warnings_as_errors=false
                    ⇒ 确认为 **Windows x64 非 V8** artifact
pdfium.dll        = 7 380 992 B · SHA-256 **d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b**
                    格式 **pei-x86-64**（architecture i386:x86-64）
exports           = 471 total · **434 个 `FPDF*`**；12 个必检符号全部存在：
                    FPDF_InitLibraryWithConfig · FPDF_LoadDocument · FPDF_GetPageCount ·
                    FPDF_LoadPage · FPDFText_LoadPage · FPDFText_CountChars · FPDFText_GetText ·
                    FPDFText_ClosePage · FPDF_ClosePage · FPDF_CloseDocument ·
                    FPDF_DestroyLibrary · FPDF_GetLastError
pdfium.dll.lib    = 114 736 B · SHA-256 **a5b07aacd8a4c0fe4900152ca58332d512018c35619f555c0fbc68ed9213d5db**
                    MSVC 格式 COFF import library（binutils 可读：`file format pe-x86-64`）
```

### 57.4 Public headers 与 upstream 逐字节对照（§11）

```text
header inventory  = include/ 下 24 个 public `fpdf*.h` + include/cpp/ 2 个 + **fpdfview.h.orig**
fpdfview.h        = SHA-256 **7fe0ff046ec5a2eaf1642c21076c39737e04d0819bc7265d7c7c33a6b216cb5f**
fpdf_text.h       = SHA-256 **4682f8553dc1d29bbee95cdcddefebbd96522c5be654807906240f8571593726**
fpdfview.h.orig   = SHA-256 **c001b029e3b026e1704a30bca8335211b3bad374676c9946113696dc6dcbc305**

upstream public/fpdfview.h @ fc46361c  vs artifact include/fpdfview.h.orig → **BYTE-IDENTICAL**
upstream public/fpdf_text.h @ fc46361c vs artifact include/fpdf_text.h     → **BYTE-IDENTICAL**
upstream public/fpdfview.h @ fc46361c  vs artifact include/fpdfview.h      → DIFFERS（**仅 6 行**）
```

**差异的成因已由 distributor 可验证 build process 证明为「预期生成步骤」**（§11 要求）：

```text
① artifact 自身携带 `fpdfview.h.orig`（= upstream 原文，逐字节一致）⇒ 是**有意 patch** 而非意外改动；
② distributor `.github/workflows/patch.yml` → `steps/03-patch.sh`：shared 构建会依次 apply
   `patches/shared_library.patch` 与 `patches/public_headers.patch`；
③ `patches/shared_library.patch` 恰触及两个文件：`BUILD.gn` 与 **`public/fpdfview.h`**，
   其 hunk 正是删除 `#if defined(COMPONENT_BUILD) … #else #define FPDF_EXPORT #endif`
   这段 6 行 guard；
④ 语义后果：upstream 在**非 COMPONENT_BUILD** 消费侧把 `FPDF_EXPORT` 定为**空**，
   而 patch 后 WIN32 消费侧得到 **`__declspec(dllimport)`** ⇒ 对 DLL 消费方是**必要且正确**的修正；
   （`patches/public_headers.patch` 只重写 `public/cpp/*.h` 的 include 路径，与本差异无关。）
⇒ 结论：**差异为预期生成步骤，§11 的 STOP 条件不成立**；本轮仅使用 public headers，未调用任何 internal API。
```

### 57.5 Runtime dependency audit（§13，以 PE import table 为证据）

```text
pdfium.dll imports = **ADVAPI32.dll · GDI32.dll · KERNEL32.dll · USER32.dll**（全部 Windows system DLL）
⇒ 无 V8 · 无 Node · 无 Python · 无 Java · 无 Office · 无 LibreOffice · 无 Qt ·
  无 WinHTTP · 无 WinINet · 无 WS2_32（逐项以 import table 实测，非「目录里没有」推断）
未向 System32 / Qt 安装目录 / MinGW 安装目录复制任何 DLL；全部 runtime staging 仅在 probe workspace
```

### 57.6 MinGW direct import-lib consumer（§14–§15）—— **DIRECT LINK = PASS**

```text
consumer compiler = **g++ (x86_64-posix-seh-rev1, MinGW-Builds) 13.1.0** · target **x86_64-w64-mingw32**
                    （与 Qt 6.11.1 kit 同一 MinGW；**未**使用 MSVC / clang-cl）
headers           = artifact include/（public headers only）
import library    = artifact lib/pdfium.dll.lib（**直接使用**，未自行重新生成 import lib）
compile command   = g++ -std=c++20 -O2 -o out/probe_pdf_direct.exe out/probe_pdf.cpp \
                      artifact/lib/pdfium.dll.lib -Iartifact/include -Wall -Wextra
compile           = **rc=0 · 0 error · 0 undefined reference · 0 warning**
probe exe         = 85 979 B · SHA-256 **78f0536088e7bca58b17d52fe587be5bc13b51fca72a6523db66343a9f25bb83**
architecture      = **pei-x86-64**
PE imports        = KERNEL32.dll · libgcc_s_seh-1.dll · libstdc++-6.dll · msvcrt.dll · **pdfium.dll**
runtime dir       = runtime/direct/ 仅含：probe exe + **frozen pdfium.dll**（SHA-256 d42c452a…，
                    与 artifact 一致）+ 3 个 MinGW runtime DLL（libgcc_s_seh-1 / libstdc++-6 / libwinpthread-1）
PATH 歧义检查      = `where.exe pdfium.dll` → **未找到**（PATH 上不存在另一份 pdfium.dll）
                    ⇒ loader 实际使用 runtime 目录内的 frozen DLL（配合显式 dll_path 打印）
repeatability     = 三个用例各连续运行 **2 次**，输出与 exit code 完全一致
```

### 57.7 功能用例（§16–§17）

| 用例 | fixture（自生成，无版权） | 期望 | 实测（2 次一致） |
| --- | --- | --- | --- |
| text-layer | `samples/text-layer.pdf` 607 B（无压缩流，`BT…Tj…ET`） | page≥1 · CountChars>0 · GetText 含预期文本 | **exit=0** · `page_count=1` · `total_chars=32` · 文本 = **`ModbusLens C1b PDFium text layer`**（完全匹配） |
| no-text | `samples/no-text.pdf` 577 B（合法页、无文本算子） | 不 OCR、不猜测、chars==0 | **exit=0** · `page_count=1` · `total_chars=**0**` · 文本为空 |
| corrupt | `samples/corrupt.pdf` 317 B（合法 PDF 截断至 55%） | 明确失败、不 crash | **exit=2** · `load_document=FAILED` · **`FPDF_GetLastError=3`**（FPDF_ERR_FILE）· 无 crash |

**dynamic fallback = NOT RUN**（Human 授权：仅当 direct 不可靠时才允许；direct 已 PASS，故未执行）。

### 57.8 许可证 / notice 分层审计（§12，事实 inventory，非法律意见）

```text
A. distributor repo license = MIT 风格（Copyright 2014-2025 Benoit Blanchon）· 1 068 B
B. upstream PDFium LICENSE @ fc46361c = BSD-3-clause 风格（Copyright 2014 The PDFium Authors）· 12 896 B
   （仓库内以 `// ` 注释前缀书写；去掉前缀后与 C 层 pdfium.txt **语义一致**）
C. artifact 内实际携带 = `LICENSE`（= distributor MIT 文本 + 追加一句
   “This package also includes third-party software. See the licenses/ directory …”，
   与 repo LICENSE 的字节差来自行尾/该句）+ `licenses/` **14 个**第三方 notice：
   abseil · agg23 · fast_float · freetype · icu · lcms · libjpeg_turbo(.ijg/.md) ·
   libopenjpeg · libpng · llvm-libc · **pdfium** · simdutf · zlib
D. NuGet：distributor README 记录其发布 `bblanchon.PDFium` / `bblanchon.PDFiumV8`；
   NuGet 元数据端点可达，但本轮**未提取**其 license declaration（tgz 路线不需要）
```

**PACKAGING LICENSE MATERIAL 工程判断**：artifact 自带 LICENSE + 14 项第三方 notice，
**明显材料齐备**（`PACKAGING LICENSE MATERIAL = PRESENT`）；但具体 redistribution 义务
（逐文件归属、NOTICE 并入方式）**必须在未来 packaging 轮重新核对**，本轮不主张
canonical distribution ready。

### 57.9 probe 位置 / 未做事项

```text
probe 工作区 = build\m12c-c1b-dependency-probe\pdfium-binary-8066\
              （downloads/ artifact/ out/ runtime/ samples/ logs/；**ignored & untracked**）
上一轮证据   = build\m12c-c1b-dependency-probe\ 下的 libzip / DOCX / PDF reconnaissance **未删除、未改动**
probe artifacts tracked = **NO**（`git ls-files build` = 0）
runtime download        = **NO**（下载仅发生在 probe preparation；运行期只用本地 bytes/DLL）
product implementation  = **NO**（未改 src/ tests/ CMakeLists.txt QML / AGENTS.md / CODEBUDDY.md）
```

### 57.10 状态

```text
M12-C C1b = STARTED / DEPENDENCY PROBE PASS / IMPLEMENTATION NOT STARTED
（PDFium binary ABI probe PASS · libzip PASS · DOCX consumer PASS）
M12-C C1a = COMPLETE / HUMAN ACCEPTED（不变）
verified LKGC = 8409c271cca966e9f9ab0ad0ba2d6470c0a66e10（不变）
未 push / 未 tag / 未 amend；未创建 behavior commit / Human candidate / canonical package
```

---

## 58. M12-C C1b — PRODUCT IMPLEMENTATION START + SEMANTICS / DEPENDENCY FREEZE（2026-09-27）

> 本节是 **docs-only freeze**：先冻结 Human 批准的产品语义与依赖物料策略，**然后**才允许写产品代码。
> 实现证据见 §59。

### 58.1 Human 授权原文（逐字归档）

> “授权：M12-C C1b product implementation START。
> 冻结 PDF 文本语义为使用 PDFium public C API
> 提取已有 text layer，
> UTF-16 正确转换为 Unicode/QString，
> 至少覆盖 ASCII、中文、无 text layer 与 corrupt PDF；
>
> 冻结 DOCX v1 plain-text 语义为
> paragraph 分隔 `\n`、
> table cell 分隔 `\t`、
> table row 分隔 `\n`、
> `w:tab`→`\t`、
> `w:br`/`w:cr`→`\n`、
> run 间无额外分隔、
> `w:t` 保持文本且尊重 `xml:space`；
>
> PDFium/libzip 依赖必须使用
> 已 probe 通过的 exact pins，
> 并通过可重复、离线可用的
> 本地 dependency materialization 进入构建，
> 禁止 runtime 下载与隐式 latest。
>
> 实现前先把这些语义与依赖物料策略归档，
> 然后才允许写产品代码。”

**授权边界**：这是 **C1b PRODUCT IMPLEMENTATION START**，**不是** C1b COMPLETE、**不是** Human acceptance、
**不是** LKGC advancement、**不是** C2/C3 授权、**不是** M12-D 授权。
verified LKGC 保持 `8409c271cca966e9f9ab0ad0ba2d6470c0a66e10`。

### 58.2 PDF 文本语义（**HUMAN-APPROVED PRODUCT DECISION**）

```text
PDF 文本 = 使用 **PDFium public C API** 提取**已有 text layer**
转换规则 = PDFium 文本按 public API contract 作为 **UTF-16** data 处理，
           正确转换为 **Unicode / QString**（禁止 Latin-1 / local 8-bit /
           UTF-8 reinterpret_cast / 逐 byte 构造）
最低覆盖 = ASCII · 中文 · 无 text layer · corrupt PDF
OCR      = **DEFERRED**（不得 OCR、不得猜测文本）
```

### 58.3 DOCX v1 plain-text 语义（**HUMAN-APPROVED PRODUCT DECISION**）

```text
paragraph 分隔     = `\n`
table cell 分隔    = `\t`
table row 分隔     = `\n`
`w:tab`            = `\t`
`w:br` / `w:cr`    = `\n`
run 之间           = **无**额外分隔符
`w:t`              = 保持文本内容，并**尊重 `xml:space`**
story 边界（v1）    = 仅 **Main Document Part** 的 main document story
                      （header / footer / comments / footnotes / endnotes /
                       text box extended stories / embedded object / image OCR /
                       tracked-change semantics / field evaluation = 不得自动扩展）
```

解析必须 **namespace-aware**（按 namespace URI + local name 判断，不得假定 prefix 为 `w` / `r`）。
不得 trim / 折叠空白 / 自动美化 / Markdown·HTML render。

### 58.4 依赖物料策略（**IMPLEMENTATION STRATEGY — under authorized offline-materialization
requirement；下列具体路径 / 脚本名 / manifest 形状属工程实现选择，不是 Human 原文**）

```text
§2 审计结论：repo **无**既有 canonical third-party materialization 机制
             （无 third_party 目录、无 lock manifest、无 FetchContent/ExternalProject；
              仅 Qt 的 find_package）；既有先例 = `scripts/make_package.py` 与
              `scripts/test_make_package_freshness.py`（Python stdlib 脚本惯例）；
              `docs/ENVIRONMENT.md §6` 红线：项目代码不得依赖任何机器的绝对路径。
             ⇒ 与下述策略**无实质冲突**，不构成「第二套 dependency truth」。

(1) tracked lock manifest —— repo 内轻量 JSON，只记 pin / hash / 上游标识，
    **不记任何本机绝对路径**（遵守 ENVIRONMENT §6）。
(2) local distfiles only —— 产品 CMake **禁止联网**：不得 FetchContent URL /
    ExternalProject URL / latest / main / master / HEAD；只接受本地已存在的 exact-pinned distfiles。
(3) explicit offline materializer —— repo-tracked Python(stdlib) 工具，**默认 NO NETWORK**；
    输入 = local distfiles 目录；输出 = ignored `build/deps/m12c-c1b/`；
    **先 SHA-256 验证 → 再解包/构建**；任何 hash mismatch 明确 FAIL。
(4) build consumption —— CMake 只消费已 materialized 的 local root；缺失 / 版本不符 /
    pin 不符 / manifest 不符 ⇒ configure **明确失败**并给出 preparation command，绝不自动联网修复。
(5) reproducibility boundary —— 「offline reproducible」定义为：exact distfiles 已在本地后，
    materialization + configure/build/test **全程无需网络**；**不**声称 clean clone 在完全没有
    dependency bytes 的机器上凭空离线构建。
(6) no vendored blob —— **不**把 pdfium tgz / pdfium.dll / pdfium.dll.lib / libzip tarball /
    libzip.a 提交进 Git。
```

### 58.5 本轮范围（HARD SCOPE）

```text
IN : A 离线依赖物料化 · B PDFium extraction foundation · C DOCX extraction foundation ·
     D deterministic unit tests · E CMake 本地依赖消费
OUT: ManualImportController 接线 · ManualStore import 行为修改 · FileDialog filter 修改 ·
     DeviceProfilePage.qml / Main.qml 修改 · Human candidate · visual review ·
     AI extraction · Candidate model · C2/C3 · M12-D
```

**PDF「无 text layer」在本 slice 不得被映射成最终 UI/import policy**（reject 导入 vs 允许保存但文本为空
= 未冻结的产品 UX 决策）：extractor 只返回明确的 `NoExtractableText` 状态。

### 58.6 未冻结项（本轮发现，待 Human）

```text
PDF page-boundary / user-visible page concatenation 语义：Human 本轮授权只覆盖
「existing text layer extraction」与 UTF-16 correctness，**未**新增 PDF page separator 规则；
T027 既有 contract 亦未冻结。⇒ 本 slice **不**自行把任何 page separator 升格为产品契约，
result 保留 **per-page** 结构；若未来必须选定 user-visible 拼接语义，另行提请 Human 裁定。
```

## 59. M12-C C1b — Extraction Foundation Final Regression + Behavior Commit（2026-09-28，behavior `fb2170e`）

> 本节归档 §58 冻结范围的实现与验证证据。执行环境 = WorkBuddy Agent 沙箱（canonical Qt 6.11.1 MinGW
> toolchain，`D:/QT/Tools/CMake_64/bin/cmake.exe` + `D:/QT/6.11.1/mingw_64` + `D:/QT/Tools/mingw1310_64`）。
> 本节为 **behavior-bearing slice 收口档案**：final regression + commit only，不含 implementation 变更。

### 59.1 起点与 resync（TRUSTED RESYNC 实测）

- starting HEAD = `e01cb1a505845188fb1d5d4e7ab0061461f54759`（与 Human 提供的期望值一致）；
  verified LKGC = `8409c271cca966e9f9ab0ad0ba2d6470c0a66e10`（PROJECT_STATUS 顶部批注链确认）。
- WIP reconciliation（`git status --porcelain=v1 --untracked-files=all`）：modified = `.gitignore`(+4)、
  `CMakeLists.txt`(+168)；untracked = materializer/DEP 脚本 2 + extractor 源 6 + `test_manual_extraction.cpp`
  + `third_party/m12c-c1b/dependencies.lock.json`，另有 2 个**外来**未跟踪文件 `_ctx.py`、`_dump.py`
  （非本 slice 产物，**未 stage、未修改**，留待 Human 处置）。`git diff --check` = PASS；cached 为空。
- Human shell 已确认：materializer mutation markers = 0；libzip restored SHA-256 =
  `82e9f2f2…`；normal materializer exit 0；NC-C1B-1..4 已完成。

### 59.2 依赖物料化交付（explicit offline materializer）

- **tracked lock** = `third_party/m12c-c1b/dependencies.lock.json`（schemaVersion 1，无任何机器绝对路径）：
  PDFium **156.0.8066.0**（distributor `bblanchon/pdfium-binaries` tag `chromium/8066`，distributor commit
  `f2e9a1c`，upstream `fc46361c…`；artifact `pdfium-win-x64.tgz` `739a57d5…`；dll `d42c452a…`；implib
  `a5b07aac…`；VERSION/args.gn 断言）· libzip **1.11.4**（`82e9f2f2…`，Release/Ninja/static，TLS/LZMA 等全 OFF）
  · zlib **1.2.13**（MinGW sysroot，`zlib.h` `a980a0d1…` + `libz.a` `a0d1c861…`）。
- **materializer** = `scripts/materialize_c1b_deps.py`（Python stdlib，**no network code path at all**）：
  先 SHA-256 验证所有 distfile 与 zlib 输入 → 再解包（防 path traversal、stamp 幂等）→ 校验 pdfium
  VERSION/args.gn/dll/implib/notices → 本地构建 libzip → 写 git-ignored materialized manifest
  `build/deps/m12c-c1b/manifest/materialized.json`（产物路径 repo-relative 记录）。
- **explicit zlib-input（审计确认）**：`locate_zlib()` **deliberately 不做** ambient PATH-first
  discovery（`shutil.which(gcc) → sysroot` 路径已移除并注释说明）；缺 `--zlib-include/--zlib-library`
  即 `fail(5)`。实际 zlib.h/libz.a 本轮重新 hash 实测 = **双双 MATCH lock**。
- **DEP results**：`scripts/test_materialize_c1b_deps.py`（DEP01–DEP12，故意篡改副本必须被拒）经
  CTest 注册后 = **12/12 PASS**（Release 与 Debug 两侧 full regression 内均 PASS；pytest 式自报
  `12/12 dependency materializer tests passed`）。CTest 注册把 lock/distfiles/zlib 路径**显式**转发，
  独立于 CTest cwd；**PATH 永不探测**（interpreter 亦为显式 `MODBUSLENS_PYTHON_EXECUTABLE`）。
- **runtime download = NONE；implicit latest = NONE；vendored blob = NONE**（distfiles 目录 git-ignored，
  tracked 的只有 lock json）。

### 59.3 CMake regeneration evidence（按 Human 措辞要求归档）

- 本轮 CMakeLists regenerate 期间，Debug 侧手动 configure **一次失败，failure boundary observed
  at/after `find_program`；exact mechanism UNKNOWN**。
- **eliminating ambient PATH discovery via explicit cache configuration**（显式传入
  `MODBUSLENS_PYTHON_EXECUTABLE`，配合 cache 中既有的显式 `CMAKE_CXX_COMPILER` /
  `CMAKE_MAKE_PROGRAM`）**restored manual configure and ninja regeneration** —— 重跑 configure
  exit 0（两次：37.5 s / 首次 112.1 s），后续 ninja regenerate 正常。
- **不声称** CMake executed Python stub，不声称其它机制；该现象仅记为环境侧观察，产品 CMake
  的设计应对（所有 tool/interpreter 路径显式化、PATH 永不探测）即为既定架构。

### 59.4 Extraction foundation（PDF / DOCX）

- **PDF**（`ManualPdfTextExtractor`，PDFium public C API）：仅提取已有 text layer，UTF-16→QString
  正确转换；**PDF 中文提取**（`pdf02_chineseTextLayer` + `pdf03_mixedAsciiAndChinese`）PASS；
  无 text layer = 显式 `NoExtractableText`（**绝不 OCR、绝不猜测**）；corrupt = 明确失败不崩溃；
  **page-boundary remains per-page**（result 保留 per-page 结构，user-visible 拼接语义仍未冻结）。
- **DOCX**（`ManualDocxTextExtractor`，libzip + Qt Core `QXmlStreamReader`，namespace-aware）：
  §58.3 七条 plain-text 语义逐条落地（d04–d11）；main document story 边界（d21 header/footer 不泄漏）；
  relationship 发现 + 不安全 target 拒绝（d12/d20）；corrupt/缺 rels/缺 main part/malformed XML
  全部明确失败不崩溃（d14–d19）；`xml:space` 保留（d11）；空 story = `NoExtractableText`（d24）。
- `ManualTextExtraction` 为统一入口/结果模型（status tokens 稳定性有专门测试）。
- **extractors 不链入 app**：CMakeLists 注释明确 —— 链入会改变已接受的 C1a 部署（exe 隐式依赖
  pdfium.dll）；wiring 属 import workflow slice。**ManualImportController PDF/DOCX wiring = 0 改动。**

### 59.5 Targeted tests + 负向对照

- `manual_extraction`（QTest）：**pdf01–pdf11（11）+ d01–d24（24）+ statusTokens** 全 PASS
  （Release 与 Debug full regression 内各复验一次）。
- `c1b_dependency_materializer`：DEP01–DEP12 **12/12 PASS**（同上）。
- **NC-C1B-1..4**：前序 implementation 轮已完成并精确还原（Human 确认）；本轮为
  final-regression-only，**按纪律未重新执行任何 mutation**。

### 59.6 Final regression（全部本轮实测，真实 exit code）

| Gate | 结果 |
| --- | --- |
| CTest inventory（`ctest -N`，Release） | **Total Tests = 55**（53→55；#27 manual_extraction · #28 c1b_dependency_materializer · #53/#55 manual_import check(+windows) · #50–#52 profile/register/active） |
| Release build | `ninja: no work to do` / exit 0 |
| **Release full CTest** | **55/55 PASS / 0 failed / exit 0**（546.57 s；materializer 用时 6m35s 属预期：每次全量 verify+build libzip） |
| Debug configure（显式 cache configuration） | exit 0（§59.3 证据） |
| Debug build | exit 0（46/46；新增 C1b 源全部编译链接；**未复现** ar/ranlib 历史错误） |
| **Debug full CTest** | **55/55 PASS / 0 failed / exit 0**（563.06 s） |
| **Windows-QPA protected gates（显式 `QT_QPA_PLATFORM=windows`，非 offscreen）** | **5/5 exit 0**：qml_manual_import_check / qml_write_foundation_check / qml_profile_editor_check / qml_register_map_check / qml_active_profile_check |
| 诊断统计（QPA 5 日志） | ReferenceError = 0 · TypeError = 0 · Unable to assign = 0 · String.arg Invalid = 0（日志中的 "invalid FC refused" / "invalidated(...)" 为功能 dump 文本，非诊断） |

### 59.7 Mutation-residue audit（含一处诚实记录）

- `scripts/materialize_c1b_deps.py` / `ManualPdfTextExtractor.cpp` / `ManualDocxTextExtractor.cpp`：
  `MUTATION` 与 `False and actual != expected_sha` = **0 命中**；代码行为正确。
- **cosmetic residue（如实入档，未修改）**：`materialize_c1b_deps.py` hash-mismatch 分支残留一行
  NC 还原后的**注释**（其文字与实际行为矛盾，描述的是 mutation 期间的临时行为并带
  "Reverted immediately after." 流程注记）。语句 `if actual != expected_sha: fail(4, …)` 本身
  正确且经 DEP 篡改用例验证。清理属 docs/注释级变更，**待 Human 裁定**，本轮按「不得修改」纪律不动。
- **〔2026-09-28 追加批注 · Human 已授权清理（commit `2287169510e4c2436f6bef539624c1c0a22d0ff2`，
  「M12: remove stale C1b mutation comment」）〕**上述 cosmetic residue 已清除：仅删除该 1 行注释
  （0 行新增，无任何 executable statement 变化），上方原文**保留不删**。清理后
  `c1b_dependency_materializer` targeted CTest 单独复跑 **PASS**（Test #28，568.78 sec；
  注册实测 #27 manual_extraction / #28 c1b_dependency_materializer，Total Tests: 55）。
  纯注释清理、无行为变化；**fb2170e 的历史验收结论仍然有效，不改写**。

### 59.8 Protected diff audit（PASS）

- tracked diff vs HEAD^ 仅 `.gitignore`（distfiles ignore rule）与 `CMakeLists.txt`（C1b 依赖/测试
  集成块；extractors **不链入 app**）—— 均属允许范围。
- 新增文件全部在允许清单（lock/materializer/DEP/extractors×6/manual_extraction tests）。
- **authority UNCHANGED**：TransactionAnalysis · RegisterDecode · DeviceProfile · ProfileStore ·
  ProfileController · ActiveProfile · M12-B semantic overlay · C1a TXT/Markdown · QML ·
  ManualImportController PDF/DOCX wiring —— 零 diff。

### 59.9 Final materializer audit（PASS，全部本轮实测）

tracked lock 无机器绝对路径 ✅ · zlib 不经 ambient PATH 推导（显式 input，缺失即 fail）✅ ·
zlib.h/libz.a 实测 hash 对 lock 双 MATCH ✅ · runtime download = NONE ✅ · implicit latest = NONE ✅ ·
PDFium exact pin 不变（156.0.8066.0 / `fc46361c…` / `739a57d5…` / `d42c452a…` / `a5b07aac…`）✅ ·
libzip exact pin 不变（1.11.4 / distfile 实测 `82e9f2f2…` = frozen）✅。

### 59.10 Behavior commit 与状态

- **behavior commit = `fb2170efc26fd7fd884ec4b950e8607385223fc0`**
  「M12: add deterministic PDF and DOCX extraction foundation」
  parent = `e01cb1a505845188fb1d5d4e7ab0061461f54759`；12 files / +2328 −0；NO AMEND / NO push / NO tag；
  显式逐文件 stage（禁 `git add .`/`-A`；`_ctx.py`、`_dump.py`、docs、build/ 均未 stage）。
- 状态：**C1b extraction foundation = IMPLEMENTED / AUTOMATED PASS**；**C1b import workflow/UI =
  NOT STARTED**；C2 / C3 / M12-D = NOT STARTED；verified LKGC = `8409c27…`（**不推进**，
  `fb2170e` 为 future candidate，需 Human 单独授权）；canonical package = NOT CREATED；
  REAL MODBUS HARDWARE = NOT VERIFIED。
- Knowledge ownership：通过本轮真实事情理解了 —— ① IMPORTED target + configure-time hash 断言
  如何让「materialized 产物被偷换」在 configure 阶段即 FATAL；② extractor 与 app 的链接隔离
  （测试目标独享依赖，app 二进制不变 ⇒ 已接受部署零风险）；③ CTest 注册显式转发解释器与
  zlib 路径 = 把「PATH 探测」这一类环境漂移从机制上排除。

---

## 60. M12-C C1b SECOND SLICE — IMPORT WORKFLOW + UI（2026-09-28 · CONTRACT FROZEN）

### 60.0 Human decision（逐字归档）

> “四组都同意。”

标为 **HUMAN-APPROVED M12-C C1b SECOND-SLICE CONTRACT DECISION**（不是 pre-existing canonical）。
授权边界：C1b second-slice contract freeze；**不是** LKGC advancement、**不是** C2/C3/M12-D 授权。
verified LKGC = `8409c271cca966e9f9ab0ad0ba2d6470c0a66e10`（不推进）。

### 60.1 GROUP 1 — IMPORT ENTRY / ROUTING（HUMAN-APPROVED）

1. 复用现有 Manual Import 区域与**同一个** FileDialog；**不新增**独立 PDF/DOCX import button。
2. v1 selectable types：TXT · MD · Markdown · PDF · DOCX。
3. extension 只负责 **extractor routing**；actual content 仍由对应 extractor 严格校验
   （`.pdf`/`.docx` 扩展名正确 ≠ 内容自动可信）。
4. unsupported extension = **explicit reject**。
5. **C1a TXT / Markdown import path 与其已验收语义 ZERO CHANGE**。

### 60.2 GROUP 2 — IMPORT SUCCESS / FAILURE SEMANTICS（HUMAN-APPROVED）

```text
正常（PDF 有 text layer / DOCX main story）
  ⇒ managed-copy success → deterministic extraction success → ManualDocument record
    → contentHash text cache → UI preview available。

PDF no text layer
  ⇒ IMPORT ITSELF = SUCCESS：保留 managed copy + ManualDocument record，
    extraction state = no_extractable_text；UI 明确显示「未发现可提取文本层」；
    禁止 OCR / 猜测文本 / 伪造 empty-success preview。

corrupt / fake / malformed / encrypted / password-protected / resource-limit exceeded
  ⇒ WHOLE IMPORT = ATOMIC FAILURE：不得留下 partial ManualDocument / partial managed
    copy / partial text cache / half-registered state；失败 UI 必须有稳定 product-level message。

original source 在 import 成功后即使被移动/删除：managed copy 仍是可用来源
（C1a Hybrid 契约：managed-copy + original-path provenance，不变）。
```

### 60.3 GROUP 3 — DUPLICATE / CACHE IDENTITY（HUMAN-APPROVED）

```text
document identity != contentHash；contentHash = cache identity。
不按 contentHash 去重 documents：同 bytes 从不同 original paths 导入 ⇒ 允许多个
ManualDocument records，各自保留 originalPath provenance。
extracted-text cache 按 contentHash 复用（同 bytes ⇒ 同 cache payload）。
不自动 merge documents；不自动修改既有 record 的 originalPath；
不把「相同内容」解释成「同一文档」。
```

### 60.4 GROUP 4 — UI PREVIEW / SLICE BOUNDARY（HUMAN-APPROVED）

```text
TXT / Markdown：保持现有 plain-text preview。
DOCX：显示已冻结的 deterministic main-story plain text。
PDF：按 page 展示；允许 presentation header（如「第 1 页」「第 2 页」），
     但 page header 只属于 presentation —— 不得写入 text cache、不得改变 extractor
     per-page truth。
PDF no-text：显示明确 no-text state，不得用空白 preview 假装普通 extraction success。
error UI：稳定 Human-facing message；PDFium/libzip raw error 只作 diagnostics，
     不得直接成为长期 product contract。
```

**本 slice scope ONLY**：PDF/DOCX import routing · managed-copy · extraction · contentHash
cache reuse · document-list integration · detail integration · preview integration。
**OUT OF SCOPE**：Manual Delete UI · AI Candidate · Accept/Edit/Reject · OCR · C2 · C3 ·
Manual Q&A/M12-D · PDF/DOCX editing · remote resource loading · runtime download ·
external content execution · automatic AI extraction。

### 60.5 Schema amendment（随本决策的工程必需，属 HUMAN-APPROVED CONTRACT 的直接实现）

```text
ManualDocumentType 增加 Pdf / Docx（extension 矩阵扩展）；
ManualImportError 增加 MalformedContent / EncryptedOrPasswordProtected；
ManualDocument 增加 extractionStateToken（"extracted" | "no_extractable_text"），
随 metadata JSON（schemaVersion 仍为 1 —— pre-release amendment，与 C1a readFunctionCode 同例）；
PDF 的 text cache = text/<contentHash>.json（pages 数组，per-page truth）；
TXT/MD/DOCX 的 text cache 保持 text/<contentHash>.txt。
C1a TXT/Markdown 行为 ZERO CHANGE。
```

---

## 61. M12-C C1b SECOND SLICE — AUTOMATED ACCEPTANCE ARCHIVE（2026-09-28 · Session A–D）

### 61.0 提交链

```text
docs-freeze  = 6abed334bb0c3305d18b8035a9e3e2150790df1f「M12: freeze C1b import workflow semantics」
behavior     = c36eb181917ab2aba9435870cd376c19b8b4bc60「M12: integrate PDF and DOCX manual import workflow」
               （parent 6abed33；11 files：CMakeLists.txt · core ManualDocument.{h,cpp} · main.cpp ·
                ManualImportController.{h,cpp} · ManualStore.{h,cpp} · DeviceProfilePage.qml ·
                tests/test_manual_import.cpp · tests/test_manual_import_pdf_docx.cpp；+1645 −211）
```

### 61.1 Session A — implementation + targeted（WorkBuddy）

```text
routing/atomicity/cache/state 实现（ManualStore TXT/MD 路径逐字节不变；PDF cache=<hash>.json；
extractionStateToken 序列化/加载；hasManagedArtifacts 类型感知）；Controller previewStateToken；
QML FileDialog filters + no-text 显式文案；CMake 依赖 preamble 上移 + app 链接 pdfium/libzip/zlib +
pdfium.dll staging；gate 扩展 stage3(.exe unsupported) + 3b/3c/3d/3e/3f。
targeted gates 10/10 PASS：manual_import 22/22 · manual_extraction 38/38 · manual_import_pdf_docx 32/32 ·
c1b_dependency_materializer 12/12 · qml_manual_import_check[_windows] · profile_editor · register_map ·
active_profile · write_foundation_windows。Total Tests: 55 → 56。
```

### 61.2 Session B — negative controls（每项 REAL RED → precise reverse patch → GREEN）

```text
NC-C1B2-1  no-text 必须保持 import success：
           mutation = NoExtractableText 分支改 MalformedContent return；
           RED = i06/i07 'result.ok()' returned FALSE（30 passed / 2 failed）→ reverse → GREEN。
NC-C1B2-2  contentHash 不得成为 document identity：
           mutation = hash 相同即返回既有 record（去重）；
           RED = i23/i24 'a.document.documentId != b.document.documentId' returned FALSE
                + i27 'a.document.originalPath != b.document.originalPath' returned FALSE
                （29 passed / 3 failed）→ reverse → GREEN。
NC-C1B2-3  failed-import durable-state non-pollution invariant（isolated managed-root override 内）：
           mutation = Malformed 分支 return 前写入 manuals/source/mutated-partial.bin；
           RED = i08 'Compared lists have different sizes. Actual 20 / Expected 19'
                （31 passed / 1 failed）→ reverse → GREEN；mutated-partial 残留 0。
           证据边界：本 NC 证明测试能够识别 partial durable state（invariant 可被违反即被检测）；
           不主张穷举证明所有内部写入顺序绝对原子。
NC-C1B2-4  page header 不得进 cache truth：
           mutation = PDF cache pagesArray 拼入 "第 N 页\n"；
           RED = i03 Actual "第 1 页\nModbusLens PDF import"（i04/i05/i07/i21 同红，27 passed / 5 failed）
           → reverse → GREEN；presentationIndex 残留 0。
targeted re-green：10/10 PASS（同 Session A protected surface），exit 0。
```

### 61.3 Session C — WorkBuddy full regression + Debug HOLD

```text
Release：configure 0 / build 0 / full CTest **56/56 passed / 0 failed / exit 0**（788.24 s）。
Windows-QPA protected gates 6/6：#51 profile_editor (2.34s) · #52 register_map (2.27s) ·
#53 active_profile (2.41s) · #54 manual_import (4.21s) · #55 write_foundation_windows (12.16s) ·
#56 manual_import_windows (6.29s，含 PDF/DOCX stages 与 1000×700 geometry)。
QML diagnostics：ReferenceError=0 · TypeError=0 · Unable to assign=0 · String.arg Invalid=0。
Release staged pdfium.dll SHA-256 = d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b
（= frozen value）。protected diff = PASS；dependency/runtime = PASS（exact pins 不变，无 runtime download）。
WorkBuddy-context Debug：configure 0；build 两次失败于
`ranlib.exe: could not create temporary file whilst writing archive: no more archived files`
（目标 libmodbuslens_core.a，step 5/205，**compile errors = 0**）；当时按协议 HOLD。
```

### 61.4 Session D — Human external Debug evidence ratification

```text
Human 在外部普通 Windows PowerShell 对当前 WIP（build/debug 既有 build tree）执行 Debug revalidation：
DEBUG CONFIGURE EXIT = 0 · CTest inventory = Total Tests: 56 ·
DEBUG BUILD EXIT = 0（[158/159] Building CXX object CMakeFiles/modbuslens.dir/src/main.cpp.obj →
[159/159] Linking CXX executable modbuslens.exe，**非 clean rebuild，是既有 target 的完成**）·
DEBUG FULL CTEST = 56/56 PASS / 0 failed / Total Test time = 125.04 sec / CTEST EXIT = 0
（manual_import_pdf_docx · c1b_dependency_materializer · qml_profile_editor_check ·
qml_register_map_check · qml_active_profile_check · qml_manual_import_check ·
qml_write_foundation_check_windows · qml_manual_import_check_windows 全 PASS）。
措辞边界：**WorkBuddy-context ranlib failure was NOT reproduced externally; external Debug build
completed and full CTest passed 56/56. Root cause remains UNKNOWN.**
（禁止归档为 sandbox-caused / binutils bug / TEMP / resource / root-cause-fixed。）
external shell 的 git rev-parse / status / diff --check 因 PATH 缺 Git 未执行成功，
**不作为 repo-state evidence**；repo truth 由 Session D resync 提供（HEAD/WIP 与 Session C 完全一致，
WIP 未变化 ⇒ Session A–C 的自动化 evidence 继续适用）。
```

### 61.5 最终状态

```text
M12-C C1b second-slice import workflow/UI =
  IMPLEMENTED / AUTOMATED PASS · HUMAN VISUAL ACCEPTANCE PENDING
（AUTOMATED ACCEPTANCE COMPLETE；未写 HUMAN ACCEPTED / COMPLETE / LKGC advanced）
C1b extraction foundation = IMPLEMENTED / AUTOMATED PASS（fb2170e）
C1a = COMPLETE / HUMAN ACCEPTED · M12-B = COMPLETE · C2/C3/M12-D = NOT STARTED
canonical package = NOT CREATED（future package gate：必须携带 pdfium.dll）
REAL MODBUS HARDWARE = NOT VERIFIED · verified LKGC = 8409c271cca966e9f9ab0ad0ba2d6470c0a66e10（不推进）
Release Human visual candidate：
  exe = build/release/modbuslens.exe（6 043 056 B，SHA-256
        026d3589a1f4c5bb3071cdba9aa53109c276b441e058c9ecd03992e5fec69792）
  runtime = build/release/pdfium.dll（7 380 992 B，SHA-256
        d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b）
  candidate product code anchor = c36eb181917ab2aba9435870cd376c19b8b4bc60
HUMAN FIXTURE SET = NOT PREPARED（fixtures 由测试 in-code 生成；Human visual 可自选
  普通 PDF / 中文 PDF / 无文本层 PDF / DOCX）
```

---

## 62. M12 WINDOWS DEPLOYMENT CORRECTION — SELF-CONTAINED CANDIDATE（2026-09-29 · Session E–I）

### 62.1 Human startup failure #1（libstdc++ entry point）

```text
Observed：Human 在 Explorer 双击 build/release/modbuslens.exe，进入 UI 前失败：
  modbuslens.exe - 无法找到入口
  _ZNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE15_M_replace_coldEPcyPKcy
Evidence：
  exe PE imports = libstdc++-6.dll + libgcc_s_seh-1.dll（libstdc++ 自身再 import libwinpthread-1.dll）
  original raw candidate 完全没有 local compiler runtime（依赖开发者 PATH）✓ VERIFIED
  canonical MinGW（D:\QT\Tools\mingw1310_64）libstdc++ 导出该符号 ✓
  D:\mingw64\bin\libstdc++-6.dll（用户 PATH 上的另一套 MinGW）存在、hash≠canonical、
  且**不导出** _M_replace_cold ✓（静态证据）
Root cause boundary：
  missing local compiler runtime = VERIFIED DEFECT
  exact Human-loaded foreign provider = NOT VERIFIED（live 0xc0000139 复现未完成，
  沙箱进程启动机制限制）
```
回收 Session E 的过度表述：不得写 “Explorer definitely loaded D:\mingw64\bin\libstdc++-6.dll”。

### 62.2 Human startup failure #2（Qt platform plugin）

```text
Observed（Human 截图，AUTHORITATIVE）：
  This application failed to start because no Qt platform plugin could be initialized.
  Available platform plugins are: windows.
Diagnostics：CONTROL（sanitized）与 REGISTRY-ENV（Machine;User 真实 PATH，Qt/QML vars 全 unset）
  均 exit 0 + SMOKE IDENTITY PASS ⇒ EXPLORER FAILURE #2 NOT REPRODUCED AUTOMATICALLY
ROOT CAUSE（historical）= UNKNOWN
已 VERIFIED 的独立缺陷：混合/非自包含 deployment architecture（deploy-era plugins/platforms
  与 current-kit platforms 并存；historical deploy tree 曾作为 runtime source；
  Debug 未获得与 Release 相同的 runtime tree）
```
0xc0000602 只记录为 STATUS_FAIL_FAST_EXCEPTION，不作 environment/deployment/product 归因。

### 62.3 永久架构修正：raw build output != deployable candidate

```text
Release Human candidate = build/release/candidate/ModbusLens/
Debug candidate        = build/debug/candidate/ModbusLens/
生成：add_custom_target(modbuslens_candidate) → cmake/modbuslens_generate_candidate.cmake
  · 清空 candidate root 后从零生成（stale sentinel 证明：创建 → 生成 → 消失）
  · source：current app target / frozen PDFium / active MinGW toolchain（CMAKE_CXX_COMPILER）
    / active Qt kit（Qt6_DIR）/ current QML module build output
  · 内容：exe、pdfium.dll、compiler runtime 3、Qt runtime 与 QuickControls2/QuickDialogs2 闭包、
    platforms/{qwindows,qoffscreen}.dll、Scheme A plugin families
    (imageformats/iconengines/tls/networkinformation)、current-kit qml 闭包、
    app 自身 ModbusLens QML 模块、qt.conf（[Paths] Prefix=. Qml2Imports=qml，单一 plugin scheme）
  · manifest：candidate-manifest.json（1713 entries：path/category/SHA-256）
  · Release/Debug 同一 configuration-aware 逻辑，独立生成，互不复制
  · historical deploy-tree runtime source = NO（plus 行引用 0）
门禁：tests/deployment_startup_check.cmake
  · 只消费 candidate（raw exe 引用 0）
  · launch 前做 manifest + compiler-runtime/Qt6Core hash 校验
  · sanitized PATH（candidate + System32 + Windows，清除 QT_PLUGIN_PATH /
    QT_QPA_PLATFORM_PLUGIN_PATH / QML*_IMPORT_PATH）
  · 要求 exit 0 + SMOKE IDENTITY PASS + QT_DEBUG_PLUGINS 证明 qwindows 来自
    candidate/platforms/qwindows.dll（实测：Qt 安装路径 0 命中）
  · NO_GENERATE=1 = negative-control isolated-candidate mode
```

### 62.4 自动化验收

```text
Session H（targeted）：Release targeted 8/8、Debug targeted 3/3、candidate gate Release/Debug
  PASS；NC-A（缺 libstdc++-6.dll）REAL RED；NC-B（缺 platforms/qwindows.dll）REAL RED；
  production gate 复绿；manual_import_pdf_docx 一次瞬态失败复跑 PASS（同一 commit 前状态）。
Session I（full）：
  Release full CTest  = 57/57 PASS，0 failed，exit 0（684.09 s），含 deployment_startup_check
                        #29 Passed 125.40 s；QML diagnostics 全 0
  Debug   full CTest  = 57/57 PASS，0 failed，exit 0（714.81 s），含 deployment_startup_check
                        Passed 130.62 s；0xc0000602 计数 0
  Debug build：无 ranlib archive failure（无需 bounded retry）
Release candidate hashes：
  modbuslens.exe b5b1f4a8a6e7dce4c5acb34b2358a84e3a2ed1143b34604c0ce591d22d2139dc
  pdfium.dll     d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b（frozen）
  libstdc++-6.dll 8013488c5528bad7966ca07f3ea2e7a9b743cacb258fe76b46a326f821cc83b0（active toolchain）
  platforms/qwindows.dll 804739071bba619b4a4312b5bb29a142545a64c4c80218e5b2e6672ad33ee8ac（active kit）
Debug candidate：1713 manifest entries，exe b418c71a…（独立，非 Release 复制）
```

### 62.5 提交与状态

```text
behavior commit = a7bbbad60908ae47f6cd0b01b747353ca4c3369e
  「M12: make Windows deployment candidate self-contained」（parent 0c7535e，
  3 files / +336：CMakeLists.txt、cmake/modbuslens_generate_candidate.cmake、
  tests/deployment_startup_check.cmake）
docs/governance commit = 本节后追加（AGENTS.md durable rule + docs 归档）

WINDOWS SELF-CONTAINED CANDIDATE = AUTOMATED FULL REGRESSION PASS
HUMAN VISUAL RE-ACCEPTANCE = PENDING（只能使用 candidate 路径，禁止使用 raw exe）
verified LKGC = 8409c271cca966e9f9ab0ad0ba2d6470c0a66e10（不推进）
C2 = NOT STARTED · canonical package = NOT CREATED · REAL MODBUS HARDWARE = NOT VERIFIED
```

---

## 63. M12-C C1b — MANUAL IMPORT VERTICAL LAYOUT REMEDIATION（2026-09-29 · Session J）

### 63.1 Human evidence（准确边界，不得美化）

```text
Human 从唯一允许的 Release candidate 启动：
  E:\desktop\ModbusLens\build\release\candidate\ModbusLens\modbuslens.exe
  · Windows self-contained candidate launch        = PASS
  · PDF 导入 / 抽取 / 预览（功能）                 = PASS
  · DOCX 导入 / 抽取 / 预览（功能）                = PASS
  · **Manual Import 垂直布局可用性                 = HOLD（Human 明确提出重大 UI 问题）**

Human 观察（约 1280x937 截图，文字描述归档）：
  上方三栏（设备档案列表 / 档案信息 / 寄存器映射）在内容大量为空时仍占据约 500+ px；
  下方 Manual Import 只剩约 200 px；PDF/DOCX 正文 preview 一次只能看到约 2–3 行；
  阅读长说明书「只能全靠滚轮翻页」。
Human intent：把说明书显示区域整体往上/放大，上方三栏不需要这么大的位置。

因此本轮准确状态：
  C1b functional re-acceptance   = PASS
  Windows deployment Human check = PASS
  C1b Manual Import layout usability = HOLD
  C1b Human visual re-acceptance = HOLD（不得写成 “C1b Human visual PASS”）
这**不是** Windows deployment regression（deployment 已 PASS，未重新调查
_M_replace_cold / platform plugin / PATH / windeployqt / DLL closure）。
```

### 63.2 RCA

```text
CURRENT GEOMETRY OWNER = DeviceProfilePage.qml → 根 ColumnLayout（anchors.fill）
ROOT CAUSE =
  · profileWorkspaceRow（三栏 RowLayout）持有 Layout.fillHeight: true
    ⇒ 吞掉 header/actions/catalog-issues 之后的**全部**剩余高度；
  · manualImportHost 只有 Layout.preferredHeight: manualImportCardItem.implicitHeight
    （content-driven）⇒ 长文档阅读区只拿到「内容最小值」，不参与剩余空间分配。
  修复前实测（1000x700）：workspace 659 / row 400（61%）/ host 161（24%）/ preview 36px。
MINIMAL CORRECTION POINT =
  ① profileWorkspaceRow：fillHeight false + 有界比例高度 min(workspace*0.42, 460)
     + minimumHeight 180（三栏各自已有 Flickable 内部滚动，不损失功能）；
  ② manualImportHost：Layout.fillHeight: true（释放出的高度归长文档阅读区，随窗口增长），
     保留 content-driven preferredHeight 作为 stretch basis/floor。
  未改信息架构 / 未新增 tab / 未改字体主题 / 未改 minimum window（Main.qml 仍 1000x700）
  / 未为单一 1280x937 截图特调。
```

### 63.3 修复后几何契约（真实 runtime 实测）

```text
1000x700（门禁尺寸，--qml-manual-import-check stage 6/7，有文档状态）：
  profileWorkspaceRow / workspace share = 0.420（277 px，原 400）
  manualImportHost  / workspace share   = 0.439（289 px，原 161）
  manualPreview viewport                = 157 px（原 36 ⇒ 约 11 行 caption 文本，增长 4.4×）
Human 尺寸（~1280x937）下同一比例规则 ⇒ 阅读区约获内容区一半高度、preview 数百 px。
未出现 clipping/overlap：上方三栏内部 Flickable 滚动，preview 保持 Flickable 垂直滚动。
```

### 63.4 回归门禁与 mutation proof

```text
新增（绑定真实 runtime geometry，位于 runManualImportCheck stage 6/7）：
  row share <= 0.50 · host share >= 0.35 · manualPreview.height >= 120
Mutation proof（precise reverse patch，未 commit）：
  恢复缺陷态 ⇒ REAL RED：
    "Manual Import area gets too little vertical space: share=0.255 (host=168 workspace=659)"
    "preview viewport is too small: h=36 (minimum 120)"
  精确逆向恢复本修复 ⇒ GREEN（share 0.420 / 0.439，preview 157）
```

### 63.5 自动化验收与提交

```text
targeted（9/9 PASS）：qml_smoke · qml_geometry_check · qml_manual_import_check ·
  qml_manual_import_check_windows · qml_profile_editor_check · qml_register_map_check ·
  qml_active_profile_check · qml_profile_semantic_check · qml_profile_semantic_demo
Release full CTest：57/57 PASS，0 failed，exit 0（775.12 s），含 deployment_startup_check
  #29（174.23 s）；QML diagnostics：ReferenceError/TypeError/Unable to assign/
  String.arg Invalid 全 0
Release candidate：由 canonical target modbuslens_candidate 重新生成（1713 manifest entries），
  deployment gate PASS（142.38 s）；exe edb362c0…、pdfium d42c452a…（frozen）、
  qwindows 80473907…
SEMANTIC ZERO DIFF：Profile schema/persistence/editor/register map 与 Manual Import 的
  routing / content validation / document identity / contentHash / cache /
  no_extractable_text / PDF per-page / DOCX deterministic extraction / FileDialog 全部未变；
  无 OCR / AI extraction / Delete / C2 / C3 / M12-D；未改 deployment architecture。

behavior commit = 19f9738c980e0a8a31b557c346fb50a4af711cab
  「M12: give manual preview usable vertical space」（parent 21636a4，2 files / +67 −1）
docs commit = 「M12: archive manual preview layout remediation」（本节 + 状态文档）

MANUAL IMPORT LAYOUT REMEDIATION = AUTOMATED PASS
C1b HUMAN VISUAL RE-ACCEPTANCE = PENDING（Human 尚未重新视觉验收新布局）
verified LKGC = 8409c271cca966e9f9ab0ad0ba2d6470c0a66e10（不推进）
C2 = NOT STARTED · canonical package = NOT CREATED · REAL MODBUS HARDWARE = NOT VERIFIED
```

---

## 64. M12-C C1b — HUMAN VISUAL ACCEPTANCE / C1b CLOSURE（2026-09-29 · Session K）

### 64.1 Human 验收原文（逐项归档，不得改写）

```text
Human 就垂直布局整改后的新版界面完成新一轮视觉验收，逐项报告：

  新版上方三栏空间            = PASS
  Manual Import 阅读空间      = PASS
  窗口缩放布局                = PASS
  PDF / DOCX 新版视觉复查     = PASS
  异常                        = 无

⇒ 权威 Human 结论：**M12-C C1b Human visual re-acceptance = PASS**

人工验收入口 = 唯一允许的 Release candidate
  E:\desktop\ModbusLens\build\release\candidate\ModbusLens\modbuslens.exe
（该候选在 §63 布局整改后由 canonical target `modbuslens_candidate` 重新生成，
  exe `edb362c0…`；raw exe 仍然禁止作为人工验收入口）
```

### 64.2 历史链保留（HOLD → remediation → re-review PASS）

```text
Session J（§63）：
  C1b functional re-acceptance        = PASS
  Windows deployment Human check      = PASS
  Manual Import 垂直布局可用性        = HOLD（Human 明确提出重大 UI 问题）
  C1b Human visual re-acceptance      = HOLD（当时明令禁止写成 PASS）
Session J 修复：behavior 19f9738c980e0a8a31b557c346fb50a4af711cab
  （有界比例三栏 min(workspace*0.42, 460) + minimumHeight 180，
   Manual Import 获得 Layout.fillHeight: true）
Session K（本节）：Human 重新视觉验收 ⇒ PASS

纪律：本节**只解除** §63 的「当前状态」字段（HOLD → PASS）。
§63 的 HOLD 证据、Human 原始观察与 RCA **一字不改、一段不删**，
不做任何追溯性重写（retroactive rewrite）= FORBIDDEN。
```

### 64.3 证据维度分立（four-axis split，禁止合并成一句「全部 PASS」）

```text
Implementation        = COMPLETE
  · fb2170e  C1b extraction foundation（PDFium 156.0.8066.0 + libzip 1.11.4，确定性离线）
  · c36eb18  C1b import workflow / UI（PDF / DOCX / TXT / MD）
  · a7bbbad  Windows self-contained candidate architecture
  · 19f9738  Manual Import preview 垂直空间整改
Automated regression  = PASS
  · Release full CTest 57/57 PASS，0 failed，exit 0（775.12 s），含
    deployment_startup_check（174.23 s）
  · QML diagnostics（ReferenceError / TypeError / Unable to assign / String.arg Invalid）全 0
  · targeted 9/9 PASS；几何断言 mutation proof REAL RED → GREEN
Human functional      = PASS
  · Windows self-contained candidate 启动 = PASS
  · PDF 导入 / 抽取 / 预览 = PASS
  · DOCX 导入 / 抽取 / 预览 = PASS
Human visual          = PASS（本轮 Session K 新证据，四项全 PASS，异常 = 无）

⇒ 文档自此可写：**M12-C C1b = COMPLETE / HUMAN ACCEPTED**
```

### 64.4 仍然未变 / 禁止顺带升级的维度

```text
failure #1（Explorer 双击 _M_replace_cold entry point）：
  missing local compiler runtime            = VERIFIED DEFECT
  exact foreign provider                    = NOT VERIFIED（保持，不事后归因）
failure #2（Qt platform plugin initialization failure）：
  Human evidence                            = VERIFIED
  historical exact root cause               = UNKNOWN（未复现即不归因，保持 UNKNOWN）
0xc0000602：仅记 STATUS_FAIL_FAST_EXCEPTION，不作归因

verified LKGC = 8409c271cca966e9f9ab0ad0ba2d6470c0a66e10
  · 不推进；本 commit 为 docs-only，**docs-only commit 永不作 LKGC**
M12-C C2      = NOT STARTED
canonical package = NOT CREATED（未执行 packaging；未来 package gate 必须携带 pdfium.dll）
REAL MODBUS HARDWARE = NOT VERIFIED
无 push / 无 tag / 无 amend
```

### 64.5 本轮动作边界（Session K = docs-only）

```text
未 build · 未 test · 未重新生成 candidate · 未 packaging · 未改产品代码 ·
未推进 LKGC · 未开始 C2。
唯一动作 = 把 Human 视觉验收 PASS 归档进 canonical docs：
  docs/tasks/T027 §64（本节）
  docs/PROJECT_STATUS.md（顶部追加批注块）
  docs/BACKLOG.md（大事记表新增一行）
  docs/devlog/2026-09-29.md（Session K 章节 + 状态字段更新）
并提交一个 docs-only commit。
附带（诚实披露）：BACKLOG.md 大事记表修正一处**结构缺陷** —— 上一轮插入把
「2026-09-29 M12 Windows Deployment」那条多行记录拦腰截断（其续行落在
「Manual Import 整改」整条记录之后）；本次把被截断的续行块移回原记录尾部，
**内容零改动**，仅恢复表格结构。
```

---

## 65. M12-C C1b — VERIFIED LKGC ADVANCE（2026-09-29 · Session L · docs-only）

### 65.0 Human 授权（SESSION L §0）

```text
Human 明确授权（SESSION L 指令 §0）：

  ① 把 verified LKGC 从 8409c271cca966e9f9ab0ad0ba2d6470c0a66e10
     推进到            19f9738c980e0a8a31b557c346fb50a4af711cab；
  ② 开始 M12-C C2。

未授权（本轮明确不做）：C3 · M12-D · canonical package · release · tag · push ·
real hardware claims · scope expansion。
```

**授权边界澄清**：本授权 = **LKGC advancement + 允许对 C2 做 contract recovery / 第一切片**；
**不是**「授权 WorkBuddy 发明 C2 缺失语义」。若 repository truth 显示 C2 仍存在未决产品/语义决策，
则按 §1/§5/§7 要求 **STOP after Phase B**，不做实现。本轮实际结果见 §65.3。

### 65.1 ancestry 与 behavior-bearing 证据（Git 实测）

```text
$ git cat-file -t 19f9738c980e0a8a31b557c346fb50a4af711cab
commit

$ git merge-base --is-ancestor 19f9738c980e0a8a31b557c346fb50a4af711cab HEAD
(exit 0)

$ git show -s --format='%H | %ad | %s' 19f9738c980e0a8a31b557c346fb50a4af711cab
19f9738c980e0a8a31b557c346fb50a4af711cab
Tue Sep 29 12:42:25 2026 +0800
M12: give manual preview usable vertical space

$ git diff --name-status 19f9738~1 19f9738
M  src/main.cpp
M  src/ui/qml/pages/DeviceProfilePage.qml
⇒ 2 files 变化，含 src/ 与 QML ⇒ BEHAVIOR-BEARING（非 docs-only）

$ git diff --name-status 19f9738 HEAD
M  docs/BACKLOG.md
M  docs/PROJECT_STATUS.md
M  docs/devlog/2026-09-29.md
M  docs/tasks/T027-m12-device-profile-manual-intelligence-contract.md
$ git log --oneline 19f9738..HEAD
b9d98af M12: archive C1b human visual acceptance
79b0833 M12: archive manual preview layout remediation
⇒ 19f9738 之后的 2 个提交全部 docs-only
⇒ 19f9738 = 最后一个 behavior-bearing tree，docs-only 提交永不作 LKGC

$ git diff --name-only 8409c27 19f9738 -- src tests CMakeLists.txt cmake scripts assets samples
CMakeLists.txt
cmake/modbuslens_generate_candidate.cmake
scripts/materialize_c1b_deps.py
scripts/test_materialize_c1b_deps.py
src/core/manual/ManualDocument.{h,cpp}
src/main.cpp
src/ui/manual/ManualDocxTextExtractor.{h,cpp}
src/ui/manual/ManualImportController.{h,cpp}
src/ui/manual/ManualPdfTextExtractor.{h,cpp}
src/ui/manual/ManualStore.{h,cpp}
src/ui/manual/ManualTextExtraction.{h,cpp}
src/ui/qml/pages/DeviceProfilePage.qml
tests/deployment_startup_check.cmake
tests/test_manual_extraction.cpp
tests/test_manual_import.cpp
tests/test_manual_import_pdf_docx.cpp
⇒ 8409c27 → 19f9738 区间共 31 files / +6228 −106，行为路径非空
（C1b extraction foundation fb2170e → import workflow/UI c36eb18 →
 self-contained candidate a7bbbad → preview 垂直空间 19f9738）
⇒ LKGC 推进跨越了 C1b 全部已验收的 behavior-bearing 工作
```

### 65.2 verified LKGC advancement

```text
verified LKGC BEFORE = 8409c271cca966e9f9ab0ad0ba2d6470c0a66e10
verified LKGC AFTER  = 19f9738c980e0a8a31b557c346fb50a4af711cab   （Human authorized）

历史链（behavior-bearing，全部 Human 授权）：
  fc86dcc（M10-C）→ 9bdd99c → d08ab55（M10 Read Correction）→ 352b81c（M10）
  → bc99e6e（M11）→ 13799d6（M12-B）→ 8409c27（M12-C C1a）→ 19f9738（M12-C C1b）

对应状态：M9 ✅ · M10 ✅ COMPLETE · M11 ✅ COMPLETE ·
  M12-A ✅ · M12-B ✅ COMPLETE · M12-C C1b = COMPLETE / HUMAN ACCEPTED
  （Implementation COMPLETE · Automated regression PASS · Human functional PASS · Human visual PASS）
本轮未改动的历史：8409c27 的所有其他出现位置保持历史原文（只增不改原则）。
```

### 65.3 C2 contract recovery（Phase B 结果摘要）

本轮在已授权范围内完成 **C2 contract recovery**（只读仓库真相，未写任何 C2 代码）。

**recovered C2 contract**（唯一 canonical 来源 = 本节 §47.10 / §47.11 / §47.12 / §47.13 / §47.14 / §15 / §17.2）：

```text
C2 NAME        = Provider-neutral AI Candidate Extraction
C2 PURPOSE     = 从已导入的 deterministic manual source 产生「建议 + evidence + confidence +
                 confirmation state」，供 Human 审阅；绝不直接改写 verified Profile
C2 INPUT       = C1 产出的 ManualDocument + deterministic extracted text + EvidenceReference
C2 OUTPUT      = Candidate 列表（ProfileField / RegisterEntryCandidate）
                 每项含 value + evidence + confidence + confirmation state
C2 OWNER       = PROPOSED（§47.9：DeviceProfilePage.qml 内新 manual/import controller）
C2 UI          = 未冻结（§47.9 明确「未冻结」）
C2 PERSISTENCE = 未冻结（§47.14 P0-E：v1 = session-only 仅为 PROPOSED）
C2 AI BOUNDARY = AI = extractor/assistant ≠ authority（canonical，已冻结）；
                 Candidate 不得直接构造 verified RegisterEntry
C2 HUMAN GATE  = Accept / Edit / Reject（canonical，属 C3 执行）
C2 ERROR/FALLBACK = §47.11 invariant 7：AI 失败 / 网络失败不得影响 imported manual 与 verified Profile
C2 NON-GOALS   = 不选 provider·model·endpoint；不做 OCR；不做 Q&A（M12-D）；不做 Accept/Edit/Reject（C3）
C2 ACCEPTANCE  = §17.2 M12-C exit（高层；无 C2 detailed matrix）
C2 DEPENDENCIES ON C1b = 直接消费 ManualDocument / ManualStore / extracted text / contentHash cache
C2 RELATION TO C3/M12-D = C3 = Human Accept/Edit/Reject + validateDeviceProfile → verified Profile；
                 M12-D = Manual Q&A（硬边界，见 §47.12）
```

**gate 判定 = HUMAN DECISION REQUIRED**（详见本轮报告 E/F/G 节）。开放项（**均未被仓库真相解析**）：

```text
REQUIRES HUMAN DECISION（阻塞 C2，§47.14 仍标 NOT FROZEN）：
  P0-B AI provider family          （§47.15 NOT FROZEN；§48.2#5 仅 guardrail「provider-neutral、不选厂商」）
  P0-C cloud upload permission     （§47.15 NOT FROZEN；§48.2#6 仅 guardrail）
  P0-D credential source / storage （§47.15 NOT FROZEN；§48.2#7 仅 guardrail）
  P0-E Candidate persistence       （§47.15 NOT FROZEN；§48.2#9 仅 guardrail "v1 session-only"）
  P0-H confidence representation   （§47.15 NOT FROZEN；§48.2#8 仅 guardrail "label + opaque raw"）

上述 §48.2 第 5–9 行明标「HUMAN-FROZEN（**未来 guardrail**）」，且逐行注明「C1a 不涉及」
⇒ 它们是**方向性 guardrail**，**不足以唯一确定 C2 的 externally visible behavior**：
  · provider seam 的具体形态（接口面 / 调用节奏 / 是否可插拔 backend）· 未冻结
  · prompt contract（格式 / 注入边界 / bounded 规则）· §15 明确「本轮不决定 AI provider、prompt format」
  · response parsing（成功/失败判别、字段映射）· 未冻结
  · Candidate 的数据模型细节（§47.10 全文标注 PROPOSED；EvidenceReference 的 excerpt 长度、
    textStart/textEnd offset 格式、content identity 字段均 PROPOSED 未冻结）
  · §47.11 Candidate 状态名/数量「未冻结」（建议最小集仅为建议）
  · §47.9 UI owner 与 §47.9 B「是否属 Device Profile workspace」= PROPOSED / 未冻结

RESOLVED BY REPOSITORY TRUTH（可直接实施、无需新裁定）：
  · AI 不得直接修改 verified Profile（canonical §15 + 11_V2_UPGRADE_PLAN §4 第 104/106 行）
  · Candidate 进入 Profile 前必须走 validateDeviceProfile（§47.11 invariant 3，DERIVED BUT SAFE）
  · §47.11 invariant 1/2/4/5/6/7（Pending/Rejected 不得进入；失败必须原子回滚；失败后仍可修正；
    AI/网络失败不得影响 manual 与 verified Profile）
  · Candidate 不得改写 wire truth / TransactionAnalysis / M11 decode / raw（四层 truth，canonical）
  · registerCount 不是 AI 输入（§33 Group 1-B，已冻结）
  · C1b 契约：document identity != contentHash；contentHash = cache identity（§60.3，已冻结）
```

**结论**：C2 的**边界与不变量**已被仓库真相充分定义，但 C2 的**可外部观察行为**
（provider seam 形态、prompt 契约、response parsing、Candidate 数据模型细节、
confidence 表示、持久化）依赖 §47.14 P0-B/C/D/E/H 与 §47.10/§47.11/§47.9 的未冻结项。
按 SESSION L §7 的定义，**至少一个未决问题会迫使 WorkBuddy 发明 externally visible behavior
⇒ 判定 = HUMAN DECISION REQUIRED，DO NOT CODE**。

### 65.4 C2 状态（禁止升级）

```text
M12-C C2 = STARTED（**仅 contract recovery**）
M12-C C2 IMPLEMENTATION = NOT STARTED
未新增任何 Candidate / provider seam / extraction-AI 代码（实测 src/ tests/ 中无 Candidate 模型：
  grep 命中的 "candidate" 全为既有 framing / protocol / catalog 语义，非 M12 Candidate）
未改 src/ · tests/ · CMakeLists.txt · QML · scripts/
C3 · M12-D = NOT STARTED
```

### 65.5 本轮动作边界（Session L · docs-only）

```text
未 build · 未 test · 未重新生成 candidate · 未 packaging · 未改产品代码 ·
未写 C2 代码 · 未开始 C3 / M12-D · 未创建 canonical package · 未 push / 未 tag / 未 amend。

唯一动作 = 归档 Human 授权的 LKGC 推进 + C2 contract recovery 结论：
  docs/tasks/T027 §65（本节）
  docs/PROJECT_STATUS.md（顶部追加批注块 + 标注前一 owner 块为历史链一环）
  docs/BACKLOG.md（M12 行追加 LKGC 推进批注）
  docs/devlog/2026-09-29.md（Session L 章节 + 状态字段更新）
并提交一个 docs-only commit（subject = `M12: advance verified LKGC through C1b`）。

仍未变：M12-C C1b = COMPLETE / HUMAN ACCEPTED；M12-C = IN PROGRESS；
C3 / M12-D = NOT STARTED；canonical package = NOT CREATED；
REAL MODBUS HARDWARE = NOT VERIFIED；
failure #1 exact foreign provider = NOT VERIFIED；
failure #2 historical exact root cause = UNKNOWN。
```

---

## 66. M12-C C2 — AI CANDIDATE CONTRACT FREEZE（2026-09-29 · Session M · docs-only）

> 性质：**append-only 归档**。本节把 Human 在 SESSION M 对 C2 缺失产品/语义决策的 10 条裁定
> （H1–H10）逐字冻结为 **HUMAN-APPROVED C2 CONTRACT**，并**显式记录**它们与
> §15 / §19 / §47.10 / §47.11 / §47.13 / §47.14 / §47.15 / §48 的历史关系。
> **禁止**据此对历史章节做全局机械替换；历史引用保持历史原貌。

### 66.0 Human 授权原文（逐字归档）

```text
Human 明确回复（SESSION M §0）：

  「同意以上 C2 冻结方案。」

该回复针对 SESSION M 提交的 H1–H10 C2 冻结方案（10 条），构成新的
HUMAN-APPROVED C2 CONTRACT。

同时确认 SESSION L 终态（SESSION M §0 起始 resync 基线）：
  HEAD                    = 489d91189a9e8fa7a2f8aed06c0434e5873092fa
  verified LKGC           = 19f9738c980e0a8a31b557c346fb50a4af711cab
  M12-C C1b               = COMPLETE / HUMAN ACCEPTED
  M12-C C2                = STARTED（仅 contract recovery）
  M12-C C2 IMPLEMENTATION = NOT STARTED

未授权（本轮明确不做）：C3 · M12-D · canonical package · release · tag · push ·
real hardware claims · scope expansion。
```

### 66.1 H1–H10 冻结契约（逐条）

**H1 PROVIDER（provider-neutral）**

```text
架构保持 provider-neutral。首个真实 / live provider 允许沿用既有 ModelScope；
model id 属 configuration，不成为业务真相，不冻结进 Candidate domain contract。
first slice 不要求 live ModelScope。
派生：C++ 侧不得出现 ModelScope-specific Candidate domain 类型 / OpenAI-specific
业务类型；provider-specific transport object 停在 adapter boundary 之外。
```

**H2 CLOUD UPLOAD（默认不上传）**

```text
默认 NO UPLOAD（deterministic 路径零网络）。
仅 Human 显式触发「AI 提取候选」时允许 cloud extraction。
selected document 首次需上传前，UI 必须展示 payload scope。
「允许上传」= selected document 的 canonical extracted text。
不上传：原始 PDF/DOCX bytes · 其他 manual · verified Device Profile ·
unrelated transaction history · credentials。
拒绝 ⇒ zero network request。
该 UI / live-network 行为不属于本 first slice 范围，但 domain / seam 不得与之冲突。
```

**H3 CREDENTIAL**

```text
v1 凭据来源 = process environment only + BYOK seam。
禁止：UI 持久化 API key · 写入 Profile JSON · 写入 Manual metadata · 写入 Candidate ·
写入 repo · 写入 log · 任何 plaintext credential persistence。
missing credential ⇒ 明确失败；不得改变 Manual / Candidate / verified Profile truth。
first slice 无 live provider ⇒ 不得要求 credential。
```

**H4 CANDIDATE PERSISTENCE**

```text
v1 = SESSION-ONLY / IN-MEMORY。重启即消失。
不写入：Device Profile JSON · manual metadata · cache truth · repo · long-term DB。
只有未来 C3 的 Human Accept / Edit 经确定性 validation 后，才可能进入 verified Profile。
```

**H5 CONFIDENCE**

```text
C2 v1：DO NOT use / display / persist provider-reported numeric confidence。
Evidence 是候选可信依据。
保留 §48.1 第 8 项 / §48.2 #8 历史原文（label + optional provider raw opaque value，
不得解释成概率）；本节新增「C2 v1 进一步 narrowing」= first slice 不实现 confidence。
```

**H6 EVIDENCE REFERENCE（本地确定性可验证）**

```text
每个有效 Candidate 必须具备可由本地 deterministic code 验证的 Evidence，绑定：
  · Manual document identity（documentId）+ content identity（contentHash）/
    canonical extracted-text identity；
  · exact excerpt；
  · canonical extracted-text location（由程序本地计算 / 确认）。
PDF 可额外携带 page identity / index；TXT / Markdown / DOCX 不得为统一 schema 强造 page。
AI / provider 返回的 quote / location = UNTRUSTED INPUT；程序必须针对 C1b canonical
extracted text 重新验证 / 重新定位。
provider 自报 offset / page / location 不能直接成为 truth。
exact excerpt 无法验证 ⇒ MUST NOT become a valid Candidate。
重复 excerpt / ambiguous location 且无既有确定性规则 ⇒ 本 first slice 不发明全局
ambiguity policy：使用唯一可定位 fixture，ambiguity behavior 明确 DEFERRED。
```

**H7 CANDIDATE LIFECYCLE**

```text
C2 只产生 PendingReview（或 repository 语义等价的 canonical state）。
Accept / Edit / Reject 属 C3。
不得 AI → verified Profile direct write。
```

**H8 PROMPT / OUTPUT CONTRACT**

```text
冻结的是 semantic contract 而非 prompt 文案。
AI 只能从提供的 canonical extracted text 提取。
不得猜 scale / offset / unit / register meaning / address / field value；
source evidence 不支持则不产出。
provider output 必须经 deterministic schema validation + deterministic evidence
validation 才能成为 Candidate。
malformed / invalid provider output 不得产生 verified Profile truth。
prompt wording / parser class / adapter class shape 由工程实现决定。
```

**H9 C2 UI BOUNDARY**

```text
未来可提供：AI extraction trigger · running state · failure reason · Candidate result display。
完整 Accept / Edit / Reject 属 C3。
first slice NO QML required（除非 repository architecture 证明否则）。
```

**H10 RETRY / FAILURE**

```text
AI / network / parser failure 必须相对 imported Manual / canonical extracted text /
verified Profile 原子。
已有成功 Candidate 集存在时，失败 retry 不得覆盖 / 清空。
新的成功 extraction 才允许替换同一 document 在当前 session 的旧 Candidate set。
first slice 不要求实现 retry，但 domain model / seam 不得让未来该 invariant 无法实现。
```

### 66.2 与历史决策的关系（显式记录，禁止机械替换）

| # | 历史出处 | 关系 | 说明 |
| --- | --- | --- | --- |
| H1 | §48.1 第 5 项 / §48.2 #5（HUMAN-FROZEN 未来 guardrail） | **REAFFIRMED**；早期「当前不选具体 provider」**保留**，并被更晚的 C2-specific decision **局部 supersede** | 历史原文（「当前不选任何厂商」）作为 C1a 时点记录**不改**；H1 新增的是「首个 live provider 允许沿用 ModelScope，但 model id 属 configuration、不进入 Candidate domain contract」 |
| H2 | §48.1 第 6 项 / §48.2 #6；§47.14 P0-C | **REAFFIRMED + NARROWED** | 新增边界：上传内容**仅** canonical extracted text；拒绝 ⇒ zero network；UI / live 行为非本切片范围 |
| H3 | §48.1 第 7 项 / §48.2 #7；§47.14 P0-D | **REAFFIRMED + NARROWED（v1 限定 process environment only）** | 保留「env var / BYOK seam；secret 不进入 repo / Profile / manual / candidate」；新增「v1 仅 process environment」以及「无 live provider 时不得要求 credential」 |
| H4 | §48.1 第 9 项 / §48.2 #9；§47.14 P0-E | **REAFFIRMED** | 「AI raw response 不长期保存」一并 REAFFIRMED |
| H5 | §48.1 第 8 项 / §48.2 #8；§47.14 P0-H | **保留历史原文 + 追加 C2 v1 narrowing** | 不改 §48 原文；追加「C2 v1 不使用 / 不显示 / 不持久化 provider-reported numeric confidence」 |
| H6 | §47.10 EvidenceReference 核心原则；§48.3F no-profile-mutation | **派生强化（非新发明）** | 「Candidate 不得只有 value + AI says page 17」→ H6 要求本地重算 location；offset 格式沿用既有 `ManualEvidenceReference` 约定（CHARACTER offsets，half open） |
| H7 | §47.11 invariant 1 / 2 | **REAFFIRMED** | Pending / Rejected 不得进入 verified Profile |
| H8 | §15 / §19（本轮不决定 prompt format）；§47.11 invariant 3 / 4 | **REAFFIRMED + 明确分工** | 冻结 semantic contract；prompt wording / parser / adapter shape 属工程实现 |
| H9 | §47.9 PROPOSED UI；§47.12 M12-C / M12-D hard boundary | **NARROWED（first slice 不要求）** | 不把 PROPOSED UI 提前冻结；不触碰 M12-D |
| H10 | §47.11 invariant 7 | **REAFFIRMED + 扩展** | 新增「失败 retry 不得清空既有成功 Candidate 集」 |

### 66.3 本 Session 第一切片范围声明（PHASE B 执行边界）

```text
FIRST SLICE（PHASE B）= C2 第一个最小 deterministic vertical slice：
  provider-neutral candidate + evidence local validation foundation。

INPUT  = one imported ManualDocument + C1b canonical extracted text +
         provider-neutral proposal input（由 TEST-ONLY deterministic fake provider 提供）
PROPOSAL 至少含：target field identity · proposed value · evidence exact excerpt
OUTPUT = zero or more PendingReview Candidate，每个 valid Candidate 绑定：
         actual ManualDocument identity · actual content identity ·
         exact evidence excerpt · deterministic locally-validated location ·
         proposed value；且不是 verified Profile truth。

明确不做：live ModelScope request · API key flow · cloud consent UI ·
big QML Candidate review UI · Accept / Edit / Reject · DeviceProfile write ·
Candidate persistence · confidence · retry · C3 · M12-D · canonical package。
完成一个 first slice 后必须 STOP。
```

### 66.4 明确未冻结 / DEFERRED

```text
AMBIGUOUS EVIDENCE MATCH POLICY  = DEFERRED / NOT IMPLEMENTED / NOT GUESSED
live ModelScope provider         = NOT STARTED
cloud consent UI                 = NOT STARTED
C2 Human Candidate review（C3）  = NOT STARTED / NOT AUTHORIZED
M12-D                            = NOT STARTED / NOT AUTHORIZED
canonical package                = NOT CREATED
```

### 66.5 状态

```text
M12-C C1b                        = COMPLETE / HUMAN ACCEPTED（未变）
M12-C                            = IN PROGRESS
M12-C C2                         = IN PROGRESS（contract 已冻结；first slice 见 §66.3）
H1–H10                           = HUMAN-APPROVED C2 CONTRACT（本节）
verified LKGC                    = 19f9738c980e0a8a31b557c346fb50a4af711cab（UNCHANGED）
C3 · M12-D                       = NOT STARTED
canonical package                = NOT CREATED
REAL MODBUS HARDWARE             = NOT VERIFIED
```

**本节动作边界（Session M · Phase A · docs-only）**：仅归档 Human H1–H10 裁定 + 历史关系；
未 build · 未 test · 未改产品代码 · 未开始 C3 / M12-D · 未创建 canonical package ·
未 push / 未 tag / 未 amend。

---

## 67. M12-C C2 — FIRST DETERMINISTIC SLICE ARCHIVE（2026-09-29 · Session M · behavior + docs）

### 67.0 两个提交

```text
冻结提交（docs-only，永不作 LKGC）：
  cf68d6823e7f5640e658ca5728191e90729a3f3a  M12: freeze C2 candidate extraction contract
  （4 files / +309 −4：docs/BACKLOG.md · docs/PROJECT_STATUS.md ·
    docs/devlog/2026-09-29.md · docs/tasks/T027…；NO AMEND）

行为提交（behavior-bearing；不是 LKGC）：
  d899e55593cfc29519779af48bd01ecb998a18b5  M12: add evidence-backed AI candidate foundation
  （4 files / +840 −0：CMakeLists.txt · src/core/candidate/CandidateExtraction.{h,cpp} ·
    tests/test_candidate_extraction.cpp；NO AMEND）
```

### 67.1 Pre-code source audit（结论）

- **A. Manual source of truth**：`src/core/manual/ManualDocument.h`（Zero-Qt）——`documentId` = document
  identity（程序生成稳定 identity）；`contentHash` = content identity（imported source bytes SHA-256）。
  `ManualEvidenceReference` offset = **CHARACTER offsets（QString UTF-16 code units）half-open [start,end)，
  -1 不适用**；`pageNumber = -1` for TXT/Markdown（永不伪造 page）。`src/ui/manual/ManualStore.h`：
  `loadText(contentHash)`（`<root>/manuals/text/<contentHash>.txt`，UTF-8 extracted cache）·
  `loadPdfPages(contentHash)`（同目录 `.json` 的 `pages[]` = PDF per-page truth）。UI 的「第 N 页」header =
  presentation-only。`ManualImportController::evidenceReferenceAt(start,end)` 已存在同一约定 ⇒ **C2 复用，
  不建第二套**。
- **B. Device Profile**：`validateDeviceProfile` = 唯一 validation owner（纯函数，从不 mutate）；
  `ProfileStore`（Qt 侧）= JSON 契约编码 + `QSaveFile` 原子写 + managed-root override。
- **C. Existing AI architecture**：`src/ui/ai/ModelScopeDiagnosisClient.h` 为 **Diagnosis-specific**；
  `tests/fake_chat_completions_server.{h,cpp}` 证明 fake/stub pattern 已存在 ⇒ **不复制第二套网络框架、
  不把 Candidate domain 塞进 Diagnosis-specific authority**。
- **D. Candidate shape（§47.9）**：ProfileField = profileId/displayName/manufacturer/model/revision/
  description；RegisterEntryCandidate = readFunctionCode/address/name/description/dataType/byteOrder/
  wordOrder/scale/offset/unit；`registerCount` 不是 AI 输入。
- **E. Build owner**：`modbuslens_core` = **Zero-Qt STATIC（links no Qt at all）** ⇒ 新 domain 进
  `src/core/candidate/`；Qt 侧只在测试目标中（ProfileStore 隔离断言）。

### 67.2 first-slice 选定（单一 ProfileField）

```text
chosen field = ProfileFieldTarget::Manufacturer
  （§47.9 明确允许；first slice 只实现一个 ProfileField —— 不实现所有 ProfileField，
   也不实现 RegisterEntryCandidate）
编译期常量：kC2FirstSliceProfileField = ProfileFieldTarget::Manufacturer
非该 target 的 proposal 在 deterministic validator 中被拒绝（refusedProposalCount++），
故该约束是**真实行为**，不是空守卫。
```

### 67.3 Provider-neutral seam 架构

```text
struct CandidateProposal        { target, proposedValue, evidenceExcerpt, locationHint(-1) }
class  ICandidateProposalProvider { virtual propose(ManualDocument, string_view) = 0; }
      ⇒ 唯一 provider 抽象；无 ModelScope / OpenAI 业务类型；provider transport 停在其边界外。
locationHint 被建模为 UNTRUSTED 输入（真实 provider 确实会报 offset），使「忽略 hint」成为
**可测的真实行为**（C2-A06），而不是不可测的假设；它永不进入 CandidateEvidence。
struct CandidateEvidence { documentId, contentHash, textStart, textEnd, excerpt }
struct ProfileFieldCandidate { target, proposedValue, evidence, lifecycle }
      ⇒ lifecycle ∈ { PendingReview }（C2 只产生 PendingReview；Accept/Edit/Reject = C3）。
```

### 67.4 EvidenceReference 表示（H6）

```text
documentId  = ManualDocument.documentId（NEVER contentHash）
contentHash = ManualDocument.contentHash
textStart/textEnd = CHARACTER offsets into canonical extracted text, half open [start,end)
                    —— 由本地 deterministic code **重新计算**，provider 自报 location 不参与。
excerpt     = exact excerpt（原样保存）
```

**AMBIGUOUS EVIDENCE MATCH POLICY = DEFERRED / NOT IMPLEMENTED / NOT GUESSED**：`locateUniqueExcerpt`
只在 exact excerpt **唯一**出现时给出 span；出现 0 次 ⇒ 拒绝（H6：无法验证 ⇒ 不得成为 valid Candidate）；
出现 ≥2 次 ⇒ 亦拒绝（保守解读 H6，不发明全局歧义策略，不伪造 span）。fixtures 使用唯一 excerpt。

### 67.5 REAL RED（无 compile-fail RED）

```text
command  : build/release/modbuslens_candidate_extraction_tests.exe
exit code: 12
结果     : Totals: 2 passed, 12 failed, 0 skipped, 0 blacklisted, 616ms
首选断言 : a01_presentExcerptYieldsOnePendingReviewCandidate()
           FAIL! 'provider.called' returned FALSE.
性质     : placeholder（返回空结果）⇒ 纯语义断言 RED；**非** compile failure / DLL failure /
           fixture malformed / crash（build 与 link 均成功）。
```

### 67.6 GREEN 实现（最小行为）

```text
consume provider-neutral proposals →
  schema validation（target == first-slice field；proposedValue 非空）→
  evidence validation（locateUniqueExcerpt 对 canonical text）→
  local location 计算（NEVER 读 locationHint）→
  construct PendingReview ProfileFieldCandidate →
  no Profile mutation · no persistence · no network · no credential · no confidence
```

GREEN 结果：`Totals: 14 passed, 0 failed`，exit 0。

### 67.7 C2-A01..A12 实测矩阵

| case | 断言 | 结果 |
| --- | --- | --- |
| A01 | exact excerpt 存在 ⇒ 恰好 1 个 PendingReview Candidate；provider 被真实消费；收到 canonical text | PASS |
| A02 | excerpt 不存在 ⇒ 0 candidate、refusedProposalCount=1 | PASS |
| A03 | `evidence.documentId` == actual `document.documentId` 且 != contentHash | PASS |
| A04 | `evidence.contentHash` == `document.contentHash` | PASS |
| A05 | 回读 canonical text 的 [textStart,textEnd) 精确得到 stored excerpt（round-trip） | PASS |
| A06 | provider 自报 locationHint（0 / 4096 / -1）不污染 validated location（== 真值） | PASS |
| A07 | candidate 生成后 `DeviceProfile` value-for-value 不变、validation code 不变 | PASS |
| A08 | 不创建 / 不更新 ProfileStore 持久化（隔离 root 下 tree 快照不变；目标路径不存在） | PASS |
| A09 | 无 Candidate / manual cache 持久化 side effect（隔离 root tree 快照不变） | PASS |
| A10 | Candidate 无语义 confidence 数值字段（**编译期 SFINAE static_assert** + 运行期字段集检查） | PASS |
| A11 | 同 deterministic 输入两次 ⇒ semantic fields 相等（ProfileFieldCandidate 值相等） | PASS |
| A12 | 驱动来自 canonical extracted text，不重读 original source（BOM 差异 + 删除原文件后仍驱动） | PASS |

A10 说明：`hasConfidenceMember` / `hasScoreMember` / `hasProbabilityMember` 对
`ProfileFieldCandidate` / `CandidateEvidence` / `CandidateProposal` 的 `static_assert(!…)`
使「重新引入 confidence 字段」**破坏构建**，因此该冻结不是文档级声明而是可执行约束。

### 67.8 Negative control（非空洞性证明）

```text
precise mutation：在 locateUniqueExcerpt 内把「excerpt 不存在 ⇒ return false」改为
                  「假定其存在于 0 并 return true」。
预期     ：C2-A02 REAL RED。
实测     ：exit 1；Totals: 13 passed, 1 failed；
           FAIL! a02_absentExcerptYieldsNoValidCandidate
             Actual (result.candidates.size()) = 1 / Expected = 0
           ⇒ compile / link 正常、test process 正常、**semantic assertion RED**。
precise reverse：精确还原原 `return false;` 分支（**未用** checkout / restore / reset）。
residue  ：grep -c "NEGATIVE-CONTROL MUTATION" = 0（残留为零）。
恢复后   ：exit 0；Totals: 14 passed, 0 failed（GREEN 复现）。
```

### 67.9 Targeted regression

```text
manual_import           Passed  16.00 sec   （C1a/C1b store + controller，相邻面）
manual_extraction       Passed   5.12 sec
manual_import_pdf_docx  Passed  11.85 sec
candidate_extraction    Passed   1.49 sec   （本轮新增，C2 first slice）
device_profile          （DeviceProfile domain/validation 未改动，仍随 full regression 覆盖）
全部 PASS，exit 0。
```

### 67.10 Release full regression

```text
release gate #29 deployment_startup_check  Passed 217.12 sec
build  : cmake --build build/release  ⇒ 78/78 objects 全部链接成功
ctest -N : Total Tests: 58   （前次 57；+1 = candidate_extraction；#27）
结果   : 100% tests passed, 0 tests failed out of 58
         Total Test time (real) = 876.02 sec
ctest exit code = 0
```

Debug：新增源在当前 canonical compiler（MinGW GCC 13.1.0）下**编译成功**
（`modbuslens_core.dir/src/core/candidate/CandidateExtraction.cpp.obj` 生成）；
Debug 链接阶段命中**既有环境问题** `ar/ranlib: could not create temporary file whilst archive`（非本轮代码语义）。
按 §15「不得因历史环境问题无限重试」，Release（canonical gate）已提供完整 58/58 证据；Debug 未运行 full。

### 67.11 AI boundary audit（静态，不替代行为测试）

```text
新 domain/test 文件中：
  QNetworkAccessManager / QNetworkRequest / QUrl / http / endpoint / QProcess = 0
  apiKey / api_key / Authorization / Bearer / getenv / qgetenv           = 0
  QSettings / QStandardPaths / QSaveFile                                  = 0
  QFile / QDir / std::filesystem / fopen                                  = 0（无 I/O）
  ModelScope                                                              = 1（注释，声明禁止）
  confidence / probability / score  仅出现在 (a) 说明「禁止」的注释、
                                    (b) **负向** static_assert / 字段名黑名单检查
⇒ 无 network call · 无 URL · 无 credential 读取要求 · 无 secret ·
  无 ModelScope-specific Candidate domain · 无 ProfileStore 写入 ·
  无 DeviceProfile mutation · 无 Candidate 持久化 · 无 confidence 数值 · 无 raw model response 持久化。
（行为证据见 C2-A06/A07/A08/A09/A10 与 C2-A12。）
```

### 67.12 Protected diff audit

```text
git diff --name-status（behavior commit d899e55）
  M  CMakeLists.txt
  A  src/core/candidate/CandidateExtraction.h
  A  src/core/candidate/CandidateExtraction.cpp
  A  tests/test_candidate_extraction.cpp
protected paths diff（src/core/analysis · src/core/protocol · src/main.cpp · src/ui/qml ·
  tests/test_manual_import.cpp · tests/test_manual_extraction.cpp · tests/test_manual_import_pdf_docx.cpp ·
  src/ui/manual · src/ui/profile · src/core/profile · src/core/manual · scripts） = 0
git diff --check = 0；无 build/generated 文件；无 secret；无 provider-specific domain coupling；
无大块 accidental rewrite（CMakeLists 仅 +1 core 源行 + 27 行新测试目标注册）。
```

### 67.13 明确未做 / 未冻结

```text
live ModelScope request / provider wiring = NOT STARTED
API key flow / cloud consent UI           = NOT STARTED
big QML Candidate review UI               = NOT STARTED（first slice NO QML required）
Accept / Edit / Reject（C3）              = NOT STARTED / NOT AUTHORIZED
DeviceProfile write / Candidate persistence = NOT STARTED
confidence / retry                        = NOT STARTED
AMBIGUOUS EVIDENCE MATCH POLICY           = DEFERRED / NOT IMPLEMENTED / NOT GUESSED
C3 · M12-D                                = NOT STARTED / NOT AUTHORIZED
canonical package / release tag / push    = NOT DONE
```

### 67.14 状态

```text
M12-C C1b                        = COMPLETE / HUMAN ACCEPTED（未变）
M12-C                            = IN PROGRESS
M12-C C2                         = IN PROGRESS
C2 Candidate/Evidence foundation = IMPLEMENTED / AUTOMATED PASS
verified LKGC                    = 19f9738c980e0a8a31b557c346fb50a4af711cab（**UNCHANGED**）
C3 · M12-D                       = NOT STARTED / NOT AUTHORIZED
canonical package                = NOT CREATED
REAL MODBUS HARDWARE             = NOT VERIFIED
```

**注意**：本行为提交**不是** LKGC；verified LKGC 仍为 `19f9738…`，推进必须 Human 明确授权。
**SESSION M = COMPLETE — STOP**（未开始第二个 C2 slice）。

---

## 68. M12-C C2 SECOND SLICE — PROVIDER ADAPTER FREEZE + SCOPE AUTHORIZATION（2026-09-29 · Session N · docs-only）

> 性质：**append-only 归档**。本节把 Human 在 SESSION N §0 对 C2 **第二个切片**的显式授权
> （start + 5 项目标）与**范围边界**（in scope / explicit deferred）冻结为可审计的 canonical 记录，
> 并在**写任何产品代码之前**落库（SESSION N §6 顺序要求）。
> **禁止**据此改写 §66 / §67（SESSION M 历史原貌保持）；本节只新增。

### 68.0 Human 授权原文（逐字归档）

```text
Human（SESSION N §0 HUMAN AUTHORIZATION）：

  「M12-C C2 SECOND SLICE = START.」

授权针对的目标（5 项，逐字转写）：
  ① ModelScope provider adapter boundary
  ② provider-neutral extraction request / response contract
  ③ deterministic strict schema / parser validation
  ④ deterministic fake transport
  ⑤ integration with SESSION M local Evidence validation

同轮明确声明：NO live network。

起始基线（SESSION N §1 KNOWN STARTING STATE，实测复核通过）：
  HEAD                    = 91ac7a2df8ed033647d71107692f5b17a14cae38
  porcelain -uall         = ?? _ctx.py / ?? _dump.py（仅此两项）
  tracked diff            = 空 · index(cached) = 空 · diff --check rc = 0
  tags                    = v1.0.0（唯一）
  git ls-files build      = 0
  verified LKGC           = 19f9738c980e0a8a31b557c346fb50a4af711cab
  SESSION M Release full  = 58/58 PASS（行为提交 d899e55）
```

### 68.1 SECOND SLICE 目标与边界（IN SCOPE）

```text
本切片 = C2 第二个最小 slice：provider adapter + strict provider response contract。
交付物（行为）：
  · provider-neutral EXTRACTION REQUEST contract（只表达 extraction 所需内容）
  · ModelScope adapter boundary（provider-specific 类型 / wire envelope / endpoint /
    model id / HTTP status 一律停在边界内，不进入 Candidate domain）
  · deterministic STRICT provider-response parser（fail-closed；无 best-effort、无类型强转）
  · deterministic FAKE TRANSPORT（capture outbound request / 返回确定性成功响应 /
    返回确定性失败 / 计数调用）——SESSION N 内唯一被消费的 transport
  · 与 SESSION M 既有 local Evidence validation 的集成（proposal → 本地验证 → PendingReview Candidate）

架构依赖方向（SESSION N §9，概念级；具体类名/文件由 repository 架构决定）：
  canonical Manual text → provider-neutral extraction request → provider adapter boundary
  → ModelScope adapter → transport seam → synthetic/fake provider response（测试内）
  → response content extraction → strict deterministic proposal parser
  → provider-neutral proposal → SESSION M local Evidence validation → PendingReview Candidate
```

### 68.2 交付纪律（本切片强制）

```text
· provider-specific 类型 MUST stop at the adapter boundary。
· Candidate domain MUST NOT 含 ModelScope 类型 / endpoint / model id / response envelope /
  HTTP status / transport object。
· SESSION N 自动化测试 MUST 使用 deterministic fake transport；**不得** live network、
  **不得**需要 token、**不得**花钱。
· Production UI MUST NOT 在本 session 被接到真实请求（不得让产品路径可能发出真实网络请求）。
· 若 M6 已有合适 injected transport seam ⇒ 复用；否则加最小 provider-transport seam；
  **不建 generalized HTTP framework**。
· 不得为「声称复用」而把 C2 Candidate 语义塞进 M6 Diagnosis-specific domain。
· provider evidence 未经验证；provider **不得**决定 canonical evidence location；
  SESSION M 本地确定性 Evidence validation 仍为权威。
· 无法本地验证的 excerpt ⇒ MUST NOT become valid PendingReview Candidate。
· raw provider response 只可 transient 存在于 adapter/parser call scope，
  MUST NOT 存入 Candidate / Profile / manual metadata / cache / project docs / 长期状态。
```

### 68.3 明确未授权 / DEFERRED（OUT OF SCOPE，本 session 不得执行）

```text
verified LKGC advancement          = NOT AUTHORIZED（保持 19f9738…）
cloud consent UI                   = NOT STARTED（非本 slice）
live ModelScope call               = NOT AUTHORIZED（NO live network）
production AI extraction UI trigger = NOT STARTED / NOT AUTHORIZED
Candidate review UI                = NOT STARTED
Accept / Edit / Reject（C3）       = NOT STARTED / NOT AUTHORIZED
DeviceProfile write                = NOT STARTED / NOT AUTHORIZED
Candidate persistence              = NOT STARTED（v1 仍 SESSION-ONLY）
C3                                 = NOT STARTED / NOT AUTHORIZED
M12-D                              = NOT STARTED / NOT AUTHORIZED
canonical package                  = NOT CREATED
tag / push / release               = NOT DONE
```

### 68.4 与 §66 冻结契约（H1–H10）的关系

本节**不新增**产品语义裁定，只做**授权 + 范围**记录；本切片的每一条纪律均为 H1–H10 的
**直接适用**，而非新冻结：

| 纪律（§68.2） | 依据 |
| --- | --- |
| provider-neutral；provider 类型止于 boundary | **H1**（provider-neutral；model id = configuration） |
| request 只带 extraction payload；测试须证明无多余内容 | **H2**（上传范围 = selected doc canonical extracted text） |
| credential = process env only；fake transport 不需 credential plumbing | **H3** |
| Candidate SESSION-ONLY；无 provider persistence | **H4** |
| 无 numeric confidence 进入 Candidate | **H5** |
| 本地重算 location；未验证 excerpt ⇒ 非 valid Candidate | **H6** |
| 只产 PendingReview | **H7** |
| fail-closed parser；schema + evidence 双重验证；prompt wording 属实现细节 | **H8** |
| 本切片仍 NO QML required | **H9** |
| failure 原子；不为此测试发明 broad controller | **H10** |

**未修改**：§66.4 的 `AMBIGUOUS EVIDENCE MATCH POLICY = DEFERRED / NOT IMPLEMENTED /
NOT GUESSED` 依然有效；本切片不发明歧义策略。

### 68.5 起点 = SESSION M 已接受 foundation（Git 实测恢复，不得重设计）

```text
SESSION_M_FREEZE_DOCS_COMMIT     = cf68d6823e7f5640e658ca5728191e90729a3f3a
                                   「M12: freeze C2 candidate extraction contract」
SESSION_M_BEHAVIOR_COMMIT        = d899e55593cfc29519779af48bd01ecb998a18b5
                                   「M12: add evidence-backed AI candidate foundation」
SESSION_M_ARCHIVE_DOCS_COMMIT    = 91ac7a2df8ed033647d71107692f5b17a14cae38
                                   「M12: archive C2 candidate foundation slice」
（git merge-base --is-ancestor d899e55 HEAD ⇒ rc = 0，实测）

本切片 MUST 复用（不得因「另一形状更好看」而重设计）：
  CANDIDATE DOMAIN OWNER      = src/core/candidate/CandidateExtraction.h（Zero-Qt）
  PROVIDER-NEUTRAL PROPOSAL   = struct CandidateProposal
                                { target, proposedValue, evidenceExcerpt, locationHint }
  PROVIDER SEAM               = class ICandidateProposalProvider
                                { propose(const ManualDocument&, std::string_view) }
  EVIDENCE TYPE               = struct CandidateEvidence
                                { documentId, contentHash, textStart, textEnd, excerpt }
  EVIDENCE VALIDATOR          = extractProfileFieldCandidates() 内的 locateUniqueExcerpt
                                （exact / UNIQUE；0 次或 ≥2 次一律拒绝）
  SUPPORTED FIRST-SLICE FIELD = kC2FirstSliceProfileField = ProfileFieldTarget::Manufacturer
  CANDIDATE STATE             = CandidateLifecycleState::PendingReview（唯一值）
  CANDIDATE STORAGE / LIFETIME= SESSION-ONLY / in-memory（无持久化句柄）
  SESSION M TEST TARGET       = modbuslens_candidate_extraction_tests / ctest `candidate_extraction`
  SESSION M CMAKE OWNER       = 顶层 CMakeLists.txt（modbuslens_core + 独立测试 target）
```

**注意**：SESSION M 的 `CandidateProposal` 已含 `locationHint`（刻意 UNTRUSTED）；本切片
的 provider adapter 产出该 provider-neutral proposal 时，**不得**改变其语义，也不得让
`locationHint` 进入 `CandidateEvidence`（H6 行为已由 C2-A06 证明）。

### 68.6 状态

```text
M12-C C1b                        = COMPLETE / HUMAN ACCEPTED（未变）
M12-C                            = IN PROGRESS
M12-C C2                         = IN PROGRESS
C2 Candidate/Evidence foundation = IMPLEMENTED / AUTOMATED PASS（§67，未变）
C2 SECOND SLICE                  = AUTHORIZED（本节）· IMPLEMENTATION = NOT STARTED
verified LKGC                    = 19f9738c980e0a8a31b557c346fb50a4af711cab（**UNCHANGED**）
C3 · M12-D                       = NOT STARTED / NOT AUTHORIZED
canonical package                = NOT CREATED
REAL MODBUS HARDWARE             = NOT VERIFIED
```

**本节动作边界（Session N · 授权归档 · docs-only）**：仅归档 Human 第二切片授权 + 范围 +
explicit deferred + SESSION M 起点事实；未 build · 未 test · 未改产品代码 ·
未推进 LKGC · 未开始 C3 / M12-D · 未创建 canonical package · 未 push / 未 tag / 未 amend。

---

## 69. M12-C C2 — SECOND SLICE ARCHIVE（PROVIDER ADAPTER + STRICT PROVIDER RESPONSE CONTRACT）（2026-09-29 · Session N · behavior + docs）

> 性质：**append-only 归档**。本节记录 C2 第二个切片的实际交付与全部实测证据。
> §68（授权冻结）保持不变；本节只新增。

### 69.0 两个提交

```text
AUTHORIZATION DOCS COMMIT = 989070be13a4d2246f8180523947c11b148fce26
                            「M12: freeze C2 provider adapter slice」
                            （4 docs / +231 −3，docs-only，NO AMEND）
BEHAVIOR COMMIT           = 19bb9cf38a9d0128c0590045f6b1ddf29069c1a5
                            「M12: add strict ModelScope candidate adapter」
                            （9 files / +1701 −0，NO AMEND）
                            CMakeLists.txt · src/core/candidate/CandidateExtraction.{h,cpp} ·
                            src/core/candidate/ProviderExtractionContract.{h,cpp} ·
                            src/ui/ai/ExtractionTransport.h ·
                            src/ui/ai/ModelScopeCandidateAdapter.{h,cpp} ·
                            tests/test_candidate_adapter.cpp
```

**注意**：行为提交**不是** LKGC；verified LKGC 仍为 `19f9738…`（见 §69.20）。

### 69.1 SOURCE AUDIT — 既有 AI / 网络栈（§7 结论）

```text
EXISTING MODELSCOPE CLIENT = src/ui/ai/ModelScopeDiagnosisClient.{h,cpp}（M6，Diagnosis-specific）
                              + src/ui/agent/ModelScopeAgentClient.{h,cpp}（Agent 用途）
EXISTING TRANSPORT OWNER   = 两者各自持有 QNetworkAccessManager（new QNetworkAccessManager(this)），
                             单飞 + QNetworkReply + 单一 QTimer 超时 owner
EXISTING CREDENTIAL SOURCE = process environment only：MODELSCOPE_API_KEY（必需）+
                             MODBUSLENS_MODELSCOPE_MODEL（可选 override），由
                             buildModelScopeProductionConfig() 读取；无 QSettings / 无持久化
EXISTING JSON PARSER STYLE = QJsonDocument::fromJson → 严格 isObject + choices isArray →
                             取 message.content 非空 string；error.message 截断 200
EXISTING TEST SEAM         = tests/fake_chat_completions_server.{h,cpp}（localhost fake，
                             capture request + scripted response）+
                             ModelScopeDiagnosisClient::configure(config) 的 endpoint 注入
REUSABLE PARTS             = ① Chat Completions wire envelope 形状
                             （{model,messages,stream:false,max_tokens} / choices[]→message.content）
                             ② HTTP status → 语义类别的映射意图（2xx 成功 vs 其余失败）
                             ③ fake transport 的 capture/script/count 模式
DIAGNOSIS-SPECIFIC PARTS THAT MUST NOT LEAK INTO C2 =
                             AiDiagnosisErrorCode（12 值）· AiAbortReason · ModelScopeClientConfig ·
                             DiagnosisPromptBuilder · diagnosisSucceeded/Failed signals ·
                             DiagnosisContext · RuleBasedDiagnosis
```

**复用决策**：**不**复用 `ModelScopeDiagnosisClient` 类本身（其 error code / signal / prompt 均为
Diagnosis-specific）；**复用**其 wire contract 形状与 fake 模式。**未修改 M6 任何文件**
（M6 diagnosis / agent / fake server / test_ai_client 全部零 diff，且 ai_client 测试仍 PASS）。

### 69.2 ModelScope WIRE CONTRACT 来源（§8 纪律）

```text
判定 = 复用仓库既有「已接受」的 wire contract，**未发明**新 envelope / 未发明新 wire 字段。
repo 内权威出处（实测 grep）：
  docs/tasks/T011-ai-diagnosis.md:387-388 · :425
    Base     = https://api-inference.modelscope.cn/v1
    Endpoint = https://api-inference.modelscope.cn/v1/chat/completions
    Headers  = Authorization: Bearer <MODELSCOPE_API_KEY> + Content-Type: application/json
    Body     = { model, messages:[{system},{user}], stream:false, max_tokens:768 }
  docs/08_KNOWLEDGE_OWNERSHIP.md:1405（同一契约的二次记录）
  src/ui/ai/ModelScopeDiagnosisClient.cpp:14-19（常量）· :257-280（choices[]→message.content）
外部交叉核对（documentation research only；**未**发任何推理请求 / 未用 token / 未花钱）：
  ModelScope API-Inference 官方文档（modelscope.cn/docs/model-service/API-Inference/intro）
  确认 base_url = https://api-inference.modelscope.cn/v1/ 、OpenAI-compatible Chat Completions、
  model = ModelScope Model-Id、messages 含 system/user、非流式响应经 choices[0].message.content。
本切片**新增**的仅是「content 内容本身须满足的 strict schema」（§69.6），
属于 C2 自有契约而非新 wire 行为。
```

### 69.3 架构（实际落地）

```text
canonical Manual text
  → core::ExtractionRequest（provider-neutral；src/core/candidate/ProviderExtractionContract.h）
  → ModelScopeCandidateAdapter（provider adapter boundary；src/ui/ai/）
  → IExtractionTransport（最小 transport seam；src/ui/ai/ExtractionTransport.h）
  → deterministic FakeExtractionTransport（tests 内）
  → extractModelScopeResponseContent()（wire envelope → assistant content）
  → parseStrictCandidateProposals()（strict、fail-closed、atomic）
  → core::CandidateProposal（provider-neutral，SESSION M 既有类型）
  → core::extractProfileFieldCandidates()（SESSION M 本地确定性 Evidence validation）
  → PendingReview Candidate
```

**边界落实为可审计事实**：`ModelScopeCandidateAdapter` 实现 SESSION M 的
`ICandidateProposalProvider`，因此第二个切片**直接复用**既有本地 Evidence 验证路径；
provider-specific 形状（endpoint 由调用方配置、model id、wire body、HTTP-like status、
transport 对象）全部只存在于 `src/ui/ai/`。

### 69.4 PROVIDER-NEUTRAL REQUEST CONTRACT（§10）

```text
struct ExtractionRequest { targetFieldToken; documentTypeToken; canonicalExtractedText; };
buildC2FirstSliceExtractionRequest(document, canonicalText) 为纯函数，只复制 extraction payload。

**结构性最小化**：documentId / contentHash / originalFileName / originalPath / byteSize /
extraction bookkeeping **根本没有字段**，因此不可能被序列化到 provider。
测试 N15 另以捕获到的 wire body 实证：仅含 canonical extracted text + target token，
不含 documentId / contentHash / filename / original path / apiKey / Authorization / Bearer /
register / transaction / profileId。
```

### 69.5 PROMPT CONTRACT（§11）

```text
冻结的是 semantic contract（H8），wording 属 adapter implementation detail。
adapter 内构造 system instruction（只用 supplied text / 不猜 / 每个 fact 必须附 exact excerpt /
不支持则省略 / 不声称 verified）与 user prompt（target token + strict 输出 schema 提示 + manual text）。
测试**不**做 full-string equality；N15 采语义断言（首行 / 证据行 / 末行均被承载 + target token 存在）。
```

### 69.6 STRICT PROVIDER RESPONSE CONTRACT（§12）

```text
provider content 的 strict schema（C2 v1）：
  { "proposals": [ { "target": <frozen token>, "value": <非空 string>, "evidence": <非空 string> } ] }

fail-closed（任一违反 ⇒ ok=false 且 **零** proposal）：
  · content 非法 JSON                      → MalformedResponse
  · 顶层非 object（如数组/标量）            → SchemaViolation
  · 顶层或元素出现未知 / 多余成员            → SchemaViolation
  · 缺失必需成员 / 必需成员 JSON 类型错误    → SchemaViolation（**无类型强转**：number 绝不读作 string）
  · 空 target / value / evidence            → SchemaViolation
  · 未知 target token                       → SchemaViolation（provider 不得扩宽冻结契约）
  · numeric confidence / score / probability 等任何多余数值成员 → SchemaViolation（H5）
  · 重复成员名（同一 object 内）             → SchemaViolation（"哪一个胜出"不许歧义）
合法特例：proposals 为空数组 = 「source 不支持任何事实」，ok=true 且零 proposal（不是错误）。
```

**实现**：为保持 `src/core` 的 Zero-Qt 与 target 的零网络能力，strict JSON reader 为
**自写的最小递归下降解析器**（object/array/string/number/bool/null；严格 RFC 8259 number
语法；拒绝孤立代理项 / 非法转义 / 原始控制字符 / 尾随内容），**未**引入任何第三方或 Qt 依赖。

### 69.7 PARSER GRANULARITY — RESPONSE ATOMICITY（§13）

```text
选定并记录 = **one provider extraction response is parsed atomically**（C2 v1 preferred）。
实现：先构建完整 accepted 集合，任一违反即整体丢弃并返回 failure —— **无部分接受混合物**。
这属于 provider-response atomicity；**未修改** SESSION M local Evidence semantics
（SESSION M 的 per-proposal 拒绝与 refusedProposalCount 语义原样保留，N07 part B 实证两层分工）。
```

### 69.8 RAW RESPONSE BOUNDARY（§14）

```text
raw provider body 只存在于 adapter / parser 的调用作用域内（`exchange.responseBody` 局部量），
不进入 CandidateProposal / CandidateEvidence / DeviceProfile / manual metadata / cache / docs / log。
编译期约束：static_assert(!hasRawResponseMember<ProviderExtractionResult>) 与
            static_assert(!hasRawResponseMember<ProfileFieldCandidate>)。
运行期约束：N12 —— envelope 含唯一标记 "chatcmpl-1"，其不出现在返回值任何字段。
```

### 69.9 TRANSPORT SEAM + FAKE TRANSPORT（§15）

```text
最小 seam = IExtractionTransport::send(requestBody) → Exchange{responseBody, transportOk, statusOk}。
**未**建 generalized HTTP framework；**未**复用 M6 的 localhost fake server（其 seam 是
Diagnosis-specific 具体客户端 + signal/error-code 语义，复用会把 C2 耦合到 diagnosis 域）。
确定性 fake = tests 内 FakeExtractionTransport：capture outbound request · 确定性成功响应 ·
确定性 transport failure · 确定性 non-success status · 计数调用。
**结构性保证**：新测试 target **不链接 Qt6::Network**（CMake 实测），产品路径无法经它发出真实请求。
**Production UI 未被接线**（无 QML / 无 controller / 无 main.cpp 改动；diff 实测为零）。
```

### 69.10 REAL RED（无 compile-fail RED，§16）

```text
做法：先落 compile-safe scaffold（公开 API 齐备、链接成功，行为确定性 inert：
      信封提取返回空、strict parser fail-closed、adapter 报 failure），再跑 N01–N18。
command  : build/release/modbuslens_candidate_adapter_tests.exe -o "$TEMP/sn_red.txt,txt"
exit code: 17
结果     : Totals: 3 passed, 17 failed, 0 skipped, 0 blacklisted, 313ms
首选断言 : n01_validProviderResponseYieldsPendingReviewCandidate()
           FAIL! Actual (transport.calls) = 0 / Expected = 1
性质     : build 与 link 均成功、test process 正常退出并产出报告 ⇒ **纯语义断言 RED**；
           **非** compile failure / link failure / DLL failure / crash / grep miss。
（中间过程如实记录一次修正：N18 断言把 QStringList 误写为 QString ⇒ 那是**编译失败**，
  按 §16 不计为 RED；修正后才取得上述真实 RED。链接期一次 ProfileStore 符号缺失同样
  属链接失败、不计为 RED，补齐源后重取。）
```

### 69.11 GREEN（§20 最小实现）

```text
consume provider-neutral request → build ModelScope wire body（内部）→ transport.exchange →
extractModelScopeResponseContent() → parseStrictCandidateProposals() → 返回 provider-neutral proposals；
失败路径一律零 proposal + 确定性 failure taxonomy（transport_error / provider_rejected_status /
malformed_response / schema_violation）。

GREEN（首次）: Totals: 19 passed, 1 failed —— n15 断言写得过于字面（正文含换行，JSON 序列化后为 \n）。
修正 = 把 n15 改为**语义断言**（首行 / 证据行 / 末行均承载 + target token 存在），非放宽标准。
GREEN（最终）: Totals: 20 passed, 0 failed, exit 0（298ms / 307ms 两次复现一致）。
```

### 69.12 N01–N18 矩阵（实测）

| case | 断言 | 结果 |
| --- | --- | --- |
| N01 | 合法 synthetic ModelScope 响应 + strict 合法 content + excerpt 存在 ⇒ 1 个 PendingReview Candidate；transport 恰好被调用 1 次 | PASS |
| N02 | 结构化 content 非法 JSON ⇒ MalformedResponse、零 proposal / 零 Candidate | PASS |
| N03 | 顶层类型错误（数组）⇒ SchemaViolation | PASS |
| N04 | 缺必需成员（value）⇒ SchemaViolation | PASS |
| N05 | 成员类型错误（value=42）⇒ SchemaViolation，**无强转** | PASS |
| N06 | 未知/多余 schema 成员 ⇒ SchemaViolation（strict 而非 permissive） | PASS |
| N07 | 未知 target token ⇒ SchemaViolation；**且**合法但非 first-slice 的 target 由 SESSION M per-proposal 拒绝（两层分工保留） | PASS |
| N08 | 空 evidence ⇒ SchemaViolation | PASS |
| N09 | **schema 合法 + excerpt 不存在 ⇒ 零 Candidate**（provider 被真实消费，refusedProposalCount=1） | PASS |
| N10 | **NOT APPLICABLE as a "bogus hint accepted" case**：本切片 schema 刻意**不含** provider location；带 location 成员者按未知成员被拒（fail-closed），故 provider 位置在最结构层面无法成为 authority | PASS（N/A 形态；见说明） |
| N11 | numeric confidence 成员 ⇒ SchemaViolation、零 Candidate；且编译期 static_assert 保证 Candidate 类型无 confidence 成员 | PASS |
| N12 | raw response 不被保留（标记不出现在返回字段；编译期无 raw 成员） | PASS |
| N13 | transport error ⇒ TransportError、零 Candidate | PASS |
| N14 | non-success provider status ⇒ ProviderRejectedStatus、零 Candidate | PASS |
| N15 | outbound payload 最小化（含 intended text；不含本地标识符 / 路径 / 凭据 / 设备与事务真值） | PASS |
| N16 | 驱动来自 canonical text（复用 SESSION M C2-A12 的结论 + 本切片新增：原文件路径不存在仍成功、wire body 无路径） | PASS |
| N17 | determinism（两次同输入 ⇒ 语义相等 + 相同 wire body） | PASS |
| N18 | DeviceProfile / ProfileStore 零 diff（隔离 root 下 tree 快照不变） | PASS |

**N10 说明（如实）**：SESSION N schema 故意不含 provider location，故「给 bogus location 而
exact excerpt 有效 ⇒ 最终 location 仍本地派生」这一形态**不被构造**；本切片以更强的结构性命题
覆盖——provider 一旦携带 location 即因 strict unknown-field 规则被整响应拒绝。SESSION M 的
C2-A06 已另行证明 `CandidateProposal::locationHint` 被本地验证器忽略。**未**扩 schema。

### 69.13 中心验收 invariant 的独立证明（§18）

```text
N09（schema 合法 + 伪造 excerpt）+ N01（对照：合法 excerpt ⇒ 1 Candidate）共同证明：
  valid JSON ≠ schema-valid ≠ 本地证据成立 ≠ PendingReview Candidate ≠ verified truth。
证据强度：N09 同时断言 transport.calls == 1（provider **确实**被消费）与
          candidates.size() == 0 且 refusedProposalCount == 1（**仍然**不产出）。
```

### 69.14 NEGATIVE CONTROL / MUTATION（§19）

```text
precise mutation：在 hasOnlyMembers() 内，把「未知成员 ⇒ return false」改为
                  「未知成员 ⇒ 忽略（continue）」——即让 strict parser 变成 permissive drift。
预期     ：N06 REAL RED。
实测     ：exit 3；Totals: 17 passed, 3 failed；
           FAIL! n06_unknownExtraSchemaFieldIsRejected
           FAIL! n10_providerLocationIsNotAuthority   （同一根因：未知成员不再被拒）
           FAIL! n11_numericConfidenceCannotReachACandidate（同一根因）
           ⇒ build / link 正常、test process 正常、**semantic assertion RED**。
precise reverse：精确还原原 `return false;` 分支并删除 mutation 注释
                 （**未用** checkout / restore / reset / stash）。
residue  ：grep -c "NEGATIVE-CONTROL MUTATION" = 0；且与 GREEN 基线**逐字节相同**（diff 空）。
恢复后   ：exit 0；Totals: 20 passed, 0 failed（GREEN 复现）。
说明     ：本 mutation 只针对 **SESSION N 新增逻辑**（strict parser 的 unknown-field 判定），
           **未**复用 SESSION M 的 evidence-mutation 作为本切片非空洞性证明。
```

### 69.15 Targeted regression（§22）

```text
build/release 下 ctest -R "candidate_adapter|candidate_extraction|manual_import|manual_extraction|ai_client"
  26 manual_import                       Passed  15.31 sec
  27 candidate_extraction                Passed   1.44 sec
  28 candidate_adapter                   Passed   0.84 sec   （本轮新增）
  29 manual_extraction                   Passed   3.39 sec
  30 manual_import_pdf_docx              Passed   8.67 sec
  41 ai_client                           Passed   1.58 sec   （M6 未改，作为无回归对照）
  57 qml_manual_import_check             Passed   4.33 sec   （regex 命中）
  59 qml_manual_import_check_windows     Passed   6.23 sec   （regex 命中）
100% tests passed, 0 tests failed out of 8 · exit code 0 · 42.49 sec
（DeviceProfile / profile_semantic / ui_bridge 等因未触及受保护面，按 §22 由 full regression 覆盖。）
```

### 69.16 Release full regression（§23）

```text
build : cmake --build build/release ⇒ 81/81 objects 全部链接成功
ctest -N : Total Tests: 59   （前次 58；+1 = candidate_adapter；其插入使 #28 之后的编号整体 +1）
结果  : 100% tests passed, 0 tests failed out of 59
        #27 candidate_extraction  Passed   1.67 sec
        #28 candidate_adapter     Passed   0.85 sec
        #31 deployment_startup_check Passed 234.39 sec
        Total Test time (real) = 898.60 sec
ctest exit code = 0
Debug：新增 core 源在当前 canonical compiler（MinGW GCC 13.1.0 / Debug）下**编译成功**
       （build/debug/CMakeFiles/modbuslens_core.dir/src/core/candidate/ProviderExtractionContract.cpp.obj，
       154699 B；CandidateExtraction.cpp.obj 同轮生成）；
       归档步骤命中**既有环境问题** `ranlib: could not create temporary file whilst writing archive`，
       故 Debug full 未运行（Release = canonical gate；与 SESSION M 记录一致）。
       新测试 target 由 `if(BUILD_TESTING)` 保护、跨 config 形状一致，无 config-sensitive 逻辑。
```

### 69.17 Static security / AI boundary audit（§24）

```text
new/changed files 静态审计（实测 grep）：
  QNetworkAccessManager / QNetworkRequest / QNetworkReply / QtNetwork     = 0（全部新文件）
  http(s):// / QUrl / Bearer / apiKey= / MODELSCOPE_API_KEY /
    MODBUSLENS_MODELSCOPE_MODEL / getenv / qEnvironmentVariable            = 0（实现文件）
  QSettings / QStandardPaths / QFile / QDir / fopen / std::filesystem      = 0（core 与 adapter 实现）
  'Authorization' 字样：仅 1 处，在 ExtractionTransport.h 的**文档注释**中（说明实现在别处）
  src/core 内 Qt include 数                                                = 0（Zero-Qt 保持）
  src/core 内 'ModelScope' 字样 = 2 处，**均为注释**（H1 禁止性/边界说明；其中 1 处为 SESSION M 既有）
     ⇒ core 无 provider 类型 / endpoint / model id / envelope / HTTP status / transport object
  密钥面：无 token 字面量 / 无 API key / 无 fixture 内凭据（N15 反向断言其不存在）
  无 raw provider response 持久化 · 无 Candidate 持久化 · 无 DeviceProfile 写入 ·
  无 Accept/Edit/Reject · 无 numeric confidence 进入 Candidate（编译期 + 运行期）·
  无 production UI 联网接线 · 无原始 PDF/DOCX 字节上传路径 · 无其他 manual 上传 ·
  无 transaction / raw TX·RX 上传 · 无 C3 / M12-D 实现
```

### 69.18 Protected-surface zero-diff audit（§25）

```text
git diff（behavior commit 19bb9cf）实际 changed paths：
  M CMakeLists.txt · M src/core/candidate/CandidateExtraction.{h,cpp} ·
  A src/core/candidate/ProviderExtractionContract.{h,cpp} ·
  A src/ui/ai/ExtractionTransport.h · A src/ui/ai/ModelScopeCandidateAdapter.{h,cpp} ·
  A tests/test_candidate_adapter.cpp            （9 files / +1701 −0）

protected paths diff（实测为空）：
  src/core/analysis · src/core/protocol · src/main.cpp · src/ui/qml · src/ui/manual ·
  src/ui/profile · src/core/profile · src/core/manual · scripts ·
  tests/test_manual_import.cpp · tests/test_manual_extraction.cpp ·
  tests/test_manual_import_pdf_docx.cpp ·
  src/ui/ai/ModelScopeDiagnosisClient.{h,cpp} · src/ui/ai/DiagnosisPromptBuilder.{h,cpp} ·
  tests/fake_chat_completions_server.{h,cpp} · tests/test_ai_client.cpp
git diff --check = 0；无无关格式化 churn；无意外大删除（全部为新增）；无 generated build artifact；
无 docs 混入 behavior commit；无 QML 改动；无 package artifact。
```

### 69.19 明确未做 / 仍然 DEFERRED

```text
live ModelScope inference（本 session 未发任何真实网络请求）   = NOT RUN / NOT VERIFIED
production AI extraction UI trigger（未接线）                  = NOT STARTED
cloud consent UI                                               = NOT STARTED
credential plumbing（本切片测试无需）                          = NOT IMPLEMENTED
Candidate review UI / Candidate display                        = NOT STARTED
Accept / Edit / Reject（C3）                                   = NOT STARTED / NOT AUTHORIZED
DeviceProfile write / Candidate persistence                    = NOT STARTED
numeric confidence / retry                                     = NOT STARTED
provider location authority                                    = schema 不含 provider location（未扩 schema）
AMBIGUOUS EVIDENCE MATCH POLICY                                = DEFERRED / NOT IMPLEMENTED / NOT GUESSED
RETRY / REPLACEMENT ATOMICITY（H10 的 orchestrator 层）        = DEFERRED TO ORCHESTRATION SLICE
   （本切片不为此发明 broad controller；parser/adapter 本身无 destructive side effect：无 I/O、
     无 store 写入，N18 实证 ProfileStore 零 diff）
C3 · M12-D                                                     = NOT STARTED / NOT AUTHORIZED
canonical package / tag / push / release                       = NOT DONE
```

### 69.20 状态

```text
M12-C C1b                        = COMPLETE / HUMAN ACCEPTED（未变）
M12-C                            = IN PROGRESS
M12-C C2                         = IN PROGRESS
C2 Candidate/Evidence foundation = IMPLEMENTED / AUTOMATED PASS（§67，未变）
C2 ModelScope adapter + strict parser = IMPLEMENTED / AUTOMATED PASS（本节；N01–N18 + mutation + 59/59）
C2 Human acceptance              = PENDING
Live ModelScope inference        = NOT RUN / NOT VERIFIED
Cloud consent UI                 = NOT STARTED
Production AI extraction UI trigger = NOT STARTED
C2 Candidate display             = NOT STARTED
C3 · M12-D                       = NOT STARTED / NOT AUTHORIZED
verified LKGC                    = 19f9738c980e0a8a31b557c346fb50a4af711cab（**UNCHANGED**）
canonical package                = NOT CREATED
REAL MODBUS HARDWARE             = NOT VERIFIED
```

**注意**：本行为提交**不是** LKGC；verified LKGC 仍为 `19f9738…`，推进必须 Human 明确授权。
**SESSION N = COMPLETE — STOP**（未开始 C2 第三个切片）。

---

## 70. M12-C C2 THIRD SLICE — ORCHESTRATION / CONSENT / CANDIDATE DISPLAY AUTHORIZATION FREEZE（2026-09-29 · Session O · docs-only）

> 性质：**append-only 归档**。本节把 Human 在 SESSION O §0 对 C2 **第三个切片**的显式授权
> （start + 5 项目标）与**范围边界（in scope / explicit deferred）**冻结为可审计记录，
> 并在**写任何产品代码之前**落库（SESSION O §5 顺序要求）。
> **禁止**据此改写 §66 / §67 / §68 / §69（历史原貌保持）；本节只新增。

### 70.0 Human 授权原文（逐字归档）

```text
Human（SESSION O §0 HUMAN AUTHORIZATION）：

  「M12-C C2 THIRD SLICE = START」

本 slice 精确范围（5 项）：
  1. Production extraction orchestration
  2. Cloud-consent gate
  3. PendingReview Candidate display
  4. deterministic fake provider 完成自动化
  5. Human UI acceptance preparation

本 slice 明确禁止：
  真实 ModelScope inference · 真实网络请求 · Human Accept/Edit/Reject ·
  DeviceProfile write · Candidate persistence · numeric confidence ·
  C3 · M12-D · canonical package · release/tag/push · verified LKGC advancement

verified LKGC MUST remain 19f9738c980e0a8a31b557c346fb50a4af711cab
```

### 70.1 起始基线（实测）+ 与 prompt 假设的差异（如实记录）

```text
HEAD                 = b3ff697c22f2a21e3c3ebec4c38ea7b3a9696d59
porcelain -uall      = ?? _ctx.py / ?? _dump.py（仅此两项）
tracked diff         = 空 · index(cached) = 空 · git diff --check rc = 0
git tag --list       = v1.0.0（唯一）
git ls-files build   = 0
verified LKGC        = 19f9738c980e0a8a31b557c346fb50a4af711cab（未推进）

SESSION M 三提交（Git 实测恢复）：
  cf68d6823e7f5640e658ca5728191e90729a3f3a「M12: freeze C2 candidate extraction contract」
  d899e55593cfc29519779af48bd01ecb998a18b5「M12: add evidence-backed AI candidate foundation」
  91ac7a2df8ed033647d71107692f5b17a14cae38「M12: archive C2 candidate foundation slice」
SESSION N 三提交（Git 实测恢复）：
  989070be13a4d2246f8180523947c11b148fce26「M12: freeze C2 provider adapter slice」
  19bb9cf38a9d0128c0590045f6b1ddf29069c1a5「M12: add strict ModelScope candidate adapter」
  b3ff697c22f2a21e3c3ebec4c38ea7b3a9696d59「M12: archive C2 provider adapter slice」

**§1 与仓库真相的差异（按 §2「repository truth > prompt assumptions」处理）**：
  SESSION O §1 写「SESSION N Release full: 58/58 PASS」。
  仓库真相（§69.16 / PROJECT_STATUS / BACKLOG 实测记录）= **59/59 PASS**
  （N 由 58 → 59，新增 CTest `candidate_adapter`；build 81/81；898.60s；exit 0）。
  ⇒ 以 **59/59** 为 canonical；58/58 视为陈旧假设，本节记录差异，不改写历史。
```

### 70.2 IN SCOPE（本切片授权交付）

```text
① Production extraction orchestration（最小生产编排层，契合既有架构）
② Cloud-consent gate（默认 NO UPLOAD；上传前需显式同意）
③ PendingReview Candidate display（仅展示本地已验证的 PendingReview Candidate）
④ deterministic fake provider 完成自动化（自动化测试**只**用确定性 fake）
⑤ Human UI acceptance preparation（准备 candidate 与自动化证据；**不**声称 Human visual PASS）
```

### 70.3 明确 DEFERRED / 禁止（本 session 不得执行）

```text
live ModelScope inference          = NOT AUTHORIZED（零真实推理）
真实网络请求                        = NOT AUTHORIZED
Human Accept / Edit / Reject（C3）  = NOT STARTED / NOT AUTHORIZED
verified DeviceProfile write        = NOT AUTHORIZED
Candidate persistence               = NOT AUTHORIZED（v1 仍 SESSION-ONLY）
numeric confidence                  = NOT AUTHORIZED
C3                                  = NOT STARTED / NOT AUTHORIZED
M12-D                               = NOT STARTED / NOT AUTHORIZED
canonical package / release / tag / push = NOT DONE
verified LKGC advancement           = NOT AUTHORIZED（保持 19f9738…）
```

### 70.4 恢复的既有架构（SESSION M/N 实测，禁止重设计）

```text
MANUAL DOCUMENT OWNER      = core::ManualDocument（src/core/manual/ManualDocument.h）
CANONICAL TEXT OWNER       = ui::ManualStore::loadText(contentHash, bool* ok)
                             （src/ui/manual/ManualStore.h:97-98；ok=false = 缓存缺失，绝不静默空串）
                             ⚠ previewText() 对 PDF **含展示页眉**（buildPdfPreview），
                               不是 canonical truth ⇒ 编排层 MUST 用 loadText，不得用 previewText
SELECTED DOCUMENT OWNER    = ui::ManualImportController（src/ui/manual/ManualImportController.h:37）
                             selectedIndex() / selectedDocument()(QVariantMap) /
                             Q_INVOKABLE selectDocument(int) / clearSelection()
                             内部真身 = std::vector<core::ManualDocument> m_documents + m_selectedIndex
CANDIDATE DOMAIN OWNER     = src/core/candidate/CandidateExtraction.{h,cpp}
                             + src/core/candidate/ProviderExtractionContract.{h,cpp}（Zero-Qt）
CANDIDATE SET/LIFETIME OWNER = **不存在**（本切片新建；v1 = SESSION-ONLY in-memory）
EVIDENCE VALIDATOR         = core::extractProfileFieldCandidates()（内部 locateUniqueExcerpt，
                             exact + UNIQUE；0 次或 ≥2 次一律拒绝 ⇒ 本地重算 location）
PROVIDER-NEUTRAL REQUEST   = core::ExtractionRequest + core::buildC2FirstSliceExtractionRequest()
PROVIDER-NEUTRAL PROPOSAL  = core::CandidateProposal + core::ICandidateProposalProvider
MODELSCOPE ADAPTER         = ui::ModelScopeCandidateAdapter
                             （src/ui/ai/ModelScopeCandidateAdapter.h:102，实现 ICandidateProposalProvider）
TRANSPORT SEAM             = ui::IExtractionTransport（src/ui/ai/ExtractionTransport.h）
STRICT PARSER              = ui::parseStrictCandidateProposals() + ui::extractModelScopeResponseContent()
CURRENT UI CONTROLLER      = ManualImportController / ProfileController / ActiveProfileController /
                             AnalysisController（全仓仅 4 个 QML_ELEMENT；**无** setContextProperty 注册，
                             靠 CMakeLists.txt:208 qt_add_qml_module + qmltyperegistrar）
DEVICE PROFILE PAGE OWNER  = src/ui/qml/pages/DeviceProfilePage.qml
                             （root id=deviceProfileRoot，objectName=deviceProfileWorkspace；
                              Main.qml StackLayout 第 6 项 index 5；rail navItem_5）
CURRENT MANUAL IMPORT UI OWNER = DeviceProfilePage.qml:698-924（manualImportHost / manualImportCard /
                             manualImportBody / manualDocumentList / manualDocColumn / manualPreview）
                             ⇒ Manual Import **不是**独立组件，是同一 Device Profile workspace 内的区域
CURRENT TEST INJECTION PATTERN = ① C++ 单元测试：tests/*.cpp + 自有 fake double
                             （如 tests/test_candidate_adapter.cpp 的 FakeExtractionTransport）
                             ② QML 门禁：src/main.cpp 内 `--qml-*` flag 分派（runManualImportCheck:16292 等），
                             用 findNamedItemRecursive（**visual childItems 树**，因 Repeater delegate
                             只在视觉树可达）+ requireSized / requireInsideWindow / 比例断言 /
                             clickNamed + steps(QTimer::singleShot, settleMs=60)
                             ③ 持久化隔离：ManualStore::setManagedRootOverride + ProfileStore::setManagedRootOverride
```

**中心 invariant 复核（SESSION N N09 仍存在且 PASS）**：schema-valid provider proposal + false /
unverifiable evidence ⇒ **0** PendingReview Candidate（由 SESSION M 的 `locateUniqueExcerpt` 拒绝，
`refusedProposalCount` 计数）。本切片**不得**绕过该路径。

### 70.5 状态

```text
M12-C C1b                        = COMPLETE / HUMAN ACCEPTED（未变）
M12-C                            = IN PROGRESS
M12-C C2                         = IN PROGRESS
C2 Candidate/Evidence foundation = IMPLEMENTED / AUTOMATED PASS（§67，未变）
C2 ModelScope adapter + strict parser = IMPLEMENTED / AUTOMATED PASS（§69，未变）
C2 THIRD SLICE                   = AUTHORIZED（本节）· IMPLEMENTATION = NOT STARTED
verified LKGC                    = 19f9738c980e0a8a31b557c346fb50a4af711cab（**UNCHANGED**）
C3 · M12-D                       = NOT STARTED / NOT AUTHORIZED
canonical package                = NOT CREATED
REAL MODBUS HARDWARE             = NOT VERIFIED
```

**本节动作边界（Session O · 授权归档 · docs-only）**：仅归档 Human 第三切片授权 + 范围 +
explicit deferred + 恢复的 M/N 架构 + §1 差异记录；未 build · 未 test · 未改产品代码 ·
未推进 LKGC · 未开始 C3 / M12-D · 未创建 canonical package · 未 push / 未 tag / 未 amend。

---

## 71. M12-C C2 — THIRD SLICE ARCHIVE（CONSENT-GATED ORCHESTRATION + PENDINGREVIEW CANDIDATE DISPLAY）（2026-09-29 · Session O · behavior + docs）

> 性质：**append-only 归档**。§70（授权冻结）保持原貌；本节只新增。

### 71.0 两个提交

```text
AUTHORIZATION DOCS COMMIT = 0d339c2be8659e7685406eeb8f443c68bd3ce1c3
                            「M12: freeze C2 orchestration and consent slice」（4 docs / +211 −1，docs-only）
BEHAVIOR COMMIT           = 309ba15a72ffd463b38ade32f0b23547b0bdf04c
                            「M12: add consent-gated AI candidate orchestration」
                            （9 files / +1585 −0，NO AMEND）
                            CMakeLists.txt · src/ui/candidate/{CandidateExtractionRunner.h,
                            ModelScopeCandidateRunner.h/.cpp, CandidateExtractionController.h/.cpp} ·
                            src/ui/qml/Main.qml · src/ui/qml/pages/DeviceProfilePage.qml ·
                            tests/test_candidate_orchestration.cpp
```

**不是** LKGC；verified LKGC 仍为 `19f9738…`（见 §71.13）。

### 71.1 CONSENT CONTRACT（§6 语义，落地实现）

```text
默认 = NO CLOUD UPLOAD。同意范围 = 当前应用会话 × selected ManualDocument identity ×
其当前 content identity（contentHash）。**无磁盘持久化**（内存 vector，无 QSettings/无文件）。
· 同 (documentId, contentHash) 且本会话已授权 ⇒ 后续提取不再弹窗（O04）
· 不同 document ⇒ 需重新同意（O05）
· 同 documentId 但 contentHash 不同 ⇒ 需重新同意（O06）
· Reject/Cancel ⇒ provider 调用数 = 0；候选集 / verified Profile / manual 全部不变；
  不永久持久化，之后的显式尝试可再次询问；无「永远记住」
同意 UI 语义（`consentScopeText`）：发送的是**已抽取文本**、**不上传**原始 PDF/DOCX、
输出仅为**待审核候选**、**不会自动修改已验证设备档案**。exact prose = UI 实现细节。
```

### 71.2 PRODUCTION ORCHESTRATION 状态机（§7）

```text
Idle --requestExtraction()--> [consent?]
   否 --> ConsentRequired --reject--> Idle（零调用、零变更）
                        --grant--> Running
   Running --success(≥1 本地验证候选)--> Succeeded（候选集原子替换）
           --failure / 零候选--------> Failed（旧成功候选集不变）
实现 = src/ui/candidate/CandidateExtractionController（QML_ELEMENT）。
依赖：selected ManualDocument（经 ManualImportController）+ **ManualStore::loadText 的
C1b canonical extracted text**（**不得**用含 PDF 展示页眉的 previewText）+ SESSION N
provider-neutral seam（经 ICandidateExtractionRunner）+ SESSION M/N 本地 Evidence validation。
**不**重读原始 PDF/DOCX，**不**建第二套 manual-text 来源。
```

### 71.3 CANDIDATE-SET ATOMICITY 与失败语义（§8）

```text
成功定义 = transport ok + strict schema ok + **≥1 个经本地 Evidence 验证的候选**。
失败来源（全部保持旧成功候选集不变、不清空、不部分替换、不 mutate）：
  transport failure · provider failure · malformed response · strict schema rejection ·
  **evidence rejection（零候选）** · orchestration error。
只有「完全成功」的新提取才可 **原子替换** 该 document/content 的当前会话候选集。
替换 = 会话内存，无持久化。实现：先在局部构建 replacement，完成后再 move 赋值。
```

### 71.4 STALE-RESULT GUARD（§9）

```text
每次 attempt 一个 generation（`++generation_` + `activeAttempt_` 记录 documentId/contentHash）。
完成时：generation ≠ activeAttempt_.generation ⇒ 丢弃；state ≠ Running ⇒ 丢弃。
不同 document/content 的新请求 **supersede** 进行中的 attempt（新 generation），
旧 completion 被 stale guard 丢弃，**不会**出现在新选中文档的候选中（O12）。
同一 document/content 的重复触发 = single-flight 忽略。
**未**为此外取消或改写任何 Manual 状态（不耦合 C2 到 Diagnosis 域语义）。
```

### 71.5 CANDIDATE DISPLAY（§12）与 C3 边界（§13）

```text
UI 只展示**本地已验证**的 PendingReview Candidate 字段：
  targetField（冻结 token）· proposedValue · evidenceExcerpt · lifecycle("pending_review")
  · documentId · contentHash · textStart/textEnd（**本地重算**的偏移）
**不展示**：numeric confidence · raw provider JSON · raw ModelScope response ·
provider 自报 offset 作为真值 · secret/token · 原始二进制。
PDF 页信息仅在本地已验证且既有可用时展示；TXT/MD/DOCX **不伪造**页信息（本切片未展示页信息）。
C3 边界：**无** Accept / Edit / Reject；`rejectConsent()` 是**同意取消**，不是 Candidate Reject，
且不 mutate 任何候选。UI 两个概念不混同（按钮文案分别为「取消」/「同意并提取」）。
```

### 71.6 QML 放置决策（关键 · 受保护面保全）

```text
既有 C1 门禁 `runManualImportCheck` 的 **stage 4** 遍历 `manualImportCard` 子树并禁止
objectName 含 token：candidate / accept / reject / question / chat / upload / credential /
apikey / network。直接插入会 **REAL RED**（实测：6 个 MANFAIL）。
⇒ 决策：**不改动受保护门禁**，把 AI 卡片做成 `manualImportCard` 的 **同级兄弟**
（同一 `manualImportHost` 内、`manualImportCard` 之外），命名为 `candidateCard`。
`manualImportCard` 内部内容 **零改动**（仅新增 `anchors.rightMargin: 336` 让出横向空间）。
**垂直方向零改动** ⇒ Session J 几何不受影响（实测 §71.7）。
（中途一次误锚点造成的 QML 局部损坏已按 AGENTS.md「精确逆向还原再重做」纪律修复：
 删除损坏行 + 补回被吞掉的根 ColumnLayout 闭合括号，括号平衡最终 = 0，EOL 统一 CRLF 1486/1486。）
```

### 71.7 Session J 几何非回归（§19 · 实测）

```text
gate 直接运行输出（QT_QPA_PLATFORM=offscreen, QT_ASSUME_STDERR_HAS_CONSOLE=1）：
  MAN: stage 4: no AI/Candidate/Q&A controls present
  MAN: stage 7: vertical budget — profile row share=0.420, manual import share=0.439,
                preview viewport=157.0
对比 SESSION M/J 验收基线（row 0.420 / host 0.439 / preview 157.0）⇒ **完全一致**。
（中途用「第三列插入 manualImportBody」方案时实测 host 0.431 / preview 137.0——虽仍过门禁但
 已偏离基线；改为同级兄弟卡片后回到基线值。）
门禁断言未被削弱：rowShare>0.50 fail、hostShare<0.35 fail、preview<120 fail、row/host 不重叠 —— 全部原样保留。
```

### 71.8 REAL RED / GREEN / 负向对照（§15–§17）

```text
（说明：本切片的 C++ 编排 API 已直接实现，故 §16 的首选 RED 与 §17 的同意门负向对照
 由**同一次精确 mutation** 覆盖——两者目标同为「同意门」，非重复计数。）

precise mutation（§17）：把 `if (!isConsentGranted(doc, hash))` 改为 `if (false)`
                         （即绕过同意门，未授权也立即调用 provider）。
实测 RED  ：exit code 6；Totals **16 passed, 6 failed**；
            FAIL! o01_noConsentNoProviderCall  Actual(state)="running" vs Expected="consent_required"
            FAIL! o02 / o03 / o05 / o06 / o13（同一根因）
            ⇒ build/link 正常、test process 正常、**semantic assertion RED**（provider 调用数 > 0 且未授权）。
precise reverse：精确还原原 `if (!isConsentGranted(...))` 分支并删除 mutation 注释
                 （**未用** checkout / restore / reset / stash）。
residue ：grep -c "NEGATIVE-CONTROL MUTATION" = 0；与 GREEN 基线**逐字节相同**（diff 空）。
恢复后  ：exit 0；Totals **22 passed, 0 failed**（GREEN 复现）。
```

### 71.9 O01–O20 矩阵（实测）

| case | 断言 | 结果 |
| --- | --- | --- |
| O01 | 未授权触发 ⇒ state=consent_required、provider 调用数=0、候选数=0 | PASS |
| O02 | Reject ⇒ 零调用、候选集不变、ProfileStore 树快照不变 | PASS |
| O03 | Grant ⇒ 恰好 1 次 attempt，且 request 携带 target="manufacturer" + canonical text | PASS |
| O04 | 同 doc/content 已授权 ⇒ 不再要求同意，provider 可被再调用 1 次 | PASS |
| O05 | 换文档 ⇒ 需同意，B 在授权前 provider 调用数不变 | PASS |
| O06 | 同 documentId 但 contentHash 变化 ⇒ 需重新同意 | PASS（APPLICABLE） |
| O07 | 成功端到端 ⇒ state=succeeded、1 个 PendingReview Candidate，字段/证据/lifecycle 正确 | PASS |
| O08 | schema 合法但 excerpt 不存在 ⇒ 零新候选、**旧成功集保留**、state=failed | PASS |
| O09 | transport failure ⇒ failureToken="transport_error"、旧集保留、Profile 树不变 | PASS |
| O10 | malformed response 与 strict schema rejection ⇒ 旧集保留（token 分别为 malformed_response / schema_violation） | PASS |
| O11 | 完全成功的新提取 ⇒ 原子替换（size=1 且为新值，无混合物） | PASS |
| O12 | A 在飞时切到 B，B 完成后再补送 A ⇒ A **不出现**在 B 的候选中（documentId 仍为 B） | PASS |
| O13 | 销毁后重建 owner ⇒ 候选数=0、state=idle、同意未被记住（再触发仍 consent_required） | PASS |
| O14 | 全部 C2 路径后 DeviceProfile 值相等 + ProfileStore 树快照不变 | PASS |
| O15 | 候选视图键名不含 confidence / score / probability | PASS |
| O16 | 候选视图键集恰为 8 个本地验证字段（含编译期无 manifest 成员断言） | PASS |
| O17 | 尝试由传入的 canonical text 驱动；证据 location 与同一文本 round-trip 一致 | PASS |
| O18 | Q_INVOKABLE 含 requestExtraction/grantConsent/rejectConsent，且**无**任何 Candidate 级 accept/edit/reject 方法 | PASS |
| O19 | 出站 request 只含 canonical text + target token；类型**无** originalPath 成员（编译期断言） | PASS |
| O20 | 同 doc/content/同意/fake 结果 ⇒ 两次候选集语义相等 | PASS |

### 71.10 QML / 几何 / 焦点 / 导航证据（§18）

```text
ctest -R "^qml_" ⇒ 100% tests passed, 0 tests failed out of 17（95.50 sec）
含：qml_smoke · qml_geometry_check · qml_nav_check · qml_focus_check ·
    qml_manual_import_check · qml_manual_import_check_windows ·
    qml_profile_editor_check · qml_register_map_check · qml_active_profile_check ·
    qml_profile_semantic_check · qml_write_foundation_check_windows · read-result 系列等
诊断计数（ReferenceError / TypeError / Unable to assign / String.arg Invalid）= **0**。
新 UI 断言来源 = 既有 `runManualImportCheck` stage 4（AI/Candidate 控件不在 manualImportCard 内）
与 stage 7（几何基线一致）——**未新增 gate、未削弱任何断言**。
```

### 71.11 Targeted regression（§20）

```text
ctest -R "candidate_|manual_|device_profile|ai_client|profile_" ⇒ 17/17 PASS / exit 0 / 75.97 sec
  #23 device_profile · #24 profile_controller · #25 active_profile_controller ·
  #26 manual_import · #27 candidate_extraction · #28 candidate_adapter ·
  #29 candidate_orchestration（本轮新增）· #30 manual_extraction · #31 manual_import_pdf_docx ·
  #34 profile_semantic · #35/#36 profile semantic gates · #42 ai_client ·
  #55 qml_profile_editor_check · #57 qml_active_profile_check ·
  #58 qml_manual_import_check · #60 qml_manual_import_check_windows
```

### 71.12 Release full regression（§21）

```text
build : cmake --build build/release ⇒ 全目标链接成功
ctest -N : Total Tests: **60**（前次 59；+1 = candidate_orchestration；其插入使 #29 之后编号 +1）
结果  : 100% tests passed, 0 tests failed out of 60 · Total 994.27 sec · ctest exit code = 0
        #32 deployment_startup_check  Passed 259.77 sec（该 canonical 门禁按设计重建 candidate 树）
Debug：本切片未触及 core 编译单元/config-sensitive 逻辑，未运行 Debug full（Release = canonical gate）。
```

### 71.13 RELEASE CANDIDATE + 部署门禁 + provenance（§26/§27）

```text
candidate root : build/release/candidate/ModbusLens/（由 canonical target 从零重建，1714 files）
exe            : build/release/candidate/ModbusLens/modbuslens.exe
                 size = 6,236,802 B
                 SHA-256 = fb6ff1b8db6eff6bd9ee6e625cca542685005ed8bba42413d1cd76450d2a7232
pdfium.dll     : SHA-256 = d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b
platforms/qwindows.dll : SHA-256 = 804739071bba619b4a4312b5bb29a142545a64c4c80218e5b2e6672ad33ee8ac
部署/启动门禁  : ctest `deployment_startup_check` = **PASS**（259.77 sec，sanitized PATH）
**启动/冒烟过程中未执行任何 ModelScope 提取**（自动化启动 only）。
candidate 仍为 build artifact（非 release artifact、非 LKGC）；**未**创建 canonical ZIP/package。
```

### 71.14 Security / authority audit（§22）

```text
新增/变更文件静态审计（实测）：
  QNetworkAccessManager / QNetworkRequest / QNetworkReply / http(s):// / Bearer /
  Authorization / QSettings / QStandardPaths::writableLocation / sk-* 字面量 = **0**
  （`QSettings` 唯一命中是 CandidateExtractionController.h 的注释「No disk, no QSettings, no cache」）
  src/core Qt include = 0（Zero-Qt 保持）
  src/ui/candidate/ 内 acceptCandidate / editCandidate / rejectCandidate / DeviceProfile 写入 /
  saveToFile = **0**
  confidence / probability / rawResponse 命中 = 1（注释：声明「无 confidence / 无 raw response」）
行为证据：无 transport 实现 ⇒ 无 live request（ModelScopeCandidateRunner::begin 恒返回 false，
beginCount 恒 0）；O01/O02 证明未授权零调用；O14 证明 Profile 零 diff；O15/O16 证明无 confidence /
无 raw response；O18 证明无 C3 动作；O19 证明出站 payload 范围。生产 UI **未**暴露任何 fake AI
模式（无调试假按钮 / 无隐藏 fake 环境变量 / 无测试 fixture loader）。
```

### 71.15 Protected-surface audit（§23）

```text
changed paths（behavior commit 309ba15，实测）：CMakeLists.txt · src/ui/qml/Main.qml ·
  src/ui/qml/pages/DeviceProfilePage.qml · 5 个新增 src/ui/candidate/* · tests/test_candidate_orchestration.cpp
protected 语义面全部未变：
  M10 transaction truth · M11 RegisterDecode · raw DEC/HEX · DeviceProfile verified persistence ·
  C1a TXT/Markdown import · C1b PDF/DOCX extraction · document identity · contentHash ·
  ManualStore/cache · PDF no_extractable_text · SESSION M Evidence validation ·
  SESSION N strict parser · Windows candidate architecture · **Session J 垂直预算整改**
  （stage 7 实测回到基线 0.420/0.439/157.0）
`manualImportCard` 内部内容零改动（仅新增 rightMargin 让出横向空间）；
C1 门禁 stage 4 原样保留且 PASS。git diff --check = 0；无 generated build artifact；
无 docs 混入 behavior commit；无 package artifact；无 C3 / M12-D 代码。
```

### 71.16 明确未做 / DEFERRED

```text
live ModelScope inference            = NOT RUN / NOT VERIFIED
真实网络请求                          = NOT EXECUTED（无 transport 实现）
production live transport            = NOT IMPLEMENTED（随 live extraction slice 交付）
Human Accept / Edit / Reject（C3）    = NOT STARTED / NOT AUTHORIZED
verified DeviceProfile write          = NOT STARTED
Candidate persistence                 = NOT STARTED（SESSION-ONLY）
numeric confidence / retry            = NOT STARTED
C3 · M12-D                            = NOT STARTED / NOT AUTHORIZED
canonical package / release / tag / push = NOT DONE
verified LKGC advancement             = NOT AUTHORIZED（保持 19f9738…）
```

### 71.17 状态

```text
M12-C C1b                        = COMPLETE / HUMAN ACCEPTED（未变）
M12-C                            = IN PROGRESS
M12-C C2                         = IN PROGRESS
Candidate/Evidence foundation    = IMPLEMENTED / AUTOMATED PASS（§67）
ModelScope adapter + strict parser = IMPLEMENTED / AUTOMATED PASS（§69）
Production orchestration         = IMPLEMENTED / AUTOMATED PASS（本节）
Cloud consent gate               = IMPLEMENTED / AUTOMATED PASS（本节）
PendingReview Candidate display  = IMPLEMENTED / AUTOMATED PASS（本节）
C2 Human visual acceptance       = PENDING
C2 Human functional AI extraction = NOT RUN / NOT VERIFIED
Candidate Accept/Edit/Reject     = NOT STARTED / C3
Live ModelScope inference        = NOT RUN / NOT VERIFIED
C3 · M12-D                       = NOT STARTED / NOT AUTHORIZED
verified LKGC                    = 19f9738c980e0a8a31b557c346fb50a4af711cab（**UNCHANGED**）
canonical package                = NOT CREATED
REAL MODBUS HARDWARE             = NOT VERIFIED
```

**注意**：本行为提交**不是** LKGC；verified LKGC 仍为 `19f9738…`，推进必须 Human 明确授权。
**SESSION O = COMPLETE — STOP**（未开始 live ModelScope 测试，未开始 C3）。

---

## 72. M12-C C2 — HUMAN UI FAILURE ARCHIVE：CONSENT DIALOG GEOMETRY + INTERACTION（2026-09-29 · Session O-R1 · docs-only）

> 性质：**append-only 归档**。本节记录 Human 对 SESSION O Release candidate 的真实 UI 检查结果。
> **不**改写 §71 的 SESSION O automated PASS 历史——这是**更晚**的 Human 验收结果。

### 72.0 Human 失败报告（逐字）

```text
Human 对 SESSION O Release candidate 进行真实 UI 检查。
Human 截图视口 = **1280 x 937**。

Human 原文：
  「是否同意选择框有问题，字体都超出弹出框了，
    然后后续点击不了同意和取消按钮」

Human 观察（逐条）：
  1. Cloud AI consent dialog 打开。
  2. 同意正文文字未能正确换行/适配对话框内部。
  3. 很长的中文同意文本可见地伸向/超出可用弹出区域。
  4. 「取消」按钮无法成功点击。
  5. 「同意」按钮无法成功点击。
  6. ⇒ 真实 Release candidate 中同意流程被完全阻断。
```

### 72.1 状态分离归档（禁止合并）

```text
SESSION O automated            = PASS（§71，未变）
SESSION O Human UI             = **FAIL / HOLD**
Consent dialog geometry        = **HUMAN FAIL**
Consent Cancel interaction     = **HUMAN FAIL**
Consent Agree interaction      = **HUMAN FAIL**
PendingReview Candidate 成功视觉态 = **NOT REACHED**（同意流程被阻断，未能到达候选展示）
Live ModelScope                = NOT RUN / NOT VERIFIED

原因：真实 Release candidate 在 1280x937 下同意文本溢出，且 Human 无法操作 Cancel/Agree。
性质：**GEOMETRY FAILURE + INTERACTION FAILURE**（**不是**纯视觉问题）。
根因（本节时点）= **UNKNOWN**，不得在源码/运行期取证前猜测。
```

### 72.2 本节授权范围（narrow remediation）

```text
已授权：诊断 consent dialog 几何失败 · 修复几何 · 修复 Cancel 交互 · 修复 Agree 交互 ·
        增加非空洞的自动化回归覆盖 · 重新生成 Release candidate 供 Human 复测。
未授权：live ModelScope inference · 新 production HTTP transport · SESSION P · C3 ·
        Candidate Accept/Edit/Reject · DeviceProfile write · Candidate persistence · M12-D ·
        canonical package · release/tag/push · LKGC advancement · DeviceProfilePage 大改。
verified LKGC = 19f9738c980e0a8a31b557c346fb50a4af711cab（UNCHANGED）
```

### 72.3 起始基线（实测）

```text
HEAD                = 3cf5fff8ecf79636b41e18a90e6b9a72bad8b11d
SESSION O 三提交     = 0d339c2be8659e7685406eeb8f443c68bd3ce1c3（授权 docs）
                     309ba15a72ffd463b38ade32f0b23547b0bdf04c（behavior）
                     3cf5fff8ecf79636b41e18a90e6b9a72bad8b11d（archive docs）
porcelain -uall     = ?? _ctx.py / ?? _dump.py（仅此两项）
tracked diff / cached = 空 · git diff --check rc = 0 · tags = v1.0.0 · ls-files build = 0
```

**本节动作边界（docs-only）**：仅归档 Human 失败证据 + 状态分离 + 授权范围；未改产品代码 ·
未 build · 未 test · 未推进 LKGC · 未创建 canonical package · 未 push / 未 tag / 未 amend。

---

## 73. M12-C C2 — CONSENT DIALOG REMEDIATION ARCHIVE（2026-09-29 · Session O-R1 · behavior + docs）

> 性质：**append-only 归档**。§72（Human 失败归档）保持原貌；本节只新增。

### 73.0 两个提交

```text
HUMAN-FAILURE DOCS COMMIT = 3d4b46a4c72a5ef175f4c433b4ea7ec4392154fe
                            「M12: archive consent dialog human failure」（4 docs / +126 −1，docs-only）
BEHAVIOR COMMIT           = 3d4c91a19401cc6e00d3cec9361a133c96f1d549
                            「M12: fix cloud consent dialog usability」
                            （3 files / +299 −2，NO AMEND）
                            CMakeLists.txt · src/main.cpp · src/ui/qml/pages/DeviceProfilePage.qml
```

### 73.1 CONSENT UI 归属（源码实测）

```text
CONSENT UI FILE        = src/ui/qml/pages/DeviceProfilePage.qml（页面级 Dialog，非独立组件）
DIALOG TYPE            = QtQuick.Controls `Dialog`（Popup 派生），objectName candidateConsentDialog
DIALOG WIDTH OWNER     = Dialog 自身：`width: Math.min(560, Overlay.overlay.width - 2*DS.spacingXL)`
DIALOG HEIGHT OWNER    = 隐式（来自 contentItem 隐式高）
CONTENT ITEM           = Dialog 的默认子项 = `ColumnLayout`（即 contentItem）
BODY TEXT ITEM         = `Label` objectName candidateConsentScope
BODY WRAP MODE         = `Text.Wrap`（**已存在**，不是缺失项）
BODY WIDTH CONSTRAINT  = **修复前：无**（仅 `Layout.fillWidth: true`）← 缺陷所在
FOOTER OWNER           = contentItem 内 `RowLayout`（含 `Item{Layout.fillWidth}` spacer + 两个 AppButton）
CANCEL BUTTON OWNER    = `AppButton` objectName candidateConsentCancelButton → onClicked: rejectConsent()+close()
AGREE BUTTON OWNER     = `AppButton` objectName candidateConsentGrantButton → onClicked: grantConsent()+close()
MODAL OVERLAY OWNER    = Dialog `modal: true`（点击由模态 overlay 接管）
WINDOW/PAGE GEOMETRY   = 页面是 Main.qml StackLayout index 5；Dialog 锚定 Overlay.overlay 居中
```

### 73.2 根因报告（§7 · 运行期证据支撑）

```text
**GEOMETRY ROOT CAUSE = VERIFIED**
  contentItem（ColumnLayout）**没有宽度约束** ⇒ 其 implicitWidth 由子项 implicitWidth 决定；
  而带 `wrapMode: Text.Wrap` 的 Label 在无宽度约束时 implicitWidth = **未换行的整行文本宽度**。
  实测（1280x937，offscreen）：dialog = 360,421.5 **560**x95，而 candidateConsentScope =
  366,451.5 **975**x13 ⇒ 宽 975 > 560（溢出 421px），高 13 = **单行，完全未换行**。
  Dialog 显式 `width` 只约束弹窗自身矩形，**不会**约束已布局的内容。

**INTERACTION ROOT CAUSE = VERIFIED**（与几何同根）
  同一溢出把 footer 整体推移：candidateConsentCancelButton = **1197**,476.5 50x34
  （dialog 右边界 = 920 ⇒ 已在对话框之外，点击被 `modal: true` 的 overlay 吞掉）；
  candidateConsentGrantButton = **1252**,476.5 89x34（右边界 1341 > 窗口宽 **1280**
  ⇒ 完全落在窗口之外，物理不可达）。⇒ 与 Human「两个按钮都点不了」一致。

**AUTOMATED REPRODUCTION = ACHIEVED**（不只是复现；是直接测量到与 Human 描述一致的越界几何）。
**未验证项（如实）**：Human 截图中出现的**字体度量**差异（offscreen 12px vs windows 16px）
未单独归因；本修复为**结构性**约束，不依赖字体度量（两平台 gate 均 PASS，见 §73.5）。
```

### 73.3 REAL RED（§10 · 修复前）

```text
command  : ./modbuslens.exe --qml-consent-check（QT_QPA_PLATFORM=offscreen,
           QT_ASSUME_STDERR_HAS_CONSOLE=1）
exit code: 1
viewport : 1280x937
结果     : CONSENT: R01: consent dialog open at 1280x937
           CONSENTFAIL: R01/R02/R03: candidateConsentScope escapes the dialog:
                        item=366,451.5 975x13 dialog=360,421.5 560x95
           CONSENTFAIL: R01/R02/R03: candidateConsentCancelButton escapes the dialog:
                        item=1197,476.5 50x34 dialog=360,421.5 560x95
           CONSENTFAIL: R01/R02/R03: candidateConsentGrantButton escapes the dialog:
                        item=1252,476.5 89x34 dialog=360,421.5 560x95
           CONSENTFAIL: R04/R06: Cancel left state 'consent_required' (expected idle …)
           CONSENTFAIL: R05: the dialog stayed open after Agree
           CONSENTFAIL: R05: Agree did not advance the orchestration (state 'consent_required')
性质     : build 与 link 成功、test process 正常 ⇒ **真实运行期几何断言 RED**（非 grep、非 static）。
```

### 73.4 最小修复（§13）

```text
src/ui/qml/pages/DeviceProfilePage.qml 的 Dialog contentItem（ColumnLayout）：
  + width: candidateConsentDialog.availableWidth
  + （Label）Layout.maximumWidth: candidateConsentDialog.availableWidth
（`wrapMode: Text.Wrap` 与 `Layout.fillWidth: true` 原本已存在，未改。）
不改同意语义 · 不改 orchestrator 权限 · 不改页面 IA · 不硬编码 1280x937 专用宽度（用响应式 availableWidth）。
**实证哪一条承重**：仅移除 `width:` 时 gate 仍 PASS；再移除 `Layout.maximumWidth` 时**原缺陷精确复现**
（scope 975x13 / cancel x=1197 / grant x=1252..1341）⇒ 承重项为 `Layout.maximumWidth`（它同时约束
layout 的隐式宽度）。两条同时保留 = 内容与文本双重有界（防御性，且均以 availableWidth 响应式表达）。
```

### 73.5 R01–R12 实测矩阵

| case | 断言 | 结果 |
| --- | --- | --- |
| R01 | 1280x937 下 consent dialog 完全位于可用窗口内 | PASS（dialog 360,421.5 560x… 在窗口内） |
| R02 | 披露文本换行并留在内容区内 | PASS（scope 不再 975 宽；wrap 生效） |
| R03 | Cancel / Agree 完全位于 dialog 与可用视口内 | PASS |
| R04 | 真实鼠标交互点击 Cancel 生效；provider 调用数 = 0 | PASS（state 变 `idle` ⇒ 未进入 Running，即零 provider 尝试） |
| R05 | 真实鼠标交互点击 Agree 生效；编排恰好推进一次 | PASS（dialog 关闭 + state 离开 consent_required/idle ⇒ 已推进；生产 runner 无 transport ⇒ 确定性 `not_configured` 失败） |
| R06 | Cancel 不改 Candidate 集 / DeviceProfile / manual | PASS（candidateCount 仍 0；device_profile 测试 PASS） |
| R07 | Agree 本身不写 verified DeviceProfile | PASS（candidateCount 0；无写路径） |
| R08 | 模态：背景被阻挡但 dialog 自身控件不被阻挡 | PASS（Cancel/Agree 两次真实点击均生效 ⇒ 模态未吞掉自身控件） |
| R09 | SESSION O O01/O02/O03 等同意语义保持 PASS | PASS（`candidate_orchestration` 22/0） |
| R10 | Session J Manual Import 几何保持 | PASS（offscreen **0.420 / 0.439 / 157.0 = 与验收基线完全一致**；`qml_manual_import_check` PASS） |
| R11 | 既有 canonical 视口门禁 | PASS（1000x700 门禁 `qml_manual_import_check` + `..._windows` 均 PASS；**未**新增视口策略） |
| R12 | 无相关 QML 诊断 | PASS（新 gate 已并入 `FAIL_REGULAR_EXPRESSION` 诊断拒绝集；0 命中） |

### 73.6 真实交互测试证据（§11/§16）

```text
CLICK TARGET            = findNamedItem(roots, "candidateConsentCancelButton"/"candidateConsentGrantButton")
EVENT METHOD            = QMouseEvent(MouseButtonPress) + QMouseEvent(MouseButtonRelease) →
                          QCoreApplication::sendEvent(window, …)（真实窗口事件，非直接调用）
ACTUAL QML CONTROL      = AppButton 的 onClicked（生产按钮本身）
OBSERVED UI RESULT      = 对话框关闭（popup.visible == false）
OBSERVED ORCHESTRATION  = Cancel ⇒ state `idle` / candidateCount 0；Agree ⇒ state 离开 consent_required
测试**未**调用 controller 方法、**未** invoke 同意结果函数、**未** grep onClicked、**未**仅检查 visible。
**交互非空洞性 mutation**：临时仅断开 Cancel 的**真实动作**（保留 close()）⇒ gate
  CONSENTFAIL: R04/R06: Cancel left state 'consent_required'（exit 1）⇒ 证明点击测试观测的是真实动作，
  而不是「对话框被关掉」这一副作用。精确还原后 residue = 0、复绿。
```

### 73.7 几何 mutation（§15 · 强制）

```text
precise mutation：移除 contentItem 的 `width: availableWidth` 与 Label 的
                  `Layout.maximumWidth: availableWidth`（回到原缺陷状态）。
实测 RED ：exit 1；原缺陷几何**精确复现**（scope 975x13 / cancel 1197 / grant 1252）。
precise reverse：精确还原两行（**未用** checkout / restore / reset / stash）。
residue ：grep -c "NEGATIVE-CONTROL MUTATION" = 0；恢复后 gate exit 0 / CONSENT CHECK PASS。
```

### 73.8 Targeted regression（§17）

```text
ctest -R "^qml_|candidate_|manual_|device_profile|ai_client|profile_" ⇒ **30/30 PASS / exit 0 / 150.95 s**
含新增 #59 qml_consent_check（4.93 s）与 #60 qml_consent_check_windows（5.14 s）、
#27 candidate_extraction · #28 candidate_adapter · #29 candidate_orchestration ·
#26 manual_import · #30 manual_extraction · #31 manual_import_pdf_docx · #23 device_profile ·
#24 profile_controller · #42 ai_client 等。
```

### 73.9 Release full regression（§18）

```text
build : 全目标链接成功（BUILDALL_RC = 0）
ctest -N : Total Tests: **62**（前次 60；+2 = qml_consent_check + qml_consent_check_windows）
结果  : 100% tests passed, 0 tests failed out of 62 · Total **837.26 s** · ctest exit code = **0**
        #32 deployment_startup_check  Passed 168.38 s
        #59 qml_consent_check         Passed   4.93 s
        #60 qml_consent_check_windows Passed   5.29 s
Debug full：未运行（本切片仅改 QML/测试门禁，未触及 core 编译单元；Release = canonical gate）。
```

### 73.10 QML 诊断 + Session J 非回归（§12/§10）

```text
QML 诊断：新 gate 已加入 FAIL_REGULAR_EXPRESSION 诊断拒绝集
          （ReferenceError / TypeError / Unable to assign / String.arg Invalid）= **0 命中**。
Session J（offscreen 实测）：`MAN: stage 7: vertical budget — profile row share=0.420,
          manual import share=0.439, preview viewport=157.0` = **与 Human 验收基线完全一致**。
Windows QPA 实测（同一 gate，字体度量更大）：row 0.420 / manual import 0.431 / preview 137.0
          ⇒ 仍满足门槛（≥0.35 / ≥120），系平台字体度量既有差异，非本切片引入。
```

### 73.11 Protected-surface audit（§19）

```text
behavior commit 3d4c91a changed paths（实测）= CMakeLists.txt · src/main.cpp ·
  src/ui/qml/pages/DeviceProfilePage.qml（QML diff 仅 +12 行：注释 + 两条宽度约束）
protected 面 diff = **0**：src/core · src/ui/manual · src/ui/profile · src/ui/ai ·
  src/ui/candidate · tests · scripts · src/ui/qml/Main.qml
⇒ M10/M11 truth · DeviceProfile 持久化 · C1a/C1b · manual identity/contentHash/ManualStore ·
  SESSION M Evidence · SESSION N strict parser · SESSION O orchestration/consent/Candidate 语义 ·
  Windows candidate 架构 全部**未变**；**未**新增 production network transport；**无** C3 代码。
git diff --check = 0；无无关格式化 churn；无 generated build artifact；behavior commit 内无 docs。
```

### 73.12 Candidate provenance + 部署门禁（§22）

```text
candidate root : build/release/candidate/ModbusLens/（canonical target 从零重建，1714 files）
exe            : build/release/candidate/ModbusLens/modbuslens.exe
                 size = 6,268,661 B
                 SHA-256 = f19e961f65e69f1ca15ef3013dc35d7edcbbb283b919b0286c2b8466a7afb122
pdfium.dll     : SHA-256 = d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b
platforms/qwindows.dll : SHA-256 = 804739071bba619b4a4312b5bb29a142545a64c4c80218e5b2e6672ad33ee8ac
部署/启动门禁  : `deployment_startup_check` = **PASS**（182.81 s，sanitized PATH）
启动/冒烟**未**执行任何 ModelScope 提取。
Human 复测入口 = 上述 candidate exe（**不是** build/release/modbuslens.exe）；**未**创建 canonical ZIP/package。
```

### 73.13 状态

```text
SESSION O automated                = PASS（§71，未变）
SESSION O Human UI                 = FAIL / HOLD（原始 Human 测试结论，§72，不改写）
Consent dialog geometry            = REMEDIATED / AUTOMATED PASS
Consent Cancel interaction         = REMEDIATED / AUTOMATED PASS
Consent Agree interaction          = REMEDIATED / AUTOMATED PASS
Consent remediation overall        = IMPLEMENTED / AUTOMATED PASS
Human re-test                      = REQUIRED
C2 Human visual acceptance         = HOLD / PENDING RE-TEST
PendingReview 成功 Human 视觉      = NOT REACHED / NOT VERIFIED
Live ModelScope                    = NOT RUN / NOT VERIFIED
verified LKGC                      = 19f9738c980e0a8a31b557c346fb50a4af711cab（**UNCHANGED**）
SESSION P                          = NOT STARTED
C3 · M12-D                         = NOT STARTED / NOT AUTHORIZED
canonical package                  = NOT CREATED
REAL MODBUS HARDWARE               = NOT VERIFIED
```

**注意**：本行为提交**不是** LKGC；verified LKGC 仍为 `19f9738…`，推进必须 Human 明确授权。
**SESSION O-R1 = COMPLETE — STOP**（未开始 SESSION P / live ModelScope / C3）。

---

## 74. M12-C C2 — HUMAN UI FAIL ARCHIVE：CONSENT OUTSIDE-CLICK DISMISSAL（2026-09-29 · Session O-R2 · docs-only）

> 性质：**append-only 归档**。记录 O-R1 之后 Human 的**再次**真实 UI 检查结果。
> **不**改写 §72/§73 的历史；这是更晚的 Human 验收结果。

### 74.0 Human 报告（逐字）

```text
Human 对 SESSION O-R1 后的 Release candidate 再次测试。

已确认（Human PASS 部分）：
  · Consent geometry / wrapping 修复：**Human 未再报告溢出**
  · **Agree button = HUMAN PASS**
  · Agree 后进入 `提取未成功 (not_configured)` = 符合当前无 production transport 的预期

新的 Human FAIL：
  Consent dialog 打开时，Human 点击 dialog 外部的背景页面 ⇒ **dialog 直接消失**。
  随后 AI Candidate 区仍显示 `需要你同意后才会发送已抽取文本`。
  Human screenshot 已确认 dialog 已消失。

结论：
  **O-R1 Human re-test = FAIL / HOLD**
  **Consent outside-click modality = HUMAN FAIL**
```

### 74.1 状态分离归档（禁止合并）

```text
SESSION O automated                   = PASS（§71，未变）
SESSION O-R1 geometry remediation     = IMPLEMENTED / AUTOMATED PASS（§73，未变）
O-R1 Human re-test                    = **FAIL / HOLD**
Geometry / wrapping                   = **HUMAN PASS**（本轮 re-test 未再报告溢出）
Agree interaction                     = **HUMAN PASS**
**Outside-click modality**            = **HUMAN FAIL**
Cancel interaction（本轮）            = 未单独报告（不作推断）
PendingReview 成功 Human 视觉         = NOT REACHED / NOT VERIFIED
Live ModelScope                       = NOT RUN / NOT VERIFIED
SESSION P                             = **BLOCKED**（未开始）

**R08 既往自动化证据 = INSUFFICIENT**（对本次澄清后的需求而言）：R08 当时只断言
「模态不阻挡 dialog 自身控件」，**未**证明「真实外部点击后 dialog 仍保持打开」
⇒ 属 **TEST SEMANTIC COVERAGE GAP**，将在 §75 以 R2-01..R2-05 补齐。
```

### 74.2 起始基线（实测）

```text
HEAD              = 5ba8c4e1a2a6c5d4fbca1540574d439aea35b288
O-R1 behavior     = 3d4c91a19401cc6e00d3cec9361a133c96f1d549
O-R1 archive docs = 5ba8c4e1a2a6c5d4fbca1540574d439aea35b288
porcelain -uall   = ?? _ctx.py / ?? _dump.py（仅此两项）
tracked / cached  = 空 · git diff --check rc = 0 · tags = v1.0.0 · ls-files build = 0
verified LKGC     = 19f9738c980e0a8a31b557c346fb50a4af711cab（UNCHANGED）
```

### 74.3 产品契约（本次澄清后冻结，供 §75 实现与验证）

```text
模态云同意对话框：外部/背景鼠标点击 MUST
  · 不关闭同意对话框
  · 不授予同意
  · 不计作 Cancel
  · 不调用 provider
  · 不改变 Candidate 集
  · 不改变 DeviceProfile
  · 不激活被点击的背景控件
外部点击后：dialog 仍可见；编排仍为 consent-required；provider 调用数 = 0。
Human 仍须能**显式**选择「取消」或「同意」（两者既有语义不变）。
Escape 行为：除非实际实现令其成为本 narrow fix 的必要条件，否则**不得改变**。
```

**本节动作边界（docs-only）**：仅归档 Human 失败 + 状态分离 + R08 覆盖缺口 + 产品契约；
未改产品代码 · 未 build · 未 test · 未推进 LKGC · 未创建 canonical package · 未 push / 未 tag / 未 amend。

---

## 75. M12-C C2 — CONSENT MODALITY REMEDIATION ARCHIVE（2026-09-29 · Session O-R2 · behavior + docs）

> 性质：**append-only 归档**。§74（Human 失败归档）保持原貌；本节只新增。

### 75.0 两个提交

```text
HUMAN-FAILURE DOCS COMMIT = d72da898e160306b956b9ea4d96fd270092b4cc8
                            「M12: archive consent outside-click human failure」（4 docs / +148 −1，docs-only）
BEHAVIOR COMMIT           = bf6c02b8c8573ebf323b10764bbd01db29e16db7
                            「M12: keep cloud consent modal until explicit choice」
                            （2 files / +54 −4，NO AMEND）
                            src/main.cpp（扩展 O-R1 运行时门禁 +48 −4）·
                            src/ui/qml/pages/DeviceProfilePage.qml（closePolicy +10）
```

### 75.1 ACTUAL POPUP / DIALOG CLOSE BEHAVIOR（§2/§3 源码 + 运行期实测）

```text
CONSENT UI FILE        = src/ui/qml/pages/DeviceProfilePage.qml
DIALOG TYPE            = QtQuick.Controls `Dialog`（Popup 派生），objectName candidateConsentDialog
MODAL                  = `modal: true`
FOCUS                  = 默认（Popup 取得焦点）
CURRENT closePolicy    = **修复前：未设置** ⇒ 继承 Qt Quick Controls 默认
                         **`Popup.CloseOnEscape | Popup.CloseOnPressOutside`**
OUTSIDE CLICK BEHAVIOR = 外部按下即触发 close() ⇒ 对话框消失（运行期实测，见 §75.3）
onClosed / onRejected  = **未定义**（无自定义关闭副作用）
CANCEL ACTION          = `candidateController.rejectConsent()` + `candidateConsentDialog.close()`
AGREE ACTION           = `candidateController.grantConsent()` + `candidateConsentDialog.close()`
ORCHESTRATION AFTER OUTSIDE DISMISS = **不变**（仍 `consent_required`；candidateCount = 0）
   ⇒ 与 Human screenshot（候选区仍显示「需要你同意后才会发送已抽取文本」）一致。

仓库既有惯例（实测 grep `closePolicy`）：
  DeviceProfilePage.qml:1119 / :1167 / :1218（同页三个模态 Dialog）与 Main.qml:80 = `Popup.NoAutoClose`
  WriteFoundationSection.qml:551 · CommunicationPage.qml:726 = `Popup.CloseOnEscape`
  ⇒ consent dialog 是**同页唯一未设 closePolicy 的对话框**（本缺陷的成因）。
```

### 75.2 ROOT CAUSE（§7）= VERIFIED

```text
ROOT CAUSE = **VERIFIED**：consent Dialog **未显式设置 closePolicy**，继承 Qt 默认含
`CloseOnPressOutside` ⇒ 真实外部鼠标按下调用 `close()`，在**未获任何显式选择**的情况下关闭同意门。
运行期证据：修复前 gate 输出 `CONSENTFAIL: R2-01: the consent dialog was dismissed by a real
outside press`（exit 1），且关闭后 `stateToken` 仍为 `consent_required`、workspace index 仍为 5
（⇒ 只有 close() 被执行，编排与背景控件均未被触碰）。

**为什么它超出「模态」的直觉**：`modal: true` 只保证**输入被 overlay 阻断**（R2-02 实测通过，
背景控件未激活）；它**不**决定 Popup 自身的 close 策略 —— 两者是独立机制。

**R08 为何漏检（§7 要求的分类）** = **TEST SEMANTIC COVERAGE GAP**：
O-R1 的 R08 断言的是「模态不阻挡 dialog **自身**控件」（Cancel/Agree 可点），
**从未**断言其互补方向「真实**外部**点击后 dialog 仍保持打开」。
⇒ 覆盖语义不完整，而非 Agent 能力退化或框架行为异常；本 session 以 R2-01..R2-05 补齐该方向。
```

### 75.3 REAL RED（§6 · 修复前，Human 缺陷自动化复现）

```text
command   : ./modbuslens.exe --qml-consent-check（QT_QPA_PLATFORM=offscreen, QT_ASSUME_STDERR_HAS_CONSOLE=1）
viewport  : 1280x937
dialog    : candidateConsentDialog 360,421.5 560x95（与 §73 修复后几何一致）
outside click coordinate = navItem_0 中心（左侧导航栏，恒在 dialog 之外且在窗口之内）
clicked background target = navItem_0（Dashboard rail entry）
dialog visible before = true
dialog visible after  = **false**（缺陷）
orchestration state   = consent_required（未变）
provider count        = 0（未变）
exit code             = **1**
failing assertion     : `CONSENTFAIL: R2-01: the consent dialog was dismissed by a real outside press`
（连带 `R04: candidateConsentCancelButton is not clickable` —— dialog 已消失）
性质：build/link 正常、test process 正常 ⇒ **真实运行期 RED**。
```

### 75.4 MINIMUM FIX（§8）

```text
src/ui/qml/pages/DeviceProfilePage.qml 的 consent Dialog：
  + closePolicy: Popup.CloseOnEscape
选择理由（evidence-based）：
  · 契约 §5 要求「Escape 行为除非必要否则**不变**」⇒ `Popup.NoAutoClose`（同页三兄弟 Dialog 的惯例）
    会**同时**去掉 Escape 关闭 ⇒ 属静默改变 Escape 语义，**故刻意不采用**；
  · `Popup.CloseOnEscape` 只移除 `CloseOnPressOutside` ⇒ 外部按下不再关闭，**Escape 行为逐字保持**。
**未**改同意文案 · **未**改 O-R1 几何约束 · **未**改 Candidate/provider/parser 语义 ·
**未**新增 production transport · **未**开始 C3 · **未**改页面 IA。
**未发生** §5 所述「Escape 语义冲突」⇒ 无需 STOP。
```

### 75.5 R2-01..R2-10 实测矩阵（§9）

| case | 断言 | 结果 |
| --- | --- | --- |
| R2-01 | 真实鼠标外部按下后 dialog 仍打开 | PASS（修复前为 RED） |
| R2-02 | 被点击位置处的背景控件未激活（`workspaceHost.currentIndex` 5 → 5） | PASS |
| R2-03 | 外部点击后 provider 调用/尝试数 = 0（state 仍为 `consent_required` ⇒ 无 attempt 启动） | PASS |
| R2-04 | 编排状态保持 consent-required | PASS |
| R2-05 | Candidate 集 / DeviceProfile / ManualDocument 均未变（candidateCount 0） | PASS |
| R2-06 | 真实点击 Cancel 仍生效（dialog 关闭 + 零 provider + 回到 `idle`） | PASS |
| R2-07 | 真实点击 Agree 仍生效（dialog 关闭 + 编排恰好推进一次） | PASS |
| R2-08 | O-R1 全部容器/换行/footer 断言仍 PASS（同一 gate 的 R01/R02/R03） | PASS |
| R2-09 | Session J Manual Import 垂直几何仍达验收门槛 | PASS（见 §75.7） |
| R2-10 | QML 诊断 0 命中（新 gate 仍在诊断拒绝集内） | PASS |

### 75.6 NEGATIVE CONTROL（§10）

```text
precise mutation：把 closePolicy 精确改回失败行为
                  `Popup.CloseOnEscape | Popup.CloseOnPressOutside`。
实测 RED ：exit **1**；`CONSENTFAIL: R2-01: the consent dialog was dismissed by a real outside
           press`（+ 连带 R04）；build/run 均正常。
precise reverse：精确还原为 `Popup.CloseOnEscape`（**未用** checkout / restore / reset / stash）。
residue ：grep -c "NEGATIVE-CONTROL MUTATION" = **0**。
恢复后  ：exit 0 / `CONSENT CHECK PASS (R01..R07 + R2-01..R2-07)`。
```

### 75.7 Targeted / Release / 诊断 / 几何（§11）

```text
targeted : ctest -R "^qml_|candidate_|manual_|device_profile|ai_client|profile_" ⇒
           **30/30 PASS / 0 failed / exit 0 / 161.05 s**
           （含 #59 qml_consent_check · #60 qml_consent_check_windows · #27 candidate_extraction ·
             #28 candidate_adapter · #29 candidate_orchestration · #26 manual_import · #23 device_profile 等）
ctest -N : Total Tests = **62**（未新增 target —— 本轮是**扩展** O-R1 既有 gate，非新建）
Release full : **100% tests passed, 0 tests failed out of 62** · Total **964.44 s** · exit **0**
           （#32 deployment_startup_check 280.23 s · #59 qml_consent_check 5.46 s）
QML 诊断 : 0 命中（gate 仍在 FAIL_REGULAR_EXPRESSION 诊断拒绝集内）
双平台   : consent gate 在 offscreen(1280x937) 与真实 Windows QPA(1280x844) **均 PASS**
Session J: offscreen `row 0.420 / manual import 0.439 / preview 157.0`（既有验收基线，未变）
```

### 75.8 Protected-surface audit（§12）

```text
behavior commit bf6c02b changed paths（实测）= src/main.cpp · src/ui/qml/pages/DeviceProfilePage.qml
protected 面 diff = **0**：src/core · src/ui/manual · src/ui/profile · src/ui/ai · src/ui/candidate ·
  tests · scripts · src/ui/qml/Main.qml
⇒ O-R1 consent geometry · Cancel/Agree 语义 · Session J 几何 · SESSION M Candidate/Evidence ·
  SESSION N strict parser · SESSION O orchestration · DeviceProfile 权威/持久化 · C1a/C1b import ·
  M10/M11 truth · Windows candidate 部署架构 **全部未变**。
**无** production HTTP transport · **无** live ModelScope · **无** C3 Accept/Edit/Reject ·
**无** AI 写 Profile · **无** Candidate 持久化 · **无** confidence · **无** M12-D。
git diff --check = 0；QML diff 精确等于 `closePolicy` 一行 + 说明注释；无格式化 churn；
无 generated build artifact；behavior commit 内无 docs。
```

### 75.9 Candidate provenance + 部署门禁（§14）

```text
candidate root : build/release/candidate/ModbusLens/（canonical target 从零重建，1714 files）
exe            : build/release/candidate/ModbusLens/modbuslens.exe
                 size = 6,274,447 B
                 SHA-256 = 05fe14ec955a7e469f2ef846f0fdcb62ef29075aac5ccb27259740813df4f361
pdfium.dll     : SHA-256 = d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b
platforms/qwindows.dll : SHA-256 = 804739071bba619b4a4312b5bb29a142545a64c4c80218e5b2e6672ad33ee8ac
部署/启动门禁  : `deployment_startup_check` = **PASS**（170.90 s，sanitized PATH）
启动/冒烟**未**执行任何 ModelScope 提取。Human 复测入口 = 上述 candidate exe；
**未**创建 canonical ZIP/package。
```

### 75.10 状态

```text
SESSION O automated                = PASS（未变）
SESSION O-R1 geometry remediation  = IMPLEMENTED / AUTOMATED PASS / **HUMAN PASS**（本轮 re-test 未再报告溢出）
O-R1 Agree interaction             = **HUMAN PASS**
O-R1 Human re-test                 = FAIL / HOLD（§74，原始 Human 结论不改写）
Outside-click modality             = **IMPLEMENTED / AUTOMATED PASS**（本节）
O-R2 Human re-test                 = **REQUIRED**
C2 Human visual                    = HOLD / PENDING RE-TEST
PendingReview 成功 Human 视觉      = NOT REACHED / NOT VERIFIED
Live ModelScope                    = NOT RUN / NOT VERIFIED
verified LKGC                      = 19f9738c980e0a8a31b557c346fb50a4af711cab（**UNCHANGED**）
SESSION P                          = NOT STARTED
C3 · M12-D                         = NOT STARTED / NOT AUTHORIZED
canonical package                  = NOT CREATED
REAL MODBUS HARDWARE               = NOT VERIFIED
```

**注意**：本行为提交**不是** LKGC；verified LKGC 仍为 `19f9738…`，推进必须 Human 明确授权。
**SESSION O-R2 = COMPLETE — STOP**（未开始 SESSION P / live ModelScope / C3）。

---

## 77. M12-C C2 — HUMAN AUTHORIZATION + VERIFIED BUILD/TEST RCA（SESSION P-R1 · 归档）

> 性质：**append-only 归档**。本节只记录 Human 授权与两条**已 VERIFIED** 的根因；
> **不**声称任何修复已实施。

### 77.0 Human 授权（逐字）

```text
Human 明确批准：「同意按上述永久修复方案恢复 SESSION P 主线。」

授权范围：A 永久修复已 VERIFIED 的 CMake acceptance configuration contract regression ·
B 永久消除 canonical CTest 对 ambient Windows PATH / foreign MinGW runtime 的依赖 ·
C 使用 genuinely fresh binary directory 完成 Release acceptance ·
D 恢复并完成当前 SESSION P production ModelScope transport slice ·
E candidate regeneration + deployment/startup acceptance ·
F docs/governance archive · G prepare Human live ModelScope gate。

**未授权**：WorkBuddy 执行真实 ModelScope inference · 读取/粘贴 Human 真实 token ·
C3 Accept/Edit/Reject · AI 写 verified DeviceProfile · Candidate persistence · numeric confidence ·
M12-D · canonical ZIP/package · release/tag/push · **verified LKGC advancement**。
**Live cloud inference remains HUMAN-ONLY。**
```

### 77.1 RCA-A — CMAKE CONFIGURATION CONTRACT REGRESSION = **VERIFIED**

```text
· commit `fb2170e` 曾引入 `option(MODBUSLENS_BUILD_C1B_EXTRACTION_TESTS "..." ON)`（默认 ON）。
· commit `c36eb18`（"M12: integrate PDF and DOCX manual import workflow"）**删除该 option() 声明**，
  但**保留** `if(MODBUSLENS_BUILD_C1B_EXTRACTION_TESTS)` 守卫。
· 当前 HEAD 与工作区的 CMakeLists.txt **既无 option() 也无 set()**（实测 grep 为空）
  ⇒ 变量未定义 ⇒ false（OFF）。
· `release-local` preset **不显式设置**该变量（只设 CMAKE_PREFIX_PATH / CMAKE_CXX_COMPILER +
  generator/binaryDir/env）；`release` 只设 CMAKE_BUILD_TYPE / CMAKE_EXPORT_COMPILE_COMMANDS。
· 旧 `build/release/CMakeCache.txt:312` = `MODBUSLENS_BUILD_C1B_EXTRACTION_TESTS:BOOL=ON`
  ⇒ **陈旧 CMake cache 保留了已被删除的 option 值**。
· 后果：旧缓存树注册完整 C1b 测试；**fresh 树因变量未定义而略过四个目标**：
  `manual_extraction` · `manual_import_pdf_docx` · `deployment_startup_check` ·
  `c1b_dependency_materializer`（四者**全部**位于 line 892–1000 的同一个 `if(...)` 块内）。
定性：**CONFIGURATION CONTRACT GAP / REGRESSION**，**不是**合法的 canonical 差异。
```

### 77.2 RCA-B — WINDOWS RUNTIME DLL RESOLUTION = **VERIFIED**

```text
consumer = `modbuslens_active_master_tests.exe`
  需要 `libstdc++-6.dll` 导出 `_ZSt28__throw_bad_array_new_lengthv`（GCC 11+ 才有）。
wrong provider（继承 PATH 第一命中）= `D:\mingw64\bin\libstdc++-6.dll`
  1,420,800 B / 2018-05-13 / SHA-256
  43b71d76ec2304f210600457a6c29baf81c026ab130b29a3471fe64c17fdec13
  ⇒ **不导出** `_ZSt28__throw_bad_array_new_lengthv`（导出数 = 0；其余 9 个所需符号各 = 1）。
canonical provider = `D:\QT\6.11.1\mingw_64\bin\libstdc++-6.dll`
  2,243,072 B / 2023-05-25 / SHA-256
  8013488c5528bad7966ca07f3ea2e7a9b743cacb258fe76b46a326f821cc83b0
  ⇒ **导出**该符号（导出数 = 1）。
另：`libgcc_s_seh-1.dll` 亦被 `D:\mingw64\bin\libgcc_s_seh-1.dll` 优先解析
  （78,336 B / 2018-05-13 / 52c1b144…），canonical = 109,056 B / 5e275891…。
受控证据：A 继承 PATH = RED · B canonical Qt/MinGW PATH = GREEN · C canonical + wrong 前置 = RED。
定性：runtime resolution is load-bearing = VERIFIED · wrong provider = VERIFIED ·
missing entrypoint = VERIFIED。
限制（如实）：WorkBuddy **未能独立捕获精确数值码**（工具限制），Human 的 fresh CTest 报告
`0xc0000139 STATUS_ENTRYPOINT_NOT_FOUND`；loader 机制已独立 VERIFIED。
```

### 77.3 RCA-C — 旧 `build/release` 边界 = **UNKNOWN（不修复、不复用）**

```text
旧 build/release 反复出现 `ninja: error: failed recompaction: Permission denied`；
genuinely fresh tree 配置 + 构建 **401/401 PASS** 且**未复现**该失败。
⇒ 该问题**隔离于旧构建树自身状态**；**精确 lock/filter/root cause 仍 UNKNOWN**。
**不得**尝试修复或以该旧树作为 canonical acceptance 证据。
```

### 77.4 撤回的既往错误表述（degradation correction）

```text
撤回：「fresh 配置未使用仓库 preset」→ 更正：Human 确用
      `cmake --preset release-local -B <fresh-dir>`；缺陷是 option() 声明缺失。
撤回：「Qt6Core.dll / build/release 陈旧 DLL 是已证元凶」→ 更正：精确 wrong provider 是外来的
      `D:\mingw64\bin\libstdc++-6.dll`（Qt6*.dll 无歧义解析到 canonical Qt）。
撤回：WorkBuddy 先前「文件锁已释放」推断 → 已由 fresh-tree 结果证伪。
**不得**重新引入上述撤回结论。若新证据与已接受 RCA 冲突：先 STOP 再变更解释。
```

### 77.5 冻结的永久修复策略（尚未实施）

```text
1. 恢复显式 CMake configuration contract（canonical 源配置必须自证其真值）。
2. canonical acceptance **不得**依赖 stale cache。
3. CTest Windows runtime 必须使用**由 toolchain 推导**的确定性路径。
4. acceptance **必须**包含 genuinely fresh binary tree。
5. raw ambient PATH **不得**决定 Qt/MinGW runtime identity。
6. 旧 `build/release` **不得**作为 fresh reproducibility 证据。
```

**本节动作边界（docs-only）**：仅归档 Human 授权 + 已 VERIFIED RCA + 撤回清单 + 冻结策略；
**未**实施任何修复 · 未改产品代码 · 未 build · 未 test · 未执行任何 live 网络请求 ·
未推进 LKGC · 未创建 canonical package · 未 push / 未 tag / 未 amend。

---

## 76. M12-C C2 — HUMAN RE-ACCEPTANCE ARCHIVE：CONSENT UI / INTERACTION / MODALITY PASS（2026-09-30 · Session P · docs-only）

> 性质：**append-only 归档**。记录 Human 对 SESSION O-R2 Release candidate 的**再次**真实 UI 复测结果。
> **不**改写 §72/§74 的 FAIL 历史；这是更晚的 Human 验收结果。

### 76.0 Human 报告（逐字）

```text
Human 完成 SESSION O-R2 Release candidate 复测。

Human report（逐字）：全部 PASS

权威解读（Human 明确范围）：
  Consent dialog layout                     = HUMAN PASS
  Outside / background click keeps dialog open = HUMAN PASS
  Background control blocked                = HUMAN PASS
  Cancel                                    = HUMAN PASS
  Agree                                     = HUMAN PASS
  Agree → `not_configured`（当前 pre-transport 构建的失败态）
                                            = HUMAN PASS / EXPECTED FOR PRE-TRANSPORT BUILD

⇒ SESSION O-R2 Human re-test = **PASS**
⇒ Consent UI / interaction / modality = **HUMAN ACCEPTED**
```

### 76.1 状态分离归档（禁止合并 · 禁止扩写）

```text
SESSION O automated                  = PASS（§71，未变）
SESSION O-R1 geometry remediation    = IMPLEMENTED / AUTOMATED PASS（§73，未变）
O-R1 original Human test             = FAIL / HOLD（§72，历史原样保留）
O-R2 pre-retest state                = PENDING（§75，历史原样保留）
**SESSION O-R2 Human re-test**       = **PASS**
Consent geometry                     = HUMAN PASS
Outside-click modality               = HUMAN PASS
Background blocking                  = HUMAN PASS
Cancel                               = HUMAN PASS
Agree                                = HUMAN PASS
Agree → not_configured               = HUMAN PASS / EXPECTED PRE-TRANSPORT STATE
Consent UI / interaction / modality  = **HUMAN ACCEPTED**

**明确保留（Human 未确认的部分，禁止扩写）**：
PendingReview 成功 Human 视觉        = **NOT REACHED / NOT VERIFIED**
Live ModelScope                      = **NOT RUN / NOT VERIFIED**
C2 overall                           = **IN PROGRESS**
verified LKGC                        = 19f9738c980e0a8a31b557c346fb50a4af711cab（**UNCHANGED**）
```

**注意**：Human 的 PASS 只覆盖其原文列出的 6 项与「Consent UI / interaction / modality」范围；
**不**构成 live ModelScope 验收，**不**构成 PendingReview 成功视觉验收，**不**构成 LKGC 推进授权。

### 76.2 本节授权范围（SESSION P engineering）

```text
已授权（SESSION P engineering）：
  · production ModelScope HTTP transport
  · process-environment credential / config seam
  · production wiring 到已接受的 C2 provider-neutral 架构
  · deterministic transport / network tests
  · Release candidate preparation
  · Human live-smoke preparation

**明确未授权**：
  Agent live ModelScope inference（Human GATE：私密 token 属 Human、canonical extracted text 会离机、
  且冻结的云同意契约要求 Human 显式同意）· C3 Accept/Edit/Reject · AI 写 verified DeviceProfile ·
  Candidate persistence · numeric confidence · M12-D · canonical package · release/tag/push ·
  **LKGC advancement**。

**WorkBuddy MUST STOP before any real inference request。**
```

### 76.3 起始基线（实测）

```text
HEAD              = 4893f78c71b6d830cb29e13b089f5978f79b0b7a
O-R2 三提交        = d72da898e160306b956b9ea4d96fd270092b4cc8（失败归档）
                    bf6c02b8c8573ebf323b10764bbd01db29e16db7（behavior）
                    4893f78c71b6d830cb29e13b089f5978f79b0b7a（修复归档）
porcelain -uall   = ?? _ctx.py / ?? _dump.py（仅此两项）
tracked / cached  = 空 · git diff --check rc = 0 · tags = v1.0.0 · ls-files build = 0
```

**本节动作边界（docs-only）**：仅归档 O-R2 Human PASS + 状态分离 + SESSION P 授权范围；
未改产品代码 · 未 build · 未 test · 未执行任何 live 网络请求 · 未推进 LKGC ·
未创建 canonical package · 未 push / 未 tag / 未 amend。

---

## 78. M12-C C2 — SESSION P-R1C：QML CONSENT FRESH-TREE CTEST "CRASH" RCA + HARNESS CREDENTIAL SANITIZATION（2026-10-01 · ZCode 接管轮）

> 性质：append-only 归档。SECTION P-R1C = SESSION P-R1 修复授权（§77）范围内的
> 一个子切片：诊断 fresh tree 上 `qml_consent_check` 的"零输出失败"。
> **⚠ 该失败实为一次真实云端推理的可见症状** —— 全文以 ISSUE-020 为准：
> `docs/issues/ISSUE-020-harness-ambient-credential-live-dispatch.md`。

### 78.1 接管基线（ZCode 实测，2026-10-01）

```text
HEAD              = 20a9129771aff9ad48f40659a6e5083ba544bc01（= §77 授权归档提交）
verified LKGC     = 19f9738c980e0a8a31b557c346fb50a4af711cab（UNCHANGED）
WIP（接管时）      = M CMakeLists.txt
                    M src/ui/candidate/ModelScopeCandidateRunner.{h,cpp}
                    ?? src/ui/candidate/ModelScopeExtractionTransport.{h,cpp}
                    ?? src/ui/candidate/ModelScopeHttpClient.{h,cpp}
                    ?? tests/test_candidate_transport.cpp
                    ?? _ctx.py / ?? _dump.py（前轮遗留，不动）
git diff --check  = rc 0 · cached = 空
```

WIP 内容 = §76 授权的 SESSION P transport slice（production `QtModelScopeHttpClient`
→ `ModelScopeExtractionTransport` → 既有 SESSION N adapter 链 + P01–P24 确定性
transport 测试）+ §77 冻结策略 ①（恢复 `option(MODBUSLENS_BUILD_C1B_EXTRACTION_TESTS)`
声明）+ 冻结策略 ③⑤（CTest runtime 由 toolchain 推导、PATH 前置）。接管时
WorkBuddy 已在 `build/acceptance/session-p-r1b-release` 完成配置/构建，并在
2026-09-30 14:53 的 `qml_consent_check` 上得到 **Failed / 零输出 / 15.87s**。

### 78.2 RCA（VERIFIED，证据矩阵见 ISSUE-020）

```text
RC-1（核心）：ambient MODELSCOPE_API_KEY（Windows User 级持久存在，len=39）
  泄漏进确定性 harness 进程。SESSION P 后 production runner 不再惰性：
  consent gate 同意点击 → runner begin() → ensureWired() → 真实 HTTPS 调度
  → 200 + strict parser + 本地 Evidence 验证全过 → candidateCount=1
  ⇒ R07 FAIL（"Agree wrote a candidate without a provider result"）。
  单变量对照：credential ABSENT ⇒ 同一二进制 CONSENT CHECK PASS / 1.87s。
  15.87s − 1.87s ≈ 真实网络往返 ⇒ WorkBuddy 14:53 运行同样含真实调度。
RC-2（可观测性）：qml_consent_check（非 _windows）未设
  QT_ASSUME_STDERR_HAS_CONSOLE=1 ⇒ GUI-subsystem exe 在 ctest 管道下
  Qt 诊断全丢 ⇒ "零输出 Failed"，被误读为 crash。
```

**Governance disclosure（不扩写、如实记录）**：WorkBuddy 14:53 ctest 与 ZCode
RCA Run A 各自可能已发生一次真实 ModelScope 推理（payload = 8 行种子文本，
模型 = 接受的默认 Qwen/Qwen3.5-27B）。无任何 Agent 读取或打印 token 值。

### 78.3 修复（SESSION P-R1C 行为改动 · 最小）

```text
1. src/main.cpp：任何 --qml-* harness 进程在创建任何 controller 之前
   qunsetenv("MODELSCOPE_API_KEY") + qunsetenv("MODBUSLENS_MODELSCOPE_MODEL")。
   harness 自己声明配置真值；production（无 --qml- 参数）保持 ambient 环境不变
   ⇒ live inference 仍为 Human-gated 产品行为。
2. src/main.cpp runConsentCheck 入口 guard：凭据对 harness 可见 ⇒
   CONSENTFAIL ... refusing to run（exit 1），入口即拒绝、零调度。
3. CMakeLists.txt：qml_consent_check 补 ENVIRONMENT
   "QT_ASSUME_STDERR_HAS_CONSOLE=1"（平台语义不变，仅补 stderr 契约）。
R07 断言本身零改动 —— 它正是本轮的探测点。
```

### 78.4 验证（真实命令与输出）

```text
fresh tree        = build/acceptance/session-p-r1c-release/（本会话新建，
                    release 等价配置 + MODBUSLENS_PYTHON_EXECUTABLE=D:/Anaconda3/python.exe）
configure         = RC 0（Configuring done 33.8s / Generating done）
build             = RC 0（417 steps 全绿，0 error）
targeted          = ctest -R ^qml_consent_check$（ambient token PRESENT = 崩溃原配置）
                    ⇒ #60 Passed 3.73s
negative control  = MUTATION-NX1（注释 token qunsetenv）⇒ rebuild ⇒
                    CONSENTFAIL: harness credential contract violated（exit 1 / 1.58s，
                    入口即拒绝 ⇒ 零调度）⇒ 精确逆向还原 ⇒ rebuild ⇒
                    #60 Passed 5.58s；grep MUTATION-NX1 = 无残留
candidate_transport = #30 Passed 1.34s（P01–P24，注入 fake client，零 socket）
full Release ctest  = 见 78.5
```

### 78.5 全量回归与状态

```text
full Release ctest（fresh tree build/acceptance/session-p-r1c-release/）
  = 63/63 PASS / 0 failed / exit 0 / 201.14s（63 Passed，无 Timeout / Not Run；
    deployment_startup_check #33 Passed 35.51s，candidate 由 canonical target 重建）
candidate provenance = source exe ≡ candidate exe（byte-identical）：
  modbuslens.exe SHA-256
  1e9a0b99ed9ff02121a41cffeec903fbc7ee811759c94393b8fef63c56182286
behavior commit = 613a32a「M12: complete session P transport and sanitize
  harness credentials」（9 files / +1234 −40，parent 20a9129，NO AMEND；
  内容 = SESSION P transport slice（§76）+ §77 冻结修复 ①③⑤ + §78 P-R1C
  harness credential sanitization —— 三者同属 §77.0 单一授权计划，且已在
  同一 fresh tree 上整体验证）
Debug full = NOT RUN（本轮未声称；Debug 归档按既有惯例由 Human 外部 shell 复验）
```

**状态（P-R1C 完成时点）**：M12-C C1b = COMPLETE / HUMAN ACCEPTED（未变）·
M12-C = IN PROGRESS · **M12-C C2 = IN PROGRESS** · foundation / adapter+parser /
orchestration+consent+display / **production transport = IMPLEMENTED /
AUTOMATED PASS（P01–P24，零 socket）** · **harness credential sanitization =
IMPLEMENTED / AUTOMATED PASS（含 REAL RED 负向对照）** · **C2 Human acceptance =
PENDING** · PendingReview 成功 Human 视觉 = NOT REACHED / NOT VERIFIED ·
**Live ModelScope = NOT RUN / NOT VERIFIED（Agent 永不执行；Human-only）** ·
C3 · M12-D = NOT STARTED / NOT AUTHORIZED · canonical package = NOT CREATED ·
REAL MODBUS HARDWARE = NOT VERIFIED · **verified LKGC = `19f9738…`（UNCHANGED）** ·
无 push / 无 tag / 无 amend。**SESSION P-R1C = COMPLETE — STOP。**
剩余 SESSION P 授权内未完项（待后续会话）：Human live ModelScope gate 准备（G）
与 Human 对 C2 的 functional/visual 验收。

### 78.6 环境观察（记录在案 · 未修复 · 不阻塞 · 不复用该树）

```text
对 build/acceptance/session-p-r1b-release/（WorkBuddy 2026-09-30 的 fresh 树）
重新执行 configure（无论 --regenerate-during-build 还是普通 -S/-B）⇒
cmake.exe 进程以 0xC0000409（UCRT fail-fast/abort）硬中止，stdout/stderr 零输出，
build.ninja 不被改写。实测隔离：
  · 同一 cmake.exe --version 正常；
  · 同一源树 configure 到全新目录（scratch / session-p-r1c-release）完全正常
    （RC 0，Generating done）⇒ 非 CMakeLists / 非 preset / 非 cmake 本体缺陷；
  · 干净最小环境仍复现 ⇒ 非 ambient 环境注入。
结论：abort 与该树既有 cache/CMakeFiles 状态相关；精确机制 UNKNOWN
（与 §77.3 RCA-C 同属"旧树状态、机制未知、不修复不复用"类别）。
处置：P-R1C 验证全部改在新建 fresh tree（78.4）完成；
build/acceptance/session-p-r1b-release/ 保留为 14:53 失败证据，不再复用。
```

**本节动作边界**：SESSION P-R1C 范围 = §77 授权 A/B/C（deterministic acceptance
repair）内的 RCA + harness 修复 + 验证。**未**执行真实 inference（修复后的
deterministic gate 结构上不可能再调度）· 未读取/打印 Human token 值 ·
未开始 C3 · 未写 DeviceProfile · 未做 Candidate persistence · 未做 M12-D ·
未推进 verified LKGC · 未创建 canonical package · 未 push / 未 tag / 未 amend。

---

## 79. M12-C C2 — HUMAN LIVE MODELSCOPE ACCEPTANCE（SESSION P-R1F · docs-only 归档）

> 性质：append-only 归档。仅记录 Human 权威报告与授权解释；**不**记录任何未被
> Human 报告的事实（不发明 latency / provider status code / response body /
> token 细节 / 计费配额 / 网络包证据 / Human 未报告的 Candidate 字段内容）。
> **无 token 值入档 · 无 raw provider response 入档。**

### 79.1 接受的 live-smoke 目标（provenance 引用 = P-R1E handoff，本轮零新生成）

```text
candidate  = build\acceptance\session-p-r1d-ratify\candidate\ModbusLens\
             （canonical 生成器 modbuslens_generate_candidate.cmake 从零重建）
exe        = 6,345,517 B / SHA-256
             44130e8a78f1686b20b3bebc129204434e8e09b9e150e15da131c302cefa68ba
qwindows   = 804739071bba619b4a4312b5bb29a142545a64c4c80218e5b2e6672ad33ee8ac
pdfium     = d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b
manifest   = 1713 files 条目（root 1714 files / 91 dirs）
startup gate = deployment_startup_check Passed（P-R1D 轮 #33，26.37s @ HEAD ca01025）
```

### 79.2 Human 权威报告（逐字）

```text
「Agree 后进入 Running，随后进入 PendingReview；
Candidate 可见；
Device Profile 未变化；
无可见错误。」
```

### 79.3 授权解释（逐字归档，禁止扩写）

```text
Human live ModelScope inference        = PASS / HUMAN VERIFIED
PendingReview successful live Candidate visual = HUMAN PASS
C2 Human functional AI extraction      = PASS
Verified DeviceProfile mutation during live extraction = NONE OBSERVED / HUMAN PASS
M12-C C2 overall Human acceptance      = PASS / HUMAN ACCEPTED
```

### 79.4 治理区分（与 ISSUE-020 的事件分立，历史分级不改写）

```text
P-R1 live smoke = HUMAN AUTHORIZED / EXPLICIT CONSENT（产品同意流）/ PASS。
本 smoke 是 Human 显式授权、经产品同意门发生的请求，
**不**属于 ISSUE-020 记录的 P-R1C 未授权事件。
既有历史分级原样保留：
  P-R1C governance                = VIOLATION CONFIRMED
  ZCode P-R1C Run A 事件分级      = STRONGLY SUPPORTED
  WorkBuddy 14:53 事件分级        = LIKELY
ISSUE-020 已追加最小澄清批注（不改写任何历史证据）。
```

### 79.5 归档时点状态（M12-C C2 收口）

```text
M12-C C1a  = COMPLETE / HUMAN ACCEPTED
M12-C C1b  = COMPLETE / HUMAN ACCEPTED
M12-C C2   = COMPLETE / HUMAN ACCEPTED
C2 内部分层：
  Candidate/Evidence foundation       = IMPLEMENTED / AUTOMATED PASS
  ModelScope adapter + strict parser  = IMPLEMENTED / AUTOMATED PASS
  Production orchestration            = IMPLEMENTED / AUTOMATED PASS
  Cloud consent                       = IMPLEMENTED / AUTOMATED PASS / HUMAN PASS
  Production ModelScope transport     = IMPLEMENTED / AUTOMATED PASS / HUMAN LIVE PASS
  Process-env credential              = IMPLEMENTED / AUTOMATED PASS / HUMAN LIVE PASS
PendingReview Candidate live visual    = HUMAN PASS
C2 Human functional AI extraction      = PASS
C3（Accept / Edit / Reject）           = NOT STARTED / NOT AUTHORIZED
M12-D（Manual Q&A）                    = NOT STARTED / NOT AUTHORIZED
M12-C overall                          = IN PROGRESS（C3 未启动，不推断收口）
canonical package                      = NOT CREATED
REAL MODBUS HARDWARE                   = NOT VERIFIED
verified LKGC                          = 19f9738c980e0a8a31b557c346fb50a4af711cab
                                         （UNCHANGED；本节 docs-only 提交永不作 LKGC）
```

**本节动作边界（docs-only）**：仅归档 Human 权威结果与授权解释；
未改 source / CMake / tests · 未 build / configure / CTest · 未生成新 candidate ·
未执行任何 ModelScope 请求 · 未读取凭据值 · 无 token 值 / raw response 入档 ·
未开始 C3 / M12-D · 未创建 canonical package · 未推进 verified LKGC ·
未 push / 未 tag / 未 amend。

---

## 80. M12-C C2 — VERIFIED LKGC ADVANCE THROUGH C2（Session P-R1F · docs-only 归档）

> 性质：append-only 归档。仅记录 Human 授权与 Git 实测验证；LKGC 推进本身是
> canonical docs 的状态转移，**不是**任何新行为。

### 80.1 Human 授权（逐字）

```text
授权：将 M12-C C2 Human acceptance 作为新的 verified behavior baseline，
并将 verified LKGC 从
19f9738c980e0a8a31b557c346fb50a4af711cab
推进到
613a32a0d6ca71ac53185779b7d5ab3b80c82870。

本授权仅推进 verified LKGC；
不授权 C3、M12-D、package、tag 或 push。
```

### 80.2 Git 实测验证（授权目标合法性）

```text
git cat-file -t 613a32a…                       = commit
git merge-base --is-ancestor 613a32a… HEAD     = exit 0（是 HEAD 祖先）
613a32a~1..613a32a changed paths               = 9 files / +1234 −40
  （CMakeLists.txt · src/main.cpp · ModelScopeCandidateRunner.{h,cpp} ·
   ModelScopeExtractionTransport.{h,cpp} · ModelScopeHttpClient.{h,cpp} ·
   tests/test_candidate_transport.cpp）⇒ behavior-bearing
613a32a..HEAD = 3 个提交（c78c960 · ca01025 · 6ab5abc），
  非 docs 路径 diff = 空 ⇒ 其后提交全部 docs-only，永不作 LKGC
⇒ 613a32a = C2 最后一个 behavior-bearing tree，为合法 LKGC 目标。
```

### 80.3 基线证据链（为什么是 613a32a）

```text
· P-R1D commit 审计：613a32a 内容 = SESSION P transport + §77 修复 + P-R1C
  harness sanitization，零凭据物料、零 docs 混入。
· P-R1D fresh ratification（build/acceptance/session-p-r1d-ratify/，凭据缺席）：
  configure RC0 · build RC0（417/417）· inventory 63 ·
  qml_consent_check / qml_consent_check_windows / candidate_transport 全 PASS ·
  full Release 63/63 PASS / 0 failed / exit 0 / 182.9s（deployment_startup_check
  Passed 26.37s）。
· P-R1F Human live acceptance（§79）：live smoke = PASS / HUMAN VERIFIED，
  C2 overall = PASS / HUMAN ACCEPTED —— Human 验收的目标 candidate 即由该
  behavior 内容生成。
```

### 80.4 推进后的 canonical 状态

```text
verified LKGC = 613a32a0d6ca71ac53185779b7d5ab3b80c82870（Human authorized）
前一 verified  = 19f9738c980e0a8a31b557c346fb50a4af711cab（转历史链一环，原文保留）
M12-C C2       = COMPLETE / HUMAN ACCEPTED（新 verified behavior baseline）
M12-C overall  = IN PROGRESS（C3 未启动，不推断收口）
C3 · M12-D     = NOT STARTED / NOT AUTHORIZED（本授权明确不授权）
canonical package = NOT CREATED · tag = 仅 v1.0.0 · push = 未发生
REAL MODBUS HARDWARE = NOT VERIFIED
```

**本节动作边界（docs-only）**：仅归档授权 + Git 验证 + 状态转移记录；
未改 source / CMake / tests · 未 build / test · 未执行任何网络请求 ·
未创建 canonical package · 未 tag / 未 push · 本节所在提交 docs-only 永不作 LKGC。

---

## 81. M12-C C3 — HUMAN CONTRACT FREEZE C3-H1..H10（SESSION C3-R1 · docs-only 归档）

> 性质：append-only 归档。Human 已明确授权以下 C3 Human contract decisions，
> 并授权将其 docs-only 归档；**本授权本身不授权 behavior implementation**。
> 下一会话先 docs-only 归档并复核 repository truth（本节即该归档）；
> 完成并由 ChatGPT/Human review 后，才另行授权 C3 first behavior slice。

### 81.0 Human 授权原文（逐字）

```text
同意以下 M12-C C3 Human contract decisions，并授权后续将其
docs-only 归档；本授权本身不授权 behavior implementation。

C3-H1 — Review granularity
M12-C C3 v1 采用单 Candidate 独立裁决。
不实现 batch Accept / batch Reject。
未来 RegisterEntryCandidate 或 batch workflow 另行授权。

C3-H2 — Accept semantics
Accept 是 Human 对当前单个 Candidate 的显式确认。

对当前已实现的 ProfileFieldCandidate：
Accept 将 proposed value 写入当前选中 Device Profile 的
ProfileController draft 对应字段。

UI 在 Accept 前必须同时让 Human 看见：
- Candidate proposed value
- 当前目标 Profile / field
- 当前 draft value

Accept 可以覆盖当前 draft value，
但不得直接修改 persisted Profile，
不得 auto-save。

持久化继续严格沿用现有 MANUAL SAVE：
Human 之后显式 Save 才进入持久化 verified Profile。

未涉及的 Profile 字段必须保持不变。

C3-H3 — Evidence freshness gate
每次 Accept / edited-confirm 前，
必须针对当前 canonical Manual truth
重新执行 deterministic evidence validation。

至少验证：
document identity
content identity / contentHash
exact excerpt
canonical location / round-trip

若 source 已不存在、content identity 不匹配、
excerpt/location 无法重新验证或 evidence 已失效：

操作必须原子失败；
Profile draft 不得变化；
Candidate 保持 PendingReview，
供 Human 检查、重新提取或处理。

不得 silent stale acceptance。

C3-H4 — Profile conflict semantics
当前 ProfileFieldCandidate v1 不增加 persisted profile fingerprint。

Candidate 不自动绑定并写入某个隐藏的旧 Profile 状态。

Human 点击 Accept 时，
权威目标就是 UI 当前明确显示的 selected Profile + current draft field。

如果 selected Profile 或该字段值自 Candidate 产生后已经变化，
系统不得静默自动应用；
UI 必须显示当前目标和当前值，
Human 此时的显式 Accept 才表示"以 Candidate value 覆盖当前 draft value"。

没有当前有效 Profile target 时不得 Accept。

这一规则仅冻结当前 ProfileFieldCandidate v1。
未来 RegisterEntryCandidate 的并发/冲突语义必须重新审计，
不得从本规则自动外推。

C3-H5 — Accepted lifecycle
成功 Accept 后：
Candidate 在当前 session 中标记为 Accepted/consumed，
并移出 PendingReview 集合；
同一 Candidate 不得再次 Accept。

Accepted 状态/audit 不持久化到 DeviceProfile JSON、
manual metadata 或其它长期存储。

进程结束后该 Candidate/audit 消失。

C3-H6 — Rejected lifecycle
Reject 必须产生零 DeviceProfile mutation。

成功 Reject 后：
Candidate 在当前 session 中标记为 Rejected/consumed，
并移出 PendingReview 集合。

C3 v1 不提供 Reject undo。
Reject 不影响未来重新运行 extraction；
新的成功 extraction 可以重新产生新的 Candidate。

Rejected 状态/audit 不长期持久化。

C3-H7 — Durable provenance
M12-C v1 不把 Candidate evidence / AI provenance /
Accept/Reject audit 写入 DeviceProfile schema。

Evidence 在 Human review 与 deterministic validation 阶段仍是必需的，
但 accepted Profile truth 的 durable provenance
明确 DEFER 到未来独立 Human decision。

因此本决定：
不授权 DeviceProfile schema version change，
不授权 migration，
不授权新的 persistent audit store。

不得把"当前不持久化 provenance"
写成"provenance 永远不需要"。

C3-H8 — Edit authority semantics
Edit 属于 C3，但不进入第一个 Accept/Reject implementation slice。

后续 Edit slice 冻结为：

Human Edit 后的值属于 Human-authored / Human-confirmed value，
不得继续表示成 AI-proposed value。

原 AI proposal + Evidence
在当前 review session 内保留作为上下文。

Human 可以把值修改为与 AI proposal 不同的值；
Evidence 此时只是 source context，
不得声称 Evidence 自动证明 Human 修改后的值。

edited-confirm 必须：
- 由 Human 显式确认
- 走与 Accept 相同的 deterministic evidence freshness gate
- 走相同 DeviceProfile validation
- 原子失败
- 失败后保持可继续编辑/审核
- 成功后只写 ProfileController draft
- 不 auto-save

C3 v1 不持久化原 AI proposal、Edit audit 或 provenance。

C3-H9 — Undo / re-review
第一个 C3 slice 不增加专用 Accept undo / Reject undo。

Accept 尚未 Save 时，
继续使用现有 Profile draft Discard 语义撤销 Profile draft 变化。

Reject 后如 Human 希望重新考虑，
通过新的 extraction 产生新的 Candidate；
不恢复旧 Rejected Candidate。

C3-H10 — First implementation slice boundary
第一个 C3 behavior slice 只实现：

ProfileFieldCandidate
+
当前实际可产的 Manufacturer target
+
Accept
+
Reject
+
evidence revalidation
+
ProfileController draft integration
+
existing DeviceProfile validation
+
existing MANUAL SAVE workflow

明确不实现：

Edit UI
batch review
RegisterEntryCandidate
DeviceProfile schema change
durable provenance/audit
auto-save
M12-D
package
tag
push
LKGC advancement

candidateCard 获得受控 ProfileController mutation path
是本 C3 slice 的有意架构变化，
但必须保持唯一权威链：

Human action
→ C3 review controller
→ ProfileController draft
→ validateDeviceProfile
→ existing explicit Save
→ ProfileStore

不得建立第二条 DeviceProfile mutation/persistence pipeline。

本授权只冻结 C3 contract。
下一会话先 docs-only 归档 C3-H1..H10 并复核 repository truth；
完成并由 ChatGPT/Human review 后，
才另行授权 C3 first behavior slice。

verified LKGC 保持：
613a32a0d6ca71ac53185779b7d5ab3b80c82870

M12-D / package / tag / push 均未授权。
```

### 81.1 定性（HUMAN-APPROVED C3 CONTRACT）

C3-H1..H10 = **HUMAN-APPROVED C3 CONTRACT**（非 pre-existing canonical，非工程推断；
来源 = 本节 81.0 的 Human 逐字授权）。自此，C3-R0 审计报告中分类为
REQUIRES HUMAN DECISION 的全部 P0-C3 项均有 Human 裁定；
§47.11 的 PROPOSED lifecycle 建议由 H1..H6/H9 的具体冻结取代（历史原文保留）。

### 81.2 与既有决策的关系（显式记录，禁止机械替换）

| 新决定 | 解决的既往未决项（C3-R0 审计 §P） | 关系定性 |
| --- | --- | --- |
| C3-H1 | P0-C3-A（粒度） | RESOLVED：v1 = 单 Candidate 裁决；batch/RegisterEntryCandidate 另行授权（NOT silently deferred——明确排除出 v1） |
| C3-H2 | P0-C3-B（变更语义）+ P0-C3-F（持久化时机） | RESOLVED：Accept = draft 覆盖 + UI 三方对照（proposal/target/draft）；不触 persisted、不 auto-save；MANUAL SAVE 冻结纪律原样沿用 |
| C3-H3 | P0-C3-G（陈旧策略）+ §47.11.6（失败后 Candidate 可修正） | RESOLVED：Accept/edited-confirm 前 evidence freshness gate（document identity + content identity + excerpt + location round-trip）；失败 = 原子失败 + draft 不变 + Candidate 保持 PendingReview；禁止 silent stale acceptance |
| C3-H4 | P0-C3-H（目标已变冲突） | RESOLVED：v1 无 persisted fingerprint；权威目标 = UI 当前显示的 selected Profile + current draft field；不得静默自动应用；无有效 target 不得 Accept；**仅限 ProfileFieldCandidate v1，RegisterEntryCandidate 须重新审计** |
| C3-H5 | P0-C3-D（Accepted 生命周期/audit） | RESOLVED：Accepted/consumed + 移出 PendingReview + 不得再次 Accept + 零持久化（进程结束即消失） |
| C3-H6 | P0-C3-E（Rejected 生命周期） | RESOLVED：零 Profile mutation + Rejected/consumed + 移出集合 + 无 undo + 不影响未来 extraction + 不持久化 |
| C3-H7 | P0-C3-I（durable provenance）+ schema 问题 | RESOLVED（v1 = 不持久化）：无 schema version change、无 migration、无 persistent audit store；「当前不持久化」≠「永远不需要」（DEFER 语义冻结） |
| C3-H8 | P0-C3-C（Edit 权威语义） | RESOLVED + DEFERRED-IMPLEMENTATION：Edit 不进首切片；后续 Edit slice 的权威语义已冻结（Human-authored 值、原提案+evidence 仅作会话上下文、edited-confirm 走同 gate/validator/原子失败/draft-only/不 auto-save、v1 不持久化 Edit audit） |
| C3-H9 | P0-C3-J（undo/re-review） | RESOLVED：无专用 undo；未 Save 用既有 draft Discard；重新考虑 = 新 extraction |
| C3-H10 | C3-R0 §Q 首切片提案 + candidateCard 受控引用设计点 | RESOLVED + BOUNDED：首切片范围逐项冻结（含明确不实现清单）；candidateCard 获得**受控** ProfileController mutation path = 有意架构变化；唯一权威链 = Human action → C3 review controller → ProfileController draft → validateDeviceProfile → existing explicit Save → ProfileStore；禁止第二条 pipeline |

未被取代（原样保留）：§4/§13/§15 冻结能力句（C3-H 系是其具体化）· §47.12 M12-C/M12-D
hard boundary · §66 H1–H10（C2 侧契约）· §17.2 M12-C exit 条件（C3 完成后链路可用）·
§67/§71 已实现层语义 · ISSUE-020 历史与澄清批注。

### 81.3 由此产生的实现边界（工程含义，本节不设计）

- 权威链唯一：`Human action → C3 review controller → ProfileController draft →
  validateDeviceProfile → existing explicit Save → ProfileStore`；
  candidateCard 与 ProfileController 之间的受控引用是 H10 明示的有意架构变化。
- 需要的域扩展 = additive：`CandidateLifecycleState` 增加Accepted/Rejected（consumed）
  状态 + 消费语义；Candidate 域不新增持久化句柄（H4/H5/H7 不变）。
- evidence freshness gate 复用既有确定性验证组件
  （`locateUniqueExcerpt` round-trip + documentId/contentHash 比对），
  不发明第二套 evidence 词汇。
- 首切片验收必须覆盖（映射 C3-R0 §N 分类）：C3-A/B/C/E/I/J/L/M/N（REQUIRED）
  + C3-K 由 H3/H4 取代（evidence gate + 显式目标对照）+ F/G/H NOT APPLICABLE
  （Edit 不在首切片）+ 现有全量回归零回归。

### 81.4 状态（本节归档时点）

```text
M12-C C1a / C1b / C2   = COMPLETE / HUMAN ACCEPTED
M12-C C3               = CONTRACT FROZEN（C3-H1..H10）· IMPLEMENTATION NOT STARTED /
                         NOT AUTHORIZED（待 ChatGPT/Human review 本归档后另行授权）
M12-C overall          = IN PROGRESS
M12-D                  = NOT STARTED / NOT AUTHORIZED
canonical package      = NOT CREATED
REAL MODBUS HARDWARE   = NOT VERIFIED
verified LKGC          = 613a32a0d6ca71ac53185779b7d5ab3b80c82870（UNCHANGED）
无 push / 无 tag / 无 amend；本节所在提交 docs-only 永不作 LKGC。
```

---

## 82. M12-C C3 — FIRST BEHAVIOR SLICE ARCHIVE（SESSION C3-R2 · behavior + docs）

> 性质：append-only 归档。授权 = Human「授权：启动 M12-C C3 first behavior slice，
> 严格按 C3-H1..H10 和 H10 冻结范围实施；允许行为代码、测试、CMake/QML 的最小必要修改，
> 以及 RED → GREEN → negative-control → targeted → fresh Release full regression →
> behavior commit → candidate/deployment → docs archive；不授权 Edit / batch /
> RegisterEntryCandidate / schema·provenance persistence / M12-D / LKGC advancement /
> package / tag / push。」

### 82.1 起始基线（实测）

```text
HEAD              = b6cdd678052e88a3907289cfc8738c3a1987e223（§81 归档提交）
verified LKGC     = 613a32a0d6ca71ac53185779b7d5ab3b80c82870（UNCHANGED）
tracked clean · cached 空 · diff --check rc 0 · tags = v1.0.0 · ls-files build = 空
```

### 82.2 实现架构（恢复的所有权 + 唯一权威链）

```text
CANDIDATE OWNER            = core::ProfileFieldCandidate（core/candidate/CandidateExtraction.h）
CANDIDATE SET OWNER        = CandidateExtractionController（pending 集 + 会话消费列表）
CANDIDATE EVIDENCE VALIDATOR = core::revalidateCandidateEvidence（新，复用
                             locateUniqueExcerpt 的同一 H6 规则）
PROFILE DRAFT OWNER        = ProfileController（m_draft；candidate-copy 纪律）
PROFILE VALIDATION OWNER   = core::validateDeviceProfile（15 规则，pure）
PROFILE PERSISTENCE OWNER  = ProfileStore（QSaveFile；经既有 saveCurrent）
MANUAL CANONICAL TEXT OWNER = ManualStore::loadText(contentHash)（loadAll 解析 documentId）
QML CANDIDATE OWNER        = candidateCard（DeviceProfilePage.qml）
ERROR SURFACE              = 控制器新增 lastReviewError/lastReviewErrorToken +
                             ProfileController 既有 lastActionError(+Token)
TEST SEAMS                 = 注入 ctor + 计数 fake runner + setManagedRootOverride ×2 +
                             QML 检查管道
唯一权威链（C3-H10）       = Human action → C3 review controller →
                             ProfileController draft → validateDeviceProfile →
                             explicit Human Save → ProfileStore
                           （candidateCard→profileController 受控引用 = H10 授权的
                             有意架构变化；QML 不触 model/store，无第二条 pipeline）
```

改动文件：core CandidateExtraction.{h,cpp}（lifecycle 消费态 + evidence 重验证）·
CandidateExtractionController.{h,cpp}（review API + 消费列表 + automation seed）·
ProfileController.{h,cpp}（applyCandidateField 受控写）· Main.qml（1 行绑定）·
DeviceProfilePage.qml（三方对照 + Accept/Reject + 错误面）· main.cpp（QML 门禁）·
CMakeLists.txt（candidate_review 目标 + 2 个 QML 门禁）·
tests/test_candidate_review.cpp（新）· tests/test_candidate_orchestration.cpp（o18 修正）。

### 82.3 验证链（全部真实命令与输出，凭据缺席）

```text
REAL RED（实现前）
  candidate_review          = exit 18（3 passed / 18 failed；失败全部为
                              「review API 不存在」运行时语义断言）
  qml_candidate_review_check = exit 1（seed API 缺失 + 控件缺失共 11 条 REVIEWFAIL）
GREEN
  candidate_review          = exit 0，21 passed / 0 failed（C3-R2-01..20 全覆盖，
                              r2_19/r2_20 结构性证据内联）
  qml_candidate_review_check = exit 0，CANDIDATE REVIEW CHECK PASS (R1..R5)，
                              QML 诊断 0（offscreen + windows 双跑均绿）
negative control（MUTATION-NX2：注释 acceptCandidate 的 evidence gate）
  = REAL RED exit 3（18 passed / 3 failed，恰好 r2_08/09/10 —— 陈旧证据可达
    controlled draft 路径）→ 精确逆向（未用 checkout/restore/reset）→
    residue 0（grep 无残留）→ 复绿 21/21
targeted 回归               = 25/25 PASS / exit 0 / 64.4s（含 candidate_review、
                              2 个 QML 门禁、extraction/adapter/orchestration/transport、
                              device_profile、profile_controller、manual 系、
                              consent/editor/register_map/geometry/nav/focus/smoke、ai_client）
  o18 修正（契约驱动，非削弱）：O 时代「controller 无 review 动作」被 §81 取代——
    acceptCandidate/rejectCandidate/automation seed 现为 Human 批准动作；
    o18 继续禁止 Edit 动作与一切持久化形态动作（save/persist/store/commit）。
fresh Release full           = 66/66 PASS / 0 failed / exit 0 / 184.9s
  （inventory = 66：63 + candidate_review + 2 个 QML 门禁；
    deployment_startup_check #34 Passed 31.25s）
```

### 82.4 提交与候选 provenance

```text
behavior commit = 61f641ef2d9045cd90dd7598dcf78ab36b5af800
                  「M12: add C3 candidate accept and reject」
                  （12 files / +1879 −7，parent b6cdd67…，NO AMEND，内无 docs）
提交后树一致性   = git diff HEAD -- src tests CMakeLists.txt 为空 ⇒ 被测源 ≡ 提交树
candidate        = buildcceptance\session-c3-r2-release\candidate\ModbusLens  exe            = 6,443,540 B / SHA-256
                   bd6133f0ad24091df152c8fe0a7789d3c5b3f9e42d89231add409ed6df2ea79f
                   （source exe ≡ candidate exe，byte-identical）
  manifest       = 1713 entries（root 1714 files），manifest 内 exe 哈希一致
  qwindows.dll   = 804739071bba619b4a4312b5bb29a142545a64c4c80218e5b2e6672ad33ee8ac
  pdfium.dll     = d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b
deployment gate  = deployment_startup_check POST-COMMIT Passed 25.31s
                   （sanitized env：PATH = candidate root + 系统目录；
                     qwindows.dll 确证从 candidate 树加载；启动零 ModelScope 请求）
```

### 82.5 安全 / 权威审计（提交前全部通过）

```text
无 live ModelScope · 无凭据读取（新增代码 0 处凭据）· C3 路径零网络
（QNetworkAccessManager/Request 在 review 路径出现次数 = 0；r2_17 运行时证明）
无 raw provider response 使用 · 无 Candidate/audit 持久化（r2_18：Profile JSON
键集 = schema 键集）· 无 schema change / version bump / migration
（DeviceProfile.h / ProfileStore.h 零改动）· 无 QML 直写 model/store ·
无第二条持久化 pipeline · 无 auto-save（r2_06）· 无 Edit（r2_13 + QML 扫描）·
无 batch（C3-H1）· 无 RegisterEntryCandidate（源码不存在）· 无 M12-D（r2_20 +
QML 扫描）· Accept/Reject 对既有 Candidate 零 provider 调用（r2_17）
```

### 82.6 HUMAN ACCEPTANCE CHECKLIST（Agent 不执行；候选 = 上表 candidate）

前置：Human 自行决定是否用 live ModelScope 产生真实 Candidate（沿用既有产品同意流；
那一步是 Human 动作）。或由 Human 选择任何已导入 manual 触发提取。

```text
A. 通过已验收的 C2 流程生成一个真实 PendingReview Manufacturer Candidate。
B. candidateCard 明确显示：目标 Profile/Manufacturer 字段、当前草稿值、
   Candidate 提议值、接受、拒绝；且无 Edit 控件。
C. 拒绝：Candidate 从待审核消失；Manufacturer 草稿不变；Save 状态不变；
   持久化 Profile 不变。
D. 再次提取，产生新 Candidate。
E. 接受：点击前可见当前草稿值；Accept 只改 Manufacturer 草稿；
   Candidate 消失；Profile 变 dirty；无自动保存。
F. Accept 后 Discard：草稿/持久化状态恢复。
G. 重新生成并 Accept，然后 Human 显式 Save：重启/重载确认 Manufacturer
   已按正常 Profile 工作流持久化。
H. 其余 Profile/Register 字段零变化。
I. 1280x937 与 1000x700 下无可见裁切/越界。
J. 界面无 M12-D / Edit / batch UI。
```

### 82.7 状态（本节归档时点）

```text
M12-C C3               = IN PROGRESS
C3 first behavior slice = IMPLEMENTED / AUTOMATED PASS
C3 Accept              = IMPLEMENTED / AUTOMATED PASS
C3 Reject              = IMPLEMENTED / AUTOMATED PASS
C3 Edit                = NOT IMPLEMENTED / DEFERRED（C3-H8 冻结语义）
C3 batch               = NOT IMPLEMENTED / DEFERRED（C3-H1）
RegisterEntryCandidate = NOT IMPLEMENTED / NOT IN CURRENT SOURCE
Durable provenance     = DEFERRED / NO SCHEMA CHANGE（C3-H7）
C3 Human visual/functional = PENDING（§82.6 checklist）
M12-C overall          = IN PROGRESS
M12-D                  = NOT STARTED / NOT AUTHORIZED
canonical package      = NOT CREATED
verified LKGC          = 613a32a0d6ca71ac53185779b7d5ab3b80c82870（UNCHANGED；
                         behavior 提交不是 LKGC）
无 push / 无 tag / 无 amend。
```

---

## 85. M12-C C3 — FIRST-SLICE HUMAN ACCEPTANCE（SESSION C3-R2C · docs-only 归档）

> 性质：append-only 归档。Human 授权（逐字）：「授权：归档 M12-C C3 first behavior
> slice Human acceptance PASS；仅 docs-only，不推进 LKGC，不开始 Edit，不开始 M12-D。」
> 仅记录 Human 观察到的证据；**不**发明时序、内部状态、网络包、provider HTTP 状态或
> Human 未观察的字段。本节为对 §82.6 PENDING 状态的 current superseding note；
> §82 历史原文不改写。

### 85.1 验收对象边界（artifact boundary）

```text
Human acceptance 覆盖的候选 = R2B 候选（§84.3）：
  buildcceptance\session-c3-r2b-postcommit-release\candidate\ModbusLens\modbuslens.exe
  exe SHA-256 = 0bc2e6a23b7b0142612c8e31387bbc603b7afbd3ad2943a55405c0eb5ae083fa
其行为提交 = bb996a5bfc5416e3392c6fd709508e21aeab62c2（§84）
本 docs 归档提交本身**不是**被测行为，**不是** LKGC。
```

### 85.2 Human 权威验收证据（逐条归档，分类 = HUMAN PASS）

```text
CHECK 1 — REJECT
  起始：Profile「1111」已打开；Manufacturer 草稿为空；真实 PendingReview
  Candidate 可见（manufacturer: ACME）。Human 点击 Reject。
  观察：Pending Candidate 消失；pending count = 0；Manufacturer 保持为空；
  无可见错误。
  ⇒ C3 Reject = HUMAN PASS；Reject 零可见 Profile mutation = HUMAN PASS。

CHECK 2 — ACCEPT TO DRAFT
  Human 重新提取产生新真实 Candidate（manufacturer: ACME）；点击 Accept。
  观察：Manufacturer 草稿变为「ACME」；Candidate 消失 / pending count = 0；
  UI 显示「未保存修改」；应用未自动保存；无可见错误。
  ⇒ C3 Accept to draft = HUMAN PASS；Candidate consumption = HUMAN PASS；
  No auto-save = HUMAN PASS。

CHECK 3 — DISCARD（使用含「放弃修改」常驻动作的 R2B 候选）
  Human 点击 Accept 后点击「放弃修改」。
  观察：Manufacturer 从「ACME」恢复为先前值（本验收 Profile 中为空）；
  「未保存修改」消失；「放弃修改」回到禁用态；Pending Candidate count 保持 0；
  已消费 Candidate 未复活；无可见错误。
  ⇒ Standing Discard = HUMAN PASS；Accept-before-Save rollback = HUMAN PASS；
  Consumed Candidate non-resurrection = HUMAN PASS。

CHECK 4 — EXPLICIT SAVE + RESTART
  Human 再产生新 Candidate（manufacturer: ACME）→ Accept → 显式 Save →
  关闭应用 → 重启应用 → 重新打开 Profile「1111」。
  观察：Manufacturer 仍显示「ACME」；验收的持久化跨重启存活；
  Pending Candidate count = 0；无可见错误。
  ⇒ Explicit Save persistence = HUMAN PASS；Restart persistence = HUMAN PASS；
  既有 Profile 持久化工作流 = HUMAN FUNCTIONAL PASS。

CHECK 5 — SCOPE BOUNDARY
  本次验收期间：未使用 C3 Edit workflow；未使用 batch review；未使用
  RegisterEntryCandidate workflow；未使用 M12-D workflow。
  ⇒ **不得**推断这些 deferred 特性已实现。
```

### 85.3 C3 first-slice 最终分类

```text
C3 contract                 = FROZEN / HUMAN APPROVED（§81，C3-H1..H10）
C3 first behavior slice     = COMPLETE / HUMAN ACCEPTED
C3 Accept                   = IMPLEMENTED / AUTOMATED PASS / HUMAN PASS
C3 Reject                   = IMPLEMENTED / AUTOMATED PASS / HUMAN PASS
Standing Discard            = IMPLEMENTED / AUTOMATED PASS / HUMAN PASS
Explicit Save persistence   = AUTOMATED FOUNDATION PASS / HUMAN PASS
C3 Edit                     = NOT IMPLEMENTED / DEFERRED（C3-H8 冻结语义）
C3 batch                    = NOT IMPLEMENTED / DEFERRED（C3-H1）
RegisterEntryCandidate      = NOT IMPLEMENTED / NOT IN CURRENT SOURCE
Durable provenance          = DEFERRED / NO SCHEMA CHANGE（C3-H7）
C3 overall                  = IN PROGRESS —— 原因：canonical C3 capability
                              （§13/§17.2）仍包含 Edit，而 Edit 尚未实现或
                              被 Human 验收
M12-C overall               = IN PROGRESS
M12-D                       = NOT STARTED / NOT AUTHORIZED
verified LKGC               = 613a32a0d6ca71ac53185779b7d5ab3b80c82870
                              （UNCHANGED；docs-only 提交永不作 LKGC）
```

### 85.4 相邻产品观察（与 C3 PASS 分立记录，非 C3 blocker，本 session 不实现）

```text
A. Manual 启动列表观察
   较早的一次 Human 运行曾观察到：应用启动时已导入手册列表为零，
   执行一次导入操作后，先前存储的手册才出现。其后 R2B 冷启动/重启运行
   均立即显示既有的 15/16 本已导入手册。
   当前分类 = HISTORICAL HUMAN OBSERVATION / NOT CURRENTLY REPRODUCED。
   不得在无可复现案例时称之为 confirmed open regression。
   未来自动化应覆盖 cold-start hydration。

B. Manual Delete UI
   Human 确认产品具有 import / selection / preview，但没有用户可见的
   删除已导入手册的操作。C1a 文档（§60/§61 时代）早已记录 Manual Delete UI
   缺席。当前分类 = KNOWN PRODUCT GAP / HUMAN REQUESTED FOR FUTURE
   MAINTENANCE。本 session 不实现；不是 C3 blocker；不启动该维护切片
   （是否立项由 Human 另行决定）。
```

### 85.5 本节动作边界（docs-only）

仅归档 Human 权威验收证据与分类；未改 src/tests/QML/CMake · 未 build /
configure / CTest / 候选重生成 · 无 live ModelScope / 凭据读取 · 无 Profile/manual
数据变更 · 未开始 Edit / M12-D / Manual Library 维护 · 未推进 LKGC ·
未创建 canonical package · 未 push / 未 tag / 未 amend。

---

## 83. M12-C C3 — ACCEPTANCE EVIDENCE ADDENDUM（SESSION C3-R2A · test-only + ratification）

> 性质：append-only 归档。Human 授权范围 = 仅关闭三项验收证据缺口
> （① 非空洞 full-profile validation；② Accept→Discard 恢复；③ 第二棵全新
> post-commit fresh ratification 树 + 候选/部署生成）；**不**重设计 C3，
> **不**实现 Edit/batch/RegisterEntryCandidate/schema·provenance/M12-D/package/
> tag/push/LKGC advancement。生产源码只读，除非新测试暴露真实缺陷
>（结果：**未暴露**，无需 STOP/HOLD）。

### 83.1 起始基线（实测）

```text
HEAD              = aba716bc3e3b3db8a5aab7a32edb9fc3ae81ade5
behavior commit   = 61f641ef2d9045cd90dd7598dcf78ab36b5af800
verified LKGC     = 613a32a0d6ca71ac53185779b7d5ab3b80c82870（UNCHANGED）
tracked clean · cached 空 · diff --check rc 0
```

### 83.2 GAP A — 非空洞 full-profile validation（新测试 r2a_01）

`r2a_01_invalidDraftAcceptFailsAtomically`：通过既有编辑 seam 把当前 draft 置为
违反**真实冻结规则**的状态（清空 displayName），非法性由两路证明——控制器投影
`validationText` 非空 + 权威 core validator `validateDeviceProfile` 在等价逻辑状态上
命名 `DisplayNameMissing`；然后以**完全有效的 Candidate + 完全有效的 evidence** 执行
C3 Accept ⇒ **Accept 失败**：review token = `candidate_apply_failed`、
ProfileController token = `display_name_missing`、Candidate 保持 PendingReview、
draft 快照逐项不变（含 manufacturer = "Old Co."）、持久化文件 byte-identical、
零 runner 调用。**证明 applyCandidateField 内的全量 validateDeviceProfile 是
承重的，而非无条件接受 Manufacturer。**

### 83.3 GAP B — Accept → Discard（新测试 r2a_02）

`r2a_02_acceptThenDiscardRestoresBaseline`：持久化基线（"Old Co."）→ 记录完整
draft/持久化快照 → 有效 Candidate → Accept 成功（draft manufacturer = 提议值、dirty、
持久化文件不变）→ 调用**既有** `discardCurrentChanges()` ⇒ draft 快照恢复到
persisted 基线（manufacturer/其余 identity 字段逐项原值）、dirty = false、持久化文件
byte-identical；**Candidate 保持 consumed**——同一 candidate map 再次 Accept = false，
绝不因 draft 被 Discard 而重回 PendingReview（C3-H5/H9）；零 runner 调用。

### 83.4 非空洞 mutation（MUTATION-NX3）

临时注释 `applyCandidateField` 内的全量 validation 调用（测试不变）⇒ **REAL RED
exit 1（22 passed / 1 failed）**，恰好 `r2a_01` 在
`'!invokeAccept(...)' returned FALSE` 上失败——bypass 后非法草稿的 Accept 成功。
精确逆向（未用 checkout/restore/reset）⇒ residue 0（grep 无残留；
ProfileController.cpp 与 HEAD diff 为空）⇒ 重建复绿 **23/23**。

### 83.5 test-only 提交

```text
commit = f5c1c906f95ddaaf7f2219cc73ef5c13cf9c277b
         「M12: strengthen C3 authority acceptance tests」
         （1 file / +98，parent aba716b…，NO AMEND）
仅 tests/test_candidate_review.cpp；零生产/QML/schema/docs 改动。
NOT LKGC；不替代 behavior commit 61f641e。
CMake 无需改动（新测试并入既有 candidate_review 目标）。
```

### 83.6 第二棵全新 post-commit ratification 树

```text
tree      = buildcceptance\session-c3-r2a-postcommit-release\（本 session 新建；
            未复用 session-c3-r2-release / session-p-r1* / 旧 build/release）
configure = RC 0（121.3s；GNU 13.1.0 / Qt 6.11.1 mingw_64 /
            PDFium 156.0.8066.0 frozen `d42c452a…` + libzip 1.11.4 offline root；
            从当前 committed HEAD 配置；凭据缺席）
build     = RC 0（435/435，350.9s）
inventory = Total Tests = 66
full      = 66/66 PASS / 0 failed / exit 0 / 180.2s
  candidate_review #31           = Passed 1.43s（23 个测试函数，含两个 R2A 新测试）
  qml_candidate_review_check #63 = Passed 2.74s
  qml_candidate_review_check_windows #64 = Passed 2.77s
  deployment_startup_check #34   = Passed 25.71s
  QML 诊断                        = 0（FAIL_REGULAR_EXPRESSION 拒绝集零命中）
```

### 83.7 行为边界（BOUNDARY）

```text
C3 BEHAVIOR BOUNDARY = 61f641ef2d9045cd90dd7598dcf78ab36b5af800
Git 实测：61f641e..HEAD 仅 docs（aba716b）+ tests（f5c1c90）；
  src/ diff = 空 ⇒ 61f641e 之后无任何生产行为变化。
test-only 提交不晋升为 behavior LKGC。
```

### 83.8 新候选 provenance（取代 C3-R2 pre-ratification 候选）

```text
candidate root = buildcceptance\session-c3-r2a-postcommit-release\candidate\ModbusLensexe path       = <candidate root>\modbuslens.exe
exe size       = 6,443,540 B
exe SHA-256    = 4f40a67afd599b2d1d0287b70a74d4c2425938816af99029f1aa9555e3dfbce5
                 （source exe ≡ candidate exe，byte-identical）
manifest       = 1713 entries（root 1714 files）
qwindows.dll   = 804739071bba619b4a4312b5bb29a142545a64c4c80218e5b2e6672ad33ee8ac
pdfium.dll     = d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b
deployment     = deployment_startup_check 独立复跑 Passed 28.14s
                 （sanitized 凭据缺席 env；qwindows 确证从 candidate 树加载；
                   启动零 ModelScope 请求）
```

### 83.9 状态（本节归档时点）

```text
C3 first slice = IMPLEMENTED / AUTOMATED PASS（R2A 三门全过：新测试 + NX3 闭环 +
                 post-commit 66/66 ratification）
C3 Human visual/functional = PENDING（Human 验收入口 = §83.8 新候选；
                 R2 旧候选 `bd6133f0…` 对 Human gate 而言已被取代）
M12-C C3 = IN PROGRESS；M12-C overall = IN PROGRESS
M12-D = NOT STARTED / NOT AUTHORIZED；canonical package = NOT CREATED
verified LKGC = 613a32a0d6ca71ac53185779b7d5ab3b80c82870（UNCHANGED；
                 docs-only 提交永不作 LKGC）
无 push / 无 tag / 无 amend。
```

---

## 84. M12-C C3 — STANDING DISCARD ACTION EXPOSED（SESSION C3-R2B · behavior + docs）

> 性质：append-only 归档。范围 = 把**既有** `ProfileController::discardCurrentChanges()`
> （早已 Q_INVOKABLE、退出守卫已在用）作为显式 Human UI 动作暴露到 Device Profile
> 工作区。不实现 Edit/batch/RegisterEntryCandidate/schema·provenance/M12-D；
> 无 LKGC advancement/package/tag/push。

### 84.1 实现（最小，2 files / +69 −1）

```text
DeviceProfilePage.qml：deviceProfileActions 行内、Save 与 Delete 之间新增
  profileDiscardButton（AppButton，既有按钮风格）：
    text = 「放弃修改」· Accessible.name = 「放弃未保存的设备档案修改」
    enabled = profileController.dirty && profileController.hasOpenProfile
    onClicked = profileController.discardCurrentChanges()
  ⇒ 无未保存修改时禁用（沿用现有 enabled 绑定风格）；无第二套 rollback 逻辑
    （这就是退出守卫已在用的权威 workflow）；QML 不触 JSON/ProfileStore；
    不触碰 Candidate lifecycle。
src/main.cpp：runProfileEditorCheck 新增 stage 16（runtime gate）。
```

### 84.2 验证链（真实命令与输出，凭据缺席）

```text
REAL RED（实现前，qml_profile_editor_check exit 1）
  PROFFAIL: the standing Discard action is missing
  PROFFAIL: the Discard action is not clickable
  PROFFAIL: Discard did not restore the persisted displayName
  PROFFAIL: Discard left the draft dirty
GREEN（实现后）
  qml_profile_editor_check = exit 0，PASS（stage 16：enabled 跟随 dirty、
    点击后 persisted displayName 恢复、dirty 清除、持久化文件 byte-identical、
    Candidate 集不动），QML 诊断 0
targeted = 17/17 PASS / exit 0 / 54.8s（editor/register_map/active_profile/
  manual_import±windows/consent±windows/candidate_review±windows/
  candidate_extraction/orchestration/profile_controller/smoke/geometry/nav/focus）
fresh full（提交源码上）= 66/66 PASS / exit 0 / 170.7s（deployment #34 Passed 25.78s）
```

### 84.3 提交与 post-commit ratification

```text
behavior commit = bb996a5bfc5416e3392c6fd709508e21aeab62c2
                  「M12: expose the profile discard action」
                  （2 files / +69 −1，parent f1d05c3…，NO AMEND，内无 docs）
post-commit ratification 树 = buildcceptance\session-c3-r2b-postcommit-release  （本 session 新建，未复用旧树；凭据缺席；configure RC 0 38.9s；build RC 0 435/435）
  inventory = 66
  full      = 66/66 PASS / 0 failed / exit 0 / 184.4s
    qml_profile_editor_check #57 = Passed 1.83s（含 stage 16）
    deployment_startup_check #34 = Passed 27.77s（canonical generator 从零重建候选）
新候选 provenance：
  exe = 6,449,965 B / SHA-256
        0bc2e6a23b7b0142612c8e31387bbc603b7afbd3ad2943a55405c0eb5ae083fa
        （source exe ≡ candidate exe，byte-identical；manifest 内哈希一致）
  manifest = 1713 entries（root 1714 files）
  qwindows.dll = 804739071bba619b4a4312b5bb29a142545a64c4c80218e5b2e6672ad33ee8ac
  pdfium.dll   = d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b
deployment gate（独立复跑）= Passed 27.29s（sanitized 凭据缺席 env；
  qwindows 确证从 candidate 树加载；启动零 ModelScope 请求）
```

### 84.4 诚实记录（证据时序）

behavior commit 信息中引用的 full 66/66 在**提交后立即补齐执行**（当时 targeted
17/17 已过、full 尚未跑完即提交——时序瑕疵如实记录；随后 full 66/66 PASS 验证了该
提交，post-commit ratification 树再次独立复证 66/66）。若 full 失败，将按纪律另行
correction commit（未发生）。

### 84.5 Human 验收补充项（并入 §82.6 checklist 之外的新增项）

```text
K. 打开一个 Profile，修改 displayName（出现「未保存修改」）⇒ [放弃修改] 可用；
   点击后 displayName 恢复为持久化值、「未保存修改」消失、无保存发生；
L. 无未保存修改时 [放弃修改] 禁用；
M. Accept 一个 Candidate 后点击 [放弃修改]：Manufacturer 草稿恢复原值，
   已消费 Candidate 不复活（不重回待审核列表）。
```

### 84.6 状态（本节归档时点）

```text
M12-C C3               = IN PROGRESS
C3 first slice / Accept / Reject = IMPLEMENTED / AUTOMATED PASS
Standing Discard action = IMPLEMENTED / AUTOMATED PASS（本节； Human visible = PENDING
                         并入 C3 Human acceptance，入口 = §84.3 新候选）
C3 Edit / batch        = NOT IMPLEMENTED / DEFERRED
RegisterEntryCandidate = NOT IMPLEMENTED / NOT IN CURRENT SOURCE
Durable provenance     = DEFERRED / NO SCHEMA CHANGE
M12-D                  = NOT STARTED / NOT AUTHORIZED
canonical package      = NOT CREATED
verified LKGC          = 613a32a0d6ca71ac53185779b7d5ab3b80c82870（UNCHANGED；
                         behavior 提交不是 LKGC）
无 push / 无 tag / 无 amend。
```

---

## 85. M12-C C3 — FIRST-SLICE HUMAN ACCEPTANCE（SESSION C3-R2C · docs-only 归档）

> 性质：append-only 归档。Human 授权（逐字）：「授权：归档 M12-C C3 first behavior
> slice Human acceptance PASS；仅 docs-only，不推进 LKGC，不开始 Edit，不开始 M12-D。」
> 仅记录 Human 观察到的证据；**不**发明时序、内部状态、网络包、provider HTTP 状态或
> Human 未观察的字段。本节为对 §82.6 PENDING 状态的 current superseding note；
> §82 历史原文不改写。

### 85.1 验收对象边界（artifact boundary）

```text
Human acceptance 覆盖的候选 = R2B 候选（§84.3）：
  buildcceptance\session-c3-r2b-postcommit-release\candidate\ModbusLens\modbuslens.exe
  exe SHA-256 = 0bc2e6a23b7b0142612c8e31387bbc603b7afbd3ad2943a55405c0eb5ae083fa
其行为提交 = bb996a5bfc5416e3392c6fd709508e21aeab62c2（§84）
本 docs 归档提交本身**不是**被测行为，**不是** LKGC。
```

### 85.2 Human 权威验收证据（逐条归档，分类 = HUMAN PASS）

```text
CHECK 1 — REJECT
  起始：Profile「1111」已打开；Manufacturer 草稿为空；真实 PendingReview
  Candidate 可见（manufacturer: ACME）。Human 点击 Reject。
  观察：Pending Candidate 消失；pending count = 0；Manufacturer 保持为空；
  无可见错误。
  ⇒ C3 Reject = HUMAN PASS；Reject 零可见 Profile mutation = HUMAN PASS。

CHECK 2 — ACCEPT TO DRAFT
  Human 重新提取产生新真实 Candidate（manufacturer: ACME）；点击 Accept。
  观察：Manufacturer 草稿变为「ACME」；Candidate 消失 / pending count = 0；
  UI 显示「未保存修改」；应用未自动保存；无可见错误。
  ⇒ C3 Accept to draft = HUMAN PASS；Candidate consumption = HUMAN PASS；
  No auto-save = HUMAN PASS。

CHECK 3 — DISCARD（使用含「放弃修改」常驻动作的 R2B 候选）
  Human 点击 Accept 后点击「放弃修改」。
  观察：Manufacturer 从「ACME」恢复为先前值（本验收 Profile 中为空）；
  「未保存修改」消失；「放弃修改」回到禁用态；Pending Candidate count 保持 0；
  已消费 Candidate 未复活；无可见错误。
  ⇒ Standing Discard = HUMAN PASS；Accept-before-Save rollback = HUMAN PASS；
  Consumed Candidate non-resurrection = HUMAN PASS。

CHECK 4 — EXPLICIT SAVE + RESTART
  Human 再产生新 Candidate（manufacturer: ACME）→ Accept → 显式 Save →
  关闭应用 → 重启应用 → 重新打开 Profile「1111」。
  观察：Manufacturer 仍显示「ACME」；验收的持久化跨重启存活；
  Pending Candidate count = 0；无可见错误。
  ⇒ Explicit Save persistence = HUMAN PASS；Restart persistence = HUMAN PASS；
  既有 Profile 持久化工作流 = HUMAN FUNCTIONAL PASS。

CHECK 5 — SCOPE BOUNDARY
  本次验收期间：未使用 C3 Edit workflow；未使用 batch review；未使用
  RegisterEntryCandidate workflow；未使用 M12-D workflow。
  ⇒ **不得**推断这些 deferred 特性已实现。
```

### 85.3 C3 first-slice 最终分类

```text
C3 contract                 = FROZEN / HUMAN APPROVED（§81，C3-H1..H10）
C3 first behavior slice     = COMPLETE / HUMAN ACCEPTED
C3 Accept                   = IMPLEMENTED / AUTOMATED PASS / HUMAN PASS
C3 Reject                   = IMPLEMENTED / AUTOMATED PASS / HUMAN PASS
Standing Discard            = IMPLEMENTED / AUTOMATED PASS / HUMAN PASS
Explicit Save persistence   = AUTOMATED FOUNDATION PASS / HUMAN PASS
C3 Edit                     = NOT IMPLEMENTED / DEFERRED（C3-H8 冻结语义）
C3 batch                    = NOT IMPLEMENTED / DEFERRED（C3-H1）
RegisterEntryCandidate      = NOT IMPLEMENTED / NOT IN CURRENT SOURCE
Durable provenance          = DEFERRED / NO SCHEMA CHANGE（C3-H7）
C3 overall                  = IN PROGRESS —— 原因：canonical C3 capability
                              （§13/§17.2）仍包含 Edit，而 Edit 尚未实现或
                              被 Human 验收
M12-C overall               = IN PROGRESS
M12-D                       = NOT STARTED / NOT AUTHORIZED
verified LKGC               = 613a32a0d6ca71ac53185779b7d5ab3b80c82870
                              （UNCHANGED；docs-only 提交永不作 LKGC）
```

### 85.4 相邻产品观察（与 C3 PASS 分立记录，非 C3 blocker，本 session 不实现）

```text
A. Manual 启动列表观察
   较早的一次 Human 运行曾观察到：应用启动时已导入手册列表为零，
   执行一次导入操作后，先前存储的手册才出现。其后 R2B 冷启动/重启运行
   均立即显示既有的 15/16 本已导入手册。
   当前分类 = HISTORICAL HUMAN OBSERVATION / NOT CURRENTLY REPRODUCED。
   不得在无可复现案例时称之为 confirmed open regression。
   未来自动化应覆盖 cold-start hydration。

B. Manual Delete UI
   Human 确认产品具有 import / selection / preview，但没有用户可见的
   删除已导入手册的操作。C1a 文档（§60/§61 时代）早已记录 Manual Delete UI
   缺席。当前分类 = KNOWN PRODUCT GAP / HUMAN REQUESTED FOR FUTURE
   MAINTENANCE。本 session 不实现；不是 C3 blocker；不启动该维护切片
   （是否立项由 Human 另行决定）。
```

### 85.5 本节动作边界（docs-only）

仅归档 Human 权威验收证据与分类；未改 src/tests/QML/CMake · 未 build /
configure / CTest / 候选重生成 · 无 live ModelScope / 凭据读取 · 无 Profile/manual
数据变更 · 未开始 Edit / M12-D / Manual Library 维护 · 未推进 LKGC ·
未创建 canonical package · 未 push / 未 tag / 未 amend。


---

## 86. M12-C C3 — VERIFIED LKGC ADVANCE THROUGH C3 FIRST SLICE（SESSION C3-R2C · docs-only 归档）

> 性质：append-only 归档。仅记录 Human 授权与 Git 实测验证；LKGC 推进本身是
> canonical docs 的状态转移，**不是**任何新行为。

### 86.0 Human 授权（逐字）

```text
授权：将 M12-C C3 first behavior slice Human acceptance 作为新的 verified
behavior baseline，并将 verified LKGC 从
613a32a0d6ca71ac53185779b7d5ab3b80c82870 推进到
bb996a5bfc5416e3392c6fd709508e21aeab62c2。
本授权仅推进 verified LKGC；不授权 Edit、Manual Library maintenance、
M12-D、package、tag 或 push。
```

### 86.1 Git 实测验证（授权目标合法性）

```text
git cat-file -t bb996a5…                   = commit
git merge-base --is-ancestor bb996a5… HEAD = exit 0（是 HEAD 祖先）
bb996a5~1..bb996a5 changed paths           = 2 files / +69 −1
  （src/main.cpp + src/ui/qml/pages/DeviceProfilePage.qml）⇒ behavior-bearing
bb996a5..HEAD = 2 个提交（f80caf4 · 783bf06），非 docs 路径 diff = 空
  ⇒ 其后提交全部 docs-only，永不作 LKGC
⇒ bb996a5 = C3 first slice（含 standing Discard action）最后一个
  behavior-bearing tree，为合法 LKGC 目标。
```

### 86.2 基线证据链（为什么是 bb996a5）

```text
· C3-R2：slice 实现 + REAL RED/GREEN + MUTATION-NX2 负向对照闭环 +
  fresh Release full 66/66（当时树）。
· C3-R2A：两个验收证据缺口测试 + MUTATION-NX3 闭环 + 第二棵全新
  post-commit 树 66/66 ratification。
· C3-R2B：standing Discard action 实现 + editor gate stage 16
  RED→GREEN + 提交源码上 full 66/66 + 第三棵全新 post-commit ratification
  树 66/66（bb996a5 所在链）+ 候选 0bc2e6a2… + deployment gate 27.29s。
· C3-R2C：Human first-slice acceptance = PASS（§85：Reject/Accept/Discard/
  Save+Restart 五项 HUMAN PASS）——Human 验收的对象候选即由 bb996a5
  内容生成。
```

### 86.3 推进后的 canonical 状态

```text
verified LKGC = bb996a5bfc5416e3392c6fd709508e21aeab62c2（Human authorized）
前一 verified  = 613a32a0d6ca71ac53185779b7d5ab3b80c82870（转历史链一环，原文保留）
M12-C C3 first behavior slice = COMPLETE / HUMAN ACCEPTED（新 verified baseline）
C3 Accept / Reject / Standing Discard = IMPLEMENTED / AUTOMATED PASS / HUMAN PASS
C3 overall     = IN PROGRESS（Edit 未实现未验收）
Edit           = NOT IMPLEMENTED / DEFERRED（本授权明确不授权）
Manual Library maintenance（含 Manual Delete UI）= NOT AUTHORIZED（本授权明确不授权）
M12-D          = NOT STARTED / NOT AUTHORIZED（本授权明确不授权）
canonical package = NOT CREATED · tag = 仅 v1.0.0 · push = 未发生
REAL MODBUS HARDWARE = NOT VERIFIED
```

**本节动作边界（docs-only）**：仅归档授权 + Git 验证 + 状态转移记录；
未改 src/tests/QML/CMake · 未 build / test · 未执行任何网络请求 ·
未创建 canonical package · 未 tag / 未 push · 本节所在提交 docs-only 永不作 LKGC。

---

## 88. MANUAL LIBRARY ML-1 — COLD-START HYDRATION + SYNTHETIC SAMPLES（SESSION ML-R1 · behavior/data + docs）

> 性质：append-only 归档。Human 授权（逐字）：「同意上述 P0-ML-A/B/C/D/F/G/H 裁定；
> 授权先执行 SLICE-ML-1：修复 cold-start hydration 并正式纳入四份 synthetic samples」，
> 流程 = RED → GREEN → negative-control → targeted → fresh Release full regression →
> behavior/data commit → post-commit fresh ratification → candidate/deployment →
> docs archive；暂不实施 ML-2 删除功能，不推进 LKGC，不开始 C3 Edit/M12-D，
> 不做 package/tag/push。

### 88.0 P0-ML-A/B/C/D/F/G/H = HUMAN-FROZEN（ML-2 语义，逐字归档于 Human 授权记录）

A 删除选中后 selected=NONE/preview 清空/提取按钮禁用（不自动换行、不残留旧 preview）·
B PendingReview 引用目标手册时 **DELETE MUST BE BLOCKED**（确定性文案；不得自动消费/
拒绝/留死候选）· C Running extraction 时 **DELETE MUST BE BLOCKED**（产品策略冻结，
尽管内存隔离技术上安全）· D metadata record = 权威可见性点，删除失败必须显式报错 ·
F 删除范围 = metadata record + **仅当无其他记录引用同 contentHash** 时的受管工件
（共享工件必须存活；Human 原始外部文件永不删除/修改；GC 失败 ⇒ 记录仍删除 + 明确
“本地缓存清理失败”警示；无新数据库/tombstone）· G ML-1/ML-2 v1 无显式 Open 按钮 ·
H 四份样例按**确切现有文件名**纳入 tracked（不改名、不重写内容；A/B/C 支持确定性
导入测试；D 仅支持确定性导入与 fake/replay 断言，禁止 live ModelScope 期望断言）。
**以上均为 ML-2 未来语义；本 session 零 Delete 实现。**

### 88.1 起始基线 + 样例字节审计（实现前）

```text
HEAD = 3e27bbb2a330af078279d00599bcce62759c17d8 · verified LKGC = bb996a5…（UNCHANGED）
tracked clean · untracked = _ctx.py/_dump.py + 四样例（未动）
A_Clear.txt       = 938 B / SHA-256 03a4f93e2208c9dede12699ac887b45cc663736ae2f6d7614ada7851d2413f74
B_Chinese.md      = 895 B / SHA-256 ad54598e5f5fdd6f7ce3f85a56b4c31a2032aa9630cc9826da927bc31892b6b3
C_Structured.txt  = 828 B / SHA-256 cb90134c09ff80d584d21804569358e36e6e5377cd9dabd58a4b7bf6af01d85f
D_NoManufacturer  = 682 B / SHA-256 bfa9f83a92ca164f316cbf7685e28ef365ec9a2ffcf5f13a1b0386a59d8a3206
均 valid UTF-8、无 BOM、全合成自述、无密钥/客户数据/绝对机器路径/可执行载荷；
B 为 UTF-8 中文 Markdown（含 Markdown 行尾双空格硬换行——Human 冻结内容，
  git diff --check 曾提示 trailing whitespace，按 P0-ML-H 不重写，SHA 提交前后一致）。
```

### 88.2 冷启动根因（源码确证，ML-R0 审计结论）

`ManualImportController` 构造函数为空、不调用 `refresh()`；全仓无启动期 refresh
触发（QML 无 Component.onCompleted/onVisibleChanged 触发）⇒ 持久化 store 存在时
新 controller 的列表**恒为空**，直至任何一次成功导入触发 `refresh()`（loadAll）使
全部持久化记录出现——与 Human 观察逐条吻合。决定性对照：`ProfileController` 构造
函数调用 `refreshCatalog()`（Profile 目录启动即水合）。

### 88.3 REAL RED → GREEN → MUTATION-NX4（凭据缺席）

```text
RED：tests/test_manual_import.cpp 新增 ML1 矩阵（ml1_01..09 + samples 路由测试）
  manual_import = exit 7（24 passed / 7 failed，三次连跑完全一致）——失败恰好为
  7 个水合断言：预填充 store（权威 API 导入）→ 销毁导入上下文 → 全新 controller
  零导入 ⇒ manualDocuments 为空。
GREEN：ManualImportController 构造函数调用既有 refresh()（与 ProfileController
  构造刷新先例对称；无 timer/异步/QML workaround/新持久化路径）
  manual_import = exit 0，31 passed / 0 failed（ML1-01..10 等价覆盖：
  水合/立即可选/受管预览/原文件删除独立性/空 store 无伪错误/损坏元数据跳过/
  零 Profile 副作用/顺序=loadAll 权威序/无隐式选中；样例 A/B/C/D 经权威导入
  路由全部成功，D 为纯内容负例、无 live 断言）
MUTATION-NX4（注释 ctor 的 refresh()）⇒ REAL RED exit 7（恰好同 7 个水合测试，
  ml1_01 count 0 vs 2）→ 精确逆向（未用 checkout/restore/reset）→ residue 0 →
  复绿 31/31
```

### 88.4 targeted + pre-commit fresh 树

```text
targeted = 20/20 PASS / exit 0 / 57.5s
pre-commit fresh 树 = buildcceptance\session-ml-r1-release\（新建）
  configure RC 0（38.5s）· build RC 0（435/435，381.5s）
  inventory = 66 · full = 66/66 PASS / 0 failed / exit 0 / 187.3s
  （manual_import #26 Passed 1.50s；deployment_startup_check #34 Passed 28.96s）
```

### 88.5 behavior/data 提交 + 样例 SHA（记录）

```text
commit = 19e2f45341c3f15d1f27bc38a9ad728d268049e3
         「M12: hydrate manual library and add test manuals」
         （7 files / +393，parent 3e27bbb…，NO AMEND；零 docs 混入；
           四样例按确切路径逐个 stage）
样例 SHA-256（提交前后一致）= §88.1 所列四个完整哈希
git diff --cached --check 对 B 的 Markdown 行尾双空格提示 trailing whitespace
  —— 属 Markdown 硬换行语义 + P0-ML-H 冻结内容，**不重写**（哈希不变证明字节级保留）
提交后一致性 = tracked clean；git diff HEAD -- src tests CMakeLists.txt samples 为空
```

### 88.6 post-commit ratification（第二棵全新树）

```text
树   = buildcceptance\session-ml-r1-postcommit-release\（新建；凭据缺席；
       configure RC 0 152.1s · build RC 0 435/435 369.3s）
inventory = 66 · full = 66/66 PASS / 0 failed / exit 0 / 184.1s
  manual_import #26 Passed 1.56s · deployment_startup_check #34 Passed 26.29s
```

### 88.7 候选 provenance + deployment（§20 边界：samples 不随候选发布）

```text
candidate root = buildcceptance\session-ml-r1-postcommit-release\candidate\ModbusLensexe            = 6,449,983 B / SHA-256
                 b80811bff2b0aba507cdcb5ca75b2f4b96357e405b8eb400110e85746c794a46
                 （source exe ≡ candidate exe，byte-identical）
manifest       = 1713 entries（root 1714 files）
qwindows.dll   = 804739071bba619b4a4312b5bb29a142545a64c4c80218e5b2e6672ad33ee8ac
pdfium.dll     = d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b
samples 边界   = 生成器不含 samples staging（实测 grep 为空）⇒ 四样例
                 repository-only，候选不携带（未授权扩展 package 政策）
deployment     = deployment_startup_check 独立复跑 Passed 24.13s
                 （sanitized 凭据缺席 env；qwindows 确证从 candidate 树加载；
                   启动零 ModelScope 请求）
```

### 88.8 HUMAN COLD-START GATE（Agent 不执行；候选 = §88.7 新候选）

```text
1. 使用 ML-R1 post-commit 候选；生产 ManualStore 已含多本手册。
2. 不点击「导入说明书」。
3. 完全关闭 ModbusLens → 启动新候选 → 打开设备页。
4. 确认：手册数量即时正确、旧列表即时可见、点击任一旧手册立即出预览。
5. 再次关闭并重启 → 不导入 → 同样立即可见。
6. （可选）从 samples/ 复制一份 tracked 样例到仓库外做导入 smoke
   ——这不是冷启动水合的必要条件。
```

### 88.9 状态（本节归档时点）

```text
Manual Library ML-1   = IMPLEMENTED / AUTOMATED PASS / HUMAN COLD-START REVIEW PENDING
Cold-start hydration  = IMPLEMENTED / AUTOMATED PASS（缺陷 CONFIRMED / FIXED）
Synthetic samples     = TRACKED / AUDITED（repository-only，不随候选发布）
Manual Delete         = NOT IMPLEMENTED（P0-ML-A/B/C/D/F 政策 HUMAN-FROZEN 为 ML-2 语义）
ML-2                  = NOT STARTED / NOT AUTHORIZED
C3 first slice        = COMPLETE / HUMAN ACCEPTED
C3 overall            = IN PROGRESS · C3 Edit = NOT STARTED / NOT AUTHORIZED
M12-D                 = NOT STARTED / NOT AUTHORIZED
canonical package     = NOT CREATED
verified LKGC         = bb996a5bfc5416e3392c6fd709508e21aeab62c2（UNCHANGED；
                        behavior/data 提交不是 LKGC）
无 push / 无 tag / 无 amend。
```

---

## 89. MANUAL LIBRARY ML-1 — HUMAN COLD-START ACCEPTANCE（SESSION ML-R1A · docs-only 归档）

> 性质：append-only 归档。Human 授权（逐字）：「授权：归档 Manual Library ML-1
> Human cold-start acceptance PASS；仅 docs-only，不推进 LKGC，不实施 ML-2 Delete，
> 不开始 C3 Edit/M12-D，不做 package/tag/push。」仅记录 Human 观察到的证据；
> **不**发明启动时延、手册精确数量、文件系统事件、内部 controller 状态、store 读取
> 次数、QML 信号时序、provider/网络行为或隐藏诊断。本节为 §88.9
> 「HUMAN COLD-START REVIEW PENDING」的 current superseding note；§88 历史原文
> 不改写。

### 89.1 验收对象边界（artifact boundary）

```text
Human acceptance 覆盖的候选 = ML-R1 post-commit 候选（§88.7）：
  buildcceptance\session-ml-r1-postcommit-release\candidate\ModbusLens\modbuslens.exe
  exe SHA-256 = b80811bff2b0aba507cdcb5ca75b2f4b96357e405b8eb400110e85746c794a46
其 behavior/data 提交 = 19e2f45341c3f15d1f27bc38a9ad728d268049e3（§88.5）
本 docs 归档提交本身**不是**被测行为，**不是** LKGC。
```

### 89.2 Human 权威陈述（逐字）

```text
「冷启动两次都不用导入，
旧说明书列表直接出现，
点击后可以正常预览。」
```

### 89.3 Human 实际执行（仅 Human 观察事实）

```text
Run 1：ModbusLens 完全关闭后启动 ML-R1 候选；Human **未**导入任何新说明书；
  已导入的旧手册列表直接出现；Human 选中一本既有手册；预览正常显示。
Run 2：ModbusLens 再次完全关闭并重启；Human 仍**未**导入任何新说明书；
  旧手册列表再次直接出现；既有手册预览保持可用。
Human 未再需要历史性 workaround（「先导入一份说明书，旧说明书列表才出现」）。
```

### 89.4 授权解释（分类 = HUMAN PASS）

```text
Manual Library ML-1 Human cold-start acceptance = PASS / HUMAN VERIFIED
Cold-start hydration                 = AUTOMATED PASS / HUMAN PASS
Cold-start existing-list visibility  = HUMAN PASS
Existing Manual selection (cold start) = HUMAN PASS
Existing Manual preview (cold start) = HUMAN PASS
Second restart repeatability         = HUMAN PASS
两次 Human 检查均未要求任何新的导入操作。
```

### 89.5 缺陷状态的 superseding current truth

```text
历史记录原样保留：Human 曾观察「启动列表为空 → 导入一份 → 旧手册才出现」。
Current superseding truth：
  Cold-start hydration defect = CONFIRMED BY SOURCE AUDIT（ML-R0，机制：
  ManualImportController 构造不加载持久化 store）→ FIXED IN ML-R1
  （ctor 调用既有 refresh()）→ AUTOMATED PASS（ML1 矩阵 + MUTATION-NX4 闭环）→
  HUMAN PASS（本节两次冷启动验证）。
历史 issue 状态 = CLOSED / VERIFIED FIXED。
「NOT CURRENTLY REPRODUCED」不再是当前最终分类——源码审计已确立机制。
```

### 89.6 ML-1 最终分类

```text
Manual Library ML-1      = COMPLETE / HUMAN ACCEPTED
Cold-start hydration     = FIXED / AUTOMATED PASS / HUMAN PASS
Existing Manual cold-start visibility = HUMAN PASS
Existing Manual selection/preview after cold start = HUMAN PASS
Synthetic samples        = TRACKED / AUDITED / SYNTHETIC / repository-only
Manual Delete            = NOT IMPLEMENTED
Manual Delete contract   = P0-ML-A/B/C/D/F/G/H HUMAN-FROZEN（§88.0，ML-2 语义）
ML-2                     = NOT STARTED / NOT AUTHORIZED
C3 first behavior slice  = COMPLETE / HUMAN ACCEPTED
C3 overall               = IN PROGRESS（Edit 未实现未验收）
C3 Edit                  = NOT STARTED / NOT AUTHORIZED
M12-C overall            = IN PROGRESS
M12-D                    = NOT STARTED / NOT AUTHORIZED
canonical package        = NOT CREATED
verified LKGC            = bb996a5bfc5416e3392c6fd709508e21aeab62c2（UNCHANGED；
                           docs-only 提交永不作 LKGC）
```

### 89.7 本节动作边界（docs-only）

仅归档 Human 权威验收证据与 superseding 分类；未改 src/tests/CMake/QML/samples ·
未 build / configure / CTest / 候选重生成 / deployment 测试 / 生产 app 启动 ·
无 live ModelScope / 凭据读取 · 无 ManualStore/DeviceProfile 数据变更 ·
未开始 ML-2 Delete / C3 Edit / M12-D / Manual Library 维护实现 ·
未推进 LKGC · 未创建 canonical package · 未 push / 未 tag / 未 amend。

---

## 90. MANUAL LIBRARY ML-1 — VERIFIED LKGC ADVANCE（SESSION ML-R1B · docs-only 归档）

> 性质：append-only 归档。仅记录 Human 授权与 Git 实测验证；LKGC 推进本身是
> canonical docs 的状态转移，**不是**任何新行为。

### 90.0 Human 授权（逐字）

```text
授权：将 Manual Library ML-1 Human acceptance
作为新的 verified behavior baseline，
并将 verified LKGC 从

bb996a5bfc5416e3392c6fd709508e21aeab62c2

推进到

19e2f45341c3f15d1f27bc38a9ad728d268049e3。

本授权仅推进 verified LKGC；
不授权 ML-2 implementation、C3 Edit、M12-D、
package、tag 或 push。
```

### 90.1 Git 实测验证（授权目标合法性）

```text
git cat-file -t 19e2f45…                   = commit
git merge-base --is-ancestor 19e2f45… HEAD = exit 0（是 HEAD 祖先）
19e2f45~1..19e2f45 changed paths           = 7 files / +393
  （ManualImportController.cpp ctor 冷启动水合 +9 ·
    tests/test_manual_import.cpp ML1 矩阵 +219 · CMakeLists.txt 样例目录
    编译定义 +4 · 四份 tracked synthetic samples 新增）
  ⇒ **behavior/data-bearing ML-1 commit**（含生产行为 + 测试 + 数据；
    如实记录其双重性质，不得描述为 docs-only）
19e2f45..HEAD = 2 个提交（f80caf4 之……更正：a3bdd16 · 6bc4ee7），
  非 docs 路径 diff = 空 ⇒ 其后提交全部 docs-only，永不作 LKGC
⇒ 19e2f45 = ML-1 最后一个 behavior/data-bearing tree，为合法 LKGC 目标。
```

### 90.2 基线证据链（为什么是 19e2f45）

```text
· ML-R1：冷启动缺陷源码确证 + ctor refresh() 最小修复 +
  REAL RED（manual_import exit 7，7 个水合断言，三次连跑一致）→
  GREEN 31/31 → MUTATION-NX4 闭环（RED → 精确逆向 → residue 0 → 复绿）→
  targeted 20/20 → pre-commit fresh 树 66/66 / 187.3s。
· ML-R1（post-commit）：第二棵全新树 66/66 / 184.1s + 候选 b80811bf…
  （manifest 1713）+ deployment gate 27.29s（独立复跑 24.13s）。
· ML-R1A：Human cold-start acceptance = PASS（两次冷启动均零导入、
  旧列表直接出现、选中/预览正常）——Human 验收对象候选即由
  19e2f45 内容生成（session-ml-r1-postcommit-release 树）。
```

### 90.3 推进后的 canonical 状态

```text
verified LKGC = 19e2f45341c3f15d1f27bc38a9ad728d268049e3（Human authorized）
前一 verified  = bb996a5bfc5416e3392c6fd709508e21aeab62c2（转历史链一环，原文保留）
Manual Library ML-1 = COMPLETE / HUMAN ACCEPTED（新 verified behavior baseline）
Cold-start hydration = FIXED / AUTOMATED PASS / HUMAN PASS
Synthetic samples    = TRACKED / AUDITED / SYNTHETIC / repository-only
Manual Delete        = NOT IMPLEMENTED
ML-2 contract        = HUMAN-FROZEN（P0-ML-A/B/C/D/F/G/H，§88.0）
ML-2 implementation  = NOT STARTED / NOT AUTHORIZED（本授权明确不授权）
C3 first slice       = COMPLETE / HUMAN ACCEPTED
C3 overall           = IN PROGRESS（Edit 未实现未验收）
C3 Edit              = NOT STARTED / NOT AUTHORIZED（本授权明确不授权）
M12-D                = NOT STARTED / NOT AUTHORIZED（本授权明确不授权）
REAL MODBUS HARDWARE = NOT VERIFIED
canonical package    = NOT CREATED · tag = 仅 v1.0.0 · push = 未发生
```

**本节动作边界（docs-only）**：仅归档授权 + Git 验证 + 状态转移记录；
未改 src/tests/CMake/QML/samples · 未 build / configure / CTest ·
未生成候选 / 未跑 deployment gate · 无 live ModelScope / 凭据读取 ·
未开始 ML-2 / C3 Edit / M12-D · 未创建 canonical package ·
未 tag / 未 push · 本节所在提交 docs-only 永不作 LKGC。

---

## 91. MANUAL LIBRARY ML-2 — SAFE MANUAL DELETION（SESSION ML-R2 · behavior + docs）

> 性质：append-only 归档。Human 授权（逐字见 §91.0 要点）：按已冻结的
> P0-ML-A/B/C/D/F/G/H 实施删除选中说明书 + 确认 + Pending/Running 阻断 +
> metadata 权威删除 + shared-content 引用保护 + unreferenced GC + GC 失败可见警告 +
> 删除后清除选择/预览 + restart persistence；不授权 C3 Edit/M12-D/LKGC/package/tag/push。

### 91.0 冻结政策摘要（product contract，非 advisory；逐字要点）

```text
A 删除成功后 selected=NONE / selectedIndex 无效 / preview 清空 / 文件名状态投影回
  未选态 / AI extraction 禁用直至显式新选；禁止自动选下一/上一、禁止残留旧 preview、
  禁止静默重定向 extraction。
B 任何 PendingReview Candidate 引用目标手册 ⇒ DELETE 必须失败/阻断；确定性文案
  「该说明书仍有待审核 AI 候选，请先接受或拒绝候选，再删除说明书。」；阻断时不打开
  破坏性确认；禁止自动消费/拒绝/清空候选或留死候选。
C Running extraction ∈ 目标 ⇒ DELETE 阻断（文案「该说明书正在进行 AI 提取，请等待
  提取完成后再删除。」）；不得 cancel provider / 改 extraction 状态 / 消费未来结果。
D documents/<documentId>.json = 权威可见性点；移除失败 ⇒ Delete FAIL、记录保持可见、
  确定性错误、禁止伪成功、禁止 GC。
F metadata 成功后：查剩余文档是否引用同 contentHash；有 ⇒ 共享工件（source/<hash>.bin
  与文本缓存）必须存活；无 ⇒ 尝试清理该手册的 unreferenced 受管源+文本缓存（类型
  决定 .txt/.json）；Human 原始外部文件永不删除/修改；GC 失败 ⇒ 记录仍删除 +
  「说明书已从 ModbusLens 移除，但本地缓存清理失败。」；已缺失工件 = 已清理；
  不引入新数据库/tombstone。
G 不加显式 Open 按钮（列表选择 = 查看）。
H 四份 synthetic samples 保持 TRACKED/AUDITED/repository-only，不改。
架构：ManualStore 只负责持久化/权威删除/共享引用保护/清理结果；Candidate/Running
  政策在其上层；唯一权威应用级删除入口 =
  Human click → candidate/running guards → confirmation → Manual controller →
  ManualStore 权威删除 → controller refresh → 清除选择/预览 → 结果/警告面；
  guards 不得只存在于按钮 enabled 状态——实际命令路径必须复检；无第二 store /
  第二持久化管线 / QML 文件删除 / QML JSON 编辑。
```

### 91.1 起始基线

```text
HEAD = 3df4d71942f234392377512670f2d933b9c150c2 · verified LKGC = 19e2f45…（UNCHANGED）
tracked clean；四 samples tracked 未动。重建所有权（实测）：
MANUAL STORE OWNER = ManualStore（deleteDocument 新 API + 最小 test-only remove
  interposer）；MANUAL CONTROLLER OWNER = ManualImportController
  （deleteDocumentById，身份保持重载）；ORCHESTRATION OWNER =
  CandidateExtractionController（已安全持有 manualController_ 方向，无循环依赖）
  —— checkManualDeleteAllowed（纯读 guard）+ deleteManualDocument（命令路径复检
  guard 后委派）；QML OWNER = DeviceProfilePage 手册卡（行内删除按钮 + 确认
  Dialog + blocker/notice Label）；确认对话框沿 profileDeleteDialog 惯例
  （modal / Popup.NoAutoClose / 380 宽 / footer DialogButtonBox / objectName /
  Accessible）。hasPendingCandidateForManual(documentId) = candidates_ 扫描
  （consumed 列表除外）；isExtractionRunningForManual = state_==Running &&
  activeAttempt_.documentId。ManualStore 新增枚举 Outcome
  {Success, SuccessWithCleanupWarning, FailureDocumentNotFound, FailureMetadataRemove}
  + token 常量 manual_delete_*（§7 语义，命名随仓库惯例）。
```

### 91.2 Store 删除算法（实现即 §8 等价）

```text
1 resolve（loadAll）→ 缺失 = FailureDocumentNotFound（零无关 mutation）
2 metadata 移除失败 = FailureMetadataRemove（不 GC、不报成功）
3 重载后仍引用同 contentHash ⇒ Success（共享工件保留）
4 无引用 ⇒ 按文档自身类型清理 text/<hash>{.txt|.json} + source/<hash>.bin
  （已缺失 = 已清理；逐路径，无 glob、无目录清扫）
5 全部成功 ⇒ Success；任一步失败 ⇒ SuccessWithCleanupWarning（metadata 保持已删，
  无回滚——P0-ML-F 明确不要求也不授权重建 metadata）
Fault seam = ManualStore::setRemoveInterposerForAutomation（默认直连
QFile::remove；测试注入确定性 metadata/GC 失败；无通用 FS 抽象、不碰真实目录权限）。
```

### 91.3 验证链（凭据缺席，全部实测）

```text
REAL RED = tests/test_manual_delete.cpp（新 target manual_delete；review API 缺失的
  运行时语义断言）exit 11（2 passed / 13 failed）
GREEN   = exit 0，15 passed / 0 failed（ML2-01/03/04-10/13/16-24/30 等价覆盖 +
  ML2-14 metadata 移除失败原子 + ML2-15 cleanup 失败可见警告；ML2-11 共享内容：
  同字节两记录 → 删一 → 另一记录仍在/仍可预览/source+text 工件存活 → 删尽 →
  元数据消失 + 工件清理；ML2-18 Running 阻断 + extraction 状态不变；ML2-19 他人
  Running 不阻断；ML2-20 consumed 不阻断；ML2-22 全路径零 runner 派发；
  ML2-24 删除非选中记录时选择按身份保持、绝无下一行静默重定向）
MUTATION-NX5-SHARED（bypass 共享引用保护）⇒ REAL RED exit 1，恰好
  ml2_sharedContent 在 QFile::exists(managedSource) 失败（幸存记录失去工件）
  → 精确逆向 → residue 0 → 复绿 15/15
MUTATION-NX6（bypass Pending 守卫）⇒ REAL RED exit 1，恰好 ml2_16 在
  !deleteOk(blocked) 失败 → 精确逆向 → residue 0 → 复绿 15/15
QML runtime gate = qml_manual_import_check 扩展 3 个 ML2 阶段（真实行按钮 →
  确认对话框显示「不会删除电脑上的原始文件」+ ModbusLens 边界文案 → Cancel 零
  mutation（库/选择不变）→ Confirm 恰好删除一条 + 选择/预览清空 + extraction
  禁用；相对计数断言）。工作期间发现并精确修复该 gate 的一处结构缺陷
  （stage 3 的 push 闭合被此前拼接吞掉，导致 3b–3f 延迟注册且计数错位——
  修复后按源码序执行）
targeted = 23/23 PASS / exit 0 / 64.1s（§20 全清单）
pre-commit fresh 树 = buildcceptance\session-ml-r2-release\（新建）
  configure RC 0（51.1s）· build RC 0（451/451，412.7s）
  inventory = 66 → **67**（+manual_delete）· full = **67/67 PASS / 0 failed /
  exit 0 / 197.5s**（manual_delete #32 Passed 1.26s；qml_manual_import_check #61
  Passed 1.78s；deployment_startup_check #35 Passed 34.89s）
```

### 91.4 提交 + post-commit ratification + 候选 + deployment

```text
behavior commit = 9bb599a34c0f4bea9b3be791caab9a6bfc592407
  「M12: add safe manual library deletion」（10 files / +1345 −2，parent
  3df4d71…，NO AMEND，内无 docs；samples 零改动）
提交后一致性 = git diff HEAD -- src tests CMakeLists.txt samples 为空
post-commit 树 = buildcceptance\session-ml-r2-postcommit-release\（新建）
  configure RC 0（150.8s）· build RC 0（451/451，406.9s）
  inventory = 67 · full = **67/67 PASS / 0 failed / exit 0 / 189.0s**
  （manual_delete #32 Passed；deployment_startup_check #35 Passed 27.01s）
候选 provenance = candidate\ModbusLens  exe = 6,487,562 B / SHA-256
        830a84262e1a23c8198b78af09a66cbd6207eacd4158678515464e9a7a2ba9bd
        （source exe ≡ candidate exe；manifest 1713 entries / root 1714 files）
  qwindows.dll = 804739071bba619b4a4312b5bb29a142545a64c4c80218e5b2e6672ad33ee8ac
  pdfium.dll   = d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b
  samples 边界 = 生成器不含 samples staging ⇒ 仍 repository-only；四样例 SHA-256
  与 §88.5 逐字节一致（03a4f93e…/ad54598e…/cb90134c…/bfa9f83a…）
deployment gate（独立复跑）= Passed 28.52s（sanitized 凭据缺席 env；
  qwindows 确证从 candidate 树加载；启动零 ModelScope 请求）
安全/数据权威审计 = 全过：删除路径零网络/零凭据；唯一触碰 = documents/、text/、
  source/ 下精确路径；原始外部文件不可达（ManualStore 从不写 originalPath）；
  无 glob/目录清扫；无 DeviceProfile 写入；无 Candidate 自动消费；无 Running
  cancel；无 schema/迁移；无 Open 按钮；QML 无文件/JSON/目录操作。
```

### 91.5 HUMAN ML-2 CHECKLIST（Agent 不执行；候选 = §91.4 新候选；建议用样例 A/B）

```text
A. 启动 ML-R2 新候选；B. 确认既有手册列表无需导入即时出现（ML-1 保持）；
C. 若库中无样例 A，则从 samples/ 复制到仓库外并导入
   （注意：仓库内 sample 文件本身是 ORIGINAL，必须存活）；
D. 选中该手册，确认预览可见；
E. 点击「删除」——确认框须显示确切手册名 +
   「将从 ModbusLens 删除已导入的说明书副本「…」。不会删除电脑上的原始文件。」；
F. 点击「取消」⇒ 手册仍在、选中仍在、预览仍在（零 mutation）；
G. 再次「删除」→ 确认 ⇒ 手册消失、选择清空、预览清空、
   AI 提取在重新选择前不可用；
H. 检查仓库内 samples 源文件仍在且未变；
I. 完全关闭并重启 ⇒ 被删手册不再出现，其余手册正常出现/预览；
J. （可选）同一样例导入两次（两条记录）→ 删一条 → 另一条仍可预览
   （ML2-11 自动化证据为权威）；
K. 无需触达 live ModelScope 即可完成 ML-2 验证；Pending/Running 阻断文案
   如需人工查看需另行显式 live 动作，本清单不假设。
```

### 91.6 状态（本节归档时点）

```text
Manual Delete            = IMPLEMENTED / AUTOMATED PASS
ML-2                     = IMPLEMENTED / AUTOMATED PASS / HUMAN REVIEW PENDING
                           （checklist = §91.5，入口 = §91.4 新候选）
Manual Library ML-1      = COMPLETE / HUMAN ACCEPTED（不变）
Cold-start hydration     = FIXED / AUTOMATED PASS / HUMAN PASS（不变）
Synthetic samples        = TRACKED / AUDITED / repository-only（不变，字节一致）
C3 first slice           = COMPLETE / HUMAN ACCEPTED · C3 overall = IN PROGRESS
C3 Edit                  = NOT STARTED / NOT AUTHORIZED
M12-D                    = NOT STARTED / NOT AUTHORIZED
canonical package        = NOT CREATED
verified LKGC            = 19e2f45341c3f15d1f27bc38a9ad728d268049e3（UNCHANGED；
                           behavior 提交不是 LKGC）
无 push / 无 tag / 无 amend。
```

---

## 92. MANUAL LIBRARY ML-2 — HUMAN DELETE ACCEPTANCE（SESSION ML-R2A · docs-only 归档）

> 性质：append-only 归档。Human 授权（逐字）：「授权：归档 Manual Library ML-2
> Human Delete acceptance PASS；仅 docs-only。将 ML-2 标记为 COMPLETE / HUMAN
> ACCEPTED；Human PASS 仅覆盖本次实际执行的删除用户工作流，Pending/Running 删除
> 阻断保持 IMPLEMENTED / AUTOMATED PASS，不虚构 Human live verification。不推进
> LKGC，不开始 C3 Edit/M12-D，不做 package/tag/push。」本节为 §91.6
> 「HUMAN REVIEW PENDING」的 current superseding note；§91 历史原文不改写。

### 92.1 验收对象边界（artifact boundary）

```text
Human acceptance 覆盖的候选 = ML-R2 post-commit 候选（§91.4）：
  buildcceptance\session-ml-r2-postcommit-release\candidate\ModbusLens\modbuslens.exe
  exe SHA-256 = 830a84262e1a23c8198b78af09a66cbd6207eacd4158678515464e9a7a2ba9bd
其 behavior 提交 = 9bb599a34c0f4bea9b3be791caab9a6bfc592407（§91.5）
本 docs 归档提交本身**不是**被测行为，**不是** LKGC。
```

### 92.2 Human 权威陈述（逐字）

```text
「这部分没有任何问题了。」
```

解释边界（授权范围内）：该结论**仅**针对其前准备的 ML-2 normal Manual Delete
checklist（§91.5 A–K）——即准备好的正常删除用户工作流无报告问题。采用冻结措辞：
**HUMAN PASS against the prepared ML-R2 normal Manual Delete acceptance flow;
no issue reported.** 不发明：精确点击时序、手册精确数量、所见警告文本、文件系统
痕迹、哈希痕迹、内部选择索引、精确缓存删除观察、网络痕迹。

### 92.3 Human 实际验证的 normal Delete workflow（授权边界内）

```text
A. ML-R2 候选启动；B. 既有 Manual library 保持可用；C. 可弃置/合成 Manual
作为删除目标；D. 被选手册删除前预览正常；E. 删除确认工作流可用；
F. Cancel 路径未产生意外删除；G. Confirm Delete 将已导入手册从 ModbusLens
库中移除；H. 删除后用户状态正常（含冻结的选择/预览语义）；
I. 原始源手册本就不应由 ModbusLens 删除；J. 重启持久化在 Human 删除
checklist 范围内：被删已导入记录不得复活，其余手册保持可用。
```

### 92.4 分类（归档）

```text
Manual Library ML-2 Human Delete acceptance = PASS / HUMAN VERIFIED
Manual Delete normal Human workflow         = HUMAN PASS
Delete confirmation normal workflow         = AUTOMATED PASS / HUMAN PASS
Cancel delete normal workflow               = AUTOMATED PASS / HUMAN PASS
Confirmed delete normal workflow            = AUTOMATED PASS / HUMAN PASS
Post-delete user workflow                   = AUTOMATED PASS / HUMAN PASS
Restart persistence after delete            = AUTOMATED PASS / HUMAN PASS
  （仅以准备的 normal Human workflow 覆盖程度为准）
Original external source safety             = AUTOMATED PASS + Human
                                              normal-flow acceptance
  （不得声称文件系统取证级 Human 证明）
Manual Library ML-2                         = COMPLETE / HUMAN ACCEPTED

—— 以下保持 AUTOMATED 证据边界，不升级为 HUMAN PASS ——
Pending Candidate delete guard   = IMPLEMENTED / AUTOMATED PASS / HUMAN LIVE NOT RUN
Running extraction delete guard  = IMPLEMENTED / AUTOMATED PASS / HUMAN LIVE NOT RUN
Shared-content protection        = IMPLEMENTED / AUTOMATED PASS
Metadata failure atomicity       = IMPLEMENTED / AUTOMATED PASS
GC failure warning semantics     = IMPLEMENTED / AUTOMATED PASS
```

### 92.5 ML-2 最终分类

```text
Manual Library ML-1      = COMPLETE / HUMAN ACCEPTED
Manual Library ML-2      = COMPLETE / HUMAN ACCEPTED
Manual Delete            = IMPLEMENTED / AUTOMATED PASS / HUMAN PASS
Cold-start hydration     = FIXED / AUTOMATED PASS / HUMAN PASS
C3 first behavior slice  = COMPLETE / HUMAN ACCEPTED
C3 overall               = IN PROGRESS（Edit 未实现未验收）
C3 Edit                  = NOT STARTED / NOT AUTHORIZED
M12-C overall            = IN PROGRESS
M12-D                    = NOT STARTED / NOT AUTHORIZED
REAL MODBUS HARDWARE     = NOT VERIFIED
canonical package        = NOT CREATED
verified LKGC            = 19e2f45341c3f15d1f27bc38a9ad728d268049e3
                           （UNCHANGED；docs-only 提交永不作 LKGC）
```

### 92.6 本节动作边界（docs-only）

仅归档 Human 权威验收证据与 superseding 分类；未改 src/tests/CMake/QML/samples ·
未 build / configure / CTest / 候选重生成 / deployment 复跑 / 生产 app 启动 ·
无 live ModelScope / 凭据读取 · 无 ManualStore/DeviceProfile 数据变更 ·
未开始 ML-2 Delete 实现（已归档为完成）/ C3 Edit / M12-D ·
未推进 LKGC · 未创建 canonical package · 未 push / 未 tag / 未 amend。


---

## 93. MANUAL LIBRARY ML-2 — VERIFIED LKGC ADVANCE（SESSION ML-R1B · docs-only 归档）

> 性质：append-only 归档。仅记录 Human 授权与 Git 实测验证；LKGC 推进本身是
> canonical docs 的状态转移，**不是**任何新行为。

### 93.0 Human 授权（逐字）

```text
授权：将 Manual Library ML-2 Human acceptance
作为新的 verified behavior baseline，
并将 verified LKGC 从

19e2f45341c3f15d1f27bc38a9ad728d268049e3

推进到

9bb599a34c0f4bea9b3be791caab9a6bfc592407。

本授权仅推进 verified LKGC；
不授权 C3 Edit、M12-D、package、tag 或 push。
```

### 93.1 Git 实测验证（授权目标合法性）

```text
git cat-file -t 9bb599a…                   = commit
git merge-base --is-ancestor 9bb599a… HEAD = exit 0（是 HEAD 祖先）
9bb599a~1..9bb599a changed paths           = 10 files / +1345 −2
  （ManualStore 删除 API + interposer seam · ManualImportController
    deleteDocumentById · CandidateExtractionController guards/命令/notice 面 ·
    DeviceProfilePage.qml 删除按钮/确认框/标签 · main.cpp 门禁 stages ·
    tests/test_manual_delete.cpp · CMakeLists target）
  ⇒ **behavior/data-bearing ML-2 commit**（含生产行为 + 测试 + 数据；
    如实记录其双重性质，不得描述为 docs-only）
9bb599a..HEAD = 2 个提交（b588997 · 04427fb），非 docs 路径 diff = 空
  ⇒ 其后提交全部 docs-only，永不作 LKGC
⇒ 9bb599a = ML-2 最后一个 behavior/data-bearing tree，为合法 LKGC 目标。
```

### 93.2 基线证据链（为什么是 9bb599a）

```text
· ML-R2：REAL RED（manual_delete exit 11，2/13）→ GREEN 15/15
  （ML2-01..30 等价覆盖）→ MUTATION-NX5-SHARED REAL RED（恰好共享内容测试）
  → 精确逆向 residue 0 → MUTATION-NX6 REAL RED（恰好 Pending 阻断测试）
  → 精确逆向 residue 0 → 复绿 15/15 → QML 门禁 3 个 ML2 阶段 PASS（含
  修复 gate 一处拼接结构缺陷）→ targeted 23/23 → pre-commit fresh 树
  67/67 PASS / 197.5s。
· ML-R2（post-commit）：第二棵全新树 66→67/67 PASS / 189.0s + 候选
  830a84262e1a23c8198b78af09a66cbd6207eacd4158678515464e9a7a2ba9bd
  （manifest 1713）+ deployment gate 28.52s。
· ML-R2A：Human Delete acceptance = PASS（§92：normal Delete workflow
  HUMAN PASS，两次分类归档）——Human 验收对象候选即由 19e2f45 之后的
  bb996a5 基线…（更正：验收候选由 19e2f45..9bb599a 链上的 9bb599a
  内容生成）。
```

### 93.3 推进后的 canonical 状态

```text
verified LKGC = 9bb599a34c0f4bea9b3be791caab9a6bfc592407（Human authorized）
前一 verified  = 19e2f45341c3f15d1f27bc38a9ad728d268049e3（转历史链一环，原文保留）
Manual Library ML-1 = COMPLETE / HUMAN ACCEPTED
Manual Library ML-2 = COMPLETE / HUMAN ACCEPTED（新 verified behavior baseline）
Manual Delete            = IMPLEMENTED / AUTOMATED PASS / HUMAN PASS
Pending Candidate delete guard = IMPLEMENTED / AUTOMATED PASS / HUMAN LIVE NOT RUN
Running extraction delete guard = IMPLEMENTED / AUTOMATED PASS / HUMAN LIVE NOT RUN
Cold-start hydration     = FIXED / AUTOMATED PASS / HUMAN PASS
Synthetic samples        = TRACKED / AUDITED / SYNTHETIC / repository-only
C3 first slice           = COMPLETE / HUMAN ACCEPTED
C3 overall               = IN PROGRESS（Edit 未实现未验收）
C3 Edit                  = NOT STARTED / NOT AUTHORIZED（本授权明确不授权）
M12-D                    = NOT STARTED / NOT AUTHORIZED（本授权明确不授权）
REAL MODBUS HARDWARE     = NOT VERIFIED
canonical package        = NOT CREATED · tag = 仅 v1.0.0 · push = 未发生
```

**本节动作边界（docs-only）**：仅归档授权 + Git 验证 + 状态转移记录；
未改 src/tests/CMake/QML/samples · 未 build / configure / CTest ·
未生成候选 / 未跑 deployment gate · 无 live ModelScope / 凭据读取 ·
未开始 ML-2 追加实现 / C3 Edit / M12-D · 未创建 canonical package ·
未 tag / 未 push · 本节所在提交 docs-only 永不作 LKGC。

## 94. C3 EDIT IMPLEMENTATION SLICE（SESSION C3-R3B · HUMAN-AUTHORED CANDIDATE VALUE CONFIRMATION）

> 会话 packet 授权（逐字）：「授权：启动 M12-C C3 Edit implementation slice，
> 严格按 C3-H1..H10，尤其 C3-H8 冻结语义实施」。本节记录该 slice 的完整
> 协议链：Preflight / contract review → RED → GREEN → 负向对照 → targeted →
> fresh full → behavior commit → post-commit ratification → 候选 → deployment。
> **Human 视觉/功能验收 = PENDING**（本 slice 不推进 LKGC）。

### 94.1 实施范围（C3-H8 冻结语义落地）

- `CandidateExtractionController::confirmEditedCandidate(QVariantMap, QString)`：
  编辑确认 = **一次显式 Human authority 动作**。链路 = 值寻址定位 Pending
  Candidate（防陈旧 UI 对象）→ C3-H4 目标存在性 → **C3-H3/4.5 与 Accept 同一
  evidence freshness gate**（证据仅作 SOURCE CONTEXT，永不"证明"编辑后的值）
  → C3-H8/4.6 经 `ProfileController::applyCandidateField` 同一受控 staged-copy
  写（全量 validateDeviceProfile / commit-once / 无 Save）→ C3-H5 以 Accepted
  消费。失败原子：Candidate 保持 PendingReview、draft 不动。
- QML（DeviceProfilePage）：行内「编辑」按钮 + 有界模态编辑对话框
  （Popup.NoAutoClose），四向上下文显式分离：当前档案值 / **AI 原始建议** /
  说明书依据（只读，措辞明确"不自动证明修改后的值"）/ 人工确认值（可编辑）。
  Cancel = 零 mutation；确认成功关闭对话框；确认失败对话框保持打开并在
  **对话框内**显示确定性错误。页面级 candidateEditNotice（含 token 着色）。
- QML review 门禁（`--qml-candidate-review-check`）：R1 翻转为"行内 Edit 控制
  必须存在"（C3-R2 时代的"无 Edit"断言随 C3-H8 冻结语义失效）；新增
  E1（修缓存→seed→四向上下文可见）→ E2（Cancel 零 mutation）→
  E3（人工值权威 + 消费 + dirty）→ E4（无自动保存 + 显式 Save 持久人工值）→
  E5（陈旧证据确认在对话框内原子可见失败）。
- 单元测试：`test_candidate_review.cpp` 新增 edit_00..edit_12（13 个用例）。

### 94.2 过程缺陷（V2 Debug/Issue Trace，真实留痕）

1. **QML 对话框内错误不可见**。Observed：E5 FAIL
   （"freshness failure is not visible inside the edit dialog"）+
   运行时 `ReferenceError: editErrorText is not defined`（qml:1329/1330）。
   Expected：确认失败时错误必须在保持打开的模态对话框内可见。Evidence：
   gate 输出 + QML 行号。Root Cause：Popup content 内的子项绑定对
   Dialog 自身属性**不能非限定解析**；`candidateEditError` 的
   `visible/text` 用了非限定 `editErrorText`，而失败通知只写在页面级
   （被模态遮挡）。Fix：`confirmCandidateEdit()` 失败分支显式
   `candidateEditDialog.editErrorText = result.text`；绑定限定为
   `candidateEditDialog.editErrorText`（与全文件限定引用惯例一致）。
   Verification：重建后 E5 PASS、全门禁 PASS。Regression protection：
   E5 阶段本身（对话框内错误可见性断言）。
2. **o18 陈旧期望修正（非删测试）**。`o18_noCandidateLevelReviewActions`
   沿用 C3-R2 时代"无 Edit 动作"断言；C3-H8 冻结后 `confirmEditedCandidate`
   成为合法动作。按 C3-R2 对 o18 的同一修正模式，将
   `confirmEditedCandidate` 加入 approved 集合并更新注释；**persistence-shape
   守卫（save/persist/store/commit）原样保留**——O 时代边界真正保护的部分
   继续被断言。22/22 PASS。
3. **agent_runtime b04 负载抖动（与本 slice 无关）**。fresh full 第 2 轮
   `b04_maxToolRounds` FAIL（`h.failed.count()==1`）；同树第 1 轮 PASS、
   独立复跑 3×28/28 PASS；本 slice 改动文件与 agent_runtime 零交集。
   判定：ctest 并行负载下的时序型 flake，非本 slice 引入；不在本 slice
   修复（纪律 9），留待专门任务。
4. **凭据环境说明**：用户环境 ambient 存在 `MODBUSLENS_MODELSCOPE_MODEL`
   （模型名 override，非 secret）；`MODELSCOPE_API_KEY` 全程缺席。
   全部 ctest / 门禁运行以 `env -u` 双变量剥离启动；`--qml-*` harness
   依 P-R1C 设计在控制器存在前自行 qunsetenv。无 live ModelScope 调用。

### 94.3 验证链（真实命令与数字）

- **REAL RED**：candidate_review 2/13（11 个 Edit 语义失败）→ GREEN：
  candidate_review **36/36**（含 edit_00..12）。
- **QML 门禁**：`--qml-candidate-review-check` PASS
  （R1..R5+E1..E5；offscreen **与** windows 双平台，exit 0 / REVIEWFAIL 0）。
- **负向对照**：**NX7**（`if (false && !revalidateEvidence(...))` 屏蔽
  freshness gate）REAL RED **恰好** edit_05/06/07 → 精确逆向 → 36/36 复绿；
  **NX8**（`applyCandidateField` 改用 AI proposedValue 忽略人工值）REAL RED
  **恰好** edit_01/02/04/09/10 → 精确逆向 → 36/36 复绿。两次还原后
  `git diff` residue 0，无 mutation 进入提交。
- **targeted**：manual_delete 15/15；manual_import 31/31；
  `--qml-manual-import-check` PASS；`--qml-profile-editor-check` PASS
  （共享 QML 回归）。
- **pre-commit fresh 树** `build/acceptance/session-c3-r3b-release/`（凭据缺席）：
  configure RC0 / build **451/451** / full ctest：第 1 轮 65/66（o18 陈旧期望，
  见 94.2-2）→ 修正 → 第 2 轮 65/66（b04 负载抖动，见 94.2-3）→ 第 3 轮
  **66/66 PASS / exit 0 / 148.36s**（deployment_startup_check #35 Passed）。
- **behavior 提交 = `4a77673`**（subject `M12: add Human-authored candidate
  value edit (C3-H8)`；6 files / +874 −29；parent `6ce54f7…`；**NO AMEND**；
  内无 docs；提交后 tree-vs-HEAD diff = 空）。
- **post-commit ratification**（新树
  `build/acceptance/session-c3-r3b-postcommit-release/`）：configure RC0 /
  build 全量 / **66/66 PASS / exit 0 / 185.87s**。
- **候选**：`candidate\ModbusLens\` 由 canonical `modbuslens_candidate`
  target 从零重建（1713 files，manifest written）；exe 6,529,589 B，
  SHA-256 `6370c55347a861caa50638ef87cde26bd509d40b8ec65a92f540ee2bba19ffd5`
  （candidate ≡ source 同值）；**deployment gate 独立复跑 Passed 25.92s**。

### 94.4 实施后 canonical 状态

- **C3 Edit = IMPLEMENTED / AUTOMATED PASS（行为提交 `4a77673`）**；
  **C3 Edit Human 视觉/功能验收 = PENDING**（本会话为"仅准备"清单；
  checklist 沿 §82.6 模式 + E1..E5 人工对应项）。
- **verified LKGC = `9bb599a…`（UNCHANGED）**：行为提交不是 LKGC；
  LKGC 推进需 Human acceptance + 独立授权（§93 模式）。
- 其余不变：C3 first slice / ML-1 / ML-2 = COMPLETE / HUMAN ACCEPTED；
  C3 batch / durable provenance / schema change = DEFERRED（C3-H1/H7）；
  M12-D = NOT STARTED / NOT AUTHORIZED；canonical package = NOT CREATED；
  无 tag / 无 push。
- 本节动作边界（实现 + 自动化验证）：改 src/QML/tests 并构建/测试；
  **未**推进 LKGC、未创建 canonical package 目录之外的分发物、未 tag、
  未 push、无凭据读取 / 无 live ModelScope。

## 95. C3 EDIT CANONICAL RELEASE RATIFICATION（SESSION C3-R3B-R2 · docs-only ADDENDUM · SUPERSEDING EVIDENCE）

> Human 授权（逐字）：「授权：执行 C3-R3B canonical ratification 补证，仅使用包含
> -DMODBUSLENS_PYTHON_EXECUTABLE=D:/Anaconda3/python.exe 的 canonical fresh
> Release 配置，从当前已提交行为树重新 configure/build，确认 inventory=67 并完成
> full 67/67；若通过，则从该 post-commit canonical tree 重新生成 Human acceptance
> candidate、运行 deployment gate，并仅做 docs-only addendum。不得修改行为
> 代码/测试/CMake，不重做 RED/GREEN/NX7/NX8，不推进 LKGC，不开始 M12-D，不做
> package/tag/push。」本节 = **superseding evidence**，不覆盖 §94 原始记录。

### 95.1 背景与前置证明

- **R1 对账结论（T027 对话记录 / session C3-R3B-R1）**：§94 两次 full 运行树
  （`session-c3-r3b-release` / `session-c3-r3b-postcommit-release`）configure 时
  遗漏了 canonical 显式变量 `MODBUSLENS_PYTHON_EXECUTABLE`
  （CMakeLists.txt:1118 的显式 opt-in 门禁，PATH 永不探测），导致
  `c1b_dependency_materializer` 未注册（66 vs 基线 67）。仓库层面零丢失：
  `git diff 9bb599a..4a77673 -- CMakeLists.txt` 为空。
- **本会话前置门禁**：RESYNC 实测 HEAD =
  `8b4d2d403a6a73459ca68eb5aa7414fe175ba899`（tracked clean / cached 空 /
  `git diff --check` PASS / untracked 仅 `_ctx.py`、`_dump.py` / tag 仅
  v1.0.0 / `git ls-files build` 空）；`git diff 4a77673..HEAD -- src tests
  CMakeLists.txt samples` = **空** ⇒ 当前 HEAD 行为/测试/CMake 树 ≡
  `4a77673` 行为树（其后仅有 docs 提交）。

### 95.2 canonical configure / build / inventory / full

- **树**（全新，不复用任何旧树）：
  `build/acceptance/session-c3-r3b-r2-canonical-release/`。
- **configure 命令**（凭据缺席：`env -u MODELSCOPE_API_KEY
  -u MODBUSLENS_MODELSCOPE_MODEL`）：
  `cmake -S . -B build/acceptance/session-c3-r3b-r2-canonical-release -G Ninja
  -DCMAKE_MAKE_PROGRAM=D:/QT/Tools/Ninja/ninja.exe -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_PREFIX_PATH=D:/QT/6.11.1/mingw_64
  -DCMAKE_CXX_COMPILER=D:/QT/Tools/mingw1310_64/bin/g++.exe
  -DMODBUSLENS_PYTHON_EXECUTABLE=D:/Anaconda3/python.exe`
  → **RC 0**；cache 实测 `MODBUSLENS_PYTHON_EXECUTABLE:FILEPATH=
  D:/Anaconda3/python.exe`；configure 输出**不含** "not registered" 消息
  （gate 静默 = 已注册）；PDFium 156.0.8066.0 pinned
  （sha256 `d42c452a…f14b`）+ libzip(static) 离线物化。
- **build**：`ninja` 全量 → **RC 0，451/451**。
- **inventory 硬门禁**：`ctest -N` = **Total Tests: 67**；8 个必需测试全部
  在位：#29 candidate_orchestration / #30 candidate_transport /
  #31 candidate_review / #32 manual_delete / #35 deployment_startup_check /
  **#36 c1b_dependency_materializer** / #64 qml_candidate_review_check /
  #65 qml_candidate_review_check_windows。
- **full unfiltered**（无 -R/-E/-L/-LE/--tests-regex/--exclude-regex，
  凭据缺席）：**67/67 PASS / 0 failed / exit 0 / 191.77s（一次通过，
  未触发 agent_runtime flake 补救路径）**。关键计时：
  c1b_dependency_materializer #36 Passed 26.71s（首次纳入 canonical full）·
  candidate_review #31 Passed 1.50s · candidate_orchestration #29 Passed
  0.44s · manual_delete #32 Passed 1.01s · agent_runtime #47 Passed 6.61s ·
  qml_candidate_review_check #64 Passed 3.40s ·
  qml_candidate_review_check_windows #65 Passed 3.15s ·
  deployment_startup_check #35 Passed 35.79s。

### 95.3 canonical 候选（唯一推荐的 C3 Edit Human 验收入口）

- 同树 canonical `modbuslens_candidate` target FROM ZERO 重建：
  `candidate\ModbusLens\`（1713 files, manifest written）。
- **candidate exe ≡ source exe**：均为 6,529,589 B，SHA-256
  **`16d05557f103432dd8fe0740e1abcd7f0a8dbe780b0a37018bb08c425e4faa20`**。
- 计数（精确措辞）：**manifest `files` 数组 = 1713 条**；**root 实际文件
  （递归、含 manifest 本身）= 1714**（不含 manifest = 1713，与条目一一
  对应）；**目录 = 90**。
- `platforms/qwindows.dll` SHA-256 =
  `804739071bba619b4a4312b5bb29a142545a64c4c80218e5b2e6672ad33ee8ac`；
  `pdfium.dll` SHA-256 =
  `d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b`。
- **deployment gate 独立复跑**（净环境，凭据缺席）：**Passed 26.48s**
  （candidate 树内 qwindows 加载证明 + manifest/runtime hash 校验 + 有限
  --qml-smoke-test 启动，零 provider 请求）。
- **候选取代关系**：本候选为 **唯一推荐的 C3 Edit Human acceptance 入口**；
  旧候选（SHA-256 前缀 `6370c553…`，§94）转为 **历史 pre-canonical-ratification
  候选**——其行为来源 `4a77673` 相同、provenance 有效，只是附着于 66-test
  非 canonical configure，不作为推荐 Human gate 工件。旧证据树保留不删。

### 95.4 canonical 状态（superseding）

- **C3 Edit = IMPLEMENTED / AUTOMATED PASS（behavior `4a77673`）**；
  **C3 Edit canonical Release ratification = 67/67 PASS**（本节）——
  §94 的 evidence limitation（"66 registered due omitted Python cache
  variable"）由本节 superseding evidence 取代；§94 历史 66/66 记录保留
  不删。
- **C3 Edit Human acceptance = PENDING**（本会话仅更新准备清单，
  §16 的 16 步 checklist 不变，验收入口改为 §95.3 canonical 候选）；
  **C3 overall = IN PROGRESS**。
- **verified LKGC = `9bb599a…`（UNCHANGED）**；M12-D = NOT AUTHORIZED；
  canonical package = NOT CREATED；无 tag（仅 v1.0.0）/ 无 push / 无 amend。
- 本节动作边界（docs-only）：本会话零 src/tests/CMake/QML/samples 改动、
  零行为语义改动、零 RED/GREEN/NX 重做；仅 configure/build/test/候选/
  deployment 只读性质运行 + 本 docs addendum。

## 96. CONSENT DIALOG LIFECYCLE REPAIR（SESSION C3-R3C · behavior `c68d2bb` · HUMAN RETEST PENDING）

> Human 授权（逐字）：「授权：修复 C3 Human gate 中发现的 Consent Dialog
> 生命周期缺陷：Human 点击'同意并提取'并成功进入提取流程后，Consent Dialog
> 必须立即自动关闭，不得等待 provider 结果或 Candidate 生成；Cancel 行为、
> session consent、provider dispatch、Candidate 生命周期及 C3 Edit 语义均
> 不得改变。允许最小 QML/main harness/test 修改，执行 REAL
> RED→GREEN→targeted→canonical fresh full regression→behavior
> commit→post-commit candidate/deployment→docs addendum；不授权其他行为
> 修改、不推进 LKGC、不开始 M12-D/package/tag/push。」

### 96.1 Human live defect（真实观察，T027 档案）

Human 使用 canonical C3 Edit 候选（`session-c3-r3b-r2-canonical-release`
树候选，exe `16d05557…`）实际操作：选择 Manual → 点击 AI 提取 → Consent
Dialog 出现 → 点击「同意并提取」→ 页面后台进入「正在提取…」且 Candidate
正常产生进入 PendingReview，**但 Consent Dialog 不自动关闭**，仍以 modal
覆盖页面。判定：consent Agree / provider dispatch / Candidate 生成均
WORKING，**Dialog lifecycle = HUMAN FAIL**；C3 Edit Human acceptance 暂被
此窄 UI 缺陷阻塞。

### 96.2 RCA（真实机制）

生产 `ModelScopeCandidateRunner::begin()` → `ModelScopeHttpClient` 用
**`QEventLoop::exec()` 同步执行整个 HTTP 往返**（56-67 行）。QML Agree
handler 原顺序 = `grantConsent()` 先、`close()` 后：真实路径上 handler
**阻塞在 grantConsent() 的嵌套事件循环里**直至 provider 完成，close() 只能
在其后执行——modal 对话框因此活过整个提取过程。harness（runConsentCheck）
无凭据 → `begin()` 本地拒绝同步失败 → close() 立即执行 → R05 断言一直
PASS，缺陷从未被现有 gate 覆盖。

### 96.3 修复（最小 QML 变更）

Agree handler 改为 **close 先于 grant**（`candidateConsentDialog.close()` →
`candidateController.grantConsent()`）。语义安全论证：dialog 打开期间 state
必为 ConsentRequired（唯一 open 路径）；close 先行后 grant 同步阻塞期间
对话框已消失（Human 语义 3.1「立即」）；若 state 已迁移（陈旧 dialog），
grantConsent() 幂等 no-op，close 与 Cancel 结果一致（§7B 本地拒绝语义保持）。
**零改动**：consent 存储/会话语义、requestExtraction/grantConsent/runner/
transport/HTTP/parser、Candidate 生命周期、Cancel、C3 Edit/Accept/Reject、
evidence freshness、ProfileController、ManualStore。

### 96.4 测试基础设施（test-only seam + gate 扩展）

- `CandidateExtractionController::setRunnerForAutomation(ICandidateExtractionRunner*)`
  = 最小 test-only seam（同 `ManualStore::setRemoveInterposerForAutomation`
  类），传 nullptr 恢复生产 owned runner；产品 UI 不可达。
- `runConsentCheck` 新增 **BlockingConsentRunner**（QEventLoop spin 400ms
  再同步失败完成，复现生产同步窗口；永不产生 Candidate）+ 4 个 stage：
  CD-16（第二文档不同 content identity → O06 重新 consent → 1000x700
  geometry with dialog OPEN；metadata 目录为 UUID 序，选择按 documentId
  身份而非硬编码 index）→ **AUTOCLOSE**（150ms 探针在 Running 窗口内触发：
  断言 state=running 且 **dialog 不可见**；点击阻塞 ~400ms；断言恰好一次
  dispatch、完成后不重开、状态推进）→ CD-12（已 grant 的 session consent
  重触发不弹 dialog）。PASS note 更新。

### 96.5 验证链（凭据缺席，无 live ModelScope）

- **REAL RED**：`AUTOCLOSE probe: running=true dialogOpen=true` →
  CONSENTFAIL "the consent dialog stayed open during the synchronous
  dispatch (Human defect)"（exit 1；单点失败，其余 stage 全过）。
- **GREEN**：修复后 `AUTOCLOSE probe: running=true dialogOpen=false`，
  CONSENT CHECK PASS（R01..R07 + R2-01..R2-07 + AUTOCLOSE + CD-12/CD-16），
  exit 0；windows 平台 gate exit 0 / CONSENTFAIL 0。
- **NX-CONSENT-CLOSE**：把 close 移回 grant 之后（原始缺陷顺序）→ REAL RED
  恰好 AUTOCLOSE 核心断言 → 精确逆向（无 checkout/restore/reset）→ 复绿
  （CONSENTFAIL 0）。
- **targeted**：consent 双平台 + candidate_review / orchestration /
  extraction / transport / qml_candidate_review 双平台 + manual_delete +
  qml_manual_import 双平台 + qml_profile_editor + smoke / geometry / nav /
  focus = **16/16 PASS**（orchestration exe 因 controller 头改动重建后通过）。
- **pre-commit canonical fresh 树**
  `build/acceptance/session-c3-r3c-release/`（含
  `-DMODBUSLENS_PYTHON_EXECUTABLE=D:/Anaconda3/python.exe`，凭据缺席）：
  configure RC0（"not registered" 消息缺席）/ build **451/451** / inventory
  **67**（c1b_dependency_materializer #36 在位）/ full unfiltered
  **67/67 PASS / exit 0 / 194.74s 一次通过**（agent_runtime #47 Passed
  6.47s 无 flake；deployment #35 35.92s；c1b #36 27.47s；candidate_review
  #31 1.59s；manual_delete #32 1.09s）。
- **behavior 提交 = `c68d2bb`**（`M12: close consent dialog when extraction
  starts`；4 files / +244 −9；parent `09646c2…`；NO AMEND；内无 docs；
  `git diff HEAD -- src tests CMakeLists.txt` = 空）。

### 96.6 post-commit canonical 候选（唯一推荐的 Human retest 入口）

- 新树 `build/acceptance/session-c3-r3c-postcommit-release/`（同 canonical
  configure）：configure/build RC0（451/451）/ `ctest -N` = **67**。
- **candidate exe ≡ source exe**：6,553,611 B，SHA-256
  **`66bef371a0ee556ee06165bd92dbdbe8f805d787377db4fe124fe2dd45957225`**。
- 计数：manifest `files` = **1713 条**；root 实际文件含 manifest = **1714**；
  目录 = **90**。qwindows `80473907…8ac`；pdfium `d42c452a…f14b`。
- **deployment gate 独立复跑 Passed 27.39s**（净环境、候选树内 qwindows、
  零 provider 请求）。

### 96.7 状态与 Human retest checklist（仅准备，不执行）

- **Consent Dialog auto-dismiss = IMPLEMENTED / AUTOMATED PASS / HUMAN
  RETEST PENDING**；**C3 Edit = IMPLEMENTED / AUTOMATED PASS / HUMAN
  ACCEPTANCE IN PROGRESS**；C3 overall = IN PROGRESS；ML-1 / ML-2 =
  COMPLETE / HUMAN ACCEPTED；**verified LKGC = `9bb599a…` UNCHANGED**；
  M12-D = NOT STARTED / NOT AUTHORIZED；canonical package = NOT CREATED；
  tag = 仅 v1.0.0；push = 无；本节所在提交 docs-only 永不作 LKGC。
- Human retest（使用 `session-c3-r3c-postcommit-release/candidate/ModbusLens/`
  候选，**无需重做 ML-1/ML-2/67-test 工程验证**）：
  1. 选择能产生 Candidate 的 Manual；
  2. 点击 AI 提取候选；
  3. Consent Dialog 出现；
  4. 点击「同意并提取」；
  5. **要求：Consent Dialog 立即消失**；
  6. 页面可显示「正在提取…」；
  7. 等待 Candidate；
  8. **要求：Candidate 出现且 Consent Dialog 保持关闭**；
  9. 从被中断处继续既有 C3 Edit Human acceptance（§16 的 16 步清单）。
  Human 不执行 live inference 授权之外的动作；本 session 不做 live
  ModelScope。

## 97. C3 EDIT + CONSENT HUMAN ACCEPTANCE ARCHIVE（SESSION C3-R3D · docs-only CLOSURE）

> Human 授权（逐字）：「授权：归档 M12-C C3 Edit 与 Consent Dialog lifecycle
> Human acceptance PASS；仅 docs-only。将 C3 Edit 标记为 COMPLETE / HUMAN
> ACCEPTED，Consent Dialog auto-dismiss 标记为 IMPLEMENTED / AUTOMATED PASS /
> HUMAN PASS，并逐项归档本次：Cancel、edited-confirm、Discard、Save+restart
> 的 Human 结果；C3 overall 仅标记 READY FOR FINAL CLOSURE AUDIT，不直接推断
> COMPLETE。不推进 LKGC，不开始 M12-D，不做 package/tag/push。」
> 本节动作边界：纯 docs 归档——零 src/tests/CMake/QML/samples 改动、零
> build/configure/CTest/candidate/deployment/app launch、零 live ModelScope、
> 零凭据读取。

### 97.1 验收对象边界（Human-tested artifact）

Human acceptance 针对 **`buildcceptance\session-c3-r3c-postcommit-releasecandidate\ModbusLens\ModbusLens.exe`**（SHA-256
`66bef371a0ee556ee06165bd92dbdbe8f805d787377db4fe124fe2dd45957225`，
behavior 提交 `c68d2bbb277096fd7fb9a76d99d7b588da6461f0`）。该候选行为树
**同时包含** C3 Edit（`4a77673da719d5626024f5e4c5d8c9e86e56f0fd`）与 Consent
lifecycle repair（`c68d2bbb277096fd7fb9a76d99d7b588da6461f0`）两个行为变更。
**不得声称** 本 docs 提交本身被 Human 测试（docs-only 提交非被测行为、非
LKGC）。

### 97.2 Human 权威观察（逐字归档 · 授权分类）

**4.1 Consent lifecycle**：
「点'同意并提取'后，同意框马上消失；之后 Candidate 正常出现，同意框没有
再回来。」
分类：Consent Dialog auto-dismiss = **HUMAN PASS**；Candidate-after-consent
flow = **HUMAN PASS**；No dialog re-open after Candidate = **HUMAN PASS**。
（不虚构毫秒级时序。）

**4.2 Edit Cancel**：
「编辑窗口四项信息正常，改值后点取消，Candidate 还在，厂商没变，也没有未
保存修改。」
分类：Edit dialog context visibility = **HUMAN PASS**；Edit Cancel =
**HUMAN PASS**；Cancel zero Profile mutation = **HUMAN PASS**；Candidate
remains Pending after Cancel = **HUMAN PASS**；No dirty state after Cancel =
**HUMAN PASS**。

**4.3 Edited-confirm**：
「确认后 Candidate 消失，厂商变成我输入的值，出现未保存修改，没有自动
保存。」
分类：Human-authored value authority = **HUMAN PASS**；edited-confirm =
**HUMAN PASS**；Candidate consumption = **HUMAN PASS**；draft-only mutation =
**HUMAN PASS**；dirty state = **HUMAN PASS**；no auto-save = **HUMAN PASS**。

**4.4 Discard**：
「放弃修改后厂商恢复旧值，未保存修改消失，Candidate 没有重新出现。」
分类：Discard after edited-confirm = **HUMAN PASS**；persisted baseline
restoration = **HUMAN PASS**；dirty clear = **HUMAN PASS**；consumed
Candidate non-resurrection = **HUMAN PASS**。

**4.5 Explicit Save + restart**：
「重新 Edit 后保存，完全重启后人工修改的厂商值仍然存在，Candidate 没有重新
出现。」
分类：explicit Save after Human Edit = **HUMAN PASS**；restart persistence =
**HUMAN PASS**；Human-authored edited value persistence = **HUMAN PASS**；
Candidate session-only semantics = **HUMAN PASS**。

### 97.3 Evidence boundary（不升级不削弱）

Human **直接验证**：normal live Consent flow；Consent auto-dismiss；
Candidate generation；Edit dialog context presentation；Cancel behavior；
Human value winning over original proposal；Candidate consumption；draft-only
/ dirty behavior；no auto-save；Discard rollback；Candidate
non-resurrection；explicit Save；restart persistence；Candidate
session-only restart behavior。

Human **未直接验证**（保持 **AUTOMATED PASS**，不升级为 HUMAN PASS）：stale
evidence failure；document missing；contentHash mismatch；ambiguous excerpt；
full-profile invalid negative case；NX7 mutation；NX8 mutation；Edit automation
zero-network behavior；schema key-set invariant；1000×700 exact geometry
measurements；keyboard automation；all 67 tests。

### 97.4 C3-H8 Human authority classification（冻结语义归档）

- Human-edited value = **HUMAN-AUTHORED / HUMAN-CONFIRMED**（非 AI-proposed）；
- Original AI proposal + Evidence = session context；
- Original Evidence = **不自动证明** Human-edited value；
- edited-confirm = 一次显式 Human authority 动作；
- success = 仅 Profile draft；persistence = 仅显式 Save；
- Candidate = 按既有 Accepted 语义消费；
- durable provenance = **NONE / DEFERRED**（C3-H7）；schema change = **NONE**；
- single Candidate only；no batch（C3-H1）。

### 97.5 最终分类（superseding §94/§95/§96 的 PENDING/IN PROGRESS 状态）

- **C3 Edit = COMPLETE / HUMAN ACCEPTED**（实现 = IMPLEMENTED / AUTOMATED
  PASS / HUMAN PASS；authority = HUMAN-AUTHORED / HUMAN-CONFIRMED）；
- Edit Cancel = AUTOMATED PASS / HUMAN PASS；
- edited-confirm = AUTOMATED PASS / HUMAN PASS；
- Discard after Edit = AUTOMATED PASS / HUMAN PASS；
- Explicit Save persistence = AUTOMATED PASS / HUMAN PASS；
- Candidate session-only behavior = AUTOMATED PASS / HUMAN PASS；
- **Consent Dialog auto-dismiss = IMPLEMENTED / AUTOMATED PASS / HUMAN PASS**；
- C3 first slice（Accept/Reject）= COMPLETE / HUMAN ACCEPTED（不升级不削弱）。

### 97.6 C3 overall = READY FOR FINAL CLOSURE AUDIT（不关闭）

C3 overall **不**标记 COMPLETE。设为 **READY FOR FINAL CLOSURE AUDIT**：
Accept/Reject first slice、Edit、Consent repair 均已 Human accepted，但
canonical closure 仍需一次只读审计确认：C3-H1..H10 全部满足；无必需 C3
能力仍 PENDING；deferred 项确属 deferred / 不在 C3 完成范围；无过时
canonical 状态与 closure 矛盾。该审计**不在本 session 执行**。

### 97.7 LKGC 与里程碑状态

- **verified LKGC = `9bb599a34c0f4bea9b3be791caab9a6bfc592407`（UNCHANGED）**
  ——不自动推进到 `4a77673…` 或 `c68d2bb…`；任何 LKGC 推进需独立 Human
  授权。
- **事实记录**：latest Human-tested behavior-bearing commit =
  **`c68d2bbb277096fd7fb9a76d99d7b588da6461f0`**（Human 验收候选同时包含
  Edit 与 Consent lifecycle repair）。
- Manual Library ML-1 = COMPLETE / HUMAN ACCEPTED；ML-2 = COMPLETE / HUMAN
  ACCEPTED；**M12-C overall = IN PROGRESS**（至 C3 final closure audit
  完成）；**M12-D = NOT STARTED / NOT AUTHORIZED**；REAL MODBUS HARDWARE =
  NOT VERIFIED；canonical package = NOT CREATED；tag = 仅 v1.0.0；push =
  无；本节所在提交 docs-only 永不作 LKGC。

## 98. M12-C C3 + M12-C OVERALL — DOCS-ONLY FINAL CLOSURE（SESSION C3-R3F · HUMAN AUTHORIZED）

> Human 授权（逐字）：「授权：执行 M12-C C3 与 M12-C overall 的 docs-only
> final closure。依据 C3-R3E GO 结论，将 C3 overall 归档为 COMPLETE / HUMAN
> ACCEPTED，并将 M12-C overall 归档为 COMPLETE / HUMAN ACCEPTED；保持
> C1a/C1b/C2/C3 各自既有 accepted evidence 与历史原文，不重写历史。最终
> closure 中准确记录 C3 behavior lineage（first-slice 主体、standing Discard、
> Edit、Consent repair 分立），并记录 latest Human-tested behavior-bearing
> commit = c68d2bbb277096fd7fb9a76d99d7b588da6461f0。本授权仅 docs/governance
> closure；verified LKGC 仍保持 9bb599a34c0f4bea9b3be791caab9a6bfc592407 不
> 推进；不开始 M12-D，不做 package/tag/push。」本节动作边界：纯 docs——零
> src/tests/CMake/QML/samples 改动、零 build/configure/CTest/candidate/
> deployment/app launch、零 live ModelScope、零凭据读取。

### 98.1 依据：C3-R3E 只读 closure audit = GO（2026-10-05，会话内判定）

C3-R3E（strict read-only，无 docs 提交）实测结论：C3-H1..H10 全部 SATISFIED
（H1、H7 = SATISFIED — HUMAN-APPROVED DEFERRED PART），0 BLOCKER；
Accept/Edit/Reject 全链路可用；AI 无法直接写入 verified Profile（C3-H10
唯一权威链 + MANUAL SAVE）；OCR = future capability 非阻塞。本 closure 前
已重新实测其关键事实：§81.0 C3-H1..H10 合同原文在档；lineage 四 commit
（98.2）type=commit 且均为 HEAD 祖先；§94–§97 档案在位。

### 98.2 C3 behavior lineage（Git 实测 changed paths，非 subject 推断）

| Commit | 角色 | 实测 changed paths |
| --- | --- | --- |
| `61f641ef2d9045cd90dd7598dcf78ab36b5af800` | **C3 first-slice core**：ProfileFieldCandidate review / Accept / Reject / evidence revalidation / ProfileController draft 集成 / Human review UI + tests（**非** standing Discard） | CMakeLists.txt、core/candidate/CandidateExtraction.{h,cpp}、main.cpp、CandidateExtractionController.{h,cpp}、ProfileController.{h,cpp}、Main.qml、DeviceProfilePage.qml、tests×2 |
| `bb996a5bfc5416e3392c6fd709508e21aeab62c2` | **Standing Discard**（「放弃修改」常驻动作，走既有权威 discard 工作流） | main.cpp、DeviceProfilePage.qml |
| `4a77673da719d5626024f5e4c5d8c9e86e56f0fd` | **C3 Edit**（Human-authored/confirmed 值、同 freshness gate、全量校验、draft-only、不 auto-save） | main.cpp、CandidateExtractionController.{h,cpp}、DeviceProfilePage.qml、tests×2 |
| `c68d2bbb277096fd7fb9a76d99d7b588da6461f0` | **Consent lifecycle repair**（Agree 进入提取后立即关闭对话框；consent/dispatch/Candidate/Edit 语义不变） | main.cpp、CandidateExtractionController.{h,cpp}、DeviceProfilePage.qml |

支持性非行为提交（不作为 behavior baseline，不作 LKGC）：`f5c1c906` =
C3-R2A test-only strengthening（仅 tests/test_candidate_review.cpp）；及
§94–§97 等 docs 归档提交。**latest Human-tested behavior-bearing commit =
`c68d2bbb277096fd7fb9a76d99d7b588da6461f0`**（`c68d2bb..HEAD` 非 docs 路径
diff = 空）；Human 验收候选 =
`buildcceptance\session-c3-r3c-postcommit-release\candidate\ModbusLensModbusLens.exe`（SHA-256
`66bef371a0ee556ee06165bd92dbdbe8f805d787377db4fe124fe2dd45957225`，≡ 同树
source exe）。

### 98.3 C3-H1..H10 closure matrix（依据 C3-R3E，不重复完整报告）

| 项 | Closure status | 证据指针 |
| --- | --- | --- |
| C3-H1（单 Candidate 裁决） | SATISFIED — HUMAN-APPROVED DEFERRED PART（batch/RegisterEntryCandidate 明确排除出 v1，另行授权） | §81.0 H1；o18；first-slice acceptance |
| C3-H2（Accept 语义） | SATISFIED | §81.0 H2；acceptCandidate + 三方对照；first-slice Human acceptance |
| C3-H3（Evidence freshness gate） | SATISFIED | §81.0 H3；revalidateEvidence 四级 + core round-trip/uniqueness；edit_05/06/07 + NX7 + QML R4/E5 |
| C3-H4（Profile conflict） | SATISFIED（RegisterEntryCandidate 并发语义须未来重新审计——合同原文） | §81.0 H4；hasOpenProfile 守卫；o14 |
| C3-H5（Accepted lifecycle） | SATISFIED | §81.0 H5；consumeCandidate；o13/edit_10/§97 non-resurrection |
| C3-H6（Rejected lifecycle） | SATISFIED | §81.0 H6；rejectCandidate 零 mutation；r3 系列 |
| C3-H7（Durable provenance） | SATISFIED — HUMAN-APPROVED DEFERRED PART（v1 不持久化 = 冻结要求；DEFER 到未来独立 Human decision） | §81.0 H7；DeviceProfile schema 无 Candidate 字段；o18 persistence-shape 守卫 |
| C3-H8（Edit authority） | SATISFIED（HUMAN-AUTHORED / HUMAN-CONFIRMED） | §81.0 H8；confirmEditedCandidate + 四向对话框；edit_00..12 + NX8 + §97 五组 HUMAN PASS |
| C3-H9（Undo / re-review） | SATISFIED | §81.0 H9；无专用 undo；standing Discard；edit_10/11；§97 |
| C3-H10（架构边界） | SATISFIED | §81.0 H10；applyCandidateField 白名单 + validateDeviceProfile + MANUAL SAVE 唯一持久化；o18 形状守卫 |

**0 BLOCKER。** batch / RegisterEntryCandidate / durable provenance / 专用
undo / OCR / M12-D / real hardware = 明确 deferred/future（98.6），非缺陷。

### 98.4 C3 final status（canonical current truth）

- **M12-C C3 = COMPLETE / HUMAN ACCEPTED**；C3 overall = COMPLETE / HUMAN
  ACCEPTED。
- C3 first slice = COMPLETE / HUMAN ACCEPTED；C3 Edit = COMPLETE / HUMAN
  ACCEPTED（authority = HUMAN-AUTHORED / HUMAN-CONFIRMED）。
- Accept = IMPLEMENTED / AUTOMATED PASS / HUMAN PASS；Reject =
  IMPLEMENTED / AUTOMATED PASS / HUMAN PASS；Standing Discard = IMPLEMENTED /
  AUTOMATED PASS / HUMAN PASS；Consent Dialog auto-dismiss = IMPLEMENTED /
  AUTOMATED PASS / HUMAN PASS；Explicit Save persistence = AUTOMATED PASS /
  HUMAN PASS；Candidate session-only lifecycle = AUTOMATED PASS / HUMAN PASS。
- 自动化-only 负向/故障路径（stale evidence / document missing /
  contentHash mismatch / ambiguous excerpt / full-profile invalid / NX7 /
  NX8 / zero-network automation / schema key-set / 1000×700 exact geometry /
  keyboard automation / all 67 tests）**保持 AUTOMATED PASS，不升级**（§97.3
  boundary 沿用）。

### 98.5 M12-C exit condition closure（§17.2 原文映射，canonical truth 实读）

| §17.2 M12-C exit 要素 | 满足证据 |
| --- | --- |
| 说明书导入 PDF/DOCX/TXT/MD 产生 Candidate 列表 | C1a（TXT/MD）= COMPLETE / HUMAN ACCEPTED（§79.5 时点 + 后续 §89–§93）；C1b（PDF/DOCX）= COMPLETE / HUMAN ACCEPTED |
| 每项带 evidence/source/confirmation 契约 | C2 = COMPLETE / HUMAN ACCEPTED（§79.5；含 Candidate/Evidence foundation、ModelScope adapter + strict parser、production orchestration、consent = HUMAN PASS、transport/credential = HUMAN LIVE PASS） |
| **Accept/Edit/Reject 全链路可用** | C3 = COMPLETE / HUMAN ACCEPTED（本节；98.3/98.4） |
| AI 无法直接写入 verified Profile | C3-H10 唯一权威链：Human → review controller → ProfileController draft → validateDeviceProfile → explicit MANUAL SAVE → ProfileStore |
| OCR = future capability | 非阻塞（canonical 原文，§15/§17.2） |

**M12-C overall = COMPLETE / HUMAN ACCEPTED**——frozen M12-C scope（C1a +
C1b + C2 + C3）完整满足 §17.2 exit。**边界保持**：这不表示未来 Manual
intelligence 工作永久完成、OCR 已实现、RegisterEntryCandidate/batch/durable
provenance 已存在、M12-D 已完成或 real hardware 已验证。

### 98.6 Deferred / future boundary（不得重新分类为缺陷）

batch review；RegisterEntryCandidate；durable provenance / Candidate audit
store；provenance 所需 schema change；dedicated Candidate undo；OCR；
M12-D；real Modbus hardware validation；canonical package/publication。

### 98.7 LKGC 与里程碑边界

- **verified LKGC = `9bb599a34c0f4bea9b3be791caab9a6bfc592407`（UNCHANGED，
  不推进）**；latest Human-tested behavior-bearing commit =
  `c68d2bbb277096fd7fb9a76d99d7b588da6461f0`。
- **LKGC TARGET READINESS = READY FOR SEPARATE HUMAN AUTHORIZATION**（依据
  C3-R3E：commit 存在、HEAD 祖先、behavior-bearing（changed paths 实测）、
  其后提交全部 docs-only、Human 候选含该树、canonical 67/67 覆盖同一代码
  谱系）。推进与否 = 独立 Human 决定。
- **M12-D = NOT STARTED / NOT AUTHORIZED**（M12-C closure 不隐式授权
  M12-D）；REAL MODBUS HARDWARE = NOT VERIFIED；canonical package =
  NOT CREATED；tag = 仅 v1.0.0；push = 无；本节所在提交 docs-only 永不作
  LKGC。

## 99. VERIFIED LKGC ADVANCE TO FINAL HUMAN-ACCEPTED M12-C BASELINE（SESSION C3-R3G · docs-only · HUMAN AUTHORIZED）

> Human 授权（逐字）：「授权：将已完成并 Human Accepted 的 M12-C final
> behavior 作为新的 verified behavior baseline，并将 verified LKGC 从
> 9bb599a34c0f4bea9b3be791caab9a6bfc592407 推进到
> c68d2bbb277096fd7fb9a76d99d7b588da6461f0。本授权仅推进 verified LKGC 并做
> docs/governance 归档；不授权 M12-D、package、tag 或 push。」本节动作边界：
> 纯 docs/governance——零 src/tests/CMake/QML/samples 改动、零
> build/configure/CTest/candidate/deployment/app launch、零 live ModelScope、
> 零凭据读取。

### 99.1 Git 合法性证明（写前实测，只读）

- `git cat-file -t c68d2bbb277096fd7fb9a76d99d7b588da6461f0` = **commit**。
- `git merge-base --is-ancestor c68d2bb… HEAD` = exit 0（**HEAD 祖先**）。
- **behavior-bearing**（changed paths 实测，非 subject 推断）：
  `c68d2bb~1..c68d2bb` = `src/main.cpp`、
  `src/ui/candidate/CandidateExtractionController.{h,cpp}`、
  `src/ui/qml/pages/DeviceProfilePage.qml` —— Consent Dialog lifecycle
  repair；其祖先已含 C3 first-slice core（`61f641ef…`）、standing Discard
  （`bb996a5…`）、C3 Edit（`4a77673…`）。
- **post-target descendants 审计（本 session docs commit 之前）**：
  `c68d2bb..HEAD` = 恰好 3 个后代（`c1ef03f…`、`8bb1205…`、`67474c7…`），
  `git diff --name-status c68d2bb..HEAD` 全部为 docs 路径
  （docs/BACKLOG.md、docs/PROJECT_STATUS.md、docs/devlog/2026-10-04.md、
  docs/devlog/2026-10-05.md、docs/tasks/T027-…md）⇒ **无任何后续行为变更**。
  （本 session 的 docs 提交之后将为第 4 个 docs-only 后代——时间口径以此
  为准，不沿用 pre-commit 计数。）

### 99.2 Human 验收 provenance（canonical evidence 引用，不重跑）

- Human-tested candidate =
  `buildcceptance\session-c3-r3c-postcommit-release\candidate\ModbusLens  ModbusLens.exe`（SHA-256
  `66bef371a0ee556ee06165bd92dbdbe8f805d787377db4fe124fe2dd45957225`，≡ 同树
  source exe），行为树 = `c68d2bbb277096fd7fb9a76d99d7b588da6461f0`。
- Human acceptance（§97.2 逐字归档）：Consent auto-dismiss PASS；Edit Cancel
  PASS；Human-edited value authority PASS；Candidate consumption PASS；
  draft-only / no auto-save PASS；Discard rollback PASS；Candidate
  non-resurrection PASS；explicit Save PASS；restart persistence PASS；
  Candidate session-only restart behavior PASS。**docs 提交非被测行为。**
- canonical 自动化证据（既有，不重跑）：C3 Edit RED/GREEN + NX7/NX8 +
  targeted + canonical Release 67/67（§94/§95）；Consent RED/GREEN +
  NX-CONSENT-CLOSE + targeted + canonical Release 67/67（§96）；post-commit
  candidate provenance + deployment gate Passed（§96.6）；C3-R3E closure
  audit = GO（§98.1 引）；C3-R3F M12-C = CLOSED / HUMAN ACCEPTED（§98）。

### 99.3 LKGC 推进后的 canonical 状态

- **verified LKGC = `c68d2bbb277096fd7fb9a76d99d7b588da6461f0`（HUMAN
  AUTHORIZED · VERIFIED BEHAVIOR BASELINE）**；前一 verified
  `9bb599a34c0f4bea9b3be791caab9a6bfc592407` 转历史链一环（原文保留不删）。
- 选择依据 = c68d2bb 为 latest Human-tested behavior-bearing commit；其后
  （含本 session）docs 提交**永不作 LKGC**。
- **M12-C = COMPLETE / HUMAN ACCEPTED**（C1a/C1b/C2/C3 各自既有 accepted
  状态保持不变，不重开、无新 acceptance claim）；C3 = COMPLETE / HUMAN
  ACCEPTED；latest Human-tested behavior-bearing commit = c68d2bb。
- Deferred / future（不得重新分类为缺陷）：batch review、
  RegisterEntryCandidate、durable Candidate provenance / audit store、
  provenance schema changes、dedicated Candidate undo、OCR、M12-D、real
  Modbus hardware validation、canonical package/publication。
- **M12-D = NOT STARTED / NOT AUTHORIZED**（LKGC 推进不授权 M12-D）；REAL
  MODBUS HARDWARE = NOT VERIFIED；canonical package = NOT CREATED；tag = 仅
  v1.0.0；push = 无；本节所在提交 docs-only 永不作 LKGC。**NEXT =
  SEPARATE HUMAN DECISION ON M12-D**。

## 100. M12-D V1 HUMAN CONTRACT FREEZE D1–D5（SESSION M12-D-R1 · docs-only · HUMAN AUTHORIZED）

> Human 授权原文（逐字）：「D1～D5 全部同意，按你修正后的方案冻结。」
> 本节动作边界：纯 docs——零 src/tests/CMake/QML/samples 改动、零
> build/CTest/candidate/deployment/app launch、零 ModelScope、零凭据读取。
> 本授权不授权 M12-D implementation、LKGC advancement、package/tag/push。

### 100.0 R0 HOLD provenance（保留原文，不重写）

Session M12-D-R0（strict read-only，无 repo commit——不虚构）实测归档：
无任何 M12-D production skeleton；**architecture feasibility = YES**；
`ManualStore` canonical text path 存在；ModelScope HTTP / Agent 基础设施
存在；M12-C Evidence helpers 机械可复用；`CandidateExtractionController`
不得成为 Q&A owner；Diagnosis Agent Q&A（M10）**不是** M12-D；M12-C
Candidate Evidence 语义**不自动**成为 M12-D citation 语义。R0 HOLD 的原因
= 产品语义未决（P0-D-A..H），非工程不可行。本节由 Human D1–D5 裁定解除
HOLD。

### 100.1 D1 — Question / Manual scope（HUMAN-FROZEN）

M12-D v1 question scope = **当前选中的单个 Manual**。
1. 无选中 Manual ⇒ Ask 不可用/拒绝。
2. 一次 Ask 属于一个 selected Manual identity/context。
3. 切换 selected Manual ⇒ 清空当前 Q&A context、answer、citations。
4. 无多 Manual 选择。
5. 无 whole-library Q&A。
6. 无 batch Manual Q&A。
7. 无 whole-library RAG 聚合。
多 Manual = **FUTURE / OUT OF CURRENT V1**（非缺陷）。

### 100.2 D2 — Citation contract（HUMAN-FROZEN）

FOUND 的 citation identity 复用既有 manual evidence identity **形状**：
`documentId` / `contentHash` / `pageNumber` / `textStart` / `textEnd` /
`excerpt`。**措辞冻结**：复用字段/identity 形状与适用的 deterministic
mechanics；**不引入** M12-C Candidate-provenance 语义全集。
- **FOUND 要求 ≥ 1 个有效 citation**（cardinality 只冻结下界；不发明
  per-sentence / per-claim / global uniqueness 等更强要求）。
- 本地 deterministic citation 校验要求：
  (A) citation document identity 属于 selected Manual context；
  (B) contentHash 与当前 managed Manual 内容匹配；
  (C) range 合法；
  (D) range → excerpt **精确 round-trip**。
- **显式冻结（修正后的方案）**：**不要求** global excerpt uniqueness——
  手册内他处存在重复文本**本身不**使 citation 失效（document/hash/range/
  excerpt round-trip 精确即有效）。
- Citation 语义含义 = 「该 Manual evidence 是该 answer 主张的来源/支撑
  上下文」；**不**等于数学证明，**不**自动保证任意模型推理正确。
- 若实现允许多 citations，则所有**显示的** citations 必须全部通过校验；
  不得呈现无效 citation。

### 100.3 D3 — Answer states + authority consequence（HUMAN-FROZEN）

用户可见语义状态：**FOUND / NOT_FOUND / INSUFFICIENT_EVIDENCE**；独立技术
状态：**ERROR**。
- **FOUND** = 存在足够有效 Manual evidence 支持设备/手册事实性回答。要求
  = 非空 answer + ≥1 个本地校验通过的 citation。
- **NOT_FOUND** = 在 selected Manual 内的本次检索未找到相关证据。**不得**
  表述为「该事实在 Manual 中任何位置都不存在」。无 general-knowledge 替代。
- **INSUFFICIENT_EVIDENCE** = 找到相关 Manual 材料，但不足以可靠回答。
  系统不得猜测。
- **ERROR** = 网络/provider 失败、provider 输出 malformed、parser 失败、
  未知 document citation、stale contentHash、invalid range、excerpt
  mismatch、本地校验失败等。**ERROR 不得转换为 NOT_FOUND 或
  INSUFFICIENT_EVIDENCE**。provider 可返回结构化语义 status，但**显示
  FOUND 必须经过本地 deterministic 校验**（provider 声明本身不足）。

**Authority consequence（INFORMATIONAL ONLY）**：M12-D answer **无权**：
create/modify/Accept/Edit/Reject Candidate；modify DeviceProfile draft /
Save DeviceProfile / modify ProfileStore；modify Manual metadata / delete
Manual；dispatch Modbus transaction；write M10/M11 truth。M12-D v1 对
answer **无 Accept / Edit / Save 动作**——Human 仅阅读 answer/citations。
**不得建立第二条 Profile authority pipeline；M12-C C3-H10 原样不动。**

### 100.4 D4 — Provider / Consent / Privacy（HUMAN-FROZEN）

- Provider family = 复用既有 ModelScope 基础设施；**不**创建第二凭据
  store、新 provider-selection UI、新 provider family、新 durable credential
  机制。底层 ModelScope credential/model 配置可复用既有 accepted
  mechanics；本授权**不**创建新 provider/model 配置 UX。
- **Q&A consent 与 M12-C extraction consent 分离**：既有 extraction consent
  **不**自动授予 Q&A consent。Q&A 需要当前应用 session 内、针对 M12-D Q&A
  feature 的**显式 Human consent**。
- Consent 披露必须以 Human 可读措辞声明：云端 AI 将接收 **Human 的
  question** 与 **selected Manual 的必要 excerpts/context**。
- **不修改、不复用**已 accepted 的 C3 Consent Dialog lifecycle 语义
  （C3-R3C）；M12-D 拥有**独立** consent surface/state。

### 100.5 D5 — Session / persistence + Manual delete interaction（HUMAN-FROZEN）

**Session-only persistence**：不持久化 question history / answer history /
citation history / raw provider response / conversation memory；不创建新
Q&A schema、history database、audit store、migration、DeviceProfile 字段。
Restart 后 Q&A history 不恢复。v1 不要求 durable M12-D provenance。

**Manual delete interaction**：Running M12-D Q&A **不阻止**既有 ML-2
Manual Delete 工作流（**不**复制 extraction 的 Pending/Running delete-block
政策）。selected/referenced Manual 被成功删除时：
1. 使该 Q&A request/generation 失效；
2. best-effort cancel 在途 provider 工作；
3. 清空 selected Q&A context；
4. 清空已显示 answer；
5. 清空已显示 citations；
6. 不把 stale answer 保留为当前 truth；
7. 属于被失效 generation 的 late response **必须丢弃**。
ML-2 delete authority 与确认工作流原样不变；M12-D 不成为 Manual deletion
owner。

### 100.6 工程细节（降级为 implementation details，不再问 Human）

在满足 D1–D5 前提下属于工程细节：独立 ManualQaController（或等效隔离
Q&A orchestration surface）；strict structured provider output；fail-closed
parser；single-flight；Idle/Running/Completed/Failed 内部状态模型；
request generation / supersession token；late-response drop；best-effort
cancel；deterministic fake provider；zero-network automation path；
credential-absence 测试；本地 citation validator 实现；防御性资源/尺寸
限制（不改变已批准产品语义）；QML geometry/focus/accessibility mechanics；
selected Manual 内的 chunking/retrieval 实现选择；UI placement（只要
selected Manual identity/context 无歧义）。**除非实现暴露真实的对外语义
冲突，不再询问 Human。**

**Output contract engineering boundary**：确切 JSON/provider 响应语法 =
工程细节，但必须能表示 semantic status / answer / citations[] 并允许对
D2/D3 的 deterministic 校验；strict / fail-closed——malformed 输出不得成为
FOUND、invalid citation 不得成为 FOUND、provider 声明本身不足以授权 FOUND
显示。除非 deterministic 实现所必需，不把任意 JSON key 名冻结为 Human
产品政策。

**Search / RAG boundary**：v1 不授权跨 manual retrieval、whole-library
vector search、多 manual synthesis、conversation memory。selected Manual
内的确定性 retrieval/chunking 策略 = 工程选择；embedding / reranker /
search backend 选择 = 工程/依赖工作（除非实质改变隐私或对外可见语义）。

### 100.7 M12-C 保护 + future/out-of-v1 边界

M12-C = COMPLETE / HUMAN ACCEPTED / FINAL BASELINE FROZEN。M12-D 不得修改
Manual import 语义、Candidate extraction 语义、Candidate lifecycle、
Accept/Edit/Reject、C3-H1..H10、ProfileController authority、ProfileStore
persistence、Consent lifecycle repair、Manual Library ML-1/ML-2 行为；共享
低层 utility 仅可在不引入错误语义合同的前提下复用。

**FUTURE / OUT-OF-V1（不得分类为当前缺陷）**：multi-manual Q&A；
whole-library Q&A；whole-library RAG；conversation memory；durable Q&A
history；durable Q&A provenance/audit；cross-session history；new provider
families；provider-selection UI；M12-D 对 Profile/Candidate 的 mutation；
OCR expansion；automatic device inference；M12-C reopening。

### 100.8 Entry gate after freeze + provisional first slice（RECOMMENDED / IMPLEMENTATION-READY · NOT AUTHORIZED）

**M12-D CONTRACT = HUMAN-FROZEN**；**M12-D ARCHITECTURE FEASIBILITY = GO**；
**M12-D IMPLEMENTATION = NOT STARTED**；**M12-D FIRST IMPLEMENTATION SLICE
= READY FOR SEPARATE HUMAN AUTHORIZATION**（本 session 不实现）。

推荐首切片（归档为推荐，非授权）：独立 Manual Q&A orchestration +
selected single Manual + single question + independent Q&A consent +
ModelScope request seam + strict structured answer + FOUND / NOT_FOUND /
INSUFFICIENT_EVIDENCE / ERROR + local citation validation + session-only
state + zero Profile/Candidate mutation + late-response invalidation +
Manual Delete invalidation integration + deterministic fake-provider tests
+ minimal QML Human surface。Explicit non-goals：multi-manual；history
persistence；schema change；Profile/Candidate mutation；M12-C 行为变更；
OCR；M12-D package work。

未来 test matrix（feasibility 推荐，非 canonical）：QA-01 selected manual
required · QA-02 empty question rejected · QA-03 consent semantics ·
QA-04 exactly one dispatch · QA-05 Running state · QA-06 valid structured
answer · QA-07 citation round-trip · QA-08 wrong document rejected ·
QA-09 contentHash mismatch rejected · QA-10 invalid range rejected ·
QA-11 excerpt mismatch rejected · QA-12 malformed provider output ·
QA-13 no Profile mutation · QA-14 no Candidate mutation · QA-15 no
auto-save · QA-16 delete interaction · QA-17 stale/late response ·
QA-18 restart/session semantics · QA-19 credential absence · QA-20
zero-network fake path · QA-21 geometry · QA-22 keyboard/focus · QA-23
M12-C regression。

### 100.9 本节动作边界

仅 docs：T027 §100 + PROJECT_STATUS + BACKLOG + devlog。零 src/tests/CMake/
QML/samples 改动；零工程重跑；无 tag / 无 push / 无 amend。本节所在提交
docs-only 永不作 LKGC。**NEXT = HUMAN / REVIEWER 对本合同归档的 review，
THEN SEPARATE IMPLEMENTATION AUTHORIZATION。**

## 101. M12-D FIRST BEHAVIOR SLICE — SINGLE-MANUAL EVIDENCE-BACKED Q&A（SESSION M12-D-R2 · behavior `80e4326` · HUMAN RETEST PENDING）

> Human 授权（逐字）：「授权：启动 M12-D first behavior slice，严格按 T027
> §100 已冻结的 D1～D5 实施 single-selected-Manual / single-question /
> session-only evidence-backed Q&A。……允许最小必要的
> core/controller/provider/QML/test/CMake 修改，并执行：REAL RED→GREEN→至少
> 一个 citation-validator negative control→至少一个 late-response/delete
> negative control→targeted regression→canonical fresh Release full
> regression→behavior commit→post-commit fresh ratification→candidate/
> deployment→docs archive。不得实现：multi-manual、whole-library RAG、
> history persistence、schema change、Profile/Candidate mutation、OCR、新
> provider family；不得推进 LKGC，不得做 package/tag/push。」

### 101.1 架构与实现（D1–D5 逐条落地）

- **core/manualqa/ManualQaContract.{h,cpp}**（纯 C++20，零 Qt）：
  `ManualQaStatus`（found/not_found/insufficient_evidence）、
  `ManualQaCitation`（documentId/contentHash/pageNumber/textStart/textEnd/
  excerpt 五元组形状）、`validateManualQaCitation`（D2 A–D：document 归属 +
  contentHash 新鲜度 + range 合法 + 精确 round-trip；**无 global uniqueness**
  ——§100.2 修正方案）、`validateManualQaFoundResult`（FOUND = 非空 answer +
  ≥1 全部有效的 citation；任一 citation 失效 ⇒ 整体 ERROR，绝不降级为
  INSUFFICIENT）、`buildManualQaContextBlocks`（deterministic bounded 检索：
  行锚定 chunk + 问题 token overlap 排序，≤6 块——§100.6 工程细节）。
- **ui/manualqa/IManualQaRunner.h**：Q-free runner seam（begin/cancel/
  beginCount；completion 绑定 generation）——语义面不暴露任何 C2
  extraction 类型。
- **ui/manualqa/ModelScopeManualQaRunner.{h,cpp}**：生产 runner，复用既有
  `ModelScopeAgentClient` 异步模型（单 timeout owner、同源重定向、TLS on、
  generation 守卫、无 token 日志）；凭据缺席 = fail-closed begin false（零
  网络）；strict fail-closed JSON reader（未知 status/malformed/缺字段/错
  型 = 解析失败 ⇒ ERROR，绝不 coerce 成语义态）；raw payload 仅内存（D5）。
  prompt 冻结信任边界（说明书文本 = 不可信源数据、仅依证据回答、三态输出、
  strict JSON 形状）。
- **ui/manualqa/ManualQaController.{h,cpp}**：**独立** Q&A orchestration
  owner（不改 CandidateExtractionController / ProfileController）。D4：
  Q&A consent = session 级独立 grant（不读不写 extraction consent）；D1：
  观察既有 Manual selection（switch ⇒ 失效 + 清空）；D5：documentsChanged
  观察 bound Manual 消失（成功删除 ⇒ 失效 + best-effort cancel + 清空
  context/answer/citations，late response 必弃）；generation 单飞行；零
  文件 I/O（static audit：0 QFile/QSaveFile；仅 ManualStore::loadText 只读）。
- **QML（DeviceProfilePage/Main）**：candidateCard 内「手册问答」触发钮 +
  非模态右侧 Drawer（manualQaCard：所选说明书标识/问题输入/提问/Running/
  FOUND answer + citation 卡（"来源/支撑上下文，不构成绝对正确性证明"措辞）/
  NOT_FOUND / INSUFFICIENT_EVIDENCE 本地文案 / ERROR）+ **独立**
  manualQaConsentDialog（modal、NoAutoClose、披露 question + 摘录发送）。
  无 chat transcript、无 Accept/Edit/Save answer 控件（gate 反射断言）。

### 101.2 测试与验证链（凭据缺席、零 live provider）

- **REAL RED**：`--qml-manual-qa-check` 的 surface 反射 stages 先于实现
  运行——7 项 Q&A surface 断言全 FAIL（exit 1；compile-safe reflection，
  runtime semantic RED）。
- **GREEN 单元**：`manual_qa`（tests/test_manual_qa.cpp）**36/36**——
  QA-01..QA-35 语义矩阵（本地拒绝/consent 分离与单次派发/citation 四门/
  重复文本不失效/零 citations/空 answer/NOT_FOUND·INSUFFICIENT 不显示
  provider 文本/malformed/provider failure/零 Profile·Candidate·Manual
  mutation/switch·delete 失效与 late drop/cancel 计数/supersession/新
  controller 无历史/无持久化 artifact/生产 runner 无凭据 fail-closed/
  parser 变体/context blocks 确定性有界）。
- **GREEN QML 门禁**：`--qml-manual-qa-check` **exit 0**（S1..S10：面板打
  开/selected Manual 标识/首次 Ask 停在 Q&A consent/披露文案/Cancel 零
  dispatch/Agree 恰一次 dispatch/FOUND answer+citation 渲染/三态本地文案/
  switch 失效 + late drop/delete 清空 + late drop/无 mutation 控件/
  1000×700 + 1280×937 containment）；windows 平台 exit 0 / FAIL 0。
  期间发现并修复两个 gate-caught 缺陷：① Drawer 内容 id 缺失
  （manualQaQuestionInput ReferenceError——补 id）；②
  hasSelectedManual 的 NOTIFY 缺失（首次 import 后 QML enabled 绑定 stale
  ——handleManualContextChanged 无条件 emit manualContextChanged）。
- **MUTATION-NX-QA-CITATION**（`false &&` 屏蔽 contentHash 新鲜度门）：
  REAL RED **恰好 qa10** → 精确逆向 → 36/36 复绿（residue 0）。
- **MUTATION-NX-QA-LATE**（`false &&` 屏蔽 late-response generation drop）：
  REAL RED **恰好 qa24/25/28/30** → 精确逆向 → 36/36 复绿（residue 0）。
- **targeted regression**：manual_qa + qml_manual_qa_check(+windows) +
  manual_import(+windows gate) + manual_delete + candidate×4 + C3 review
  gates + consent gates + profile_controller/device_profile/editor gate +
  agent_runtime/ai_client + smoke/geometry/nav/focus = **24/24 PASS**（期间
  修复 Drawer 内容 id 缺失导致的 ReferenceError）。
- **inventory ledger**（§22 HARD RULE）：BASELINE = 67
  （session-c3-r3c-postcommit-release 实测）；NEW = 3（#32 manual_qa、
  #69 qml_manual_qa_check、#70 qml_manual_qa_check_windows）⇒ EXPECTED 70；
  fresh 树 configure 后实测 **ctest -N = 70** 且 67 基线名全在
  （manual_delete #33、c1b_dependency_materializer #37 等）——configure 含
  `-DMODBUSLENS_PYTHON_EXECUTABLE=D:/Anaconda3/python.exe`，"not registered"
  消息缺席。
- **pre-commit fresh 树 `session-m12d-r2-release/`**（凭据缺席）：configure
  RC0 / build RC0 **474/474** / full unfiltered **70/70 PASS / exit 0 /
  275.35s**（manual_qa #32 2.94s、deployment #36 44.21s、c1b #37 70.32s、
  qml_manual_qa_check #69 3.63s、#70 windows 3.74s）。
- **行为提交 = `80e43261597532dcc8d9f45b5372117c6e684674`**
  （`M12: add evidence-backed single-manual Q&A`；12 files / +3527 −1；
  parent `7f85485…`；NO AMEND；内无 docs；提交后
  `git diff HEAD -- src tests CMakeLists.txt samples` = 空）。
- **post-commit 树 `session-m12d-r2-postcommit-release/`**：configure RC0 /
  `ctest -N` = **70** / build RC0 / full unfiltered **70/70 PASS / exit 0 /
  232.33s**（manual_qa #32 2.21s、deployment #36 36.90s、c1b #37 37.57s、
  qml_manual_qa #69/#70 3.86s/3.43s）。
- **candidate**（post-commit 树，canonical generator FROM ZERO）：
  candidate exe ≡ source exe（**6,795,902 B，SHA-256
  `5324e0fbb699dd500d763dfc23e2d154478149b64f296abacdfb84abd41f7dae`**）；
  manifest `files` = **1713 条** / root 实际文件含 manifest = **1714** /
  目录 = **90**；qwindows `80473907…8ac`；pdfium `d42c452a…f14b`。
- **deployment gate（环境事件如实归档）**：post-commit 树的 gate 因
  **环境级文件锁**无法运行——generator 的 REMOVE_RECURSE 撞上
  `candidate/ModbusLens/modbuslens.exe` 的系统级句柄锁（无任何进程/模块
  持有者可查，Get-Process/Get-CimInstance/Modules 扫描均为空；等 40+ 分钟
  不释放；MsMpEng 在位）。**deployment 验证改在 pre-commit canonical 树
  `session-m12d-r2-release/` 运行：Passed 43.79s**（该树源码 ≡ `80e4326`
  提交内容——`git diff HEAD -- src tests CMakeLists.txt samples` 在提交后
  为空；锁释放后可在 post-commit 树补跑）。**无任何伪造/绕过**。

### 101.3 状态与 Human 验收清单（仅准备，不执行）

- **M12-D first behavior slice = IMPLEMENTED / AUTOMATED PASS**；
  **M12-D Human acceptance = PENDING**；**M12-D overall = IN PROGRESS**；
  M12-D contract = HUMAN-FROZEN（§100）；multi-manual / whole-library RAG /
  history persistence / schema changes / Profile·Candidate mutation = NONE /
  FUTURE（§100.7 边界原样）；M12-C = COMPLETE / HUMAN ACCEPTED / FINAL
  BASELINE FROZEN；**verified LKGC = `c68d2bb…` UNCHANGED**；canonical
  package = NOT CREATED；tag = 仅 v1.0.0；push = 无；本节所在提交 docs-only
  永不作 LKGC。
- **Human 验收候选（唯一推荐入口）** =
  `build\acceptance\session-m12d-r2-postcommit-release\candidate\ModbusLens\
  ModbusLens.exe`（SHA-256
  `5324e0fbb699dd500d763dfc23e2d154478149b64f296abacdfb84abd41f7dae`）。
  **短清单**：① 启动候选；② 导入/选中能产出答案的说明书；③ Q&A 面板
  显示所选说明书；④ 输入说明书中有明确答案的问题；⑤ 首次提问弹出
  **独立的 Q&A consent**（明确提到提问 + 摘录/上下文发送云端）；⑥ Cancel
  一次：零回答/零请求；⑦ 再提问 + 同意；⑧ consent 关闭、Running 出现、
  结果随后出现；⑨ FOUND 回答可见；⑩ ≥1 条 citation 可见且摘录与所选
  说明书对应；⑪ 无 Candidate 产生；⑫ 无 Profile dirty/mutation；
  ⑬ 切换说明书：旧回答/引用清空；⑭ 重启：无问答历史。（citation-hash
  破坏与 late-response 竞态由自动化覆盖，无需 Human 复现。）

## 102. M12-D EXACT HUMAN CANDIDATE DEPLOYMENT RATIFICATION（SESSION M12-D-R2A · docs-only superseding addendum）

> Human 授权（逐字）：「授权：执行 M12-D-R2A post-commit deployment
> provenance 补证。仅解决 M12-D-R2 留下的 exact post-commit Human candidate
> deployment gate 缺口；不得修改行为代码、测试、CMake、QML 或 M12-D 合同，
> 不重做 RED/GREEN/NX，不推进 LKGC。……deployment 必须验证的就是最终推荐给
> Human 的那个 exact candidate。通过后仅追加 docs-only superseding addendum
> ……不重写 §101 历史。不得 package/tag/push。」

### 102.1 前置证明与 PATH A 判定

- **行为树等价硬门禁**：`80e4326…` type = commit、是 HEAD 祖先；
  `git diff 80e4326..HEAD -- src tests CMakeLists.txt samples` = **空**；
  唯一后代 = `2e2cfc1…`（docs-only）⇒ 当前 HEAD 行为/测试/CMake 树 ≡
  `80e4326` 行为树。
- **PATH A 判定（如实）**：重测既有 R2 post-commit candidate 时发现其
  工件**不完整**——R2 会话的 deployment gate 尝试（REMOVE_RECURSE）已把
  `candidate/ModbusLens/` 半删，仅剩被环境级文件锁持有的 `modbuslens.exe`
  （SHA-256 仍为 `5324e0fb…`，但 manifest 与其余 1712 个文件已失）。
  candidate exe 的锁在本 session 开始时**已自行释放**（约 1 小时后）。
  按 packet §7「artifact identity changed ⇒ 不再称为旧 candidate」→
  **PATH B**。

### 102.2 PATH B — 新 canonical post-commit 树 + exact candidate

- 新树 `build/acceptance/session-m12d-r2a-deployment-release/`（当前 HEAD
  源码；configure 含 `-DMODBUSLENS_PYTHON_EXECUTABLE=D:/Anaconda3/python.exe`
  与既有 canonical Release toolchain；凭据缺席；"not registered" 消息缺席）。
- **inventory 硬门禁**：`ctest -N` = **Total Tests: 70**（= 67 + 3，与 R2
  archive 精确一致），manual_qa #32 / qml_manual_qa_check #69 /
  qml_manual_qa_check_windows #70 / manual_delete #33 / candidate_review #31 /
  c1b_dependency_materializer #37 / deployment_startup_check #36 全部注册。
- **build**：`ninja modbuslens` RC 0（**116/116** 步——仅 app 链路，按
  §13 无需重跑 full 70/70；R2 post-commit 70/70 已覆盖同一行为树）。
- **candidate FROM ZERO**（canonical generator；无手工 DLL/EXE 复制、无
  ZIP）：**candidate exe ≡ source exe**（均 6,795,902 B，SHA-256
  **`6563ea1b33d22ff4511c6bb480bc48e2d1f6c3ae852c8001d1f45244c4a5f629`**；
  manifest exe entry 同值）。
- 计数（精确措辞）：manifest `files` = **1713 条**；root 实际文件
  **含 manifest = 1714**、**不含 manifest = 1713**；目录 = **90**；
  qwindows `80473907…8ac`；pdfium `d42c452a…f14b`。
- **EXACT deployment gate**：对该 exact candidate 运行
  `deployment_startup_check`（凭据缺席净环境）= **Passed 34.42s**——
  manifest/runtime closure、candidate 树内 qwindows 加载（QT_DEBUG_PLUGINS
  证据）、`--qml-smoke-test` 启动 exit 0、零 provider request。

### 102.3 FINAL HUMAN CANDIDATE（唯一推荐）

**`build\acceptance\session-m12d-r2a-deployment-release\candidate\ModbusLens\
ModbusLens.exe`**，SHA-256
`6563ea1b33d22ff4511c6bb480bc48e2d1f6c3ae852c8001d1f45244c4a5f629`
（6,795,902 B；behavior 树 = `80e4326…`）。旧 R2 候选
`5324e0fb…` 转为**历史 R2 post-commit 工件**（其 exact deployment 因环境
文件锁事件未证；工件已在锁事件中不完整）——不再推荐，不删除其历史记录。

### 102.4 措辞澄清（不重写 §101）

- **新文件计数**：R2 行为 diff 总计 = **12 files = 8 新文件
  （core/manualqa ×2 + ui/manualqa ×5 + tests ×1）+ 4 修改文件
  （CMakeLists.txt、main.cpp、Main.qml、DeviceProfilePage.qml）**——§101
  报告中「7 新」为计数笔误；行为本身无任何差异主张。
- **I/O 措辞**：准确的安全陈述 = 「Q&A 实现对 Q&A 状态/持久化执行
  **零写 I/O**；仅执行经授权的**只读 I/O**（如 `ManualStore::loadText`
  读取 canonical Manual 数据）」——不再使用歧义的「零文件 I/O」
  （loadText 本身是文件读 I/O）。
- **main.cpp 截断事件分类**：操作型编辑事故（失败补丁脚本截断），在
  canonical validation **之前**通过 HEAD 基线 + 已知补丁序列精确重建；
  **无语义残留**——fresh 70/70 与 post-commit 70/70 覆盖的均为最终提交
  源码。不淡化事故本身。

### 102.5 状态

- **M12-D first behavior slice = IMPLEMENTED / AUTOMATED PASS**；
  **M12-D EXACT HUMAN CANDIDATE DEPLOYMENT = PASS**；
  **M12-D HUMAN GATE = READY**；**M12-D Human acceptance = PENDING**；
  M12-D overall = IN PROGRESS；M12-D contract = HUMAN-FROZEN（§100）；
  M12-C = COMPLETE / HUMAN ACCEPTED / FINAL BASELINE FROZEN；
  **verified LKGC = `c68d2bbb277096fd7fb9a76d99d7b588da6461f0` UNCHANGED**；
  M12-D（second slice）/ package / tag / push = 未授权未发生；本节所在
  提交 docs-only 永不作 LKGC。**NEXT = HUMAN M12-D Q&A ACCEPTANCE**
  （候选 = §102.3；清单 = §101.3）。

## 103. M12-D HUMAN-LIVE STRUCTURED OUTPUT COMPATIBILITY RCA + REPAIR（SESSION M12-D-R2B · behavior `777f783` · HUMAN RE-TEST PENDING）

> Human 授权（逐字）：「授权：针对 M12-D Human live Q&A 中出现的'云端返回的
> 问答结果无法解析'执行窄范围 RCA。先只读定位失败发生在 HTTP/provider、JSON
> syntax、structured result schema/parser，还是后续 citation validation；不得
> 读取/打印 token、Authorization 或完整 raw provider response，不得为了通过
> 而放宽 fail-closed parser/citation validation。若确认是 provider
> structured-output compatibility 缺陷，可做最小必要修复及针对性自动化验证、
> canonical regression、behavior commit、fresh candidate/deployment 与 docs
> addendum；不得改变 D1～D5 产品语义，不推进 LKGC，不开始第二
> slice/package/tag/push。」

### 103.1 Human live defect（逐字保留）

- 选中：`ModbusLens_Test_Manual_A_Clear.txt`；提问：「这台设备的厂商是
  什么？」；UI 显示：「云端返回的问答结果无法解析。」
- 已获 Human 部分确认：Q&A 入口/consent 路径「没问题的」（PARTIAL PASS，
  不升级）；FOUND + citation = 未验收；M12-D Human acceptance = PENDING /
  DEFECT OPEN。

### 103.2 RCA（先只读，后受控 live 诊断）

- **错误串唯一来源**：`ManualQaController::completeAttempt` 中
  `parseProviderResult` 返回 nullopt ⇒ `qa_malformed_output` ⇒ 「云端返回的
  问答结果无法解析。」——**排除断言**：L1/L2（provider/HTTP 失败走 `!ok`
  分支，文案不同）；L5（citation 校验在其后，失败 token 为 `qa_citation_*`，
  文案不同）；L6（映射正确）。失败层 = **L3（final content 非合法 JSON）**
  及其下游 schema 细分。
- **请求侧审计**（AgentClient 共享 body）：`{model, messages, tools:[],
  stream:false, max_tokens:768}`——无 enable_thinking/response_format/
  temperature；**max_tokens=768 为 T011 diagnosis 契约的共享硬编码**。
- **响应提取审计**：AgentClient 发射 `choices[0].message` **整对象**；
  reasoning_content 为独立字段（T011 契约，不混入 content）。
- **安全 live 诊断 ×5**（packet §11 allowlist：仅结构元数据；合成手册
  + 非敏感问题；不打印/不落盘任何内容/凭据/raw response）：
  ①② thinking 开启（默认）：HTTP 2xx、reasoning_content = yes、
  completion_tokens = **4082/6060**、content 212/286 B、首 token =
  object-open、无 `<think>`/fence、JSON 合法、keys/形状正确（含
  citations[0] 六字段全 str/int 正确类型）。
  ③④⑤ `chat_template_kwargs:{enable_thinking:false}` 与顶层
  `enable_thinking:false`：**均被 provider 接受并生效**——reasoning 消失、
  completion_tokens 骤降 **63–65**、content 仍为合法 JSON。
- **根因（两层，均在授权兼容性范围内）**：
  1. **提取层缺陷（主因，L4'/提取）**：`handleRoundSucceeded` 对
     AgentClient **已发射的 assistant MESSAGE 对象**再取
     `choices[0].message.content` ⇒ **content 恒为空** ⇒ 每次 live Q&A 的
     parse 必然失败——与 Human 100% 失败、自动化不失败（单元不走该层）
     完全吻合。
  2. **thinking 兼容性（次因，L3 加剧项）**：thinking 消耗 4082–6060
     completion tokens，复杂真实手册问题上可挤占/截断 final JSON——
     请求层显式禁用为 §14 首选路径。

### 103.3 修复（最小面，strictness 零放松）

- **提取修复**：新增纯函数
  `ModelScopeManualQaRunner::extractAssistantContent(assistantMessage)`
  （message **顶层** content；reasoning_content 永不进入 answer——T011）；
  `handleRoundSucceeded` 改用之。
- **请求修复**：新增纯函数 `buildRequestBody(request)`（请求体构造自
  begin 抽出，供确定性测试）——Q&A 请求显式
  `chat_template_kwargs:{enable_thinking:false}`（provider 实测支持生效）；
  其余字段（model/messages/tools/stream:false/max_tokens:768）与 diagnosis
  共享契约原样；AgentRuntime 等其他消费者零变化。
- **不变式**：strict fail-closed parser、citation A–D 校验、ERROR 三态
  分离、session-only、consent 分离、零 mutation 边界——全部原样（§103.2
  诊断的 schema 形状本就正确，无 parser 放松理由）。
- **无 raw response 日志/持久化**；live 诊断脚本位于 git-ignored `build/`
  （临时、不提交）。

### 103.4 测试与验证链（凭据缺席、零 live provider 于 CTest）

- **PRE-FIX RED ×2（变异重现 pre-fix 状态，§18）**：
  RED-1（提取回退变异）⇒ **恰好 qa37** FAIL（content empty）；
  RED-2（kwargs 移除变异）⇒ **恰好 qa36** FAIL——均精确逆向、residue 0。
- **GREEN**：`manual_qa` **38/38**（含新增 qa36 请求体契约、qa37 顶层
  content 契约 + reasoning 不泄漏防御断言）；targeted（manual_qa +
  agent_runtime/agent_integration/ai_client + manual_import/delete +
  candidate_review + consent gate）**11/11 PASS**。
- **pre-commit fresh 树 `session-m12d-r2b-release/`**（python 变量生效）：
  configure RC0 / inventory **70** / build **474/474** / full unfiltered
  **70/70 PASS / exit 0 / 215.51s**（manual_qa #32 2.15s、agent_runtime
  #48 6.44s、deployment #36 47.21s、c1b #37 28.38s）。
- **行为提交 = `777f783f8532c7481eb5bd615cacb0d26b60f4a2`**
  （`M12: fix manual Q&A structured output compatibility`；3 files /
  +147 −36；parent `ff5f371…`；NO AMEND；内无 docs；提交后
  `git diff HEAD -- src tests CMakeLists.txt samples` = 空）。
- **post-commit 树 `session-m12d-r2b-postcommit-release/`**：configure RC0 /
  inventory **70** / build RC0 / full **70/70 PASS / exit 0 / 213.81s**。
- **R2B candidate**（canonical generator FROM ZERO）：candidate exe ≡
  source exe（**6,799,524 B，SHA-256
  `e41caf8e8647a7578d049a8d5dfe45f61230f321d74892a8f7b47f5502d3aa46`**）；
  manifest `files` = **1713 条** / root 实际文件含 manifest = **1714** /
  目录 = **90**；qwindows `80473907…8ac`；pdfium `d42c452a…f14b`。
- **EXACT deployment gate**：对该 exact candidate 运行 = **Passed
  27.99s**（净环境、候选树内 qwindows、启动 exit 0、零 provider request）。

### 103.5 状态与 Human re-test

- **M12-D Human-live structured-output defect = REPAIRED / AUTOMATED
  PASS**；**M12-D Human re-test = REQUIRED**（用 §103.4 R2B 候选复测
  FOUND + citation）；**M12-D Human acceptance = IN PROGRESS**（§102 部分
  PASS 的入口/consent 项不升级）；**M12-D overall = IN PROGRESS**；M12-D
  contract = HUMAN-FROZEN（§100）；M12-C = COMPLETE / HUMAN ACCEPTED /
  FROZEN；**verified LKGC = `c68d2bb…` UNCHANGED**；M12-D second slice /
  package / tag / push = 未授权未发生；本节所在提交 docs-only 永不作 LKGC。
- **R2A 候选 `6563ea1b…` 转历史缺陷重现工件**；**唯一推荐 Human 候选 =
  R2B `e41caf8e…`**。

## 104. M12-D INTERMITTENT STRUCTURED-OUTPUT DEFECT — SAFE OBSERVABILITY + CAPABILITY AUDIT（SESSION M12-D-R2D · behavior `a0146b7` · ROOT CAUSE OPEN · HUMAN GATE HOLD）

> Human 授权（逐字）：「授权：执行 M12-D-R2D intermittent structured-output
> failure observability + RCA。新的 Human evidence 再次复现'云端返回的问答
> 结果无法解析'，因此 R2C 的 transient-only 推定被 supersede。首先不得修改
> strict parser/citation validator 的接受范围；先把 parseProviderResult 的
> 失败原因拆成安全、确定性的内部分类，并增加测试……随后用最多 2 次受控
> live probe 复现并只记录……若确认 provider 存在稳定或间歇性的结构化输出
> 兼容问题，再判断实际 ModelScope endpoint/model 是否支持 provider-side
> structured-output / JSON response mode……禁止任意 prose 中抽取 JSON，禁止
> 放宽 citation validation，禁止改变 D1～D5。」

### 104.1 Human 重复证据（权威）与 R2C 推定取代

Human 在 **R2B 候选上独立复现**「云端返回的问答结果无法解析。」（第二次，
与 R2 修复后的首次相互独立）⇒ **间歇性生产缺陷成立**；R2C 的
transient-only 推定**仅对其那一次 runner !ok 事件有效**（该事件历史保留，
§103），**不构成**对结构化输出缺陷的关闭。FOUND + citation 仍未验收；
**M12-D Human gate = HOLD**（本节末状态）。

### 104.2 Phase A — 安全观测性（接受集零变化）

- `parseProviderResult(json, failureCategory*)` 增加安全失败类别（§7 最小
  集，匹配实际 parser 契约）：`qa_parse_empty_content` /
  `qa_parse_markdown_fence` / `qa_parse_think_envelope` /
  `qa_parse_json_syntax` / `qa_parse_top_level_not_object` /
  `qa_parse_missing_status` / `qa_parse_status_wrong_type` /
  `qa_parse_status_unknown` / `qa_parse_missing_answer` /
  `qa_parse_answer_wrong_type` / `qa_parse_missing_citations` /
  `qa_parse_citations_wrong_type` / `qa_parse_citation_not_object` /
  `qa_parse_citation_missing_required_field` /
  `qa_parse_citation_wrong_field_type`。类别 = **纯 token**（零
  question/answer/excerpt/reasoning/raw content）。citation freshness
  校验仍在 parse 之后的独立层（不混入 parse 类别）。
- **接受集等价证明**：全部 pre-R2D parser 测试（合法 + malformed 变体）
  **不变通过**（manual_qa 38/38 → 39/39），无输入新通过、无合法输入新失败。
- `ManualQaController`：`lastErrorToken` 携带类别 token；failureText 追加
  「（类别: qa_parse_*）」——**content-free**，Human 复测截图即可报告精确
  失败类；通用文案主干不变（§22）。
- **观测性测试 qa38**：15+ 断言覆盖 envelope/schema/prose 全类别矩阵 +
  成功时类别为空 + prose 环绕仍拒绝；qa18 更新为新 token 契约。

### 104.3 live probe ×2（元数据 only）+ capability 审计

- **probe 1（生产等价请求，R2B 请求体含 enable_thinking=false）**：HTTP
  200 / reasoning absent / content 242 B / finish=stop / **safe category =
  ok** / citations 1 / citation validator reached = yes——**未重现**。
- **probe 2（response_format={"type":"json_object"} 能力探测）**：HTTP
  **200**（provider 接受该参数）**但不可靠**——content 首非空白 token 非
  object-open，safe category = **qa_parse_top_level_not_object**、completion
  353——**response_format 不被采用为修复**（引入新失败类，违反 §16/§17 的
  可靠性要求）。
- **fence / `<think>` / 截断**：本 session 探针均未重现（categories 无样本）
  ⇒ 按 §18/§20/§21 **不加** fence 归一化/`<think>` 容忍/max_tokens 调整
  （无 finish_reason=length 证据）。
- **§13 纪律**：两次探针成功 ≠ 无缺陷（Human 两次独立复现已成立）；间歇性
  缺陷保持 OPEN，等待带类别的下一次复现。

### 104.4 状态（§41 路径）

- **M12-D STRUCTURED OUTPUT DEFECT = OPEN**（间歇性；根因未闭合到单一
  可修点）；**OBSERVABILITY = IMPROVED**（安全类别体系 + qa38 + 类别直达
  failureText/lastErrorToken）；**M12-D HUMAN GATE = HOLD**——但**下一次
  Human 复现将自动携带精确类别**（failureText 内嵌），Agent 下一会话可据
  类别直接走 §18 的确定性修复（fence ⇒ 归一化；think ⇒ 请求层强化；
  json_syntax+length ⇒ max_tokens；schema ⇒ prompt/schema 对齐）。
- 本 session 工程产出已走完整链：**行为提交 =
  `a0146b777c682a785d27bdad324968cf6555e3de`**（observability hardening；
  4 files / +194 −21；parent `6618673…`；NO AMEND；内无 docs）；
  pre-commit fresh 树 `session-m12d-r2d-release/`：configure RC0（python
  变量生效）/ inventory **70** / build **474/474** / full **70/70 PASS /
  exit 0 / 193.31s**；post-commit 树 `session-m12d-r2d-postcommit-release/`：
  inventory 70 / full **70/70 / 199.22s**；**R2D 候选（唯一推荐 Human 工件，
  含观测性）**：candidate exe ≡ source exe（**6,802,378 B，SHA-256
  `cc839d36144724947da4f7578f7f91fac38c286088b3a6f96000d227c8a75ca6`**）；
  manifest 1713 条 / root 含 manifest 1714 / 目录 90；**EXACT deployment
  gate Passed 23.50s**。
- M12-D first slice = IMPLEMENTED / AUTOMATED PASS；M12-D Human acceptance
  = IN PROGRESS；M12-D overall = IN PROGRESS；M12-C = COMPLETE / HUMAN
  ACCEPTED / FROZEN；**verified LKGC = `c68d2bb…` UNCHANGED**；M12-D second
  slice / package / tag / push = 未授权未发生；本节所在提交 docs-only 永不
  作 LKGC。

## 105. M12-D CITATION VALIDATION HUMAN-LIVE RCA + APP-RESOLVED CITATIONS（SESSION M12-D-R2E · behavior `8416fe7` · HUMAN RE-TEST PENDING）

> Human 授权（逐字）：「授权：执行 M12-D-R2E citation validation Human-live
> RCA。新的 Human evidence 已出现'回答的说明书依据未通过本地校验'，证明本次
> 请求已成功通过 provider/runner 与 structured parser，失败发生在本地
> deterministic citation validation。……若确认是当前 provider citation
> contract 设计导致模型需要自行生成不可可靠复现的 canonical identity/range，
> 则先做 source/contract feasibility audit，判断能否在不改变 D1～D5 的前提下
> 由应用提供稳定 chunk/citation identity、由 provider 仅选择引用、再由本地
> 映射回 D2 canonical citation shape……不得放宽 document/hash/range/excerpt
> round-trip 校验，不推进 LKGC，不开始第二切片/package/tag/push。任何大文件
> 损坏立即 STOP，不得同 session 重建。」

### 105.1 Human live defect（逐字保留）

选中 `ModbusLens_Test_Manual_A_Clear.txt`、问「这台设备的厂商是什么？」→
UI「回答的说明书依据未通过本地校验。」——与 R2C 的「问答未成功」（runner
!ok）和 R2 的「云端返回的问答结果无法解析」（parse 失败）**类别不同**：
provider/runner/structured parser **全部成功**，FOUND 进入本地 citation
validation 且**失败**。

### 105.2 Phase A — 只读 RCA（错误源/validator 契约/类别保留/权威表）

- **错误串唯一来源**：`ManualQaController::completeAttempt` 的
  `validateManualQaFoundResult` 失败分支 → `citationCodeToErrorToken`
  （ManualQaController.cpp:29）→ 「回答的说明书依据未通过本地校验。」。
- **类别保留审计（§8）**：validator 的四个类别
  （DocumentMismatch/ContentHashMismatch/InvalidRange/RoundTripFailed）
  **已经**通过 `citationCodeToErrorToken` 完整保留进
  `lastErrorToken`/`failureText`——无 collapse，**不加重复观测性**。
- **校验顺序（§9）**：empty answer → zero citations → 逐 citation
  [documentId → contentHash → range → round-trip]，首败即返（先败屏蔽后败
  ——探针解读时已考虑）。
- **citation contract 权威表（§10/§11，源码实测）**：prompt 将
  documentId/contentHash 与每块 [start,end) 偏移**明文交给模型**，并要求
  模型在输出中原样复制 identity/hash、**自行生成数值偏移**、**逐字复制
  excerpt** ⇒ 权威分类：documentId = MODEL-COPIED；contentHash =
  MODEL-COPIED；pageNumber = 未提供（模型输出 -1）；textStart/textEnd =
  **MODEL-GENERATED**；excerpt = MODEL-COPIED。**模型必须从 prompt 文本中
  精确再现 canonical identity/range = YES**——这就是确定性弱点。

### 105.3 live probe ×2（§18/§19：结构元数据 + 安全类别 only）

对合成 Manual A + 合成问题、生产等价请求（enable_thinking=false）执行 D2
校验镜像：**两次均为 HTTP 200 / parse ok / citations 1 /
D2 validation category = round_trip_failed**，且 citation[0] offsets
均在提供的块边界内——模型**试图**按块边界引用但 excerpt/偏移组合无法逐字
对齐 canonical 切片。**R2E-C4 RoundTripFailed 确定性重现**（×2），子类 =
§21-C（模型数值偏移生成不可靠）+ §21-D（excerpt 非精确切片副本）。

### 105.4 修复（§22 Option 1 — APP-RESOLVED CITATIONS，§14 A–G 全部证实）

- **A**：失败由模型生成/复制 canonical identity/range 导致（105.2/105.3）；
- **B**：app 在 dispatch 前已拥有确定性块表（buildManualQaContextBlocks 的
  [start,end,text]）；citationId = "c1".."cN"（会话内、非密、opaque、按
  generation 失效）；
- **C**：映射产出的最终 citation 仍为精确 D2 六字段；
- **D**：`validateManualQaCitation`/`validateManualQaFoundResult` **零改动**
  ——映射后的 canonical citation 必然通过 round-trip（excerpt 即真实切片）；
- **E**：provider 权威**缩小**（只能选择，不能创作 canonical 值）；
- **F**：D1–D5 用户可见语义零变化；
- **G**：映射与篡改拒绝可确定性测试。

**实现**：`buildRequestBody` prompt 改为 **citationId 选择契约**（每块前
标注 `citationId: cN`；输出只能 `{"citationId":"cN"}`，禁止自行编造
documentId/contentHash/偏移/原文）；`parseProviderResult` 接受
`{"citationId":…}` 形状并把 opaque id 暂存；新增纯函数
`resolveProviderResult(json, request, failureCategory*)`：parse + 将
selected id 映射回 canonical citation（documentId/contentHash 取请求绑定、
offsets/excerpt 取所引块）——**未知 id fail-closed**（qa_parse_citation_
unknown_id）；`ManualQaController::completeAttempt` 改用 resolve pass
（activeRequest_ 保存当前请求块表）。**validator/A–D 校验零改动**；provider
无法再供给 canonical 值 ⇒ 篡改面消失。

### 105.5 测试与验证链（凭据缺席、零 live provider 于 CTest）

- **PRE-FIX RED（live）**：R2E 探针 ×2 round_trip_failed（Human live 类）
  + Human 本体两次失败 = 历史 REAL RED。
- **qa40（新契约 GREEN）**：citationId 选择 → canonical citation 映射
  （documentId/contentHash = 绑定值、offsets = 块界、excerpt = 真实切片）
  → FOUND；**unknown id "c99" ⇒ ERROR `qa_parse_citation_unknown_id`**
  （strictness counter-test，fail-closed）。
- **qa41（失败类确定性重现）**：provider-authored excerpt/offsets 不匹配
  ⇒ ERROR `qa_citation_round_trip_failed`——锁定旧失败类在任意 legacy 输出
  下仍 fail-closed。
- **NX-QA-CITID**：临时允许未知 citationId 回落 block 0 ⇒ RED **恰好
  qa40** → 精确逆向 → 41/41 复绿（residue 0）。
- **targeted**：manual_qa 41/41 + agent_runtime/ai_client + manual_import/
  delete + candidate_review + consent gate = **10/10 PASS**。
- **pre-commit fresh 树 `session-m12d-r2e-release/`**（python 变量生效）：
  configure RC0 / inventory **70** / build **474/474** / full unfiltered
  **70/70 PASS / exit 0**（manual_qa #32 2.74s、deployment #36 33.65s、
  c1b #37 26.67s、agent_runtime #48 6.86s、qml_manual_qa #69/#70）。
- **行为提交 = `8416fe7a26eaa8c79ab8186513b60af5954a18b9`**
  （`M12: make manual Q&A citations app-resolved`；5 files / +221 −11；
  parent `918c028…`；NO AMEND；内无 docs；提交后
  `git diff HEAD -- src tests CMakeLists.txt samples` = 空）。
- **post-commit 树 `session-m12d-r2e-postcommit-release/`**：configure RC0 /
  inventory **70** / build RC0 / full **70/70 PASS / exit 0 / 205.06s**。
- **R2E candidate**（canonical generator FROM ZERO）：candidate exe ≡
  source exe（**6,814,977 B，SHA-256
  `658f0ff6102eebb170d15d0e89fcddb9312dbfdf7593a129418835dc2e96c4b9`**）；
  manifest `files` = **1713 条** / root 实际文件含 manifest = **1714** /
  目录 = **90**；qwindows `80473907…8ac`；pdfium `d42c452a…f14b`。
- **EXACT deployment gate**：对该 exact candidate = **Passed 27.55s**
  （净环境、候选树内 qwindows、启动 exit 0、零 provider request）。

### 105.6 状态与 Human re-test

- **M12-D citation validation Human-live defect = REPAIRED / AUTOMATED
  PASS**；**D2 canonical citation validation = UNCHANGED / AUTHORITATIVE**
  （六字段形状与 A–D 校验逐字未动；无 uniqueness）；**M12-D HUMAN RE-TEST =
  REQUIRED**（复测 FOUND + citation，唯一入口 = R2E 候选）；M12-D Human
  acceptance = IN PROGRESS；M12-D overall = IN PROGRESS；M12-D contract =
  HUMAN-FROZEN（§100）；M12-C = COMPLETE / HUMAN ACCEPTED / FROZEN；
  **verified LKGC = `c68d2bb…` UNCHANGED**；M12-D second slice / package /
  tag / push = 未授权未发生；本节所在提交 docs-only 永不作 LKGC。

## 106. M12-D R2E HUMAN LIVE Q&A ACCEPTANCE ARCHIVE（SESSION M12-D-R2E-A · docs-only · Human 授权归档）

> Human 授权（逐字）：「授权：归档 M12-D R2E Human live Q&A acceptance
> PASS；仅 docs-only。Human PASS 仅覆盖本次实际执行的 single-selected-Manual
> / single-question FOUND + citation 正向工作流：选中
> ModbusLens_Test_Manual_A_Clear.txt 提问'这台设备的厂商是什么？'成功得到
> 厂商答案并显示说明书引用，本地 citation validation 通过，不再出现
> provider / structured parse / citation validation 错误。若 Human 已确认
> 问答后无 Candidate、无 DeviceProfile mutation、无'未保存修改'，则一并按
> 实际观察归档；未实际 Human 执行的 NOT_FOUND / INSUFFICIENT_EVIDENCE /
> ERROR、Manual switch/delete invalidation、late-response drop、session-only
> restart 等路径保持 IMPLEMENTED / AUTOMATED PASS，不虚构 Human live
> verification。将 M12-D first behavior slice 标记为 COMPLETE / HUMAN
> ACCEPTED；M12-D overall 仅标记 READY FOR FINAL CLOSURE AUDIT，不直接推断
> COMPLETE。verified LKGC 保持 c68d2bbb277096fd7fb9a76d99d7b588da6461f0 不
> 变；不开始第二切片，不做 package/tag/push。」

### 106.0 Human 授权原文

见本节引文（逐字，未删改）。

### 106.1 验收对象 / candidate identity

- **behavior commit = `8416fe7a26eaa8c79ab8186513b60af5954a18b9`**（type =
  commit、HEAD 祖先 exit 0；`8416fe7..HEAD -- src tests CMakeLists.txt
  samples` = 空 ⇒ 其后仅 docs 提交 `487061f…`，本 docs commit 非被测行为、
  非 LKGC）。
- **Human-tested candidate** =
  `build\acceptance\session-m12d-r2e-postcommit-release\candidate\ModbusLens\
  ModbusLens.exe`，本 session 只读重测 SHA-256 =
  **`658f0ff6102eebb170d15d0e89fcddb9312dbfdf7593a129418835dc2e96c4b9`**
  （在档一致，未重建）。

### 106.2 Human 实际执行（逐字记录，不增不减）

- Manual = `ModbusLens_Test_Manual_A_Clear.txt`；question = 「这台设备的
  厂商是什么？」。
- Human 观察：① Q&A 成功返回设备厂商答案；② 显示了说明书引用（来源证据
  块）；③ 引用通过本地 deterministic validation；④ 本次成功运行中**未**
  出现「问答未成功，请稍后重试。」「云端返回的问答结果无法解析。」「回答
  的说明书依据未通过本地校验。」三种先前失败；⑤ Human 额外确认：「问答后
  没有 Candidate，设备档案没有变化，也没有出现未保存修改。」

### 106.3 Human PASS scope（严格限定）

单 selected Manual / 单 question / FOUND / 非空 answer / 可见 citation /
本地 citation validation 通过 / 零 Candidate 产生 / 零 DeviceProfile
mutation / 零 dirty「未保存修改」——**仅此实际工作流** ⇒
**M12-D FIRST-SLICE POSITIVE WORKFLOW = HUMAN PASS**。

### 106.4 automated-only boundary（不升级）

以下保持 **IMPLEMENTED / AUTOMATED PASS / HUMAN LIVE NOT RUN**（§7 清单，
不虚构 Human 证据）：NOT_FOUND；INSUFFICIENT_EVIDENCE；ERROR 路径；Manual
switch invalidation；Manual delete invalidation；late-response drop；
session-only restart；unknown citationId fail-closed；generation-scoped
citationId。

### 106.5 R2E 架构 current truth

**APP-RESOLVED CITATION SELECTION = IMPLEMENTED / AUTOMATED PASS / HUMAN
POSITIVE-FLOW PASS**：provider 仅选择 app 签发的 opaque citationId；app 将
选中 id 映射回 canonical D2 citation（documentId/contentHash = 请求绑定、
offsets/excerpt = 所引真实块）；**D2 CANONICAL VALIDATOR = UNCHANGED /
AUTHORITATIVE**（六字段形状 + A–D 校验 + 无 uniqueness，逐字未动）；
**Provider authority = INFORMATIONAL SELECTION ONLY**。citationId 本身
**不是** canonical 权威。

### 106.6 历史 superseding statement

R2（parse 失败）/ R2B 修复 / R2C runner !ok（transient 推定，仅对那次
事件有效）/ R2D 观测性 + §29 编辑事故 / R2D-A recovery audit PASS / R2E
repair——全部历史证据**保留原样**（含 R2D §29 PROCESS VIOLATION =
PERMANENT HISTORICAL INCIDENT，R2D-A recovery audit = PASS，行为树
RECOVERED AS VALID）。**当前真值**：R2E 正向 FOUND + citation 工作流 =
HUMAN PASS；历史失败是已被修复/加装观测的缺陷的有效历史证据。

### 106.7 first-slice 最终分类

**M12-D first behavior slice = COMPLETE / HUMAN ACCEPTED**
（IMPLEMENTED / AUTOMATED PASS / HUMAN PASS；正向 Human acceptance 严格
限定于 §106.3 范围；automated-only 负向/状态路径按 §106.4 保持原状——
Human 明确限定了验收范围，故不要求 Human 重放全部自动化路径）。

### 106.8 overall boundary

**M12-D overall = READY FOR FINAL CLOSURE AUDIT**（**不**标记 COMPLETE）。
独立只读 final closure audit 须确认：D1–D5 全满足；无必需 first-slice
能力 pending；automated-only 路径在冻结合同下是否充分；second-slice 能力
是否为 M12-D exit 所必需；无未决 P0；canonical docs 无矛盾。该审计不在本
session 执行。

### 106.9 动作边界

verified LKGC = `c68d2bbb277096fd7fb9a76d99d7b588da6461f0` UNCHANGED
（`8416fe7…` 与任何 docs commit 均**不是** LKGC；不准备 LKGC 推进提交）；
second slice = NOT STARTED / NOT AUTHORIZED；canonical package = NOT
CREATED；tag = 仅 v1.0.0；push = 无；本节所在提交 docs-only 永不作 LKGC。

## 107. M12-D DOCS-ONLY FINAL CLOSURE（SESSION M12-D-R2G · Human 授权 · overall = COMPLETE / HUMAN ACCEPTED）

> Human 授权（逐字）：「授权：执行 M12-D docs-only final closure。依据
> M12-D-R2F Final Closure Audit = GO，将 M12-D overall 正式归档为 COMPLETE /
> HUMAN ACCEPTED，并保持 M12-D first behavior slice = COMPLETE / HUMAN
> ACCEPTED。准确保留 Human acceptance 分层：本次 Human PASS 仅覆盖
> single-selected-Manual / single-question FOUND + citation 正向工作流及实际
> 观察到的零 Candidate、零 DeviceProfile mutation、零 dirty；NOT_FOUND /
> INSUFFICIENT_EVIDENCE / ERROR、Manual switch/delete invalidation、
> late-response drop、session-only restart 继续保持 IMPLEMENTED / AUTOMATED
> PASS / HUMAN LIVE NOT RUN，不升级为 Human PASS。归档 D1～D5 = SATISFIED、
> first behavior slice 覆盖全部 canonical v1 required capabilities、
> CANONICAL REQUIRED SECOND SLICE = NONE，并仅按 T027 §100 的真实 canonical
> 边界记录 deferred/future 项，不把其他 milestone 的历史 deferred 能力混入
> M12-D。保持全部历史 defect / incident / superseding evidence 原文，不重写
> 历史。记录 latest Human-tested M12-D behavior-bearing commit =
> 8416fe7a26eaa8c79ab8186513b60af5954a18b9。本授权仅 docs/governance
> closure；verified LKGC 保持 c68d2bbb277096fd7fb9a76d99d7b588da6461f0 不
> 变，不推进；不开始任何新 behavior slice，不做 package/tag/push。」

### 107.1 起始基线 / provenance

- 起始 HEAD = `138af14d20f5457aa83d10ec80229f5e47f9b812`（RESYNC 实测：
  tracked clean / cached 空 / diff-check PASS / untracked 仅 `_ctx.py`、
  `_dump.py` / tag 仅 v1.0.0 / `git ls-files build` 空）。
- **行为谱系（实测）**：`8416fe7…` type = commit、HEAD 祖先（exit 0）；
  `git diff 8416fe7..HEAD -- src tests CMakeLists.txt samples` = **空** ⇒
  当前 HEAD 行为树 = Human-tested `8416fe7` 行为树。**latest Human-tested
  M12-D behavior-bearing commit = `8416fe7a26eaa8c79ab8186513b60af5954a18b9`**
  （docs commit 非 behavior-bearing、非 LKGC）。

### 107.2 R2F GO authority（closure 依据）

M12-D-R2F Final Closure Audit（strict read-only）= **GO**：D1–D5 全
SATISFIED；first behavior slice 覆盖全部 canonical M12-D v1 required
capabilities；CANONICAL REQUIRED SECOND SLICE = NONE；automated-only
负向/状态路径按冻结合同判定 sufficient（确定性、fail-closed、非
mutation/session-only、D1–D5 完整规约、稳定自动化覆盖，canonical 未要求
Human live）；无未决必需 P0；无当前状态矛盾；M12-C isolation 无漂移。
本 closure 仅归档该 GO 结论，不新增工程证据。

### 107.3 D1–D5 final closure matrix

| 项 | Closure | 要点 |
| --- | --- | --- |
| D1 单 selected Manual / 单问题 | SATISFIED | hasSelectedManual 前置拒绝；单 generation；switch 失效+清空；无多手册/全库/RAG/batch |
| D2 canonical citation 契约 | SATISFIED | citationId（app-resolved）选择 → 映射回六字段 canonical citation（identity/hash 请求绑定、offsets/excerpt 真实块）；A–D 校验（identity/hash/range/精确 round-trip）逐字未动、无 global uniqueness；FOUND ≥1 有效 citation |
| D3 四态 + INFORMATIONAL ONLY | SATISFIED | FOUND/NOT_FOUND/INSUFFICIENT_EVIDENCE 与 ERROR 分离不互转；FOUND = 非空 answer + ≥1 有效 citation；answer 无 Accept/Edit/Save、零 Candidate/Profile/Manual/Modbus 权威 |
| D4 独立 consent | SATISFIED | Q&A consent 独立成员/独立 dialog（披露提问+摘录）；与 extraction consent 无共享；ModelScope 基础设施复用、无第二凭据 store/provider UI |
| D5 session-only + delete 交互 | SATISFIED | 零持久化/零 schema；switch/delete 失效 generation + best-effort cancel + 清空 + late drop；Running 不阻 ML-2 delete |

### 107.4 Human acceptance evidence boundary（分层精确）

HUMAN PASS **仅限**实际执行的正向工作流：单 selected Manual
（`ModbusLens_Test_Manual_A_Clear.txt`）/ 单问题（「这台设备的厂商是
什么？」）/ FOUND / 非空 answer / 可见 citation / 本地 citation validation
通过 / 零 Candidate / 零 DeviceProfile mutation / 零「未保存修改」。
**NOT_FOUND / INSUFFICIENT_EVIDENCE / ERROR / switch·delete invalidation /
late-response drop / session-only restart = IMPLEMENTED / AUTOMATED PASS /
HUMAN LIVE NOT RUN**——不升级、不虚构。

### 107.5 automated-only path evidence boundary

上述 automated-only 路径的 closure sufficiency 依据（R2F 判定）：确定性、
fail-closed、非 mutation/session-only、被 D1–D5 完整规约、稳定自动化覆盖
（qa16/qa17/qa18/qa19/qa24/qa25/qa26/qa27/qa28/qa29/qa31/qa32/qa33/qa40/
qa41 + QML 门禁 S 阶段 + NX-QA-LATE/NX-QA-CITID/NX-QA-CITID-unknown 历史
负向对照）；canonical 未要求 Human live proof ⇒ closure 充分。

### 107.6 v1 exit mapping（§17.2 canonical）

问答仅基于已导入证据 ✓ · FOUND/NOT_FOUND/INSUFFICIENT_EVIDENCE 可区分 ✓ ·
无证据不回答 ✓ · ERROR 独立 ✓ · 单 selected Manual 范围 ✓ · citation
deterministic/local-authoritative ✓ · INFORMATIONAL ONLY ✓ · 零
Profile/Candidate/Manual/Modbus mutation 权威 ✓ · session-only ✓。

### 107.7 first-slice coverage / no canonical second slice

First behavior slice **覆盖全部 canonical M12-D v1 required capabilities**
（D1–D5 矩阵 + §17.2 exit）；**CANONICAL REQUIRED SECOND SLICE = NONE**——
canonical 中无任何段落为 M12-D 冻结必需的第二切片能力；历史 "second slice"
字样 = 历史/其他 milestone 标签，不转化为新需求。

### 107.8 M12-D-only deferred/future boundary（§100 原分类，不混入他 milestone 项）

multi-manual Q&A · whole-library Q&A · whole-library RAG · conversation
memory · durable Q&A history · cross-session Q&A history/state · 新
provider families · provider-selection UI · OCR expansion · automatic
device inference · M12-D 对 Profile/Candidate 的 mutation · M12-C
reopening——均为 **FUTURE / OUT-OF-SCOPE**（§100 原分类），非缺陷。

### 107.9 historical incident/defect preservation

R2 初版实现 · R2B structured-output 修复 · R2C 通用 runner 失败事件 ·
R2D parse 观测性 · **R2D §29 process violation = PERMANENT HISTORICAL
INCIDENT** · R2D-A recovery audit = PASS / 行为树 RECOVERED AS VALID ·
R2E app-resolved citation 修复 · R2E Human positive acceptance——全部
原文保留；R2B/R2E 缺陷已修复、R2C 为 transient/未分类历史事件 ⇒ 当前行为
树中**无 OPEN 且必需的缺陷**。

### 107.10 行为谱系

80e4326（first slice）→ 777f783（structured-output 修复）→ a0146b7（观测
性硬化）→ 8416fe7（app-resolved citations）→ …docs-only 提交至 HEAD。
**latest Human-tested M12-D behavior-bearing commit = `8416fe7…`**（实测
HEAD 祖先；其后全部 docs-only）。

### 107.11 M12-D final state

- **M12-D overall = COMPLETE / HUMAN ACCEPTED**；
- **M12-D first behavior slice = COMPLETE / HUMAN ACCEPTED**；
- positive FOUND + citation workflow = HUMAN PASS；NOT_FOUND /
  INSUFFICIENT_EVIDENCE / ERROR / switch·delete invalidation /
  late-response drop / session-only restart = IMPLEMENTED / AUTOMATED PASS
  / HUMAN LIVE NOT RUN（不升级）；
- D1–D5 = SATISFIED；CANONICAL REQUIRED SECOND SLICE = NONE；
- Human-tested candidate = R2E 候选 `658f0ff6…c4b9`（6,814,977 B，行为树
  `8416fe7…`）。

### 107.12 动作边界 / LKGC

**verified LKGC = `c68d2bbb277096fd7fb9a76d99d7b588da6461f0` UNCHANGED（不
推进）**；8416fe7 可记录为 future LKGC target（R2F §36 advisory：READY FOR
SEPARATE HUMAN AUTHORIZATION），推进与否 = 独立 Human 决定；M12-C =
COMPLETE / HUMAN ACCEPTED / FINAL BASELINE FROZEN；canonical package = NOT
CREATED；tag = 仅 v1.0.0；push = 无；本节所在提交 docs-only 永不作 LKGC。

## 108. M12 OVERALL DOCS-ONLY FINAL CLOSURE（SESSION M12-R3 · Human 授权 · M12 overall = COMPLETE / HUMAN ACCEPTED）

> Human 授权（逐字）：「授权：执行 M12 docs-only final closure。依据 M12
> Overall Final Closure Audit = GO，将 M12 overall 正式归档为 COMPLETE /
> HUMAN ACCEPTED。保持各子 milestone 的 canonical 状态与证据分层不变：
> M12-A = FOUNDATION ACCEPTED；M12-B = COMPLETE；M12-C = COMPLETE / HUMAN
> ACCEPTED / FINAL BASELINE FROZEN；M12-D = COMPLETE / HUMAN ACCEPTED。不得
> 因为 M12 overall closure 将任何 automated-only 路径升级为 Human PASS，也
> 不得扩大既有 Human acceptance scope。归档：ALL CANONICAL M12 EXIT
> CONDITIONS = SATISFIED；UNRESOLVED REQUIRED P0 / HUMAN DECISION = NONE；
> CURRENT REQUIRED DEFECT BLOCKER = NONE。并记录：latest Human-tested
> behavior-bearing commit = 8416fe7a26eaa8c79ab8186513b60af5954a18b9。准确
> 保留 REQUIRED / DEFERRED / FUTURE 边界及全部历史 defect / incident /
> superseding evidence，不重写历史；canonical package、real Modbus hardware
> validation、LKGC advancement 应保持为独立 release / governance decision，
> 不得误写成 M12 功能 closure blocker。仅修改 canonical docs / governance
> records，不得修改代码/tests/CMake/QML/samples；不得 build/test/生成
> candidate。verified LKGC 保持 c68d2bbb277096fd7fb9a76d99d7b588da6461f0
> 不变；不 package/tag/push；不开始任何新 milestone 或 behavior slice。
> 完成后提交独立 docs-only commit，并报告：最终 HEAD、docs diff、
> behavior-tree no-drift proof、repository integrity。」

### 108.1 起始基线 / closure authority

- 起始 HEAD = `2e9d7890c9d9f9c66ff449cc26869cf287aa3301`（RESYNC 实测：
  tracked clean / cached 空 / diff-check PASS / untracked 仅 `_ctx.py`、
  `_dump.py` / tag 仅 v1.0.0 / `git ls-files build` 空）。
- **closure authority**：M12 Overall Final Closure Audit（strict
  read-only，session M12-RFINAL-AUDIT）= **GO**——M12-A/B/C/D canonical
  exit 全部满足；无独立 M12 overall exit（M12 overall exit = 四个子里程碑
  canonical exit 的 conjunction）；无未决必需 P0；无当前矛盾；无必需 open
  defect；跨 milestone isolation 完好。本节仅归档该 GO，不新增工程证据。

### 108.2 M12-A/B/C/D canonical state matrix

| Milestone | Canonical state | 依据 |
| --- | --- | --- |
| M12-A | **FOUNDATION ACCEPTED**（含 readFunctionCode amendment；A01–A20 matrix；零 UI 不制造 Human PASS） | §17.1 / §28 / §30 / §45.10 |
| M12-B | **COMPLETE**（Slice 1–4 全部 HUMAN ACCEPTED，§45.1 逐字 7 项；最后 behavior tree `13799d6…`） | §45 / §17.2 |
| M12-C | **COMPLETE / HUMAN ACCEPTED / FINAL BASELINE FROZEN**（C1a/C1b/C2/C3 各自 accepted；唯一权威链 + MANUAL SAVE；AI 无法直写 verified Profile） | §79.5 / §86 / §93 / §98 / §99 / §17.2 |
| M12-D | **COMPLETE / HUMAN ACCEPTED**（D1–D5 SATISFIED；无 canonical 必需 second slice） | §100–§107 / §17.2 |

**M12 overall = COMPLETE / HUMAN ACCEPTED**（四者 conjunction + §18 per-stage
closure policy 全部满足）。

### 108.3 M12 overall exit mapping

M12-A exit = satisfied；M12-B exit = satisfied；M12-C exit = satisfied；
M12-D exit = satisfied ⇒ **ALL CANONICAL M12 EXIT CONDITIONS = SATISFIED**。
无独立 hidden functional exit、无新增 cross-cutting 需求、无由历史名称
（"second slice" 等）创造的新能力。

### 108.4 Human / automated evidence layering（精确保留）

- M12-A：FOUNDATION ACCEPTED 按其既有证据层（零 UI 不主张 Human）。
- M12-B：Human acceptance = §45.1 逐字 7 项范围（未扩写）。
- M12-C：component/slice 级 Human acceptance 边界（含 HUMAN LIVE PASS /
  AUTOMATED PASS 分层）原样。
- M12-D：**HUMAN PASS 仅覆盖实际执行的正向工作流**（单 selected Manual /
  单问题 / FOUND + citation / 本地校验通过 / 零 Candidate / 零
  DeviceProfile mutation / 零 dirty）；**NOT_FOUND /
  INSUFFICIENT_EVIDENCE / ERROR / Manual switch·delete invalidation /
  late-response drop / session-only restart = IMPLEMENTED / AUTOMATED PASS
  / HUMAN LIVE NOT RUN——不因 overall closure 升级**。

### 108.5 REQUIRED / DEFERRED / FUTURE boundary（不移动分类）

- **REQUIRED**：M12-A/B/C/D canonical exit 全部能力——已全部 satisfied。
- **DEFERRED（canonical 明示）**：M12-A DEFER 项（Float64/string/bit-field/
  自动设备推断/厂商云/自动单位换算/auto 40001 映射）；M12-C durable
  provenance/schema（C3-H7）；M12-D durable provenance（§100.7）。
- **FUTURE / OUT-OF-SCOPE（canonical 明示）**：OCR expansion、multi-manual
  Q&A、whole-library Q&A/RAG、conversation memory、durable Q&A history、
  cross-session state、新 provider families、provider-selection UI、M12-D
  mutation、M12-C reopening。
- deferred/future 项**不是**缺陷、**不是** blocker。

### 108.6 historical defect / incident preservation

M12-A/B 历史缺陷与修复；M12-C：cold-start hydration、C3 first slice、
Standing Discard、C3 Edit、Consent lifecycle repair、canonical 66→67
ratification correction、其他归档证据边界；M12-D：R2 初版行为、R2A
provenance closure、R2B structured-output 修复、R2C runner/provider
transient 事件、R2D observability、**R2D §29 process violation =
PERMANENT HISTORICAL INCIDENT**、R2D-A recovery audit = PASS、R2E
app-resolved citation 修复、R2E Human acceptance、R2F final closure audit
GO、R2G M12-D final closure——**全部原文保留，不重写、不软化**。

### 108.7 latest Human-tested behavior lineage

M12-A anchors：`1c42aaf`（first slice）→ `5d4d9c2`（lookup foundation）；
M12-B anchors：`becadc5`（Register Map Editor）→ … → `13799d6`（M12-B 最后
behavior tree）；M12-C anchors：`61f641ef` → `bb996a5` → `4a77673` →
`c68d2bb`；M12-D anchors：`80e4326` → `777f783` → `a0146b7` → `8416fe7`。
全部为 HEAD 祖先（实测）。**latest Human-tested behavior-bearing commit =
`8416fe7a26eaa8c79ab8186513b60af5954a18b9`**（Human-tested M12-D final
behavior tree；latest behavior-bearing tree across M12；**NOT yet verified
LKGC**；其后 docs commits 非 behavior baseline）。no-drift 实测：
`git diff 8416fe7..HEAD -- src tests CMakeLists.txt samples` = 空。

### 108.8 release / governance boundary（独立决策，非 M12 功能 blocker）

**SEPARATE RELEASE / GOVERNANCE ITEMS**：verified LKGC advancement（保持
`c68d2bbb…` UNCHANGED；推进需独立 Human 授权）；canonical package
creation/publication（NOT CREATED）；tag creation（仅 v1.0.0 既有）；
push（NONE）；real Modbus hardware validation（canonical = NOT VERIFIED /
future / separate hardware validation——非 M12 功能 closure 必需）。以上
**不得**误写为 M12 功能 closure blocker。

### 108.9 final M12 canonical state

```text
M12-A  = FOUNDATION ACCEPTED
M12-B  = COMPLETE
M12-C  = COMPLETE / HUMAN ACCEPTED / FINAL BASELINE FROZEN
M12-D  = COMPLETE / HUMAN ACCEPTED
M12 OVERALL = COMPLETE / HUMAN ACCEPTED
ALL CANONICAL M12 EXIT CONDITIONS = SATISFIED
UNRESOLVED REQUIRED P0 / HUMAN DECISION = NONE
CURRENT REQUIRED DEFECT BLOCKER = NONE
latest Human-tested behavior-bearing commit = 8416fe7a26eaa8c79ab8186513b60af5954a18b9
verified LKGC = c68d2bbb277096fd7fb9a76d99d7b588da6461f0（UNCHANGED）
canonical package = NOT CREATED
REAL MODBUS HARDWARE = NOT VERIFIED
tag = v1.0.0 only
push = NONE
```

### 108.10 action boundary

本节所在提交 docs-only 永不作 LKGC；无新 milestone / behavior slice 启动；
无 build/test/candidate；无 package/tag/push/amend。**NEXT = SEPARATE HUMAN
DECISION ON LKGC ADVANCEMENT OR OTHER POST-M12 RELEASE / GOVERNANCE
ACTION**。

## 109. POST-M12 HUMAN UX REPAIR — MANUAL Q&A PANEL RETRACT / REOPEN
（2026-10-06，narrow UI lifecycle repair + governance addendum）

> 本轮为 **narrow UI lifecycle repair**：Human 观测缺陷 → 只读源码审计 →
> REAL RED → QML-only GREEN → targeted → negative control → fresh canonical
> full → behavior commit → post-commit tree → candidate FROM ZERO →
> deployment gate → 本节归档。
> 未触碰 M12-D D1–D5 语义；未修改 ManualQaController /
> ModelScopeManualQaRunner / ManualQaContract（citation validator）/
> ManualStore / ML-2 delete / Candidate / DeviceProfile / Modbus；
> 未推进 LKGC；未 package/tag/push/amend；未开始任何新 milestone 或新
> behavior slice。**M12 overall 结论保持 COMPLETE / HUMAN ACCEPTED 不变，
> 未被重新开启。**

### 109.1 Human 观测（来源与口径）

本次 packet §1 记录的 Human 观测（**原文措辞未在本会话上下文中逐字保留，
因此本条按 packet 描述记录，不作为逐字引用**）：

```text
Manual Q&A 右侧面板在打开之后无法收回；
切换到诊断（Diagnosis）页之后，该面板仍占据窗口右侧。
```

**未声称**：真实硬件验证；package 验证；M12-D D1–D5 语义变更；任何
automated-only 路径升级为 Human PASS。

### 109.2 只读源码审计（§4，先于任何编辑）

审计对象 = `src/ui/qml/pages/DeviceProfilePage.qml`（当前 HEAD `b56ef61`）。

```text
面板实现          Drawer { id: manualQaCard; objectName: "manualQaCard";
                  edge: Qt.RightEdge; modal: false;
                  closePolicy: Popup.NoAutoClose; width: 360;
                  height: Overlay.overlay ? Overlay.overlay.height : 600 }
打开路径          manualQaOpenButton.onClicked -> manualQaCard.open()
                  （第 1113 行，全页唯一 open 调用）
关闭路径          `grep -n "manualQaCard\." DeviceProfilePage.qml`
                  → 仅 1 处命中：`manualQaCard.open()`；**全仓无 close() 调用**
面板内容 objectName
                  manualQaHeader / manualQaSelectedManual /
                  manualQaQuestionInput / manualQaAskButton /
                  manualQaRunningLabel / manualQaResultStatus /
                  manualQaAnswer / manualQaCitationsHeader / manualQaCitations
                  —— 无任何 close / retract / 关闭 控件
Escape / 外部点击  closePolicy: Popup.NoAutoClose → 两条内置关闭路径均被抑制
导航宿主          Main.qml StackLayout，页面用
                  `enabled: currentIndex === <index>` 切换
                  （诊断 = navItem_4 / index 4；设备 = navItem_5 / index 5）
```

关键结构性事实：`Drawer` 是 `QQuickPopup`，其 popup item 挂在**窗口 Overlay
层**，不属于页面的 StackLayout 子树。页面切换只改变页面的 `enabled`，
**不改变 popup 的 `open` / `position`**——因此面板在切到诊断页后继续占据
右侧，与 Human 观测完全一致。

另注（本轮 gate 实测语义，供后续引用）：Drawer 关闭时 `opened` 立即变
false，但 `visible` 在滑出过渡期间（exit transition）**保持 true**；布局事实
由 `position`（0 = 完全收回）与 `x`（RightEdge 收回时 = 窗口宽度）表达。

### 109.3 根因分类（§5，按源码事实，不猜）

```text
UX-R1-A（主根因，成立）  面板没有任何 Human 可见的关闭 / 收回控件。
                        证据：`manualQaCard.` 全页仅 1 处使用（open）；
                        面板内容清单中无 close/retract 控件。
UX-R1-E（并存根因，成立）打开路径单向（open-only），无 toggle /
                        close 语义；配合 NoAutoClose 抑制 Escape 与
                        外部点击 → 打开之后**不存在任何收回路径**。
UX-R1-B（不成立）        "存在关闭动作但无效"——不存在任何关闭动作。
UX-R1-C（不成立）        "关闭清空状态 / 取消请求"——无关闭路径可触发。
UX-R1-D（不成立）        "关闭后打开入口不可用"——打开入口与面板状态无关。
UX-R1-F（不成立）        "导航自动关闭"——源码中不存在任何导航联动。
```

结论：**A 是缺陷主体，E 是其放大器**（两者共同构成"打开即不可撤回"）；
本轮修复只针对 A + E，并显式**不引入** F（不发明导航语义），也不改变
C/D 判定所依赖的任何行为（修复后以 gate 正向证明 C/D 仍然成立）。

### 109.4 REAL RED（先于 GREEN，runtime UI semantic RED）

在既有 `--qml-manual-qa-check`（`src/main.cpp::runManualQaCheck`）追加
UX-A / UX-01..UX-12 stages。断言全部基于 **objectName 运行时反射**，
不引用任何新符号 → **compile-safe**；RED 是运行时 UI 语义失败，不是编译失败。

修复前（原始源码）实测首条失败与级联：

```text
QAGATE FAIL: UX-01 (REAL RED): the open Manual Q&A panel exposes NO
Human-visible retract control (no manualQaCloseButton in the visual tree)
- and closePolicy NoAutoClose suppresses Escape and outside-press, so the
panel cannot be retracted at all
QAGATE FAIL: UX-02: the retract control is missing
QAGATE FAIL: UX-03: the retract control is not clickable
QAGATE FAIL: UX-04: the panel is still visible after the retract control
was used
QAGATE FAIL: UX-07 / UX-08 / UX-11 / UX-12: the retract control is gone /
not usable / not available again
EXIT=1
```

RED 阶段同时暴露两处**本 gate 自身的缺陷**（已当场修正，属 gate 侧而非
产品侧，记录在案以免误读）：

1. 首版断言用 `property("open")` 读取 Drawer 状态——Qt 的属性名是
   `opened`（`QQuickPopup`），`open` 取值为空 → 检查静默失效。
   实测修正为 `opened`。
2. 首版 UX-05 断言 `cancelCount() != 0`（绝对计数）——该计数器在 gate 内
   **跨 stage 累积**（S7/S8 的 invalidation 两次已消耗取消），
   会误报。改为与基线做 before/after drift 比较。

### 109.5 GREEN（QML-only 修复）

变更文件：`src/ui/qml/pages/DeviceProfilePage.qml`（+18 / −3，唯一行为变更）。
面板 header 由裸 `SectionHeader` 改为 `RowLayout { SectionHeader +
AppButton }`：

```qml
RowLayout {
    Layout.fillWidth: true
    spacing: DS.spacingS

    SectionHeader {
        objectName: "manualQaHeader"
        Layout.fillWidth: true
        title: qsTr("手册问答（基于所选说明书证据）")
    }
    // POST-M12-UX-R1: the panel is an explicit retract-only
    // surface. This control is the single close path and is
    // always usable, including while an attempt is running:
    // closing is VISUAL ONLY (no cancel, no state clear).
    AppButton {
        objectName: "manualQaCloseButton"
        Accessible.name: qsTr("关闭手册问答")
        text: qsTr("关闭")
        onClicked: manualQaCard.close()
    }
}
```

设计要点：`AppButton` 是 presentation-only 组件，业务语义留在调用点
（`manualQaCard.close()`），符合 M9-A 组件约定；控件**永不 disabled**
（运行中也可关闭，见 109.6 UX-08）；`onClicked` 只调用 popup 的 `close()`，
不触碰 controller → 关闭天生是"纯视觉"操作。

### 109.6 GREEN matrix（UX-01..UX-12，提交后树 canonical 实测）

提交后树 `build/acceptance/session-post-m12-ux-r1-postcommit-release` 实测
（transcript = `build/_evidence/uxr1-green-postcommit.txt`，EXIT=0，0 FAIL）：

```text
QAGATE: WRITE [UX-R1]: …/ux-r1-01-panel-open.png 1280x937
QAGATE: UX-03: the real window-level click on the retract control closed the panel
QAGATE: UX-04: after the retract control, opened=0 visible=1
QAGATE: UX-04 (settled): opened=0 position=0 panelX=1280 controlScene=0,0 0x0
        painted=0 window=1280x937
QAGATE: WRITE [UX-R1]: …/ux-r1-02-device-closed.png 1280x937
QAGATE: UX-11 (settled): opened=0 position=0 panelX=1280 windowWidth=1280
QAGATE: WRITE [UX-R1]: …/ux-r1-03-diagnosis-closed.png 1280x937
QAGATE: WRITE [UX-R1]: …/ux-r1-04-device-reopened.png 1280x937
MANUAL QA CHECK PASS: the M12-D Q&A surface is present with an unambiguous
selected Manual context
```

| Stage | 契约 | 断言（摘要） | 实测 |
| --- | --- | --- | --- |
| UX-01 | A | 面板打开时存在 Human 可见的 retract 控件（存在/可见/enabled） | PASS（RED 时失败） |
| UX-02 | A | 控件是 Button（class 含 "Button"）、文案含「关闭」、位于面板矩形内且在窗口内 | PASS |
| UX-03 | A | **真实 Human 路径**：窗口级合成点击控件 → 面板关闭（未走 fallback：note 明示 real click） | PASS |
| UX-04 | B | 关闭后 `opened=0`；**exit transition 沉降后** `position=0`、`panelX=1280`（= 窗口宽度，完全移出可见布局）、控件不再 paint | PASS |
| UX-05 | D | 关闭前后 state/result/answer/citations/beginCount/cancelCount/documentId/candidateCount/profileDirty **零 drift** | PASS |
| UX-06 | C | 关闭后原打开入口仍可点击并重开（enter transition 沉降后 `opened=1`） | PASS |
| UX-07 | F | 重开后 question / selectedLabel / answer 文本与关闭前一致；retract 控件仍在 | PASS |
| UX-08 | E | 运行中关闭：state 仍 running、`cancelCount` 不变、Deferred 完成仍 pending（**关闭 ≠ Cancel**） | PASS |
| UX-09 | D/E | 面板关闭期间到达的完成**仍然落库**（result=found）——面板是视图，不是会话 owner | PASS |
| UX-10 | G | 切到诊断页**不自动关闭**（gate 显式先重开再导航，断言仍 opened） | PASS |
| UX-11 | B | 在诊断页关闭：`opened=0 position=0 panelX=1280`；诊断页 visible+enabled、`diagnosisRunBaselineButton` visible+enabled（页面可用） | PASS |
| UX-12 | C/F | 回到设备页重开成功；控件再次可用；UI 文本与 stable 状态（documentId / candidateCount / profileDirty）与关闭前一致 | PASS |

**像素级证据**（post-commit 树截图，1280×937，脚本实测）：

```text
open vs reopened  : changed px = 0        ← 关闭→重开状态被逐像素保留
open vs closed    : changed px = 52684    ← 关闭确实移除了面板区域
右侧区域 x920–1280 非白像素：
  01 open 20764 / 02 device closed 34228（页面内容回到该区域）
  / 03 diagnosis closed 12182 / 04 reopened 20764（= 01）
```

截图目录：`build/_evidence/uxr1-shots-postcommit/`（git-ignored 证据区，
与 `uxr1-shots/` 两轮一致）。

### 109.7 Negative control（真实 mutate → RED → 精确逆向还原 → GREEN）

- 变体：对 QML 施加**精确逆向 patch**（移除 RowLayout 包装与
  `manualQaCloseButton`，恢复原 `SectionHeader`）→ 重建 → 运行 gate。
- 实测 RED（`build/_evidence/uxr1-nx-red.txt`，EXIT=1）首条断言与 109.4
  完全一致；且沉降状态为 `opened=1 position=1 panelX=920`（完全打开、
  占据 920..1280）→ 证明该断言**由缺失的控件直接导致**。
- 还原：**精确正向 patch**（同一文本），未使用
  `git checkout -- <file>` / `git restore` / `git reset`；
  还原后 `md5sum -c` 校验 = `577a65a02712d646cb2ab03726cdfbde …: OK`，
  gate 复跑 EXIT=0（GREEN 复原）。

### 109.8 Targeted regression + fresh canonical full

```text
Targeted（工作树，15 tests）:
  manual_qa / manual_import / manual_delete / qml_manual_qa_check(+windows)
  / qml_manual_import_check(+windows) / qml_candidate_review_check(+windows)
  / qml_consent_check(+windows) / qml_active_profile_check
  / qml_profile_editor_check / qml_nav_check / qml_geometry_check
  → 100% tests passed, 0 failed out of 15（real 60.95s）

Fresh canonical tree（pre-commit）= build/acceptance/session-post-m12-ux-r1-release
  configure RC 0（57.5s + generate 2.6s；cache 实测
  MODBUSLENS_PYTHON_EXECUTABLE:FILEPATH=D:/Anaconda3/python.exe；
  "not registered" 计数 = 0）
  build 474/474 RC 0（error: 计数 = 0）
  ctest -N = 70（含 #36 deployment_startup_check / #37 c1b_dependency_materializer）
  full unfiltered ctest = 70/70 PASS，exit 0，real 234.97s（一次通过）

Fresh canonical tree（post-commit）= build/acceptance/session-post-m12-ux-r1-postcommit-release
  configure RC 0（Python 变量在 cache；"not registered" = 0）
  build 474/474 RC 0（error: = 0）
  full unfiltered ctest = 70/70 PASS，exit 0，real 211.48s
```

### 109.9 Behavior commit

```text
commit   = dde40024632aa8f8ab0de6d8cb86ce28d811f944
subject  = M12: make manual Q&A panel retractable
parent   = b56ef61a38c28c5a7c3cbec0008da41e4df2b0ac
files    = src/main.cpp (+611 / −0，全部位于 runManualQaCheck 的单个 hunk)
           src/ui/qml/pages/DeviceProfilePage.qml (+18 / −3)
总计     2 files changed, 629 insertions(+), 3 deletions(-)
NO AMEND；untracked 仅 _ctx.py / _dump.py（未触碰）
```

**行为边界证明**（`git diff --name-only` 实测）：

```text
git diff --name-only -- src/core src/ui/manualqa src/ui/manual
    src/ui/candidate src/ui/profile tests CMakeLists.txt
= （空）
```

即：citation validator / D1–D5 语义 / runner / controller / ManualStore /
ML-2 / Candidate / DeviceProfile / Modbus **零改动**；本轮唯一行为变更是
面板 header 的关闭控件（QML）与对应 gate stages。

### 109.10 Candidate（FROM ZERO）+ deployment gate

```text
候选生成   ninja modbuslens_candidate（post-commit tree）
           → "generate_candidate: candidate ready at
             …/session-post-m12-ux-r1-postcommit-release/candidate/ModbusLens
             (1713 files, manifest written)"
           生成器第 1 步即 file(REMOVE_RECURSE) 清空候选根 → 每次 FROM ZERO

exe 身份   source  modbuslens.exe = 6,886,038 B
                    SHA-256 c2c35d11577ede0f3950ddadbb487b8b0441e2390ca88a0d0f0da614b12f0e92
           candidate modbuslens.exe = 6,886,038 B / 同 SHA-256（**identical**）
           manifest entries = 1713 / root 实际文件（含 manifest）= 1714 /
           root 目录数 = 90 / generated-by = modbuslens_generate_candidate.cmake
           platforms/qwindows.dll SHA-256 = 804739071bba619b4a4312b5bb29a142545a64c4c80218e5b2e6672ad33ee8ac
           pdfium.dll               SHA-256 = d42c452a4cf8ca19a87e9c659d4e05035be742c21696ac13431cf73ac1bbf14b

deployment gate（该 exact candidate；凭据缺席 + 净化环境：
    env -u MODELSCOPE_API_KEY -u MODBUSLENS_MODELSCOPE_MODEL
        -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH
        -u QML_IMPORT_PATH -u QML2_IMPORT_PATH）
  ctest -R "^deployment_startup_check$"        → Passed 28.00s
  ctest -R "^deployment_startup_check$" -V     → Passed 27.34s
      "-- deployment_startup_check: candidate …/candidate/ModbusLens —
        manifest verified, sanitized launch PASSED (exit 0,
        SMOKE IDENTITY PASS, qwindows from candidate)"
PATH 只含 candidate root + Windows 系统目录；零 provider 请求（--qml-smoke-test
为有限启动，无网络路径）。
```

### 109.11 状态收口

```text
MANUAL Q&A PANEL RETRACT UX REPAIR = IMPLEMENTED / AUTOMATED PASS
M12 OVERALL = COMPLETE / HUMAN ACCEPTED（UNCHANGED；NOT REOPENED）
  M12-A = FOUNDATION ACCEPTED；M12-B = COMPLETE；
  M12-C = COMPLETE / HUMAN ACCEPTED / FINAL BASELINE FROZEN；
  M12-D = COMPLETE / HUMAN ACCEPTED
D1–D5 = 未变更（零源码改动；fresh full 70/70 复证）
HUMAN UX RETEST（面板收回/重开） = PENDING
latest Human-tested behavior-bearing commit = 8416fe7a26eaa8c79ab8186513b60af5954a18b9
  （不变；本轮新 behavior commit dde4002… **尚未**经 Human 验证）
verified LKGC = c68d2bbb277096fd7fb9a76d99d7b588da6461f0（UNCHANGED，NO advance）
canonical package = NOT CREATED
REAL MODBUS HARDWARE = NOT VERIFIED
tag = v1.0.0 only；push = NONE
NEXT = HUMAN PANEL RETRACT / REOPEN RETEST USING THE NEW CANDIDATE
```

### 109.12 Human 重测清单（12 步，使用 109.10 的 candidate）

```text
 1. 用 candidate/ModbusLens/modbuslens.exe 启动（无需开发环境 Qt/MinGW）。
 2. 进入「设备」页，导入或选中一本说明书。
 3. 点击「手册问答」打开右侧面板；确认面板右上角有明确的「关闭」按钮。
 4. （可选）等待/执行一次提问，得到 FOUND 答案与引用。
 5. 点击「关闭」：确认面板从右侧**滑出并消失**，页面恢复完整宽度。
 6. 确认问题草稿、所选说明书、答案与引用在面板上仍然保留（重开后一致）。
 7. 再次点击「手册问答」重开：确认内容与关闭前一致，可继续使用。
 8. 提问后（Running 期间）点击「关闭」：确认**不是取消**，请求继续。
 9. 关闭状态下等待回答返回，再重开面板：确认答案/引用已呈现。
10. 面板打开时切到「诊断」页：确认页面没有被面板挡住（面板仍可关闭
    ——这是刻意的显式收回设计，本轮不引入导航自动关闭）。
11. 在诊断页点击「关闭」：确认面板消失且诊断页操作正常（运行基线等）。
12. 回到「设备」页再次打开/关闭：确认无异常、无状态丢失。
```

### 109.13 Knowledge Learned / Interview Notes

- **Qt Quick 的 popup 层次与页面导航正交**：`Drawer`/`Popup` 挂在窗口
  `Overlay`，页面 `StackLayout` 的 `enabled` 切换不影响 popup → "面板跟着
  页面走"必须由显式状态或显式关闭控件解决，不能靠页面可见性。
- **`closePolicy: Popup.NoAutoClose` 是双刃剑**：它消除了误触关闭，但同时
  移除 Escape 与外部点击两条内置路径 → 必须自备显式关闭控件，否则面板
  变成"单向上屏"。
- **Drawer 的关闭是位置驱动**：`opened` 立即 false，`visible` 在 exit
  transition 期间保持 true，布局事实由 `position`（0=收回）与 `x`（RightEdge
  收回 = 窗口宽度）表达 → 断言必须在过渡**沉降后**取，且不能用 `visible`
  当布局信号。
- **属性名即契约**：`QObject::property("open")` 对 `opened` 静默返回空值，
  使断言"看起来通过"——反射式断言必须对关键属性做存在性/取值校验。
- **计数器式断言的陷阱**：跨 stage 累积的计数字段只能做 before/after
  drift，不能做绝对值断言。
- **负向对照的纪律**：真实 mutate → RED → 精确逆向 patch → 复原校验
  （md5 一致）→ 复绿；禁止 `git checkout/restore/reset` 还原。
- **UI 缺陷的像素级证据**：关闭→重开后截图逐像素相同（0 changed px）
  是"状态未被关闭动作破坏"的最强自动化证据之一（仍非 Human PASS）。

### 109.14 action boundary

本轮 behavior commit `dde4002…` 为 **LKGC candidate**（真实改动路径含
`src/` 与 QML），但 **本期不推进 LKGC**（无人验收项完成前不得推进）。
本节所在 docs-only 提交永不作 LKGC。未 package / tag / push / amend；
未开始新 milestone / 新 behavior slice。**NEXT = HUMAN PANEL RETRACT /
REOPEN RETEST USING THE NEW CANDIDATE**。
