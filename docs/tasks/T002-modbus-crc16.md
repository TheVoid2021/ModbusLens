# T002 — Modbus CRC16

> 状态：**DONE** ✅｜Phase A: ✅ DONE（Learning）｜Phase B: ✅ DONE（Test Design）｜Phase C: ✅ DONE（Test First + Implementation）
> 协议依据：MODBUS over Serial Line Protocol and Implementation Guide **V1.02**（主要，CRC Generation 章节）；Modbus Application Protocol **V1.1b3**（辅助）。
> 任务边界（沿用 BACKLOG）：仅 CRC-16/MODBUS 按位实现与测试。**不含** Frame Codec（T003）、查表优化、benchmark、fuzz、Simulator（T005）、Serial（T010）——全部未越界。

## Goal

交付一个正确、可解释、经标准向量验证的 CRC-16/MODBUS **按位法计算**模块（TDD：先测试后实现），并把学习、设计、实现全程留痕。三阶段均已完成：Phase A 学习留痕、Phase B 测试设计与接口定案、Phase C 实现 + RED→GREEN 全程证据。

## Background

- ModbusLens 是旁路监听诊断平台；CRC 校验是帧解析的第一道闸门，决定一帧进"可信数据"还是"错误统计"。
- CRC-16/MODBUS 参数琐碎（初值、反射多项式、线上字节序），按"学习 → 设计测试 → 实现"三步走，每步留痕、经得起面试追问。
- 项目坚持"协议自己实现、外部实现只做对拍"（02_ARCHITECTURE §5）。

## Technical Decisions（已确定）

| 阶段 | 决策 | 内容 | 理由 |
| --- | --- | --- | --- |
| A | 协议依据 | RTU 层以 V1.02 为主；字节序约定直接引用 V1.02 | 与 T003 帧模型、T005 帧切分共用同一权威来源 |
| A | 算法形态 | 反射按位算法：初值 0xFFFF、右移、判 LSB、条件 XOR 0xA001 | 与串口按位到达方向一致；最直白、适合面试讲解 |
| A | 查表/性能 | 不在 T002 范围 | 遵循 BACKLOG 边界；先正确性后性能 |
| A | 测试基础 | 两个 KAT 向量锁定（0x4B37 / 0x0A84） | 权威输入输出 |
| A | 留痕位置 | 学习内容在 03_MODBUS_LEARNING §10；问答在本文件 | 知识底座与任务档案分离 |
| B | 测试证据分级 | P0 KAT 主要证据；P1 边界/invariant；P2 行为辅助 | KAT 与外部权威对齐；自洽错实现可能骗过 property test（Phase C 的 stub 恰好实证了这一点，见 Problems） |
| B | 接口形态 | `modbuslens::core::calculateModbusCrc(std::span<const std::uint8_t>) -> std::uint16_t` | 核心不依赖 Qt UI、零拷贝、可测试；见 §Interface Design |
| B | T002/T003 职责割线 | T002 只做 CRC calculation（返回数值）；wire serialization（低字节在前）归 T003 | 数值与字节序正交 |
| B | TDD 顺序 | RED→GREEN 13 步；**不提交故意错误代码**，RED 证据只留档案/devlog | 仓库永远可构建 |
| C | Core 载体 | 独立静态库 target `modbuslens_core`（不链接任何 Qt），CRC 为其第一个模块 | 见 §Implementation"Why a Separate Core Target" |

## Test Design（Phase B 定稿，Phase C 已照此实现）

> 概念割线：**CRC calculation**（字节序列 → uint16 数值，T002）与 **CRC byte serialization**（数值 → 帧尾两字节、低字节在前，T003 Frame Model）。矩阵中的 wire bytes 仅作知识验收，T002 未实现序列化。

### 测试矩阵

