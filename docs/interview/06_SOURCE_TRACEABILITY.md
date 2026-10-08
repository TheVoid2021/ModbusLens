# 06 — SOURCE TRACEABILITY（引用完整性核验）

> 用途：证明本 Fact Pack 的每一条重要事实**至少可追溯到一处仓库来源**，
> 并公开**本轮实际读了什么 / 没读什么**，避免把定点读取伪装成全文通读。
> 证据分级：`[V-nnn]` = VERIFIED；`[C-nn]` = CONFLICT；`[U-nn]` = UNVERIFIED。
>
> 权威计数表在 `00_FACT_INDEX.md` §4（用 `grep -c` 可复现）。

---

## 1. 核验方法（本轮实际执行的命令）

```bash
# 结构与状态
git rev-parse --show-toplevel / git branch --show-current / git rev-parse HEAD
git status --porcelain=v1 --untracked-files=all
git diff --name-status / git diff --cached --name-status / git diff --check
git log --oneline --decorate -20 / git tag --list / git show-ref --tags
git rev-list --count HEAD / git log --reverse --pretty=format:"%h|%ad|%s" --date=short

# 引用存在性
git rev-parse <sha>^{commit}          # 对每个被引用的 commit
git ls-tree -r --name-only HEAD -- docs/tasks | wc -l
git ls-tree -r --name-only HEAD -- docs/adr
git ls-tree -r --name-only HEAD -- docs/issues | wc -l

# 能力类主张一律回源（不靠文档转述）
grep -cE "^\s*add_test\(" CMakeLists.txt
grep -oE "add_test\(NAME [a-zA-Z0-9_]+" CMakeLists.txt
grep -rniE "mbap|modbus.?tcp|transactionid" src/ tests/          # → 0
grep -rniE "write_coil|write_register|set_register|device_write" src/ui/agent/   # → 0
grep -rn "#include <Q\|#include <Qt" src/core/                    # → 0
grep -n "target_link_libraries(modbuslens_core" CMakeLists.txt    # → 无此语句
grep -rn "IFrameSource" src/ tests/                               # → 0
ls -d src/io src/app                                              # → 不存在
for sym in std::filesystem std::chrono std::variant …; do grep -rl …; done
grep -rn "demo_v2" tests/ CMakeLists.txt scripts/                 # → 0
git ls-files tests/data                                           # → 空
grep -n "demo_v1" CMakeLists.txt                                  # configure_file COPYONLY
```
`[V-401]`

---

## 2. Commit SHA 存在性核验

下列 SHA 均经 `git rev-parse <sha>^{commit}` 或 `git log` 实测存在。

### 2.1 里程碑 / 阶段代表提交

