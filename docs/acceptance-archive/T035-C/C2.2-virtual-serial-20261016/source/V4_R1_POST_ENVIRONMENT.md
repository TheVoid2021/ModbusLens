# V4_R1_POST_ENVIRONMENT.md — 测试后环境稳定性（V4-R2 实测，2026-10-17 17:12）

> 说明：V4-R1 时本文件记录「未执行测试」的收束态；**本轮（V4-R2）已执行完整 E1–E7，本文件被测试后实测结果取代**。
> R1 收束态事实保留于 `V4_R1_FINAL_REPORT.md`。

## §7 测试后检查（E1–E7 全部完成后，Human 已断开串口并关闭 GUI）

| 检查项 | 结果 |
|---|---|
| GetPortNames | **COM3 / COM4 / COM7 / COM8** |
| PnP ports | COM7->COM8 **OK**（VSBC9\DEVICES\0000，CM_PROB_NONE）· COM8->COM7 **OK**（\0001）· 蓝牙 COM3/COM4 **OK** |
| 驱动服务 | evserial9 **Running(0)** · VSBC9 **Running(0)** · sershare Stopped(1077 按需) |
| SERIALCOMM | `VSerial9_0=COM7` · `VSerial9_1=COM8`（未改变） |
| 问题设备 | **(none)** |
| COM7 / COM8 开闭 | **OPEN+CLOSE OK**（ModbusLens 已释放 COM7，从站已释放 COM8） |
| 最小双向校验 | **PASS — `5A A5 7E` COM7→COM8 3/3 EXACT** |
| 蓝牙 COM3/COM4 | 未变化 |
| 从站进程 | 全部已退出（各模式 MaxRequests 达上限后 bounded exit；FRAGMENTED 17:11:03 释放 COM8） |

## 观察项

- `modbuslens` 进程（pid **33524**，StartTime 17:06:26）在 Human 报告「已关闭」后**仍存在**，
  但**未持有任何串口**（COM7/COM8 均可独占打开）。疑为第二个 GUI 实例未被关闭。
  **不影响 V4 结论**（无端口占用、无通信残留）；列为待 Human 确认的收尾项。

## 结论

**环境稳定，Pair 完好，测试前后一致，无端口消失、无驱动异常、无残留串口占用。**


---

## R3 附录 — 进程退出确认（2026-10-17）

> 追加记录。上文 §7 实测（17:12）保持原样，其中「观察项：modbuslens pid 33524 仍在」在当时真实存在，
> 不追改为当时不存在。

**Human 事后检查（2026-10-17）**：
Get-CimInstance Win32_Process -Filter "Name='modbuslens.exe'" | Select-Object ProcessId, CreationDate, ExecutablePath
→ **无输出**（该次查询时无 modbuslens.exe 进程）。

**结论**：R2 的收尾观察项（PID 33524）**已解除**；结合 R2 已有的 COM7/COM8 开闭与最小双向校验（5A A5 7E 3/3 EXACT），
**测试后环境收尾门禁 = PASS**。

**证据归属**：本条为 Human 提供的事后只读检查结果；Agent 本轮未启动任何程序、未打开任何端口、未复测。