| Test ID | Input | Expected Result | Why This Test Exists | What Bug It Can Catch |
| --- | --- | --- | --- | --- |
| CRC-T01 | ASCII `123456789`（字节 31–39） | CRC value `0x4B37`（wire `37 4B`） | CRC 通用基准串；规范实现必过锚点 | 初值/多项式/位序/迭代次数任一错误——自洽错实现无法通过 |
| CRC-T02 | `01 03 00 00 00 01` | CRC value `0x0A84`（wire `84 0A`） | 真实帧 KAT：验证参与计算的字节范围理解正确；wire 字节验收字节序理解 | 字节范围错误、字节序理解错误 |
| CRC-T03 | 空字节序列 | `0xFFFF`（原样返回初值） | 纯函数边界契约（空数据不是合法 Modbus 帧，帧合法性属 T003） | 隐藏的"至少读一个字节"假设 |
| CRC-T04 | `00`（单字节） | `0x40BF` | 最短真实输入：最小循环骨架只跑一次 | 循环次数 off-by-one、并入顺序缺陷 |
| CRC-T05 | `01 03 00 00 00 00` vs `01 03 00 00 00 01` | `0xCA45` ≠ `0x0A84` | 敏感性演示（不宣称碰撞保证，CRC 非密码学 hash） | "实现没消费输入"类静态值 bug |
| CRC-T06 | `01 03 00 00 00 01 84 0A` | `0x0000` | invariant 辅助证据；不能替代 KAT | 自洽性破坏 |

### 期望值来源与复核方式

- T01/T02/T06：规范/公开基准向量；T03/T04/T05：一次性独立 Python 参考实现计算并断言（脚本不进仓库）。
- 规范示例对拍：V1.02 示例 `02 07` → value `0x1241`、wire `41 12`；V1.1b3 示例 `11 03 00 6B 00 03` → value `0x8776`、wire `76 87`——与我们的算法一致。
- 关于官方 PDF 访问：Phase B 当时所处的工具与网络环境无法访问 V1.02 PDF（URL 返回 404），因此使用已知参数与规范印刷示例做了独立交叉验证。**这不代表 Modbus Organization 官方资源本身已经失效**（后续可换网络/工具再取原文复核）。

### 测试优先级

P0（必须通过）= T01/T02/T03；P1（应该通过）= T04/T06；P2（行为辅助）= T05。**KAT 是主要证据，property/invariant 是补充证据。**

## Interface Design（Phase B 定案，Phase C 已按此实现）

- 签名：`namespace modbuslens::core { std::uint16_t calculateModbusCrc(std::span<const std::uint8_t> data); }`
- 方案 B（标准 C++ span）胜出：核心可测试（无需 Qt 对象）、零拷贝（指针+长度只读视图）、生命周期安全（调用方持有数据）、初学者可懂、不过度设计。
- 明确排除：QByteArray 入参（绑定 QtCore）、迭代器对、ranges 约束模板。
- 返回**数值 CRC**；序列化（低字节在前）归 T003。

## Phase C Execution Plan（13 步，已执行）

1. 创建 CRC public interface → 2. 创建 CRC tests → 3. 未正确实现时运行测试、保留真实 RED 证据 → 4. 实现最简按位 CRC → 5. 再运行测试 → 6. GREEN → 7. 全项目 Build → 8. CTest 全通过 → 9. 问题记录 → 10. Knowledge Learned → 11. PROJECT_STATUS/BACKLOG → 12. LKGC 推进 → 13. Git commit。执行实录见 §Implementation / §Verification。

## Knowledge I Must Be Able To Explain（Phase A 必答清单）

**Q1. CRC 是干什么的？**
完整性校验码：发送方对"地址+功能码+数据"算出 2 字节校验附在帧尾，接收方重算比对，不一致即判定帧损坏并丢弃。解决"意外损坏检测"，不解决"加密"或"来源证明"。

**Q2. CRC 和普通 checksum 有什么不同？**
checksum（求和/LRC）是弱校验：字节换位求和不变，照样漏检。CRC 是模 2 多项式除法取余，对字节位置敏感，可检测全部 1-bit 错误、奇数个位错误、长度 <16 bit 的突发错误及绝大多数更长突发；16-bit 随机漏检率约 1/65536。成本同量级，检出能力显著更强。

**Q3. CRC 能防止恶意篡改吗？**
不能。无密钥、算法公开，篡改后重算即可通过。防篡改需要密钥/签名/MAC。ModbusLens 绝不把 CRC 通过当安全证据。

**Q4. 为什么初始值是 0xFFFF？**
① CRC-16/MODBUS（IBM 系）家族的规范参数（V1.02 规定）；② 若初值为 0，全 0 数据与前导 0 字节不改变寄存器，"没数据"与"全 0 数据"无法区分；0xFFFF 保证任何输入都改变状态。

