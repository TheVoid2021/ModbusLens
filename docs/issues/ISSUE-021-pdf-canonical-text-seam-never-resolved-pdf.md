# ISSUE-021: PDF 说明书导入成功且 preview 有文本，但 AI 提取报 no_canonical_text

> 来源：真实使用场景——比色传感器说明书（`模拟量输入系列使用手册(RS485版).pdf`）
> 导入测试（T033）。

## Symptom（现象）

- PDF import 状态 = ready；
- Manual preview 能显示抽取出的中文文本（页码、中文标题、目录文字）；
- 点击"AI 提取候选"失败，error category = `no_canonical_text`。

准确的初始描述：**"PDF has displayable extracted text, but downstream
candidate-extraction pipeline reports `no_canonical_text`"**——不是
"PDF extraction completely failed"。

## Impact（影响）

所有 PDF 说明书（有文本层的）都无法进入 AI candidate extraction 与
Manual Q&A（同样消费 canonical seam，报 `qa_no_canonical_text`）。
TXT / Markdown / DOCX 不受影响。

## Reproduction（复现）

1. 导入任意**有文本层**的 PDF（真实样本：1,886,074 B / PDF-1.7 / 17 页 /
   9 个 ToUnicode CMap / 36 `Tj` + 21 `TJ` 文本操作符 / 含空用户口令式
   `/Encrypt`——pdfium 可打开，非阻断）。
2. import = ready，preview 显示中文文本。
3. 选中该说明书，触发 AI 提取候选 → `no_canonical_text`。

自动化复现 = `candidate_orchestration::o21_textLayerPdfReachesConsentGate`
（REAL RED，见 Verification）。

## Root Cause（根因）

`ManualStore::importSourceFile` 对 PDF 的 cache truth 是**每页 JSON**
（`text/<contentHash>.json`，`{"pages":[...]}`，T027 §60.4 冻结语义），
从不写 `.txt`。而 canonical-text seam
`ManualStore::loadText(contentHash, ok)` **只读** `text/<contentHash>.txt`
——对 PDF 永远 `ok=false`。

数据流断点：

```text
PDF import → format-specific extraction → per-page truth →
  ├→ cache = <hash>.json（每页数组）──→ preview（loadPdfPages + 显示用页头）✓
  └→ cache = <hash>.txt（不存在！）  ←── loadText（candidate/QA/revalidate）✗
```

即：**PDF 的「每页 truth → canonical text」聚合路径从未存在**。
preview 的文本（`buildPdfPreview`）带显示用页头（"第 N 页"），
刻意不是 cache truth，因此也不可作为 canonical 文本来源。

被逐项否定的假设：B（无 canonicalization 步骤，不存在"归一化吃掉文本"）、
C（selection 与 extraction 使用同一 `selectedDocument()`）、
D（`status=Ready` 以 managed artifacts 存在为前提）、
F（ToUnicode CMap 存在，text layer 真实有效）、
G（同步读取，无 stale/async）、OCR 假设（text layer 存在，§6 否定）。

## Fix（修复）

责任所有权层（`ManualStore` = cache truth owner），非 AI/QML workaround：

1. **`ManualStore::loadText`**：`.txt` 不存在时回退读取 PDF 每页 `.json`
   cache，做**确定性聚合**——页序不变、页间单 `"\n"` 连接、**不含显示用页头**。
   无文本 PDF 的聚合结果 = 空串且 `ok=true`（真实"无文本"，绝不造文本）。
2. **`CandidateExtractionController::requestExtraction`**：
   `extractionStateToken == "no_extractable_text"` → 失败于准确的
   `no_extractable_text` 分类（"该说明书不包含可提取的文本（OCR 暂不支持）"）；
   canonical 文本为空 → 维持 `no_canonical_text` fail-closed；
   `documentType` 由原文件名经冻结扩展名规则推导（原硬编码 `Txt`）。
3. **`ManualQaController::beginAttempt`**：canonical 文本为空 → 维持
   `qa_no_canonical_text` fail-closed（不得向 provider 派发空上下文）。

## Verification（验证）

- **REAL RED**（精确逆向三处修复后重建）：`i31`/`i32`/`o21`/`o22`/`qa42`
  全部 FAIL；`o23`/`qa43` 双态 GREEN（不回归守卫）。
- **GREEN**（重放修复后）：`modbuslens_manual_import_pdf_docx_tests` 34/34、
  `modbuslens_candidate_orchestration_tests` 25/25、`modbuslens_manual_qa_tests`
  43/43。
- Full Release CTest 见任务档案 T033。

## Lessons（教训）

1. **一个格式进入受管存储时，"每页 truth"与"canonical 文本"两个视图必须同时
   有着落**——C1b 设计了 PDF 每页 JSON 缓存与 `loadPdfPages`，但 canonical
   seam 只接了 TXT/DOCX 的 `.txt`，格式间 handoff 缺口未被任何测试覆盖
   （`requestExtraction()` 的 ManualStore 依赖路径此前无控制器级测试）。
2. preview 文本 ≠ cache truth 的边界设计是对的（页头显示专用），
   但它使"preview 有字"对"canonical 有字"**零证明力**——这正是本缺陷
   在 UI 上显得矛盾的原因。
3. 测试环境事故（同轮记录）：shell 环境携带 `MODELSCOPE_API_KEY` 时
   `qa34_productionRunnerFailsClosedWithoutCredential` 必失败
   （该测试前提 = 无 credential）。运行测试前需 unset 凭据类环境变量；
   已有 ISSUE-020 同族（ambient credential），本条为其测试执行面补充实例。
