# ENVIRONMENT — 开发环境与工具链

> 规则：环境变化（换机器、升级 Qt/编译器）必须更新本文件 §1 与 [PROJECT_STATUS.md](PROJECT_STATUS.md)，并记录变更日期。

## 1. 当前验证环境（唯一官方验证过的组合）

> 以下组合经 configure + build + test 实测通过（2026-09-05，T001/T001.1）。
> **其他组合（Qt 版本、编译器、平台）未经验证**；换环境后请按 §4 重建并跑通，然后回填本表。

| 项 | 值 |
| --- | --- |
| OS | Windows 11 |
| Qt | **6.11.1**（Desktop MinGW kit） |
| C++ 编译器 | MinGW-W64 g++ **13.1.0**（posix-seh，Qt 安装器自带） |
| CMake | **3.30.5**（Qt 安装器自带 Tools/CMake_64） |
| Ninja | **1.12.1**（Qt 安装器自带 Tools/Ninja） |
| Git | 2.55.0 |

### 1.1 本机事实记录（仅供人读与排障，**不是构建输入**）

以下是本项目最初开发机的安装位置（仅作文档记录；**项目代码、提交的 CMake 文件、测试数据均不得依赖这些路径**，红线见 §5）：

| 组件 | 本机路径 |
| --- | --- |
| Qt 前缀 | `D:/QT/6.11.1/mingw_64` |
| CMake | `D:/QT/Tools/CMake_64` |
| Ninja | `D:/QT/Tools/Ninja` |
| MinGW | `D:/QT/Tools/mingw1310_64` |

> ⚠️ 换电脑后这些路径必然失效——这正是 §2/§3 中"机器私有 preset"机制存在的原因。

## 2. 为什么需要 CMakeUserPresets.json（机器私有 preset）

1. **工具链不在默认 PATH**：本机系统 PATH 里有旧版 g++ 8.1（`D:/mingw64`）和 Anaconda 的 Qt5 qmake，二者与 Qt 6 的 ABI 不兼容，直接裸用 `cmake` 会找到错误的编译器/错误的 Qt。
2. **每个开发机路径都不同**：盘符、Qt 版本、kit 类型因人而异。如果把某台机器的绝对路径写进提交的 `CMakePresets.json`，其他人（或另一个 AI Coding 平台）clone 后 100% 构建失败。
3. 因此约定两层结构：
   - `CMakePresets.json`（**提交**）：通用预设 `debug/release`，只描述"构建类型 + 输出目录"，不绑定生成器与工具链路径；
   - `CMakeUserPresets.json`（**gitignored，永不提交**）：每台机器自己创建，注入本机 Qt/编译器/生成器路径，名为 `debug-local/release-local`。

## 3. 如何创建自己的本机 preset（从模板开始）

仓库提供可提交模板：[`CMakeUserPresets.example.json`](../CMakeUserPresets.example.json)。

```bash
# 1. 复制模板（生成的 CMakeUserPresets.json 已被 .gitignore 保护）
cp CMakeUserPresets.example.json CMakeUserPresets.json

# 2. 编辑 CMakeUserPresets.json，把以下 4 处示例路径替换为你的机器真实路径：
#    environment.PATH          → <你的 Qt 安装>/Tools/{CMake_64,Ninja,mingw*}/bin
#    cacheVariables.
#      CMAKE_PREFIX_PATH       → <你的 Qt 前缀>，如 C:/Qt/6.8.2/mingw_64
#      CMAKE_CXX_COMPILER      → <你的 MinGW>/bin/g++.exe
#    模板自带的 C:/Qt/... 是通用示例值（Qt 默认安装布局），不是任何一台真实机器的路径。

# 3. 自检工具链（应与 §1 表一致的版本量级）
<Qt前缀>/bin/qtpaths.exe --qt-version      # 例如输出 6.8.2 / 6.11.1
<MinGW>/bin/g++.exe --version              # 例如 13.1.0

# 4. 三条标准命令
cmake --preset debug-local
cmake --build --preset debug-local
ctest --preset debug-local
```

- 模板中**故意不含** `QTFRAMEWORK_BYPASS_LICENSE_CHECK` 等任何本机特例变量——新机器不需要；只有出现 qtlicd 警告时才按 §6 FAQ 处理。
- 如果 Qt 恰好装在系统默认路径且默认生成器可用，也可以直接用通用 preset：`cmake --preset debug [-DCMAKE_PREFIX_PATH=<Qt前缀>]`。