| SHA | Subject | 阶段 | |
| --- | --- | --- | --- |
| `aa337f6` | T001: 项目引导 — 骨架、文档体系、最小 Qt6 应用 | P1 | `[V-402]` |
| `ffd2b94` | T001.1: Bootstrap Documentation Cleanup | P1 | `[V-403]` |
| `e8ef30c` | T002: implement CRC-16/MODBUS core with KAT tests | P2 | `[V-404]` |
| `a44a6d2` | T003: Modbus RTU frame model with value semantics | P2 | `[V-405]` |
| `73825c6` | T004(Part A): implement RTU wire codec with CRC validation | P2 | `[V-406]` |
| `e8b62f6` | T004(Part B): implement Function 0x03 semantic decoder | P2 | `[V-407]` |
| `3a896df` | T005: implement deterministic Modbus simulated slave | P3 | `[V-408]` |
| `2c8d850` | T006: implement deterministic wire fault injection | P3 | `[V-409]` |
| `14982f6` | T007(Part A): implement single transaction analysis | P4 | `[V-410]` |
| `0f3109a` | T007(Part B): implement transaction statistics snapshot | P4 | `[V-411]` |
| `76030a2` | T008(Part A): migrate app to Qt Quick and add QML bridge | P4 | `[V-412]` |
| `28f38b0` | T008.1: add standalone Windows deployment workflow | P4 | `[V-413]` |
| `e4920da` | T009(Part A): implement versioned replay log and analysis core | P5 | `[V-414]` |
| `d473d36` | T009(Part B): integrate replay files into analysis dashboard | P5 | `[V-415]` |
| `b31233b` | T010(Part A): implement serial transaction runtime and QtSerialPort adapter | P5 | `[V-416]` |
| `33ed197` | T010(Part B): integrate serial controls into the analysis dashboard | P5 | `[V-417]` |
| `06ef801` | T011(Part A): add deterministic diagnosis baseline | P6 | `[V-418]` |
| `f087275` | T011(Part B): integrate ModelScope LLM diagnosis explanation | P6 | `[V-419]` |
| `9921efd` | T012(Part A): read-only agent tool layer — dispatcher + validation + JSON DTO | P6 | `[V-420]` |
| `da453a7` | T012(Part B Phase 1): native tool-calling agent runtime — FSM + provider adapter | P6 | `[V-421]` |
| `01841b1` | fix(T011): evidence-scope guard against AI over-attribution (ISSUE-006) | P6 | `[V-422]` |
| `e922c19` | fix(T012): ISSUE-007 — raise agent total tool budget 3→6 + planning discipline | P6 | `[V-423]` |
| `85699ff` | fix(T011): use split workspace for diagnosis and transactions | P6 | `[V-424]` |
| `bb3f3b4` | fix(T011): make AI timeout cancellation deterministic | P6 | `[V-425]` |
| `aea1e64` | polish(T013): visual styling V1~V7 + user-facing terminology guidance | P7 | `[V-426]` |
| `5a2f60c` | polish(T013): light tabbed diagnosis UI and stable transaction table | P7 | `[V-427]` |
| `a25d63c` | fix(T013): repair serial port refresh and UI alignment | P7 | `[V-428]` |
| `99f17d6` | fix(T013): verify serial discovery and use transaction pane width | P7 | `[V-429]` |
| `2a5be8e` | M8: reconstruct final project evidence for interview review | P8 | `[V-430]` |
| `dab9b5f` | M8.1: audit diagnostic coverage and replay gaps | P8 | `[V-431]` |
| `0ec6e06` | T014: add TransactionIssue model surface and RED detail tests | P9 | `[V-432]` |
| `2ba719f` | T015: implement passive analyzer core (FC06, generic exception, broadcast status) | P9 | `[V-433]` |
| `02ce302` | T015: guard generic exception matcher against request function MSB | P9 | `[V-434]` |
| `92ccf16` | T015(Part C): migrate to request-issue collection and add Function16 surface with RED tests | P9 | `[V-435]` |
| `ae067ab` | T015(Part C): lock baseline formatter request-issue visibility（UI-T05 anchor）= **v1.0.0 tag 的 peeled commit** | P9 | `[V-436]` |
| `a40d935` | V2 Gate 0: freeze V1 baseline and establish upgrade governance | P10 | `[V-437]` |
| `4fc934f` | T016: M9-A Phase 2 — Design System core + first component migration | P10 | `[V-438]` |
| `6562dd3` | M9-A: fix statistics layout regression（ISSUE-012 修复） | P10 | `[V-439]` |
| `189c62c` | M9-B1: application shell skeleton | P10 | `[V-440]` |
| `0498d6c` | M9-F: complete final acceptance and close M9 | P10 | `[V-441]` |
| `3e7aaeb` | M10: design Active Master v1 and write safety | P11 | `[V-442]` |
| `db2aea9` | M10-C: separate confirmation from write dispatch | P11 | `[V-443]` |
| `42fcd0b` | M10-D3: add atomic FC06 dispatch and evidence integration | P11 | `[V-444]` |
| `d20c07b` | M10-D4: expose safe FC06 production write UI | P11 | `[V-445]` |
| `7eb3ba5` | M10-E3: add FC16 product capability and atomic dispatch | P11 | `[V-446]` |
| `d923a3a` | M10-E4: expose safe FC16 production write UI | P11 | `[V-447]` |
| `cc3c6f8` | M10-F: fix FC16 terminal and add structured request preview | P11 | `[V-448]` |
| `8d78ddb` | T022: fix the portable-smoke ReferenceError and the FC03 request-row overflow | P11 | `[V-449]` |
| `d08ab55` | T022: keep the write section inside 1000x700 on the real Windows platform（ISSUE-018 修复） | P11 | `[V-450]` |
| `b8abe2c` | M10-F: close final acceptance | P11 | `[V-451]` |
| `a494d9c` | T023: implement FC03 read-result observability | P11 | `[V-452]` |
| `352b81c` | T023: make the read function code editable (text-input request UX) | P11 | `[V-453]` |
| `954893d` | M11: define decode acceptance contract | P12 | `[V-454]` |
| `b95de54` | M11: add single-register decode view | P12 | `[V-455]` |
| `bc99e6e` | M11: add the 32-bit decode views and word order | P12 | `[V-456]` |
| `d58fd4d` | M11: fix the read-result HEX echo and its gate coverage | P12 | `[V-457]` |
| `2f767d1` | M11: archive the second slice and stage the review | P12 | `[V-458]` |
| `cc9193d` | M11: ratify the 32-bit alignment contract | P12 | `[V-459]` |
| `6550ca9` | M11: close register readout and decode | P12 | `[V-460]` |
| `03d2dc1` | M11: advance verified LKGC | P12 | `[V-461]` |
| `ad706f2` | M11: archive final portable package | P12 | `[V-462]` |
| `e9c376c` | M12: define device-profile acceptance contract | P13 | `[V-463]` |
| `1c42aaf` | M12: add device-profile JSON foundation | P13 | `[V-464]` |
| `da07f43` | M12: add device-profile workspace | P13 | `[V-465]` |
| `b502ea8` | T027: restore displayName notification, close dirty dialog on Save/Discard, harden profile editor gate（ISSUE-019 修复） | P13 | `[V-466]` |
| `becadc5` | M12: add register-map editor | P13 | `[V-467]` |
| `f1567a5` | M12: add profile semantic readout | P13 | `[V-468]` |
| `13799d6` | M12: keep the human semantic demo alive until closed | P13 | `[V-469]` |
| `7bcd2ca` | M12: add deterministic manual import foundation | P13 | `[V-470]` |
| `fb2170e` | M12: add deterministic PDF and DOCX extraction foundation | P13 | `[V-471]` |
| `d899e55` | M12: add evidence-backed AI candidate foundation | P13 | `[V-472]` |
| `19bb9cf` | M12: add strict ModelScope candidate adapter | P13 | `[V-473]` |
| `309ba15` | M12: add consent-gated AI candidate orchestration | P13 | `[V-474]` |
| `613a32a` | M12: complete session P transport and sanitize harness credentials（ISSUE-020 修复） | P13 | `[V-475]` |
| `4a77673` | M12: add Human-authored candidate value edit (C3-H8) | P13 | `[V-476]` |
| `80e4326` | M12: add evidence-backed single-manual Q&A | P13 | `[V-477]` |
| `8416fe7` | M12: make manual Q&A citations app-resolved | P13 | `[V-478]` |
| `9c065f2` | UI: make Manual Q&A results scrollable = **当前 verified LKGC** | P13 | `[V-479]` |
| `751eed4` | Release: freeze canonical candidate-tree packaging contract | P14 | `[V-480]` |
| `4643e31` | Release: package canonical candidate tree | P14 | `[V-481]` |
| `b18e378` | Release: archive canonical package human acceptance = **v2.0.0 tag 的 peeled commit** | P14 | `[V-482]` |
| `2f53197` | POST-M12: archive v2.0.0 release closure and interview materials = **`origin/main`** | P14 | `[V-483]` |
| `8fc4f75` | Release Prep R1: audit documentation, evidence and GitHub assets | P14 | `[V-484]` |
| `8956121` | UI Release Audit R1: verify release visual quality | P14 | `[V-485]` |
| `6982c78` | UI Freeze R1: capture final screenshot candidates（**被拒 05-serial 版本所在提交**） | P14 | `[V-486]` |
| `1c4f9a0` | T030 R2: recapture final serial screenshot (blocked, drop 05) | P14 | `[V-487]` |
| `82cea51` | T031 A: close documentation truth gaps | P14 | `[V-488]` |
| `28656b3` | T031 B: finalize repository README | P14 | `[V-489]` |
| `69a58ae` | T031 C: add MIT license and close repository metadata = **当前 HEAD** | P14 | `[V-490]` |

