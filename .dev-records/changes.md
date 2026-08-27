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
- 新增 `assets/audio/voices.ini` 与三个由 Windows SAPI 生成的短 WAV 样例（历史实现，已被本轮 corpus 原版 VoiceSet 素材 Superseded），AudioService 以数据表选择语音，程序化 cue 仅作 fallback/debug。
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

## 2026-08-27 - PR #1 最终收口：选择、占位、UI Skin 与原版 VoiceSet

### 变更范围
- 不新增完整 Editor 绘图工具、复杂 Unit Info Panel、第二单位、生产、AI、Trigger 或资源系统；仅收口现有首轮 Sandbox 的视觉和数据边界。

### 具体改动
- 选择标识改为以 `SelectionRadius` 为半径的 world-ground 圆，经等距 Camera 投影为椭圆；选中实线，hover/框选候选虚线，已选单位不叠加虚线。
- `OccupancyProfile=Infantry` 进入 Simulation occupancy；每个 Ground Cell 使用 TopCenter/BottomLeft/BottomRight 三个 subcell，满格后搜索最近可用 cell，避免最终坐标重叠。
- 清理正式 HUD 的开发提示；F3 保留调试数据，正式底部 HUD 保持五段结构。
- 新增 `INI/UI.ini` 和 `UiLayoutDatabase`，以主题图片、父级 rect、相对锚点作为绘制与 hit-test 的唯一来源；迁移主菜单、Command Card、HUD、生产栏、战略栏和 Sandbox 浮窗。
- `VoiceSelect/VoiceMove/VoiceAttack` 改为数据驱动 VoiceSet，多样本随机且禁止立即重复；接入本地 RA2/YR corpus 提取的原版 E2 样本。

### 验证边界
- 自动测试覆盖 UI 父级移动同步、VoiceSet 配置、多 Infantry subcell 和最终不重叠；真实 configure/build/CTest/运行截图将在本轮所有改动完成后重跑。
- 截图和自动化结果只作为运行证据，不替代用户的最终视觉/交互验收。

## 2026-08-27 - 最终验证记录

- 运行 `tools/dev/build.ps1`：MSVC x64 compile/link PASS；`tools/dev/test.ps1`：CTest `1/1` PASS。
- 实际启动 `build/windows-vcpkg/ra2yr_client.exe`，进程保持响应；日志确认 SDL/D3D11 renderer、Rules/Art、CONS.SHP、unittem.pal 和 E2Select/E2Move/E2Attack 各 3 个 WAV 样本加载。
- 运行时诊断确认 SDL logical/pixel canvas 为 `1280x720`；截图工具在当前桌面合成环境下存在黑帧/被遮挡情况，保留的截图仅作为运行证据，不替代人工视觉/交互验收。

## 2026-08-27 - PR #1 UI Skin merge blocker 收口

### 变更范围
- 只补齐正式 UI 面板的主题图片数据化；不新增 Gameplay、完整 Editor 绘图工具或复杂 Unit Info Panel。

### 具体改动
- 在 `INI/UI.ini` 增加 HUD 总背景、Minimap、Model、UnitInfo、Portrait、Command Card、Production、Strategic 和 Sandbox 的独立主题图片 ID。
- 新增 `assets/ui/themes/ra2_soviet/{hud,editor}` 下的项目自有 RA2/YR 风格正式面板 PNG，并让 CMake 资源复制流程带入构建目录。
- 正式 HUD/生产栏/战略栏/Sandbox 外壳改用 `renderer.drawImage(theme asset, rect)`；动态内容仍使用代码绘制。
- `core_tests` 校验九个正式面板 image ID 均存在且默认主题路径可解析；文档记录 MOD 可独立替换图片与修改 rect。

### 验证情况
- `tools/dev/setup.ps1`：CMake configure/generate PASS，vcpkg manifest restore 已使用已有依赖。
- `tools/dev/build.ps1`：MSVC x64 compile/link PASS。
- `tools/dev/test.ps1`：CTest `1/1` PASS。
- 实际启动 `build/windows-vcpkg/ra2yr_client.exe`，进程保持响应；构建目录确认九个主题 PNG 已复制，运行日志确认 SDL/D3D11、Rules/Art、CONS.SHP、unittem.pal 和 VoiceSet 加载。

## 2026-08-27 - PR #2 Editor Tools and Unit Status

### 变更范围
- 只增加 Editor Tool System 和 Unit Status ViewModel，不新增第二单位、生产、经济、AI、Trigger、多人或真实 MAP/TMP。

### 具体改动
- 新增数据驱动 Terrain Registry/Map 与六工具编辑控制器；Void、四连通 Fill、笔刷预览、单位真实 spawn/erase 和 Infantry occupancy 均通过正式模块实现。
- Rules 支持 infantry registry、UIName/SecondaryUIName、多个武器、ArmorDefinition、ExperienceValue、VeterancyProfile、可选 shields/energy 和标签本地化。
- Simulation 保存击杀/经验/军阶并在正式击杀时更新；HUD 通过 UnitStatusViewModel 读取生命、军阶、护甲、多武器和标签。
- UI.ini 增加工具窗口 rect、工具图标和 Status 卡片布局；新增 `INI/Terrain.ini`、`INI/Editor.ini` 与项目自有图标资源。

### 验证情况
- MSVC x64 configure、compile/link 和 CTest 已完成通过；EXE 启动证据在本轮最终验证中补充，自动测试不等同于人工验收。

### 最终验证补充
- 最新改动重新完成官方 setup/configure、MSVC compile/link 和 CTest `1/1 PASS`；UnitStatus 阈值来自 `UI.ini`，Editor brush presets 来自 `Editor.ini`，武器卡贴图按 Rules weapon registry 动态加载。
- 实际启动最新 `ra2yr_client.exe` 并采集主菜单、Editor Sandbox、F3 Debug Overlay 窗口证据；截图不替代人工视觉/交互验收。

## 2026-08-27 - 编队 reservation 与 Sandbox card geometry 收口

### 变更

- `Simulation::commitReservation` 现在先释放 reservation map，再写入当前 occupancy；`releaseReservation` 同步清空 cell/subcell 字段，避免旧目标槽位永久阻塞后续命令。
- Sandbox palette 增加 `sandbox.asset.icon.*` 与 `sandbox.asset.label.*` 相对 Rect；素材卡片绘制使用同一份配置布局，图标与标签不再依赖 C++ inset 常量。
- 编队测试覆盖 6/9 infantry 的 3-subcell 分配、每格三种 subcell、确定性 assignment 和抵达后二次复用。

### 验证

- `tools/dev/setup.ps1` configure/generate PASS；MSVC x64 build/link PASS；CTest `1/1 PASS`。
- EXE 实际启动且保持响应，日志确认 SDL/D3D11、Rules/Art、CONS.SHP、unittem.pal 和 E2 VoiceSet 样本加载。
