# ADR001: 最终 UI 采用 Qt Quick + QML

- 状态：**Accepted**
- 日期：2026-09-05
- 决策来源：**用户在 T003 开始前明确确认的 UI 架构决策**；本 ADR 于 T003 期间补录归档（AGENTS 纪律 7）。
- 关联任务：T001（scaffold 来源）、T003（本 ADR 补录）、T008（切换实施）

## Context

T001 为快速验证 Qt/CMake 工具链，使用了最小 `QMainWindow`（Qt Widgets）scaffold——它只证明"可构建、可测试、可演示"，不代表 UI 技术选型。开发者本身更熟悉 QML，因此最终产品没有必要重新投入时间学习 QWidget UI 体系。

## Decision

**最终 UI 使用 Qt 6 + Qt Quick + QML + Qt Quick Controls；不使用 QWidget 作为最终 UI 技术方案。**

职责边界：

| 层 | 技术 | 职责 |
| --- | --- | --- |
| UI | QML / Qt Quick Controls | 页面布局、交互、动画、状态展示、Dashboard |
| C++ 适配层 | Controller / Model（`QAbstractListModel`）/ `QSerialPort` | 把核心数据暴露给 QML、承载设备 I/O 与业务编排 |
| Pure C++ Core（`modbuslens_core`） | 纯 C++20 | CRC、Frame、Codec、Transaction、Analysis |

**Modbus Core 不得依赖 QML**（也不依赖 QtGUI）；core 与 QML 之间只经 C++ 适配层（Controller/Model）单向桥接。

当前 `src/main.cpp` 的 QMainWindow 只是 bootstrap scaffold：**T003 不删除、不迁移、不重构该 UI**；真正切换到 QML 放在 **T008 Qt Analysis UI**（届时 smoke 测试随之调整为 QML 加载冒烟）。

## Consequences

优点：

- 利用开发者已有的 QML 能力，两周量级项目显著降低 UI 学习/试错成本；
- QML 的声明式绑定/动画/图表生态更适合现代诊断 Dashboard；
- C++ Core 独立于 UI 技术演进，可独立测试（与 ADR 无关地成立，QML 边界进一步强化它）。

代价：

- 后续需要设计清晰的 C++/QML 数据边界（哪些对象暴露、如何避免跨线程绑定陷阱）；
- Transaction/统计等数据需要通过 QObject/`QAbstractListModel` 暴露，多一层适配代码；
- Widgets scaffold 与最终 QML 并存至 T008，期间 UI 相关演示以核心能力为主。