# ISSUE-003: 本机 Qt 6.11.1 未安装 QtSerialPort 组件 —— T010 Implementation 阻塞

> 状态：**OPEN**（2026-09-07 发现；修复方案待用户决策）

## Symptom（现象）

T010 Part A Learning / Test Design 阶段执行"本机 QtSerialPort 能力实证"（只读取证），核验 `D:\QT\6.11.1\mingw_64`：

| 核验项 | 结果 |
| --- | --- |
| QtSerialPort headers（`include/QtSerialPort/`） | **不存在** |
| Qt6SerialPort CMake package（`lib/cmake/Qt6SerialPort*`） | **不存在** |
| Qt6::SerialPort CMake target | 因 package 缺失，**不可用** |
| Qt6SerialPort.dll（`bin/`） | **不存在** |
| QSerialPort / QSerialPortInfo 类 | headers 缺失，**不可用** |
| 残留物 | 仅有 `doc/config/exampleurl-qtserialport.qdocconf` 与 `translations/qtserialport_*.qm`（文档/翻译的安装残留） |

`lib/cmake/` 中亦无 Qt6SerialBus 等相近组件。`D:\QT` 为唯一 Qt 安装（6.11.1，无多版本）。

## Impact（影响）

- T010（Serial Mode）Part A Implementation 硬依赖 QSerialPort / QSerialPortInfo：open/config 串口、readyRead 异步读、write 请求，均无法进行。
- 无 QtSerialPort 时 configure 阶段 `find_package(Qt6 COMPONENTS SerialPort)` 即失败——Serial Adapter 与序列化集成测试全部阻塞。
- 其余工作（Pure C++ SerialTransactionSession、SERIAL-A01~A13 状态机测试）不依赖 Qt，理论上可先行，但任务的"Clean build + full ctest"验收要求 Serial Qt adapter 也过 → 整体仍阻塞。

## Repro（复现步骤）

```bash
ls /d/QT/6.11.1/mingw_64/lib/cmake | grep -i serial   # 无输出
ls /d/QT/6.11.1/mingw_64/include/QtSerialPort         # No such file or directory
```

## 定位过程（Root Cause）

Qt 6 安装时未勾选 **Additional Libraries → Qt Serial Port** 组件（属于官方 addons，默认不带）。初始工具链安装清单（见 ENVIRONMENT.md §5 环境搭建）只含 GUI/QML 开发所需组件。T001~T009 均未用到串口，故至今未暴露。

## 解决方案（选项，待用户决策）

- **选项 A（推荐）**：用现有安装的官方维护工具补充该组件——
  `D:\QT\MaintenanceTool.exe`（已实证存在）→ Add or remove components → Qt 6.11.1 → Additional Libraries → 勾选 **Qt Serial Port**。属"给现有 kit 补官方组件"，不安装新 Qt 版本、不下载第三方串口库。
- **选项 B**：用户自行按上述路径补装后告知。
- **选项 C**：T010 范围降级为 Pure C++ runtime 先行（Qt adapter 顺延）——不推荐，验收闭环不完整。

## Verification（验证，待补）

- 补装后复核：`lib/cmake/Qt6SerialPort`、`include/QtSerialPort/QSerialPort`、`bin/Qt6SerialPort.dll`、`lib/libQt6SerialPort.a` 存在；
- `find_package(Qt6 COMPONENTS SerialPort)` configure PASS；
- SERIAL-I01（invalid port open）与后续 adapter smoke PASS。

## Learning（教训）

- 新任务依赖新 Qt 组件时，Learning 阶段先做**能力实证**（本任务规则即如此设定）——本次实证在写任何代码前拦截了环境缺口。
- ENVIRONMENT.md 需同步记录：本机 Qt 6.11.1 初始未含 QtSerialPort；补装路径与验证命令。

## Related

- 发现于：T010 Part A Learning / Test Design（docs/tasks/T010-serial-mode.md §"QtSerialPort 本机实证"）
- 阻塞：T010 Part A Implementation（Serial Qt adapter / SERIAL-I01）