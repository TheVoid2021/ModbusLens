# Devlog — 2026-09-05（T001 项目引导）

- 09:50 探测本机环境：Qt 6.11.1（MinGW kit，D:/QT）自带 CMake 3.30.5 / Ninja 1.12.1 / MinGW g++ 13.1.0；系统 PATH 有 g++ 8.1 与 Anaconda Qt5（需隔离）。
- 建立目录骨架、CMake Presets 双层结构（通用 + 机器私有）。
- 最小应用与冒烟测试：build 通过、ctest 1/1 通过、offscreen 启动验证（exit=124 为预期）。
- 处理 AutoMoc 的 qtlicd 许可证提示：`QTFRAMEWORK_BYPASS_LICENSE_CHECK=1` 注入 local preset，重构建零噪音。
- 编写全套文档体系（AGENTS 规约、charter/requirements/architecture/learning/test/demo、STATUS/BACKLOG/ENVIRONMENT/INTERVIEW）。
- 提交：主提交 `aa337f6`；收尾 docs-only 回填提交见 `git log`。
- 任务档案：[T001](tasks/T001-project-bootstrap.md)