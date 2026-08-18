# RA2YR-bycpp 开发者说明

本文档面向参与 `RA2YR-bycpp` 开发、研究、代码审查和 AI Coding 的开发者与 AI Agent。

如果你只是想了解这个项目是什么、未来能做什么、目前是否可用，请先阅读 [`README.md`](./README.md)。

---

## 1. 开发者第一阅读顺序

开始修改代码前，至少依次阅读：

1. [`README.md`](./README.md) —— 面向使用者的项目定位与当前状态；
2. [`第一次与ChatGPT讨论的RA2YR C++ Engine 总实施计划 v1.md`](./第一次与ChatGPT讨论的RA2YR%20C%2B%2B%20Engine%20总实施计划%20v1.md) —— 项目的第一次完整架构讨论、背景、决策和阶段计划；
3. 后续新增的 ADR、格式研究、兼容性记录、性能基准和当前 milestone 文档。

---

## 2. 《第一次与ChatGPT讨论的RA2YR C++ Engine 总实施计划 v1.md》是什么

这份文件不是普通 TODO，也不是要求后续开发者永远逐字执行的“圣经”。

它存在的意义主要有五个：

1. **项目背景记录**：解释为什么项目从此前的 Godot / Unity 路线转向 C++ 自研专用引擎；
2. **第一次完整架构讨论记录**：保留维护者与 ChatGPT 第一次正式讨论中的思路、取舍、踩坑经验和关键选择；
3. **长期参考基线**：记录项目最初冻结的兼容目标、技术栈、Simulation、Renderer、性能、稳定性、CI、MOD 和阶段路线；
4. **防止项目失忆**：后续开发者或 AI 不应在不了解历史原因的情况下重新引入已经证明不合适的方向；
5. **设计演进起点**：如果逆向研究、实测数据或工程现实证明其中某项决定不合理，应通过 ADR 或新版计划有记录地修订，而不是为了维护旧文档而忽略事实。

因此，对这份文件的正确态度是：

> **认真阅读、理解背景、默认遵循；出现新证据时允许有记录地修订。**

尤其要注意，它不仅保存“最终答案”，也保存当时为什么选择 C++、为什么不用完整重编辑器引擎、为什么把可见运行结果作为 milestone 硬门禁等讨论过程。这些背景本身就是项目资产。

---

## 3. 项目技术定位

本项目不是“做一个像红警2的 RTS”，而是尝试开发一个使用 **C++23** 的、数据驱动的 RA2YR 兼容 RTS Engine / Runtime Loader。

长期目标是：

> `gamemd.exe` 能识别和运行的传统 RA2YR 数据与 MOD 内容，本引擎也应尽可能直接识别、解析和运行。

重点兼容对象包括但不限于：

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
- 后续经研究确认属于 YR runtime 的其他必要格式和覆盖语义。

传统 MOD 的目标是尽可能 **零转换运行**：不要求先把 SHP/VXL 转成现代素材，不要求先预解包 MIX，也不要求先导入专有编辑器工程。

---

## 4. 当前冻结的主要技术方向

### 语言与构建

- C++23；
- CMake；
- MSVC 为主要 Windows 工具链；
- clang-cl 作为第二编译器和 CI 交叉验证；
- vcpkg manifest mode 管理并锁定第三方依赖版本；
- 自有代码使用高 warning level，并将 warning 视为 error。

### 平台

- 首发：Windows 10 x64 / Windows 11 x64；
- SDL3 负责 Window / Input / Platform 基础层；
- 首发图形 backend：自研 Direct3D 11；
- 最低 D3D Feature Level：11_0；
- Core / Simulation / Content 不得被 Windows API 污染，以便未来增加其他平台 backend。

### 核心自研范围

第三方库可以承担基础设施，但不能成为以下核心语义的权威：

- Westwood MIX / VFS；
- RA2YR content discovery / precedence；
- SHP / VXL / HVA / TMP / PAL / MAP 等 legacy runtime reader；
- Rules / Art / Sound / AI 数据驱动解析；
- Classic RA2YR Renderer semantics；
- deterministic Simulation；
- Movement / Pathfinding；
- Economy / Combat / Production / AI / Trigger；
- Traditional MOD compatibility；
- 后续 Ares / Phobos 配置和行为语义兼容。

---

## 5. 架构纪律

项目必须保持明确的单向依赖边界，避免逐渐演变成难以维护的屎山。

基本分层方向：

```text
Engine/Core
Engine/Platform
Engine/Graphics
Engine/Audio
Engine/Diagnostics

Westwood/*
Content/*
GameData/*
Simulation/*
Renderer/*
Client/*
Modding/*
Tools/*
Benchmarks/*
Tests/*
```

关键规则：

- Simulation 不能依赖 Renderer；
- Westwood parser 不能依赖 SDL / D3D；
- Core 不能反向依赖上层 GameData / Client；
- 运行模式、资源来源、兼容 profile 必须通过源码或文本配置显式表达；
- 尽量做到 **code is truth**，不引入类似重编辑器隐藏序列化状态的问题。

