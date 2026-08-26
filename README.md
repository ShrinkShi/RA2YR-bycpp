# RA2YR-bycpp

`RA2YR-bycpp` 是一个使用 **C++23** 开发的数据驱动通用 RTS 游戏引擎 / Runtime Loader 项目。**RA2YR / Yuri's Revenge 1.001 是当前第一个、也是最严格的兼容规则集和内容基线。**

项目长期目标现在同时包含两条不能互相牺牲的主线：

1. **Classic / Compatibility：**尽可能做到 `gamemd.exe` 能识别和运行的传统 RA2YR 数据与 MOD 内容，本引擎也能够直接识别、解析和运行；
2. **General RTS Runtime：**在不破坏 Classic 兼容的前提下，把生产、技能、资源、护盾、移动、状态、AI、Trigger 等逐步抽象为数据驱动的通用 RTS 机制，使不同规则集和二改 MOD 可以在同一 Runtime 上扩展。

也就是说，我们希望原版游戏或传统 MOD 中的 MIX、INI、MAP、SHP、VXL、HVA 等数据能够尽量保持原样，而不是先转换成另一套专用工程格式；同时，长期也不把引擎限制为只能表达 RA2YR 原版的规则边界。

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

### Enhanced / General RTS

在 Classic 主线稳定推进的同时，逐步提供不破坏兼容性的现代增强与通用 RTS 能力，例如：

- 更高分辨率和高刷新率；
- 宽屏 / 高 DPI；
- 现代光照与阴影；
- 现代资源格式；
- Lua 扩展；
- 热重载；
- 更高级的编队和寻路策略；
- Command / Order / Ability；
- 多生产方式与独立生产者；
- 多武器、多护盾、状态效果、通用移动能力；
- 可扩展资源、科技、老兵与基础设施体系；
- 更先进的 AI 与 Trigger；
- 其他不破坏 Classic 兼容性的现代功能。

“通用 RTS”不意味着放弃 RA2YR，而是要求新系统能够同时容纳 Classic 规则和新的混合规则集。

---

## 设计演进时间线

### 2026-08-18 — C++ 重启与 RA2YR 兼容基线

第一次完整架构讨论重新定义了项目的工程路线：从此前 Unity 阶段转向 C++23 code-first Runtime，以真实 RA2YR 内容闭环、传统格式兼容、性能与可验证性为核心。

记录：

[`第一次与ChatGPT讨论的RA2YR C++ Engine 总实施计划 v1.md`](./第一次与ChatGPT讨论的RA2YR%20C%2B%2B%20Engine%20总实施计划%20v1.md)

这份文档是项目第一次正式架构讨论、目标冻结与长期实施路线的参考性基线。

### 2026-08-25 — 通用 RTS 方向与“红色际霸”混合规则

第二次讨论进一步明确：项目长期目标不仅是复刻 / 重置 RA2YR，还要把 RA2YR 作为第一个兼容规则集，逐步建设可承载不同 RTS 玩法的数据驱动 Runtime。

“红色际霸”被确定为第一个混合规则压力测试：在同一引擎中同时容纳 RA2 人类阵营与类似 SC2 神族 / 虫族的技能、独立生产、工人建造、护盾、移动、基础设施等差异。

本次记录冻结了已经达成一致的：

- Command / Order / Ability 分层；
- RA2 全局生产 UI 与独立生产建筑的结合方式；
- 多生产方式与生产者选择；
- 固定护甲减伤、多武器、多层护盾方向；
- Power / Psionic Field / 虫族补给等基础设施共存；
- Credits 与地图采集物分离；
- 经验值老兵、击杀 / 经验归属可配置；
- 隐身、潜地、伪装、幻象的初步边界；
- 相对高度、组合式移动能力；
- 附属炮塔、状态效果、变形、航空与战略能力栏；
- AI、Trigger、统一模型作为下一阶段重点专题。

记录：

[`第二次与ChatGPT讨论的通用RTS规则框架与红色际霸设计记录 v1.md`](./第二次与ChatGPT讨论的通用RTS规则框架与红色际霸设计记录%20v1.md)

第二份文档是对第一份基线的扩展，不是替代。若两份文档涉及不同阶段目标，应按“保留 Classic 兼容 + 新增通用抽象”的原则理解；未明确冻结的问题仍应以研究和实测为准。

### 2026-08-25 — 通用 RTS AI 架构原则冻结

第三次专题讨论暂时跳过 Trigger 与最终统一数据模型，先冻结 AI 与 Simulation 之间的边界。AI 被定义为一种 Player Controller：真人、RuleBased AI、Neural AI 等最终都通过相同的 `Command → Order` 通道操作 Simulation，正常 AI 不允许直接修改资源、单位或世界状态。

本次重点确认：

- AI 默认只能读取当前玩家合法可知的结构化 Observation；
- 战争迷雾中的敌军使用 Last Known Information，不泄漏实时世界状态；
- 公平 AI 受到反应延迟和 Action Budget 约束，避免依靠无限操作频率形成“超人微操”；
- Strategic / Tactical / Micro 作为逻辑分层，但具体算法可替换；
- RuleBased AI 作为测试、Baseline 与早期训练对手必须存在；
- Neural AI 长期推荐 Replay 模仿学习 → Self-play → League 的训练路线；
- Replay、Headless 高速 Simulation、受控随机种子和尽量确定性的 Simulation 被视为 AI 基础设施；
- Observation / Action Schema 必须面向通用 RTS，不能绑定固定单位列表；
- AI Skill 与 Personality 分离；作弊 AI 可以存在，但必须与公平难度明确区分。