**Q5. 0xA001 是什么？**
反射后的生成多项式。规范多项式 0x8005（x¹⁶+x¹⁵+x²+1）的位反转；反射算法（右移/判 LSB）每步"减去除数"= XOR 0xA001。模 2 减法即 XOR。

**Q6. 为什么代码里 CRC 是 0x0A84，但报文里看到的是 84 0A？**
0x0A84 是 uint16 数值；帧尾按"低字节在前"放置：0x84 先发、0x0A 后发。数值与字节序正交，与主机端序无关，必须手工逐字节写入。

**Q7. 如果 CRC 校验失败，ModbusLens 后续怎么处理？**
旁路观察者口径：1) 不丢弃——保留原始字节+时间戳+方向，标记校验失败；2) 计数——进错误统计供诊断规则消费；3) 隔离——坏帧不进事务配对；4) 不纠错不重传；5) UI 坏帧高亮（T008）。

## Testing Knowledge I Must Be Able To Explain（Phase B 必答清单）

**TQ1. 为什么不能只测试一个 CRC 输入？**
一个输入锚定不了算法身份的全部自由度（初值/多项式/位序/迭代次数/并入方式，任一出错都可能碰巧不显形）。标准串+真实帧+边界长度组合才能逐一钉死。

**TQ2. Known Answer Test 是什么？**
输入与期望输出均由外部权威给定的测试：不"算"期望而是"引用"答案。互操作正确性=与别人一致，KAT 直接锚定"别人"，是主要证据。

**TQ3. 为什么空输入测试仍有价值？**
测纯函数的确定性边界行为（空→原样返回初值），防止隐藏的"至少读一个字节"假设被误用。双重界定：空数据不是合法 Modbus 帧，帧合法性属 T003。

**TQ4. 为什么 zero-remainder 不能替代 KAT？**
它是自洽性检查：错误但自洽的实现也可能"append 后为零"。KAT 与外部权威对齐，invariant 只与自己对齐。Phase C 的 stub 实验实证了这一点（恒返 0 的 stub 让 T06 通过而全部 KAT 失败）。

**TQ5. 为什么 calculation 与 serialization 分开测试？**
两者正交（算法 bug vs 端序/拼接 bug），分开=失败定位精确；也对应任务边界：wire 字节在 T002 只是知识验收（CRC-T02 注释），序列化实现归 T003。

**TQ6. 什么是 boundary test？**
在定义域边界（空、单字节、上限长度）验证行为；off-by-one 与初始化/收尾错误最常在边界暴露。

**TQ7. 为什么暂时不做 benchmark？**
v1 无性能需求（10 帧/秒、≤256B 帧，按位法远小于 1ms）；无真实需求与 profiling 证据不优化，且 benchmark 引入 CI 噪声。NFR-05 量化（T007 后）确有需求再立项。

**TQ8. 为什么暂时不实现 lookup-table？**
查表是空间换时间的优化，只在性能需求成立时值得；代价是可读性下降+表格生成正确性风险。v1 优先正确性、可解释性、进度；有 profiling 依据再走独立任务+ADR。

## Before Coding Checklist（Phase C 前已逐项确认）

- [x] CRC 用途：检测传输意外损坏，不是安全机制
- [x] 初始值 0xFFFF（家族规范参数 + 全 0 盲区）
- [x] 按位流程：init → 每字节 XOR → 8×（看 LSB → 右移 → LSB=1 则 XOR 0xA001）
- [x] polynomial：0x8005 / 反射 0xA001
- [x] byte order：数值 vs 线上（低字节在前），0x0A84 → 84 0A
- [x] 两个测试向量：0x4B37（37 4B）、0x0A84（84 0A）
- [x] 坏帧处理口径（Q7）
- [x] 测试矩阵与优先级（P0=T01/T02/T03；P1=T04/T06；P2=T05）
- [x] 接口签名默写

## Implementation（Phase C 实录）

### 新增文件

