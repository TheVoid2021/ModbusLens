# T035-C 源码可复现性证据归档（R1–R8）

## 归档身份

```text
PROJECT            = ModbusLens
TASK               = T035-C
SCOPE              = SOURCE-REPRO R1–R8（源码复现 / 自动化回归证据）
ARCHIVED_AT        = 2026-10-18
R8 SOURCE COMMIT   = 64931eb84827d3b27a2e3d180ea5d92f170a25b0
R8 SOURCE PARENT   = 517d78c5e5d780d831af5e6d038d70cfe8747ebc
ARCHIVE STATE      = 本地 Git 证据保全（非远程备份、非 Release、非 LKGC）
```

## 各轮日期与目的

| 轮次 | 日期 | 目的 | 结果 |
|---|---|---|---|
| **R1** | 2026-10-17 | 隔离构建 + 首次全量 ctest（`build/t035c-source-repro-20261017`） | 构建 **628/628 PASS**；**CTest 70/74**（4 项失败） |
| **R2** | 2026-10-17 | 四项失败 RCA（静态 + 证据审计） | RCA = PARTIAL；**`RELEASE COMPARISON = NOT EQUIVALENT`**（Release 不含 C1/C2 源码） |
| **R3** | 2026-10-17 | 四项机制验证 + 最小修复决策门禁 | RCA = PARTIAL（readiness CONFIRMED；K 场景/布局/semantic 分级） |
| **R4** | 2026-10-17 | 受控最小修复（FIX-01 readiness 隔离、FIX-02 K125 装置、FIX-04 semantic、regMap 诊断） | FIX-01/02 PASS；FIX-04 待复验；regMap `SCROLL GEOMETRY FEASIBLE` |
| **R5** | 2026-10-17 | semantic 复验 + regMap 滚动契约 + 全量回归 | 目标 4/4 PASS；**全量 ctest 70/74**（4 项 write 回归，根因 FIX-02） |
| **R6** | 2026-10-17 | Harness 生命周期 RCA + `closePort` 修复 | FIX-02b 部分有效；**目标 4/7**（C35/R16 仍失败） |
| **R7** | 2026-10-17 | C35/R16 因果 trace（instrumentation-only） | **C35/R16 同源 CONFIRMED**（成员/局部 session 混用） |
| **R8** | 2026-10-17/18 | FIX-02c 完成路径清理 + 全量回归 | **构建 628/628 · 目标 7/7 · 全量 ctest 74/74（EXIT 0）** |

## 原始证据来源

```
R1/R2/R3  build/t035c-source-repro-20261017/_repro_evidence/       （共 27 文件，见 Manifest 的 ROUND 列）
R4        build/t035c-source-fix-20261017/_fix_evidence/           （顶层 16 文件）
R5        build/t035c-source-fix-20261017/_fix_evidence/R5/        （18 文件）
R6        build/t035c-source-repro-r6-20261017/_repro_evidence/    （12 文件）
R7        build/t035c-source-diagnostic-r7-20261017/_diagnostic_evidence/（7 文件）
R8        build/t035c-source-fix-r8-20261017/_fix_evidence/        （16 文件）
```

**归档源文件总数 = 96 · 总字节 = 1,091,651**
`source/R1-R3/` 为共用目录的扁平副本；每个文件的实际轮次见 `SHA256SUMS.csv` 的 `ROUND` 列。

## R8 通过结论

```
R8 BUILD           = 628/628 PASS（0 error）
R8 TARGETED        = 7/7 PASS
R8 FULL CTEST      = 74/74 PASS（CTEST_EXIT_CODE = 0，235.12 s，单次完整运行）
R8 ISOLATED EXE    = b1d483e350dabe41325580849236e9ffdfc7312239e09c776c3c5e880be13146
```

**R8 EXE 未进入本归档**（`modbuslens.exe` / DLL / CMakeCache / build.ninja 一律排除）。

## 历史失败记录（必须保留）

```
R1 FULL CTEST = 70/74（4 项失败：register_readiness / qml_read_result_check /
                       qml_register_map_check / qml_active_profile_check）
R5 FULL CTEST = 70/74（4 项失败：manual_import_pdf_docx / qml_write_foundation_check /
                       qml_production_write_check / qml_write_foundation_check_windows）
R6 TARGETED   = 4/7（C35 / R16 / write_foundation_windows 仍失败）
R8 FULL CTEST = 74/74
```
**早期失败报告全部原样保留，未删除、未改写。**

## ISSUE-025 边界

```
ISSUE-025 = OPEN / UNRESOLVED
R5 manual_import_pdf_docx = FAIL（contentHash mismatch）
R8 manual_import_pdf_docx = PASS（1.90 s，单次）
```
**一次通过不代表 flaky 缺陷已经修复**；该 Issue 保持 OPEN，需独立修复或书面的风险接受。

## 冻结 EXE（本归档未包含、未改变）

```
Release = 4044edae6933ed98acd6424483399dbcdd5185b26840ebfd6dc5008f14a93fe5
Debug   = 9cadc2c53d80e7b74e9a4bf65dd042068d58b78a2e4e128f7b573c9445876915
```

## 真实硬件

`C2 REAL HARDWARE = PENDING / NOT VERIFIED`（本归档不构成任何硬件验收）

## 归档性质

**本地 Git 证据保全**，不是 GitHub 远程备份、不是异地备份、不是 Release、不是 Verified LKGC。
原始文件字节由 `source/` 下的副本 + `SHA256SUMS.csv` + 本目录 `.gitattributes`（`* -text whitespace=cr-at-eol`）共同保证。

## 校验方法

```powershell
Import-Csv .\SHA256SUMS.csv | ForEach-Object {
    $p = Join-Path . $_.ARCHIVE_RELATIVE_PATH
    $h = (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLower()
    [PSCustomObject]@{ Path=$_.ARCHIVE_RELATIVE_PATH; Match=($h -eq $_.SHA256) }
} | Where-Object { -not $_.Match }
```
无输出 = 全部一致。
