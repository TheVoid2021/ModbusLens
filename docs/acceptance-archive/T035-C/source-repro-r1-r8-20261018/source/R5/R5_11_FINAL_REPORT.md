# R5_11_FINAL_REPORT — T035-C Round SOURCE-REPRO-R5

日期：2026-10-17 22:52
结论：**SOURCE-REPRO-R5 = PARTIAL**

- **§3 FIX-04 第二版（经 3 次迭代）→ PASS**
- **§4 负例 → PASS（内建自检）**
- **§5/§6 Register Map 滚动可达 + 断言修正 → PASS**
- **§7 TARGETED 4/4 PASS**
- **§8.1 全量构建 PASS（494/494）· §8.2 测试清单一致 · §8.4 全量 ctest = 70/74 FAIL（4 项新失败，根因为 FIX-02 回归）**

---

## A. Trusted Resync

HEAD `517d78c…`（MATCH）· staged=0 · diff-check PASS · WIP **50**（50 项 SHA-256 基线已存）· 冻结 Release/Debug **MATCH**。详见 §P 与 R5_01。

## B. R4 两项既有修复的回归状态

| 修复 | R5 复验结果 |
|---|---|
| FIX-01（readiness 测试隔离 + 负例） | **PASS**（27/27，EXIT 0） |
| FIX-02（Harness session 生命周期） | **K 场景 PASS**（`qml_read_result_check` EXIT 0）**但引入 4 项 write 门禁回归**（见 §N） |

## C. FIX-04 第二版正例结果（含迭代过程）

| 迭代 | 判据 | 结果 |
|---|---|---|
| R4 末版 | 仅 `readResultPanel` 子树豁免 | **FAIL**（`stage 13` note 打印但末行 ACTFAIL；`fail()` 不中断） |
| 第 1 次修正（本轮） | + `readResultDetailDialog` + `readResultValuesList` | **仍 FAIL**（EXIT 1，ACTFAIL 1） |
| 诊断（SEMDIAG） | 打印命中项 | **命中 = `engineeringSemanticNotice visible=0`，位于 `engineeringWorkspace`（非 Communication 页）** |
| **最终修正** | 追加 **`item->isVisible()`**（overlay 的本质条件） | **PASS（EXIT 0 / ACTFAIL 0）** |

**报告原文**：`ACT: stage 13: no semantic overlay` · `ACT: stage 13 negative control: visible unauthorised semantic overlay detected` · `ACT: ACTIVE PROFILE CHECK PASS (B3-Q01..Q22)`

## D. FIX-04 负例结果

**内建负例（每次运行都执行）**：创建可见的 `QQuickItem`（`objectName = testSemanticOverlayProbe`）挂到 `window->contentItem()` → 用**同一判据**重新 walk → 必须命中 → 移除。

```
VALID_SEMANTIC_OBJECT   = ACCEPTED（readSemanticCell 在 read-result 证据子树内）
HIDDEN_SEMANTIC_OBJECT  = ACCEPTED（engineeringSemanticNotice，visible=0，非 overlay）
FORBIDDEN_OVERLAY       = DETECTED（probe 命中，note 输出）
NEGATIVE_CONTROL        = PASS
```

## E. Semantic 判断作用域

**最终判据（三条同时满足才算违规）**：
1. `objectName` 含 `semantic`（CaseInsensitive）
2. **`isVisible()`**（不可见项不构成 overlay）
3. **祖先链不含** `readResultPanel` / `readResultDetailDialog` / `readResultValuesList`（已批准产品语义展示）

**保留了「真正的错误 overlay 仍 FAIL」**（由内建负例每轮证明），**未删除断言、未改名、未禁用 semantic 功能、未改 CommunicationPage.qml**。

## F/G. Register Map 实际滚动轨迹（程序化，明确标注）

```
SCROLLDIAG before: contentY=0 contentHeight=627 viewport=513 maxY=114
SCROLLDIAG after:  contentY=114 fieldRect=x257.5 y584.5 w705 h32
                   insideWindow=1 insideDialog=1 enabled=1
SCROLLDIAG viewportLocal: y=481 h=32 viewportH=513 insideViewport=1
SCROLLDIAG focus=1
REG: REGISTER MAP CHECK PASS (B2-Q01..Q30)
```
**判定**：滚动 114px 后，字段底部 = 481+32 = **513 = viewport 高度**（**恰好完整可见**，余量 0）；`insideWindow=1`、`enabled=1`、`focus=1`。
**证据等级**：**程序化滚动（`contentY` 直接设置）** —— 证明**几何可达性**，**未验证**鼠标滚轮/触控手势的交互体验。

