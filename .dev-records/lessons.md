# 经验总结

## 2026-08-26 - 真实兼容语料必须通过显式路径进入运行时

### 场景
最新 main 不包含大型 LFS corpus，而相邻工作区保有语料未提交改动。

### 经验
运行时通过 `RA2YR_CORPUS_ROOT` 注入语料路径，代码不硬编码开发者本机绝对路径，也不把 synthetic fixture 当作 Classic 兼容证据。

### 下次建议
在 CI 增加可选 corpus integration profile，并单独记录 LFS materialization 状态。
