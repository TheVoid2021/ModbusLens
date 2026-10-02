# ISSUE-020: 环境凭据泄漏进确定性 harness —— 同意门检查在授权下发起真实云端推理（SESSION P-R1C）

- **发现会话**：SESSION P-R1C（2026-10-01，ZCode 跨 Agent 接管自 WorkBuddy）
- **关联任务**：M12-C C2 · SESSION P transport slice（T027 §76 授权 / §77 修复授权）
- **定性**：**测试卫生 + 安全边界缺陷**（产品代码行为符合 SESSION P 设计；缺陷在于确定性 harness 与 ambient 环境的边界）

## Observed（现象）

1. 2026-09-30 14:53，WorkBuddy 在 fresh acceptance tree
   `build/acceptance/session-p-r1b-release` 上运行 `ctest -R qml_consent_check`：
   **Test Failed，零输出，耗时 15.87s**（`Testing/Temporary/LastTest.log`：`<end of output>`）。
   当时被当作"fresh-tree ctest crash"上报。
2. ZCode 接管后复跑（ambient 环境不变）得到**语义失败**而非静默崩溃：
   `CONSENTFAIL: R07: Agree wrote a candidate without a provider result`（exit 1）。
3. 同一二进制在 `MODELSCOPE_API_KEY` 被显式移除后：`CONSENT CHECK PASS`（exit 0，1.87s）。

## Expected（预期）

`--qml-consent-check` 是确定性 harness：无凭据时 Agree 只能推进到 `not_configured`
失败态，candidateCount 恒为 0（R07 断言，SESSION O-R2 已 Human 验收）。任何
deterministic gate **永不**发起真实网络请求。

## Evidence（证据 · 全部实测）

| Run | `MODELSCOPE_API_KEY` | `QT_ASSUME_STDERR_HAS_CONSOLE` | 结果 |
| --- | --- | --- | --- |
| WorkBuddy ctest 14:53（windows QPA） | PRESENT（User 级持久环境，len=39） | 未设置 | exit≠0、**零输出**、15.87s |
| ZCode Run A（offscreen） | PRESENT（ambient） | 设置 | exit 1、**R07 FAIL（candidateCount=1）** |
| ZCode Run B（offscreen） | **ABSENT（env -u）** | 设置 | exit 0 PASS、1.87s |
| ZCode Run C（offscreen） | **ABSENT（env -u）** | 未设置 | exit 0 PASS、**零输出** |

- `MODELSCOPE_API_KEY` 在 Windows **User 级环境持久存在**（长度 39；值未读取、未打印），
  因此注入每个测试进程。Run A → R07 失败 ⇒ candidateCount=1 ⇒ 已走完
  HTTP 200 → strict parser → 本地 Evidence 验证全链 ⇒ **发生了真实 ModelScope 推理**。
- 15.87s − 1.87s ≈ 14s ≈ 真实 HTTPS 往返，与 WorkBuddy 14:53 运行同样包含真实调度一致。
- 零输出机制：`qml_consent_check`（非 _windows 变体）是 GUI-subsystem exe 且
  **未设置** `QT_ASSUME_STDERR_HAS_CONSOLE=1`，ctest 管道下 Qt 诊断全部丢弃
  （Run C 证实：exit 0 + 0 字节输出）⇒ 失败呈现为"静默崩溃"。

## 复现步骤

1. Windows User 环境设置 `MODELSCOPE_API_KEY`（本机既有）。
2. 构建 SESSION P WIP（production runner 接通真实 transport）。
3. `ctest -R qml_consent_check`（或直接运行 `modbuslens.exe --qml-consent-check`）。
4. 观察 R07 FAIL（candidateCount=1）——即一次真实推理已发生。

## 定位过程

1. 从 `LastTest.log`（`<end of output>`）确认 WorkBuddy 运行为零输出失败。
2. offscreen 复跑 → 得到 R07 语义失败（非崩溃）。
3. 检查凭据存在性（只查存在、未读值）→ User 级 PRESENT。
4. 单变量对照 Run A/B/C（上表）→ 唯一决定变量 = ambient 凭据；零输出 = stderr 契约缺失。
5. 代码链核对：`CandidateExtractionController::beginAttempt` →
   `ModelScopeCandidateRunner::begin` →（有凭据时）`ensureWired()` →
   `ModelScopeExtractionTransport::send` → `QtModelScopeHttpClient::post`（真实网络）。

