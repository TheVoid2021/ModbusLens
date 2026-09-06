# Devlog — 2026-09-06（T005 Implementation: SimulatedSlave）

- TDD RED：`SimulatedSlave.h` 仅声明 + SIM-T01~T07 / SIM-I01 两套测试 + CMake targets → 链接失败，29 处 undefined reference（构造/set/handleRequest）。
- 实现 `src/core/simulator/SimulatedSlave.{h,cpp}`：const `handleRequest`（纯应答端点）按序处理——地址归属（IgnoredRequest）→ 功能分发（≠0x03 → `fn|0x80/{0x01}`）→ **复用 T004B decoder**（错误 → `0x83/{0x03}`）→ 32 位越界判定（含区间跨末尾，`0x83/{0x02}`）→ 正常响应（byteCount + 显式大端写侧）。寄存器空间语义：连续 vector，set 时 resize 补 0（追加进档案）。
- **ISSUE-001 建档**：首版后 t01 失败（T02~T07 过）+ 集成测试编译失败（`&右值` ×4）同根——variant 测试辅助函数把"指向临时 variant 内部"的指针带出语句边界（UB 悬垂）；修复 = 辅助函数改 `std::optional<T>` 拷贝语义 + 集成测试绑定具名局部量；T004 的 codec/f03 测试同批修复（断言零变化）。教训：`const&` 绑临时合法，但内部取址传出语句即 UB；"测试全过 ≠ 无 UB"。
- GREEN：SIM-T01~T07（10 passed）+ SIM-I01（3 passed，wire 金样 `C4 0B`/`BA 7A` 逐字节）全过；全项目 ctest **7/7**（smoke/crc/frame/codec/f03/simulator/simulator_integration）；clean 重建 39 targets 零警告。
- 归档：T005 档案标 DONE（含 12 题问答与实现体会）；PROJECT_STATUS（Current Task=None、Last Completed=T005、M3 进行中、LKGC 待回填）；BACKLOG（T005 Done、T006 Ready）；ARCHITECTURE/TEST_STRATEGY/INTERVIEW 同步。
- 提交：代码提交 `T005: implement deterministic Modbus simulated slave`（= 新 LKGC）+ docs-only 回填提交。
- 任务档案：[T005](tasks/T005-simulator-basic-slave.md)；Issue：[ISSUE-001](issues/ISSUE-001-variant-test-dangling-pointer.md)