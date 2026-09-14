# 04 — 测试策略

> 状态：v1.1（T001 建立；T001.1 按细粒度任务重排更新任务引用）
> 与 AGENTS.md 强制规则联动：**修改代码后必须构建并执行相关测试**。

## 1. 目标

1. 核心逻辑（协议、事务、统计、诊断）有**可信、可独立运行、可回归**的测试；
2. 任一模式（Simulator/Replay/Serial 适配层）的行为都可脱离硬件验证；
3. 每个任务的 DoD：构建成功 + 相关测试通过 + 档案更新。

## 2. 分层与手段

```text
        ▲  E2E / 演示脚本（05_DEMO_GUIDE 的步骤即验收脚本）
       ╱
      ├── 集成测试：模式端到端（Simulator 全链路；Replay+fixture 日志；Serial+虚拟串口对）
     ╱
    ├── 组件测试：数据源适配器（虚拟时钟确定性、日志解析、串口帧切分器）
   ╱
  ├── 单元测试：协议编解码 / CRC / 事务配对 / 统计 / 诊断规则  ← 数量最大、跑得最快
```

| 层 | 位置（规划） | 手段 | 备注 |
| --- | --- | --- | --- |
| 单元 | `tests/unit` | QtTest 或轻量断言 + CTest | 无 I/O、无 GUI，毫秒级 |
| 组件 | `tests/component` | 同上 + 受控桩 | 数据源适配器 |
| 集成 | `tests/integration` | 同上 + fixture | `tests/data/` 存金样帧、示例日志 |
| E2E | `demo/` 脚本 | 人工 + 录屏清单 | 演示即验收 |

## 3. 关键验证手段（按模块）

### 协议（CRC / 帧 / 编解码）

- **规范向量对拍（KAT）**：官方文档示例 + pymodbus/libmodbus 生成的帧做黄金样例——CRC 规范串 "123456789"→0x4B37（T002）；整帧 `01 03 00 00 00 01`→CRC `84 0A`（T003）。
- **证据分级约定**：KAT（外部权威给答案）是算法正确性的**主要证据**；property/invariant（自洽性）是**补充证据**。T002 的 CRC 测试矩阵（CRC-T01–T06：KAT/边界/敏感性/zero-remainder）与 P0/P1/P2 优先级设计见 [T002 档案](tasks/T002-modbus-crc16.md)。
- **属性测试**：CRC 零余数性质（消息+CRC 再算恒为 0）（T002）。
- **往返（round-trip）测试**：encode→decode 恒等（T003/T004 起）。
- **模糊（fuzz-lite）测试**：随机/截断/翻转字节输入，解码必须"给出结果或安全失败"，绝不崩溃（T003 起，含帧切分输入）。
- 边界：最大/最小长度、边界地址、异常码帧（T003/T004）。

### 事务分析

- 构造合成流量（配对、乱序、广播、超时、重试）验证配对与时延统计（T007）。
- **诊断细节保留（T014）**：ProtocolError 的确定性 reason（`TransactionIssue` 七值 + 稀疏载荷）逐分支断言；RED 以断言失败实证（tx 11 passed / 9 failed，9 条 issue 缺失断言）→ GREEN（tx 20/20）；**production invariant**（`analyzeFunction03Transaction` 生产输出中 `ProtocolError ⇒ issue.has_value()`）与 per-code 载荷约束以专用测试锁定（TX a13~a18）——下游对人工构造/防御性 `ProtocolError + issue=nullopt` 保持防御（omit detail），测试不写无条件 `iff`。统计口径回归 = STAT-B09（issue 不影响 snapshot；防御性输入如实标注）。分层只断言“继承与展示”、不重判：REPLAY-i05 / SERIAL-a07·a11·a15·a16（同漏斗继承）、DIAG-A11（context 保值）、AI-B18/B19（prompt additive + 防御 omit）、AGENT-A11（DTO hasX）、UI-T01（issueText role）。

- **被动回放扩展（T015）**：新增 `passive` 目标（`tests/test_passive_analysis.cpp`）——Function 0x06 单元解码 ×4 + PASSIVE-P01~P15 端到端（FC03 golden 回归锚 / FC06 Success / echo mismatch 独立 issue / generic Exception FC08 / invalid quantity+Exception 双事实 / 不毒死后续记录 / unsupported 显式事实 / unicast Timeout 不变 / broadcast `ExpectedNoResponse` / broadcast 收到响应 → UnexpectedResponseForBroadcast / addr0 非广播能力 → InvalidBroadcastFunction / CRC 与地址失配遗传 / 混合批统计 / 确定性双跑）。RED 实证 = 13 条断言在旧整批失败链路上真实 FAIL（8 passed/13 failed）→ GREEN。统计公式以 STAT-B10 + P14 锁定（completed 含广播；rate=success/(completed−expectedNoResponse)；分母 0 ⇒ nullopt）；Baseline 以 DIAG-A12/A13 锁定（Info 顺序 + broadcast 永不单独 Healthy）；下游以 AI-B20 / AGENT-A12 / UI-T02·T03 锁定；UI-R04 按 Phase A 预声明由“整批 load 失败”替换为 per-record 期望。**Active Serial 安全回归以源码 grep 锚定（无 FC06 encoder / 无 Function16 / 无 write tool）**。Review 前专项 semantic audit（`PASSIVE-P16`）：generic exception matcher 必须满足 `(request.function & 0x80) == 0`——RED=旧逻辑把 request 0x88 / response 0x88 误判为 Exception 0x01（21 passed/1 failed），修复后进入既有 Unsupported 路径；GREEN=passive **22/22**。**Function 0x10 被动语义（T015 Part C）**：requestIssues 有序 collection（ordering≠discard、防 cascade；MULTI-C01/02）、Function16 structural reader + normal response matcher（echo mismatch 复用既有载荷列；BCAST-C01/02 锁定 Expectation/Validity 正交）；RED 12 条断言失败 → GREEN passive **54/54**、ctest 24/24。UI-T04/T05 锁定既有 UI 的 Success+issues 与 Baseline formatter 呈现（presentation gap 审计补锚）。

