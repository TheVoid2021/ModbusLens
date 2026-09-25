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