### 2.2 关键历史 SHA（非代表提交但被本文引用）

| SHA | 用途 | |
| --- | --- | --- |
| `9e79558` | 中文化 pass = 当时 LKGC（`docs/06` §8） | `[V-491]` |
| `97f17d6` / `325f27c` | T013 Phase E 修复 / 视觉 PASS 收口（`docs/07` 附录写作时的 LKGC/HEAD 之一为 `99f17d6`） | `[V-492]` |
| `f649270` | **新增 `ADR-003` 的提交**（T015 Phase A 归档） | `[V-493]` |
| `9712a6c` | ISSUE-016 E6-2 与 ISSUE-017 的**缺陷归属提交**（TransactionsPage delegate） | `[V-494]` |
| `0d5c219` | ISSUE-014 建档所在提交（M10-C3 时期） | `[V-495]` |
| `c78c960` | ISSUE-020 RCA 存档提交 | `[V-496]` |
| `2bf049b` → `a29e508` → `d8d0ff2` | M10-E5 **过早收口 → 撤回 → 重新执行**的自我纠正链 | `[V-497]` |
| `ef71244` / `806d424` / `fc86dcc` / `c50dbfe` | M10-B/M10-C 的 migration / 原子性 / 安全证据提交 | `[V-498]` |
| `641b1db` / `6e248b0` / `0463207` | M9-E 版本权威三步（决策 → 统一身份 → 校验跟随 CMake） | `[V-499]` |
| `1c4f9a0`（= `pushed-HEAD` 时点） | T031-A 声明 CTest 70/70 snapshot 时点的 HEAD | `[V-500]` |

