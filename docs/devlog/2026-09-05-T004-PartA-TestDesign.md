# Devlog — 2026-09-05（T004 Part A: RTU Wire Codec Test Design）

- 接口定案：`encodeRtuFrame(const ModbusRtuFrame&) -> std::vector<std::uint8_t>`；`decodeRtuFrame(std::span<const std::uint8_t>) -> std::variant<ModbusRtuFrame, RtuDecodeError>`（enum class {FrameTooShort, CrcMismatch} + struct 包装，便于未来加诊断字段）。对比 bool/optional/expected/异常五案后取 variant——诊断工具必须知道失败原因。
- 测试矩阵 RTU-A01~A07（P0：A01 encode KAT / A02 decode KAT / A03 CRC mismatch / A04 最小 4 字节与空输入；P1：A05 空 data 编码 `01 07 41 E2`、A06 异常形态透明 round-trip、A07 普通 round-trip）。
- 期望值一次性独立复核：crc(01 03 00 00 00 02)=0x0BC4（A03 前提）、crc(01 07)=0xE241、crc(01 83 02)=0xF1C0、crc(01 03 02 00 64)=0xAFB9。
- 设计边界落库：CRC mismatch 只返回结构化错误（不写日志/不统计/不建事务）；decode 用非拥有 span，raw bytes 由更高层保存；ADU≤256B 记为 Deferred Decision（oversized/streaming/t1.5/t3.5 属 T010）。
- PROJECT_STATUS 增加 Current Part / Next Part 行；BACKLOG T004 行同步。本阶段 docs-only，src/、tests/、CMakeLists.txt 未改动，LKGC 保持 `a44a6d2`。
- 任务档案：[T004](tasks/T004-modbus-rtu-codec.md)