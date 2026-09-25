# T024 — M11 Register Readout & Decode：Contract / Acceptance Definition

> **状态：CONTRACT DEFINED — AWAITING HUMAN REVIEW（docs-only；Implementation = NOT STARTED）。**
> **授权依据**：Human 明确说「开始 M11」（2026-09-24 handoff）⇒ M11 获得 **START AUTHORIZATION**；
> 本轮执行的是 M11 的**第一阶段：canonical scope / boundary / acceptance contract**。
> 该授权**不等于**：无契约的实现授权 / push 授权 / tag 授权 / release 授权 / LKGC 推进授权。
> 本文档不修改任何产品代码；所有 repo 事实均经本轮只读重验。

---

## 1. Canonical objective

在 M10 已冻结的 **raw wire truth**（`TransactionAnalysis.values` = 原始 uint16 寄存器字）之上，为**成功的 register-read 结果**提供**派生的、只读的语义解释视图**（Hex / Binary / UInt16 / Int16 / UInt32 / Int32 / Float32，含 word order），使读取结果**可解释**而不改变任何 wire 事实、不触碰 raw 权威、不引入设备语义声称。

一句话边界：**M10 拥有事实，M11 提供视图。**

## 2. Repo basis（逐条引用，全部本轮重验）

| # | 依据 | 内容 | 位置 |
| --- | --- | --- | --- |
| B1 | canonical M11 名称与范围 | 「### M11 — Register Readout & Decode：FC03 successful response → raw register values → Hex / Binary / UInt16 / Int16 / UInt32 / Int32 / Float32（含 byte/word order）。必须区分：**raw deterministic register data** 与 **device-specific physical semantics**。」 | `docs/11_V2_UPGRADE_PLAN.md` §M11（line 98 段） |
| B2 | BACKLOG M11 行 | 「raw register values → Hex/Binary/UInt16/Int16/UInt32/Int32/Float32 + byte/word-order；区分 raw data 与 device semantics」；此前状态 `⏸ HOLD`（T023 明确 M11-B1–B5，值语义全属 M11，须 Human 另行立项） | `docs/BACKLOG.md` line 21 |
| B3 | T023 M11-B1–B5（READ-M11 边界） | B1 M11=Register Readout & Decode；B2 int16 语义解释 / Float32 / byte·word order / raw vs device physical semantics **全部属 M11**；B3 CLASS-10 值表只呈现**原始 uint16**（DEC + 0xHEX）；B4 core（GAP-1）只保留 `std::vector<std::uint16_t>` 原始值、**M11 可在其上加解码视图**；B5 M11 scope 需 Human 立项（= 本轮） | `docs/tasks/T023-…-contract.md` §14（line 533–544） |
| B4 | raw words 权威已落地 | `TransactionAnalysis` 增补 `std::vector<std::uint16_t> values`（append-last，**RAW ONLY**：「the protocol-level uint16 words exactly as …」；Success 才携带，非 Success 恒空） | `src/core/analysis/TransactionAnalysis.h:150–173` |
| B5 | 读取功能码已可编辑（FC04/custom 基础） | `ReadHoldingRegistersIntent` +`functionCode`（默认 0x03）；`activeRequestFunctionCode()` = wire byte 唯一权威；`ReadFunctionCodeOutOfRange`（0x01..0x7F）；分析器从 REQUEST FRAME 派生 `F / F|0x80`（03→83、04→84、41→C1，无 per-code 副本）；`parseReadFunctionCode`（core）+ QML 五字段 DecimalField raw-text | `src/core/active/ActiveRequestIntent.h:69,118,131`；`src/core/active/ReadRequestParsing.h:44`；`src/core/serial/SerialTransactionSession.cpp:51,254`；`src/core/analysis/TransactionAnalysis.cpp:264,364` |
| B6 | PDU/0-based 地址权威 | 全链 PDU/0-based；HEX/DEC 仅为显示投影 | T023 §4 / M10-F request builder |
| B7 | M10 现状 | M10 = COMPLETE；M10-F = CLOSED；verified LKGC = `352b81c82d5efa9aac5418cccaaf1605a68cd9d3`（Human 2026-09-24 授权）；Final accepted portable D = `D013B12F…B3FFBC65` | `docs/PROJECT_STATUS.md` 头部批注 |

## 3. M10 / M11 boundary

**M10 拥有（M11 不得改写）**：request 配置（五字段 raw-text）+ 可编辑读取功能码；Actual TX / Actual RX 原始字节；10 类 Read Result 分类法与全部 outcome/issue；**raw uint16 register words（`TransactionAnalysis.values`）**；raw PDU 地址；DEC / HEX 展示；FC03 / FC04（register-read-compatible）/ 自定义 register-read-compatible 功能码；FC06 / FC16 写路径；transaction history / evidence。

**M10 明确排除（未实现）**：int16 语义解释、Float32、byte/word order、scaling、engineering units、register map、device 语义。

**排除项的 M11 归属（逐项，§P-9 / §P-10）**：

| M10 排除项 | 归属 | 依据 |
| --- | --- | --- |
| int16 语义解释 | **M11 显式指派** | T023 M11-B2 |
| Float32 | **M11 显式指派** | T023 M11-B2 + 11_V2_UPGRADE_PLAN §M11 |
| byte/word order（含 word swap） | **M11 显式指派** | T023 M11-B2 + 11_V2_UPGRADE_PLAN §M11 |
| raw data vs device physical semantics 区分 | **M11 显式指派** | T023 M11-B2 |
| UInt32 / Int32 / Binary / Hex | **M11（plan 逐名列举）** | 11_V2_UPGRADE_PLAN §M11 + BACKLOG M11 行 |
| scaling / offset / engineering units | **repo 未指派**（既不在 M10，也未被写进 M11 条文） | 11_V2_UPGRADE_PLAN §M11 无此三词；T023 M11-B2 无 |
| register map / device profile / 40001 别名 | **M12 域**（Device Profile & Manual Intelligence） | 11_V2_UPGRADE_PLAN §M12 |

> **诚实声明**：§P-10 的答案是「M10 排除项只有一部分被显式指派给 M11」。本契约对未指派项的处理见 §6（全部 DEFERRED 或 OUT OF SCOPE，不因「常见」而升级为 REQUIRED）。

## 4. REQUIRED NOW（M11 v1）

| # | 能力 | repo 依据 |
| --- | --- | --- |
| R1 | 对「成功 register-read 结果」的 canonical raw uint16 words 提供只读**派生解码视图**：Hex、Binary、UInt16、Int16、UInt32、Int32、Float32 | B1（plan 逐名）+ B3-B4 |
| R2 | **ordering 双轴均 REQUIRED**（canonical §M11 逐字「含 byte/word order」）：**byte order**（16-bit 寄存器内字节序）= `normal`（协议大端直通，默认）与 `byte-swapped within register`；**word order**（寄存器间顺序，仅 2 寄存器类型适用）= `big-endian`（AB CD，默认）与 `little-endian word order`（CD AB）。类型选择 + 两轴选择为**用户显式配置** | B1 逐字（「含 byte/word order」同时点名两轴）+ T023 M11-B2「word swap 属 M11」 |
| R3 | 解码资格按「**成功的 register-read-compatible 事务 + canonical raw words**」判定（FC03 / FC04 / 自定义 register-read-compatible 功能码一视同仁）；**禁止** `if function == 0x03` 硬编码；**禁止**把 FC01/FC02 位读当寄存器读 | T023 Part C（功能码可编辑、`0x01..0x7F`）+ 分析器已按 request frame 参数化 |
| R4 | **Raw truth 不变式**：decode 只是派生视图；`TransactionAnalysis.values` 仍是唯一 raw 权威；raw DEC/HEX 展示保持原样并在解码视图旁边始终可查；**decode 状态/错误绝不改写 wire result**（Success 不得因解码失败变成 Timeout/TransportError/CRC/Exception） | T023 M11-B3/B4 + READ-RX-5 既有原则 |
| R5 | **Decode failure model v1**（最小集，见 §9）：`InsufficientWords` / `OutOfRangeSelection` / `InvalidConfiguration` / `UnsupportedType` —— 独立的 decode 状态，**不是** wire outcome | T023 R-FACT 原则（事实与原因分离）的 M11 延伸 |
| R6 | **UI 边界**：接入**既有** Read Result 呈现（Communication 页结论行 + 有界可滚动 read-result 对话框的值表区域）：解码视图与 raw DEC/HEX 并排呈现；用户以**显式控件**选择类型与（2 寄存器类型的）word order；**不新建大页面**（T023 §7 已否决独立页先例） | T023 READ-UI + §7 IA 决议 |
| R7 | **地址语义**：PDU / 0-based 权威不变；raw register address（PDU）在解码视图中保持可追溯 | B6 + T023 §4 |

## 5. DEFERRED（本轮明确不做，且验收矩阵不得伪造其用例）

| 项 | 状态 | 依据 / 理由 |
| --- | --- | --- |
| scaling / offset / engineering units | DEFERRED | repo 无任何条文指派（§P-10）；公式 repo 未定义（`decoded*scale` 还是 `+offset` 均无据），**不得发明** |
| 40001 / 4xxxx 展示别名 | DEFERRED | 地址权威为 PDU/0-based（B6）；别名属 presentation，需 Human 明确要求 |
| Float64 / ASCII-string 类型 | DEFERRED | 不在 plan §M11 类型清单中 |
| register map / device profile / vendor 语义 | OUT OF SCOPE（M12 域） | 11_V2_UPGRADE_PLAN §M12 |
| 自动设备识别 / 自动单位推断 | OUT OF SCOPE | §X |

## 6. OUT OF SCOPE（M11 v1 明确不做）

- 任何**改写 wire transaction truth** 的映射（decode 错误绝不改写 Success）。
- QML 内的第二套 wire 解码（QML 只消费 core 提供的事实/视图）。
- vendor-specific 物理语义声称（custom FC 只意味着「可用 register-read-compatible schema」，不声称厂商语义）。
- 自动 device profile / register 数据库 / 40001 自动映射。
- 自动 retry、自动写回、Agent 自主写。

## 7. 数据类型矩阵（v1 冻结）

| 视图 | 寄存器宽 | 输入字数 | 解释规则 | 显示规则 | 失败行为 |
| --- | --- | --- | --- | --- | --- |
| Hex | 16 bit | 1 | 位型直通 | `0x%X`（大写十六进制） | —（raw 恒可用） |
| Binary | 16 bit | 1 | 位型直通 | `0b` + 16 位二进制 | — |
| UInt16 | 16 bit | 1 | 无符号 | 0..65535（DEC） | — |
| Int16 | 16 bit | 1 | two's complement 有符号 | −32768..32767 | — |
| UInt32 | 32 bit | 2 | 按 word order 组合 32 位位型，无符号 | 0..4294967295 | InsufficientWords / OutOfRangeSelection |
| Int32 | 32 bit | 2 | 同上 + two's complement | −2147483648..2147483647 | 同上 |
| Float32 | 32 bit | 2 | IEEE-754 binary32 位型（按 word order 组合后直接重解释） | 按 IEEE 语义显示；**NaN / ±Inf 按位型如实呈现（显示 NaN / Inf），不视为 decode 错误** | 同上 |

**ordering 冻结（双轴，canonical §M11「含 byte/word order」逐字依据）**：
- **byte order（16-bit 寄存器内字节序；全部类型适用）**：`normal`（默认；协议大端直通，raw word 原样）与 `byte-swapped within register`（每字高/低字节互换——**仅用于该解码视图，不改 raw**）。
- **word order（寄存器间顺序；仅 2 寄存器类型适用）**：`big-endian`（默认；words[0] 为高 16 位，AB CD）与 `little-endian word order`（words[1] 为高 16 位，CD AB）。
- **组合管线**：raw words →（可选 byte swap within register）→（word order 组合，2 字类型）→ 按目标类型重解释。默认 `normal` + `big-endian` = 协议标准直通。raw 永不改变。
- **术语纪律**：`word order`（寄存器间）、`byte order`（16-bit 字内部）、`endianness`（泛称）三词不得混用；`byte swap` 专指寄存器内互换，`word swap` 专指寄存器间互换。

