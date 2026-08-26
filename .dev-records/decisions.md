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
