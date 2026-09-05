# T003 — Modbus RTU Frame Model

> 状态：**IN PROGRESS → DONE**（见 §Result）｜前置：T002 CRC16 ✅（LKGC = T002 已验证代码提交）
> 协议依据：MODBUS over Serial Line Protocol and Implementation Guide **V1.02**（帧结构）。
> 任务边界：**仅**用纯 C++20 建立 RTU Frame 的内存数据模型，供 Simulator/Replay/Serial/Function Codec 共享同一消息表示。**禁止**（本次全部未做）：encode/decode RTU bytes、CRC wire serialization、CRC validation、Function 0x03 字段解析、Holding Register 值解析、Request/Response matcher、Timeout、Simulator、Replay、QSerialPort、QML 页面、Agent、fuzz、benchmark。

## Goal

让项目拥有一份唯一的、语义化的 Modbus RTU 消息内存表示（value object），使后续所有模式与编解码任务不再各自发明"一帧是什么"。

## Background

- T002 交付了 CRC 计算（`modbuslens_core`），但"一帧 Modbus RTU 消息在内存里长什么样"仍无定义——若不先定模型，T004 codec 与三种数据源会各自造轮子。
- 架构 D1/D3 要求核心与 I/O、GUI 解耦；模型必须留在 `modbuslens_core`（无 Qt）。

## What Is A Modbus RTU Frame（本任务依据的协议知识边界）

Wire frame（线路字节序列）：

```text
Address(1B) + Function Code(1B) + Data(0~252B) + CRC(2B)   → 最大 256 B
```

- Address = 1 byte；Function Code = 1 byte；Data = 0~252 bytes；CRC = 2 bytes；**最大 RTU frame = 256 bytes**。
- RTU 串口层另有 t1.5/t3.5 帧间静默时序规则——它们属于 **Serial Transport / Framing 接收逻辑（T010）**，**T003 不实现任何时间判断**。

## Wire Frame vs In-Memory Model（核心设计决策）

线路上的完整报文 `01 03 00 00 00 01 84 0A` 分解为：

```text
Address: 01 | Function: 03 | Data: 00 00 00 01 | CRC: 84 0A
```

但本项目的内存模型**只保存** `address / functionCode / data`，**不把 CRC 存为长期可修改字段**：

- CRC 是 `address + functionCode + data` 的**派生结果**；对象同时保存 data 与 crc 时，修改 data 后极易产生 **stale CRC**（旧校验值配新数据）这类静默 bug；
- T004 Codec 在 **encode 时**根据当前数据重新计算 CRC；**decode 时**先验证 wire CRC，通过后才产生 `ModbusRtuFrame`——CRC 校验失败的数据不进入语义模型（走错误统计路径）；
- 若未来诊断需要"CRC 错误的原始帧"，可单独保存 **Raw Frame Bytes**（诊断视图），**不污染语义 Frame Model**。

## Technical Decisions

| 决策 | 内容 | 理由 |
| --- | --- | --- |
| 形态 | 简单 aggregate struct + C++20 defaulted `operator==` | value object 语义直白；无构造器/builder/factory/继承体系（防过度设计） |
| CRC 不入库 | 内存模型零 CRC 字段 | 见上节：派生数据入库 = stale CRC 隐患 |
| 载体 | 加入既有 `modbuslens_core`（header-only，无新 cpp/无新 library） | 模型是纯数据；不为它建新协议 library |
| 容器 | `std::vector<std::uint8_t>` | Core 禁 Qt 类型（QByteArray/QString/QObject/QVariant/QML）；vector 语义清晰、通用 |
| 辅助函数 | `isExceptionResponse(frame)`：`(functionCode & 0x80) != 0` | 属于 Frame 的通用协议属性（0x03↔0x83），不依赖任何具体功能码的数据格式；异常码含义解析留给 T004/analysis |
| t1.5/t3.5 | 不实现 | 属于 Serial 传输层（T010） |
| fuzz | **从 T003 范围移除**（BACKLOG 同步修订） | 用户明确指示；fuzz 待 wire codec 存在后才有意义 |
| UI（ADR001） | 最终 UI = Qt Quick/QML；QMainWindow 仅为 scaffold，T003 不动 UI | 用户在 T003 开始前确认；见 [ADR001](../adr/ADR001-use-qt-quick-qml.md) |

