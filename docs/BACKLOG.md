# BACKLOG — 任务清单与里程碑

> 规则：每个任务完成后更新（状态、依赖、优先级）；任务档案在 `docs/tasks/`。
> 状态取值：`Done` / `In Progress` / `Ready` / `Backlog`
> 优先级：`P0` 必须现在做 · `P1` 尽快 · `P2` 有余力再做

## 里程碑总览

| ID | 里程碑 | 任务 | 状态 |
| --- | --- | --- | --- |
| M1 | 工程引导与文档体系 | T001 | ✅ 完成 |
| M2 | 协议核心 | T002, T003 | ⬜ |
| M3 | 三种数据源模式 | T004, T005, T006 | ⬜ |
| M4 | 事务分析与统计 | T007 | ⬜ |
| M5 | 诊断规则引擎 | T008 | ⬜ |
| M6 | UI 整合与可视化 | T009 | ⬜ |
| M7 | 只读 LLM Agent（HTTP） | T010 | ⬜ |
| M8 | 打磨与发布 | T011 | ⬜ |

## 任务表

| ID | 标题 | 里程碑 | 优先级 | 状态 | 依赖 | 说明 / 关键产出 |
| --- | --- | --- | --- | --- | --- | --- |
| T001 | 项目引导：骨架 + 文档体系 + 最小 Qt6 应用 | M1 | P0 | ✅ Done | — | 目录骨架、AGENTS 规约、全套文档、CMake Presets、offscreen 冒烟测试 |
| T002 | Modbus 协议核心 v1：CRC16 + RTU 帧编解码 | M2 | P0 | Ready（**建议下一任务**） | T001 | `src/core/` 数据模型与纯函数解码；按位/查表 CRC；金样对拍 + round-trip + fuzz-lite；`tests/unit/` 落地 |
| T003 | MLog 日志格式 v1 与解析器 | M2 | P1 | Backlog | T002 | 日志格式规范（ADR）、写入/解析、fixture 日志样例入 `tests/data/` |
| T004 | Simulator Mode：虚拟从站 + 主站轮询闭环 | M3 | P0 | Backlog | T002 | `IFrameSource` 首个实现；虚拟时钟确定性（ADR）；异常注入开关 |
| T005 | Serial Mode：QtSerialPort 采集 + t3.5 帧切分 | M3 | P0 | Backlog | T002 | 串口参数配置；环形缓冲；com0com/socat 虚拟串口对集成测试 |
| T006 | Replay Mode：回放与时间轴控制 | M3 | P0 | Backlog | T002, T003 | 回放速度/暂停/跳转；**口径一致性测试**（Simulator 与 Replay 同流同结论） |
| T007 | 事务分析与统计核心 | M4 | P0 | Backlog | T002 | 请求-响应配对、时延、超时、广播；统计快照；合成流量单测 |
| T008 | 诊断规则引擎与报告 | M5 | P0 | Backlog | T007 | 确定性规则集；Markdown/JSON 报告导出；规则命中单测 |
| T009 | UI 整合：模式切换 + 帧/事务/统计/报告视图 | M6 | P0 | Backlog | T004–T008 | UI 薄壳；核心↔视图解耦；offscreen 冒烟扩展 |
| T010 | HTTP 服务 + 只读 LLM Agent | M7 | P2 | Backlog | T007, T008 | 只读 Service（无写 API）；Agent 自然语言诊断说明；配置化开关 |
| T011 | 打包、演示脚本与文档终稿 | M8 | P1 | Backlog | T009(+T010) | 安装包/便携包；demo/ 素材齐备；面试录屏与文档终审 |

## 建议路线（默认顺序）

`T001 ✅ → T002 CRC/帧解析 → T003 日志格式 → T004 Simulator → T005 Serial → T006 Replay → T007 事务统计 → T008 诊断 → T009 UI → T010 Agent(选做) → T011 收尾`

## 变更记录

| 日期 | 事件 |
| --- | --- |
| 2026-09-05 | T001 建立本文件与任务表（T002–T011 草案） |
| 2026-09-05 | T001 完成（提交 `aa337f6`）；M1 关闭；T002 保持 Ready |