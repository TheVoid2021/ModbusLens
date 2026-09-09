# Devlog 2026-09-09 — UI Localization Pass（独立 pass）

## 今日工作

- **UI Localization Pass 完成**（用户 Manual Localization Smoke = PASS 后归档；code/config commit `9e79558`，= 新 LKGC）：
  - **QML（`src/ui/qml/Main.qml`）**：42 处静态文案 + 状态卡/串口控制/诊断面板/最近通信记录全部简体中文；漏网的 `Reading...` 补译为"读取中..."；专业实体（CRC/RS485/FC03/8N1/0x02/COM/ModelScope/Qwen/.mlog/ms/AI）按政策保留英文，无冗余双语。
  - **`TransactionListModel.cpp`**：statusText 六状态中文（进行中/成功/异常/CRC 错误/超时/协议错误）。
  - **`AnalysisController.cpp/.h`**：模式/来源标签（**含 .h 成员默认初始值"模拟器模式/确定性演示"——第一次 ctest 失败即漏此点，已补齐**）；基线格式 `诊断结果：`/`建议检查：` + 12 条建议动作中文；AI/串口/回放全部错误文案中文。
  - **`SerialPortAdapter.cpp` / `ModelScopeDiagnosisClient.cpp`**：串口与 AI 客户端错误消息中文。
  - **`DiagnosisPromptBuilder.cpp`**：system prompt 尾段改为输出语言约束（concise Simplified Chinese + 术语保留英文 + Do not use Markdown / plain text only + 中文章节名）；原有英文 authority 规则一字未改；user prompt 结构化事实通道保持英文。
  - **结构性修正**：replay 错误短语原为 `const char*` + `QLatin1String` —— 中文经 Latin-1 逐字节映射必然乱码，改为 `QString` + `QStringLiteral`（UTF-16 编译期语义），文件内私有函数、无 API/枚举改动。
  - **测试**：`tests/test_ui_bridge.cpp`（49 函数）与 `tests/test_ai_client.cpp` 的 presentation 断言随新文案同步更新（语义保持：如 `!contains("Healthy")` → `!contains("全部")`）；Core 语义与 Core 测试零改动。
- **验证**：clean 全量重建 126 targets **零警告**；ctest **20/20**（含 qml_smoke）；`scripts/deploy_windows.bat`；minimal-PATH smoke PASS（deploy 刷新前后各一次）。
- **用户 Manual Localization Smoke = PASS（9 项人工确认）**：三模式文案/专业实体保留/各面板文案自然/Baseline 中文/Replay 文件名不翻译/真实 ModelScope 中文输出/PlainText 渲染/无截断无回归。
- **non-blocking polish 记录**：深色主题下部分 Label / Diagnosis 正文对比度偏低 → 记入 BACKLOG，归 T013 统一处理（本次不实现、不扩 scope）。

## 关联文档

- 政策：新增 [docs/06_UI_LANGUAGE_POLICY.md](../06_UI_LANGUAGE_POLICY.md)（UI 语言政策单点）
- 状态：[PROJECT_STATUS](../PROJECT_STATUS.md)（LKGC 推进至 `9e79558`；ctest 20/20）
- 访谈：[INTERVIEW_NOTES](../INTERVIEW_NOTES.md)（本地化边界/QLatin1String 乱码/LLM 输出语言治理 3 问）

## 提交

- code/config：`9e79558` `chore(ui): localize user-facing text to Simplified Chinese`（= **新 LKGC**，经 Manual Smoke 验证）
- docs-only：本 devlog 同批归档提交（**不**推进 LKGC）