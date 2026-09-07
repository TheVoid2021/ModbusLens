# Devlog 2026-09-07 — T010 Part A Learning / Test Design（Serial Transaction Runtime，docs-only）

## 今日工作

- **T010 启动（Part A: Learning / Test Design，docs-only）**，全部定案落于 [T010 档案](../tasks/T010-serial-mode.md)：
  - **Serial 本质与分工**：真实字节流 → 任意 chunk 累积 → candidate 判终 → 复用 T004/T007 → 现有 Dashboard；三模式共享模型层（Frame/Analysis/Statistics/UI Model），仍不抽 IFrameSource。
  - **framing 定案**：readyRead ≠ 一帧；receive buffer 跨信号累积；正常响应 5+2N、Exception 固定 5 bytes；oversized 不截断（等 timeout 收口整 buffer decode）；不先做 t3.5 scanner。
  - **timeout 双路语义**（Serial 最重要的诊断分界）：零 bytes → NoResponse → Timeout；partial bytes → decode → CrcError/ProtocolError。禁止"Timer 到期全标 Timeout"。
  - **Session 模型/API**：`SerialTransactionSession`（Pure C++，Zero Qt）——两态 Idle/AwaitingResponse、one outstanding → Busy、begin/feed/onResponseTimeout/cancel、完成必回 Idle；elapsed 由 adapter 以 ms 传入。
  - **FC03 encoder 核验**：现有 Function03 无 encode API → Implementation 补最小 `encodeReadHoldingRegistersRequest`（语义 Frame；quantity 1~125）；slave address 限 1~247。
  - 矩阵 SERIAL-A01~A14（含 A07 partial timeout 语义锁定、A13 地址校验）+ SERIAL-I01（invalid port open）落库；18 题问答；22 步计划。

## 重要实证发现（真问题）

**ISSUE-003（OPEN）**：本机 Qt 6.11.1 **未安装 QtSerialPort 组件**——只读取证确认 `include/QtSerialPort`、`lib/cmake/Qt6SerialPort`、`bin/Qt6SerialPort.dll` 全部不存在（仅有 doc/translations 残留）。T010 Part A Implementation 的 Qt adapter 与 SERIAL-I01 将因此阻塞（`find_package(Qt6 COMPONENTS SerialPort)` 必失败）。`D:\QT\MaintenanceTool.exe` 已实证存在，可用官方补装 Additional Libraries → Qt Serial Port（不算装新 Qt、不引第三方库）。详情与选项见 [ISSUE-003](../../issues/ISSUE-003-qtserialport-not-installed.md)。Pure Session（SERIAL-A01~A14）不依赖 Qt，可先行。

## 验证（docs-only）

- 环境只读取证 + git 状态复核（LKGC `d473d36` / HEAD `bb0a652` / T010 未开始，与仓库一致）；FC03 encoder 全局检索无结果。
- docs-only commit；LKGC 维持 `d473d36`。

## 下一步

- 待用户对 ISSUE-003 决策（补装 QtSerialPort 与否）。
- 之后：T010 Part A — Implementation（22 步，见 T010 档案）；不开始 Part B、不开始 T011。