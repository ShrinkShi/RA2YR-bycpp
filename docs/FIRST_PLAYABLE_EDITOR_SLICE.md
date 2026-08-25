# First Playable Editor Slice

## Scope

This milestone creates one executable with two explicit modes:

```text
MainMenu -> EditorSandbox
```

The sandbox uses a procedural 64x64 grass isometric grid and a real original `CONS.SHP` sprite when the local compatibility corpus is configured. It demonstrates red/blue owners, selection, movement, stop, hold, patrol, attack-move, attack, damage and death.

The lower-right command card is deliberately 3 rows by 5 columns. It is not a screenshot and each enabled command routes to the Simulation command path.

## Architecture

- `src/Westwood`: INI, PAL and SHP(TS) readers without SDL/D3D dependencies.
- `src/GameData`: Rules-driven E2 and M1Carbine data.
- `src/Simulation`: EntityId plus compact component-like unit data, separate `Owner`/`Faction` fields and explicit command/order transitions.
- `src/Renderer`: SDL window ownership boundary and D3D11/GDI presentation.
- `src/Client`: MainMenu, EditorSandbox input routing and HUD layout.

Simulation and content readers do not include renderer headers.

## Build

Prerequisites: Windows 10/11 x64, Visual Studio C++ workload or clang-cl, CMake 3.28+, Ninja, vcpkg and SDL3.

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset windows-vcpkg
cmake --build build/windows-vcpkg --config Debug
ctest --test-dir build/windows-vcpkg -C Debug --output-on-failure
```

## Run

The client requires a materialized local RA2YR corpus. The expected root contains `extracted/ini`, `extracted/leaf` and the corpus manifest.

```powershell
$env:RA2YR_CORPUS_ROOT = 'E:\path\to\CNCRA2YR1.001\corpus'
build\windows-vcpkg\ra2yr_client.exe
```

The runtime reads the corpus's already audited extracted leaf paths for `CONS.SHP` (`ra2.mix/conquer.mix`) and `unittem.pal` (`ra2.mix/cache.mix`), plus the effective 1.001 `rulesmd.ini` from `yr-1.001-patch`. A runtime MIX VFS reader remains a later backlog item; the extracted SHP is still the original indexed SHP binary, not a PNG conversion.

## Implemented

- DTA/CnCNet-inspired independent menu controls with hover/pressed states.
- Resolution-independent logical 1920x1080 UI layout for 1280x720, 1920x1080 and 2560x1440 window sizes.
- Main menu buttons: campaign, load, skirmish, online, LAN, settings, statistics, editor and exit.
- Editor mode in the same executable.
- 64x64 isometric grass grid with explicit `GridCoord`, `WorldCoord`, `ScreenCoord` conversions and height field.
- Red/Blue owner placement and real SHP frame decoding/remap when corpus is available.
- Single selection, drag selection, empty-click deselect, right-click commands and keyboard commands.
- 3x5 command card with Move, Stop, Guard/Hold, Attack, Deploy, Patrol, Repair, Waypoint and Attack Move slots.
- Left strategic ability rail and right two-tier production sidebar shell.

## Not implemented in this slice

Real MAP/TMP loading, FA2 import/export, production queues, economy, multiplayer, AI, triggers, full fog of war, VXL, buildings, audio and save/load remain outside this milestone. Procedural grass is explicitly Development Sandbox content and is not Classic TMP compatibility.