---

## 3. Task / Issue / ADR 存在性核验

### 3.1 任务编号体系（**重要：编号不是连续的**）

`git ls-tree -r --name-only HEAD -- docs/tasks` 实测 = **30** 个 `.md` 文件：`[V-501]`

```text
T001, T001.1, T002 … T024, T027, T028, T029, T030, T031   （共 30 个文件）
```

- **`T025` / `T026` 没有独立任务文件**——它们是 `docs/BACKLOG.md` 中的**独立任务行**
  （M11 First / Second Slice），实现档案写在 **`docs/tasks/T024-m11-register-readout-decode-contract.md`** 之内。`[V-502]`
- **`T032` 在本轮之前不存在**——它是 `docs/PROJECT_STATUS.md` 顶部块与 `docs/devlog/2026-10-07.md:316`
  以 "NEXT" 形式引用的**保留编号**；本轮据此首次建档 `docs/tasks/T032-interview-handoff-closure.md`。`[V-503]`
- **M8 / M8.1 / POST-M12 / Release 各轮没有独立 `T0xx` 编号**，
  其档案分别落在 `docs/07`、`docs/09` 与 `docs/tasks/T027` §108–§119。`[V-504]`
- 编号**跳跃**（T024 → T027）是事实，不是缺失：`T025`/`T026` 被 M11 两个切片占用（见上）。`[V-505]`

### 3.2 Issue 存在性

`git ls-tree -r --name-only HEAD -- docs/issues` 实测 = **20** 个文件
（`ISSUE-001` … `ISSUE-020`，编号连续）。`[V-506]`

### 3.3 ADR 存在性

