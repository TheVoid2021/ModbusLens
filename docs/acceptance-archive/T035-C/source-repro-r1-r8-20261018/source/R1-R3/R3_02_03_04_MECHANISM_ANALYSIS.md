# R3_02 / R3_03 / R3_04 — 三项机制分析（合并文件，对应 §9 建议文件）

> 说明：R3_02（readiness 机制）/ R3_03（K125 事务链）/ R3_04（register-map 布局）合并为本文件，
> 内容完整；`R3_05`（semantic 生命周期）与 `R3_07`（最小修复计划）并入 `R3_08_FINAL_REPORT.md` §F / §L。
> **未独立建文件的原因**：避免把同一分析复制到多份文件；内容未删减。

---

# R3_02 — `register_readiness` 实际失败机制

## 结构性缺陷（CONFIRMED，源码级）

`tests/test_register_readiness.cpp`：
- `c16_deterministicAndSideEffectFree()`（`:354` 起）与 `d18_wholeValidationChainHasNoSideEffects()`（`:613` 起）
- 均使用 `snapshotDir(QDir::tempPath())` 对**全机共享 TEMP 目录**做 `fileName|size|mtime(ms)` 全量快照
- 执行被测链后 `QCOMPARE(after, before)`

**⇒ 任何同机进程/其他测试在窗口内写 TEMP 即失败。**

## §2.2 补查结果（只读，未新增执行）

| 检查项 | 结果 |
|---|---|
| 失败是非零退出还是异常结束 | R1 CTest 记录为 `***Failed 0.77 sec`，**无 stdio、无 exception code、无 crash dump 痕迹** ⇒ 与「`QCOMPARE` 断言不相等后 QTest 正常退出（return 1）」一致 |
| 是否进入 c16/d18 | **无法从 CTest 日志确认**（`--output-on-failure` 未捕获该测试输出）⇒ **PARTIAL** |
| 其他测试是否生成 TEMP 文件 | **是**（Qt `QTemporaryFile`/`QTemporaryDir`、QML/QML 缓存、40+ 测试进程均可能） |
| 原始 `TEMP`/`TMP` 指向 | 进程继承的 `%TEMP%`（`C:\Users\付\AppData\Local\Temp`，**全机共享**） |
| 是否有残留文件/后台子进程 | 未发现（R1 后 `_repro_evidence` 无异常残留） |

## 受控验证（诊断 #1）

- **独立 TEMP（`r3_isolated_temp_17482`）下运行：27 passed / 0 failed / EXIT 0**（9 ms）
- 隔离目录残留 = 0（测试自身不写 TEMP，符合「纯函数」预期）

## 结论

```
TEST DESIGN VULNERABILITY = CONFIRMED
ORIGINAL CTEST FAILURE CAUSE = STRONGLY SUPPORTED
```
（机制完全解释现象：单独/独立 TEMP = PASS，全套 = FAIL；但**缺 c16/d18 的运行期输出**，故原始那次失败保持 STRONGLY SUPPORTED 而非 CONFIRMED。）

---

# R3_03 — K 场景（125 寄存器）完整事务链分析

## 源码链（`src/main.cpp`）

```
:10829 push: window->resize(1024,720); scanRead(1, 0, 125, 1000);
              └ scanRead (:10242) = transport->setCompleteReadImmediately(false)
                                    + controller->readHoldingRegistersOnce(1,0,125,1000)
:10835 push: transport->completeReadWithBytes(responseWith(1, values), 25ms)
              └ responseWith (:10302) = data[0] = values.size()*2 = 250 = 0xFA
                                        + encodeRtuFrame(...)  → 255 字节 RTU 帧
:10840 push: if (controller->readResultValueCount() != 125) fail("READFAIL K: value count is ...")
```

## 关键装置缺陷（源码级，CONFIRMED）

| 位置 | 缺陷 |
|---|---|
| `HarnessWriteTransport::simulateAdapterRemoval`（`:5238`） | 只 `pending_.reset()` + emit terminal，**未复位 `session_`**（C2.1 引入的成员） |
| `HarnessWriteTransport::startActiveRequest`（`:5075`） | `session_.beginActiveRequest(request);` **返回值被丢弃** —— 若 session 处于 `AwaitingResponse`，begin 失败被静默忽略 |
| `HarnessWriteTransport::completeReadWithBytes`（`:5188`） | 改用成员 `session_` 后，**feed 使用 session 的旧 descriptor**，而非 `pending_`（当前请求） |

