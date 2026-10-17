# R2_06_DEBUG_RELEASE_COMPARISON — Debug / Release 对照有效性核验（**关键结论：不等价**）

## 三组证据

| 组 | EXE | 构建时间 | SHA-256 | 源码构成 |
|---|---|---|---|---|
| FROZEN RELEASE | `build/release/modbuslens.exe` | **2026-10-14 20:15** | `4044edae…` | **不含 C1/C2**（见下） |
| FROZEN DEBUG | `build/debug-h2/modbuslens.exe` | 2026-10-16 14:51 | `9cadc2c5…` | 含全部 WIP |
| NEW ISOLATED DEBUG | `build/t035c-source-repro-20261017/modbuslens.exe` | 2026-10-17 20:06 | `7a06e6f7…` | 含全部 WIP |

## 源码版本对应关系（决定性证据）

`build/release/build.ninja`（2026-10-14 12:35）中：

```
grep -c "RegisterReadiness"      → 0    ← C1 新文件未参与 Release 构建
grep -c "EngineeringReadController" → 0  ← C2 新文件未参与 Release 构建
```

`build/debug-h2/build.ninja` 与 `build/t035c-source-repro-20261017/build.ninja`：**3 / 6**（均含）

**⇒ Release 构建树的源码 = HEAD `3adaea42`（2026-10-13 23:21）时代的源码，完全不含 T035-C C1/C2 的任何 .cpp/.h/QML 改动。**

## 逐项检查点回答（§5 表格）

| 检查点 | FROZEN RELEASE vs 新 Debug | 结论 |
|---|---|---|
| 源码版本 | 不同（Release 缺 C1/C2 全部 WIP） | **不等价** |
| QML 资源 | 不同（`DeviceProfilePage.qml` 2134 行 vs 2558 行；`EngineeringPage.qml` 未含 C2 改动） | **不等价** |
| 构建宏 | Debug vs Release（`NDEBUG`、断言、优化等级不同） | 不同 |
| 测试命令 | 相同（`modbuslens --qml-*-check`） | 等价 |
| Qt 库 | 相同（`D:/QT/6.11.1/mingw_64`） | 等价 |
| 平台插件 | 相同（同一 Qt 安装） | 等价 |
| 工作目录 | 不同（`build/release` vs `build/t035c-source-repro-20261017`） | 差异 |
| 测试数据 | 门禁内部构造（相同逻辑，但代码版本不同） | **不等价** |
| 持久化状态 | 均未显式隔离（未设 APPDATA/TEMP 重定向） | 相同 |
| 断言实现 | **不同源码版本**（main.cpp 差异 +1327 行） | **不等价** |

## 结论

```
RELEASE COMPARISON = NOT EQUIVALENT
```

**因此 R1 报告中的「冻结 Release 3 项 EXIT 0 ⇒ Debug-only」推论不成立**（R1 已给出但未校验等价性）。
Release 的 PASS **只说明「旧源码（无 C1/C2）下这三个门禁通过」**，
**不能**用于证明「新源码下三个失败是无害的 Debug 现象」。

## 可确认与不可确认

- **可确认**：3 项 QML 失败**不是**「全新构建目录」或「offscreen 平台」导致（旧 Debug 同源同样失败；windows 平台同样失败）。
- **不可确认**：是否由 WIP 引入（**需要「HEAD 源码 Debug 构建」作为第三组对照**，本轮未授权重新构建 HEAD ⇒ `UNRESOLVED`）。