**Float32 确定性向量（REQUIRED 用例，写入验收矩阵）**：
`1.0` = words [0x3F80, 0x0000]（big-endian）；`0.0` = [0x0000, 0x0000]；`−2.0` = [0xC000, 0x0000]；NaN = [0x7FC0, 0x0000]（任一 NaN 位型）；+Inf = [0x7F80, 0x0000]；little-endian 下 `1.0` = [0x0000, 0x3F80]。

## 8. byte order / word order 术语（§V 冻结）

- **register（word）order**：多寄存器值中**寄存器之间的先后解释**（AB CD vs CD AB）。M11 v1 REQUIRED。
- **byte order within register**：单个 16-bit 寄存器**内部**两个字节的先后。M11 v1 **DEFERRED**。
- 本契约与未来 UI 文案中禁止把 `endianness`（泛称）、`byte swap`（寄存器内）、`word swap`（寄存器间）三词互换使用。

## 9. Decode failure model（v1 冻结）

独立于 wire result 的 **decode status 共 5 个状态**：`Ok` = success；其余 **4 个 = decode/configuration failure states**。不新增 wire outcome，不触碰七 outcome：

| decode status | 触发 | wire result | raw 呈现 |
| --- | --- | --- | --- |
| `Ok` | 解码成功 | 不变 | 正常 |
| `InsufficientWords` | 所选起点 + 类型所需字数 > 返回字数（如末寄存器取 UInt32） | **仍为 Success** | raw 恒在 |
| `OutOfRangeSelection` | 所选起点不在返回数据范围内 | 仍为 Success | raw 恒在 |
| `InvalidConfiguration` | 类型所需字数 ∉ {1,2} / 数量为 0 / 其它非法配置 | 仍为 Success | raw 恒在 |
| `UnsupportedType` | 所选类型不在 v1 矩阵（防御位；UI 不应能选出） | 仍为 Success | raw 恒在 |

- decode status **绝不**映射进 `TransactionStatus`，也**绝不**改写 Read Result 10 类分类（CLASS-10 仍是 wire 事实）。
- NaN / ±Inf 是**合法 IEEE 值**，不是 decode failure。

## 10. UI boundary（§Z）

- 解码视图**并入既有 Read Result 呈现**（T023 方案 B：结论行 + 有界可滚动证据对话框的 CLASS-10 值表区域），**不新建独立页**、不复制 raw 数据。
- raw DEC/HEX 列**恒显**；解码列在其旁（同表或紧邻区块），二者逐寄存器对齐、共享同一 PDU 地址。
- 类型选择（Hex/Binary/UInt16/Int16/UInt32/Int32/Float32）与 **ordering 控件**（byte order：全部类型适用；word order：仅 2 寄存器类型启用）为**用户显式控件**；默认类型与控件落位已经 Human 裁定冻结（§22：默认 **UInt16**；控件位于 **Read Result 详情区域/详情对话框**，不新增主页一排控件）。
- decode status 非 `Ok` 时：解码单元格显示状态文案（如「字数不足」），**raw 列不受影响**。
- **1000×700 约束**：新增控件不得把既有内容推出窗口（M10-F ISSUE-016/018 的教训）；实现轮必须以真实 windows QPA 几何门禁复测。

## 11. Function Code boundary（§S）

- 解码资格 = `read_success`（CLASS-10）+ canonical raw words 存在；与功能码数值无关（FC03 / FC04 / 自定义 register-read-compatible 一致）。
- 禁止 FC01/FC02 位读语义混入；禁止厂商语义声称；异常响应（F|0x80）不是成功读取，无解码资格（无 raw words）。

## 12. Raw-truth ownership（§R）

`TransactionAnalysis.values`（RAW uint16，Success 才有）是**唯一**寄存器值权威；M11 的解码结果是**派生视图**（可在 core 增加纯函数 `decodeRegisterView(words, type, wordOrder, start, count) -> result`，或在 presentation 层调用 core 纯解码——实现方向在 implementation entry gate 后由 source audit 决定）；**任何路径都不得把解码结果写回 TransactionAnalysis / model role / QML 状态以冒充 raw**。

## 13. Hardware policy（§AB）

- 纯确定性解码**可以且应该**用已知 raw words 自动测试（无需硬件）。
- **Real hardware 在 M11 v1 中不作为 closure hard gate；如执行，属 supplementary / optional evidence。**
- **来源定性（诚实边界）**：这是 **T024 新定义，待 Human Review 接受** —— **不是** M10-F §ZE14 的自动跨 milestone 继承。§ZE14 标题虽为「M10-F / M11 boundary」且明确「M11 = Register Readout & Decode，继续 HOLD；不得把 M10 的 0x10 write 与 M11 的 register decode 混成一个阶段」，但其 **OPTIONAL 硬件条款写在 M10-F 上下文中**；repo **未发现**「§ZE14 的 OPTIONAL 规则治理 M11 closure」或「M11 real hardware 为 required hard gate」的明文 —— 因此按「repo silent ⇒ 不升格、不发明」处理，由本契约显式定义。
- 附注：§ZE14 写于读取功能码可编辑（T023 Part C）之前，其「M11 解码对象主要来自 successful FC03 raw registers」反映当时状态；现行 canonical = T023 Part C 后的 FC03 / FC04 / 自定义 register-read-compatible 结果（见 §11）。

## 14. Package / portable policy（§AC）

- M11 v1 最终验收**沿用 repo 惯例**：full Debug/Release CTest + 全部 QML gates + 真实 windows QPA（含 1000×700）+ Human review；这些在 implementation 轮执行。
- **canonical package / portable Final D 是否为 M11 必需**：repo 对 M11 **无明文**（M10-F 的 canonical-package 要求属 M10-F 自身契约）。本轮**不预设**；由 Human 在 M11 implementation 完成、进入 acceptance 时裁定。本轮不执行任何 build/test/package/windeployqt。
- **WorkBuddy/Zcode 环境边界**：WorkBuddy 曾有 windeployqt 宿主环境问题（qtpaths OK / windeployqt 失败；原生 PowerShell 两者皆 PASS）——那是 **WorkBuddy 专属执行环境观察**，ZCode **不得继承**为既有 blocker；未来打包阶段若涉及 windeployqt，以 ZCode 当时环境的实测证据判断。

## 15. M10 regression contract（§AD）

M11 实现轮与 exit criteria 必须保护（以 repo 现行等价物为准，本轮不执行）：五字段键盘编辑、FC03 / FC04 / 自定义 register-read-compatible 读取、Actual TX / Actual RX、Read Result 10 类分类与文案、raw uint16 / DEC / HEX、FC06 / FC16、R15 / R16 / R17 markers、QML 诊断 0/0/0（ReferenceError/TypeError/Unable to assign）、1000×700、package independence（`git ls-files build = 0`）。

## 16. Acceptance matrix（v1 REQUIRED 全覆盖；实现轮逐行执行）

> 列：ID / 场景 / raw words（输入）/ 配置 / 期望 decoded / 期望 decode status / raw truth / 自动化 / Human? / HW?

| ID | 场景 | raw words | 配置 | 期望 decoded | status | raw truth | 自动化 | Human | HW |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A01 | UInt16 max | [0xFFFF] | UInt16 | 65535 | Ok | DEC/HEX 不变 | ✅ | — | — |
| A02 | UInt16 zero | [0x0000] | UInt16 | 0 | Ok | 不变 | ✅ | — | — |
| A03 | Int16 negative | [0xFFFF] | Int16 | −1 | Ok | 不变 | ✅ | — | — |
| A04 | Int16 min | [0x8000] | Int16 | −32768 | Ok | 不变 | ✅ | — | — |
| A05 | Hex | [0x1234] | Hex | 0x1234 | Ok | 不变 | ✅ | — | — |
| A06 | Binary | [0x0001] | Binary | 0b0000000000000001 | Ok | 不变 | ✅ | — | — |
| A07 | UInt32 big-endian | [0x1234, 0x5678] | UInt32 + big-endian | 305419896（0x12345678） | Ok | 不变 | ✅ | — | — |
| A08 | UInt32 little-endian | [0x1234, 0x5678] | UInt32 + little-endian | 1450709556（0x56781234） | Ok | 不变 | ✅ | — | — |
| A09 | Int32 negative | [0xFFFF, 0xFF38] | Int32 + big-endian | −200 | Ok | 不变 | ✅ | — | — |
| A10 | Int32 min | [0x8000, 0x0000] | Int32 + big-endian | −2147483648 | Ok | 不变 | ✅ | — | — |
| A11 | Float32 1.0 | [0x3F80, 0x0000] | Float32 + big-endian | 1.0 | Ok | 不变 | ✅ | — | — |
| A12 | Float32 zero | [0x0000, 0x0000] | Float32 + big-endian | 0.0 | Ok | 不变 | ✅ | — | — |
| A13 | Float32 −2.0 | [0xC000, 0x0000] | Float32 + big-endian | −2.0 | Ok | 不变 | ✅ | — | — |
| A14 | Float32 NaN | [0x7FC0, 0x0000] | Float32 + big-endian | NaN（如实显示） | Ok | 不变 | ✅ | — | — |
| A15 | Float32 +Inf | [0x7F80, 0x0000] | Float32 + big-endian | Inf（如实显示） | Ok | 不变 | ✅ | — | — |
| A16 | Float32 word order 反转 | [0x0000, 0x3F80] | Float32 + little-endian | 1.0 | Ok | 不变 | ✅ | — | — |
| A17 | 字数不足 | [0x1234]（末寄存器） | UInt32 | — | InsufficientWords | **不变、仍 Success** | ✅ | — | — |
| A18 | 范围外选择 | [0x1234] | 起点第 2 字的 UInt16 | — | OutOfRangeSelection | 不变、仍 Success | ✅ | — | — |
| A19 | 非法配置 | [0x1234] | 数量 0 | — | InvalidConfiguration | 不变、仍 Success | ✅ | — | — |
| A20 | raw 保全（解码后） | 任意 | 任意 | 任意 | Ok | **DEC/HEX 与解码前逐字节一致** | ✅ | ✅（抽查） | — |
| A21 | 来源：FC03 | FC03 成功读取 | 任意 v1 类型 | 同 A01–A16 | Ok | 不变 | ✅ | — | — |
| A22 | 来源：FC04 | FC04（register-read-compatible）成功 | 同上 | 同左 | Ok | 不变 | ✅ | — | — |
| A23 | 来源：custom FC | 自定义 FC（如 0x41）成功 | 同上 | 同左 | Ok | 不变 | ✅ | — | — |
| A24 | 非成功不解码 | Timeout / Exception 等 | 任意 | 无解码区 | —（无 decode 资格） | 既有 READ-RX-5 行为 | ✅ | — | — |
| A25 | 1000×700 | — | — | — | — | 新 UI 在 1000×700 可用、写区 C4 仍 PASS | ✅（真实 QPA） | ✅ | — |
| A26 | M10 回归 | — | — | — | — | §15 全清单 | ✅ | ✅ | — |
| A27 | **OPTIONAL / SUPPLEMENTARY REAL-HARDWARE EVIDENCE（非 canonical mandatory gate）** | 真实设备读取 | 任意 | 真实值 | Ok | 不变 | — | ✅ | OPTIONAL / SUPPLEMENTARY |
| A28 | UInt16 byte-swap（寄存器内） | [0x1234] | UInt16 + byte order = swapped | 13330（0x3412） | Ok | 不变 | ✅ | — | — |
| A29 | Int16 byte-swap（寄存器内） | [0x0080] | Int16 + byte order = swapped | −32768（0x8000） | Ok | 不变 | ✅ | — | — |
| A30 | Float32 byte-swap + big-endian word | [0x803F, 0x0000] | Float32 + byte order = swapped, word = big-endian | 1.0 | Ok | 不变 | ✅ | — | — |
| A31 | Float32 byte-swap + little-endian word（工业 DCBA 全反序） | [0x0000, 0x803F] | Float32 + byte order = swapped, word = little-endian | 1.0 | Ok | 不变 | ✅ | — | — |

