# 09 — RESUME PROJECT ENTRY（简历项目条目）

> **用途**：可直接誊入简历的项目条目 + **口径注释** + **禁止表述清单**。
> **事实来源**：只使用已验收 Fact Pack（`docs/interview/00`–`06`）；**不新增** `[V-]` 标记。
> **写法原则**：写**工程贡献与取舍**，不写功能清单；**只用当前已核实且有意义的数据**。

---

## 1. 一句话项目标题与定位

> **ModbusLens — C++20 / Qt6 的 Modbus RTU 通信分析与故障诊断桌面工具**
>
> 以**零 Qt 依赖的确定性协议分析核心**（CRC-16 / RTU 帧编解码 / FC03·FC06·FC16 语义 /
> 事务归一化与正交 Issue / 统计 / 规则诊断）为底座，让 **Simulator / Replay / Serial 三种数据源
> 复用同一分析管线**；LLM 解释与**只读** Agent 仅作为可选上层。

依据：`05` §A1–A13；`03` D1/D3；功能码范围为**有意收敛**（一等支持 FC03 读 + FC06/0x10 写，
其余显式 Unsupported）`[V-309]`。

---

## 2. 简历 bullet（3 条 + 可选第 4 条）

### Bullet 1 — 协议与事务建模（贡献：把"这一帧合法吗"推进到"这笔事务有几个独立问题"）

> 以纯 C++20 实现 Modbus RTU 分析核心——CRC-16/MODBUS **按位实现**（规范向量 KAT 对拍）、
> RTU 帧模型与 wire 编解码（`std::variant` 错误模型）、FC03/FC06/FC16 语义解码——
> 并编译为**不链接任何 Qt 的静态库**；把事务结局与协议违规事实拆成 **7 类状态 + 两组正交 Issue**，
> 使「响应成功但请求参数违规」（如 `Success + InvalidRequestByteCount + InvalidRequestLength`）
> 这类复合结果可被精确表达，且既有数据集行为**逐位不变**。

依据：`05` §A1/§A2；`03` D1（构建图证据：`modbuslens_core` 无 `target_link_libraries`）、
D7（两段式演化）。`[V-010]` `[V-196]` `[V-215]` `[V-319]`

### Bullet 2 — 被动分析重构与三数据源复用（贡献：容错与口径一致性）

> 将日志回放从**整批失败**重构为**逐记录诊断**：文本语法错误才整文件失败，
> 有效帧之后的语义违规 / 工具不支持的功能码按**单条记录**保留，
> 「不支持的正常语义」显式披露而**不伪装成错误**（`Unsupported ≠ ProtocolError`）；
> 并在 Simulator / Replay / Serial 三条数据源上复用同一批分析函数，
> 以「**Golden Replay 与 Simulator 演示批次统计快照严格相等**」的自动化断言锁定口径一致。

依据：`03` D3/D9；`01` P5 Verification（4/4/0、1/1/1/1/0、0.25、25.0 ms 严格相等）。
`[C-02]` `[V-198]` `[V-225]`

### Bullet 3 — 确定性诊断 + AI / Agent 权限边界（贡献：不把判定权交给模型）

> 实现**确定性规则诊断**（纯 enum finding + 建议检查项，不宣称物理根因、无健康评分），
> 并在此事实层之上接入 LLM 解释与 **3 个只读 Agent 工具**；
> 以**类型层白名单**（写能力在类型层面不存在）、**3 轮 / 6 次调用硬预算**与
> **双层陈旧数据守卫**约束其权限——协议事实（CRC / 状态 / 超时 / 异常码 / 统计）**只由核心判定**，
> 且无 provider 配置时三数据源 + 统计 + 基线诊断**完整可用**。

依据：`05` §A4/§A11/§C；`03` D5/D6；`grep` 写工具 0 命中。`[V-303]` `[V-304]` `[V-322]` `[V-347]`

### Bullet 4（可选）— 验证纪律与发布工程（贡献：把"能跑"变成"可证明"）

> 建立 **CTest 70 个测试目标**的验证矩阵（含本地 fake HTTP provider 与 fake serial transport，
> **零真实网络**）与**负向对照纪律**（真实 mutate → 观察 RED → 精确逆向还原 → 基线复绿），
> 并处理真实工程缺陷：以**哈希溯源**定位部署机 ABI/runtime 冲突（旧 libstdc++ 抢占 Qt 符号），
> 由此建立**独立部署目录 + minimal-PATH 启动门 + 逐文件 SHA-256 manifest**
> 的发布身份链（候选树从零重建、fail-fast 打包）。

依据：`05` §A12/§A13；`04` ISSUE-002（最强 RCA #1）。`[V-265]` `[V-354]` `[V-356]`

> **是否采用第 4 条**：取决于简历篇幅。它与前三条**维度不同**（前三条是产品与设计，
> 第 4 条是**验证与发布工程**），若版面允许建议保留——它承载了最真实的一个 RCA 故事。

---

## 3. 技术关键词（**实际使用**，来自 `02_TECH_STACK_MAP.md`）

