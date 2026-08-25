# 第三次与ChatGPT讨论的通用RTS AI架构原则 v1

> 记录日期：2026-08-25  
> 项目仓库：`ShrinkShi/RA2YR-bycpp`  
> 文档性质：通用 RTS Runtime 的 AI 架构原则与训练基础设施设计记录  
> 状态：**已确认的阶段性架构基线。算法、模型、具体难度数值仍可继续迭代，但本文冻结 AI 与 Simulation 之间的基本边界。**

---

## 0. 与前两次设计记录的关系

此前两份正式设计记录分别解决：

1. [`第一次与ChatGPT讨论的RA2YR C++ Engine 总实施计划 v1.md`](./第一次与ChatGPT讨论的RA2YR%20C%2B%2B%20Engine%20总实施计划%20v1.md)：C++23 重启、RA2YR Classic / Compatibility 基线、真实内容闭环与长期实施路线；
2. [`第二次与ChatGPT讨论的通用RTS规则框架与红色际霸设计记录 v1.md`](./第二次与ChatGPT讨论的通用RTS规则框架与红色际霸设计记录%20v1.md)：把项目长期边界从“RA2YR 专用兼容 Runtime”扩展为“以 RA2YR 为首个严格兼容规则集的数据驱动通用 RTS Runtime”，并冻结 Command / Order / Ability、生产、护盾、移动等初步规则。

第二次讨论把 AI、Trigger、统一模型列为下一阶段专题。本文件专门冻结其中的 **AI** 部分。

本文件不要求当前 P0 立即实现完整神经网络训练系统，也不改变“先闭环真实 RA2YR 内容”的近期开发优先级。它的目的，是防止 Simulation、Command、Replay、Fog of War、Determinism、Headless 等基础设施先按“没有训练型 AI”设计，导致后期不得不大规模返工。

---

# 第一部分：AI 的总体目标

## 1. AI 的产品目标

RA2YR-bycpp 的长期 AI 目标不是复制原版 RA2 / YR 的 `ai.ini` 固定小队逻辑，也不是简单通过增加资源、缩短生产时间、开全图等方式制造“高难度”。

目标是：

> **正常 AI 不依靠额外资源、全图视野、无反应延迟和无限操作频率获得难度，而尽量在与真人相同的信息与规则约束下，通过更高质量的战略、战术和微操决策形成不同强度。**

同时允许存在专门的娱乐性作弊 AI，但它必须被明确标识为作弊模式，不能把额外资源或全图视野包装成“AI 更聪明”。

---

## 2. AI 是 Player Controller，不是单位内部逻辑

AI 应被视为一种玩家控制器：

```text
                    Game Simulation
                          │
          ┌───────────────┼───────────────┐
          ▼               ▼               ▼
     HumanPlayer      ScriptedAI       NeuralAI
          │               │               │
          └───────────────┼───────────────┘
                          ▼
                       Command
                          ▼
                        Order
                          ▼
                  Game Simulation
```

真人与 AI 的核心区别只是：

> **谁生成 Command。**

真人路径：

```text
鼠标 / 键盘
    ↓
UI / Command Card
    ↓
Command
    ↓
Order
```

AI 路径：

```text
Observation
    ↓
AI Policy
    ↓
Command
    ↓
Order
```

因此 AI 不应获得另一套专用“内部控制 API”。

---

## 3. AI 永远不能直接修改 Simulation 状态

正常 AI、训练 AI、RuleBased AI、Neural AI 都不得直接执行：

```cpp
unit->position = ...;
unit->target = ...;
player.money += ...;
SpawnUnit(...);
```

AI 只能提出合法的游戏命令，例如：

```text
Move(UnitGroup, Position)
Attack(UnitGroup, Target)
AttackMove(UnitGroup, Position)
Train(Factory, Product)
Build(Builder, BuildingType, Position)
CastAbility(Unit, Ability, Target)
Research(Building, Upgrade)
```

之后仍由 Simulation 判断：

- 命令是否合法；
- 是否拥有控制权；
- 是否拥有足够资源；
- 是否满足科技前置；
- 是否有合法目标；
- 是否处于冷却；
- 是否存在合法路径；
- 是否受到状态效果限制；
- 是否满足当前规则集的其他约束。

AI 不得绕过上述判定。

---

# 第二部分：Observation 与战争迷雾