Entity / Simulation 采用 `EntityId + data-oriented component stores` 方向，而不是深层 OOP 继承树，也不默认依赖大型第三方 ECS。

权威 Simulation 采用 deterministic 设计，整数 / 定点优先；Renderer、GPU、插值和视觉表现可以正常使用 float。

---

## 6. C++ 代码质量约束

### 推荐

- RAII；
- `std::vector` / `std::array` / `std::span`；
- `std::unique_ptr`；
- `std::optional`；
- 显式 ownership；
- 可预期错误使用 `Result<T, Error>` 风格；
- profiler 证明热点后再引入 pool / arena / frame allocator。

### 应谨慎或避免

- 复杂模板元编程；
- CRTP 炫技；
- 宏生成大量业务代码；
- 隐藏式 Service Locator；
- 全局 Singleton 驱动架构；
- 多层继承树；
- 无明确收益的大型 DI / Reflection 框架；
- 在业务代码中散落裸 `new/delete`。

自有代码编译门禁：

```text
MSVC:    /W4 /WX
clang-cl: high warnings + warnings-as-errors
```

第三方依赖的警告应隔离，不应因为第三方代码噪声降低本项目自身的标准。

---

## 7. 最重要的开发纪律：真实结果优先

旧路线已经验证过一个严重问题：parser、foundation 和自动化测试不断增加，并不等于玩家看到的游戏真的更接近 RA2YR。

因此从本仓库开始，以下规则为硬要求。

### Parser PASS 不等于功能完成

```text
MIX parser PASS
MAP parser PASS
TMP parser PASS
```

只能说明底层的一部分成立。

如果当前 milestone 要求地图显示，而 Game Window 中没有真实地图，那么 milestone 就是 **FAIL**。

### Synthetic fixture 不能冒充兼容性

Synthetic / generated fixtures 可以用于：

- unit test；
- malformed-input test；
- CI；
- deterministic regression。

但不能证明：

- Stock YR 可运行；
- `ra2yrmix` 可运行；
- 某个传统 MOD 可运行。

### 禁止 placeholder 隐藏真实失败

Classic / strict compatibility 路径中，不允许：

```text
真实 SHP 失败 -> 蓝矩形
真实 TMP 失败 -> 绿色平面
```

然后仍宣称 READY。

关键资源失败应 fail closed，并输出可定位的结构化诊断。

### 禁止验收特判

禁止为了通过某次截图或人工验收：

- 硬编码单张地图；
- 硬编码用户绝对路径；
- 特判某个 SHP / VXL；
- 用预解包 PNG 绕开 runtime reader；
- 使用程序化内容冒充真实 legacy 资源。

---

## 8. 当前 P0

当前第一阶段为：

# P0 — Original Content Boot

只关注：

```text
Window
+ D3D11
+ VFS / MIX
+ MAP / IsoMapPack5
+ TMP
+ PAL
+ Terrain Renderer
```

P0 的最终人工验收只有一个核心结果：

> **启动程序，在窗口中真正看到从 `ra2yrmix` 的 MIX / MAP / TMP / PAL 链路直接解析出的 RA2YR 地图地形。**

只有 parser tests、日志、接口、类或 synthetic terrain，都不算 P0 完成。

P0 未通过时，不应扩展战斗、经济、AI、多人、现代光影等系统。

---

## 9. 总体 milestone 路线

- P0 — Original Content Boot
- P1 — Static Westwood World：SHP / Overlay / 矿石 / 建筑 / 树木
- P2 — Voxel World：VXL / HVA
- P3 — Player Interaction
- P4 — Movement / A*
- P5 — Economy Vertical Slice
- P6 — Combat Vertical Slice
- P7 — Complete Classic Renderer
- P8 — Full GameData Compatibility
- P9 — UI / Audio / Game Shell
- P10 — Stock YR Compatibility
- P11 — Traditional MOD Compatibility
- P12 — Performance & Stability Certification
- P13 — Ares Compatibility
- P14 — Phobos Compatibility
- P15 — Enhanced Engine

详细设计背景、阶段边界和选择记录见第一次总实施计划文档。

---

## 10. Compatibility Corpus

开发阶段维护者会提供：

```text
ra2yrmix/
```

它的定位是：

> **Required Development Compatibility Corpus**

本引擎至少必须能够读取其中纳入测试的内容。

但它不自动等于干净的 Stock Yuri's Revenge 1.001 黄金标准。长期 compatibility corpus 预计包括：

```text
StockYR1001
ra2yrmix
TraditionalMods
AresMods
PhobosMods
```

技术可读性和资源是否适合公开分发是两个问题；第三方受版权保护资源的分发授权应由维护者单独确认。

---

## 11. Timing / GameSpeed

Classic Simulation timing 目前不拍脑袋指定 30Hz 或 60Hz。

需要专门进行 `gamemd.exe Timing Research`，研究：

- Simulation Frame；
- GameSpeed；
- wall clock；
- movement；
- ROF；
- animation；
- build time；
- trigger / superweapon / radiation / AI；
- 掉帧时的真实行为。

