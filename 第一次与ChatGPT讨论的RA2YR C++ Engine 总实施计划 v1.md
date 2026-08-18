# 第一次与ChatGPT讨论的RA2YR C++ Engine 总实施计划 v1

> 记录日期：2026-08-18  
> 项目仓库：`ShrinkShi/RA2YR-bycpp`  
> 文档性质：第一次正式架构讨论、目标冻结与长期实施路线记录  
> 状态：**参考性基线，不是不可修改的圣经。后续如有研究证据、实测数据或更优方案，应通过新的设计记录更新，而不是为了迎合本文件而忽略事实。**

---

## 0. 这份文档为什么存在

本项目不是从零开始凭空产生的。此前已经尝试过 Godot、Unity 等路线，也做过大量 Red Alert 2: Yuri's Revenge（以下简称 RA2YR / YR）格式研究、运行时基础、Simulation、VXL/SHP/TMP/MIX 等工作。

问题不在于“完全没有代码”，而在于此前的开发长期出现了一个严重偏差：**底层 foundation、parser、自动化测试数量不断增长，但玩家真正打开程序时，看到的仍然是程序化绿地、蓝色矩形、少量不完整 VXL，甚至连场景配置都可能仍在 synthetic mode。**

这暴露了两个根本问题：

1. 重编辑器、隐式场景状态的游戏引擎对 AI coding 不友好，代码并不总是运行时真实状态；
2. 项目验收标准错误地把“reader 能解析”“测试全绿”“某层接口存在”等同于“客户端已经完成”。

因此本次讨论决定不再继续围绕 Unity 修补，而是重新定义项目：

> **使用 C++23 开发一个数据驱动、面向 RA2YR 兼容的专用 RTS 游戏引擎 / 加载器，直接读取原版 YR 与传统 MOD 的 MIX、INI、MAP、SHP、VXL、HVA、TMP、PAL、WAV、AUD、CSF 等内容，目标是在兼容模式下达到类似 `gamemd.exe` 的资源识别与游戏运行效果，同时以现代工程方式解决性能、稳定性、扩展性和渣机优化问题。**

这份文件要保留的，不只是最终计划，还包括**为什么这样决定、哪些路线被讨论过、哪些坑必须避免**。后续开发者、AI Agent、维护者读完后，应该理解项目的背景，而不是只看到一堆没有来源的技术规则。

---

# 第一部分：第一次讨论的思路与决策记录

## 1. 为什么开始考虑离开 Unity

第一次讨论的直接诱因来自 Unity 阶段的人工复验。

开发期间已经存在大量 Core、Simulation、Presentation、格式 reader 和测试，但实际 Game View 曾出现：

- 程序化纯绿色地形；
- synthetic 蓝色矩形作为实体 fallback；
- 少数 VXL 虽能显示但远未形成完整 RA2YR 画面；
- TMP/theater/ore/resource presentation 没有真正接到画面；
- 自动化测试全绿，但人类试玩仍然 FAIL；
- 更典型的是代码已经有 `StrictRealContent` 路线，而实际人工验收 `.unity` scene 仍被序列化为 `syntheticMode = true / strictRealContent = false`。

这说明对 AI coding 而言，Unity 存在大量“代码之外的事实”：

- `.unity` YAML；
- Prefab；
- Inspector serialized state；
- `.meta` GUID；
- GameObject/Component 生命周期；
- Material/Shader/Scene 绑定；
- EditMode/PlayMode；
- 导入器和编辑器状态。

开发者或 AI 可能读完 C# 后认为某功能已经完成，但真正运行时仍被场景里的一个序列化布尔值反转。

因此核心诉求从“换一个画面更好的引擎”逐渐变为：

> **让代码本身成为主要事实来源，减少隐藏的编辑器状态，让整个资源、Simulation、Renderer 和客户端链路都能被 AI 和人通过源码直接追踪。**

需要特别说明：讨论中也明确认识到，Unity 本身并不是“渲染能力差”。之前画面差主要是因为真实 TMP、Overlay、SHP、资源组合等没有闭环，而不是 Unity GPU 能力不足。换引擎不能自动解决错误的验收体系。

---

## 2. 曾讨论过的候选路线

### 2.1 MonoGame / C# code-first

最初认为 MonoGame 是非常合适的折中：

- 可以大量复用现有纯 C# Core / Simulation / Formats；
- code-first，没有 Unity 大量 Inspector/Scene 隐式状态；
- 能直接把 MIX 中解码出的像素、顶点上传到 GPU；
- 很适合等距 RTS、SHP、VXL、TMP 等专用 Renderer。

如果目标只是“减少 Unity magic，又最大化复用 C#”，MonoGame 是非常强的选择。

### 2.2 Godot C#

Godot 的 `.tscn` 比 Unity scene YAML 更易读，也可以继续使用 C#。但它仍然是完整的 SceneTree/Node/Resource/Inspector 驱动引擎，仍存在代码与编辑器绑定状态分离的问题。

因此 Godot 被认为比 Unity 更适合 AI，但没有根治“引擎有太多隐式约定”的问题。

### 2.3 raylib / raylib-cs

优点是极度 code-first、状态透明、AI 易读；缺点是更底层，需要自建大量管理层。适合技术 PoC，但若长期项目需要成熟现代 GPU abstraction，则还要评估。

