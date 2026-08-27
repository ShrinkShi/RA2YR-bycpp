Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    return (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}

function Write-Ok([string]$Message) {
    Write-Host "[OK] $Message" -ForegroundColor Green
}

function Write-Info([string]$Message) {
    Write-Host "[INFO] $Message" -ForegroundColor Cyan
}

function Invoke-Native {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $false)][string[]]$ArgumentList = @()
    )

    & $FilePath @ArgumentList
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code $LASTEXITCODE`: $FilePath $($ArgumentList -join ' ')"
    }
}

function Refresh-ProcessPath {
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $entries = @()
    if ($userPath) { $entries += $userPath -split ";" }
    if ($machinePath) { $entries += $machinePath -split ";" }

    $seen = @{}
    $unique = foreach ($entry in $entries) {
        $clean = $entry.Trim()
        if ($clean -and -not $seen.ContainsKey($clean.ToLowerInvariant())) {
            $seen[$clean.ToLowerInvariant()] = $true
            $clean
        }
    }
    $env:Path = $unique -join ";"
}

function Resolve-VsWhere {
    $command = Get-Command vswhere -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }

    $programFilesX86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
    if ($programFilesX86) {
        $candidate = Join-Path $programFilesX86 "Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }

    throw "vswhere.exe was not found. Install Visual Studio Installer discovery support."
}

function Resolve-VsInstallation {
    $vswhere = Resolve-VsWhere
    $installationPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1).Trim()
    $installationVersion = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion | Select-Object -First 1).Trim()
    if (-not $installationPath -or -not (Test-Path -LiteralPath $installationPath)) {
        throw "Visual Studio installation with the MSVC x64 workload was not found."
    }

    $vsDevCmd = Join-Path $installationPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $vsDevCmd)) {
        throw "VsDevCmd.bat was not found under the discovered Visual Studio installation: $vsDevCmd"
    }

    $msvcRoot = Join-Path $installationPath "VC\Tools\MSVC"
    $clPath = Get-ChildItem -LiteralPath $msvcRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object {
            $candidate = Join-Path $_.FullName "bin\Hostx64\x64\cl.exe"
            if (Test-Path -LiteralPath $candidate) { $candidate }
        } |
        Select-Object -First 1
    if (-not $clPath) {
        throw "MSVC x64 cl.exe was not found under: $msvcRoot"
    }

    return [PSCustomObject]@{
        VsWhere = $vswhere
        InstallationPath = $installationPath
        InstallationVersion = $installationVersion
        VsDevCmd = $vsDevCmd
        ClPath = $clPath
    }
}

function Import-VsDevEnvironment {
    param([Parameter(Mandatory = $true)][string]$VsDevCmd)

    $commandLine = 'call "' + $VsDevCmd + '" -arch=x64 -host_arch=x64 >nul && set'
    $environmentLines = & $env:ComSpec /d /s /c $commandLine
    if ($LASTEXITCODE -ne 0) {
        throw "VsDevCmd.bat failed with exit code $LASTEXITCODE."
    }

    foreach ($line in $environmentLines) {
        $match = [regex]::Match([string]$line, "^(?<name>[^=]+)=(?<value>.*)$")
        if ($match.Success) {
            Set-Item -Path ("Env:" + $match.Groups["name"].Value) -Value $match.Groups["value"].Value
        }
    }

    $cl = Get-Command cl -ErrorAction SilentlyContinue
    if (-not $cl) { throw "cl.exe is still unavailable after VsDevCmd initialization." }
    Write-Ok ("MSVC environment initialized: " + $cl.Source)
}

function Resolve-CommandPath {
    param([Parameter(Mandatory = $true)][string]$Name)
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $command) { throw "$Name was not found on PATH." }
    return $command.Source
}

function Resolve-CorpusRoot {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)

    $required = @(
        "extracted\ini\yr-1.001-patch\rulesmd.ini",
        "extracted\leaf\ra2.mix\conquer.mix\cons.shp",
        "extracted\leaf\ra2.mix\cache.mix\unittem.pal"
    )
    $candidates = @()
    if ($env:RA2YR_CORPUS_ROOT) { $candidates += $env:RA2YR_CORPUS_ROOT }
    $candidates += Join-Path (Split-Path -Parent $RepoRoot) "RA2YR-corpus"

    $worktreeLines = & git -C $RepoRoot worktree list --porcelain
    if ($LASTEXITCODE -eq 0) {
        foreach ($line in $worktreeLines) {
            if ($line -match "^worktree (.+)$") {
                $worktreePath = $Matches[1]
                $candidates += Join-Path $worktreePath "CNCRA2YR1.001\corpus"
                $candidates += Join-Path $worktreePath "corpus"
            }
        }
    }

    foreach ($candidate in ($candidates | Where-Object { $_ } | Select-Object -Unique)) {
        $root = [IO.Path]::GetFullPath($candidate)
        if (-not (Test-Path -LiteralPath $root)) { continue }
        $missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $root $_)) })
        if ($missing.Count -eq 0) {
            $env:RA2YR_CORPUS_ROOT = $root
            Write-Ok ("RA2YR_CORPUS_ROOT=$root")
            return $root
        }
    }

    Write-Info "No complete RA2YR corpus found; project-owned Development Sandbox assets remain available."
    return $null
}

function Initialize-DeveloperEnvironment {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)

    Refresh-ProcessPath
    $cmakePath = Resolve-CommandPath "cmake"
    $ninjaPath = Resolve-CommandPath "ninja"
    $vcpkgRoot = [Environment]::GetEnvironmentVariable("VCPKG_ROOT", "Process")
    if (-not $vcpkgRoot) { $vcpkgRoot = [Environment]::GetEnvironmentVariable("VCPKG_ROOT", "User") }
    if (-not $vcpkgRoot) { throw "VCPKG_ROOT is not set. Configure the user environment first." }
    $vcpkgRoot = [IO.Path]::GetFullPath($vcpkgRoot)
    if (-not (Test-Path -LiteralPath (Join-Path $vcpkgRoot "vcpkg.exe"))) { throw "vcpkg.exe was not found under VCPKG_ROOT=$vcpkgRoot" }
    if (-not (Test-Path -LiteralPath (Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"))) { throw "vcpkg.cmake was not found under VCPKG_ROOT=$vcpkgRoot" }
    $env:VCPKG_ROOT = $vcpkgRoot

    $vs = Resolve-VsInstallation
    Import-VsDevEnvironment $vs.VsDevCmd
    # VsDevCmd may select the Visual Studio-bundled vcpkg; keep the independent one.
    $env:VCPKG_ROOT = $vcpkgRoot
    $cmakeVersion = (& $cmakePath --version | Select-Object -First 1)
    $ninjaVersion = (& $ninjaPath --version | Select-Object -First 1)
    if ([version](([regex]::Match($cmakeVersion, "([0-9]+\.[0-9]+\.[0-9]+)")).Groups[1].Value) -lt [version]"3.28.0") {
        throw "CMake >= 3.28 is required, but $cmakeVersion was found."
    }

    Write-Ok ("Visual Studio $($vs.InstallationVersion): $($vs.InstallationPath)")
    Write-Ok ("MSVC x64 compiler: $($vs.ClPath)")
    Write-Ok ("CMake: $cmakePath ($cmakeVersion)")
    Write-Ok ("Ninja: $ninjaPath ($ninjaVersion)")
    Write-Ok ("VCPKG_ROOT=$vcpkgRoot")

    return [PSCustomObject]@{
        RepoRoot = $RepoRoot
        CMake = $cmakePath
        Ninja = $ninjaPath
        VcpkgRoot = $vcpkgRoot
        Vs = $vs
    }
}

function Get-BuildRoot {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)
    return Join-Path $RepoRoot "build\windows-vcpkg"
}

function Remove-FailedBuildCache {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)
    $buildRoot = [IO.Path]::GetFullPath((Get-BuildRoot $RepoRoot))
    $expectedPrefix = ([IO.Path]::GetFullPath((Join-Path $RepoRoot "build"))).TrimEnd("\") + "\"
    if (-not $buildRoot.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove an unexpected build path: $buildRoot"
    }
    if (Test-Path -LiteralPath $buildRoot) {
        Write-Info "Removing generated failed cache: $buildRoot"
        Remove-Item -LiteralPath $buildRoot -Recurse -Force
    }
}

function Invoke-Configure {
    param([Parameter(Mandatory = $true)]$Environment)
    Invoke-Native $Environment.CMake @("--preset", "windows-vcpkg")
}

function Invoke-Build {
    param([Parameter(Mandatory = $true)]$Environment)
    Invoke-Native $Environment.CMake @("--build", "--preset", "windows-vcpkg-debug")
}

function Invoke-Tests {
    param([Parameter(Mandatory = $true)]$Environment)
    $ctest = Join-Path (Split-Path -Parent $Environment.CMake) "ctest.exe"
    if (-not (Test-Path -LiteralPath $ctest)) { $ctest = Resolve-CommandPath "ctest" }
    Invoke-Native $ctest @("--preset", "windows-vcpkg-debug")
}

function Resolve-ClientExe {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)
    $buildRoot = Get-BuildRoot $RepoRoot
    $exe = Get-ChildItem -LiteralPath $buildRoot -Recurse -Filter "ra2yr_client.exe" -File -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -notmatch "vcpkg_installed" } |
        Select-Object -First 1
    if (-not $exe) { throw "ra2yr_client.exe was not found. Build the project first." }
    return $exe.FullName
}

function Invoke-Client {
    param([Parameter(Mandatory = $true)][string]$RepoRoot)
    $exe = Resolve-ClientExe $RepoRoot
    $runtimeBins = @(
        (Join-Path $RepoRoot "build\windows-vcpkg\vcpkg_installed\x64-windows\bin"),
        (Join-Path $RepoRoot "build\windows-vcpkg\vcpkg_installed\x64-windows\debug\bin")
    )
    $env:Path = (($runtimeBins | Where-Object { Test-Path -LiteralPath $_ }) + ($env:Path -split ";")) -join ";"
    Write-Info "Starting $exe"
    Push-Location (Split-Path -Parent $exe)
    try {
        & $exe
        if ($LASTEXITCODE -ne 0) { throw "ra2yr_client.exe exited with code $LASTEXITCODE." }
    } finally {
        Pop-Location
    }
}
