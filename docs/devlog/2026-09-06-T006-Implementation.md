# Devlog — 2026-09-06（T006 Implementation: SimulationFault）

- 实现前追加三条规则进档案：delay 模式隔离（CorruptCrc/None 恒 0ms，无组合故障）、validWire 契约（T006 不做 CRC 校验/解析）、禁墙钟阈值测试（"不真等"的证据是实现无 sleep/timer/thread）。
- TDD RED：`SimulationFault.h` 仅声明 + FAULT-T01~T05 / I01/I02 两套测试 + CMake targets → 链接失败，6 处 undefined reference（applySimulationFault）。stub 未写、未提交。
- 实现 `SimulationFault.{h,cpp}`：单一 switch 四模式——None 透传 / DropResponse 交付 `DroppedResponse{}`（不用空 DeliveredWire 表达"没交付"）/ CorruptCrc 末 CRC 字节 XOR 0x01（payload 不动；空 wire 最小防护透传）/ ArtificialDelay 元数据。无 sleep/timer/thread/random。
- 过程问题：集成测试类名声明/定义不一致（FaultIntegrationTest vs SimulationFaultIntegrationTest，重命名残留）致编译失败——统一后通过；教训：重命名要工具级替换。
- GREEN：FAULT-T01~T05（7 passed，含 T05 确定性双调用 + 模式隔离断言）+ I01/I02（4 passed，wire 金样 BA 7A→7B 逐字节）全过；全项目 ctest **9/9**（+fault/fault_integration）；clean 重建 48 targets 零警告；SimulatedSlave 零修改（git diff 为空）。
- 归档：T006 档案标 DONE（含 14 问索引与实现体会）；PROJECT_STATUS（Current Task=None、Last Completed=T006、M3 ✅ 关闭、LKGC 待回填）；BACKLOG（T006 Done、M3 关闭、T007 Ready）；ARCHITECTURE/TEST_STRATEGY/INTERVIEW 同步。
- 提交：代码提交 `T006: implement deterministic wire fault injection`（= 新 LKGC）+ docs-only 回填提交。
- 任务档案：[T006](tasks/T006-fault-injection.md)