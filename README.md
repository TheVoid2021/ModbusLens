# ModbusLens — 工业通信智能诊断平台

一个用于工业现场 Modbus 通信**监听、分析与诊断**的桌面软件，也是一份完整的、开发过程全程可追溯的秋招软件工程项目。

## 三种运行模式（共享同一套分析核心）

| 模式 | 说明 | 适用场景 |
| --- | --- | --- |
| **Simulator Mode** | 内置虚拟从站，无需任何硬件即可产生 Modbus 流量 | 开发调试、初学演示、无设备环境 |
| **Replay Mode** | 读取历史通信日志，离线回放并分析 | 事后排障、故障复盘 |
| **Serial Mode** | 通过真实串口连接 Modbus RTU 设备在线采集 | 现场诊断 |

三种模式共用同一套 **协议解析 → 事务分析 → 统计 → 诊断** 核心逻辑，保证结果口径一致、逻辑只实现一次。

## 技术栈

- C++20
- Qt 6（Widgets；后续按需 QtCharts / Qt SerialPort / Qt Network）
- CMake（Presets）+ CTest
- 后续任务引入 HTTP 服务与只读 LLM Agent（可选增强，核心功能不依赖）

## 目录结构

```text
ModbusLens/
├── src/          # 应用源码（后续按 app/core/io/ui 分层）
├── tests/        # 单元与集成测试（CTest 驱动）
├── demo/         # 演示素材：脚本、示例日志、演示清单
├── docs/         # 全部项目文档（唯一事实来源）
│   ├── tasks/    # 每个任务的完整过程档案
│   ├── issues/   # 技术问题定位与解决记录
│   ├── adr/      # 架构决策记录
│   └── devlog/   # 开发日志
├── CMakeLists.txt
└── CMakePresets.json
```

## 快速开始

环境要求与各平台安装方法见 [docs/ENVIRONMENT.md](docs/ENVIRONMENT.md)。

```bash
# 本机（Windows + Qt 6.11.1 MinGW，使用机器专属 preset）
cmake --preset debug-local
cmake --build --preset debug-local
ctest --preset debug-local
./build/debug/modbuslens.exe

# 其他平台（Qt 位于系统默认位置）
cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

> 若 Qt 不在系统默认位置，为 configure 追加 `-DCMAKE_PREFIX_PATH=<Qt6 安装前缀>`。

## 文档导航

- 📋 项目状态（**先看这个**）：[docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md)
- 🗺 任务清单与里程碑：[docs/BACKLOG.md](docs/BACKLOG.md)
- 🏛 项目章程：[docs/00_PROJECT_CHARTER.md](docs/00_PROJECT_CHARTER.md)
- 📐 需求基线：[docs/01_REQUIREMENTS.md](docs/01_REQUIREMENTS.md)
- 🧱 架构设计：[docs/02_ARCHITECTURE.md](docs/02_ARCHITECTURE.md)
- 📖 Modbus 知识库：[docs/03_MODBUS_LEARNING.md](docs/03_MODBUS_LEARNING.md)
- 🧪 测试策略：[docs/04_TEST_STRATEGY.md](docs/04_TEST_STRATEGY.md)
- 🎬 演示指南：[docs/05_DEMO_GUIDIDE.md](docs/05_DEMO_GUIDIDE.md)
- 💬 面试素材：[docs/INTERVIEW_NOTES.md](docs/INTERVIEW_NOTES.md)
- 🔧 开发规约（AI 代理必读）：[AGENTS.md](AGENTS.md)

## 当前状态

项目当前处于 **M1 里程碑（工程引导与文档体系）** 收尾阶段，详情见 [docs/PROJECT_STATUS.md](docs/PROJECT_STATUS.md)。

## 开发原则速览

1. 全程可追溯：每个任务留下"为什么 → 怎么做 → 改了哪些文件 → 遇到什么问题 → 如何解决 → 如何验证"的完整档案。
2. 平台可移植：所有项目状态存储在 Git 仓库的 Markdown 文件中，随时可移交给任何 AI Coding 平台。
3. 核心不依赖 LLM：诊断分析是确定性逻辑；Agent 仅作只读增强。