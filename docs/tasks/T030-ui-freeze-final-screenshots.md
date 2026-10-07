# T030 — UI FREEZE + FINAL SCREENSHOT CAPTURE R1

- **Purpose**：T029 Human Review = ACCEPTED（UI P0=0 / P1=0 → 按冻结治理 **SKIP UI FIX**，RUI-11..16 = DEFERRED P2）；本轮正式宣布 **UI FREEZE** 并用 accepted build 生成 Final Screenshot Set。
- **Accepted executable**：`build/package-extract/ModbusLens-2.0.0-windows-x64/ModbusLens.exe` = **7,029,729 B**，SHA-256 `518b9485ccedbc17f366038e08ada32c4fe2ff00d35e94543c1dfaf4be523acb`（本轮重测一致；provenance = §116/§117 canonical chain：worktree-LKGC candidate → v2.0.0 package → R5G 公开资产同字节）。
- **Product diff before capture** = 0（`b18e378…`→HEAD 产品路径 EMPTY；exe identity 与 accepted evidence 一致）。
- **UI FREEZE = ACTIVE**（自本任务起：除非 Human 重新打开 P0/P1 blocker，production UI 不得再改；navigation/layout/theme/数值/行为/icon/header/button/诊断呈现任一变更将使 FINAL SCREENSHOTS = STALE）。

## 1. Runtime / capture procedure

```text
runtime = Windows platform plugin + 真实桌面会话（无 offscreen；真实字体渲染）
capture = 既有冻结 gate --qml-evidence-capture <dir>（M9-B4.4 harness：
          业务状态迁移使用与人工流程相同的 Controller 命令，每次抓取前
          断言状态——workspace index / nav selected / Diagnosis tab /
          deterministic facts（T008 DEMO-1..4 常量），错误状态即 exit 1）
分辨率  = 逻辑 1024x720（物理 1280x900 @ 125% DPI——与仓库既有 accepted
          截图（m9f-f2-*、geometry-*）同一约定）
transcript = build/_evidence/relr-final-capture.log（RC 0，22 states，0 EVIDENCE FAIL）
05-serial 来自 write-foundation gate（同一 accepted exe，1024x720）：
          c4-confirmation-1024x720.png（FC16 确认写入对话框）
```

## 2. Final Screenshot Set（docs/assets/screenshots/）

| Final file | 来源 state（同一 accepted exe） | 内容 | SHA-256 | 尺寸 |
| --- | --- | --- | --- | --- |
| 01-hero-dashboard.png | m9c-dashboard-demo-1024x720 | Simulator 确定性演示批次：已观测 4 / 已完成 4 / 成功率 25.0% / 平均延迟 25.0 ms / 成功·异常·CRC·超时分布条 / 需关注 3 条 | f231031517273cd7a2250344718881a5533511e4dc27fa40b72a73b486c6ebc0 | 1280x900（逻辑 1024x720 @125% DPI） |
| 02-transactions.png | m9d-transactions-demo-selected-1024x720 | 事务列表（成功/异常 0x02/CRC/超时）+ 选中 CRC 事务明细区 | acb8004508c1331296a9d4551cd7dd7f07d09f8cc32770fc3b81c631000b8533 | 同上 |
| 03-replay-analysis.png | m9b4-replay-notice-1024x720 | **已加载 Replay**：回放模式 · t015_unsupported_fc08.mlog + 不支持记录提示（功能码 0x08 未计入统计） | b1d5c1a441d2548d808bd8f3086d1d9f4119ef1d080a6fa2229220836473030a | 同上 |
| 04-diagnosis-baseline.png | m9b5-diagnosis-baseline-1024x720 | Baseline 诊断（确定性 demo 事实）：CRC 错误 1 / 无响应超时 1 / 设备异常 0x02 1 + 建议检查 6 项 | 720a9cced9d254a4a94924dca34da14cf864c89ad9da7b2974cbee2ef3a3d0ea | 同上 |
| 05-serial.png | c4-confirmation-1024x720（write-foundation gate） | Serial 已连接（COM_HARNESS fixture）+ FC16 写入 + **确认写入对话框**（目标/寄存器/值清单/PDU/RTU hex） | 0526cc9b16c6340565eb8514cca7f8172a0398ae47cf52e49cda84f24fd42cde | 同上 |

逐张人工检查（§7 清单）：正确页面 ✓ / 正确状态 ✓ / 非空白 ✓（min≥28）/ 非旋转 ✓ / 无 clipping / overlap ✓ / 字体真实正常 ✓ / 无 API key、token、私人路径 ✓ / 无鼠标、IDE、terminal、测试框架窗口残留 ✓ / 无 Qt platform error dialog ✓ / 无 offscreen 字体缺失 ✓ / 所有数字来自实际 deterministic run ✓ / 未编辑 PNG 内容 ✓。

**尺寸口径说明**：物理 1280x900 = 逻辑 1024x720 @ 125% DPI，与仓库既有 accepted 截图约定完全一致（m9f-f2-*、geometry-* 同为 1280x900，文件名携带逻辑尺寸）。

## 3. Per-image disposition

