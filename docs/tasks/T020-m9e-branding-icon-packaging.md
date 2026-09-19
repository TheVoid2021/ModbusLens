# T020 — M9-E Branding / Icon / Packaging

> **状态：IN PROGRESS — Phase 1 = PASS；E1 = PASS；E2 = PASS；**E3 = 实施完成；E3 Review = HOLD（fail-fast coverage gap）→ correction 已落库（9/9 runtime probes fail-closed + 口径修正），awaiting E3 Re-review**；E4 未开始。**
> 上游边界：M9-D（T019）= ✅ COMPLETE（verified LKGC `07561d9`）；M9-E **不得**重新打开 Transactions IA / selection·detail / Diagnosis / Legacy。

## 0. V2 Protocol 对应

Learning / Design Gate（本轮）→ Review → 实施阶段（E1–E4，见 §S）→ 逐阶段 Review。本轮 docs-only：**零 src/QML/CMake/scripts/assets/tests/samples/screenshots 改动**。

## 1. Preflight（2026-09-19）

HEAD `6299444`、main、clean、verified LKGC `07561d9`、`v1.0.0^{commit}`=`ae067ab`、**annotated tag object `2cee626`**（type=tag）、origin/main `a40d935`、ahead 68 / behind 0、`git diff --check` PASS —— 全部相符。

## 2. M9-D Closure Re-read（确认边界）

PROJECT_STATUS / BACKLOG / T019 Final Closure 实读确认：**M9-D = COMPLETE**；final IA = 0 Transactions / 1 Dashboard / 2 Communication / 3 Replay / 4 Diagnosis / 5 Device disabled；Legacy retired runtime absent。Tab-only focus traversal 属 **M9-F**（非 M9-E）。M9-E 不重开上述任何域。

## 3. Task Ownership（§3）

BACKLOG 已登记 M9-E 里程碑行，但**无 task document**；`docs/tasks/` 现有 T001–T019，**T020 = 真实下一个空闲 id** ⇒ 本文件即 canonical task doc。**未**把 M9-E 追加进 T019（已完成，只作历史边界引用）。

## 4. Branding Inventory（§4，真实搜索，逐类）

全仓库 `ModbusLens` 出现实测：`src/` 27 处、`CMakeLists.txt` 2、`scripts/deploy_windows.bat` 7、`docs/` 40 文件、`tests/` 2（均为注释）。分类：

| 类别 | 实例 | 动作 |
| --- | --- | --- |
| **A. user-visible product name** | `Main.qml title: qsTr("ModbusLens")`；deploy 后 `ModbusLens.exe` 文件名 | 保持 ModbusLens；呈现完整化（icon/metadata），**不改名** |
| **B. binary/package identity** | target `modbuslens`（build `modbuslens.exe`）→ deploy 拷贝为 `ModbusLens.exe`；`project(ModbusLens VERSION 0.1.0)`；windeployqt `--qmldir src/ui/qml`；qmldir URI `ModbusLens` | PE metadata 补齐（§16）；exe 名统一问题登记（见 §N4） |
| **C. internal target/project name** | `qt_add_qml_module(URI ModbusLens)`、import 语句（全部 QML 组件）、`:/ModbusLens/` qrc 路由、`setOrganizationName/ApplicationName("ModbusLens")` | **不动**（技术 identifier，改名 = 大规模破坏） |
| **D. docs-only mention** | docs/ 40 文件、README、本文件 | 随文案更新，非本轮 |
| **E. test fixture/string oracle** | `tests/test_passive_analysis.cpp` 2 处（注释）；harness 断言**无**品牌字符串依赖（deploy checklist 是文件名而非 UI 文本 oracle） | 不动 |

**结论（§4 核心问题）**：品牌**不需要改名**——需要的是把现有 ModbusLens 品牌的**呈现完整化**（identity / icon / metadata / packaging）。**禁止机械全局替换**。

## 5. App / Window Identity Audit（§5，实读 main.cpp:5553–5557 + Main.qml:51）

| property | 当前值 | source owner | visible where | packaging impact | M9-E candidate |
| --- | --- | --- | --- | --- | --- |
| `QCoreApplication::organizationName` | `"ModbusLens"` | main.cpp 硬编码 | 注册表/QSettings 路径（当前无 QSettings 使用） | 低 | 保持（真实名称，非编造） |
| `QCoreApplication::applicationName` | `"ModbusLens"` | main.cpp 硬编码 | QStandardPaths、崩溃日志 | 低 | 保持 |
| `QCoreApplication::applicationVersion` | `"0.1.0"` | **main.cpp 硬编码字符串** | About 类查询（当前 UI 无展示） | 中——**与 CMake project VERSION 双源漂移风险** | **是**：单一 source（§10） |
| `organizationDomain` | **missing**（未设置） | — | — | 低 | 不编造（无权威域名）；保持 missing |
| `applicationDisplayName` | **missing**（未设置；窗口标题由 QML title 决定） | — | 窗口标题后备 | 低 | 否（title 已显式） |
| `QGuiApplication::setWindowIcon` / `ApplicationWindow.icon` | **missing** | — | 任务栏/Alt-Tab 退回 Qt 默认图标 | **高** | **是**（§6/§14） |
| `Main.qml title` | `qsTr("ModbusLens")`（固定，无 session 后缀） | Main.qml | 标题栏/任务栏 | 低 | 保持固定（§17） |
| window flags | 默认（无特殊 flags） | Main.qml | — | 低 | 否 |
| **PE metadata（FILEVERSION/ProductName/…）** | **missing by construction**（无 `.rc`、CMake 无 RC 处理） | — | Explorer 文件属性/右键 | **高** | **是**（§16） |

## 6. Icon Audit（§6，逐层，全部实搜）

| 层 | 现状 | 来源/机制 | build-tree | deployed tree |
| --- | --- | --- | --- | --- |
| A. QML/window icon | **不存在**（Main.qml 无 icon 属性；main.cpp 无 setWindowIcon） | — | 无 | 无 |
| B. Windows taskbar/window icon | 退回 Qt 默认图标（因 A 缺失） | — | 同 A | 同 A |
| C. **PE executable icon** | **不存在**（无 `.rc` 文件、CMakeLists 无 RC/target_sources 处理；`find` 实证仓库无 .ico/.rc/.svg） | — | 无 | 无 |
| D. deploy folder asset | 无 icon 资产（deployed tree 实测只有 exe/DLL/QML 目录/samples） | — | — | 无 |
| E. future installer icon | N/A（无 installer，§20） | — | — | — |
| F. docs/screenshots logo | 无产品 logo（`docs/assets/` 只有截图与架构图） | — | — | — |

**明确区分**：QML `Image` 元素与 PE executable icon 是**两个不同机制**（前者是运行时图片资源，后者是 Windows 资源段嵌入）；当前两者都不存在，M9-E 需分别实现且不可混同（§28 的验证 oracle 也因此分立）。

## 7. Existing Resource Mechanism（§7）

| resource type | 机制（实读 CMakeLists/main.cpp） | build embedding | deploy dependency | platform-specific |
| --- | --- | --- | --- | --- |
| QML 模块 | `qt_add_qml_module(modbuslens URI ModbusLens VERSION 1.0)`（exe 附着 qrc，`:/ModbusLens/` 路由；qmldir 自动生成） | 是（qrc 内嵌） | windeployqt 拷出 `ModbusLens/` 目录 | 否 |
| DesignSystem tokens | **engine context property**（main.cpp 从 qrc URL 实例化；ISSUE-010：不走 module singleton） | 是 | 同上 | 否 |
| 测试 fixtures | `configure_file(... COPYONLY)` → build tree `test_data/`（**不进 deploy**） | 否（文件拷贝） | 无 | 否 |
| deploy 资产 | `scripts/deploy_windows.bat`：windeployqt + 手写 checklist（xcopy qmldir/QML 树 + `samples\demo_v1.mlog`） | 否 | 是（checklist 驱动） | 是（Windows 批处理） |
| `.rc` / qrc / qt_add_resources | **均不存在** | — | — | — |

**结论**：icon/PE metadata 应**复用现有机制**——PE icon/metadata 走 CMake 的 RC 编译（qt_add_executable 对 .rc 的标准集成）；窗口 icon 走 qrc（可加入 qt_add_qml_module 的 RESOURCES 或最小 qt_add_resources）。**不为一个 icon 建第二套资源管线**。

## 8. Packaging Baseline（§8，实读 deploy_windows.bat + deployed tree 实测）

当前真实 packaging = **A. portable deployed folder**（`build/deploy/`：ModbusLens.exe + Qt runtime DLL + QML 模块目录 + platforms 插件 + samples/demo_v1.mlog）。**无 zip、无 installer、无 MSIX/MSI/NSIS/Inno**（scripts/ 只有 deploy_windows.bat 与 bench_replay）。deploy 由 windeployqt + 手写 checklist 驱动，checklist **只含 `samples\demo_v1.mlog`**（t014/t015 fixtures 实测不在 deployed tree）。**如实声明：current packaging baseline = portable Windows deployment**；milestone 叫 Packaging **不等于**要做 installer（§20）。

## 9. Publication vs Development Boundary（§9，冻结）

local main ahead 68 ≠ published release。M9-E **不得**：push、移动 v1.0.0、自动创建 release tag、发布 GitHub Release、上传 installer/package。Packaging implementation 只产出**本地可验证 artifact**；publication 需显式授权（另行决策）。

## 10. Version Source Audit（§10）

| 来源 | 现状 |
| --- | --- |
| CMake `project(ModbusLens VERSION 0.1.0)` | 存在（也是 QML module VERSION 1.0 的邻居，但两者语义不同） |
| main.cpp `setApplicationVersion("0.1.0")` | 存在（**硬编码字符串**） |
| Windows resource VERSIONINFO | 不存在 |
| Git tag 驱动 | 不存在 |

**双源漂移风险**：CMake VERSION 与硬编码字符串目前巧合一致（0.1.0），无机制保证。**裁定：单一 source of truth = CMake project VERSION**——implementation 时用 `configure_file` 生成版本头（或 .rc 直读 CMake 变量），`setApplicationVersion` 与 PE FILEVERSION/PRODUCTVERSION 均从其派生。**禁止运行时 `git describe` 作为 deployed app 唯一版本来源**（deployed tree 无 .git；除非设计论证否则不引入）。

## 11. V1 Tag Boundary（§11，冻结）

`v1.0.0` = annotated tag，tag object `2cee626`，commit target `ae067ab`，**永不移动**。M9-E 的版本信息**不得**通过移动/重打 v1.0.0 实现；未来如需新版本号，创建**新** tag 属 publication 决策（§9），非 M9-E。

## 12. Branding Goal Definition（§12，优先级排序）

| 目标 | 优先级 | 理由 |
| --- | --- | --- |
| **A. 应用识别**（窗口/任务栏/exe 看得出是 ModbusLens） | **P0** | 当前三层 icon 全缺，任务栏是 Qt 默认图——识别缺口最刺眼 |
| **B. 一致性**（窗口标题、文件属性、包名一致，版本单源） | **P0** | 双源漂移风险；Explorer 属性当前全空 |
| **C. 专业交付**（部署包无需开发环境即可辨认和运行） | **P1** | portable 包已可运行；缺的是可辨认性（icon/metadata）与可选 zip |
| **D. 视觉 branding**（logo/icon 风格统一） | **P1**（随 A 交付单枚 icon，不做完整 VI） | 避免范围膨胀 |
| **E. release publication** | **REJECT for M9-E**（§9 冻结） | 需显式授权 |

## 13. Icon Design Requirements（§13，仅规范，本轮不生成资产）

**应传达**：Modbus（串口/寄存器网格语义）、diagnostic（状态/信号感）、lens/inspection（放大/聚焦母题）、industrial/tooling（克制、几何、工程感）。
**应避免**：复杂细节、文字元素、小尺寸不可读、仿真特定厂商品牌（Modbus logo/厂商商标）。
**规范**：单一几何母题（lens + 寄存器网格/信号线，≤3 个形状）；高对比（深底/浅底均可辨）；小尺寸可辨（16px 下母题仍可识别——以形状轮廓为先，细节在后）；透明背景；**monochrome tolerance**（单色剪影下仍可辨）；无浅色/深色主题依赖（单 icon 适配双色底，§30）。

