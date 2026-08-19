param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDir,

    [string]$Branch = "agent/ra2yr-corpus-lfs",

    [string]$MixDatabase = ""
)

$ErrorActionPreference = "Stop"
$env:PYTHONUTF8 = "1"

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command not found: $Name"
    }
}

function Assert-LastExitCode([string]$Step) {
    if ($LASTEXITCODE -ne 0) {
        throw "$Step failed with exit code $LASTEXITCODE"
    }
}

Require-Command git
Require-Command py

try {
    git lfs version | Out-Null
    Assert-LastExitCode "git lfs version"
} catch {
    throw "Git LFS is not installed or not available."
}

# Resolve from the script path instead of native git output so Windows PowerShell
# 5.1 cannot corrupt a non-ASCII repository path through code-page conversion.
$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot ".git"))) {
    throw "Could not resolve the RA2YR-bycpp repository root from the script location: $RepoRoot"
}
Set-Location -LiteralPath $RepoRoot

$Remote = (git remote get-url origin)
Assert-LastExitCode "git remote get-url origin"
if (-not $Remote -or $Remote -notmatch "ShrinkShi/RA2YR-bycpp") {
    throw "Unexpected origin remote: $Remote"
}

if ((git status --porcelain)) {
    throw "Working tree is not clean. Commit or stash unrelated work before expanding the corpus."
}

if (-not (Test-Path -LiteralPath $SourceDir -PathType Container)) {
    throw "Source directory does not exist: $SourceDir"
}

git fetch origin $Branch
Assert-LastExitCode "git fetch corpus branch"
git switch $Branch
Assert-LastExitCode "git switch corpus branch"
git pull --ff-only origin $Branch
Assert-LastExitCode "git pull corpus branch"

git lfs install --local
Assert-LastExitCode "git lfs install --local"

$Builder = Join-Path $PSScriptRoot "build_corpus.py"
$BuilderArgs = @($Builder, "--source", $SourceDir, "--repo-root", $RepoRoot)
if ($MixDatabase) {
    if (-not (Test-Path -LiteralPath $MixDatabase -PathType Leaf)) {
        throw "Mix database file does not exist: $MixDatabase"
    }
    $BuilderArgs += @("--mix-database", $MixDatabase)
}

py @BuilderArgs
Assert-LastExitCode "build expanded corpus"

git add -A
Assert-LastExitCode "git add -A"

$ManifestFile = Get-ChildItem -LiteralPath $RepoRoot -Recurse -File -Filter "corpus-manifest.json" | Select-Object -First 1
if (-not $ManifestFile) {
    throw "corpus-manifest.json was not generated"
}
$Manifest = Get-Content -LiteralPath $ManifestFile.FullName -Raw -Encoding UTF8 | ConvertFrom-Json
if ($Manifest.schema -ne 2) {
    throw "Expected corpus manifest schema 2, found $($Manifest.schema)"
}
if (-not $Manifest.leafExtraction.enabled) {
    throw "Leaf extraction is not marked enabled in the manifest"
}

$LfsExtensions = @(".mix", ".yro", ".map", ".shp", ".vxl", ".hva", ".tmp", ".pal", ".csf")
$ExpectedLfsCount = @(
    $Manifest.entries | Where-Object {
        $LfsExtensions -contains [System.IO.Path]::GetExtension([string]$_.path).ToLowerInvariant()
    }
).Count
$ActualLfsFiles = @(git lfs ls-files --name-only)
Assert-LastExitCode "git lfs ls-files"
if ($ActualLfsFiles.Count -ne $ExpectedLfsCount) {
    throw "Git LFS coverage mismatch: manifest expects $ExpectedLfsCount LFS-managed files, but git lfs ls-files reports $($ActualLfsFiles.Count)."
}

git diff --cached --quiet
if ($LASTEXITCODE -eq 0) {
    throw "Expanded corpus produced no staged changes. Nothing to publish."
}
if ($LASTEXITCODE -ne 1) {
    throw "git diff --cached --quiet failed with exit code $LASTEXITCODE"
}

Write-Host "----- expanded corpus summary -----"
Write-Host ("Entries: {0}; payload: {1:N3} GiB; LFS files: {2}" -f $Manifest.entryCount, ($Manifest.totalBytes / 1GB), $ActualLfsFiles.Count)
Write-Host ("Scanned MIX archives: {0}" -f $Manifest.leafExtraction.archiveCount)
Write-Host ("Resolved leaf assets added: {0}" -f $Manifest.leafExtraction.resolvedLeafAssetsAdded)
Write-Host ("Resolved nested MIX added: {0}" -f $Manifest.leafExtraction.resolvedNestedMixAdded)
Write-Host ("Unresolved MIX hashes recorded: {0}" -f $Manifest.leafExtraction.unresolvedHashCount)

Write-Host "----- staged diff summary -----"
git diff --cached --stat
Assert-LastExitCode "git diff --cached --stat"

git commit -m "assets: expand RA2YR resolved leaf compatibility corpus"
Assert-LastExitCode "git commit expanded corpus"

$LfsUploaded = $false
for ($Attempt = 1; $Attempt -le 5; $Attempt++) {
    Write-Host "Git LFS upload attempt $Attempt/5"
    git lfs push --all origin $Branch
    if ($LASTEXITCODE -eq 0) {
        $LfsUploaded = $true
        break
    }
    if ($Attempt -lt 5) {
        Write-Warning "Git LFS upload failed; retrying without rebuilding the corpus."
        Start-Sleep -Seconds (5 * $Attempt)
    }
}
if (-not $LfsUploaded) {
    throw "Git LFS upload failed after 5 attempts. Do not rebuild; rerun git lfs push --all origin $Branch later."
}

$GitPushed = $false
for ($Attempt = 1; $Attempt -le 3; $Attempt++) {
    Write-Host "Git branch push attempt $Attempt/3"
    git push -u origin $Branch
    if ($LASTEXITCODE -eq 0) {
        $GitPushed = $true
        break
    }
    if ($Attempt -lt 3) {
        Start-Sleep -Seconds (5 * $Attempt)
    }
}
if (-not $GitPushed) {
    throw "Git branch push failed after 3 attempts. LFS objects may already be uploaded; retry only git push -u origin $Branch."
}

Write-Host "Expanded corpus branch pushed: $Branch"
Write-Host "Next gate: inspect resolution reports, then fresh-clone selected leaf assets and verify SHA-256."
