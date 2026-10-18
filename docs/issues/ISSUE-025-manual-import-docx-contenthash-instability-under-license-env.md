# ISSUE-025: `manual_import_pdf_docx` i24 DOCX contentHash Instability（T035-A Completion Gate Run 1/2 失败项）

> 任务关联：**T035-A Completion Gate**（2026-10-14，VERIFICATION ONLY 轮；发现于该轮 §5 Canonical CTest）。
> 状态：**OPEN / UNRESOLVED**（未修复；本轮与登记轮均**未授权**修改测试夹具或源码）。
> 严重度：**中**（不影响产品源码正确性主张；影响门禁可重复性与 CI 可信度）。

## Observed

T035-A Completion Gate 的两次 CTest 全量运行（Run 1 / Run 2，执行环境**额外携带构建期 `QTLICD_*` 重定向变量**）各出现 **71/72**，唯一失败项恒为 **#38 `manual_import_pdf_docx`**：

| 轮次 | 结果 | CTEST_EXIT | 总耗时 | #38 耗时 | #38 CTest Output |
|---|---|---|---|---|---|
| Run 1 | 71/72 | 8 | 458.40s | 4.86s | **为空** |
| Run 2 | 71/72 | 8 | 522.27s | 11.42s | **为空** |
| Run 3（canonical，无 `QTLICD_*`） | **72/72** | **0** | 754.08s | 5.37s | — |

失败子用例 = `i24_sameDocxBytesTwoRecords()`（`tests/test_manual_import_pdf_docx.cpp:695`）：

```
FAIL!  : ManualImportPdfDocxTest::i24_sameDocxBytesTwoRecords() Compared values are not the same
   Actual   (a.document.contentHash): "3cd405b696bb703308482437e1e476eb19cb427c3abf03ede0758aa5d99aeedd"
   Expected (b.document.contentHash): "f074546e05766b101d1359d8b02b4f5e8458564804fce633f2b75562a4c70a44"
```

即：**同一份 DOCX 内容两次写出、两次导入，得到不同的 contentHash**。

## Expected

- 相同字节的源文件两次导入 ⇒ `contentHash` 必须相同（`contentHash` 的语义 = 源文件字节摘要；`documentId` 才承载"两次导入是两条记录"）。
- Canonical CTest = 72/72 / exit 0。

## CTest Output 为空的说明（已排除的解释）

- 该测试二进制 = **Console 子系统**（PE Subsystem = 3，实测），非 GUI 子系统 ⇒ **不是** "WIN32 子系统吞掉诊断" 那一类已知现象。
- **无进程崩溃**：Windows Application 事件日志 20:48–20:56 无任何 `modbuslens` 记录（同期仅有无关第三方应用的事件）；`%LOCALAPPDATA%\CrashDumps` 无本测试转储。
- 最可能解释（**未证明**）：stdout 为管道时 CRT 全缓冲，异常退出路径导致缓冲丢失；但退出机制本身**未取证**。

## Evidence（已证实 / 未证实严格分离）

### 已证实（PROVEN）

1. **Run 1 / Run 2 = 71/72**，失败项恒为 #38（Agent 会话记录 + `GATE-REPORT.md`；Run 1/2 的原始 stdout 日志与 `LastTest.log` **已丢失**，见下"证据缺口"）。
2. **Run 3 canonical（不携带 `QTLICD_*`）= 72/72 / exit 0 / 754.08s** —— 原始证据在案：`build/acceptance/session-t035a-completion-gate-20261014/ctest/LastTest_run3_canonical.log`（`Test Passed.` = 72、`Test Failed` = 0、`deployment_startup_check` 0 命中）与 `logs/t035a_ctest_full_run3.log`。
3. **#38 的失败断言 = `i24` 的 contentHash 不一致**，两个哈希值已完整留档（见上文；持久证据 = `probes/t035a_env_probe_all3.out`）。
4. **携带 `QTLICD_*` 重定向变量（三变量同时存在）的运行更容易复现该失败**：
   - 带三变量：独立运行 **4/5 FAIL**、全量 Run 1/2 各 1 项失败；
   - 不带：独立运行 **6/6 PASS**、CTest 单项 **3/3 PASS**、前置序列（#35→#38）**4/4 PASS**、全量 Run 3 **72/72**；
   - **单变量分别设置**（仅 `QTLICD_CONFIG_DIR` / 仅 `QTLICD_RUNTIME_DIR` / 仅 `QTLICD_LOG_DIR`）**均不触发** ⇒ 观察到的效应属**三变量组合**。
   - 反例（重要）：带三变量 + 并发后台负载（改变机器时序）时**通过** ⇒ 该效应**不是确定性依赖**，判定为**时序敏感**。