## J → K 的状态传递（决定项）

```
J 场景 (:10794) scanRead(1, 0, 2, 1000)   ← session_ 进入 AwaitingResponse（qty=2）
   ↓ (:10795) transport->simulateAdapterRemoval(...)  ← pending_ 被清、session_ 未复位
K 场景 (:10829) scanRead(1, 0, 125, 1000)
   ↓ session_.beginActiveRequest(qty=125) → 返回 error（AwaitingResponse），被丢弃
   ↓ pending_ = qty=125（但 session_ 仍持 qty=2）
   ↓ (:10835) session_.feedResponseBytes(255 字节)
   ↓ 期望 7 字节（qty=2）却收到 255 字节 ⇒ 不完整/oversized ⇒ fed 无 analysis
   ↓ fallback session_.onResponseTimeout(25ms) → analysis（非 ReadValues）
   ↓ emit transactionCompleted（stale guard 通过：pendingRequest_ 匹配）
   ↓ controller->readResultValueCount() = 0
```

## 分类

```
K 场景失败层级 = K4/K5 之间：
  请求确实"发出"（pending_ 更新）但 session 未按新 descriptor 重建 ⇒
  完成事件被接受（K4 通过）但 analysis 非 ReadValues ⇒ 结果被拒（K5）
⇒ K5 CONTROLLER_RESULT_REJECTED（根因 = 装置 session_ 未复位 + begin 返回值未检查）
```
**证据等级：STRONGLY SUPPORTED**（机制链完整闭合、与「value count=0 且无 crash」现象一致；缺运行期 trace 逐点实证）

**不是产品缺陷**：`HarnessWriteTransport` 是 `src/main.cpp` 内的**门禁装置类**，仅 `--qml-*-check` argv 分支可达，正常 GUI 启动不可达；`SerialTransactionSession` / `AnalysisController` 未参与缺陷成因。

---

# R3_04 — `qml_register_map_check` 布局与滚动可达性

## 诊断 #2 关键输出

```
REGGEO entryEditorDialog: x=89.5 y=73.5 w=879 h=595     ← bottom = 668.5（在 1000×700 内）
REG: stage 16: 1000x700 reachable incl. dialog           ← 对话框本体可达
REG: stage 17: editor fields focusable                   ← 编辑器字段可聚焦（PASS）
REGFAIL: regUnitField outside: x=257.5 y=698.5 w=729 h=32 win=1000x700
                                                          ← bottom = 730.5（超窗口 30.5 / 超 dialog 62）
```

## 必须回答的六问

| # | 问题 | 回答 |
|---|---|---|
| 1 | 字段是否在滚动容器内？ | **未验证**（门禁未输出 flickable/ScrollView/clip 信息；`regUnitField` 在 `entryEditorDialog` 的 `ColumnLayout` 内，静态父级链可见但滚动容器是否存在需运行期确认） |
| 2 | 能否通过正常滚动访问？ | **NOT VERIFIED**（`stage 17: editor fields focusable` 通过说明字段可聚焦，但未证明可视/可滚动到） |
| 3 | 若能滚动，原断言是否过严？ | **UNRESOLVED** |
| 4 | 若不能滚动，是否真实布局缺陷？ | **UNRESOLVED** |
| 5 | C1 新增字段是否改变页面高度？ | **PARTIAL**：`DeviceProfilePage.qml` HEAD 2134 行 → WIP **2558 行（+424）**；`entryEditorDialog` 高度 595 固定，字段 y=698.5 已超出其底部 ⇒ **dialog 内容高度已超容器** |
| 6 | 是否有更小的合理布局修复？ | **建议**：`entryEditorDialog` 内容改为 `Flickable/ScrollView`（若当前不是），或压缩行高/间距使字段在 595 内；**不应**通过放宽 `requireInsideWindow` 掩盖 |

## 结论

```
LAYOUT_ROOT_CAUSE = PARTIAL
FIELD_SCROLL_ACCESSIBILITY = NOT VERIFIED
PRODUCT_LAYOUT_DEFECT = UNRESOLVED
TEST_ASSERTION_DEFECT = UNRESOLVED
```
**不得**判为「测试断言过严」（未验证滚动可达）；**也不得**判为「产品必然缺陷」（未验证不可达）。