**过程中修正的两个坐标系错误（如实记录）**：
1. `sceneRectOf(popup)` 与 `mapToItem(dialog)` 基准不同 ⇒ `insideDialog` 误判；
2. `itemOf(popup).height()` = **0** ⇒ 改用 **Flickable viewport 局部坐标**（唯一自洽的坐标系）。

## H. 修正后的断言与原断言对比

| | 原断言 | R5 断言 |
|---|---|---|
| 判据 | `requireInsideWindow(regUnitField)`：**scroll offset 0 时**必须完整在窗口内 | 找到祖先 `QQuickFlickable` → **滚动到 max** → 要求：`insideWindow` + `insideViewport` + `enabled` + `focus` |
| 其余 12 个字段 | 同 | **不变**（仍 `requireInsideWindow` 严格检查） |
| 失败条件 | 溢出即失败 | 无滚动容器 / 滚动后仍不可见 / 不可交互 ⇒ 失败 |

**保留 10.5px 级余量约束**：断言要求「滚动后完整可见（含 0.5px 容差）」，**不**放宽为「部分可见」或「有 Flickable 即 PASS」。

## I. 实际修改文件

**仅 `src/main.cpp`**（`runQmlActiveProfileCheck` + `runQmlRegisterMapCheck` 两段）。`tests/test_register_readiness.cpp` **本轮未改**（保持 R4 的 FIX-01）。详见 `R5_10_SOURCE_DIFF_AUDIT.md`。

## J. 隔离增量构建结果

| 构建 | 结果 |
|---|---|
| `ninja modbuslens`（×5，各次编辑后） | 全部 **exit 0**、0 error |
| `ninja`（全量，§8.1） | **exit 0，494/494**，0 error，**50 个测试 exe** |

## K. 四项原始失败测试全新结果

| 测试 | EXIT | 结果 |
|---|---|---|
| `register_readiness` | **0** | **PASS（27/27）** |
| `qml_read_result_check` | **0** | **PASS** |
| `qml_register_map_check` | **0** | **PASS** |
| `qml_active_profile_check` | **0** | **PASS** |

**TARGETED = 4/4 PASS**

## L. 全量测试目标构建

`494/494` 目标，0 error，`*_tests.exe` = **50**。

## M. 完整安全 CTest 实际执行结果（唯一一次）

```
TESTS_DISCOVERED = 74
TESTS_APPROVED   = 74（与 R1 名称集合逐项一致，TEST_NAME_SET_IDENTICAL=YES）
TESTS_EXECUTED   = 74
TESTS_PASSED     = 70
TESTS_FAILED     = 4
TESTS_SKIPPED    = 0
CTEST_EXIT_CODE  = 8
```

**失败项**（**与 R1 的 4 项完全不同**）：

| # | 测试 | 失败断言 |
|---|---|---|
| 39 | `manual_import_pdf_docx` | **ISSUE-025 已知问题**（contentHash 不稳定） |
| 58 | `qml_write_foundation_check` | `WRITEFAIL C35: the reason was overwritten (disconnected)` · `DLG5b: the read did not make busy true` · `DLG5b: the reason is not BusyBecameTrue` |
| 59 | `qml_production_write_check` | 同上 + `DLG5b: the dialog is still visible after the busy invalidation` · `PRODWRITEFAIL R16: no pending request to lose / terminals=0` |
| 71 | `qml_write_foundation_check_windows` | 同 #58 |

**根因（CONFIRMED，本轮修改引入）**：
**FIX-02 的 `beginOk == false ⇒ 返回 NotSent 且不设 pending`** 改变了 write 门禁依赖的旧行为——这些门禁的某些子场景**依赖「即使 session 拒绝 begin，transport 仍接受请求并设 pending」**（旧 fake 的宽松语义），因此：
- `serialBusy_` 不再被设为 true ⇒ `DLG5b: the read did not make busy true`
- reason 序列改变 ⇒ `C35: the reason was overwritten`
- 无 pending 可丢 ⇒ `R16: no pending request to lose` / `terminals=0`

**⇒ R5 目标 4 项全绿，但全量净结果仍为 70/74（换了 4 项）⇒ 不能宣布 FULL CTEST PASS。**

