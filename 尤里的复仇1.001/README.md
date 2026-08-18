# 尤里的复仇 1.001 参考语料说明

本目录记录 `RA2YR-bycpp` 对 **Red Alert 2 + Yuri's Revenge 1.001** 原版/官方增补资源的兼容性参考信息。

本次审计样本来自维护者提供的“1.006 + YR 1.001、官方地图增补包、音乐包、Windows 10/11 兼容补丁、无过场动画版”发行包。它不是可直接视为“纯净光盘原版”的黄金镜像，因此这里把 **原版/官方内容** 与 **第三方发行包附加内容** 明确分开。

> 重要：当前样本是维护者明确说明的“无过场动画版”。因此 `movmd03.mix` 为 0 字节是该发行包的主动裁剪，不作为文件损坏证据。

## 1. 为什么仓库不直接提交完整 MIX

当前源码仓库不直接 vendor 完整原版游戏资产，原因有两层：

1. `ra2.mix`（281,888,480 B）与 `ra2md.mix`（204,527,696 B）均超过 GitHub 普通 Git 单文件 100 MiB 硬限制；
2. 原版游戏数据是否适合公开再分发，与引擎是否需要读取这些数据是两个问题。项目的 `DEVELOPERS.md` 已要求把这两件事分开处理。

因此这里保存：

- 经过实测的文件身份与分类；
- SHA-256；
- MIX / nested MIX 的有效资源路径；
- YR 1.001 的 INI 覆盖关系；
- 对 P0/P1/P2 等阶段真正有价值的资源容器清单。

实际兼容性测试应由维护者在本地提供合法游戏文件，并按 `DEVELOPERS.md` 的 Compatibility Corpus 约定挂载。

## 2. 本次最重要的结论

### 2.1 可作为核心原版/官方语料候选的文件

| 文件 | 作用 | 备注 |
|---|---|---|
| `ra2.mix` | RA2 主资源容器 | 内含 `local.mix`、`conquer.mix`、地形/剧院 MIX 等；YR 会继承大量 RA2 内容 |
| `ra2md.mix` | YR 主资源容器 | 内含 `localmd.mix`、`conqmd.mix`、YR 新增地形/剧院资源等 |
| `expandmd01.mix` | YR 1.001 官方补丁资源容器 | **1.001 实际使用的 `rulesmd.ini`、`soundmd.ini` 位于这里** |
| `language.mix` | RA2 本地化/文本/图标资源容器 | 含 `cameo.mix` 等 |
| `langmd.mix` | YR 本地化/文本/图标资源容器 | 已验证含 `cameomd.mix`、`ra2md.csf` |
| `MULTIMD.MIX` | YR 多人地图容器 | MAP 兼容性语料 |
| `MAPSMD03.MIX` | 官方地图增补相关 MIX | 属官方增补，不等同于基础光盘 1.001 |
| `*.yro`（13 个） | Westwood/EA 官方 YR Map Pack #5 cumulative | 属官方后续地图增补，不等同于基础光盘 1.001 |
| `movmd03.mix` | YR 过场电影容器 | 当前“无过场动画版”为 0 B，占位/裁剪；P0-P2 可忽略 |

### 2.2 明确不是“纯原版资源语料”的发行包附加物

| 文件/目录 | 判断 | 对重制项目的价值 |
|---|---|---|
| `ddraw.dll` | 第三方 `cnc-ddraw` DirectDraw 兼容层 | **有研究价值，但不应成为新 D3D11 renderer 的运行时依赖** |
| `ddraw.ini` | `cnc-ddraw` 配置 | 可研究缩放、窗口/全屏、VSync、帧率限制、Alt-Tab、CPU affinity 等兼容策略 |
| `cnc-ddraw config.exe` | `cnc-ddraw` 配置工具 | 工具本身无需进入 compatibility corpus |
| `Shaders/` | `cnc-ddraw` 的 GLSL/upscaling shader | 可作图像缩放效果参考，不是 RA2YR 原版素材 |
| `game.fnt` | 发行包额外加入的简体中文字体 | 发布者自己声明为附加特性；不是纯净原版基线 |
| `Sn_Changer.bat` | 随机生成序列号并写入注册表 | 安装/联机辅助脚本，不是游戏内容格式语料 |
| `SetupReg.exe` | `MYTH/DYNASTY` Registry Setup | 第三方破解/注册辅助，不作为原版基线 |
| `YURI.EXE` | 文件内明确含 `MYTH/DEViANCE` 字符串 | 不是可信的纯原版可执行文件基线 |
| `RA2MD.INI` | 当前用户配置/发行包修改配置 | 含高分辨率与兼容配置，不能作为“原厂默认 INI” |

`gamemd.exe` 的 PE 元数据仍标识为 Westwood Yuri's Revenge 主程序，且本样本 MD5 为 `fe2301a1f48841aa084aade100b25335`；它可以在后续黑盒行为研究中使用，但 **二进制行为参考** 与 **可公开提交的内容语料** 要分开管理。

