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