## N. R1 的 70/74 记录保留

R1 `03_ctest_full.log` **未被覆盖**；R5 新日志为 `R5_09_FULL_CTEST.log`（独立文件）。

## O. 测试 Harness 与生产路径隔离

- FIX-02 的 `session_.cancel()` 与 `beginOk` 拒绝**仅在 `HarnessWriteTransport`**（`src/main.cpp` 内、仅 `--qml-*-check` argv 分支可达）
- `SerialPortAdapter` / `AnalysisController` / `EngineeringReadController` **未修改**
- FIX-04 判据仅影响门禁断言；负例 probe 仅存在于门禁执行期，**正常 GUI 不创建**
- 未降低任何 CRC / SlaveId / Function / Quantity 断言

## P. WIP 哈希与 Git 安全审计

| 项 | 值 |
|---|---|
| HEAD | `517d78c5e5d780d831af5e6d038d70cfe8747ebc`（**未变**） |
| staged | **0** |
| `git diff --check` | **PASS** |
| WIP 数量 | **50**（未变） |
| CHANGED（vs R5 baseline） | **1 = `src/main.cpp`**（ADDED=0 / REMOVED=0） |
| 冻结 Release / Debug | **MATCH** |
| 归档 79 / 原始证据 75 | **完整** |
| canonical docs | **未改** |
| `git add/commit/push/tag/package` | **未执行** |
| VSPD / COM7-COM8 / 真实 RTU | **未触碰** |
| VERIFIED LKGC | **UNCHANGED** |

## Q. 46 文件原子提交条件

```
SOURCE COMMIT ELIGIBILITY = BLOCKED
```
理由：**全量回归 70/74**（4 项 write 门禁因 FIX-02 回归而红）。**不得提交**。

## R. Real Hardware / OBS / LKGC

`C2 REAL HARDWARE = PENDING / NOT VERIFIED` · `OBS-V4-001/002/003` 状态不变 · `VERIFIED LKGC = UNCHANGED` · `T035-C OVERALL = NOT YET CLOSED`

---

## 遗留问题与下一轮最小修复方案（需 Human 授权）

**FIX-02 需要改为「cancel + retry」而非「直接拒绝」**：

```cpp
auto begin = session_.beginActiveRequest(request);
if (std::get_if<SerialTransactionError>(&begin) != nullptr) {
    // A stale session (terminal cleared pending_ but not the session) must not
    // swallow the NEXT request: reset then retry ONCE, preserving the request
    // (write gates rely on the request being accepted and busy becoming true).
    session_.cancel();
    begin = session_.beginActiveRequest(request);
}
const bool beginOk = std::get_if<SerialTransactionError>(&begin) == nullptr;
if (!beginOk)
    return ActiveStartResult{false, TransportDisposition::NotSent, std::nullopt};
```

**预期**：K 场景仍通过（stale → cancel + retry 成功）；4 项 write 门禁恢复（请求被接受、busy=true、reason 序列不变）；无效 descriptor 仍诚实拒绝。

**验证门禁**：
1. 重建 `modbuslens`；
2. 目标 5 项：`qml_read_result_check` + `qml_register_map_check` + `qml_active_profile_check` + `qml_write_foundation_check` + `qml_production_write_check`；
3. 全量 74 项 ctest 一次（全新记录）。

---

## 最终状态

```text
FIX-01 = PASS
FIX-02 = PASS（K 场景）/ 但引入 4 项 write 门禁回归（CONFIRMED）
FIX-04 = PASS（正例 + 负例）
REGISTER_MAP_SCROLL = VERIFIED（程序化；几何可达；114px 滚动 vs 103.5px 所需）
REGISTER_MAP ASSERTION = PASS（REGISTER MAP CHECK PASS B2-Q01..Q30）
TARGETED REGRESSION = 4/4 PASS
FULL BUILD = PASS（494/494，0 error）
FULL CTEST = 70/74（EXIT 8）— 4 项为 FIX-02 回归（非 R1 的 4 项）
SOURCE-REPRO-R5 = PARTIAL
SOURCE COMMIT ELIGIBILITY = BLOCKED
SOURCE / TEST COMMIT = NOT EXECUTED
C2.2 VIRTUAL SERIAL = HUMAN ACCEPTED / CLOSED
C2 REAL HARDWARE = PENDING
T035-C OVERALL = NOT YET CLOSED
VERIFIED LKGC = UNCHANGED
```