## 14. Icon Size / Format Plan（§14）

| 尺寸 | 用途 | 是否需要 |
| --- | --- | --- |
| 16 | 窗口标题栏/小任务栏 | **是** |
| 24 | Alt-Tab/部分 DPI | **是** |
| 32 | 任务栏/桌面小图标 | **是** |
| 48 | Explorer 中图标/Alt-Tab 大图 | **是** |
| 64 | 高 DPI 任务栏 | **是** |
| 128 | Explorer 特大视图/关于页（可选） | 可选（低价值，默认不含） |
| 256 | Explorer 超大视图/现代 Windows 缩放 | **是**（ICO 内含） |

**source master format = SVG**（矢量母版）；**derived = multi-resolution .ico（16/24/32/48/64/256 一枚多帧）+ 窗口图标用 PNG 帧（qrc 内嵌 16/32/48 或直接 QIcon 从 ICO 读取）**。**禁止**把单张 256 PNG 改名 .ico（不是 multi-resolution 结构，Explorer/任务栏缩放质量差）。**implementation tool requirement**：仓库当前无 ICO 生成工具链——需 ImageMagick `magick` 或 `icotool`（icoutils）之一，Phase 1 标记为 **tool requirement**（implementation 前确认本机可用性；若不可用则先解决工具，不手写 ICO 结构）。

## 15. Source Asset Ownership（§15）

裁定：**新建顶层 `assets/`**（仓库现状：无 assets/resources/src/ui/assets；`docs/assets/` 是文档图像，**不**作 canonical product asset）。结构：`assets/brand/icon.svg`（source artwork，唯一手写母版）+ `assets/brand/generated/`（.ico/派生 PNG，构建或工具生成，**不入 git 或入 git 由 implementation 阶段按可复现性裁定**——默认生成物入库以保证无工具环境可构建，风险与权衡届时记录）。

## 16. Windows PE Metadata（§16）

现状：全缺（无 .rc）。M9-E **应加入**（B 类一致性目标），字段与真实来源：

| 字段 | 值 | 来源 |
| --- | --- | --- |
| FILEVERSION / PRODUCTVERSION | `0.1.0.0`（随版本演进） | **CMake project VERSION**（configure_file 派生，§10） |
| FileDescription | `ModbusLens` | 产品名（A 类事实） |
| ProductName | `ModbusLens` | 产品名 |
| OriginalFilename | `ModbusLens.exe` | deploy 名（B 类事实） |
| CompanyName / LegalCopyright / organizationDomain | **不设置** | **仓库无权威信息——不编造法人名称/版权主体/域名**（§16 红线） |

机制：新增 `src/app.rc`（或 assets 下）经 CMake 加入 target（qt_add_executable 自动编译 .rc）——**复用现有构建机制，不引入第二资源管线**。

## 17. Window Title Policy（§17）

**裁定：保持固定 `ModbusLens`**（现状已如此）。否决 `ModbusLens — <session/source>` 后缀：信息价值低（标题栏已有页面内容、Replay 页内有 sourceLabel）、隐私（文件名进标题栏/截图/任务栏 tooltip）、长度风险、source ownership（标题属 Main.qml 静态品牌，session 属 Controller——两轴耦合后每次 load 都要更新 title，无对应需求）。**B4 冻结重申：sourceLabel 仅 basename，绝不把绝对 Replay path 放进 window title。**

## 18. Package Naming（§18）

候选裁定：`ModbusLens-<version>-windows-x64`。前置确认（implementation 首步）：architecture 从 **CMakeCache/编译器三元组实测**（当前工具链 mingw1310_64 为 64-bit，但不能硬编码——build config 未在本 Phase 验证），toolchain 标记 `mingw`（`ModbusLens-0.1.0-windows-x64-mingw` 备选，若实现阶段判定 runtime 可辨认性需要）。三口径：**zip 名 = folder 名 = 上述**；**exe 名 = `ModbusLens.exe`**（deploy 现状，保持）。version 来自单一 source（§10）。

## 19. Portable ZIP Decision（§19）

比较：**A. 继续 deployed folder only**（零新增，但"交付"仍是手工目录）；**B. 增加 deterministic zip**（folder → 单文件 artifact + manifest/checklist 校验；交付/归档/演示可移植）；**C. installer**（§20 否）。**推荐 B**：D5/D6 人工交付已证明"需要把 candidate 交给别人跑"的场景真实存在，zip 是最小增量（PowerShell `Compress-Archive` 或 CMake archive，无新框架依赖），且 verification 可机器审计（解压→运行→checklist）。zip 步骤**确定性**要求：固定输入树（deploy checklist 输出）+ 固定命名，不做时间戳嵌入。

## 20. Installer Decision（§20）

**DEFER（REJECT for M9-E）**。理由：无 uninstall/Start Menu/desktop shortcut/registry/权限/code signing/升级路径需求，且会引入 installer framework 依赖（NSIS/Inno/MSIX）与显著 scope expansion——"更专业"不是评分项。触发条件（未来重启的门槛）：真实分发对象需要安装体验/卸载/快捷方式时，另行立项。

## 21. Code Signing Boundary（§21）

审计结果：**无证书、无 signing pipeline、无 signtool 集成、无 CI secret**（仓库实测）。⇒ M9-E **不得**伪造 "signed package"；artifact 定性为 **unsigned local artifact**，可接受（本地验证/演示用途）。未来若需签名 = 单独 release/security workflow（含证书管理），不在 docs 承诺。

## 22. Packaging Contents（§22，基于真实 deploy tree）

**应包含**：`ModbusLens.exe`；Qt runtime（windeployqt 输出：Core/Gui/Qml/Quick/QuickControls2/Network/SerialPort DLL + D3Dcompiler 等）；QML 模块目录（`ModbusLens/qmldir` + 镜像 QML 树）；platforms 插件；`samples/demo_v1.mlog`（§23）；（新增）license/readme 说明文件——**内容属 implementation 阶段撰写**。
**不得包含**：build intermediates、PDB（无明确 debug package 需求）、temporary harness output（`build/d3_*` 等脚本/日志）、credentials/token、user config、absolute paths、AI provider secret。**negative scan 进验证计划**（§36）。

## 23. Sample-data Policy（§23）

实测：deployed tree **只含 `demo_v1.mlog`**（用户演示 sample，A 类）——t014/t015 系 build-tree 测试 fixture（`configure_file` 注入，B/C 类），**当前就不在 deploy checklist 里**。**裁定维持现状**：package 只含 `demo_v1.mlog`；`t014_protocol_error.mlog` / `t015_broadcast.mlog` = **internal regression fixture，非 user-facing**（它们是"错误/广播"语义的测试输入，作为用户演示内容反而误导）。不因 deploy checklist 结构而默认"所有 sample 都是产品内容"。

## 24. Credential / Config Boundary（§24，冻结）

package **不得包含** user credential / token / local settings / developer config / absolute path / AI provider secret。`aiConfigured=true` 是**运行时环境状态**（用户机器的环境变量/配置），**不等于** package 含 token——当前 token 来自运行环境而非仓库文件（T011 边界）。M9-E 验证计划必须含 **negative scan**：deploy/package 树 grep token/secret 模式 + 无配置文件审计 + 无绝对路径审计。

## 25. Build-config Decision（§25）

当前长期验证用 **debug-local**（MinGW Debug）。**裁定：M9-E packaging candidate 采用 Release**（`release-local` preset 或等价）。理由：交付物体积/无调试运行时依赖/启动与运行性能/Explorer 元数据的专业一致性；Debug 包含调试符号与断言行为，不适合作为交付 artifact。RelWithDebInfo 否决（符号对 portable 演示无价值、体积大）。**Debug accepted ≠ Release accepted**——Release candidate 必须重跑全部关键门禁（§26/§36）。Qt runtime 随 Release 构建由 windeployqt 自动切换 Release DLL。

## 26. Release-behavior Equivalence（§26）

Release candidate **重新跑**：build、full ctest（Release 构建目录）、qml_smoke、qml_nav、qml_geometry、deploy、strict minimal-PATH、screenshot/evidence（如视觉口径需要）、package 检查。**禁止**只编译 Release 然后复用 Debug 截图声称包装通过。已有先例风险：offscreen/平台插件差异（D6 RCA①）在 Release deploy 同样适用。

## 27. Existing Perf Boundary（§27）

既有公开性能 = offline Replay/core analysis 吞吐（100k/1M 记录基准，`docs/10_REPLAY_PERFORMANCE_BENCHMARK.md`）。M9-E **不得**把 package size、installer 时长、startup time 与该吞吐指标混同；如需 startup/package 数据，另行口径记录，不入 perf 基准文档。

## 28. Startup Branding Verification（§28，oracle 分层）

| 验证项 | oracle |
| --- | --- |
| window title | QML/harness 读取（已有能力） |
| window icon | 需窗口级取证（QQuickWindow::icon / 截图 titlebar 人工确认） |
| taskbar/Alt-Tab icon | **人工**（Windows shell 行为，offscreen 不可证） |
| **PE exe icon** | **PE 资源 inspection**（解析 .rsrc 段/RT_GROUP_ICON）或 Windows Explorer 人工——**QML screenshot 不能证明 PE icon embedding** |
| Windows file metadata | PE VS_VERSION_INFO inspection（同上） |

## 29. Icon Visual Verification（§29，未来人工清单）

16/24/32 小尺寸、taskbar、Alt-Tab、窗口 titlebar、Explorer 大图标、high-DPI 缩放、light/dark 背景。标准：不糊、不裁切、无透明边异常、小尺寸可辨。

## 30. Theme Compatibility（§30）

单 icon 方案（§13：无主题依赖设计）；**不**做 adaptive variants（无需求信号）；**不**为图标改 DesignSystem/全局色板——M9-E branding ≠ UI theme redesign。

## 31. AppBar / SessionChip Boundary（§31）

M9-C deferred：SessionChip/AppBar refinement。**裁定：A——不改 AppBar**，仅 system-level icon/title/package。理由：窗口/任务栏/Explorer 层已完整解决识别；AppBar 加 logo 会与标题栏品牌重复（双 logo），且触碰已冻结的 Dashboard/AppBar 布局（回归面大收益小）。C（header redesign）默认否决。若未来 Review 判定需要品牌 mark，另行立项。

## 32. StatisticsOverview Zero-consumer Decision（§32）

consumer=0 但保留（M9-D closure 裁定）。M9-E 只在审计 CMake/deploy 时**保持其 registration 与 deploy guard 原样**；**不顺手删**。component cleanup 不是 Branding/Icon/Packaging 的一部分——继续 DEFER，除非 BACKLOG 另行 task。

## 33. Accessibility Boundary（§33）

M9-F 已登记全局 accessibility / Tab focus-chain audit——**M9-E 不做**全局 focus order / 键盘导航 redesign。icon 无文字 ⇒ system-level branding 不替代可访问 app name（window title/applicationName 保持文字形态，已满足）。

## 34. Alternatives Scorecard（§34，三案）

| 维度 | A. Minimal Branding | B. Portable Release Package | C. Installer Package |
| --- | --- | --- | --- |
| user value | 识别/一致性达成 | + 单文件交付物（可发人/可归档） | + 安装/卸载体验 |
| repo/tooling fit | 高（.rc + qrc + 现有 deploy） | 高（+zip 一步，无框架） | 低（新 framework 依赖） |
| new dependency | ICO 工具（§14） | 同 A + zip 工具（系统自带） | installer 工具链 |
| release risk | 低 | 低（zip 确定性可控） | 中（安装语义/卸载/注册表） |
| verification burden | 低 | 中（package 审计 + negative scan + 解压运行） | 高（安装/卸载/升级全矩阵） |
| rollbackability | 高 | 高 | 低 |
| signing implications | 无 | 无（unsigned local） | 强（签名缺失更刺眼） |
| M9-E scope fit | **完全** | **完全** | 超界（§20 DEFER） |

