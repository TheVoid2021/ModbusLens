# Devlog 2026-09-08 — T010 Part B Learning / Test Design（Serial UI Integration，docs-only）

## 今日工作

- **T010 Part B 启动（Phase: Learning / Test Design，docs-only）**；全部定案落于 [T010 档案](../tasks/T010-serial-mode.md)（SB-0 ~ SB-22），严格基于真实 Part A API（SerialPortAdapter.h 五个成员 + 两个信号）设计，不依赖聊天记忆：
  - **Controller Serial API**：refreshSerialPorts / connectSerial / disconnectSerial / readHoldingRegistersOnce + serialConnected/serialBusy/hasSerialError/serialErrorMessage/serialPortNames 属性；Replay error 与 Serial transport error 两个独立 state，最小增量。
  - **source 语义**：Connect 成功=来源切换（Serial Mode + "COM3 @ 9600" + 清旧 batch）；失败=原子保留（与 T009 同构）；Disconnect 保留最后诊断结果（Clear ≠ Disconnect 职责分离）。
  - **Read Once**：replace=1 语义（rowCount 恒 1，summarizeTransactions({analysis})）；Success 100%/25ms、Timeout Rate 0.0 合法值 + avg=—；pending metadata 只在 accept 后保存、失败/取消清除。
  - **hardware-free seam**：非 Q_INVOKABLE 的 `publishSerialResult(sourceLabel, deviceAddress, analysis)`——生产 adapter 信号与 UI-S05/06/07 测试共用；与 T008 "禁伪造 status" 不冲突的理由入档。
  - **安全纪律**：port discovery 只 enumerate（QSerialPortInfo::availablePorts）；绝不自动 open 任何 COM（probe 的 2 个设备不证明是 Modbus 设备）；Hardware Smoke 无硬件记 NOT RUN 不伪报；无已知 slave 不做强预期 assertion。
  - **矩阵**：UI-S01~S09 + SERIAL-I02（PE-4 有界回归：QSignalSpy 锁定 transportError 恰 1 次）+ SERIAL-I03（主动断开静默）；28 步计划。
  - **deployment 计划**：app 真实链接 Qt6::SerialPort（core 保持 Zero Qt）；windeployqt 自动部署 Qt6SerialPort.dll；provenance SHA256 == `D:\QT\6.11.1\mingw_64\bin`（ISSUE-003 多 kit 回归保护）。
- 文档同步：PROJECT_STATUS（面板/§2/§7）、BACKLOG（M5/T010 行/路线/变更记录）。

## 关键决策

- **不做第二套 Dashboard/Model/统计**：Serial 只是第三个 source，publish 走现有 applySnapshot + setTransactionEntries 同路径。
- **Clear 与 Disconnect 拆开**：清结果 vs 关连接是两个正交动作，合并会造成误操作（断设备或留过期数据）。
- **seam 用 fixture 而非假 transport**：验证的是 presentation 映射（Core result → UI），Core 正确性已由 SERIAL-A01~A16 锁定。

## 验证（docs-only）

- git 状态/log 复核一致（LKGC `b31233b` / HEAD `072ce16`）；工作区最终仅 docs 变更。
- `git diff --check` PASS；`src/tests/CMakeLists.txt/scripts` 零修改；docs-only commit；LKGC 保持 `b31233b`。

## 追加（同日）· Part B Implementation 与用户确认

- Implementation 完成（`33ed197`，新 LKGC）：Adapter API 拆分 + Controller serial 全套 + QML Serial Controls；UI-S01~S10 + SERIAL-I02/I03/I05 全绿；ctest 18/18、clean 108 targets 零警告、qml smoke、deploy provenance（Qt6SerialPort.dll SHA256=MinGW bin）+ minimal-PATH smoke PASS。
- **用户 Manual Serial UI Smoke = PASS（A~F）**；**Hardware Smoke = NOT RUN（hardware unavailable）**。
- **T010 Part B DONE → T010 整体 DONE → M5（T009+T010）CLOSED**。LKGC=`33ed197`；docs-only 确认提交不再次推进。未 push；未开始 T011。

## 下一步（待用户指令，不自动开始）

- T010 Part B — Implementation（28 步，见 SB-20）；Manual Serial UI Smoke A~F 将 WAITING FOR USER；Hardware Smoke 按 SB-18 政策。