| Final file | Disposition |
| --- | --- |
| 01-hero-dashboard.png | **READY FOR HUMAN REVIEW** |
| 02-transactions.png | **READY FOR HUMAN REVIEW** |
| 03-replay-analysis.png | **READY FOR HUMAN REVIEW**（已加载 Replay + 不支持记录提示；如 Human 偏好统计列表更丰富的画面，此张为 DROP/REPLACE 候选——dashboard-replay 态可替代，由 Human 决定） |
| 04-diagnosis-baseline.png | **READY FOR HUMAN REVIEW**（Human Decision：不伪造 Agent answer，改用 Baseline 确定性诊断） |
| 05-serial.png | **READY FOR HUMAN REVIEW**（COM_HARNESS 为既有 fixture 命名，仅出现于测试态） |

## 4. Frozen decisions

```text
T029 HUMAN REVIEW   = ACCEPTED
MINIMAL UI FIX      = SKIPPED（Human decision：P2 = DEFER）
UI FREEZE           = ACTIVE（RUI-11..16 = DEFERRED P2）
RUI-11..16          = DEFERRED P2（Replay/Agent 空态提示、留白密度、
                      contentHash 展示、演示数据命名、header 版本号）
FINAL SCREENSHOTS   = CAPTURED / PENDING HUMAN REVIEW
NON-UI GAPS         = OPEN（LICENSE / 02_ARCH 冲突 / ctest 数字 /
                      GitHub 元数据 / CHANGELOG / .github —— 未顺手修）
README              = UNCHANGED（本轮不改；integration 是下一 gate）
REBUILD/REPACKAGE   = NONE（截图来自既有 accepted build，未重编译）
```

## 5. Next gate

**HUMAN REVIEW OF FINAL SCREENSHOTS**（5 张逐张目检）→ 通过后进入
README/文档/GitHub GAP CLOSURE（README 引用截图、LICENSE 决策、docs
冲突修正、GitHub 元数据——需单独授权）。

## 6. Boundary

本轮零产品/QML/打包/candidate/ZIP 改动（`b18e378..HEAD` 产品路径持续
EMPTY）；capture 使用的 `--qml-evidence-capture` / `--qml-write-foundation-check`
均为既有冻结 gate（harness-only，无产品行为变更）；本任务所在提交 =
docs/assets-only（NOT LKGC）。


---

# T030-R2 — FINAL SERIAL SCREENSHOT RECAPTURE（2026-10-07）

## R2.0 Human review result（T030 §2 shots）

```text
01-hero-dashboard.png   = ACCEPTED
02-transactions.png     = ACCEPTED
03-replay-analysis.png  = DROP FROM README / KEEP AS EVIDENCE
04-diagnosis-baseline.png = ACCEPTED
05-serial.png           = RECAPTURE REQUIRED
```

拒绝理由（逐字归档）：①COM_HARNESS 暴露自动化 fixture/internal naming；
②页面同时出现"串口已打开"与"未检测到串口"视觉状态矛盾；③FC16 一次
显示 40 registers，信息密度过高。**不是 UI layout defect**；UI FREEZE
继续有效。

## R2.1 recapture 尝试与环境实测（fail-closed 过程，如实归档）

```text
accepted exe 重验 = 518b9485…523acb / 7,029,729 B（一致）
本机串口环境实测 = Windows 枚举出 COM3 / COM4
  （"蓝牙链接上的标准串行"——Bluetooth SPP 虚拟口）
attempt 1/2（既有 gate）= --qml-write-foundation-check 使用非 production
  fixture（fake transport，名称 COM_HARNESS 硬编码于 gate harness）→
  产生被 Human 拒绝的同一状态，且下拉框（真实端口枚举）与 fixture 状态
  矛盾——即 Human 指出的缺陷根源。
attempt 3（真实 UI 自动化，real clicks/typing on accepted exe，9 个
  诊断截图 build/_evidence/relr5-serial/）：
  d2 刷新串口后仍 = "未检测到串口"；
  d3/d4 下拉打开但无任何条目（popup 空列表）；
  d8 确认写入不可达（写入按钮保持禁用 = M10 写安全边界在自动化下
  依然成立）。
根因 = 产品**诚实**枚举：本机无物理在位的串口设备（COM3/COM4 为
  Bluetooth phantom 口，Qt setupapi 按 DIGCF_PRESENT 正确跳过；
  .NET SERIALCOMM 注册表视图包含它们，故 PowerShell 看得到而产品
  看不到——产品行为正确）。
→ 一致的 "COM3 @ 9600" 真实产品状态在本机**无法构造**；唯一路径是
  被 Human 拒绝的 COM_HARNESS fixture 或修改产品/测试代码（均被禁止）。
```

## R2.2 处置（按 packet §6 fallback）

```text
05-SERIAL-RECAPTURE = BLOCKED
RECOMMENDATION      = DROP 05 FROM README
FINAL SCREENSHOT SET = 4 张（01 hero / 02 transactions / 04 diagnosis
                      baseline）
docs/assets/screenshots/05-serial.png = 已从仓库移除
  （被拒绝的 COM_HARNESS 版本保留于 Git 历史 commit 6982c78，未丢失；
   build/_evidence/relr5-serial/ 的诊断截图为本地 ignored 证据）
UI FREEZE           = 继续 ACTIVE（本轮零 UI/产品改动）
01/02/04            = 维持 ACCEPTED（不受本轮影响）
```

## R2.3 状态

```text
PRODUCT CODE / QML = UNCHANGED；README = UNCHANGED（integration 下一 gate）
PUSH = NONE；TAG/Release = UNCHANGED；REBUILD/REPACKAGE = NONE
NEXT = README integration（引用 01/02/04）+ 其余 non-UI GAP 依 Human 授权处理
```