## 4. AI 默认只获得该玩家合法可知的信息

AI 不应直接读取完整 World State，而应读取经过玩家知识过滤后的：

```text
PlayerObservation
```

建议逻辑结构：

```text
PlayerObservation
├─ Self
│  ├─ Resources
│  ├─ Upgrades
│  ├─ Infrastructure
│  └─ StrategicAbilities
│
├─ OwnEntities[]
├─ VisibleEnemyEntities[]
├─ KnownEnemyEntities[]
├─ VisibleTerrain[]
├─ ExploredTerrain[]
├─ VisibleResourceNodes[] / KnownResourceNodes[]
├─ Events[]
└─ GameTime
```

如果敌方单位或建筑当前不在该玩家合法视野内，AI 不得获得它的实时坐标、血量、命令、生产队列等真实状态。

---

## 5. 可见敌人与“最后已知信息”必须分开

战争迷雾不等于失忆。

例如：

```text
12:01
侦察兵发现敌军战争工厂

12:20
战争工厂离开视野
```

AI 可以保留：

```text
LastKnown:
    Type=WarFactory
    Position=(50,80)
    SeenAt=12:01
```

但不能继续获得：

```text
它当前是否仍存在
当前 HP
当前生产队列
当前 Rally Point
当前最新坐标
```

这允许 AI 像真人一样形成推断：

> “此前看见对方有战争工厂，因此对方可能正在生产载具。”

但禁止 AI 通过内部状态确认黑幕后的真实情况。

---

## 6. AI 不必通过屏幕像素识别游戏

“公平”不等于“必须让 AI 从最终渲染画面 OCR 血条和图标”。

默认 AI 可以使用结构化 Observation，例如：

```text
EntityID
UnitType
UnitTag[]
Position
VisibleHP
VisibleShield
VisibleStatusEffects
VisibleOrders（若规则允许被观察）
```

公平边界是：

> **结构化数据是否包含真人按照游戏规则无法获得的信息。**

而不是要求 AI 在视觉识别层重复人类的感知困难。

因此第一阶段神经网络 AI 不需要依赖截图输入。

---

# 第三部分：公平性、反应延迟与操作预算

## 7. 仅限制视野还不够

即使 AI 不开全图，如果它能够：

```text
每个 Simulation Tick
同时精确控制数百个单位
```

仍然拥有真人无法达到的超人级操作能力。

因此公平 AI 必须考虑：

```text
ReactionDelay
ActionBudget / ActionsPerSecond
BurstBudget
```

这些参数属于 AI Skill Profile，而不是 Simulation Tick Rate。

Simulation 即使 60 / 120 Tick 运行，也不代表 AI 每 Tick 都可以发无限命令。

---

## 8. 群体命令按一次人类操作计算

真人框选 20 辆坦克再右键移动，本质上是一条群体命令，而不是 20 次独立操作。

因此：

```text
Move(20 units, Position)
```

应当按一个动作预算计算。

同理：

```text
AttackMove(ArmyGroup, Position)
```

也属于一个群体命令。

不能因为 AI 的内部表示按 Entity 展开，就把一次正常的人类群控强制拆成 N 个动作。

---

## 9. Camera / Attention 限制不是第一版强制要求

默认 Fair AI：

- 不要求真的移动屏幕摄像机；
- 可以读取全部“该玩家当前合法可知”的结构化信息；
- 但必须受到反应延迟和操作预算限制。

未来可增加更严格的 HumanLike / Research 模式：

```text
CameraPosition
AttentionArea
Selection
ControlGroups
MoveAttention
```

从而模拟真人有限注意力。

但这会显著增加训练难度，不应成为第一版 AI 的硬依赖。

---

# 第四部分：三级决策结构

## 10. AI 按 Strategic / Tactical / Micro 三层组织

通用 RTS 的时间尺度跨度极大。

AI 不应强制由一个模块直接同时处理：

```text
什么时候扩张基地
兵营应该造什么
这一只残血步兵下一帧往哪走
```

建议逻辑分层：

```text
Strategic
    ↓
Tactical
    ↓
Micro
    ↓
Command
```

这是一种架构分层，不要求三个层级必须使用三个独立神经网络。

---

## 11. Strategic：战略层

战略层以较低频率运行，负责：

