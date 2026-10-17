# PRODUCTION_SERIAL_PATH.md — 阶段 1 审计结果（READ-ONLY）

## 调用链（每一级源码位置）

1. **EngineeringPage.qml** (`src/ui/qml/pages/EngineeringPage.qml`)
   - `readController.readOnce(selectedProfileId, selectedFunctionCode, selectedAddress, slave, timeout)`
   - → `requestSingleRead()` QML 函数
2. **EngineeringReadController::readOnce** (`src/ui/engineering/EngineeringReadController.cpp`)
   - 三重门禁（参数域 / 磁盘 Readiness 复核 / 串口三态）
   - → `analysis_->readHoldingRegistersOnce(slaveId, address, snapshot.quantity, timeoutMs, functionCode)`
   - **无直接 transport 引用**（grep serialTransport_/startActiveRequest/Harness/Recording = 0 命中）
3. **AnalysisController::readHoldingRegistersOnce** (`src/ui/AnalysisController.cpp:1294`)
   - 参数域 guard → descriptor 构造（intent + wire ADU）→ `serialTransport_->startActiveRequest(descriptor)`
   - `pendingRequest_ = descriptor; serialBusy_ = true`
4. **SerialTransactionAdapter** (`src/ui/serial/SerialPortAdapter.cpp`，`SerialTransactionAdapter`)
   - 生产默认实现（`serialAdapter_` 成员，`AnalysisController.h:881`）
   - `session_.beginActiveRequest(request)` + QSerialPort write
5. **SerialPortAdapter / QSerialPort** (`src/ui/serial/SerialPortAdapter.cpp:136-139`)
   - 8N1 + caller baud + QSerialPort 实际 COM write/read
6. **Windows COM** → 外部设备

## 特别确认

- A. 生产路径无 `setSerialTransport(&Harness...)`：
  main.cpp 中的 setSerialTransport 调用全部位于门禁/测试函数内
  （:14674/:14676 = runC2ReadCheck 内——仅 `--qml-c2-read-check` 启动参数时执行；
  :21882 = 另一门禁函数内）。**正常 GUI 启动（无启动参数）不经过任何门禁函数**。
- B/C/D/E. RecordingSerialTransport / HarnessWriteTransport / runC2ReadCheck /
  --qml-c2-read-check 在 Main.qml / AnalysisController.h / EngineeringReadController.h
  中 **0 命中**（仅 main.cpp 的门禁函数与分发分支）。
- F. 正常启动使用生产 `SerialTransactionAdapter serialAdapter_`（QSerialPort）：
  `AnalysisController::serialTransport_` 初始指向 `serialAdapter_`（M10-A seam，
  "production default points at serialAdapter_"）。AnalysisController.h:881-882。
- EngineeringReadController 无 HarnessWriteTransport / RecordingSerialTransport /
  completeReadWithBytes / direct result injection 引用。

## 结论

正常 GUI 启动 → EngineeringPage → EngineeringReadController → AnalysisController
→ SerialTransactionAdapter（生产）→ SerialPortAdapter/QSerialPort → Windows COM。
无 fake transport、无 replay、无 harness 注入。**审计通过**。
