# 开发时间线

## 2026-08-26 - 建立 first playable editor 隔离分支

### 用户目标
- 在最新 `origin/main` 上启动 DTA 风格主菜单与 RTS Editor/Sandbox vertical slice。
- 右下角 Command Card 固定为 3 行 5 列。

### 本轮处理
- fetch 并确认 `origin/main` 为 `70509c10290a1d82711589dc946ce03dad78b4e4`。
- 从该提交建立 `feature/first-playable-editor-shell` 独立 worktree。
- 确认 main 尚无 C++ 源码，仅有架构、语料与设计文档。

### 关键结论
- 使用外部 `RA2YR_CORPUS_ROOT` 读取真实兼容语料；不复制或转换原版 SHP 为 PNG。
- 首轮运行目标锁定 Windows + SDL3 + D3D11，核心 Simulation/Westwood 解析不依赖 Renderer。

### 影响文件
- 无直接文件修改

### 后续事项
- 完成核心数据、模拟、渲染、客户端 UI 和测试。

## 2026-08-26 - 完成首轮工程落盘与静态门禁

### 用户目标
- 需要一个可操作的 DTA 风格主菜单、Editor/Sandbox 和 3x5 Command Card。

### 本轮处理
- 新增 CMake/vcpkg 工程、Westwood INI/PAL/SHP(TS) 读取、Rules 数据、Simulation 命令与战斗、SDL3/D3D11/GDI 客户端。
- 主菜单的 Editor/Exit 按钮有真实行为，其余按钮保留 hover/pressed 并显示未实现提示。
- Editor 支持红/蓝 E2 放置、选择、移动、停止、Hold、巡逻、Attack Move、攻击、伤害和死亡路径。
- 右下角 Command Card 明确为 3 行 5 列。

### 关键结论
- 静态 include、括号/花括号、无尾随空白检查通过。
- 外部 corpus 中真实 `CONS.SHP`、`unittem.pal` 和 effective `rulesmd.ini` 可定位；本地构建工具链缺失，因此运行时尚未人工验收。

### 影响文件
- `CMakeLists.txt`
- `CMakePresets.json`
- `src/`
- `tests/core_tests.cpp`
- `docs/FIRST_PLAYABLE_EDITOR_SLICE.md`
- `docs/THIRD_PARTY_ASSETS.md`
- `assets/ui/strings.ini`

### 后续事项
- 在 Windows C++ 工具链环境 configure/build/test。
- 启动程序完成主菜单、分辨率、资源、输入和战斗人工验收。
- 后续再实现 MIX VFS、真实 MAP/TMP 和完整 Classic 链路。

## 2026-08-26 - Git 落盘与远端备份审查

### 用户目标
- 暂停新增功能，先审查、提交并备份首轮 Editor Sandbox slice。

### 本轮处理
- 核对 feature worktree 的分支、HEAD、状态和文件清单。
- 确认原 `agent/ra2yr-corpus-lfs` worktree 的未提交语料仍保持隔离，不将其纳入本次提交。
- 复核 feature worktree 中没有复制原版 MIX/SHP/PAL/RULES 语料；构建工具链缺失的验证边界保持不变。

### 后续事项
- 完成提交、fetch、push 和 Draft PR 备份后停止，不 merge PR。