- `src/core/protocol/ModbusCrc.h`：接口声明（Phase B 定案签名），注释写明算法参数与"数值 vs 序列化"的职责割线。
- `src/core/protocol/ModbusCrc.cpp`：按位实现。
- `tests/test_modbus_crc.cpp`：QtTest 正式测试，T01–T06 六个用例，断言与 Phase B 矩阵一一对应；CRC-T02 只断言数值 `0x0A84`，wire `84 0A` 以注释保留为 T003 序列化验收依据；CRC-T05 两个输入完整写出并直接断言两个已知数值（`0xCA45` 与 `0x0A84`），比"只验不同"更强。
- `CMakeLists.txt`：新增 `modbuslens_core` 静态库 target 与 `modbuslens_crc_tests` 测试 target。

### 依赖关系（T002 后的实际 target 图）

```text
modbuslens (Qt6::Widgets)
    └── modbuslens_core (STATIC, 无任何 Qt 链接)   ← CRC 是第一个模块
modbuslens_tests (Qt6::Widgets + Qt6::Test)        ← smoke
modbuslens_crc_tests (modbuslens_core + Qt6::Test) ← CRC-T01~T06
```

### Why a Separate Core Target（为什么放独立 core target 而不是写进 UI）

1. **协议逻辑与 GUI 解耦**：CRC/解析/事务是确定性纯逻辑，混进 MainWindow 会被事件循环、信号槽和 UI 生命周期污染，也无法在无显示器环境验证。
2. **三模式复用**：Simulator/Replay/Serial 的数据源不同，但都要调用同一套协议核心——独立库是"共享核心、禁止复制"纪律（AGENTS 纪律 10）在构建系统上的落点。
3. **更容易单元测试**：`modbuslens_crc_tests` 只链 core + QtTest，测试毫秒级、可在 CI/offscreen 环境跑，不拖起整个 GUI。
4. **不锁定 Qt**：core 是纯 C++20（只用 `<cstdint>/<span>`），即使未来某部分不用 Qt（CLI 工具、其他平台），协议核心原样可用。

未建立更多 library target（io/ui 等留待对应任务），避免提前抽象。

### 算法实现（与 Phase A 伪代码一一对应）

```cpp
std::uint16_t crc = 0xFFFF;                    // 家族规范初值
for (std::uint8_t byte : data) {               // 覆盖地址+功能码+数据
    crc ^= byte;                               // 并入一个字节（长除法"带下一位"）
    for (int bit = 0; bit < 8; ++bit) {        // 每字节 8 bit 各迭代一次
        const std::uint16_t previousLsb = crc & 0x0001;  // 先看 LSB（当前商位）
        crc >>= 1;                             // 右移：推出已处理位（反射算法）
        if (previousLsb == 1) {
            crc ^= 0xA001;                     // 商位为 1 → XOR 反射多项式
        }
    }
}
return crc;                                    // 数值 CRC（序列化归 T003）
```

要点：全部为值语义整数运算（XOR/移位/比较），无 reinterpret_cast、无 memcpy、无未定义行为（`std::uint16_t` 无符号、移位量固定 1），**与 CPU 端序无关**——端序只影响"字节在内存里怎么排"，而本算法是对"数值"做位运算。

### TDD RED stub（仅存在于未提交工作区，未入库）

为让测试可链接，先以 `return 0x0000;` 的 stub 占位并运行测试采集 RED；随后同一文件被真实实现替换。stub 未进入任何提交（`git diff` 中 cpp 文件直接呈现最终实现）。

## Files Changed

