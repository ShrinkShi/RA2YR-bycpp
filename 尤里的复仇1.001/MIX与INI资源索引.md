# MIX 与 INI 资源索引

这份索引只回答一个问题：**哪些原版/官方 RA2YR 文件对 `RA2YR-bycpp` 的兼容引擎实现真正有用，以及应从哪里读取。**

## 1. 优先级结论

### P0：地图地形启动链

必须优先支持：

```text
ra2.mix
ra2md.mix
  -> nested MIX
  -> MAP / YRO
  -> IsoMapPack5
  -> theater INI
  -> ISO theater MIX
  -> TMP
  -> PAL
```

当前样本中最有价值的容器包括：

- `ra2.mix`
- `ra2md.mix`
- `MULTIMD.MIX`
- `MAPSMD03.MIX`
- 官方 `*.yro`
- `local.mix` / `localmd.mix`
- `cache.mix` / `cachemd.mix`
- `isogen.mix` / `isogenmd.mix`
- `isotemp.mix` / `isotemmd.mix`
- `isosnow.mix` / `isosnomd.mix`
- `isourb.mix` / `isourbmd.mix`
- `isodes.mix` / `isodesmd.mix`
- `isoubn.mix` / `isoubnmd.mix`
- `isolun.mix` / `isolunmd.mix`
- 小型剧院辅助 MIX：`tem.mix`、`sno.mix`、`urb.mix`、`des.mix`、`ubn.mix`、`lun.mix`

### P1：SHP / Overlay / 建筑 / 步兵 / UI

重点：

- `conquer.mix`：RA2 步兵/动画等 SHP；
- `conqmd.mix`：YR 新增步兵/动画等 SHP；
- `generic.mix` / `genermd.mix`：通用剧院图形；
- `cache.mix` / `cachemd.mix`：palette 与缓存 shape；
- `language.mix -> cameo.mix`：RA2 build cameo SHP；
- `langmd.mix -> cameomd.mix`：YR build cameo SHP。

### P2：VXL / HVA

重点：

- `ra2.mix -> local.mix`
- `ra2md.mix -> localmd.mix`

`local*.mix` 同时也是大量 INI 与 UI 图形的主要来源。

## 2. 当前已实测的 nested MIX

### `ra2.mix`

| nested MIX | 字节数 | 主要意义 |
|---|---:|---|
| `cache.mix` | 196,632 | palette / cached SHP |
| `local.mix` | 34,793,784 | RA2 INI、VXL/HVA、部分 UI |
| `conquer.mix` | 16,284,016 | RA2 infantry / animation SHP |
| `generic.mix` | 15,810,386 | generic theater graphics |
| `isogen.mix` | 11,992,046 | generic isometric resources |
| `isotemp.mix` | 29,171,410 | Temperate TMP/iso resources |
| `isosnow.mix` | 28,758,698 | Snow TMP/iso resources |
| `isourb.mix` | 31,811,402 | Urban TMP/iso resources |
| `tem.mix` | 10,850 | Temperate theater helper/palette data |
| `sno.mix` | 10,898 | Snow theater helper/palette data |
| `urb.mix` | 10,850 | Urban theater helper/palette data |
| `temperat.mix` | 2,728,266 | Temperate theater graphics |
| `snow.mix` | 18,421,274 | Snow theater graphics |
| `urban.mix` | 2,726,218 | Urban theater graphics |
| `load.mix` | 25,478,184 | loading/front-end graphics |
| `neutral.mix` | 45,571,600 | neutral/civilian resources |
| `sidec01.mix` | 2,099,412 | side UI/resources |
| `sidec02.mix` | 2,102,564 | side UI/resources |

> `ra2.mix` 索引共 21 项；上表只记录本轮已通过已知文件名哈希确认身份的条目，不对尚未命名的哈希强行猜名。

### `ra2md.mix`

| nested MIX | 字节数 | 主要意义 |
|---|---:|---|
| `cachemd.mix` | 10,424 | YR cached resources |
| `localmd.mix` | 4,819,480 | YR INI、VXL/HVA、部分 UI |
| `conqmd.mix` | 6,226,784 | YR infantry / animation SHP |
| `genermd.mix` | 12,567,594 | YR generic theater graphics |
| `isogenmd.mix` | 6,637,126 | YR generic iso additions |
| `isotemmd.mix` | 5,053,802 | YR Temperate iso additions |
| `isosnomd.mix` | 8,662,870 | YR Snow iso additions |
| `isourbmd.mix` | 5,198,826 | YR Urban iso additions |
| `des.mix` | 12,946 | Desert helper/palette data |
| `isodes.mix` | 35,742,754 | Desert main iso/TMP resources |
| `isodesmd.mix` | 1,929,046 | Desert YR iso additions |
| `ubn.mix` | 12,946 | NewUrban helper/palette data |
| `isoubn.mix` | 32,364,926 | NewUrban main iso/TMP resources |
| `isoubnmd.mix` | 9,632,338 | NewUrban YR iso additions |
| `lun.mix` | 12,946 | Lunar helper/palette data |
| `isolun.mix` | 23,541,462 | Lunar main iso/TMP resources |
| `isolunmd.mix` | 3,408,654 | Lunar YR iso additions |
| `snowmd.mix` | 12,552,046 | YR Snow theater additions |
| `loadmd.mix` | 13,080,744 | YR loading/front-end graphics |
| `sidec02md.mix` | 1,823,972 | YR side UI/resources |

