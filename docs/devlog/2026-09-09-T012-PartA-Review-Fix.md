# Devlog 2026-09-09 — T012 Part A Review Fix: Pending is not an anomaly

## 今日工作（Part A Review 修复）

- **Review P0 定位**：`get_recent_anomalies` 原以 `status != Success` 筛选 —— 把 `Pending`（事务未完成，既非成功也非完成性失败）误算为 anomaly，违背 T011 deterministic contract。
- **修复（follow-up code/test commit `797269a`，未 amend `9921efd`/`f04106b`，Review 全程留痕）**：
  - anomaly 定义改为**显式 whitelist**：`Exception / CrcError / Timeout / ProtocolError`（本地 helper `isAnomalyStatus`，未引入新的 Failure/Anomaly enum）。
  - latest-20 语义不变且绑定 anomaly 序列：先筛 whitelist ordinal，再取后 20 条、原序返回——**尾部 Pending 不占 20 名额**（不同于"取最后 20 条 transaction 再过滤"）。
  - **RED 证据**：新测试对旧过滤逻辑运行 → A02 FAIL（Compared values are not the same）；恢复 whitelist → GREEN。
- **测试锁定**：AGENT-A02 扩充两场景——① 六状态 batch（Success/Pending/Exception/CrcError/Timeout/ProtocolError）断言 entries 恰为 4 条白名单 anomaly 且无 Pending/无 Success；② 44 条复合 batch（10 Success + 10 Pending + 21 CrcError + 尾部 3 Pending）→ entries = Crc #22..#41、total=21、truncated=true（错误的"取尾部 20 条再过滤"会从 #25 开始）。AGENT-A03 追加未知异常码 0x7E → `exception_code` 存在但 `exception_name` **缺席**（不猜）。
- **exception_name audit 结论**：实现仅对标准 0x01~0x04 做 deterministic mapping，未知码 absent/nullopt，无任何推测文案 —— 无需修改，测试已锁定。
- **Qt/Pure wording 审计**：docs 中原无 "Pure C++ / Zero Qt" 失实表述；本轮补充精确边界（T012 档案 + ADR002 + CMake 注释）："read-only deterministic tool query layer, offline and zero-network；QtCore JSON confined to the arguments / serialization adapter boundary"。
- **自动验证**：clean 全量重建 131 targets 零警告；ctest **21/21**（agent_tools 含 A01~A09 全部未删未弱化）。

## 状态

- **新 Part A LKGC candidate = `797269a`**（自动验证通过；verified LKGC 仍 `01841b1`，未推进）。
- T012 overall = IN PROGRESS；Part A = IMPLEMENTED / AWAITING REVIEW；Part B = NOT STARTED；M6 = IN PROGRESS；未 push。

## 关联档案

- 任务：[T012-agent-tools](../../tasks/T012-agent-tools.md)（Review Fix 段 + Git Commit 回填）；ADR：[ADR002](../../adr/ADR002-readonly-tool-agent-architecture.md)。