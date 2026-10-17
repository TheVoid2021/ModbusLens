# V4_R1_PRODUCTION_PATH_AUDIT.md — 生产串口调用链源码审计（只读，2026-10-17 16:43）

## 审计方法

独立重新读取当前源码（不引用历史报告结论），逐层 grep + 行号定位。

## 调用链（逐层，实际源码位置）

| 层 | 证据（路径:行号） |
|---|---|
| 1. UI 触发 | `src/ui/qml/pages/EngineeringPage.qml:158 function requestSingleRead()` → `:165 readController.readOnce(...)` → `:441 onClicked: page.requestSingleRead()` |
| 2. Controller 门禁 | `src/ui/engineering/EngineeringReadController.cpp:142/149/157 refuse(...)`（controller_unavailable / invalid_request_parameters / profile_unavailable）+ `:155 profiles_->savedRegisterReadiness(profileId, functionCode, address)`（磁盘 Readiness 复核） |
| 3. 分发 | `EngineeringReadController::readOnce` → `analysis_->readHoldingRegistersOnce(...)`（同文件；无直接 transport 引用） |
| 4. AnalysisController 生产 transport | `src/ui/AnalysisController.cpp:165 serialTransport_ = &serialAdapter_;`（生产默认）+ `:1430 const auto start = serialTransport_->startActiveRequest(descriptor);` + 声明 `src/ui/AnalysisController.h:881 SerialTransactionAdapter serialAdapter_;` |
| 5. 适配器 / QSerialPort | `src/ui/serial/SerialPortAdapter.cpp:136-139` `setDataBits(Data8)` / `setParity(NoParity)` / `setStopBits(OneStop)` / `setFlowControl(NoFlowControl)`；`:38 QSerialPortInfo::availablePorts()`（实时枚举） |
| 6. 事务会话（CRC/RTU 校验） | `SerialPortAdapter.cpp:217 session_.beginActiveRequest(request)` + `:340 session_.feedResponseBytes(...)`（发送时快照 + 接收字节经同一会话状态机） |
| 7. 结果投影 | `AnalysisController::handleSerialTransactionCompleted` → `captureReadResultFromRecord` → `readResultChanged` → `EngineeringReadController::onReadResultChanged`（身份匹配 + decode + Linear 计算 → UI） |

## 正常启动路径的禁用项核验

| 禁用项 | 结果 |
|---|---|
| `RecordingSerialTransport` / `HarnessWriteTransport` / Replay / Fake transport | `Main.qml`=0 · `EngineeringPage.qml`=0 · `EngineeringReadController.h`=0 · `AnalysisController.h`=**1**（仅 `setSerialTransport` **seam 声明**——M10-A 注入点；正常启动不调用，`main.cpp` 中调用全部位于门禁函数内） |
| 直接 Controller 注入 | 无（正常路径仅通过 QML 页面函数） |
| Test-only completion 注入 | 无（`completeReadWithBytes` 仅存在于 main.cpp 的 Harness 类，属门禁函数） |

## 结论

正常 GUI 启动 → EngineeringPage → EngineeringReadController → AnalysisController → **SerialTransactionAdapter（QSerialPort）** → COM7。**§4 审计 PASS**，与冻结契约一致。