**推荐：B**（A 的超集：Release config + deterministic zip + manifest/check）；C = DEFER。

## 35. NOW / DEFER / REJECT Table（§35，每项带理由）

| 项 | 裁定 | 理由 |
| --- | --- | --- |
| app/window title（固定 ModbusLens） | **NOW（保持现状，无代码改动）** | 已正确；§17 否决后缀 |
| window icon | **NOW** | A 类 P0 缺口；qrc/QGuiApplication setWindowIcon |
| PE icon（.rc + multi-size .ico） | **NOW** | A/B 类 P0 缺口；复用 CMake RC 机制 |
| PE metadata（FILEVERSION/ProductName/…） | **NOW** | B 类一致性；真实来源字段（§16），不编造 CompanyName/Copyright |
| version 单一 source（CMake VERSION → configure_file 派生） | **NOW** | 消除双源漂移（§10） |
| portable deploy（现状） | **NOW（保持）** | 已存在，§8 |
| zip（deterministic，B 案） | **NOW** | 交付价值真实（D5/D6 人工交付先例）；增量最小（§19） |
| installer | **DEFER** | 无卸载/快捷方式/注册表/签名需求；显著 scope expansion（§20） |
| code signing | **REJECT for M9-E** | 无证书/管线/secret；unsigned local artifact 定性（§21） |
| AppBar logo | **REJECT for M9-E** | A 案足够；双 logo + 布局回归面（§31） |
| sample packaging | **NOW（维持现状：仅 demo_v1.mlog）** | §23 实测与裁定 |
| release config（Release candidate） | **NOW** | §25 裁定；Debug accepted ≠ Release accepted |
| StatisticsOverview cleanup | **DEFER** | 非 branding 范围（§32） |
| publication（push/release/上传） | **REJECT for M9-E** | §9 冻结 |

## 36. Verification Plan（§36，implementation 阶段执行）

build（选定 config）→ full ctest（Release 构建目录）→ qml_smoke / qml_nav / qml_geometry → `git diff --check` → deploy → strict minimal-PATH → **package tree audit**（checklist 逐项）→ **negative secret/config scan**（token/secret/绝对路径模式 grep + 无配置文件）→ QML/module presence（qmldir/QML 树）→ **StatisticsOverview retained** → **PE icon/resource inspection**（.rsrc/RT_GROUP_ICON/RT_VERSION）→ **version metadata verification**（VS_VERSION_INFO 值 == 单一 source）→ **artifact naming verification** → **package extraction/run verification**（解压到新目录 + 严格最小 PATH 运行）。若 Release：以上关键门禁在 Release candidate 重跑。

## 37. Screenshot / Evidence Plan（§37）

重点**不是**再拍所有页面：A. window/titlebar + taskbar icon（人工）；B. Explorer exe icon/metadata（人工 + PE inspection 输出）；C. deployed package tree（机器审计输出）；D. optional About/branding surface（**仅若** Phase 1 Review 批准该 surface 存在——当前设计无 About 页）。产品页回归截图取最小集。**不用 screenshot 证明** PE metadata / secret absence / package completeness（各自有机器/人工 oracle，§28）。

## 38. Manual Acceptance Plan（§38）

app name 一致 / window icon / taskbar icon / Alt-Tab icon / Explorer exe icon / high-DPI / clean extraction + run（新目录）/ package 无开发环境依赖 / package 内文件合理 / samples 可理解 / 无 credential / existing UI 无 branding regression。若有 zip：解压新目录运行测试。

## 39. Implementation Sequencing（§39）

- **E1. identity/version/resource contract**：version 单源（configure_file）+ `.rc`（PE metadata + icon 引用）+ window icon 嵌入（qrc PNG/ICO）+ harness 增 PE/version 断言。runnable/testable/rollbackable。
- **E2. icon asset pipeline**：SVG master → multi-size .ico/PNG 生成（工具链确认）+ 各层接入验证 + 人工 icon 清单（§29）。
- **E3. Release packaging**：Release-local 构建 + deploy + deterministic zip + package audit + negative scan（若 Review 砍 zip，则 E3 退化为 Release deploy 校验）。
- **E4. evidence/manual candidate**：证据集（§37）+ 人工清单（§38）。
- installer DEFER ⇒ **不创建 installer stage**。zip 若被 Review 否决则并入 E3，不单列。

## 40. Knowledge Questions（§40 必答）

1. **window icon 与 PE executable icon 为什么不是同一件事？** 前者是运行时窗口/任务栏图标（QGuiApplication/QML window icon，可来自 qrc），由窗口管理器在运行时使用；后者是 Windows 资源段（.rc 编译进 exe 的 RT_GROUP_ICON），Explorer/安装器在不运行程序时读取。一个管"跑起来后"，一个管"文件本身"；嵌入手段（qrc vs .rc）、验证 oracle（窗口取证 vs PE inspection）、缺失后果都不同。
2. **为什么 milestone 名含 Packaging 不等于必须做 installer？** Packaging 的本质是"产出可交付的本地 artifact"——当前真实 baseline 是 portable folder（§8 实证），它已经是可交付形态；installer 解决的是安装/卸载/快捷方式/升级等**分发体验**问题，而这些问题在本项目无需求信号。按名义造 installer = scope 自膨胀（§20 判据逐条否决）。
3. **当前 authoritative version 来自哪里？** 双源：CMake `project(VERSION 0.1.0)` 与 main.cpp 硬编码 `setApplicationVersion("0.1.0")`——目前巧合一致、无机制绑定。裁定收敛为 CMake VERSION 单源（configure_file 派生）。
4. **为什么 v1.0.0 不能"跟着开发移动"？** 它是 V1 主线完成时点的**历史锚**（tag object `2cee626` → commit `ae067ab`），PROJECT_STATUS 的追溯与对比都依赖它不动；移动它等于篡改历史（Git 政策同理）。新版本号 = 新 tag（publication 决策），不是改旧 tag。
5. **为什么 packaging candidate 不能继续默认 Debug 而不做决策？** Debug accepted ≠ Release accepted：不同的 Qt runtime DLL、断言/优化行为、体积与符号；把长期 debug-local deploy 当正式交付，等于从未验证过真正要交付的那个二进制。决策必须显式（本轮裁定 Release + 全门禁重跑）。
6. **为什么 screenshot 不能证明 PE metadata？** 截图拍的是**运行中的窗口内容**；PE icon/VERSIONINFO 是**文件资源段**，在不运行程序时由 Explorer/PE 解析器读取。两者机制、读取者、时机都不同——PE 层必须用资源 inspection 或 Explorer 人工取证。
7. **为什么零 consumer 的 StatisticsOverview 不能在 M9-E 顺手删除？** M9-D closure 已把它定性为"zero-consumer but kept、去留另行 ownership decision"；M9-E 的授权是 branding/packaging，删除组件是另一类变更（涉及 QML module contract 与 deploy checklist 双侧），顺手删 = 未授权 scope + 破坏"extend, do not silently redefine"。
8. **为什么 package 必须做 credential/config negative scan？** AI/Agent 能力让运行时环境里存在 provider token（T011 边界：token 在环境不在仓库）；打包是"目录快照"动作，最容易把开发机局部状态（配置/日志/绝对路径）带进去。`aiConfigured=true` 只说明运行时配了 token，恰恰提示打包时要防环境泄漏——negative scan 是唯一机器可证的防线。
9. **哪些工作留给 M9-F / 正式 release pipeline？** M9-F：全局 accessibility / Tab focus-chain audit、最终人工视觉验收。release pipeline：push、新版本 tag、GitHub Release、签名、上传 artifact——全部需显式授权，M9-E 只产本地 artifact。

## 41. Documentation（§41）

本文件即 canonical task doc；PROJECT_STATUS / BACKLOG / devlog / INTERVIEW_NOTES 同步（本轮）。状态：**M9-E IN PROGRESS，Phase = Learning / Design Gate，Implementation = NOT STARTED**。

## 42. Allowed Changes（§42，本轮）

docs-only：`docs/tasks/T020-*.md`（新建）、PROJECT_STATUS、BACKLOG、devlog、INTERVIEW_NOTES。**零** src/QML/CMakeLists/scripts/assets/resources/tests/samples/screenshots 改动。

## 43. Git / LKGC（§43）

独立 docs-only commit（建议 `M9-E: design branding icon and packaging`）；**不 amend `6299444`**、不 rebase、不 push；**verified LKGC 继续 = `07561d9`**（docs-only 不推进）。

## Phase 1 Review = HOLD + Correction（2026-09-19，append-only）

> **M9-E Phase 1 Review = HOLD**。P0 blocker：**product/package version authority unresolved**——Phase 1 原设计把 CMake VERSION 直接作为 applicationVersion/PE/package version，但仓库已存在 immutable 历史发布 tag `v1.0.0`，版本语义必须先解释清楚。本轮**只定 policy，不做 version bump**（不改 project VERSION / applicationVersion / tag / PE resource 任何值）。E1 在本 correction re-review 通过前不开始。

### C1. Version Evidence Audit（§2，全仓库实搜）

| 证据 | 实测结果 |
| --- | --- |
| `v1.0.0` tag 注释 | **"ModbusLens v1.0.0 verified product baseline"** → commit `ae067ab`（T015 Part C / Gate 0 V1 冻结点） |
| CMake `project(VERSION 0.1.0)` 首次出现 | T001（`aa337f6`，2026-09-05 项目引导） |
| v1.0.0 时点的 CMake VERSION | **仍是 0.1.0**（`git show ae067ab:CMakeLists.txt` 实测） |
| CMakeLists.txt 全历史 VERSION 变更 | **零次**（T001 引入后从未修改） |
| PROJECT_STATUS "当前版本" 行 | "**0.1.0**（2026-09-05，T001 建立；T001.1 未改代码，版本不变）"——镜像 CMake，同样未随 v1.0.0 演进 |
| 下一个 product/package version 的定义 | **UNRESOLVED**——全仓库（docs/11_V2_UPGRADE_PLAN / charter / BACKLOG / 任务文档）**无任何** 1.0.1 / 2.0.0 / 0.1.0-dev 或其它下一版本号定义 |

**四问回答**：
- **A. v1.0.0 是正式 product release version 吗？** **是**——annotated 注释自证 "verified product baseline"，且是 Gate 0 冻结的 V1 发布锚点。
- **B. CMake VERSION 0.1.0 在 v1.0.0 前后是否一直如此？** **是**——T001 引入后零变更（含 v1.0.0 时点与 HEAD）。
- **C. 它是 stale product version 还是另有技术语义？** **stale product version**——它是 T001 引导期的默认值，产品演进到 v1.0.0 时从未同步；无任何文档为其定义独立技术语义。
- **D. 仓库是否已定义 next version？** **UNRESOLVED**（如上）。

### C2. Version Authority Decision（§3，Model A + 值 UNRESOLVED）

- **裁定 Model A**：`project(VERSION ...)` **就是** public product-version authority（机制层面唯一）。
- **但当前值 0.1.0 = stale（UNRESOLVED）**：与 immutable `v1.0.0` 基线矛盾。**实际版本值（bump 到 1.0.x / 2.x / 其它）= 明确的 release decision，只能由用户做出——本 correction 不选择、不暗示**。
- 派生关系（机制冻结）：`applicationVersion`、PE FileVersion/ProductVersion（字符串与数值）、package/zip 文件名版本段——**全部从 authority 派生，禁止第二个 hard-coded version literal**。
- **禁止**：git describe 运行时依赖；tag moving；新 hard-coded literal。
- **显式记录的 open fact**：tag（1.0.0）与 CMake VERSION（0.1.0）的 mismatch 将持续存在，直到用户做出 release decision——E1 携带机制落地时**保持现值 0.1.0 原样**（不做 bump），mismatch 作为已知状态入档。

### C3. PE Numeric Mapping（§4，冻结）