### 2.4 Stride / Wicked Engine 等完整 C++ 引擎

优点是 C++、开源、完整 Renderer/Editor；问题是又会重新引入另一套 Scene/ECS/Asset/Editor 假设。既然已经愿意承担 C++ 重构成本，就没有必要再次被一个大通用引擎绑住。

### 2.5 C++ 自研专用引擎

最终用户提出：如果改用 C++，是否干脆从头开发游戏引擎，以巨大工程量换完全可控。

讨论后形成的结论是：

> **自研“RA2YR 专用薄引擎”是合理路线，但不能把“自研”理解为 Win32、音频设备、窗口系统、字体栅格化、每个图形 API 全部自己写。应自己掌握决定兼容性和游戏体验的核心层，而把平台基础设施交给成熟库。**

因此选择：

- C++23；
- SDL3 做平台抽象；
- Windows 首发自己写 D3D11 renderer backend；
- 未来需要跨平台时再增加其他 graphics backend；
- MIX/VFS、Westwood 格式、Rules/Art、Simulation、Renderer semantics、MOD compatibility 自研。

---

## 3. 项目的本质不是“复刻画面”，而是替代 `gamemd.exe` 的数据消费能力

讨论中对“完全兼容”进行了重新定义。

只做到：

- 能打开 `.mix`；
- 能 parse `.shp`；
- 能显示一个 `.vxl`；

远远不够。

真正的目标包含：

```text
文件发现 / MIX 搜索顺序
        ↓
嵌套 MIX / expansion / override
        ↓
INI / Rules / Art / Sound / AI
        ↓
逻辑资源名解析
        ↓
SHP / VXL / HVA / TMP / PAL / WAV / AUD / CSF / MAP
        ↓
地图 / 单位 / 建筑 / 资源 / 音频 / UI
        ↓
Simulation 与玩家客户端
```

即：

> **传统 YR MOD 的文件尽量原样放入本引擎，无需预转换、无需 Unity importer、无需重新制作素材，就能被识别和运行。**

这也是为什么 FinalAlert 2（FA2）仍然有价值：如果我们真正兼容 YR MAP 和相关数据语义，那么 FA2 可以继续作为传统地图编辑器，而无需在项目初期自研地图编辑器。

但 FA2 不能反向限制未来 Enhanced/Modern 扩展；现代扩展可以超出 FA2 数据模型。

---

## 4. “完全可控”并不等于“零第三方依赖”

讨论明确否决了“什么都自己造”的极端路线。

建议的边界：

### 允许成熟第三方基础设施

- SDL3：Window/Input/Platform；
- CMake：构建；
- vcpkg manifest mode：第三方依赖版本管理；
- 测试、压缩、开发工具等基础库，按需要引入；
- 后续 Lua 可作为 Modern MOD 脚本层。

### 必须自己掌握的核心

- Westwood MIX/VFS 语义；
- SHP/VXL/HVA/TMP/PAL/MAP 等 reader 和 runtime semantics；
- Rules/Art/Sound/AI 数据解析与组合；
- RA2YR content precedence；
- Simulation；
- Pathfinding/Movement 策略；
- Classic Renderer semantics；
- Compatibility profiles；
- Ares/Phobos 行为语义兼容。

核心原则：

> **成熟库可以替我们解决通用基础设施，但不能让第三方库成为 RA2YR 兼容语义的权威。**

---

## 5. C++ 的风险：不能把项目写成模板元编程屎山

C++ 自研引擎并不天然比 Unity 更适合 AI。若项目充斥：

- 模板元编程；
- CRTP；
- 巨型宏；
- 隐式 Service Locator；
- Singleton；
- 多层继承；
- RTTI 驱动“魔法”；
- 过度抽象的泛型 framework；

则 AI 可读性可能比 Unity 更差。

因此讨论确定：优先简单、显式的数据结构和模块接口。

例如倾向：

```cpp
class MixArchive {
public:
    Result<FileView, MixError> Open(FileId id) const;
};
```

而不是为了抽象而把 Archive 做成十几层 policy template。

---

## 6. 最重要的经验：以后不允许“foundation 完成 = milestone 完成”

这是整个 C++ 重启项目最重要的制度之一。

以后任何阶段都必须区分：

```text
Parser success
!= Runtime presentation success

Runtime presentation success
!= Player interaction success

Unit tests green
!= Real corpus compatibility

Real corpus compatibility
!= Human playable pass
```

每个里程碑至少需要结合：

1. Code；
2. Tests；
3. Real corpus；
4. Runtime output；
5. Performance；
6. Human verification。

P0 的最典型规则：

> 即使 MIX/MAP/TMP/PAL 测试全部通过，只要程序窗口里没有真正显示从 `ra2yrmix` 读取出的 RA2YR 地形，P0 就没有完成。

---

# 第二部分：项目目标与兼容政策

## 7. 最终项目定位

`RA2YR-bycpp` 定位为：

> **C++23 开发的数据驱动 RA2YR 兼容 RTS 游戏引擎 / runtime loader。**

长期目标：

1. Stock Yuri's Revenge 1.001；
2. 传统纯数据 YR MOD；
3. Ares 配置/行为语义；
4. Phobos 配置/行为语义；
5. Modern/Enhanced MOD API。