`docs/adr/` 实测 = `.gitkeep` + **3** 份 ADR：`[V-507]`
```text
ADR001-use-qt-quick-qml.md                      （命名不带连字符）
ADR002-readonly-tool-agent-architecture.md      （命名不带连字符）
ADR-003-broadcast-outcome-semantics.md          （命名带连字符 —— 命名风格不一致，事实记录）
```

---

## 4. CTest 目标清单（70）

`grep -oE "add_test\(NAME [a-zA-Z0-9_]+" CMakeLists.txt` 实测输出（按字母序）：`[V-508]`

```text
active_master            active_profile_controller  active_request
agent_integration        agent_runtime              agent_tools
ai_client                c1b_dependency_materializer candidate_adapter
candidate_extraction     candidate_orchestration    candidate_review
candidate_transport      codec                      crc
deployment_startup_check device_profile             diagnosis
f03                      fault                      fault_integration
fc06_active              fc16_active                frame
manual_delete            manual_extraction          manual_import
manual_import_pdf_docx   manual_qa                  passive
profile_controller       profile_semantic           qml_active_profile_check
qml_candidate_review_check            qml_candidate_review_check_windows
qml_consent_check                      qml_consent_check_windows
qml_focus_check                        qml_geometry_check
qml_manual_import_check                qml_manual_import_check_windows
qml_manual_qa_check                    qml_manual_qa_check_windows
qml_nav_check                          qml_production_write_check
qml_profile_editor_check               qml_profile_semantic_check
qml_profile_semantic_demo              qml_read_result_check
qml_read_result_demo                   qml_read_result_demo32
qml_register_map_check                 qml_smoke
qml_write_foundation_check             qml_write_foundation_check_windows
register_decode          replay_analysis            replay_log
serial                   serial_adapter             simulator
simulator_integration    statistics                 statistics_integration
transaction              transaction_integration    ui_bridge
write_dispatch           write_encoder              write_prepare
```

**计数 = 70**（`grep -cE "^\s*add_test\(" CMakeLists.txt` → 70）。`[V-509]`

---

## 5. 资源文件存在性核验

| 资源 | 实测结果 | |
| --- | --- | --- |
| `samples/demo_v1.mlog` | 存在（canonical 黄金样本；经 `configure_file(... COPYONLY)` 到 `${CMAKE_CURRENT_BINARY_DIR}/test_data/demo_v1.mlog`） | `[V-510]` |
| `samples/demo_v2.mlog` | **存在，但未被任何测试/CMake/脚本引用**（`grep -rn "demo_v2" tests/ CMakeLists.txt scripts/` → 0） | `[V-511]` |
| `tests/data/` | **在 git 中为空**（`git ls-files tests/data` → 无输出） | `[V-512]` |
| `docs/assets/screenshots/05-serial.png` | **不存在**（已按 T030-R2 从仓库移除；被拒版本在 `6982c78` 的历史中） | `[V-513]` |
| `docs/assets/screenshots/01-hero-dashboard.png`、`02-transactions.png`、`03-replay-analysis.png`、`04-diagnosis-baseline.png` | 均存在 | `[V-514]` |
| `docs/assets/architecture.png` | 存在但 **STALE / DO NOT USE IN README**（T031-A §A.5：无生成源；README 改用 Mermaid） | `[V-515]` |
| `LICENSE` | 存在（MIT，`Copyright (c) 2026 TheVoid2021`） | `[V-516]` |
| `third_party/m12c-c1b/` | 存在（PDFium/libzip/zlib 物化根） | `[V-517]` |
| `cmake/modbuslens_generate_candidate.cmake` | 存在 | `[V-518]` |
| `scripts/` | `bench_replay/`、`deploy_windows.bat`、`make_icon.py`、`make_package.py`、`materialize_c1b_deps.py`、`test_make_canonical_package.py`、`test_make_package_freshness.py`、`test_materialize_c1b_deps.py` | `[V-519]` |
| `src/io/`、`src/app/` | **不存在**（`docs/02_ARCHITECTURE.md` §4 中的规划目录从未创建） | `[V-520]` |

---

