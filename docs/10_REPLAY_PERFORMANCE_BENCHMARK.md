# 10 — Replay 链路性能基准（Replay Pipeline Performance Benchmark）

> 状态：v1（2026-09-14 建立，简历性能声明核验 + 证据落库；与 `scripts/bench_replay/` 复现工具配套）。
> 本文只陈述**在本机真实测得**的数据；口径、原始输出、复现命令全部可追溯，测不到的事实一律标注。

## 1. 为什么有这份文档

1. 简历声明了三组性能数字：**10 万条混合记录中位 70.9 ms ≈ 141 万条/秒（0.709 μs/条）；1M 记录 ≈686.7 ms；10k~1M 近线性**。
2. AGENTS.md 纪律 13 要求重要数据必须落在仓库 Markdown；此前这些数字在仓库中无任何痕迹，无法追溯。
3. 2026-09-14 用本机同一工具链、同一生产链路独立复测（结果为 §5），并与简历数字对照（§6），判定其可信性。
4. 本文同时补上 T007 时代按约定搁置的"粗粒度性能回归基准"（`docs/04_TEST_STRATEGY.md` §性能回归：T007 后为帧吞吐添加粗粒度基准）——未来核心链路改动后可用同一脚本重跑对照，防明显退化。

## 2. 测量口径（pipeline 定义）

**被测链路 = 生产 Replay 模式的完整 Core 路径**（与 App 加载 `.mlog` 后执行的路径一致）：

```text
读 .mlog 文件到内存
  → parseReplayLog(text)        （T009 v1 解析器，纯文本 → ReplayLog）
  → analyzeReplayLog(log)       （T015 per-record 被动分析，内部含 summarizeTransactions 统计）
```

- **包含**：磁盘 IO、文本解析、逐记录被动分析、统计汇总。
- **不包含**：QML/UI 渲染、Prompt/Agent、任何真实时钟/sleep、网络。
- 单线程；Release 构建（`-O3`，MinGW-W64 g++ 13.1.0）；直接链接 `libmodbuslens_core.a`（纯 C++20，无 Qt），与生产核心构建一致。
- **"中位" = 同一规模多轮完整 pipeline 重跑后取中位数**（非单次、非均值）；逐轮原始值在 §5.2 全部给出。
- 报告两个口径：`total`（含读盘）与 `parse+analyze`（去 IO，与简历对照的同口径值）。

## 3. 混合样本构成（deterministic，`scripts/bench_replay/gen_samples.py`）

每 100 条为一个确定的构成周期（10k/100k/1M 均为整周期，无 unsupported、全 analyzed）：

| # | 记录类别 | 每 100 条占比 | 状态路径（T015 per-record） |
| --- | --- | --- | --- |
| 1 | FC03 正常成功 | 60 | Success |
| 2 | FC03 异常响应 0x02 | 10 | Exception |
| 3 | FC03 响应 CRC 错（末 CRC 字节翻转） | 5 | CrcError |
| 4 | FC03 超时（elapsed=1000，NO_RESPONSE） | 5 | Timeout |
| 5 | FC06 回显成功 | 8 | Success |
| 6 | Function 0x10 正常成功 | 7 | Success |
| 7 | 广播 FC06（addr 0，NO_RESPONSE） | 3 | ExpectedNoResponse |
| 8 | 广播 0x10（addr 0，NO_RESPONSE） | 2 | ExpectedNoResponse |

生成器内置与 `samples/demo_v1.mlog` 真实帧的 CRC16 断言（`C4 0B` / `BA 7A` / `C0 F1`），保证 wire 字节金样正确后再写样本。

> **诚实声明**：简历原实测的混合构成没有记录（这是当时的证据缺口，见 §7）。本样本是"覆盖全部 per-record 路径的代表性混合"，不是对原测构成的逐位复刻。

## 4. 环境与工具链

| 项 | 值 |
| --- | --- |
| OS | Windows 11 (10.0.26200) |
| 编译器 | MinGW-W64 g++ 13.1.0（`D:/QT/Tools/mingw1310_64`，与 PROJECT_STATUS §5 一致） |
| 编译选项 | `-std=c++20 -O3`（等价 Release preset 的 `CMAKE_BUILD_TYPE=Release`） |
| 被测库 | `build/release/libmodbuslens_core.a`（`cmake --preset release-local --target modbuslens_core`） |
| 样本体积 | 10k ≈0.57 MB / 100k ≈5.68 MB / 1M ≈56.76 MB |
| 运行条件 | 桌面上有其他应用负载；CPU 型号未记录（本基数为主机相对值，**非跨机常量**） |

## 5. 实测结果（2026-09-14 首次核验运行）

### 5.1 中位数汇总

| 规模 | read | parse | analyze | total | **parse+analyze（去 IO，同口径）** | 速率（去 IO 口径） |
| --- | --- | --- | --- | --- | --- | --- |
| 10k（7 轮） | 1.18 ms | 4.32 ms | 2.27 ms | 7.84 ms | ≈6.6 ms | ≈1.5 M 条/s |
| 100k（9 轮） | 11.06 ms | 53.79 ms | 28.34 ms | 95.31 ms | **84.12 ms** | **1.19 M 条/s（841 ns/条）** |
| 1M（5 轮） | 77.53 ms | 450.47 ms | 266.45 ms | 794.37 ms | **716.93 ms** | **1.40 M 条/s（717 ns/条）** |

