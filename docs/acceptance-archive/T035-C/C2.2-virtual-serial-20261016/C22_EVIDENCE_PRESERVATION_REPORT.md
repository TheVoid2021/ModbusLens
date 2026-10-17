# C22_EVIDENCE_PRESERVATION_REPORT.md — T035-C Round C2.2-EVIDENCE-PRESERVE-R1

日期：2026-10-17 18:04
模式：**CONTROLLED EVIDENCE PRESERVATION**（方案 A+B：字节级副本 + SHA-256 索引）
结论：**C2.2 EVIDENCE COPY = VERIFIED**

---

## A. Trusted Resync

已实际读取：`AGENTS.md`、`CODEBUDDY.md`、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、
`docs/tasks/T035-device-intelligence-enhancement.md`、`docs/devlog/2026-10-17.md`、
`V4_R3_HUMAN_FINAL_ACCEPTANCE.md`、`C22_FINAL_DOCSYNC_REPORT.md`。

| 检查 | 结果 |
|---|---|
| HEAD | `3adaea4282a2282a3d340f3811b433d334031800` **MATCH** |
| 冻结 Release SHA256 | `4044edae6933ed98acd6424483399dbcdd5185b26840ebfd6dc5008f14a93fe5` **MATCH** |
| C2.1 Debug SHA256 | `9cadc2c53d80e7b74e9a4bf65dd042068d58b78a2e4e128f7b573c9445876915` **MATCH** |
| staged | **0** |
| `git diff --check` | **PASS** |

## B. 修改前 WIP baseline

编辑前重新取得实际 baseline：`git status --porcelain -uall` = **57 项**（**不是 56**——上轮 DOCSYNC 新增的
`docs/devlog/2026-10-17.md` 使其 +1）。快照保存并在 §M 做差异审计。
**未执行** reset / restore / checkout / clean / stash。

## C. 源证据文件总数与字节总量（编辑前实测）

```text
源目录   = build/acceptance/session-t035c-round-c22-20261016/
FILE_COUNT = 75
TOTAL_BYTES = 208,184
```

**与上轮 DOCSYNC 报告「74 文件 / 约 319K」的差异解释**：
- **+1 文件** = 上轮新建的 `C22_FINAL_DOCSYNC_REPORT.md`（10,355 B）；74 → **75**
- **字节差异**：上轮 319K 是 `du` 的**磁盘占用**（含簇对齐），本轮 208,184 B 是**实际文件字节和**——两者口径不同，非文件丢失。
- 逐文件清单见 `SHA256SUMS.csv`（75 行）。

## D. 扩展名与敏感数据预审（全部 75 文件）

| 扩展名 | 数量 |
|---|---|
| `.md` | 29 |
| `.log` | 30 |
| `.ps1` | 5 |
| `.txt` | 4 |
| `.flag` | 7（外部从站运行期就绪标记，内容 `READY`） |

| 预审项 | 结果 |
|---|---|
| 敏感信息（api key / secret / password / 私钥 / sk- 令牌）扫描 | **0 命中** |
| 二进制/可执行（exe/dll/sys/pdb/obj/lib/dmp/zip 等） | **0 个** |
| 符号链接 / 目录联接 / 指向源目录外 | **0 个** |
| 大文件（> 200 KB） | **0 个** |
| 正在写入/持续变化文件 | 无（测试早已结束；复制前后源 SHA 全等，见 §I `SOURCE_CHANGED_DURING_COPY = 0`） |

**未复制整个 `build/`**；仅归档本 session 的证据目录。

## E. 源证据目录

`build/acceptance/session-t035c-round-c22-20261016/`（75 文件，全部位于根层，无子目录；`logs/` 目录为空）

12 项关键证据逐一 `Test-Path` 确认存在：`V4_R3_HUMAN_FINAL_ACCEPTANCE.md`、`V4_R2_FINAL_REPORT.md`、
`V4_R1_NORMAL_TXRX.log`、`V4_R1_ERROR_MATRIX.log`、`V4_R1_GUI_EVIDENCE.md`、`V4_R1_POST_ENVIRONMENT.md`、
`C22_FINAL_DOCSYNC_REPORT.md`、`V3_R3_FINAL_REPORT.md`、`V3_R2_V2_RETEST.log`、`V3_R3_FULL_MATRIX.log`、
`V4_R2_E1_NORMAL_slave.log`、`V4_R2_E7_FRAGMENTED_slave.log`。

