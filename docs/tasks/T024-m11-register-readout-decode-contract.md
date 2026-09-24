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
| R2 | 2 寄存器类型（UInt32 / Int32 / Float32）的 **word order**：`big-endian`（AB CD，默认）与 `little-endian word order`（CD AB）两种；类型选择 + word order 选择为**用户显式配置** | B1「含 byte/word order」+ T023 M11-B2「word swap 属 M11」 |
| R3 | 解码资格按「**成功的 register-read-compatible 事务 + canonical raw words**」判定（FC03 / FC04 / 自定义 register-read-compatible 功能码一视同仁）；**禁止** `if function == 0x03` 硬编码；**禁止**把 FC01/FC02 位读当寄存器读 | T023 Part C（功能码可编辑、`0x01..0x7F`）+ 分析器已按 request frame 参数化 |
| R4 | **Raw truth 不变式**：decode 只是派生视图；`TransactionAnalysis.values` 仍是唯一 raw 权威；raw DEC/HEX 展示保持原样并在解码视图旁边始终可查；**decode 状态/错误绝不改写 wire result**（Success 不得因解码失败变成 Timeout/TransportError/CRC/Exception） | T023 M11-B3/B4 + READ-RX-5 既有原则 |
| R5 | **Decode failure model v1**（最小集，见 §9）：`InsufficientWords` / `OutOfRangeSelection` / `InvalidConfiguration` / `UnsupportedType` —— 独立的 decode 状态，**不是** wire outcome | T023 R-FACT 原则（事实与原因分离）的 M11 延伸 |
| R6 | **UI 边界**：接入**既有** Read Result 呈现（Communication 页结论行 + 有界可滚动 read-result 对话框的值表区域）：解码视图与 raw DEC/HEX 并排呈现；用户以**显式控件**选择类型与（2 寄存器类型的）word order；**不新建大页面**（T023 §7 已否决独立页先例） | T023 READ-UI + §7 IA 决议 |
| R7 | **地址语义**：PDU / 0-based 权威不变；raw register address（PDU）在解码视图中保持可追溯 | B6 + T023 §4 |

## 5. DEFERRED（本轮明确不做，且验收矩阵不得伪造其用例）

| 项 | 状态 | 依据 / 理由 |
| --- | --- | --- |
| **byte swap（16-bit 寄存器**内部**字节序交换）** | DEFERRED | repo 只点名「word swap / word order」（T023 line 898、plan「byte/word order」）；**寄存器内字节交换未被任何条文点名**。M10 raw words 已是按协议大端解出的 uint16，v1 不再做寄存器内二次交换。**术语区分见 §8。** |
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

**word order 冻结**：
- `big-endian`（默认）：字序 = 寄存器序（words[0] 为高 16 位，AB CD）。
- `little-endian`（即行业所谓 word-swapped）：words[1] 为高 16 位（CD AB）。
- **byte order within register：DEFERRED**（§5）。术语纪律：`word order`（寄存器序）与 `byte order`（16-bit 字内部的字节序）是**两个轴**，文档与 UI 不得混用 `endianness / byte swap / word swap` 三词。

**Float32 确定性向量（REQUIRED 用例，写入验收矩阵）**：
`1.0` = words [0x3F80, 0x0000]（big-endian）；`0.0` = [0x0000, 0x0000]；`−2.0` = [0xC000, 0x0000]；NaN = [0x7FC0, 0x0000]（任一 NaN 位型）；+Inf = [0x7F80, 0x0000]；little-endian 下 `1.0` = [0x0000, 0x3F80]。

## 8. byte order / word order 术语（§V 冻结）

- **register（word）order**：多寄存器值中**寄存器之间的先后解释**（AB CD vs CD AB）。M11 v1 REQUIRED。
- **byte order within register**：单个 16-bit 寄存器**内部**两个字节的先后。M11 v1 **DEFERRED**。
- 本契约与未来 UI 文案中禁止把 `endianness`（泛称）、`byte swap`（寄存器内）、`word swap`（寄存器间）三词互换使用。

## 9. Decode failure model（v1 冻结）

