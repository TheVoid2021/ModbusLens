# ADR-004: Evidence-Gated Register Candidate Extraction（T027 §66 H8 规则修订）

- **状态：原状态行保留于下（不覆盖）：PROPOSED — PENDING HUMAN REVIEW（2026-10-14）。本 ADR 未被 Human 接受前，T035-A（AI Register Candidate Extraction）不得进入实现（T035 统一执行指令 §11-A）。**
- **状态追记（2026-10-14，A.3.2.3 轮）：** 该准入条件的「接受」事实，依任务档案 §6 时间线 **R4**：「Phase 1 四方向 = HUMAN ACCEPTED（D = v1 最小 schema）」——四方向含本 ADR（评审点 A / H8 修订方向）。其后 A.1/A.2/A.3.1/A.3.2.1 依次实现并获 HUMAN ACCEPTED；A.3.2.2 = IMPLEMENTED / PENDING HUMAN REVIEW；A.3.2.3 见文末「关联裁决记录」。**口径诚实声明：** 任务档案 §3B-D5-A 与台账 T035-P02 仍写着「ADR-004 = PROPOSED」，那是**提出时状态**的原文（该档案 append-only，未回改）；本 ADR 的正式状态字段**未**由 Agent 单方改为 ACCEPTED——**是否把本 ADR 状态字段提升为 ACCEPTED 属 Human 裁定项**。本 ADR 的下一次实质变更见文末追加记录。
- **关联任务**：T035（task doc §3B-D5-A/D5-D；问题台账 T035-P02）；关联契约：T027 §66（H1–H10，尤其 H6 Evidence / H8 Output Contract）、T027 §24.3（冻结公式）；关联机制记录：`docs/devlog/2026-10-13.md` §11.3（CRLF 机制）、T034-B2-HV2 CAND-H1/H2。

## 背景

T027 §66 冻结的 H8 规则原文：「the AI … must never guess scale / offset / unit / register meaning / address / field value」。该规则制定于 M12-C C2 first slice——当时候选目标仅有 manufacturer 单字段，字段黑名单式禁令是正确且保守的。

T035 的产品目标（Human 统一执行指令 §0/§4）要求 AI 从设备手册提取 **Register Mapping Candidate**，最低字段含 functionCode / address / name / dataType / quantity / scale / offset / unit 等——其中 scale / offset / unit / address / register meaning 恰为 H8 黑名单字段。维持 H8 原文则寄存器候选只能携带名称类字段，工程量解释闭环（Raw → 模型 → 规则 → 工程量）无法由 AI 辅助建立。

## 备选方案

1. **维持 H8 原文**：register 候选仅含 name 类字段，scale/offset/unit 永远手工录入。→ 产品目标半残，AI 提取价值有限（排除理由：与 T035 目标冲突）。
2. **删除安全限制**：AI 自由生成全部字段，不要求证据。→ 违反 T035 §2 硬规则（禁止降低 Candidate 安全校验、禁止绕过 Human Review）与既有 H6 机制（排除）。
3. **Evidence-Gated 修订（本 ADR 提案）**：把「字段黑名单」精确化为「可验证性门槛」——禁止无证据推测；允许有明确文档证据时提取候选；证据匹配判定权 = 本地确定性代码；字段值正确性判定权 = Human review。

## 决策（待 Human 接受）

采纳方案 3，具体边界如下：

1. **允许**：AI 提取明确存在于手册文本中的信息（例：PDF 表格行含 "Register 5 / Temperature / Uint16 / Resolution 0.1℃" ⇒ 可提 address=5、dataType=Uint16、scale=0.1、unit=℃）。
2. **禁止**：无文档证据的推测（例：文档未说明 scale/unit ⇒ 不得生成，留冻结默认值 scale=1.0 / offset=0.0 / unit=空，UI 标注「手册未给出」）。
3. **判定权分层（不新增信任面）**：
   - 证据真实性/唯一性 = **本地确定性代码**（现有精确子串唯一性机制原样保留，禁止模糊匹配/相似度/重复容忍）；
   - 字段值正确性 = **Human review**（Accept 即 Human 确认；与既有 manufacturer 链相同的信任模型，如实写入 UI 提示）；
   - schema 合法性 = **严格解析器**（成员集冻结，未知成员/类型错/缺必填 ⇒ 整响应原子拒绝）。
4. **不删除、不降级任何现有校验**：H6 evidence 机制、C3 应用前重校验、`validateDeviceProfile` 唯一 validation owner、AI → Proposal → Evidence → Human Review → Device Profile 链路全部保持。

## 理由

- H8 原文的真实意图是「禁止无据生成」而非「永久禁止字段扩展」；当时候据范围只有 manufacturer 是范围约束而非安全终态。
- 方案 3 把安全边界从「字段枚举」升级为「可验证性判定」，与 H6 同构：AI 输出始终是 UNTRUSTED INPUT，只有通过本地确定性验证的候选才进入 Human review。
- 默认值策略（无证据 ⇒ 冻结默认）保证「宁缺勿错」：缺证据字段退化为现状（手工补填），而不是带上编造值。

## 后果

**正面**：寄存器候选可携带完整工程量字段；T035 产品闭环可建立；安全模型与现有机制连续，无第二套信任体系。