## F. 归档目标目录

```text
docs/acceptance-archive/T035-C/C2.2-virtual-serial-20261016/
├── README.md                              (A–H 说明)
├── SHA256SUMS.csv                         (75 行完整性清单)
├── source/                                (75 个原始证据文件的字节级副本)
└── C22_EVIDENCE_PRESERVATION_REPORT.md    (本报告)
```

**Git 忽略检查**：`git check-ignore -v` 对 `README.md` 与 `source/V4_R2_FINAL_REPORT.md` 均返回
**exit=1（未被忽略）** ⇒ 归档目录可被 Git 追踪（本轮未 add）。

## G. 复制方法

PowerShell `Get-ChildItem -File -Recurse -Force` + `Copy-Item -LiteralPath`（逐文件），
**不转码、不重写行尾、不格式化 Markdown、不重新生成日志、不改动源文件**。
遇到已存在目标：SHA-256 相同则 SKIP，不同则记 CONFLICT 并停止。

执行结果：**COPIED = 75 · SKIPPED(identical) = 0 · CONFLICTS = 0**。

## H. 文件数量与路径集合校验

```text
SOURCE_FILES = 75
ARCHIVE_FILES = 75
FILE_COUNT_MATCH = YES
RELATIVE_PATH_SET_MATCH = YES   （逐文件按相对路径一对一映射，无额外副本）
UNEXPECTED_SOURCE_COPIES = 0
```

## I. SHA-256 全量校验结果

| 指标 | 结果 |
|---|---|
| MISSING_FILES | **0** |
| SHA256_MISMATCH | **0** |
| SIZE_MISMATCH | **0** |
| SOURCE_CHANGED_DURING_COPY | **0**（复制前/后重算源哈希全等） |

**结论：全部 75 个原始文件 SHA-256 逐一一致 ⇒ 字节级复制成功。**
（非仅数量一致；源文件未被任何方式修改以使哈希吻合。）

## J. README 与 Manifest 状态

- `README.md`：已生成，含 A 身份 / B 来源 / C 结果 / D 能力边界 / E 保留观察 / F 复验方法 /
  **G 证据来源限制** / **H Git 状态（未提交）** / 备注（`.flag` 性质）。
- `SHA256SUMS.csv`：**75 行**，字段 `RelativePath,SizeBytes,SHA256`，按 `RelativePath` 排序（可复现）；
  **路径唯一（0 重复）**；**不含** README / 清单自身 / 本报告的条目；未捏造时间戳或哈希。
- **独立复验**：读取 CSV 重新计算目标文件哈希 → `RE_VERIFY_ROWS = 75 · MISSING = 0 · MISMATCH = 0`。

## K. canonical 引用同步

仅在复制与校验全部成功后执行，各追加一条（幂等标记 `C22-EVIDENCE-ARCHIVE-2026-10-17`）：

| 文件 | 追加内容 |
|---|---|
| `docs/PROJECT_STATUS.md` | 末尾表格 +1 行：副本位置 / 清单位置 / 校验指标 / 原件仍保留 / 验收结论不变 / Git 未提交 / 真实硬件未验证 |
| `docs/tasks/T035-device-intelligence-enhancement.md` | 新增 `### 11.16 证据归档副本` 小节（同上要点 + 截图未落盘说明） |

**未重复追加**（标记存在即 SKIP）；**BACKLOG / devlog 未改**（本轮无需）。

## L. Git 忽略与可追踪性检查

| 项 | 结果 |
|---|---|
| 归档目录被忽略？ | **否**（check-ignore exit=1） |
| 归档文件当前状态 | **untracked（`??`）** —— `ARCHIVE COPY VERIFIED / GIT TRACKING PENDING` |
| 是否已 `git add` / `commit` | **否**（本轮明确禁止） |