## 3. 发布者宣传项与当前实际文件的对应

### cnc-ddraw 渲染/兼容补丁

对应：

```text
ddraw.dll
ddraw.ini
cnc-ddraw config.exe
Shaders/
```

`cnc-ddraw` 本质是旧 DirectDraw API 的兼容性重实现/包装层，用于修复现代 Windows 下黑屏、卡顿、崩溃、Alt-Tab 等问题，并附带窗口化、无边框、缩放 shader、FPS limiter、VSync 等功能。

对 `RA2YR-bycpp` 的正确用法是把它当作 **旧游戏兼容行为的参考样本**，而不是照搬其 DDraw→GDI/OpenGL/D3D9 架构。我们的首发渲染后端已经冻结为原生 D3D11。

### 防卡代码 / 游戏内高分辨率

当前 `RA2MD.INI` 中可直接看到：

```ini
[Video]
AllowHiResModes=yes
VideoBackBuffer=no
AllowVRAMSidebar=no
ScreenWidth=1280
ScreenHeight=720
```

其中 `AllowHiResModes=yes` 与高分辨率可选直接相关；`VideoBackBuffer=no`、`AllowVRAMSidebar=no` 属常见旧版兼容配置。因为这个文件是运行后/发行包修改过的用户配置，所以只作为行为参考，不作为 stock 默认值。

### 简体中文字体

对应：`game.fnt`。

### 自动更换序列号

对应至少包括：`Sn_Changer.bat`。脚本会生成随机值并写入：

```text
HKEY_LOCAL_MACHINE\SOFTWARE\Westwood\Yuri's Revenge
```

### 自动注册

当前目录存在 `SetupReg.exe`，其 PE 描述为 `Registry Setup - MYTH/DYNASTY`，属于第三方注册辅助；此外还有 `Register.exe/Register.ini`。原安装器外层 SFX 脚本本身不在当前解出的游戏目录中，所以“安装时自动注册”的完整逻辑不能仅凭目录完全还原。

### 管理员运行 / XP、2003 兼容模式 / 桌面 `-speedcontrol`

这些更可能是原自解压安装器执行的系统设置/快捷方式逻辑。当前提供给审计的是解包后的目录，不包含外层 7-Zip SFX 安装脚本，因此本轮不能把它们对应到某个游戏目录文件。

### UDP 联机组件

这里存在一个需要特别保留的差异：常见 RA2/YR IPX→UDP LAN patch 会在游戏目录放置 `wsock32.dll`，但 **当前样本中没有 `wsock32.dll`**。

所以当前只能得出：

> 发布说明宣称包含 UDP 联机组件，但本次“无过场动画版”实际解包目录未检出标准的 `wsock32.dll` UDP patch；可能是版本差异、打包遗漏或发布说明沿用了其他构建的描述。

不要把这个宣传项写成已经验证的 stock/patch 事实。

## 4. XCC Mixer 与 MIX

XCC Mixer 可以打开这些 MIX；本项目也不应依赖 XCC 才能运行。

本轮已经用独立解析逻辑直接验证：

- `ra2.mix`：encrypted + checksum；
- `ra2md.mix`：encrypted + checksum；
- `expandmd01.mix`：encrypted + checksum；
- 可从父 MIX 中正确定位并提取 nested MIX；
- 可从 `expandmd01.mix` 定位 YR 1.001 的 `rulesmd.ini` 与 `soundmd.ini`。

Westwood MIX 不保存完整文件名，而保存文件名哈希、偏移和长度；TS/RA2/YR 时代使用 CRC32 类文件名哈希。原版加密 MIX 还涉及 Westwood key block + Blowfish 头部解密。因此未来 C++ `Westwood/Mix` 模块必须原生实现这些能力。

## 5. 进一步索引

- [`MIX与INI资源索引.md`](./MIX与INI资源索引.md) —— 直接面向 P0/P1/P2 的资源路径与 INI 覆盖关系。
- [`发行包文件审计.md`](./发行包文件审计.md) —— 当前发行包逐类归属。
- [`SHA256SUMS.txt`](./SHA256SUMS.txt) —— 当前样本关键文件哈希，防止以后把不同 repack 混为同一黄金语料。
- [`manifest.json`](./manifest.json) —— 给工具/AI Agent 使用的机器可读摘要。

## 6. 参考资料

- cnc-ddraw: https://github.com/FunkyFr3sh/cnc-ddraw
- ModEnc MIX: https://modenc.renegadeprojects.com/MIX
- ModEnc INI: https://modenc.renegadeprojects.com/INI
- ModEnc Theaters: https://modenc.renegadeprojects.com/Theaters
- Westwood MIX format: https://moddingwiki.shikadi.net/wiki/MIX_Format_%28Westwood%29
- Official YR Map Pack #5 (cumulative): https://www.cnclabs.com/downloads/details/942/