记录：

[`第三次与ChatGPT讨论的通用RTS AI架构原则 v1.md`](./第三次与ChatGPT讨论的通用RTS%20AI架构原则%20v1.md)

这份文档冻结的是 AI 的接口、公平性和训练基础设施边界，不冻结具体神经网络算法、模型拓扑或难度参数。

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

未来如果 Enhanced / General RTS 功能超出 FA2 能力，再考虑新增专门的开发工具。

传统 MOD 的核心原则仍然是：

```text
原有 MIX / INI / MAP / SHP / VXL / HVA ...
                    ↓
                RA2YR-bycpp
                    ↓
             尽可能直接运行
```

新的通用规则和二改 MOD 则可以在此基础上使用扩展数据定义，而不要求传统 RA2YR 内容为了新架构被强制迁移。

---

## 当前开发状态

项目目前处于 C++ 重启后的最早期阶段，**还不是可替代 `gamemd.exe` 的完整游戏版本**。

当前 `feature/first-playable-editor-shell` 分支新增第一轮可玩编辑器垂直切片：

- `C++23 + CMake + SDL3 + Direct3D 11` 客户端工程骨架；
- RA2/YR 红黑 CRT / 银灰金属控制台主菜单，使用独立图片按钮和 hover/pressed 状态；DTA/CnCNet 仅作组件结构参考；
- 同一 EXE 内的 Editor/Sandbox 模式；
- 程序化 64x64 等距草地开发地图；
- Rules 驱动 E2/M1Carbine/SA/InvisibleLow 数据读取；
- 项目自有最小运行素材中的真实 `CONS.SHP`、`unittem.pal` 读取与红蓝 owner remap；
- 选择、移动、停止、Hold、巡逻、Attack Move、攻击、扣血和死亡；
- 右下角 3 行 5 列 Command Card。

该 slice 的范围、构建与 Classic 兼容边界见 [`docs/FIRST_PLAYABLE_EDITOR_SLICE.md`](./docs/FIRST_PLAYABLE_EDITOR_SLICE.md)。其中程序化草地明确属于 Development Sandbox，不代表真实 TMP/MAP 兼容已经完成。

当前第一阶段是：

# P0 — Original Content Boot

目标只聚焦一件事：

> **先用项目自有最小素材完成 Rules/Art → SHP/PAL → D3D11 链路，并在程序窗口中稳定显示首轮编辑器切片。**

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

通用 RTS 的第二阶段设计不会改变这个近期验收顺序：**当前仍先把真实 RA2YR 内容链路闭环，再逐步把 Gameplay Runtime 泛化。**

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
- SHA-256 与机器可读 `manifest.json`；
- [`LFS语料导入.md`](./尤里的复仇1.001/LFS语料导入.md) 定义的完整 Git LFS Compatibility Corpus 方案。

大型兼容性资源采用 Git LFS 保存。当前冻结的 corpus 结构以 7 个顶层 MIX 为权威原始件，并额外保存 48 个已解析 nested MIX、关键 INI/CSF 和 13 个官方 `.yro` 地图；TMP/PAL/SHP/VXL/HVA 等大量 leaf asset 由对应 nested MIX 完整承载，避免把同一字节重复存储数倍。所有 corpus 文件都必须通过 SHA-256 manifest 与 fresh-clone LFS materialization 验收。

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

三次正式讨论记录分别保存为：

1. [`第一次与ChatGPT讨论的RA2YR C++ Engine 总实施计划 v1.md`](./第一次与ChatGPT讨论的RA2YR%20C%2B%2B%20Engine%20总实施计划%20v1.md) — C++ 重启、RA2YR 兼容与长期实施基线；
2. [`第二次与ChatGPT讨论的通用RTS规则框架与红色际霸设计记录 v1.md`](./第二次与ChatGPT讨论的通用RTS规则框架与红色际霸设计记录%20v1.md) — 通用 RTS 方向、红色际霸混合规则与后续 AI / Trigger / 统一模型专题入口；
3. [`第三次与ChatGPT讨论的通用RTS AI架构原则 v1.md`](./第三次与ChatGPT讨论的通用RTS%20AI架构原则%20v1.md) — AI Player Controller、公平 Observation / Action、三级决策、Replay / Headless / Determinism 与训练路线基线。

阅读时优先区分“已经冻结的当前规则”和“尚待研究的方向”，不要把讨论性建议直接当成实现事实。

---

## 关于原版游戏和第三方资源

本仓库开发的是兼容引擎与通用 RTS Runtime。

Red Alert 2、Yuri's Revenge、Westwood、Electronic Arts 以及各 MOD 中第三方素材的相关权利属于各自权利人。

引擎源码的许可证与原版游戏数据、MOD 素材的授权是不同问题。使用者应自行确保其使用的原版游戏文件和第三方 MOD 内容来源合法并符合相应授权要求。
