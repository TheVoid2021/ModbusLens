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
## 2026-09-07 补装后重实证 = **FAIL**（用户声称已安装，机器证据不支持）

用户反馈："Qt SerialPort 已通过官方 MaintenanceTool 手动安装完成"。按规则执行安装后重实证（不因口头确认跳过检测），逐项结果：

| 核验项 | 期望 | 实际 |
| --- | --- | --- |
| `D:\QT\6.11.1\mingw_64\include\QtSerialPort`（QSerialPort/QSerialPortInfo） | 存在 | **不存在**（include 全目录字母序列表无 QtSerialPort/QtSerialBus） |
| `D:\QT\6.11.1\mingw_64\lib\cmake\Qt6SerialPort` | 存在 | **不存在**（lib/cmake grep -i serial 无输出） |
| `D:\QT\6.11.1\mingw_64\bin\Qt6SerialPort.dll` | 存在 | **不存在**（bin 151 个文件，grep -i serial 0 命中） |
| `D:\QT\6.11.1\mingw_64\lib\libQt6SerialPort.a` | 存在 | **不存在** |
| compiler | 仍为 MinGW13.1 | 未受影响（CMakeCache 仍指向 D:/QT/Tools/mingw1310_64，编译链无问题，但组件缺失仍在） |
| MaintenanceTool 今日运行痕迹 | InstallationLog.txt 应有 2026-09-07 记录 | **无**——`D:\QT\InstallationLog.txt` mtime = **Sep 1 19:42**；MaintenanceTool.dat/components.xml mtime = Aug 7 16:06 |
| 其他 Qt 安装位置 | — | 全盘例行检查：仅 `D:\QT`（6.11.1/mingw_64）一处；C:\Qt 不存在；唯一在盘的 Qt6SerialPort.dll 位于 `D:\QT\Tools\QtCreator\bin\`（QtCreator 自带运行时，与 6.11.1 kit 无关） |

**结论**：本机 Qt 6.11.1 kit 上 QtSerialPort 组件仍未落地；MaintenanceTool 尚无今日运行记录。可能原因（供用户排查，非断言）：组件被勾选安装到了其他 Qt 版本条目（本机只有 6.11.1，不存在该情况）；安装尚未完成/被取消；勾选的组件名不同（6.11.1 下应为 `Additional Libraries → Qt Serial Port`）。

**状态：仍 OPEN。T010 Part A Implementation 按规则停止（一步失败即停）。**

## 用户自查清单（排查提示）

1. 打开 `D:\QT\MaintenanceTool.exe` → "Add or remove components" → 搜索框输入 `serial` → 确认 **Qt 6.11.1 → Additional Libraries → Qt Serial Port** 复选框是否已勾选（不是 Qt Serial Bus）。
2. 补装完成后运行安装日志应出现当日记录：查看 `D:\QT\InstallationLog.txt` 末尾时间戳。
3. 硬核验（PowerShell）：`Test-Path D:\QT\6.11.1\mingw_64\include\QtSerialPort` 与 `Test-Path D:\QT\6.11.1\mingw_64\bin\Qt6SerialPort.dll` 应为 True。
