# Windows development workflow

The first playable client slice uses the `windows-vcpkg` Ninja preset. The repository keeps the preset machine-independent: it reads `VCPKG_ROOT` from the user environment and does not contain a local absolute path.

## One-command workflow

From the repository root, run:

```powershell
.\tools\dev\dev.ps1
```

The script discovers Visual Studio with `vswhere`, imports the x64 `VsDevCmd.bat` environment, validates CMake/Ninja/vcpkg/corpus, configures on the first run, builds, runs CTest, and starts `ra2yr_client.exe`. Existing configured builds reuse their cache and do not reinstall dependencies. Use `.\tools\dev\dev.ps1 -Clean` only when a generated build cache must be discarded.

The standalone commands are:

```powershell
.\tools\dev\setup.ps1
.\tools\dev\build.ps1
.\tools\dev\test.ps1
.\tools\dev\run.ps1
```

Each script fails immediately if a required tool, configuration, asset, or command is missing.

## Visual Studio

After setting the user-level `VCPKG_ROOT` and reopening Visual Studio, select the `windows-vcpkg` CMake configure preset. The existing build and test presets remain available for the Ninja workflow. A local `CMakeUserPresets.json` is supported and ignored, but is not required for the standard setup.

## Corpus

The scripts discover a complete local corpus through `RA2YR_CORPUS_ROOT` or the Git worktrees registered for this repository. They require:

- `extracted/ini/yr-1.001-patch/rulesmd.ini`
- `extracted/leaf/ra2.mix/conquer.mix/cons.shp`
- `extracted/leaf/ra2.mix/cache.mix/unittem.pal`

The corpus is read from its own worktree and is never copied into the feature branch.