## M. 已有 WIP 保护结果

- baseline 57 项 vs 本轮结束后 `git status` 差异：**唯一变化 = 新增 77 行 `??`（归档 77 文件）**；
  **既有 45 行 `M` 与 11 行 `??` 全部原样保留，无覆盖、无清理、无丢失**。
- 两个 canonical 文档为**末尾追加**（既有正文未动）。

## N. Git / EXE 安全审计

| 项 | 结果 |
|---|---|
| HEAD | 未变 ✅ |
| staged | 0 ✅ |
| `git diff --check` | PASS ✅ |
| 冻结 Release / C2.1 Debug SHA256 | 均 MATCH ✅ |
| 产品源码 / QML / CMakeLists / tests | **零修改** |
| Device Profile / VSPD / 驱动 / Windows 注册表 | **零修改** |
| 启动程序 / 打开端口 / 发送 RTU / 重编译 / CTest | **全部未执行** |
| 原始 `build/acceptance/` 内容 | **未被修改**（复制只读 + 源哈希前后一致） |
| commit / push / tag / package / LKGC | **全部未执行** |

## O. 尚未完成的持久化措施

1. **Git 追踪未建立**：归档仍为 untracked；需下一轮单独授权**精确暂存范围**（避免混入既有 45 项 WIP），再 commit。
2. **外部备份未验证**：无远程仓库、无网盘/离线副本。
3. **原件的 build 清理风险仍在**：`build/` 仍被 `.gitignore` 忽略；副本虽已建立，但**副本尚未进入 Git** ⇒ 若此时清理 build 且丢失工作区，证据仍可能仅存于本机磁盘。
4. **Human 截图未落盘**：归档不含截图原文件（仅对话记录）。
5. 本轮未对 HTML/图片类证据做任何处理（源目录本无图片）。

## P. 本轮实际修改文件清单

| 文件 | 操作 |
|---|---|
| `docs/acceptance-archive/T035-C/C2.2-virtual-serial-20261016/source/`（75 文件） | **新建**（字节级副本） |
| `docs/acceptance-archive/T035-C/C2.2-virtual-serial-20261016/SHA256SUMS.csv` | **新建**（75 行） |
| `docs/acceptance-archive/T035-C/C2.2-virtual-serial-20261016/README.md` | **新建** |
| `docs/acceptance-archive/T035-C/C2.2-virtual-serial-20261016/C22_EVIDENCE_PRESERVATION_REPORT.md` | **新建**（本文件） |
| `docs/PROJECT_STATUS.md` | **追加** 1 行 |
| `docs/tasks/T035-device-intelligence-enhancement.md` | **追加** `### 11.16` 小节 |

归档目录统计：**77 文件（75 source + 2 元文件）**。

## Q. 后续建议

1. **下一轮（需授权）**：精确 `git add docs/acceptance-archive/T035-C/C2.2-virtual-serial-20261016/`（**仅该目录**，
   不触碰 45 项既有 WIP）+ 单独 commit，使证据真正进入 Git 历史；
2. 建立**外部备份**（离线/云端），覆盖 Git 未覆盖的场景；
3. 若 Human 希望归档含 GUI 截图，请将截图文件放入源目录或归档目录（并更新清单）；
4. 长期：把「每次 acceptance 结束即归档 + 清单」写入流程规范（避免下次再补）。

---

## 状态

```text
C2.2 EVIDENCE COPY = VERIFIED
SOURCE FILES = UNMODIFIED
SHA256 MANIFEST = COMPLETE
ARCHIVE GIT-CANDIDATE = READY
ARCHIVE GIT COMMIT = NOT EXECUTED
EXTERNAL BACKUP = NOT VERIFIED

C2.2 VIRTUAL SERIAL = HUMAN ACCEPTED / CLOSED
C2 REAL HARDWARE = PENDING
T035-C OVERALL = NOT YET CLOSED
VERIFIED LKGC = UNCHANGED
```

**STOP — RETURN TO HUMAN / CHATGPT REVIEW。**
