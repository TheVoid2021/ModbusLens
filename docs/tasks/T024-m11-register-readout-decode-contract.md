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
