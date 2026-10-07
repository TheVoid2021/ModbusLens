# Changelog

本文件只记录有仓库证据支撑的版本事实。当前状态权威 = docs/PROJECT_STATUS.md。

## 2.0.0 — 2026-10-07

首次公开发布：GitHub Release `v2.0.0`（tag `v2.0.0`，annotated，
指向 commit `b18e378…`），发布资产
`ModbusLens-2.0.0-windows-x64.zip`（35,814,735 字节，
SHA-256 `6db918f35b62b3bb7638b7e0660cb74d9137d22aada1ca512513bd0a2654449d`），
已经自动化验证、人工验收（H1–H8）与公开资产独立下载校验。

自历史 `v1.0.0` tag（2026-09-14，早期开发阶段，未发布 GitHub Release）
以来，按仓库证据累计的产品能力：

- **Modbus RTU 确定性核心（C++20，零 Qt）**：CRC-16/帧模型/编解码、
  FC03 语义管线、七状态事务分析 + 正交结构化 Issue 集、统计与基线
  规则诊断（无 Key/无网络完整可用）。
- **FC06 / FC16 支持**：Replay 被动语义分析 + 主动确认式写路径
  （写前快照/校验、确认对话框、确认-派发边界），含真机 probe 与
  热拔检测验收。
- **寄存器读数解码（M11）**：读出视图与 32-bit / word-order 解码。
- **UI 现代化（M9）**：设计系统 + 六工作区桌面应用（事务/总览/通信/
  回放/诊断/设备），QML + C++ 控制器架构。
- **设备档案与手册智能（M12）**：设备档案管理、PDF/DOCX 手册导入
  （pinned PDFium/libzip 离线物化）、AI 提取候选 + 人工审核、
  引用校验式手册问答（app-resolved citations）。
- **AI 解释 + 只读 Agent**：结构化事实之上的自然语言解释；有界只读
  工具调用（3 轮 / 6 次上限、双重陈旧数据守卫）；无写工具。
- **发布工程**：candidate 契约（从零重建 + 逐文件 SHA-256 manifest）、
  fail-fast 打包器、deployment 门、公开资产独立下载验证。
- **可追溯性**：490+ commits、任务档案/Issue/ADR/devlog 全程留档。

已知边界：真实工业硬件泛化验证未做（NOT VERIFIED）；包未签名；
无 installer；无 Modbus TCP。

## 1.0.0 — 2026-09-14（历史 tag）

仓库于早期开发阶段（M9 UI 现代化之前）打下的历史 annotated tag
（`2cee626…`，指向 T015 收口 commit），**未发布** GitHub Release。
仅作历史标记保留。