5. **该测试不在 T035-A 变更范围内**：`tests/test_manual_import_pdf_docx.cpp` 未出现在工作树 diff 中；其测试二进制 mtime = **2026-10-08**（本轮全量构建未重建它，说明它不依赖 T035-A 变更的头文件）。

### 未证实（NOT PROVEN）

1. **libzip ZIP 时间戳为最终根因** —— 当前最强假设：夹具 `writeDocx()` 经 libzip（`zip_source_buffer` + `zip_file_add`，**未设置显式 mtime**）写出容器，条目 DOS 时间戳取当前时间（**2 秒粒度**）；两次 `writeDocx()` 若落入不同 2 秒桶 ⇒ 字节不同 ⇒ contentHash 不同。**该假设未获最终证明**（按字节抢救两份临时 DOCX 的尝试失败；无直接字节级证据）。
2. **T035-A 代码导致该失败** —— 无证据支持，且有反证（测试不在变更范围；canonical 全量通过）。
3. **问题已被永久修复** —— **未修复**；本轮及登记轮均未授权改动夹具。
4. **`QTLICD_*` 与失败之间的因果机制**（是 license 客户端握手引入的启动延迟？还是其他副作用？）—— 未定位。

## Root Cause

**UNRESOLVED.** 最强假设（未证明）见"未证实 1"。可确定的是：**该失败与 T035-A 变更无关**，其暴露条件是"测试运行环境携带构建期 license 重定向变量"（时序扰动）或等价负载。

## Fix

**NONE（未授权）。** 候选最小修复方向（**仅为提案，等待 Human 裁定，本轮不得实施**）：

- 方向 A：夹具侧——`writeDocx()` 对每个条目设置**确定性 mtime**（如固定时间戳），使"相同内容 ⇒ 相同字节"成为**构造性保证**而非时序巧合；
- 方向 B：契约侧——明确"同内容两次写出必须字节相同"是否为该测试的**有效前提**；若否，改为比较规范化内容而非整数哈希；
- 方向 C：环境侧——把"CTest 运行环境不得携带 `QTLICD_*`"固化为门禁规范（见"回归保护"）。

## Verification

**NOT RUN**（无修复）。可复现对照实验已执行并留档（见 Evidence 已证实 4）。

## Regression Protection

1. **门禁口径（本轮已确立并写入记录）**：canonical CTest **不得携带** `QTLICD_*` 重定向变量——该三变量是**构建期**（license daemon）要求，与测试运行无关；T034 FINAL-GATE-G2 的 72/72 亦未携带。
2. **本 issue 保持 OPEN / UNRESOLVED**：在夹具侧或契约侧获得确定性保证之前，该失败在"非 canonical 环境"或高负载机器上仍可能复现；**不得**将其归档为 FIXED。
3. 若未来把该测试纳入任何自动化门禁，必须先取得夹具确定性的**构造性证据**（而非"多跑几次都过"）。

## Evidence Gaps（证据缺口，如实登记，不补造）