authoritative version `X.Y.Z` ⇒ PE numeric **`X,Y,Z,0`**；PE FileVersion string = **`X.Y.Z`**；PE ProductVersion string = **`X.Y.Z`**。第四段 = 固定 `0`（derive padding）。**无独立 tweak source**（若未来真实 policy 需要第四段，按事实另行裁定）。

### C4. Do Not Bump（§5，证明）

本轮 diff 仅 docs（T020/PROJECT_STATUS/BACKLOG/devlog/INTERVIEW_NOTES）；`git status` 证实 src/CMakeLists/scripts 零改动；tag 未动。**具体版本值修改属后续 approved implementation/release decision**。

### C5. Legal Artifact Audit（§6）

`LICENSE*` / `COPYING*` / `NOTICE*` / `COPYRIGHT*` / `AUTHORS*`：**全仓库不存在**（find 实证）；charter/README 无 license 表述。⇒ license = **missing**，且**不生成**（选择 license 是法律决策，需用户输入）。package contents 修正：authoritative license 存在则随包原样分发；不存在则**不放占位/自造文件**。README/package notes 可后续撰写，**不能替代法律 license**。CompanyName / LegalCopyright 继续 **omitted**（除非未来得到权威来源）。

### C6. applicationDisplayName（§7）

**NOW / E1**：`applicationDisplayName = "ModbusLens"`（来源：现有 product name，无编造）。同时冻结：`applicationName` / `organizationName` 本阶段不改语义；`organizationDomain` 继续 **missing**（不编造）。已补入 NOW/DEFER/REJECT 表。

### C7. ZIP Contract（§8，术语修正 + 裁定）

**术语修正**：脚本生成 zip ≠ deterministic。**裁定：A. scripted portable ZIP**——当前工具链（PowerShell `Compress-Archive`）**不能保证** byte-reproducibility（条目顺序/时间戳/机器元数据）。**B. byte-reproducible ZIP**（stable entry ordering + normalized timestamps + no machine-specific metadata + same-tree two-run SHA256 equality）作为**后续增强**登记；若未来实施，其 verification contract 按上述四条执行。原 §19 中"deterministic"措辞由本条替代。

### C8. Asset Ownership Contract（§9，冻结）

- `assets/brand/icon.svg` = **canonical source**（committed）。
- `assets/brand/windows/ModbusLens.ico` = **committed derived Windows product asset**。
- **normal build 不得依赖 ImageMagick/icotool**（构建可复现性不绑定可选工具）。
- **E2 asset-generation step**：先探测实际工具（`magick` / `icotool` / 仓库已有工具）→ 选定一个 → 记录 **tool + version + 固定 generation command** → 验证 ICO 内含 sizes（16/24/32/48/64/256）。**不得 silent fallback**（工具缺失 = 停止并报告，不悄悄换法）。
- temporary PNG/previews：**build/ only**（不入 assets/、不入 git）。

### C9. Installer / Signing Terminology（§10，统一措辞）

- **Installer：DEFERRED / NOT IN M9-E IMPLEMENTATION**（原"REJECT"措辞废弃——那是范围裁定，不是永久产品否决）。
- **Code signing：DEFERRED TO RELEASE/SECURITY WORKFLOW / NOT IN M9-E IMPLEMENTATION**（同上；unsigned local artifact 定性不变）。
- **Publication：NOT AUTHORIZED**（不变）。

### C10. Release Decision（§11，保留）

packaging candidate = **Release** 不变；Debug acceptance 不能替代 Release acceptance。E3 至少重跑：build / ctest / qml_smoke / qml_nav / qml_geometry / deploy / strict minimal-PATH / **package extraction + run** / 必要 branding evidence。

### C11. E1–E4 Sequencing（§12，保留 + 门禁）

E1 identity/version/PE metadata contract → E2 icon asset + window/PE integration → E3 Release deploy + portable ZIP/package checks → E4 evidence/manual candidate。**E1 开始前必须通过本 correction re-review**；**E1 不生成 icon**；E1 落地 version 机制时**携带现值 0.1.0 不变**（C2）。

### C12. 受影响的 Phase 1 原文修正索引

| Phase 1 原文 | 修正 |
| --- | --- |
| §10 version 单源 | 机制保留；**新增：当前值 stale/UNRESOLVED + mismatch 入档（C2）** |
| §16 PE metadata 表 FILEVERSION "0.1.0.0（随版本演进）" | 映射冻结为 C3；**值仍 0.1.0.0（不 bump）** |
| §18 package naming | 版本段来自 authority；**当前值 = 0.1.0（UNRESOLVED）** |
| §19 "deterministic zip" | 改称 **scripted portable ZIP**（C7）；byte-reproducible 为后续增强 |
| §21 "Code signing REJECT for M9-E" | **DEFERRED TO RELEASE/SECURITY WORKFLOW**（C9） |
| §20 installer "DEFER（REJECT for M9-E）" | **DEFERRED / NOT IN M9-E IMPLEMENTATION**（C9） |
| §22 contents "license/readme 说明文件" | **无 authoritative license ⇒ 不生成 license**（C5）；readme/notes 可后续撰写 |
| §35 表 Code signing 行 | 措辞改 DEFERRED（C9） |
| §40 Q3 答案 | 补充 C2 的 stale/UNRESOLVED 事实 |
| NOW/DEFER/REJECT 表 | **补入 applicationDisplayName = NOW/E1**（C6） |

### C13. Result

- P0（version authority）**policy 闭环**：authority = Model A（CMake 机制单源），**值 = stale/UNRESOLVED 待用户 release decision**，mismatch 显式入档，禁止 bump 于本 correction。
- 四项绑定纠正全部落档：LICENSE policy（缺失不生成）、applicationDisplayName（NOW/E1）、ZIP 术语（scripted portable ZIP；byte-reproducible 为增强）、icon source/derived 契约（normal build 零工具依赖）。
- **Next Action = M9-E Phase 1 Re-review**；通过后 E1（不生成 icon、不 bump 版本值）。
- verified LKGC **仍 = `07561d9`**；未 push。

## D7. Next（correction 之后的追加）

- **M9-E Phase 1 Re-review（用户）**；通过后 **E1 — identity/version/PE metadata contract**（携带现值 0.1.0，不生成 icon）。
## M9-E Version Decision（用户 authoritative decision，2026-09-19，append-only）

> **M9-E Version Decision Closure**。Phase 1 HOLD 的 P0（version authority 值 UNRESOLVED）由用户作出**显式人工产品/release 决策**后正式解决。本轮 docs-only；E1 未开始。

### V1. Human Version Decision（§1）

- **user decision：next public product version = `2.0.0`**。
- **decision status：RESOLVED**。
- **provenance**：这不是仓库推断、不是 ZCode 选择、**不是**由 "V2" 名称自动推导——它是 **explicit human product/release decision**（用户对 Phase 1 HOLD P0 的直接裁定）。此前 correction 中"UNRESOLVED 待用户 release decision"的开放事实自此关闭。

### V2. Historical Version Boundary（§2，冻结不变）

- **historical release = `v1.0.0`**：tag object `2cee626`、commit target `ae067ab`（"ModbusLens v1.0.0 verified product baseline"）——**永不移动**。
- **当前 CMake `project VERSION = 0.1.0`**：已审计为 **stale bootstrap value**（T001 引导默认值、全历史零变更、与 v1.0.0 基线矛盾的开放事实）。
- **E1 将把它更新为 `2.0.0`**，以恢复 CMake project VERSION 作为唯一 public product-version authority 的**语义一致性**（value reconciliation）。

### V3. Single-source Contract（§3，冻结）

E1 后唯一 authority：**CMake `project(VERSION) = 2.0.0`**。以下全部**派生**，**不得**存在第二份硬编码 version：

| 派生项 | 规则 |
| --- | --- |
| `QCoreApplication::applicationVersion`（QGuiApplication/QApplication 同） | configure_file 派生，读取同一 authority |
| PE FileVersion string | `2.0.0` |
| PE ProductVersion string | `2.0.0` |
| Windows numeric version | `2,0,0,0` |
| package artifact version（zip/folder 名） | `ModbusLens-2.0.0-<arch>` |
| 任何未来 About/version UI | 必须读取同一 authority（若存在） |

（Q3 知识问答的原"双源"结论由本表替代：单源 + 派生，硬编码字面量移除属 E1。）

### V4. PE Mapping（§4，最终冻结）

- public semantic version：**`2.0.0`**
- PE numeric：**`2,0,0,0`**
- PE FileVersion string：**`2.0.0`**
- PE ProductVersion string：**`2.0.0`**
- 第四段 = **固定 0 padding**，**不是**独立 tweak/version source（无 2.0.0.1 之类的第四段来源）。

### V5. Package Naming Contract（§5）

版本部分冻结：**`ModbusLens-2.0.0-<verified-architecture>`**。`<verified-architecture>` 仍必须由 build/toolchain **实际验证后填写**（E3 取证），**不得现在硬编码 x64**。exe 名保持 **`ModbusLens.exe`**。

### V6. Publication Boundary（§6）

**version decision ≠ publication**。本决定**不授权**：`git tag v2.0.0`、GitHub Release、push、installer publication、package upload、code signing。未来如创建 `v2.0.0` tag，必须是**独立的 release/publication gate**；**E1/E2/E3/E4 不得自动创建 tag**。

### V7. Phase 1 Resolution（§7）

- 原「M9-E Phase 1 Review = HOLD」的 P0：**resolved by explicit user decision**（本节 V1）。
- **M9-E Phase 1 Learning / Design Gate = PASS**（含 4 项绑定纠正 + 本 version decision）。
- 记录：**version authority mechanism = RESOLVED（CMake 单源，见 Phase 1 Correction C2）**；**version authority value = 2.0.0**。
- **E1 implementation = AUTHORIZED AFTER THIS DOCS CLOSURE REVIEW**（即本 commit 之后即可开始 E1，无需再等一轮 Review——但 E1 自身完成后仍按惯例提交/汇报）。
- 历史 HOLD 记录（原 Phase 1 Review HOLD + Correction C1–C12）**append-only 保留，不删除**（RCA 留痕）。

### V8. Retained Decisions（§8，重申不变）

`applicationDisplayName` = ModbusLens（NOW/E1）；`CompanyName` = omitted；`LegalCopyright` = omitted；`organizationDomain` = omitted（不编造）；LICENSE = **missing / do not invent**；portable ZIP = **scripted portable ZIP**（不作 byte-reproducible 声明）；installer = **DEFERRED / NOT IN M9-E implementation**；signing = **DEFERRED TO release/security workflow**；AppBar logo = not in scope；StatisticsOverview cleanup = deferred；packaging candidate = **Release**；**M9-F owns global Tab/focus-chain audit**。

### V9. E1 Contract Preview（§9，即将允许的范围）

**E1 允许**：
- CMake `project(VERSION)`：`0.1.0` → **`2.0.0`**
- `applicationVersion` 从 configured version 派生（移除硬编码字面量）
- `applicationDisplayName = ModbusLens`
- 引入 Windows PE metadata contract/resource infrastructure（.rc / configure_file 派生）
- version 字段全部从 CMake authority 派生

**E1 不做**：icon artwork、ICO generation、window icon integration、ZIP packaging、installer、signing、publication。**icon 属 E2**。

### V10. Docs Sync（§10）

T020（本节）/ PROJECT_STATUS / BACKLOG / devlog / INTERVIEW_NOTES 同步。状态：**M9-E IN PROGRESS；Phase 1 = PASS；Next = E1 — identity/version/PE metadata contract；Implementation = NOT STARTED（直到本 docs closure 完成后按 V7 授权开始）**。

### V11. Git / LKGC（§12/§13）

独立 docs-only commit（建议 `M9-E: record 2.0.0 product version decision`）；**不 amend `9d2a82e`**、不 rebase、不 push；**verified LKGC 继续 = `07561d9`**（docs-only 不推进）。
## E1 GO（Version Decision Closure Review = PASS，2026-09-19，append-only）

