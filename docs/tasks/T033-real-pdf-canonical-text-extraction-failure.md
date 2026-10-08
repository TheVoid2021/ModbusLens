# T033 — Real-world PDF Manual Canonical Text Extraction Failure

- **Goal**: 修复真实使用场景缺陷——PDF 说明书导入成功、preview 可见中文文本，
  但 AI candidate extraction 失败于 `no_canonical_text`。修复必须落在数据所有权层
  （ManualStore canonical-text seam），禁止 AI/QML 层 workaround。

- **Background**: T032-C PAUSED 期间的独立 PRODUCT BUGFIX。来源 = 真实比色传感器
  说明书（`模拟量输入系列使用手册(RS485版).pdf`，1,886,074 B，PDF-1.7，17 页，
  9 个 ToUnicode CMap，36 `Tj` + 21 `TJ` 文本操作符，含空用户口令式 `/Encrypt`
  ——pdfium 可打开；import = ready，preview 显示中文文本）导入测试。
  初始描述（准确口径）："PDF has displayable extracted text, but downstream
  candidate-extraction pipeline reports `no_canonical_text`"——**不是**
  "PDF extraction completely failed"。RCA 详见 `docs/issues/ISSUE-021-pdf-canonical-text-seam-never-resolved-pdf.md`。

- **Technical Decisions**:
  1. **修复落点 = `ManualStore::loadText`（canonical seam 唯一所有者）**。
     PDF cache truth（每页 `.json`，T027 §60.4 冻结）在 store 内做确定性聚合，
     所有下游（candidate extraction / Manual Q&A / evidence revalidation /
     main.cpp gate）经同一 seam 自动获得 PDF canonical 文本，零调用方改动。
     备选「`loadText` 增加 documentType 参数、逐调用方显式传入」被否：
     改动面大且 seam 语义（"该内容的 canonical 文本"）本就与调用方无关。
  2. **聚合规则 = 页序不变、页间单 `"\n"`、不含显示页头**。"第 N 页" 页头是
     `buildPdfPreview` 的 presentation-only 产物，刻意不进 cache truth；
     canonical 文本同样不得携带。规则自洽即可成立：provider 输入与本地
     evidence offset 重算消费**同一**聚合串，offset 语义（UTF-16 code units、
     half-open）不受分隔符选择影响。
  3. **无文本 PDF fail-closed 且分类准确**：`no_extractable_text` 状态的文档在
     candidate 侧前置拦截（准确 token，非泛化 `no_canonical_text`）；QA 侧空
     canonical 维持 `qa_no_canonical_text`。绝不造文本、绝不派发空上下文。
  4. **`documentType` 修正**：`requestExtraction()` 原硬编码 `Txt`，改为经冻结
     扩展名规则 `manualDocumentTypeFromExtension` 从原文件名推导（core 侧仅
     identity 消费，实测 `grep` 无行为耦合，低风险）。
  5. **不加 OCR**：真实 PDF 的 text layer 经字节级证据确认存在
     （ToUnicode CMap + 文本操作符），OCR 假设被否定；无文本 PDF 属既有的
     `no_extractable_text` 冻结语义（T027 §60.2），本轮仅补准确分类。

- **Implementation**:
  1. `ManualStore::loadText`：`.txt` 命中 → 原样返回（C1a 逐字节不变）；
     未命中 → 尝试 `<hash>.json`（PDF 页缓存）→ 解析 `pages` 数组 →
     确定性聚合；两者皆缺 → `ok=false`（不可解析，维持原语义）。
  2. `CandidateExtractionController::requestExtraction()`：
     `no_extractable_text` 前置拦截（新 token `no_extractable_text`）；
     `loadText` 失败**或空串** → `no_canonical_text`；documentType 推导。
  3. `ManualQaController::beginAttempt()`：`!textOk || canonicalText.isEmpty()`
     → `qa_no_canonical_text`（fail-closed 保持）。

- **Files Changed**:
  - `src/ui/manual/ManualStore.cpp`（loadText PDF 聚合回退）
  - `src/ui/candidate/CandidateExtractionController.cpp`（前置分类守卫 +
    空 canonical 兜底 + documentType 推导；`<tuple>` include）
  - `src/ui/manualqa/ManualQaController.cpp`（空 canonical fail-closed）
  - `tests/test_manual_import_pdf_docx.cpp`（+i31/i32）
  - `tests/test_candidate_orchestration.cpp`（+o21/o22/o23 + PDF fixture
    builder + orchestrationImport helper + includes）
  - `tests/test_manual_qa.cpp`（+qa42/qa43 + PDF fixture builder + includes）
  - `docs/issues/ISSUE-021-…`、本档案、devlog、PROJECT_STATUS、BACKLOG