- 总体经济分配；
- 科技路线；
- 扩张；
- 兵种组成；
- 侦察需求；
- 生产方向；
- 总体进攻与防守；
- 风险偏好；
- 长期目标切换。

战略层输出更接近：

```text
ExpandBase(location)
IncreaseAntiAir()
AttackRegion(EastBase)
Research(Armor2)
Defend(MainBase)
```

而不是具体单位下一帧移动几格。

---

## 12. Tactical：战术层

战术层负责“这支部队怎样执行战略目标”。

例如战略目标：

```text
Attack(EastBase)
```

战术层可以分解成：

```text
主力：正面推进
空军：侧翼骚扰
反隐单位：跟随主力
远程炮兵：保持后排距离
侦察兵：争夺高地视野
```

主要对象是 Squad / Task Group / Combat Group，而不是经济系统。

---

## 13. Micro：微操层

微操层负责具体交战行为，例如：

- 残血后撤；
- 集火高价值目标；
- 躲避范围技能；
- 保持射程；
- 近战包围；
- 释放技能；
- 保护反隐单位；
- 炮塔切换目标；
- 追击或停止追击；
- 根据武器和移动能力调整站位。

Micro 层通常具有最高更新频率，但仍受公平性 Action Budget 约束。

---

## 14. 分层的意义是复用，而不是强制模型结构

不同规则集可能拥有完全不同的宏观经济：

```text
RA2 MCV / 建造厂体系
虫族幼虫体系
神族工人折跃体系
未来其他 MOD 的特殊生产方式
```

但许多战术 / 微操能力可跨规则集复用，例如：

```text
躲 AoE
集火
保持距离
保护 Detector
合理使用技能
```

因此未来可以出现：

```text
StrategicPolicy_RedAlert
StrategicPolicy_ZergLike

共用：
TacticalPolicy_General
MicroPolicy_General
```

具体是否这样训练由后续实验决定，接口不应预先阻止这种组合。

---

# 第五部分：AI Policy 必须可替换

## 15. 不把“神经网络”写死进引擎核心

即使长期目标明确包含神经网络 / 机器学习 AI，Runtime 仍应面向抽象 Policy。

概念接口：

```cpp
class IAIPolicy
{
public:
    virtual ActionProposalList Decide(const Observation& observation) = 0;
};
```

可能的实现包括：

```text
RuleBasedPolicy
BehaviorTreePolicy
NeuralPolicy
ReplayPolicy
DebugPolicy
RemotePolicy
```

因此可以逐步演进：

```text
第一阶段
Strategic = RuleBased
Tactical  = RuleBased
Micro     = RuleBased

中期
Strategic = Neural
Tactical  = RuleBased
Micro     = Neural

后期
各层按实验结果自由组合
```

---

## 16. RuleBased AI 必须存在

RuleBased AI 不是最终目标，但不能因为计划使用神经网络就跳过。

它至少承担三个职责：

### 16.1 Gameplay 测试基线

没有基础 AI 时，大量双边玩法测试只能人工操作两个玩家。

### 16.2 早期训练对手

随机初始化的 Neural AI 与另一个完全不会玩的 Neural AI 直接 Self-play，可能长期学不到有效策略。

RuleBased AI 可以作为 Curriculum / Baseline Opponent。

### 16.3 API 正确性验证

如果 Neural AI 从来不生产兵营，需要判断究竟是：

```text
模型不会
```

还是：

```text
Build / Produce Command API 本身坏了
```

若 RuleBased AI 能稳定完成同一操作，就能快速缩小问题范围。

因此 RuleBased AI 是基础设施和测试工具，而不是回到原版 RA2 的固定作弊 AI。

---

# 第六部分：Replay、Headless 与训练路线

## 17. Replay 从设计上就是训练数据

Replay 不应只被视为“给玩家回看录像”。

它应尽量记录足够的信息，使同一场比赛可以重放、复现、分析并转换成训练样本。

至少应考虑：

```text
Map / MapVersion
RulesVersion
RandomSeed
Player / Faction
Commands
GameEvents
Winner / EndReason
必要的版本与校验元数据
```

训练管线需要能够形成：

```text
Observation_t → Action_t
```

样本。

例如真人在某时刻：

```text
Observation:
    Credits=850
    WarFactory=1
    VisibleEnemyTanks=6

HumanCommand:
    Produce(AntiArmorUnit)
```

就可以成为行为模仿数据的一部分。