（A28–A31 为 clarification round 新增：canonical §M11 逐字「含 byte/word order」同时点名两轴，byte order 与 word order 均为 REQUIRED；全部 byte-order 用例断言 raw DEC/HEX 逐字节不变。）

## 17. Implementation entry gate

1. 本契约经 **Human Review PASS**（scope / v1 边界 / acceptance matrix / truth architecture 逐项裁定；〔待 Review〕标注项——尤其解码默认视图与 UI 位置细节——由 Human 裁定）。
2. 另起 implementation 轮：**先**重新 source audit（`TransactionAnalysis.values` 消费链、Read Result 呈现链、Communication 布局预算）**→ RED 证据 → 最小 implementation slice**（建议首切片 = 纯 core 解码函数 + 单元矩阵 A01–A20；UI 集成另切片）——**不是一次性实现整个 M11**。
3. 每片独立 behavior-bearing commit + full regression + 停轮 Human Review。

## 18. Exit criteria（provenance 已标注：A = PRE-EXISTING REPO REQUIREMENT / B = DERIVED FROM ACCEPTED M10 REGRESSION BASELINE / C = NEW T024 CONTRACT REQUIREMENT，subject to Human Review）

| exit criterion | 来源类型 |
| --- | --- |
| REQUIRED 解码矩阵全绿（A01–A26、A28–A31） | **C**（T024 新定义） |
| full Debug / Release regression 0 失败 | **A**（AGENTS.md 工作纪律 3 + V2 Validation Rules：改动必须构建 + 全量测试） |
| QML 诊断 0/0/0（ReferenceError / TypeError / Unable to assign） | **B**（M10-F / Read Correction 已验收基线） |
| 1000×700 可用（真实 windows QPA） | **B**（M10-F ISSUE-016/018 修正基线） |
| §15 M10 回归清单无回归 | **B**（由 accepted M10 baseline 派生） |
| raw truth 未被改写的直接证明（A20 + wire result 不变性） | **B/C**（原则源自 T023 M11-B3/B4 + READ-RX-5 既有冻结【B】；A20 具体化 = T024【C】） |
| 文档归档（T024 + 状态文档） | **A**（AGENTS.md 档案区规则） |
| 独立 behavior-bearing commit | **A**（AGENTS.md Git 治理 + V2 protocol） |
| Human Review | **A**（AGENTS.md V2 protocol / Human Review 环节） |
| packaging / portable Final D | **未冻结** —— repo 对 M11 packaging 无明文（本轮复查确认）；是否必需由 Human 在 acceptance 阶段裁定，**不是本契约的 exit criterion** |
| real hardware | **非 exit criterion** —— T024 新定义的 **supplementary / optional evidence**（【C】，subject to Human Review；A27 不阻塞 exit） |

**REAL HARDWARE 不阻塞 exit**（见 §13 来源定性：T024 新定义，非 §ZE14 自动继承）。

## 19. Problems Encountered / 待 Human 裁定

- 本轮无实现问题（docs-only）。
- **〔已全部裁定 — 2026-09-25 Human Review PASS，裁定冻结见 §22〕**：① 解码视图默认状态；② 解码控件落位；③ acceptance matrix A14/A15 的 NaN/Inf 显示文案。（原第 3 项「byte-swap-within-register 是否转 REQUIRED」已由 canonical 重读解决：§M11 逐字「含 byte/word order」⇒ 双轴 REQUIRED，见 §4 R2 / §8 / §21。）
- **同步台账要点（§N）**：16 项 handoff 声明中 15 项 VERIFIED_FROM_REPO（HEAD/LKGC/M10 states/M11 authorization/values 权威/FC04+custom/decode 非 FC03-only/排除项归属/无既有 M11 任务文档均实证；「M11 已获 START AUTHORIZATION」来源 = Human 原话，属授权事实而非 repo 内容）；1 项 UNKNOWN → 已按 §AB/§AC 处理为「repo silent ⇒ 不升级」（M11 的 hardware/package 硬门要求）。无 CONTRADICTED。
- 教训（§C 编辑安全）：本轮全程使用精确锚点小步编辑 + 全量 diff 审查，未使用模糊替换。

## 20. Git Commit

`M11: define decode acceptance contract`（docs-only；不 amend `fbb59b3`；不 rebase；不 push；不 tag；verified LKGC 保持 `352b81c82d5efa9aac5418cccaaf1605a68cd9d3`）。

## 21. Clarification Round log（2026-09-25，docs-only，commit `M11: clarify decode contract semantics`）

> Human Review 提出 4 项 clarification（A/B/C/D）。本轮逐项解决；主体契约不推翻。

### A. DecodeStatus cardinality

- 矛盾：正文曾写「decode failure model 四值」却列出 5 个状态。
- 裁定：**DecodeStatus 共 5 个** —— `Ok` = success；`InsufficientWords` / `OutOfRangeSelection` / `InvalidConfiguration` / `UnsupportedType` = **4 个 decode/configuration failure states**。未新增、未删除状态（§9 已改正）。

### B. byte order / word order canonical source

- 矛盾：上一轮报告同时声称「canonical 含 byte/word order」与「byte swap = DEFERRED（repo 未点名）」—— 二者冲突。
- 逐字重读 `docs/11_V2_UPGRADE_PLAN.md` §M11（字节级验证）：`… Float32（含 byte/word order）。` —— canonical **同时点名 byte order 与 word order 两个轴**。
- **CASE 1 适用**：byte order within register 与 word order across registers **均 REQUIRED**（§4 R2 / §7 / §8 / §10 / §16 A28–A31 已改正）；上一轮「byte swap = DEFERRED」的 DEFERRED 行**撤销**，相应报告表述更正为「canonical 原文确含 byte/word order，此前 DEFERRED 属转述错误」。术语纪律（§8）：`byte order`（寄存器内）/ `word order`（寄存器间）/ `endianness`（泛称）不得混用。

### C. M11 hardware policy 来源

- 事实：T022 §ZE14 标题为「M10-F / M11 boundary」，正文明确「M11 = Register Readout & Decode，继续 HOLD；不得把 M10 的 0x10 write 与 M11 的 register decode 混成一个阶段」，但其 **OPTIONAL 硬件条款写在 M10-F 上下文中**；repo **无**「§ZE14 治理 M11」或「M11 real hardware 为硬门」的明文。
- 裁定：T024 v1 的「real hardware 不作为 closure hard gate；如执行属 supplementary / optional evidence」= **T024 新定义（subject to Human Review）**，**非 §ZE14 自动跨 milestone 继承**（§13 / §18 已改正；上一轮「延续 §ZE14」措辞撤回）。
- A27 更名为 **OPTIONAL / SUPPLEMENTARY REAL-HARDWARE EVIDENCE（非 canonical mandatory gate）**。

### D. one-time resync ledger 完整闭环（16 项）

| 分类 | 数量 | 明细 |
| --- | --- | --- |
| VERIFIED_FROM_REPO | **13** | claims 1–6（HEAD / LKGC / M10 COMPLETE / M10-F CLOSED / Read Correction CLOSED / M11 曾 HOLD）、8（raw uint16 权威）、9（FC04 register-read-compatible）、10（custom FC）、11（decode 非 FC03-only 的事实基础 = 可编辑功能码 + 参数化分析器；规范表述即 T024 R3）、12（M10 排除项）、13（排除项归属为**部分指派**）、16（原无 M11 任务文档） |
| CONTEXT_ONLY / HUMAN_AUTHORIZED | **1** | claim 7（Human「开始 M11」—— 授权事实来自对话上下文；本轮已按 AF 写入状态文档） |
| UNKNOWN（resync 时）→ 处理 | **2** | claim 14（M11 hardware policy —— repo silent ⇒ T024 新定义，见 C）；claim 15（M11 package/portable policy —— repo silent ⇒ **保持未冻结**，由 Human 在 acceptance 裁定） |
| CONTRADICTED | **0** | — |
| **合计** | **16** | 13 + 1 + 2 = 16 |

（上一轮报告「15 VERIFIED + 1 UNKNOWN」的口径错误在此更正：claim 7 应归 HUMAN_AUTHORIZED，claims 14/15 应分别计 UNKNOWN。）

### E. Exit-criteria provenance

见 §18 表：A = pre-existing repo requirement（AGENTS 治理与纪律）；B = derived from accepted M10/T023 baseline；C = new T024 requirement（subject to Human Review）。**packaging / portable 对 M11 仍无 repo 明文 —— 保持未冻结，未偷偷新增**；real hardware 为 C 级 supplementary evidence，非 exit criterion。

### F. 本轮验证边界

docs-only：未 build / 未 test / 未 package；未修改 src / tests / CMakeLists.txt / scripts / assets / samples；未 push / 未 tag / 未 amend；verified LKGC 保持 `352b81c82d5efa9aac5418cccaaf1605a68cd9d3`。

## 22. Human Review Resolution & Implementation Entry（2026-09-25，docs-only freeze）

> **T024 Human Review = PASS。** Human 对 4 项〔待 Review〕/clarification 事项明确裁决（原文：「可以，开始吧，我先看看效果」）⇒ **implementation entry gate = OPEN；M11 implementation = AUTHORIZED（FIRST SLICE STARTING）**。以下裁定自本节起为**冻结契约**（不再是〔待 Review〕）：

### C1. 默认解析类型 = UInt16（无符号16位整数）

- 最接近 M10 已验收的 canonical raw uint16 register truth；不默认引入 signed / 32-bit / float 语义假设。
- **不代表只支持 UInt16**：M11 v1 仍按 §4 REQUIRED 类型全集实现。

### C2. Decode 控件落位 = 现有 Read Result 详情区域 / 详情对话框

- **不得**向 Communication 主页面新增一整排 decode controls（保护 1000×700，避免复现 M10 纵向 geometry regression）。
- 详情区控件文案（冻结）：`解析类型`（默认 无符号16位整数）、`寄存器内字节顺序`（默认 正常）、`32位寄存器顺序`（**仅** UInt32 / Int32 / Float32 等多寄存器类型时有意义）。
- **第一 implementation slice 不实现 32-bit 类型**，因此本轮**不得**为「看起来完整」提前提供未经测试的 32-bit word-order 控件（如需预留必须 disabled + 明确不可用，优先最小 UI）。

### C3. Float32 特殊值显示（未来 slice 冻结行为，本 slice 不实现）

- NaN / +Infinity / −Infinity 都是合法 IEEE-754 binary32 结果 ⇒ `DecodeStatus = Ok`；**不得**映射为通信失败 / CRC 错误 / 响应格式错误 / Timeout / TransportError / ModbusException / decode failure。
- 中文 UI 文案冻结：**非数字（NaN）**、**正无穷大（+Inf）**、**负无穷大（−Inf）**。

### C4. Raw 与 Decoded 明确分离

- 永远区分**原始值**与**解析后的派生值**：设备 canonical raw word `0x1234` 在用户选择寄存器内字节交换后，raw 仍必须是 `0x1234`，derived decode 可为 `0x3412`；UI **不得**把 `0x3412` 冒充设备实际返回值。
- 中文概念命名：**原始十六进制 / 解析后十六进制**；**原始十进制 / 解析结果**。Raw 列永远保留。

### Implementation entry

- **entry gate = OPEN**。第一切片范围 = T024 §H/§I/§J/§K/§L/§M/§N（单寄存器可视垂直切片：Hex/Binary/UInt16/Int16 + 寄存器内 byte order + 既有 Read Result 详情集成 + raw/decoded 分离 + 默认 UInt16 + FC03/FC04/custom 不被功能码硬编码排除 + deterministic tests）。
- 本节冻结**不**构成：M11 COMPLETE / acceptance complete / LKGC advanced / push / tag / release 授权。

## 23. First Implementation Slice — Single-Register Decode View（2026-09-25，behavior-bearing）

> **T025（M11 First Slice）实现完成，等待 Human 查看效果（非 Final D / 非 canonical package / 非 LKGC）。**
> 范围 = §22 授权的**单寄存器可视垂直切片**；32-bit / Float32 / word order / scaling 等留待后续切片。

### W0. Docs freeze 与 entry gate