## Test Design

| Test ID | 场景 | 断言 | 说明 |
| --- | --- | --- | --- |
| FRAME-T01 | 表示请求 `01 03 00 00 00 01`（wire CRC `84 0A`） | address=0x01, functionCode=0x03, data={00 00 00 01} | **不**编码/解析 84 0A，只证明模型能表达该请求 |
| FRAME-T02 | 表示 0x03 响应 `01 03 02 00 64 ...` | address=0x01, functionCode=0x03, data={02,00,64} | 只表示 Byte Count + Register Data；register value=100 的解析属 T004 |
| FRAME-T03 | 异常响应 `01 83 02` 与正常 0x03 帧 | `isExceptionResponse` 分别为 true / false | 只判标志位；异常码 0x02 的含义不在 T003 解析 |
| FRAME-T04 | 两个相同 frame；改一个 data byte 后 | `==` 然后 `!=` | 证明模型是简单 value object，不引入复制逻辑 |

## RED Evidence（真实发生）

先建 `tests/test_modbus_rtu_frame.cpp` + CMake target `modbuslens_frame_tests`，**头文件尚未创建**即构建：

```text
$ cmake --preset debug-local            # configure 通过（target 已声明）
$ cmake --build --preset debug-local
FAILED: CMakeFiles/modbuslens_frame_tests.dir/tests/test_modbus_rtu_frame.cpp.obj
E:/desktop/ModbusLens/tests/test_modbus_rtu_frame.cpp:6:10: fatal error:
    core/protocol/ModbusRtuFrame.h: No such file or directory
```

RED 表现为**编译失败**（模型尚不存在，无法伪造运行时失败也不必人造 bug）——按 T003 指示如实记录。`smoke`/`crc` 两个既有 target 不受影响。

## Implementation

- `src/core/protocol/ModbusRtuFrame.h`（header-only）：aggregate struct（`address`/`functionCode`/`data`）+ defaulted `operator==` + inline `isExceptionResponse`；注释写明"为什么无 CRC 字段"与职责边界（T004 codec）。
- 未创建 cpp 文件、未加 constructor/builder/factory/继承。
- CMake：仅新增测试 target `modbuslens_frame_tests`（链 `modbuslens_core` + `Qt6::Test`）；`modbuslens_core` 不变、零 Qt 依赖保持。

## Files Changed

- 新增：`src/core/protocol/ModbusRtuFrame.h`、`tests/test_modbus_rtu_frame.cpp`、`docs/adr/ADR001-use-qt-quick-qml.md`、`docs/tasks/T003-modbus-rtu-frame-model.md`（本文件）、`docs/devlog/2026-09-05-T003.md`
- 修改：`CMakeLists.txt`（frame test target）、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`（T003 Done、fuzz 移除、T004 Ready）、`docs/02_ARCHITECTURE.md`（ADR001 引用 + 目录树）、`docs/INTERVIEW_NOTES.md`
- 未改动：`src/core/protocol/ModbusCrc.{h,cpp}`、`src/main.cpp`、`tests/test_smoke.cpp`、`tests/test_modbus_crc.cpp`、presets

## Problems Encountered

1. RED 阶段按预期表现为编译失败（头文件缺失）——这是本任务 TDD 的自然形态，非问题；无其他实现问题。**No significant implementation issue encountered.**
2. 流程性事项：BACKLOG 原 T003 行含"fuzz-lite"，与用户指示冲突 → 已从 T003 范围删除并在 BACKLOG 变更记录留痕（见 Solutions）。

## Solutions

1. RED 即记录、实现即 GREEN（见 Verification）。
2. T003 行说明改写为实际交付范围；wire 编解码/CRC 校验集成随 T004 codec 承接，fuzz 待 codec 存在后再评估立项。

## Verification

```text
$ cmake --preset debug-local                          # configure PASS
$ cmake --build --preset debug-local                  # build PASS（增量）
$ ./build/debug/modbuslens_frame_tests.exe -o build/green_frame.txt,txt
exit_code=0
PASS: t01_readHoldingRegistersRequest / t02_readHoldingRegistersResponse /
      t03_exceptionResponseFlag / t04_equalityAndValueSemantics
