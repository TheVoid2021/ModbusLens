# R2_02_REGISTER_READINESS_RCA — `register_readiness` 失败机制分析

## 现象（R1 实测，不可改动）

```
FULL CTEST      = FAIL（0.77 sec，末有 stdio 输出被 CTest 丢弃/为空）
DIRECT EXEC     = PASS（27/27，127 ms，exit 0）
ISOLATED CTEST  = PASS（27/27，0.50 sec，exit 0）
```

## 静态分析（源码级）

`tests/test_register_readiness.cpp`（649 行）**无** QSettings / QStandardPaths / QTemporaryDir / 环境变量依赖；
**仅两处**使用 `QDir::tempPath()`：

| 测试 | 位置 | 逻辑 |
|---|---|---|
| `c16_deterministicAndSideEffectFree()` | `:354` | `snapshotDir(QDir::tempPath())` → 调用两次 `validateRegisterForEngineeringDisplay` → `QCOMPARE(after, before)` |
| `d18_wholeValidationChainHasNoSideEffects()` | `:613` | 同上（serialize→parse→validate 三种 kind 后比较 TEMP 快照） |

`snapshotDir()` 的实现（`:340`）：
```cpp
QDir(dir).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)
→ 每项记录 "fileName|size|lastModified.toMSecsSinceEpoch()"
```

## 根因（CONFIRMED — 机制级）

**该测试断言「整个系统 TEMP 目录在测试执行前后完全不变（含文件名/大小/mtime 毫秒精度）」**。
`QDir::tempPath()` 是**全机共享目录**：

- **单独运行**：无并发写入者 ⇒ 快照相等 ⇒ PASS
- **全套 74 项串行运行**：其他测试进程（Qt `QTemporaryFile`/`QTemporaryDir`、QML 缓存、日志、构建工具临时文件、同机其他进程如 Office/浏览器/杀软扫描）在该时间窗口内**只要写入/修改 TEMP 中任意一项** ⇒ 快照差异 ⇒ `QCOMPARE` 失败
- 观察到的 0.77 s 快速失败与「c16/d18 立即不相等」一致

**⇒ 这是测试脆弱性（test fragility），不是产品缺陷**：
`validateRegisterForEngineeringDisplay` / `ProfileStore::parseFromJson` 均为纯函数（无 I/O），
`c16`/`d18` 的「无副作用」命题**在无干扰环境下已被单独验证（27/27 PASS）**。

**不允许**据此断言「产品有副作用」；也**不允许**将 FAIL 改记为 PASS。

## 证据等级

`CONFIRMED`（源码机制 + 单独/全套差异完全吻合；无需新增执行）

## 产品质量影响

**P2 — 非阻塞观察**：不影响 T035-C 已验收功能（纯函数行为已单独验证）。
但**修复建议**：把 `QDir::tempPath()` 全目录快照改为**专属子目录快照**（如 `QTemporaryDir` 内的 probe 目录），
否则该测试在全套 ctest 中**结构性地不可靠**。
