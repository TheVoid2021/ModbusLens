# T029 — UI RELEASE AUDIT R1：发布级 UI 视觉审计（v2.0.0）

- **Purpose**：按 T028 §5（RUI-01..10）与 §12（审计/修复分离）对 v2.0.0 最终 UI 做发布级视觉审计；只记录，不修复。
- **Baseline**：HEAD `8fc4f755…`（RELEASE-PREP-R1 后）；accepted build = **从 canonical ZIP 解压的 `build/package-extract/ModbusLens-2.0.0-windows-x64/ModbusLens.exe`**（7,029,729 B，SHA-256 链见 §116/§117）。
- **Environment**：Windows platform plugin + 真实桌面会话（**未使用 offscreen**，避免无字体渲染）；产品未做任何修改。
- **Audit runtime**：既有冻结 gates 自带的状态构造 + `grabWindow` 证据导出：
  - `--qml-geometry-check --qml-geometry-dump`（5 工作区 × 2 尺寸 + demo 批次 + 选中事务明细 = 14 states）
  - `--qml-manual-qa-check --qml-write-dump`（Device Profile + Manual Q&A 面板 = 9 states）
  - `--qml-write-foundation-check --qml-write-dump`（Serial 连接 + FC16 确认写入对话框 = 4 states）
  - 共 **27 张 audit evidence PNG** + 4 份 run log → `docs/evidence/ui-release-audit/`（**AUDIT EVIDENCE / NOT FINAL SCREENSHOT**）。
- **Resolutions**：1024x720（默认）与 1000x700（应用 minimum，`Main.qml minimumWidth/Height 1000/700`）。
- **Gate results**：geometry RC 0（PASS，0 GEOFAIL）；manual-qa RC 0；production-write RC 0；write-foundation RC 0。

## 1. Pages / navigation mapping（当前真实导航）

Rail 6 项：事务(Transactions) / 总览(Dashboard) / 通信(Serial+写) / 回放(Replay) / 诊断(Diagnosis) / 设备(Device Profile+Manual Intelligence)。T028 名称与实际 UI 的 mapping：T028 "工作台/Overview"=总览，"设备/Serial/Device Profile"=设备（含串口与手册智能）。右侧 header 含**模式指示器**（模拟器模式 / 串口模式，随数据源切换）+ 清空结果。

## 2. States covered（27 evidence + 覆盖说明）

Empty（5 页 × 2 尺寸）、Simulator demo 批次（Dashboard 双尺寸）、选中事务明细（双尺寸）、Manual Q&A（打开/长结果顶部/滚动到底/短结果/关闭态/重开）、Serial 无硬件、Serial 已连接（COM_HARNESS fixture）、FC16 确认写入对话框（双尺寸）。

**本轮未获得视觉证据的状态**（如实记录）：Replay **已加载** demo_v1.mlog 的页面态（既有 gates 不做 load+dump；功能已由 H4 人工验证）；Diagnosis 的 Baseline/AI tab 当前构建 dump（geometry pass 落在 Agent tab）；hover/pressed 态（合成事件不可靠）；窗口标题栏/任务栏图标（grabWindow 仅客户区）。

## 3. Global visual audit（PASS / ISSUE / NOT VERIFIED）