是否在 Replay 中逐帧存完整 Observation，还是通过 Deterministic Replay 重建 Observation，属于后续存储与性能设计问题；但训练用途必须从架构阶段考虑。

---

## 18. Simulation 必须支持 Headless 高速运行

训练模式不能依赖：

- Renderer；
- Audio；
- GUI；
- 真实时间等待。

应支持：

```text
Simulation Only
```

并允许远高于 1x 的速度运行，只受到 Simulation CPU / 并行策略等实际瓶颈限制。

这不是单纯的性能优化，而是大规模 AI 训练所需的基础设施。

---

## 19. Simulation 应尽量确定性

期望目标：

```text
相同 Map
相同 Rules
相同 Seed
相同 Commands

→ 相同 Simulation 结果
```

随机行为统一使用受控的 Deterministic RNG / Seed，不允许各 Gameplay 模块随手引入不可复现的系统随机源。

Determinism 同时服务：

- Replay；
- AI 训练；
- Bug 复现；
- Benchmark；
- 多人同步；
- 自动化测试。

具体是否达到跨 CPU / 跨编译器 bit-exact，需要后续结合数值系统与网络模型继续研究；本文件冻结的是“可复现确定性应成为架构目标”。

---

## 20. 推荐的长期训练路线

当前认可的训练路线方向：

```text
阶段 0
RuleBased AI
    ↓

阶段 1
Human Replay
    ↓
Imitation / Supervised Learning
    ↓

阶段 2
Neural AI vs RuleBased / Curriculum Opponents
    ↓

阶段 3
Self-play
    ↓

阶段 4
League Training
```

这不是当前实现排期，也不锁死具体机器学习算法；它定义的是避免“从完全随机策略直接探索整个 RTS 巨大动作空间”的总体路线。

---

## 21. League Training 用于减少固定套路和发现弱点

未来训练池可以存在不同职责的 Agent，例如：

```text
Main Agent
Rush Exploiter
Macro Exploiter
Air Exploiter
Turtle Exploiter
Harass Exploiter
```

当主 AI 长期暴露某个弱点时，专门 Exploiter 可以持续针对该弱点训练，迫使主策略形成反制。

是否采用完整 League、Population Based Training 或其他多智能体方法由未来实验决定。

---

# 第七部分：通用 RTS 的 Observation / Action Schema

## 22. AI 不能只认识固定单位 ID

通用 RTS 引擎不能把 Neural Observation 的核心语义设计成固定枚举：

```text
RhinoTank
GrizzlyTank
Zealot
Marine
...
```

否则 MOD 增加数百个新单位后，AI Schema 立即失效。

AI 的基础 Entity Observation 应尽量来自通用 Gameplay Definition，例如：

```text
Entity
├─ UnitType / DefinitionId
├─ UnitTag[]
├─ Health
├─ Armor
├─ Shields[]
├─ Position
├─ MovementCapabilities
├─ Weapons[]
├─ Abilities[]
├─ Cost / EconomyRole
├─ ProductionRole
└─ VisibleState
```

`UnitType` / `DefinitionId` 可以作为 embedding 或特征之一，但不能是 AI 理解单位语义的唯一来源。

例如 AI 应有机会从通用数据理解：

> “这是一个高生命、重甲、机械、远程对地单位。”

而不是只能记忆：

> “ID 17 就是犀牛坦克。”

这一原则与通用 Rules / UnitTag / Ability / Weapon 数据驱动方向一致。

---

## 23. Action Space 必须参数化、动态化

不要把输出空间写死成：

```text
Action1 = BuildRhino
Action2 = BuildGrizzly
Action3 = BuildZealot
```

建议逻辑结构接近：

```text
CommandType
+ Actor / Actors
+ Ability / Product / Upgrade
+ Target / Position
```

示例：

```text
CommandType=Produce
Actor=WarFactory#128
Product=RhinoTank
```

```text
CommandType=CastAbility
Actor=Templar#55
Ability=PsiStorm
Target=(120,56)
```

```text
CommandType=Move
Actors=Squad#8
Target=(40,92)
```

这样新增 MOD 单位、技能和科技时，不需要为每个内容重新修改一个固定 Action 枚举。

具体 Neural Network 如何编码动态 Entity / Ability / Target，留给模型实现层研究。

---

# 第八部分：难度、风格与作弊模式

## 24. AI Skill Profile 与模型本身分离