- **Problems Encountered**:
  1. 新测试首轮编译失败（测试文件缺 `using modbuslens::ui::ManualStore/
     ManualImportController`）——补 using 后通过。
  2. `qa34_productionRunnerFailsClosedWithoutCredential` 在本机 shell 失败：
     环境携带 `MODELSCOPE_API_KEY`（ambient credential 污染测试前提；
     ISSUE-020 同族）。`unset MODELSCOPE_API_KEY` 后通过。**与本修复无关**
     （qa34 走 TXT seed，不触及本轮任何改动路径）。

- **Solutions**: 见上；根因与数据流详见 ISSUE-021。

- **Verification**:
  - **REAL RED**（三处修复精确逆向 → 重建 → 运行；无 git checkout/restore/reset）：
    - `modbuslens_manual_import_pdf_docx_tests`：exit 2，
      `i31_pdfCanonicalTextAggregation 'ok' returned FALSE`、
      `i32_pdfNoTextCanonicalTextIsExplicitlyEmpty 'ok' returned FALSE`
      （Totals: 32 passed, 2 failed）
    - `modbuslens_candidate_orchestration_tests`：exit 2，
      `o21_textLayerPdfReachesConsentGate`（consent_required ≠ failed）、
      `o22_noTextPdfFailsClosedWithAccurateClassification`（token 不符）
      （Totals: 23 passed, 2 failed）
    - `modbuslens_manual_qa_tests`：exit 1，`qa42_textLayerPdfDispatchesThroughCanonicalSeam`
      （Totals: 42 passed, 1 failed）
    - `o23_txtSelectionStillReachesConsentGate`、`qa43_noTextPdfFailsClosedLocally`
      在 RED/GREEN 双态均 PASS（不回归守卫，符合预测）。
  - **GREEN**（重放修复 → 重建 → 运行）：
    - `modbuslens_manual_import_pdf_docx_tests`：**34 passed, 0 failed**（exit 0）
    - `modbuslens_candidate_orchestration_tests`：**25 passed, 0 failed**（exit 0）
    - `modbuslens_manual_qa_tests`：**43 passed, 0 failed**（exit 0；
      `unset MODELSCOPE_API_KEY` 后）
  - **Full Release CTest**：`ctest --test-dir build/release`（2026-10-08 实测，
    744.42 s）→ 首跑 **69/70 PASS**（`Test Passed.` 计数 = 69；`ctest -N` = 70
    targets 不变，新用例加入既有 target）。唯一失败 = `deployment_startup_check`：
    `file COPY_FILE build/release/modbuslens.exe → candidate/ModbusLens/modbuslens.exe
    because: Permission denied` —— 运行中的 ModbusLens 实例（PID 10120，
    窗口标题 ModbusLens）锁住 candidate exe，与本轮源码改动无关（gate 在
    candidate 生成阶段即失败，未到启动/验证阶段）。**该实例关闭后单独复跑
    `ctest -R deployment_startup_check` → Passed（238.28 s）** ⇒ full
    regression 实际 = **70/70**。
  - `git diff --check`：PASS。
  - **Clean full Release CTest run（final evidence，应用户要求的一次完整运行）**：
    `ctest --test-dir build/release --output-on-failure`（凭据变量 unset、无
    进程锁）→ **70/70 PASS / 0 failed / exit 0 / 1102.80 s**（`ctest -N` = 70；
    `deployment_startup_check` 在本 run 内 Passed 217.09 s）。本 run 为独立
    事件；首跑 69/70 与 isolated rerun 保留为历史记录，不互相替换。

- **Result**: PDF（有文本层）的 canonical text seam 修复完成：
  - import → preview（每页 truth + 显示页头）不变；
  - canonical text = 每页 truth 的确定性聚合（无页头）；
  - AI candidate extraction 前置条件通过（真实 UI 复测 = Human 步骤，见下）；
  - Manual Q&A 同步修复；
  - 无文本 PDF fail-closed 且分类准确；
  - TXT / Markdown / DOCX 行为逐字节不变（i01/i02/i12 + o23 守卫）。
  - full regression = **70/70**（首跑 69/70 + deployment gate 进程锁解除后
    复跑 Passed 238.28 s）。
  - **剩余未了项**：真实 UI 端到端复验（import ready → preview 有文本 →
    点"AI 提取候选"不再 no_canonical_text）= **Human re-test 步骤**，
    本轮未声称完成。

- **Knowledge Learned**:
  1. 「每页 truth」与「canonical 文本」是两个必须同时有着落的视图；新格式进
     受管存储时，canonical seam 的格式覆盖必须有测试（`requestExtraction()`
     的 store 依赖路径此前零控制器级覆盖，是本缺陷漏网的结构原因）。
  2. preview 文本 ≠ cache truth 的边界设计正确，但意味着「preview 有字」对
     「canonical 有字」零证明力——UI 矛盾表象的解释。
  3. 测试环境凭据污染（`MODELSCOPE_API_KEY` ambient）会让"无凭据"前提的
     测试必失败；执行测试前 unset 凭据变量应成为惯例（ISSUE-020 的执行面
     补充实例）。