- **Version Decision Closure Review = PASS**；**E1 = GO**。
- **2.0.0 是 explicit human product decision**（provenance 见「M9-E Version Decision」节：非仓库推断、非工具选择、非 "V2" 名称推导）。
- **非阻塞审计 note（Git-object evidence 纪律）**：今后证明 "Git tag unchanged" **不得使用 `git status`**（status 不展示 tag 状态）；必须使用 **`git rev-parse <tag>`（tag object）、`git rev-parse <tag>^{commit}`（commit target）、`git tag -l` / `git show-ref --tags`** 等 Git object evidence。本轮起 all tag proofs 按此执行。

## E1 — Identity / Version / PE Metadata Contract（Implementation Record，2026-09-19）

> Version Decision Closure Review = **PASS**；**E1 = GO**（见「E1 GO」节）。2.0.0 为 explicit human product decision。

### E1.1 Mandatory Source Re-read（§2，实读）

- `CMakeLists.txt`：`project(ModbusLens VERSION <X> DESCRIPTION ... LANGUAGES CXX)`；target = `qt_add_executable(modbuslens WIN32 src/main.cpp)`。
- `src/main.cpp:5553-5557`：organizationName/applicationName="ModbusLens"、applicationVersion=硬编码 `"<X>"`（E1 前为 0.1.0）；无 displayName/domain。
- `Main.qml:51`：`title: qsTr("ModbusLens")`。
- `scripts/deploy_windows.bat`：`copy /y %BUILD_DIR%\modbuslens.exe %DEPLOY_DIR%\ModbusLens.exe`。

### E1.2 CMake Authority Change（§3）

`project(ModbusLens VERSION 0.1.0 → **2.0.0**)`——**全仓库唯一** public product-version literal（user decision `2.0.0`，见 Version Decision 节）。

### E1.3 Generated Version Interface（§4）

- committed templates：**`src/version.h.in`** + **`src/platform/windows/ModbusLens.rc.in`**。
- generated（build tree only，**不提交**）：`build/debug/generated/modbuslens_version.h`（`MODBUSLENS_VERSION_STRING = "@PROJECT_VERSION@"` + MAJOR/MINOR/PATCH）与 `build/debug/generated/ModbusLens.rc`。
- `configure_file` 双输出 + `target_include_directories(modbuslens PRIVATE generated)`；**.rc 仅 `if(WIN32)` 加入** `target_sources`（非 Windows build 不依赖 rc compiler）。
- **deployed exe 完全自包含**：无运行时 git / tag / repo 文件 / CMakeLists 解析。

### E1.4 applicationVersion / applicationDisplayName（§5/§6）

- **移除** main.cpp 硬编码 `"0.1.0"`；`setApplicationVersion(QStringLiteral(MODBUSLENS_VERSION_STRING))`——运行时实测 **2.0.0**。
- **新增** `QGuiApplication::setApplicationDisplayName("ModbusLens")`（冻结产品名；Qt 语义 = 空 title 窗口的后备标题，Main.qml 已显式 title，不受影响）。
- 保持：applicationName / organizationName = "ModbusLens"；`organizationDomain` **继续 unset**（无权威信息）。窗口 title 冻结 = "ModbusLens"（无版本/来源后缀）。

### E1.5 PE Resource（§8–§10）

generated `.rc`：`FILEVERSION/PRODUCTVERSION = 2,0,0,0`（MAJOR/MINOR/PATCH,0 固定 padding）；string table（040904B0）：FileVersion/ProductVersion = **2.0.0**、ProductName/FileDescription = **ModbusLens**、OriginalFilename = **ModbusLens.exe**；**无 CompanyName/LegalCopyright**；**无 ICON 语句**（E2 才引入）。`#include <windows.h>`（VOS_*/VFT_* 常量来源）。

### E1.6 Filename Audit（§11）

build-tree exe = **`modbuslens.exe`**（CMake target id 小写）；deployed = **`ModbusLens.exe`**（deploy 脚本 `copy /y` 改名，T008.1 起既有行为）。差异 = **大小写 + 部署改名**，无更实质的 rename ⇒ 不触发 STOP；`OriginalFilename = "ModbusLens.exe"` 取产品交付名。**未**修改 target OUTPUT_NAME / deploy naming。

### E1.7 RED → GREEN（§14/§15/§17）

- **RED（E0 tree + E1 oracle）**：`RED_EXIT=1`、`SMOKEFAIL identity: applicationVersion = '0.1.0', expected '2.0.0' (expected E1 missing implementation)`。
- **GREEN**：`SMOKE IDENTITY PASS: applicationName=ModbusLens displayName=ModbusLens version=2.0.0 organizationName=ModbusLens organizationDomain=<unset> title=ModbusLens`（build-tree 与 **deployed** 同句 PASS）。
- oracle 直接读取 **`QCoreApplication::applicationVersion()`** 等运行时值（非 grep 源码）；displayName 断言在修复前由 Qt 的 fallback（缺省 = applicationName）即满足——E1 后为**显式设置**（语义等值、来源显式化，如实记录）。
- **numeric oracle**（§17）：PowerShell `VersionInfo.FileVersionRaw/ProductVersionRaw` = **`2.0.0.0`**（**runtime/PE inspected**，读取二进制 numeric 字段）；source-level 佐证 = generated `.rc` 内容 `FILEVERSION 2,0,0,0`。**两类 oracle 分列报告，无虚报**。

### E1.8 Deploy Propagation（§18/§26）

`deploy_windows.bat` **zero diff**（metadata 内嵌 exe）；重新部署后 **deployed `ModbusLens.exe` PE 检查 = build-tree 逐字段一致**（Raw/strings/Product/FileDescription/OriginalFilename/空 Company/Copyright）。deploy copy 未剥离/改变 metadata。

### E1.9 Gates（§23–§26/§31）

```text
configure + build → 0 error
--qml-smoke-test      → EXIT=0（SMOKE IDENTITY PASS version=2.0.0）
--qml-nav-check       → EXIT=0（basic five-workspace + A–T 20 项；M DEFERRED）
--qml-geometry-check  → EXIT=0；steps = 14 / printed segments = 18；0 GEOFAIL
ctest                 → 100% tests passed, 0 failed out of 26（未新增 CTest）
deploy                → OK；strict minimal PATH 部署版 smoke/nav/geometry 全 0（含 identity PASS）
PE inspection         → build-tree 与 deployed 双 exe：Raw 2.0.0.0 / strings 2.0.0 / 字段齐全 / Company·Copyright 空
stderr 卫生（三模式）  → 全 0
```

### E1.10 Clean Reconfigure Test（§22）

删除 `build/debug/generated/` 全部生成物 → `cmake --preset debug-local` 重新 configure → 两个生成文件**从 committed templates 再生** → rebuild → smoke identity PASS（version=2.0.0）。证明生成物不依赖旧 build dir。

### E1.11 Version Search Audit（§20）

- `0.1.0` production 命中：**0 处有效源**（仅 main.cpp 两行注释记述"旧字面量已移除"——历史记述非版本源）。
- `2.0.0` production 命中：**CMakeLists.txt:4（唯一 authority）** + main.cpp **smoke oracle 的期望值字面量**（两处，`applicationVersion != "2.0.0"` 判定期望——**非版本源**：oracle 必须独立钉住期望值才能检测 authority 漂移（与 C4 golden facts 同理）；未来 release decision 更新版本时同步更新该期望）。templates 仅 `@PROJECT_VERSION@` 系占位符。

### E1.12 Publication / Tag Proof（§19，Git-object evidence）

`git tag -l` = 仅 `v1.0.0`（**v2.0.0 不存在**，preflight `V2_TAG_EXISTS=0`）；`git rev-parse v1.0.0` = `2cee626`、`v1.0.0^{commit}` = `ae067ab` —— 未变。origin/main `a40d935` 不变；无 push/Release/upload。**2.0.0 metadata = development candidate identity，非 publication evidence**。

### E1.13 Negative Scope（§29）

无 SVG/ICO/window icon/taskbar icon/PE icon/AppBar logo/ZIP/Release packaging 工作/installer/signing/LICENSE 发明/CompanyName 发明/Copyright 发明/organizationDomain 发明/StatisticsOverview cleanup/M9-F accessibility/v2.0.0 tag/push。.rc 中无 IDI_ICON/ICON/.ico 引用（§12）。

### E1.14 Files Changed / Result（§33）

`CMakeLists.txt`（VERSION 2.0.0 + configure_file + WIN32 rc）、`src/version.h.in`（新模板）、`src/platform/windows/ModbusLens.rc.in`（新模板）、`src/main.cpp`（生成头 include + 派生 applicationVersion + displayName + smoke identity oracle）、T020（GO 节 + 本记录）、状态文档。**deploy script/QML/其它页 zero diff**。

**E1 candidate complete, awaiting E1 Review**；M9-E 未 COMPLETE；verified LKGC **不变 = `07561d9`**；未 push。

## E2. Next

- **M9-E E1 Review（用户）**；通过后 **E2 — icon asset pipeline + window/PE integration**（SVG master → multi-resolution ICO；window icon/PE icon 接入；人工 icon 清单）。
## E1 Review = HOLD + Correction（2026-09-19，append-only）

> **M9-E E1 Review = HOLD**。P0：final tree 在 `src/main.cpp` smoke oracle 中保留了硬编码 `"2.0.0"`（作为 expected version）——它**不是 runtime version source**，但仍是一个 **second version maintenance fact**，违反 E1 核心契约："change CMake VERSION once → all version consumers follow"。原 E1 RED 证据（actual=0.1.0 / expected=2.0.0 / EXIT=1）作为 historical implementation evidence 保留于 §E1.7，不删改——**RED oracle（钉住决策值检测漂移）与 final consistency oracle（验证 runtime == configured authority）职责不同**，HOLD 的裁定是最终树采用后者。

### H1. Exact Literal Audit（§2，修复前）

| literal | 命中 | 分类 |
| --- | --- | --- |
| `2.0.0` | CMakeLists.txt:4 | **A. authority literal**（唯一合法） |
| `2.0.0` | main.cpp:5634 + main.cpp:5637（smoke oracle 期望值 ×2） | **D. test/harness literal（P0 违规——第二版本维护事实）** |
| `0.1.0` | main.cpp:5560/5607（注释记述历史） | **E. comment/history** |

### H2. Final Runtime Identity Oracle（§3/§5）

applicationVersion 的期望值改从**同一 generated authority** 读取：

```cpp
if (QCoreApplication::applicationVersion()
    != QStringLiteral(MODBUSLENS_VERSION_STRING)) { ... return 1; }
```

**无 `expectedVersion = "2.0.0"` 残留**；literal 未转移到任何 .cpp/.h/script。oracle 职责重定义：**验证 runtime 值 == configured authority 值**（捕捉"有人重新引入 divergent 硬编码"这类回归）；"版本跟随 CMake"的端到端证明由 **§H3 mutation probe** 承担。其余 identity 断言不变（applicationName/DisplayName/organizationName = ModbusLens、organizationDomain unset、title = ModbusLens）。

### H3. Version-change Mutation Probe（§7，本 correction 的核心验证）

**未 commit 的临时 mutation**：CMake `VERSION 2.0.0` → `2.0.1`（仅此一处改动）→ configure + build：

| 证据 | 结果 |
| --- | --- |
| generated `modbuslens_version.h` | `MODBUSLENS_VERSION_STRING "2.0.1"`、MAJOR 2 / MINOR 0 / PATCH 1 |
| smoke identity oracle | **PASS**，`version=2.0.1`（oracle 自动跟随 authority，零测试代码改动） |
| **PE FileVersionRaw** | **`2.0.1.0`**（runtime/PE inspected，二进制 numeric 字段） |
| **PE ProductVersionRaw** | **`2.0.1.0`** |
| PE FileVersion string | `2.0.1` |

⇒ **单一改动点（CMake VERSION）驱动全部 version consumers**（runtime / generated header / PE numeric / PE strings）。**Rollback**：`git checkout -- CMakeLists.txt` → reconfigure + build → `MODBUSLENS_VERSION_STRING "2.0.0"` → smoke PASS `version=2.0.0` → **工作树无 mutation 残留**（仅 oracle 修复本身）。此 probe 非版本决策、非 commit、非 publication。

