# Editor Tools and Unit Status

本轮是第二轮开发，范围限定为现有 Editor Sandbox 的正式编辑工具和 SC2 风格单位状态信息视图。它不引入第二个战斗单位、生产、经济、AI、Trigger、多人、真实 MAP/TMP 或复杂编辑器绘图工具。

## Editor Tool System

`src/Editor/EditorToolState.*` 保存当前工具、资产类别、当前资产、Owner 和笔刷预设。`EditorToolController` 负责把输入转换为 TerrainMap/Simulation 操作，渲染器不参与编辑规则。

可用工具为指针、铅笔、橡皮、刷子、油漆桶和取色器。Terrain 与 Unit 类别可用，Building/Resource 保留为 disabled 框架。指针保持原有 RTS 选取、命令、相机和缩放行为，不修改世界。

`TerrainMap` 使用 `TerrainCell { TerrainTypeId, Height, Exists }`。Void 由 `Exists=false` 表示，不能放置单位。地形静态 GPU 缓冲只在地图 dirty 后重建，每帧最多一次；笔刷同一 stroke 不会重复编辑同一个 cell。

Unit 类别通过 Rules infantry registry 选择 Definition，放置调用 Simulation 的真实 spawn 和 Infantry subcell occupancy。橡皮擦直接删除 Entity；油漆桶只作用于 Terrain，使用四连通 flood fill 和 4096 cell 安全上限。所有编辑操作都会返回明确的 blocked/no-op 反馈。

地形注册表在 `INI/Terrain.ini`，编辑器笔刷默认值在 `INI/Editor.ini`。工具栏、资产类别、笔刷和浮窗 rect 均由 `INI/UI.ini` 提供，视觉绘制与命中测试共享同一个 parent-relative rect。

## Unit Status Panel

`src/Client/Hud/UnitStatusViewModel.*` 将 `Simulation::Entity` 与 Rules/Veterancy 数据组合成只读 `UnitStatusViewModel`。HUD 不直接读取规则或拼接调试文件名。

ViewModel 包含名称、副名称、生命值分段、可选护盾/能量、击杀数、经验/下一军阶、护甲卡、任意数量武器卡和本地化标签。E2 目前只提供生命值，未伪造护盾/能量。护甲升级状态使用 `PlayerUpgradeState` 的数据结构，但本轮不实现研究系统。

E2 的击杀会由 Simulation 累加 KillCount 和目标的 ExperienceValue，并依照 Rules 的 VeterancyProfile 更新等级标签；本轮只显示状态，不修改战斗属性。

单位模型/头像继续使用真实 CONS.SHP 帧。所有正式 HUD 外壳图片来自 `INI/UI.ini` 的 RA2 Soviet theme；`drawRect` 仅用于动态内容、预览、地图和调试覆盖层。

## 验证边界

自动测试覆盖 Terrain Registry、Void/Brush/Fill/Eraser、Unit 放置、Infantry occupancy、Veterancy、HP 分段、可选护盾/能量、击杀/经验、三武器卡、标签和 Camera 变换。MSVC configure/build、CTest 和 EXE 启动是运行证据；自动测试和截图不等同于用户的最终人工视觉/交互验收。
