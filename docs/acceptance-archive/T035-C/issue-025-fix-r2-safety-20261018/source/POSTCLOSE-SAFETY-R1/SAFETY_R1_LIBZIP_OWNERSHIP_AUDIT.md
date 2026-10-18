# SAFETY_R1 — libzip Failure-Path Ownership Audit（提交后质量复核）

日期：2026-10-18 18:20
对象：`tests/test_manual_import_pdf_docx.cpp::writeDocx()`（已提交版本 `e5058a4`）
结论：**ERROR_PATH_LEAK = CONFIRMED（修复前）· 已实施最小修复 · 新提交未执行**

---

## 一、所有权真值表（修复前实测源码）

| 路径 | 修复前代码行为 | libzip 契约 | 判定 |
|---|---|---|---|
| **A. `malloc` 失败** | `return false`（不 memcpy、不 free） | — | ✅ 正确 |
| **B. `zip_source_buffer` 失败** | `std::free(copy); return false;` | 未创建 source，调用方拥有 buffer | ✅ 正确 |
| **C. `zip_file_add` 失败（`index < 0`）** | **`return false;`（无 `zip_source_free`）** | **失败时不接管 source，调用方必须释放** | ❌ **Source 泄漏 + `copy` 泄漏**（`freep = 1` ⇒ source 拥有 copy） |
| **D. `zip_file_add` 成功** | 不调用 `zip_source_free` | 已接管 | ✅ 正确 |
| **E. `zip_file_set_dostime` 失败** | `return false`（source 已归 archive） | 由 `zip_close`/`zip_discard` 清理 | ✅ 正确 |
| **F. `zip_close` 失败** | **`zip_close(archive); return ok;`（返回值未检查）** | 失败时 archive 仍有效，必须 `zip_discard` | ❌ **archive 泄漏 + 半写文件被当作成功** |
| **G. 成功路径** | `copy` 由 libzip 释放一次 | — | ✅ 正确 |

```
LIBZIP_OWNERSHIP_TABLE = COMPLETE
ERROR_PATH_LEAK         = CONFIRMED（C：zip_file_add 失败分支）
DOUBLE_FREE_RISK        = NOT FOUND（D 路径未误加 free）
ARCHIVE_CLOSE_CLEANUP   = FAIL（F：close 失败未 discard、未返回失败）
```

## 二、最小修复（仅测试文件，2 处）

```cpp
// C：add 失败 → 调用方释放 source（其 freep=1 会一并释放深拷贝）
if (index < 0) {
    zip_source_free(source);
    return false;
}

// F：close 失败 → discard archive，并如实返回失败
if (zip_close(archive) < 0) {
    zip_discard(archive);
    return false;
}
return ok;
```

**未改动**：XML 内容、条目名、条目顺序、`kFixedDosTime/kFixedDosDate`、`freep=1` 成功路径、产品 `contentHash`、i24/i24b/i25 断言。

## 三、修复后验证

| 项 | 结果 |
|---|---|
| 新隔离目录 `build/t035c-issue025-postclose-safety-r1-20261018/`（预先不存在） | ✅ |
| CONFIGURE | **0** |
| BUILD（目标 `modbuslens_manual_import_pdf_docx_tests`） | **0（9/9，0 error）** |
| `TEST_EXE_SHA256` | `e421ef98e5844c569603b37a949171c1b35cd6e91b98c5171c0b99f1c657cf18` |
| 目标测试 | **EXIT 0 · i24 PASS · i24b PASS · i25 PASS · Totals 35 passed / 0 failed** |
| 全量构建 | **619/619，0 error，50 测试目标** |
| 全量 CTest | 见 `SAFETY_R1_FULL_CTEST.log` |

## 四、失败路径运行期验证的边界

```
FAILURE_PATH_RUNTIME_CHECK = NOT VERIFIED
```
**原因**：本轮未在测试中注入「重复条目名触发 `ZIP_ER_EXISTS`」的探针（§4.2 允许但未要求），也未链接 AddressSanitizer/Dr. Memory。
**⇒ 泄漏结论为静态所有权证明**（依据 libzip 官方契约：`zip_file_add` 失败不接管 source；`zip_close` 失败需 `zip_discard`）。
**未把「测试 EXIT 0」冒充内存泄漏检测结果。**

## 五、上次提交流程偏差登记（ISSUE-025-CLOSE-GIT-R1）

```
偏差：首次 git diff --cached --check 返回 EXIT 2（"new blank line at EOF"）
处置：仅清理本次追加内容的尾部空行（12969 → 12968 B）后重新暂存，复验 EXIT 0
未使用：reset / restore / clean / stash
最终提交范围：2 文件（测试 + ISSUE 文档），越界 0 —— 与审计结果一致
```