| 项 | 状态 |
|---|---|
| Run 1 / Run 2 的 CTest stdout 日志 | **MISSING**（存于 `%TEMP%`，在归档前被该目录的**不明自动清理**删除） |
| Run 1 的 `LastTest.log` | **MISSING**（先被 Run 2 覆盖，后随 `%TEMP%` 策略失效） |
| Run 2 的 `LastTest.log` | **MISSING** |
| Run 2 的失败清单 | **PRESERVED**：`build/acceptance/session-t035a-completion-gate-20261014/ctest/LastTestsFailed_run2_record.log`（`38:manual_import_pdf_docx`，mtime 21:09）+ `.PROVENANCE.txt` |
| #38 失败详情（含两个哈希） | **PRESERVED**：`probes/t035a_env_probe_all3.out` 等 4 份环境探针输出 |
| 原始构建日志（`BUILD_EXIT=0` / 149 steps） | **MISSING**（同上）；事实仅存于 Agent 会话记录与 `GATE-REPORT.md` |

## 关联

- `docs/tasks/T035-device-intelligence-enhancement.md` §5.8（Gate 验证）与 §7 问题追踪（T035-P08）
- `docs/devlog/2026-10-14.md` §11（Gate 执行）与 §13（登记）
- `build/acceptance/session-t035a-completion-gate-20261014/`（全部幸存证据）

---

# Resolution / Closure Addendum（2026-10-18 追加，不覆盖以上原文）

```text
ISSUE-025 STATUS = CLOSED
CLOSED DATE      = 2026-10-18
RESOLUTION       = TEST FIXTURE DETERMINISM FIX
```

## R1. 原始症状（历史事实，未改写）

```text
R5 manual_import_pdf_docx = FAIL
tests/test_manual_import_pdf_docx.cpp(695) : failure location
Actual   (a.document.contentHash): c44db754e9b368873b49e8b5669374931d5217bcee11986c6320086ed445c199
Expected (b.document.contentHash): a51a5cd4410740e36720cc65f9c9c2637d5e49ecbf681dfb436f69813d37319a
```
R5 为 FAIL，R1 与 R8 同一测试为 PASS —— 这一「不稳定」事实在原文中保留，未作任何修改。

## R2. 最终根因（RCA-R1 + FIX-R1 + FIX-R2 三轮定位）

**双层不确定性叠加，全部位于测试 Fixture 构造，不在产品实现。**

1. **（主因）libzip source buffer 生命周期缺陷**
   `writeDocx()` 的 `add` lambda 把 `content.constData()` 传给
   `zip_source_buffer(..., freep = 0)`（**不拷贝、不拥有**）。而 `[Content_Types].xml`
   这一条传入的是**临时 `QByteArray(kContentTypes)`**：临时对象在 `add()` 返回时析构，
   而 libzip **直到 `zip_close()` 才读取条目数据** ⇒ 悬空指针 ⇒ 读到被复用的堆内存
   ⇒ 两次写入的 `[Content_Types].xml` 内容不同 ⇒ DOCX 源字节不同
   ⇒ `contentHash = SHA-256(源字节)` 不同 ⇒ `i24` 断言失败。

   > 这同时解释了「最小 probe 成功、真实 i24 失败」：probe 用 `const char*` 字面量
   > （静态存储，进程生命周期），不存在悬空。

2. **（次因）ZIP entry 的 DOS 时间戳**
   libzip 默认以「当前时间」写每个 entry 的 mtime（2 秒粒度）。即使数据有效，
   两次写入若**跨过 2 秒边界**，字节仍会不同。

**⇒ 原文 `## Root Cause` 章节中关于 License 环境的怀疑，经三轮机制审计判定为
**历史调查记录**，本次以本 Addendum 作出明确纠正：**

```text
LICENSE ENV = 非本次已确认的根因（三轮 ctest 运行均 unset QTLICD_*）
```

## R3. 修复措施（全部位于 `tests/test_manual_import_pdf_docx.cpp`）