## Root Cause（根因）

两个独立缺陷叠加：

- **RC-1（核心）**：SESSION P 使 production runner 不再惰性后，ambient
  `MODELSCOPE_API_KEY` 成为确定性 harness 的**隐藏输入**。harness 没有声明自己的
  配置真值，凭据从 User 环境泄漏进 gate，同意门点击被降级为真实 provider 调度。
  违反 §77.5 同构原则（"canonical acceptance 不得依赖 ambient 环境真值"）与
  H2/H3 的 credential/consent 边界精神。
- **RC-2（可观测性）**：`qml_consent_check` 缺少 `QT_ASSUME_STDERR_HAS_CONSOLE=1`
  （其余所有 gate 均有），失败诊断被静默吞掉，把语义失败伪装成"crash"。

## Fix（修复）

- `src/main.cpp`：任何 `--qml-*` harness 进程在创建任何 controller 之前
  `qunsetenv("MODELSCOPE_API_KEY")` + `qunsetenv("MODBUSLENS_MODELSCOPE_MODEL")`
  —— harness 自己声明配置真值；production（无 `--qml-` 参数）不受影响。
- `runConsentCheck` 入口 guard：若凭据对 harness 可见，立即
  `CONSENTFAIL ... refusing to run`（exit 1），把未来的泄漏变成响亮失败而非真实调度。
- `CMakeLists.txt`：`qml_consent_check` 补 `ENVIRONMENT "QT_ASSUME_STDERR_HAS_CONSOLE=1"`
  （平台语义不变）。

## Verification（验证）

1. 修复后同配置（ambient token PRESENT）`ctest -R qml_consent_check` → **Passed 3.73s**。
2. 负向对照 MUTATION-NX1（注释掉 token qunsetenv，精确 patch）→ rebuild →
   `CONSENTFAIL: harness credential contract violated`（exit 1，1.58s，入口即拒绝 ⇒ 零调度）
   → 精确逆向还原 → rebuild → **Passed 5.58s**，residue 0（grep 无残留）。
3. `candidate_transport`（P01–P24）Passed；full Release ctest 见任务档案 §78。

## Regression Protection（回归保护）

- R07 断言本身（无 provider 结果不得写 candidate）保持不变——本轮它正是探测点。
- `runConsentCheck` 入口凭据 guard：任何未来回归（删除清理、或新增读取环境凭据的
  路径）都会变成响亮的确定性失败，而不是静默真实网络调用。

## Governance disclosure（必须向 Human 披露）

- WorkBuddy 2026-09-30 14:53 的 ctest 运行与 ZCode Run A（RCA 复现）**各自可能已发生
  一次真实 ModelScope 推理**（payload = 8 行种子手册文本；token 为 Human 的 User 环境凭据；
  模型 = 接受的默认 `Qwen/Qwen3.5-27B`）。两轮均无任何 Agent 读取/打印 token 值；
  seed 文本非机密。此披露不构成对真实推理的授权请求，仅如实记录已发生事实。
- 修复后 deterministic gates 在结构上不可能再发起 provider 调度。

## Lessons（教训）

1. "测试是确定性的"必须包括**环境输入**：把 ambient env 当作隐藏参数的 harness
   在某个用户机器上必然失效——且失败模式可以是"静默真实副作用"而非可见断言失败。
2. 安全边界（credential/consent）要有**结构化防泄漏**：与其信任调用者不设凭据，
   不如在 harness 入口显式清除 + 入口断言。
3. GUI-subsystem exe 的 gate 必须**声明 stderr 契约**，否则失败不可诊断
   （本次"crash"之谜的直接来源）。

---

## 追加澄清批注（2026-10-02 · SESSION P-R1F · 最小澄清，不改写以上任何历史证据）

本文记录的未授权事件**仅限** P-R1C 期间的两起：WorkBuddy 14:53 ctest 运行（分级
LIKELY）与 ZCode Run A（分级 STRONGLY SUPPORTED）。其后由 **Human 显式授权、经产品
同意流**完成的 P-R1 live ModelScope smoke（T027 §79：Agree → Running → PendingReview，
PASS）**不**属于本 issue 的未授权事件，两者不得混同。既有 Observed / Expected /
Evidence / Root Cause / Fix / Verification / Regression Protection 各节作为历史事实
原样保留。
