# 变更记录

## 2026-08-26 - 初始化第一轮可玩编辑器工程

### 变更范围
- C++23/CMake/vcpkg 工程骨架。
- Westwood INI、PAL、SHP(TS) 解析。
- Rules 数据加载和 data-oriented Sandbox Simulation。
- SDL3/D3D11 客户端、DTA 风格主菜单、Editor HUD、3x5 Command Card。

### 具体改动
- 新增 `src/Westwood` 的 INI、PAL、SHP(TS) 解码。
- 新增 `src/GameData` 的 E2/M1Carbine 数据读取。
- 新增 `src/Simulation` 的 Owner/Faction、选择、移动、命令、攻击、扣血和死亡。
- 新增 SDL3/D3D11/GDI 文本客户端和 3x5 Command Card。
- 新增 `docs/FIRST_PLAYABLE_EDITOR_SLICE.md`、`docs/THIRD_PARTY_ASSETS.md` 与 UI 字符串资源。

### 验证情况
- 静态检查通过：本地 include 可解析、括号/花括号计数平衡、无尾随空白、Command Card 为 15 个槽位。
- 外部 corpus 资产存在：`CONS.SHP` 160896 B、`unittem.pal` 768 B、effective `rulesmd.ini` 743215 B；CONS 首帧为 76x96 画布、602 帧、Format 3。
- 本机暂缺 CMake/MSVC/clang-cl/vcpkg，未执行真实 configure/build/test/程序人工验收。

### 风险
- D3D11/GDI 文本渲染需在 Windows 工具链上实际编译与人工验证。

## 2026-08-26 - PR #1 视觉与状态收口

### 变更范围
- 不新增 Gameplay 玩法；只修正首轮 Editor Sandbox 的动画状态、UI 结构、术语和可观测性。

### 具体改动
- `ArtDatabase` 支持 8 方向序列帧索引；`Simulation::Entity` 保存独立的 Idle/Walk/Attack/Death、方向和帧状态。
- Editor 底部 HUD 固定为五段；右侧生产栏增加产能建筑 selector；左侧战略能力栏加入折叠控件与能力名称。
- 玩家信息改为动员兵、M1 卡宾枪、轻型装甲、125/125；资源文件名和动画状态转入 F3。
- 统一 Command Card 第一行术语为移动、停止、原地不动、巡逻、攻击/攻击移动。
- 主菜单按钮比例和布局向 RA2/YR 参考图收敛，使用已有合法 RA2/YR 风格图片资源。
- SDL 音频初始化与程序化 UI/单位 cue 事件链路打通；当前 cue 不是配音录音。

### 验证情况
- MSVC x64 C++ 编译、链接成功；CTest 1/1 通过。
- 实际 EXE 启动并加载项目 Rules/Art/CONS/palette；重新捕获主菜单、Editor HUD 与 F3 状态截图。
- 最终人工视觉/交互验收仍由用户完成。