不要求 Easy / Normal / Hard 各自维护完全独立的 AI 架构。

难度可以通过 Skill Profile 控制，例如：

```text
ReactionDelay
ActionBudget
BurstBudget
StrategicLookahead
MicroPrecision
ScoutingFrequency
RiskTolerance
MistakeRate
MemoryAccuracy
```

目标是让低难度更接近“反应较慢、侦察较差、微操有限、决策偶尔次优的玩家”，而不是人为送钱。

具体数值以后通过试玩与 Benchmark 调整，本文件不冻结数值。

---

## 25. AI Personality 与 Skill 分离

Skill 表示“水平”。

Personality 表示“打法偏好”。

可能的人格维度 / 标签包括：

```text
Aggressive
Defensive
Economic
Rush
Tech
Harass
Balanced
Unpredictable
```

同一强度 AI 可以拥有不同 Personality，从而减少遭遇战每次重复同一种套路的问题。

Personality 最终可以作为 Policy 条件输入，也可以由 RuleBased / Neural 等不同实现解释，不应在 Simulation 核心写大量：

```cpp
if (personality == Aggressive) ...
```

阵营规则和 Personality 仍应通过数据 / Policy 层处理。

---

## 26. 允许作弊 AI，但必须明确标识

可以存在娱乐用途的：

```text
Cheating AI
```

例如：

```text
ResourceIncome > 100%
FullVision
ProductionBonus
```

但必须与公平 AI 难度分开。

建议未来 UI / 配置概念上区分：

```text
AI Fairness
├─ Fair
├─ Handicap
└─ Cheating
```

“困难”不应默认等价于“作弊”。

---

# 第九部分：推荐的总体架构

## 27. Runtime AI 数据流

当前建议的逻辑架构：

```text
                 Game Simulation
                       │
                       ▼
             Player Knowledge Filter
                       │
                       ▼
                  Observation
                       │
              ┌────────┴─────────┐
              ▼                  ▼
          AI Memory          Event Stream
              │                  │
              └────────┬─────────┘
                       ▼
                   AI Policy
        ┌──────────────┼──────────────┐
        ▼              ▼              ▼
     Strategic      Tactical        Micro
        │              │              │
        └──────────────┼──────────────┘
                       ▼
                Action Proposal
                       │
                       ▼
                 Fairness Layer
              ┌────────┼────────┐
              ▼        ▼        ▼
           Delay      APS     Legality
              │        │        │
              └────────┼────────┘
                       ▼
                    Command
                       │
                       ▼
                     Order
                       │
                       ▼
                 Game Simulation
```

其中 Legality 的最终权威仍然是正式 Simulation / Command Validator；Fairness Layer 不能代替正式规则校验。

---

## 28. 训练旁路

```text
Game Simulation
      │
      ├── Replay
      │      ↓
      │ Imitation Learning
      │
      └── Headless Environment
               ↓
            Self-play
               ↓
          League Training
               ↓
           Neural Model
               ↓
            AI Policy
```

训练系统是 Runtime 的外部消费者之一，不应要求把训练框架直接编译进普通游戏客户端。

未来可以把：

- Runtime / Simulation；
- Headless environment；
- Dataset / Replay tooling；
- Training pipeline；
- Inference runtime；

拆成清晰边界，避免游戏客户端强依赖某个特定机器学习框架。

---

# 第十部分：正式冻结的 20 条 AI 原则

以下原则在本次讨论中已确认，后续实现和设计默认不得无故违反。若未来实测证明其中某条不合理，应通过新的设计记录明确修订，而不是静默偏离。

