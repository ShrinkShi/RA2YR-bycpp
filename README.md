# RA2YR-bycpp

一个使用 **C++23** 开发的、面向 **Red Alert 2: Yuri's Revenge** 数据兼容的现代 RTS 游戏引擎 / runtime loader 项目。

本项目的长期目标不是“做一个看起来像红警2的 RTS”，而是尽可能做到：

> `gamemd.exe` 能识别和运行的传统 RA2YR 数据与 MOD 内容，本引擎也能够直接识别、解析和运行。

也就是说，传统资源应尽量保持原样：

- MIX
- INI / Rules / Art / Sound / AI
- MAP
- SHP
- VXL
- HVA
- TMP
- PAL
- WAV / AUD
- CSF
- PCX
- IsoMapPack5 / OverlayPack / OverlayDataPack / PreviewPack
- 后续经研究确认属于 YR runtime 的其他必要格式

传统 MOD 的目标体验是“尽可能零转换”：不要求先导入某个编辑器、不要求把 SHP/VXL 转成现代素材、不要求预解包 MIX 才能运行。

---

## 给开发者和 AI Agent 的第一阅读顺序

在开始写代码之前，请先阅读：

### [`第一次与ChatGPT讨论的RA2YR C++ Engine 总实施计划 v1.md`](./第一次与ChatGPT讨论的RA2YR%20C%2B%2B%20Engine%20总实施计划%20v1.md)

这不是普通的 TODO，也不是必须永远逐字执行的“圣经”。

它的作用是：

1. **项目背景记录**：说明为什么项目从此前的 Godot/Unity 路线转向 C++ 自研专用引擎；
2. **第一次架构讨论记录**：保留维护者与 ChatGPT 第一次完整讨论中的思路、取舍、踩坑经验和 1~50 项关键决策；
3. **长期参考基线**：记录项目最初冻结的兼容目标、技术栈、Simulation、Renderer、性能、稳定性、CI、MOD 和阶段计划；
4. **防止失忆和重复争论**：未来开发者或 AI 不应在不了解历史原因的情况下随意把项目重新带回“重编辑器 + synthetic demo + foundation 完成即宣布 milestone 完成”的模式；
5. **作为设计演进的起点**：未来如果实测、逆向研究或工程现实证明某项设计不合理，应新增 ADR / 新版本计划并记录为什么修改，而不是因为旧文档写过就拒绝事实。

因此正确态度是：

> **认真阅读、理解背景、默认遵循；遇到新证据时允许有记录地修订。**

---

## 当前冻结的核心方向

### 语言与工程

- C++23
- CMake
- MSVC 为主要 Windows 工具链
- clang-cl 作为第二编译器和 CI 验证
- vcpkg manifest mode 管理并锁定第三方依赖
- 自有代码高 warning level，并将 warning 视为 error

### 平台

- Windows 10 x64
- Windows 11 x64
- SDL3 负责 Window/Input/Platform 基础层
- 首发图形 backend：自研 Direct3D 11，最低 Feature Level 11_0
- Core / Simulation / Content 不应被 Windows API 污染，未来可增加其他平台 backend

### 核心自研范围

以下内容是本项目的核心能力，不能把第三方库变成其语义权威：

- Westwood MIX / VFS
- RA2YR content discovery / precedence
- SHP / VXL / HVA / TMP / PAL / MAP 等 legacy runtime reader
- Rules / Art / Sound / AI 数据驱动解析
- Classic RA2YR Renderer semantics
- deterministic Simulation
- Pathfinding / Movement
- Economy / Combat / Production / AI / Trigger
- Traditional MOD compatibility
- 后续 Ares / Phobos 配置和行为语义兼容

---

## 最重要的开发纪律

这个项目此前在别的引擎路线中已经付出过一个很大的代价：

> 底层 parser、foundation 和自动化测试不断增加，但实际玩家打开程序时仍可能看到程序化绿地、蓝色矩形和不完整的真实资源。

因此从本仓库开始，以下规则是硬要求。

### 1. Parser PASS 不等于功能完成

例如：

```text
MIX parser PASS
MAP parser PASS
TMP parser PASS
```

只能证明底层的一部分正确。

如果真实地图仍然没有出现在 Game Window，当前阶段仍然是 **FAIL**。

### 2. Synthetic fixture 不能冒充真实兼容性

Synthetic/generated fixtures 用于：

- unit test
- malformed-input test
- CI
- deterministic regression

但不能用于证明：

- Stock YR 可运行
- `ra2yrmix` 可运行
- 某个传统 MOD 可运行

真实兼容必须使用真实 compatibility corpus 验证。

### 3. 不允许用 placeholder 隐藏失败

Classic / strict compatibility 路径中：

```text
真实 SHP 失败 -> 蓝矩形
真实 TMP 失败 -> 绿色平面
```

然后继续显示“READY”，这是禁止的。

关键真实资源失败时应该 fail closed，并给出可诊断错误。