## 4. 换电脑 / 换 AI 平台时重新建立环境的清单

1. 安装 Git 与 Qt（<https://www.qt.io/download-qt-installer>，勾选 Desktop/MinGW kit——安装器自带 CMake、Ninja、MinGW，一步到位）。
2. `git clone` 仓库，先读 `AGENTS.md` → `PROJECT_STATUS.md` → `BACKLOG.md`。
3. 按 §3 从模板生成本机 `CMakeUserPresets.json`。
4. 三条命令全绿（configure + build + ctest）。
5. **回填事实**：更新本文件 §1 验证环境表与 `PROJECT_STATUS.md` 的"开发环境"（项目可追溯性要求）。
6. （Linux/macOS 参考）Ubuntu：`sudo apt install g++ cmake ninja-build qt6-base-dev`；macOS：`brew install cmake ninja qt`。均用通用 preset；串口模式（T010）还需 Linux `dialout` 组权限。

## 5. 路径依赖红线（项目代码不得依赖任何机器的绝对路径）

- **红线**：`src/`、`tests/`、`CMakeLists.txt`、提交的 `CMakePresets.json`、`.github/`（未来 CI）、示例日志/测试数据中，**禁止出现任何机器绝对路径**（`D:/QT`、`E:/desktop`、`C:/Users/...` 等）。
- **自检命令**（每次提交前可跑）：

```bash
git grep -n -E "D:/QT|E:/desktop|C:/Users" -- . ':!docs/*' ':!demo/*'
# 应无输出；docs/ 与 demo/ 中仅允许 §1.1 这类"文档事实记录"出现本机路径
```

- **双保险**：真实本机 preset 被 `.gitignore` 精确忽略（`git check-ignore CMakeUserPresets.json` 应命中），即使 `git add -A` 也不会误提交。
- 收益：任何 AI 平台/协作者接管时，"clone → 复制模板 → 三条命令"即可独立重建，不继承原电脑的任何路径假设。

## 6. 常见问题（FAQ）

| 现象 | 原因与解法 |
| --- | --- |
| `find_package(Qt6)` 失败 | Qt 不在系统默认位置 → 在本机 `CMakeUserPresets.json` 中把 `CMAKE_PREFIX_PATH` 改为真实前缀（§3 第 2 步） |
| AutoMoc 出现 "Cannot acquire license to use qtframework" | 这是**当前这台开发机的 Qt 安装**出现 qtlicd（Qt 许可证服务）不可用/版本不匹配的**构建期警告**。临时处理：仅在本机（且仅本机）的 `CMakeUserPresets.json` 的 `environment` 中加 `"QTFRAMEWORK_BYPASS_LICENSE_CHECK": "1"`。该提示来自本机构建输出，**不能**表述为"Qt 官方确认的推荐方案"，也**不是** ModbusLens 的必要依赖；**项目代码与提交的文件绝不使用该变量**（example 模板不含它，CI 与换机后的环境不需要它）。不出现该警告就不必设置；如要正式解决，应为该 Qt 安装注册 License Service 或联系 Qt 支持 |
| 构建时用了错误编译器 | 多工具链混装（旧版 MinGW、Anaconda Qt5 在 PATH）→ 一律通过 `*-local` preset 显式指定 `CMAKE_CXX_COMPILER` 与 PATH，不要依赖 PATH 顺序 |
| 测试无法运行/闪退 | 无显示器环境 → 测试已统一 `QT_QPA_PLATFORM=offscreen`（在 `CMakeLists.txt` 的 `set_tests_properties` 中） |
| 中文乱码或 MSVC 编译中文报错 | 源码文件保持 ASCII 安全（英文注释）；中文仅出现在 UTF-8 的 Markdown 文档 |

## 7. 未来：CI（关联 BACKLOG）

- 计划：GitHub Actions 双平台矩阵（Ubuntu + Windows），通用 preset + `ctest` 作为最小门禁；时机安排在 T002 之后评估。
- 目标：任何平台/协作者先看 CI 全绿再开始改代码。

## 8. 变更记录

| 日期 | 事件 |
| --- | --- |
| 2026-09-05 | T001：记录本机（Windows / Qt 6.11.1 MinGW）环境，产出 local preset 方案 |
| 2026-09-05 | T001.1：重写本文件——明确验证环境、preset 制作流程（example 模板）、路径红线与换机重建清单；澄清 qtlicd 处理为临时本机手段 |