### H4. Search Acceptance（§8，恢复正式树后）

production `2.0.0` 命中 = **CMakeLists.txt:4 一处（唯一 active authority literal）**；main.cpp **零** 2.0.0 命中（P0 闭环）；templates 仅 `@PROJECT_VERSION@` 占位符；`0.1.0` 仅注释（E 类）。

### H5. Regression & Gates（§11/§16）

configure + build 0 error；smoke 0（identity PASS version=2.0.0）；nav 0（**basic five-workspace + A–T 20 项**，M DEFERRED）；geometry 0（**14 steps / 18 segments**，0 GEOFAIL）；ctest **26/26**；stderr 卫生三模式 0。**Deploy**：deploy_windows.bat zero diff；重新部署后 **deployed PE = FileVersionRaw/ProductVersionRaw 2.0.0.0**、strict minimal PATH 部署版 smoke/nav/geometry 全 0（identity PASS line 同 deployed）、evidence capture 22 张 PASS。

### H6. Tag / Publication Proof（§13，Git-object evidence）

`git rev-parse v1.0.0` = `2cee626`；`git rev-parse v1.0.0^{commit}` = `ae067ab`（**unchanged**）；`git tag -l` = 仅 `v1.0.0`；**v2.0.0 不存在**；origin/main `a40d935` 不变；无 push/Release/upload。

### H7. Result（§14–§17）

- **E1 核心契约成立**：change CMake VERSION once → applicationVersion / generated header / PE numeric / PE strings 全部跟随（mutation probe 证明）。
- Files：`src/main.cpp`（oracle 改为读 MODBUSLENS_VERSION_STRING + RCA 注释）+ docs（T020/PROJECT_STATUS/BACKLOG/devlog/INTERVIEW_NOTES）。**CMakeLists/ModbusLens.rc.in/QML/assets/deploy script 零 diff**（正式内容）。
- **Next Action = M9-E E1 Re-review**；E2 仍未开始（icon 禁令不变）。
- verified LKGC **仍 = `07561d9`**；未 push。
## E1 Re-review = PASS + E2 GO（2026-09-19，append-only）

- **M9-E E1 Re-review = PASS**。
- **P0（second version literal）已由 `641b1db` 闭环**：final consistency oracle 改读 `MODBUSLENS_VERSION_STRING`（同一 generated authority）；**最终证据：production maintained `2.0.0` literal 仅 CMake project VERSION 一处**（main.cpp/templates/scripts 零命中）。
- **mutation probe 证据确认**：CMake `2.0.0 → 2.0.1`（仅改 authority）自动得到 applicationVersion `2.0.1` / PE string `2.0.1` / PE numeric `2.0.1.0`，随后完整回滚。
- **E1 = PASS**；**E2 = GO**（icon asset pipeline + Qt window icon + Windows PE icon integration）。
- E2 边界重申：不做 ZIP/Release packaging/installer/signing/AppBar logo/theme redesign/About dialog/version bump/v2.0.0 tag/publication/StatisticsOverview cleanup/M9-F work。
## E2 — Icon Asset Pipeline + Qt Window Icon + Windows PE Icon Integration（Implementation Record，2026-09-19）

> E1 Re-review = PASS；E2 = GO（见「E1 Re-review = PASS + E2 GO」节）。

### E2.1 Mandatory Re-read + 资产现状（§2）

实读 T020 icon/resource 裁定、CMakeLists、main.cpp、ModbusLens.rc.in、version.h.in、deploy_windows.bat、DesignSystem（仅视觉语言参考，**未改 DS**）。仓库现状复验：**仍无 .svg/.ico/window icon/PE ICON resource**（E1 只有 VERSIONINFO）。

### E2.2 Toolchain Probe + Selected Generator（§3/§4）

| 工具 | 探测结果 |
| --- | --- |
| `magick`（ImageMagick） | **不存在** |
| `icotool`（icoutils） | **不存在** |
| `inkscape` / `rsvg-convert` | **不存在** |
| **PyQt5**（Anaconda，Qt 5.15.2，含 QtSvg） | **可用**（QSvgRenderer offscreen 渲染实测成功） |
| **Pillow 10.2.0** | **可用**（multi-frame ICO 写入） |

**Selected generator = Python helper `scripts/make_icon.py`**（**PyQt5 QSvgRenderer** SVG 光栅化 + **Pillow** multi-frame ICO 组装），按 §4 允许作为 scripts/ developer helper：**normal configure/build 从不调用它**（derived ICO 已提交；`grep -i "python|magick|icotool|make_icon" CMakeLists.txt` = 0；build.ninja 仅 2 处 "python" 命中为 Qt 自带 SBOM cmake 文件名引用，非调用）。Generator **fail-fast**（缺工具/渲染失败/尺寸错误/帧数错误任一即非 0 退出；无 silent fallback——本轮实际捕获并修复 2 个 generator 自身缺陷，见 §E2.9）。

### E2.3 Icon Visual Contract（§5/§6）

**设计**：圆角方形 tile（深蓝 `#24527F`，源自 DesignSystem primary 家族 primaryPressed——**记录：非 DS token contract**）+ 白色 glyph：**放大镜 ring 内嵌 2×2 register grid** + 右下 handle。无文字/无字母依赖/无厂商 logo/无渐变/无 filter/无细线；透明外部背景；monochrome 剪影可辨；不依赖 light/dark theme（单 icon，Phase 1 冻结）。

### E2.4 Canonical SVG（§7）

`assets/brand/icon.svg`（committed）：viewBox 0 0 256、纯矢量 shape、**无 embedded raster/external font/external URL/script/filter/linked image**；**未用**文字转路径。SVG sanity：XML 可解析、PyQt5 renderer `isValid() = true`、可脱离仓库单独渲染。

### E2.5 Generation Pipeline + ICO Inventory（§8/§9/§10）

- **command**：`python scripts/make_icon.py`（tool：PyQt5 Qt 5.15.2 QSvgRenderer + Pillow 10.2.0 ICO writer）。
- **output**：`assets/brand/windows/ModbusLens.ico`（**16,768 bytes**，committed）。
- **独立 ICO directory audit（struct 解析，非工具输出信任）**：header（reserved=0/type=1）合法、**frame count = 6**、frames = **(16,16),(24,24),(32,32),(48,48),(64,64),(256,256)**、无 missing/duplicate；fail-fast 曾实际拦截 2 个 generator 缺陷（QSize 与 tuple 比较错误；QImage→PIL 缺转换），pipeline 的 fail-fast 属性被真实验证。

### E2.6 Qt Runtime Integration（§12/§13）

`qt_add_qml_module(modbuslens ... RESOURCES assets/brand/windows/ModbusLens.ico)` ⇒ runtime path **`:/ModbusLens/assets/brand/windows/ModbusLens.ico`**（qrc 内嵌，**deployed tree 无需 loose icon file**）。`main()` 中 `app.setWindowIcon(QIcon(brandResource))`（QGuiApplication 创建后、主窗口 load 前）。**runtime oracle（smoke identity 扩展）**：brand resource 存在（`QFile::exists`）+ `QGuiApplication::windowIcon()` **non-null** + `availableSizes() = [16x16 24x24 32x32 48x48 64x64 256x256]`（informational——ICO frames 真相仍由 §E2.5 独立审计）。

### E2.7 PE Integration（§15/§16）

`ModbusLens.rc.in` 增加 **`1 ICON "@MODBUSLENS_ICO_PATH@"`**——`MODBUSLENS_ICO_PATH` = CMake 源 root 派生路径（configure 时替换；**committed template 无机器路径**）；windres 经其 include dirs（含 CMake 源 root）解析，**clean configure 可解析**（§24 clean 测试证明）。**同一 committed ICO 同时供 Qt runtime 与 PE**（single source/derived asset；CMake RESOURCES 与 rc.in 路径 identity 由 source/CMake audit 证明）。**VERSIONINFO 字段保持 E1 原样**（numeric 2,0,0,0 / strings 2.0.0 / OriginalFilename ModbusLens.exe / 无 Company·Copyright）。

### E2.8 E2 RED（§18，E1 tree）

- **runtime**：`SMOKEFAIL identity: brand icon resource = '<missing>'` → `RED_EXIT=1`（resource 缺失在 icon-null 检查前拦截）。
- **PE**：pefile oracle = **NONE**（无 RT_ICON/RT_GROUP_ICON）。
- 双 RED 均来自 E2 missing feature，未破坏版本/业务测试。

### E2.9 GREEN（§19）+ Problems/RCA

实现后：brand resource 存在 → `windowIcon` non-null → **`windowIconSizes=[16x16 24x24 32x32 48x48 64x64 256x256]`**（Qt 实际解码记录，informational）；**PE RT_ICON ×6 + RT_GROUP_ICON ×1**。**RCA（本轮真实缺陷，fail-fast 捕获）**：①generator 帧尺寸断言用 `QImage.size() != tuple` 比较恒 False（QSize 类型）→ 改 width()/height() 显式比较；②QImage→PIL 缺转换（直接把 QImage 传给 PIL save）→ 加 in-memory PNG 转换；③`assets/brand/windows/` 目录不存在致 save 失败 → 预建目录；④**`setWindowIcon` 遗漏**（集成时只加了 oracle 与资源，忘了调用本身——GREEN 检查时 PE 已绿而 runtime 仍 null，oracle 直接定位）→ 补 `app.setWindowIcon`；⑤rc.in 注释用 `--` 会重演 windres 语法错误（E1 教训，直接用 `//`）。

### E2.10 Deploy / Runtime Icon on Deployed（§27/§28/§14）

`deploy_windows.bat` **zero diff**；**deployed `imageformats/qico.dll` 实测存在**（windeployqt 既已拷贝——ICO 解码插件依赖在 deployed tree **天然满足**，未手工复制任何 plugin、未改 Phase-1 asset policy、未引入 QtSvg/loose PNG）。strict minimal PATH（替换式）部署版：smoke 0（**identity PASS + windowIconSizes 全 6 尺寸 = deployed runtime icon 加载证明**）/ nav 0（A–T 20 项）/ geometry 0（14 steps·18 segments·0 GEOFAIL）；stderr 无 icon decode/plugin/missing-file/QImageReader/QML resource 警告。**deployed PE**：RT_ICON ×6 + RT_GROUP_ICON ×1 + Raw 2.0.0.0（pefile inspected）。**build-tree PASS 且 deployed PASS，无 blocker**。

### E2.11 Evidence（§11/§29/§30/§31）

**committed review evidence**：`docs/assets/screenshots/m9e-icon-size-matrix.png`（从 committed ICO 实际 frames 生成：16/24/32/48/64/256 × light/dark 双底 contact sheet；**仅 Review evidence，非 runtime asset**）。High-DPI（125%）检查状态：contact sheet 多尺寸清晰无裁切/模糊/异常透明边；titlebar/taskbar/Alt-Tab 的 OS 层人工检查待用户（Manual Icon Visual Review = **WAITING FOR USER**，未自标 PASS）。

### E2.12 Regression / Freeze / Negative Scope（§21–§23/§26/§32/§33）

- **E1 identity/PE metadata 回归**：smoke identity PASS（含 version=2.0.0）+ PE metadata Raw 2.0.0.0 逐字段不变。
- **业务零变化**：nav A–T 全 PASS、geometry 0 GEOFAIL、ctest **26/26**、workspace IA/键盘/selection/cue 零 diff。
- **Main.qml/AppBar/DS 零 diff**（无 logo/无 About button/无 header 变化）；NavigationRail/pages/DS/Controller/Core/tests business code 零 diff；deploy script 零 diff。
- **E3 negative scope**：无 Release build migration/ZIP/package naming/manifest/secret scan pipeline/extraction test（全部属 E3）；本轮部署验证沿用现有 debug-local binary 证明 icon integration。
- **no publication**：CMake VERSION 仍 2.0.0；v1.0.0 object/target 不变（Git-object evidence：`2cee626`→`ae067ab`）；v2.0.0 不存在；origin/main 不变；无 push/Release/upload。

