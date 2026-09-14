# bench_replay — Replay 生产链路基准（复现工具）

对简历中 Replay 性能声明（10 万条中位 70.9 ms / ≈141 万条每秒 / 1M ≈686.7 ms）的独立复测工具。
口径、样本构成、原始数据与结论：**[docs/10_REPLAY_PERFORMANCE_BENCHMARK.md](../../docs/10_REPLAY_PERFORMANCE_BENCHMARK.md)**。

## 文件

| 文件 | 作用 |
| --- | --- |
| `gen_samples.py` | 生成确定性混合 `.mlog` 样本（10k / 100k / 1M）到 `out/`（git-ignored） |
| `bench.cpp` | 测量链路：读文件 → `parseReplayLog` → `analyzeReplayLog`（含统计），每轮输出分相耗时，末尾输出各相位与总链路**中位数** |
| `README.md` | 本文件 |

## 使用步骤（Windows 本机，MinGW 13.1.0 / Qt 6.11.1）

```bash
# 1) 构建 Release core（一次性；脚本只依赖纯 C++ 静态库，无需 Qt）
cmake --preset release-local
cmake --build --preset release-local --target modbuslens_core

# 2) 生成样本 + 编译 harness
cd scripts/bench_replay
python gen_samples.py
g++ -std=c++20 -O3 -I../../src bench.cpp ../../build/release/libmodbuslens_core.a -o bench.exe

# 3) 运行
./bench.exe out/sample_100k.mlog 9   # 9 轮取中位
./bench.exe out/sample_1m.mlog 5     # 5 轮取中位
```

## 注意事项（口径纪律，引用前必读）

- **中位数** = 同一规模多轮完整 pipeline 重跑后取中位（非单次、非均值）。
- 链路含读盘；报告单列 `parse+analyze`（去 IO）作为与简历对照的同口径值。
- 绝对数值是**主机相对值**：跨机比较无意义；同机前后对照用于退化检测。
- 运行时应尽量关闭其他重负载；本机噪声带实测约 ±10~20%。
- 生成样本不入库（`out/` git-ignored）；`gen_samples.py` 保证位级再生与混合构成（构成说明见 benchmark 文档 §3）。