### 三种模式复用性验证（全局护栏）

- **口径一致性测试**：同一组测试帧流，分别经过 SimulatorSource / ReplaySource 进入分析核心，断言产出统计/报告完全一致。**T009 Part A 已落地第一形态**：T008 Demo Batch（Simulator 现场生成）与 `demo_v1.mlog` Golden Replay（历史文件加载）共享同一 wire 金样，两套统计快照严格相等（4/4/0、1/1/1/1/0、0.25、25.0ms）。这是架构承诺 D1 的可自动验证形式。

### Simulator

- 固定种子 → 流量确定性 → 断言可复现；虚拟时钟精确控制 t3.5（T005）。

### Replay

- `tests/data/demo_v1.mlog` golden fixture（**T009 Part A 落地**，configure_file COPYONLY 进 build 树 + 单一 compile definition 定位）+ 期望命中结论：REPLAY-A01~A08 解析矩阵（header 合法性/hex 金样/行号规则/注释空行 CRLF）、REPLAY-I01~I05 集成矩阵（四结果 Golden 统计与 T008 Demo 同口径 4/4/0·1/1/1/1/0·0.25·25.0ms、确定性双跑全等、坏 request→InvalidRequestWire/InvalidRequestData/InvalidRequestFunction、坏 response→CrcError/ProtocolError 不失败）。
- 口径一致性在此的自动化形式：Replay Golden 与 Simulator Demo 的统计快照严格相等（架构承诺 D1 的可验证形式）。

### Serial

- 虚拟串口对（Windows: com0com；Linux: socat pty）跑真正的 QSerialPort 代码路径（T010）。
- 真机场景保留"人工核对清单"（见 05_DEMO_GUIDE），不计入自动测试。

### GUI

- 冒烟：offscreen 平台下构造窗口/控件（T001 已建立 `smoke`）。
- 有实际业务界面后，仅测"核心→视图刷新"的可测函数（T008 起），UI 细节靠人工演示验收。

## 4. CTest 组织

- 每个测试目标注册为独立 `add_test`，命名 `category.name`；
- 用 `set_tests_properties(... LABELS "unit|component|integration")` 打标签，未来支持 `ctest -L unit` 快速过滤；
- 需要真实硬件或人工步骤的测试以 `manual_` 前缀 + `DISABLED` 或文档化，不进默认 `ctest`；
- 所有可在无显示器环境运行的 QT 测试统一加 `QT_QPA_PLATFORM=offscreen`（CMakeLists 已示范）。
- 已落地（T002–T008A）：ctest 注册 14 个测试——`crc`（CRC-T01~T06）、`frame`（FRAME-T01~T04）、`codec`（RTU-A01~A07）、`f03`（F03-B01~B12，含 V1.1b3 官方金样）、`simulator`（SIM-T01~T07）、`simulator_integration`（SIM-I01 全链路闭环）、`fault`（FAULT-T01~T05 含确定性护栏）、`fault_integration`（FAULT-I01/I02）、`transaction`（TX-A01~A12 六状态与跨帧校验）、`transaction_integration`（TX-I01~I03）、`statistics`（STAT-B01~B08 含四不变量）、`statistics_integration`（STAT-I01 真实链路聚合）、`ui_bridge`（Controller/Model 桥接）、`qml_smoke`（真实 exe 加载 QML 后退出；原 `smoke` 随 QWidget bootstrap 在 T008 Part A 删除）；除 `qml_smoke` 运行真实 app 外均链 `modbuslens_core`/`modbuslens_ui` + QtTest。

## 5. 覆盖率与质量门槛

- 覆盖工具：gcc `--coverage` + gcovr（Windows MinGW 可用），CI 中统计核心层。
- 目标：**核心层（协议/事务/统计/诊断）行覆盖 ≥ 80%**，分支关注错误路径。
- 性能回归：T007（统计落地）之后为"帧吞吐"添加粗粒度基准（不做硬性 SLA，防明显退化）。
- 静态质量：`-Wall -Wextra` 零警告（至少 GCC/Clang）；"警告即错误"在 CI 中作为可选项（后续任务评估）。

## 6. 回归要求

- 提交前必跑 `ctest --preset <active>` 全绿；
- 失败修复必须记录进对应任务/Issue，不允许静默跳过或删测试；
- 历史测试结果作为任务 Verification 的一部分写入任务档案。