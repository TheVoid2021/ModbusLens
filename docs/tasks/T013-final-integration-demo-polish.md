# T013 — Final Integration & Demo Polish

- **状态**：IN PROGRESS — **Phase A = FINAL POLISH AUDIT / AWAITING USER IMPLEMENTATION SCOPE REVIEW（2026-09-11，docs-only）**
- **背景**：T011/T012 及 Post-Closure Stabilization 全部完成（LKGC `3572cf7`）。T013 不是第二轮产品开发——只做 UI visual polish、用户术语 polish、最终 Demo 流程、README/部署/演示证据一致性与面试表达就绪。

---

## 1. Phase A Scope 定位

包含：A. UI visual polish；B. user-facing terminology polish；C. final demo flow polish；D. README / deployment / demo evidence consistency；E. portfolio screenshot readiness；F. interview presentation readiness。
默认禁止（记 future enhancement 而非塞 T013）：新 Modbus 功能码（FC06/FC10）、write register、polling scheduler、新 Agent Tool、startAddress/quantity 增强、database、login、chat history、RAG、MCP、multi-agent、新 Provider、新协议、新模式、telemetry。

## 2. UI Visual Audit（现象 / 证据 / 影响 / 建议 / 优先级）

| # | 现象 | 证据位置 | 影响 | 建议（Implementation 时执行） | P |
| --- | --- | --- | --- | --- | --- |
| V1 | 深色主题下 Diagnosis 正文与次要说明文字对比度偏低 | 用户历史人工发现 + Main.qml 硬编码灰色 `#606060`/`#909090`/`#6080a0` | 长文阅读疲劳、"未成品"观感 | 建立 4~6 个主题色常量并整体调亮次级文字、error 色保持醒目 | P0（HUMAN REVIEW：具体色值定稿需真实屏幕对照确认） |
| V2 | Baseline / AI 解释 / Agent 问答三块视觉身份不足 | Main.qml diagnosisContent Column:三块均为同款式 Label 序列 | 面试时难以一眼区分"确定性事实 vs LLM 解释" | 各块加小型节标题样式/左侧色条（纯样式，不动语义） | P0 |
| V3 | Section title hierarchy 不明显（“AI 解释/Agent 问答/确定性基线诊断”字号与正文接近） | Main.qml Label font.bold 无字号阶梯 | 层级模糊 | title 统一字号阶梯（16/13）+ 分隔线 | P1 |
| V4 | Statistics cards hierarchy 扁平 | RowLayout 卡片等宽等样式 | 成功率/延迟与其他计数无差别 | 关键指标（成功率/平均延迟）与计数卡分层（描边/色调） | P1 |
| V5 | Busy / normal / error 状态指引弱（“请求中.../分析中...”与 error 同为普通 Label） | Main.qml 两处 busy label `#6080a0` | 状态变化不清 | busy 加前缀图标式文本或统一状态色规范；error 保持红色体系 | P1 |
| V6 | 滚动条可见性与手感（diagnosisFlick ScrollBar AsNeeded） | Main.qml diagnosisFlick | 长 Agent 回答时不易发现可滚动 | ScrollBar 常显（AlwaysOn 或 hover 增强，Qt6 能力内） | P1 |
| V7 | 长 Agent answer 阅读排版（无行高/段落间距控制） | Main.qml PlainText Label 默认 lineHeight | 900+ 字长文可读性一般 | lineHeight≈1.35 + 段间 margin | P1 |
| V8 | Recent Transactions 可扫读性（四列等宽） | transaction delegate | 扫读尚可，可微调列宽/斑马纹 | 列宽比例微调 + 可选斑马纹 | P2 |
| V9 | 最小尺寸 1000x700 下密度 | ApplicationWindow minimumWidth/Height=1000/700 | 已由人工 Offline smoke A 通过 | 无明显问题（记录为已验收） | — |
| V10 | 常见大桌面尺寸布局 | 1298x947 实测无障碍 | 无问题 | 无需改动 | — |

## 3. ISSUE-004 Layout Preservation Audit（结论：全部建议不触碰已验证架构）

- root 不做 full-page scrolling；Horizontal SplitView；左 Diagnosis pane 内部独立 Flickable；右 Recent Transactions 独立 ListView。
- V1~V8 全部是**样式/色值/字号**级别，不新增 ScrollView、不改容器层级、不引入长答案撑窗路径；long-answer containment 由现有 Flickable 契约继续保证。
- Implementation 时以 qml_smoke + 用户 Manual UI smoke（A/B/C/E/F/G/J）复核 ISSUE-004 类回归。

## 4. User-facing Terminology Audit

