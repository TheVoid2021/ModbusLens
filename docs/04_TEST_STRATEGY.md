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

### 三种模式复用性验证（全局护栏）

- **口径一致性测试**：同一组测试帧流，分别经过 SimulatorSource / ReplaySource 进入分析核心，断言产出统计/报告完全一致（T009 落地，与 Replay 同批）。这是架构承诺 D1 的可自动验证形式。

### Simulator

- 固定种子 → 流量确定性 → 断言可复现；虚拟时钟精确控制 t3.5（T005）。

### Replay

- `tests/data/sample-*.mlog` 夹具 + 期望命中结论（T009；MLog 格式同一任务定义）。

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