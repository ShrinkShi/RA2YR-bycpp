# Third-party assets

本文件区分“GPL 源码/客户端实现”和“EA/Westwood 原版数据”。两者不是同一授权。

| 素材/实现 | 来源 | 具体文件或路径 | 用途 | 许可证/权利状态 | 是否修改 |
|---|---|---|---|---|---|
| Main menu UI structure reference | [CnCNet/xna-cncnet-client](https://github.com/CnCNet/xna-cncnet-client), `develop` | `DXMainClient/DXGUI/Generic/MainMenu.cs` | 独立按钮、hover、pressed、布局参考 | GPLv3 source; no C# runtime copied | 否 |
| Original E2/Conscript sprite | User-provided local RA2YR compatibility corpus | `extracted/leaf/ra2.mix/conquer.mix/cons.shp` | Runtime SHP sprite, loaded directly from the audited extracted leaf | EA/Westwood game data; user must ensure lawful local use; not redistributed here | 否 |
| Temperate unit palette | User-provided local RA2YR compatibility corpus | `extracted/leaf/ra2.mix/cache.mix/unittem.pal` | Palette decode and owner remap | EA/Westwood game data; local runtime input only | 否 |
| YR 1.001 rules | User-provided local RA2YR compatibility corpus | `extracted/ini/yr-1.001-patch/rulesmd.ini` | E2/M1Carbine/SA/InvisibleLow data | EA/Westwood game data; local runtime input only | 否 |
| UI font | Windows system font `Microsoft YaHei UI` | Installed OS font, selected by GDI | Chinese UI text | OS-provided font; not copied into repository | 否 |

The feature branch does not copy protected RA2/YR binaries into Git. Set `RA2YR_CORPUS_ROOT` to a materialized local corpus when running the client.
