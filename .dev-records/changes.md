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

## 2026-08-26 - PR #1 remaining findings closure

### 变更范围
- 不新增玩法；只修正动画配置运行时、Definition 数据边界、自动攻击行为、空侧栏状态、语音和显示比例。

### 具体改动
- `AnimationSequence` 使用 `facingStride`、`frameDelayMs`、`loop`；`Death=134,15,0` 对所有方向选择同一段非方向帧。
- `Simulation::spawn` 接收 `UnitDefinition`，Entity 独立保存 Definition ID、Faction、多个 UnitTag、AutoAcquire、ReturnFire 和最近攻击者。
- Idle/Stop/Hold 只在射程内自动开火，不追击；AttackMove 继续沿路索敌和追击；受击后 ReturnFire 接入 Simulation。
- Strategic Ability Bar 和 Producer Selector 在无 Provider/Entity 时显示 disabled 空槽，不伪造能力名或建筑名。
- 新增 `assets/audio/voices.ini` 与三个由 Windows SAPI 生成的短 WAV 样例，AudioService 以数据表选择语音，程序化 cue 仅作 fallback/debug。
- 调整 RA2/YR 主菜单控制台和按钮布局，命名 `RenderScaleConfig` 集中世界/单位/UI 比例。
- 更新 corpus Superseded 说明与第三方素材清单。

### 验证情况
- CMake configure PASS；MSVC x64 compile/link PASS；CTest 1/1 PASS。
- 实际启动窗口保持响应；主菜单、Editor HUD、F3 性能层截图已重新捕获。
- 最终人工交互验收仍由用户完成；截图不等同于人工验收通过。

## 2026-08-26 - PR #1 第三次人工验收收口

### 变更范围
- 只修复现有 Editor Sandbox 的输入、渲染锚点、所属色、音频事件、Camera/Zoom 和 Editor 工具层；不新增第二单位、生产、AI、Trigger、经济或多人。

### 具体改动
- `Direction8` 使用等距屏幕方向分类，`Art.ini` 以 `FacingMap` 负责素材 facing 映射；F3 显示世界方向、Art facing、动画帧与 SHP frame index。
- `SpriteFrameGPU` 保存 crop/full-canvas/pivot metadata，渲染和选取共享 ground anchor；静态地形改存 world/isometric 顶点并由 D3D11 constant 应用 camera/zoom。
- 所属色统一走 Owner -> ColorSchemeId -> 16 级 16..31 remap LUT；健康条按生命状态着色，不再按 Owner 着色。
- Command Card pending target 优先于框选；Move/Patrol/AttackMove/Smart Context 共享目标路由，快捷键与按钮复用同一行为，Stop/Hold 立即执行。
- VoiceSelect/MoveAcknowledgement/AttackAcknowledgement 使用真实配置 WAV、独立 voice stream 和清队列策略；attackEvent 只走独立 WeaponFire（当前无素材则静音）。
- 新增 F2 可拖动/折叠/关闭 `SandboxPalette`，移除正常 Production Sidebar 的 Editor Owner 控件；F6 开发用八方向循环测试。

### 验证情况
- 增量 MSVC x64 `/W4 /WX` compile/link PASS。
- 定向 core CTest 已通过；全量 configure/build/CTest、运行截图和人工交互证据待最终收口后重跑。

### 风险
- 运行期 Camera、screen-space hit-test、16..31 remap 和命令语音需要用户在真实窗口中做最终视觉/交互确认；自动测试不替代人工验收。

## 2026-08-26 - PR #1 第三次收口最终验证

- 完成干净 build 目录验证：独立 vcpkg restore、CMake configure、MSVC x64 compile/link、CTest 和实际运行均成功。
- 运行时输出确认 `VoiceSelect`、`VoiceMoveAcknowledgement`、`VoiceAttackAcknowledgement` 数据项加载，以及 Rules/Art/CONS.SHP/unittem.pal 内容链路可用。
- 保留 UI 结构收口：五段底部 HUD、3x5 Command Card、空 Strategic/Producer provider 槽位和独立 SandboxPalette。