研究完成后再冻结 Classic timing。

Renderer FPS 与权威 Simulation 分离，以便未来在不改变 Classic 游戏速度的前提下支持高刷新率。

---

## 12. Pathfinding

普通单位以 A* 为第一基础策略。

引擎不要求所有单位永远使用同一种算法。未来应允许按单位 / Locomotor / 编队需求选择不同路径策略，例如：

- A* Grid；
- formation / squad pathfinding；
- hierarchical A*；
- flow field；
- naval；
- aircraft；
- 特殊 Locomotor。

但 P4 之前不要为了未来编队系统提前制造复杂抽象。

---

## 13. Hot Reload

项目确认支持热重载，但它属于开发体验和现代扩展能力，不得干扰 Classic compatibility 主线。

后续 Development Mode 可逐步支持：

- Rules / Art / Sound INI reload；
- loose modern asset reload；
- shader reload；
- explicit MIX remount；
- modern resource reload。

不同字段必须根据风险分级，例如：

```text
LiveSafe
RecreateRequired
MatchRestartRequired
EngineRestartRequired
```

不允许简单把新的类型定义无条件覆盖到已经运行的对象上。

---

## 14. 性能与稳定性门禁

重置不能以明显低于原版的效率作为代价。

当前 provisional benchmark：

### Low

- 4GB system RAM；
- 11th-gen Intel Core i5 class CPU；
- integrated graphics；
- Low / Medium quality；
- 200 active units；
- 目标不低于 60 FPS。

### High

- 8GB system RAM；
- 14th-gen Intel Core i5 class CPU；
- RTX 4060 class；
- Maximum quality；
- 1000 active units；
- 目标不低于 60 FPS。

这些是参考目标，后续必须通过真实硬件和正式 benchmark workload 校准。

性能基准至少记录：

- Average FPS；
- 1% / 0.1% low；
- P95 / P99 frame time；
- CPU / GPU frame time；
- RAM / VRAM；
- allocation rate；
- Simulation / Pathfinding / Render 时间。

在正式 reference benchmark 中跌破 60 FPS 视为性能回归，需要定位原因，而不是简单降低标准。

稳定性目标：

```text
memory leak = 0
use-after-free = 0
double-free = 0
invalid read/write = 0
```

并通过 ASan、静态分析、parser fuzz、长期 soak 和 memory growth test 持续约束。

---

## 15. CI 基线

每个 PR 逐步建立并要求：

- MSVC Debug / Release；
- clang-cl；
- Unit Tests；
- Integration Tests；
- Parser Tests；
- Synthetic Fixtures；
- ASan（目标平台可用范围内）；
- Static Analysis；
- Repository hygiene。

Nightly / 专用 runner：

- full `ra2yrmix` compatibility；
- parser fuzz；
- long soak；
- performance benchmark；
- determinism test；
- large battle benchmark。

---

## 16. Classic / Enhanced 边界

### Classic

优先完成：

- Stock YR compatibility；
- Traditional YR MOD；
- FinalAlert 2 地图兼容；
- Classic Renderer；
- 必要的可观察原版 quirks。

原版 gameplay bug / quirks 可在研究后归类为 `Legacy Behavior Options`，作为房间特色开关；安全漏洞、内存破坏、崩溃等问题不复制。

### Enhanced

Classic 主线稳定后再扩展：

- 现代光照；
- 更高质量阴影；
- AA / 高刷新率；
- 现代资源 provider；
- Lua；
- 更高级的编队 / 寻路；
- 现代 MOD 能力。

Ares / Phobos 的目标是兼容其配置和行为语义，不直接加载它们面向 `gamemd.exe` 的 32-bit DLL ABI。

---

## 17. 开发者提交“完成”结论前必须回答什么

任何 milestone 的“完成”必须同时考虑：

```text
Code
Tests
Real corpus
Runtime output
Performance
Human verification
```

例如资源 / 画面阶段，应优先报告：

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

## 18. 给 AI Coding Agent 的额外规则

AI Agent 开始任务前必须先恢复 GitHub / 当前工作树真实状态，而不是相信旧提示词中的 SHA、测试数量或阶段状态。

AI Agent 不得：

- 为了让测试通过而降低兼容语义；
- 用 synthetic 内容替代真实兼容测试并宣称成功；
- 把 parser success 推导为 runtime presentation success；
- 未经证据修改已经冻结的架构边界；
- 把大量无关重构混入当前 milestone；
- 在没有性能数据的情况下进行复杂“优化式重写”；
- 用 hardcode 绕过 VFS / GameData / logical asset route。

如果计划文档与新的真实证据冲突，应明确记录冲突、提出修订，而不是静默偏离。

---

## 19. 当前一句话开发目标

> **先别造坦克、经济、AI、UI，也别堆更多 foundation。先把窗口打开，然后从真实 `ra2yrmix` 的 MIX → MAP → TMP → PAL 链路画出第一张真正的 RA2YR 地图。**
