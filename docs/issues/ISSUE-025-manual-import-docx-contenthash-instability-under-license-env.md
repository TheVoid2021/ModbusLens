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