| 项 | 结果 | 备注 |
| --- | --- | --- |
| Application icon / taskbar icon | NOT VERIFIED（本轮） | grabWindow 不含标题栏/任务栏；M9-E/F 既有验收在案；UI FREEZE 时以发布包启动复核 |
| Window title | NOT VERIFIED（本轮，同上） | — |
| Top header / 模式指示器 | PASS | 随数据源切换（模拟器模式/串口模式实测） |
| Navigation rail | PASS | 6 项；选中态清晰；1000x700 下不挤压 |
| Typography / heading hierarchy / readability | PASS | 总览/段落/卡片标题层级一致 |
| Spacing / alignment / margins | PASS（附 RUI-11 备注） | 各页下半部留白偏大（见 RUI-11，P2） |
| Button hierarchy / primary-secondary | PASS | 主操作蓝底（运行演示批次/写入/确认写入），次操作浅底 |
| Input / output styling | PASS | 输入框、PDU/RTU 十六进制预览（mono）清晰 |
| Tables | PASS | 列（设备/功能码/状态/耗时/异常码）无塌缩；空态占位居中 |
| Selected / hover / pressed / disabled / focus | selected PASS；disabled PASS；hover/pressed NOT VERIFIED（合成事件）；focus 既有 M9-F F1 证据 | — |
| Empty state | PASS | 暂无通信记录 / 未打开设备档案 / 尚未运行基线诊断（诚实占位） |
| Loading state | NOT APPLICABLE（确定性加载瞬时完成，无可捕获 loading 态） | — |
| Success/Warning/Error/Neutral 语义色 | PASS | 成功绿、异常/错误红、需关注黄、中性灰（分布条+状态卡+表格行一致） |
| Dividers / borders / cards | PASS | 卡片/分区边框一致 |
| 长 中文 / 英 文本 | PASS | 30 条长答案换行+滚动；Clause 长文本换行 |
| Hex / 协议值 / 延迟 / 大数值 | PASS | PDU/RTU hex、25ms/1000ms、大计数 |
| Scrollbar behavior | PASS | Manual Q&A Flickable 滚动到底可达（UX-R2 证据） |
| 1024x720 / 1000x700 | PASS | 全 14 state 无 clipping/overlap/意外滚动；1000x700 = minimum size 行为正常 |

## 4. Product-specific audit（§8）

1. **完整桌面产品观感**：PASS——Dashboard/通信页在真实字体渲染下是成品级观感，非测试 harness。
2. **Dashboard 信息层级**：PASS——统计卡→分布条→需关注结果→基线诊断入口，层级清楚。
3. **Transactions 可读性**：PASS——设备/功能码/状态/耗时/异常码一列可见，选中行+详情区联动。
4. **Replay 模式感知**：PASS（弱项）——空态只有"加载回放日志"卡，无额外模式提示（RUI-12，P2）；**已加载态本轮无 dump**（H4 已人工验证功能）。
5. **Diagnosis 三层区分**：PASS——三 tab 层级清楚；Agent 空态无 provider 提示（RUI-13，P2）。
6. **Serial/Device 无硬件态**：PASS——"未检测到串口"诚实呈现，写入禁用态明确，不是残缺页面。
7. **空白/密度/残留**：各页下半部留白偏大（RUI-11，P2）；无 debug/demo wording 残留；无贴边控件。
8. **误导性文字**：未发现（唯一 harness 名 COM_HARNESS 出现在测试 fixture 态，非最终截图素材）。

## 5. RUI-01..RUI-10 disposition

| ID | Original concern | Page/state | Resolution | Observed | Evidence | Sev | Disposition |
| --- | --- | --- | --- | --- | --- | --- | --- |
| RUI-01 | Device Profile 工作区无专属视觉验收 | 设备页整页 | 本轮 27 evidence 覆盖档案列表/详情空态/手册导入卡 | 布局/禁用态/空态正常，无 P0/P1 | ux-r1-01/02/03/04 | — | **PARTIALLY CONFIRMED**（静态态已审；表单交互细节未穷举） |
| RUI-02 | Manual Q&A 面板视觉 | 设备页 Q&A Drawer | ux-r1/ux-r2 9 states | 收回/滚动/短长结果全部正常；无 P1 | ux-r1-01..04, ux-r2-01..05 | — | **CONFIRMED**（UX-R1/R2 修复视觉复验通过） |
| RUI-03 | 候选卡片视觉一致性 | 设备页候选卡 | 手册卡选中/删除态已审 | 卡片一致；Accept/Edit/Discard 三态未在本轮 dump 中 | ux-r1-01（qa-ux 选中） | — | **PARTIALLY CONFIRMED** |
| RUI-04 | 应用/任务栏图标发布级复核 | 窗口外框 | grabWindow 不含 | — | — | — | **BLOCKED / NOT VERIFIED**（UI FREEZE 时发布包启动复核） |
| RUI-05 | 标题/头部与版本呈现 | header | m9c/m9d dumps | header 无版本号展示（仅产品名+模式指示器） | m9c-dashboard-demo | P2 | **PARTIALLY CONFIRMED**（RUI-16：考虑 header/about 显示版本） |
| RUI-06 | 长文本边界（非 Q&A 页） | 事务明细/回放/诊断输入 | dumps | 换行正常无截断 | m9d-transactions-detail | — | **CONFIRMED** |
| RUI-07 | 空态 | 5 页 | dumps | 占位诚实居中 | m9d/m9b4/m9b5 | — | **CONFIRMED** |
| RUI-08 | 语义色一致性 | 全局 | dumps | 绿/红/黄/灰语义一致 | m9c-demo/m9d | — | **CONFIRMED** |
| RUI-09 | 双尺寸全 6 工作区回归 | 14 states | geometry gate RC 0 | 无 clipping/overlap | 全部 geometry dumps | — | **CONFIRMED** |
| RUI-10 | DS token 一致性（M12 新增面） | Q&A 面板/手册卡 | dumps | 新增面复用 DS token，无不协调 | ux-r1-01/ux-r2-* | — | **CONFIRMED** |