> `ra2md.mix` 索引共 25 项；同样只报告已通过哈希实测命中的名字。

## 3. YR 1.001 INI 的正确来源

这是本轮最容易踩坑的地方。

### 不要把 `localmd.mix` 里的 `rulesmd.ini` 当成 1.001 最终规则

本样本实测：

- `ra2md.mix -> localmd.mix -> rulesmd.ini`：742,958 B，属于 YR 1.000 基线版本；
- `expandmd01.mix -> rulesmd.ini`：743,215 B，属于 **YR 1.001 patch effective version**。

同理：

- `localmd.mix -> soundmd.ini`：99,292 B；
- `expandmd01.mix -> soundmd.ini`：99,392 B，**1.001 应以该版本为准**。

所以未来 content precedence 至少要表达：

```text
base RA2
  < YR ra2md/localmd
  < YR official patch expandmd01
  < loose/mod/expand/ecache 等更高优先级覆盖（后续单独研究冻结）
```

## 4. 当前实测存在的 RA2 INI（`ra2.mix -> local.mix`）

对兼容引擎有直接参考价值：

```text
art.ini
ai.ini
battle.ini
coopcamp.ini
eva.ini
key.ini
keyboard.ini
mapsel.ini
mpbattle.ini
mpcoop.ini
mpduel.ini
mpmeat.ini
mpmodes.ini
mpmw.ini
mpnaval.ini
mpsiege.ini
mpunholy.ini
mission.ini
rmg.ini
rules.ini
snow.ini
sound.ini
temperat.ini
theme.ini
tutorial.ini
ui.ini
urban.ini
```

这些内容即使目标是 YR 1.001 也不能简单丢弃，因为 YR 大量继承 RA2 基础资源与行为。

## 5. 当前实测存在的 YR INI（`ra2md.mix -> localmd.mix`）

```text
artmd.ini
aimd.ini
battlemd.ini
coopcampmd.ini
desertmd.ini
evamd.ini
key.ini
lunarmd.ini
mapselmd.ini
mpbattlemd.ini
mpcoopmd.ini
mpduelmd.ini
mpmeatmd.ini
mpmodesmd.ini
mpmwmd.ini
mpnavalmd.ini
mpsiegemd.ini
mpunholymd.ini
missionmd.ini
rmgmd.ini
rulesmd.ini        # 1.000 基线；1.001 被 expandmd01 覆盖
snowmd.ini
soundmd.ini        # 1.000 基线；1.001 被 expandmd01 覆盖
temperatmd.ini
thememd.ini
uimd.ini
urbanmd.ini
urbannmd.ini
```

另外 `expandmd01.mix` 实测存在：

```text
rulesmd.ini        # 1.001 effective
soundmd.ini        # 1.001 effective
```

## 6. 各类 INI 只需要知道到什么程度

本目录不是逐字段百科，因此只给开发定位：

| INI | 用途 |
|---|---|
| `rules(md).ini` | 核心对象/武器/弹头/单位等规则数据 |
| `art(md).ini` | 图形资源名、动画、SHP/VXL/HVA 表现关联等 |
| `ai(md).ini` | AI 相关配置 |
| `sound(md).ini` | 游戏音效条目 |
| `theme(md).ini` | 音乐主题条目 |
| `eva(md).ini` | EVA/语音事件关联 |
| `battle(md).ini` | 战役列表 |
| `mission(md).ini` | 单人任务列表 |
| `mapsel(md).ini` | 战役/任务选择相关 |
| `temperat(md).ini` / `snow(md).ini` / `urban(md).ini` / `desertmd.ini` / `urbannmd.ini` / `lunarmd.ini` | 各 theater 的 terrain/tile 控制 |
| `ui(md).ini` | UI/Advanced Command Bar 等 |
| `rmg(md).ini` | 随机地图生成配置 |
| `mp*.ini` | 多人模式/预设规则 |
| `coopcamp(md).ini` | 合作战役/合作内容配置 |
| `keyboard.ini` / `key.ini` | 键位/快捷键相关基础配置 |

## 7. 为什么 `Blowfish.dll` 不作为 MIX 实现依赖

本轮实际读取了 encrypted `ra2.mix`、`ra2md.mix` 和 `expandmd01.mix`，证明 encrypted MIX 的头部可以独立实现解析。

因此：

- `Blowfish.dll` 可以保留为旧程序生态研究对象；
- **C++ 引擎不应通过加载原版 `Blowfish.dll` 来实现 MIX reader**；
- 应在 `Westwood/Mix` 层自己实现 Westwood key block、Blowfish header decrypt、CRC32 filename lookup、checksum 与 nested MIX。

这样才能满足项目“核心 legacy reader 自研、跨平台层解耦、可测试、可 fuzz”的架构纪律。