不要求直接加载给 `gamemd.exe` 注入的 Ares/Phobos 原生 DLL ABI；我们实现其公开/研究确认的配置和行为语义。

---

## 8. “原版 Bug”政策

不采用简单的“bug-for-bug 永久复制”，也不简单全部修掉。

引擎定义：

```text
Legacy Behavior Options
```

原版的可观察 gameplay quirks 可以作为房间特色设置，与：

- Super Weapons；
- Crates；
- 其他游戏规则；

类似，由房主选择是否启用。

建议分类：

- `RequiredCompatibilityQuirk`：为兼容特定内容必须存在；
- `OptionalLegacyQuirk`：可以作为原版特性开关；
- `Security/StabilityBug`：越界、UAF、内存泄漏、崩溃等绝不复制。

兼容原版行为，不等于复刻原版的内存破坏。

---

## 9. Classic 与 Enhanced

### Classic

先开发，优先级最高：

- 原版资源语义；
- 原版视觉与客户端行为；
- 原版 GameSpeed/timing（以研究为准）；
- 传统 MOD 兼容。

### Enhanced

Classic 稳定后再加入：

- 更现代图形；
- 更高质量光照/阴影/AA；
- 现代资产 Provider；
- Modern MOD Lua；
- 更高级寻路/编队；
- 热重载增强；
- 其他突破原版限制的功能。

Renderer 增强不得改变 authoritative Simulation。

---

# 第三部分：冻结的技术架构

## 10. 技术栈

- Language：**C++23**；
- Build：**CMake**；
- Primary compiler：**MSVC**；
- Secondary compiler：**clang-cl**；
- Platform layer：**SDL3**；
- Windows graphics：**自研 D3D11 Renderer**；
- 最低 D3D feature level：**11_0**；
- Dependency management：**vcpkg manifest mode + 锁定版本**；
- Engine config：**TOML**；
- Legacy game data：继续使用 **INI**；
- Windows 10/11 x64 为正式首发平台；
- 代码架构不得故意阻止未来 Linux/macOS backend。

---

## 11. 推荐目录/模块

```text
RA2YR-bycpp/
├── engine/
│   ├── core/
│   ├── platform/
│   ├── graphics/
│   ├── audio/
│   ├── jobs/
│   └── diagnostics/
├── westwood/
│   ├── mix/
│   ├── ini/
│   ├── csf/
│   ├── pal/
│   ├── shp/
│   ├── vxl/
│   ├── hva/
│   ├── tmp/
│   ├── aud/
│   ├── pcx/
│   └── map/
├── content/
│   ├── vfs/
│   ├── sources/
│   ├── resolver/
│   ├── cache/
│   └── profiles/
├── gamedata/
│   ├── rules/
│   ├── art/
│   ├── sound/
│   ├── ai/
│   ├── theater/
│   └── scenario/
├── simulation/
│   ├── world/
│   ├── components/
│   ├── missions/
│   ├── movement/
│   ├── pathfinding/
│   ├── combat/
│   ├── economy/
│   ├── production/
│   ├── ai/
│   └── triggers/
├── renderer/
│   ├── terrain/
│   ├── shp/
│   ├── voxel/
│   ├── overlay/
│   ├── effects/
│   ├── shroud/
│   └── ui/
├── client/
│   ├── input/
│   ├── camera/
│   ├── selection/
│   ├── commands/
│   ├── sidebar/
│   └── menus/
├── modding/
├── tools/
├── benchmarks/
└── tests/
```

CMake target 负责 enforcing 单向依赖。禁止靠“开发者记得不要 include”维持架构。

---

## 12. Entity / Simulation

选择：**EntityId + 自研 data-oriented component stores**。

不采用：

- 深层 OOP `Tank : Vehicle : Techno : Object`；
- 第一阶段直接引入通用大型 ECS；
- 全局 Singleton 管理一切。

典型 component store：

- Position；
- Health；
- Owner；
- Mission；
- Locomotor；
- Weapon；
- Target；
- Veterancy；
- Cargo；
- Production；
- AI state。

Simulation authority 从第一天按 deterministic lockstep 友好方式设计。

---

## 13. 确定性（Determinism）

已冻结决策：**15D**。

原则：

- authoritative Simulation 整数/定点优先；
- deterministic RNG；
- stable iteration order；
- 明确的 update phases；
- 线程完成顺序不能决定逻辑结果；
- Renderer 可以正常使用 float/SIMD/GPU float/插值。

目标：

```text
同初始状态 + 同随机种子 + 同命令序列
=> 相同 Simulation frame 得到相同 authoritative state
```

这是未来 lockstep 联机、reproducible bug、replay/debugging 的基础。

---

## 14. Timing Research

已冻结决策：**16C**。

不凭感觉规定 30Hz 或 60Hz。

先建立专项：

`R-TIME — gamemd.exe Timing Research`

至少研究：

- GameSpeed 0~6；
- wall clock 与 game frame；
- movement；
- ROF；
- animation；
- build time；
- superweapon；
- triggers；
- radiation/effects；
- AI；
- 掉帧时原版如何推进。

最终形成：

`YR Timing Compatibility Specification`

Classic 使用研究确认的原版 timing semantics；Renderer refresh rate 与 Simulation 解耦。

---