```text
docs freeze commit = 2630909（M11: freeze decode UI defaults；docs-only）。
Human Review = PASS（4 项裁定冻结于 §22）；entry gate = OPEN。
```

### W1. Source audit（§F 十问，全部 current tree 实证）

```text
F1  canonical raw words owner = TransactionAnalysis.values（RAW ONLY，append-last，
    Success 才携带；AnalysisController 经 ReadResultSnapshot.analysis 持有）。
F2  QML 的 raw DEC/HEX 来自 controller 的 readResultValues() 行投影
    （index / address DEC+HEX / value DEC+HEX），不是 QML 自行解码。
F3  Read Result 详情数据来自 ReadResultSnapshot（唯一写入点 = 3 个 capture helper）
    + 28→30 个只读投影属性。
F4  本轮新增的纯解码层 = core/analysis/RegisterDecode.{h,cpp}（Zero Qt，std::string
    输出沿用 core 惯例）——derived presentation result 的 owner。
F5  解码层位置 = core：输入只有 canonical raw words + decode configuration；
    不见 RTU 字节 / 不解析 CRC / 不解析 byteCount / 不判定事务结果 ⇒
    不形成第二套 wire truth，QML 也不做任何位运算/字节交换/重解释。
F6  decode configuration = 派生呈现状态（readDecodeType / readDecodeByteOrder，
    Q_PROPERTY WRITE + readResultChanged），**不是** transaction wire state；
    越界写入被忽略（UI bug 不能把配置置为不可表达状态）。
F7  TransactionAnalysis **未修改** —— decode 不写回 canonical raw 权威（§12）。
F8  FC03 / FC04 / custom 经同一条 canonical 路径：captureReadResultFromRecord 从
    intent payload 读取 functionCode（T023 Part C），analyzer 按 request frame
    参数化 ⇒ 解码资格判定处**没有任何 function==0x03 硬编码**。
F9/F10 详情 UI = readResultDialog（720×520 上限 + 有界 ScrollView）；解码控件置于
    值表上方（对话框内部），主页面几何不受影响。
```

### W2. 实现

```text
core/analysis/RegisterDecode.{h,cpp}：
  RegisterDecodeType{Hex=0, Binary=1, UInt16=2, Int16=3}（int 值 = QML combo 索引）
  RegisterByteOrder{Normal=0, ByteSwapped=1}
  RegisterDecodeStatus{Ok, InsufficientWords, OutOfRangeSelection,
                       InvalidConfiguration, UnsupportedType}（T024 §9 全集）
  decodeEffectiveWord(raw, byteOrder) / decodeRegisterWord(raw, type, byteOrder)
  registerDecodeStatusName()（machine tokens）
  Hex 格式复用 raw HEX 惯例（0x + 大写 4 位）；Binary 固定 16 位宽；
  Int16 用显式阈值两补码（-0x10000），无 implementation-defined cast；
  矩阵外类型 ⇒ UnsupportedType + 空文本（绝不猜测）。
AnalysisController：
  readDecodeType / readDecodeByteOrder（Q_PROPERTY WRITE，越界忽略，默认 = UInt16 + Normal）
  readResultValues() 行投影新增 derived 列：decoded（Ok 才有文本）+ decodeStatus（machine token）。
QML（CommunicationPage）：
  readResultDialog 值表上方新增 readDecodeControls（解析类型 / 寄存器内字节顺序 两个
  ComboBox，Accessible.name 齐备，索引直接镜像 core 枚举）；
  值表 delegate 新增「解析 %6」列（Ok 显示派生值，非 Ok 显示 无法解析）；
  控件置于**对话框内部**（主页面几何零改动）。
```

### W3. 测试与门禁（真实数量）

```text
register_decode（新目标）：19 passed —— UInt16/Int16/Hex/Binary × normal/byte-swapped、
  边界（0 / max / 0x8000）、raw 保全（r1/r2：独立推导 helper 交叉验证）、
  UnsupportedType（enum 99）、status tokens、swap 对合性。
ui_bridge：88 passed（+4 = READ-D1 默认配置 / D2 配置跟随 / D3 raw 列零变化 /
  D4 FC03+FC04+custom(0x41) 三来源全部可解码）。
Debug ctest **39/39 PASS**、Release ctest **39/39 PASS**（38 → 39，新增 register_decode）。
QML gates：read-result（含 M6–M9）/ write-foundation / focus / smoke / nav / geometry 全部 exit 0。
诊断 0/0/0。新增代码零 warning（src/main.cpp 5 条 pre-existing 未动）。
```

### W4. RCA

```text
RCA-9（M9 前置状态）：gate M8 把解码类型留在 Hex，M9 未先恢复 UInt16 ⇒ 读到 Hex 视图的
  0x3412。产品行为正确（byte order 只作用于该视图内），是**测试前置状态缺陷** ⇒
  M9 先恢复 UInt16 再测 byte order。
RCA-10（d2 期望值）：raw word 100（0x0064）byte-swapped = 0x6400 = 25600；初稿误写 256。
RCA-11（d4 功能码文本）：功能码字段须传**两位十六进制文本**（"03"/"04"/"41"），单字节
  char 会成为不可打印字符被 core parser 拒绝 —— 与 UI 真实输入形式一致后三来源全部通过。
```

### W5. Human visual candidate

```text
path = build/release/ModbusLens.exe（source-tree Release，非 Final D / 非 canonical package）
size = 4476794 bytes
SHA-256 = BEB295BC65B39A486ABC890C7D813DD53C9E83FE20DE68FC2D6D813DC6865128
用途 = M11 first-slice Human visual candidate（查看解析控件 / 默认 UInt16 /
  十六进制·二进制·有符号16位 切换 / 正常·字节交换 切换 / raw 列恒不变 / 1000×700）。
```

### W6. Files / Commit

```text
新增：src/core/analysis/RegisterDecode.{h,cpp}、tests/test_register_decode.cpp
修改：src/core/analysis（CMake 注册）、src/ui/AnalysisController.{h,cpp}、
      src/ui/qml/pages/CommunicationPage.qml、src/main.cpp（gate M6–M9 + summary）、
      CMakeLists.txt（register_decode 目标）、tests/test_ui_bridge.cpp、docs（本节 + 状态文档）
commit：`M11: add single-register decode view`（behavior-bearing；NO AMEND；不 push；不 tag）
verified LKGC 保持 `352b81c82d5efa9aac5418cccaaf1605a68cd9d3`。
```
__zcode_status=$?
if [ "$__zcode_status" -eq 0 ]; then pwd -P > '/c/Users/付/AppData/Local/Temp/zcode-f3915b2f-6bef-4044-84c8-d4bf24de3f12-cwd'; fi
exit "$__zcode_status"

## 23. First Implementation Slice — Single-Register Decode View（2026-09-25，behavior-bearing）

> **T025（M11 First Slice）实现完成，等待 Human 查看效果（非 Final D / 非 canonical package / 非 LKGC）。**
> 范围 = §22 授权的**单寄存器可视垂直切片**；32-bit / Float32 / word order / scaling 等留待后续切片。

### W0. Docs freeze 与 entry gate

```text
docs freeze commit = 2630909（M11: freeze decode UI defaults；docs-only）。
Human Review = PASS（4 项裁定冻结于 §22）；entry gate = OPEN。
```

### W1. Source audit（§F 十问，全部 current tree 实证）

```text
F1  canonical raw words owner = TransactionAnalysis.values（RAW ONLY，append-last，
    Success 才携带；AnalysisController 经 ReadResultSnapshot.analysis 持有）。
F2  QML 的 raw DEC/HEX 来自 controller 的 readResultValues() 行投影
    （index / address DEC+HEX / value DEC+HEX），不是 QML 自行解码。
F3  Read Result 详情数据来自 ReadResultSnapshot（唯一写入点 = 3 个 capture helper）
    + 30 个只读投影属性。
F4  本轮新增的纯解码层 = core/analysis/RegisterDecode.{h,cpp}（Zero Qt，std::string
    输出沿用 core 惯例）——derived presentation result 的 owner。
F5  解码层位置 = core：输入只有 canonical raw words + decode configuration；
    不见 RTU 字节 / 不解析 CRC / 不解析 byteCount / 不判定事务结果 ⇒
    不形成第二套 wire truth，QML 也不做任何位运算/字节交换/重解释。
F6  decode configuration = 派生呈现状态（readDecodeType / readDecodeByteOrder，
    Q_PROPERTY WRITE + readResultChanged），**不是** transaction wire state；
    越界写入被忽略（UI bug 不能把配置置为不可表达状态）。
F7  TransactionAnalysis **未修改** —— decode 不写回 canonical raw 权威（§12）。
F8  FC03 / FC04 / custom 经同一条 canonical 路径：captureReadResultFromRecord 从
    intent payload 读取 functionCode（T023 Part C），analyzer 按 request frame
    参数化 ⇒ 解码资格判定处**没有任何 function==0x03 硬编码**。
F9/F10 详情 UI = readResultDialog（720×520 上限 + 有界 ScrollView）；解码控件置于
    值表上方（对话框内部），主页面几何不受影响。
```

### W2. 实现

```text
core/analysis/RegisterDecode.{h,cpp}：
  RegisterDecodeType{Hex=0, Binary=1, UInt16=2, Int16=3}（int 值 = QML combo 索引）
  RegisterByteOrder{Normal=0, ByteSwapped=1}
  RegisterDecodeStatus{Ok, InsufficientWords, OutOfRangeSelection,
                       InvalidConfiguration, UnsupportedType}（T024 §9 全集）
  decodeEffectiveWord(raw, byteOrder) / decodeRegisterWord(raw, type, byteOrder)
  registerDecodeStatusName()（machine tokens）
  Hex 格式复用 raw HEX 惯例（0x + 大写 4 位）；Binary 固定 16 位宽；
  Int16 用显式阈值两补码（-0x10000），无 implementation-defined cast；
  矩阵外类型 ⇒ UnsupportedType + 空文本（绝不猜测）。
AnalysisController：
  readDecodeType / readDecodeByteOrder（Q_PROPERTY WRITE，越界忽略，默认 = UInt16 + Normal）
  readResultValues() 行投影新增 derived 列：decoded（Ok 才有文本）+ decodeStatus（machine token）。
QML（CommunicationPage）：
  readResultDialog 值表上方新增 readDecodeControls（解析类型 / 寄存器内字节顺序 两个
  ComboBox，Accessible.name 齐备，索引直接镜像 core 枚举）；
  值表 delegate 新增「解析 %6」列（Ok 显示派生值，非 Ok 显示 无法解析）；
  控件置于**对话框内部**（主页面几何零改动）。
```

### W3. 测试与门禁（真实数量）

```text
register_decode（新目标）：19 passed —— UInt16/Int16/Hex/Binary × normal/byte-swapped、
  边界（0 / max / 0x8000）、raw 保全（r1/r2：独立推导 helper 交叉验证）、
  UnsupportedType（enum 99）、status tokens、swap 对合性。
ui_bridge：88 passed（+4 = READ-D1 默认配置 / D2 配置跟随 / D3 raw 列零变化 /
  D4 FC03+FC04+custom(0x41) 三来源全部可解码）。
Debug ctest **39/39 PASS**、Release ctest **39/39 PASS**（38 → 39，新增 register_decode）。
QML gates：read-result（含 M6–M9）/ write-foundation / focus / smoke / nav / geometry 全部 exit 0。
诊断 0/0/0。新增代码零 warning（src/main.cpp 5 条 pre-existing 未动）。
```

### W4. RCA

```text
RCA-9（M9 前置状态）：gate M8 把解码类型留在 Hex，M9 未先恢复 UInt16 ⇒ 读到 Hex 视图的
  0x3412。产品行为正确（byte order 只作用于该视图内），是**测试前置状态缺陷** ⇒
  M9 先恢复 UInt16 再测 byte order。
RCA-10（d2 期望值）：raw word 100（0x0064）byte-swapped = 0x6400 = 25600；初稿误写 256。
RCA-11（d4 功能码文本）：功能码字段须传**两位十六进制文本**（"03"/"04"/"41"），单字节
  char 会成为不可打印字符被 core parser 拒绝 —— 与 UI 真实输入形式一致后三来源全部通过。
```