## 6. 本轮**实际阅读范围**的诚实声明

### 6.1 全文通读
- 全部 20 份 `docs/issues/ISSUE-0xx-*.md`
- 全部 3 份 `docs/adr/*.md`
- `AGENTS.md`、`README.md`、`CHANGELOG.md`
- `docs/00_PROJECT_CHARTER.md`、`01_REQUIREMENTS.md`、`02_ARCHITECTURE.md`、`03_MODBUS_LEARNING.md`、
  `04_TEST_STRATEGY.md`、`05_DEMO_GUIDE.md`、`06_UI_LANGUAGE_POLICY.md`、`07_FINAL_PROJECT_REVIEW.md`、
  `09_DIAGNOSTIC_COVERAGE_AUDIT.md`、`10_REPLAY_PERFORMANCE_BENCHMARK.md`、
  `11_PROJECT_FINAL_RETROSPECTIVE.md`、`11_V2_UPGRADE_PLAN.md`、`12_RESUME_INTERVIEW_QA.md`、`ENVIRONMENT.md`
- `docs/tasks/T001`…`T024`、`T028`、`T029`、`T030`、`T031`
- `docs/INTERVIEW_NOTES.md` 头部、§M11 Second Slice、§T027 §94、§POST-M12 FINAL INTERVIEW PACK（F1–F11）
- `docs/tasks/T027` 的 §1–§53 标题结构 + §45.8、§119.1–§119.19 全文
- `git log --reverse --pretty=format:"%h|%ad|%s"`（**全部 498 个提交**）
`[V-521]`

### 6.2 **定点读取，未全文通读**（重要披露）
- **`docs/tasks/T022`（776 KB / 11593 行）**：读取方式 = 引用它的 canonical 文档
  （`docs/04_TEST_STRATEGY.md` §3、`docs/03`、全 3 份 ADR 中的 T022 引用、`docs/PROJECT_STATUS` 顶部块）
  + `git log` 中的 M10 提交链 + **直接回源核验**（`src/core/active/`、`src/ui/serial/`、
  `grep` 写工具为 0）。**未按行通读 T022 全文**——因此 T022 内可能存在本文档未覆盖的细节。`[U-03]`
- **`docs/tasks/T017`（310 KB）/ `T019`（153 KB）/ `T021`（178 KB）/ `T018`（92 KB）/ `T020`（105 KB）**：
  读取方式 = `grep -nE "^#{1,2,3} "` 取标题结构 + 相关 Issue 档案全文 + `git log` 提交链 +
  `docs/INTERVIEW_NOTES.md` 对应追加条目。**未按行通读**。`[U-04]`
- **`docs/tasks/T027`（708 KB / 12110 行）**：读取方式 = 标题结构全覆盖 + 关键节全文
  （§24–§35 契约冻结、§45 M12-B 收口、§47–§53 M12-C C1a、§119 全文）+ 相关 Issue 全文 +
  `git log` 提交链 + `docs/INTERVIEW_NOTES.md` F 段。**§54–§118 仅为标题级覆盖**。`[U-05]`
- **`docs/08_KNOWLEDGE_OWNERSHIP.md`（115 KB / 1921 行）**：仅读取标题结构（Part 1–Part N 的
  章节名与 §7/§10/§2.14 等标题），**未通读正文**。`[U-06]`
- **`docs/devlog/`（932 KB / 68 个文件）**：仅按 `ls` 取文件清单与日期分布，
  用其确认"逐日留档到 2026-10-07"这一事实；**未通读任何单篇 devlog**。`[U-07]`
- **`docs/PROJECT_STATUS.md`（529 KB / 2421 行，单行极长）**：读取方式 = 顶部 3 个最新块
  （T031-C / T031-B / T031-A 时点）+ `grep` 关键断言（`REAL HARDWARE` / `NOT CREATED` / LKGC）。
  **未通读 2421 行**（其中大量为历史状态叙述，按 newest-first 追加）。`[U-08]`