## 15. GameSpeed

已冻结决策：**17D**。

### Classic

精确遵守研究确认的 YR GameSpeed semantics。

### Enhanced

以后允许：

- 0.5×；
- 1×；
- 1.5×；
- 2×；
- Pause；
- single-frame step。

目标允许“原版游戏逻辑速度 + 60/120/144/240Hz Renderer”，而不是提高 Renderer FPS 就把游戏整体加速。

---

## 16. 多线程

已冻结：18C。

权威 Simulation 使用确定阶段：

```text
Input
→ Commands
→ Missions
→ Movement
→ Combat
→ Economy
→ AI
→ Commit
```

安全并行候选：

- MIX / file IO；
- asset decode；
- cache；
- render preparation；
- pathfinding jobs；
- visibility queries；
- 非权威计算。

并行任务最终结果必须按稳定 key 排序/归并，不能让 race timing 改变 Simulation。

---

## 17. 内存策略

已冻结：19C。

默认：

- RAII；
- `std::vector`；
- `std::array`；
- `std::span`；
- `std::unique_ptr`；
- `std::optional`；
- 明确 ownership。

只有 profiler 证明热点后，才局部采用：

- Arena；
- Pool；
- Frame allocator；
- Slab。

禁止为了“看起来高性能”从第一天到处自定义 allocator。

---

## 18. 错误处理

已冻结：35B。

可预期错误使用显式 Result，例如：

```cpp
Result<MixArchive, MixError>
Result<ShpDocument, ShpError>
Result<MapDocument, MapError>
```

坏 MOD/坏资源必须：

- 不越界；
- 不 null dereference；
- 不崩；
- 明确 diagnostic；
- fail closed。

Exception 只允许在极少数不可恢复/第三方异常边界转换使用。

---

## 19. 日志、Assert 与 Fatal

结构化日志至少包含：

- Timestamp；
- Severity；
- Subsystem；
- ErrorCode；
- LogicalAsset；
- Context。

Debug：内部 invariant 被破坏时立即 assert。

Release：

- 外部坏数据 => explicit Result/diagnostic；
- 内部 invariant 已损坏 => 保存日志/crash dump 后受控终止。

不能吞掉错误后继续运行损坏的 Simulation。

---

## 20. 编译质量

已冻结：25C。

自有代码：

```text
MSVC:    /W4 /WX
clang-cl: -Wall -Wextra -Werror
```

第三方依赖 warning 隔离。

任何新的自有代码 warning 都必须阻断 build，而不是积累成未来技术债。

---

# 第四部分：资源系统与格式兼容

## 21. Virtual File System

VFS 是核心系统之一：

```text
VirtualFileSystem
├── MixMount
│   └── nested MIX
├── LooseMount
└── Memory/Test Mount
```

至少存在两个 resolution profile：

### YRCompatibility

目标是研究并复制 `gamemd.exe` 的：

- root archive discovery；
- expansion archive；
- nested MIX；
- filename ID；
- override/search precedence；
- map/local content 规则。

### Modern

未来允许：

- loose PNG override SHP；
- modern model override VXL；
- OGG/WAV override AUD；
- modern package；
- MOD layer。

Simulation 与 GameData 尽量使用 LogicalAssetId，不依赖某个具体文件扩展名。

---

## 22. Legacy 格式范围

第一阶段兼容目标至少覆盖：

- MIX；
- INI；
- MAP；
- SHP(TS)；
- VXL；
- HVA；
- TMP；
- PAL；
- AUD；
- WAV；
- CSF；
- PCX；
- IsoMapPack5；
- OverlayPack；
- OverlayDataPack；
- PreviewPack。

如果后续真实 `gamemd.exe` 路径证明还有必要格式，则继续纳入。

不能把兼容性定义为“扩展名看起来支持”，而必须达到 runtime semantics。

---

## 23. GameData 完全数据驱动

核心数据来源：

- `rules.ini / rulesmd.ini`；
- `art.ini / artmd.ini`；
- `sound.ini / soundmd.ini`；
- `ai.ini / aimd.ini`；
- map overrides；
- mode overrides；
- CSF。

禁止 C++ 业务逻辑硬编码“HTNK 是某坦克”“某矿石一定是某文件名”来绕过 Rules/Art。

理想链路：

```text
Rules Type
→ Art Section
→ Image/Animation/Asset Logical ID
→ VFS
→ Decoder
→ Runtime Asset
```

---

## 24. Cache

已冻结：39C。

```text
Source MIX / Loose asset
      ↓
Decoded memory cache
      ↓
Optional derived disk cache
```

Derived cache key 至少考虑：

- source hash；
- LogicalAssetId；
- decoder version；
- render profile。

修改 MOD 后缓存必须可靠失效。Cache 是性能层，不得成为内容 authority。

---

## 25. Hot Reload

用户明确要求最终支持热重载。它不是 Stock YR 必须特性，而是现代开发能力。

Development Mode 可逐步支持：

- Rules/Art/Sound INI reload；
- loose modern asset reload；
- shader reload；
- MIX explicit remount/reload。

每类数据需要声明 reload safety：

- `LiveSafe`；
- `RecreateRequired`；
- `MatchRestartRequired`；
- `EngineRestartRequired`。

不能简单把新 Rules 覆盖到已经运行的对象内存后假设一切安全。

