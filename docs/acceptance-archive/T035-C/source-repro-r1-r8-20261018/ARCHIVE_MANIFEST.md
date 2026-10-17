# ARCHIVE_MANIFEST — T035-C SOURCE-REPRO R1–R8

| 项 | 值 |
|---|---|
| 归档目录 | `docs/acceptance-archive/T035-C/source-repro-r1-r8-20261018/` |
| 归档日期 | 2026-10-18 |
| 原始证据文件数 | **96** |
| 原始证据总字节 | **1,091,651** |
| Manifest 数据行 | **96**（含表头共 97 行） |
| R8 源码基线 | `64931eb84827d3b27a2e3d180ea5d92f170a25b0` |
| R8 隔离 EXE（**未入库**） | `b1d483e350dabe41325580849236e9ffdfc7312239e09c776c3c5e880be13146` |
| 冻结 Release EXE（未改） | `4044edae6933ed98acd6424483399dbcdd5185b26840ebfd6dc5008f14a93fe5` |
| 冻结 Debug EXE（未改） | `9cadc2c53d80e7b74e9a4bf65dd042068d58b78a2e4e128f7b573c9445876915` |

## 各轮文件数

| ROUND | FILES |
|---|---|
| R1 | 15 |
| R2 | 5 |
| R3 | 7 |
| R4 | 16 |
| R5 | 18 |
| R6 | 12 |
| R7 | 7 |
| R8 | 16 |
| **合计** | **96** |

## 目录结构

```text
source-repro-r1-r8-20261018/
├── README.md
├── ARCHIVE_MANIFEST.md
├── SHA256SUMS.csv              (96 数据行 + 表头)
├── .gitattributes              (* -text whitespace=cr-at-eol)
└── source/
    ├── R1-R3/   (27 files；R1 15 + R2 5 + R3 7，按 Manifest 的 ROUND 列归属)
    ├── R4/      (16 files)
    ├── R5/      (18 files)
    ├── R6/      (12 files)
    ├── R7/      (7 files)
    └── R8/      (16 files)
```

**说明**：R1/R2/R3 的原始证据位于同一物理目录（`build/t035c-source-repro-20261017/_repro_evidence/`），归档时保持扁平副本以**不改变原始文件名与相对结构**；各文件的实际轮次由 `SHA256SUMS.csv` 的 `ROUND` 列逐行标注（`R2_*` → R2、`R3_*` → R3、其余 01–15 → R1）。

## 包含 / 排除

**包含**：原始构建日志、CTest 全量/单项输出、目标回归日志、诊断 trace、RCA 与机制分析、最终报告、`LastTest` 副本、合并说明。

**排除**：`modbuslens.exe`、`.dll/.obj/.o/.pdb`、`CMakeCache.txt`、`build.ninja`、构建目录其余内容、用户 Profile、License 文件、任何凭据、与本轮无关的文件。

## 字节保真

每个文件三重校验：

```text
ORIGINAL_SHA256 == COPIED_SHA256 == STAGED_BLOB_SHA256
```

第三项在 `git add` 之后从 Git index 的原始字节（`git cat-file blob :<path>`）计算。

## 安全审计摘要

| 检查 | 结果 |
|---|---|
| 真实 API Key / Token（`sk-` / `ghp_` / `github_pat_` / `AKIA` / `xox[bap]-`） | **0** |
| 私钥（`BEGIN ... PRIVATE KEY`） | **0** |
| `Authorization: Bearer <token>` | **0** |
| 密码赋值 | **0** |
| License Key 赋值 | **0** |
| 数据库连接串（带凭据） | **0** |
| 邮箱地址 | **0** |
| `QTLICD_*` 原文值 | **0**（仅变量名亦未出现） |
| 非 localhost URL | 60 处，**全部为 Qt 官方文档链接**（`doc.qt.io` / `dejavu-fonts.github.io`），非敏感 |

## 校验方法

在归档根目录执行：逐行读取 `SHA256SUMS.csv`，对每个 `ARCHIVE_RELATIVE_PATH` 计算 SHA-256 并与 `SHA256` 列比较；无差异行即完整。比较时**必须使用字节模式读取**（避免文本模式换行转换）。