- **外部用户可见 UI 标签**（已中文化且良好）：成功/异常/CRC 错误/超时/协议错误/进行中/设备异常 0x02 等 —— 无动作。
- **AI/Tool 通道曾直接抛出的英文术语 → 建议在 prompt/output 指导下述自然中文化**（Implementation 时仅改 prompt 措辞，零 authority 语义变化）：
  - `evidence_scope` → 当前观测范围 / 当前观测批次
  - `multiple anomaly types` → 观察到多种异常
  - `shared root cause` → 共同根因（“不能据此判断它们存在共同根因”）
  - `current_observed_batch` → 当前观测批次
- **AI 回答历史真实出现的层术语校准建议**（T012 Live evidence 记录，非事实错误但易误读）：
  - “Exception（功能码异常）” → “Modbus 异常响应”
  - “链路层完整性” → “CRC 校验失败”（链路层仅在确讲物理/数据链路时用）
  - “传输层无响应” → “响应超时”
  - 建议系统指令给出“自然中文术语示例表”而非禁令长文（保持 INIT 已有 authority 规则不动）。
- Baseline formatter / Controller 错误文案 / QML labels：现状已符合语言政策；无需改。

## 5. Register-address Limitation（保持，不扩数据模型）

- 当前事实链：Exception 0x02 = Illegal Data Address + 建议核对 register map / 请求地址范围 / 设备寄存器文档。**不得**声称具体寄存器地址有问题。
- 记录为 known limitation + future enhancement（T015+ candidate）+ interview trade-off；非 T013 blocker；明确写入 README limitations。

## 6. Final 3~5 分钟面试 Demo 主线（设计稿；Audit 阶段不跑、不调真实 Provider）

1. 启动 ModbusLens（无 key 环境亦可）。
2. Simulator Demo：固定 4 事务（1 Success / 1 Exception 0x02 / 1 CRC Error / 1 Timeout）。
3. Dashboard 讲解：Observed/Completed/Pending/成功率/平均延迟（4/4/0 · 1/1/1/1/0 · 25% · 25ms）。
4. Run Baseline Diagnosis：强调无 API Key、无 LLM 时核心诊断仍工作 → "AI is interpreter, not detector"。
5. Replay demo_v1.mlog：同一证据可重复分析（口径一致性护栏）。
6. Serial Mode controls：说明真实场景 USB-RS485 接 Modbus RTU（不要求现场硬件）。
7. Ask AI：deterministic facts → one-shot natural-language explanation。
8. Ask Agent：question → native tool calling → read-only tools → final answer（展示问题「本批次主要有哪些异常？」）。
9. 权限边界：Agent 不能改串口/写寄存器/重发/控制设备。
10. （可选 20~30s）ISSUE-007 故事：真实多步诊断 → tool budget fail closed → RCA → total 3→6 + planning discipline → 同题真实 re-validation PASS。
- 演示纪律：先离线彩排（无 key + localhost fake）再真机；不为了 Demo 消耗真实 quota（Live 已有历史证据即可引用）。

## 7. Golden Demo Data Consistency Audit

- 事实源：Simulator Demo（makeEntry 顺序 Success 25ms / Exception 0x02 18ms / CRC 17ms / Timeout 1000ms）+ `samples/demo_v1.mlog` golden replay（同口径）。README 与 05_DEMO_GUIDE **均无任何 golden 数字**（两文件已过时，见 §8）——不在数值分歧，而在文件陈旧。
- 任务档案/INTERVIEW_NOTES 中 4/4/0 · 1/1/1/1/0 · 0.25 · 25.0ms 口径一致，未发现分歧。
- 结论：数字一致；陈旧文档缺失数字而非矛盾。Implementation 时 README/DEMO_GUIDE 统一采用上述六组数字。

## 8. Final README Audit（现状 = “严重过时”, P0）

README 当前事实 vs 真实产品不符项：
1. 技术栈写 “Qt 6（Widgets…后续 Qt SerialPort / Qt Network）” —— 真实为 Qt6.11 Quick/QML + SerialPort + Network（ADR001）。
2. “当前状态”仍写 “已完成 M1…下一任务是 T002 CRC16” —— 真实 M2~M6 全完成。
3. 缺少：AI Diagnosis 与 Agent 区别、Agent 三个 read-only Tool、权限边界、无 key 可用功能、limitations、Provider 证据（不夸大 SLA）、测试规模/验证方式、三模式一段式说明过简。
4. 目录结构仍为旧骨架（应反映 src/core|ui|app 实际分层）。
5. 构建命令仍以旧 preset 为主（可保留但需补 deploy 路径）。
- 建议最终结构按用户 15 项清单重构（1~2 分钟理解定位）：What/Solves/Stack+Motive/Modes/Deterministic Core/AI vs Agent/3 read-only Tools/Permission boundary/Tests/Provider evidence(叙述性)/Build/Run/Demo/API key optional/No-key still works/Limitations。