---

# 第五部分：Renderer 与画面

## 26. D3D11 Renderer

首发 Windows renderer：**Direct3D 11, Feature Level 11_0 minimum**。

目标不是开发通用 3A 引擎，而是建立 RA2YR 专用 Renderer：

- TerrainRenderer；
- TmpRenderer；
- OverlayRenderer；
- ShpRenderer；
- VoxelRenderer；
- EffectRenderer；
- Shroud/Fog；
- UiRenderer。

---

## 27. Classic Renderer 优先

已冻结：21C，且**先做 Classic**。

Classic 优先解决：

- palette；
- indexed SHP；
- player remap；
- shadow；
- SHP animation；
- TMP terrain；
- elevation；
- overlay/resource；
- VXL/HVA；
- Westwood-style voxel lighting；
- effect layering；
- fog/shroud；
- UI composition。

Enhanced Renderer 只能在 Classic 基线形成以后进入主线。

---

## 28. 分辨率

已冻结：22B。

目标原生支持：

- 4:3；
- 16:9；
- 16:10；
- 21:9；
- 720p/1080p/1440p/4K；
- 高 DPI；
- windowed/borderless/fullscreen。

Classic 保持世界比例、摄像机和 UI 语义，不使用简单粗暴的整屏拉伸。

---

## 29. Shader

已冻结：41C。

Development：允许 HLSL 快速编译/reload。

Release：使用预编译 shader bytecode，避免玩家运行时因临时编译造成明显卡顿。

---

# 第六部分：寻路、单位与玩法架构

## 30. Pathfinding

用户最终没有选择强制复刻原版路径算法，而是选择现代化策略：

> **普通单位以 A* 为基础，并允许不同单位、编队和 Locomotor 使用不同 Pathfinding Strategy。**

建议接口概念：

```text
IPathfinder
IPathStrategy
IMovementStrategy
```

初始：

- 普通陆地单位：A*。

未来候选：

- Squad/Formation pathfinding；
- Hierarchical A*；
- Flow Field；
- Naval；
- Aircraft；
- Subterranean/特殊 locomotor；
- 大规模同目标移动优化。

这里兼容目标更偏向“合法地形、单位行为、游戏规则正确”，而不是强制路径上的每个拐点都复制 `gamemd.exe`。

---

## 31. 无人口上限

项目不增加人为全局人口限制。

不存在固定：

```text
MaximumUnits = 500
```

这类现代人口帽。

但是：

- `BuildLimit` 等单位限制仍由 Rules 数据控制；
- 内存/EntityId/资源耗尽必须有安全保护；
- “无游戏人口限制”不等于允许无限分配直到 OOM 崩溃。

---

## 32. Multiplayer architecture

已冻结：29C。

第一版不立即实现网络，但 Simulation 从第一天为 deterministic lockstep 准备：

- input command stream；
- simulation frame；
- deterministic RNG；
- state hash；
- stable ordering。

以后加入多人不应要求重写整个 Simulation。

---

## 33. Save / Replay

已冻结：

- 不要求读取原版 `.sav`；
- 不要求兼容原版 replay。

未来引擎自己的 Save/Replay 另行设计。

---

# 第七部分：MOD、FA2 与扩展生态

## 34. FinalAlert 2

已冻结：32C。

目标：

- FA2 正常输出的 YR MAP 必须可直接加载；
- 兼容常见 FA2 quirks/不规范输出；
- 暂不自研地图编辑器。

未来 Modern 扩展不要求 FA2 能理解全部新字段。

---

## 35. Traditional MOD 零转换

已冻结：33B。

传统 YR MOD 的目标体验：

```text
原来的 MIX / INI / MAP / 素材
→ 放入/指向 RA2YR Engine
→ 尽可能直接运行
```

不要求开发者先转换成自有资产格式。

---

## 36. Ares / Phobos

长期路线：

1. Stock YR 1.001；
2. Traditional MOD；
3. Ares semantics；
4. Phobos semantics。

不是直接 LoadLibrary 它们用于 `gamemd.exe` 的 DLL，而是由本引擎实现相应配置和行为语义。

---

## 37. Modern MOD scripting

已冻结：45B。

在 Classic/YR/Ares/Phobos 主线成熟之后，Enhanced MOD 可以支持受控 Lua scripting。

不允许 Modern MOD 默认加载任意 native DLL 获得完整进程权限。

---

# 第八部分：性能与稳定性

## 38. 性能目标为什么重要

重置项目不能只是“比 2001 年原版更漂亮但更慢”。

用户明确要求：**效率不能低于原版 RA2YR，否则重置失去意义；同时重视渣机。**

因此性能不是 P12 才第一次考虑，而是从架构阶段就进入 benchmark 和 CI。

---

## 39. Provisional Low Benchmark

参考硬件（后续必须实机校准）：

- System RAM：4 GB；
- CPU：11th-gen Intel Core i5 class；
- GPU：integrated graphics；
- Quality：Low / Medium；
- Scenario：200 active units；
- Target：**不低于 60 FPS**。

“200 units”后续必须拆为 workload：idle/movement/pathfinding/combat 等，因为负载差异巨大。

---

## 40. Provisional High Benchmark

参考硬件：

