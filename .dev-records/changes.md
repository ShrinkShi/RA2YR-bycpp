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