- **`docs/BACKLOG.md`（465 KB / 445 行，单行极长）**：读取方式 = `grep` 定位 T025/T026 行与里程碑表。
  **未通读全部 445 行**。`[U-09]`
- **`docs/tasks/T016` / `T024` / `T009`–`T015`**：完整读取或以 Issue/RCA 档案等价覆盖；
  `T015`（100 KB）与 `T014`（47 KB）以 Issue ADR-003 + `docs/04` 的 T014/T015 段落 + `docs/11` §4.6 覆盖。`[U-10]`

> **结论**：本 Fact Pack 对**能力边界、架构决策、缺陷 RCA、发布事实**这四类主张的证据强度最高
> （因为都做了源码/git/测试级回源核验）；
> 对**单个里程碑内部的全部过程细节**（尤其 T017/T019/T021/T022/T027 的中段）
> 为**标题级 + 关键节覆盖**，不声称穷尽。`[V-522]`

---

## 7. UNVERIFIED 清单（共 11 条）

| ID | 主张 | 为什么 UNVERIFIED | |
| --- | --- | --- | --- |
| `[U-01]` | `docs/09` 引用的 `TransactionAnalysis.h:14-21 / :32-38 / :40-47` 仍指向原文 | 该文件在 T014/T015 后追加了内容，行号已位移。**属历史行号，不可直接复用**；本轮未逐行重算对应关系 | 见 `01_PROJECT_TIMELINE.md` P2 注 |
| `[U-02]` | `docs/02_ARCHITECTURE.md` §6 的规则"任何对 D1–D7 的变动 → 新建 ADR"是否被完全遵守 | D3（`IFrameSource` 未提取）与 D10（M10 主动写路径）在 `docs/02` 中以**就地更新 + 引用取代关系**维护，而非各立一份 ADR。**是否构成"静默改架构"是治理判断，不是可从仓库单向判定的事实** | 见 `03_ARCHITECTURE_DECISIONS.md` 附 |
| `[U-03]` | `docs/tasks/T022` 全文内部细节 | 本轮未按行通读（776 KB / 11593 行） | §6.2 |
| `[U-04]` | `T017`/`T018`/`T019`/`T020`/`T021` 全文内部细节 | 本轮为标题级 + Issue 档案等价覆盖 | §6.2 |
| `[U-05]` | `T027` §54–§118 的全部内部细节 | 本轮仅标题级覆盖（该区间为发布链与 M12-C/D 的密集档案区） | §6.2 |
| `[U-06]` | `docs/08_KNOWLEDGE_OWNERSHIP.md` 正文内容 | 仅读取标题结构 | §6.2 |
| `[U-07]` | 68 篇 `docs/devlog/*.md` 的具体内容 | 仅取文件清单与日期分布 | §6.2 |
| `[U-08]` | `docs/PROJECT_STATUS.md` 全部历史状态块 | 仅读最新 3 块 + 关键断言 grep | §6.2 |
| `[U-09]` | `docs/BACKLOG.md` 全部 445 行 | 仅 grep 定位 | §6.2 |
| `[U-10]` | GitHub 网页端**当前**状态（Description / 15 Topics / Social Preview 是否已应用；`v2.0.0` tag ref 是否在远端） | **本地仓库无法判定远端网页状态**；任务基线也明确"Social Preview: do not assume"。`origin/main` = `2f53197` 的祖先链**包含** `b18e378`（v2.0.0 的 peeled commit），但**tag ref 本身是否存在于远端无法离线核验** | §8 |
| `[U-11]` | `docs/INTERVIEW_NOTES.md` F8 的"无 register map/工程单位语义层"表述**是否已修正** | 该同族表述**未在 T032-A1 修改**——不在任务 §6 授权文件集内 ⇒ 待 Human 授权的后续整理项。其事实内核已由 `README.md` §11 承载并修正（`[C-03]`），此项仅指 INTERVIEW_NOTES 文本本身 | §7 |

---

## 8. `origin` 与发布状态的精确口径