Phase A：`docs/tasks/T002-modbus-crc16.md`（新增）、`docs/devlog/2026-09-05-T002-PhaseA.md`（新增）、`docs/03_MODBUS_LEARNING.md`、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`。

Phase B：`docs/tasks/T002-modbus-crc16.md`、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、`docs/04_TEST_STRATEGY.md`、`docs/devlog/2026-09-05-T002-PhaseB.md`（新增）。

Phase C（本阶段）：
- 新增：`src/core/protocol/ModbusCrc.h`、`src/core/protocol/ModbusCrc.cpp`、`tests/test_modbus_crc.cpp`、`docs/devlog/2026-09-05-T002-PhaseC.md`
- 修改：`CMakeLists.txt`（core/crc-tests target）、`docs/tasks/T002-modbus-crc16.md`（本文件补齐）、`docs/PROJECT_STATUS.md`、`docs/BACKLOG.md`、`docs/02_ARCHITECTURE.md`、`docs/04_TEST_STRATEGY.md`、`docs/INTERVIEW_NOTES.md`、`docs/03_MODBUS_LEARNING.md`（404 表述追加澄清）
- 未改动：`src/main.cpp`、`tests/test_smoke.cpp`、`CMakePresets.json`

## Problems Encountered

Phase A/B（历史记录，保留）：
1. 数值/线上字节序记忆颠倒（41 12 ↔ 0x4112），独立断言当场抓出——已更正并成为 Q6 的实证。
2. Phase B 当时所处的工具/网络环境无法访问 V1.02 PDF（URL 404），改用"公开参数 + 规范印刷示例数值对拍"完成复核。〔追加澄清〕这不代表 Modbus Organization 官方资源本身已经失效；后续环境允许时可重新获取原文二次复核。

Phase C（真实发生）：
1. **stub 让 CRC-T06 通过而全部 KAT 失败**：恒返 0 的占位实现骗过了 zero-remainder invariant（0 恰好是 T06 若走"错路径"也会得到的值），却骗不过任何 KAT——Phase B"KAT 是主要证据、invariant 是补充证据"的论断被当场实证。这不是代码 bug，是测试设计的宝贵教训。
2. **Git Bash 管道下 QtTest stdout 丢失**：首次直接运行 `modbuslens_crc_tests.exe 2>&1 | head` 只拿到退出码 5、看不到失败详情（Windows GUI 子系统 + 管道行为）。影响的是证据采集，不是被测代码。
3. 实现本身：**No significant implementation issue encountered**——没有出现 signed/unsigned、span 构造、include、CMake 链接或 CRC 结果错误问题；一次实现即 GREEN。

## Solutions

1. T06 事件 → 写进档案作为"invariant 证据力低于 KAT"的实证；测试代码不变（T06 本来就定位 P1 辅助）。
2. 改用 QtTest 自带输出重定向 `-o build/red_crc.txt,txt` 落盘后读取，完整拿到 5 个 FAIL 的 expected/actual；GREEN 阶段同样方式留证。
3. （无实现问题需解决。）

## Verification

### RED（实现前，stub 返回 0x0000，未提交）

命令与输出（QtTest `-o` 落盘，节选关键行）：

```text
$ cmake --preset debug-local && cmake --build --preset debug-local   # 构建成功（stub）
$ ./build/debug/modbuslens_crc_tests.exe -o build/red_crc.txt,txt
exit_code=5（QtTest 返回失败用例数）

FAIL!  : ModbusCrcTest::t01_standardKat() Compared values are not the same
   Actual   (calculateModbusCrc(data)): 0        Expected: 19255 (0x4B37)
FAIL!  : ModbusCrcTest::t02_modbusRequestKat()
   Actual  : 0   Expected: 2692 (0x0A84)
FAIL!  : ModbusCrcTest::t03_emptyInput()
   Actual  : 0   Expected: 65535 (0xFFFF)
FAIL!  : ModbusCrcTest::t04_singleByte()
   Actual  : 0   Expected: 16575 (0x40BF)
FAIL!  : ModbusCrcTest::t05_inputChangesResult()
   Actual  : 0   Expected: 51781 (0xCA45)
PASS   : ModbusCrcTest::t06_zeroRemainder()          ← stub 恒 0 恰好满足 T06
Totals: 3 passed, 5 failed

$ ctest --preset debug-local -R "^crc$"
The following tests FAILED: 2 - crc (Failed)
```

为什么 RED 是预期结果：stub 未实现任何算法，5 个 P0/P1 KAT/边界用例必须失败；T06 通过恰好证明"invariant 不能替代 KAT"。证据真实，未伪造、未植入复杂 bug。

### GREEN（实现后）

```text
$ cmake --build --preset debug-local
[5/5] Linking CXX executable modbuslens_crc_tests.exe
$ ./build/debug/modbuslens_crc_tests.exe -o build/green_crc.txt,txt
exit_code=0
PASS : t01_standardKat / t02_modbusRequestKat / t03_emptyInput /
       t04_singleByte / t05_inputChangesResult / t06_zeroRemainder
Totals: 8 passed, 0 failed, 0 skipped (2ms)

$ ctest --preset debug-local
2/2 Test #1: smoke  Passed   #2: crc  Passed
100% tests passed, 0 tests failed out of 2