独立于 wire result 的 **decode status**（四值，不新增 wire outcome，不触碰七 outcome）：

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
- 类型选择（Hex/Binary/UInt16/Int16/UInt32/Int32/Float32）与 word order（仅 2 寄存器类型启用）为**用户显式控件**；默认 = 无解码（或 UInt16，以 Human Review 裁定为准 —— 本契约标注〔待 Review〕）。
- decode status 非 `Ok` 时：解码单元格显示状态文案（如「字数不足」），**raw 列不受影响**。
- **1000×700 约束**：新增控件不得把既有内容推出窗口（M10-F ISSUE-016/018 的教训）；实现轮必须以真实 windows QPA 几何门禁复测。

## 11. Function Code boundary（§S）

- 解码资格 = `read_success`（CLASS-10）+ canonical raw words 存在；与功能码数值无关（FC03 / FC04 / 自定义 register-read-compatible 一致）。
- 禁止 FC01/FC02 位读语义混入；禁止厂商语义声称；异常响应（F|0x80）不是成功读取，无解码资格（无 raw words）。

## 12. Raw-truth ownership（§R）

`TransactionAnalysis.values`（RAW uint16，Success 才有）是**唯一**寄存器值权威；M11 的解码结果是**派生视图**（可在 core 增加纯函数 `decodeRegisterView(words, type, wordOrder, start, count) -> result`，或在 presentation 层调用 core 纯解码——实现方向在 implementation entry gate 后由 source audit 决定）；**任何路径都不得把解码结果写回 TransactionAnalysis / model role / QML 状态以冒充 raw**。

## 13. Hardware policy（§AB）

- 纯确定性解码**可以且应该**用已知 raw words 自动测试（无需硬件）。
- **Real hardware = OPTIONAL / NON-BLOCKING**：repo（§ZE14 惯例 + T023/M10 closure 边界）未把真实硬件定为 M11 硬门；本轮**不升级**为硬门。若 Human 要求真实设备验证 Float32 等，属后续 OPTIONAL 增补。

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
| A27 | OPTIONAL 真实设备 | 真实设备读取 | 任意 | 真实值 | Ok | 不变 | — | ✅ | OPTIONAL |

## 17. Implementation entry gate

1. 本契约经 **Human Review PASS**（scope / v1 边界 / acceptance matrix / truth architecture 逐项裁定；〔待 Review〕标注项——尤其解码默认视图与 UI 位置细节——由 Human 裁定）。
2. 另起 implementation 轮：**先**重新 source audit（`TransactionAnalysis.values` 消费链、Read Result 呈现链、Communication 布局预算）**→ RED 证据 → 最小 implementation slice**（建议首切片 = 纯 core 解码函数 + 单元矩阵 A01–A20；UI 集成另切片）——**不是一次性实现整个 M11**。
3. 每片独立 behavior-bearing commit + full regression + 停轮 Human Review。

## 18. Exit criteria

REQUIRED 矩阵全绿；full Debug/Release regression 0 失败；QML 诊断 0/0/0；1000×700 可用（真实 windows QPA）；§15 M10 回归清单无回归；raw truth 未被改写的直接证明（A20 + wire result 不变性）；文档归档；独立 behavior-bearing commit；Human Review。**REAL HARDWARE = OPTIONAL（A27 不阻塞 exit）。**

## 19. Problems Encountered / 待 Human 裁定

- 本轮无实现问题（docs-only）。
- **〔待 Review〕**：① 解码视图默认状态（「无解码」还是默认 UInt16）；② 解码控件落位（值表内联下拉 vs 对话框头部）；③ byte-swap-within-register 是否提前出 DEFERRED 转 REQUIRED（repo 未点名，本契约按 DEFERRED）；④ acceptance matrix A14/A15 的 NaN/Inf 显示文案。
- **同步台账要点（§N）**：16 项 handoff 声明中 15 项 VERIFIED_FROM_REPO（HEAD/LKGC/M10 states/M11 authorization/values 权威/FC04+custom/decode 非 FC03-only/排除项归属/无既有 M11 任务文档均实证；「M11 已获 START AUTHORIZATION」来源 = Human 原话，属授权事实而非 repo 内容）；1 项 UNKNOWN → 已按 §AB/§AC 处理为「repo silent ⇒ 不升级」（M11 的 hardware/package 硬门要求）。无 CONTRADICTED。
- 教训（§C 编辑安全）：本轮全程使用精确锚点小步编辑 + 全量 diff 审查，未使用模糊替换。

## 20. Git Commit

`M11: define decode acceptance contract`（docs-only；不 amend `fbb59b3`；不 rebase；不 push；不 tag；verified LKGC 保持 `352b81c82d5efa9aac5418cccaaf1605a68cd9d3`）。
