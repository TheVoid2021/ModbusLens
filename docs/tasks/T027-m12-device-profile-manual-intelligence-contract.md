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
