# Devlog 2026-09-09 — T012 Part B Gate 0: ModelScope Native Tool-Calling Probe（Request #1）

## 今日工作

- **Gate 0 Probe — Request #1（真实 Provider 能力验证，非 Agent Runtime）**：以最小问题 + 单工具 schema 实测 ModelScope API-Inference 是否返回标准 native tool_calls。
  - **结果：PASS 证据确凿** — HTTP 200；`finish_reason="tool_calls"`；`message.tool_calls` 恰一项 `{name:"get_session_summary", arguments:"{}", id:"call_fda63adb045c484a81392be5", type:"function"}`；`message.content=""`。**ModelScope 原生 tools→tool_calls 路径成立**（TD-4 路径 A 不需求证 Hermes 降级）。
  - sanitized request shape 与完整证据已入 [T012 档案](../../tasks/T012-agent-tools.md)（Gate 0 段）。
- **预算与事件（如实记录）**：真实请求累计 = **2**（= 本轮授权红线）。第一次运行因我自己的临时 Probe 脚本存在本地解析 bug（对字符串 payload 双重 JSON 转换）导致 provider 已 200 但证据未落盘——不掩盖，已记录；修复后重跑（脚本内 PROBE_BUDGET=1 强制上限）成功捕获证据。**Request #2（tool-result round trip）未执行**——需用户追加授权，任何人不提前发第三个请求。
- 临时脚本已删除；working tree 仅 docs 改动；零 production code / 零 tests / 零 CMake 改动。

## 状态

- verified LKGC 仍 **`797269a`**(Provider 证据不是新 code baseline,不推进)。
- T012 Part A DONE；Part B = NOT STARTED(仅 Gate 0 探测进行中,round trip 待授权)；M6 IN PROGRESS；未 push。

## 问题与决策点(供用户)

Request #2 需要 **1 次追加真实请求**;你批准后再执行(方案已在档案中,不做任何设计变更)。