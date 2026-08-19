# RA2YR 1.001 Compatibility Corpus：Git LFS 导入说明

本仓库从本版本开始使用 **Git Large File Storage (Git LFS)** 保存 RA2/YR 兼容性测试所需的大型二进制资源。

## 1. 目标

Compatibility Corpus 的权威保存结构为：

```text
CNCRA2YR1.001/corpus/
├── top-level/
│   ├── ra2.mix
│   ├── ra2md.mix
│   ├── expandmd01.mix
│   ├── language.mix
│   ├── langmd.mix
│   ├── MULTIMD.MIX
│   └── MAPSMD03.MIX
├── nested/
│   ├── ra2.mix/
│   ├── ra2md.mix/
│   ├── language.mix/
│   └── langmd.mix/
├── extracted/
│   ├── ini/
│   │   ├── common/
│   │   ├── ra2/
│   │   ├── yr-base/
│   │   └── yr-1.001-patch/
│   └── strings/
├── official-maps/
├── CORPUS-SHA256SUMS.txt
└── corpus-manifest.json
```

顶层 MIX 是权威原始资源；nested MIX 单独保存是为了让格式研究和测试无需每次重复从父容器提取。

INI、CSF 和官方地图会展开保存，便于 diff、测试和人工检查。

TMP、PAL、SHP、VXL、HVA 等大量二进制资源 **不再从 nested MIX 全量重复展开一份**。它们的完整原始字节已经包含在对应 nested MIX 中；`MIX与INI资源索引.md` 负责记录位置。若某个后续测试确实需要把具体 leaf asset 独立提取，`.gitattributes` 已预先为这些扩展名配置 Git LFS。

## 2. 当前审计样本的确定规模

`tools/ra2yr-corpus/build_corpus.py` 已针对当前无过场动画版样本实测：

- 7 个 canonical top-level MIX；
- 48 个单独保存的 nested MIX；
- 56 个 INI；
- 2 个 CSF；
- 13 个官方 `.yro` 地图；
- 合计 **126 个资源文件**；
- 资源 payload 合计 **1,269,875,373 B（约 1.183 GiB）**。

生成的 `corpus-manifest.json` 和 `CORPUS-SHA256SUMS.txt` 用于证明 GitHub LFS 中的对象与本次审计样本一致。

## 3. 已完整解析的 RA2 主容器

当前 `ra2.mix` 实测共有 21 个索引条目：

- 20 个已命名 nested MIX；
- 1 个 `key.ini`。

20 个 nested MIX 为：

```text
cache.mix
local.mix
conquer.mix
generic.mix
isogen.mix
isotemp.mix
isosnow.mix
isourb.mix
temperat.mix
snow.mix
urban.mix
tem.mix
sno.mix
urb.mix
load.mix
neutral.mix
sidec01.mix
sidec02.mix
sidenc01.mix
sidenc02.mix
```

因此本次导入不存在“只识别一部分 ra2.mix 条目然后把剩余哈希忽略”的情况。

## 4. 已完整解析的 YR 主容器

当前 `ra2md.mix` 实测共有 25 个索引条目：

- 24 个已命名 nested/theater MIX；
- 1 个 `key.ini`。

24 个 MIX 为：

```text
cachemd.mix
localmd.mix
conqmd.mix
genermd.mix
isogenmd.mix
isotemmd.mix
isosnomd.mix
isourbmd.mix
des.mix
isodes.mix
isodesmd.mix
ubn.mix
isoubn.mix
isoubnmd.mix
lun.mix
isolun.mix
isolunmd.mix
snowmd.mix
loadmd.mix
sidec02md.mix
desert.mix
urbann.mix
lunar.mix
ntrlmd.mix
```

## 5. language / langmd

为了直接访问本地化与构建图标资源，额外展开保存：

```text
language.mix -> audio.mix
language.mix -> cameo.mix
language.mix -> ra2.csf

langmd.mix -> audiomd.mix
langmd.mix -> cameomd.mix
langmd.mix -> ra2md.csf
```

父 `language.mix` / `langmd.mix` 本身仍完整保存，因此没有因为只展开已命名条目而丢失其他内容。

## 6. YR 1.001 INI 覆盖

仓库同时保存：

```text
ra2.mix -> local.mix -> RA2 INI
ra2md.mix -> localmd.mix -> YR 1.000 基线 INI
expandmd01.mix -> rulesmd.ini / soundmd.ini -> YR 1.001 effective
```

`rulesmd.ini` 和 `soundmd.ini` 的 1.001 有效版本必须以 `expandmd01.mix` 中的版本为准。

## 7. Git LFS 规则

根目录 `.gitattributes` 将 corpus 下这些二进制扩展名交给 Git LFS：

```text
mix / MIX
yro
map
shp
vxl
hva
tmp
pal
csf
```

INI 保存在普通 Git 中，但显式使用 `-text`，避免 Git 自动换行转换破坏原始字节与哈希。

## 8. 可重复导入

维护工具：

```text
tools/ra2yr-corpus/build_corpus.py
tools/ra2yr-corpus/publish_lfs.ps1
```

`build_corpus.py`：

- 直接解析 encrypted Westwood MIX；
- 验证 `ra2.mix` 必须是 21 个索引条目；
- 验证 `ra2md.mix` 必须是 25 个索引条目；
- 提取确定的 nested MIX；
- 提取 INI / CSF / 官方地图；
- 生成 SHA-256 与机器可读 manifest；
- 如果换成了结构不同的 repack，会 fail closed，而不是静默当作同一黄金语料。

`publish_lfs.ps1`：

- 从最新 `origin/main` 建立 `agent/ra2yr-corpus-lfs`；
- 构建 corpus；
- 验证二进制是否命中 Git LFS filter；
- 验证当前审计样本必须生成 126 个资源、1,269,875,373 B；
- commit；
- push 分支，并由 Git LFS 上传实际二进制对象。

示例：

```powershell
py -m pip install cryptography
./tools/ra2yr-corpus/publish_lfs.ps1 -SourceDir "D:\Games\RA2YR\cnc-YR"
```

## 9. 验收要求

LFS 导入不能只看 GitHub 网页上出现 pointer 文件。最终必须从一个新的工作目录验证：

```powershell
git clone https://github.com/ShrinkShi/RA2YR-bycpp.git RA2YR-bycpp-lfs-check
git -C RA2YR-bycpp-lfs-check lfs pull
```

然后：

1. `git lfs ls-files` 能列出 corpus 二进制；
2. `ra2.mix` 实际大小为 281,888,480 B，而不是约 130 B 的 LFS pointer；
3. `ra2md.mix` 实际大小为 204,527,696 B；
4. `corpus-manifest.json` 为 126 entries；
5. 按 `CORPUS-SHA256SUMS.txt` 校验全部文件通过；
6. XCC Mixer 能直接打开拉取后的 MIX；
7. 后续 RA2YR-bycpp 的 VFS/MIX 测试应直接使用这套 corpus，而不是 synthetic 替代品。

只有以上全部通过，才可以说“资源已经完整保存在 GitHub LFS”。
