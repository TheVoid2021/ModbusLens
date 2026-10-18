# ARCHIVE_MANIFEST — ISSUE-025 FIX-R2 + POSTCLOSE-SAFETY-R1

| 项 | 预期 | 实测 |
|---|---|---|
| EXPECTED_SOURCE_FILES | 60（指令值） | — |
| ACTUAL_SOURCE_FILES | — | **70** |
| COPIED_SOURCE_FILES | — | **70** |
| EXCLUDED_FILES | — | **0** |
| TOTAL_SOURCE_BYTES | — | **858,276** |
| DUPLICATE_PATHS | 0 | **0** |

**差异解释（必读）**：
`FIX-R2/_evidence` 的目录条目数为 52（**42 文件 + 10 个 `dump_run_*` 子目录**），
上一轮报告误把条目数当作文件数。本轮递归清点：**42 + 20（子目录内 DOCX）= 62**；
加 `POSTCLOSE-SAFETY-R1` 的 8 ⇒ **70**。**不是文件丢失，也无内容缺失。**

## 分组

| SourceGroup | 文件数 | 说明 |
|---|---|---|
| **FIX-R2** | **62** | 顶层 42（27 log / 12 txt / 2 docx / 1 md）+ `dump_run_1..10/` 各 2 个 DOCX（共 20） |
| **POSTCLOSE-SAFETY-R1** | **8** | 全部顶层（configure/build/targeted/full-ctest 日志 + 审计 md） |
| **合计** | **70** | |

## 目录结构

```text
issue-025-fix-r2-safety-20261018/
├── README.md
├── ARCHIVE_MANIFEST.md
├── SOURCE_INVENTORY.csv      (70 数据行 + 表头；含 Disposition / ExclusionReason)
├── SHA256SUMS.csv            (70 数据行 + 表头)
├── .gitattributes            (* -text whitespace=cr-at-eol)
└── source/
    ├── FIX-R2/               (42 files)
    │   └── dump_run_{1..10}/ (各 2 个 .docx，共 20)
    └── POSTCLOSE-SAFETY-R1/  (8 files)
```

## 排除项

**无。** 两个来源目录内不存在 `EXE / DLL / OBJ / O / PDB / 静态库`。
`probe_libzip.cpp` 与 `probe_libzip.exe` 位于 **FIX-R1** 目录
（`build/t035c-issue025-fix-r1-20261018/_fix_evidence/`），**不属本轮来源范围**，
因此既不复制也不计入排除统计。

## DOCX 样本审计

`actual_i24_a.docx` / `actual_i24_b.docx`（及 `dump_run_*` 内 20 份）均为**测试运行时生成**的最小 DOCX：

```
[Content_Types].xml   429 B
_rels/.rels           297 B
word/document.xml     213 B
文件大小               922 B（全部 22 份一致）
```
内容为固定测试文本（`重复导入` / `内容甲` / `内容乙`），**不含任何用户数据**。

## 字节保真

每个文件三重校验：`ORIGINAL_SHA256 == COPIED_SHA256 == STAGED_BLOB_SHA256`（第三项在 `git add` 后从 index 原始字节计算）。

## 安全审计摘要

| 检查 | 结果 |
|---|---|
| 真实 API Key / Token（`sk-` / `ghp_` / `github_pat_` / `AKIA` / `xox[bap]-`） | **0** |
| 私钥（`BEGIN ... PRIVATE KEY`） | **0** |
| `Authorization: Bearer <token>` | **0** |
| 密码赋值 / 带凭据连接串 | **0 / 0** |
| 邮箱地址 | **0** |
| 二进制可执行文件 | **0** |