**负面**：
1. 「AI 编造字段值但附真实摘录」的残余风险仍存在（与 manufacturer 链相同）——由 Human review 兜底，须在评审 UI 明示。
2. 证据匹配对 PDF 表格文本形态敏感：PDF canonical text 行尾为 CRLF，跨行摘录在现行零规范化精确匹配下必被拒（`evidence_not_found`）——该问题的处置（T035-P03 / 统一指令评审点 B-2）是**独立裁定项**，不在本 ADR 内默认放行。
3. quantity/dataType 一致性、FC/address 域校验等新增确定性校验需要新增测试面（RED 优先，入既有测试 target）。

## 关联裁决记录

（Human 接受/拒绝后在此追加批注，不覆盖上文。）

### 2026-10-14 · T035-A.3.2.3 evidence 匹配规则限缩修订（IMPLEMENTED / PENDING HUMAN REVIEW）

**背景（事实层，先于裁定）**：T035-A.3.2.2 完成后进入 A.3.2.3（Evidence normalization，即本文 §后果.2 与任务档案 §3B-D2/§3B-D5-B 的 B-2 项）。Agent 在**只读侦察**阶段实测本机真实 PDF canonical text 形态：聚合后 **CR=286 / LF=301 / CRLF pairs=286**，**页内 CRLF、页间裸 LF**（16 页 ⇒ 15 个连接符）——即 canonical 为**混合行尾**，而非统一的 CRLF 或 LF。

**由此推翻两项此前判断**：

1. ~~「只有跨页边界才会因行尾失配」~~ → **作废**。canonical 页内既为 CRLF，则**模型返回 LF 时同页内也会失配**。真实 Provider Evidence 格式仍为 **UNKNOWN**，失配面**不可**按"页边界"收窄推定。
2. ~~「CRLF→LF 不会引入新重复（因此不降低强度）」~~ → **作废**。规范化会把**原本不同**的原始片段（例如一处 CRLF 形态 + 一处孤立 CR 形态）合流为**相同**片段。

**Human 裁定（取代原 Normalization V1 的较宽范围）**：

- **仅**实施确定性**行尾等价**匹配：`CRLF → LF`、孤立 `CR → LF`、`LF → LF`；**非行尾字符必须逐字符一致**。
- **禁止**：TAB → SPACE、连续空格折叠、任意空白统一化、Unicode 正规化、大小写转换、数字格式转换、标点删除、模糊匹配、相似度匹配、语义匹配。**完整 whitespace normalization = DEFERRED**。
- **唯一性不放松**：规范化视图上的命中必须**恰好一次**；0 次 = `not_found`；**≥2 次 = ambiguous reject，禁止直接采用第一个匹配**。
- **raw 偏移保真**：返回的 span 必须是**原始文本**的真实偏移（经确定性 normalized→raw index map，CRLF 两字节对应一个规范化字符）；并须通过 **round-trip**（`normalize(raw.slice) == normalize(evidence)`，仅索引范围合法**不够**）。
- **同源复验**：提取期与 Human Accept 期必须使用**同一确定性规则**（新增 `revalidateRegisterCandidateEvidence`，register 专用）。
- **manufacturer / profile-field 路径继续 exact-only**（`locateUniqueExcerpt` 与 `revalidateCandidateEvidence` 语义不变）。
- **身份不变**：`documentId` / `contentHash` 基于**原始 canonical** 不变；**不得**改写整份 canonical document 或引入"规范化后文档"作为新的 canonical 形态。
- **不得洗白**：存储的 excerpt 仍为 **Provider 原始提交**；规范化仅用于**建立行尾等价并定位**，不得以"只检查已存 raw excerpt"绕过最初 Provider Evidence 的真实性/唯一性约束。
- **forensics 与判定同源**：不得出现"判定 PASS 而 forensics 报 `evidence_not_found`"；新增 sanitized `match_mode` token（`exact` / `line_ending_normalized` / `none`）。

**对本文 §决策.3 的影响（不删除原文，追加说明）**：§决策.3 第一分项原文为「证据真实性/唯一性 = 本地确定性代码（**现有精确子串唯一性机制原样保留，禁止模糊匹配/相似度/重复容忍**）」。本次修订在**不放宽唯一性、不引入模糊/相似度**的前提下，把"精确"细化为"**精确优先，其后仅限行尾等价**"——**register 路径**适用，**manufacturer 路径不受影响**。§决策.3 的第二、三分项（值正确性 = Human review；schema = 严格解析器）**不变**。§决策.4「不删除、不降级任何现有校验」**保持有效**：本轮未删除任何校验，唯一性检查在规范化视图上**至少与 exact 同严**。

**定性（必须与结论一起引用）**：本修订交付的是一种**确定性、范围受限的兼容机制**，**不构成**"历史 AI 提取失败根因已被修复"的证明。**该根因仍为 UNRESOLVED**（依 `docs/devlog/2026-10-13.md` §11.4 的既有诚实标准），真实 Provider Evidence 格式仍为 **UNKNOWN**。

**验证**（详见 `docs/tasks/T035-device-intelligence-enhancement.md` §5.7 与 `docs/devlog/2026-10-14.md` §10）：六套件 **254 passed / 0 failed / exit 0 ×6** + QML 门禁 exit 0 ×4 + `-functions` 槽注册实测 + **负向对照 REAL RED→精确逆向复绿**（a323_10 规范化重复必拒：38/1 → 39/0）+ `git diff --check` PASS。
