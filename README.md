# RA2YR-bycpp

`RA2YR-bycpp` 是一个使用 **C++23** 开发的 Red Alert 2: Yuri's Revenge（RA2YR / YR）兼容 RTS 游戏引擎 / Runtime Loader 项目。

项目的长期目标不是“做一个看起来像红警2的 RTS”，而是尽可能做到：

> **`gamemd.exe` 能识别和运行的传统 RA2YR 数据与 MOD 内容，本引擎也能够直接识别、解析和运行。**

也就是说，我们希望原版游戏或传统 MOD 中的 MIX、INI、MAP、SHP、VXL、HVA 等数据能够尽量保持原样，而不是先转换成另一套专用工程格式。

---

## 项目目标

计划兼容的传统 RA2YR 数据包括但不限于：

- MIX / nested MIX；
- `rules.ini` / `rulesmd.ini`；
- `art.ini` / `artmd.ini`；
- Sound / AI 等 INI；
- MAP；
- SHP；
- VXL；
- HVA；
- TMP；
- PAL；
- WAV / AUD；
- CSF；
- PCX；
- IsoMapPack5 / OverlayPack / OverlayDataPack / PreviewPack；
- 后续经研究确认属于 YR runtime 的其他必要格式和资源覆盖规则。

传统 MOD 的目标体验是：

> **尽可能零转换运行。**

也就是说，不要求用户先把 SHP/VXL 转成现代素材，不要求预解包 MIX，也不要求把 MOD 导入某个专有编辑器工程之后才能启动。

---

## 原版兼容与现代增强

项目计划分为两个方向：

### Classic / Compatibility

优先实现：

- Stock Yuri's Revenge 1.001；
- 传统 YR MOD；
- FinalAlert 2 地图兼容；
- 原版风格资源解析、渲染和游戏逻辑；
- 后续逐步研究 Ares / Phobos 的配置与行为语义兼容。

目标是让老资源、老地图和老 MOD 尽量无需修改即可运行。

### Enhanced

在 Classic 主线稳定之后，再逐步提供现代增强能力，例如：

- 更高分辨率和高刷新率；
- 宽屏 / 高 DPI；
- 现代光照与阴影；
- 现代资源格式；
- Lua 扩展；
- 热重载；
- 更高级的编队和寻路策略；
- 其他不破坏 Classic 兼容性的现代功能。

---

## 性能目标

这个项目的意义不仅是“重新写一遍”，还包括解决老引擎在现代系统上的性能、稳定性和扩展性问题。

当前参考目标为：

### Low

- 4GB system RAM；
- 11th-gen Intel Core i5 class CPU；
- 集成显卡；
- Low / Medium 画质；
- 约 200 个活跃单位；
- 目标不低于 60 FPS。

### High

- 8GB system RAM；
- 14th-gen Intel Core i5 class CPU；
- RTX 4060 class；
- Maximum 画质；
- 约 1000 个活跃单位；
- 目标不低于 60 FPS。

这些仍是早期参考目标，后续会通过真实硬件、真实地图和正式 benchmark workload 持续校准。

稳定性方面，项目会重点防止：

- memory leak；
- use-after-free；
- double-free；
- invalid read/write；
- 长时间运行内存持续无边界上涨。

---

## 地图与 MOD

如果对 RA2YR 的 MAP 格式和相关数据做到足够完整的兼容，现有 **FinalAlert 2** 地图仍可以继续作为传统地图制作工具，而不需要因为换了新引擎就强迫所有地图作者迁移到新的编辑器。

未来如果 Enhanced 功能超出 FA2 能力，再考虑新增专门的开发工具。

传统 MOD 的核心原则仍然是：

```text
原有 MIX / INI / MAP / SHP / VXL / HVA ...
                    ↓
                RA2YR-bycpp
                    ↓
             尽可能直接运行
```

---

## 当前开发状态

项目目前处于 C++ 重启后的最早期阶段，**还不是可替代 `gamemd.exe` 的完整游戏版本**。

当前第一阶段是：

# P0 — Original Content Boot

目标只聚焦一件事：

> **从真实 RA2YR 测试内容中直接完成 MIX → MAP → TMP → PAL → D3D11 链路，并在程序窗口中真正显示出一张 RA2YR 地图。**

P0 通过后，才会继续加入：

- SHP / Overlay / 矿石 / 建筑 / 树木；
- VXL / HVA 载具；
- 鼠标选择和命令；
- A* 移动；
- 采矿、经济和生产；
- 战斗；
- 完整 Classic Renderer；
- UI、声音和游戏外壳；
- Stock YR / Traditional MOD / Ares / Phobos 兼容。

因此当前仓库主要用于研发和兼容性验证，还不建议普通玩家作为成品游戏使用。

---

## 系统方向

首发目标平台：

- Windows 10 x64；
- Windows 11 x64。

首发图形后端计划使用 Direct3D 11，同时保持核心架构与平台层解耦，为未来其他平台留下空间。

---

## 原版 / 官方资源研究索引

仓库维护 [`尤里的复仇1.001/`](./尤里的复仇1.001/) 目录，用于记录 Stock/官方资源与第三方整合包附加内容之间的边界，以及真实 RA2YR 文件在兼容引擎中的读取位置。

目前其中包括：

- 当前 YR 1.001 无过场动画整合包的文件审计；
- `ra2.mix` / `ra2md.mix` / `expandmd01.mix` 的 nested MIX 与关键 INI 路径；
- YR 1.001 `rulesmd.ini` / `soundmd.ini` 的有效覆盖来源；
- cnc-ddraw、中文字体、注册/序列号工具等第三方附加物的分类；
- 官方地图增补与基础 1.001 的分层；
- SHA-256 与机器可读 `manifest.json`。

完整原版 MIX 不直接 vendor 到本公共源码仓库；兼容性测试使用维护者本地持有的合法游戏文件，并以该目录中的哈希和资源索引进行校验。

---

## 给开发者

如果你准备参与代码、格式研究、测试、性能分析，或者让 AI Agent 参与开发，请不要只依赖本 README。

请阅读：

### [`DEVELOPERS.md`](./DEVELOPERS.md)

其中包含：

- 开发者阅读顺序；
- 项目架构约束；
- C++ 代码规范和质量门禁；
- P0～P15 milestone；
- Compatibility Corpus；
- Timing / GameSpeed；
- Pathfinding；
- Hot Reload；
- CI、性能和稳定性要求；
- AI Coding Agent 的额外规则；
- 第一次总实施计划文档的性质和使用方式。

项目第一次完整架构讨论与长期参考基线保存在：

[`第一次与ChatGPT讨论的RA2YR C++ Engine 总实施计划 v1.md`](./第一次与ChatGPT讨论的RA2YR%20C%2B%2B%20Engine%20总实施计划%20v1.md)

---

## 关于原版游戏和第三方资源

本仓库开发的是兼容引擎。

Red Alert 2、Yuri's Revenge、Westwood、Electronic Arts 以及各 MOD 中第三方素材的相关权利属于各自权利人。

引擎源码的许可证与原版游戏数据、MOD 素材的授权是不同问题。使用者应自行确保其使用的原版游戏文件和第三方 MOD 内容来源合法并符合相应授权要求。
