# Windows development workflow

The first playable client slice uses the `windows-vcpkg` Ninja preset. The repository keeps the preset machine-independent: it reads `VCPKG_ROOT` from the user environment and does not contain a local absolute path.

## One-command workflow

From the repository root, run:

```powershell
.\tools\dev\dev.ps1
```

The script discovers Visual Studio with `vswhere`, imports the x64 `VsDevCmd.bat` environment, validates CMake/Ninja/vcpkg, configures on the first run, builds, runs CTest, and starts `ra2yr_client.exe`. It uses the project-owned `INI/` and `assets/` content copied beside the executable; `RA2YR_CORPUS_ROOT` is optional. Existing configured builds reuse their cache and do not reinstall dependencies. Use `.\tools\dev\dev.ps1 -Clean` only when a generated build cache must be discarded.

The standalone commands are:

```powershell
.\tools\dev\setup.ps1
.\tools\dev\build.ps1
.\tools\dev\test.ps1
.\tools\dev\run.ps1
```

Each script fails immediately if a required tool, configuration, project-owned asset, or command is missing.

## Canonical local checkout

PR #2 work must use the single canonical checkout `E:\时锐\RA2\RA2YR-bycpp`. Run fetch, configure, build, CTest, runtime checks and screenshot capture from that directory. Do not create another per-PR clone or worktree for ongoing fixes. Existing historical worktree directories may remain on disk for reference, but they are not active development roots.

The large `agent/ra2yr-corpus-lfs` checkout remains separate from the feature branch. It is only a local source for compatibility corpus work and must never be merged, copied into the feature branch, or committed as PR assets.

## Visual Studio

After setting the user-level `VCPKG_ROOT` and reopening Visual Studio, select the `windows-vcpkg` CMake configure preset. The existing build and test presets remain available for the Ninja workflow. A local `CMakeUserPresets.json` is supported and ignored, but is not required for the standard setup.

## Runtime content and optional corpus

The normal build/test/run flow does not discover or require a complete corpus. The executable uses content beside itself:

- `INI/Rules.ini`
- `INI/Art.ini`
- `assets/game/ra2/infantry/CONS.SHP`
- `assets/game/ra2/palettes/unittem.pal`

`RA2YR_CORPUS_ROOT` remains an optional path for future corpus integration/import/Classic workflows. The complete corpus is read from its separate worktree and is never copied into the feature branch.