- System RAM：8 GB；
- CPU：14th-gen Intel Core i5 class；
- GPU：RTX 4060 class；
- Quality：Maximum；
- Scenario：1000 active units；
- Target：**不低于 60 FPS**。

上述是开发期目标，不是假装已经经过硬件验证的正式规格。

---

## 41. 性能硬门禁

用户要求：在定义的官方 reference benchmark/reference hardware 上，出现 <60 FPS 要作为问题定位和优化，而不是接受为正常。

不能只看平均 FPS。正式 benchmark 至少记录：

- Average FPS；
- 1% low；
- 0.1% low；
- P95/P99 frame time；
- CPU frame time；
- GPU frame time；
- Simulation time；
- Pathfinding time；
- Render time；
- RAM；
- VRAM；
- allocation rate；
- load/startup time。

平均 100 FPS、但周期性 300ms 卡顿不能算性能通过。

---

## 42. Compatibility 与性能冲突

已冻结：50C。

Classic 保留外部可观察的兼容行为，但内部实现可以任意优化。

如果某个原版 bug/quirk 本身导致严重性能问题，则结合 `Legacy Behavior Options`：

- Compatibility/Legacy 可按需保留；
- Enhanced 默认修复。

安全漏洞、UAF、越界、内存泄漏不属于需要复制的兼容行为。

---

## 43. 稳定性门禁

长期目标：

- memory leak = 0；
- use-after-free = 0；
- double-free = 0；
- invalid read/write = 0；
- 空指针访问进入编译/静态检查/运行时防护体系；
- 长时间运行内存应趋于稳定，而不是持续上涨。

工具门禁：

- ASan（目标平台可用范围内）；
- static analysis；
- parser fuzzing；
- long-running soak；
- memory growth test；
- handle/thread/GPU resource 监控。

---

# 第九部分：CI、版本和工程治理

## 44. CI

每个 PR 至少目标覆盖：

- MSVC build；
- clang-cl build；
- unit tests；
- synthetic integration；
- parser tests；
- ASan where supported；
- static analysis；
- repository hygiene。

Nightly：

- fuzz；
- long soak；
- performance regression；
- determinism regression；
- large battle；
- full `ra2yrmix` compatibility suite（根据 corpus 和 runner 实际条件）。

---

## 45. Versioning

已冻结：47B。

使用 Semantic Versioning 管理公开 Modern MOD API / schema。

公开接口要版本化和提供迁移策略，但**内部 C++ ABI 不承诺永久兼容**，否则会严重阻止重构优化。

---

## 46. Crash reporting / privacy

已冻结：49B。

正式版：

- 本地日志/dump 可生成；
- 网络 crash report 必须用户主动同意；
- 不默认偷偷上传用户数据。

---

# 第十部分：测试 Corpus 政策

## 47. `ra2yrmix`

用户会提供名为：

```text
ra2yrmix/
```

的测试素材目录。

定位：

> **Required Development Compatibility Corpus**

硬要求：本引擎至少必须能读取并运行/呈现其中被纳入验收的内容。

它**不是 Stock YR 1.001 唯一黄金标准**。

长期测试体系应扩展为：

```text
StockYR1001
ra2yrmix
TraditionalMods
AresMods
PhobosMods
```

其中干净 Stock YR 1.001 未来承担“原版 gamemd.exe compatibility”的黄金基线。

关于 GitHub：用户计划开发阶段将 `ra2yrmix` 放入 GitHub 以方便 AI coding。工程本身应能直接使用它，但内容是否适合公开 tracked 需要由其版权/授权状态决定；这一法律/分发问题与技术上能否读取 MIX 是两回事。

---

# 第十一部分：总体实施阶段

## P0 — Original Content Boot

只做真正形成第一张真实画面所需的最小链路：

- CMake/C++23 project；
- SDL3 Window/Input foundation；
- D3D11 device/swapchain；
- basic logging/result；
- VFS foundation；
- MIX；
- MAP；
- IsoMapPack5；
- TMP；
- PAL；
- 最小 Terrain Renderer。

### P0 唯一关键人工验收

> **启动程序，必须看到从 `ra2yrmix` 的真实 MIX/MAP/TMP/PAL 链路直接读取并显示的 RA2YR 地图地形。**

禁止：

- 程序化绿色地形作为成功结果；
- 蓝色 placeholder；
- 手工把 TMP 解包成 PNG 再加载；
- parser tests 全绿就宣布 P0 完成。

P0 失败，则先停止更大规模 C++ migration，修清基本技术路线。

---

## P1 — Static Westwood World

接入：

- OverlayPack；
- OverlayDataPack；
- SHP；
- ore/resource；
- structures；
- trees/props；
- map static objects。

验收：Game Window 已经能看到真实 RA2YR 地图视觉，而不是 renderer test。

---

## P2 — Voxel World

接入：

- VXL；
- HVA；
- palette/remap；
- voxel lighting；
- vehicle sections。

验收：真实 YR 车辆正确出现在真实地图上。

---

## P3 — Player Interaction

接入：

- RA2 mouse semantics；
- camera edge scrolling；
- selection；
- box selection；
- contextual commands；
- cursor。

验收：玩家可以真正选择和控制单位。

---

## P4 — Movement

接入：

- A*；
- terrain passability；
- occupancy；
- collision；
- Locomotor foundation；
- path strategy abstraction。

