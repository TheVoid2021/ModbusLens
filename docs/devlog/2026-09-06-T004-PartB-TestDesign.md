# Devlog — 2026-09-06（T004 Part B: Function 0x03 Codec Learning + Test Design）

- 依据 V1.1b3 §6.3 复核官方示例（一次性 Python 数值对拍，不进仓库）：request data `00 6B 00 03`→107/3；response data `06 02 2B 00 00 00 64`→byteCount=6、values=[555,0,100]。官方 PDF 本环境 404（同 V1.02 口径澄清：非官方资源失效）。
- 三个语义模型定案：`ReadHoldingRegistersRequest{startAddress,quantity}`、`ReadHoldingRegistersResponse{values}`、`ModbusExceptionResponse{exceptionCode}`（跨功能码命名，不存 functionCode——类型即语义）。
- 独立错误模型：`Function03DecodeErrorCode{WrongFunctionCode, InvalidRequestLength, InvalidQuantity, InvalidByteCount, InvalidExceptionLength}`——不复用 Part A 枚举（不同失败层级）。
- 测试矩阵 F03-B01~B12（P0×10/P1×2）落库；quantity 范围 1~125（125 由 256B 帧上限反推）；byteCount 偶数且与实际数据一致。
- 显式 Deferred 至 T007：request/response 配对、quantity 一致性、latency、timeout、status（例：quantity=2 vs byteCount=6/3 寄存器，Part B 不可判）。40001 人类编号不进 Core（0-based 协议地址，换算归 UI/Device Profile）。
- 18 步 Implementation Plan 定稿；本阶段 docs-only，src/、tests/、CMakeLists.txt 未改动，LKGC 保持 `73825c6`。
- 任务档案：[T004](tasks/T004-modbus-rtu-codec.md)