### E2.13 Files Changed（§34/§35）

**新增**：`assets/brand/icon.svg`（canonical source artwork）、`assets/brand/windows/ModbusLens.ico`（committed derived Windows asset，16,768 B）、`scripts/make_icon.py`（maintainer helper）、`docs/assets/screenshots/m9e-icon-size-matrix.png`（review evidence）。**修改**：`CMakeLists.txt`（VERSION 侧不变；RESOURCES + ICO path substitution）、`src/platform/windows/ModbusLens.rc.in`（ICON 语句）、`src/main.cpp`（setWindowIcon + icon oracle）、T020 + 状态 docs。**未改**：QML pages/NavigationRail/DS/deploy script/Controller/Core/tests business code。

**E2 candidate complete, awaiting E2 Review + Manual Icon Visual Review（WAITING FOR USER）**；M9-E 未 COMPLETE；verified LKGC **不变 = `07561d9`**；未 push。

## E3. Next

- **M9-E E2 Review + Manual Icon Visual Review（用户：titlebar/taskbar/Alt-Tab/Explorer + size matrix A–D）**；通过后 **E3 — Release packaging + portable ZIP + package checks**。
## E2 Review = PASS + Manual Icon Visual Review = PASS + E3 GO（2026-09-19，append-only）

- **M9-E E2 Review = PASS**；**Manual Icon Visual Review = PASS**。
- **Manual provenance**：size matrix reviewed from committed ICO frames（`m9e-icon-size-matrix.png`）；Windows deployed candidate 人工确认：**Titlebar PASS / Taskbar PASS / Alt-Tab PASS / Explorer exe PASS / 125% DPI PASS**。
- **视觉结论**：16px lens + grid remains recognizable；24/32 strong small-size presentation；48/64 stable proportions；256 balanced；light/dark acceptable；no visible clipping / halo / transparent-edge defect。
- **generator substitution 记录**：**PyQt5（Qt 5.15.2 QSvgRenderer）+ Pillow 10.2.0 = accepted implementation-time generator substitution**——**不是**原 Phase-1 预先指定的 ImageMagick/icotool（探测均不存在后按 fail-fast 规则选定，E2 Review 追认）。
- **E3 = GO**（Release packaging + scripted portable ZIP + package integrity/negative checks）。
- E3 边界重申：不做 installer/MSI/MSIX/NSIS/Inno/code signing/v2.0.0 tag/GitHub Release/upload/publication/byte-reproducible ZIP claim/AppBar/logo 改动/icon redesign/M9-F accessibility/StatisticsOverview cleanup。
## E3 — Release Packaging + Scripted Portable ZIP + Package Integrity / Negative Checks（Implementation Record，2026-09-19）

> E2 Review = PASS；Manual Icon Visual Review = PASS（见「E2 Review = PASS + Manual Icon Visual Review = PASS + E3 GO」节）；**E3 = GO**。

### E3.1 Packaging Re-read（§2，实读）

- **CMakePresets.json**：committed configurePresets `debug`（build/debug, Debug）/ `release`（build/release, **CMAKE_BUILD_TYPE=Release**）/ default；机器私有 `CMakeUserPresets.json`（git-ignored）提供 **`release-local`**（Ninja + 本机 Qt/编译器路径）——**Release preset 已存在，直接复用**。
- **deploy_windows.bat**：**BUILD_DIR/QT_BIN/MINGW_BIN 参数化已存在**；**DEPLOY_DIR 原为硬编码 `build\deploy`**。
- generated version interface / PE integration 现状 = E1/E2 记录所述。

### E3.2 E3 RED（§4）

E2 tree：`build/package` 不存在、无任何 zip、无 packaging script ⇒ **"expected E3 missing packaging"**（未预放空 zip，未破坏产品测试）。

### E3.3 Release Clean Configure / Build（§5/§6/§22）

全新 **`build/release`** tree（`cmake --preset release-local`；**不复用** build/debug 的 generated/objects/exe）：generator Ninja、CMAKE_BUILD_TYPE=Release（single-config 显式）、编译器 MinGW g++ 13.1.0、Qt 6.11.1；**全部 187 目标**（app + 26 个测试可执行）构建 0 error。**clean reconfigure 语义由"全新目录"满足**（生成 version header/.rc 均从 committed 模板再生）。

### E3.4 Architecture Evidence（§6/§33）

**PE Machine = 0x8664（AMD64）**（pefile 实测最终 Release exe）+ 编译器工具链 mingw1310_64（x86_64-w64-mingw32 目标族）⇒ **package arch label = `x64`**（非因宿主 64-bit 而假设）。packaging 脚本内建拒绝：Machine ≠ 0x8664 即 fail。

### E3.5 Version Derivation（§8/§34）

packaging 脚本从 **Release build tree 的 configured authority** 取版本：`generated/modbuslens_version.h` 的 `MODBUSLENS_VERSION_STRING`（= CMake project VERSION = **2.0.0**）⇒ **stem = `ModbusLens-2.0.0-windows-x64`**；并**交叉验证** Release exe 的 PE ProductVersion == authority（不一致即 fail）。**脚本内无第二份版本字面量**（README 的 2.0.0 由 packaging step 从 authority 生成）；不解析 docs/git tag/README/旧目录名。

### E3.6 Deploy Strategy（§9/§10/§38）

**复用** deploy_windows.bat（不复制第二套 deploy 逻辑）：新增**可选第 4 参数 `DEPLOY_DIR`**（缺省 = `build\deploy`，无参调用行为不变）。Release deploy 显式传入 `build\release\deploy`（§11 分离树）。**Debug deploy regression**：参数化后无参调用重跑 EXIT=0、`build/deploy/ModbusLens.exe` 再现 ⇒ debug-local workflow 未被破坏。RCA：首次实现经 cmd 传空参数失败（**cmd 会丢弃空引号位置参数**）→ 改为 packaging 脚本自读 CMakeCache 显式传 QT_BIN/MINGW_BIN；另一缺陷为批处理注释行被 Python `\b`/`\r` 转义损坏（已修复，无控制字符残留）。

### E3.7 Release Binary Identity（§12/§15/§21）

Release exe（build/release/modbuslens.exe，**2,571,655 bytes**）机器证明：**applicationVersion=2.0.0（authority 派生）**、applicationDisplayName=ModbusLens、windowIcon non-null（availableSizes 6 尺寸）、**PE FileVersionRaw/ProductVersionRaw = 2.0.0.0**、ProductName/FileDescription = ModbusLens、OriginalFilename = ModbusLens.exe、**PE icon RT_ICON ×6 + RT_GROUP_ICON ×1** ⇒ **Release 未丢失 E1/E2 identity**。

### E3.8 Staging + README（§13/§14）

staging = **`build/package/ModbusLens-2.0.0-windows-x64/`**，内容来自 **Release deployed tree**（非源码树/Debug deploy）；**idempotent**（每次先删重建）。README.txt 由 packaging step **从 authority 版本模板生成**（内容仅真实事实：ModbusLens 2.0.0 / portable Windows package / 启动方式 / sample 位置 / unsigned development candidate / no installer required）；**不含** LICENSE 条款/Company ownership/signed/officially published/v2.0.0 tag exists 表述（仓库无 authoritative LICENSE ⇒ 包内**不生成 LICENSE**）。

### E3.9 Package Contents Checks（§15/§16/§17/§18/§19/§35）

- **required present**（机器验证）：ModbusLens.exe、platforms/qwindows.dll、**imageformats/qico.dll**（deployed runtime 实际包含/需要）、ModbusLens/qmldir + Main.qml、**StatisticsOverview.qml（consumer=0 但 packaged component 保留——M9-D/M9-E 契约）**、samples/demo_v1.mlog、README.txt。
- **forbidden absent**：CMakeFiles/`*.o`/`*.obj`/`*.a`/`*.ninja`/build.ninja/CMakeCache.txt、`*.cpp`/`*.h.in`/`*.rc.in`、tests/、`make_icon.py`、`icon.svg`、loose ModbusLens.ico、t014/t015 fixtures、temporary evidence。**PDB**：Release tree 实测无（正常）。
- **Qt runtime 不做脆弱全量 allowlist**：只验证 required present + forbidden absent。
- **maintainer-tool boundary**：package 无 python/PyQt5/Pillow/make_icon.py（§35）。

### E3.10 Negative Scans（§20/§21）

