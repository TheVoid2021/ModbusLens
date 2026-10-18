# ISSUE-025 — FIX-R2 与 POSTCLOSE-SAFETY-R1 证据归档

## 归档身份

```text
PROJECT      = ModbusLens
TASK         = T035-C / ISSUE-025
SCOPE        = FIX-R2（DOCX fixture 确定性）+ POSTCLOSE-SAFETY-R1（libzip 错误路径加固）
ARCHIVED_AT  = 2026-10-18
ISSUE-025    = CLOSED（2026-10-18）
ARCHIVE STATE = 本地 Git 证据保全（非远程备份、非 Release、非 LKGC）
```

## 两个来源目录

```
build/t035c-issue025-fix-r2-20261018/_evidence/                      （FIX-R2）
build/t035c-issue025-postclose-safety-r1-20261018/_evidence/         （POSTCLOSE-SAFETY-R1）
```

## 数量与字节（实测）

| 项 | 值 |
|---|---|
| 源文件总数 | **70** |
| 　└ FIX-R2 | **62**（顶层 42 + `dump_run_1..10/` 内 20 个 DOCX） |
| 　└ POSTCLOSE-SAFETY-R1 | **8** |
| 实际复制 | **70** |
| 排除 | **0**（两目录内**无** EXE/DLL/OBJ/PDB/静态库） |
| 源文件总字节 | **858,276** |
| 重复路径 | **0** |

**关于「预期 52」的解释**：上一轮报告的 `ls | wc -l = 52` 是 **`FIX-R2/_evidence` 的目录条目数**（42 个文件 + 10 个 `dump_run_*` 子目录），**不是文件数**。本轮按文件系统实际递归清点：**42 + 20 = 62**。该口径差异已在此登记，不是文件丢失。

**`probe_libzip.cpp/.exe`**：位于 **`build/t035c-issue025-fix-r1-20261018/_fix_evidence/`**（FIX-R1 目录），**不属于本轮两个来源目录**，因此不在本归档内，也不存在「排除的可执行文件」。

## FIX-R2 根因与修复结论

1. **根因（CONFIRMED）**：`writeDocx()` 的 `add` 把 `content.constData()` 交给
   `zip_source_buffer(..., freep = 0)`，而 `[Content_Types].xml` 传入的是
   **临时 `QByteArray(kContentTypes)`**。libzip **直到 `zip_close()` 才读取条目数据**
   ⇒ 悬空指针 ⇒ 读到被复用的堆内存 ⇒ 两次写入的 DOCX 字节不同
   ⇒ `contentHash = SHA-256(源字节)` 不同 ⇒ i24 失败。
2. **次因**：ZIP entry 的 DOS mtime 默认取「当前时间」（2 秒粒度），跨边界即字节不同。
3. **修复**：`add` 内部深拷贝 + `freep = 1`；每条目固定 `zip_file_set_dostime(0x0000, 0x2821)`。
4. **验证**：两份实际 fixture **922 B / SHA-256 相同**
   （`55cb1dd25c307dd7d2e6c6bd1e3e244ae10e61b53cd0d46fda66690d05de898b`）；
   **10/10 独立进程稳定性**（10 轮 SHA 唯一 = 1）；**全量 CTest 74/74 PASS**。

## POSTCLOSE-SAFETY-R1 的两项错误路径修复

1. **`zip_file_add` 失败** ⇒ 调用 `zip_source_free(source)`（libzip 契约：失败不接管；
   该 source 因 `freep = 1` 拥有深拷贝）。
2. **`zip_close` 失败** ⇒ `zip_discard(archive)` 并**如实返回失败**（不得把半写 DOCX 当成功）。
3. 验证：目标测试 **35 passed / 0 failed**（i24/i24b/i25 全 PASS）；
   全量构建 **619/619**；全量 CTest **74/74 PASS（EXIT 0，242.86 s）**。

## 边界声明（重要）

```
DYNAMIC FAILURE-PATH FAULT INJECTION = NOT EXECUTED
LEAK DETECTOR (ASan / Dr. Memory)     = NOT EXECUTED
```
错误路径的正确性为**静态契约证明**（依据 libzip 官方所有权语义），
**不得**把「74/74 通过」读作「异常路径内存安全认证」。

## 与源码提交的对应关系

| 内容 | Git Commit |
|---|---|
| 46 文件源码基线（R8 受测） | `64931eb84827d3b27a2e3d180ea5d92f170a25b0` |
| ISSUE-025 修复 + 关闭 | `e5058a467a7764ecbff11a9acdde19efffd62fc7` |
| 错误路径加固 | `ada9aedc3c9ec41e70c2d579892098d176812831` |

**R8 的 74/74 与 FIX-R2 / SAFETY-R1 的 74/74 属不同轮次，不作合并陈述。**

## 归档性质

**本地 Git 证据保全**，不是 GitHub 远程备份、不是异地备份、不是 Release、不是 Verified LKGC。

## 校验方法

逐行读取 `SHA256SUMS.csv`，对每个 `ArchiveRelativePath` 以**二进制方式**计算 SHA-256
并与 `SHA256` 列比较；无差异行即完整。另可对照 `SOURCE_INVENTORY.csv` 的 `Disposition` 列
（本归档全部为 `COPIED`）。
