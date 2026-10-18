# R2_02 / R2_04 / R2_06 — libzip Buffer 生命周期 RCA + 实际 DOCX 字节对照 + 根因判定

## 一、`SOURCE_BUFFER_LIFETIME_BUG = CONFIRMED`（源码级）

### 危险三条件（全部成立）

```cpp
zip_source_buffer(archive, content.constData(), content.size(), 0);
//                              ↑ 指针                          ↑ freep = 0（不拷贝、不拥有）

bool ok = add(QStringLiteral("[Content_Types].xml"), QByteArray(kContentTypes));
//                                                  ↑ 临时 QByteArray（prvalue）
```

| 条件 | 判定 |
|---|---|
| `freep = 0` | **成立** |
| `DATA = TEMPORARY_QBYTEARRAY.constData()` | **成立（仅 `[Content_Types].xml` 这一条）** |
| `QBYTEARRAY_LIFETIME < ZIP_CLOSE` | **成立**（临时对象在 `add()` 返回时析构；`zip_close()` 在函数末尾） |

**libzip 官方契约**：`zip_source_buffer()` 引用的缓冲区必须在 source 使用期间有效；条目数据**直到 `zip_close()` 才被真正读取/压缩**。因此 `[Content_Types].xml` 的数据在 `zip_close()` 时**已释放**，读取的是**被复用的堆内存**。

### 逐项检查（§2.1 十项）

| # | 检查项 | 结论 |
|---|---|---|
| 1 | `add` lambda 参数类型 | `(const QString &name, const QByteArray &content)` |
| 2 | 数据来源 | `content.constData()` |
| 3 | `freep` 实际值 | **0（不拥有）** |
| 4 | 是否用 `constData()` | **是** |
| 5 | 是否临时变量 | **`[Content_Types].xml` 传入 `QByteArray(kContentTypes)` ⇒ 是**；`rels` / `document` 为具名局部变量 ⇒ 否 |
| 6 | lambda 返回后内存是否有效 | **临时对象已析构 ⇒ 无效** |
| 7 | ZIP source 读取时机 | **`zip_close()`**（libzip 契约） |
| 8 | `zip_close` 时机 | `writeDocx()` 末尾（临时对象已销毁） |
| 9 | 是否存在 detach/reallocate/销毁 | **临时 `QByteArray` 析构 ⇒ 释放** |
| 10 | source 所有权转移 | `zip_file_add` 成功后 source 归 archive；但**它指向的缓冲区不属于 libzip**（`freep=0`） |

### 为什么独立 probe 成功、真实 i24 失败

| | probe（上一轮） | 真实 i24 |
|---|---|---|
| 条目数据来源 | **`const char*` 字符串字面量**（静态存储，进程生命周期） | **`QByteArray(kContentTypes)` 临时对象** |
| `freep` | 0 | 0 |
| `zip_close()` 时指针有效性 | **有效** | **悬空** |
| 结果 | **两次字节完全相同** | **两次字节不同（读到的堆残留不同）** |

**⇒ 这不是「压缩器不确定性」，而是调用方把临时缓冲区交给异步读取的库。**

## 二、i24 实际输入（§3）

| 项 | DOCX A | DOCX B |
|---|---|---|
| 路径 | `<testTemp>/i24-a.docx` | `<testTemp>/i24-b.docx` |
| `writeDocx` 参数 | `rels` / `document` / 默认 `extra = {}` | **完全相同** |
| 预写 | `writeBytes(first, QByteArray())`（0 字节占位） | 无 |
| ZIP 打开标志 | `ZIP_CREATE \| ZIP_TRUNCATE` | 同 |
| 条目顺序 | `[Content_Types].xml` → `_rels/.rels` → `word/document.xml` | 同 |
| 压缩设置 | libzip 默认（deflate） | 同 |

**⇒ 输入参数确实相同；差异不来自代码路径，而来自 `[Content_Types].xml` 的缓冲区生命周期。**

## 三、修复后实际 DOCX 字节对照（§4，`MODBUSLENS_ISSUE025_DUMP` 实测）

```
actual_i24_a.docx   922 B   sha256 = 55cb1dd25c307dd7d2e6c6bd1e3e244ae10e61b53cd0d46fda66690d05de898b
actual_i24_b.docx   922 B   sha256 = 55cb1dd25c307dd7d2e6c6bd1e3e244ae10e61b53cd0d46fda66690d05de898b
FIRST_DIFFERING_BYTE_OFFSET = N/A（完全相同）
TOTAL_DIFFERING_BYTES = 0
```
十次独立进程的 10 份 dump **SHA 全部相同**（`UNIQUE_DOCX_SHAS = 1`）⇒ **字节确定性达成**。

## 四、修复内容（§6 情况 A，仅测试文件）

```cpp
const auto add = [archive](const QString &name, const QByteArray &content) {
    // libzip reads a source-buffer entry at zip_close(), NOT at add time.
    // Callers may pass a TEMPORARY QByteArray (e.g. QByteArray(kContentTypes));
    // its storage dies at the end of this call -> dangling pointer.
    const auto size = static_cast<std::size_t>(content.size());
    auto *copy = static_cast<char *>(std::malloc(size == 0 ? 1 : size));
    if (copy == nullptr) return false;
    if (size > 0) std::memcpy(copy, content.constData(), size);
    zip_source_t *source = zip_source_buffer(archive, copy, size, 1);  // freep = 1
    if (source == nullptr) { std::free(copy); return false; }
    ...
};
```

**所有权与错误路径**：
- 拷贝由 `malloc` 分配，**交给 libzip 释放**（`freep=1`）——**不是把 Qt 的指针交给 `free`**（符合 §2.2 禁令）。
- `zip_source_buffer` 失败 ⇒ 自行 `free(copy)`（libzip 未接管）。
- `zip_file_add` / `zip_file_set_dostime` 失败 ⇒ source 已归 archive，由 `zip_close` 清理；`copy` 由 libzip 释放。
- **XML 内容、条目名、条目顺序、`zip_close` 处理均未改。**

## 五、FIX-A（固定 DOS mtime）是否仍需保留

**需要保留**。理由：
- 本轮确认的悬空指针解释了「每次运行都不同」的**主因**；
- 但 libzip **默认仍以「当前时间」写 entry mtime**（2 秒粒度），若不固定，**跨 2 秒边界的两次写入仍会产生不同字节**（上一轮探针 `DOS_ONLY: identical=1` 的前提是**同一 2 秒窗口**）。
- ⇒ **FIX-A 与 FIX-R2 是互补的两层确定性**：**数据有效性**（深层）+ **元数据固定**（表层）。

## 六、i24b 断言修正（§7）

**移除**：`QCOMPARE(a.document.byteSize, b.document.byteSize)`（**错误前提**：deflate 输出长度取决于内容；实测 918 vs 916）。
**保留/建立**：

```cpp
QVERIFY(a.document.byteSize > 0);
QVERIFY(b.document.byteSize > 0);
QVERIFY(a.document.contentHash != b.document.contentHash);
```
（不同文本 ⇒ 不同源字节 ⇒ 不同 contentHash；**未把 hash 固定为常量、未删除不等断言**）

## 七、根因判定

```
SOURCE_BUFFER_LIFETIME_BUG = CONFIRMED
ISSUE-025 ROOT_CAUSE      = CONFIRMED（测试 Fixture 层；产品 contentHash 语义正确）
分类                      = E. SOURCE_BUFFER_LIFETIME_ERROR（叠加 A. INPUT_BYTES_DIFFER 的后果）
```
