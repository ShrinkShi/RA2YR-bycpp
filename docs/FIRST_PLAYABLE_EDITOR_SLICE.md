# First Playable Editor Slice

## Scope

This milestone creates one executable with two explicit modes:

```text
MainMenu -> EditorSandbox
```

The sandbox uses a static 64x64 grass isometric mesh and the project-owned minimum `assets/game/ra2/` runtime assets. It demonstrates red/blue owners, selection, movement, stop, hold, patrol, attack-move, attack, damage and death without requiring the full compatibility corpus.

The lower-right command card is deliberately 3 rows by 5 columns. It is not a screenshot and each enabled command routes to the Simulation command path.

## Architecture

- `src/Westwood`: INI, PAL and SHP(TS) readers without SDL/D3D dependencies.
- `src/GameData`: Rules-driven E2 and M1Carbine data.
- `src/Simulation`: EntityId plus compact component-like unit data, separate `Owner`/`Faction` fields and explicit command/order transitions.
- `src/Renderer`: SDL window ownership boundary, D3D11 indexed sprites/static terrain and Direct2D/DirectWrite text presentation.
- `src/Client`: MainMenu, EditorSandbox input routing, HUD layout, camera/zoom and the draggable F2 sandbox palette.

Simulation and content readers do not include renderer headers.

## Infantry animation data

For a Westwood infantry sequence `first,count,facingStride`, the third value is the frame stride between facing-specific starts. A zero stride means that the sequence is not directional, so `Death=134,15,0` always selects frames 134 through 148 regardless of facing. Runtime frame delay and looping are explicit `Art.ini` metadata and are not encoded in the sequence triple.

## Build

Prerequisites: Windows 10/11 x64, Visual Studio C++ workload or clang-cl, CMake 3.28+, Ninja, vcpkg and SDL3.

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset windows-vcpkg
cmake --build build/windows-vcpkg --config Debug
ctest --test-dir build/windows-vcpkg -C Debug --output-on-failure
```

## Run

The Development Sandbox resolves `INI/` and `assets/` beside the executable, so the client does not require `RA2YR_CORPUS_ROOT`.

```powershell
build\windows-vcpkg\ra2yr_client.exe
```

The runtime reads its own `INI/Rules.ini`, `INI/Art.ini`, `assets/game/ra2/infantry/CONS.SHP` and `assets/game/ra2/palettes/unittem.pal`. `CONS.SHP` remains the original indexed SHP binary; it is never converted to PNG. The complete compatibility corpus remains an optional input for future Classic/import/regression workflows.

## Implemented

- RA2/YR red-black CRT console main menu with independent image buttons and hover/pressed states; DTA/CnCNet remains a component and Editor HUD reference only.
- Resolution-independent logical 1920x1080 UI layout for 1280x720, 1920x1080 and 2560x1440 window sizes.
- Main menu buttons: campaign, load, skirmish, online, LAN, settings, statistics, editor and exit.
- Editor mode in the same executable.
- 64x64 isometric grass grid with explicit `GridCoord`, `WorldCoord`, `ScreenCoord` conversions and height field.
- Red/Blue owner placement and real SHP frame decoding/remap from project-owned assets.
- Engine `Direction8` is mapped to the SHP facing order through `ConSequence.FacingMap`; SHP crop metadata and a shared ground pivot drive rendering and screen-space selection.
- The world uses an isometric camera with edge scrolling and cursor-centered wheel zoom. Static terrain remains in a GPU vertex buffer and receives camera constants at draw time.
- Single selection, drag selection, empty-click deselect, right-click commands and keyboard commands.
- Selection markers are projected from a world-ground circle into isometric ellipses. `SelectionRadius` is read from the unit Definition; selected markers are solid and hover/drag candidates are dashed.
- Infantry occupancy is simulation state: each ground cell has `TopCenter`, `BottomLeft` and `BottomRight` subcells. An explicit `OccupancyProfile=Infantry` limits a destination cell to three units and searches nearby cells for additional units.
- 3x5 command card with only Move, Stop, Hold, Patrol and Attack Move in the first row; the second and third rows are reserved.
- Left collapsible strategic ability rail with empty slots and right two-tier production sidebar shell with disabled empty producer selectors.
- `INI/UI.ini` drives the RA2/YR skin image IDs, logical rects, parent-relative widget anchors and shared command-card/sandbox hit boxes. Formal HUD and Sandbox shells use independent theme image IDs including `ui.hud.background`, `ui.hud.minimap.background`, unified `ui.hud.unitstatus.background`, `ui.hud.portrait.background`, `ui.hud.commandcard.background`, `ui.hud.production.background`, `ui.hud.strategic.background`, `ui.editor.sandbox.background` and dedicated editor control skins; rendering and input consume the same layout data, so a MOD can replace a panel image or move its rect without C++ changes.
- Data-driven E2 VoiceSets load multiple original RA2/YR Select, Move and Attack samples, choose randomly without immediate repetition, and retain procedural cues only as fallback/debug audio.
- Command acknowledgements use a dedicated voice stream and latest-intent queue policy; repeated simulation weapon events are a separate silent-until-sample `WeaponFire` path.

## Not implemented in this slice

Real MAP/TMP loading, FA2 import/export, production queues, economy, multiplayer, AI, triggers, full fog of war, VXL, buildings and save/load remain outside this milestone. Procedural grass is explicitly Development Sandbox content and is not Classic TMP compatibility.
