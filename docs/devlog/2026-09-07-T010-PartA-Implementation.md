# Devlog 2026-09-07 — T010 Part A Implementation（Serial Transaction Runtime + QtSerialPort Adapter）

## 今日工作

- **环境前置闭环**：用户补装 Qt Serial Port 后按既定五步实证——正确 MinGW kit 的 headers/CMake package/DLL/.a 全部 EXISTS、compiler 未变、仓库外临时 CMake probe（find_package + Qt6::SerialPort link + 真实使用 QSerialPort/QSerialPortInfo）configure/compile/link/run 四步全过（可见 2 个串口设备）。**ISSUE-003 = RESOLVED ✅**（根因：首次补装落入 MSVC kit `D:\QTDesign\6.11.1\msvc2022_64` 的多 kit 错位）。
- **T010 Part A Implementation 完成**（真实 RED→GREEN，代码提交 `b31233b`）：
  - `encodeReadHoldingRegistersRequest`（T004 最小补全：语义 Frame、quantity 1~125；unicast 地址校验分层留 Session）。
  - `src/core/serial/SerialTransactionSession`（Zero Qt）：两态机、one-outstanding、任意 chunk 累积、修正版 framing（bit7 异常格式 5B 不硬编码 0x83 / 0x03=5+响应 byteCount / 其他正常码等超时）、exact-candidate 不截断、timeout 双路（空→Timeout；partial→真实 wire-truth）、cancel 不伪造状态。
  - `src/ui/serial/SerialPortAdapter`（Qt 层薄壳）：QSerialPort(8N1)+QElapsedTimer+single-shot QTimer（仅超时）；write 失败/fatal→cancel+Transport Error；禁 blocking。
  - 测试：SERIAL-A01~A16 + encoder 3 用例（21）+ adapter 2 用例，全绿；ctest 18/18；clean 106 targets 零警告。
- 归档：T010 档案 Implementation 章、ISSUE-003 RESOLVED、PROJECT_STATUS/BACKLOG/INTERVIEW_NOTES、本 devlog。

## 遇到的真问题

- **PE-4（最有价值）：QSerialPort errorOccurred 反馈风暴**——open 失败端口会发射 `0（NoError）→10（DeviceNotFoundError）` 无限序列（最小 repro 实测几十 KB 输出），handler 内 close 又触发更多，adapter 测试 SIGSEGV。修复：QueuedConnection（避免 open() 栈内重入）+ `suppressPortErrors_` 一次性处理 + 同步路径自报 transportError。
- **PE-5（期望 vs 真实）**：A07 原按 ProtocolError 写预期，实测 4 字节 partial 恰为 RTU 最小帧形状（addr+fn+CRC）→ codec 判 CrcMismatch → **CrcError**。改为双场景锁定真实行为（4B→CrcError、2B→ProtocolError），两种都不等于 Timeout。

## 验证

```text
kit 实证 + CMake probe：configure/compile/link/run 全 PASS
RED：15 处 undefined reference
GREEN：serial 21/21、serial_adapter 2/2；ctest 18/18
clean：106 targets，编译 warning/error 0
Core Zero Qt：src/core/serial 无 Qt include
ISSUE-001：6 处 get_if 均具名 local
deploy：minimal-PATH smoke exit=0（app 未链 SerialPort；Qt6SerialPort.dll 按设计不进 deploy）
```

## Git

- Part A Implementation（code/config）：`b31233b`（**新 LKGC**）
- 归档 docs-only（本 devlog 所在提交），LKGC 哈希回填

## 下一步（待用户指令，不自动开始）

- T010 Part B — Learning / Test Design：Serial UI Integration + Hardware/No-Hardware Smoke。
- Part B 预告已入档案：Port/Baud ComboBox、Connect/Disconnect、Read Once（replace 1 transaction）、Serial header、transport error 展示、hardware smoke（无硬件则 NOT RUN 记录）。