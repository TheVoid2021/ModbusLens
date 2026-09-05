# ENVIRONMENT — 开发环境与工具链

> 规则：环境变化（换机器、升级 Qt/编译器）必须更新本文件并记录变更日期。

## 1. 本机实测环境（Windows，2026-09-05 由 T001 验证）

| 项 | 值 | 路径 |
| --- | --- | --- |
| OS | Windows 11 (10.0.26200) | — |
| Shell | Git Bash | — |
| C++ 编译器 | MinGW-W64 g++ 13.1.0 (posix-seh) | `D:/QT/Tools/mingw1310_64` |
| CMake | 3.30.5（Qt 安装器自带） | `D:/QT/Tools/CMake_64` |
| Ninja | 1.12.1（Qt 安装器自带） | `D:/QT/Tools/Ninja` |
| Qt | 6.11.1，Desktop MinGW kit | `D:/QT/6.11.1/mingw_64` |
| Git | 2.55.0 | — |

**本机标准三步走**（工具链由被 gitignore 的 `CMakeUserPresets.json` 注入，无需手改 PATH）：

```bash
cmake --preset debug-local
cmake --build --preset debug-local
ctest --preset debug-local
```

## 2. 重要警告（本机特有）

1. 系统 `PATH` 里另有 `D:/mingw64` 的 **g++ 8.1.0** 与 Anaconda 的 **Qt5 qmake**——与本项目 Qt 6 ABI 不兼容。**禁止裸用**它们构建，只能走 `*-local` preset 注入 Qt 自带工具链。
2. 本机 Qt 构建时曾出现 qtlicd 许可证提示（K1，见 PROJECT_STATUS），preset 中已注入 `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1` 规避。
3. Qt 安装目录在 `D:/QT`（盘符可能不同）；换机器后用 `qtpaths.exe --qt-version` 核对 kit。

## 3. 各平台搭建指引

### Windows（推荐：Qt 官方在线安装器，版本对齐 MinGW kit）

1. 安装 Qt 在线安装器（https://www.qt.io/download-qt-installer）。**Qt for Desktop/MinGW kit 自带 CMake、Ninja 与 MinGW 工具链**，无需单独安装编译环境。
2. 记下四个路径：Qt 前缀（如 `C:/Qt/6.x.y/mingw_64`）、`Tools/CMake_64`、`Tools/Ninja`、`Tools/mingw*/bin`。
3. 复制 `CMakeUserPresets.json.example`（如存在）或在本地建 `CMakeUserPresets.json`，把上面四个路径填进去（参考已注释示例；该文件被 .gitignore，避免污染仓库与其它开发者）。
4. 执行 `cmake --preset debug-local && cmake --build --preset debug-local && ctest --preset debug-local`。

备用路线：vcpkg（`vcpkg install qtbase`）+ MSVC + Visual Studio Generator；或 MSYS2 UCRT 工具链（注意与 Qt kit 的 ABI 对齐，g++ 8.x 不可用）。

### Linux（Ubuntu/Debian 示例）

```bash
sudo apt install g++ cmake ninja-build qt6-base-dev
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```

串口开发还需 `qt6-serialport-dev`（T005 起）；串口权限加入 `dialout` 组。

### macOS

```bash
brew install cmake ninja qt
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```

## 4. 常见问题（FAQ）

| 现象 | 原因与解法 |
| --- | --- |
| `find_package(Qt6)` 失败 | Qt 不在系统默认位置 → configure 加 `-DCMAKE_PREFIX_PATH=<Qt前缀>`，或在 CMakeUserPresets.json 中固化 |
| AutoMoc 出现 "Cannot acquire license to use qtframework" | 本机 Qt 的证书服务(qtlicd)不可用 → preset 环境注入 `QTFRAMEWORK_BYPASS_LICENSE_CHECK=1`（该变量仅跳过构建期检查，不改变产物许可；正式商业使用应先注册 LicenseService） |
| 程序或测试闪退/无法显示 | 无显示器环境 → 测试已统一加 `QT_QPA_PLATFORM=offscreen`（CMakeLists 中设置） |
| 混用多套 Qt 导致的玄学链接错误 | PATH/CMAKE_PREFIX_PATH 里混入 Qt5（如 Anaconda）→ 清理或只用 preset 注入的工具链 |
| 编译中文注释报错（MSVC） | 源码文件保持 ASCII 安全（英文注释），中文只出现在 Markdown 文档（UTF-8） |
| `ctest` 找不到 Qt DLL | 运行测试的 shell 需要能定位 Qt bin；使用 preset 注入的 PATH（`*-local` 已处理） |

## 5. 未来：CI（BACKLOG 关联）

- 计划：GitHub Actions 双平台矩阵（Ubuntu + Windows），`cmake --preset debug && ctest --preset debug` 作为最小门禁；时机在 T002 之后评估。
- 目标：**任何 AI Coding 平台/协作者**先跑通 CI 再开始改代码。

## 6. 变更记录

| 日期 | 事件 |
| --- | --- |
| 2026-09-05 | T001 记录本机（Windows/Qt 6.11.1 MinGW）环境并产出 local presets 方案 |