### W5. Human visual candidate

```text
path = build/release/ModbusLens.exe（source-tree Release，非 Final D / 非 canonical package）
size = 4476794 bytes
SHA-256 = BEB295BC65B39A486ABC890C7D813DD53C9E83FE20DE68FC2D6D813DC6865128
用途 = M11 first-slice Human visual candidate（查看解析控件 / 默认 UInt16 /
  十六进制·二进制·有符号16位 切换 / 正常·字节交换 切换 / raw 列恒不变 / 1000×700）。
```

### W6. Files / Commit

```text
新增：src/core/analysis/RegisterDecode.{h,cpp}、tests/test_register_decode.cpp
修改：src/ui/AnalysisController.{h,cpp}、src/ui/qml/pages/CommunicationPage.qml、
      src/main.cpp（gate M6–M9 + summary）、CMakeLists.txt（RegisterDecode.cpp +
      register_decode 目标）、tests/test_ui_bridge.cpp、docs（本节 + 状态文档）
commit：`M11: add single-register decode view`（behavior-bearing；NO AMEND；不 push；不 tag）
verified LKGC 保持 `352b81c82d5efa9aac5418cccaaf1605a68cd9d3`。
```

### W7. Visual Candidate Superseded — Runtime Deployment RCA & Self-Contained Staging（2026-09-25，docs-only 更正）

> **Human 实测失败（failure evidence，非 UI FAIL）**：双击 `build
elease\ModbusLens.exe` 无法启动，
> 三个 Windows loader 错误 —— `_ZSt28__throw_bad_array_new_lengthv` / `_ZSt21__glibcxx_assert_failPKciS0_S0_`
> （涉及 `D:\QT.11.1\mingw_64in\Qt6Qml.dll`）、`_ZNSt3pmr20get_default_resourceEv`（涉及
> `D:\QT.11.1\mingw_64in\Qt6Gui.dll`）。Human **尚未进入 UI**，故不构成 UI FAIL。
> 上节 W5 的「visual candidate = build/release/ModbusLens.exe」**由本节更正为 INVALID / NOT LAUNCHABLE**。

**RCA（全部实测证据）**：

```text
工具链（CMakeCache 实证）：CMAKE_CXX_COMPILER = D:/QT/Tools/mingw1310_64/bin/g++.exe
  （GCC 13.1.0，MinGW-Builds）；Qt6_DIR = D:/QT/6.11.1/mingw_64；Release。
build
elease：**无任何运行时 DLL**（非 deployed tree）。
exe 导入表：libstdc++-6.dll / libgcc_s_seh-1.dll + Qt6{Core,Gui,Qml,Quick,QuickControls2,
  SerialPort,Network}.dll；Qt6Qml.dll 导入 2 个、Qt6Gui.dll 导入全部 3 个缺失符号。
PATH 候选（where.exe）：D:\Git\mingw64in、D:\mingw64in、D:\QT.11.1\mingw_64in
  —— **匹配工具链 D:\QT\Tools\mingw1310_64in 不在 ambient PATH 上**。
符号比对：D:\Git\mingw64（新 runtime）与 D:\QT.11.1\mingw_64in（2,243,072 B）
  **均有**三符号；**D:\mingw64in（1,420,800 B，GCC 8/9 时代）三符号全缺**。
受控复现：PATH = mingw64in 先于 Qt bin ⇒ **exit 127（精确复现 Human 三错误）**；
  PATH = 匹配工具链 + Qt bin ⇒ exit 0。
```

**分类 = B（ambient PATH 上存在不匹配的旧 libstdc++-6.dll）+ A（build
elease 非 deployed tree）**；
**非 M11 decode 产品代码缺陷；b95de54 无需回滚。**

**Staging（self-contained，非 canonical package / 非 Final D / 非 LKGC）**：

```text
path   = build/m11-visual-candidate/（独立、可删除；build/release 未被污染）
exe    = 4,476,794 B  SHA-256 BEB295BC65B39A486ABC890C7D813DD53C9E83FE20DE68FC2D6D813DC6865128
         （与源候选 byte-identical）
部署    = D:\QT.11.1\mingw_64in\windeployqt.exe（qtpaths --qt-version = 6.11.1 实证）
         --release --compiler-runtime --qmldir src/ui/qml；进程级 PATH 显式使用
         匹配工具链（未改系统 PATH；未继承 WorkBuddy blocker）
runtime：libstdc++-6.dll 2,243,072 B / libgcc_s_seh-1.dll 109,056 B /
         libwinpthread-1.dll 53,248 B —— 全部与验证工具链 byte-identical
QML     = qml/（QtQuick/QtQml/Qt/… 全量 imports）+ platforms/qwindows.dll +
         ModbusLens/（**qt_add_qml_module 文件系统模块目录** —— windeployqt 只部署
         imports，不部署 app 自身模块目录；缺失即
         "Module ModbusLens contains no type named Main"，本节发现并补齐）
```

**Clean-env 验证（PATH = System32;Windows，无任何 Qt/MinGW）**：smoke exit 0；
`--qml-read-result-check` PASS（M6–M9 全绿，诊断 0/0/0）；`--qml-production-write-check`
PASS（**R15/R16/R17 全在**，诊断 0/0/0）。无「无法定位程序输入点」/ missing DLL /
platform plugin failure。

**新发现的既有缺陷（本轮不修，报告待裁）**：`CommunicationPage.qml:333`
`commStartHexEcho` 的 `.arg(a, b)` 双参形式在 QML 引擎报
`String.arg(): Invalid arguments`（自 `352b81c` T023 Part C 起存在于每次启动）；
诊断三分类 0/0/0 未覆盖该 Error 类别。建议后续 correction 轮改为链式 `.arg()`。

### W8. Correction — commStartHexEcho `.arg(a, b)` 运行时错误 + 门禁回归保护（behavior commit）

> **Human Review 前置 blocker（§0–§4 确认）**：clean-env staging smoke 输出
> `CommunicationPage.qml:333: Error: String.arg(): Invalid arguments`。逐字源文本（§3 七问）：
> 表达式 = `qsTr("起始地址 HEX %1 ｜ %2").arg(previewStartHex, previewFunctionLabel)`（2 个占位符、
> 2 个 string 参数）；求值时机 = **CommunicationPage 组件创建时**（text 绑定急切求值，与 visible 无关）；
> **production-reachable = YES**（正常页面加载即触发；非 test-only synthetic state）。
> 用户可见后果：`commStartHexEcho` 标签（起始地址 HEX 行）绑定失败、文本不渲染。
> **根因**：QML 引擎的 `String.arg()` 仅支持**单参**调用（QTBUG-63263 一族）；
> 多参形式在 Qt 6.11.1 运行时抛 `Invalid arguments`。
> **修复（最小）**：改为链式 `.arg(a).arg(b)`（两参值均不含 %n，替换顺序无污染）；
> 用户文案语义不变。

**回归保护（§6）**：现有 ctest 诊断三分类（ReferenceError / TypeError / Unable to assign）
通过 `FAIL_REGULAR_EXPRESSION` 实现；本轮把 `String.arg..: Invalid arguments`
（无转义依赖的正则，两个 `.` 匹配字面括号）加入**同一**属性组（8 个 QML 门禁测试全部生效）。
**负向对照（§7）**：临时恢复 `.arg(a, b)` 双参形式并重建 ⇒
直接运行 gate 出现 **13 条** String.arg 错误；`ctest -R qml_read_result_check`
**FAIL**（38 Failed / regex 命中）⇒ 还原修复后同一测试 **PASS**、错误计数 **0**。
（教训：CMake 双引号串会把 `\(\)` 消费成 `()`，正则改用 escape-free 形式。）


### W9. RCA 证据语义收紧 + staging 刷新（docs-only）

**措辞更正（§11）**：上一节（W7）对 `D:\mingw64\bin\libstdc++-6.dll` 的描述收紧为——
该 stale runtime 是**与 Human 三个 loader 错误完全吻合、且已在受控复现中（PATH 优先序）
精确复现同一失败）的 offending candidate**；**Human 当次 Explorer 启动的 loaded-module path
未被直接捕获**（无 loaded-module trace），故不断言它就是当次实际加载的那一份。

**staging 刷新**：W8 行为修复改变 exe ⇒ 旧 staging（exe SHA BEB295BC…5128）**STALE 已废弃**；
按 §13/§14 同法重新部署：新 exe = `build/m11-visual-candidate/ModbusLens.exe`
（4,476,794 B，SHA-256 `5D38EB094AA6C3CEFC6F13C73BC62F7C606CE4A27AF9693224E03512BC1DD7EC`，
source == staging byte-identical）；三个 MinGW runtime 仍与 GCC 13.1.0 工具链
byte-identical；clean-env（PATH=System32;Windows）smoke / read-result（M6–M9）/
production-write（R15/R16/R17）全部 exit 0 且 **String.arg errors = 0、0/0/0**。

## 24. Demo Harness — First-Slice Human Visual Demo（2026-09-25，behavior-bearing）

> **Human 无真实 Modbus 设备**，无法触发真实 FC03 成功读取来查看 M11 解码 UI。
> 本节新增一个 **TEST-ONLY / DEMO-ONLY** 隐藏 CLI 入口 `--qml-read-result-demo`，
> 通过与 `--qml-read-result-check` 相同的 production request → session → analyzer →
> ReadResultSnapshot 路径注入一个**固定合成 FC03 成功**，然后保持 GUI 打开供 Human 操作
> M11 解码控件。**不打开真实串口；不产生真实 serial I/O；synthetic success ≠ real hardware evidence。**

### X1. Source audit（§4 七问，全部 current tree 实证）

```
Q1 现有 read-result gate 怎样制造 Success transaction？
   scanRead（setCompleteReadImmediately(false) + readHoldingRegistersOnce）→
   transport->completeReadWithBytes(responseWith(unit, values, fc), 25) →
   production session/analyzer/Snapshot 全链路（0 bypass）。
Q2 复用路径：demo 调用 controller->readHoldingRegistersOnce（production dispatch）
   + transport->completeReadWithBytes（production byte injection）。
Q3 demo 不直接赋值 decoded result：decode 由 readResultValues() 的行构造
   从 TransactionAnalysis.values 派生（M11 已有代码），demo 只触发读取。
Q4 demo 不重新解析 RTU bytes：response 由 encodeRtuFrame 构建（含 CRC），
   session/analyzer 解析；QML 无位运算。
Q5 demo 不打开真实 COM：transport = HarnessWriteTransport（harness double），
   connectSerial("COM_DEMO_HARNESS", 9600) 走 seam 不触碰真实串口。
```

### X2. Demo dataset 与期望值

```
Slave=1  Function=03  Start=1000  Quantity=3  Timeout=1000 ms
Raw words = 0x1234 / 0xFFFF / 0x0080
UInt16 Normal:  4660 / 65535 / 128
UInt16 Swapped: 13330 / 65535 / 128（0x3412 / 0xFFFF / 0x8000）
Int16 Normal:   4660 / -1 / 128
Int16 Swapped:  13330 / -1 / -32768（0x8000 两补码）
Hex Normal:     0x1234 / 0xFFFF / 0x0080
Hex Swapped:    0x3412 / 0xFFFF / 0x8000
Binary Normal:  0b0001001000110100 / 0b1111111111111111 / 0b0000000010000000
```

### X3. Demo harness 实现