验收：大量单位能在真实地图上合法移动，不穿越不允许区域。

---

## P5 — Economy Vertical Slice

接入：

- ore；
- Harvester；
- refinery；
- unload；
- credits；
- production；
- power。

验收：真实可见的采矿→返回矿厂→卸矿→资金增长→建造循环。

---

## P6 — Combat Vertical Slice

接入：

- weapon；
- projectile；
- warhead；
- armor；
- damage；
- death；
- veterancy foundation。

验收：一场小规模遭遇战可以真实打完。

---

## P7 — Complete Classic Renderer

补齐：

- SHP animation；
- shadow；
- effects；
- terrain elevation；
- cliff/ramp；
- water；
- bridges；
- damage state；
- fog/shroud；
- classic composition order。

---

## P8 — Full GameData Compatibility

扩大：

- Rules；
- Art；
- Sound；
- AI；
- Scenario；
- Triggers；
- SuperWeapons；
- Tech tree；
- map overrides；
- game modes。

---

## P9 — UI / Audio / Game Shell

接入：

- sidebar；
- radar/minimap；
- build queue；
- menus；
- skirmish setup；
- hotkeys；
- EVA；
- music；
- sound；
- campaign boot path。

---

## P10 — Stock YR Compatibility

建立系统化 `gamemd.exe` 对照：

```text
same StockYR1001
same map
same Rules/Art
same input/scenario

 gamemd.exe
     vs
 RA2YR-bycpp
```

比较：

- resource resolution；
- map composition；
- units/structures；
- economy；
- combat；
- timing；
- gameplay semantics；
- rendering reference；
- loading/performance。

---

## P11 — Traditional MOD Compatibility

纳入多组传统纯数据 MOD 作为回归，目标尽可能零转换运行。

---

## P12 — Performance & Stability Certification

正式执行：

- Low 200 units；
- High 1000 units；
- movement/pathfinding/combat variants；
- soak；
- memory；
- startup/loading；
- CPU/GPU profiling；
- regression budgets。

---

## P13 — Ares Compatibility

逐项实现 Ares 数据/行为语义，并建立对应 compatibility matrix。

---

## P14 — Phobos Compatibility

在 Ares 基础上继续实现 Phobos 扩展语义。

---

## P15 — Enhanced Engine

Classic 主线稳定后进入：

- Enhanced lighting；
- modern shaders；
- modern assets；
- Lua；
- advanced pathfinding/formation；
- expanded hot reload；
- new MOD capabilities；
- 其他现代化特性。

注意：某些 infrastructure（例如 hot reload 框架）可以提前建设，但不能抢占 Classic compatibility 主线。

---

# 第十二部分：第一次讨论中 1~50 决策的汇总

下面把会话中的选择题和最终答案以工程决策形式集中记录，便于未来追溯。

1. 兼容目标：先 Stock YR + 传统 MOD，之后 Ares/Phobos —— **C**。  
2. 原版 Bug：改造成 `Legacy Behavior Options`，由房主决定 gameplay quirk 开关；安全/稳定性 bug 不复制。  
3. 图形：SDL3 + 自研 D3D11 renderer —— **C**。  
4. 平台：Windows 优先，架构允许未来 Linux/macOS —— **C**。  
5. Modern assets：架构预留 Provider，Stock YR 完成后扩展 —— **C**。  
6. 资源覆盖：`YRCompatibility` + `Modern` 双 Profile —— **C**。  
7. 地图编辑：优先 FA2 兼容，不急于自研地图编辑器。  
8. 性能参考：Low 4GB/11代i5级/核显/200单位；High 8GB/14代i5级/RTX4060/1000单位；目标 60FPS+，后续实测校准。  
9. 性能比较：FPS/frame time/CPU/RAM/loading/大战稳定性/现代大规模 benchmark —— **D**。  
10. C++ 标准：C++23 —— **C**。  
11. 依赖：基础设施可用成熟库，核心兼容与 Simulation 自研 —— **B**。  
12. 插件：实现 Ares/Phobos 语义，不兼容原 32bit DLL ABI —— **B**。  
13. 测试素材：用户提供 `ra2yrmix`。  
14. 仓库：独立新仓库 —— **A**。  
15. Determinism：Simulation 确定性，Renderer 可 float —— **D**。  
16. Tick/Timing：先专项研究 gamemd.exe，再冻结 Classic timing —— **C**。  
17. GameSpeed：Classic 复制研究确认的原版语义，Enhanced 再扩展，Renderer FPS 解耦 —— **D**。  
18. 多线程：有序 Simulation + 安全任务并行 —— **C**。  
19. 内存：默认 RAII/STL，热点再 arena/pool —— **C**。  
20. Entity：EntityId + data-oriented component store —— **C**。  
21. Renderer：Classic/Enhanced 双 Profile，先 Classic —— **C**。  
22. 分辨率：现代宽屏/高 DPI 原生支持 —— **B**。  
23. D3D11 最低 Feature Level：11_0 —— **A**。  
24. Compiler：MSVC 正式 + clang-cl CI —— **C**。  
25. Warning：自有代码 `/W4 /WX` + clang warnings-as-errors，第三方隔离 —— **C**。  
26. Sanitizer/static analysis：PR 门禁 + nightly soak/fuzz —— **C**。  
27. `ra2yrmix`：开发期作为可用 corpus；用户计划放 GitHub 便于开发。  
28. `ra2yrmix`：不是 Stock 黄金标准，但必须至少能读。  
29. Multiplayer：首版不做，但从第一天 deterministic lockstep-friendly —— **C**。  
30. 原版 SAV：不兼容 —— **A**。  
31. 原版 Replay：不兼容 —— **A**。  
32. FA2：兼容正常输出及常见 quirks —— **C**。  
33. Traditional MOD：尽可能零修改/零转换直接运行 —— **B**。  
34. 配置：Engine TOML；Legacy 游戏数据继续 INI —— **C**。  
35. Error handling：`Result<T, Error>` 为主 —— **B**。  
36. Logging：结构化日志 —— **B**。  
37. Assert/Fatal：Debug 强断言；Release 外部数据 fail-closed，内部 invariant 损坏 dump+终止 —— **B**。  
38. Dependency control：CMake target enforce 单向依赖 —— **B**。  
39. Cache：memory decoded cache + optional derived disk cache —— **C**。  
40. Hot reload：支持，作为开发/增强能力。  
41. Shader：Development 可编译，Release 预编译 —— **C**。  
42. Pathfinding：普通单位首先采用 A*，以后提供不同 Path Strategy；不强制复制原版每一条寻路轨迹。  
43. Population：不设人为全局人口上限，Rules 的 `BuildLimit` 等仍生效。  
44. FPS：官方 reference benchmark 中 <60FPS 视为优化问题，不接受“差不多”。  
45. Modern scripting：后期受控 Lua —— **B**。  
46. CI：PR 完整基础门禁 + Nightly fuzz/soak/perf/full corpus —— **B**。  
47. Version：SemVer，公开 MOD API/schema 版本化，内部 C++ ABI 不永久保证 —— **B**。  
48. Dependency management：vcpkg manifest mode —— **A**。  
49. Crash report：用户主动同意后才上传 —— **B**。  
50. Compatibility vs performance：Classic 外部行为兼容，内部任意优化；性能灾难型 quirk 可成为 Legacy 选项 —— **C**。