Totals: 6 passed, 0 failed, 0 skipped (7ms)
$ ctest --preset debug-local
3/3: smoke Passed | crc Passed | frame Passed → 100% tests passed
$ cmake --build --preset debug-local --clean-first    # clean 全量重建
警告/错误行数 grep = 0（零警告）；ctest 再次 3/3 通过
```

既有 CRC tests 未破坏（crc Passed）。

## Result

✅ **T003 DONE**：`ModbusRtuFrame`（address/functionCode/data + value 语义）+ `isExceptionResponse` 进入 `modbuslens_core`；4 个 FRAME 测试全绿；ctest 3/3；零 Qt 依赖保持；BACKLOG 边界全部遵守（无编解码/无 CRC 序列化/无 fuzz/无 t1.5-t3.5/无 UI 改动）；ADR001 归档。

## Knowledge Learned

- **派生数据不入模型**：CRC 这类可从字段重算的值，存进 value object 只会制造 stale-data 隐患；"encode 时算、decode 时验"是更稳的职责切分。
- **语义模型与线表示分离**：`ModbusRtuFrame` 是"协议含义"，wire bytes 是"传输事实"；两者经 codec 单向转换，诊断所需的 raw bytes 将来单独挂附，不混进语义层。
- **C++20 defaulted comparison**：`operator==(...) const = default` 让 value object 一行获得全字段比较，避免手写遗漏字段。
- **header-only 的适用边界**：纯数据 + 一行 inline 判断不需要 cpp；等出现需要隐藏的实现细节再拆。
- 架构决策（QML）应尽早 ADR 归档，即使实施在后面的任务——否则后续任务会在错误假设上设计接口。

## Potential Interview Questions

1. **一条 Modbus RTU frame 有哪些部分？** Address(1B)+Function(1B)+Data(0~252B)+CRC(2B)，最大 256B；另有帧间 t3.5/帧内 t1.5 静默规则（属串口层）。
2. **为什么 Data 长度不固定？** 长度由功能码语义决定（读几个寄存器、写多少数据各不相同），协议用"长度上限 + 功能码自带长度信息"表达，因此帧边界要靠功能码规则与 t3.5 静默共同判定——这也是 T010 帧切分的难点。
3. **为什么内存 Frame 不保存 CRC？** CRC 是三字段的派生值；入库会产生 stale CRC（改 data 忘改 crc）静默 bug。encode 时现算、decode 时先验后建；诊断要 raw bytes 单独存。
4. **为什么 Core 用 `std::vector<uint8_t>` 而不是 QByteArray？** Core 禁 Qt 类型（可测试性/可移植性/避免隐式共享语义进入协议层）；vector 是标准、直白、跨边界的字节容器。
5. **0x03 和 0x83 有什么关系？** 0x83 = 0x03 | 0x80，是 0x03 请求的异常响应标志位；Frame 层只判标志位，异常码含义（如 0x02 Illegal Data Address）归 codec/analysis。
6. **为什么 T003 不解析 Holding Register？** 寄存器值解释（字节序/量程/缩放）依赖功能码语义，属于 codec（T004）与业务层；Frame 模型必须保持"对任何功能码通用"。
7. **Wire representation 和 domain model 有什么区别？** wire 是带 CRC 的完整字节序列（传输事实）；domain model 是校验通过后的语义对象（协议含义）。转换单向且不可逆地丢弃 CRC（可重算）。
8. **为什么不在 QML 中表示/解析 Modbus Frame？** QML 是 UI 层，解析协议会把它变成业务层（不可测、易错、难复用）；QML 只消费 C++ 适配层暴露的 Model（见 ADR001 职责边界）。

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| T001 | `aa337f6` | 项目引导 |
| T001.1 | `ffd2b94`（+回填 `bfa73c6`） | 文档清理 |
| T002 | `e8ef30c`（+回填 `ae4e708`） | CRC16（LKGC 至 T002） |
| T003 代码提交（**新 LKGC**） | `PENDING-BACKFILL` | `T003: Modbus RTU frame model with value semantics` |
| 回填提交（docs-only，HEAD） | 见 `git log` | 回填哈希 |

> LKGC 推进：本任务产生新业务代码并经 configure/build/ctest（3/3）验证；LKGC 从 `e8ef30c` 推进至 T003 代码提交，由 docs-only 回填提交写入 PROJECT_STATUS。ADR001 随本任务提交，其决策来源为用户在 T003 开始前的确认（见 ADR 头部）。