## 6. New findings（RUI-11..16，全部 P2）

| ID | 现象 | 页 | Severity |
| --- | --- | --- | --- |
| RUI-11 | 各工作区下半部大面积留白（信息密度偏上） | Dashboard/Transactions/Replay/Diagnosis | P2 |
| RUI-12 | Replay 空态缺少工作区提示文案（仅"加载回放日志"卡） | Replay | P2 |
| RUI-13 | Agent tab 无 provider 时空态无提示（不知需配置 Key） | Diagnosis | P2 |
| RUI-14 | 手册卡展示 contentHash（工程元数据暴露在 UI） | 设备页 | P2 |
| RUI-15 | 演示数据命名随意（profile "1111"）——final shots 需真实感数据 | 设备页（数据层，非代码） | P2 |
| RUI-16 | header 无版本号展示（窗口标题本轮不可见） | 全局 | P2 |

**UI P0 = 无；UI P1 = 无；UI P2 = RUI-11..16 + RUI-04 复核项。**

## 7. Final Screenshot Set readiness（T028 §6 核对）

| 文件 | 就绪状态 |
| --- | --- |
| 01-hero-dashboard.png | **READY AFTER FREEZE**（m9c-dashboard-demo 证明状态可达且观感达标） |
| 02-transactions.png | **READY AFTER FREEZE**（selected-detail 状态可达） |
| 03-replay-analysis.png | **NEEDS CONTENT/STATE FIXTURE**（需加载 demo_v1.mlog 的 dump 路径；H4 已人工验证功能；UI FREEZE 轮需提供加载手段或 Human 手动加载） |
| 04-diagnosis-agent.png | **NEEDS CONTENT/STATE FIXTURE**（Agent 真答需 provider；备选 = demo 批次后的 Baseline tab，READY AFTER FREEZE） |
| 05-serial.png | **READY AFTER FREEZE**（c4 确认态/FC03 结果态均可达；若认为展示价值不足可 DROP——建议保留，写路径是 M10 亮点） |

## 8. Minimal UI Fix Plan（下一轮；本轮不执行）

**Fix Batch A（P0/P1）**：无——审计未发现任何 P0/P1。

**Fix Batch B（P2，可选，UI FREEZE 前 tiny QML 文案级）**：
- RUI-12：Replay 空态加一行提示（QML Label；文件 `ReplayPage.qml`；回归风险极低；验证=geometry+目测）。
- RUI-13：Agent tab 无 provider 提示（文件 `DiagnosisPage.qml`；同上）。

**Fix Batch C（P2，UI FREEZE 时数据/流程级，非代码）**：
- RUI-15：final shots 使用真实感 profile/手册命名（经 UI 操作构造，不改代码）。
- RUI-04：发布包启动复核图标/标题栏（打包启动即得）。

每个修复项的 Required verification：geometry gate + 相关 gate 重跑 + 目测证据。

## 9. Non-UI gaps preserved

GAP-03（02_ARCH Serial 表述）、GAP-04（ctest 数字）、GAP-05（LICENSE）、GAP-06（GitHub 元数据）、GAP-07..10 —— **保持 OPEN / DEFERRED / HUMAN DECISION**，未被本轮关闭或改变。

## 10. Verdict

UI 无 P0/P1；v2.0.0 UI 达到发布级；剩余全部为 P2 打磨与两项截图状态准备。审计证据 27 PNG + 4 log 保存在 `docs/evidence/ui-release-audit/`（NOT FINAL SCREENSHOT）。
