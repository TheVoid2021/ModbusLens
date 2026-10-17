# R2_03 / R2_04 / R2_05 / R2_07 —— 合并说明（NOT SEPARATED）

按 R2 指令 §9 建议的 8 份文件，以下 4 份**未独立建文件**，内容已完整并入指定位置：

| 建议文件 | 状态 | 实际位置 |
|---|---|---|
| `R2_03_QML_READ_RESULT_RCA.md` | **MERGED** | `R2_08_FINAL_REPORT.md` §D（K 场景：`scanRead` + `completeReadWithBytes`，PARTIAL/UNRESOLVED） |
| `R2_04_QML_REGISTER_MAP_RCA.md` | **MERGED** | `R2_08_FINAL_REPORT.md` §E（`regUnitField` y=698.5/705.5，h=32，1000×700 溢出 30.5–37.5px；HEAD 2134 行 → WIP 2558 行） |
| `R2_05_QML_ACTIVE_PROFILE_RCA.md` | **MERGED** | `R2_08_FINAL_REPORT.md` §F（`readSemanticCell` @ CommunicationPage.qml:1039 与断言 :17532 冲突；HEAD 已含；引入提交 `f1567a5` 2026-09-26） |
| `R2_07_DIAGNOSTIC_RUNS.log` | **EQUIVALENT** | `R2_01_RESYNC.txt` 末节（`R2 NEW DIAGNOSTIC RUNS = 0`） |

## 原因

本轮 RCA 以**静态源码分析 + R1 保留日志**完成，**未新增测试执行**（§6 上限 6 次，使用 0 次）；
R2_03/04/05 的分析深度与 R2_08 的 §D/§E/§F 完全一致，独立建文件仅重复内容，故合并。

**未执行项明确标记**：本轮**未执行**任何「K 场景专项诊断」「regUnitField 布局测量」「semantic 项枚举」等新增诊断命令。
