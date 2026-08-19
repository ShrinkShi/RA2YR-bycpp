param(
    [Parameter(Mandatory = $true)]
    [string]$SourceDir,

    [string]$Branch = "agent/ra2yr-corpus-lfs"
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
    throw "Git LFS is not installed or not available. Install Git LFS first, then rerun this script."
}

# Do not obtain the repository path from `git rev-parse` here. Windows PowerShell
# 5.1 can decode UTF-8 native-process output using the active legacy code page,
# which corrupts non-ASCII paths. Resolve from this script's own filesystem path
# instead; PowerShell/.NET already holds PSScriptRoot as a Unicode string.
$RepoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\.."))
if (-not (Test-Path -LiteralPath (Join-Path $RepoRoot ".git"))) {
    throw "Could not resolve the RA2YR-bycpp repository root from the script location: $RepoRoot"
}
Set-Location -LiteralPath $RepoRoot

$Remote = (git remote get-url origin).Trim()
Assert-LastExitCode "git remote get-url origin"
if ($Remote -notmatch "ShrinkShi/RA2YR-bycpp") {
    throw "Unexpected origin remote: $Remote"
}

if ((git status --porcelain)) {
    throw "Working tree is not clean. Commit or stash unrelated work before importing the corpus."
}

git fetch origin main
Assert-LastExitCode "git fetch origin main"
git switch main
Assert-LastExitCode "git switch main"
git pull --ff-only origin main
Assert-LastExitCode "git pull --ff-only origin main"

# `git branch --list` intentionally emits no output when the branch does not
# exist. Windows PowerShell represents that as $null, so calling .Trim() on the
# command result throws InvokeMethodOnNull. Materialize the result as an array
# and check Count instead.
$ExistingBranch = @(git branch --list $Branch)
Assert-LastExitCode "git branch --list corpus branch"
if ($ExistingBranch.Count -gt 0) {
    git branch -D $Branch
    Assert-LastExitCode "delete existing local corpus branch"
}
git switch -c $Branch
Assert-LastExitCode "create corpus branch"

git lfs install --local
Assert-LastExitCode "git lfs install --local"

$Builder = Join-Path $PSScriptRoot "build_corpus.py"
py $Builder --source $SourceDir --repo-root $RepoRoot
Assert-LastExitCode "build corpus"

# The working tree was required to be clean before generation, so staging all
# generated changes is safer than embedding a non-ASCII repository path in this
# Windows PowerShell 5.1 script.
git add -A
Assert-LastExitCode "git add -A"

$ManifestFile = Get-ChildItem -LiteralPath $RepoRoot -Recurse -File -Filter "corpus-manifest.json" | Select-Object -First 1
if (-not $ManifestFile) {
    throw "corpus-manifest.json was not generated"
}
$Manifest = Get-Content -LiteralPath $ManifestFile.FullName -Raw -Encoding UTF8 | ConvertFrom-Json

$LfsExtensions = @(".mix", ".yro", ".map", ".shp", ".vxl", ".hva", ".tmp", ".pal", ".csf")
$ExpectedLfsCount = @(
    $Manifest.entries | Where-Object {
        $LfsExtensions -contains [System.IO.Path]::GetExtension([string]$_.path).ToLowerInvariant()
    }
).Count
# Validate the staged snapshot rather than HEAD/working-tree discovery.
$ValidationTree = (git write-tree).Trim()
Assert-LastExitCode "git write-tree for LFS validation"

$ValidationCommit = (git commit-tree $ValidationTree -p HEAD -m "temporary corpus LFS validation").Trim()
Assert-LastExitCode "git commit-tree for LFS validation"

$ActualLfsFiles = @(git lfs ls-files --name-only $ValidationCommit)
Assert-LastExitCode "git lfs ls-files staged snapshot"

if ($ActualLfsFiles.Count -ne $ExpectedLfsCount) {
    throw "Git LFS coverage mismatch: manifest expects $ExpectedLfsCount LFS-managed files, but staged snapshot reports $($ActualLfsFiles.Count)."
}

git lfs fsck --pointers $ValidationCommit
Assert-LastExitCode "git lfs fsck staged pointers"

Write-Host "----- git lfs status -----"
git lfs status
Assert-LastExitCode "git lfs status"

Write-Host ("Entries: {0}; payload: {1:N3} GiB; LFS files: {2}" -f $Manifest.entryCount, ($Manifest.totalBytes / 1GB), $ActualLfsFiles.Count)

if ($Manifest.entryCount -ne 126) {
    throw "Unexpected corpus entry count: $($Manifest.entryCount); expected 126 for the audited sample."
}
if ($Manifest.totalBytes -ne 1269875373) {
    throw "Unexpected corpus payload size: $($Manifest.totalBytes); expected 1269875373 bytes for the audited sample."
}

git commit -m "assets: add RA2YR 1.001 compatibility corpus via Git LFS"
Assert-LastExitCode "git commit corpus"

# Git LFS uploads binary objects before Git updates the remote branch ref.
git push -u origin $Branch
Assert-LastExitCode "git push corpus branch"

Write-Host "Corpus branch pushed: $Branch"
Write-Host "Verify a fresh clone can materialize all LFS objects before merging into main."
