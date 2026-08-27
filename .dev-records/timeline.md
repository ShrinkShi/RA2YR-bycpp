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

## 2026-08-26 - PR #1 人工验收收口修复

### 用户目标
- 暂停 Gameplay 新功能，只修正 E2 动画、编辑器 HUD、生产/战略侧栏、主菜单比例、玩家信息和最小音频反馈。

### 本轮处理
- 将 CONS 的动画选择改为 `状态 + 方向 + 帧`，固定 8 方向，并区分 Ready/Walk/Fire/Death。
- 将底部 HUD 拆为小地图、单位模型、单位信息、头像/动画预览、3x5 Command Card 五段。
- 增加具体产能建筑选择器第二排、可收起战略能力栏和 Terrain/Unit、E2、Owner、选择/放置状态提示。
- 将正式单位信息与 F3 调试层分离；F3 才显示 `E2 / CONS.SHP`、方向和帧。
- 主菜单使用 RA2/YR 红黑 CRT、银色金属框、红色按钮和黄色中文，不再使用 Tiberian Sun 主题。
- 接入程序化菜单/单位 cue 音频架构，并保留最小可用提示音。

### 真实验证
- `build.ps1` 重新编译并链接 `ra2yr_client.exe`。
- `test.ps1` 通过 CTest：1/1。
- 实际启动窗口，日志确认 `Rules.ini`、`Art.ini`、`CONS.SHP`、`unittem.pal` 加载成功；F3 截图确认 Ready、Walk、Fire 状态证据。
- 截图仅作为人工验收证据，不将静态/自动化验证描述为人工视觉验收通过。

## 2026-08-26 - PR #1 remaining findings closure

### 本轮处理
- 复核远端 feature exact HEAD 后继续在同一分支工作，未触碰 main 或 `agent/ra2yr-corpus-lfs` worktree。
- 修正 Westwood infantry sequence 三元组语义：第三字段为 `facingStride`；显式加载每个序列的 `FrameDelayMs`/`Loop`，Death stride=0 不使用 facing。
- 将 Faction/UnitTag/AutoAcquire/ReturnFire 写入每个 Definition/Entity；同 Owner 的不同 Faction 单位可并存；Simulation 实现 Idle/Stop/Hold 自动攻击/还击与 AttackMove 追击。
- 战略能力和 Producer Selector 保留框架但清空未实现名称；加入真实 WAV 语音样例和 `voices.ini` 数据映射。
- 菜单收紧居中、单位世界缩放集中配置，并将 corpus 依赖说明标为 Development Sandbox 的 Superseded/optional。

### 真实验证
- CMake configure、MSVC build、CTest 均成功；实际 EXE 保持响应并捕获主菜单、Editor 和 F3 画面。
- 运行期日志确认 Select/Move/Attack 三条 WAV voice、Rules.ini、Art.ini、CONS.SHP 和 unittem.pal 已加载。

## 2026-08-26 - PR #1 第三次人工验收收口

### 用户目标
- 不新增第二单位、生产、AI、Trigger 或其他玩法；修复方向映射、命令语音重复、SHP 锚点/选取、Owner remap、Command Card targeting、等距 Camera/Zoom 和 Editor 工具层。

### 本轮处理
- 增加屏幕八方向 `Direction8` 与 `Art.ini` `FacingMap`，Simulation 不再把世界数学角度直接当 SHP facing。
- 将静态地形保留为 world/isometric 顶点，在 D3D11 world constant 中应用 CameraOffset/Zoom；统一世界坐标到渲染、选取、命令和小地图视口。
- 将 SHP crop/full-canvas metadata 与 ground pivot 送入 GPU，单击选取改为当前帧 screen-space bounds。
- 将所属色 remap 改为 16..31 的 16 级 ColorScheme LUT；生命条改为绿/黄/红健康状态。
- 将命令应答与 WeaponFire 分离，Voice 使用独立 stream 和 latest-intent 清队列策略；增加 F3 VoiceAck/LastVoice 观测。
- 增加 F2 可拖动/折叠/关闭的 SandboxPalette，正常生产栏不再承载编辑器 Owner 控件；F6 提供八方向开发测试循环。

