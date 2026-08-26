# 技术决策记录

## 2026-08-26 - 使用隔离 worktree 启动垂直切片

### 背景
原工作区在 `agent/ra2yr-corpus-lfs` 上有大量未提交语料改动，不能为了建立 feature 分支而切换或覆盖。

### 决策
从最新 `origin/main` 创建 `feature/first-playable-editor-shell` 独立 worktree。

### 原因
保留用户现有语料工作区，同时满足“不直接修改 main、从最新 main 建分支”的项目纪律。

### 代价
本轮代码位于相邻 worktree，不会自动出现在原语料 worktree 的工作区中。

### 替代方案
- 临时 stash 原工作区：会改变用户现有工作流且包含大量 untracked 语料，风险更高。

## 2026-08-26 - 动画与调试信息分层

### 决策
- 用 `Art.ini` 的 8 个方向和每个状态的独立序列计算帧号；渲染调用同时传入状态、帧和方向。
- 正式 HUD 只展示玩家可读单位字段；资源文件名、序列名、方向和帧只在 F3 覆盖层显示。

### 原因
- 避免把 Idle 错当 Walk，也避免将方向索引混入状态序列；同时保持正式 UI 不暴露实现细节。

## 2026-08-26 - 最小音频采用程序化 cue（Superseded）

### 决策
- 先接入 SDL3 音频流和菜单/单位事件，并用短程序化音调作为最小样例。

### 原因
- 本轮优先级是动画和 HUD；程序化 cue 能验证事件链路，不冒充完整单位配音。

## 2026-08-26 - 数据驱动的最小真实语音

### 决策
- 使用少量由 Windows SAPI `SpVoice` 生成的短 WAV 作为 E2 Select/Move/Attack 样例，通过 `assets/audio/voices.ini` 映射到通用 AudioCue。
- 保留程序化音调仅作为没有语音素材时的 fallback/debug，不将其称为单位语音。

### 原因
- 本轮要求验证真实语音链路，同时避免复制 1GB+ corpus 音频或把 Westwood 原始录音混入 PR。

## 2026-08-26 - 世界方向与 SHP facing 解耦

### 背景
世界网格的数学 `atan2` 轴与等距屏幕方向、Westwood infantry facing 序列不是同一个坐标约定。

### 决策
先将世界位移投影到 2:1 等距屏幕基底，分类为固定 `Direction8`（N、NW、W、SW、S、SE、E、NE），再由 `Art.ini` `FacingMap` 映射到素材 facing。

### 原因
避免 Simulation 依赖某个 CONS.SHP 的帧顺序，同时让不同素材可用数据配置替换方向映射。

### 代价
Art 配置需要显式维护方向 map；未来非 8-facing 资产需要扩展定义。

## 2026-08-26 - GPU 世界 Camera 与 UI 坐标分离

### 背景
相机移动/缩放不能触发每帧 CPU 重建静态 4096 tile 网格，也不能影响固定逻辑分辨率 HUD。

### 决策
静态 terrain buffer 存等距 world-local 顶点，D3D11 world vertex shader 读取 CameraOffset/Zoom；单位、鼠标、命令、小地图均经 `IsometricCamera` 转换，UI 仍使用 logical screen space。

### 原因
满足 edge-scroll/wheel zoom 的一致坐标链，并保持静态 terrain 的 GPU 缓存路径。

### 代价
增加一个 world pipeline 和 constant buffer，需要保证所有世界交互路径不回退到旧固定投影。

## 2026-08-26 - Owner remap 使用显式 ColorScheme LUT

### 背景
原 shader 把 192..207 当作所属色，且使用单一 owner tint，无法复现 RA2/YR 的 16 级 remap 语义。

### 决策
使用 Owner -> ColorSchemeId -> 16 级 LUT，严格重映射 palette index 16..31；非 remap index 保持原 palette。

### 原因
红蓝单位共享 CONS.SHP 时，所属色应由同一像素区域产生不同渐变，而不是覆盖整张 sprite。

### 代价
Sprite constant buffer 从单色扩展为 16 个 RGBA 项；未来更多 HouseColor 方案需要扩展 ColorScheme 资源表。

## 2026-08-26 - Command voice 与 WeaponFire 分离

### 背景
把每次 simulation attackEvent 播成 VoiceAttack 会造成连续射击时语音重复。

### 决策
按钮/快捷键的有效命令才触发 Voice acknowledgement；攻击事件只发布 WeaponFire，Voice stream 采用 latest-intent 清队列。

### 原因
命令语音和武器/Gameplay SFX 的生命周期不同；一次编队命令在客户端只发布一次 acknowledgement。

### 代价
当前 WeaponFire 没有正式素材，因此保持静音；需要后续添加独立武器音效资源。

## 2026-08-26 - 最终验证边界

### 决策

- 将 `dev.ps1 -Clean` 的实际结果记录为构建、CTest 和运行证据；不把自动化结果表述为人工视觉/交互验收通过。
- 保持 `feature/first-playable-editor-shell` 与 `agent/ra2yr-corpus-lfs` 两个 worktree 独立；corpus 的既有未提交内容不进入本次提交。

### 验证边界

- 本轮不新增第二单位、生产、ComputerPlayerAI、Trigger 或其他玩法范围。