1. **修复 ZIP Source Buffer 所有权与生存期**：`add` 内部 `malloc` + `memcpy` 深拷贝，
   `zip_source_buffer(..., freep = 1)` 由 libzip 释放；`zip_source_buffer` 返回空时
   自行 `free(copy)`（未把 Qt 拥有的内存交给 `free`）。
2. **固定受控 DOCX 条目的 DOS 时间戳**：`zip_file_set_dostime(index, 0x0000, 0x2821, 0)`
   （2000-01-01 00:00:00），不受系统时钟影响。
3. **增加真实 DOCX 字节相等验证**（i24）：比较两份 fixture 的源字节长度与 SHA-256，
   不只比较 XML 文本。
4. **保留「不同内容 ⇒ 不同 Hash」负例**（i24b）。
5. **删除错误的「压缩文件等长」前提**：deflate 输出长度取决于内容（实测 918 vs 916），
   等长不是有效契约。

## R4. 验证证据（仅对应 2026-10-18 的 FIX-R2 受控运行）

```text
I24 = PASS
I24B = PASS
I25 = PASS
MANUAL_IMPORT_TARGET = PASS（35 passed / 0 failed）
DOCX BYTE EQUALITY = VERIFIED（两份 fixture 均 922 B，
                     SHA-256 = 55cb1dd25c307dd7d2e6c6bd1e3e244ae10e61b53cd0d46fda66690d05de898b）
10-RUN STABILITY = 10/10 PASS（独立进程；10 轮 DOCX SHA 唯一 = 1；
                   时间跨 16:54:01–16:56:41 ≈ 80 个 2 秒窗口）
FULL BUILD = 619/619 PASS（0 error）
FULL CTEST = 74/74 PASS（CTEST_EXIT_CODE = 0，260.80 s，单次完整运行）
```

以上为 **FIX-R2 受控运行**的证据，不得与 R1/R5/R8 的历史结果混为同一轮。

## R5. 产品影响（明确边界）

- **产品 ContentHash 算法未修改。**
- **产品契约保持原样**：`contentHash = SHA-256(完整源文件字节)`（`ManualStore::importSourceFile`，`src/ui/manual/ManualStore.cpp`）。
- **未发现此次故障来自产品 Hash 计算** —— 产品对任意真实文件都正确计算源字节摘要。
- **License 环境不是本次已确认的根因**（三轮运行均 `QTLICD_*` unset）。
- **本次关闭的是「测试 Fixture 不确定性」**，不代表所有 DOCX 导入缺陷均已不存在；
  真实硬件 / 真实用户文档场景仍按既有边界管理。

## R6. 历史保真

- 原文 `## Observed` / `## Expected` / `## Evidence` / `## Root Cause` / `## Fix` /
  `## Verification` / `## Evidence Gaps` **全部保留**，未删改。
- R5 的 FAIL 事实与两条 contentHash **保留为历史记录**。
- R1/R5 的历史 FAIL **未改成 PASS**。
- R8（74/74）与 FIX-R2（74/74）**分属不同轮次**，未合并陈述。

## R7. 本地证据位置（未进入 Git）

```text
build/t035c-issue025-rca-r1-20261018/_evidence/     ISSUE025_R1_02_ANALYSIS.md
build/t035c-issue025-fix-r1-20261018/_fix_evidence/ （FIX-R1：已实施但未通过的中间态）
build/t035c-issue025-fix-r2-20261018/_evidence/     R2_02_BUFFER_LIFETIME_AUDIT.md、
                                                    R2_08/R2_09/R2_10/R2_11 日志、
                                                    actual_i24_a.docx / actual_i24_b.docx
```

```text
FIX-R2 RAW EVIDENCE = LOCAL / GITIGNORED
FIX-R2 EVIDENCE GIT ARCHIVE = NOT EXECUTED
FIX-R2 EVIDENCE EXTERNAL BACKUP = NOT VERIFIED
```
上述日志**尚未归档进 Git**；如需要，应另行发起受控证据归档任务。