- **Potential Interview Questions**:
  1. **Q: 为什么 PDF 能预览却不能 AI 提取？**
     A: 两条数据通路不同。预览走 `loadPdfPages`（每页 JSON + 显示时页头）；
     AI 提取走 `loadText`（canonical 文本 seam）。C1b 设计 PDF 缓存时只实现
     了每页视图，canonical seam 只接 TXT/DOCX 的 `.txt`，PDF 的聚合路径缺失。
  2. **Q: 为什么不在 AI 提取时从 preview 抓文本？**
     A: preview 带 presentation-only 页头，不是 cache truth；让 QML/UI 成为
     canonical text 的事实来源会破坏数据所有权边界（AI 层 workaround 会把
     修复锚死在错误层）。
  3. **Q: 聚合用 "\n"，为什么不担心 evidence offset 错位？**
     A: evidence 验证与 provider 输入消费**同一个**聚合串；offset 是对该串
     重算的，聚合规则自洽即可，无需与任何"原始布局"对齐。
  4. **Q: 怎么证明不是扫描件、不需要 OCR？**
     A: 字节级证据：ToUnicode CMap ×9、`Tj/TJ` 文本操作符 ×57、preview 真实
     中文；且 fail-closed 语义（`no_extractable_text`）对真正无文本的 PDF
     仍然成立并有专门测试（i32/o22/qa43）。
  5. **Q: RED→GREEN 怎么做的？**
     A: 先写测试 → 精确逆向三处修复（不用 git checkout/restore）→ 重建捕获
     5 个 RED → 精确重放修复 → 复绿 + full regression 70/70。

- **Git Commit**: `T033: fix PDF canonical text extraction seam`（单一 commit；
  SHA 在提交后由后续 docs 提交回填，不预测）。Human review/acceptance 前未
  commit；显式 stage T033 文件集，绝不 `git add .` / `git add -A`，不混入
  T032 interview docs。

## Post-Review Addendum（2026-10-08 晚 · HUMAN REVIEW = CHANGES REQUESTED）

- **REAL MANUAL UI RETEST #1 = FAIL**（Human 用原先已导入的真实 PDF 复测，
  仍报 `no_canonical_text`）。
- **RCA（CASE A：RUNNING EXE IS STALE，证据齐全）**：Human 复测所用 binary =
  `candidate/ModbusLens/modbuslens.exe`（PID 5752，SHA-256 `25583686…50e0`，
  LastWrite 2026-10-06 21:47）——**早于 T033 源码修改（10-08 16:46）**；
  T033 修复仅被编译进三个 test target，production `modbuslens` target 在
  Human 复测前**从未重建**。本轮重建（build log 明确编译三个 T033 源文件）
  → 新 exe `871e33d8…51667`（19:10:38）。本次复测记录为
  **REAL RETEST INVALIDATED BY STALE RUNTIME IDENTITY**。
- **真实 cache 只读取证（未改动任何用户数据）**：managed root =
  `C:\Users\付\AppData\Roaming\ModbusLens\ModbusLens\manuals`；
  documentId `a26598da-…`；contentHash
  `ecee094e…543db`；`<hash>.txt` MISSING（PDF 冻结语义）；`<hash>.json`
  EXISTS（schema = `{schemaVersion, pages:[str×16]}`，总 8710 字符 = charCount）
  —— **与当前 importer schema 完全一致，CASE B 否定**；production 同逻辑
  只读推演：含修复代码 `loadText ok=True`、聚合长度 8725。
- **不变项**：零代码修改（本 addendum 阶段）、零 cache 变更、零重导入；
  clean full CTest 70/70（1102.80s）历史记录不变；synthetic regression PASS
  不升级为"真实问题已修复"。
- **NEXT = Human 关闭旧实例 → 从重建后的 exe 启动 → 用原有 PDF 复测**
  （REAL MANUAL UI RETEST #2 = PENDING HUMAN）。

## Human Acceptance（2026-10-08 晚）

- **REAL MANUAL UI RETEST #2 = PASS for T033 scope**：Human 关闭 stale 实例、
  使用重建后 production exe（`871e33d8…51667`）对**原有已导入**的真实说明书
  复测（未重新导入、未清 cache）——**`no_canonical_text` 已消失**，
  pipeline 真实进入后续 Candidate / Provider 阶段（期间观察到 `no_candidate`
  与 `provider_rejected_status`）。
- **T033 = COMPLETE / HUMAN ACCEPTED**。T033 修复范围 = PDF canonical text
  seam；`no_candidate` / `provider_rejected_status` 属 seam **之后**的独立层
  （candidate 质量 / provider 额度），**不归入 T033 root cause**。
- **T034 — Configurable LLM Provider / BYOK = 已授权 PHASE A（LEARNING /
  DESIGN ONLY）**，动因 = ModelScope 额度耗尽（非 T033 defect）。T032-C 保持
  PAUSED。