## 9. Final Deployment Audit（主要结论：已达标，重验即可）

- `scripts/deploy_windows.bat`：已生成独立 build/deploy（windeployqt + MinGW runtime）、provenance SHA256 验证、minimal-PATH smoke 多次 PASS、Qt6Network/Qt6SerialPort/QML runtime/TLS（schannel 系统提供）齐全。
- 验收计划（Implementation 尾声执行一次并记录证据）：clean build（0 警告）→ full ctest 23/23 → qml smoke → deploy → minimal-PATH smoke → 用户 Manual UI smoke。不引入新 packaging 系统。

## 10. Screenshot / Portfolio Evidence Plan（建议保留 4~5 张）

- 图1 Simulator Dashboard（四事务 golden + 统计 4/4/0·25%·25ms）——证明核心链路。
- 图2 Baseline Diagnosis（诊断结果/建议检查,无 key 态）——证明确定性诊断、AI 非必需。
- 图3 Agent 问答（问题+长答案,PlainText）——证明 native tool calling 最终产品形态。
- 图4 Replay demo_v1.mlog 加载态——证明口径一致性。
- 图5 Serial Controls 面板——证明真实设备路径 UI。
- 每张截图状态要求：无 token 泄露、无绝对路径、无调试日志、无账户信息。

## 11. Interview Readiness Audit（19 个工程故事 + limitations）

- 故事线（已有档案可讲,本阶段只核对表述）：RTU CRC/帧模型、事务配对与 timeout/CRC/exception 语义、确定性 Simulator、Replay 可重复性、异步 Serial + Qt 事件模型、QML↔Controller 边界、deterministic baseline、AI interpreter-not-detector（真实 Live 证据）、双层 stale guard、immutable Agent snapshot、native tool calling（Gate 0 真实证据）、只读 Tool 严格验证、bounded Runtime、ISSUE-004 布局 RCA（runtime geometry 取证）、ISSUE-006 attribution discipline、ISSUE-007 budget trade-off（真实 Live 闭环）、ISSUE-008 不可复现的边缘证据纪律、ISSUE-009 provider 可见性硬化、**Stabilization 五讲**（不重试关闭 Issue / evidence vs hardening / fail-closed 本已成立 / fake HTTP 集成回归 / 不伪造 RCA）。
- **ISSUE-008/009 只讲已证明内容**：不得说"找到了历史 exact root cause"。
- Real limitations（正面呈现）：FC03-focused v1；无 register startAddress/quantity detail；无 write tools；无 RAG/MCP/multi-agent；API key 桌面模型属 portfolio-scale 设计。

## 12. Recommended Implementation Set（选 8 项；其余审计项降级/暂缓）

P0（面试前必须）：①README 全文重构（§8 清单）；②05_DEMO_GUIDE 重写为当前真实产品（含 §6 十步主线 + golden 数字 + 权限红线）；③V1+V2+V3 深色对比度/三块视觉身份/标题层级（纯样式常量+小结构样式）；④部署/截图/证据一致性——完成后重跑验收链并产出 4~5 张标准截图清单。
P1（值得做）：⑤V5+V6 busy/error 状态与滚动条；⑥V7 长答案排版（lineHeight/段间距）；⑦术语 polish（§4：AI 输出术语自然中文化 + prompt 示例表，不改 authority）。
P2（有余力/进 backlog）：V4 statistics 分层、V8 交易列表可扫读、demo/ 目录脚本与 checklist。
**严格 8 项实现集 = ①②③④⑤⑥⑦+ 最终部署验收链（§9）**。

## 13. Explicit No-Go List

FC06/FC10；startAddress/quantity enrichment；write-register Tool；自动设备控制；新协议/新模式；RAG/MCP/multi-agent；chat history；database；新 Provider；active quota polling；复杂 settings 页；Agent Runtime 重设计；Core 重设计；QML 架构级重写（含任何推翻 ISSUE-004 SplitView 的方案）。

## 14. 状态与 Git 记录

- 本 Phase A 仅 docs。T013=IN PROGRESS；Phase A=FINAL POLISH AUDIT / AWAITING USER IMPLEMENTATION SCOPE REVIEW；M6 保持 DONE；ISSUE-008/009 保持 OPEN+MONITORING/NON-BLOCKING；verified LKGC `3572cf7` 不动。
- 后续：用户批准 8 项实现集后进入 T013 Implementation。