---

# 第十三部分：面向 AI Coding 的特殊开发纪律

本项目本身就是在吸取 AI coding 失败经验后重新启动，因此需要额外纪律。

## 48. Code is truth

尽量让：

- Config；
- Asset route；
- Runtime mode；
- Renderer profile；
- Test mode；

通过文本配置和源码明确表达，避免依赖不可读/隐藏编辑器状态。

## 49. 不允许 screenshot-specific hack

禁止为了让某次人工截图看起来正确而：

- hardcode map；
- hardcode one asset physical path；
- 画绿色/蓝色 placeholder 冒充 real content；
- 特判一个 VXL/SHP；
- 使用预解包 PNG 绕过 runtime reader。

## 50. AI 每轮任务必须有“禁止范围”

Codex/AI Task 必须明确：

- 当前里程碑；
- 允许改哪些模块；
- 明确不做什么；
- real corpus acceptance；
- human acceptance；
- performance gate；
- final report。

避免 AI 因“顺手优化”把一个任务无限扩大。

## 51. 不以测试数量作为成果

`1700 tests passed` 这种数字可以作为质量证据，但不能成为“功能完成”的主要证明。

应优先汇报：

```text
Real MAP loaded: YES/NO
Real TMP visible: YES/NO
Real SHP visible: YES/NO
Real VXL visible: YES/NO
Synthetic fallback: 0 / N
Human verification: PASS/FAIL
Performance budget: PASS/FAIL
```

---

# 第十四部分：现在应该怎么开始

本文件创建时，仓库仍处于最初始状态。

**下一步不应该立刻实现经济、AI、战斗、多人、Ares、Phobos。**

正确顺序：

1. 将本计划与仓库说明纳入版本控制；
2. 确认 `ra2yrmix` corpus 的实际目录和内容；
3. 建立 P0 branch/PR；
4. 初始化 CMake/C++23/MSVC/clang-cl/vcpkg/SDL3；
5. 建立最小 Window + D3D11；
6. 开始 MIX/MAP/TMP/PAL 真实链路；
7. 在屏幕上看到第一张真实 RA2YR 地图；
8. 只有 P0 Human PASS 后才进入 P1。

---

# 结语

第一次讨论最终达成的核心思想可以压缩成一句话：

> **我们不再开发“一个使用某通用游戏引擎、碰巧能导入几种 Westwood 文件的 RTS”，而是开发“一个把 RA2YR 数据格式、资源解析、运行时 Simulation 和专用 Renderer 当成第一等公民的现代 C++ 兼容引擎”。**

同时必须记住另一个同等重要的经验：

> **换成 C++ 不会自动让项目成功。真正要改变的是可观察、可验证、真实资源驱动的开发方法。以后任何 milestone 都必须以真实运行结果为最终证据，而不是以 foundation 数量和自动化测试数量代替玩家看到的实际效果。**

本文件是 2026-08-18 第一次正式讨论形成的 v1 基线。未来出现新的逆向证据、性能实测、格式研究或架构调整时，应新增 ADR/计划版本记录原因，并保留本文件作为项目历史背景与设计演进证据。