```
入口：main.cpp 新增 runReadResultDemo(engine, app) + dispatch --qml-read-result-demo
      （紧跟 --qml-read-result-check 的 dispatch 之后）。
辅助：本地 clickNamed lambda（同 gate 模式，用 findNamedItem + qobject_cast）；
      本地 responseWith lambda（同 gate 模式，用 SHIPPED encodeRtuFrame）。
阶段：
  0  navigate to Communication workspace（clickNamed("navItem_2")）；
  1  controller->readHoldingRegistersOnce(1, 1000, 3, 1000)（production dispatch）；
  2  transport->completeReadWithBytes(responseWith(1, {0x1234,0xFFFF,0x0080}, 0x03), 25)
     （production byte injection → session/analyzer → Snapshot）；
  3  DEMODECODE assertions + demo title + keep-or-exit。
     断言：ReadSuccess；values count=3；raw words = 0x1234/0xFFFF/0x0080；
           readDecodeType=UInt16；readDecodeByteOrder=Normal；
           readResultDetailsButton 可见（Human 可打开 M11 decode controls）。
     成功：window->setTitle("ModbusLens — M11 解码演示（模拟数据）")
           + note("DEMODECODE: READY: synthetic register-read success; "
                  "values=0x1234,0xFFFF,0x0080; no real serial I/O")
           + if (--demo-exit-after-ready) window->close() / app.exit(0)；
           else 保持 GUI 运行。
     失败：DEMODECODE FAIL + exit 1。
```

### X4. 自动化保护（§15）

```
ctest 新增 qml_read_result_demo 目标：
  COMMAND modbuslens --qml-read-result-demo --demo-exit-after-ready
  ENVIRONMENT offscreen + stderr console（同 QML 门禁组）
  FAIL_REGULAR_EXPRESSION 包含 DEMODECODE FAIL + 三诊断 + String.arg 模式
  ⇒ 39 → 40 tests。
负向完整性检查（§16）：临时把 dataset 0x1234 → 0x1235 ⇒ 重建 ⇒
  DEMODECODE FAIL: word 0: raw DEC=4661, expected 4660 ⇒ 还原。
  证明 demo 值来自 production path，不是 QML 写死。
```

### X5. 边界

```
· 正常启动行为不变（title 仍为 ModbusLens；demo 标题仅在 --qml-read-result-demo 路径设置）。
· 不新增正式产品按钮 / 菜单项 / 主页面 UI。
· 不打开任何真实 COM；不产生 FC06/FC16 真实写入。
· 32-bit / Float32 / word order / scaling 等仍属后续切片。
· REAL MODBUS HARDWARE = NOT VERIFIED（demo PASS ≠ real FC03 device PASS）。
· verified LKGC 保持 352b81c…（不推进）。
```

## 25. First-Slice Human Visual Acceptance Archive（2026-09-25，docs-only）

> **Human 对 M11 first-slice visual demo 的实际确认（逐项记录）：**
>
> 1. Demo 正常启动（`--qml-read-result-demo` → DEMODECODE READY → GUI 打开）。
> 2. Demo request / editor / preview / Actual TX **视觉一致**
>    （slave=1 / function=03 / start=1000 / quantity=3 / timeout=1000 ms）。
> 3. 默认解析类型 = **UInt16**、byte order = **Normal** —— 正确。
> 4. **Hex / Binary / UInt16 / Int16** 四种解析均正确。
> 5. **Normal / ByteSwapped** 切换正确。
> 6. **raw DEC / raw HEX** 在解析设置变化时保持不变。
> 7. **1000×700 正常**（Human 明确确认）。
> 8. **synthetic deterministic demo evidence** —— Human 未使用真实设备完成本轮
>    decode demo，该 PASS 不是新增 real-hardware evidence。

### 状态

```text
M11 first slice = ACCEPTED
M11 overall     = IN PROGRESS
M11 second slice = AUTHORIZED / STARTING
verified LKGC    = 352b81c82d5efa9aac5418cccaaf1605a68cd9d3（不变）
REAL HARDWARE    = NOT VERIFIED（不变）
```

### First-slice docs commit

本节归档 = docs-only commit（行为代码无改动；下一行为提交 = M11 second slice）。

## 26. Second Implementation Slice — 32-bit Decode Views + Word Order（2026-09-25，behavior-bearing）

> **M11 second slice 实现完成，等待 Human 查看效果（非 Final D / 非 canonical package / 非 LKGC）。**
> 行为提交 = `bc99e6e`（M11: add the 32-bit decode views and word order）；
> First-slice Human PASS 归档 = docs-only commit `bceb5a2`（§25，先于本切片实现提交）。

### W1. 2-register 对齐裁定（§4 audit 的回答，冻结）

- **裁定 = 滑动窗口逐行对齐（sliding window per row）**：对 2 寄存器类型，**第 i 行解码 words[i] 与 words[i+1]**；解码列与 raw 列保持 §10 的"逐寄存器对齐、共享同一 PDU 地址"；**最后一个寄存器单独报告 `InsufficientWords`**（§9 逐字「如末寄存器取 UInt32」的直接落地）。
- **备选被否**：固定不重叠配对（0+1/2+3）违背"每个寄存器行都可成为解码起点"的 §9 语义；独立 start-index 控件违背 §22 C2（1000×700 约束、最小 UI）。
- **地址范围可见性**：成功的 2 寄存器行新增 `decodeSpan` 字段（如 `1000-1001`），UI 以「范围 1000-1001」后缀呈现——Human 永远能看到派生值用了哪两个地址；1 寄存器类型不携带该字段；`InsufficientWords` 行不携带（没有任何字被消费）。
- **OutOfRangeSelection**：滑动窗口模型下 UI 永远不会产生该状态（行号天然在范围内）；它作为 core API 的防御状态保留（`decodeRegisterView(start >= size)`、空数据），由 core 测试直接冻结。

### W2. 实现（全部落在共享层，三种数据源共用）

- **core（`RegisterDecode.h/.cpp`，纯 C++20 零 Qt）**：`RegisterDecodeType` 扩展 `UInt32=4 / Int32=5 / Float32=6`（既有 Hex/Binary/UInt16/Int16 数值不变）；新增 `RegisterWordOrder{HighWordFirst=0, LowWordFirst=1}`（AB CD 默认 / CD AB）；`registerDecodeTypeWordCount()`（1/2/越界 0）；`registerWordOrderName()`（`high_word_first`/`low_word_first` token）；`decodeRegisterPair(w0, w1, type, byteOrder, wordOrder)` —— 冻结管线 = 寄存器内字节交换 → word order 组合 → 类型重解释；`decodeRegisterView(words, start, type, byteOrder, wordOrder)` —— §12 命名的单一入口，承载 §9 五状态完整映射（start<0 或类型越界 → `InvalidConfiguration`；start≥size → `OutOfRangeSelection`；起点在数据内但剩余字不足 → `InsufficientWords`）。Int32 two's complement 不依赖实现定义转换（与 Int16 同纪律）；Float32 经 memcpy 位重解释 + `std::to_chars`（规范保证 locale 无关、最短往返）。
- **controller（`AnalysisController`）**：`readDecodeType` 有效范围扩至 0..6（越界写仍忽略）；新增 `readDecodeWordOrder`（int，默认 0=HighWordFirst，越界写忽略）与只读 `readDecodeWordOrderEnabled`（= 类型消耗 2 寄存器，§22 C2 的控件使能门）；`readResultValues()` 按 word count 分流：**1 寄存器路径逐字节保持 first-slice 已验收代码不动**（回归保护），2 寄存器路径走 `decodeRegisterView` 滑动窗口并附加 `decodeSpan`。
- **QML（`CommunicationPage.qml` 详情对话框，未新增主页控件）**：解析类型下拉 = 7 项（新增 无符号32位整数 / 有符号32位整数 / 32位浮点数）；新增 `32位寄存器顺序` 下拉（`高字在前（AB CD）` / `低字在前（CD AB）`，`enabled` 绑定 `readDecodeWordOrderEnabled`，禁用时 opacity 0.4 —— §22 C2"仅多寄存器类型时有意义"）；值表 delegate 新增「范围 1000-1001」后缀。**现有 Hex/Binary/UInt16/Int16 全部保持**。
- **Float32 特殊值（§22 C3 逐字落地）**：`非数字（NaN）` / `正无穷大（+Inf）` / `负无穷大（−Inf）`（U+2212 减号以显式 UTF-8 转义写入 core，测试以 `QChar(0x2212)` 构造期望串防同形字符）；status 恒为 `Ok`；有限值最短往返 + 整数值补 `.0`（A11–A13 的 `1.0`/`0.0`/`-2.0`）。

### W3. 测试（deterministic，oracle 全部独立于被测实现）

- **core（`test_register_decode.cpp` 19 → 50）**：新增 31 用例 —— word count 表（含越界 0）、word order token、pair 入口拒绝 1 寄存器类型、UInt32 A07/A08/max/byte-swap 双轴（A28/A29 同构）、Int32 A09/A10/max/−1、Float32 A11/A12/A13/A14/A15/−Inf/A16/A30/A31/0.1 往返/π 位型往返、`decodeRegisterView` 全部失败模式（A17 + OutOfRange + InvalidConfig×2 + 空数据 + 单字直通 + word order 轴）。32 位组合 oracle 独立重写（`combineWords`/`int32Value`/`bitsToFloat`）。
- **bridge（`test_ui_bridge.cpp` 88 → 95，READ-D5..D11）**：类型边界扩至 4..6 且 7/−1/99 被拒；word order 默认/边界/越界忽略；`readDecodeWordOrderEnabled` 门（UInt16 关、三 32 位类型开、Int16 关）；UInt32 滑动窗口投影（A07 = 305419896、行 1 滑窗、行 2 insufficient、span `0-1`/`1-2`、raw 列逐行相等）；Float32 投影（1.0/−5.0/次正规值位型往返/insufficient/raw 不动）；Int32 投影（−200/INT32_MIN/0xFFFF 仍显示 65535）；word order 翻转改变派生值（1065353216→16256）而 raw DEC/HEX 与 span 不动。
- **demo32（`--qml-read-result-demo32`，ctest `qml_read_result_demo` 之外新增 `qml_read_result_demo32`，40 → 41）**：TEST-ONLY/DEMO-ONLY，与 first-slice demo 同安全边界（窗口标题「M11 32位解码演示（模拟数据）」；`--demo-exit-after-ready` 供 ctest headless）。数据集 = unit 1 / FC03 / start 1000 / quantity 6 / timeout 1000 / words `0x3F80,0x0000,0xC0A0,0x0000,0x4049,0x0FDB`——一份响应同时呈现 Float32 1.0（1000-1001）/−5.0（1002-1003）/π 位型（1004-1005）、UInt32 `0x3F800000`/`0xC0A00000`、Int32 负值、word order 翻转（`0x00003F80`）。9 个 stage 全部经 production path（字段文本 → `readRegisterRequest` 派发 → 会话/分析器/Snapshot），断言 token `DEMODECODE32`，期望值由 harness 内独立 oracle（uint32/int32 文本 + π 位型往返）计算，非手算常量。stage 4 还断言：字段可见值与事务一致（quantity=6）、`readDecodeWordOrderEnabled` 在 UInt16 为 false / UInt32 为 true、1 寄存器行无 decodeSpan。

### W4. 负向对照（真实 mutate → FAIL → restore，全部恢复后全量回归绿）

- **A（word order 组合反向）**：`decodeRegisterPair` 高/低字选择翻转 → core **31 用例中 19 个 FAIL**（u32_1/2/4/5、i32_1/2/3、f32_1/3/4/5/6/7/8/9/10/11、v2/v8）→ 恢复。
- **B（byte swap 被跳过）**：pair 解码改用原始字 → 恰好 4 个 byte-swap 用例 FAIL（u32_4/5、f32_8/9 = A28–A31 同构）→ 恢复。
- **C（解码改写 raw）**：2 寄存器行把 raw DEC 覆写为 0 → 恰好 3 个 raw-preservation 用例 FAIL（d8/d9/d10）→ 恢复。
- 结论：ordering 双轴、byte swap、raw 权威分别被独立测试钉死，静默回归不可能溜过门禁。

### W5. Verification（真实命令与结果）

```text
构建（Debug 与 Release 各一次，全量）：cmake --build build/debug | build/release
  → 0 error（既有 2 处无害 unused-variable 告警原样保留，无新增）
targeted：modbuslens_register_decode_tests → 50 passed, 0 failed
          modbuslens_ui_bridge_tests      → 95 passed, 0 failed
full ctest：build/debug  → 41/41 PASS（含 qml_read_result_demo32）
            build/release → 41/41 PASS（含 qml_write_foundation_check_windows 真 windows QPA 几何门）
QML 诊断卫生：FAIL_REGULAR_EXPRESSION（ReferenceError/TypeError/Unable to assign/
  String.arg Invalid arguments）覆盖 demo32，两轮全 0 命中
git diff --check → PASS
负向对照 A/B/C → FAIL 复现并恢复（见 W4）
```