- **credential/config scan**：危险文件名（.env/credentials*/secrets*/token*/**.pem/.key/.pfx/.p12**）+ 可识别文本文件（.txt/.qml/.json/.js）内容模式（OPENAI_API_KEY/ANTHROPIC_API_KEY/API_KEY=/BEGIN PRIVATE KEY/Bearer ）——**PASS**。措辞 = **known-risk credential/config negative scan PASS**（非"数学证明二进制无 secret"）；对二进制 DLL 不做 naive grep。
- **absolute-path audit**：package 文本文件扫源码 root/用户 home/build root 绝对路径 —— **PASS**；二进制内路径（若有）单独分类，不当 product blocker。

### E3.11 Manifest / ZIP / Extraction（§22–§26/§39/§40）

- **manifest**：`package-manifest.sha256`（**1496 payload 文件**，sorted relative path + SHA256，排除自身）——content/integrity record，**非 byte-reproducible ZIP claim**。
- **ZIP**：python zipfile ZIP_DEFLATED（`ModbusLens-2.0.0-windows-x64.zip`，**40,569,927 bytes**，sha256 `59d2d1261900383aa7915d774fed20ecfa9c1c772584e16a15184c7925a8f222`）——**当前 candidate artifact identity**，**不声明**未来同树必然同 hash。
- **entries**：ZIP entries == staging file set（1497，无 missing/extra，根正确）。
- **fresh extraction**：`build/package-extract/ModbusLens-2.0.0-windows-x64/`（删除旧目录重解压）→ **extracted 文件逐个 SHA256 == manifest**。
- **fail-fast**：脚本任一 gate 失败即非 0 退出（本轮真实 fail 了 4 次：deploy 空参数、PE 解析、镜像目录、subprocess decode——全部修复后全绿）。
- **idempotence**：脚本连续运行 3 次，每次先删重建 staging/ZIP/extract（连续两次 ZIP sha256 不同 = scripted ZIP 语义的实证，非缺陷）。

### E3.12 Minimal-PATH Extracted Run + CWD Independence（§27/§28/§30）

fresh extraction 目录、**PATH = C:\Windows\System32;C:\Windows（替换式）**：`--qml-smoke-test` / `--qml-nav-check` / `--qml-geometry-check` **全 PASS**（含 identity PASS 行 version=2.0.0 + windowIconSizes 6 尺寸 = deployed runtime icon 证明）；无 ReferenceError/TypeError/binding loop/NaN/Infinity/plugin/QImageReader/missing-DLL 警告。**external-CWD launch**：从非 package 目录以绝对路径启动 smoke —— PASS（working-directory independence；sample 由用户显式加载，无相对路径依赖）。

### E3.13 Sizes（§32，packaging evidence only）

Release exe **2,571,655 B**（Debug exe 348,702,210 B —— informational，无阈值）；staging 树 ≈ **40.3 MB**；ZIP **40,569,927 B**。不与 Replay perf KPI 混同。

### E3.14 Files / Git Classification（§45/§47）

`scripts/deploy_windows.bat`（DEPLOY_DIR 参数化）、`scripts/make_package.py`（新 maintainer helper，fail-fast/idempotent）+ T020/状态 docs ⇒ **behavior-bearing packaging candidate**（非 docs-only）。**ZIP/exe/DLL/manifest 中间物全部留 ignored build/**，未提交。

### E3.15 Result

**E3 candidate complete, awaiting E3 Review**；随后 **E4 — final evidence/manual candidate**（clean ZIP extraction human launch / window-taskbar-Explorer identity / sample usability / final manual acceptance）之后才可能 M9-E COMPLETE。verified LKGC **不变 = `07561d9`**；未 push。

## E4. Next

- **M9-E E3 Review（用户）**；通过后 **E4 — final evidence/manual candidate**（clean ZIP extraction human launch、window/taskbar/Explorer identity、sample usability、package contents sanity、final manual acceptance）。

## E3 Review = HOLD（2026-09-19，append-only）

- **M9-E E3 Review = HOLD**。主成功链已接受（Release → Release deploy → staging → manifest → scripted ZIP → fresh extraction → strict minimal-PATH execution 全 PASS）。
- **P0：packaging fail-fast contract 尚未被确定性验证**——成功路径已证，但 bad-package fail-closed 行为未充分 exercised。区分 **implementation-time failures**（开发期真实失败的修复留痕）与 **acceptance fail-fast probes**（本轮补齐的确定性注入验证）。
- 另两项 report corrections：①ZIP hash 不同不得作为 idempotence proof；②Debug exe "348.7 MB" 与 D6 的 34,782,210 bytes 精确证据冲突，需重测。
- E3 原报告历史不删改；本节之后追加 correction 结果。

## E3 Review = HOLD + Correction（2026-09-19，append-only）

> **M9-E E3 Review = HOLD**。主成功链已接受（Release → Release deploy → staging → manifest → scripted ZIP → fresh extraction → strict minimal-PATH execution 全 PASS）。P0：**packaging fail-fast contract 尚未被确定性验证**。另两项 report corrections：①ZIP hash 不同 ≠ idempotence proof；②Debug exe "348.7 MB" 系转写错误需重测。原 E3 报告历史保留不删改；本轮区分 **implementation-time failures**（开发期真实失败留痕）与 **acceptance fail-fast probes**（本轮确定性注入验证）。

### H1. Fail-fast Source Audit（§2，condition → detection → propagation → exit → probe status）

| 条件 | detection point | propagation | final exit | probe status |
| --- | --- | --- | --- | --- |
| A Release exe missing | `main()` isfile gate；缺失且 deploy 后仍缺 ⇒ `pe_machine_and_version` → pefile `FileNotFoundError` | uncaught exception | exit 1 | **RUNTIME-COVERED**（probe A） |
| B version unavailable | `read_authority_version`：generated header 缺失/无 STRING | `fail()` → `sys.exit(1)` | exit 1 | **RUNTIME-COVERED**（probe B） |
| C unsupported PE architecture | `pe_machine_and_version`：`Machine != 0x8664` | `fail()` → `sys.exit(1)` | exit 1 | **RUNTIME-COVERED**（probe F4） |
| D deploy/windeployqt failure | `run_deploy`：CMakeCache 字段缺失 / windeployqt 不存在 / bat 非零退出 / 无 "ready" 输出 | `fail()` → `sys.exit(1)` | exit 1 | **RUNTIME-COVERED**（probe D） |
| E required file missing | `structural_checks`：required 清单逐项 isfile | `fail()` → `sys.exit(1)` | exit 1 | **RUNTIME-COVERED**（probe F1） |
| F credential/config hit | `negative_scans`：危险文件名 + 文本模式 | `fail()` → `sys.exit(1)` | exit 1 | **RUNTIME-COVERED**（probe F2） |
| G manifest mismatch | `verify_tree_against_manifest`：payload SHA256+path 逐文件核对 | `fail()` → `sys.exit(1)` | exit 1 | **RUNTIME-COVERED**（probe F3） |
| H ZIP creation failure | `make_zip`：输出目标不可写（目录占用）→ `PermissionError` | uncaught exception | exit 1 | **RUNTIME-COVERED**（probe H） |
| I extraction/run gate failure | `extract_and_verify` / `minimal_path_run`：解压校验 + 子进程非零/超时 | `fail()` / 超时异常 | exit 1 | **RUNTIME-COVERED**（probe F5） |

**全部九类 RUNTIME-COVERED（无一仅 SOURCE-COVERED）**。fail-fast 语义：任一 gate 失败 ⇒ 非零退出，不产出/不保留成功候选。

### H2. Deterministic Failure Probes（§3/§4，全部在 ignored `build/e3-failure-probes/`，真实 deployed tree 的 throwaway 副本；未污染真实 staging/package；runner 移至 `build/e3_probe_runner.py` 防 rmtree 自删）

| probe | 注入方式 | 实测结果 |
| --- | --- | --- |
| **F1** missing required runtime | 副本删除 `platforms/qwindows.dll` → `structural_checks` | `SystemExit(1)`："required package file missing: platforms/qwindows.dll" |
| **F2** credential/config hit | 副本加入 `.env`（内容 `OPENAI_API_KEY=E3_TEST_SENTINEL`，纯测试占位）→ `negative_scans` | `SystemExit(1)`："credential-like filename in package: .env" |
| **F3** manifest mismatch | 真 staging + `write_manifest` → tamper README.txt → `verify_tree_against_manifest` | `SystemExit(1)`："payload mismatch vs manifest: README.txt" |
| **F4** unsupported architecture | **不改真实 exe**：pefile 读 Release exe → `Machine = 0x014C (i386)` → `pe.write()` 写入**副本** → `pe_machine_and_version` | `SystemExit(1)`："PE Machine is 0x014C, expected AMD64 (0x8664)" |
| **F5** extracted-run failure | 副本删除 `platforms/qwindows.dll` → `minimal_path_run` | 子进程失败（broken exe 无法启动 platform）→ `fail()` 非零（TimeoutExpired/非零退出均为 fail-closed 形态） |
| **A** missing exe | 副本删除 exe → `pe_machine_and_version` | uncaught `FileNotFoundError` → 非零 |
| **B** version unavailable | 空 build dir → `read_authority_version` | `SystemExit(1)` |
| **D** deploy failure | 伪造 CMakeCache（指向不存在工具）→ `run_deploy` | `SystemExit(1)`："windeployqt.exe not found" |
| **H** ZIP failure | `blocker.zip` 为目录（不可写目标）→ `make_zip` | uncaught `PermissionError` → 非零 |

**9/9 probes fail-closed**（修复过程中 runner 自身 2 个缺陷被发现并修复：REPO 计算少一层 dirname、probe_h finally 引用错误变量名——probe 不掩盖产品 gate 的真实行为）。

### H3. No Test Backdoor（§5）

无 `--pretend-secret`/`--fake-architecture`/`--force-failure` 类产品级注入开关；probes 直接调用既有 gate 函数 + 真实文件系统注入。**唯一 refactor**：manifest 校验逻辑从 `extract_and_verify` 提取为 `verify_tree_against_manifest(root_dir)`（extract 与 probe 共用；**成功路径 contract 不变**）。`deploy_windows.bat` 正式内容 diff = DEPLOY_DIR 参数化（E3 已接受）。

### H4. Successful Pipeline Re-run（§6，probes 之后全 GREEN）

`make_package.py` 完整重跑：Release deploy → staging → structural PASS → negative scans PASS → manifest（1496 payload）→ ZIP → entries==staging → fresh extraction + manifest 校验 → **minimal-PATH extracted smoke/nav/geometry PASS** → external-CWD PASS。

### H5. Idempotence Terminology Correction（§7）

- **修正**：ZIP hash 不同**不再**用作 idempotence proof。
- **正确证据**：重复运行前主动清理 staging/ZIP/extraction；两轮运行的 **manifest 逐字节相同**（sha256 `57d06c21…` == `57d06c21…`，diff = 0 行）⇒ relative payload set、manifest-covered set、required/forbidden 结果、final gates 全部相同。
- ZIP sha256 跨轮不同（`59d2d126…` / `d6b03014…`）仅证明 **byte-reproducibility NOT CLAIMED**（scripted portable ZIP 语义）。

### H6. Size Re-measure（§8，精确口径）

| 文件 | 精确 bytes | MB（10^6） | MiB（2^20） |
| --- | --- | --- | --- |
| `build/debug/modbuslens.exe` | **35,066,016** | 35.07 MB | 33.44 MiB |
| `build/deploy/ModbusLens.exe`（本次重部署后） | **35,066,016** | 35.07 MB | 33.44 MiB |
| `build/release/modbuslens.exe` | **2,571,655** | 2.57 MB | 2.45 MiB |

- **修正 E3 原报告**："Debug exe 348,702,210 B / 348.7 MB" 为**转写错误**（多写一位数字）；D6 记录 34,782,210 B 与当前 35,066,016 B 的差值 = E3 期正常演进（键盘修复 + E1/E2 增量），**非 348 MB 异常 ⇒ 无 STOP 事项**。
- Release exe 2,571,655 B 复测一致。

### H7. Python / Tool Provenance（§9）

- **Python 3.11.7**（`D:\Anaconda3\python.exe`）；`make_package.py` 依赖 = Python stdlib（zipfile/hashlib/subprocess/struct）+ **pefile**（版本/架构读取）。
- **Python + pefile = maintainer packaging dependency，NOT end-user/runtime dependency**；package 内仍无 Python（negative scan 覆盖）。
- ICO 生成链（E2）provenance 不变：PyQt5 Qt 5.15.2 + Pillow 10.2.0。

### H8. Release Preset Boundary（§10）

- committed `release` preset = **Release semantic/build contract**（CMAKE_BUILD_TYPE=Release、build/release）。
- user `release-local`（git-ignored）= **machine-local Qt/编译器绑定**——不是仓库自包含 artifact；正常 onboarding 仍需本机 toolchain 配置（ENVIRONMENT.md 记录）。无需重构 preset。

### H9. Contract Freeze（§11）

version = 2.0.0、arch label = x64、Release mode、package stem、sample policy（仅 demo_v1.mlog）、StatisticsOverview retention、icon embedding、PE metadata、README legal wording、scripted ZIP semantics——**零改动**（本轮只闭环验证与口径）。

### H10. Full Regression（§12，probes 之后）

Release ctest **26/26**；Release smoke/nav/geometry **0**（identity PASS version=2.0.0 + windowIconSizes 6 尺寸；basic five-workspace + A–T 20 项；**14 steps / 18 segments** · 0 GEOFAIL）；deploy PASS；package structure/scans/manifest/ZIP/extract 全 PASS；minimal-PATH 三模式 PASS；external-CWD PASS；`git diff --check` PASS；stderr 卫生 0。

### H11. Publication / Tag（§13，Git-object evidence）

`git rev-parse v1.0.0` = `2cee626`、`v1.0.0^{commit}` = `ae067ab`（**unchanged**）；`git tag -l` = 仅 v1.0.0；**v2.0.0 absent**；origin/main = `a40d935` 不变；无 push/Release/upload/signing。

### H12. Result（§14–§17）

- **P0 闭环**：fail-fast contract 由 9/9 deterministic runtime probes 确证（bad package 一律 fail-closed，成功候选不可能带缺陷通过）。
- **两项 report corrections 落档**：idempotence 证据口径改为 manifest/payload 集合比较；Debug 尺寸修正为 35,066,016 B ≈ 35.07 MB。
- **Files**：`scripts/make_package.py`（verify_tree_against_manifest refactor）+ docs（T020/PROJECT_STATUS/BACKLOG/devlog/INTERVIEW_NOTES）+ ignored probes。**CMakeLists/QML/icon assets/version/Controller/Core/deploy script 正式内容零 diff**（deploy script 保持 E3 已接受状态）。
- **Next Action = M9-E E3 Re-review**；通过后 **E4 — final evidence/manual candidate**。
- verified LKGC **仍 = `07561d9`**；未 push。
## 44. Review 请求项（Phase 1 Review 须裁定）

1. §12 目标优先级（A/B P0、C/D P1、E REJECT）是否接受。
2. §19/§35：zip（B 案）NOW vs 维持 A（仅 portable folder）。
3. §14 ICO 工具链 requirement（ImageMagick/icotool）可用性确认方式。
4. §15 assets/ 目录结构与生成物入库策略。
5. §16 CompanyName/LegalCopyright 保持缺失（不编造）是否接受。
6. §25 Release 裁定与 §26 重跑范围。
7. §S 阶段序列（E1–E4）与每阶段边界。