### 关键结论
- 远端 feature exact HEAD 在本轮开始前为 `79ce22118da760aeee0749e582768c1623862dfe`；corpus worktree 的既有未提交内容未触碰。
- MSVC 严格警告编译已通过；CTest 和最终运行截图待本轮全部改动完成后重新执行。

### 影响文件
- `src/Engine/Core/Types.h`
- `src/GameData/Art.*`
- `src/Renderer/D3D11Renderer.*`
- `src/Simulation/Simulation.*`
- `src/Westwood/Palette/Palette.*`
- `src/Westwood/Shp/Shp.*`
- `src/Client/main.cpp`
- `tests/core_tests.cpp`
- `INI/Art.ini`
- `assets/audio/voices.ini`
- `assets/ui/strings.ini`
- `docs/FIRST_PLAYABLE_EDITOR_SLICE.md`
- `docs/THIRD_PARTY_ASSETS.md`

### 后续事项
- 重新执行 configure/build/CTest，实际运行并采集主菜单、浮窗、HUD、F3、Owner remap、Camera/Zoom 和命令交互证据；人工视觉/交互结论仍交由用户确认。

## 2026-08-26 - PR #1 第三次收口最终验证

- 从干净生成目录执行 `tools/dev/dev.ps1 -Clean`。
- 自动发现 Visual Studio 18.9.12120.119、MSVC x64、CMake 4.4.2、Ninja 1.13.2，并使用独立 `E:\Tools\vcpkg`。
- vcpkg manifest restore、CMake configure/generate、MSVC `/W4 /WX` compile/link 全部完成；CTest `1/1` 通过。
- 实际启动 `ra2yr_client.exe`，窗口保持响应；日志确认语音表、Rules.ini、Art.ini、CONS.SHP、unittem.pal 已读取。
- 采集主菜单、Editor HUD、F3、F6 八方向 Walk、Command Card target 证据；截图仅作为运行证据，不替代人工视觉/交互验收。

## 2026-08-27 - PR #1 最终收口实现

### 本轮处理
- 复核远端 feature exact HEAD 为 `7e66e8770e81b01402609da077af97a6aee2b5dd`，保持 `main` 和 `agent/ra2yr-corpus-lfs` worktree 不变。
- 将选择标识改为 camera 投影的地面椭圆，并使用 Rules 的 `SelectionRadius`；hover 和框选候选使用虚线，正式选中使用实线。
- 将显式 `OccupancyProfile=Infantry` 实现为三 subcell Simulation occupancy，增加同一目的地的 3 人上限、邻近 cell 搜索和无重叠回归测试。
- 将正式 UI geometry、图片主题和 parent-relative widget anchors 收拢到 `INI/UI.ini`，绘制和 hit-test 共用 `UiLayoutDatabase`。
- 清理正式 HUD 开发提示，保留 F3 Debug Overlay；移除旧程序/SAPI 语音样本引用，接入本地 corpus 提取的原版 E2 多样本 VoiceSet。

### 验证边界
- 已完成定向代码审查和回归测试补充；最终 configure/build/CTest、EXE 启动和截图必须在提交前重新执行。

## 2026-08-27 - PR #1 最终验证完成

- `tools/dev/build.ps1` PASS；`tools/dev/test.ps1` PASS，CTest `1/1`。
- 实际 EXE 进程保持响应，stderr 记录 SDL/D3D11 renderer 初始化、Rules/Art/CONS.SHP/unittem.pal 读取和三组 E2 VoiceSet 的九个样本加载。
- 可用运行截图保存在 `artifacts/manual-validation/`（该目录已忽略，不进入 Git）；人工视觉/交互验收仍未宣称通过。

## 2026-08-27 - UI Skin merge blocker 收口

- 复核现有 UI layout/image 分离设计，在 `INI/UI.ini` 增加九个正式面板主题图片映射。
- 新增并构建复制 `assets/ui/themes/ra2_soviet/hud/*.png` 与 `editor/sandbox.png`；正式面板外壳改由 `renderer.drawImage` 使用这些主题素材。
- 增加默认主题图片路径回归断言，完成 configure、MSVC compile/link、CTest 和实际 EXE 响应检查；未触碰独立 corpus worktree，未新增 Gameplay 范围。