### W6. Problems / RCA（V2 Debug Trace 字段齐全）

**RCA-1（harness-only，非产品行为）`--qml-read-result-demo32` 首跑 details 入口不可达。**
- **Observed**：`DEMODECODE32 FAIL: the read-result details entry is not reachable`；祖先链 dump 显示 `communicationWorkspace visible=0 w=0`（StackLayout 从未切到 Communication）。
- **Expected**：stage 0 的 rail 点击应切到 Communication（first-slice demo 同结构 PASS）。
- **Evidence**：临时插桩 `found=1 visible=0 hasResult=1` + 祖先可见性逐级 dump（现文件无残留）。
- **Root Cause**：demo32 新写的 `clickNamed` 中 QMouseEvent press 的 **buttons 实参误传 `Qt::NoButton`**（`Qt::LeftButton, Qt::NoButton, Qt::NoModifier`）——buttons=NoButton 的 press 是畸形事件，TapHandler 丢弃，页面从未切换；first-slice demo 的同函数正确传 `Qt::LeftButton, Qt::LeftButton`。
- **Fix**：press 改为 `Qt::LeftButton, Qt::LeftButton, Qt::NoModifier`，并加注释冻结该陷阱（press 必须同时携带 button 与 buttons 状态）。
- **Verification**：`ctest -R qml_read_result_demo` → demo 与 demo32 双 PASS。
- **Regression Protection**：demo32 的 9 个 stage 持续断言 details 入口与页面可见字段（fieldChecks），同形回归会在 ctest 中复现为可见断言失败；陷阱以代码注释冻结。

**RCA-2（API 陷阱复用既有认知）**：harness 内 `.arg(int, QString, QString)` 多参混用再次触发 Qt6 无此重载（QStringTest 同坑在案）；改链式 `.arg(i).arg(a, b)`。FAIL_REGULAR_EXPRESSION 已含 `String.arg..: Invalid arguments` 作为 QML 层防护（本例发生在 C++ 层，由编译器直接拦截）。

### W7. Files Changed（行为提交 bc99e6e）

```text
src/core/analysis/RegisterDecode.h   | +62  （类型/枚举/三 API/契约注释）
src/core/analysis/RegisterDecode.cpp | +156 （pair/view 解码 + Float32 文案）
src/ui/AnalysisController.h          | +15  （2 Q_PROPERTY + 3 方法 + 成员）
src/ui/AnalysisController.cpp        | +84  （范围扩展 + word order + 滑动窗口投影）
src/ui/qml/pages/CommunicationPage.qml | 类型下拉 7 项 + word order 下拉 + 范围后缀
src/main.cpp                         | runReadResultDemo32（9 stage）+ 派发项
tests/test_register_decode.cpp       | +31 用例
tests/test_ui_bridge.cpp             | +7 用例（READ-D5..D11）
CMakeLists.txt                       | qml_read_result_demo32 + 两组 test properties
共 9 文件，+1495 / −11
```

### W8. 边界（本轮明确不做什么）

```text
· scaling / offset / engineering units / 40001 别名 / Float64 / String —— 仍 DEFERRED（T024 §3）
· register map / device profile / 厂商语义 —— M12；本轮零设备语义声称
· 写回 / 写权限 —— 无任何 write 路径变化；Agent 无新增能力
· REAL MODBUS HARDWARE = NOT VERIFIED（demo32 为 synthetic 证据）
· verified LKGC 保持 352b81c…（不推进）；未 push / 未 tag / 未 amend
· 现有 Hex/Binary/UInt16/Int16 视图与 first-slice 1 寄存器投影路径逐字节保持
```

### W9. 状态

```text
M11 first slice  = ACCEPTED（§25）
M11 second slice = IMPLEMENTED / awaiting Human visual review（demo32）
M11 overall      = IN PROGRESS
verified LKGC    = 352b81c82d5efa9aac5418cccaaf1605a68cd9d3（不变）
REAL HARDWARE    = NOT VERIFIED（不变）
```

## 27. Human Contract Ratification — 2-register Alignment = Sliding Window per Raw Row（2026-09-25，docs-only）

> **Human / ChatGPT 在审计轮后正式追认：M11 v1 的 2-register alignment policy = SLIDING WINDOW PER RAW ROW。自本节起为 canonical M11 v1 contract。**

### 27.1 追认的规则（冻结表述）

对 canonical raw values `words[0], words[1], … words[n-1]`：

- 第 i 行的 32-bit decode 使用 **words[i] + words[i+1]**，其中 **0 ≤ i < n−1**；
- 最后一行 i = n−1 在选择 UInt32 / Int32 / Float32 时 `DecodeStatus = InsufficientWords`，**不得伪造第二个 word**；
- UI 必须显示派生值的来源地址范围（如 **1000-1001**）；
- raw rows **不得重排、不得改写、不得隐藏**（raw DEC / raw HEX / 地址恒显恒不变）。

### 27.2 Provenance（冻结，禁止事后改写历史）

- **pre-implementation T024（`d5438fc847a45b4ab0d63fe83553e5cb12a85ac1` 版）没有逐字冻结该算法**——审计轮全文检索确认：仅有「所选起点」（§9）、「逐寄存器对齐」（§10）、`decodeRegisterView(words, type, wordOrder, start, count)` API 形状（§12）、A17/A18 向量等**约束性**条文，无任何「row i 使用 words[i] 与 words[i+1]」或等价的滑动窗口逐字条款；
- 该策略是 **implementation-time decision**，在 behavior `bc99e6ea871628a3a685b9cf80cf3840e7b3b171` 实现时作出（代码注释与测试同落地）；
- 契约化文本（§26 W1）**首次出现于 docs archive `2f767d141ea36425e3276425bc1bf1b41ceb187c`**（`git log -S "滑动窗口"` 唯一命中；`git blame` 同证）；
- **Human 于 2026-09-25 审计轮后正式追认（本节）**，追认后始成 canonical M11 v1 contract。**不得改写旧章节制造"早已冻结"的假象。**

### 27.3 Demo32 数据集与 Human 可见值（审计更正后的权威口径）

数据集（当前源码 `--qml-read-result-demo32`）：unit 1 / FC 03 / **start 1000** / **quantity 6** / timeout 1000；
words = `0x3F80, 0x0000, 0xC0A0, 0x0000, 0x4049, 0x0FDB`（地址 1000–1005），默认 Normal + HighWordFirst。

| 地址范围 | bits32 | UInt32 | Int32 | Float32 |
|---|---|---|---|---|
| 1000-1001 | 0x3F800000 | 1065353216 | 1065353216 | **1.0** |
| 1001-1002 | 0x0000C0A0 | 49312 | 49312 | 次正规值（位型往返） |
| 1002-1003 | 0xC0A00000 | 3231711232 | **−1063256064** | **−5.0** |
| 1003-1004 | 0x00004049 | 16457 | 16457 | 次正规值（位型往返） |
| 1004-1005 | 0x40490FDB | 1078530011 | 1078530011 | **π（≈3.1415927）** |
| 1005（末行） | — | InsufficientWords | InsufficientWords | InsufficientWords |

- **305419896（0x12345678，A07）与 −200（0xFFFFFF38，A09）不属于 demo32 数据集**——它们是 automated acceptance matrix / unit / bridge 测试向量（保持有效）；此前 PROJECT_STATUS Next Action 与上轮报告把它们列为 demo32 Human 可见值属**转述错误**，本轮已更正。切换 LowWordFirst 后行 0 = 0x00003F80 = 16256、行 1 = 0xC0A00000 = 3231711232、行 2 = 0x0000C0A0 = 49312。

### 27.4 Demo32 自动证据边界（如实划定）

- **已覆盖**：可见编辑器字段 == 请求（1/03/1000/6/1000）；read_success + 6 raw 行 DEC；默认 UInt16/Normal/HighWordFirst；word-order 控件使能门（UInt16 关 / UInt32 开）；UInt32 滑窗值 + `decodeSpan`（1000-1001 … 1004-1005）+ 末行 insufficient；LowWordFirst 翻转值 + raw HEX 不动；Float32 1.0 / −5.0 / π 位型往返；Int32 负值；raw DEC 恒不变；details 入口可达。
- **未由 demo32 覆盖**（由其它门承担，勿混引）：`previewReadDraft` PDU/RTU 预览 == Actual TX 的等式断言属 **first-slice demo 与 `--qml-read-result-check`**（两者在本轮两层证据中均 PASS）；1000×700 几何断言属 **`--qml-geometry-check` / `qml_write_foundation_check_windows`**。demo32 窗口内的 1000×700 目视结论仍需 Human 确认。

### 27.5 Evidence Completion（2026-09-25 本轮，全部真实 exit code——直接重定向取得，非管道值）

**Clean-env（new staging `build/m11-visual-candidate`，PATH 仅 Windows system dirs）**：

| 门 | exe exit code | 诊断（4 模式） | FAIL marker |
|---|---|---|---|
| --qml-smoke-test | 0 | 0 | 0 |
| --qml-read-result-check | 0 | 0 | 0 |
| --qml-read-result-demo --demo-exit-after-ready | 0 | 0 | 0 |
| --qml-read-result-demo32 --demo-exit-after-ready | 0 | 0 | 0 |
| --qml-production-write-check | 0 | 0 | 0 |

**Windows QPA（显式 `QT_QPA_PLATFORM=windows`，同一 staging）**：smoke = 0；read-result = 0；first demo = 0；demo32 = 0；production-write = 0；write-foundation = 0；focus = 0；nav = 0；geometry = 0（九门全 0 诊断、0 FAIL）。

**R15 / R16 / R17 staging clean-env 证据**：`--qml-production-write-check` 输出含 R15×5（open / silent-slave Timeout / adapter removal / reconnect）/ R16×2 / R17×3 marker + `PRODUCTION WRITE CHECK PASS` 收尾（windows QPA 层同 PASS）——**自此"staging clean-env R15/R16/R17 PASS"有本层直接证据，不再引用 offscreen ctest 代证**。

**identity**：source 与 staging `ModbusLens.exe` 均 = 4564623 B / SHA-256 `aef74296e3be70150f7fa2f386c9f1b5a56f81ab90f7ccef2984948519814639`（cmp 逐字节一致；staging 未重建）。

### 27.6 状态

```text
M11 second slice = IMPLEMENTED / AUTOMATED EVIDENCE COMPLETE / HUMAN REVIEW PENDING
alignment policy = RATIFIED（本节，2026-09-25）
package          = NOT CREATED
verified LKGC    = 352b81c82d5efa9aac5418cccaaf1605a68cd9d3（不变）
```

## 28. M11 Final Acceptance / Closure（2026-09-25，docs-only closure）

> **M11 = ✅ COMPLETE。** 依据 §18 exit criteria 全部满足（下表），且 Human 第一/第二切片视觉验收均 PASS。
> 本节归档 = docs-only closure commit（行为代码零改动；M11 behavior-bearing candidate = `bc99e6ea871628a3a685b9cf80cf3840e7b3b171`，**仅为候选、不构成 verified LKGC 推进**）。

### 28.1 Human Second-Slice Visual Acceptance Archive（逐字归档，不扩写）

- **Human 原始结论（逐字）**：「M11 第二切片全部 PASS，raw 不变，word order/byte order 正常，1000×700 正常」⇒ **M11 SECOND SLICE HUMAN VISUAL ACCEPTANCE = PASS**。
- 该 PASS 覆盖：UInt32 / Int32 / Float32、word order、byte order、raw-preservation、1000×700。第二切片不再处于 HUMAN REVIEW PENDING。
- **边界（不得扩写）**：Human 当前无真实设备——该 PASS **不是** real-hardware 证据；本结论不声称任何物理 PLC / slave 验证。
- First-slice Human PASS = §25（2026-09-25 12:40 归档，`bceb5a2`）；本节 = second slice 归档。

