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