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