### 28.2 Exit criteria 终表（对照 §18）

| exit criterion（§18） | 结果 | 证据 |
| --- | --- | --- |
| REQUIRED 解码矩阵全绿（A01–A26、A28–A31） | **PASS** | core `register_decode` 50/50（A07–A17、A28–A31 + 失败模式 + NaN/±Inf 冻结文案 + view 入口）；`ui_bridge` 95/95（READ-D1..D11 含 FC03/FC04/custom 资格）；负向对照 A/B/C（19/4/3 FAIL→恢复）；demo32 五 stage |
| full Debug / Release regression 0 失败 | **PASS**（含一次未复现的历史失败，root cause UNKNOWN，见 28.3） | Debug：build 0 error + ctest **41/41**；Release：build 0 error + 全量 41/41（closure ×2 + 证据修正轮稳定性 ×3）+ #41 targeted **20/20** |
| QML 诊断 0/0/0（+String.arg 模式） | **PASS** | clean-env 5 门 + windows-QPA 9 门共 **14 份日志**：ReferenceError=0、TypeError=0、Unable to assign=0、String.arg() Invalid=0、全部 FAIL marker=0 |
| 1000×700 可用（真实 windows QPA） | **PASS** | `qml_geometry_check` / `qml_write_foundation_check_windows` exit 0（两层）+ Human 目视确认（§25 + §28.1） |
| §15 M10 回归清单无回归 | **PASS** | editable read FC（FC03/FC04/custom）+ 五字段键盘编辑 + baud + Actual TX/RX + 10 类分类 + FC06/FC16 + R15/R16/R17（两层各 9 marker + `PRODUCTION WRITE CHECK PASS`）+ preview + write path：全量 41/41（Debug/Release）+ staging clean-env 五门 + windows-QPA 九门全 0 |
| raw truth 未被改写的直接证明 | **PASS** | A20 族 + 负向对照 C（raw 改写 ⇒ 3 测试真实 FAIL→恢复）+ 架构 grep（`RegisterDecodeStatus`/解码 API 在 core 内零外泄；`TransactionAnalysis` 与 decode 零耦合；QML 零 wire 解析） |
| 文档归档 | **PASS** | 本节 + PROJECT_STATUS / BACKLOG / devlog 同步（closure commit） |
| 独立 behavior-bearing commit | **PASS** | `bc99e6e`（第一切片 `b95de54`→`de58019` 演进链已归档） |
| Human Review | **PASS** | first slice §25 + second slice §28.1（双 PASS） |
| packaging / portable Final D | **NOT CREATED**（非 exit criterion，§18 明文） | 未创建 ZIP / Final D；`build/m11-visual-candidate` 仅为 visual/verification staging，不是 canonical package / release artifact / Final D / LKGC |
| real hardware | **OPTIONAL / NOT PERFORMED / NOT VERIFIED**（非 exit criterion，§18 明文） | Human 无设备；不写 PASS/FAIL；不阻塞 closure |

### 28.3 Release 首次运行历史失败记录（V2 Debug Trace；2026-09-25 证据修正轮改写）

> **修正 provenance**：本节初版曾写"瞬态/桌面时序（推定）/非产品缺陷"等机制性表述；因**首次失败的
> 具体 stdout/stderr 未留存**，任何失败机制断言均无直接证据。2026-09-25 证据修正轮（Human 指令）
> 撤回全部机制断言，改为以下纯事实记录。**后续连续 PASS 不构成对首次失败根因的反向证明。**

- **Observed**：closure 轮 Release 全量 ctest 第 1 次运行 **40/41**——失败 = `41:qml_write_foundation_check_windows`。
- **Expected**：41/41。
- **Evidence**：`build/release/Testing/Temporary/LastTestsFailed.log` 仅留测试名一行（`41:qml_write_foundation_check_windows`）；该次失败的具体输出**被后续运行覆盖，未留存**。
- **First-failure detailed output = NOT RETAINED。Root cause = UNKNOWN**（不推断机制；禁止仅凭测试名归因）。
- **Fix**：无产品改动（零代码变更；不降低门禁标准）。
- **Verification（closure 轮原始复跑）**：全量 2 次连续 41/41 + 该门单独 5 次 PASS（合计 6 连绿）。
- **Verification（2026-09-25 证据修正轮新增稳定性数据）**：
  - targeted：`ctest -R qml_write_foundation_check_windows`（真实 Windows QPA，显式 `QT_QPA_PLATFORM=windows`）**20/20 PASS**（每次独立日志 + 真实 exit code 0 ×20；四类诊断 ReferenceError/TypeError/Unable to assign/String.arg() Invalid 与全部 FAIL marker 计数 = 0）；
  - Release 全量：**3/3 = 41/41**（exit 0 ×3，日志零诊断零 FAIL）。
- **解释边界**：以上只能支持「closure 首次 Release run 曾观察到 `qml_write_foundation_check_windows` 单次失败；首次失败具体输出未留存，root cause UNKNOWN；closure 轮原始复跑与本轮 targeted 20/20、全量 3/3 均未复现；现有最终 acceptance gates 稳定通过」。**不得**写"证明是 timing""证明是 harness-only""证明不是产品问题"。
- **Regression Protection**：该门持续运行于 Debug/Release 全量回归；如再现按 V2 Debug Trace 另行建档排查（不因本记录视为已解）。

### 28.4 Scope guard 终检（closure 轮零临时实现）

scaling / offset / engineering units / 40001·4xxxx alias / register map / device profile / vendor semantic interpretation / Float64 / string decode / automatic device inference / automatic retry / writeback / M12 功能——**全部未实现，维持 DEFERRED / OUT OF SCOPE**（源码与本轮行为树零新增）。

### 28.5 Closure decision 与治理状态

```text
M11 = ✅ COMPLETE（contract required set + 双 Human PASS + Debug/Release + 两层证据 + scope clean，无 blocker）
M11 behavior-bearing LKGC candidate = bc99e6ea871628a3a685b9cf80cf3840e7b3b171（PROPOSED，等待 Human 批准）
verified LKGC = 352b81c82d5efa9aac5418cccaaf1605a68cd9d3（本轮不推进）
REAL HARDWARE = NOT PERFORMED / NOT VERIFIED（NON-BLOCKING）
package = NOT CREATED（非必需）
M12 = NOT STARTED
```

> **〔2026-09-25 治理批注 · LKGC ADVANCED〕** 上块中的 PROPOSED candidate 已由 Human
> 明确批准（授权原文：「批准推进 LKGC 到
> bc99e6ea871628a3a685b9cf80cf3840e7b3b171」）⇒ **verified LKGC =
> bc99e6ea871628a3a685b9cf80cf3840e7b3b171**（前一 verified = 352b81c…；ancestry
> 双向实测 exit 0；bc99e6e..HEAD 实测仅 docs/ 5 文件 ⇒ bc99e6e = FINAL M11
> BEHAVIOR-BEARING TREE；其后 2f767d1 / cc9193d / 6550ca9 / 30332d2 均 docs-only，
> 永不作 LKGC）。§28.2/§28.3 的证据边界不变：real hardware = NOT PERFORMED /
> NOT VERIFIED（NON-BLOCKING）；Release 历史异常 = ONE NON-REPRODUCED HISTORICAL
> FAILURE / root cause UNKNOWN。

## 29. M11 Final Portable Package Archive（2026-09-25，docs-only archive）

> **M11 FINAL PORTABLE PACKAGE = VERIFIED。** 打包于 M11 governance closure 之后（HEAD 系 =
> `bc99e6e` + 5 个 docs-only 提交；`bc99e6e..HEAD` 实测仅 docs/ 5 文件 ⇒ 打包 tree 与
> behavior tree `bc99e6e` 行为同一）。canonical 流水线全绿，零语义改动。

### 29.1 Identity（本轮独立重算，不采信历史 hash）

| 路 | 路径 | size | SHA-256 |
|---|---|---|---|
| A | `build/release/ModbusLens.exe` | 4564623 | `aef74296e3be70150f7fa2f386c9f1b5a56f81ab90f7ccef2984948519814639` |
| B | `build/release/deploy/ModbusLens.exe` | 4564623 | 同上 |
| C | `build/package/ModbusLens-2.0.0-windows-x64/ModbusLens.exe` | 4564623 | 同上 |
| D | `build/package-extract/ModbusLens-2.0.0-windows-x64/ModbusLens.exe` | 4564623 | 同上 |

**A == B == C == D（cmp byte-identical）**。
**ZIP = `build/package/ModbusLens-2.0.0-windows-x64.zip`：41139738 B / SHA-256
`10783914547fed26f2d3539126ef51238213280cf892fa8c434628ef789563d5`**（1499 entries =
1498 payload 含 README.txt + 1 manifest）。

### 29.2 流水线记录（全部真实 exit code）

工具链实测（CMakeCache）：Qt `D:/QT/6.11.1/mingw_64`（6.11.1）/ GCC
`D:/QT/Tools/mingw1310_64/bin/g++.exe` 13.1.0 / Release。环境门禁：qtpaths --qt-version =
6.11.1、qtpaths -query、windeployqt --version = 6.11.1 全 exit 0（**历史 ISSUE-015 的
qtpaths pipe 阻塞在本会话未出现**）。Release build：0 error（no work to do，tree 与 LKGC
行为同一）。Release CTest：**41/41**。freshness oracle：PASS（含 RED 用例）。retention：
`E:\desktop\ModbusLens\build\retention-m11-final-20260925-165545\{release-deploy, package,
package-extract}`（旧 M10 产物整目录保留）。make_package：stem = `ModbusLens-2.0.0-windows-x64`，
structural / credential-config negative scan / absolute-path audit / manifest 1498 payload /
ZIP entries 1499 / fresh extraction / minimal-PATH（smoke·nav·geometry）/ external-CWD 全 PASS。

### 29.3 Final D portable gates（clean PATH = 仅 Windows 系统目录 + QT_QPA_PLATFORM=windows）

九门全部 **exit 0**（真实 exe exit code，直接重定向）：smoke / read-result / first demo /
demo32 / production-write / write-foundation / focus / nav / geometry。诊断四模式
（ReferenceError / TypeError / Unable to assign / String.arg() Invalid）与全部 FAIL marker
（READFAIL/PRODWRITEFAIL/WRITEFAIL/GEOFAIL/NAVFAIL/FOCUSFAIL/SMOKEFAIL）**= 0**。
external-CWD smoke（从 D 目录外、clean PATH）exit 0。runtime version = **2.0.0**。

### 29.4 M11 portable evidence（全部来自 Final D，非 source-tree 混层）

- **First slice**：read-result gate M6（UInt16+Normal 默认、raw 0x1234→4660、raw intact）/
  M7（类型下拉含 全部四种 + 默认 index）/ M8（Hex 视图 0x1234、raw 不动）/ M9（ByteSwapped
  13330、raw HEX 不动）+ first demo READY（0x1234/0xFFFF/0x0080）。
- **Second slice**（demo32 五 stage + READY，words=0x3F80,0x0000,0xC0A0,0x0000,0x4049,0x0FDB，
  start 1000 / quantity 6）：默认 UInt16/Normal/HighWordFirst + word-order 控件门；
  UInt32 滑窗 + spans + 末行 insufficient_words；LowWordFirst 翻转且 raw 不动；
  Float32 1.0 / −5.0 / π；Int32 负值。
- **M10 回归**：R15×4 / R16×2 / R17×3 marker + `PRODUCTION WRITE CHECK PASS`；
  read-result check PASS（TX == preview == encoder；10 类 taxonomy）。
- **1000×700**：geometry / write-foundation gates exit 0（真 windows QPA）。

### 29.5 边界

M11 real-hardware supplementary verification = **NOT PERFORMED / NOT VERIFIED（NON-BLOCKING）**
——package PASS 不构成 hardware PASS。canonical naming 保持（未改名 M11 专用 ZIP）。
打包命令 canonical 档案 = `docs/ENVIRONMENT.md` §4b。本节归档 = docs-only commit，
不改变 verified LKGC = `bc99e6ea871628a3a685b9cf80cf3840e7b3b171`。
