# PROJECT_STATUS — 项目状态单一事实源

> 规则：本文件在每个任务**完成时**更新（AGENTS.md 工作纪律 5）。任何协作者以此文件为真相，其次才是聊天上下文。
> Last Known Good Commit = 最近一次**构建+测试双通过**的主线提交；其后仅文档回填的提交以"补充提交"形式注明，不改动 LKGC。

## 状态面板

| 项 | 值 |
| --- | --- |
| 当前版本 | **0.1.0**（2026-09-05，T001 建立；T001.1 未改代码，版本不变） |
| 当前 Milestone | M1 ✅ 已完成；**M2 协议核心进行中（T002：Phase A/B 完成，Phase C 未开始）** |
| Last Known Good Commit | `aa337f6`（最近一次构建+测试双通过；T001.1 与 T002-PhaseA 均为 docs-only 提交，LKGC 不推进，哈希见 git log） |
| Build 状态 | ✅ **通过** — Debug/MinGW 13.1.0/Qt 6.11.1/CMake 3.30.5，零警告 |
| Test 状态 | ✅ **1/1 通过**（`smoke`，内含 2 个 QtTest 用例） |
| 已完成任务 | T001 · T001.1 |
| 当前任务（Current Task） | **T002 Modbus CRC16**（IN PROGRESS，未完成） |
| 当前阶段（Current Phase） | **Phase B — Test Design**（本阶段 docs-only 交付已提交；Phase A ✅ / Phase B ✅ / Phase C ⬜） |
| 下一步动作（Next Action） | 启动 **T002 Phase C — Test First + Implementation** |
| T002 之后的下一任务（Next Task After T002） | **T003 Modbus RTU Frame Model**（必须在 T002 完成并验证后启动） |
| Known Issues | 见 §4 |
| 开发环境 | 见 §5 |
| 标准 Build/Test 命令 | 见 §6 |

## 1. 已完成任务

| 任务 | 标题 | 结果摘要 | 档案 |
| --- | --- | --- | --- |
| T001 | 项目引导：骨架、文档体系、最小 Qt6 应用 | 骨架可构建、smoke 测试通过、文档体系建立 | [T001](tasks/T001-project-bootstrap.md) |
| T001.1 | Bootstrap Documentation Cleanup | 文档更名、preset 示例模板、环境文档重写、BACKLOG 细粒度拆分；构建与测试复验通过 | [T001.1](tasks/T001.1-bootstrap-cleanup.md) |

## 2. 当前任务

- **T002 Modbus CRC16 — IN PROGRESS**：Phase A（学习与知识留痕）✅、Phase B（测试设计：矩阵/优先级/接口定案/Phase C 计划）✅（均已 docs-only 提交）；**Phase C（Test First + Implementation）⬜ 未开始**。
- 请勿把 T003 视为当前下一步：当前下一步动作是启动 T002 Phase C；T003 仅在 T002 完成并验证后按 BACKLOG 顺序启动。

## 3. T002 之后的下一任务

- **T003 — Modbus RTU Frame Model**（详见 [BACKLOG](BACKLOG.md)）：帧数据模型（地址/功能码/数据/CRC 布局）、构造/序列化/解析 + CRC 校验集成、金样帧对拍与 fuzz-lite。注意 T003 包含"CRC 数值 → 帧尾两字节（低字节在前）"的序列化职责（已在 T002 测试设计中预留知识验收）。

## 4. Known Issues（当前已知问题）

| # | 问题 | 影响 | 状态/应对 |
| --- | --- | --- | --- |
| K1 | 本机 Qt 在 AutoMoc 阶段出现 qtlicd 证书服务不可用的构建期警告 | 仅为构建日志噪音，产物正常 | **临时环境处理**：仅在本机（gitignored 的 CMakeUserPresets.json）注入 `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`。项目代码与提交文件**不依赖**该变量（T001.1 已澄清措辞，见 [ENVIRONMENT](ENVIRONMENT.md) §6） |
| K2 | 系统 PATH 中存在 Anaconda 的 Qt5 qmake 与 MinGW g++ 8.1.0（过旧） | 若直接裸用会产生 Qt/编译器 ABI 不匹配 | 规避：统一通过 `*-local` preset 注入 Qt 自带工具链；见 [ENVIRONMENT](ENVIRONMENT.md) |
| K3 | `Could NOT find WrapVulkanHeaders`（configure 提示） | 无（Qt Widgets 不依赖；仅影响未来 QtQuick/RHI 功能） | 记录观察，不处理 |

## 5. 开发环境

| 项 | 值 |
| --- | --- |
| OS | Windows 11 (10.0.26200)，Shell: Git Bash |
| CMake | 3.30.5（Qt 自带 `D:/QT/Tools/CMake_64`） |
| 生成器/构建器 | Ninja 1.12.1（Qt 自带） |
| 编译器 | MinGW-W64 g++ 13.1.0（`D:/QT/Tools/mingw1310_64`，posix-seh） |
| Qt | 6.11.1（`D:/QT/6.11.1/mingw_64`，Desktop MinGW kit） |
| Git | 2.55.0（identity: Zhiwei Fu <1627017595@qq.com>） |

> 各平台安装方法与常见坑：[ENVIRONMENT.md](ENVIRONMENT.md)

## 6. 标准 Build/Test 命令

```bash
# —— 本机（Windows，Qt 6.11.1 MinGW；工具链由 CMakeUserPresets.json 注入，不提交）——
cmake --preset debug-local          # configure
cmake --build --preset debug-local  # build
ctest --preset debug-local          # test

# —— 其他平台（Qt 在系统默认位置）——
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

# Qt 不在默认位置时：
cmake --preset debug -DCMAKE_PREFIX_PATH=<Qt6前缀>
```

## 7. 变更记录（本文件）

| 日期 | 事件 |
| --- | --- |
| 2026-09-05 | T001 建立本文件；记录初始环境、构建与测试结果 |
| 2026-09-05 | T001 收尾：回填 LKGC=`aa337f6`，M1 关闭 |
| 2026-09-05 | T001.1 完成（docs-only；提交哈希与信息见 git log / T001.1 档案）；LKGC 依约定不变 |
| 2026-09-05 | T002 启动：Phase A Learning Checkpoint（docs-only）；T002 标记 IN PROGRESS，**未标完成** |
| 2026-09-05 | T002 Phase B（Test Design，docs-only）完成：测试矩阵 CRC-T01–T06 + 优先级 + 接口定案 + Phase C 13 步计划；状态改为四段式表达（Current Task/Phase/Next Action/Next Task After T002） |