```text
remote            = origin  https://github.com/TheVoid2021/ModbusLens.git
origin/main       = 2f53197cc149bbf75e876b3b94234a2f934a8048
HEAD              = 69a58aedf059794b4485dc897587b4c6962f8946
divergence        = 0 behind / 7 ahead（git rev-list --left-right --count origin/main...HEAD）
local-only commits（7）：
  8fc4f75  Release Prep R1: audit documentation, evidence and GitHub assets
  8956121  UI Release Audit R1: verify release visual quality
  6982c78  UI Freeze R1: capture final screenshot candidates
  1c4f9a0  T030 R2: recapture final serial screenshot (blocked, drop 05)
  82cea51  T031 A: close documentation truth gaps
  28656b3  T031 B: finalize repository README
  69a58ae  T031 C: add MIT license and close repository metadata
```
`[V-523]`

- **T028 §1 记录**："main 已 fast-forward 至 `2f53197…` 并推送" ⇒ 推送**发生在 T028 时点**，
  之后 T028–T031 的 7 个提交**未推送**。`[V-524]`
- **任务基线声明 `PUSH = NONE`**（本轮）：与上述吻合——**本轮未 push**。`[V-525]`
- **v2.0.0 发布链 = 仓库记录的 COMPLETE / INDEPENDENTLY VERIFIED（证据等级已精确化，T032-A1）**：
  **VERIFIED（仓库事实）**——仓库档案记录了 v2.0.0 发布/公开流程已完成，并保留本地 release/tag 证据
  （T027 §119.11：URL / 资产大小 / SHA-256 / R5G PASS；验证通道 = 公开页面 HTML 解析 +
  直接公开资产下载逐字节比对；REST API 未认证访问 403 是限流而非发布失败）。
  **UNVERIFIED（当前远端事实）**——**当前** GitHub 页面状态、远端 tag ref、
  Social Preview / Description / Topics **在 T032-A / A1 期间均未实时核验**（离线环境无法判定）⇒ `[U-10]`。
  **措辞红线**：不把"仓库记录了发布完成"升级为"当前远端状态已复核"；
  也不把真实历史留档降级为"未发布"（AGENTS 状态等式：`historical evidence ≠ currently executed evidence`）。`[V-561]`
- **LICENSE 与已发布 ZIP 的关系**：LICENSE 于 v2.0.0 发布**之后**加入仓库根目录，
  **已发布 ZIP 不包含该文件且未被重建**——证据 = 两份 package manifest 中 LICENSE 条目数均为 **0**。`[V-526]`
- **`docs/assets/screenshots/05-serial.png`** 已从仓库移除；**README 不引用不存在的资产**。`[V-527]`

---

## 9. 引用完整性自检（本轮已执行的验证）

| 检查项 | 结果 | |
| --- | --- | --- |
| 全部 `[V-]` 标记的 commit SHA 存在 | PASS（`git rev-parse` / `git log` 逐条核验） | `[V-528]` |
| 全部 Task ID 存在或已显式说明不存在的原因 | PASS（`T025`/`T026` 无独立文件已说明；`T032` 本轮创建） | `[V-529]` |
| 全部 Issue ID 存在（ISSUE-001…020） | PASS（20/20） | `[V-530]` |
| 全部 ADR ID 存在（3 份，含命名风格不一致的事实） | PASS | `[V-531]` |
| Fact Pack 内部相对链接目标存在 | PASS（同目录 00–06 共 7 个文件全部创建） | `[V-532]` |
| 被引用的 `README.md` / `CHANGELOG.md` / `docs/*` / `LICENSE` / `CMakeLists.txt` 路径存在 | PASS | `[V-533]` |
| 产品路径未被本任务修改 | PASS（`git diff --name-only HEAD -- src tests cmake scripts samples CMakeLists.txt` → EMPTY） | `[V-534]` |
| 截图资产未被本任务修改 | PASS（`git diff --name-only HEAD -- docs/assets` → EMPTY） | `[V-535]` |
| `git diff --check` | PASS | `[V-536]` |