$ cmake --build --preset debug-local --clean-first   # clean 全量重建
警告/错误行数 grep = 0（零警告，含新 core target）
ctest 再次 2/2 通过
```

## Result

✅ **T002 DONE**。交付：
- `modbuslens_core`（无 Qt 纯 C++20 静态库）中的 `calculateModbusCrc` 按位实现；
- 6 个正式测试（T01–T06）全绿，RED→GREEN 全程真实留痕；
- 全项目 configure/build（零警告）/ctest（2/2：smoke、crc）通过；
- BACKLOG 边界全部遵守：无查表/benchmark/fuzz/Frame Codec/Simulator/Serial，T003 未启动。

## Knowledge Learned

- **为什么 `std::span<const std::uint8_t>`**：只读"指针+长度"视图——零拷贝、不拥有数据（生命周期归调用方）、任何字节容器通用、核心无需 Qt 对象即可测试。
- **为什么 Core 不用 QByteArray**：会把纯逻辑绑到 QtCore（隐式共享、Qt 头依赖），非 Qt 场景（CLI/其他平台/纯单测）被拖累；需要时在调用侧一行适配即可。
- **为什么返回 `uint16_t` 而不是两个 wire 字节**：calculation 与 serialization 正交——返回数值让 CRC 函数职责单一、可独立测试；"低字节在前"是帧格式约定，归 T003 帧模型，混在一起会让两层的 bug 互相掩护。
- **为什么按位法而不是 lookup table**：v1 优先正确性/可解释性/进度；按位法每行可讲、逐位可验；查表是性能优化，需真实需求+profiling 依据（暂无），且带来表格生成正确性风险。
- **为什么有 KAT 还要测空输入和 zero-remainder**：KAT 锚定"算法身份"，边界测试钉住"函数契约"（空输入行为明确），invariant 提供自洽性检查——三者证据角色不同。stub 实验实证：T06 在错误实现下也能通过，KAT 不能省。
- **为什么实现与 CPU endian 无关**：算法只对 `std::uint16_t`/`std::uint8_t` 做**值语义**的 XOR/移位/比较，不触碰内存布局（无 reinterpret_cast/memcpy/union 拆字节）；端序只影响字节在内存的排列，序列化时的"低字节在前"由 T003 显式按字节写出，同样与端序无关。
- 工程小课：Windows GUI 子系统程序在 Git Bash 管道下 stdout 可能丢失，QtTest 用 `-o file,txt` 落盘是最稳的证据采集方式。

## Potential Interview Questions

- 前两阶段 15 题仍有效（两个 Knowledge 清单）。
- Phase C 新增可答点：
  1. 为什么把协议核心做成独立静态库 target？（解耦/复用/可测性/不锁 Qt）
  2. 你的 CRC 实现如何证明与端序无关？（值语义位运算 vs 内存布局访问）
  3. TDD 的 RED 证据怎么留才不作弊？（stub 只在工作区、不入库；失败输出落盘存档）
  4. `std::span` 与 `const std::vector<uint8_t>&` 作为参数的取舍？（零拷贝视图 vs 容器绑定；span 亦兼容 C 数组）
  5. 为什么 `previousLsb` 要在移位前取？（先判后移，判断的是"被移出前"的商位）

## Git Commit

| 提交 | 哈希 | 说明 |
| --- | --- | --- |
| Phase A | `a64ee7e` | `T002(Phase A): CRC16 学习 checkpoint — 知识留痕（docs-only）` |
| Phase B | `28a538c` | `T002(Phase B): CRC16 测试设计 — 测试矩阵/接口定案/Phase C 计划（docs-only）` |
| Phase C（代码提交，**LKGC**） | `PENDING-BACKFILL` | `T002: implement CRC-16/MODBUS core with KAT tests` |
| 回填提交（docs-only，HEAD） | 见 `git log` | 回填代码提交哈希至本档案与 PROJECT_STATUS |

> LKGC 推进：Phase C 产生了业务代码并经 configure/build/ctest 验证，LKGC 从 `aa337f6` 推进至 Phase C 代码提交（哈希由 docs-only 回填提交写入 PROJECT_STATUS）；HEAD 为回填提交，两者在 PROJECT_STATUS 中明确区分。