### 4. Code is truth

尽量避免隐藏编辑器状态。运行模式、资源来源、兼容 profile 等应通过源码或文本配置明确表达，并能通过 repository review 追踪。

### 5. 不允许 screenshot-specific hack

禁止为了某次人工验收截图临时：

- hardcode 一个地图；
- hardcode 一个本地绝对路径；
- 特判某个 SHP/VXL；
- 使用预解包 PNG 绕开 runtime reader；
- 画程序化内容冒充真实资源。

---

## 当前总体里程碑

### P0 — Original Content Boot

第一阶段只关注：

```text
Window
+ D3D11
+ VFS/MIX
+ MAP/IsoMapPack5
+ TMP
+ PAL
+ Terrain Renderer
```

### P0 最终人工验收

> **启动程序，必须在窗口中真正看到从 `ra2yrmix` 的 MIX/MAP/TMP/PAL 链路直接解析出的 RA2YR 地图地形。**

只有 parser tests，没有真实地图画面，不算 P0 完成。

后续阶段大体为：

- P1 Static Westwood World：SHP/Overlay/矿石/建筑/树木
- P2 Voxel World：VXL/HVA
- P3 Player Interaction
- P4 Movement / A*
- P5 Economy Vertical Slice
- P6 Combat Vertical Slice
- P7 Complete Classic Renderer
- P8 Full GameData Compatibility
- P9 UI / Audio / Game Shell
- P10 Stock YR Compatibility
- P11 Traditional MOD Compatibility
- P12 Performance & Stability Certification
- P13 Ares Compatibility
- P14 Phobos Compatibility
- P15 Enhanced Engine

详细边界和验收要求见总实施计划文档。

---

## Compatibility Corpus

开发阶段维护者会提供：

```text
ra2yrmix/
```

它的定位是：

> **Required Development Compatibility Corpus**

本引擎至少必须能够直接读取和兼容其中纳入测试的内容。

但 `ra2yrmix` 并不自动等于干净 Stock Yuri's Revenge 1.001 黄金标准。长期还需要建立：

```text
StockYR1001
ra2yrmix
TraditionalMods
AresMods
PhobosMods
```

多层 compatibility corpus。

注意：测试素材的技术可读性与其是否适合公开分发是两个问题。若 corpus 含第三方受版权保护内容，应由维护者确认其分发授权；引擎本身不应把“公开仓库一定携带完整原版游戏 payload”设计成必要条件。

---

## 性能与稳定性原则

重置不能以性能明显差于原版为代价。

当前 provisional benchmark 目标：

### Low

- 4GB system RAM
- 11th-gen Intel Core i5 class CPU
- integrated graphics
- Low / Medium quality
- 200 active units
- 目标不低于 60 FPS

### High

- 8GB system RAM
- 14th-gen Intel Core i5 class CPU
- RTX 4060 class
- Maximum quality
- 1000 active units
- 目标不低于 60 FPS

这些是参考目标，必须在未来通过真实硬件和 workload benchmark 校准。

稳定性目标包括：

- memory leak = 0
- use-after-free = 0
- double-free = 0
- invalid read/write = 0
- 长时间运行内存不得无边界持续上涨

项目将使用 RAII、显式 ownership、Result 类型、ASan/static analysis/fuzz/soak 等手段约束。

---

## MOD 与扩展策略

### Classic / Compatibility

优先完成：

- Stock YR
- Traditional YR MOD
- FinalAlert 2 地图兼容

### 后续

- Ares semantics
- Phobos semantics
- Modern asset providers
- Lua scripting
- Enhanced Renderer
- 热重载
- 更高级的编队和寻路策略

不要求直接兼容 Ares/Phobos 给 `gamemd.exe` 注入的 32-bit DLL ABI。

---

## 对开发者的要求

在提交一个“完成”结论前，必须回答当前 milestone 对应的真实问题。

例如资源/画面阶段优先报告：

```text
Real MAP loaded: YES / NO
Real TMP visible: YES / NO
Real SHP visible: YES / NO
Real VXL visible: YES / NO
Synthetic fallback count: N
Performance gate: PASS / FAIL
Human verification: PASS / FAIL
```

自动化测试数量只是证据之一，不是最终结论。

---

## License / upstream game assets

本仓库中的引擎源码与第三方依赖遵循各自许可证。

RA2YR / Westwood / Electronic Arts 的原版游戏素材、MOD 素材和其他第三方资源的权利属于其各自权利人。是否可以把某个测试 corpus 或原版 payload 提交到公开仓库，应单独依据其授权状态判断。

---

## 当前状态

仓库处于 C++ 重启项目的最初阶段。

**当前优先事项不是战斗、经济、AI、多人或画面增强。**

第一优先级是 P0：

> 从真实 `ra2yrmix` 内容直接启动 MIX → MAP → TMP → PAL → D3D11 的可见地图链路。

先把第一张真实 RA2YR 地图画出来，再继续后面的系统。