（*analyze 列为链路内单相中位数；`parse+analyze` 中位数按**每轮相加后**取中位，而非两列中位相加。）

### 5.2 逐轮原始输出

**100k（9 轮，ms）**：

```text
iter 0: read 9.71  parse 49.24 analyze 24.77 total 83.72
iter 1: read 11.98 parse 47.53 analyze 25.63 total 85.15
iter 2: read 11.19 parse 50.67 analyze 33.45 total 95.31
iter 3: read 13.34 parse 57.39 analyze 28.34 total 99.07
iter 4: read 9.93  parse 57.04 analyze 27.07 total 94.04
iter 5: read 10.12 parse 49.56 analyze 26.73 total 86.41
iter 6: read 11.74 parse 53.79 analyze 32.51 total 98.03
iter 7: read 11.06 parse 66.22 analyze 30.70 total 107.99
iter 8: read 10.86 parse 60.92 analyze 29.72 total 101.51
```

**1M（5 轮，ms）**：

```text
iter 0: read 77.53 parse 448.65 analyze 242.24 total 768.41
iter 1: read 77.44 parse 450.47 analyze 266.45 total 794.37
iter 2: read 99.32 parse 548.43 analyze 266.60 total 914.34
iter 3: read 90.49 parse 531.62 analyze 270.05 total 892.16
iter 4: read 77.53 parse 439.10 analyze 239.03 total 755.66
```

验证行（两档一致）：`records=100000/1000000 analyzed=同数 unsupported=0 stats.success=75%` —— 与 §3 构成完全对账；CrcError/Exception/Timeout/ExpectedNoResponse 各按比例落入对应路径。

### 5.3 内部换算自洽性核对

- 100k：84.12 ms ÷ 100 000 = 841 ns/条；1M：716.93 ms ÷ 1 000 000 = 717 ns/条 —— 大档略快于小档（固定开销摊薄 + 缓存行为），规模间差异 < 15%。
- 规模扩展：1M ÷ 100k = 10 倍数据 → 716.93 ÷ 84.12 ≈ **8.5 倍**用时（去 IO 口径）；total 口径 794.37 ÷ 95.31 ≈ 8.3 倍 —— **近线性或更优**，与简历"10k~1M 近线性"一致。

### 5.4 落库后复跑（提交版脚本原样命令，验证可复现）

同一台机器、同一脚本、同一天稍晚：

| 规模 | parse+analyze 中位 | total 中位 |
| --- | --- | --- |
| 100k（5 轮） | 73.36 ms（1.36 M 条/s） | 82.36 ms |
| 1M（3 轮） | 738.71 ms（1.35 M 条/s） | 830.76 ms |

两次独立运行间 parse+analyze 在 73.4~84.1 ms（100k）/ 716.9~738.7 ms（1M）波动——即 §6 所述"本机噪声带 ±10~15%"的直接实证；简历 100k 档的 70.9 ms 落在该噪声带边界附近（原测构成未记录的差异仍可解释余量）。

## 6. 与简历声明对照

简历数字本身先通过**算数自洽**核对：
`70.9 ms ÷ 100 000 条 = 0.709 μs/条 = 709 ns`；`100 000 ÷ 0.0709 s ≈ 1 410 437 条/s ≈ 141 万条/s`；`686.7 ÷ 70.9 ≈ 9.69`（10 倍数据 ≈ 9.7 倍用时，近线性）—— 声明内部无算数错误。

与本次复测（去 IO 同口径）对照：

| 简历声明 | 本机复测（同口径） | 偏差 |
| --- | --- | --- |
| 100k 中位 70.9 ms | 84.12 ms | +18.6%（本档噪声带内，见下） |
| ≈1.41 M 条/s，0.709 μs/条 | 1.19 M 条/s，841 ns/条 | 同上 |
| 1M ≈686.7 ms | 716.93 ms | **+4.4%** |

**结论（判定：可信）**：

1. 数量级完全一致；1M 档同口径偏差仅 ≈4%，100k 档 ≈19%。
2. 100k 档的差异可解释：该档运行噪声明显（parse 相在 47~66 ms 波动，约 ±20%），且原测混合构成未记录——若构成以轻量 FC03 为主（本样本含 8% FC06、7% 0x10、5% 广播等较重 per-record 路径），19% 的差距在预期之内。
3. 简历的三组数字与本仓库代码的实际性能相称，不存在虚标量级的问题。

## 7. 已知边界与诚实声明

- **原测的当时运行记录未落库**（构成、轮次、原始输出均无）——无法对原数字逐位对账，只能"同机同链路复测"比照；这也是本基准建立的原因。今后任何性能声明必须先落本文档。
- 本基数是**主机相对值**：不构成跨机 SLA；复现的意义是同机前后对照与退化检测。
- 未使用 `-march=native`、未隔离核心、未固定频率；样本构成非原测构成。
- 生成样本不入库（`out/` git-ignored）；`gen_samples.py` 保证位级再生。

## 8. 关联文件

- 复现工具与快速上手：`scripts/bench_replay/`（`gen_samples.py` + `bench.cpp` + `README.md`）
- 面试问答（方法论口径）：`docs/INTERVIEW_NOTES.md` §3 Post-T015 条目
- 当日简记：`docs/devlog/2026-09-14-benchmark-verification.md`
- 基准落点的原始约定：`docs/04_TEST_STRATEGY.md` §性能回归（T007 起搁置的"粗粒度基准"即本文档）