1. **AI 是 Player Controller，而不是单位内部逻辑。**
2. **AI 与真人最终都只能通过 `Command → Order` 操作 Simulation。**
3. **正常 AI 禁止直接修改资源、单位和世界状态。**
4. **AI 默认只获得当前玩家合法可知的信息。**
5. **战争迷雾中的已知敌人采用 Last Known Information，而非真实状态。**
6. **AI 使用结构化 Observation，不要求从屏幕像素识别游戏。**
7. **公平 AI 必须存在反应延迟和动作频率约束，防止超人微操。**
8. **群体命令按一个人类操作计入动作预算。**
9. **摄像机 / 注意力限制作为更严格的 HumanLike 模式，而不是第一版强制要求。**
10. **AI 按 Strategic / Tactical / Micro 三层组织，但每层具体算法可替换。**
11. **RuleBased AI 必须存在，主要承担 baseline、测试与早期训练对手，而不是最终作弊 AI。**
12. **Neural AI 推荐先 Replay 模仿学习，再 Self-play，最后 League。**
13. **Simulation 必须支持无渲染 Headless 高速运行。**
14. **Replay 从设计上就是训练数据，不只是录像。**
15. **Simulation 应尽量确定性，随机行为统一由受控 Seed 驱动。**
16. **Observation 与 Action Schema 要面向通用 RTS，不能绑定固定单位列表。**
17. **AI 难度优先来自反应、操作预算和决策能力，而不是资源作弊。**
18. **作弊 AI 可以存在，但必须与公平难度明确分离。**
19. **AI Personality 与 Skill Profile 分离：一个决定风格，一个决定水平。**
20. **训练中的 AI 也必须遵守正式游戏规则，除非该实验明确标记为特权环境。**

其中对底层架构影响最大的原则是：

- 第 2 条：统一 Command / Order 接口；
- 第 4 条：Player Knowledge / Observation 过滤；
- 第 7 条：公平性层与操作预算；
- 第 13 条：Headless Simulation；
- 第 16 条：面向通用 RTS 的动态 Observation / Action Schema。

这些能力应在相关 Runtime 子系统落地时提前考虑，而不是等神经网络模型开始训练后再补。

---

# 第十一部分：当前不冻结的内容

本文件不冻结以下内容：

- 使用 PPO、SAC、IMPALA、Transformer 或其他具体算法；
- 模型大小和网络拓扑；
- Observation 的最终张量 / 图结构编码；
- 动态 Action Space 的最终神经网络实现；
- Strategic / Tactical / Micro 是否分别训练独立模型；
- Easy / Normal / Hard 的具体 APS、Delay 和 MistakeRate 数值；
- Replay 最终二进制文件格式；
- Headless 训练服务的进程 / RPC 架构；
- 是否采用完整 League / Population Based Training；
- 是否对 HumanLike AI 强制模拟 Camera / Selection / Control Group；
- 训练集许可、公开 Replay 来源与数据清洗方案；
- GPU / CPU 训练集群方案；
- Trigger 与 AI Event Stream 的最终耦合方式。

这些内容需要在 Simulation、Replay、Trigger、统一数据模型与真实玩法进一步成熟后继续研究。

---

# 第十二部分：与未来 Trigger / 统一模型专题的接口

AI 与尚未冻结的两个专题存在直接依赖：

## 29. Trigger

AI 需要 Event Stream，但这不代表 Trigger 系统必须成为 AI 的内部接口。

未来应研究：

```text
GameEvent
├─ Trigger 消费
├─ Replay 消费
├─ UI / Presentation 消费
└─ AI Observation / Memory 消费
```

尽量让 Event 来源统一，而不是分别维护 `AIEvent` 与 `TriggerEvent` 两套重复事实。

## 30. 统一模型 / Rules Schema

通用 AI 要理解新 MOD 单位，依赖数据定义能够暴露：

- UnitTag；
- Armor / Shield；
- Weapon；
- Ability；
- MovementCapabilities；
- Production / Construction；
- Resource / Infrastructure；
- Target / Relationship；
- 其他对决策有意义的通用语义。

因此统一模型不仅服务 Gameplay 和 MOD，也会成为 AI Observation Schema 的主要语义来源。

这进一步强化了项目的总原则：

> **不要通过大量阵营名、单位名和硬编码 ID 表达玩法；让规则、标签、组件、能力和命令本身具有足够的机器可读语义。**

---

# 结论

本次 AI 设计不试图提前决定“最终使用哪个神经网络”，而是先冻结更基础的问题：

> **AI 能看见什么、能做什么、必须经过什么接口、怎样保证公平、怎样训练、怎样复现，以及怎样避免绑定某一套固定单位。**

最终期望形成：

```text
可替换 AI Policy
      ↓
公平且结构化的 Observation / Action
      ↓
统一 Command / Order
      ↓
确定性 Gameplay Simulation
      ↓
Replay + Headless + Training
```

这套基础成立以后，RuleBased、行为树、模仿学习、Self-play、League、未来新的神经网络方法都可以在不破坏 Gameplay Runtime 的情况下继续演进。
