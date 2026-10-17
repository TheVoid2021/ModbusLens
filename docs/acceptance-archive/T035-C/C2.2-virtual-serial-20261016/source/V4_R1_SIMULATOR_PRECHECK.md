# V4_R1_SIMULATOR_PRECHECK.md — 外部从站预检（静态 + 独占性，2026-10-17 16:43）

## 复用 R3 已审计模拟器（不重新实现）

- `virtual_modbus_slave.ps1` — SHA-256 `5bedfcf1d4fee9ee484a99e66c9323fcb6d6cc95582cb200e80ba3fce8277ac8`（与 R3 审计版本一致）
- `probe_virtual_modbus.ps1` — SHA-256 `b62af4a03e5d6f03a5bd0f0101dc8026314d03b8c6b297e92389a92169f9b6ae`（V4 不使用该 probe；COM7 归属 ModbusLens）
- `run_v3_matrix.ps1` — SHA-256 `21e471bb749ff20b0dea5145af44555e663105a961180f15d0301c199a8d3630`（V4 不使用）

## 静态契约核验（脚本参数与固定值）

- 端口 = **COM8**（`-PortName` 默认 "COM8"）
- 9600 / 8 / None / One / No FlowControl（SerialPort 构造 + `Handshake::None`）
- Slave ID = **1**；支持 FC **0x03**；Address = **49**（0-based）；Quantity = **1**；Register = **1234 (0x04D2)**
- 真实 COM8 读 → 累积 8 字节窗口 → CRC 校验 → slave/fc/addr/qty 校验 → 动态构造响应 → 动态重算 CRC → 真实 COM8 Write
- 有界：MaxRequests=1 / IdleExitSeconds=8；finally 释放端口；不操作其他进程；不调用产品 API
- 记录：`slave_log_<MODE>.log`（REQUEST/TX/RX hex + 时间戳）
- 模式覆盖：NORMAL / CRC_ERROR / TIMEOUT / WRONG_SLAVE / WRONG_FUNCTION / EXCEPTION / FRAGMENTED（V4 逐个复用）

## 独占性

- COM7/COM8 当前空闲（独占 open+release 测试 PASS，见 V4_R1_PRE_ENVIRONMENT.md）
- V4 期间归属约定：**COM7 → ModbusLens（生产 QSerialPort）· COM8 → 外部从站**
- 不运行 V3 probe/matrix；不使用其他工具占用 COM7

## V4 与 V3 的差异（必须声明）

V3 的 probe（COM7）与从站（COM8）互测；**V4 中 COM7 侧换为 ModbusLens 生产 QSerialPort 路径**——生产程序自行编码请求、接收响应并解码。模拟器脚本本身不变。

## Precheck 结论

**PASS**（静态契约与独占性满足）。运行期将从站以 `-Mode NORMAL` 等模式按 Human GUI 节奏启动。