```text
语言 / 标准   C++20（std::variant / std::optional / std::string_view / std::span；CXX_EXTENSIONS OFF）
UI            Qt 6（Quick / QML / Quick Controls 2，Fusion style）
通信          Qt SerialPort（QSerialPort / QSerialPortInfo）· Qt Network（QNetworkAccessManager）
持久化        Qt JSON（QJsonDocument / QSaveFile）——位于 Adapter 层，核心保持零 Qt
构建 / 测试   CMake ≥3.21 + Presets · CTest（70 个目标）· Qt Test 断言 · QSignalSpy
协议          Modbus RTU · CRC-16/MODBUS（按位实现，反射多项式 0xA001，低字节先上线）
              功能码 FC03 / FC06 / Function 0x10 · 异常码语义 · 广播（address 0）语义
              字节序 / 字序解码（UInt32 / Int32 / Float32）
架构          三数据源（Simulator / Replay / Serial）复用同一分析核心 · 事务归一化 + 正交 Issue
              确定性规则诊断 · 只读 Agent（native tool calling，3 工具）
第三方        PDFium（pinned）+ libzip + zlib（static，离线物化 + 配置期 hash 断言）
              —— 仅用于手册导入的本地 PDF/DOCX 抽取
发布          Windows 便携 ZIP · windeployqt · candidate 契约（从零重建 + 逐文件 SHA-256 manifest）
              · minimal-PATH 启动门
可选 AI       OpenAI-compatible Chat Completions（BYOK，环境变量配置）
```

**关键词层面的诚实边界**：不含 Modbus TCP、Modbus ASCII、installer、代码签名、CI 流水线、
数据库、Docker、MCP、RAG、vector DB、多智能体、`std::filesystem`。
（`std::filesystem` 与 `QProcess` 都在核验中被**实测排除**——`src/` 与 `tests/` 命中为 0。）
`[V-305]` `[V-306]` `[V-307]` `[V-135]` `[V-160]` `[V-396]`

---

## 4. **禁止出现在简历上的表述**（明确清单）

以下表述**证据不支持**，一旦写上即为夸大——`05` §7 已给出对照口径：

| ❌ 禁止写 | 为什么 | 可写的替代表述 |
| --- | --- | --- |
| "已在真实 PLC / 现场设备上验证" | **REAL MODBUS HARDWARE = NOT VERIFIED** | "串口读/写路径已实现并有自动化与 harness 级验证" `[V-300]` `[V-301]` |
| "完成真实串口写验证 / 已对设备成功写寄存器" | 真实串口写**未验证** | "写路径含参数校验、写前快照与确认-派发分离，并有 R15–R17 门禁覆盖" `[V-331]` |
| "支持 Modbus TCP / RTU-TCP 双协议" | **未实现**（`grep` 0 命中） | "仅 Modbus RTU" `[V-305]` |
| "Agent 可自动操作 / 下发设备" | Agent **无写 API** | "Agent 永远只读，3 个只读工具，写能力在类型层面不存在" `[V-304]` |
| "支持扫描件 PDF / OCR" | **未实现** | "手册导入支持 TXT / Markdown / PDF / DOCX（本地抽取）" `[V-341]` |
| "已在 GitHub 公开发布并验证可下载" | 发布链为**仓库留档的历史事实**；**当前远端状态未实时核验** | "发布链完成并留档（候选树契约 + 逐文件哈希 manifest + H1–H8 人工验收）" `[U-10]` |
| "支持全部 Modbus 功能码" | 功能码**有意收敛** | "一等支持 FC03 读、FC06/0x10 写；其余显式 Unsupported" `[V-309]` |
| "工业级 / 生产可用 / enterprise-grade / production-ready" | 无工业认证、无字段泛化验证 | 直接删掉限定词，只陈述实现与验证范围 `[V-391]` |
| "全链路性能优化 / 高吞吐" | 基准已建立但**按 10 帧/秒需求尚未优化** | "建立可复现的回放性能基准并定位瓶颈（解析约占三分之二）" `[V-358]` |
| "24 个测试目标"（或任何历史数字当作现状） | `docs/12` 的 24 是 **v1 历史快照** | "**CTest 70 个测试目标**（Release 配置 70/70 PASS）" `[C-05]` `[V-354]` |
| "有 CI 流水线" | **未接 CI**，验证链是本机的 | "本机验证链：configure / build / ctest / QML 门禁 / deployment gate" `[V-396]` |
| 引用 `docs/INTERVIEW_NOTES` F8 的"无 register map / 工程单位层" | 该表述**已被判定过时**（见 `[C-03]`），且同族文本仍标 `[U-11]` | 用 `README.md` §11 的当前口径：语义层**只覆盖读数呈现，不进入诊断判定** `[C-03]` |

---

## 5. 口径注解（被追问时用）

1. **"7 类状态 / 两组正交 Issue"**：对外说 **7 个状态**；Issue 说 **13 个业务 Issue**
   （响应侧 9 业务值 + 请求侧 4）。**枚举口径是 14**（响应侧 10 含 1 个防御哨兵）——
   若面试官数出 14，说明"哨兵不计入业务语义"。`[V-317]`
2. **"复用同一分析核心"**：共享的是**核心函数**；架构文档中的 `IFrameSource` 统一接口
   **从未实现**（`grep` 0 命中、`src/io`/`src/app` 不存在）。正确说法见 `[C-02]`。
3. **"Golden Replay 与 Simulator 统计严格相等"**：这是**可执行断言**，不是文字承诺；
   四个数字与演示指南一致（4/4/0 · 1/1/1/1/0 · 25% · 25 ms）。`[V-027]` `[V-198]`
4. **"3 轮 / 6 次调用"**：`rounds=3` 与 `total calls=6` 是**两个维度的预算**，
   其中 total 由 3 → 6 是**真实 Live 触发超限后经 RCA 调整**的（不是一开始就设对）。`[V-270]`
5. **"发布身份链"**：验收身份是 **exe SHA-256**，ZIP 不作为 identity criterion；
   公开资产的逐字节验证是**仓库留档的历史证据**，当前远端页面状态属 `[U-10]`。`[V-562]`
6. **推荐数字口径**（只用当前真值）：CTest 目标 **70** ／ ADR 文件 **3** ／ 任务文档 **30**。
   `[V-559]` `[V-560]`